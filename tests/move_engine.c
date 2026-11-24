/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (0.14.0, the owner's 2026-11-24: "add a page after the FM-1 knobs for Engine parameters") the ENGINE knob page,
 * stages 1 and 2 of engine-params' BUILD-PLAN.md: the ep1 API (the description checked and fuzzed), the page's
 * presence and place among the knob pages, arriving, the knobs (lights, touch and turn, every value format), the SETs
 * merged per block, the watch, the page list (the jog's click), an engine changed under the page, a layer off, the
 * description gone and back, the layer by channel. A two-layer firmware ("Chord", "Bass") with two engines ("VA",
 * "FM6") is written here as an EP1D blob. Writes engine_*.pbm / .leds (the screens, the LEDs) into the working
 * directory. */
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
static rfm_move_act_t A[1024];
static int nact;
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
static void clear(void) { nact = 0, said[0] = 0; }
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
static void jog_down(int on) { raw(0xB0, MR_CC_JOG_CLICK, on ? 127 : 0); }
static void jog_click(void) { jog_down(1), jog_down(0); }
static void track(int t, int down) { button(MR_CC_TRACK1 - t, down); }
static void tap_track(int t) { track(t, 1), track(t, 0); }
static void hold_track(int t) { track(t, 1), adv_ms(RFM_MOVE_TAP_MS + 20); }
static void rest(void) { adv_ms(RFM_MOVE_POPUP_MS + 50), now_ms += RFM_MOVE_DOUBLE_MS; }
static void back(void) { press(MR_CC_BACK); }

static rfm_move_fw_t E[NFW];
static void fresh(void)
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
    rest();
    clear();
}
/* a note layout on slot 2 (felucca: the slot played), its track view: track 2 selected (MIDI channel 2: 1 here) */
static void track_view(void)
{
    fresh();
    hold_track(1), hit(26), track(1, 0);
    rest(), clear();
    tap_track(1), rest();
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON), rfm_move_set_speech(&M, 1);
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.fw.active == 1 && !M.seq.view && M.seq.sel_track == 1);
    rest(), clear();
}
static void select_track(int t) { hold_track(1), step(t, 1), step(t, 0), track(1, 0), rest(); }

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
static void write_pbm(const char *name)
{
    char path[96];
    FILE *f;
    int x, y, c, r;
    uint8_t w[RFM_MOVE_NLEDS][3];
    screen();
    snprintf(path, sizeof path, "engine_%s.pbm", name);
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "P1\n128 64\n");
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++)
            fputc(px(x, y) ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
    leds(w);                                            /* (the pads, the steps, the knobs, the icons, the Tracks: as
                                                         * move_sessview's .leds) */
    snprintf(path, sizeof path, "engine_%s.leds", name);
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
    fclose(f);
}
static int white(const uint8_t c[3]) { return c[0] == c[1] && c[1] == c[2] && c[0] > 200; }
static int dark(const uint8_t c[3]) { return !c[0] && !c[1] && !c[2]; }
static int is_slot(const uint8_t c[3], int slot)      /* lit in the slot's colour (any brightness) */
{
    const rfmv_rgb_t s = RFMV_SLOT_RGB[slot];
    const int b = s.r > s.g ? (s.r > s.b ? 0 : 2) : (s.g > s.b ? 1 : 2), cv[3] = { c[0], c[1], c[2] };
    const int sv[3] = { s.r, s.g, s.b };
    return cv[b] > 0 && !white(c) && (cv[0] >= cv[1]) == (sv[0] >= sv[1]) && (cv[1] >= cv[2]) == (sv[1] >= sv[2]);
}
static int sum(const uint8_t c[3]) { return c[0] + c[1] + c[2]; }
/* the strip at the bottom: its cells (a run of lit pixels on row 63) and the thick one (lit on row 61) */
static int strip_cells(int *thick)
{
    int x, n = 0, on = 0;
    *thick = -1;
    for (x = 0; x < 128; x++) {
        if (px(x, 63) && !on)
            n++;
        if (px(x, 61) && px(x, 63) && *thick < 0)
            *thick = n - 1;
        on = px(x, 63);
    }
    return n;
}

/* ================================================================ the test firmware's description (EP1D) === */
typedef struct { uint16_t pid; const char *name; uint8_t flags; int16_t mn, mx, df; uint8_t fmt, arg; int8_t table;
                 const char *unit; uint8_t step; } tp_t;
typedef struct { uint8_t id; const char *name; uint8_t group; int16_t k[8]; } tpage_t;   /* k: a param, -1 none */
typedef struct { uint16_t eid; const char *name; uint8_t first, n; } teng_t;
typedef struct { int16_t first; int count; const char *const *s; } ttab_t;
enum { F_BIP = 1, F_LIST = 2, F_DYN = 4, F_GLOBAL = 32 };
enum { INT, FIXED, TABLE, NOTE, PERCENT, ONOFF };
enum { P_WAVE, P_SHAPE, P_OCT, P_TUNE, P_SYNC, P_CUT, P_RES, P_ENVAMT, P_FTYPE, P_ATK, P_DEC, P_SUS, P_REL, P_LRATE,
       P_LDEPTH, P_LWAVE, P_MOD1, P_MOD2, P_REV, P_DLY, P_LEVEL, P_PAN, P_ROOT, P_OP1L, P_OP1R, P_OP2L, P_OP2R, P_FMENV,
       P_FB, P_N };
