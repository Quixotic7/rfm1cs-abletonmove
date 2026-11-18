/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm1cs_store (store.h): the project actions as slot I/O. The order is the web player's (RemoteFM1 move-link.js,
 * a3946f6): a cut's read then its delete; a paste after the reads asked before it; a colour read, changed, written. */
#include "rfm1cs/store.h"

#include <string.h>

void rfm1cs_project_set_color(uint8_t *b, uint32_t len, int color)
{
    uint32_t h = 2166136261u, i, n;
    if (!b || len < RFM1CS_COLOR_BYTE + 1 + 4)
        return;
    n = len - 4;
    b[RFM1CS_COLOR_BYTE] = (uint8_t)color;
    for (i = 0; i < n; i++)
        h = (h ^ b[i]) * 16777619u;
    b[n] = (uint8_t)h, b[n + 1] = (uint8_t)(h >> 8), b[n + 2] = (uint8_t)(h >> 16), b[n + 3] = (uint8_t)(h >> 24);
}

int rfm1cs_first_free(uint32_t used)
{
    int s;
    for (s = 0; s < RFM1CS_SLOTS; s++)
        if (!(used >> s & 1))
            return s;
    return -1;
}

void rfm1cs_store_init(rfm1cs_store_t *st, const rfm1cs_store_io_t *io, uint32_t used)
{
    memset(st, 0, sizeof *st);
    if (io)
        st->io = *io;
    st->used = used;
}

static void do_write(rfm1cs_store_t *st, int slot, const uint8_t *b, uint32_t n)
{
    if (!st->io.write || st->io.write(st->io.ctx, slot, b, n) != 0)
        return;
    st->used |= 1u << slot;
    if (st->io.stored)
        st->io.stored(st->io.ctx, slot, b, n, st->used);
}
static void do_remove(rfm1cs_store_t *st, int slot)
{
    if (st->io.remove)
        st->io.remove(st->io.ctx, slot);
    st->used &= ~(1u << slot);
    if (st->io.stored)
        st->io.stored(st->io.ctx, slot, NULL, 0, st->used);
}
static void paste_now(rfm1cs_store_t *st, int slot)
{
    if (st->clip_len)
        do_write(st, slot, st->clip, st->clip_len);
}
/* a read's answer for this kind */
static void finish(rfm1cs_store_t *st, int kind, int slot, int color, const uint8_t *b, uint32_t n)
{
    if (n > RFM_MOVE_PROJECT_MAX)
        b = NULL, n = 0;
    if (!b)
        n = 0;
    switch (kind) {
    case RFM_MOVE_ACT_PROJECT_LOAD:
        if (st->io.loaded)
            st->io.loaded(st->io.ctx, slot, n ? b : NULL, n);
        break;
    case RFM_MOVE_ACT_PROJECT_CUT:
    case RFM_MOVE_ACT_PROJECT_COPY:
        if (n) {
            memmove(st->clip, b, n);
            st->clip_len = n;
        }
        break;
    case RFM_MOVE_ACT_PROJECT_COLOR:
        if (n) {
            if (b != st->buf)
                memmove(st->buf, b, n);
            rfm1cs_project_set_color(st->buf, n, color);
            do_write(st, slot, st->buf, n);
        }
        break;
    default: break;
    }
}
static void push(rfm1cs_store_t *st, int kind, int slot, int color, int read)
{
    const int i = (st->qhead + st->qn++) % RFM1CS_STORE_QUEUE;
    st->q[i].kind = (uint8_t)kind, st->q[i].slot = (uint8_t)slot, st->q[i].color = (uint8_t)color;
    st->q[i].read = (uint8_t)read;
}
/* the pastes now at the head (their reads before them answered) */
static void drain(rfm1cs_store_t *st)
{
    while (st->qn && !st->q[st->qhead].read) {
        const int slot = st->q[st->qhead].slot;
        st->qhead = (st->qhead + 1) % RFM1CS_STORE_QUEUE, st->qn--;
        paste_now(st, slot);
    }
}
static int ask_read(rfm1cs_store_t *st, int kind, int slot, int color)
{
    int r;
    if (!st->io.read || st->qn >= RFM1CS_STORE_QUEUE)
        return -1;
    push(st, kind, slot, color, 1);
    r = st->io.read(st->io.ctx, slot, st->buf, RFM_MOVE_PROJECT_MAX);
    if (r == RFM1CS_STORE_PENDING)
        return 1;
    st->qn--;                                           /* (answered at once: not queued after all) */
    finish(st, kind, slot, color, r > 0 ? st->buf : NULL, r > 0 ? (uint32_t)r : 0);
    return 1;
}

