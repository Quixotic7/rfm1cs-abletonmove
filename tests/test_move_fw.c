/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* 0.4.0: the firmware browser (the projects view's Tracks 1..3), the catalog, the library blob, the levels and voices,
 * FX setup, the firmwares' states, the sequencer's MIDI tagged with its track, the old formats, and a host with no
 * firmware list. Input goes in as the Move's raw USB-MIDI packets through mr_decode (a fake Move). Then every screen of
 * the mockup's gallery (move-firmware-browser-mockup.html, its catalog in move_fwb_data.h) set up on a live core and
 * compared with the mockup's pixels (move_fwb_ref.h, from move_v2_ref.mjs); writes fw_<name>.pbm for the contact
 * sheet. 0.4.1: the host's edits (rfm_move_fw_set_active .. rfm_move_fx_set_page) against the Move's own. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "move_int.h"
#include "moveremote/client.h"
#include "rfm1cs/move.h"
#include "move_fwb_data.h"
#include "move_fwb_ref.h"

#define NFW ((int)(sizeof FWB / sizeof FWB[0]))

/* ---- the actions (data copied out: ids, blobs) */
typedef struct { rfm_move_act_t a; char id[RFM_MOVE_FW_ID_MAX]; } act_t;
static act_t acts[4096];
static int nact;
static uint8_t lib[RFM_MOVE_FW_LIB_BYTES];
static uint32_t lib_n;
static int nlib;
static void on_act(void *ctx, const rfm_move_act_t *a)
{
    (void)ctx;
    if (nact < 4096) {
        acts[nact].a = *a, acts[nact].id[0] = 0;
        if (a->data && a->kind != RFM_MOVE_ACT_FW_LIBRARY && a->kind != RFM_MOVE_ACT_PROJECT_SAVE && a->len < RFM_MOVE_FW_ID_MAX)
            memcpy(acts[nact].id, a->data, a->len), acts[nact].id[a->len] = 0;
        acts[nact].a.data = NULL;
        nact++;
    }
    if (a->kind == RFM_MOVE_ACT_FW_LIBRARY && a->len <= sizeof lib)
        memcpy(lib, a->data, a->len), lib_n = a->len, nlib++;
}
static void clear(void) { nact = 0; }
static int count(int kind)
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += acts[i].a.kind == kind;
    return n;
}
static const act_t *last(int kind)
{
    int i;
    for (i = nact - 1; i >= 0; i--)
        if (acts[i].a.kind == kind)
            return &acts[i];
    return NULL;
}

/* ---- the fake Move */
static rfm_move_t M, M2;
static uint32_t now_ms;
static void raw(rfm_move_t *m, uint8_t st, uint8_t d1, uint8_t d2)
{
    const uint8_t pkt[4] = { (uint8_t)(st >> 4), st, d1, d2 };
    mr_event_t ev;
    mr_decode(pkt, 0, &ev);
    rfm_move_input(m, &ev, now_ms += 5);
}
static void pad(rfm_move_t *m, int p, int vel) { raw(m, vel ? 0x90 : 0x80, (uint8_t)(MR_NOTE_PAD0 + p), (uint8_t)vel); }
static void tap_pad(rfm_move_t *m, int p) { pad(m, p, 100), pad(m, p, 0); }
static void step(rfm_move_t *m, int s, int down) { raw(m, down ? 0x90 : 0x80, (uint8_t)(MR_NOTE_STEP0 + s), down ? 127 : 0); }
static void tap_step(rfm_move_t *m, int s) { step(m, s, 1), step(m, s, 0); }
static void button(rfm_move_t *m, int cc, int down) { raw(m, 0xB0, (uint8_t)cc, down ? 127 : 0); }
static void press(rfm_move_t *m, int cc) { button(m, cc, 1), button(m, cc, 0); }
static void touch(rfm_move_t *m, int k, int on) { raw(m, on ? 0x90 : 0x80, (uint8_t)k, on ? 127 : 0); }
static void knob(rfm_move_t *m, int k, int d) { raw(m, 0xB0, (uint8_t)(MR_CC_KNOB1 + k), (uint8_t)(d & 127)); }

static void jog(rfm_move_t *m, int d) { raw(m, 0xB0, MR_CC_JOG, (uint8_t)(d & 127)); }
static void jog_press(rfm_move_t *m) { raw(m, 0xB0, MR_CC_JOG_CLICK, 127), raw(m, 0xB0, MR_CC_JOG_CLICK, 0); }
static void track(rfm_move_t *m, int t, int down) { button(m, MR_CC_TRACK1 - t, down); }
/* (0.8.0) a Track tap acts at its release; a press within RFM_MOVE_DOUBLE_MS of a tap's release is a double click:
 * every tap here starts after that window */
static void tap_track(rfm_move_t *m, int t) { now_ms += RFM_MOVE_DOUBLE_MS + 50, track(m, t, 1), track(m, t, 0); }
static void dbl_track(rfm_move_t *m, int t)   /* a double click: Tracks 1..3 the browser on that slot, Track 4 the projects */
{
    tap_track(m, t), track(m, t, 1), track(m, t, 0);
    now_ms += RFM_MOVE_DOUBLE_MS + 50;
}
static void adv_ms(rfm_move_t *m, uint32_t ms) { while (ms) { const uint32_t k = ms < 10 ? ms : 10; rfm_move_seq_advance(m, k * 48, 48000); ms -= k; } }
static void rest_pop(rfm_move_t *m) { adv_ms(m, RFM_MOVE_POPUP_MS + 50); }
/* (0.8.0) the projects grid: Track 4 held + step 1 (Shift + step 1, outside the view) */
static void projects(rfm_move_t *m) { now_ms += RFM_MOVE_DOUBLE_MS + 50, track(m, 3, 1), tap_step(m, 0), track(m, 3, 0); }
static void browser(rfm_move_t *m, int t) { dbl_track(m, t); }   /* (0.8.0) the browser on slot t: Track t double click */
static int pad_of(int cell) { return (3 - cell / 8) * 8 + cell % 8; }
static void write_pbm(const char *name);
/* (0.5.0) FX setup's one layer: column 1 MIDI FX, 3 the firmwares, 5 audio FX, rows 1..3 from the top */
static int fxpad(int page, int row) { return pad_of(row * 8 + (page == 0 ? 0 : page == 1 ? 2 : 4)); }

/* ---- the catalog as the host has it (the mockup's), and its modules */
static rfm_move_fw_t E[NFW];
static int idx(const char *id)
{
    int i;
    for (i = 0; i < NFW; i++)
        if (!strcmp(FWB[i].id, id))
            return i;
    return -1;
}
static void catalog_reset(void)
{
    int i;
    for (i = 0; i < NFW; i++) {
        E[i].id = FWB[i].id, E[i].name = FWB[i].name, E[i].version = FWB[i].version, E[i].installed = FWB[i].installed;
        E[i].desc = FWB[i].desc, E[i].thumb = FWB[i].thumb, E[i].cats = FWB[i].cats, E[i].cat = FWB[i].cat;
        E[i].virt = 0;
    }
}
static const char *const W_RATE[] = { "1/8", "1/16", "1/32" }, *const W_MODE[] = { "Up", "Down", "Up/Dn", "Random" };
static const char *const W_OCT[] = { "1", "2", "3", "4" }, *const W_SHAPE[] = { "Major", "Minor", "Sus4", "7th", "9th" };
static const char *const W_INV[] = { "0", "1", "2" }, *const W_CURVE[] = { "Soft", "Linear", "Hard" };
static const char *const W_TIME[] = { "1/8", "1/8.", "1/4" };
/* (0.4.x hosts' positional initializers: the 0.5.0 fields left out, zero = as before) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const rfm_move_fx_param_t P_ARP[] = { { "Rate", 3, W_RATE, 0, 0 }, { "Mode", 4, W_MODE, 0, 0 }, { "Octaves", 4, W_OCT, 0, 0 }, { "Gate", 0, NULL, 64, 0 } };
static const rfm_move_fx_param_t P_CHORD[] = { { "Shape", 5, W_SHAPE, 0, 0 }, { "Invert", 3, W_INV, 0, 0 }, { "Spread", 0, NULL, 64, 0 } };
static const rfm_move_fx_param_t P_VEL[] = { { "Amount", 0, NULL, 64, 0 }, { "Curve", 3, W_CURVE, 0, 0 } };
static const rfm_move_fx_param_t P_TR[] = { { "Semis", 0, NULL, 64, 0 } };
static const rfm_move_fx_param_t P_HUM[] = { { "Time", 0, NULL, 64, 0 }, { "Velocity", 0, NULL, 64, 0 } };
static const rfm_move_fx_module_t MFX[] = { { "Arp", 4, P_ARP }, { "Chord", 3, P_CHORD }, { "Velocity", 2, P_VEL },
                                            { "Transpose", 1, P_TR }, { "Humanize", 2, P_HUM } };
static const rfm_move_fx_param_t P_REV[] = { { "Size", 0, NULL, 64, 0 }, { "Damping", 0, NULL, 64, 0 }, { "Mix", 0, NULL, 127, 1 } };
static const rfm_move_fx_param_t P_DLY[] = { { "Time", 3, W_TIME, 0, 0 }, { "Feedback", 0, NULL, 64, 0 }, { "Mix", 0, NULL, 127, 1 } };
static const rfm_move_fx_param_t P_CHO[] = { { "Rate", 0, NULL, 64, 0 }, { "Depth", 0, NULL, 64, 0 }, { "Mix", 0, NULL, 127, 1 } };
static const rfm_move_fx_param_t P_BIT[] = { { "Bits", 0, NULL, 64, 0 }, { "Rate", 0, NULL, 64, 0 } };
static const rfm_move_fx_param_t P_FIL[] = { { "Cutoff", 0, NULL, 64, 0 }, { "Reso", 0, NULL, 64, 0 } };
static const rfm_move_fx_module_t AFX[] = { { "Freeverb", 3, P_REV }, { "Delay", 3, P_DLY }, { "Chorus", 3, P_CHO },
                                            { "Bitcrush", 2, P_BIT }, { "Filter", 2, P_FIL } };
#pragma GCC diagnostic pop

/* ---- a library blob, written by hand from the documented format (host guide, v0.4.0) */
typedef struct { const char *id; int color; uint16_t on, set; } recdef_t;
static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static uint32_t lib_write(uint8_t *o, int active, const int lv[3], const char *const slots[3], const char *const tags[9],
                          const recdef_t *r, int nr, const int *fav, int nf)
{
    uint8_t *p = o + 256;
    uint32_t h;
    int k;
    memset(o, 0, 256);
    memcpy(o, "RMVL", 4);
    o[4] = 1, o[5] = (uint8_t)active, o[6] = (uint8_t)lv[0], o[7] = (uint8_t)lv[1], o[8] = (uint8_t)lv[2];
    o[9] = (uint8_t)nr, o[10] = (uint8_t)(nr >> 8), o[11] = (uint8_t)nf, o[12] = (uint8_t)(nf >> 8);
    for (k = 0; k < 3; k++)
        if (slots[k])
            memcpy(o + 16 + k * 32, slots[k], strlen(slots[k]));
    for (k = 0; k < 9; k++)
        if (tags[k])
            memcpy(o + 112 + k * 16, tags[k], strlen(tags[k]));
    for (k = 0; k < nr; k++, p += 37) {
        memset(p, 0, 37);
        memcpy(p, r[k].id, strlen(r[k].id));
        p[32] = (uint8_t)r[k].color, p[33] = (uint8_t)r[k].on, p[34] = (uint8_t)(r[k].on >> 8);
        p[35] = (uint8_t)r[k].set, p[36] = (uint8_t)(r[k].set >> 8);
    }
    for (k = 0; k < nf; k++, p += 2)
        p[0] = (uint8_t)fav[k], p[1] = (uint8_t)(fav[k] >> 8);
    h = fnv(o, (uint32_t)(p - o));
    for (k = 0; k < 4; k++)
        p[k] = (uint8_t)(h >> (8 * k));
    return (uint32_t)(p - o) + 4;
}
#define T(s) (uint16_t)(1u << ((s) - 1))
/* the mockup's fresh(): Track 1 shown, SLOOP and ChoralRoot loaded, its favourites, colours, tags and FX setup */
static void fixture(rfm_move_t *m)
{
    static uint8_t blob[RFM_MOVE_FW_LIB_BYTES];
    recdef_t r[64];
    int fav[64], nr = 0, nf = 0, k;
    static const int lv[3] = { 100, 80, 80 };
    static const char *const slots[3] = { "sloop", "choralroot", NULL };
    static const char *const tags[9] = { "Ambient", "Live set", "Retro" };
    static const struct { const char *id; int color; uint16_t tags; } X[] = {
        { "sloop", 13, T(9) }, { "elasticat", 11, T(9) | T(8) }, { "golf", 4, 0 }, { "amb", 0, T(8) },
        { "tekstuur", 0, T(8) }, { "x0x", 0, T(9) }, { "sid1", 0, T(10) }, { "chipbox", 0, T(10) }, { "trax", 0, T(10) } };
    catalog_reset();
    rfm_move_init(m, NULL, on_act, NULL);
    for (k = 0; k < (int)(sizeof FWB_FAV0 / sizeof FWB_FAV0[0]); k++)
        if (idx(FWB_FAV0[k]) >= 0)
            fav[nf++] = nr, r[nr].id = FWB_FAV0[k], r[nr].color = 0, r[nr].on = r[nr].set = 0, nr++;
    for (k = 0; k < (int)(sizeof X / sizeof X[0]); k++) {
        int j;
        for (j = 0; j < nr && strcmp(r[j].id, X[k].id); j++) {}
        if (j == nr)
            r[nr].id = X[k].id, r[nr].on = r[nr].set = 0, nr++;
        r[j].color = X[k].color, r[j].on = X[k].tags, r[j].set = X[k].tags;
    }
    CHECK(rfm_move_fw_library(m, blob, lib_write(blob, 0, lv, slots, tags, r, nr, fav, nf)) == 0);
    rfm_move_fw_list(m, E, NFW);
    rfm_move_fw_slots(m, slots);
    rfm_move_fw_voices(m, 3);
    rfm_move_fx_modules(m, RFM_MOVE_FX_MIDI, MFX, 5);
    rfm_move_fx_modules(m, RFM_MOVE_FX_AUDIO, AFX, 5);
    {
        rfm_move_fxset_t *x = &m->seq.fx;
        static const uint8_t mv[2][8] = { { 1, 0, 1, 90 }, { 0, 0, 40 } }, av[2][8] = { { 90, 40, 35 }, { 1, 60, 30 } };
        strcpy(x->mfx[0].name, "Arp"), memcpy(x->mfx[0].val, mv[0], 8), x->mfx[0].filt = T(7);
        strcpy(x->mfx[1].name, "Chord"), memcpy(x->mfx[1].val, mv[1], 8), x->mfx[1].filt = T(6);
        strcpy(x->afx[0].name, "Freeverb"), memcpy(x->afx[0].val, av[0], 8);
        strcpy(x->afx[1].name, "Delay"), memcpy(x->afx[1].val, av[1], 8);
        x->ffilt[0] = T(1) | T(2) | T(3) | T(4) | T(5), x->ffilt[1] = T(7) | T(8), x->ffilt[2] = T(6);
        x->send[1][0] = 90, x->send[2][0] = 30, x->send[2][1] = 70;
        rfm_move_fx_modules(m, RFM_MOVE_FX_MIDI, MFX, 5), rfm_move_fx_modules(m, RFM_MOVE_FX_AUDIO, AFX, 5);
    }
    browser(m, 0);                                      /* (0.8.0: Track 1's double click; was Shift + step 1) */
    clear();
}
static void select_fw(rfm_move_t *m, const char *id) { m->fw.sel = (int16_t)idx(id), m->fw.jog = 0; }

/* ---- the screen */
static uint8_t scr[MR_SCREEN_BYTES];
static void screen(rfm_move_t *m) { rfm_move_screen(m, NULL, scr); }
static int px(int x, int y) { return scr[(y >> 3) * 128 + x] >> (y & 7) & 1; }
static int lit_in(int x0, int y0, int x1, int y1)
{
    int x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += px(x, y);
    return n;
}
static rfm_move_led_t leds[RFM_MOVE_NLEDS];
static int led_of(rfm_move_t *m, int cc, int number, uint8_t rgb[3])   /* an LED as it is now: 1 found */
{
    int n, i;
    rfm_move_leds_invalidate(m);
    n = rfm_move_leds(m, leds, RFM_MOVE_NLEDS);
    for (i = 0; i < n; i++)
        if (leds[i].cc == cc && leds[i].number == number) {
            rgb[0] = leds[i].r, rgb[1] = leds[i].g, rgb[2] = leds[i].b;
            return 1;
        }
    return 0;
}

/* ================================================================ a host with no firmware list === */
static void test_nolist(void)
{
    rfm_move_t *m = &M;
    uint8_t c[3];
    int i;
    rfm_move_init(m, NULL, on_act, NULL);
    projects(m);
    CHECK(m->seq.projects && m->fw.track == 3);         /* (Track 4, as before) */
    screen(m);
    clear();
    press(m, MR_CC_MENU);                               /* Menu leaves, as before */
    CHECK(!m->seq.projects && nact == 0);
    projects(m);
    clear();
    tap_track(m, 0);                                    /* (0.8.0) Track 1 in the view: out, slot 0 played (already the
                                                         * active one: no FW_ACTIVE) */
    CHECK(!m->seq.projects && m->s.mode == RFM_MOVE_MODE_FM1 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0 && m->pop == POP_LAYOUT);
    dbl_track(m, 0), dbl_track(m, 1), tap_track(m, 1), tap_track(m, 2);   /* (no list: no browser, Tracks 2/3 nothing) */
    CHECK(!m->seq.projects && rfm_move_fw_active(m) == 0 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0);
    projects(m);                                        /* the projects grid: everything pressed, no crash */
    rest_pop(m), screen(m);
    CHECK(m->seq.projects && lit_in(0, 0, 128, 64) > 0);
    for (i = 0; i < 32; i++)
        tap_pad(m, i);
    for (i = 0; i < 16; i++)
        tap_step(m, i);
    jog(m, 3), jog_press(m), knob(m, 0, 10), press(m, MR_CC_SAMPLE), press(m, MR_CC_DELETE), press(m, MR_CC_MUTE);
    press(m, MR_CC_LEFT), press(m, MR_CC_RIGHT), press(m, MR_CC_LOOP), button(m, MR_CC_CAPTURE, 1), tap_step(m, 9);
    button(m, MR_CC_CAPTURE, 0), knob(m, 8, 5);
    screen(m);
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 0 && count(RFM_MOVE_ACT_FW_DOWNLOAD) == 0 && count(RFM_MOVE_ACT_FW_UNLOAD) == 0);
    /* (0.8.0) no list: Track 1 the one firmware (lit), Tracks 2, 3 dark */
    CHECK(led_of(m, 1, MR_CC_TRACK1, c) && (c[0] || c[1] || c[2]));
    CHECK(led_of(m, 1, MR_CC_TRACK1 - 1, c) && !c[0] && !c[1] && !c[2] && led_of(m, 1, MR_CC_TRACK1 - 2, c) && !c[0] && !c[1] && !c[2]);
    if (m->seq.projects && !m->fw.track)                /* (left by a pad: back to the grid) */
        projects(m);
    press(m, MR_CC_MUTE);
    press(m, MR_CC_MENU);                               /* (no browser: Session leaves, no FX setup) */
    CHECK(!m->seq.projects && !m->fw.fx);
    projects(m);
    CHECK(m->fw.track == 3);                            /* (again on Track 4) */
    press(m, MR_CC_BACK);
    CHECK(!m->seq.projects);
    /* an empty list (the browser on, nothing installed): the firmware track's empty state */
    rfm_move_fw_list(m, NULL, 0);
    browser(m, 0);                                      /* (0.8.0: Track 1's double click) */
    CHECK(m->seq.projects && m->fw.track == 0);
    screen(m);
    CHECK(lit_in(0, 0, 128, 64) > 0);
    for (i = 0; i < 32; i++)
        tap_pad(m, i);
    press(m, MR_CC_MUTE), jog_press(m);
    CHECK(count(RFM_MOVE_ACT_FW_DOWNLOAD) == 0);
    press(m, MR_CC_BACK), press(m, MR_CC_BACK);
    CHECK(!m->seq.projects);
}

