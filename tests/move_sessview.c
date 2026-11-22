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
    CHECK(M.seq.tr[0].queued == SEQ_NONE && M.seq.tr[0].active == 2 && !clip_has(0, 0) && !clip_has(0, 3));   /* (0.12.0: the
                                                         * row deleted, nothing launched) */
    press(MR_CC_UNDO);
    CHECK(clip_has(0, 0) && clip_has(3, 0));
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

/* ================================================================ 4. clips deleted, copied, pasted (0.12.0, the owner's) === */
static void test_cut_copy(void)
{
    session_view();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    clip_put(0, 0, 60), clip_put(1, 0, 62), clip_put(2, 0, 64), clip_put(3, 0, 65);
    rest(), clear();
    /* Delete held + a clip: deleted at the press, into the buffer (an X in its slot). Delete never pastes: the same
     * slot again, an empty slot: nothing */
    button(MR_CC_DELETE, 1);
    hit(PAD(0, 0));
    CHECK(!clip_has(0, 0) && M.seq.clip_kind == 2 && M.seq.cut && M.seq.sv_armed && rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)) == 2);
    screen();
    write_pbm("cut");
    CHECK(px(0 * 16 + 1, 4 + 6) && px(0 * 16 + 4, 4 + 3));   /* (an X in its slot) */
    CHECKF(strstr(said, "Cut track 1 clip 1") != NULL, "said \"%s\"", said);
    clear(), now_ms += 400, hit(PAD(0, 0)), hit(PAD(0, 2));
    CHECK(!clip_has(0, 0) && !clip_has(0, 2) && M.seq.cut);
    button(MR_CC_DELETE, 0);
    /* one undo brings it back */
    press(MR_CC_UNDO);
    CHECK(clip_note(0, 0, 60));
    rest();
    /* Delete + B: gone; Copy + an empty slot: B pasted there (the buffer stays: again elsewhere) */
    button(MR_CC_DELETE, 1), hit(PAD(1, 0)), button(MR_CC_DELETE, 0);
    CHECK(!clip_has(1, 0));
    clear();
    button(MR_CC_COPY, 1), hit(PAD(4, 1));
    CHECK(clip_note(4, 1, 62) && !M.seq.sv_armed && !rfmv_seq_src_clip(&M, SEQ_CLIP(1, 0)));
    CHECKF(strstr(said, "Pasted to track 5 clip 2") != NULL, "said \"%s\"", said);
    now_ms += 400, hit(PAD(5, 2));
    CHECK(clip_note(5, 2, 62));
    /* .. Copy + a clip now (not armed: the last was a paste): copied (dashed), armed; then Copy + another clip: replaced
     * by it; once (the paste disarms): the next clip is copied */
    clear(), now_ms += 400, hit(PAD(2, 0));
    CHECK(clip_note(2, 0, 64) && M.seq.sv_armed && rfmv_seq_src_clip(&M, SEQ_CLIP(2, 0)) == 1);
    screen();
    write_pbm("copied");
    now_ms += 400, hit(PAD(2, 0));                      /* (the very clip just copied: copied again, no self-paste) */
    CHECK(clip_note(2, 0, 64) && M.seq.sv_armed && M.seq.clip_src == SEQ_CLIP(2, 0));
    clear(), now_ms += 400, hit(PAD(3, 0));
    CHECK(clip_note(3, 0, 64) && !clip_note(3, 0, 65) && !M.seq.sv_armed);
    CHECKF(strstr(said, "Track 4 clip 1 replaced") != NULL, "said \"%s\"", said);
    now_ms += 400, hit(PAD(5, 2));
    CHECK(clip_note(5, 2, 62) && M.seq.sv_armed && M.seq.clip_src == SEQ_CLIP(5, 2));
    button(MR_CC_COPY, 0);
    press(MR_CC_UNDO);
    CHECK(clip_note(3, 0, 65) && !clip_note(3, 0, 64));
    /* armed survives Copy's release: Copy + a clip later pastes over it (the owner's: "replace a clip if the previous
     * function was delete or copy"); nothing happens after Copy is let go: a plain press launches */
    now_ms += 1000, hit(PAD(3, 0));
    CHECK(clip_note(3, 0, 65) && M.seq.sel_track == 3 && !M.seq.paste);
    button(MR_CC_COPY, 1), hit(PAD(3, 0)), button(MR_CC_COPY, 0);
    CHECK(clip_note(3, 0, 62) && !clip_note(3, 0, 65));
    press(MR_CC_UNDO);
    /* Back: the marks go and armed with them; the buffer stays (Copy + an empty slot still pastes) */
    button(MR_CC_COPY, 1), hit(PAD(0, 0)), button(MR_CC_COPY, 0);
    CHECK(M.seq.sv_armed && rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)) == 1);
    back();
    CHECK(!M.seq.sv_armed && !rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)) && M.seq.clip_kind == 2 && M.seq.view == 1);
    button(MR_CC_COPY, 1), hit(PAD(3, 0)), button(MR_CC_COPY, 0);   /* (not armed: a clip is copied, not replaced) */
    CHECK(clip_note(3, 0, 65) && M.seq.clip_src == SEQ_CLIP(3, 0));
    back();
    button(MR_CC_COPY, 1), hit(PAD(6, 3)), button(MR_CC_COPY, 0);
    CHECK(clip_note(6, 3, 65));
    /* (0.10.0, 0.12.0: every view) Copy tapped alone: nothing */
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
    back();                                             /* (0.12.0) nothing open: Back leaves nothing */
    CHECK(M.seq.view == 1);
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
static int cell_stop(int c, int r)                      /* (0.12.0) filled with a black stop square in it (it stops at
                                                         * the bar; was an outline with a white square) */
{
    const int x = c * 16 + 1, y = 4 + r * 15;
    int i, j, n = 0;
    for (j = y + 3; j < y + 10; j++)
        for (i = x + 4; i < x + 10; i++)
            n += px(i, j);
    return cell_clip(c, r) && n == 0 && px(x + 2, y + 6) && px(x + 11, y + 6);
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
    /* (0.12.0, the owner's) no icon under the steps is lit in session view at rest (0.11.0 lit the stop steps') */
    leds(w);
    for (i = 0; i < 16; i++)
        CHECKF(!w[L_ICON0 + i][0], "icon %d", i + 1);
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

/* ================================================================ (0.12.0) the stop icon, the icons, the routed columns, paging === */
static void test_sv_marks(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3], w2[RFM_MOVE_NLEDS][3];
    int i, stop = 0;
    session_view();
    owners_grid();
    clip_put(4, 0, 70), clip_put(4, 1, 71);
    /* n16: no icon lit at rest, stopped or playing; Shift held: the shortcuts' icons, as in track view */
    leds(w);
    for (i = 0; i < 16; i++)
        CHECKF(!w[L_ICON0 + i][0], "icon %d at rest", i + 1);
    button(MR_CC_SHIFT, 1);
    leds(w);
    CHECK(w[L_ICON0 + 0][0] > 0 && w[L_ICON0 + 4][0] > 0 && !w[L_ICON0 + 3][0]);
    button(MR_CC_SHIFT, 0), rest();
    hit(SCN(0)), adv_ms(700), bar_start();
    leds(w);
    for (i = 0; i < 16; i++)
        CHECKF(!w[L_ICON0 + i][0], "icon %d playing", i + 1);
    /* n9: tracks 2 and 5 do not reach the firmware played (felucca, slot 2): their pads a quarter, in every state;
     * their cells' outlines dotted on the screen; the scene column never */
    M.seq.fx.ffilt[1] &= (uint16_t)~((1u << 1) | (1u << 4));
    now_ms += 1000, hit(PAD(4, 1));                     /* (track 5's clip 2 waits for the bar: blinking, selected) */
    leds(w);
    CHECKF(bright(w[L_PAD0 + PAD(1, 0)]) * 3 < bright(w[L_PAD0 + PAD(0, 0)]) && lit(w[L_PAD0 + PAD(1, 0)]),
           "a playing clip not routed %d, routed %d", bright(w[L_PAD0 + PAD(1, 0)]), bright(w[L_PAD0 + PAD(0, 0)]));
    CHECK(bright(w[L_PAD0 + PAD(1, 1)]) * 3 < bright(w[L_PAD0 + PAD(3, 1)]) && lit(w[L_PAD0 + PAD(1, 1)]));
    CHECK(green(w[L_PAD0 + SCN(0)]) && bright(w[L_PAD0 + SCN(0)]) > 60);
    for (i = 0; i < 60 && !stop; i++) {                 /* (the handover phase with the stop icon, for the board) */
        adv_ms(10), screen();
        stop = cell_stop(4, 0);
    }
    CHECK(stop);
    write_pbm("routedcols");
    {
        const int y = 4;                                /* row 1's cells: routed solid, not routed dotted */
        CHECK(px(0 * 16 + 1 + 1, y) && px(0 * 16 + 1 + 2, y) && px(3 * 16 + 1 + 1, y));
        CHECK(!px(1 * 16 + 1 + 1, y) && px(1 * 16 + 1 + 2, y) && !px(1 * 16 + 1 + 3, y));   /* (track 2: playing, filled) */
        CHECK(!px(4 * 16 + 1 + 1, y + 15) && px(4 * 16 + 1 + 2, y + 15));   /* (track 5's clip 2: an outline) */
    }
    /* a pressed pad is not dimmed (white: it is selected at the press) */
    pad(PAD(1, 1), 100);
    leds(w2);
    CHECKF(bright(w2[L_PAD0 + PAD(1, 1)]) > 150, "pressed: %02x%02x%02x", w2[L_PAD0 + PAD(1, 1)][0], w2[L_PAD0 + PAD(1, 1)][1],
           w2[L_PAD0 + PAD(1, 1)][2]);
    pad(PAD(1, 1), 0);
    /* a Track held (the routing pads) or no firmware list: nothing dimmed by it */
    hold_track(1);
    leds(w2);
    CHECK(!lit(w2[L_PAD0 + PAD(4, 0)]) && bright(w2[L_PAD0 + PAD(0, 0)]) > 150);   /* (the routing: not routed dark) */
    track(1, 0), rest();
    M.fw.host = 0;
    leds(w2), screen();
    CHECK(bright(w2[L_PAD0 + PAD(1, 0)]) == bright(w2[L_PAD0 + PAD(0, 0)]) && px(1 * 16 + 1 + 1, 4));
    M.fw.host = 1;
    M.seq.fx.ffilt[1] |= (uint16_t)((1u << 1) | (1u << 4));
    /* c11: Shift + Left / Right a page of 7 tracks, Shift + Up / Down 4 rows; clamped as the single steps */
    rest();
    button(MR_CC_SHIFT, 1);
    press(MR_CC_RIGHT);
    CHECK(M.seq.track_page == 7);
    press(MR_CC_RIGHT);
    CHECK(M.seq.track_page == 9);
    press(MR_CC_LEFT);
    CHECK(M.seq.track_page == 2);
    press(MR_CC_LEFT);
    CHECK(M.seq.track_page == 0);
    press(MR_CC_DOWN);
    CHECK(M.seq.scene_off == 4);
    press(MR_CC_DOWN), press(MR_CC_DOWN), press(MR_CC_DOWN);
    CHECK(M.seq.scene_off == 12);
    press(MR_CC_UP);
    CHECK(M.seq.scene_off == 8);
    button(MR_CC_SHIFT, 0);
    CHECK(!M.seq.shift_lock && !(M.seq.mods & SEQ_M_SHIFT));
    button(MR_CC_SHIFT, 1), button(MR_CC_SHIFT, 0);     /* (Shift used: its release was no tap; one tap: no latch) */
    CHECK(!M.seq.shift_lock);
    press(MR_CC_RIGHT), press(MR_CC_DOWN);
    CHECK(M.seq.track_page == 1 && M.seq.scene_off == 9);
    aligned("paged");
}

/* ================================================================ (0.12.0) session view from anywhere; Back stays === */
static int tk_sent(int cc)                              /* a button's press or release sent to the takeover */
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += A[i].kind == RFM_MOVE_ACT_TK_INPUT && A[i].pkt[1] == 0xB0 && A[i].pkt[2] == cc;
    return n;
}
static void test_sv_anywhere(void)
{
    session_view();
    /* a note layout: Menu out; Shift + Menu in and out (named; Shift used: no latch) */
    press(MR_CC_MENU);
    CHECK(!M.seq.view && M.s.mode == RFM_MOVE_MODE_SEQ);
    rest();
    button(MR_CC_SHIFT, 1), press(MR_CC_MENU);
    CHECK(M.seq.view && rfmv_pop_up(&M, POP_VIEW) && M.pop_i == 1);
    write_pbm("view_named");
    button(MR_CC_SHIFT, 0);
    button(MR_CC_SHIFT, 1), press(MR_CC_MENU), button(MR_CC_SHIFT, 0);
    CHECK(!M.seq.view && !M.seq.shift_lock && rfmv_pop_up(&M, POP_VIEW) && M.pop_i == 0);
    rest();
    /* Back held + Menu: in; Back's release: nothing (no click: no layer closed, the view stays) */
    clip_put(0, 0, 60);
    button(MR_CC_COPY, 1), hit(PAD(0, 0)), button(MR_CC_COPY, 0);   /* (a layer Back would close: the copy's mark) */
    press(MR_CC_MENU), rest();
    button(MR_CC_COPY, 1), hit(PAD(0, 0)), button(MR_CC_COPY, 0);
    press(MR_CC_MENU), rest();
    CHECK(!M.seq.view && M.seq.clip_kind == 2);
    button(MR_CC_BACK, 1), press(MR_CC_MENU);
    CHECK(M.seq.view);
    button(MR_CC_BACK, 0);
    CHECK(M.seq.view);
    rest();
    /* Back in session view with nothing open: nothing (the owner's "Back never changes the mode or the view") */
    while (rfmv_seq_layer_open(&M))
        back();
    back(), rest();
    CHECK(M.seq.view && M.s.mode == RFM_MOVE_MODE_SEQ && M.fw.active == 1);
    button(MR_CC_BACK, 1), press(MR_CC_MENU), button(MR_CC_BACK, 0);
    CHECK(!M.seq.view && M.s.mode == RFM_MOVE_MODE_SEQ);
    /* the FM-1 layout (slot 1): Track 1 held + Menu: session view; the Track's hold menu does not come, its release
     * is nothing (no tap: no layout popup) */
    tap_track(0), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && M.fw.active == 0);
    track(0, 1), press(MR_CC_MENU);
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.seq.view && rfm_move_slot_layout(&M, 0) == RFM_MOVE_LAY_FM1);
    adv_ms(RFM_MOVE_TAP_MS + 200);
    CHECK(!rfmv_hold_shown(&M, 0) && !rfmv_lay_menu(&M));
    screen();
    write_pbm("from_fm1");
    track(0, 0), adv_ms(20);
    CHECK(M.seq.view && M.s.mode == RFM_MOVE_MODE_SEQ && !rfmv_pop_up(&M, POP_LAYOUT) && M.fw.active == 0);
    rest();
    /* .. a Track tap there: the firmware played changes, the view stays; Track 2 held + Menu: out, to slot 2's own
     * layout (its Scale layout: the track view) */
    tap_track(1), rest();
    CHECK(M.seq.view && M.fw.active == 1);
    track(1, 1), press(MR_CC_MENU), track(1, 0);
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && !M.seq.view && M.fw.active == 1 && rfmv_pop_up(&M, POP_VIEW));
    rest();
    /* .. back in, slot 1 tapped there, out: its FM-1 layout, named */
    press(MR_CC_MENU), tap_track(0), rest();
    CHECK(M.seq.view && M.fw.active == 0 && M.s.mode == RFM_MOVE_MODE_SEQ);
    track(0, 1), press(MR_CC_MENU), track(0, 0);
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && !M.seq.view && M.fw.active == 0 && rfmv_pop_up(&M, POP_LAYOUT));
    rest();
    /* Back + Menu from the FM-1 layout: in and out */
    button(MR_CC_BACK, 1), press(MR_CC_MENU), button(MR_CC_BACK, 0);
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.seq.view);
    press(MR_CC_MENU);                                  /* (Menu alone: out, to the FM-1 layout) */
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && !M.seq.view);
    rest();
    /* a takeover that has the buttons (Back is its own): Back + Menu and Track held + Menu are the core's; neither
     * Menu nor Back reaches the firmware */
    rfm_move_set_takeover(&M, 0, 0x5F, "ChoralRoot");
    hold_track(0), hit(25), track(0, 0), rest();
    CHECK(m_tk_shown() == 0);
    clear();
    button(MR_CC_BACK, 1), press(MR_CC_MENU), button(MR_CC_BACK, 0);
    CHECK(M.seq.view && M.s.mode == RFM_MOVE_MODE_SEQ && m_tk_shown() < 0);
    CHECKF(!tk_sent(MR_CC_MENU) && !tk_sent(MR_CC_BACK), "sent: Menu %d, Back %d", tk_sent(MR_CC_MENU), tk_sent(MR_CC_BACK));
    rest(), clear();
    button(MR_CC_BACK, 1), press(MR_CC_MENU), button(MR_CC_BACK, 0);
    CHECK(!M.seq.view && m_tk_shown() == 0 && rfm_move_mode(&M) == RFM_MOVE_MODE_TAKEOVER);
    CHECKF(!tk_sent(MR_CC_MENU) && !tk_sent(MR_CC_BACK), "sent: Menu %d, Back %d", tk_sent(MR_CC_MENU), tk_sent(MR_CC_BACK));
    rest(), clear();
    track(0, 1), press(MR_CC_MENU), track(0, 0);
    CHECK(M.seq.view && m_tk_shown() < 0 && !tk_sent(MR_CC_MENU));
    rest();
    track(0, 1), press(MR_CC_MENU), track(0, 0);
    CHECK(!M.seq.view && m_tk_shown() == 0);
    /* Back alone in the takeover is still the firmware's */
    rest(), clear();
    back();
    CHECK(tk_sent(MR_CC_BACK) == 2 && m_tk_shown() == 0);
}

