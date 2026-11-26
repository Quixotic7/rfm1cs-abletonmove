/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (stage 3a, the owner's 2026-11-25 kit round) the kit (RFM_MOVE_CAP_KIT; README "The kit (stage 3a)"): the capability
 * off (no Kit view, no KIT_* action), Track 4's round (Mixer, Routing, Kit; held: pads 3, 4, 5), the note encoding of
 * the right half's pitches, the pads (play, choose, Shift, the right half Pitches / In Key / Off), the steps (the chosen
 * pad's only; a held step + a pad; Delete + a step), the knobs (lit, dead, popups, fine), the jog's rows, popups and
 * tabs, Back, Files (the places, a folder, the preview, a load, the answers), Delete / Mute + a pad and Undo, a pad held
 * + Sample (the take into the pad, its screens), the sampler's To Kit, the sequencer's tracks routed to the kit (and to
 * a firmware and back), the project (version 11: saved, loaded, refused when bad; a version 10 one keeps its routing;
 * a new one routes 13..16 to the kit), the settings (version 9), the layers over it (the sampler, a Shift menu, Loop,
 * a held Track), and the screens against the mock-up's boards (move_kit_ref.h); (stage 3b) 18..24; (stage 3c) 25..29: a
 * pad's own edits, mix and join, a take sliced, the page's calls, boards N, O, P and V whole. Writes kit_*.pbm / .leds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "padlock.h"
#include "move_int.h"
#include "v2.h"
#include "moveremote/client.h"
#include "rfm1cs/move.h"
#include "move_fwb_data.h"
#include "move_kit_ref.h"
#include "proj15.h"

#define NFW ((int)(sizeof FWB / sizeof FWB[0]))
#define KCAPS (RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_SAMPLER | RFM_MOVE_CAP_KIT)

/* ---- the actions, logged */
static rfm_move_act_t A[4096];
static char D[4096][RFM_MOVE_KIT_PATH];                 /* (a KIT_LOAD's path) */
static int nact;
static char said[8192];
static void on_act(void *ctx, const rfm_move_act_t *a)
{
    (void)ctx;
    if (a->kind == RFM_MOVE_ACT_SAY) {
        snprintf(said + strlen(said), sizeof said - strlen(said), "%s|", (const char *)a->data);
        return;
    }
    if (nact < (int)(sizeof A / sizeof A[0])) {
        A[nact] = *a, A[nact].data = NULL;
        D[nact][0] = 0;
        if ((a->kind == RFM_MOVE_ACT_KIT_LOAD || a->kind == RFM_MOVE_ACT_KIT_FIND) && a->data)
            snprintf(D[nact], sizeof D[nact], "%.*s", (int)a->len, (const char *)a->data);
        nact++;
    }
}
static void clear(void) { nact = 0, said[0] = 0; }
static int count(int kind)
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += A[i].kind == kind;
    return n;
}
static int last_i(int kind)
{
    int i;
    for (i = nact - 1; i >= 0; i--)
        if (A[i].kind == kind)
            return i;
    return -1;
}
static const rfm_move_act_t *last(int kind) { const int i = last_i(kind); return i >= 0 ? &A[i] : NULL; }
#define TL_PLAY_T 0                                     /* (the Sound row's tools: Play first) */
static int kit_acts(void)
{
    int k, n = 0;
    for (k = RFM_MOVE_ACT_KIT_NOTE; k <= RFM_MOVE_ACT_KIT_SLICE; k++)
        n += count(k);
    for (k = RFM_MOVE_ACT_KIT_FIND; k <= RFM_MOVE_ACT_KIT_AUDITION; k++)   /* (round A) */
        n += count(k);
    return n;
}

/* ---- the fake Move */
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
static int kp(int k) { return (k / 4) * 8 + k % 4; }     /* kit pad k (0..15: pad 1 bottom left) -> the Move's pad */
static int rp(int n) { return (n / 4) * 8 + 4 + n % 4; } /* the right half's pad n (0..15) */
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
static void back(void) { press(MR_CC_BACK); }
static void tools(void) { button(MR_CC_SHIFT, 1), jog_click(), button(MR_CC_SHIFT, 0); }   /* (pages) */

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
static void fresh(void) { fresh_caps(KCAPS); }
/* the Kit view (Track 4 held + its pad 3) */
static void kit_view(void)
{
    fresh();
    hold_track(3), hit(26), track(3, 0);
    jog(1), rest(), clear();                            /* (pages: Pad Sample, after Sample Kit) */
}
/* a sound on pad k, as the host answers a load */
static void sound(int k, const char *name, uint32_t frames, const uint8_t *pk, int n)
{
    char path[64];
    snprintf(path, sizeof path, "/data/UserData/UserLibrary/Samples/%s.wav", name);
    rfm_move_kit_loaded(&M, k, RFM_MOVE_KIT_OK, name, path, frames, 48000, n, pk);
}

/* ---- the screen and the LEDs */
static uint8_t SCR[MR_SCREEN_BYTES];
static void screen(void) { rfm_move_screen(&M, NULL, SCR); }
static int px(int x, int y) { return SCR[(y >> 3) * 128 + x] >> (y & 7) & 1; }
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
/* as the mock writes its boards: kit_<name>.pbm and .leds (4 pad rows from the top, the steps, the knobs, the icons,
 * the Track buttons, then Sample Record Play Loop Mute Delete Copy Undo) */
static void dump(const char *name)
{
    char path[96];
    FILE *f;
    int x, y, c, r;
    uint8_t w[RFM_MOVE_NLEDS][3];
    static const int WB[5] = { W_LOOP, W_MUTE, W_DELETE, W_COPY, W_UNDO };
    screen();
    snprintf(path, sizeof path, "kit_%s.pbm", name);
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
    snprintf(path, sizeof path, "kit_%s.leds", name);
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
typedef struct { int x, y, w, h; } box_t;
static int vs_ref(const char *name, const box_t *ex, int nex)
{
    const kit_ref_t *r = NULL;
    int x, y, out = 0, i;
    for (i = 0; i < (int)(sizeof KIT_REF / sizeof KIT_REF[0]); i++)
        if (!strcmp(KIT_REF[i].name, name))
            r = &KIT_REF[i];
    if (!r)
        return -1;
    screen();
    for (y = 0; y < 64; y++)
        for (x = 0; x < 128; x++) {
            const int want = r->px[y][x >> 3] >> (7 - (x & 7)) & 1;
            int masked = 0;
            for (i = 0; i < nex; i++)
                masked |= x >= ex[i].x && x < ex[i].x + ex[i].w && y >= ex[i].y && y < ex[i].y + ex[i].h;
            if (want != px(x, y) && !masked) {
                if (out < 6)
                    fprintf(stderr, "  %s: (%d, %d) mock %d, core %d\n", name, x, y, want, px(x, y));
                out++;
            }
        }
    return out;
}
static uint8_t peak_for(int a, int h)
{
    int p;
    for (p = 0; p < 256; p++)
        if ((p * h + 127) / 255 == a)
            return (uint8_t)p;
    return 255;
}
/* the mock's wavec heights (decay: a drum hit) as peaks: w px wide (a column every 2), h each way */
static int mock_peaks(int w, int h, unsigned seed, int decay, uint8_t *pk)
{
    int x, n = 0;
    for (x = 0; x < w; x += 2) {
        const int beat = (x * 16 / w) % 4;
        int a;
        seed = seed * 1103515245u + 12345u;
        a = 1 + (int)((seed >> 16) % (unsigned)h) * (beat == 0 ? 4 : beat == 2 ? 3 : 2) / 4;
        if (decay)
            a = 1 + (h - 1) * (w - x) * (w - x) / (w * w) + (int)((seed >> 20) % 2u);
        if (a > h)
            a = h;
        pk[n++] = peak_for(a, h);
    }
    return n;
}
static int level_for(int px_, int w)
{
    int l;
    for (l = 1; l <= 32767; l++)
        if (rfmv_smp_lvpx(l, w) == px_)
            return l;
    return -1;
}

/* ================================================================ the tests === */
/* 1. the note encoding: every pad and pitch a note of its own on its track, back again */
static void t_encoding(void)
{
    int k, s, ok = 1, uniq = 1;
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++) {
        CHECK(rfm_move_kit_note(k, 0) == 36 + k);
        for (s = 0; s <= RFM_MOVE_KIT_SEMIS; s++) {
            const int n = rfm_move_kit_note(k, s);
            int s2 = -1, k2, j;
            k2 = rfm_move_kit_pad_of(RFM_MOVE_KIT_TRACK0 + k / 4, n, &s2);
            ok &= n >= 0 && n <= 127 && k2 == k && s2 == s;
            for (j = 0; j < 4; j++)                     /* (no other pad of the track has that note) */
                if (j != k % 4) {
                    int t;
                    for (t = 0; t <= RFM_MOVE_KIT_SEMIS; t++)
                        uniq &= rfm_move_kit_note((k / 4) * 4 + j, t) != n;
                }
        }
    }
    CHECK(ok && uniq);
    CHECK(rfm_move_kit_note(0, RFM_MOVE_KIT_SEMIS + 1) == -1 && rfm_move_kit_note(16, 0) == -1);
    CHECK(rfm_move_kit_pad_of(3, 40, NULL) == -1 && rfm_move_kit_pad_of(15, 51, NULL) == 15);
    CHECK(rfm_move_kit_note(15, RFM_MOVE_KIT_SEMIS) == 127 && rfm_move_kit_note(0, 1) == 0);
}

/* 2. the capability off: Track 4 goes round the Mixer and Routing; no Kit view, no KIT_* action */
static void t_cap_off(void)
{
    fresh_caps(RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_SAMPLER);
    CHECK(!kit_acts());
    tap_track(3), rest();
    CHECK(M.seq.projects && M.fw.fx && rfmv_t4_page(&M) == 0);
    tap_track(3), rest();
    CHECK(rfmv_t4_page(&M) == 1);
    tap_track(3), rest();
    CHECK(rfmv_t4_page(&M) == 0 && !M.kit.on);
    hold_track(3), hit(26), hit(28), track(3, 0), rest();
    CHECK(!M.kit.on && !kit_acts() && !rfm_move_kit_shown(&M));
    CHECK(rfm_move_kit_loaded(&M, 0, RFM_MOVE_KIT_OK, "a", "/a.wav", 10, 48000, 0, NULL) == 0 && !M.kit.pad[0].path[0]);
}

/* 3. Track 4's round: Mixer, Routing, Kit; held: pad 3 the Kit, pad 4 dead, pad 5 the projects; the view it was left on */
static void t_round(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    fresh();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1);
    CHECK(count(RFM_MOVE_ACT_KIT_LOAD) == 16 && count(RFM_MOVE_ACT_KIT_SET) == 16 * RFM_MOVE_KIT_P_N + RFM_MOVE_KIT_BUS_N);   /* (the
                                                         * capability: every pad told) */
    CHECK(M.kit.route == 0xF000);                       /* (nothing stored: a new project's kit) */
    tap_track(3), rest();
    CHECK(M.seq.projects && rfmv_t4_page(&M) == 0);
    tap_track(3), rest();
    CHECK(M.seq.projects && rfmv_t4_page(&M) == 1);
    tap_track(3);
    CHECK(!M.seq.projects && rfmv_kit_active(&M) && M.s.mode == RFM_MOVE_MODE_SEQ && M.seq.sel_track == 12);
    CHECK(M.pop == POP_T4 && M.pop_i == 2 && rfmv_t4_page(&M) == 2 && M.s.t4_kit == 1);
    rest();
    CHECK(rfm_move_kit_shown(&M));
    leds(w);
    CHECK(is_rgb(w[L_TRACK0 + 3], 255, 60, 160));      /* (Track 4: the kit's) */
    tap_track(3), rest();                               /* Kit -> the Mixer; the layout under it as it was */
    CHECK(M.seq.projects && rfmv_t4_page(&M) == 0 && !M.kit.on && M.s.t4_kit == 0);
    back(), rest();
    CHECK(!M.seq.projects && M.s.mode == RFM_MOVE_MODE_FM1);
    /* held: pad 3 the Kit; Track 1 tapped: its layout, the kit left; Track 4 again: the Kit, as it was left */
    hold_track(3);
    leds(w);
    CHECK(lit(w[L_PAD0 + 26]) && !lit(w[L_PAD0 + 27]) && lit(w[L_PAD0 + 28]));   /* (pad 4 the Looper: dark) */
    hit(26), track(3, 0), rest();
    CHECK(rfmv_kit_active(&M));
    tap_track(0), rest();
    CHECK(!M.kit.on && M.s.mode == RFM_MOVE_MODE_FM1 && M.fw.active == 0);
    tap_track(3), rest();
    CHECK(rfmv_kit_active(&M));
    hold_track(3), hit(27), track(3, 0), rest();        /* (pad 4: nothing) */
    CHECK(rfmv_kit_active(&M));
    hold_track(3), hit(28), track(3, 0), rest();        /* (pad 5: the projects, over the kit) */
    CHECK(M.seq.projects && M.kit.on && !rfmv_kit_active(&M));
    back(), rest();
    CHECKF(!M.seq.projects && rfmv_kit_active(&M), "projects %d on %d mode %d view %d", M.seq.projects, M.kit.on, M.s.mode,
           M.seq.view);
    /* Menu: session view under the same Track; again: the kit */
    press(MR_CC_MENU), rest();
    CHECK(M.seq.view && !rfmv_kit_active(&M) && M.kit.on);
    press(MR_CC_MENU), rest();
    CHECK(rfmv_kit_active(&M));
    /* the held Track 4's screen names it */
    hold_track(3);
    screen();
    track(3, 0), rest();
}

/* 4. the pads: play (KIT_NOTE, the chosen pad and its track), Shift (chosen, silent), the right half */
static void t_pads(void)
{
    const rfm_move_act_t *a;
    uint8_t w[RFM_MOVE_NLEDS][3];
    kit_view();
    pad(kp(5), 90);
    CHECK((a = last(RFM_MOVE_ACT_KIT_NOTE)) && a->index == 5 && a->down == 1 && a->vel > 0 && a->value == 0 && a->track == 14);
    CHECK(M.kit.cur == 5 && M.seq.sel_track == 13);
    leds(w);
    CHECK(is_rgb(w[L_PAD0 + kp(5)], 255, 255, 255));   /* (pressed: white) */
    clear(), pad(kp(5), 0);
    CHECK((a = last(RFM_MOVE_ACT_KIT_NOTE)) && a->index == 5 && a->down == 0 && a->vel == 0);
    CHECK(count(RFM_MOVE_ACT_MIDI) == 0);               /* (the kit's: no firmware hears it) */
    clear(), button(MR_CC_SHIFT, 1), hit(kp(12)), button(MR_CC_SHIFT, 0);
    CHECK(M.kit.cur == 12 && M.seq.sel_track == 15 && !count(RFM_MOVE_ACT_KIT_NOTE));
    /* the right half: Pitches (16 semitones up), In Key (C minor: rows a fourth apart), Off (dead) */
    clear(), hit(rp(7));
    CHECK((a = last(RFM_MOVE_ACT_KIT_NOTE)) && a->index == 12 && a->value == 7 && a->down == 0);
    CHECK(A[last_i(RFM_MOVE_ACT_KIT_NOTE) - 1].value == 7 && A[last_i(RFM_MOVE_ACT_KIT_NOTE) - 1].down == 1);
    leds(w);
    CHECK(is_rgb(w[L_PAD0 + rp(0)], 152, 107, 229) && is_rgb(w[L_PAD0 + rp(12)], 152, 107, 229) &&
          is_rgb(w[L_PAD0 + rp(1)], 39, 39, 39));       /* (its own pitch and its octave bright, in its row's colour) */
    M.kit.rh = RFM_MOVE_KIT_RH_INKEY4, M.s.scale = RFM_MOVE_SCALE_MINOR;   /* (pages v2: round A's In Key) */
    CHECK(rfmv_kit_right_semis(&M, 0) == 0 && rfmv_kit_right_semis(&M, 1) == 2 && rfmv_kit_right_semis(&M, 3) == 5 &&
          rfmv_kit_right_semis(&M, 4) == 5 && rfmv_kit_right_semis(&M, 15) == 12 + 8);
    clear(), hit(rp(4));
    CHECK(count(RFM_MOVE_ACT_KIT_NOTE) == 2 && A[0].value == 5);
    /* (pages v2) In Key: 16 scale notes in a row; Chromatic: rows a fourth apart; Chop: the top left the first chop */
    M.kit.rh = RFM_MOVE_KIT_RH_INKEY;
    CHECK(rfmv_kit_right_semis(&M, 4) == 7 && rfmv_kit_right_semis(&M, 7) == 12);
    M.kit.rh = RFM_MOVE_KIT_RH_CHROMA;
    CHECK(rfmv_kit_right_semis(&M, 4) == 5 && rfmv_kit_right_semis(&M, 15) == 18);
    M.kit.rh = RFM_MOVE_KIT_RH_CHOP;
    sound(12, "Loop", 4800, NULL, 0);
    clear(), hit(rp(12));
    CHECK(count(RFM_MOVE_ACT_KIT_NOTE) == 2 && A[0].index == 12 && A[0].value == 0 &&
          (A[0].klk_mask >> RFM_MOVE_KIT_LK_CHOP & 1) && A[0].klk[RFM_MOVE_KIT_LK_CHOP] == 0 &&
          A[0].klk[RFM_MOVE_KIT_LK_CHOPE] == 0);
    clear(), pad(rp(1), 100), pad(rp(14), 100);         /* (two slots: chops 13 and 2, the range 2..13 looped) */
    CHECK((a = last(RFM_MOVE_ACT_KIT_NOTE)) && a->down && a->klk[RFM_MOVE_KIT_LK_CHOP] == 2 && a->klk[RFM_MOVE_KIT_LK_CHOPE] == 13);
    pad(rp(1), 0), pad(rp(14), 0);
    M.kit.rh = RFM_MOVE_KIT_RH_PITCHES;
    /* a track routed to a firmware instead: its pads are MIDI on the track's channel, tagged with it */
    M.kit.route = (uint16_t)(M.kit.route & ~(1u << 12));
    clear(), hit(kp(2));
    CHECK(!count(RFM_MOVE_ACT_KIT_NOTE) && (a = last(RFM_MOVE_ACT_MIDI)) && a->track == 13 && a->bytes[1] == 38);
    M.kit.route |= 1u << 12;
}

/* 5. the steps: the chosen pad's only; a click toggles its own note at the release; a held step + a pad; Delete */
static void t_steps(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int lo;
    const int clip = SEQ_CLIP(12, 0);
    kit_view();
    hit(kp(1));                                         /* (pad 2, track 13) */
    step(0, 1);
    CHECK(rfmv_seq_find(&M, clip, 0, 37) < 0);          /* (nothing at the press) */
    step(0, 0);
    CHECK(rfmv_seq_find(&M, clip, 0, 37) >= 0);
    tap_step(4);
    hit(kp(0));                                         /* (pad 1: another pad of the same track) */
    tap_step(4);
    CHECK(rfmv_seq_find(&M, clip, 4, 37) >= 0 && rfmv_seq_find(&M, clip, 4, 36) >= 0);
    leds(w);
    CHECK(is_rgb(w[L_STEP0 + 4], 219, 51, 137) && !is_rgb(w[L_STEP0 + 0], 219, 51, 137));   /* (pad 1's: step 5 only) */
    tap_step(4);                                        /* (pad 1's off; pad 2's stays) */
    CHECK(rfmv_seq_find(&M, clip, 4, 36) < 0 && rfmv_seq_find(&M, clip, 4, 37) >= 0);
    rfmv_seq_undo(&M);
    CHECK(rfmv_seq_find(&M, clip, 4, 36) >= 0);
    /* a held step + pads: their notes in (or out); the step's own click does nothing then */
    step(8, 1), hit(kp(2)), hit(rp(3)), step(8, 0);    /* (pad 3, then pad 3 three semitones up) */
    CHECK(rfmv_seq_find(&M, clip, 8, 38) >= 0 && rfmv_seq_find(&M, clip, 8, rfm_move_kit_note(2, 3)) >= 0 &&
          rfmv_seq_range(&M, clip, 8, &lo) - lo == 2);
    button(MR_CC_SHIFT, 1), hit(kp(0)), button(MR_CC_SHIFT, 0), tap_step(8);   /* (pad 1's there too) */
    CHECK(rfmv_seq_find(&M, clip, 8, 36) >= 0);
    /* Delete + a step: the chosen pad's notes there only (pad 3: its own and its pitched one) */
    button(MR_CC_SHIFT, 1), hit(kp(2)), button(MR_CC_SHIFT, 0);
    button(MR_CC_DELETE, 1), tap_step(8), button(MR_CC_DELETE, 0);
    CHECK(rfmv_seq_find(&M, clip, 8, 38) < 0 && rfmv_seq_find(&M, clip, 8, rfm_move_kit_note(2, 3)) < 0 &&
          rfmv_seq_find(&M, clip, 8, 36) >= 0);
    /* another row's pad: its track */
    hit(kp(9)), tap_step(2);
    CHECK(M.seq.sel_track == 14 && rfmv_seq_find(&M, SEQ_CLIP(14, 0), 2, 45) >= 0);
    dump("steps");
}

/* 6. the sequencer: a kit track's notes play its pads (the right half's pitch kept), a firmware's track not */
static void t_seq(void)
{
    int i, ok = 0;
    kit_view();
    rfmv_seq_note_put(&M, SEQ_CLIP(12, 0), 0, 37, 100, 24, 0);
    rfmv_seq_note_put(&M, SEQ_CLIP(12, 0), 4, rfm_move_kit_note(1, 7), 100, 24, 0);
    rfmv_seq_note_put(&M, SEQ_CLIP(13, 0), 0, 40, 100, 24, 0);
    rfmv_seq_note_put(&M, SEQ_CLIP(0, 0), 0, 60, 100, 24, 0);
    M.seq.tr[12].active = M.seq.tr[13].active = M.seq.tr[0].active = 0;
    M.kit.route = (uint16_t)(M.kit.route & ~(1u << 13));   /* (track 14 to no kit) */
    clear();
    rfm_move_seq_play(&M, 1);
    adv_ms(800);
    rfm_move_seq_play(&M, 0);
    for (i = 0; i < nact; i++)
        if (A[i].kind == RFM_MOVE_ACT_KIT_NOTE && A[i].down) {
            ok |= A[i].index == 1 && A[i].value == 0 && A[i].track == 13 ? 1 : 0;
            ok |= A[i].index == 1 && A[i].value == 7 ? 2 : 0;
            ok |= A[i].index == 4 ? 4 : 0;              /* (track 14: not the kit's) */
        }
    CHECK(ok == 3);
    CHECK(count(RFM_MOVE_ACT_KIT_NOTE) == 4);           /* (two on, two off) */
}

/* 7. the knobs (pages v2, the owner's 2026-11-26 design): the page's. Sample Kit 1 Right half, 2 Trim; Pad Sample 1
 * Start, 2 End, 3 Transpose (Shift: cents), 4 Engine, 5..8 the engine's first four (2026-11-26; Loop: 5 Loop Start, 6
 * Loop End, 7 Loop Fade); Trig the sequencer's (8 dark); Pad Sample 2 1..4 A D S R, 5, 6 the engine's further, 7 Choke,
 * 8 Reverse; Warp 1 the algorithm, 2 Sync, 3
 * Speed, 4 Pitch, 5, 6 its own; Filter 1 Cutoff, 2 Res, 3 Morph, 4 Env Amount, 5..8 its ADSR; Pad Mix 1 Level, 2 Pan, 3 Cutoff, 5..7
 * Sends; Kit Mix 1 Level, 2 Pan, 3 Cutoff, 4 Res, 5..7 Sends; Shift: fine; the rest dark */
