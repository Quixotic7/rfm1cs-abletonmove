/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm_move: the Move as a RemoteFM1 panel. The FM-1 mode's mapping against the owner's design
 * (design/remotefm1move.movedesign.json), the note layouts against hand-computed notes, (0.8.0) the Track buttons (a
 * tap, a double click, the held menus), (0.9.0, proposal 3) a slot's note layout = the sequencer on its selected
 * track, Back no flip, the merged Track menu, per-firmware solo, the browser's double tap, the clock out, release-all,
 * settings round trips, LED diffing, and the screen's golden frame.
 * Events go in as the Move's raw USB-MIDI packets through libmoveremote's own mr_decode. */
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "moveremote/client.h"
#include "move_int.h"
#include "rfm1cs/move.h"

static rfm_move_act_t acts[512];
static int nact;
static void on_act(void *ctx, const rfm_move_act_t *a)
{
    (void)ctx;
    if (nact < 512)
        acts[nact++] = *a;
}
static void clear(void) { nact = 0; }

static uint32_t now_ms;
static void raw(rfm_move_t *m, uint8_t st, uint8_t d1, uint8_t d2)
{
    const uint8_t pkt[4] = { (uint8_t)(st >> 4), st, d1, d2 };
    mr_event_t ev;
    mr_decode(pkt, 0, &ev);
    rfm_move_input(m, &ev, now_ms += 5);
}
static void pad(rfm_move_t *m, int p, int vel) { raw(m, vel ? 0x90 : 0x80, (uint8_t)(MR_NOTE_PAD0 + p), (uint8_t)vel); }
static void pad_at(rfm_move_t *m, int p, int v) { raw(m, 0xA0, (uint8_t)(MR_NOTE_PAD0 + p), (uint8_t)v); }
static void step(rfm_move_t *m, int s, int down) { raw(m, down ? 0x90 : 0x80, (uint8_t)(MR_NOTE_STEP0 + s), down ? 127 : 0); }
static void button(rfm_move_t *m, int cc, int down) { raw(m, 0xB0, (uint8_t)cc, down ? 127 : 0); }
static void knob(rfm_move_t *m, int k, int d) { raw(m, 0xB0, (uint8_t)(MR_CC_KNOB1 + k), (uint8_t)(d & 127)); }
static void jog(rfm_move_t *m, int d) { raw(m, 0xB0, MR_CC_JOG, (uint8_t)(d & 127)); }
static void touch(rfm_move_t *m, int k, int on) { raw(m, on ? 0x90 : 0x80, (uint8_t)k, on ? 127 : 0); }
static void track(rfm_move_t *m, int t, int down) { button(m, MR_CC_TRACK1 - t, down); }   /* t 0..3 */

static int has(uint8_t kind, int index, int down, int value)
{
    int i;
    for (i = 0; i < nact; i++)
        if (acts[i].kind == kind && acts[i].index == index && acts[i].down == down && acts[i].value == value)
            return 1;
    return 0;
}
static int has_midi(int s, int d1, int d2)
{
    int i;
    for (i = 0; i < nact; i++)
        if (acts[i].kind == RFM_MOVE_ACT_MIDI && acts[i].n == 3 && acts[i].bytes[0] == s && acts[i].bytes[1] == d1 &&
            acts[i].bytes[2] == d2)
            return 1;
    return 0;
}
static int count(uint8_t kind)
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += acts[i].kind == kind;
    return n;
}

/* ---- the design's labels (pad-<row>-<col>, row 1 the top), and the FM-1's keys' names (key 0 = F3) */
static const char *const DESIGN[4][8] = {
    { "", "F#", "G#", "A#", "", "C#", "D#", "" },
    { "F", "G", "A", "B", "C", "D", "E", "F" },
    { "F#", "G#", "A#", "", "C#", "D#", "", "F#" },
    { "G", "A", "B", "C", "D", "E", "F", "G" },
};
static const char *const NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *key_name(int k) { return NAMES[(53 + k) % 12]; }
static int key_black(int k) { return (0x54A >> ((k + 5) % 12)) & 1; }
static int pad_of(int row, int col) { return (4 - row) * 8 + (col - 1); }

static void test_fm1_pads(void)
{
    rfm_move_t m;
    int row, col, k, seen[27] = { 0 }, nkeys = 0;
    for (row = 1; row <= 4; row++)
        for (col = 1; col <= 8; col++) {
            const int p = pad_of(row, col), key = rfm_move_pad_key(p);
            if (!DESIGN[row - 1][col - 1][0]) {
                CHECKF(key < 0, "pad-%d-%d is unused in the design, plays %d", row, col, key);
                continue;
            }
            CHECKF(key >= 0 && key < 27 && !strcmp(key_name(key), DESIGN[row - 1][col - 1]),
                   "pad-%d-%d: key %d (%s), the design says %s", row, col, key, key >= 0 ? key_name(key) : "-",
                   DESIGN[row - 1][col - 1]);
            if (key >= 0 && key < 27)
                seen[key]++, nkeys++;
        }
    CHECK(nkeys == 27);
    for (k = 0; k < 27; k++)
        CHECKF(seen[k] == 1, "key %d on %d pads", k, seen[k]);
    /* the low octave is rows 1-2 (F3..F4), the high one rows 3-4 (F#4..G5); whites ascend along their rows */
    for (col = 1; col <= 8; col++) {
        CHECK(rfm_move_pad_key(pad_of(2, col)) <= 12 && !key_black(rfm_move_pad_key(pad_of(2, col))));
        CHECK(rfm_move_pad_key(pad_of(4, col)) >= 14 && !key_black(rfm_move_pad_key(pad_of(4, col))));
        if (col > 1) {
            CHECK(rfm_move_pad_key(pad_of(2, col)) > rfm_move_pad_key(pad_of(2, col - 1)));
            CHECK(rfm_move_pad_key(pad_of(4, col)) > rfm_move_pad_key(pad_of(4, col - 1)));
        }
    }
    CHECK(rfm_move_pad_key(pad_of(2, 1)) == 0 && rfm_move_pad_key(pad_of(4, 8)) == 26);
    CHECK(rfm_move_pad_key(pad_of(3, 1)) == 13 && rfm_move_pad_key(pad_of(3, 8)) == 25);

    /* through the input: velocity, pressure, release */
    rfm_move_init(&m, NULL, on_act, NULL);
    for (row = 1; row <= 4; row++)
        for (col = 1; col <= 8; col++) {
            const int p = pad_of(row, col), key = rfm_move_pad_key(p);
            clear();
            pad(&m, p, 90);
            if (key < 0) {
                CHECK(nact == 0);
                pad(&m, p, 0);
                CHECK(nact == 0);
                continue;
            }
            CHECK(nact == 1 && acts[0].kind == RFM_MOVE_ACT_KEY && acts[0].index == key && acts[0].down == 1 &&
                  acts[0].vel == 90);
            clear();
            pad_at(&m, p, 64);
            CHECK(nact == 1 && has(RFM_MOVE_ACT_PRESS, key, 0, 64));
            pad_at(&m, p, 64);                          /* the same pressure: nothing */
            CHECK(nact == 1);
            clear();
            pad(&m, p, 0);
            CHECK(nact == 2 && acts[0].kind == RFM_MOVE_ACT_PRESS && acts[0].value == 0 &&
                  acts[1].kind == RFM_MOVE_ACT_KEY && acts[1].index == key && acts[1].down == 0);
        }
    CHECK(rfm_move_lean(127) == 100 && rfm_move_lean(64) == 50 && rfm_move_lean(0) == 0);
}

static void test_steps(void)
{
    rfm_move_t m;
    int s;
    rfm_move_init(&m, NULL, on_act, NULL);
    for (s = 0; s < 16; s++) {
        const int k = rfm_move_step_key(s);
        CHECKF(k >= 0 && !key_black(k), "step %d: key %d", s + 1, k);
        if (s)
            CHECK(k > rfm_move_step_key(s - 1));
        clear();
        step(&m, s, 1);
        CHECK(nact == 1 && has(RFM_MOVE_ACT_KEY, k, 1, 0) && acts[0].vel == 100);
        clear();
        step(&m, s, 0);
        CHECK(nact == 1 && has(RFM_MOVE_ACT_KEY, k, 0, 0));
    }
    CHECK(rfm_move_step_key(0) == 0 && rfm_move_step_key(15) == 26);
    /* a white key held by a pad and a step: down with the first, up with the last */
    clear();
    pad(&m, pad_of(2, 1), 80);
    step(&m, 0, 1);
    pad(&m, pad_of(2, 1), 0);
    CHECK(nact == 1);
    step(&m, 0, 0);
    CHECK(nact == 2 && has(RFM_MOVE_ACT_KEY, 0, 0, 0));
}

static void test_buttons_knobs(void)
{
    static const struct { int cc, b; } B[] = {
        { MR_CC_CAPTURE, RFM_MOVE_B_FX }, { MR_CC_SAMPLE, RFM_MOVE_B_SEL }, { MR_CC_MUTE, RFM_MOVE_B_ENV },
        { MR_CC_DELETE, RFM_MOVE_B_LFO }, { MR_CC_COPY, RFM_MOVE_B_EDIT }, { MR_CC_SHIFT, RFM_MOVE_B_GLO },
        { MR_CC_MENU, RFM_MOVE_B_HOME }, { MR_CC_UNDO, RFM_MOVE_B_SAVE }, { MR_CC_LOOP, RFM_MOVE_B_ARP },
        { MR_CC_PLAY, RFM_MOVE_B_PLAY }, { MR_CC_REC, RFM_MOVE_B_REC },   /* (0.4.3: Back is no FM-1 button) */
        { MR_CC_LEFT, RFM_MOVE_B_OCTDN }, { MR_CC_RIGHT, RFM_MOVE_B_OCTUP },
    };
    static const int ENC[8] = { RFM_MOVE_E_SELECT, RFM_MOVE_E_PRESETS, RFM_MOVE_E_ALGO, -1,   /* (0.8.0) */
                                RFM_MOVE_E_K1, RFM_MOVE_E_K2, RFM_MOVE_E_K3, RFM_MOVE_E_K4 };
    rfm_move_t m;
    int i, covered = 0;
    rfm_move_init(&m, NULL, on_act, NULL);
    /* the buttons: the FM-1 layout's (0.8.0: a note layout has none of the FM-1's; Sequencer mode takes them over,
     * test_move_seq.c) */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    for (i = 0; i < 13; i++) {
        clear();
        button(&m, B[i].cc, 1);
        CHECKF(nact == 1 && has(RFM_MOVE_ACT_BTN, B[i].b, 1, 0), "cc %d", B[i].cc);
        button(&m, B[i].cc, 1);                         /* (a repeat: nothing) */
        CHECK(nact == 1);
        button(&m, B[i].cc, 0);
        CHECK(nact == 2 && has(RFM_MOVE_ACT_BTN, B[i].b, 0, 0));
        covered |= 1 << B[i].b;
    }
    /* (0.8.0) a note layout: no FM-1 button at all (Play is the sequencer's transport there; 0.9.0: a note layout IS
     * the sequencer, rfm_move_set_mode(MIDI) the slot's note layout) */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_MIDI);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && rfm_move_slot_layout(&m, 0) == RFM_MOVE_LAY_SCALE);
    for (i = 0; i < 13; i++) {
        clear();
        button(&m, B[i].cc, 1), button(&m, B[i].cc, 0);
        CHECKF(count(RFM_MOVE_ACT_BTN) == 0, "note layout: cc %d", B[i].cc);
    }
    CHECK(m.seq.playing || m.seq.start_pending);        /* (Play: the transport started) */
    {   /* (0.9.0) its own clock out to every firmware: Start, a clock every 4 ticks (24 PPQN), Stop */
        int fa = 0, f8 = 0, fc = 0, k;
        clear();
        for (k = 0; k < 50; k++)
            rfm_move_seq_advance(&m, 480, 48000);       /* (0.5 s: 120 BPM, 24 clocks) */
        for (k = 0; k < nact; k++)
            if (acts[k].kind == RFM_MOVE_ACT_MIDI && acts[k].n == 1 && acts[k].dest == RFM_MOVE_DEST_ALL)
                fa += acts[k].bytes[0] == 0xFA, f8 += acts[k].bytes[0] == 0xF8;
        CHECKF(fa == 1 && f8 >= 22 && f8 <= 26, "FA %d, F8 %d", fa, f8);
        clear();
        button(&m, MR_CC_PLAY, 1), button(&m, MR_CC_PLAY, 0);
        for (k = 0; k < nact; k++)
            fc += acts[k].kind == RFM_MOVE_ACT_MIDI && acts[k].n == 1 && acts[k].bytes[0] == 0xFC &&
                  acts[k].dest == RFM_MOVE_DEST_ALL;
        CHECK(fc == 1 && count(RFM_MOVE_ACT_BTN) == 0);
    }
    CHECK(!m.seq.playing && !m.seq.start_pending);
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    CHECK(covered == (((1 << 14) - 1) & ~(1 << RFM_MOVE_B_SEQ)) && rfm_move_button_of(MR_CC_BACK) < 0);
    CHECK(rfm_move_button_of(MR_CC_UP) == RFM_MOVE_B_SEQ && rfm_move_button_of(MR_CC_DOWN) < 0 &&
          rfm_move_button_of(MR_CC_TRACK1) < 0);
    /* (0.4.4) FM-1 mode: Up is the FM-1's SEQ; Down (no host firmware list here) nothing; no octave */
    rfm_move_set_mode(&m, 0);
    clear();
    button(&m, MR_CC_UP, 1);
    CHECK(nact == 1 && has(RFM_MOVE_ACT_BTN, RFM_MOVE_B_SEQ, 1, 0));
    button(&m, MR_CC_UP, 1);
    CHECK(nact == 1);
    button(&m, MR_CC_UP, 0);
    CHECK(nact == 2 && has(RFM_MOVE_ACT_BTN, RFM_MOVE_B_SEQ, 0, 0));
    clear();
    button(&m, MR_CC_DOWN, 1), button(&m, MR_CC_DOWN, 0);
    CHECK(nact == 0 && m.s.octave == 0);
    /* .. the MIDI modes: Up the octave, not SEQ */
    rfm_move_set_mode(&m, 1);
    clear();
    button(&m, MR_CC_UP, 1), button(&m, MR_CC_UP, 0);
    CHECK(count(RFM_MOVE_ACT_BTN) == 0 && m.s.octave == 1);
    button(&m, MR_CC_DOWN, 1), button(&m, MR_CC_DOWN, 0);
    CHECK(m.s.octave == 0);
    rfm_move_set_mode(&m, 0);

    /* the knobs, the jog wheel, the volume knob */
    for (i = 0; i < 8; i++) {
        CHECK(rfm_move_knob_enc(i) == ENC[i]);
        clear();
        knob(&m, i, 6);                                 /* (the owner's: an FM-1 detent every 2 of the Move's ticks) */
        knob(&m, i, -4);
        if (ENC[i] < 0)
            CHECK(nact == 0);
        else
            CHECK(nact == 2 && has(RFM_MOVE_ACT_TURN, ENC[i], 0, 3) && has(RFM_MOVE_ACT_TURN, ENC[i], 0, -2));
    }
    clear();
    knob(&m, 4, 1);                                     /* a tick: nothing yet; the next: a detent */
    CHECK(nact == 0);
    knob(&m, 4, 1);
    CHECK(nact == 1 && has(RFM_MOVE_ACT_TURN, ENC[4], 0, 1));
    knob(&m, 4, 1), knob(&m, 4, -1), knob(&m, 4, -1);   /* (a turn back forgets the half: -1 needs two) */
    CHECK(nact == 2 && has(RFM_MOVE_ACT_TURN, ENC[4], 0, -1));
    knob(&m, 4, 3);                                     /* (3 ticks: 1 detent, 1 left over) */
    touch(&m, 4, 1), touch(&m, 4, 0);                   /* (the release forgets it) */
    clear();
    knob(&m, 4, 1);
    CHECK(nact == 0);
    knob(&m, 4, -1);
    clear();
    clear();
    jog(&m, -1);
    jog(&m, 5);
    CHECK(nact == 2 && has(RFM_MOVE_ACT_TURN, RFM_MOVE_E_SELECT, 0, -1) && has(RFM_MOVE_ACT_TURN, RFM_MOVE_E_SELECT, 0, 5));
    /* (0.8.0) the jog's click: what it turns, SELECT -> PRESETS -> ALGO (named), no action itself */
    clear();
    raw(&m, 0xB0, MR_CC_JOG_CLICK, 127), raw(&m, 0xB0, MR_CC_JOG_CLICK, 0);
    CHECK(nact == 0 && m.jog_enc == 1 && m.pop == POP_JOG && m.pop_i == 1);
    jog(&m, 2);                                         /* (a detent a tick, as knob 2 turns it) */
    CHECK(nact == 1 && has(RFM_MOVE_ACT_TURN, RFM_MOVE_E_PRESETS, 0, 2));
    clear();
    raw(&m, 0xB0, MR_CC_JOG_CLICK, 127), raw(&m, 0xB0, MR_CC_JOG_CLICK, 0);
    jog(&m, -1);
    CHECK(m.jog_enc == 2 && nact == 1 && has(RFM_MOVE_ACT_TURN, RFM_MOVE_E_ALGO, 0, -1));
    clear();
    raw(&m, 0xB0, MR_CC_JOG_CLICK, 127), raw(&m, 0xB0, MR_CC_JOG_CLICK, 0);
    jog(&m, 1);
    CHECK(m.jog_enc == 0 && nact == 1 && has(RFM_MOVE_ACT_TURN, RFM_MOVE_E_SELECT, 0, 1));
    /* MASTER: 16 a detent from the host's value, clamped */
    rfm_move_set_master(&m, 500);
    clear();
    knob(&m, 8, 2);
    CHECK(nact == 1 && has(RFM_MOVE_ACT_MASTER, 0, 0, 532));
    knob(&m, 8, 63);
    knob(&m, 8, 63);
    CHECK(acts[nact - 1].value == 1023 && m.master == 1023);
    clear();
    knob(&m, 8, 1);                                     /* at the top: no action */
    CHECK(nact == 0);
    knob(&m, 8, -63), knob(&m, 8, -63);
    CHECK(acts[nact - 1].kind == RFM_MOVE_ACT_MASTER && acts[nact - 1].value == 0);
    clear();
    knob(&m, 8, -1);
    CHECK(nact == 0);
    /* knob touch: remembered for the screen */
    {
        touch(&m, 4, 1);
        CHECK(m.touched == 4 && m.pop == POP_ENC && m.pop_blank);   /* (0.5.6, FM-1 mode: a touch shows its name) */
    }
    touch(&m, 4, 0);
    CHECK(m.touched == -1);
}

/* (0.8.0) a Track button's tap and its double click: pressed / released with time between (a tap is let go within
 * RFM_MOVE_TAP_MS; the next press within RFM_MOVE_DOUBLE_MS of it is a double click) */
static void gap(void) { now_ms += RFM_MOVE_DOUBLE_MS + 50; }
static void tap(rfm_move_t *m, int t) { gap(), track(m, t, 1), track(m, t, 0), gap(); }
static void dbl(rfm_move_t *m, int t) { gap(), track(m, t, 1), track(m, t, 0), track(m, t, 1), track(m, t, 0), gap(); }
static void back(rfm_move_t *m) { button(m, MR_CC_BACK, 1), button(m, MR_CC_BACK, 0); }   /* a click */
/* Track t (0..2) held + its layout menu's top-row pad: the slot's layout (RFM_MOVE_LAY_*) */
static void lay(rfm_move_t *m, int t, int l)
{
    track(m, t, 1);
    pad(m, 24 + l, 100), pad(m, 24 + l, 0);
    track(m, t, 0);
}

/* a host with two firmwares, loaded in slots 1 and 2 (Tracks 1, 2; Track 3 empty) */
static void two_slots(rfm_move_t *m)
{
    static rfm_move_fw_t fl[2];
    static const char *const sl[3] = { "a", "b", NULL };
    fl[0].id = "a", fl[0].name = "A", fl[0].version = fl[0].installed = "1";
    fl[1].id = "b", fl[1].name = "B", fl[1].version = fl[1].installed = "1";
    rfm_move_fw_list(m, fl, 2);
    rfm_move_fw_slots(m, sl);
}

