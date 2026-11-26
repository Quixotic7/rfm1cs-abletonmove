/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (2026-11-26) the tests that make an older project from a saved one: a version 16 blob as version 15 (a note 7 bytes,
 * no kit semitones, the kit header's octave byte 0), its check sum again. n the new length */
#ifndef RFM_TEST_PROJ15_H
#define RFM_TEST_PROJ15_H
#include <stdint.h>
#include <string.h>
#include "rfm1cs/move.h"

/* (2026-11-26) a kit part as version 15 / 16 had it: 45 settings a pad (Scan dropped), in place; its new size */
static inline uint32_t kit_v15(uint8_t *k)
{
    int pad;
    uint8_t *o = k + 16 + 24;
    const uint8_t *x = k + 16 + 24;
    for (pad = 0; pad < RFM_MOVE_KIT_PADS; pad++, o += RFM_MOVE_KIT_PATH + 2 * RFM_MOVE_KIT_P_N_V15,
        x += RFM_MOVE_KIT_PATH + 2 * RFM_MOVE_KIT_P_N)
        memmove(o, x, RFM_MOVE_KIT_PATH + 2 * RFM_MOVE_KIT_P_N_V15);
    k[13] = 0;                                          /* (Bits' tenths flag) */
    return RFM_MOVE_KIT_BYTES_V15;
}
static inline void proj_sum(uint8_t *b, uint32_t n)
{
    uint32_t i, h;
    for (h = 2166136261u, i = 0; i < n - 4; i++)
        h = (h ^ b[i]) * 16777619u;
    for (i = 0; i < 4; i++)
        b[n - 4 + i] = (uint8_t)(h >> (8 * i));
}
/* (2026-11-26) a version 18 blob as version 17: the pads' original places (the kit part's last 16 paths) gone */
static inline uint32_t proj_v17(uint8_t *b, uint32_t n)
{
    const uint32_t o = RFM_MOVE_KIT_PADS * RFM_MOVE_KIT_PATH;
    if (b[4] < 18 || n < o + 4)
        return n;
    memmove(b + n - 4 - o, b + n - 4, 4);
    n -= o;
    b[4] = 17;
    proj_sum(b, n);
    return n;
}
static inline uint32_t proj_v15(uint8_t *b, uint32_t n)
{
    const uint32_t off = 24 + RFM_MOVE_SEQ_TRACKS * 4 + RFM_MOVE_SEQ_CLIPS * 2;   /* (project.c OFF_NOTES) */
    uint32_t nn, i, h;
    if (n < off + 2 || b[4] < 16)
        return n;
    n = proj_v17(b, n);
    if (b[4] >= 17) {                                   /* (2026-11-26: the kit part's 46 settings a pad down to 45) */
        uint8_t *k = b + n - 4 - RFM_MOVE_KIT_BYTES_V17;
        kit_v15(k);
        memmove(k + RFM_MOVE_KIT_BYTES_V15, b + n - 4, 4);
        n -= RFM_MOVE_KIT_BYTES_V17 - RFM_MOVE_KIT_BYTES_V15;
    }
    nn = (uint32_t)b[off] | (uint32_t)b[off + 1] << 8;
    for (i = 0; i < nn; i++)                            /* (note i's 7 bytes to its place, its 8th gone) */
        memmove(b + off + 2 + i * 7, b + off + 2 + i * 8, 7);
    memmove(b + off + 2 + nn * 7, b + off + 2 + nn * 8, n - (off + 2 + nn * 8));
    n -= nn;
    b[4] = 15;
    b[n - 4 - RFM_MOVE_KIT_BYTES_V15 + 12] = 0, b[n - 4 - RFM_MOVE_KIT_BYTES_V15 + 13] = 0;   /* (16's octave, Bits' flag) */
    for (h = 2166136261u, i = 0; i < n - 4; i++)
        h = (h ^ b[i]) * 16777619u;
    for (i = 0; i < 4; i++)
        b[n - 4 + i] = (uint8_t)(h >> (8 * i));
    return n;
}
#endif