static void t_knobs(void)
{
    const rfm_move_act_t *a;
    uint8_t w[RFM_MOVE_NLEDS][3];
    int k, lit_n = 0;
    kit_view();
    hit(kp(4));
    leds(w);
    for (k = 0; k < 8; k++)
        lit_n += lit(w[L_RING0 + k]);
    CHECK(lit_n == 4 && is_rgb(w[L_RING0 + 2], 149, 35, 93) && !lit(w[L_RING0 + 4]));   /* (Pad Sample, One Shot: 1..4;
                                                         * 5..8 the engine's, none) */
    clear();
    touch(2, 1), knob(2, 6), touch(2, 0);
    CHECK((a = last(RFM_MOVE_ACT_KIT_SET)) && a->index == 4 && a->param == RFM_MOVE_KIT_P_PITCH && a->value == 300);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 300 && M.seq.dirty);
    button(MR_CC_SHIFT, 1), knob(2, 5), button(MR_CC_SHIFT, 0);   /* (Shift: fine, cents) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 305 && M.kit.page == RFM_MOVE_KIT_PG_SAMPLE);
    knob(2, 2);                                         /* (a fine pitch goes to the next whole semitone first) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 400);
    knob(3, 5);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_MOMENTARY);   /* (a list: 5 ticks a step) */
    knob(0, 10);                                        /* (1 Start: never past the end) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_START] == 640);
    knob(1, -3);                                        /* (2 End) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_END] == RFM_MOVE_KIT_FRAC - 192);
    knob(3, 5);                                         /* (Loop) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_LOOP);
    knob(4, 10), knob(6, 30);                           /* (2026-11-26: the engine's on 5..8: 5 Loop Start (from the
                                                         * region's start), 7 Loop Fade) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_LOOPS] == 1280 && M.kit.pad[4].v[RFM_MOVE_KIT_P_LOOPXF] == 30);
    /* Trig: the sequencer's TRIG knobs, 8 dark */
    jog(1);
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_TRIG);
    leds(w);
    for (k = 0, lit_n = 0; k < 8; k++)
        lit_n += lit(w[L_RING0 + k]);
    CHECK(lit_n == 7 && !lit(w[L_RING0 + 7]));
    /* Pad Sample 2 (2026-11-26): 1 Attack, 4 Release, 5, 6 the engine's further (Loop: none), 7 Choke, 8 Reverse */
    jog(1);
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_SAMPLE2);
    leds(w);
    CHECK(lit(w[L_RING0 + 0]) && lit(w[L_RING0 + 3]) && !lit(w[L_RING0 + 4]) && !lit(w[L_RING0 + 5]) && lit(w[L_RING0 + 6]));
    knob(0, 2), knob(3, 4), knob(6, 10), knob(7, 5);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_ATTACK] == 1 && M.kit.pad[4].v[RFM_MOVE_KIT_P_RELEASE] == 2 &&
          M.kit.pad[4].v[RFM_MOVE_KIT_P_CHOKE] == 2 && M.kit.pad[4].v[RFM_MOVE_KIT_P_REVERSE] == 1);
    knob(7, -5);
    /* Warp: 1 the algorithm (Off: the rest dark), 2 Sync, 3 Speed, 4 Pitch (not Repitch's), 5, 6 its own */
    jog(1);
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_WARP);
    leds(w);
    for (k = 0, lit_n = 0; k < 8; k++)
        lit_n += lit(w[L_RING0 + k]);
    CHECK(lit_n == 1);
    knob(0, 5), knob(2, 4);                             /* (Repitch; Speed 0.05 a detent) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_WARP] == RFM_MOVE_KIT_W_REPITCH && M.kit.pad[4].v[RFM_MOVE_KIT_P_SPEED] == 120);
    touch(1, 1), knob(1, 6), touch(1, 0);               /* (Sync on: Speed a multiplier) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_STRETCH] == 2 && M.kit.pad[4].v[RFM_MOVE_KIT_P_SPEED] == 100);
    knob(2, 1);
    leds(w);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_SPEED] == 150 && !lit(w[L_RING0 + 3]));   /* (Repitch: no Pitch) */
    knob(0, 5), knob(3, 4), knob(4, 10), knob(5, 20);   /* (Grains) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_WARP] == RFM_MOVE_KIT_W_GRAINS && M.kit.pad[4].v[RFM_MOVE_KIT_P_WPITCH] == 200 &&
          M.kit.pad[4].v[RFM_MOVE_KIT_P_WGRAIN] == 60 && M.kit.pad[4].v[RFM_MOVE_KIT_P_WJITTER] == 20);
    knob(0, 5), knob(5, 5);                             /* (Beats: 6 Search) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_WARP] == RFM_MOVE_KIT_W_BEATS && M.kit.pad[4].v[RFM_MOVE_KIT_P_WSEARCH] == 15);
    knob(1, -9), knob(3, -4), knob(0, -15);
    CHECK(!M.kit.pad[4].v[RFM_MOVE_KIT_P_WARP] && !M.kit.pad[4].v[RFM_MOVE_KIT_P_STRETCH]);
    /* Filter, Pad Mix */
    jog(1);
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_FILTER);
    knob(0, -10), knob(2, 50), knob(3, -20), knob(4, 2);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_CUTOFF] == 90 && M.kit.pad[4].v[RFM_MOVE_KIT_P_MORPH] == 50 &&
          M.kit.pad[4].v[RFM_MOVE_KIT_P_FENV] == -20 && M.kit.pad[4].v[RFM_MOVE_KIT_P_FATTACK] == 1);
    knob(0, 10), knob(2, -50), knob(3, 20), jog(1);
    knob(0, -6), knob(1, -4), knob(2, -4), knob(4, 10);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_LEVEL] == -3 && M.kit.pad[4].v[RFM_MOVE_KIT_P_PAN] == -4 &&
          M.kit.pad[4].v[RFM_MOVE_KIT_P_CUTOFF] == 96 && M.kit.pad[4].v[RFM_MOVE_KIT_P_SEND1] == 10);
    knob(2, 4), knob(4, -10);
    /* Kit Mix: the kit's strip (the last page: it stops there) */
    jog(1), jog(1);
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_KITMIX);
    clear(), knob(0, -3);
    CHECK(M.kit.mix[RFM_MOVE_KIT_MIX_LEVEL] == rfmv_lvl_step(100, -3) && count(RFM_MOVE_ACT_MIX) >= 1);
    clear(), knob(2, -20);                              /* (3: the kit bus's low-pass) */
    CHECK(M.kit.bus[RFM_MOVE_KIT_BUS_CUTOFF] == 80 && (a = last(RFM_MOVE_ACT_KIT_SET)) && a->index == RFM_MOVE_KIT_BUS &&
          a->param == RFM_MOVE_KIT_BUS_CUTOFF && a->value == 80);
    knob(2, 20);
    /* Sample Kit (the first): the right half, Trim silence */
    jog(-9);
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_KIT);
    knob(0, 5), knob(1, -5);
    CHECK(M.kit.rh == RFM_MOVE_KIT_RH_INKEY && !M.kit.trim);
    knob(0, -5), knob(1, 5), jog(1);
    /* a knob touched: its popup (the name, the value) */
    rest(), touch(0, 1);
    CHECK(M.kit.kpop == 0);
    screen();
    touch(0, 0), rest();
    CHECK(rfmv_now_ms(&M) - M.kit.kpop_ms >= RFM_MOVE_POPUP_MS);   /* (the popup: a second after the release) */
}

/* 8. (pages) the jog: a page a detent, its name and the marks for a second; the click: Files (KIT: Kits), Back closes
 * it; Shift + the click: the pad's tools */
static void t_rows(void)
{
    kit_view();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    CHECK(M.kit.tab == 0 && M.kit.page == RFM_MOVE_KIT_PG_SAMPLE);
    clear(), jog(1);
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_TRIG && M.kit.turned && rfmv_kit_timed(&M));
    CHECKF(strstr(said, "Trig page") != NULL, "said %s", said);   /* (the screen reader: the page) */
    screen();
    dump("pagename");
    jog(-3);
    CHECK(M.kit.page == 0);                             /* (it stops at the ends, as the sequencer's) */
    rest();
    dump("samplekit");
    jog(1), rest();
    back();                                             /* (the pages: nothing more; never out of the view) */
    CHECK(rfmv_kit_active(&M) && M.kit.tab == 0);
    jog_click();                                        /* (Files, its own layer) */
    CHECK(M.kit.tab == 2 && M.kit.place == 0);
    jog(1);                                             /* (2026-11-26: the root My Samples alone: no further) */
    CHECK(M.kit.tab == 2 && M.kit.psel == 0 && M.kit.page == RFM_MOVE_KIT_PG_SAMPLE);
    rfm_move_kit_core_lib(&M, 1), jog(1);               /* (.. and the Move Library when the host has it) */
    CHECK(M.kit.psel == 1);
    rfm_move_kit_core_lib(&M, 0);
    CHECK(M.kit.psel == 0);
    back();
    CHECK(M.kit.tab == 0 && rfmv_kit_active(&M));
    /* Sample Kit's click: Kits (Load, Save) */
    jog(-1), jog_click();
    CHECK(M.kit.tab == 1 && M.kit.kitrow == 0);
    jog(1);
    CHECK(M.kit.kitrow == 1 && M.kit.page == RFM_MOVE_KIT_PG_KIT);
    back();
    CHECK(M.kit.tab == 0);
    jog(1);
    /* (the owner's follow-up) Shift + the click: nothing (the pad's tools gone; the sampler keeps its own) */
    button(MR_CC_SHIFT, 1), jog_click(), button(MR_CC_SHIFT, 0);
    CHECK(!M.kit.tools && M.kit.tab == 0);
    rfm_move_set_speech(&M, 0);
}

/* 9. Files: the places, a folder, the preview, a load and its answer, Back up */
static void t_files(void)
{
    const rfm_move_act_t *a;
    rfm_move_kit_row_t r[3];
    uint8_t pk[64];
    int i;
    kit_view();
    hit(kp(6));
    jog_click();                                        /* (Files, the places; pad 7 empty: nothing looked for) */
    CHECK(M.kit.tab == 2 && M.kit.place == 0 && M.kit.psel == 0 && !count(RFM_MOVE_ACT_KIT_FIND));
    jog(1), clear(), jog_click();                       /* Samples */
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_SAMPLES && (a = last(RFM_MOVE_ACT_KIT_ROWS)) && a->value == 2 && a->take == 0 &&
          a->from == 0 && a->count == 5);              /* (pages v2: the chosen row and two each way, for the slide) */
    memset(r, 0, sizeof r);
    r[0].id = 7, r[0].kind = RFM_MOVE_KIT_ROW_DIR, snprintf(r[0].name, sizeof r[0].name, "Drums");
    r[1].id = 8, r[1].kind = RFM_MOVE_KIT_ROW_FILE, snprintf(r[1].name, sizeof r[1].name, "Clap");
    r[2].id = 9, r[2].kind = RFM_MOVE_KIT_ROW_FILE, snprintf(r[2].name, sizeof r[2].name, "Hat");
    CHECK(rfm_move_kit_rows(&M, 2, 0, 3, 0, 3, r) == 1);
    clear(), jog(1);                                    /* (round A) Clap: at rest, onto the chosen pad (7), heard */
    CHECK(!count(RFM_MOVE_ACT_KIT_LOAD) && !count(RFM_MOVE_ACT_KIT_PREVIEW));
    adv_ms(RFM_MOVE_KIT_LAND_MS + 20);
    CHECK(count(RFM_MOVE_ACT_KIT_LOAD) == 1 && (a = last(RFM_MOVE_ACT_KIT_LOAD)) && a->index == 6 && a->take == 8 &&
          a->value == 2 && !D[last_i(RFM_MOVE_ACT_KIT_LOAD)][0] && M.kit.pad[6].st == RFM_MOVE_KIT_LOADING &&
          !count(RFM_MOVE_ACT_KIT_PREVIEW));
    clear();
    rfm_move_kit_loaded(&M, 6, RFM_MOVE_KIT_OK, "Clap", "/data/UserData/UserLibrary/Samples/Clap.wav", 4800, 48000, 0, NULL);
    CHECK(!count(RFM_MOVE_ACT_KIT_NOTE));              /* (2026-11-26: loaded, not heard: the pad plays it) */
    clear(), jog(1), jog(-1), jog(1);                   /* (a fast spin: one load, the sound it rests on) */
    adv_ms(RFM_MOVE_KIT_LAND_MS - 40);
    CHECK(!count(RFM_MOVE_ACT_KIT_LOAD));
    adv_ms(60);
    CHECKF(count(RFM_MOVE_ACT_KIT_LOAD) == 1 && last(RFM_MOVE_ACT_KIT_LOAD)->take == 9, "loads %d",
           count(RFM_MOVE_ACT_KIT_LOAD));
    adv_ms(500);
    CHECK(count(RFM_MOVE_ACT_KIT_LOAD) == 1);
    clear(), jog(-2), adv_ms(RFM_MOVE_KIT_LAND_MS + 20);   /* (a folder: nothing) */
    CHECK(!count(RFM_MOVE_ACT_KIT_LOAD) && M.kit.fsel[0] == 0);
    for (i = 0; i < 64; i++)
        pk[i] = (uint8_t)(255 - i * 3);
    M.kit.pad[6].v[RFM_MOVE_KIT_P_START] = 500, M.kit.pad[6].v[RFM_MOVE_KIT_P_PITCH] = 300;
    clear();
    CHECK(rfm_move_kit_loaded(&M, 6, RFM_MOVE_KIT_OK, "Hat", "/data/UserData/UserLibrary/Samples/Hat.wav", 9600, 48000, 64, pk) >= 0);
    CHECK(!strcmp(M.kit.pad[6].path, "/data/UserData/UserLibrary/Samples/Hat.wav") && M.kit.pad[6].st == RFM_MOVE_KIT_OK &&
          M.kit.pad[6].npk == 64 && M.kit.pad[6].v[RFM_MOVE_KIT_P_START] == 0 &&   /* (a new sound: its whole region) */
          M.kit.pad[6].v[RFM_MOVE_KIT_P_PITCH] == 300);  /* (.. its settings kept) */
    CHECK(rfm_move_kit_loaded(&M, 5, RFM_MOVE_KIT_TOO_LONG, "Big", NULL, 0, 0, 0, NULL) >= 0 &&
          M.kit.pad[5].st == RFM_MOVE_KIT_TOO_LONG && !strcmp(M.kit.msg, "Too Long"));
    M.kit.pad[6].st = RFM_MOVE_KIT_LOADING, M.kit.pad[6].fresh = 1;   /* (another sound asked for pad 7, refused: the
                                                         * pad keeps Hat) */
    CHECK(rfm_move_kit_loaded(&M, 6, RFM_MOVE_KIT_TOO_LONG, "Big", NULL, 0, 0, 0, NULL) >= 0 &&
          M.kit.pad[6].st == RFM_MOVE_KIT_OK && M.kit.pad[6].frames == 9600 && strstr(M.kit.pad[6].path, "Hat.wav"));
    /* one Undo for the whole browse: the pad as it was before Files (empty) */
    rest(), clear(), press(MR_CC_UNDO);
    CHECK(!M.kit.pad[6].path[0] && (a = last(RFM_MOVE_ACT_KIT_LOAD)) && a->index == 6 && !a->take &&
          !D[last_i(RFM_MOVE_ACT_KIT_LOAD)][0]);
    printf("  ok    Files: the jog at rest on a sound puts it on pad 7 (one load for a fast spin), heard; one Undo\n");
    /* the click keeps it (loaded at once if the jog has not rested), Files closed; Undo again the empty pad */
    jog(2), clear(), jog_click();
    CHECK(count(RFM_MOVE_ACT_KIT_LOAD) == 1 && last(RFM_MOVE_ACT_KIT_LOAD)->take == 9 && M.kit.tab == 0 &&
          M.kit.page == RFM_MOVE_KIT_PG_SAMPLE && !strcmp(M.kit.msg, "Hat"));
    adv_ms(300);
    CHECK(count(RFM_MOVE_ACT_KIT_LOAD) == 1);
    rfm_move_kit_loaded(&M, 6, RFM_MOVE_KIT_OK, "Hat", "/data/UserData/UserLibrary/Samples/Hat.wav", 9600, 48000, 64, pk);
    /* Files again for pad 7: where its sound sits (KIT_FIND, the answer: Samples/Drums, its third row) */
    rest(), clear();
    jog_click();
    CHECK(M.kit.tab == 2 && (a = last(RFM_MOVE_ACT_KIT_FIND)) && a->index == 6 &&
          !strcmp(D[last_i(RFM_MOVE_ACT_KIT_FIND)], "/data/UserData/UserLibrary/Samples/Hat.wav"));
    {
        static const uint32_t F[2] = { 0, 7 };
        static const int16_t S[2] = { 0, 2 };
        CHECK(rfm_move_kit_found(&M, 5, 2, 1, F, S, 9) == 0);   /* (another pad's: not asked) */
        clear();
        rfm_move_kit_found(&M, 6, 2, 1, F, S, 9);
        CHECK(M.kit.place == 2 && M.kit.depth == 1 && M.kit.folder[1] == 7 && M.kit.fsel[1] == 2 && M.kit.fsel[0] == 0);
        CHECK((a = last(RFM_MOVE_ACT_KIT_ROWS)) && a->take == 7 && a->from == 0);   /* (on Hat, the pad's own: pages
                                                         * v2, two rows before it) */
        r[0].id = 11, snprintf(r[0].name, sizeof r[0].name, "Kick"), r[0].kind = RFM_MOVE_KIT_ROW_FILE;
        r[1].id = 12, snprintf(r[1].name, sizeof r[1].name, "Snare");
        CHECK(rfm_move_kit_rows(&M, 2, 7, 3, 0, 3, r) == 1);
        adv_ms(RFM_MOVE_KIT_LAND_MS + 20);
        CHECK(!count(RFM_MOVE_ACT_KIT_LOAD) && M.kit.fsel[1] == 2);
        screen();
        dump("files_found");
    }
    /* a pad pressed in Files: it plays, it is the target (empty: the list stays), pad 7 again: its sound looked for */
    clear(), hit(kp(3));
    CHECK(count(RFM_MOVE_ACT_KIT_NOTE) == 2 && M.kit.cur == 3 && M.kit.br_pad == 4 && !count(RFM_MOVE_ACT_KIT_FIND));
    hit(kp(6));
    CHECK(M.kit.cur == 6 && (a = last(RFM_MOVE_ACT_KIT_FIND)) && a->index == 6 && count(RFM_MOVE_ACT_KIT_NOTE) == 4);
    clear(), hit(kp(6));                                /* (the same pad: plays, no new browse) */
    CHECK(count(RFM_MOVE_ACT_KIT_NOTE) == 2 && !count(RFM_MOVE_ACT_KIT_FIND));
    button(MR_CC_SHIFT, 1), hit(kp(3)), button(MR_CC_SHIFT, 0);   /* (Shift + pad 4: the target, silent) */
    clear(), jog(-1), jog_click();                      /* (Snare at once on pad 4: the click before the rest) */
    CHECK((a = last(RFM_MOVE_ACT_KIT_LOAD)) && a->index == 3 && a->take == 12 && count(RFM_MOVE_ACT_KIT_LOAD) == 1 &&
          M.kit.tab == 0);
    rest();
    CHECK(count(RFM_MOVE_ACT_KIT_LOAD) == 1);
    clear(), press(MR_CC_UNDO);                         /* (Undo: the last target's pad, pad 4, empty again) */
    CHECK((a = last(RFM_MOVE_ACT_KIT_LOAD)) && a->index == 3 && !a->take && !M.kit.pad[3].path[0] &&
          !strcmp(M.kit.pad[6].path, "/data/UserData/UserLibrary/Samples/Hat.wav"));
    printf("  ok    Files on the pad's sound (KIT_FIND); a pad pressed there the target; the click keeps; Undo the last target's\n");
    /* Back: a folder up, the places, Files closed (never out of the view) */
    rest();
    jog_click();
    CHECK(M.kit.tab == 2 && M.kit.place == 2 && M.kit.depth == 1);
    rfm_move_kit_rows(&M, 2, 7, 0, 0, 0, NULL);
    screen();
    dump("empty_folder");
    back();
    CHECK(M.kit.depth == 0);
    back();
    CHECK(M.kit.place == 0 && M.kit.psel == 0);         /* (2026-11-26: My Samples, the root's first) */
    back();
    CHECK(M.kit.tab == 0 && rfmv_kit_active(&M));
    back();
    CHECK(M.kit.tab == 0 && rfmv_kit_active(&M));
    /* (2026-11-26) no Takes in the root: My Samples, the Move Library (its rows asked as a place's) */
    rfm_move_kit_core_lib(&M, 1);
    jog_click(), jog(-1), clear(), jog_click();
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_SAMPLES && (a = last(RFM_MOVE_ACT_KIT_ROWS)) && a->value == RFM_MOVE_KIT_PL_SAMPLES);
    back(), jog(1), clear(), jog_click();
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_CORE && (a = last(RFM_MOVE_ACT_KIT_ROWS)) && a->value == RFM_MOVE_KIT_PL_CORE);
    rfm_move_kit_core_lib(&M, 0);                       /* (gone: the root) */
    CHECK(M.kit.place == 0 && M.kit.psel == 0);
    back();
}

/* 10. Delete / Mute held + a pad; Undo */
static void t_delete(void)
{
    const int clip = SEQ_CLIP(12, 0);
    uint8_t w[RFM_MOVE_NLEDS][3];
    kit_view();
    sound(2, "Snare", 4800, NULL, 0);
    hit(kp(2)), tap_step(0), tap_step(8);
    CHECK(rfmv_seq_find(&M, clip, 8, 38) >= 0);
    clear();
    button(MR_CC_DELETE, 1), hit(kp(2));                /* (its steps first) */
    CHECK(rfmv_seq_find(&M, clip, 0, 38) < 0 && rfmv_seq_find(&M, clip, 8, 38) < 0 && M.kit.pad[2].path[0] &&
          !count(RFM_MOVE_ACT_KIT_NOTE));
    hit(kp(2));                                         /* (none left: the pad emptied) */
    button(MR_CC_DELETE, 0);
    CHECK(!M.kit.pad[2].path[0] && last(RFM_MOVE_ACT_KIT_LOAD) && last(RFM_MOVE_ACT_KIT_LOAD)->index == 2);
    leds(w);
    CHECK(lit(w[L_WHITE0 + W_UNDO]));
    press(MR_CC_UNDO);                                  /* (the pad back) */
    CHECK(M.kit.pad[2].path[0]);
    press(MR_CC_UNDO);                                  /* (then the sequencer's: the steps back) */
    CHECK(rfmv_seq_find(&M, clip, 8, 38) >= 0);
    clear();
    button(MR_CC_MUTE, 1), hit(kp(2)), button(MR_CC_MUTE, 0);
    CHECK(M.kit.pad[2].v[RFM_MOVE_KIT_P_MUTE] == 1 && last(RFM_MOVE_ACT_KIT_SET) &&
          last(RFM_MOVE_ACT_KIT_SET)->param == RFM_MOVE_KIT_P_MUTE && !count(RFM_MOVE_ACT_KIT_NOTE));
    hit(kp(0)), rest();
    leds(w);
    CHECK(is_rgb(w[L_PAD0 + kp(2)], 39, 39, 39));       /* (muted: grey) */
    CHECK(!M.seq.tr[12].mute);                          /* (the pad's, not the track's) */
}

/* 11. a pad held + Sample: the take into the pad (the sampler's settings), its screens, the pad let go */
static void t_rec(void)
{
    const rfm_move_act_t *a;
    rfm_move_smp_status_t st;
    uint8_t w[RFM_MOVE_NLEDS][3];
    kit_view();
    M.s.smp[RFM_MOVE_SMP_MODE] = RFM_MOVE_SMP_M_MANUAL;
    pad(kp(6), 100);
    clear(), press(MR_CC_SAMPLE);
    CHECK((a = last(RFM_MOVE_ACT_KIT_REC)) && a->index == 6 && a->down == 1 && a->value == RFM_MOVE_SMP_M_MANUAL);
    CHECK(!count(RFM_MOVE_ACT_SMP_PRESS) && !M.smp.open && M.kit.rec_pad == 7);
    memset(&st, 0, sizeof st);
    st.state = RFM_MOVE_SMP_ST_RECORDING, st.rate = 48000, st.frames = 96000;
    rfm_move_smp_status(&M, &st);
    leds(w);
    CHECK(is_rgb(w[L_SAMPLE], 255, 0, 0) && is_rgb(w[L_PAD0 + kp(6)], 255, 255, 255));
    screen();
    clear(), pad(kp(6), 0);
    CHECK((a = last(RFM_MOVE_ACT_KIT_REC)) && a->index == 6 && a->down == 0 && M.kit.rec_pad == 7);
    st.state = RFM_MOVE_SMP_ST_IDLE;
    rfm_move_smp_status(&M, &st);
    rfm_move_kit_loaded(&M, 6, RFM_MOVE_KIT_OK, "Take 3", "/data/UserData/UserLibrary/Samples/RemoteFM1/2026-11-25/t3.wav",
                        96000, 48000, 0, NULL);
    CHECK(!M.kit.rec_pad && !strcmp(M.kit.pad[6].name, "Take 3"));
    /* armed (Threshold), the pad let go: dropped, the kit's screen again */
    pad(kp(3), 100), press(MR_CC_SAMPLE);
    st.state = RFM_MOVE_SMP_ST_ARMED;
    rfm_move_smp_status(&M, &st);
    CHECK(M.kit.rec_pad == 4);
    pad(kp(3), 0);
    CHECK(!M.kit.rec_pad);
    st.state = RFM_MOVE_SMP_ST_IDLE;
    rfm_move_smp_status(&M, &st);
    /* Sample alone: the sampler app as ever */
    clear(), press(MR_CC_SAMPLE);
    CHECK(M.smp.open && !count(RFM_MOVE_ACT_KIT_REC));
    /* the app over the kit: the jog the app's, the pads the kit's */
    jog(1);
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_SAMPLE);
    clear(), hit(kp(1));
    CHECK(count(RFM_MOVE_ACT_KIT_NOTE) == 2);
    back();
    CHECK(!M.smp.open && rfm_move_kit_shown(&M));
}

