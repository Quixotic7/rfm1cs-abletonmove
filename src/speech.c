/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (0.6.0) words for a screen reader (move.h, rfm_move_set_speech; README "Speech"). What the screen shows, as text:
 * the base view (a mode, a firmware track, FX setup's page, the sequencer's view) said as an EVENT when it changes;
 * a popup (a knob's value, a held Track's menu item, a readout, a message) and the selection (the firmware picked,
 * the FX slot focused) said as a VALUE when their words change. Made only at the end of a call that can change the
 * screen (rfmv_say_check), never per frame; nothing at all while speech is off. Schwung's conventions: "label: value",
 * views by their name, short, digits for numbers, note names as the screen has them. */
#include <stdio.h>
#include <string.h>

#include "move_int.h"

#define clampi rfmv_clampi

static const char *const SCALE_LONG[RFM_MOVE_NSCALES] = {
    "Major", "Minor", "Dorian", "Mixolydian", "Pentatonic Major", "Pentatonic Minor", "Harmonic Minor", "Blues",
    "Phrygian", "Lydian", "Locrian", "Melodic Minor", "Whole Tone", "Diminished Half Whole", "Diminished Whole Half",
    "Hungarian Minor", "Phrygian Dominant", "In Sen", "Hirajoshi", "Iwato", "Pelog", "Double Harmonic",
    "Spanish 8 Tone", "Bebop" };
static const char *const LAYOUT_LONG[4] = { "Scale", "Chromatic", "Drum", "Octave" };
/* the sequencer's messages (seq_ui.c MSG, SEQ_MSG_* order), as words */
static const char *const MSG_WORDS[] = {
    "", "No empty clip", "Full", "Load failed", "Captured", "Undo", "Redo", "Copied", "Pasted", "", "Quantized",
    "Loop doubled", "External tempo", "Duplicated", "Cleared", "Capture cleared", "Sync: AUTO", "Sync: INT",
    "Sync: EXT", "Full velocity", "Pad velocity", "Metronome on", "Metronome off", "New clip", "Clip cleared" };
static const char *const FN_WORDS[RFM_MOVE_FN_N] = { "None", "Restart", "Reverse", "Forward", "Flip", "Random",
                                                     "Random function", "Leap", "Jump", "Repeat" };

/* ================================================================ helpers === */
/* printable ASCII only, runs of spaces as one, no space at either end */
static void tidy(char *s)
{
    int i, n = 0, sp = 1;
    for (i = 0; s[i]; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!sp)
                s[n++] = ' ', sp = 1;
        } else if (c > 32 && c < 127)
            s[n++] = (char)c, sp = 0;
    }
    while (n > 0 && s[n - 1] == ' ')
        n--;
    s[n] = 0;
}
static uint32_t hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h ? h : 1;
}
static void len_words(int v, char *b, int cap)          /* a note length (screen.c's len_text) */
{
    if (rfmv_seq_len_inf(v))
        snprintf(b, (size_t)cap, "Infinite");
    else if (v % RFM_MOVE_SEQ_TPS == 0)
        snprintf(b, (size_t)cap, "%d %s", v / RFM_MOVE_SEQ_TPS, v == RFM_MOVE_SEQ_TPS ? "step" : "steps");
    else if (v < RFM_MOVE_SEQ_TPS)
        snprintf(b, (size_t)cap, "1/%d step", (RFM_MOVE_SEQ_TPS + v / 2) / v);
    else
        snprintf(b, (size_t)cap, "%d.%d steps", v / RFM_MOVE_SEQ_TPS, v % RFM_MOVE_SEQ_TPS * 10 / RFM_MOVE_SEQ_TPS);
}
static const char *enc_label(const rfm_move_t *m, int e)
{
    return e >= 0 && e <= RFM_MOVE_NENC && m->label[e][0] ? m->label[e] : rfm_move_enc_name(e);
}
static void track_words(const rfm_move_t *m, int t, char *o, int cap)
{
    const rfm_move_track_t *tr = &m->seq.tr[t & 15];
    snprintf(o, (size_t)cap, "Track %d%s", (t & 15) + 1, tr->solo ? ", solo" : tr->mute ? ", muted" : "");
}

/* ================================================================ the base view === */
/* its subject and its words (lng: the long form a view entered from another has: "Firmwares, ..", "FX setup, ..",
 * "Sequencer mode") */
enum { S_FW_TRACK0 = 0x11, S_FW_LAST = 0x23, S_FX0 = 0x100, S_FX_LAST = 0x3FF, S_SEQ_TRACK = 4, S_SEQ_SESSION = 5,
       S_MENU0 = 0x400, S_MENU_LAST = 0x43F };
static int base_words(const rfm_move_t *m, char *o, int cap)
{
    int s = rfmv_fw_say_base(m, o, cap);
    if (s)
        return s;
    if (m->seq.smenu && !m->seq.projects) {             /* (0.6.1) a Shift menu open: its name */
        if (m->seq.smenu == RFM_MOVE_SM_GLOBAL) {       /* (0.6.2: the list, its row) */
            char r[64];
            rfmv_global_words(m, r, sizeof r);
            snprintf(o, (size_t)cap, "%s, %s", rfmv_seq_smenu_name(m->seq.smenu), r);
        } else
            snprintf(o, (size_t)cap, "%s", rfmv_seq_smenu_name(m->seq.smenu));
        return S_MENU0 + (m->s.mode & 3) * 16 + (m->seq.smenu & 15);   /* (0.6.3: the mode under it too) */
    }
    switch (m->s.mode) {
    case RFM_MOVE_MODE_MIDI:
        if (m->mmenu) {
            snprintf(o, (size_t)cap, "Keys and Scales");
            return 7;
        }
        snprintf(o, (size_t)cap, "MIDI mode%s", m->s.midi_view == RFM_MOVE_MV_NOTES ? ", Notes" : "");
        return m->s.midi_view == RFM_MOVE_MV_NOTES ? 6 : 2;
    case RFM_MOVE_MODE_CONTROL: snprintf(o, (size_t)cap, "Control mode"); return 3;
    case RFM_MOVE_MODE_SEQ:
        if (m->seq.view) {
            snprintf(o, (size_t)cap, "Session");
            return S_SEQ_SESSION;
        }
        snprintf(o, (size_t)cap, "Track %d", m->seq.sel_track + 1);
        return S_SEQ_TRACK;
    default: snprintf(o, (size_t)cap, "FM-1 mode"); return 1;
    }
}
static int in_fw(int s) { return s >= S_FW_TRACK0 && s <= S_FW_LAST; }
static int in_fx(int s) { return s >= S_FX0 && s <= S_FX_LAST; }
static int in_menu(int s) { return s >= S_MENU0 && s <= S_MENU_LAST; }
static int in_seq(int s) { return s == S_SEQ_TRACK || s == S_SEQ_SESSION || in_menu(s); }   /* (a Shift
                                                         * menu closed: back to the view, by its name) */
