/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (2026-11-26) the held step's padlock (v2.c, 7 x 9) somewhere on a packed screen (rfm_move_screen's: a byte a column
 * of 8 rows): its pixels lit, its two holes dark */
#ifndef RFM_TEST_PADLOCK_H
#define RFM_TEST_PADLOCK_H
#include <stdint.h>
static int pl_px(const uint8_t *s, int x, int y) { return x >= 0 && x < 128 && y >= 0 && y < 64 && (s[(y >> 3) * 128 + x] >> (y & 7) & 1); }
static int scr_padlock(const uint8_t *s)
{
    static const uint8_t P[9] = { 0x1C, 0x22, 0x22, 0x7F, 0x7F, 0x77, 0x77, 0x7F, 0x7F };
    int x, y, i, j, ok;
    for (y = -1; y <= 64 - 9; y++)
        for (x = 0; x <= 128 - 7; x++) {
            for (ok = 1, j = 0; j < 9 && ok; j++)
                for (i = 0; i < 7 && ok; i++)
                    if ((P[j] >> (6 - i) & 1) && !pl_px(s, x + i, y + j))
                        ok = 0;
            if (ok && !pl_px(s, x + 3, y + 1) && !pl_px(s, x + 3, y + 2) && !pl_px(s, x + 3, y + 5))
                return 1;
        }
    return 0;
}
static int scr_subset(const uint8_t *a, const uint8_t *b, int n)   /* every pixel lit in a lit in b */
{
    int i;
    for (i = 0; i < n; i++)
        if (a[i] & ~b[i])
            return 0;
    return 1;
}
#endif