/* 12. the sampler's To Kit: the app closes on the Kit view, the take waits for its pad */
static void t_to_kit(void)
{
    const rfm_move_act_t *a;
    rfm_move_smp_row_t r;
    int i;
    kit_view();
    tap_track(0), rest();                               /* (out of the kit: the FM-1 layout; Sample the FM-1's there) */
    hold_track(3), press(MR_CC_SAMPLE), track(3, 0), rest();   /* (Track 4 held + Sample: the app) */
    rfm_move_smp_offer(&M, 0, 0, 0x7, 0);
    CHECK(M.smp.open);
    memset(&r, 0, sizeof r);
    r.id = 42, r.kind = RFM_MOVE_SMP_ROW_TAKE, r.src = RFM_MOVE_SMP_SRC_MASTER, r.frames = 48000, r.rate = 48000;
    M.smp.cur = r, M.smp.scr = 1, M.smp.ai = 0;         /* (a take's actions) */
    for (i = 0; i < 6; i++)
        jog(1);
    screen();
    jog_click();
    CHECK(!M.smp.open && rfmv_kit_active(&M) && M.kit.pend_take == 42);
    rest();
    clear(), hit(kp(9));
    CHECK((a = last(RFM_MOVE_ACT_KIT_LOAD)) && a->index == 9 && a->take == 42 && a->value == RFM_MOVE_KIT_PL_TAKES &&
          !M.kit.pend_take && M.kit.cur == 9 && !count(RFM_MOVE_ACT_KIT_NOTE));
}

/* 13. a held Track's hold on track 13..16: to the firmware, again back to the kit; its steps in the kit's colours */
static void t_route(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    kit_view();
    hold_track(0);
    leds(w);
    CHECK(is_rgb(w[L_STEP0 + 12], 199, 46, 125) && is_rgb(w[L_STEP0 + 15], 132, 93, 199));   /* (board T) */
    step(13, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 30), step(13, 0);
    CHECK(!(M.kit.route >> 13 & 1) && (M.seq.fx.ffilt[0] >> 13 & 1));
    adv_ms(400);
    step(13, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 30), step(13, 0);
    CHECK((M.kit.route >> 13 & 1) && !(M.seq.fx.ffilt[0] >> 13 & 1));
    track(0, 0), rest();
}

/* 14. the project: the kit saved (version 11) and loaded (its sounds asked for by path, its settings sent); a bad kit
 * refused; a version 10 project keeps its routing, an empty kit; a new project routes 13..16 to the kit */
static void t_project(void)
{
    static uint8_t b[RFM_MOVE_PROJECT_MAX], c[RFM_MOVE_PROJECT_MAX];
    int n, n10, i, loads = 0;
    uint32_t h;
    kit_view();
    sound(3, "Rim", 4800, NULL, 0);
    M.kit.pad[3].v[RFM_MOVE_KIT_P_PITCH] = -250, M.kit.pad[3].v[RFM_MOVE_KIT_P_CHOKE] = 2;
    M.kit.rh = RFM_MOVE_KIT_RH_INKEY, M.kit.cur = 3, M.kit.route = 0xB000;
    rfmv_seq_note_put(&M, SEQ_CLIP(12, 0), 2, rfm_move_kit_note(3, 9), 100, 24, 0);
    n = rfm_move_project_save(&M, b, sizeof b);
    CHECK(n == RFM_MOVE_PROJECT_FIXED + 8 && b[4] == RFM_MOVE_PROJECT_VERSION && rfm_move_project_check(b, (uint32_t)n) == 0);
    clear();
    rfm_move_init(&M2, NULL, on_act, NULL);
    rfm_move_fw_caps(&M2, KCAPS);
    clear();
    CHECK(rfm_move_project_load(&M2, 1, b, (uint32_t)n) == 0);
    CHECK(!strcmp(M2.kit.pad[3].path, M.kit.pad[3].path) && M2.kit.pad[3].v[RFM_MOVE_KIT_P_PITCH] == -250 &&
          M2.kit.pad[3].v[RFM_MOVE_KIT_P_CHOKE] == 2 && M2.kit.rh == RFM_MOVE_KIT_RH_INKEY && M2.kit.cur == 3 &&
          M2.kit.route == 0xB000 && M2.kit.pad[3].st == RFM_MOVE_KIT_LOADING);
    for (i = 0; i < nact; i++)
        if (A[i].kind == RFM_MOVE_ACT_KIT_LOAD && A[i].index == 3)
            loads += !strcmp(D[i], M.kit.pad[3].path);
    CHECK(loads == 1 && count(RFM_MOVE_ACT_KIT_SET) == 16 * RFM_MOVE_KIT_P_N + RFM_MOVE_KIT_BUS_N);
    {                                                   /* the right half's pitch on the step, through the load */
        int s = -1;
        const int j = rfmv_seq_find(&M2, SEQ_CLIP(12, 0), 2, rfm_move_kit_note(3, 9));
        CHECK(j >= 0 && rfm_move_kit_pad_of(12, M2.seq.notes[j].note, &s) == 3 && s == 9);
    }
    /* bad kit bytes: refused */
    for (i = 0; i < 4; i++) {
        uint32_t k;
        memcpy(c, b, (size_t)n);
        CHECK(i || (b[n - 4 - RFM_MOVE_KIT_BYTES + 3] == 3 && b[n - 4 - RFM_MOVE_KIT_BYTES + 4] == 100 &&
                    b[n - 4 - RFM_MOVE_KIT_BYTES + 5] == 64));   /* (12: the chosen pad, the kit's strip) */
        switch (i) {
        case 0: c[n - 4 - RFM_MOVE_KIT_BYTES] = 9; break;              /* (no such layout) */
        case 1: c[n - 4 - RFM_MOVE_KIT_BYTES + 1] = 1; break;          /* (track 1 to the kit) */
        case 2: c[n - 4 - RFM_MOVE_KIT_BYTES + 40 + 159] = 'x'; break; /* (a path with no NUL) */
        default: c[n - 4 - RFM_MOVE_KIT_BYTES + 40 + 160 + 2 * RFM_MOVE_KIT_P_START] = 0xFF,
                 c[n - 4 - RFM_MOVE_KIT_BYTES + 40 + 160 + 2 * RFM_MOVE_KIT_P_START + 1] = 0x7F; break;   /* (start past
                                                         * the end) */
        }
        for (h = 2166136261u, k = 0; k < (uint32_t)n - 4; k++)
            h = (h ^ c[k]) * 16777619u;
        for (k = 0; k < 4; k++)
            c[n - 4 + k] = (uint8_t)(h >> (8 * k));
        CHECKF(rfm_move_project_check(c, (uint32_t)n) == -9, "bad kit %d", i);
    }
    /* version 10 (no kit): its routing as it was, an empty kit */
    memcpy(c, b, (size_t)n);
    n = (int)proj_v15(c, (uint32_t)n);                  /* (2026-11-26: 7 bytes a note before 16) */
    n10 = n - RFM_MOVE_KIT_BYTES_V15;
    c[4] = 10;
    for (h = 2166136261u, i = 0; i < n10 - 4; i++)
        h = (h ^ c[i]) * 16777619u;
    for (i = 0; i < 4; i++)
        c[n10 - 4 + i] = (uint8_t)(h >> (8 * i));
    CHECK(rfm_move_project_check(c, (uint32_t)n10) == 0 && rfm_move_project_check(c, (uint32_t)n) < 0);
    CHECK(rfm_move_project_load(&M2, 2, c, (uint32_t)n10) == 0 && !M2.kit.route && !M2.kit.pad[3].path[0] &&
          M2.seq.fx.ffilt[0] == M.seq.fx.ffilt[0]);
    /* a new (empty) project: tracks 13..16 to the kit, no firmware */
    M2.seq.fx.ffilt[1] = 0xFFFF;
    CHECK(rfm_move_project_load(&M2, 3, NULL, 0) == 0 && M2.kit.route == 0xF000 && !(M2.seq.fx.ffilt[1] & 0xF000));
}

/* 15. the settings: version 9 keeps Track 4's view; a version 8 image loads (the Mixer) */
static void t_settings(void)
{
    uint8_t b[RFM_MOVE_SETTINGS_BYTES];
    rfm_move_settings_t s, t;
    rfm_move_settings_default(&s);
    s.t4_kit = 1;
    CHECK(rfm_move_save(&s, b) == RFM_MOVE_SETTINGS_BYTES && b[3] == 9 && b[RFM_MOVE_SETTINGS_BYTES - 1] == 1);
    CHECK(rfm_move_load(&t, b, sizeof b) == 0 && t.t4_kit == 1);
    b[3] = 8, b[4] = RFM_MOVE_SETTINGS_BYTES_V8 & 0xFF, b[5] = RFM_MOVE_SETTINGS_BYTES_V8 >> 8;
    CHECK(rfm_move_load(&t, b, RFM_MOVE_SETTINGS_BYTES_V8) == 0 && t.t4_kit == 0);
}

/* 16. the layers over it: a Shift menu (its screen, knobs, jog), Loop (the loop's pads), a held Track */
static void t_layers(void)
{
    kit_view();
    button(MR_CC_SHIFT, 1), tap_step(4), button(MR_CC_SHIFT, 0);   /* (Shift + step 5: Tempo) */
    CHECK(M.seq.smenu && !rfm_move_kit_shown(&M) && rfmv_kit_active(&M));
    clear(), knob(0, 3);
    CHECK(!count(RFM_MOVE_ACT_KIT_SET));               /* (the menu's knob) */
    clear(), hit(kp(1));
    CHECK(count(RFM_MOVE_ACT_KIT_NOTE) == 2);          /* (the pads stay the kit's) */
    back();
    CHECK(!M.seq.smenu && rfm_move_kit_shown(&M));
    button(MR_CC_LOOP, 1), clear(), hit(kp(1)), button(MR_CC_LOOP, 0);
    CHECK(!count(RFM_MOVE_ACT_KIT_NOTE));              /* (Loop: the loop's length) */
    rest();
    hold_track(1), clear(), hit(26);
    CHECK(!count(RFM_MOVE_ACT_KIT_NOTE));              /* (the held Track's menu) */
    track(1, 0), rest();
}

/* 17. the screens against the mock-up's boards */
static void t_mock(void)
{
    static const box_t AHR_E = { 0, 48, 128, 16 }, AHR_F = { 0, 36, 128, 28 };
    static const box_t ICONS_V = { 0, 24, 128, 40 }, TABS = { 0, 0, 128, 15 };
    uint8_t pk[64];
    int out, n;
    kit_view();
    M.seq.bpm10 = 1200;
    /* A, rest: pad 5, Pitch +3 chosen, its wave (a hit), the steps it lasts (4) */
    n = mock_peaks(128, 11, 55u, 1, pk);
    hit(kp(4));
    sound(4, "Snare Rim", 28541, pk, n);
    M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] = 300;
    rest();
    dump("rest");                                       /* (pages, 2026-11-26: boards A and B, the rows, no longer
                                                         * apply: the SOUND page drawn, not compared) */
    /* C, a knob touched: Pitch's popup and the steps it lasts (pages v2: "Transpose", knob 3: drawn, not compared) */
    rest(), touch(2, 1);
    dump("pitch");
    touch(2, 0), rest();
    /* E, Play: Loop; F, Attack 20 ms (the envelopes' proportions are the pad's own: masked). (pages v2: Play's
     * popup has a fifth icon, the Stretch engine: drawn, not compared) */
    M.kit.pad[4].v[RFM_MOVE_KIT_P_MODE] = RFM_MOVE_KIT_M_LOOP, M.kit.pad[4].v[RFM_MOVE_KIT_P_ATTACK] = 20;
    M.kit.pad[4].v[RFM_MOVE_KIT_P_RELEASE] = 300;
    touch(3, 1);                                        /* (pages v2: Pad Sample's knob 4) */
    (void)AHR_E;
    dump("mode");
    touch(3, 0), rest(), M.kit.page = RFM_MOVE_KIT_PG_SAMPLE2, touch(0, 1);   /* (2026-11-26: Pad Sample 2's knob 1) */
    out = vs_ref("attack", &AHR_F, 1);
    CHECKF(out == 0, "attack: %d differ", out);
    dump("attack");
    touch(0, 0), rest(), M.kit.page = RFM_MOVE_KIT_PG_SAMPLE;
    dump("envelope");
    /* G, Choke: group 2, pads 9 and 10 in it (pad 11 chosen) */
    sound(8, "Hat", 4800, NULL, 0), sound(9, "Hat2", 4800, NULL, 0), sound(10, "Hat3", 4800, NULL, 0);
    M.kit.pad[8].v[RFM_MOVE_KIT_P_CHOKE] = M.kit.pad[9].v[RFM_MOVE_KIT_P_CHOKE] = M.kit.pad[10].v[RFM_MOVE_KIT_P_CHOKE] = 2;
    button(MR_CC_SHIFT, 1), hit(kp(10)), button(MR_CC_SHIFT, 0);
    dump("padsample");
    jog(2), rest(), touch(6, 1);                        /* (pages v2: Pad Sample 2's knob 7) */
    out = vs_ref("choke", NULL, 0);
    CHECKF(out == 0, "choke: %d differ", out);
    dump("choke");
    touch(6, 0), rest(), jog(-2), rest();
    /* Q, the Kit tab (3b): Load, Save (chosen), the right half; the kit's name. S, In Key: C minor (its popup) */
    snprintf(M.kit.name, sizeof M.kit.name, "Kit 3");
    button(MR_CC_SHIFT, 1), hit(kp(4)), button(MR_CC_SHIFT, 0);
    M.s.root = 0, M.s.scale = RFM_MOVE_SCALE_MINOR;
    jog(2), rest();
    dump("pagesample2");
    jog(1), rest();
    dump("warp");                                       /* (pages v2: Warp, where Stretch was) */
    jog(1), rest();
    dump("filter");
    jog(1), rest();
    dump("padmix");
    jog(1), rest();
    dump("kitmix");
    jog_click(), jog(1), rest();                        /* (Kits: Load, Save; board Q's tabs and right half row no
                                                         * longer apply: drawn, not compared) */
    CHECK(M.kit.tab == 1 && M.kit.kitrow == 1);
    dump("kittab");
    back();
    M.kit.rh = RFM_MOVE_KIT_RH_INKEY;
    jog(-7), rest();
    dump("samplekit2");
    touch(0, 1);                                        /* (pages: Sample Kit's knob 1) */
    out = vs_ref("inkey", NULL, 0);
    CHECKF(out == 0, "inkey: %d differ", out);
    dump("inkey");
    touch(0, 0), rest(), jog(1), rest();
    /* I, the places (3b: Kits the third); J, a folder: three sounds, their pictures */
    rfm_move_kit_core_lib(&M, 1);                       /* (2026-11-26: My Samples, the Move Library) */
    jog_click(), jog(1), rest();
    CHECK(M.kit.tab == 2 && M.kit.psel == 1);
    dump("places");                                     /* (pages: the owner's browser: arrows, no tabs; boards I
                                                         * and J drawn, not compared) */
    out = 0, (void)TABS;
    jog_click();
    {
        rfm_move_kit_row_t r[3];
        static const char *const F[3] = { "Kick 808", "Rim 2", "Snare 4" };
        int i, c;
        memset(r, 0, sizeof r);
        for (i = 0; i < 3; i++) {
            uint8_t t[16];
            r[i].id = (uint32_t)(20 + i), r[i].kind = RFM_MOVE_KIT_ROW_FILE;
            snprintf(r[i].name, sizeof r[i].name, "%s", F[i]);
            mock_peaks(28, 5, 9u + (unsigned)i, 1, t);
            for (c = 0; c < RFM_MOVE_SMP_THUMB; c++)
                r[i].thumb[c] = t[c];
        }
        rfm_move_kit_rows(&M, 2, 0, 3, 0, 3, r);
        jog(1), rest();
        out = 0;
        dump("files");
    }
    back(), back(), rest();
    /* V, the Sound row's tools: gone (the owner's follow-up): not compared */
    (void)ICONS_V;
    /* L, armed (Threshold -30 dB, the level 6 px); M, recording (2 s, 35 columns of the take) */
    {
        rfm_move_smp_status_t st;
        int i;
        memset(&st, 0, sizeof st);
        M.s.smp[RFM_MOVE_SMP_MODE] = RFM_MOVE_SMP_M_THRESHOLD, M.s.smp[RFM_MOVE_SMP_THRESHOLD] = -30;
        button(MR_CC_SHIFT, 1), hit(kp(6)), button(MR_CC_SHIFT, 0);
        pad(kp(6), 100), press(MR_CC_SAMPLE);
        st.state = RFM_MOVE_SMP_ST_ARMED, st.level = (uint16_t)level_for(6, 28);
        rfm_move_smp_status(&M, &st);
        while (rfmv_now_ms(&M) % 500u >= 250u)
            adv_ms(10);
        out = vs_ref("armed", NULL, 0);
        CHECKF(out == 0, "armed: %d differ", out);
        dump("armed");
        st.state = RFM_MOVE_SMP_ST_RECORDING, st.level = (uint16_t)level_for(22, 28), st.rate = 48000, st.frames = 2 * 48000;
        {
            uint8_t w70[64];
            const int nw = mock_peaks(70, 15, 41u, 0, w70);
            for (i = 0; i < nw; i++)
                st.peaks[i] = w70[i];
            st.npeaks = (uint8_t)nw, st.span = 0;
        }
        rfm_move_smp_status(&M, &st);
        out = vs_ref("recording", NULL, 0);
        CHECKF(out == 0, "recording: %d differ", out);
        dump("recording");
        pad(kp(6), 0);
        st.state = RFM_MOVE_SMP_ST_IDLE;
        rfm_move_smp_status(&M, &st);
    }
    printf("mock boards: pitch, mode, attack, choke, inkey, padact, armed, recording\n");
}

/* ================================================================ (stage 3b) === */
static const uint8_t *L(int i)                          /* (an LED of the last leds()) */
{
    static uint8_t w[RFM_MOVE_NLEDS][3];
    if (i < 0)
        leds(w);
    return w[i < 0 ? 0 : i];
}
/* 18. (pages v2: the Stretch engine's Stretch page, knob 1 Sync) Stretch: knob 3 (lit), the steps it lasts (the popup's grid and the step keys, board D), KIT_SET's microseconds
 * at the tempo and the track's rate, sent again when either changes; Pitch ignored ("Stretch") */