/* ================================================================ lists, filters, banks === */
static int n_installed(void) { int i, n = 0; for (i = 0; i < NFW; i++) n += FWB[i].installed != NULL; return n; }
static void test_filters(void)
{
    rfm_move_t *m = &M;
    int ids[32], i, n, want;
    fixture(m);
    CHECK(m->fw.track == 0 && m->s.fw_scope == 0);
    n = rfmv_fw_bank(m, ids);
    CHECK(n == (int)(sizeof FWB_FAV0 / sizeof FWB_FAV0[0]) && !strcmp(E[ids[0]].id, FWB_FAV0[0]) && !strcmp(E[ids[1]].id, FWB_FAV0[1]));
    tap_step(m, 1);                                     /* step 2: All (installed) */
    CHECK(m->s.fw_scope == 1 && m->fw.pop == FP_FILTER && count(RFM_MOVE_ACT_SETTINGS) == 1);
    n = rfmv_fw_list_len(m);
    CHECK(n == n_installed() && n > 32);
    tap_step(m, 2 + 2);                                 /* step 5: Chords */
    for (i = 0, want = 0; i < NFW; i++)
        want += FWB[i].installed && (FWB[i].cats & RFM_MOVE_CAT_CHORDS);
    CHECK(rfmv_fw_list_len(m) == want && want > 0);
    tap_step(m, 3 + 2);                                 /* step 6: Drums: just Drums (0.4.2: one category) */
    for (i = 0, want = 0; i < NFW; i++)
        want += FWB[i].installed && (FWB[i].cats & RFM_MOVE_CAT_DRUMS);
    CHECK(m->s.fw_filter == T(6) && rfmv_fw_list_len(m) == want && want > 0);
    tap_step(m, 0);                                     /* Favorites that are Drums (AND) */
    for (i = 0, want = 0; i < (int)(sizeof FWB_FAV0 / sizeof FWB_FAV0[0]); i++) {
        const int k = idx(FWB_FAV0[i]);
        want += k >= 0 && FWB[k].installed && (FWB[k].cats & RFM_MOVE_CAT_DRUMS);
    }
    CHECK(rfmv_fw_list_len(m) == want);
    tap_step(m, 5);                                     /* the lit one again: all categories */
    CHECK(m->s.fw_filter == 0 && rfmv_fw_list_len(m) == (int)(sizeof FWB_FAV0 / sizeof FWB_FAV0[0]));
    tap_step(m, 1);
    tap_step(m, 8);                                     /* step 9: the user tag "Live set" (sloop, elasticat, x0x) */
    CHECK(rfmv_fw_list_len(m) == 3);
    tap_step(m, 4);                                     /* a category replaces the tag too */
    CHECK(m->s.fw_filter == T(5));
    tap_step(m, 14);                                    /* step 15: no tag there: nothing happens */
    CHECK(m->s.fw_filter == T(5));
    tap_step(m, 4);
    CHECK(m->s.fw_filter == 0);
    /* the catalog: one scope (everything, Update ready, Not installed) */
    press(m, MR_CC_MUTE);
    CHECK(m->fw.catalog && m->fw.pop == FP_CATALOG && rfmv_fw_list_len(m) == NFW);
    tap_step(m, 0);
    for (i = 0, want = 0; i < NFW; i++)
        want += FWB[i].installed && strcmp(FWB[i].installed, FWB[i].version) != 0;
    CHECK(m->s.fw_cat_scope == 1 && rfmv_fw_list_len(m) == want && want == 4);
    tap_step(m, 1);                                     /* Not installed (instead) */
    CHECK(m->s.fw_cat_scope == 2 && rfmv_fw_list_len(m) == NFW - n_installed());
    tap_step(m, 1);
    CHECK(m->s.fw_cat_scope == 0 && rfmv_fw_list_len(m) == NFW);
    press(m, MR_CC_MUTE);
    /* banks: All has 2 + 1 banks; Right / Left; a filter starts again at bank 1 */
    tap_step(m, 1);
    {
        const int nb = (rfmv_fw_list_len(m) + 31) / 32;
        uint8_t c[3];
        CHECK(nb >= 2 && led_of(m, 1, MR_CC_RIGHT, c) && c[0] > 0 && led_of(m, 1, MR_CC_LEFT, c) && c[0] == 0);
        press(m, MR_CC_RIGHT);
        CHECK(m->fw.bank[1] == 1 && m->fw.pop == FP_BANK);
        n = rfmv_fw_bank(m, ids);
        CHECK(n == (nb > 2 ? 32 : rfmv_fw_list_len(m) - 32));
        press(m, MR_CC_LEFT), press(m, MR_CC_LEFT);
        CHECK(m->fw.bank[1] == 0);
        press(m, MR_CC_RIGHT), tap_step(m, 2);
        CHECK(m->fw.bank[1] == 0);
        tap_step(m, 2);
    }
    /* a pad selects; Back steps out: the description, the selection, then the view */
    n = rfmv_fw_bank(m, ids);
    tap_pad(m, pad_of(3));
    CHECK(m->fw.sel == ids[3]);
    jog(m, 2);
    CHECK(m->fw.jog == 2 || m->fw.jog == 1);
    press(m, MR_CC_BACK);
    CHECK(m->fw.jog == 0 && m->fw.sel == ids[3]);
    press(m, MR_CC_BACK);
    CHECK(m->fw.sel == -1 && m->seq.projects);
    press(m, MR_CC_BACK);
    CHECK(!m->seq.projects);
    /* the view's state is a setting: kept (version 4) */
    {
        uint8_t img[RFM_MOVE_SETTINGS_BYTES];
        rfm_move_settings_t st;
        m->s.fw_filter = T(9), m->s.fw_cat_scope = 2, m->s.fx_page = 3, m->s.fw_track = 3;
        CHECK(rfm_move_save(&m->s, img) == RFM_MOVE_SETTINGS_BYTES && img[3] == RFM_MOVE_SETTINGS_VERSION);
        CHECK(rfm_move_load(&st, img, sizeof img) == 0 && st.fw_scope == 1 && st.fw_filter == T(9) &&
              st.fw_cat_scope == 2 && st.fx_page == 3 && st.fw_track == 3);
        m->s.fw_filter = T(5) | T(9), m->s.fw_cat_scope = 3;   /* (0.4.1 saved several: the lowest; both scopes: 1) */
        rfm_move_save(&m->s, img);
        CHECK(rfm_move_load(&st, img, sizeof img) == 0 && st.fw_filter == T(5) && st.fw_cat_scope == 1);
        img[3] = 3, img[4] = (uint8_t)RFM_MOVE_SETTINGS_BYTES_V3;   /* (version 3: the view's defaults) */
        CHECK(rfm_move_load(&st, img, RFM_MOVE_SETTINGS_BYTES_V3) == 0 && st.fw_scope == 0 && st.fw_filter == 0 &&
              st.fw_track == 0 && st.vel_level == m->s.vel_level);
    }
}

/* ================================================================ load, unload, active, levels, voices === */
static void test_slots(void)
{
    rfm_move_t *m = &M;
    const act_t *a;
    uint8_t c0[3], c1[3];
    int ids[32], k, felucca = idx("felucca");
    fixture(m);
    {   /* Favorites: this track's firmware (SLOOP, cell 2) breathes; one on another track (ChoralRoot, cell 1) not */
        int t, d0 = 0, d1 = 0;
        for (t = 0; t < 6; t++) {
            led_of(m, 0, MR_NOTE_PAD0 + pad_of(1), c0), adv_ms(m, 270), led_of(m, 0, MR_NOTE_PAD0 + pad_of(1), c1);
            d1 |= memcmp(c0, c1, 3) != 0;
            led_of(m, 0, MR_NOTE_PAD0 + pad_of(0), c0), adv_ms(m, 270), led_of(m, 0, MR_NOTE_PAD0 + pad_of(0), c1);
            d0 |= memcmp(c0, c1, 3) != 0;
        }
        CHECK(d1 && !d0 && !strcmp(E[m->fw.slot_fw[0]].id, "sloop"));
    }
    /* Track 3 (empty): All, the pad of Felucca, a jog press: FW_LOAD(2, felucca); a bar until the host says. (0.8.0) Its
     * browser: Track 3's double click (its tap played the empty slot: FW_ACTIVE) */
    dbl_track(m, 2);
    CHECK(m->seq.projects && m->fw.track == 2 && (a = last(RFM_MOVE_ACT_FW_ACTIVE)) && a->a.slot == 2 && rfm_move_fw_active(m) == 2);
    tap_step(m, 1);
    rfmv_fw_bank(m, ids);
    for (k = 0; k < 32 && ids[k] != felucca; k++) {}
    CHECK(k < 32);
    tap_pad(m, pad_of(k));
    clear();
    jog_press(m);
    CHECK((a = last(RFM_MOVE_ACT_FW_LOAD)) && a->a.slot == 2 && a->a.index == 2 && !strcmp(a->id, "felucca") && a->a.value == felucca);
    CHECK(m->fw.busy_kind && !m->fw.slot_id[2][0]);
    rfm_move_fw_progress(m, "felucca", 40);
    screen(m);
    CHECK(lit_in(14, 51, 54, 55) == 40 * 4 && lit_in(54, 51, 114, 55) == 0);   /* (the bar: 40 %) */
    rfm_move_fw_list(m, E, NFW);                        /* (a new list meanwhile: the load is kept, by its id) */
    CHECK(m->fw.busy_kind && m->fw.busy_fw == felucca);
    jog_press(m);
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 1);            /* (one at a time) */
    {
        const char *const s3[3] = { "sloop", "choralroot", "felucca" };
        rfm_move_fw_slots(m, s3);
    }
    CHECK(!m->fw.busy_kind && m->fw.pop == FP_LOADED && m->fw.slot_fw[2] == felucca);
    CHECK(led_of(m, 1, MR_CC_TRACK1 - 2, c0) && (c0[0] || c0[1] || c0[2]));
    /* a refused load: Too busy */
    tap_pad(m, pad_of(0)), jog_press(m);
    rfm_move_fw_progress(m, m->fw.list[m->fw.busy_fw].id, RFM_MOVE_FW_BUSY);
    CHECK(!m->fw.busy_kind && m->fw.pop == FP_MSG && !strcmp(m->fw.pop_text, "Too busy"));
    /* Delete: unloaded at once, with its message */
    clear();
    press(m, MR_CC_DELETE);
    CHECK((a = last(RFM_MOVE_ACT_FW_UNLOAD)) && a->a.slot == 2 && !m->fw.slot_id[2][0] && m->fw.pop == FP_UNLOAD);
    press(m, MR_CC_DELETE);
    CHECK(count(RFM_MOVE_ACT_FW_UNLOAD) == 1);          /* (nothing left to unload) */
    /* Track 4: the projects (0.8.0: its double click); the active slot stays */
    clear();
    dbl_track(m, 3);
    CHECK(m->seq.projects && m->fw.track == 3 && !m->fw.fx && rfm_move_fw_active(m) == 2 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0);
    /* levels: Track held + volume (the view): FW_LEVEL; the volume alone: MASTER */
    browser(m, 0);
    clear();
    track(m, 1, 1), knob(m, 8, -8), track(m, 1, 0);
    CHECK((a = last(RFM_MOVE_ACT_FW_LEVEL)) && a->a.slot == 1 && a->a.value == 72 && m->fw.level[1] == 72);
    CHECK(count(RFM_MOVE_ACT_MASTER) == 0);
    /* (fixed in 0.8.0: fw.c view input, volume + a held Track): the Track was used, so its release is no tap: still in the
     * browser on Track 1, slot 0 still the active one (as outside the view, where on_knob marks it used) */
    CHECK(m->seq.projects && m->fw.track == 0 && rfm_move_fw_active(m) == 0 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0);
    if (!m->seq.projects)
        browser(m, 0);                                  /* (to go on as the brief has it) */
    knob(m, 8, 2);
    CHECK(count(RFM_MOVE_ACT_MASTER) == 1);
    screen(m);
    /* outside the view, the browser on: Track 2 held + volume is its level, and its release no tap */
    press(m, MR_CC_BACK);
    CHECK(!m->seq.projects && m->s.mode == RFM_MOVE_MODE_FM1);
    clear();
    track(m, 1, 1), knob(m, 8, 5), track(m, 1, 0);
    CHECK((a = last(RFM_MOVE_ACT_FW_LEVEL)) && a->a.value == 77 && m->s.mode == RFM_MOVE_MODE_FM1 && count(RFM_MOVE_ACT_MASTER) == 0);
    screen(m);
    CHECK(m->fw.pop == FP_LEVEL && lit_in(0, 59, 128, 64) > 0);
    /* voices: 1 sounds the active slot only; a loaded slot past it blinks its Track LED */
    {
        const char *const s3[3] = { "sloop", "choralroot", "felucca" };
        int t, differs = 0;
        rfm_move_fw_slots(m, s3);
        tap_track(m, 2);                                /* (Track 3 the active one: 0.8.0, its tap) */
        CHECK(rfm_move_fw_active(m) == 2 && !m->seq.projects);
        rfm_move_fw_voices(m, 1);
        CHECK(rfm_move_fw_sounding(m) == 1 << 2);      /* (the active one: Track 3) */
        rfm_move_fw_voices(m, 2);
        CHECK(rfm_move_fw_sounding(m) == ((1 << 2) | 1));
        rfm_move_fw_voices(m, 3);
        CHECK(rfm_move_fw_sounding(m) == 7);
        rfm_move_fw_voices(m, 1);
        browser(m, 2);
        for (t = 0; t < 8; t++) {
            led_of(m, 1, MR_CC_TRACK1 - 1, c0), adv_ms(m, 250), led_of(m, 1, MR_CC_TRACK1 - 1, c1);
            differs |= memcmp(c0, c1, 3) != 0;
        }
        CHECK(differs && m->seq.projects && m->fw.track == 2);   /* (Track 2: past the voices, blinking) */
        for (t = 0, differs = 0; t < 8; t++) {
            led_of(m, 1, MR_CC_TRACK1 - 2, c0), adv_ms(m, 250), led_of(m, 1, MR_CC_TRACK1 - 2, c1);
            differs |= memcmp(c0, c1, 3) != 0;
        }
        CHECK(!differs);                                /* (Track 3: the active one, steady) */
        rfm_move_fw_voices(m, 3);
        press(m, MR_CC_BACK);
    }
    /* a host with no list: Track held + volume stays MASTER (0.8.0: the release no mode change, Track 2 nothing) */
    rfm_move_init(m, NULL, on_act, NULL);
    clear();
    track(m, 1, 1), knob(m, 8, 5), track(m, 1, 0);
    CHECK(count(RFM_MOVE_ACT_FW_LEVEL) == 0 && count(RFM_MOVE_ACT_MASTER) == 1 && m->s.mode == RFM_MOVE_MODE_FM1);
}

/* ================================================================ the library === */
static void test_library(void)
{
    rfm_move_t *m = &M;
    const int golf = idx("golf"), melodee = idx("melodee");
    uint8_t c[3];
    int r;
    fixture(m);
    nlib = 0;
    /* Sampling: a favourite (Melodee is one: removed, then added at the end) */
    select_fw(m, "melodee");
    CHECK(led_of(m, 1, MR_CC_SAMPLE, c) && c[0] == 255 && c[1] == 70);
    press(m, MR_CC_SAMPLE);
    CHECK(m->fw.pop == FP_FAV && m->fw.pop_b == 0 && led_of(m, 1, MR_CC_SAMPLE, c) && c[0] < 255);
    press(m, MR_CC_SAMPLE);
    CHECK(m->fw.rec[m->fw.fav[m->fw.nfav - 1]].fw == melodee);
    /* knob 1: the colour (one every few ticks), its LED in the colour */
    select_fw(m, "golf");
    knob(m, 0, RFM_MOVE_LIST_TICKS - 1);
    CHECK(m->fw.pop == FP_COLOR && m->fw.rec[rfmv_fw_rec(m, golf)].color == 4);
    knob(m, 0, 1);
    CHECK(m->fw.rec[rfmv_fw_rec(m, golf)].color == 5);
    CHECK(led_of(m, 1, MR_CC_KNOB1, c) && c[0] == 40 && c[1] == 255 && c[2] == 70);   /* (Green) */
    /* Capture held + a step: a tag; a free step makes a new tag */
    button(m, MR_CC_CAPTURE, 1);
    tap_step(m, 11);                                    /* step 12: "Tag 5" */
    CHECK(!strcmp(m->fw.tag[12 - 8], "Tag 5") && m->fw.cap_step == 12);
    tap_step(m, 2);                                     /* step 3: Synth, against the catalog's */
    button(m, MR_CC_CAPTURE, 0);
    CHECK(m->fw.capture == 0);
    CHECK(nlib == 0);                                   /* (debounced) */
    adv_ms(m, RFM_MOVE_FW_LIB_SAVE_MS + 20);
    CHECK(nlib == 1 && lib_n > 256);
    /* the blob round-trips: a new core, the same library */
    rfm_move_init(&M2, NULL, on_act, NULL);
    CHECK(rfm_move_fw_library(&M2, lib, lib_n) == 0);
    rfm_move_fw_list(&M2, E, NFW);
    CHECK(M2.fw.nfav == m->fw.nfav && M2.fw.rec[M2.fw.fav[M2.fw.nfav - 1]].fw == melodee);
    CHECK((r = rfmv_fw_rec(&M2, golf)) >= 0 && M2.fw.rec[r].color == 5);
    CHECK(!strcmp(M2.fw.tag[4], "Tag 5") && !strcmp(M2.fw.tag[0], "Ambient"));
    CHECK(!strcmp(M2.fw.slot_id[0], "sloop") && !strcmp(M2.fw.slot_id[1], "choralroot") && M2.fw.level[1] == 80);
    {
        static uint8_t again[RFM_MOVE_FW_LIB_BYTES];
        const uint32_t n2 = rfm_move_fw_library_save(&M2, again, sizeof again);
        CHECK(n2 == lib_n && !memcmp(again, lib, lib_n));
        CHECK(rfm_move_fw_library_save(&M2, again, n2 - 1) == 0);
    }
    /* the tag survives an update (kept by id), and a firmware the catalog dropped keeps its record */
    {
        rfm_move_fw_t few[2];
        few[0] = E[golf], few[1] = E[melodee];
        rfm_move_fw_list(&M2, few, 2);
        CHECK((r = rfmv_fw_rec(&M2, 0)) >= 0 && M2.fw.rec[r].color == 5);
        CHECK(rfm_move_fw_library_save(&M2, lib, sizeof lib) > 256);
        rfm_move_fw_list(&M2, E, NFW);
    }
    /* damaged blobs: refused (the library left empty) */
    {
        static uint8_t bad[RFM_MOVE_FW_LIB_BYTES];
        const uint32_t n = rfm_move_fw_library_save(m, lib, sizeof lib);
        int k, refused = 0;
        for (k = 0; k < (int)n; k += 7) {
            memcpy(bad, lib, n);
            bad[k] ^= 0x40;
            refused += rfm_move_fw_library(&M2, bad, n) < 0;
        }
        CHECK(refused == (int)((n + 6) / 7));
        CHECK(rfm_move_fw_library(&M2, lib, n - 1) < 0 && M2.fw.nrec == 0);
        CHECK(rfm_move_fw_library(&M2, NULL, 0) == 0 && M2.fw.nfav == 0);
        CHECK(rfm_move_fw_library(&M2, lib, n) == 0 && M2.fw.nfav == m->fw.nfav);
    }
    /* leaving the view writes a pending change at once */
    nlib = 0;
    select_fw(m, "golf"), knob(m, 0, RFM_MOVE_LIST_TICKS);
    press(m, MR_CC_BACK), press(m, MR_CC_BACK);
    CHECK(nlib == 1 && !m->seq.projects);
}

