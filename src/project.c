/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm_move's projects: the sequencer's whole state as a versioned, little-endian byte blob, and the 32 slots the
 * host stores them in (the core asks with actions; it never does IO). A blob may come from a file someone sent:
 * rfm_move_project_check reads nothing past n and accepts only a blob every field of which is in range.
 *
 *   0  "RMSQ"  4 version (3; 2 loads: a shorter arp, no per-track key; 1 loads: no extension)  5 flags (0)  6 bpm10 u16  8 clock mode  9 fixed velocity  10 selected track
 *   11 tracks (16)  12 scenes (16)  13 record mode (RFM_MOVE_RM_*)  14 record style (RFM_MOVE_RS_*)  15 groove
 *   16 root  17 scale (version 3)  18 colour 0..16 (any version: older blobs have 0, the slot's own)  19..23 zero
 *   24 16 tracks x { channel 0..15, mute 0/1, active clip 0..15 or 255, edited clip 0..15 }
 *   88 256 clip lengths u16 (1..256 steps), clip id = track * 16 + scene
 *  600 notes u16, then n x { clip, step, note 0..127, velocity 1..127, micro -12..11, length u16 1..6144 }
 *      strictly ascending by (clip, step, note)
 *      locks u16, then n x { clip, step, cc, value } strictly ascending by (clip, step, cc): cc 0..127 value
 *      0..127 (a CC lock), or (version 2) a trig parameter RFM_MOVE_TRIG_* (128..133) in its range
 *      (version 2) 256 loop starts (lstart + length <= 256), 32 bytes of clips-made bits, 16 track rates,
 *      16 grooves (0..150 %), 16 arps x { pattern, rate, range 1..8, chance 0..100, mode, gate 2..200, odist -24..24 },
 *      16 x { the pads' layout 0..3, 4 banks x 8 knobs' CC numbers 0..127 }
 *      (version 3) 16 x { own, root, scale, solo }  (version 4) 16 x the TRIG defaults { micro + 12, ratchet,
 *      chance, condition, function, leap + 15, jump, length index }
 *      (version 5) FX setup (fw.c): 3 MIDI FX x { module name 24 (NUL padded, "" empty), 8 values 0..127 (version 6:
 *      128 by parameter index, 0..127 or 255 unset), track
 *      filter u16 (one bit, or 0xFFFF all) }, 3 audio FX x { name 24, 8 values }, 3 firmware track filters u16,
 *      3 pans, 3 x 3 sends (firmware by audio FX), 0..127
 *      (version 7) 3 firmwares' MIDI routes (bits 0..2 the slots, never its own; 8 USB out; no cycle)
 *      (version 8) the launch quantization (RFM_MOVE_LQ_*), 256 clips' launch settings (bits 0..2 its launch: 0 the
 *      project's, 1..6 RFM_MOVE_LQ_* + 1; bit 3 after its loop the next clip)
 *      (version 9, 0.13.0) the firmwares (projfw.c): 3 slots x { id 32 NUL padded ("" empty), level, layout, scale,
 *      root, channel, octave, 8 CC numbers, the sequencer track }, the slot played, 32 drum notes, velocity mode, level
 *      FNV-1a (32 bit) of everything before it */
#include <stdio.h>
#include <string.h>

#include "move_int.h"

#define HDR 24
#define OFF_TRACKS HDR
#define OFF_LENS (OFF_TRACKS + RFM_MOVE_SEQ_TRACKS * 4)
#define OFF_NOTES (OFF_LENS + RFM_MOVE_SEQ_CLIPS * 2)

static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static void w16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v & 0xFF), p[1] = (uint8_t)(v >> 8 & 0xFF); }
static void w32(uint8_t *p, uint32_t v) { w16(p, v & 0xFFFF), w16(p + 2, v >> 16); }
static unsigned r16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t r32(const uint8_t *p) { return (uint32_t)r16(p) | (uint32_t)r16(p + 2) << 16; }

int rfm_move_project_save(rfm_move_t *m, uint8_t *out, uint32_t cap)
{
    rfmv_seq_sync(m);                                   /* (the selected track's layout and CC numbers in) */
    const rfm_move_seq_t *q = &m->seq;
    const uint32_t need = RFM_MOVE_PROJECT_FIXED + (uint32_t)q->nnotes * 7u + (uint32_t)q->nlocks * 4u;   /* (v2) */
    uint8_t *p = out;
    int i;
    if (!out || cap < need)
        return -1;
    memset(p, 0, HDR);
    memcpy(p, "RMSQ", 4);
    p[4] = RFM_MOVE_PROJECT_VERSION, p[5] = 1;         /* (p[5] bit 0, 0.5.5: the clock byte is a choice) */
    w16(p + 6, q->bpm10);
    p[8] = q->clock_mode, p[9] = m->s.vel_mode == RFM_MOVE_VEL_FIXED,   /* (was the project's Full Velocity; now a setting) */
    p[10] = q->sel_track;
    p[13] = q->rec_mode, p[14] = q->rec_style;          /* (version 2) */
    p[15] = q->groove, p[16] = q->root, p[17] = q->scale;   /* (version 3: the Global groove, key and scale) */
    p[18] = q->color;                                   /* (its colour: any version may carry it, 0 the slot's) */
    p[11] = RFM_MOVE_SEQ_TRACKS, p[12] = RFM_MOVE_SEQ_SCENES;
    p += HDR;
    for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++, p += 4)
        p[0] = q->tr[i].ch, p[1] = q->tr[i].mute, p[2] = (uint8_t)(q->tr[i].active < 0 ? 255 : q->tr[i].active),
        p[3] = (uint8_t)q->tr[i].sel;
    for (i = 0; i < RFM_MOVE_SEQ_CLIPS; i++, p += 2)
        w16(p, q->len[i]);
    w16(p, q->nnotes), p += 2;
    for (i = 0; i < q->nnotes; i++, p += 7) {
        const rfm_move_note_t *n = &q->notes[i];
        p[0] = n->clip, p[1] = n->step, p[2] = n->note, p[3] = n->vel, p[4] = (uint8_t)n->micro;
        w16(p + 5, n->len);
    }
    w16(p, q->nlocks), p += 2;
    for (i = 0; i < q->nlocks; i++, p += 4)
        p[0] = q->locks[i].clip, p[1] = q->locks[i].step, p[2] = q->locks[i].cc, p[3] = q->locks[i].val;
    memcpy(p, q->lstart, RFM_MOVE_SEQ_CLIPS), p += RFM_MOVE_SEQ_CLIPS;
    memcpy(p, q->made, sizeof q->made), p += sizeof q->made;
    for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++)
        *p++ = q->tr[i].rate;
    for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++)
        *p++ = q->tr[i].groove;
    for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++, p += RFM_MOVE_ARP_BYTES) {
        const rfm_move_arp_t *a = &q->tr[i].arp;
        p[0] = a->pat, p[1] = a->rate, p[2] = a->range, p[3] = a->chance, p[4] = a->mode, p[5] = a->gate;
        p[6] = (uint8_t)a->odist, p[7] = a->quant, p[8] = a->reset, p[9] = a->mlen, p[10] = a->tlen;
        memcpy(p + 11, a->lane, sizeof a->lane);
    }
    for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++, p += 1 + sizeof q->tr[i].cc)
        p[0] = q->tr[i].layout, memcpy(p + 1, q->tr[i].cc, sizeof q->tr[i].cc);
    for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++, p += 4)
        p[0] = q->tr[i].own, p[1] = q->tr[i].root, p[2] = q->tr[i].scale, p[3] = q->tr[i].solo;
    for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++, p += 8)
        memcpy(p, q->tr[i].tdef, 8);
    rfmv_fxset_write(&q->fx, p), p += RFM_MOVE_FXSET_BYTES;   /* (version 5: FX setup) */
    for (i = 0; i < RFM_MOVE_FW_SLOTS; i++)              /* (version 7: the MIDI routes) */
        *p++ = q->fx.mroute[i];
    *p++ = q->launch_q;                                 /* (version 8: the launch quantization, the clips' own) */
    memcpy(p, q->cset, RFM_MOVE_SEQ_CLIPS), p += RFM_MOVE_SEQ_CLIPS;
    rfmv_pfw_write(m, p), p += RFM_MOVE_PFW_BYTES;      /* (version 9: the firmwares) */
    w32(p, fnv(out, (uint32_t)(p - out)));
    p += 4;
    return (int)(p - out);
}