static void t_stretch(void)
{
    const rfm_move_act_t *a;
    char v[32];
    int i, out;
    kit_view();
    M.seq.bpm10 = 1200;
    hit(kp(8));                                         /* (pad 9: track 15, 1/16) */
    sound(8, "Loop", 96000, NULL, 0);
    M.kit.pad[8].v[RFM_MOVE_KIT_P_WARP] = RFM_MOVE_KIT_W_REPITCH;   /* (pages v2: Warp Repitch, Sync its knob 2) */
    rest(), clear();
    jog(3), touch(1, 1), knob(1, 48);                   /* (3 ticks a step: 16 steps) */
    CHECK(M.kit.pad[8].v[RFM_MOVE_KIT_P_STRETCH] == 16);
    CHECKF((a = last(RFM_MOVE_ACT_KIT_SET)) && a->param == RFM_MOVE_KIT_P_STRETCH && a->value == 16 && a->from == 2000000u,
           "from %u", a ? a->from : 0);                 /* (16 x 125 ms) */
    (void)out;                                          /* (pages v2: "Sync": drawn, not compared) */
    dump("stretch");
    L(-1);
    CHECK(is_rgb(L(L_RING0 + 1), 255, 255, 255) && is_rgb(L(L_STEP0), 119, 219, 254) && is_rgb(L(L_STEP0 + 5), 51, 94, 109) &&
          is_rgb(L(L_STEP0 + 15), 51, 94, 109));        /* (board D: the steps it lasts, the first brightest) */
    knob(1, -36);                                       /* (4 steps: the rest dark) */
    L(-1);
    CHECK(M.kit.pad[8].v[RFM_MOVE_KIT_P_STRETCH] == 4 && lit(L(L_STEP0 + 3)) && !lit(L(L_STEP0 + 4)));
    touch(1, 0), rest();
    L(-1);
    CHECK(!is_rgb(L(L_STEP0 + 1), 51, 94, 109));        /* (the popup gone: the pad's steps again) */
    /* the tempo halved: sent again, twice as long; the track's rate: again */
    clear(), M.seq.bpm10 = 600, adv_ms(20);
    CHECK((a = last(RFM_MOVE_ACT_KIT_SET)) && a->index == 8 && a->param == RFM_MOVE_KIT_P_STRETCH && a->from == 1000000u &&
          count(RFM_MOVE_ACT_KIT_SET) == 1);
    clear(), M.seq.tr[14].rate = (uint8_t)(M.seq.tr[14].rate + 1), adv_ms(20);
    CHECK(count(RFM_MOVE_ACT_KIT_SET) == 1 && last(RFM_MOVE_ACT_KIT_SET)->from != 1000000u);
    clear(), adv_ms(20);
    CHECK(!count(RFM_MOVE_ACT_KIT_SET));                /* (no change: nothing) */
    /* (pages v2) Repitch with Sync: Transpose ignored ("Fit", the owner's rule); Sync's popup lights the steps */
    M.seq.tr[14].rate = (uint8_t)(M.seq.tr[14].rate - 1), M.seq.bpm10 = 1200, adv_ms(20);
    {
        uint16_t subj;
        uint32_t st;
        jog(-3), rest(), touch(2, 1);
        rfmv_kit_pop_words(&M, v, sizeof v, &subj, &st);
        CHECKF(!strcmp(v, "Transpose: Fit"), "said %s", v);
        touch(2, 0), rest();
    }
    jog(3), rest(), touch(1, 1);
    L(-1);
    CHECK(lit(L(L_STEP0 + 3)) && !lit(L(L_STEP0 + 4)));
    knob(1, -12);
    CHECK(!M.kit.pad[8].v[RFM_MOVE_KIT_P_STRETCH]);
    touch(1, 0), rest();
    for (i = 0; i < 1; i++)
        CHECK(M.kit.pad[8].v[RFM_MOVE_KIT_P_PITCH] == 0);
}
/* 19. the sends (board H): pages, Pad Mix's knobs 3..5 */
static void t_sends(void)
{
    const rfm_move_act_t *a;
    int out;
    kit_view();
    hit(kp(5)), jog(5), rest();                         /* (pad 6; Pad Mix: pages v2, Trig, Pad Sample 2, Warp,
                                                         * Filter on the way) */
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_PADMIX);
    touch(4, 1), knob(4, 35);                           /* (pages v2: knob 5 Send 1) */
    CHECK(M.kit.pad[5].v[RFM_MOVE_KIT_P_SEND1] == 35 && (a = last(RFM_MOVE_ACT_KIT_SET)) && a->param == RFM_MOVE_KIT_P_SEND1);
    out = vs_ref("send", NULL, 0);
    CHECKF(out == 0, "send: %d differ", out);
    dump("send");
    touch(4, 0), rest();
    knob(5, 20);
    CHECK(M.kit.pad[5].v[RFM_MOVE_KIT_P_SEND2] == 20);
    knob(6, 60), knob(6, 60);
    CHECK(M.kit.pad[5].v[RFM_MOVE_KIT_P_SEND3] == 100);
    rest(), jog(-5), rest();
}
/* 20. the kit's strip: the Mixer's knob 4 and column 4; Routing's kit pad (its strip on knobs 1..5, its tracks) */
static void t_mixer(void)
{
    const rfm_move_act_t *a;
    int i;
    fresh();
    CHECK((a = last(RFM_MOVE_ACT_MIX)) && a->slot == RFM_MOVE_KIT_SLOT && a->mix[0] == 100 && a->mix[1] == 64);   /* (the
                                                         * capability: the kit's strip told) */
    tap_track(3), rest();
    CHECK(M.seq.projects && rfmv_t4_page(&M) == 0);
    L(-1);
    CHECK(lit(L(L_RING0 + 3)) && !lit(L(L_RING0 + 4)) && lit(L(L_PAD0 + 3)) && lit(L(L_PAD0 + 24 + 3)));   /* (knob 4; the
                                                         * kit's column at 100 of 127: four pads) */
    clear(), touch(3, 1), knob(3, -12);                /* (2026-11-26: 0.5 dB a detent: -2.1 -> -8.1 dB, 50) */
    CHECK((a = last(RFM_MOVE_ACT_MIX)) && a->slot == RFM_MOVE_KIT_SLOT && a->mix[0] == 50 && M.kit.mix[0] == 50 && M.seq.dirty);
    dump("mixer_kit_pop");
    touch(3, 0), rest();
    dump("mixer_kit");                                  /* (four strips: the kit the fourth) */
    L(-1);
    CHECK(lit(L(L_PAD0 + 8 + 3)) && !lit(L(L_PAD0 + 24 + 3)));
    hit(29);                                            /* (the right 4 x 4's top row, second: Pan) */
    clear(), knob(3, -10);
    CHECK(M.fw.mix_par == RFM_MOVE_MIXP_PAN && (a = last(RFM_MOVE_ACT_MIX)) && a->mix[1] == 54);
    /* Routing: the kit's pad (column 4, top), its strip, its tracks */
    tap_track(3), rest();
    CHECK(rfmv_t4_page(&M) == 1);
    hit(27);
    CHECK(M.fw.rt_kit);
    L(-1);
    CHECK(is_rgb(L(L_PAD0 + 27), 255, 60, 160) && !lit(L(L_STEP0 + 3)) && lit(L(L_STEP0 + 12)));
    clear(), knob(2, 30);                               /* (knob 3: Send 1) */
    CHECK((a = last(RFM_MOVE_ACT_MIX)) && a->slot == RFM_MOVE_KIT_SLOT && a->mix[2] == 30);
    tap_step(12);
    CHECK(!(M.kit.route >> 12 & 1));
    M.seq.fx.ffilt[0] |= 1u << 12;
    tap_step(12);
    CHECK((M.kit.route >> 12 & 1) && !(M.seq.fx.ffilt[0] >> 12 & 1));   /* (the kit's alone) */
    clear(), tap_step(2);
    CHECK(!nact && M.kit.route == 0xF000);              /* (1..12: dead) */
    hit(16 + 2);                                        /* (a firmware's pad: the kit no longer chosen) */
    CHECK(!M.fw.rt_kit);
    for (i = 0; i < 1; i++)
        back();
    /* the project keeps the strip */
    {
        static uint8_t b[RFM_MOVE_PROJECT_MAX];
        const int n = rfm_move_project_save(&M, b, sizeof b);
        rfm_move_init(&M2, NULL, on_act, NULL);
        rfm_move_fw_caps(&M2, KCAPS);
        clear();
        CHECK(rfm_move_project_load(&M2, 1, b, (uint32_t)n) == 0 && M2.kit.mix[0] == 50 && M2.kit.mix[1] == 54 &&
              M2.kit.mix[2] == 30 && (a = last(RFM_MOVE_ACT_MIX)) && a->slot == RFM_MOVE_KIT_SLOT && a->mix[2] == 30);
    }
}
/* 21. a whole folder: Shift + the click on a folder, All To Pads (board K); the host's answers; one Undo */
static void t_folder(void)
{
    const rfm_move_act_t *a;
    rfm_move_kit_row_t r[2];
    int k, out;
    kit_view();
    sound(0, "Old", 4800, NULL, 0);
    M.kit.pad[0].v[RFM_MOVE_KIT_P_PITCH] = 500;
    jog_click(), jog(1), jog_click();                   /* (Files, Samples) */
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_SAMPLES);
    memset(r, 0, sizeof r);
    r[0].id = 70, r[0].kind = RFM_MOVE_KIT_ROW_DIR, snprintf(r[0].name, sizeof r[0].name, "808");
    r[1].id = 71, r[1].kind = RFM_MOVE_KIT_ROW_FILE, snprintf(r[1].name, sizeof r[1].name, "Clap");
    rfm_move_kit_rows(&M, 2, 0, 2, 0, 2, r);
    button(MR_CC_SHIFT, 1), jog_click(), button(MR_CC_SHIFT, 0);
    CHECK(M.kit.fact && M.kit.fai == 1 && M.kit.depth == 0);
    {
        static const box_t TOP = { 0, 0, 128, 24 };     /* (the mock's stand-in wave above the icons) */
        out = vs_ref("folder", &TOP, 1);
        CHECKF(out == 0, "folder: %d differ", out);
    }
    dump("folder");
    L(-1);
    CHECK(is_rgb(L(L_PAD0 + kp(15)), 132, 93, 199) && !lit(L(L_PAD0 + rp(0))));   /* (every pad: where they go) */
    jog(-1);
    CHECK(M.kit.fai == 0);
    back();
    CHECK(!M.kit.fact && M.kit.place == RFM_MOVE_KIT_PL_SAMPLES);
    button(MR_CC_SHIFT, 1), jog_click(), button(MR_CC_SHIFT, 0);
    clear(), jog_click();
    CHECK((a = last(RFM_MOVE_ACT_KIT_FOLDER)) && a->value == 2 && a->take == 70 && !M.kit.fact);
    CHECK(count(RFM_MOVE_ACT_KIT_SET) == 16 * RFM_MOVE_KIT_P_N && !M.kit.pad[0].path[0] &&
          M.kit.pad[0].v[RFM_MOVE_KIT_P_PITCH] == 0 && M.kit.pad[15].st == RFM_MOVE_KIT_LOADING);
    for (k = 0; k < 16; k++) {                          /* (the host: three sounds, the rest emptied) */
        char path[64];
        snprintf(path, sizeof path, "/s/808/%c.wav", 'a' + k);
        if (k < 3)
            rfm_move_kit_loaded(&M, k, RFM_MOVE_KIT_OK, path + 7, path, 4800, 48000, 0, NULL);
        else
            rfm_move_kit_loaded(&M, k, RFM_MOVE_KIT_EMPTY, NULL, NULL, 0, 0, 0, NULL);
    }
    CHECK(!strcmp(M.kit.pad[2].path, "/s/808/c.wav") && M.kit.pad[2].st == RFM_MOVE_KIT_OK &&
          M.kit.pad[3].st == RFM_MOVE_KIT_EMPTY && !M.kit.pad[3].path[0]);
    L(-1);
    CHECK(lit(L(L_WHITE0 + W_UNDO)));
    clear(), press(MR_CC_UNDO);
    CHECK(strstr(M.kit.pad[0].path, "Old.wav") && M.kit.pad[0].v[RFM_MOVE_KIT_P_PITCH] == 500 && !M.kit.pad[2].path[0] &&
          count(RFM_MOVE_ACT_KIT_LOAD) == 16 && !strcmp(M.kit.msg, "Undo"));
    /* Open (the actions' first icon): the folder entered */
    rest();
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_SAMPLES && M.kit.fsel[0] == 0);
    button(MR_CC_SHIFT, 1), jog_click(), button(MR_CC_SHIFT, 0);
    jog(-1), clear(), jog_click();
    CHECKF(M.kit.depth == 1 && M.kit.folder[1] == 70 && !count(RFM_MOVE_ACT_KIT_FOLDER), "depth %d", M.kit.depth);
}
/* 21b. (2026-11-26) a dead row (the host's "Reading..." or "120 .ogg (unsupported)"): shown, nothing loaded on the
 * jog's landing nor on the click, Files stays open in its folder */
static void t_note_row(void)
{
    rfm_move_kit_row_t r[2];
    kit_view();
    jog_click(), jog(1), jog_click();                   /* (Files, Samples) */
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_SAMPLES);
    memset(r, 0, sizeof r);
    r[0].kind = RFM_MOVE_KIT_ROW_NOTE, snprintf(r[0].name, sizeof r[0].name, "3 .ogg (unsupported)");
    r[1].id = 72, r[1].kind = RFM_MOVE_KIT_ROW_FILE, snprintf(r[1].name, sizeof r[1].name, "Clap");
    rfm_move_kit_rows(&M, 2, 0, 2, 0, 2, r);
    clear(), rest(), jog_click(), rest();
    CHECK(!count(RFM_MOVE_ACT_KIT_LOAD) && M.kit.tab == 2 && M.kit.place == RFM_MOVE_KIT_PL_SAMPLES && M.kit.depth == 0);
    jog(1), rest();                                     /* (the next row, a sound: loaded as before) */
    CHECK(count(RFM_MOVE_ACT_KIT_LOAD) == 1);
    back(), back();
}
/* 22. kits: the Kit tab's Save ("Saved / Kit 3", board R), Load (Files' Kits), a kit applied, its Undo; Files' Kits */
static void t_kits(void)
{
    const rfm_move_act_t *a;
    rfm_move_kit_row_t r[2];
    const char *paths[RFM_MOVE_KIT_PADS] = { 0 };
    static int16_t v[RFM_MOVE_KIT_PADS][RFM_MOVE_KIT_P_N];
    const uint8_t mix[RFM_MOVE_KIT_MIX_N] = { 90, 40, 10, 20, 30 };
    int k, out;
    kit_view();
    hit(kp(4)), sound(4, "Snare", 4800, NULL, 0), rest();
    jog(-1), jog_click(), jog(1);                       /* (pages: Sample Kit's click: Kits, Save) */
    CHECK(M.kit.tab == 1 && M.kit.kitrow == 1);
    clear(), jog_click();
    CHECK(count(RFM_MOVE_ACT_KIT_SAVE) == 1 && !strcmp(M.kit.msg, "Saving"));
    jog_click();
    CHECK(count(RFM_MOVE_ACT_KIT_SAVE) == 1);           /* (one at a time) */
    CHECK(rfm_move_kit_saved(&M, 1, "Kit 3") == 1 && !strcmp(rfm_move_kit_name(&M), "Kit 3") && M.seq.dirty);
    out = vs_ref("saved", NULL, 0);
    CHECKF(out == 0, "saved: %d differ", out);
    dump("saved");
    rfm_move_kit_saved(&M, 0, NULL);
    CHECK(!strcmp(M.kit.msg, "Not Saved") && !strcmp(M.kit.name, "Kit 3"));
    CHECK(!strcmp(rfm_move_kit_path(&M, 4), M.kit.pad[4].path) && rfm_move_kit_layout(&M) == 0 &&
          rfm_move_kit_mix(&M, RFM_MOVE_KIT_MIX_LEVEL) == 100 && !rfm_move_kit_path(&M, 16));
    /* Load: Files' Kits, a kit a row (I_KIT), its click KIT_OPEN */
    rest(), jog(-1), clear(), jog_click();
    CHECK(M.kit.tab == 2 && M.kit.place == RFM_MOVE_KIT_PL_KITS && (a = last(RFM_MOVE_ACT_KIT_ROWS)) && a->value == 3);
    memset(r, 0, sizeof r);
    r[0].id = 300, r[0].kind = RFM_MOVE_KIT_ROW_KIT, snprintf(r[0].name, sizeof r[0].name, "Kit 1");
    r[1].id = 301, r[1].kind = RFM_MOVE_KIT_ROW_KIT, snprintf(r[1].name, sizeof r[1].name, "Kit 3");
    rfm_move_kit_rows(&M, 3, 0, 2, 0, 2, r);
    jog(1), clear(), jog_click();
    CHECK((a = last(RFM_MOVE_ACT_KIT_OPEN)) && a->take == 301 && !count(RFM_MOVE_ACT_KIT_PREVIEW));
    dump("kits");
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++) {
        v[k][RFM_MOVE_KIT_P_END] = RFM_MOVE_KIT_FRAC;
        v[k][RFM_MOVE_KIT_P_PITCH] = (int16_t)(k * 100);
    }
    v[2][RFM_MOVE_KIT_P_STRETCH] = 8, v[2][RFM_MOVE_KIT_P_SEND2] = 50, v[3][RFM_MOVE_KIT_P_LEVEL] = 99;   /* (99: clamped) */
    paths[0] = "/k/Kit 1/Kick.wav", paths[2] = "/k/Kit 1/Loop.wav";
    clear();
    CHECK(rfm_move_kit_apply(&M, "Kit 1", RFM_MOVE_KIT_RH_INKEY, mix, paths, (const int16_t (*)[RFM_MOVE_KIT_P_N])v) >= 0);
    CHECK(!strcmp(M.kit.name, "Kit 1") && M.kit.rh == RFM_MOVE_KIT_RH_INKEY && M.kit.mix[0] == 90 &&
          !strcmp(M.kit.pad[2].path, "/k/Kit 1/Loop.wav") && M.kit.pad[2].v[RFM_MOVE_KIT_P_STRETCH] == 8 &&
          M.kit.pad[3].v[RFM_MOVE_KIT_P_LEVEL] == 6 && !M.kit.pad[4].path[0] && !strcmp(M.kit.msg, "Loaded"));
    CHECK(count(RFM_MOVE_ACT_KIT_LOAD) == 16 && count(RFM_MOVE_ACT_KIT_SET) == 16 * RFM_MOVE_KIT_P_N + RFM_MOVE_KIT_BUS_N &&
          count(RFM_MOVE_ACT_MIX) == 1);
    rfm_move_kit_loaded(&M, 2, RFM_MOVE_KIT_OK, "Loop", "/k/Kit 1/Loop.wav", 96000, 48000, 0, NULL);
    CHECK(M.kit.pad[2].st == RFM_MOVE_KIT_OK);
    clear(), press(MR_CC_UNDO);                         /* (the kit before it) */
    CHECK(!strcmp(M.kit.name, "Kit 3") && M.kit.rh == 0 && M.kit.mix[0] == 100 && strstr(M.kit.pad[4].path, "Snare") &&
          !M.kit.pad[2].path[0]);
    CHECK(rfm_move_kit_apply(&M, "x", 0, NULL, NULL, NULL) >= 0 && !strcmp(M.kit.msg, "Not Loaded") && !strcmp(M.kit.name, "Kit 3"));
    /* the places: Kits the third */
    rest(), back();                                     /* (2026-11-26: the kits not in the root: Back to Load) */
    CHECK(M.kit.place == 0 && M.kit.tab == 1 && M.kit.kitrow == 0);
    clear(), jog_click();
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_KITS && M.kit.tab == 2 && last(RFM_MOVE_ACT_KIT_ROWS));
}
/* 23. the sampler's To Kit: the next empty pad (its To "Kit Pad 8", the pad lit white, board U), claimed, filled */
static void t_smp_kit(void)
{
    static const signed char HAS[16] = { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 0 };
    char nm[16];
    int k, out;
    kit_view();
    for (k = 0; k < 16; k++)
        if (HAS[k])
            snprintf(nm, sizeof nm, "S%d", k), sound(k, nm, 4800, NULL, 0);
    press(MR_CC_SAMPLE), rest();
    CHECK(M.smp.open && rfmv_smp_shown(&M));
    rfm_move_smp_offer(&M, 1u << RFM_MOVE_SMP_SRC_MASTER | 1u << RFM_MOVE_SMP_SRC_KIT,
                       1u << RFM_MOVE_SMP_SOURCE | 1u << RFM_MOVE_SMP_MODE | 1u << RFM_MOVE_SMP_TO, 0x7,
                       1u << RFM_MOVE_SMP_TO_FILES | 1u << RFM_MOVE_SMP_TO_KIT);
    CHECK(rfm_move_smp_set(&M, RFM_MOVE_SMP_SOURCE, RFM_MOVE_SMP_SRC_KIT) >= 0 &&
          rfm_move_smp_set(&M, RFM_MOVE_SMP_MODE, RFM_MOVE_SMP_M_BARS) >= 0 &&
          rfm_move_smp_set(&M, RFM_MOVE_SMP_TO, RFM_MOVE_SMP_TO_KIT) >= 0);
    M.smp.row = RFM_MOVE_SMP_MODE, rest();
    jog(1);                                             /* (three rows: Source, Mode, To chosen) */
    {
        static const box_t METER[2] = { { 40, 7, 60, 1 }, { 50, 0, 50, 14 } };   /* (the sampler's own level line in
                                                         * the top row: not the mock's) */
        out = vs_ref("sampler", METER, 2);
    }
    CHECKF(out == 0, "sampler: %d differ", out);
    dump("sampler");
    L(-1);
    CHECK(is_rgb(L(L_PAD0 + kp(7)), 159, 159, 159) && !is_rgb(L(L_PAD0 + kp(11)), 159, 159, 159));
    CHECK(rfm_move_kit_next_empty(&M, 0) == 7 && rfm_move_kit_next_empty(&M, 1) == 7 && rfm_move_kit_next_empty(&M, 1) == 11);
    M.kit.pad[7].v[RFM_MOVE_KIT_P_START] = 300;
    rfm_move_kit_loaded(&M, 7, RFM_MOVE_KIT_OK, "K 1 Bar", "/takes/k1.wav", 96000, 48000, 0, NULL);
    CHECK(!strcmp(M.kit.pad[7].path, "/takes/k1.wav") && M.kit.pad[7].v[RFM_MOVE_KIT_P_START] == 0 && M.kit.pad[7].st == RFM_MOVE_KIT_OK);
    rfm_move_kit_loaded(&M, 11, RFM_MOVE_KIT_OK, "K 1 Bar", "/takes/k2.wav", 96000, 48000, 0, NULL);
    for (k = 0; k < 16; k++)
        if (!HAS[k] && k != 7 && k != 11)
            rfm_move_kit_next_empty(&M, 1);
    {
        char t[24];
        rfmv_kit_to_text(&M, t, sizeof t);
        CHECKF(!strcmp(t, "Kit Full") && rfm_move_kit_next_empty(&M, 0) == -1, "%s", t);
    }
}
/* 24. the project: version 12 saved and read back (stretch, sends, the name); a version 11 one still loads */
static void t_project12(void)
{
    static uint8_t b[RFM_MOVE_PROJECT_MAX], c[RFM_MOVE_PROJECT_MAX];
    int n, n11, i, k;
    uint32_t h;
    kit_view();
    sound(3, "Rim", 4800, NULL, 0);
    M.kit.pad[3].v[RFM_MOVE_KIT_P_STRETCH] = 12, M.kit.pad[3].v[RFM_MOVE_KIT_P_SEND3] = 77, M.kit.pad[3].v[RFM_MOVE_KIT_P_CHOKE] = 3;
    snprintf(M.kit.name, sizeof M.kit.name, "Kit 9"), M.kit.mix[4] = 5, M.kit.route = 0xD000;
    n = rfm_move_project_save(&M, b, sizeof b);
    CHECK(b[4] == RFM_MOVE_PROJECT_VERSION && rfm_move_project_check(b, (uint32_t)n) == 0);
    rfm_move_init(&M2, NULL, on_act, NULL);
    rfm_move_fw_caps(&M2, KCAPS);
    CHECK(rfm_move_project_load(&M2, 1, b, (uint32_t)n) == 0 && M2.kit.pad[3].v[RFM_MOVE_KIT_P_STRETCH] == 12 &&
          M2.kit.pad[3].v[RFM_MOVE_KIT_P_SEND3] == 77 && !strcmp(M2.kit.name, "Kit 9") && M2.kit.mix[4] == 5);
    /* a version 11 blob (as stage 3a wrote it): 8 bytes, 12 settings a pad */
    n = (int)proj_v15(b, (uint32_t)n);                  /* (2026-11-26: 7 bytes a note before 16) */
    memcpy(c, b, (size_t)n - 4 - RFM_MOVE_KIT_BYTES_V15);
    {
        uint8_t *o = c + n - 4 - RFM_MOVE_KIT_BYTES_V15;
        const uint8_t *x = b + n - 4 - RFM_MOVE_KIT_BYTES_V15;
        memset(o, 0, RFM_MOVE_KIT_BYTES_V11);
        memcpy(o, x, 4);
        o += 8, x += 40;
        for (k = 0; k < RFM_MOVE_KIT_PADS; k++, o += 160 + 24, x += 160 + 2 * RFM_MOVE_KIT_P_N_V15)
            memcpy(o, x, 160 + 24);
    }
    n11 = n - RFM_MOVE_KIT_BYTES_V15 + RFM_MOVE_KIT_BYTES_V11;
    c[4] = 11;
    for (h = 2166136261u, i = 0; i < n11 - 4; i++)
        h = (h ^ c[i]) * 16777619u;
    for (i = 0; i < 4; i++)
        c[n11 - 4 + i] = (uint8_t)(h >> (8 * i));
    CHECK(rfm_move_project_check(c, (uint32_t)n11) == 0);
    CHECK(rfm_move_project_load(&M2, 2, c, (uint32_t)n11) == 0 && M2.kit.pad[3].v[RFM_MOVE_KIT_P_CHOKE] == 3 &&
          !M2.kit.pad[3].v[RFM_MOVE_KIT_P_STRETCH] && !M2.kit.pad[3].v[RFM_MOVE_KIT_P_SEND3] && !M2.kit.name[0] &&
          M2.kit.mix[0] == 100 && M2.kit.mix[1] == 64 && M2.kit.route == 0xD000 && strstr(M2.kit.pad[3].path, "Rim"));
}


/* ================================================================ (stage 3c) === */
static int said3c(const char *w)                        /* the kit's message now (its two rows) */
{
    return strstr(M.kit.msg, w) || strstr(M.kit.msg2, w) || (strstr(w, "+") && strstr(M.kit.msg2, w));
}
static int slice_n(void)                                /* the slices that fit from the first pad */
{
    const int room = RFM_MOVE_KIT_PADS - M.kit.sl_first;
    return M.kit.sl_n < room ? M.kit.sl_n : room;
}
/* a sound with a path of its own on pad k (the host's answer to its load) */
static void sound_at(int k, const char *path, uint32_t frames, uint32_t rate)
{
    const char *b = strrchr(path, '/');
    char nm[32];
    snprintf(nm, sizeof nm, "%s", b ? b + 1 : path);
    if (strrchr(nm, '.'))
        *strrchr(nm, '.') = 0;
    rfm_move_kit_loaded(&M, k, RFM_MOVE_KIT_OK, nm, path, frames, rate, 0, NULL);
}
/* 25. (the owner's follow-up) a pad's own edits are gone from the kit: Shift + the click opens nothing, the page's
 * edit (rfm_move_kit_edit) sends no KIT_EDIT */
static void t_tools3c(void)
{
    kit_view();
    sound_at(0, "/s/Kick.wav", 48000, 48000);
    clear(), tools();
    CHECK(!M.kit.tools && !rfm_move_kit_edit(&M, 0, RFM_MOVE_KIT_ED_FREEZE) && !count(RFM_MOVE_ACT_KIT_EDIT));
}
/* 26. mix and join (board N): Copy held, a pad, then another: Copy / Mix / Join, the jog chooses, the click does it
 * (Copy: the first's sound and settings onto the second; Mix, Join: KIT_MERGE, the host's new file on the second pad,
 * a join's seam); Undo brings the pad back; Copy let go before a choice, or Back: nothing done */
