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
    /* track view, Track 2 held, a track's step: (0.12.x, the owner's) a click selects it at its press */
    press(MR_CC_MENU);
    CHECK(!M.seq.view);
    hold_track(1);
    step(5, 1);
    leds(w);
    CHECK(white(w[L_STEP0 + 5]));                       /* (pressed: white at once) */
    CHECK(M.s.lock[1] == 5 && M.seq.sel_track == 5 && M.lk_word == LK_LOCKED);
    step(5, 0);
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
    CHECK(!(M.seq.fx.ffilt[1] >> 6 & 1) && M.s.lock[1] == 6 && lit(w[L_STEP0 + 7]));   /* (0.12.x: the press selected it;
                                                         * not routed, it stays selected: blinking dim) */
    /* a double click: the solo; its first press selected it */
    step(8, 1), step(8, 0), step(8, 1), step(8, 0);
    CHECK(M.fsolo[1] == 9 && M.lk_word == LK_SOLO);
    adv_ms(RFM_MOVE_DOUBLE_MS + 100);
    CHECK(M.fsolo[1] == 9 && M.s.lock[1] == 8);
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
    /* the knobs it uses are lit (0.14.0, the owner's: all but knob 4, CHANCE gone: dark, and its turn nothing) */
    leds(w);
    for (i = 0; i < 8; i++)
        CHECKF(!lit(w[L_RING0 + i]) == (i == 3), "knob %d", i + 1);
    {
        const rfm_move_arp_t was = *ap;
        knob(3, 50), knob(3, -50);
        CHECK(!memcmp(&was, ap, sizeof was) && ap->chance == 100);
        rest();
    }
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
    {   /* (seen on the Move) "Sequencer" whole on the screen: nothing lit in the edge columns of its rows */
        int y, edge = 0, body = 0;
        write_pbm("view_named_seq");
        for (y = 8; y < 48; y++)
            edge += px(0, y) + px(1, y) + px(126, y) + px(127, y), body += px(20, y) + px(64, y) + px(100, y);
        CHECK(!edge && body);
    }
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
    jog(-1), jog_click(), jog(1), rest();               /* (0.14.0: on from the main page; a lane's click is random) */
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
    const int wf = M.seq.smenu == RFM_MOVE_SM_WORKFLOW;
    rest(), scr_save();
    snprintf(nm, sizeof nm, "menu_%s_rest", name);
    write_pbm(nm);
    for (k = 0, ok = 1; k < 8; k++)                     /* (0.13.0: the first one long: the jog's cursor; 0.14.0,
                                                         * the owner's 2026-11-24: Workflow none, its rows are the
                                                         * knobs) */
        ok &= wf ? !lit_rows(17, 19) : (bits >> k & 1) ? tick(k, !(bits & ((1 << k) - 1))) : !px(6 + k * 16, 17);
    CHECKF(ok && rings_menu(bits), "%s: ticks at rest", name);
    for (k = 0; k < 8; k++) {
        touch(k, 1);
        if (bits >> k & 1) {
            screen();
            CHECKF((wf ? !lit_rows(17, 19) && px(1, 19 + k * 15 + 1) && px(126, 19 + k * 15 + 1) : tick(k, 1)) && !scr_same(),
                   "%s: knob %d touched", name, k + 1);
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
    shift_step(9), menu_heads("velocity", 0x03);         /* (0.13.0: knob 2 the option) */
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
    CHECKF((M.seq.smenu == RFM_MOVE_SM_WORKFLOW ? px(1, 20) && !lit_rows(17, 19) : tick(0, 1)) && M.pop != POP_ENC &&
           M.pop != POP_TRKINFO && M.pop != POP_CC && M.pop != POP_TRIG, "%s: touch: header, pop %d", where, M.pop);
    snprintf(nm, sizeof nm, "menu_over_%s_k1", where);
    write_pbm(nm);
    knob(0, 1);
    touch(0, 0);
    CHECKF(!count(RFM_MOVE_ACT_TURN), "%s: no FM-1 turn", where);
    if (M.seq.smenu == RFM_MOVE_SM_TEMPO) {
        CHECKF(rfm_move_seq_bpm10(&M) == bpm + 10, "%s: knob 1 the tempo", where);
        jog_click(), jog(-1), jog_click();              /* (0.13.0: the jog's click edits the selected tempo) */
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
    jog_click(), jog(-1), jog_click();                  /* (0.13.0: the click edits the tempo, again: selects) */
    CHECK(rfm_move_seq_bpm10(&M) == 1200 && !M.seq.sm_edit);
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
    jog(2), rest();                                     /* (0.12.x: past the FM-1 page) */
    CHECK(!M.seq.trig && !M.seq.fwp && M.s.bank == 0);
    clip = SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel);
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 24, 0);
    rfmv_seq_lock_set(&M, clip, 0, M.s.cc[M.s.bank][2], 127);
    step(0, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    leds(w);
    CHECK(lit(w[L_RING0 + 0]) && bright(w[L_RING0 + 2]) > 3 * bright(w[L_RING0 + 0]));
    write_pbm("cc_locks_dim");
    step(0, 0), jog(-2), rest();
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
    CHECK(on == 2 && (w[L_ICON0 + 0][0] == 127 || w[L_ICON0 + 1][0] == 127) && w[L_ICON0 + 12][0] == 127);   /* (the
                                                         * scope only: no filter on; 0.14.0: step 13 Random, a function) */
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
    {                                                   /* (project 1 is an older one: all routed, track n on channel
                                                         * n; an empty project made current now gets a new project's
                                                         * routing) */
        static const int ALL[RFM_MOVE_FW_SLOTS] = { 0xFFFF, 0xFFFF, 0xFFFF };
        int t;
        rfm_move_route_set(&M, ALL, NULL);
        for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
            M.seq.tr[t].ch = (uint8_t)t;
    }
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
    /* the click (0.14.0: at its release; the press shows the project's info): the loading screen at once, its rows from
     * the grid's knowledge (no answer yet), the Tracks dark */
    pad(ps_pad(6), 100);
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_LOAD) == 0 && M.seq.proj_pending < 0 && M.pop == POP_PROJ);
    pad(ps_pad(6), 0);
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
static void ps_states(void)                             /* ChoralRoot's States: 1, 2, 5 and 16 stored, 2 the active one
                                                         * (0.14.0: ignored; bit 0, a state file at index 0 the host has
                                                         * not moved yet: pad 1 is the project's all the same) */
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
    CHECK(white(w[L_PAD0 + 24]));                       /* (0.14.0: pad 1, the project's own sound: white, breathing) */
    CHECK(w[L_PAD0 + 25][0] == 107 && w[L_PAD0 + 28][0] == 107 && w[L_PAD0 + 23][0] == 107 && !lit(w[L_PAD0 + 26]));
    CHECK(!lit(w[L_STEP0 + 0]) && w[L_STEP0 + 13][0] == 63 && w[L_ICON0 + 13][0] == 127 && w[L_ICON0 + 12][0] == 0);
    CHECK(!lit(w[L_RING0 + 0]) && w[L_WHITE0 + W_BACK][0] == 80 && w[L_WHITE0 + W_UNDO][0] == 0);
    CHECK(w[L_TRACK0 + 0][0] == 255 && w[L_TRACK0 + 1][2] == 63);
    screen();
    CHECK(px(1 + 1, 16 + 1) && px(1 + 16, 16) && !px(1 + 16 + 1, 16 + 1) && px(1 + 32 + 6, 16 + 5) && !px(1 + 32 + 1, 16 + 1));
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
    CHECK(white(w[L_PAD0 + ps_pad(4)]) && M.fw.undo == 1);   /* (0.14.0: loaded into the project) */
    pad(ps_pad(4), 0);
    /* Track 4: the layer closes, Track 4 does its job */
    rest();
    tap_track(3), rest();
    CHECK(rfm_move_states_open(&M) < 0);
    back(), rest();
}

/* p8: the gestures on a state pad, as clips in session view; (0.14.0, the owner's 2026-11-24) pads 2..32 a library: a
 * stored one loads into the project (none active), an empty one stores the sound; copy, paste, replace when armed,
 * copy when not, delete; Copy / Delete clicked alone: nothing. Pad 1's own gestures: test_o14_states_project */
static void ps_open0(void)
{
    hold_track(0), hit(30), track(0, 0), rest(), ps_clear();
}
static void test_ps_gestures(void)
{
    const ps_act_t *a;
    ps_states();
    ps_open0();
    hit(ps_pad(4));                                     /* a stored state: loaded into the project; pad 1 stays */
    CHECK(npa == 1 && (a = ps_find(RFM_MOVE_ACT_STATE_LOAD, 0)) && a->index == 4 && a->value == 0 && !strcmp(a->id, "choralroot"));
    CHECK(M.fw.st_cur[0] == -1 && M.fw.sl_pop == 2 && M.fw.undo == 1);
    screen();
    CHECK(lit_rows(10, 24) > 30);
    write_pbm("ps_state_loaded");
    rest(), ps_clear();
    hit(ps_pad(2));                                     /* an empty one: the sound stored there (not active) */
    CHECK(npa == 1 && (a = ps_find(RFM_MOVE_ACT_STATE_SAVE, 0)) && a->index == 2 && M.fw.st_cur[0] == -1 && (M.fw.st_used[0] >> 2 & 1));
    rest(), ps_clear();
    press(MR_CC_COPY), press(MR_CC_DELETE);             /* (clicked alone: nothing) */
    CHECK(npa == 0 && !M.fw.sb_kind);
    /* Copy held: its icon in the header; + a stored state: copied (dashed), armed */
    button(MR_CC_COPY, 1);
    screen();
    CHECK(pixels(116, 1, 11, 11) > 10);
    hit(ps_pad(1));
    CHECK(npa == 1 && (a = ps_find(RFM_MOVE_ACT_STATE_COPY, 0)) && a->index == 1 && M.fw.sb_armed);
    screen();
    CHECK(px(17 + 1, 16) && !px(17 + 3, 16));           /* (dashed: 2 on, 2 off) */
    write_pbm("ps_states_copied");
    /* + an empty one: pasted there (value 0), disarmed; the undo of the load above kept (the sound unchanged) */
    hit(ps_pad(7));
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_PASTE, 0)) && a->index == 7 && a->value == 0 && !M.fw.sb_armed &&
          (M.fw.st_used[0] >> 7 & 1) && M.fw.undo == 1);
    /* not armed: + a stored state copies it; armed: + another stored one pastes over it */
    ps_clear();
    hit(ps_pad(4));
    CHECK(ps_find(RFM_MOVE_ACT_STATE_COPY, 0) && ps_find(RFM_MOVE_ACT_STATE_COPY, 0)->index == 4 && M.fw.sb_armed);
    hit(ps_pad(15));
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_PASTE, 0)) && a->index == 15 && a->value == 0 && !M.fw.sb_armed);
    CHECK(!ps_find(RFM_MOVE_ACT_STATE_LOAD, -1));       /* (nothing loads while Copy is held) */
    button(MR_CC_COPY, 0), rest();
    /* Delete held + a stored state: deleted into the buffer (crossed), armed */
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

/* p9: step 14 New (the project's sound as first installed; none active), Undo after it (its light only while there);
 * (0.14.0) Undo after a library load too, one step */
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
    CHECK(white(w[L_PAD0 + 24]));                       /* (pad 1 stays the one you are in) */
    CHECKF(strstr(said, "New state, ChoralRoot") != NULL, "said %s", said);
    write_pbm("ps_state_new");
    step(13, 0);
    CHECK(M.fw.st_used[0] == (1u | 2u | 16u | 1u << 15));   /* (no stored state touched) */
    ps_clear(), clear();
    press(MR_CC_UNDO);
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_UNDO, 0)) && a->value == -1 && a->index == 255 && M.fw.st_cur[0] == -1);
    leds(w);
    CHECK(w[L_WHITE0 + W_UNDO][0] == 0 && w[L_STEP0 + 13][0] == 63);
    CHECKF(strstr(said, "Undo, new state") != NULL, "said %s", said);
    write_pbm("ps_state_undo");
    ps_clear();
    press(MR_CC_UNDO);                                  /* (one step: nothing more) */
    CHECK(npa == 0);
    /* a library load: Undo brings back the sound from before it */
    rest(), ps_clear(), clear();
    hit(ps_pad(4));
    leds(w);
    CHECK(w[L_WHITE0 + W_UNDO][0] == 127 && white(w[L_PAD0 + 24]));
    press(MR_CC_UNDO);
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_UNDO, 0)) && a->value == -1 && a->index == 255 && npa == 2);
    CHECKF(strstr(said, "Undo, State 5") != NULL, "said %s", said);
    write_pbm("o14_states_undo_load");
    ps_clear();
    press(MR_CC_UNDO);
    CHECK(npa == 0);
    /* a New, then a load: the load's undo (one step) */
    step(13, 1), step(13, 0), rest();
    hit(ps_pad(4)), rest();
    ps_clear();
    press(MR_CC_UNDO), press(MR_CC_UNDO);
    CHECK(ps_count(RFM_MOVE_ACT_STATE_UNDO) == 1);
    /* a New, then States left: gone */
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

/* ================================================================ (0.12.x) the owner's 2026-11-22 items === */
/* item 1: a held Track's step selects its track at the press (routed too); the double click solos at its second press,
 * the track stays selected; a hold flips the routing from what it was before the press; nothing more at 300 ms or at
 * the release */
static void test_fwp_tkclick(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    track_view();
    CHECK(M.fw.active == 1 && M.seq.sel_track == 1 && (M.seq.fx.ffilt[1] >> 6 & 1));
    rfm_move_set_speech(&M, 0);
    M.seq.fx.ffilt[1] = (uint16_t)(M.seq.fx.ffilt[1] & ~(1u << 9));   /* (track 10: not routed to slot 2) */
    hold_track(1), clear();
    /* a click on a routed track: selected at the press: the screen's words, the track edited */
    step(6, 1);
    screen();
    CHECK(M.s.lock[1] == 6 && M.seq.sel_track == 6 && M.lk_word == LK_LOCKED && M.menu_what == MENU_LOCK && M.menu_i == 6);
    leds(w);
    CHECK(white(w[L_STEP0 + 6]));                       /* (pressed: white) */
    write_pbm("fwp_tk_press");
    step(6, 0);
    leds(w);
    CHECK(!white(w[L_STEP0 + 6]) && M.seq.tk_pend == 6);   /* (let go: the selected track's blink, no wait shown) */
    adv_ms(RFM_MOVE_DOUBLE_MS + 30);
    CHECK(M.s.lock[1] == 6 && M.lk_word == LK_LOCKED && M.seq.tk_pend < 0);
    /* a click on a not-routed track: selected and routed at the press */
    step(9, 1);
    CHECK(M.s.lock[1] == 9 && M.seq.sel_track == 9 && (M.seq.fx.ffilt[1] >> 9 & 1));
    step(9, 0), adv_ms(RFM_MOVE_DOUBLE_MS + 30);
    CHECK(M.s.lock[1] == 9 && (M.seq.fx.ffilt[1] >> 9 & 1));
    /* a double click: the first press selects, the second solos; the track stays selected */
    now_ms += 500;
    step(4, 1), step(4, 0);
    CHECK(M.s.lock[1] == 4 && !M.fsolo[1]);
    step(4, 1);
    CHECK(M.fsolo[1] == 5 && M.s.lock[1] == 4 && M.lk_word == LK_SOLO);
    step(4, 0), adv_ms(RFM_MOVE_DOUBLE_MS + 100);
    CHECK(M.fsolo[1] == 5 && M.s.lock[1] == 4 && M.seq.sel_track == 4);
    step(4, 1), step(4, 0), step(4, 1), step(4, 0);     /* (the solo off again) */
    CHECK(!M.fsolo[1]);
    adv_ms(RFM_MOVE_DOUBLE_MS + 100);
    /* a hold on a routed track: selected at the press, not routed at 300 ms (still selected), its release nothing */
    now_ms += 500;
    step(11, 1);
    CHECK(M.s.lock[1] == 11 && (M.seq.fx.ffilt[1] >> 11 & 1));
    adv_ms(RFM_MOVE_SEQ_HOLD_MS - 50);
    CHECK(M.seq.fx.ffilt[1] >> 11 & 1);
    adv_ms(70);
    CHECK(!(M.seq.fx.ffilt[1] >> 11 & 1) && M.lk_word == LK_OFF && M.s.lock[1] == 11);
    step(11, 0), adv_ms(RFM_MOVE_DOUBLE_MS + 100);
    CHECK(!(M.seq.fx.ffilt[1] >> 11 & 1) && M.s.lock[1] == 11);
    /* a hold on a not-routed track: routed at the press, still routed at 300 ms */
    now_ms += 500;
    M.seq.fx.ffilt[1] = (uint16_t)(M.seq.fx.ffilt[1] & ~(1u << 13));
    step(13, 1);
    CHECK(M.seq.fx.ffilt[1] >> 13 & 1);
    adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    CHECK((M.seq.fx.ffilt[1] >> 13 & 1) && M.lk_word == LK_ON && M.s.lock[1] == 13);
    step(13, 0), adv_ms(RFM_MOVE_DOUBLE_MS + 100);
    CHECK(M.seq.fx.ffilt[1] >> 13 & 1);
    /* the Track let go right after a click: nothing more */
    now_ms += 500;
    step(2, 1), step(2, 0);
    CHECK(M.s.lock[1] == 2);
    track(1, 0);
    CHECK(M.s.lock[1] == 2 && M.seq.sel_track == 2 && M.s.mode == RFM_MOVE_MODE_SEQ && !M.seq.view);
    rest();
    /* session view: the steps under a held Track follow the same rule */
    press(MR_CC_MENU), rest();
    CHECK(M.seq.view);
    hold_track(1), step(5, 1);
    CHECK(M.s.lock[1] == 5);
    step(5, 0), track(1, 0), rest();
    press(MR_CC_MENU), rest();
}

/* item 2: a note layout's FM-1 knob page (second: TRIG, FM-1, banks 1..4); level 1 the knobs, level 2 (the jog's
 * click) the buttons and steps too; the pads the note layout's, the jog ours */
