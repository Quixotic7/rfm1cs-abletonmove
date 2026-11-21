/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* 0.8.0: the Track buttons, proposal 2 (README "The Track buttons"; the owner's board
 * remoteFM14Move/docs/design/track-buttons-v2.movedesign.json). Track 1..3 the firmware slots (tap: play it in its own
 * layout; hold: its layout menu; double click: its browser), Track 4 the sequencer side (tap: the Mixer <-> Routing;
 * hold: Mixer / Routing pads and the Shift shortcuts; double click: the projects), Back flips the layouts and the
 * Sequencer, the Sequencer's lock menu, the FM-1 layout's knobs and jog, the note layouts' CCs, settings version 7.
 * Writes tracks_*.pbm (the screens) into the working directory; tools/tracks_board.py compares them with the board. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "move_int.h"
#include "moveremote/client.h"
#include "rfm1cs/move.h"
#include "move_fwb_data.h"

#define NFW ((int)(sizeof FWB / sizeof FWB[0]))

/* ---- the actions, logged */
static rfm_move_act_t A[512];
static int nact;
static char said[4096];
static void on_act(void *ctx, const rfm_move_act_t *a)
{
    (void)ctx;
    if (a->kind == RFM_MOVE_ACT_SAY) {
        snprintf(said + strlen(said), sizeof said - strlen(said), "%c:%s|", a->index == RFM_MOVE_SAY_EVENT ? 'E' : 'V',
                 (const char *)a->data);
        return;
    }
    if (nact < (int)(sizeof A / sizeof A[0]))
        A[nact] = *a, A[nact].data = NULL, nact++;
}
static void clear(void) { nact = 0, said[0] = 0; }
static int count(int kind)
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += A[i].kind == kind;
    return n;
}
static const rfm_move_act_t *find(int kind)
{
    int i;
    for (i = 0; i < nact; i++)
        if (A[i].kind == kind)
            return &A[i];
    return NULL;
}

/* ---- the fake Move */
static rfm_move_t M;
static uint32_t now_ms;
static void raw(uint8_t st, uint8_t d1, uint8_t d2)
{
    const uint8_t pkt[4] = { (uint8_t)(st >> 4), st, d1, d2 };
    mr_event_t ev;
    mr_decode(pkt, 0, &ev);
    rfm_move_input(&M, &ev, now_ms += 5);
}
static void adv_ms(uint32_t ms)
{
    while (ms) {
        const uint32_t k = ms < 10 ? ms : 10;
        rfm_move_seq_advance(&M, k * 48, 48000);
        now_ms += k, ms -= k;
    }
}
static void pad(int p, int vel) { raw(vel ? 0x90 : 0x80, (uint8_t)(MR_NOTE_PAD0 + p), (uint8_t)vel); }
static void hit(int p) { pad(p, 100), pad(p, 0); }
static void step(int s, int down) { raw(down ? 0x90 : 0x80, (uint8_t)(MR_NOTE_STEP0 + s), down ? 127 : 0); }
static void button(int cc, int down) { raw(0xB0, (uint8_t)cc, down ? 127 : 0); }
static void press(int cc) { button(cc, 1), button(cc, 0); }
static void knob(int k, int d) { raw(0xB0, (uint8_t)(MR_CC_KNOB1 + k), (uint8_t)(d & 127)); }
static void jog(int d) { raw(0xB0, MR_CC_JOG, (uint8_t)(d & 127)); }
static void jog_click(void) { raw(0xB0, MR_CC_JOG_CLICK, 127), raw(0xB0, MR_CC_JOG_CLICK, 0); }
static void track(int t, int down) { button(MR_CC_TRACK1 - t, down); }
static void tap_track(int t) { track(t, 1), track(t, 0); }
static void hold_track(int t) { track(t, 1), adv_ms(RFM_MOVE_TAP_MS + 20); }
static void dbl_track(int t) { tap_track(t), now_ms += 50, tap_track(t); }
static void rest(void) { adv_ms(RFM_MOVE_POPUP_MS + 50), now_ms += RFM_MOVE_DOUBLE_MS; }
static void back(void) { press(MR_CC_BACK); }