/* ================================================================ (0.12.0) scenes copied, pasted, deleted; the held icons === */
static int row_clips(int scene)
{
    int t, n = 0;
    for (t = 0; t < 16; t++)
        n += clip_has(t, scene);
    return n;
}
static int scene_icon(int r)                            /* the scene column's cell r: an icon (not a number) */
{
    int x, y, n = 0;
    for (y = 4 + r * 15; y < 4 + r * 15 + 12; y++)
        for (x = 113; x < 127; x++)
            n += px(x, y);
    return n > 18;
}
static void test_sv_scenes(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    static uint8_t a[MR_SCREEN_BYTES], b[MR_SCREEN_BYTES];
    session_view();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    owners_grid();
    clip_put(9, 0, 80);                                 /* (a clip out of view: the row is all 16 tracks) */
    M.seq.cset[SEQ_CLIP(0, 0)] = 3;                     /* (a clip's launch settings go with it) */
    hit(SCN(0)), adv_ms(700), bar_start();
    rest(), clear();
    /* the held icons, at the press: Delete: scissors in the scene column; Copy: the two sheets */
    screen(), memcpy(a, SCR, sizeof a);
    button(MR_CC_DELETE, 1);
    screen();
    CHECK(scene_icon(0) && scene_icon(3) && memcmp(a, SCR, sizeof a));
    write_pbm("delete_held");
    /* Delete + a scene pad: every clip of the row deleted, into the scene buffer; nothing launched; the row's cells
     * crossed and its pads red while Delete is held; said */
    hit(SCN(0));
    CHECK(row_clips(0) == 0 && M.seq.sc_kind == 2 && M.seq.sc_armed && M.seq.sc_src == 0);
    CHECK(M.seq.tr[0].queued == SEQ_STOP || M.seq.tr[0].active == -1);   /* (a playing clip deleted: as a clip's) */
    CHECKF(strstr(said, "Scene 1 deleted") != NULL, "said \"%s\"", said);
    screen(), leds(w);
    CHECK(px(0 * 16 + 4, 4 + 3) && px(3 * 16 + 4, 4 + 3) && !px(4 * 16 + 4, 4 + 3));   /* (X in tracks 1..4; 5 had none) */
    CHECK(w[L_PAD0 + PAD(0, 0)][0] > 0 && !w[L_PAD0 + PAD(0, 0)][1] && lit(w[L_PAD0 + PAD(2, 0)]) && !lit(w[L_PAD0 + PAD(4, 0)]));
    write_pbm("scene_deleted");
    button(MR_CC_DELETE, 0);
    screen();
    CHECK(!px(0 * 16 + 4, 4 + 3) && cell_dot(0, 0));    /* (the marks only while it is held) */
    /* one undo: the row back */
    press(MR_CC_UNDO);
    CHECK(row_clips(0) == 5 && clip_note(9, 0, 80));
    rest();
    /* Delete + an empty row: nothing */
    button(MR_CC_DELETE, 1), hit(SCN(3)), button(MR_CC_DELETE, 0);
    CHECK(row_clips(3) == 0 && M.seq.sc_src == 0);
    /* Copy + a scene: copied (armed), the row dashed while Copy is held; Copy + an empty row: pasted there, every track
     * (the buffer's holes empty), the launch settings with it; nothing launched */
    back(), clear();                                    /* (Back: not armed now; the buffer stays) */
    button(MR_CC_COPY, 1);
    screen();
    CHECK(scene_icon(0));
    write_pbm("copy_held");
    hit(SCN(0));
    CHECK(M.seq.sc_kind == 1 && M.seq.sc_armed && rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)) == 1 && !rfmv_seq_src_clip(&M, SEQ_CLIP(4, 0)));
    CHECKF(strstr(said, "Scene 1 copied") != NULL, "said \"%s\"", said);
    screen();
    write_pbm("scene_copied");
    clear(), hit(SCN(3));
    CHECK(row_clips(3) == 5 && clip_note(0, 3, 60) && clip_note(9, 3, 80) && clip_has(2, 3) && !clip_has(4, 3) && !M.seq.sc_armed);
    CHECK(M.seq.cset[SEQ_CLIP(0, 3)] == 3 && M.seq.tr[0].queued != 3);
    CHECKF(strstr(said, "Pasted to scene 4") != NULL, "said \"%s\"", said);
    button(MR_CC_COPY, 0);
    press(MR_CC_UNDO);
    CHECK(row_clips(3) == 0);
    /* Copy + a row with clips, armed (the last scene function a copy): replaced, all 16 tracks (track 3 keeps none) */
    button(MR_CC_COPY, 1), hit(SCN(0)), button(MR_CC_COPY, 0);   /* (copied) */
    clear();
    button(MR_CC_COPY, 1), hit(SCN(2)), button(MR_CC_COPY, 0);   /* (row 3: tracks 1 and 3) */
    CHECK(row_clips(2) == 5 && clip_note(0, 2, 60) && !clip_note(0, 2, 62) && clip_note(2, 2, 68) && !clip_note(2, 2, 70) &&
          clip_note(9, 2, 80));
    CHECKF(strstr(said, "Scene 3 replaced") != NULL, "said \"%s\"", said);
    press(MR_CC_UNDO);
    CHECK(clip_note(0, 2, 62) && clip_note(2, 2, 70) && !clip_has(9, 2) && !clip_has(1, 2));
    /* not armed (pasted last): Copy + the row copies it; the very row just copied: copied again */
    button(MR_CC_COPY, 1), hit(SCN(2)), hit(SCN(2)), button(MR_CC_COPY, 0);
    CHECK(M.seq.sc_src == 2 && M.seq.sc_armed && clip_note(0, 2, 62));
    /* Back: the marks and armed go, the buffer stays */
    back();
    CHECK(!M.seq.sc_armed && M.seq.sc_kind == 1);
    button(MR_CC_COPY, 1), hit(SCN(3)), button(MR_CC_COPY, 0);
    CHECK(clip_note(0, 3, 62) && clip_note(2, 3, 70) && row_clips(3) == 2);
    /* Delete, then Copy + an empty row: the deleted row pasted there */
    button(MR_CC_DELETE, 1), hit(SCN(3)), button(MR_CC_DELETE, 0);
    CHECK(row_clips(3) == 0);
    button(MR_CC_COPY, 1), hit(SCN(1)), button(MR_CC_COPY, 0);   /* (armed by the delete: row 2 replaced) */
    CHECK(clip_note(0, 1, 62) && !clip_has(1, 1) && clip_note(2, 1, 70) && !clip_has(3, 1));
    /* the held icons in track view: top right; let go: gone; a quick click of Copy or Delete changes nothing */
    press(MR_CC_MENU), rest();
    screen(), memcpy(a, SCR, sizeof a);
    button(MR_CC_DELETE, 1);
    screen(), memcpy(b, SCR, sizeof b);
    CHECK(memcmp(a, b, sizeof a) && px(116 + 5, 1 + 5));   /* (the blades cross at the middle) */
    write_pbm("track_delete_held");
    button(MR_CC_DELETE, 0);
    button(MR_CC_COPY, 1);
    screen();
    CHECK(px(116, 1) && !px(116 + 5, 1 + 2));
    write_pbm("track_copy_held");
    button(MR_CC_COPY, 0);
    rest();
    {
        int t, sn, before = 0, after = 0;
        for (t = 0; t < 16; t++)
            for (sn = 0; sn < 16; sn++)
                before += clip_has(t, sn) * (1 + rfmv_seq_clip_used(&M, SEQ_CLIP(t, sn)));
        press(MR_CC_DELETE), press(MR_CC_COPY), rest();
        press(MR_CC_MENU), rest(), press(MR_CC_DELETE), press(MR_CC_COPY), rest();
        for (t = 0; t < 16; t++)
            for (sn = 0; sn < 16; sn++)
                after += clip_has(t, sn) * (1 + rfmv_seq_clip_used(&M, SEQ_CLIP(t, sn)));
        CHECKF(before == after && M.seq.msg != SEQ_MSG_DUP, "%d, then %d", before, after);
        screen();
        CHECK(!scene_icon(0));
    }
}

/* ================================================================ (0.12.0) launch quantization, a clip's own launch settings === */
static int32_t next_mul(int32_t T, int32_t q) { return (T / q + 1) * q; }
/* track 0 playing: its other clip (scene 0 <-> 1) pressed mid-beat; the tick it took over at, and the tick pressed */
static int32_t switch_at(int32_t *pressed, int *queued)
{
    const int to = M.seq.tr[0].active == 0 ? 1 : 0;
    int i;
    bar_start(), adv_ms(130);
    *pressed = M.seq.T;
    now_ms += 1000, hit(PAD(0, to));
    *queued = M.seq.tr[0].queued == to;
    for (i = 0; i < 2000 && M.seq.tr[0].active != to; i++)
        adv_ms(10);
    return M.seq.tr[0].active == to ? M.seq.tr[0].t0 : -1;
}
static void test_sv_launch_q(void)
{
    static rfm_move_t M2;
    static uint8_t blob[RFM_MOVE_PROJECT_MAX];
    const int32_t BAR = RFM_MOVE_SEQ_BAR, BEAT = RFM_MOVE_SEQ_BAR / 4;
    int32_t p, t;
    int qd, n, i, k;
    session_view();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    clip_put(0, 0, 60), clip_put(0, 1, 62), clip_put(2, 3, 64), clip_put(0, 3, 65);
    CHECK(M.seq.launch_q == RFM_MOVE_LQ_BAR);
    /* Global settings, Launch: Instant .. Clip (1 Bar at first); said */
    button(MR_CC_SHIFT, 1), step(1, 1), step(1, 0), button(MR_CC_SHIFT, 0);
    CHECK(M.seq.smenu == RFM_MOVE_SM_GLOBAL);
    jog(5), clear();
    CHECK(M.gl_row == 5);
    screen();
    write_pbm("global_launch");
    jog_click(), jog(-2), jog_click();
    CHECKF(M.seq.launch_q == RFM_MOVE_LQ_INSTANT && strstr(said, "Launch: Instant") != NULL, "said \"%s\"", said);
    back();
    CHECK(!M.seq.smenu);
    hit(SCN(0)), adv_ms(300);
    CHECK(M.seq.playing && M.seq.tr[0].active == 0);
    /* Instant: at the press, nothing waits (no flashing phase), in the bar's phase */
    t = switch_at(&p, &qd);
    CHECKF(!qd && t == p - p % BAR, "instant: pressed %d, from %d", (int)p, (int)t);
    /* 1 Beat, 1 Bar, 2 Bars, 4 Bars: the next one from the song's start */
    {
        static const int LQ[4] = { RFM_MOVE_LQ_BEAT, RFM_MOVE_LQ_BAR, RFM_MOVE_LQ_2BARS, RFM_MOVE_LQ_4BARS };
        static const int32_t Q[4] = { RFM_MOVE_SEQ_BAR / 4, RFM_MOVE_SEQ_BAR, 2 * RFM_MOVE_SEQ_BAR, 4 * RFM_MOVE_SEQ_BAR };
        for (k = 0; k < 4; k++) {
            M.seq.launch_q = (uint8_t)LQ[k];
            t = switch_at(&p, &qd);
            CHECKF(qd && t == next_mul(p, Q[k]), "%s: pressed %d, from %d, want %d", rfmv_lq_name(LQ[k]), (int)p, (int)t,
                   (int)next_mul(p, Q[k]));
        }
    }
    /* Clip: the end of the playing clip's loop (24 steps: 1.5 bars, not on a bar) */
    M.seq.launch_q = RFM_MOVE_LQ_CLIP;
    rfmv_seq_len_set(&M, SEQ_CLIP(0, 0), 24), rfmv_seq_len_set(&M, SEQ_CLIP(0, 1), 24);
    {
        int32_t t0;
        t0 = M.seq.tr[0].t0;
        t = switch_at(&p, &qd);
        CHECKF(qd && t > p && (t - t0) % (24 * RFM_MOVE_SEQ_TPS) == 0 && t - p <= 24 * RFM_MOVE_SEQ_TPS, "clip: from %d, pressed %d, at %d",
               (int)t0, (int)p, (int)t);
    }
    rfmv_seq_len_set(&M, SEQ_CLIP(0, 0), 16), rfmv_seq_len_set(&M, SEQ_CLIP(0, 1), 16);
    /* a clip's own Launch wins over the project's: 4 Bars, but clip 2 says 1 Beat */
    M.seq.launch_q = RFM_MOVE_LQ_4BARS;
    M.seq.cset[SEQ_CLIP(0, 0)] = M.seq.cset[SEQ_CLIP(0, 1)] = RFM_MOVE_LQ_BEAT + 1;
    t = switch_at(&p, &qd);
    CHECKF(qd && t == next_mul(p, BEAT), "own 1 Beat: pressed %d, from %d", (int)p, (int)t);
    M.seq.cset[SEQ_CLIP(0, 0)] = M.seq.cset[SEQ_CLIP(0, 1)] = 0;
    /* a scene with mixed settings: track 1's clip Instant (at once), track 3's the project's (1 Bar: at the bar) */
    M.seq.launch_q = RFM_MOVE_LQ_BAR;
    M.seq.cset[SEQ_CLIP(0, 3)] = RFM_MOVE_LQ_INSTANT + 1;
    bar_start(), adv_ms(130);
    p = M.seq.T;
    now_ms += 1000, hit(SCN(3));
    CHECK(M.seq.tr[0].active == 3 && M.seq.tr[0].queued == SEQ_NONE && M.seq.tr[2].queued == 3);
    for (i = 0; i < 400 && M.seq.tr[2].active != 3; i++)
        adv_ms(10);
    CHECKF(M.seq.tr[2].t0 == next_mul(p, BAR), "track 3 from %d, want %d", (int)M.seq.tr[2].t0, (int)next_mul(p, BAR));
    /* After = Next: track 2's clips 1, 2, 3 (a run), each Next: 1 -> 2 -> 3 -> 1 at each loop's end, seamless; clip 5
     * alone in its run: it loops */
    clip_put(1, 0, 50), clip_put(1, 1, 51), clip_put(1, 2, 52), clip_put(1, 4, 54);
    for (k = 0; k < 5; k++)
        M.seq.cset[SEQ_CLIP(1, k)] = RFM_MOVE_CS_AFTER_NEXT;
    CHECK(rfmv_seq_after_next(&M, 1, 0) == 1 && rfmv_seq_after_next(&M, 1, 2) == 0 && rfmv_seq_after_next(&M, 1, 4) == 4);
    bar_start();
    now_ms += 1000, hit(PAD(1, 0));
    for (i = 0; i < 400 && M.seq.tr[1].active != 0; i++)
        adv_ms(10);
    t = M.seq.tr[1].t0;
    {
        static const int WANT[4] = { 1, 2, 0, 1 };
        for (k = 0; k < 4; k++) {
            for (i = 0; i < 400 && M.seq.tr[1].t0 == t; i++)
                adv_ms(10);
            CHECKF(M.seq.tr[1].active == WANT[k] && M.seq.tr[1].t0 == t + BAR, "after %d: clip %d from %d (was %d)", k,
                   M.seq.tr[1].active + 1, (int)M.seq.tr[1].t0, (int)t);
            t = M.seq.tr[1].t0;
        }
    }
    M.seq.scene_off = 1;                                /* (rows 2..5 in view: clip 5 on the pads' row 4) */
    bar_start(), now_ms += 1000, hit(PAD(1, 3));
    for (i = 0; i < 400 && M.seq.tr[1].active != 4; i++)
        adv_ms(10);
    adv_ms(2 * 2000 + 100);
    CHECK(M.seq.tr[1].active == 4 && M.seq.tr[1].queued == SEQ_NONE);
    M.seq.scene_off = 0;
    /* kept with the project (version 8): the project's and every clip's; copied with a clip; undone */
    M.seq.launch_q = RFM_MOVE_LQ_2BARS, M.seq.cset[SEQ_CLIP(5, 7)] = RFM_MOVE_LQ_CLIP + 1 + RFM_MOVE_CS_AFTER_NEXT;
    clip_put(5, 7, 40);
    n = rfm_move_project_save(&M, blob, sizeof blob);
    CHECK(n > 0 && blob[4] == RFM_MOVE_PROJECT_VERSION);
    rfm_move_init(&M2, NULL, NULL, NULL);
    CHECK(rfm_move_project_load(&M2, 0, blob, (uint32_t)n) == 0 && M2.seq.launch_q == RFM_MOVE_LQ_2BARS &&
          M2.seq.cset[SEQ_CLIP(5, 7)] == RFM_MOVE_LQ_CLIP + 1 + RFM_MOVE_CS_AFTER_NEXT && M2.seq.cset[SEQ_CLIP(1, 0)] == RFM_MOVE_CS_AFTER_NEXT &&
          !M2.seq.cset[SEQ_CLIP(4, 4)]);
    {                                                   /* a clip's settings out of range: refused */
        uint32_t h = 2166136261u;
        blob[n - 4 - RFM_MOVE_PFW_BYTES - RFM_MOVE_SEQ_CLIPS + 3] = 0x17;
        for (k = 0; k < n - 4; k++)
            h = (h ^ blob[k]) * 16777619u;
        for (k = 0; k < 4; k++)
            blob[n - 4 + k] = (uint8_t)(h >> (8 * k));
        CHECK(rfm_move_project_check(blob, (uint32_t)n) < 0);
    }
    {                                                   /* a version 7 blob: loads, with the defaults */
        uint32_t h = 2166136261u;
        n = rfm_move_project_save(&M, blob, sizeof blob);
        memmove(blob + n - 4 - 1 - RFM_MOVE_SEQ_CLIPS - RFM_MOVE_PFW_BYTES, blob + n - 4, 4);
        n -= 1 + RFM_MOVE_SEQ_CLIPS + RFM_MOVE_PFW_BYTES, blob[4] = 7;
        for (k = 0; k < n - 4; k++)
            h = (h ^ blob[k]) * 16777619u;
        for (k = 0; k < 4; k++)
            blob[n - 4 + k] = (uint8_t)(h >> (8 * k));
        M2.seq.launch_q = RFM_MOVE_LQ_4BARS, M2.seq.cset[3] = 5;
        CHECK(rfm_move_project_load(&M2, 0, blob, (uint32_t)n) == 0 && M2.seq.launch_q == RFM_MOVE_LQ_BAR && !M2.seq.cset[3] &&
              !M2.seq.cset[SEQ_CLIP(5, 7)] && rfmv_seq_find(&M2, SEQ_CLIP(5, 7), 0, 40) >= 0);
    }
    /* Copy + an empty slot: the settings come with the clip; Undo: the slot as it was */
    M.seq.scene_off = 4;                                /* (rows 5..8: clip 8 on the pads' row 4) */
    button(MR_CC_COPY, 1), hit(PAD(5, 3)), hit(PAD(6, 3)), button(MR_CC_COPY, 0);
    CHECK(M.seq.cset[SEQ_CLIP(6, 7)] == RFM_MOVE_LQ_CLIP + 1 + RFM_MOVE_CS_AFTER_NEXT && clip_note(6, 7, 40));
    press(MR_CC_UNDO);
    CHECK(!M.seq.cset[SEQ_CLIP(6, 7)] && !clip_has(6, 7));
}

/* ================================================================ (0.12.0) a select step held: the clip's settings, the level === */
static int has_text(int y0, int y1) { return lit_rows(y0, y1) > 20; }
static void test_sv_clipset(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    static uint8_t g[MR_SCREEN_BYTES];
    int i, l1, l0;
    session_view();
    owners_grid();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    rest(), clear();
    screen(), memcpy(g, SCR, sizeof g);
    /* a quick click on column 2's select step: it selects, nothing else shows (before, while, after) */
    step(2, 1), adv_ms(100);
    CHECK(M.seq.sel_track == 1 && !rfmv_seq_cs_on(&M));
    step(2, 0), adv_ms(400);
    screen();
    CHECK(!rfmv_seq_cs_on(&M) && !memcmp(g, SCR, sizeof g) && !strstr(said, "Launch"));
    /* held 300 ms: track 2's selected clip's settings while it is down; knobs 1, 2 lit, 3..8 dark; said */
    clear();
    step(2, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS - 60);
    CHECK(!rfmv_seq_cs_on(&M));
    adv_ms(100);
    CHECK(rfmv_seq_cs_on(&M));
    CHECKF(strstr(said, "Track 2 clip 1, Launch: Global, After: Loop") != NULL, "said \"%s\"", said);
    screen(), leds(w);
    write_pbm("clipset");
    CHECK(lit(w[L_RING0 + 0]) && lit(w[L_RING0 + 1]) && !lit(w[L_RING0 + 2]) && !lit(w[L_RING0 + 7]));
    CHECK(has_text(0, 16) && has_text(18, 64) && memcmp(g, SCR, sizeof g));
    /* knob 1 touched: its name and value (the header's tick long, the row underlined); turned: Instant, then 1 Bar */
    clear(), raw(0x90, 0, 127);
    CHECKF(strstr(said, "Launch: Global") != NULL, "said \"%s\"", said);
    screen();
    write_pbm("clipset_launch");
    CHECK(px(3, 11) && px(12, 11) && !px(19, 12) && px(22, 11));
    clear(), knob(0, RFM_MOVE_LIST_TICKS);
    CHECKF(M.seq.cset[SEQ_CLIP(1, 0)] == RFM_MOVE_LQ_INSTANT + 1 && strstr(said, "Launch: Instant") != NULL, "said \"%s\"", said);
    knob(0, 2 * RFM_MOVE_LIST_TICKS);
    CHECK(M.seq.cset[SEQ_CLIP(1, 0)] == RFM_MOVE_LQ_BAR + 1);
    raw(0x80, 0, 0);
    /* knob 2: After Next; knobs 3..8: dead (no popup, no select, nothing) */
    clear(), knob(1, RFM_MOVE_LIST_TICKS);
    CHECKF((M.seq.cset[SEQ_CLIP(1, 0)] & RFM_MOVE_CS_AFTER_NEXT) && strstr(said, "After: Next") != NULL, "said \"%s\"", said);
    screen();
    write_pbm("clipset_after");
    raw(0x90, 4, 127), knob(4, 5), raw(0x80, 4, 0);
    CHECK(M.seq.sel_track == 1 && M.pop != POP_TRKINFO && M.pop != POP_CC && M.seq.cset[SEQ_CLIP(1, 0)] == (RFM_MOVE_LQ_BAR + 1 | RFM_MOVE_CS_AFTER_NEXT));
    /* the pads still work while it is held */
    now_ms += 1000, hit(PAD(0, 1));
    CHECK(M.seq.sel_track == 0 && rfmv_seq_cs_on(&M));
    /* let go: the grid again; nothing at the release */
    step(2, 0), adv_ms(20);
    CHECK(!rfmv_seq_cs_on(&M) && M.seq.cset[SEQ_CLIP(1, 0)] == (RFM_MOVE_LQ_BAR + 1 | RFM_MOVE_CS_AFTER_NEXT));
    leds(w);
    CHECK(lit(w[L_RING0 + 3]));                         /* (the knobs as session view's again) */
    press(MR_CC_UNDO);
    CHECK(M.seq.cset[SEQ_CLIP(1, 0)] == RFM_MOVE_LQ_BAR + 1);   /* (one undo a change) */
    rest();
    /* a knob touched before the 300 ms: shown at once */
    step(6, 1), raw(0x90, 1, 127);
    CHECK(rfmv_seq_cs_on(&M) && M.seq.sel_track == 3);
    raw(0x80, 1, 0), step(6, 0), rest();
    /* an empty selected slot: "No clip", the knobs dark and dead */
    step(8, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 50);
    screen(), leds(w);
    write_pbm("clipset_empty");
    CHECK(rfmv_seq_cs_on(&M) && M.seq.sel_track == 4 && !lit(w[L_RING0 + 0]) && !lit(w[L_RING0 + 1]) && has_text(24, 48));
    knob(0, 3 * RFM_MOVE_LIST_TICKS);
    CHECK(!M.seq.cset[SEQ_CLIP(4, 0)] && !clip_has(4, 0));
    step(8, 0), rest();
    /* the volume knob: the level of the firmware the track plays: the one played (felucca, slot 2) when it reaches it,
     * named in the level popup; else the lowest slot it reaches; none: "Not routed", nothing changes */
    step(2, 1);
    l1 = M.fw.level[1], l0 = M.fw.level[0];
    clear(), knob(8, -6);
    CHECK(M.fw.level[1] == l1 - 6 && M.fw.level[0] == l0 && rfmv_seq_cs_on(&M) && find(RFM_MOVE_ACT_FW_LEVEL) && !count(RFM_MOVE_ACT_MASTER));
    screen();
    write_pbm("clipset_level");
    M.seq.fx.ffilt[1] &= (uint16_t)~(1u << 1);
    knob(8, -3);
    CHECK(M.fw.level[0] == l0 - 3 && M.fw.level[1] == l1 - 6);
    for (i = 0; i < RFM_MOVE_FW_SLOTS; i++)
        M.seq.fx.ffilt[i] &= (uint16_t)~(1u << 1);
    clear(), knob(8, -3);
    screen();
    write_pbm("clipset_notrouted");
    CHECKF(M.fw.level[0] == l0 - 3 && M.fw.level[1] == l1 - 6 && !count(RFM_MOVE_ACT_MASTER) && strstr(said, "Not routed"), "said \"%s\"", said);
    step(2, 0);
    for (i = 0; i < RFM_MOVE_FW_SLOTS; i++)
        M.seq.fx.ffilt[i] |= (uint16_t)(1u << 1);
    /* the volume alone (no step held): MASTER, as before */
    rest(), clear(), knob(8, 1);
    CHECK(count(RFM_MOVE_ACT_MASTER) == 1);
}