/* a trig parameter's value is in range */
static int trig_ok(int cc, int v)
{
    switch (cc) {
    case RFM_MOVE_TRIG_RATCHET: return v >= 2 && v <= 8;
    case RFM_MOVE_TRIG_CHANCE: return v >= 1 && v <= 99;
    case RFM_MOVE_TRIG_COND: return v >= 1 && v < RFM_MOVE_COND_N;
    case RFM_MOVE_TRIG_FUNC: return v >= 1 && v < RFM_MOVE_FN_N;
    case RFM_MOVE_TRIG_LEAP: return v <= 30;
    case RFM_MOVE_TRIG_JUMP: return v < RFM_MOVE_SEQ_MAXLEN;
    case RFM_MOVE_TRIG_REPEAT: return v >= 1 && v <= 16;
    default: return 0;
    }
}

/* every check; q NULL: only check. 0 ok */
static int parse(const uint8_t *b, uint32_t n, rfm_move_seq_t *q)
{
    uint32_t nn, nl, offl, i, prev, ext, fixed, ab;
    const uint8_t *p, *x;
    int ver;
    if (!b || n < 5 || n > RFM_MOVE_PROJECT_MAX)
        return -1;
    if (memcmp(b, "RMSQ", 4))
        return -1;
    ver = b[4];
    if (ver < 1 || ver > RFM_MOVE_PROJECT_VERSION)
        return -2;
    ext = ver >= 9 ? RFM_MOVE_PROJECT_EXT : ver == 8 ? RFM_MOVE_PROJECT_EXT_V8 : ver == 7 ? RFM_MOVE_PROJECT_EXT_V7 : ver == 6 ? RFM_MOVE_PROJECT_EXT_V6 : ver == 5 ? RFM_MOVE_PROJECT_EXT_V5 : ver == 4 ? RFM_MOVE_PROJECT_EXT_V4 : ver == 3 ? RFM_MOVE_PROJECT_EXT_V3 :
          ver == 2 ? RFM_MOVE_PROJECT_EXT_V2 : 0;
    ab = ver >= 3 ? RFM_MOVE_ARP_BYTES : RFM_MOVE_ARP_BYTES_V2;   /* (an arp's bytes) */
    fixed = RFM_MOVE_PROJECT_FIXED - RFM_MOVE_PROJECT_EXT + ext;
    if (n < fixed)
        return -1;
    if (b[11] != RFM_MOVE_SEQ_TRACKS || b[12] != RFM_MOVE_SEQ_SCENES)
        return -3;
    nn = r16(b + OFF_NOTES);
    if (nn > RFM_MOVE_SEQ_NOTES)
        return -4;
    offl = OFF_NOTES + 2 + nn * 7;
    if (offl + 2 + 4 > n)
        return -4;
    nl = r16(b + offl);
    if (nl > RFM_MOVE_SEQ_LOCKS || offl + 2 + nl * 4 + ext + 4 != n)
        return -4;
    x = b + offl + 2 + nl * 4;                          /* (version 2: the extension) */
    if (r32(b + n - 4) != fnv(b, n - 4))
        return -5;
    if (r16(b + 6) < RFM_MOVE_SEQ_BPM10_MIN || r16(b + 6) > RFM_MOVE_SEQ_BPM10_MAX || b[8] > RFM_MOVE_CLOCK_EXTERNAL ||
        b[9] > 1 || b[10] >= RFM_MOVE_SEQ_TRACKS || b[13] >= RFM_MOVE_RM_N || b[14] >= RFM_MOVE_RS_N || b[15] > 150 ||
        b[16] > 11 || b[17] >= RFM_MOVE_NSCALES || b[18] > RFM_MOVE_PROJ_COLORS)
        return -6;
    for (i = 0, p = b + OFF_TRACKS; i < RFM_MOVE_SEQ_TRACKS; i++, p += 4)
        if (p[0] > 15 || p[1] > 1 || (p[2] != 255 && p[2] >= RFM_MOVE_SEQ_SCENES) || p[3] >= RFM_MOVE_SEQ_SCENES)
            return -6;
    for (i = 0, p = b + OFF_LENS; i < RFM_MOVE_SEQ_CLIPS; i++, p += 2)
        if (r16(p) < 1 || r16(p) > RFM_MOVE_SEQ_MAXLEN)
            return -6;
    for (i = 0, prev = 0, p = b + OFF_NOTES + 2; i < nn; i++, p += 7) {
        const uint32_t key = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        const int micro = (int8_t)p[4];
        const unsigned len = r16(p + 5);
        if (p[2] > 127 || p[3] < 1 || p[3] > 127 || micro < -RFM_MOVE_SEQ_TPS / 2 || micro >= RFM_MOVE_SEQ_TPS / 2 ||
            len < 1 || len > RFM_MOVE_SEQ_MAXLEN * RFM_MOVE_SEQ_TPS || (i && key <= prev))
            return -7;
        prev = key;
    }
    {   /* at most RFM_MOVE_SEQ_POLY notes a step */
        uint32_t run = 0;
        for (i = 0, p = b + OFF_NOTES + 2; i < nn; i++, p += 7) {
            run = i && p[0] == p[-7] && p[1] == p[-6] ? run + 1 : 1;
            if (run > RFM_MOVE_SEQ_POLY)
                return -7;
        }
    }
    for (i = 0, prev = 0, p = b + offl + 2; i < nl; i++, p += 4) {
        const uint32_t key = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        if ((p[2] > 127 ? ver < 2 || !trig_ok(p[2], p[3]) : p[3] > 127) || (i && key <= prev))
            return -8;
        prev = key;
    }
    if (ext) {
        for (i = 0; i < RFM_MOVE_SEQ_CLIPS; i++)
            if (x[i] + r16(b + OFF_LENS + i * 2) > RFM_MOVE_SEQ_MAXLEN)
                return -9;
        for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++) {
            const uint8_t *r = x + RFM_MOVE_SEQ_CLIPS + RFM_MOVE_SEQ_CLIPS / 8;
            const uint8_t *a = r + 2 * RFM_MOVE_SEQ_TRACKS + i * ab;
            int k;
            if (r[i] >= RFM_MOVE_SEQ_RATES || r[RFM_MOVE_SEQ_TRACKS + i] > 150 || a[0] >= RFM_MOVE_AP_N ||
                a[1] >= RFM_MOVE_ARP_RATES || a[2] < 1 || a[2] > 8 || a[3] > 100 || a[4] >= RFM_MOVE_AM_N || a[5] < 2 ||
                a[5] > 200 || (int8_t)a[6] < -24 || (int8_t)a[6] > 24)
                return -9;
            if (ver >= 3) {                             /* quant reset mlen tlen, the lanes */
                if (a[7] > 16 || a[8] >= RFM_MOVE_AR_N || a[9] < 1 || a[9] > 16 || a[10] < 1 || a[10] > 16)
                    return -9;
                for (k = 0; k < RFM_MOVE_ARP_LANE; k++)
                    if (a[11 + k] >= RFM_MOVE_AL_N || (int8_t)a[11 + RFM_MOVE_ARP_LANE + k] < -48 ||
                        (int8_t)a[11 + RFM_MOVE_ARP_LANE + k] > 48)
                        return -9;
            }
        }
        if (ver >= 3)
            for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++) { /* (own, root, scale, solo) */
                const uint8_t *o = x + RFM_MOVE_PROJECT_EXT_V2 + (RFM_MOVE_ARP_BYTES - RFM_MOVE_ARP_BYTES_V2) * RFM_MOVE_SEQ_TRACKS + i * 4;
                if (o[0] > 3 || o[1] > 11 || o[2] >= RFM_MOVE_NSCALES || o[3] > 1)
                    return -9;
            }
        if (ver >= 4)
            for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++) { /* (the trig defaults) */
                const uint8_t *d = x + RFM_MOVE_PROJECT_EXT_V3 + i * 8;
                if (d[0] > 23 || d[1] < 1 || d[1] > 8 || d[2] < 1 || d[2] > 100 || d[3] >= RFM_MOVE_COND_N ||
                    d[4] >= RFM_MOVE_FN_N || d[5] > 30 || d[7] >= RFM_MOVE_TRIG_NLENS)
                    return -9;
            }
        if (ver >= 5 && rfmv_fxset_check(x + RFM_MOVE_PROJECT_EXT_V4, ver >= 6 ? RFM_MOVE_FX_PARAMS : RFM_MOVE_FX_PARAMS_V5) != 0)
            return -9;                                  /* (FX setup) */
        if (ver >= 7 && rfmv_mroute_check(x + RFM_MOVE_PROJECT_EXT_V6) != 0)
            return -9;                                  /* (the MIDI routes: no own bit, no cycle) */
        if (ver >= 8) {                                 /* (the launch quantization, the clips' own) */
            const uint8_t *c = x + RFM_MOVE_PROJECT_EXT_V7;
            if (c[0] >= RFM_MOVE_LQ_N)
                return -9;
            for (i = 0; i < RFM_MOVE_SEQ_CLIPS; i++)
                if ((c[1 + i] & 7) > RFM_MOVE_LQ_N || (c[1 + i] & ~0x0Fu))
                    return -9;
        }
        if (ver >= 9 && rfmv_pfw_check(x + RFM_MOVE_PROJECT_EXT_V8) != 0)
            return -9;                                  /* (0.13.0: the firmwares) */
        for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++) {      /* (each track's layout and CC numbers) */
            const uint8_t *l = x + RFM_MOVE_SEQ_CLIPS + RFM_MOVE_SEQ_CLIPS / 8 + RFM_MOVE_SEQ_TRACKS * (2 + ab) +
                               i * (1 + RFM_MOVE_NBANKS * RFM_MOVE_NKNOBS);
            int k;
            if (l[0] > RFM_MOVE_LAYOUT_OCTAVE)
                return -9;
            for (k = 1; k <= RFM_MOVE_NBANKS * RFM_MOVE_NKNOBS; k++)
                if (l[k] > 127)
                    return -9;
        }
    }
    if (!q)
        return 0;
    q->bpm10 = (uint16_t)r16(b + 6), q->clock_mode = b[8], q->sel_track = b[10];
    if (q->clock_mode == RFM_MOVE_CLOCK_AUTO && !(b[5] & 1))
        q->clock_mode = RFM_MOVE_CLOCK_INTERNAL;        /* (0.5.5: AUTO was only the default then; its own clock) */
    q->rec_mode = b[13], q->rec_style = b[14], q->color = b[18];
    if (ver >= 3)                                       /* (older: the key and scale MIDI mode has now) */
        q->groove = b[15], q->root = b[16], q->scale = b[17];
    for (i = 0, p = b + OFF_TRACKS; i < RFM_MOVE_SEQ_TRACKS; i++, p += 4) {
        q->tr[i].ch = p[0], q->tr[i].mute = p[1], q->tr[i].active = (int8_t)(p[2] == 255 ? -1 : p[2]);
        q->tr[i].sel = (int8_t)p[3];
    }
    for (i = 0, p = b + OFF_LENS; i < RFM_MOVE_SEQ_CLIPS; i++, p += 2)
        q->len[i] = (uint16_t)r16(p);
    q->nnotes = (uint16_t)nn;
    for (i = 0, p = b + OFF_NOTES + 2; i < nn; i++, p += 7) {
        rfm_move_note_t *o = &q->notes[i];
        memset(o, 0, sizeof *o);
        o->clip = p[0], o->step = p[1], o->note = p[2], o->vel = p[3], o->micro = (int8_t)p[4];
        o->len = (uint16_t)r16(p + 5);
    }
    q->nlocks = (uint16_t)nl;
    for (i = 0, p = b + offl + 2; i < nl; i++, p += 4)
        q->locks[i].clip = p[0], q->locks[i].step = p[1], q->locks[i].cc = p[2], q->locks[i].val = p[3];
    if (ext) {
        memcpy(q->lstart, x, RFM_MOVE_SEQ_CLIPS);
        memcpy(q->made, x + RFM_MOVE_SEQ_CLIPS, sizeof q->made);
        for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++) {
            const uint8_t *r = x + RFM_MOVE_SEQ_CLIPS + RFM_MOVE_SEQ_CLIPS / 8;
            const uint8_t *a = r + 2 * RFM_MOVE_SEQ_TRACKS + i * ab;
            rfm_move_arp_t *o = &q->tr[i].arp;
            q->tr[i].rate = r[i], q->tr[i].groove = r[RFM_MOVE_SEQ_TRACKS + i];
            o->pat = a[0], o->rate = a[1], o->range = a[2], o->chance = a[3], o->mode = a[4], o->gate = a[5];
            o->odist = (int8_t)a[6];
            if (ver >= 3) {
                o->quant = a[7], o->reset = a[8], o->mlen = a[9], o->tlen = a[10];
                memcpy(o->lane, a + 11, sizeof o->lane);
            }
            if (ver == 2 && q->tr[i].groove)
                q->tr[i].own |= 1;                      /* (version 2's grooves were the tracks' own) */
        }
        for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++) {
            const uint8_t *l = x + RFM_MOVE_SEQ_CLIPS + RFM_MOVE_SEQ_CLIPS / 8 + RFM_MOVE_SEQ_TRACKS * (2 + ab) +
                               i * (1 + RFM_MOVE_NBANKS * RFM_MOVE_NKNOBS);
            q->tr[i].layout = l[0];
            memcpy(q->tr[i].cc, l + 1, sizeof q->tr[i].cc);
        }
        if (ver >= 3)
            for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++) {
                const uint8_t *o = x + RFM_MOVE_PROJECT_EXT_V2 + (RFM_MOVE_ARP_BYTES - RFM_MOVE_ARP_BYTES_V2) * RFM_MOVE_SEQ_TRACKS + i * 4;
                q->tr[i].own = o[0], q->tr[i].root = o[1], q->tr[i].scale = o[2], q->tr[i].solo = o[3];
            }
        if (ver >= 4)
            for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++)
                memcpy(q->tr[i].tdef, x + RFM_MOVE_PROJECT_EXT_V3 + i * 8, 8);
        if (ver >= 5)
            rfmv_fxset_read(&q->fx, x + RFM_MOVE_PROJECT_EXT_V4, ver >= 6 ? RFM_MOVE_FX_PARAMS : RFM_MOVE_FX_PARAMS_V5);
        memset(q->fx.mroute, 0, sizeof q->fx.mroute);   /* (before version 7: no routes) */
        if (ver >= 7)
            memcpy(q->fx.mroute, x + RFM_MOVE_PROJECT_EXT_V6, sizeof q->fx.mroute);
    }
    q->launch_q = 0;                                    /* (before version 8: 1 Bar; every clip the project's) */
    memset(q->cset, 0, sizeof q->cset);
    if (ver >= 8) {
        q->launch_q = x[RFM_MOVE_PROJECT_EXT_V7];
        memcpy(q->cset, x + RFM_MOVE_PROJECT_EXT_V7 + 1, sizeof q->cset);
    }
    if (!ext) {                                            /* version 1: loops from step 1, clips with notes, 1/16 */
        memset(q->lstart, 0, sizeof q->lstart);
        memset(q->made, 0, sizeof q->made);
        for (i = 0; i < nn; i++)
            q->made[q->notes[i].clip >> 3] = (uint8_t)(q->made[q->notes[i].clip >> 3] | 1u << (q->notes[i].clip & 7));
        for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++)
            q->tr[i].rate = RFM_MOVE_SEQ_RATE_16;
    }
    return 0;
}