static void test_modes(void)
{
    rfm_move_t m;
    rfm_move_init(&m, NULL, on_act, NULL);
    CHECK(m.s.mode == RFM_MOVE_MODE_FM1 && rfm_move_slot_layout(&m, 0) == RFM_MOVE_LAY_FM1);
    pad(&m, pad_of(2, 1), 100);
    pad_at(&m, pad_of(2, 1), 40);
    step(&m, 4, 1);
    button(&m, MR_CC_SHIFT, 1);
    clear();
    track(&m, 0, 1);                                    /* Track 1 down: nothing yet */
    CHECK(m.s.mode == RFM_MOVE_MODE_FM1 && (m.track_held & 1) && nact == 0);
    pad(&m, 24 + RFM_MOVE_LAY_SCALE, 100);              /* (0.8.0) its layout menu: Scale, everything let go; (0.9.0) a
                                                         * note layout IS the sequencer, on the slot's selected track
                                                         * (track 1), routed to it, the TRIG page, the track view */
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && rfm_move_slot_layout(&m, 0) == RFM_MOVE_LAY_SCALE &&
          m.s.layout == RFM_MOVE_LAYOUT_SCALE);
    CHECK(rfm_move_seq_lock(&m, 0) == 0 && m.seq.sel_track == 0 && (m.seq.fx.ffilt[0] & 1) && m.seq.trig &&
          !m.seq.view && m.seq.tr[0].layout == RFM_MOVE_LAYOUT_SCALE);
    CHECK(has(RFM_MOVE_ACT_KEY, 0, 0, 0) && has(RFM_MOVE_ACT_PRESS, 0, 0, 0) &&
          has(RFM_MOVE_ACT_KEY, rfm_move_step_key(4), 0, 0) && has(RFM_MOVE_ACT_BTN, RFM_MOVE_B_GLO, 0, 0));
    CHECK(count(RFM_MOVE_ACT_SETTINGS) >= 1);           /* (two now: the layout, the mode) */
    pad(&m, 24 + RFM_MOVE_LAY_SCALE, 0);
    track(&m, 0, 0);                                    /* (its menu used: no tap) */
    CHECK(m.track_held == 0 && m.s.mode == RFM_MOVE_MODE_SEQ && m.pop != POP_LAYOUT);
    clear();
    pad(&m, pad_of(2, 1), 0), step(&m, 4, 0), button(&m, MR_CC_SHIFT, 0);   /* the old presses' ends: nothing */
    CHECK(nact == 0);
    clear();
    tap(&m, 0);                                         /* the slot's tap: its layout played (its track view), named */
    CHECK(count(RFM_MOVE_ACT_SETTINGS) == 0 && m.s.mode == RFM_MOVE_MODE_SEQ && m.seq.sel_track == 0 && m.seq.trig &&
          !m.seq.view && m.pop == POP_LAYOUT && m.pop_i == 0);
    /* release-all: notes, the transport */
    pad(&m, 0, 100);
    button(&m, MR_CC_PLAY, 1);
    clear();
    rfm_move_release_all(&m);
    CHECK(has_midi(0x80, 48, 0) && count(RFM_MOVE_ACT_BTN) == 0 && !m.seq.playing && !m.seq.start_pending);
    button(&m, MR_CC_PLAY, 0);
    clear();
    rfm_move_release_all(&m);
    CHECK(nact == 0);
    /* (0.9.0) Back's click with nothing open: nothing (no Sequencer mode to flip to); Menu: session view, (0.12.0)
     * Menu again: the track view (Back: nothing) */
    clear();
    back(&m);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && !m.seq.view && nact == 0);
    button(&m, MR_CC_MENU, 1), button(&m, MR_CC_MENU, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.seq.view);
    back(&m);                                           /* (0.12.0, the owner's: Back never leaves the view; Menu) */
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.seq.view);
    button(&m, MR_CC_MENU, 1), button(&m, MR_CC_MENU, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && !m.seq.view);
    clear();
    pad(&m, 0, 100);                                    /* the live pads: track 1's channel (1) */
    CHECK(has_midi(0x90, 48, 100));
    pad(&m, 0, 0);
    /* (0.9.0) Track 1 held + step 3: track 3 selected for slot 1 (edited now: the slot is played), its channel */
    track(&m, 0, 1), step(&m, 2, 1), step(&m, 2, 0), now_ms += 400, track(&m, 0, 0);
    CHECK(rfm_move_seq_lock(&m, 0) == 2 && m.seq.sel_track == 2 && (m.seq.fx.ffilt[0] >> 2 & 1) &&
          m.s.mode == RFM_MOVE_MODE_SEQ);
    clear();
    pad(&m, 0, 100), pad(&m, 0, 0);
    CHECK(has_midi(0x92, 48, 100) && has_midi(0x82, 48, 0));
    tap(&m, 0);                                         /* (a tap: the slot's selected track kept) */
    CHECK(m.seq.sel_track == 2 && m.pop == POP_LAYOUT);
    rfm_move_set_mode(&m, RFM_MOVE_MODE_CONTROL);      /* (Control, MIDI: the note layout, the sequencer) */
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && rfm_move_slot_layout(&m, 0) == RFM_MOVE_LAY_SCALE);
    lay(&m, 0, RFM_MOVE_LAY_FM1);
    CHECK(m.s.mode == RFM_MOVE_MODE_FM1 && rfm_move_slot_layout(&m, 0) == RFM_MOVE_LAY_FM1);
}

static void test_layouts(void)
{
    rfm_move_settings_t s;
    rfm_move_t m;
    int p;
    rfm_move_settings_default(&s);
    /* 1 isomorphic in scale (C major): +1 degree a column, +3 degrees (a 4th) a row */
    s.layout = RFM_MOVE_LAYOUT_SCALE;
    CHECK(rfm_move_pad_note(&s, 0) == 48 && rfm_move_pad_note(&s, 1) == 50 && rfm_move_pad_note(&s, 2) == 52);
    CHECK(rfm_move_pad_note(&s, 3) == 53 && rfm_move_pad_note(&s, 7) == 60);
    CHECK(rfm_move_pad_note(&s, 8) == 53 && rfm_move_pad_note(&s, 16) == 59 && rfm_move_pad_note(&s, 24) == 64);
    CHECK(rfm_move_pad_note(&s, 31) == 76);
    s.scale = RFM_MOVE_SCALE_MINOR, s.root = 9;         /* A minor */
    CHECK(rfm_move_pad_note(&s, 0) == 57 && rfm_move_pad_note(&s, 2) == 60 && rfm_move_pad_note(&s, 8) == 62);
    s.scale = RFM_MOVE_SCALE_MAJOR, s.root = 0;
    /* 2 chromatic: +1 a column, +5 a row */
    s.layout = RFM_MOVE_LAYOUT_CHROMATIC;
    CHECK(rfm_move_pad_note(&s, 0) == 48 && rfm_move_pad_note(&s, 7) == 55 && rfm_move_pad_note(&s, 8) == 53);
    CHECK(rfm_move_pad_note(&s, 31) == 70);
    /* 3 drum: C2 at the bottom right, leftwards, then the next row from the right */
    s.layout = RFM_MOVE_LAYOUT_DRUM;
    CHECK(rfm_move_pad_note(&s, 7) == 36 && rfm_move_pad_note(&s, 6) == 37 && rfm_move_pad_note(&s, 0) == 43);
    CHECK(rfm_move_pad_note(&s, 15) == 44 && rfm_move_pad_note(&s, 8) == 51 && rfm_move_pad_note(&s, 31) == 60);
    CHECK(rfm_move_pad_note(&s, 24) == 67);
    /* 4 octave: a row an octave, 7 degrees and the octave */
    s.layout = RFM_MOVE_LAYOUT_OCTAVE;
    CHECK(rfm_move_pad_note(&s, 0) == 48 && rfm_move_pad_note(&s, 4) == 55 && rfm_move_pad_note(&s, 6) == 59);
    CHECK(rfm_move_pad_note(&s, 7) == 60 && rfm_move_pad_note(&s, 8) == 60 && rfm_move_pad_note(&s, 31) == 96);
    /* the octave and the edges */
    s.octave = 1;
    CHECK(rfm_move_pad_note(&s, 0) == 60);
    s.octave = 4, s.layout = RFM_MOVE_LAYOUT_OCTAVE;
    CHECK(rfm_move_pad_note(&s, 0) == 96 && rfm_move_pad_note(&s, 31) == -1);
    s.octave = -4, s.layout = RFM_MOVE_LAYOUT_DRUM;
    CHECK(rfm_move_pad_note(&s, 7) == -1 && rfm_move_pad_note(&s, 24) == 19);

    /* through the input (0.9.0): Track 1 held + top pads = its slot's layout (a note layout: the sequencer), + steps =
     * the track selected for the slot (step 10: track 10, its channel 10); Up / Down the octave */
    rfm_move_init(&m, NULL, on_act, NULL);
    track(&m, 0, 1);
    clear();
    step(&m, 9, 1), step(&m, 9, 0);
    CHECK(rfm_move_seq_lock(&m, 0) != 9);               /* (0.11.0: a click: gated; another press: it acts first) */
    pad(&m, 24 + RFM_MOVE_LAY_CHROMA, 100);
    CHECK(rfm_move_seq_lock(&m, 0) == 9 && (m.seq.fx.ffilt[0] >> 9 & 1) && count(RFM_MOVE_ACT_KEY) == 0);
    pad(&m, 24 + RFM_MOVE_LAY_CHROMA, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.s.layout == RFM_MOVE_LAYOUT_CHROMATIC &&
          rfm_move_slot_layout(&m, 0) == RFM_MOVE_LAY_CHROMA && m.seq.sel_track == 9 &&
          m.seq.tr[9].layout == RFM_MOVE_LAYOUT_CHROMATIC && count(RFM_MOVE_ACT_MIDI) == 0 &&
          count(RFM_MOVE_ACT_SETTINGS) >= 1);
    clear();
    pad(&m, 30, 100), pad(&m, 30, 0);                   /* the menu's unused pad while held: nothing */
    CHECK(nact == 0 && m.s.layout == RFM_MOVE_LAYOUT_CHROMATIC);
    track(&m, 0, 0);
    clear();
    pad(&m, 5, 70);                                     /* chromatic: pad 5 and pad 8 are both F3 (53) */
    pad(&m, 8, 90);
    CHECK(nact == 1 && has_midi(0x99, 53, 70));
    pad_at(&m, 8, 33);
    CHECK(has_midi(0xA9, 53, 33));
    pad(&m, 5, 0);
    CHECK(!has_midi(0x89, 53, 0));
    pad(&m, 8, 0);
    CHECK(has_midi(0x89, 53, 0));
    clear();
    button(&m, MR_CC_UP, 1), button(&m, MR_CC_UP, 0);
    CHECK(m.s.octave == 1 && count(RFM_MOVE_ACT_SETTINGS) == 1);
    for (p = 0; p < 10; p++)
        button(&m, MR_CC_DOWN, 1), button(&m, MR_CC_DOWN, 0);
    CHECK(m.s.octave == RFM_MOVE_OCT_MIN);
    for (p = 0; p < 10; p++)
        button(&m, MR_CC_UP, 1), button(&m, MR_CC_UP, 0);
    CHECK(m.s.octave == RFM_MOVE_OCT_MAX);
    /* a note held over an octave change ends where it began */
    m.s.octave = 0;
    clear();
    pad(&m, 0, 100);
    button(&m, MR_CC_UP, 1);
    pad(&m, 0, 0);
    CHECK(has_midi(0x99, 48, 100) && has_midi(0x89, 48, 0));
    button(&m, MR_CC_UP, 0);
    /* (0.9.0) the knobs: the sequencer's (the TRIG page first), no slot CC, no FM-1 encoder */
    clear();
    knob(&m, 0, 2);
    CHECK(m.seq.trig && count(RFM_MOVE_ACT_MIDI) == 0 && count(RFM_MOVE_ACT_TURN) == 0);
}

static void test_drum_settings(void)
{
    rfm_move_t m;
    int p;
    rfm_move_settings_t s, t;
    uint8_t img[RFM_MOVE_SETTINGS_BYTES], bad[RFM_MOVE_SETTINGS_BYTES];
    rfm_move_init(&m, NULL, on_act, NULL);
    lay(&m, 0, RFM_MOVE_LAY_DRUM);                      /* (0.8.0) Track 1 held + the Drum pad (0.9.0: the sequencer,
                                                         * track 1 in Drum) */
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.s.layout == RFM_MOVE_LAYOUT_DRUM && m.seq.tr[0].layout == RFM_MOVE_LAYOUT_DRUM);
    clear();
    pad(&m, 7, 100);                                    /* bottom right: C2 */
    CHECK(has_midi(0x90, 36, 100));
    jog(&m, 2);                                         /* held + jog: its note */
    CHECK(m.s.drum[7] == 38 && count(RFM_MOVE_ACT_TURN) == 0 && count(RFM_MOVE_ACT_SETTINGS) == 1);
    jog(&m, -1);
    CHECK(m.s.drum[7] == 37);
    pad(&m, 7, 0);
    CHECK(has_midi(0x80, 36, 0));                       /* the note that sounded ends */
    clear();
    jog(&m, 1);                                         /* no pad held: no note, no FM-1 SELECT (the sequencer's jog) */
    CHECK(count(RFM_MOVE_ACT_TURN) == 0 && count(RFM_MOVE_ACT_MIDI) == 0 && m.s.drum[7] == 37);
    jog(&m, -1);
    clear();
    pad(&m, 7, 90);
    CHECK(has_midi(0x90, 37, 90));
    pad(&m, 7, 0);
    /* the settings: a byte image, back again */
    m.s.channel = 5, m.s.octave = -2, m.s.scale = RFM_MOVE_SCALE_DORIAN, m.s.root = 7, m.s.bank = 2;
    rfmv_slot_push(&m);                                 /* (0.8.0: the active slot's record, as a change makes it) */
    m.s.cc[3][7] = 1;
    CHECK(rfm_move_save(&m.s, img) == RFM_MOVE_SETTINGS_BYTES);
    CHECK(rfm_move_load(&s, img, sizeof img) == 0 && !memcmp(&s, &m.s, sizeof s));
    rfm_move_init(&m, &s, on_act, NULL);                /* a new session from the saved settings: the slot's Drum layout,
                                                         * the sequencer on its track (1) */
    CHECK(m.s.drum[7] == 37 && m.s.mode == RFM_MOVE_MODE_SEQ && rfm_move_slot_layout(&m, 0) == RFM_MOVE_LAY_DRUM);
    CHECK(m.s.layout == RFM_MOVE_LAYOUT_DRUM);          /* CORE? (rfm_move_init: the Sequencer entered, not the slot's
                                                         * note layout: the track's own layout, Scale) */
    clear();
    pad(&m, 7, 90);
    CHECK(has_midi(0x90, 37 - 24, 90));                 /* (0.9.0: track 1's channel, not s.channel; octave -2) CORE? */
    pad(&m, 7, 0);
    /* not an image: the defaults */
    memcpy(bad, img, sizeof bad);
    bad[3] = 99;
    rfm_move_settings_default(&t);
    CHECK(rfm_move_load(&s, bad, sizeof bad) == -1 && !memcmp(&s, &t, sizeof s));
    CHECK(rfm_move_load(&s, img, sizeof img - 1) == -1);
    CHECK(rfm_move_load(&s, NULL, 0) == -1);
    /* out-of-range fields: their defaults, the rest kept */
    memcpy(bad, img, sizeof bad);
    bad[6] = 9, bad[8] = 16, bad[9] = (uint8_t)(int8_t)-7, bad[13 + 2] = 200;
    CHECK(rfm_move_load(&s, bad, sizeof bad) == 0);
    CHECK(s.mode == 0 && s.channel == 0 && s.octave == 0 && s.cc[0][2] == 18 && s.root == 7 && s.drum[7] == 37);
    /* (0.8.0) a version 6 image (88 bytes) as a 0.7 host saved it in MIDI mode (Drum, bank 3): every slot as the one
     * mode was, the bank's CCs (0.9.0: this image's mode byte is the sequencer's; written as MIDI here) */
    memcpy(bad, img, sizeof bad);
    bad[3] = 6, bad[4] = (uint8_t)RFM_MOVE_SETTINGS_BYTES_V6, bad[5] = 0;
    CHECK(bad[6] == RFM_MOVE_MODE_SEQ && bad[7] == RFM_MOVE_LAYOUT_DRUM && bad[12] == 2);
    bad[6] = RFM_MOVE_MODE_MIDI;
    CHECK(rfm_move_load(&s, bad, RFM_MOVE_SETTINGS_BYTES_V6) == 0);
    for (p = 0; p < RFM_MOVE_FW_SLOTS; p++)
        CHECKF(s.slot[p].lay == RFM_MOVE_LAY_DRUM && s.slot[p].channel == 5 && s.slot[p].octave == -2 &&
               s.slot[p].root == 7 && s.slot[p].scale == RFM_MOVE_SCALE_DORIAN && s.slot[p].cc[0] == 102 &&
               s.lock[p] == p, "slot %d", p);
    rfm_move_init(&m, &s, on_act, NULL);                /* (0.9.0) a session from it: slot 1's Drum, the sequencer (MIDI
                                                         * mode is never entered) */
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.s.layout == RFM_MOVE_LAYOUT_DRUM);   /* CORE? (rfm_move_init: MIDI) */
    bad[6] = RFM_MOVE_MODE_CONTROL;                     /* (Control mode: MIDI now) */
    CHECK(rfm_move_load(&s, bad, RFM_MOVE_SETTINGS_BYTES_V6) == 0 && s.mode == RFM_MOVE_MODE_MIDI &&
          s.slot[0].lay == RFM_MOVE_LAY_DRUM);
    bad[6] = RFM_MOVE_MODE_FM1;
    CHECK(rfm_move_load(&s, bad, RFM_MOVE_SETTINGS_BYTES_V6) == 0 && s.slot[2].lay == RFM_MOVE_LAY_FM1);
}

/* (0.8.0) the old Control mode's CCs are every note layout's knobs: each slot's own 8 CC numbers (16..23), on its
 * channel; Track n held (or Shift) + a knob its CC number. (Control mode and its four banks on the jog are gone.) */
static void test_control(void)
{
    static rfm_move_t m;
    static rfm_move_fw_t fl[2];
    static const char *const sl[3] = { "a", "b", NULL };
    int k, t;
    rfm_move_settings_t d;
    rfm_move_settings_default(&d);
    for (t = 0; t < RFM_MOVE_FW_SLOTS; t++)
        for (k = 0; k < 8; k++)
            CHECK(d.slot[t].cc[k] == 16 + k && d.slot[t].lay == RFM_MOVE_LAY_FM1);
    rfm_move_init(&m, NULL, on_act, NULL);
    rfm_move_set_mode(&m, RFM_MOVE_MODE_CONTROL);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && rfm_move_slot_layout(&m, 0) == RFM_MOVE_LAY_SCALE && m.seq.trig);
    clear();
    knob(&m, 0, 3);                                     /* (the TRIG page first: no CC) */
    CHECK(count(RFM_MOVE_ACT_MIDI) == 0);
    jog(&m, 1);                                         /* the jog: the next knob page, bank 1 (16..23) */
    CHECK(!m.seq.trig && m.s.bank == 0);
    clear();
    knob(&m, 0, 3);
    CHECK(nact == 1 && has_midi(0xB0, 16, 67) && m.pop == POP_CC && m.pop_i == 0);
    knob(&m, 7, -58);                                   /* a big turn: 64 - 58 */
    CHECK(has_midi(0xB0, 23, 6));
    knob(&m, 7, -10);
    CHECK(has_midi(0xB0, 23, 0));
    clear();
    knob(&m, 7, -1);                                    /* at 0: nothing sent */
    CHECK(nact == 0);
    /* Track 1 held + a knob: the selected track's CC number, nothing sent */
    track(&m, 0, 1);
    clear();
    knob(&m, 1, 5);
    CHECK(m.s.cc[0][1] == 22 && count(RFM_MOVE_ACT_MIDI) == 0 && count(RFM_MOVE_ACT_SETTINGS) == 1 &&
          m.menu_what == MENU_CC);
    now_ms += 400, track(&m, 0, 0);
    clear();
    knob(&m, 1, 1);
    CHECK(has_midi(0xB0, 22, 65));
    /* the FM-1 layout: Track 1 held + a knob is nothing */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    track(&m, 0, 1);
    clear();
    knob(&m, 3, 4);
    CHECK(nact == 0);
    now_ms += 400, track(&m, 0, 0);
    rfm_move_set_mode(&m, RFM_MOVE_MODE_MIDI);
    CHECK(m.s.cc[0][1] == 22 && m.seq.tr[0].cc[0][1] == 22);   /* (kept in the track) */
    /* the channel is the track's; the volume knob is still MASTER; the pads still play notes */
    m.seq.tr[0].ch = 3;
    jog(&m, 1);
    CHECK(!m.seq.trig);
    clear();
    knob(&m, 1, 1);
    knob(&m, 8, 1);
    pad(&m, 0, 50);
    CHECK(has_midi(0xB3, 22, 66) && has(RFM_MOVE_ACT_MASTER, 0, 0, 784) && has_midi(0x93, 48, 50));
    pad(&m, 0, 0);
    /* each slot its own track: Track 2 held + a knob sets the CC number of the track selected for slot 2 (slot 1
     * played, its track 1 untouched); slot 2 played sends its own */
    fl[0].id = "a", fl[0].name = "A", fl[0].version = fl[0].installed = "1";
    fl[1].id = "b", fl[1].name = "B", fl[1].version = fl[1].installed = "1";
    rfm_move_fw_list(&m, fl, 2);
    rfm_move_fw_slots(&m, sl);
    CHECK(rfm_move_set_slot_layout(&m, 1, RFM_MOVE_LAY_DRUM) == 0);
    CHECK(rfm_move_seq_lock(&m, 1) == 1 && m.seq.tr[1].layout == RFM_MOVE_LAYOUT_DRUM && m.seq.sel_track == 0);
    track(&m, 1, 1);
    clear();
    knob(&m, 0, 10);
    now_ms += 400, track(&m, 1, 0);
    CHECK(rfm_move_fw_active(&m) == 0 && count(RFM_MOVE_ACT_MIDI) == 0 && m.seq.sel_track == 0);
    CHECKF(m.seq.tr[1].cc[0][0] == 26 && m.s.cc[0][0] == 16, "track 2's CC %d, track 1's %d", m.seq.tr[1].cc[0][0],
           m.s.cc[0][0]);                               /* (fixed in 0.9.0) */
    tap(&m, 1);
    CHECK(rfm_move_fw_active(&m) == 1 && m.s.mode == RFM_MOVE_MODE_SEQ && m.s.layout == RFM_MOVE_LAYOUT_DRUM &&
          m.seq.sel_track == 1 && m.seq.trig);
    jog(&m, 1);
    clear();
    knob(&m, 0, 1);
    CHECKF(nact >= 1 && acts[0].bytes[0] == 0xB1 && acts[0].bytes[1] == 26, "%d: %02x %d", nact, nact ? acts[0].bytes[0] : 0,
           nact ? acts[0].bytes[1] : 0);               /* (fixed in 0.9.0: track 2's own CC number) */
    tap(&m, 0);
    CHECK(rfm_move_fw_active(&m) == 0 && m.s.layout == RFM_MOVE_LAYOUT_SCALE && m.seq.sel_track == 0);
    jog(&m, 1);
    clear();
    knob(&m, 0, 1);
    CHECKF(nact == 1 && acts[0].bytes[0] == 0xB3 && acts[0].bytes[1] == 16, "%02x %d %d", nact ? acts[0].bytes[0] : 0,
           nact ? acts[0].bytes[1] : 0, nact ? acts[0].bytes[2] : 0);   /* (fixed in 0.9.0: track 1's CC 16 untouched;
                                                         * the knob's value is the sequencer's, shared by the tracks) */
}

