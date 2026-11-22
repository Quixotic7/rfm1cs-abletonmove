/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm_move's screen: the owner's design (README.md, "The screen"). The Move's screen is small, so at
 * rest every screen is symbols and pictures only; words come in large popups while a knob is touched or turned, a
 * step or a project pad is held (and RFM_MOVE_POPUP_MS after), or for a moment after an action (the sequencer's
 * messages). FM-1 mode is the owner's pixel drawing of the FM-1 (live: touched encoders, lit buttons, held keys) and
 * the FM-1's screen; MIDI mode the pad map; Control mode the banks and the 8 values; the sequencer's screens are
 * seq_ui.c's. Text is font5x7 at 2x (4x for the big numbers). */
#include <stdio.h>
#include <string.h>

#include "move_int.h"
#include "v2.h"
#include "../third_party/moveremote/common/font5x7.h"

#define clampi rfmv_clampi

/* ================================================================ drawing === */
static void fill(mr_canvas_t *cv, int x, int y, int w, int h, int on) { mr_gfx_rect(cv, x, y, w, h, on, 1); }

/* the CSS triangle (border-left w, top / bottom h / 2 transparent): row j is w x (1 - |centre - h/2| / (h/2)) long */
void rfmv_tri(mr_canvas_t *cv, int x, int y, int w, int h, int on)
{
    int j;
    for (j = 0; j < h; j++) {
        const int d = 2 * j + 1 - h, len = (w * (h - (d < 0 ? -d : d)) + h / 2) / h;
        fill(cv, x, y + j, len, 1, on);
    }
}

const char *rfmv_note_name(int n, char *b, int cap)
{
    snprintf(b, (size_t)cap, "%s%d", rfmv_note_names[n % 12], n / 12 - 2);   /* (Ableton's: MIDI 60 = C3) */
    return b;
}

/* ================================================================ popups === */
uint32_t rfmv_now_ms(const rfm_move_t *m) { return (uint32_t)(m->seq.now_us / 1000u); }

void rfmv_pop(rfm_move_t *m, int kind, int i)
{
    if (m->pop != kind || m->pop_i != i || !rfmv_pop_up(m, kind))
        m->pop_t0 = rfmv_now_ms(m);                     /* (0.5.6: a new subject: a long label scrolls from its start) */
    m->pop = (uint8_t)kind, m->pop_i = (uint8_t)i, m->pop_ms = rfmv_now_ms(m), m->pop_val = 0;
    m->pop_delay = 0, m->pop_blank = 0, m->pop_shown = 0;
}

void rfmv_pop_let(rfm_move_t *m, int kind, int i)
{
    if (m->pop == kind && m->pop_i == i)
        m->pop_ms = rfmv_now_ms(m);
}

void rfmv_menu(rfm_move_t *m, int what, int i) { m->menu_what = (uint8_t)what, m->menu_i = (uint8_t)i, m->menu_ms = rfmv_now_ms(m); }

static int pad_of_slot(int slot) { return (3 - slot / 8) * 8 + slot % 8; }

/* the popup's control is still held (touched): it stays up */
static int pop_held(const rfm_move_t *m)
{
    const int i = m->pop_i;
    switch (m->pop) {
    case POP_ENC:
    case POP_MASTER: return m->touched == i;            /* (0.5.6: FM-1 mode, MIDI's NOTES too: a touch shows it) */
    case POP_LABEL: return rfmv_label_held(m, i);
    case POP_CC:
    case POP_TRKINFO:
    case POP_TRIG: return m->touched == i;
    case POP_SELECT: return 0;                          /* (turning only: a moment after the last detent) */
    case POP_DRUM: return i < RFM_MOVE_NPADS && m->pad_down[i];
    case POP_LOOP: return (m->seq.mods & SEQ_M_LOOP) != 0;
    case POP_STEP: return i < RFM_MOVE_NSTEPS && (m->seq.step_held >> i & 1);
    case POP_PROJ: return i < RFM_MOVE_PROJECTS && m->pad_down[pad_of_slot(i)];
    default: return 0;
    }
}

int rfmv_pop_up(const rfm_move_t *m, int kind)
{
    const uint32_t dur = m->pop == POP_READ ? m->rd_ms : RFM_MOVE_POPUP_MS;   /* (0.5.7: a readout's own time) */
    return m->pop && m->pop == kind && (pop_held(m) || rfmv_now_ms(m) - m->pop_ms < dur);
}

/* (0.5.4) a host's message: printable ASCII kept, big, wrapped at spaces onto two rows (each cut to fit) */
void rfm_move_popup(rfm_move_t *m, const char *text)
{
    int i, k = 0;
    for (i = 0; text && text[i] && k < (int)sizeof m->pop_text - 1; i++)
        if (text[i] >= 32 && text[i] < 127)
            m->pop_text[k++] = text[i];
    m->pop_text[k] = 0;
    if (k)
        rfmv_pop(m, POP_HOST, 0);
    rfmv_say_check(m);
}
static void fit_row(char *r, int k)                     /* cut r until it fits 124 px at k x */
{
    int n = (int)strlen(r);
    while (n > 0 && v2_tw(r, k) > 124)
        r[--n] = 0;
}
static void host_popup(const rfm_move_t *m, mr_canvas_t *cv)
{
    char a[32], b[32];
    int i, cut = -1;
    mr_gfx_clear(cv, 0);
    snprintf(a, sizeof a, "%s", m->pop_text);
    b[0] = 0;
    if (v2_tw(a, 2) > 124) {                            /* the last space that leaves a first row that fits */
        for (i = 1; a[i]; i++)
            if (a[i] == ' ') {
                char t[32];
                snprintf(t, sizeof t, "%.*s", i, a);
                if (v2_tw(t, 2) <= 124)
                    cut = i;
            }
        if (cut > 0) {
            snprintf(b, sizeof b, "%s", a + cut + 1);
            a[cut] = 0;
        }
    }
    fit_row(a, 2), fit_row(b, 2);
    v2_msg(cv, a, b);
}

/* (0.7.0) a takeover entered: its name over "Takeover", at 2x (a long name cut to fit) */
static void tk_popup(const rfm_move_t *m, mr_canvas_t *cv)
{
    char a[RFM_MOVE_TK_NAME];
    snprintf(a, sizeof a, "%s", m->tk[m->pop_i % RFM_MOVE_FW_SLOTS].name);
    fit_row(a, 2);
    mr_gfx_clear(cv, 0);
    v2_msg(cv, a[0] ? a : "Takeover", a[0] ? "Takeover" : NULL);
}

/* ---- (0.8.0) the Track buttons' screens (the board, make_tracks_v2.py's s_pop): a line at 2x on top, the word as big
 * as fits (4x down) at 20, a line at 2x at 50, n dashes at the bottom (the current one 3 px tall), all centred */
static void ctext(mr_canvas_t *cv, int y, const char *s, int k)
{
    v2_T(cv, 64 - v2_tw(s, k) / 2, y, s, k, 1);
}
static void pop_screen_at(mr_canvas_t *cv, const char *top, const char *big, int n, int cur, const char *bottom, int y);
void rfmv_pop_screen(mr_canvas_t *cv, const char *top, const char *big, int n, int cur, const char *bottom)
{
    pop_screen_at(cv, top, big, n, cur, bottom, 20);
}
/* (the lock screen's word sits at 18, the board's s_lock) */
static void pop_screen_at(mr_canvas_t *cv, const char *top, const char *big, int n, int cur, const char *bottom, int y)
{
    char a[40];
    int k, i;
    const int x0 = 64 - (n * 10 + (n - 1) * 4) / 2;
    mr_gfx_clear(cv, 0);
    if (top && *top) {
        snprintf(a, sizeof a, "%s", top);
        fit_row(a, 2);
        ctext(cv, 0, a, 2);
    }
    for (k = 4; k > 1 && v2_tw(big, k) > 126; k--) {}
    snprintf(a, sizeof a, "%s", big);
    if (k == 1)
        fit_row(a, 1);
    ctext(cv, y, a, k);
    if (bottom && *bottom) {
        snprintf(a, sizeof a, "%s", bottom);
        fit_row(a, 2);
        ctext(cv, 50, a, 2);
    }
    for (i = 0; i < n; i++)
        fill(cv, x0 + i * 14, i == cur ? 60 : 61, 10, i == cur ? 3 : 1, 1);
}
/* a slot's layout in words: "FM-1", its takeover's name (all capitals in Title Case: "Chords"), "Scale" .. */
const char *rfmv_lay_name(const rfm_move_t *m, int slot, int lay)
{
    static const char *const N[RFM_MOVE_LAY_N] = { "FM-1", "Takeover", "Scale", "Chroma", "Drum", "Octave" };
    static char b[RFM_MOVE_TK_NAME + 4];
    int i, lower = 0;
    if (lay != RFM_MOVE_LAY_TK)
        return N[lay % RFM_MOVE_LAY_N];
    snprintf(b, sizeof b, "%s", m->tk[slot % RFM_MOVE_FW_SLOTS].name);
    if (!b[0])
        return N[1];
    for (i = 0; b[i]; i++)
        lower |= b[i] >= 'a' && b[i] <= 'z';
    if (!lower) {
        char t[sizeof b];
        rfmv_tcase(b, t, sizeof t);
        snprintf(b, sizeof b, "%s", t);
    }
    return b;
}
static void slot_title(const rfm_move_t *m, int t, char *o, int cap)   /* its firmware, or "Track n" (empty) */
{
    rfmv_fw_slot_name(m, t, o, cap);
    if (!o[0] || !m->fw.host)
        snprintf(o, (size_t)cap, "Track %d", t + 1);
    if (!m->fw.host)
        snprintf(o, (size_t)cap, "FM-1");
}
static void keys_menu_v(mr_canvas_t *cv, int root, int scale, int layout, int ch)
{
    char b[24];
    snprintf(b, sizeof b, "%s %s", rfmv_note_names[root % 12], rfm_move_scale_name(scale));
    if (v2_tw(b, 4) <= 128)
        v2_keysMenu(cv, b, layout, root % 12, ch, -1);
    else {                                              /* (a long scale name: the same screen, the name smaller) */
        v2_keysMenu(cv, "", layout, root % 12, ch, -1);
        v2_TC(cv, 64, v2_tw(b, 3) <= 128 ? 10 : 13, b, v2_tw(b, 3) <= 128 ? 3 : 2, 1);
    }
}
static const char *const LK_WORDS[] = { "selected", "solo", "solo off", "not routed", "routed", "muted", "unmuted",
                                        "cleared" };
