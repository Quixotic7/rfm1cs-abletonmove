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
 * a held Track), and the screens against the mock-up's boards (move_kit_ref.h). Writes kit_*.pbm / .leds. */
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
static int kit_acts(void)
{
    int k, n = 0;
    for (k = RFM_MOVE_ACT_KIT_NOTE; k <= RFM_MOVE_ACT_KIT_REC; k++)
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

/* 7. the knobs: the chosen pad's (1 Pitch, 2 Start, 4 Play, 5 Attack, 6 Release, 7 Level; 3 and 8 dark and dead) */
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
    CHECK(lit_n == 6 && !lit(w[L_RING0 + 2]) && !lit(w[L_RING0 + 7]) && is_rgb(w[L_RING0 + 0], 149, 35, 93));
    clear();
    touch(0, 1), knob(0, 6), touch(0, 0);
    CHECK((a = last(RFM_MOVE_ACT_KIT_SET)) && a->index == 4 && a->param == RFM_MOVE_KIT_P_PITCH && a->value == 300);
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 300 && M.seq.dirty);
    button(MR_CC_SHIFT, 1), knob(0, 5), button(MR_CC_SHIFT, 0);   /* (Shift: cents) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 305);
    knob(0, 2);                                         /* (a fine pitch goes to the next whole semitone first) */
    CHECK(M.kit.pad[4].v[RFM_MOVE_KIT_P_PITCH] == 400);
    clear(), touch(2, 1), knob(2, 10), touch(2, 0), touch(7, 1), knob(7, 10), touch(7, 0);
    CHECK(!kit_acts());                                 /* (3 and 8: dead) */
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
    /* the Kit tab: the right half's layout */
    jog(-1), jog_click(), jog(-1), jog_click(), jog(1), jog_click();
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
    jog(1), clear(), jog_click();
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
    CHECK(M.kit.pad[6].st == RFM_MOVE_KIT_LOADING && !strcmp(M.kit.pad[6].name, "Hat"));
    for (i = 0; i < 64; i++)
        pk[i] = (uint8_t)(255 - i * 3);
    M.kit.pad[6].v[RFM_MOVE_KIT_P_START] = 500;
    clear();
    CHECK(rfm_move_kit_loaded(&M, 6, RFM_MOVE_KIT_OK, "Hat", "/data/UserData/UserLibrary/Samples/Hat.wav", 9600, 48000, 64, pk) >= 0);
    CHECK(!strcmp(M.kit.pad[6].path, "/data/UserData/UserLibrary/Samples/Hat.wav") && M.kit.pad[6].st == RFM_MOVE_KIT_OK &&
          M.kit.pad[6].npk == 64 && M.kit.pad[6].v[RFM_MOVE_KIT_P_START] == 0);   /* (a new sound: its whole region) */
    CHECK(rfm_move_kit_loaded(&M, 5, RFM_MOVE_KIT_TOO_LONG, "Big", NULL, 0, 0, 0, NULL) >= 0 &&
          M.kit.pad[5].st == RFM_MOVE_KIT_TOO_LONG && !strcmp(M.kit.msg, "Too Long"));
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
    CHECK((a = last(RFM_MOVE_ACT_KIT_REC)) && a->index == 6 && a->down == 0);
    st.state = RFM_MOVE_SMP_ST_IDLE;
    rfm_move_smp_status(&M, &st);
    rfm_move_kit_loaded(&M, 6, RFM_MOVE_KIT_OK, "Take 3", "/data/UserData/UserLibrary/Samples/RemoteFM1/2026-11-25/t3.wav",
                        96000, 48000, 0, NULL);
    CHECK(!M.kit.rec_pad && !strcmp(M.kit.pad[6].name, "Take 3"));
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
    CHECK(n == RFM_MOVE_PROJECT_FIXED + 7 && b[4] == 11 && rfm_move_project_check(b, (uint32_t)n) == 0);
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
        switch (i) {
        case 0: c[n - 4 - RFM_MOVE_KIT_BYTES] = 9; break;              /* (no such layout) */
        case 1: c[n - 4 - RFM_MOVE_KIT_BYTES + 1] = 1; break;          /* (track 1 to the kit) */
        case 2: c[n - 4 - RFM_MOVE_KIT_BYTES + 8 + 159] = 'x'; break;  /* (a path with no NUL) */
        default: c[n - 4 - RFM_MOVE_KIT_BYTES + 8 + 160 + 2 * RFM_MOVE_KIT_P_START] = 0xFF,
                 c[n - 4 - RFM_MOVE_KIT_BYTES + 8 + 160 + 2 * RFM_MOVE_KIT_P_START + 1] = 0x7F; break;   /* (start past
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
    static const box_t ROW3 = { 0, 48, 128, 16 }, AHR_E = { 0, 48, 128, 16 }, AHR_F = { 0, 36, 128, 28 };
    static const box_t ROWS_Q[2] = { { 0, 14, 128, 50 }, { 60, 0, 68, 14 } }, ICONS_V = { 0, 24, 128, 40 };
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
    /* B, three: Sound, Pitch (chosen), the third row (the mock's Stretch, 3b: here Start) */
    jog(1), jog(-1);
    out = vs_ref("three", &ROW3, 1);
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
    jog(7), jog_click(), rest();
    CHECK(M.kit.row == 8 && M.kit.edit);
    out = vs_ref("choke", NULL, 0);
    CHECKF(out == 0, "choke: %d differ", out);
    dump("choke");
    back();
    /* S, In Key: C minor (the Kit tab's popup) */
    M.kit.rh = RFM_MOVE_KIT_RH_INKEY;
    button(MR_CC_SHIFT, 1), hit(kp(4)), button(MR_CC_SHIFT, 0);
    M.s.root = 0, M.s.scale = RFM_MOVE_SCALE_MINOR;
    jog(-20), jog_click(), jog(1), jog_click(), jog(1), rest();
    CHECK(M.kit.tab == 1 && !M.kit.on_tabs);
    out = vs_ref("kittab", ROWS_Q, 2);
    CHECKF(out == 0, "kittab: %d differ (the tabs)", out);
    dump("kittab");
    jog_click();
    out = vs_ref("inkey", NULL, 0);
    CHECKF(out == 0, "inkey: %d differ", out);
    dump("inkey");
    back(), back();
    /* I, the places (Kits: 3b, the third row masked); J, a folder: three sounds, their pictures */
    jog_click(), jog(1), jog_click(), jog(1), jog(1), rest();
    CHECK(M.kit.tab == 2 && M.kit.psel == 1);
    out = vs_ref("places", &ROW3, 1);
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
    return check_done("move_kit");
}