/* ---- LEDs */
static void test_leds(void)
{
    rfm_move_t m;
    rfm_move_led_t out[RFM_MOVE_NLEDS + 4];
    uint8_t lv[RFM_MOVE_NLEVELS];
    int n, i, j, dup = 0;
    rfm_move_init(&m, NULL, on_act, NULL);
    memset(lv, 0, sizeof lv);
    rfm_move_panel(&m, lv, 0);
    n = rfm_move_leds(&m, out, RFM_MOVE_NLEDS + 4);
    CHECK(n == RFM_MOVE_NLEDS);
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            dup += out[i].cc == out[j].cc && out[i].number == out[j].number;
    CHECK(dup == 0);
    for (i = 0; i < n; i++)                             /* Track 1 in the FM-1 mode's colour */
        if (out[i].cc && out[i].number == MR_CC_TRACK1)
            CHECK(out[i].rgb && out[i].r == 0xc4 && out[i].g == 0x3a && out[i].b == 0xff);
    CHECK(rfm_move_leds(&m, out, RFM_MOVE_NLEDS) == 0);
    /* key 0's LED (F3: the keymap's id 14 at row 4 column 3 -> levels[3 * 4 + 3]) lights pad-2-1 and step 1 */
    lv[3 * 4 + 3] = 255;
    rfm_move_panel(&m, lv, 0);
    n = rfm_move_leds(&m, out, RFM_MOVE_NLEDS);
    CHECK(n == 2);
    CHECK(n == 2 && !out[0].cc && out[0].number == MR_NOTE_PAD0 + pad_of(2, 1) && out[0].r == 255 && out[0].g == 255);
    CHECK(n == 2 && !out[1].cc && out[1].number == MR_NOTE_STEP0 + 0 && out[1].r == 255);
    rfm_move_panel(&m, lv, 0);                          /* the same again: nothing */
    CHECK(rfm_move_leds(&m, out, RFM_MOVE_NLEDS) == 0);
    /* a black key lights pink: key 1 (F#3, id 15 at row 4 column 2) */
    lv[2 * 4 + 3] = 255;
    rfm_move_panel(&m, lv, 0);
    n = rfm_move_leds(&m, out, RFM_MOVE_NLEDS);
    CHECK(n == 1 && out[0].number == MR_NOTE_PAD0 + pad_of(1, 2) && out[0].r == 0xff && out[0].g == 0x7a && out[0].b == 0xa8);
    /* PLAY's green (and PLAY id 12 at row 2 column 10) */
    rfm_move_panel(&m, lv, 255);
    n = rfm_move_leds(&m, out, RFM_MOVE_NLEDS);
    CHECK(n == 1 && out[0].cc && out[0].number == MR_CC_PLAY && out[0].r == 0 && out[0].g == 255 && out[0].b == 0);
    /* SEQ's LED (id 11 at row 1 column 1): (0.4.4) on Up, in FM-1 mode */
    lv[1 * 4 + 0] = 255;
    rfm_move_panel(&m, lv, 255);
    n = rfm_move_leds(&m, out, RFM_MOVE_NLEDS);
    CHECK(n == 1 && out[0].cc && out[0].number == MR_CC_UP && out[0].r == 127);
    /* a mode change repaints what changes, once; at most max a call, the rest next time (0.8.0: Track 1's layout
     * menu, Scale) */
    lay(&m, 0, RFM_MOVE_LAY_SCALE);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ);               /* (0.9.0: the note layout is the sequencer) */
    n = rfm_move_leds(&m, out, 5);
    CHECK(n == 5);
    n = rfm_move_leds(&m, out, RFM_MOVE_NLEDS);
    CHECK(n > 0);
    CHECK(rfm_move_leds(&m, out, RFM_MOVE_NLEDS) == 0);
    /* a pressed pad lights white (0.9.0, the sequencer's pads: the other pad of its note (pad 8, F3) and Capture's
     * LED (a note to capture) change too; nothing else) */
    pad(&m, 3, 100);
    n = rfm_move_leds(&m, out, RFM_MOVE_NLEDS);
    for (i = 0, j = 0; i < n; i++) {
        if (!out[i].cc && out[i].number == MR_NOTE_PAD0 + 3) {
            j += 100;
            CHECK(out[i].r == 255 && out[i].g == 255 && out[i].b == 255);
        } else {
            j++;
            CHECKF((!out[i].cc && out[i].number == MR_NOTE_PAD0 + 8) || (out[i].cc && out[i].number == MR_CC_CAPTURE),
                        "LED %s %d changed", out[i].cc ? "cc" : "note", out[i].number);
        }
    }
    CHECK(j >= 100 && j <= 102);
    pad(&m, 3, 0);
    rfm_move_leds_invalidate(&m);
    CHECK(rfm_move_leds(&m, out, RFM_MOVE_NLEDS + 4) == RFM_MOVE_NLEDS);
}

/* ---- the screen */
static uint32_t fnv(const uint8_t *p, int n)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static void write_pbm(const char *path, const uint8_t *packed)
{
    FILE *f = fopen(path, "w");
    int x, y;
    if (!f)
        return;
    fprintf(f, "P1\n128 64\n");
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++)
            fputc(packed[(y >> 3) * 128 + x] >> (y & 7) & 1 ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
}

/* golden frames (FNV-1a of the 1024 packed bytes); a change in the drawing is a change here, made on purpose:
 * look at the PBMs this test writes into the build directory first (move_*.pbm) */
#define GOLD_FM1 0xca84fa11u
#define GOLD_FM1_LIVE 0x714e834du
#define GOLD_KNOB 0xa5cf1bbau
#define GOLD_MASTER 0x7ac52bc1u
#define GOLD_MIDI 0x6bbd6df5u                           /* (0.9.0: the sequencer's track view) */
#define GOLD_MIDI_HIT GOLD_MIDI                         /* (0.9.0: a live note draws nothing) */
#define GOLD_CC 0xb28a5e1cu                             /* (0.9.0: the sequencer's CC popup, its page strip) */
#define GOLD_MENU_MIDI 0x91e1fc98u
#define GOLD_MENU_CC 0x4ce8d36du                        /* (0.9.0: the sequencer's "Knob 3 CC", "20") */

static int lit_in(const uint8_t *out, int x0, int y0, int x1, int y1)   /* the pixels on in [x0, x1) x [y0, y1) */
{
    int x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += out[(y >> 3) * 128 + x] >> (y & 7) & 1;
    return n;
}
static int px_at(const uint8_t *packed, int x, int y) { return packed[(y >> 3) * 128 + x] >> (y & 7) & 1; }
static uint32_t gold(const char *name, const uint8_t *out, uint32_t want)
{
    char path[64];
    const uint32_t h = fnv(out, MR_SCREEN_BYTES);
    snprintf(path, sizeof path, "move_%s.pbm", name);
    write_pbm(path, out);
    printf("screen %s: %08x\n", name, h);
    CHECKF(want == 0 || h == want, "%s %08x, golden %08x", name, h, want);
    return h;
}
static void ms(rfm_move_t *m, uint32_t n) { rfm_move_seq_advance(m, n * 1000u, 1000000u); }   /* the popups' clock */

/* the owner's FM-1 drawing, read from the PNG the owner drew (64 x 64, white = on) independently of screen.c's table:
 * rows 15..51; the rest is black */
static const char *const ART[37] = {
    "....########################################################....",   /* 15 */
    "...#........................................................#...",   /* 16 */
    "..#..........................................................#..",   /* 17 */
    "..#...............##############.............................#..",   /* 18 */
    "..#...............##############.............................#..",   /* 19 */
    "..#...##....##....##..........##...##.....##....##.....##....#..",   /* 20 */
    "..#...##....##....##..........##...##.....##....##.....##....#..",   /* 21 */
    "..#...............##...###....##.............................#..",   /* 22 */
    "..#...............##..#####...##.............................#..",   /* 23 */
    "..#...............##.##..##...##.............................#..",   /* 24 */
    "..#...............##.##...##..##..#######################....#..",   /* 25 */
    "..#...##....##....####....##..##.#...#...#...#...#...#...#...#..",   /* 26 */
    "..#...##....##....###......#####.#...#...#...#...#...#...#...#..",   /* 27 */
    "..#...............##........####.#...#...#...#...#...#...#...#..",   /* 28 */
    "..#...............##..........##.#########################...#..",   /* 29 */
    "..#...............##############.#...#...#...#...#...#...#...#..",   /* 30 */
    "..#...#########...##############.#...#...#...#...#...#...#...#..",   /* 31 */
    "..#...#...#...#..................#...#...#...#...#...#...#...#..",   /* 32 */
    "..#...#########...................#######################....#..",   /* 33 */
    "..#..........................................................#..",   /* 34 */
    "..#..........................................................#..",   /* 35 */
    "..#....#################################################.....#..",   /* 36 */
    "..#...#.................................................#....#..",   /* 37 */
    "..#...#...##.##.##.##.##.##.##.##.##.##.##.##.##.##.##..#....#..",   /* 38 */
    "..#...#...##.##.##.##.##.##.##.##.##.##.##.##.##.##.##..#....#..",   /* 39 */
    "..#...#...##.##.##.##.##.##.##.##.##.##.##.##.##.##.##..#....#..",   /* 40 */
    "..#...#...##.##.##.##.##.##.##.##.##.##.##.##.##.##.##..#....#..",   /* 41 */
    "..#...#.................................................#....#..",   /* 42 */
    "..#...#.##.##.##.##.##.##.##.##.##.##.##.##.##.##.##.##.#....#..",   /* 43 */
    "..#...#.##.##.##.##.##.##.##.##.##.##.##.##.##.##.##.##.#....#..",   /* 44 */
    "..#...#.##.##.##.##.##.##.##.##.##.##.##.##.##.##.##.##.#....#..",   /* 45 */
    "..#...#.##.##.##.##.##.##.##.##.##.##.##.##.##.##.##.##.#....#..",   /* 46 */
    "..#...#.................................................#....#..",   /* 47 */
    "..#....#################################################.....#..",   /* 48 */
    "..#..........................................................#..",   /* 49 */
    "...#........................................................#...",   /* 50 */
    "....########################################################....",   /* 51 */
};
static int art_px(int x, int y) { return y >= 15 && y < 52 && ART[y - 15][x] == '#'; }
/* the left half against the drawing: the number of pixels that differ */
static int art_diff(const uint8_t *out)
{
    int x, y, d = 0;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            d += px_at(out, x, y) != art_px(x, y);
    return d;
}

static void test_screen(void)
{
    static uint16_t lcd[240 * 240];
    static rfm_move_t m;
    uint8_t out[MR_SCREEN_BYTES], out2[MR_SCREEN_BYTES], lv[RFM_MOVE_NLEVELS];
    int x, y, d;
    /* a test card: a horizontal grey ramp, a white box, a black disc */
    for (y = 0; y < 240; y++)
        for (x = 0; x < 240; x++) {
            const int g = x * 31 / 239, dx = x - 160, dy = y - 160;
            uint16_t v = (uint16_t)(g << 11 | (g * 2) << 5 | g);
            if (x >= 20 && x < 100 && y >= 20 && y < 100)
                v = 0xFFFF;
            if (dx * dx + dy * dy < 40 * 40)
                v = 0;
            lcd[y * 240 + x] = v;
        }
    rfm_move_init(&m, NULL, on_act, NULL);
    m.s.pop_half = 1;                                   /* (0.5.9: the left half here; the whole screen: test_popup_width) */
    rfm_move_set_label(&m, RFM_MOVE_E_K1, "CUTOFF");
    /* at rest: the left half is the owner's drawing, pixel for pixel; the right half the FM-1's screen */
    rfm_move_screen(&m, lcd, out);
    CHECKF(art_diff(out) == 0, "FM-1 mode at rest: the drawing exactly (%d pixels differ)", art_diff(out));
    CHECK(out[(1 * 128) + 64 + 15] == 0xFF);            /* x 79, y 8..15: inside the box */
    CHECK(out[(5 * 128) + 64 + 43] == 0x00);            /* x 107, y 40..47: inside the disc */
    for (d = 0, y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            d += px_at(out, 64 + x, y) != m.thumb[y][x];
    CHECK(d == 0);
    gold("fm1", out, GOLD_FM1);
    rfm_move_screen(&m, NULL, out2);                    /* NULL: the last screen kept */
    CHECK(!memcmp(out, out2, sizeof out));
    /* LEDs dim (under RFM_MOVE_SCREEN_LIT) change nothing */
    memset(lv, RFM_MOVE_SCREEN_LIT - 1, sizeof lv);
    rfm_move_panel(&m, lv, 0);
    rfm_move_screen(&m, NULL, out);
    CHECK(art_diff(out) == 0);

    /* (0.5.6, the owner's) a touch alone: the knob's name in the left half, no value yet (K1: knob 5); the volume
     * knob: MASTER and its level; the release: gone */
    touch(&m, 4, 1);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_ENC && m.pop_blank && lit_in(out, 0, 8, 64, 24) > 10 && !lit_in(out, 0, 26, 64, 64));
    gold("knob_touch", out, 0);
    touch(&m, 4, 0);
    CHECK(m.pop == POP_NONE);
    touch(&m, 8, 1);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_MASTER && lit_in(out, 0, 28, 64, 56) > 20);
    touch(&m, 8, 0);
    touch(&m, 0, 1);                                    /* knob 1: PRESETS */
    CHECK(m.pop == POP_ENC && m.pop_i == 0);
    touch(&m, 0, 0);
    rfm_move_screen(&m, NULL, out);
    CHECK(art_diff(out) == 0);
    jog(&m, 1), jog(&m, 2);                             /* the jog wheel: SELECT's popup on the left half while turning
                                                         * (its detents), the mirror stays */
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_SELECT && m.pop_val == 3 && !px_at(out, 0, 0) && !px_at(out, 63, 63) && lit_in(out, 0, 28, 64, 56) > 40 && px_at(out, 64 + 15, 8));
    ms(&m, RFM_MOVE_POPUP_MS);
    rfm_move_screen(&m, NULL, out);
    CHECK(art_diff(out) == 0);                          /* timed out: the drawing at rest */
    jog(&m, -1);                                        /* (a new gesture: from 0) */
    CHECK(m.pop_val == -1);
    ms(&m, RFM_MOVE_POPUP_MS);
    /* buttons: lit (the FM-1's LED from RFM_MOVE_SCREEN_LIT) or held, their cells filled; FX lit, REC held (Rec),
     * OCT+ held (Right) */
    memset(lv, 0, sizeof lv);
    lv[m.led_of[RFM_MOVE_B_FX]] = RFM_MOVE_SCREEN_LIT;
    rfm_move_panel(&m, lv, 0);
    button(&m, MR_CC_REC, 1);
    button(&m, MR_CC_RIGHT, 1);
    /* keys: F3 (key 0, the first natural) and F#3 (key 1, the first gap's block) and G5 (key 26, the last) held */
    pad(&m, 16, 100), pad(&m, 25, 100), pad(&m, 7, 100);
    rfm_move_screen(&m, NULL, out);
    for (y = 26; y < 29; y++)
        for (x = 34; x < 37; x++)
            CHECK(px_at(out, x, y));                    /* FX: the grid's first cell */
    CHECK(px_at(out, 55, 31) && px_at(out, 54, 30) && px_at(out, 56, 32));   /* REC: the last */
    CHECK(px_at(out, 11, 32) && px_at(out, 13, 32) && !px_at(out, 8, 32));   /* OCT+ yes, OCT- no */
    CHECK(!px_at(out, 8, 43) && !px_at(out, 9, 46) && !px_at(out, 10, 38) && !px_at(out, 11, 41));
    CHECK(!px_at(out, 53, 43) && !px_at(out, 54, 46) && px_at(out, 50, 43));
    CHECK(art_diff(out) == 9 + 9 + 3 + 3 * 8);
    gold("fm1_live", out, GOLD_FM1_LIVE);
    pad(&m, 16, 0), pad(&m, 25, 0), pad(&m, 7, 0);
    button(&m, MR_CC_REC, 0), button(&m, MR_CC_RIGHT, 0);
    memset(lv, 0, sizeof lv);
    rfm_move_panel(&m, lv, 0);
    rfm_move_screen(&m, NULL, out);
    CHECK(art_diff(out) == 0);

    /* the knob popup: a turn (the firmware's label, this gesture's detents) */
    touch(&m, 4, 1);
    knob(&m, 4, 4), knob(&m, 4, 2);                     /* (6 ticks: 3 detents) */
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_ENC && m.pop_val == 3);
    CHECK(!px_at(out, 0, 0) && !px_at(out, 63, 63) && lit_in(out, 0, 8, 64, 24) > 20 && lit_in(out, 0, 28, 64, 56) > 40 &&
          !lit_in(out, 0, 57, 64, 64));       /* the left half, no frame, no bar: the name and the value */
    for (d = 0, y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            d += px_at(out, 64 + x, y) != m.thumb[y][x];
    CHECK(d == 0);                                      /* (the FM-1's screen stays in view) */
    gold("knob", out, GOLD_KNOB);
    ms(&m, RFM_MOVE_POPUP_MS - 10);                     /* (the owner's: up while turning, a moment after the last
                                                         * detent; a touch alone does not hold it) */
    rfm_move_screen(&m, NULL, out2);
    CHECK(rfmv_pop_up(&m, POP_ENC) && rfm_move_screen_timed(&m));   /* ("Cutoff" scrolls meanwhile) */
    ms(&m, 20);
    rfm_move_screen(&m, NULL, out2);
    CHECK(rfmv_pop_up(&m, POP_ENC));                    /* (0.5.6: still touched: still up) */
    knob(&m, 4, 2);                                     /* (turning again in the same touch: the count goes on) */
    CHECK(m.pop_val == 4);
    touch(&m, 4, 0);                                    /* the release: gone at once, the count back to 0 */
    rfm_move_screen(&m, NULL, out);
    CHECK(art_diff(out) == 0 && !rfm_move_screen_timed(&m) && m.pop_val == 0 && m.pop == POP_NONE);
    touch(&m, 4, 1);                                    /* a later touch: its name, no value */
    rfm_move_screen(&m, NULL, out);
    CHECK(rfmv_pop_up(&m, POP_ENC) && m.pop_blank && m.pop_val == 0);
    knob(&m, 4, -2);                                    /* a new gesture: the count starts again */
    CHECK(m.pop_val == -1 && rfmv_pop_up(&m, POP_ENC) && !m.pop_blank);
    touch(&m, 4, 0);
    touch(&m, 8, 1), knob(&m, 8, 1), knob(&m, 8, -1);   /* MASTER the same */
    CHECK(rfmv_pop_up(&m, POP_MASTER));
    touch(&m, 8, 0);
    CHECK(!rfmv_pop_up(&m, POP_MASTER) && m.pop == POP_NONE);
    ms(&m, RFM_MOVE_POPUP_MS);
    knob(&m, 8, -16);                                   /* the volume knob: MASTER (768 - 256 = 512: 50 %) */
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_MASTER);
    for (d = 0, y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            d += px_at(out, 64 + x, y) != m.thumb[y][x];
    CHECK(d == 0);
    gold("master", out, GOLD_MASTER);
    ms(&m, RFM_MOVE_POPUP_MS);

    /* (0.9.0) a note layout: the sequencer's track view (the edited clip: 16 step cells, the beat lines), no pad map
     * any more. (0.8.0) Track 1's layout menu: Scale; its tap names the layout */
    lay(&m, 0, RFM_MOVE_LAY_SCALE);
    tap(&m, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.pop == POP_LAYOUT && m.pop_i == 0);   /* ("FM-1, Scale" named) */
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_pop_layout.pbm", out);
    CHECK(lit_in(out, 0, 0, 128, 16) > 20 && lit_in(out, 0, 20, 128, 48) > 60 && px_at(out, 24 + 2 * 14 + 5, 60) &&
          !px_at(out, 24 + 5, 60) && px_at(out, 24 + 5, 61));   /* ("FM-1", "Scale", 6 dashes: the 3rd thick) */
    ms(&m, RFM_MOVE_POPUP_MS);
    rfm_move_screen(&m, NULL, out);
    CHECK(lit_in(out, 0, 46, 128, 58) > 200 && lit_in(out, 0, 0, 128, 44) < 200);   /* (the 16 step cells below) */
    gold("midi", out, GOLD_MIDI);
    pad(&m, 1, 90);                                     /* a live note (stopped, not recording): the clip unchanged */
    rfm_move_screen(&m, NULL, out2);
    CHECK(!memcmp(out, out2, sizeof out) && m.seq.nnotes == 0);
    gold("midi_hit", out2, GOLD_MIDI_HIT);
    pad(&m, 1, 0);
    touch(&m, 4, 1);                                    /* a touch: the TRIG page's knob (0.9.0: no slot CC) */
    CHECK(m.pop == POP_TRIG && m.pop_i == 4);
    touch(&m, 4, 0);
    ms(&m, RFM_MOVE_POPUP_MS);
    /* Track 1 held: its menu; then the scale changed (0.9.0: the key the track uses): the keys screen, its name at 4x */
    track(&m, 0, 1);
    pad(&m, 17, 100), pad(&m, 17, 0);                   /* row 2, pad 2: minor */
    rfm_move_screen(&m, NULL, out);
    CHECK(m.menu_what == MENU_SCALE && m.s.scale == RFM_MOVE_SCALE_MINOR && m.seq.scale == RFM_MOVE_SCALE_MINOR);
    CHECK(px_at(out, 1, 0) && !px_at(out, 33, 0) && px_at(out, 3, 58));       /* (the layout tab thick, a cell filled) */
    gold("menu_midi", out, GOLD_MENU_MIDI);
    pad(&m, 16, 100), pad(&m, 16, 0);
    pad(&m, 24 + RFM_MOVE_LAY_DRUM, 100), pad(&m, 24 + RFM_MOVE_LAY_DRUM, 0);   /* the drum layout: a pad held + the jog
                                                         * wheel, its note */
    track(&m, 0, 0);
    pad(&m, 7, 100);
    jog(&m, -2);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_DRUM && m.pop_i == 7 && m.s.drum[7] == 34 && lit_in(out, 0, 3, 128, 19) > 20 && px_at(out, 4, 54));
    write_pbm("move_pop_drum.pbm", out);
    pad(&m, 7, 0);
    ms(&m, RFM_MOVE_POPUP_MS - 1);
    CHECK(rfmv_pop_up(&m, POP_DRUM));
    ms(&m, 1);
    CHECK(!rfmv_pop_up(&m, POP_DRUM));
    lay(&m, 0, RFM_MOVE_LAY_SCALE);

    /* (0.9.0) the note layout's knobs: the sequencer's; the jog to bank 1: the track's CCs (the slot's are gone) */
    jog(&m, 1);
    CHECK(!m.seq.trig && m.s.bank == 0);
    knob(&m, 1, 36);                                    /* knob 2: 100 */
    ms(&m, RFM_MOVE_POPUP_MS);
    knob(&m, 0, 1);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_CC && m.pop_i == 0 && m.cc_val[0][1] == 100 && m.cc_val[0][0] == 65);
    gold("cc", out, GOLD_CC);
    ms(&m, RFM_MOVE_POPUP_MS);
    rfm_move_screen(&m, NULL, out2);
    CHECK(px_at(out, 4, 54) && !rfmv_pop_up(&m, POP_CC));   /* dropped (the bar gone) */
    track(&m, 0, 1);                                    /* Track 1 held + knob 3: the track's CC number */
    knob(&m, 2, 2);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.menu_what == MENU_CC && m.menu_i == 2 && m.s.cc[0][2] == 20 && lit_in(out, 0, 20, 128, 48) > 60);   /* ("Knob 3",
                                                         * "CC 20" at 4x) */
    gold("menu_cc", out, GOLD_MENU_CC);
    track(&m, 0, 0);
    CHECK(GOLD_FM1 && GOLD_FM1_LIVE && GOLD_KNOB && GOLD_MASTER && GOLD_MIDI && GOLD_MIDI_HIT && GOLD_CC &&
          GOLD_MENU_MIDI && GOLD_MENU_CC);
}