/* (0.11.0, the owner's) a firmware slot at a glance (its held Track button; session view's Track tap): its firmware,
 * its layout (a padlock: latch on), the key the tracks use, and the 16 tracks: a block: it reaches the firmware, a
 * dot: it does not, underlined: the one selected for it, a bar over it: the firmware's solo. changed: a track's step
 * or column just acted: "Track n" and what happened in place of the layout and the key */
void rfmv_slot_screen(const rfm_move_t *m, mr_canvas_t *cv, int t, int chg)
{
    const int lay = rfmv_slot_lay(m, t), seq = m->s.mode == RFM_MOVE_MODE_SEQ;
    char nm[24], a[24];
    int i;
    mr_gfx_clear(cv, 0);
    slot_title(m, t, nm, sizeof nm);
    nm[10] = 0;
    v2_T(cv, 0, 0, nm, 2, 1);
    if (chg) {
        snprintf(a, sizeof a, "Track %d", (m->menu_i & 15) + 1);
        v2_T(cv, 0, 17, a, 2, 1);
        rfmv_tcase(m->lk_word < 8 ? LK_WORDS[m->lk_word] : "selected", a, sizeof a);
        v2_T(cv, 0, 34, a, 2, 1);
    } else {
        snprintf(a, sizeof a, "%s", rfmv_lay_name(m, t, lay));
        a[9] = 0;
        v2_T(cv, 0, 17, a, 2, 1);
        if (m->latch)
            v2_ic(cv, V2I_LOCK, 115, 18, 1);
        {
            char sn[20];
            rfmv_tcase(rfm_move_scale_name(seq ? m->s.scale : m->seq.scale), sn, sizeof sn);
            snprintf(a, sizeof a, "%s %s", rfmv_note_names[(seq ? m->s.root : m->seq.root) % 12], sn);
        }
        if (v2_tw(a, 2) <= 128)
            v2_T(cv, 0, 34, a, 2, 1);
        else
            v2_T(cv, 0, 38, a, 1, 1);
    }
    for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++) {
        const int x = i * 8 + 1;
        if (m->seq.fx.ffilt[t] >> i & 1)
            v2_R(cv, x, 53, 6, 5, 1);
        else
            v2_R(cv, x + 2, 55, 2, 2, 1);
        if (m->fsolo[t] == i + 1)
            v2_R(cv, x, 51, 6, 1, 1);
        if (m->s.lock[t] == i)
            v2_R(cv, x, 60, 6, 2, 1);
    }
}
/* a held Track's menu: a firmware's layout menu (its layout; a scale, the root, the channel or the octave changed:
 * the keys screen; a knob: its CC number), Track 4's (its page), the Sequencer's lock menu (the track locked) */
static void track_menu(const rfm_move_t *m, mr_canvas_t *cv, int t)
{
    char nm[24], a[16], b[16];
    const int recent = rfmv_now_ms(m) - m->menu_ms < RFM_MOVE_POPUP_MS;
    slot_title(m, t % 3, nm, sizeof nm);
    if (t == 3) {
        if (rfmv_fx_ok(m))
            rfmv_pop_screen(cv, "Track 4", rfmv_t4_page(m) ? "Routing" : "Mixer", 2, rfmv_t4_page(m), NULL);
        else
            rfmv_pop_screen(cv, "Track 4", "Shift", 0, 0, NULL);
        return;
    }
    {                                                   /* (0.9.0) one menu: the pads the layout, the steps the tracks */
        const int lay = rfmv_slot_lay(m, t), seq = m->s.mode == RFM_MOVE_MODE_SEQ;
        if (recent && m->menu_what == MENU_LOCK) {      /* a step: the track and what happened, over the 16 tracks */
            rfmv_slot_screen(m, cv, t, 1);
        } else if (seq && m->menu_what == MENU_CC && (recent || m->touched == (m->menu_i & 7))) {
            const int tr = m->s.lock[t] % 16, k = m->menu_i & 7;   /* a knob: the track's CC number (the board's I) */
            snprintf(a, sizeof a, "Knob %d", k + 1);
            snprintf(b, sizeof b, "CC %d", tr == m->seq.sel_track ? m->s.cc[m->s.bank & 3][k] : m->seq.tr[tr].cc[m->s.bank & 3][k]);
            rfmv_pop_screen(cv, a, b, 0, 0, nm);
        } else if (recent && (m->menu_what == MENU_SCALE || m->menu_what == MENU_ROOT || m->menu_what == MENU_OCT)) {
            mr_gfx_clear(cv, 0);                        /* the key */
            keys_menu_v(cv, seq ? m->s.root : m->seq.root, seq ? m->s.scale : m->seq.scale,
                        lay >= RFM_MOVE_LAY_SCALE ? lay - RFM_MOVE_LAY_SCALE : -1, m->seq.tr[m->s.lock[t] % 16].ch);
        } else                                          /* (0.11.0) at rest: the slot at a glance */
            rfmv_slot_screen(m, cv, t, 0);
    }
}

/* ---- v2 (the design canvas's V2Screen, see v2.h): full-screen popups with no frame, the left-half ones (FM-1
 * mode, MIDI's NOTES) the name and the value only; Title Case words */
static int g_half;
static int g_full;                                      /* (a popup over the whole screen: nothing drawn back) */
static int g_label_drawn;                               /* (0.5.8) a label's words drawn this time */