/* the words said for a view entered from the view `from` (-1: the long form always) */
static int base_said(const rfm_move_t *m, int from, char *o, int cap)
{
    char b[RFM_MOVE_SAY_MAX];
    const int s = base_words(m, b, sizeof b);
    if (in_menu(s) && in_menu(from) && (s & 15) == (from & 15)) {   /* (a Track pressed under a menu: the mode) */
        static const char *const MODE[4] = { "FM-1 mode", "MIDI mode", "Control mode", "Sequencer mode" };
        snprintf(o, (size_t)cap, "%s, %s", MODE[(s >> 4) & 3], b);
    } else if (s >= S_FW_TRACK0 && s < S_FW_TRACK0 + 3 && !in_fw(from))
        snprintf(o, (size_t)cap, "Firmwares, %s", b);
    else if (in_fx(s) && !in_fx(from) && s < 0x200)
        snprintf(o, (size_t)cap, "FX setup, %s", b);
    else if ((s == S_SEQ_TRACK || s == S_SEQ_SESSION) && !in_seq(from))
        snprintf(o, (size_t)cap, "Sequencer mode%s", s == S_SEQ_SESSION ? ", Session" : "");
    else
        snprintf(o, (size_t)cap, "%s", b);
    return s;
}

/* ================================================================ popups === */
static const int8_t SLOT_ENC[8] = { RFM_MOVE_E_SELECT, RFM_MOVE_E_ALGO, RFM_MOVE_E_PRESETS, RFM_MOVE_E_K1,
                                    RFM_MOVE_E_K2, RFM_MOVE_E_K3, RFM_MOVE_E_K4, RFM_MOVE_E_MASTER };
/* (0.6.7) a knob slot's readout, valid for speech: known, and for the control's label as it is now (a Shift page's
 * other label: the old page's readout is stale) */
