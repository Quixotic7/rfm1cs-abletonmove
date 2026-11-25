/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (0.15.0, the owner's 2026-11-24) the sampler app (RFM_MOVE_CAP_SAMPLER; README "The sampler"): the capability off
 * (Sample's audio loop as before), the SETs at the capability, opening and closing, the record key, the rows on the
 * jog (one row, three rows a second, the tabs above), a row's popup, the tabs' click, Track held + Sample (the source),
 * Back, a take running (armed, recording, the app closed and back), the host's events (saved, captured, nothing, an
 * error), the lights, what passes to the layer under it (pads, knobs, steps, arrows), the FM-1 layout (Sample stays
 * SEL), the projects view (the favourite key), a takeover (its claim keeps Sample; hidden while the app is open), a
 * Shift menu under it, a knob's popup over it, the host's offer, the settings blob (version 8, a version 7 one), the
 * speech, and the screens against the mock-up's boards (move_smp_ref.h). Writes sampler_*.pbm / .leds (the screens,
 * the LEDs) into the working directory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "move_int.h"
#include "moveremote/client.h"
#include "rfm1cs/move.h"
#include "move_fwb_data.h"
#include "move_smp_ref.h"
#include "move_smp_ref2.h"

#define NFW ((int)(sizeof FWB / sizeof FWB[0]))

/* ---- the actions, logged */
static rfm_move_act_t A[1024];
static int nact, served;
static char said[8192];
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
static void clear(void) { nact = 0, served = 0, said[0] = 0; }
static int count(int kind)
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += A[i].kind == kind;
    return n;
}
static const rfm_move_act_t *last(int kind)
{
    int i;
    for (i = nact - 1; i >= 0; i--)
        if (A[i].kind == kind)
            return &A[i];
    return NULL;
}
static int smp_acts(void) { return count(RFM_MOVE_ACT_SMP_SET) + count(RFM_MOVE_ACT_SMP_PRESS); }

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
static void touch(int k, int on) { raw(on ? 0x90 : 0x80, (uint8_t)k, on ? 127 : 0); }
static void jog(int d) { raw(0xB0, MR_CC_JOG, (uint8_t)(d & 127)); }
static void jog_click(void) { raw(0xB0, MR_CC_JOG_CLICK, 127), raw(0xB0, MR_CC_JOG_CLICK, 0); }
static void track(int t, int down) { button(MR_CC_TRACK1 - t, down); }
static void tap_track(int t) { track(t, 1), track(t, 0); }
static void hold_track(int t) { track(t, 1), adv_ms(RFM_MOVE_TAP_MS + 20); }
static void rest(void) { adv_ms(RFM_MOVE_POPUP_MS + 50), now_ms += RFM_MOVE_DOUBLE_MS; }
static void back(void) { press(MR_CC_BACK); }
static void sample(void) { press(MR_CC_SAMPLE); }

static rfm_move_fw_t E[NFW];
static void fresh_caps(int caps)
{
    static const char *const ids[RFM_MOVE_FW_SLOTS] = { "choralroot", "felucca", "" };
    int i;
    for (i = 0; i < NFW; i++) {
        E[i].id = FWB[i].id, E[i].name = FWB[i].name, E[i].version = FWB[i].version, E[i].installed = FWB[i].installed;
        E[i].desc = FWB[i].desc, E[i].thumb = FWB[i].thumb, E[i].cats = FWB[i].cats, E[i].cat = FWB[i].cat;
    }
    now_ms = 1000;
    rfm_move_init(&M, NULL, on_act, NULL);
    rfm_move_fw_list(&M, E, NFW);
    rfm_move_fw_slots(&M, ids);
    rfm_move_fw_caps(&M, caps);
    rest();
    clear();
}
static void fresh(void) { fresh_caps(RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_SAMPLER); }
/* a note layout on slot 2 (felucca: the slot played), its track view, speech on */
static void note_view_caps(int caps)
{
    fresh_caps(caps);
    hold_track(1), hit(26), track(1, 0);                /* (Track 2 held + the top row's pad 3: a note layout) */
    rest(), clear();
    tap_track(1), rest();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.fw.active == 1 && !M.seq.view);
    rest(), clear();
}
static void note_view(void) { note_view_caps(RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_SAMPLER); }

/* ---- the host's status */
static rfm_move_smp_status_t ST;
static void status(int state)
{
    ST.state = (uint8_t)state;
    rfm_move_smp_status(&M, &ST);
}

/* ---- the screen and the LEDs */
static uint8_t SCR[MR_SCREEN_BYTES];
static void screen(void) { rfm_move_screen(&M, NULL, SCR); }
static int px(int x, int y) { return SCR[(y >> 3) * 128 + x] >> (y & 7) & 1; }
static int pixels(int x0, int y0, int w, int h)
{
    int x, y, n = 0;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            n += px(x, y);
    return n;
}
static void leds(uint8_t w[RFM_MOVE_NLEDS][3])
{
    rfm_move_led_t o[RFM_MOVE_NLEDS];
    int i, n;
    memset(w, 0, RFM_MOVE_NLEDS * 3);
    rfm_move_leds_invalidate(&M);
    n = rfm_move_leds(&M, o, RFM_MOVE_NLEDS);
    for (i = 0; i < n; i++)
        w[i][0] = o[i].r, w[i][1] = o[i].g, w[i][2] = o[i].b;
}
static void sample_led(uint8_t c[3])
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    leds(w);
    memcpy(c, w[L_SAMPLE], 3);
}
static int is_rgb(const uint8_t c[3], int r, int g, int b) { return c[0] == r && c[1] == g && c[2] == b; }
/* the screen and the LEDs as the other tests dump them (move_engine's .leds) and one more row, the buttons as the
 * mock-up writes them: Sample, Record, Play (RGB), Loop, Mute, Delete, Copy, Undo (white: the level as grey) */
static void dump(const char *name)
{
    char path[96];
    FILE *f;
    int x, y, c, r;
    uint8_t w[RFM_MOVE_NLEDS][3];
    static const int WB[5] = { W_LOOP, W_MUTE, W_DELETE, W_COPY, W_UNDO };
    screen();
    snprintf(path, sizeof path, "sampler_%s.pbm", name);
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "P1\n128 64\n");
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++)
            fputc(px(x, y) ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
    leds(w);
    snprintf(path, sizeof path, "sampler_%s.leds", name);
    if (!(f = fopen(path, "w")))
        return;
    for (r = 3; r >= 0; r--)
        for (c = 0; c < 8; c++)
            fprintf(f, "%02x%02x%02x%c", w[L_PAD0 + r * 8 + c][0], w[L_PAD0 + r * 8 + c][1], w[L_PAD0 + r * 8 + c][2], c == 7 ? '\n' : ' ');
    for (c = 0; c < 16; c++)
        fprintf(f, "%02x%02x%02x%c", w[L_STEP0 + c][0], w[L_STEP0 + c][1], w[L_STEP0 + c][2], c == 15 ? '\n' : ' ');
    for (c = 0; c < 8; c++)
        fprintf(f, "%02x%02x%02x%c", w[L_RING0 + c][0], w[L_RING0 + c][1], w[L_RING0 + c][2], c == 7 ? '\n' : ' ');
    for (c = 0; c < 16; c++)
        fprintf(f, "%02x%c", w[L_ICON0 + c][0], c == 15 ? '\n' : ' ');
    for (c = 0; c < 4; c++)
        fprintf(f, "%02x%02x%02x%c", w[L_TRACK0 + c][0], w[L_TRACK0 + c][1], w[L_TRACK0 + c][2], c == 3 ? '\n' : ' ');
    fprintf(f, "%02x%02x%02x %02x%02x%02x %02x%02x%02x", w[L_SAMPLE][0], w[L_SAMPLE][1], w[L_SAMPLE][2], w[L_REC][0],
            w[L_REC][1], w[L_REC][2], w[L_PLAY][0], w[L_PLAY][1], w[L_PLAY][2]);
    for (c = 0; c < 5; c++) {
        const int v = w[L_WHITE0 + WB[c]][0] * 2 > 255 ? 255 : w[L_WHITE0 + WB[c]][0] * 2;
        fprintf(f, " %02x%02x%02x", v, v, v);
    }
    fputc('\n', f);
    fclose(f);
}

/* ---- the mock-up's boards */
static const smp_ref_t *ref(const char *name)
{
    int i;
    for (i = 0; i < (int)(sizeof SMP_REF / sizeof SMP_REF[0]); i++)
        if (!strcmp(SMP_REF[i].name, name))
            return &SMP_REF[i];
    for (i = 0; i < (int)(sizeof SMP_REF2 / sizeof SMP_REF2[0]); i++)   /* (stage 2's) */
        if (!strcmp(SMP_REF2[i].name, name))
            return &SMP_REF2[i];
    return NULL;
}
typedef struct { int x, y, w, h; } box_t;
/* the screen against a board: pixels that differ outside the boxes (0: the same), and inside them (*in) */
static int vs_ref(const char *name, const box_t *ex, int nex, int *in)
{
    const smp_ref_t *r = ref(name);
    int x, y, out = 0, i;
    *in = 0;
    screen();
    for (y = 0; y < 64; y++)
        for (x = 0; x < 128; x++) {
            const int want = r->px[y][x >> 3] >> (7 - (x & 7)) & 1;
            int masked = 0;
            for (i = 0; i < nex; i++)
                masked |= x >= ex[i].x && x < ex[i].x + ex[i].w && y >= ex[i].y && y < ex[i].y + ex[i].h;
            if (want != px(x, y)) {
                if (masked)
                    (*in)++;
                else {
                    if (out < 8)
                        fprintf(stderr, "  %s: (%d, %d) mock %d, core %d\n", name, x, y, want, px(x, y));
                    out++;
                }
            }
        }
    return out;
}
/* the mock-up's wave (mock_recorder_v4.c wave()): its column heights; the peaks that draw them here */
static int mock_wave(int w, int h, int upto, unsigned seed, int a[64])
{
    int x, n = 0;
    for (x = 0; x < w; x += 2) {
        const int beat = (x * 16 / w) % 4;
        seed = seed * 1103515245u + 12345u;
        a[n] = 1 + (int)((seed >> 16) % (unsigned)h) * (beat == 0 ? 4 : beat == 2 ? 3 : 2) / 4;
        if (x >= upto)
            a[n] = 0;
        n++;
    }
    return n;
}
static uint8_t peak_for(int a, int h)
{
    int p;
    for (p = 0; p < 256; p++)
        if ((p * h + 127) / 255 == a)
            return (uint8_t)p;
    return 0;
}
static void feed_wave(int h, int upto, unsigned seed, int span)
{
    int a[64], n = mock_wave(116, h, upto, seed, a), i;
    ST.npeaks = 0, ST.span = (uint8_t)span;
    for (i = 0; i < n && a[i]; i++)
        ST.peaks[ST.npeaks++] = peak_for(a[i], h);
    if (!span)
        ST.npeaks = (uint8_t)n;                         /* (a history: all of it) */
}
static int level_for(int px_, int w)                    /* a level drawn px_ wide on a w-pixel meter */
{
    int l;
    for (l = 1; l <= 32767; l++)
        if (rfmv_smp_lvpx(l, w) == px_)
            return l;
    return -1;
}

/* ================================================================ the tests === */
/* 1. the capability off: Sample's audio loop as before; the API does nothing */
static void t_cap_off(void)
{
    note_view_caps(RFM_MOVE_CAPS_ALL);
    sample();
    CHECK(M.seq.arec == 1 && !M.smp.open && !smp_acts());
    sample();
    CHECK(M.seq.arec == 0);
    memset(&ST, 0, sizeof ST);
    CHECK(rfm_move_smp_status(&M, &ST) == 0 && rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_SAVED, 4, 1200, 8000, NULL) == 0);
    rfm_move_smp_sync(&M);
    CHECK(!smp_acts() && !rfm_move_smp_shown(&M));
    {
        uint8_t c[3];
        sample_led(c);
        CHECK(is_rgb(c, 39, 39, 39));                   /* (the sequencer's own Sample light, as before) */
    }
}

