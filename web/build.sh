#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# rfm_move for the browser: one single-file ES module (the .wasm inlined) that loads in a page, a Worker and Node,
# with the core (src/), the vendored Move Remote client's mr_decode, its 1-bit graphics and font, and web/move_web.c's
# flat API. web/move-core.js wraps it.
#   web/build.sh [OUT_DIR]      (default: build/web/; Emscripten from ~/emsdk, or emcc on PATH as in CI)
# Writes OUT_DIR/rfm_move.mjs.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/build/web}
if ! command -v emcc >/dev/null 2>&1; then
    . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
fi
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cd "$ROOT"
MR=third_party/moveremote
emcc -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter -I"$ROOT/include" -I"$ROOT/$MR/include" \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node -sSINGLE_FILE=1 -sFILESYSTEM=0 \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=1MB -sSTACK_SIZE=128KB -sEXPORT_NAME=createRfmMove \
    -sEXPORTED_FUNCTIONS=_malloc,_free -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP32,stringToUTF8,lengthBytesUTF8 \
    src/move.c src/screen.c src/v2.c src/seq.c src/seq_ui.c src/project.c src/session.c src/store.c "$MR/host/mr_client.c" "$MR/host/mr_gfx.c" "$MR/common/font5x7.c" web/move_web.c \
    -o "$OUT/rfm_move.mjs"
ls -l "$OUT/rfm_move.mjs"
