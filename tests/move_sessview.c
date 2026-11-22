/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* 0.9.2: session view as it is reached since the Track buttons (0.8.0, 0.9.0): a firmware's Track tapped (its note
 * layout: the sequencer), then Menu. One test a problem the owner reported on the Move (2026-11-21): scenes, the
 * pressed clip shown as selected (pad and screen), the screen against the pads, clips cut and copied as steps are;
 * and what was found on the way: the RGB writes a playing session view sends, and session view entered while a
 * takeover's clock led the sequencer. Writes sessview_*.pbm (the screens) into the working directory. */
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
    snprintf(path, sizeof path, "sessview_%s.pbm", name);
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "P1\n128 64\n");
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++)
            fputc(px(x, y) ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
    {                                                   /* the pads (the top row first) and the steps beside it, for
                                                         * the board (remoteFM14Move docs/design/make_session_v1.py) */
        rfm_move_led_t o[RFM_MOVE_NLEDS];
        int i, n, r, c;
        uint8_t w[RFM_MOVE_NLEDS][3];
        memset(w, 0, sizeof w);
        rfm_move_leds_invalidate(&M);
        n = rfm_move_leds(&M, o, RFM_MOVE_NLEDS);
        for (i = 0; i < n; i++)
            w[i][0] = o[i].r, w[i][1] = o[i].g, w[i][2] = o[i].b;
        snprintf(path, sizeof path, "sessview_%s.leds", name);
        if (!(f = fopen(path, "w")))
            return;
        for (r = 3; r >= 0; r--)
            for (c = 0; c < 8; c++)
                fprintf(f, "%02x%02x%02x%c", w[L_PAD0 + r * 8 + c][0], w[L_PAD0 + r * 8 + c][1], w[L_PAD0 + r * 8 + c][2], c == 7 ? '\n' : ' ');
        for (c = 0; c < 16; c++)
            fprintf(f, "%02x%02x%02x%c", w[L_STEP0 + c][0], w[L_STEP0 + c][1], w[L_STEP0 + c][2], c == 15 ? '\n' : ' ');
        for (c = 0; c < 8; c++)                         /* (0.11.0) the knobs' lights, the icons under the steps, the
                                                         * Track buttons */
            fprintf(f, "%02x%02x%02x%c", w[L_RING0 + c][0], w[L_RING0 + c][1], w[L_RING0 + c][2], c == 7 ? '\n' : ' ');
        for (c = 0; c < 16; c++)
            fprintf(f, "%02x%c", w[L_ICON0 + c][0], c == 15 ? '\n' : ' ');
        for (c = 0; c < 4; c++)
            fprintf(f, "%02x%02x%02x%c", w[L_TRACK0 + c][0], w[L_TRACK0 + c][1], w[L_TRACK0 + c][2], c == 3 ? '\n' : ' ');
        fclose(f);
    }
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
static int m_nlat(void) { return M.nlat; }


static int m_tk_shown(void) { return rfm_move_tk_shown(&M); }

/* ---- the session grid on the screen (seq_ui.c session_screen, v2_cell): a cell 14 x 13 at (c * 16 + 1, 4 + r * 15) */
static int cell_clip(int c, int r) { return px(c * 16 + 1, 4 + r * 15 + 6) || px(c * 16 + 2, 4 + r * 15 + 6); }
static int cell_dot(int c, int r) { return px(c * 16 + 7, 4 + r * 15 + 5) && !cell_clip(c, r); }
static int cell_icon(int c, int r)                      /* (0.10.0) something inside its outline (the play icon: armed) */
{
    int x, n = 0;
    for (x = c * 16 + 3; x < c * 16 + 13; x++)
        n += px(x, 4 + r * 15 + 6);
    return cell_clip(c, r) && n > 0 && n < 10;
}
static int cell_framed(int c, int r)                    /* the selected clip's frame, in the gap round the cell */
{
    return px(c * 16, 3 + r * 15) && px(c * 16 + 15, 3 + r * 15 + 14) && px(c * 16, 3 + r * 15 + 7);
}
static int nframed(void)
{
    int c, r, n = 0;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 8; c++)
            n += cell_framed(c, r);
    return n;
}
#define PAD(c, r) ((3 - (r)) * 8 + (c))                 /* the pad of column c (0..7), row r (0 the top) */
static int lit(const uint8_t c[3]) { return c[0] + c[1] + c[2] > 0; }
static int white(const uint8_t c[3]) { return c[0] == c[1] && c[1] == c[2] && c[0] > 40; }
static int green(const uint8_t c[3]) { return c[1] > 0 && c[0] == 0 && c[2] == 0; }
static void clip_put(int track, int scene, int note) { rfmv_seq_note_put(&M, SEQ_CLIP(track, scene), 0, note, 100, 24, 0); }
static int clip_note(int track, int scene, int note) { return rfmv_seq_find(&M, SEQ_CLIP(track, scene), 0, note) >= 0; }
static int clip_has(int track, int scene) { return rfmv_seq_clip_has(&M, SEQ_CLIP(track, scene)); }

/* playing: on to just past a bar line, so a launch asked for now waits most of a bar (120 BPM: 2 s) */
static void bar_start(void)
{
    int i;
    for (i = 0; i < 400 && !(M.seq.playing && M.seq.T % (16 * RFM_MOVE_SEQ_TPS) < RFM_MOVE_SEQ_TPS); i++)
        adv_ms(10);
}
/* the way in on the Move: Track 2 (felucca) given the Scale layout, tapped (the sequencer on its track), Menu */
static void session_view(void)
{
    fresh();
    hold_track(1), hit(26), track(1, 0);
    rest(), clear();
    tap_track(1), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.fw.active == 1 && !M.seq.view && M.seq.sel_track == 1);
    press(MR_CC_MENU);
    CHECK(M.seq.view == 1);
    rest(), clear();
}
/* the owner's grid (2026-11-21): tracks 1..4, clips 1 2 3 / 1 2 / 1 3 / 1 2 */
static void owners_grid(void)
{
    static const uint8_t G[4][3] = { { 1, 1, 1 }, { 1, 1, 0 }, { 1, 0, 1 }, { 1, 1, 0 } };
    int t, s;
    for (t = 0; t < 4; t++)
        for (s = 0; s < 3; s++)
            if (G[t][s])
                clip_put(t, s, 60 + t * 4 + s);
}

