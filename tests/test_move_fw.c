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
static void knob(rfm_move_t *m, int k, int d) { raw(m, 0xB0, (uint8_t)(MR_CC_KNOB1 + k), (uint8_t)(d & 127)); }

static void jog(rfm_move_t *m, int d) { raw(m, 0xB0, MR_CC_JOG, (uint8_t)(d & 127)); }
static void jog_press(rfm_move_t *m) { raw(m, 0xB0, MR_CC_JOG_CLICK, 127), raw(m, 0xB0, MR_CC_JOG_CLICK, 0); }
static void track(rfm_move_t *m, int t, int down) { button(m, MR_CC_TRACK1 - t, down); }
static void tap_track(rfm_move_t *m, int t) { track(m, t, 1), track(m, t, 0); }
static void adv_ms(rfm_move_t *m, uint32_t ms) { while (ms) { const uint32_t k = ms < 10 ? ms : 10; rfm_move_seq_advance(m, k * 48, 48000); ms -= k; } }
static void rest_pop(rfm_move_t *m) { adv_ms(m, RFM_MOVE_POPUP_MS + 50); }
static void projects(rfm_move_t *m) { track(m, 0, 1), tap_step(m, 0), track(m, 0, 0); }   /* (Track 1 held + step 1) */
static int pad_of(int cell) { return (3 - cell / 8) * 8 + cell % 8; }

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
    }
}
static const char *const W_RATE[] = { "1/8", "1/16", "1/32" }, *const W_MODE[] = { "Up", "Down", "Up/Dn", "Random" };
static const char *const W_OCT[] = { "1", "2", "3", "4" }, *const W_SHAPE[] = { "Major", "Minor", "Sus4", "7th", "9th" };
static const char *const W_INV[] = { "0", "1", "2" }, *const W_CURVE[] = { "Soft", "Linear", "Hard" };
static const char *const W_TIME[] = { "1/8", "1/8.", "1/4" };
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
    projects(m);
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
    tap_track(m, 0);                                    /* a firmware track: empty, no crash */
    CHECK(m->fw.track == 0 && m->seq.projects);
    rest_pop(m), screen(m);
    CHECK(lit_in(0, 0, 128, 64) > 0);
    for (i = 0; i < 32; i++)
        tap_pad(m, i);
    for (i = 0; i < 16; i++)
        tap_step(m, i);
    jog(m, 3), jog_press(m), knob(m, 0, 10), press(m, MR_CC_SAMPLE), press(m, MR_CC_DELETE), press(m, MR_CC_MUTE);
    press(m, MR_CC_LEFT), press(m, MR_CC_RIGHT), press(m, MR_CC_LOOP), button(m, MR_CC_CAPTURE, 1), tap_step(m, 9);
    button(m, MR_CC_CAPTURE, 0), knob(m, 8, 5);
    screen(m);
    CHECK(count(RFM_MOVE_ACT_FW_LOAD) == 0 && count(RFM_MOVE_ACT_FW_DOWNLOAD) == 0 && count(RFM_MOVE_ACT_FW_UNLOAD) == 0);
    CHECK(led_of(m, 1, MR_CC_TRACK1, c) && c[0] == c[1] && c[1] == c[2] && c[0] > 0);   /* (an empty slot: faint white) */
    press(m, MR_CC_MUTE);
    press(m, MR_CC_MENU);                               /* (no browser: Session leaves, no FX setup) */
    CHECK(!m->seq.projects && !m->fw.fx);
    projects(m);
    CHECK(m->fw.track == 3);                            /* (again on Track 4) */
    press(m, MR_CC_BACK);
    CHECK(!m->seq.projects);
    /* an empty list (the browser on, nothing installed): the firmware track's empty state */
    rfm_move_fw_list(m, NULL, 0);
    projects(m);
    CHECK(m->fw.track == 0);
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
        CHECK(rfm_move_save(&m->s, img) == RFM_MOVE_SETTINGS_BYTES && img[3] == 4);
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
    /* Track 3 (empty): All, the pad of Felucca, a jog press: FW_LOAD(2, felucca); a bar until the host says */
    tap_track(m, 2);
    CHECK(m->fw.track == 2 && (a = last(RFM_MOVE_ACT_FW_ACTIVE)) && a->a.slot == 2 && rfm_move_fw_active(m) == 2);
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
    /* Track 4: the projects; the active slot stays */
    clear();
    tap_track(m, 3);
    CHECK(m->fw.track == 3 && rfm_move_fw_active(m) == 2 && count(RFM_MOVE_ACT_FW_ACTIVE) == 0);
    /* levels: Track held + volume (the view): FW_LEVEL; the volume alone: MASTER */
    tap_track(m, 0);
    clear();
    track(m, 1, 1), knob(m, 8, -8), track(m, 1, 0);
    CHECK((a = last(RFM_MOVE_ACT_FW_LEVEL)) && a->a.slot == 1 && a->a.value == 72 && m->fw.level[1] == 72);
    CHECK(count(RFM_MOVE_ACT_MASTER) == 0);
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
        projects(m), tap_track(m, 2), press(m, MR_CC_BACK);   /* (Track 3 the active one) */
        CHECK(rfm_move_fw_active(m) == 2 && !m->seq.projects);
        rfm_move_fw_voices(m, 1);
        CHECK(rfm_move_fw_sounding(m) == 1 << 2);      /* (the active one: Track 3) */
        rfm_move_fw_voices(m, 2);
        CHECK(rfm_move_fw_sounding(m) == ((1 << 2) | 1));
        rfm_move_fw_voices(m, 3);
        CHECK(rfm_move_fw_sounding(m) == 7);
        rfm_move_fw_voices(m, 1);
        projects(m);
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
    /* a host with no list: Track held + volume stays MASTER, the release a mode change as before */
    rfm_move_init(m, NULL, on_act, NULL);
    clear();
    track(m, 1, 1), knob(m, 8, 5), track(m, 1, 0);
    CHECK(count(RFM_MOVE_ACT_FW_LEVEL) == 0 && count(RFM_MOVE_ACT_MASTER) == 1);
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
    press(m, MR_CC_MENU);                               /* Session: FX setup, page 1 (MIDI FX) */
    CHECK(m->fw.fx && m->s.fx_page == 0 && m->fw.pop == FP_RPAGE);
    CHECK(led_of(m, 1, MR_CC_TRACK1, c) && c[1] == 255 && c[2] == 170);
    /* steps: MIDI FX 1 acts on track 7; step 3: track 3 only; step 3 again: all */
    clear();
    tap_step(m, 2);
    CHECK(m->seq.fx.mfx[0].filt == T(3) && (a = last(RFM_MOVE_ACT_ROUTE)) && a->a.filt[3] == T(3) && a->a.filt[0] == 0x1F);
    tap_step(m, 2);
    CHECK(m->seq.fx.mfx[0].filt == 0xFFFF && m->seq.dirty);
    /* the jog: the module list (Move's list), a press loads */
    tap_pad(m, pad_of(2));                              /* slot 3 (empty) */
    jog(m, 2);
    CHECK(m->fw.browse == 2);
    screen(m);
    clear();
    jog_press(m);
    CHECK((a = last(RFM_MOVE_ACT_FX_LOAD)) && a->a.fx == RFM_MOVE_FX_MIDI && a->a.slot == 2 && a->a.value == 1 &&
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
    /* Delete: empties the slot */
    clear();
    press(m, MR_CC_DELETE);
    CHECK((a = last(RFM_MOVE_ACT_FX_LOAD)) && a->a.value == -1 && m->seq.fx.mfx[2].mod == -1 && !m->seq.fx.mfx[2].name[0]);
    /* Routing (Track 2): each firmware's filter, a step toggles */
    tap_track(m, 1);
    CHECK(m->s.fx_page == 1);
    tap_pad(m, pad_of(1));
    clear();
    tap_step(m, 6);                                     /* track 7 out of firmware 2's 7, 8 */
    CHECK(m->seq.fx.ffilt[1] == T(8) && (a = last(RFM_MOVE_ACT_ROUTE)) && a->a.filt[1] == T(8));
    jog(m, 1), jog_press(m), press(m, MR_CC_DELETE);    /* (no slot here) */
    CHECK(count(RFM_MOVE_ACT_FX_LOAD) == 0);
    /* Audio FX (Track 3): a dry / wet parameter's popup says 100 % wet */
    tap_track(m, 2);
    tap_pad(m, pad_of(0));
    knob(m, 2, -1);                                     /* Freeverb's Mix */
    screen(m);
    CHECK(m->fw.pop == FP_PARAM && lit_in(0, 56, 128, 64) > 20);
    /* Mixer (Track 4): knob 1 the level (FW_LEVEL), 2 the pan, 3..5 the sends (MIX) */
    tap_track(m, 3);
    tap_pad(m, pad_of(2));
    clear();
    knob(m, 0, -10);
    CHECK((a = last(RFM_MOVE_ACT_FW_LEVEL)) && a->a.slot == 2 && a->a.value == 70);
    knob(m, 1, -4);
    CHECK((a = last(RFM_MOVE_ACT_MIX)) && a->a.slot == 2 && a->a.mix[0] == 70 && a->a.mix[1] == 60 && a->a.mix[2] == 30 && a->a.mix[3] == 70);
    knob(m, 4, 9);
    CHECK((a = last(RFM_MOVE_ACT_MIX)) && a->a.mix[4] == 9 && m->seq.fx.send[2][2] == 9);
    screen(m);
    /* the project keeps FX setup (version 5); a project load tells the host */
    {
        static uint8_t blob[RFM_MOVE_PROJECT_MAX];
        const int n = rfm_move_project_save(m, blob, sizeof blob);
        CHECK(n == RFM_MOVE_PROJECT_FIXED && blob[4] == 5);
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
    /* the host refuses a module: Too busy, the slot empty */
    tap_track(m, 2);
    rfm_move_fx_refused(m, RFM_MOVE_FX_AUDIO, 0);
    CHECK(m->seq.fx.afx[0].mod == -1 && m->fw.pop == FP_RLOADED);
    screen(m);
    /* a host with no modules: no MIDI FX / Audio FX page; Session again or Back leaves */
    rfm_move_fx_modules(m, RFM_MOVE_FX_MIDI, NULL, 0);
    rfm_move_fx_modules(m, RFM_MOVE_FX_AUDIO, NULL, 0);
    CHECK(m->s.fx_page == 1);
    tap_track(m, 0), tap_track(m, 2);
    CHECK(m->s.fx_page == 1 && led_of(m, 1, MR_CC_TRACK1, c) && !c[0] && !c[1] && !c[2]);
    tap_track(m, 3);
    CHECK(m->s.fx_page == 3);
    press(m, MR_CC_MENU);
    CHECK(!m->fw.fx && m->seq.projects);
    press(m, MR_CC_MENU), press(m, MR_CC_BACK);
    CHECK(!m->fw.fx && m->seq.projects);
    /* the whole setup at once (at start) */
    clear();
    rfm_move_fx_sync(m);
    CHECK(count(RFM_MOVE_ACT_FX_LOAD) == 6 && count(RFM_MOVE_ACT_MIX) == 3 && count(RFM_MOVE_ACT_ROUTE) == 1);
}

/* ================================================================ states === */
static void test_states(void)
{
    rfm_move_t *m = &M;
    const act_t *a;
    fixture(m);
    tap_track(m, 2);                                    /* (empty: Loop does nothing) */
    press(m, MR_CC_LOOP);
    CHECK(!m->fw.states);
    tap_track(m, 0);
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
    int i, tagged = 0, other = 0;
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
            const int ch = acts[i].a.bytes[0] & 15;
            if ((ch == 0 && acts[i].a.track == 1) || (ch == 9 && acts[i].a.track == 6))
                tagged++;
            else
                other++;
        }
    CHECK(tagged >= 5 && other == 0);                   /* (on, off, the CC lock: each with its track) */
    rfm_move_seq_play(m, 0);
    clear();
    tap_track(m, 1);                                    /* MIDI mode: a pad played live: no track */
    pad(m, 0, 100), pad(m, 0, 0);
    CHECK(last(RFM_MOVE_ACT_MIDI) && last(RFM_MOVE_ACT_MIDI)->a.track == 0);
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
    CHECK(blob[4] == RFM_MOVE_PROJECT_VERSION && RFM_MOVE_PROJECT_VERSION == 5);
    n4 = n - RFM_MOVE_FXSET_BYTES;                      /* a version 4 blob: no FX setup */
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
    /* a version 5 blob cut to version 4's length is refused */
    blob[4] = 5;
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
    press(m, MR_CC_MENU);
    if (page)
        tap_track(m, page);
    else
        tap_track(m, 0);
    tap_pad(m, pad_of(slot));
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
        tap_track(m, 2), tap_step(m, 1), select_fw(m, "felucca"), jog_press(m);
        return rfm_move_fw_progress(m, "felucca", 55), 1;
    }
    if (IS("loaded")) return tap_track(m, 2), select_fw(m, "felucca"), jog_press(m), rfm_move_fw_progress(m, "felucca", 100), 1;
    if (IS("track_held_volume_level")) {
        three(m);
        track(m, 2, 1), knob(m, 8, 10), track(m, 2, 0);
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
    if (IS("audio_fx_track_3_choosing_a_module")) return fxs(m, 2, 2), jog(m, 3), 1;
    if (IS("audio_fx_a_module_s_knob")) return fxs(m, 2, 0), knob(m, 0, 1), knob(m, 0, -1), 1;
    if (IS("mixer_track_4")) return fxs(m, 3, 1), rest_pop(m), 1;
    if (IS("mixer_a_send_knob")) return fxs(m, 3, 2), knob(m, 3, 1), knob(m, 3, -1), 1;
    if (IS("session_the_page_s_name")) return fxs(m, 3, 0), tap_track(m, 3), 1;
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
        if (!strcmp(n, "fwb.step_favorites_chords")) {   /* (0.4.2: the filter popup redrawn; test_filter_popup) */
            CHECK(h != FWB_REF[i].hash);
            continue;
        }
        same += h == FWB_REF[i].hash;
        CHECKF(h == FWB_REF[i].hash, "%s: %08x, the mockup %08x", n, h, FWB_REF[i].hash);
    }
    printf("move_fw: %d of %d screens as the mockup's, pixel for pixel (1 redrawn in 0.4.2)\n", same,
           (int)(sizeof FWB_REF / sizeof FWB_REF[0]));
    /* two of the core's own (not in the mockup): Track 4 in the view, States */
    fixture(m), tap_track(m, 3), rest_pop(m), screen(m), write_pbm("track4_projects");
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
    tap_track(&M2, 1);
    CHECK(same_screens(m, &M2));
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
    fixture(&M2), press(&M2, MR_CC_MENU), tap_track(&M2, 3), tap_pad(&M2, pad_of(2));
    knob(&M2, 0, -10), knob(&M2, 1, -34), knob(&M2, 2, -20);   /* (send 1: 30 -> 10) */
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
    press(m, MR_CC_MENU), tap_track(m, 1), rest_pop(m);    /* FX setup's Routing on screen */
    screen(m), h0 = hash_scr();
    CHECK(rfm_move_route_set(m, (const int[3]){ 0xFFFF, -1, -1 }, NULL) == 1);
    screen(m);
    CHECK(hash_scr() != h0);

    /* a module: fx_load's defaults, FX_LOAD (no FX_PARAM), the project; as the jog's press */
    fixture(m), fixture(&M2);
    press(m, MR_CC_MENU), tap_pad(m, pad_of(2)), rest_pop(m);
    press(&M2, MR_CC_MENU), tap_pad(&M2, pad_of(2)), jog(&M2, 1), jog_press(&M2), rest_pop(&M2);
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
    /* the page: SETTINGS, as a Track press in FX setup */
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
          rfm_move_fx_set_page(m, RFM_MOVE_FX_PAGE_MIX) == 0 && rfm_move_fw_set_active(m, 2) == 0);
    CHECK(count(RFM_MOVE_ACT_FW_LEVEL) == 1 && count(RFM_MOVE_ACT_FX_LOAD) == 1 && count(RFM_MOVE_ACT_SETTINGS) == 1 &&
          count(RFM_MOVE_ACT_FW_ACTIVE) == 1 && m->fw.active == 2 && m->s.fx_page == 3);
}

/* ---- (0.4.1) no favourites: the view shows All; the setting keeps the user's choice */
static void reenter(rfm_move_t *m) { press(m, MR_CC_BACK), projects(m); }
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
    /* .. until the next entry, or another firmware track */
    tap_track(m, 1);
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
    step(m, 15, 1);                                     /* (step 16: no tag made) */
    CHECK(!m->fw.hold_step);
    step(m, 15, 0);
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
    fixture(m), tap_track(m, 1), press(m, MR_CC_MUTE), rest_pop(m), select_fw(m, FWB_NEWFW);
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
    test_states();
    test_midi_track();
    test_old_formats();
    test_screens();
    return check_done("move_fw");
}
