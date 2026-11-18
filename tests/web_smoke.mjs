// SPDX-License-Identifier: MPL-2.0
// The wasm build in Node: the module loads, gives its version, maps a pad and draws a screen.
//   node tests/web_smoke.mjs [path/to/rfm_move.mjs]      (default build/web/rfm_move.mjs)
import { pathToFileURL, fileURLToPath } from "node:url";
import { resolve, dirname } from "node:path";
import { MoveCore, packet, PROJECT_MAX, FILE_MAX } from "../web/move-core.js";

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
core.free();
console.log(`web_smoke: ${checks} checks, ${fails} failed`);
process.exit(fails ? 1 : 0);