/* Title Case ("CUTOFF" -> "Cutoff", "UP DN" -> "Up Dn"); words with a digit stay as they are (K1, P12) */
void rfmv_tcase(const char *s, char *o, int cap)
{
    int i = 0, start = 1, digit = 0;
    const char *w;
    for (; *s && i < cap - 1; s++) {
        if (start) {                                    /* (a word with a digit or a '/': kept) */
            for (digit = 0, w = s; *w && *w != ' '; w++)
                digit |= (*w >= '0' && *w <= '9') || *w == '/';
        }
        o[i++] = digit ? *s : start ? (char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s)
                                    : (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
        start = *s == ' ' || *s == '-';
    }
    o[i] = 0;
}

/* the knob pages' strip: Sequencer mode TRIG and the 4 banks, else the 4 banks */
static void bank_strip(const rfm_move_t *m, mr_canvas_t *cv)
{
    if (m->s.mode == RFM_MOVE_MODE_SEQ)
        v2_strip(cv, RFM_MOVE_NBANKS + 2, 1u << (m->seq.trig ? 0 : m->seq.fwp ? 1 : m->s.bank + 2), -1, 0, 128);
    else
        v2_strip(cv, 4, 1u << m->s.bank, -1, 0, 128);
}

/* (0.5.6) a row of 2x text in the left half (x 1..62): centred if it fits, else it scrolls (the owner's: no 1x
 * text there): still for LSCROLL_HOLD ms, LSCROLL_MS a pixel to its end, still again, from its start again */
#define LSCROLL_HOLD 700u
#define LSCROLL_MS 30u
static uint32_t g_scroll_ms;                            /* (the popup's age, set by the caller) */
/* (0.5.9) the label and readout popups' width: 64 the left half, 128 the whole screen (the owner's default; the
 * setting pop_half picks the half); g_wide_drawn: one drew over the whole screen (no thumbnail drawn back) */
static int g_lw = 64, g_wide_drawn;
static void lclear(mr_canvas_t *cv)
{
    v2_R(cv, 0, 0, g_lw, 64, 0);
    if (g_lw > 64)
        g_wide_drawn = 1;
}
/* a row of k x text across the popup (x 1..g_lw-2): centred if it fits, else it scrolls (no 1x text) */
static void lrowk(mr_canvas_t *cv, int y, const char *s, int k)
{
    const int w = v2_tw(s, k), room = g_lw - 2;
    int off, x;
    if (w <= room) {
        v2_TC(cv, g_lw / 2.0, y, s, k, 1);
        return;
    }
    {
        const uint32_t run = (uint32_t)(w - room) * LSCROLL_MS, cyc = LSCROLL_HOLD * 2 + run, t = g_scroll_ms % cyc;
        off = t < LSCROLL_HOLD ? 0 : t >= LSCROLL_HOLD + run ? w - room : (int)((t - LSCROLL_HOLD) / LSCROLL_MS);
    }
    for (x = 0; s[x]; x++) {                            /* (each glyph clipped to the popup) */
        const uint8_t *g = mr_glyph(s[x]);
        int i, j;
        for (i = 0; i < 5; i++)
            for (j = 0; j < 8; j++)
                if (g[i] >> j & 1) {
                    const int px = 1 + x * 6 * k + i * k - off;
                    if (px >= 1 && px + k - 1 <= room)
                        v2_R(cv, px, y + j * k, k, k, 1);
                }
    }
}
static void lrow(mr_canvas_t *cv, int y, const char *s) { lrowk(cv, y, s, 2); }
/* the value: the biggest of 4x / 3x that fits on a row, else 2x rows (up to two); y0 its top, y1 the room's end */
static int wrap_rows(const char *text, char r[3][RFM_MOVE_LABEL_MAX + 8], int max, int k);
static void big_value(mr_canvas_t *cv, const char *v, int y0, int y1)
{
    const int room = g_lw - 2, mid = (y0 + y1) / 2;
    int k;
    for (k = 4; k >= 3; k--)
        if (v2_tw(v, k) <= room) {
            v2_TC(cv, g_lw / 2.0, mid - 4 * k, v, k, 1);
            return;
        }
    {
        char rows[3][RFM_MOVE_LABEL_MAX + 8];
        const int n = wrap_rows(v, rows, 2, 2);
        int i;
        for (i = 0; i < n; i++)
            lrow(cv, n == 1 ? mid - 8 : mid - 17 + i * 18, rows[i]);
    }
}
static void lbar(mr_canvas_t *cv, double frac, int bip)   /* a thin bar along the bottom (bipolar: from the middle) */
{
    const int room = g_lw - 2, h = g_lw > 64 ? 3 : 2, y = 63 - h, w = (int)(frac * room + 0.5);
    if (bip) {
        const int x = 1 + w, c = g_lw / 2;
        v2_R(cv, x < c ? x : c, y, x < c ? c - x : x - c + 1, h, 1);
        v2_R(cv, c, y - 2, 1, 2, 1);                    /* (the middle marked) */
    } else
        v2_R(cv, 1, y, w, h, 1);
}
static void lpop(mr_canvas_t *cv, const char *label, const char *val)   /* the name (a row), the value */
{
    lclear(cv);
    if (!val || !val[0]) {                              /* (a name alone: as a label) */
        lrow(cv, 8, label);
        return;
    }
    lrow(cv, 3, label);
    big_value(cv, val, 21, 59);
}
/* text wrapped at spaces onto up to max rows of k x (the last takes the rest); returns the rows' count */
static int wrap_rows(const char *text, char r[3][RFM_MOVE_LABEL_MAX + 8], int max, int k)
{
    const char *p = text;
    int n = 0;
    while (*p == ' ')
        p++;
    while (*p && n < max) {                             /* greedy: words while the row fits; the last takes the rest */
        int len = 0, end;
        while (p[len] && p[len] != ' ')
            len++;                                      /* (the first word, whatever its width) */
        end = len;
        while (n < max - 1 && p[end] == ' ') {
            char t[RFM_MOVE_LABEL_MAX + 8];
            int e = end + 1;
            while (p[e] && p[e] != ' ')
                e++;
            snprintf(t, sizeof t, "%.*s", e, p);
            if (v2_tw(t, k) > g_lw - 2)
                break;
            end = e;
        }
        if (n == max - 1)
            end = (int)strlen(p);
        snprintf(r[n], RFM_MOVE_LABEL_MAX + 8, "%.*s", end, p);
        p += end, n++;
        while (*p == ' ')
            p++;
    }
    return n;
}
/* a button's or key's label (a message): one row at 4x or 3x if it fits, two at 3x if both fit, else up to three
 * 2x rows (one that does not fit scrolls) */
static void label_rows(mr_canvas_t *cv, const char *text)
{
    char r[3][RFM_MOVE_LABEL_MAX + 8];
    int n, i, k;
    lclear(cv);
    for (k = 4; k >= 3; k--)
        if (v2_tw(text, k) <= g_lw - 2) {
            v2_TC(cv, g_lw / 2.0, 32 - 4 * k, text, k, 1);
            return;
        }
    n = wrap_rows(text, r, 2, 3);
    if (n == 2 && v2_tw(r[0], 3) <= g_lw - 2 && v2_tw(r[1], 3) <= g_lw - 2) {
        v2_TC(cv, g_lw / 2.0, 5, r[0], 3, 1);
        v2_TC(cv, g_lw / 2.0, 35, r[1], 3, 1);
        return;
    }
    n = wrap_rows(text, r, 3, 2);
    for (i = 0; i < n; i++)
        lrow(cv, n == 1 ? 24 : n == 2 ? 14 + i * 22 : 3 + i * 21, r[i]);
}

/* (0.5.7) a readout: the title (a row), the value (4x or 3x when it fits, else up to two 2x rows), a thin bar at the
 * bottom with frac; (0.5.9) the left half or the whole screen (g_lw). A message: its words wrapped. Not in the FM-1
 * popups' place (another mode's full screen): the edit popup and its bar */
static void readout_draw(mr_canvas_t *cv, const rfm_move_readout_t *r)
{
    if (!g_half) {
        mr_gfx_clear(cv, 0);
        v2_titleBox(cv, r->title, r->value, 64, 124);
        if (r->frac >= 0)
            v2_vbar(cv, r->frac / 1000.0, r->flags & RFM_MOVE_RD_BIPOLAR, 4, 120, 52);
        return;
    }
    if (r->kind == RFM_MOVE_RD_PARAM && !r->value[0])
        lpop(cv, r->title, "");                         /* (a knob's: the name, no value) */
    else if (r->kind == RFM_MOVE_RD_MESSAGE || !r->value[0] || !r->title[0]) {
        char t[RFM_MOVE_LABEL_MAX * 2 + 2];
        snprintf(t, sizeof t, "%s%s%s", r->title, r->title[0] && r->value[0] ? " " : "", r->value);
        label_rows(cv, t);
    } else
        lpop(cv, r->title, r->value);
    if (r->frac >= 0)
        lbar(cv, r->frac / 1000.0, r->flags & RFM_MOVE_RD_BIPOLAR);
}

/* a value popup: the FM-1 popups' place (the name, the value; whole screen: a bar), else the whole screen (the name,
 * the value, a bar) */
static void value_pop(mr_canvas_t *cv, const char *label, const char *val, double frac, int bar)
{
    if (g_half) {
        lpop(cv, label, val);
        if (bar && g_lw > 64 && val && val[0])
            lbar(cv, bar == 2 ? frac / 2 + 0.5 : frac, bar == 2);
        return;
    }
    mr_gfx_clear(cv, 0);
    v2_titleBox(cv, label, val, 64, 124);
    if (bar)
        v2_vbar(cv, frac, bar == 2, 4, 120, 52);
}

static const char *enc_label(const rfm_move_t *m, int e)
{
    return e >= 0 && e <= RFM_MOVE_NENC && m->label[e][0] ? m->label[e] : rfm_move_enc_name(e);
}

/* the knob popups' contents, by kind */
/* (0.5.8) a knob's readout: what it controls (its label when the readout has no title), then the value */
static void knob_readout(const rfm_move_t *m, mr_canvas_t *cv, const rfm_move_readout_t *rd, int slot)
{
    static const int8_t SLOT_ENC[8] = { RFM_MOVE_E_SELECT, RFM_MOVE_E_ALGO, RFM_MOVE_E_PRESETS, RFM_MOVE_E_K1,
                                        RFM_MOVE_E_K2, RFM_MOVE_E_K3, RFM_MOVE_E_K4, RFM_MOVE_E_MASTER };
    rfm_move_readout_t r = *rd;
    if (!r.title[0]) {
        char l[sizeof r.title];                         /* (0.6.12: the title's size: no truncation warning) */
        rfmv_tcase(enc_label(m, SLOT_ENC[slot & 7]), l, sizeof l);
        snprintf(r.title, sizeof r.title, "%s", l);
    }
    r.kind = RFM_MOVE_RD_PARAM;
    readout_draw(cv, &r);
}

static void value_popup(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    char l[24], v[24];
    const int i = m->pop_i;
    switch (m->pop) {
    case POP_SELECT:
    case POP_ENC:
    case POP_MASTER: {                                  /* (0.5.7) a readout kept for the knob: it, not the detents */
        const int sl = rfmv_pop_slot(m);
        if (sl >= 0 && m->rd[sl].on) {                  /* (0.5.8, the owner's: every knob, PRESETS too, the same: what
                                                         * it controls (its label when the readout has no title), then
                                                         * the value, whatever the readout's kind) */
            knob_readout(m, cv, &m->rd[sl], sl);
            return;
        }
        break;
    }
    default: break;
    }
    switch (m->pop) {
    case POP_SELECT:
    case POP_ENC: {                                     /* an FM-1 encoder: this turn's detents */
        const int e = m->pop == POP_SELECT ? RFM_MOVE_E_SELECT : rfm_move_knob_enc(i), t = m->pop_val;
        rfmv_tcase(enc_label(m, e), l, sizeof l);
        snprintf(v, sizeof v, t ? "%+d" : "0", t);
        if (m->pop_blank)                               /* (0.5.6: touched, not turned: the name alone) */
            v[0] = 0;
        value_pop(cv, l, v, clampi(t, -32, 32) / 32.0, 2);
        return;
    }
    case POP_MASTER:
        rfmv_tcase(enc_label(m, RFM_MOVE_E_MASTER), l, sizeof l);
        snprintf(v, sizeof v, "%d%%", (m->master * 100 + 511) / 1023);
        value_pop(cv, l, v, m->master / 1023.0, 1);
        return;
    case POP_CC: {
        const int b = m->s.bank, sl = m->fw.active % 3, notes = m->s.mode == RFM_MOVE_MODE_MIDI;
        const int cc = notes ? m->s.slot[sl].cc[i & 7] : m->s.cc[b][i & 7];   /* (0.8.0: a note layout: the slot's) */
        int val = notes ? m->slot_ccv[sl][i & 7] : m->cc_val[b][i & 7], lock = 0;
        snprintf(l, sizeof l, "CC %d", cc);
        if (m->s.mode == RFM_MOVE_MODE_SEQ && !q->view && q->step_first >= 0) {   /* a held step's lock */
            const int clip = SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel);
            val = rfmv_seq_lock_get(m, clip, q->page * RFM_MOVE_SEQ_PAGE + q->step_first, cc);
            lock = 1;
        }
        if (val < 0)
            snprintf(v, sizeof v, "--"), val = 0;
        else
            snprintf(v, sizeof v, "%d", val);
        value_pop(cv, l, v, val / 127.0, 1);
        if (!g_half) {
            if (lock)
                v2_ic(cv, V2I_LOCK, 114, 4, 1);         /* (a padlock: the step's lock) */
            if (!notes)
                bank_strip(m, cv);
        }
        return;
    }
    case POP_DRUM: {
        const int n = m->s.drum[i & 31];
        rfmv_note_name(n, v, sizeof v);
        value_pop(cv, "Drum", v, n / 127.0, 1);
        return;
    }
    case POP_TEMPO: {
        const int b10 = rfm_move_seq_bpm10(m);
        snprintf(v, sizeof v, b10 % 10 ? "%d.%d" : "%d", b10 / 10, b10 % 10);
        value_pop(cv, "Tempo", v, 0, 0);
        return;
    }
    default:                                            /* POP_BANK (the sequencer: TRIG before bank 1) */
        if (m->s.mode == RFM_MOVE_MODE_SEQ && !q->trig && q->fwp) {   /* (0.12.x) the FM-1 page (its level 2: "Keys"
                                                         * too; an empty slot: "Empty slot"), the whole screen */
            mr_gfx_clear(cv, 0);
            v2_titleBox(cv, !rfmv_fwp_live(m) ? "Empty slot" : rfmv_fwp_full(m) ? "Knobs+Keys" : "Knobs", "FM-1", 64, 124);
            bank_strip(m, cv);
            g_full = 1;
            return;
        }
        if (m->s.mode == RFM_MOVE_MODE_SEQ && q->trig)
            value_pop(cv, "Knobs", "Trig", 0, 0);
        else {
            snprintf(v, sizeof v, "%d", m->s.bank + 1);
            value_pop(cv, "Bank", v, 0, 0);
        }
        if (!g_half)
            bank_strip(m, cv);
        return;
    }
}

/* ---- the TRIG page: a picture on the left, the name and the value on the right (no boxes); the knob pages as the
 * strip, TRIG first. A step held and the arrows, the jog or the volume knob (not on the TRIG page): Nudge, Length,
 * Velocity as edit popups over the page strip */
static const char *const FN_NAMES[RFM_MOVE_FN_N] = { "None", "Restart", "Reverse", "Forward", "Flip", "Random",
                                                      "Rnd Fn", "Leap", "Jump", "Repeat" };