/* ================================================================ (0.12.0, r2) a double click stops only what plays === */
static void test_sv_dbl_stop(void)
{
    int t;
    session_view();
    owners_grid();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    hit(SCN(0)), adv_ms(700), bar_start();
    CHECK(M.seq.tr[0].active == 0 && M.seq.tr[4].active == -1);
    /* an empty slot of a track that plays: a click stops it at the bar, a double click at once */
    now_ms += 1000, hit(PAD(0, 3));
    CHECK(M.seq.tr[0].queued == SEQ_STOP);
    hit(PAD(0, 3));
    CHECK(M.seq.tr[0].active == -1 && M.seq.tr[0].queued == SEQ_NONE);
    /* .. of a track that plays nothing (track 5): nothing changes, nothing said of a stop */
    rest(), clear();
    now_ms += 1000, hit(PAD(4, 2)), hit(PAD(4, 2));
    CHECK(M.seq.tr[4].active == -1 && M.seq.tr[4].queued == SEQ_NONE && M.seq.tr[4].sel == 2 && !clip_has(4, 2));
    CHECKF(!strstr(said, "stop") && !strstr(said, "Stop"), "said \"%s\"", said);
    for (t = 1; t < 4; t++)
        CHECK(M.seq.tr[t].active == 0 && M.seq.tr[t].queued == SEQ_NONE);
    /* Shift + a double click on a scene: its playing clips stop at once */
    now_ms += 1000;
    button(MR_CC_SHIFT, 1), hit(SCN(0)), hit(SCN(0)), button(MR_CC_SHIFT, 0);
    for (t = 1; t < 4; t++)
        CHECKF(M.seq.tr[t].active == -1 && M.seq.tr[t].queued == SEQ_NONE, "track %d", t + 1);
    /* .. a scene nothing of which plays (row 2, all stopped now; row 4 empty): nothing changes, nothing said */
    hit(SCN(2)), adv_ms(2100);                          /* (row 3 plays: tracks 1 and 3) */
    rest(), clear();
    now_ms += 1000;
    button(MR_CC_SHIFT, 1), hit(SCN(1)), hit(SCN(1)), hit(SCN(3)), button(MR_CC_SHIFT, 0);
    CHECK(M.seq.tr[0].active == 2 && M.seq.tr[2].active == 2 && M.seq.tr[0].queued == SEQ_NONE && M.seq.tr[2].queued == SEQ_NONE);
    CHECKF(!strstr(said, "stop") && !strstr(said, "Stop"), "said \"%s\"", said);
    CHECK(M.seq.playing);
}

/* ---- (0.12.0) the arp package: the track view (a firmware's Track tapped), speech on */
static void track_view(void)
{
    session_view();
    press(MR_CC_MENU), rest();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    CHECK(!M.seq.view && M.s.mode == RFM_MOVE_MODE_SEQ);
    rest(), clear();
}
static void led_flush(void)                             /* everything sent: from now only the changes come out */
{
    rfm_move_led_t o[RFM_MOVE_NLEDS];
    rfm_move_leds_invalidate(&M);
    (void)rfm_move_leds(&M, o, RFM_MOVE_NLEDS);
}
static void led_msgs(int *steps, int *pads)             /* the LED writes due now, counted */
{
    rfm_move_led_t o[RFM_MOVE_NLEDS];
    const int n = rfm_move_leds(&M, o, RFM_MOVE_NLEDS);
    int i;
    for (i = 0; i < n; i++) {
        if (o[i].cc || !o[i].rgb)
            continue;
        if (o[i].number >= MR_NOTE_STEP0 && o[i].number < MR_NOTE_STEP0 + 16)
            ++*steps;
        else if (o[i].number >= MR_NOTE_PAD0 && o[i].number < MR_NOTE_PAD0 + 32)
            ++*pads;
    }
}
static int full(const uint8_t c[3]) { return white(c) && c[0] > 200; }
static int pixels(int x0, int y0, int w, int h)
{
    int x, y, n = 0;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            n += px(x, y);
    return n;
}
static void shift_step10(int hold_ms)                   /* Shift + step 11, let go after hold_ms */
{
    button(MR_CC_SHIFT, 1), step(10, 1), adv_ms((uint32_t)hold_ms), step(10, 0), button(MR_CC_SHIFT, 0);
    now_ms += RFM_MOVE_DOUBLE_MS;                       /* (no Shift double tap) */
}

/* n2: Shift + step 11 gated (a click opens the menu at the release, a hold toggles the arp at 300 ms, the menu as
 * it was); the jog's click and the hold: "Arp On" / "Arp Off" the whole screen; off: the menu's pictures dimmed */
static void test_arp_toggle(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    rfm_move_arp_t *ap;
    int on_px, off_px;
    track_view();
    ap = &M.seq.tr[M.seq.sel_track].arp;
    CHECK(ap->mode == RFM_MOVE_AM_OFF);
    /* the press: white, nothing else */
    button(MR_CC_SHIFT, 1), step(10, 1);
    leds(w);
    CHECK(white(w[L_STEP0 + 10]) && !M.seq.smenu && ap->mode == RFM_MOVE_AM_OFF && !rfmv_pop_up(&M, POP_ARPW));
    adv_ms(150);
    CHECK(!M.seq.smenu && ap->mode == RFM_MOVE_AM_OFF);
    /* a click: the menu at the release; nothing of the hold */
    step(10, 0);
    CHECK(M.seq.smenu == RFM_MOVE_SM_ARP && ap->mode == RFM_MOVE_AM_OFF && !rfmv_pop_up(&M, POP_ARPW));
    /* again: closed (the Move's) */
    step(10, 1), adv_ms(100), step(10, 0);
    CHECK(!M.seq.smenu);
    /* a hold: at 300 ms, still down, the arp on, the menu not opened; "Arp On" up, the icon full at once */
    clear();
    step(10, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS - 30);
    CHECK(ap->mode == RFM_MOVE_AM_OFF && !rfmv_pop_up(&M, POP_ARPW));
    adv_ms(40);
    leds(w);
    CHECK(ap->mode == RFM_MOVE_AM_ON && !M.seq.smenu && rfmv_pop_up(&M, POP_ARPW) && w[L_ICON0 + 10][0] == 127);
    CHECKF(strstr(said, "Arp On") != NULL, "said %s", said);
    write_pbm("arp_toggle_pop");
    step(10, 0);                                        /* (its release: nothing) */
    CHECK(!M.seq.smenu && ap->mode == RFM_MOVE_AM_ON);
    leds(w);
    CHECK(w[L_ICON0 + 10][0] == 127);
    step(10, 1), adv_ms(320), step(10, 0);              /* (held again: off; "Arp Off") */
    CHECK(ap->mode == RFM_MOVE_AM_OFF && !M.seq.smenu && rfmv_pop_up(&M, POP_ARPW));
    leds(w);
    CHECK(w[L_ICON0 + 10][0] == 24);
    write_pbm("arp_toggle_offpop");
    /* the menu open: a click closes it, a hold toggles and leaves it open */
    step(10, 1), step(10, 0);
    CHECK(M.seq.smenu == RFM_MOVE_SM_ARP);
    step(10, 1), adv_ms(320);
    CHECK(ap->mode == RFM_MOVE_AM_ON && M.seq.smenu == RFM_MOVE_SM_ARP);
    step(10, 0);
    CHECK(M.seq.smenu == RFM_MOVE_SM_ARP && ap->mode == RFM_MOVE_AM_ON);
    button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    /* the jog's click: the popup at once; off, the walk dimmed (a checker), the header plain */
    rest();
    screen();
    on_px = pixels(0, 30, 64, 30);
    write_pbm("arp_on_main");
    clear();
    jog_click();
    CHECK(ap->mode == RFM_MOVE_AM_OFF && rfmv_pop_up(&M, POP_ARPW));
    CHECKF(strstr(said, "Arp Off") != NULL, "said %s", said);
    rest();
    screen();
    off_px = pixels(0, 30, 64, 30);
    CHECKF(off_px > 0 && off_px * 3 < on_px * 2, "walk on %d, off %d", on_px, off_px);
    write_pbm("arp_offdim");
    jog(1), rest();                                     /* (a lane while off: dimmed too) */
    screen();
    off_px = pixels(0, 19, 128, 37);
    write_pbm("arp_offdim_lane");
    jog_click(), rest();
    screen();
    on_px = pixels(0, 19, 128, 37);
    CHECKF(off_px > 0 && off_px * 3 < on_px * 2, "lane on %d, off %d", on_px, off_px);
    write_pbm("arp_on_lane");
    back();
    CHECK(!M.seq.smenu);
}

/* n3: the lanes take the step keys (a flash and a wave), a held lane step takes the pads, step 1 + step n the length */
static void test_arp_lanes(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3], w0[RFM_MOVE_NLEDS][3];
    rfm_move_arp_t *ap;
    int clip, i, L, t, ns, np, peak, n0, ok;
    track_view();
    ap = &M.seq.tr[M.seq.sel_track].arp;
    clip = SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel);
    rfmv_seq_note_put(&M, clip, 2, 60, 100, 24, 0);
    shift_step10(20);
    CHECK(M.seq.smenu == RFM_MOVE_SM_ARP && M.seq.arp_page == 0);
    ap->mode = RFM_MOVE_AM_ON;                          /* (on: the pictures plain, as the board shows them) */
    /* the main page leaves the step keys to the clip: a tap enters a note */
    step(6, 1), step(6, 0);
    CHECK(rfmv_seq_find(&M, clip, 6, 60) >= 0 || rfmv_seq_range(&M, clip, 6, &n0) > n0);
    step(6, 1), step(6, 0);
    CHECK(rfmv_seq_range(&M, clip, 6, &n0) == n0);
    leds(w0);
    /* the lane page: a flash, then a wave left to right; the LED writes counted (steps: 32 in all, 16 at most at once) */
    led_flush();
    jog(1);
    CHECK(M.seq.arp_page == 1);
    ns = np = 0, peak = 0;
    for (t = 0; t < 600; t += 10) {
        int s1 = 0, p1 = 0;
        led_msgs(&s1, &p1);
        ns += s1, np += p1, peak = s1 > peak ? s1 : peak;
        adv_ms(10);
    }
    printf("arp wave: %d step LED writes, at most %d in a frame\n", ns, peak);
    CHECKF(ns <= 34 && peak <= 16 && np == 0, "wave: %d step writes, %d at once, %d pad writes", ns, peak, np);
    leds(w);
    for (i = 0, ok = 1; i < 16; i++)
        ok &= !full(w[L_STEP0 + i]) || i == M.seq.arp_sel;
    CHECK(ok);                                          /* (settled: the lane's lights) */
    /* back on the main page: the clip's lights at once (no wave) */
    jog(-1);
    leds(w);
    for (i = 0, ok = 1; i < 16; i++)
        ok &= !memcmp(w[L_STEP0 + i], w0[L_STEP0 + i], 3);
    CHECK(ok);
    /* the wave's frames */
    jog(1);
    adv_ms(60);
    leds(w);
    for (i = 0, ok = 1; i < 16; i++)
        ok &= full(w[L_STEP0 + i]);
    CHECK(ok);                                          /* (the flash: all 16) */
    write_pbm("arp_wave1");
    adv_ms(100);                                        /* (160: 4 settled) */
    leds(w);
    for (i = 4, ok = 1; i < 16; i++)
        ok &= full(w[L_STEP0 + i]);
    CHECK(ok && !full(w[L_STEP0 + 1]));
    write_pbm("arp_wave2");
    adv_ms(160);                                        /* (320: 12 settled) */
    leds(w);
    for (i = 12, ok = 1; i < 16; i++)
        ok &= full(w[L_STEP0 + i]);
    CHECK(ok && !full(w[L_STEP0 + 9]));
    write_pbm("arp_wave3");
    adv_ms(100);
    leds(w);
    CHECK(!full(w[L_STEP0 + 9]) && !full(w[L_STEP0 + 15]));
    /* a step pressed during the wave works at once and ends it */
    jog(-1), jog(1), adv_ms(30);
    step(5, 1);
    leds(w);
    CHECK(M.seq.arp_sel == 5 && full(w[L_STEP0 + 5]) && !full(w[L_STEP0 + 9]));
    step(5, 0), rest();
    /* step 1 held + step 12: the length 12 at that press (step 12 not picked); the steps past it dark at once */
    step(0, 1), step(11, 1);
    leds(w);
    CHECK(ap->mlen == 12 && M.seq.arp_sel == 0 && !lit(w[L_STEP0 + 12]) && !lit(w[L_STEP0 + 15]) && lit(w[L_STEP0 + 11]));
    CHECKF(strstr(said, "Length: 12") != NULL, "said %s", said);
    write_pbm("arp_len");
    step(11, 0);
    step(11, 1), step(11, 0);                           /* (again: 1) */
    CHECK(ap->mlen == 1);
    step(15, 1), step(15, 0);                           /* (16) */
    CHECK(ap->mlen == 16);
    step(3, 1), step(3, 0);                             /* (4) */
    adv_ms(400);                                        /* (step 1 held past 300 ms after a length: no pads) */
    CHECK(ap->mlen == 4 && !M.seq.al_pads);
    step(0, 0);
    /* a step past the length: dark and dead (nothing picked, nothing reaches the clip) */
    n0 = rfmv_seq_clip_used(&M, clip);
    step(9, 1), step(9, 0);
    CHECK(M.seq.arp_sel == 0 && rfmv_seq_clip_used(&M, clip) == n0 && rfmv_seq_range(&M, clip, 9, &L) == L);
    knob(1, RFM_MOVE_LIST_TICKS * 12);
    CHECK(ap->mlen == 16);
    rest();
    /* a quick click on a lane step: picked; the pads never change */
    leds(w0);
    step(4, 1), adv_ms(150);
    leds(w);
    CHECK(!memcmp(w[L_PAD0], w0[L_PAD0], 32 * 3) && M.seq.arp_sel == 4);
    step(4, 0);
    leds(w);
    CHECK(!memcmp(w[L_PAD0], w0[L_PAD0], 32 * 3) && !M.seq.al_pads);
    /* held 300 ms: the pads are its MOD shortcuts (the current one white, the notes' row lit, the rest dark) */
    ap->lane[0][4] = RFM_MOVE_AL_PLAY;
    step(4, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    leds(w);
    CHECK(M.seq.al_pads && white(w[L_PAD0 + 0]) && lit(w[L_PAD0 + 2]) && !white(w[L_PAD0 + 2]) && lit(w[L_PAD0 + 13]) &&
          !lit(w[L_PAD0 + 14]) && !lit(w[L_PAD0 + 16]) && !lit(w[L_PAD0 + 31]));
    clear();
    hit(2);                                             /* (Tie: at the press, no note played) */
    leds(w);
    CHECK(ap->lane[0][4] == RFM_MOVE_AL_TIE && count(RFM_MOVE_ACT_MIDI) == 0 && white(w[L_PAD0 + 2]) && !white(w[L_PAD0]));
    CHECKF(strstr(said, "Step 5: Tie") != NULL, "said %s", said);
    hit(10);                                            /* (Note 3) */
    CHECK(ap->lane[0][4] == RFM_MOVE_AL_N1 + 2);
    hit(20);                                            /* (dark: dead) */
    CHECK(ap->lane[0][4] == RFM_MOVE_AL_N1 + 2 && count(RFM_MOVE_ACT_MIDI) == 0);
    hit(2);
    write_pbm("arp_modpads");
    step(4, 0);                                         /* (let go: the pads back at once) */
    leds(w);
    CHECK(!M.seq.al_pads && !memcmp(w[L_PAD0], w0[L_PAD0], 32 * 3));
    /* a pad pressed while the step is down (before 300 ms): the shortcut at once */
    step(7, 1), adv_ms(50), hit(9);
    CHECK(ap->lane[0][7] == RFM_MOVE_AL_N1 + 1 && M.seq.al_pads && count(RFM_MOVE_ACT_MIDI) == 0);
    step(7, 0);
    /* a note held on a pad when the takeover begins: it plays on and its release lets it go (no hanging note) */
    ap->mode = RFM_MOVE_AM_OFF;                         /* (off: the pad plays its note, not the arp) */
    clear();
    pad(27, 100);
    CHECK(count(RFM_MOVE_ACT_MIDI) == 1);
    step(3, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    CHECK(M.seq.al_pads);
    pad(27, 0);
    CHECK(count(RFM_MOVE_ACT_MIDI) == 2);
    step(3, 0);
    ap->mode = RFM_MOVE_AM_ON;
    /* every MOD function draws its sign, at every length, at rest and with knob 1 touched */
    for (L = 1; L <= 16; L++) {
        int k;
        ap->mlen = (uint8_t)L;
        for (i = 0; i < 16; i++)
            ap->lane[0][i] = (int8_t)((i + L) % RFM_MOVE_AL_N);
        for (k = 0; k < 2; k++) {
            if (k)
                raw(0x90, 0, 127);
            screen();
            for (i = 0, ok = 1; i < 16; i++) {
                const int sg = pixels(i * 8 - 1, 22, 9, 22);
                ok &= i < L ? sg > 0 : sg <= 4;
                if (i < L && !sg)
                    fprintf(stderr, "lane length %d: step %d (%d) has no sign\n", L, i + 1, ap->lane[0][i]);
            }
            CHECKF(ok, "length %d touched %d", L, k);
            if (k)
                raw(0x80, 0, 0);
        }
        rest();
    }
    for (i = 0; i < 16; i++)
        ap->lane[0][i] = (int8_t)(i % RFM_MOVE_AL_N);
    ap->mlen = 16;
    screen();
    write_pbm("arp_mod_all");
    /* TRANSPOSE: the pads are -16..+15 from the bottom-left; 0 and the octaves marked; a press sets it */
    jog(1), rest();
    CHECK(M.seq.arp_page == 2);
    for (i = 0; i < 16; i++)
        ap->lane[1][i] = 0;
    step(2, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    leds(w);
    CHECK(M.seq.al_pads && white(w[L_PAD0 + 16]) && green(w[L_PAD0 + 28]) && green(w[L_PAD0 + 4]) && lit(w[L_PAD0 + 0]) &&
          !green(w[L_PAD0 + 0]));
    clear();
    hit(23);
    leds(w);
    CHECK(ap->lane[1][2] == 7 && white(w[L_PAD0 + 23]) && !white(w[L_PAD0 + 16]) && count(RFM_MOVE_ACT_MIDI) == 0);
    CHECKF(strstr(said, "Step 3: +7") != NULL, "said %s", said);
    write_pbm("arp_transpads");
    hit(0);
    CHECK(ap->lane[1][2] == -16);
    ap->lane[1][2] = 20;                                /* (out of the pads' range: none white) */
    leds(w);
    for (i = 0, ok = 1; i < 32; i++)
        ok &= !white(w[L_PAD0 + i]);
    CHECK(ok);
    step(2, 0);
    for (i = 0; i < 16; i++)
        ap->lane[1][i] = (int8_t)((i * 7) % 25 - 12);
    screen();
    for (i = 0, ok = 1; i < 16; i++)
        ok &= pixels(i * 8 + 1, 22, 6, 33) > 0;
    CHECK(ok);                                          /* (every value drawn) */
    back();
}

/* a9: Up / Down in track view: "Octave +1" with the pads' notes, a white row sweeping the way it went */
static void test_octave(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int t, np, peak, i, ok, row;
    track_view();
    CHECK(M.s.octave == 0);
    led_flush();
    press(MR_CC_UP);
    CHECK(M.s.octave == 1 && rfmv_pop_up(&M, POP_OCT));
    CHECKF(strstr(said, "Octave 1, ") != NULL, "said %s", said);
    np = peak = 0;
    for (t = 0; t < 300; t += 10) {                     /* (the writes: a row a frame, few) */
        int s1 = 0, p1 = 0;
        led_msgs(&s1, &p1);
        np += p1, peak = p1 > peak ? p1 : peak;
        adv_ms(10);
    }
    printf("octave sweep: %d pad LED writes, at most %d in a frame\n", np, peak);
    CHECKF(np <= 64 && peak <= 16, "sweep: %d pad writes, %d at once", np, peak);
    /* the frames: Up, from the bottom row; Down, from the top */
    press(MR_CC_UP);
    screen();
    write_pbm("octave_pop");
    for (row = 0; row < 4; row++) {
        leds(w);
        for (i = row * 8, ok = 1; i < row * 8 + 8; i++)
            ok &= white(w[L_PAD0 + i]);
        CHECKF(ok, "up: row %d", row);
        adv_ms(50);
    }
    leds(w);
    for (i = 0, ok = 0; i < 8; i++)
        ok += white(w[L_PAD0 + 24 + i]);
    CHECK(ok < 8);                                      /* (done: the pads' own colours) */
    rest();
    press(MR_CC_DOWN);
    leds(w);
    for (i = 24, ok = 1; i < 32; i++)
        ok &= white(w[L_PAD0 + i]);
    CHECK(ok && M.s.octave == 1);
    /* a pad pressed during the sweep plays at once */
    clear();
    pad(3, 100);
    CHECK(count(RFM_MOVE_ACT_MIDI) == 1);
    pad(3, 0), rest();
    /* at the range's end: the popup (unchanged), no sweep */
    M.s.octave = RFM_MOVE_OCT_MAX;
    rest(), clear();
    press(MR_CC_UP);
    leds(w);
    for (i = 0, ok = 0; i < 8; i++)
        ok += white(w[L_PAD0 + i]);
    CHECK(M.s.octave == RFM_MOVE_OCT_MAX && rfmv_pop_up(&M, POP_OCT) && ok < 8);
    screen();
    write_pbm("octave_max");
    M.s.octave = 0;
    rest();
}

/* a11: a held step: Up / Down transposes it and shows its notes; step 1 held + step 2 toggles its length 2 <-> 1 */
static void test_held_step(void)
{
    int clip, lo;
    track_view();
    clip = SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel);
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 24, 0);
    rfmv_seq_note_put(&M, clip, 0, 64, 100, 24, 0);
    rfmv_seq_note_put(&M, clip, 0, 67, 100, 24, 0);
    step(0, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    clear();
    press(MR_CC_UP);
    CHECK(rfmv_seq_find(&M, clip, 0, 61) >= 0 && M.pop == POP_TRIG && M.pop_i == 8 && rfmv_pop_up(&M, POP_TRIG));
    CHECKF(strstr(said, "Db3 F3 Ab3") != NULL, "said %s", said);
    press(MR_CC_DOWN), press(MR_CC_DOWN);
    CHECK(rfmv_seq_find(&M, clip, 0, 59) >= 0);
    screen();
    write_pbm("held_transpose");
#define LEN0() (rfmv_seq_range(&M, clip, 0, &lo) > lo ? M.seq.notes[lo].len : -1)
    /* the owner's three cases (step 1 held, step 2 pressed) and a wider span */
    clear();
    step(1, 1), step(1, 0);                             /* (1 step: 2) */
    CHECK(LEN0() == 48 && M.pop == POP_TRIG && M.pop_i == 6 && rfmv_pop_up(&M, POP_TRIG));
    CHECKF(strstr(said, "Length") != NULL, "said %s", said);
    screen();
    write_pbm("held_len2");
    step(1, 1), step(1, 0);                             /* (2: 1) */
    CHECK(LEN0() == 24);
    step(1, 1), step(1, 0);                             /* (1: 2 again) */
    CHECK(LEN0() == 48);
    step(3, 1), step(3, 0);                             /* (step 4: 4 steps) */
    CHECK(LEN0() == 96);
    step(1, 1), step(1, 0);                             /* (more than 2: 2) */
    CHECK(LEN0() == 48);
    step(3, 1), step(3, 0), step(3, 1), step(3, 0);     /* (step 4 twice: 4, then 1) */
    CHECK(LEN0() == 24);
    rfmv_seq_note_put(&M, clip, 0, 59, 100, 40, 0);     /* (between 1 and 2 steps: 1) */
    step(1, 1), step(1, 0);
    CHECK(LEN0() == 24);
#undef LEN0
    step(0, 0);
    CHECK(rfmv_seq_range(&M, clip, 0, &lo) > lo);       /* (the held step kept its notes) */
}

/* ================================================================ package C (0.12.0): buttons, menus, held controls === */
static void touch(int k, int on) { raw(on ? 0x90 : 0x80, (uint8_t)k, on ? 127 : 0); }
static uint8_t SAVED[MR_SCREEN_BYTES];
static void scr_save(void) { screen(), memcpy(SAVED, SCR, sizeof SAVED); }
static int scr_same(void) { screen(); return !memcmp(SAVED, SCR, sizeof SAVED); }
static int ring_menu(const uint8_t c[3]) { return c[0] > 2 * c[1] && c[1] > 0 && c[2] == 0 && c[0] >= 100; }   /* (deep
                                                         * orange: a menu's knob) */
static int rings_menu(int bits)                         /* the knob lights are the menu's: its knobs orange, the rest off */
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int k, ok = 1;
    leds(w);
    for (k = 0; k < 8; k++)
        ok &= (bits >> k & 1) ? ring_menu(w[L_RING0 + k]) : !lit(w[L_RING0 + k]);
    return ok;
}
static void shift_step(int s) { button(MR_CC_SHIFT, 1), step(s, 1), step(s, 0), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS; }