static rfm_move_fw_t E[NFW];
static void fresh_with(const rfm_move_settings_t *s, int list)
{
    static const char *const ids[RFM_MOVE_FW_SLOTS] = { "choralroot", "felucca", "" };
    int i;
    for (i = 0; i < NFW; i++) {
        E[i].id = FWB[i].id, E[i].name = FWB[i].name, E[i].version = FWB[i].version, E[i].installed = FWB[i].installed;
        E[i].desc = FWB[i].desc, E[i].thumb = FWB[i].thumb, E[i].cats = FWB[i].cats, E[i].cat = FWB[i].cat;
    }
    now_ms = 1000;
    rfm_move_init(&M, s, on_act, NULL);
    if (list) {
        rfm_move_fw_list(&M, E, NFW);
        rfm_move_fw_slots(&M, ids);
    }
    rest();
    clear();
}
static void fresh(void) { fresh_with(NULL, 1); }

/* ---- the screen */
static uint8_t SCR[MR_SCREEN_BYTES];
static void screen(void) { rfm_move_screen(&M, NULL, SCR); }
static int px(int x, int y) { return SCR[(y >> 3) * 128 + x] >> (y & 7) & 1; }
static void write_pbm(const char *name)
{
    char path[96];
    FILE *f;
    int x, y;
    screen();
    snprintf(path, sizeof path, "tracks_%s.pbm", name);
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "P1\n128 64\n");
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++)
            fputc(px(x, y) ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
}
static int lit_rows(int y0, int y1)                     /* pixels on in rows [y0, y1) */
{
    int x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = 0; x < 128; x++)
            n += px(x, y);
    return n;
}
static void leds(uint8_t w[RFM_MOVE_NLEDS][3])
{
    rfm_move_led_t o[RFM_MOVE_NLEDS];
    int i, n;
    rfm_move_leds_invalidate(&M);
    n = rfm_move_leds(&M, o, RFM_MOVE_NLEDS);
    for (i = 0; i < n; i++)
        w[i][0] = o[i].r, w[i][1] = o[i].g, w[i][2] = o[i].b;
}
static int bright(const uint8_t c[3]) { return c[0] + c[1] + c[2]; }

/* ================================================================ Track 1..3: tap, hold, double click === */
static void test_tap(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    fresh();
    CHECK(M.fw.active == 0 && M.s.mode == RFM_MOVE_MODE_FM1);
    for (int t = 0; t < 3; t++)
        CHECK(M.s.slot[t].lay == RFM_MOVE_LAY_FM1 && M.s.lock[t] == t && M.s.slot[t].cc[0] == 16 && M.s.slot[t].cc[7] == 23);
    tap_track(1);                                       /* slot 2: played, FW_ACTIVE, named */
    CHECK(M.fw.active == 1 && count(RFM_MOVE_ACT_FW_ACTIVE) == 1 && find(RFM_MOVE_ACT_FW_ACTIVE)->slot == 1);
    CHECK(M.pop == POP_LAYOUT && M.pop_i == 1 && rfm_move_mode(&M) == RFM_MOVE_MODE_FM1);
    write_pbm("tap_fm1");
    CHECK(lit_rows(0, 16) > 0 && lit_rows(60, 64) > 0); /* (the name on top, the six dashes) */
    leds(w);
    CHECK(bright(w[L_TRACK0 + 1]) > 3 * bright(w[L_TRACK0 + 0]) && bright(w[L_TRACK0 + 3]) > 0);
    rest(), clear();
    tap_track(1);                                       /* again: no FW_ACTIVE, named again */
    CHECK(count(RFM_MOVE_ACT_FW_ACTIVE) == 0 && M.pop == POP_LAYOUT && !M.seq.projects);
    rest(), clear();
    tap_track(2);                                       /* an empty slot: played too (faint white) */
    CHECK(M.fw.active == 2 && count(RFM_MOVE_ACT_FW_ACTIVE) == 1);
    leds(w);
    CHECK(w[L_TRACK0 + 2][0] == w[L_TRACK0 + 2][1] && w[L_TRACK0 + 2][1] == w[L_TRACK0 + 2][2] && w[L_TRACK0 + 2][0] > 0);
    rest(), clear();
    track(0, 1), adv_ms(RFM_MOVE_TAP_MS + 20), track(0, 0);   /* a hold alone: no switch */
    CHECK(M.fw.active == 2 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0);

    fresh_with(NULL, 0);                                /* no firmware list: one slot, Tracks 2, 3 nothing */
    tap_track(1);
    CHECK(M.fw.active == 0 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0);
    dbl_track(0);
    CHECK(!M.seq.projects);
}