static void repeat_pic(mr_canvas_t *cv, int n)         /* the step filled, its copies dashed after it */
{
    int i;
    v2_R(cv, 3, 24, 6, 12, 1);
    for (i = 0; i < n && i < 4; i++)
        v2_dashO(cv, 11 + i * 8, 24, 6, 12);
}
void rfmv_cond_name(int c, char *b, int cap)
{
    static const char *const N[] = { "Fill", "!Fill", "Pre", "!Pre", "Nei", "!Nei", "1st", "!1st" };
    if (c >= 1 && c <= RFM_MOVE_COND_AB_N) {
        int bb = 2, a = c;
        while (a > bb)
            a -= bb, bb++;
        snprintf(b, (size_t)cap, "%d:%d", a, bb);
    } else if (c >= RFM_MOVE_COND_FILL && c < RFM_MOVE_COND_N)
        snprintf(b, (size_t)cap, "%s", N[c - RFM_MOVE_COND_FILL]);
    else
        snprintf(b, (size_t)cap, "Always");
}
static void len_text(int v, char *b, int cap)
{
    if (rfmv_seq_len_inf(v))
        snprintf(b, (size_t)cap, "Inf");
    else if (v % RFM_MOVE_SEQ_TPS == 0)
        snprintf(b, (size_t)cap, "%d", v / RFM_MOVE_SEQ_TPS);
    else if (v < RFM_MOVE_SEQ_TPS)
        snprintf(b, (size_t)cap, "1/%d", (RFM_MOVE_SEQ_TPS + v / 2) / v);
    else
        snprintf(b, (size_t)cap, "%d.%d", v / RFM_MOVE_SEQ_TPS, v % RFM_MOVE_SEQ_TPS * 10 / RFM_MOVE_SEQ_TPS);
}
static void steps_base(mr_canvas_t *cv)               /* a base line and the step ticks under a picture */
{
    int i;
    v2_R(cv, 2, 45, 41, 1, 1);
    for (i = 0; i < 6; i++)
        v2_R(cv, 2 + i * 8, 40, 1, 6, 1);
}
static void trig_popup(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    const int k = m->pop_i, clip = SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel);
    int abs = q->page * RFM_MOVE_SEQ_PAGE + (q->step_first >= 0 ? q->step_first : 0), v, i;
    char l[16], b[16];
    if (q->step_first < 0) {                            /* none held: the clip's first step with notes */
        int lo, hi = rfmv_seq_clip_range(m, clip, &lo);
        abs = hi > lo ? q->notes[lo].step : abs;
    }
    v = q->trig && q->step_first < 0 ? rfmv_seq_tdef_val(m, k) : rfmv_seq_trig_val(m, clip, abs, k);   /* (none held:
                                                         * the defaults for new steps) */
    mr_gfx_clear(cv, 0);
    g_full = 1;
    if (k == 8) {                                       /* (0.12.0) a held step transposed (Up / Down): its notes now */
        char nm[48];
        rfmv_seq_held_names(m, nm, sizeof nm, 124);
        v2_TC(cv, 64, 3, "Transpose", 2, 1);
        v2_TC(cv, 64, v2_tw(nm, 4) <= 124 ? 21 : 27, nm, v2_tw(nm, 4) <= 124 ? 4 : 2, 1);
        rfmv_seq_page_strip(m, cv, abs);
        return;
    }
    if (!rfmv_seq_trig_on(m)) {                         /* a held step's edit popup (V2Track's seq.micro / len / vel) */
        if (k == 0)
            snprintf(l, sizeof l, "Nudge"), snprintf(b, sizeof b, v ? "%+d%%" : "0%%", v * 100 / RFM_MOVE_SEQ_TPS);
        else if (k == 6)
            snprintf(l, sizeof l, "Length"), len_text(v, b, sizeof b);
        else
            snprintf(l, sizeof l, "Velocity"), snprintf(b, sizeof b, "%d", v);
        v2_titleBox(cv, l, b, 64, 124);
        if (k == 7)
            v2_vbar(cv, v / 127.0, 0, 4, 120, 52);
        rfmv_seq_page_strip(m, cv, abs);
        return;
    }
    l[0] = b[0] = 0;
    switch (k) {
    case 0:                                             /* the note between its step's ticks, pushed by the offset */
        snprintf(l, sizeof l, "Micro"), snprintf(b, sizeof b, v ? "%+d" : "0", v);
        v2_R(cv, 2, 47, 41, 1, 1), v2_R(cv, 2, 40, 1, 8, 1), v2_R(cv, 42, 40, 1, 8, 1);
        v2_R(cv, 7 + clampi(v, -5, 9), 26, 26, 4, 1);
        if (v > 0)
            v2_L(cv, 4, 36, 10, 36), v2_L(cv, 8, 34, 10, 36), v2_L(cv, 8, 38, 10, 36);
        else if (v < 0)
            v2_L(cv, 40, 36, 34, 36), v2_L(cv, 36, 34, 34, 36), v2_L(cv, 36, 38, 34, 36);
        break;
    case 1: {                                           /* the hits inside one step, fading */
        const int n = clampi(v, 1, 8), sp = 40 / n;
        snprintf(l, sizeof l, "Ratch"), snprintf(b, sizeof b, "x%d", v);
        for (i = 0; i < n; i++)
            v2_R(cv, 4 + i * sp, 12 + i * 16 / n, sp * 4 / 10 > 1 ? sp * 4 / 10 : 1, 34 - i * 16 / n, 1);
        v2_R(cv, 2, 47, 40, 1, 1);
        break;
    }
    case 2: {                                           /* a die */
        static const int P[5][2] = { { 8, 14 }, { 30, 14 }, { 19, 25 }, { 8, 36 }, { 30, 36 } };
        snprintf(l, sizeof l, "Chance"), snprintf(b, sizeof b, "%d%%", v);
        for (i = 0; i < 5; i++)
            v2_R(cv, P[i][0], P[i][1], 6, 6, 1);
        break;
    }
    case 3:                                             /* A:B as B passes, the A-th tall */
        snprintf(l, sizeof l, "Cond"), rfmv_cond_name(v, b, sizeof b);
        if (v >= 1 && v <= RFM_MOVE_COND_AB_N) {
            int bb = 2, a = v;
            while (a > bb)
                a -= bb, bb++;
            for (i = 0; i < bb; i++) {
                const int x = 2 + i * (40 / bb);
                if (i == a - 1)
                    v2_R(cv, x, 18, 40 / bb - 2, 20, 1);
                else
                    v2_R(cv, x, 36, 40 / bb - 2, 2, 1);
            }
        }
        break;
    case 4:                                             /* the playhead's path over the steps */
        snprintf(l, sizeof l, "Func"), snprintf(b, sizeof b, "%s", FN_NAMES[clampi(v, 0, RFM_MOVE_FN_N - 1)]);
        steps_base(cv);
        switch (v) {
        case RFM_MOVE_FN_RESTART: v2_L(cv, 37, 30, 5, 30), v2_L(cv, 5, 30, 9, 26), v2_L(cv, 5, 30, 9, 34), v2_R(cv, 3, 20, 1, 20, 1); break;
        case RFM_MOVE_FN_REVERSE: v2_L(cv, 37, 30, 5, 30), v2_L(cv, 5, 30, 9, 26), v2_L(cv, 5, 30, 9, 34); break;
        case RFM_MOVE_FN_FORWARD: v2_L(cv, 5, 30, 37, 30), v2_L(cv, 37, 30, 33, 26), v2_L(cv, 37, 30, 33, 34); break;
        case RFM_MOVE_FN_FLIP:
            v2_L(cv, 5, 24, 37, 24), v2_L(cv, 37, 24, 33, 20), v2_L(cv, 37, 24, 33, 28);
            v2_L(cv, 37, 34, 5, 34), v2_L(cv, 5, 34, 9, 30), v2_L(cv, 5, 34, 9, 38);
            break;
        case RFM_MOVE_FN_RANDOM: case RFM_MOVE_FN_RFUNC: v2_ic(cv, V2I_DICE, 16, 22, 1); break;
        case RFM_MOVE_FN_LEAP: case RFM_MOVE_FN_JUMP:
            v2_L(cv, 5, 38, 13, 20), v2_L(cv, 13, 20, 29, 20), v2_L(cv, 29, 20, 37, 38);
            v2_L(cv, 33, 36, 37, 38), v2_L(cv, 37, 34, 37, 38);
            break;
        case RFM_MOVE_FN_REPEAT: repeat_pic(cv, 3); break;
        default: break;
        }
        break;
    case 5: {                                           /* the function's amount */
        const int f = q->step_first < 0 ? rfmv_seq_tdef_val(m, 4) : rfmv_seq_trig_val(m, clip, abs, 4);
        steps_base(cv);
        if (f == RFM_MOVE_FN_LEAP || f == RFM_MOVE_FN_JUMP) {
            v2_L(cv, 5, 38, 13, 20), v2_L(cv, 13, 20, 29, 20), v2_L(cv, 29, 20, 37, 38);
            v2_L(cv, 33, 36, 37, 38), v2_L(cv, 37, 34, 37, 38);
        }
        if (f == RFM_MOVE_FN_LEAP)
            snprintf(l, sizeof l, "Leap"), snprintf(b, sizeof b, "%+d", v);
        else if (f == RFM_MOVE_FN_JUMP)
            snprintf(l, sizeof l, "Jump"), snprintf(b, sizeof b, "%d", v + 1);
        else if (f == RFM_MOVE_FN_REPEAT)
            snprintf(l, sizeof l, "Repeat"), snprintf(b, sizeof b, "x%d", v), repeat_pic(cv, v);
        else
            snprintf(l, sizeof l, "Amount"), snprintf(b, sizeof b, "--");
        break;
    }
    case 6:                                             /* a gate bar over the steps it covers */
        snprintf(l, sizeof l, "Length"), len_text(v, b, sizeof b);
        steps_base(cv);
        v2_R(cv, 3, 26, rfmv_seq_len_inf(v) ? 40 : clampi(v * 8 / RFM_MOVE_SEQ_TPS - 2, 1, 40), 6, 1);
        break;
    case 7:                                             /* velocity */
        snprintf(l, sizeof l, "Velo"), snprintf(b, sizeof b, "%d", v);
        v2_ic(cv, V2I_VEL, 16, 22, 1);
        break;
    default: snprintf(l, sizeof l, "--"); break;
    }
    v2_titleBox(cv, l, b, 88, 80);
    v2_strip(cv, RFM_MOVE_NBANKS + 2, q->trig ? 1 : 2, -1, 0, 128);   /* (0.12.x: the FM-1 page second; a step held on it:
                                                         * its TRIG knobs) */
}

/* Loop held or latched (page edit): the loop icon, the range in words, the 16 pages as cells; the jog: Move's
 * "Loop Length" (the length at 4x, the looped pages thick on a strip of 16) */
