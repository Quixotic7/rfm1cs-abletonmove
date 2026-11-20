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
/* knobs 1..8 in FM-1 mode: 1 PRESETS, 2 ALGO, 3 4 none, 5..8 K1..K4 */
static const int8_t KNOB_ENC[RFM_MOVE_NKNOBS] = {
    RFM_MOVE_E_PRESETS, RFM_MOVE_E_ALGO, -1, -1, RFM_MOVE_E_K1, RFM_MOVE_E_K2, RFM_MOVE_E_K3, RFM_MOVE_E_K4,
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
static const char *const MODE_NAMES[4] = { "FM-1", "MIDI", "CONTROL", "SEQ" };
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
const char *rfm_move_mode_name(int mode) { return mode >= 0 && mode < 4 ? MODE_NAMES[mode] : "?"; }
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
    for (b = 0; b < RFM_MOVE_NBANKS; b++)
        for (k = 0; k < RFM_MOVE_NKNOBS; k++)
            s->cc[b][k] = (uint8_t)(DEFAULT_CC[b] + k);
    for (p = 0; p < RFM_MOVE_NPADS; p++)   /* bottom-right = 36, leftwards, then the next row up from the right */
        s->drum[p] = (uint8_t)(RFM_MOVE_DRUM_BASE + (p / 8) * 8 + (7 - p % 8));
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
    *p++ = s->pop_half;                                 /* (version 5) */
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
          (b[3] == 4 && (b[4] | b[5] << 8) == RFM_MOVE_SETTINGS_BYTES - 1 && n >= RFM_MOVE_SETTINGS_BYTES - 1) ||
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
        s->pop_half = p[9] & 1;
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
void changed(rfm_move_t *m) { emit(m, RFM_MOVE_ACT_SETTINGS, 0, 0, 0, 0); }

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
    m->edit_pad = -1;
    rfmv_seq_ui_release(m);
}

void rfm_move_release_all(rfm_move_t *m)
{
    release_holds(m);
    rfmv_latch_drop_all(m);                             /* (0.5.3) */
    rfmv_seq_stop(m);                                   /* the link is gone: nothing may keep sounding */
    m->track_held = 0;
    m->back_down = m->back_used = m->back_trk = 0;
    m->touched = -1;
    m->pop = POP_NONE;
}

void rfm_move_set_mode(rfm_move_t *m, int mode)
{
    if (mode < 0 || mode > 3 || mode == m->s.mode)
        return;
    release_holds(m);
    rfmv_latch_drop_all(m);                             /* (0.5.3: a mode change lets the latched notes go) */
    m->mmenu = 0;
    if (m->s.mode == RFM_MOVE_MODE_SEQ)
        rfmv_seq_leave(m);                              /* (the tracks' own layout and CC numbers out, MIDI mode's back) */
    m->s.mode = (uint8_t)mode;
    if (mode == RFM_MOVE_MODE_SEQ)
        rfmv_seq_enter(m);
    m->pop = POP_NONE;
    changed(m);
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
        rfmv_say_readout(m, control);                   /* (0.6.7: its label then, for speech) */
        if (knob_up == control)
            return;                                     /* (its popup is up: drawn in place) */
    }
    if (!rfmv_half(m) || m->seq.projects)
        return;
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
    if (m->s.mode != RFM_MOVE_MODE_FM1 || m->seq.projects)
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
    m->touched = -1;
    rfm_move_set_keymap(m, NULL);
    rfmv_fw_init(m);
    rfmv_seq_init(m);
    if (m->s.mode == RFM_MOVE_MODE_SEQ)
        rfmv_seq_enter(m);
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
    return rfmv_track_held(m, t) && (m->menu_what != MENU_NONE || rfmv_now_ms(m) - m->tbtn_clk[t] >= RFM_MOVE_TAP_MS);
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
    return m->s.mode == RFM_MOVE_MODE_FM1 || (m->s.mode == RFM_MOVE_MODE_MIDI && !m->mmenu && m->s.midi_view == RFM_MOVE_MV_NOTES);
}
int rfmv_seq_trk_held(const rfm_move_t *m) { return rfmv_track_held(m, 3); }   /* (any mode) */
#define track_held(m, t) rfmv_track_held(m, t)
static int cc_mode(const rfm_move_t *m) { return m->s.mode == RFM_MOVE_MODE_CONTROL || m->s.mode == RFM_MOVE_MODE_SEQ; }