/* ================================================================ 1. scenes: the 8th column (0.10.0) === */
#define SCN(r) PAD(7, r)                                /* the scene pad of row r */
static void test_scenes(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int t;
    session_view();
    owners_grid();
    /* the scene pads are green: clips in the row brighter than none */
    leds(w);
    CHECK(green(w[L_PAD0 + SCN(0)]) && green(w[L_PAD0 + SCN(3)]) && w[L_PAD0 + SCN(0)][1] > w[L_PAD0 + SCN(3)][1]);
    /* stopped: the scene pad of row 2: that scene the tracks' clips at once (they play with Play); the tracks with
     * no clip there play nothing; its number underlined on the screen, its pad full green */
    hit(SCN(1));
    CHECK(M.seq.tr[0].active == 1 && M.seq.tr[1].active == 1 && M.seq.tr[2].active == -1 && M.seq.tr[3].active == 1);
    CHECK(M.seq.playing || M.seq.start_pending);        /* (0.11.0, the owner's: a scene pad starts the transport) */
    leds(w), screen();
    CHECK(green(w[L_PAD0 + SCN(1)]) && w[L_PAD0 + SCN(1)][1] > 90 && px(7 * 16 + 3, 4 + 15 + 11) && !px(7 * 16 + 3, 4 + 11));
    adv_ms(300);
    CHECK(M.seq.playing && M.seq.tr[0].active == 1);
    /* playing: the scene pad of row 1: queued (the clips' pads and the scene's green, blinking), launched at the bar */
    rest(), bar_start();
    pad(SCN(0), 100), adv_ms(180), pad(SCN(0), 0);
    for (t = 0; t < 4; t++)
        CHECKF(M.seq.tr[t].queued == 0, "track %d queued %d", t + 1, M.seq.tr[t].queued);
    leds(w);
    CHECK(green(w[L_PAD0 + PAD(0, 0)]) && green(w[L_PAD0 + PAD(3, 0)]) && green(w[L_PAD0 + SCN(0)]));
    adv_ms(2100);
    for (t = 0; t < 4; t++)
        CHECKF(M.seq.tr[t].active == 0 && M.seq.tr[t].queued == SEQ_NONE, "track %d active %d", t + 1, M.seq.tr[t].active);
    screen();
    write_pbm("scene");
    CHECK(px(7 * 16 + 3, 4 + 11) && !px(7 * 16 + 3, 4 + 15 + 11));   /* (scene 1 underlined now) */
    /* row 3: tracks 1 and 3 have a clip there, 2 and 4 stop at the bar */
    bar_start();
    hit(SCN(2));
    CHECK(M.seq.tr[0].queued == 2 && M.seq.tr[1].queued == SEQ_STOP && M.seq.tr[2].queued == 2 && M.seq.tr[3].queued == SEQ_STOP);
    adv_ms(2100);
    CHECK(M.seq.tr[0].active == 2 && M.seq.tr[1].active == -1 && M.seq.tr[2].active == 2 && M.seq.tr[3].active == -1);
    CHECK(M.seq.playing);
    /* a modifier + a scene pad: nothing; Copy let go afterwards is no tap either */
    button(MR_CC_SHIFT, 1), hit(SCN(0)), button(MR_CC_SHIFT, 0);
    button(MR_CC_COPY, 1), hit(SCN(0)), button(MR_CC_COPY, 0);
    button(MR_CC_DELETE, 1), hit(SCN(0)), button(MR_CC_DELETE, 0);
    CHECK(M.seq.tr[0].queued == SEQ_NONE && M.seq.tr[0].active == 2 && clip_has(0, 0) && !clip_has(0, 3));
}