static void t_merge(void)
{
    const rfm_move_act_t *a;
    uint8_t w[RFM_MOVE_NLEDS][3];
    kit_view();
    sound_at(1, "/s/Hat.wav", 24000, 48000);
    sound_at(4, "/s/Snare.wav", 48000, 48000);
    M.kit.pad[1].v[RFM_MOVE_KIT_P_PITCH] = 500, M.kit.pad[1].v[RFM_MOVE_KIT_P_END] = RFM_MOVE_KIT_FRAC / 2;
    /* Copy let go before a choice: nothing */
    clear(), button(MR_CC_COPY, 1), hit(kp(1)), hit(kp(4));
    CHECK(M.kit.mg_a == 2 && M.kit.mg_b == 5 && !count(RFM_MOVE_ACT_KIT_NOTE) && rfmv_kit_back_has(&M));
    leds(w);
    CHECK(is_rgb(w[L_PAD0 + kp(4)], 255, 255, 255) && !lit(w[L_RING0]) && !lit(w[L_PAD0 + rp(0)]));
    button(MR_CC_COPY, 0), adv_ms(20);
    CHECK(!M.kit.mg_a && !count(RFM_MOVE_ACT_KIT_MERGE) && !count(RFM_MOVE_ACT_KIT_LOAD) &&
          !strcmp(M.kit.pad[4].path, "/s/Snare.wav"));
    /* Back: closed, nothing */
    button(MR_CC_COPY, 1), hit(kp(1)), hit(kp(4)), back();
    CHECK(!M.kit.mg_a && !count(RFM_MOVE_ACT_KIT_MERGE));
    button(MR_CC_COPY, 0);
    /* Copy: pad 2's sound and settings onto pad 5 */
    rest(), clear(), button(MR_CC_COPY, 1), hit(kp(1)), hit(kp(4)), jog_click(), button(MR_CC_COPY, 0);
    CHECK(!strcmp(M.kit.pad[4].path, "/s/Hat.wav") && M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 500 &&
          M.kit.pad[4].v[RFM_MOVE_KIT_P_END] == RFM_MOVE_KIT_FRAC / 2 && last_i(RFM_MOVE_ACT_KIT_LOAD) >= 0 &&
          last(RFM_MOVE_ACT_KIT_LOAD)->index == 4 && !strcmp(D[last_i(RFM_MOVE_ACT_KIT_LOAD)], "/s/Hat.wav") &&
          !count(RFM_MOVE_ACT_KIT_MERGE) && M.kit.cur == 4);
    sound_at(4, "/s/Hat.wav", 24000, 48000);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_END] == RFM_MOVE_KIT_FRAC / 2);   /* (its region kept) */
    clear(), press(MR_CC_UNDO);
    CHECK(!strcmp(M.kit.pad[4].path, "/s/Snare.wav") && M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 0);
    sound_at(4, "/s/Snare.wav", 48000, 48000);
    /* Mix: KIT_MERGE (pad 5 the result's, pad 2 the first) */
    rest(), clear(), button(MR_CC_COPY, 1), hit(kp(1)), hit(kp(4)), jog(1), jog_click(), button(MR_CC_COPY, 0);
    CHECK((a = last(RFM_MOVE_ACT_KIT_MERGE)) && a->index == 4 && a->take == 1 && a->value == RFM_MOVE_KIT_MG_MIX &&
          M.kit.pad[4].st == RFM_MOVE_KIT_LOADING && !strcmp(M.kit.msg, "Mix") && said3c("2+5"));
    sound_at(4, "/s/Kits/Edits/Mix 2+5.wav", 48000, 48000);
    CHECK(!strcmp(M.kit.pad[4].name, "Mix 2+5") && !M.kit.pad[4].cut);
    {   /* (a path shorter than the one before it: the project's kit still valid, NUL padded) */
        static uint8_t kb[RFM_MOVE_KIT_BYTES];
        memset(M.kit.pad[6].path, 'x', sizeof M.kit.pad[6].path - 1);
        sound_at(6, "/s/a.wav", 4800, 48000);
        rfmv_kit_write(&M, kb), kit_v15(kb);            /* (as 15 had it) */
        CHECK(rfmv_kit_check(kb, 15) == 0 && !strcmp(M.kit.pad[6].path, "/s/a.wav"));
        M.kit.pad[6].path[0] = 0;
        memset(M.kit.pad[6].path + 1, 'y', 20);
        rfmv_kit_write(&M, kb), kit_v15(kb);
        CHECK(rfmv_kit_check(kb, 15) == 0);
        rfm_move_kit_loaded(&M, 6, RFM_MOVE_KIT_EMPTY, NULL, NULL, 0, 0, 0, NULL);
        memset(M.kit.pad[6].path, 0, sizeof M.kit.pad[6].path);
    }
    press(MR_CC_UNDO);
    CHECK(!strcmp(M.kit.pad[4].path, "/s/Snare.wav"));
    sound_at(4, "/s/Snare.wav", 48000, 48000);
    /* Join: the seam where the first's region ends (12000 of 60000 frames) */
    rest(), clear(), button(MR_CC_COPY, 1), hit(kp(1)), hit(kp(4)), jog(2), jog_click(), button(MR_CC_COPY, 0);
    CHECK((a = last(RFM_MOVE_ACT_KIT_MERGE)) && a->value == RFM_MOVE_KIT_MG_JOIN);
    sound_at(4, "/s/Kits/Edits/Join 2+5.wav", 60000, 48000);
    CHECK(M.kit.pad[4].cut > 6550 && M.kit.pad[4].cut < 6560 && M.kit.pad[4].v[RFM_MOVE_KIT_P_END] == RFM_MOVE_KIT_FRAC);
    /* Mix onto an empty pad: nothing asked ("Empty") */
    rest(), clear(), button(MR_CC_COPY, 1), hit(kp(1)), hit(kp(9)), jog(1), jog_click(), button(MR_CC_COPY, 0);
    CHECK(!count(RFM_MOVE_ACT_KIT_MERGE) && said3c("Empty") && !M.kit.pad[9].path[0]);
}
/* 27. a take sliced (board P): To Kit, then Slice: KIT_SLICE asks its cuts (Hits), the pads play the slices and
 * choose one, the jog the method (asked again), knobs 1 and 2 its cut, Shift + a pad the first pad; the click puts
 * them onto the pads (one file, a region each); Undo brings the kit back; Back leaves (DONE) */
static void t_slice(void)
{
    static const uint32_t CUT[5] = { 0, 12000, 24000, 30000, 48000 };
    const rfm_move_act_t *a;
    uint8_t w[RFM_MOVE_NLEDS][3];
    int k, n;
    kit_view();
    M.seq.bpm10 = 1200;
    sound_at(5, "/s/Old.wav", 4800, 48000);
    rfmv_kit_to(&M, 42), rest();
    CHECK(M.kit.pend_take == 42 && M.kit.pend_i == 0);
    jog(1), clear(), jog_click();
    CHECK(M.kit.sl_on && !M.kit.pend_take && (a = last(RFM_MOVE_ACT_KIT_SLICE)) && a->param == RFM_MOVE_KIT_SL_ASK &&
          a->take == 42 && a->value == RFM_MOVE_KIT_SM_HITS);
    CHECK(rfm_move_kit_slices(&M, 41, "/t/x.wav", 48000, 48000, 4, CUT, 0, NULL) == 0);   /* (another take's) */
    rfm_move_kit_slices(&M, 42, "/t/1125-1200 Master 1b.wav", 48000, 48000, 4, CUT, 0, NULL);
    CHECK(M.kit.sl_ok == 1 && M.kit.sl_n == 4);
    leds(w);
    CHECK(lit(w[L_PAD0 + kp(3)]) && !lit(w[L_PAD0 + kp(4)]) && is_rgb(w[L_PAD0 + kp(0)], 255, 255, 255) &&
          lit(w[L_RING0]) && lit(w[L_RING0 + 1]) && lit(w[L_RING0 + 2]) && !lit(w[L_RING0 + 3]));   /* (round A: knob 3 the
                                                         * Hits' sensitivity) */
    clear(), hit(kp(2));                                /* (slice 3 heard and chosen) */
    CHECK(M.kit.sl_cur == 2 && (a = last(RFM_MOVE_ACT_KIT_SLICE)) && a->param == RFM_MOVE_KIT_SL_PLAY && a->from == 24000 &&
          a->count == 6000 && !count(RFM_MOVE_ACT_KIT_NOTE));
    knob(0, 2);                                         /* (its cut: two columns, 375 frames each) */
    CHECK(M.kit.sl_cut[2] == 24750);
    knob(1, -10);                                       /* (fine: 10 ms) */
    CHECK(M.kit.sl_cut[2] == 24270);
    knob(0, -60), knob(0, -60);                         /* (never past the cut before: 10 ms short of it) */
    CHECK(M.kit.sl_cut[2] == 12480);
    clear(), jog(1);                                    /* (Equal 16: asked again) */
    CHECK(M.kit.sl_meth == RFM_MOVE_KIT_SM_EQ16 && (a = last(RFM_MOVE_ACT_KIT_SLICE)) && a->param == RFM_MOVE_KIT_SL_ASK &&
          a->value == RFM_MOVE_KIT_SM_EQ16);
    clear(), jog(3);                                    /* (Steps: a step's microseconds at the tempo; round B: Tap after) */
    CHECKF((a = last(RFM_MOVE_ACT_KIT_SLICE)) && a->value == RFM_MOVE_KIT_SM_STEPS && a->from == 125000, "steps: %u",
           a ? (unsigned)a->from : 0u);
    rfm_move_kit_slices(&M, 42, "/t/1125-1200 Master 1b.wav", 48000, 48000, 4, CUT, 0, NULL);
    button(MR_CC_SHIFT, 1), hit(kp(13)), button(MR_CC_SHIFT, 0);   /* (the first pad: 14; three fit) */
    CHECK(M.kit.sl_first == 13 && slice_n() == 3);
    clear(), jog_click();
    CHECK(!M.kit.sl_on && count(RFM_MOVE_ACT_KIT_LOAD) == 3 && said3c("Pads 14-16") && M.kit.cur == 13);
    for (k = 13, n = 0; k < 16; k++, n++) {
        CHECK(!strcmp(M.kit.pad[k].path, "/t/1125-1200 Master 1b.wav"));
        CHECK(M.kit.pad[k].v[RFM_MOVE_KIT_P_START] == (int)((uint64_t)CUT[n] * RFM_MOVE_KIT_FRAC / 48000) &&
              M.kit.pad[k].v[RFM_MOVE_KIT_P_END] == (int)((uint64_t)CUT[n + 1] * RFM_MOVE_KIT_FRAC / 48000));
        sound_at(k, "/t/1125-1200 Master 1b.wav", 48000, 48000);
    }
    CHECK(M.kit.pad[14].v[RFM_MOVE_KIT_P_START] == 8191);   /* (the answer keeps the region) */
    press(MR_CC_UNDO);                                  /* (the kit back) */
    CHECK(!M.kit.pad[13].path[0] && !strcmp(M.kit.pad[5].path, "/s/Old.wav"));
    /* Back leaves (DONE); To Pad: onto the chosen pad */
    rfmv_kit_to(&M, 43), jog(1), jog_click(), clear(), back();
    CHECK(!M.kit.sl_on && (a = last(RFM_MOVE_ACT_KIT_SLICE)) && a->param == RFM_MOVE_KIT_SL_DONE);
    rfmv_kit_to(&M, 44), clear(), jog_click();
    CHECK((a = last(RFM_MOVE_ACT_KIT_LOAD)) && a->take == 44 && a->index == M.kit.cur && !M.kit.pend_take);
    /* a failed answer: "Not Sliced" */
    rfmv_kit_to(&M, 45), jog(1), jog_click();
    rfm_move_kit_slices(&M, 45, "/t/y.wav", 0, 48000, 0, CUT, 0, NULL);
    CHECK(M.kit.sl_ok == 2 && said3c("Not Sliced"));
    back();
}
/* 28. the page's Kit tab: a pad played, chosen, a setting set (clamped as the knobs) */
static void t_page3c(void)
{
    const rfm_move_act_t *a;
    kit_view();
    sound_at(2, "/s/Clap.wav", 4800, 48000);
    clear();
    CHECK(rfm_move_kit_play(&M, 2, 90) && (a = last(RFM_MOVE_ACT_KIT_NOTE)) && a->index == 2 && a->vel == 90 && a->down);
    CHECK(!rfm_move_kit_play(&M, 3, 90) && rfm_move_kit_play(&M, 2, 0) && !last(RFM_MOVE_ACT_KIT_NOTE)->down);
    CHECK(rfm_move_kit_choose(&M, 6) && M.kit.cur == 6 && M.seq.sel_track == 13);
    clear();
    CHECK(rfm_move_kit_set(&M, 2, RFM_MOVE_KIT_P_PITCH, 9999) && M.kit.pad[2].v[RFM_MOVE_KIT_P_PITCH] == 2400 &&
          (a = last(RFM_MOVE_ACT_KIT_SET)) && a->index == 2 && a->param == RFM_MOVE_KIT_P_PITCH && a->value == 2400);
    CHECK(rfm_move_kit_set(&M, 2, RFM_MOVE_KIT_P_ATTACK, 18) && M.kit.pad[2].v[RFM_MOVE_KIT_P_ATTACK] == 20);
    CHECK(rfm_move_kit_set(&M, 2, RFM_MOVE_KIT_P_START, 40000) &&
          M.kit.pad[2].v[RFM_MOVE_KIT_P_START] == RFM_MOVE_KIT_FRAC - 16);
    CHECK(!rfm_move_kit_set(&M, 2, RFM_MOVE_KIT_P_N, 1) && !rfm_move_kit_set(&M, 16, 0, 1));
}
/* 29. the boards N (merge), O (joined), P (slice) and V (padact, whole) */
static void t_mock3c(void)
{
    static const int CUT[17] = { 0, 9, 16, 24, 31, 40, 48, 55, 63, 72, 80, 87, 95, 104, 111, 119, 127 };
    static const box_t COL62 = { 62, 28, 1, 1 };
    uint8_t pk[64], p2[64];
    uint32_t cuts[17];
    int out, n, i;
    kit_view();
    M.seq.bpm10 = 1200;
    /* N: Copy held, pad 2 then pad 5, Join chosen */
    rfm_move_kit_loaded(&M, 1, RFM_MOVE_KIT_OK, "Hat", "/s/Hat.wav", 127, 48000, 0, NULL);
    rfm_move_kit_loaded(&M, 4, RFM_MOVE_KIT_OK, "Snare", "/s/Snare.wav", 129, 48000, 0, NULL);
    button(MR_CC_COPY, 1), hit(kp(1)), hit(kp(4)), jog(2);
    out = vs_ref("merge", NULL, 0);
    CHECKF(out == 0, "merge: %d differ", out);
    dump("merge");
    /* O: the join on pad 5, its seam (pages: the SOUND page drawn, the Pad tab's board no longer applies) */
    jog_click(), button(MR_CC_COPY, 0);
    n = mock_peaks(62, 11, 12u, 1, pk);
    pk[n++] = 0;
    n += mock_peaks(64, 11, 55u, 1, pk + n);
    rfm_move_kit_loaded(&M, 4, RFM_MOVE_KIT_OK, "Join 2+5", "/s/Kits/Edits/Join 2+5.wav", 48000, 48000, n, pk);
    rest();
    (void)COL62;
    dump("joined");
    /* V: the Sound row's tools: gone (the owner's follow-up) */
    (void)p2;
    /* P: 16 slices by the hits, slice 6 chosen, onto pads 1-16 */
    button(MR_CC_SHIFT, 1), hit(kp(0)), button(MR_CC_SHIFT, 0);
    rfmv_kit_to(&M, 77), jog(1), jog_click();
    for (i = 0; i < 17; i++)
        cuts[i] = (uint32_t)CUT[i] * 100u;
    cuts[16] = 12800;
    n = mock_peaks(128, 12, 77u, 0, pk);
    rfm_move_kit_slices(&M, 77, "/t/take.wav", 12800, 48000, 16, cuts, n, pk);
    hit(kp(5)), rest();
    out = vs_ref("slice", NULL, 0);
    CHECKF(out == 0, "slice: %d differ", out);
    dump("slice");
    back();
    printf("mock boards (3c): merge, padact, slice\n");
}

/* ================================================================ (round A, 2026-11-25) === */
/* 30. the close-up of Start (pages v2: knob 1) and End (knob 2) (its row's popup): KIT_PEAKS of the window round the point (8 times nearer,
 * Shift: 32), the point a line with a notch, what does not play dithered; heard at rest (KIT_AUDITION, 150 ms) */
static void t_zoom(void)
{
    const rfm_move_act_t *a;
    uint8_t pk[64];
    int i;
    kit_view();
    memset(pk, 255, sizeof pk);
    button(MR_CC_SHIFT, 1), hit(kp(1)), button(MR_CC_SHIFT, 0);
    sound(1, "Loop", 96000, NULL, 0);
    rest(), clear();
    touch(0, 1), adv_ms(10);                            /* (Start at 0: the window [-6000, 6000), its part in the sound) */
    CHECK(count(RFM_MOVE_ACT_KIT_PEAKS) == 1 && (a = last(RFM_MOVE_ACT_KIT_PEAKS)) && a->index == 1 && a->from == 0 &&
          a->count == 6000 && a->value == RFM_MOVE_KIT_PEAKS);
    adv_ms(50);
    CHECK(count(RFM_MOVE_ACT_KIT_PEAKS) == 1);          /* (asked once while it stays) */
    clear(), button(MR_CC_SHIFT, 1), adv_ms(10);        /* (Shift: 32 times nearer) */
    CHECK((a = last(RFM_MOVE_ACT_KIT_PEAKS)) && a->from == 0 && a->count == 1500);
    CHECK(rfm_move_kit_peaks(&M, 1, 0, 1500, 64, pk) == 1);
    screen();
    dump("zoom_start");
    CHECK(px(64, 24) && px(64, 63) && px(61, 24) && px(67, 24) && px(63, 26) && !px(61, 26));   /* (the point, its notch) */
    CHECK(px(66, 44) && px(66, 45) && px(66, 29) && !px(20, 44) && !px(20, 45));   /* (played; before the sound: nothing) */
    {
        int top = 0;
        for (i = 0; i < 128; i++)
            top |= px(i, 4);
        CHECK(top);                                     /* (the value, large) */
    }
    clear(), knob(0, 40);                               /* (Shift: fine, 4 a detent) */
    CHECK(M.kit.pad[1].v[RFM_MOVE_KIT_P_START] == 160 && !count(RFM_MOVE_ACT_KIT_AUDITION));
    adv_ms(100), knob(0, 1), adv_ms(100);               /* (a turn more: the pending one later) */
    CHECK(!count(RFM_MOVE_ACT_KIT_AUDITION));
    adv_ms(400);
    CHECK(!count(RFM_MOVE_ACT_KIT_AUDITION));           /* (2026-11-26: never heard at rest; the pad plays it) */
    button(MR_CC_SHIFT, 0), touch(0, 0), rest(), clear();
    /* End (pages v2: Pad Sample's knob 2): the close-up at the sound's end; the part after the point dithered */
    touch(1, 1);
    CHECK(M.kit.kpop == 1);
    adv_ms(10);
    CHECK((a = last(RFM_MOVE_ACT_KIT_PEAKS)) && a->from == 90000 && a->count == 6000);
    rfm_move_kit_peaks(&M, 1, 90000, 6000, 64, pk);
    knob(1, -20), adv_ms(10);                           /* (End 20 detents in: 1280 of 32767) */
    CHECK(M.kit.pad[1].v[RFM_MOVE_KIT_P_END] == RFM_MOVE_KIT_FRAC - 1280);
    rfm_move_kit_peaks(&M, 1, last(RFM_MOVE_ACT_KIT_PEAKS)->from, last(RFM_MOVE_ACT_KIT_PEAKS)->count, 64, pk);
    screen();
    dump("zoom_end");
    CHECK(px(40, 44) && px(40, 45) && px(80, 44) && !px(80, 45) && px(64, 40));   /* (played left, dithered right) */
    adv_ms(400);
    CHECK(!count(RFM_MOVE_ACT_KIT_AUDITION));           /* (not heard at rest) */
    touch(1, 0);
    printf("  ok    the close-up: KIT_PEAKS round Start (x8, Shift x32) and End, the point and notch, dithered; not heard at rest\n");
}
/* 31. Slice's Hits: knob 3 the sensitivity (lit), the cuts asked again at rest; dead with Equal */
static void t_sens(void)
{
    const rfm_move_act_t *a;
    kit_view();
    rfmv_kit_to(&M, 42), rest();
    clear(), jog(1), jog_click();
    CHECK(M.kit.sl_on && (a = last(RFM_MOVE_ACT_KIT_SLICE)) && a->param == RFM_MOVE_KIT_SL_ASK && a->count == 50);
    clear(), touch(2, 1), knob(2, 5);
    CHECK(M.kit.sl_sens == 55 && !count(RFM_MOVE_ACT_KIT_SLICE));
    screen();
    dump("sens");
    adv_ms(RFM_MOVE_KIT_SENS_MS + 10);
    CHECK(count(RFM_MOVE_ACT_KIT_SLICE) == 1 && (a = last(RFM_MOVE_ACT_KIT_SLICE)) && a->param == RFM_MOVE_KIT_SL_ASK &&
          a->value == RFM_MOVE_KIT_SM_HITS && a->count == 55 && a->take == 42);
    touch(2, 0), clear(), jog(1);                       /* (Equal 16: knob 3 dead) */
    knob(2, 5), adv_ms(RFM_MOVE_KIT_SENS_MS + 10);
    CHECK(M.kit.sl_sens == 55 && count(RFM_MOVE_ACT_KIT_SLICE) == 1 && last(RFM_MOVE_ACT_KIT_SLICE)->value == RFM_MOVE_KIT_SM_EQ16);
    back();
    printf("  ok    Slice's Hits: knob 3 the sensitivity, the cuts asked again (count 55)\n");
}
/* 32. (pages v2: Toggle is gone) the engines on knob 4: One Shot .. Granular, their names; a held engine's tools' Play let
 * go (round A's Toggle test, adapted) */
static void t_toggle(void)
{
    const rfm_move_act_t *a;
    char v[32];
    kit_view();
    sound(0, "Loop", 48000, NULL, 0);
    rest(), clear();
    knob(3, 15);                                        /* (5 ticks an engine: Pitch Env) */
    CHECK(M.kit.pad[0].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_PITCHENV && (a = last(RFM_MOVE_ACT_KIT_SET)) &&
          a->param == RFM_MOVE_KIT_P_MODE && a->value == 3);
    {
        uint16_t subj;
        uint32_t stamp;
        CHECK(rfmv_kit_pop_words(&M, v, sizeof v, &subj, &stamp) && !strcmp(v, "Engine: Pitch envelope"));
    }
    screen();
    dump("toggle");
    knob(3, 20);
    CHECK(M.kit.pad[0].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_GRANULAR);   /* (the last) */
    rest(), clear(), hit(kp(0)), hit(kp(0));
    CHECK(count(RFM_MOVE_ACT_KIT_NOTE) == 4);
    printf("  ok    the engines: knob 4, their names\n");
}
/* 33. the Kit tab's Trim silence: its row (On), its popup, KIT_REC's param, saved (version 13; 12 read as On) */
static void t_trim(void)
{
    static uint8_t kb[RFM_MOVE_KIT_BYTES], b[RFM_MOVE_PROJECT_MAX];
    const rfm_move_act_t *a;
    int n;
    kit_view();
    CHECK(M.kit.trim == 1 && rfm_move_kit_trim(&M));
    jog(-1), rest(), touch(1, 1);                       /* (pages: Sample Kit's knob 2) */
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_KIT && M.kit.kpop == 1);
    screen();
    dump("trim");
    knob(1, -5);
    CHECK(!M.kit.trim);
    touch(1, 0), jog(1), rest();
    sound(6, "Old", 4800, NULL, 0);
    clear(), pad(kp(6), 100), press(MR_CC_SAMPLE);
    CHECK((a = last(RFM_MOVE_ACT_KIT_REC)) && a->down && a->index == 6 && a->param == 0);
    pad(kp(6), 0);
    M.kit.rec_pad = 0, M.kit.rec_held = 0;
    rfm_move_kit_set_trim(&M, 1);
    clear(), pad(kp(6), 100), press(MR_CC_SAMPLE);
    CHECK((a = last(RFM_MOVE_ACT_KIT_REC)) && a->down && a->param == 1);
    pad(kp(6), 0);
    M.kit.rec_pad = 0, M.kit.rec_held = 0;
    /* saved: version 13 its byte; 12 read as On; a bad byte refused */
    rfm_move_kit_set_trim(&M, 0);
    rfmv_kit_write(&M, kb), kit_v15(kb);
    CHECK(kb[9] == 0 && rfmv_kit_check(kb, 15) == 0);
    rfm_move_kit_set_trim(&M, 1);
    rfmv_kit_write(&M, kb), kit_v15(kb);
    CHECK(kb[9] == 1 && rfmv_kit_check(kb, 15) == 0 && rfmv_kit_check(kb, 12) != 0);
    kb[9] = 2;
    CHECK(rfmv_kit_check(kb, 14) != 0);
    kb[9] = 0;
    rfm_move_init(&M2, NULL, on_act, NULL);
    rfm_move_fw_caps(&M2, KCAPS);
    rfmv_kit_read(&M2, kb, 12);
    CHECK(M2.kit.trim == 1);
    rfm_move_kit_set_trim(&M, 0);
    M.kit.pad[6].v[RFM_MOVE_KIT_P_MODE] = RFM_MOVE_KIT_M_LOFI;
    n = rfm_move_project_save(&M, b, sizeof b);
    CHECK(b[4] == RFM_MOVE_PROJECT_VERSION && rfm_move_project_check(b, (uint32_t)n) == 0);
    CHECK(rfm_move_project_load(&M2, 1, b, (uint32_t)n) == 0 && M2.kit.trim == 0 &&
          M2.kit.pad[6].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_LOFI);
    /* (pages v2) before version 15: Toggle (3) loads as Loop, Stretch N as Warp Repitch, Sync N */
    M.kit.pad[0].v[RFM_MOVE_KIT_P_MODE] = 3, M.kit.pad[0].v[RFM_MOVE_KIT_P_STRETCH] = 4;
    rfmv_kit_write(&M, kb);
    rfmv_kit_read(&M2, kb, 14);
    CHECK(M2.kit.pad[0].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_LOOP && M2.kit.pad[0].v[RFM_MOVE_KIT_P_STRETCH] == 4 &&
          M2.kit.pad[0].v[RFM_MOVE_KIT_P_WARP] == RFM_MOVE_KIT_W_REPITCH && M2.kit.bus[0] == 100);
    printf("  ok    Trim silence: the Kit tab's row, KIT_REC's param, saved (version 13; 12 read as On)\n");
}


