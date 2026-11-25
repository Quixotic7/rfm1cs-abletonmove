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
#include "move_int.h"
#include "moveremote/client.h"
#include "rfm1cs/move.h"
#include "move_fwb_data.h"
#include "move_kit_ref.h"

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
        if (a->kind == RFM_MOVE_ACT_KIT_LOAD && a->data)
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
    rest(), clear();
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
    CHECK(count(RFM_MOVE_ACT_KIT_LOAD) == 16 && count(RFM_MOVE_ACT_KIT_SET) == 16 * RFM_MOVE_KIT_P_N);   /* (the
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
    M.kit.rh = RFM_MOVE_KIT_RH_INKEY, M.s.scale = RFM_MOVE_SCALE_MINOR;
    CHECK(rfmv_kit_right_semis(&M, 0) == 0 && rfmv_kit_right_semis(&M, 1) == 2 && rfmv_kit_right_semis(&M, 3) == 5 &&
          rfmv_kit_right_semis(&M, 4) == 5 && rfmv_kit_right_semis(&M, 15) == 12 + 8);
    clear(), hit(rp(4));
    CHECK(count(RFM_MOVE_ACT_KIT_NOTE) == 2 && A[0].value == 5);
    M.kit.rh = RFM_MOVE_KIT_RH_OFF;
    clear(), hit(rp(4));
    CHECK(!count(RFM_MOVE_ACT_KIT_NOTE));
    leds(w);
    CHECK(!lit(w[L_PAD0 + rp(4)]) && !lit(w[L_PAD0 + rp(0)]));
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

/* 7. the knobs: the chosen pad's (1 Pitch, 2 Start, 3 Stretch, 4 Play, 5 Attack, 6 Release, 7 Level, 8 Send 1) */
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
    CHECK(lit_n == 8 && is_rgb(w[L_RING0 + 2], 149, 35, 93) && is_rgb(w[L_RING0 + 0], 149, 35, 93));   /* (3b: all) */
    clear();
    touch(0, 1), knob(0, 6), touch(0, 0);
    CHECK((a = last(RFM_MOVE_ACT_KIT_SET)) && a->index == 4 && a->param == RFM_MOVE_KIT_P_PITCH && a->value == 300);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 300 && M.seq.dirty);
    button(MR_CC_SHIFT, 1), knob(0, 5), button(MR_CC_SHIFT, 0);   /* (Shift: cents) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 305);
    knob(0, 2);                                         /* (a fine pitch goes to the next whole semitone first) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 400);
    clear(), touch(2, 1), knob(2, 6), touch(2, 0), touch(7, 1), knob(7, 10), touch(7, 0);   /* (3b: 3 Stretch, 8 Send 1) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_STRETCH] == 2 && M.kit.pad[4].v[RFM_MOVE_KIT_P_SEND1] == 10);
    CHECK((a = last(RFM_MOVE_ACT_KIT_SET)) && a->param == RFM_MOVE_KIT_P_SEND1 && a->value == 10);
    clear(), knob(0, 6);                                /* (stretched: Pitch ignored, kept) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 400 && !kit_acts());
    knob(2, -9), knob(7, -10);
    CHECK(!M.kit.pad[4].v[RFM_MOVE_KIT_P_STRETCH] && !M.kit.pad[4].v[RFM_MOVE_KIT_P_SEND1]);
    knob(3, 5);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_MOMENTARY);   /* (a list: 5 ticks a step) */
    knob(4, 2), knob(5, 4);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_ATTACK] == 1 && M.kit.pad[4].v[RFM_MOVE_KIT_P_RELEASE] == 2);
    knob(6, -6);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_LEVEL] == -3);
    knob(1, 10);                                        /* (Start: never past the end) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_START] == 640);
    /* a knob touched: its popup (the name, the value), over the rows */
    touch(0, 1);
    CHECK(M.kit.kpop == 0);
    screen();
    touch(0, 0), rest();
    CHECK(rfmv_now_ms(&M) - M.kit.kpop_ms >= RFM_MOVE_POPUP_MS);   /* (the popup: a second after the release) */
}

