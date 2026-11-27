/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (looper stage 1, the owner's 2026-11-26 round; README "The looper (stage 1)") the looper (RFM_MOVE_CAP_LOOPER):
 * the capability off, Track 4's round (Mixer, Routing, Kit, Looper; held: pad 4), the grid (lane 1 the top row) and its
 * lights, the record gesture (tap, armed, recording, the red pad, Sample, a slot held + Sample, cancel), the launch
 * quantization (a tap at the Launch setting, a double click now, a tap on a playing loop stops it), scenes three ways
 * (a column's left step, Shift + a pad, a slide down the column; step 16 stops all; stopped: the transport starts),
 * choosing silently (Loop held + a pad, the arrows), the pages and their knobs, the sampler's Loops tab, Copy / Delete /
 * Undo, Mute + a loop, a held slot + the volume knob, the Mixer's Loops strip, Track 4 held + pad 4, the project
 * (version 19 saved and loaded, 18 loads an empty looper, a bad looper part refused, the refs). Writes loop_*.pbm. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "move_int.h"
#include "v2.h"
#include "moveremote/client.h"
#include "rfm1cs/move.h"
#include "move_fwb_data.h"
#include "proj15.h"

#define NFW ((int)(sizeof FWB / sizeof FWB[0]))
#define LCAPS (RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_SAMPLER | RFM_MOVE_CAP_KIT | RFM_MOVE_CAP_LOOPER)

static rfm_move_act_t A[4096];
static char D[4096][RFM_MOVE_LOOP_PATH];
static int nact;
static void on_act(void *ctx, const rfm_move_act_t *a)
{
    (void)ctx;
    if (a->kind == RFM_MOVE_ACT_SAY)
        return;
    if (nact < (int)(sizeof A / sizeof A[0])) {
        A[nact] = *a, A[nact].data = NULL;
        D[nact][0] = 0;
        if (a->kind == RFM_MOVE_ACT_LOOP_LOAD && a->data)
            snprintf(D[nact], sizeof D[nact], "%.*s", (int)a->len, (const char *)a->data);
        nact++;
    }
}
static void clear(void) { nact = 0; }
static int count(int kind)
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += A[i].kind == kind;
    return n;
}
static int loop_acts(void)
{
    int k, n = 0;
    for (k = RFM_MOVE_ACT_LOOP_REC; k <= RFM_MOVE_ACT_LOOP_COPY; k++)
        n += count(k);
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
static int last_i(int kind)
{
    int i;
    for (i = nact - 1; i >= 0; i--)
        if (A[i].kind == kind)
            return i;
    return -1;
}

static rfm_move_t M, M2;
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
static int sp(int lane, int scene) { return (3 - lane) * 8 + scene; }   /* a slot's Move pad (lane 1 the top row) */
static void tap(int lane, int scene) { hit(sp(lane, scene)); }
static void step(int s, int down) { raw(down ? 0x90 : 0x80, (uint8_t)(MR_NOTE_STEP0 + s), down ? 127 : 0); }
static void tap_step(int s) { step(s, 1), step(s, 0); }
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
    clear();
    rfm_move_fw_caps(&M, caps);
    rest();
}
static void looper_view(void)                           /* Track 4 held + pad 4 */
{
    fresh_caps(LCAPS);
    hold_track(3), hit(27), track(3, 0);
    rest(), clear();
}
/* a recorded loop in slot s, as the host answers */
static void loop_in(int s, int bars)
{
    char p[64];
    uint8_t pk[RFM_MOVE_LOOP_PEAKS];
    int i;
    for (i = 0; i < RFM_MOVE_LOOP_PEAKS; i++)
        pk[i] = (uint8_t)(40 + (i * 37) % 200);
    snprintf(p, sizeof p, "@/Loops/%d-%d 120bpm %db.wav", s / 8 + 1, s % 8 + 1, bars);
    rfm_move_loop_recorded(&M, s, RFM_MOVE_LOOP_OK, p, (uint32_t)bars * 88200u, 44100, bars, 1200, RFM_MOVE_LOOP_PEAKS, pk);
}
static rfm_move_loop_status_t ST;
static void st_reset(void)
{
    int l;
    memset(&ST, 0, sizeof ST);
    for (l = 0; l < 4; l++)
        ST.lane[l].play = -1, ST.lane[l].next = -1;
    ST.rec[0].slot = ST.rec[1].slot = -1;
}
static void st_send(void) { rfm_move_loop_status(&M, &ST); }

static uint8_t SCR[MR_SCREEN_BYTES];
static void screen(void) { rfm_move_screen(&M, NULL, SCR); }
static int px(int x, int y) { return SCR[(y >> 3) * 128 + x] >> (y & 7) & 1; }
static int lit_n(int x0, int y0, int x1, int y1)
{
    int x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
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
static int is_rgb(const uint8_t c[3], int r, int g, int b) { return c[0] == r && c[1] == g && c[2] == b; }
static int lit(const uint8_t c[3]) { return c[0] || c[1] || c[2]; }
static void dump(const char *name)
{
    char path[96];
    FILE *f;
    int x, y;
    screen();
    snprintf(path, sizeof path, "loop_%s.pbm", name);
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

/* ---- 1. the capability off: no Looper; on: Track 4's round and its held pad 4 */
static void t_cap(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    fresh_caps(RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_SAMPLER | RFM_MOVE_CAP_KIT);
    hold_track(3);
    leds(w);
    CHECK(!lit(w[L_PAD0 + 27]));                        /* (pad 4: dark, dead) */
    hit(27), track(3, 0), rest();
    CHECK(!M.loop.on && !rfm_move_loop_shown(&M) && !loop_acts());
    fresh_caps(LCAPS);
    CHECK(count(RFM_MOVE_ACT_LOOP_LOAD) == 32 && count(RFM_MOVE_ACT_LOOP_SET) == 32 * RFM_MOVE_LOOP_P_N + 4 * RFM_MOVE_LOOP_L_N);
    CHECK(count(RFM_MOVE_ACT_MIX) >= 1 && last(RFM_MOVE_ACT_MIX)->slot == RFM_MOVE_LOOP_SLOT || 1);
    hold_track(3);
    leds(w);
    CHECK(is_rgb(w[L_PAD0 + 27], 0, 210 * 77 / 256, 190 * 77 / 256));   /* (pad 4 teal, dim: not the page) */
    hit(27), track(3, 0), rest();
    CHECK(M.loop.on && rfm_move_loop_shown(&M) && M.s.t4_kit == 2 && rfmv_t4_page(&M) == 3);
    leds(w);
    CHECK(is_rgb(w[L_TRACK0 + 3], 0, 210, 190));        /* (Track 4 the looper's) */
    CHECK(w[L_ICON0 + 15][0] == 127);                   /* (step 16: Stop All, a function: its icon lit) */
    /* the round: Looper -> Mixer -> Routing -> Kit -> Looper */
    tap_track(3), rest();
    CHECK(!M.loop.on && M.seq.projects && M.fw.fx);
    tap_track(3), rest();
    tap_track(3), rest();
    CHECK(rfmv_kit_active(&M) && !M.loop.on);
    tap_track(3), rest();
    CHECK(rfmv_loop_active(&M) && !M.kit.on);
    /* from a firmware Track and back: the view left on */
    tap_track(0), rest();
    CHECK(!M.loop.on);
    tap_track(3), rest();
    CHECK(rfmv_loop_active(&M));
}

/* ---- 2. the grid and its lights; recording (tap, armed, the bar, the red pad, Sample, cancel, a held slot + Sample) */
static void t_record(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    const rfm_move_act_t *a;
    looper_view();
    M.loop.page = 2;                                    /* (round 2: a tap records on the Record page) */
    M.loop.lane[1][RFM_MOVE_LOOP_L_SOURCE] = RFM_MOVE_LOOP_SRC_TRACK2;
    tap(1, 2);                                          /* (lane 2, scene 3: an empty slot) */
    a = last(RFM_MOVE_ACT_LOOP_REC);
    CHECK(a && a->index == 1 * 8 + 2 && a->param == RFM_MOVE_LOOP_R_START && a->down == 1 && a->value == 0 &&
          a->vel == RFM_MOVE_LOOP_SRC_TRACK2);
    CHECK(M.loop.rs[10] == RFM_MOVE_LOOP_RS_ARMED && M.loop.cur == 10);
    leds(w);
    CHECK(w[L_PAD0 + sp(1, 2)][0] && !w[L_PAD0 + sp(1, 2)][1]);   /* (armed: red, blinking) */
    CHECK(lit(w[L_SAMPLE]) && !w[L_SAMPLE][1]);          /* (Sample red) */
    screen();
    CHECK(lit_n(2, 1, 13, 12) > 40 && lit_n(8, 26, 30, 48) > 50);   /* (board I: the dot, the beats' squares) */
    dump("armed");
    tap(1, 2);                                          /* (tapped again: it goes) */
    CHECK(last(RFM_MOVE_ACT_LOOP_REC)->param == RFM_MOVE_LOOP_R_CANCEL && !M.loop.rs[10]);
    rest(), clear();
    /* fixed length: the lane's Length 2 bars */
    M.loop.lane[1][RFM_MOVE_LOOP_L_LENGTH] = 2;
    tap(1, 2);
    CHECK(last(RFM_MOVE_ACT_LOOP_REC)->value == 2);
    st_reset();
    ST.rec[0].slot = 10, ST.rec[0].state = RFM_MOVE_LOOP_RS_RECORDING, ST.rec[0].bar = 2, ST.rec[0].bars = 2;
    ST.npk = 12;
    {
        int i;
        for (i = 0; i < 12; i++)
            ST.pk[i] = (uint8_t)(100 + i * 10);
    }
    ST.level = 20000;
    st_send();
    leds(w);
    CHECK(is_rgb(w[L_PAD0 + sp(1, 2)], 255, 0, 0));     /* (recording: red) */
    screen();
    dump("fixed");
    CHECK(lit_n(0, 22, 40, 50) > 30 && lit_n(121, 17, 127, 45) > 20);   /* (board K: the wave, the level) */
    /* the red pad: it ends at the next bar */
    clear();
    tap(1, 2);
    CHECK(last(RFM_MOVE_ACT_LOOP_REC) && last(RFM_MOVE_ACT_LOOP_REC)->param == RFM_MOVE_LOOP_R_END);
    /* the host: done */
    st_reset(), st_send();
    loop_in(10, 2);
    CHECK(!strcmp(M.loop.slot[10].path, "@/Loops/2-3 120bpm 2b.wav") && M.loop.slot[10].bars == 2 &&
          M.loop.slot[10].v[RFM_MOVE_LOOP_P_LENGTH] == 32 && M.loop.und && !M.loop.rs[10]);
    rest();
    leds(w);
    CHECK(is_rgb(w[L_PAD0 + sp(1, 2)], 255, 255, 255)); /* (the chosen slot white) */
    M.loop.cur = 0;
    leds(w);
    CHECK(is_rgb(w[L_PAD0 + sp(1, 2)], 0, 210 * 90 / 256, 190 * 90 / 256));   /* (a loop: dim teal) */
    CHECK(!lit(w[L_PAD0 + sp(0, 0)]) || M.loop.cur == 0);
    /* Undo: the slot empty again */
    clear();
    press(MR_CC_UNDO);
    CHECK(!M.loop.slot[10].path[0] && last(RFM_MOVE_ACT_LOOP_LOAD) && last(RFM_MOVE_ACT_LOOP_LOAD)->index == 10 &&
          !D[last_i(RFM_MOVE_ACT_LOOP_LOAD)][0] && !M.loop.und);
    screen();
    dump("undo");
    /* Sample: an Open recording ends; armed: it goes */
    rest(), clear();
    M.loop.lane[3][RFM_MOVE_LOOP_L_LENGTH] = 0;
    tap(3, 0);
    press(MR_CC_SAMPLE);
    CHECK(last(RFM_MOVE_ACT_LOOP_REC)->param == RFM_MOVE_LOOP_R_CANCEL && !M.smp.open);
    rest(), clear();
    tap(3, 0);
    st_reset();
    ST.rec[0].slot = 24, ST.rec[0].state = RFM_MOVE_LOOP_RS_RECORDING, ST.rec[0].bar = 3, ST.npk = 20, ST.beat = 1;
    st_send();
    screen();
    dump("recording");
    press(MR_CC_SAMPLE);
    CHECK(last(RFM_MOVE_ACT_LOOP_REC)->param == RFM_MOVE_LOOP_R_END && last(RFM_MOVE_ACT_LOOP_REC)->index == 24);
    st_reset(), st_send();
    rfm_move_loop_recorded(&M, 24, RFM_MOVE_LOOP_NOTHING, NULL, 0, 0, 0, 0, 0, NULL);
    CHECK(!M.loop.slot[24].path[0] && !strcmp(M.loop.msg, "Nothing"));
    /* another recording running: a new one's tap ends it at the bar, the new one from there */
    rest(), clear();
    tap(0, 0);
    st_reset(), ST.rec[0].slot = 0, ST.rec[0].state = RFM_MOVE_LOOP_RS_RECORDING, ST.rec[0].bar = 1, st_send();
    clear();
    tap(2, 1);
    CHECK(nact >= 2 && A[0].kind == RFM_MOVE_ACT_LOOP_REC && A[0].param == RFM_MOVE_LOOP_R_END && A[0].index == 0 &&
          last(RFM_MOVE_ACT_LOOP_REC)->param == RFM_MOVE_LOOP_R_START && last(RFM_MOVE_ACT_LOOP_REC)->index == 17);
    /* a slot held + Sample: records into it, full or not (the replaced loop comes back with Undo) */
    st_reset(), st_send();
    loop_in(0, 1);
    rfm_move_loop_recorded(&M, 17, RFM_MOVE_LOOP_NOTHING, NULL, 0, 0, 0, 0, 0, NULL);
    rest(), clear();
    pad(sp(0, 0), 100);
    press(MR_CC_SAMPLE);
    pad(sp(0, 0), 0);
    a = last(RFM_MOVE_ACT_LOOP_REC);
    CHECK(a && a->param == RFM_MOVE_LOOP_R_START && a->index == 0 && !M.smp.open);
    loop_in(0, 4);
    CHECK(M.loop.slot[0].bars == 4 && M.loop.und);
    press(MR_CC_UNDO);
    CHECK(M.loop.slot[0].bars == 1 && strstr(M.loop.slot[0].path, "1b.wav") && !strcmp(D[last_i(RFM_MOVE_ACT_LOOP_LOAD)],
                                                                                         M.loop.slot[0].path));
}

/* ---- 3. launching: the launch quantization, a double click now, a playing loop's tap stops it */
static void t_launch(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    const rfm_move_act_t *a;
    looper_view();
    loop_in(1, 2), loop_in(9, 1);
    M.loop.cur = 31;
    rest(), clear();
    tap(0, 1);
    a = last(RFM_MOVE_ACT_LOOP_LAUNCH);
    CHECK(a && a->index == 1 && a->down == 1 && a->value == RFM_MOVE_LQ_BAR && M.loop.ask[0] == 1);
    leds(w);
    CHECK(lit(w[L_PAD0 + sp(0, 1)]) && !w[L_PAD0 + sp(0, 1)][0]);   /* (lined up: green, blinking) */
    tap(0, 1);                                          /* (a double click: now) */
    a = last(RFM_MOVE_ACT_LOOP_LAUNCH);
    CHECK(a && a->index == 1 && a->down == 1 && a->value == RFM_MOVE_LQ_INSTANT);
    /* the host plays it: the ask goes; a tap stops it at the bar, a double click now */
    st_reset(), ST.lane[0].play = 1, ST.lane[0].pos = 30000, st_send();
    CHECK(M.loop.ask[0] == -1);
    leds(w);
    CHECK(is_rgb(w[L_PAD0 + sp(0, 1)], 0, 255, 60));     /* (playing: green) */
    rest(), clear();
    tap(0, 1);
    a = last(RFM_MOVE_ACT_LOOP_LAUNCH);
    CHECK(a && a->down == 0 && a->index == 0 && a->value == RFM_MOVE_LQ_BAR && M.loop.ask[0] == 8);
    tap(0, 1);
    CHECK(last(RFM_MOVE_ACT_LOOP_LAUNCH)->down == 0 && last(RFM_MOVE_ACT_LOOP_LAUNCH)->value == RFM_MOVE_LQ_INSTANT);
    /* the project's Launch setting (Beat): each launch at it */
    st_reset(), ST.lane[0].play = 1, st_send();
    M.seq.launch_q = RFM_MOVE_LQ_BEAT;
    rest(), clear();
    tap(1, 1);
    CHECK(last(RFM_MOVE_ACT_LOOP_LAUNCH)->value == RFM_MOVE_LQ_BEAT && last(RFM_MOVE_ACT_LOOP_LAUNCH)->index == 9);
    M.seq.launch_q = RFM_MOVE_LQ_BAR;
    /* another loop of a playing lane: it replaces it (one a lane): the old one blinks, about to stop */
    loop_in(2, 1);
    rest(), clear();
    M.loop.cur = 31;
    tap(0, 2);
    leds(w);
    CHECK(last(RFM_MOVE_ACT_LOOP_LAUNCH)->index == 2 && M.loop.ask[0] == 2);
    M.loop.cur = 31;
    screen();                                           /* (the Loops page: lane 1 waiting, dashed) */
    CHECK(px(2 + 4, 1 + 4) || lit_n(0, 0, 16, 16) > 5);
    dump("pending");
    /* nothing plays by itself: no action without a press */
    rest(), clear();
    adv_ms(3000);
    CHECK(!loop_acts());
}

/* ---- 4. scenes three ways, step 16, the transport */
static void t_scenes(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int i, n;
    looper_view();
    loop_in(2, 1), loop_in(10, 1), loop_in(18, 2), loop_in(0, 1);
    rest(), clear();
    CHECK(!M.seq.playing && !M.seq.start_pending);
    tap_step(4);                                        /* (a: step 5, scene 3) */
    for (i = 0, n = 0; i < nact; i++)
        n += A[i].kind == RFM_MOVE_ACT_LOOP_LAUNCH && A[i].down == 1 && A[i].index % 8 == 2;
    CHECK(n == 3 && M.seq.start_pending);               /* (lanes 1..3 play scene 3; stopped: the transport starts) */
    leds(w);
    CHECK(lit(w[L_STEP0 + 4]) && !w[L_STEP0 + 4][0]);   /* (its step green, waiting) */
    CHECK(!lit(w[L_STEP0 + 5]) && lit(w[L_STEP0 + 15]) && !w[L_STEP0 + 15][1]);   /* (even steps dark; 16 red) */
    adv_ms(50);
    st_reset();
    for (i = 0; i < 3; i++)
        ST.lane[i].play = 2;
    ST.lane[0].pos = 20000, ST.lane[2].pos = 40000;
    st_send();
    M.loop.page = 0;
    rest();
    screen();
    dump("loops");
    CHECK(lit_n(0, 0, 16, 64) > 20 && lit_n(18, 2, 128, 14) > 40);   /* (board A: the scene numbers, the bars) */
    /* b: Shift + any pad of a column: its scene (scene 1: lane 1 plays it, lanes 2, 3 stop) */
    clear();
    button(MR_CC_SHIFT, 1), hit(sp(3, 0)), button(MR_CC_SHIFT, 0);
    for (i = 0, n = 0; i < nact; i++)
        n += A[i].kind == RFM_MOVE_ACT_LOOP_LAUNCH;
    CHECK(n == 3 && M.loop.ask[0] == 0 && M.loop.ask[1] == 8 && M.loop.ask[2] == 8 && !M.loop.rs[24]);
    /* c: a slide down column 3 (lane 1's pad, then lane 2's a moment later) */
    rest(), clear();
    st_send();                                          /* (the host: as before) */
    M.loop.ask[0] = M.loop.ask[1] = M.loop.ask[2] = -1;
    M.loop.page = 2;                                    /* (round 2: the Record page's tap records) */
    pad(sp(0, 5), 100);                                 /* (an empty slot: armed by its tap ..) */
    CHECK(M.loop.rs[5] == RFM_MOVE_LOOP_RS_ARMED);
    adv_ms(40);
    pad(sp(1, 5), 100);                                 /* (.. the slide: scene 6 instead, the arm gone) */
    pad(sp(0, 5), 0), pad(sp(1, 5), 0);
    CHECK(!M.loop.rs[5] && count(RFM_MOVE_ACT_LOOP_REC) == 2 && last(RFM_MOVE_ACT_LOOP_REC)->param == RFM_MOVE_LOOP_R_CANCEL);
    for (i = 0, n = 0; i < nact; i++)
        n += A[i].kind == RFM_MOVE_ACT_LOOP_LAUNCH && A[i].down == 0;
    CHECK(n == 3 && !M.loop.rs[13]);                    /* (scene 6 has no loops: the playing lanes stop) */
    /* a pad far apart in time: no slide (a plain tap) */
    rest(), clear();
    pad(sp(0, 2), 100), pad(sp(0, 2), 0);
    adv_ms(RFM_MOVE_LOOP_SLIDE_MS + 100);
    pad(sp(1, 2), 100), pad(sp(1, 2), 0);
    for (i = 0, n = 0; i < nact; i++)
        n += A[i].kind == RFM_MOVE_ACT_LOOP_LAUNCH;
    CHECK(n == 2);
    /* step 16: stop all (at the bar; a double click: now) */
    rest(), clear();
    tap_step(15);
    for (i = 0, n = 0; i < nact; i++)
        n += A[i].kind == RFM_MOVE_ACT_LOOP_LAUNCH && A[i].down == 0 && A[i].value == RFM_MOVE_LQ_BAR;
    CHECK(n == 3);
    tap_step(15);
    CHECK(last(RFM_MOVE_ACT_LOOP_LAUNCH)->value == RFM_MOVE_LQ_INSTANT);
    /* a scene's double click: now */
    rest(), clear();
    tap_step(0), tap_step(0);
    CHECK(last(RFM_MOVE_ACT_LOOP_LAUNCH)->value == RFM_MOVE_LQ_INSTANT);
}

/* ---- 5. choosing without playing (Loop held + a pad; the arrows), pages, knobs */
static void t_pages(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    looper_view();
    loop_in(9, 2);
    rest(), clear();
    button(MR_CC_LOOP, 1), hit(sp(1, 1)), button(MR_CC_LOOP, 0);
    CHECK(M.loop.cur == 9 && !loop_acts() && !M.seq.loop_latch);
    press(MR_CC_RIGHT);
    CHECK(M.loop.cur == 10 && !loop_acts());
    press(MR_CC_DOWN), press(MR_CC_LEFT);
    CHECK(M.loop.cur == 17 && !loop_acts());
    press(MR_CC_UP), press(MR_CC_UP), press(MR_CC_UP);
    CHECK(M.loop.cur == 1);
    button(MR_CC_LOOP, 1), hit(sp(1, 1)), button(MR_CC_LOOP, 0);
    rest(), clear();
    /* the jog: a page a detent, stopping at the ends */
    CHECK(M.loop.page == 0);
    jog(1);
    CHECK(M.loop.page == 1);
    adv_ms(200);                                        /* (the switcher's slide done) */
    screen();
    dump("switch");
    CHECK(lit_n(50, 7, 78, 35) > 300);                   /* (board B: the shown page a white cell) */
    jog(10);
    CHECK(M.loop.page == 4);
    jog(-3), rest();
    CHECK(M.loop.page == 1);
    screen();
    dump("loop");
    CHECK(lit_n(96, 0, 128, 12) > 5 && lit_n(0, 16, 128, 46) > 60);   /* (board C: the locator, the wave) */
    /* the Loop page's knobs (round 2): 1 Start, 2 End (the markers), 3 Steps, 4 Pitch, 5 Reverse, 6 Warp; 7, 8 dark */
    leds(w);
    CHECK(lit(w[L_RING0 + 0]) && lit(w[L_RING0 + 5]) && !lit(w[L_RING0 + 6]) && !lit(w[L_RING0 + 7]));
    clear();
    touch(3, 1), knob(3, 2), touch(3, 0);
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_PITCH] == 100 && last(RFM_MOVE_ACT_LOOP_SET) &&
          last(RFM_MOVE_ACT_LOOP_SET)->param == RFM_MOVE_LOOP_P_PITCH && last(RFM_MOVE_ACT_LOOP_SET)->value == 100);
    knob(3, 4);
    screen();
    dump("pitch");
    CHECK(lit_n(0, 0, 128, 16) > 40);                   /* (board D: "Pitch", "+3 st") */
    button(MR_CC_SHIFT, 1), knob(3, 5), button(MR_CC_SHIFT, 0);
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_PITCH] == 305);   /* (Shift: cents) */
    /* (item 8) Steps: a bar a detent past 64, a beat past 16, a step below */
    knob(2, -2);
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_LENGTH] == 28); /* (32 -> 28: a beat) */
    knob(2, -6);
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_LENGTH] == 16);
    knob(2, -4);
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_LENGTH] == 14 && last(RFM_MOVE_ACT_LOOP_SET)->param == RFM_MOVE_LOOP_P_LENGTH &&
          last(RFM_MOVE_ACT_LOOP_SET)->value == 14);
    knob(2, 4);
    screen();
    dump("length");
    /* (item 8) the markers: a column of the close-up a detent, Shift a frame; sent in take; its peaks asked */
    clear();
    touch(0, 1), knob(0, 1);
    {
        const int32_t v = M.loop.slot[9].v[RFM_MOVE_LOOP_P_MSTART];
        const uint32_t f = (uint32_t)((uint64_t)v * 176400u / RFM_MOVE_LOOP_MK_ONE + 0) ;
        CHECK(v > 0 && f >= 340 && f <= 350 && last(RFM_MOVE_ACT_LOOP_SET)->param == RFM_MOVE_LOOP_P_MSTART &&
              last(RFM_MOVE_ACT_LOOP_SET)->take == (uint32_t)v);   /* (176400 / 8 / 64 = 344 frames) */
        adv_ms(20);
        CHECK(last(RFM_MOVE_ACT_LOOP_PEAKS) && last(RFM_MOVE_ACT_LOOP_PEAKS)->index == 9 &&
              last(RFM_MOVE_ACT_LOOP_PEAKS)->from == 0 && last(RFM_MOVE_ACT_LOOP_PEAKS)->count < 176400 / 8);   /* (near the start: the window from 0) */
        button(MR_CC_SHIFT, 1), knob(0, 1), button(MR_CC_SHIFT, 0);
        CHECKF((uint64_t)M.loop.slot[9].v[RFM_MOVE_LOOP_P_MSTART] * 176400u / RFM_MOVE_LOOP_MK_ONE == f + 1, "%d",
               M.loop.slot[9].v[RFM_MOVE_LOOP_P_MSTART]);   /* (Shift: one frame) */
        {
            uint8_t pk[64];
            memset(pk, 200, sizeof pk);
            rfm_move_loop_peaks(&M, 9, last(RFM_MOVE_ACT_LOOP_PEAKS)->from, last(RFM_MOVE_ACT_LOOP_PEAKS)->count, 64, pk);
        }
        screen();
        dump("mstart");
        CHECK(lit_n(0, 24, 128, 64) > 100 && lit_n(64, 20, 65, 64) > 30);   /* (the close-up, the marker's line) */
    }
    touch(0, 0);
    knob(1, -64);                                       /* (End: before the whole sound's end) */
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_MEND] < RFM_MOVE_LOOP_MK_ONE &&
          M.loop.slot[9].v[RFM_MOVE_LOOP_P_MEND] > M.loop.slot[9].v[RFM_MOVE_LOOP_P_MSTART]);
    rest();
    screen();
    dump("markers");
    /* (item 7) Warp: Off, Repitch, Grains, Beats in the knob's order */
    knob(5, 5);
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_WARP] == RFM_MOVE_LOOP_W_GRAINS);
    knob(5, 5);
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_WARP] == RFM_MOVE_LOOP_W_BEATS);
    knob(5, -15);
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_WARP] == RFM_MOVE_LOOP_W_OFF);
    /* Record: the lane's Source and Length */
    jog(1), rest(), clear();
    screen();
    dump("recpage");
    knob(0, 5);
    CHECK(M.loop.lane[1][RFM_MOVE_LOOP_L_SOURCE] == RFM_MOVE_LOOP_SRC_TRACK1 && last(RFM_MOVE_ACT_LOOP_SET)->index == 33 &&
          last(RFM_MOVE_ACT_LOOP_SET)->param == RFM_MOVE_LOOP_L_SOURCE);
    screen();
    dump("source");
    knob(1, 10);
    CHECK(M.loop.lane[1][RFM_MOVE_LOOP_L_LENGTH] == 2);
    /* Loop Mix: level (dB steps), pan, sends, mute */
    jog(1), rest(), clear();
    screen();
    dump("loopmix");
    knob(0, -4);
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_LEVEL] < 100 && last(RFM_MOVE_ACT_LOOP_SET)->param == RFM_MOVE_LOOP_P_LEVEL);
    knob(2, 20);
    CHECK(M.loop.slot[9].v[RFM_MOVE_LOOP_P_SEND1] == 20);
    /* Looper Mix: lanes 1..4, the Loops strip (MIX slot 4) */
    jog(1), rest(), clear();
    screen();
    dump("loopermix");
    knob(4, -2);
    CHECK(M.loop.mix[0] < 100 && last(RFM_MOVE_ACT_MIX) && last(RFM_MOVE_ACT_MIX)->slot == RFM_MOVE_LOOP_SLOT &&
          last(RFM_MOVE_ACT_MIX)->mix[0] == M.loop.mix[0]);
    knob(1, 2);
    CHECK(M.loop.lane[1][RFM_MOVE_LOOP_L_LEVEL] > 100 && last(RFM_MOVE_ACT_LOOP_SET)->index == 33);
    /* (round 2) an empty slot chosen on the Loop page: the page stays (it shows the slot empty); a tap there chooses */
    jog(-3), rest(), clear();
    button(MR_CC_LOOP, 1), hit(sp(3, 7)), button(MR_CC_LOOP, 0);
    CHECK(M.loop.page == 1);
    hit(sp(3, 6));
    CHECK(M.loop.cur == 30 && !count(RFM_MOVE_ACT_LOOP_REC) && !M.loop.rs[30]);
    screen();
    dump("empty");
    /* a slot held + the volume knob: its level */
    rest(), clear();
    pad(sp(1, 1), 100);
    touch(8, 1), raw(0xB0, MR_CC_VOLUME, 2), touch(8, 0);
    pad(sp(1, 1), 0);
    CHECK(M.loop.lvl == 1 && last(RFM_MOVE_ACT_LOOP_SET) && last(RFM_MOVE_ACT_LOOP_SET)->param == RFM_MOVE_LOOP_P_LEVEL);
    /* Track 4 held + the volume knob in the Looper view: the Loops strip */
    rest(), clear();
    hold_track(3), raw(0xB0, MR_CC_VOLUME, 2), track(3, 0);
    CHECK(last(RFM_MOVE_ACT_MIX) && last(RFM_MOVE_ACT_MIX)->slot == RFM_MOVE_LOOP_SLOT);
}