/* ================================================================ the catalog === */
static void test_catalog(void)
{
    rfm_move_t *m = &M;
    const act_t *a;
    int ids[32], k, n, upd = -1, nw = -1, ok = -1;
    uint8_t c0[3], c1[3];
    fixture(m);
    press(m, MR_CC_MUTE);
    rest_pop(m);
    n = rfmv_fw_bank(m, ids);
    CHECK(n == 32);
    for (k = 0; k < 32; k++) {
        const int st = !E[ids[k]].installed ? 0 : strcmp(E[ids[k]].installed, E[ids[k]].version) ? 1 : 2;
        if (st == 1 && upd < 0)
            upd = k;
        if (st == 0 && nw < 0)
            nw = k;
        if (st == 2 && ok < 0)
            ok = k;
    }
    screen(m);
    if (nw >= 0) {                                      /* not on the Move: dashed under, faint white */
        const int x = (nw % 8) * 16, y = (nw / 8) * 16;
        CHECK(px(x + 3, y + 13) && px(x + 4, y + 13) && !px(x + 5, y + 13));
        CHECK(led_of(m, 0, MR_NOTE_PAD0 + pad_of(nw), c0) && c0[0] == c0[1] && c0[0] == c0[2] && c0[0] < 80);
    }
    if (upd < 0) {                                      /* (no update in bank 1: the next banks) */
        press(m, MR_CC_RIGHT), press(m, MR_CC_RIGHT), press(m, MR_CC_RIGHT);
        rest_pop(m);
        n = rfmv_fw_bank(m, ids);
        for (k = 0; k < n; k++)
            if (E[ids[k]].installed && strcmp(E[ids[k]].installed, E[ids[k]].version)) {
                upd = k;
                break;
            }
        screen(m);
    }
    CHECK(upd >= 0);
    if (upd >= 0) {                                     /* an update: the arrow top right, breathing */
        const int x = (upd % 8) * 16, y = (upd / 8) * 16, p = pad_of(upd);
        int t, differs = 0;
        CHECK(px(x + 12, y) && px(x + 11, y + 1) && px(x + 13, y + 1));
        for (t = 0; t < 6; t++) {
            led_of(m, 0, MR_NOTE_PAD0 + p, c0), adv_ms(m, 300), led_of(m, 0, MR_NOTE_PAD0 + p, c1);
            differs |= memcmp(c0, c1, 3) != 0;
        }
        CHECK(differs);
        /* a jog press updates it: FW_DOWNLOAD, the bar, done */
        tap_pad(m, p);
        clear();
        jog_press(m);
        CHECK((a = last(RFM_MOVE_ACT_FW_DOWNLOAD)) && a->a.value == ids[upd] && m->fw.busy_kind == 3);
        rfm_move_fw_progress(m, E[ids[upd]].id, 100);
        CHECK(!m->fw.busy_kind && m->fw.pop == FP_LOADED && m->fw.pop_b == 10);
        rfm_move_fw_installed(m, ids[upd], E[ids[upd]].version);
        CHECK(E[ids[upd]].installed == E[ids[upd]].version);
        clear();
        jog_press(m);                                   /* (0.4.2: current now: loaded on the track shown) */
        CHECK(count(RFM_MOVE_ACT_FW_DOWNLOAD) == 0 && (a = last(RFM_MOVE_ACT_FW_LOAD)) && a->a.slot == m->fw.track &&
              !strcmp(a->id, E[ids[upd]].id) && m->fw.busy_kind == 1);
        rfm_move_fw_progress(m, E[ids[upd]].id, 100);
        CHECK(!m->fw.busy_kind && m->fw.slot_fw[m->fw.track] == ids[upd]);
    }
    /* not on the Move: downloaded, then in All */
    press(m, MR_CC_LEFT), press(m, MR_CC_LEFT), press(m, MR_CC_LEFT);
    n = rfmv_fw_bank(m, ids);
    for (nw = -1, k = 0; k < n; k++)
        if (!E[ids[k]].installed) {
            nw = k;
            break;
        }
    CHECK(nw >= 0);
    tap_pad(m, pad_of(nw));
    clear();
    jog_press(m);
    CHECK((a = last(RFM_MOVE_ACT_FW_DOWNLOAD)) && !strcmp(a->id, E[ids[nw]].id) && m->fw.busy_kind == 2);
    rfm_move_fw_progress(m, E[ids[nw]].id, RFM_MOVE_FW_FAILED);
    CHECK(!m->fw.busy_kind && m->fw.pop == FP_MSG);
    jog_press(m);
    rfm_move_fw_progress(m, E[ids[nw]].id, 100);
    rfm_move_fw_installed(m, ids[nw], E[ids[nw]].version);
    press(m, MR_CC_MUTE), tap_step(m, 1);
    CHECK(rfmv_fw_list_len(m) == n_installed() + 1);
    catalog_reset();
}

/* ================================================================ FX setup === */
static void test_fx(void)
{
    rfm_move_t *m = &M;
    const act_t *a;
    uint8_t c[3];
    fixture(m);
    press(m, MR_CC_MENU);                               /* Session: FX setup (0.8.0: first on the Mixer) */
    CHECK(m->fw.fx && m->s.fx_page == 3 && m->fw.pop == FP_RPAGE);
    tap_track(m, 3);                                    /* (0.8.0) Track 4: Routing; column 1's pad: MIDI FX 1 */
    CHECK(m->fw.fx && m->s.fx_page == 1 && m->pop == POP_T4 && m->pop_i == 1);
    tap_pad(m, fxpad(0, 0));
    CHECK(m->fw.fx && m->s.fx_page == 0 && m->fw.fx_focus == 0);
    /* (0.8.0: no page on Tracks 1..3 any more) FX setup is the sequencer side: Track 4 bright, Track 1 dim */
    {
        uint8_t c4[3];
        CHECK(led_of(m, 1, MR_CC_TRACK1 - 3, c4) && led_of(m, 1, MR_CC_TRACK1, c) && c4[0] + c4[1] + c4[2] > c[0] + c[1] + c[2]);
    }
    /* steps: MIDI FX 1 acts on track 7; step 3: track 3 only; step 3 again: all */
    clear();
    tap_step(m, 2);
    CHECK(m->seq.fx.mfx[0].filt == T(3) && (a = last(RFM_MOVE_ACT_ROUTE)) && a->a.filt[3] == T(3) && a->a.filt[0] == 0x1F);
    tap_step(m, 2);
    CHECK(m->seq.fx.mfx[0].filt == 0xFFFF && m->seq.dirty);
    /* (0.5.0) the jog alone: nothing; slot 3's pad held + the jog: the module list (Move's list), let go: loaded */
    tap_pad(m, fxpad(0, 2));                            /* slot 3 (empty) */
    jog(m, 2), jog_press(m);
    CHECK(m->fw.browse < 0 && !m->fw.fx_pages && count(RFM_MOVE_ACT_FX_LOAD) == 0);
    pad(m, fxpad(0, 2), 100), jog(m, 2);
    CHECK(m->fw.browse == 2 && count(RFM_MOVE_ACT_FX_LOAD) == 0);
    screen(m), write_pbm("fx_hold_jog_choose");
    clear();
    pad(m, fxpad(0, 2), 0);
    CHECK(m->fw.browse < 0 && (a = last(RFM_MOVE_ACT_FX_LOAD)) && a->a.fx == RFM_MOVE_FX_MIDI && a->a.slot == 2 && a->a.value == 1 &&
          !strcmp(a->id, "Chord") && m->seq.fx.mfx[2].mod == 1);
    /* knobs: its parameters (a word list round, a number clamped) */
    clear();
    knob(m, 0, RFM_MOVE_LIST_TICKS * 6);                /* Shape: 5 words, +6 = Minor */
    CHECK(m->seq.fx.mfx[2].val[0] == 1 && (a = last(RFM_MOVE_ACT_FX_PARAM)) && a->a.param == 0 && a->a.value == 1);
    knob(m, 2, 60);
    knob(m, 2, 60);
    CHECK(m->seq.fx.mfx[2].val[2] == 127 && m->fw.pop == FP_PARAM);
    knob(m, 5, 3);                                      /* (no parameter 6) */
    CHECK(count(RFM_MOVE_ACT_FX_PARAM) == 3);
    /* .. held and let go without a turn: nothing loaded */
    clear();
    pad(m, fxpad(0, 2), 100), pad(m, fxpad(0, 2), 0);
    CHECK(count(RFM_MOVE_ACT_FX_LOAD) == 0);
    /* Delete alone: nothing; Delete + the pad: empties the slot */
    press(m, MR_CC_DELETE);
    CHECK(count(RFM_MOVE_ACT_FX_LOAD) == 0 && m->seq.fx.mfx[2].mod == 1);
    button(m, MR_CC_DELETE, 1), tap_pad(m, fxpad(0, 2)), button(m, MR_CC_DELETE, 0);
    CHECK((a = last(RFM_MOVE_ACT_FX_LOAD)) && a->a.value == -1 && m->seq.fx.mfx[2].mod == -1 && !m->seq.fx.mfx[2].name[0]);
    /* Routing: a firmware's pad (column 3; Track 2 too): its filter, a step toggles */
    tap_pad(m, fxpad(1, 1));
    CHECK(m->s.fx_page == 1 && m->fw.fx_focus == 1);
    clear();
    tap_step(m, 6);                                     /* track 7 out of firmware 2's 7, 8 */
    CHECK(m->seq.fx.ffilt[1] == T(8) && (a = last(RFM_MOVE_ACT_ROUTE)) && a->a.filt[1] == T(8));
    pad(m, fxpad(1, 1), 100), jog(m, 1), pad(m, fxpad(1, 1), 0), jog_press(m);   /* (no slot here) */
    button(m, MR_CC_DELETE, 1), tap_pad(m, fxpad(1, 1)), button(m, MR_CC_DELETE, 0);
    CHECK(count(RFM_MOVE_ACT_FX_LOAD) == 0);
    /* .. its knobs: level, pan, sends 1..3 (the Mixer's values) */
    knob(m, 1, -4);
    CHECK((a = last(RFM_MOVE_ACT_MIX)) && a->a.slot == 1 && m->seq.fx.pan[1] == 60 && m->fw.pop == FP_PARAM);
    knob(m, 1, 4);
    CHECK(m->s.fx_page == 1);                           /* (0.8.0: Track 2 no page any more) */
    /* Audio FX (column 5; Track 3): a dry / wet parameter's popup says 100 % wet */
    tap_pad(m, fxpad(2, 0));
    CHECK(m->s.fx_page == 2);
    knob(m, 2, -1);                                     /* Freeverb's Mix */
    screen(m);
    CHECK(m->fw.pop == FP_PARAM && lit_in(0, 56, 128, 64) > 20);
    /* (0.5.0) Mixer (0.8.0: Track 4's tap, from Routing's Audio FX column): knobs 1..3 the slots' level (FW_LEVEL); the right-hand pads pick Pan, Send 1..3 (MIX) */
    tap_track(m, 3);
    CHECK(m->fw.mix_par == RFM_MOVE_MIXP_LEVEL);
    clear();
    knob(m, 2, -10);
    CHECK((a = last(RFM_MOVE_ACT_FW_LEVEL)) && a->a.slot == 2 && a->a.value == 70);
    rest_pop(m), screen(m), write_pbm("fx_mixer_level");
    CHECK(led_of(m, 0, MR_NOTE_PAD0 + pad_of(3 * 8 + 1), c) && c[0] > 0);   /* (slot 2's meter (80): bottom lit, top dark;
                                                         * slot 3 empty: dark) */
    CHECK(led_of(m, 0, MR_NOTE_PAD0 + pad_of(1), c) && !c[0] && !c[1] && !c[2]);
    CHECK(led_of(m, 0, MR_NOTE_PAD0 + pad_of(3 * 8 + 2), c) && !c[0] && !c[1] && !c[2]);
    CHECK(led_of(m, 0, MR_NOTE_PAD0 + pad_of(4), c) && c[0] == 255);   /* (Level chosen: bright) */
    tap_pad(m, pad_of(5));                              /* Pan */
    CHECK(m->fw.mix_par == RFM_MOVE_MIXP_PAN);
    knob(m, 2, -4);
    CHECK((a = last(RFM_MOVE_ACT_MIX)) && a->a.slot == 2 && a->a.mix[0] == 70 && a->a.mix[1] == 60 && a->a.mix[2] == 30 && a->a.mix[3] == 70);
    tap_pad(m, pad_of(8 + 6));                          /* Send 3 */
    knob(m, 2, 9);
    CHECK((a = last(RFM_MOVE_ACT_MIX)) && a->a.mix[4] == 9 && m->seq.fx.send[2][2] == 9);
    knob(m, 4, 9);                                      /* (knobs 4..8: nothing) */
    CHECK(m->seq.fx.send[2][2] == 9);
    screen(m), write_pbm("fx_mixer_send3");
    tap_pad(m, pad_of(5)), rest_pop(m), screen(m), write_pbm("fx_mixer_pan");
    /* the project keeps FX setup (version 5); a project load tells the host */
    {
        static uint8_t blob[RFM_MOVE_PROJECT_MAX];
        const int n = rfm_move_project_save(m, blob, sizeof blob);
        CHECK(n == RFM_MOVE_PROJECT_FIXED && blob[4] == RFM_MOVE_PROJECT_VERSION);
        rfm_move_init(&M2, NULL, on_act, NULL);
        rfm_move_fw_list(&M2, E, NFW);
        rfm_move_fx_modules(&M2, RFM_MOVE_FX_AUDIO, AFX, 5);   /* (no MIDI FX modules: kept by name) */
        clear();
        CHECK(rfm_move_project_load(&M2, 3, blob, (uint32_t)n) == 0);
        CHECK(!memcmp(&M2.seq.fx.ffilt, &m->seq.fx.ffilt, sizeof m->seq.fx.ffilt) && M2.seq.fx.send[2][2] == 9 &&
              M2.seq.fx.pan[2] == 60 && !strcmp(M2.seq.fx.mfx[0].name, "Arp") && M2.seq.fx.mfx[0].mod == -1 &&
              M2.seq.fx.afx[0].mod == 0 && M2.seq.fx.afx[0].val[0] == 90);
        CHECK(count(RFM_MOVE_ACT_FX_LOAD) == 6 && count(RFM_MOVE_ACT_MIX) == 3 && count(RFM_MOVE_ACT_ROUTE) == 1 &&
              count(RFM_MOVE_ACT_FX_PARAM) == 3 + 3);    /* (the two audio modules' parameters) */
        rfm_move_fx_modules(&M2, RFM_MOVE_FX_MIDI, MFX, 5);
        CHECK(M2.seq.fx.mfx[0].mod == 0);               /* (found once the host has it) */
        /* a damaged FX block is refused */
        {
            const int at = n - 4 - RFM_MOVE_FXSET_BYTES + RFM_MOVE_FX_NAME + 1;   /* MIDI FX 1's 2nd value */
            uint32_t h;
            int k;
            blob[at] = 200;
            h = fnv(blob, (uint32_t)n - 4);
            for (k = 0; k < 4; k++)
                blob[n - 4 + k] = (uint8_t)(h >> (8 * k));
            CHECK(rfm_move_project_check(blob, (uint32_t)n) < 0);
        }
    }
    /* the host refuses a module: Too busy, the slot empty. (0.8.0) Track 4 again: Routing, on its column (Audio FX) */
    tap_track(m, 3);
    CHECK(m->fw.fx && m->s.fx_page == 2);
    rfm_move_fx_refused(m, RFM_MOVE_FX_AUDIO, 0);
    CHECK(m->seq.fx.afx[0].mod == -1 && m->fw.pop == FP_RLOADED);
    screen(m);
    /* a host with no modules: no MIDI FX / Audio FX page; Session again or Back leaves */
    rfm_move_fx_modules(m, RFM_MOVE_FX_MIDI, NULL, 0);
    rfm_move_fx_modules(m, RFM_MOVE_FX_AUDIO, NULL, 0);
    CHECK(m->s.fx_page == 1);
    tap_pad(m, fxpad(0, 0)), tap_pad(m, fxpad(2, 0));   /* (0.8.0: the columns of no page: nothing) */
    CHECK(m->s.fx_page == 1 && m->fw.fx);
    tap_track(m, 3);                                    /* Track 4: Routing -> the Mixer */
    CHECK(m->s.fx_page == 3 && m->pop == POP_T4 && m->pop_i == 0);   /* (named: Mixer) */
    press(m, MR_CC_MENU);
    CHECK(!m->fw.fx && m->seq.projects);
    press(m, MR_CC_MENU), press(m, MR_CC_BACK);       /* (0.4.5: Back leaves the view, FX setup remembered) */
    CHECK(!m->fw.fx && !m->seq.projects && m->s.fw_fx);
    tap_track(m, 3);                                    /* (0.8.0: Track 4 back in, on the Mixer last used) */
    CHECK(m->fw.fx && m->seq.projects && m->s.fx_page == 3);
    press(m, MR_CC_MENU);
    CHECK(!m->fw.fx && m->seq.projects);
    /* the whole setup at once (at start) */
    clear();
    rfm_move_fx_sync(m);
    CHECK(count(RFM_MOVE_ACT_FX_LOAD) == 6 && count(RFM_MOVE_ACT_MIX) == 3 && count(RFM_MOVE_ACT_ROUTE) == 1);
}

/* ================================================================ states === */
/* (0.6.6) MIDI routes on the Routing page: a firmware's pad held + another's (or the Out pad under them) toggles;
 * a cycle refused; the act carries every slot's mask; saved (version 7), older projects load with none */