static void test_layout_menu(void)
{
    const rfm_move_act_t *a;
    uint8_t w[RFM_MOVE_NLEDS][3];
    fresh();
    hold_track(1);                                      /* Track 2 held: Felucca's layout menu */
    write_pbm("menu_fm1");
    leds(w);
    CHECK(w[L_PAD0 + 24][0] == 255 && w[L_PAD0 + 24][2] == 255);   /* FM-1 white, full */
    CHECK(!bright(w[L_PAD0 + 25]));                     /* no takeover: pad 2 dark */
    CHECK(bright(w[L_PAD0 + 26]) > 0 && bright(w[L_PAD0 + 26]) < bright(w[L_PAD0 + 24]));
    CHECK(!bright(w[L_RING0]));                         /* the FM-1 layout: no CC knobs */
    knob(0, 3);                                         /* .. so a knob does nothing */
    CHECK(M.s.slot[1].cc[0] == 16 && count(RFM_MOVE_ACT_MIDI) == 0);
    hit(28);                                            /* Drum */
    CHECK(M.s.slot[1].lay == RFM_MOVE_LAY_DRUM && M.fw.active == 0 && M.s.mode == RFM_MOVE_MODE_FM1);
    write_pbm("menu_drum");
    step(1, 1), step(1, 0);                             /* channel 2 */
    hit(1);                                             /* scale row 2 pad 2: Minor */
    CHECK(M.s.slot[1].channel == 1 && M.s.slot[1].scale == rfmv_scale_of_pad(1));
    write_pbm("menu_scale");
    knob(0, 58);                                        /* a note layout: knob 1's CC number */
    CHECK(M.s.slot[1].cc[0] == 74 && M.menu_what == MENU_CC);
    write_pbm("menu_cc74");
    jog(2);                                             /* the root */
    button(MR_CC_UP, 1), button(MR_CC_UP, 0);           /* the octave */
    CHECK(M.s.slot[1].root == 2 && M.s.slot[1].octave == 1);
    CHECK(M.s.slot[0].lay == RFM_MOVE_LAY_FM1 && M.s.slot[0].channel == 0 && M.s.slot[0].root == 0 && M.s.slot[0].cc[0] == 16);
    track(1, 0);                                        /* used: no tap */
    CHECK(M.fw.active == 0);
    rest(), clear();
    tap_track(1);                                       /* Felucca in its Drum layout, its own settings */
    CHECK(M.fw.active == 1 && M.s.mode == RFM_MOVE_MODE_MIDI && M.s.layout == RFM_MOVE_LAYOUT_DRUM);
    CHECK(M.s.channel == 1 && M.s.root == 2 && M.s.octave == 1 && M.s.scale == rfmv_scale_of_pad(1));
    write_pbm("tap_drum");
    rest(), clear();
    pad(0, 100);                                        /* a note on channel 2 */
    a = find(RFM_MOVE_ACT_MIDI);
    CHECK(a && a->bytes[0] == 0x91 && a->bytes[1] == M.s.drum[0] + 12);
    pad(0, 0), clear();
    knob(0, 1);                                         /* knob 1: CC 74 on channel 2 */
    a = find(RFM_MOVE_ACT_MIDI);
    CHECK(a && a->bytes[0] == 0xB1 && a->bytes[1] == 74 && a->bytes[2] == 65 && M.pop == POP_CC);
    clear();
    button(MR_CC_SHIFT, 1), knob(1, 2), button(MR_CC_SHIFT, 0);   /* Shift + knob 2: its CC number */
    CHECK(M.s.slot[1].cc[1] == 19 && count(RFM_MOVE_ACT_MIDI) == 0 && M.pop == POP_CCNUM);
    write_pbm("shift_ccnum");
    rest(), clear();
    press(MR_CC_CAPTURE), press(MR_CC_LEFT), press(MR_CC_MUTE);   /* a note layout: no FM-1 buttons */
    CHECK(count(RFM_MOVE_ACT_BTN) == 0);
    button(MR_CC_DOWN, 1), button(MR_CC_DOWN, 0);       /* Down: the octave (the slot's) */
    CHECK(M.s.octave == 0 && M.s.slot[1].octave == 0);
    press(MR_CC_PLAY);                                  /* Play: the sequencer's transport */
    CHECK(M.seq.playing || M.seq.start_pending);
    press(MR_CC_PLAY);
    CHECK(!M.seq.playing && !M.seq.start_pending);
    tap_track(0);                                       /* back to ChoralRoot: FM-1, its own settings */
    CHECK(M.fw.active == 0 && M.s.mode == RFM_MOVE_MODE_FM1 && M.s.channel == 0 && M.s.root == 0);
    CHECK(M.s.slot[1].lay == RFM_MOVE_LAY_DRUM && M.s.slot[1].channel == 1);

    rest(), hold_track(0), hit(31), track(0, 0);        /* pad 8: latch */
    CHECK(rfm_move_latch(&M));
    hold_track(0), hit(31), track(0, 0);
    CHECK(!rfm_move_latch(&M));
}