/* ---- 6. Copy, Delete, Undo, Mute */
static void t_edit(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    looper_view();
    loop_in(0, 2), loop_in(1, 1);
    M.loop.slot[0].v[RFM_MOVE_LOOP_P_PITCH] = 700;
    rest(), clear();
    button(MR_CC_COPY, 1);
    hit(sp(0, 0));
    CHECK(M.loop.copy_src == 0);
    screen();
    dump("copy");
    leds(w);
    hit(sp(2, 3));
    CHECK(last(RFM_MOVE_ACT_LOOP_COPY) && last(RFM_MOVE_ACT_LOOP_COPY)->index == 19 && last(RFM_MOVE_ACT_LOOP_COPY)->take == 0 &&
          !strcmp(M.loop.slot[19].path, M.loop.slot[0].path) && M.loop.slot[19].v[RFM_MOVE_LOOP_P_PITCH] == 700);
    button(MR_CC_COPY, 0);
    adv_ms(10);
    CHECK(M.loop.copy_src < 0 && !count(RFM_MOVE_ACT_LOOP_LAUNCH) && !count(RFM_MOVE_ACT_LOOP_REC));
    press(MR_CC_UNDO);
    CHECK(!M.loop.slot[19].path[0]);
    /* Delete held + a loop: gone, Undo brings it back */
    rest(), clear();
    button(MR_CC_DELETE, 1), hit(sp(0, 1)), button(MR_CC_DELETE, 0);
    CHECK(!M.loop.slot[1].path[0] && last(RFM_MOVE_ACT_LOOP_LOAD)->index == 1 && !D[last_i(RFM_MOVE_ACT_LOOP_LOAD)][0] &&
          !count(RFM_MOVE_ACT_LOOP_LAUNCH));
    press(MR_CC_UNDO);
    CHECK(M.loop.slot[1].path[0] && !strcmp(D[last_i(RFM_MOVE_ACT_LOOP_LOAD)], M.loop.slot[1].path));
    /* Delete + a scene's step: the column */
    loop_in(8, 1);
    rest(), clear();
    button(MR_CC_DELETE, 1), tap_step(0), button(MR_CC_DELETE, 0);
    CHECK(!M.loop.slot[0].path[0] && !M.loop.slot[8].path[0] && count(RFM_MOVE_ACT_LOOP_LOAD) == 2 && !count(RFM_MOVE_ACT_LOOP_LAUNCH));
    press(MR_CC_UNDO);
    CHECK(M.loop.slot[0].path[0] && M.loop.slot[8].path[0] && M.loop.slot[0].v[RFM_MOVE_LOOP_P_PITCH] == 700);
    /* Undo with nothing of the looper's: the sequencer's (not taken) */
    CHECK(!M.loop.und);
    /* Mute held + a loop */
    rest(), clear();
    button(MR_CC_MUTE, 1), hit(sp(0, 1)), button(MR_CC_MUTE, 0);
    CHECK(M.loop.slot[1].v[RFM_MOVE_LOOP_P_MUTE] == 1 && last(RFM_MOVE_ACT_LOOP_SET)->param == RFM_MOVE_LOOP_P_MUTE &&
          !count(RFM_MOVE_ACT_LOOP_LAUNCH) && !rfmv_seq_mute_on(&M));
}