static void test_mroute(void)
{
    rfm_move_t *m = &M;
    static uint8_t blob[RFM_MOVE_PROJECT_MAX];
    const act_t *a;
    uint8_t c[3];
    int n, k;
    uint32_t h;
    fixture(m);
    rfm_move_fw_slots(m, (const char *const[3]){ "sloop", "choralroot", "golf" });
    press(m, MR_CC_MENU), tap_track(m, 3), rest_pop(m), clear();   /* FX setup, the Routing page (0.8.0: Session opens
                                                         * the Mixer, Track 4's tap flips to Routing) */
    CHECK(m->fw.fx && m->s.fx_page == 1 && m->fw.fx_focus == 0);
    /* at rest: the Out pad dark */
    CHECK(led_of(m, 0, MR_NOTE_PAD0 + fxpad(1, 3), c) && !c[0] && !c[1] && !c[2]);
    /* slot 1 held: slot 2 tapped, slot 3, Out: each on (the act: every slot's mask); the focus stays */
    pad(m, fxpad(1, 0), 100);
    CHECK(led_of(m, 0, MR_NOTE_PAD0 + fxpad(1, 3), c) && c[0] < 60 && c[0] == c[1]);   /* (held: Out shown, off) */
    tap_pad(m, fxpad(1, 1));
    CHECK(m->seq.fx.mroute[0] == 2 && m->fw.fx_focus == 0 && m->fw.pop == FP_MROUTE && m->fw.pop_b == (1 | 1 << 4));
    CHECK((a = last(RFM_MOVE_ACT_ROUTE)) && a->a.mroute[0] == 2 && a->a.mroute[1] == 0 && a->a.filt[0] == m->seq.fx.ffilt[0]);
    screen(m), write_pbm("mroute_on");
    tap_pad(m, fxpad(1, 2)), tap_pad(m, fxpad(1, 3));
    CHECK(m->seq.fx.mroute[0] == (2 | 4 | RFM_MOVE_MROUTE_OUT) && rfm_move_midi_route(m, 0) == 14 && m->seq.dirty);
    CHECK(led_of(m, 0, MR_NOTE_PAD0 + fxpad(1, 3), c) && c[0] == 20 && c[1] == 220 && c[2] == 255);
    CHECK(led_of(m, 0, MR_NOTE_PAD0 + fxpad(1, 1), c) && c[0] == 20 && c[1] == 220 && c[2] == 255);
    tap_pad(m, fxpad(1, 2));                            /* (again: off) */
    CHECK(m->seq.fx.mroute[0] == (2 | RFM_MOVE_MROUTE_OUT) && (m->fw.pop_b >> 4 & 1) == 0);
    pad(m, fxpad(1, 0), 0), rest_pop(m);
    screen(m), write_pbm("mroute_rest");                /* (the routes' notches; Out faint) */
    CHECK(led_of(m, 0, MR_NOTE_PAD0 + fxpad(1, 3), c) && c[1] > 20 && c[1] < 120);
    /* a plain tap on slot 2: selected, no route changed; the Out pad alone: nothing */
    clear();
    tap_pad(m, fxpad(1, 1));
    CHECK(m->fw.fx_focus == 1 && m->seq.fx.mroute[1] == 0 && m->seq.fx.mroute[0] == 10 && !count(RFM_MOVE_ACT_ROUTE));
    tap_pad(m, fxpad(1, 3));
    CHECK(m->seq.fx.mroute[1] == 0 && !count(RFM_MOVE_ACT_ROUTE));
    /* a cycle refused: slot 2 held + slot 1 (1 -> 2 already): "Loop", nothing changed, no act */
    pad(m, fxpad(1, 1), 100), tap_pad(m, fxpad(1, 0));
    CHECK(m->seq.fx.mroute[1] == 0 && m->fw.pop == FP_MROUTE && (m->fw.pop_b >> 5 & 1) && !count(RFM_MOVE_ACT_ROUTE));
    screen(m), write_pbm("mroute_loop");
    tap_pad(m, fxpad(1, 2));                            /* (2 -> 3: fine) */
    pad(m, fxpad(1, 1), 0);
    CHECK(m->seq.fx.mroute[1] == 4);
    pad(m, fxpad(1, 2), 100), tap_pad(m, fxpad(1, 0)), pad(m, fxpad(1, 2), 0);   /* (3 -> 1: 1 -> 2 -> 3 -> 1) */
    CHECK(m->seq.fx.mroute[2] == 0 && (m->fw.pop_b >> 5 & 1));
    /* the host's edit */
    CHECK(rfm_move_midi_route_set(m, 0, 1) == -1 && rfm_move_midi_route_set(m, 0, 16) == -1 &&
          rfm_move_midi_route_set(m, 2, 1) == -1 && rfm_move_midi_route_set(m, 3, 0) == -1);   /* (own, >15, cycle) */
    clear();
    CHECK(rfm_move_midi_route_set(m, 2, RFM_MOVE_MROUTE_OUT) == 1 && (a = last(RFM_MOVE_ACT_ROUTE)) &&
          a->a.mroute[0] == 10 && a->a.mroute[1] == 4 && a->a.mroute[2] == 8);
    CHECK(rfm_move_midi_route_set(m, 2, RFM_MOVE_MROUTE_OUT) == 0 && rfm_move_midi_route(m, 3) == -1);
    /* saved and loaded (version 7; the load tells the host; 0.12.0: version 8's launch settings after them) */
    n = rfm_move_project_save(m, blob, sizeof blob);
    {                                                   /* (0.13.0: made a version 8 blob, version 9's firmwares off) */
        memmove(blob + n - 4 - RFM_MOVE_PFW_BYTES - RFM_MOVE_KIT_BYTES, blob + n - 4, 4);
        n -= RFM_MOVE_PFW_BYTES + RFM_MOVE_KIT_BYTES, blob[4] = 8;   /* (stage 3a: nor the kit) */
        for (h = 2166136261u, k = 0; k < n - 4; k++)
            h = (h ^ blob[k]) * 16777619u;
        for (k = 0; k < 4; k++)
            blob[n - 4 + k] = (uint8_t)(h >> (8 * k));
    }
    CHECK(n > 0 && blob[4] == 8 && blob[n - 4 - 257 - 3] == 10 && blob[n - 4 - 257 - 2] == 4 && blob[n - 4 - 257 - 1] == 8);
    rfm_move_init(&M2, NULL, on_act, NULL);
    rfm_move_fw_list(&M2, E, NFW);
    clear();
    CHECK(rfm_move_project_load(&M2, 0, blob, (uint32_t)n) == 0 && !memcmp(M2.seq.fx.mroute, m->seq.fx.mroute, 3));
    CHECK((a = last(RFM_MOVE_ACT_ROUTE)) && a->a.mroute[0] == 10 && a->a.mroute[2] == 8);
    /* (0.12.0) made a version 7 blob (version 8's launch settings off its end): the routes last */
    memmove(blob + n - 4 - 1 - RFM_MOVE_SEQ_CLIPS, blob + n - 4, 4);
    n -= 1 + RFM_MOVE_SEQ_CLIPS, blob[4] = 7;
    /* a cycle or an own bit in a blob: refused */
    {
        static uint8_t bad[RFM_MOVE_PROJECT_MAX];
        int j;
        for (j = 0; j < 2; j++) {
            memcpy(bad, blob, (size_t)n);
            if (j == 0)
                bad[n - 4 - 3] = 1;                     /* (slot 1 to itself) */
            else
                bad[n - 4 - 1] = 1;                     /* (3 -> 1: 1 -> 2 -> 3 -> 1) */
            for (h = 2166136261u, k = 0; k < n - 4; k++)
                h = (h ^ bad[k]) * 16777619u;
            for (k = 0; k < 4; k++)
                bad[n - 4 + k] = (uint8_t)(h >> (8 * k));
            CHECK(rfm_move_project_check(bad, (uint32_t)n) < 0);
        }
    }
    /* a version 6 blob (no routes): loads with none */
    memmove(blob + n - 4 - 3, blob + n - 4, 4);
    n -= 3, blob[4] = 6;
    for (h = 2166136261u, k = 0; k < n - 4; k++)
        h = (h ^ blob[k]) * 16777619u;
    for (k = 0; k < 4; k++)
        blob[n - 4 + k] = (uint8_t)(h >> (8 * k));
    M2.seq.fx.mroute[0] = 6;
    CHECK(rfm_move_project_load(&M2, 0, blob, (uint32_t)n) == 0 && !M2.seq.fx.mroute[0] && !M2.seq.fx.mroute[2]);
    /* no FX setup (RFM_MOVE_CAP_FX off): the host's edit refused */
    rfm_move_fw_caps(&M2, 0);
    CHECK(rfm_move_midi_route_set(&M2, 0, RFM_MOVE_MROUTE_OUT) == -1);
}
/* (0.6.9) favourites reordered with knob 2 (Favorites list): the selection follows, the ends hold, the library keeps
 * it; All and the catalog: nothing; the host's getter and setter */
static void test_fav_order(void)
{
    rfm_move_t *m = &M;
    int ids[32], n0, k, a, b, c;
    const char *fv[64];
    fixture(m);
    browser(m, 0), rest_pop(m);                         /* (0.8.0: Track 1's double click) */
    CHECK(!m->fw.catalog && !m->s.fw_scope && !m->s.fw_filter);
    n0 = rfmv_fw_bank(m, ids);
    CHECK(n0 >= 3);
    a = ids[0], b = ids[1], c = ids[2];
    {                                                   /* (0.6.13) nothing selected: knobs 1, 2 dark, a touch nothing */
        uint8_t c1[3], c2[3];
        CHECK(m->fw.sel < 0 && led_of(m, 1, MR_CC_KNOB1, c1) && !c1[0] && !c1[1] && !c1[2]);
        CHECK(led_of(m, 1, MR_CC_KNOB1 + 1, c2) && !c2[0] && !c2[1] && !c2[2]);
        touch(m, 1, 1), touch(m, 1, 0);
        CHECK(m->fw.pop != FP_ORDER);
    }
    tap_pad(m, pad_of(1));                              /* (the second favourite) */
    CHECK(m->fw.sel == b);
    {                                                   /* (0.6.13) a favourite selected: knob 2 lit (the favourites'
                                                         * colour), knob 1 its colour; a touch: their popups at once */
        uint8_t c2[3];
        CHECK(led_of(m, 1, MR_CC_KNOB1 + 1, c2) && c2[0] == 255 && c2[1] == 70 && c2[2] == 110);
        touch(m, 1, 1);
        CHECK(m->fw.pop == FP_ORDER && (m->fw.pop_b & 0xFFF) == 2 && (m->fw.pop_b & 0x1000));
        touch(m, 1, 0);
        touch(m, 0, 1);
        CHECK(m->fw.pop == FP_COLOR);
        touch(m, 0, 0), rest_pop(m);
    }
    clear(), nlib = 0;
    knob(m, 1, RFM_MOVE_LIST_TICKS);                    /* (a step down: third) */
    rfmv_fw_bank(m, ids);
    CHECK(ids[0] == a && ids[1] == c && ids[2] == b && m->fw.sel == b && m->fw.pop == FP_ORDER && m->fw.pop_b == 3);
    screen(m), write_pbm("fav_order");
    knob(m, 1, -2 * RFM_MOVE_LIST_TICKS);              /* (two up: first) */
    rfmv_fw_bank(m, ids);
    CHECK(ids[0] == b && ids[1] == a && ids[2] == c && m->fw.sel == b && m->fw.pop_b == 1);
    knob(m, 1, -RFM_MOVE_LIST_TICKS);                   /* (the top: it stays) */
    rfmv_fw_bank(m, ids);
    CHECK(ids[0] == b && m->fw.pop_b == 1);
    knob(m, 1, 1);                                      /* (short of a step: nothing) */
    rfmv_fw_bank(m, ids);
    CHECK(ids[0] == b);
    touch(m, 1, 0);
    /* the library keeps the order (FW_LIBRARY, debounced): a new core reads it */
    adv_ms(m, RFM_MOVE_FW_LIB_SAVE_MS + 20);
    CHECK(nlib == 1);
    rfm_move_init(&M2, NULL, on_act, NULL);
    CHECK(rfm_move_fw_library(&M2, lib, lib_n) == 0);
    rfm_move_fw_list(&M2, E, NFW);
    CHECK(rfm_move_fw_favorites(&M2, fv, 64) == m->fw.nfav && rfm_move_fw_favorites(m, NULL, 0) == m->fw.nfav);
    rfm_move_fw_favorites(m, fv + 32, 32);
    for (k = 0; k < m->fw.nfav && k < 32; k++)
        CHECK(!strcmp(fv[k], fv[32 + k]));
    CHECK(!strcmp(fv[0], E[b].id));
    /* the host's reorder: the given ones first, the rest as they were */
    {
        const char *want[2] = { E[c].id, "no-such-firmware" };
        CHECK(rfm_move_fw_favorites_set(m, want, 2) == 1 && rfm_move_fw_favorites_set(m, want, 2) == 0);
        rfmv_fw_bank(m, ids);
        CHECK(ids[0] == c && ids[1] == b && ids[2] == a);
        CHECK(rfm_move_fw_favorites_set(m, NULL, 1) == -1);
    }
    /* All: knob 2 does nothing (its LED off, a touch nothing) */
    tap_step(m, 1), rest_pop(m);
    CHECK(m->s.fw_scope == 1);
    tap_pad(m, pad_of(0));
    {
        uint8_t c2[3], c1[3];
        CHECK(led_of(m, 1, MR_CC_KNOB1 + 1, c2) && !c2[0] && !c2[1] && !c2[2]);
        CHECK(led_of(m, 1, MR_CC_KNOB1, c1) && (c1[0] | c1[1] | c1[2]));   /* (knob 1: its colour, a job here too) */
        touch(m, 1, 1), touch(m, 1, 0);
        CHECK(m->fw.pop != FP_ORDER);
    }
    {
        const int sel = m->fw.sel;
        uint16_t before[64];
        memcpy(before, m->fw.fav, sizeof before);
        knob(m, 1, 3 * RFM_MOVE_LIST_TICKS);
        CHECK(!memcmp(before, m->fw.fav, sizeof before) && m->fw.pop != FP_ORDER && m->fw.sel == sel);
    }
}
static void test_states(void)
{
    rfm_move_t *m = &M;
    const act_t *a;
    fixture(m);
    browser(m, 2);                                      /* (empty: Loop does nothing; 0.8.0: Track 3's double click) */
    press(m, MR_CC_LOOP);
    CHECK(m->seq.projects && m->fw.track == 2 && !m->fw.states);
    browser(m, 0);
    CHECK(m->seq.projects && m->fw.track == 0);
    rfm_move_fw_states(m, 0, 1u << 4);                  /* state 5 stored */
    press(m, MR_CC_LOOP);
    CHECK(m->fw.states);
    screen(m);
    clear();
    tap_pad(m, pad_of(4));                              /* a stored one: loaded */
    CHECK((a = last(RFM_MOVE_ACT_STATE_LOAD)) && a->a.slot == 0 && a->a.index == 4 && !strcmp(a->id, "sloop"));
    tap_pad(m, pad_of(0));                              /* an empty one: saved there */
    CHECK((a = last(RFM_MOVE_ACT_STATE_SAVE)) && a->a.index == 0 && (m->fw.st_used[0] & 1) && m->fw.st_cur[0] == 0);
    button(m, MR_CC_COPY, 1), tap_pad(m, pad_of(4)), button(m, MR_CC_COPY, 0);   /* Copy + a stored one: saved over */
    CHECK(count(RFM_MOVE_ACT_STATE_SAVE) == 2 && last(RFM_MOVE_ACT_STATE_SAVE)->a.index == 4);
    button(m, MR_CC_DELETE, 1), tap_pad(m, pad_of(4)), tap_pad(m, pad_of(9)), button(m, MR_CC_DELETE, 0);
    CHECK(count(RFM_MOVE_ACT_STATE_DELETE) == 1 && !(m->fw.st_used[0] & (1u << 4)) && count(RFM_MOVE_ACT_FW_UNLOAD) == 0);
    screen(m);
    CHECK(m->fw.pop == FP_STATE);
    tap_pad(m, pad_of(0));
    rest_pop(m), screen(m);
    CHECK(lit_in(0, 0, 16, 16) > 100 && lit_in(16 * 4, 0, 16 * 5, 16) == 4);   /* (state 1 current: filled; 5: a dot) */
    press(m, MR_CC_BACK);                               /* Back: out of States (still the view) */
    CHECK(!m->fw.states && m->seq.projects);
    rfm_move_fw_slots(m, (const char *const[3]){ "felucca", "choralroot", NULL });
    CHECK(m->fw.st_used[0] == 0);                       /* (another firmware: its own states) */
}

/* ================================================================ the sequencer's MIDI, tagged === */
static void test_midi_track(void)
{
    rfm_move_t *m = &M;
    int i, tagged = 0, other = 0, fa = 0, f8 = 0, fc = 0;
    rfm_move_init(m, NULL, on_act, NULL);
    rfmv_seq_note_put(m, SEQ_CLIP(0, 0), 0, 60, 100, 24, 0);
    rfmv_seq_note_put(m, SEQ_CLIP(5, 0), 2, 62, 100, 24, 0);
    rfmv_seq_lock_set(m, SEQ_CLIP(5, 0), 2, 74, 50);
    m->seq.tr[5].ch = 9;
    clear();
    rfm_move_seq_play(m, 1);
    adv_ms(m, 600);
    for (i = 0; i < nact; i++)
        if (acts[i].a.kind == RFM_MOVE_ACT_MIDI) {
            const act_t *x = &acts[i];
            const int ch = x->a.bytes[0] & 15;
            if (x->a.n == 1 && x->a.track == 0 && x->a.dest == RFM_MOVE_DEST_ALL && x->a.bytes[0] >= 0xF8)
                fa += x->a.bytes[0] == 0xFA, f8 += x->a.bytes[0] == 0xF8;   /* (0.9.0: the clock out, untagged) */
            else if ((ch == 0 && x->a.track == 1) || (ch == 9 && x->a.track == 6))
                tagged++;
            else
                other++;
        }
    CHECK(tagged >= 5 && other == 0);                   /* (on, off, the CC lock: each with its track) */
    CHECK(fa == 1 && f8 >= 27 && f8 <= 30);             /* (0.9.0) Start, then 24 PPQN: 48 a second at 120 BPM */
    clear();
    rfm_move_seq_play(m, 0);
    for (i = 0; i < nact; i++)
        fc += acts[i].a.kind == RFM_MOVE_ACT_MIDI && acts[i].a.n == 1 && acts[i].a.bytes[0] == 0xFC &&
              acts[i].a.dest == RFM_MOVE_DEST_ALL;
    CHECK(fc == 1);                                     /* (and Stop as it stops) */
    clear();
    adv_ms(m, 200);                                     /* (stopped: no clock) */
    CHECK(count(RFM_MOVE_ACT_MIDI) == 0);
    /* (0.9.0) Track 1 held + Scale: a note layout, which is the sequencer, on slot 1's selected track (track 1) */
    track(m, 0, 1), tap_pad(m, 24 + RFM_MOVE_LAY_SCALE), track(m, 0, 0);
    CHECK(m->s.mode == RFM_MOVE_MODE_SEQ && rfm_move_slot_layout(m, 0) == RFM_MOVE_LAY_SCALE &&
          rfm_move_seq_lock(m, 0) == 0 && m->seq.sel_track == 0 && (m->seq.fx.ffilt[0] & 1));
    clear();                                            /* a pad played live: the track's channel, untagged, slot played */
    pad(m, 0, 100), pad(m, 0, 0);
    CHECK(last(RFM_MOVE_ACT_MIDI) && last(RFM_MOVE_ACT_MIDI)->a.track == 0 && last(RFM_MOVE_ACT_MIDI)->a.dest == 0 &&
          (last(RFM_MOVE_ACT_MIDI)->a.bytes[0] & 15) == m->seq.tr[0].ch);
}

/* ================================================================ the old formats === */
static void test_old_formats(void)
{
    static uint8_t blob[RFM_MOVE_PROJECT_MAX];
    rfm_move_t *m = &M;
    int n, n4, k;
    uint32_t h;
    rfm_move_init(m, NULL, on_act, NULL);
    rfmv_seq_note_put(m, 0, 3, 64, 90, 24, 0);
    m->seq.fx.send[0][1] = 33;
    n = rfm_move_project_save(m, blob, sizeof blob);
    CHECK(blob[4] == RFM_MOVE_PROJECT_VERSION && RFM_MOVE_PROJECT_VERSION == 11);
    n4 = n - RFM_MOVE_FXSET_BYTES - RFM_MOVE_FW_SLOTS - 1 - RFM_MOVE_SEQ_CLIPS - RFM_MOVE_PFW_BYTES - RFM_MOVE_KIT_BYTES;   /* a version 4 blob: no FX setup (0.6.6:
                                                         * nor MIDI routes; 0.12.0: nor launch settings) */
    blob[4] = 4;
    for (h = 2166136261u, k = 0; k < n4 - 4; k++)
        h = (h ^ blob[k]) * 16777619u;
    for (k = 0; k < 4; k++)
        blob[n4 - 4 + k] = (uint8_t)(h >> (8 * k));
    rfm_move_init(&M2, NULL, on_act, NULL);
    M2.seq.fx.send[0][1] = 99;
    CHECK(rfm_move_project_load(&M2, 0, blob, (uint32_t)n4) == 0 && rfmv_seq_find(&M2, 0, 3, 64) >= 0);
    CHECK(M2.seq.fx.send[0][1] == 0 && M2.seq.fx.ffilt[0] == 0xFFFF && M2.seq.fx.mfx[0].filt == 0xFFFF && M2.seq.fx.pan[2] == 64);
    CHECK(rfm_move_project_check(blob, (uint32_t)n4 + 1) < 0);
    /* a version 5 (or 6) blob cut to version 4's length is refused */
    blob[4] = 5;
    CHECK(rfm_move_project_check(blob, (uint32_t)n4) < 0);
    blob[4] = 6;
    CHECK(rfm_move_project_check(blob, (uint32_t)n4) < 0);
}