/* 2. the capability: every setting once, the defaults; the old loop off */
static void t_caps_sync(void)
{
    static const int want[RFM_MOVE_SMP_N] = { RFM_MOVE_SMP_SRC_MASTER, RFM_MOVE_SMP_M_BARS, 0, -24, 0, 0, RFM_MOVE_SMP_TO_FILES };
    int k, ok = 1;
    fresh_caps(RFM_MOVE_CAPS_ALL);
    M.seq.arec = 1;
    rfm_move_fw_caps(&M, RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_SAMPLER);
    CHECK(count(RFM_MOVE_ACT_SMP_SET) == RFM_MOVE_SMP_N && M.seq.arec == 0);
    for (k = 0; k < RFM_MOVE_SMP_N; k++)
        ok &= A[k].kind == RFM_MOVE_ACT_SMP_SET && A[k].index == k && A[k].value == want[k];
    CHECK(ok);
    clear();
    rfm_move_fw_caps(&M, RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_SAMPLER | RFM_MOVE_CAP_PROJFW);   /* (already on: no SETs) */
    CHECK(!smp_acts() && (rfm_move_fw_caps_get(&M) & RFM_MOVE_CAP_SAMPLER));
    rfm_move_smp_sync(&M);
    CHECK(count(RFM_MOVE_ACT_SMP_SET) == RFM_MOVE_SMP_N);
    CHECK(rfm_move_smp_value(&M, RFM_MOVE_SMP_THRESHOLD) == -24 && rfm_move_smp_value(&M, 99) == -1);
}

/* 3. open, the record key, Back closes; the light; Back's light */
static void t_open(void)
{
    uint8_t c[3], w[RFM_MOVE_NLEDS][3];
    note_view();
    sample_led(c);
    CHECK(is_rgb(c, 11, 11, 11));                       /* (closed, idle: faint) */
    leds(w);
    CHECK(w[L_WHITE0 + W_BACK][0] == 0);
    button(MR_CC_SAMPLE, 1);
    CHECK(M.smp.open && rfm_move_smp_shown(&M) && !smp_acts());   /* (opened at the press; nothing recorded) */
    sample_led(c);
    CHECK(is_rgb(c, 255, 255, 255));
    leds(w);
    CHECK(w[L_WHITE0 + W_BACK][0] == 80);               /* (Back has the app to close) */
    button(MR_CC_SAMPLE, 0);
    CHECK(!smp_acts() && M.seq.arec == 0);
    dump("open");
    button(MR_CC_SAMPLE, 1);                            /* the record key: at the press */
    CHECK(count(RFM_MOVE_ACT_SMP_PRESS) == 1 && last(RFM_MOVE_ACT_SMP_PRESS)->value == RFM_MOVE_SMP_M_BARS &&
          last(RFM_MOVE_ACT_SMP_PRESS)->down == 1);
    button(MR_CC_SAMPLE, 0);
    CHECK(count(RFM_MOVE_ACT_SMP_PRESS) == 1 && M.seq.arec == 0);
    back();
    CHECK(!M.smp.open && !rfm_move_smp_shown(&M) && M.s.mode == RFM_MOVE_MODE_SEQ);
    sample_led(c);
    CHECK(is_rgb(c, 11, 11, 11));
}

/* 4. the rows: one at rest, three a second while the jog turns, the tabs above the first */
static void t_rows(void)
{
    note_view();
    sample();
    CHECK(M.smp.row == RFM_MOVE_SMP_SOURCE && !M.smp.on_tabs);
    screen();
    CHECK(pixels(0, 49, 128, 15) > 1000);               /* (one row, chosen: white) */
    jog(1);
    CHECK(M.smp.row == RFM_MOVE_SMP_MODE && M.smp.turned);
    screen();                                           /* three rows: Source, Mode (chosen, the middle), Length */
    CHECK(pixels(0, 32, 128, 15) > 1000 && pixels(0, 16, 128, 15) < 1000 && pixels(0, 48, 128, 15) < 1000);
    CHECK(rfm_move_screen_timed(&M));
    dump("three_rows");
    adv_ms(RFM_MOVE_POPUP_MS - 100);
    screen();
    CHECK(pixels(0, 32, 128, 15) > 1000);               /* (still three) */
    adv_ms(200);
    screen();
    CHECK(pixels(0, 49, 128, 15) > 1000 && pixels(0, 32, 128, 1) < 60);   /* (back to one: the wave's place) */
    jog(5);
    CHECK(M.smp.row == RFM_MOVE_SMP_TO);
    jog(3);
    CHECK(M.smp.row == RFM_MOVE_SMP_TO);                /* (the last row: no wrap) */
    screen();                                           /* (the last row at the bottom of three) */
    CHECK(pixels(0, 48, 128, 15) > 1000);
    jog(-6);
    CHECK(M.smp.row == RFM_MOVE_SMP_SOURCE && !M.smp.on_tabs);
    screen();                                           /* (the first row at the top of three) */
    CHECK(pixels(0, 16, 128, 15) > 1000);
    jog(-1);
    CHECK(M.smp.on_tabs && M.smp.row == RFM_MOVE_SMP_SOURCE);
    screen();
    CHECK(pixels(0, 0, 15, 14) > 120 && pixels(0, 49, 128, 15) < 400);   /* (the tabs: Record a block; the row plain) */
    jog(-3);
    CHECK(M.smp.on_tabs);
    jog(1);
    CHECK(!M.smp.on_tabs && M.smp.row == RFM_MOVE_SMP_SOURCE);
    jog(-1), jog(2);                                    /* (from the tabs, two down: the second row) */
    CHECK(!M.smp.on_tabs && M.smp.row == RFM_MOVE_SMP_MODE);
    CHECK(!count(RFM_MOVE_ACT_SMP_SET) && !count(RFM_MOVE_ACT_MIDI));   /* (moving changes nothing) */
}