int rfm_move_project_check(const uint8_t *b, uint32_t n) { return parse(b, n, NULL); }

/* ---- a project in brief, for the grid and its popup */
static void info_count(rfm_move_proj_info_t *o, uint8_t seen[RFM_MOVE_SEQ_CLIPS], int clip, int note)
{
    if (!seen[clip])
        seen[clip] = 1, o->clips++;
    if (note && o->notes[clip / RFM_MOVE_SEQ_SCENES] < 0xFFFF)
        o->notes[clip / RFM_MOVE_SEQ_SCENES]++;
}
static void info_live(const rfm_move_t *m, rfm_move_proj_info_t *o)
{
    const rfm_move_seq_t *q = &m->seq;
    uint8_t seen[RFM_MOVE_SEQ_CLIPS];
    int i;
    memset(o, 0, sizeof *o);
    memset(seen, 0, sizeof seen);
    o->ok = 1, o->bpm10 = q->bpm10, o->color = q->color;
    o->has_fw = 1;                                      /* (0.13.0: saved as version 9, with what it holds now) */
    rfmv_pfw_saved_ids(m, o->fw);
    for (i = 0; i < q->nnotes; i++)
        info_count(o, seen, q->notes[i].clip, 1);
    for (i = 0; i < q->nlocks; i++)
        info_count(o, seen, q->locks[i].clip, 0);
}
void rfmv_proj_info(const rfm_move_t *m, int slot, rfm_move_proj_info_t *o)
{
    const rfm_move_seq_t *q = &m->seq;
    if (slot == q->proj_cur && q->proj_pending < 0)
        info_live(m, o);
    else if (slot >= 0 && slot < RFM_MOVE_PROJECTS)
        *o = q->info[slot];
    else
        memset(o, 0, sizeof *o);
}
int rfm_move_project_info(rfm_move_t *m, int slot, const uint8_t *b, uint32_t n)
{
    rfm_move_proj_info_t *o;
    uint8_t seen[RFM_MOVE_SEQ_CLIPS];
    uint32_t nn, nl, i;
    const uint8_t *p;
    if (slot < 0 || slot >= RFM_MOVE_PROJECTS)
        return -1;
    o = &m->seq.info[slot];
    memset(o, 0, sizeof *o);
    if (parse(b, n, NULL) != 0)
        return -1;
    memset(seen, 0, sizeof seen);
    o->ok = 1, o->bpm10 = (uint16_t)r16(b + 6), o->color = b[18];
    if (b[4] >= 9)                                      /* (0.13.0) its firmwares */
        o->has_fw = 1, rfmv_pfw_ids(b + n - 4 - RFM_MOVE_PFW_BYTES, o->fw);
    nn = r16(b + OFF_NOTES);
    for (i = 0, p = b + OFF_NOTES + 2; i < nn; i++, p += 7)
        info_count(o, seen, p[0], 1);
    nl = r16(b + OFF_NOTES + 2 + nn * 7);
    for (i = 0, p = b + OFF_NOTES + 4 + nn * 7; i < nl; i++, p += 4)
        info_count(o, seen, p[0], 0);
    return 0;
}