static uint16_t held_btns(const rfm_move_t *m)        /* (0.6.8) the FM-1 buttons held: a readout's page */
{
    uint16_t b = 0;
    int i;
    for (i = 0; i < RFM_MOVE_NBTN; i++)
        if (m->btn_count[i])
            b = (uint16_t)(b | 1u << i);
    return b;
}
static int rd_valid(const rfm_move_t *m, int sl)
{
    return sl >= 0 && sl < 8 && m->rd[sl].on && m->rd_mods[sl] == held_btns(m) &&
           !strcmp(m->rd_lbl[sl], enc_label(m, SLOT_ENC[sl]));
}
/* (0.6.8) the full title seen for this control on this page with this label, NULL none */
static const char *title_seen(const rfm_move_t *m, int sl)
{
    const uint16_t b = held_btns(m);
    const char *l = enc_label(m, SLOT_ENC[sl & 7]);
    int i;
    for (i = 0; i < m->say_tcn; i++)
        if (m->say_tc[i].slot == sl && m->say_tc[i].mods == b && !strcmp(m->say_tc[i].lbl, l))
            return m->say_tc[i].title;
    for (i = m->say_tcn - 1; i >= 0; i--)               /* (0.6.9) another page's, for a readout that named this
                                                         * control itself (control >= 0) with this label */
        if (m->say_tc[i].slot == sl && m->say_tc[i].expl && !strcmp(m->say_tc[i].lbl, l))
            return m->say_tc[i].title;
    return NULL;
}
void rfmv_say_readout(rfm_move_t *m, int slot, int expl)
{
    const int n = (int)(sizeof m->say_tc / sizeof m->say_tc[0]);
    int i;
    if (slot < 0 || slot >= 8)
        return;
    snprintf(m->rd_lbl[slot], sizeof m->rd_lbl[slot], "%s", enc_label(m, SLOT_ENC[slot]));
    m->rd_mods[slot] = held_btns(m);
    m->rd_any = 1;
    if (m->rd[slot].title[0]) {                         /* (its title kept for this control, page and label) */
        for (i = 0; i < m->say_tcn; i++)
            if (m->say_tc[i].slot == slot && m->say_tc[i].mods == m->rd_mods[slot] &&
                !strcmp(m->say_tc[i].lbl, m->rd_lbl[slot]))
                break;
        if (i == m->say_tcn) {
            if (m->say_tcn < n)
                m->say_tcn++;
            else
                memmove(&m->say_tc[0], &m->say_tc[1], sizeof m->say_tc[0] * (size_t)(n - 1)), i = n - 1;
        }
        m->say_tc[i].slot = (uint8_t)slot, m->say_tc[i].mods = m->rd_mods[slot];
        m->say_tc[i].expl = (uint8_t)(expl != 0);
        snprintf(m->say_tc[i].lbl, sizeof m->say_tc[i].lbl, "%s", m->rd_lbl[slot]);
        snprintf(m->say_tc[i].title, sizeof m->say_tc[i].title, "%s", m->rd[slot].title);
    }
    if (m->speech && m->say_turn == slot + 1)
        m->say_rdnew = 1;                               /* (the turn's answer: said if its words are new; the same:
                                                         * once, by the range's end repeat) */
}
void rfmv_say_turned(rfm_move_t *m)
{
    const int sl = rfmv_pop_slot(m);
    if (!m->speech || sl < 0)
        return;
    m->say_turn = (uint8_t)(sl + 1), m->say_turn_ms = rfmv_now_ms(m);
}
static void readout_words(const rfm_move_t *m, const rfm_move_readout_t *r, int slot, char *o, int cap)
{
    char t[RFM_MOVE_LABEL_MAX + 8];
    snprintf(t, sizeof t, "%s", r->title);
    if (!t[0] && slot >= 0 && slot < 8)
        rfmv_tcase(enc_label(m, SLOT_ENC[slot]), t, sizeof t);
    if (t[0] && r->value[0])
        snprintf(o, (size_t)cap, r->kind == RFM_MOVE_RD_MESSAGE ? "%s, %s" : "%s: %s", t, r->value);
    else
        snprintf(o, (size_t)cap, "%s%s", t, r->value);
}
static void trig_words(const rfm_move_t *m, char *o, int cap)
{
    const rfm_move_seq_t *q = &m->seq;
    const int k = m->pop_i, clip = SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel);
    int abs = q->page * RFM_MOVE_SEQ_PAGE + (q->step_first >= 0 ? q->step_first : 0), v;
    char b[32];
    if (q->step_first < 0) {
        int lo, hi = rfmv_seq_clip_range(m, clip, &lo);
        abs = hi > lo ? q->notes[lo].step : abs;
    }
    v = q->trig && q->step_first < 0 ? rfmv_seq_tdef_val(m, k) : rfmv_seq_trig_val(m, clip, abs, k);
    if (!q->trig) {
        if (k == 0)
            snprintf(o, (size_t)cap, "Nudge: %+d%%", v * 100 / RFM_MOVE_SEQ_TPS);
        else if (k == 6)
            len_words(v, b, sizeof b), snprintf(o, (size_t)cap, "Length: %s", b);
        else
            snprintf(o, (size_t)cap, "Velocity: %d", v);
        return;
    }
    switch (k) {
    case 0: snprintf(o, (size_t)cap, v ? "Micro: %+d" : "Micro: 0", v); break;
    case 1: snprintf(o, (size_t)cap, "Ratchet: %d", v); break;
    case 2: snprintf(o, (size_t)cap, "Chance: %d%%", v); break;
    case 3: rfmv_cond_name(v, b, sizeof b), snprintf(o, (size_t)cap, "Condition: %s", b); break;
    case 4: snprintf(o, (size_t)cap, "Function: %s", FN_WORDS[clampi(v, 0, RFM_MOVE_FN_N - 1)]); break;
    case 5: {
        const int f = q->step_first < 0 ? rfmv_seq_tdef_val(m, 4) : rfmv_seq_trig_val(m, clip, abs, 4);
        if (f == RFM_MOVE_FN_LEAP)
            snprintf(o, (size_t)cap, "Leap: %+d", v);
        else if (f == RFM_MOVE_FN_JUMP)
            snprintf(o, (size_t)cap, "Jump: %d", v + 1);
        else if (f == RFM_MOVE_FN_REPEAT)
            snprintf(o, (size_t)cap, "Repeat: %d", v);
        else
            snprintf(o, (size_t)cap, "Amount: none");
        break;
    }
    case 6: len_words(v, b, sizeof b), snprintf(o, (size_t)cap, "Length: %s", b); break;
    default: snprintf(o, (size_t)cap, "Velocity: %d", v); break;
    }
}
static void loop_words(const rfm_move_t *m, char *o, int cap)
{
    const rfm_move_seq_t *q = &m->seq;
    const int clip = SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel);
    const int L = rfmv_seq_loop_hi(m, clip) - rfmv_seq_loop_lo(m, clip), P = L / RFM_MOVE_SEQ_PAGE, S = L % RFM_MOVE_SEQ_PAGE;
    if (!P)
        snprintf(o, (size_t)cap, "Loop: %d %s", S, S == 1 ? "step" : "steps");
    else if (S)
        snprintf(o, (size_t)cap, "Loop: %d bars %d steps", P, S);
    else
        snprintf(o, (size_t)cap, "Loop: %d %s", P, P == 1 ? "bar" : "bars");
}
static void step_words(const rfm_move_t *m, char *o, int cap)   /* (0.6.5, the owner's: just the step; the notes
                                                         * are heard) */
{
    snprintf(o, (size_t)cap, "Step %d", m->pop_i + 1);
}
static void menu_words(const rfm_move_t *m, char *o, int cap)
{
    const int i = m->menu_i;
    o[0] = 0;
    if (rfmv_now_ms(m) - m->menu_ms >= RFM_MOVE_POPUP_MS)
        return;
    switch (m->menu_what) {
    case MENU_LAYOUT: snprintf(o, (size_t)cap, "Layout: %s", LAYOUT_LONG[i & 3]); break;
    case MENU_SCALE: snprintf(o, (size_t)cap, "Scale: %s", SCALE_LONG[i % RFM_MOVE_NSCALES]); break;
    case MENU_ROOT: snprintf(o, (size_t)cap, "Root: %s", rfmv_note_names[i % 12]); break;
    case MENU_CHANNEL:
    case MENU_TCH: snprintf(o, (size_t)cap, "Channel: %d", i + 1); break;
    case MENU_CC: snprintf(o, (size_t)cap, "Knob %d: CC %d", (i & 7) + 1, m->s.cc[m->s.bank][i & 7]); break;
    case MENU_TRACK: track_words(m, i, o, cap); break;
    case MENU_RATE: snprintf(o, (size_t)cap, "Rate: %s", rfm_move_seq_rate_name(i)); break;
    default: break;
    }
}
/* m->pop's words */
static void pop_kind_words(const rfm_move_t *m, char *o, int cap)
{
    const rfm_move_seq_t *q = &m->seq;
    const int i = m->pop_i;
    char l[32];
    o[0] = 0;
    switch (m->pop) {
    case POP_SELECT:
    case POP_ENC:
    case POP_MASTER: {
        const int sl = rfmv_pop_slot(m);
        if (rd_valid(m, sl)) {                          /* (0.6.7) touched or turned: "Name: value" (the readout's
                                                         * full title) */
            readout_words(m, &m->rd[sl], sl, o, cap);
            return;
        }
        if (m->pop == POP_MASTER) {
            snprintf(o, (size_t)cap, "Volume %d%%", (m->master * 100 + 511) / 1023);
            return;
        }
        {
            const char *ti = sl >= 0 ? title_seen(m, sl) : NULL;   /* (0.6.8: a title seen: the full name) */
            if (ti)
                snprintf(l, sizeof l, "%s", ti);
            else
                rfmv_tcase(enc_label(m, m->pop == POP_SELECT ? RFM_MOVE_E_SELECT : rfm_move_knob_enc(i)), l, sizeof l);
        }
        if (m->say_late)
            o[0] = 0;                                   /* (0.6.9) the range's end repeat with no value known: nothing
                                                         * (was the name again: "Voicing", "Voicing") */
        else if (m->pop_blank || !m->pop_val)
            snprintf(o, (size_t)cap, "%s", l);          /* (0.6.7: never the Move's placeholder "0") */
        else if (m->rd_any)
            o[0] = 0;                                   /* (0.6.7) a firmware with readouts: its value comes as one;
                                                         * the Move's detent count is never said (0.6.8: nor the name
                                                         * again at the range's end with no value known) */
        else
            snprintf(o, (size_t)cap, "%s: %+d", l, m->pop_val);
        return;
    }
    case POP_CC: {
        const int b = m->s.bank, cc = m->s.cc[b][i & 7];
        int val = m->cc_val[b][i & 7];
        if (m->s.mode == RFM_MOVE_MODE_SEQ && !q->view && q->step_first >= 0)
            val = rfmv_seq_lock_get(m, SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel),
                                    q->page * RFM_MOVE_SEQ_PAGE + q->step_first, cc);
        if (val < 0)
            snprintf(o, (size_t)cap, "CC %d: none", cc);
        else
            snprintf(o, (size_t)cap, "CC %d: %d", cc, val);
        return;
    }
    case POP_DRUM: snprintf(o, (size_t)cap, "Pad %d: %s", (i & 31) + 1, rfmv_note_name(m->s.drum[i & 31], l, sizeof l)); return;
    case POP_TEMPO: {
        const int b10 = rfm_move_seq_bpm10(m);
        snprintf(o, (size_t)cap, b10 % 10 ? "Tempo %d.%d" : "Tempo %d", b10 / 10, b10 % 10);
        return;
    }
    case POP_BANK:
        if (m->s.mode == RFM_MOVE_MODE_SEQ && q->trig)
            snprintf(o, (size_t)cap, "Trig knobs");
        else
            snprintf(o, (size_t)cap, "Bank %d", m->s.bank + 1);
        return;
    case POP_TRIG: trig_words(m, o, cap); return;
    case POP_LOOP: loop_words(m, o, cap); return;
    case POP_STEP: step_words(m, o, cap); return;
    case POP_BAR: snprintf(o, (size_t)cap, "Bar %d", i + 1); return;
    case POP_TRACK: track_words(m, i, o, cap); return;
    case POP_PROJ: {
        rfm_move_proj_info_t in;
        rfmv_proj_info(m, i, &in);
        if (m->pop_val == 1)
            snprintf(o, (size_t)cap, "Colour %d", in.color ? in.color : i % RFM_MOVE_PROJ_COLORS + 1);
        else if (in.ok)
            snprintf(o, (size_t)cap, "Project %d, %d BPM, %d %s", i + 1, (in.bpm10 + 5) / 10, in.clips,
                     in.clips == 1 ? "clip" : "clips");
        else
            snprintf(o, (size_t)cap, "Project %d, empty", i + 1);
        return;
    }
    case POP_FW: {
        char nm[64];
        rfmv_fw_slot_name(m, i, nm, sizeof nm);
        snprintf(o, (size_t)cap, "Playing, Slot %d, %s", i + 1, nm[0] ? nm : "empty");
        return;
    }
    case POP_MODE:
        if (i == 30 || i == 31)
            snprintf(o, (size_t)cap, "%s", i == 31 ? "Latch on" : "Latch off");
        return;                                         /* (a mode, a view: the base says it) */
    case POP_HOST: snprintf(o, (size_t)cap, "%s", m->pop_text); return;
    case POP_LABEL: {
        char t[RFM_MOVE_LABEL_MAX + 1];
        if (rfmv_half(m) && rfmv_label(m, i, t, sizeof t))
            rfmv_tcase(t, o, cap);
        return;
    }
    case POP_READ:
        if (rfmv_half(m))
            readout_words(m, &m->rd_pop, i < 8 ? i : -1, o, cap);
        return;
    default: return;
    }
}
/* the popup up now (as rfm_move_screen picks it): 1 one is up (its words may be "": the base says it); its subject
 * and stamp (a new stamp: said again even with the same words) */