/* ================================================================ the mockup's screens === */
static uint32_t hash_scr(void)
{
    uint32_t h = 0x811c9dc5u;
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 128; x++)
            h = (h ^ (uint32_t)px(x, y)) * 0x01000193u;
    return h;
}
static void write_pbm(const char *name)
{
    char path[160];
    FILE *f;
    int x, y;
    snprintf(path, sizeof path, "fw_%s.pbm", name);
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "P1\n128 64\n");
    for (y = 0; y < 64; y++, fputc('\n', f))
        for (x = 0; x < 128; x++)
            fprintf(f, "%d ", px(x, y));
    fclose(f);
}
static void three(rfm_move_t *m) { rfm_move_fw_slots(m, (const char *const[3]){ "sloop", "choralroot", "felucca" }); }
static void fxs(rfm_move_t *m, int page, int slot)       /* the mockup's FXS */
{
    three(m);
    press(m, MR_CC_MENU);                               /* (0.8.0: Session opens the Mixer; Track 4's tap: Routing) */
    if (page < 3)                                       /* (0.5.0: the slot's pad in its column) */
        tap_track(m, 3), tap_pad(m, fxpad(page, slot));
}
static void popset(rfm_move_t *m, int kind, int a, int b) { m->fw.pop = (uint8_t)kind, m->fw.pop_a = (int16_t)a, m->fw.pop_b = (int16_t)b, m->fw.pop_ms = rfmv_now_ms(m); }
static int setup(rfm_move_t *m, const char *n)
{
#define IS(s) (!strcmp(n, "fwb." s))
    fixture(m);
    rest_pop(m);
    if (IS("track_1_favorites_at_rest")) return 1;
    if (IS("name_view_sloop")) return select_fw(m, "sloop"), 1;
    if (IS("name_view_melodee")) return select_fw(m, "melodee"), 1;
    if (IS("name_view_choralroot")) return select_fw(m, "choralroot"), 1;
    if (IS("name_view_no_catalog_image")) return select_fw(m, "fizz"), 1;
    if (IS("description_top_jog_1")) return select_fw(m, "sloop"), jog(m, 1), 1;
    if (IS("description_scrolled")) return select_fw(m, "sloop"), jog(m, 60), 1;
    if (IS("knob_1_pad_colour")) return select_fw(m, "golf"), knob(m, 0, RFM_MOVE_LIST_TICKS), 1;
    if (IS("capture_held")) return select_fw(m, "elasticat"), button(m, MR_CC_CAPTURE, 1), 1;
    if (IS("capture_held_step_9")) return select_fw(m, "elasticat"), button(m, MR_CC_CAPTURE, 1), tap_step(m, 8), tap_step(m, 8), 1;
    if (IS("step_favorites_chords")) return tap_step(m, 4), 1;
    if (IS("track_tap")) return popset(m, FP_TRACK, 1, 0), 1;
    if (IS("track_tap_an_empty_track")) return popset(m, FP_TRACK, 2, 0), 1;
    if (IS("loading_into_track_3")) {
        browser(m, 2), tap_step(m, 1), select_fw(m, "felucca"), jog_press(m);
        return rfm_move_fw_progress(m, "felucca", 55), 1;
    }
    if (IS("loaded")) return browser(m, 2), select_fw(m, "felucca"), jog_press(m), rfm_move_fw_progress(m, "felucca", 100), 1;
    if (IS("track_held_volume_level")) {
        three(m);
        track(m, 2, 1), knob(m, 8, 10), track(m, 2, 0);   /* (fixed in 0.8.0: fw.c view volume): this release is taken for a
                                                         * tap (out of the view, slot 3 played): this screen then
                                                         * differs; held 400 ms it matches the mockup */
        return track(m, 1, 1), knob(m, 8, -8), 1;
    }
    if (IS("delete_unload")) return press(m, MR_CC_DELETE), 1;
    if (IS("mute_the_catalog")) return press(m, MR_CC_MUTE), 1;
    if (IS("catalog_at_rest")) return press(m, MR_CC_MUTE), rest_pop(m), 1;
    if (IS("catalog_an_update_selected")) return press(m, MR_CC_MUTE), rest_pop(m), select_fw(m, "sloop"), 1;
    if (IS("catalog_not_on_the_move")) return press(m, MR_CC_MUTE), rest_pop(m), select_fw(m, FWB_NEWFW), 1;
    if (IS("downloading")) return press(m, MR_CC_MUTE), rest_pop(m), select_fw(m, FWB_NEWFW), jog_press(m), rfm_move_fw_progress(m, FWB_NEWFW, 40), 1;
    if (IS("midi_fx_track_7_tap_again_for_all")) return fxs(m, 0, 0), popset(m, FP_RFILTER, 7, 1), 1;
    if (IS("session_midi_fx_page_track_1")) return fxs(m, 0, 0), rest_pop(m), 1;
    if (IS("routing_page_track_2_firmware_3")) return fxs(m, 1, 2), rest_pop(m), 1;
    if (IS("routing_step_6_on_firmware_3")) return fxs(m, 1, 2), popset(m, FP_RFILTER, 6, 1), 1;
    if (IS("audio_fx_track_3_choosing_a_module")) return fxs(m, 2, 2), pad(m, fxpad(2, 2), 100), jog(m, 3), 1;   /* (0.5.0: held) */
    if (IS("audio_fx_a_module_s_knob")) return fxs(m, 2, 0), knob(m, 0, 1), knob(m, 0, -1), 1;
    if (IS("mixer_track_4")) return fxs(m, 3, 1), rest_pop(m), 1;
    if (IS("mixer_a_send_knob")) return fxs(m, 3, 2), knob(m, 3, 1), knob(m, 3, -1), 1;
    if (IS("session_the_page_s_name")) return fxs(m, 3, 0), rest_pop(m), popset(m, FP_RPAGE, 3, 0), 1;   /* (0.8.0: no
                                                         * Track press names a page now: the page's popup itself) */
    if (IS("bank_changed_left_right")) return tap_step(m, 1), press(m, MR_CC_RIGHT), 1;
    if (IS("sampling_favourite")) return select_fw(m, "golf"), press(m, MR_CC_SAMPLE), press(m, MR_CC_SAMPLE), 1;
    if (IS("no_favorites_yet")) return rfm_move_fw_library(m, NULL, 0), rfm_move_fw_list(m, E, NFW), 1;
    return 0;
#undef IS
}
static void test_screens(void)
{
    rfm_move_t *m = &M;
    int i, same = 0;
    for (i = 0; i < (int)(sizeof FWB_REF / sizeof FWB_REF[0]); i++) {
        const char *n = FWB_REF[i].name;
        uint32_t h;
        CHECKF(setup(m, n), "%s: no setup", n);
        screen(m);
        h = hash_scr();
        write_pbm(n + 4);
        if (!strcmp(n, "fwb.step_favorites_chords") || !strcmp(n, "fwb.mixer_track_4") ||
            !strcmp(n, "fwb.mixer_a_send_knob") || !strcmp(n, "fwb.capture_held") ||
            !strcmp(n, "fwb.capture_held_step_9")) {    /* (0.4.2: the filter popup redrawn, test_filter_popup; 0.5.0:
                                                         * the owner's Mixer, test_fx; 0.5.4: Tags' step 16 is
                                                         * Fantasy's, always there, test_fantasy) */
            CHECK(h != FWB_REF[i].hash);
            continue;
        }
        same += h == FWB_REF[i].hash;
        CHECKF(h == FWB_REF[i].hash, "%s: %08x, the mockup %08x", n, h, FWB_REF[i].hash);
    }
    printf("move_fw: %d of %d screens as the mockup's, pixel for pixel (1 redrawn in 0.4.2, 2 in 0.5.0, 2 in 0.5.4)\n", same,
           (int)(sizeof FWB_REF / sizeof FWB_REF[0]));
    /* two of the core's own (not in the mockup): Track 4 in the view, States */
    fixture(m), dbl_track(m, 3), rest_pop(m), screen(m), write_pbm("track4_projects");
    fixture(m), rfm_move_fw_states(m, 0, 0x8013), press(m, MR_CC_LOOP), tap_pad(m, pad_of(1)), rest_pop(m), screen(m), write_pbm("states");
    fixture(m), rfm_move_fw_list(m, NULL, 0), rest_pop(m), screen(m), write_pbm("no_firmwares");
}

/* ================================================================ the host's edits (0.4.1) === */
static uint8_t scr2[MR_SCREEN_BYTES];
static void undirty(rfm_move_t *m) { m->seq.dirty = 0, m->fw.lib_dirty = 0; }
static int same_screens(rfm_move_t *a, rfm_move_t *b)
{
    screen(a), memcpy(scr2, scr, sizeof scr), screen(b);
    return !memcmp(scr, scr2, sizeof scr);
}
static void test_host_edits(void)
{
    rfm_move_t *m = &M;
    const act_t *a;
    uint32_t h0;
    static const int fwf_bad[3] = { 0x10000, -1, -1 };
    /* the active slot in the projects view: a Track press (the track shown, its popup), screen and all */
    fixture(m), fixture(&M2);
    undirty(m);
    CHECK(rfm_move_fw_set_active(m, 1) == 1);
    CHECK(m->fw.active == 1 && m->fw.track == 1 && m->fw.pop == FP_TRACK && m->fw.lib_dirty && !m->seq.dirty);
    CHECK(count(RFM_MOVE_ACT_FW_ACTIVE) == 1 && (a = last(RFM_MOVE_ACT_FW_ACTIVE)) && a->a.slot == 1 && !strcmp(a->id, "choralroot"));
    browser(&M2, 1);                                    /* (0.8.0: the Move's own: Track 2's double click) */
    CHECK(M2.fw.active == 1 && M2.fw.track == 1 && M2.seq.projects && same_screens(m, &M2));
    rest_pop(m), rest_pop(&M2);
    CHECK(same_screens(m, &M2));
    CHECK(rfm_move_fw_set_active(m, 3) == -1 && rfm_move_fw_set_active(m, -1) == -1 && m->fw.active == 1);
    /* .. in FX setup: the page stays, no Track popup; FX setup left: that firmware's track */
    press(m, MR_CC_MENU);
    clear(), undirty(m);
    CHECK(rfm_move_fw_set_active(m, 0) == 1 && m->fw.active == 0 && m->fw.fx && m->fw.pop == FP_RPAGE && m->fw.lib_dirty);
    CHECK(count(RFM_MOVE_ACT_FW_ACTIVE) == 1 && (a = last(RFM_MOVE_ACT_FW_ACTIVE)) && !strcmp(a->id, "sloop"));
    CHECK(rfm_move_fw_set_active(m, 0) == 0 && count(RFM_MOVE_ACT_FW_ACTIVE) == 1);
    press(m, MR_CC_MENU);
    CHECK(!m->fw.fx && m->fw.track == 0);

    /* a level: FW_LEVEL and the library, not the project */
    fixture(m), undirty(m);
    CHECK(rfm_move_fw_set_level(m, 1, 50) == 1);
    CHECK(m->fw.level[1] == 50 && rfm_move_fw_level(m, 1) == 50 && m->fw.lib_dirty && !m->seq.dirty);
    CHECK((a = last(RFM_MOVE_ACT_FW_LEVEL)) && a->a.slot == 1 && a->a.value == 50 && !strcmp(a->id, "choralroot"));
    clear();
    CHECK(rfm_move_fw_set_level(m, 1, 50) == 0 && nact == 0);
    CHECK(rfm_move_fw_set_level(m, 1, 128) == -1 && rfm_move_fw_set_level(m, 3, 0) == -1 && m->fw.level[1] == 50);
    /* the library's save follows (FW_LIBRARY, its level) */
    nlib = 0;
    adv_ms(m, RFM_MOVE_FW_LIB_SAVE_MS + 20);
    CHECK(nlib == 1 && lib[7] == 50 && !m->fw.lib_dirty);

    /* the mix: pan and sends are MIX and the project; the level FW_LEVEL alone, as the Mixer's knobs */
    fixture(m), undirty(m);
    CHECK(rfm_move_mix_set(m, 2, -1, 30, (const int[3]){ 10, -1, -1 }) == 1);
    CHECK(m->seq.fx.pan[2] == 30 && m->seq.fx.send[2][0] == 10 && m->seq.fx.send[2][1] == 70 && m->seq.dirty && !m->fw.lib_dirty);
    CHECK(count(RFM_MOVE_ACT_MIX) == 1 && count(RFM_MOVE_ACT_FW_LEVEL) == 0 && (a = last(RFM_MOVE_ACT_MIX)) && a->a.slot == 2 &&
          a->a.mix[0] == 80 && a->a.mix[1] == 30 && a->a.mix[2] == 10 && a->a.mix[3] == 70 && a->a.mix[4] == 0);
    clear(), undirty(m);
    CHECK(rfm_move_mix_set(m, 2, 70, -1, NULL) == 1);
    CHECK(count(RFM_MOVE_ACT_MIX) == 0 && count(RFM_MOVE_ACT_FW_LEVEL) == 1 && m->fw.lib_dirty && !m->seq.dirty);
    clear();
    CHECK(rfm_move_mix_set(m, 2, 70, 30, (const int[3]){ 10, 70, 0 }) == 0 && nact == 0);
    CHECK(rfm_move_mix_set(m, 2, -2, -1, NULL) == -1 && rfm_move_mix_set(m, 0, -1, -1, (const int[3]){ 0, 128, 0 }) == -1 &&
          rfm_move_mix_set(m, 3, -1, -1, NULL) == -1 && nact == 0);
    /* .. the same as the Move's own Mixer */
    fixture(&M2), press(&M2, MR_CC_MENU);              /* (0.8.0: Session opens on the Mixer) */
    CHECK(M2.fw.fx && M2.s.fx_page == 3);
    knob(&M2, 2, -10), tap_pad(&M2, pad_of(5)), knob(&M2, 2, -34), tap_pad(&M2, pad_of(12)), knob(&M2, 2, -20);   /* (send 1: 30 -> 10) */
    CHECK(!memcmp(&m->seq.fx, &M2.seq.fx, sizeof m->seq.fx) && M2.fw.level[2] == m->fw.level[2]);

    /* the routing: one ROUTE, the project; a MIDI FX filter is one track or all */
    fixture(m), undirty(m);
    CHECK(rfm_move_route_set(m, (const int[3]){ 0x3, -1, 0 }, (const int[3]){ -1, 0x30, T(4) }) == 0);   /* (not shown) */
    CHECK(m->seq.fx.ffilt[0] == 3 && m->seq.fx.ffilt[1] == (T(7) | T(8)) && m->seq.fx.ffilt[2] == 0 &&
          m->seq.fx.mfx[0].filt == T(7) && m->seq.fx.mfx[1].filt == 0xFFFF && m->seq.fx.mfx[2].filt == T(4) && m->seq.dirty);
    CHECK(count(RFM_MOVE_ACT_ROUTE) == 1 && (a = last(RFM_MOVE_ACT_ROUTE)) && a->a.filt[0] == 3 && a->a.filt[2] == 0 &&
          a->a.filt[3] == T(7) && a->a.filt[4] == 0xFFFF && a->a.filt[5] == T(4));
    clear(), undirty(m);
    CHECK(rfm_move_route_set(m, (const int[3]){ 0x3, -1, 0 }, NULL) == 0 && nact == 0 && !m->seq.dirty);
    CHECK(rfm_move_route_set(m, fwf_bad, NULL) == -1 && m->seq.fx.ffilt[0] == 3);
    press(m, MR_CC_MENU), tap_track(m, 3), rest_pop(m);    /* FX setup's Routing on screen (0.8.0: Track 4 flips) */
    CHECK(m->fw.fx && m->s.fx_page == 1);
    screen(m), h0 = hash_scr();
    CHECK(rfm_move_route_set(m, (const int[3]){ 0xFFFF, -1, -1 }, NULL) == 1);
    screen(m);
    CHECK(hash_scr() != h0);

    /* a module: fx_load's defaults, FX_LOAD (no FX_PARAM), the project; as the jog's press */
    fixture(m), fixture(&M2);
    press(m, MR_CC_MENU), tap_track(m, 3), tap_pad(m, fxpad(0, 2)), rest_pop(m);   /* (0.8.0: via Routing's columns) */
    CHECK(m->fw.fx && m->s.fx_page == 0 && m->fw.fx_focus == 2);
    press(&M2, MR_CC_MENU), tap_track(&M2, 3), pad(&M2, fxpad(0, 2), 100), jog(&M2, 1), pad(&M2, fxpad(0, 2), 0), rest_pop(&M2);
    clear(), undirty(m);
    screen(m), h0 = hash_scr();
    CHECK(rfm_move_fx_set_module(m, RFM_MOVE_FX_MIDI, 2, 0) == 1);
    CHECK(m->seq.fx.mfx[2].mod == 0 && !strcmp(m->seq.fx.mfx[2].name, "Arp") && m->seq.fx.mfx[2].val[3] == 64 && m->seq.dirty);
    CHECK(count(RFM_MOVE_ACT_FX_LOAD) == 1 && count(RFM_MOVE_ACT_FX_PARAM) == 0 && (a = last(RFM_MOVE_ACT_FX_LOAD)) &&
          a->a.fx == RFM_MOVE_FX_MIDI && a->a.slot == 2 && a->a.value == 0 && !strcmp(a->id, "Arp"));
    CHECK(!memcmp(&m->seq.fx, &M2.seq.fx, sizeof m->seq.fx) && same_screens(m, &M2));
    screen(m);
    CHECK(hash_scr() != h0);
    /* .. a parameter: clamped as the knob clamps, FX_PARAM, the project */
    clear(), undirty(m);
    CHECK(rfm_move_fx_set_param(m, RFM_MOVE_FX_MIDI, 2, 0, 7) == 1);   /* Rate: 3 words */
    CHECK(m->seq.fx.mfx[2].val[0] == 2 && m->seq.dirty && (a = last(RFM_MOVE_ACT_FX_PARAM)) && a->a.fx == RFM_MOVE_FX_MIDI &&
          a->a.slot == 2 && a->a.param == 0 && a->a.value == 2);
    CHECK(rfm_move_fx_set_param(m, RFM_MOVE_FX_MIDI, 2, 3, 200) == 1 && m->seq.fx.mfx[2].val[3] == 127);
    clear();
    CHECK(rfm_move_fx_set_param(m, RFM_MOVE_FX_MIDI, 2, 3, 127) == 0 && nact == 0);
    CHECK(rfm_move_fx_set_param(m, RFM_MOVE_FX_MIDI, 2, 4, 1) == -1);   /* (Arp has 4) */
    CHECK(rfm_move_fx_set_param(m, RFM_MOVE_FX_AUDIO, 2, 0, 1) == -1);  /* (empty) */
    CHECK(rfm_move_fx_set_module(m, RFM_MOVE_FX_AUDIO, 0, 5) == -1 && rfm_move_fx_set_module(m, 2, 0, 0) == -1 &&
          rfm_move_fx_set_module(m, RFM_MOVE_FX_MIDI, 3, 0) == -1 && nact == 0);
    /* .. the same module again: its defaults back (as the Move); -1 empties */
    CHECK(rfm_move_fx_set_module(m, RFM_MOVE_FX_MIDI, 2, 0) == 1 && m->seq.fx.mfx[2].val[0] == 0 && m->seq.fx.mfx[2].val[3] == 64);
    CHECK(rfm_move_fx_set_module(m, RFM_MOVE_FX_MIDI, 2, -1) == 1 && m->seq.fx.mfx[2].mod == -1 && !m->seq.fx.mfx[2].name[0] &&
          (a = last(RFM_MOVE_ACT_FX_LOAD)) && a->a.value == -1);
    /* the page: SETTINGS, as a column's pad in FX setup */
    clear();
    screen(m), h0 = hash_scr();
    CHECK(rfm_move_fx_set_page(m, RFM_MOVE_FX_PAGE_AUDIO) == 1 && m->s.fx_page == 2 && count(RFM_MOVE_ACT_SETTINGS) == 1);
    screen(m);
    CHECK(hash_scr() != h0);
    CHECK(rfm_move_fx_set_page(m, RFM_MOVE_FX_PAGE_AUDIO) == 0 && count(RFM_MOVE_ACT_SETTINGS) == 1);
    CHECK(rfm_move_fx_set_page(m, 4) == -1);
    rfm_move_fx_modules(m, RFM_MOVE_FX_MIDI, NULL, 0);
    CHECK(rfm_move_fx_set_page(m, RFM_MOVE_FX_PAGE_MIDI) == -1 && rfm_move_fx_set_module(m, RFM_MOVE_FX_MIDI, 0, 0) == -1);

    /* the projects view not on screen: done, nothing to redraw */
    fixture(m);
    press(m, MR_CC_BACK);
    CHECK(!m->seq.projects);
    clear();
    CHECK(rfm_move_fw_set_level(m, 0, 10) == 0 && rfm_move_fx_set_module(m, RFM_MOVE_FX_AUDIO, 2, 1) == 0 &&
          rfm_move_fx_set_page(m, RFM_MOVE_FX_PAGE_AUDIO) == 0 && rfm_move_fw_set_active(m, 2) == 0);
    CHECK(count(RFM_MOVE_ACT_FW_LEVEL) == 1 && count(RFM_MOVE_ACT_FX_LOAD) == 1 && count(RFM_MOVE_ACT_SETTINGS) == 1 &&
          count(RFM_MOVE_ACT_FW_ACTIVE) == 1 && m->fw.active == 2 && m->s.fx_page == 2);   /* (0.8.0: the Mixer is the
                                                         * default page now: Audio FX a change) */
}

