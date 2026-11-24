/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm_move: an Ableton Move as a RemoteFM1 panel. See rfm1cs/move.h and README.md (the tables, the
 * decisions). Portable C99: no OS calls, no allocation. */
#include <stdio.h>
#include <string.h>

#include "move_int.h"

#define clampi rfmv_clampi
#define SCALES rfmv_scales
#define NOTE_NAMES rfmv_note_names
#define SCALE_NAMES rfmv_scale_names
#define MODE_RGB rfmv_mode_rgb
#define BANK_RGB rfmv_bank_rgb
#define WHITE RFMV_WHITE
#define RED RFMV_RED
#define GREEN RFMV_GREEN
#define OFF RFMV_OFF
#define sc rfmv_sc
#define add rfmv_add
#define emit rfmv_emit
#define emit_midi rfmv_emit_midi
#define changed rfmv_changed
#define note_hold rfmv_note_hold
#define note_let rfmv_note_let
#define let_go rfmv_let_go
#define midi_pad rfmv_midi_pad
typedef rfmv_rgb_t rgb_t;

/* ================================================================ the mapping === */
/* pads 0..31 (bottom-left first, rows of 8): the FM-1 key each plays in FM-1 mode. The design's rows, top first:
 *   row 1  -  F# G# A# -  C# D# -        (keys 1 3 5, 8 10)
 *   row 2  F  G  A  B  C  D  E  F        (keys 0 2 4 6 7 9 11 12)
 *   row 3  F# G# A# -  C# D# -  F#       (keys 13 15 17, 20 22, 25)
 *   row 4  G  A  B  C  D  E  F  G        (keys 14 16 18 19 21 23 24 26) */
static const int8_t PAD_KEY[RFM_MOVE_NPADS] = {
    14, 16, 18, 19, 21, 23, 24, 26,   /* row 4 (bottom) */
    13, 15, 17, -1, 20, 22, -1, 25,   /* row 3 */
    0,  2,  4,  6,  7,  9,  11, 12,   /* row 2 */
    -1, 1,  3,  5,  -1, 8,  10, -1,   /* row 1 (top) */
};
/* steps 1..16: the 16 white keys, F3 .. G5 */
static const int8_t STEP_KEY[RFM_MOVE_NSTEPS] = { 0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26 };
/* knobs 1..8 in the FM-1 layout (0.8.0, proposal 2): 1 SELECT, 2 PRESETS, 3 ALGO, 4 none, 5..8 K1..K4 */
static const int8_t KNOB_ENC[RFM_MOVE_NKNOBS] = {
    RFM_MOVE_E_SELECT, RFM_MOVE_E_PRESETS, RFM_MOVE_E_ALGO, -1, RFM_MOVE_E_K1, RFM_MOVE_E_K2, RFM_MOVE_E_K3, RFM_MOVE_E_K4,
};
/* Move button (CC) -> FM-1 button */
static const struct { uint8_t cc, b; } BUTTONS[] = {
    { MR_CC_CAPTURE, RFM_MOVE_B_FX }, { MR_CC_SAMPLE, RFM_MOVE_B_SEL }, { MR_CC_MUTE, RFM_MOVE_B_ENV },
    { MR_CC_DELETE, RFM_MOVE_B_LFO }, { MR_CC_COPY, RFM_MOVE_B_EDIT }, { MR_CC_SHIFT, RFM_MOVE_B_GLO },
    { MR_CC_MENU, RFM_MOVE_B_HOME }, { MR_CC_UNDO, RFM_MOVE_B_SAVE }, { MR_CC_LOOP, RFM_MOVE_B_ARP },
    { MR_CC_PLAY, RFM_MOVE_B_PLAY },   /* (0.4.3: Back is the view flip, no longer SEQ) */ { MR_CC_REC, RFM_MOVE_B_REC },
    { MR_CC_LEFT, RFM_MOVE_B_OCTDN }, { MR_CC_RIGHT, RFM_MOVE_B_OCTUP },
    { MR_CC_UP, RFM_MOVE_B_SEQ },   /* (0.4.4: FM-1 mode only; the MIDI modes' octave there) */
};
#define NBUTTONS ((int)(sizeof BUTTONS / sizeof BUTTONS[0]))