static int pop_words(const rfm_move_t *m, char *o, int cap, uint16_t *subj, uint32_t *stamp)
{
    const rfm_move_seq_t *q = &m->seq;
    int t, up;
    o[0] = 0, *subj = 0, *stamp = 0;
    if (m->pop == POP_HOST && rfmv_pop_up(m, POP_HOST)) {
        snprintf(o, (size_t)cap, "%s", m->pop_text);
        *subj = 0x100 | POP_HOST, *stamp = m->pop_t0;
        return 1;
    }
    if (rfmv_fw_say_pop(m, o, cap)) {
        *subj = 0x400 | m->fw.pop;
        return 1;
    }
    if (m->seq.projects && (m->fw.fx || (m->fw.host && m->fw.track < 3)))
        return 0;                                       /* (the view's own screen: no other popup on it) */
    if (m->pop == POP_MODE && (m->pop_i == 30 || m->pop_i == 31) && rfmv_pop_up(m, POP_MODE)) {
        pop_kind_words(m, o, cap);
        *subj = 0x100 | POP_MODE, *stamp = m->pop_t0;
        return 1;
    }
    for (t = 1; t < 4 && (q->projects || !rfmv_hold_shown(m, t)); t++) {}
    if (t < 4) {                                        /* a held Track's menu: the item changed */
        menu_words(m, o, cap);
        *subj = (uint16_t)(0x200 | m->menu_what);
        return 1;
    }
    up = m->pop && (m->pop != POP_SELECT || rfmv_half(m)) && rfmv_pop_up(m, m->pop) &&
         !(m->pop == POP_MODE && m->pop_i < 4 && rfmv_hold_shown(m, m->pop_i));
    if (up && m->pop == POP_STEP && (q->view || q->projects || m->s.mode != RFM_MOVE_MODE_SEQ))
        up = 0;
    if (up && m->pop == POP_LABEL && rfmv_now_ms(m) - m->pop_ms < m->pop_delay)
        up = 0;
    if (rfmv_seq_msg_text(m) && (!up || (int32_t)(q->msg_ms - m->pop_ms) >= 0)) {
        snprintf(o, (size_t)cap, "%s", q->msg < sizeof MSG_WORDS / sizeof MSG_WORDS[0] ? MSG_WORDS[q->msg] : "");
        *subj = (uint16_t)(0x300 | q->msg), *stamp = m->msg_n;
        return 1;
    }
    if (!up)
        return 0;
    pop_kind_words(m, o, cap);
    *subj = (uint16_t)(0x100 | m->pop), *stamp = m->pop_t0;
    return 1;
}