int rfm_move_project_load(rfm_move_t *m, int slot, const uint8_t *b, uint32_t n)
{
    rfm_move_seq_t *q = &m->seq;
    if (slot < 0 || slot >= RFM_MOVE_PROJECTS)
        return -1;
    if (b && n && parse(b, n, NULL) != 0) {
        if (q->proj_pending == slot)
            q->proj_pending = -1;
        rfmv_pfw_load_failed(m, slot);                  /* (0.13.0: its loading screen goes) */
        rfmv_seq_msg(m, SEQ_MSG_LOADFAIL);
        return -1;
    }
    rfmv_latch_drop_all(m);                             /* (0.5.3: a load lets the latched notes go) */
    const int was = q->playing || q->start_pending;     /* (the transport stays as it was: only Play starts or
                                                         * stops it, the owner's) */
    rfmv_seq_stop(m);
    rfmv_seq_ui_release(m);
    rfmv_seq_project_clear(m);
    if (b && n) {
        int c;
        parse(b, n, q);
        q->proj_used |= 1u << slot;
        for (c = 0; c < RFM_MOVE_SEQ_CLIPS; c++)        /* (0.10.0, the owner's review) clips with no notes or locks
                                                         * (before 0.10.0 every press on an empty session slot made
                                                         * one) are empty slots again */
            if ((q->made[c >> 3] >> (c & 7) & 1) && !rfmv_seq_clip_used(m, c))
                q->made[c >> 3] = (uint8_t)(q->made[c >> 3] & ~(1u << (c & 7)));
    }
    if (q->clock_mode != RFM_MOVE_CLOCK_EXTERNAL)
        q->following = 0;
    q->ext_arm = 0, q->ext_arm_n = 0, q->ext_stall = 0;
    q->bank = (uint8_t)(q->sel_track / 4);
    rfmv_seq_load_track(m);                             /* (the selected track's layout, CC numbers, key) */
    q->proj_cur = (int8_t)slot, q->proj_pending = -1;   /* (projects mode stays: the owner's, left with Back / Menu) */
    rfmv_pfw_loaded(m, slot, b && n && b[4] >= 9 ? b + n - 4 - RFM_MOVE_PFW_BYTES : NULL);   /* (0.13.0: its
                                                         * firmwares asked for; the loading screen) */
    if (q->play_after_load || was) {                    /* (Play pressed while it loaded, or it was playing; 0.13.0:
                                                         * once its firmwares are in) */
        q->play_after_load = 0;
        if (m->pfw_on)
            m->pfw_play = 1;
        else
            rfmv_seq_start(m);
    }
    if (!(b && n))                                      /* (0.13.0) an empty project made the current one (the host at
                                                         * start with nothing stored there, a cut of the open one, a
                                                         * new one): a new project's routing, channels and Track
                                                         * buttons' tracks, together. (The clear above put the channels
                                                         * and filters back to the old defaults while the settings kept
                                                         * the new tracks: slot 2's pads then played channel 5) */
        rfmv_pfw_defaults(m);
    q->dirty = 0;
    info_live(m, &q->info[slot]);
    rfmv_fx_loaded(m);                                  /* (0.4.0: its FX setup to the host) */
    return 0;
}