static int acts_of(int kind, int index)
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += A[i].kind == kind && A[i].index == index;
    return n;
}
static void fwp_png_labels(void)
{
    static const char *lbl[RFM_MOVE_LBL_N];
    int i;
    for (i = 0; i < RFM_MOVE_LBL_N; i++)
        lbl[i] = NULL;
    lbl[rfmv_enc_slot(RFM_MOVE_E_K1)] = "Cutoff";
    lbl[rfmv_enc_slot(RFM_MOVE_E_K4)] = "";         /* (no job: dark, dead) */
    rfm_move_set_labels(&M, lbl);
}
static int step_has(int clip, int s)
{
    int lo;
    return rfmv_seq_range(&M, clip, M.seq.page * RFM_MOVE_SEQ_PAGE + s, &lo) > lo;
}
static void test_fwp_page(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3], lv[RFM_MOVE_NLEVELS];
    int i, clip, oct;
    const rfm_move_act_t *a;
    track_view();
    fwp_png_labels();
    clip = edit_clip_now();
    CHECK(M.seq.trig && !M.seq.fwp && M.s.bank == 0);
    /* the page order: TRIG, FM-1, bank 1 .. 4; the stored bank keeps its meaning */
    jog(1);
    CHECK(!M.seq.trig && M.seq.fwp && M.s.bank == 0 && M.pop == POP_BANK && rfmv_fwp_page(&M) && rfmv_fwp_live(&M));
    CHECKF(strstr(said, "FM-1 knobs") != NULL, "said \"%s\"", said);
    screen();
    CHECK(lit_rows(60, 64) > 0 && lit_rows(0, 56) > 0);  /* (the words, the page strip under them) */
    write_pbm("fwp_page");
    jog(1);
    CHECK(!M.seq.fwp && !M.seq.trig && M.s.bank == 0);
    jog(3);
    CHECK(M.s.bank == 3);
    jog(5);
    CHECK(M.s.bank == 3);                               /* (the last page: clamped) */
    M.s.bank = 2;                                       /* (a stored bank 3: still bank 3) */
    jog(-1);
    CHECK(M.s.bank == 1 && !M.seq.fwp);
    jog(-1);
    CHECK(M.s.bank == 0 && !M.seq.fwp);
    M.s.bank = 2, jog(-3);                              /* (bank 3 -> FM-1 in one turn: the bank stays as it was) */
    CHECK(M.seq.fwp && !M.seq.trig && M.s.bank == 2);
    M.s.bank = 2;
    jog(-1);
    CHECK(M.seq.trig && M.s.bank == 2);
    jog(1), rest(), clear();
    CHECK(M.seq.fwp && M.s.bank == 2);
    /* level 1: the knobs reach the firmware as in the FM-1 layout (an FM-1 detent every 2 ticks); knob 4 none */
    knob(4, 2);
    a = find(RFM_MOVE_ACT_TURN);
    CHECK(a && a->index == RFM_MOVE_E_K1 && a->value == 1 && count(RFM_MOVE_ACT_MIDI) == 0);
    knob(0, 2), knob(1, 2), knob(2, 2), knob(7, 2);
    CHECK(acts_of(RFM_MOVE_ACT_TURN, RFM_MOVE_E_SELECT) == 1 && acts_of(RFM_MOVE_ACT_TURN, RFM_MOVE_E_PRESETS) == 1 &&
          acts_of(RFM_MOVE_ACT_TURN, RFM_MOVE_E_ALGO) == 1 && acts_of(RFM_MOVE_ACT_TURN, RFM_MOVE_E_K4) == 1);
    clear(), knob(3, 4);
    CHECK(nact == 0);
    rest();
    leds(w);                                            /* (R4: the knobs with a job lit; 4 and K4 ("") dark) */
    for (i = 0; i < 8; i++)
        CHECKF(lit(w[L_RING0 + i]) == (i != 3 && i != 7), "knob %d", i + 1);
    /* a touch: the knob's name (R5), its readout's value once it comes, full screen (Popup Full) */
    clear(), touch(4, 1);
    CHECK(M.pop == POP_ENC && M.pop_i == 4 && M.pop_blank && rfmv_pop_up(&M, POP_ENC));
    CHECKF(strstr(said, "Cutoff") != NULL, "said \"%s\"", said);
    leds(w);
    CHECK(bright(w[L_RING0 + 4]) > bright(w[L_RING0 + 5]));
    write_pbm("fwp_l1_touch");
    knob(4, 2);
    rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "Cutoff", "1.2k", 400, 0, 0);
    screen();
    CHECK(M.pop == POP_ENC && M.rd[rfmv_enc_slot(RFM_MOVE_E_K1)].on);
    write_pbm("fwp_l1_value");
    M.s.pop_half = 1;
    write_pbm("fwp_l1_half");
    M.s.pop_half = 0;
    touch(4, 0);
    CHECK(!rfmv_pop_up(&M, POP_ENC));                   /* (as the FM-1 layout: the release ends it) */
    touch(3, 1);
    CHECK(!rfmv_pop_up(&M, POP_ENC));                   /* (knob 4: no job, no popup) */
    touch(3, 0), rest();
    /* the rest is the sequencer's: the pads play the track, the steps edit the clip, Play its transport */
    clear(), hit(0);
    CHECK(count(RFM_MOVE_ACT_MIDI) == 2 && count(RFM_MOVE_ACT_KEY) == 0);
    step(2, 1), step(2, 0);
    CHECK(rfmv_seq_clip_used(&M, clip) && count(RFM_MOVE_ACT_KEY) == 0);
    /* a held step: its knobs are the TRIG page's (lit as there), not the firmware's */
    step(2, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20), clear();
    knob(1, 1);
    CHECK(count(RFM_MOVE_ACT_TURN) == 0 && rfmv_seq_trig(&M, clip, 2, RFM_MOVE_TRIG_RATCHET) == 2);
    step(2, 0), rest();
    /* the jog's click: level 2 at the press, named a second ("Knobs+Keys") */
    clear();
    raw(0xB0, MR_CC_JOG_CLICK, 127);
    CHECK(M.seq.fwp_full && rfmv_fwp_full(&M) && M.pop == POP_FWLV && M.pop_i == 1 && rfmv_pop_up(&M, POP_FWLV));
    CHECKF(strstr(said, "Knobs and keys") != NULL, "said \"%s\"", said);
    write_pbm("fwp_level2_pop");
    raw(0xB0, MR_CC_JOG_CLICK, 0), rest();
    /* level 2: the FM-1's screen, its LEDs on the steps and taken buttons, the step icons dark */
    memset(lv, 0, sizeof lv);
    for (i = 0; i < RFM_MOVE_NLEVELS; i++)
        lv[i] = (uint8_t)(i * 37 % 256);
    rfm_move_panel(&M, lv, 0);
    leds(w);
    for (i = 0; i < 16; i++)
        CHECK(!w[L_ICON0 + i][0]);
    for (i = 0; i < 16; i++)                            /* (the FM-1's key levels: white, never the clip's colours) */
        CHECK(lit(w[L_STEP0 + i]) && w[L_STEP0 + i][0] == w[L_STEP0 + i][1] && w[L_STEP0 + i][1] == w[L_STEP0 + i][2]);
    CHECK(lit(w[L_PLAY]) || lit(w[L_REC]) || lit(w[L_SAMPLE]));
    CHECK(rfmv_half(&M) && rfm_move_fwp_level(&M) == 2);
    write_pbm("fwp_level2");
    /* a step: the FM-1's key, not the clip; a mapped button: the FM-1's; Play the firmware's PLAY, not the transport */
    clear();
    step(5, 1);
    a = find(RFM_MOVE_ACT_KEY);
    CHECK(a && a->index == rfm_move_step_key(5) && a->down && a->vel == 100 && !step_has(clip, 5));
    leds(w);
    CHECK(lit(w[L_STEP0 + 5]) && !white(w[L_STEP0 + 5]));   /* (pressed: the FM-1 layout's colour) */
    step(5, 0);
    CHECK(count(RFM_MOVE_ACT_KEY) == 2 && !step_has(clip, 5));
    clear(), press(MR_CC_CAPTURE), press(MR_CC_PLAY), press(MR_CC_SHIFT), press(MR_CC_MENU);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_FX) == 2 && acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_PLAY) == 2 &&
          acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_GLO) == 2 && acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_HOME) == 2);
    CHECK(!M.seq.playing && !M.seq.view && M.s.mode == RFM_MOVE_MODE_SEQ);
    /* a mapped button held: a key's label at once (the FM-1 layout's) */
    button(MR_CC_SHIFT, 1), step(0, 1);
    CHECK(M.pop == POP_LABEL);
    step(0, 0), button(MR_CC_SHIFT, 0), rest();
    /* the pads still play the track; Up / Down the octave; the knobs still the firmware's */
    clear(), hit(0);
    CHECK(count(RFM_MOVE_ACT_MIDI) == 2 && count(RFM_MOVE_ACT_KEY) == 0);
    oct = M.s.octave, press(MR_CC_UP);
    CHECK(M.s.octave == oct + 1 && count(RFM_MOVE_ACT_BTN) == 0);
    press(MR_CC_DOWN), clear(), knob(5, 2);
    CHECK(acts_of(RFM_MOVE_ACT_TURN, RFM_MOVE_E_K2) == 1);
    rest();
    /* Back's click (0.14.0, the owner's 2026-11-24: "Back should exit out of the FM-1 knob lock"): Knobs; the click
     * again: level 2 */
    clear(), back();
    CHECK(!rfmv_fwp_full(&M) && rfm_move_fwp_level(&M) == 1 && count(RFM_MOVE_ACT_BTN) == 0 && M.pop == POP_FWLV);
    jog_click(), rest();
    CHECK(rfmv_fwp_full(&M));
    /* (0.14.0, the owner's) level 2's jog is the FM-1 layout's: its turn the target (SELECT), the page stays; the
     * click ends the takeover of the steps and buttons; the click again: level 2 */
    step(7, 1);                                          /* (a key held: let go when level 2 ends) */
    clear(), jog(1);
    CHECK(rfmv_fwp_full(&M) && acts_of(RFM_MOVE_ACT_TURN, RFM_MOVE_E_SELECT) == 1 && rfmv_seq_kpage(&M) == 1);
    rest(), clear(), jog_click();
    CHECK(!rfmv_fwp_full(&M) && !M.seq.fwp_full && acts_of(RFM_MOVE_ACT_KEY, rfm_move_step_key(7)) == 1);
    step(7, 0), step(9, 1), step(9, 0);
    CHECK(step_has(clip, 9) && count(RFM_MOVE_ACT_KEY) == 1);   /* (the key let go; step 10 the clip's again) */
    clear(), press(MR_CC_CAPTURE);
    CHECK(count(RFM_MOVE_ACT_BTN) == 0);
    rest(), jog_click();
    CHECK(rfmv_fwp_full(&M));
    rest();
    /* the double click again: level 1 ("Knobs") */
    clear(), jog_click();
    CHECK(!M.seq.fwp_full && M.pop == POP_FWLV && M.pop_i == 0 && rfm_move_fwp_level(&M) == 1);
    CHECKF(strstr(said, "Knobs") != NULL, "said \"%s\"", said);
    write_pbm("fwp_level1_pop");
    rest();
    /* a held Track: its menu over the page (the steps its tracks); Track 4 held: its layer */
    jog_click(), rest();
    hold_track(1), clear(), step(3, 1);
    CHECK(M.s.lock[1] == 3 && count(RFM_MOVE_ACT_KEY) == 0);
    step(3, 0), track(1, 0), rest();
    /* a Track tap: the slot's layout again, its track 4 (the lock just set) on its own page (TRIG); (0.14.0, the
     * owner's 2026-11-24) another track: level 2 over */
    tap_track(1), rest();
    CHECK(M.seq.trig && !M.seq.fwp && !rfmv_fwp_full(&M) && !M.seq.fwp_full);
    /* an empty slot (Track 3: nothing loaded): the page is there, its knobs dark and dead, its click nothing */
    hold_track(2), hit(26), track(2, 0), rest();
    tap_track(2), rest();
    CHECK(M.fw.active == 2 && M.s.mode == RFM_MOVE_MODE_SEQ);
    clear(), jog(1);
    CHECK(M.seq.fwp && rfmv_fwp_page(&M) && !rfmv_fwp_live(&M));
    CHECKF(strstr(said, "no firmware") != NULL, "said \"%s\"", said);
    write_pbm("fwp_empty");
    clear(), knob(4, 4), touch(5, 1), jog_click();
    CHECK(count(RFM_MOVE_ACT_TURN) == 0 && !M.seq.fwp_full && M.pop != POP_ENC);
    leds(w);
    for (i = 0; i < 8; i++)
        CHECK(!lit(w[L_RING0 + i]));
    touch(5, 0), rest();
    rfm_move_set_labels(&M, NULL);
    tap_track(1), rest();
}

/* item 3: a Track 1..3 tap into a note layout: the steps show the 16 tracks a moment (the selected one blinking),
 * then the clip's steps; a step press ends it and is a clip step at once */
static void test_fwp_trackflash(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3], w0[RFM_MOVE_NLEDS][3];
    int i, st = 0, pd = 0, clip;
    track_view();
    rfm_move_set_speech(&M, 0);
    clip = edit_clip_now();
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 24, 0);
    M.seq.fx.ffilt[1] = (uint16_t)(M.seq.fx.ffilt[1] & ~(1u << 9));   /* (track 10 not routed to slot 2) */
    rest();
    leds(w0);                                           /* (the clip's steps) */
    led_flush();
    tap_track(1);                                       /* (the slot already played: which track am I on) */
    CHECK(M.seq.tflash && rfmv_seq_tflash_on(&M) && M.pop == POP_LAYOUT);
    leds(w);
    CHECK(white(w[L_STEP0 + 1]) && !lit(w[L_STEP0 + 9]) && lit(w[L_STEP0 + 4]) && !white(w[L_STEP0 + 4]));
    for (i = 0; i < 16; i++)
        CHECK(!w[L_ICON0 + i][0]);
    write_pbm("fwp_trackflash");
    led_flush();
    for (i = 0; i < 70; i++)                            /* (10 ms frames: the writes counted) */
        adv_ms(10), led_msgs(&st, &pd);
    CHECKF(st <= 16 + 8 && pd == 0, "%d step writes, %d pad writes", st, pd);
    CHECK(!rfmv_seq_tflash_on(&M));
    leds(w);
    for (i = 0; i < 16; i++)
        CHECK(!memcmp(w[L_STEP0 + i], w0[L_STEP0 + i], 3) || i == 0);
    CHECK(!memcmp(w[L_STEP0 + 9], w0[L_STEP0 + 9], 3));
    rest();
    /* a step press during it: a clip step at once, the flash ends */
    tap_track(1);
    CHECK(rfmv_seq_tflash_on(&M));
    step(5, 1);
    CHECK(!rfmv_seq_tflash_on(&M));
    step(5, 0);
    CHECK(step_has(clip, 5));
    rest();
    /* from the FM-1 layout into a note layout: shown; into the FM-1 layout, or in session view: not */
    tap_track(0), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && !rfmv_seq_tflash_on(&M));
    tap_track(1);
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && rfmv_seq_tflash_on(&M));
    rest();
    press(MR_CC_MENU), rest();
    tap_track(1);
    CHECK(M.seq.view && !rfmv_seq_tflash_on(&M));
    rest(), press(MR_CC_MENU), rest();
    /* a double click: the browser, no flash over it */
    dbl_track(1);
    CHECK(M.seq.projects && !rfmv_seq_tflash_on(&M));
    back(), rest();
}

/* ---- (0.13.0, the owner's, 2026-11-22, on 0.12.0 on the Move) "Deleting a clip from session leaves a X box hanging
 * out": the X (screen and pad) shows only until Delete is let go; the buffer stays (Copy held + an empty slot pastes
 * it). "While a clip is playing if you double click on it it should stop": now, not at the bar; a double click on a
 * clip that was not playing still plays it now, with Launch on Instant too */
static void test_x_gone_dbl_stop(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int k;
    session_view(), owners_grid();
    button(MR_CC_DELETE, 1), hit(PAD(0, 0));
    screen(), leds(w);
    CHECK(!clip_has(0, 0) && rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)) == 2 && lit(w[L_PAD0 + PAD(0, 0)]) && !cell_dot(0, 0));
    button(MR_CC_DELETE, 0);
    screen(), leds(w);
    CHECK(rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)) == 0 && cell_dot(0, 0) && !lit(w[L_PAD0 + PAD(0, 0)]));   /* an empty slot */
    write_pbm("x_gone");
    button(MR_CC_DELETE, 1);                            /* Delete held again: no X comes back */
    screen();
    CHECK(cell_dot(0, 0));
    button(MR_CC_DELETE, 0);
    button(MR_CC_COPY, 1), hit(PAD(4, 0)), button(MR_CC_COPY, 0);   /* the buffer is still there: pasted */
    CHECK(clip_has(4, 0) && clip_note(4, 0, 60));
    rest();
    /* scenes: the row's X the same */
    button(MR_CC_DELETE, 1), hit(PAD(7, 1));
    CHECK(rfmv_seq_src_clip(&M, SEQ_CLIP(0, 1)) == 2);
    button(MR_CC_DELETE, 0);
    CHECK(rfmv_seq_src_clip(&M, SEQ_CLIP(0, 1)) == 0);
    button(MR_CC_COPY, 1);
    CHECK(rfmv_seq_src_clip(&M, SEQ_CLIP(0, 1)) == 0);   /* (nor while Copy is held later) */
    button(MR_CC_COPY, 0), rest();

    /* the double click */
    for (k = 0; k < 2; k++) {                           /* Launch: 1 Bar, then Instant */
        session_view(), owners_grid();
        M.seq.launch_q = (uint8_t)(k ? RFM_MOVE_LQ_INSTANT : RFM_MOVE_LQ_BAR);
        hit(PAD(1, 0)), rest();
        press(MR_CC_PLAY), bar_start(), adv_ms(600);
        CHECK(M.seq.playing && M.seq.tr[1].active == 0);
        now_ms += 1000;
        hit(PAD(1, 0));                                 /* the playing clip, a click: it plays on */
        CHECK(M.seq.tr[1].active == 0 && M.seq.tr[1].queued == SEQ_NONE);
        now_ms += 50;
        hit(PAD(1, 0));                                 /* .. again within the double click's time: it stops now */
        CHECK(M.seq.tr[1].active == -1 && M.seq.tr[1].queued == SEQ_NONE && M.seq.playing);
        screen();
        if (!k)
            write_pbm("dbl_stopped");
        now_ms += 1000;
        hit(PAD(1, 1)), now_ms += 50, hit(PAD(1, 1));   /* a clip that was not playing: a double click plays it now */
        CHECK(M.seq.tr[1].active == 1 && M.seq.tr[1].queued == SEQ_NONE);
        now_ms += 1000;
        hit(PAD(1, 0)), now_ms += 50, hit(PAD(1, 0));   /* .. and the other one of that track: it takes over, not a stop */
        CHECK(M.seq.tr[1].active == 0 && M.seq.tr[1].queued == SEQ_NONE);
        press(MR_CC_PLAY), rest();
    }
}

/* ---- (0.13.0, the owner's, 2026-11-22) the loop's length in steps: "While in loop and changing the step length of
 * the bars, we need to show the steps so you can see which steps are not in the loop when reducing smaller than 16
 * steps": the jog's popup and the step lights show the loop's last bar's steps; "use the pads to quickly set step
 * length": the two lower pad rows, 1..16 steps of the last bar; "adding a step outsite that should not make the loop
 * length of the page become a bar": a step entered after the loop's end in its own bar leaves the length alone */
static void test_loop_steps(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int clip, i, n;
    fresh();
    hold_track(1), hit(26), track(1, 0), rest();
    tap_track(1), rest();
    clip = SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel);
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 24, 0), rfmv_seq_note_put(&M, clip, 8, 62, 100, 24, 0);
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && !M.seq.view && M.seq.len[clip] == 16);
    button(MR_CC_LOOP, 1);
    CHECK(!rfmv_seq_loop_steps(&M));
    jog(-11);                                           /* 16 -> 5 steps */
    CHECK(M.seq.len[clip] == 5 && rfmv_seq_loop_steps(&M));
    screen(), leds(w);
    write_pbm("loop_steps");
    CHECK(white(w[L_STEP0 + 0]) && lit(w[L_STEP0 + 4]) && !white(w[L_STEP0 + 4]) && !lit(w[L_STEP0 + 5]) && !lit(w[L_STEP0 + 15]));
    CHECK(lit(w[L_STEP0 + 8]) && bright(w[L_STEP0 + 8]) < bright(w[L_STEP0 + 4]));   /* (a note after the end: faint) */
    for (n = 0, i = 0; i < 16; i++)                     /* the screen's row: 5 cells (the first filled: a note), then dots */
        n += px(i * 8, 56);
    CHECK(n == 5 && px(8 * 5 + 3, 57) && px(3, 57) && !px(8 + 3, 57));
    adv_ms(RFM_MOVE_POPUP_MS + 50);                     /* the popup's second over: the steps are the pages again */
    leds(w);
    CHECK(!rfmv_seq_loop_steps(&M) && white(w[L_STEP0 + 0]) && !lit(w[L_STEP0 + 4]));
    /* the pads: the last bar's length */
    leds(w);
    CHECK(white(w[L_PAD0 + 8 + 4]) && lit(w[L_PAD0 + 8]) && !white(w[L_PAD0 + 8]) && lit(w[L_PAD0 + 0]) &&
          bright(w[L_PAD0 + 0]) < bright(w[L_PAD0 + 8]) && !lit(w[L_PAD0 + 16]) && !lit(w[L_PAD0 + 31]));
    clear();
    hit(8 + 7);                                         /* the upper of the two rows, its 8th pad: 8 steps */
    CHECK(M.seq.len[clip] == 8 && rfmv_seq_loop_steps(&M) && !count(RFM_MOVE_ACT_MIDI));
    hit(20);                                            /* an upper row: dead */
    CHECK(M.seq.len[clip] == 8 && !count(RFM_MOVE_ACT_MIDI));
    hit(7);                                             /* the bottom row's last pad: 16, the whole bar: the page strip */
    CHECK(M.seq.len[clip] == 16 && !rfmv_seq_loop_steps(&M));
    press(MR_CC_UNDO);
    CHECK(M.seq.len[clip] == 8);
    rfmv_seq_len_set(&M, clip, 32);                     /* two bars: the pads are bar 2's steps */
    rest();
    hit(8 + 7);
    CHECK(M.seq.len[clip] == 24);
    screen(), write_pbm("loop_steps_2bars");
    hit(8 + 0);
    CHECK(M.seq.len[clip] == 17);
    button(MR_CC_LOOP, 0), rest();
    CHECK(!(M.seq.mods & SEQ_M_LOOP));                  /* (used: no latch) */
    /* a step entered after the loop's end, in its own bar: the loop keeps its length */
    rfmv_seq_len_set(&M, clip, 5);
    hit(3), rest();                                     /* (a pad played: the notes a step gets) */
    step(10, 1), step(10, 0);
    CHECK(rfmv_seq_range(&M, clip, 10, &i) > i && M.seq.len[clip] == 5);
}

/* ---- (0.13.0, the owner's 2026-11-22) the jog in the Shift menus: its turn selects a parameter (the menu's knobs', in
 * knob order; nothing changes), its click edits it (the turn changes it as its knob does), a click again selects;
 * Back closes from either state; a knob turned works as before and takes the cursor */
static void mj_vals(char *o, int cap)                   /* every Shift menu's parameter, as text */
{
    const rfm_move_seq_t *q = &M.seq;
    const rfm_move_track_t *t = &q->tr[q->sel_track];
    snprintf(o, (size_t)cap, "ch%d rt%d qs%d bpm%d gr%d tg%d own%d ly%d rt%d sc%d vm%d vl%d rm%d rs%d", t->ch, t->rate,
             q->qstrength, rfm_move_seq_bpm10(&M), q->groove, t->groove, t->own, M.s.layout, M.s.root, M.s.scale,
             M.s.vel_mode, M.s.vel_level, q->rec_mode, q->rec_style);
}
static int mj_first(int bits) { int k = 0; while (k < 8 && !(bits >> k & 1)) k++; return k; }
static int mj_next(int bits, int k) { int n = k + 1; while (n < 8 && !(bits >> n & 1)) n++; return n < 8 ? n : k; }
static int mj_col(int x, int y0, int y1)                /* column x's lit pixels, rows y0..y1 */
{
    int y, n = 0;
    for (y = y0; y <= y1; y++)
        n += px(x, y);
    return n;
}
static int mj_head_arrows(void)                         /* the header's editing arrows: the header's leftmost and
                                                         * rightmost lit columns are arrow tips (row 7 alone) */
{
    int l = 0, r = 127;
    screen();
    while (l < 128 && !mj_col(l, 0, 16))
        l++;
    while (r >= 0 && !mj_col(r, 0, 16))
        r--;
    return l < r && mj_col(l, 0, 16) == 1 && px(l, 7) && mj_col(r, 0, 16) == 1 && px(r, 7);
}
/* (0.14.0, item 4) the jog's selection as Global settings' row: Workflow's in its row (19 + 15 n, 15 high), the
 * other menus' in the top line (0, 17 high). Selecting: the row inverted (lit at both ends); editing: framed (both
 * ends lit at the middle, clear just inside) with the words inverted in the middle */