/* 5. a row's click: its popup; the jog changes the value (heard at once); a click or Back ends it */
static void t_popup(void)
{
    char w[64];
    note_view();
    sample();
    jog(3);                                             /* Threshold */
    CHECK(M.smp.row == RFM_MOVE_SMP_THRESHOLD);
    jog_click();
    CHECK(M.smp.edit);
    clear();
    jog(-2);
    CHECK(count(RFM_MOVE_ACT_SMP_SET) == 1 && last(RFM_MOVE_ACT_SMP_SET)->index == RFM_MOVE_SMP_THRESHOLD &&
          last(RFM_MOVE_ACT_SMP_SET)->value == -26 && count(RFM_MOVE_ACT_SETTINGS) == 1 && M.s.smp[3] == -26);
    jog(1);
    CHECK(count(RFM_MOVE_ACT_SMP_SET) == 2 && last(RFM_MOVE_ACT_SMP_SET)->value == -25);
    jog(60);
    CHECK(M.s.smp[3] == 0 && last(RFM_MOVE_ACT_SMP_SET)->value == 0);
    jog(1);
    CHECK(count(RFM_MOVE_ACT_SMP_SET) == 3);            /* (at the end: nothing more) */
    rfm_move_words(&M, w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Threshold: 0 dB"), "%s", w);
    jog_click();
    CHECK(!M.smp.edit && M.smp.open);
    jog(-1), jog_click();                               /* Length's popup: Open, 1 Bar .. */
    CHECK(M.smp.row == RFM_MOVE_SMP_LENGTH && M.smp.edit);
    clear();
    jog(3);
    CHECK(last(RFM_MOVE_ACT_SMP_SET)->index == RFM_MOVE_SMP_LENGTH && last(RFM_MOVE_ACT_SMP_SET)->value == 4);
    dump("popup_length");
    back();                                             /* Back: the popup ends, the app stays */
    CHECK(!M.smp.edit && M.smp.open);
    jog(-2), jog_click();                               /* Source's popup: Master .. Stems (offered) */
    clear();
    jog(10);
    CHECKF(M.s.smp[0] == RFM_MOVE_SMP_SRC_STEMS && count(RFM_MOVE_ACT_SMP_SET) == 1, "src %d sets %d", M.s.smp[0], count(RFM_MOVE_ACT_SMP_SET));   /* (one turn: one SET, its last value) */
    jog(-1);
    CHECK(M.s.smp[0] == RFM_MOVE_SMP_SRC_INPUT);
    dump("popup_source");
    back(), back();
    CHECK(!M.smp.open);
}

/* 6. the tabs: their click, the jog picks one, a click or Back ends it; a host that offers only Record: one tab */
static void t_tabs(void)
{
    note_view();
    sample();
    jog(-1), jog_click(), jog(1);                       /* (the host offers Record only: one tab, nothing to pick) */
    CHECK(M.smp.picking && M.smp.tab == RFM_MOVE_SMP_TAB_RECORD);
    screen();
    CHECK(pixels(18, 0, 16, 14) == 0);                  /* (one icon: Takes not offered, not shown) */
    jog_click(), back();
    rfm_move_smp_offer(&M, 0, 0, 0xF, 0);               /* (all four offered: Record, Takes, Tools built; not Loops) */
    sample();
    jog(-1);
    CHECK(M.smp.on_tabs);
    jog_click();
    CHECK(M.smp.picking && M.smp.tab == RFM_MOVE_SMP_TAB_RECORD);
    screen();
    CHECK(pixels(18, 0, 16, 14) > 20 && pixels(36, 0, 16, 14) > 20 && pixels(54, 0, 16, 14) == 0);   /* (three) */
    dump("tabs3");
    jog(1);
    CHECK(M.smp.picking && M.smp.tab == RFM_MOVE_SMP_TAB_TAKES);
    jog(5);
    CHECK(M.smp.tab == RFM_MOVE_SMP_TAB_TOOLS);         /* (Loops not built: Tools the last) */
    jog(-3);
    CHECK(M.smp.picking && M.smp.tab == RFM_MOVE_SMP_TAB_RECORD && M.smp.open);
    jog(1);
    jog_click();
    CHECK(!M.smp.picking && M.smp.on_tabs && M.smp.tab == RFM_MOVE_SMP_TAB_TAKES);
    jog_click(), back();
    CHECK(!M.smp.picking && M.smp.open);
    back();
    CHECK(!M.smp.open);
    sample();                                           /* the tab and the row remembered */
    CHECK(M.smp.open && M.smp.tab == RFM_MOVE_SMP_TAB_TAKES && M.smp.on_tabs);
    back();
    rfm_move_smp_offer(&M, 0, 0, 1, 0);                 /* (Takes no longer offered: Record) */
    CHECK(M.smp.tab == RFM_MOVE_SMP_TAB_RECORD);
}

/* 7. Track held + Sample: the source (Track 1..3 its slot, Track 4 the master), nothing recorded, the Track used */
static void t_track_source(void)
{
    note_view();
    track(2, 1);
    button(MR_CC_SAMPLE, 1);
    CHECK(M.smp.open && M.s.smp[0] == RFM_MOVE_SMP_SRC_SLOT3 && M.smp.row == RFM_MOVE_SMP_SOURCE &&
          !count(RFM_MOVE_ACT_SMP_PRESS) && last(RFM_MOVE_ACT_SMP_SET)->value == RFM_MOVE_SMP_SRC_SLOT3);
    CHECK(rfm_move_smp_shown(&M));                      /* (the Track's hold used up: the app shows at once) */
    button(MR_CC_SAMPLE, 0), track(2, 0);
    CHECK(M.fw.active == 1 && M.s.mode == RFM_MOVE_MODE_SEQ);   /* (its release: no tap, the slot stays) */
    screen();
    CHECK(pixels(110, 0, 18, 15) > 10);                 /* (the sign: 3) */
    dump("track3");
    hold_track(3);
    clear();
    sample();                                           /* (open: Track 4 held + Sample: the master) */
    track(3, 0);
    CHECK(M.s.smp[0] == RFM_MOVE_SMP_SRC_MASTER && !count(RFM_MOVE_ACT_SMP_PRESS) && M.smp.open);
    back();
    hold_track(0), sample(), track(0, 0);               /* (closed: opened with slot 1) */
    CHECK(M.smp.open && M.s.smp[0] == RFM_MOVE_SMP_SRC_SLOT1 && M.fw.active == 1);
}

/* 8. what the app does not take stays the layer's under it: pads, knobs, steps, arrows (the same actions, the same
 * state), and the jog does not reach it */
static int log_kinds(char *o, int cap)
{
    int i, n = 0;
    o[0] = 0;
    for (i = 0; i < nact; i++)
        if (!(A[i].kind >= RFM_MOVE_ACT_SMP_SET && A[i].kind <= RFM_MOVE_ACT_SMP_UNDO) && A[i].kind != RFM_MOVE_ACT_SETTINGS)
            n += snprintf(o + strlen(o), (size_t)(cap - (int)strlen(o)), "%d/%d/%d/%d/%d.", A[i].kind, A[i].index,
                          A[i].bytes[0], A[i].bytes[1], A[i].value);
    return n;
}
static void layer_gestures(void)
{
    hit(10);
    knob(0, 3);
    step(4, 1), step(4, 0);
    press(MR_CC_RIGHT);
    pad(5, 90), adv_ms(50), pad(5, 0);
}
static void t_layer(void)
{
    static char a[4096], b[4096];
    int notes_a, page_a, notes_b, page_b;
    note_view();
    layer_gestures();
    log_kinds(a, sizeof a);
    notes_a = M.seq.nnotes, page_a = M.seq.page;
    note_view();
    sample();
    clear();
    layer_gestures();
    log_kinds(b, sizeof b);
    notes_b = M.seq.nnotes, page_b = M.seq.page;
    CHECKF(!strcmp(a, b) && a[0], "closed %s / open %s", a, b);
    CHECK(notes_a == notes_b && page_a == page_b && notes_a > 0);
    CHECK(M.smp.open && count(RFM_MOVE_ACT_MIDI) > 0);
    {                                                   /* the pads' and steps' lights: as the layer's */
        uint8_t w1[RFM_MOVE_NLEDS][3], w2[RFM_MOVE_NLEDS][3];
        leds(w1);
        back();
        leds(w2);
        CHECK(!memcmp(w1, w2, L_SAMPLE * 3) && !memcmp(w1[L_RING0], w2[L_RING0], 8 * 3));
        sample();
    }
    clear();
    {
        const int sel = M.seq.sel_track, pg = M.seq.page;
        jog(2), jog_click(), jog(1), jog_click();
        CHECK(M.seq.sel_track == sel && M.seq.page == pg && !count(RFM_MOVE_ACT_MIDI) && M.smp.row == RFM_MOVE_SMP_LENGTH);
    }
    {                                                   /* a held step takes the jog and the screen while held (R3) */
        step(2, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
        CHECK(rfmv_smp_covered(&M) && !rfm_move_smp_shown(&M));
        jog(1);
        CHECK(M.smp.row == RFM_MOVE_SMP_LENGTH && !M.smp.edit);
        step(2, 0);
        CHECK(!rfmv_smp_covered(&M) || M.pop);
    }
}

/* 9. a knob's popup over the app (R5), then the app again with the jog */
static void t_knob_popup(void)
{
    note_view();
    sample();
    rest();
    touch(0, 1), knob(0, 1);
    CHECK(M.pop && rfmv_smp_covered(&M) && !rfm_move_smp_shown(&M));
    dump("knob_over");
    touch(0, 0);
    rest();
    CHECK(!rfmv_smp_covered(&M) && rfm_move_smp_shown(&M));
    touch(0, 1), knob(0, 1);
    jog(1);                                             /* (the jog: the app's again, on top) */
    CHECK(!rfmv_smp_covered(&M));
    touch(0, 0);
}

/* 10. a take: armed (Bars), recording, the app closed (the take goes on), Sample brings it back, saved */
static void t_take(void)
{
    uint8_t c[3];
    note_view();
    rfm_move_smp_offer(&M, 0x3F, 0x7F, 1, 1);
    sample();
    sample();                                           /* the record key */
    CHECK(count(RFM_MOVE_ACT_SMP_PRESS) == 1);
    memset(&ST, 0, sizeof ST);
    ST.bars = 4, ST.level = 8000;
    status(RFM_MOVE_SMP_ST_ARMED);
    CHECK(rfm_move_screen_timed(&M));
    adv_ms(500 - rfmv_now_ms(&M) % 500);                /* (the blink's first half: lit) */
    sample_led(c);
    CHECK(is_rgb(c, 255, 0, 0));
    screen();
    CHECK(pixels(2, 1, 11, 11) > 50);                   /* (the dot) */
    dump("armed");
    adv_ms(260);
    sample_led(c);
    CHECK(is_rgb(c, 29, 0, 0));                         /* (blinking) */
    screen();
    CHECK(pixels(2, 1, 11, 11) == 0);
    jog(1), jog_click();                                /* (a take: no list) */
    CHECK(!M.smp.edit && M.smp.row == RFM_MOVE_SMP_SOURCE);
    ST.bar = 1, ST.span = 58;
    feed_wave(15, 20, 7u, 58);
    status(RFM_MOVE_SMP_ST_RECORDING);
    sample_led(c);
    CHECK(is_rgb(c, 255, 0, 0));
    screen();
    {
        int thick;
        int x, n = 0, on = 0;
        thick = -1;
        for (x = 0; x < 128; x++) {
            if (px(x, 63) && !on)
                n++;
            if (px(x, 61) && px(x, 63) && thick < 0)
                thick = n - 1;
            on = px(x, 63);
        }
        CHECK(n == 4 && thick == 0);                    /* (the strip: 4 bars, the first) */
    }
    back();                                             /* closed: the take goes on, red */
    CHECK(!M.smp.open && count(RFM_MOVE_ACT_SMP_PRESS) == 1);
    sample_led(c);
    CHECK(is_rgb(c, 255, 0, 0));
    dump("closed_recording");
    clear();
    sample();                                           /* back, and the same press ends the take */
    CHECK(M.smp.open && count(RFM_MOVE_ACT_SMP_PRESS) == 1);
    ST.bar = 4;
    status(RFM_MOVE_SMP_ST_SAVING);
    sample_led(c);
    CHECK(is_rgb(c, 255, 0, 0));
    status(RFM_MOVE_SMP_ST_IDLE);
    CHECK(rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_SAVED, 4, 1200, 8000, NULL) == 1);
    sample_led(c);
    CHECK(is_rgb(c, 0, 255, 0));                       /* (green 250 ms) */
    screen();
    CHECK(pixels(0, 12, 128, 14) > 50 && pixels(0, 32, 128, 14) > 50);   /* "Saved" / "4 Bars" */
    dump("saved");
    adv_ms(RFM_MOVE_SMP_FLASH_MS + 10);
    sample_led(c);
    CHECK(is_rgb(c, 255, 255, 255));
    adv_ms(RFM_MOVE_POPUP_MS);
    screen();
    CHECK(pixels(0, 49, 128, 15) > 1000);               /* (the list again) */
    CHECK(!strcmp(M.smp.msg_a, "Saved") && !strcmp(M.smp.msg_b, "4 Bars"));
}

/* 11. the events: a take with no bars (its seconds), nothing to capture, errors; with the app closed */
static void t_events(void)
{
    note_view();
    sample();
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_SAVED, 0, 1200, 7250, NULL);
    CHECK(!strcmp(M.smp.msg_a, "Saved") && !strcmp(M.smp.msg_b, "7.25 s"));
    dump("saved_secs");
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_SAVED, 1, 1200, 2000, NULL);
    CHECK(!strcmp(M.smp.msg_b, "1 Bar"));
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_NOTHING, 0, 0, 0, NULL);
    CHECK(!strcmp(M.smp.msg_a, "Nothing") && !strcmp(M.smp.msg_b, "to capture"));
    dump("nothing");
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_ERROR, 0, 0, 0, "Disk low");
    CHECK(!strcmp(M.smp.msg_a, "Disk low") && !M.smp.msg_b[0]);
    dump("error");
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_ERROR, 0, 0, 0, "Write error on disk");
    CHECK(!strcmp(M.smp.msg_a, "Write") && !strcmp(M.smp.msg_b, "error on d"));
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_ERROR, 0, 0, 0, NULL);
    CHECK(!strcmp(M.smp.msg_a, "Error"));
    CHECK(rfm_move_smp_event(&M, 9, 0, 0, 0, NULL) == 0);
    jog(1);                                             /* (a turn ends the message) */
    CHECK(!M.smp.msg);
    back();
    clear();
    CHECK(rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_NOTHING, 0, 0, 0, NULL) == 0);   /* closed: said, not shown */
    CHECKF(strstr(said, "V:Nothing to capture|") != NULL, "%s", said);
    {                                                   /* the time with no bars */
        sample();
        memset(&ST, 0, sizeof ST);
        ST.frames = 48000 * 73, ST.rate = 48000, ST.level = 3000;
        feed_wave(15, 116, 7u, 0);
        status(RFM_MOVE_SMP_ST_RECORDING);
        screen();
        CHECK(pixels(20, 0, 60, 14) > 40 && pixels(0, 59, 128, 5) == 0);   /* "1:13", no strip */
        dump("time");
        status(RFM_MOVE_SMP_ST_IDLE);
    }
}

/* 12. the FM-1 layout: Sample (closed, idle) stays SEL; Track held + Sample opens it; then Sample is the app's */
static void t_fm1(void)
{
    fresh();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1);
    sample();
    CHECK(count(RFM_MOVE_ACT_BTN) == 2 && A[0].kind == RFM_MOVE_ACT_BTN && A[0].index == RFM_MOVE_B_SEL && !M.smp.open);
    clear();
    hold_track(0), sample(), track(0, 0);
    CHECK(M.smp.open && !count(RFM_MOVE_ACT_BTN) && M.s.smp[0] == RFM_MOVE_SMP_SRC_SLOT1 && M.s.mode == RFM_MOVE_MODE_FM1);
    clear();
    sample();
    CHECK(count(RFM_MOVE_ACT_SMP_PRESS) == 1 && !count(RFM_MOVE_ACT_BTN));
    clear();
    jog(1);                                             /* (the jog: the app's, not the FM-1's SELECT) */
    CHECK(!count(RFM_MOVE_ACT_TURN) && M.smp.row == RFM_MOVE_SMP_MODE);
    hit(3);                                             /* (the pads: the FM-1's keys) */
    CHECK(count(RFM_MOVE_ACT_KEY) == 2);
    dump("fm1");
    back();
    clear();
    memset(&ST, 0, sizeof ST);
    status(RFM_MOVE_SMP_ST_RECORDING);                  /* a take runs: Sample brings the app back and ends it */
    sample();
    CHECK(M.smp.open && !count(RFM_MOVE_ACT_BTN) && count(RFM_MOVE_ACT_SMP_PRESS) == 1);
    back(), clear();
    status(RFM_MOVE_SMP_ST_ARMED);                      /* armed and waiting: Sample only shows the app */
    sample();
    CHECK(M.smp.open && !count(RFM_MOVE_ACT_BTN) && !count(RFM_MOVE_ACT_SMP_PRESS));
    status(RFM_MOVE_SMP_ST_IDLE);
    back(), clear();
    sample();
    CHECK(!M.smp.open && count(RFM_MOVE_ACT_BTN) == 2);
}

/* 13. the projects view keeps Sample (the browser's favourite key) */
static void t_browser(void)
{
    note_view();
    tap_track(0), tap_track(0);                         /* (a double tap: the browser on slot 1) */
    CHECK(M.seq.projects);
    clear();
    sample();
    CHECK(!M.smp.open && !smp_acts());
    {
        uint8_t c[3];
        sample_led(c);
        CHECK(c[0] != 11 || c[1] != 11);                /* (the browser's light, not the sampler's) */
    }
}