int rfm1cs_store_request(rfm1cs_store_t *st, int kind, int slot, int value, const uint8_t *data, uint32_t len)
{
    if (kind != RFM_MOVE_ACT_PROJECT_SAVE && kind != RFM_MOVE_ACT_PROJECT_LOAD && kind != RFM_MOVE_ACT_PROJECT_DELETE &&
        kind != RFM_MOVE_ACT_PROJECT_CUT && kind != RFM_MOVE_ACT_PROJECT_COPY && kind != RFM_MOVE_ACT_PROJECT_PASTE &&
        kind != RFM_MOVE_ACT_PROJECT_COLOR)
        return 0;
    if (slot < 0 || slot >= RFM1CS_SLOTS)
        return -1;
    switch (kind) {
    case RFM_MOVE_ACT_PROJECT_SAVE:
        if (!data || !len || len > RFM_MOVE_PROJECT_MAX)
            return -1;
        do_write(st, slot, data, len);
        return 1;
    case RFM_MOVE_ACT_PROJECT_DELETE: do_remove(st, slot); return 1;
    case RFM_MOVE_ACT_PROJECT_LOAD:
    case RFM_MOVE_ACT_PROJECT_COPY: return ask_read(st, kind, slot, 0);
    case RFM_MOVE_ACT_PROJECT_COLOR: return ask_read(st, kind, slot, value & 255);
    case RFM_MOVE_ACT_PROJECT_CUT:
        if (ask_read(st, kind, slot, 0) < 0)
            return -1;
        do_remove(st, slot);                            /* (asked after the read: a queued host reads it first) */
        return 1;
    case RFM_MOVE_ACT_PROJECT_PASTE:
        if (st->qn) {
            if (st->qn >= RFM1CS_STORE_QUEUE)
                return -1;
            push(st, kind, slot, 0, 0);
        } else {
            paste_now(st, slot);
        }
        return 1;
    default: return 0;
    }
}

void rfm1cs_store_answer(rfm1cs_store_t *st, int slot, const uint8_t *b, uint32_t n)
{
    int kind, color;
    (void)slot;
    if (!st->qn || !st->q[st->qhead].read)
        return;
    kind = st->q[st->qhead].kind, slot = st->q[st->qhead].slot, color = st->q[st->qhead].color;
    st->qhead = (st->qhead + 1) % RFM1CS_STORE_QUEUE, st->qn--;
    finish(st, kind, slot, color, b, b ? n : 0);
    drain(st);
}

int rfm1cs_store_scan(rfm1cs_store_t *st, int open_slot, uint32_t *used_out)
{
    int slot, r, open_len = 0;
    uint32_t used = 0;
    if (!st->io.read)
        return -1;
    for (slot = 0; slot < RFM1CS_SLOTS; slot++) {
        r = st->io.read(st->io.ctx, slot, st->buf, RFM_MOVE_PROJECT_MAX);
        if (r == RFM1CS_STORE_PENDING)
            return -1;
        if (r <= 0)
            continue;
        used |= 1u << slot;
        if (st->io.stored)
            st->io.stored(st->io.ctx, slot, st->buf, (uint32_t)r, used);
    }
    st->used = used;
    if (used_out)
        *used_out = used;
    if (open_slot >= 0 && open_slot < RFM1CS_SLOTS && (used >> open_slot & 1)) {
        r = st->io.read(st->io.ctx, open_slot, st->buf, RFM_MOVE_PROJECT_MAX);
        open_len = r > 0 ? r : 0;
    }
    return open_len;
}