/* ================================================================ (2026-11-26, the release's checks) === */
/* rows [y0, y1) of the screen against a canvas, only x >= x0 */
static int band_eq(const mr_canvas_t *cv, int x0, int y0, int y1)
{
    int x, y;
    screen();
    for (y = y0; y < y1; y++)
        for (x = x0; x < 128; x++)
            if (!!cv->px[y][x] != px(x, y))
                return 0;
    return 1;
}
/* 37. the page switcher: the jog's order Sample Kit, Pad Sample, Trig, Pad Sample 2, Warp, Filter, Pad Mix, Kit Mix
 * (said by the page's name); Pad Sample 2 shown "Sample 2" under the row */
static void t_pages_v2(void)
{
    static const char *const NM[RFM_MOVE_KIT_PG_N] = { "Sample Kit", "Pad Sample", "Trig", "Pad Sample 2", "Warp",
                                                        "Filter", "Pad Mix", "Kit Mix" };
    static mr_canvas_t cv;
    int i;
    kit_view();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    jog(-1), rest();
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_KIT);
    for (i = 1; i < RFM_MOVE_KIT_PG_N; i++) {
        char w[32];
        clear(), jog(1);
        snprintf(w, sizeof w, "|%s page", NM[i]);
        CHECKF(M.kit.page == i && (strstr(said, w + 1) == said || strstr(said, w)), "page %d said %s", i, said);
        if (i == RFM_MOVE_KIT_PG_SAMPLE2) {             /* (the name under the switcher: "Sample 2", y 41, 2x) */
            adv_ms(150);                                /* (the slide over) */
            memset(&cv, 0, sizeof cv);
            v2_TC(&cv, 64, 7 + 28 + 6, "Sample 2", 2, 1);
            CHECK(band_eq(&cv, 0, 41, 41 + 16));
            memset(&cv, 0, sizeof cv);
            v2_TC(&cv, 64, 7 + 28 + 6, "Pad Sample 2", 2, 1);
            CHECK(!band_eq(&cv, 0, 41, 41 + 16));
        }
        if (i == RFM_MOVE_KIT_PG_WARP)
            CHECK(M.kit.page == RFM_MOVE_KIT_PG_WARP);
    }
    clear(), jog(1);                                    /* (Kit Mix the last: no wrap) */
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_KITMIX);
    rfm_move_set_speech(&M, 0);
}

/* 38. the browser's root: My Samples alone, with the Move Library after it; the chosen row's long name a ping-pong
 * marquee: 1 s still, 25 px a second, 1 s at its end, back */
static void mq_at(mr_canvas_t *cv, const char *name, int off)
{
    memset(cv, 0, sizeof *cv);
    v2_T(cv, 16 - off, 21 + 3, name, 2, 1);
}
static void t_root(void)
{
    static mr_canvas_t cv;
    uint32_t t0;
    kit_view();
    jog_click();
    CHECK(M.kit.tab == 2 && M.kit.place == 0 && M.kit.psel == 0);
    jog(1);
    CHECK(M.kit.psel == 0);                             /* (My Samples alone) */
    clear(), jog_click();
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_SAMPLES && last(RFM_MOVE_ACT_KIT_ROWS));
    back();
    rfm_move_kit_core_lib(&M, 1);
    CHECK(M.kit.place == 0 && M.kit.psel == 0);
    jog(1);
    CHECK(M.kit.psel == 1);
    clear(), jog_click();
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_CORE && last(RFM_MOVE_ACT_KIT_ROWS) &&
          last(RFM_MOVE_ACT_KIT_ROWS)->value == RFM_MOVE_KIT_PL_CORE);
    back();
    CHECK(M.kit.place == 0 && M.kit.psel == 1);
    /* "Move Library": 12 letters at 2x, 142 px, 30 past its 112: the marquee from the choice's time */
    adv_ms(20);
    t0 = M.kit.mq_ms;
    adv_ms(900 - (rfmv_now_ms(&M) - t0));               /* (900 ms: still) */
    mq_at(&cv, "Move Library", 0);
    CHECK(band_eq(&cv, 16, 24, 40));
    adv_ms(1400 - (rfmv_now_ms(&M) - t0));              /* (1.4 s: 10 px on) */
    CHECK(!band_eq(&cv, 16, 24, 40));
    mq_at(&cv, "Move Library", 10);
    CHECKF(band_eq(&cv, 16, 24, 40), "marquee at %u ms", rfmv_now_ms(&M) - t0);
    adv_ms(1000 + 1200 + 500 - (rfmv_now_ms(&M) - t0));   /* (its end shown, still) */
    mq_at(&cv, "Move Library", 30);
    CHECK(band_eq(&cv, 16, 24, 40));
    adv_ms(1000 + 1200 + 1000 + 400 - (rfmv_now_ms(&M) - t0));   /* (back: 10 px of 30) */
    mq_at(&cv, "Move Library", 20);
    CHECK(band_eq(&cv, 16, 24, 40));
    jog(-1), adv_ms(150);                               /* (My Samples: short, still) */
    mq_at(&cv, "My Samples", 0);
    CHECK(band_eq(&cv, 16, 24, 40));
    rfm_move_kit_core_lib(&M, 0);
    back();
}

/* 39. a step's click gated: held past RFM_MOVE_SEQ_HOLD_MS, or a knob touched meanwhile, toggles nothing (in or out);
 * a plain tap still does */
static void t_gated(void)
{
    const int clip = SEQ_CLIP(12, 0);
    kit_view();
    hit(kp(1));
    step(3, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20), step(3, 0);
    CHECK(rfmv_seq_find(&M, clip, 3, 37) < 0);          /* (a hold: not in) */
    tap_step(3);
    CHECK(rfmv_seq_find(&M, clip, 3, 37) >= 0);         /* (a tap: in) */
    step(3, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20), step(3, 0);
    CHECK(rfmv_seq_find(&M, clip, 3, 37) >= 0);         /* (a hold: not out) */
    step(3, 1), touch(0, 1), touch(0, 0), step(3, 0);
    CHECK(rfmv_seq_find(&M, clip, 3, 37) >= 0);         /* (a knob touched meanwhile: not out) */
    step(5, 1), touch(1, 1), touch(1, 0), step(5, 0);
    CHECK(rfmv_seq_find(&M, clip, 5, 37) < 0);          /* (.. nor in) */
    tap_step(3);
    CHECK(rfmv_seq_find(&M, clip, 3, 37) < 0);          /* (a tap: out) */
}

/* 40. a pitch held (Pitches) + a step's click: that pitch on the step, not the pad's own note; a held step lights the
 * pitches on it */
static void t_pitch_step(void)
{
    const int clip = SEQ_CLIP(12, 0);
    uint8_t w[RFM_MOVE_NLEDS][3], w0[RFM_MOVE_NLEDS][3];
    kit_view();
    hit(kp(1));
    CHECK(M.kit.rh == RFM_MOVE_KIT_RH_PITCHES);
    pad(rp(3), 100), tap_step(6), pad(rp(3), 0);
    CHECK(rfmv_seq_find(&M, clip, 6, rfm_move_kit_note(1, 3)) >= 0 && rfmv_seq_find(&M, clip, 6, 37) < 0);
    pad(rp(3), 100), tap_step(6), pad(rp(3), 0);         /* (again: out) */
    CHECK(rfmv_seq_find(&M, clip, 6, rfm_move_kit_note(1, 3)) < 0);
    pad(rp(3), 100), tap_step(6), pad(rp(3), 0);
    rest(), leds(w0);
    step(6, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    leds(w);
    step(6, 0);
    CHECK(!is_rgb(w[L_PAD0 + rp(3)], w0[L_PAD0 + rp(3)][0], w0[L_PAD0 + rp(3)][1], w0[L_PAD0 + rp(3)][2]) &&
          is_rgb(w[L_PAD0 + rp(3)], 255, 60, 160));     /* (lit in its row's colour, track 13's) */
    CHECK(is_rgb(w[L_PAD0 + rp(5)], w0[L_PAD0 + rp(5)][0], w0[L_PAD0 + rp(5)][1], w0[L_PAD0 + rp(5)][2]));
    CHECK(rfmv_seq_find(&M, clip, 6, rfm_move_kit_note(1, 3)) >= 0);   /* (the hold: nothing toggled) */
}

/* the KIT_NOTE ons of pad k while the sequencer plays ms */
static int play_ons(int k, int ms, rfm_move_act_t *first)
{
    int i, n = 0;
    clear();
    rfm_move_seq_play(&M, 1);
    adv_ms((uint32_t)ms);
    rfm_move_seq_play(&M, 0);
    for (i = 0; i < nact; i++)
        if (A[i].kind == RFM_MOVE_ACT_KIT_NOTE && A[i].down && A[i].index == k) {
            if (!n && first)
                *first = A[i];
            n++;
        }
    return n;
}
/* 41. a step lock on a setting with no LK lock of its own (Pad Mix's Send 1: an XLOCK slot pair) reaches the hit's
 * KIT_NOTE as kp_mask / kp[] */
static void t_xlock(void)
{
    const int clip = SEQ_CLIP(12, 0);
    rfm_move_act_t a;
    int c, e;
    kit_view();
    sound(1, "Snare", 4800, NULL, 0);
    hit(kp(1)), tap_step(0);
    CHECK(rfmv_seq_find(&M, clip, 0, 37) >= 0);
    jog(RFM_MOVE_KIT_PG_PADMIX - RFM_MOVE_KIT_PG_SAMPLE), rest();
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_PADMIX);
    step(0, 1), knob(4, 30), step(0, 0);                /* (knob 5: Send 1) */
    c = rfmv_seq_lock_get(&M, clip, 0, RFM_MOVE_KIT_XLOCK0 + (0 * 4 + 1) * 2);
    e = rfmv_seq_lock_get(&M, clip, 0, RFM_MOVE_KIT_XLOCK0 + (0 * 4 + 1) * 2 + 1);
    CHECKF(c == RFM_MOVE_KIT_P_SEND1 && e > 0, "slot 0: %d = %d", c, e);
    CHECK(M.kit.pad[1].v[RFM_MOVE_KIT_P_SEND1] == 0 && rfmv_seq_find(&M, clip, 0, 37) >= 0);   /* (the pad's own kept) */
    M.seq.tr[12].active = 0;
    memset(&a, 0, sizeof a);
    CHECK(play_ons(1, 200, &a) >= 1);
    CHECKF((a.kp_mask >> RFM_MOVE_KIT_P_SEND1 & 1) && a.kp[RFM_MOVE_KIT_P_SEND1] == e,
           "kp_mask %llx send1 %d", (unsigned long long)a.kp_mask, a.kp[RFM_MOVE_KIT_P_SEND1]);
    CHECK(!(a.kp_mask >> RFM_MOVE_KIT_P_SEND2 & 1));
}

/* 41b. (2026-11-26, the owner's "LoFi bits ... floating point ... up to 24bit") Bits in tenths 1.0..24.0: a detent
 * 0.1, "12.5 bit"; a step's lock in tenths; a version 16 kit part without the tenths flag (whole bits, its locks) read
 * as tenths */
static void t_bits(void)
{
    static uint8_t b[RFM_MOVE_KIT_BYTES];
    const int clip = SEQ_CLIP(12, 0), xs = RFM_MOVE_KIT_XLOCK0 + (0 * 4 + 1) * 2;
    char w[96];
    uint16_t subj;
    uint32_t stamp;
    const int at = 16 + RFM_MOVE_KIT_NAME + 1 * (RFM_MOVE_KIT_PATH + 2 * RFM_MOVE_KIT_P_N) + RFM_MOVE_KIT_PATH +
                   2 * RFM_MOVE_KIT_P_BITS;           /* (pad 2's Bits in the kit part) */
    kit_view();
    sound(1, "Snare", 4800, NULL, 0);
    hit(kp(1)), rest();
    CHECK(M.kit.pad[1].v[RFM_MOVE_KIT_P_BITS] == 160);  /* (16.0 bits, as before) */
    rfm_move_kit_set(&M, 1, RFM_MOVE_KIT_P_MODE, RFM_MOVE_KIT_M_LOFI);
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_SAMPLE);        /* (2026-11-26: the engine's on Pad Sample's 5..8) */
    knob(4, 125);                                       /* (3 detents down) */
    CHECKF(M.kit.pad[1].v[RFM_MOVE_KIT_P_BITS] == 157, "bits %d", M.kit.pad[1].v[RFM_MOVE_KIT_P_BITS]);
    rfm_move_kit_set(&M, 1, RFM_MOVE_KIT_P_BITS, 125);
    knob(4, 1), knob(4, 127);
    CHECK(rfmv_kit_pop_words(&M, w, sizeof w, &subj, &stamp) && strstr(w, "12.5 bit"));
    rfm_move_kit_set(&M, 1, RFM_MOVE_KIT_P_BITS, 999);
    CHECK(M.kit.pad[1].v[RFM_MOVE_KIT_P_BITS] == 240);   /* (24 bits at most) */
    rest(), tap_step(0), rest();
    step(0, 1), knob(4, 127), step(0, 0), rest();       /* (a lock: 23.9 bits) */
    CHECKF(rfmv_seq_lock_get(&M, clip, 0, xs) == RFM_MOVE_KIT_P_BITS && rfmv_seq_lock_get(&M, clip, 0, xs + 1) == 239 - 10,
           "lock %d = %d", rfmv_seq_lock_get(&M, clip, 0, xs), rfmv_seq_lock_get(&M, clip, 0, xs + 1));
    /* a kit part saved before the tenths: the flag off, whole bits (12), a whole-bit lock (7 bits: 6) */
    rfmv_kit_write(&M, b);
    CHECK(b[13] == 1 && rfmv_kit_check(b, 17) == 0);
    b[13] = 0, b[at] = 12, b[at + 1] = 0;
    CHECK(rfmv_kit_check(b, 17) == 0);
    rfmv_seq_lock_set(&M, clip, 0, xs + 1, 6);
    rfmv_kit_read(&M, b, 17);
    CHECKF(M.kit.pad[1].v[RFM_MOVE_KIT_P_BITS] == 120 && rfmv_seq_lock_get(&M, clip, 0, xs + 1) == 60,
           "bits %d lock %d", M.kit.pad[1].v[RFM_MOVE_KIT_P_BITS], rfmv_seq_lock_get(&M, clip, 0, xs + 1));
    b[13] = 1, b[at] = 5;                               /* (tenths: 0.5 bits is out of range) */
    CHECK(rfmv_kit_check(b, 17) != 0);
}
/* 41c. (2026-11-26, the owner's) Loop Start / End as a percentage of the region, its ends START / END (screen and
 * words: "Loop Start: Start") */
static void t_loop_pct(void)
{
    char w[96];
    uint16_t subj;
    uint32_t stamp;
    kit_view();
    sound(1, "Snare", 48000, NULL, 0);
    hit(kp(1)), rest();
    rfm_move_kit_set(&M, 1, RFM_MOVE_KIT_P_MODE, RFM_MOVE_KIT_M_LOOP);
    rfm_move_kit_set(&M, 1, RFM_MOVE_KIT_P_START, RFM_MOVE_KIT_FRAC / 4);   /* (the region: its second quarter on) */
    rfm_move_kit_set(&M, 1, RFM_MOVE_KIT_P_LOOPS, 0), rfm_move_kit_set(&M, 1, RFM_MOVE_KIT_P_LOOPE, RFM_MOVE_KIT_FRAC);
    touch(4, 1);
    CHECKF(rfmv_kit_pop_words(&M, w, sizeof w, &subj, &stamp) && !strcmp(w, "Loop Start: Start"), "words %s", w);
    screen();
    touch(4, 0), rest(), touch(5, 1);
    CHECKF(rfmv_kit_pop_words(&M, w, sizeof w, &subj, &stamp) && !strcmp(w, "Loop End: End"), "words %s", w);
    touch(5, 0), rest();
    rfm_move_kit_set(&M, 1, RFM_MOVE_KIT_P_LOOPS, RFM_MOVE_KIT_FRAC / 4 + (RFM_MOVE_KIT_FRAC - RFM_MOVE_KIT_FRAC / 4) / 2);
    touch(4, 1);
    CHECKF(rfmv_kit_pop_words(&M, w, sizeof w, &subj, &stamp) && !strcmp(w, "Loop Start: 50.0%"), "words %s", w);
    touch(4, 0), rest();
}
/* 41d. (2026-11-26, the owner's) Granular's Scan: Pad Sample 5 Position, 6 Scan, 7 Size, 8 Density; Pad Sample 2 5
 * Spray, 6 Spread; Scan -100..+100 ("Off" at 0, "+35 %"), locked on a step as the rest */
static void t_scan(void)
{
    char w[96];
    uint16_t subj;
    uint32_t stamp;
    const int clip = SEQ_CLIP(12, 0);
    kit_view();
    sound(1, "Snare", 48000, NULL, 0);
    hit(kp(1)), rest();
    rfm_move_kit_set(&M, 1, RFM_MOVE_KIT_P_MODE, RFM_MOVE_KIT_M_GRANULAR);
    CHECK(M.kit.page == RFM_MOVE_KIT_PG_SAMPLE && M.kit.pad[1].v[RFM_MOVE_KIT_P_GSCAN] == 0);
    touch(5, 1);
    CHECKF(rfmv_kit_pop_words(&M, w, sizeof w, &subj, &stamp) && !strcmp(w, "Scan: Off"), "words %s", w);
    knob(5, 35);
    CHECKF(M.kit.pad[1].v[RFM_MOVE_KIT_P_GSCAN] == 35 && rfmv_kit_pop_words(&M, w, sizeof w, &subj, &stamp) &&
           !strcmp(w, "Scan: +35 %"), "scan %d words %s", M.kit.pad[1].v[RFM_MOVE_KIT_P_GSCAN], w);
    knob(5, 127 - 59), touch(5, 0), rest();             /* (-60: -25) */
    CHECK(M.kit.pad[1].v[RFM_MOVE_KIT_P_GSCAN] == -25);
    knob(6, 2), knob(7, 1);                             /* (7 Size, 8 Density) */
    CHECK(M.kit.pad[1].v[RFM_MOVE_KIT_P_GSIZE] != 80 && M.kit.pad[1].v[RFM_MOVE_KIT_P_GDENS] == 5);
    M.kit.page = RFM_MOVE_KIT_PG_SAMPLE2;
    knob(4, 10), knob(5, 3);                            /* (Pad Sample 2: 5 Spray, 6 Spread) */
    CHECK(M.kit.pad[1].v[RFM_MOVE_KIT_P_GSPRAY] == 10 && M.kit.pad[1].v[RFM_MOVE_KIT_P_GSPREAD] == 3);
    M.kit.page = RFM_MOVE_KIT_PG_SAMPLE, rest();
    tap_step(0), rest();
    step(0, 1), touch(5, 1), knob(5, 10), touch(5, 0), step(0, 0), rest();   /* (a lock: -15) */
    {
        int j, got = 0;
        for (j = 0; j < RFM_MOVE_KIT_XSLOTS; j++)
            if (rfmv_seq_lock_get(&M, clip, 0, RFM_MOVE_KIT_XLOCK0 + (j * 4 + 1) * 2) == RFM_MOVE_KIT_P_GSCAN)
                got = rfmv_seq_lock_get(&M, clip, 0, RFM_MOVE_KIT_XLOCK0 + (j * 4 + 1) * 2 + 1) - 100;
        CHECKF(got == -15 && M.kit.pad[1].v[RFM_MOVE_KIT_P_GSCAN] == -25, "lock %d", got);
    }
}
/* 42. the Kit view's Layout menu (Shift + step 8): the jog's turn sets the right half's mode, not the note layout */
static void t_kit_layout(void)
{
    int l0;
    kit_view();
    l0 = M.s.layout;
    CHECK(M.kit.rh == RFM_MOVE_KIT_RH_PITCHES && rfmv_kit_rh_of(&M) == RFM_MOVE_KIT_RH_PITCHES);
    button(MR_CC_SHIFT, 1), step(7, 1), step(7, 0), button(MR_CC_SHIFT, 0), now_ms += RFM_MOVE_DOUBLE_MS;
    CHECKF(M.seq.smenu == RFM_MOVE_SM_LAYOUT, "smenu %d", M.seq.smenu);
    rest(), jog(1);
    CHECKF(M.kit.rh == RFM_MOVE_KIT_RH_INKEY && M.s.layout == l0, "rh %d layout %d", M.kit.rh, M.s.layout);
    jog(9);
    CHECK(M.kit.rh == RFM_MOVE_KIT_RH_CHOP && M.s.layout == l0);   /* (clamped at the last) */
    jog(-9);
    CHECK(M.kit.rh == RFM_MOVE_KIT_RH_PITCHES && M.s.layout == l0);
    back();
}

/* 43. the kit open, Track 4 held + the volume knob: the kit's level, no firmware's, its popup (not the held Track's
 * menu) while the knob is touched and a second after; (2026-11-26) the same from the Mixer, the kit not shown */
static int scr_eq(const uint8_t *a, const uint8_t *b) { return !memcmp(a, b, MR_SCREEN_BYTES); }
static mr_canvas_t CV;
static void t_kit_level(void)
{
    uint8_t lv[RFM_MOVE_FW_SLOTS], menu[MR_SCREEN_BYTES];
    int l0;
    kit_view();
    memcpy(lv, M.fw.level, sizeof lv);
    l0 = M.kit.mix[RFM_MOVE_KIT_MIX_LEVEL];
    hold_track(3), clear();
    screen(), memcpy(menu, SCR, sizeof menu);           /* (Track 4's menu) */
    touch(8, 1);
    CHECKF(M.kit.lvl == 2 && M.kit.lvl_touch, "lvl %d", M.kit.lvl);
    raw(0xB0, MR_CC_VOLUME, (uint8_t)(-3 & 127));
    CHECKF(M.kit.mix[RFM_MOVE_KIT_MIX_LEVEL] == rfmv_lvl_step(l0, -3), "level %d", M.kit.mix[RFM_MOVE_KIT_MIX_LEVEL]);
    CHECK(!memcmp(lv, M.fw.level, sizeof lv) && !count(RFM_MOVE_ACT_FW_LEVEL) && count(RFM_MOVE_ACT_MIX) >= 1);
    adv_ms(RFM_MOVE_POPUP_MS + 200);                    /* (touched: it stays) */
    screen();
    CHECK(!scr_eq(SCR, menu) && rfmv_kit_level_screen(&M, &CV) == 1);
    {
        uint8_t pop[MR_SCREEN_BYTES];
        memcpy(pop, SCR, sizeof pop);
        touch(8, 0), adv_ms(RFM_MOVE_POPUP_MS + 50);
        screen();
        CHECK(scr_eq(SCR, menu) && !scr_eq(pop, menu)); /* (let go: gone after its second, the menu again) */
    }
    track(3, 0), rest();
    /* the Mixer (Track 4's round, the kit not shown): Track 4 held + the volume knob is the kit's level all the same */
    tap_track(3), rest();
    CHECKF(M.seq.projects && !rfmv_kit_active(&M), "projects %d", M.seq.projects);
    l0 = M.kit.mix[RFM_MOVE_KIT_MIX_LEVEL];
    hold_track(3), clear();
    raw(0xB0, MR_CC_VOLUME, 2);
    CHECKF(M.kit.mix[RFM_MOVE_KIT_MIX_LEVEL] == rfmv_lvl_step(l0, 2) && M.kit.lvl == 2 && rfmv_kit_level_screen(&M, &CV) == 1,
           "level %d (was %d)", M.kit.mix[RFM_MOVE_KIT_MIX_LEVEL], l0);
    CHECK(!count(RFM_MOVE_ACT_MASTER));
    track(3, 0), rest();
}

/* 47. (2026-11-26) the Kit view, a pad held + the volume knob: that pad's Level in its knob's steps, its popup while
 * touched; no step, no other pad, no master */