/* l3 + a17: with a Shift menu open, a held step, Loop, Copy, Delete, Mute and Shift take over while held; the menu is
 * back at their release with its knobs, jog and screen; a Track tap's popup over the menu, then the menu */
static void test_held_over_menu(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    rfm_move_track_t *t;
    int clip, lo = 0, ch, len;
    track_view();
    t = &M.seq.tr[M.seq.sel_track];
    clip = SEQ_CLIP(M.seq.sel_track, t->sel);
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 24, 0);
    shift_step(2);                                      /* Workflow: knobs 1..3 */
    CHECK(M.seq.smenu == RFM_MOVE_SM_WORKFLOW);
    rest(), scr_save();
    CHECK(rings_menu(0x07));
    write_pbm("over_menu_rest");
    ch = t->ch;
    /* a held step: its popup at 300 ms over the menu; the knobs, jog, volume, arrows its; the menu stays */
    step(0, 1);
    CHECK(rfmv_seq_held_takes(&M) && !rings_menu(0x07));
    adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    CHECK(rfmv_pop_up(&M, POP_STEP) && !scr_same());
    write_pbm("over_menu_step");
    knob(0, 3);                                         /* (the TRIG page: micro) */
    CHECK(t->ch == ch && rfmv_seq_range(&M, clip, 0, &lo) > lo && M.seq.notes[lo].micro == 3 && rfmv_pop_up(&M, POP_TRIG));
    jog(2);
    CHECK(t->ch == ch && M.seq.notes[lo].len == 24 + 4 && M.pop == POP_TRIG && M.pop_i == 6);
    knob(8, 5);
    CHECK(M.seq.notes[lo].vel == 105);
    press(MR_CC_UP);
    CHECK(rfmv_seq_find(&M, clip, 0, 61) >= 0);
    write_pbm("over_menu_step_turn");
    step(0, 0);
    CHECK(M.seq.smenu == RFM_MOVE_SM_WORKFLOW && rings_menu(0x07));
    rest();
    CHECK(scr_same());
    knob(0, RFM_MOVE_LIST_TICKS);                       /* (the menu's knob again: the channel) */
    CHECK(t->ch == ch + 1);
    rest(), scr_save();
    /* Loop held: its pages picture and the jog (the clip's length); the menu keeps its knobs, lit */
    len = M.seq.len[clip];
    button(MR_CC_LOOP, 1);
    CHECK(!scr_same() && rings_menu(0x07));
    write_pbm("over_menu_loop");
    jog(1);
    CHECK(M.seq.len[clip] == len + 1 && M.seq.smenu == RFM_MOVE_SM_WORKFLOW);
    knob(0, -RFM_MOVE_LIST_TICKS);
    CHECK(t->ch == ch);
    button(MR_CC_LOOP, 0);
    CHECK(!M.seq.loop_latch && M.seq.smenu == RFM_MOVE_SM_WORKFLOW);
    rest(), scr_save();
    /* Loop's quick click under the menu: latched, shown over it; Back closes the latch first, then the menu */
    press(MR_CC_LOOP);
    CHECK(M.seq.loop_latch && !scr_same() && rings_menu(0x07));
    back();
    CHECK(!M.seq.loop_latch && M.seq.smenu == RFM_MOVE_SM_WORKFLOW && scr_same());
    /* Copy, Delete held: the track screen with their icon; Mute held: the 16 tracks, a step mutes */
    button(MR_CC_COPY, 1);
    CHECK(!scr_same() && rings_menu(0x07) && rfmv_seq_held_icon(&M) == 2);
    write_pbm("over_menu_copy");
    button(MR_CC_COPY, 0);
    CHECK(scr_same());
    button(MR_CC_DELETE, 1);
    CHECK(!scr_same() && rfmv_seq_held_icon(&M) == 1);
    button(MR_CC_DELETE, 0);
    rest();
    CHECK(scr_same());
    button(MR_CC_MUTE, 1);
    CHECK(!scr_same() && rings_menu(0x07));
    write_pbm("over_menu_mute");
    step(5, 1), step(5, 0);
    CHECK(M.seq.tr[5].mute && M.seq.smenu == RFM_MOVE_SM_WORKFLOW);
    button(MR_CC_MUTE, 0);
    rest();
    CHECK(scr_same() && rings_menu(0x07) && M.seq.smenu == RFM_MOVE_SM_WORKFLOW);
    rfmv_seq_mute(&M, 5, 0);
    /* Shift held: the shortcuts' icons; Shift + another menu's step switches, + its own closes it */
    button(MR_CC_SHIFT, 1);
    leds(w);
    CHECK(w[L_ICON0 + 2][0] == 127 && w[L_ICON0 + 6][0] == 24);
    step(6, 1), step(6, 0);
    CHECK(M.seq.smenu == RFM_MOVE_SM_GROOVE);
    step(6, 1), step(6, 0);
    CHECK(!M.seq.smenu);
    button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    /* a Track tap under the menu: the slot changes, its popup a second, the menu back (it stays open) */
    shift_step(2);
    rest(), scr_save();
    tap_track(0);
    CHECK(M.fw.active == 0 && M.seq.smenu == RFM_MOVE_SM_WORKFLOW && (rfmv_pop_up(&M, POP_LAYOUT) || rfmv_pop_up(&M, POP_TK)));
    screen();
    CHECK(lit_rows(0, 64) > 0);
    write_pbm("over_menu_tracktap");
    rest();
    CHECK(M.seq.smenu == RFM_MOVE_SM_WORKFLOW && rings_menu(0x07));
    write_pbm("over_menu_tracktap_after");
    tap_track(1), rest();
    CHECK(M.fw.active == 1 && M.seq.smenu == RFM_MOVE_SM_WORKFLOW && scr_same());
    back();
    CHECK(!M.seq.smenu);
}

/* l2: Menu never closes a Shift menu: a note layout and session view flip under it, the FM-1 layout's Menu is HOME */
static void test_menu_stays(void)
{
    track_view();
    shift_step(4);                                      /* Tempo */
    CHECK(M.seq.smenu == RFM_MOVE_SM_TEMPO);
    press(MR_CC_MENU);
    CHECK(M.seq.view && M.seq.smenu == RFM_MOVE_SM_TEMPO);
    press(MR_CC_MENU);
    CHECK(!M.seq.view && M.seq.smenu == RFM_MOVE_SM_TEMPO);
    back();
    CHECK(!M.seq.smenu);
    shift_step(1);                                      /* Global settings: the same */
    CHECK(M.seq.smenu == RFM_MOVE_SM_GLOBAL);
    press(MR_CC_MENU);
    CHECK(M.seq.view && M.seq.smenu == RFM_MOVE_SM_GLOBAL);
    press(MR_CC_MENU), back();
    CHECK(!M.seq.smenu && !M.seq.view);
    /* the FM-1 layout (slot 1): Track 4 held + step 9 opens Keys & Scales; Menu: HOME, the menu stays */
    tap_track(0), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1);
    track(3, 1), step(8, 1), step(8, 0), track(3, 0), rest(), clear();
    CHECK(M.seq.smenu == RFM_MOVE_SM_KEYS);
    press(MR_CC_MENU);
    CHECK(M.seq.smenu == RFM_MOVE_SM_KEYS && find(RFM_MOVE_ACT_BTN) && find(RFM_MOVE_ACT_BTN)->index == RFM_MOVE_B_HOME);
    back();
    CHECK(!M.seq.smenu);
    tap_track(1), rest();
}

/* a5: every Shift menu names its knobs: the touched (or just turned) knob's name and value on top, a tick a knob in
 * use (the active one long); at rest the menu's own top; an unused knob touched: nothing changes */
static int tick(int k, int lng) { return lng ? px(3 + k * 16, 17) && px(12 + k * 16, 17) : px(6 + k * 16, 17) && !px(4 + k * 16, 17); }
static void menu_heads(const char *name, int bits)
{
    char nm[48];
    int k, ok;
    rest(), scr_save();
    snprintf(nm, sizeof nm, "menu_%s_rest", name);
    write_pbm(nm);
    for (k = 0, ok = 1; k < 8; k++)
        ok &= (bits >> k & 1) ? tick(k, 0) : !px(6 + k * 16, 17);
    CHECKF(ok && rings_menu(bits), "%s: ticks at rest", name);
    for (k = 0; k < 8; k++) {
        touch(k, 1);
        if (bits >> k & 1) {
            screen();
            CHECKF(tick(k, 1) && !scr_same(), "%s: knob %d touched", name, k + 1);
            snprintf(nm, sizeof nm, "menu_%s_k%d", name, k + 1);
            write_pbm(nm);
        } else
            CHECKF(scr_same() && !rfmv_pop_up(&M, M.pop), "%s: unused knob %d touched", name, k + 1);
        touch(k, 0), rest();
    }
    CHECKF(scr_same(), "%s: back at rest", name);
}
static void test_menu_heads(void)
{
    track_view();
    shift_step(2), menu_heads("workflow", 0x07);
    jog(1);                                             /* (the jog's value named: the channel) */
    CHECK(!scr_same());
    write_pbm("menu_workflow_jog");
    jog(-1), back();
    shift_step(4), menu_heads("tempo", 0x03), back();
    shift_step(6), menu_heads("groove", 0x03), back();
    shift_step(7), menu_heads("layout", 0x01), back();
    shift_step(8), menu_heads("keys", 0x0F), back();
    shift_step(9), menu_heads("velocity", 0x01);
    jog(1);
    CHECK(!scr_same());
    write_pbm("menu_velocity_jog");
    jog(-1), back();
    button(MR_CC_SHIFT, 1), press(MR_CC_REC), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    CHECK(M.seq.smenu == RFM_MOVE_SM_REC);
    menu_heads("record", 0x03), back();
    CHECK(!M.seq.smenu);
}

/* a6 + a7: a Shift menu over the Mixer, Routing and the FM-1 layout owns the knob lights, the touch's header (no
 * other popup), the turns and the jog, as in track view; Global settings over the Mixer: the jog its rows */
static void menu_over(const char *where, int sh_step, int bits)
{
    char nm[48];
    const int bpm = rfm_move_seq_bpm10(&M);
    button(MR_CC_SHIFT, 1), step(sh_step, 1), step(sh_step, 0), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    CHECKF(M.seq.smenu != 0, "%s: open", where);
    rest(), clear();
    CHECKF(rings_menu(bits), "%s: the menu's knob lights", where);
    touch(0, 1);
    screen();
    CHECKF(tick(0, 1) && M.pop != POP_ENC && M.pop != POP_TRKINFO && M.pop != POP_CC && M.pop != POP_TRIG, "%s: touch: header, pop %d",
           where, M.pop);
    snprintf(nm, sizeof nm, "menu_over_%s_k1", where);
    write_pbm(nm);
    knob(0, 1);
    touch(0, 0);
    CHECKF(!count(RFM_MOVE_ACT_TURN), "%s: no FM-1 turn", where);
    if (M.seq.smenu == RFM_MOVE_SM_TEMPO) {
        CHECKF(rfm_move_seq_bpm10(&M) == bpm + 10, "%s: knob 1 the tempo", where);
        jog(-1);
        CHECKF(rfm_move_seq_bpm10(&M) == bpm, "%s: the jog the tempo", where);
    }
    touch(5, 1);                                        /* (an unused knob: nothing) */
    CHECKF(M.pop != POP_ENC && M.pop != POP_TRKINFO, "%s: unused knob", where);
    touch(5, 0);
    back(), rest();
    CHECKF(!M.seq.smenu, "%s: Back closes", where);
}
static void test_menu_over_views(void)
{
    int row, ck, vm;
    track_view();
    menu_over("track", 4, 0x03);
    tap_track(3), rest();                               /* the Mixer */
    CHECK(M.seq.projects && M.fw.fx);
    menu_over("mixer", 4, 0x03);
    CHECK(M.seq.projects);
    /* Global settings over the Mixer: the jog moves through the rows; the knobs dark and dead */
    button(MR_CC_SHIFT, 1), step(1, 1), step(1, 0), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    CHECK(M.seq.smenu == RFM_MOVE_SM_GLOBAL);
    row = M.gl_row, ck = M.seq.clock_mode, vm = M.s.vel_mode;
    jog(1);
    CHECK(M.gl_row == row + 1 && M.seq.clock_mode == ck);
    knob(0, 3), knob(1, 3), knob(2, 3), knob(3, 3);
    CHECK(M.seq.clock_mode == ck && M.s.vel_mode == vm && rings_menu(0));
    write_pbm("menu_over_mixer_global");
    back(), rest();
    CHECK(!M.seq.smenu && M.seq.projects);
    tap_track(3), rest();                               /* Routing */
    CHECK(M.seq.projects && M.fw.fx);
    menu_over("routing", 2, 0x07);
    back(), rest();
    /* the FM-1 layout: Track 4 held + step 5 opens Tempo */
    tap_track(0), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && !M.seq.projects);
    track(3, 1), step(4, 1), step(4, 0), track(3, 0), rest(), clear();
    CHECK(M.seq.smenu == RFM_MOVE_SM_TEMPO && rings_menu(0x03));
    touch(4, 1);                                        /* (knob 5, K1 in the FM-1 layout: not the menu's: nothing) */
    CHECK(M.pop != POP_ENC);
    touch(4, 0);
    knob(4, 4);
    CHECK(!count(RFM_MOVE_ACT_TURN));
    back(), rest();
    track(3, 1), step(4, 1), step(4, 0), track(3, 0), rest(), clear();
    touch(0, 1);
    screen();
    CHECK(tick(0, 1) && M.pop != POP_ENC);
    write_pbm("menu_over_fm1_k1");
    knob(0, 2), touch(0, 0);
    CHECK(!count(RFM_MOVE_ACT_TURN));
    back(), tap_track(1), rest();
}