static void mj_row(int sm, int n, int *y0, int *h) { *y0 = sm == RFM_MOVE_SM_WORKFLOW ? 19 + n * 15 : 0, *h = sm == RFM_MOVE_SM_WORKFLOW ? 15 : 17; }
static int mj_sel_row(int sm, int n)
{
    int y, h;
    mj_row(sm, n, &y, &h), screen();
    return px(1, y + 1) && px(126, y + 1) && px(1, y + h - 2) && px(126, y + h - 2);
}
static int mj_edit_row(int sm, int n)
{
    int y, h;
    mj_row(sm, n, &y, &h), screen();
    return px(0, y + h / 2) && px(127, y + h / 2) && !px(1, y + h / 2) && !px(126, y + h / 2) && px(64, y + 1);
}
static void mj_open(int sm)
{
    if (sm == RFM_MOVE_SM_REC)
        button(MR_CC_SHIFT, 1), press(MR_CC_REC), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    else
        shift_step(sm == RFM_MOVE_SM_WORKFLOW ? 2 : sm == RFM_MOVE_SM_TEMPO ? 4 : sm == RFM_MOVE_SM_GROOVE ? 6 :
                   sm == RFM_MOVE_SM_LAYOUT ? 7 : sm == RFM_MOVE_SM_KEYS ? 8 : 9);
}
static void test_mj_menus(void)
{
    static const struct { int sm, bits; const char *name; } MN[] = {
        { RFM_MOVE_SM_WORKFLOW, 0x07, "workflow" }, { RFM_MOVE_SM_TEMPO, 0x03, "tempo" },
        { RFM_MOVE_SM_GROOVE, 0x03, "groove" },     { RFM_MOVE_SM_LAYOUT, 0x01, "layout" },
        { RFM_MOVE_SM_KEYS, 0x0F, "keys" },         { RFM_MOVE_SM_VEL, 0x03, "velocity" },
        { RFM_MOVE_SM_REC, 0x03, "record" },
    };
    static rfm_move_t MC;
    char v0[160], v1[160], v2[160], nm[48];
    int i;
    track_view();
    for (i = 0; i < (int)(sizeof MN / sizeof MN[0]); i++) {
        const int f = mj_first(MN[i].bits), n = mj_next(MN[i].bits, f), last = 7 - __builtin_clz(MN[i].bits) + 24;
        const int wf = MN[i].sm == RFM_MOVE_SM_WORKFLOW;   /* (0.14.0, the owner's 2026-11-24: no ticks there) */
        mj_open(MN[i].sm), rest(), clear();
        CHECKF(M.seq.smenu == MN[i].sm && rfmv_seq_menu_sel(&M) == f && !rfmv_seq_menu_editing(&M), "%s: opened: the first",
               MN[i].name);
        screen();
        CHECKF((wf ? !lit_rows(17, 19) : tick(f, 1)) && !mj_head_arrows() && !mj_sel_row(MN[i].sm, f) && !mj_edit_row(MN[i].sm, f),
               "%s: the first's tick long, no selection drawn yet", MN[i].name);
        scr_save(), mj_vals(v0, sizeof v0);
        /* the turn selects: nothing changes; the header names it at once, its tick long */
        jog(1);
        mj_vals(v1, sizeof v1);
        CHECKF(!strcmp(v0, v1) && rfmv_seq_menu_sel(&M) == n, "%s: the turn selects (%d), changes nothing", MN[i].name, n);
        screen();
        CHECKF((wf ? !lit_rows(17, 19) : tick(n, 1) && (n == f || !tick(f, 1))) && !scr_same(), "%s: the selection shown",
               MN[i].name);
        CHECKF(mj_sel_row(MN[i].sm, n) && (MN[i].sm != RFM_MOVE_SM_WORKFLOW || !mj_sel_row(MN[i].sm, 0)),
               "%s: the selection inverted in its row (0.14.0)", MN[i].name);
        if (MN[i].sm == RFM_MOVE_SM_WORKFLOW)
            CHECK(!px(1, 1) && !px(126, 1));          /* (the top line stays "Workflow", not inverted) */
        CHECKF(said[0] != 0, "%s: the selection said", MN[i].name);
        snprintf(nm, sizeof nm, "mj_%s_sel", MN[i].name);
        write_pbm(nm);
        jog(5);                                         /* (stops at the end) */
        mj_vals(v1, sizeof v1);
        CHECKF(!strcmp(v0, v1) && rfmv_seq_menu_sel(&M) == last, "%s: the end", MN[i].name);
        jog(-9);
        CHECKF(rfmv_seq_menu_sel(&M) == f, "%s: the start", MN[i].name);
        jog(1);
        /* the click edits: the header stays with arrows; the turn changes only that one, as its knob */
        clear(), jog_click();
        CHECKF(rfmv_seq_menu_editing(&M) && strstr(said, "Editing "), "%s: editing, said %s", MN[i].name, said);
        rest();
        CHECKF(mj_edit_row(MN[i].sm, n) && (wf || tick(n, 1)), "%s: editing shown (after the second too)", MN[i].name);
        snprintf(nm, sizeof nm, "mj_%s_edit", MN[i].name);
        write_pbm(nm);
        MC = M;
        jog(1);
        mj_vals(v1, sizeof v1);
        {
            const int list = (MN[i].sm == RFM_MOVE_SM_WORKFLOW && n <= 1) || (MN[i].sm == RFM_MOVE_SM_GROOVE && n == 1) ||
                             MN[i].sm == RFM_MOVE_SM_LAYOUT || (MN[i].sm == RFM_MOVE_SM_KEYS && n <= 3) ||
                             (MN[i].sm == RFM_MOVE_SM_VEL && n == 1) || MN[i].sm == RFM_MOVE_SM_REC;
            rfm_move_t *keep = malloc(sizeof *keep);
            *keep = M, M = MC;
            knob(n, list ? RFM_MOVE_LIST_TICKS : 1);    /* (the knob's step, on the copy from before the turn) */
            mj_vals(v2, sizeof v2);
            M = *keep, free(keep);
        }
        CHECKF(strcmp(v0, v1) && !strcmp(v1, v2), "%s: the edit turn = knob %d's step (%s / %s)", MN[i].name, n + 1, v1, v2);
        /* the click again: selecting; the turn changes nothing */
        clear(), jog_click();
        CHECKF(!rfmv_seq_menu_editing(&M) && strstr(said, "Done"), "%s: selecting again", MN[i].name);
        jog(-1);
        mj_vals(v2, sizeof v2);
        CHECKF(!strcmp(v1, v2) && rfmv_seq_menu_sel(&M) == f, "%s: selects again", MN[i].name);
        rest();
        CHECKF(!mj_edit_row(MN[i].sm, f) && mj_sel_row(MN[i].sm, f), "%s: selecting: inverted, not framed (0.14.0: kept after the second)", MN[i].name);
        /* a knob meanwhile: works, takes the cursor (editing ended) */
        jog_click();
        knob(last, MN[i].sm == RFM_MOVE_SM_TEMPO || MN[i].sm == RFM_MOVE_SM_WORKFLOW ? -1 : -RFM_MOVE_LIST_TICKS);
        mj_vals(v2, sizeof v2);
        CHECKF(rfmv_seq_menu_sel(&M) == last && (last == f || !rfmv_seq_menu_editing(&M)), "%s: a knob takes the cursor",
               MN[i].name);
        /* Back from editing: closed at once; opened again: the first, selecting */
        jog_click();
        CHECKF(rfmv_seq_menu_editing(&M) || last == f, "%s: editing before Back", MN[i].name);
        back();
        CHECKF(!M.seq.smenu, "%s: Back closes from editing", MN[i].name);
        mj_open(MN[i].sm), rest();
        CHECKF(M.seq.smenu == MN[i].sm && rfmv_seq_menu_sel(&M) == f && !rfmv_seq_menu_editing(&M), "%s: again: the first",
               MN[i].name);
        back(), rest();
    }
}
/* the same in session view, over the Mixer, over the FM-1 layout and from a held Track 4; the arp's jog and Global
 * settings unchanged; a held step over a menu keeps the jog */
static void mj_workflow(const char *where)
{
    const rfm_move_track_t *t = &M.seq.tr[M.seq.sel_track];
    const int ch = t->ch, rate = t->rate;
    CHECKF(M.seq.smenu == RFM_MOVE_SM_WORKFLOW, "%s: open", where);
    jog(1);
    CHECKF(t->ch == ch && t->rate == rate && rfmv_seq_menu_sel(&M) == 1, "%s: selects", where);
    jog_click(), jog(1);
    CHECKF(t->ch == ch && t->rate == rate + 1 && rfmv_seq_menu_editing(&M), "%s: edits", where);
    {
        char nm[48];
        snprintf(nm, sizeof nm, "mj_%s_edit", where);
        write_pbm(nm);
    }
    jog_click(), jog(-1);
    CHECKF(t->rate == rate + 1 && rfmv_seq_menu_sel(&M) == 0, "%s: selects again", where);
    jog_click(), back(), rest();
    CHECKF(!M.seq.smenu, "%s: Back", where);
}
static void test_mj_places(void)
{
    rfm_move_track_t *t;
    int clip, lo = 0, mode, pg, row;
    session_view(), rest();
    CHECK(M.seq.view);
    shift_step(2), rest(), mj_workflow("session");
    tap_track(3), rest();                               /* the Mixer */
    CHECK(M.seq.projects);
    shift_step(2), rest(), mj_workflow("mixer");
    track_view(), tap_track(0), rest();                 /* the FM-1 layout (slot 1): Track 4 + step 3 */
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1);
    track(3, 1), step(2, 1), step(2, 0), track(3, 0), rest(), clear();
    mj_workflow("fm1");
    CHECK(!count(RFM_MOVE_ACT_TURN) && M.jog_enc == 0);  /* (not the FM-1's SELECT, its click not the jog's mode) */
    track(3, 1), adv_ms(50), step(2, 1), step(2, 0);    /* Track 4 held: the menu it opened has the jog and its click */
    CHECK(M.seq.smenu == RFM_MOVE_SM_WORKFLOW);
    jog(1), jog_click();
    CHECK(rfmv_seq_menu_sel(&M) == 1 && rfmv_seq_menu_editing(&M));
    track(3, 0), back(), rest();
    /* the arp: the jog its pages, its click the arp on / off (unchanged) */
    track_view();
    shift_step10(0), rest();
    CHECK(M.seq.smenu == RFM_MOVE_SM_ARP);
    mode = M.seq.tr[M.seq.sel_track].arp.mode, pg = M.seq.arp_page;
    jog(1);
    CHECK(M.seq.arp_page == pg + 1);
    jog(-1), jog_click();
    CHECK(M.seq.tr[M.seq.sel_track].arp.mode != mode && !M.seq.sm_edit);
    jog_click(), back(), rest();
    /* Global settings: the list (unchanged) */
    shift_step(1);
    row = M.gl_row;
    jog(1), jog_click();
    CHECK(M.gl_row == row + 1 && M.gl_edit && !M.seq.sm_edit);
    jog_click(), back(), rest();
    CHECK(!M.seq.smenu);
    /* a held step over a menu: the jog the notes' length, its click nothing of the menu's */
    t = &M.seq.tr[M.seq.sel_track];
    clip = SEQ_CLIP(M.seq.sel_track, t->sel);
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 24, 0);
    shift_step(2), rest();
    step(0, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    jog(2), jog_click();
    CHECK(rfmv_seq_range(&M, clip, 0, &lo) > lo && M.seq.notes[lo].len == 28 && !M.seq.sm_edit && rfmv_seq_menu_sel(&M) == 0);
    step(0, 0), back(), rest();
}

/* (0.13.0, the owner's) the Loop layer: a page's step held + the jog: every note on that page longer / shorter (a held
 * step's units), one undo; the loop untouched; an empty page: the popup, nothing changes; two held: nothing */
static int pl_len(int clip, int st, int note) { int i = rfmv_seq_find(&M, clip, st, note); return i >= 0 ? M.seq.notes[i].len : -1; }
static void test_mj_page_len(void)
{
    static const int ST[5] = { 0, 16, 20, 30, 40 }, NT[5] = { 60, 62, 64, 65, 67 };
    int clip, i, lo, hi, ok;
    fresh();
    hold_track(1), hit(26), track(1, 0), rest();
    tap_track(1), rest();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    clip = SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel);
    rfmv_seq_len_set(&M, clip, 64);
    for (i = 0; i < 5; i++)
        rfmv_seq_note_put(&M, clip, ST[i], NT[i], 100, 24, 0);
    rfmv_seq_note_put(&M, clip, 20, 67, 90, 30, 0);     /* (a chord: both) */
    lo = rfmv_seq_loop_lo(&M, clip), hi = rfmv_seq_loop_hi(&M, clip);
    rest(), clear();
    button(MR_CC_LOOP, 1);
    step(1, 1);                                         /* page 2 held: shown at once */
    CHECK(M.seq.page == 1);
    jog(3);
    for (i = 0, ok = 1; i < 5; i++)
        ok &= pl_len(clip, ST[i], NT[i]) == (ST[i] >= 16 && ST[i] < 32 ? 24 + 3 * 2 : 24);
    CHECK(ok && pl_len(clip, 20, 67) == 36);
    CHECK(M.seq.len[clip] == 64 && rfmv_seq_loop_lo(&M, clip) == lo && rfmv_seq_loop_hi(&M, clip) == hi);
    CHECK(M.pop == POP_LOOP && M.pop_val == 2 && M.seq.loop_dlen == 6);
    write_pbm("mj_page_len");
    adv_ms(20);
    CHECKF(strstr(said, "Bar 2 length: plus 1/4 step") != NULL, "said %s", said);
    button(MR_CC_SHIFT, 1), jog(1), button(MR_CC_SHIFT, 0);   /* Shift: whole steps */
    CHECK(pl_len(clip, 16, 62) == 48 && pl_len(clip, 20, 67) == 48 && pl_len(clip, 0, 60) == 24);
    write_pbm("mj_page_len_shift");
    for (i = 0; i < 10; i++)                            /* (clamped: 1 tick at least) */
        jog(-10);
    CHECK(pl_len(clip, 16, 62) == 1 && pl_len(clip, 40, 67) == 24);
    step(1, 0);
    CHECK(M.seq.len[clip] == 64 && rfmv_seq_loop_hi(&M, clip) == hi);
    step(1, 1), step(1, 0);                             /* (a tap right after: not a double tap: the loop stays) */
    CHECK(rfmv_seq_loop_lo(&M, clip) == lo && rfmv_seq_loop_hi(&M, clip) == hi);
    button(MR_CC_LOOP, 0), rest();
    CHECK(!(M.seq.mods & SEQ_M_LOOP));                  /* (used: no latch) */
    press(MR_CC_UNDO);                                  /* one undo: the whole hold */
    for (i = 0, ok = 1; i < 5; i++)
        ok &= pl_len(clip, ST[i], NT[i]) == 24;
    CHECK(ok && pl_len(clip, 20, 67) == 30);
    /* an empty page: the popup ("--"), nothing changes */
    rest(), clear();
    button(MR_CC_LOOP, 1), step(3, 1), jog(2);
    for (i = 0, ok = 1; i < 5; i++)
        ok &= pl_len(clip, ST[i], NT[i]) == 24;
    CHECK(ok && M.pop == POP_LOOP && M.pop_val == 3 && M.seq.len[clip] == 64);
    write_pbm("mj_page_len_empty");
    step(3, 0);
    /* two pages held: the range (as before); the jog nothing */
    step(0, 1), step(2, 1);
    CHECK(rfmv_seq_loop_lo(&M, clip) == 0 && rfmv_seq_loop_hi(&M, clip) == 48);
    hi = M.seq.len[clip];
    jog(2);
    CHECK(pl_len(clip, 0, 60) == 24 && pl_len(clip, 40, 67) == 24 && rfmv_seq_loop_hi(&M, clip) == 48 && M.seq.len[clip] == hi);
    step(2, 0), step(0, 0);
    /* none held: the jog the loop's length (as before) */
    jog(-1);
    CHECK(rfmv_seq_loop_hi(&M, clip) == 47 && M.pop_val == 1);
    button(MR_CC_LOOP, 0), rest();
}

/* (0.13.0, the owner's) the Loop layer's double tap: always that page alone (in the loop, the clip's outside it, past
 * the clip's end, a ghost bar reached with Right); a tap only shows a page (a page past the end too: a later tap there
 * confirms nothing); a ghost bar reached with Right: its key confirms it (as before); held + a second: the range */
static int lt_clip;
static void lt_reset(void)                              /* the loop: pages 2..3; a note on page 5 (the clip to there) */
{
    button(MR_CC_LOOP, 0), rest();
    rfmv_seq_loop_set(&M, lt_clip, 16, 32);
    M.seq.page = 1;
    rest(), now_ms += 500;
}
static int lt_is(int lo, int hi) { return rfmv_seq_loop_lo(&M, lt_clip) == lo && rfmv_seq_loop_hi(&M, lt_clip) == hi; }
static void lt_dbl(int s) { step(s, 1), step(s, 0), now_ms += 50, step(s, 1), step(s, 0); }
static void test_loop_dbl(void)
{
    static const int PG[3] = { 1, 0, 4 };              /* in the loop; the clip's before it; the clip's after it */
    int i;
    fresh();
    hold_track(1), hit(26), track(1, 0), rest();
    tap_track(1), rest();
    lt_clip = SEQ_CLIP(M.seq.sel_track, M.seq.tr[M.seq.sel_track].sel);
    rfmv_seq_note_put(&M, lt_clip, 70, 60, 100, 24, 0);
    for (i = 0; i < 3; i++) {
        lt_reset();
        button(MR_CC_LOOP, 1), lt_dbl(PG[i]);
        CHECKF(lt_is(PG[i] * 16, PG[i] * 16 + 16) && M.seq.page == PG[i], "double tap on page %d: alone", PG[i] + 1);
        button(MR_CC_LOOP, 0), rest(), press(MR_CC_UNDO);
        CHECKF(lt_is(16, 48), "page %d: one undo", PG[i] + 1);
    }
    lt_reset();                                         /* past the clip's end (no Right) */
    button(MR_CC_LOOP, 1), lt_dbl(7);
    CHECK(lt_is(112, 128) && M.seq.page == 7);
    write_pbm("mj_loop_dbl");
    button(MR_CC_LOOP, 0), rest(), press(MR_CC_UNDO);
    CHECK(lt_is(16, 48));
    lt_reset();                                         /* a ghost bar reached with Right: the first tap confirms, the
                                                         * second makes it alone; one Undo: as before both */
    M.seq.page = 4, press(MR_CC_RIGHT), press(MR_CC_RIGHT), rest();
    CHECK(M.seq.page == 6);
    button(MR_CC_LOOP, 1), lt_dbl(6);
    CHECK(lt_is(96, 112) && M.seq.page == 6);
    button(MR_CC_LOOP, 0), rest(), press(MR_CC_UNDO);
    CHECK(lt_is(16, 48));
    /* a single tap on a page past the end, then again after the double tap's time: only shown, the loop the same */
    lt_reset();
    button(MR_CC_LOOP, 1), step(9, 1), step(9, 0);
    CHECK(lt_is(16, 48) && M.seq.page == 9);
    now_ms += 500, adv_ms(10);
    step(9, 1), step(9, 0);
    CHECK(lt_is(16, 48) && M.seq.page == 9);
    now_ms += 500, adv_ms(10);
    step(8, 1), step(8, 0);                             /* (a ghost before the shown one: shown, not confirmed) */
    CHECK(lt_is(16, 48) && M.seq.page == 8);
    /* a ghost bar reached with Right, tapped once: confirmed (as before) */
    lt_reset();
    M.seq.page = 4, press(MR_CC_RIGHT), press(MR_CC_RIGHT), rest();
    button(MR_CC_LOOP, 1), step(6, 1), step(6, 0);
    CHECK(lt_is(16, 112) && M.seq.page == 6);
    /* held + a second: the range (as before) */
    lt_reset();
    button(MR_CC_LOOP, 1), step(2, 1), step(5, 1), step(5, 0), step(2, 0);
    CHECK(lt_is(32, 96));
    button(MR_CC_LOOP, 0), rest();
}

/* ---- (0.13.0) an empty project loaded (the host at start: rfm_move_project_load with no blob, after
 * rfm_move_project_dir) keeps a new project's routing, channels and Track tracks together. Found by the host's
 * latch_e2e: the clear put the channels back to 1..16 while the Track buttons stayed on tracks 1, 5, 9, so slot 2's
 * pads played channel 5 */
static void test_empty_load_defaults(void)
{
    ps_fresh(1);
    rfm_move_project_dir(&M, 0);
    CHECK(M.seq.fx.ffilt[1] == 0x00F0 && M.seq.tr[4].ch == 0 && M.s.lock[1] == 4);
    rfm_move_project_load(&M, 0, NULL, 0);
    CHECK(M.seq.fx.ffilt[0] == 0x000F && M.seq.fx.ffilt[1] == 0x00F0 && M.seq.fx.ffilt[2] == 0x0F00);
    CHECK(M.seq.tr[4].ch == 0 && M.seq.tr[6].ch == 2 && M.seq.tr[15].ch == 3 && M.s.lock[1] == 4 && M.s.lock[2] == 8);
    CHECK(!M.seq.dirty);                                /* (nothing to save: an empty project is not stored by this) */
    ps_fresh(0);                                        /* an older host: as before */
    rfm_move_project_dir(&M, 0);
    rfm_move_project_load(&M, 0, NULL, 0);
    CHECK(M.seq.fx.ffilt[1] == 0xFFFF && M.seq.tr[6].ch == 6 && M.s.lock[1] == 1);
}

