# Changes

Versions are git tags (`vMAJOR.MINOR.PATCH`); hosts take this repository as a submodule pinned to one.
`RFM1CS_VERSION` in `include/rfm1cs/move.h` and `rfm_move_version()` name it.

## 0.1.0 (2026-11-18)

The Ableton Move core moved out of RemoteFM1 (`host/src/move/`, `host/include/rfm/move.h`, the Move tests,
`web/move/`, `web/site/js/move-core.js`, `move-seq.js`, the vendored Move Remote client) with its git history, as of
RemoteFM1 a3946f6. No change in behaviour.

- The header is `rfm1cs/move.h` (`rfm/move.h` still works); the `rfm_move_*` names are unchanged.
- `rfm_move_version()`, `RFM1CS_VERSION`; wasm `mw_version()`, `MoveCore.version()`.
- CMake: the `rfm1cs_move` library for `add_subdirectory()`; the tests when top level.
- `web/build.sh [OUT_DIR]` builds `rfm_move.mjs` (default `build/web/`).