/* the thumbnail is a luminance threshold, no dither: flat colours come out solid, 1 px strokes survive */
static void test_threshold(void)
{
    static uint16_t lcd[240 * 240];
    rfm_move_t m;
    uint8_t out[MR_SCREEN_BYTES];
    int x, y, solid_white = 1, solid_black = 1, line = 0;
    for (y = 0; y < 240; y++)
        for (x = 0; x < 240; x++)
            lcd[y * 240 + x] = x < 120 ? 0x4208 /* dark grey, luma ~66: black */ : 0xF800 /* red, luma ~76+: see below */;
    for (y = 0; y < 240; y++)
        for (x = 0; x < 120; x++)
            if (y < 120)
                lcd[y * 240 + x] = 0x8410;              /* mid grey, luma ~132: white */
    for (y = 120; y < 240; y++)
        lcd[y * 240 + 60] = 0xFFFF;                     /* a 1 px white line in the dark grey */
    rfm_move_init(&m, NULL, on_act, NULL);
    rfm_move_screen(&m, lcd, out);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            const int v = m.thumb[y][x];
            if (y < 31 && x < 31 && !v) solid_white = 0;      /* mid grey quarter */
            if (y >= 33 && x < 31 && x != 16 && v) solid_black = 0;  /* dark grey quarter but the line's column */
            if (y >= 33 && x == 16 && v) line++;
        }
    CHECK(solid_white);
    CHECK(solid_black);
    CHECKF(line >= 30, "the 1 px line survives (%d of 31 rows)", line);
    /* pure red (luma 76) is under the threshold (80): black */
    CHECK(m.thumb[10][50] == (((77 * 255) >> 8) > RFM_MOVE_THUMB_LUMA));
}

/* the owner's MIDI mode round: Shift + step 9 (Keys & Scales), the scale pads coloured, a Track 2 tap turning MIDI
 * mode's screens (NOTES: the notes and the chord), the mode named on a tap (not a hold), the settings' version 2 */
static void test_midi_round(void)
{
    static rfm_move_t m;
    uint8_t out[MR_SCREEN_BYTES], out2[MR_SCREEN_BYTES], img[RFM_MOVE_SETTINGS_BYTES];
    rfm_move_settings_t st;
    rfm_move_led_t leds[RFM_MOVE_NLEDS];
    char c[16];
    int i, n, x, y, d;
    rfm_move_init(&m, NULL, on_act, NULL);
    /* the chords */
    {
        static const struct { uint8_t n[5]; int k; const char *name; } T[] = {
            { { 51, 55, 58 }, 3, "Eb" }, { { 58, 61, 65, 68 }, 4, "Bbm7" }, { { 48, 52, 55, 59 }, 4, "Cmaj7" }, { { 57, 60, 64 }, 3, "Am" }, { { 50, 55, 57 }, 3, "Dsus4" },
            { { 55, 59, 62, 65 }, 4, "G7" }, { { 52, 55, 60 }, 3, "C/E" }, { { 48, 51, 55 }, 3, "Cm" },
            { { 48, 55 }, 2, "C5" }, { { 48, 49 }, 2, "" }, { { 60 }, 1, "" }, { { 47, 50, 53, 57 }, 4, "Bm7b5" },
        };
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); i++)
            CHECKF(!strcmp(rfmv_chord_name(T[i].n, T[i].k, c, sizeof c), T[i].name), "%s: %s", T[i].name, c);
        CHECK(!strcmp(rfmv_note_name(60, c, sizeof c), "C3") && !strcmp(rfmv_note_name(63, c, sizeof c), "Eb3") &&
              !strcmp(rfmv_note_name(70, c, sizeof c), "Bb3") && !strcmp(rfmv_note_name(36, c, sizeof c), "C1") &&
              !strcmp(rfmv_note_name(66, c, sizeof c), "Gb3"));   /* (Ableton's names: MIDI 60 = C3, flats) */
    }
    /* (0.8.0) Track 1's tap names the slot's layout over the whole screen; a hold (its menu) names nothing (0.9.0: a
     * note layout is the sequencer, s.mode SEQ) */
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_SCALE);
    ms(&m, RFM_MOVE_POPUP_MS);
    tap(&m, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.pop == POP_LAYOUT && m.pop_i == 0);
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_mode_midi.pbm", out);
    for (d = 0, y = 0; y < 64; y++)
        for (x = 0; x < 128; x++)
            d += px_at(out, x, y) && ((y > 14 && y < 20) || (y > 48 && y < 60));
    CHECK(!px_at(out, 0, 0) && d == 0 && lit_in(out, 0, 0, 128, 14) > 20 && lit_in(out, 0, 20, 128, 48) > 60 &&
          px_at(out, 24 + 2 * 14 + 5, 60) && !px_at(out, 24 + 5, 60));   /* (the whole screen, no frame: "FM-1",
                                                         * "Scale" at 4x, centred; the strip: layout 3 of 6) */
    ms(&m, RFM_MOVE_POPUP_MS);
    track(&m, 0, 1), now_ms += 500, track(&m, 0, 0);    /* Track 1 held: its menu, no tap */
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && !rfmv_pop_up(&m, POP_LAYOUT));
    tap(&m, 2);                                         /* (no firmware list: one slot, Tracks 2 and 3 nothing) */
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && !rfmv_pop_up(&m, POP_LAYOUT) && rfm_move_fw_active(&m) == 0);
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_FM1);
    ms(&m, RFM_MOVE_POPUP_MS);
    m.thumb[10][10] = 1;                                /* (a lit pixel on the FM-1's screen: covered by the popup) */
    tap(&m, 0);                                         /* the FM-1 layout: "FM-1" over the whole screen too */
    CHECK(m.s.mode == RFM_MOVE_MODE_FM1 && m.pop == POP_LAYOUT && m.pop_i == 0);
    rfm_move_screen(&m, NULL, out);
    CHECK(!px_at(out, 64 + 10, 10) && !px_at(out, 0, 0));
    ms(&m, RFM_MOVE_POPUP_MS);
    rfm_move_screen(&m, NULL, out);
    CHECK(px_at(out, 64 + 10, 10));                     /* (then the FM-1's screen again) */
    m.thumb[10][10] = 0;
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_SCALE);
    ms(&m, RFM_MOVE_POPUP_MS);
    /* (0.8.0) a note layout's Shift + a step: the Move's shortcuts on the sequencer (step 9: Keys), no FM-1 GLO; Back
     * closes it (0.9.0: the sequencer's own Shift) */
    clear();
    button(&m, MR_CC_SHIFT, 1), step(&m, 8, 1), step(&m, 8, 0), button(&m, MR_CC_SHIFT, 0);
    CHECK(m.seq.smenu == RFM_MOVE_SM_KEYS && count(RFM_MOVE_ACT_BTN) == 0 && m.s.mode == RFM_MOVE_MODE_SEQ);
    back(&m);
    CHECK(!m.seq.smenu && m.s.mode == RFM_MOVE_MODE_SEQ);
    /* the scale: Track 1 held, row 2's pads (cyan); its hold screen only once held 300 ms, or used (0.9.0: the key the
     * track uses; track 1 is GLOBAL: the project's key too) */
    clear();
    rfm_move_screen(&m, NULL, out2);                    /* (the pad map) */
    track(&m, 0, 1);
    rfm_move_screen(&m, NULL, out);
    CHECK(!memcmp(out, out2, sizeof out) && !rfmv_hold_shown(&m, 0) && rfm_move_screen_timed(&m));   /* (not yet: the
                                                         * pad map; the host draws again) */
    ms(&m, RFM_MOVE_TAP_MS);
    rfm_move_screen(&m, NULL, out);
    CHECK(rfmv_hold_shown(&m, 0) && memcmp(out, out2, sizeof out) && lit_in(out, 0, 0, 128, 14) > 20);   /* (held: the
                                                         * menu, its slot named on top) */
    pad(&m, 17, 100), pad(&m, 17, 0);                   /* minor */
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_midi_keys.pbm", out);
    CHECK(lit_in(out, 0, 7, 128, 35) > 200);           /* ("C MIN" at 4x) */
    jog(&m, 2);                                         /* D */
    CHECK(m.s.scale == RFM_MOVE_SCALE_MINOR && m.s.root == 2 && m.seq.scale == RFM_MOVE_SCALE_MINOR && m.seq.root == 2 &&
          count(RFM_MOVE_ACT_MIDI) == 0);
    rfm_move_leds_invalidate(&m);
    n = rfm_move_leds(&m, leds, RFM_MOVE_NLEDS);
    for (i = 0; i < n; i++)
        if (!leds[i].cc && leds[i].number == MR_NOTE_PAD0 + 17)
            CHECK(leds[i].r == 255 && leds[i].g == 255 && leds[i].b == 255);   /* (0.8.0, the board's: the scale in use
                                                         * white) */
        else if (!leds[i].cc && leds[i].number == MR_NOTE_PAD0 + 16)
            CHECK(leds[i].r < 20 && leds[i].b > 20 && leds[i].b < 100);   /* (the others cyan, dim) */
    clear();
    jog(&m, -3);                                        /* (the jog wraps the root: B) */
    CHECK(m.s.root == 11 && m.seq.root == 11 && m.menu_what == MENU_ROOT && count(RFM_MOVE_ACT_TURN) == 0);
    jog(&m, 3);
    button(&m, MR_CC_UP, 1), button(&m, MR_CC_UP, 0);   /* (0.8.0) Up / Down: the pads' octave */
    CHECK(m.s.octave == 1 && m.menu_what == MENU_OCT);
    button(&m, MR_CC_DOWN, 1), button(&m, MR_CC_DOWN, 0);
    CHECK(m.s.octave == 0 && m.s.scale == RFM_MOVE_SCALE_MINOR && m.s.root == 2);
    /* 24 scales: rows 2, 3, 4 of the pads (the first 8 as before); knob 2 walks all of them; short names */
    pad(&m, 8, 100), pad(&m, 8, 0);                     /* row 3, pad 1: scale 9, phrygian */
    CHECK(m.s.scale == RFM_MOVE_SCALE_PHRYGIAN);
    pad(&m, 7, 100), pad(&m, 7, 0);                     /* row 4, pad 8: scale 24, bebop */
    CHECK(m.s.scale == RFM_MOVE_SCALE_BEBOP && RFM_MOVE_NSCALES == 24 && RFM_MOVE_SCALE_BEBOP == RFM_MOVE_NSCALES - 1);
    {   /* (0.8.0: the knobs are CC numbers now; pads 1..24 are the 24 scales, each once) */
        int seen = 0;
        for (i = 0; i < 24; i++)
            seen |= 1 << rfmv_scale_of_pad(i);
        CHECK(seen == (1 << 24) - 1);
    }
    for (i = 0; i < RFM_MOVE_NSCALES; i++) {
        int k, prev = -1, ok = rfmv_scales[i][0] >= 5 && rfmv_scales[i][0] <= 8 && rfmv_scales[i][1] == 0;
        for (k = 1; k <= rfmv_scales[i][0]; k++)
            ok &= rfmv_scales[i][k] > prev && rfmv_scales[i][k] < 12, prev = rfmv_scales[i][k];
        CHECKF(ok && strlen(rfm_move_scale_name(i)) <= 6, "scale %d %s", i, rfm_move_scale_name(i));
    }
    {   /* bebop's 8 notes on the in-scale layout; the settings keep scale 24 */
        rfm_move_settings_t st2 = m.s;
        uint8_t im[RFM_MOVE_SETTINGS_BYTES];
        st2.layout = RFM_MOVE_LAYOUT_SCALE, st2.root = 0, st2.octave = 0;
        CHECK(rfm_move_pad_note(&st2, 7) == 48 + 11 && rfm_move_pad_note(&st2, 8) == 48 + 5);   /* (8th degree B; +3 a row: F) */
        rfm_move_save(&st2, im);
        CHECK(rfm_move_load(&st2, im, sizeof im) == 0 && st2.scale == RFM_MOVE_SCALE_BEBOP);
    }
    pad(&m, 17, 100), pad(&m, 17, 0);                   /* (minor again) */
    track(&m, 0, 0);                                    /* (used: no tap on its release) */
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.s.midi_view == RFM_MOVE_MV_PADS && m.pop != POP_LAYOUT);
    rfm_move_set_scale(&m, RFM_MOVE_SCALE_MAJOR, 0);
    ms(&m, RFM_MOVE_POPUP_MS);
    track(&m, 0, 1), track(&m, 0, 0);                   /* a quick click on Track 1: its hold screen never drawn */
    CHECK(m.pop == POP_LAYOUT && m.menu_what == MENU_NONE && !rfmv_hold_shown(&m, 0));
    gap();
    ms(&m, RFM_MOVE_POPUP_MS);
    /* (0.9.0) the NOTES screen (notes held, their chord) was MIDI mode's: a note layout is the sequencer now, its
     * screen the track view; midi_view is only kept as saved */
    m.s.midi_view = RFM_MOVE_MV_NOTES;
    /* the settings keep the screen; a version 1 image (77 bytes) still loads */
    rfm_move_save(&m.s, img);
    CHECK(rfm_move_load(&st, img, sizeof img) == 0 && st.midi_view == RFM_MOVE_MV_NOTES);
    img[3] = 2, img[4] = (uint8_t)(RFM_MOVE_SETTINGS_BYTES_V3 - 2);   /* (version 2: no velocity setting) */
    CHECK(rfm_move_load(&st, img, RFM_MOVE_SETTINGS_BYTES_V3 - 2) == 0 && st.midi_view == RFM_MOVE_MV_NOTES &&
          st.vel_mode == RFM_MOVE_VEL_DYNAMIC && st.vel_level == 127);
    img[3] = 1, img[4] = (uint8_t)(RFM_MOVE_SETTINGS_BYTES_V3 - 3);
    CHECK(rfm_move_load(&st, img, RFM_MOVE_SETTINGS_BYTES_V3 - 3) == 0 && st.midi_view == RFM_MOVE_MV_PADS &&
          st.mode == RFM_MOVE_MODE_SEQ);                /* (0.9.0: the mode it was saved in, the sequencer) */
    m.s.midi_view = RFM_MOVE_MV_PADS;
}

/* (0.8.0) Track 4 held is the Move's Shift layer, in any mode (in the FM-1 layout Shift itself is the FM-1's GLO):
 * Track 4 + step and + Record work on the sequencer, their menus stay open until Back; the pads are Track 4's menu
 * (pads 1, 2 the Mixer, Routing), the rest nothing */