/* a8: Track 4 held is a whole layer: nothing of it before 300 ms (its own light only), all of it at 300 ms or at once
 * when something is pressed meanwhile; its dead controls dark and dead; a menu it opened owns the knobs */
static int icons_lit(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int i, n = 0;
    leds(w);
    for (i = 0; i < 16; i++)
        n += w[L_ICON0 + i][0] > 0;
    return n;
}
static void test_t4_layer(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3], w0[RFM_MOVE_NLEDS][3];
    int i, ok, oct, cv;
    track_view();
    rest(), scr_save(), leds(w0);
    /* down: only its own light; no icons, the screen and the pads as they were */
    track(3, 1);
    leds(w);
    CHECK(icons_lit() == 0 && scr_same() && bright(w[L_TRACK0 + 3]) >= bright(w0[L_TRACK0 + 3]));
    for (i = 0, ok = 1; i < 32; i++)
        ok &= !memcmp(w[L_PAD0 + i], w0[L_PAD0 + i], 3);
    CHECK(ok);
    adv_ms(150);
    CHECK(icons_lit() == 0 && scr_same());
    /* 300 ms: the whole layer at once: the screen, the icons, pads 1 and 2 only, the knobs and dead buttons dark */
    adv_ms(170);
    leds(w);
    CHECK(icons_lit() > 0 && !scr_same() && lit(w[L_PAD0 + 24]) && lit(w[L_PAD0 + 25]));
    for (i = 0, ok = 1; i < 32; i++)
        ok &= i == 24 || i == 25 || !lit(w[L_PAD0 + i]);
    for (i = 0; i < 8; i++)
        ok &= !lit(w[L_RING0 + i]);
    CHECK(ok);
    CHECK(!w[L_WHITE0 + W_UP][0] && !w[L_WHITE0 + W_DOWN][0] && !w[L_WHITE0 + W_LEFT][0] && !w[L_WHITE0 + W_RIGHT][0] &&
          !w[L_WHITE0 + W_MENU][0] && !w[L_WHITE0 + W_COPY][0] && !w[L_WHITE0 + W_DELETE][0] && !w[L_WHITE0 + W_MUTE][0] &&
          w[L_WHITE0 + W_LOOP][0] && w[L_WHITE0 + W_UNDO][0]);
    write_pbm("t4_layer");
    /* dead: Up, Left, Menu, Mute, Copy, a knob, the jog */
    oct = M.s.octave, cv = M.seq.page;
    press(MR_CC_UP), press(MR_CC_RIGHT), press(MR_CC_MENU), press(MR_CC_MUTE), press(MR_CC_COPY);
    CHECK(M.s.octave == oct && M.seq.page == cv && !M.seq.view && !(M.seq.mods & (SEQ_M_MUTE | SEQ_M_COPY)) && !M.seq.mute_latch);
    cv = M.cc_val[M.s.bank][0];
    knob(0, 5), jog(1);
    CHECK(M.cc_val[M.s.bank][0] == cv && !M.seq.trig == !M.seq.trig);
    track(3, 0), rest();
    CHECK(!M.seq.projects && icons_lit() == 0 && scr_same());   /* (a hold: no tap) */
    /* a press before 300 ms is the hold: it acts (dead here) and the layer shows at once */
    track(3, 1), adv_ms(50);
    press(MR_CC_UP);
    CHECK(M.s.octave == oct && icons_lit() > 0 && !scr_same());
    track(3, 0), rest();
    CHECK(!M.seq.projects);
    /* a menu it opened owns the knobs, lit, while Track 4 is still down */
    track(3, 1), adv_ms(50);
    step(4, 1), step(4, 0);                             /* (Tempo) */
    CHECK(M.seq.smenu == RFM_MOVE_SM_TEMPO && rings_menu(0x03));
    write_pbm("t4_menu_opened");
    knob(0, 1);
    CHECK(rfm_move_seq_bpm10(&M) == 1210);
    jog(-1);
    CHECK(rfm_move_seq_bpm10(&M) == 1200);
    track(3, 0), rest();
    CHECK(M.seq.smenu == RFM_MOVE_SM_TEMPO && rings_menu(0x03));
    /* a menu open before Track 4: under its hold layer the knobs are dead and dark */
    track(3, 1), adv_ms(320);
    CHECK(rings_menu(0) && !scr_same());
    knob(0, 1);
    CHECK(rfm_move_seq_bpm10(&M) == 1200);
    track(3, 0), rest();
    CHECK(rings_menu(0x03));
    back();
    CHECK(!M.seq.smenu);
}

/* a2: Loop and Mute: a quick click latches the layer (a click again or Back ends it), a hold is the same layer until
 * the release (a long hold let go unused: no latch); shown at the press. Mute's tap no longer mutes the track */
static void test_loop_mute_taps(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int sel;
    track_view();
    rest(), scr_save();
    /* Loop: the press shows the layer at once; a quick click latches */
    button(MR_CC_LOOP, 1);
    leds(w);
    CHECK(!scr_same() && w[L_WHITE0 + W_LOOP][0] == 127);
    adv_ms(120), button(MR_CC_LOOP, 0);
    CHECK(M.seq.loop_latch && !scr_same());
    write_pbm("loop_latched");
    press(MR_CC_LOOP);                                  /* (a click again: off) */
    CHECK(!M.seq.loop_latch);
    rest();
    CHECK(scr_same());
    press(MR_CC_LOOP), back();                          /* (Back closes it) */
    CHECK(!M.seq.loop_latch && scr_same());
    button(MR_CC_LOOP, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 100), button(MR_CC_LOOP, 0);   /* (a long hold, unused) */
    CHECK(!M.seq.loop_latch && scr_same());
    button(MR_CC_LOOP, 1), step(3, 1), step(3, 0), button(MR_CC_LOOP, 0);   /* (quick, but used: no latch) */
    CHECK(!M.seq.loop_latch && M.seq.page == 3);
    press(MR_CC_LEFT), press(MR_CC_LEFT), press(MR_CC_LEFT), rest();
    button(MR_CC_LOOP, 1), press(MR_CC_UP), button(MR_CC_LOOP, 0);   /* (something else pressed: no latch) */
    CHECK(!M.seq.loop_latch);
    rest(), scr_save();
    /* Mute: the same; its click latches the 16 tracks on the steps; the selected track not muted */
    sel = M.seq.sel_track;
    button(MR_CC_MUTE, 1);
    leds(w);
    CHECK(!scr_same() && w[L_WHITE0 + W_MUTE][0] == 127);
    adv_ms(100), button(MR_CC_MUTE, 0);
    leds(w);
    CHECK(M.seq.mute_latch && !M.seq.tr[sel].mute && !scr_same() && w[L_WHITE0 + W_MUTE][0] == 127 && icons_lit() == 0);
    write_pbm("mute_latched");
    step(4, 1), step(4, 0);
    CHECK(M.seq.tr[4].mute && M.seq.mute_latch);
    step(4, 1), step(4, 0);
    CHECK(!M.seq.tr[4].mute);
    press(MR_CC_MUTE);
    CHECK(!M.seq.mute_latch);
    rest();
    CHECK(scr_same());
    press(MR_CC_MUTE), back();
    CHECK(!M.seq.mute_latch && scr_same());
    button(MR_CC_MUTE, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 100), button(MR_CC_MUTE, 0);
    CHECK(!M.seq.mute_latch && !M.seq.tr[sel].mute && scr_same());
    button(MR_CC_MUTE, 1), step(6, 1), step(6, 0), button(MR_CC_MUTE, 0);   /* (a hold used: track 7 muted) */
    CHECK(!M.seq.mute_latch && M.seq.tr[6].mute);
    rfmv_seq_mute(&M, 6, 0);
    /* session view: latched, a clip pad mutes its track */
    press(MR_CC_MENU), rest();
    CHECK(M.seq.view);
    press(MR_CC_MUTE);
    CHECK(M.seq.mute_latch);
    hit(PAD(2, 1));
    CHECK(M.seq.tr[M.seq.track_page + 2].mute);
    write_pbm("mute_latched_session");
    hit(PAD(2, 1));
    back();
    CHECK(!M.seq.mute_latch && !M.seq.tr[M.seq.track_page + 2].mute);
    press(MR_CC_MENU), rest();
}

/* q2: Shift + step 13 has no job (dark, dead); Delete held + Play clears the selected clip, in both views */
static void test_clear_clip(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int clip, lo;
    track_view();
    clip = SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel);
    rfmv_seq_note_put(&M, clip, 2, 60, 100, 24, 0);
    rfmv_seq_note_put(&M, clip, 9, 62, 100, 24, 0);
    button(MR_CC_SHIFT, 1);
    leds(w);
    CHECK(w[L_ICON0 + 12][0] == 0 && w[L_ICON0 + 13][0] == 24);
    step(12, 1), step(12, 0);
    button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    CHECK(rfmv_seq_clip_range(&M, clip, &lo) == lo + 2);
    clear();
    button(MR_CC_DELETE, 1), press(MR_CC_PLAY);
    CHECK(rfmv_seq_clip_range(&M, clip, &lo) == lo && !M.seq.playing && !M.seq.start_pending && rfmv_seq_clip_has(&M, clip));
    button(MR_CC_DELETE, 0);
    CHECK(!strstr(said, "Delete: hold"));
    screen();
    write_pbm("clip_cleared");
    press(MR_CC_UNDO);
    CHECK(rfmv_seq_clip_range(&M, clip, &lo) == lo + 2);
    press(MR_CC_PLAY);                                  /* (playing: Delete + Play clears, the transport plays on) */
    CHECK(M.seq.playing || M.seq.start_pending);
    press(MR_CC_MENU), rest();
    button(MR_CC_DELETE, 1), press(MR_CC_PLAY), button(MR_CC_DELETE, 0);
    CHECK(M.seq.view && rfmv_seq_clip_range(&M, clip, &lo) == lo && (M.seq.playing || M.seq.start_pending));
    press(MR_CC_PLAY), press(MR_CC_MENU), rest();
}

/* q1: a colour per firmware slot (orange, blue, yellow) on the Track buttons, the routing, session view's columns
 * (the lowest slot a track reaches; none: grey); solo purple */
static int hue_orange(const uint8_t c[3]) { return c[0] > 0 && c[2] == 0 && c[1] * 10 > c[0] * 3 && c[1] * 10 < c[0] * 6; }
static int hue_blue(const uint8_t c[3]) { return c[2] > c[1] && c[1] > c[0] && c[0] > 0; }
static int hue_yellow(const uint8_t c[3]) { return c[0] > 0 && c[2] == 0 && c[1] * 10 > c[0] * 7; }
static int hue_grey(const uint8_t c[3]) { return c[0] > 0 && c[0] == c[1] && c[2] > c[0] && c[2] - c[0] < c[0] / 4 + 2; }
static void test_slot_colours(void)
{
    static const char *const ids[RFM_MOVE_FW_SLOTS] = { "choralroot", "felucca", "aurora" };
    uint8_t w[RFM_MOVE_NLEDS][3];
    int t;
    session_view();
    rfm_move_fw_slots(&M, ids);
    M.seq.track_page = 0, M.seq.scene_off = 0;
    M.seq.fx.ffilt[0] = 1u << 0, M.seq.fx.ffilt[1] = 1u << 1 | 1u << 4, M.seq.fx.ffilt[2] = 1u << 2 | 1u << 4;
    for (t = 0; t < 5; t++)
        clip_put(t, 2, 60 + t);
    rest();
    leds(w);
    CHECKF(hue_orange(w[L_PAD0 + PAD(0, 2)]) && hue_blue(w[L_PAD0 + PAD(1, 2)]) && hue_yellow(w[L_PAD0 + PAD(2, 2)]) &&
           hue_grey(w[L_PAD0 + PAD(3, 2)]) && hue_blue(w[L_PAD0 + PAD(4, 2)]),
           "columns %02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x", w[L_PAD0 + PAD(0, 2)][0],
           w[L_PAD0 + PAD(0, 2)][1], w[L_PAD0 + PAD(0, 2)][2], w[L_PAD0 + PAD(1, 2)][0], w[L_PAD0 + PAD(1, 2)][1],
           w[L_PAD0 + PAD(1, 2)][2], w[L_PAD0 + PAD(2, 2)][0], w[L_PAD0 + PAD(2, 2)][1], w[L_PAD0 + PAD(2, 2)][2],
           w[L_PAD0 + PAD(3, 2)][0], w[L_PAD0 + PAD(3, 2)][1], w[L_PAD0 + PAD(3, 2)][2], w[L_PAD0 + PAD(4, 2)][0],
           w[L_PAD0 + PAD(4, 2)][1], w[L_PAD0 + PAD(4, 2)][2]);
    CHECK(hue_orange(w[L_STEP0 + 0]) && hue_yellow(w[L_STEP0 + 4]) && hue_grey(w[L_STEP0 + 6]));   /* (the select steps) */
    CHECK(hue_orange(w[L_TRACK0 + 0]) && hue_blue(w[L_TRACK0 + 1]) && hue_yellow(w[L_TRACK0 + 2]));   /* (the Track buttons) */
    CHECK(hue_blue(w[L_RING0 + 1]) && hue_blue(w[L_RING0 + 4]) && !hue_blue(w[L_RING0 + 0]));   /* (the knobs: slot 2's) */
    write_pbm("slotcols");
    /* the held Track's routing: its slot's colour on the pads and steps; the solo purple */
    hold_track(2);
    leds(w);
    CHECK(hue_yellow(w[L_PAD0 + PAD(2, 0)]) && hue_yellow(w[L_PAD0 + PAD(4, 0)]) && !lit(w[L_PAD0 + PAD(0, 0)]) &&
          hue_yellow(w[L_STEP0 + 4]));
    write_pbm("route_hold");
    rfmv_fw_solo(&M, 2, 4);
    leds(w);
    CHECK(w[L_STEP0 + 4][0] == 170 || w[L_STEP0 + 4][0] == 170 * 70 / 256);
    rfmv_fw_solo(&M, 2, 4);
    track(2, 0), rest();
}

/* a10: FILL (Shift + Loop held): Loop's light blinks fast, "Fill" on the screen (said once), a diagonal wave over the
 * pads in the track's colour (under ~100 LED writes a second); the pads still play; off: all back at once */
static void test_fill(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3], w0[RFM_MOVE_NLEDS][3];
    int t, np, peak, i, ok, on = 0, off = 0, waves = 0;
    track_view();
    hit(5);                                             /* (pad 6 played once: the last notes as they will be after) */
    rest(), scr_save(), leds(w0);
    led_flush();
    button(MR_CC_SHIFT, 1), button(MR_CC_LOOP, 1);
    CHECK(M.seq.fill && !scr_same());
    CHECKF(strstr(said, "Fill on") != NULL, "said %s", said);
    write_pbm("fill");
    np = peak = 0;
    for (t = 0; t < 1000; t += 10) {                    /* (a second: the writes counted, the light's blinks) */
        int s1 = 0, p1 = 0;
        led_msgs(&s1, &p1);
        np += p1, peak = p1 > peak ? p1 : peak;
        leds(w);
        on += w[L_WHITE0 + W_LOOP][0] == 127, off += w[L_WHITE0 + W_LOOP][0] == 0;
        for (i = 0, ok = 0; i < 32; i++)
            ok += memcmp(w[L_PAD0 + i], w0[L_PAD0 + i], 3) != 0;
        waves += ok > 0;
        led_flush();
        adv_ms(10);
    }
    printf("fill wave: %d pad LED writes a second, at most %d at once\n", np, peak);
    CHECKF(np <= 100 && peak <= 8 && np > 20, "fill: %d pad writes, %d at once", np, peak);
    CHECKF(on > 20 && off > 20 && waves > 50, "fill: light on %d off %d, wave frames %d", on, off, waves);
    clear();
    pad(5, 100);                                        /* (a pad plays, white at once) */
    leds(w);
    CHECK(count(RFM_MOVE_ACT_MIDI) == 1 && white(w[L_PAD0 + 5]));
    pad(5, 0);
    button(MR_CC_LOOP, 0);                              /* (off: the pads, the light, the screen back at once) */
    CHECK(!M.seq.fill);
    leds(w);
    for (i = 0, ok = 1; i < 32; i++)
        ok &= !memcmp(w[L_PAD0 + i], w0[L_PAD0 + i], 3);
    CHECKF(ok && w[L_WHITE0 + W_LOOP][0] == w0[L_WHITE0 + W_LOOP][0], "pads %d, loop %d / %d", ok, w[L_WHITE0 + W_LOOP][0],
           w0[L_WHITE0 + W_LOOP][0]);
    button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    rest();
    CHECK(scr_same());
}

/* a12: knob lights that match: TRIG page knob 8 dark and no popup; a held step's unlocked CC knobs dim; the project
 * grid's knob 1 only while a pad is held (its touch: "Colour" and the name); the FM-1 layout's no-job knob dark */
static void test_knob_lights(void)
{
    static const char *lbl[RFM_MOVE_LBL_N];
    uint8_t w[RFM_MOVE_NLEDS][3];
    int clip, i;
    track_view();
    CHECK(M.seq.trig);
    leds(w);
    CHECK(!lit(w[L_RING0 + 7]) && lit(w[L_RING0 + 0]));
    touch(7, 1);
    CHECK(!rfmv_pop_up(&M, POP_TRIG));
    knob(7, 3);
    CHECK(!rfmv_pop_up(&M, POP_TRIG));
    touch(7, 0), rest();
    /* bank 1 (the CC page), a step held, one lock: the locked knob bright, the others dim (not dark) */
    jog(1), rest();
    CHECK(!M.seq.trig);
    clip = SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel);
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 24, 0);
    rfmv_seq_lock_set(&M, clip, 0, M.s.cc[M.s.bank][2], 127);
    step(0, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    leds(w);
    CHECK(lit(w[L_RING0 + 0]) && bright(w[L_RING0 + 2]) > 3 * bright(w[L_RING0 + 0]));
    write_pbm("cc_locks_dim");
    step(0, 0), jog(-1), rest();
    /* the project grid: knob 1 dark, lit while a project's pad is held; touched: its colour by name */
    press(MR_CC_UNDO), rest();
    button(MR_CC_SHIFT, 1), step(0, 1), step(0, 0), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    CHECK(M.seq.projects && M.fw.track == 3);
    rest();
    CHECK(rings_menu(0));
    M.seq.proj_used |= 1u << 5;
    pad(PAD(5, 0), 100);
    CHECK(rings_menu(0x01));
    rest(), clear();
    touch(0, 1);
    CHECK(M.pop == POP_PROJ && M.pop_val == 1 && rfmv_pop_up(&M, POP_PROJ));
    CHECKF(strstr(said, "Colour ") != NULL, "said %s", said);
    write_pbm("proj_colour");
    touch(0, 0), pad(PAD(5, 0), 0), rest();
    CHECK(rings_menu(0));
    back(), rest();
    CHECK(!M.seq.projects);
    /* the FM-1 layout: a knob whose label is "" has no job: dark */
    tap_track(0), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1);
    for (i = 0; i < RFM_MOVE_LBL_N; i++)
        lbl[i] = NULL;
    lbl[rfmv_enc_slot(RFM_MOVE_E_K1)] = "";
    lbl[rfmv_enc_slot(RFM_MOVE_E_K2)] = "Cutoff";
    rfm_move_set_labels(&M, lbl);
    leds(w);
    CHECK(!lit(w[L_RING0 + 4]) && lit(w[L_RING0 + 5]) && lit(w[L_RING0 + 0]) && !lit(w[L_RING0 + 3]));
    rfm_move_set_labels(&M, NULL);
    tap_track(1), rest();
}