static void test_double(void)
{
    fresh();
    dbl_track(1);                                       /* the browser on slot 2 */
    CHECK(M.seq.projects && !M.fw.fx && M.fw.track == 1 && M.fw.active == 1);
    write_pbm("browser");
    back(), back(), back();                             /* out, to the slot's layout */
    CHECK(!M.seq.projects && M.s.mode == RFM_MOVE_MODE_FM1 && M.fw.active == 1);
    rest(), clear();
    dbl_track(0);
    CHECK(M.seq.projects && M.fw.track == 0);
    tap_track(1);                                       /* a tap in the view: out, slot 2 played */
    CHECK(!M.seq.projects && M.fw.active == 1);
    rest();
    dbl_track(3);                                       /* Track 4: the projects */
    CHECK(M.seq.projects && !M.fw.fx && M.fw.track == 3);
}

/* ================================================================ Track 4 === */
static void test_track4(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    fresh();
    tap_track(3);                                       /* the Mixer (the default) */
    CHECK(M.seq.projects && M.fw.fx && M.s.fx_page == RFM_MOVE_FX_PAGE_MIX && M.pop == POP_T4 && M.pop_i == 0);
    write_pbm("t4_mixer_pop");
    rest();
    tap_track(3);                                       /* Routing */
    CHECK(M.seq.projects && M.fw.fx && M.s.fx_page == RFM_MOVE_FX_PAGE_ROUTE && M.pop_i == 1);
    rest();
    back();                                             /* out */
    CHECK(!M.seq.projects);
    rest();
    tap_track(3);                                       /* remembered: Routing */
    CHECK(M.seq.projects && M.s.fx_page == RFM_MOVE_FX_PAGE_ROUTE);
    tap_track(0);                                       /* a firmware Track: out, played */
    CHECK(!M.seq.projects && M.fw.active == 0);
    rest(), clear();
    hold_track(3);                                      /* Track 4 held: its menu */
    write_pbm("t4_menu");
    leds(w);
    CHECK(w[L_PAD0 + 25][0] == 255 && w[L_PAD0 + 25][1] == 120);   /* Routing (the one its tap opens) full */
    CHECK(w[L_PAD0 + 24][0] > 0 && w[L_PAD0 + 24][0] < 255);
    CHECK(bright(w[L_STEP0 + 4]) > 0 && w[L_ICON0 + 4][0] > 0);    /* step 5 (Tempo): a shortcut */
    step(4, 1), step(4, 0);                             /* Tempo: the Shift menu */
    CHECK(M.seq.smenu == RFM_MOVE_SM_TEMPO);
    hit(24);                                            /* pad 1: the Mixer */
    CHECK(M.seq.projects && M.fw.fx && M.s.fx_page == RFM_MOVE_FX_PAGE_MIX);
    track(3, 0);
    CHECK(M.seq.projects && M.s.fx_page == RFM_MOVE_FX_PAGE_MIX);   /* (used: no flip) */

    fresh();                                            /* one firmware, no FX: Track 4 is the projects */
    rfm_move_fw_caps(&M, 0);
    tap_track(3);
    CHECK(M.seq.projects && !M.fw.fx && M.fw.track == 3);
}

