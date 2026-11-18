/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm1cs_store: the sequencer's 32 project slots for a host that keeps slot bytes somewhere (files, a database).
 * C99, no allocation, no OS calls; it never touches the core or the session itself, so it can run on the host's I/O
 * thread. The host gives it the core's RFM_MOVE_ACT_PROJECT_* actions (copied out of the session's callback: run the
 * store after the session call returns) and does only the slot I/O:
 *
 *   rfm1cs_store_init(&st, &io, used_bits);
 *   rfm1cs_store_request(&st, act->kind, act->index, act->value, act->data, act->len);
 *
 *   SAVE      write(slot, blob)                             then stored(slot, blob)
 *   DELETE    remove(slot)                                  then stored(slot, NULL)
 *   LOAD      read(slot) -> loaded(slot, bytes | NULL)      (give it to rfm1cs_session_project_answer)
 *   CUT       read(slot) into the buffer, remove(slot)      (the remove is asked at once, after the read)
 *   COPY      read(slot) into the buffer
 *   PASTE     write(slot, the buffer)                       (after every read asked before it was answered)
 *   COLOR     read(slot), byte 18 = colour and the check made again, write(slot, ..)
 *
 * stored(slot, bytes | NULL, used) tells the host to update the core's grid: rfm_move_project_dir(core, used) and
 * rfm_move_project_info(core, slot, bytes, n) (on the core's thread). A read may answer later: return
 * RFM1CS_STORE_PENDING, and answer each pending read, in the order asked, with rfm1cs_store_answer (the web player's
 * IndexedDB does). A host either answers every read at once or always later, never both. */
#ifndef RFM1CS_STORE_H
#define RFM1CS_STORE_H
#include <stdint.h>

#include "rfm1cs/move.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RFM1CS_SLOTS 32
#define RFM1CS_STORE_PENDING (-2)
#define RFM1CS_STORE_QUEUE 32
#define RFM1CS_COLOR_BYTE 18          /* a project blob's colour (0..16); its last 4 bytes are an FNV-1a check */

typedef struct {
    void *ctx;
    /* the slot's bytes into out (cap RFM_MOVE_PROJECT_MAX): their length, 0 nothing stored there, < 0 an error, or
     * RFM1CS_STORE_PENDING (answer later) */
    int (*read)(void *ctx, int slot, uint8_t *out, uint32_t cap);
    int (*write)(void *ctx, int slot, const uint8_t *b, uint32_t n);     /* 0 ok */
    int (*remove)(void *ctx, int slot);                                  /* 0 ok (nothing there is ok) */
    void (*loaded)(void *ctx, int slot, const uint8_t *b, uint32_t n);   /* LOAD's answer (NULL / 0: none) */
    void (*stored)(void *ctx, int slot, const uint8_t *b, uint32_t n, uint32_t used);   /* optional */
} rfm1cs_store_io_t;

typedef struct {
    rfm1cs_store_io_t io;
    uint32_t used;                    /* bit n: slot n holds a project (as far as the store knows) */
    uint32_t clip_len;                /* the cut / copy buffer (0: empty) */
    uint8_t clip[RFM_MOVE_PROJECT_MAX];
    uint8_t buf[RFM_MOVE_PROJECT_MAX];
    struct { uint8_t kind, slot, color, read; } q[RFM1CS_STORE_QUEUE];   /* reads asked, and pastes behind them */
    int qhead, qn;
} rfm1cs_store_t;

void rfm1cs_store_init(rfm1cs_store_t *st, const rfm1cs_store_io_t *io, uint32_t used);
/* an RFM_MOVE_ACT_PROJECT_* action (kind, slot = act->index, value = act->value, data/len = act->data/len); 1 handled,
 * 0 not a project action, -1 refused (a bad slot, the queue full) */
int rfm1cs_store_request(rfm1cs_store_t *st, int kind, int slot, int value, const uint8_t *data, uint32_t len);
/* a pending read's answer (b NULL / n 0: nothing there), in the order the reads were asked */
void rfm1cs_store_answer(rfm1cs_store_t *st, int slot, const uint8_t *b, uint32_t n);
static inline uint32_t rfm1cs_store_used(const rfm1cs_store_t *st) { return st->used; }
/* a synchronous host's start: every slot read (stored() for each one there, for the grid), the used bits into
 * *used_out; returns the open slot's length, its bytes left in st->buf (0: none there), for
 * rfm1cs_session_projects_init(s, now, *used_out, open_slot, st->buf, n); -1 when a read was pending */
int rfm1cs_store_scan(rfm1cs_store_t *st, int open_slot, uint32_t *used_out);

/* helpers */
void rfm1cs_project_set_color(uint8_t *blob, uint32_t n, int color);   /* byte 18 and the check, in place */
int rfm1cs_first_free(uint32_t used);                                  /* the lowest free slot, -1 none */

#ifdef __cplusplus
}
#endif
#endif