/* ================================================================ 0.14.0: the owner's run of 2026-11-23 === */
/* note ons of the arp for track tr (1..16), as MIDI actions (the host routes a track's notes to the slots it is
 * routed to) */
static int arp_ons(int tr)
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += A[i].kind == RFM_MOVE_ACT_MIDI && A[i].track == tr && (A[i].bytes[0] & 0xF0) == 0x90 && A[i].bytes[2];
    return n;
}
/* item 6: "Turning the sequencer's arp on is not functioning, No notes getting sent to the firmware". Every way the
 * UI turns it on, the pads held, stopped and running: the arp's notes come out on the pads' track, which is routed to
 * the slot the pads play */
static void test_o14_arp_notes(void)
{
    rfm_move_arp_t *ap;
    int tr, slot;
    track_view();
    tr = M.seq.sel_track, slot = M.fw.active;
    ap = &M.seq.tr[tr].arp;
    CHECK(ap->mode == RFM_MOVE_AM_OFF && (M.seq.fx.ffilt[slot] >> tr & 1));
    /* off: a pad plays live (track 0), no arp */
    clear(), pad(0, 100), adv_ms(600), pad(0, 0), adv_ms(50);
    CHECK(arp_ons(tr + 1) == 0 && count(RFM_MOVE_ACT_MIDI) >= 2);
    /* 1. Shift + step 11's click opens the menu, the jog's click turns it on; the menu open, a pad held */
    button(MR_CC_SHIFT, 1), step(10, 1), step(10, 0), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    CHECK(M.seq.smenu == RFM_MOVE_SM_ARP);
    jog_click();
    CHECK(ap->mode == RFM_MOVE_AM_ON);
    clear(), pad(0, 100), adv_ms(1000);
    CHECKF(arp_ons(tr + 1) >= 3, "menu open, stopped: %d arp notes", arp_ons(tr + 1));
    pad(0, 0), adv_ms(600), clear(), adv_ms(600);
    CHECK(arp_ons(tr + 1) == 0);                        /* (let go: it stops) */
    /* .. the menu closed (Back), two pads held */
    back(), rest();
    CHECK(!M.seq.smenu && ap->mode == RFM_MOVE_AM_ON);
    clear(), pad(0, 100), pad(2, 100), adv_ms(1000);
    CHECKF(arp_ons(tr + 1) >= 3, "menu closed, stopped: %d", arp_ons(tr + 1));
    pad(0, 0), pad(2, 0), adv_ms(300);
    /* .. running */
    press(MR_CC_PLAY), adv_ms(200);
    CHECK(M.seq.playing);
    clear(), pad(1, 100), adv_ms(1200);
    CHECKF(arp_ons(tr + 1) >= 3, "running: %d", arp_ons(tr + 1));
    pad(1, 0), adv_ms(300);
    press(MR_CC_PLAY), adv_ms(200);
    CHECK(!M.seq.playing);
    /* 2. Shift + step 11 held: off, held again: on (the menu not opened) */
    button(MR_CC_SHIFT, 1), step(10, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20), step(10, 0);
    CHECK(ap->mode == RFM_MOVE_AM_OFF);
    step(10, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20), step(10, 0), button(MR_CC_SHIFT, 0), rest();
    CHECK(ap->mode == RFM_MOVE_AM_ON && !M.seq.smenu);
    clear(), pad(3, 100), adv_ms(1000);
    CHECKF(arp_ons(tr + 1) >= 3, "toggled by the hold, stopped: %d", arp_ons(tr + 1));
    pad(3, 0), adv_ms(300);
    press(MR_CC_PLAY), adv_ms(200), clear(), pad(3, 100), adv_ms(1200);
    CHECKF(arp_ons(tr + 1) >= 3, "toggled by the hold, running: %d", arp_ons(tr + 1));
    pad(3, 0), press(MR_CC_PLAY), adv_ms(300);
    /* 3. Track 4 held + step 11 (the Shift layer) held: off and on the same */
    track(3, 1), adv_ms(RFM_MOVE_TAP_MS + 20), step(10, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20), step(10, 0), track(3, 0), rest();
    CHECKF(ap->mode == RFM_MOVE_AM_OFF, "mode %d", ap->mode);
    track(3, 1), adv_ms(RFM_MOVE_TAP_MS + 20), step(10, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20), step(10, 0), track(3, 0), rest();
    CHECK(ap->mode == RFM_MOVE_AM_ON && M.s.mode == RFM_MOVE_MODE_SEQ && !M.seq.view);
    clear(), pad(4, 100), adv_ms(1000);
    CHECKF(arp_ons(tr + 1) >= 3, "Track 4's layer: %d", arp_ons(tr + 1));
    pad(4, 0), adv_ms(300);
    /* 4. another firmware's Track tapped and back: the arp is the track's, it still plays */
    tap_track(0), rest(), tap_track(1), rest();
    CHECK(M.seq.sel_track == tr && M.fw.active == slot && M.seq.tr[tr].arp.mode == RFM_MOVE_AM_ON);
    clear(), pad(5, 100), adv_ms(1000);
    CHECKF(arp_ons(tr + 1) >= 3, "after a Track tap: %d", arp_ons(tr + 1));
    pad(5, 0), adv_ms(300);
    /* 5. the Hold mode (knob 5): latched after the pads are let go */
    button(MR_CC_SHIFT, 1), step(10, 1), step(10, 0), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    knob(4, RFM_MOVE_LIST_TICKS * 3);
    CHECK(ap->mode == RFM_MOVE_AM_HOLD);
    back(), rest();
    hit(0), clear(), adv_ms(1000);
    CHECKF(arp_ons(tr + 1) >= 3, "Hold, let go: %d", arp_ons(tr + 1));
    rfmv_seq_arp_stop(&M), ap->mode = RFM_MOVE_AM_OFF;
    /* the owner's project (the host's trace): CHANCE 0, GATE 2, OCT DIST -24, QUANT 0, the MOD lane's step 1 a tie:
     * it played nothing. The owner's, the same day: "The arp should be 100% and not changable as knob parameter": a
     * stored CHANCE is not looked at any more: it sounds */
    {
        rfm_move_arp_t keep = *ap;
        unsigned h_on;
        int i;
        ap->pat = 4, ap->rate = 11, ap->range = 2, ap->chance = 0, ap->gate = 2, ap->odist = -24, ap->quant = 0;
        ap->reset = 1, ap->mlen = 9, ap->tlen = 10, ap->lane[0][0] = RFM_MOVE_AL_TIE;
        button(MR_CC_SHIFT, 1), step(10, 1), step(10, 0), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
        clear(), jog_click();
        CHECK(ap->mode != RFM_MOVE_AM_OFF && !rfmv_seq_arp_silent(&M, tr));
        CHECKF(strstr(said, "Silent") == NULL, "said %s", said);
        rest(), clear(), pad(0, 100), adv_ms(1000), pad(0, 0), adv_ms(300);
        CHECKF(arp_ons(tr + 1) >= 3, "the owner's arp, CHANCE 0 stored: %d", arp_ons(tr + 1));
        jog_click(), rest();                            /* (off) */
        /* an arp that still can never sound (every MOD step in its length a rest): the core plays nothing, and the
         * arp's screen says so: "Arp" / "Silent" the whole screen at the toggle, "Arp Silent" at rest, the walk dimmed
         * as when off */
        for (i = 0; i < 16; i++)
            ap->lane[0][i] = RFM_MOVE_AL_REST;
        clear(), jog_click();
        CHECK(ap->mode != RFM_MOVE_AM_OFF && rfmv_seq_arp_silent(&M, tr) && rfmv_pop_up(&M, POP_ARPW) &&
              M.pop_i == RFM_MOVE_AM_N);
        CHECKF(strstr(said, "Silent") != NULL, "said %s", said);
        write_pbm("o14_arp_silent_pop");
        rest(), clear(), pad(0, 100), adv_ms(1000), pad(0, 0), adv_ms(300);
        CHECK(arp_ons(tr + 1) == 0);
        screen();
        h_on = area_hash(0, 0, 128, 17);
        write_pbm("o14_arp_silent");
        for (i = 1; i < 16; i++)                        /* (steps play again: it sounds; the header the mode again) */
            ap->lane[0][i] = RFM_MOVE_AL_PLAY;
        CHECK(!rfmv_seq_arp_silent(&M, tr));
        rest(), screen();
        CHECK(area_hash(0, 0, 128, 17) != h_on);
        clear(), pad(0, 100), adv_ms(1000), pad(0, 0), adv_ms(300);
        CHECK(arp_ons(tr + 1) >= 3);
        back(), rest();
        rfmv_seq_arp_stop(&M), *ap = keep;
    }
}

/* item 1: "if I have page set to a CC bank or FM-1 knobs, switching to another track and back the knobs reset to the
 * trig page, the page I am on should be remembered": each track's knob page, by any way of selecting it */
static void test_o14_kpage(void)
{
    int tr;
    track_view();
    tr = M.seq.sel_track;
    CHECK(M.seq.trig && rfmv_seq_kpage(&M) == 0);
    /* the FM-1 page; another firmware's Track and back: the FM-1 page again */
    jog(1), rest();
    CHECK(M.seq.fwp && !M.seq.trig);
    tap_track(0), rest(), tap_track(1), rest();
    CHECK(M.seq.sel_track == tr && M.seq.fwp && !M.seq.trig && rfmv_fwp_page(&M));
    write_pbm("o14_kpage_fm1_back");
    /* bank 3; another track selected (Track 2 held + a step): that one's page (never shown: TRIG); back: bank 3 */
    jog(3), rest();
    CHECK(rfmv_seq_kpage(&M) == 4 && M.s.bank == 2);
    hold_track(1), step(6, 1), step(6, 0), track(1, 0), rest();
    CHECK(M.seq.sel_track == 6 && M.seq.trig && rfmv_seq_kpage(&M) == 0);
    jog(2), rest();                                     /* (track 7: bank 1) */
    CHECK(rfmv_seq_kpage(&M) == 2 && M.s.bank == 0);
    hold_track(1), step(tr, 1), step(tr, 0), track(1, 0), rest();
    CHECK(M.seq.sel_track == tr && rfmv_seq_kpage(&M) == 4 && M.s.bank == 2);
    write_pbm("o14_kpage_bank3_back");
    /* session view: a knob's touch selects its column's track; Menu back: that track's page */
    press(MR_CC_MENU), rest();
    CHECK(M.seq.view);
    raw(0x90, 6, 127), raw(0x80, 6, 0), rest();
    CHECK(M.seq.sel_track == 6);
    press(MR_CC_MENU), rest();
    CHECK(!M.seq.view && rfmv_seq_kpage(&M) == 2 && M.s.bank == 0);
    /* Track 2's tap: the slot's own track again (it was selected for it), its page kept */
    tap_track(1), rest();
    CHECK(M.seq.sel_track == tr && rfmv_seq_kpage(&M) == 4);
    jog(-4), rest();
    CHECK(M.seq.trig);
}

/* item 3: "The current state is not obvious on pads, Should be a different contrasting color": the active state's pad
 * white (no slot's colour, not solo's purple), breathing slowly; a pressed pad white and steady; stored ones the
 * slot's colour, dim; for all three slots */
static void test_o14_state_pad(void)
{
    static const char *const nm[3] = { "o14_states_slot1", "o14_states_slot2", "o14_states_slot3" };
    uint8_t w[RFM_MOVE_NLEDS][3];
    int t, k, hi = 0, lo = 0;
    ps_states();
    rfm_move_fw_states(&M, 1, 1u | 4u | 8u), rfm_move_fw_state_cur(&M, 1, 2);
    rfm_move_fw_states(&M, 2, 1u | 2u), rfm_move_fw_state_cur(&M, 2, 0);
    CHECK(M.fw.st_cur[0] == -1 && M.fw.st_cur[1] == -1 && M.fw.st_cur[2] == -1);   /* (2026-11-24: ignored) */
    hold_track(0), hit(30), track(0, 0);
    for (t = 0; t < 3; t++) {                           /* (2026-11-24) pad 1, the project's, in every slot */
        const int other = t == 0 ? 1 : t == 1 ? 2 : 1;
        if (t)
            tap_track(t);
        adv_ms(500);                                    /* (swept in; the beat's bright part) */
        CHECK(rfm_move_states_open(&M) == t);
        leds(w);
        CHECKF(full(w[L_PAD0 + 24]), "slot %d: pad 1 %02x%02x%02x", t + 1, w[L_PAD0 + 24][0], w[L_PAD0 + 24][1],
               w[L_PAD0 + 24][2]);
        CHECKF(lit(w[L_PAD0 + 24 + other]) && !white(w[L_PAD0 + 24 + other]) &&
               bright(w[L_PAD0 + 24 + other]) * 2 < bright(w[L_PAD0 + 24]), "slot %d: stored", t + 1);
        write_pbm(nm[t]);
        for (k = 0; k < 20; k++) {                      /* (a second: bright, then a third, then bright) */
            adv_ms(50), leds(w);
            hi += full(w[L_PAD0 + 24]);
            lo += white(w[L_PAD0 + 24]) && !full(w[L_PAD0 + 24]);
        }
    }
    CHECKF(hi >= 30 && lo >= 9, "breathing: %d bright, %d dim", hi, lo);
    /* a pressed pad: white and steady, pad 1 meanwhile breathing */
    pad(24 + 3, 100);
    for (k = 0, hi = 0; k < 20; k++)
        adv_ms(50), leds(w), hi += full(w[L_PAD0 + 24 + 3]);
    CHECK(hi == 20);
    pad(24 + 3, 0), back(), rest();
}

/* (0.14.0, the owner's 2026-11-24: "a project should keep it's own unique firmware state which then shows up as slot 1,
 * so slot 1 always means project state, a unique state per project") pad 1 is the open project's own sound: named
 * "Project 3" on opening and while held, white and breathing, a click saves the project now (PROJECT_SAVE value 1,
 * its pad off, on, off); Delete + pad 1 refused (red, "kept", nothing sent); Copy + pad 1 copies the sound now, a
 * paste onto it goes into the project (value 1, Undo after it); a held library pad says "State 5" */
static void test_o14_states_project(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    const ps_act_t *a;
    int seen[3] = { 0, 0, 0 }, k;
    ps_states();
    M.seq.proj_cur = 2;                                 /* (project 3 open) */
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    rest(), ps_clear(), clear();
    hold_track(0), hit(30);                             /* opening: the one you are in, at once */
    screen();
    CHECK(lit_rows(2, 20) > 40);
    CHECKF(strstr(said, "States, ChoralRoot, Project 3") != NULL, "said %s", said);
    write_pbm("o14_states_open");
    track(0, 0), rest(), ps_clear(), clear();
    leds(w);
    CHECK(white(w[L_PAD0 + 24]) && w[L_PAD0 + 25][0] == 107 && w[L_PAD0 + 28][0] == 107 && !lit(w[L_PAD0 + 26]));
    screen();
    CHECK(px(1 + 1, 16 + 1) && !px(1 + 4, 16 + 4));     /* (pad 1's tile filled, its "P" dark in it) */
    write_pbm("o14_states");
    /* pad 1 pressed: the project saved now; held: "Project 3 / saved" stays */
    pad(ps_pad(0), 100);
    CHECK(npa == 1 && (a = ps_find(RFM_MOVE_ACT_PROJECT_SAVE, -1)) && a->index == 2 && a->value == 1);
    CHECK(!ps_count(RFM_MOVE_ACT_STATE_SAVE) && M.fw.undo == 0 && (M.seq.proj_used >> 2 & 1));
    CHECKF(strstr(said, "Project 3 saved") != NULL, "said %s", said);
    adv_ms(RFM_MOVE_POPUP_MS + 200);
    screen();
    CHECK(lit_rows(10, 24) > 30 && !px(1 + 1, 16 + 1));
    write_pbm("o14_states_held_project");
    pad(ps_pad(0), 0), adv_ms(20);
    screen();
    CHECK(px(1 + 1, 16 + 1));                           /* (let go: the grid again) */
    /* a quick click: its pad off, on, off, then breathing again */
    rest(), ps_clear();
    hit(ps_pad(0));
    for (k = 0; k < 40; k++) {
        leds(w);
        if (k * 10 < 3 * RFMV_PJ_ACK_MS - 20)
            seen[(k * 10) / RFMV_PJ_ACK_MS] |= white(w[L_PAD0 + 24]) ? 2 : lit(w[L_PAD0 + 24]) ? 4 : 1;
        adv_ms(10);
    }
    CHECKF(seen[0] == 1 && seen[1] == 2 && (seen[2] & 1), "ack %d %d %d", seen[0], seen[1], seen[2]);
    leds(w);
    CHECK(white(w[L_PAD0 + 24]) && ps_count(RFM_MOVE_ACT_PROJECT_SAVE) == 1);
    /* a library pad held: "State 5", not the project; loaded into it */
    rest(), ps_clear(), clear();
    pad(ps_pad(4), 100), adv_ms(RFM_MOVE_POPUP_MS + 200);
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_LOAD, 0)) && a->index == 4);
    screen();
    CHECK(lit_rows(10, 24) > 30);
    write_pbm("o14_states_held_library");
    pad(ps_pad(4), 0), rest();
    leds(w);
    CHECK(white(w[L_PAD0 + 24]) && w[L_PAD0 + 28][0] == 107);   /* (pad 1 stays the one you are in) */
    /* Delete + pad 1: refused, red while held, "Project 3 / kept" */
    ps_clear(), clear();
    button(MR_CC_DELETE, 1), pad(ps_pad(0), 100);
    leds(w);
    CHECK(npa == 0 && w[L_PAD0 + 24][0] > 100 && w[L_PAD0 + 24][1] < 40 && w[L_PAD0 + 24][2] < 40);
    CHECKF(strstr(said, "Project 3 kept") != NULL, "said %s", said);
    write_pbm("o14_states_delete_project");
    pad(ps_pad(0), 0), button(MR_CC_DELETE, 0), rest();
    CHECK(npa == 0 && !M.fw.sb_kind);
    /* Copy + pad 1: the sound now into the buffer; + a stored library pad: written over; again + an empty one: in it */
    ps_clear();
    button(MR_CC_COPY, 1), hit(ps_pad(0));
    CHECK(npa == 1 && (a = ps_find(RFM_MOVE_ACT_STATE_COPY, 0)) && a->index == 0 && M.fw.sb_src == 0 && M.fw.sb_armed);
    screen();
    CHECK(px(0, 15) && px(1 + 1, 16 + 1));              /* (pad 1's tile filled and dashed round) */
    write_pbm("o14_states_copy_project");
    hit(ps_pad(4));
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_PASTE, 0)) && a->index == 4 && a->value == 0 && !M.fw.sb_armed);
    hit(ps_pad(0)), hit(ps_pad(9));
    CHECK(ps_count(RFM_MOVE_ACT_STATE_COPY) == 2 && (a = ps_find(RFM_MOVE_ACT_STATE_PASTE, 0)) && a->index == 9 &&
          (M.fw.st_used[0] >> 9 & 1) && M.fw.undo == 1);   /* (the load's undo kept: the sound unchanged) */
    /* a library state copied, pasted onto pad 1: into the project (value 1), Undo after it */
    ps_clear();
    hit(ps_pad(1)), hit(ps_pad(0));
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_PASTE, 0)) && a->index == 0 && a->value == 1 && M.fw.undo == 1);
    CHECK(!ps_find(RFM_MOVE_ACT_STATE_LOAD, -1) && !(M.fw.st_used[0] & 1u << 30));
    button(MR_CC_COPY, 0);
    leds(w);
    CHECK(w[L_WHITE0 + W_UNDO][0] == 127);
    write_pbm("o14_states_paste_project");
    rest(), ps_clear(), clear();
    press(MR_CC_UNDO);
    CHECK((a = ps_find(RFM_MOVE_ACT_STATE_UNDO, 0)) && a->value == -1 && a->index == 255);
    CHECKF(strstr(said, "Undo, paste") != NULL, "said %s", said);
    /* the host's state file moved from index 0 to a library pad: its report, as any */
    rfm_move_fw_states(&M, 0, M.fw.st_used[0] & ~1u);
    rest();
    leds(w);
    CHECK(white(w[L_PAD0 + 24]));
    back(), rest();
}

/* (0.14.0) a host without RFM_MOVE_CAP_PROJFW: its States as before (the browser's Loop): pad 1 a state like any,
 * the active one its rfm_move_fw_state_cur's, no project save */
static void test_o14_states_old_host(void)
{
    ps_fresh(0);
    rfm_move_fw_states(&M, 0, 1u | 2u), rfm_move_fw_state_cur(&M, 0, 1);
    CHECK(M.fw.st_cur[0] == 1);
    dbl_track(0), rest();
    press(MR_CC_LOOP);
    CHECK(M.fw.states && !M.fw.sl);
    ps_clear();
    hit(ps_pad(0));
    CHECK(npa == 1 && ps_find(RFM_MOVE_ACT_STATE_LOAD, 0) && ps_find(RFM_MOVE_ACT_STATE_LOAD, 0)->index == 0 &&
          M.fw.st_cur[0] == 0 && !ps_count(RFM_MOVE_ACT_PROJECT_SAVE));
    back(), back(), rest();
}