/* 14. a takeover: one that claims the buttons keeps Sample; one that does not: the app opens, the takeover hidden */
static void t_takeover(void)
{
    fresh();
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_SCREEN | RFM_MOVE_TK_PADS | RFM_MOVE_TK_BUTTONS, "CHORDS");
    rfm_move_set_slot_layout(&M, 0, RFM_MOVE_LAY_TK);
    CHECK(rfm_move_tk_shown(&M) == 0);
    clear();
    sample();
    CHECK(count(RFM_MOVE_ACT_TK_INPUT) == 2 && !M.smp.open);
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_SCREEN | RFM_MOVE_TK_PADS, "CHORDS");
    hold_track(1), track(1, 0), rest();
    hold_track(0), track(0, 0), rest();
    rfm_move_set_slot_layout(&M, 0, RFM_MOVE_LAY_TK);
    rfm_move_input(&M, &(mr_event_t){ 0 }, now_ms);     /* (a call that syncs the takeover) */
    CHECK(rfm_move_tk_shown(&M) == 0);
    clear();
    hold_track(0), sample(), track(0, 0);
    CHECK(M.smp.open && rfm_move_tk_shown(&M) == -1 && count(RFM_MOVE_ACT_TK_STATE) >= 1 &&
          last(RFM_MOVE_ACT_TK_STATE)->down == 0);
    screen();
    CHECK(pixels(0, 49, 128, 15) > 1000);               /* (the app's screen, not the firmware's) */
    back();
    CHECK(!M.smp.open && rfm_move_tk_shown(&M) == 0);
}

/* 15. a Shift menu: Sample closes it, the app shows; Global settings keeps everything */
static void t_menus(void)
{
    note_view();
    rfmv_seq_menu_open(&M, RFM_MOVE_SM_VEL);
    CHECK(M.seq.smenu == RFM_MOVE_SM_VEL);
    sample();
    CHECK(!M.seq.smenu && rfm_move_smp_shown(&M) && !count(RFM_MOVE_ACT_SMP_PRESS));
    rfmv_seq_menu_open(&M, RFM_MOVE_SM_VEL);            /* a menu opened over it: Back closes the menu first */
    CHECK(M.smp.open && !rfm_move_smp_shown(&M));
    back();
    CHECK(!M.seq.smenu && rfm_move_smp_shown(&M));
    rfmv_seq_menu_open(&M, RFM_MOVE_SM_GLOBAL);
    clear();
    sample();
    CHECK(!smp_acts() && M.seq.smenu == RFM_MOVE_SM_GLOBAL);
}

/* 16. the host's offer: sources, rows, To's values */
static void t_offer(void)
{
    note_view();
    sample();
    jog_click();                                        /* (Source's popup) */
    jog(2);
    CHECK(M.s.smp[0] == RFM_MOVE_SMP_SRC_SLOT2);
    clear();
    rfm_move_smp_offer(&M, (1 << RFM_MOVE_SMP_SRC_MASTER) | (1 << RFM_MOVE_SMP_SRC_INPUT) | (1 << RFM_MOVE_SMP_SRC_KIT), 0, 0, 0);
    CHECK(M.s.smp[0] == RFM_MOVE_SMP_SRC_MASTER && last(RFM_MOVE_ACT_SMP_SET)->value == RFM_MOVE_SMP_SRC_MASTER);
    jog(1);
    CHECK(M.s.smp[0] == RFM_MOVE_SMP_SRC_INPUT);
    jog_click(), rest();
    screen();
    CHECK(pixels(117, 1, 11, 11) > 20);                 /* (the input's icon as the sign) */
    dump("input_sign");
    jog_click();
    jog(1);
    CHECK(M.s.smp[0] == RFM_MOVE_SMP_SRC_KIT);
    jog(1);
    CHECK(M.s.smp[0] == RFM_MOVE_SMP_SRC_KIT);
    jog_click();
    rfm_move_smp_offer(&M, 0, (1 << RFM_MOVE_SMP_MODE) | (1 << RFM_MOVE_SMP_LENGTH) | (1 << RFM_MOVE_SMP_TO), 0,
                       (1 << RFM_MOVE_SMP_TO_FILES) | (1 << RFM_MOVE_SMP_TO_KIT));
    CHECK(M.smp.row == RFM_MOVE_SMP_MODE);              /* (Source gone: the first offered) */
    screen();
    {
        int x, n = 0;
        for (x = 0; x < 128; x++)
            n += px(x, 47) && !px(x - 1 < 0 ? 0 : x - 1, 47);
        CHECK(n == 3);                                  /* (three marks) */
    }
    jog(5);
    CHECK(M.smp.row == RFM_MOVE_SMP_TO);
    jog_click(), jog(1);
    CHECK(M.s.smp[6] == RFM_MOVE_SMP_TO_KIT);
    jog(-1), jog_click();
    CHECK(M.s.smp[6] == RFM_MOVE_SMP_TO_FILES);
}

/* 17. the settings blob: version 8 with the sampler's; version 7 (130 bytes) loads its defaults; bad values */
static void t_blob(void)
{
    uint8_t b[RFM_MOVE_SETTINGS_BYTES];
    rfm_move_settings_t s, t;
    rfm_move_settings_default(&s);
    CHECK(s.smp[0] == RFM_MOVE_SMP_SRC_MASTER && s.smp[1] == RFM_MOVE_SMP_M_BARS && s.smp[2] == 0 && s.smp[3] == -24 &&
          s.smp[4] == 0 && s.smp[5] == 0 && s.smp[6] == RFM_MOVE_SMP_TO_FILES);
    s.smp[0] = RFM_MOVE_SMP_SRC_INPUT, s.smp[1] = RFM_MOVE_SMP_M_CAPTURE, s.smp[2] = 6, s.smp[3] = -60, s.smp[4] = 24;
    s.smp[5] = 1, s.smp[6] = RFM_MOVE_SMP_TO_LOOPER, s.vel_mode = RFM_MOVE_VEL_HARD;
    CHECK(rfm_move_save(&s, b) == RFM_MOVE_SETTINGS_BYTES && RFM_MOVE_SETTINGS_BYTES == 137 && b[3] == 8);
    CHECK(rfm_move_load(&t, b, sizeof b) == 0 && !memcmp(&s, &t, sizeof s));
    b[3] = 7, b[4] = RFM_MOVE_SETTINGS_BYTES_V7 & 0xFF, b[5] = RFM_MOVE_SETTINGS_BYTES_V7 >> 8;   /* (an older image) */
    CHECK(rfm_move_load(&t, b, RFM_MOVE_SETTINGS_BYTES_V7) == 0 && t.vel_mode == RFM_MOVE_VEL_HARD &&
          t.smp[0] == RFM_MOVE_SMP_SRC_MASTER && t.smp[3] == -24 && t.smp[6] == RFM_MOVE_SMP_TO_FILES);
    rfm_move_save(&s, b);
    b[RFM_MOVE_SETTINGS_BYTES_V7 + 3] = 5;              /* (out of range: that one's default) */
    b[RFM_MOVE_SETTINGS_BYTES_V7 + 0] = 99;
    CHECK(rfm_move_load(&t, b, sizeof b) == 0 && t.smp[3] == -24 && t.smp[0] == RFM_MOVE_SMP_SRC_MASTER && t.smp[4] == 24);
    CHECK(rfm_move_load(&t, b, sizeof b - 1) == -1);
    {                                                   /* the core with these settings: SETs say them */
        fresh_caps(RFM_MOVE_CAPS_ALL);
        M.s = s;
        clear();
        rfm_move_fw_caps(&M, RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_SAMPLER);
        CHECK(count(RFM_MOVE_ACT_SMP_SET) == RFM_MOVE_SMP_N && A[0].value == RFM_MOVE_SMP_SRC_INPUT && A[2].value == 32 &&
              A[3].value == -60 && A[4].value == 24 && A[6].value == RFM_MOVE_SMP_TO_LOOPER);
        clear();                                        /* (the host's offer without Looper: To moves to Files) */
        rfm_move_smp_offer(&M, 0, 0, 0, 1 << RFM_MOVE_SMP_TO_FILES);
        CHECK(count(RFM_MOVE_ACT_SMP_SET) == 1 && A[0].index == RFM_MOVE_SMP_TO && A[0].value == RFM_MOVE_SMP_TO_FILES &&
              count(RFM_MOVE_ACT_SETTINGS) == 1);
    }
}

/* 18. the speech: opening, a row, a value, the tabs, a take, a save, closing */
static void t_speech(void)
{
    note_view();
    sample();
    CHECKF(!strcmp(said, "E:Sampler|V:Source: Master|"), "%s", said);
    clear();
    jog(1);
    CHECKF(!strcmp(said, "V:Mode: Bars|"), "%s", said);
    clear();
    jog(1);
    CHECKF(!strcmp(said, "V:Length: Open|"), "%s", said);
    jog_click(), clear();
    jog(3);
    CHECKF(!strcmp(said, "V:Length: 4 bars|"), "%s", said);
    jog_click(), jog(-2), clear();
    CHECK(M.smp.row == RFM_MOVE_SMP_SOURCE);
    jog(-1);
    CHECKF(!strcmp(said, "V:Tabs, Record|"), "%s", said);
    clear();
    jog_click();
    CHECKF(!strcmp(said, "V:Tab: Record|"), "%s", said);
    jog_click(), clear();
    memset(&ST, 0, sizeof ST);
    status(RFM_MOVE_SMP_ST_ARMED);
    CHECKF(!strcmp(said, "V:Armed|"), "%s", said);
    clear();
    status(RFM_MOVE_SMP_ST_RECORDING);
    CHECKF(!strcmp(said, "V:Recording|"), "%s", said);
    clear();
    ST.level = 20000;
    status(RFM_MOVE_SMP_ST_RECORDING);
    CHECK(!said[0]);                                    /* (the level: nothing) */
    status(RFM_MOVE_SMP_ST_SAVING);
    status(RFM_MOVE_SMP_ST_IDLE);
    clear();
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_SAVED, 4, 1200, 8000, NULL);
    CHECKF(!strcmp(said, "V:Saved, 4 bars|"), "%s", said);
    clear();
    M.s.smp[RFM_MOVE_SMP_MODE] = RFM_MOVE_SMP_M_CAPTURE;
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_SAVED, 2, 1200, 4000, NULL);
    CHECKF(!strcmp(said, "V:Captured, 2 bars|"), "%s", said);
    clear();
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_NOTHING, 0, 0, 0, NULL);
    CHECKF(!strcmp(said, "V:Nothing to capture|"), "%s", said);
    rest();
    CHECKF(!strcmp(said, "V:Nothing to capture|V:Tabs, Record|"), "%s", said);   /* (the message gone: the list) */
    clear();
    back();
    CHECKF(!strcmp(said, "E:Felucca, Scale layout, Track 2|"), "%s", said);   /* (closed: the view under it named) */
    clear();
    status(RFM_MOVE_SMP_ST_ARMED);                      /* (closed: the take's state said) */
    CHECKF(!strcmp(said, "V:Armed|"), "%s", said);
    status(RFM_MOVE_SMP_ST_IDLE);
}

/* 19. Capture listening: Sample's light dim while closed */
static void t_listen(void)
{
    uint8_t c[3];
    note_view();
    sample(), jog(1), jog_click(), jog(-1), jog_click(), back();   /* (Mode: Capture) */
    CHECK(M.s.smp[1] == RFM_MOVE_SMP_M_CAPTURE && !M.smp.open);
    memset(&ST, 0, sizeof ST);
    ST.listening = 1;
    status(RFM_MOVE_SMP_ST_IDLE);
    sample_led(c);
    CHECK(is_rgb(c, 39, 39, 39));
    dump("closed_listening");
    ST.listening = 0;
    status(RFM_MOVE_SMP_ST_IDLE);
    sample_led(c);
    CHECK(is_rgb(c, 11, 11, 11));
    sample(), jog(1);                                   /* (Length in Capture mode: Auto) */
    {
        char w[64];
        rfm_move_words(&M, w, sizeof w);
        CHECKF(!strcmp(w, "Sampler, Length: Auto"), "%s", w);
    }
}