void rfm_move_project_dir(rfm_move_t *m, uint32_t used)
{
    m->seq.proj_used = used;
    rfmv_pfw_first(m, used);                            /* (0.13.0: a fresh install: a new project's routing) */
}
int rfm_move_project_slot(const rfm_move_t *m) { return m->seq.proj_cur; }

static void emit_slot(rfm_move_t *m, int kind, int slot, const uint8_t *data, uint32_t len)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = (uint8_t)kind, a.index = (uint8_t)slot, a.data = data, a.len = len;
    if (m->fn)
        m->fn(m->ctx, &a);
}

/* value 1: the player's own save (the open project's pad clicked): the host stores the firmwares as they are at this
 * moment too, not only what its autosave last saw (0: the autosave, a flush before a load, a copy) */
static void proj_save(rfm_move_t *m, int value)
{
    rfm_move_seq_t *q = &m->seq;
    const int n = rfm_move_project_save(m, q->blob, sizeof q->blob);
    rfm_move_act_t a;
    if (n <= 0)
        return;
    q->dirty = 0;
    q->proj_used |= 1u << q->proj_cur;
    info_live(m, &q->info[q->proj_cur]);
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_PROJECT_SAVE, a.index = (uint8_t)q->proj_cur, a.value = (int16_t)value, a.data = q->blob;
    a.len = (uint32_t)n;
    if (m->fn)
        m->fn(m->ctx, &a);
}
void rfmv_proj_save_now(rfm_move_t *m) { proj_save(m, 0); }