/* item 5: "Using SHIFT + a track button should take you to Track settings similar to Ableton's Move software
 * shortcuts": Shift + Track 1..3, at the press, that firmware's selected track's settings (Shift + step 3's menu) */
static void test_o14_shift_track(void)
{
    int tr;
    track_view();
    tr = M.s.lock[1] % RFM_MOVE_SEQ_TRACKS;
    /* track view, the slot played: the menu at the press, named by its track */
    clear(), button(MR_CC_SHIFT, 1), track(1, 1);
    CHECK(M.seq.smenu == RFM_MOVE_SM_WORKFLOW && M.seq.sel_track == tr && M.fw.active == 1 && !M.seq.view);
    CHECKF(strstr(said, "settings") != NULL, "said %s", said);
    adv_ms(RFM_MOVE_TAP_MS + 50);                       /* (held on: no Track menu) */
    CHECK(!rfmv_lay_menu(&M) && M.seq.smenu == RFM_MOVE_SM_WORKFLOW);
    track(1, 0), button(MR_CC_SHIFT, 0), rest();
    CHECK(M.seq.smenu == RFM_MOVE_SM_WORKFLOW && !(M.seq.mods & SEQ_M_SHIFT));
    write_pbm("o14_shift_track");
    /* the knobs are the track's settings: knob 2 its rate */
    {
        const int r = M.seq.tr[tr].rate;
        knob(1, RFM_MOVE_LIST_TICKS);
        CHECK(M.seq.tr[tr].rate != r);
        knob(1, -RFM_MOVE_LIST_TICKS);
    }
    back(), rest();                                     /* (Back closes it; the view and mode stay) */
    CHECK(!M.seq.smenu && M.s.mode == RFM_MOVE_MODE_SEQ && !M.seq.view && M.fw.active == 1);
    /* session view: it stays; the firmware's selected track selected, its settings open */
    press(MR_CC_MENU), rest();
    raw(0x90, 4, 127), raw(0x80, 4, 0), rest();         /* (another track selected first) */
    CHECK(M.seq.view && M.seq.sel_track != tr);
    button(MR_CC_SHIFT, 1), track(1, 1);
    CHECK(M.seq.view && M.seq.smenu == RFM_MOVE_SM_WORKFLOW && M.seq.sel_track == tr);
    track(1, 0), button(MR_CC_SHIFT, 0), rest();
    write_pbm("o14_shift_track_session");
    back(), rest();
    CHECK(M.seq.view && !M.seq.smenu);
    press(MR_CC_MENU), rest();
    /* Track 4 with Shift: its own tap (the Mixer), as before */
    button(MR_CC_SHIFT, 1), tap_track(3), button(MR_CC_SHIFT, 0), rest();
    CHECK(M.seq.projects && M.seq.smenu != RFM_MOVE_SM_WORKFLOW);
    /* the Mixer: Shift + Track 2 leaves it for the slot's track view with the menu */
    button(MR_CC_SHIFT, 1), track(1, 1), track(1, 0), button(MR_CC_SHIFT, 0), rest();
    CHECK(!M.seq.projects && M.seq.smenu == RFM_MOVE_SM_WORKFLOW && M.seq.sel_track == tr && M.fw.active == 1);
    back(), rest();
    /* without Shift: the tap as ever (no menu) */
    tap_track(1), rest();
    CHECK(!M.seq.smenu && M.fw.active == 1);
}

/* item 2: "On the sequencer with FM-1 knobs it would be nice to be able to click the Jogwheel to make it control one
 * of the knobs like it does in the FM-1 layout. Click to exit. Shift + Click to change the target knob, should be
 * linked with the Fm-1 layout"; then, the same day: "No double click on the Jogwheel to toggle takeover. Single click
 * and shift + jogwheel to change jogwheel mode. Single click for changing jogwheel mode is synced with FM-1 layout
 * and FM-1 layout keeps the single click behaviour" */
static void sh_jog_click(void) { button(MR_CC_SHIFT, 1), jog_click(), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS; }
static void test_o14_fwp_jog(void)
{
    const char *lb[RFM_MOVE_LBL_N];
    uint8_t w[RFM_MOVE_NLEDS][3];
    const rfm_move_act_t *a;
    int i;
    track_view();
    for (i = 0; i < RFM_MOVE_LBL_N; i++)
        lb[i] = NULL;
    lb[rfmv_enc_slot(RFM_MOVE_E_K1)] = "Cutoff";
    rfm_move_set_labels(&M, lb);
    jog(1), rest();
    CHECK(rfmv_fwp_live(&M) && !M.fwj && M.jog_enc == 0);
    /* the click: the takeover at once (Knobs+Keys, no wait for a second click): the page is the FM-1 layout's, its
     * jog too: on the target (SELECT), that knob white; Play the firmware's */
    clear(), jog_click();
    CHECK(M.seq.fwp_full && rfmv_pop_up(&M, POP_FWLV) && rfmv_fwj_on(&M) && !M.fwj);
    leds(w);
    CHECK(full(w[L_RING0 + 0]) && !white(w[L_RING0 + 1]));
    write_pbm("o14_fwp_takeover");
    rest(), clear(), jog(1);
    a = find(RFM_MOVE_ACT_TURN);
    CHECK(a && a->index == RFM_MOVE_E_SELECT && a->value == 1 && rfmv_seq_kpage(&M) == 1);
    rest(), clear(), press(MR_CC_PLAY);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_PLAY) == 2 && !M.seq.playing && !M.seq.start_pending);
    /* .. Shift + click there: the next target, as the FM-1 layout's click (PRESET), and on round to SELECT */
    rest(), clear(), sh_jog_click();
    CHECK(M.jog_enc == 1 && M.seq.fwp_full && rfmv_pop_up(&M, POP_FWJ) && M.pop_i == 2);
    clear(), jog(1);
    a = find(RFM_MOVE_ACT_TURN);
    CHECK(a && a->index == RFM_MOVE_E_PRESETS);
    rest(), sh_jog_click(), sh_jog_click();
    CHECK(M.jog_enc == 0 && M.seq.fwp_full);
    /* the click again: Knobs: the jog its pages again, no knob white, Play the Move's */
    rest(), clear(), jog_click();
    CHECK(!M.seq.fwp_full && rfmv_pop_up(&M, POP_FWLV) && !rfmv_fwj_on(&M));
    rest(), leds(w);
    for (i = 0; i < 3; i++)
        CHECK(!white(w[L_RING0 + i]));
    clear(), jog(1);
    CHECK(!count(RFM_MOVE_ACT_TURN) && rfmv_seq_kpage(&M) == 2);
    jog(-1), rest();
    /* Shift + click, not taken over: the jog on a knob while the steps and buttons stay the sequencer's: SELECT first */
    clear(), sh_jog_click();
    CHECK(M.fwj && rfmv_fwj_on(&M) && rfmv_pop_up(&M, POP_FWJ) && M.pop_i == 1 && !M.seq.fwp_full && M.jog_enc == 0);
    leds(w);
    CHECK(full(w[L_RING0 + 0]) && !white(w[L_RING0 + 1]));
    CHECKF(strstr(said, "Jog: Select") != NULL, "said %s", said);
    write_pbm("o14_fwp_jog_on");
    /* its turn: SELECT, as the FM-1 layout's jog; the page stays */
    clear(), jog(2);
    a = find(RFM_MOVE_ACT_TURN);
    CHECK(a && a->index == RFM_MOVE_E_SELECT && a->value == 2 && M.seq.fwp && rfmv_seq_kpage(&M) == 1);
    rest();
    leds(w);
    CHECK(full(w[L_RING0 + 0]) && !white(w[L_RING0 + 4]) && lit(w[L_RING0 + 4]));
    screen();
    CHECK(lit_rows(1, 15) > 80);                        /* ("Jog Select" on top while it lasts) */
    write_pbm("o14_fwp_jog_rest");
    /* (the owner's 2026-11-23: "if not pushed [for takeover] ... Play should be the Move's host") with the jog on a
     * knob the buttons are still the Move's: Play starts and stops its sequencer, the firmware hears no button */
    clear(), press(MR_CC_PLAY);
    CHECK(!count(RFM_MOVE_ACT_BTN) && (M.seq.playing || M.seq.start_pending) && M.fwj);
    press(MR_CC_PLAY);
    CHECK(!count(RFM_MOVE_ACT_BTN) && !M.seq.playing && !M.seq.start_pending && M.fwj);
    rest();
    /* Shift + click: the next target at the press (PRESET, knob 2 white); its turn PRESETS */
    clear(), button(MR_CC_SHIFT, 1), jog_click();
    CHECK(M.jog_enc == 1 && M.fwj && rfmv_pop_up(&M, POP_FWJ) && M.pop_i == 2);
    button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    CHECK(!M.seq.mods && !rfmv_pop_up(&M, POP_TEMPO));
    leds(w);
    CHECK(full(w[L_RING0 + 1]) && !white(w[L_RING0 + 0]));
    write_pbm("o14_fwp_jog_preset");
    clear(), jog(-1);
    a = find(RFM_MOVE_ACT_TURN);
    CHECK(a && a->index == RFM_MOVE_E_PRESETS && a->value == -1);
    rest();
    /* linked with the FM-1 layout: there the jog turns PRESETS too (Track 2's FM-1 layout) */
    hold_track(1), hit(24), track(1, 0), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && M.jog_enc == 1);
    clear(), jog(1);
    a = find(RFM_MOVE_ACT_TURN);
    CHECK(a && a->index == RFM_MOVE_E_PRESETS);
    jog_click();                                        /* (the FM-1 layout's click: ALGO, for this slot) */
    CHECK(M.jog_enc == 2);
    rest();
    hold_track(1), hit(26), track(1, 0), rest();        /* (its note layout again: the FM-1 page kept, the target ALGO
                                                         * now; 0.14.0, the owner's 2026-11-24: "If I go to another
                                                         * track or mode and come back, the lock state should exit":
                                                         * the jog its pages again) */
    CHECKF(M.s.mode == RFM_MOVE_MODE_SEQ && M.seq.fwp && M.jog_enc == 2 && !M.fwj, "mode %d fwp %d enc %d fwj %d", M.s.mode, M.seq.fwp, M.jog_enc, M.fwj);
    /* each slot its own target: Track 1 (ChoralRoot's FM-1 layout) has SELECT */
    tap_track(0), rest();
    CHECK(M.jog_enc == 0);
    tap_track(1), rest();
    CHECK(M.jog_enc == 2);
    /* Shift + click from the pages: SELECT first, whatever the FM-1 layout had; after ALGO: the jog its pages again
     * ("Jog / Pages"), the knob's white gone, the target SELECT */
    rest(), sh_jog_click();
    CHECK(M.fwj && M.jog_enc == 0);
    sh_jog_click(), sh_jog_click();
    CHECK(M.fwj && M.jog_enc == 2);
    /* .. a click in and out of the takeover: the jog its pages again too */
    rest(), jog_click(), rest();
    CHECK(M.seq.fwp_full && rfmv_fwj_on(&M) && M.jog_enc == 2);
    jog_click(), rest();
    CHECK(!M.seq.fwp_full && !M.fwj && !rfmv_fwj_on(&M));
    sh_jog_click(), sh_jog_click(), sh_jog_click();
    CHECK(M.fwj && M.jog_enc == 2);
    rest(), clear(), sh_jog_click();
    CHECK(!M.fwj && rfmv_pop_up(&M, POP_FWJ) && M.pop_i == 0 && M.jog_enc == 0);
    write_pbm("o14_fwp_jog_off");
    rest(), leds(w), screen();
    for (i = 0; i < 3; i++)
        CHECK(!white(w[L_RING0 + i]));
    CHECK(lit_rows(1, 15) < 40);                        /* (the header gone) */
    clear(), jog(1);
    CHECK(!count(RFM_MOVE_ACT_TURN) && rfmv_seq_kpage(&M) == 2);
    jog(-1), rest();
    /* a held step takes the jog (its length) over the knob */
    sh_jog_click(), rest();
    CHECK(M.fwj);
    step(3, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20), clear(), jog(1);
    CHECK(!count(RFM_MOVE_ACT_TURN));
    step(3, 0), rest();
    sh_jog_click(), sh_jog_click(), sh_jog_click(), rest();
    CHECK(!M.fwj);
    rfm_move_set_labels(&M, NULL);
}

/* item 7: "ChoralRoot sequencer using FM-1 knobs, holding shift and turning the knob is changing the engine, which is
 * good, but there is no UI communicating what is happening or the new engine". On the FM-1 page's level 2 (Knobs+Keys,
 * where the Move's Shift is the FM-1's GLO; the FM-1 layout the same) a knob turned with Shift held: no readout of
 * that page yet: "Shift" + the knob's name (never the other page's last value); a readout then: shown; the knob's
 * label changed by the firmware: the new one */
static void test_o14_shift_knob(void)
{
    const char *lb[RFM_MOVE_LBL_N];
    const int sl = rfmv_enc_slot(RFM_MOVE_E_K1);
    int i, plain, shifted, eng;
    track_view();
    for (i = 0; i < RFM_MOVE_LBL_N; i++)
        lb[i] = NULL;
    lb[sl] = "Cutoff";
    rfm_move_set_labels(&M, lb);
    jog(1), rest(), jog_click(), rest();
    CHECK(rfm_move_fwp_level(&M) == 2);
    /* no Shift: the turn, its readout ("Cutoff 1.2k") */
    knob(4, 2);
    rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "Cutoff", "1.2k", 400, 0, 0);
    CHECK(M.pop == POP_ENC && rfmv_rd_valid(&M, sl));
    screen(), plain = lit_rows(0, 20);
    write_pbm("o14_shift_knob_plain");
    rest();
    /* Shift held (the FM-1's GLO), the knob turned: "Shift Cutoff" and the count, not "1.2k" */
    clear(), button(MR_CC_SHIFT, 1);
    CHECK(count(RFM_MOVE_ACT_BTN) == 1 && M.btn_count[RFM_MOVE_B_GLO]);
    knob(4, 2);
    CHECK(M.pop == POP_ENC && rfmv_pop_up(&M, POP_ENC) && !rfmv_rd_valid(&M, sl) && count(RFM_MOVE_ACT_TURN) == 1);
    screen(), shifted = lit_rows(0, 20);
    CHECKF(shifted > plain + 20, "the title: %d lit, plain %d", shifted, plain);
    write_pbm("o14_shift_knob_noreadout");
    /* the firmware's readout for it: shown at once */
    rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "Engine", "FM2", -1, 0, 0);
    CHECK(rfmv_rd_valid(&M, sl) && !strcmp(M.rd[sl].title, "Engine") && rfmv_pop_up(&M, POP_ENC));
    write_pbm("o14_shift_knob_readout");
    /* the firmware renames the knob while Shift is held, no readout: "Shift Engine" */
    lb[sl] = "Engine";
    rfm_move_set_labels(&M, lb);
    M.rd[sl].on = 0;
    knob(4, 2);
    screen(), eng = lit_rows(0, 20);
    CHECK(rfmv_pop_up(&M, POP_ENC) && eng > 0);
    write_pbm("o14_shift_knob_label");
    /* Shift let go: the page's own again; the Shift page's readout is not shown there */
    button(MR_CC_SHIFT, 0), rest();
    lb[sl] = "Cutoff";
    rfm_move_set_labels(&M, lb);
    button(MR_CC_SHIFT, 1);
    rfm_move_readout(&M, sl, RFM_MOVE_RD_PARAM, "Engine", "FM3", -1, 0, 0);   /* (a Shift-page readout, while held) */
    button(MR_CC_SHIFT, 0), rest();
    knob(4, 2);
    CHECK(!strcmp(M.rd[sl].title, "Engine") && !rfmv_rd_valid(&M, sl) && rfmv_pop_up(&M, POP_ENC));   /* ("Cutoff +1") */
    write_pbm("o14_shift_knob_after");
    rest();
    /* (the host's trace) a turn that gets no readout of its own: the older readout no longer shown as current after
     * RFM_MOVE_RD_PAIR_MS; the turn itself (name, count) until a readout comes, which then shows at once */
    {
        unsigned h_rd, h_turn;
        knob(4, 2);
        rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "Cutoff", "1.2k", 400, 0, 0);
        screen(), h_rd = area_hash(0, 0, 128, 64);
        rest();
        knob(4, 2), adv_ms(RFM_MOVE_RD_PAIR_MS - 100), screen();
        CHECK(area_hash(0, 0, 128, 64) == h_rd);       /* (its pair time: the readout may still come) */
        adv_ms(150), screen(), h_turn = area_hash(0, 0, 128, 64);
        CHECK(h_turn != h_rd && rfmv_pop_up(&M, POP_ENC));
        write_pbm("o14_knob_turn_noreadout");
        rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "Cutoff", "1.3k", 410, 0, 0);
        screen();
        CHECK(area_hash(0, 0, 128, 64) != h_turn);
        rest();
    }
    jog_click(), rest();
    CHECK(rfm_move_fwp_level(&M) == 1);
    /* level 1 (Knobs): Shift is the sequencer's; (0.14.0, the owner's 2026-11-24, item 6) a knob turned with it is the
     * firmware's shifted turn too: GLO down before it, up with Shift (test_n24_shift_knob) */
    clear(), button(MR_CC_SHIFT, 1), knob(1, 2);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_GLO) == 1 && acts_of(RFM_MOVE_ACT_TURN, RFM_MOVE_E_PRESETS) == 1 &&
          M.btn_count[RFM_MOVE_B_GLO]);
    write_pbm("o14_shift_knob_level1");
    button(MR_CC_SHIFT, 0), rest();
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_GLO) == 2 && !M.btn_count[RFM_MOVE_B_GLO]);
    rfm_move_set_labels(&M, NULL);
}

/* items 8..13: the projects page (the owner's 2026-11-23) */
static void test_o14_projects(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int k, hi = 0, lo = 0;
    ps_two_projects();
    grid();
    CHECK(M.seq.proj_cur == 0 && (M.seq.proj_used >> 6 & 1));
    /* 10: the open project's pad white, breathing (stopped); a stored one its colour, dim */
    for (k = 0; k < 20; k++) {
        adv_ms(50), leds(w);
        hi += full(w[L_PAD0 + ps_pad(0)]);
        lo += white(w[L_PAD0 + ps_pad(0)]) && !full(w[L_PAD0 + ps_pad(0)]);
    }
    CHECKF(hi >= 10 && lo >= 3, "open project: %d bright, %d dim", hi, lo);
    CHECK(lit(w[L_PAD0 + ps_pad(6)]) && !white(w[L_PAD0 + ps_pad(6)]));
    write_pbm("o14_proj_grid");
    /* 9: a project's pad pressed: its info at once, nothing loaded; held past the gate: info only, its release nothing */
    ps_clear(), pad(ps_pad(6), 100);
    CHECK(M.pop == POP_PROJ && M.pop_i == 6 && rfmv_pop_up(&M, POP_PROJ) && !ps_count(RFM_MOVE_ACT_PROJECT_LOAD));
    write_pbm("o14_proj_info");
    adv_ms(RFM_MOVE_TAP_MS + 50);
    pad(ps_pad(6), 0);
    CHECK(!ps_count(RFM_MOVE_ACT_PROJECT_LOAD) && M.seq.proj_cur == 0 && M.seq.proj_pending < 0);
    /* 11: the open one clicked: saved now, "Saved", its pad blinks; nothing loads */
    rest(), ps_clear(), hit(ps_pad(0));
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_SAVE) == 1 && ps_find(RFM_MOVE_ACT_PROJECT_SAVE, -1)->value == 1 &&
          !ps_count(RFM_MOVE_ACT_PROJECT_LOAD) &&
          !ps_count(RFM_MOVE_ACT_PROJECT_FW) && M.pop == POP_HOST && !strcmp(M.pop_text, "Saved"));
    write_pbm("o14_proj_saved");
    leds(w);
    CHECK(!lit(w[L_PAD0 + ps_pad(0)]));                 /* (off, on, off) */
    adv_ms(RFMV_PJ_ACK_MS + 10), leds(w);
    CHECK(full(w[L_PAD0 + ps_pad(0)]));
    rest();
    /* 12: Copy + project 7, then an empty pad: pasted; project 7 no longer dashed (the buffer kept: a second paste) */
    button(MR_CC_COPY, 1), hit(ps_pad(6)), button(MR_CC_COPY, 0);
    CHECK(M.seq.pbuf_kind == 1 && M.seq.pbuf_src == 6);
    button(MR_CC_COPY, 1), hit(ps_pad(10)), button(MR_CC_COPY, 0), rest();
    CHECK((M.seq.proj_used >> 10 & 1) && M.seq.pbuf_kind == 1 && M.seq.pbuf_src != 6);
    screen();
    CHECK(px(6 * 16 + 1, 1) && px(6 * 16 + 2, 1) && px(6 * 16 + 3, 1));   /* (project 7 outlined: no dashes) */
    write_pbm("o14_proj_pasted");
    ps_clear(), button(MR_CC_COPY, 1), hit(ps_pad(11)), button(MR_CC_COPY, 0);
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_PASTE) == 1 && (M.seq.proj_used >> 11 & 1));
    rest();
    /* 13: Delete + project 7: a red flash, then dark; the X only while Delete is held, then an empty slot */
    ps_clear(), button(MR_CC_DELETE, 1), hit(ps_pad(6));
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_CUT) == 1 && !(M.seq.proj_used >> 6 & 1));
    leds(w);
    CHECK(w[L_PAD0 + ps_pad(6)][0] == 255 && !w[L_PAD0 + ps_pad(6)][1]);
    screen();
    CHECK(px(6 * 16 + 4, 4) || px(6 * 16 + 5, 5));      /* (the X while Delete is held) */
    write_pbm("o14_proj_delete_held");
    button(MR_CC_DELETE, 0), adv_ms(RFMV_PJ_DEL_MS + 20), leds(w), screen();
    CHECK(!lit(w[L_PAD0 + ps_pad(6)]) && px(6 * 16 + 7, 7) && !px(6 * 16 + 1, 1) && !px(6 * 16 + 4, 4));
    write_pbm("o14_proj_deleted");
    /* 8: Undo, lit while it can: project 7 back where it was (the host's buffer pasted there) */
    CHECK(w[L_WHITE0 + W_UNDO][0] == 127);
    ps_clear(), press(MR_CC_UNDO);
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_PASTE) == 1 && ps_find(RFM_MOVE_ACT_PROJECT_PASTE, -1) &&
          (M.seq.proj_used >> 6 & 1) && M.seq.info[6].ok && M.pop == POP_HOST && strstr(M.pop_text, "Undo"));
    write_pbm("o14_proj_undo");
    leds(w);
    CHECK(!w[L_WHITE0 + W_UNDO][0] && lit(w[L_PAD0 + ps_pad(6)]));
    ps_clear(), press(MR_CC_UNDO);                      /* (one step) */
    CHECK(!npa);
    /* .. the open project deleted: Undo pastes it back and opens it again */
    ps_clear(), button(MR_CC_DELETE, 1), hit(ps_pad(0)), button(MR_CC_DELETE, 0);
    CHECK(!(M.seq.proj_used & 1));
    ps_clear(), press(MR_CC_UNDO);
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_PASTE) == 1 && ps_count(RFM_MOVE_ACT_PROJECT_LOAD) == 1 && M.seq.proj_pending == 0);
    rfm_move_project_load(&M, 0, NULL, 0), rest();
    /* .. leaving the page ends it */
    button(MR_CC_DELETE, 1), hit(ps_pad(11)), button(MR_CC_DELETE, 0);
    back(), rest(), grid();
    ps_clear(), press(MR_CC_UNDO);
    CHECK(!ps_count(RFM_MOVE_ACT_PROJECT_PASTE));
    back(), rest();
}