/* FM1P_KEYMAP_INIT (FM1VST core-glue/common/panel.h; felucca-bsp's FM1_KEYMAP): id at [row][col] */
static const int8_t FM1_KEYMAP[6][11] = {
    { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
    { 5, 11, 4, 10, 3, 9, 2, 8, -1, -1, -1 },
    { 34, 35, 36, 37, 38, 40, 39, 13, 7, 6, 12 },
    { 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33 },
    { 0, 1, 15, 14, 17, 16, 19, 18, 20, 21, 22 },
    { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
};

const uint8_t SCALES[RFM_MOVE_NSCALES][RFM_MOVE_SCALE_W] = {   /* [0] = length (the first 8: the indexes as before) */
    { 7, 0, 2, 4, 5, 7, 9, 11 }, { 7, 0, 2, 3, 5, 7, 8, 10 }, { 7, 0, 2, 3, 5, 7, 9, 10 },
    { 7, 0, 2, 4, 5, 7, 9, 10 }, { 5, 0, 2, 4, 7, 9 },       { 5, 0, 3, 5, 7, 10 },
    { 7, 0, 2, 3, 5, 7, 8, 11 }, { 6, 0, 3, 5, 6, 7, 10 },
    { 7, 0, 1, 3, 5, 7, 8, 10 }, { 7, 0, 2, 4, 6, 7, 9, 11 }, { 7, 0, 1, 3, 5, 6, 8, 10 },   /* phrygian lydian locrian */
    { 7, 0, 2, 3, 5, 7, 9, 11 }, { 6, 0, 2, 4, 6, 8, 10 },                                   /* melodic minor, whole tone */
    { 8, 0, 1, 3, 4, 6, 7, 9, 10 }, { 8, 0, 2, 3, 5, 6, 8, 9, 11 },                         /* diminished H-W, W-H */
    { 7, 0, 2, 3, 6, 7, 8, 11 }, { 7, 0, 1, 4, 5, 7, 8, 10 },                               /* Hungarian minor, phrygian dom. */
    { 5, 0, 1, 5, 7, 10 }, { 5, 0, 2, 3, 7, 8 }, { 5, 0, 1, 5, 6, 10 }, { 5, 0, 1, 3, 7, 8 },   /* in sen, hirajoshi, iwato, pelog */
    { 7, 0, 1, 4, 5, 7, 8, 11 }, { 8, 0, 1, 3, 4, 5, 6, 8, 10 }, { 8, 0, 2, 4, 5, 7, 9, 10, 11 },   /* double harmonic,
                                                         * Spanish 8-tone, bebop dominant */
};
const char *const SCALE_NAMES[RFM_MOVE_NSCALES] = { "MAJ", "MIN", "DOR", "MIX", "PMAJ", "PMIN", "HMIN", "BLUES",
                                                    "PHRYG", "LYD", "LOCR", "MMIN", "WHOLE", "DIM HW", "DIM WH", "HUNG",
                                                    "PHDOM", "INSEN", "HIRA", "IWATO", "PELOG", "DBLHAR", "SPAN8",
                                                    "BEBOP" };
static const char *const MODE_NAMES[5] = { "FM-1", "MIDI", "CONTROL", "SEQ", "TAKEOVER" };
static const char *const LAYOUT_NAMES[4] = { "SCALE", "CHROMA", "DRUM", "OCTAVE" };
static const char *const ENC_NAMES[8] = { "SELECT", "ALGO", "K1", "K2", "K3", "K4", "PRESETS", "MASTER" };
const char *const NOTE_NAMES[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };   /* (Ableton's: flats) */
static const uint8_t DEFAULT_CC[RFM_MOVE_NBANKS] = { 16, 24, 102, 110 };

int rfm_move_pad_key(int pad) { return pad >= 0 && pad < RFM_MOVE_NPADS ? PAD_KEY[pad] : -1; }
int rfm_move_step_key(int step) { return step >= 0 && step < RFM_MOVE_NSTEPS ? STEP_KEY[step] : -1; }
int rfm_move_knob_enc(int knob) { return knob >= 0 && knob < RFM_MOVE_NKNOBS ? KNOB_ENC[knob] : -1; }
int rfm_move_button_of(int cc)
{
    int i;
    for (i = 0; i < NBUTTONS; i++)
        if (BUTTONS[i].cc == cc)
            return BUTTONS[i].b;
    return -1;
}
const char *rfm_move_mode_name(int mode) { return mode >= 0 && mode < 5 ? MODE_NAMES[mode] : "?"; }
const char *rfm_move_layout_name(int l) { return l >= 0 && l < 4 ? LAYOUT_NAMES[l] : "?"; }
const char *rfm_move_scale_name(int s) { return s >= 0 && s < RFM_MOVE_NSCALES ? SCALE_NAMES[s] : "?"; }
const char *rfm_move_enc_name(int e) { return e >= 0 && e < 8 ? ENC_NAMES[e] : "?"; }

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the velocity option d along the jog's order (Dynamic, Fixed, Soft, Linear, Hard); into Fixed from another: the
 * level the last velocity played (the owner's) */
void rfmv_vel_step(rfm_move_t *m, int d)
{
    static const uint8_t ORDER[RFM_MOVE_VEL_N] = { RFM_MOVE_VEL_DYNAMIC, RFM_MOVE_VEL_FIXED, RFM_MOVE_VEL_SOFT,
                                                   RFM_MOVE_VEL_LINEAR, RFM_MOVE_VEL_HARD };
    int i, at = 0;
    for (i = 0; i < RFM_MOVE_VEL_N; i++)
        if (ORDER[i] == m->s.vel_mode)
            at = i;
    at = clampi(at + d, 0, RFM_MOVE_VEL_N - 1);
    if (ORDER[at] == RFM_MOVE_VEL_FIXED && m->s.vel_mode != RFM_MOVE_VEL_FIXED && m->vel_last)
        m->s.vel_level = m->vel_last;
    m->s.vel_mode = ORDER[at];
}

const char *rfm_move_vel_name(int v)
{
    static const char *const N[RFM_MOVE_VEL_N] = { "DYNAMIC", "FIXED", "SOFT", "HARD", "LINEAR" };
    return v >= 0 && v < RFM_MOVE_VEL_N ? N[v] : "?";
}

/* SOFT: a lighter touch plays louder (v^0.5-ish); HARD: it takes a firmer one (v^2-ish) */
int rfm_move_vel(const rfm_move_settings_t *s, int v)
{
    v = clampi(v, 1, 127);
    switch (s->vel_mode) {
    case RFM_MOVE_VEL_FIXED: return clampi(s->vel_level, 1, 127);
    case RFM_MOVE_VEL_SOFT: return clampi(v + (v * (127 - v)) / 127, 1, 127);
    case RFM_MOVE_VEL_HARD: return clampi((v * v + 126) / 127, 1, 127);
    default: return v;
    }
}

/* scale degree d (0 = the root) in semitones above the root */
static int degree(int scale, int d)
{
    const uint8_t *sc = SCALES[scale < RFM_MOVE_NSCALES ? scale : 0];
    return 12 * (d / sc[0]) + sc[1 + d % sc[0]];
}

int rfm_move_pad_note(const rfm_move_settings_t *s, int pad)
{
    const int r = pad / 8, c = pad % 8, base = 48 + s->root + 12 * s->octave;
    int n;
    if (pad < 0 || pad >= RFM_MOVE_NPADS)
        return -1;
    switch (s->layout) {
    case RFM_MOVE_LAYOUT_SCALE: n = base + degree(s->scale, c + 3 * r); break;
    case RFM_MOVE_LAYOUT_CHROMATIC: n = base + c + 5 * r; break;
    case RFM_MOVE_LAYOUT_DRUM: n = s->drum[pad] + 12 * s->octave; break;
    default: n = base + 12 * r + degree(s->scale, c); break;
    }
    return n >= 0 && n <= 127 ? n : -1;
}

/* ================================================================ settings === */
void rfm_move_settings_default(rfm_move_settings_t *s)
{
    int b, k, p;
    memset(s, 0, sizeof *s);
    s->mode = RFM_MOVE_MODE_FM1;
    s->layout = RFM_MOVE_LAYOUT_SCALE;
    s->scale = RFM_MOVE_SCALE_MAJOR;
    s->vel_mode = RFM_MOVE_VEL_DYNAMIC, s->vel_level = 127;
    s->fx_page = RFM_MOVE_FX_PAGE_MIX;                  /* (0.8.0: Track 4's first tap: the Mixer) */
    for (b = 0; b < RFM_MOVE_NBANKS; b++)
        for (k = 0; k < RFM_MOVE_NKNOBS; k++)
            s->cc[b][k] = (uint8_t)(DEFAULT_CC[b] + k);
    for (p = 0; p < RFM_MOVE_NPADS; p++)   /* bottom-right = 36, leftwards, then the next row up from the right */
        s->drum[p] = (uint8_t)(RFM_MOVE_DRUM_BASE + (p / 8) * 8 + (7 - p % 8));
    for (b = 0; b < RFM_MOVE_FW_SLOTS; b++) {           /* (0.8.0) each slot: the FM-1 layout, C major, channel 1,
                                                         * CC 16..23 */
        rfm_move_slotset_t *ss = &s->slot[b];
        ss->lay = RFM_MOVE_LAY_FM1, ss->scale = RFM_MOVE_SCALE_MAJOR;
        for (k = 0; k < RFM_MOVE_NKNOBS; k++)
            ss->cc[k] = (uint8_t)(DEFAULT_CC[0] + k);
        s->lock[b] = (uint8_t)b;
    }
    rfmv_smp_defaults(s->smp);                          /* (0.15.0) the sampler's */
}

uint32_t rfm_move_save(const rfm_move_settings_t *s, uint8_t o[RFM_MOVE_SETTINGS_BYTES])
{
    uint8_t *p = o;
    *p++ = 'R', *p++ = 'M', *p++ = 'V', *p++ = RFM_MOVE_SETTINGS_VERSION;
    *p++ = (uint8_t)(RFM_MOVE_SETTINGS_BYTES & 0xFF), *p++ = (uint8_t)(RFM_MOVE_SETTINGS_BYTES >> 8);
    *p++ = s->mode, *p++ = s->layout, *p++ = s->channel, *p++ = (uint8_t)s->octave;
    *p++ = s->scale, *p++ = s->root, *p++ = s->bank;
    memcpy(p, s->cc, sizeof s->cc), p += sizeof s->cc;
    memcpy(p, s->drum, sizeof s->drum), p += sizeof s->drum;
    *p++ = s->midi_view, *p++ = s->vel_mode, *p++ = s->vel_level;
    *p++ = s->fw_track, *p++ = s->fw_scope, *p++ = (uint8_t)(s->fw_filter & 0xFF), *p++ = (uint8_t)(s->fw_filter >> 8);
    *p++ = s->fw_cat_scope, *p++ = (uint8_t)(s->fx_page | (s->fw_fx ? 0x80 : 0));   /* (version 4; 0.4.5: FX setup's
                                                         * memory in bit 7) */
    *p++ = (uint8_t)(s->pop_half | (s->fw_play ? 2 : 0));   /* (version 5; 0.14.0: FW Play in bit 1) */
    *p++ = s->speech;                                   /* (version 6) */
    {                                                   /* (version 7, 0.8.0) the slots, then the locks */
        int i;
        for (i = 0; i < RFM_MOVE_FW_SLOTS; i++) {
            const rfm_move_slotset_t *ss = &s->slot[i];
            *p++ = ss->lay, *p++ = ss->scale, *p++ = ss->root, *p++ = ss->channel, *p++ = (uint8_t)ss->octave;
            memcpy(p, ss->cc, sizeof ss->cc), p += sizeof ss->cc;
        }
        for (i = 0; i < RFM_MOVE_FW_SLOTS; i++)
            *p++ = s->lock[i];
        for (i = 0; i < RFM_MOVE_SMP_N; i++)            /* (version 8, 0.15.0) the sampler's */
            *p++ = (uint8_t)s->smp[i];
    }
    return (uint32_t)(p - o);
}

int rfm_move_load(rfm_move_settings_t *s, const uint8_t *b, uint32_t n)
{
    rfm_move_settings_t d;
    const uint8_t *p;
    int i, oct;
    rfm_move_settings_default(s);
    if (!b || n < 6 || b[0] != 'R' || b[1] != 'M' || b[2] != 'V' ||
        !((b[3] == RFM_MOVE_SETTINGS_VERSION && (b[4] | b[5] << 8) == RFM_MOVE_SETTINGS_BYTES && n >= RFM_MOVE_SETTINGS_BYTES) ||
          (b[3] == 7 && (b[4] | b[5] << 8) == RFM_MOVE_SETTINGS_BYTES_V7 && n >= RFM_MOVE_SETTINGS_BYTES_V7) ||
          (b[3] == 6 && (b[4] | b[5] << 8) == RFM_MOVE_SETTINGS_BYTES_V6 && n >= RFM_MOVE_SETTINGS_BYTES_V6) ||
          (b[3] == 5 && (b[4] | b[5] << 8) == RFM_MOVE_SETTINGS_BYTES_V6 - 1 && n >= RFM_MOVE_SETTINGS_BYTES_V6 - 1) ||
          (b[3] == 4 && (b[4] | b[5] << 8) == RFM_MOVE_SETTINGS_BYTES_V6 - 2 && n >= RFM_MOVE_SETTINGS_BYTES_V6 - 2) ||
          (b[3] == 3 && (b[4] | b[5] << 8) == RFM_MOVE_SETTINGS_BYTES_V3 && n >= RFM_MOVE_SETTINGS_BYTES_V3) ||
          (b[3] == 2 && (b[4] | b[5] << 8) == RFM_MOVE_SETTINGS_BYTES_V3 - 2 && n >= RFM_MOVE_SETTINGS_BYTES_V3 - 2) ||
          (b[3] == 1 && (b[4] | b[5] << 8) == RFM_MOVE_SETTINGS_BYTES_V3 - 3 && n >= RFM_MOVE_SETTINGS_BYTES_V3 - 3)))
        return -1;
    d = *s;
    p = b + 6;
    s->mode = p[0] < 4 ? p[0] : d.mode;
    s->layout = p[1] < 4 ? p[1] : d.layout;
    s->channel = p[2] < 16 ? p[2] : d.channel;
    oct = (int8_t)p[3];
    s->octave = (int8_t)(oct >= RFM_MOVE_OCT_MIN && oct <= RFM_MOVE_OCT_MAX ? oct : d.octave);
    s->scale = p[4] < RFM_MOVE_NSCALES ? p[4] : d.scale;
    s->root = p[5] < 12 ? p[5] : d.root;
    s->bank = p[6] < RFM_MOVE_NBANKS ? p[6] : d.bank;
    p += 7;
    for (i = 0; i < RFM_MOVE_NBANKS * RFM_MOVE_NKNOBS; i++)
        if (p[i] < 128)
            s->cc[i / RFM_MOVE_NKNOBS][i % RFM_MOVE_NKNOBS] = p[i];
    p += RFM_MOVE_NBANKS * RFM_MOVE_NKNOBS;
    for (i = 0; i < RFM_MOVE_NPADS; i++)
        if (p[i] < 128)
            s->drum[i] = p[i];
    p += RFM_MOVE_NPADS;
    s->midi_view = b[3] >= 2 && p[0] < RFM_MOVE_MV_N ? p[0] : RFM_MOVE_MV_PADS;
    if (b[3] >= 3) {
        s->vel_mode = p[1] < RFM_MOVE_VEL_N ? p[1] : d.vel_mode;
        s->vel_level = p[2] >= 1 && p[2] <= 127 ? p[2] : d.vel_level;
    }
    if (b[3] >= 4) {                                    /* (the projects view as it was left) */
        const unsigned f = (unsigned)p[5] | (unsigned)p[6] << 8;
        s->fw_track = p[3] < 4 ? p[3] : d.fw_track;
        s->fw_scope = p[4] < 2 ? p[4] : d.fw_scope;
        s->fw_filter = (uint16_t)(f & 0xFFFC);          /* (steps 3..16) */
        s->fw_filter &= (uint16_t)-s->fw_filter;        /* (0.4.2: one category or tag; an older choice of several: the
                                                         * lowest) */
        s->fw_cat_scope = (uint8_t)(p[7] & 3);
        if (s->fw_cat_scope == 3)
            s->fw_cat_scope = 1;                        /* (0.4.2: one scope; both were: Update ready) */
        s->fx_page = (p[8] & 0x7F) < 4 ? p[8] & 0x7F : d.fx_page;
        s->fw_fx = p[8] >> 7;
    }
    if (b[3] >= 5)                                      /* (0.5.9; older: the whole screen, the default) */
        s->pop_half = p[9] & 1, s->fw_play = p[9] >> 1 & 1;   /* (0.14.0: FW Play, bit 1) */
    if (b[3] >= 6)                                      /* (0.6.12; older: not chosen, the host's default) */
        s->speech = p[10] <= 3 ? p[10] : 0;
    if (s->mode == RFM_MOVE_MODE_CONTROL)
        s->mode = RFM_MOVE_MODE_MIDI;                   /* (0.8.0: Control mode is gone; its CCs are every layout's) */
    if (b[3] >= 7) {                                    /* (0.8.0) the slots, the locks */
        const uint8_t *q = p + 11;
        for (i = 0; i < RFM_MOVE_FW_SLOTS; i++, q += 13) {
            rfm_move_slotset_t *ss = &s->slot[i];
            int k;
            ss->lay = q[0] < RFM_MOVE_LAY_N ? q[0] : RFM_MOVE_LAY_FM1;
            ss->scale = q[1] < RFM_MOVE_NSCALES ? q[1] : RFM_MOVE_SCALE_MAJOR;
            ss->root = q[2] < 12 ? q[2] : 0;
            ss->channel = q[3] < 16 ? q[3] : 0;
            oct = (int8_t)q[4];
            ss->octave = (int8_t)(oct >= RFM_MOVE_OCT_MIN && oct <= RFM_MOVE_OCT_MAX ? oct : 0);
            for (k = 0; k < RFM_MOVE_NKNOBS; k++)
                if (q[5 + k] < 128)
                    ss->cc[k] = q[5 + k];
        }
        for (i = 0; i < RFM_MOVE_FW_SLOTS; i++)
            s->lock[i] = q[i] < RFM_MOVE_SEQ_TRACKS ? q[i] : (uint8_t)i;
        if (b[3] >= 8)                                  /* (0.15.0) the sampler's (each checked; older: the defaults) */
            rfmv_smp_load(s->smp, q + RFM_MOVE_FW_SLOTS);
    } else                                              /* (older: every slot as the one mode was, its bank's CCs) */
        for (i = 0; i < RFM_MOVE_FW_SLOTS; i++) {
            rfm_move_slotset_t *ss = &s->slot[i];
            ss->lay = (uint8_t)(s->mode == RFM_MOVE_MODE_MIDI ? RFM_MOVE_LAY_SCALE + s->layout : RFM_MOVE_LAY_FM1);
            ss->scale = s->scale, ss->root = s->root, ss->channel = s->channel, ss->octave = s->octave;
            memcpy(ss->cc, s->cc[s->bank], sizeof ss->cc);
        }
    return 0;
}

/* ================================================================ actions === */

void emit(rfm_move_t *m, uint8_t kind, int index, int down, int vel, int value)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.frame = m->seq.frame;
    a.kind = kind, a.index = (uint8_t)index, a.down = (uint8_t)down, a.vel = (uint8_t)vel, a.value = (int16_t)value;
    if (m->fn)
        m->fn(m->ctx, &a);
}
void emit_midi(rfm_move_t *m, int status, int d1, int d2)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.frame = m->seq.frame;
    a.kind = RFM_MOVE_ACT_MIDI, a.n = 3;
    a.bytes[0] = (uint8_t)status, a.bytes[1] = (uint8_t)(d1 & 127), a.bytes[2] = (uint8_t)(d2 & 127);
    a.dest = m->emit_dest;                              /* (0.5.4: a latched note's off, its slot) */
    if (m->fn)
        m->fn(m->ctx, &a);
}
void rfmv_slot_push(rfm_move_t *m);
void changed(rfm_move_t *m)
{
    rfmv_slot_push(m);                                  /* (0.8.0: the active slot's mirror back into its record) */
    emit(m, RFM_MOVE_ACT_SETTINGS, 0, 0, 0, 0);
}

/* a key or button held by several Move controls: down with the first, up with the last */
static int key_slot(int k);
static int btn_slot(int b);
static int any_btn_held(const rfm_move_t *m);
static void label_pop(rfm_move_t *m, int slot, int at_once);
static void label_let(rfm_move_t *m, int slot);
static void key_hold(rfm_move_t *m, int k, int vel)
{
    if (m->key_count[k]++ == 0) {
        emit(m, RFM_MOVE_ACT_KEY, k, 1, clampi(vel, 1, 127), 0);
        if (any_btn_held(m))                            /* (0.5.6) an FM-1 button held: the key's label */
            label_pop(m, key_slot(k), 1);
    }
}
static void key_let(rfm_move_t *m, int k)
{
    if (!m->key_count[k] || --m->key_count[k])
        return;
    label_let(m, key_slot(k));
    if (m->key_press[k]) {
        m->key_press[k] = 0;
        emit(m, RFM_MOVE_ACT_PRESS, k, 0, 0, 0);
    }
    emit(m, RFM_MOVE_ACT_KEY, k, 0, 0, 0);
}
static void btn_hold(rfm_move_t *m, int b)
{
    const int other = any_btn_held(m);
    if (m->btn_count[b]++ == 0) {
        emit(m, RFM_MOVE_ACT_BTN, b, 1, 0, 0);
        label_pop(m, btn_slot(b), other || m->jog_touch);   /* (0.5.6) held: its label; another held, (0.5.8) or the
                                                         * jog touched: at once */
        rfmv_say_press(m, btn_slot(b));                 /* (0.6.1: speech: every press says its name at once) */
    }
}
static void btn_let(rfm_move_t *m, int b)
{
    if (m->btn_count[b] && --m->btn_count[b] == 0) {
        emit(m, RFM_MOVE_ACT_BTN, b, 0, 0, 0);
        label_let(m, btn_slot(b));
    }
}
void note_hold(rfm_move_t *m, int ch, int n, int vel)
{
    if (m->note_count[ch][n]++ == 0)
        emit_midi(m, 0x90 | ch, n, clampi(vel, 1, 127));
}
void note_let(rfm_move_t *m, int ch, int n)
{
    if (m->note_count[ch][n] && --m->note_count[ch][n] == 0)
        emit_midi(m, 0x80 | ch, n, 0);
}
void let_go(rfm_move_t *m, rfm_move_hold_t *h)
{
    if (h->kind == H_KEY)
        key_let(m, h->a);
    else if (h->kind == H_BTN)
        btn_let(m, h->a);
    else if (h->kind == H_NOTE)
        note_let(m, h->a, h->b);
    h->kind = H_NONE;
}

/* (0.5.3) latch (MIDI and Control modes): a pad's note keeps sounding after its release (it keeps its hold). A press
 * with no other pad down starts a new chord: the latched notes go first; a press on a latched note lets that note
 * go (and plays nothing). Off, a mode change, a project load and rfm_move_release_all let them all go.
 * (0.5.4) Each latched note keeps the firmware slot it was played on. While another slot is active it is parked: it
 * holds no note_count (the active slot may play the same note), and its note off goes straight to its slot (dest).
 * A new chord and a re-press act on the active slot's latched notes only */
#define LAT_PARK 0x80
static int act_slot(const rfm_move_t *m) { return m->fw.active < RFM_MOVE_FW_SLOTS ? m->fw.active : 0; }
static void latch_sync(rfm_move_t *m)                   /* park / unpark against the active slot */
{
    const int a = act_slot(m);
    int i;
    for (i = 0; i < m->nlat; i++) {
        const int s = m->lat_slot[i] & 3, park = (m->lat_slot[i] & LAT_PARK) != 0, ch = m->lat_ch[i], n = m->lat_n[i];
        if (s != a && !park) {
            if (m->note_count[ch][n])
                m->note_count[ch][n]--;                 /* (it sounds on its slot; nothing sent) */
            m->lat_slot[i] |= LAT_PARK;
        } else if (s == a && park) {
            m->note_count[ch][n]++;                     /* (back on its slot: a hold again) */
            m->lat_slot[i] &= (uint8_t)~LAT_PARK;
        }
    }
}
static int latch_find(rfm_move_t *m, int ch, int n)     /* on the active slot */
{
    int i;
    latch_sync(m);
    for (i = 0; i < m->nlat; i++)
        if (m->lat_ch[i] == ch && m->lat_n[i] == n && !(m->lat_slot[i] & LAT_PARK))
            return i;
    return -1;
}
static void latch_drop(rfm_move_t *m, int i)
{
    m->emit_dest = (uint8_t)((m->lat_slot[i] & 3) + 1);
    if (m->lat_slot[i] & LAT_PARK)
        emit_midi(m, 0x80 | m->lat_ch[i], m->lat_n[i], 0);
    else
        note_let(m, m->lat_ch[i], m->lat_n[i]);
    m->emit_dest = 0;
    m->nlat--;
    m->lat_ch[i] = m->lat_ch[m->nlat], m->lat_n[i] = m->lat_n[m->nlat], m->lat_slot[i] = m->lat_slot[m->nlat];
}
static void latch_drop_slot(rfm_move_t *m)              /* the active slot's (a new chord) */
{
    int i;
    latch_sync(m);
    for (i = m->nlat - 1; i >= 0; i--)
        if (!(m->lat_slot[i] & LAT_PARK))
            latch_drop(m, i);
}
void rfmv_latch_drop_all(rfm_move_t *m)
{
    latch_sync(m);
    while (m->nlat)
        latch_drop(m, m->nlat - 1);
}
static void latch_set(rfm_move_t *m, int on)
{
    m->latch = on != 0;
    if (!m->latch)
        rfmv_latch_drop_all(m);
}
void rfm_move_set_latch(rfm_move_t *m, int on)
{
    latch_set(m, on);
    rfmv_say_check(m);
}
int rfm_move_latch(const rfm_move_t *m) { return m->latch; }

/* every pad, step and button let go (the track buttons stay as they are) */
static void release_holds(rfm_move_t *m)
{
    int i;
    for (i = 0; i < RFM_MOVE_NPADS; i++) {
        let_go(m, &m->pad[i]);
        m->pad_down[i] = 0, m->pad_press[i] = 0;
    }
    for (i = 0; i < RFM_MOVE_NSTEPS; i++)
        let_go(m, &m->step[i]);
    for (i = 0; i < 128; i++)
        let_go(m, &m->btn[i]);
    if (m->fwp_glo)                                     /* (0.14.0: the FM-1 page's Shift + knob's GLO) */
        m->fwp_glo = 0, btn_let(m, RFM_MOVE_B_GLO);
    m->edit_pad = -1;
    m->pj_press = m->pj_del = m->pj_ack = -1;          /* (0.14.0: the projects page) */
    rfmv_seq_ui_release(m);
}

void rfm_move_release_all(rfm_move_t *m)
{
    release_holds(m);
    rfmv_latch_drop_all(m);                             /* (0.5.3) */
    rfmv_seq_stop(m);                                   /* the link is gone: nothing may keep sounding */
    m->track_held = 0;
    m->back_down = m->back_used = m->back_trk = 0, m->back_cut = 0;
    m->touched = -1;
    m->pop = POP_NONE;
    rfmv_tk_release(m);                                 /* (0.7.0: the firmware's held controls too) */
}

static void set_mode(rfm_move_t *m, int mode)
{
    if (mode == RFM_MOVE_MODE_CONTROL || mode == RFM_MOVE_MODE_MIDI)
        mode = RFM_MOVE_MODE_SEQ;                       /* (0.8.0: no Control mode; 0.9.0: a note layout is the
                                                         * sequencer) */
    if (mode < 0 || mode > 3 || mode == m->s.mode)
        return;
    rfmv_slot_push(m);
    release_holds(m);
    if (mode == RFM_MOVE_MODE_SEQ || m->s.mode == RFM_MOVE_MODE_SEQ)
        rfmv_latch_drop_all(m);                         /* (0.5.3: a mode change lets the latched notes go; 0.8.0: a
                                                         * slot's layouts among themselves keep them, as a slot switch) */
    m->mmenu = 0;
    if (m->s.mode == RFM_MOVE_MODE_SEQ)
        rfmv_seq_leave(m);                              /* (the tracks' own layout and CC numbers out, MIDI mode's back) */
    m->s.mode = (uint8_t)mode;
    if (mode == RFM_MOVE_MODE_SEQ)
        rfmv_seq_enter(m);
    else
        rfmv_slot_pull(m);                              /* (0.8.0: the slot's own layout, scale, channel, octave) */
    m->pop = POP_NONE;
    changed(m);
}

/* ---- (0.8.0) the slots' own settings (proposal 2): s.slot[] is each slot's record; s.layout .. s.octave are the
 * active slot's mirror while a layout is played (outside the Sequencer, whose track has them then) */
static int in_layouts(const rfm_move_t *m) { return m->s.mode != RFM_MOVE_MODE_SEQ && !m->seq.in_seq; }
void rfmv_slot_push(rfm_move_t *m)                      /* the mirror into the active slot's record */
{
    rfm_move_slotset_t *ss = &m->s.slot[act_slot(m)];
    if (m->s.mode == RFM_MOVE_MODE_SEQ && m->seq.in_seq && ss->lay >= RFM_MOVE_LAY_SCALE &&
        m->seq.sel_track == m->s.lock[act_slot(m)])
        ss->lay = (uint8_t)(RFM_MOVE_LAY_SCALE + (m->s.layout & 3));   /* (0.9.0: its track's layout, the slot's) */
    if (!in_layouts(m))
        return;
    ss->scale = m->s.scale, ss->root = m->s.root, ss->channel = m->s.channel, ss->octave = m->s.octave;
    if (ss->lay >= RFM_MOVE_LAY_SCALE)
        ss->lay = (uint8_t)(RFM_MOVE_LAY_SCALE + (m->s.layout & 3));   /* (Shift + step 8's Layout menu) */
}
void rfmv_slot_pull(rfm_move_t *m)                      /* the active slot's record into the mirror */
{
    const rfm_move_slotset_t *ss = &m->s.slot[act_slot(m)];
    if (!in_layouts(m))
        return;
    m->s.scale = ss->scale, m->s.root = ss->root, m->s.channel = ss->channel, m->s.octave = ss->octave;
    if (ss->lay >= RFM_MOVE_LAY_SCALE)
        m->s.layout = (uint8_t)(ss->lay - RFM_MOVE_LAY_SCALE);
}
int rfmv_slot_lay(const rfm_move_t *m, int slot)        /* the layout a slot plays (TK without a takeover: FM-1) */
{
    const int l = m->s.slot[slot % RFM_MOVE_FW_SLOTS].lay;
    return l == RFM_MOVE_LAY_TK && !m->tk[slot % RFM_MOVE_FW_SLOTS].claims ? RFM_MOVE_LAY_FM1 : l;
}
/* the active slot's layout played (outside the Sequencer): FM-1 mode or MIDI mode, the takeover by its layout */
/* (0.9.0, proposal 3) a note layout IS the sequencer: the slot's selected track (s.lock[t]) is the one edited and
 * played, routed to the slot at once; its layout is the slot's; the knobs open on the TRIG page */
static void slot_seq_enter(rfm_move_t *m, int t, int fresh)
{
    rfm_move_seq_t *q = &m->seq;
    const int tr = m->s.lock[t] % RFM_MOVE_SEQ_TRACKS, lay = m->s.slot[t].lay;
    rfmv_seq_select(m, tr);
    rfmv_route_on(m, t, tr);
    if (lay >= RFM_MOVE_LAY_SCALE && m->s.layout != lay - RFM_MOVE_LAY_SCALE) {
        m->s.layout = (uint8_t)(lay - RFM_MOVE_LAY_SCALE);   /* (the track takes the slot's layout) */
        rfmv_seq_sync(m);
    }
    if (fresh) {
        q->view = 0;                                    /* (the owner's: the track view) */
        rfmv_seq_kpage_restore(m);                      /* (0.14.0, the owner's: the track's knob page as it was left;
                                                         * TRIG for a track never shown) */
    }
}
/* the active slot's layout played: the FM-1 layout or its takeover (FM-1 mode), or a note layout (the sequencer) */
static void slot_mode_fresh(rfm_move_t *m, int fresh)
{
    const int t = act_slot(m), seq = m->s.slot[t].lay >= RFM_MOVE_LAY_SCALE;
    if (seq && m->s.mode != RFM_MOVE_MODE_SEQ)
        fresh = 1;
    set_mode(m, seq ? RFM_MOVE_MODE_SEQ : RFM_MOVE_MODE_FM1);
    if (seq)
        slot_seq_enter(m, t, fresh);
    else
        rfmv_slot_pull(m);
}
static void slot_mode(rfm_move_t *m) { slot_mode_fresh(m, 0); }
/* fw.c: the active slot is about to change (what is held goes to the slot it was played on) / has changed */
void rfmv_slot_leaving(rfm_move_t *m)
{
    release_holds(m);
    m->jog_enc_s[act_slot(m)] = (uint8_t)(m->jog_enc % 3);   /* (0.14.0: the jog target is the slot's) */
}
void rfmv_slot_changed(rfm_move_t *m)
{
    m->jog_enc = (uint8_t)(m->jog_enc_s[act_slot(m)] % 3);
    m->fwj = 0;                                         /* (0.14.0: another slot: the page's jog its pages again) */
    if (m->s.mode != RFM_MOVE_MODE_SEQ)
        rfmv_slot_pull(m);
    slot_mode_fresh(m, 1);                              /* (0.9.0: another slot: its layout, its track) */
}
static int set_slot_lay(rfm_move_t *m, int t, int lay)
{
    rfm_move_slotset_t *ss;
    if (t < 0 || t >= RFM_MOVE_FW_SLOTS || lay < 0 || lay >= RFM_MOVE_LAY_N || (lay == RFM_MOVE_LAY_TK && !m->tk[t].claims))
        return -1;
    ss = &m->s.slot[t];
    if (ss->lay == lay)
        return 0;
    ss->lay = (uint8_t)lay;
    if (lay >= RFM_MOVE_LAY_SCALE)                      /* (0.9.0: the selected track takes the layout) */
        m->seq.tr[m->s.lock[t] % RFM_MOVE_SEQ_TRACKS].layout = (uint8_t)(lay - RFM_MOVE_LAY_SCALE);
    if (t == act_slot(m)) {
        if (lay >= RFM_MOVE_LAY_SCALE)
            m->s.layout = (uint8_t)(lay - RFM_MOVE_LAY_SCALE);
        slot_mode(m);
    }
    changed(m);
    return 0;
}
int rfmv_set_slot_lay(rfm_move_t *m, int t, int lay) { return set_slot_lay(m, t, lay); }
int rfm_move_set_slot_layout(rfm_move_t *m, int slot, int lay)
{
    const int r = set_slot_lay(m, slot, lay);
    rfmv_tk_sync(m);
    rfmv_say_check(m);
    return r;
}
int rfm_move_slot_layout(const rfm_move_t *m, int slot)
{
    return slot < 0 || slot >= RFM_MOVE_FW_SLOTS ? -1 : m->s.slot[slot].lay;
}
int rfm_move_seq_lock(const rfm_move_t *m, int slot)
{
    return slot < 0 || slot >= RFM_MOVE_FW_SLOTS ? -1 : m->s.lock[slot];
}

void rfm_move_set_mode(rfm_move_t *m, int mode)
{
    const int t = act_slot(m);
    rfm_move_slotset_t *ss = &m->s.slot[t];
    if (mode == RFM_MOVE_MODE_FM1 || mode == RFM_MOVE_MODE_TAKEOVER || mode == RFM_MOVE_MODE_MIDI ||
        mode == RFM_MOVE_MODE_CONTROL || mode == RFM_MOVE_MODE_SEQ) {   /* (0.9.0: SEQ, MIDI, CONTROL: the slot's
                                                         * note layout, the sequencer) */
        int lay = mode == RFM_MOVE_MODE_FM1 ? RFM_MOVE_LAY_FM1 : mode == RFM_MOVE_MODE_TAKEOVER ? RFM_MOVE_LAY_TK
                : ss->lay >= RFM_MOVE_LAY_SCALE ? ss->lay : RFM_MOVE_LAY_SCALE;   /* (a slot never in a note
                                                         * layout: Scale, not another slot's) */
        if (lay == RFM_MOVE_LAY_TK && !m->tk[t].claims) {
            rfmv_say_check(m);
            return;
        }
        if (ss->lay == lay)
            slot_mode(m);                               /* (the layout it has: played again) */
        else
            set_slot_lay(m, t, lay);
    }
    rfmv_tk_sync(m);
    rfmv_say_check(m);
}

void rfm_move_set_scale(rfm_move_t *m, int scale, int root)
{
    if (scale < 0 || scale >= RFM_MOVE_NSCALES || root < 0 || root > 11)
        return;
    m->s.scale = (uint8_t)scale, m->s.root = (uint8_t)root;
    changed(m);
}

void rfm_move_set_master(rfm_move_t *m, int value) { m->master = (uint16_t)clampi(value, 0, 1023); }
int rfm_move_set_battery(rfm_move_t *m, int battery, int power_bits)
{
    const int b = battery < 0 ? -1 : clampi(battery, 0, 100);
    if (b == m->battery && (uint8_t)power_bits == m->power)
        return 0;
    m->battery = (int16_t)b, m->power = (uint8_t)power_bits;
    return 1;
}

void rfm_move_set_label(rfm_move_t *m, int enc, const char *text)
{
    if (enc < 0 || enc > RFM_MOVE_NENC)
        return;
    snprintf(m->label[enc], sizeof m->label[enc], "%s", text ? text : "");
    m->lbl_has[rfmv_enc_slot(enc)] = 0;                 /* (this call's "" is the default, not "no job") */
}

/* ---- (0.5.6) labels: DYNAMIC-LABELS slots (FM1P_* order) to and from rfm1cs's encoders, buttons and keys */
static const uint8_t ENC_SLOT[RFM_MOVE_NENC + 1] = { 0, 1, 3, 4, 5, 6, 2, 7 };   /* RFM_MOVE_E_* -> FM1P_E_* */
int rfmv_enc_slot(int enc) { return enc >= 0 && enc <= RFM_MOVE_NENC ? ENC_SLOT[enc] : 0; }
static int btn_slot(int b) { return RFM_MOVE_LBL_BTN0 + (b + RFM_MOVE_NBTN - 2) % RFM_MOVE_NBTN; }   /* (FX first) */
static int key_slot(int k)                              /* key 0 = F3: the F octave's whites and blacks */
{
    static const int8_t W[12] = { 0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6 }, B[12] = { -1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1 };
    const int o = k / 12, s = k % 12;
    return W[s] >= 0 ? RFM_MOVE_LBL_WHITE0 + o * 7 + W[s] : RFM_MOVE_LBL_BLACK0 + o * 5 + B[s];
}
static int enc_nojob(const rfm_move_t *m, int enc)
{
    const int sl = rfmv_enc_slot(enc);
    return m->lbl_has[sl] && !m->lbl[sl][0];
}
void rfm_move_set_labels(rfm_move_t *m, const char *const lbl[RFM_MOVE_LBL_N])
{
    int i, e;
    for (i = 0; i < RFM_MOVE_LBL_N; i++) {
        const char *t = lbl ? lbl[i] : NULL;
        snprintf(m->lbl[i], sizeof m->lbl[i], "%s", t ? t : "");
        m->lbl_has[i] = t != NULL;
    }
    for (e = 0; e <= RFM_MOVE_NENC; e++)
        snprintf(m->label[e], sizeof m->label[e], "%s", m->lbl[ENC_SLOT[e]]);
}
void rfm_move_set_button_names(rfm_move_t *m, const char *const names[RFM_MOVE_NBTN])
{
    int i;
    for (i = 0; i < RFM_MOVE_NBTN; i++)
        snprintf(m->btn_nm[i], sizeof m->btn_nm[i], "%s", names && names[i] ? names[i] : "");
}
int rfmv_label(const rfm_move_t *m, int slot, char *out, int cap)
{
    const char *t = "";
    if (slot < 0 || slot >= RFM_MOVE_LBL_N)
        return 0;
    if (m->lbl_has[slot])
        t = m->lbl[slot];
    else if (slot >= RFM_MOVE_LBL_BTN0 && slot < RFM_MOVE_LBL_WHITE0)
        t = m->btn_nm[slot - RFM_MOVE_LBL_BTN0];
    snprintf(out, (size_t)cap, "%s", t);
    return out[0] != 0;
}
int rfmv_label_held(const rfm_move_t *m, int slot)
{
    if (slot >= RFM_MOVE_LBL_BTN0 && slot < RFM_MOVE_LBL_WHITE0)
        return m->btn_count[(slot - RFM_MOVE_LBL_BTN0 + 2) % RFM_MOVE_NBTN] > 0;
    if (slot >= RFM_MOVE_LBL_WHITE0 && slot < RFM_MOVE_LBL_N) {
        int k;
        for (k = 0; k < RFM_MOVE_NKEYS; k++)
            if (key_slot(k) == slot)
                return m->key_count[k] > 0;
    }
    return 0;
}
/* ---- (0.5.7) readouts */
int rfmv_pop_slot(const rfm_move_t *m)
{
    switch (m->pop) {
    case POP_ENC: return KNOB_ENC[m->pop_i & 7] >= 0 ? rfmv_enc_slot(KNOB_ENC[m->pop_i & 7]) : -1;
    case POP_SELECT: return RFM_MOVE_LBL_KNOB0 + 0;
    case POP_MASTER: return RFM_MOVE_LBL_KNOB0 + 7;
    default: return -1;
    }
}
static void rd_copy(char *o, const char *t)             /* printable ASCII, 16 kept */
{
    int i, k = 0;
    for (i = 0; t && t[i] && k < RFM_MOVE_LABEL_MAX; i++)
        if (t[i] >= 32 && t[i] < 127)
            o[k++] = t[i];
    o[k] = 0;
}
void rfm_move_readouts_clear(rfm_move_t *m)
{
    memset(m->rd, 0, sizeof m->rd);
    memset(m->rd_lbl, 0, sizeof m->rd_lbl);
    memset(&m->rd_pop, 0, sizeof m->rd_pop);
    if (m->pop == POP_READ)
        m->pop = POP_NONE;
}
static void readout(rfm_move_t *m, int control, int kind, const char *title, const char *value, int frac, int flags,
                    int ms)
{
    const int expl = control >= 0;                      /* (0.6.9: the host named the control) */
    rfm_move_readout_t r;
    const int up = m->pop && rfmv_pop_up(m, m->pop), knob_up = up ? rfmv_pop_slot(m) : -1;
    memset(&r, 0, sizeof r);
    rd_copy(r.title, title), rd_copy(r.value, value);
    r.frac = (int16_t)(frac < 0 ? -1 : clampi(frac, 0, 1000));
    r.kind = (uint8_t)clampi(kind, RFM_MOVE_RD_PARAM, RFM_MOVE_RD_MESSAGE), r.flags = (uint8_t)flags, r.on = 1;
    if (!r.title[0] && !r.value[0])
        return;
    if (control < 0 && knob_up >= 0 &&                  /* (the gesture in progress: a knob touched or just moved) */
        (m->touched == m->pop_i || rfmv_now_ms(m) - m->pop_ms < RFM_MOVE_RD_PAIR_MS))
        control = knob_up;
    if (control >= RFM_MOVE_LBL_KNOB0 && control < RFM_MOVE_LBL_BTN0) {
        m->rd[control] = r;                             /* (kept: the knob's touch shows it) */
        m->rd_at[control & 7] = rfmv_now_ms(m);         /* (0.14.0: this turn's, or an older one) */
        rfmv_say_readout(m, control, expl);             /* (0.6.7: its label then, for speech) */
        if (knob_up == control)
            return;                                     /* (its popup is up: drawn in place) */
    }
    if ((!rfmv_half(m) && !(rfmv_fwp_knobs(m) && control >= RFM_MOVE_LBL_KNOB0 && control < RFM_MOVE_LBL_BTN0)) ||
        m->seq.projects)
        return;                                         /* (0.12.x: the FM-1 page's level 1: a knob's own, over the
                                                         * track view) */
    m->rd_pop = r;
    rfmv_pop(m, POP_READ, control < 0 ? 255 : control);
    m->rd_ms = (uint16_t)(ms > 0 ? clampi(ms, 1, 60000) : RFM_MOVE_POPUP_MS);
}
void rfm_move_readout(rfm_move_t *m, int control, int kind, const char *title, const char *value, int frac, int flags,
                      int ms)
{
    readout(m, control, kind, title, value, frac, flags, ms);
    rfmv_say_check(m);
}

static int any_btn_held(const rfm_move_t *m)
{
    int b;
    for (b = 0; b < RFM_MOVE_NBTN; b++)
        if (m->btn_count[b])
            return 1;
    return 0;
}
/* FM-1 mode: a button's or key's label up: after RFM_MOVE_LABEL_HOLD_MS (delay), or at once (a button held) */
static void label_pop(rfm_move_t *m, int slot, int at_once)
{
    char t[RFM_MOVE_LABEL_MAX + 1];
    if ((m->s.mode != RFM_MOVE_MODE_FM1 && !rfmv_fwp_full(m)) || m->seq.projects)
        return;
    if (!rfmv_label(m, slot, t, sizeof t) && m->pop == POP_LABEL && m->pop_i != slot && rfmv_pop_up(m, POP_LABEL))
        return;                                         /* (no words now: the label up stays; 0.5.8: else it comes up
                                                         * anyway, the firmware may name it once pressed) */
    rfmv_pop(m, POP_LABEL, slot);
    m->pop_delay = at_once ? 0 : RFM_MOVE_LABEL_HOLD_MS;
    if (m->pop_t0 == rfmv_now_ms(m))
        m->pop_t0 += m->pop_delay;                      /* (a long label scrolls from when it shows) */
}
static void label_let(rfm_move_t *m, int slot)          /* its control let go: a tap (not yet shown) shows nothing */
{
    if (m->pop != POP_LABEL || m->pop_i != slot)
        return;
    if (rfmv_now_ms(m) - m->pop_ms < m->pop_delay || !m->pop_shown)
        m->pop = POP_NONE;                              /* (0.5.8: never words only after the release) */
    else
        rfmv_pop_let(m, POP_LABEL, slot), m->pop_delay = 0;
}

void rfm_move_set_keymap(rfm_move_t *m, const int8_t keymap[6][11])
{
    int r, c;
    memset(m->led_of, -1, sizeof m->led_of);
    for (r = 1; r <= 4; r++)
        for (c = 0; c < 11; c++) {
            const int id = (keymap ? keymap : FM1_KEYMAP)[r][c];
            if (id >= 0 && id < 41)
                m->led_of[id] = (int8_t)(c * 4 + (r - 1));
        }
}

const char *rfm_move_version(void) { return RFM1CS_VERSION; }

void rfm_move_init(rfm_move_t *m, const rfm_move_settings_t *settings, rfm_move_act_fn fn, void *ctx)
{
    int b, k;
    memset(m, 0, sizeof *m);
    if (settings)
        m->s = *settings;
    else
        rfm_move_settings_default(&m->s);
    m->fn = fn, m->ctx = ctx;
    for (b = 0; b < RFM_MOVE_NBANKS; b++)
        for (k = 0; k < RFM_MOVE_NKNOBS; k++)
            m->cc_val[b][k] = 64;
    m->master = 768;
    m->battery = -1;
    m->edit_pad = -1;
    m->pj_press = m->pj_del = m->pj_ack = -1;          /* (0.14.0: the projects page) */
    m->touched = -1;
    rfm_move_set_keymap(m, NULL);
    for (b = 0; b < RFM_MOVE_FW_SLOTS; b++)
        for (k = 0; k < RFM_MOVE_NKNOBS; k++)
            m->slot_ccv[b][k] = 64;
    m->fx_route = RFM_MOVE_FX_PAGE_ROUTE;
    rfmv_fw_init(m);
    rfmv_seq_init(m);
    rfmv_ep_init(m);                                    /* (0.14.0: no firmware's ep1 yet) */
    if (m->s.slot[0].lay >= RFM_MOVE_LAY_SCALE) {       /* (0.8.0, 0.9.0) the active slot's layout (slot 0 until the
                                                         * library names another): a note layout is the sequencer */
        m->s.mode = RFM_MOVE_MODE_SEQ;
        rfmv_seq_enter(m);
        slot_seq_enter(m, 0, 1);
    } else {
        m->s.mode = RFM_MOVE_MODE_FM1;
        rfmv_slot_pull(m);
    }
}

/* ================================================================ input === */
int rfmv_scale_of_pad(int p) { return (2 - p / 8) * 8 + p % 8; }   /* pads 0..23: rows 2..4 from the top */
/* the top row's first four pads pick the layout, rows 2..4 the 24 scales */
int rfmv_pick_layout(rfm_move_t *m, int p)
{
    if (p >= 24 && p < 28) {
        rfmv_menu(m, MENU_LAYOUT, p - 24);
        if (m->s.layout != p - 24) {
            m->s.layout = (uint8_t)(p - 24);
            changed(m);
        }
        return 1;
    }
    if (p < 24) {                                       /* rows 2, 3, 4 (from the top): scales 1..8, 9..16, 17..24 */
        const int sc2 = rfmv_scale_of_pad(p);
        rfmv_menu(m, MENU_SCALE, sc2);
        if (m->s.scale != sc2) {
            m->s.scale = (uint8_t)sc2;
            changed(m);
        }
        return 1;
    }
    return 0;
}

static int midi_mode(const rfm_move_t *m) { return m->s.mode != RFM_MOVE_MODE_FM1; }
int rfmv_track_held(const rfm_move_t *m, int t) { return (m->track_held >> t) & 1; }
/* a held Track button's screen (its menu) shows once it has been held RFM_MOVE_TAP_MS, or used, not on a tap */
int rfmv_hold_shown(const rfm_move_t *m, int t)
{
    return rfmv_track_held(m, t) && (m->menu_what != MENU_NONE || rfmv_now_ms(m) - m->tbtn_clk[t] >= RFM_MOVE_TAP_MS ||
                                     (t == 3 && (m->tbtn_used >> 3 & 1)));   /* (0.12.0, a8: Track 4: anything pressed
                                                         * while it is down is the hold: its layer at once) */
}
/* a list-type menu knob (scale, root, layout, rate, a pattern ..): one item every RFM_MOVE_LIST_TICKS of the Move's
 * ticks, the rest kept; a turn back or the knob let go forgets it. k: the knob (0..7; -1 the jog: 1:1) */
int rfmv_list_ticks(rfm_move_t *m, int k, int d)
{
    int a, n;
    if (k < 0 || k > RFM_MOVE_NKNOBS)
        return d;
    a = m->list_acc[k];
    if ((a > 0 && d < 0) || (a < 0 && d > 0))
        a = 0;
    a += d;
    n = a / RFM_MOVE_LIST_TICKS;
    m->list_acc[k] = (int8_t)(a - n * RFM_MOVE_LIST_TICKS);
    return n;
}
void rfmv_release_holds(rfm_move_t *m) { release_holds(m); }
int rfmv_half(const rfm_move_t *m)                     /* popups on the left half: the FM-1's screen on the right */
{
    return m->s.mode == RFM_MOVE_MODE_FM1 || (m->s.mode == RFM_MOVE_MODE_MIDI && m->s.midi_view == RFM_MOVE_MV_NOTES) ||
           (rfmv_fwp_full(m) && !m->seq.smenu);        /* (0.12.x: the FM-1 page's level 2: the FM-1's screen) */
}
#define track_held(m, t) rfmv_track_held(m, t)

/* ---- (0.8.0) the Track buttons, proposal 2 (README "The Track buttons"): Track 1..3 the firmware slots, Track 4 the
 * sequencer side. Which held Track's menu is up: */
int rfmv_seq_trk_held(const rfm_move_t *m)              /* a firmware Track held: the steps its lock menu (slot + 1;
                                                         * 0.9.0: in every layout) */
{
    int t;
    for (t = 0; t < rfmv_fw_nslots(m); t++)             /* (0.9.0: over the projects view too) */
        if (track_held(m, t))
            return t + 1;
    return 0;
}
int rfmv_lay_menu(const rfm_move_t *m)                  /* a firmware Track held: the pads its layout menu (0.9.0: every
                                                         * layout, the note layouts (the sequencer) too) */
{
    int t;
    for (t = 0; t < rfmv_fw_nslots(m); t++)             /* (0.9.0: over the projects view too) */
        if (track_held(m, t))
            return t + 1;
    return 0;
}
int rfmv_t4_menu(const rfm_move_t *m) { return track_held(m, 3); }   /* Track 4 held: its menu (the view too) */
/* ---- (0.12.x, the owner's 2026-11-22: "we should also add in a page to control the firmware's knobs like you get on
 * the FM-1 Layout .. Then when clicking the jogwheel it should switch the takeover between just knobs and then full
 * shortcuts: Buttons, steps, ect, but pads stay in the MIDI layout, and Jogwheel is not taken over") a note layout's
 * FM-1 knob page: level 1 the knobs (the FM-1 layout's: SELECT, PRESETS, ALGO, -, K1..K4), level 2 the FM-1 layout's
 * buttons (all it maps but Up: Up / Down stay the pads' octave) and the steps (its keys) too. The same paths as the
 * FM-1 layout (enc_turn, key_hold, btn_hold), so the firmware gets exactly what that layout sends */
int rfmv_fwp_page(const rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    return m->s.mode == RFM_MOVE_MODE_SEQ && q->fwp && !q->trig && !q->view && !q->projects;
}
int rfmv_fwp_live(const rfm_move_t *m)                  /* (an empty slot: the page is there, its knobs dark and dead) */
{
    return rfmv_fwp_page(m) && (!m->fw.host || m->fw.slot_id[act_slot(m)][0]);
}
int rfmv_fwp_full(const rfm_move_t *m) { return rfmv_fwp_live(m) && m->seq.fwp_full; }
int rfm_move_fwp_level(const rfm_move_t *m) { return rfmv_fwp_full(m) ? 2 : rfmv_fwp_live(m) ? 1 : 0; }
int rfmv_fwp_knobs(const rfm_move_t *m) { return rfmv_fwp_live(m) && !m->seq.smenu && !m->seq.step_held; }
static int fwp_btn(int cc) { return cc != MR_CC_UP && rfm_move_button_of(cc) >= 0; }   /* (level 2's buttons) */
/* what level 2 took is let go once it is not on (the jog's click, another page, another view or slot) */
static void fwp_sync(rfm_move_t *m)
{
    int i;
    if (m->s.mode != RFM_MOVE_MODE_SEQ || rfmv_fwp_full(m))
        return;
    for (i = 0; i < RFM_MOVE_NSTEPS; i++)
        if (m->step[i].kind == H_KEY)
            let_go(m, &m->step[i]);
    for (i = 0; i < 128; i++)
        if (m->btn[i].kind == H_BTN)
            let_go(m, &m->btn[i]);
}
const char *rfmv_jog_name(int i) { static const char *const N[3] = { "SELECT", "PRESET", "ALGO" }; return N[i % 3]; }

/* outside Sequencer mode: a sequencer menu (opened from Track 4's menu or Shift + a step) is open: it takes what it
 * needs */
static int fm1_seq_layer(const rfm_move_t *m)
{
    return m->s.mode != RFM_MOVE_MODE_SEQ && (m->seq.smenu || m->seq.projects);
}
/* the Move's Shift layer on the sequencer: Track 4 held + a step or button (any mode), Shift + a step outside the FM-1
 * layout (in it Shift is the FM-1's GLO) */
static void fm1_shift(rfm_move_t *m, int is_step, int i, int down, uint32_t now)
{
    rfm_move_seq_t *q = &m->seq;
    const uint8_t had = q->mods & SEQ_M_SHIFT, sm = q->smenu;
    q->mods |= SEQ_M_SHIFT;
    if (is_step)
        rfmv_seq_step(m, i, down, now);
    else
        rfmv_seq_button(m, i, down, now);
    if (!had)
        q->mods &= (uint8_t)~SEQ_M_SHIFT;
    if (q->smenu && q->smenu != sm)
        q->t4_opened = 1;                               /* (0.12.0, a8: a menu it opened owns the knobs, lit, while
                                                         * Track 4 is still down) */
    rfmv_menu(m, MENU_SHIFT, 0);                        /* (the Track button was used: its release is no tap) */
}

/* (0.9.0) the key (root, scale; -1 leaves one): in the sequencer the live one (its track's or the project's), else
 * the project's */
static void key_set(rfm_move_t *m, int root, int scale)
{
    rfm_move_seq_t *q = &m->seq;
    if (m->s.mode == RFM_MOVE_MODE_SEQ && q->in_seq) {
        if (root >= 0)
            m->s.root = (uint8_t)root;
        if (scale >= 0)
            m->s.scale = (uint8_t)scale;
        changed(m);
        rfmv_seq_sync(m);
    } else {
        if (root >= 0)
            q->root = (uint8_t)root;
        if (scale >= 0)
            q->scale = (uint8_t)scale;
    }
    rfmv_seq_dirty(m);
}
static int key_root(const rfm_move_t *m) { return m->s.mode == RFM_MOVE_MODE_SEQ ? m->s.root : m->seq.root; }
/* a firmware Track held (outside the Sequencer): its slot's layout menu. Top row: 1 FM-1, 2 the takeover (dark with
 * none), 3..6 Scale, Chroma, Drum, Octave, 8 latch; rows 2..4 the 24 scales */
static void lay_pad(rfm_move_t *m, int t, int p)
{
    rfm_move_slotset_t *ss = &m->s.slot[t];
    if (p == 31) {                                      /* latch (shared, as before), named */
        latch_set(m, !m->latch);
        rfmv_menu(m, MENU_LAYOUT, rfmv_slot_lay(m, t));
        rfmv_pop(m, POP_MODE, m->latch ? 31 : 30);
        return;
    }
    if (p >= 24 && p < 24 + RFM_MOVE_LAY_N) {
        const int lay = p - 24;
        if (lay == RFM_MOVE_LAY_TK && !m->tk[t].claims) {
            rfmv_menu(m, MENU_LAYOUT, rfmv_slot_lay(m, t));   /* (dark: nothing; the button was used) */
            return;
        }
        set_slot_lay(m, t, lay);
        rfmv_menu(m, MENU_LAYOUT, lay);
        return;
    }
    if (p < 24) {                                       /* rows 2, 3, 4 (from the top): scales 1..8, 9..16, 17..24 (0.9.0:
                                                         * the key the sequencer's tracks use: the project's, or the
                                                         * selected track's own) */
        const int sc2 = rfmv_scale_of_pad(p);
        rfmv_menu(m, MENU_SCALE, sc2);
        key_set(m, -1, sc2);
        (void)ss;
        return;
    }
    if (p == 30 && rfmv_st_pad7(m, t))                  /* (0.13.0, the owner's) pad 7: the firmware's States */
        rfmv_st_open(m, t, 0);
    rfmv_menu(m, MENU_LAYOUT, rfmv_slot_lay(m, t));    /* (pad 7 with no firmware or no host's PROJFW: nothing) */
}

static void on_pad(rfm_move_t *m, int p, int vel, uint32_t now)
{
    rfm_move_hold_t *h = &m->pad[p];
    int t;
    if (!vel) {                                         /* released */
        if (m->pad_down[p] && m->s.mode == RFM_MOVE_MODE_SEQ)
            rfmv_seq_pad(m, p, 0, now);
        if (m->latch && h->kind == H_NOTE && midi_mode(m) &&   /* (0.9.0: the note layouts, the sequencer: live notes
                                                         * only; a recording took the held length above) */
            m->nlat < RFM_MOVE_LATCH_MAX && latch_find(m, h->a, h->b) < 0) {
            m->lat_ch[m->nlat] = h->a, m->lat_n[m->nlat] = h->b;   /* (0.5.3) latched: its hold kept, (0.5.4) on */
            m->lat_slot[m->nlat++] = (uint8_t)act_slot(m);      /* the active slot */
            h->kind = H_NONE;
        }
        let_go(m, h);
        m->pad_down[p] = 0, m->pad_press[p] = 0;
        if (m->edit_pad == p)
            m->edit_pad = -1;
        rfmv_pop_let(m, POP_DRUM, p);
        return;
    }
    if (m->pad_down[p])
        return;
    m->pad_down[p] = 1;
    vel = rfm_move_vel(&m->s, vel);                     /* (the velocity setting: every pad that plays, any mode) */
    m->vel_last = (uint8_t)vel;
    h->kind = H_NONE;
    if (rfmv_t4_menu(m)) {                              /* (0.8.0) Track 4 held: pad 1 the Mixer, pad 2 Routing */
        h->kind = H_MENU;
        rfmv_menu(m, MENU_T4, rfmv_t4_page(m));
        if ((p == 24 || p == 25) && rfmv_fx_ok(m))
            rfmv_view_open(m, p == 24 ? VIEW_MIXER : VIEW_ROUTING);
        return;
    }
    if ((t = rfmv_lay_menu(m)) != 0) {                  /* (0.8.0) a firmware Track held: its layout menu; (0.11.0,
                                                         * the owner's) in session view the pads are the routing: a
                                                         * column's track to that firmware or not */
        h->kind = H_MENU;
        if (rfmv_sess_on(m))
            rfmv_seq_route_pad(m, t - 1, p);
        else
            lay_pad(m, t - 1, p);
        return;
    }
    if (fm1_seq_layer(m) && m->seq.projects) {
        h->kind = H_MENU;                               /* (projects mode: its grid. The Shift menus leave the pads to
                                                         * the mode, the owner's) */
        rfmv_seq_pad(m, p, vel, now);
        return;
    }
    if (rfmv_seq_trk_held(m)) {                         /* (the Sequencer's lock menu: the pads do nothing) */
        h->kind = H_MENU;
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ) {
        if (m->latch && !m->seq.view && !m->seq.projects && !m->seq.step_held) {   /* (0.9.0) latch in a note layout
                                                         * (the sequencer): a latched note pressed again goes; a new
                                                         * chord lets the old one go */
            const int n = rfm_move_pad_note(&m->s, p), ch = m->seq.tr[m->seq.sel_track].ch;
            int i, others = 0;
            if (n >= 0 && (i = latch_find(m, ch, n)) >= 0) {
                latch_drop(m, i);
                h->kind = H_MENU;
                return;
            }
            for (i = 0; i < RFM_MOVE_NPADS; i++)
                others += i != p && m->pad[i].kind == H_NOTE;
            if (!others)
                latch_drop_slot(m);
        }
        rfmv_seq_pad(m, p, vel, now);
        return;
    }
    if (!midi_mode(m)) {
        const int k = PAD_KEY[p];
        if (k >= 0) {
            h->kind = H_KEY, h->a = (uint8_t)k;
            key_hold(m, k, vel);
        }
        return;
    }
    {
        const int n = rfm_move_pad_note(&m->s, p);
        int i, others = 0;
        if (n < 0)
            return;
        if (m->latch) {                                 /* (0.5.3) latch */
            if ((i = latch_find(m, m->s.channel, n)) >= 0) {
                latch_drop(m, i);                       /* (a latched note pressed again: it goes) */
                h->kind = H_MENU;
                return;
            }
            for (i = 0; i < RFM_MOVE_NPADS; i++)
                others += i != p && m->pad[i].kind == H_NOTE;
            if (!others)
                latch_drop_slot(m);                     /* (a new chord: the old one goes, this slot's) */
        }
        h->kind = H_NOTE, h->a = m->s.channel, h->b = (uint8_t)n;
        note_hold(m, h->a, n, vel);
        if (m->s.layout == RFM_MOVE_LAYOUT_DRUM)
            m->edit_pad = (int8_t)p;
    }
}

static void on_pad_pressure(rfm_move_t *m, int p, int v)
{
    const rfm_move_hold_t *h = &m->pad[p];
    v = clampi(v, 0, 127);
    if (!m->pad_down[p] || m->pad_press[p] == v)
        return;
    m->pad_press[p] = (uint8_t)v;
    if (h->kind == H_KEY && m->key_press[h->a] != v) {
        m->key_press[h->a] = (uint8_t)v;
        emit(m, RFM_MOVE_ACT_PRESS, h->a, 0, 0, v);
    } else if (h->kind == H_NOTE)
        emit_midi(m, 0xA0 | h->a, h->b, v);
}

static void on_step(rfm_move_t *m, int s, int down, uint32_t now)
{
    rfm_move_hold_t *h = &m->step[s];
    int t;
    if (!down) {
        if (h->kind == H_SEQ || (h->kind == H_MENU && s == 10 && m->seq.arp_sh)) {   /* (Sequencer mode's step: its
                                                         * release; 0.12.0: Track 4's Shift + step 11 too, a gated click) */
            const int sm = m->seq.smenu;
            rfmv_seq_step(m, s, 0, now);
            if (rfmv_t4_menu(m) && m->seq.smenu && m->seq.smenu != sm)
                m->seq.t4_opened = 1;
        }
        let_go(m, h);
        return;
    }
    if (h->kind != H_NONE)
        return;
    m->seq.tflash = 0;                                  /* (0.12.x: a step pressed ends a Track tap's flash) */
    if (rfmv_t4_menu(m) || (m->seq.projects && m->fw.fx && m->shift_down)) {
        h->kind = H_MENU;                               /* (0.8.0) Track 4 held (any mode), (0.9.0) Shift in the Mixer or
                                                         * Routing: Shift + step, the Move's shortcuts on the sequencer */
        fm1_shift(m, 1, s, 1, now);
        return;
    }
    if ((t = rfmv_seq_trk_held(m)) != 0) {              /* (0.9.0) a firmware Track held, any layout: the steps are the
                                                         * 16 tracks (select, solo, not routed: seq_ui.c) */
        h->kind = H_SEQ;
        rfmv_seq_step(m, s, 1, now);
        return;
    }
    if (rfmv_fwp_full(m) && !m->fwg) {                  /* (0.12.x) the FM-1 page's level 2: the steps are the FM-1's
                                                         * keys, as in the FM-1 layout */
        h->kind = H_KEY, h->a = (uint8_t)STEP_KEY[s];
        key_hold(m, h->a, 100);
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ) {
        h->kind = H_SEQ;                                /* (a sequencer step press, for its release; the lock menu's
                                                         * steps are the tracks there) */
        rfmv_seq_step(m, s, 1, now);
    } else if (!midi_mode(m)) {
        h->kind = H_KEY, h->a = (uint8_t)STEP_KEY[s];
        key_hold(m, h->a, 100);                         /* (steps have no velocity: the FM-1's own default, 100) */
    }
}

/* (0.8.0) a note layout's Play: the sequencer's transport (the buttons there are RemoteFM1's own) */
static void play_toggle(rfm_move_t *m)
{
    if (m->seq.playing || m->seq.start_pending)
        rfmv_seq_stop(m);
    else
        rfmv_seq_start(m);
}

/* (0.12.x, the owner's) Back held: its shortcuts' layer, in the sequencer's track and session views (an open Shift menu
 * too); not in the FM-1 layout, a takeover, the projects view, under a held Track or a held step */
static const uint8_t BACK_CC[] = { MR_CC_LEFT, MR_CC_RIGHT, MR_CC_UP, MR_CC_DOWN, MR_CC_LOOP, MR_CC_PLAY, MR_CC_DELETE,
                                   MR_CC_SHIFT, MR_CC_CAPTURE, MR_CC_UNDO, MR_CC_COPY, MR_CC_MUTE, MR_CC_REC,
                                   MR_CC_SAMPLE };
static int back_bit(int cc)
{
    int i;
    for (i = 0; i < (int)sizeof BACK_CC; i++)
        if (BACK_CC[i] == cc)
            return i;
    return -1;
}
int rfmv_back_keys(const rfm_move_t *m)
{
    return m->back_down && m->s.mode == RFM_MOVE_MODE_SEQ && !m->seq.projects && !m->track_held && !m->seq.step_held &&
           !m->seq.fill && rfm_move_states_open(m) < 0 &&   /* (0.13.0: not in the States layer, where Back closes it) */
           !rfmv_fwp_full(m);                           /* (the FM-1 page's level 2: as the FM-1 layout) */
}
/* (0.14.0, the owner's 2026-11-24: "Holding back + Play should always trigger play on the Move's side") where Play
 * alone is the firmware's: the FM-1 layout (a takeover's too: its Play claimed or not), the FM-1 page's level 2
 * (Knobs+Keys, Delete / Copy not held) */
uint32_t rfmv_ui_rnd(rfm_move_t *m)
{
    uint32_t x = m->ui_rng ? m->ui_rng : 0x2545F491u;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return m->ui_rng = x;
}
int rfmv_play_fw(const rfm_move_t *m)
{
    if (m->seq.projects || rfmv_t4_menu(m) || rfmv_lay_menu(m))
        return 0;
    return m->s.mode == RFM_MOVE_MODE_FM1 || (m->s.mode == RFM_MOVE_MODE_SEQ && rfmv_fwp_full(m) && !m->fwg);
}
/* (0.14.0, the owner's test note: "a global setting to enable so that a firmware's play button also drives the Move's
 * playback") Global settings' FW Play on Both: a Play press that goes to the firmware also starts / stops the Move's
 * transport (Shift + Play, the firmware's shifted Play, does not) */
void rfmv_play_both(rfm_move_t *m)
{
    if (m->s.fw_play)
        play_toggle(m);
}
static void on_button(rfm_move_t *m, int cc, int down, uint32_t now)
{
    rfm_move_hold_t *h;
    int b, t;
    if (cc == MR_CC_SHIFT && !down && m->fwp_glo)      /* (0.14.0) the FM-1 page's Shift + knob: GLO up with Shift
                                                         * (Shift's own release goes on below) */
        m->fwp_glo = 0, btn_let(m, RFM_MOVE_B_GLO);
    if ((b = back_bit(cc)) >= 0) {                      /* (0.12.x) Back held + a button: at its press, Back's
                                                         * shortcut (or nothing: dead); its release: nothing */
        if (!down && (m->back_cut >> b & 1)) {
            m->back_cut = (uint16_t)(m->back_cut & ~(1u << b));
            return;
        }
        if (down && rfmv_back_keys(m)) {
            m->back_cut = (uint16_t)(m->back_cut | 1u << b);
            rfmv_seq_back_key(m, cc);
            return;
        }
        if (down && cc == MR_CC_PLAY && m->back_down && rfmv_play_fw(m)) {
            m->back_cut = (uint16_t)(m->back_cut | 1u << b);   /* (0.14.0) Back held + Play where Play is the
                                                         * firmware's: the Move's Play, as in the track view (nothing
                                                         * to the firmware; its release nothing either) */
            m->back_used = 1;
            play_toggle(m);
            rfmv_pop(m, POP_BACK, m->seq.playing || m->seq.start_pending ? BK_PLAY : BK_STOP);
            return;
        }
    }
    if (rfmv_t4_menu(m) && (cc == MR_CC_REC || cc == MR_CC_PLAY || cc == MR_CC_CAPTURE || cc == MR_CC_UNDO ||
                            cc == MR_CC_LOOP || cc == MR_CC_SAMPLE)) {
        if (down)                                       /* Track 4 held: Shift + Record (the record settings), Shift +
                                                         * Play, Capture, Undo (redo), Loop (double) on the sequencer */
            fm1_shift(m, 0, cc, 1, now);
        return;
    }
    if (rfmv_t4_menu(m) && down && cc != MR_CC_BACK) {  /* (0.12.0, a8) Track 4's hold layer: Up, Down, Left, Right,
                                                         * Menu, Copy, Delete, Mute, Shift have no job in it: dead (and
                                                         * dark); their releases pass */
        rfmv_menu(m, MENU_T4, rfmv_t4_page(m));
        return;
    }
    if ((t = rfmv_lay_menu(m)) != 0 && (cc == MR_CC_UP || cc == MR_CC_DOWN)) {
        const int o = clampi(m->s.octave + (cc == MR_CC_UP ? 1 : -1), RFM_MOVE_OCT_MIN, RFM_MOVE_OCT_MAX);
        if (down && !rfmv_sess_on(m)) {                 /* a firmware Track held: the pads' octave (0.11.0: not in
                                                         * session view: the held Track is the routing only there) */
            rfmv_menu(m, MENU_OCT, o - RFM_MOVE_OCT_MIN);
            if (o != m->s.octave) {
                m->s.octave = (int8_t)o;
                changed(m);
            }
        }
        return;
    }
    if (cc == MR_CC_MENU && !m->seq.projects && (m->back_down || rfmv_lay_menu(m))) {
        /* (0.12.0, the owner's) Back held + Menu, or Track 1..3 held + Menu (the Move-side Shift where the FM-1 or a
         * takeover has the Move's Shift): session view in or out, in every layout. The Track's hold is used up: its
         * menu, tap and double click do not come (its release: nothing); Back's click does not come either */
        if (down) {
            int k;
            for (k = 0; k < 3; k++)
                if (track_held(m, k))
                    m->track_held = (uint8_t)(m->track_held & ~(1u << k)), m->tbtn_up[k] = 0;
            rfmv_menu(m, MENU_NONE, 0);
            if (m->back_down)
                m->back_used = 1;
            rfmv_sv_toggle(m);
        }
        return;
    }
    if (cc == MR_CC_LOOP && !down && m->seq.fill) {    /* (FILL's Loop let go: wherever Shift was) */
        m->seq.fill = 0;
        if (m->s.mode != RFM_MOVE_MODE_SEQ) {
            m->seq.loop_end = 0;
            return;
        }
    }
    if (m->s.mode != RFM_MOVE_MODE_SEQ && cc == MR_CC_BACK &&
        (m->seq.smenu || m->seq.projects || m->seq.loop_latch || m->seq.shift_lock)) {
        if (down)                                       /* (a sequencer layer open: Back closes it; 0.12.0, l2: Menu
                                                         * never closes a menu: in the FM-1 layout it is the FM-1's
                                                         * HOME, the menu stays) */
            rfmv_seq_button(m, MR_CC_BACK, 1, now);
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ && (cc == MR_CC_DELETE || cc == MR_CC_COPY) &&
        (down ? m->btn[cc].kind == H_NONE && rfmv_fwp_full(m) : m->btn[cc].kind == H_GATE)) {
        /* (0.14.0, the owner's "Clearing the clip"; item 14) the FM-1 page's level 2: Delete and Copy are the
         * sequencer's modifiers at the press (Delete + Play clears the clip, + a pad its notes, + a step); a click
         * (let go within RFM_MOVE_TAP_MS, nothing else pressed) is then also the firmware's button, as a tap (down
         * and up at the release); held longer or used with something: the firmware hears nothing of it */
        const int bit = cc == MR_CC_DELETE ? 1 : 2, i = cc == MR_CC_DELETE ? 0 : 1;
        if (down) {
            m->btn[cc].kind = H_GATE;
            m->fwg = (uint8_t)(m->fwg | bit), m->fwg_other = (uint8_t)(m->fwg_other & ~bit), m->fwg_ms[i] = now;
            rfmv_seq_button(m, cc, 1, now);
            return;
        }
        m->btn[cc].kind = H_NONE;
        m->fwg = (uint8_t)(m->fwg & ~bit);
        rfmv_seq_button(m, cc, 0, now);
        if (!(m->fwg_other & bit) && !(m->seq.mods_used & (cc == MR_CC_DELETE ? SEQ_M_DELETE : SEQ_M_COPY)) &&
            now - m->fwg_ms[i] < RFM_MOVE_TAP_MS && rfmv_fwp_full(m)) {
            rfm_move_hold_t tap;
            tap.kind = H_BTN, tap.a = (uint8_t)rfm_move_button_of(cc), tap.b = 0;
            btn_hold(m, tap.a);
            let_go(m, &tap);
        }
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ && cc >= 0 && cc < 128 &&
        (down ? m->btn[cc].kind == H_NONE && rfmv_fwp_full(m) && fwp_btn(cc) && !m->fwg : m->btn[cc].kind == H_BTN)) {
        h = &m->btn[cc];                                /* (0.12.x) the FM-1 page's level 2: the FM-1 layout's buttons
                                                         * (Play the firmware's PLAY too) */
        if (!down)
            let_go(m, h);
        else {
            h->kind = H_BTN, h->a = (uint8_t)rfm_move_button_of(cc), btn_hold(m, h->a);
            if (cc == MR_CC_PLAY && !m->shift_down && m->btn[MR_CC_SHIFT].kind != H_BTN)
                rfmv_play_both(m);                      /* (0.14.0: FW Play on Both) */
        }
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ && cc >= 0 && cc < 128 && (m->btn[cc].kind == H_NONE || m->btn[cc].kind == H_MENU)) {
        if (!down && m->btn[cc].kind == H_MENU) {
            m->btn[cc].kind = H_NONE;
            rfmv_seq_button(m, cc, 0, now);
            return;
        }
        if (down && m->btn[cc].kind == H_NONE && rfmv_seq_button(m, cc, 1, now)) {
            if (m->s.mode == RFM_MOVE_MODE_SEQ)         /* (Back leaves the mode) */
                m->btn[cc].kind = H_MENU;
            return;
        }
    }
    if (cc == MR_CC_SHIFT)
        m->shift_down = (uint8_t)down;
    if (midi_mode(m)) {                                 /* (0.8.0) a note layout: the buttons are RemoteFM1's own;
                                                         * only the FM-1 layout reaches the FM-1's */
        if (cc == MR_CC_UP || cc == MR_CC_DOWN) {       /* the octave */
            const int o = clampi(m->s.octave + (cc == MR_CC_UP ? 1 : -1), RFM_MOVE_OCT_MIN, RFM_MOVE_OCT_MAX);
            if (down && o != m->s.octave) {
                m->s.octave = (int8_t)o;
                changed(m);
            }
        } else if (down && m->shift_down && (cc == MR_CC_REC || cc == MR_CC_PLAY || cc == MR_CC_CAPTURE ||
                                             cc == MR_CC_UNDO || cc == MR_CC_LOOP || cc == MR_CC_SAMPLE))
            fm1_shift(m, 0, cc, 1, now);                /* (Shift + Record, Play .. as the Shift layer) */
        else if (down && cc == MR_CC_PLAY)
            play_toggle(m);                             /* (Play: the sequencer's transport) */
        return;
    }
    if (cc == MR_CC_DOWN) {
        if (down)                                       /* (0.4.4) the FM-1 layout: the next loaded firmware (no repeat) */
            rfmv_fw_cycle(m);
        return;
    }
    if (cc < 0 || cc > 127 || (b = rfm_move_button_of(cc)) < 0)
        return;
    h = &m->btn[cc];
    if (!down) {
        let_go(m, h);
        return;
    }
    if (h->kind != H_NONE)
        return;
    h->kind = H_BTN, h->a = (uint8_t)b;
    btn_hold(m, b);
    if (cc == MR_CC_PLAY && !m->shift_down && rfmv_play_fw(m))
        rfmv_play_both(m);                              /* (0.14.0: FW Play on Both: the FM-1 layout's Play) */
}

/* (0.8.0) a note layout's knob k's CC number (Track n held + the knob, or Shift + the knob there), for slot t */
static void cc_number(rfm_move_t *m, int t, int k, int d)
{
    rfm_move_slotset_t *ss = &m->s.slot[t];
    const int c = clampi(ss->cc[k] + d, 0, 127);
    if (c != ss->cc[k]) {
        ss->cc[k] = (uint8_t)c;
        changed(m);
    }
}
/* an FM-1 encoder turned by a knob or the jog: the turn (knobs: an FM-1 detent every RFM_MOVE_KNOB_TICKS), the popup
 * counting this gesture's detents (knob k's; the jog's on the knob that turns the same) */
static void enc_turn(rfm_move_t *m, int k, int d, int jog)
{
    const int e = KNOB_ENC[k];
    const int acc = m->pop == POP_ENC && m->pop_i == k && (m->touched == k || rfmv_pop_up(m, POP_ENC)) ? m->pop_val : 0;
    int t = d;
    if (!jog) {
        if ((m->knob_acc[k] > 0 && d < 0) || (m->knob_acc[k] < 0 && d > 0))
            m->knob_acc[k] = 0;                         /* (a turn back: the half detent forgotten) */
        t = m->knob_acc[k] + d;                         /* the owner's: half as fast, an FM-1 detent every 2 ticks */
        m->knob_acc[k] = (int8_t)(t % RFM_MOVE_KNOB_TICKS);
        t /= RFM_MOVE_KNOB_TICKS;
    }
    if (t)
        emit(m, RFM_MOVE_ACT_TURN, e, 0, 0, t);
    if (enc_nojob(m, e))                                /* (0.5.6: "" = no job: no popup) */
        return;
    if (!(m->pop == POP_ENC && m->pop_i == k && rfmv_pop_up(m, POP_ENC) && !m->pop_blank))
        m->turn_ms = rfmv_now_ms(m);                    /* (0.14.0: a turn begins) */
    rfmv_pop(m, POP_ENC, k);
    m->pop_val = (int16_t)clampi(acc + t, -9999, 9999);
    rfmv_say_turned(m);                                 /* (0.6.7: speech: its readout awaited) */
}

static void on_knob(rfm_move_t *m, int k, int d, uint32_t now)
{
    int t;
    if (!d)
        return;
    if (k == 8 && m->s.mode == RFM_MOVE_MODE_SEQ && rfmv_seq_volume(m, d))
        return;                                         /* (Sequencer mode, a step held: its velocity) */
    if (k == 8 && rfmv_fw_volume(m, d)) {              /* (0.4.0, the browser on) Track 1..3 held: that slot's level */
        rfmv_menu(m, MENU_SHIFT, 0);                    /* (the Track button was used: its release is no tap) */
        return;
    }
    if (k == 8) {                                       /* volume: MASTER */
        rfmv_master_turn(m, d);
        return;
    }
    if (k < 0 || k >= RFM_MOVE_NKNOBS)
        return;
    if ((t = rfmv_lay_menu(m)) != 0) {                  /* (0.8.0) a firmware Track held: in a note layout (0.9.0: the
                                                         * sequencer) the selected track's CC numbers (seq_ui.c); the
                                                         * FM-1 layout, a takeover: nothing */
        if (m->s.mode == RFM_MOVE_MODE_SEQ && !m->seq.view) {   /* (the track selected for that firmware: its CC
                                                         * number; 0.11.0: session view: nothing) */
            const int tr = m->s.lock[t - 1] % RFM_MOVE_SEQ_TRACKS, b = m->s.bank & 3;
            if (tr == m->seq.sel_track)
                rfmv_seq_knob(m, k, d, now);
            else {
                uint8_t *c = &m->seq.tr[tr].cc[b][k];
                const int v = clampi(*c + d, 0, 127);
                rfmv_menu(m, MENU_CC, k);
                if (v != *c)
                    *c = (uint8_t)v, rfmv_seq_dirty(m);
            }
        }
        return;
    }
    if (rfmv_t4_menu(m) && !(m->seq.smenu && m->seq.t4_opened))
        return;                                         /* (Track 4 held: the knobs do nothing; 0.12.0: a menu it opened
                                                         * has them) */
    if (fm1_seq_layer(m) && m->seq.smenu) {
        rfmv_seq_knob(m, k, d, now);                    /* (a sequencer menu open outside it: its knobs) */
        return;
    }
    if (rfmv_fwp_page(m) && !m->seq.smenu && !m->seq.step_held) {
        if (rfmv_fwp_live(m) && KNOB_ENC[k] >= 0) {     /* (0.12.x) the FM-1 page: the FM-1 layout's knobs (an empty
                                                         * slot: dead) */
            if (!m->seq.fwp_full && !m->fwp_glo && m->btn[MR_CC_SHIFT].kind != H_NONE) {
                /* (0.14.0, the owner's 2026-11-24: "While not in takeover mode, the Move's shift button plus a FM-1
                 * knob turn should also do the shift and show the engine change") Knobs: Shift held + a turn is the
                 * firmware's shifted turn, as the FM-1 layout's: its GLO down before the first one, up with Shift */
                m->fwp_glo = 1;
                btn_hold(m, RFM_MOVE_B_GLO);
                m->seq.mods_used |= SEQ_M_SHIFT;        /* (Shift's release: no latch) */
            }
            enc_turn(m, k, d, 0);
        }
        return;
    }
    if (rfmv_ep_page(m) && !m->seq.smenu && !m->seq.step_held) {
        rfmv_ep_knob(m, k, d);                          /* (0.14.0) the ENGINE page: the page's parameters, absolute
                                                         * (a held step: the TRIG knobs, as on the FM-1 page) */
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ) {               /* (the lock menu: the track's CC numbers, seq_ui.c) */
        rfmv_seq_knob(m, k, d, now);
        return;
    }
    if (midi_mode(m)) {                                 /* (0.8.0) a note layout: the Control CCs on the slot's channel;
                                                         * Shift + a knob its CC number */
        const int s = act_slot(m);
        if (m->shift_down) {
            cc_number(m, s, k, d);
            rfmv_pop(m, POP_CCNUM, k);
            return;
        }
        {
            const int v = clampi(m->slot_ccv[s][k] + d, 0, 127);
            if (v != m->slot_ccv[s][k]) {
                m->slot_ccv[s][k] = (uint8_t)v;
                emit_midi(m, 0xB0 | m->s.channel, m->s.slot[s].cc[k], v);
            }
            rfmv_pop(m, POP_CC, k);
        }
        return;
    }
    if (KNOB_ENC[k] >= 0)
        enc_turn(m, k, d, 0);
}

void rfmv_master_turn(rfm_move_t *m, int d)
{
    const int v = clampi(m->master + d * RFM_MOVE_MASTER_STEP, 0, 1023);
    rfmv_pop(m, POP_MASTER, 8);
    if (v != m->master) {
        m->master = (uint16_t)v;
        emit(m, RFM_MOVE_ACT_MASTER, 0, 0, 0, v);
    }
}

/* the FM-1 layout's jog (and, 0.14.0, the FM-1 page's when on a knob): SELECT (as before), or PRESETS / ALGO as their
 * knobs (2, 3) turn them, a detent a tick */
static void jog_target_turn(rfm_move_t *m, int d)
{
    if (m->jog_enc % 3 == 0) {
        emit(m, RFM_MOVE_ACT_TURN, RFM_MOVE_E_SELECT, 0, 0, d);
        {
            const int acc = rfmv_pop_up(m, POP_SELECT) ? m->pop_val : 0;
            if (!rfmv_pop_up(m, POP_SELECT))
                m->turn_ms = rfmv_now_ms(m);            /* (0.14.0: a turn begins) */
            rfmv_pop(m, POP_SELECT, 9);
            m->pop_val = (int16_t)clampi(acc + d, -9999, 9999);
            rfmv_say_turned(m);
        }
    } else
        enc_turn(m, m->jog_enc % 3, d, 1);
}
/* (0.14.0, the owner's 2026-11-23) the FM-1 knob page's jog on a knob: its turn is the FM-1 layout's jog (the slot's
 * target, shared with that layout), while nothing above the page takes the jog (a menu, a held step, Loop, Shift's
 * tempo, a held Track, Track 4's layer) */
static int fwj_free(const rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    return rfmv_fwp_live(m) && !q->smenu && !q->step_held && !(q->mods & (SEQ_M_SHIFT | SEQ_M_LOOP)) && !q->loop_latch &&
           !rfmv_lay_menu(m) && !rfmv_t4_menu(m) && !(m->s.layout == RFM_MOVE_LAYOUT_DRUM && m->edit_pad >= 0 &&
                                                       m->pad_down[m->edit_pad]);
}
/* (the owner's 2026-11-23, second word: "No double click on the Jogwheel to toggle takeover. Single click and shift +
 * jogwheel to change jogwheel mode") the jog turns the target while the page is taken over (Knobs+Keys: all of it the
 * FM-1 layout's, its jog too) or once Shift + click has put it on a knob */
static int fwj_set(const rfm_move_t *m) { return rfmv_fwp_live(m) && (m->fwj || m->seq.fwp_full); }
int rfmv_fwj_on(const rfm_move_t *m) { return fwj_set(m) && fwj_free(m); }
int rfmv_fwj_lit(const rfm_move_t *m) { return fwj_set(m); }
static int shift_held(const rfm_move_t *m) { return (m->seq.mods & SEQ_M_SHIFT) || m->btn[MR_CC_SHIFT].kind != H_NONE; }
/* the jog's press on the page. A click: Knobs <-> Knobs+Keys (the takeover; 0.12.x's click, as before 0.14.0's first
 * cut). Shift + click: what the jog turns, the target shared with the FM-1 layout (whose own click changes it): taken
 * over, the next of SELECT -> PRESET -> ALGO as that layout's click; else the pages -> SELECT -> PRESET -> ALGO -> the
 * pages again. The target's knob is lit white while the jog is on it (R1) */
/* (0.14.0, the owner's 2026-11-24: "Back should exit out of the FM-1 knob lock. If I go to another track or mode and
 * come back, the lock state should exit") the lock: the page taken over (Knobs+Keys) or the jog on a knob. Back's
 * click with nothing else to close ends it ("Knobs"); another mode, slot, layout, track, view ends it too (the track's
 * knob page itself stays remembered) */
int rfmv_fwp_locked(const rfm_move_t *m) { return rfmv_fwp_live(m) && (m->seq.fwp_full || m->fwj); }
void rfmv_fwp_unlock(rfm_move_t *m) { m->seq.fwp_full = 0, m->fwj = 0; }
void rfmv_fwp_ctx(rfm_move_t *m)
{
    const int a = act_slot(m);
    const uint32_t k = (uint32_t)(m->s.mode & 7) | (uint32_t)(a & 3) << 3 | (uint32_t)(m->seq.sel_track & 15) << 5 |
                       (uint32_t)(m->seq.view != 0) << 9 | (uint32_t)(m->seq.projects != 0) << 10 |
                       (uint32_t)(rfmv_slot_lay(m, a) & 15) << 11 | (uint32_t)(m->s.layout & 7) << 15 | 1u << 31;
    if (k != m->fwp_key)
        m->fwp_key = k, rfmv_fwp_unlock(m);
}
static void fwj_press(rfm_move_t *m)
{
    if (shift_held(m)) {
        if (m->seq.fwp_full)
            m->jog_enc = (uint8_t)((m->jog_enc + 1) % 3);
        else if (!m->fwj)
            m->fwj = 1, m->jog_enc = 0;
        else if (m->jog_enc % 3 == 2)
            m->fwj = 0, m->jog_enc = 0;
        else
            m->jog_enc = (uint8_t)(m->jog_enc % 3 + 1);
        m->seq.mods_used |= SEQ_M_SHIFT;
        rfmv_pop(m, POP_FWJ, fwj_set(m) ? 1 + m->jog_enc % 3 : 0);
        return;
    }
    m->seq.fwp_full = (uint8_t)!m->seq.fwp_full;
    if (!m->seq.fwp_full)
        m->fwj = 0;                                     /* (out of the takeover: the jog its pages, always) */
    rfmv_pop(m, POP_FWLV, m->seq.fwp_full);
}

static void on_jog(rfm_move_t *m, int d, uint32_t now)
{
    int t;
    if (!d)
        return;
    if ((t = rfmv_lay_menu(m)) != 0) {                  /* a firmware Track held: the root (the key the tracks use) */
        const int r = ((key_root(m) + d) % 12 + 12) % 12;
        if (rfmv_sess_on(m))
            return;                                     /* (0.11.0: session view: the held Track is the routing only) */
        rfmv_menu(m, MENU_ROOT, r);
        key_set(m, r, -1);
        return;
    }
    if (rfmv_t4_menu(m) && !(m->seq.smenu && m->seq.t4_opened))
        return;                                         /* (Track 4 held: dead; 0.12.0: a menu it opened has the jog) */
    if (fm1_seq_layer(m) && m->seq.smenu) {
        rfmv_seq_jog(m, d, now);                        /* (a sequencer menu open outside it) */
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ && rfmv_fwj_on(m)) {
        jog_target_turn(m, d);                          /* (0.14.0: the FM-1 page's jog on a knob) */
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ) {
        rfmv_seq_jog(m, d, now);
        return;
    }
    if (midi_mode(m)) {                                 /* a note layout: only a held drum pad's note */
        if (m->s.layout == RFM_MOVE_LAYOUT_DRUM && m->edit_pad >= 0 && m->pad_down[m->edit_pad]) {
            const int p = m->edit_pad, n = clampi(m->s.drum[p] + d, 0, 127);
            if (n != m->s.drum[p]) {
                m->s.drum[p] = (uint8_t)n;
                changed(m);
            }
            rfmv_pop(m, POP_DRUM, p);
        }
        return;
    }
    jog_target_turn(m, d);
}

/* (0.8.0) the jog's click in the FM-1 layout: what it turns, SELECT -> PRESET -> ALGO, named a second */
static void on_jog_click(rfm_move_t *m, int down)
{
    if (down && m->seq.smenu && !rfmv_lay_menu(m) && (!rfmv_t4_menu(m) || m->seq.t4_opened) &&
        (m->s.mode == RFM_MOVE_MODE_SEQ || fm1_seq_layer(m)) && rfmv_seq_menu_click(m))
        return;                                         /* (0.13.0, the owner's) a Shift menu: the jog's click edits the
                                                         * selected parameter, again: selects (where its turn goes) */
    if (down && m->seq.smenu == RFM_MOVE_SM_ARP && !rfmv_lay_menu(m) && !rfmv_t4_menu(m)) {
        rfmv_seq_arp_click(m);                          /* (0.11.0, the owner's) the arp menu: the jog's click: on / off
                                                         * (0.14.0: Delete held: reset; a lane: random) */
        return;
    }
    if (rfmv_ep_page(m) && !rfmv_lay_menu(m) && !rfmv_t4_menu(m) && !m->seq.smenu) {
        rfmv_ep_click(m, down);                         /* (0.14.0, the owner's) the ENGINE page: the page list, at the
                                                         * click's release (no double click there) */
        return;
    }
    if (down && rfmv_fwp_live(m) && !rfmv_lay_menu(m) && !rfmv_t4_menu(m) && !m->seq.smenu) {
        fwj_press(m);                                   /* (0.14.0) the FM-1 page: a click knobs <-> knobs + keys,
                                                         * Shift + click what the jog turns */
        return;
    }
    if (!down || m->s.mode != RFM_MOVE_MODE_FM1 || rfmv_lay_menu(m) || rfmv_t4_menu(m) || fm1_seq_layer(m))
        return;
    m->jog_enc = (uint8_t)((m->jog_enc + 1) % 3);
    rfmv_pop(m, POP_JOG, m->jog_enc);
}

/* a touch: the knob's popup (in FM-1 mode the drawing marks the knob instead, and a turn brings the popup) */
static void on_touch(rfm_move_t *m, int k, int on)
{
    int t;
    if (on && m->speech && m->seq.smenu && k < RFM_MOVE_NKNOBS && !rfmv_seq_held_takes(m)) {   /* (0.6.3) a Shift menu's
                                                         * knob touched: its item (0.12.0: not a held step's knob) */
        char w[64];
        rfmv_seq_smenu_words(m, k, w, sizeof w);
        if (w[0])
            rfmv_say_text(m, w);
    }
    if (k == 9)
        m->jog_touch = (uint8_t)on;                     /* (0.5.8: its own flag; a knob touched meanwhile is touched) */
    if (!on) {
        if (m->touched == k)
            m->touched = -1;
        if (k < RFM_MOVE_NKNOBS)
            m->knob_acc[k] = 0, m->list_acc[k] = 0;
        if ((rfmv_half(m) || (rfmv_fwp_page(m) && m->pop == POP_ENC)) && (m->pop == POP_ENC || m->pop == POP_MASTER) &&
            m->pop_i == k) {
            m->pop = POP_NONE, m->pop_val = 0;          /* (FM-1 mode, MIDI's NOTES: the release ends the gesture: the
                                                         * popup goes, its count starts again) */
            return;
        }
        if (m->pop == POP_TRKINFO && m->pop_i == k)
            m->pop = POP_NONE;                          /* (0.10.0: session's track popup goes with the touch) */
        if (m->pop == POP_ENC || m->pop == POP_MASTER || m->pop == POP_CC || m->pop == POP_TRIG || m->pop == POP_CCNUM ||
            m->pop == POP_EPK)
            rfmv_pop_let(m, m->pop, k);
        return;
    }
    m->touched = (int8_t)k;
    if (k == 8 && rfmv_fw_volume(m, 0))                 /* (0.13.0, R5) a Track 1..3 held, the volume knob touched: its
                                                         * slot's level shows at the touch (a turn changes it) */
        return;
    if ((t = rfmv_lay_menu(m)) != 0) {                  /* (0.8.0) a firmware Track held: a note layout's knob shows its
                                                         * CC number (the FM-1 layout's: nothing) */
        if (k < RFM_MOVE_NKNOBS && m->s.mode == RFM_MOVE_MODE_SEQ && !m->seq.view)
            rfmv_menu(m, MENU_CC, k);
        return;
    }
    if (rfmv_t4_menu(m) && !(m->seq.smenu && m->seq.t4_opened))
        return;
    if (k < RFM_MOVE_NKNOBS && m->seq.smenu && rfmv_menu_knobs(m) >= 0)
        return;                                         /* (0.12.0, a5 .. a7) an open Shift menu: its header names the
                                                         * knob, in every view and layout; no other popup */
    if (k <= 8 && rfmv_seq_cs_touch(m, k))
        return;                                         /* (0.12.0) a select step held: its clip's settings take the
                                                         * knobs (no track popup, no select) */
    if (rfmv_sess_on(m) && !m->seq.smenu && k < 8) {    /* (0.11.0: an open Shift menu has the knobs there too) */
        if (k < RFM_MOVE_SV_COLS)                       /* (session: a knob touched selects the track in its column;
                                                         * 0.10.0: and shows its popup while touched) */
            rfmv_seq_select(m, m->seq.track_page + k);
        rfmv_pop(m, POP_TRKINFO, k);                    /* (0.11.0, the owner's: knob 8: the scenes) */
        return;
    }
    if (k < RFM_MOVE_NKNOBS && rfmv_fwp_page(m) && !m->seq.step_held) {   /* (0.12.x) the FM-1 page: as the FM-1
                                                         * layout: the knob's name (a knob with no job: nothing) */
        if (rfmv_fwp_live(m) && KNOB_ENC[k] >= 0 && !enc_nojob(m, KNOB_ENC[k]))
            rfmv_pop(m, POP_ENC, k), m->pop_blank = 1;
        return;
    }
    if (k < RFM_MOVE_NKNOBS && rfmv_ep_page(m) && !m->seq.step_held && rfmv_ep_touch(m, k))
        return;                                         /* (0.14.0) the ENGINE page: the name and the value (R5); a
                                                         * knob with nothing on it: nothing */
    if (rfmv_half(m)) {                                 /* (0.5.6, the owner's: FM-1 mode, MIDI's NOTES: a touch alone
                                                         * shows the knob's name (and MASTER its level); a turn its
                                                         * detents) */
        if (k == 8)
            rfmv_pop(m, POP_MASTER, 8);
        else if (k < RFM_MOVE_NKNOBS && m->s.mode == RFM_MOVE_MODE_MIDI)
            rfmv_pop(m, m->shift_down ? POP_CCNUM : POP_CC, k);
        else if (k < RFM_MOVE_NKNOBS && KNOB_ENC[k] >= 0 && !enc_nojob(m, KNOB_ENC[k]))
            rfmv_pop(m, POP_ENC, k), m->pop_blank = 1;
        return;
    }
    if (k < RFM_MOVE_NKNOBS && rfmv_menu_knobs(m) >= 0)
        return;                                         /* (a menu or a held Track: its own screen, the legend ring) */
    if (k == 8)
        rfmv_pop(m, POP_MASTER, 8);
    else if (k < RFM_MOVE_NKNOBS && m->s.mode == RFM_MOVE_MODE_SEQ && rfmv_seq_trig_on(m) && !m->seq.view) {
        if (k < 7)
            rfmv_pop(m, POP_TRIG, k);                   /* (0.12.0, a12: knob 8 has no job there: no popup) */
    }
    else if (k < RFM_MOVE_NKNOBS && m->s.mode == RFM_MOVE_MODE_SEQ)
        rfmv_pop(m, POP_CC, k);
    else if (k < RFM_MOVE_NKNOBS && m->s.mode == RFM_MOVE_MODE_MIDI)
        rfmv_pop(m, m->shift_down ? POP_CCNUM : POP_CC, k);
    else if (k < RFM_MOVE_NKNOBS && KNOB_ENC[k] >= 0) {
        rfmv_pop(m, POP_ENC, k);
        m->pop_val = 0;
    }
}

/* ---- (0.8.0) the Track buttons: a tap acts at its release (RFM_MOVE_TAP_MS, nothing done meanwhile); pressed again
 * within RFM_MOVE_DOUBLE_MS of that release: a double click; held: the button's menu (move.c's input, screen.c) */
static void name_layout(rfm_move_t *m)                  /* the slot played now named: its takeover's, or its layout */
{
    const int t = act_slot(m);
    if (rfmv_tk_mode(m))
        rfmv_pop(m, POP_TK, t);
    else
        rfmv_pop(m, POP_LAYOUT, t);
}
/* (0.12.0, the owner's) session view from anywhere (Menu in a note layout; Shift + Menu; Back held + Menu; Track 1..3
 * held + Menu): in, from a note layout's track view or from the FM-1 layout / a takeover (the sequencer then, the
 * slot's layout kept); out: the active slot's own layout (a note layout: its track view; the FM-1 layout or its
 * takeover). Named either way (POP_VIEW; the FM-1 layout or the takeover: its own name) */
void rfmv_sv_toggle(rfm_move_t *m)
{
    rfm_move_seq_t *q = &m->seq;
    q->pad_tap = -1;
    if (rfmv_sess_on(m)) {
        q->view = 0;
        if (m->s.slot[act_slot(m)].lay < RFM_MOVE_LAY_SCALE) {
            slot_mode_fresh(m, 1);                      /* (the FM-1 layout, the takeover) */
            name_layout(m);
        } else
            rfmv_pop(m, POP_VIEW, 0);
        return;
    }
    if (m->s.mode != RFM_MOVE_MODE_SEQ)
        set_mode(m, RFM_MOVE_MODE_SEQ);
    q->view = 1;
    rfmv_pop(m, POP_VIEW, 1);
}
static void track_tap(rfm_move_t *m, int t)
{
    if (rfmv_st_on(m)) {                                /* (0.13.0) States open: Track 1..3 shows that slot's; Track 4
                                                         * closes it and does its job */
        if (t < 3) {
            rfmv_st_track(m, t);
            return;
        }
        rfmv_st_close(m);
    }
    if (t == 3) {                                       /* the sequencer side: the Mixer <-> Routing (remembered); no
                                                         * FX setup (one firmware, no FX): the projects */
        if (!rfmv_fx_ok(m)) {
            rfmv_view_open(m, VIEW_PROJECTS);
            return;
        }
        if (m->seq.projects && m->fw.fx)
            rfmv_view_open(m, rfmv_t4_page(m) ? VIEW_MIXER : VIEW_ROUTING);
        else
            rfmv_view_open(m, rfmv_t4_page(m) ? VIEW_ROUTING : VIEW_MIXER);
        return;
    }
    if (t >= rfmv_fw_nslots(m))
        return;                                         /* (one slot: Tracks 2, 3 dark, nothing) */
    if (m->seq.projects)
        rfmv_view_close(m);                             /* (out of the view to where it came from, then the tap) */
    if (rfmv_sess_on(m)) {                              /* (0.11.0, the owner's) session view is its own layer: the
                                                         * firmware played changes, the view stays; its summary (the
                                                         * tracks that reach it) a second, the knobs' lights after */
        rfmv_fw_activate_keep(m, t);
        rfmv_pop(m, POP_SLOT, t);
        return;
    }
    if (!rfmv_fw_activate(m, t))                        /* (its layout played: rfmv_slot_changed; 0.9.0: a note layout
                                                         * is the sequencer on the slot's selected track) */
        slot_mode_fresh(m, 1);                          /* (the slot already played: again, its track view) */
    name_layout(m);
    rfmv_seq_tflash_start(m);                           /* (0.12.x: a note layout: its selected track on the steps) */
}
static void track_double(rfm_move_t *m, int t)
{
    rfmv_st_close(m);                                   /* (0.13.0: the browser or the projects over States) */
    if (t == 3)
        rfmv_view_open(m, VIEW_PROJECTS);               /* the sequencer's projects */
    else if (t < rfmv_fw_nslots(m) && m->fw.host)
        rfmv_view_open(m, VIEW_SLOT0 + t);              /* the browser on that slot */
}
/* (0.14.0, the owner's 2026-11-23: "Using SHIFT + a track button should take you to Track settings similar to Ableton's
 * Move software shortcuts") Shift held (the Move's Shift being ours: a note layout, session view, the Mixer and
 * Routing; not the FM-1 layout or a takeover, where it is the firmware's GLO) + Track 1..3: at the press, that
 * firmware played (as its tap: session view and the views stay as a tap leaves them) with its selected track, and
 * the track's settings (Workflow, Shift + step 3's menu) open over it. Track 4 with Shift: its own jobs, unchanged */
static int shift_ours(const rfm_move_t *m)
{
    if (m->seq.smenu == RFM_MOVE_SM_GLOBAL || rfmv_st_on(m) || m->track_held || m->back_down)
        return 0;
    if (m->seq.projects)
        return m->shift_down && m->fw.fx;
    return m->s.mode == RFM_MOVE_MODE_SEQ && (m->seq.mods & SEQ_M_SHIFT) && !rfmv_fwp_full(m);
}
static void track_tap(rfm_move_t *m, int t);
static void shift_track(rfm_move_t *m, int t)
{
    const int sv = rfmv_sess_on(m);
    if (t >= rfmv_fw_nslots(m))
        return;
    if (m->fw.active != t || m->seq.projects || (!sv && m->s.mode != RFM_MOVE_MODE_SEQ))
        track_tap(m, t);
    rfmv_seq_select(m, m->s.lock[t] % RFM_MOVE_SEQ_TRACKS);   /* (its selected track: session view selects it too) */
    rfmv_seq_menu_open(m, RFM_MOVE_SM_WORKFLOW);
    m->pop = POP_NONE, m->seq.tflash = 0;               /* (the menu at once, naming the track: no slot popup over it) */
    {
        char w[32];
        snprintf(w, sizeof w, "Track %d settings", m->seq.sel_track + 1);
        rfmv_say_text(m, w);
    }
}
static int track_input(rfm_move_t *m, const mr_event_t *ev, uint32_t now)
{
    int t;
    unsigned bit;
    if (ev->type != MR_EV_BUTTON || ev->index < MR_CC_TRACK4 || ev->index > MR_CC_TRACK1)
        return 0;
    t = MR_CC_TRACK1 - ev->index, bit = 1u << t;
    if (!ev->value && (m->tbtn_shift & bit)) {
        m->tbtn_shift = (uint8_t)(m->tbtn_shift & ~bit);   /* (0.14.0: Shift + Track's release: nothing) */
        return 1;
    }
    if (ev->value && t < 3 && !(m->track_held & bit) && shift_ours(m)) {
        m->tbtn_shift = (uint8_t)(m->tbtn_shift | bit);
        m->tbtn_up[t] = 0;
        shift_track(m, t);
        return 1;
    }
    if (ev->value) {
        if (m->track_held & bit)
            return 1;
        if (m->back_down)
            m->back_used = 1;                           /* (Back's click is not one now) */
        rfmv_menu(m, MENU_NONE, 0);
        if (t == 3)
            m->seq.t4_opened = 0;
        m->track_held = (uint8_t)(m->track_held | bit);
        m->tbtn_used = (uint8_t)(m->tbtn_used & ~bit);
        m->tbtn_ms[t] = now, m->tbtn_clk[t] = rfmv_now_ms(m);
        if (m->tbtn_up[t] && (int32_t)(now - m->tbtn_up[t]) < RFM_MOVE_DOUBLE_MS) {   /* (signed: a host's clock from 0) */
            m->tbtn_up[t] = 0;
            m->tbtn_dbl = (uint8_t)(m->tbtn_dbl | bit);
            track_double(m, t);
        }
        return 1;
    }
    if (!(m->track_held & bit))
        return 1;
    m->track_held = (uint8_t)(m->track_held & ~bit);
    rfmv_seq_tk_flush(m);                               /* (0.11.0: a track's click that waited: it acts now) */
    if (m->tbtn_dbl & bit) {                            /* (a double click's release: nothing) */
        m->tbtn_dbl = (uint8_t)(m->tbtn_dbl & ~bit);
        return 1;
    }
    if (now - m->tbtn_ms[t] >= RFM_MOVE_TAP_MS || m->menu_what != MENU_NONE || (m->tbtn_used & bit)) {
        m->tbtn_used = (uint8_t)(m->tbtn_used & ~bit);
        m->tbtn_up[t] = 0;                              /* (a hold, or its menu used: nothing) */
        return 1;
    }
    track_tap(m, t);
    m->tbtn_up[t] = now ? now : 1;
    return 1;
}

/* (0.4.3, 0.8.0) Back, in every mode and the projects view: a click (let go within RFM_MOVE_TAP_MS, nothing else
 * pressed meanwhile, no Track held) closes the latest layer (a menu, Loop's latch, Shift's lock, the clipboard's
 * marks; in the view: Capture, states, the description, the selection, the catalog, FX setup's module list, then
 * out of the view), and with nothing to close flips between the slot's layout and the Sequencer (proposal 2) */
static void back_click(rfm_move_t *m, uint32_t now)
{
    if (rfmv_st_back(m))                                /* (0.13.0) States: its marks, then the layer (where it came
                                                         * from shows again) */
        return;
    if (m->seq.projects && m->seq.smenu) {              /* (0.9.0) a Shift menu over the Mixer / Routing: it closes */
        rfmv_seq_button(m, MR_CC_BACK, 1, now);
        return;
    }
    if (m->seq.projects) {                              /* (the view's own Back: one level up, then out) */
        mr_event_t e;
        memset(&e, 0, sizeof e);
        e.type = MR_EV_BUTTON, e.index = MR_CC_BACK, e.value = 127;
        rfmv_view_input(m, &e, now);
        e.value = 0;
        if (m->seq.projects)
            rfmv_view_input(m, &e, now);
        if (!m->seq.projects) {                         /* (flipped out: where it is now, named) */
            name_layout(m);
        }
        return;
    }
    if (rfmv_smp_back(m))                               /* (0.15.0) the sampler: its popup or tab choice, then the app */
        return;
    if (rfmv_seq_layer_open(m))
        rfmv_seq_button(m, MR_CC_BACK, 1, now);
    else if (rfmv_fwp_locked(m)) {                      /* (0.14.0, the owner's own exception to "Back never changes
                                                         * the mode or the view") the FM-1 page's lock: Knobs, the jog
                                                         * its pages */
        rfmv_fwp_unlock(m);
        rfmv_pop(m, POP_FWLV, 0);
    }
    /* (0.9.0, the owner's: nothing open, nothing: no flip; 0.12.0: "Back never changes the mode or the view": session
     * view stays too, Menu leaves it) */
}
static int back_input(rfm_move_t *m, const mr_event_t *ev, uint32_t now)   /* 1: taken */
{
    if (ev->type == MR_EV_BUTTON && ev->index == MR_CC_BACK) {
        if (ev->value) {
            m->back_down = 1, m->back_used = m->track_held != 0, m->back_ms = now, m->back_clk = rfmv_now_ms(m);
        } else if (m->back_down) {
            m->back_down = 0;
            if (!m->back_used && now - m->back_ms < RFM_MOVE_TAP_MS)
                back_click(m, now);
        }
        return 1;
    }
    if (m->back_down && ev->value && ev->type != MR_EV_TOUCH && ev->type != MR_EV_PAD_PRESSURE)
        m->back_used = 1;                               /* (anything else pressed meanwhile: no click) */
    return 0;
}

/* (0.8.0, 0.9.0) over the projects view, what the main handlers take (else the view's): a held Track's menu (Track
 * 1..3: the layout menu and the tracks; Track 4: Mixer / Routing and the Shift layer), Shift + a step in the Mixer and
 * Routing, an open Shift menu's knobs and jog, and the releases of what they took */
static int view_takes(const rfm_move_t *m, const mr_event_t *ev)
{
    const int i = ev->index, menu = rfmv_lay_menu(m) || rfmv_t4_menu(m);
    switch (ev->type) {
    case MR_EV_STEP:
        return i >= 0 && i < RFM_MOVE_NSTEPS && (m->step[i].kind != H_NONE || (ev->value && (menu || (m->shift_down && m->fw.fx))));
    case MR_EV_PAD: return i >= 0 && i < RFM_MOVE_NPADS && (ev->value ? menu : m->pad[i].kind == H_MENU);
    case MR_EV_KNOB:
    case MR_EV_JOG:
    case MR_EV_JOG_CLICK:
    case MR_EV_TOUCH: return (menu || m->seq.smenu) && !(ev->type == MR_EV_TOUCH && i == 9);
    case MR_EV_BUTTON: return ev->value && menu && i != MR_CC_SHIFT && i != MR_CC_BACK;
    default: return 0;
    }
}
static void input(rfm_move_t *m, const mr_event_t *ev, uint32_t now_ms)
{
    if (ev->type == MR_EV_TOUCH && ev->index == 9) {   /* (0.6.12) the jog's touch, wherever it goes: Jog speech */
        if (ev->value && !m->jog_touch)
            m->say_jogstart = 1;
        m->jog_touch = (uint8_t)(ev->value != 0);
    }
    if (rfmv_global_input(m, ev))                       /* (0.6.2) Global settings owns the controls while open */
        return;
    if (rfmv_tk_route(m, ev))                           /* (0.7.0) a takeover: its claimed controls, volume + Back */
        return;
    if (m->s.mode == RFM_MOVE_MODE_SEQ && ev->value &&
        (ev->type == MR_EV_BUTTON || ev->type == MR_EV_PAD || ev->type == MR_EV_STEP || ev->type == MR_EV_JOG_CLICK) &&
        !(ev->type == MR_EV_BUTTON && (ev->index == MR_CC_LOOP || ev->index == MR_CC_MUTE)))
        m->seq.mods_other |= (uint8_t)((m->btn[MR_CC_LOOP].kind != H_NONE ? SEQ_M_LOOP : 0) |
                                      (m->btn[MR_CC_MUTE].kind != H_NONE ? SEQ_M_MUTE : 0));   /* (0.12.0, a2: Loop or
                                                         * Mute held and something else pressed: not a click) */
    if (m->fwg && ev->value && ev->type != MR_EV_TOUCH && ev->type != MR_EV_PAD_PRESSURE &&
        !(ev->type == MR_EV_BUTTON && (ev->index == MR_CC_DELETE || ev->index == MR_CC_COPY) &&
          m->btn[ev->index].kind == H_GATE))
        m->fwg_other = (uint8_t)(m->fwg_other | m->fwg);   /* (0.14.0: Knobs+Keys' Delete / Copy used: no firmware tap) */
    if (back_input(m, ev, now_ms))
        return;
    if (track_input(m, ev, now_ms))                   /* (0.8.0) the Track buttons, in every mode and the view */
        return;
    if (rfmv_st_input(m, ev))                           /* (0.13.0) the States layer: the pads, steps, knobs, jog */
        return;
    if (m->seq.tk_pend >= 0 && ev->value && ev->type != MR_EV_TOUCH && ev->type != MR_EV_PAD_PRESSURE &&
        ev->type != MR_EV_STEP && !(ev->type == MR_EV_PAD && rfmv_sess_on(m)))
        rfmv_seq_tk_flush(m);                           /* (0.11.0) a held Track's track clicked, then something else
                                                         * pressed: the click acts first (a layout goes to that track) */
    if (m->track_held && ev->value && ev->type != MR_EV_TOUCH && ev->type != MR_EV_PAD_PRESSURE &&
        m->menu_what == MENU_NONE)
        m->tbtn_used = m->track_held;                   /* (anything pressed with a Track held: its release no tap) */
    if (rfmv_smp_input(m, ev))                          /* (0.15.0) the sampler app: Sample (Track held + Sample), the
                                                         * jog while it shows */
        return;
    if (m->seq.projects && (m->pop == POP_MODE || m->pop == POP_T4) && ev->value && ev->type != MR_EV_TOUCH &&
        ev->type != MR_EV_PAD_PRESSURE)
        m->pop = POP_NONE;                              /* (0.4.5: the flip's name goes with the first press there) */
    if (ev->type == MR_EV_BUTTON && ev->index == MR_CC_SHIFT && m->seq.projects)
        m->shift_down = (uint8_t)(ev->value != 0);      /* (0.9.0: Shift + step in the Mixer and Routing) */
    if (m->seq.projects && !view_takes(m, ev)) {                              /* the projects view: a mode of its own (a slot's browser, the
                                                         * projects, FX setup's Mixer and Routing) */
        rfmv_view_input(m, ev, now_ms);
        return;
    }
    switch (ev->type) {
    case MR_EV_PAD:
        if (ev->index >= 0 && ev->index < RFM_MOVE_NPADS)
            on_pad(m, ev->index, clampi(ev->value, 0, 127), now_ms);
        break;
    case MR_EV_PAD_PRESSURE:
        if (ev->index >= 0 && ev->index < RFM_MOVE_NPADS)
            on_pad_pressure(m, ev->index, ev->value);
        break;
    case MR_EV_STEP:
        if (ev->index >= 0 && ev->index < RFM_MOVE_NSTEPS)
            on_step(m, ev->index, ev->value != 0, now_ms);
        break;
    case MR_EV_BUTTON: on_button(m, ev->index, ev->value != 0, now_ms); break;
    case MR_EV_KNOB: on_knob(m, ev->index, ev->value, now_ms); break;
    case MR_EV_JOG: on_jog(m, ev->value, now_ms); break;
    case MR_EV_JOG_CLICK: on_jog_click(m, ev->value != 0); break;   /* (0.8.0: the FM-1 layout's jog; FILL is Shift +
                                                         * Loop held, the owner's) */
    case MR_EV_TOUCH:
        if (ev->index >= 0 && ev->index <= 9)
            on_touch(m, ev->index, ev->value != 0);
        break;
    default: break;                                     /* jacks, MIDI .. */
    }
}
void rfm_move_input(rfm_move_t *m, const mr_event_t *ev, uint32_t now_ms)
{
    m->in_ms = now_ms;                                  /* (0.9.0: the input's clock, for double taps) */
    m->say_stepev = ev->type == MR_EV_STEP;
    if (ev->type == MR_EV_BUTTON && ev->index == MR_CC_BACK && ev->value)
        m->say_bdown = m->say_n;                        /* (0.6.3) Back: nothing said from its press to its release: */
    m->say_back = m->speech && ev->type == MR_EV_BUTTON && ev->index == MR_CC_BACK && !ev->value &&
                  m->say_n == m->say_bdown;             /* .. where it left you is said */
    input(m, ev, now_ms);
    rfmv_fwp_ctx(m);                                    /* (0.14.0: the FM-1 page's lock ends with its place) */
    fwp_sync(m);                                        /* (0.12.x: level 2's keys and buttons let go once it is off) */
    rfmv_ep_sync(m, 1);                                 /* (0.14.0: the ENGINE page: there or not, its popups, the watch) */
    rfmv_slot_push(m);                                  /* (0.8.0: the active slot's record kept current) */
    rfmv_pfw_watch(m);                                  /* (0.13.0: a firmware setting changed: the project edited) */
    rfmv_tk_sync(m);                                    /* (0.7.0: the takeover shown or gone) */
    rfmv_say_check(m);                                  /* (0.6.0: speech, when on) */
}

/* ================================================================ LEDs === */
void rfm_move_panel(rfm_move_t *m, const uint8_t levels[RFM_MOVE_NLEVELS], uint8_t play_green)
{
    memcpy(m->levels, levels, RFM_MOVE_NLEVELS);
    m->play_green = play_green;
}

/* the FM-1's LED level 0..255 as a brightness 0..256 (panel.js ledAlpha: the 5 levels' curve) */
static int alpha(int level)
{
    static const int LX[5] = { 0, 12, 40, 96, 255 }, LY[5] = { 0, 51, 97, 159, 256 };
    int i;
    level = clampi(level, 0, 255);
    for (i = 1; i < 5; i++)
        if (level <= LX[i])
            return LY[i - 1] + (LY[i] - LY[i - 1]) * (level - LX[i - 1]) / (LX[i] - LX[i - 1]);
    return 256;
}
static int level_of(const rfm_move_t *m, int id)
{
    const int i = id >= 0 && id < 41 ? m->led_of[id] : -1;
    return i >= 0 ? m->levels[i] : 0;
}
int rfmv_level(const rfm_move_t *m, int id) { return level_of(m, id); }

const rgb_t MODE_RGB[4] = { { 0xc4, 0x3a, 0xff }, { 0xc8, 0xff, 0x2a }, { 0x1a, 0xe4, 0xff }, { 0xff, 0x2a, 0x2a } };
const rgb_t BANK_RGB[4] = { { 0x1a, 0xe4, 0xff }, { 0xff, 0xb0, 0x2a }, { 0xc4, 0x3a, 0xff }, { 0xc8, 0xff, 0x2a } };
const rgb_t WHITE = { 255, 255, 255 }, GREEN = { 0, 255, 0 }, RED = { 255, 0, 0 };
const rgb_t OFF = { 0, 0, 0 };
static const rgb_t MENU_KNOB = { 255, 72, 0 };         /* a menu's knobs (the owner's): deep orange, no mode's or bank's */

/* a held Track's (its menu or layer shown) or an open menu's knobs: a bit each; -1 none (the knobs as they are) */
int rfmv_menu_knobs(const rfm_move_t *m)
{
    if (m->seq.smenu && !rfmv_seq_held_takes(m) && !(rfmv_lay_menu(m) && rfmv_hold_shown(m, rfmv_lay_menu(m) - 1)) &&
        !(rfmv_t4_menu(m) && rfmv_hold_shown(m, 3) && !m->seq.t4_opened))
        return rfmv_seq_menu_knobs(m);                  /* (0.12.0, a6: a Shift menu owns its knobs over every view,
                                                         * the Mixer and Routing too; a held Track's menu over it, a
                                                         * held step, Track 4 held (unless it opened the menu) take
                                                         * them) */
    if (m->seq.projects)                              /* (Track 4: knob 1 the colour; the firmware tracks and FX
                                                         * setup light their own) */
        return m->fw.fx || m->fw.track < 3 ? -1 : rfmv_proj_knob(m) ? 0x01 : 0;   /* (0.12.0, a12: knob 1 only
                                                         * while a project's pad is held) */
    {                                                   /* (0.8.0) a firmware Track's menu: a note layout's CC numbers
                                                         * (all 8), the FM-1 layout none; the lock menu: the track's CC
                                                         * numbers; Track 4's: none */
        int t;
        for (t = 0; t < 4; t++)
            if (rfmv_hold_shown(m, t)) {
                if (t == 3)
                    return 0;
                if (m->s.mode == RFM_MOVE_MODE_SEQ)
                    return m->seq.view ? 0 : 0xFF;      /* (0.11.0: session view: the routing only, no knobs) */
                return rfmv_slot_lay(m, t) >= RFM_MOVE_LAY_SCALE ? 0xFF : 0;
            }
    }
    if (rfmv_seq_cs_knobs(m) >= 0)
        return rfmv_seq_cs_knobs(m);                    /* (0.12.0) a select step held: Launch, After */
    if (m->seq.smenu && !rfmv_seq_held_takes(m))       /* (0.12.0, l3: a held step has the knobs over it) */
        return rfmv_seq_menu_knobs(m);
    return -1;
}
static const rgb_t PINK = { 0xff, 0x7a, 0xa8 };
#define FLOOR 24                                        /* a mapped key's glow while its FM-1 LED is off (/256) */
#define DIM 32                                          /* an unselected choice (/256) */

rgb_t sc(rgb_t c, int f)
{
    rgb_t o;
    f = clampi(f, 0, 256);
    o.r = (uint8_t)(c.r * f / 256), o.g = (uint8_t)(c.g * f / 256), o.b = (uint8_t)(c.b * f / 256);
    return o;
}
rgb_t add(rgb_t a, rgb_t b)
{
    rgb_t o;
    o.r = (uint8_t)clampi(a.r + b.r, 0, 255), o.g = (uint8_t)clampi(a.g + b.g, 0, 255);
    o.b = (uint8_t)clampi(a.b + b.b, 0, 255);
    return o;
}

/* the LED list: index -> (kind, number) */
static const uint8_t WHITE_CC[13] = { MR_CC_SHIFT, MR_CC_MENU, MR_CC_BACK, MR_CC_CAPTURE, MR_CC_DOWN, MR_CC_UP,
                                      MR_CC_UNDO, MR_CC_LOOP, MR_CC_COPY, MR_CC_LEFT, MR_CC_RIGHT, MR_CC_MUTE,
                                      MR_CC_DELETE };

static void led_id(int i, rfm_move_led_t *l)
{
    memset(l, 0, sizeof *l);
    l->rgb = 1;
    if (i < L_STEP0)
        l->cc = 0, l->number = (uint8_t)(MR_NOTE_PAD0 + i);
    else if (i < L_PLAY)
        l->cc = 0, l->number = (uint8_t)(MR_NOTE_STEP0 + i - L_STEP0);
    else if (i == L_PLAY)
        l->cc = 1, l->number = MR_CC_PLAY;
    else if (i == L_REC)
        l->cc = 1, l->number = MR_CC_REC;
    else if (i == L_SAMPLE)
        l->cc = 1, l->number = MR_CC_SAMPLE;
    else if (i < L_RING0)
        l->cc = 1, l->number = (uint8_t)(MR_CC_TRACK1 - (i - L_TRACK0));
    else if (i < L_WHITE0)
        l->cc = 1, l->number = (uint8_t)(MR_CC_KNOB1 + i - L_RING0);
    else if (i < L_ICON0)
        l->rgb = 0, l->cc = 1, l->number = WHITE_CC[i - L_WHITE0];
    else                                                /* the white icons under the steps */
        l->rgb = 0, l->cc = 1, l->number = (uint8_t)(MR_CC_STEP_ICON0 + i - L_ICON0);
}

/* a MIDI mode's pad colour */
rgb_t midi_pad(const rfm_move_t *m, int p, rgb_t mode)
{
    if (rfm_move_pad_note(&m->s, p) >= 0 && m->pad_down[p])
        return WHITE;
    return rfmv_layout_pad(m, p, mode);
}
rgb_t rfmv_layout_pad(const rfm_move_t *m, int p, rgb_t mode)
{
    const int n = rfm_move_pad_note(&m->s, p);
    int rel, i;
    const uint8_t *scl = SCALES[m->s.scale < RFM_MOVE_NSCALES ? m->s.scale : 0];
    if (n < 0)
        return OFF;
    rel = ((n - m->s.root) % 12 + 12) % 12;
    if (rel == 0)
        return mode;
    if (m->s.layout == RFM_MOVE_LAYOUT_DRUM)
        return sc(WHITE, 90);
    for (i = 0; i < scl[0]; i++)
        if (scl[1 + i] == rel)
            return sc(WHITE, 90);
    return sc(WHITE, 12);
}

/* (0.8.0) a firmware Track's layout menu (the board's colours): the top row's layouts (FM-1 white, the takeover
 * purple, the note layouts lime; the slot's own full, the others 30 %), latch amber, the 24 scales cyan (the slot's
 * white); the steps the channel (the slot's white) */
static void lay_menu_leds(const rfm_move_t *m, int t, rgb_t c[L_WHITE0])
{
    static const rgb_t LAYC[RFM_MOVE_LAY_N] = { { 255, 255, 255 }, { 196, 58, 255 }, { 200, 255, 42 }, { 200, 255, 42 },
                                                { 200, 255, 42 }, { 200, 255, 42 } };
    const int lay = rfmv_slot_lay(m, t);
    int i;
    if (rfmv_sess_on(m)) {                              /* (0.11.0) session view: the pads are the routing */
        rfmv_seq_route_leds(m, t, c);
        return;
    }
    for (i = 0; i < RFM_MOVE_NPADS; i++) {
        rgb_t v = OFF;
        if (i >= 24 && i < 24 + RFM_MOVE_LAY_N) {
            const int l = i - 24;
            if (l != RFM_MOVE_LAY_TK || m->tk[t].claims)
                v = sc(LAYC[l], l == lay ? 256 : 77);
        } else if (i == 31)
            v = sc((rgb_t){ 255, 170, 0 }, m->latch ? 256 : 77);
        else if (i == 30 && rfmv_st_pad7(m, t))         /* (0.13.0) pad 7: States, in the slot's colour */
            v = RFMV_SLOT_RGB[t];
        else if (i < 24)
            v = (m->s.mode == RFM_MOVE_MODE_SEQ ? m->s.scale : m->seq.scale) == rfmv_scale_of_pad(i) ? WHITE
              : sc((rgb_t){ 26, 228, 255 }, 90);   /* (0.9.0: the key the tracks use) */
        c[L_PAD0 + i] = v;
    }
    for (i = 0; i < RFM_MOVE_NSTEPS; i++)               /* (0.9.0) the steps: the 16 tracks (the lock menu) */
        c[L_STEP0 + i] = rfmv_lock_rgb(m, t, i);
    for (i = 0; i < RFM_MOVE_NKNOBS; i++)               /* (the sequencer: the track's CC numbers) */
        c[L_RING0 + i] = m->s.mode == RFM_MOVE_MODE_SEQ ? sc(WHITE, m->touched == i ? 256 : 128) : OFF;
}
/* (0.8.0) Track 4's menu: pad 1 the Mixer (red), pad 2 Routing (orange), the page Track 4's tap opens full; the steps
 * with a Shift shortcut dim white (rfmv_shift_icons lights their icons) */
static void t4_menu_leds(const rfm_move_t *m, rgb_t c[L_WHITE0])
{
    uint8_t ic[16];
    int i;
    for (i = 0; i < L_TRACK0; i++)
        c[i] = OFF;
    if (rfmv_fx_ok(m)) {
        c[L_PAD0 + 24] = sc((rgb_t){ 255, 42, 42 }, rfmv_t4_page(m) == 0 ? 256 : 77);
        c[L_PAD0 + 25] = sc((rgb_t){ 255, 120, 0 }, rfmv_t4_page(m) == 1 ? 256 : 77);
    }
    rfmv_shift_icons(m, ic);
    for (i = 0; i < RFM_MOVE_NSTEPS; i++)               /* (0.12.0: a pressed step white, R1) */
        c[L_STEP0 + i] = m->step[i].kind != H_NONE && ic[i] ? WHITE : ic[i] ? sc(WHITE, ic[i] > 64 ? 256 : 90) : OFF;
    for (i = 0; i < RFM_MOVE_NKNOBS; i++)
        c[L_RING0 + i] = OFF;
}
/* (0.12.0, a8) Track 4's hold layer: the buttons it has no job for dark; Capture, Undo, Loop (Shift + them) lit */
static void t4_menu_white(uint8_t wv[13])
{
    static const uint8_t DEAD[] = { W_SHIFT, W_MENU, W_DOWN, W_UP, W_COPY, W_LEFT, W_RIGHT, W_MUTE, W_DELETE };
    int i;
    for (i = 0; i < (int)sizeof DEAD; i++)
        wv[DEAD[i]] = 0;
    wv[W_CAPTURE] = wv[W_CAPTURE] > 60 ? wv[W_CAPTURE] : 60;
    wv[W_UNDO] = wv[W_UNDO] > 60 ? wv[W_UNDO] : 60;
    wv[W_LOOP] = wv[W_LOOP] > 60 ? wv[W_LOOP] : 60;
}

/* (0.12.x) the FM-1 page: its knobs as the FM-1 layout lights them (R4: a knob with a job lit, the others dark; an
 * empty slot: all dark); level 2: the steps and the buttons it takes as the FM-1 layout (the firmware's LEDs) */
static void fwp_leds(const rfm_move_t *m, rgb_t c[L_WHITE0], uint8_t wv[13])
{
    const rgb_t fm = MODE_RGB[RFM_MOVE_MODE_FM1];
    int i;
    if (!rfmv_fwp_page(m))
        return;
    if (!m->seq.smenu && !m->seq.step_held)
        for (i = 0; i < RFM_MOVE_NKNOBS; i++)
            c[L_RING0 + i] = rfmv_fwp_live(m) && KNOB_ENC[i] >= 0 && !enc_nojob(m, KNOB_ENC[i])
                           ? (rfmv_fwj_lit(m) && i == m->jog_enc % 3 ? WHITE : sc(fm, m->touched == i ? 256 : 64)) : OFF;
                                                        /* (0.14.0: the jog's target white while the jog is on it) */
    if (!rfmv_fwp_full(m))
        return;
    for (i = 0; i < RFM_MOVE_NSTEPS; i++) {
        const int lv = alpha(level_of(m, 14 + STEP_KEY[i]));
        c[L_STEP0 + i] = m->step[i].kind == H_KEY ? fm : sc(WHITE, lv > FLOOR ? lv : FLOOR);
    }
    c[L_PLAY] = add(sc(WHITE, alpha(level_of(m, RFM_MOVE_B_PLAY))), sc(GREEN, alpha(m->play_green)));
    c[L_REC] = sc(RED, alpha(level_of(m, RFM_MOVE_B_REC)));
    c[L_SAMPLE] = sc(WHITE, alpha(level_of(m, RFM_MOVE_B_SEL)));
    for (i = 0; i < 13; i++)
        if (m->btn[WHITE_CC[i]].kind == H_GATE)
            wv[i] = 127;                                /* (0.14.0: Delete / Copy held: the sequencer's, full) */
        else if (fwp_btn(WHITE_CC[i]))
            wv[i] = (uint8_t)clampi(alpha(level_of(m, rfm_move_button_of(WHITE_CC[i]))) * 127 / 256, 6, 127);
}

static void wanted(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3])
{
    const rgb_t mode = MODE_RGB[m->s.mode & 3];
    const int fm1 = !midi_mode(m), lm = rfmv_lay_menu(m) && rfmv_hold_shown(m, rfmv_lay_menu(m) - 1);
    const int t4m = rfmv_t4_menu(m) && rfmv_hold_shown(m, 3);   /* (0.12.0, a8: its layer whole, with a menu it opened
                                                         * on the knobs) */
    rgb_t c[L_WHITE0];
    uint8_t wv[13];
    int i;
    for (i = 0; i < RFM_MOVE_NPADS; i++) {
        if (fm1) {
            const int k = PAD_KEY[i];
            const int lv = k >= 0 ? alpha(level_of(m, 14 + k)) : 0;
            c[i] = k < 0 ? OFF : m->pad_down[i] ? mode : sc((0x54A >> ((k + 5) % 12)) & 1 ? PINK : WHITE, lv > FLOOR ? lv : FLOOR);
        } else
            c[i] = midi_pad(m, i, mode);
    }
    for (i = 0; i < RFM_MOVE_NSTEPS; i++) {
        rgb_t s = OFF;
        if (fm1) {
            const int lv = alpha(level_of(m, 14 + STEP_KEY[i]));
            s = m->step[i].kind == H_KEY ? mode : sc(WHITE, lv > FLOOR ? lv : FLOOR);
        }
        c[L_STEP0 + i] = s;
    }
    if (fm1) {
        c[L_PLAY] = add(sc(WHITE, alpha(level_of(m, RFM_MOVE_B_PLAY))), sc(GREEN, alpha(m->play_green)));
        c[L_REC] = sc(RED, alpha(level_of(m, RFM_MOVE_B_REC)));
        c[L_SAMPLE] = sc(WHITE, alpha(level_of(m, RFM_MOVE_B_SEL)));
    } else {                                            /* (0.8.0) a note layout: Play the sequencer's transport */
        c[L_PLAY] = m->seq.playing ? GREEN : sc(WHITE, 40);
        c[L_REC] = OFF, c[L_SAMPLE] = OFF;
    }
    rfmv_track_leds(m, c);                              /* (0.8.0: the slots' colours and Track 4 everywhere) */
    for (i = 0; i < RFM_MOVE_NKNOBS; i++) {
        rgb_t r = OFF;
        if (!fm1) {                                     /* (0.8.0) a note layout: the Control CCs, by their values */
            const rgb_t bc = { 0x1a, 0xe4, 0xff };
            r = m->touched == i ? bc : sc(bc, 16 + m->slot_ccv[act_slot(m)][i] * 240 / 127);
        } else if (KNOB_ENC[i] >= 0 && !enc_nojob(m, KNOB_ENC[i]))
            r = sc(mode, m->touched == i ? 256 : 64);   /* (0.12.0, a12: a knob the firmware gives no job ("" its
                                                         * label): dark) */
        c[L_RING0 + i] = r;
    }
    for (i = 0; i < 13; i++) {
        const int cc = WHITE_CC[i], b = rfm_move_button_of(cc);
        int v = 0;
        if (!fm1)                                       /* (0.8.0) a note layout: Up / Down the octave, Back, Shift */
            v = cc == MR_CC_UP ? (m->s.octave > 0 ? 127 : 24) : cc == MR_CC_DOWN ? (m->s.octave < 0 ? 127 : 24)
              : cc == MR_CC_BACK ? 40 : cc == MR_CC_SHIFT ? (m->shift_down ? 127 : 24) : 0;
        else if (cc == MR_CC_DOWN)
            v = rfmv_fw_loaded(m) > 1 ? 127 : 0;       /* (0.4.4: lit with another firmware to cycle to) */
        else if (cc == MR_CC_UP)
            v = clampi(alpha(level_of(m, b)) * 127 / 256, 6, 127);
        else if (cc == MR_CC_BACK)
            v = 40;                                     /* (0.8.0: Back flips to the Sequencer) */
        else if (b >= 0)
            v = clampi(alpha(level_of(m, b)) * 127 / 256, 6, 127);
        wv[i] = (uint8_t)v;
    }
    if (m->seq.projects)                                /* the projects view: its own LEDs (the firmwares or the
                                                         * projects; the rest off) */
        rfmv_fw_leds(m, c, wv);
    else if (m->s.mode == RFM_MOVE_MODE_SEQ) {          /* Sequencer mode takes over all but the Track LEDs */
        rfmv_seq_leds(m, c, wv);
        fwp_leds(m, c, wv);                             /* (0.12.x) the FM-1 page's */
        rfmv_ep_leds(m, c);                             /* (0.14.0) the ENGINE page's */
        if (rfmv_seq_tflash_on(m))
            rfmv_seq_tflash_leds(m, c);                 /* (0.12.x) a Track's tap: the tracks a moment */
    }
    if (lm)
        lay_menu_leds(m, rfmv_lay_menu(m) - 1, c);
    else if (t4m)
        t4_menu_leds(m, c), t4_menu_white(wv);
    {
        const int mk = lm || (t4m && !(m->seq.smenu && m->seq.t4_opened)) ? -1 : rfmv_menu_knobs(m);   /* a menu or a
                                                         * held Track: only its knobs, its colour */
        if (mk >= 0)
            for (i = 0; i < RFM_MOVE_NKNOBS; i++)
                c[L_RING0 + i] = !(mk >> i & 1) ? OFF : sc(MENU_KNOB, m->touched == i ? 256 : 110);
    }
    if (m->seq.fill) {                                  /* (0.12.0, a10) FILL: the pads' wave (a pressed one white),
                                                         * Loop's light blinking fast, in every layout */
        const rgb_t fc = rfmv_seq_fill_rgb(m);
        for (i = 0; i < RFM_MOVE_NPADS; i++)
            if (!m->pad_down[i] && rfmv_seq_fill_on(m, i))
                c[L_PAD0 + i] = fc;
        wv[W_LOOP] = (uint8_t)((rfmv_now_ms(m) - m->seq.fill_ms) / 100 % 2 ? 0 : 127);
    }
    rfmv_track_leds(m, c);
    for (i = 0; i < L_WHITE0; i++)
        w[i][0] = c[i].r, w[i][1] = c[i].g, w[i][2] = c[i].b;
    for (i = 0; i < 13; i++)
        w[L_WHITE0 + i][0] = wv[i], w[L_WHITE0 + i][1] = 0, w[L_WHITE0 + i][2] = 0;
    {
        uint8_t ic[16];
        rfmv_shift_icons(m, ic);
        for (i = 0; i < 16; i++)
            w[L_ICON0 + i][0] = ic[i], w[L_ICON0 + i][1] = 0, w[L_ICON0 + i][2] = 0;
    }
    rfmv_smp_leds(m, w);                                /* (0.15.0) the sampler: Sample's light */
    if (m->back_down)                                   /* (0.12.0, a13) Back: full at its press; else lit only with a
                                                         * layer for its click to close */
        w[L_WHITE0 + W_BACK][0] = 127;
    else if (!m->seq.projects)
        w[L_WHITE0 + W_BACK][0] = rfmv_back_has(m) ? 80 : 0;
    if (rfmv_back_keys(m) && (m->back_used || rfmv_now_ms(m) - m->back_clk >= RFM_MOVE_TAP_MS)) {
        /* (0.12.x) Back held (past a click's time, or something pressed with it): its shortcuts lit, a pressed one
         * full, the other buttons dark (dead); none of this on a click */
        for (i = 0; i < 13; i++) {
            const int cc = WHITE_CC[i], b = back_bit(cc);
            if (cc != MR_CC_BACK)
                w[L_WHITE0 + i][0] = (uint8_t)(!rfmv_seq_back_lit(m, cc) ? 0
                                               : (b >= 0 && (m->back_cut >> b & 1)) ? 127 : 100);
        }
        {
            const int pb = back_bit(MR_CC_PLAY), pl = (m->back_cut >> pb & 1) ? 256 : 160;
            const rgb_t pc = sc(m->seq.playing ? GREEN : WHITE, pl);
            w[L_PLAY][0] = pc.r, w[L_PLAY][1] = pc.g, w[L_PLAY][2] = pc.b;
        }
        memset(w[L_REC], 0, 3), memset(w[L_SAMPLE], 0, 3);
    }
    rfmv_st_leds(m, w);                                 /* (0.13.0) the States layer: its pads, steps, buttons */
    if (!lm && !t4m)
        rfmv_tk_leds(m, w);                             /* (0.7.0) a takeover: its claimed LEDs (0.8.0: not under a held
                                                         * Track's menu) */
}

int rfm_move_leds(rfm_move_t *m, rfm_move_led_t *out, int max)
{
    uint8_t w[RFM_MOVE_NLEDS][3];
    int i, n = 0;
    wanted(m, w);
    for (i = 0; i < RFM_MOVE_NLEDS && n < max; i++) {
        if (m->sent_ok[i] && !memcmp(m->sent[i], w[i], 3))
            continue;
        memcpy(m->sent[i], w[i], 3);
        m->sent_ok[i] = 1;
        led_id(i, &out[n]);
        out[n].r = w[i][0], out[n].g = w[i][1], out[n].b = w[i][2];
        n++;
    }
    return n;
}

void rfm_move_leds_invalidate(rfm_move_t *m) { memset(m->sent_ok, 0, sizeof m->sent_ok); }