void rfm_move_project_flush(rfm_move_t *m)
{
    if (m->seq.dirty)
        rfmv_proj_save_now(m);
}

void rfmv_proj_poll(rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    if (q->dirty && q->now_us - q->dirty_us >= (uint64_t)RFM_MOVE_AUTOSAVE_MS * 1000u)
        rfmv_proj_save_now(m);
}

/* a pad in the project grid: the current project is saved, then the slot loads (the host answers) or starts empty */
void rfmv_proj_open(rfm_move_t *m, int slot)
{
    rfm_move_seq_t *q = &m->seq;
    if (slot < 0 || slot >= RFM_MOVE_PROJECTS || q->proj_pending >= 0)
        return;
    if (slot == q->proj_cur)
        return;                                         /* (the open one: nothing to do) */
    if (q->dirty || (q->proj_used >> q->proj_cur & 1))
        rfmv_proj_save_now(m);
    if (q->proj_used >> slot & 1) {
        q->proj_pending = (int8_t)slot;
        rfmv_pfw_press(m, slot);                        /* (0.13.0: its firmwares' loading screen at the press) */
        emit_slot(m, RFM_MOVE_ACT_PROJECT_LOAD, slot, NULL, 0);   /* (last: the host may answer at once) */
        return;
    }
    rfm_move_project_load(m, slot, NULL, 0);           /* a new, empty project, kept at once */
    rfmv_pfw_new(m, slot);                              /* (0.13.0: its three slots empty) */
    rfmv_proj_save_now(m);
}

void rfmv_proj_delete(rfm_move_t *m, int slot)
{
    rfm_move_seq_t *q = &m->seq;
    if (slot < 0 || slot >= RFM_MOVE_PROJECTS || q->proj_pending >= 0)
        return;
    q->proj_used &= ~(1u << slot);
    q->info[slot].ok = 0;
    if (slot == q->proj_cur) {                          /* the open one: it empties (and is not saved again; the
                                                         * transport as it was) */
        const int was = q->playing || q->start_pending;
        rfmv_seq_stop(m);
        rfmv_seq_ui_release(m);
        rfmv_seq_project_clear(m);
        if (was)
            rfmv_seq_start(m);
    }
    emit_slot(m, RFM_MOVE_ACT_PROJECT_DELETE, slot, NULL, 0);
}

/* ================================================================ projects mode === */
/* A mode of its own (the owner's): the 32 projects on the pads (top-left = 1), in their colours; a pad loads its
 * project (an empty one: a new project there); Delete / Copy + a pad: cut / copy it into the host's buffer, or paste
 * the buffer into an empty one; a pad held + knob 1: its colour; Play plays the selected project; Back or Menu
 * leaves. Everything else does nothing here. */
static const rfmv_rgb_t PROJ_RGB[RFM_MOVE_PROJ_COLORS] = {
    { 255, 36, 36 },  { 255, 120, 0 },  { 255, 200, 0 },  { 190, 255, 40 }, { 40, 255, 70 },  { 0, 255, 170 },
    { 20, 220, 255 }, { 40, 120, 255 }, { 90, 60, 255 },  { 170, 60, 255 }, { 255, 50, 210 }, { 255, 110, 150 },
    { 255, 160, 100 }, { 160, 110, 60 }, { 150, 150, 160 }, { 255, 255, 255 },
};
static int proj_color(const rfm_move_t *m, int slot)   /* 1..16 */
{
    rfm_move_proj_info_t in;
    rfmv_proj_info(m, slot, &in);
    return in.color ? in.color : slot % RFM_MOVE_PROJ_COLORS + 1;
}
rfmv_rgb_t rfmv_proj_rgb(const rfm_move_t *m, int slot) { return PROJ_RGB[proj_color(m, slot) - 1]; }

static int slot_full(const rfm_move_t *m, int slot)
{
    return (m->seq.proj_used >> slot & 1) || (slot == m->seq.proj_cur && m->seq.dirty);
}
/* (0.12.0, a12) knob 1 has its job (the colour) only while a project's pad is held */
int rfmv_proj_knob(const rfm_move_t *m) { return m->seq.proj_held >= 0 && slot_full(m, m->seq.proj_held); }
const char *rfmv_proj_colour_name(int c)
{
    static const char *const N[RFM_MOVE_PROJ_COLORS] = { "Red", "Orange", "Yellow", "Lime", "Green", "Mint", "Cyan",
                                                         "Blue", "Indigo", "Purple", "Magenta", "Pink", "Peach",
                                                         "Brown", "Grey", "White" };
    return N[(c - 1 + RFM_MOVE_PROJ_COLORS) % RFM_MOVE_PROJ_COLORS];
}

void rfmv_proj_enter(rfm_move_t *m)
{
    rfm_move_seq_t *q = &m->seq;
    m->pj_press = m->pj_del = m->pj_ack = -1, m->pj_undo = 0;
    rfmv_release_holds(m);                              /* (everything held let go: its releases come here now) */
    q->projects = 1, q->smenu = 0, q->proj_held = -1, q->play_after_load = 0;
    q->mods = q->mods_used = 0, q->shift_lock = 0;
    rfmv_pop(m, POP_MODE, 20);                          /* (Move's set icon over "Projects", a second) */
    rfmv_fw_enter(m);                                   /* (0.4.0: the track it opens on; a firmware track: no name) */
}
static void proj_leave(rfm_move_t *m)
{
    rfm_move_seq_t *q = &m->seq;
    int i;
    q->projects = 0, q->mods = q->mods_used = 0, q->proj_held = -1;
    if (m->s.fw_fx != (m->fw.fx != 0)) {                /* (0.4.5) FX setup showing as the view is left: it opens
                                                         * there next time */
        m->s.fw_fx = m->fw.fx != 0;
        rfmv_changed(m);
    }
    m->fw.fx = 0;
    for (i = 0; i < RFM_MOVE_NPADS; i++)
        m->pad_down[i] = 0, m->pad[i].kind = 0;
    for (i = 0; i < 128; i++)
        m->btn[i].kind = 0;
    for (i = 0; i < RFM_MOVE_NSTEPS; i++)
        m->step[i].kind = 0;
    m->pop = POP_NONE;
}
/* Back (or, before the browser, Menu) in the projects view: out (a copy's mark goes; a cut stays in the buffer) */
void rfmv_proj_back(rfm_move_t *m)
{
    m->pj_undo = 0, m->pj_press = -1;                   /* (0.14.0: leaving the page ends the delete's undo) */
    if (m->seq.pbuf_kind == 1)
        m->seq.pbuf_kind = 0;
    proj_leave(m);
}