/* a13: dead presses: Undo / Redo with nothing: "None"; Back lit only with a layer to close (full at its press);
 * Shift + Sample dark */
static void test_dead_presses(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    track_view();
    while (M.seq.un)
        press(MR_CC_UNDO);
    rest(), clear();
    press(MR_CC_UNDO);
    CHECK(M.seq.msg == SEQ_MSG_NOUNDO && rfmv_seq_msg_text(&M) && !strcmp(rfmv_seq_msg_text(&M), "None"));
    CHECKF(strstr(said, "None") != NULL, "said %s", said);
    write_pbm("undo_none");
    rest(), clear();
    button(MR_CC_SHIFT, 1), press(MR_CC_UNDO);
    CHECK(M.seq.msg == SEQ_MSG_NOUNDO && rfmv_seq_msg_text(&M));
    leds(w);
    CHECK(!lit(w[L_SAMPLE]));                           /* (Shift + Sample: no job) */
    button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS, rest();
    leds(w);
    CHECK(w[L_WHITE0 + W_BACK][0] == 0);                /* (nothing to close: dark) */
    button(MR_CC_BACK, 1);
    leds(w);
    CHECK(w[L_WHITE0 + W_BACK][0] == 127);              /* (pressed: full) */
    button(MR_CC_BACK, 0), rest();
    shift_step(2);
    leds(w);
    CHECK(w[L_WHITE0 + W_BACK][0] > 0);
    back();
    leds(w);
    CHECK(w[L_WHITE0 + W_BACK][0] == 0);
    press(MR_CC_LOOP);
    leds(w);
    CHECK(w[L_WHITE0 + W_BACK][0] > 0);
    back(), press(MR_CC_MUTE);
    leds(w);
    CHECK(w[L_WHITE0 + W_BACK][0] > 0);
    back(), press(MR_CC_MENU), rest();                  /* (session view at rest: dark too) */
    leds(w);
    CHECK(M.seq.view && w[L_WHITE0 + W_BACK][0] == 0);
    press(MR_CC_MENU), rest();
}

/* a14: the browser's filter steps: an icon only under a filter that is on; a step white at its press (the toggle at
 * the release, as before) */
static void test_browser_icons(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int s, tagstep = -1, on, i;
    track_view();
    dbl_track(0), rest();
    CHECK(M.seq.projects && M.fw.track == 0 && !M.fw.fx);
    leds(w);
    for (s = 2; s < 16 && tagstep < 0; s++)
        if (lit(w[L_STEP0 + s]))
            tagstep = s;
    CHECK(tagstep >= 2);
    for (i = 0, on = 0; i < 16; i++)
        on += w[L_ICON0 + i][0] > 0;
    CHECK(on == 1 && (w[L_ICON0 + 0][0] == 127 || w[L_ICON0 + 1][0] == 127));   /* (the scope only: no filter on) */
    write_pbm("browser_icons");
    step(tagstep, 1);
    leds(w);
    CHECK(white(w[L_STEP0 + tagstep]) && !(M.s.fw_filter >> tagstep & 1) && w[L_ICON0 + tagstep][0] == 0);
    step(tagstep, 0);
    leds(w);
    CHECK((M.s.fw_filter >> tagstep & 1) && w[L_ICON0 + tagstep][0] == 127 && !white(w[L_STEP0 + tagstep]));
    write_pbm("browser_filter_on");
    step(tagstep, 1), step(tagstep, 0);                 /* (again: off) */
    leds(w);
    CHECK(!(M.s.fw_filter >> tagstep & 1) && w[L_ICON0 + tagstep][0] == 0);
    step(tagstep, 1), adv_ms(RFM_MOVE_TAP_MS + 30);      /* (held: white, its popup; no toggle) */
    leds(w);
    CHECK(white(w[L_STEP0 + tagstep]));
    step(tagstep, 0);
    CHECK(!(M.s.fw_filter >> tagstep & 1));
    back(), rest();
}

/* ================================================================ (0.13.0) P-core: a project holds its firmwares;
 * firmware States as a layer (RFM_MOVE_CAP_PROJFW). A fake host: the actions logged with their ids, the projects
 * stored (PROJECT_SAVE / _LOAD answered), the firmwares answered by hand (rfm_move_proj_fw) */
typedef struct { int kind, slot, index, value; char id[RFM_MOVE_FW_ID_MAX]; } ps_act_t;
static ps_act_t PA[512];
static int npa, ps_answer;
static uint8_t PS_BLOB[RFM_MOVE_PROJECTS][RFM_MOVE_PROJECT_MAX];
static uint32_t PS_N[RFM_MOVE_PROJECTS];
static void ps_act(void *ctx, const rfm_move_act_t *a)
{
    rfm_move_t *m = ctx ? (rfm_move_t *)ctx : &M;
    on_act(NULL, a);
    if (a->kind == RFM_MOVE_ACT_SAY)
        return;
    if (npa < (int)(sizeof PA / sizeof PA[0])) {
        ps_act_t *p = &PA[npa++];
        p->kind = a->kind, p->slot = a->slot, p->index = a->index, p->value = a->value, p->id[0] = 0;
        if (a->data && a->len < RFM_MOVE_FW_ID_MAX && a->kind != RFM_MOVE_ACT_PROJECT_SAVE)
            memcpy(p->id, a->data, a->len), p->id[a->len] = 0;
    }
    if (a->kind == RFM_MOVE_ACT_PROJECT_SAVE && a->len <= RFM_MOVE_PROJECT_MAX)
        memcpy(PS_BLOB[a->index], a->data, a->len), PS_N[a->index] = a->len;
    else if (a->kind == RFM_MOVE_ACT_PROJECT_LOAD && ps_answer)
        rfm_move_project_load(m, a->index, PS_BLOB[a->index], PS_N[a->index]);
}
static void ps_clear(void) { npa = 0, clear(); }
static int ps_count(int kind)
{
    int i, n = 0;
    for (i = 0; i < npa; i++)
        n += PA[i].kind == kind;
    return n;
}
static const ps_act_t *ps_find(int kind, int slot)      /* the last of that kind (slot -1: any) */
{
    int i;
    for (i = npa - 1; i >= 0; i--)
        if (PA[i].kind == kind && (slot < 0 || PA[i].slot == slot))
            return &PA[i];
    return NULL;
}
static void ps_slots(const char *a, const char *b, const char *c)
{
    const char *const ids[RFM_MOVE_FW_SLOTS] = { a, b, c };
    rfm_move_fw_slots(&M, ids);
}
/* the Move's host: three slots, FX, and the project's firmwares */
static void ps_fresh(int cap)
{
    fresh();
    M.fn = ps_act, M.ctx = NULL;
    rfm_move_fw_caps(&M, RFM_MOVE_CAPS_ALL | (cap ? RFM_MOVE_CAP_PROJFW : 0));
    memset(PS_N, 0, sizeof PS_N);
    ps_answer = 0;
    rest(), ps_clear();
}
static int ps_pad(int slot) { return (3 - slot / 8) * 8 + slot % 8; }   /* a project's (or a state's) pad */
static void ps_store(int slot)                          /* M's project as it is now, stored at slot (the grid knows it) */
{
    uint32_t used = M.seq.proj_used | 1u << slot;
    PS_N[slot] = (uint32_t)rfm_move_project_save(&M, PS_BLOB[slot], sizeof PS_BLOB[slot]);
    rfm_move_project_dir(&M, used);
    rfm_move_project_info(&M, slot, PS_BLOB[slot], PS_N[slot]);
}
static const uint8_t *ps_part(int slot) { return PS_BLOB[slot] + PS_N[slot] - 4 - RFM_MOVE_PFW_BYTES; }
static void refnv32(uint8_t *b, uint32_t n)
{
    uint32_t h = 2166136261u, k;
    for (k = 0; k < n - 4; k++)
        h = (h ^ b[k]) * 16777619u;
    for (k = 0; k < 4; k++)
        b[n - 4 + k] = (uint8_t)(h >> (8 * k));
}
static void grid(void)                                  /* the project grid (Track 4's double click) */
{
    dbl_track(3), rest();
    CHECK(M.seq.projects && M.fw.track == 3);
    ps_clear();
}

/* p1: the blob's version 9: the firmwares and their settings round trip; a version 8 project leaves them alone; a
 * host without the capability never touches them and saves the part back as it came */
static rfm_move_t M2;
static void test_ps_blob(void)
{
    static uint8_t b8[RFM_MOVE_PROJECT_MAX], b9[RFM_MOVE_PROJECT_MAX];
    const uint8_t *part;
    int n, n8;
    ps_fresh(1);
    ps_slots("choralroot", "sloop", "felucca");
    rfm_move_fw_set_level(&M, 1, 77);
    M.s.slot[2].scale = RFM_MOVE_SCALE_DORIAN, M.s.slot[2].root = 3, M.s.slot[2].channel = 5, M.s.slot[2].octave = -2;
    M.s.slot[2].lay = RFM_MOVE_LAY_DRUM, M.s.slot[2].cc[7] = 99, M.s.lock[2] = 11;
    M.s.drum[0] = 41, M.s.vel_mode = RFM_MOVE_VEL_FIXED, M.s.vel_level = 90;
    n = rfm_move_project_save(&M, b9, sizeof b9);
    CHECK(n == RFM_MOVE_PROJECT_FIXED && b9[4] == 9 && rfm_move_project_check(b9, (uint32_t)n) == 0);
    part = b9 + n - 4 - RFM_MOVE_PFW_BYTES;
    CHECK(!strcmp((const char *)part, "choralroot") && !strcmp((const char *)part + RFM_MOVE_PFW_SLOT_BYTES, "sloop"));
    CHECK(part[RFM_MOVE_PFW_SLOT_BYTES + RFM_MOVE_FW_ID_MAX] == 77);
    /* a value out of range in it: refused */
    memcpy(b8, b9, (size_t)n), b8[n - 4 - RFM_MOVE_PFW_BYTES + 2 * RFM_MOVE_PFW_SLOT_BYTES + RFM_MOVE_FW_ID_MAX + 1] = 9;
    refnv32(b8, (uint32_t)n);
    CHECK(rfm_move_project_check(b8, (uint32_t)n) == -9);
    /* loaded on another Move: everything set, the firmwares asked for (one PROJECT_FW a slot, the project's slot) */
    rfm_move_init(&M2, NULL, ps_act, &M2);
    rfm_move_fw_list(&M2, E, NFW);
    rfm_move_fw_caps(&M2, RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_PROJFW);
    ps_clear();
    CHECK(rfm_move_project_load(&M2, 4, b9, (uint32_t)n) == 0);
    CHECK(M2.fw.level[1] == 77 && M2.s.slot[2].scale == RFM_MOVE_SCALE_DORIAN && M2.s.slot[2].root == 3 &&
          M2.s.slot[2].channel == 5 && M2.s.slot[2].octave == -2 && M2.s.slot[2].lay == RFM_MOVE_LAY_DRUM &&
          M2.s.slot[2].cc[7] == 99 && M2.s.lock[2] == 11);
    CHECK(M2.s.drum[0] == 41 && M2.s.vel_mode == RFM_MOVE_VEL_FIXED && M2.s.vel_level == 90);
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_FW) == 3 && ps_find(RFM_MOVE_ACT_FW_LEVEL, 1) && ps_count(RFM_MOVE_ACT_SETTINGS) >= 1);
    CHECK(!strcmp(ps_find(RFM_MOVE_ACT_PROJECT_FW, 0)->id, "choralroot") && ps_find(RFM_MOVE_ACT_PROJECT_FW, 0)->index == 4);
    CHECK(!strcmp(ps_find(RFM_MOVE_ACT_PROJECT_FW, 2)->id, "felucca") && rfm_move_proj_fw_loading(&M2));
    CHECK(!M2.seq.dirty);                               /* (the load's own settings: no edit) */
    /* a version 8 blob (no firmwares): the slots, the levels and the settings as they are; saved again as 9 with what
     * is loaded */
    n8 = n - RFM_MOVE_PFW_BYTES;
    memcpy(b8, b9, (size_t)n8 - 4), b8[4] = 8;
    refnv32(b8, (uint32_t)n8);
    CHECK(rfm_move_project_check(b8, (uint32_t)n8) == 0);
    ps_fresh(1);
    ps_slots("dub", "", "");
    rfm_move_fw_set_level(&M, 0, 50);
    ps_clear();
    CHECK(rfm_move_project_load(&M, 2, b8, (uint32_t)n8) == 0);
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_FW) == 0 && !rfm_move_proj_fw_loading(&M) && !strcmp(M.fw.slot_id[0], "dub"));
    CHECK(M.fw.level[0] == 50 && M.s.drum[0] != 41 && M.s.vel_mode != RFM_MOVE_VEL_FIXED);
    n = rfm_move_project_save(&M, b9, sizeof b9);
    CHECK(b9[4] == 9 && !strcmp((const char *)b9 + n - 4 - RFM_MOVE_PFW_BYTES, "dub"));
    {                                                   /* the grid's info of a version 8 project: no firmwares */
        rfm_move_proj_info_t in;
        CHECK(rfm_move_project_info(&M, 9, b8, (uint32_t)n8) == 0);
        rfmv_proj_info(&M, 9, &in);
        CHECK(in.ok && !in.has_fw && !in.fw[0][0]);
    }
    /* a host without the capability: the load touches no firmware, and the part goes back as it came */
    ps_fresh(1);
    ps_slots("choralroot", "sloop", "felucca");
    n = rfm_move_project_save(&M, b9, sizeof b9);
    ps_fresh(0);
    rfm_move_fw_set_level(&M, 0, 33);
    ps_clear();
    CHECK(rfm_move_project_load(&M, 1, b9, (uint32_t)n) == 0);
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_FW) == 0 && !rfm_move_proj_fw_loading(&M) && M.fw.level[0] == 33 &&
          !strcmp(M.fw.slot_id[1], "felucca"));
    n8 = rfm_move_project_save(&M, b8, sizeof b8);
    CHECK(n8 == n && !memcmp(b8 + n - 4 - RFM_MOVE_PFW_BYTES, b9 + n - 4 - RFM_MOVE_PFW_BYTES, RFM_MOVE_PFW_BYTES));
    /* .. and its level changes: no project edit */
    rfm_move_fw_set_level(&M, 0, 40), adv_ms(20);
    CHECK(!M.seq.dirty);
}

/* p2: a project opened in the grid: its loading screen at the press (before the host answers), the bars as the host
 * reports, a Track lit when its firmware is ready, Play waiting for the load, a missing firmware said and kept */
static void ps_two_projects(void)                       /* project 1: ChoralRoot, Felucca, -; project 7: ChoralRoot,
                                                         * SLOOP, Felucca at 120 BPM, 12 clips */
{
    int c;
    ps_fresh(1);
    ps_slots("choralroot", "sloop", "felucca");
    rfm_move_seq_set_bpm(&M, 1200);
    for (c = 0; c < 12; c++)
        clip_put(c % 4, c / 4, 60 + c);
    ps_store(6);
    rfm_move_project_load(&M, 0, NULL, 0);
    ps_slots("choralroot", "felucca", "");
    ps_store(0);
    ps_answer = 0;
    rest(), ps_clear();
}
static void test_ps_loading(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    const ps_act_t *a;
    ps_two_projects();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    grid();
    /* the press: the loading screen at once, its rows from the grid's knowledge (no answer yet), the Tracks dark */
    pad(ps_pad(6), 100);
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_LOAD) == 1 && rfm_move_proj_fw_loading(&M) && M.seq.proj_pending == 6);
    screen();
    CHECK(lit_rows(0, 21) > 60 && lit_rows(28, 36) > 20 && lit_rows(52, 60) > 20);
    leds(w);
    CHECK(!lit(w[L_TRACK0 + 0]) && !lit(w[L_TRACK0 + 1]) && !lit(w[L_TRACK0 + 2]));
    CHECKF(strstr(said, "Loading project 7") != NULL, "said %s", said);
    /* Play meanwhile: not yet */
    press(MR_CC_PLAY);
    CHECK(!M.seq.playing && !M.seq.start_pending && M.pfw_play);
    /* the host answers: the project in, its firmwares asked for (the same one in slot 1: only its memory swapped) */
    ps_clear();
    CHECK(rfm_move_project_load(&M, 6, PS_BLOB[6], PS_N[6]) == 0);
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_FW) == 3 && (a = ps_find(RFM_MOVE_ACT_PROJECT_FW, 1)) && !strcmp(a->id, "sloop") &&
          a->index == 6 && rfm_move_proj_fw_loading(&M));
    rfm_move_proj_fw(&M, 0, RFM_MOVE_PFW_READY);
    rfm_move_proj_fw(&M, 1, 60);
    leds(w);
    CHECK(lit(w[L_TRACK0 + 0]) && !lit(w[L_TRACK0 + 1]) && !lit(w[L_TRACK0 + 2]) && w[L_TRACK0 + 0][0] == 255);
    screen();
    CHECK(px(84 + 42, 28 + 3) && px(84 + 20, 40 + 3) && !px(84 + 30, 40 + 3) && px(84 + 30, 40 + 1));   /* (full, 60 %) */
    write_pbm("ps_proj_loading");
    pad(ps_pad(6), 0);
    CHECK(rfm_move_proj_fw_loading(&M) && !M.seq.playing);
    /* the rest settle: slot 2 ready, Felucca not on this Move: the transport starts, "Felucca / not installed" a
     * second, the slot empty, the project still wants it */
    rfm_move_proj_fw(&M, 1, RFM_MOVE_PFW_READY);
    clear();
    rfm_move_proj_fw(&M, 2, RFM_MOVE_PFW_MISSING);
    CHECK(!rfm_move_proj_fw_loading(&M) && (M.seq.playing || M.seq.start_pending) && !M.fw.slot_id[2][0]);
    CHECK(!strcmp(M.fw.slot_id[1], "sloop"));
    CHECKF(strstr(said, "Felucca not installed, Track 3 empty") != NULL, "said %s", said);
    screen();
    CHECK(lit_rows(8, 22) > 40 && lit_rows(32, 39) > 20 && lit_rows(46, 53) > 20);
    write_pbm("ps_proj_missing");
    press(MR_CC_PLAY), rest();
    CHECK(!M.seq.playing);
    {                                                   /* saved: the missing one kept, the loaded ones as they are */
        rfm_move_proj_info_t in;
        rfm_move_project_flush(&M);
        rfmv_proj_info(&M, 6, &in);
        CHECK(in.has_fw && !strcmp(in.fw[0], "choralroot") && !strcmp(in.fw[1], "sloop") && !strcmp(in.fw[2], "felucca"));
        rfmv_proj_save_now(&M);
        CHECK(!strcmp((const char *)ps_part(6) + 2 * RFM_MOVE_PFW_SLOT_BYTES, "felucca"));
        /* the user loads another firmware there, then empties it: the project no longer wants Felucca (an edit) */
        ps_slots("choralroot", "sloop", "dub"), adv_ms(20);
        CHECK(M.seq.dirty);
        ps_slots("choralroot", "sloop", ""), adv_ms(20);
        rfmv_proj_info(&M, 6, &in);
        CHECK(!in.fw[2][0]);
        adv_ms(RFM_MOVE_AUTOSAVE_MS + 50);
        CHECK(!M.seq.dirty && !((const char *)ps_part(6))[2 * RFM_MOVE_PFW_SLOT_BYTES]);
    }
    back(), rest();
}
/* p3: no news for 20 s: failed; another project pressed while one loads: that one loads (the host told again) */
static void test_ps_limit(void)
{
    ps_two_projects();
    grid();
    hit(ps_pad(6));
    ps_clear();
    rfm_move_project_load(&M, 6, PS_BLOB[6], PS_N[6]);
    rfm_move_proj_fw(&M, 0, RFM_MOVE_PFW_READY), rfm_move_proj_fw(&M, 1, 30);
    adv_ms(RFM_MOVE_PFW_WAIT_MS - 100);
    CHECK(rfm_move_proj_fw_loading(&M));
    rfm_move_proj_fw(&M, 1, 40);                        /* (news: its time starts again) */
    adv_ms(200);
    CHECK(rfm_move_proj_fw_loading(&M) && M.pfw_st[2] == RFM_MOVE_PFW_FAILED && M.pfw_st[1] == 40);
    adv_ms(RFM_MOVE_PFW_WAIT_MS);
    CHECK(!rfm_move_proj_fw_loading(&M) && M.pfw_st[1] == RFM_MOVE_PFW_FAILED && M.pfw_msg == 6 && !M.fw.slot_id[1][0]);
    screen();
    CHECK(lit_rows(32, 39) > 10);                       /* ("failed") */
    write_pbm("ps_proj_failed");
    rest();
    /* restarted: project 7 again from project 1 while it loads */
    ps_answer = 1;
    hit(ps_pad(0));
    CHECK(M.seq.proj_cur == 0 && rfm_move_proj_fw_loading(&M) && ps_find(RFM_MOVE_ACT_PROJECT_FW, 0)->index == 0);
    ps_clear();
    hit(ps_pad(6));
    CHECK(M.seq.proj_cur == 6 && rfm_move_proj_fw_loading(&M) && ps_count(RFM_MOVE_ACT_PROJECT_FW) == 3 &&
          ps_find(RFM_MOVE_ACT_PROJECT_FW, 0)->index == 6);
    back(), rest();
}
/* p4: a new project: its three slots emptied (the host told), the rest as it is; saved as version 9 */
static void test_ps_new(void)
{
    const ps_act_t *a;
    ps_two_projects();
    rfm_move_fw_set_level(&M, 1, 66);
    grid();
    hit(ps_pad(12));
    CHECK(M.seq.proj_cur == 12 && ps_count(RFM_MOVE_ACT_PROJECT_FW) == 3 && (a = ps_find(RFM_MOVE_ACT_PROJECT_FW, 2)) &&
          !a->id[0] && a->index == 12 && !rfm_move_proj_fw_loading(&M));
    CHECK(!M.fw.slot_id[0][0] && !M.fw.slot_id[1][0] && M.fw.level[1] == 66);
    CHECK(PS_N[12] && PS_BLOB[12][4] == 9 && !ps_part(12)[0] && ps_part(12)[RFM_MOVE_PFW_SLOT_BYTES + RFM_MOVE_FW_ID_MAX] == 66);
    back(), rest();
}
/* p5: a held project shows its firmwares (the mock-ups' rows); a version 8 one three dotted lines; duplicated with
 * Copy held + its pad, then an empty pad */