/* 20. the screens against the mock-up's boards */
static void t_mock(void)
{
    static const box_t TAB2 = { 18, 0, 16, 14 };        /* (the mock-up's second tab, Takes: not built here) */
    static const box_t MARK = { 72, 52, 5, 12 };        /* (the popup's threshold mark: see below) */
    int in, out;
    note_view();
    rfm_move_smp_offer(&M, 0, 0, 0, 0);
    M.s.smp[0] = RFM_MOVE_SMP_SRC_SLOT2;
    /* one: Mode (Bars) chosen, the live wave, the level 17 px, the threshold's mark 9 px (-41 dB) */
    M.s.smp[3] = -41, M.s.smp[2] = 3;
    memset(&ST, 0, sizeof ST);
    feed_wave(12, 116, 31u, 0);
    ST.level = (uint16_t)level_for(17, 28);
    sample(), jog(1), rest();
    status(RFM_MOVE_SMP_ST_IDLE);
    out = vs_ref("one", &TAB2, 1, &in);
    CHECKF(out == 0 && in > 0, "one: %d differ, %d in the second tab", out, in);
    printf("mock one: %d pixels differ outside the second tab's place, %d inside it\n", out, in);
    dump("one");
    /* three: Source, Mode (chosen), Length (4 Bars); the level 30 of 56, the threshold's mark at 40 (-17 dB) */
    M.s.smp[3] = -17;
    ST.level = (uint16_t)level_for(30, 56);
    status(RFM_MOVE_SMP_ST_IDLE);
    jog(1), jog(-1);
    out = vs_ref("three", &TAB2, 1, &in);
    CHECKF(out == 0 && in > 0, "three: %d differ, %d in the second tab", out, in);
    printf("mock three: %d pixels differ outside the second tab's place, %d inside it\n", out, in);
    dump("three");
    /* tabs: the jog on them; the level 14, the mark 9 */
    M.s.smp[3] = -41;
    ST.level = (uint16_t)level_for(14, 28);
    status(RFM_MOVE_SMP_ST_IDLE);
    jog(-2), rest();
    CHECK(M.smp.on_tabs);
    out = vs_ref("tabs", &TAB2, 1, &in);
    CHECKF(out == 0 && in > 0, "tabs: %d differ, %d in the second tab", out, in);
    printf("mock tabs: %d pixels differ outside the second tab's place, %d inside it\n", out, in);
    dump("tabs");
    /* popup: Threshold -24, the level 40 of 112 */
    M.s.smp[3] = -24;
    ST.level = (uint16_t)level_for(40, 112);
    status(RFM_MOVE_SMP_ST_IDLE);
    jog(4), jog_click();
    CHECK(M.smp.edit && M.smp.row == RFM_MOVE_SMP_THRESHOLD);
    out = vs_ref("popup", &MARK, 1, &in);
    CHECKF(out == 0 && in == 45, "popup: %d differ, %d at the mark", out, in);
    printf("mock popup: %d pixels differ outside the threshold's mark, %d at it (the mock 72, here 75)\n", out, in);
    dump("popup");
    jog_click();
    /* run: bar 2 of 4, the take's wave 26 columns of 58, the level 19 */
    memset(&ST, 0, sizeof ST);
    ST.bar = 2, ST.bars = 4, ST.level = (uint16_t)level_for(19, 28);
    feed_wave(15, 52, 7u, 58);
    CHECK(ST.npeaks == 26);
    status(RFM_MOVE_SMP_ST_RECORDING);
    out = vs_ref("run", NULL, 0, &in);
    CHECKF(out == 0, "run: %d differ", out);
    printf("mock run: %d pixels differ\n", out);
    dump("run");
    /* captured: Capture mode, 4 bars kept */
    status(RFM_MOVE_SMP_ST_IDLE);
    M.s.smp[1] = RFM_MOVE_SMP_M_CAPTURE;
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_SAVED, 4, 1200, 8000, NULL);
    out = vs_ref("captured", NULL, 0, &in);
    CHECKF(out == 0, "captured: %d differ", out);
    printf("mock captured: %d pixels differ\n", out);
    dump("captured");
}

/* 21. the level's scale: -60..0 dB, no libm */
static void t_scale(void)
{
    CHECK(rfmv_smp_lvpx(0, 28) == 0 && rfmv_smp_lvpx(32767, 28) == 28 && rfmv_smp_lvpx(32, 28) == 0);
    CHECK(rfmv_smp_lvpx(16384, 60) == 54);              /* (-6.02 dB) */
    CHECK(rfmv_smp_lvpx(3277, 60) == 40);               /* (-20 dB) */
    CHECK(rfmv_smp_lvpx(33, 60) == 0);                  /* (-60 dB) */
}

/* ================================================================ (stage 2) the takes === */
/* a fake host's takes: three days, the newest first; the first day's first three takes are the mock-up's "takes" board
 * (2, 2, M; 4, 4, 8 bars; their pictures its seeds 21, 77, 5), then the input (7.25 s), one of no known source (3 s),
 * a stems take (2 bars) */
#define DAY0 0x100u
typedef struct {
    rfm_move_smp_row_t day;
    rfm_move_smp_row_t t[8];
    int nt;
} fday_t;
static fday_t FD[3];
static int nfd;
static struct { uint32_t take, from, count; uint8_t pk[64]; int on; } OVR;   /* the mock's peaks for one view */
static void mock_peaks(uint32_t take, uint32_t from, uint32_t count, int h, unsigned seed)
{
    int a[64], n = mock_wave(128, h, 128, seed, a), i;
    OVR.take = take, OVR.from = from, OVR.count = count, OVR.on = 1;
    for (i = 0; i < n; i++)
        OVR.pk[i] = peak_for(a[i], h);
}
static void fake_db(void)
{
    static const unsigned SEED[3] = { 21u, 77u, 5u };
    int d, i, c;
    memset(FD, 0, sizeof FD), nfd = 3, OVR.on = 0;
    for (d = 0; d < 3; d++) {
        rfm_move_smp_row_t *r = &FD[d].day;
        FD[d].nt = d == 0 ? 6 : d == 1 ? 2 : 1;
        r->id = DAY0 + (uint32_t)d, r->kind = RFM_MOVE_SMP_ROW_DAY, r->day = (uint8_t)(24 - d), r->month = 11;
        r->today = d == 0, r->takes = (uint16_t)FD[d].nt;
        for (i = 0; i < FD[d].nt; i++) {
            rfm_move_smp_row_t *t = &FD[d].t[i];
            t->id = 0x1000u + (uint32_t)(d * 16 + i), t->kind = RFM_MOVE_SMP_ROW_TAKE, t->folder = r->id, t->rate = 44100;
            t->src = i == 2 ? RFM_MOVE_SMP_SRC_MASTER : i == 3 ? RFM_MOVE_SMP_SRC_INPUT : i == 4 ? RFM_MOVE_SMP_SRC_NONE
                   : RFM_MOVE_SMP_SRC_SLOT2;
            t->bars = (uint8_t)(i < 2 ? 4 : i == 2 ? 8 : i == 5 ? 2 : 0), t->bpm10 = t->bars ? 1200 : 0;
            t->frames = t->bars ? t->bars * 88200u : i == 3 ? 44100u * 7 + 11025 : 44100u * 3;
            if (d == 0 && i < 3) {
                int a[64], n = mock_wave(28, 5, 28, SEED[i], a);
                for (c = 0; c < n; c++)
                    t->thumb[c] = peak_for(a[c], 5);
            } else
                for (c = 0; c < RFM_MOVE_SMP_THUMB; c++)
                    t->thumb[c] = (uint8_t)(60 + (c * 37 + i * 11) % 190);
        }
    }
    FD[0].t[5].kind = RFM_MOVE_SMP_ROW_STEMS, FD[0].t[5].src = RFM_MOVE_SMP_SRC_STEMS;
}
static void answer_rows(const rfm_move_act_t *a)
{
    rfm_move_smp_row_t rows[8];
    int n = 0, i, d = -1;
    const uint32_t folder = a->take;
    if (!folder) {
        for (i = (int)a->from; i < nfd && n < (int)a->count; i++)
            rows[n++] = FD[i].day;
        rfm_move_smp_rows(&M, 0, 0, nfd, (int)a->from, n, rows);
        return;
    }
    if (folder == RFM_MOVE_SMP_NEWEST) {
        for (i = 0; i < nfd && !FD[i].nt; i++) {}
        d = i < nfd ? i : -1;
    } else
        for (i = 0; i < nfd; i++)
            d = FD[i].day.id == folder ? i : d;
    if (d < 0) {
        rfm_move_smp_rows(&M, folder, folder == RFM_MOVE_SMP_NEWEST ? 0 : folder, 0, 0, 0, NULL);
        return;
    }
    for (i = (int)a->from; i < FD[d].nt && n < (int)a->count; i++)
        rows[n++] = FD[d].t[i];
    rfm_move_smp_rows(&M, folder, FD[d].day.id, FD[d].nt, (int)a->from, n, rows);
}
static uint8_t fpk(uint32_t take, uint64_t f) { return (uint8_t)(40 + ((f / 2000u) * 53u + take * 7u) % 180u); }
static void answer_peaks(const rfm_move_act_t *a)
{
    uint8_t pk[64];
    int c;
    const int n = a->value;
    if (OVR.on && a->take == OVR.take && a->from == OVR.from && a->count == OVR.count)
        memcpy(pk, OVR.pk, 64);
    else
        for (c = 0; c < n; c++)
            pk[c] = fpk(a->take, a->from + (uint64_t)a->count * (uint64_t)c / (uint64_t)n);
    rfm_move_smp_peaks(&M, a->take, a->from, a->count, n, pk);
}
static void serve(void)                                 /* the host answers what was asked (answers may ask more) */
{
    while (served < nact) {
        const rfm_move_act_t a = A[served++];
        if (a.kind == RFM_MOVE_ACT_SMP_ROWS)
            answer_rows(&a);
        else if (a.kind == RFM_MOVE_ACT_SMP_PEAKS)
            answer_peaks(&a);
    }
}
/* a note layout, speech on, the host offering Record, Takes, Tools; the app on Takes, the newest take chosen */
static void takes_app(void)
{
    note_view();
    fake_db();
    rfm_move_smp_offer(&M, 0, 0, 0x7, 0);
    sample();
    jog(-1), jog_click(), jog(1), jog_click(), jog(1);  /* (the tabs, their click, Takes, the click, the first row) */
    serve();
    CHECK(M.smp.tab == RFM_MOVE_SMP_TAB_TAKES && !M.smp.on_tabs && M.smp.sel == 0 && M.smp.known && M.smp.total == 6);
    rest(), clear();
}
static void to_tab(int t)                               /* the jog to the tabs, a tab picked */
{
    jog(-20), jog_click(), jog(-5), jog(t), jog_click(), jog(1);
    serve();
}
static uint8_t white(int w_)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    leds(w);
    return w[L_WHITE0 + w_][0];
}
static void words(char *w, int cap) { rfm_move_words(&M, w, cap); }