/* ================================================================ Back, the Sequencer, the lock menu === */
static void test_seq(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    fresh();
    rfm_move_set_slot_layout(&M, 0, RFM_MOVE_LAY_SCALE);
    CHECK(M.s.mode == RFM_MOVE_MODE_MIDI);
    back();                                             /* the Sequencer */
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.pop == POP_SEQ);
    rest();
    back();                                             /* the slot's layout again */
    CHECK(M.s.mode == RFM_MOVE_MODE_MIDI && M.pop == POP_LAYOUT && M.s.slot[0].lay == RFM_MOVE_LAY_SCALE);
    rest(), back(), rest(), clear();
    tap_track(1);                                       /* the Sequencer: Track 2 selects track 2 (locked to it) */
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.seq.sel_track == 1 && M.fw.active == 1 && M.pop == POP_LOCK);
    rest();
    hold_track(1);                                      /* the lock menu */
    write_pbm("lock_menu");
    step(5, 1), step(5, 0);                             /* tap: track 6 locked to Felucca, selected */
    CHECK(M.s.lock[1] == 5 && M.seq.sel_track == 5 && M.lk_word == LK_LOCKED);
    step(6, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20), step(6, 0);   /* hold: track 7 not routed to Felucca */
    CHECK(!(M.seq.fx.ffilt[1] >> 6 & 1) && (M.seq.fx.ffilt[0] >> 6 & 1) && M.lk_word == LK_OFF);
    write_pbm("lock_notrouted");
    now_ms += 500;
    step(2, 1), step(2, 0), step(2, 1), step(2, 0);     /* double tap: track 3 solo (its first tap locked it) */
    CHECK(M.seq.tr[2].solo && M.lk_word == LK_SOLO && M.s.lock[1] == 2);
    now_ms += 500;
    step(5, 1), step(5, 0);                             /* track 6 locked again */
    leds(w);
    CHECK(w[L_STEP0 + 2][2] == 255 && w[L_STEP0 + 2][0] == 40);   /* soloed: blue */
    CHECK(w[L_STEP0 + 6][0] > 60 && w[L_STEP0 + 6][2] < w[L_STEP0 + 6][0] / 4 && w[L_STEP0 + 6][0] < 255);   /* not
                                                         * routed: dim red */
    CHECK(bright(w[L_TRACK0 + 1]) > bright(w[L_TRACK0 + 0]));   /* the held firmware's Track lit */
    clear(), hit(10);                                   /* the pads do nothing while held */
    CHECK(count(RFM_MOVE_ACT_MIDI) == 0);
    track(1, 0);
    rest(), clear();
    tap_track(0);                                       /* Track 1: its own locked track (track 1) */
    CHECK(M.seq.sel_track == 0 && M.fw.active == 0);
    rest();
    tap_track(1);                                       /* Track 2: track 6 now */
    CHECK(M.seq.sel_track == 5);
}

/* ================================================================ the FM-1 layout's knobs and jog === */
static void test_fm1(void)
{
    const rfm_move_act_t *a;
    fresh();
    CHECK(rfm_move_knob_enc(0) == RFM_MOVE_E_SELECT && rfm_move_knob_enc(1) == RFM_MOVE_E_PRESETS &&
          rfm_move_knob_enc(2) == RFM_MOVE_E_ALGO && rfm_move_knob_enc(3) == -1 && rfm_move_knob_enc(4) == RFM_MOVE_E_K1);
    knob(0, 2);                                         /* knob 1: SELECT (a detent every 2 ticks) */
    a = find(RFM_MOVE_ACT_TURN);
    CHECK(a && a->index == RFM_MOVE_E_SELECT && a->value == 1);
    clear(), knob(3, 4);                                /* knob 4: nothing */
    CHECK(count(RFM_MOVE_ACT_TURN) == 0);
    clear(), jog(1);                                    /* the jog: SELECT */
    a = find(RFM_MOVE_ACT_TURN);
    CHECK(a && a->index == RFM_MOVE_E_SELECT && a->value == 1);
    rest(), clear(), jog_click();                       /* PRESET */
    CHECK(M.jog_enc == 1 && M.pop == POP_JOG && M.pop_i == 1);
    write_pbm("jog_preset");
    jog(-1);
    a = find(RFM_MOVE_ACT_TURN);
    CHECK(a && a->index == RFM_MOVE_E_PRESETS && a->value == -1);
    rest(), jog_click(), clear(), jog(1);               /* ALGO */
    a = find(RFM_MOVE_ACT_TURN);
    CHECK(M.jog_enc == 2 && a && a->index == RFM_MOVE_E_ALGO);
    jog_click();
    CHECK(M.jog_enc == 0);
    clear(), press(MR_CC_SHIFT);                        /* Shift: the FM-1's GLO */
    a = find(RFM_MOVE_ACT_BTN);
    CHECK(a && a->index == RFM_MOVE_B_GLO);
}