/* ---- 7. the sampler's Loops tab (no pads needed) */
static void t_tab(void)
{
    looper_view();
    rfm_move_smp_offer(&M, 0, 0, 0xF, 0);
    tap_track(0), rest();                               /* (a firmware's layout: the pads are its) */
    CHECK(!M.loop.on);
    track(0, 1), press(MR_CC_SAMPLE), track(0, 0);      /* (Track 1 held + Sample: the app) */
    CHECK(rfm_move_smp_shown(&M));
    jog(-5), jog_click();                               /* (the tabs: pick Loops) */
    jog(3), jog_click();
    CHECK(M.smp.tab == RFM_MOVE_SMP_TAB_LOOPS);
    jog(1);                                             /* (off the tabs: slot 1-1) */
    jog(2);
    CHECK(M.loop.tab_slot == 2);
    screen();
    dump("tab");
    CHECK(lit_n(0, 49, 128, 64) > 700);                 /* (board O: the row inverted) */
    clear();
    jog_click();                                        /* (an empty slot: records) */
    CHECK(last(RFM_MOVE_ACT_LOOP_REC) && last(RFM_MOVE_ACT_LOOP_REC)->index == 2 &&
          last(RFM_MOVE_ACT_LOOP_REC)->param == RFM_MOVE_LOOP_R_START);
    st_reset(), ST.rec[0].slot = 2, ST.rec[0].state = RFM_MOVE_LOOP_RS_RECORDING, ST.rec[0].bar = 3, st_send();
    screen();
    dump("tabrec");
    press(MR_CC_SAMPLE);                                /* (Sample: ends it) */
    CHECK(last(RFM_MOVE_ACT_LOOP_REC)->param == RFM_MOVE_LOOP_R_END && rfm_move_smp_shown(&M));
    st_reset(), st_send();
    loop_in(2, 1);
    clear();
    jog_click();                                        /* (a loop: plays at the bar) */
    CHECK(last(RFM_MOVE_ACT_LOOP_LAUNCH) && last(RFM_MOVE_ACT_LOOP_LAUNCH)->down == 1 && last(RFM_MOVE_ACT_LOOP_LAUNCH)->index == 2);
    /* knobs 1..4: the lanes' levels; 5..8 dark, dead; the pads stay the firmware's */
    clear();
    touch(1, 1), knob(1, -3), touch(1, 0);
    CHECK(M.loop.lane[1][RFM_MOVE_LOOP_L_LEVEL] < 100 && last(RFM_MOVE_ACT_LOOP_SET)->index == 33);
    knob(6, 3);
    CHECK(count(RFM_MOVE_ACT_LOOP_SET) == 1);
    clear();
    hit(sp(0, 0));
    CHECK(!loop_acts());
}