static const tp_t PAR[P_N] = {
    { 101, "Wave", F_LIST | F_DYN, 0, 3, 0, TABLE, 0, 0, "", 0 },
    { 102, "Shape", 0, 0, 127, 64, PERCENT, 0, -1, "", 0 },
    { 103, "Octave", F_BIP, -2, 2, 0, INT, 0, -1, "oct", 0 },
    { 104, "Tune", F_BIP, -1200, 1200, 0, FIXED, 2, -1, "st", 0 },
    { 105, "Sync", 0, 0, 1, 0, ONOFF, 0, -1, "", 0 },
    { 110, "Cutoff", 0, 0, 127, 64, TABLE, 0, 1, "", 0 },
    { 111, "Reso", 0, 0, 127, 0, PERCENT, 0, -1, "", 0 },
    { 112, "Env amt", F_BIP, -480, 480, 0, FIXED, 1, -1, "", 0 },
    { 113, "Type", F_LIST, 0, 3, 0, TABLE, 0, 2, "", 0 },
    { 120, "Attack", 0, 0, 5000, 10, INT, 0, -1, "ms", 0 },
    { 121, "Decay", 0, 0, 5000, 300, INT, 0, -1, "ms", 0 },
    { 122, "Sustain", 0, 0, 127, 100, PERCENT, 0, -1, "", 0 },
    { 123, "Release", 0, 0, 5000, 420, INT, 0, -1, "ms", 10 },
    { 130, "Rate", F_DYN, 0, 127, 40, INT, 0, -1, "", 0 },
    { 131, "Depth", F_BIP, -100, 100, 0, PERCENT, 0, -1, "", 0 },
    { 132, "LFO wave", F_LIST, 0, 3, 0, TABLE, 0, 3, "", 0 },
    { 140, "Mod 1", F_BIP, -64, 63, 0, PERCENT, 0, -1, "", 0 },
    { 141, "Mod 2", F_BIP, -64, 63, 0, PERCENT, 0, -1, "", 0 },
    { 150, "Rev size", F_GLOBAL, 0, 127, 64, INT, 0, -1, "", 0 },
    { 151, "Delay", 0, 1, 2000, 250, INT, 0, -1, "ms", 5 },
    { 160, "Level", 0, 0, 127, 100, INT, 0, -1, "", 0 },
    { 161, "Pan", F_BIP, -64, 63, 0, INT, 0, -1, "", 0 },
    { 162, "Root", 0, 0, 127, 60, NOTE, 3, -1, "", 0 },
    { 201, "OP1 Level", 0, 0, 99, 99, INT, 0, -1, "", 0 },
    { 202, "OP1 Ratio", 0, 50, 3200, 100, FIXED, 2, -1, "", 0 },
    { 203, "OP2 Level", 0, 0, 99, 80, INT, 0, -1, "", 0 },
    { 204, "OP2 Ratio", 0, 50, 3200, 200, FIXED, 2, -1, "", 0 },
    { 205, "Env time", 0, 0, 99, 20, INT, 0, -1, "", 0 },
    { 206, "Feedback", 0, 0, 7, 0, INT, 0, -1, "", 0 },
};
#define K8(a, b, c, d, e, f, g, h) { a, b, c, d, e, f, g, h }
static const tpage_t PAGES[] = {
    /* VA (engine id 2): pages 0..6 */
    { 1, "OSC 1", 1, K8(P_WAVE, P_SHAPE, P_OCT, P_TUNE, P_SYNC, -1, -1, -1) },
    { 2, "FILTER", 2, K8(P_CUT, P_RES, P_ENVAMT, P_FTYPE, -1, -1, -1, -1) },
    { 3, "ENV 1", 3, K8(P_ATK, P_DEC, P_SUS, P_REL, -1, -1, -1, -1) },
    { 4, "LFO 1/2", 4, K8(P_LRATE, P_LDEPTH, P_LWAVE, -1, -1, -1, -1, -1) },
    { 5, "MOD 1/2", 5, K8(P_MOD1, P_MOD2, -1, -1, -1, -1, -1, -1) },
    { 6, "FX", 6, K8(P_REV, P_DLY, -1, -1, -1, -1, -1, -1) },
    { 7, "MIX", 7, K8(P_LEVEL, P_PAN, P_ROOT, -1, -1, -1, -1, -1) },
    /* FM6 (engine id 0): pages 7..11, no FILTER, the shared FX and MIX */
    { 1, "OP 1", 1, K8(P_OP1L, P_OP1R, P_FB, -1, -1, -1, -1, -1) },
    { 2, "OP 2", 1, K8(P_OP2L, P_OP2R, -1, -1, -1, -1, -1, -1) },
    { 3, "ENV", 3, K8(P_FMENV, -1, -1, -1, -1, -1, -1, -1) },
    { 6, "FX", 6, K8(P_REV, P_DLY, -1, -1, -1, -1, -1, -1) },
    { 7, "MIX", 7, K8(P_LEVEL, P_PAN, P_ROOT, -1, -1, -1, -1, -1) },
};
#define NPAGES ((int)(sizeof PAGES / sizeof PAGES[0]))
enum { PG_OSC1, PG_FILTER, PG_ENV1, PG_LFO, PG_MOD, PG_VAFX, PG_VAMIX, PG_OP1, PG_OP2, PG_FMENV, PG_FMFX, PG_FMMIX };
static const teng_t ENGS[] = { { 2, "VA", 0, 7 }, { 0, "FM6", 7, 5 } };
enum { E_VA, E_FM6 };
static const char *const LAYERS[] = { "Chord", "Bass" };
static const char *const T_WAVE[] = { "Saw", "Square", "Tri", "Noise" };
static const char *const T_TYPE[] = { "LP24", "LP12", "HP", "BP" };
static const char *const T_LWAVE[] = { "Sine", "Tri", "Saw", "S&H" };
static char cut_s[128][12];
static const char *cut_p[128];
static ttab_t TABS[4] = { { 0, 4, T_WAVE }, { 0, 128, cut_p }, { 0, 4, T_TYPE }, { 0, 4, T_LWAVE } };