/* ---- (0.4.1) no favourites: the view shows All; the setting keeps the user's choice */
static void reenter(rfm_move_t *m) { press(m, MR_CC_BACK), browser(m, 0); }   /* (0.8.0: Track 1's double click) */
static void test_no_favs(void)
{
    rfm_move_t *m = &M;
    fixture(m);
    m->s.fw_scope = 0, reenter(m);                      /* favourites: the saved choice (Favorites) */
    CHECK(m->seq.projects && m->fw.track == 0 && !m->fw.all_auto && rfmv_fw_list_len(m) == m->fw.nfav && m->fw.nfav > 1);
    m->s.fw_scope = 1, reenter(m);
    CHECK(!m->fw.all_auto && rfmv_fw_list_len(m) == n_installed());
    /* none: All on entry, the setting kept */
    m->s.fw_scope = 0, m->fw.nfav = 0, reenter(m);
    CHECK(m->fw.all_auto && m->s.fw_scope == 0 && rfmv_fw_list_len(m) == n_installed());
    screen(m);
    CHECK(lit_in(0, 0, 128, 64) > 0);
    /* step 1 pressed: the user's choice holds (an empty Favorites) */
    tap_step(m, 0);
    CHECK(!m->fw.all_auto && rfmv_fw_list_len(m) == 0);
    /* .. until the next entry, or another firmware track (0.8.0: Track 2's double click) */
    browser(m, 1);
    CHECK(m->fw.all_auto && rfmv_fw_list_len(m) == n_installed());
    /* the last favourite removed (Sampling): All at once; one added again: All stays until the next entry */
    fixture(m);
    m->s.fw_scope = 0, m->fw.nfav = 1, reenter(m);
    CHECK(!m->fw.all_auto && rfmv_fw_list_len(m) == 1);
    m->fw.sel = (int16_t)idx(m->fw.rec[m->fw.fav[0]].id);
    press(m, MR_CC_SAMPLE);
    CHECK(m->fw.nfav == 0 && m->fw.all_auto && m->s.fw_scope == 0 && rfmv_fw_list_len(m) == n_installed());
    press(m, MR_CC_SAMPLE);
    CHECK(m->fw.nfav == 1 && m->fw.all_auto && rfmv_fw_list_len(m) == n_installed());
    reenter(m);
    CHECK(!m->fw.all_auto && rfmv_fw_list_len(m) == 1);
    /* the catalog (Mute) is unchanged */
    m->fw.nfav = 0, reenter(m), press(m, MR_CC_MUTE);
    CHECK(m->fw.catalog && rfmv_fw_list_len(m) > n_installed());
}

/* ================================================================ the filter popup (0.4.2) === */
static char fname[24], frows[3][22];
static const char *fstate;
static int fk;
static void words(rfm_move_t *m, int step) { fk = rfmv_fw_filter_text(m, step, fname, &fstate, frows); }
static void test_filter_popup(void)
{
    rfm_move_t *m = &M;
    uint32_t h0;
    /* one filter: the step's name big, ON, what is shown */
    fixture(m);
    tap_step(m, 4);                                     /* step 5: Chords */
    CHECK(m->fw.pop == FP_FILTER && m->fw.pop_a == 5);
    words(m, 5);
    CHECK(!strcmp(fname, "Chords") && !strcmp(fstate, "ON") && fk == 2 && !strcmp(frows[0], "Showing: Chords,") &&
          !strcmp(frows[1], "favorites"));
    screen(m), write_pbm("filter_one");
    /* another category replaces it; the lit one again: all categories */
    tap_step(m, 2);                                     /* Synth */
    CHECK(m->fw.pop_a == 3 && m->s.fw_filter == T(3));
    words(m, 3);
    CHECK(!strcmp(fname, "Synth") && !strcmp(fstate, "ON") && fk == 2 && !strcmp(frows[0], "Showing: Synth,") &&
          !strcmp(frows[1], "favorites"));
    words(m, 5);
    CHECK(!strcmp(fstate, "off"));
    tap_step(m, 2);
    words(m, 3);
    CHECK(!strcmp(fstate, "off") && fk == 1 && !strcmp(frows[0], "Showing: favorites"));
    screen(m), write_pbm("filter_all_categories");
    tap_step(m, 1);                                     /* All */
    words(m, 2);
    CHECK(!strcmp(fname, "All") && !strcmp(fstate, "ON") && fk == 2 && !strcmp(frows[0], "Showing: all") &&
          !strcmp(frows[1], "installed"));
    tap_step(m, 5);
    words(m, 6);
    CHECK(!strcmp(fname, "Drums") && !strcmp(frows[0], "Showing: Drums,") && !strcmp(frows[1], "installed"));
    /* the catalog's scope: Update ready (on the Move, a newer version in the catalog) or Not installed */
    fixture(m), press(m, MR_CC_MUTE), rest_pop(m);
    tap_step(m, 0);
    words(m, 1);
    CHECK(!strcmp(fname, "Update ready") && !strcmp(fstate, "ON") && fk == 1 && !strcmp(frows[0], "Showing: update ready"));
    screen(m), write_pbm("filter_update_ready");
    tap_step(m, 1);
    words(m, 2);
    CHECK(!strcmp(fname, "Not installed") && !strcmp(fstate, "ON") && fk == 2 && !strcmp(frows[0], "Showing: not") &&
          !strcmp(frows[1], "installed"));
    words(m, 1);
    CHECK(!strcmp(fstate, "off"));
    tap_step(m, 1);
    words(m, 2);
    CHECK(!strcmp(fstate, "off") && !strcmp(frows[0], "Showing: the whole") && !strcmp(frows[1], "catalog"));
    tap_step(m, 1), tap_step(m, 2);
    words(m, 3);
    CHECK(!strcmp(fname, "Synth") && !strcmp(frows[0], "Showing: Synth, not") && !strcmp(frows[1], "installed"));
    screen(m), write_pbm("filter_catalog_synth");
    /* a hold shows the step without changing it; a tap toggles on release */
    fixture(m), rest_pop(m);
    screen(m), h0 = hash_scr();
    clear();
    step(m, 4, 1);
    CHECK(m->fw.hold_step == 5 && !(m->s.fw_filter & T(5)));
    adv_ms(m, RFM_MOVE_TAP_MS + 20);
    CHECK(rfm_move_screen_timed(m));
    screen(m), write_pbm("filter_hold");
    CHECK(hash_scr() != h0);
    step(m, 4, 0);
    CHECK(!m->fw.hold_step && !(m->s.fw_filter & T(5)) && count(RFM_MOVE_ACT_SETTINGS) == 0 && m->fw.pop == FP_FILTER &&
          m->fw.pop_a == 5);
    words(m, 5);
    CHECK(!strcmp(fstate, "off"));
    step(m, 4, 1), adv_ms(m, 50), step(m, 4, 0);        /* a tap */
    CHECK((m->s.fw_filter & T(5)) && count(RFM_MOVE_ACT_SETTINGS) == 1);
    step(m, 14, 1);                                     /* (step 15: no tag made; 0.5.4: step 16 is Fantasy's) */
    CHECK(!m->fw.hold_step);
    step(m, 14, 0);
    /* a tag's name at its longest: never cut, small when past 10 */
    fixture(m), rest_pop(m);
    strcpy(m->fw.tag[0], "Late night pads");            /* (RFM_MOVE_FW_TAG_NAME - 1) */
    tap_step(m, 7);
    words(m, 8);
    CHECK(!strcmp(fname, "Late night pads") && !strcmp(frows[0], "Showing: Late night") && !strcmp(frows[1], "pads, favorites"));
    screen(m), write_pbm("filter_long_tag");
    rest_pop(m), select_fw(m, "elasticat"), button(m, MR_CC_CAPTURE, 1), tap_step(m, 7);
    screen(m), write_pbm("capture_long_tag");
    CHECK(lit_in(0, 20, 128, 28) > 0);
    button(m, MR_CC_CAPTURE, 0);
}

/* ================================================================ download, then load (0.4.2) === */
static void test_download_load(void)
{
    rfm_move_t *m = &M;
    const act_t *a;
    const int nf = idx(FWB_NEWFW);
    int other;
    CHECK(nf >= 0 && !FWB[nf].installed);
    /* not on the Move: the jog downloads; done and still selected: the popup offers the load; the jog loads */
    fixture(m), browser(m, 1), press(m, MR_CC_MUTE), rest_pop(m), select_fw(m, FWB_NEWFW);   /* (0.8.0: Track 2 double) */
    clear();
    jog_press(m);
    CHECK((a = last(RFM_MOVE_ACT_FW_DOWNLOAD)) && !strcmp(a->id, FWB_NEWFW) && count(RFM_MOVE_ACT_FW_LOAD) == 0);
    rfm_move_fw_progress(m, FWB_NEWFW, 100);
    rfm_move_fw_installed(m, nf, E[nf].version);
    CHECK(m->fw.pop == FP_LOADED && m->fw.pop_a == nf && count(RFM_MOVE_ACT_FW_LOAD) == 0);
    screen(m), write_pbm("download_offer");
    CHECK(lit_in(0, 46, 128, 54) > 0);
    jog_press(m);
    CHECK((a = last(RFM_MOVE_ACT_FW_LOAD)) && a->a.slot == 1 && !strcmp(a->id, FWB_NEWFW) && count(RFM_MOVE_ACT_FW_DOWNLOAD) == 1);
    rfm_move_fw_progress(m, FWB_NEWFW, 100);
    CHECK(m->fw.slot_fw[1] == nf && m->fw.pop == FP_LOADED && m->fw.pop_b == 1);
    /* installed and current: the jog loads at once */
    select_fw(m, "golf");
    clear();
    jog_press(m);
    CHECK((a = last(RFM_MOVE_ACT_FW_LOAD)) && a->a.slot == 1 && !strcmp(a->id, "golf") && count(RFM_MOVE_ACT_FW_DOWNLOAD) == 0);
    rfm_move_fw_progress(m, "golf", 100);
    /* an update ready: the jog updates (no load) */
    select_fw(m, "sloop");
    CHECK(E[idx("sloop")].installed && strcmp(E[idx("sloop")].installed, E[idx("sloop")].version));
    clear();
    jog_press(m);
    CHECK(count(RFM_MOVE_ACT_FW_DOWNLOAD) == 1 && count(RFM_MOVE_ACT_FW_LOAD) == 0 && m->fw.busy_kind == 3);
    rfm_move_fw_progress(m, "sloop", 100);
    rfm_move_fw_installed(m, idx("sloop"), E[idx("sloop")].version);
    screen(m);
    CHECK(m->fw.pop_b == 10 && lit_in(0, 46, 128, 54) > 0);   /* (still selected: the offer) */
    /* moved on before it finished: no offer, nothing loaded */
    fixture(m), press(m, MR_CC_MUTE), rest_pop(m), select_fw(m, FWB_NEWFW), jog_press(m);
    other = idx("golf");
    m->fw.sel = (int16_t)other;                         /* (another pad) */
    clear();
    rfm_move_fw_progress(m, FWB_NEWFW, 100);
    rfm_move_fw_installed(m, nf, E[nf].version);
    screen(m);
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 0 && m->fw.pop == FP_LOADED && lit_in(0, 46, 128, 54) == 0);
    fixture(m), press(m, MR_CC_MUTE), rest_pop(m), select_fw(m, FWB_NEWFW), jog_press(m);
    press(m, MR_CC_MUTE);                               /* (the catalog left) */
    clear();
    rfm_move_fw_progress(m, FWB_NEWFW, 100);
    rfm_move_fw_installed(m, nf, E[nf].version);
    screen(m);
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 0 && !m->fw.catalog && lit_in(0, 46, 128, 54) == 0);
    catalog_reset();
}

/* (0.9.0, the owner's) the browser: a pad tapped twice within RFM_MOVE_DOUBLE_MS (the same pad) loads it, as the jog's
 * press; the browser stays. In the catalog only one on the Move and current (a download is still the jog's) */
enum { DL_OK, DL_NEW, DL_UPD };
static int dl_kind(int i)
{
    return !E[i].installed || !E[i].installed[0] ? DL_NEW : E[i].version && strcmp(E[i].installed, E[i].version) ? DL_UPD : DL_OK;
}
static int dl_find(rfm_move_t *m, int kind, int not_fw)   /* a cell of the bank shown (Right: the next), or -1 */
{
    int ids[32], n, k, tries;
    for (tries = 0; tries < 8; tries++, press(m, MR_CC_RIGHT))
        for (n = rfmv_fw_bank(m, ids), k = 0; k < n; k++)
            if (ids[k] >= 0 && ids[k] != not_fw && dl_kind(ids[k]) == kind)
                return k;
    return -1;
}
/* (the browser times its taps on the sequencer's clock, the Track buttons on the input's: both, as on a Move) */
static void wait_ms(rfm_move_t *m, uint32_t ms) { now_ms += ms, adv_ms(m, ms); }
static void test_dbl_load(void)
{
    rfm_move_t *m = &M;
    const act_t *a;
    int ids[32], k, j;
    fixture(m), browser(m, 1), rest_pop(m);
    CHECK(m->seq.projects && m->fw.track == 1 && !m->fw.catalog);
    k = dl_find(m, DL_OK, m->fw.slot_fw[1]);
    CHECK(k >= 0);
    if (k < 0)
        return;
    rfmv_fw_bank(m, ids);
    clear();
    tap_pad(m, pad_of(k)), wait_ms(m, RFM_MOVE_DOUBLE_MS + 50), tap_pad(m, pad_of(k));   /* (too slow: selected only) */
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 0 && m->fw.sel == ids[k]);
    j = (k + 1) % rfmv_fw_bank(m, ids);                 /* (another pad, then this one: no load) */
    wait_ms(m, RFM_MOVE_DOUBLE_MS + 50), tap_pad(m, pad_of(j)), tap_pad(m, pad_of(k));
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 0 && m->fw.sel == ids[k]);
    wait_ms(m, RFM_MOVE_DOUBLE_MS + 50), tap_pad(m, pad_of(k)), tap_pad(m, pad_of(k));
    CHECK((a = last(RFM_MOVE_ACT_FW_LOAD)) && count(RFM_MOVE_ACT_FW_LOAD) == 1 && a->a.slot == 1 &&
          !strcmp(a->id, E[ids[k]].id) && m->seq.projects && !m->fw.states && m->fw.busy_kind == 1);
    tap_pad(m, pad_of(k));                              /* (a third tap: no second load) */
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 1);
    rfm_move_fw_progress(m, E[ids[k]].id, 100);
    CHECK(!m->fw.busy_kind && m->fw.slot_fw[1] == ids[k] && m->seq.projects);
    /* the catalog: not on the Move, an update: nothing (the jog's); current: loaded */
    press(m, MR_CC_MUTE), rest_pop(m);
    CHECK(m->fw.catalog);
    for (j = DL_NEW; j <= DL_UPD; j++) {
        k = dl_find(m, j, -1);
        CHECK(k >= 0);
        if (k < 0)
            continue;
        rfmv_fw_bank(m, ids);
        clear();
        wait_ms(m, RFM_MOVE_DOUBLE_MS + 50), tap_pad(m, pad_of(k)), tap_pad(m, pad_of(k));
        CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 0 && count(RFM_MOVE_ACT_FW_DOWNLOAD) == 0 && m->fw.sel == ids[k] &&
              !m->fw.busy_kind);
    }
    k = dl_find(m, DL_OK, m->fw.slot_fw[1]);
    CHECK(k >= 0);
    if (k < 0)
        return;
    rfmv_fw_bank(m, ids);
    clear();
    wait_ms(m, RFM_MOVE_DOUBLE_MS + 50), tap_pad(m, pad_of(k)), tap_pad(m, pad_of(k));
    CHECK((a = last(RFM_MOVE_ACT_FW_LOAD)) && a->a.slot == 1 && !strcmp(a->id, E[ids[k]].id) &&
          count(RFM_MOVE_ACT_FW_DOWNLOAD) == 0 && m->seq.projects && m->fw.catalog);
    rfm_move_fw_progress(m, E[ids[k]].id, 100);
    catalog_reset();
}

/* ================================================================ Back (0.4.3) === */
static void test_back(void)
{
    rfm_move_t *m = &M;
    const act_t *a;
    uint8_t base[MR_SCREEN_BYTES];
    const int mode0 = (fixture(m), m->s.mode);
    /* the view at its root: a click flips back to the mode. (0.9.0) Another (nothing open): nothing at all (no
     * flip to a Sequencer, nor into the view) */
    CHECK(m->seq.projects && m->fw.track == 0 && m->fw.sel < 0);
    press(m, MR_CC_BACK);
    CHECK(!m->seq.projects && m->s.mode == mode0 && m->pop == POP_LAYOUT && count(RFM_MOVE_ACT_BTN) == 0);
    rest_pop(m), screen(m), memcpy(base, scr, sizeof base);
    press(m, MR_CC_BACK), press(m, MR_CC_BACK);
    screen(m);
    CHECK(!m->seq.projects && m->s.mode == mode0 && count(RFM_MOVE_ACT_BTN) == 0 && !memcmp(scr, base, sizeof base));
    browser(m, 0);
    /* not at its root: one level up first (the description, the selection; FX setup) */
    select_fw(m, "golf"), jog(m, 1);
    press(m, MR_CC_BACK);
    CHECK(m->seq.projects && m->fw.sel >= 0 && m->fw.jog == 0);
    press(m, MR_CC_BACK);
    CHECK(m->seq.projects && m->fw.sel < 0);
    press(m, MR_CC_MENU);
    CHECK(m->fw.fx);
    press(m, MR_CC_BACK);                               /* (0.4.5: FX setup is no layer: out at once) */
    CHECK(!m->seq.projects && !m->fw.fx && m->s.fw_fx);
    tap_track(m, 3), press(m, MR_CC_MENU), press(m, MR_CC_BACK);   /* (0.8.0: in on FX setup by Track 4, Menu off it, out) */
    CHECK(!m->seq.projects && !m->s.fw_fx && m->s.mode == mode0);
    /* a long hold, alone: nothing; a Track held meanwhile: no click */
    button(m, MR_CC_BACK, 1), now_ms += RFM_MOVE_TAP_MS + 50, button(m, MR_CC_BACK, 0);
    CHECK(!m->seq.projects && m->s.mode == mode0);
    track(m, 3, 1), press(m, MR_CC_BACK), now_ms += 400, track(m, 3, 0);
    CHECK(!m->seq.projects && m->s.mode == mode0);
    /* (0.8.0: Back + Track is gone) Track 2's tap: that slot's firmware played, no mode change; its popup */
    rest_pop(m);
    clear();
    tap_track(m, 1);
    CHECK(m->fw.active == 1 && (a = last(RFM_MOVE_ACT_FW_ACTIVE)) && a->a.slot == 1 && !strcmp(a->id, "choralroot") &&
          !m->seq.projects && m->s.mode == mode0 && m->pop == POP_LAYOUT && m->pop_i == 1 && m->fw.lib_dirty);
    screen(m), write_pbm("back_track_firmware");
    CHECK(lit_in(0, 0, 128, 64) > 0);
    /* .. Back held over a Track's tap: Back no click (no flip), the tap as ever */
    clear();
    now_ms += RFM_MOVE_DOUBLE_MS + 50;
    button(m, MR_CC_BACK, 1), track(m, 0, 1), button(m, MR_CC_BACK, 0), track(m, 0, 0);
    CHECK(m->fw.active == 0 && count(RFM_MOVE_ACT_FW_ACTIVE) == 1 && !m->seq.projects && m->s.mode == mode0);
    /* .. in the view: a Track's tap leaves it and plays that slot */
    browser(m, 0);
    clear();
    tap_track(m, 1);
    CHECK(!m->seq.projects && m->fw.active == 1 && count(RFM_MOVE_ACT_FW_ACTIVE) == 1 && m->s.mode == mode0);
    /* the sequencer: Back ends Loop's latch first, the sequencer's screen at once. (0.9.0: the sequencer is slot 2's
     * note layout, on its selected track, 2) */
    rfm_move_set_slot_layout(m, 1, RFM_MOVE_LAY_DRUM);
    CHECK(m->s.mode == RFM_MOVE_MODE_SEQ && m->seq.sel_track == 1 && !m->seq.view && m->seq.trig);
    rest_pop(m), screen(m), memcpy(base, scr, sizeof base);
    press(m, MR_CC_LOOP);
    CHECK(m->seq.loop_latch && rfmv_pop_up(m, POP_LOOP));
    screen(m);
    CHECK(memcmp(scr, base, sizeof base) != 0);
    press(m, MR_CC_BACK);
    CHECK(!m->seq.loop_latch && !m->seq.projects);
    screen(m);                                          /* (the frame right after: the sequencer, not the Loop popup) */
    CHECK(!memcmp(scr, base, sizeof base));
    press(m, MR_CC_LOOP), press(m, MR_CC_LOOP);         /* Loop's own exit: at once too */
    screen(m);
    CHECK(!m->seq.loop_latch && !memcmp(scr, base, sizeof base));
    button(m, MR_CC_LOOP, 1), jog(m, 1);                /* Loop held (the length), let go: at once */
    CHECK(rfmv_pop_up(m, POP_LOOP));
    button(m, MR_CC_LOOP, 0);
    screen(m);
    CHECK(!rfmv_pop_up(m, POP_LOOP));
    jog(m, -1), rest_pop(m), screen(m), memcpy(base, scr, sizeof base);
    clear();
    press(m, MR_CC_BACK);                               /* (0.9.0) nothing open: nothing (no flip) */
    screen(m);
    CHECK(!m->seq.projects && m->s.mode == RFM_MOVE_MODE_SEQ && count(RFM_MOVE_ACT_BTN) == 0 &&
          !memcmp(scr, base, sizeof base));
    press(m, MR_CC_MENU);                               /* (0.9.0) session view; (0.12.0) Back: nothing, Menu closes it */
    CHECK(m->seq.view);
    press(m, MR_CC_BACK);
    CHECK(m->seq.view);
    press(m, MR_CC_MENU);
    rest_pop(m), screen(m);
    CHECK(!m->seq.view && m->s.mode == RFM_MOVE_MODE_SEQ && !memcmp(scr, base, sizeof base));
    /* Shift + step 1 still opens the view; out: the note layout again */
    button(m, MR_CC_SHIFT, 1), tap_step(m, 0), button(m, MR_CC_SHIFT, 0);
    CHECK(m->seq.projects);
    press(m, MR_CC_BACK);
    CHECK(!m->seq.projects && m->s.mode == RFM_MOVE_MODE_SEQ && m->pop == POP_LAYOUT && m->pop_i == 1);
}