/* ================================================================ saying === */
static void say(rfm_move_t *m, int kind, const char *w)
{
    rfm_move_act_t a;
    if (!w[0] || !m->fn)
        return;
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_SAY, a.index = (uint8_t)kind, a.frame = 0;
    a.data = (const uint8_t *)w, a.len = (uint32_t)strlen(w);
    m->say_n++;
    m->fn(m->ctx, &a);
}
static int transport(const rfm_move_t *m)
{
    return (m->seq.playing || m->seq.start_pending ? 1 : 0) | (m->seq.rec ? 2 : 0);
}
static void say_transport(rfm_move_t *m)
{
    const int tr = transport(m);
    if (tr == m->say_tr)
        return;
    if ((tr ^ m->say_tr) & 1)
        say(m, RFM_MOVE_SAY_VALUE, tr & 1 ? "Playing" : "Stopped");
    if ((tr ^ m->say_tr) & 2)
        say(m, RFM_MOVE_SAY_VALUE, tr & 2 ? "Record on" : "Record off");
    m->say_tr = (uint8_t)tr;
}
/* ================================================================ (0.6.1) presses, watched values === */
/* an FM-1 button pressed (any mode that plays the FM-1): its words at once, every press (its label, else the button's
 * static name, else the printed name); the label's popup later is not said again */
void rfmv_say_press(rfm_move_t *m, int slot)
{
    static const char *const PRINTED[RFM_MOVE_NBTN] = { "FX", "Select", "Envelope", "LFO", "Edit", "Global", "Home",
                                                        "Save", "Arp", "Sequencer", "Play", "Record", "Octave down",
                                                        "Octave up" };   /* (the panel's words, as said) */
    char t[RFM_MOVE_LABEL_MAX + 1];
    if (!m->speech || slot < RFM_MOVE_LBL_BTN0 || slot >= RFM_MOVE_LBL_BTN0 + RFM_MOVE_NBTN)
        return;
    if (rfmv_label(m, slot, t, sizeof t))
        rfmv_tcase(t, m->say_now, sizeof m->say_now);   /* (as its popup shows it) */
    else
        snprintf(m->say_now, sizeof m->say_now, "%s", PRINTED[slot - RFM_MOVE_LBL_BTN0]);
    m->say_lbl_t0 = m->pop == POP_LABEL && m->pop_i == slot ? m->pop_t0 : 0;
}

void rfmv_say_text(rfm_move_t *m, const char *text)
{
    if (!m->speech)
        return;
    snprintf(m->say_now, sizeof m->say_now, "%s", text ? text : "");
    m->say_lbl_t0 = 0;
}

/* values the screen shows without a popup (or that a button changes): their words, said when they change */
enum { I_TRACK, I_PAGE, I_SCENES, I_TRACKS, I_OCTAVE, I_BANK, I_LAYOUT, I_ROOT, I_SCALE, I_CHANNEL, I_VEL, I_TRIG,
       I_LATCH, I_TEMPO, I_STEPREC, I_SAMPLE, I_AUDIO, I_LOOPED, I_FILL, I_SHIFTLOCK, I_CLIPBOARD, I_PASTED,
       I_SMJOG, I_SM0, I_N = I_SM0 + 8 };
#define TRACKSET ((1u << I_OCTAVE) | (1u << I_BANK) | (1u << I_LAYOUT) | (1u << I_ROOT) | (1u << I_SCALE) | \
                  (1u << I_CHANNEL) | (1u << I_TRIG))