static void test_ps_held(void)
{
    rfm_move_proj_info_t in;
    ps_two_projects();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    ps_answer = 1;
    grid();
    hit(ps_pad(6));
    clear();
    rfm_move_proj_fw(&M, 0, RFM_MOVE_PFW_READY), rfm_move_proj_fw(&M, 1, RFM_MOVE_PFW_READY),
        rfm_move_proj_fw(&M, 2, RFM_MOVE_PFW_READY);
    CHECKF(strstr(said, "Project 7, 120 BPM, 12 clips, ChoralRoot, SLOOP, Felucca") != NULL, "said %s", said);   /* (the
                                                         * pressed project's popup once loaded) */
    rest(), clear();
    pad(ps_pad(6), 100);                                /* (the open one: held, nothing loads) */
    screen();
    CHECK(M.pop == POP_PROJ && lit_rows(28, 36) > 30 && lit_rows(52, 60) > 20 && lit_rows(0, 22) > 80);
    write_pbm("ps_proj_held");
    pad(ps_pad(6), 0), rest();
    /* an empty slot: a dotted line */
    ps_slots("choralroot", "sloop", ""), adv_ms(20);
    rfm_move_seq_set_bpm(&M, 960);
    pad(ps_pad(6), 100);
    screen();
    CHECK(lit_rows(52, 60) > 5 && lit_rows(52, 60) < 40);
    write_pbm("ps_proj_held_empty");
    pad(ps_pad(6), 0), rest();
    /* duplicated: Copy held + project 7, then an empty pad: the host's buffer pasted there (its firmwares too) */
    ps_clear();
    button(MR_CC_COPY, 1);
    hit(ps_pad(6)), hit(ps_pad(20));
    button(MR_CC_COPY, 0);
    CHECK(ps_find(RFM_MOVE_ACT_PROJECT_COPY, -1) && ps_find(RFM_MOVE_ACT_PROJECT_COPY, -1)->index == 6);
    CHECK(ps_find(RFM_MOVE_ACT_PROJECT_PASTE, -1) && ps_find(RFM_MOVE_ACT_PROJECT_PASTE, -1)->index == 20);
    rfmv_proj_info(&M, 20, &in);
    CHECK((M.seq.proj_used >> 20 & 1) && in.ok && in.has_fw && !strcmp(in.fw[1], "sloop") && M.seq.proj_cur == 6);
    /* a version 8 project held: three dotted lines */
    {
        static uint8_t b8[RFM_MOVE_PROJECT_MAX];
        const int n8 = (int)PS_N[6] - RFM_MOVE_PFW_BYTES;
        memcpy(b8, PS_BLOB[6], (size_t)n8 - 4), b8[4] = 8;
        refnv32(b8, (uint32_t)n8);
        memcpy(PS_BLOB[3], b8, (size_t)n8), PS_N[3] = (uint32_t)n8;
        rfm_move_project_dir(&M, M.seq.proj_used | 1u << 3);
        CHECK(rfm_move_project_info(&M, 3, b8, (uint32_t)n8) == 0);
        rfmv_pop(&M, POP_PROJ, 3);
        screen();
        CHECK(lit_rows(28, 60) > 30 && lit_rows(28, 60) < 120);
    }
    back(), rest();
}

/* p6: a held Track's pad 7 (the States layer): lit in the slot's colour with a firmware and the capability, dark and
 * dead without; States opened there return to that layout with Back; opened by the browser's Loop, to the browser */
static void test_ps_pad7(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int mode;
    ps_fresh(0);                                        /* (an older host: dark, dead) */
    hold_track(0);
    leds(w);
    CHECK(!lit(w[L_PAD0 + 30]));
    hit(30);
    CHECK(!M.fw.sl);
    track(0, 0), rest();
    dbl_track(0), rest();                               /* (.. and the browser's Loop: the old States, the old gestures) */
    press(MR_CC_LOOP);
    CHECK(M.fw.states && !M.fw.sl);
    ps_clear();
    button(MR_CC_COPY, 1), hit(ps_pad(3)), button(MR_CC_COPY, 0);
    CHECK(ps_find(RFM_MOVE_ACT_STATE_SAVE, 0) && !ps_find(RFM_MOVE_ACT_STATE_COPY, -1));   /* (Copy + pad: saved over) */
    back(), back(), rest();
    ps_fresh(1);                                        /* (Felucca on Track 2, its Scale layout: the mock's menu) */
    hold_track(1), hit(26), track(1, 0), rest();
    mode = M.s.mode;
    hold_track(1);
    leds(w);
    CHECK(w[L_PAD0 + 30][0] == 40 && w[L_PAD0 + 30][1] == 120 && w[L_PAD0 + 30][2] == 255);   /* (Track 2: blue) */
    write_pbm("ps_trk_menu");
    track(1, 0), rest();
    CHECK(M.s.mode == mode && !M.fw.sl);
    tap_track(0), rest();
    hold_track(2);                                      /* (an empty slot: dark, dead) */
    leds(w);
    CHECK(!lit(w[L_PAD0 + 30]));
    hit(30);
    CHECK(!M.fw.sl);
    track(2, 0), rest();
    /* opened from Track 1's menu in the FM-1 layout; the Track let go: States stay; Back: the layout again */
    mode = M.s.mode;
    hold_track(0), hit(30);
    CHECK(rfm_move_states_open(&M) == 0 && M.fw.sl_from == 0);
    track(0, 0), rest();
    CHECK(rfm_move_states_open(&M) == 0 && !M.seq.projects);
    back(), rest();
    CHECK(rfm_move_states_open(&M) < 0 && M.s.mode == mode && !M.seq.projects);
    /* from the browser: Loop opens it over the browser, Back (or Loop) goes back to the browser */
    dbl_track(0), rest();
    CHECK(M.seq.projects && M.fw.track == 0);
    press(MR_CC_LOOP);
    CHECK(rfm_move_states_open(&M) == 0 && M.fw.sl_from == 1 && !M.fw.states);
    back(), rest();
    CHECK(rfm_move_states_open(&M) < 0 && M.seq.projects && M.fw.track == 0);
    press(MR_CC_LOOP), press(MR_CC_LOOP);
    CHECK(rfm_move_states_open(&M) < 0 && M.seq.projects && M.fw.track == 0);
    back(), rest();
    /* opened over a note layout's track view: Back there again, nothing else changed */
    tap_track(1), rest();
    hold_track(1), hit(26), track(1, 0), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && !M.seq.view);
    hold_track(1), hit(30), track(1, 0), rest();
    CHECK(rfm_move_states_open(&M) == 1);
    back(), rest();
    CHECK(rfm_move_states_open(&M) < 0 && M.s.mode == RFM_MOVE_MODE_SEQ && !M.seq.view && !M.seq.projects);
}

/* p7: the States screen and lights; the opening's name and sweep (LED writes counted); Track 1..3 taps */
static void ps_states(void)                             /* ChoralRoot's States: 1, 2, 5 and 16 stored, 2 the active one */
{
    ps_fresh(1);
    ps_slots("choralroot", "sloop", "felucca");
    rfm_move_fw_states(&M, 0, 1u | 2u | 16u | 1u << 15);
    rfm_move_fw_state_cur(&M, 0, 1);
    rest(), ps_clear();
}
static void test_ps_layer(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int t, np, ns, peak;
    ps_states();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    rest(), clear();
    led_flush();
    hold_track(0);
    led_flush();
    hit(30);
    CHECK(rfm_move_states_open(&M) == 0);
    CHECKF(strstr(said, "States, ChoralRoot") != NULL, "said %s", said);
    screen();
    CHECK(lit_rows(10, 24) > 40);                       /* ("States" at 2x, its name under it: the opening) */
    write_pbm("ps_states_open");
    track(0, 0);
    /* the sweep: a column a frame in the slot's colour, settling into the states' lights; few writes a frame */
    np = ns = peak = 0;
    for (t = 0; t < 1000; t += 10) {
        int s1 = 0, p1 = 0;
        led_msgs(&s1, &p1);
        np += p1, ns += s1, peak = p1 > peak ? p1 : peak;
        if (t == 120) {
            leds(w);
            CHECK(w[L_PAD0 + 2][0] == 255 && w[L_PAD0 + 2][1] == 120 && w[L_PAD0 + 26][0] == 255);   /* (column 3) */
        }
        adv_ms(10);
    }
    printf("states sweep: %d pad LED writes in a second, at most %d at once\n", np, peak);
    CHECKF(np <= 100 && peak <= 9, "sweep: %d pad writes, %d at once", np, peak);
    leds(w);
    CHECK(w[L_PAD0 + 25][0] == 255 && w[L_PAD0 + 25][1] == 120);   /* (state 2, the active one: full orange) */
    CHECK(w[L_PAD0 + 24][0] == 107 && w[L_PAD0 + 28][0] == 107 && w[L_PAD0 + 23][0] == 107 && !lit(w[L_PAD0 + 26]));
    CHECK(!lit(w[L_STEP0 + 0]) && w[L_STEP0 + 13][0] == 63 && w[L_ICON0 + 13][0] == 127 && w[L_ICON0 + 12][0] == 0);
    CHECK(!lit(w[L_RING0 + 0]) && w[L_WHITE0 + W_BACK][0] == 80 && w[L_WHITE0 + W_UNDO][0] == 0);
    CHECK(w[L_TRACK0 + 0][0] == 255 && w[L_TRACK0 + 1][2] == 63);
    screen();
    CHECK(px(1 + 16 + 1, 16 + 1) && px(1, 16) && !px(1 + 1, 16 + 1) && px(1 + 32 + 6, 16 + 5) && !px(1 + 32 + 1, 16 + 1));
    write_pbm("ps_states");
    /* dead: the other steps, the knobs, the jog, Shift */
    ps_clear();
    hit(30 - 24 + 0), step(3, 1), step(3, 0), knob(0, 3), jog(1), press(MR_CC_SHIFT), press(MR_CC_MUTE);
    CHECK(npa == 1 && PA[0].kind == RFM_MOVE_ACT_STATE_SAVE);   /* (only the pad: state 31 saved) */
    /* a Track's tap: that slot's States (its name again); an empty slot's: said, States stay */
    ps_clear();
    tap_track(1), adv_ms(20);
    CHECK(rfm_move_states_open(&M) == 1);
    CHECKF(strstr(said, "States, SLOOP") != NULL, "said %s", said);
    rest();
    ps_slots("choralroot", "sloop", "");
    tap_track(2), adv_ms(20);
    CHECK(rfm_move_states_open(&M) == 1);
    CHECKF(strstr(said, "Track 3, empty") != NULL, "said %s", said);
    screen();
    write_pbm("ps_states_empty");
    rest();
    /* a press during the sweep acts at once, white */
    tap_track(0), adv_ms(60);
    pad(ps_pad(4), 100);
    leds(w);
    CHECK(white(w[L_PAD0 + ps_pad(4)]) && M.fw.st_cur[0] == 4);
    pad(ps_pad(4), 0);
    /* Track 4: the layer closes, Track 4 does its job */
    rest();
    tap_track(3), rest();
    CHECK(rfm_move_states_open(&M) < 0);
    back(), rest();
}

/* p8: the gestures on a state pad, as clips in session view: load, save, copy, paste, replace when armed, copy when
 * not, delete (the active one too), paste onto the active one (value 1); Copy / Delete clicked alone: nothing */
static void ps_open0(void)
{
    hold_track(0), hit(30), track(0, 0), rest(), ps_clear();
}
static void test_ps_gestures(void)
{
    const ps_act_t *a;
    ps_states();
    ps_open0();
    hit(ps_pad(4));                                     /* a stored state: loaded, the active one */
    CHECK(npa == 1 && (a = ps_find(RFM_MOVE_ACT_STATE_LOAD, 0)) && a->index == 4 && !strcmp(a->id, "choralroot"));
    CHECK(M.fw.st_cur[0] == 4 && M.fw.sl_pop == 2);
    screen();
    CHECK(lit_rows(10, 24) > 30);
    write_pbm("ps_state_loaded");
    rest(), ps_clear();
    hit(ps_pad(2));                                     /* an empty one: saved there, the active one */
    CHECK(npa == 1 && (a = ps_find(RFM_MOVE_ACT_STATE_SAVE, 0)) && a->index == 2 && M.fw.st_cur[0] == 2 && (M.fw.st_used[0] >> 2 & 1));
    rest(), ps_clear();
    press(MR_CC_COPY), press(MR_CC_DELETE);             /* (clicked alone: nothing) */
    CHECK(npa == 0 && !M.fw.sb_kind && M.fw.st_cur[0] == 2);
    /* Copy held: its icon in the header; + a stored state: copied (dashed), armed */
    button(MR_CC_COPY, 1);
    screen();
    CHECK(pixels(116, 1, 11, 11) > 10);
    hit(ps_pad(0));
    CHECK(npa == 1 && (a = ps_find(RFM_MOVE_ACT_STATE_COPY, 0)) && a->index == 0 && M.fw.sb_armed && M.fw.st_cur[0] == 2);
    screen();
    CHECK(px(1 + 1, 16) && !px(1 + 3, 16));             /* (dashed: 2 on, 2 off) */
    write_pbm("ps_states_copied");
    /* + an empty one: pasted there (not active: value 0), disarmed */
    hit(ps_pad(7));
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_PASTE, 0)) && a->index == 7 && a->value == 0 && !M.fw.sb_armed &&
          (M.fw.st_used[0] >> 7 & 1) && M.fw.st_cur[0] == 2);
    /* not armed: + a stored state copies it; armed: + another stored one pastes over it */
    ps_clear();
    hit(ps_pad(4));
    CHECK(ps_find(RFM_MOVE_ACT_STATE_COPY, 0) && ps_find(RFM_MOVE_ACT_STATE_COPY, 0)->index == 4 && M.fw.sb_armed);
    hit(ps_pad(15));
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_PASTE, 0)) && a->index == 15 && a->value == 0 && !M.fw.sb_armed);
    /* the active one replaced: the host loads it too (value 1) */
    ps_clear();
    hit(ps_pad(0)), hit(ps_pad(2));
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_PASTE, 0)) && a->index == 2 && a->value == 1 && M.fw.st_cur[0] == 2);
    CHECK(!ps_find(RFM_MOVE_ACT_STATE_LOAD, -1));       /* (nothing loads while Copy is held) */
    button(MR_CC_COPY, 0), rest();
    /* Delete held + a stored state: deleted into the buffer (crossed), armed; the active one: none active */
    ps_clear();
    button(MR_CC_DELETE, 1);
    hit(ps_pad(15));
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_DELETE, 0)) && a->index == 15 && !(M.fw.st_used[0] >> 15 & 1) && M.fw.sb_kind == 2);
    screen();
    CHECK(px(1 + 7 * 16, 16 + 12) && px(1 + 7 * 16 + 13, 16 + 12 + 10));   /* (crossed) */
    write_pbm("ps_states_deleted");
    hit(ps_pad(9));                                     /* (an empty one: nothing; Delete never pastes) */
    CHECK(npa == 1);
    hit(ps_pad(2));
    CHECK(ps_count(RFM_MOVE_ACT_STATE_DELETE) == 2 && M.fw.st_cur[0] == -1);
    button(MR_CC_DELETE, 0);
    /* Back: the marks first, then the layer */
    CHECK(M.fw.sb_armed);
    back(), adv_ms(20);
    CHECK(!M.fw.sb_armed && rfm_move_states_open(&M) == 0);
    /* the buffer is ChoralRoot's: SLOOP's States do not offer it */
    tap_track(1), rest(), ps_clear();
    button(MR_CC_COPY, 1), hit(ps_pad(3)), button(MR_CC_COPY, 0);
    CHECK(npa == 0);
    back(), rest();
    CHECK(rfm_move_states_open(&M) < 0);
}

/* p9: step 14 New (the firmware as first installed; none active), Undo after it (its light only while there) */
static void test_ps_new_undo(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    const ps_act_t *a;
    ps_states();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    ps_open0();
    clear();
    step(13, 1);
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_NEW, 0)) && a->slot == 0 && !strcmp(a->id, "choralroot") && M.fw.st_cur[0] == -1);
    leds(w);
    CHECK(white(w[L_STEP0 + 13]) && w[L_ICON0 + 13][0] == 127 && w[L_WHITE0 + W_UNDO][0] == 127 && w[L_PAD0 + 25][0] == 107);
    CHECKF(strstr(said, "New state, ChoralRoot") != NULL, "said %s", said);
    write_pbm("ps_state_new");
    step(13, 0);
    CHECK(M.fw.st_used[0] == (1u | 2u | 16u | 1u << 15));   /* (no stored state touched) */
    ps_clear(), clear();
    press(MR_CC_UNDO);
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_UNDO, 0)) && a->value == 1 && a->index == 1 && M.fw.st_cur[0] == 1);
    leds(w);
    CHECK(w[L_WHITE0 + W_UNDO][0] == 0 && w[L_STEP0 + 13][0] == 63);
    CHECKF(strstr(said, "Undo, new state") != NULL, "said %s", said);
    write_pbm("ps_state_undo");
    ps_clear();
    press(MR_CC_UNDO);                                  /* (one step: nothing more) */
    CHECK(npa == 0);
    /* a New, then a load: the undo is gone */
    step(13, 1), step(13, 0), rest();
    hit(ps_pad(4));
    leds(w);
    CHECK(w[L_WHITE0 + W_UNDO][0] == 0);
    ps_clear();
    press(MR_CC_UNDO);
    CHECK(npa == 0);
    /* a New, then States left: gone too */
    step(13, 1), step(13, 0), back(), rest();
    ps_open0();
    leds(w);
    CHECK(w[L_WHITE0 + W_UNDO][0] == 0);
    back(), rest();
}