static void test_fm1_shift_layer(void)
{
    static rfm_move_t m;
    uint8_t out[MR_SCREEN_BYTES];
    rfm_move_led_t leds[RFM_MOVE_NLEDS];
    int i, n;
    rfm_move_init(&m, NULL, on_act, NULL);
    ms(&m, RFM_MOVE_POPUP_MS);
    clear();
    button(&m, MR_CC_SHIFT, 1), button(&m, MR_CC_SHIFT, 0);   /* Shift: the FM-1's */
    CHECK(has(RFM_MOVE_ACT_BTN, RFM_MOVE_B_GLO, 1, 0));
    track(&m, 3, 1);
    CHECK(!rfmv_hold_shown(&m, 3));                     /* (its screen: not before 300 ms, or a use) */
    clear();
    pad(&m, 16, 100);                                   /* (a key pad while Track 4 is held: its menu's, nothing; a
                                                         * use) */
    CHECK(nact == 0 && rfmv_hold_shown(&m, 3));
    pad(&m, 16, 0);
    ms(&m, RFM_MOVE_TAP_MS);
    rfm_move_screen(&m, NULL, out);
    CHECK(rfmv_hold_shown(&m, 3) && lit_in(out, 0, 0, 128, 14) > 20 && lit_in(out, 0, 20, 128, 48) > 60);   /* ("Track 4",
                                                         * "Shift": no FX here) */
    write_pbm("move_fm1_shift.pbm", out);
    rfm_move_leds_invalidate(&m);
    n = rfm_move_leds(&m, leds, RFM_MOVE_NLEDS);
    for (i = 0; i < n; i++)
        if (!leds[i].cc && leds[i].number == MR_NOTE_STEP0 + 5)
            CHECK(leds[i].r > 0);                       /* (step 6: Metronome, lit) */
        else if (!leds[i].cc && leds[i].number == MR_NOTE_STEP0 + 3)
            CHECK(leds[i].r == 0);                      /* (step 4: nothing) */
    for (i = 0; i < n; i++)                             /* the icons under the steps: a shortcut dim, none off */
        if (leds[i].cc && leds[i].number == MR_CC_STEP_ICON0 + 5)
            CHECK(!leds[i].rgb && leds[i].r == 24);
        else if (leds[i].cc && leds[i].number == MR_CC_STEP_ICON0 + 3)
            CHECK(leds[i].r == 0);
    clear();
    step(&m, 5, 1), step(&m, 5, 0);                     /* Track 4 + step 6: the metronome */
    CHECK(m.seq.metro && count(RFM_MOVE_ACT_KEY) == 0);
    {   /* written on change only: the metronome's icon full now, nothing else */
        int k = 0;
        n = rfm_move_leds(&m, leds, RFM_MOVE_NLEDS);
        for (i = 0; i < n; i++)
            if (leds[i].cc && leds[i].number == MR_CC_STEP_ICON0 + 5) {
                k++;
                CHECK(leds[i].r == 127);
            } else if (leds[i].cc && leds[i].number >= MR_CC_STEP_ICON0 && leds[i].number < MR_CC_STEP_ICON0 + 16)
                k += 100;
        CHECK(k == 1);
    }
    step(&m, 6, 1), step(&m, 6, 0);                     /* Track 4 + step 7: Groove's menu */
    rfm_move_leds(&m, leds, RFM_MOVE_NLEDS);
    track(&m, 3, 0);
    {   /* Track 4 let go: every icon off (and the step pads' own LEDs, notes 16..31, untouched by them) */
        int k = 0, on = 0;
        n = rfm_move_leds(&m, leds, RFM_MOVE_NLEDS);
        for (i = 0; i < n; i++)
            if (leds[i].cc && leds[i].number >= MR_CC_STEP_ICON0 && leds[i].number < MR_CC_STEP_ICON0 + 16)
                k++, on += leds[i].r != 0;
        CHECK(k == 14 && on == 0);
    }
    CHECK(m.seq.smenu == RFM_MOVE_SM_GROOVE && m.s.mode == RFM_MOVE_MODE_FM1 && !m.seq.projects && m.pop != POP_MODE);
    clear();
    jog(&m, 2);                                         /* (the menu's: the groove, not SELECT) */
    CHECK(m.seq.groove == 10 && count(RFM_MOVE_ACT_TURN) == 0);
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_fm1_groove.pbm", out);
    clear();
    back(&m);                                           /* Back: the menu closes (not the FM-1's SEQ) */
    CHECK(!m.seq.smenu && nact == 0 && m.s.mode == RFM_MOVE_MODE_FM1);
    back(&m);                                           /* (0.9.0: nothing open: nothing, no flip, no FM-1 button) */
    CHECK(m.s.mode == RFM_MOVE_MODE_FM1 && !m.seq.projects && nact == 0);
    track(&m, 3, 1), button(&m, MR_CC_REC, 1), button(&m, MR_CC_REC, 0), track(&m, 3, 0);   /* Track 4 + Record */
    CHECK(m.seq.smenu == RFM_MOVE_SM_REC && !m.seq.projects && count(RFM_MOVE_ACT_BTN) == 0);
    clear();
    jog(&m, 1);
    CHECK(m.seq.rec_mode == RFM_MOVE_RM_RECORD && count(RFM_MOVE_ACT_TURN) == 0);
    back(&m);
    track(&m, 3, 1), step(&m, 4, 1), step(&m, 4, 0), now_ms += 400, step(&m, 4, 1), step(&m, 4, 0), track(&m, 3, 0);
    CHECKF(m.seq.bpm10 >= 1450 && m.seq.bpm10 <= 1500 && m.seq.smenu == RFM_MOVE_SM_TEMPO, "tapped %d", m.seq.bpm10);   /* (Track
                                                         * 4 + step 5 twice, ~410 ms apart: tap tempo) */
    back(&m);
    CHECK(!m.seq.smenu);
    track(&m, 3, 1), step(&m, 0, 1), step(&m, 0, 0), track(&m, 3, 0);   /* Track 4 + step 1: the projects */
    CHECK(m.seq.projects);
    back(&m);
    CHECK(!m.seq.projects && m.s.mode == RFM_MOVE_MODE_FM1);
    /* (0.8.0) Shift in a note layout (0.9.0: the sequencer's Shift): held, or locked: the Shift layer's icons; in the
     * FM-1 layout none (GLO) */
    gap();
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_SCALE);
    button(&m, MR_CC_SHIFT, 1);
    {
        uint8_t ic[16];
        rfmv_shift_icons(&m, ic);
        CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && ic[0] == 24 && ic[3] == 0 && ic[5] == 127 && ic[15] == 24);   /* (the
                                                         * metronome on: full) */
        button(&m, MR_CC_SHIFT, 0);
        rfmv_shift_icons(&m, ic);
        CHECK(ic[0] == 0 && ic[5] == 0);
        now_ms += 1000;
        button(&m, MR_CC_SHIFT, 1), button(&m, MR_CC_SHIFT, 0), button(&m, MR_CC_SHIFT, 1), button(&m, MR_CC_SHIFT, 0);
        rfmv_shift_icons(&m, ic);
        CHECK(m.seq.shift_lock && ic[0] == 24);
        back(&m);
        CHECK(!m.seq.shift_lock && m.s.mode == RFM_MOVE_MODE_SEQ);
    }
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_FM1);
    button(&m, MR_CC_SHIFT, 1);
    {
        uint8_t ic[16];
        rfmv_shift_icons(&m, ic);
        for (i = 0; i < 16; i++)
            CHECK(ic[i] == 0);
    }
    button(&m, MR_CC_SHIFT, 0);
    back(&m);                                           /* (Back: nothing; still the FM-1 layout) */
    CHECK(m.s.mode == RFM_MOVE_MODE_FM1);
    clear();
    step(&m, 4, 1), step(&m, 4, 0);                     /* (Track 4 let go: the steps are keys again) */
    CHECK(count(RFM_MOVE_ACT_KEY) == 2);
}

/* a Track button switches only on a tap: its name at the release, first (the new mode's screen once the name has
 * gone); a press shows nothing new; a hold is its menu over the mode that is on, which stays */
static void test_mode_name_first(void)
{
    static rfm_move_t m;
    uint8_t out[MR_SCREEN_BYTES];
    int x, y, d;
    rfm_move_init(&m, NULL, on_act, NULL);
    two_slots(&m);
    CHECK(rfm_move_set_slot_layout(&m, 1, RFM_MOVE_LAY_DRUM) == 0 && rfm_move_fw_active(&m) == 0);
    gap();
    track(&m, 1, 1);                                    /* Track 2 down: still slot 1's FM-1 layout, no name */
    rfm_move_screen(&m, NULL, out);
    CHECK(m.s.mode == RFM_MOVE_MODE_FM1 && rfm_move_fw_active(&m) == 0 && m.pop != POP_LAYOUT);
    clear();
    track(&m, 1, 0);                                    /* the tap's release: slot 2, its Drum layout, its name only */
    rfm_move_screen(&m, NULL, out);
    for (d = 0, y = 0; y < 64; y++)
        for (x = 0; x < 128; x++)
            d += px_at(out, x, y) && ((y > 14 && y < 20) || (y > 48 && y < 60));
    CHECK(rfm_move_fw_active(&m) == 1 && count(RFM_MOVE_ACT_FW_ACTIVE) == 1 && m.s.mode == RFM_MOVE_MODE_SEQ &&
          m.s.layout == RFM_MOVE_LAYOUT_DRUM && m.pop == POP_LAYOUT && m.pop_i == 1 && d == 0 &&
          rfm_move_screen_timed(&m));                   /* (no pads drawn; the host draws again) */
    ms(&m, RFM_MOVE_POPUP_MS);
    rfm_move_screen(&m, NULL, out);
    CHECK(!rfmv_pop_up(&m, POP_LAYOUT) && !rfm_move_screen_timed(&m));   /* (then the layout's screen) */
    gap();
    track(&m, 0, 1);                                    /* Track 1 held 300 ms: its menu over slot 2's layout */
    ms(&m, RFM_MOVE_TAP_MS);
    rfm_move_screen(&m, NULL, out);
    CHECK(rfmv_hold_shown(&m, 0) && lit_in(out, 0, 20, 128, 48) > 60 && m.s.mode == RFM_MOVE_MODE_SEQ);
    now_ms += 400;
    track(&m, 0, 0);
    CHECK(!rfmv_pop_up(&m, POP_LAYOUT) && rfm_move_fw_active(&m) == 1 && m.s.mode == RFM_MOVE_MODE_SEQ);   /* (no
                                                         * switch, no name) */
    tap(&m, 0);                                         /* (a tap: slot 1, its FM-1 layout) */
    ms(&m, RFM_MOVE_POPUP_MS);
    CHECK(rfm_move_fw_active(&m) == 0 && m.s.mode == RFM_MOVE_MODE_FM1);
    track(&m, 1, 1), ms(&m, RFM_MOVE_TAP_MS);          /* Track 2 held from slot 1: slot 2's menu, its pads */
    rfm_move_screen(&m, NULL, out);
    CHECK(rfmv_hold_shown(&m, 1) && lit_in(out, 0, 20, 128, 48) > 60 && m.s.mode == RFM_MOVE_MODE_FM1);
    pad(&m, 0, 100), pad(&m, 0, 0);                     /* (the top-left scale pad: 0.9.0, the key the tracks use;
                                                         * the FM-1 layout played: the project's) */
    CHECK(m.seq.scale == rfmv_scale_of_pad(0) && m.s.scale == RFM_MOVE_SCALE_MAJOR && m.menu_what == MENU_SCALE);
    now_ms += 400, track(&m, 1, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_FM1 && rfm_move_fw_active(&m) == 0 && !rfmv_pop_up(&m, POP_LAYOUT));   /* (slot
                                                         * 1's FM-1 layout, as it was) */
}

/* (0.8.0) Track 4 in every mode: held, it is the Shift layer and switches nothing (a note layout: the pads are its
 * menu's, nothing); Track 1's tap names the slot's layout first */
static void test_track1_anywhere(void)
{
    static rfm_move_t m;
    uint8_t out[MR_SCREEN_BYTES];
    uint8_t ic[16];
    int x, y, d;
    rfm_move_init(&m, NULL, on_act, NULL);
    lay(&m, 0, RFM_MOVE_LAY_SCALE);                     /* a note layout (0.9.0: the sequencer) */
    ms(&m, RFM_MOVE_POPUP_MS);
    gap();
    track(&m, 3, 1);                                    /* Track 4 down: still the note layout, the Shift layer */
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ);
    rfmv_shift_icons(&m, ic);
    CHECK(ic[5] == 0 && ic[3] == 0);                    /* (0.12.0, a8: nothing of the hold before its 300 ms) */
    ms(&m, RFM_MOVE_TAP_MS + 10);
    rfmv_shift_icons(&m, ic);
    CHECK(ic[5] == 24 && ic[3] == 0);                   /* (the icons under the steps) */
    clear();
    pad(&m, 0, 100), pad(&m, 0, 0);                     /* (the pads: its menu's, no note) */
    CHECK(count(RFM_MOVE_ACT_MIDI) == 0);
    step(&m, 5, 1), step(&m, 5, 0);                     /* Track 4 + step 6: the metronome */
    CHECK(m.seq.metro);
    ms(&m, RFM_MOVE_POPUP_MS);                          /* (the CLICK ON message gone) */
    rfm_move_screen(&m, NULL, out);
    CHECK(rfmv_hold_shown(&m, 3) && lit_in(out, 0, 20, 128, 48) > 60);   /* (held: Track 4's screen over MIDI mode's) */
    step(&m, 8, 1), step(&m, 8, 0);                     /* Track 4 + step 9: Keys & Scales, open past the release */
    track(&m, 3, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.seq.smenu == RFM_MOVE_SM_KEYS && !m.seq.projects);   /* (used: no tap) */
    clear();
    knob(&m, 1, RFM_MOVE_LIST_TICKS);                   /* (the menu's: the scale, not a CC) */
    CHECK(m.s.scale == 1 && count(RFM_MOVE_ACT_TURN) == 0 && count(RFM_MOVE_ACT_MIDI) == 0);
    back(&m);
    CHECK(!m.seq.smenu && m.s.mode == RFM_MOVE_MODE_SEQ);
    /* a tap on Track 1: the slot's layout named first */
    gap();
    track(&m, 0, 1);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.pop != POP_LAYOUT);
    track(&m, 0, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.pop == POP_LAYOUT && m.pop_i == 0);
    rfm_move_screen(&m, NULL, out);
    for (d = 0, y = 0; y < 64; y++)
        for (x = 0; x < 128; x++)
            d += px_at(out, x, y) && ((y > 14 && y < 20) || (y > 48 && y < 60));
    CHECK(d == 0);                                      /* (the name only, no drawing) */
    /* (0.9.0) Back with nothing open: nothing (no flip); Track 4 held is the Shift layer here too (no step entered) */
    gap();
    back(&m);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && !m.seq.view);
    ms(&m, RFM_MOVE_POPUP_MS);
    track(&m, 3, 1), step(&m, 5, 1), step(&m, 5, 0), track(&m, 3, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && !m.seq.metro && m.seq.nnotes == 0 && !m.seq.projects);
}

/* the Velocity menu ((0.8.0) Track 4 held + step 10 in any mode, Shift + step 10 in the sequencer): sticky; the jog
 * the option, knob 1 the level; every pad that plays goes through it */
static void test_velocity(void)
{
    static rfm_move_t m;
    rfm_move_settings_t s;
    uint8_t img[RFM_MOVE_SETTINGS_BYTES];
    rfm_move_settings_default(&s);
    CHECK(rfm_move_vel(&s, 40) == 40 && rfm_move_vel(&s, 0) == 1);
    s.vel_mode = RFM_MOVE_VEL_SOFT;
    CHECK(rfm_move_vel(&s, 40) > 40 && rfm_move_vel(&s, 127) == 127 && rfm_move_vel(&s, 1) >= 1);
    s.vel_mode = RFM_MOVE_VEL_HARD;
    CHECK(rfm_move_vel(&s, 40) < 40 && rfm_move_vel(&s, 127) == 127 && rfm_move_vel(&s, 1) == 1);
    s.vel_mode = RFM_MOVE_VEL_FIXED, s.vel_level = 77;
    CHECK(rfm_move_vel(&s, 5) == 77 && !strcmp(rfm_move_vel_name(s.vel_mode), "FIXED"));
    rfm_move_save(&s, img);
    rfm_move_settings_default(&s);
    CHECK(rfm_move_load(&s, img, sizeof img) == 0 && s.vel_mode == RFM_MOVE_VEL_FIXED && s.vel_level == 77);

    rfm_move_init(&m, NULL, on_act, NULL);
    track(&m, 3, 1), step(&m, 9, 1), step(&m, 9, 0), track(&m, 3, 0);   /* FM-1 mode: the menu, over FM-1 mode */
    CHECK(m.seq.smenu == RFM_MOVE_SM_VEL && m.s.mode == RFM_MOVE_MODE_FM1);
    jog(&m, 1), knob(&m, 0, -27);                       /* FIXED, 100 */
    CHECK(m.s.vel_mode == RFM_MOVE_VEL_FIXED && m.s.vel_level == 100 && m.seq.smenu == RFM_MOVE_SM_VEL);
    button(&m, MR_CC_BACK, 1), button(&m, MR_CC_BACK, 0);
    CHECK(!m.seq.smenu);
    lay(&m, 0, RFM_MOVE_LAY_SCALE);                     /* MIDI mode: a pad at the level */
    clear();
    pad(&m, 0, 20);
    CHECK(has_midi(0x90, 48, 100));
    pad(&m, 0, 0);
}

/* (0.8.0) close what Back closes (a menu, a layer, the projects view, Global settings): Back clicks while one is
 * open, so that none flips the mode */
static void close_all(rfm_move_t *m)
{
    int k;
    for (k = 0; k < 8 && (rfmv_seq_layer_open(m) || m->seq.projects || m->seq.smenu); k++)
        back(m);
}
/* the owner's: only a Track gesture changes the mode (0.9.0: Back's click with nothing open flips nothing). Every other
 * button (alone, with Shift, with a Track held, with Back after to close what it opened), the jog and the knobs leave
 * it as it was, in every layout (0.9.0: the FM-1 layout, a note layout = the sequencer, the bare Sequencer API) */
static void test_mode_only_by_tap(void)
{
    static const int CC[] = { MR_CC_CAPTURE, MR_CC_SAMPLE, MR_CC_MUTE, MR_CC_DELETE, MR_CC_COPY, MR_CC_SHIFT,
                              MR_CC_MENU, MR_CC_UNDO, MR_CC_LOOP, MR_CC_PLAY, MR_CC_REC, MR_CC_LEFT,
                              MR_CC_RIGHT, MR_CC_UP, MR_CC_DOWN };
    static const int MODES[3] = { RFM_MOVE_MODE_FM1, RFM_MOVE_MODE_MIDI, RFM_MOVE_MODE_SEQ };
    static rfm_move_t m;
    int mi, i, ok = 1;
    rfm_move_init(&m, NULL, on_act, NULL);
    for (mi = 0; mi < 3; mi++) {
        const int mode = MODES[mi] == RFM_MOVE_MODE_MIDI ? RFM_MOVE_MODE_SEQ : MODES[mi];
        rfm_move_set_mode(&m, MODES[mi]);
        CHECK(m.s.mode == mode);
        for (i = 0; i < (int)(sizeof CC / sizeof CC[0]); i++) {
            button(&m, CC[i], 1), button(&m, CC[i], 0);
            button(&m, MR_CC_SHIFT, 1), button(&m, CC[i], 1), button(&m, CC[i], 0), button(&m, MR_CC_SHIFT, 0);
            if (CC[i] != MR_CC_MENU)                    /* (Track 1 held + Menu: session view, a Track gesture; 0.12.0:
                                                         * Track 4 held + Menu no longer flips it back) */
                track(&m, 0, 1), button(&m, CC[i], 1), button(&m, CC[i], 0), now_ms += 400, track(&m, 0, 0);
            track(&m, 3, 1), button(&m, CC[i], 1), button(&m, CC[i], 0), now_ms += 400, track(&m, 3, 0);
            close_all(&m);
            CHECK(!m.seq.projects);
            jog(&m, 1), knob(&m, 0, 2);
            if (m.s.mode != mode) {
                CHECKF(0, "mode %d changed to %d by CC %d", mode, m.s.mode, CC[i]);
                ok = 0;
                rfm_move_set_mode(&m, MODES[mi]);
            }
        }
        for (i = 0; i < 16; i++) {                      /* (the Shift layer's shortcuts, Back after each) */
            if (mode == RFM_MOVE_MODE_SEQ)
                button(&m, MR_CC_SHIFT, 1), step(&m, i, 1), step(&m, i, 0), button(&m, MR_CC_SHIFT, 0);
            else
                track(&m, 3, 1), step(&m, i, 1), step(&m, i, 0), now_ms += 400, track(&m, 3, 0);
            close_all(&m);
            CHECK(!m.seq.projects);
            if (m.s.mode != mode) {
                CHECKF(0, "mode %d changed to %d by shortcut %d", mode, m.s.mode, i + 1);
                ok = 0;
                rfm_move_set_mode(&m, MODES[mi]);
            }
        }
        rfm_move_seq_play(&m, 0);
    }
    CHECK(ok);
    /* (0.9.0) Back's click with nothing open: no flip, in the FM-1 layout or a note layout */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    back(&m);
    CHECK(m.s.mode == RFM_MOVE_MODE_FM1);
    rfm_move_set_mode(&m, RFM_MOVE_MODE_MIDI);
    back(&m);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && rfm_move_slot_layout(&m, 0) == RFM_MOVE_LAY_SCALE);
}