/* ================================================================ 2. the pressed clip shown as selected; Live's colours === */
static void test_selected(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int i, flash = 0, grn = 0;
    session_view();
    owners_grid();
    /* at rest: track 2's clip 1 (the slot's track) is the selected one: its pad white, its cell framed, no other;
     * row 1's other clips are their tracks' clips (Play starts them): green, steady, the play icon in their outlines;
     * the rest their track's colour, dim */
    leds(w), screen();
    write_pbm("rest");
    CHECK(white(w[L_PAD0 + PAD(1, 0)]) && cell_framed(1, 0) && nframed() == 1);
    CHECK(green(w[L_PAD0 + PAD(0, 0)]) && w[L_PAD0 + PAD(0, 0)][1] == 255 && cell_icon(0, 0) && cell_icon(1, 0));
    CHECK(!green(w[L_PAD0 + PAD(0, 1)]) && w[L_PAD0 + PAD(0, 1)][0] == 255 * 80 / 256 && !cell_icon(0, 1));
    /* stopped: a clip pressed: it is the track's clip (Play starts it: the play icon in its outline) and the selected
     * one, on the pad and on the screen; the clip selected before, its track's clip to play: green, steady */
    hit(PAD(1, 0)), now_ms += 1000, hit(PAD(2, 2));
    leds(w), screen();
    write_pbm("selected");
    CHECK(M.seq.sel_track == 2 && M.seq.tr[2].sel == 2 && M.seq.tr[2].active == 2 && M.seq.tr[1].active == 0);
    CHECK(white(w[L_PAD0 + PAD(2, 2)]) && green(w[L_PAD0 + PAD(1, 0)]) && w[L_PAD0 + PAD(1, 0)][1] == 255);
    CHECK(cell_framed(2, 2) && !cell_framed(1, 0) && nframed() == 1 && cell_clip(2, 2));
    CHECK(cell_icon(2, 2) && !cell_icon(2, 0));         /* (the icon: armed; track 3's clip 1 no longer) */
    CHECK(px(2 * 16 + 1, 0) && !px(1 * 16 + 1, 0));     /* (the bar over the selected track's column) */
    /* (0.10.0) an empty slot pressed: selected (white, framed round its dot), no clip made; stopped: the track's
     * clip to play is none */
    now_ms += 1000, hit(PAD(1, 2));
    leds(w), screen();
    CHECK(M.seq.sel_track == 1 && M.seq.tr[1].sel == 2 && !clip_has(1, 2) && M.seq.tr[1].active == -1);
    CHECK(white(w[L_PAD0 + PAD(1, 2)]) && cell_framed(1, 2) && cell_dot(1, 2) && nframed() == 1);
    CHECK(!green(w[L_PAD0 + PAD(1, 0)]));
    /* .. the first note makes the clip (track view: a step), and it is the track's clip */
    press(MR_CC_MENU);
    CHECK(!M.seq.view && M.seq.sel_track == 1);
    step(4, 1), step(4, 0);
    CHECK(clip_has(1, 2) && M.seq.tr[1].active == 2);
    press(MR_CC_MENU);
    now_ms += 1000, hit(PAD(1, 0));
    /* playing: a clip pressed is selected at once on the screen (framed, its doubled outline while it waits) and
     * its pad blinks green; at the bar it plays: green, pulsing, white on the beat; the cell filled inside its frame */
    press(MR_CC_PLAY), adv_ms(700), bar_start();
    now_ms += 1000, pad(PAD(0, 1), 100), adv_ms(150), pad(PAD(0, 1), 0), adv_ms(60);
    leds(w), screen();
    write_pbm("queued");
    CHECK(M.seq.sel_track == 0 && M.seq.tr[0].sel == 1 && M.seq.tr[0].queued == 1 && M.seq.tr[0].active == 0);
    CHECK(cell_framed(0, 1) && nframed() == 1);
    CHECK(green(w[L_PAD0 + PAD(0, 1)]) || white(w[L_PAD0 + PAD(0, 1)]) || w[L_PAD0 + PAD(0, 1)][0] == 255 * 60 / 256);
    adv_ms(2100);
    screen();
    write_pbm("playing");
    CHECK(M.seq.tr[0].active == 1 && M.seq.tr[0].queued == SEQ_NONE);
    CHECK(cell_framed(0, 1) && nframed() == 1);
    CHECK(!px(0 * 16 + 1, 4 + 15 + 6) && px(0 * 16 + 2, 4 + 15 + 6));   /* (filled, clear of its frame) */
    for (i = 0; i < 50; i++) {                          /* a beat (500 ms): the selected playing clip white on the beat,
                                                         * green after; another playing clip green throughout */
        adv_ms(10), leds(w);
        flash += w[L_PAD0 + PAD(0, 1)][0] == 255 && w[L_PAD0 + PAD(0, 1)][1] == 255;
        grn += green(w[L_PAD0 + PAD(0, 1)]);
        CHECKF(green(w[L_PAD0 + PAD(1, 0)]), "track 2's playing clip at %d ms: %02x%02x%02x", i * 10, w[L_PAD0 + PAD(1, 0)][0],
               w[L_PAD0 + PAD(1, 0)][1], w[L_PAD0 + PAD(1, 0)][2]);
    }
    CHECKF(flash >= 4 && flash <= 12 && grn >= 36 && flash + grn == 50, "white %d, green %d of 50", flash, grn);
    CHECK(w[L_PAD0 + PAD(0, 0)][0] == 255 * 80 / 256 && !green(w[L_PAD0 + PAD(0, 0)]));   /* (a stopped clip: its track's, dim) */
    /* the playing clip pressed again: it plays on, selected */
    now_ms += 1000, hit(PAD(0, 1)), adv_ms(100);
    CHECK(M.seq.tr[0].active == 1 && M.seq.tr[0].queued == SEQ_NONE && M.seq.sel_track == 0);
    /* (0.10.0) Shift + a clip: selected for editing, nothing launched: track 2 plays its clip 1 on */
    button(MR_CC_SHIFT, 1), hit(PAD(1, 1)), button(MR_CC_SHIFT, 0);
    leds(w), screen();
    write_pbm("shiftsel");
    CHECK(M.seq.sel_track == 1 && M.seq.tr[1].sel == 1 && M.seq.tr[1].active == 0 && M.seq.tr[1].queued == SEQ_NONE);
    CHECK(white(w[L_PAD0 + PAD(1, 1)]) && cell_framed(1, 1) && nframed() == 1);
    /* .. Shift + an empty slot: selected, the track plays on (without Shift it stops at the bar) */
    button(MR_CC_SHIFT, 1), hit(PAD(1, 3)), button(MR_CC_SHIFT, 0);
    CHECK(M.seq.tr[1].sel == 3 && !clip_has(1, 3) && M.seq.tr[1].queued == SEQ_NONE && M.seq.tr[1].active == 0);
    bar_start(), now_ms += 1000, hit(PAD(1, 3));
    leds(w), screen();
    write_pbm("empty");
    CHECK(M.seq.tr[1].sel == 3 && !clip_has(1, 3) && M.seq.tr[1].queued == SEQ_STOP);
    adv_ms(2100);
    CHECK(M.seq.tr[1].active == -1 && M.seq.playing && !clip_has(1, 3));
}