static void loop_popup(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    const int clip = SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel);
    const int a = rfmv_seq_loop_lo(m, clip), h = rfmv_seq_loop_hi(m, clip), pa = a / RFM_MOVE_SEQ_PAGE;
    const int pb = (h - 1) / RFM_MOVE_SEQ_PAGE;
    char b[24];
    int i;
    mr_gfx_clear(cv, 0);
    g_full = 1;
    if (m->pop_val == 1 && rfmv_now_ms(m) - m->pop_ms < RFM_MOVE_POPUP_MS) {   /* the jog: the length (a second) */
        const int L = h - a, P = L / RFM_MOVE_SEQ_PAGE, S = L % RFM_MOVE_SEQ_PAGE;
        uint32_t sel = 0;
        if (!P)
            snprintf(b, sizeof b, "%d Step%s", S, S == 1 ? "" : "s");
        else if (S)
            snprintf(b, sizeof b, "%d Bar%s +%d", P, P == 1 ? "" : "s", S);   /* (0.13.0: it said "1.3") */
        else
            snprintf(b, sizeof b, "%d %s", P, P == 1 ? "Bar" : "Bars");
        for (i = pa; i <= pb && i < 16; i++)
            sel |= 1u << i;
        v2_titleBox(cv, "Loop", b, 64, 124);
        if (h % RFM_MOVE_SEQ_PAGE) {                    /* (0.13.0, the owner's) the loop ends inside a bar: that bar's
                                                         * 16 steps: in the loop a cell (filled: a note), after its
                                                         * end a dot */
            const int p0 = pb * RFM_MOVE_SEQ_PAGE;
            for (i = 0; i < 16; i++) {
                int nlo;
                const int x = i * 8, has = rfmv_seq_range(m, clip, p0 + i, &nlo) > nlo;
                if (p0 + i >= h)
                    v2_R(cv, x + 3, 57, 2, 2, 1);
                else if (has)
                    v2_R(cv, x, 52, 7, 11, 1);
                else
                    v2_O(cv, x, 52, 7, 11, 1);
            }
        } else
            v2_strip(cv, 16, sel, -1, 0, 128);
        return;
    }
    if ((m->pop_val == 2 || m->pop_val == 3) && rfmv_now_ms(m) - m->pop_ms < RFM_MOVE_POPUP_MS) {
        /* (0.13.0, the owner's) a page held + the jog: "Bar n Length", the change so far ("+1/12", "-2"; an empty
         * page "--"); under it the page's 16 steps (a note: filled). "Bar n" 2x, "Length" 1x beside it */
        const int p0 = m->pop_i * RFM_MOVE_SEQ_PAGE, dl = m->seq.loop_dlen;
        char l[24], v[16];
        snprintf(l, sizeof l, "Bar %d", m->pop_i + 1);
        if (m->pop_val == 3)
            snprintf(b, sizeof b, "--");
        else {
            len_text(dl < 0 ? -dl : dl, v, sizeof v);
            snprintf(b, sizeof b, "%s%s", dl < 0 ? "-" : "+", dl ? v : "0");
        }
        v2_T(cv, 2, 3, l, 2, 1);                        /* (the bar at 2x, "Length" beside it: one 2x line has no
                                                         * room for both) */
        v2_T(cv, 126 - v2_tw("Length", 1), 7, "Length", 1, 1);
        v2_titleBox(cv, "", b, 64, 124);
        for (i = 0; i < 16; i++) {
            int nlo;
            const int x = i * 8;
            if (rfmv_seq_range(m, clip, p0 + i, &nlo) > nlo)
                v2_R(cv, x, 52, 7, 11, 1);
            else
                v2_O(cv, x, 52, 7, 11, 1);
        }
        return;
    }
    rfmv_seq_loop_edit(m, cv);                          /* (else: page edit's own screen) */
}

/* a step held (300 ms): its notes in words, the velocity and the length; the steps row, the held step marked */
static void step_popup(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    const int s = m->pop_i, clip = SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel);
    const int abs = q->page * RFM_MOVE_SEQ_PAGE + s;
    int lo, hi = rfmv_seq_range(m, clip, abs, &lo), n = hi - lo, i;
    char b[48], nb[8];
    mr_gfx_clear(cv, 0);
    g_full = 1;
    b[0] = 0;
    for (i = 0; i < n; i++) {                           /* the names that fit (2x), "+n" for the rest */
        char t[48];
        snprintf(t, sizeof t, "%s%s%s", b, i ? " " : "", rfmv_note_name(q->notes[lo + i].note, nb, sizeof nb));
        if (v2_tw(t, 2) > 124 - (i + 1 < n ? 30 : 0) && i) {
            snprintf(t, sizeof t, "%s +%d", b, n - i);
            snprintf(b, sizeof b, "%s", t);
            break;
        }
        snprintf(b, sizeof b, "%s", t);
    }
    v2_TC(cv, 64, 3, b, 2, 1);
    if (n) {
        const rfm_move_note_t *n0 = &q->notes[lo];
        v2_ic(cv, V2I_VEL, 2, 26, 1);
        snprintf(b, sizeof b, "%d", n0->vel);
        v2_T(cv, 18, 24, b, 2, 1);
        v2_R(cv, 68, 31, 18, 1, 1), v2_R(cv, 68, 28, 1, 7, 1), v2_R(cv, 85, 28, 1, 7, 1);   /* length: a bracket */
        len_text(n0->len, b, sizeof b);
        v2_T(cv, 92, 24, b, 2, 1);
    }
    rfmv_seq_steps_cells(m, cv, 1u << s);
}

/* a project pad held: its number at 4x, a bar a track, the tempo, the clips; knob 1 turned: its colour */
static int bits_of(int n)
{
    int b = 0;
    while (n > 0)
        b++, n >>= 1;
    return b;
}
static void project_popup(const rfm_move_t *m, mr_canvas_t *cv)
{
    rfm_move_proj_info_t in;
    char b[16];
    int i, nt = 8;
    rfmv_proj_info(m, m->pop_i, &in);
    mr_gfx_clear(cv, 0);
    g_full = 1;
    if (m->pop_val == 1) {                              /* the colour, once */
        const int c = in.color ? in.color : m->pop_i % RFM_MOVE_PROJ_COLORS + 1;
        snprintf(b, sizeof b, "%s", rfmv_proj_colour_name(c));   /* (0.12.0, a12: its name) */
        v2_titleBox(cv, "Colour", b, 64, 124);
        v2_strip(cv, 16, 1u << (c - 1), -1, 0, 128);
        return;
    }
    snprintf(b, sizeof b, "%02d", m->pop_i + 1);
    if (rfmv_pfw_cap(m)) {                              /* (0.13.0, the owner's mock) its number at 3x, the tempo and
                                                         * the clips at 2x, a row a firmware slot (the bars gone) */
        v2_T(cv, 0, 0, b, 3, 1);
        if (!in.ok)
            return;
        v2_ic(cv, V2I_PLAY, 40, 5, 1);
        snprintf(b, sizeof b, "%d", (in.bpm10 + 5) / 10);
        v2_T(cv, 53, 3, b, 2, 1);
        v2_ic(cv, V2I_CLIP, 92, 5, 1);
        snprintf(b, sizeof b, "%d", in.clips);
        v2_T(cv, 105, in.clips >= 100 ? 6 : 3, b, in.clips >= 100 ? 1 : 2, 1);
        rfmv_pfw_rows(m, cv, in.fw, NULL, 28);
        return;
    }
    v2_T(cv, 2, 4, b, 4, 1);
    if (!in.ok)
        return;
    for (i = 8; i < RFM_MOVE_SEQ_TRACKS; i++)
        if (in.notes[i])
            nt = 16;                                    /* (tracks 9..16 in use: 16 thinner bars) */
    for (i = 0; i < nt; i++) {
        const int h = in.notes[i] ? clampi(5 * bits_of(in.notes[i]) + 1, 1, 26) : 1;
        if (nt == 8)
            v2_R(cv, 64 + i * 8, 32 - h, 5, h, 1);
        else
            v2_R(cv, 64 + i * 4, 32 - h, 3, h, 1);
    }
    v2_ic(cv, V2I_PLAY, 2, 44, 1);
    snprintf(b, sizeof b, "%d", (in.bpm10 + 5) / 10);
    v2_T(cv, 16, 43, b, 2, 1);
    v2_ic(cv, V2I_CLIP, 74, 44, 1);
    snprintf(b, sizeof b, "%d", in.clips);
    v2_T(cv, 90, 43, b, 2, 1);
}

/* a message (Title Case, past tense naming the thing): one or two rows, an icon over it where one says more, the
 * page strip kept under it in track view; the left half: the rows at 2x where they fit */
static void message_popup(const rfm_move_t *m, mr_canvas_t *cv)
{
    const char *a, *b;
    int icon, id = rfmv_seq_msg_info(m, &a, &b, &icon);
    if (g_half) {
        v2_R(cv, 0, 0, 64, 64, 0);
        if (*b) {
            v2_TC(cv, 32, 18, a, v2_tw(a, 2) <= 62 ? 2 : 1, 1);
            v2_TC(cv, 32, 38, b, v2_tw(b, 2) <= 62 ? 2 : 1, 1);
        } else
            v2_TC(cv, 32, 26, a, v2_tw(a, 2) <= 62 ? 2 : 1, 1);
        return;
    }
    mr_gfx_clear(cv, 0);
    g_full = 1;
    if (id == SEQ_MSG_CAPTURED) {                       /* Move's message with the tempo under it */
        char t[16];
        const int b10 = rfm_move_seq_bpm10(m);
        snprintf(t, sizeof t, "%d BPM", (b10 + 5) / 10);
        v2_titleBox(cv, a, t, 64, 124);
    } else if (icon >= 0) {                             /* (a toggle or a one-shot with its icon: no strip) */
        v2_ic(cv, icon, 58, 4, 1);
        v2_msg(cv, a, b);
        return;
    } else
        v2_msg(cv, a, b);
    if (id == SEQ_MSG_DOUBLE) {                         /* the loop's pages on a strip of 16 */
        const rfm_move_seq_t *q = &m->seq;
        const int clip = SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel);
        const int pa = rfmv_seq_loop_lo(m, clip) / RFM_MOVE_SEQ_PAGE, pb = (rfmv_seq_loop_hi(m, clip) - 1) / RFM_MOVE_SEQ_PAGE;
        uint32_t sel = 0;
        int i;
        for (i = pa; i <= pb && i < 16; i++)
            sel |= 1u << i;
        v2_strip(cv, 16, sel, -1, 0, 128);
    } else if (m->s.mode == RFM_MOVE_MODE_SEQ && !m->seq.view && !m->seq.projects)
        rfmv_seq_page_strip(m, cv, -1);
}

/* the count-in: a record dot, the beats to go at 4x; the strip fills beat by beat */
static void count_popup(const rfm_move_t *m, mr_canvas_t *cv)
{
    char b[4];
    const int go = clampi((m->seq.count_in - 2) / 96 + 1, 1, 4);   /* (a bar: 4 .. 1) */
    snprintf(b, sizeof b, "%d", go);
    mr_gfx_clear(cv, 0);
    v2_ic(cv, V2I_REC, 14, 22, 1);
    v2_TC(cv, 76, 14, b, 4, 1);
    v2_strip(cv, 4, (1u << (5 - go)) - 1, -1, 0, 128);
}