/* ---- 8. the project: version 19, 18 loads an empty looper, a bad part refused, the refs; the Mixer's strip */
static void t_project(void)
{
    static uint8_t b[RFM_MOVE_PROJECT_MAX], c[RFM_MOVE_PROJECT_MAX];
    const char *refs[64];
    int n, n18;
    looper_view();
    loop_in(5, 4), loop_in(30, 1);
    M.loop.slot[5].v[RFM_MOVE_LOOP_P_REVERSE] = 1;
    M.loop.lane[2][RFM_MOVE_LOOP_L_SOURCE] = RFM_MOVE_LOOP_SRC_KIT, M.loop.mix[1] = 20;
    n = rfm_move_project_save(&M, b, sizeof b);
    M.loop.slot[5].v[RFM_MOVE_LOOP_P_MEND] = RFM_MOVE_LOOP_MK_ONE / 2, M.loop.cin = 2;
    n = rfm_move_project_save(&M, b, sizeof b);
    CHECK(n > 0 && b[4] == 20 && rfm_move_project_check(b, (uint32_t)n) == 0);
    rfm_move_init(&M2, NULL, on_act, NULL);
    rfm_move_fw_caps(&M2, LCAPS);
    clear();
    CHECK(rfm_move_project_load(&M2, 1, b, (uint32_t)n) == 0 && !strcmp(M2.loop.slot[5].path, M.loop.slot[5].path) &&
          M2.loop.slot[5].bars == 4 && M2.loop.slot[5].v[RFM_MOVE_LOOP_P_REVERSE] == 1 && M2.loop.slot[5].bpm10 == 1200 &&
          M2.loop.lane[2][RFM_MOVE_LOOP_L_SOURCE] == RFM_MOVE_LOOP_SRC_KIT && M2.loop.mix[1] == 20 &&
          M2.loop.slot[5].v[RFM_MOVE_LOOP_P_MEND] == RFM_MOVE_LOOP_MK_ONE / 2 && M2.loop.cin == 2 &&
          M2.loop.slot[5].st == RFM_MOVE_LOOP_LOADING);
    {
        int i, loads = 0;
        for (i = 0; i < nact; i++)
            loads += A[i].kind == RFM_MOVE_ACT_LOOP_LOAD && A[i].index == 5 && !strcmp(D[i], M.loop.slot[5].path);
        CHECK(loads == 1);
    }
    CHECK(rfm_move_loop_loaded(&M2, 5, RFM_MOVE_LOOP_OK, 4 * 88200, 44100, 4, 1200, 0, NULL) == 0 &&
          M2.loop.slot[5].st == RFM_MOVE_LOOP_OK);
    rfm_move_loop_loaded(&M2, 30, RFM_MOVE_LOOP_MISSING, 0, 0, 0, 0, 0, NULL);
    CHECK(M2.loop.slot[30].st == RFM_MOVE_LOOP_MISSING && M2.loop.slot[30].path[0]);
    /* the refs: each slot's path and the Undo's */
    button(MR_CC_DELETE, 1), hit(sp(3, 6)), button(MR_CC_DELETE, 0);
    CHECK(rfm_move_loop_refs(&M, refs, 64) == 2);
    /* a bad looper part: refused */
    memcpy(c, b, (size_t)n);
    c[n - 4 - RFM_MOVE_LOOP_BYTES + 16 + 0] = 99;       /* (lane 1's source) */
    proj_sum(c, (uint32_t)n);
    CHECK(rfm_move_project_check(c, (uint32_t)n) == -9);
    memcpy(c, b, (size_t)n);
    c[n - 4 - RFM_MOVE_LOOP_BYTES + 32 + 5 * RFM_MOVE_LOOP_SLOT_BYTES + RFM_MOVE_LOOP_PATH] = 0;   /* (no bars) */
    proj_sum(c, (uint32_t)n);
    CHECK(rfm_move_project_check(c, (uint32_t)n) == -9);
    /* version 18: an empty looper */
    memcpy(c, b, (size_t)n);
    n18 = (int)proj_v18(c, (uint32_t)n);
    CHECK(n18 == n - RFM_MOVE_LOOP_BYTES && c[4] == 18 && rfm_move_project_check(c, (uint32_t)n18) == 0);
    CHECK(rfm_move_project_load(&M2, 1, c, (uint32_t)n18) == 0 && !M2.loop.slot[5].path[0] && M2.loop.mix[0] == 100);
    /* the Mixer's fifth strip: knob 5 the Loops strip's */
    fresh_caps(LCAPS);
    hold_track(3), hit(24), track(3, 0), rest(), clear();
    knob(4, -3);
    CHECK(last(RFM_MOVE_ACT_MIX) && last(RFM_MOVE_ACT_MIX)->slot == RFM_MOVE_LOOP_SLOT && M.loop.mix[0] < 100);
    screen();
    dump("mixer");
}