/* 22. the Takes tab: asked as it opens, drawn as rows come (empty before), the window as the jog scrolls, the mock */
static void t2_takes(void)
{
    static const box_t TOOLS = { 36, 0, 16, 14 };       /* (the mock's line has two tabs: Tools is not in it) */
    char w[96];
    int in, out, p0;
    note_view();
    fake_db();
    rfm_move_smp_offer(&M, 0, 0, 0x7, 0);
    sample();
    clear();
    jog(-1), jog_click(), jog(1);
    CHECK(M.smp.tab == RFM_MOVE_SMP_TAB_TAKES && count(RFM_MOVE_ACT_SMP_ROWS) == 1 &&
          last(RFM_MOVE_ACT_SMP_ROWS)->take == RFM_MOVE_SMP_NEWEST && last(RFM_MOVE_ACT_SMP_ROWS)->from == 0 &&
          last(RFM_MOVE_ACT_SMP_ROWS)->count == 3);
    jog_click(), jog(1);
    CHECK(!M.smp.on_tabs && M.smp.sel == 0 && count(RFM_MOVE_ACT_SMP_ROWS) == 1);   /* (asked once until answered) */
    screen();
    CHECK(pixels(0, 16, 128, 15) == 15 * 128 && pixels(0, 31, 128, 33) == 0);   /* (rows not come: the chosen one's
                                                         * block, nothing else) */
    dump("takes_late");
    serve();
    CHECK(M.smp.known && M.smp.total == 6 && M.smp.folder == DAY0);
    screen();
    CHECK(pixels(0, 32, 128, 32) > 60);
    jog(1);
    CHECK(M.smp.sel == 1 && count(RFM_MOVE_ACT_SMP_ROWS) == 1);   /* (rows 0..2 here: nothing asked) */
    out = vs_ref("takes", &TOOLS, 1, &in);
    CHECKF(out == 0 && in > 0, "takes: %d differ, %d in Tools' place", out, in);
    printf("mock takes: %d pixels differ outside the third tab's place, %d inside it (the mock has two tabs)\n", out, in);
    dump("takes");
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Slot 2, 4 bars"), "%s", w);
    clear();
    jog(2);                                             /* (row 3 in the middle: rows 2..4, two of them not here) */
    CHECK(M.smp.sel == 3 && count(RFM_MOVE_ACT_SMP_ROWS) == 1 && last(RFM_MOVE_ACT_SMP_ROWS)->take == DAY0 &&
          last(RFM_MOVE_ACT_SMP_ROWS)->from == 2);
    screen();
    p0 = pixels(0, 32, 128, 15);
    CHECK(p0 == 15 * 128);                              /* (the chosen row not come yet: its block only) */
    serve();
    screen();
    CHECK(pixels(3, 34, 11, 11) < 121 - 20 && pixels(0, 32, 128, 15) < p0);   /* (the input's icon, black on white) */
    dump("takes_input");
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Input, 7.2 seconds"), "%s", w);
    jog(1);
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Take, 3.0 seconds"), "%s", w);   /* (a source not known: "?") */
    jog(1), serve(), jog(5);
    CHECK(M.smp.sel == 5);                              /* (the last: no wrap) */
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Stems, 2 bars"), "%s", w);
    dump("takes_stems");
    jog(-10);
    CHECK(M.smp.on_tabs && M.smp.sel == 0);             /* (up past the first: the tabs) */
    jog(1);
    CHECK(!M.smp.on_tabs && M.smp.sel == 0);
    CHECK(!count(RFM_MOVE_ACT_MIDI) && !count(RFM_MOVE_ACT_SMP_SET));
}

/* 23. the days: the left arrow, a click on a day opens it, Right too, Back the day's takes; the arrows and Delete lit */
static void t2_days(void)
{
    static const box_t TOOLS = { 36, 0, 16, 14 };
    char w[96];
    int in, out, pg;
    takes_app();
    CHECK(white(W_LEFT) == 60 && white(W_RIGHT) == 0 && white(W_DELETE) == 60 && white(W_UNDO) == 0);
    pg = M.seq.page;
    press(MR_CC_LEFT);
    CHECK(M.smp.days && count(RFM_MOVE_ACT_SMP_ROWS) == 1 && last(RFM_MOVE_ACT_SMP_ROWS)->take == 0 &&
          last(RFM_MOVE_ACT_SMP_ROWS)->from == 0 && M.seq.page == pg);   /* (the app's Left, not the page's) */
    serve();
    CHECK(M.smp.dknown && M.smp.dtotal == 3);
    out = vs_ref("days", &TOOLS, 1, &in);
    CHECKF(out == 0 && in > 0, "days: %d differ, %d in Tools' place", out, in);
    printf("mock days: %d pixels differ outside the third tab's place, %d inside it\n", out, in);
    dump("days");
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Today"), "%s", w);
    CHECK(white(W_LEFT) == 0 && white(W_RIGHT) == 60 && white(W_DELETE) == 0);
    jog(1);
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, 23 November"), "%s", w);
    clear();
    jog_click();
    CHECK(!M.smp.days && M.smp.folder == DAY0 + 1 && M.smp.sel == 0 && last(RFM_MOVE_ACT_SMP_ROWS)->take == DAY0 + 1);
    serve();
    CHECK(M.smp.total == 2);
    press(MR_CC_LEFT), serve();
    CHECK(M.smp.days && M.smp.dsel == 1);               /* (the day chosen remembered) */
    back();
    CHECK(!M.smp.days && M.smp.open && M.smp.folder == DAY0 + 1);   /* (Back: the day's takes again) */
    press(MR_CC_LEFT), jog(1), press(MR_CC_RIGHT), serve();
    CHECK(!M.smp.days && M.smp.folder == DAY0 + 2 && M.smp.total == 1);
    FD[2].nt = 0;                                       /* (its last take deleted elsewhere: the day gone) */
    clear();
    rfm_move_smp_changed(&M);
    serve();
    CHECK(M.smp.folder == DAY0 && M.smp.total == 6 && count(RFM_MOVE_ACT_SMP_ROWS) >= 2 &&
          last(RFM_MOVE_ACT_SMP_ROWS)->take == RFM_MOVE_SMP_NEWEST);   /* (the newest day instead) */
    back();
    CHECK(!M.smp.open);
    CHECK(white(W_LEFT) != 60 || M.s.mode != RFM_MOVE_MODE_SEQ);   /* (closed: the layer's lights) */
}

/* 24. a take's actions: the click, its wave, Play (twice: it stops), the line moving, Back; a stems take; the mock */
static void t2_act(void)
{
    static const box_t ICONS = { 58, 26, 70, 16 };      /* (the mock's row has "to a kit pad" and "to the looper") */
    static uint8_t a1[MR_SCREEN_BYTES];
    const rfm_move_smp_row_t *t;
    char w[96];
    int in, out, x, diff = 0, other = 0;
    takes_app();
    jog(1);
    t = &FD[0].t[1];
    mock_peaks(t->id, 0, t->frames, 10, 77u);
    jog_click();
    CHECK(M.smp.scr == 1 && M.smp.cur.id == t->id && M.smp.ai == 0);
    CHECK(count(RFM_MOVE_ACT_SMP_PEAKS) == 1 && last(RFM_MOVE_ACT_SMP_PEAKS)->take == t->id &&
          last(RFM_MOVE_ACT_SMP_PEAKS)->from == 0 && last(RFM_MOVE_ACT_SMP_PEAKS)->count == t->frames &&
          last(RFM_MOVE_ACT_SMP_PEAKS)->value == RFM_MOVE_SMP_WPEAKS);
    screen();
    CHECK(pixels(0, 0, 128, 24) == 0);                  /* (the wave not come: none) */
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Play"), "%s", w);
    serve();
    jog(1);
    out = vs_ref("act", &ICONS, 1, &in);
    CHECKF(out == 0 && in > 0, "act: %d differ, %d in the row's last three places", out, in);
    printf("mock act: %d pixels differ outside the icons' last three places, %d inside them (the mock's five icons, "
           "this stage's three)\n", out, in);
    dump("act");
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Trim"), "%s", w);
    jog(5);
    CHECK(M.smp.ai == 2);
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Delete"), "%s", w);
    jog(-5);
    clear();
    jog_click();                                        /* Play */
    CHECK(count(RFM_MOVE_ACT_SMP_PLAY) == 1 && last(RFM_MOVE_ACT_SMP_PLAY)->value == 1 &&
          last(RFM_MOVE_ACT_SMP_PLAY)->take == t->id && last(RFM_MOVE_ACT_SMP_PLAY)->from == 0 &&
          last(RFM_MOVE_ACT_SMP_PLAY)->count == 0 && M.smp.play_id == t->id);
    memset(&ST, 0, sizeof ST);
    ST.play_take = t->id, ST.play_pos = t->frames / 2;
    CHECK(rfm_move_smp_status(&M, &ST) == 1 && rfm_move_screen_timed(&M));
    screen();
    memcpy(a1, SCR, sizeof a1);
    dump("act_playing");
    ST.play_take = 0, ST.play_pos = 0;
    status(RFM_MOVE_SMP_ST_IDLE);
    CHECK(M.smp.play_id == 0);                          /* (the host: it ended) */
    screen();
    for (x = 0; x < 128; x++) {
        int y;
        for (y = 0; y < 64; y++) {
            const int d = (a1[(y >> 3) * 128 + x] >> (y & 7) & 1) != px(x, y);
            diff += d && x == 64;
            other += d && x != 64;
        }
    }
    CHECK(diff > 10 && other == 0);                     /* (the line at the middle, the wave inverted under it) */
    jog_click();
    ST.play_take = t->id, ST.play_pos = 100;
    status(RFM_MOVE_SMP_ST_IDLE);
    clear();
    jog_click();                                        /* (again: it stops) */
    CHECK(count(RFM_MOVE_ACT_SMP_PLAY) == 1 && last(RFM_MOVE_ACT_SMP_PLAY)->value == 0 && !M.smp.play_id);
    ST.play_take = 0;
    status(RFM_MOVE_SMP_ST_IDLE);
    back();
    CHECK(M.smp.scr == 0 && M.smp.open && M.smp.tab == RFM_MOVE_SMP_TAB_TAKES && M.smp.sel == 1);
    jog(4);                                             /* a stems take: Play, Delete */
    serve();
    clear();
    jog_click();
    CHECK(M.smp.scr == 1 && M.smp.cur.kind == RFM_MOVE_SMP_ROW_STEMS);
    jog(5);
    CHECK(M.smp.ai == 1);
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Delete"), "%s", w);
    serve();
    screen();
    CHECK(pixels(33, 26, 15, 15) > 150 && pixels(58, 26, 15, 15) == 0);   /* (two icons, the second chosen) */
    dump("act_stems");
    jog(-1), jog_click();
    CHECK(last(RFM_MOVE_ACT_SMP_PLAY)->value == 1 && last(RFM_MOVE_ACT_SMP_PLAY)->take == FD[0].t[5].id);
    back();
    CHECK(count(RFM_MOVE_ACT_SMP_PLAY) == 2 && last(RFM_MOVE_ACT_SMP_PLAY)->value == 0);   /* (Back: it stops) */
}

/* 25. Trim: the knobs (1 start, 2 end, the others dark and dead), the jog's icons and its click on a marker, the zoom,
 * Shift's fine steps, Play the kept part, Keep (the cut), Back (nothing), the host's answer; the mock */
