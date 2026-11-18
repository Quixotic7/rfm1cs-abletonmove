/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm1cs_store (store.h): the project actions as slot I/O. The order is the web player's (RemoteFM1 move-link.js,
 * a3946f6): a cut's read then its delete; a paste after the reads asked before it; a colour read, changed, written. */
#include "rfm1cs/store.h"

#include <stdio.h>
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

/* ---- the project file (store.h): the web player's packFile / unpackFile (RemoteFM1 js/move-projects.js) */
static const uint8_t file_magic[4] = { 0x52, 0x4D, 0x56, 0x50 };   /* "RMVP" */
static void wr32le(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static uint32_t rd32le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

uint32_t rfm1cs_file_size(const rfm1cs_file_entry_t *e, int n)
{
    uint32_t len = RFM1CS_FILE_HEAD;
    int i;
    for (i = 0; i < n; i++)
        len += RFM1CS_FILE_ENTRY + e[i].len;
    return len;
}

uint32_t rfm1cs_file_pack(const rfm1cs_file_entry_t *e, int n, int kind, uint8_t *out, uint32_t cap)
{
    uint32_t len, o = RFM1CS_FILE_HEAD;
    int i;
    if (n < 0 || n > 65535 || (n && !e))
        return 0;
    len = rfm1cs_file_size(e, n);
    if (!out || len > cap)
        return 0;
    memcpy(out, file_magic, 4);
    out[4] = RFM1CS_FILE_VERSION;
    out[5] = (uint8_t)(kind ? kind : n == 1 ? RFM1CS_FILE_ONE : RFM1CS_FILE_SET);
    out[6] = (uint8_t)n, out[7] = (uint8_t)(n >> 8);
    for (i = 0; i < n; i++) {
        out[o] = (uint8_t)e[i].slot, out[o + 1] = 0;
        wr32le(out + o + 2, e[i].saved);
        wr32le(out + o + 6, e[i].len);
        if (e[i].len)
            memcpy(out + o + RFM1CS_FILE_ENTRY, e[i].bytes, e[i].len);
        o += RFM1CS_FILE_ENTRY + e[i].len;
    }
    return len;
}

static int file_bad(rfm1cs_file_error_t *err, int code, int index, int slot, uint32_t value)
{
    if (err)
        err->code = code, err->index = index, err->slot = slot, err->value = value;
    return -code;
}

int rfm1cs_file_unpack(const uint8_t *b, uint32_t n, int *kind_out, rfm1cs_file_entry_t e[RFM1CS_SLOTS],
                       rfm1cs_file_error_t *err)
{
    uint32_t o = RFM1CS_FILE_HEAD, seen = 0;
    int kind, count, i;
    if (err)
        memset(err, 0, sizeof *err);
    if (!b)
        return file_bad(err, RFM1CS_FILE_E_EMPTY, 0, 0, 0);
    if (n > RFM1CS_FILE_MAX)
        return file_bad(err, RFM1CS_FILE_E_SIZE, 0, 0, n);
    if (n < RFM1CS_FILE_HEAD || memcmp(b, file_magic, 4) != 0)
        return file_bad(err, RFM1CS_FILE_E_MAGIC, 0, 0, 0);
    if (b[4] != RFM1CS_FILE_VERSION)
        return file_bad(err, RFM1CS_FILE_E_VERSION, 0, 0, b[4]);
    kind = b[5], count = b[6] | b[7] << 8;
    if (kind != RFM1CS_FILE_ONE && kind != RFM1CS_FILE_SET)
        return file_bad(err, RFM1CS_FILE_E_KIND, 0, 0, (uint32_t)kind);
    if (count < 1 || count > RFM1CS_SLOTS || (kind == RFM1CS_FILE_ONE && count != 1))
        return file_bad(err, RFM1CS_FILE_E_COUNT, 0, 0, (uint32_t)count);
    for (i = 0; i < count; i++) {
        int slot;
        uint32_t t, len;
        if (o + RFM1CS_FILE_ENTRY > n)
            return file_bad(err, RFM1CS_FILE_E_CUT, i, 0, 0);
        slot = b[o], t = rd32le(b + o + 2), len = rd32le(b + o + 6);
        if (slot >= RFM1CS_SLOTS)
            return file_bad(err, RFM1CS_FILE_E_SLOT, i, slot, 0);
        if (seen >> slot & 1)
            return file_bad(err, RFM1CS_FILE_E_TWICE, i, slot, 0);
        if (len < 1 || len > RFM_MOVE_PROJECT_MAX)
            return file_bad(err, RFM1CS_FILE_E_LEN, i, slot, len);
        if (len > n - o - RFM1CS_FILE_ENTRY)
            return file_bad(err, RFM1CS_FILE_E_CUT, i, slot, 0);
        if (rfm_move_project_check(b + o + RFM1CS_FILE_ENTRY, len) != 0)
            return file_bad(err, RFM1CS_FILE_E_DAMAGED, i, slot, 0);
        seen |= 1u << slot;
        if (e)
            e[i].slot = slot, e[i].saved = t, e[i].bytes = b + o + RFM1CS_FILE_ENTRY, e[i].len = len;
        o += RFM1CS_FILE_ENTRY + len;
    }
    if (o != n)
        return file_bad(err, RFM1CS_FILE_E_TRAILING, count, 0, n - o);
    if (kind_out)
        *kind_out = kind;
    return count;
}

int rfm1cs_file_check(const uint8_t *b, uint32_t n, rfm1cs_file_error_t *err)
{
    const int r = rfm1cs_file_unpack(b, n, NULL, NULL, err);
    return r < 0 ? r : 0;
}

uint32_t rfm1cs_file_error_text(const rfm1cs_file_error_t *err, const char *self, char *out, uint32_t cap)
{
    char why[96];
    int k;
    const int code = err ? err->code : RFM1CS_FILE_E_EMPTY, i = err ? err->index + 1 : 0, slot = err ? err->slot + 1 : 0;
    const unsigned long v = err ? (unsigned long)err->value : 0;
    if (!out || !cap)
        return 0;
    switch (code) {
    case RFM1CS_FILE_OK: snprintf(why, sizeof why, "%s", ""); break;
    case RFM1CS_FILE_E_SIZE: snprintf(why, sizeof why, "it is %lu bytes, more than a full set (%lu)", v, (unsigned long)RFM1CS_FILE_MAX); break;
    case RFM1CS_FILE_E_MAGIC: snprintf(why, sizeof why, "it does not start with RMVP"); break;
    case RFM1CS_FILE_E_VERSION:
        snprintf(why, sizeof why, "version %lu; this %s reads version %d", v, self && *self ? self : "page", RFM1CS_FILE_VERSION);
        break;
    case RFM1CS_FILE_E_KIND: snprintf(why, sizeof why, "kind %lu", v); break;
    case RFM1CS_FILE_E_COUNT: snprintf(why, sizeof why, "%lu projects", v); break;
    case RFM1CS_FILE_E_CUT: snprintf(why, sizeof why, "project %d is cut off", i); break;
    case RFM1CS_FILE_E_SLOT: snprintf(why, sizeof why, "slot %d out of range", slot); break;
    case RFM1CS_FILE_E_TWICE: snprintf(why, sizeof why, "slot %d twice", slot); break;
    case RFM1CS_FILE_E_LEN: snprintf(why, sizeof why, "project %d is %lu bytes", i, v); break;
    case RFM1CS_FILE_E_DAMAGED: snprintf(why, sizeof why, "project %d (slot %d) is damaged", i, slot); break;
    case RFM1CS_FILE_E_TRAILING: snprintf(why, sizeof why, "%lu bytes after the last project", v); break;
    default: snprintf(why, sizeof why, "nothing to read"); break;
    }
    k = code == RFM1CS_FILE_OK ? snprintf(out, cap, "%s", "") : snprintf(out, cap, "Not a Move project file: %s.", why);
    return k < 0 ? 0 : (uint32_t)k < cap ? (uint32_t)k : cap - 1;
}