/* the Shift menus take the screen, knobs, jog, Back and Session only: the pads and steps play on (the owner's) */
static void test_menus_leave_pads(void)
{
    static rfm_move_t m;
    rfm_move_init(&m, NULL, on_act, NULL);
    track(&m, 3, 1), step(&m, 8, 1), step(&m, 8, 0), now_ms += 400, track(&m, 3, 0);   /* Keys & Scales over FM-1 */
    CHECK(m.seq.smenu == RFM_MOVE_SM_KEYS && m.s.mode == RFM_MOVE_MODE_FM1);
    clear();
    pad(&m, pad_of(1, 0), 100), pad(&m, pad_of(1, 0), 0);
    step(&m, 0, 1), step(&m, 0, 0);
    CHECK(count(RFM_MOVE_ACT_KEY) >= 4 && m.seq.smenu == RFM_MOVE_SM_KEYS);   /* (a key and a step-key, down and up) */
    knob(&m, 1, RFM_MOVE_LIST_TICKS);
    CHECK(m.s.scale == 1);
    button(&m, MR_CC_MENU, 1), button(&m, MR_CC_MENU, 0);   /* (0.12.0, l2) Menu: the FM-1's HOME, the menu stays */
    CHECK(m.seq.smenu == RFM_MOVE_SM_KEYS && has(RFM_MOVE_ACT_BTN, RFM_MOVE_B_HOME, 1, 0));
    button(&m, MR_CC_BACK, 1), button(&m, MR_CC_BACK, 0);   /* (only Back closes it) */
    CHECK(!m.seq.smenu);
}

/* a held Track (its menu or layer) or a Shift menu: only the knobs it uses lit, in the menu colour (deep orange, no
 * mode's or bank's); the others off; back as they were after; written on change only */
static void ring_of(rfm_move_t *m, int k, uint8_t rgb[3])
{
    static rfm_move_led_t out[RFM_MOVE_NLEDS];
    int i, n;
    rfm_move_leds_invalidate(m);
    n = rfm_move_leds(m, out, RFM_MOVE_NLEDS);
    for (i = 0; i < n; i++)
        if (out[i].cc && out[i].number == MR_CC_KNOB1 + k)
            rgb[0] = out[i].r, rgb[1] = out[i].g, rgb[2] = out[i].b;
}
static void test_menu_knob_leds(void)
{
    static rfm_move_t m;
    static rfm_move_led_t out[RFM_MOVE_NLEDS];
    uint8_t a[3] = { 0 }, b[3] = { 0 }, n0[3] = { 0 }, n4[3] = { 0 };
    int k;
    rfm_move_init(&m, NULL, on_act, NULL);
    ring_of(&m, 0, n0), ring_of(&m, 4, n4);             /* (FM-1 mode: the knobs as they are) */
    /* (0.8.0) Track 1 held over the FM-1 layout: its menu has no knob job, every ring off */
    track(&m, 0, 1), ms(&m, RFM_MOVE_TAP_MS);
    for (k = 0; k < 8; k++) {
        ring_of(&m, k, a);
        CHECK(!a[0] && !a[1] && !a[2]);
    }
    now_ms += 400, track(&m, 0, 0);
    ring_of(&m, 0, a), ring_of(&m, 4, b);
    CHECK(!memcmp(a, n0, 3) && !memcmp(b, n4, 3));       /* (as they were) */
    /* .. over a note layout: the 8 CC numbers, white; Track 4 held: none */
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_SCALE);
    ring_of(&m, 0, n0), ring_of(&m, 4, n4);
    track(&m, 0, 1), ms(&m, RFM_MOVE_TAP_MS);
    ring_of(&m, 0, a), ring_of(&m, 4, b);
    CHECK(a[0] == 255 * 128 / 256 && a[1] == a[0] && a[2] == a[0] && !memcmp(a, b, 3));   /* (white, half) */
    CHECK(rfm_move_leds(&m, out, RFM_MOVE_NLEDS) == 0);   /* (written once) */
    now_ms += 400, track(&m, 0, 0);
    ring_of(&m, 0, a), ring_of(&m, 4, b);
    CHECK(!memcmp(a, n0, 3) && !memcmp(b, n4, 3));       /* (as they were) */
    track(&m, 3, 1), ms(&m, RFM_MOVE_TAP_MS);
    for (k = 0; k < 8; k++) {
        ring_of(&m, k, a);
        CHECK(!a[0] && !a[1] && !a[2]);
    }
    now_ms += 400, track(&m, 3, 0);
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_FM1);
    ring_of(&m, 0, n0);
    track(&m, 3, 1), step(&m, 9, 1), step(&m, 9, 0), now_ms += 400, track(&m, 3, 0);   /* the Velocity menu: knob 1 */
    for (k = 0; k < 8; k++) {
        ring_of(&m, k, a);
        CHECK(k == 0 ? a[0] && a[1] && !a[2] : !a[0] && !a[1] && !a[2]);
    }
    back(&m);
    ring_of(&m, 0, a);
    CHECK(!memcmp(a, n0, 3) && !m.seq.smenu && m.s.mode == RFM_MOVE_MODE_FM1);
}

/* (0.5.0) the owner's: Track 4 held + a pad changes neither the layout nor the scale (Track 2's job), in any mode;
 * Track 2 held + a pad still does */
static void test_track4_pads(void)
{
    static rfm_move_t m;
    static const int MODES[3] = { RFM_MOVE_MODE_FM1, RFM_MOVE_MODE_MIDI, RFM_MOVE_MODE_SEQ };
    int i;
    for (i = 0; i < 3; i++) {
        const int mode = MODES[i], want = mode == RFM_MOVE_MODE_FM1 ? RFM_MOVE_MODE_FM1 : RFM_MOVE_MODE_SEQ;
        int lay0;
        rfm_move_init(&m, NULL, on_act, NULL);
        rfm_move_set_mode(&m, mode);                    /* (0.9.0: MIDI = the slot's note layout = the sequencer) */
        lay0 = rfm_move_slot_layout(&m, 0);
        clear();
        track(&m, 3, 1);                                /* (0.8.0) Track 4 held: pads 1, 2 the Mixer, Routing (no FX
                                                         * here: nothing); the rest nothing */
        pad(&m, 25, 100), pad(&m, 25, 0);
        pad(&m, 3, 100), pad(&m, 3, 0);
        now_ms += 400, track(&m, 3, 0);                 /* (a hold: no tap) */
        CHECKF(m.s.mode == want && rfm_move_slot_layout(&m, 0) == lay0 && m.s.scale == RFM_MOVE_SCALE_MAJOR &&
               !m.seq.projects && count(RFM_MOVE_ACT_SETTINGS) == 0 && count(RFM_MOVE_ACT_MIDI) == 0,
               "mode %d: layout %d scale %d", mode, rfm_move_slot_layout(&m, 0), m.s.scale);
        track(&m, 0, 1);                                /* Track 1 held: its menu, in every layout (0.9.0: the sequencer
                                                         * too): Chroma, then a scale pad (the key the track uses) */
        pad(&m, 24 + RFM_MOVE_LAY_CHROMA, 100), pad(&m, 24 + RFM_MOVE_LAY_CHROMA, 0);
        pad(&m, 3, 100), pad(&m, 3, 0);
        now_ms += 400, track(&m, 0, 0);
        CHECKF(m.s.mode == RFM_MOVE_MODE_SEQ && m.s.layout == RFM_MOVE_LAYOUT_CHROMATIC &&
               rfm_move_slot_layout(&m, 0) == RFM_MOVE_LAY_CHROMA && m.s.scale == rfmv_scale_of_pad(3) &&
               m.seq.scale == rfmv_scale_of_pad(3) && count(RFM_MOVE_ACT_MIDI) == 0, "mode %d", mode);
    }
}

/* (0.5.3) latch, the note layouts: (0.8.0) Track 1 held + the top row's pad 8 toggles it (named); released pads keep
 * sounding; a press with no pad down is a new chord (the old one goes); a latched note pressed again goes; off, the
 * Sequencer, release_all and a project load let them all go (a layout change in the slot keeps them) */
static int sounding_notes(const rfm_move_t *m)
{
    int c, n, k = 0;
    for (c = 0; c < 16; c++)
        for (n = 0; n < 128; n++)
            k += m->note_count[c][n] != 0;
    return k;
}
static void test_latch(void)
{
    static rfm_move_t m;
    static uint8_t out[MR_SCREEN_BYTES];
    int n0, n1, n2;
    rfm_move_init(&m, NULL, on_act, NULL);
    rfm_move_set_mode(&m, RFM_MOVE_MODE_MIDI);         /* (0.9.0: the slot's Scale layout, the sequencer, track 1) */
    n0 = rfm_move_pad_note(&m.s, 0), n1 = rfm_move_pad_note(&m.s, 1), n2 = rfm_move_pad_note(&m.s, 2);
    CHECK(!rfm_move_latch(&m) && n0 >= 0 && n1 >= 0 && n2 >= 0 && m.s.mode == RFM_MOVE_MODE_SEQ);
    /* the toggle: Track 1 held + pad 32 (the top row's 8th), its popup over the menu, the pad lit */
    clear();
    track(&m, 0, 1), pad(&m, 31, 100), pad(&m, 31, 0);
    CHECK(rfm_move_latch(&m) && m.pop == POP_MODE && m.pop_i == 31 && count(RFM_MOVE_ACT_SETTINGS) == 0);
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_latch_on.pbm", out);
    CHECK(lit_in(out, 20, 18, 108, 40) > 0);
    now_ms += 400, track(&m, 0, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.s.layout == RFM_MOVE_LAYOUT_SCALE);
    /* a note latched: no off at its release (0.9.0: in the note layout, the sequencer, live notes) */
    clear();
    pad(&m, 0, 100), pad(&m, 0, 0);
    CHECK(has_midi(0x90, n0, 100) && !has_midi(0x80, n0, 0) && m.nlat == 1);
    m.pop = POP_NONE;
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_latch_midi.pbm", out);
    /* (0.9.0) no framed "L" in the sequencer's track view (its corner holds the 16-track map); the Track menu's pad 8
     * shows the latch */
    /* a new chord (no pad down): the old one goes; its notes latched together */
    clear();
    pad(&m, 1, 100), pad(&m, 2, 100);
    CHECK(has_midi(0x80, n0, 0) && has_midi(0x90, n1, 100) && has_midi(0x90, n2, 100));
    pad(&m, 1, 0), pad(&m, 2, 0);
    CHECK(m.nlat == 2 && !has_midi(0x80, n1, 0) && !has_midi(0x80, n2, 0));
    /* a latched note pressed again: it goes, nothing played */
    clear();
    pad(&m, 1, 100), pad(&m, 1, 0);
    CHECK(has_midi(0x80, n1, 0) && !has_midi(0x90, n1, 100) && m.nlat == 1 && sounding_notes(&m) == 1);
    /* off: all go */
    clear();
    track(&m, 0, 1), pad(&m, 31, 100), pad(&m, 31, 0), now_ms += 400, track(&m, 0, 0);
    CHECK(!rfm_move_latch(&m) && m.pop_i == 30 && m.nlat == 0 && sounding_notes(&m) == 0);
    CHECK(has_midi(0x80, n2, 0));
    /* off: a pad's release ends it, as before */
    clear();
    pad(&m, 0, 100), pad(&m, 0, 0);
    CHECK(has_midi(0x80, n0, 0) && sounding_notes(&m) == 0);
    /* (0.8.0) a layout change in the slot keeps them; (0.9.0) the FM-1 layout (out of the sequencer) lets them go
     * (latch stays on); release_all; a project load */
    rfm_move_set_latch(&m, 1);
    pad(&m, 0, 100), pad(&m, 0, 0);
    clear();
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_CHROMA);
    CHECK(m.s.layout == RFM_MOVE_LAYOUT_CHROMATIC && !has_midi(0x80, n0, 0) && m.nlat == 1 && sounding_notes(&m) == 1);
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_SCALE);
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_FM1);
    CHECK(has_midi(0x80, n0, 0) && m.nlat == 0 && rfm_move_latch(&m) && sounding_notes(&m) == 0);
    rfm_move_set_mode(&m, RFM_MOVE_MODE_MIDI);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && m.s.layout == RFM_MOVE_LAYOUT_SCALE);
    pad(&m, 0, 100), pad(&m, 0, 0);
    CHECK(m.nlat == 1 && sounding_notes(&m) == 1);
    rfm_move_release_all(&m);
    CHECK(m.nlat == 0 && sounding_notes(&m) == 0);
    pad(&m, 0, 100), pad(&m, 0, 0);
    CHECK(rfm_move_project_load(&m, 0, NULL, 0) == 0 && m.nlat == 0 && sounding_notes(&m) == 0);
    /* FM-1 mode: no latch (the keys let go) */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    clear();
    pad(&m, 0, 100), pad(&m, 0, 0);
    CHECK(count(RFM_MOVE_ACT_KEY) == 2 && m.nlat == 0);
    rfm_move_set_latch(&m, 0);
}

/* (0.5.4) latched notes stay on their firmware slot: a slot switch leaves them sounding there, their offs go there */
static int midi_dest(int st, int d1)                    /* the dest of the first such MIDI action, -1 none */
{
    int i;
    for (i = 0; i < nact; i++)
        if (acts[i].kind == RFM_MOVE_ACT_MIDI && acts[i].bytes[0] == st && acts[i].bytes[1] == d1)
            return acts[i].dest;
    return -1;
}
static void test_latch_slots(void)
{
    static rfm_move_t m;
    static rfm_move_fw_t fl[2];
    static const char *const sl[3] = { "a", "b", NULL };
    int n0, n1;
    fl[0].id = "a", fl[0].name = "A", fl[0].version = fl[0].installed = "1";
    fl[1].id = "b", fl[1].name = "B", fl[1].version = fl[1].installed = "1";
    rfm_move_init(&m, NULL, on_act, NULL);
    rfm_move_fw_list(&m, fl, 2);
    rfm_move_fw_slots(&m, sl);
    rfm_move_set_mode(&m, RFM_MOVE_MODE_MIDI);
    CHECK(rfm_move_set_slot_layout(&m, 1, RFM_MOVE_LAY_SCALE) == 0);   /* (0.8.0: each slot its own layout) */
    rfm_move_set_latch(&m, 1);
    n0 = rfm_move_pad_note(&m.s, 0), n1 = rfm_move_pad_note(&m.s, 1);
    /* (0.9.0) each slot's note layout plays its selected track: slot 1 track 1 (channel 1, 0x90), slot 2 track 2
     * (channel 2, 0x91) */
    /* slot 1 latched n0; slot 2 active: nothing sent, the same note plays there anew */
    pad(&m, 0, 100), pad(&m, 0, 0);
    CHECK(m.nlat == 1 && rfm_move_fw_active(&m) == 0);
    clear();
    rfm_move_fw_set_active(&m, 1);
    CHECK(count(RFM_MOVE_ACT_FW_ACTIVE) == 1 && !has_midi(0x80, n0, 0) && m.seq.sel_track == 1);
    pad(&m, 0, 100);
    CHECK(has_midi(0x91, n0, 100) && midi_dest(0x91, n0) == 0 && !has_midi(0x80, n0, 0));   /* (a new chord: slot 1's
                                                         * stays) */
    pad(&m, 0, 0);
    CHECKF(m.nlat == 2 && sounding_notes(&m) == 1, "latched %d, sounding %d", m.nlat, sounding_notes(&m));   /* (slot
                                                         * 1's is counted on its slot, as in 0.5.4) */
    /* a new chord on slot 2: only slot 2's latched note goes (to slot 2) */
    clear();
    pad(&m, 1, 100), pad(&m, 1, 0);
    CHECK(has_midi(0x81, n0, 0) && midi_dest(0x81, n0) == 2 && !has_midi(0x80, n0, 0) && has_midi(0x91, n1, 100) &&
          m.nlat == 2);
    /* back on slot 1: its note is a hold again; a re-press there lets it go, to slot 1 */
    clear();
    rfm_move_fw_set_active(&m, 0);
    pad(&m, 0, 100), pad(&m, 0, 0);
    CHECK(has_midi(0x80, n0, 0) && midi_dest(0x80, n0) == 1 && !has_midi(0x90, n0, 100) && m.nlat == 1);
    /* latch off: slot 2's n1 goes to slot 2, while slot 1 is active */
    clear();
    rfm_move_set_latch(&m, 0);
    CHECK(has_midi(0x81, n1, 0) && midi_dest(0x81, n1) == 2 && m.nlat == 0 && sounding_notes(&m) == 0);
    /* release_all and leaving the note layout: every slot's, each to its own */
    rfm_move_set_latch(&m, 1);
    pad(&m, 0, 100), pad(&m, 0, 0);
    rfm_move_fw_set_active(&m, 1);
    pad(&m, 0, 100), pad(&m, 0, 0);
    clear();
    rfm_move_release_all(&m);
    CHECK(count(RFM_MOVE_ACT_MIDI) == 2 && m.nlat == 0 && sounding_notes(&m) == 0);
    CHECK(acts[0].dest + acts[1].dest == 3 && acts[0].dest != acts[1].dest);
    pad(&m, 0, 100), pad(&m, 0, 0);
    rfm_move_fw_set_active(&m, 0);
    clear();
    rfm_move_set_slot_layout(&m, 0, RFM_MOVE_LAY_FM1);  /* (0.9.0: out of the sequencer, the FM-1 layout) */
    CHECK(has_midi(0x81, n0, 0) && midi_dest(0x81, n0) == 2 && m.nlat == 0 && sounding_notes(&m) == 0);
    /* a pad played unlatched: no dest, as always */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_MIDI);
    rfm_move_set_latch(&m, 0);
    clear();
    pad(&m, 0, 100), pad(&m, 0, 0);
    CHECK(midi_dest(0x90, n0) == 0 && midi_dest(0x80, n0) == 0);
}

/* (0.5.4) a host's message: big, a second, over any screen; wrapped onto two rows */
/* (0.5.6) the firmware's labels in FM-1 mode: a button held 300 ms, anything pressed while a button is held, a knob
 * touched; a contact sheet of them (move_labels_sheet.pbm) */
