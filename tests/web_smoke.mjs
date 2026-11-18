// SPDX-License-Identifier: MPL-2.0
// The wasm build in Node: the module loads, gives its version, maps a pad and draws a screen.
//   node tests/web_smoke.mjs [path/to/rfm_move.mjs]      (default build/web/rfm_move.mjs)
import { pathToFileURL, fileURLToPath } from "node:url";
import { resolve, dirname } from "node:path";
import { MoveCore, packet } from "../web/move-core.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const mod = resolve(process.argv[2] || `${root}/build/web/rfm_move.mjs`);
let checks = 0, fails = 0;
const ok = (c, what) => { checks++; if (!c) { fails++; console.error(`FAIL: ${what}`); } };
const core = await MoveCore.load(null, pathToFileURL(mod).href);
const want = /#define RFM1CS_VERSION "([^"]+)"/.exec((await import("node:fs")).readFileSync(`${root}/include/rfm1cs/move.h`, "utf8"))[1];
ok(core.version() === want, `version ${core.version()} = ${want}`);
const acts = core.input(packet(0x90, 68, 100), 0, 0);        // the first pad (note 68) down
ok(acts.some((a) => a.kind === "key" && a.down), "a pad gives a key down");
ok(core.screen(null).length === 1024, "the screen is 1024 bytes");
core.free();
console.log(`web_smoke: ${checks} checks, ${fails} failed`);
process.exit(fails ? 1 : 0);