/* 8. the jog: one row at rest, three while it turns, the tabs above; a click: the popup (the jog its value), Back */
static void t_rows(void)
{
    kit_view();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    CHECK(M.kit.tab == 0 && M.kit.row == 0);
    clear(), jog(1);
    CHECK(M.kit.row == 1 && M.kit.turned);
    CHECKF(strstr(said, "Pitch: 0") != NULL, "said %s", said);   /* (the screen reader: the row) */
    jog_click();
    CHECK(M.kit.edit);
    jog(2);
    CHECK(M.kit.pad[0].v[RFM_MOVE_KIT_P_PITCH] == 200);
    back();
    CHECK(!M.kit.edit && rfmv_kit_active(&M));
    back();                                             /* (the rows: nothing more; never out of the view) */
    CHECK(rfmv_kit_active(&M) && !M.kit.on_tabs);
    jog(-5);
    CHECK(M.kit.on_tabs);
    jog_click(), jog(2), jog_click();
    CHECK(M.kit.tab == 2 && !M.kit.picking && M.kit.on_tabs);
    jog(1);
    CHECK(!M.kit.on_tabs && M.kit.place == 0);
    /* the Kit tab: the right half's layout (3b: its third row) */
    jog(-1), jog_click(), jog(-1), jog_click(), jog(1), jog(2), jog_click();
    CHECK(M.kit.tab == 1 && M.kit.edit);
    jog(1);
    CHECK(M.kit.rh == RFM_MOVE_KIT_RH_INKEY);
    back(), back();
    CHECK(M.kit.on_tabs);
    /* the Sound row's click: Play, Delete */
    jog_click(), jog(-1), jog_click(), jog(1);
    CHECK(M.kit.tab == 0 && M.kit.row == 0);
    sound(0, "Kick", 4800, NULL, 0);
    jog_click();
    CHECK(M.kit.tools && M.kit.ti == 0);
    clear(), jog_click();
    CHECK(last(RFM_MOVE_ACT_KIT_NOTE) && last(RFM_MOVE_ACT_KIT_NOTE)->down);
    jog(4), clear(), jog_click();                       /* (3c: Play, Trim, Normalize, Reverse, Delete) */
    CHECK(!M.kit.tools && !M.kit.pad[0].path[0] && last(RFM_MOVE_ACT_KIT_LOAD) && !last(RFM_MOVE_ACT_KIT_LOAD)->take);
    clear(), press(MR_CC_UNDO);                         /* (the pad back) */
    CHECK(M.kit.pad[0].path[0] && last_i(RFM_MOVE_ACT_KIT_LOAD) >= 0 && strstr(D[last_i(RFM_MOVE_ACT_KIT_LOAD)], "Kick.wav"));
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
    jog(-1), jog_click(), jog(2), jog_click(), jog(1);  /* (Files, the places) */
    CHECK(M.kit.tab == 2 && !M.kit.on_tabs && M.kit.place == 0 && M.kit.psel == 0);
    jog(1), clear(), jog_click();                       /* Samples */
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_SAMPLES && (a = last(RFM_MOVE_ACT_KIT_ROWS)) && a->value == 2 && a->take == 0 &&
          a->from == 0 && a->count == 3);
    memset(r, 0, sizeof r);
    r[0].id = 7, r[0].kind = RFM_MOVE_KIT_ROW_DIR, snprintf(r[0].name, sizeof r[0].name, "Drums");
    r[1].id = 8, r[1].kind = RFM_MOVE_KIT_ROW_FILE, snprintf(r[1].name, sizeof r[1].name, "Clap");
    r[2].id = 9, r[2].kind = RFM_MOVE_KIT_ROW_FILE, snprintf(r[2].name, sizeof r[2].name, "Hat");
    CHECK(rfm_move_kit_rows(&M, 2, 0, 3, 0, 3, r) == 1);
    clear(), jog(1);                                    /* (a sound: heard as the jog lands on it) */
    CHECK((a = last(RFM_MOVE_ACT_KIT_PREVIEW)) && a->down && a->take == 8 && a->value == 2);
    clear(), jog(1);
    CHECK(count(RFM_MOVE_ACT_KIT_PREVIEW) == 1 && last(RFM_MOVE_ACT_KIT_PREVIEW)->take == 9);
    clear(), jog_click();                               /* onto the chosen pad (7) */
    CHECK((a = last(RFM_MOVE_ACT_KIT_LOAD)) && a->index == 6 && a->take == 9 && a->value == 2 && !D[last_i(RFM_MOVE_ACT_KIT_LOAD)][0]);
    CHECK(M.kit.pad[6].st == RFM_MOVE_KIT_LOADING);
    clear(), hit(kp(6));                                /* (a pad while browsing: it plays, it is the target) */
    CHECKF(count(RFM_MOVE_ACT_KIT_NOTE) == 2 && M.kit.cur == 6, "notes %d", count(RFM_MOVE_ACT_KIT_NOTE));
    for (i = 0; i < 64; i++)
        pk[i] = (uint8_t)(255 - i * 3);
    M.kit.pad[6].v[RFM_MOVE_KIT_P_START] = 500;
    clear();
    CHECK(rfm_move_kit_loaded(&M, 6, RFM_MOVE_KIT_OK, "Hat", "/data/UserData/UserLibrary/Samples/Hat.wav", 9600, 48000, 64, pk) >= 0);
    CHECK(!strcmp(M.kit.pad[6].path, "/data/UserData/UserLibrary/Samples/Hat.wav") && M.kit.pad[6].st == RFM_MOVE_KIT_OK &&
          M.kit.pad[6].npk == 64 && M.kit.pad[6].v[RFM_MOVE_KIT_P_START] == 0);   /* (a new sound: its whole region) */
    CHECK(rfm_move_kit_loaded(&M, 5, RFM_MOVE_KIT_TOO_LONG, "Big", NULL, 0, 0, 0, NULL) >= 0 &&
          M.kit.pad[5].st == RFM_MOVE_KIT_TOO_LONG && !strcmp(M.kit.msg, "Too Long"));
    M.kit.pad[6].st = RFM_MOVE_KIT_LOADING, M.kit.pad[6].fresh = 1;   /* (another sound asked for pad 7, refused: the
                                                         * pad keeps Hat) */
    CHECK(rfm_move_kit_loaded(&M, 6, RFM_MOVE_KIT_TOO_LONG, "Big", NULL, 0, 0, 0, NULL) >= 0 &&
          M.kit.pad[6].st == RFM_MOVE_KIT_OK && M.kit.pad[6].frames == 9600 && strstr(M.kit.pad[6].path, "Hat.wav"));
    /* a folder: entered, its rows asked; Back: up, the places, the tabs */
    rest();
    jog(-2), clear(), jog_click();
    CHECK(M.kit.depth == 1 && M.kit.folder[1] == 7 && (a = last(RFM_MOVE_ACT_KIT_ROWS)) && a->take == 7);
    CHECK(rfm_move_kit_rows(&M, 2, 7, 0, 0, 0, NULL) == 1);
    screen();
    dump("empty_folder");
    back();
    CHECK(M.kit.depth == 0);
    back();
    CHECK(M.kit.place == 0 && M.kit.psel == 1);
    back();
    CHECK(M.kit.on_tabs && M.kit.tab == 2);
    back();
    CHECK(M.kit.on_tabs && rfmv_kit_active(&M));
    /* Takes: every take, the newest first (the host's) */
    jog(1), clear(), jog_click();
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_TAKES && (a = last(RFM_MOVE_ACT_KIT_ROWS)) && a->value == 1);
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
    CHECK(M.kit.row == 0);
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
    CHECK(n == RFM_MOVE_PROJECT_FIXED + 7 && b[4] == 12 && rfm_move_project_check(b, (uint32_t)n) == 0);
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
    CHECK(loads == 1 && count(RFM_MOVE_ACT_KIT_SET) == 16 * RFM_MOVE_KIT_P_N);
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
    n10 = n - RFM_MOVE_KIT_BYTES;
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
    static const box_t ICONS_V = { 0, 24, 128, 40 };
    uint8_t pk[64];
    int out, n;
    kit_view();
    M.seq.bpm10 = 1200;
    /* A, rest: pad 5, Pitch +3 chosen, its wave (a hit), the steps it lasts (4) */
    n = mock_peaks(128, 11, 55u, 1, pk);
    hit(kp(4));
    sound(4, "Snare Rim", 28541, pk, n);
    M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] = 300;
    jog(1), rest();
    out = vs_ref("rest", NULL, 0);
    CHECKF(out == 0, "rest: %d differ", out);
    dump("rest");
    /* B, three: Sound, Pitch (chosen), Stretch (3b) */
    jog(1), jog(-1);
    out = vs_ref("three", NULL, 0);
    CHECKF(out == 0, "three: %d differ", out);
    dump("three");
    /* C, a knob touched: Pitch's popup and the steps it lasts */
    rest(), touch(0, 1);
    out = vs_ref("pitch", NULL, 0);
    CHECKF(out == 0, "pitch: %d differ", out);
    dump("pitch");
    touch(0, 0), rest();
    /* E, Play: Loop; F, Attack 20 ms (the envelopes' proportions are the pad's own: masked) */
    M.kit.pad[4].v[RFM_MOVE_KIT_P_MODE] = RFM_MOVE_KIT_M_LOOP, M.kit.pad[4].v[RFM_MOVE_KIT_P_ATTACK] = 20;
    M.kit.pad[4].v[RFM_MOVE_KIT_P_RELEASE] = 300;
    touch(3, 1);
    out = vs_ref("mode", &AHR_E, 1);
    CHECKF(out == 0, "mode: %d differ", out);
    dump("mode");
    touch(3, 0), rest(), touch(4, 1);
    out = vs_ref("attack", &AHR_F, 1);
    CHECKF(out == 0, "attack: %d differ", out);
    dump("attack");
    touch(4, 0), rest();
    /* G, Choke: group 2, pads 9 and 10 in it (pad 11 chosen) */
    sound(8, "Hat", 4800, NULL, 0), sound(9, "Hat2", 4800, NULL, 0), sound(10, "Hat3", 4800, NULL, 0);
    M.kit.pad[8].v[RFM_MOVE_KIT_P_CHOKE] = M.kit.pad[9].v[RFM_MOVE_KIT_P_CHOKE] = M.kit.pad[10].v[RFM_MOVE_KIT_P_CHOKE] = 2;
    button(MR_CC_SHIFT, 1), hit(kp(10)), button(MR_CC_SHIFT, 0);
    jog(8), jog_click(), rest();
    CHECK(M.kit.row == 9 && M.kit.edit);
    out = vs_ref("choke", NULL, 0);
    CHECKF(out == 0, "choke: %d differ", out);
    dump("choke");
    back();
    /* Q, the Kit tab (3b): Load, Save (chosen), the right half; the kit's name. S, In Key: C minor (its popup) */
    snprintf(M.kit.name, sizeof M.kit.name, "Kit 3");
    button(MR_CC_SHIFT, 1), hit(kp(4)), button(MR_CC_SHIFT, 0);
    M.s.root = 0, M.s.scale = RFM_MOVE_SCALE_MINOR;
    jog(-20), jog_click(), jog(1), jog_click(), jog(1), jog(1), rest();
    CHECK(M.kit.tab == 1 && !M.kit.on_tabs && M.kit.kitrow == 1);
    out = vs_ref("kittab", NULL, 0);
    CHECKF(out == 0, "kittab: %d differ", out);
    dump("kittab");
    M.kit.rh = RFM_MOVE_KIT_RH_INKEY;
    jog(1), jog_click();
    out = vs_ref("inkey", NULL, 0);
    CHECKF(out == 0, "inkey: %d differ", out);
    dump("inkey");
    back(), back();
    /* I, the places (3b: Kits the third); J, a folder: three sounds, their pictures */
    jog_click(), jog(1), jog_click(), jog(1), jog(1), rest();
    CHECK(M.kit.tab == 2 && M.kit.psel == 1);
    out = vs_ref("places", NULL, 0);
    CHECKF(out == 0, "places: %d differ", out);
    dump("places");
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
        out = vs_ref("files", NULL, 0);
        CHECKF(out == 0, "files: %d differ", out);
        dump("files");
    }
    M.kit.tab = 0, M.kit.on_tabs = 0, M.kit.picking = 0, M.kit.edit = 0, M.kit.row = 1;
    rest();
    /* V, the Sound row's tools (3a: Play, Delete; the wave as the mock's) */
    button(MR_CC_SHIFT, 1), hit(kp(4)), button(MR_CC_SHIFT, 0);
    n = mock_peaks(128, 10, 55u, 1, pk);
    sound(4, "Snare Rim", 28541, pk, n);
    jog(-1), jog_click(), rest();
    CHECK(M.kit.tools && M.kit.row == 0);
    out = vs_ref("padact", &ICONS_V, 1);
    CHECKF(out == 0, "padact: %d differ (the wave)", out);
    dump("padact");
    back();
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
    printf("mock boards: rest, three, pitch, mode, attack, choke, kittab, inkey, places, files, padact, armed, recording\n");
}