/* whichever popup is up now (the newer of a control's popup and a message; the count-in under both) */
static void popups(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    const char *msg = rfmv_seq_msg_text(m);
    int up = m->pop && (m->pop != POP_SELECT || g_half) && rfmv_pop_up(m, m->pop) &&
             !(m->pop == POP_MODE && m->pop_i < 4 && rfmv_hold_shown(m, m->pop_i));   /* (its button held: its menu) */
    if (up && m->pop == POP_STEP && (q->view || q->projects || m->s.mode != RFM_MOVE_MODE_SEQ))
        up = 0;
    if (up && m->pop == POP_LABEL && rfmv_now_ms(m) - m->pop_ms < m->pop_delay)
        up = 0;                                         /* (0.5.6: a button not yet held long enough) */
    g_scroll_ms = rfmv_now_ms(m) - m->pop_t0;
    if (msg && (!up || (int32_t)(q->msg_ms - m->pop_ms) >= 0)) {
        message_popup(m, cv);
        return;
    }
    if (up) {
        if (m->pop == POP_STEP)
            step_popup(m, cv);
        else if (m->pop == POP_TRIG)
            trig_popup(m, cv);
        else if (m->pop == POP_LOOP)
            loop_popup(m, cv);
        else if (m->pop == POP_FW) {                    /* (0.4.3) Back + Track: the firmware played now */
            char nm[24], b[12];
            rfmv_fw_slot_name(m, m->pop_i, nm, sizeof nm);
            nm[10] = 0;
            snprintf(b, sizeof b, "Track %d", m->pop_i + 1);
            mr_gfx_clear(cv, 0);
            v2_msg(cv, nm, b);
            g_full = 1;
        } else if (m->pop == POP_TRKINFO) {             /* (0.10.0) session view, a knob touched: its track; (0.11.0)
                                                         * knob 8: the scenes */
            if (m->pop_i >= RFM_MOVE_SV_COLS)
                rfmv_seq_scene_pop(m, cv);
            else
                rfmv_seq_track_pop(m, cv, clampi(q->track_page + m->pop_i, 0, RFM_MOVE_SEQ_TRACKS - 1));
            g_full = 1;
        } else if (m->pop == POP_TRACK) {                 /* a track change: its name */
            char b[12];
            snprintf(b, sizeof b, "Track %d", m->pop_i + 1);
            mr_gfx_clear(cv, 0);
            v2_msg(cv, b, NULL);
            g_full = 1;
        } else if (m->pop == POP_MODE) {                /* a Track button tapped: the mode (or its view) named, the
                                                         * whole screen, the strip the four modes (the two views) */
            static const char *const MODES[4] = { "FM-1", "MIDI", "Control", "Seq" };
            static const char *const VIEWS[RFM_MOVE_MV_N] = { "Pads", "Notes" };
            mr_gfx_clear(cv, 0);
            if (m->pop_i == 20) {                       /* projects mode entered: Move's set icon over the name */
                v2_ic(cv, V2I_PROJ, 58, 4, 1);
                v2_TC(cv, 64, 26, "Projects", 2, 1);
            } else if (m->pop_i == 30 || m->pop_i == 31) {   /* (0.5.3) Track 2 held + pad 8: latch */
                v2_TC(cv, 64, 22, m->pop_i == 31 ? "Latch on" : "Latch off", 2, 1);
            } else if (m->pop_i >= 21 && m->pop_i <= 23) {   /* (0.4.5) Back into the view, (0.5.2) Menu: Firmwares,
                                                         * FX & Mix, Mixer */
                v2_ic(cv, V2I_PROJ, 58, 4, 1);
                v2_TC(cv, 64, 26, m->pop_i == 21 ? "Firmwares" : m->pop_i == 22 ? "FX & Mix" : "Mixer", 2, 1);
            } else if (m->pop_i < 4)
                v2_modeName(cv, MODES[m->pop_i], 4, m->pop_i);
            else
                v2_modeName(cv, VIEWS[(m->pop_i - 10) % RFM_MOVE_MV_N], RFM_MOVE_MV_N, (m->pop_i - 10) % RFM_MOVE_MV_N);
            g_full = 1;
        } else if (m->pop == POP_BAR) {                 /* Move's "Bar 2" (a ghost page) over the ghost strip */
            char b[12];
            snprintf(b, sizeof b, "Bar %d", m->pop_i + 1);
            mr_gfx_clear(cv, 0);
            v2_TC(cv, 64, 22, b, 2, 1);
            rfmv_seq_page_strip(m, cv, -1);
            g_full = 1;
        } else if (m->pop == POP_PROJ)
            project_popup(m, cv);
        else if (m->pop == POP_HOST)
            host_popup(m, cv);
        else if (m->pop == POP_TK) {                    /* (0.7.0) a takeover entered: its name, the whole screen */
            tk_popup(m, cv);
            g_full = 1;
        } else if (m->pop == POP_SLOT) {                /* (0.11.0) session view, a Track tapped: its slot's summary */
            rfmv_slot_screen(m, cv, m->pop_i % 3, 0);
            g_full = 1;
        } else if (m->pop == POP_VIEW) {                /* (0.11.0) Menu under an open Shift menu: the view named */
            mr_gfx_clear(cv, 0);
            v2_modeName(cv, m->pop_i ? "Session" : "Sequencer", 2, m->pop_i != 0);
            g_full = 1;
        } else if (m->pop == POP_FWLV) {                /* (0.12.x) the FM-1 page's level: "Knobs" / "Knobs+Keys" */
            rfmv_pop_screen(cv, "FM-1", m->pop_i ? "Knobs+Keys" : "Knobs", 2, m->pop_i != 0, NULL);
            g_full = 1;
        } else if (m->pop == POP_ARPW) {                /* (0.12.0, the owner's) the arp switched: "Arp" over its mode
                                                         * at 4x, the whole screen */
            rfmv_pop_screen(cv, "Arp", rfmv_seq_arp_mode_name(m->pop_i), 0, 0, NULL);
            g_full = 1;
        } else if (m->pop == POP_OCT) {                 /* (0.12.0) Up / Down: the octave at 4x, the pads' notes under */
            char o[8], r[24];
            snprintf(o, sizeof o, m->s.octave ? "%+d" : "0", m->s.octave);
            rfmv_seq_oct_range(m, r, sizeof r);
            rfmv_pop_screen(cv, "Octave", o, 0, 0, r);
            g_full = 1;
        } else if (m->pop == POP_BACK) {                /* (0.12.x) Back held + a button: what it did */
            rfmv_seq_back_pop(m, cv);
            g_full = 1;
        } else if (m->pop >= POP_LAYOUT && m->pop <= POP_SEQ) {   /* (0.8.0) the Track buttons' popups (the board) */
            char nm[24], a[16], b[16];
            const int i = m->pop_i;
            switch (m->pop) {
            case POP_LAYOUT:
                slot_title(m, i % 3, nm, sizeof nm);
                rfmv_pop_screen(cv, nm, rfmv_lay_name(m, i % 3, rfmv_slot_lay(m, i % 3)), RFM_MOVE_LAY_N,
                                rfmv_slot_lay(m, i % 3), NULL);
                break;
            case POP_JOG: rfmv_pop_screen(cv, "Jog", rfmv_jog_name(i), 3, i % 3, NULL); break;
            case POP_T4: rfmv_pop_screen(cv, "Track 4", i ? "Routing" : "Mixer", 2, i != 0, NULL); break;
            case POP_CCNUM:
                slot_title(m, m->fw.active % 3, nm, sizeof nm);
                snprintf(a, sizeof a, "Knob %d", (i & 7) + 1);
                snprintf(b, sizeof b, "CC %d", m->s.slot[m->fw.active % 3].cc[i & 7]);
                rfmv_pop_screen(cv, a, b, 0, 0, nm);
                break;
            case POP_LOCK:
                slot_title(m, i % 3, nm, sizeof nm);
                snprintf(a, sizeof a, "Track %d", m->s.lock[i % 3] + 1);
                pop_screen_at(cv, nm, a, 0, 0, m->lk_word < 8 ? LK_WORDS[m->lk_word] : NULL, 18);
                break;
            default: rfmv_pop_screen(cv, NULL, "Sequencer", 0, 0, NULL); break;
            }
            g_full = 1;
        }
        else if (m->pop == POP_READ) {                  /* (0.5.7) a readout of its own (the left half) */
            if (g_half && m->pop_i < 8)
                knob_readout(m, cv, &m->rd_pop, m->pop_i);   /* (0.5.8: a knob's, the same rule) */
            else if (g_half)
                readout_draw(cv, &m->rd_pop);
        } else if (m->pop == POP_LABEL) {                 /* (0.5.6) FM-1 mode: a button's or key's label */
            char t[RFM_MOVE_LABEL_MAX + 1], l[RFM_MOVE_LABEL_MAX + 8];
            if (g_half && rfmv_label(m, m->pop_i, t, sizeof t)) {
                rfmv_tcase(t, l, sizeof l);
                label_rows(cv, l);
                g_label_drawn = 1;
            }
        } else
            value_popup(m, cv);
        return;
    }
    if (m->s.mode == RFM_MOVE_MODE_SEQ && q->start_pending == 3)
        count_popup(m, cv);
}

/* ================================================================ FM-1 mode === */
/* the owner's 64 x 64 drawing of the FM-1 (white = on; bit 63 = x 0): the frame, MASTER SELECT PRESETS ALGO (2 x 2
 * dots), the screen, K1..K4 and the 6 x 2 buttons, OCT- / OCT+, the keybed (11 + 4 gaps over 16 naturals) */
static const uint64_t FM1_ART[64] = {
    0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u,
    0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u,
    0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u,
    0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u, 0x0ffffffffffffff0u,
    0x1000000000000008u, 0x2000000000000004u, 0x20003fff00000004u, 0x20003fff00000004u,
    0x230c30031830c184u, 0x230c30031830c184u, 0x200031c300000004u, 0x200033e300000004u,
    0x2000366300000004u, 0x200036333fffff84u, 0x230c3c3344444444u, 0x230c381f44444444u,
    0x2000300f44444444u, 0x200030037fffffc4u, 0x20003fff44444444u, 0x23fe3fff44444444u,
    0x2222000044444444u, 0x23fe00003fffff84u, 0x2000000000000004u, 0x2000000000000004u,
    0x21ffffffffffff04u, 0x2200000000000084u, 0x2236db6db6db6c84u, 0x2236db6db6db6c84u,
    0x2236db6db6db6c84u, 0x2236db6db6db6c84u, 0x2200000000000084u, 0x22db6db6db6db684u,
    0x22db6db6db6db684u, 0x22db6db6db6db684u, 0x22db6db6db6db684u, 0x2200000000000084u,
    0x21ffffffffffff04u, 0x2000000000000004u, 0x1000000000000008u, 0x0ffffffffffffff0u,
    0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u,
    0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u,
    0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u, 0x0000000000000000u,
};
/* the drawn encoders' 2 x 2 dots, by matrix encoder (0 SELECT 1 ALGO 2..5 K1..K4 6 PRESETS 7 MASTER): the left 2 x 2
 * as the FM-1 has them (MASTER SELECT / PRESETS ALGO), K1..K4 in the row over the buttons */
