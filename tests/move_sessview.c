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

/* ================================================================ 1. scenes === */
static void test_scenes(void)
{
    int t;
    session_view();
    owners_grid();
    /* stopped: Shift + a clip of row 2: that scene the tracks' clips at once (they play with Play); the tracks with
     * no clip there play nothing */
    button(MR_CC_SHIFT, 1), adv_ms(150), hit(PAD(0, 1)), button(MR_CC_SHIFT, 0);
    CHECK(M.seq.tr[0].active == 1 && M.seq.tr[1].active == 1 && M.seq.tr[2].active == -1 && M.seq.tr[3].active == 1);
    CHECK(!M.seq.playing && !M.seq.start_pending);
    press(MR_CC_PLAY), adv_ms(300);
    CHECK(M.seq.playing && M.seq.tr[0].active == 1);
    /* playing: Shift + a clip of row 1: queued (the pads green), launched at the bar; again with a held Shift and
     * a slow press, as a hand does it */
    rest(), bar_start();
    button(MR_CC_SHIFT, 1), adv_ms(400), pad(PAD(2, 0), 100), adv_ms(180), pad(PAD(2, 0), 0), adv_ms(200), button(MR_CC_SHIFT, 0);
    for (t = 0; t < 4; t++)
        CHECKF(M.seq.tr[t].queued == 0, "track %d queued %d", t + 1, M.seq.tr[t].queued);
    {
        uint8_t w[RFM_MOVE_NLEDS][3];
        leds(w);
        CHECK(green(w[L_PAD0 + PAD(0, 0)]) && green(w[L_PAD0 + PAD(3, 0)]));
    }
    adv_ms(2100);
    for (t = 0; t < 4; t++)
        CHECKF(M.seq.tr[t].active == 0 && M.seq.tr[t].queued == SEQ_NONE, "track %d active %d", t + 1, M.seq.tr[t].active);
    /* row 3: tracks 1 and 3 have a clip there, 2 and 4 stop at the bar */
    bar_start();
    button(MR_CC_SHIFT, 1), hit(PAD(0, 2)), button(MR_CC_SHIFT, 0);
    CHECK(M.seq.tr[0].queued == 2 && M.seq.tr[1].queued == SEQ_STOP && M.seq.tr[2].queued == 2 && M.seq.tr[3].queued == SEQ_STOP);
    adv_ms(2100);
    CHECK(M.seq.tr[0].active == 2 && M.seq.tr[1].active == -1 && M.seq.tr[2].active == 2 && M.seq.tr[3].active == -1);
    CHECK(M.seq.playing);
}

/* ================================================================ 2. the pressed clip shown as selected === */
static void test_selected(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    session_view();
    owners_grid();
    /* at rest: track 2's clip 1 (the slot's track) is the selected one: its pad white, its cell framed, no other */
    leds(w), screen();
    write_pbm("rest");
    CHECK(white(w[L_PAD0 + PAD(1, 0)]) && cell_framed(1, 0) && nframed() == 1);
    /* stopped: a clip pressed: it is the track's clip and the selected one, on the pad and on the screen */
    hit(PAD(2, 2));
    leds(w), screen();
    write_pbm("selected");
    CHECK(M.seq.sel_track == 2 && M.seq.tr[2].sel == 2 && M.seq.tr[2].active == 2);
    CHECK(white(w[L_PAD0 + PAD(2, 2)]) && !white(w[L_PAD0 + PAD(1, 0)]) && lit(w[L_PAD0 + PAD(1, 0)]));
    CHECK(cell_framed(2, 2) && !cell_framed(1, 0) && nframed() == 1 && cell_clip(2, 2));
    CHECK(px(2 * 16 + 1, 0) && !px(1 * 16 + 1, 0));     /* (the bar over the selected track's column) */
    /* an empty slot pressed: a new clip there, selected the same way */
    now_ms += 1000, hit(PAD(5, 1));
    leds(w), screen();
    CHECK(M.seq.sel_track == 5 && M.seq.tr[5].sel == 1 && clip_has(5, 1));
    CHECK(white(w[L_PAD0 + PAD(5, 1)]) && cell_framed(5, 1) && cell_clip(5, 1) && nframed() == 1);
    /* playing: a clip pressed is selected at once on the screen (framed, its doubled outline while it waits) and
     * its pad blinks green; at the bar it plays: the pad white (pulsing), the cell filled inside its frame */
    press(MR_CC_PLAY), adv_ms(700), bar_start();
    now_ms += 1000, pad(PAD(0, 1), 100), adv_ms(150), pad(PAD(0, 1), 0), adv_ms(60);
    leds(w), screen();
    write_pbm("queued");
    CHECK(M.seq.sel_track == 0 && M.seq.tr[0].sel == 1 && M.seq.tr[0].queued == 1 && M.seq.tr[0].active == 0);
    CHECK(cell_framed(0, 1) && nframed() == 1);
    CHECK(green(w[L_PAD0 + PAD(0, 1)]));
    adv_ms(2100);
    leds(w), screen();
    write_pbm("playing");
    CHECK(M.seq.tr[0].active == 1 && M.seq.tr[0].queued == SEQ_NONE);
    CHECK(white(w[L_PAD0 + PAD(0, 1)]) && cell_framed(0, 1) && nframed() == 1);
    CHECK(!px(0 * 16 + 1, 4 + 15 + 6) && px(0 * 16 + 2, 4 + 15 + 6));   /* (filled, clear of its frame) */
    CHECK(lit(w[L_PAD0 + PAD(0, 0)]) && !white(w[L_PAD0 + PAD(0, 0)]));  /* (the clip it replaced: its track's colour) */
    /* the playing clip pressed again: it plays on, selected */
    now_ms += 1000, hit(PAD(0, 1)), adv_ms(100);
    CHECK(M.seq.tr[0].active == 1 && M.seq.tr[0].queued == SEQ_NONE && M.seq.sel_track == 0);
}

