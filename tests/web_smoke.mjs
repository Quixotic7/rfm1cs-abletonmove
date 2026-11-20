// SPDX-License-Identifier: MPL-2.0
// The wasm build in Node: the module loads, gives its version, maps a pad and draws a screen.
//   node tests/web_smoke.mjs [path/to/rfm_move.mjs]      (default build/web/rfm_move.mjs)
import { pathToFileURL, fileURLToPath } from "node:url";
import { resolve, dirname } from "node:path";
import { MoveCore, packet, PROJECT_MAX, FILE_MAX, SETTINGS_BYTES, FX } from "../web/move-core.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const mod = resolve(process.argv[2] || `${root}/build/web/rfm_move.mjs`);
let checks = 0, fails = 0;
const ok = (c, what) => { checks++; if (!c) { fails++; console.error(`FAIL: ${what}`); } };
const core = await MoveCore.load(null, pathToFileURL(mod).href);
const want = /#define RFM1CS_VERSION "([^"]+)"/.exec((await import("node:fs")).readFileSync(`${root}/include/rfm1cs/move.h`, "utf8"))[1];
ok(core.version() === want, `version ${core.version()} = ${want}`);
ok(core.M._mw_project_max() === PROJECT_MAX && core.M._mw_file_max() === FILE_MAX, `PROJECT_MAX ${PROJECT_MAX}, FILE_MAX ${FILE_MAX} as the C has them`);
const acts = core.input(packet(0x90, 68, 100), 0, 0);        // the first pad (note 68) down
ok(acts.some((a) => a.kind === "key" && a.down), "a pad gives a key down");
ok(core.screen(null).length === 1024, "the screen is 1024 bytes");
ok(core.fwCapsGet() === 3, "(0.5.10) caps: three slots and FX by default");
core.fwCaps(0);
ok(core.fwCapsGet() === 0, "(0.5.10) caps 0: one firmware, no FX");
core.fwCaps(3);
// the session: start asks for the transport, opened says HELLO
const a = core.sessStart(1000), open = a.evs.find((e) => e.type === "open");
ok(open && open.attempt > 0 && open.attempt === core.sessAttempt() && core.sessStatus().state === 1, "the session: start -> open (an attempt), CONNECTING");
ok(core.sessText({ host: "move.local" }).text === "Connecting to move.local..." && core.sessText({}).tone === "busy", "the words: connecting");
ok(core.sessOpened(1000, open.attempt + 1).evs.length === 0, "an older (or other) attempt's open: ignored");
const b = core.sessOpened(1000, open.attempt), hello = b.evs.find((e) => e.type === "send");
ok(hello && hello.bytes[2] === 0x81 && hello.bytes.length === 37, "opened -> HELLO");
ok(core.sessNextMs(1000) === 4000, "the HELLO's wait: 4 s");
const c = core.sessTick(5000);
ok(c.evs.some((e) => e.type === "lost" && e.why === 2) && core.sessStatus().state === 3, "no HELLO: lost, retrying");
const words = { host: "move.local" };
ok(core.sessText(words).text === "No answer from move.local: trying again in 1 s..." && core.whyText(3, words) === "the Move went quiet"
   && core.lostText(2, false, words) === "The Move: link lost (no answer from move.local)." && core.byeText(1, false).text === "The Move left Move Remote (its exit gesture).",
   "the words: retrying, why, the LOST toast, BYE");