#define SMITEMS (0xFFu << I_SM0)
typedef char say_items_fit[I_N <= 32 ? 1 : -1];
static void item_words(const rfm_move_t *m, int i, char *o, int cap)
{
    const rfm_move_seq_t *q = &m->seq;
    const int seq = m->s.mode == RFM_MOVE_MODE_SEQ, midi = m->s.mode == RFM_MOVE_MODE_MIDI || seq;
    o[0] = 0;
    if (q->projects && i != I_TEMPO)
        return;                                         /* (the projects view: its own words) */
    switch (i) {
    case I_TRACK:
        if (seq)
            track_words(m, q->sel_track, o, cap);
        break;
    case I_PAGE:
        if (seq && !q->view)
            snprintf(o, (size_t)cap, "Bar %d", q->page + 1);
        break;
    case I_SCENES:
        if (seq && q->view)
            snprintf(o, (size_t)cap, "Scenes %d to %d", q->scene_off + 1, q->scene_off + 4);
        break;
    case I_TRACKS:
        if (seq && q->view)
            snprintf(o, (size_t)cap, "Tracks %d to %d", q->track_page + 1, q->track_page + 8);
        break;
    case I_OCTAVE:
        if (midi)
            snprintf(o, (size_t)cap, "Octave %d", m->s.octave);
        break;
    case I_BANK:
        if (m->s.mode == RFM_MOVE_MODE_CONTROL || seq)
            snprintf(o, (size_t)cap, "Bank %d", m->s.bank + 1);
        break;
    case I_LAYOUT:
        if (midi)
            snprintf(o, (size_t)cap, "Layout: %s", LAYOUT_LONG[m->s.layout & 3]);
        break;
    case I_ROOT:
        if (midi)
            snprintf(o, (size_t)cap, "Root: %s", rfmv_note_names[m->s.root % 12]);
        break;
    case I_SCALE:
        if (midi)
            snprintf(o, (size_t)cap, "Scale: %s", SCALE_LONG[m->s.scale % RFM_MOVE_NSCALES]);
        break;
    case I_CHANNEL:
        if (m->s.mode == RFM_MOVE_MODE_MIDI || m->s.mode == RFM_MOVE_MODE_CONTROL)
            snprintf(o, (size_t)cap, "Channel: %d", m->s.channel + 1);
        break;
    case I_VEL: {
        char v[16];
        rfmv_tcase(rfm_move_vel_name(m->s.vel_mode), v, sizeof v);
        if (m->s.vel_mode == RFM_MOVE_VEL_FIXED)
            snprintf(o, (size_t)cap, "Velocity: %s %d", v, m->s.vel_level);
        else
            snprintf(o, (size_t)cap, "Velocity: %s", v);
        break;
    }
    case I_TRIG:
        if (seq && !q->view)
            snprintf(o, (size_t)cap, "%s", q->trig ? "Trig knobs" : "");
        break;
    case I_LATCH: snprintf(o, (size_t)cap, "%s", m->latch ? "Latch on" : "Latch off"); break;
    case I_TEMPO: {
        const int b10 = rfm_move_seq_bpm10(m);
        snprintf(o, (size_t)cap, b10 % 10 ? "Tempo %d.%d" : "Tempo %d", b10 / 10, b10 % 10);
        break;
    }
    case I_STEPREC: snprintf(o, (size_t)cap, "%s", q->steprec ? "Step record on" : "Step record off"); break;
    case I_SAMPLE: snprintf(o, (size_t)cap, "%s", q->srec == 2 ? "Sampling" : q->srec ? "Sample armed" : "Sample off"); break;
    case I_AUDIO:
        snprintf(o, (size_t)cap, "%s", q->arec == 1 ? "Audio loop armed" : q->arec == 2 ? "Recording audio loop"
                                        : q->arec == 3 ? "Audio loop ending" : "Audio loop off");
        break;
    case I_LOOPED: snprintf(o, (size_t)cap, "%s", q->loop_latch ? "Loop edit on" : "Loop edit off"); break;
    case I_FILL: snprintf(o, (size_t)cap, "%s", q->fill ? "Fill on" : "Fill off"); break;
    case I_SHIFTLOCK: snprintf(o, (size_t)cap, "%s", q->shift_lock ? "Shift locked" : "Shift unlocked"); break;
    case I_CLIPBOARD:                                   /* (0.6.3: a Delete's cut too, and what) */
        if (q->copy_src || q->cut) {                    /* (steps: their own words, "Step 5 cut") */
            const char *v = q->cut ? "Cut" : "Copied";
            if (q->clip_kind == 2)
                snprintf(o, (size_t)cap, "%s track %d clip %d", v, q->clip_src / RFM_MOVE_SEQ_SCENES + 1,
                         q->clip_src % RFM_MOVE_SEQ_SCENES + 1);
            else if (q->clip_kind == 3)
                snprintf(o, (size_t)cap, "%s track %d", v, q->clip_src + 1);
        }
        break;
    case I_PASTED:
        if (q->copy_pasted && q->clip_kind != 1)        /* (steps: "Pasted to step 9") */
            snprintf(o, (size_t)cap, "Pasted");
        break;
    default:
        if (i >= I_SMJOG && i < I_N && q->smenu)
            rfmv_seq_smenu_words(m, i - I_SM0, o, cap);
        break;
    }
}
/* the shown page's steps with notes (Sequencer mode's track view), and which page of which clip */
static uint16_t page_steps(const rfm_move_t *m, uint32_t *key)
{
    const rfm_move_seq_t *q = &m->seq;
    const int clip = SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel);
    uint16_t b = 0;
    int s, lo;
    if (m->s.mode != RFM_MOVE_MODE_SEQ || q->view || q->projects) {
        *key = 0;
        return 0;
    }
    *key = 1u << 31 | (uint32_t)clip << 8 | q->page;
    for (s = 0; s < RFM_MOVE_SEQ_PAGE; s++)
        if (rfmv_seq_range(m, clip, q->page * RFM_MOVE_SEQ_PAGE + s, &lo) > lo)
            b = (uint16_t)(b | 1u << s);
    return b;
}
static uint16_t bits_of(const rfm_move_t *m, int solo)
{
    uint16_t b = 0;
    int t;
    for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
        if (solo ? m->seq.tr[t].solo : m->seq.tr[t].mute)
            b = (uint16_t)(b | 1u << t);
    return b;
}
static int popcount16(unsigned v)
{
    int n = 0;
    for (; v; v &= v - 1)
        n++;
    return n;
}
/* said this call already (a popup, a press): an item with the same words is not said again */
typedef struct { const char *w[4]; int n; } said_now_t;
static int said_now(const said_now_t *sn, const char *w)
{
    int i;
    for (i = 0; i < sn->n; i++)
        if (!strcmp(sn->w[i], w))
            return 1;
    return 0;
}
static int said_track(const said_now_t *sn, int t)     /* "Track n, .." said this call (a held Track's menu) */
{
    char p[16];
    int i;
    snprintf(p, sizeof p, "Track %d,", t + 1);
    for (i = 0; i < sn->n; i++)
        if (!strncmp(sn->w[i], p, strlen(p)))
            return 1;
    return 0;
}
static void say_v(rfm_move_t *m, said_now_t *sn, const char *w)
{
    if (!w[0] || said_now(sn, w))
        return;
    say(m, RFM_MOVE_SAY_VALUE, w);
}
/* the watched values; silent: take them as said (a new view, speech turned on) */
static void watch(rfm_move_t *m, int silent, said_now_t *sn)
{
    const rfm_move_seq_t *q = &m->seq;
    char w[RFM_MOVE_SAY_MAX];
    uint32_t quiet, key;
    uint16_t b;
    int i, t;
    if (q->projects)
        silent = 1;                                     /* (the projects view: its own words; a project loaded there
                                                         * changes everything at once) */
    quiet = silent ? 0xFFFFFFFFu : 0;
    if (q->smenu == RFM_MOVE_SM_GLOBAL)
        quiet |= 1u << I_VEL;                           /* (0.6.2: the list says its own rows) */
    for (i = 0; i < I_N; i++) {
        uint32_t h;
        item_words(m, i, w, sizeof w);
        tidy(w);
        h = w[0] ? hash(w) : 0;
        if (h == m->say_h[i])
            continue;
        m->say_h[i] = h;
        if (quiet >> i & 1)
            continue;
        say_v(m, sn, w);
        if (i == I_TRACK)
            quiet |= TRACKSET;                          /* (another track: its own layout, key, CCs follow it) */
        if (i == I_SMJOG)
            quiet |= SMITEMS;                           /* (the arp's page turned: its knobs' new words) */
    }
    b = page_steps(m, &key);                            /* a step on or off */
    if (!silent && !m->say_now[0] && m->say_stepev && key && key == m->say_skey && b != m->say_steps &&
        popcount16((unsigned)(b ^ m->say_steps)) <= 2)  /* (a cut, a paste: their own words; 0.6.5: a step pressed
                                                         * only, never notes played or recorded) */
        for (i = 0; i < RFM_MOVE_SEQ_PAGE; i++)
            if ((b ^ m->say_steps) >> i & 1) {
                if (q->cut && q->clip_kind == 1 && q->clip_step % RFM_MOVE_SEQ_PAGE == i && !(b >> i & 1) &&
                    q->clip_step / RFM_MOVE_SEQ_PAGE == q->page)
                    continue;                           /* (a Delete's cut: "Cut step n" says it) */
                snprintf(w, sizeof w, "Step %d", i + 1);
                say_v(m, sn, w);
            }
    m->say_steps = b, m->say_skey = key;
    {                                                   /* mutes, solos */
        const uint16_t mu = bits_of(m, 0), so = bits_of(m, 1);
        if (!silent && popcount16((unsigned)((mu ^ m->say_mute) | (so ^ m->say_solo))) <= 3)
            for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++) {
                if (said_track(sn, t))
                    continue;                           /* (the held Track's menu said it: "Track 3, muted") */
                if ((mu ^ m->say_mute) >> t & 1)
                    snprintf(w, sizeof w, "Track %d %s", t + 1, mu >> t & 1 ? "muted" : "unmuted"), say_v(m, sn, w);
                if ((so ^ m->say_solo) >> t & 1)
                    snprintf(w, sizeof w, "Track %d %s", t + 1, so >> t & 1 ? "solo" : "solo off"), say_v(m, sn, w);
            }
        m->say_mute = mu, m->say_solo = so;
    }
    {                                                   /* clips launched, queued, stopped */
        int n = 0, last = -1, same = 1;
        for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
            if (q->tr[t].active != m->say_act[t] || q->tr[t].queued != m->say_q[t]) {
                if (n && (q->tr[t].active != q->tr[last].active || q->tr[t].queued != q->tr[last].queued))
                    same = 0;
                n++, last = t;
            }
        if (!silent && n > 2 && same && q->tr[last].queued >= 0) {
            snprintf(w, sizeof w, "Scene %d queued", q->tr[last].queued + 1), say_v(m, sn, w);
        } else if (!silent && n > 2 && same && q->tr[last].active >= 0 && q->tr[last].queued < -1) {
            snprintf(w, sizeof w, "Scene %d playing", q->tr[last].active + 1), say_v(m, sn, w);
        } else if (!silent && n && n <= 2)
            for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++) {
                const rfm_move_track_t *tr = &q->tr[t];
                if (tr->queued != m->say_q[t] && tr->queued >= -1) {
                    if (tr->queued >= 0)
                        snprintf(w, sizeof w, "Track %d, clip %d queued", t + 1, tr->queued + 1);
                    else
                        snprintf(w, sizeof w, "Track %d, stop queued", t + 1);
                    say_v(m, sn, w);
                } else if (tr->active != m->say_act[t]) {
                    if (tr->active >= 0)
                        snprintf(w, sizeof w, "Track %d, clip %d playing", t + 1, tr->active + 1);
                    else
                        snprintf(w, sizeof w, "Track %d stopped", t + 1);
                    say_v(m, sn, w);
                }
            }
        for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
            m->say_act[t] = q->tr[t].active, m->say_q[t] = q->tr[t].queued;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ && !silent) {    /* the sequencer's modifiers, pressed */
        static const char *const MOD[4] = { "Shift", "Copy", "Delete", "Mute" };
        const unsigned nw = (unsigned)(q->mods & ~m->say_mods);
        for (i = 0; i < 4; i++)
            if (nw >> i & 1)
                say_v(m, sn, MOD[i]);
    }
    m->say_mods = q->mods;
}