static void t2_trim(void)
{
    const rfm_move_smp_row_t *t;
    rfm_move_smp_row_t nr;
    uint8_t lw[RFM_MOVE_NLEDS][3];
    uint32_t F, st, e;
    char w[96];
    int i, ok = 1, in, out;
    takes_app();
    jog(1), jog_click(), serve();
    t = &FD[0].t[1];
    F = t->frames, st = F / 256;
    jog(1);
    clear();
    jog_click();
    CHECK(M.smp.scr == 2 && M.smp.trim_ret == 1 && M.smp.t_start == 0 && M.smp.t_end == F && M.smp.t_i == 0 &&
          !count(RFM_MOVE_ACT_SMP_PEAKS));              /* (the whole take's wave: here from the actions) */
    leds(lw);
    for (i = 0; i < 8; i++)
        ok &= i < 2 ? lw[L_RING0 + i][0] == 109 && lw[L_RING0 + i][1] == 30 : !lw[L_RING0 + i][0] && !lw[L_RING0 + i][1];
    CHECK(ok && lw[L_WHITE0 + W_SHIFT][0] == 40);
    dump("trim_start");
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Start, 0.000 seconds"), "%s", w);
    knob(0, 3);                                         /* knob 1: the start, 3 steps; zoomed on it */
    CHECK(M.smp.t_start == 3 * st && !count(RFM_MOVE_ACT_MIDI) && count(RFM_MOVE_ACT_SMP_PEAKS) == 1 &&
          last(RFM_MOVE_ACT_SMP_PEAKS)->from == 0 && last(RFM_MOVE_ACT_SMP_PEAKS)->count == 64 * st);
    serve();
    screen();
    CHECK(pixels(0, 0, 60, 14) > 30);                   /* (its time on top) */
    dump("trim_zoom");
    clear();
    knob(2, 5), knob(7, -2), touch(3, 1), touch(3, 0);  /* (the others: dead) */
    CHECK(!nact && M.smp.t_start == 3 * st && M.smp.t_end == F);
    touch(1, 1);                                        /* knob 2 touched: zoomed on the end, End chosen */
    CHECK(M.smp.t_i == 1 && M.smp.t_knob == 2 && last(RFM_MOVE_ACT_SMP_PEAKS)->from == F - 64 * st);
    leds(lw);
    CHECK(lw[L_RING0 + 1][0] == 255 && lw[L_RING0][0] == 109);
    touch(1, 0);
    rest();
    CHECK(!M.smp.t_turned && !M.smp.t_knob);           /* (a second after: the whole take) */
    clear();
    jog_click();                                        /* (End chosen) its click: the jog moves it */
    CHECK(M.smp.t_move == 2);
    jog(-4);
    e = F - 4 * st;
    CHECKF(M.smp.t_end == e && !count(RFM_MOVE_ACT_SMP_PEAKS), "end %u (want %u), %d asked", M.smp.t_end, e,
           count(RFM_MOVE_ACT_SMP_PEAKS));              /* (near the end: the view stays at the take's end, asked) */
    button(MR_CC_SHIFT, 1);                             /* Shift: fine (the app's: nothing under it covers it) */
    CHECK(M.smp.shdown && M.btn[MR_CC_SHIFT].kind == H_NONE && rfm_move_smp_shown(&M));
    jog(-3);
    CHECK(M.smp.t_end == e - 3 * (st / 16) && last(RFM_MOVE_ACT_SMP_PEAKS)->count == 64 * (st / 16) &&
          last(RFM_MOVE_ACT_SMP_PEAKS)->from == M.smp.t_end - 32 * (st / 16));   /* (64 fine steps about it) */
    leds(lw);
    CHECK(lw[L_WHITE0 + W_SHIFT][0] == 127);
    serve();
    dump("trim_fine");
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, End, 7.869 seconds"), "%s", w);   /* (347030 frames) */
    screen();
    CHECK(pixels(0, 0, 90, 14) > 60);                   /* ("7.869 s": to the ms while Shift is held) */
    button(MR_CC_SHIFT, 0);
    e = M.smp.t_end;
    back();                                             /* Back: the move ends, Trim stays */
    CHECK(M.smp.scr == 2 && !M.smp.t_move);
    jog(1);
    clear();
    jog_click();                                        /* Play: the kept part */
    CHECK(count(RFM_MOVE_ACT_SMP_PLAY) == 1 && last(RFM_MOVE_ACT_SMP_PLAY)->value == 1 &&
          last(RFM_MOVE_ACT_SMP_PLAY)->from == 3 * st && last(RFM_MOVE_ACT_SMP_PLAY)->count == e - 3 * st);
    jog_click();
    CHECK(last(RFM_MOVE_ACT_SMP_PLAY)->value == 0);
    jog(1);
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Keep"), "%s", w);
    clear();
    jog_click();                                        /* Keep: the cut, sample-exact; back to the actions */
    CHECK(count(RFM_MOVE_ACT_SMP_EDIT) == 1 && last(RFM_MOVE_ACT_SMP_EDIT)->index == RFM_MOVE_SMP_OP_TRIM &&
          last(RFM_MOVE_ACT_SMP_EDIT)->take == t->id && last(RFM_MOVE_ACT_SMP_EDIT)->from == 3 * st &&
          last(RFM_MOVE_ACT_SMP_EDIT)->count == e - 3 * st && M.smp.scr == 1 && !strcmp(M.smp.msg_a, "Trimmed"));
    nr = *t, nr.frames = e - 3 * st, nr.bars = 0;
    clear();
    CHECK(rfm_move_smp_done(&M, RFM_MOVE_SMP_OP_TRIM, 1, &nr) == 1);
    CHECK(M.smp.cur.frames == nr.frames && count(RFM_MOVE_ACT_SMP_PEAKS) == 1 &&
          last(RFM_MOVE_ACT_SMP_PEAKS)->count == nr.frames && count(RFM_MOVE_ACT_SMP_ROWS) == 0);   /* (the list:
                                                         * asked when it shows) */
    serve();
    rest();
    jog_click();                                        /* (Trim again) Back: nothing cut */
    CHECK(M.smp.scr == 2 && M.smp.t_end == nr.frames);
    clear();
    knob(0, 10), knob(1, -10), back();
    CHECK(M.smp.scr == 1 && !count(RFM_MOVE_ACT_SMP_EDIT));
    jog_click(), jog(3), jog_click();                   /* (Keep untouched: nothing to cut) */
    CHECK(M.smp.scr == 1 && !count(RFM_MOVE_ACT_SMP_EDIT));
    /* the mock's trim board: End chosen, the whole take (103000 frames), the part kept 9..101 of 128, "1.84 s" */
    jog_click();
    M.smp.cur.frames = 103000, M.smp.cur.rate = 44100, M.smp.t_start = 7200, M.smp.t_end = 81200;
    M.smp.t_i = 1, M.smp.t_move = 0;
    mock_peaks(t->id, 0, 103000, 13, 61u);
    rfm_move_smp_peaks(&M, t->id, 0, 103000, 64, OVR.pk);
    rest();
    out = vs_ref("trim", NULL, 0, &in);
    CHECKF(out == 0, "trim: %d differ", out);
    printf("mock trim: %d pixels differ\n", out);
    dump("trim");
}

/* 26. the Tools tab: the take chosen in Takes, its wave, the icons; Normalize, Reverse at the click; a Fade's popup
 * (the jog its length, a click applies, Back not); Trim and back; a stems take: no tools */
static void t2_tools(void)
{
    const rfm_move_smp_row_t *t;
    char w[96];
    takes_app();
    to_tab(2);                                          /* (no take chosen yet: the newest) */
    CHECK(M.smp.tab == RFM_MOVE_SMP_TAB_TOOLS && !M.smp.on_tabs && M.smp.cur.id == FD[0].t[0].id);
    to_tab(1);
    jog(1), jog_click(), serve(), back();               /* (a take chosen: its actions opened) */
    t = &FD[0].t[1];
    to_tab(2);
    CHECK(M.smp.tab == RFM_MOVE_SMP_TAB_TOOLS && !M.smp.on_tabs && M.smp.cur.id == t->id && M.smp.ti == 0);
    screen();
    CHECK(pixels(0, 0, 128, 24) > 100 && pixels(8, 26, 15, 15) > 150);   /* (the wave; Trim chosen) */
    dump("tools");
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Trim"), "%s", w);
    jog(1);
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Normalize"), "%s", w);
    clear();
    jog_click();
    CHECK(count(RFM_MOVE_ACT_SMP_EDIT) == 1 && last(RFM_MOVE_ACT_SMP_EDIT)->index == RFM_MOVE_SMP_OP_NORMALIZE &&
          last(RFM_MOVE_ACT_SMP_EDIT)->take == t->id && !strcmp(M.smp.msg_a, "Normalized"));
    dump("normalized");
    rest(), jog(1), clear(), jog_click();
    CHECK(last(RFM_MOVE_ACT_SMP_EDIT)->index == RFM_MOVE_SMP_OP_REVERSE && !strcmp(M.smp.msg_a, "Reversed"));
    rest(), jog(1), clear(), jog_click();
    CHECK(M.smp.fade == 1 && !count(RFM_MOVE_ACT_SMP_EDIT));
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Fade in: 100 ms"), "%s", w);
    jog(2);
    CHECK(rfm_move_smp_value(&M, 0) >= 0);
    screen();
    CHECK(pixels(0, 0, 128, 18) > 30 && pixels(0, 20, 128, 40) > 30);   /* (its name, its length) */
    dump("fade");
    back();
    CHECK(!M.smp.fade && !count(RFM_MOVE_ACT_SMP_EDIT) && M.smp.open);
    jog_click(), jog(-1);
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Fade in: 200 ms"), "%s", w);   /* (its length remembered: 500, one down) */
    jog_click();
    CHECK(count(RFM_MOVE_ACT_SMP_EDIT) == 1 && last(RFM_MOVE_ACT_SMP_EDIT)->index == RFM_MOVE_SMP_OP_FADE_IN &&
          last(RFM_MOVE_ACT_SMP_EDIT)->value == 200 && !strcmp(M.smp.msg_a, "Faded In"));
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Faded in"), "%s", w);
    rest(), jog(1), jog_click(), jog_click();
    CHECK(last(RFM_MOVE_ACT_SMP_EDIT)->index == RFM_MOVE_SMP_OP_FADE_OUT && last(RFM_MOVE_ACT_SMP_EDIT)->value == 100);
    rest(), jog(-10);
    CHECK(M.smp.on_tabs);
    screen();
    CHECK(pixels(18, 0, 16, 14) > 20 && pixels(36, 0, 16, 14) > 60);   /* (the tabs replace the wave) */
    jog(1), jog_click();                                /* Trim from Tools: Back is Tools again */
    CHECK(M.smp.scr == 2 && M.smp.trim_ret == 0 && M.smp.cur.id == t->id);
    back();
    CHECK(M.smp.scr == 0 && M.smp.tab == RFM_MOVE_SMP_TAB_TOOLS);
    to_tab(1);                                          /* a stems take chosen in Takes: no tools */
    jog(5), serve();
    CHECK(M.smp.sel == 5);
    jog_click(), serve(), back();
    to_tab(2);
    CHECK(M.smp.cur.kind == RFM_MOVE_SMP_ROW_STEMS);
    clear();
    jog_click();
    CHECK(!nact && M.smp.scr == 0);
    screen();
    CHECK(pixels(0, 26, 128, 15) == 0);                 /* (no icons) */
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Stems"), "%s", w);
}

/* 27. Delete held + the jog's click; the host's trash and its one Undo; Delete from a take's actions; Undo is the
 * layer's in the Record tab; a host's failure */