/* (0.15.0, the owner's 2026-11-25) Down in FM-1 mode: nothing (0.4.4 .. 0.14.0: the next loaded firmware; the Track
 * buttons switch firmwares): no slot change, no FM-1 button, no popup, unlit. The MIDI modes and Sequencer mode keep
 * Down */
static void test_down(void)
{
    rfm_move_t *m = &M;
    uint8_t rgb[3];
    fixture(m);
    press(m, MR_CC_BACK);
    rfm_move_set_mode(m, RFM_MOVE_MODE_FM1);
    CHECK(!m->seq.projects && m->fw.active == 0);
    CHECK(led_of(m, 1, MR_CC_DOWN, rgb) && rgb[0] == 0);
    rest_pop(m), clear();
    button(m, MR_CC_DOWN, 1);
    adv_ms(m, 1500);
    button(m, MR_CC_DOWN, 0);
    CHECK(m->fw.active == 0 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0 && count(RFM_MOVE_ACT_BTN) == 0 &&
          !rfmv_pop_up(m, POP_LAYOUT) && m->s.mode == RFM_MOVE_MODE_FM1 && m->s.octave == 0);
    three(m);
    clear();
    press(m, MR_CC_DOWN), press(m, MR_CC_DOWN), press(m, MR_CC_DOWN);
    CHECK(count(RFM_MOVE_ACT_FW_ACTIVE) == 0 && m->fw.active == 0);
    CHECK(led_of(m, 1, MR_CC_DOWN, rgb) && rgb[0] == 0);
    /* (0.9.0) a note layout (= the sequencer): the octave, no firmware */
    rfm_move_fw_slots(m, (const char *const[3]){ "sloop", "choralroot", NULL });
    rfm_move_set_mode(m, RFM_MOVE_MODE_MIDI);
    CHECK(m->s.mode == RFM_MOVE_MODE_SEQ);
    clear();
    press(m, MR_CC_DOWN);
    CHECK(count(RFM_MOVE_ACT_FW_ACTIVE) == 0 && m->fw.active == 0 && m->s.octave == -1);
    press(m, MR_CC_UP);
    CHECK(m->s.octave == 0);
    /* Up in FM-1 mode: the FM-1's SEQ */
    rfm_move_set_mode(m, RFM_MOVE_MODE_FM1);
    clear();
    press(m, MR_CC_UP);
    CHECK(count(RFM_MOVE_ACT_BTN) == 2 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0);
}

/* (0.4.5) the Back flip names where it goes (as a Track tap does); FX setup is no layer Back closes, and is
 * remembered (and in the settings). (0.8.0) Back no longer flips into the view: Track 4 opens FX setup again, its
 * double click the projects */
static void test_flip_fx(void)
{
    rfm_move_t *m = &M;
    static uint8_t blob[RFM_MOVE_SETTINGS_BYTES];
    rfm_move_settings_t s2;
    const int mode0 = (fixture(m), m->s.mode);
    CHECK(m->seq.projects && !m->fw.fx && m->fw.track == 0);
    press(m, MR_CC_BACK);                               /* out: the slot and its layout named (0.8.0) */
    CHECK(!m->seq.projects && m->s.mode == mode0 && m->pop == POP_LAYOUT && m->pop_i == 0 && rfmv_pop_up(m, POP_LAYOUT));
    screen(m), write_pbm("flip_out_mode");
    CHECK(lit_in(0, 0, 128, 64) > 0);
    rest_pop(m);
    CHECK(!rfmv_pop_up(m, POP_LAYOUT));
    dbl_track(m, 3);                                    /* in (0.8.0: Track 4's double click): Projects, named */
    CHECK(m->seq.projects && m->fw.track == 3 && m->pop == POP_MODE && m->pop_i == 20 && rfmv_pop_up(m, POP_MODE));
    screen(m), write_pbm("flip_in_projects");
    CHECK(lit_in(30, 20, 98, 40) > 0 && lit_in(0, 50, 128, 64) == 0);   /* (the name alone, full screen) */
    jog(m, 1);                                          /* (a press or turn there: the name goes) */
    CHECK(m->pop != POP_MODE);
    jog(m, -1), rest_pop(m);
    /* FX setup (Menu), on the Mix page: Back flips out at once; (0.8.0) Track 4 in: FX setup again, on its page, named */
    press(m, MR_CC_MENU);
    CHECK(m->fw.fx && m->s.fx_page == 3);
    rest_pop(m);
    press(m, MR_CC_BACK);
    CHECK(!m->seq.projects && !m->fw.fx && m->s.fw_fx && m->pop == POP_LAYOUT && m->s.mode == mode0);
    rest_pop(m);
    tap_track(m, 3);
    CHECK(m->seq.projects && m->fw.fx && m->s.fx_page == 3 && m->pop == POP_T4 && m->pop_i == 0);   /* ("Mixer") */
    screen(m), write_pbm("flip_in_fx");
    /* .. kept in the settings image */
    CHECK(rfm_move_save(&m->s, blob) == RFM_MOVE_SETTINGS_BYTES && rfm_move_load(&s2, blob, sizeof blob) == 0 &&
          s2.fw_fx == 1 && s2.fx_page == 3);
    /* Menu still toggles; a module list closes first */
    press(m, MR_CC_MENU);
    CHECK(m->seq.projects && !m->fw.fx);
    press(m, MR_CC_MENU), tap_track(m, 3), tap_pad(m, fxpad(0, 0));   /* (0.8.0: Routing, MIDI FX's column) */
    CHECK(m->fw.fx && m->s.fx_page == 0);
    pad(m, fxpad(0, 2), 100), jog(m, 2);              /* (0.5.0: slot 3 held + the jog) */
    CHECK(m->fw.browse == 2);
    press(m, MR_CC_BACK);
    CHECK(m->seq.projects && m->fw.fx && m->fw.browse < 0);
    clear(), pad(m, fxpad(0, 2), 0);                    /* (closed by Back: nothing loaded on the release) */
    CHECK(count(RFM_MOVE_ACT_FX_LOAD) == 0);
    press(m, MR_CC_BACK);
    CHECK(!m->seq.projects && m->s.fw_fx);
    /* (0.8.0) Track 4 opens FX setup on the side last used (Routing's: MIDI FX, as left) */
    tap_track(m, 3);
    CHECK(m->seq.projects && m->fw.fx && m->s.fx_page == 0);
    press(m, MR_CC_MENU), press(m, MR_CC_BACK);         /* (left on the firmwares: forgotten) */
    CHECK(!m->seq.projects && !m->s.fw_fx);
    /* a note layout: out names it */
    rfm_move_set_mode(m, RFM_MOVE_MODE_MIDI);           /* (0.9.0: MIDI = the slot's note layout = the sequencer) */
    CHECK(m->s.mode == RFM_MOVE_MODE_SEQ && rfm_move_slot_layout(m, 0) >= RFM_MOVE_LAY_SCALE);
    browser(m, 0), rest_pop(m), press(m, MR_CC_BACK);
    CHECK(!m->seq.projects && m->s.mode == RFM_MOVE_MODE_SEQ && m->pop == POP_LAYOUT && m->pop_i == 0);
    rfm_move_set_mode(m, RFM_MOVE_MODE_FM1);
}

/* (0.4.5) FX setup: a knob touched shows its parameter for as long as it is touched (a turn updates it; let go: gone
 * at once); a knob with no parameter there: nothing */
static void test_fx_touch(void)
{
    rfm_move_t *m = &M;
    uint8_t img[MR_SCREEN_BYTES];
    fixture(m);
    press(m, MR_CC_MENU), tap_track(m, 3), tap_pad(m, fxpad(0, 0));   /* FX setup, MIDI FX, slot 1: Arp (0.8.0: Session
                                                         * opens the Mixer; Track 4: Routing; column 1's pad) */
    CHECK(m->fw.fx && m->s.fx_page == 0 && m->seq.fx.mfx[0].mod >= 0);
    rest_pop(m);
    touch(m, 0, 1);
    CHECK(m->fw.pop == FP_PARAM && m->fw.pop_a == 0 && rfmv_fw_timed(m));
    screen(m), memcpy(img, scr, sizeof img), write_pbm("fx_touch_param");
    adv_ms(m, 3 * RFM_MOVE_POPUP_MS);                   /* held, no turn: still up */
    CHECK(m->fw.pop == FP_PARAM && rfmv_fw_timed(m));
    screen(m);
    CHECK(!memcmp(scr, img, sizeof img));
    knob(m, 0, 1);                                      /* a turn: updated, still held */
    adv_ms(m, 3 * RFM_MOVE_POPUP_MS);
    CHECK(m->fw.pop == FP_PARAM && m->fw.pop_b && rfmv_fw_timed(m));
    touch(m, 0, 0);                                     /* let go: gone */
    CHECK(!rfmv_fw_timed(m));
    screen(m);
    CHECK(memcmp(scr, img, sizeof img) != 0);
    knob(m, 1, 1);                                      /* untouched turn: the timed popup, as before */
    CHECK(m->fw.pop == FP_PARAM && !m->fw.pop_b);
    rest_pop(m);
    CHECK(!rfmv_fw_timed(m));
    touch(m, 7, 1);                                     /* knob 8: no parameter on Arp: nothing */
    CHECK(!rfmv_fw_timed(m));
    touch(m, 7, 0);
    /* the Mixer: Level, Pan, sends; knob 6 nothing */
    tap_track(m, 3), rest_pop(m);
    CHECK(m->s.fx_page == 3);
    touch(m, 1, 1);
    adv_ms(m, 3 * RFM_MOVE_POPUP_MS);
    CHECK(m->fw.pop == FP_PARAM && m->fw.pop_a == 1 && rfmv_fw_timed(m));
    screen(m), write_pbm("fx_touch_mix_pan");
    touch(m, 1, 0);
    CHECK(!rfmv_fw_timed(m));
    touch(m, 5, 1);
    CHECK(!rfmv_fw_timed(m));
    touch(m, 5, 0);
    press(m, MR_CC_MENU);
}

/* (0.5.0) a module's own parameter pages (a host's ui_hierarchy plan): CloudSeed, 16 parameters; root has 11 (knob 3
 * a gap; its 3rd parameter after the last knob, page 2), a link to Advanced (3), Advanced's to Deep (2) */
static const char *const W_LPHP[] = { "LP", "HP" };
#define CP(n, sn) { n, 0, NULL, 64, 0, sn, 0, 0, 0, NULL }
static const rfm_move_fx_param_t P_CS[16] = {
    { "Mix", 0, NULL, 38, 1, "Mix", 0, 100, 0, "%" }, { "Mode", 2, W_LPHP, 0, 0, "Mode", 0, 0, 0, NULL },
    CP("Width", "Width"), CP("Size", "Size"), { "Decay", 0, NULL, 20, 0, "Decay", 0.1f, 10, 1, "s" },
    CP("Pre-delay", "Pre"), CP("Low cut", "LoCut"), CP("High cut", "HiCut"), CP("Tap count", "Taps"),
    CP("Tap length", "TapLn"), CP("Tap decay", "TapDc"), CP("Seed", "Seed"), CP("Diffusion", "Diff"),
    CP("Mod rate", "ModR"), CP("Deep one", "Deep1"), { "Deep two", 0, NULL, 77, 0, "Deep2", 0, 0, 0, NULL } };
static const rfm_move_fx_page_t PG_CS[4] = {
    { "CloudSeed", { 0, 1, -1, 3, 4, 5, 6, 7 } }, { "CloudSeed 2", { 8, 9, 10, 2, -1, -1, -1, -1 } },
    { "Advanced", { 11, 12, 13, -1, -1, -1, -1, -1 } }, { "Deep", { 14, 15, -1, -1, -1, -1, -1, -1 } } };
static rfm_move_fx_module_t AFX6[6];
static rfm_move_fx_param_t P_BIG[10];
static rfm_move_fx_module_t MFX6[6];
static void fx6(rfm_move_t *m)
{
    int k;
    memcpy(AFX6, AFX, sizeof AFX), memcpy(MFX6, MFX, sizeof MFX);
    AFX6[5].name = "CloudSeed", AFX6[5].nparams = 16, AFX6[5].params = P_CS, AFX6[5].npages = 4, AFX6[5].pages = PG_CS;
    for (k = 0; k < 10; k++)                            /* (no plan: pages of 8 in order) */
        memset(&P_BIG[k], 0, sizeof P_BIG[k]), P_BIG[k].name = "Big", P_BIG[k].def = (uint8_t)(10 + k);
    MFX6[5].name = "Big", MFX6[5].nparams = 10, MFX6[5].params = P_BIG;
    rfm_move_fx_modules(m, RFM_MOVE_FX_AUDIO, AFX6, 6);
    rfm_move_fx_modules(m, RFM_MOVE_FX_MIDI, MFX6, 6);
}
static void test_fx_pages(void)
{
    rfm_move_t *m = &M;
    static uint8_t blob[RFM_MOVE_PROJECT_MAX], b5[RFM_MOVE_PROJECT_MAX];
    const rfm_move_fxslot_t *sl = &m->seq.fx.afx[0];
    const act_t *a;
    uint8_t rgb[3];
    int n, n5, k, body;
    uint32_t h;
    fixture(m), fx6(m);
    press(m, MR_CC_MENU), tap_track(m, 3), tap_pad(m, fxpad(2, 0));   /* (0.8.0: via Routing) */
    CHECK(m->fw.fx && m->s.fx_page == RFM_MOVE_FX_PAGE_AUDIO && m->fw.fx_focus == 0);
    CHECK(rfm_move_fx_set_module(m, RFM_MOVE_FX_AUDIO, 0, 5) >= 0 && sl->mod == 5 && sl->val[0] == 38 && sl->val[15] == 77);
    rest_pop(m);
    /* the slots first; the jog's click: the module's page 1 (CloudSeed); knob 3 a gap */
    CHECK(!m->fw.fx_pages);
    jog(m, 1);                                          /* (on the slots, no pad held: nothing) */
    CHECK(!m->fw.fx_pages && m->fw.browse < 0);
    jog_press(m);
    CHECK(m->fw.fx_pages && m->fw.fx_pg[1][0] == 0);
    screen(m), write_pbm("fx_pages_root");
    CHECK(lit_in(0, 10, 128, 60) > 0);
    clear();
    knob(m, 2, 3), touch(m, 2, 1);
    CHECK(count(RFM_MOVE_ACT_FX_PARAM) == 0 && !rfmv_fw_timed(m));
    touch(m, 2, 0);
    knob(m, 3, 2);                                      /* knob 4: Size (param 3) */
    CHECK((a = last(RFM_MOVE_ACT_FX_PARAM)) && a->a.param == 3 && a->a.value == 66 && sl->val[3] == 66);
    rest_pop(m);
    /* the jog: page 2 (Width came after the last knob: knob 4) */
    jog(m, 1);
    CHECK(m->fw.fx_pg[1][0] == 1);
    knob(m, 3, -4);
    CHECK((a = last(RFM_MOVE_ACT_FX_PARAM)) && a->a.param == 2 && sl->val[2] == 60);
    rest_pop(m);
    /* the jog: Advanced, Deep; at the last page: nothing */
    jog(m, 1);
    knob(m, 0, 5);
    CHECK((a = last(RFM_MOVE_ACT_FX_PARAM)) && a->a.param == 11 && sl->val[11] == 69);
    jog(m, 1), jog(m, 1), jog_press(m);
    CHECK(m->fw.fx_pg[1][0] == 3 && m->fw.fx_pages);
    CHECK(led_of(m, 0, MR_NOTE_PAD0 + fxpad(2, 0), rgb) && rgb[0] > 0);   /* (its pad breathing) */
    rest_pop(m);
    /* a touch: the popup while touched (Deep two), gone on release */
    touch(m, 1, 1);
    adv_ms(m, 3 * RFM_MOVE_POPUP_MS);
    CHECK(m->fw.pop == FP_PARAM && m->fw.pop_a == 1 && rfmv_fw_timed(m));
    screen(m), write_pbm("fx_pages_touch");
    touch(m, 1, 0);
    CHECK(!rfmv_fw_timed(m));
    screen(m), write_pbm("fx_pages_deep");
    /* the host's edit by index, past 8 */
    clear();
    CHECK(rfm_move_fx_set_param(m, RFM_MOVE_FX_AUDIO, 0, 14, 99) == 1 && sl->val[14] == 99 &&
          (a = last(RFM_MOVE_ACT_FX_PARAM)) && a->a.param == 14 && a->a.value == 99);
    CHECK(rfm_move_fx_set_param(m, RFM_MOVE_FX_AUDIO, 0, 16, 1) == -1);
    /* the jog back to page 1 (and no further); Back: the slots again (a layer), then out */
    jog(m, -1), jog(m, -1), jog(m, -1), jog(m, -1);
    CHECK(m->fw.fx_pages && m->fw.fx_pg[1][0] == 0);
    knob(m, 1, RFM_MOVE_LIST_TICKS);                    /* (Mode: a word) */
    CHECK(sl->val[1] == 1);
    knob(m, 0, 127);                                    /* (Mix: 100 %) */
    screen(m), write_pbm("fx_pages_words");
    rest_pop(m);
    press(m, MR_CC_BACK);
    CHECK(!m->fw.fx_pages && m->seq.projects && m->fw.fx);
    screen(m), write_pbm("fx_pages_back_to_slots");
    jog_press(m), tap_pad(m, fxpad(1, 0));              /* (another column's pad: the pages close) */
    CHECK(!m->fw.fx_pages && m->s.fx_page == 1);
    tap_pad(m, fxpad(2, 2)), jog_press(m);              /* (an empty slot: no pages) */
    CHECK(!m->fw.fx_pages);
    /* no plan: pages of 8 (Big, 10 parameters: page 2 knobs 1, 2) */
    tap_pad(m, fxpad(0, 2));
    CHECK(rfm_move_fx_set_module(m, RFM_MOVE_FX_MIDI, 2, 5) >= 0 && m->seq.fx.mfx[2].val[9] == 19);
    jog_press(m), jog(m, 1);
    clear();
    knob(m, 1, 1), knob(m, 2, 1);
    CHECK((a = last(RFM_MOVE_ACT_FX_PARAM)) && a->a.param == 9 && count(RFM_MOVE_ACT_FX_PARAM) == 1);
    /* the whole setup: every parameter */
    clear();
    rfm_move_fx_sync(m);
    CHECK(count(RFM_MOVE_ACT_FX_PARAM) == 4 + 3 + 10 + 16 + 3);   /* (Arp, Chord, Big; CloudSeed, Delay) */
    /* saved and loaded: all 16 values */
    n = rfm_move_project_save(m, blob, sizeof blob);
    CHECK(n > 0 && blob[4] == RFM_MOVE_PROJECT_VERSION);
    rfm_move_init(&M2, NULL, on_act, NULL), fx6(&M2);
    CHECK(rfm_move_project_load(&M2, 0, blob, (uint32_t)n) == 0 && M2.seq.fx.afx[0].mod == 5);
    CHECK(!memcmp(M2.seq.fx.afx[0].val, sl->val, 16) && M2.seq.fx.afx[0].val[14] == 99);
    CHECK(M2.seq.fx.mfx[2].val[9] == 20);               /* (page 2's knob 2 turned +1) */
    /* a version 5 project (8 values a slot): those 8, the rest the defaults */
    body = n - 4 - 1 - RFM_MOVE_SEQ_CLIPS - RFM_MOVE_FW_SLOTS - RFM_MOVE_FXSET_BYTES - RFM_MOVE_PFW_BYTES - RFM_MOVE_KIT_BYTES;   /* (0.6.6: the MIDI routes after FX
                                                         * setup; 0.12.0: the launch settings after them) */
    memcpy(b5, blob, (size_t)body);
    {
        const uint8_t *p = blob + body;
        uint8_t *o = b5 + body;
        for (k = 0; k < 6; k++) {
            const int x = k < 3 ? 2 : 0;
            memcpy(o, p, RFM_MOVE_FX_NAME + 8);
            memcpy(o + RFM_MOVE_FX_NAME + 8, p + RFM_MOVE_FX_NAME + RFM_MOVE_FX_PARAMS, (size_t)x);
            o += RFM_MOVE_FX_NAME + 8 + x, p += RFM_MOVE_FX_NAME + RFM_MOVE_FX_PARAMS + x;
        }
        memcpy(o, p, 18), o += 18;
        n5 = (int)(o - b5) + 4;
    }
    CHECK(n5 == n - 1 - RFM_MOVE_SEQ_CLIPS - RFM_MOVE_FW_SLOTS - (RFM_MOVE_FXSET_BYTES - RFM_MOVE_FXSET_BYTES_V5) - RFM_MOVE_PFW_BYTES -
          RFM_MOVE_KIT_BYTES);
    b5[4] = 5;
    for (h = 2166136261u, k = 0; k < n5 - 4; k++)
        h = (h ^ b5[k]) * 16777619u;
    for (k = 0; k < 4; k++)
        b5[n5 - 4 + k] = (uint8_t)(h >> (8 * k));
    rfm_move_init(&M2, NULL, on_act, NULL), fx6(&M2);
    CHECK(rfm_move_project_load(&M2, 0, b5, (uint32_t)n5) == 0 && M2.seq.fx.afx[0].mod == 5);
    CHECK(!memcmp(M2.seq.fx.afx[0].val, sl->val, 8) && M2.seq.fx.afx[0].val[11] == 64 && M2.seq.fx.afx[0].val[15] == 77);
    CHECK(M2.seq.fx.mfx[2].val[7] == 17 && M2.seq.fx.mfx[2].val[9] == 19);   /* (past 8: the default again) */
    /* .. its modules given after the load: the defaults then */
    rfm_move_init(&M2, NULL, on_act, NULL);
    CHECK(rfm_move_project_load(&M2, 0, b5, (uint32_t)n5) == 0 && M2.seq.fx.afx[0].val[15] == RFM_MOVE_FX_UNSET);
    fx6(&M2);
    CHECK(M2.seq.fx.afx[0].val[15] == 77);
    /* a version 6 value out of range (not unset) is refused */
    blob[body + RFM_MOVE_FX_NAME + 20] = 200;
    for (h = 2166136261u, k = 0; k < n - 4; k++)
        h = (h ^ blob[k]) * 16777619u;
    for (k = 0; k < 4; k++)
        blob[n - 4 + k] = (uint8_t)(h >> (8 * k));
    CHECK(rfm_move_project_check(blob, (uint32_t)n) < 0);
    press(m, MR_CC_MENU);
}