/* cut / copy a full slot into the host's buffer, or paste it into an empty one */
static void proj_clip(rfm_move_t *m, int slot, int cut)
{
    rfm_move_seq_t *q = &m->seq;
    if (q->proj_pending >= 0)
        return;
    if (slot_full(m, slot)) {
        if (slot == q->proj_cur && (q->dirty || !(q->proj_used >> slot & 1)))
            rfmv_proj_save_now(m);                      /* (the open one: stored as it is first) */
        rfmv_proj_info(m, slot, &q->pbuf_info);
        emit_slot(m, cut ? RFM_MOVE_ACT_PROJECT_CUT : RFM_MOVE_ACT_PROJECT_COPY, slot, NULL, 0);
        q->pbuf_kind = (uint8_t)(cut ? 2 : 1), q->pbuf_src = (uint8_t)slot;
        m->pj_undo = (uint8_t)(cut ? 1 : 0);            /* (0.14.0: Undo brings a deleted one back; a copy ends it) */
        if (cut) {                                      /* (gone at once; the open one: an empty project there) */
            m->pj_del = (int8_t)slot, m->pj_del_ms = rfmv_now_ms(m);   /* (0.14.0: a red flash, then dark; the grid
                                                         * stays in view: its X while Delete is held, then a dot) */
            if (m->pop == POP_PROJ)
                m->pop = POP_NONE;
            q->proj_used &= ~(1u << slot);
            q->info[slot].ok = 0;
            if (slot == q->proj_cur) {
                rfm_move_project_load(m, slot, NULL, 0);
                q->proj_used &= ~(1u << slot);
            }
        }
        return;
    }
    if (!q->pbuf_kind)
        return;
    emit_slot(m, RFM_MOVE_ACT_PROJECT_PASTE, slot, NULL, 0);
    q->proj_used |= 1u << slot;
    q->info[slot] = q->pbuf_info;
    m->pj_undo = 0;                                     /* (0.14.0: a paste ends the delete's undo) */
    if (q->pbuf_kind == 2)
        q->pbuf_kind = 0;                               /* (a cut is pasted once) */
    else
        q->pbuf_src = 0xFF;                             /* (0.14.0, the owner's: "The copied pad should not still show
                                                         * as dashed after a paste": the copy stays in the buffer for
                                                         * another paste, its source no longer marked) */
}
/* (0.14.0, the owner's: "No undo for deleted project") Undo, right after Delete + a project: it is back in its slot
 * as it was (the host's buffer pasted there: what a paste carries, the firmwares' memories too); the open one is
 * opened again. One step; a paste, a copy, another delete (its own undo then) or leaving the page ends it */
static void proj_undo(rfm_move_t *m)
{
    rfm_move_seq_t *q = &m->seq;
    const int slot = q->pbuf_src;
    char w[24];
    if (!m->pj_undo || q->pbuf_kind != 2 || slot >= RFM_MOVE_PROJECTS || slot_full(m, slot) || q->proj_pending >= 0)
        return;
    m->pj_undo = 0, m->pj_del = -1;
    emit_slot(m, RFM_MOVE_ACT_PROJECT_PASTE, slot, NULL, 0);
    q->proj_used |= 1u << slot;
    q->info[slot] = q->pbuf_info;
    q->pbuf_kind = 0;
    if (slot == q->proj_cur) {                          /* (the open one was emptied: it loads again) */
        q->dirty = 0;
        q->proj_pending = (int8_t)slot;
        rfmv_pfw_press(m, slot);
        emit_slot(m, RFM_MOVE_ACT_PROJECT_LOAD, slot, NULL, 0);
    }
    snprintf(w, sizeof w, "Undo Project %02d", slot + 1);
    rfm_move_popup(m, w);
}
int rfmv_proj_undo_lit(const rfm_move_t *m)
{
    return m->pj_undo && m->seq.pbuf_kind == 2 && m->seq.pbuf_src < RFM_MOVE_PROJECTS && !slot_full(m, m->seq.pbuf_src);
}