/* ================================================================ the takeover on pad 2 === */
static void test_takeover(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    fresh();
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_ALL & ~(RFM_MOVE_TK_TRACKS | RFM_MOVE_TK_VOLUME), "CHORDS");
    hold_track(0);
    leds(w);
    CHECK(w[L_PAD0 + 25][0] > 0 && w[L_PAD0 + 25][2] > 0);   /* pad 2: the takeover (purple, dim) */
    hit(25);
    CHECK(M.s.slot[0].lay == RFM_MOVE_LAY_TK && rfm_move_mode(&M) == RFM_MOVE_MODE_TAKEOVER);
    write_pbm("menu_chords");
    clear(), hit(26);                                   /* the menu takes the pads, not the firmware */
    CHECK(count(RFM_MOVE_ACT_TK_INPUT) == 0 && M.s.slot[0].lay == RFM_MOVE_LAY_SCALE);
    hit(25);
    track(0, 0);
    CHECK(rfm_move_tk_shown(&M) == 0);
    clear(), hit(3);                                    /* now the firmware's */
    CHECK(count(RFM_MOVE_ACT_TK_INPUT) == 2);
    rest(), tap_track(1);                               /* another slot: the takeover closes; back: open again */
    CHECK(rfm_move_tk_shown(&M) == -1);
    rest(), tap_track(0);
    CHECK(rfm_move_tk_shown(&M) == 0);
    rfm_move_set_takeover(&M, 0, 0, NULL);              /* no takeover any more: the FM-1 layout */
    CHECK(M.s.slot[0].lay == RFM_MOVE_LAY_FM1 && rfm_move_set_slot_layout(&M, 0, RFM_MOVE_LAY_TK) == -1);
}

/* ================================================================ settings version 7 === */
static void test_settings(void)
{
    rfm_move_settings_t s, t;
    uint8_t b[RFM_MOVE_SETTINGS_BYTES];
    rfm_move_settings_default(&s);
    s.slot[1].lay = RFM_MOVE_LAY_DRUM, s.slot[1].channel = 9, s.slot[1].cc[3] = 99, s.slot[2].octave = -2;
    s.lock[2] = 13;
    CHECK(rfm_move_save(&s, b) == RFM_MOVE_SETTINGS_BYTES && RFM_MOVE_SETTINGS_BYTES == 130);
    CHECK(rfm_move_load(&t, b, sizeof b) == 0);
    CHECK(t.slot[1].lay == RFM_MOVE_LAY_DRUM && t.slot[1].channel == 9 && t.slot[1].cc[3] == 99 && t.slot[2].octave == -2 &&
          t.lock[2] == 13 && t.lock[0] == 0);
    rfm_move_settings_default(&s);                      /* a version 6 image (MIDI mode, Drum, bank 2): every slot */
    s.mode = RFM_MOVE_MODE_CONTROL, s.layout = RFM_MOVE_LAYOUT_DRUM, s.bank = 1, s.channel = 4;
    rfm_move_save(&s, b);
    b[3] = 6, b[4] = RFM_MOVE_SETTINGS_BYTES_V6 & 0xFF, b[5] = RFM_MOVE_SETTINGS_BYTES_V6 >> 8;
    CHECK(rfm_move_load(&t, b, RFM_MOVE_SETTINGS_BYTES_V6) == 0);
    CHECK(t.mode == RFM_MOVE_MODE_MIDI && t.slot[2].lay == RFM_MOVE_LAY_DRUM && t.slot[0].channel == 4 && t.slot[1].cc[0] == 24);
    fresh_with(&t, 1);                                  /* loaded: slot 1's Drum played */
    CHECK(M.s.mode == RFM_MOVE_MODE_MIDI && M.s.layout == RFM_MOVE_LAYOUT_DRUM);
}

/* ================================================================ speech === */
static void test_speech(void)
{
    fresh();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON);
    rfm_move_set_speech(&M, 1);
    rfm_move_set_slot_layout(&M, 1, RFM_MOVE_LAY_DRUM);
    clear();
    tap_track(1);
    CHECKF(strstr(said, "E:Felucca, Drum layout|") != NULL, "said \"%s\"", said);
    rest(), clear();
    hold_track(1);
    CHECKF(strstr(said, "Felucca, layouts, Drum") != NULL, "said \"%s\"", said);
    clear(), knob(0, 58);
    CHECKF(strstr(said, "Knob 1: CC 74") != NULL, "said \"%s\"", said);
    track(1, 0), rest(), clear();
    back();
    CHECKF(strstr(said, "Sequencer mode") != NULL, "said \"%s\"", said);
    rest(), clear();
    hold_track(0);
    CHECKF(strstr(said, "lock, Track 1") != NULL, "said \"%s\"", said);
    track(0, 0), rest(), clear();
    tap_track(3);
    CHECKF(strstr(said, "Mixer") != NULL, "said \"%s\"", said);
}

int main(void)
{
    test_tap();
    test_layout_menu();
    test_double();
    test_track4();
    test_seq();
    test_fm1();
    test_takeover();
    test_settings();
    test_speech();
    return check_done("move_tracks");
}