static const uint8_t ENC_XY[8][2] = { { 12, 20 }, { 12, 26 }, { 35, 20 }, { 42, 20 }, { 48, 20 }, { 55, 20 },
                                      { 6, 26 }, { 6, 20 } };
static const int8_t NATURAL_OF[RFM_MOVE_NKEYS] = {   /* key -> its natural's index, or (sharps) -1 - the one below */
    0, -1, 1, -2, 2, -3, 3, 4, -5, 5, -6, 6, 7, -8, 8, -9, 9, -10, 10, 11, -12, 12, -13, 13, 14, -15, 15,
};

void rfmv_fm1_art(const rfm_move_t *m, mr_canvas_t *cv)
{
    int x, y, k, e = -1, b;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            cv->px[y][x] = (uint8_t)(FM1_ART[y] >> (63 - x) & 1);
    /* the encoder touched (or SELECT, the jog wheel, turned a moment ago): its dot inverted in a 6 x 6 block */
    if (m->touched >= 0 && m->touched < RFM_MOVE_NKNOBS)
        e = rfm_move_knob_enc(m->touched);
    else if (m->touched == 8)
        e = RFM_MOVE_E_MASTER;
    else if (m->touched == 9 || rfmv_pop_up(m, POP_SELECT))
        e = RFM_MOVE_E_SELECT;
    if (e >= 0)
        v2_XR(cv, ENC_XY[e][0] - 2, ENC_XY[e][1] - 2, 6, 6);
    /* the buttons lit (RFM_MOVE_SCREEN_LIT) or held: their cells filled. The 6 x 2 grid in the panel's order
     * (FX SEL ENV LFO EDIT GLO / HOME SAVE ARP SEQ PLAY REC = matrix 2..13), OCT- OCT+ in the box at the left */
    for (b = 0; b < RFM_MOVE_NBTN; b++) {
        if (!m->btn_count[b] && rfmv_level(m, b) < RFM_MOVE_SCREEN_LIT)
            continue;
        if (b < 2)
            fill(cv, 7 + 4 * b, 32, 3, 1, 1);
        else
            fill(cv, 34 + 4 * ((b - 2) % 6), 26 + 4 * ((b - 2) / 6), 3, 3, 1);
    }
    /* the keys held: their 2 x 4 blocks dark. Naturals: the bottom row's 16; sharps: the top row's block in the gap
     * after their natural (the gaps B-C and E-F stay as drawn) */
    for (k = 0; k < RFM_MOVE_NKEYS; k++) {
        const int n = NATURAL_OF[k];
        if (!m->key_count[k])
            continue;
        if (n >= 0)
            fill(cv, 8 + 3 * n, 43, 2, 4, 0);
        else
            fill(cv, 10 + 3 * (-1 - n), 38, 2, 4, 0);
    }
    if (m->seq.playing)                                 /* the sequencer plays: a small triangle over the drawing */
        rfmv_tri(cv, 3, 3, 5, 9, 1);
}

/* ================================================================ MIDI, Control === */
/* a pad's note in the layout: 2 the root, 1 in the scale (every drum pad), 0 out, -1 no note */
static int pad_class(const rfm_move_t *m, int p)
{
    const int n = rfm_move_pad_note(&m->s, p);
    const uint8_t *scl = rfmv_scales[m->s.scale < RFM_MOVE_NSCALES ? m->s.scale : 0];
    int rel, i;
    if (n < 0)
        return -1;
    rel = ((n - m->s.root) % 12 + 12) % 12;
    if (rel == 0)
        return 2;
    if (m->s.layout == RFM_MOVE_LAYOUT_DRUM)
        return 1;
    for (i = 0; i < scl[0]; i++)
        if (scl[1 + i] == rel)
            return 1;
    return 0;
}

/* V2Midi midi.pads: 14 x 14 cells, 16 px apart, top row = the top pads; root filled, in the scale outlined, out a
 * dot; a pressed pad carries a 6 x 6 square in the opposite colour */
static void midi_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    int p;
    for (p = 0; p < RFM_MOVE_NPADS; p++) {
        const int x = (p % 8) * 16 + 1, y = (3 - p / 8) * 16 + 1, c = pad_class(m, p);
        if (c < 0)
            continue;
        if (c == 2)
            v2_R(cv, x, y, 14, 14, 1);
        else if (c == 1)
            v2_O(cv, x, y, 14, 14, 1);
        else
            v2_R(cv, x + 6, y + 6, 2, 2, 1);
        if (m->pad_down[p])                             /* pressed: a square in the opposite colour */
            v2_R(cv, x + 4, y + 4, 6, 6, c != 2);
    }
}

/* ---- MIDI mode's NOTES screen: the notes held (names, 2x when they fit) and the chord they make (Cmaj7, Am, Dsus4,
 * G7; inverted: a slash chord, C/E) on the left half */
static const struct { uint16_t set; const char *name; } CHORDS[] = {
    { 0x091, "" }, { 0x089, "m" }, { 0x049, "dim" }, { 0x111, "aug" }, { 0x085, "sus2" }, { 0x0A1, "sus4" },
    { 0x891, "maj7" }, { 0x491, "7" }, { 0x489, "m7" }, { 0x449, "m7b5" }, { 0x249, "dim7" }, { 0x889, "mmaj7" },
    { 0x291, "6" }, { 0x289, "m6" }, { 0x095, "add9" }, { 0x08D, "madd9" }, { 0x495, "9" }, { 0x895, "maj9" },
    { 0x48D, "m9" }, { 0x4A1, "7sus4" }, { 0x081, "5" },
};
/* the chord of notes n[0..k) (any order): its name, or "" */
const char *rfmv_chord_name(const uint8_t *n, int k, char *out, int cap)
{
    uint16_t pcs = 0;
    int i, r, c, bass = 128, best = -1, best_r = 0;
    out[0] = 0;
    for (i = 0; i < k; i++) {
        pcs = (uint16_t)(pcs | 1u << (n[i] % 12));
        if (n[i] < bass)
            bass = n[i];
    }
    if (k < 2)
        return out;
    for (r = 0; r < 12; r++) {                          /* (the bass as the root first: C6 over Am7/C) */
        const int root = (bass % 12 + r) % 12;
        const uint16_t rel = (uint16_t)(((pcs >> root) | (pcs << (12 - root))) & 0xFFF);
        if (!(rel & 1))
            continue;
        for (c = 0; c < (int)(sizeof CHORDS / sizeof CHORDS[0]); c++)
            if (CHORDS[c].set == rel && (best < 0 || c < best))
                best = c, best_r = root;
        if (best >= 0)
            break;
    }
    if (best < 0)
        return out;
    snprintf(out, (size_t)cap, "%s%s", rfmv_note_names[best_r], CHORDS[best].name);
    if (bass % 12 != best_r) {
        const size_t l = strlen(out);
        snprintf(out + l, (size_t)cap - l, "/%s", rfmv_note_names[bass % 12]);
    }
    return out;
}
/* V2Midi midi.chord: the chord's name at 3x (2x, 1x when it is wider), the held notes on a one-octave keyboard */
static void notes_half(const rfm_move_t *m, mr_canvas_t *cv)
{
    uint8_t n[16];
    char c[16];
    int k = 0, i, held = 0;
    for (i = 0; i < 128 && k < 16; i++)
        if (m->note_count[m->s.channel][i])
            n[k++] = (uint8_t)i, held |= 1 << (i % 12);
    if (!k)
        return;                                         /* (at rest: black) */
    rfmv_chord_name(n, k, c, sizeof c);
    if (*c) {
        const int s = v2_tw(c, 3) <= 62 ? 3 : v2_tw(c, 2) <= 62 ? 2 : 1;
        v2_TC(cv, 32, s == 3 ? 4 : s == 2 ? 8 : 12, c, s, 1);
    }
    v2_kbd(cv, 1, 34, 62, 28, -1, 0, held);
}


/* ================================================================ held Track buttons === */
/* the build's key / scale screen (kept, the owner's): the layouts as tabs, the key and the scale at 4x (3x, 2x when
 * wider), the root on a 12-key row, the channels (MIDI) or Global / Track (the sequencer's Keys & Scales) */
void rfmv_keys_menu(const rfm_move_t *m, mr_canvas_t *cv, int glob)
{
    char b[24];
    snprintf(b, sizeof b, "%s %s", rfmv_note_names[m->s.root % 12], rfm_move_scale_name(m->s.scale));
    if (v2_tw(b, 4) <= 128)
        v2_keysMenu(cv, b, m->s.layout, m->s.root % 12, m->s.channel, glob);
    else {                                              /* (a long scale name: the same screen, the name smaller) */
        v2_keysMenu(cv, "", m->s.layout, m->s.root % 12, m->s.channel, glob);
        v2_TC(cv, 64, v2_tw(b, 3) <= 128 ? 10 : 13, b, v2_tw(b, 3) <= 128 ? 3 : 2, 1);
    }
}



/* ================================================================ the screen === */
/* The thumbnail is plain black and white, no dither (the owner's call: it reads more clearly). Each FM-1 pixel is
 * white when its luminance is above RFM_MOVE_THUMB_LUMA; a thumbnail pixel (a 3..4 px box of them) is white when at
 * least a quarter of its box is, so 1 px lines and text strokes survive the 3.75x reduction. */
static void thumb_from(rfm_move_t *m, const uint16_t *px)
{
    int x, y, i, j;
    for (y = 0; y < 64; y++) {
        const int y0 = y * 240 / 64, y1 = (y + 1) * 240 / 64;
        for (x = 0; x < 64; x++) {
            const int x0 = x * 240 / 64, x1 = (x + 1) * 240 / 64;
            uint32_t lit = 0, cnt = 0;
            for (j = y0; j < y1; j++)
                for (i = x0; i < x1; i++) {
                    const uint16_t v = px[j * 240 + i];
                    const uint32_t r = (v >> 11 & 31) * 255 / 31, g = (v >> 5 & 63) * 255 / 63, b = (v & 31) * 255 / 31;
                    lit += ((77 * r + 150 * g + 29 * b) >> 8) > RFM_MOVE_THUMB_LUMA;
                    cnt++;
                }
            m->thumb[y][x] = (uint8_t)(lit * 4 >= cnt);
        }
    }
}