/* item 14: "Clearing the clip should only clear the steps on that clip and reset to 1 page/bar", and in Knobs+Keys
 * (the FM-1 page's level 2) Delete + Play reached the firmware as LFO + PLAY and cleared nothing */
static void test_o14_clear_clip(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int lv, clip;
    track_view();
    clip = edit_clip_now();
    jog(1), rest();
    for (lv = 1; lv <= 2; lv++) {
        if (lv == 2)
            jog_click(), rest();
        CHECK(rfm_move_fwp_level(&M) == lv);
        rfmv_seq_loop_set(&M, clip, 0, 32);
        rfmv_seq_note_put(&M, clip, 3, 60, 100, 24, 0), rfmv_seq_note_put(&M, clip, 20, 62, 100, 24, 0);
        clear(), button(MR_CC_DELETE, 1);
        if (lv == 2) {
            leds(w);
            CHECK(w[L_WHITE0 + W_DELETE][0] == 127);     /* (the sequencer's Delete: full at the press) */
        }
        press(MR_CC_PLAY), button(MR_CC_DELETE, 0);
        CHECKF(!count(RFM_MOVE_ACT_BTN) && !count(RFM_MOVE_ACT_MIDI) && !M.seq.playing && !M.seq.start_pending,
               "level %d: %d buttons to the firmware", lv, count(RFM_MOVE_ACT_BTN));
        CHECKF(!rfmv_seq_clip_used(&M, clip) && M.seq.len[clip] == 16 && M.seq.lstart[clip] == 0, "level %d: len %d", lv,
               M.seq.len[clip]);
        press(MR_CC_UNDO);                              /* (Undo: notes and length back; level 2: Undo is the FM-1's
                                                         * SAVE, so the sequencer's undo is called directly) */
        if (lv == 2)
            rfmv_seq_undo(&M);
        CHECK(rfmv_seq_clip_used(&M, clip) && M.seq.len[clip] == 32);
        rest();
    }
    write_pbm("o14_clear_clip_l2");
    /* level 2: Delete clicked alone: the firmware's LFO, a tap at the release */
    clear(), button(MR_CC_DELETE, 1);
    CHECK(!count(RFM_MOVE_ACT_BTN));
    button(MR_CC_DELETE, 0);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_LFO) == 2);
    /* .. held past the gate: nothing to the firmware */
    rest(), clear(), button(MR_CC_DELETE, 1), adv_ms(RFM_MOVE_TAP_MS + 30), button(MR_CC_DELETE, 0);
    CHECK(!count(RFM_MOVE_ACT_BTN));
    /* .. Delete + a pad: its notes out of the clip, nothing to the firmware */
    rest(), clear(), button(MR_CC_DELETE, 1), hit(0), button(MR_CC_DELETE, 0);
    CHECK(!count(RFM_MOVE_ACT_BTN));
    /* Copy: a click the firmware's EDIT; held + a step: the sequencer's (the step is not the FM-1's key) */
    rest(), clear(), press(MR_CC_COPY);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_EDIT) == 2);
    rest(), clear(), button(MR_CC_COPY, 1), step(3, 1), step(3, 0), button(MR_CC_COPY, 0);
    CHECK(!count(RFM_MOVE_ACT_BTN) && !count(RFM_MOVE_ACT_KEY));
    /* Play alone in level 2: the firmware's PLAY, the Move's sequencer stays stopped (unchanged; the owner's call) */
    rest(), clear(), press(MR_CC_PLAY);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_PLAY) == 2 && !M.seq.playing && !M.seq.start_pending);
    jog_click(), rest(), jog(-1), rest();
}

/* ================================================================ the owner's notes of 2026-11-24 === */
/* the Track buttons over 1.2 s: each one's dimmest and brightest */
static void n24_tracks(int lo[4], int hi[4])
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int k, t;
    for (t = 0; t < 4; t++)
        lo[t] = 9999, hi[t] = 0;
    for (k = 0; k < 24; k++) {
        adv_ms(50), leds(w);
        for (t = 0; t < 4; t++) {
            const int b = bright(w[L_TRACK0 + t]);
            lo[t] = b < lo[t] ? b : lo[t], hi[t] = b > hi[t] ? b : hi[t];
        }
    }
}
/* item 1: "Track Button LEDs should stay lit to show which Firmware/Mode I'm on": the played firmware's Track lit at
 * rest in every mode (steady in the FM-1 layout and a takeover, breathing in the sequencer), never dark and always
 * brighter than the others loaded; an empty slot dark */
static void test_n24_track_leds(void)
{
    int lo[4], hi[4];
    /* the note layout (track view): Track 2 breathing, Track 1 a quarter, Track 3 (empty) dark, Track 4 dim */
    track_view();
    n24_tracks(lo, hi);
    CHECKF(lo[1] > 2 * hi[0] && hi[1] > lo[1] && !hi[2] && hi[3] < lo[1], "note layout: %d..%d / %d / %d / %d", lo[1],
           hi[1], hi[0], hi[2], hi[3]);
    write_pbm("n24_tracks_note");
    /* the FM-1 knob page, Knobs and Knobs+Keys: the same */
    jog(1), rest();
    n24_tracks(lo, hi);
    CHECK(lo[1] > 2 * hi[0] && hi[1] > lo[1] && !hi[2]);
    jog_click(), rest();
    CHECK(rfm_move_fwp_level(&M) == 2);
    n24_tracks(lo, hi);
    CHECK(lo[1] > 2 * hi[0] && hi[1] > lo[1] && !hi[2]);
    write_pbm("n24_tracks_knobskeys");
    jog_click(), rest(), jog(-1), rest();
    /* session view: the same */
    press(MR_CC_MENU), rest();
    CHECK(M.seq.view);
    n24_tracks(lo, hi);
    CHECK(lo[1] > 2 * hi[0] && hi[1] > lo[1] && !hi[2] && hi[3] < lo[1]);
    write_pbm("n24_tracks_session");
    press(MR_CC_MENU), rest();
    /* the FM-1 layout (Track 1's): Track 1 steady full, Track 2 a quarter */
    hold_track(0), hit(24), track(0, 0), rest(), tap_track(0), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && M.fw.active == 0);
    n24_tracks(lo, hi);
    CHECKF(lo[0] == hi[0] && lo[0] > 2 * hi[1] && !hi[2] && hi[3] < lo[0], "FM-1 layout: %d..%d / %d", lo[0], hi[0], hi[1]);
    write_pbm("n24_tracks_fm1");
    /* a takeover (ChoralRoot's, not claiming the Tracks): steady, as the FM-1 layout */
    rfm_move_set_takeover(&M, 0, 0x5F, "ChoralRoot");
    hold_track(0), hit(25), track(0, 0), rest();
    CHECKF(m_tk_shown() == 0, "takeover %d mode %d", m_tk_shown(), M.s.mode);
    n24_tracks(lo, hi);
    CHECK(lo[0] == hi[0] && lo[0] > 2 * hi[1] && !hi[2]);
    write_pbm("n24_tracks_takeover");
    /* the Mixer (Track 4's page): Track 4 full, the played Track still lit (60 %, steady) */
    tap_track(1), rest(), press(MR_CC_MENU), rest(), tap_track(3), rest();
    CHECK(M.seq.projects);
    n24_tracks(lo, hi);
    CHECKF(lo[1] == hi[1] && lo[1] > 2 * hi[0] && lo[3] > hi[1] && !hi[2], "Mixer: %d..%d, Track 4 %d", lo[1], hi[1], lo[3]);
    write_pbm("n24_tracks_mixer");
    /* the empty slot played: faint white, never dark */
    tap_track(2), rest();
    n24_tracks(lo, hi);
    CHECK(lo[2] > 0);
}

/* item 2: "Holding back + Play should always trigger play on the Move's side": where Play alone is the firmware's (the
 * FM-1 layout, Knobs+Keys, a takeover claiming the buttons) Back held + Play starts and stops the Move's sequencer;
 * the firmware hears neither; Back's click does not come */
static void n24_back_play(const char *pic)
{
    clear(), back_down(), button(MR_CC_PLAY, 1);
    CHECKF((M.seq.playing || M.seq.start_pending) && M.pop == POP_BACK && M.pop_i == BK_PLAY, "%s: playing %d pop %d",
           pic, M.seq.playing, M.pop);
    CHECKF(strstr(said, "Move play") != NULL, "said %s", said);
    write_pbm(pic);
    button(MR_CC_PLAY, 0), back_up(), adv_ms(300);
    CHECK(M.seq.playing && !count(RFM_MOVE_ACT_BTN) && !count(RFM_MOVE_ACT_TK_INPUT));
    clear(), back_down(), button(MR_CC_PLAY, 1), back_up(), button(MR_CC_PLAY, 0);   /* (released in either order) */
    CHECK(!M.seq.playing && !M.seq.start_pending && M.pop_i == BK_STOP && !count(RFM_MOVE_ACT_BTN) &&
          !count(RFM_MOVE_ACT_TK_INPUT));
    rest();
}
static void test_n24_back_play(void)
{
    const int8_t oct = 0;
    track_view();
    /* Knobs+Keys: Play alone the firmware's PLAY; Back + Play the Move's */
    jog(1), rest(), jog_click(), rest();
    CHECK(rfm_move_fwp_level(&M) == 2);
    clear(), press(MR_CC_PLAY);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_PLAY) == 2 && !M.seq.playing);
    rest(), n24_back_play("n24_back_play_knobskeys");
    CHECK(rfm_move_fwp_level(&M) == 2 && M.s.mode == RFM_MOVE_MODE_SEQ);
    jog_click(), rest(), jog(-1), rest();
    /* the track view: Back + Play is still "Bar 1" (never a start) */
    clear(), back_down(), button(MR_CC_PLAY, 1);
    CHECK(!M.seq.playing && !M.seq.start_pending && M.pop_i == BK_BAR1);
    button(MR_CC_PLAY, 0), back_up(), rest();
    /* the FM-1 layout: the same; the mode stays (no Back click) */
    hold_track(1), hit(24), track(1, 0), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1);
    clear(), press(MR_CC_PLAY);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_PLAY) == 2 && !M.seq.playing);
    rest(), n24_back_play("n24_back_play_fm1");
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && M.s.octave == oct);
    /* a takeover claiming the buttons (Back is held by the core): the same, the firmware gets no Back */
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_SCREEN | RFM_MOVE_TK_PADS | RFM_MOVE_TK_BUTTONS, "ChoralRoot");
    hold_track(0), hit(25), track(0, 0), rest(), tap_track(0), rest();
    CHECK(m_tk_shown() == 0);
    clear(), press(MR_CC_PLAY);
    CHECK(count(RFM_MOVE_ACT_TK_INPUT) == 2 && !M.seq.playing);
    rest(), n24_back_play("n24_back_play_takeover");
    CHECK(m_tk_shown() == 0);
    clear(), press(MR_CC_BACK);                         /* (Back alone: still the firmware's press and release) */
    CHECK(count(RFM_MOVE_ACT_TK_INPUT) == 2);
    rfm_move_set_takeover(&M, 0, 0, "");
}

/* item 3: "a global setting to enable so that a firmware's play button also drives the Move's playback": Global
 * settings' FW Play (Firmware / Both), saved with the settings */
static void n24_play_both(int tk)
{
    clear(), button(MR_CC_PLAY, 1);
    CHECK((tk ? count(RFM_MOVE_ACT_TK_INPUT) == 1 : acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_PLAY) == 1) &&
          (M.seq.playing || M.seq.start_pending));
    button(MR_CC_PLAY, 0), adv_ms(300);
    clear(), press(MR_CC_PLAY);
    CHECK((tk ? count(RFM_MOVE_ACT_TK_INPUT) == 2 : acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_PLAY) == 2) &&
          !M.seq.playing && !M.seq.start_pending);
    rest();
}
static void test_n24_fw_play(void)
{
    uint8_t blob[RFM_MOVE_SETTINGS_BYTES];
    rfm_move_settings_t s2;
    int i;
    track_view();
    CHECK(M.s.fw_play == 0);
    button(MR_CC_SHIFT, 1), step(1, 1), step(1, 0), button(MR_CC_SHIFT, 0);
    CHECK(M.seq.smenu == RFM_MOVE_SM_GLOBAL);
    for (i = 0; i < 10; i++)
        jog(1);
    clear(), jog_click(), jog(1);
    CHECK(M.s.fw_play == 1 && count(RFM_MOVE_ACT_SETTINGS) >= 1);
    CHECKF(strstr(said, "FW Play: Both") != NULL, "said %s", said);
    write_pbm("n24_fw_play");
    jog_click(), back(), rest();
    CHECK(!M.seq.smenu);
    /* saved: in pop_half's byte, bit 1; Popup size kept apart */
    M.s.pop_half = 0;
    rfm_move_save(&M.s, blob);
    CHECK(rfm_move_load(&s2, blob, sizeof blob) == 0 && s2.fw_play == 1 && s2.pop_half == 0);
    M.s.pop_half = 1, M.s.fw_play = 0;
    rfm_move_save(&M.s, blob);
    CHECK(rfm_move_load(&s2, blob, sizeof blob) == 0 && s2.fw_play == 0 && s2.pop_half == 1);
    M.s.pop_half = 0, M.s.fw_play = 1;
    /* the track view: Play is the Move's (once, not twice) */
    clear(), button(MR_CC_PLAY, 1);
    CHECK(M.seq.playing || M.seq.start_pending);
    button(MR_CC_PLAY, 0), press(MR_CC_PLAY), rest();
    CHECK(!M.seq.playing && !M.seq.start_pending && !count(RFM_MOVE_ACT_BTN));
    /* Knobs+Keys: the firmware's PLAY and the Move's transport */
    jog(1), rest(), jog_click(), rest();
    n24_play_both(0);
    /* .. Shift (the firmware's GLO there) + Play: the firmware's only */
    clear(), button(MR_CC_SHIFT, 1), press(MR_CC_PLAY), button(MR_CC_SHIFT, 0);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_PLAY) == 2 && !M.seq.playing && !M.seq.start_pending);
    jog_click(), rest(), jog(-1), rest();
    /* the FM-1 layout */
    hold_track(1), hit(24), track(1, 0), rest();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1);
    n24_play_both(0);
    /* .. Back + Play: the Move's only, once */
    clear(), back_down(), press(MR_CC_PLAY), back_up();
    CHECK((M.seq.playing || M.seq.start_pending) && !count(RFM_MOVE_ACT_BTN));
    back_down(), press(MR_CC_PLAY), back_up(), rest();
    CHECK(!M.seq.playing && !M.seq.start_pending);
    /* a takeover claiming the buttons */
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_SCREEN | RFM_MOVE_TK_PADS | RFM_MOVE_TK_BUTTONS, "ChoralRoot");
    hold_track(0), hit(25), track(0, 0), rest(), tap_track(0), rest();
    CHECK(m_tk_shown() == 0);
    n24_play_both(1);
    rfm_move_set_takeover(&M, 0, 0, "");
    /* Firmware (the default): the firmware's Play only */
    M.s.fw_play = 0;
    hold_track(1), hit(24), track(1, 0), rest(), tap_track(1), rest();
    clear(), press(MR_CC_PLAY);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_PLAY) == 2 && !M.seq.playing && !M.seq.start_pending);
}

/* item 4: "Back should exit out of the FM-1 knob lock. If I go to another track or mode and come back, the lock state
 * should exit": Back (nothing else open) ends Knobs+Keys and the jog on a knob; another track, view, mode, slot too;
 * the track's knob page stays */
static void test_n24_knob_lock(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int tr;
    track_view();
    tr = M.seq.sel_track;
    jog(1), rest(), jog_click(), rest();
    CHECK(rfm_move_fwp_level(&M) == 2);
    leds(w);
    CHECK(w[L_WHITE0 + W_BACK][0] == 80);               /* (Back lit: its click has something to end) */
    /* Back: Knobs ("Knobs"), the firmware hears nothing */
    clear(), back();
    CHECK(rfm_move_fwp_level(&M) == 1 && !M.fwj && M.pop == POP_FWLV && M.pop_i == 0 && !count(RFM_MOVE_ACT_BTN));
    CHECKF(strstr(said, "Knobs") != NULL, "said %s", said);
    write_pbm("n24_lock_back");
    rest(), leds(w);
    CHECK(w[L_WHITE0 + W_BACK][0] == 0 && !rfmv_back_has(&M));   /* (nothing left for Back) */
    clear(), back();                                    /* (again: nothing, the page stays) */
    CHECK(rfm_move_fwp_level(&M) == 1 && M.seq.fwp && M.s.mode == RFM_MOVE_MODE_SEQ && !M.seq.view);
    /* the jog on a knob (Shift + click): Back ends it; a menu open: Back closes the menu first */
    sh_jog_click(), rest();
    CHECK(M.fwj);
    button(MR_CC_SHIFT, 1), step(2, 1), step(2, 0), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    CHECK(M.seq.smenu == RFM_MOVE_SM_WORKFLOW);
    back();
    CHECK(!M.seq.smenu && M.fwj);
    rest(), back();
    CHECK(!M.fwj && rfm_move_fwp_level(&M) == 1);
    rest();
    /* another track (Track 2 held + step 7) and back: Knobs, the FM-1 page kept */
    jog_click(), rest();
    hold_track(1), step(6, 1), step(6, 0), track(1, 0), rest();
    CHECK(M.seq.sel_track == 6 && !M.seq.fwp_full);
    hold_track(1), step(tr, 1), step(tr, 0), track(1, 0), rest();
    CHECK(M.seq.sel_track == tr && rfm_move_fwp_level(&M) == 1 && rfmv_seq_kpage(&M) == 1);
    write_pbm("n24_lock_track_back");
    /* session view and back */
    sh_jog_click(), jog_click(), rest();
    CHECK(M.seq.fwp_full);
    back_down(), press(MR_CC_MENU), back_up(), rest();   /* (Menu is the firmware's HOME in Knobs+Keys: Back + Menu) */
    CHECK(M.seq.view);
    press(MR_CC_MENU), rest();
    CHECKF(rfm_move_fwp_level(&M) == 1 && !M.fwj && rfmv_seq_kpage(&M) == 1, "level %d fwj %d kpage %d full %d sel %d",
           rfm_move_fwp_level(&M), M.fwj, rfmv_seq_kpage(&M), M.seq.fwp_full, M.seq.sel_track);
    /* another slot and back */
    jog_click(), rest();
    tap_track(0), rest(), tap_track(1), rest();
    CHECK(rfm_move_fwp_level(&M) == 1 && !M.fwj && rfmv_seq_kpage(&M) == 1);
    /* another page and back (the same track, mode, view): the lock stays, as before */
    jog_click(), rest(), jog(1), rest(), jog(-1), rest();
    CHECK(rfm_move_fwp_level(&M) == 2);
    back(), rest(), jog(-1), rest();
}

/* item 5: "No need to draw the little lines to show which parameter is selected, the highlighted parameter is enough":
 * the track settings (Shift + Track 2, Shift + step 3) draw no knob ticks under the title, at rest, selecting, editing
 * or with a knob touched; the selected row is the only mark. (The picture menus keep their ticks: R5's mark for the
 * knob's place; the arp's too) */
