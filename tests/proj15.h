/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (2026-11-26) the tests that make an older project from a saved one: a version 16 blob as version 15 (a note 7 bytes,
 * no kit semitones, the kit header's octave byte 0), its check sum again. n the new length */
#ifndef RFM_TEST_PROJ15_H
#define RFM_TEST_PROJ15_H
#include <stdint.h>
#include <string.h>
#include "rfm1cs/move.h"

static inline uint32_t proj_v15(uint8_t *b, uint32_t n)
{
    const uint32_t off = 24 + RFM_MOVE_SEQ_TRACKS * 4 + RFM_MOVE_SEQ_CLIPS * 2;   /* (project.c OFF_NOTES) */
    uint32_t nn, i, h;
    if (n < off + 2 || b[4] != 16)
        return n;
    nn = (uint32_t)b[off] | (uint32_t)b[off + 1] << 8;
    for (i = 0; i < nn; i++)                            /* (note i's 7 bytes to its place, its 8th gone) */
        memmove(b + off + 2 + i * 7, b + off + 2 + i * 8, 7);
    memmove(b + off + 2 + nn * 7, b + off + 2 + nn * 8, n - (off + 2 + nn * 8));
    n -= nn;
    b[4] = 15;
    b[n - 4 - RFM_MOVE_KIT_BYTES + 12] = 0, b[n - 4 - RFM_MOVE_KIT_BYTES + 13] = 0;   /* (16's octave, Bits' tenths flag) */
    for (h = 2166136261u, i = 0; i < n - 4; i++)
        h = (h ^ b[i]) * 16777619u;
    for (i = 0; i < 4; i++)
        b[n - 4 + i] = (uint8_t)(h >> (8 * i));
    return n;
}
#endif