/* ================================================================ (stage 3b) === */
static const uint8_t *L(int i)                          /* (an LED of the last leds()) */
{
    static uint8_t w[RFM_MOVE_NLEDS][3];
    if (i < 0)
        leds(w);
    return w[i < 0 ? 0 : i];
}
/* 18. Stretch: knob 3 (lit), the steps it lasts (the popup's grid and the step keys, board D), KIT_SET's microseconds
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
    rest(), clear();
    touch(2, 1), knob(2, 48);                           /* (3 ticks a step: 16 steps) */
    CHECK(M.kit.pad[8].v[RFM_MOVE_KIT_P_STRETCH] == 16);
    CHECKF((a = last(RFM_MOVE_ACT_KIT_SET)) && a->param == RFM_MOVE_KIT_P_STRETCH && a->value == 16 && a->from == 2000000u,
           "from %u", a ? a->from : 0);                 /* (16 x 125 ms) */
    out = vs_ref("stretch", NULL, 0);
    CHECKF(out == 0, "stretch: %d differ", out);
    dump("stretch");
    L(-1);
    CHECK(is_rgb(L(L_RING0 + 2), 255, 255, 255) && is_rgb(L(L_STEP0), 119, 219, 254) && is_rgb(L(L_STEP0 + 5), 51, 94, 109) &&
          is_rgb(L(L_STEP0 + 15), 51, 94, 109));        /* (board D: the steps it lasts, the first brightest) */
    knob(2, -36);                                       /* (4 steps: the rest dark) */
    L(-1);
    CHECK(M.kit.pad[8].v[RFM_MOVE_KIT_P_STRETCH] == 4 && lit(L(L_STEP0 + 3)) && !lit(L(L_STEP0 + 4)));
    touch(2, 0), rest();
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
    /* Pitch: "Stretch", a turn kept away; the rows: Stretch after Pitch, its popup lights the steps too */
    M.seq.tr[14].rate = (uint8_t)(M.seq.tr[14].rate - 1), M.seq.bpm10 = 1200, adv_ms(20);
    {
        uint16_t subj;
        uint32_t st;
        touch(0, 1);
        rfmv_kit_pop_words(&M, v, sizeof v, &subj, &st);
        CHECKF(!strcmp(v, "Pitch: Stretch"), "said %s", v);
        touch(0, 0), rest();
    }
    jog(2), jog_click();
    CHECK(M.kit.row == 2 && M.kit.edit);
    L(-1);
    CHECK(lit(L(L_STEP0 + 3)) && !lit(L(L_STEP0 + 4)));
    jog(-4);
    CHECK(!M.kit.pad[8].v[RFM_MOVE_KIT_P_STRETCH]);
    back(), rest();
    for (i = 0; i < 1; i++)
        CHECK(M.kit.pad[8].v[RFM_MOVE_KIT_P_PITCH] == 0);
}
/* 19. the sends: knob 8 Send 1 (board H), rows Send 1..3 */
static void t_sends(void)
{
    const rfm_move_act_t *a;
    int out;
    kit_view();
    hit(kp(5)), rest();                                 /* (pad 6) */
    touch(7, 1), knob(7, 35);
    CHECK(M.kit.pad[5].v[RFM_MOVE_KIT_P_SEND1] == 35 && (a = last(RFM_MOVE_ACT_KIT_SET)) && a->param == RFM_MOVE_KIT_P_SEND1);
    out = vs_ref("send", NULL, 0);
    CHECKF(out == 0, "send: %d differ", out);
    dump("send");
    touch(7, 0), rest();
    jog(14), jog_click(), jog(20);                      /* (row 15: Send 2) */
    CHECK(M.kit.row == 14 && M.kit.pad[5].v[RFM_MOVE_KIT_P_SEND2] == 20);
    back(), jog(5), jog_click(), jog(60), jog(60);
    CHECK(M.kit.row == 15 && M.kit.pad[5].v[RFM_MOVE_KIT_P_SEND3] == 100);
    back();
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
    clear(), touch(3, 1), knob(3, -50);
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
    jog(-1), jog_click(), jog(2), jog_click(), jog(1), jog(1), jog_click();   /* (Files, Samples) */
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
    jog(-1), jog_click(), jog(1), jog_click(), jog(1), jog(1);   /* (the Kit tab, Save) */
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
    CHECK(count(RFM_MOVE_ACT_KIT_LOAD) == 16 && count(RFM_MOVE_ACT_KIT_SET) == 16 * RFM_MOVE_KIT_P_N &&
          count(RFM_MOVE_ACT_MIX) == 1);
    rfm_move_kit_loaded(&M, 2, RFM_MOVE_KIT_OK, "Loop", "/k/Kit 1/Loop.wav", 96000, 48000, 0, NULL);
    CHECK(M.kit.pad[2].st == RFM_MOVE_KIT_OK);
    clear(), press(MR_CC_UNDO);                         /* (the kit before it) */
    CHECK(!strcmp(M.kit.name, "Kit 3") && M.kit.rh == 0 && M.kit.mix[0] == 100 && strstr(M.kit.pad[4].path, "Snare") &&
          !M.kit.pad[2].path[0]);
    CHECK(rfm_move_kit_apply(&M, "x", 0, NULL, NULL, NULL) >= 0 && !strcmp(M.kit.msg, "Not Loaded") && !strcmp(M.kit.name, "Kit 3"));
    /* the places: Kits the third */
    rest(), back(), back();
    CHECK(M.kit.place == 0);
    jog(5), clear(), jog_click();
    CHECK(M.kit.place == RFM_MOVE_KIT_PL_KITS && last(RFM_MOVE_ACT_KIT_ROWS));
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
    CHECK(b[4] == 12 && rfm_move_project_check(b, (uint32_t)n) == 0);
    rfm_move_init(&M2, NULL, on_act, NULL);
    rfm_move_fw_caps(&M2, KCAPS);
    CHECK(rfm_move_project_load(&M2, 1, b, (uint32_t)n) == 0 && M2.kit.pad[3].v[RFM_MOVE_KIT_P_STRETCH] == 12 &&
          M2.kit.pad[3].v[RFM_MOVE_KIT_P_SEND3] == 77 && !strcmp(M2.kit.name, "Kit 9") && M2.kit.mix[4] == 5);
    /* a version 11 blob (as stage 3a wrote it): 8 bytes, 12 settings a pad */
    memcpy(c, b, (size_t)n - 4 - RFM_MOVE_KIT_BYTES);
    {
        uint8_t *o = c + n - 4 - RFM_MOVE_KIT_BYTES;
        const uint8_t *x = b + n - 4 - RFM_MOVE_KIT_BYTES;
        memset(o, 0, RFM_MOVE_KIT_BYTES_V11);
        memcpy(o, x, 4);
        o += 8, x += 40;
        for (k = 0; k < RFM_MOVE_KIT_PADS; k++, o += 160 + 24, x += 160 + 32)
            memcpy(o, x, 160 + 24);
    }
    n11 = n - RFM_MOVE_KIT_BYTES + RFM_MOVE_KIT_BYTES_V11;
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
/* 25. a pad's own edits (board V): the Sound row's tools Play, Trim, Normalize, Reverse, Delete; an edit asks the host
 * for a new file (KIT_EDIT), its answer the pad's own copy (Trim: the region whole; Normalize: kept; Reverse:
 * mirrored); Undo brings the pad back as it was; a refused one keeps the sound; the knobs and the right half dark */
static void t_tools3c(void)
{
    const rfm_move_act_t *a;
    uint8_t w[RFM_MOVE_NLEDS][3];
    kit_view();
    sound_at(0, "/s/Kick.wav", 48000, 48000);
    M.kit.pad[0].v[RFM_MOVE_KIT_P_START] = 1000, M.kit.pad[0].v[RFM_MOVE_KIT_P_END] = 9000;
    M.kit.pad[0].v[RFM_MOVE_KIT_P_PITCH] = 200;
    jog_click();                                        /* (the Sound row's click: the tools) */
    CHECK(M.kit.tools && M.kit.ti == TL_PLAY_T);
    leds(w);
    CHECK(!lit(w[L_RING0]) && !lit(w[L_PAD0 + rp(0)]));
    clear(), knob(0, 3);
    CHECK(M.kit.pad[0].v[RFM_MOVE_KIT_P_PITCH] == 200 && !count(RFM_MOVE_ACT_KIT_SET));   /* (dead) */
    hit(rp(0));
    CHECK(!count(RFM_MOVE_ACT_KIT_NOTE));               /* (the right half dead) */
    /* Trim: a new file, the region whole */
    jog(1), clear(), jog_click();
    CHECK(!M.kit.tools && (a = last(RFM_MOVE_ACT_KIT_EDIT)) && a->index == 0 && a->value == RFM_MOVE_KIT_ED_TRIM);
    CHECK(M.kit.pad[0].st == RFM_MOVE_KIT_LOADING && said3c("Trimmed"));
    sound_at(0, "/s/Kits/Edits/Kick trim.wav", 8000, 48000);
    CHECK(!strcmp(M.kit.pad[0].path, "/s/Kits/Edits/Kick trim.wav") && M.kit.pad[0].v[RFM_MOVE_KIT_P_START] == 0 &&
          M.kit.pad[0].v[RFM_MOVE_KIT_P_END] == RFM_MOVE_KIT_FRAC && M.kit.pad[0].v[RFM_MOVE_KIT_P_PITCH] == 200);
    clear(), press(MR_CC_UNDO);                         /* (the pad back: its file, its region) */
    CHECK(!strcmp(M.kit.pad[0].path, "/s/Kick.wav") && M.kit.pad[0].v[RFM_MOVE_KIT_P_START] == 1000 &&
          M.kit.pad[0].v[RFM_MOVE_KIT_P_END] == 9000 && last_i(RFM_MOVE_ACT_KIT_LOAD) >= 0 &&
          !strcmp(D[last_i(RFM_MOVE_ACT_KIT_LOAD)], "/s/Kick.wav"));
    sound_at(0, "/s/Kick.wav", 48000, 48000);
    CHECK(M.kit.pad[0].v[RFM_MOVE_KIT_P_START] == 1000);
    /* Normalize: the region kept */
    rest(), jog_click(), jog(2), clear(), jog_click();
    CHECK((a = last(RFM_MOVE_ACT_KIT_EDIT)) && a->value == RFM_MOVE_KIT_ED_NORMALIZE);
    sound_at(0, "/s/Kits/Edits/Kick norm.wav", 48000, 48000);
    CHECK(M.kit.pad[0].v[RFM_MOVE_KIT_P_START] == 1000 && M.kit.pad[0].v[RFM_MOVE_KIT_P_END] == 9000 &&
          !strcmp(M.kit.pad[0].path, "/s/Kits/Edits/Kick norm.wav"));
    /* Reverse: the region mirrored */
    rest(), jog_click(), jog(3), clear(), jog_click();
    CHECK((a = last(RFM_MOVE_ACT_KIT_EDIT)) && a->value == RFM_MOVE_KIT_ED_REVERSE);
    sound_at(0, "/s/Kits/Edits/Kick norm rev.wav", 48000, 48000);
    CHECK(M.kit.pad[0].v[RFM_MOVE_KIT_P_START] == RFM_MOVE_KIT_FRAC - 9000 &&
          M.kit.pad[0].v[RFM_MOVE_KIT_P_END] == RFM_MOVE_KIT_FRAC - 1000);
    /* a refused one: the pad keeps its sound */
    rest(), jog_click(), jog(2), jog_click();
    rfm_move_kit_loaded(&M, 0, RFM_MOVE_KIT_FAILED, NULL, NULL, 0, 0, 0, NULL);
    CHECK(M.kit.pad[0].st == RFM_MOVE_KIT_OK && M.kit.pad[0].frames == 48000 &&
          !strcmp(M.kit.pad[0].path, "/s/Kits/Edits/Kick norm rev.wav") && said3c("Not Loaded"));
    /* an empty pad: nothing asked */
    rest(), button(MR_CC_SHIFT, 1), hit(kp(3)), button(MR_CC_SHIFT, 0);
    jog_click(), jog(1), clear(), jog_click();
    CHECK(!count(RFM_MOVE_ACT_KIT_EDIT));
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
          lit(w[L_RING0]) && lit(w[L_RING0 + 1]) && !lit(w[L_RING0 + 2]));
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
    clear(), jog(3), jog(1);                            /* (Steps: a step's microseconds at the tempo; past it nothing) */
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
    /* O: the join on pad 5, its seam; the Pad tab at rest */
    jog_click(), button(MR_CC_COPY, 0);
    n = mock_peaks(62, 11, 12u, 1, pk);
    pk[n++] = 0;
    n += mock_peaks(64, 11, 55u, 1, pk + n);
    rfm_move_kit_loaded(&M, 4, RFM_MOVE_KIT_OK, "Join 2+5", "/s/Kits/Edits/Join 2+5.wav", 48000, 48000, n, pk);
    rest();
    out = vs_ref("joined", &COL62, 1);
    CHECKF(out == 0, "joined: %d differ", out);
    dump("joined");
    /* V: the Sound row's tools, Trim chosen (the whole board) */
    n = mock_peaks(128, 10, 55u, 1, p2);
    rfm_move_kit_loaded(&M, 4, RFM_MOVE_KIT_OK, "Snare Rim", "/s/Snare Rim.wav", 28541, 48000, n, p2);
    jog_click(), jog(1), rest();
    CHECK(M.kit.tools && M.kit.ti == 1);
    out = vs_ref("padact", NULL, 0);
    CHECKF(out == 0, "padact: %d differ", out);
    dump("padact");
    back();
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
    printf("mock boards (3c): merge, joined, padact, slice\n");
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
    t_kits();
    t_smp_kit();
    t_project12();
    printf("mock boards (3b): stretch, send, folder, saved, sampler\n");
    t_tools3c();
    t_merge();
    t_slice();
    t_page3c();
    t_mock3c();
    return check_done("move_kit");
}