static void t_pad_level(void)
{
    const int clip = SEQ_CLIP(12, 0);
    int l0, l2, nn;
    kit_view();
    sound(1, "Snare", 4800, NULL, 0);
    sound(2, "Hat", 4800, NULL, 0);
    hit(kp(1)), tap_step(0), rest();
    nn = M.seq.nnotes, l0 = M.kit.pad[1].v[RFM_MOVE_KIT_P_LEVEL], l2 = M.kit.pad[2].v[RFM_MOVE_KIT_P_LEVEL];
    pad(kp(1), 100), clear();
    touch(8, 1);
    CHECKF(M.kit.lvl == 1, "lvl %d", M.kit.lvl);
    raw(0xB0, MR_CC_VOLUME, (uint8_t)(-4 & 127));       /* (two steps: two ticks a step, as its knob) */
    CHECKF(M.kit.pad[1].v[RFM_MOVE_KIT_P_LEVEL] < l0 && M.kit.pad[2].v[RFM_MOVE_KIT_P_LEVEL] == l2,
           "level %d (was %d)", M.kit.pad[1].v[RFM_MOVE_KIT_P_LEVEL], l0);
    CHECK(!count(RFM_MOVE_ACT_MASTER) && count(RFM_MOVE_ACT_KIT_SET) >= 1);
    adv_ms(RFM_MOVE_POPUP_MS + 200);
    CHECK(rfmv_kit_level_screen(&M, &CV) == 1);   /* (touched: up) */
    touch(8, 0), pad(kp(1), 0), adv_ms(RFM_MOVE_POPUP_MS + 50);
    CHECK(!rfmv_kit_level_screen(&M, &CV));
    CHECKF(M.seq.nnotes == nn && rfmv_seq_find(&M, clip, 0, 37) >= 0, "notes %d (was %d)", M.seq.nnotes, nn);
    pad(kp(1), 100), pad(kp(2), 100);                   /* (two held: both) */
    l0 = M.kit.pad[1].v[RFM_MOVE_KIT_P_LEVEL], l2 = M.kit.pad[2].v[RFM_MOVE_KIT_P_LEVEL];
    raw(0xB0, MR_CC_VOLUME, (uint8_t)(-2 & 127));
    CHECK(M.kit.pad[1].v[RFM_MOVE_KIT_P_LEVEL] < l0 && M.kit.pad[2].v[RFM_MOVE_KIT_P_LEVEL] < l2);
    pad(kp(1), 0), pad(kp(2), 0), rest();
}

/* 50. (2026-11-26, the owner's: "each volume level adjustment feels like it's all part of the whole") one level popup:
 * a pad's level from its held pad + the volume knob is Pad Mix's knob 1's; the kit's from Track 4 + the volume knob is
 * Kit Mix's knob 1's and the Mixer's Kit strip's; a slot's from its Track + the volume knob is the Mixer's knob's; each
 * value in dB */
static void t_level_popups(void)
{
    static uint8_t a[MR_SCREEN_BYTES], b[MR_SCREEN_BYTES];
    char v[16];
    kit_view();
    sound(1, "Snare", 4800, NULL, 0);
    hit(kp(1)), rest();
    M.kit.page = RFM_MOVE_KIT_PG_PADMIX;
    touch(0, 1), screen(), memcpy(a, SCR, sizeof a), touch(0, 0), rest();
    pad(kp(1), 100), touch(8, 1), screen(), memcpy(b, SCR, sizeof b);
    dump("level_pad");
    touch(8, 0), pad(kp(1), 0), rest();
    CHECK(scr_eq(a, b) && M.kit.pad[1].v[RFM_MOVE_KIT_P_LEVEL] == 0);   /* (Pad Mix's knob 1 = the held pad's) */
    rfmv_db_text(v, sizeof v, 0, 1);
    CHECK(!strcmp(v, "0 dB"));
    rfmv_db_text(v, sizeof v, rfmv_lvl_db10(100), 1);
    CHECK(!strcmp(v, "-2.1 dB"));
    rfmv_db_text(v, sizeof v, -120, 1);
    CHECK(!strcmp(v, "-12 dB"));
    rfmv_db_text(v, sizeof v, 20, 1);
    CHECK(!strcmp(v, "+2.0 dB"));
    rfmv_db_text(v, sizeof v, rfmv_lvl_db10(0), 1);
    CHECK(!strcmp(v, "-inf dB"));
    CHECK(rfmv_lvl_step(127, -1) == 120 && rfmv_lvl_step(120, 1) == 127 && rfmv_lvl_step(1, -1) == 0 && rfmv_lvl_step(0, 1) == 1);
    M.kit.page = RFM_MOVE_KIT_PG_KITMIX;               /* the kit: Kit Mix's knob 1, Track 4 + the volume knob */
    touch(0, 1), screen(), memcpy(a, SCR, sizeof a), touch(0, 0), rest();
    hold_track(3), touch(8, 1), screen(), memcpy(b, SCR, sizeof b);
    dump("level_kit");
    touch(8, 0), track(3, 0), rest();
    CHECK(scr_eq(a, b));
    tap_track(3), rest();                               /* .. the Mixer's Kit strip */
    CHECK(M.seq.projects && rfmv_t4_page(&M) == 0 && M.fw.mix_par == RFM_MOVE_MIXP_LEVEL);
    touch(3, 1), screen(), memcpy(a, SCR, sizeof a), touch(3, 0), rest();
    CHECK(scr_eq(a, b));
    touch(0, 1), screen(), memcpy(a, SCR, sizeof a), touch(0, 0), rest();   /* a slot: the Mixer's knob 1 */
    hold_track(0);
    touch(8, 1), screen(), memcpy(b, SCR, sizeof b);
    dump("level_track");
    touch(8, 0), track(0, 0), rest();
    CHECKF(scr_eq(a, b), "track popups differ (level %d)", M.fw.level[0]);
}

/* 51. (2026-11-26, the owner's: "Holding a step while stopped should preview it") stopped, a step's press plays the
 * chosen pad's hits there once (KIT_NOTE with the step's locks), its release lets them go; nothing toggles; while
 * playing the press plays nothing */
static void t_step_preview(void)
{
    const int clip = SEQ_CLIP(12, 0);
    const rfm_move_act_t *a;
    int nn, i, ons = 0;
    kit_view();
    sound(1, "Snare", 4800, NULL, 0);
    hit(kp(1)), tap_step(0), rest();
    M.kit.page = RFM_MOVE_KIT_PG_PADMIX;
    step(0, 1), knob(0, -4), step(0, 0), rest();         /* (a level lock on the hit) */
    nn = M.seq.nnotes;
    clear(), step(0, 1);
    CHECKF((a = last(RFM_MOVE_ACT_KIT_NOTE)) && a->down && a->index == 1 && a->vel > 0 && (a->kp_mask >> RFM_MOVE_KIT_P_LEVEL & 1) &&
           count(RFM_MOVE_ACT_KIT_NOTE) == 1, "notes %d", count(RFM_MOVE_ACT_KIT_NOTE));
    adv_ms(RFM_MOVE_SEQ_HOLD_MS + 50), clear(), step(0, 0);
    CHECK((a = last(RFM_MOVE_ACT_KIT_NOTE)) && !a->down && a->index == 1 && count(RFM_MOVE_ACT_KIT_NOTE) == 1);
    CHECK(M.seq.nnotes == nn && rfmv_seq_find(&M, clip, 0, 37) >= 0);   /* (nothing toggled) */
    clear(), step(5, 1), step(5, 0), rest();            /* (an empty step: nothing to play; its click puts a hit) */
    for (i = 0; i < nact; i++)
        ons += A[i].kind == RFM_MOVE_ACT_KIT_NOTE && A[i].down;
    CHECK(!ons);
    rfm_move_seq_play(&M, 1), adv_ms(5), clear();
    step(0, 1);
    CHECK(!count(RFM_MOVE_ACT_KIT_NOTE));               /* (playing: the press plays nothing more) */
    step(0, 0), rfm_move_seq_play(&M, 0), rest();
}

/* 52. (2026-11-26, the owner's: "You should be able to lock parameters on any step. If you lock a parameter on a step
 * without notes it becomes a ghost trigger") a lock on a step without the pad's hit is written (no "No Hit"); the step
 * shows the ghost (the pad's colour, dim, steady); the sequencer sends KIT_GHOST with its continuous locks, no note;
 * it saves and loads; Delete + the step clears it; a tap turns it into a hit that keeps its locks */
static void t_ghost(void)
{
    const int clip = SEQ_CLIP(12, 0), lv = RFM_MOVE_KIT_LOCK0 + RFM_MOVE_KIT_LK_LEVEL * 4 + 1;
    const rfm_move_act_t *a;
    int nn, i, g = 0, ons = 0;
    kit_view();
    sound(1, "Snare", 4800, NULL, 0);
    hit(kp(1)), tap_step(0), rest();
    M.kit.page = RFM_MOVE_KIT_PG_PADMIX;
    nn = M.seq.nnotes;
    step(2, 1), touch(0, 1), knob(0, -6), knob(2, -10);   /* (Level and Cutoff on step 3: no hit there) */
    screen();
    CHECK(rfmv_seq_lock_get(&M, clip, 2, lv) >= 0 && M.seq.nnotes == nn && M.kit.lpop);
    touch(0, 0), step(2, 0), rest();
    CHECK(rfmv_seq_find(&M, clip, 2, 37) < 0 && rfmv_seq_lock_get(&M, clip, 2, lv) >= 0);   /* (no hit made) */
    L(-1);
    {   /* (the owner's 2026-11-26: a ghost read as an empty step) a pastel of the row's colour: every channel lit, at
         * least 3x an empty step's light, paler than a hit, not white; steady */
        const uint8_t *g = L(L_STEP0 + 2), *e = L(L_STEP0 + 5), *h = L(L_STEP0);
        uint8_t g0[3];
        const int gs = g[0] + g[1] + g[2], es = e[0] + e[1] + e[2];
        memcpy(g0, g, 3);
#define MX(c) ((c)[0] > (c)[1] ? ((c)[0] > (c)[2] ? (c)[0] : (c)[2]) : ((c)[1] > (c)[2] ? (c)[1] : (c)[2]))
#define MN(c) ((c)[0] < (c)[1] ? ((c)[0] < (c)[2] ? (c)[0] : (c)[2]) : ((c)[1] < (c)[2] ? (c)[1] : (c)[2]))
        CHECKF(g[0] && g[1] && g[2] && gs >= 3 * es && !(g[0] == g[1] && g[1] == g[2]) && !is_rgb(g, h[0], h[1], h[2]) &&
               (MX(g) - MN(g)) * MX(h) < (MX(h) - MN(h)) * MX(g), "ghost %d %d %d, empty %d %d %d, hit %d %d %d", g[0], g[1], g[2],
               e[0], e[1], e[2], h[0], h[1], h[2]);
        adv_ms(500), L(-1);
        CHECK(is_rgb(L(L_STEP0 + 2), g0[0], g0[1], g0[2]));
#undef MX
#undef MN
    }
    clear();
    rfm_move_seq_play(&M, 1), adv_ms(600), rfm_move_seq_play(&M, 0);
    for (i = 0; i < nact; i++)
        if (A[i].kind == RFM_MOVE_ACT_KIT_GHOST && A[i].index == 1 && !g++)
            CHECKF((A[i].kp_mask >> RFM_MOVE_KIT_P_LEVEL & 1) && (A[i].kp_mask >> RFM_MOVE_KIT_P_CUTOFF & 1) &&
                   A[i].kp[RFM_MOVE_KIT_P_LEVEL] < 0 &&
                   !(A[i].kp_mask >> RFM_MOVE_KIT_P_START & 1), "mask %llx", (unsigned long long)A[i].kp_mask);
        else if (A[i].kind == RFM_MOVE_ACT_KIT_NOTE && A[i].down)
            ons++;
    CHECKF(g == 1 && ons == 1, "ghosts %d notes %d", g, ons);   /* (step 1's hit, step 3's ghost) */
    {                                                   /* it saves and loads */
        static uint8_t b[RFM_MOVE_PROJECT_MAX];
        const int n = rfm_move_project_save(&M, b, sizeof b);
        rfm_move_init(&M2, NULL, on_act, NULL);
        rfm_move_fw_caps(&M2, KCAPS);
        CHECK(n > 0 && rfm_move_project_load(&M2, 1, b, (uint32_t)n) == 0 && rfmv_seq_lock_get(&M2, clip, 2, lv) >= 0 &&
              rfmv_seq_find(&M2, clip, 2, 37) < 0);
    }
    button(MR_CC_DELETE, 1), tap_step(2), button(MR_CC_DELETE, 0), rest();   /* Delete + the step: gone */
    CHECK(rfmv_seq_lock_get(&M, clip, 2, lv) < 0 && M.seq.nnotes == nn);
    step(2, 1), knob(0, -6), step(2, 0), rest();        /* again; a tap: a hit, its locks kept */
    i = rfmv_seq_lock_get(&M, clip, 2, lv);
    adv_ms(RFM_MOVE_DOUBLE_MS + 50), tap_step(2), rest();
    CHECK(rfmv_seq_find(&M, clip, 2, 37) >= 0 && i >= 0 && rfmv_seq_lock_get(&M, clip, 2, lv) == i);
    (void)a;
}

/* 53. (2026-11-26, Move's 14.2.3 "Deleting Automation") Delete held + a knob touched, no step held: the chosen pad's
 * locks of that setting on every step of its clip (Send 1: its XLOCK pairs freed), the hits kept, a ghost with no lock
 * left gone, one with another lock kept; "Cleared" "Send 1", said; again: "No" "Automation"; Undo: as before, at once;
 * (the owner's) a step held, a knob touched, then Delete: that step's lock only, Undo brings it back; a plain Delete with
 * a step held and no knob touched: nothing (the hit stays) */
/* (the owner's 2026-11-26 report) a Cutoff lock on step 2 from the Filter page: Delete held + knob 1 touched clears it;
 * so does knob 1 touched, then Delete pressed (no step held) */
static void t_auto_clear_cutoff(void)
{
    const int clip = SEQ_CLIP(12, 0), lc = RFM_MOVE_KIT_LOCK0 + RFM_MOVE_KIT_LK_CUTOFF * 4 + 1;
    kit_view();
    sound(1, "Snare", 4800, NULL, 0);
    hit(kp(1)), tap_step(0), tap_step(1), rest();
    M.kit.page = RFM_MOVE_KIT_PG_FILTER;
    step(1, 1), knob(0, -20), step(1, 0), rest();
    CHECKF(rfmv_seq_lock_get(&M, clip, 1, lc) >= 0, "the cutoff lock on step 2: %d", rfmv_seq_lock_get(&M, clip, 1, lc));
    clear();
    button(MR_CC_DELETE, 1), touch(0, 1), touch(0, 0), button(MR_CC_DELETE, 0), rest();
    CHECKF(rfmv_seq_lock_get(&M, clip, 1, lc) < 0, "Delete + knob 1: cleared (%d; msg %s %s)",
           rfmv_seq_lock_get(&M, clip, 1, lc), M.kit.msg, M.kit.msg2);
    press(MR_CC_UNDO), rest();
    CHECK(rfmv_seq_lock_get(&M, clip, 1, lc) >= 0);
    touch(0, 1), press(MR_CC_DELETE), touch(0, 0), rest();   /* (the other order: the knob touched, then Delete) */
    CHECKF(rfmv_seq_lock_get(&M, clip, 1, lc) < 0, "knob 1 then Delete: cleared (%d)", rfmv_seq_lock_get(&M, clip, 1, lc));
}
static void t_auto_clear(void)
{
    static rfm_move_lock_t before[RFM_MOVE_SEQ_LOCKS];
    const int clip = SEQ_CLIP(12, 0), lv = RFM_MOVE_KIT_LOCK0 + RFM_MOVE_KIT_LK_LEVEL * 4 + 1;
    const int x0 = RFM_MOVE_KIT_XLOCK0 + (0 * 4 + 1) * 2;
    int nb, nn, s, sends = 0;
    char w[64];
    uint16_t subj;
    uint32_t stamp;
    kit_view();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    sound(1, "Snare", 4800, NULL, 0);
    hit(kp(1)), tap_step(0), rest();
    M.kit.page = RFM_MOVE_KIT_PG_PADMIX;
    step(0, 1), knob(4, 30), step(0, 0), rest();        /* (Send 1 on the hit) */
    step(2, 1), knob(4, 20), step(2, 0), rest();        /* (a ghost: Send 1 alone) */
    step(5, 1), knob(0, -6), knob(4, 10), step(5, 0), rest();   /* (a ghost: Level and Send 1) */
    CHECK(rfmv_seq_lock_get(&M, clip, 0, x0) == RFM_MOVE_KIT_P_SEND1 && rfmv_seq_lock_get(&M, clip, 2, x0) == RFM_MOVE_KIT_P_SEND1 &&
          rfmv_seq_lock_get(&M, clip, 5, lv) >= 0);
    nn = M.seq.nnotes, nb = M.seq.nlocks;
    memcpy(before, M.seq.locks, sizeof before);
    clear();
    button(MR_CC_DELETE, 1), touch(4, 1);
    for (s = 0; s < RFM_MOVE_SEQ_MAXLEN; s++)
        sends += rfmv_seq_lock_get(&M, clip, s, x0) >= 0 || rfmv_seq_lock_get(&M, clip, s, x0 + 1) >= 0;
    CHECKF(!sends && M.seq.nlocks == nb - 6, "sends %d locks %d of %d", sends, M.seq.nlocks, nb);
    CHECK(M.seq.nnotes == nn && rfmv_seq_find(&M, clip, 0, 37) >= 0);   /* (the hit kept) */
    {
        int lo;
        CHECK(rfmv_seq_lock_range(&M, clip, 2, &lo) == lo);   /* (step 3's ghost: gone) */
    }
    CHECK(rfmv_seq_lock_get(&M, clip, 5, lv) >= 0);    /* (step 6's: its Level kept) */
    CHECKF(!strcmp(M.kit.msg, "Cleared") && !strcmp(M.kit.msg2, "Send 1"), "msg %s %s", M.kit.msg, M.kit.msg2);
    CHECK(rfmv_kit_pop_words(&M, w, sizeof w, &subj, &stamp) && !strcmp(w, "Cleared Send 1"));
    touch(4, 0), button(MR_CC_DELETE, 0);
    adv_ms(50);
    CHECKF(strstr(said, "Cleared Send 1") != NULL, "said %s", said);
    CHECK(!strstr(said, "Delete:"));                    /* (the release: no hint) */
    rest();
    button(MR_CC_DELETE, 1), touch(4, 1), touch(4, 0), button(MR_CC_DELETE, 0);
    CHECKF(!strcmp(M.kit.msg, "No") && !strcmp(M.kit.msg2, "Automation") && M.seq.nlocks == nb - 6, "msg %s %s",
           M.kit.msg, M.kit.msg2);
    rest(), press(MR_CC_UNDO), rest();
    CHECKF(M.seq.nlocks == nb && !memcmp(M.seq.locks, before, (size_t)nb * sizeof before[0]) && M.seq.nnotes == nn,
           "undo: locks %d of %d", M.seq.nlocks, nb);   /* (every lock, the ghost and the slots as they were) */
    /* the owner's: a step held, a knob touched, then Delete: that step's lock only, its own undo step */
    step(5, 1), touch(4, 1), touch(4, 0), press(MR_CC_DELETE);
    CHECK(rfmv_seq_lock_get(&M, clip, 5, x0) < 0 && rfmv_seq_lock_get(&M, clip, 5, x0 + 1) < 0 &&
          rfmv_seq_lock_get(&M, clip, 5, lv) >= 0 && rfmv_seq_lock_get(&M, clip, 2, x0) == RFM_MOVE_KIT_P_SEND1 &&
          rfmv_seq_lock_get(&M, clip, 0, x0) == RFM_MOVE_KIT_P_SEND1);
    CHECK(M.kit.lpop == 5 && M.kit.kpop == 4 && M.kit.lkc_ms && !M.kit.lkc_none);   /* (its popup, "Lock Cleared") */
    CHECKF(rfmv_kit_pop_words(&M, w, sizeof w, &subj, &stamp) && !strncmp(w, "Step 6 Send 1: ", 15), "words %s", w);
    screen();                                           /* (2026-11-26: no tag; cleared: no padlock) */
    CHECK(!px(0, 0) && !scr_padlock(SCR));
    step(5, 0), rest();
    CHECKF(strstr(said, "Lock cleared, Send 1") != NULL, "said %s", said);
    CHECK(rfmv_seq_find(&M, clip, 5, 37) < 0);         /* (the release toggled nothing) */
    step(2, 1), touch(4, 1), press(MR_CC_DELETE), touch(4, 0), step(2, 0), rest();   /* (still touched: the same) */
    {
        int lo;
        CHECK(rfmv_seq_lock_range(&M, clip, 2, &lo) == lo && rfmv_seq_find(&M, clip, 2, 37) < 0);   /* (the ghost: gone) */
    }
    press(MR_CC_UNDO), rest();
    CHECK(rfmv_seq_lock_get(&M, clip, 2, x0) == RFM_MOVE_KIT_P_SEND1 && rfmv_seq_lock_get(&M, clip, 5, x0) < 0);
    press(MR_CC_UNDO), rest();
    CHECK(M.seq.nlocks == nb && !memcmp(M.seq.locks, before, (size_t)nb * sizeof before[0]));
    step(0, 1), press(MR_CC_DELETE), step(0, 0), rest();   /* (no knob touched: nothing; the hit stays) */
    CHECK(M.seq.nlocks == nb && rfmv_seq_find(&M, clip, 0, 37) >= 0);
    step(0, 1), button(MR_CC_DELETE, 1), knob(4, 1), button(MR_CC_DELETE, 0), step(0, 0), rest();   /* (the other
                                                         * order: step, Delete, a turn) */
    CHECK(rfmv_seq_lock_get(&M, clip, 0, x0) < 0 && rfmv_seq_find(&M, clip, 0, 37) >= 0);
    rfm_move_set_speech(&M, 0);
}

/* 48. (2026-11-26, the owner's: "the popup displays completely differently while holding a step") a held step's lock
 * popup is the knob's own popup (the S tag in the corner aside), for every lockable knob on every page and engine; a
 * locked cutoff draws as the pad at that cutoff */
/* (2026-11-26, the owner's "a clear locked / not-locked language") every knob's popup with a step held: not locked,
 * no padlock and hollow (its pixels a part of the locked one's); locked (a turn there and back), the padlock; no step's
 * name either way; without a step held, no padlock */
static void t_lock_popup(void)
{
    static uint8_t a[MR_SCREEN_BYTES], b[MR_SCREEN_BYTES], c[MR_SCREEN_BYTES];
    const int clip = SEQ_CLIP(12, 0);
    int pg, k, md, n = 0, bad = 0, nlk = 0;
    kit_view();
    sound(1, "Snare", 48000, NULL, 0);
    hit(kp(1)), tap_step(0), rest();
    for (md = 0; md < RFM_MOVE_KIT_M_N; md++)
        for (pg = 0; pg < RFM_MOVE_KIT_PG_N; pg++) {
            if (pg == RFM_MOVE_KIT_PG_TRIG)
                continue;
            M.kit.pad[1].v[RFM_MOVE_KIT_P_MODE] = (int16_t)md;
            M.kit.pad[1].v[RFM_MOVE_KIT_P_WARP] = (int16_t)(md % RFM_MOVE_KIT_W_N);
            M.kit.page = (uint8_t)pg;
            for (k = 0; k < 8; k++) {
                const int nl = M.seq.nlocks;
                touch(k, 1);
                if (M.kit.kpop != k) {
                    touch(k, 0), rest();
                    continue;
                }
                screen(), memcpy(a, SCR, sizeof a);
                touch(k, 0), rest();
                step(0, 1), touch(k, 1);
                if (M.kit.kpop == k && M.kit.lpop) {
                    screen(), memcpy(b, SCR, sizeof b);
                    knob(k, 4), knob(k, 124);              /* (a lock, there and back) */
                    screen(), memcpy(c, SCR, sizeof c);
                    n++;
                    if ((scr_padlock(a) || scr_padlock(b) || (M.seq.nlocks > nl && !scr_padlock(c)) || px(0, 0) ||
                         !memcmp(a, b, sizeof a)) && bad++ < 4)
                        CHECKF(0, "mode %d page %d knob %d: plain %d, held %d, locked %d (locks %d -> %d), tag %d, held = plain %d",
                               md, pg, k, scr_padlock(a), scr_padlock(b), scr_padlock(c), nl, M.seq.nlocks, px(0, 0),
                               !memcmp(a, b, sizeof a));
                    nlk += M.seq.nlocks > nl;
                }
                touch(k, 0), step(0, 0), rest();
                if (M.seq.nlocks != nl)
                    press(MR_CC_UNDO), rest();
            }
        }
    CHECKF(n >= 40 && nlk >= 40 && !bad, "%d compared, %d locked, %d bad", n, nlk, bad);
    /* Pad Mix's Send 1 (its big value): the hollow one's pixels a part of the locked one's at the same value */
    M.kit.pad[1].v[RFM_MOVE_KIT_P_MODE] = 0, M.kit.pad[1].v[RFM_MOVE_KIT_P_WARP] = 0;
    M.kit.page = RFM_MOVE_KIT_PG_PADMIX;
    step(0, 1), touch(4, 1), screen(), memcpy(b, SCR, sizeof b);
    knob(4, 1), knob(4, 127), screen(), memcpy(c, SCR, sizeof c);
    CHECK(!scr_padlock(b) && scr_padlock(c) && scr_subset(b, c, MR_SCREEN_BYTES) && memcmp(b, c, sizeof b));
    touch(4, 0), step(0, 0), rest(), press(MR_CC_UNDO), rest();
    /* a locked cutoff: the pad drawn at the lock's value, the padlock; cleared (step held + knob + Delete): hollow */
    M.kit.pad[1].v[RFM_MOVE_KIT_P_MODE] = 0, M.kit.pad[1].v[RFM_MOVE_KIT_P_WARP] = 0;
    M.kit.page = RFM_MOVE_KIT_PG_FILTER;
    {
        rfm_move_act_t x;
        int c0 = M.kit.pad[1].v[RFM_MOVE_KIT_P_CUTOFF], lk;
        step(0, 1), touch(0, 1), knob(0, -40);
        screen();
        CHECK(scr_padlock(SCR));
        press(MR_CC_DELETE), screen();
        CHECK(!scr_padlock(SCR) && !px(0, 0));          /* (cleared: hollow at once, no "Lock Cleared") */
        press(MR_CC_UNDO), screen();
        touch(0, 0), step(0, 0), rest();
        CHECK(M.kit.pad[1].v[RFM_MOVE_KIT_P_CUTOFF] == c0 && rfmv_seq_find(&M, clip, 0, 37) >= 0);
        M.seq.tr[12].active = 0;
        memset(&x, 0, sizeof x);
        CHECK(play_ons(1, 200, &x) >= 1);
        lk = x.kp[RFM_MOVE_KIT_P_CUTOFF];
        CHECKF((x.kp_mask >> RFM_MOVE_KIT_P_CUTOFF & 1) && lk < c0, "cutoff lock %d (pad %d)", lk, c0);
        rest(), touch(0, 1), screen(), touch(0, 0);
        CHECK(!scr_padlock(SCR));                       /* (no step held: plain) */
    }
}