/* ================================================================ 3. the screen and the pads === */
static void aligned(const char *what)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int c, r;
    leds(w), screen();
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 7; c++) {
            const int track = M.seq.track_page + c, scene = M.seq.scene_off + r, has = clip_has(track, scene);
            const int sel = track == M.seq.sel_track && M.seq.tr[track].sel == scene;
            CHECKF(cell_clip(c, r) == has && cell_dot(c, r) == !has, "%s: the screen's column %d row %d (track %d clip %d: %d)", what,
                   c + 1, r + 1, track + 1, scene + 1, has);
            CHECKF(lit(w[L_PAD0 + PAD(c, r)]) == (has || sel), "%s: the pad of column %d row %d (track %d clip %d: %d)", what,
                   c + 1, r + 1, track + 1, scene + 1, has);
            CHECKF(cell_framed(c, r) == sel, "%s: the frame at column %d row %d", what, c + 1, r + 1);
        }
        CHECKF(green(w[L_PAD0 + SCN(r)]) && !cell_framed(7, r), "%s: the scene pad of row %d", what, r + 1);
    }
}
static void test_aligned(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int i;
    session_view();
    owners_grid();
    clip_put(9, 6, 70), clip_put(15, 15, 71), clip_put(6, 3, 72);
    aligned("at rest");
    hit(PAD(3, 1)), aligned("a clip pressed");
    press(MR_CC_RIGHT), aligned("one track right");
    press(MR_CC_DOWN), aligned("one clip down");
    CHECK(M.seq.track_page == 1 && M.seq.scene_off == 1);
    now_ms += 1000, hit(PAD(0, 0));                     /* (the top-left pad: track 2, clip 2) */
    CHECK(M.seq.sel_track == 1 && M.seq.tr[1].sel == 1);
    aligned("scrolled, a clip pressed");
    for (i = 0; i < 20; i++)
        press(MR_CC_RIGHT), press(MR_CC_DOWN);
    CHECK(M.seq.track_page == 9 && M.seq.scene_off == 12);   /* (7 tracks in view: 10 .. 16) */
    aligned("the far corner");
    now_ms += 1000, hit(PAD(6, 3));                     /* (column 7's bottom pad: track 16, clip 16) */
    CHECK(M.seq.sel_track == 15 && M.seq.tr[15].sel == 15);
    aligned("the far corner, its clip pressed");
    /* the scenes' numbers in the 8th column: 13 .. 16 here (two digits: pixels left of the cell's middle) */
    screen();
    {
        int x, y, n1 = 0, n2 = 0;
        for (y = 4; y < 17; y++)
            for (x = 112; x < 128; x++)
                n1 += px(x, y) && x < 120, n2 += px(x, y) && x >= 120;
        CHECKF(n1 >= 5 && n2 >= 5, "the scene's number: %d + %d pixels", n1, n2);
    }
    /* the knobs over the columns: a touch selects that column's track and shows its popup while touched: the
     * track, the firmwares it reaches (all three by default), its clip, its channel */
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    clear();
    raw(0x90, 3, 127), adv_ms(60);
    CHECK(M.seq.sel_track == 9 + 3 && M.pop == POP_TRKINFO && rfmv_pop_up(&M, POP_TRKINFO));
    write_pbm("info");
    {
        char row[4][32];
        rfmv_seq_track_info(&M, 12, row);
        CHECKF(!strcmp(row[0], "Track 13") && !strcmp(row[2], "Clip 1, empty") && !strcmp(row[3], "Channel 13"), "\"%s\" \"%s\" \"%s\"",
               row[0], row[2], row[3]);
        CHECKF(strstr(row[1], "Felucca") != NULL, "the firmwares: \"%s\"", row[1]);
        rfmv_seq_track_info(&M, 15, row);
        CHECKF(!strcmp(row[2], "Clip 16, 1 bar"), "\"%s\"", row[2]);
    }
    CHECKF(strstr(said, "Track 13") != NULL && strstr(said, "Channel 13") != NULL, "said \"%s\"", said);
    CHECK(!cell_framed(3, 0) && lit_rows(0, 18) > 60);  /* (the popup over the whole screen, not the grid) */
    adv_ms(3000);
    CHECK(rfmv_pop_up(&M, POP_TRKINFO));                /* (it stays while the knob is touched) */
    raw(0x80, 3, 0), adv_ms(20);
    CHECK(!rfmv_pop_up(&M, POP_TRKINFO));
    aligned("a knob touched and let go");
    raw(0x90, 7, 127);                                  /* (0.11.0, the owner's) knob 8, over the scenes: it says so */
    screen();
    CHECK(M.seq.sel_track == 12 && rfmv_pop_up(&M, POP_TRKINFO) && M.pop_i == 7 && lit_rows(0, 18) > 60 && !cell_framed(3, 0));
    write_pbm("scenes");
    raw(0x80, 7, 0), adv_ms(20);
    CHECK(!rfmv_pop_up(&M, POP_TRKINFO));
    /* the steps under the columns (0.10.0, Control Live's): odd selects the column's track, even stops it; step 15
     * nothing; the selected track's select step its full colour */
    rfm_move_set_speech(&M, 0);
    step(2, 1), step(2, 0);
    CHECK(M.seq.sel_track == 9 + 1);
    leds(w);
    CHECK(bright(w[L_STEP0 + 2]) > 3 * bright(w[L_STEP0 + 0]) && bright(w[L_STEP0 + 0]) > 0 && !lit(w[L_STEP0 + 14]));
    step(14, 1), step(14, 0);
    CHECK(M.seq.sel_track == 9 + 1);
    /* (0.11.0, the owner's) session view is its own layer: a slot's Track tapped: that firmware is the one played,
     * the view stays; its summary a second (the 16 tracks: which reach it), then the grid again */
    tap_track(0);
    screen();
    CHECK(M.seq.view && M.fw.active == 0 && M.s.mode == RFM_MOVE_MODE_SEQ && M.seq.sel_track == 9 + 1 && rfmv_pop_up(&M, POP_SLOT));
    write_pbm("slot");
    rest();
    CHECK(M.seq.view && !rfmv_pop_up(&M, POP_SLOT));
    tap_track(1), rest();
    CHECK(M.seq.view && M.fw.active == 1);
}

/* ================================================================ the steps: stop a track, stop every clip; Shift + Play === */
static void test_stops(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    session_view();
    owners_grid();
    hit(SCN(0));                                        /* (0.11.0: the scene pad starts the transport) */
    adv_ms(700), bar_start();
    leds(w);
    CHECK(white(w[L_STEP0 + 1]) && white(w[L_STEP0 + 7]) && bright(w[L_STEP0 + 9]) < 30 && white(w[L_STEP0 + 15]));
    /* step 4: track 2 stops at the bar (its stop step blinks; the others play on) */
    step(3, 1), step(3, 0);
    CHECK(M.seq.tr[1].queued == SEQ_STOP && M.seq.tr[0].queued == SEQ_NONE && M.seq.sel_track == 1);
    adv_ms(2100);
    CHECK(M.seq.tr[1].active == -1 && M.seq.tr[0].active == 0 && M.seq.playing);
    /* Mute + a step: the 16 tracks' mutes, as everywhere (not the column pairs) */
    button(MR_CC_MUTE, 1), step(2, 1), step(2, 0), button(MR_CC_MUTE, 0);
    CHECK(M.seq.tr[2].mute && !M.seq.tr[1].mute && M.seq.tr[2].active == 0);
    button(MR_CC_MUTE, 1), step(2, 1), step(2, 0), button(MR_CC_MUTE, 0);
    CHECK(!M.seq.tr[2].mute);
    /* Shift + Play: every clip from its start, as in track view (before 0.10.0: every clip stopped) */
    button(MR_CC_SHIFT, 1), press(MR_CC_PLAY), button(MR_CC_SHIFT, 0);
    adv_ms(300);
    CHECK(M.seq.playing && M.seq.tr[0].active == 0 && M.seq.tr[0].queued == SEQ_NONE && M.seq.tr[2].queued == SEQ_NONE);
    /* step 16: every clip stops at the bar; the transport runs on */
    bar_start();
    step(15, 1), step(15, 0);
    CHECK(M.seq.tr[0].queued == SEQ_STOP && M.seq.tr[2].queued == SEQ_STOP && M.seq.tr[3].queued == SEQ_STOP);
    leds(w), screen();
    write_pbm("stopall");
    adv_ms(2100);
    CHECK(M.seq.tr[0].active == -1 && M.seq.tr[2].active == -1 && M.seq.tr[3].active == -1 && M.seq.playing);
    leds(w);
    CHECK(bright(w[L_STEP0 + 15]) < 30 && bright(w[L_STEP0 + 1]) < 30);
}

/* ================================================================ clips with no notes (0.10.0) === */
static void test_hollow(void)
{
    static uint8_t blob[RFM_MOVE_PROJECT_MAX];
    uint8_t w[RFM_MOVE_NLEDS][3];
    int n;
    session_view();
    owners_grid();
    /* a clip made with no notes (as every press on an empty slot made one before 0.10.0): its corners only on the
     * screen, its pad much dimmer than a clip with notes */
    rfmv_seq_clip_make(&M, SEQ_CLIP(3, 2), 1), rfmv_seq_clip_make(&M, SEQ_CLIP(5, 1), 1);
    leds(w), screen();
    write_pbm("hollow");
    CHECK(clip_has(3, 2) && !cell_clip(3, 2) && !cell_dot(3, 2) && px(3 * 16 + 1, 4 + 2 * 15) && px(3 * 16 + 14, 4 + 2 * 15 + 12));
    CHECK(lit(w[L_PAD0 + PAD(3, 2)]) && bright(w[L_PAD0 + PAD(3, 2)]) * 3 < bright(w[L_PAD0 + PAD(3, 0)]));
    /* saved and loaded: the clips with no notes are empty slots again; the others stay */
    n = (int)rfm_move_project_save(&M, blob, sizeof blob);
    CHECK(n > 0 && rfm_move_project_load(&M, 0, blob, (uint32_t)n) == 0);
    CHECK(!clip_has(3, 2) && !clip_has(5, 1) && clip_has(0, 0) && clip_has(2, 2) && clip_note(3, 1, 73));
    screen();
    CHECK(cell_dot(3, 2) && cell_clip(3, 1));
}