static void test_n24_settings_ticks(void)
{
    track_view();
    button(MR_CC_SHIFT, 1), track(1, 1), track(1, 0), button(MR_CC_SHIFT, 0), rest();
    CHECK(M.seq.smenu == RFM_MOVE_SM_WORKFLOW);
    screen();
    CHECK(!lit_rows(15, 19));
    write_pbm("n24_settings_rest");
    jog(1), screen();
    CHECK(!lit_rows(15, 19) && px(1, 35) && px(126, 35));   /* (row 2 inverted, nothing over the rows) */
    write_pbm("n24_settings_sel");
    jog_click(), screen();
    CHECK(!lit_rows(15, 19) && px(0, 41) && px(127, 41));
    write_pbm("n24_settings_edit");
    jog_click(), touch(2, 1), screen();
    CHECK(!lit_rows(15, 19));
    touch(2, 0), back(), rest();
    /* Tempo (a picture menu) keeps its ticks */
    shift_step(4), rest(), screen();
    CHECK(tick(0, 1) && tick(1, 0));
    back(), rest();
}

/* item 6: "While not in takeover mode, the Move's shift button plus a FM-1 knob turn should also do the shift and show
 * the engine change": the FM-1 page's Knobs: Shift held + a knob: GLO down before the first turn, up with Shift; the
 * popup "Shift" + the knob's name; Shift's own jobs unchanged; Shift alone sends nothing */
static void test_n24_shift_knob(void)
{
    const char *lb[RFM_MOVE_LBL_N];
    const int sl = rfmv_enc_slot(RFM_MOVE_E_K1);
    int i, plain, shifted, bpm;
    track_view();
    for (i = 0; i < RFM_MOVE_LBL_N; i++)
        lb[i] = NULL;
    lb[sl] = "Cutoff";
    rfm_move_set_labels(&M, lb);
    jog(1), rest();
    CHECK(rfm_move_fwp_level(&M) == 1);
    knob(4, 2), adv_ms(RFM_MOVE_RD_PAIR_MS + 20), screen(), plain = lit_rows(0, 20);
    rest();
    /* Shift pressed and let go alone: nothing to the firmware */
    clear(), button(MR_CC_SHIFT, 1), adv_ms(100), button(MR_CC_SHIFT, 0);
    CHECK(!count(RFM_MOVE_ACT_BTN) && !count(RFM_MOVE_ACT_TURN));
    rest();
    /* Shift held + knob 5 (K1): GLO down first, then the turn; "Shift Cutoff" */
    clear(), button(MR_CC_SHIFT, 1);
    CHECK(!count(RFM_MOVE_ACT_BTN));
    knob(4, 2);
    CHECK(nact >= 2 && A[0].kind == RFM_MOVE_ACT_BTN && A[0].index == RFM_MOVE_B_GLO && A[0].down &&
          acts_of(RFM_MOVE_ACT_TURN, RFM_MOVE_E_K1) == 1 && M.btn_count[RFM_MOVE_B_GLO]);
    adv_ms(RFM_MOVE_RD_PAIR_MS + 20), screen(), shifted = lit_rows(0, 20);
    CHECKF(rfmv_pop_up(&M, POP_ENC) && shifted > plain + 20, "the title: %d lit, plain %d", shifted, plain);
    write_pbm("n24_shift_knob");
    /* the firmware's readout (its engine), coming with a turn: shown at once */
    knob(4, 1);
    rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "Engine", "FM2", -1, 0, 0);
    CHECK(rfmv_rd_valid(&M, sl) && !strcmp(M.rd[sl].title, "Engine"));
    write_pbm("n24_shift_knob_engine");
    clear(), knob(5, 2);                                /* (another knob: GLO stays down, not sent again) */
    CHECK(!count(RFM_MOVE_ACT_BTN) && acts_of(RFM_MOVE_ACT_TURN, RFM_MOVE_E_K2) == 1);
    clear(), button(MR_CC_SHIFT, 0);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_GLO) == 1 && !find(RFM_MOVE_ACT_BTN)->down && !M.btn_count[RFM_MOVE_B_GLO]);
    CHECK(!(M.seq.mods & SEQ_M_SHIFT) && !M.seq.shift_lock);   /* (no latch from it) */
    rest();
    /* Shift's own jobs: + step 3 (the track settings), + the jog's turn (the tempo), + the jog's click (the jog on a
     * knob): nothing to the firmware */
    clear(), shift_step(2);
    CHECK(M.seq.smenu == RFM_MOVE_SM_WORKFLOW && !count(RFM_MOVE_ACT_BTN));
    back(), rest();
    bpm = rfm_move_seq_bpm10(&M);
    clear(), button(MR_CC_SHIFT, 1), jog(1), button(MR_CC_SHIFT, 0);
    CHECK(rfm_move_seq_bpm10(&M) != bpm && !count(RFM_MOVE_ACT_BTN) && rfmv_seq_kpage(&M) == 1);
    rest(), button(MR_CC_SHIFT, 1), jog(-1), button(MR_CC_SHIFT, 0), rest();
    clear(), sh_jog_click();
    CHECK(M.fwj && !count(RFM_MOVE_ACT_BTN));
    rest(), back(), rest();
    /* Knobs+Keys: Shift is the firmware's GLO at its own press, as before */
    jog_click(), rest(), clear(), button(MR_CC_SHIFT, 1);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_GLO) == 1);
    knob(4, 2), button(MR_CC_SHIFT, 0);
    CHECK(acts_of(RFM_MOVE_ACT_BTN, RFM_MOVE_B_GLO) == 2 && !M.btn_count[RFM_MOVE_B_GLO] && !M.fwp_glo);
    rest(), back(), rest(), jog(-1), rest();
    rfm_move_set_labels(&M, NULL);
}

/* item 7: "Arp does not work for the sequenced notes": the clip's notes feed the track's arp (down for their length),
 * the arp's notes go out on the track (its routing), the raw notes not; Hold and one-pass modes, the transport's stop,
 * mutes and solo; a recording takes what was played */
static int ons_of(int tr, int note)                     /* note ons of track tr (1..16), note (-1 any) */
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += A[i].kind == RFM_MOVE_ACT_MIDI && A[i].track == tr && (A[i].bytes[0] & 0xF0) == 0x90 && A[i].bytes[2] &&
             (note < 0 || A[i].bytes[1] == note);
    return n;
}
static int sounding_left(int tr)                        /* note ons minus offs of track tr since clear() */
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        if (A[i].kind == RFM_MOVE_ACT_MIDI && A[i].track == tr)
            n += ((A[i].bytes[0] & 0xF0) == 0x90 && A[i].bytes[2]) ? 1 : ((A[i].bytes[0] & 0xF0) == 0x80 ||
                                                                          (A[i].bytes[0] & 0xF0) == 0x90) ? -1 : 0;
    return n;
}
static void test_n24_arp_clip(void)
{
    rfm_move_arp_t *ap;
    int tr, clip, n;
    track_view();
    tr = M.seq.sel_track, clip = edit_clip_now();
    ap = &M.seq.tr[tr].arp;
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 16 * 24, 0), rfmv_seq_note_put(&M, clip, 0, 64, 100, 16 * 24, 0);
    rfmv_seq_note_put(&M, clip, 0, 67, 100, 16 * 24, 0);   /* (C E G held the whole bar) */
    ap->rate = 9, ap->range = 1;                        /* (16 a bar, one octave) */
    /* the arp off: the chord itself, three notes a bar */
    clear(), press(MR_CC_PLAY), adv_ms(2000 - 10);
    n = ons_of(tr + 1, -1);
    CHECKF(n == 3 && ons_of(tr + 1, 64) == 1, "off: %d notes a bar", n);
    press(MR_CC_PLAY), adv_ms(200);
    /* on: the arp's 16ths over C E G (on the track: its routing), not the chord */
    ap->mode = RFM_MOVE_AM_ON;
    clear(), press(MR_CC_PLAY), adv_ms(60);
    CHECKF(ons_of(tr + 1, -1) == 1 && ons_of(tr + 1, 60) == 1, "the first 16th: %d", ons_of(tr + 1, -1));   /* (C, not
                                                         * the chord) */
    adv_ms(2000 - 70);
    n = ons_of(tr + 1, -1);
    CHECKF(n >= 15 && n <= 16 && ons_of(tr + 1, 64) >= 5 && ons_of(0, -1) == 0, "on: %d notes a bar, %d E", n,
           ons_of(tr + 1, 64));
    write_pbm("n24_arp_clip");
    /* the transport stopped: nothing left sounding or running */
    press(MR_CC_PLAY), adv_ms(600);
    CHECKF(sounding_left(tr + 1) == 0 && !M.seq.arps[tr].run && !M.seq.arps[tr].n && !M.seq.nsnd, "left %d",
           sounding_left(tr + 1));
    clear(), adv_ms(1000);
    CHECK(ons_of(tr + 1, -1) == 0);
    /* muted: silent; unmuted: again; another track soloed: silent */
    rfmv_seq_mute(&M, tr, 1);
    clear(), press(MR_CC_PLAY), adv_ms(1000);
    CHECK(ons_of(tr + 1, -1) == 0);
    rfmv_seq_mute(&M, tr, 0), adv_ms(2000);
    CHECK(ons_of(tr + 1, -1) >= 8);
    M.seq.tr[(tr + 1) % 16].solo = 1, clear(), adv_ms(2000);
    CHECK(ons_of(tr + 1, -1) == 0);
    M.seq.tr[(tr + 1) % 16].solo = 0;
    press(MR_CC_PLAY), adv_ms(300);
    /* one pass (Once): C E G once a bar (the chord comes again each bar) */
    ap->mode = RFM_MOVE_AM_ONCE;
    clear(), press(MR_CC_PLAY), adv_ms(4000 - 10);
    CHECKF(ons_of(tr + 1, -1) == 6 && ons_of(tr + 1, 60) == 2, "once: %d", ons_of(tr + 1, -1));
    press(MR_CC_PLAY), adv_ms(300);
    /* Hold: the clip's chord a step long; the arp goes on after it, until the transport stops */
    rfmv_seq_clip_clear(&M, clip);
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 24, 0), rfmv_seq_note_put(&M, clip, 0, 64, 100, 24, 0);
    ap->mode = RFM_MOVE_AM_HOLD;
    clear(), press(MR_CC_PLAY), adv_ms(2000 - 10);
    CHECKF(ons_of(tr + 1, -1) >= 15, "hold: %d", ons_of(tr + 1, -1));
    press(MR_CC_PLAY), adv_ms(300);
    CHECK(!M.seq.arps[tr].run && sounding_left(tr + 1) == 0);
    clear(), adv_ms(1000);
    CHECK(ons_of(tr + 1, -1) == 0);
    /* a pad with it (the selected track): merged into the same arp; let go: the clip's notes play on */
    ap->mode = RFM_MOVE_AM_ON;
    rfmv_seq_clip_clear(&M, clip);
    rfmv_seq_note_put(&M, clip, 0, 60, 100, 16 * 24, 0);
    clear(), press(MR_CC_PLAY), adv_ms(200);
    pad(0, 100), adv_ms(600);
    CHECK(M.seq.arps[tr].n == 2);
    pad(0, 0), adv_ms(100);
    CHECK(M.seq.arps[tr].n == 1 && M.seq.arps[tr].run);
    press(MR_CC_PLAY), adv_ms(300);
    CHECK(!M.seq.arps[tr].run && !M.seq.nsnd);
    ap->mode = RFM_MOVE_AM_OFF, ap->rate = 6, ap->range = 2;
    rfmv_seq_clip_clear(&M, clip);
}

/* item 8: "Changing Arp rate breaks the arp from working right until you retrigger the notes": RATE turned while a
 * note is held (stopped, then running): the run goes on at the new rate at once, no gap, no burst */
static void test_n24_arp_rate(void)
{
    rfm_move_arp_t *ap;
    int tr, run, n;
    track_view();
    tr = M.seq.sel_track, ap = &M.seq.tr[tr].arp;
    ap->mode = RFM_MOVE_AM_ON, ap->rate = 9, ap->range = 1, ap->quant = 0;   /* (16 a bar: a note each 125 ms) */
    shift_step(10), rest();
    CHECK(M.seq.smenu == RFM_MOVE_SM_ARP);
    for (run = 0; run < 2; run++) {
        if (run)
            press(MR_CC_PLAY), adv_ms(100);
        pad(0, 100), adv_ms(1000);
        clear(), adv_ms(1000);
        n = ons_of(tr + 1, -1);
        CHECKF(n >= 7 && n <= 9, "%s, 1/16: %d a second", run ? "running" : "stopped", n);
        knob(1, -3 * RFM_MOVE_LIST_TICKS);              /* (RATE 1/8: a note each 250 ms) */
        CHECK(ap->rate == 6);
        clear(), adv_ms(1000);
        n = ons_of(tr + 1, -1);
        CHECKF(n >= 3 && n <= 5, "%s, slower: %d a second", run ? "running" : "stopped", n);
        knob(1, 6 * RFM_MOVE_LIST_TICKS);               /* (RATE 1/32: a note each 62.5 ms) */
        CHECK(ap->rate == 12);
        clear(), adv_ms(100);
        n = ons_of(tr + 1, -1);
        CHECKF(n <= 2, "%s, faster: %d in the first 100 ms (no burst)", run ? "running" : "stopped", n);
        adv_ms(900);
        n = ons_of(tr + 1, -1);
        CHECKF(n >= 14 && n <= 17, "%s, faster: %d a second", run ? "running" : "stopped", n);
        knob(1, -3 * RFM_MOVE_LIST_TICKS);
        pad(0, 0), adv_ms(300);
        if (run)
            press(MR_CC_PLAY), adv_ms(100);
    }
    write_pbm("n24_arp_rate");
    back(), rest();
    ap->mode = RFM_MOVE_AM_OFF, ap->rate = 6, ap->range = 2, ap->quant = 10;
}

/* item 9: "Need a page/quick way to reset the arp to defaults": Delete held + the jog's click in the arp menu: the main
 * page the whole arp (lanes too, on / off kept), a lane page that lane; one undo step; Delete lit in the menu */
static void test_n24_arp_reset(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    rfm_move_arp_t *ap, keep, def;
    int tr, i;
    track_view();
    tr = M.seq.sel_track, ap = &M.seq.tr[tr].arp;
    rfmv_seq_arp_default(&def);
    shift_step(10), rest();
    CHECK(M.seq.smenu == RFM_MOVE_SM_ARP);
    jog_click(), rest();                                /* (on) */
    leds(w);
    CHECK(w[L_WHITE0 + W_DELETE][0] == 90);
    ap->pat = 5, ap->rate = 11, ap->range = 4, ap->gate = 150, ap->odist = -7, ap->quant = 0, ap->reset = 2;
    ap->mlen = 5, ap->tlen = 9;
    for (i = 0; i < 16; i++)
        ap->lane[0][i] = (int8_t)(i % 4), ap->lane[1][i] = (int8_t)(i - 8);
    keep = *ap;
    /* a lane page (MOD): that lane only */
    jog(1), rest();
    CHECK(M.seq.arp_page == 1);
    clear(), button(MR_CC_DELETE, 1);
    leds(w);
    CHECK(w[L_WHITE0 + W_DELETE][0] == 127);
    jog_click();
    CHECK(ap->mlen == 16 && ap->lane[0][3] == 0 && ap->tlen == 9 && ap->lane[1][0] == -8 && ap->pat == 5);
    CHECK(M.pop == POP_HOST && !strcmp(M.pop_text, "Mod Reset") && strstr(said, "Mod Reset"));
    write_pbm("n24_arp_reset_mod");
    button(MR_CC_DELETE, 0), rest();
    CHECK(M.seq.smenu == RFM_MOVE_SM_ARP && ap->mode == RFM_MOVE_AM_ON);   /* (Delete's release: nothing else) */
    press(MR_CC_UNDO), rest();
    CHECK(!memcmp(ap, &keep, sizeof keep));
    /* TRANSPOSE */
    jog(1), rest();
    button(MR_CC_DELETE, 1), jog_click(), button(MR_CC_DELETE, 0), rest();
    CHECK(ap->tlen == 16 && ap->lane[1][0] == 0 && ap->mlen == 5 && ap->lane[0][3] == 3);
    press(MR_CC_UNDO), rest();
    CHECK(!memcmp(ap, &keep, sizeof keep));
    /* the main page: all of it, the mode kept (on) */
    jog(-2), rest();
    clear(), button(MR_CC_DELETE, 1), jog_click();
    def.mode = RFM_MOVE_AM_ON;
    CHECK(!memcmp(ap, &def, sizeof def) && !strcmp(M.pop_text, "Arp Reset"));
    write_pbm("n24_arp_reset");
    button(MR_CC_DELETE, 0), rest();
    press(MR_CC_UNDO), rest();                          /* (one undo step: all of it back) */
    CHECK(!memcmp(ap, &keep, sizeof keep));
    /* the click alone still turns it off and on */
    jog_click(), rest();
    CHECK(ap->mode == RFM_MOVE_AM_OFF);
    back(), rest();
    leds(w);
    CHECK(w[L_WHITE0 + W_DELETE][0] == 30);
    rfmv_seq_arp_default(ap);
}

/* item 10: "Clicking Jogwheel on the Mod or transpose pages should create a random pattern": a lane page's click fills
 * the lane over its length, musically; a new one each click; the step keys' wave; undoable; the main page toggles */
static void test_n24_arp_random(void)
{
    rfm_move_arp_t *ap, before;
    int tr, i, k, zeros = 0, plays = 0, tot = 0, bad = 0;
    track_view();
    tr = M.seq.sel_track, ap = &M.seq.tr[tr].arp;
    M.ui_rng = 12345;
    shift_step(10), rest(), jog_click(), rest();        /* (on) */
    CHECK(ap->mode == RFM_MOVE_AM_ON);
    ap->mlen = 8;
    for (i = 8; i < 16; i++)
        ap->lane[0][i] = RFM_MOVE_AL_CHORD;
    jog(1), rest();
    before = *ap;
    clear(), jog_click();
    CHECK(ap->mode == RFM_MOVE_AM_ON && ap->lane[0][0] == RFM_MOVE_AL_PLAY && !rfmv_seq_arp_silent(&M, tr) &&
          M.seq.al_wave && memcmp(ap, &before, sizeof before) && strstr(said, "Mod random"));
    for (i = 8; i < 16; i++)
        CHECK(ap->lane[0][i] == RFM_MOVE_AL_CHORD);    /* (past its length: untouched) */
    write_pbm("n24_arp_random_mod");
    press(MR_CC_UNDO), rest();
    CHECK(!memcmp(ap, &before, sizeof before));
    ap->mlen = 16;
    for (k = 0; k < 30; k++) {                          /* (each click a new one; musical) */
        rfm_move_arp_t prev = *ap;
        jog_click();
        CHECK(memcmp(ap, &prev, sizeof prev) && ap->lane[0][0] == RFM_MOVE_AL_PLAY && !rfmv_seq_arp_silent(&M, tr));
        for (i = 0; i < 16; i++, tot++) {
            plays += ap->lane[0][i] == RFM_MOVE_AL_PLAY;
            bad += ap->lane[0][i] < 0 || ap->lane[0][i] >= RFM_MOVE_AL_N1 ||
                   (i && ap->lane[0][i] == RFM_MOVE_AL_TIE && ap->lane[0][i - 1] == RFM_MOVE_AL_REST);
        }
    }
    CHECKF(plays * 2 > tot && plays * 10 < tot * 9 && !bad, "MOD: %d plays of %d, %d bad", plays, tot, bad);
    rest();
    /* TRANSPOSE: mostly 0, octaves, fifths, a few steps */
    jog(1), rest(), tot = 0;
    for (k = 0; k < 30; k++) {
        jog_click();
        for (i = 0; i < 16; i++, tot++) {
            const int v = ap->lane[1][i];
            zeros += v == 0;
            bad += v != 0 && v != 12 && v != -12 && v != 7 && v != -5 && v != 2 && v != 3 && v != 5 && v != -2;
        }
    }
    CHECKF(zeros * 3 > tot && zeros * 10 < tot * 8 && !bad, "TRANSPOSE: %d zeros of %d, %d bad", zeros, tot, bad);
    write_pbm("n24_arp_random_transpose");
    /* the main page: the click still turns it off */
    jog(-2), rest(), jog_click();
    CHECK(ap->mode == RFM_MOVE_AM_OFF);
    back(), rest();
    rfmv_seq_arp_default(ap);
}

/* item 11: "Transpose pattern and mod patterns should have completely separate lengths. Using the step shortcut keys is
 * changing the lengths of both patterns": every way of setting a lane's length leaves the other lane's alone */