void rfmv_proj_input(rfm_move_t *m, const mr_event_t *ev, uint32_t now)
{
    rfm_move_seq_t *q = &m->seq;
    (void)now;
    switch (ev->type) {
    case MR_EV_PAD: {
        const int p = ev->index, slot = (3 - p / 8) * 8 + p % 8;
        if (p < 0 || p >= RFM_MOVE_NPADS)
            return;
        if (!ev->value) {
            const int click = m->pj_press == slot && !m->pj_used && rfmv_now_ms(m) - m->pj_press_ms < RFM_MOVE_TAP_MS;
            m->pad_down[p] = 0;
            if (q->proj_held == slot)
                q->proj_held = -1;
            if (m->pj_press == slot)
                m->pj_press = -1;
            rfmv_pop_let(m, POP_PROJ, slot);
            if (click) {                                /* (0.14.0) the click: at its release */
                if (slot == q->proj_cur && q->proj_pending < 0) {
                    proj_save(m, 1);                    /* (the open one: saved now, "Saved", its pad blinks; nothing
                                                         * reloads; value 1: the firmwares as they are now too) */
                    m->pj_ack = (int8_t)slot, m->pj_ack_ms = rfmv_now_ms(m);
                    rfm_move_popup(m, "Saved");
                } else
                    rfmv_proj_open(m, slot);
            }
            return;
        }
        if (m->pad_down[p])
            return;
        m->pad_down[p] = 1, q->proj_held = (int8_t)slot;
        if (q->mods & (SEQ_M_DELETE | SEQ_M_COPY)) {
            q->mods_used |= q->mods & (SEQ_M_DELETE | SEQ_M_COPY);
            proj_clip(m, slot, (q->mods & SEQ_M_DELETE) != 0);
            return;
        }
        /* (0.14.0, the owner's: "Holding a project tab should just show info not load the project") its info at the
         * press; a click opens it at the release; held past RFM_MOVE_TAP_MS: info only, its release nothing */
        rfmv_pop(m, POP_PROJ, slot);
        m->pj_press = (int8_t)slot, m->pj_press_ms = rfmv_now_ms(m), m->pj_used = 0;
        return;
    }
    case MR_EV_BUTTON: {
        const int cc = ev->index, down = ev->value != 0;
        const int mb = cc == MR_CC_DELETE ? SEQ_M_DELETE : cc == MR_CC_COPY ? SEQ_M_COPY : 0;
        if (cc >= MR_CC_TRACK4 && cc <= MR_CC_TRACK1) {   /* (inert; a release still lets the button go) */
            if (!down)
                m->track_held = (uint8_t)(m->track_held & ~(1u << (MR_CC_TRACK1 - cc)));
            return;
        }
        if (mb) {
            q->mods = (uint8_t)(down ? q->mods | mb : q->mods & ~mb);
            return;
        }
        if (!down)
            return;
        if (cc == MR_CC_BACK || cc == MR_CC_MENU)       /* out */
            rfmv_proj_back(m);
        else if (cc == MR_CC_UNDO)
            proj_undo(m);                               /* (0.14.0: a deleted project back) */
        else if (cc == MR_CC_PLAY) {                    /* the selected project plays (loading: once loaded) */
            if (rfmv_pfw_play(m))
                ;                                       /* (0.13.0: its firmwares loading: once they are in) */
            else if (q->playing || q->start_pending)
                rfmv_seq_stop(m);
            else if (q->proj_pending >= 0)
                q->play_after_load = 1;
            else
                rfmv_seq_start(m);
        }
        return;
    }
    case MR_EV_KNOB:                                    /* a pad held + knob 1: its colour (one a few ticks) */
        if (ev->index == 0 && q->proj_held >= 0 && slot_full(m, q->proj_held)) {
            m->pj_used = 1;                             /* (0.14.0: its release opens nothing) */
            const int slot = q->proj_held, d = rfmv_list_ticks(m, 0, ev->value);
            int c;
            if (!d) {                                   /* (not a whole colour yet: its screen at once) */
                rfmv_pop(m, POP_PROJ, slot);
                m->pop_val = 1;
                return;
            }
            c = ((proj_color(m, slot) - 1 + d) % RFM_MOVE_PROJ_COLORS + RFM_MOVE_PROJ_COLORS) % RFM_MOVE_PROJ_COLORS + 1;
            if (slot == q->proj_cur)
                q->color = (uint8_t)c;
            q->info[slot].color = (uint8_t)c;
            {
                rfm_move_act_t a;
                memset(&a, 0, sizeof a);
                a.kind = RFM_MOVE_ACT_PROJECT_COLOR, a.index = (uint8_t)slot, a.value = (int16_t)c;
                if (m->fn)
                    m->fn(m->ctx, &a);
            }
            rfmv_pop(m, POP_PROJ, slot);
            m->pop_val = 1;                             /* (knob 1: the colour popup) */
        }
        return;
    case MR_EV_TOUCH:
        if (!ev->value && ev->index == 0)
            m->list_acc[0] = 0;
        if (ev->value && ev->index == 0 && rfmv_proj_knob(m)) {
            rfmv_pop(m, POP_PROJ, q->proj_held);        /* (0.12.0, a12) touched: "Colour" and its name */
            m->pop_val = 1;
        }
        return;
    default: return;                                    /* (steps, the jog, the other knobs: nothing) */
    }
}

/* the pads: each project in its colour (dim; the open one full, pulsing on the beat while it plays; loading:
 * blinking; empty: off; a cut slot: red, blinking; copied: blinking); Back, Menu, Copy, Delete, Play lit */
void rfmv_proj_leds(const rfm_move_t *m, rfmv_rgb_t c[L_WHITE0], uint8_t w[13])
{
    const rfm_move_seq_t *q = &m->seq;
    const int bl = q->tl >= 0 && (q->tl / 48) & 1, beat = q->playing && q->T >= 0 ? 256 - (int)(q->T % 96) * 120 / 96 : 256;
    const uint32_t now = rfmv_now_ms(m);
    int i;
    memset(c, 0, sizeof(rfmv_rgb_t) * L_WHITE0);
    memset(w, 0, 13);
    for (i = 0; i < RFM_MOVE_NPADS; i++) {
        const int slot = (3 - i / 8) * 8 + i % 8;
        rfmv_rgb_t v = RFMV_OFF;
        if (m->pj_del == slot && !slot_full(m, slot)) {   /* (0.14.0, the owner's) deleted: a red flash, then dark */
            if (now - m->pj_del_ms < RFMV_PJ_DEL_MS)
                v = RFMV_RED;
        } else if (slot == q->proj_pending)
            v = rfmv_sc(rfmv_proj_rgb(m, slot), bl ? 256 : 30);
        else if (slot == q->proj_cur && m->pj_ack == slot && now - m->pj_ack_ms < 3 * RFMV_PJ_ACK_MS)
            v = (now - m->pj_ack_ms) / RFMV_PJ_ACK_MS == 1 ? RFMV_WHITE : RFMV_OFF;   /* (saved: off, on, off) */
        else if (slot == q->proj_cur)                   /* (0.14.0, the owner's: "should be brighter") white: on the
                                                         * beat while playing, breathing as the active state when not */
            v = q->playing ? rfmv_sc(RFMV_WHITE, beat) : rfmv_active_rgb(now);
        else if (slot_full(m, slot))
            v = rfmv_sc(rfmv_proj_rgb(m, slot), q->pbuf_kind == 1 && q->pbuf_src == slot && bl ? 256 : 60);
        c[L_PAD0 + i] = v;
    }
    c[L_PLAY] = q->playing ? RFMV_GREEN : rfmv_sc(RFMV_WHITE, 40);
    w[W_BACK] = 100, w[W_MENU] = 60;
    w[W_COPY] = (q->mods & SEQ_M_COPY) ? 127 : 40;
    w[W_DELETE] = (q->mods & SEQ_M_DELETE) ? 127 : 40;
    w[W_UNDO] = rfmv_proj_undo_lit(m) ? 127 : 0;      /* (0.14.0: only while a deleted project can come back) */
}