/* rfm_move_seq_advance (every call: a few compares): the transport started or stopped (an external clock), a clip
 * launched or stopped at its bar, and a popup or a message the clock brought up (a held step's popup, a held button's
 * label once RFM_MOVE_LABEL_HOLD_MS has passed, the external tempo's message): the words checked once for each */
void rfmv_say_tick(rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    int due = 0, t;
    if (!m->speech)
        return;
    say_transport(m);
    if (m->pop && (m->pop != m->tick_pop || m->pop_t0 != m->tick_t0) && rfmv_pop_up(m, m->pop) &&
        (m->pop != POP_LABEL || rfmv_now_ms(m) - m->pop_ms >= m->pop_delay)) {
        m->tick_pop = m->pop, m->tick_t0 = m->pop_t0;
        due = 1;
    }
    if (q->msg && m->msg_n != m->tick_msg && rfmv_seq_msg_text(m)) {
        m->tick_msg = m->msg_n;
        due = 1;
    }
    for (t = 0; t < RFM_MOVE_SEQ_TRACKS && !due; t++)
        due = q->tr[t].active != m->say_act[t] || q->tr[t].queued != m->say_q[t];
    if (m->say_turn && rfmv_now_ms(m) - m->say_turn_ms >= RFM_MOVE_SAY_TURN_MS) {
        m->say_turn = 0;                                /* (0.6.7) a knob turned, no readout (its range's end): its
                                                         * "Name: value" again, once; no readout known: the detents */
        if (m->pop && rfmv_pop_up(m, m->pop))
            m->say_force = 1, m->say_late = 1, due = 1;
    }
    if (due)
        rfmv_say_check(m);
}
void rfmv_say_check(rfm_move_t *m)
{
    char w[RFM_MOVE_SAY_MAX], pw[RFM_MOVE_SAY_MAX];
    said_now_t sn;
    uint16_t ps;
    uint32_t st;
    int s, up, fresh = 0;
    uint16_t said0;
    if (!m->speech)
        return;
    sn.n = 0, said0 = m->say_n;
    s = base_said(m, m->view_subj, w, sizeof w);        /* 1. the view, when it changes */
    if (s != m->view_subj) {
        tidy(w);
        say(m, RFM_MOVE_SAY_EVENT, w);
        m->view_subj = (uint16_t)s, m->focus_h = 0;     /* (the new view's selection follows it) */
        fresh = 1;
    }
    if (m->seq.projects && (m->fw.pop == FP_TRACK || m->fw.pop == FP_RPAGE || m->fw.pop == FP_CATALOG) &&
        m->fw.pop_ms != m->say_fwms) {                  /* (0.6.3) the view's Track / page pressed again: named */
        m->say_fwms = m->fw.pop_ms;
        if (!fresh) {
            base_words(m, w, sizeof w);
            tidy(w);
            say(m, RFM_MOVE_SAY_VALUE, w);
        }
    }
    say_transport(m);
    if (m->say_now[0]) {                                /* 2. a button pressed */
        say(m, RFM_MOVE_SAY_VALUE, m->say_now);
        sn.w[sn.n++] = m->say_now;
    }
    up = pop_words(m, pw, sizeof pw, &ps, &st);         /* 3. a popup: its words when they change */
    tidy(pw);
    if (up) {
        const int pressed = ps == (0x100 | POP_LABEL) && m->say_lbl_t0 && st == m->say_lbl_t0;
        if (m->say_rdnew && pw[0] && strcmp(pw, m->said))
            m->say_turn = 0;                            /* (0.6.8) the turn answered with new words: no repeat due */
        if (pw[0] && !pressed && (strcmp(pw, m->said) || (ps == m->said_subj && st != m->said_t0) || m->say_force)) {
            say(m, RFM_MOVE_SAY_VALUE, pw);
            rfmv_fw_say_focus(m, w, sizeof w);          /* (the popup said the selection's words: taken as said) */
            tidy(w);
            if (w[0] && !strcmp(w, pw))
                m->focus_h = hash(w);
        }
        snprintf(m->said, sizeof m->said, "%s", pw);
        m->said_subj = ps, m->said_t0 = st;
        sn.w[sn.n++] = m->said;
    } else {
        rfmv_fw_say_focus(m, w, sizeof w);              /* 4. the selection: when it changes */
        tidy(w);
        {
            const uint32_t h = w[0] ? hash(w) : 0;
            if (h != m->focus_h && strcmp(w, m->said))  /* (the popup just gone said the same: not again) */
                say(m, RFM_MOVE_SAY_VALUE, w);
            else if (!h && m->focus_h && !fresh) {      /* (0.6.3) the selection gone (Back): the view named */
                base_words(m, w, sizeof w);
                tidy(w);
                say(m, RFM_MOVE_SAY_VALUE, w);
            }
            m->focus_h = h;
        }
        m->said[0] = 0, m->said_subj = 0, m->said_t0 = 0;
    }
    m->say_force = 0, m->say_late = 0, m->say_rdnew = 0;
    watch(m, fresh, &sn);                               /* 5. the values a button or a menu changed */
    m->say_now[0] = 0;
    if (m->say_back && m->say_n == said0) {             /* (0.6.3) Back that changed nothing said: where you are */
        base_said(m, -1, w, sizeof w);
        tidy(w);
        say(m, RFM_MOVE_SAY_VALUE, w);
    }
    m->say_back = 0;
}