/* (0.5.2) Menu's toggle in the projects view is named, as the Back flip: FX & Mix (Mixer on its page) one way,
 * Firmwares (Projects on Track 4) the other. (0.8.0: FX setup starts on the Mixer; Track 4 flips Mixer <-> Routing) */
static void test_menu_names(void)
{
    rfm_move_t *m = &M;
    fixture(m);
    rest_pop(m);
    CHECK(m->seq.projects && !m->fw.fx && m->fw.track == 0);
    press(m, MR_CC_MENU);                               /* in, on the Mixer (the default page): Mixer */
    CHECK(m->fw.fx && m->s.fx_page == 3 && m->pop == POP_MODE && m->pop_i == 23 && rfmv_pop_up(m, POP_MODE));
    rest_pop(m);
    CHECK(!rfmv_pop_up(m, POP_MODE));
    press(m, MR_CC_MENU);
    CHECK(!m->fw.fx && m->seq.projects && m->pop == POP_MODE && m->pop_i == 21);
    screen(m), write_pbm("menu_to_firmwares");
    tap_track(m, 3);                                    /* (a press there: the name goes; 0.8.0: Track 4 opens the Mixer) */
    CHECK((m->pop != POP_MODE || m->pop_i != 21) && m->fw.fx && m->s.fx_page == 3);
    tap_track(m, 3);                                    /* (Routing) */
    CHECK(m->fw.fx && m->s.fx_page == 1);
    press(m, MR_CC_MENU), rest_pop(m), press(m, MR_CC_MENU);   /* out and in again, on Routing: FX & Mix */
    CHECK(m->fw.fx && m->pop == POP_MODE && m->pop_i == 22 && rfmv_pop_up(m, POP_MODE));
    screen(m), write_pbm("menu_to_fx");
    CHECK(lit_in(30, 20, 98, 40) > 0 && lit_in(0, 50, 128, 64) == 0);   /* (the name alone, full screen) */
    rest_pop(m);
    dbl_track(m, 3);                                    /* (0.8.0) the projects (Track 4) */
    CHECK(m->seq.projects && !m->fw.fx && m->fw.track == 3);
    CHECK(m->s.fx_page == 3);                           /* (its tap first: Routing -> the Mixer) */
    press(m, MR_CC_MENU);                               /* FX setup, its Mixer */
    CHECK(m->fw.fx && m->s.fx_page == 3);
    press(m, MR_CC_MENU);                               /* out, on Track 4: Projects */
    CHECK(!m->fw.fx && m->fw.track == 3 && m->pop == POP_MODE && m->pop_i == 20);
    screen(m), write_pbm("menu_to_projects");
    CHECK(lit_in(30, 20, 98, 40) > 0);
    press(m, MR_CC_MENU);                               /* in again, on the Mixer: Mixer */
    CHECK(m->fw.fx && m->pop == POP_MODE && m->pop_i == 23);
    screen(m), write_pbm("menu_to_mixer");
    press(m, MR_CC_MENU);
}

/* (0.5.4) Fantasy CFWs: rfm_move_fw_t.virt, their badge, step 16's filter (built in), a ninth user tag moved */
static void test_fantasy(void)
{
    rfm_move_t *m = &M;
    static uint8_t blob[RFM_MOVE_FW_LIB_BYTES];
    static const int lv[3] = { 100, 80, 80 };
    static const char *const slots[3] = { "sloop", NULL, NULL };
    static const char *const tags[9] = { "Ambient", "Live set", "Retro", NULL, NULL, NULL, NULL, NULL, "Ninth" };
    recdef_t r[2] = { { "golf", 0, T(16) | T(8), T(16) | T(8) }, { "elasticat", 0, T(16), T(16) } };
    int ids[32], ve = idx("elasticat"), va = idx("aurora"), rec;
    uint8_t rgb[3];
    CHECK(ve >= 0 && va >= 0);
    fixture(m);
    E[ve].virt = 1, E[va].virt = 1;                     /* (Elasticat on the Move, AURORA not) */
    rfm_move_fw_list(m, E, NFW);
    rest_pop(m);
    tap_step(m, 1);                                     /* All */
    clear();
    tap_step(m, 15);                                    /* step 16: Fantasy */
    CHECK(m->s.fw_filter == T(16) && count(RFM_MOVE_ACT_SETTINGS) == 1);
    CHECK(rfmv_fw_list_len(m) == 1 && rfmv_fw_bank(m, ids) && ids[0] == ve);
    words(m, 16);
    CHECK(!strcmp(fname, "Fantasy") && !strcmp(fstate, "ON") && !strcmp(frows[0], "Showing: Fantasy,"));
    CHECK(led_of(m, 0, MR_NOTE_STEP0 + 15, rgb) && rgb[0] > 200 && rgb[1] < 90 && rgb[2] > 150);
    rest_pop(m), screen(m), write_pbm("fantasy_grid");
    CHECK(px(2, 1) && px(1, 2) && px(3, 2) && px(2, 3) && !px(2, 2));   /* (the cell's diamond) */
    select_fw(m, "elasticat"), screen(m), write_pbm("fantasy_name");
    CHECK(px(119, 0) && px(127, 8) && px(123, 2) && px(121, 4) && !px(123, 4));   /* (the framed diamond) */
    select_fw(m, "sloop"), screen(m);
    CHECK(!px(123, 2));                                 /* (none on a real firmware) */
    /* the catalog: both, the same filter */
    press(m, MR_CC_MUTE), rest_pop(m);
    CHECK(m->fw.catalog && rfmv_fw_list_len(m) == 2);
    select_fw(m, "aurora"), screen(m), write_pbm("fantasy_catalog");
    CHECK(px(119, 0) && px(123, 2));
    m->fw.sel = -1, screen(m), write_pbm("fantasy_catalog_grid");
    press(m, MR_CC_MUTE), rest_pop(m);
    /* Capture: step 16 shows it, toggles nothing */
    select_fw(m, "elasticat"), button(m, MR_CC_CAPTURE, 1);
    CHECK(led_of(m, 0, MR_NOTE_STEP0 + 15, rgb) && rgb[0] > 200 && rgb[2] > 150);
    clear();
    tap_step(m, 15);
    CHECK(m->fw.cap_step < 0 && (rec = rfmv_fw_rec(m, ve), rec < 0 || !(m->fw.rec[rec].tag_set & T(16))));
    button(m, MR_CC_CAPTURE, 0);
    tap_step(m, 15);                                    /* (the filter off) */
    CHECK(!m->s.fw_filter);
    /* a library with a ninth user tag (step 16): it moves to the first free step (11), its marks with it */
    CHECK(rfm_move_fw_library(m, blob, lib_write(blob, 0, lv, slots, tags, r, 2, NULL, 0)) == 0);
    CHECK(!strcmp(m->fw.tag[3], "Ninth") && !m->fw.tag[8][0]);
    rec = rfmv_fw_rec(m, idx("golf"));
    CHECK(rec >= 0 && (m->fw.rec[rec].tag_on & T(11)) && (m->fw.rec[rec].tag_on & T(8)) && !(m->fw.rec[rec].tag_on & T(16)));
    rec = rfmv_fw_rec(m, ve);
    CHECK(rec >= 0 && (m->fw.rec[rec].tag_set & T(11)) && !(m->fw.rec[rec].tag_set & T(16)));
    catalog_reset();
}

/* (0.5.10) a host with one firmware and no FX (rfm_move_fw_caps(m, 0): the web page, the phone apps): the browser as
 * ever, loading into slot 0; no other slot reachable, no FX setup, nothing of FX emitted */
static void test_single(void)
{
    rfm_move_t *m = &M;
    static uint8_t blob[RFM_MOVE_FW_LIB_BYTES];
    static const int lv[3] = { 90, 80, 70 };
    static const char *const sl[3] = { "sloop", "choralroot", "felucca" };
    const act_t *a;
    uint8_t rgb[3];
    int ids[32], k, felucca = idx("felucca");
    catalog_reset();
    rfm_move_init(m, NULL, on_act, NULL);
    CHECK(rfm_move_fw_caps_get(m) == RFM_MOVE_CAPS_ALL);   /* (the default: the Move's three slots and FX) */
    rfm_move_fw_caps(m, 0);
    CHECK(rfm_move_fw_caps_get(m) == 0);
    /* a library from a three-slot host (slot 3 played): slot 0, the others gone */
    CHECK(rfm_move_fw_library(m, blob, lib_write(blob, 2, lv, sl, (const char *const[9]){ 0 }, NULL, 0, NULL, 0)) == 0);
    CHECK(m->fw.active == 0 && !m->fw.slot_id[1][0] && !m->fw.slot_id[2][0]);
    rfm_move_fw_list(m, E, NFW);
    rfm_move_fw_slots(m, sl);
    rfm_move_fw_voices(m, 3);
    rfm_move_fx_modules(m, RFM_MOVE_FX_MIDI, MFX, 5), rfm_move_fx_modules(m, RFM_MOVE_FX_AUDIO, AFX, 5);
    CHECK(!strcmp(m->fw.slot_id[0], "sloop") && !m->fw.slot_id[1][0] && !m->fw.slot_id[2][0] && m->fw.voices == 1 &&
          rfm_move_fw_sounding(m) == 1 && rfmv_fw_loaded(m) == 1);
    m->s.fw_fx = 1;                                     /* (FX setup left showing on the Move: not here) */
    clear();
    browser(m, 0), rest_pop(m);                         /* (0.8.0: Track 1's double click) */
    CHECK(m->seq.projects && m->fw.track == 0 && !m->fw.fx && count(RFM_MOVE_ACT_FW_ACTIVE) == 0);
    screen(m), write_pbm("single_at_rest");
    CHECK(led_of(m, 1, MR_CC_TRACK1, rgb) && (rgb[0] | rgb[1] | rgb[2]));
    CHECK(led_of(m, 1, MR_CC_TRACK1 - 1, rgb) && !(rgb[0] | rgb[1] | rgb[2]));
    CHECK(led_of(m, 1, MR_CC_TRACK1 - 2, rgb) && !(rgb[0] | rgb[1] | rgb[2]));
    CHECK(led_of(m, 1, MR_CC_TRACK1 - 3, rgb) && (rgb[0] | rgb[1] | rgb[2]));   /* (Track 4: the projects) */
    /* Tracks 2, 3: nothing (no new popup: the entry's Track name had its time) */
    {
        const uint32_t pms = m->fw.pop_ms;
        tap_track(m, 1), tap_track(m, 2), dbl_track(m, 1);
        CHECK(m->seq.projects && m->fw.track == 0 && m->fw.active == 0 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0 && m->fw.pop_ms == pms);
    }
    track(m, 1, 1), knob(m, 8, 10), track(m, 1, 0);    /* (Track 2 held + volume: no level) */
    CHECK(count(RFM_MOVE_ACT_FW_LEVEL) == 0);
    track(m, 0, 1), knob(m, 8, -8);                     /* Track 1 held + volume: slot 0's level */
    CHECK((a = last(RFM_MOVE_ACT_FW_LEVEL)) && a->a.slot == 0 && a->a.value == 82);
    screen(m), write_pbm("single_level");
    CHECK(lit_in(0, 59, 128, 64) > 0 && lit_in(110, 59, 128, 64) == 0);   /* (one bar, the screen's width at most) */
    track(m, 0, 0), rest_pop(m);
    CHECK(m->seq.projects && m->fw.track == 0);         /* (fixed in 0.8.0: as in test_slots: the Track used by the volume, its
                                                         * release is no tap: still in the browser) */
    if (!m->seq.projects)
        browser(m, 0);                                  /* (to go on) */
    /* the browser as ever: All, Felucca, the jog's press loads it into slot 0 */
    tap_step(m, 1);
    rfmv_fw_bank(m, ids);
    for (k = 0; k < 32 && ids[k] != felucca; k++) {}
    CHECK(k < 32);
    tap_pad(m, pad_of(k));
    clear();
    jog_press(m);
    CHECK((a = last(RFM_MOVE_ACT_FW_LOAD)) && a->a.slot == 0 && !strcmp(a->id, "felucca"));
    rfm_move_fw_progress(m, "felucca", 100);
    CHECK(!strcmp(m->fw.slot_id[0], "felucca") && m->fw.slot_fw[0] == felucca);
    screen(m), write_pbm("single_loaded");
    rest_pop(m);
    press(m, MR_CC_MUTE), rest_pop(m);                  /* (the catalog) */
    CHECK(m->fw.catalog);
    screen(m), write_pbm("single_catalog");
    press(m, MR_CC_MUTE);
    /* Menu (Session): no FX setup; out of the view, as Back */
    clear();
    press(m, MR_CC_MENU);
    CHECK(!m->fw.fx && !m->seq.projects);
    screen(m), write_pbm("single_menu_out");
    rest_pop(m), browser(m, 0), rest_pop(m);
    CHECK(m->seq.projects && !m->fw.fx && m->fw.track == 0);
    tap_track(m, 3), rest_pop(m);                       /* (0.8.0) Track 4, no FX: the projects grid */
    CHECK(m->seq.projects && !m->fw.fx && m->fw.track == 3);
    press(m, MR_CC_MENU);                               /* (out as before the browser) */
    CHECK(!m->fw.fx && !m->seq.projects);
    CHECK(count(RFM_MOVE_ACT_FX_LOAD) == 0 && count(RFM_MOVE_ACT_FX_PARAM) == 0 && count(RFM_MOVE_ACT_MIX) == 0 &&
          count(RFM_MOVE_ACT_ROUTE) == 0);
    /* FM-1 mode: Track 2 (0.8.0: no Back + Track) and Down switch nothing */
    rfm_move_set_mode(m, RFM_MOVE_MODE_FM1), rest_pop(m), clear();
    tap_track(m, 1), dbl_track(m, 2);
    press(m, MR_CC_DOWN), press(m, MR_CC_DOWN);
    CHECK(count(RFM_MOVE_ACT_FW_ACTIVE) == 0 && m->fw.active == 0 && m->s.octave == 0);
    /* the host's edits: other slots and FX refused, slot 0's level taken */
    clear();
    CHECK(rfm_move_fw_set_active(m, 1) == -1 && rfm_move_fw_set_active(m, 2) == -1 && rfm_move_fw_set_active(m, 0) == 0);
    CHECK(rfm_move_fw_set_level(m, 1, 50) == -1 && rfm_move_fw_set_level(m, 0, 50) >= 0 && m->fw.level[0] == 50);
    CHECK(rfm_move_mix_set(m, 0, -1, 30, NULL) == -1 && rfm_move_mix_set(m, 0, -1, -1, (const int[3]){ 9, -1, -1 }) == -1 &&
          rfm_move_mix_set(m, 1, 60, -1, NULL) == -1 && rfm_move_mix_set(m, 0, 60, -1, NULL) >= 0 && m->fw.level[0] == 60);
    CHECK(rfm_move_route_set(m, (const int[3]){ 1, 1, 1 }, NULL) == -1);
    CHECK(rfm_move_fx_set_module(m, RFM_MOVE_FX_MIDI, 0, 1) == -1 && rfm_move_fx_set_param(m, RFM_MOVE_FX_AUDIO, 0, 0, 5) == -1 &&
          rfm_move_fx_set_page(m, RFM_MOVE_FX_PAGE_MIX) == -1);
    rfm_move_fx_sync(m);
    CHECK(rfm_move_project_load(m, 1, NULL, 0) == 0);
    CHECK(count(RFM_MOVE_ACT_FW_LEVEL) == 2 && count(RFM_MOVE_ACT_FX_LOAD) == 0 && count(RFM_MOVE_ACT_FX_PARAM) == 0 &&
          count(RFM_MOVE_ACT_MIX) == 0 && count(RFM_MOVE_ACT_ROUTE) == 0 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0);
    /* turned off at any time: the open FX setup and slots 2, 3 go */
    fixture(m), three(m), press(m, MR_CC_MENU);
    CHECK(m->fw.fx);
    rfm_move_fw_caps(m, RFM_MOVE_CAP_FX);              /* (FX kept, one slot) */
    CHECK(m->fw.fx && !m->fw.slot_id[1][0] && rfm_move_fw_sounding(m) == 1);
    rfm_move_fw_caps(m, 0);
    CHECK(!m->fw.fx && m->seq.projects && m->fw.track == 0);
    press(m, MR_CC_MENU);
    CHECK(!m->fw.fx);
}

int main(void)
{
    test_nolist();
    test_filters();
    test_slots();
    test_library();
    test_catalog();
    test_fx();
    test_host_edits();
    test_no_favs();
    test_filter_popup();
    test_download_load();
    test_dbl_load();
    test_back();
    test_down();
    test_flip_fx();
    test_fx_touch();
    test_fx_pages();
    test_menu_names();
    test_fantasy();
    test_single();
    test_states();
    test_mroute();
    test_fav_order();
    test_midi_track();
    test_old_formats();
    test_screens();
    return check_done("move_fw");
}