static uint8_t g_sheet[12][MR_SCREEN_BYTES];
static int g_nsheet;
static const char *g_sheet_pre = "labels";
static void sheet_add(const uint8_t *out, const char *name)
{
    char path[64];
    snprintf(path, sizeof path, "move_%s_%s.pbm", g_sheet_pre, name);
    write_pbm(path, out);
    if (g_nsheet < 12)
        memcpy(g_sheet[g_nsheet++], out, MR_SCREEN_BYTES);
}
static void sheet_write(void)                           /* 3 across, a 4-pixel grey-less gap (0) */
{
    char path[64];
    FILE *f;
    const int rows = (g_nsheet + 2) / 3, W = 3 * 128 + 4 * 4, H = rows * 64 + (rows + 1) * 4;
    int x, y;
    snprintf(path, sizeof path, "move_%s_sheet.pbm", g_sheet_pre);
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "P1\n%d %d\n", W, H);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            const int c = (x - 4) / 132, r = (y - 4) / 68, ix = (x - 4) % 132, iy = (y - 4) % 68, i = r * 3 + c;
            int v = 1;                                  /* (the gaps white, the screens as they are) */
            if (x >= 4 && y >= 4 && ix < 128 && iy < 64)
                v = i < g_nsheet && g_sheet[i][(iy >> 3) * 128 + ix] >> (iy & 7) & 1;
            fputc(v ? '1' : '0', f);
        }
        fputc('\n', f);
    }
    fclose(f);
}
static int left_lit(const uint8_t *out) { return lit_in(out, 0, 0, 64, 64); }
static void test_labels(void)
{
    static rfm_move_t m;
    static uint8_t rest_out[MR_SCREEN_BYTES], out[MR_SCREEN_BYTES], out2[MR_SCREEN_BYTES];
    const char *lbl[RFM_MOVE_LBL_N] = { NULL };
    const char *names[RFM_MOVE_NBTN] = { "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+" };
    rfm_move_init(&m, NULL, on_act, NULL);
    m.s.pop_half = 1;                                   /* (0.5.9: the left half; the whole screen: test_popup_width) */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    m.pop = POP_NONE;
    lbl[RFM_MOVE_LBL_KNOB0 + 3] = "Resonance";          /* K1 (FM1P_E_K1 = 3): scrolls */
    lbl[RFM_MOVE_LBL_KNOB0 + 1] = "";                   /* ALGO: no job */
    lbl[RFM_MOVE_LBL_BTN0 + 11] = "Rec arm";            /* REC (FM1P_B_REC = 11) */
    lbl[RFM_MOVE_LBL_BTN0 + 7] = "";                    /* SAVE: no job */
    lbl[RFM_MOVE_LBL_WHITE0 + 0] = "Kick";              /* F3 */
    lbl[RFM_MOVE_LBL_BLACK0 + 0] = "Clear All Steps";   /* F#3 */
    lbl[RFM_MOVE_LBL_WHITE0 + 15] = "Snare";            /* G5 */
    rfm_move_set_labels(&m, lbl);
    rfm_move_set_button_names(&m, names);
    CHECK(!strcmp(m.label[RFM_MOVE_E_K1], "Resonance") && !m.label[RFM_MOVE_E_ALGO][0] && !m.label[RFM_MOVE_E_K2][0]);
    rfm_move_screen(&m, NULL, rest_out);

    /* a tap: nothing shown; held 300 ms: its label (from then until a second after its release) */
    button(&m, MR_CC_REC, 1);
    CHECK(m.pop == POP_LABEL && m.pop_i == RFM_MOVE_LBL_BTN0 + 11 && rfm_move_screen_timed(&m));
    ms(&m, 100);
    rfm_move_screen(&m, NULL, out);
    CHECK(art_diff(out) == 9);                          /* (the drawing: REC's cell filled, no label) */
    button(&m, MR_CC_REC, 0);
    CHECK(m.pop == POP_NONE);
    rfm_move_screen(&m, NULL, out);
    CHECK(!memcmp(out, rest_out, sizeof out));
    button(&m, MR_CC_REC, 1);
    ms(&m, RFM_MOVE_LABEL_HOLD_MS - 5);
    rfm_move_screen(&m, NULL, out);
    CHECK(art_diff(out) == 9 && rfm_move_screen_timed(&m));
    ms(&m, 10);
    rfm_move_screen(&m, NULL, out);
    CHECK(left_lit(out) > 40);                          /* ("Rec" / "Arm": two rows, 0.5.9: at 3x) */
    sheet_add(out, "rec_held");
    ms(&m, 3000);
    CHECK(rfmv_pop_up(&m, POP_LABEL));                  /* (held: it stays) */

    /* a button held: every other control pressed shows its label at once */
    pad(&m, 16, 100);                                   /* F3 */
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_LABEL && m.pop_i == RFM_MOVE_LBL_WHITE0 && left_lit(out) > 20);
    sheet_add(out, "key_kick");
    pad(&m, 16, 0);
    pad(&m, 25, 100);                                   /* F#3: three rows */
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop_i == RFM_MOVE_LBL_BLACK0 && lit_in(out, 0, 3, 64, 19) > 0 && lit_in(out, 0, 45, 64, 61) > 0);
    sheet_add(out, "key_clear_all");
    pad(&m, 25, 0);
    step(&m, 15, 1);                                    /* step 16: G5 */
    CHECK(m.pop_i == RFM_MOVE_LBL_WHITE0 + 15);
    rfm_move_screen(&m, NULL, out);
    sheet_add(out, "step_snare");
    step(&m, 15, 0);
    pad(&m, 18, 100);                                   /* (a key with no label: nothing new) */
    CHECK(m.pop_i == RFM_MOVE_LBL_WHITE0 + 15);
    pad(&m, 18, 0);
    button(&m, MR_CC_PLAY, 1);                          /* PLAY: no live label, its static name */
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop_i == RFM_MOVE_LBL_BTN0 + 10 && left_lit(out) > 20);
    sheet_add(out, "play_static");
    /* the label as it is when drawn: the firmware renames PLAY while it is held */
    lbl[RFM_MOVE_LBL_BTN0 + 10] = "Stop";
    rfm_move_set_labels(&m, lbl);
    rfm_move_screen(&m, NULL, out2);
    CHECK(memcmp(out, out2, sizeof out) != 0);
    sheet_add(out2, "play_renamed");
    button(&m, MR_CC_PLAY, 0);
    button(&m, MR_CC_REC, 0);
    CHECK(rfmv_pop_up(&m, POP_LABEL));
    ms(&m, RFM_MOVE_POPUP_MS + 10);
    CHECK(!rfmv_pop_up(&m, POP_LABEL));
    /* keys alone are an instrument: no label */
    pad(&m, 16, 100);
    CHECK(!rfmv_pop_up(&m, POP_LABEL));
    pad(&m, 16, 0);
    /* "" = no job: SAVE held shows nothing */
    {
        int cc;
        for (cc = 0; cc < 128 && rfm_move_button_of(cc) != RFM_MOVE_B_SAVE; cc++)
            ;
        if (cc < 128) {
            button(&m, cc, 1), ms(&m, 500);
            rfm_move_screen(&m, NULL, out);
            CHECK(!m.pop_shown && art_diff(out) == 9);  /* (0.5.8: up, no words: the drawing only) */
            button(&m, cc, 0);
            CHECK(!rfmv_pop_up(&m, POP_LABEL));
        }
    }

    /* knobs: a touch shows the name (no value yet), turning the detents; a long name scrolls; "" no popup */
    touch(&m, 4, 1);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_ENC && m.pop_blank && lit_in(out, 0, 26, 64, 64) == 0);
    sheet_add(out, "knob_touch");
    ms(&m, 1500);
    rfm_move_screen(&m, NULL, out2);
    CHECK(memcmp(out, out2, sizeof out) != 0 && rfm_move_screen_timed(&m));   /* ("Resonance" scrolled) */
    sheet_add(out2, "knob_scrolled");
    knob(&m, 4, 4);
    rfm_move_screen(&m, NULL, out);
    CHECK(!m.pop_blank && m.pop_val == 2 && lit_in(out, 0, 28, 64, 56) > 20);
    sheet_add(out, "knob_turned");
    touch(&m, 4, 0);
    clear();
    touch(&m, 2, 1);                                    /* ALGO (0.8.0: knob 3): no job */
    knob(&m, 2, 4);
    CHECK(m.pop == POP_NONE && nact == 1 && acts[0].kind == RFM_MOVE_ACT_TURN && acts[0].index == RFM_MOVE_E_ALGO);
    touch(&m, 2, 0);
    touch(&m, 8, 1);                                    /* the volume knob: MASTER and its level */
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_MASTER);
    sheet_add(out, "master_touch");
    touch(&m, 8, 0);
    /* rfm_move_set_label: its "" is the default name again */
    rfm_move_set_label(&m, RFM_MOVE_E_ALGO, "");
    touch(&m, 2, 1);                                    /* (0.8.0: ALGO on knob 3) */
    CHECK(m.pop == POP_ENC && m.pop_i == 2);
    touch(&m, 2, 0);

    /* other modes: no labels (0.8.0: a note layout) */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_MIDI);
    m.pop = POP_NONE;
    button(&m, MR_CC_REC, 1), ms(&m, 500);
    CHECK(!rfmv_pop_up(&m, POP_LABEL));
    button(&m, MR_CC_REC, 0);
    /* cleared: a button's static name stays, keys show nothing */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    rfm_move_set_labels(&m, NULL);
    CHECK(!m.lbl_has[RFM_MOVE_LBL_BTN0 + 11] && !m.label[RFM_MOVE_E_K1][0]);
    button(&m, MR_CC_REC, 1), ms(&m, 400);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_LABEL && left_lit(out) > 20);
    sheet_add(out, "rec_static");
    pad(&m, 16, 100);
    CHECK(m.pop_i == RFM_MOVE_LBL_BTN0 + 11);
    pad(&m, 16, 0), button(&m, MR_CC_REC, 0);
    sheet_write();
}

/* (0.5.7) readouts: a knob's kept (its touch: what it controls now and the last value; turned: live, no detents),
 * paired with the gesture in progress, or a popup of their own (a preset's name, a message); a contact sheet */
static void test_readouts(void)
{
    static rfm_move_t m;
    static uint8_t out[MR_SCREEN_BYTES], out2[MR_SCREEN_BYTES];
    const char *lbl[RFM_MOVE_LBL_N] = { NULL };
    g_sheet_pre = "readouts", g_nsheet = 0;
    rfm_move_init(&m, NULL, on_act, NULL);
    m.s.pop_half = 1;                                   /* (0.5.9: the left half; the whole screen: test_popup_width) */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    m.pop = POP_NONE;
    lbl[RFM_MOVE_LBL_KNOB0 + 3] = "K1";
    rfm_move_set_labels(&m, lbl);
    /* no readout yet: 0.5.6's touch (the name, no value) */
    touch(&m, 4, 1);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_ENC && m.pop_blank && lit_in(out, 0, 26, 64, 64) == 0);
    /* the firmware's readout while touched (control -1: paired with K1): drawn in place */
    rfm_move_readout(&m, -1, RFM_MOVE_RD_PARAM, "Cutoff", "1.2k", 400, 0, 0);
    CHECK(m.rd[3].on && !strcmp(m.rd[3].title, "Cutoff") && m.pop == POP_ENC);
    rfm_move_screen(&m, NULL, out);
    CHECK(lit_in(out, 0, 3, 64, 19) > 10 && lit_in(out, 0, 23, 64, 55) > 40 && lit_in(out, 1, 61, 25, 63) == 2 * 24 &&
          lit_in(out, 26, 61, 63, 63) == 0);            /* (the title, the value at 4x, the bar 40 %) */
    sheet_add(out, "knob_touched");
    touch(&m, 4, 0);
    ms(&m, 3000);
    /* a later touch: what it controls and the last value */
    touch(&m, 4, 1);
    rfm_move_screen(&m, NULL, out2);
    CHECK(m.pop == POP_ENC && !memcmp(out, out2, sizeof out));
    /* turned: the detents give way to the readout; a new one updates it live */
    knob(&m, 4, 4);
    rfm_move_screen(&m, NULL, out2);
    CHECK(!memcmp(out, out2, sizeof out));
    rfm_move_readout(&m, 3, RFM_MOVE_RD_PARAM, "Cutoff", "2.4 kHz", 600, 0, 0);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_ENC && lit_in(out, 1, 61, 39, 63) == 2 * 37);   /* ("2.4 kHz": two 2x rows... or 3x) */
    sheet_add(out, "knob_turned");
    rfm_move_readout(&m, 3, RFM_MOVE_RD_PARAM, "Filter Resonance", "-12", 250, RFM_MOVE_RD_BIPOLAR, 0);
    rfm_move_screen(&m, NULL, out);
    CHECK(lit_in(out, 16, 61, 33, 63) > 0 && lit_in(out, 34, 61, 63, 63) == 0);   /* (from the middle, left) */
    sheet_add(out, "knob_bipolar");
    ms(&m, 1500);
    rfm_move_screen(&m, NULL, out2);
    CHECK(memcmp(out, out2, sizeof out) != 0);          /* (the long title scrolled) */
    touch(&m, 4, 0);

    /* nothing in use: a popup of its own, for its time (a preset's name; ms 2000) */
    ms(&m, 3000);
    rfm_move_readout(&m, -1, RFM_MOVE_RD_PRESET, "Preset", "Warm Pad", -1, 0, 2000);
    CHECK(m.pop == POP_READ && rfm_move_screen_timed(&m));
    rfm_move_screen(&m, NULL, out);
    CHECK(lit_in(out, 0, 3, 64, 19) > 10 && lit_in(out, 0, 23, 64, 60) > 20 && lit_in(out, 0, 61, 64, 63) == 0);
    sheet_add(out, "preset");
    ms(&m, 1500);
    CHECK(rfmv_pop_up(&m, POP_READ));
    ms(&m, 600);
    CHECK(!rfmv_pop_up(&m, POP_READ));
    rfm_move_readout(&m, -1, RFM_MOVE_RD_PRESET, "Preset", "A Very Long Patch Name", -1, 0, 0);
    rfm_move_screen(&m, NULL, out);
    sheet_add(out, "preset_long");
    rfm_move_readout(&m, -1, RFM_MOVE_RD_MESSAGE, "Pattern saved", NULL, -1, 0, 0);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_READ && lit_in(out, 0, 0, 64, 64) > 40);
    sheet_add(out, "message");
    ms(&m, RFM_MOVE_POPUP_MS + 10);
    CHECK(!rfmv_pop_up(&m, POP_READ));
    /* a knob's readout with its popup down: kept, and shown on its own */
    rfm_move_readout(&m, 4, RFM_MOVE_RD_PARAM, "Decay", "87", 870, 0, 0);
    CHECK(m.pop == POP_READ && m.rd[4].on && !strcmp(m.rd[4].value, "87"));
    rfm_move_screen(&m, NULL, out);
    sheet_add(out, "knob_own");
    ms(&m, RFM_MOVE_POPUP_MS + 10);
    /* pairing: within 250 ms of a turn (no touch) it is the knob's; later, its own */
    knob(&m, 5, 2);                                     /* K2 (FM1P_E_K2 = 4) */
    ms(&m, 100);
    rfm_move_readout(&m, -1, RFM_MOVE_RD_PARAM, "Decay", "90", 900, 0, 0);
    CHECK(m.pop == POP_ENC && !strcmp(m.rd[4].value, "90"));
    ms(&m, 300);
    rfm_move_readout(&m, -1, RFM_MOVE_RD_PARAM, "Decay", "91", 910, 0, 0);
    CHECK(m.pop == POP_READ && !strcmp(m.rd[4].value, "90"));
    ms(&m, RFM_MOVE_POPUP_MS + 10);
    jog(&m, 1);                                         /* the jog: SELECT's (slot 0) */
    rfm_move_readout(&m, -1, RFM_MOVE_RD_PARAM, "Program", "12", -1, 0, 0);
    CHECK(m.pop == POP_SELECT && m.rd[0].on);
    rfm_move_screen(&m, NULL, out);
    sheet_add(out, "jog");
    ms(&m, RFM_MOVE_POPUP_MS + 10);
    /* nothing to show: ignored; another mode: a knob's kept, no popup */
    rfm_move_readout(&m, -1, RFM_MOVE_RD_PARAM, NULL, "", -1, 0, 0);
    CHECK(!rfmv_pop_up(&m, POP_READ));
    rfm_move_set_mode(&m, RFM_MOVE_MODE_MIDI);
    m.pop = POP_NONE;
    rfm_move_readout(&m, 6, RFM_MOVE_RD_PARAM, "Sustain", "64", 500, 0, 0);
    rfm_move_readout(&m, -1, RFM_MOVE_RD_PRESET, "Preset", "Bass", -1, 0, 0);
    CHECK(m.rd[6].on && m.pop == POP_NONE);
    /* a firmware change: forgotten */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    rfm_move_readouts_clear(&m);
    CHECK(!m.rd[3].on && !m.rd[6].on);
    touch(&m, 4, 1);
    CHECK(m.pop_blank);
    touch(&m, 4, 0);
    sheet_write();
}

/* (0.5.8, the owner's) the jog wheel touched: a button's label at once; a label the firmware gives only once the
 * button is pressed (Elasticat) shows while held, live; words never only after the release; a knob's readout always
 * its name then its value (PRESETS too) */
static void test_labels_058(void)
{
    static rfm_move_t m;
    static uint8_t out[MR_SCREEN_BYTES];
    const char *lbl[RFM_MOVE_LBL_N] = { NULL };
    g_sheet_pre = "labels058", g_nsheet = 0;
    rfm_move_init(&m, NULL, on_act, NULL);
    m.s.pop_half = 1;                                   /* (0.5.9: the left half; the whole screen: test_popup_width) */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    m.pop = POP_NONE;
    lbl[RFM_MOVE_LBL_BTN0 + 11] = "Rec arm";            /* REC */
    lbl[RFM_MOVE_LBL_BTN0 + 10] = "";                   /* PLAY: no words until pressed */
    rfm_move_set_labels(&m, lbl);
    /* the jog touched: a press shows the label at once (and acts) */
    touch(&m, 9, 1);
    touch(&m, 4, 1), touch(&m, 4, 0);                   /* (a knob touched meanwhile: the jog still is) */
    CHECK(m.jog_touch);
    m.pop = POP_NONE;
    clear();
    button(&m, MR_CC_REC, 1);
    CHECK(nact == 1 && acts[0].kind == RFM_MOVE_ACT_BTN && acts[0].down);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_LABEL && left_lit(out) > 40 && m.pop_shown);
    sheet_add(out, "jog_rec");
    button(&m, MR_CC_REC, 0);
    CHECK(rfmv_pop_up(&m, POP_LABEL));                  /* (a second after the release) */
    ms(&m, RFM_MOVE_POPUP_MS + 10);
    /* a tap with the jog touched: shown at once, so it stays its second */
    button(&m, MR_CC_REC, 1);
    rfm_move_screen(&m, NULL, out);
    button(&m, MR_CC_REC, 0);
    CHECK(rfmv_pop_up(&m, POP_LABEL));
    ms(&m, RFM_MOVE_POPUP_MS + 10);
    touch(&m, 9, 0);
    CHECK(!m.jog_touch);
    /* the jog not touched: the hold rule */
    button(&m, MR_CC_REC, 1);
    rfm_move_screen(&m, NULL, out);
    CHECK(art_diff(out) == 9);
    ms(&m, RFM_MOVE_LABEL_HOLD_MS + 5);
    rfm_move_screen(&m, NULL, out);
    CHECK(left_lit(out) > 40);
    button(&m, MR_CC_REC, 0);
    ms(&m, RFM_MOVE_POPUP_MS + 10);

    /* Elasticat: PLAY gets words only once pressed: shown while held as they come, live */
    touch(&m, 9, 1);
    button(&m, MR_CC_PLAY, 1);
    rfm_move_screen(&m, NULL, out);
    {                                                   /* (no words yet: the drawing, as with no popup) */
        static uint8_t ref[MR_SCREEN_BYTES];
        const uint8_t was = m.pop;
        m.pop = POP_NONE, rfm_move_screen(&m, NULL, ref), m.pop = was;
        CHECK(m.pop == POP_LABEL && !m.pop_shown && !memcmp(out, ref, sizeof out));
    }
    lbl[RFM_MOVE_LBL_BTN0 + 10] = "Chop";
    rfm_move_set_labels(&m, lbl);
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop_shown && left_lit(out) > 20);
    sheet_add(out, "pressed_named");
    lbl[RFM_MOVE_LBL_BTN0 + 10] = "Slice";              /* (renamed while held: live) */
    rfm_move_set_labels(&m, lbl);
    {
        static uint8_t o2[MR_SCREEN_BYTES];
        rfm_move_screen(&m, NULL, o2);
        CHECK(memcmp(out, o2, sizeof out) != 0);
        sheet_add(o2, "pressed_renamed");
    }
    button(&m, MR_CC_PLAY, 0);
    CHECK(rfmv_pop_up(&m, POP_LABEL));
    ms(&m, RFM_MOVE_POPUP_MS + 10);
    /* words only at the release (the firmware names it as it lets go): nothing after the release */
    lbl[RFM_MOVE_LBL_BTN0 + 10] = "";
    rfm_move_set_labels(&m, lbl);
    button(&m, MR_CC_PLAY, 1);
    rfm_move_screen(&m, NULL, out);
    lbl[RFM_MOVE_LBL_BTN0 + 10] = "Play";
    rfm_move_set_labels(&m, lbl);
    button(&m, MR_CC_PLAY, 0);
    CHECK(m.pop == POP_NONE);
    touch(&m, 9, 0);
    rfm_move_screen(&m, NULL, out);
    CHECK(art_diff(out) == 0);

    /* readouts: every knob the same, the name then the value; PRESETS's preset with no title: its label, the name */
    lbl[RFM_MOVE_LBL_KNOB0 + 2] = "Sound";
    rfm_move_set_labels(&m, lbl);
    rfm_move_readout(&m, 2, RFM_MOVE_RD_PRESET, NULL, "Warm Pad", -1, 0, 0);
    ms(&m, RFM_MOVE_POPUP_MS + 10);
    touch(&m, 1, 1);                                    /* knob 2 (0.8.0): PRESETS */
    rfm_move_screen(&m, NULL, out);
    CHECK(m.pop == POP_ENC && lit_in(out, 0, 3, 64, 19) > 10 && lit_in(out, 0, 23, 64, 60) > 20);
    sheet_add(out, "presets_name");
    touch(&m, 1, 0);
    rfm_move_readout(&m, 2, RFM_MOVE_RD_PRESET, "Preset 12", "Bass", -1, 0, 0);
    rfm_move_screen(&m, NULL, out);                     /* (its own popup, the popup down: the same rule) */
    CHECK(m.pop == POP_READ && lit_in(out, 0, 3, 64, 19) > 10 && lit_in(out, 0, 23, 64, 60) > 20);
    sheet_add(out, "presets_own");
    ms(&m, RFM_MOVE_POPUP_MS + 10);
    rfm_move_readout(&m, 4, RFM_MOVE_RD_MESSAGE, "Decay", "87", 870, 0, 0);
    touch(&m, 5, 1);                                    /* K2: a message kind, still the name then the value */
    rfm_move_screen(&m, NULL, out);
    CHECK(lit_in(out, 0, 23, 64, 56) > 40 && lit_in(out, 1, 61, 63, 63) > 0);
    sheet_add(out, "k2_message_kind");
    touch(&m, 5, 0);
    sheet_write();
}

/* (0.5.9, the owner's) the FM-1 popups (labels, readouts, the knobs') over the whole screen by default; the setting
 * (Global Settings, knob 4; settings version 5) gives the left half back; contact sheets of both */
