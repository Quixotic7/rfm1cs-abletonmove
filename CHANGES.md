# Changes

Versions are git tags (`vMAJOR.MINOR.PATCH`); hosts take this repository as a submodule pinned to one.
`RFM1CS_VERSION` in `include/rfm1cs/move.h` and `rfm_move_version()` name it.

## 0.3.0 (2026-11-18)

What the iOS and Android ports asked for. Behaviour is unchanged otherwise.

**API changes (a host must update its calls):**

- `rfm1cs_session_opened(s, now, attempt)`, `rfm1cs_session_rx(s, now, attempt, bytes, n)`,
  `rfm1cs_session_closed(s, now, attempt, final)`: the attempt id `RFM1CS_EV_OPEN` now carries (`ev.attempt`, a new
  one each try, never 0; `rfm1cs_session_attempt()` reads the current one). A call with an older try's id is ignored:
  a socket that closes late can no longer end the next try. wasm: `mw_sess_opened/_rx/_closed` take it after `now`;
  `move-core.js`: `{ type: "open", attempt }`, `sessOpened(now, attempt)`, `sessRx(now, attempt, bytes)`,
  `sessClosed(now, attempt, final)`.
- `rfm1cs_status_t` and `rfm1cs_session_t` have new fields at their ends (`lost_why`, `bye`, `gone_live`; `attempt`,
  the thumbnail's pacing): rebuild, do not mix objects from 0.2.0.
- `move-core.js` `PROJECT_MAX` is 39200, `RFM_MOVE_PROJECT_MAX` as the C has it (it said 39072, a stale copy: blobs
  of 39073..39200 bytes were refused by `projectCheck` and by the web's and Android's file readers).

**New:**

- `store.h`: the project file every host wrote itself (`.rfmmove` / `.rfmmoveset`, "RMVP"):
  `rfm1cs_file_pack`, `rfm1cs_file_unpack` (every blob checked), `rfm1cs_file_check`, `rfm1cs_file_size`,
  `rfm1cs_file_error_text` (the web player's words; "this page" or "this app"), `RFM1CS_FILE_MAX`. The web's, the
  iOS app's and the Android app's own writers made byte-identical files; `tests/rmvp/` keeps one pair from each, and
  the `session` test reads and repacks them. wasm `mw_file_*`; `move-core.js` `filePack()`, `fileUnpack()`.
- `session.h`: the words to show, from the web player: `rfm1cs_status_text()` (the status line and its tone, from
  the state and how the link last went), `rfm1cs_why_text()`, `rfm1cs_lost_text()`, `rfm1cs_bye_text()`,
  `rfm1cs_words_t`. wasm `mw_sess_text`; `move-core.js` `sessText()`, `whyText()`, `lostText()`, `byeText()`.
- `posix_tcp.h`: `rfm1cs_tcp_connect()` takes `host:port`, `[v6]:port`, `[v6]` or a bare IPv6 address
  (`rfm1cs_tcp_parse()`; the address's own port, else the argument, else 7720). New ctest `tcp`.
- `rfm1cs_session_frame()`: called again within 33 ms with the same pixels, the thumbnail is not rebuilt (an FNV-1a
  of the pixels decides); call it at most every 33 ms.
- README.md, "Threads": a session shared by a network thread and an engine thread under one mutex (actions come out
  on the network thread: queue them), and a network thread driving it alone while the engine is stopped.

## 0.2.0 (2026-11-18)

The link written once, for every host.

- `session.h` / `session.c`: the Move Remote link, transport-free (C99, no allocation, no OS calls, no threads):
  the web player's MoveLink logic (HELLO, CONFIG, ping 1 s, silence 3 s, HELLO wait 4 s, retries 1/2/4 s, BYE, LED
  flushing, the screen paced at 33 ms, release on loss, LEDs invalidated on a new connection), plus what iOS and
  Android had: `MR_VIA_*` as the Move says it, PLAY green as a level, a status struct with counters, project and
  settings requests as actions (the settings image attached). A patient wait (a browser asking about local network
  access), `give_up` before the first connection, a final close.
- `store.h` / `store.c`: the project actions as slot reads, writes and deletes: the cut / copy / paste buffer, the
  colour (byte 18 and its FNV-1a check), the used bits; reads answered at once or later, in order.
- `posix_tcp.h` / `posix_tcp.c` (optional): non-blocking TCP for native hosts.
- `rfm_move_thumb()`: the FM-1's screen into the thumbnail now (the session draws later).
- wasm: `mw_sess_*`, `mw_store_*` (the store's I/O to JS through `Module.rfmHosts`); `move-core.js`: `sess*`,
  `store*`, `STATE`, `WHY`, `CH`.
- CMake: `rfm1cs_session`, `rfm1cs_tcp`; tests `session` (a fake Move), `session_e2e` (the simulator over TCP).

## 0.1.0 (2026-11-18)

The Ableton Move core moved out of RemoteFM1 (`host/src/move/`, `host/include/rfm/move.h`, the Move tests,
`web/move/`, `web/site/js/move-core.js`, `move-seq.js`, the vendored Move Remote client) with its git history, as of
RemoteFM1 a3946f6. No change in behaviour.

- The header is `rfm1cs/move.h` (`rfm/move.h` still works); the `rfm_move_*` names are unchanged.
- `rfm_move_version()`, `RFM1CS_VERSION`; wasm `mw_version()`, `MoveCore.version()`.
- CMake: the `rfm1cs_move` library for `add_subdirectory()`; the tests when top level.
- `web/build.sh [OUT_DIR]` builds `rfm_move.mjs` (default `build/web/`).