/* 48b. (2026-11-26, the owner's: a pad page's clears work on that pad, a kit page's on all 16) Delete + Play on Pad
 * Sample: the chosen pad's hits, ghosts and locks, the other pads of its track kept; on Sample Kit: all four kit tracks'
 * clips; Delete + a knob on Sample Kit or Kit Mix (both orders): every pad's locks, the hits kept; each one undo step */
static void t_scoped_clear(void)
{
    static rfm_move_note_t n0[RFM_MOVE_SEQ_NOTES];
    static rfm_move_lock_t l0[RFM_MOVE_SEQ_LOCKS];
    const int c12 = SEQ_CLIP(12, 0), c13 = SEQ_CLIP(13, 0);
    int nn, nl, i, other = 0;
    kit_view();
    sound(1, "Snare", 4800, NULL, 0), sound(2, "Hat", 4800, NULL, 0), sound(4, "Kick", 4800, NULL, 0);
    hit(kp(2)), tap_step(1), rest();
    hit(kp(4)), tap_step(2), rest();
    hit(kp(1)), tap_step(0), rest();
    M.kit.page = RFM_MOVE_KIT_PG_PADMIX;
    step(0, 1), touch(4, 1), knob(4, 20), touch(4, 0), step(0, 0), rest();   /* (pad 2: a Send 1 lock on its hit) */
    step(3, 1), touch(4, 1), knob(4, 20), touch(4, 0), step(3, 0), rest();   /* (.. and a ghost on step 4) */
    hit(kp(2)), rest();
    step(1, 1), touch(4, 1), knob(4, 10), touch(4, 0), step(1, 0), rest();   /* (pad 3: a lock on its hit) */
    hit(kp(1)), rest();
    nn = M.seq.nnotes, nl = M.seq.nlocks;
    memcpy(n0, M.seq.notes, sizeof n0), memcpy(l0, M.seq.locks, sizeof l0);
    CHECK(nn == 3 && nl >= 6);
    /* a pad page: pad 2's only */
    M.kit.page = RFM_MOVE_KIT_PG_SAMPLE;
    button(MR_CC_DELETE, 1), press(MR_CC_PLAY), button(MR_CC_DELETE, 0), rest();
    CHECKF(rfmv_seq_find(&M, c12, 0, 37) < 0 && rfmv_seq_find(&M, c12, 1, 38) >= 0 && rfmv_seq_find(&M, c13, 2, 40) >= 0 &&
           M.seq.nnotes == 2, "notes %d", M.seq.nnotes);
    for (i = 0; i < M.seq.nlocks; i++)
        other += M.seq.locks[i].clip == c12 && (M.seq.locks[i].step == 0 || M.seq.locks[i].step == 3);
    CHECKF(!other && M.seq.nlocks == 2 && !M.seq.playing, "locks %d (pad 2's left %d)", M.seq.nlocks, other);
    CHECKF(!strcmp(M.kit.msg, "Cleared") && !strcmp(M.kit.msg2, "Pad 2"), "msg %s %s", M.kit.msg, M.kit.msg2);
    press(MR_CC_UNDO), rest();
    CHECK(M.seq.nnotes == nn && M.seq.nlocks == nl && !memcmp(M.seq.notes, n0, sizeof n0[0] * (size_t)nn) &&
          !memcmp(M.seq.locks, l0, sizeof l0[0] * (size_t)nl));
    /* a kit page: every pad */
    M.kit.page = RFM_MOVE_KIT_PG_KIT;
    button(MR_CC_DELETE, 1), press(MR_CC_PLAY), button(MR_CC_DELETE, 0), rest();
    CHECKF(M.seq.nnotes == 0 && M.seq.nlocks == 0 && !strcmp(M.kit.msg, "Cleared") && !strcmp(M.kit.msg2, "Kit"),
           "notes %d locks %d msg %s %s", M.seq.nnotes, M.seq.nlocks, M.kit.msg, M.kit.msg2);
    press(MR_CC_UNDO), rest();
    CHECK(M.seq.nnotes == nn && M.seq.nlocks == nl && !memcmp(M.seq.locks, l0, sizeof l0[0] * (size_t)nl));
    /* Delete held + a knob on Kit Mix: every pad's locks, the hits kept */
    M.kit.page = RFM_MOVE_KIT_PG_KITMIX;
    button(MR_CC_DELETE, 1), touch(0, 1), touch(0, 0), button(MR_CC_DELETE, 0), rest();
    CHECKF(M.seq.nlocks == 0 && M.seq.nnotes == nn && !strcmp(M.kit.msg, "Cleared All") && !strcmp(M.kit.msg2, "Locks"),
           "locks %d notes %d msg %s %s", M.seq.nlocks, M.seq.nnotes, M.kit.msg, M.kit.msg2);
    press(MR_CC_UNDO), rest();
    CHECK(M.seq.nlocks == nl && !memcmp(M.seq.locks, l0, sizeof l0[0] * (size_t)nl));
    /* .. a knob touched, then Delete, on Sample Kit */
    M.kit.page = RFM_MOVE_KIT_PG_KIT;
    touch(1, 1), press(MR_CC_DELETE), touch(1, 0), rest();
    CHECKF(M.seq.nlocks == 0 && M.seq.nnotes == nn, "locks %d", M.seq.nlocks);
    press(MR_CC_UNDO), rest();
    CHECK(M.seq.nlocks == nl);
    /* a pad page's Delete + a knob: still the chosen pad's only (pad 3's lock kept) */
    M.kit.page = RFM_MOVE_KIT_PG_PADMIX;
    button(MR_CC_DELETE, 1), touch(4, 1), touch(4, 0), button(MR_CC_DELETE, 0), rest();
    CHECKF(M.seq.nlocks == 2, "locks %d", M.seq.nlocks);
    press(MR_CC_UNDO), rest();
    CHECK(M.seq.nlocks == nl);
}
/* 49. (2026-11-26, the owner's: "the up and down arrows should adjust the pitch range since a sample can go +- 24
 * semitones") Up / Down an octave on the right half (lit where it moves; Chop: dark, dead), "Pitch -24"; a pitch under
 * the pad's (kw -32) heard, entered at a step, recorded, played back and kept through a version 16 save; past +24 dead */
static int kw_note(int clip, int note)
{
    int i;
    for (i = 0; i < M.seq.nnotes; i++)
        if (M.seq.notes[i].clip == clip && M.seq.notes[i].note == note && M.seq.notes[i].kw == -32)
            return i;
    return -1;
}
static void t_pitch_range(void)
{
    static uint8_t b[RFM_MOVE_PROJECT_MAX];
    uint8_t w[RFM_MOVE_NLEDS][3];
    const int clip = SEQ_CLIP(12, 0), lo = rfm_move_kit_note(1, -21);
    rfm_move_act_t a;
    int n, i, rec_ok = 0;
    kit_view();
    sound(1, "Snare", 4800, NULL, 0);
    hit(kp(1)), rest();
    CHECK(M.kit.rh == RFM_MOVE_KIT_RH_PITCHES && M.kit.rh_oct == 0);
    leds(w);
    CHECK(lit(w[L_WHITE0 + W_UP]) && lit(w[L_WHITE0 + W_DOWN]));
    press(MR_CC_DOWN), press(MR_CC_DOWN);
    CHECKF(M.kit.rh_oct == -2 && !strcmp(M.kit.msg, "Pitch") && !strcmp(M.kit.msg2, "-24"), "oct %d %s %s", M.kit.rh_oct,
           M.kit.msg, M.kit.msg2);
    press(MR_CC_DOWN);
    leds(w);
    CHECK(M.kit.rh_oct == -2 && !lit(w[L_WHITE0 + W_DOWN]) && lit(w[L_WHITE0 + W_UP]));
    CHECK(rfmv_kit_right_semis(&M, 0) == -24 && rfmv_kit_right_semis(&M, 15) == -9 && lo >= 0);
    clear(), pad(rp(3), 100), pad(rp(3), 0);            /* (heard at -21) */
    CHECKF(last(RFM_MOVE_ACT_KIT_NOTE) && count(RFM_MOVE_ACT_KIT_NOTE) == 2 && A[last_i(RFM_MOVE_ACT_KIT_NOTE) - 1].value == -21,
           "%d notes", count(RFM_MOVE_ACT_KIT_NOTE));
    step(0, 1), pad(rp(3), 100), pad(rp(3), 0), step(0, 0), rest();   /* (entered at step 1) */
    CHECKF(kw_note(clip, lo) >= 0, "no note %d kw -32", lo);
    M.seq.tr[12].active = 0;
    memset(&a, 0, sizeof a);
    CHECK(play_ons(1, 200, &a) >= 1 && a.value == -21);
    /* saved (version 16) and back: the pitch, the octave */
    n = rfm_move_project_save(&M, b, sizeof b);
    CHECK(n > 0 && b[4] == RFM_MOVE_PROJECT_VERSION && rfm_move_project_check(b, (uint32_t)n) == 0);
    rfm_move_init(&M2, NULL, on_act, NULL);
    rfm_move_fw_caps(&M2, KCAPS);
    CHECK(rfm_move_project_load(&M2, 1, b, (uint32_t)n) == 0 && M2.kit.rh_oct == -2);
    {
        const int j = rfmv_seq_find(&M2, clip, 0, lo);
        CHECK(j >= 0 && M2.seq.notes[j].kw == -32);
    }
    /* Up three: +12 (Pitches 12..27: past +24 dead) */
    press(MR_CC_UP), press(MR_CC_UP), press(MR_CC_UP);
    leds(w);
    CHECK(M.kit.rh_oct == 1 && !lit(w[L_WHITE0 + W_UP]) && !strcmp(M.kit.msg2, "+12"));
    CHECK(rfmv_kit_right_semis(&M, 12) == 24 && rfmv_kit_right_semis(&M, 13) == RFMV_KIT_DEAD);
    clear(), pad(rp(13), 100), pad(rp(13), 0);
    CHECK(!count(RFM_MOVE_ACT_KIT_NOTE));
    /* Record, playing, an octave down: the note lands with its kw */
    press(MR_CC_DOWN), press(MR_CC_DOWN);
    CHECK(M.kit.rh_oct == -1);
    rfm_move_seq_play(&M, 1), press(MR_CC_REC);
    adv_ms(10);
    clear(), pad(rp(2), 100), adv_ms(50), pad(rp(2), 0);   /* (-10) */
    adv_ms(100);
    press(MR_CC_REC), rfm_move_seq_play(&M, 0);
    for (i = 0; i < M.seq.nnotes; i++)
        rec_ok |= M.seq.notes[i].clip == clip && M.seq.notes[i].note == rfm_move_kit_note(1, -10) && M.seq.notes[i].kw == -32;
    CHECK(rec_ok);
    /* Chop: Up / Down dark and dead */
    M.kit.rh = RFM_MOVE_KIT_RH_CHOP;
    press(MR_CC_UP);
    leds(w);
    CHECK(M.kit.rh_oct == -1 && !lit(w[L_WHITE0 + W_UP]) && !lit(w[L_WHITE0 + W_DOWN]));
    M.kit.rh = RFM_MOVE_KIT_RH_PITCHES, M.kit.rh_oct = 0;
}

/* 44. versions 12, 13 and 14 into 15: Toggle as Loop, Stretch N as Warp Repitch Sync N, In Key as In Key 4ths, Off as
 * Pitches, the new settings at their defaults */
static int old_blob(const uint8_t *b, int n, int ver, uint8_t *c)
{
    const int np = ver >= 14 ? RFM_MOVE_KIT_P_N_V14 : RFM_MOVE_KIT_P_N_V12;
    const int kb = 16 + 24 + RFM_MOVE_KIT_PADS * (RFM_MOVE_KIT_PATH + 2 * np), base = n - 4 - RFM_MOVE_KIT_BYTES_V15;
    const uint8_t *x = b + base;
    uint8_t *o = c + base;
    int k, i, no;
    uint32_t h;
    memcpy(c, b, (size_t)base);
    memset(o, 0, (size_t)kb);
    memcpy(o, x, 16 + 24);
    o[10] = o[11] = 0;                                  /* (no kit bus before 15) */
    if (ver < 13)
        o[4 + RFM_MOVE_KIT_MIX_N] = 0;                  /* (no Trim before 13) */
    o += 40, x += 40;
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++, o += RFM_MOVE_KIT_PATH + 2 * np, x += RFM_MOVE_KIT_PATH + 2 * RFM_MOVE_KIT_P_N_V15)
        memcpy(o, x, (size_t)(RFM_MOVE_KIT_PATH + 2 * np));
    no = base + kb + 4;
    c[4] = (uint8_t)ver;
    for (h = 2166136261u, i = 0; i < no - 4; i++)
        h = (h ^ c[i]) * 16777619u;
    for (i = 0; i < 4; i++)
        c[no - 4 + i] = (uint8_t)(h >> (8 * i));
    return no;
}
static void t_project_old(void)
{
    static uint8_t b[RFM_MOVE_PROJECT_MAX], c[RFM_MOVE_PROJECT_MAX];
    static rfm_move_t D0;
    int n, ver, i;
    rfm_move_init(&D0, NULL, NULL, NULL);
    kit_view();
    sound(3, "Rim", 4800, NULL, 0);
    sound(5, "Pad", 9600, NULL, 0);
    M.kit.pad[3].v[RFM_MOVE_KIT_P_MODE] = 3;            /* (round A's Toggle) */
    M.kit.pad[3].v[RFM_MOVE_KIT_P_STRETCH] = 12, M.kit.pad[3].v[RFM_MOVE_KIT_P_SEND2] = 40;
    M.kit.pad[3].v[RFM_MOVE_KIT_P_MORPH] = 70, M.kit.pad[3].v[RFM_MOVE_KIT_P_SPEED] = 200;   /* (15's: dropped) */
    M.kit.pad[5].v[RFM_MOVE_KIT_P_MODE] = RFM_MOVE_KIT_M_MOMENTARY;
    for (ver = 12; ver <= 14; ver++) {
        int rh;
        for (rh = 1; rh <= 2; rh++) {
            int no, bad = -1;
            M.kit.rh = (uint8_t)rh;                     /* (an older one's 1 In Key, 2 Off) */
            n = (int)proj_v15(b, (uint32_t)rfm_move_project_save(&M, b, sizeof b));
            no = old_blob(b, n, ver, c);
            CHECKF(rfm_move_project_check(c, (uint32_t)no) == 0, "version %d check %d", ver, rfm_move_project_check(c, (uint32_t)no));
            rfm_move_init(&M2, NULL, on_act, NULL);
            rfm_move_fw_caps(&M2, KCAPS);
            CHECKF(rfm_move_project_load(&M2, 1, c, (uint32_t)no) == 0, "version %d load", ver);
            CHECKF(M2.kit.rh == (rh == 1 ? RFM_MOVE_KIT_RH_INKEY4 : RFM_MOVE_KIT_RH_PITCHES), "version %d rh %d -> %d", ver, rh,
                   M2.kit.rh);
            CHECK(M2.kit.pad[3].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_LOOP &&
                  M2.kit.pad[3].v[RFM_MOVE_KIT_P_WARP] == RFM_MOVE_KIT_W_REPITCH &&
                  M2.kit.pad[3].v[RFM_MOVE_KIT_P_STRETCH] == 12 && M2.kit.pad[3].v[RFM_MOVE_KIT_P_SEND2] == 40 &&
                  strstr(M2.kit.pad[3].path, "Rim"));
            CHECK(M2.kit.pad[5].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_MOMENTARY &&
                  M2.kit.pad[5].v[RFM_MOVE_KIT_P_WARP] == RFM_MOVE_KIT_W_OFF);
            for (i = ver >= 14 ? RFM_MOVE_KIT_P_N_V14 : RFM_MOVE_KIT_P_N_V12; i < RFM_MOVE_KIT_P_N && bad < 0; i++)
                if (i != RFM_MOVE_KIT_P_WARP && M2.kit.pad[3].v[i] != D0.kit.pad[3].v[i])
                    bad = i;
            CHECKF(bad < 0, "version %d: setting %d %d, not its default %d", ver, bad, bad < 0 ? 0 : M2.kit.pad[3].v[bad],
                   bad < 0 ? 0 : D0.kit.pad[3].v[bad]);
            CHECK(M2.kit.bus[RFM_MOVE_KIT_BUS_CUTOFF] == 100 && M2.kit.bus[RFM_MOVE_KIT_BUS_RESO] == 0);
        }
    }
}

/* 45. Chop: a Chop slot held + a step's click enters the chosen pad's hit with that chop; the sequencer plays it back
 * with the chop on its KIT_NOTE; Record while playing keeps a played chop on its step */
static void t_chop_seq(void)
{
    const int clip = SEQ_CLIP(12, 0);
    rfm_move_act_t a;
    int i, rec_ok = 0;
    kit_view();
    sound(1, "Loop", 48000, NULL, 0);
    hit(kp(1));
    M.kit.rh = RFM_MOVE_KIT_RH_CHOP;
    pad(rp(13), 100), tap_step(0), pad(rp(13), 0);      /* (rp 13: the top row's second, chop 1) */
    CHECK(rfmv_seq_find(&M, clip, 0, 37) >= 0 &&
          rfmv_seq_lock_get(&M, clip, 0, RFM_MOVE_KIT_LOCK0 + RFM_MOVE_KIT_LK_CHOP * 4 + 1) == 1 &&
          rfmv_seq_lock_get(&M, clip, 0, RFM_MOVE_KIT_LOCK0 + RFM_MOVE_KIT_LK_CHOPE * 4 + 1) == 1);
    M.seq.tr[12].active = 0;
    memset(&a, 0, sizeof a);
    CHECK(play_ons(1, 200, &a) >= 1);
    CHECKF((a.klk_mask >> RFM_MOVE_KIT_LK_CHOP & 1) && a.klk[RFM_MOVE_KIT_LK_CHOP] == 1 && a.klk[RFM_MOVE_KIT_LK_CHOPE] == 1,
           "klk %x chop %d..%d", a.klk_mask, a.klk[RFM_MOVE_KIT_LK_CHOP], a.klk[RFM_MOVE_KIT_LK_CHOPE]);
    /* Record, playing: chop 10 (rp 6: the second row from the bottom, its third) played lands with its chop */
    rfmv_seq_undo(&M);
    CHECK(rfmv_seq_find(&M, clip, 0, 37) < 0);
    rfm_move_seq_play(&M, 1), press(MR_CC_REC);
    adv_ms(10);
    CHECK(M.seq.rec);
    clear(), pad(rp(6), 100), adv_ms(50), pad(rp(6), 0);
    adv_ms(100);
    press(MR_CC_REC), rfm_move_seq_play(&M, 0);
    for (i = 0; i < RFM_MOVE_SEQ_PAGE * 4; i++)
        if (rfmv_seq_find(&M, clip, i, 37) >= 0 &&
            rfmv_seq_lock_get(&M, clip, i, RFM_MOVE_KIT_LOCK0 + RFM_MOVE_KIT_LK_CHOP * 4 + 1) == 10)
            rec_ok = 1;
    CHECK(rec_ok);
    M.kit.rh = RFM_MOVE_KIT_RH_PITCHES;
}

/* 46. a project saved and loaded keeps the pads' paths (a round C "@/" one too) and pages v2's settings: the engine, a
 * loop point, the warp, the morph, the kit bus */
static void t_project_v2(void)
{
    static uint8_t b[RFM_MOVE_PROJECT_MAX];
    int n;
    kit_view();
    sound(2, "Kick", 4800, NULL, 0);
    rfm_move_kit_loaded(&M, 7, RFM_MOVE_KIT_OK, "Take 1", RFM_MOVE_KIT_OWN "takes/t1.wav", 9600, 48000, 0, NULL);
    CHECK(!strcmp(M.kit.pad[7].path, "@/takes/t1.wav"));
    M.kit.pad[7].v[RFM_MOVE_KIT_P_MODE] = RFM_MOVE_KIT_M_LOOP, M.kit.pad[7].v[RFM_MOVE_KIT_P_LOOPS] = 4000;
    M.kit.pad[7].v[RFM_MOVE_KIT_P_WARP] = RFM_MOVE_KIT_W_GRAINS, M.kit.pad[7].v[RFM_MOVE_KIT_P_MORPH] = 60;
    M.kit.pad[7].v[RFM_MOVE_KIT_P_FENV] = -30, M.kit.pad[2].v[RFM_MOVE_KIT_P_MODE] = RFM_MOVE_KIT_M_GRANULAR;
    M.kit.pad[2].v[RFM_MOVE_KIT_P_GSIZE] = 120;
    rfm_move_kit_set_bus(&M, 45, 20);
    n = rfm_move_project_save(&M, b, sizeof b);
    CHECK(n > 0 && b[4] == RFM_MOVE_PROJECT_VERSION && rfm_move_project_check(b, (uint32_t)n) == 0);
    rfm_move_init(&M2, NULL, on_act, NULL);
    rfm_move_fw_caps(&M2, KCAPS);
    CHECK(rfm_move_project_load(&M2, 1, b, (uint32_t)n) == 0);
    CHECK(!strcmp(M2.kit.pad[7].path, "@/takes/t1.wav") && !strcmp(M2.kit.pad[2].path, M.kit.pad[2].path));
    CHECK(M2.kit.pad[7].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_LOOP && M2.kit.pad[7].v[RFM_MOVE_KIT_P_LOOPS] == 4000 &&
          M2.kit.pad[7].v[RFM_MOVE_KIT_P_WARP] == RFM_MOVE_KIT_W_GRAINS && M2.kit.pad[7].v[RFM_MOVE_KIT_P_MORPH] == 60 &&
          M2.kit.pad[7].v[RFM_MOVE_KIT_P_FENV] == -30);
    CHECK(M2.kit.pad[2].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_GRANULAR && M2.kit.pad[2].v[RFM_MOVE_KIT_P_GSIZE] == 120);
    CHECK(rfm_move_kit_bus(&M2, RFM_MOVE_KIT_BUS_CUTOFF) == 45 && rfm_move_kit_bus(&M2, RFM_MOVE_KIT_BUS_RESO) == 20);
}

int main(void)
{
    t_encoding();
    t_cap_off();
    t_round();
    t_pads();
    t_steps();
    t_seq();
    t_knobs();
    t_rows();
    t_files();
    t_delete();
    t_rec();
    t_to_kit();
    t_route();
    t_project();
    t_settings();
    t_layers();
    t_mock();
    t_stretch();
    t_sends();
    t_mixer();
    t_folder();
    t_note_row();
    t_kits();
    t_smp_kit();
    t_project12();
    printf("mock boards (3b): stretch, send, folder, saved, sampler\n");
    t_tools3c();
    t_merge();
    t_slice();
    t_page3c();
    t_mock3c();
    t_zoom();
    t_sens();
    t_toggle();
    t_trim();
    t_pages_v2();
    t_root();
    t_gated();
    t_pitch_step();
    t_xlock();
    t_bits();
    t_loop_pct();
    t_scan();
    t_kit_layout();
    t_kit_level();
    t_pad_level();
    t_level_popups();
    t_step_preview();
    t_ghost();
    t_auto_clear();
    t_auto_clear_cutoff();
    t_lock_popup();
    t_scoped_clear();
    t_pitch_range();
    t_project_old();
    t_chop_seq();
    t_project_v2();
    return check_done("move_kit");
}