/* ================================================================ 4. clips cut and copied as steps are === */
static void test_cut_copy(void)
{
    session_view();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    clip_put(0, 0, 60), clip_put(1, 0, 62), clip_put(2, 0, 64), clip_put(3, 0, 65);
    rest(), clear();
    /* Delete held throughout (the steps' sequence, 0.6.3): cut A; A again: pasted back; an empty slot: pasted; B
     * (a clip): cut; C right after: pasted over; C again: cut. The clipboard stays */
    button(MR_CC_DELETE, 1);
    hit(PAD(0, 0));
    CHECK(!clip_has(0, 0) && M.seq.clip_kind == 2 && M.seq.cut && rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)) == 2);
    screen();
    write_pbm("cut");
    CHECK(px(0 * 16 + 1, 4 + 6) && px(0 * 16 + 4, 4 + 3));   /* (an X in its slot) */
    CHECKF(strstr(said, "Cut track 1 clip 1") != NULL, "said \"%s\"", said);
    clear(), now_ms += 400, hit(PAD(0, 0));
    CHECK(clip_note(0, 0, 60) && !M.seq.cut && M.seq.clip_kind == 2 && !rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)));
    CHECKF(strstr(said, "Pasted to track 1 clip 1") != NULL, "said \"%s\"", said);
    clear(), hit(PAD(0, 2));
    CHECK(clip_note(0, 2, 60) && clip_note(0, 0, 60));
    CHECKF(strstr(said, "Pasted to track 1 clip 3") != NULL, "said \"%s\"", said);
    clear(), hit(PAD(1, 0));
    CHECK(!clip_has(1, 0) && M.seq.cut && rfmv_seq_src_clip(&M, SEQ_CLIP(1, 0)) == 2);
    clear(), hit(PAD(2, 0));
    CHECK(clip_note(2, 0, 62) && !clip_note(2, 0, 64) && !M.seq.cut);
    CHECKF(strstr(said, "Track 3 clip 1 replaced") != NULL, "said \"%s\"", said);
    now_ms += 400, hit(PAD(2, 0));
    CHECK(!clip_has(2, 0) && M.seq.cut);
    button(MR_CC_DELETE, 0);
    /* the next hold starts fresh: a clip is cut; an empty slot gets the clipboard, again and again */
    button(MR_CC_DELETE, 1), hit(PAD(3, 0)), button(MR_CC_DELETE, 0);
    CHECK(!clip_has(3, 0) && M.seq.cut);
    button(MR_CC_DELETE, 1), hit(PAD(4, 1)), button(MR_CC_DELETE, 0);
    button(MR_CC_DELETE, 1), hit(PAD(5, 2)), button(MR_CC_DELETE, 0);
    CHECK(clip_note(4, 1, 65) && clip_note(5, 2, 65) && M.seq.clip_kind == 2 && !M.seq.cut);
    /* Undo: the last paste's notes back out */
    press(MR_CC_UNDO);
    CHECK(!clip_note(5, 2, 65) && clip_note(4, 1, 65));
    /* Copy held: a clip copied (dashed while Copy is held), an empty slot gets it; the source stays */
    rest();                                             /* (Undo's message gone) */
    button(MR_CC_COPY, 1), hit(PAD(0, 0));
    screen();
    write_pbm("copied");
    CHECK(clip_note(0, 0, 60) && rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)) == 1);
    hit(PAD(6, 0)), now_ms += 400, hit(PAD(6, 3));
    button(MR_CC_COPY, 0);
    CHECK(clip_note(6, 0, 60) && clip_note(6, 3, 60) && clip_note(0, 0, 60) && !M.seq.paste);
    /* Copy + a clip, Copy let go, then a press: pasted there once (Move's way), a clip there replaced; the press
     * after it is a press again */
    clip_put(5, 0, 71);
    button(MR_CC_COPY, 1), hit(PAD(0, 0)), button(MR_CC_COPY, 0);
    CHECK(M.seq.paste == 1 && rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)) == 1);
    clear(), now_ms += 400, hit(PAD(5, 0));
    CHECK(clip_note(5, 0, 60) && !clip_note(5, 0, 71) && !M.seq.paste);
    CHECKF(strstr(said, "Track 6 clip 1 replaced") != NULL, "said \"%s\"", said);
    press(MR_CC_UNDO);
    CHECK(clip_note(5, 0, 71) && !clip_note(5, 0, 60));
    now_ms += 1000, hit(PAD(5, 0));
    CHECK(clip_note(5, 0, 71) && M.seq.sel_track == 5 && M.seq.tr[5].active == 0);
    /* Back drops a waiting copy's mark (a layer), then leaves session view */
    button(MR_CC_COPY, 1), hit(PAD(0, 0)), button(MR_CC_COPY, 0);
    back();
    CHECK(!M.seq.paste && M.seq.view == 1);
    /* (0.10.0, the owner's) Copy tapped alone: nothing here (in track view it duplicates the clip, as the Move) */
    {
        int t, sn, before = 0, after = 0;
        for (t = 0; t < 16; t++)
            for (sn = 0; sn < 16; sn++)
                before += clip_has(t, sn);
        now_ms += 1000, hit(PAD(0, 0)), now_ms += 1000;
        press(MR_CC_COPY);
        for (t = 0; t < 16; t++)
            for (sn = 0; sn < 16; sn++)
                after += clip_has(t, sn);
        CHECKF(after == before && M.seq.msg != SEQ_MSG_DUP && M.seq.tr[0].sel == 0, "%d clips, then %d", before, after);
    }
    back();
    CHECK(M.seq.view == 0);
}