static void test_n24_arp_lens(void)
{
    rfm_move_arp_t *ap;
    int tr, pg;
    track_view();
    tr = M.seq.sel_track, ap = &M.seq.tr[tr].arp;
    shift_step(10), rest(), jog_click(), rest();
    for (pg = 1; pg <= 2; pg++) {
        uint8_t *mine = pg == 1 ? &ap->mlen : &ap->tlen, *other = pg == 1 ? &ap->tlen : &ap->mlen;
        ap->mlen = 16, ap->tlen = 16;
        jog(1), rest();
        CHECK(M.seq.arp_page == pg);
        /* step 1 held + step 5, let go in either order */
        step(0, 1), step(4, 1), step(4, 0), step(0, 0), rest();
        CHECKF(*mine == 5 && *other == 16, "page %d, step 1 + 5: %d / %d", pg, *mine, *other);
        step(0, 1), step(6, 1), step(0, 0), step(6, 0), rest();
        CHECKF(*mine == 7 && *other == 16, "page %d, step 1 + 7: %d / %d", pg, *mine, *other);
        /* knob 2 */
        knob(1, -2 * RFM_MOVE_LIST_TICKS), rest();
        CHECKF(*mine == 5 && *other == 16, "page %d, knob 2: %d / %d", pg, *mine, *other);
        /* a lane step held (its pads), then step 1 + n: still this lane's only */
        step(2, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20), step(2, 0), rest();
        step(0, 1), step(2, 1), step(2, 0), step(0, 0), rest();
        CHECKF(*mine == 3 && *other == 16, "page %d, after a held step: %d / %d", pg, *mine, *other);
        /* the random pattern and the reset: this lane's only */
        jog_click(), rest();
        CHECK(*mine == 3 && *other == 16);
        *other = 9;
        button(MR_CC_DELETE, 1), jog_click(), button(MR_CC_DELETE, 0), rest();
        CHECK(*mine == 16 && *other == 9);
    }
    /* the engine: each lane on its own length (MOD 3 of plays, TRANSPOSE 2: +0 / +12): the pattern of 6 */
    jog(-2), rest(), back(), rest();
    ap->mlen = 3, ap->tlen = 2, ap->lane[0][0] = ap->lane[0][1] = ap->lane[0][2] = RFM_MOVE_AL_PLAY;
    ap->lane[0][2] = RFM_MOVE_AL_REST, ap->lane[1][0] = 0, ap->lane[1][1] = 12, ap->range = 1, ap->rate = 9;
    ap->quant = 0;
    clear(), pad(0, 100), adv_ms(125 * 6 - 40);
    CHECKF(ons_of(tr + 1, -1) == 4, "%d notes in six steps", ons_of(tr + 1, -1));   /* (play play rest x2) */
    pad(0, 0), adv_ms(300);
    rfmv_seq_arp_default(ap);
}

/* item 12: "Arp octaves should be able to go up or down": Octaves -8..-1, 1..8 (no 0), "Oct -2"; the walk goes down;
 * saved in the CHANCE byte (99) with OCT DIST negated, no new version; read back */
static void test_n24_arp_down(void)
{
    static uint8_t blob[RFM_MOVE_PROJECT_MAX];
    rfm_move_arp_t *ap;
    int tr, n, i, lo = 0, hi = 0, ver;
    track_view();
    tr = M.seq.sel_track, ap = &M.seq.tr[tr].arp;
    shift_step(10), rest(), jog_click(), rest();
    CHECK(ap->range == 2 && !ap->down);
    knob(2, -RFM_MOVE_LIST_TICKS);
    CHECK(ap->range == 1 && !ap->down);
    clear(), knob(2, -RFM_MOVE_LIST_TICKS);             /* (over 0: -1) */
    CHECKF(ap->range == 1 && ap->down, "range %d down %d", ap->range, ap->down);
    CHECKF(strstr(said, "down") != NULL, "said %s", said);
    knob(2, -RFM_MOVE_LIST_TICKS);
    CHECK(ap->range == 2 && ap->down);
    write_pbm("n24_arp_oct_down_turn");
    rest();
    write_pbm("n24_arp_oct_down");                      /* ("Oct -2" over the walk) */
    knob(2, 2 * RFM_MOVE_LIST_TICKS);                   /* (-2 -> -1 -> 1) */
    CHECK(ap->range == 1 && !ap->down);
    knob(2, -2 * RFM_MOVE_LIST_TICKS);
    CHECK(ap->range == 2 && ap->down);
    for (i = 0; i < 20; i++)
        knob(2, -RFM_MOVE_LIST_TICKS);
    CHECK(ap->range == 8 && ap->down);
    for (i = 0; i < 6; i++)
        knob(2, RFM_MOVE_LIST_TICKS);
    CHECK(ap->range == 2 && ap->down);
    back(), rest();
    /* the walk: a note, two octaves down: n, n - 12, n, n - 12 .. */
    ap->rate = 9, ap->quant = 0;
    clear(), pad(0, 100), adv_ms(125 * 4 - 40), pad(0, 0), adv_ms(200);
    {
        int k, first = -1;
        for (k = 0; k < nact; k++)
            if (A[k].kind == RFM_MOVE_ACT_MIDI && A[k].track == tr + 1 && (A[k].bytes[0] & 0xF0) == 0x90 && A[k].bytes[2]) {
                if (first < 0)
                    first = A[k].bytes[1];
                lo += A[k].bytes[1] == first - 12, hi += A[k].bytes[1] > first;
            }
        CHECKF(lo == 2 && hi == 0, "down: %d an octave below, %d above", lo, hi);
    }
    /* saved: CHANCE 99, OCT DIST -12 (a 0.13.0 core walks them down by it); read back: Oct -2, spread +12 */
    n = rfm_move_project_save(&M, blob, sizeof blob);
    CHECK(n > 0);
    ver = blob[4];
    ap->down = 0, ap->odist = 3;
    CHECK(rfm_move_project_load(&M, 0, blob, (uint32_t)n) == 0);
    CHECKF(ap->down && ap->range == 2 && ap->odist == 12 && blob[4] == ver, "down %d range %d odist %d", ap->down, ap->range,
           ap->odist);
    rfmv_seq_arp_default(ap);
    n = rfm_move_project_save(&M, blob, sizeof blob);
    ap->down = 1;
    rfm_move_project_load(&M, 0, blob, (uint32_t)n);
    CHECK(!ap->down && ap->odist == 12);
}

/* item 13: "Loading a new project is starting playback when the Move's playback is stopped": in the core, stopped, a
 * project's load (the grid's open, the host's answer, a new empty one) never starts the transport, launches no clip
 * and sends no note or Start; and a Play pressed for a load that never came does not start a later one */
static int n24_quiet(void)                              /* stopped, nothing queued, no note on, no Start since clear() */
{
    int i, t, bad = M.seq.playing || M.seq.start_pending;
    for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
        bad |= M.seq.tr[t].queued != SEQ_NONE;
    for (i = 0; i < nact; i++)
        bad |= A[i].kind == RFM_MOVE_ACT_MIDI && (((A[i].bytes[0] & 0xF0) == 0x90 && A[i].bytes[2]) || A[i].bytes[0] == 0xFA);
    return !bad;
}
static void test_n24_load_stopped(void)
{
    ps_two_projects();
    grid();
    /* project 7 opened, the host answering at once, its firmwares ready: stopped, silent */
    ps_answer = 1;
    ps_clear(), hit(ps_pad(6));
    CHECK(M.seq.proj_cur == 6 && n24_quiet());
    rfm_move_proj_fw(&M, 0, RFM_MOVE_PFW_READY), rfm_move_proj_fw(&M, 1, RFM_MOVE_PFW_READY);
    rfm_move_proj_fw(&M, 2, RFM_MOVE_PFW_READY), adv_ms(2000);
    CHECK(!rfm_move_proj_fw_loading(&M) && n24_quiet());
    /* a new project (an empty pad), then project 1 by the host's call itself */
    rest(), ps_clear(), hit(ps_pad(20)), adv_ms(2000);
    CHECK(M.seq.proj_cur == 20 && n24_quiet());
    ps_clear(), rfm_move_project_load(&M, 0, PS_BLOB[0], PS_N[0]), adv_ms(2000);
    CHECK(M.seq.proj_cur == 0 && n24_quiet());
    ps_clear(), rfm_move_project_load(&M, 21, NULL, 0), adv_ms(2000);
    CHECK(n24_quiet());
    /* Play pressed while a project loads, the host never answering: when its wait runs out nothing starts */
    ps_answer = 0;
    rest(), ps_clear(), hit(ps_pad(6));
    CHECK(M.seq.proj_pending == 6 && rfm_move_proj_fw_loading(&M));
    press(MR_CC_PLAY);
    adv_ms(RFM_MOVE_PFW_WAIT_MS + 200);
    CHECKF(M.seq.proj_pending < 0 && n24_quiet(), "the wait ran out: playing %d", M.seq.playing || M.seq.start_pending);
    /* .. a host without the firmwares' part: Play while pending, the load refused; a later load must not start */
    ps_fresh(0);
    ps_store(3), ps_store(4);
    grid();
    ps_clear(), hit(ps_pad(3));
    CHECK(M.seq.proj_pending == 3);
    press(MR_CC_PLAY);
    CHECK(M.seq.play_after_load && n24_quiet());
    {
        static uint8_t bad[RFM_MOVE_PROJECT_MAX];
        memcpy(bad, PS_BLOB[3], PS_N[3]), bad[8] ^= 0x55;
        CHECK(rfm_move_project_load(&M, 3, bad, PS_N[3]) != 0);
    }
    ps_clear(), rfm_move_project_load(&M, 4, PS_BLOB[4], PS_N[4]), adv_ms(500);
    CHECKF(n24_quiet(), "a later load: playing %d", M.seq.playing || M.seq.start_pending);
    /* .. and Play pressed twice while pending: not after all */
    ps_clear(), hit(ps_pad(3));
    press(MR_CC_PLAY), press(MR_CC_PLAY);
    rfm_move_project_load(&M, 3, PS_BLOB[3], PS_N[3]), adv_ms(500);
    CHECK(n24_quiet());
    /* (Play once while pending: it plays once loaded, as before) */
    ps_clear(), hit(ps_pad(4)), press(MR_CC_PLAY);
    rfm_move_project_load(&M, 4, PS_BLOB[4], PS_N[4]), adv_ms(100);
    CHECK(M.seq.playing || M.seq.start_pending);
    press(MR_CC_PLAY), back(), rest();
}

/* item 14: a host's entry point for a new project in an empty slot: rfm_move_project_new does what an empty pad's
 * click does (the same actions, in order); refused for a slot that holds a project, out of range, a load pending */
static void test_n24_project_new(void)
{
    static ps_act_t click[64];
    int nc, i, same;
    ps_two_projects();
    grid();
    ps_clear(), hit(ps_pad(20));
    nc = npa < 64 ? npa : 64;
    memcpy(click, PA, sizeof click[0] * (size_t)nc);
    CHECK(M.seq.proj_cur == 20 && (M.seq.proj_used >> 20 & 1) && ps_count(RFM_MOVE_ACT_PROJECT_SAVE) == 2);
    ps_two_projects();
    grid();
    ps_clear();
    CHECK(rfm_move_project_new(&M, 20) == 0 && M.seq.proj_cur == 20 && (M.seq.proj_used >> 20 & 1));
    for (i = 0, same = npa == nc; same && i < nc; i++)
        same = PA[i].kind == click[i].kind && PA[i].slot == click[i].slot && PA[i].index == click[i].index &&
               !strcmp(PA[i].id, click[i].id);
    CHECKF(same, "%d actions, the click %d", npa, nc);
    CHECK(ps_find(RFM_MOVE_ACT_PROJECT_SAVE, -1) && ps_count(RFM_MOVE_ACT_PROJECT_FW) == 3 && n24_quiet());
    write_pbm("n24_project_new");
    /* refused: a stored one, out of range, a load pending */
    ps_clear();
    CHECK(rfm_move_project_new(&M, 6) == -1 && rfm_move_project_new(&M, 32) == -1 && rfm_move_project_new(&M, -1) == -1);
    CHECK(!npa && M.seq.proj_cur == 20);
    hit(ps_pad(6));
    CHECK(M.seq.proj_pending == 6 && rfm_move_project_new(&M, 25) == -1 && !(M.seq.proj_used >> 25 & 1));
    rfm_move_project_load(&M, 6, PS_BLOB[6], PS_N[6]);
    back(), rest();
}

/* item 15: the firmware browser's step 13: Random: a firmware of the list shown (the filter, the catalog), never the
 * one selected; it only selects (no load), named and said as a pad's selection; the pads roll to it; its icon lit
 * while the browser is open; one or none: "Nothing to pick" */
static void test_n24_fw_random(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int k, prev, uninst = 0, rolled = 0;
    track_view();
    dbl_track(0), rest();
    CHECK(M.seq.projects && M.fw.track == 0 && !M.fw.catalog);
    M.ui_rng = 777;
    leds(w);
    CHECK(w[L_ICON0 + 12][0] == 127 && lit(w[L_STEP0 + 12]));
    prev = M.fw.sel;
    clear(), step(12, 1);
    CHECKF(M.fw.sel >= 0 && M.fw.sel != prev && E[M.fw.sel].installed && !count(RFM_MOVE_ACT_FW_LOAD), "sel %d", M.fw.sel);
    leds(w);
    CHECK(white(w[L_STEP0 + 12]));                      /* (pressed: white) */
    for (k = 0; k < 6; k++) {                           /* (the roll: a white pad on its way) */
        int i;
        adv_ms(30), leds(w);
        for (i = 0; i < 32; i++)
            rolled += white(w[L_PAD0 + i]);
    }
    CHECK(rolled > 0);
    step(12, 0), adv_ms(50);
    CHECKF(strstr(said, E[M.fw.sel].name) != NULL, "said %s, sel %s", said, E[M.fw.sel].name);
    write_pbm("n24_fw_random");
    for (k = 0; k < 20; k++) {
        prev = M.fw.sel;
        step(12, 1), step(12, 0);
        CHECK(M.fw.sel >= 0 && M.fw.sel != prev && E[M.fw.sel].installed);
    }
    CHECK(!count(RFM_MOVE_ACT_FW_LOAD));
    /* the full catalog: the ones not installed too */
    press(MR_CC_MUTE), rest();
    CHECK(M.fw.catalog);
    for (k = 0; k < 30; k++) {
        prev = M.fw.sel;
        step(12, 1), step(12, 0);
        CHECK(M.fw.sel >= 0 && M.fw.sel != prev);
        uninst += !E[M.fw.sel].installed;
    }
    CHECKF(uninst > 0 && !count(RFM_MOVE_ACT_FW_DOWNLOAD), "%d not installed", uninst);
    write_pbm("n24_fw_random_catalog");
    press(MR_CC_MUTE), rest();
    /* a list of one: nothing; the step dark */
    rfm_move_fw_list(&M, E, 1);
    rest(), leds(w);
    CHECK(!lit(w[L_STEP0 + 12]) && w[L_ICON0 + 12][0] == 127);
    prev = M.fw.sel;
    step(12, 1), step(12, 0);
    CHECK(M.fw.sel == prev && M.fw.pop != 0 && !strcmp(M.fw.pop_text, "Nothing to pick"));
    write_pbm("n24_fw_random_none");
    rfm_move_fw_list(&M, E, NFW);
    back(), rest();
}

/* item 16: the projects page's step 13: a new project in the first empty slot, the open one stored first, three
 * different installed firmwares at random, a random key; Undo: removed, the one before open again; no room: dark, a
 * press says so */
static int n24_installed(const char *id)
{
    int i;
    for (i = 0; i < NFW; i++)
        if (!strcmp(E[i].id, id))
            return E[i].installed && E[i].installed[0];
    return 0;
}
static void test_n24_proj_random(void)
{
    static rfm_move_fw_t E2[NFW];
    uint8_t w[RFM_MOVE_NLEDS][3];
    int i, first_fw = -1, save0 = -1, nfw = 0, root, scale;
    ps_two_projects();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    grid();
    M.ui_rng = 4242;
    leds(w);
    CHECK(w[L_ICON0 + 12][0] == 127 && lit(w[L_STEP0 + 12]));
    ps_clear(), step(12, 1);
    for (i = 0; i < npa; i++) {
        if (PA[i].kind == RFM_MOVE_ACT_PROJECT_SAVE && PA[i].index == 0 && save0 < 0)
            save0 = i;
        if (PA[i].kind == RFM_MOVE_ACT_PROJECT_FW && PA[i].id[0]) {
            if (first_fw < 0)
                first_fw = i;
            nfw++;
            CHECKF(n24_installed(PA[i].id) && PA[i].index == 1, "%s installed, project %d", PA[i].id, PA[i].index + 1);
        }
    }
    CHECKF(save0 >= 0 && first_fw > save0 && nfw == 3, "save %d, fw %d, %d firmwares", save0, first_fw, nfw);
    CHECK(strcmp(ps_find(RFM_MOVE_ACT_PROJECT_FW, 0)->id, ps_find(RFM_MOVE_ACT_PROJECT_FW, 1)->id) &&
          strcmp(ps_find(RFM_MOVE_ACT_PROJECT_FW, 0)->id, ps_find(RFM_MOVE_ACT_PROJECT_FW, 2)->id) &&
          strcmp(ps_find(RFM_MOVE_ACT_PROJECT_FW, 1)->id, ps_find(RFM_MOVE_ACT_PROJECT_FW, 2)->id));
    CHECK(M.seq.proj_cur == 1 && (M.seq.proj_used >> 1 & 1) && (M.seq.proj_used & 1) && M.seq.projects &&
          M.fw.track == 3 && rfm_move_proj_fw_loading(&M) && ps_find(RFM_MOVE_ACT_PROJECT_SAVE, -1)->index == 1);
    CHECK(M.seq.root < 12 && M.seq.scale < RFM_MOVE_NSCALES && !M.seq.nnotes && !M.seq.playing);
    CHECK(!strcmp((const char *)ps_part(1), ps_find(RFM_MOVE_ACT_PROJECT_FW, 0)->id));   /* (stored with them) */
    root = M.seq.root, scale = M.seq.scale;
    CHECKF(strstr(said, "New project 2") != NULL, "said %s", said);
    leds(w);
    CHECK(white(w[L_STEP0 + 12]));
    write_pbm("n24_proj_random_loading");
    step(12, 0);
    for (i = 0; i < 3; i++)
        rfm_move_proj_fw(&M, i, RFM_MOVE_PFW_READY);
    rest();
    CHECK(!rfm_move_proj_fw_loading(&M) && M.seq.proj_cur == 1);
    write_pbm("n24_proj_random");
    /* Undo: the new one removed, project 1 open again */
    leds(w);
    CHECK(w[L_WHITE0 + W_UNDO][0] == 127);
    ps_clear(), press(MR_CC_UNDO);
    CHECK(ps_count(RFM_MOVE_ACT_PROJECT_DELETE) == 1 && ps_find(RFM_MOVE_ACT_PROJECT_DELETE, -1)->index == 1 &&
          ps_count(RFM_MOVE_ACT_PROJECT_LOAD) == 1 && ps_find(RFM_MOVE_ACT_PROJECT_LOAD, -1)->index == 0 &&
          !(M.seq.proj_used >> 1 & 1) && !ps_count(RFM_MOVE_ACT_PROJECT_SAVE));
    rfm_move_project_load(&M, 0, PS_BLOB[0], PS_N[0]), rest();
    CHECK(M.seq.proj_cur == 0);
    /* again: another key (the dice go on) */
    ps_clear(), step(12, 1), step(12, 0);
    CHECK(M.seq.proj_cur == 1 && (M.seq.root != root || M.seq.scale != scale));
    for (i = 0; i < 3; i++)
        rfm_move_proj_fw(&M, i, RFM_MOVE_PFW_READY);
    rest();
    /* one firmware installed: one asked for, the other slots empty */
    memcpy(E2, E, sizeof E2);
    for (i = 0, nfw = 0; i < NFW; i++)
        if (E2[i].installed && E2[i].installed[0] && nfw++)
            E2[i].installed = NULL;
    rfm_move_fw_list(&M, E2, NFW);
    ps_clear(), step(12, 1), step(12, 0);
    CHECK(M.seq.proj_cur == 2 && ps_count(RFM_MOVE_ACT_PROJECT_FW) == 3 && ps_find(RFM_MOVE_ACT_PROJECT_FW, 0)->id[0] &&
          !ps_find(RFM_MOVE_ACT_PROJECT_FW, 1)->id[0] && !ps_find(RFM_MOVE_ACT_PROJECT_FW, 2)->id[0]);
    rfm_move_proj_fw(&M, 0, RFM_MOVE_PFW_READY), rest();
    rfm_move_fw_list(&M, E, NFW);
    /* every slot used: dark, a press says so and does nothing */
    rfm_move_project_dir(&M, 0xFFFFFFFFu), rest();
    leds(w);
    CHECK(w[L_ICON0 + 12][0] == 0 && !lit(w[L_STEP0 + 12]));
    ps_clear(), step(12, 1), step(12, 0);
    CHECK(!npa && M.pop == POP_HOST && !strcmp(M.pop_text, "No empty slot") && M.seq.proj_cur == 2);
    write_pbm("n24_proj_random_full");
    back(), rest();
}

int main(void)
{
    test_n24_proj_random();
    test_n24_fw_random();
    test_n24_project_new();
    test_n24_load_stopped();
    test_n24_arp_down();
    test_n24_arp_lens();
    test_n24_arp_random();
    test_n24_arp_reset();
    test_n24_arp_rate();
    test_n24_arp_clip();
    test_n24_shift_knob();
    test_n24_settings_ticks();
    test_n24_knob_lock();
    test_n24_fw_play();
    test_n24_back_play();
    test_n24_track_leds();
    test_o14_clear_clip();
    test_o14_projects();
    test_o14_shift_knob();
    test_o14_fwp_jog();
    test_o14_shift_track();
    test_o14_state_pad();
    test_o14_states_project();
    test_o14_states_old_host();
    test_o14_kpage();
    test_o14_arp_notes();
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
    test_fwp_tkclick();
    test_fwp_page();
    test_fwp_trackflash();
    test_x_gone_dbl_stop();
    test_loop_steps();
    test_mj_menus();
    test_mj_places();
    test_mj_page_len();
    test_loop_dbl();
    test_empty_load_defaults();
    (void)count, (void)find, (void)knob, (void)jog, (void)jog_click, (void)dbl_track, (void)m_nlat;
    return check_done("move_sessview");
}