void rfm_move_thumb(rfm_move_t *m, const uint16_t *rgb565)
{
    if (rgb565)
        thumb_from(m, rgb565);
}

int rfm_move_screen_timed(const rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    int t;
    for (t = 0; t < 4; t++)                             /* (a Track button held, its screen not yet due) */
        if (rfmv_track_held(m, t) && !rfmv_hold_shown(m, t))
            return 1;
    return q->playing || q->start_pending || rfmv_seq_msg_text(m) != NULL || (m->pop && rfmv_pop_up(m, m->pop)) ||
           (m->menu_what && rfmv_now_ms(m) - m->menu_ms < RFM_MOVE_POPUP_MS) ||
           (q->projects && q->proj_pending >= 0) || rfmv_fw_timed(m) || rfmv_pfw_timed(m) || rfmv_st_timed(m);
}

void rfm_move_screen(rfm_move_t *m, const uint16_t *rgb565, uint8_t out[MR_SCREEN_BYTES])
{
    mr_canvas_t cv;                                     /* (8 KB of stack) */
    int t, x, y;
    if (rgb565)
        thumb_from(m, rgb565);
    if (m->pop == POP_HOST && rfmv_pop_up(m, POP_HOST)) {   /* (0.5.4) a host's message: over any screen */
        host_popup(m, &cv);
        mr_gfx_pack(&cv, out);
        return;
    }
    if (rfmv_pfw_screen(m, &cv) || rfmv_st_screen(m, &cv)) {   /* (0.13.0) a project's firmwares loading (or one
                                                         * missing); the States layer, over every view */
        mr_gfx_pack(&cv, out);
        return;
    }
    if (m->seq.fill) {                                  /* (0.12.0, a10) FILL on: "Fill" the whole screen while held */
        mr_gfx_clear(&cv, 0);
        rfmv_pop_screen(&cv, NULL, "Fill", 0, 0, NULL);
        mr_gfx_pack(&cv, out);
        return;
    }
    if (!(m->pop == POP_MODE && (m->pop_i == 30 || m->pop_i == 31) && rfmv_pop_up(m, POP_MODE))) {
        for (t = 0; t < 4 && (!rfmv_hold_shown(m, t) || (t == 3 && m->seq.smenu && m->seq.t4_opened) ||
                              (t < 3 && ((m->seq.projects && m->fw.pop == FP_LEVEL) || rfmv_fw_level_shown(m)))); t++) {}   /* (a slot's level set in the
                                                         * view: its popup) */   /* (0.9.0: over the view
                                                         * too; Track 4's gives way to a Shift menu it opened) */   /* (0.8.0) a held Track's menu: over any layout, a
                                                         * takeover too (a tap: nothing of it) */
        if (t < 4) {
            track_menu(m, &cv, t);
            mr_gfx_pack(&cv, out);
            return;
        }
    }
    if ((m->track_held & 7) && rfmv_fw_level_shown(m) && rfmv_fw_screen(m, &cv)) {   /* (0.13.0) a held Track + the
                                                         * volume knob: the slot's level, over a takeover's screen too */
        mr_gfx_pack(&cv, out);
        return;
    }
    if (rfmv_tk_claims(m) & RFM_MOVE_TK_SCREEN) {       /* (0.7.0) a takeover with the screen: the firmware's, its
                                                         * name over it on entering */
        if (m->pop == POP_TK && rfmv_pop_up(m, POP_TK)) {
            tk_popup(m, &cv);
            mr_gfx_pack(&cv, out);
        } else if (m->pop == POP_MASTER && rfmv_pop_up(m, POP_MASTER) && !(rfmv_tk_claims(m) & RFM_MOVE_TK_VOLUME)) {
            mr_gfx_clear(&cv, 0);                       /* (0.8.1) the volume knob (the host's, not claimed): its
                                                         * level over the firmware's screen, full screen */
            value_popup(m, &cv);
            mr_gfx_pack(&cv, out);
        } else if (!(rfmv_tk_claims(m) & RFM_MOVE_TK_KNOBS) && rfmv_pop_up(m, m->pop) &&
                   (m->pop == POP_ENC || (m->pop == POP_READ && m->pop_i < RFM_MOVE_NKNOBS))) {
            /* (0.12.0, a15, the owner's: "Keep the setting to put the popups on the left side or full screen as I might
             * want to sometimes see the screen while turning the knob") a knob the takeover leaves to the FM-1: its
             * popup over the takeover's screen, while touched and its second after a turn, in the popups' place (Global
             * settings' Popup size: the left half, the firmware's screen on the right; or the whole screen) */
            const uint8_t *ts = m->tk[m->fw.active % RFM_MOVE_FW_SLOTS].screen;
            for (y = 0; y < 64; y++)
                for (x = 0; x < 128; x++)
                    cv.px[y][x] = (uint8_t)(ts[(y >> 3) * 128 + x] >> (y & 7) & 1);
            g_half = 1, g_full = 0, g_label_drawn = 0, g_wide_drawn = 0;
            g_lw = m->s.pop_half ? 64 : 128;
            g_scroll_ms = rfmv_now_ms(m) - m->pop_t0;
            if (m->pop == POP_READ)
                knob_readout(m, &cv, &m->rd_pop, m->pop_i);
            else
                value_popup(m, &cv);
            g_lw = 64;
            mr_gfx_pack(&cv, out);
        } else
            memcpy(out, m->tk[m->fw.active % RFM_MOVE_FW_SLOTS].screen, MR_SCREEN_BYTES);
        return;
    }
    if (m->seq.projects && ((m->pop == POP_MODE && m->pop_i >= 20 && m->pop_i <= 23) || m->pop == POP_T4) &&
        rfmv_pop_up(m, m->pop)) {
        mr_gfx_clear(&cv, 0);                           /* (0.4.5) Back into the view: its name, a second */
        popups(m, &cv);
        mr_gfx_pack(&cv, out);
        return;
    }
    if (m->seq.projects && m->seq.smenu) {              /* (0.9.0) a Shift menu over the Mixer / Routing */
        mr_gfx_clear(&cv, 0);
        rfmv_seq_screen(m, &cv);
        popups(m, &cv);
        mr_gfx_pack(&cv, out);
        return;
    }
    if (rfmv_fw_screen(m, &cv)) {                       /* (0.4.0) the projects view's firmware tracks, FX setup,
                                                         * its popups; a slot's level */
        mr_gfx_pack(&cv, out);
        return;
    }
    mr_gfx_clear(&cv, 0);
    if (m->pop == POP_MODE && (m->pop_i == 30 || m->pop_i == 31) && rfmv_pop_up(m, POP_MODE)) {
        popups(m, &cv);                                 /* (0.5.3) latch's name: over the layout menu too */
        mr_gfx_pack(&cv, out);
        return;
    }
    if (m->s.mode != RFM_MOVE_MODE_SEQ && (m->seq.smenu || m->seq.projects)) {   /* a sequencer menu opened from
                                                         * Track 4's menu or Shift + a step: full width until Back */
        rfmv_seq_screen(m, &cv);
        popups(m, &cv);
        mr_gfx_pack(&cv, out);
        return;
    }
    if (rfmv_fwp_knobs(m) && !rfmv_half(m) && rfmv_pop_up(m, m->pop) &&
        (m->pop == POP_ENC || (m->pop == POP_READ && m->pop_i < RFM_MOVE_NKNOBS))) {
        /* (0.12.x) the FM-1 page's level 1: a knob's popup over the track view, in the popups' place as over a
         * takeover (Global settings' Popup size: the whole screen, or the left half with the firmware's screen on the
         * right) */
        rfmv_seq_screen(m, &cv);
        g_half = 1, g_full = 0, g_label_drawn = 0, g_wide_drawn = 0;
        g_lw = m->s.pop_half ? 64 : 128;
        g_scroll_ms = rfmv_now_ms(m) - m->pop_t0;
        if (m->pop == POP_READ)
            knob_readout(m, &cv, &m->rd_pop, m->pop_i);
        else
            value_popup(m, &cv);
        if (g_lw == 64)
            for (y = 0; y < 64; y++)
                for (x = 0; x < 64; x++)
                    cv.px[y][64 + x] = m->thumb[y][x];
        g_lw = 64;
        mr_gfx_pack(&cv, out);
        return;
    }
    switch (m->s.mode) {
    case RFM_MOVE_MODE_SEQ:
        if (rfmv_fwp_full(m) && !m->seq.smenu) {        /* (0.12.x) the FM-1 page's level 2: the FM-1's screen (its
                                                         * buttons and keys act on it), as the FM-1 layout */
            rfmv_fm1_art(m, &cv);
            for (y = 0; y < 64; y++)
                for (x = 0; x < 64; x++)
                    cv.px[y][64 + x] = m->thumb[y][x];
        } else
            rfmv_seq_screen(m, &cv);
        break;
    case RFM_MOVE_MODE_MIDI:
        if (m->s.midi_view == RFM_MOVE_MV_NOTES) {
            notes_half(m, &cv);                         /* the notes and the chord; the FM-1's screen on the right */
            for (y = 0; y < 64; y++)
                for (x = 0; x < 64; x++)
                    cv.px[y][64 + x] = m->thumb[y][x];
        } else
            midi_screen(m, &cv);
        break;
    default:
        rfmv_fm1_art(m, &cv);
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++)
                cv.px[y][64 + x] = m->thumb[y][x];
        break;
    }
    if (m->latch && m->s.mode == RFM_MOVE_MODE_MIDI) {
        v2_R(&cv, 119, 0, 9, 11, 1);                    /* (0.5.3) latched: a framed "L" in the top-right corner
                                                         * (readable over a lit or dark pad) */
        v2_R(&cv, 120, 1, 7, 9, 0);
        v2_T(&cv, 121, 1, "L", 1, 1);
    }
    g_half = rfmv_half(m), g_full = 0, g_label_drawn = 0, g_wide_drawn = 0;
    g_lw = m->s.pop_half ? 64 : 128;                    /* (0.5.9: the labels' and readouts' width, the owner's) */
    popups(m, &cv);
    g_lw = 64;
    if (g_label_drawn && rfmv_label_held(m, m->pop_i))
        m->pop_shown = 1;                               /* (0.5.8: shown while held: its second after the release) */
    if (g_half && !g_full && !g_wide_drawn) {          /* (any wider popup: the FM-1's screen drawn back over it) */
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++)
                cv.px[y][64 + x] = m->thumb[y][x];
        g_half = 0;
    }
    mr_gfx_pack(&cv, out);
}