void rfm_move_set_speech(rfm_move_t *m, int on)
{
    char w[RFM_MOVE_SAY_MAX];
    said_now_t sn;
    uint16_t ps;
    uint32_t st;
    if ((on != 0) == m->speech)
        return;
    m->speech = on != 0;
    m->say_now[0] = 0;
    if (!m->speech)
        return;
    m->view_subj = (uint16_t)base_words(m, w, sizeof w);   /* (the screen now taken as said: the host names it) */
    m->say_tr = (uint8_t)transport(m);
    m->tick_pop = m->pop, m->tick_t0 = m->pop_t0, m->tick_msg = m->msg_n;
    if (pop_words(m, w, sizeof w, &ps, &st)) {
        tidy(w);
        snprintf(m->said, sizeof m->said, "%s", w);
        m->said_subj = ps, m->said_t0 = st, m->focus_h = 0;
    } else {
        m->said[0] = 0, m->said_subj = 0, m->said_t0 = 0;
        rfmv_fw_say_focus(m, w, sizeof w);
        tidy(w);
        m->focus_h = w[0] ? hash(w) : 0;
    }
    sn.n = 0;
    watch(m, 1, &sn);
}
int rfm_move_speech(const rfm_move_t *m) { return m->speech; }

int rfm_move_words(const rfm_move_t *m, char *out, int cap)
{
    char b[RFM_MOVE_SAY_MAX], p[RFM_MOVE_SAY_MAX];
    uint16_t ps;
    uint32_t st;
    int n;
    if (!out || cap <= 0)
        return 0;
    base_said(m, -1, b, sizeof b);
    tidy(b);
    if (!pop_words(m, p, sizeof p, &ps, &st))
        rfmv_fw_say_focus(m, p, sizeof p);
    tidy(p);
    n = snprintf(out, (size_t)cap, "%s%s%s", b, p[0] ? ", " : "", p);
    return n < cap ? n : cap - 1;
}