/* p10 (the owner's addendum): a new project's routing and channels: tracks 1-4 to firmware 1, 5-8 to 2, 9-12 to 3,
 * 13-16 to none; channels 1..4 in each group of four; the Tracks' tracks 1, 5, 9 (the orchestrator's); MIDI FX "all".
 * A loaded project keeps what it stored; a fresh install's first project is a new one; one slot: as before */
static void test_ps_newproj(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int t, ok;
    ps_two_projects();
    ps_answer = 1;
    grid();
    hit(ps_pad(12));
    CHECK(M.seq.proj_cur == 12 && M.seq.fx.ffilt[0] == 0x000F && M.seq.fx.ffilt[1] == 0x00F0 && M.seq.fx.ffilt[2] == 0x0F00);
    for (t = 0, ok = 1; t < RFM_MOVE_SEQ_TRACKS; t++)
        ok &= M.seq.tr[t].ch == t % 4;
    CHECK(ok && M.s.lock[0] == 0 && M.s.lock[1] == 4 && M.s.lock[2] == 8 && M.seq.fx.mfx[0].filt == 0xFFFF);
    CHECK(ps_find(RFM_MOVE_ACT_ROUTE, -1) && ps_find(RFM_MOVE_ACT_SETTINGS, -1));
    CHECK(PS_BLOB[12][4] == 9);                         /* (saved with them) */
    /* a loaded project keeps its own: project 1 (all routed, track n on channel n) */
    hit(ps_pad(0));
    CHECK(M.seq.proj_cur == 0 && M.seq.fx.ffilt[0] == 0xFFFF && M.seq.fx.ffilt[2] == 0xFFFF && M.seq.tr[9].ch == 9);
    hit(ps_pad(12));
    CHECK(M.seq.proj_cur == 12 && M.seq.fx.ffilt[1] == 0x00F0 && M.seq.tr[5].ch == 1);
    /* session view: the columns in the colour of the slot their track reaches (grey: none) */
    ps_slots("choralroot", "sloop", "felucca");
    back(), rest();
    tap_track(0), rest();
    button(MR_CC_BACK, 1), press(MR_CC_MENU), button(MR_CC_BACK, 0), rest();
    CHECK(M.seq.view == 1);
    clip_put(0, 0, 60), clip_put(4, 0, 60), clip_put(5, 0, 60);
    leds(w);
    CHECK(w[L_STEP0 + 2][0] > w[L_STEP0 + 2][2] && w[L_STEP0 + 8][2] > w[L_STEP0 + 8][0]);   /* (the select steps:
                                                         * tracks 1-4 orange, 5-8 blue) */
    write_pbm("ps_newproj_session");
    press(MR_CC_MENU), rest();
    /* a fresh install: no project stored, the open (empty) project gets them */
    ps_fresh(1);
    rfm_move_project_dir(&M, 0);
    CHECK(M.seq.fx.ffilt[1] == 0x00F0 && M.seq.tr[6].ch == 2 && M.s.lock[2] == 8);
    ps_fresh(1);                                        /* (projects stored: nothing) */
    rfm_move_project_dir(&M, 1u << 3);
    CHECK(M.seq.fx.ffilt[1] == 0xFFFF && M.seq.tr[6].ch == 6 && M.s.lock[2] == 2);
    /* one slot (the web page, the phone apps): as before */
    ps_fresh(1);
    rfm_move_fw_caps(&M, RFM_MOVE_CAP_PROJFW);
    rfm_move_project_dir(&M, 0);
    CHECK(M.seq.fx.ffilt[0] == 0xFFFF && M.seq.tr[6].ch == 6 && M.s.lock[1] == 1);
}

/* ---- (0.12.x) Back held: its shortcuts (the owner's card "Shortcuts on a held Back", 2026-11-22): + Left / Up home,
 * + Loop the whole clip, + Play to bar 1, + Delete every layer closed; at the second button's press, its own job not
 * done, Back's click not either */
static void back_down(void) { button(MR_CC_BACK, 1); }
static void back_up(void) { button(MR_CC_BACK, 0); }
static int wl(const uint8_t w[RFM_MOVE_NLEDS][3], int k) { return w[L_WHITE0 + k][0]; }
static int back_lights(const uint8_t w[RFM_MOVE_NLEDS][3], int up)   /* the held Back's layer on the buttons */
{
    return wl(w, W_BACK) == 127 && wl(w, W_LEFT) >= 100 && wl(w, W_LOOP) >= 100 && wl(w, W_DELETE) >= 100 &&
           wl(w, W_MENU) >= 100 && (up ? wl(w, W_UP) >= 100 : wl(w, W_UP) == 0) && !wl(w, W_RIGHT) && !wl(w, W_DOWN) &&
           !wl(w, W_SHIFT) && !wl(w, W_CAPTURE) && !wl(w, W_UNDO) && !wl(w, W_COPY) && !wl(w, W_MUTE) &&
           bright(w[L_PLAY]) > 300 && !lit(w[L_REC]) && !lit(w[L_SAMPLE]);
}
static int edit_clip_now(void) { return SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel); }

/* gating: a click closes the latest layer and shows nothing of the hold; held past the click's time the shortcuts
 * light (only they); held long and let go alone: nothing */
static void test_back_gating(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    track_view();
    press(MR_CC_LOOP), rest();
    CHECK(M.seq.loop_latch);
    back_down();
    leds(w);
    CHECK(wl(w, W_BACK) == 127 && wl(w, W_SHIFT) == 24 && wl(w, W_RIGHT) > 0 && wl(w, W_UNDO) > 0 && lit(w[L_REC]));
    back_up();
    CHECK(!M.seq.loop_latch);                           /* (the click) */
    press(MR_CC_LOOP), rest(), scr_save();
    back_down(), adv_ms(RFM_MOVE_TAP_MS + 20);
    leds(w);
    CHECK(back_lights(w, 0) && scr_same());
    write_pbm("back_held_track");
    adv_ms(500), back_up();
    CHECK(M.seq.loop_latch && scr_same());              /* (held long, alone: nothing) */
    leds(w);
    CHECK(wl(w, W_SHIFT) == 24 && wl(w, W_RIGHT) > 0 && lit(w[L_REC]));
    back();
    CHECK(!M.seq.loop_latch);
    press(MR_CC_MENU), rest();                          /* session view: Up lit too */
    back_down(), adv_ms(RFM_MOVE_TAP_MS + 20);
    leds(w);
    CHECK(M.seq.view && back_lights(w, 1));
    write_pbm("back_held_session");
    back_up();
    CHECK(M.seq.view);
    press(MR_CC_MENU), rest();
}

/* + Left: bar 1 (track view), track 1 (session view); + Up (session view): scene 1; the rest dead */
static void test_back_home(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int clip, oct;
    track_view();
    clip = edit_clip_now();
    rfmv_seq_note_put(&M, clip, 3 * RFM_MOVE_SEQ_PAGE + 2, 60, 100, 24, 0);
    rfmv_seq_loop_set(&M, clip, 0, 4 * RFM_MOVE_SEQ_PAGE);
    press(MR_CC_RIGHT), press(MR_CC_RIGHT), rest(), clear();
    CHECK(M.seq.page == 2);
    back_down(), button(MR_CC_LEFT, 1);
    leds(w);
    CHECK(M.seq.page == 0 && M.pop == POP_BAR && M.pop_i == 0 && wl(w, W_LEFT) == 127);   /* (at the press) */
    CHECKF(strstr(said, "Bar 1") != NULL, "said \"%s\"", said);
    write_pbm("back_bar1");
    button(MR_CC_LEFT, 0), back_up();
    CHECK(M.seq.page == 0);
    /* under an open Shift menu: the same, and Back's click does not close it */
    press(MR_CC_RIGHT), shift_step(4);
    CHECK(M.seq.page == 1 && M.seq.smenu == RFM_MOVE_SM_TEMPO);
    back_down(), press(MR_CC_LEFT), back_up();
    CHECK(M.seq.page == 0 && M.seq.smenu == RFM_MOVE_SM_TEMPO);
    /* dead in track view: Up (no octave), Right, Down */
    rest(), clear();
    oct = M.s.octave;
    back_down(), press(MR_CC_UP), press(MR_CC_RIGHT), press(MR_CC_DOWN), back_up();
    CHECK(M.s.octave == oct && M.seq.page == 0 && M.seq.smenu == RFM_MOVE_SM_TEMPO &&
          !rfmv_pop_up(&M, POP_OCT) && !rfmv_pop_up(&M, POP_BAR));
    CHECKF(!strstr(said, "Octave") && !strstr(said, "Bar"), "said \"%s\"", said);
    back();
    CHECK(!M.seq.smenu);
    /* session view */
    press(MR_CC_MENU), rest();
    press(MR_CC_RIGHT), press(MR_CC_RIGHT), press(MR_CC_RIGHT), press(MR_CC_DOWN), press(MR_CC_DOWN), rest(), clear();
    CHECK(M.seq.view && M.seq.track_page == 3 && M.seq.scene_off == 2);
    back_down(), button(MR_CC_LEFT, 1);
    CHECK(M.seq.track_page == 0 && M.seq.scene_off == 2 && M.pop == POP_BACK && M.pop_i == BK_TRACK1);
    CHECKF(strstr(said, "Track 1") != NULL, "said \"%s\"", said);
    write_pbm("back_track1");
    button(MR_CC_LEFT, 0), clear(), button(MR_CC_UP, 1);
    leds(w);
    CHECK(M.seq.scene_off == 0 && M.pop == POP_BACK && M.pop_i == BK_SCENE1 && wl(w, W_UP) == 127);
    CHECKF(strstr(said, "Scene 1") != NULL, "said \"%s\"", said);
    write_pbm("back_scene1");
    button(MR_CC_UP, 0), back_up();
    CHECK(M.seq.view && M.seq.track_page == 0 && M.seq.scene_off == 0);
    back_down(), press(MR_CC_RIGHT), press(MR_CC_DOWN), back_up();   /* (dead) */
    CHECK(M.seq.view && M.seq.track_page == 0 && M.seq.scene_off == 0);
    press(MR_CC_MENU), rest();
}

/* + Loop: the selected clip's loop back to the whole clip, one undo step; whole already: the popup, no undo step */
static void test_back_loop(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int clip, un;
    track_view();
    clip = edit_clip_now();
    rfmv_seq_note_put(&M, clip, 2, 60, 100, 24, 0);
    rfmv_seq_note_put(&M, clip, 3 * RFM_MOVE_SEQ_PAGE + 4, 62, 100, 24, 0);
    rfmv_seq_group(&M);
    rfmv_seq_loop_set(&M, clip, RFM_MOVE_SEQ_PAGE, RFM_MOVE_SEQ_PAGE);   /* (the loop: bar 2 only) */
    rest(), clear();
    back_down(), button(MR_CC_LOOP, 1);
    leds(w);
    CHECK(rfmv_seq_loop_lo(&M, clip) == 0 && rfmv_seq_loop_hi(&M, clip) == 4 * RFM_MOVE_SEQ_PAGE);
    CHECK(!M.seq.loop_latch && !(M.seq.mods & SEQ_M_LOOP) && M.pop == POP_LOOP && M.pop_val == 1 && wl(w, W_LOOP) == 127);
    CHECKF(strstr(said, "Loop: 4 bars") != NULL, "said \"%s\"", said);
    write_pbm("back_loop");
    button(MR_CC_LOOP, 0), back_up();
    CHECK(!M.seq.loop_latch && !(M.seq.mods & SEQ_M_LOOP));   /* (Loop's click: no latch) */
    rest();
    press(MR_CC_UNDO);
    CHECK(rfmv_seq_loop_lo(&M, clip) == RFM_MOVE_SEQ_PAGE && rfmv_seq_loop_hi(&M, clip) == 2 * RFM_MOVE_SEQ_PAGE);
    back_down(), press(MR_CC_LOOP), back_up(), rest();
    un = M.seq.un, clear();
    back_down(), press(MR_CC_LOOP);
    CHECK(M.seq.un == un && M.pop == POP_LOOP && rfmv_pop_up(&M, POP_LOOP) && rfmv_seq_loop_hi(&M, clip) == 4 * RFM_MOVE_SEQ_PAGE);
    CHECKF(strstr(said, "Loop: 4 bars") != NULL, "said \"%s\"", said);
    back_up();
    CHECK(!M.seq.loop_latch);
    rest();
}

/* + Play: the transport to bar 1, never started or stopped */
static void test_back_play(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    track_view();
    back_down(), button(MR_CC_PLAY, 1);
    leds(w);
    CHECK(!M.seq.playing && !M.seq.start_pending && M.pop == POP_BACK && M.pop_i == BK_BAR1 && bright(w[L_PLAY]) > 600);
    CHECKF(strstr(said, "Bar 1") != NULL, "said \"%s\"", said);
    write_pbm("back_play");
    button(MR_CC_PLAY, 0), back_up(), adv_ms(100);
    CHECK(!M.seq.playing && !M.seq.start_pending);      /* (stopped stays stopped) */
    press(MR_CC_PLAY), adv_ms(1500);
    CHECK(M.seq.playing && M.seq.T > 8 * RFM_MOVE_SEQ_TPS);
    back_down(), button(MR_CC_PLAY, 1), adv_ms(20);
    CHECK(M.seq.playing && M.seq.T < 4 * RFM_MOVE_SEQ_TPS && M.pop == POP_BACK && M.pop_i == BK_BAR1);
    button(MR_CC_PLAY, 0), back_up(), adv_ms(100);
    CHECK(M.seq.playing);                               /* (playing stays playing, from bar 1) */
    press(MR_CC_PLAY), rest();
    CHECK(!M.seq.playing);
}

/* + Delete: every layer Back's clicks would close, at once; nothing open: "None"; no delete armed, nothing deleted */
static void test_back_delete(void)
{
    int clip;
    track_view();
    clip = edit_clip_now();
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 24, 0);
    button(MR_CC_COPY, 1), step(0, 1), step(0, 0), button(MR_CC_COPY, 0);   /* (the clipboard's marks) */
    CHECK(M.seq.paste);
    shift_step(4);
    press(MR_CC_LOOP), press(MR_CC_MUTE);
    button(MR_CC_SHIFT, 1), button(MR_CC_SHIFT, 0), button(MR_CC_SHIFT, 1), button(MR_CC_SHIFT, 0);
    CHECK(M.seq.smenu == RFM_MOVE_SM_TEMPO && M.seq.loop_latch && M.seq.mute_latch && M.seq.shift_lock && M.seq.paste);
    rest(), clear();
    back_down(), button(MR_CC_DELETE, 1);
    CHECK(!M.seq.smenu && !M.seq.loop_latch && !M.seq.mute_latch && !M.seq.shift_lock && !M.seq.paste && !rfmv_back_has(&M));
    CHECK(!(M.seq.mods & (SEQ_M_SHIFT | SEQ_M_LOOP | SEQ_M_MUTE | SEQ_M_DELETE)) && M.pop == POP_BACK && M.pop_i == BK_CLOSED);
    CHECKF(strstr(said, "All closed") != NULL, "said \"%s\"", said);
    write_pbm("back_closed");
    button(MR_CC_DELETE, 0), back_up();
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && !M.seq.view && !(M.seq.mods & SEQ_M_DELETE) && rfmv_seq_find(&M, clip, 0, 60) >= 0);
    rest(), clear();
    back_down(), button(MR_CC_DELETE, 1);
    CHECK(M.pop == POP_BACK && M.pop_i == BK_NONE);
    CHECKF(strstr(said, "None") != NULL, "said \"%s\"", said);
    write_pbm("back_none");
    button(MR_CC_DELETE, 0), back_up();
    press(MR_CC_MENU), rest();                          /* session view: the latch closes, the view stays */
    press(MR_CC_MUTE);
    CHECK(M.seq.view && M.seq.mute_latch);
    back_down(), press(MR_CC_DELETE), back_up();
    CHECK(M.seq.view && !M.seq.mute_latch && !(M.seq.mods & SEQ_M_DELETE));
    press(MR_CC_MENU), rest();
    /* not in the FM-1 layout (its buttons are the FM-1's): no shortcut, no popup */
    tap_track(0), rest(), clear();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1);
    back_down(), press(MR_CC_LEFT), press(MR_CC_DELETE), press(MR_CC_LOOP), back_up();
    CHECK(!rfmv_pop_up(&M, POP_BACK) && !rfmv_pop_up(&M, POP_LOOP) && !rfmv_pop_up(&M, POP_BAR) && !M.back_cut);
    tap_track(1), rest();
}

/* ---- (0.13.0, the owner's, 2026-11-22: "Holding a track button and turning the volume knob should change that
 * firmware's volume level and also visually update to communicate the change") a held Track 1..3 + the volume knob:
 * the slot's level popup over the Track's menu in a note layout, session view and the FM-1 layout (it showed only in
 * the firmware browser); a touch shows it too; a second later the menu is back */
static void test_track_volume(void)
{
    static uint8_t MENU[MR_SCREEN_BYTES], POP[MR_SCREEN_BYTES];
    int l0 = 0, v, a0;
    for (v = 0; v < 3; v++) {                           /* a note layout, session view, the FM-1 layout */
        if (v == 1)
            session_view();
        else {
            fresh();
            hold_track(1), hit(v ? 24 : 26), track(1, 0), rest();   /* (Felucca on Track 2: Scale, or the FM-1 layout) */
            tap_track(1), rest();
        }
        CHECK(v == 2 ? M.s.mode == RFM_MOVE_MODE_FM1 : M.s.mode == RFM_MOVE_MODE_SEQ && M.seq.view == (v == 1));
        a0 = M.fw.active;
        hold_track(1);
        screen(), memcpy(MENU, SCR, sizeof MENU);
        l0 = M.fw.level[1];
        clear();
        raw(0xB0, (uint8_t)(MR_CC_KNOB1 + 8), 3);       /* the volume knob, 3 up */
        CHECK(M.fw.level[1] == l0 + 3 && count(RFM_MOVE_ACT_FW_LEVEL) == 1 && !count(RFM_MOVE_ACT_MASTER));
        screen(), memcpy(POP, SCR, sizeof POP);
        CHECK(memcmp(POP, MENU, sizeof MENU) != 0 && rfmv_fw_level_shown(&M));   /* the level, not the menu */
        if (v == 0)
            write_pbm("trk_volume");
        raw(0xB0, (uint8_t)(MR_CC_KNOB1 + 8), 125);     /* 3 down: the level as it was */
        CHECK(M.fw.level[1] == l0);
        adv_ms(RFM_MOVE_POPUP_MS + 50);
        screen();
        CHECK(!memcmp(SCR, MENU, sizeof MENU));         /* the Track's menu is back while it is still held */
        touch(8, 1);                                    /* a touch alone: the level shows, nothing changes */
        screen();
        CHECK(M.fw.level[1] == l0 && rfmv_fw_level_shown(&M) && memcmp(SCR, MENU, sizeof MENU) != 0);
        touch(8, 0);
        track(1, 0), rest();
        CHECK(M.fw.active == a0 && M.seq.view == (v == 1));   /* (the Track used by the knob: its release no tap) */
    }
    clear();
    raw(0xB0, (uint8_t)(MR_CC_KNOB1 + 8), 3);           /* no Track held: the master volume, as ever */
    CHECK(M.fw.level[1] == l0 && !rfmv_fw_level_shown(&M) && count(RFM_MOVE_ACT_MASTER) == 1);
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
    test_sv_marks();
    test_sv_anywhere();
    test_sv_scenes();
    test_sv_launch_q();
    test_sv_clipset();
    test_sv_dbl_stop();
    test_arp_toggle();
    test_arp_lanes();
    test_octave();
    test_held_step();
    test_held_over_menu();
    test_menu_stays();
    test_menu_heads();
    test_menu_over_views();
    test_t4_layer();
    test_loop_mute_taps();
    test_clear_clip();
    test_slot_colours();
    test_fill();
    test_knob_lights();
    test_dead_presses();
    test_browser_icons();
    test_ps_blob();
    test_ps_loading();
    test_ps_limit();
    test_ps_new();
    test_ps_held();
    test_ps_pad7();
    test_ps_layer();
    test_ps_gestures();
    test_ps_new_undo();
    test_ps_newproj();
    test_back_gating();
    test_back_home();
    test_back_loop();
    test_back_play();
    test_back_delete();
    test_track_volume();
    (void)count, (void)find, (void)knob, (void)jog, (void)jog_click, (void)dbl_track, (void)m_nlat;
    return check_done("move_sessview");
}