/* ================================================================ the RGB writes a playing session view sends === */
static void test_led_writes(void)
{
    rfm_move_led_t o[RFM_MOVE_NLEDS];
    int t, i, n = 0, pads = 0, k;
    session_view();
    for (t = 0; t < 8; t++)
        clip_put(t, 0, 60 + t);
    hit(PAD(7, 0));                                     /* (the scene: every track's clip 1; 7 in view; it plays) */
    adv_ms(500);
    rfm_move_leds(&M, o, RFM_MOVE_NLEDS);
    for (i = 0; i < 2000 / 3; i++) {                    /* 2 s (4 beats at 120 BPM), a block every 3 ms as a host has */
        adv_ms(3);
        k = rfm_move_leds(&M, o, RFM_MOVE_NLEDS);
        n += k;
        while (k-- > 0)
            pads += !o[k].cc && o[k].number >= MR_NOTE_PAD0 && o[k].number < MR_NOTE_PAD0 + 32;
    }
    /* (before 0.9.2: a new level every tick: 8 pads x 96 x 4 beats = 3072 writes) */
    CHECKF(pads >= 8 * 4 && pads <= 8 * 6 * 5, "%d pad writes in 2 s", pads);
    CHECKF(n <= 8 * 6 * 5 + 120, "%d LED writes in 2 s", n);
}

/* ================================================================ from a takeover whose clock led the sequencer === */
static void test_from_takeover(void)
{
    static const uint8_t fa = 0xFA, f8 = 0xF8;
    int i;
    fresh();
    rfm_move_set_takeover(&M, 0, 0x5F, "ChoralRoot");
    hold_track(0), hit(25), track(0, 0), rest();        /* (Track 1 held + pad 2: the takeover) */
    CHECK(m_tk_shown() == 0);
    rfm_move_seq_midi_in(&M, &fa, 1);                   /* its Play: Start, then its clock: the sequencer follows */
    for (i = 0; i < 96; i++)
        rfm_move_seq_midi_in(&M, &f8, 1), adv_ms(20);
    CHECK(M.seq.playing && M.seq.following);
    owners_grid();
    hold_track(1), hit(26), track(1, 0);                /* Track 2: the Scale layout, tapped, Menu */
    tap_track(1);
    for (i = 0; i < 24; i++)                            /* (its clock goes on: no longer the sequencer's) */
        rfm_move_seq_midi_in(&M, &f8, 1), adv_ms(20);
    press(MR_CC_MENU);
    CHECK(m_tk_shown() < 0 && M.seq.view && M.seq.playing && !M.seq.following);
    rest(), bar_start();
    now_ms += 1000, hit(PAD(0, 1));
    CHECK(M.seq.tr[0].queued == 1);
    adv_ms(2100);
    CHECKF(M.seq.tr[0].active == 1 && M.seq.tr[0].queued == SEQ_NONE, "active %d queued %d", M.seq.tr[0].active, M.seq.tr[0].queued);
    bar_start();
    hit(SCN(0));
    adv_ms(2100);
    CHECK(M.seq.tr[0].active == 0 && M.seq.tr[1].active == 0 && M.seq.tr[2].active == 0 && M.seq.tr[3].active == 0);
}

/* ================================================================ (0.11.0) the owner's UX rules: session view === */
static int cell_stop(int c, int r)                      /* an outline with the stop square in it (it stops at the bar) */
{
    const int x = c * 16 + 1, y = 4 + r * 15;
    int i, j, n = 0;
    for (j = y + 3; j < y + 10; j++)
        for (i = x + 4; i < x + 10; i++)
            n += px(i, j);
    return cell_clip(c, r) && n == 42 && !px(x + 2, y + 6) && !px(x + 11, y + 6);
}
static unsigned area_hash(int x0, int y0, int w, int h)
{
    unsigned hsh = 2166136261u;
    int x, y;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            hsh = (hsh ^ (unsigned)px(x, y)) * 16777619u;
    return hsh;
}
static void test_launch(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int i, stops = 0, plays = 0, grn = 0, own = 0, shot = 0, sc_a = 0, sc_b = 0, t;
    unsigned h0;
    session_view();
    owners_grid();
    hit(SCN(0));
    adv_ms(700), bar_start();
    /* the stop steps' icons are lit (a step that is a function), the select steps' are not; step 16's too */
    leds(w);
    CHECK(w[L_ICON0 + 1][0] > 0 && !w[L_ICON0 + 0][0] && !w[L_ICON0 + 2][0] && w[L_ICON0 + 15][0] > 0 && !w[L_ICON0 + 14][0]);
    /* the knobs are lit (a touch shows a track): knob 8 green (the scenes); a track the firmware played gets in its
     * colour, one it does not get faint white */
    M.seq.fx.ffilt[1] &= (uint16_t)~(1u << 3);
    leds(w);
    CHECK(green(w[L_RING0 + 7]) && lit(w[L_RING0 + 0]) && !white(w[L_RING0 + 0]) && w[L_RING0 + 3][0] == w[L_RING0 + 3][1] &&
          lit(w[L_RING0 + 3]) && bright(w[L_RING0 + 3]) < bright(w[L_RING0 + 0]));
    M.seq.fx.ffilt[1] |= 1u << 3;
    /* a clip launched over the track's playing one: the playing one flashes with a stop icon (its pad: green going
     * to its own colour), the one that waits flashes with a play icon (its pad: green blinking) */
    now_ms += 1000, hit(PAD(0, 1));
    CHECK(M.seq.tr[0].queued == 1 && M.seq.tr[0].active == 0);
    for (i = 0; i < 60; i++) {
        adv_ms(10), screen(), leds(w);
        stops += cell_stop(0, 0), plays += cell_icon(0, 1);
        grn += green(w[L_PAD0 + PAD(0, 0)]), own += !green(w[L_PAD0 + PAD(0, 0)]) && w[L_PAD0 + PAD(0, 0)][0] > 0;
        if (cell_stop(0, 0) && !shot)
            write_pbm("handover"), shot = 1;
    }
    CHECKF(stops > 8 && stops < 52 && plays > 8 && plays < 52, "the stop icon %d, the play icon %d of 60", stops, plays);
    CHECKF(grn > 8 && own > 8 && grn + own == 60, "the stopping pad: green %d, its colour %d of 60", grn, own);
    adv_ms(2100);
    CHECK(M.seq.tr[0].active == 1);
    /* a scene launched: its box flashes with a play icon while it waits; the scene that plays .. with a stop icon */
    hit(SCN(0)), adv_ms(2100), bar_start();
    now_ms += 1000, hit(SCN(2));
    h0 = 0, shot = 0;
    for (i = 0; i < 60; i++) {
        adv_ms(10), screen();
        if (!i)
            h0 = area_hash(7 * 16, 4 + 2 * 15, 16, 13);
        sc_a += area_hash(7 * 16, 4 + 2 * 15, 16, 13) != h0;
        sc_b += px(7 * 16 + 3 + 1, 4 + 0 * 15 + 1 + 1) && px(7 * 16 + 3 + 9, 4 + 0 * 15 + 1 + 9);   /* (the stop square's corners) */
        if (sc_b && !shot)
            write_pbm("scenequeue"), shot = 1;
    }
    CHECKF(sc_a > 8 && sc_a < 52 && sc_b > 8 && sc_b < 52, "the waiting scene's box changed %d, the playing one's stop icon %d of 60", sc_a, sc_b);
    adv_ms(2100);
    /* a double click on a clip: now, not at the bar */
    bar_start();
    now_ms += 1000, hit(PAD(0, 0)), hit(PAD(0, 0));
    CHECK(M.seq.tr[0].active == 0 && M.seq.tr[0].queued == SEQ_NONE && M.seq.playing && M.seq.sel_track == 0);
    /* .. on an empty slot: its track stops now (no clip made) */
    now_ms += 1000, hit(PAD(0, 3)), hit(PAD(0, 3));
    CHECK(M.seq.tr[0].active == -1 && M.seq.tr[0].queued == SEQ_NONE && !clip_has(0, 3) && M.seq.tr[0].sel == 3);
    /* .. on a scene: now */
    bar_start();
    now_ms += 1000, hit(SCN(1)), hit(SCN(1));
    CHECK(M.seq.tr[0].active == 1 && M.seq.tr[1].active == 1 && M.seq.tr[2].active == -1 && M.seq.tr[3].active == 1);
    for (t = 0; t < 4; t++)
        CHECKF(M.seq.tr[t].queued == SEQ_NONE, "track %d queued %d", t + 1, M.seq.tr[t].queued);
    /* Shift + a scene: its clips stop at the bar .. */
    bar_start();
    now_ms += 1000;
    button(MR_CC_SHIFT, 1), hit(SCN(1)), button(MR_CC_SHIFT, 0);
    CHECK(M.seq.tr[0].queued == SEQ_STOP && M.seq.tr[1].queued == SEQ_STOP && M.seq.tr[3].queued == SEQ_STOP && M.seq.tr[2].queued == SEQ_NONE);
    CHECK(!M.seq.shift_lock && M.seq.playing);
    adv_ms(2100);
    CHECK(M.seq.tr[0].active == -1 && M.seq.tr[1].active == -1 && M.seq.tr[3].active == -1 && M.seq.playing);
    /* .. a double click with Shift: now */
    now_ms += 1000, hit(SCN(0)), adv_ms(2100), bar_start();
    CHECK(M.seq.tr[0].active == 0 && M.seq.tr[3].active == 0);
    now_ms += 1000;
    button(MR_CC_SHIFT, 1), hit(SCN(0)), hit(SCN(0)), button(MR_CC_SHIFT, 0);
    for (t = 0; t < 4; t++)
        CHECKF(M.seq.tr[t].active == -1 && M.seq.tr[t].queued == SEQ_NONE, "track %d active %d queued %d", t + 1, M.seq.tr[t].active, M.seq.tr[t].queued);
    CHECK(M.seq.playing);
}