static void t2_delete(void)
{
    rfm_move_smp_row_t gone;
    char w[96];
    int i;
    takes_app();
    jog(1);
    gone = FD[0].t[1];
    button(MR_CC_DELETE, 1);
    CHECK(M.smp.ddown && M.btn[MR_CC_DELETE].kind == H_NONE && rfm_move_smp_shown(&M) && white(W_DELETE) == 127);
    jog_click();
    CHECK(count(RFM_MOVE_ACT_SMP_EDIT) == 1 && last(RFM_MOVE_ACT_SMP_EDIT)->index == RFM_MOVE_SMP_OP_DELETE &&
          last(RFM_MOVE_ACT_SMP_EDIT)->take == gone.id && M.smp.scr == 0 && !strcmp(M.smp.msg_a, "Deleted"));
    dump("deleted");
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Deleted"), "%s", w);
    button(MR_CC_DELETE, 0);
    CHECK(!M.smp.ddown && M.btn[MR_CC_DELETE].kind == H_NONE);
    for (i = 1; i < FD[0].nt - 1; i++)                  /* the host: into its trash, one undo */
        FD[0].t[i] = FD[0].t[i + 1];
    FD[0].nt--;
    memset(&ST, 0, sizeof ST);
    ST.undo = 1;
    status(RFM_MOVE_SMP_ST_IDLE);
    clear();
    rfm_move_smp_done(&M, RFM_MOVE_SMP_OP_DELETE, 1, NULL);
    CHECK(count(RFM_MOVE_ACT_SMP_ROWS) == 1 && !M.smp.known);   /* (the list asked again) */
    serve();
    CHECK(M.smp.total == 5 && M.smp.sel == 1);
    rest();
    CHECK(white(W_UNDO) == 60);
    clear();
    press(MR_CC_UNDO);
    CHECK(count(RFM_MOVE_ACT_SMP_UNDO) == 1 && !strcmp(M.smp.msg_a, "Undo"));
    for (i = FD[0].nt; i > 1; i--)                      /* the host: it is back */
        FD[0].t[i] = FD[0].t[i - 1];
    FD[0].t[1] = gone, FD[0].nt++;
    ST.undo = 0;
    status(RFM_MOVE_SMP_ST_IDLE);
    rfm_move_smp_done(&M, RFM_MOVE_SMP_OP_UNDO, 1, &gone);
    serve();
    CHECK(M.smp.total == 6 && white(W_UNDO) == 0);
    clear();
    press(MR_CC_UNDO);                                  /* (nothing to undo: dark, dead) */
    CHECK(!count(RFM_MOVE_ACT_SMP_UNDO) && !nact);
    jog_click(), jog(2);                                /* from the actions: Delete */
    clear();
    jog_click();
    CHECK(count(RFM_MOVE_ACT_SMP_EDIT) == 1 && last(RFM_MOVE_ACT_SMP_EDIT)->take == gone.id && M.smp.scr == 0 &&
          M.smp.tab == RFM_MOVE_SMP_TAB_TAKES);
    clear();
    rfm_move_smp_done(&M, RFM_MOVE_SMP_OP_DELETE, 0, NULL);   /* the host could not */
    CHECK(!strcmp(M.smp.msg_a, "Not done"));
    rest();
    to_tab(0);                                          /* the Record tab: Undo is the layer's under it */
    ST.undo = 1;
    status(RFM_MOVE_SMP_ST_IDLE);
    clear();
    press(MR_CC_UNDO);
    CHECK(!count(RFM_MOVE_ACT_SMP_UNDO) && !M.smp.udown);
    button(MR_CC_DELETE, 1);                            /* .. and Delete (held: over the app, R3) */
    CHECK(!M.smp.ddown && rfmv_smp_covered(&M));
    button(MR_CC_DELETE, 0);
}

/* 28. the layer rules: pads, steps and knobs play under Takes and Tools as with the app closed; in Trim the pads and
 * steps do, the knobs are dead */
static void gestures2(int knobs)
{
    hit(10);
    if (knobs)
        knob(0, 3), knob(5, -2);
    step(4, 1), step(4, 0);
    pad(5, 90), adv_ms(50), pad(5, 0);
}
static void t2_layers(void)
{
    static char a[4096], b[4096], c[4096], d[4096], e[4096];
    note_view();
    M.pop = 0;
    gestures2(1);
    log_kinds(a, sizeof a);
    CHECK(M.pop != 0);                                  /* (closed: a knob's turn shows its popup) */
    note_view();
    gestures2(0);
    log_kinds(d, sizeof d);
    takes_app();
    gestures2(1);
    log_kinds(b, sizeof b);
    CHECKF(!strcmp(a, b) && a[0], "closed %s / Takes %s", a, b);
    takes_app();
    to_tab(2);
    clear();
    gestures2(1);
    log_kinds(c, sizeof c);
    CHECKF(!strcmp(a, c), "closed %s / Tools %s", a, c);
    takes_app();
    jog_click(), serve(), jog(1), jog_click();
    CHECK(M.smp.scr == 2);
    clear();
    M.pop = 0;
    gestures2(1);
    log_kinds(e, sizeof e);
    CHECKF(!strcmp(d, e), "closed, no knobs %s / Trim %s", d, e);
    CHECK(M.smp.t_start > 0 && M.pop == 0 && rfm_move_smp_shown(&M));   /* (knob 1 moved the start; knob 6 nothing,
                                                         * no popup of the layer under it) */
}

/* 29. after a take is saved: a Capture's preview (its actions, Play chosen, played when the song is stopped), Trim
 * after a Manual or Threshold take, nothing after Bars, nor with the app closed, nor for a host without Takes */
static void t2_auto(void)
{
    rfm_move_smp_row_t r;
    char w[96];
    note_view();
    fake_db();
    rfm_move_smp_offer(&M, 0, 0, 0x7, 0);
    sample();
    clear();
    r = FD[0].t[0];
    M.s.smp[RFM_MOVE_SMP_MODE] = RFM_MOVE_SMP_M_CAPTURE;
    rfm_move_smp_event(&M, RFM_MOVE_SMP_EV_SAVED, 4, 1200, 8000, NULL);
    CHECK(rfm_move_smp_saved(&M, &r, RFM_MOVE_SMP_M_CAPTURE) == 1);
    CHECK(M.smp.scr == 1 && M.smp.cur.id == r.id && M.smp.ai == 0 && M.smp.tab == RFM_MOVE_SMP_TAB_RECORD &&
          count(RFM_MOVE_ACT_SMP_PLAY) == 1 && last(RFM_MOVE_ACT_SMP_PLAY)->value == 1 &&
          last(RFM_MOVE_ACT_SMP_PLAY)->take == r.id && M.smp.folder == r.folder && M.smp.sel == 0);
    screen();
    CHECK(pixels(0, 12, 128, 14) > 50);                 /* ("Captured" first, its second) */
    rest(), serve();
    screen();
    CHECK(pixels(8, 26, 15, 15) > 150);                 /* (then the actions, Play chosen) */
    dump("preview");
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, Play"), "%s", w);
    clear();
    back();                                             /* Back: the tab it came over (Record), the play stopped */
    CHECK(M.smp.scr == 0 && M.smp.tab == RFM_MOVE_SMP_TAB_RECORD && M.smp.open &&
          last(RFM_MOVE_ACT_SMP_PLAY)->value == 0);
    press(MR_CC_PLAY), adv_ms(30);                      /* the song running: shown, not played over it */
    CHECK(M.seq.playing);
    clear();
    rfm_move_smp_saved(&M, &r, RFM_MOVE_SMP_M_CAPTURE);
    CHECK(M.smp.scr == 1 && !count(RFM_MOVE_ACT_SMP_PLAY));
    press(MR_CC_PLAY), adv_ms(30), back();
    CHECK(!M.seq.playing);
    clear();
    rfm_move_smp_saved(&M, &r, RFM_MOVE_SMP_M_MANUAL);  /* Manual: Trim */
    CHECK(M.smp.scr == 2 && M.smp.trim_ret == 0 && M.smp.cur.id == r.id && M.smp.t_end == r.frames &&
          M.smp.tab == RFM_MOVE_SMP_TAB_RECORD && !count(RFM_MOVE_ACT_SMP_PLAY));
    serve();
    dump("auto_trim");
    back();
    CHECK(M.smp.scr == 0 && M.smp.tab == RFM_MOVE_SMP_TAB_RECORD);
    rfm_move_smp_saved(&M, &r, RFM_MOVE_SMP_M_THRESHOLD);   /* Threshold: Trim; Keep with nothing cut: back */
    CHECK(M.smp.scr == 2);
    jog(3), clear(), jog_click();
    CHECK(M.smp.scr == 0 && !count(RFM_MOVE_ACT_SMP_EDIT) && M.smp.tab == RFM_MOVE_SMP_TAB_RECORD);
    rfm_move_smp_saved(&M, &r, RFM_MOVE_SMP_M_BARS);    /* Bars: nothing */
    CHECK(M.smp.scr == 0);
    rfm_move_smp_saved(&M, &FD[0].t[5], RFM_MOVE_SMP_M_MANUAL);   /* a stems take, Manual: no Trim for stems, nothing */
    CHECK(M.smp.scr == 0);
    memset(&ST, 0, sizeof ST);                          /* (the host's last status still SAVING: Trim all the same, */
    status(RFM_MOVE_SMP_ST_SAVING);                     /* shown once it says IDLE) */
    rfm_move_smp_saved(&M, &r, RFM_MOVE_SMP_M_MANUAL);
    CHECK(M.smp.scr == 2);
    status(RFM_MOVE_SMP_ST_IDLE);
    screen();
    CHECK(pixels(10, 48, 15, 15) > 150);                /* (Trim's icons: Start chosen) */
    back();
    status(RFM_MOVE_SMP_ST_RECORDING);                  /* (a new take recording: nothing comes over it) */
    rfm_move_smp_saved(&M, &r, RFM_MOVE_SMP_M_CAPTURE);
    CHECK(M.smp.scr == 0);
    status(RFM_MOVE_SMP_ST_IDLE);
    back();
    CHECK(!M.smp.open);
    clear();
    rfm_move_smp_saved(&M, &r, RFM_MOVE_SMP_M_CAPTURE);   /* the app closed: nothing */
    rfm_move_smp_saved(&M, &r, RFM_MOVE_SMP_M_MANUAL);
    CHECK(M.smp.scr == 0 && !count(RFM_MOVE_ACT_SMP_PLAY));
    sample();
    CHECK(M.smp.open && M.smp.scr == 0 && M.smp.tab == RFM_MOVE_SMP_TAB_RECORD);
    note_view();                                        /* a host that offers only Record: nothing at all */
    sample();
    clear();
    CHECK(rfm_move_smp_saved(&M, &r, RFM_MOVE_SMP_M_CAPTURE) == 0 && !nact && M.smp.scr == 0);
    CHECK(rfm_move_smp_rows(&M, 0, 0, 3, 0, 0, NULL) == 0 && rfm_move_smp_done(&M, 1, 1, NULL) == 0);
    rfm_move_smp_changed(&M);
    press(MR_CC_UNDO), press(MR_CC_LEFT);
    CHECK(!nact || !count(RFM_MOVE_ACT_SMP_ROWS));
}

/* 30. no takes yet: Takes and Tools say so; a take saved meanwhile: the list asked again */
static void t2_none(void)
{
    char w[96];
    note_view();
    fake_db();
    FD[0].nt = FD[1].nt = FD[2].nt = 0, nfd = 0;
    rfm_move_smp_offer(&M, 0, 0, 0x7, 0);
    sample();
    to_tab(1);
    CHECK(M.smp.known && !M.smp.total && !M.smp.folder);
    screen();
    CHECK(pixels(0, 26, 128, 16) > 50);
    dump("no_takes");
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, No takes"), "%s", w);
    clear();
    jog_click();
    CHECK(!nact && M.smp.scr == 0);
    to_tab(2);
    words(w, sizeof w);
    CHECKF(!strcmp(w, "Sampler, No takes"), "%s", w);
    to_tab(1);
    fake_db();
    clear();
    rfm_move_smp_changed(&M);
    CHECK(count(RFM_MOVE_ACT_SMP_ROWS) == 1 && last(RFM_MOVE_ACT_SMP_ROWS)->take == RFM_MOVE_SMP_NEWEST);
    serve();
    CHECK(M.smp.total == 6 && M.smp.folder == DAY0);
}

int main(void)
{
    t_cap_off();
    t_caps_sync();
    t_open();
    t_rows();
    t_popup();
    t_tabs();
    t_track_source();
    t_layer();
    t_knob_popup();
    t_take();
    t_events();
    t_fm1();
    t_browser();
    t_takeover();
    t_menus();
    t_offer();
    t_blob();
    t_speech();
    t_listen();
    t_mock();
    t_scale();
    t2_takes();
    t2_days();
    t2_act();
    t2_trim();
    t2_tools();
    t2_delete();
    t2_layers();
    t2_auto();
    t2_none();
    return check_done("move_sampler");
}
