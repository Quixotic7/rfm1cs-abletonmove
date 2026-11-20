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
    "", "No empty clip", "Full", "Load failed", "Captured", "Undo", "Redo", "Copied", "Pasted", "Cut", "Quantized",
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
enum { S_FW_TRACK0 = 0x11, S_FW_LAST = 0x23, S_FX0 = 0x100, S_FX_LAST = 0x3FF, S_SEQ_TRACK = 4, S_SEQ_SESSION = 5 };
static int base_words(const rfm_move_t *m, char *o, int cap)
{
    int s = rfmv_fw_say_base(m, o, cap);
    if (s)
        return s;
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
static int in_seq(int s) { return s == S_SEQ_TRACK || s == S_SEQ_SESSION; }
/* the words said for a view entered from the view `from` (-1: the long form always) */
static int base_said(const rfm_move_t *m, int from, char *o, int cap)
{
    char b[RFM_MOVE_SAY_MAX];
    const int s = base_words(m, b, sizeof b);
    if (s >= S_FW_TRACK0 && s < S_FW_TRACK0 + 3 && !in_fw(from))
        snprintf(o, (size_t)cap, "Firmwares, %s", b);
    else if (in_fx(s) && !in_fx(from) && s < 0x200)
        snprintf(o, (size_t)cap, "FX setup, %s", b);
    else if (in_seq(s) && !in_seq(from))
        snprintf(o, (size_t)cap, "Sequencer mode%s", s == S_SEQ_SESSION ? ", Session" : "");
    else
        snprintf(o, (size_t)cap, "%s", b);
    return s;
}

/* ================================================================ popups === */
static void readout_words(const rfm_move_t *m, const rfm_move_readout_t *r, int slot, char *o, int cap)
{
    static const int8_t SLOT_ENC[8] = { RFM_MOVE_E_SELECT, RFM_MOVE_E_ALGO, RFM_MOVE_E_PRESETS, RFM_MOVE_E_K1,
                                        RFM_MOVE_E_K2, RFM_MOVE_E_K3, RFM_MOVE_E_K4, RFM_MOVE_E_MASTER };
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
static void step_words(const rfm_move_t *m, char *o, int cap)
{
    const rfm_move_seq_t *q = &m->seq;
    const int s = m->pop_i, clip = SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel);
    int lo, hi = rfmv_seq_range(m, clip, q->page * RFM_MOVE_SEQ_PAGE + s, &lo), i, n;
    char nb[8];
    n = snprintf(o, (size_t)cap, "Step %d: %s", s + 1, hi > lo ? "" : "empty");
    for (i = lo; i < hi && n < cap - 1; i++)
        n += snprintf(o + n, (size_t)(cap - n), "%s%s", i > lo ? ", " : "", rfmv_note_name(q->notes[i].note, nb, sizeof nb));
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
        if (sl >= 0 && m->rd[sl].on) {
            readout_words(m, &m->rd[sl], sl, o, cap);
            return;
        }
        if (m->pop == POP_MASTER) {
            snprintf(o, (size_t)cap, "Volume %d%%", (m->master * 100 + 511) / 1023);
            return;
        }
        rfmv_tcase(enc_label(m, m->pop == POP_SELECT ? RFM_MOVE_E_SELECT : rfm_move_knob_enc(i)), l, sizeof l);
        if (m->pop_blank)
            snprintf(o, (size_t)cap, "%s", l);
        else
            snprintf(o, (size_t)cap, m->pop_val ? "%s: %+d" : "%s: 0", l, m->pop_val);
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
/* rfm_move_seq_advance (every call: a few compares): the transport started or stopped (an external clock), and a
 * popup or a message the clock brought up (a held step's popup, a held button's label once RFM_MOVE_LABEL_HOLD_MS
 * has passed, the external tempo's message): the words checked once for each */
void rfmv_say_tick(rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    int due = 0;
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
    if (due)
        rfmv_say_check(m);
}
void rfmv_say_check(rfm_move_t *m)
{
    char w[RFM_MOVE_SAY_MAX];
    uint16_t ps;
    uint32_t st;
    int s, up;
    if (!m->speech)
        return;
    s = base_said(m, m->view_subj, w, sizeof w);        /* 1. the view, when it changes */
    if (s != m->view_subj) {
        tidy(w);
        say(m, RFM_MOVE_SAY_EVENT, w);
        m->view_subj = (uint16_t)s, m->focus_h = 0;     /* (the new view's selection follows it) */
    }
    say_transport(m);
    up = pop_words(m, w, sizeof w, &ps, &st);           /* 2. a popup: its words when they change */
    tidy(w);
    if (up) {
        if (w[0] && (strcmp(w, m->said) || (ps == m->said_subj && st != m->said_t0))) {
            char f[RFM_MOVE_SAY_MAX];
            say(m, RFM_MOVE_SAY_VALUE, w);
            rfmv_fw_say_focus(m, f, sizeof f);          /* (the popup said the selection's words: taken as said) */
            tidy(f);
            if (f[0] && !strcmp(f, w))
                m->focus_h = hash(f);
        }
        snprintf(m->said, sizeof m->said, "%s", w);
        m->said_subj = ps, m->said_t0 = st;
        return;                                         /* (the selection under it: unchanged, not said again) */
    }
    rfmv_fw_say_focus(m, w, sizeof w);                  /* 3. the selection: when it changes */
    tidy(w);
    {
        const uint32_t h = w[0] ? hash(w) : 0;
        if (h != m->focus_h && strcmp(w, m->said))      /* (the popup just gone said the same: not again) */
            say(m, RFM_MOVE_SAY_VALUE, w);
        m->focus_h = h;
    }
    m->said[0] = 0, m->said_subj = 0, m->said_t0 = 0;
}

void rfm_move_set_speech(rfm_move_t *m, int on)
{
    char w[RFM_MOVE_SAY_MAX];
    uint16_t ps;
    uint32_t st;
    if ((on != 0) == m->speech)
        return;
    m->speech = on != 0;
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