/* ================================================================ (0.11.0) a held Track's tracks: gated, at once === */
static void test_route(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    unsigned f0;
    session_view();
    owners_grid();
    f0 = M.seq.fx.ffilt[1];
    CHECK((f0 & 0x7F) == 0x7F);
    /* session view, Track 2 held: the pads are the routing: a column lit: its track reaches the firmware; the scene
     * column and the knobs dark (nothing passes to the clips) */
    hold_track(1);
    leds(w), screen();
    CHECK(lit(w[L_PAD0 + PAD(0, 0)]) && lit(w[L_PAD0 + PAD(0, 3)]) && lit(w[L_PAD0 + PAD(6, 2)]) && !lit(w[L_PAD0 + SCN(0)]) && !lit(w[L_PAD0 + SCN(3)]));
    CHECK(!lit(w[L_RING0 + 0]) && !lit(w[L_RING0 + 7]));
    write_pbm("route_hold");
    /* a click on column 3: nothing until the double click's time has passed (its pad white meanwhile), then its track
     * is not routed: the column dark, its step dark, the screen says so */
    pad(PAD(2, 1), 100);
    leds(w);
    CHECK(white(w[L_PAD0 + PAD(2, 1)]) && M.seq.fx.ffilt[1] == f0);
    pad(PAD(2, 1), 0), adv_ms(100);
    CHECK(M.seq.fx.ffilt[1] == f0);
    adv_ms(RFM_MOVE_DOUBLE_MS);
    leds(w), screen();
    CHECK(M.seq.fx.ffilt[1] == (f0 & ~(1u << 2)) && M.lk_word == LK_OFF);
    CHECK(!lit(w[L_PAD0 + PAD(2, 0)]) && !lit(w[L_PAD0 + PAD(2, 3)]) && !lit(w[L_STEP0 + 2]) && lit(w[L_STEP0 + 3]));
    write_pbm("route_off");
    /* a double click: the firmware's solo, at once; the click's toggle never shows */
    hit(PAD(4, 0)), hit(PAD(4, 0));
    CHECK(M.fsolo[1] == 5 && M.seq.fx.ffilt[1] == 1u << 4 && M.lk_word == LK_SOLO);
    adv_ms(RFM_MOVE_DOUBLE_MS + 100);
    CHECK(M.fsolo[1] == 5 && M.seq.fx.ffilt[1] == 1u << 4);
    hit(PAD(4, 0)), hit(PAD(4, 0));
    CHECK(!M.fsolo[1] && M.seq.fx.ffilt[1] == (f0 & ~(1u << 2)));
    adv_ms(RFM_MOVE_DOUBLE_MS + 100);
    CHECK(M.seq.fx.ffilt[1] == (f0 & ~(1u << 2)));
    /* the Track let go right after a click: the click acts then; no clip was launched, the view is session's */
    hit(PAD(2, 0)), track(1, 0);
    CHECK(M.seq.fx.ffilt[1] == f0 && M.seq.view && M.seq.tr[2].queued == SEQ_NONE && M.seq.sel_track == 1 && M.fw.active == 1);
    rest();
    /* track view, Track 2 held, a track's step: a click selects it once the double click's time has passed */
    press(MR_CC_MENU);
    CHECK(!M.seq.view);
    hold_track(1);
    step(5, 1);
    leds(w);
    CHECK(white(w[L_STEP0 + 5]));                       /* (pressed: white at once) */
    step(5, 0);
    CHECK(M.s.lock[1] != 5 && M.seq.sel_track != 5);
    adv_ms(RFM_MOVE_DOUBLE_MS + 30);
    CHECK(M.s.lock[1] == 5 && M.seq.sel_track == 5 && M.lk_word == LK_LOCKED);
    /* a hold: not routed once held long enough, while still down (the LED and the screen then); its release: nothing */
    step(6, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS - 60);
    CHECK(M.seq.fx.ffilt[1] >> 6 & 1);
    adv_ms(90);
    CHECK(!(M.seq.fx.ffilt[1] >> 6 & 1) && M.lk_word == LK_OFF && M.menu_what == MENU_LOCK && M.menu_i == 6);
    screen();
    write_pbm("hold_notrouted");
    step(6, 0), adv_ms(RFM_MOVE_DOUBLE_MS + 100);
    leds(w);
    CHECK(!(M.seq.fx.ffilt[1] >> 6 & 1) && M.s.lock[1] == 5 && !lit(w[L_STEP0 + 6]) && lit(w[L_STEP0 + 7]));
    /* a double click: the solo; no select from it */
    step(8, 1), step(8, 0), step(8, 1), step(8, 0);
    CHECK(M.fsolo[1] == 9 && M.lk_word == LK_SOLO);
    adv_ms(RFM_MOVE_DOUBLE_MS + 100);
    CHECK(M.fsolo[1] == 9 && M.s.lock[1] == 5);
    /* the owner's: the solo stays when another track is selected (that track is routed once the solo ends) */
    step(9, 1), step(9, 0), adv_ms(RFM_MOVE_DOUBLE_MS + 30);
    CHECK(M.s.lock[1] == 9 && M.seq.sel_track == 9 && M.fsolo[1] == 9 && M.seq.fx.ffilt[1] == 1u << 8 && (M.fsolo_save[1] >> 9 & 1));
    rest();
    screen();
    write_pbm("hold");
    track(1, 0);
}