static void popup_shots(rfm_move_t *m, uint8_t *out, int full)
{
    const char *lbl[RFM_MOVE_LBL_N] = { NULL };
    uint8_t pix[64 * 64];
    int i;
    for (i = 0; i < 64 * 64; i++)                       /* (a thumbnail: a checkerboard, to see it covered or not) */
        m->thumb[i / 64][i % 64] = (uint8_t)((i / 64 / 8 + i % 64 / 8) & 1);
    (void)pix;
    lbl[RFM_MOVE_LBL_KNOB0 + 3] = "Resonance";
    lbl[RFM_MOVE_LBL_BTN0 + 11] = "Rec arm";
    lbl[RFM_MOVE_LBL_BLACK0] = "Clear All Steps";
    lbl[RFM_MOVE_LBL_WHITE0] = "Kick";
    lbl[RFM_MOVE_LBL_BTN0 + 10] = "";
    rfm_move_set_labels(m, lbl);
    rfm_move_readouts_clear(m);
    m->pop = POP_NONE;
    touch(m, 4, 1);                                     /* a knob touched, no readout: its name */
    rfm_move_screen(m, NULL, out);
    CHECKF(full ? lit_in(out, 64, 0, 128, 64) > 20 && lit_in(out, 0, 0, 128, 64) < 1000 : lit_in(out, 64, 0, 128, 64) == 2048,
           "full %d: right %d, all %d", full, lit_in(out, 64, 0, 128, 64), lit_in(out, 0, 0, 128, 64));   /* (whole: no
                                                         * checkerboard; half: the FM-1's screen on the right) */
    sheet_add(out, "knob_name");
    knob(m, 4, 6);                                      /* turned: the detents */
    rfm_move_screen(m, NULL, out);
    sheet_add(out, "knob_detents");
    rfm_move_readout(m, -1, RFM_MOVE_RD_PARAM, "Cutoff", "1.2 kHz", 400, 0, 0);
    rfm_move_screen(m, NULL, out);
    CHECK(full ? lit_in(out, 1, 60, 51, 63) == 3 * 50 : lit_in(out, 1, 61, 25, 63) == 2 * 24);   /* (the bar 40 %) */
    sheet_add(out, "readout");
    rfm_move_readout(m, -1, RFM_MOVE_RD_PARAM, "Pan", "L12", 406, RFM_MOVE_RD_BIPOLAR, 0);
    rfm_move_screen(m, NULL, out);
    sheet_add(out, "bipolar");
    touch(m, 4, 0);
    ms(m, 3000);
    rfm_move_readout(m, -1, RFM_MOVE_RD_PRESET, "Preset 12", "Warm Pad", -1, 0, 0);
    rfm_move_screen(m, NULL, out);
    CHECK(m->pop == POP_READ);
    sheet_add(out, "preset");
    ms(m, 3000);
    rfm_move_readout(m, -1, RFM_MOVE_RD_MESSAGE, "Pattern saved", NULL, -1, 0, 0);
    rfm_move_screen(m, NULL, out);
    sheet_add(out, "message");
    ms(m, 3000);
    touch(m, 9, 1);                                     /* the jog touched: a button's label at once */
    button(m, MR_CC_REC, 1);
    rfm_move_screen(m, NULL, out);
    CHECK(m->pop == POP_LABEL && m->pop_shown);
    sheet_add(out, "button");
    pad(m, 25, 100);                                    /* a key, a button held */
    rfm_move_screen(m, NULL, out);
    sheet_add(out, "key_long");
    pad(m, 25, 0);
    pad(m, 16, 100);
    rfm_move_screen(m, NULL, out);
    sheet_add(out, "key");
    pad(m, 16, 0), button(m, MR_CC_REC, 0);
    ms(m, 3000);
    button(m, MR_CC_PLAY, 1);                           /* no words: the FM-1's screen stays (right half) */
    rfm_move_screen(m, NULL, out);
    CHECK(m->pop == POP_LABEL && !m->pop_shown && lit_in(out, 64, 0, 128, 64) == 2048);
    button(m, MR_CC_PLAY, 0);
    touch(m, 9, 0);
    ms(m, 3000);
}
static void test_popup_width(void)
{
    static rfm_move_t m;
    static uint8_t out[MR_SCREEN_BYTES];
    uint8_t img[RFM_MOVE_SETTINGS_BYTES];
    rfm_move_settings_t st;
    /* the setting: whole screen by default; saved (version 5; 0.6.12: version 6, the speech mode after it); older
     * settings: the default */
    rfm_move_settings_default(&st);
    CHECK(st.pop_half == 0 && st.speech == 0);
    st.pop_half = 1, st.speech = 1 + RFM_MOVE_SPEECH_JOG;
    CHECK(rfm_move_save(&st, img) == RFM_MOVE_SETTINGS_BYTES && img[3] == 7 && img[RFM_MOVE_SETTINGS_BYTES_V6 - 2] == 1 &&
          img[RFM_MOVE_SETTINGS_BYTES_V6 - 1] == 3);   /* (0.8.0: version 7, the slots after them) */
    CHECK(rfm_move_load(&st, img, sizeof img) == 0 && st.pop_half == 1 && st.speech == 3);
    img[3] = 6, img[4] = (uint8_t)RFM_MOVE_SETTINGS_BYTES_V6;   /* (a version 6 image: 88 bytes) */
    CHECK(rfm_move_load(&st, img, RFM_MOVE_SETTINGS_BYTES_V6) == 0 && st.pop_half == 1 && st.speech == 3);
    img[3] = 5, img[4] = (uint8_t)(RFM_MOVE_SETTINGS_BYTES_V6 - 1);   /* (a version 5 image: 87 bytes) */
    CHECK(rfm_move_load(&st, img, RFM_MOVE_SETTINGS_BYTES_V6 - 1) == 0 && st.pop_half == 1 && st.speech == 0);
    img[3] = 4, img[4] = (uint8_t)(RFM_MOVE_SETTINGS_BYTES_V6 - 2);   /* (a version 4 image: 86 bytes) */
    CHECK(rfm_move_load(&st, img, RFM_MOVE_SETTINGS_BYTES_V6 - 2) == 0 && st.pop_half == 0);
    /* Global Settings (Shift + step 2 in Sequencer mode): (0.6.2) its Popup size row (the jog, a click to edit) */
    rfm_move_init(&m, NULL, on_act, NULL);
    rfm_move_set_mode(&m, RFM_MOVE_MODE_SEQ);
    button(&m, MR_CC_SHIFT, 1), step(&m, 1, 1), step(&m, 1, 0), button(&m, MR_CC_SHIFT, 0);
    CHECK(m.seq.smenu == RFM_MOVE_SM_GLOBAL);
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_global_popup_full.pbm", out);
    clear();
    jog(&m, 4), raw(&m, 0xB0, MR_CC_JOG_CLICK, 127), raw(&m, 0xB0, MR_CC_JOG_CLICK, 0);
    CHECK(m.gl_row == 4 && m.gl_edit);                  /* (0.6.12: Speech first) */
    jog(&m, 1);
    CHECK(m.s.pop_half == 1 && nact >= 1 && acts[nact - 1].kind == RFM_MOVE_ACT_SETTINGS);
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_global_popup_half.pbm", out);
    jog(&m, 1);
    CHECK(m.s.pop_half == 1);                           /* (the end) */
    jog(&m, -1);
    CHECK(m.s.pop_half == 0);
    button(&m, MR_CC_BACK, 1), button(&m, MR_CC_BACK, 0);
    CHECK(!m.gl_edit && m.seq.smenu == RFM_MOVE_SM_GLOBAL);   /* (Back: out of the edit, the menu stays) */
    /* both widths, the same popups */
    rfm_move_set_mode(&m, RFM_MOVE_MODE_FM1);
    m.seq.smenu = 0;
    g_sheet_pre = "popups_full", g_nsheet = 0;
    popup_shots(&m, out, 1);
    sheet_write();
    m.s.pop_half = 1;
    g_sheet_pre = "popups_half", g_nsheet = 0;
    popup_shots(&m, out, 0);
    sheet_write();
}

static void test_host_popup(void)
{
    static rfm_move_t m;
    static uint8_t out[MR_SCREEN_BYTES];
    rfm_move_init(&m, NULL, on_act, NULL);
    rfm_move_set_mode(&m, RFM_MOVE_MODE_MIDI);
    rfm_move_popup(&m, "FM-1 linked");
    CHECK(m.pop == POP_HOST && !strcmp(m.pop_text, "FM-1 linked") && rfm_move_screen_timed(&m));
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_host_popup.pbm", out);
    CHECK(lit_in(out, 0, 8, 128, 30) > 0 && lit_in(out, 0, 30, 128, 50) > 0);   /* (two rows at 2x) */
    rfm_move_popup(&m, "FM-1 lost");
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_host_popup_lost.pbm", out);
    CHECK(lit_in(out, 0, 18, 128, 38) > 0 && lit_in(out, 0, 0, 128, 14) == 0);   /* (one row) */
    m.seq.now_us += (RFM_MOVE_POPUP_MS + 10) * 1000u;
    CHECK(!rfmv_pop_up(&m, POP_HOST) && !rfm_move_screen_timed(&m));
    rfm_move_popup(&m, "\x01\x02");                     /* (nothing printable: no popup) */
    CHECK(!rfmv_pop_up(&m, POP_HOST));
    rfm_move_popup(&m, "A very long message that cannot fit on the screen at all");
    CHECK(strlen(m.pop_text) == 31 && rfmv_pop_up(&m, POP_HOST));
    rfm_move_screen(&m, NULL, out);
    write_pbm("move_host_popup_long.pbm", out);
    /* the projects view: over it too */
    rfm_move_fw_list(&m, NULL, 0);
    m.seq.projects = 1;
    rfm_move_popup(&m, "FM-1 lost");
    rfm_move_screen(&m, NULL, out);
    CHECK(lit_in(out, 0, 18, 128, 38) > 0 && lit_in(out, 0, 0, 128, 14) == 0);
    m.seq.projects = 0;
}

/* (0.5.11) the version's numbers, its string, the library's and CMake's agree */
static void test_version(void)
{
    char v[32];
    snprintf(v, sizeof v, "%d.%d.%d", RFM1CS_VERSION_MAJOR, RFM1CS_VERSION_MINOR, RFM1CS_VERSION_PATCH);
    CHECKF(!strcmp(v, RFM1CS_VERSION), "RFM1CS_VERSION_* %s, RFM1CS_VERSION %s", v, RFM1CS_VERSION);
    CHECK(!strcmp(rfm_move_version(), RFM1CS_VERSION));
}
/* (0.8.0, proposal 2) the Track buttons' taps and double clicks: Track 1..3 a tap plays the slot, a double click its
 * browser; Track 4 a tap FX setup (the Mixer <-> Routing) or the projects, a double click the projects; in the
 * projects view a Track 1..3 tap leaves it; the Sequencer: a tap selects the track locked to the slot, held the lock
 * menu (a step tapped: locked; double: solo; held: not routed) */
static void test_track_gestures(void)
{
    static rfm_move_t m;
    int s;
    rfm_move_init(&m, NULL, on_act, NULL);
    /* no firmware list: no browser; Track 4: the projects grid */
    dbl(&m, 0);
    CHECK(!m.seq.projects && m.s.mode == RFM_MOVE_MODE_FM1);
    tap(&m, 3);
    CHECK(m.seq.projects && m.fw.track == 3 && !m.fw.fx);
    back(&m);
    CHECK(!m.seq.projects && m.s.mode == RFM_MOVE_MODE_FM1);
    dbl(&m, 3);
    CHECK(m.seq.projects && m.fw.track == 3 && !m.fw.fx);
    back(&m);
    CHECK(!m.seq.projects);
    /* two firmwares (FX on): Track 2's double click: slot 2's browser, it is played */
    two_slots(&m);
    rfm_move_set_slot_layout(&m, 1, RFM_MOVE_LAY_SCALE);
    clear();
    dbl(&m, 1);
    CHECK(m.seq.projects && !m.fw.fx && m.fw.track == 1 && rfm_move_fw_active(&m) == 1 &&
          count(RFM_MOVE_ACT_FW_ACTIVE) == 1);
    clear();                                            /* (0.9.0) a firmware's pad tapped twice at once: loaded into
                                                         * the slot (as the jog's press), the browser stays */
    pad(&m, 24, 100), pad(&m, 24, 0);
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 0);
    pad(&m, 24, 100), pad(&m, 24, 0);
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 1 && m.seq.projects);
    for (s = 0; s < nact; s++)
        if (acts[s].kind == RFM_MOVE_ACT_FW_LOAD)
            CHECK(acts[s].slot == 1);
    clear();
    gap(), pad(&m, 24, 100), pad(&m, 24, 0), gap(), pad(&m, 24, 100), pad(&m, 24, 0);   /* (apart: no load) */
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 0 && m.seq.projects);
    tap(&m, 0);                                         /* (a Track 1..3 tap in the view: out, slot 1 played) */
    CHECK(!m.seq.projects && rfm_move_fw_active(&m) == 0 && m.s.mode == RFM_MOVE_MODE_FM1 && m.pop == POP_LAYOUT &&
          m.pop_i == 0);
    tap(&m, 1);                                         /* slot 2's Scale layout (0.9.0: the sequencer, its track 2) */
    CHECK(!m.seq.projects && rfm_move_fw_active(&m) == 1 && m.s.mode == RFM_MOVE_MODE_SEQ && m.pop == POP_LAYOUT &&
          m.pop_i == 1 && m.seq.sel_track == 1 && (m.seq.fx.ffilt[1] >> 1 & 1));
    tap(&m, 2);                                         /* (an empty slot: active, its FM-1 layout) */
    CHECK(rfm_move_fw_active(&m) == 2 && m.s.mode == RFM_MOVE_MODE_FM1);
    tap(&m, 0);
    /* Track 4: FX setup's Mixer, a tap there Routing, again the Mixer; its double click the projects */
    tap(&m, 3);
    CHECK(m.seq.projects && m.fw.fx && m.s.fx_page == RFM_MOVE_FX_PAGE_MIX && m.pop == POP_T4 && m.pop_i == 0);
    tap(&m, 3);
    CHECK(m.seq.projects && m.fw.fx && m.s.fx_page != RFM_MOVE_FX_PAGE_MIX && m.pop == POP_T4 && m.pop_i == 1);
    tap(&m, 3);
    CHECK(m.seq.projects && m.fw.fx && m.s.fx_page == RFM_MOVE_FX_PAGE_MIX);
    tap(&m, 0);
    CHECK(!m.seq.projects && rfm_move_fw_active(&m) == 0);
    tap(&m, 3);                                         /* (the one last used: the Mixer) */
    CHECK(m.seq.projects && m.fw.fx && m.s.fx_page == RFM_MOVE_FX_PAGE_MIX);
    back(&m);
    CHECK(!m.seq.projects);
    dbl(&m, 3);
    CHECK(m.seq.projects && !m.fw.fx && m.fw.track == 3);
    back(&m);
    CHECK(!m.seq.projects && m.s.mode == RFM_MOVE_MODE_FM1);
    /* Track 4 held + pad 2: Routing; + pad 1: the Mixer (from a layout: in the view the pads are the view's) */
    track(&m, 3, 1), pad(&m, 25, 100), pad(&m, 25, 0), track(&m, 3, 0);
    CHECK(m.seq.projects && m.fw.fx && m.s.fx_page != RFM_MOVE_FX_PAGE_MIX);
    back(&m);
    CHECK(!m.seq.projects);
    track(&m, 3, 1), pad(&m, 24, 100), pad(&m, 24, 0), track(&m, 3, 0);
    CHECK(m.seq.projects && m.fw.fx && m.s.fx_page == RFM_MOVE_FX_PAGE_MIX);
    back(&m);
    CHECK(!m.seq.projects);
    /* (0.9.0) Back with nothing open: nothing. Track 2's tap: slot 2's note layout, its selected track (2) */
    back(&m);
    CHECK(m.s.mode == RFM_MOVE_MODE_FM1 && rfm_move_fw_active(&m) == 0);
    gap();
    tap(&m, 1);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && rfm_move_fw_active(&m) == 1 && m.seq.sel_track == 1 && m.pop == POP_LAYOUT &&
          m.pop_i == 1);
    /* the Track menu's steps: Track 2 held + step 6 tapped: track 6 selected for slot 2 and routed to it; slot 2 is
     * played: the track edited now */
    track(&m, 1, 1);
    clear();
    step(&m, 5, 1), step(&m, 5, 0);
    CHECK(rfm_move_seq_lock(&m, 1) != 5);               /* (0.11.0: once the double click's time has passed) */
    ms(&m, RFM_MOVE_DOUBLE_MS + 30);
    CHECK(rfm_move_seq_lock(&m, 1) == 5 && m.seq.sel_track == 5 && (m.seq.fx.ffilt[1] >> 5 & 1) &&
          m.lk_word == LK_LOCKED && count(RFM_MOVE_ACT_SETTINGS) >= 1 && m.seq.nnotes == 0);
    now_ms += 500;                                      /* (later: no double tap) */
    s = m.seq.fx.ffilt[1];
    step(&m, 7, 1), step(&m, 7, 0), step(&m, 7, 1), step(&m, 7, 0);   /* step 8 twice: track 8 soloed on slot 2 alone
                                                         * (the sequencer's own solo untouched) */
    CHECK(m.fsolo[1] == 8 && m.seq.fx.ffilt[1] == 1u << 7 && m.fsolo_save[1] == s && !m.seq.tr[7].solo &&
          m.seq.fx.ffilt[0] == 0xFFFF && rfm_move_seq_lock(&m, 1) == 5 && m.lk_word == LK_SOLO);   /* (0.11.0: the
                                                         * double click selects nothing: track 6 stays selected) */
    now_ms += 500;
    step(&m, 9, 1), step(&m, 9, 0), step(&m, 9, 1), step(&m, 9, 0);   /* another: the solo moves (one a firmware) */
    CHECK(m.fsolo[1] == 10 && m.seq.fx.ffilt[1] == 1u << 9 && !m.seq.tr[9].solo);
    now_ms += 500;
    step(&m, 9, 1), step(&m, 9, 0), step(&m, 9, 1), step(&m, 9, 0);   /* on the soloed one: off, the filter back */
    CHECKF(!m.fsolo[1] && (m.seq.fx.ffilt[1] & (s | 1u << 7)) == (s | 1u << 7) && m.lk_word == LK_UNSOLO,
           "filter %04x, was %04x", m.seq.fx.ffilt[1], s);
    now_ms += 500;
    s = m.seq.fx.ffilt[1] >> 2 & 1;
    step(&m, 2, 1), ms(&m, RFM_MOVE_SEQ_HOLD_MS + 10), step(&m, 2, 0);   /* step 3 held: its routing to slot 2 */
    CHECK((m.seq.fx.ffilt[1] >> 2 & 1) == !s && (m.seq.fx.ffilt[0] >> 2 & 1) == 1 && rfm_move_seq_lock(&m, 1) == 5);
    now_ms += 500;
    step(&m, 4, 1), step(&m, 4, 0), step(&m, 4, 1), step(&m, 4, 0);   /* a solo, then a hold on another: the solo ends
                                                         * first, then that track's routing flips */
    CHECK(m.fsolo[1] == 5);
    now_ms += 500;
    s = m.seq.fx.ffilt[1];
    step(&m, 11, 1), ms(&m, RFM_MOVE_SEQ_HOLD_MS + 10), step(&m, 11, 0);
    CHECKF(!m.fsolo[1] && m.seq.fx.ffilt[1] == (m.fsolo_save[1] ^ 1u << 11), "filter %04x, saved %04x (solo %04x)",
           m.seq.fx.ffilt[1], m.fsolo_save[1], s);
    now_ms += 400, track(&m, 1, 0);
    CHECK(m.s.mode == RFM_MOVE_MODE_SEQ && rfm_move_fw_active(&m) == 1);
    gap();
    tap(&m, 0);                                         /* Track 1: slot 1's FM-1 layout */
    CHECK(rfm_move_fw_active(&m) == 0 && m.s.mode == RFM_MOVE_MODE_FM1);
    tap(&m, 1);                                         /* Track 2: its track (6, the one clicked) now */
    CHECK(rfm_move_seq_lock(&m, 1) == 5 && m.seq.sel_track == 5 && rfm_move_fw_active(&m) == 1 && m.s.mode == RFM_MOVE_MODE_SEQ);
    /* the settings keep the locks and the slots' layouts (version 7) */
    {
        rfm_move_settings_t st;
        uint8_t img[RFM_MOVE_SETTINGS_BYTES];
        CHECK(rfm_move_save(&m.s, img) == RFM_MOVE_SETTINGS_BYTES && rfm_move_load(&st, img, sizeof img) == 0);
        CHECK(st.lock[1] == 5 && st.lock[0] == 0 && st.slot[1].lay == RFM_MOVE_LAY_SCALE && st.slot[0].lay == RFM_MOVE_LAY_FM1);
    }
}

int main(void)
{
    test_version();
    test_fm1_pads();
    test_steps();
    test_buttons_knobs();
    test_modes();
    test_layouts();
    test_track4_pads();
    test_latch();
    test_latch_slots();
    test_host_popup();
    test_labels();
    test_readouts();
    test_labels_058();
    test_popup_width();
    test_drum_settings();
    test_control();
    test_leds();
    test_screen();
    test_threshold();
    test_midi_round();
    test_fm1_shift_layer();
    test_mode_name_first();
    test_track1_anywhere();
    test_velocity();
    test_mode_only_by_tap();
    test_menus_leave_pads();
    test_menu_knob_leds();
    test_track_gestures();
    return check_done("move");
}