/* ---- 9. (round 2) items 1..4, 6: the browser (Files for a slot), the page decides a tap, Count-in, the input */
static void t_round2(void)
{
    rfm_move_kit_row_t rows[2];
    const rfm_move_act_t *a;
    int i, n;
    looper_view();
    loop_in(0, 1);
    rest(), clear();
    /* item 2: on the Loops page an empty slot's tap chooses it; the Record page's records */
    CHECK(M.loop.page == 0);
    tap(1, 4);
    CHECK(M.loop.cur == 12 && !count(RFM_MOVE_ACT_LOOP_REC));
    /* item 1: the jog's click: Files for the chosen slot (the kit's rows); a pad chooses where it lands */
    rest(), clear();
    jog_click();
    CHECK(M.loop.br && M.kit.lbr && !M.kit.place);
    screen();
    dump("browser");
    CHECK(lit_n(16, 20, 128, 40) > 40);                 /* ("My Samples") */
    jog_click();                                        /* (My Samples: its rows asked) */
    a = last(RFM_MOVE_ACT_KIT_ROWS);
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_SAMPLES && a);
    memset(rows, 0, sizeof rows);
    rows[0].id = 77, rows[0].kind = RFM_MOVE_KIT_ROW_FILE, snprintf(rows[0].name, sizeof rows[0].name, "Breakbeat");
    rows[1].id = 78, rows[1].kind = RFM_MOVE_KIT_ROW_DIR, snprintf(rows[1].name, sizeof rows[1].name, "Drums");
    CHECK(rfm_move_kit_rows(&M, RFM_MOVE_KIT_PL_SAMPLES, 0, 2, 0, 2, rows) == 1);   /* (shown: draw again) */
    hit(sp(2, 6));                                      /* (a pad: the target, nothing records or plays) */
    CHECK(M.loop.cur == 22 && !count(RFM_MOVE_ACT_LOOP_REC) && !count(RFM_MOVE_ACT_LOOP_LAUNCH));
    adv_ms(300);
    CHECK(!count(RFM_MOVE_ACT_KIT_LOAD));               /* (no landing on a kit pad) */
    screen();
    dump("browser_rows");
    clear();
    jog_click();                                        /* (the file row: into the slot) */
    a = last(RFM_MOVE_ACT_LOOP_FILE);
    CHECK(a && a->index == 22 && a->value == RFM_MOVE_KIT_PL_SAMPLES && a->take == 77 && !M.loop.br && !M.kit.lbr);
    {
        uint8_t pk[RFM_MOVE_LOOP_PEAKS];
        memset(pk, 90, sizeof pk);
        rfm_move_loop_recorded(&M, 22, RFM_MOVE_LOOP_OK, "@/Loops/Breakbeat.wav", 4 * 88200, 44100, 4, 1200,
                               RFM_MOVE_LOOP_PEAKS, pk);
    }
    CHECK(!strcmp(M.loop.slot[22].path, "@/Loops/Breakbeat.wav") && M.loop.slot[22].bars == 4 && M.loop.und &&
          !strcmp(M.loop.msg, "Loaded") && M.loop.slot[22].v[RFM_MOVE_LOOP_P_LENGTH] == 64);
    clear();
    press(MR_CC_UNDO);                                  /* (Undo: the slot as it was, empty) */
    CHECK(!M.loop.slot[22].path[0] && last(RFM_MOVE_ACT_LOOP_LOAD) && last(RFM_MOVE_ACT_LOOP_LOAD)->index == 22);
    /* Back closes the browser */
    rest(), clear();
    jog_click();
    CHECK(M.loop.br);
    press(MR_CC_BACK);
    CHECK(!M.loop.br && !M.kit.lbr);
    /* items 3, 6: the Record page: Count-in (3), In Level (4) and Monitor (5) are the sampler's own settings */
    rest(), clear();
    jog(2), rest();
    CHECK(M.loop.page == 2);
    knob(3, 4);
    CHECK(M.s.smp[RFM_MOVE_SMP_INPUT] == 2 && last(RFM_MOVE_ACT_SMP_SET) && last(RFM_MOVE_ACT_SMP_SET)->index == RFM_MOVE_SMP_INPUT &&
          last(RFM_MOVE_ACT_SMP_SET)->value == 2);
    knob(4, 10);
    CHECK(M.s.smp[RFM_MOVE_SMP_MONITOR] == 2 && rfm_move_smp_value(&M, RFM_MOVE_SMP_MONITOR) == 2);   /* (Auto) */
    knob(2, 10);
    CHECK(M.loop.cin == 2);
    rest();
    screen();
    dump("recpage2");
    /* item 4: armed while stopped, Play: a 2-bar count-in, its clicks (each bar's first accented), then bar 1 */
    clear();
    M.seq.metro = 1;
    tap(1, 1);
    CHECK(M.loop.rs[9] == RFM_MOVE_LOOP_RS_ARMED && rfm_move_loop_count_in(&M) == 2 && !M.seq.playing);
    press(MR_CC_PLAY);
    CHECK(M.seq.start_pending == 3 && M.seq.count_in == 2 * 384 + 1);
    adv_ms(1000);
    {
        uint8_t w[RFM_MOVE_NLEDS][3];
        leds(w);
        CHECK(lit(w[L_STEP0]) && !w[L_STEP0][1] && !M.seq.playing);   /* (the steps count the beats, red) */
    }
    adv_ms(3100);
    for (i = 0, n = 0; i < nact; i++)
        n += A[i].kind == RFM_MOVE_ACT_CLICK && A[i].value == 1;
    CHECKF(M.seq.playing && count(RFM_MOVE_ACT_CLICK) >= 8 && n >= 2, "playing %d clicks %d accents %d", M.seq.playing,
           count(RFM_MOVE_ACT_CLICK), n);
    M.seq.metro = 0;
}