/* ================================================================ (0.11.0) the arp menu === */
static void test_arp(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    rfm_move_arp_t *ap;
    unsigned h0, h1, h2;
    int g, used, i;
    session_view();
    press(MR_CC_MENU);
    ap = &M.seq.tr[M.seq.sel_track].arp;
    button(MR_CC_SHIFT, 1), step(10, 1), step(10, 0), button(MR_CC_SHIFT, 0);
    CHECK(M.seq.smenu == RFM_MOVE_SM_ARP && ap->mode == RFM_MOVE_AM_OFF);
    rest();
    screen();
    h0 = area_hash(0, 0, 128, 17);
    write_pbm("arp_off");
    /* the jog's click: on and off, the header says which at once */
    jog_click();
    screen();
    CHECK(ap->mode == RFM_MOVE_AM_ON && area_hash(0, 0, 128, 17) != h0);
    write_pbm("arp_on");
    knob(4, RFM_MOVE_LIST_TICKS * 3), jog_click();
    CHECK(ap->mode == RFM_MOVE_AM_OFF);
    jog_click();
    CHECK(ap->mode == RFM_MOVE_AM_HOLD);                /* (as it was) */
    rest();
    screen();
    h0 = area_hash(0, 0, 128, 17);
    /* a knob touched: its name and value in the header; turned: the value changes there; let go: a second, then the
     * page's own header again */
    raw(0x90, 5, 127);
    screen();
    h1 = area_hash(0, 0, 128, 17);
    g = ap->gate;
    knob(5, 1);
    screen();
    h2 = area_hash(0, 0, 128, 17);
    CHECK(h1 != h0 && h2 != h1 && ap->gate == g + 5 && M.pop != POP_CC && M.pop != POP_TRIG);
    write_pbm("arp_gate");
    raw(0x80, 5, 0), adv_ms(RFM_MOVE_POPUP_MS + 100);
    screen();
    CHECK(area_hash(0, 0, 128, 17) == h0);
    /* the knobs it uses are lit (all 8 on this page) */
    leds(w);
    for (i = 0; i < 8; i++)
        CHECKF(lit(w[L_RING0 + i]), "knob %d", i + 1);
    /* Menu: the view flips under the menu, named a second; the menu stays, its knobs too; Back closes it */
    press(MR_CC_MENU);
    screen();
    CHECK(M.seq.view && M.seq.smenu == RFM_MOVE_SM_ARP && rfmv_pop_up(&M, POP_VIEW));
    write_pbm("arp_view");
    rest();
    g = ap->gate;
    raw(0x90, 5, 127), knob(5, 1), raw(0x80, 5, 0);
    CHECK(ap->gate == g + 5 && M.pop != POP_TRKINFO && M.seq.smenu == RFM_MOVE_SM_ARP);
    press(MR_CC_MENU), rest();
    CHECK(!M.seq.view && M.seq.smenu == RFM_MOVE_SM_ARP);
    /* the MOD lane (the jog): the step keys are its 16 steps (none reaches the clip under the menu); knob 1: what the
     * picked one does, knob 2 the length, knob 3 RESET; knobs 4..8 dark and dead */
    jog(1);
    CHECK(M.seq.arp_page == 1);
    used = rfmv_seq_clip_used(&M, SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel));
    step(4, 1), step(4, 0);
    CHECK(M.seq.arp_sel == 4 && rfmv_seq_clip_used(&M, SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel)) == used);
    knob(0, RFM_MOVE_LIST_TICKS);
    CHECK(ap->lane[0][4] == RFM_MOVE_AL_REST);
    knob(1, -RFM_MOVE_LIST_TICKS * 4);
    CHECK(ap->mlen == 12);
    g = ap->gate;
    knob(5, 3);
    CHECK(ap->gate == g);
    leds(w);
    CHECK(lit(w[L_RING0 + 0]) && lit(w[L_RING0 + 2]) && !lit(w[L_RING0 + 3]) && !lit(w[L_RING0 + 7]));
    CHECK(white(w[L_STEP0 + 4]) && !lit(w[L_STEP0 + 12]) && lit(w[L_STEP0 + 0]) && !w[L_ICON0 + 4][0]);
    for (i = 0; i < 12; i++)
        ap->lane[0][i] = (int8_t)(i < RFM_MOVE_AL_N1 ? i : RFM_MOVE_AL_N1 + i - RFM_MOVE_AL_N1);
    screen();
    write_pbm("arp_mod");
    jog(1);
    CHECK(M.seq.arp_page == 2);
    for (i = 0; i < 16; i++)
        ap->lane[1][i] = (int8_t)((i * 7) % 25 - 12);
    step(3, 1), step(3, 0);
    screen();
    write_pbm("arp_trans");
    /* Shift + step 11 still closes it (the Shift layer is over the lane's steps); Back too */
    back();
    CHECK(!M.seq.smenu);
}

int main(void)
{
    test_scenes();
    test_selected();
    test_aligned();
    test_stops();
    test_hollow();
    test_cut_copy();
    test_led_writes();
    test_from_takeover();
    test_launch();
    test_route();
    test_arp();
    (void)count, (void)find, (void)knob, (void)jog, (void)jog_click, (void)dbl_track, (void)m_nlat;
    return check_done("move_sessview");
}