static void w16(uint8_t *p, int v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { w16(p, (int)(v & 0xFFFF)), w16(p + 2, (int)(v >> 16)); }
static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static void name_at(uint8_t *p, const char *s, int w) { memset(p, 0, (size_t)w), memcpy(p, s, strlen(s) < (size_t)w ? strlen(s) : (size_t)w - 1); }
/* an EP1D blob from small tables (the shim's job on the firmware's side; SPEC.md 3): its length */
static uint32_t ep_write(uint8_t *o, const char *const *lay, int nl, const teng_t *en, int ne, const tpage_t *pg, int np,
                         const tp_t *par, int npar, const ttab_t *tab, int nt)
{
    uint8_t *p = o + 32;
    uint32_t n, sbase, soff = 0;
    int i, k;
    for (i = 0; i < nl; i++, p += 16)
        memset(p, 0, 16), name_at(p, lay[i], 11), p[11] = 1;   /* (NOTES_BY_CHANNEL) */
    for (i = 0; i < ne; i++, p += 16)
        memset(p, 0, 16), w16(p, en[i].eid), name_at(p + 2, en[i].name, 11), p[13] = en[i].n, w16(p + 14, en[i].first);
    for (i = 0; i < np; i++, p += 32) {
        memset(p, 0, 32), p[0] = pg[i].id, name_at(p + 1, pg[i].name, 11);
        for (k = 0; k < 8; k++)
            w16(p + 12 + 2 * k, pg[i].k[k] < 0 ? 0xFFFF : pg[i].k[k]);
        p[28] = pg[i].group;
    }
    for (i = 0; i < npar; i++, p += 32) {
        const tp_t *q = &par[i];
        memset(p, 0, 32), w16(p, q->pid), name_at(p + 2, q->name, 11), p[13] = q->flags;
        w16(p + 14, q->mn), w16(p + 16, q->mx), w16(p + 18, q->df), p[20] = q->fmt, p[21] = q->arg;
        w16(p + 22, q->table < 0 ? 0xFFFF : q->table), name_at(p + 24, q->unit, 5), p[29] = q->step, p[30] = 255;
    }
    sbase = (uint32_t)(p + 8 * nt - o);
    for (i = 0; i < nt; i++, p += 8) {
        w32(p, soff), w16(p + 4, tab[i].count), w16(p + 6, tab[i].first);
        for (k = 0; k < tab[i].count; k++) {
            const size_t l = strlen(tab[i].s[k]) + 1;
            memcpy(o + sbase + soff, tab[i].s[k], l);
            soff += (uint32_t)l;
        }
    }
    n = sbase + soff;
    memcpy(o, "EP1D", 4), w16(o + 4, 1), w16(o + 6, 32), w16(o + 8, npar), o[10] = (uint8_t)ne, o[11] = (uint8_t)nl;
    w16(o + 12, np), w16(o + 14, nt), w32(o + 16, n), memset(o + 24, 0, 8);
    w32(o + 20, fnv(o + 32, n - 32));
    return n;
}
static uint8_t BLOB[RFM_MOVE_EP_DESC_MAX];
static uint32_t blob_n;
static void make_blob(void)
{
    int i;
    for (i = 0; i < 128; i++) {                         /* (a curve's words, as the firmware's own formatter: "1.2k" at
                                                         * 64) */
        if (i < 64)
            snprintf(cut_s[i], sizeof cut_s[i], "%d", 20 + i * 15);
        else
            snprintf(cut_s[i], sizeof cut_s[i], "%d.%dk", (12 + (i - 64) * 3) / 10, (12 + (i - 64) * 3) % 10);
        cut_p[i] = cut_s[i];
    }
    blob_n = ep_write(BLOB, LAYERS, 2, ENGS, 2, PAGES, NPAGES, PAR, P_N, TABS, 4);
}
/* the firmware's values for a layer on an engine (its parameters; the others EP_NONE) */
static int16_t VAL[P_N];
static void vals_for(int eng)
{
    int i;
    for (i = 0; i < P_N; i++)
        VAL[i] = RFM_MOVE_EP_NONE;
    for (i = 0; i < NPAGES; i++)
        if (i >= ENGS[eng].first && i < ENGS[eng].first + ENGS[eng].n) {
            int k;
            for (k = 0; k < 8; k++)
                if (PAGES[i].k[k] >= 0)
                    VAL[PAGES[i].k[k]] = PAR[PAGES[i].k[k]].df;
        }
}
static uint8_t LENG[2], LCH[2];
static void layers(int slot, int e0, int e1)            /* Chord on channel 1 (0), Bass on channel 2 (1) */
{
    LENG[0] = (uint8_t)e0, LENG[1] = (uint8_t)e1, LCH[0] = 0, LCH[1] = 1;
    rfm_move_ep_layers(&M, slot, 2, LENG, LCH);
    if (e0 != 0xFF)
        vals_for(e0), rfm_move_ep_values(&M, slot, 0, VAL, P_N);
    if (e1 != 0xFF)
        vals_for(e1), rfm_move_ep_values(&M, slot, 1, VAL, P_N);
}
static void give(int slot)                              /* the host: the description, the layers, the values */
{
    CHECK(rfm_move_ep_describe(&M, slot, BLOB, blob_n) == 0);
    layers(slot, E_VA, E_FM6);
}
static int watch_of(int *slot, int *layer)              /* the last EP_WATCH: its page (-2 none sent) */
{
    const rfm_move_act_t *a = last(RFM_MOVE_ACT_EP_WATCH);
    if (!a)
        return -2;
    *slot = a->slot, *layer = a->layer;
    return a->value;
}
static void words(int k, char *n, char *v)
{
    if (!rfmv_ep_knob_words(&M, k, n, 16, v, 16, NULL, NULL))
        n[0] = v[0] = 0;
}

/* ================================================================ the description checked, fuzzed === */
static uint32_t rng = 12345;
static uint32_t rnd(void)
{
    rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
    return rng;
}
static int check_copy(const uint8_t *b, uint32_t n)    /* checked from a buffer of exactly n bytes (ASan: no read past) */
{
    uint8_t *c = malloc(n ? n : 1);
    int r;
    memcpy(c, b, n);
    r = rfm_move_ep_check(c, n);
    free(c);
    return r;
}
static void refix(uint8_t *b, uint32_t n) { w32(b + 20, fnv(b + 32, n - 32)); }
static void test_e14_check(void)
{
    static uint8_t b[RFM_MOVE_EP_DESC_MAX];
    uint32_t n, i;
    int r, bad;
    fresh();
    make_blob();
    CHECK(rfm_move_ep_check(BLOB, blob_n) == 0);
    CHECK(rfm_move_ep_check(NULL, blob_n) < 0 && rfm_move_ep_check(BLOB, 0) < 0 && rfm_move_ep_check(BLOB, 31) < 0);
    for (n = 0; n < blob_n; n++)                        /* truncated: refused, never a read past n */
        CHECK(check_copy(BLOB, n) < 0);
    for (n = 0; n < blob_n; n += 7) {                   /* truncated with the length and the FNV made to agree */
        memcpy(b, BLOB, n);
        if (n >= 32)
            w32(b + 16, n), refix(b, n);
        CHECK(check_copy(b, n) < 0);
    }
    /* a field out of range, the FNV made to agree: refused */
#define BAD(stmt)                                                                                                   \
    do {                                                                                                            \
        memcpy(b, BLOB, blob_n);                                                                                    \
        stmt;                                                                                                       \
        refix(b, blob_n);                                                                                           \
        CHECK(check_copy(b, blob_n) < 0);                                                                           \
    } while (0)
    {
        const uint32_t par = 32 + 16 * 2 + 16 * 2 + 32 * NPAGES, pag = 32 + 16 * 2 + 16 * 2, tab = par + 32 * P_N;
        BAD(b[4] = 2);                                  /* version */
        BAD(w16(b + 8, 0));                             /* no parameters */
        BAD(b[11] = 9);                                 /* nine layers */
        BAD(b[32 + 3] = 0x07);                          /* a layer's name not printable */
        BAD(memset(b + 32, 'A', 11));                   /* .. no NUL in its field */
        BAD(w16(b + 32 + 32 + 16 + 14, NPAGES - 1));    /* FM6's pages past the end */
        BAD(w16(b + pag + 32 * 1 + 12, P_N));           /* a page's knob: no such parameter */
        BAD(w16(b + par + 32 * P_SHAPE, 0));            /* pid 0 */
        BAD(w16(b + par + 32 * P_SHAPE, 101));          /* a pid twice */
        BAD(w16(b + par + 32 * P_SHAPE + 16, -5));      /* max < min */
        BAD(b[par + 32 * P_SHAPE + 20] = 6);            /* no such format */
        BAD(b[par + 32 * P_TUNE + 21] = 5);             /* FIXED with 5 decimals */
        BAD(b[par + 32 * P_ROOT + 21] = 2);             /* NOTE's octave */
        BAD(w16(b + par + 32 * P_CUT + 22, 4));         /* TABLE: no such table */
        BAD(memset(b + par + 32 * P_PAN + 24, 'x', 5)); /* a unit of 5 */
        BAD(w32(b + tab + 8, blob_n));                  /* a table's strings outside the section */
        BAD(w16(b + tab + 4, 60000));                   /* .. more strings than there are */
        BAD(b[tab + 8 * 4 + 1] = 0x01);                 /* .. a string not printable */
        memcpy(b, BLOB, blob_n), b[40] ^= 1;            /* the FNV not refixed: refused */
        CHECK(check_copy(b, blob_n) < 0);
    }
#undef BAD
    for (i = 0, bad = 0; i < 4000; i++) {              /* bit flips, the FNV made to agree half the time */
        uint32_t k;
        memcpy(b, BLOB, blob_n);
        for (k = 0; k < 1 + rnd() % 4; k++)
            b[rnd() % blob_n] ^= (uint8_t)(1u << (rnd() % 8));
        if (i & 1)
            refix(b, blob_n);
        r = check_copy(b, blob_n);
        bad += r < 0;
        if (r == 0) {                                   /* (accepted: every value's words drawn without a fault) */
            const uint32_t np = (uint32_t)b[8] | (uint32_t)b[9] << 8;
            static const int V[] = { -32768, -1200, -1, 0, 1, 3, 60, 127, 5000, 32767 };
            char t[24];
            uint32_t pi;
            int vi;
            CHECK(rfm_move_ep_describe(&M, 0, b, blob_n) == 0);
            for (pi = 0; pi < np; pi++)
                for (vi = 0; vi < (int)(sizeof V / sizeof V[0]); vi++)
                    rfmv_ep_text(&M, 0, (int)pi, V[vi], t, sizeof t);
        }
    }
    rfm_move_ep_describe(&M, 0, NULL, 0);
    CHECKF(bad > 1900, "refused %d", bad);
    for (i = 0; i < 3000; i++) {                        /* random blobs behind a plausible header */
        const uint32_t len = 32 + rnd() % 4000;
        uint32_t k;
        for (k = 0; k < len; k++)
            b[k] = (uint8_t)rnd();
        memcpy(b, "EP1D", 4), w16(b + 4, 1), w16(b + 6, 32), w16(b + 8, 1 + rnd() % 40), b[10] = (uint8_t)(1 + rnd() % 4);
        b[11] = (uint8_t)(1 + rnd() % 3), w16(b + 12, 1 + rnd() % 10), w16(b + 14, rnd() % 3), w32(b + 16, len);
        refix(b, len);
        (void)check_copy(b, len);
    }
    CHECK(1);
}

/* ================================================================ presence, place, arriving === */
static void test_e14_presence(void)
{
    int n, th;
    track_view();
    /* no ep1 anywhere: TRIG, FM-1, then bank 1 at once; the strip six cells */
    jog(1), rest(), jog(1);
    CHECK(rfmv_seq_kpage(&M) == 3 && M.s.bank == 0 && !M.seq.eng && !rfmv_ep_avail(&M));
    screen();
    n = strip_cells(&th);
    CHECKF(n == 6 && th == 2, "cells %d thick %d", n, th);
    write_pbm("e14_noep_bank1");
    jog(-1), rest();
    CHECK(rfmv_seq_kpage(&M) == 1);
    /* a description, no layers yet: still none (the page has nothing to show) */
    make_blob();
    CHECK(rfm_move_ep_describe(&M, 1, BLOB, blob_n) == 0 && !rfm_move_ep_has(&M, 1) && !rfmv_ep_avail(&M));
    jog(1), rest();
    CHECK(rfmv_seq_kpage(&M) == 3);
    jog(-1), rest();
    /* the layers: the page is there, after FM-1; the strip seven cells, the third shown */
    layers(1, E_VA, E_FM6);
    CHECK(rfm_move_ep_has(&M, 1) && rfmv_ep_avail(&M) && rfmv_ep_target(&M, 1) == 1);
    clear(), jog(1);
    CHECK(rfmv_seq_kpage(&M) == 2 && M.seq.eng && rfmv_ep_page(&M) && rfmv_pop_up(&M, POP_EPA) && M.pop_i == 0);
    screen();
    n = strip_cells(&th);
    CHECKF(n == 7 && th == 2, "cells %d thick %d", n, th);
    /* a refused blob: no page; the track comes back on FM-1 */
    {
        static uint8_t b[RFM_MOVE_EP_DESC_MAX];
        memcpy(b, BLOB, blob_n), b[200] ^= 0x40;        /* (the FNV no longer agrees) */
        CHECK(rfm_move_ep_describe(&M, 1, b, blob_n) < 0 && !rfm_move_ep_has(&M, 1));
    }
    adv_ms(10);
    CHECK(!M.seq.eng && rfmv_seq_kpage(&M) == 1 && M.seq.kpage[1] == 1);
    rest(), jog(1), rest();
    CHECK(rfmv_seq_kpage(&M) == 3);
    jog(-2), rest();
    /* the target: the slot played if the track reaches it and it has ep1, else the lowest reached slot that has */
    give(0), give(2);
    CHECK(rfmv_ep_target(&M, 1) == 0);                  /* (slot 2, played, has none: slot 1, the lowest) */
    M.seq.fx.ffilt[0] &= (uint16_t)~(1u << 1);          /* (track 2 not routed to slot 1) */
    CHECK(rfmv_ep_target(&M, 1) == 2);
    give(1);
    CHECK(rfmv_ep_target(&M, 1) == 1);                  /* (the played slot, reached, with ep1) */
    M.seq.fx.ffilt[1] &= (uint16_t)~(1u << 1);
    CHECK(rfmv_ep_target(&M, 1) == 2);
    M.seq.fx.ffilt[2] &= (uint16_t)~(1u << 1);
    CHECK(rfmv_ep_target(&M, 1) == -1 && !rfmv_ep_avail(&M));   /* (a track reaching no ep1 slot: no page) */
    /* an empty slot: no page */
    M.seq.fx.ffilt[0] = M.seq.fx.ffilt[1] = M.seq.fx.ffilt[2] = 0xFFFF;
    rfm_move_ep_describe(&M, 0, NULL, 0), rfm_move_ep_describe(&M, 1, NULL, 0), rfm_move_ep_describe(&M, 2, NULL, 0);
    CHECK(!rfm_move_ep_has(&M, 0) && !rfmv_ep_avail(&M));
}
static void test_e14_jog_order(void)
{
    track_view();
    make_blob(), give(1);
    jog(-6), rest();
    CHECK(rfmv_seq_kpage(&M) == 0 && M.seq.trig);
    jog(1), rest();
    CHECK(rfmv_seq_kpage(&M) == 1 && M.seq.fwp);
    jog(1), rest();
    CHECK(rfmv_seq_kpage(&M) == 2 && M.seq.eng);
    jog(1), rest();
    CHECK(rfmv_seq_kpage(&M) == 3 && M.s.bank == 0);
    jog(3), rest();
    CHECK(rfmv_seq_kpage(&M) == 6 && M.s.bank == 3);
    jog(1), rest();
    CHECK(rfmv_seq_kpage(&M) == 6);                     /* (the end) */
    jog(-4), rest();                                    /* (several detents in one: a page each) */
    CHECK(rfmv_seq_kpage(&M) == 2);
    /* without ep1: several detents skip it */
    rfm_move_ep_describe(&M, 1, NULL, 0), adv_ms(10);
    CHECK(rfmv_seq_kpage(&M) == 1);                     /* (left on ENGINE: back on FM-1) */
    jog(-1), rest(), jog(3), rest();
    CHECK(rfmv_seq_kpage(&M) == 4 && M.s.bank == 1);
}
static void test_e14_arrival(void)
{
    int n, th, s, l;
    track_view();
    make_blob(), give(1);
    select_track(0);                                    /* (track 1, channel 1: Chord, the VA) */
    clear();
    jog(1), rest(), clear(), jog(1);
    CHECK(rfmv_pop_up(&M, POP_EPA) && M.pop_i == 0);
    CHECKF(strstr(said, "Engine, Osc 1") != NULL, "said %s", said);
    CHECK(watch_of(&s, &l) == PG_OSC1 && s == 1 && l == 0);
    screen();
    n = strip_cells(&th);
    CHECK(n == 7 && th == 2);
    CHECK(pixels(0, 0, 64, 9) > 10);                    /* ("Chord  VA" small, top left) */
    CHECK(pixels(0, 12, 128, 36) > 100);                /* (the page's name big) */
    write_pbm("e14_arrive_osc1");
    /* the list to FILTER; away and back: "FILTER" */
    jog_click(), jog(1), jog_click(), rest();
    jog(1), rest(), clear(), jog(-1);
    CHECKF(strstr(said, "Engine, Filter") != NULL, "said %s", said);
    write_pbm("e14_arrive_filter");
    rest();
    CHECK(!rfmv_pop_up(&M, POP_EPA));
    write_pbm("e14_rest");
    /* another track (left on a bank), then this one again: it was left on ENGINE: arriving again */
    select_track(4);
    CHECK(!M.seq.eng);
    clear(), select_track(0);
    CHECK(M.seq.eng && rfmv_seq_kpage(&M) == 2);
    CHECK(strstr(said, "Engine, Filter") != NULL);
    /* session view and back with Menu: the page is there, no arrival popup (the view is named) */
    press(MR_CC_MENU), rest();
    CHECK(!rfmv_ep_page(&M));
    press(MR_CC_MENU);
    CHECK(rfmv_ep_page(&M) && !(M.pop == POP_EPA && rfmv_pop_up(&M, POP_EPA)));
}

/* ================================================================ the knobs === */
static void test_e14_knobs(void)
{
    char n[16], v[16];
    uint8_t w[RFM_MOVE_NLEDS][3];
    const rfm_move_act_t *a;
    int k;
    track_view();
    make_blob(), give(1);
    select_track(0);
    jog(1), rest(), jog(1), rest();                     /* (ENGINE: OSC 1) */
    CHECK(M.seq.eng && rfmv_ep_knob_param(&M, 0) == P_WAVE && rfmv_ep_knob_param(&M, 5) < 0);
    /* the words of every format */
    words(0, n, v); CHECKF(!strcmp(n, "Wave") && !strcmp(v, "Saw"), "%s %s", n, v);   /* TABLE (LIST, DYN no cell) */
    words(1, n, v); CHECKF(!strcmp(v, "50%"), "%s", v);                               /* PERCENT */
    words(2, n, v); CHECKF(!strcmp(v, "0 oct"), "%s", v);                             /* INT, a unit */
    words(3, n, v); CHECKF(!strcmp(v, "0.00 st"), "%s", v);                           /* FIXED(2) */
    words(4, n, v); CHECKF(!strcmp(v, "Off"), "%s", v);                               /* ONOFF */
    rfmv_ep_text(&M, 1, P_OCT, 2, v, sizeof v); CHECKF(!strcmp(v, "+2 oct"), "%s", v);   /* BIPOLAR's sign */
    rfmv_ep_text(&M, 1, P_TUNE, -50, v, sizeof v); CHECKF(!strcmp(v, "-0.50 st"), "%s", v);
    rfmv_ep_text(&M, 1, P_ENVAMT, 240, v, sizeof v); CHECKF(!strcmp(v, "+24.0"), "%s", v);
    rfmv_ep_text(&M, 1, P_CUT, 64, v, sizeof v); CHECKF(!strcmp(v, "1.2k"), "%s", v);
    rfmv_ep_text(&M, 1, P_CUT, 200, v, sizeof v); CHECKF(!strcmp(v, "200"), "%s", v);   /* (outside the table: INT) */
    rfmv_ep_text(&M, 1, P_ROOT, 60, v, sizeof v); CHECKF(!strcmp(v, "C3"), "%s", v);     /* NOTE(3) */
    rfmv_ep_text(&M, 1, P_ROOT, 61, v, sizeof v); CHECKF(!strcmp(v, "C#3"), "%s", v);
    rfmv_ep_text(&M, 1, P_ROOT, 0, v, sizeof v); CHECKF(!strcmp(v, "C-2"), "%s", v);
    rfmv_ep_text(&M, 1, P_SUS, 127, v, sizeof v); CHECKF(!strcmp(v, "100%"), "%s", v);
    rfmv_ep_text(&M, 1, P_LDEPTH, 50, v, sizeof v); CHECKF(!strcmp(v, "+50%"), "%s", v);   /* PERCENT bipolar */
    rfmv_ep_text(&M, 1, P_MOD1, -64, v, sizeof v); CHECKF(!strcmp(v, "-100%"), "%s", v);
    rfmv_ep_text(&M, 1, P_MOD1, 0, v, sizeof v); CHECKF(!strcmp(v, "0%"), "%s", v);
    rfmv_ep_text(&M, 1, P_REL, 420, v, sizeof v); CHECKF(!strcmp(v, "420 ms"), "%s", v);
    rfmv_ep_text(&M, 1, P_SYNC, 1, v, sizeof v); CHECKF(!strcmp(v, "On"), "%s", v);
    /* the lights: the slot's colour (slot 2: blue) by value, the empty knobs dark */
    leds(w);
    for (k = 0; k < 5; k++)
        CHECKF(is_slot(w[L_RING0 + k], 1), "knob %d %02x%02x%02x", k, w[L_RING0 + k][0], w[L_RING0 + k][1], w[L_RING0 + k][2]);
    for (k = 5; k < 8; k++)
        CHECK(dark(w[L_RING0 + k]));
    CHECK(sum(w[L_RING0 + 1]) > sum(w[L_RING0 + 0]));   /* (Shape at the middle, Wave at its first) */
    /* a touch: the name, the value, white; R5 */
    clear(), touch(1, 1);
    CHECK(rfmv_pop_up(&M, POP_EPK) && M.pop_i == 1);
    leds(w);
    CHECK(white(w[L_RING0 + 1]));
    CHECKF(strstr(said, "Shape, 50%") != NULL, "said %s", said);
    screen();
    CHECK(pixels(0, 0, 128, 16) > 20 && pixels(0, 20, 128, 30) > 60 && pixels(4, 52, 120, 5) > 50);
    write_pbm("e14_touch_percent");
    touch(1, 0), rest();
    CHECK(!rfmv_pop_up(&M, POP_EPK));
    /* a touch on an empty knob: nothing, dark, dead */
    clear(), touch(6, 1), knob(6, 3), adv_ms(10);
    CHECK(!rfmv_pop_up(&M, POP_EPK) && !count(RFM_MOVE_ACT_EP) && !count(RFM_MOVE_ACT_MIDI));
    touch(6, 0), rest();
    /* a turn: the step (Shape: automatic, 1 a tick for 128 steps), the popup follows */
    clear(), touch(1, 1), knob(1, 3), adv_ms(10);
    a = last(RFM_MOVE_ACT_EP);
    CHECK(a && a->value == 67 && a->pid == 102);
    words(1, n, v); CHECKF(!strcmp(v, "53%"), "%s", v);
    touch(1, 0), rest();
    /* a LIST: one item every RFM_MOVE_LIST_TICKS ticks */
    clear(), touch(0, 1);
    knob(0, RFM_MOVE_LIST_TICKS - 1), adv_ms(10);
    CHECK(!count(RFM_MOVE_ACT_EP));
    knob(0, 1), adv_ms(10);
    a = last(RFM_MOVE_ACT_EP);
    CHECK(a && a->pid == 101 && a->value == 1);
    words(0, n, v); CHECKF(!strcmp(v, "Square"), "%s", v);
    write_pbm("e14_turn_list");
    touch(0, 0), rest();
    /* a DYN: its words from the watched cells (the cell is that pid), else its format */
    {
        rfm_move_ep_cell_t cl[8];
        memset(cl, 0, sizeof cl);
        cl[0].pid = 101, snprintf(cl[0].name, 11, "Color"), snprintf(cl[0].text, 11, "Pink");
        rfm_move_ep_values(&M, 1, 0, (vals_for(E_VA), VAL[P_WAVE] = 1, VAL), P_N);
        rfm_move_ep_cells(&M, 1, 0, PG_OSC1, cl);
        words(0, n, v); CHECKF(!strcmp(n, "Color") && !strcmp(v, "Pink"), "%s %s", n, v);
        rfm_move_ep_cells(&M, 1, 1, PG_OSC1, cl);       /* (another layer's cells: not these) */
        words(0, n, v); CHECKF(!strcmp(n, "Wave") && !strcmp(v, "Square"), "%s %s", n, v);
        rfm_move_ep_cells(&M, 1, 0, PG_OSC1, cl);
        clear(), touch(0, 1);
        CHECKF(strstr(said, "Color, Pink") != NULL, "said %s", said);
        write_pbm("e14_touch_dyn");
        touch(0, 0), rest();
    }
    /* the FILTER page: TABLE Cutoff, a bipolar FIXED, a list type */
    jog_click(), jog(1), jog_click(), rest();
    CHECK(rfmv_ep_knob_param(&M, 0) == P_CUT);
    touch(0, 1);
    words(0, n, v); CHECKF(!strcmp(n, "Cutoff") && !strcmp(v, "1.2k"), "%s %s", n, v);
    write_pbm("e14_touch_table");
    touch(0, 0), rest();
    clear(), touch(2, 1), knob(2, 24);
    words(2, n, v); CHECKF(!strcmp(n, "Env amt") && !strcmp(v, "+16.8"), "%s %s", n, v);   /* (auto step 961 / 128: 7) */
    write_pbm("e14_turn_bipolar");
    touch(2, 0), rest();
    /* Shift held: one stored step a tick (fine); ENV 1's Attack (automatic step 39), Release (its own step 10) */
    jog_click(), jog(1), jog_click(), rest();
    CHECK(rfmv_ep_knob_param(&M, 0) == P_ATK && rfmv_ep_knob_param(&M, 3) == P_REL);
    clear(), knob(0, 2), adv_ms(10);
    CHECK(last(RFM_MOVE_ACT_EP) && last(RFM_MOVE_ACT_EP)->value == 10 + 2 * 39);
    clear(), button(MR_CC_SHIFT, 1), knob(0, 2), button(MR_CC_SHIFT, 0), adv_ms(10);
    CHECK(last(RFM_MOVE_ACT_EP) && last(RFM_MOVE_ACT_EP)->value == 10 + 2 * 39 + 2);
    CHECK(!M.seq.shift_lock);
    clear(), knob(3, -1), adv_ms(10);
    CHECK(last(RFM_MOVE_ACT_EP) && last(RFM_MOVE_ACT_EP)->value == 410);
    words(3, n, v); CHECKF(!strcmp(v, "410 ms"), "%s", v);
    clear(), knob(0, -60), adv_ms(10);                  /* (clamped at the range's end) */
    CHECK(last(RFM_MOVE_ACT_EP) && last(RFM_MOVE_ACT_EP)->value == 0);
    rest();
    /* the MIX page's NOTE */
    jog_click(), jog(4), jog_click(), rest();
    CHECK(rfmv_ep_knob_param(&M, 2) == P_ROOT);
    clear(), touch(2, 1), knob(2, 2);
    CHECKF(strstr(said, "Root, D3") != NULL, "said %s", said);
    words(2, n, v); CHECKF(!strcmp(v, "D3"), "%s", v);
    write_pbm("e14_turn_note");
    touch(2, 0), rest();
    /* a held step: the TRIG knobs (as on the FM-1 page), not the page's */
    clear(), step(3, 1), knob(1, 2), adv_ms(10);
    CHECK(!count(RFM_MOVE_ACT_EP));
    step(3, 0), rest();
}
/* several ticks in one block: one SET, the latest value, frame 0; the value shown until the record agrees */
static void test_e14_merge(void)
{
    int i, n;
    track_view();
    make_blob(), give(1);
    select_track(0);
    jog(1), rest(), jog(1), rest();
    clear();
    knob(1, 1), knob(1, 1), knob(1, 1), knob(2, 1);    /* (no rfm_move_seq_advance between them) */
    CHECK(!count(RFM_MOVE_ACT_EP));
    rfm_move_seq_advance(&M, 128, 44100);
    CHECK(count(RFM_MOVE_ACT_EP) == 2);
    for (i = 0, n = 0; i < nact; i++)
        if (A[i].kind == RFM_MOVE_ACT_EP) {
            CHECK(A[i].frame == 0 && A[i].op == RFM_MOVE_EP_SET && A[i].slot == 1 && A[i].layer == 0);
            n += (A[i].pid == 102 && A[i].value == 67) + (A[i].pid == 103 && A[i].value == 1);
        }
    CHECK(n == 2);
    clear(), rfm_move_seq_advance(&M, 128, 44100);
    CHECK(!count(RFM_MOVE_ACT_EP));                     /* (sent once) */
    /* the firmware's record still the old value: the sent one shown (no flicker back) */
    vals_for(E_VA), rfm_move_ep_values(&M, 1, 0, VAL, P_N);
    CHECK(rfmv_ep_value(&M, 1, 0, P_SHAPE) == 67);
    /* .. it agrees: the record's from now (a firmware's own change shows at once) */
    VAL[P_SHAPE] = 67, rfm_move_ep_values(&M, 1, 0, VAL, P_N);
    VAL[P_SHAPE] = 90, rfm_move_ep_values(&M, 1, 0, VAL, P_N);
    CHECK(rfmv_ep_value(&M, 1, 0, P_SHAPE) == 90);
    /* .. it never agrees (clamped by the firmware): the record's after RFMV_EP_OVR_MS */
    clear(), knob(2, 1), adv_ms(10);
    CHECK(rfmv_ep_value(&M, 1, 0, P_OCT) == 2);
    adv_ms(RFMV_EP_OVR_MS);
    CHECK(rfmv_ep_value(&M, 1, 0, P_OCT) == 0);
    /* a firmware's own change: the light follows, nothing pops up */
    rest();
    VAL[P_SHAPE] = 127, rfm_move_ep_values(&M, 1, 0, VAL, P_N);
    {
        uint8_t w[RFM_MOVE_NLEDS][3];
        leds(w);
        CHECK(is_slot(w[L_RING0 + 1], 1) && sum(w[L_RING0 + 1]) > 200);
        CHECK(!rfmv_pop_up(&M, POP_EPK));
        write_pbm("e14_fw_changed");
    }
}

/* ================================================================ the watch === */
static void test_e14_watch(void)
{
    int s, l;
    track_view();
    make_blob(), give(1);
    clear(), jog(1), rest(), jog(1);                    /* (track 2: Bass, FM6, OP 1) */
    CHECK(watch_of(&s, &l) == PG_OP1 && s == 1 && l == 1);
    rest(), clear(), jog_click(), jog(1);               /* (the list's highlight) */
    CHECK(count(RFM_MOVE_ACT_EP_WATCH) == 1 && watch_of(&s, &l) == PG_OP2);
    jog_click(), rest(), clear();
    jog(1);                                             /* (left the page: none) */
    CHECK(count(RFM_MOVE_ACT_EP_WATCH) == 1 && watch_of(&s, &l) == -1 && s == 1);
    clear(), jog(-1);
    CHECK(watch_of(&s, &l) == PG_OP2 && l == 1);
    rest(), clear(), select_track(0);                   /* (another track: its layer, its page) */
    CHECK(watch_of(&s, &l) == -1);                      /* (track 1 never on ENGINE: TRIG) */
    jog(2), rest(), clear(), select_track(1);
    CHECK(watch_of(&s, &l) == PG_OP2 && l == 1);
    clear(), select_track(0);
    CHECK(watch_of(&s, &l) == PG_OSC1 && l == 0);       /* (both on ENGINE: the watch follows the track) */
    clear(), press(MR_CC_MENU), rest();                 /* (session view: no page shows) */
    CHECK(watch_of(&s, &l) == -1);
    clear(), press(MR_CC_MENU), rest();
    CHECK(watch_of(&s, &l) == PG_OSC1);
    clear(), layers(1, E_FM6, E_FM6);                   /* (the engine changed: no OSC 1 on FM6: the page last shown
                                                         * on FM6, OP 2, the bass's) */
    CHECK(watch_of(&s, &l) == PG_OP2 && l == 0);
    clear(), layers(1, 0xFF, E_FM6);                    /* (the layer off: none) */
    CHECK(watch_of(&s, &l) == -1);
}

/* ================================================================ the page list === */
static void test_e14_list(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int i;
    track_view();
    make_blob(), give(1);
    select_track(0);
    jog(1), rest(), jog(1), rest();
    /* the click: at its release */
    clear(), jog_down(1);
    CHECK(!M.seq.ep_list);
    jog_down(0);
    CHECK(M.seq.ep_list && rfmv_ep_list_shown(&M));
    CHECKF(strstr(said, "Pages, Osc 1") != NULL, "said %s", said);
    screen();
    CHECK(pixels(0, 0, 64, 9) > 10 && pixels(0, 9, 128, 1) == 128);   /* (the title line, its rule) */
    CHECK(pixels(0, 11, 125, 17) > 1000);               /* (OSC 1 inverted, the first row) */
    CHECK(pixels(126, 11, 2, 53) > 0);                  /* (the scroll mark) */
    write_pbm("e14_list_open");
    /* its turn: a page a detent, the knobs follow at once; no wrap */
    clear(), jog(1);
    CHECK(rfmv_ep_knob_param(&M, 0) == P_CUT && strstr(said, "Filter") != NULL);
    leds(w);
    CHECK(is_slot(w[L_RING0 + 3], 1) && dark(w[L_RING0 + 4]));   /* (FILTER: four knobs) */
    write_pbm("e14_list_filter");
    jog(-5);
    CHECK(rfmv_ep_knob_param(&M, 0) == P_WAVE);
    jog(20);
    CHECK(rfmv_ep_knob_param(&M, 0) == P_LEVEL);        /* (MIX, the last; no wrap) */
    screen();
    CHECK(pixels(0, 47, 125, 17) > 600);                /* (the last row inverted: scrolled) */
    write_pbm("e14_list_end");
    jog(-4);
    CHECK(rfmv_ep_knob_param(&M, 0) == P_ATK);          /* (ENV 1) */
    /* a knob while it is open: the highlighted page's, its popup over the list a second */
    clear(), knob(0, 1), adv_ms(10);
    CHECK(last(RFM_MOVE_ACT_EP) && last(RFM_MOVE_ACT_EP)->pid == 120 && rfmv_pop_up(&M, POP_EPK));
    write_pbm("e14_list_knob");
    rest();
    CHECK(M.seq.ep_list && !rfmv_pop_up(&M, POP_EPK));
    /* pads and steps keep working: a step held takes over (the track view), the list back at its release */
    step(2, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    CHECK(!rfmv_ep_list_shown(&M) && M.seq.ep_list);
    jog(1);                                             /* (the held step's: its notes' length, not the list) */
    CHECK(rfmv_ep_knob_param(&M, 0) == P_ATK);
    write_pbm("e14_list_step_held");
    step(2, 0), rest();
    CHECK(rfmv_ep_list_shown(&M));
    button(MR_CC_DELETE, 1);
    CHECK(!rfmv_ep_list_shown(&M));
    button(MR_CC_DELETE, 0), rest();
    CHECK(rfmv_ep_list_shown(&M));
    button(MR_CC_LOOP, 1), adv_ms(RFM_MOVE_SEQ_HOLD_MS + 20);
    screen();
    CHECK(rfmv_seq_loop_over(&M));
    button(MR_CC_LOOP, 0), rest();
    CHECK(rfmv_ep_list_shown(&M));
    hit(10);                                            /* (a pad plays) */
    CHECK(M.seq.ep_list);
    /* the click again: closed on the highlighted page */
    clear(), jog_click();
    CHECK(!M.seq.ep_list && rfmv_ep_knob_param(&M, 0) == P_ATK);
    clear(), jog(1);                                    /* (the jog its pages again) */
    CHECK(rfmv_seq_kpage(&M) == 3);
    jog(-1), rest();
    /* Back closes it, nothing else changes (no mode, no view) */
    jog_click();
    CHECK(M.seq.ep_list && rfmv_back_has(&M));
    jog(1), back();
    CHECK(!M.seq.ep_list && rfmv_ep_page(&M) && rfmv_ep_knob_param(&M, 0) == P_LRATE && M.s.mode == RFM_MOVE_MODE_SEQ);
    /* Shift + click: nothing; Delete held + click: nothing (the list does not open) */
    rest(), button(MR_CC_SHIFT, 1), jog_click(), button(MR_CC_SHIFT, 0);
    CHECK(!M.seq.ep_list && !M.seq.shift_lock);
    rest(), button(MR_CC_DELETE, 1), jog_click(), button(MR_CC_DELETE, 0);
    CHECK(!M.seq.ep_list);
    /* a Shift menu over the list: Back closes the menu first, then the list */
    rest(), jog_click();
    button(MR_CC_SHIFT, 1), step(1, 1), step(1, 0), button(MR_CC_SHIFT, 0);   /* (Shift + step 2: Global settings) */
    if (M.seq.smenu) {
        back();
        CHECK(!M.seq.smenu && M.seq.ep_list);
    }
    back();
    CHECK(!M.seq.ep_list);
    /* leaving the track view closes it */
    jog_click();
    CHECK(M.seq.ep_list);
    press(MR_CC_MENU), rest();
    CHECK(!M.seq.ep_list);
    press(MR_CC_MENU), rest();
    for (i = 0; i < 2; i++)
        jog(-1);
}

/* ================================================================ things changing under the page === */
static void test_e14_engine_change(void)
{
    char n[16], v[16];
    uint8_t w[RFM_MOVE_NLEDS][3];
    track_view();
    make_blob(), give(1);
    select_track(0);
    jog(1), rest(), jog(1), rest();
    jog_click(), jog(1), jog_click(), rest();           /* (VA: FILTER) */
    CHECK(rfmv_ep_knob_param(&M, 0) == P_CUT);
    /* Chord's engine to FM6: "Chord" over "FM6" a second; FM6 has no FILTER, never shown: its first */
    clear(), layers(1, E_FM6, E_FM6);
    CHECK(rfmv_pop_up(&M, POP_EPA) && M.pop_i == 1 && rfmv_ep_knob_param(&M, 0) == P_OP1L);
    adv_ms(10);                                         /* (said at the core's next call) */
    CHECKF(strstr(said, "Chord, FM6") != NULL, "said %s", said);
    write_pbm("e14_engine_changed");
    leds(w);
    CHECK(is_slot(w[L_RING0 + 2], 1) && dark(w[L_RING0 + 3]));   /* (relit: OP 1's three knobs) */
    rest();
    /* FM6's OP 2, then back to the VA: no OP 2 there: the one last shown on it (FILTER) */
    jog_click(), jog(1), jog_click(), rest();
    CHECK(rfmv_ep_knob_param(&M, 0) == P_OP2L);
    layers(1, E_VA, E_FM6), rest();
    CHECK(rfmv_ep_knob_param(&M, 0) == P_CUT);
    /* the VA's MIX, to FM6: its own MIX (the same name) */
    jog_click(), jog(5), jog_click(), rest();
    CHECK(rfmv_ep_knob_param(&M, 0) == P_LEVEL);
    layers(1, E_FM6, E_FM6), rest();
    words(0, n, v);
    CHECK(rfmv_ep_knob_param(&M, 0) == P_LEVEL && !strcmp(n, "Level"));
    {                                                   /* (MIX: FM6's page, not the VA's) */
        int s, l;
        clear(), layers(1, E_VA, E_FM6), layers(1, E_FM6, E_FM6);
        CHECK(watch_of(&s, &l) == PG_FMMIX);
    }
    /* back to FM6's OP 2 on FM6, to the VA and back: FM6 remembers OP 2 */
    rest(), jog_click(), jog(-3), jog_click(), rest();
    CHECK(rfmv_ep_knob_param(&M, 0) == P_OP2L);
    layers(1, E_VA, E_FM6), rest();
    CHECK(rfmv_ep_knob_param(&M, 0) == P_LEVEL);       /* (OP 2 not on the VA; the VA's last: MIX) */
    layers(1, E_FM6, E_FM6), rest();
    CHECK(rfmv_ep_knob_param(&M, 0) == P_LEVEL);        /* (from the VA's MIX: FM6's MIX, the name first) */
    /* the list open follows the engine */
    jog_click();
    layers(1, E_VA, E_FM6);
    CHECK(M.seq.ep_list && rfmv_ep_list_shown(&M));
    rest();
    write_pbm("e14_list_followed");
    jog_click(), rest();
    /* the bass on track 2 (channel 2): FM6's page, not the chord's */
    select_track(1), jog(2), rest();
    CHECK(M.seq.eng && rfmv_ep_layer_of(&M, 1, 1) == 1 && rfmv_ep_knob_param(&M, 0) == P_LEVEL);   /* (FM6's page last
                                                         * shown: the chord's MIX) */
    write_pbm("e14_bass_track2");
}
static void test_e14_layer_off(void)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int k, s, l;
    track_view();
    make_blob(), give(1);
    layers(1, E_VA, 0xFF);                              /* (ChoralRoot's bass off) */
    CHECK(rfmv_ep_avail(&M));                           /* (still there, so the owner sees why) */
    clear(), jog(1), rest(), jog(1);
    CHECK(M.seq.eng && rfmv_pop_up(&M, POP_EPA) && rfmv_ep_knob_param(&M, 0) < 0);
    CHECKF(strstr(said, "Engine, Bass off") != NULL, "said %s", said);
    CHECK(watch_of(&s, &l) == -2 || watch_of(&s, &l) == -1);
    screen();
    CHECK(pixels(0, 12, 128, 36) > 50);                 /* ("Off" big) */
    write_pbm("e14_layer_off");
    leds(w);
    for (k = 0; k < 8; k++)
        CHECK(dark(w[L_RING0 + k]));
    clear(), knob(0, 3), touch(1, 1), adv_ms(10);
    CHECK(!count(RFM_MOVE_ACT_EP) && !rfmv_pop_up(&M, POP_EPK));
    touch(1, 0), rest();
    jog_click();                                        /* (no pages: no list; its "Off" again) */
    CHECK(!M.seq.ep_list && rfmv_pop_up(&M, POP_EPA));
    rest();
    /* the bass on again: "Bass" over "FM6", the knobs lit */
    clear(), layers(1, E_VA, E_FM6);
    CHECK(rfmv_pop_up(&M, POP_EPA) && M.pop_i == 1 && rfmv_ep_knob_param(&M, 0) == P_OP1L);
    CHECK(watch_of(&s, &l) == PG_OP1 && l == 1);
    leds(w);
    CHECK(is_slot(w[L_RING0 + 0], 1));
}
static void test_e14_gone(void)
{
    int s, l;
    static uint8_t b2[RFM_MOVE_EP_DESC_MAX];
    tpage_t pg2[NPAGES];
    track_view();
    make_blob(), give(1);
    jog(1), rest(), jog(1), rest();
    jog_click(), jog(1), jog_click(), rest();           /* (Bass, FM6: OP 2) */
    CHECK(rfmv_ep_knob_param(&M, 0) == P_OP2L);
    /* the description replaced (the pages in another order, the ids kept): OP 2 found again by (engine, page id) */
    memcpy(pg2, PAGES, sizeof pg2);
    pg2[PG_OP1] = PAGES[PG_OP2], pg2[PG_OP2] = PAGES[PG_OP1];
    {
        const uint32_t n2 = ep_write(b2, LAYERS, 2, ENGS, 2, pg2, NPAGES, PAR, P_N, TABS, 4);
        clear();
        CHECK(rfm_move_ep_describe(&M, 1, b2, n2) == 0);
        layers(1, E_VA, E_FM6);
        CHECK(rfmv_ep_knob_param(&M, 0) == P_OP2L && watch_of(&s, &l) == PG_OP1);   /* (its index now 7) */
    }
    /* the slot emptied (another firmware coming): the page goes, the track on FM-1, the watch none */
    clear(), rfm_move_ep_describe(&M, 1, NULL, 0);
    CHECK(M.seq.eng);                                   /* (not before the core's next call: it may come back) */
    adv_ms(10);
    CHECK(!M.seq.eng && rfmv_seq_kpage(&M) == 1 && watch_of(&s, &l) == -1 && s == 1);
    write_pbm("e14_gone_fm1");
    /* described again in one go (the host's describe(NULL) then the new one): the page follows */
    jog(1), rest();                                     /* (bank 1 without ENGINE) */
    jog(-1), rest();
    give(1);
    jog(1), rest();
    CHECK(M.seq.eng);
    rfm_move_ep_describe(&M, 1, NULL, 0), give(1), adv_ms(10);
    CHECK(M.seq.eng && rfmv_ep_knob_param(&M, 0) == P_OP1L);   /* (the old firmware's memory gone: its first) */
    /* a takeover, the projects view, FM-1 mode: no page there */
    tap_track(0), rest();
    CHECK(!rfmv_ep_page(&M));
}
static void test_e14_channel(void)
{
    track_view();
    make_blob(), give(1);
    CHECK(M.seq.tr[1].ch == 1 && rfmv_ep_layer_of(&M, 1, 1) == 1);   /* (track 2 plays channel 2: the record's 1: Bass) */
    CHECK(M.seq.tr[0].ch == 0 && rfmv_ep_layer_of(&M, 0, 1) == 0);
    CHECK(M.seq.tr[4].ch == 4 && rfmv_ep_layer_of(&M, 4, 1) == 0);   /* (no layer on channel 5: layer 1) */
    LENG[0] = E_VA, LENG[1] = E_FM6, LCH[0] = 4, LCH[1] = 4;   /* (both on channel 5: the lowest) */
    rfm_move_ep_layers(&M, 1, 2, LENG, LCH);
    CHECK(rfmv_ep_layer_of(&M, 4, 1) == 0 && rfmv_ep_layer_of(&M, 1, 1) == 0);
    LCH[0] = 0xFF, LCH[1] = 4;
    rfm_move_ep_layers(&M, 1, 2, LENG, LCH);
    CHECK(rfmv_ep_layer_of(&M, 4, 1) == 1);
    M.ep.lset[4] = 1;                                   /* (the Layer setting's seam: a layer by number) */
    CHECK(rfmv_ep_layer_of(&M, 4, 1) == 0);
    M.ep.lset[4] = 0;
    /* the channel changed (Track 4 held + the jog): the page follows the layer */
    layers(1, E_VA, E_FM6);
    jog(1), rest(), jog(1), rest();
    CHECK(rfmv_ep_knob_param(&M, 0) == P_OP1L);
    M.seq.tr[1].ch = 0, adv_ms(10);
    CHECK(rfmv_ep_knob_param(&M, 0) == P_WAVE && rfmv_pop_up(&M, POP_EPA));
}

int main(void)
{
    test_e14_check();
    test_e14_presence();
    test_e14_jog_order();
    test_e14_arrival();
    test_e14_knobs();
    test_e14_merge();
    test_e14_watch();
    test_e14_list();
    test_e14_engine_change();
    test_e14_layer_off();
    test_e14_gone();
    test_e14_channel();
    return check_done("move_engine");
}