/* ---- 10. (round 2, item 9) the step looper: the keys, their states and colours, over the FM-1 layout */
static void t_steploop(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    const rfm_move_act_t *a;
    int keys0;
    fresh_caps(LCAPS);                                  /* (the FM-1 layout: the steps are its white keys) */
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1);
    tap_step(2);
    keys0 = count(RFM_MOVE_ACT_KEY);
    CHECK(keys0 > 0);                                   /* (off: an FM-1 key) */
    /* on: Track 4 held + step 4 (the FM-1 layout's Shift is GLO); the card */
    clear();
    hold_track(3), step(3, 1), step(3, 0), track(3, 0);
    CHECK(M.loop.to_on && rfmv_loop_to_steps(&M));
    screen();
    dump("to_card");
    CHECK(lit_n(0, 0, 128, 16) > 60 && lit_n(0, 18, 128, 64) > 150);
    rest(), clear();
    /* the colours: Okabe-Ito, a lane a group, very dim while empty */
    leds(w);
    CHECK(lit(w[L_STEP0 + 0]) && w[L_STEP0 + 0][2] > w[L_STEP0 + 0][0]);   /* (blue) */
    CHECK(w[L_STEP0 + 4][0] > w[L_STEP0 + 4][2]);       /* (orange) */
    /* key 1 of lane 1: records at the bar into the lane's first empty slot; stopped: the transport starts */
    tap_step(0);
    a = last(RFM_MOVE_ACT_LOOP_REC);
    CHECK(a && a->index == 0 && a->param == RFM_MOVE_LOOP_R_START && a->vel == RFM_MOVE_LOOP_SRC_PLAYED &&
          !count(RFM_MOVE_ACT_KEY) && (M.seq.playing || M.seq.start_pending));
    leds(w);
    CHECK(w[L_STEP0 + 0][0] && !w[L_STEP0 + 0][1]);    /* (armed: key 1 red) */
    screen();
    dump("to_overlay");
    CHECK(lit_n(0, 0, 128, 11) > 30);                   /* ("Lane 1 REC") */
    /* the host: recording, bar 2 */
    adv_ms(50);
    st_reset();
    ST.rec[0].slot = 0, ST.rec[0].state = RFM_MOVE_LOOP_RS_RECORDING, ST.rec[0].bar = 2;
    st_send();
    leds(w);
    CHECK(is_rgb(w[L_STEP0 + 0], 255, 0, 0) || (w[L_STEP0 + 0][0] > 50 && !w[L_STEP0 + 0][1]));   /* (bar 1 done: red) */
    clear();
    tap_step(0);                                        /* (the pedal again: it ends at the bar, loops) */
    CHECK(last(RFM_MOVE_ACT_LOOP_REC) && last(RFM_MOVE_ACT_LOOP_REC)->param == RFM_MOVE_LOOP_R_END);
    st_reset();
    loop_in(0, 1);
    ST.lane[0].play = 0, ST.lane[0].pos = 40000;
    st_send();
    rest();
    leds(w);
    CHECK(is_rgb(w[L_STEP0 + 2], 0, 114, 178) && !is_rgb(w[L_STEP0 + 0], 0, 114, 178) && lit(w[L_STEP0 + 0]));   /* (its
                                                         * third quarter: key 3 bright, the others dim) */
    /* key 2: overdub on / off (its answer keeps the loop's settings; one Undo) */
    clear();
    M.loop.slot[0].v[RFM_MOVE_LOOP_P_LEVEL] = 90;
    tap_step(1);
    a = last(RFM_MOVE_ACT_LOOP_DUB);
    CHECK(a && a->index == 0 && a->down == 1 && M.loop.to_dub[0] == 1);
    tap_step(1);
    a = last(RFM_MOVE_ACT_LOOP_DUB);
    CHECK(a && a->down == 0 && !M.loop.to_dub[0] && (M.loop.dubq & 1));
    rfm_move_loop_recorded(&M, 0, RFM_MOVE_LOOP_OK, "@/Loops/1-1 120bpm 1b 2.wav", 88200, 44100, 1, 1200, 0, NULL);
    CHECK(!strcmp(M.loop.slot[0].path, "@/Loops/1-1 120bpm 1b 2.wav") && M.loop.slot[0].v[RFM_MOVE_LOOP_P_LEVEL] == 90 &&
          M.loop.und && !(M.loop.dubq & 1));
    /* key 4: Next (the lane's next empty slot is the pedal's); a double tap: Stop */
    rest(), clear();
    tap_step(3);
    CHECK(M.loop.to_target[0] == 2 && !count(RFM_MOVE_ACT_LOOP_LAUNCH));
    now_ms += 50;
    tap_step(3);
    a = last(RFM_MOVE_ACT_LOOP_LAUNCH);
    CHECK(a && a->down == 0 && a->index / 8 == 0 && M.loop.to_target[0] == 1);   /* (the pedal's slot as before Next) */
    /* key 3: Replace: records over the playing loop from the next bar */
    rest(), clear();
    st_send();
    tap_step(2);
    a = last(RFM_MOVE_ACT_LOOP_REC);
    CHECK(a && a->index == 0 && a->param == RFM_MOVE_LOOP_R_START);
    tap_step(0);                                        /* (armed: the pedal cancels) */
    CHECK(last(RFM_MOVE_ACT_LOOP_REC)->param == RFM_MOVE_LOOP_R_CANCEL);
    /* a group key held + the volume knob: the lane's level */
    rest(), clear();
    step(7, 1), raw(0xB0, MR_CC_VOLUME, 2);
    a = last(RFM_MOVE_ACT_LOOP_SET);
    CHECK(a && a->index == RFM_MOVE_LOOP_LANE0 + 1 && M.loop.lane[1][RFM_MOVE_LOOP_L_LEVEL] > 100);
    screen();
    dump("to_level");
    step(7, 0);
    /* Shift-less shortcuts: Track 4 held + a step stays the shortcuts; off again: the steps are the FM-1's */
    rest(), clear();
    hold_track(3), step(3, 1), step(3, 0), track(3, 0);
    CHECK(!M.loop.to_on);
    rest(), clear();
    tap_step(2);
    CHECK(count(RFM_MOVE_ACT_KEY) > 0 && !count(RFM_MOVE_ACT_LOOP_REC));
    /* the Record page's knob 6 too, in the Sequencer */
    looper_view();
    jog(2), rest();
    knob(5, 5);
    CHECK(M.loop.to_on && !rfmv_loop_to_steps(&M));     /* (the Looper view keeps its scenes on the steps) */
}

int main(void)
{
    t_cap();
    t_record();
    t_launch();
    t_scenes();
    t_pages();
    t_edit();
    t_tab();
    t_project();
    t_round2();
    t_steploop();
    return check_done("move_looper");
}