/* ================================================================ 3. the screen and the pads === */
static void aligned(const char *what)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int c, r;
    leds(w), screen();
    for (r = 0; r < 4; r++)
        for (c = 0; c < 8; c++) {
            const int track = M.seq.track_page + c, scene = M.seq.scene_off + r, has = clip_has(track, scene);
            const int sel = track == M.seq.sel_track && M.seq.tr[track].sel == scene;
            CHECKF(cell_clip(c, r) == has && cell_dot(c, r) == !has, "%s: the screen's column %d row %d (track %d clip %d: %d)", what,
                   c + 1, r + 1, track + 1, scene + 1, has);
            CHECKF(lit(w[L_PAD0 + PAD(c, r)]) == (has || sel), "%s: the pad of column %d row %d (track %d clip %d: %d)", what,
                   c + 1, r + 1, track + 1, scene + 1, has);
            CHECKF(cell_framed(c, r) == sel, "%s: the frame at column %d row %d", what, c + 1, r + 1);
        }
}
static void test_aligned(void)
{
    int i;
    session_view();
    owners_grid();
    clip_put(9, 6, 70), clip_put(15, 15, 71), clip_put(7, 3, 72);
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
    CHECK(M.seq.track_page == 8 && M.seq.scene_off == 12);
    aligned("the far corner");
    now_ms += 1000, hit(PAD(7, 3));                     /* (the bottom-right pad: track 16, clip 16) */
    CHECK(M.seq.sel_track == 15 && M.seq.tr[15].sel == 15);
    aligned("the far corner, its clip pressed");
    /* the knobs over the columns: a touch selects that column's track (its selected clip framed) */
    raw(0x90, 3, 127), raw(0x80, 3, 0);
    CHECK(M.seq.sel_track == 8 + 3);
    aligned("a knob touched");
    /* a slot's Track tapped: its track view again (session view is the Menu button's) */
    tap_track(1), rest();
    CHECK(!M.seq.view && M.seq.sel_track == 1);
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
    clip_put(7, 0, 71);
    button(MR_CC_COPY, 1), hit(PAD(0, 0)), button(MR_CC_COPY, 0);
    CHECK(M.seq.paste == 1 && rfmv_seq_src_clip(&M, SEQ_CLIP(0, 0)) == 1);
    clear(), now_ms += 400, hit(PAD(7, 0));
    CHECK(clip_note(7, 0, 60) && !clip_note(7, 0, 71) && !M.seq.paste);
    CHECKF(strstr(said, "Track 8 clip 1 replaced") != NULL, "said \"%s\"", said);
    press(MR_CC_UNDO);
    CHECK(clip_note(7, 0, 71) && !clip_note(7, 0, 60));
    now_ms += 1000, hit(PAD(7, 0));
    CHECK(clip_note(7, 0, 71) && M.seq.sel_track == 7 && M.seq.tr[7].active == 0);
    /* Back drops a waiting copy's mark (a layer), then leaves session view */
    button(MR_CC_COPY, 1), hit(PAD(0, 0)), button(MR_CC_COPY, 0);
    back();
    CHECK(!M.seq.paste && M.seq.view == 1);
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
    press(MR_CC_PLAY), adv_ms(500);
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
    button(MR_CC_SHIFT, 1), hit(PAD(1, 0)), button(MR_CC_SHIFT, 0);
    adv_ms(2100);
    CHECK(M.seq.tr[0].active == 0 && M.seq.tr[1].active == 0 && M.seq.tr[2].active == 0 && M.seq.tr[3].active == 0);
}

int main(void)
{
    test_scenes();
    test_selected();
    test_aligned();
    test_cut_copy();
    test_led_writes();
    test_from_takeover();
    (void)count, (void)find, (void)step, (void)knob, (void)jog, (void)jog_click, (void)dbl_track, (void)lit_rows, (void)bright, (void)m_nlat;
    return check_done("move_sessview");
}