/* outside Sequencer mode: a sequencer menu (opened with Track 1 held, the Shift layer) is open: it takes what it
 * needs */
static int fm1_seq_layer(const rfm_move_t *m)
{
    return m->s.mode != RFM_MOVE_MODE_SEQ && (m->seq.smenu || m->seq.projects);
}
/* Track 1 held, in any mode: the Move's Shift layer (the owner's; in FM-1 and MIDI mode Shift is the FM-1's) */
static void fm1_shift(rfm_move_t *m, int is_step, int i, int down, uint32_t now)
{
    rfm_move_seq_t *q = &m->seq;
    const uint8_t had = q->mods & SEQ_M_SHIFT;
    q->mods |= SEQ_M_SHIFT;
    if (is_step)
        rfmv_seq_step(m, i, down, now);
    else
        rfmv_seq_button(m, i, down, now);
    if (!had)
        q->mods &= (uint8_t)~SEQ_M_SHIFT;
    rfmv_menu(m, MENU_SHIFT, 0);                        /* (Track 1 was used: its release is no tap) */
}

static void on_pad(rfm_move_t *m, int p, int vel, uint32_t now)
{
    rfm_move_hold_t *h = &m->pad[p];
    if (!vel) {                                         /* released */
        if (m->pad_down[p] && m->s.mode == RFM_MOVE_MODE_SEQ)
            rfmv_seq_pad(m, p, 0, now);
        if (m->latch && h->kind == H_NOTE && midi_mode(m) && m->s.mode != RFM_MOVE_MODE_SEQ &&
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
    if (fm1_seq_layer(m) && m->seq.projects) {
        h->kind = H_MENU;                               /* (projects mode: its grid. The Shift menus leave the pads to
                                                         * the mode, the owner's) */
        rfmv_seq_pad(m, p, vel, now);
        return;
    }
    if (rfmv_seq_trk_held(m) && !track_held(m, 1)) {   /* (0.5.0, the owner's) Track 4 held: the pads do nothing (the
                                                         * layout and scale are Track 2's) */
        h->kind = H_MENU;
        return;
    }
    if (track_held(m, 1) || (m->mmenu && m->s.mode == RFM_MOVE_MODE_MIDI)) {   /* Track 2 held, MIDI mode's Keys &
                                                         * Scales: the layout, the scale */
        h->kind = H_MENU;
        if (p == 31 && track_held(m, 1)) {              /* (0.5.3) the top row's pad 8: latch, named */
            latch_set(m, !m->latch);
            rfmv_menu(m, MENU_LAYOUT, m->s.layout);     /* (used: Track 2's release changes no mode) */
            rfmv_pop(m, POP_MODE, m->latch ? 31 : 30);
            return;
        }
        rfmv_pick_layout(m, p);
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ) {
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
    if (!down) {
        if (h->kind == H_SEQ)                           /* (Sequencer mode's step: its release) */
            rfmv_seq_step(m, s, 0, now);
        let_go(m, h);
        return;
    }
    if (h->kind != H_NONE)
        return;
    if (track_held(m, 0)) {                             /* Track 1 held (any mode): Shift + step, the Move's shortcuts
                                                         * (projects, tempo, metronome .. on the sequencer) */
        h->kind = H_MENU;
        fm1_shift(m, 1, s, 1, now);
        return;
    }
    if (track_held(m, 1)) {                             /* Track 2 held: steps 1..16 = the MIDI channel */
        h->kind = H_MENU;
        rfmv_menu(m, MENU_CHANNEL, s);
        if (m->s.channel != s) {
            m->s.channel = (uint8_t)s;
            changed(m);
        }
    } else if (track_held(m, 3) && m->s.mode != RFM_MOVE_MODE_SEQ) {
        h->kind = H_SEQ;                                /* (Track 4 held, any mode: the steps are the 16 tracks) */
        rfmv_seq_step(m, s, 1, now);
    } else if (!midi_mode(m)) {
        h->kind = H_KEY, h->a = (uint8_t)STEP_KEY[s];
        key_hold(m, h->a, 100);                         /* (steps have no velocity: the FM-1's own default, 100) */
    } else if (m->s.mode == RFM_MOVE_MODE_SEQ) {
        h->kind = H_SEQ;                                /* (a sequencer step press, for its release) */
        rfmv_seq_step(m, s, 1, now);
    }
}

static void on_button(rfm_move_t *m, int cc, int down, uint32_t now)
{
    rfm_move_hold_t *h;
    int b;
    if (cc >= MR_CC_TRACK4 && cc <= MR_CC_TRACK1) {
        const int t = MR_CC_TRACK1 - cc;
        if (down) {                                     /* (the owner's: a press never switches; a hold is the
                                                         * button's menu or layer, in the mode that is on) */
            rfmv_menu(m, MENU_NONE, 0);
            m->track_held = (uint8_t)(m->track_held | 1u << t);
            m->tbtn_ms[t] = now, m->tbtn_clk[t] = rfmv_now_ms(m);
        } else if (m->track_held >> t & 1) {
            m->track_held = (uint8_t)(m->track_held & ~(1u << t));
            if (now - m->tbtn_ms[t] >= RFM_MOVE_TAP_MS || m->menu_what != MENU_NONE) {
                ;                                       /* (a hold, or used: no switch) */
            } else if (m->s.mode != t) {                /* a tap: that mode, its name first (its screen once the name
                                                         * has gone) */
                rfm_move_set_mode(m, t);
                rfmv_pop(m, POP_MODE, t);
            } else {                                    /* a tap in the mode already on: its next screen (Track 2:
                                                         * MIDI mode's), named */
                if (t == RFM_MOVE_MODE_MIDI) {
                    m->s.midi_view = (uint8_t)((m->s.midi_view + 1) % RFM_MOVE_MV_N);
                    changed(m);
                    rfmv_pop(m, POP_MODE, 10 + m->s.midi_view);
                }
            }
        }
        return;
    }
    if (track_held(m, 3) && m->s.mode != RFM_MOVE_MODE_SEQ && (cc == MR_CC_UP || cc == MR_CC_DOWN)) {
        if (down)                                       /* (Track 4 held, any mode: the selected track's rate) */
            rfmv_seq_button(m, cc, 1, now);
        return;
    }
    if (cc == MR_CC_LOOP && !down && m->seq.fill) {    /* (FILL's Loop let go: wherever Shift was) */
        m->seq.fill = 0;
        if (m->s.mode != RFM_MOVE_MODE_SEQ) {
            m->seq.loop_end = 0;
            return;
        }
    }
    if (track_held(m, 0) && (cc == MR_CC_REC || cc == MR_CC_PLAY || cc == MR_CC_CAPTURE || cc == MR_CC_UNDO ||
                             cc == MR_CC_LOOP || cc == MR_CC_SAMPLE)) {
        if (down)                                       /* Track 1 held: Shift + Record (the record settings), Shift +
                                                         * Play, Capture, Undo (redo), Loop (double) on the sequencer */
            fm1_shift(m, 0, cc, 1, now);
        return;
    }
    if (m->s.mode != RFM_MOVE_MODE_SEQ && (cc == MR_CC_BACK || cc == MR_CC_MENU) &&
        (m->seq.smenu || m->seq.projects || m->seq.loop_latch || m->seq.shift_lock)) {
        if (down)                                       /* (a sequencer layer open: Back or Session closes it, not the
                                                         * FM-1's SEQ / HOME) */
            rfmv_seq_button(m, MR_CC_BACK, 1, now);
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
    if (cc == MR_CC_DOWN && m->s.mode == RFM_MOVE_MODE_FM1) {
        if (down)                                       /* (0.4.4) FM-1 mode: the next loaded firmware (no repeat) */
            rfmv_fw_cycle(m);
        return;
    }
    if ((cc == MR_CC_UP && midi_mode(m)) || cc == MR_CC_DOWN) {   /* (FM-1 mode's Up: the FM-1's SEQ, below) */
        const int o = clampi(m->s.octave + (cc == MR_CC_UP ? 1 : -1), RFM_MOVE_OCT_MIN, RFM_MOVE_OCT_MAX);
        if (down && midi_mode(m) && o != m->s.octave) {
            m->s.octave = (int8_t)o;
            changed(m);
        }
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
}

static void on_knob(rfm_move_t *m, int k, int d, uint32_t now)
{
    if (!d)
        return;
    if (track_held(m, 1) && (k == 0 || k == 1)) {      /* Track 2 held: knob 1 the root, knob 2 the scale (lists:
                                                         * one a few ticks) */
        const int n = rfmv_list_ticks(m, k, d);
        if (!n)
            return;
        if (k == 0) {
            m->s.root = (uint8_t)(((m->s.root + n) % 12 + 12) % 12);
            rfmv_menu(m, MENU_ROOT, m->s.root);
        } else {
            m->s.scale = (uint8_t)clampi(m->s.scale + n, 0, RFM_MOVE_NSCALES - 1);
            rfmv_menu(m, MENU_SCALE, m->s.scale);
        }
        changed(m);
        return;
    }
    if (k < RFM_MOVE_NKNOBS && fm1_seq_layer(m) && m->seq.smenu) {
        rfmv_seq_knob(m, k, d, now);                    /* (FM-1 mode, a sequencer menu open: its knobs) */
        return;
    }
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
    if (track_held(m, 2)) {                             /* Track 3 held (any mode): the knob's CC number, this bank */
        const int b = m->s.bank, c = clampi(m->s.cc[b][k] + d, 0, 127);
        rfmv_menu(m, MENU_CC, k);
        if (c != m->s.cc[b][k]) {
            m->s.cc[b][k] = (uint8_t)c;
            changed(m);
        }
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ || track_held(m, 3)) {   /* (Track 4 held, any mode: the sequencer's track
                                                         * settings) */
        rfmv_seq_knob(m, k, d, now);
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_CONTROL) {
        const int b = m->s.bank;
        if (track_held(m, 2)) {                         /* Track 3 held: the knob's CC number */
            const int c = clampi(m->s.cc[b][k] + d, 0, 127);
            rfmv_menu(m, MENU_CC, k);
            if (c != m->s.cc[b][k]) {
                m->s.cc[b][k] = (uint8_t)c;
                changed(m);
            }
        } else {
            const int v = clampi(m->cc_val[b][k] + d, 0, 127);
            if (v != m->cc_val[b][k]) {
                m->cc_val[b][k] = (uint8_t)v;
                emit_midi(m, 0xB0 | m->s.channel, m->s.cc[b][k], v);
            }
            rfmv_pop(m, POP_CC, k);
        }
        return;
    }
    if (KNOB_ENC[k] >= 0) {                            /* (the popup counts this gesture's detents, the FM-1's) */
        const int acc = m->pop == POP_ENC && m->pop_i == k && (m->touched == k || rfmv_pop_up(m, POP_ENC)) ? m->pop_val : 0;
        int t;
        if ((m->knob_acc[k] > 0 && d < 0) || (m->knob_acc[k] < 0 && d > 0))
            m->knob_acc[k] = 0;                         /* (a turn back: the half detent forgotten) */
        t = m->knob_acc[k] + d;                         /* the owner's: half as fast, an FM-1 detent every 2 ticks */
        m->knob_acc[k] = (int8_t)(t % RFM_MOVE_KNOB_TICKS);
        t /= RFM_MOVE_KNOB_TICKS;
        if (t)
            emit(m, RFM_MOVE_ACT_TURN, KNOB_ENC[k], 0, 0, t);
        if (enc_nojob(m, KNOB_ENC[k]))                  /* (0.5.6: "" = no job: no popup) */
            return;
        rfmv_pop(m, POP_ENC, k);
        m->pop_val = (int16_t)clampi(acc + t, -9999, 9999);
        rfmv_say_turned(m);                             /* (0.6.7: speech: its readout awaited) */
    }
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

static void on_jog(rfm_move_t *m, int d, uint32_t now)
{
    if (!d)
        return;
    if ((fm1_seq_layer(m) && m->seq.smenu) || (track_held(m, 3) && m->s.mode != RFM_MOVE_MODE_SEQ)) {
        rfmv_seq_jog(m, d, now);                        /* (a sequencer menu open outside it, or Track 4 held: the
                                                         * track's channel) */
        return;
    }
    if (track_held(m, 1) || (m->mmenu && m->s.mode == RFM_MOVE_MODE_MIDI)) {   /* Track 2 held, Keys & Scales: the
                                                         * root */
        const int r = ((m->s.root + d) % 12 + 12) % 12;
        rfmv_menu(m, MENU_ROOT, r);
        if (r != m->s.root) {
            m->s.root = (uint8_t)r;
            changed(m);
        }
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ) {
        rfmv_seq_jog(m, d, now);
        return;
    }
    if (midi_mode(m) && m->s.layout == RFM_MOVE_LAYOUT_DRUM && m->edit_pad >= 0 && m->pad_down[m->edit_pad]) {
        const int p = m->edit_pad, n = clampi(m->s.drum[p] + d, 0, 127);
        if (n != m->s.drum[p]) {
            m->s.drum[p] = (uint8_t)n;
            changed(m);
        }
        rfmv_pop(m, POP_DRUM, p);
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_CONTROL) {
        const int b = clampi(m->s.bank + d, 0, RFM_MOVE_NBANKS - 1);
        if (b != m->s.bank) {
            m->s.bank = (uint8_t)b;
            changed(m);
        }
        return;
    }
    emit(m, RFM_MOVE_ACT_TURN, RFM_MOVE_E_SELECT, 0, 0, d);
    {                                                   /* the drawing's SELECT lights; FM-1 mode and MIDI's NOTES: a
                                                         * left-half popup while turning (this gesture's detents: from 0
                                                         * once it has timed out) */
        const int acc = rfmv_pop_up(m, POP_SELECT) ? m->pop_val : 0;
        rfmv_pop(m, POP_SELECT, 9);
        m->pop_val = (int16_t)clampi(acc + d, -9999, 9999);
        rfmv_say_turned(m);
    }
}

/* a touch: the knob's popup (in FM-1 mode the drawing marks the knob instead, and a turn brings the popup) */
static void on_touch(rfm_move_t *m, int k, int on)
{
    if (on && m->speech && m->seq.smenu && k < RFM_MOVE_NKNOBS) {   /* (0.6.3) a Shift menu's knob touched: its item */
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
        if (rfmv_half(m) && (m->pop == POP_ENC || m->pop == POP_MASTER) && m->pop_i == k) {
            m->pop = POP_NONE, m->pop_val = 0;          /* (FM-1 mode, MIDI's NOTES: the release ends the gesture: the
                                                         * popup goes, its count starts again) */
            return;
        }
        if (m->pop == POP_ENC || m->pop == POP_MASTER || m->pop == POP_CC || m->pop == POP_TRIG)
            rfmv_pop_let(m, m->pop, k);
        return;
    }
    m->touched = (int8_t)k;
    if (m->s.mode == RFM_MOVE_MODE_SEQ && m->seq.view && !m->seq.projects && k < 8) {
        rfmv_seq_select(m, m->seq.track_page + k);     /* (session: a knob touched selects the track in its column) */
        return;
    }
    if (rfmv_half(m)) {                                 /* (0.5.6, the owner's: FM-1 mode, MIDI's NOTES: a touch alone
                                                         * shows the knob's name (and MASTER its level); a turn its
                                                         * detents) */
        if (k == 8)
            rfmv_pop(m, POP_MASTER, 8);
        else if (k < RFM_MOVE_NKNOBS && KNOB_ENC[k] >= 0 && !enc_nojob(m, KNOB_ENC[k]))
            rfmv_pop(m, POP_ENC, k), m->pop_blank = 1;
        return;
    }
    if (rfmv_track_held(m, 2))
        return;
    if (k < RFM_MOVE_NKNOBS && rfmv_menu_knobs(m) >= 0)
        return;                                         /* (a menu or a held Track: its own screen, the legend ring) */
    if (k == 8)
        rfmv_pop(m, POP_MASTER, 8);
    else if (k < RFM_MOVE_NKNOBS && m->s.mode == RFM_MOVE_MODE_SEQ && m->seq.trig && !m->seq.view)
        rfmv_pop(m, POP_TRIG, k);
    else if (k < RFM_MOVE_NKNOBS && cc_mode(m))
        rfmv_pop(m, POP_CC, k);
    else if (k < RFM_MOVE_NKNOBS && KNOB_ENC[k] >= 0) {
        rfmv_pop(m, POP_ENC, k);
        m->pop_val = 0;
    }
}

/* (0.4.3) Back, in every mode and the projects view: a click (let go within RFM_MOVE_TAP_MS, nothing else pressed
 * meanwhile, no Track held) closes the latest layer (a menu, Loop's latch, Shift's lock, the clipboard's marks; in the
 * view: Capture, states, the description, the selection, the catalog, FX setup), and with nothing to close flips to
 * the projects view or back to the mode. Held + Track 1..3: that firmware is played, no flip */
static void back_click(rfm_move_t *m, uint32_t now)
{
    if (m->seq.projects) {                              /* (the view's own Back: one level up, then out) */
        mr_event_t e;
        memset(&e, 0, sizeof e);
        e.type = MR_EV_BUTTON, e.index = MR_CC_BACK, e.value = 127;
        rfmv_view_input(m, &e, now);
        e.value = 0;
        if (m->seq.projects)
            rfmv_view_input(m, &e, now);
        if (!m->seq.projects)                           /* (0.4.5) flipped out: the mode named, as a Track tap does */
            rfmv_pop(m, POP_MODE, m->s.mode);
        return;
    }
    if (m->mmenu)
        m->mmenu = 0;
    else if (rfmv_seq_layer_open(m))
        rfmv_seq_button(m, MR_CC_BACK, 1, now);
    else {                                              /* flipped in: the view named (FX & Mix, Firmwares, Projects) */
        rfmv_proj_enter(m);
        if (m->seq.projects)
            rfmv_fw_view_pop(m);
    }
}
static int back_input(rfm_move_t *m, const mr_event_t *ev, uint32_t now)   /* 1: taken */
{
    if (ev->type == MR_EV_BUTTON && ev->index == MR_CC_BACK) {
        if (ev->value) {
            m->back_down = 1, m->back_used = m->track_held != 0, m->back_ms = now;
        } else if (m->back_down) {
            m->back_down = 0;
            if (!m->back_used && now - m->back_ms < RFM_MOVE_TAP_MS)
                back_click(m, now);
        }
        return 1;
    }
    if (ev->type == MR_EV_BUTTON && ev->index >= MR_CC_TRACK4 && ev->index <= MR_CC_TRACK1) {
        const int t = MR_CC_TRACK1 - ev->index;
        if (ev->value && m->back_down) {               /* Back held + Track: the firmware (Track 4: nothing) */
            m->back_used = 1, m->back_trk = (uint8_t)(m->back_trk | 1u << t);
            if (t < 3)
                rfmv_fw_switch(m, t);
            return 1;
        }
        if (!ev->value && (m->back_trk >> t & 1)) {    /* (its release: nothing else) */
            m->back_trk = (uint8_t)(m->back_trk & ~(1u << t));
            return 1;
        }
        return 0;
    }
    if (m->back_down && ev->value && ev->type != MR_EV_TOUCH && ev->type != MR_EV_PAD_PRESSURE)
        m->back_used = 1;                               /* (anything else pressed meanwhile: no click) */
    return 0;
}

static void input(rfm_move_t *m, const mr_event_t *ev, uint32_t now_ms)
{
    if (rfmv_global_input(m, ev))                       /* (0.6.2) Global settings owns the controls while open */
        return;
    if (back_input(m, ev, now_ms))
        return;
    if (m->seq.projects && m->pop == POP_MODE && ev->value && ev->type != MR_EV_TOUCH && ev->type != MR_EV_PAD_PRESSURE)
        m->pop = POP_NONE;                              /* (0.4.5: the flip's name goes with the first press there) */
    if (m->seq.projects) {                              /* the projects view: a mode of its own (Tracks 1..3 the
                                                         * firmware slots, Track 4 the projects, Session FX setup) */
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
    case MR_EV_JOG_CLICK:                               /* (FILL is Shift + Loop held now, the owner's) */
        break;
    case MR_EV_TOUCH:
        if (ev->index >= 0 && ev->index <= 9)
            on_touch(m, ev->index, ev->value != 0);
        break;
    default: break;                                     /* jog click (the FM-1 has no encoder push), jacks, MIDI .. */
    }
}
void rfm_move_input(rfm_move_t *m, const mr_event_t *ev, uint32_t now_ms)
{
    m->say_stepev = ev->type == MR_EV_STEP;
    if (ev->type == MR_EV_BUTTON && ev->index == MR_CC_BACK && ev->value)
        m->say_bdown = m->say_n;                        /* (0.6.3) Back: nothing said from its press to its release: */
    m->say_back = m->speech && ev->type == MR_EV_BUTTON && ev->index == MR_CC_BACK && !ev->value &&
                  m->say_n == m->say_bdown;             /* .. where it left you is said */
    input(m, ev, now_ms);
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
    if (m->seq.projects)                                /* (Track 4: knob 1 the colour; the firmware tracks and FX
                                                         * setup light their own) */
        return m->fw.fx || m->fw.track < 3 ? -1 : 0x01;
    if (rfmv_hold_shown(m, 1))
        return 0x03;                                    /* (Track 2: root, scale) */
    if (rfmv_hold_shown(m, 2) || rfmv_hold_shown(m, 3))
        return 0xFF;                                    /* (Track 3: the CC numbers; Track 4: the track's settings) */
    if (m->seq.smenu)
        return rfmv_seq_menu_knobs(m);
    if (rfmv_hold_shown(m, 0))
        return 0;                                       /* (Track 1: the Shift layer, no knobs) */
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

static void wanted(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3])
{
    const rgb_t mode = MODE_RGB[m->s.mode & 3];
    const int t2 = track_held(m, 1) || (m->mmenu && m->s.mode == RFM_MOVE_MODE_MIDI), fm1 = !midi_mode(m);
    const rgb_t SCALE_RGB = { 0, 190, 255 };            /* (the scale pads: their own colour, cyan) */
    rgb_t c[L_WHITE0];
    uint8_t wv[13];
    int i;
    for (i = 0; i < RFM_MOVE_NPADS; i++) {
        if (t2 && i == 31 && track_held(m, 1))         /* (0.5.3) latch: amber, bright while on */
            c[i] = sc((rgb_t){ 255, 170, 30 }, m->latch ? 256 : 40);
        else if (t2)
            c[i] = i >= 24 && i < 28 ? sc(mode, m->s.layout == i - 24 ? 256 : 64)
                 : i < 24 ? sc(SCALE_RGB, m->s.scale == rfmv_scale_of_pad(i) ? 256 : 56) : OFF;
        else if (fm1) {
            const int k = PAD_KEY[i];
            const int lv = k >= 0 ? alpha(level_of(m, 14 + k)) : 0;
            c[i] = k < 0 ? OFF : m->pad_down[i] ? mode : sc((0x54A >> ((k + 5) % 12)) & 1 ? PINK : WHITE, lv > FLOOR ? lv : FLOOR);
        } else
            c[i] = midi_pad(m, i, mode);
    }
    for (i = 0; i < RFM_MOVE_NSTEPS; i++) {
        rgb_t s = OFF;
        if (fm1 && track_held(m, 0)) {                  /* Track 1 held: the Shift layer's steps (the ones with a job) */
            static const uint16_t JOBS = 0xF7F7;        /* steps 1 2 3 5 6 7 8 9 10 11 13 14 15 16 */
            s = JOBS >> i & 1 ? sc(WHITE, 96) : OFF;
        } else if (t2 && m->mmenu && !track_held(m, 1))
            s = i == 8 ? WHITE : OFF;                   /* (Keys & Scales: step 9) */
        else if (t2)
            s = m->s.channel == i ? mode : sc(WHITE, 16);
        else if (fm1) {
            const int lv = alpha(level_of(m, 14 + STEP_KEY[i]));
            s = m->step[i].kind == H_KEY ? mode : sc(WHITE, lv > FLOOR ? lv : FLOOR);
        }
        c[L_STEP0 + i] = s;
    }
    c[L_PLAY] = add(sc(WHITE, alpha(level_of(m, RFM_MOVE_B_PLAY))), sc(GREEN, alpha(m->play_green)));
    c[L_REC] = sc(RED, alpha(level_of(m, RFM_MOVE_B_REC)));
    c[L_SAMPLE] = sc(WHITE, alpha(level_of(m, RFM_MOVE_B_SEL)));
    for (i = 0; i < 4; i++)
        c[L_TRACK0 + i] = sc(MODE_RGB[i], m->s.mode == i ? 256 : DIM / 2);
    for (i = 0; i < RFM_MOVE_NKNOBS; i++) {
        rgb_t r = OFF;
        if (m->s.mode == RFM_MOVE_MODE_CONTROL) {
            const rgb_t bc = BANK_RGB[m->s.bank & 3];
            r = m->touched == i ? bc : sc(bc, 16 + m->cc_val[m->s.bank][i] * 240 / 127);
        } else if (KNOB_ENC[i] >= 0)
            r = sc(mode, m->touched == i ? 256 : 64);
        c[L_RING0 + i] = r;
    }
    for (i = 0; i < 13; i++) {
        const int cc = WHITE_CC[i], b = rfm_move_button_of(cc);
        int v = 0;
        if (cc == MR_CC_DOWN && !midi_mode(m))
            v = rfmv_fw_loaded(m) > 1 ? 127 : 0;       /* (0.4.4: lit with another firmware to cycle to) */
        else if (cc == MR_CC_UP || cc == MR_CC_DOWN)
            v = !midi_mode(m) ? clampi(alpha(level_of(m, b)) * 127 / 256, 6, 127) : (cc == MR_CC_UP ? m->s.octave > 0 : m->s.octave < 0) ? 127 : 24;
        else if (b >= 0)
            v = clampi(alpha(level_of(m, b)) * 127 / 256, 6, 127);
        wv[i] = (uint8_t)v;
    }
    if (m->seq.projects)                                /* the projects view: its own LEDs (the firmwares or the
                                                         * projects; the rest off) */
        rfmv_fw_leds(m, c, wv);
    else if (m->s.mode == RFM_MOVE_MODE_SEQ && !t2)    /* Sequencer mode takes over all but the Track LEDs */
        rfmv_seq_leds(m, c, wv);
    {
        const int mk = rfmv_menu_knobs(m);              /* a menu or a held Track: only its knobs, its colour */
        if (mk >= 0)
            for (i = 0; i < RFM_MOVE_NKNOBS; i++)
                c[L_RING0 + i] = !(mk >> i & 1) ? OFF : sc(MENU_KNOB, m->touched == i ? 256 : 110);
    }
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