// the project file: pack, unpack, refused in the web player's words
{
  const blob = core.projectSave(), f = core.filePack([{ slot: 3, saved: 1794909600, bytes: blob }]);
  const u = core.fileUnpack(f), bad = core.fileUnpack(f.slice(0, f.length - 1)), app = core.fileUnpack(Uint8Array.from([...f.subarray(0, 4), 2, ...f.subarray(5)]), "app");
  ok(f.length === 18 + blob.length && u.kind === 1 && u.entries[0].slot === 3 && u.entries[0].saved === 1794909600 && u.entries[0].bytes.every((x, i) => x === blob[i]),
     "the project file: a round trip");
  ok(bad.error === "Not a Move project file: project 1 is cut off." && app.error === "Not a Move project file: version 2; this app reads version 1."
     && core.fileUnpack(new Uint8Array(0)).error === "Not a Move project file: it does not start with RMVP." && core.fileUnpack(null).error === "Not a Move project file: nothing to read.",
     "the project file: refused with a reason");
}
core.sessStop(5000);
// (0.4.0) the firmware browser: a list built in wasm memory, the projects view, a load, the library blob, FX setup
{
  ok(core.M._mw_settings_bytes() === SETTINGS_BYTES && core.save().length === SETTINGS_BYTES, `SETTINGS_BYTES ${SETTINGS_BYTES}`);
  const thumb = new Uint8Array(512).fill(0xAA);
  core.fwList([{ id: "alpha", name: "Alpha", version: "1.0", installed: "1.0", desc: "The first.", thumb, cats: 1, cat: 0 },
               { id: "beta", name: "Beta", version: "2.0", installed: null, cats: 4, cat: 2 }]);
  core.fwVoices(1);
  const io = (st, d1, d2) => core.input(packet(st, d1, d2), 0, 0);
  io(0xB0, 43, 127); io(0x90, 16, 127); io(0x80, 16, 0); io(0xB0, 43, 0);   // Track 1 + step 1: the projects view
  io(0x90, 17, 127); io(0x80, 17, 0);                                       // step 2: All (Alpha)
  io(0x90, 92, 100); io(0x80, 92, 0);                                       // the top-left pad: Alpha
  io(0xB0, 118, 127); io(0xB0, 118, 0);                                    // Sampling: a favourite
  const load = [...io(0xB0, 3, 127), ...io(0xB0, 3, 0)].find((a) => a.kind === "fwLoad");
  ok(load && load.slot === 0 && load.fw === 0, "a jog press: fwLoad slot 0, firmware 0");
  core.fwProgress("alpha", 100);
  ok(core.fwSlots(["alpha", null, null]).length === 0 && core.fwSounding() === 1 && core.fwActive() === 0, "fwSlots, the sounding slot");
  const lib = core.fwLibrarySave(), flush = core.fwFlush().find((a) => a.kind === "fwLibrary");
  ok(lib && lib.length > 256 && flush && flush.bytes.length === lib.length, "the library blob (fwLibrarySave, fwFlush's fwLibrary)");
  ok(core.fwLibrary(lib) === 0 && core.fwLibrary(lib.slice(0, lib.length - 1)) === -1, "the library blob loads; a cut one is refused");
  ok(lib[9] === 1 && lib[11] === 1 && String.fromCharCode(...lib.subarray(256, 261)) === "alpha", "the library: Alpha, a favourite");
  core.fwLibrary(lib);
  core.fxModules(FX.AUDIO, [{ name: "Freeverb", params: [{ name: "Size", def: 64 }, { name: "Mix", def: 127, dryWet: true }] },
                            { name: "Delay", params: [{ name: "Time", words: ["1/8", "1/4"], def: 0 }] }]);
  const sync = core.fxSync();
  ok(sync.filter((a) => a.kind === "fxLoad").length === 6 && sync.filter((a) => a.kind === "mix").length === 3 &&
     sync.some((a) => a.kind === "route" && a.fw[0] === 0xFFFF), "fxSync: fxLoad x6, mix x3, route");
  io(0xB0, 50, 127); io(0xB0, 50, 0);                                       // Session: FX setup
  io(0xB0, 41, 127); io(0xB0, 41, 0);                                       // Track 3: Audio FX
  io(0xB0, 14, 1);                                                          // the jog: the module list
  const fxl = [...io(0xB0, 3, 127), ...io(0xB0, 3, 0)].find((a) => a.kind === "fxLoad");
  ok(fxl && fxl.fx === FX.AUDIO && fxl.slot === 0 && fxl.module === 0, "Audio FX: a module loaded");
  ok(core.screen(null).length === 1024, "FX setup's screen");
  io(0xB0, 50, 127); io(0xB0, 51, 127);                                     // Session, Back: out
}
core.free();
console.log(`web_smoke: ${checks} checks, ${fails} failed`);
process.exit(fails ? 1 : 0);
