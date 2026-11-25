/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (0.15.0, the owner's 2026-11-24) the sampler app: the host's recorder as an app (move.h, "the sampler app"; README
 * "The sampler"). Only with RFM_MOVE_CAP_SAMPLER. It takes the screen, the jog and Back, and Sample (its record key);
 * everything else stays the layer's under it. The settings are a list on the jog: at rest one row (the chosen
 * setting) under the source's live wave and level; while the jog turns three rows a second; up past the first row the
 * jog is on the tabs (a click, the jog picks a tab, a click or Back ends it); a row's click is its popup (its name and
 * value, the jog changes it). A take running: its own screen (the bars, its wave, the level). The screens are the
 * mock-up's (remoteFM14Move-recorder docs/design/mock_recorder_v4.c), pixel for pixel. The core times nothing of a
 * take: the host's status says where it is.
 *
 * (stage 2) the takes: the Takes tab (three rows a window of the host's list, the left arrow the days, Delete held + a
 * click deletes), a take's actions (Play, Trim, Delete), Trim (knobs 1 and 2 the markers, Shift fine, zoomed while one
 * moves), the Tools tab (Trim, Normalize, Reverse, Fade in, Fade out on the take chosen in Takes), Undo; the preview
 * after a Capture and Trim after a Manual or Threshold take. The host owns the files: the core asks (SMP_ROWS,
 * SMP_PEAKS), never waits, and draws what has come. */
#include <stdio.h>
#include <string.h>

#include "move_int.h"
#include "v2.h"

#define clampi rfmv_clampi
#define S(m) (&(m)->smp)

static const int8_t DEF[RFM_MOVE_SMP_N] = { RFM_MOVE_SMP_SRC_MASTER, RFM_MOVE_SMP_M_BARS, 0, -24, 0, 0,
                                            RFM_MOVE_SMP_TO_FILES };
static const int8_t LEN_BARS[RFM_MOVE_SMP_LENGTHS] = { 0, 1, 2, 4, 8, 16, 32 };
static const char *const SRC_NAME[RFM_MOVE_SMP_SRC_N] = { "Master", "Slot 1", "Slot 2", "Slot 3", "Input", "Stems",
                                                          "Looper", "Kit" };
static const char *const SRC_SIGN[RFM_MOVE_SMP_SRC_N] = { "M", "1", "2", "3", "", "S", "L", "K" };
static const char *const MODE_NAME[RFM_MOVE_SMP_M_N] = { "Capture", "Bars", "Threshold", "Manual" };
static const char *const TO_NAME[RFM_MOVE_SMP_TO_N] = { "Files", "Looper", "Kit" };
static const char *const POP_NAME[RFM_MOVE_SMP_N] = { "Source", "Mode", "Length", "Threshold", "In Level", "Monitor",
                                                      "To" };   /* ("Input level" is 130 px at 2x) */
static const char *const SAY_NAME[RFM_MOVE_SMP_N] = { "Source", "Mode", "Length", "Threshold", "Input level", "Monitor",
                                                      "To" };
static const char *const TAB_NAME[RFM_MOVE_SMP_TAB_N] = { "Record", "Takes", "Tools", "Loops" };
static const char *const MONTH[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *const MONTH_SAY[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                           "September", "October", "November", "December" };

/* ---- the icons (the mock-up's, 11 x 11; normalize, reverse and the fades drawn for stage 2 in their style) */
typedef const char *const icon_t[11];
static icon_t I_SRC = { "........##.", "....#...##.", ".....#..##.", "......#.##.", "#######.##.", "#######.##.",
                        "......#.##.", ".....#..##.", "....#...##.", "........##.", "..........." };
static icon_t I_MODE = { "###.....###", "#.........#", "#.........#", "#....#....#", "#...###...#", "#...###...#",
                         "#....#....#", "#.........#", "#.........#", "###.....###", "..........." };
static icon_t I_LEN = { "...........", "...........", "#.........#", "#.........#", "#.........#", "###########",
                        "#.........#", "#.........#", "#.........#", "...........", "..........." };
static icon_t I_THR = { "......##...", "......##...", "......##...", "####..##...", "####..##...", "####..##...",
                        "####..##...", "......##...", "......##...", "......##...", "..........." };
static icon_t I_IN = { "....###....", "....###....", "....###....", "....###....", "..#.###.#..", "..#.###.#..",
                       "..#.....#..", "...#####...", ".....#.....", ".....#.....", "...#####..." };
static icon_t I_MON = { "...#####...", "..#.....#..", ".#.......#.", ".#.......#.", ".#.......#.", "###.....###",
                        "###.....###", "###.....###", "###.....###", "...........", "..........." };
static icon_t I_TO = { ".....#.....", ".....#.....", ".....#.....", "..#..#..#..", "...#.#.#...", "....###....",
                       ".....#.....", "#.........#", "#.........#", "###########", "..........." };
static icon_t I_LIST = { "...........", "##..#######", "##..#######", "...........", "##..#######", "##..#######",
                         "...........", "##..#######", "##..#######", "...........", "..........." };
static icon_t I_DOT = { "...........", "...#####...", "..#######..", ".#########.", ".#########.", ".#########.",
                        ".#########.", ".#########.", "..#######..", "...#####...", "..........." };
static icon_t I_TRIM = { "##.......##", "#.........#", "#.........#", "#..#...#..#", "#.##...##.#", "#.##...##.#",
                         "#..#...#..#", "#.........#", "#.........#", "##.......##", "..........." };
static icon_t I_LOOP = { "...#####...", "..#.....#..", ".#.......#.", ".#.......#.", ".#.......#.", ".#.....####",
                         ".#......###", "..#......#.", "...###.....", "...........", "..........." };
static icon_t I_START = { "##.........", "#..........", "#....#.....", "#...##.....", "#..########", "#..########",
                          "#...##.....", "#....#.....", "#..........", "##.........", "..........." };
static icon_t I_END = { ".........##", "..........#", ".....#....#", ".....##...#", "########..#", "########..#",
                        ".....##...#", ".....#....#", "..........#", ".........##", "..........." };
static icon_t I_PLAY = { "...........", "..##.......", "..####.....", "..######...", "..########.", "..########.",
                         "..######...", "..####.....", "..##.......", "...........", "..........." };
static icon_t I_OK = { "...........", "..........#", ".........##", "........##.", ".......##..", "##....##...",
                       ".##..##....", "..####.....", "...##......", "...........", "..........." };
static icon_t I_X = { "...........", ".##.....##.", "..##...##..", "...##.##...", "....###....", "....###....",
                      "...##.##...", "..##...##..", ".##.....##.", "...........", "..........." };
static icon_t I_DIR = { "...........", ".####......", "#....#####.", "#.........#", "#.........#", "#.........#",
                        "#.........#", "#.........#", "###########", "...........", "..........." };
static icon_t I_NORM = { "###########", "...........", "..#.....#..", ".###...###.", "#####.#####", "..#.....#..",
                         "..#..#..#..", "..#..#..#..", "..#..#..#..", "..#..#..#..", "..........." };
static icon_t I_REV = { "......#....", "......##...", "##########.", "......##...", "......#....", "....#......",
                        "...##......", ".##########", "...##......", "....#......", "..........." };
static icon_t I_FIN = { "..........#", ".........##", "........#.#", ".......#..#", "......#...#", ".....#....#",
                        "....#.....#", "...#......#", "..#.......#", "###########", "..........." };
static icon_t I_FOUT = { "#..........", "##.........", "#.#........", "#..#.......", "#...#......", "#....#.....",
                         "#.....#....", "#......#...", "#.......#..", "###########", "..........." };
static icon_t *const ROW_ICON[RFM_MOVE_SMP_N] = { &I_SRC, &I_MODE, &I_LEN, &I_THR, &I_IN, &I_MON, &I_TO };
static icon_t *const TAB_ICON[RFM_MOVE_SMP_TAB_N] = { &I_DOT, &I_LIST, &I_TRIM, &I_LOOP };
static void ic(mr_canvas_t *cv, icon_t i, int x, int y, int v)
{
    int r, c;
    for (r = 0; r < 11; r++)
        for (c = 0; c < 11 && i[r][c]; c++)
            if (i[r][c] == '#')
                v2_px(cv, x + c, y + r, v);
}

/* ---- (stage 2) what a take can do: its actions' row (a take: Play, Trim, Delete; stems: Play, Delete; "to a kit pad"
 * and "to the looper" go between Trim and Delete when they are built, as the mock-up's row), the Tools tab's */
enum { SCR_TAB, SCR_ACT, SCR_TRIM };
enum { A_PLAY, A_TRIM, A_DEL };
static const uint8_t ACT_TAKE[] = { A_PLAY, A_TRIM, A_DEL }, ACT_STEMS[] = { A_PLAY, A_DEL };
static icon_t *const ACT_ICON[] = { &I_PLAY, &I_TRIM, &I_X };
static const char *const ACT_NAME[] = { "Play", "Trim", "Delete" };
enum { T_TRIM, T_NORM, T_REV, T_FIN, T_FOUT, T_N };
static icon_t *const TOOL_ICON[T_N] = { &I_TRIM, &I_NORM, &I_REV, &I_FIN, &I_FOUT };
static const char *const TOOL_NAME[T_N] = { "Trim", "Normalize", "Reverse", "Fade In", "Fade Out" };
static const char *const TOOL_SAY[T_N] = { "Trim", "Normalize", "Reverse", "Fade in", "Fade out" };
static icon_t *const TRIM_ICON[4] = { &I_START, &I_END, &I_PLAY, &I_OK };
static const char *const TRIM_SAY[4] = { "Start", "End", "Play", "Keep" };
static const uint16_t FADE_MS[] = { 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
#define NFADE ((int)(sizeof FADE_MS / sizeof FADE_MS[0]))
#define FADE_DEF 4                                      /* (100 ms) */
#define MSG_INFO 4                                      /* (msg: one of ours, "Deleted", "Not done") */
#define NOASK 0xFFFFu

/* ================================================================ the settings === */
void rfmv_smp_defaults(int8_t v[RFM_MOVE_SMP_N]) { memcpy(v, DEF, sizeof DEF); }
static int in_range(int k, int v)
{
    switch (k) {
    case RFM_MOVE_SMP_SOURCE: return v >= 0 && v < RFM_MOVE_SMP_SRC_N;
    case RFM_MOVE_SMP_MODE: return v >= 0 && v < RFM_MOVE_SMP_M_N;
    case RFM_MOVE_SMP_LENGTH: return v >= 0 && v < RFM_MOVE_SMP_LENGTHS;
    case RFM_MOVE_SMP_THRESHOLD: return v >= RFM_MOVE_SMP_THR_MIN && v <= 0;
    case RFM_MOVE_SMP_INPUT: return v >= RFM_MOVE_SMP_IN_MIN && v <= RFM_MOVE_SMP_IN_MAX;
    case RFM_MOVE_SMP_MONITOR: return v == 0 || v == 1;
    case RFM_MOVE_SMP_TO: return v >= 0 && v < RFM_MOVE_SMP_TO_N;
    default: return 0;
    }
}
void rfmv_smp_load(int8_t v[RFM_MOVE_SMP_N], const uint8_t *p)
{
    int k;
    for (k = 0; k < RFM_MOVE_SMP_N; k++)
        v[k] = (int8_t)(in_range(k, (int8_t)p[k]) ? (int8_t)p[k] : DEF[k]);
}
int rfmv_smp_cap(const rfm_move_t *m) { return (m->fw.caps & RFM_MOVE_CAP_SAMPLER) != 0; }
static int wire(int k, int v) { return k == RFM_MOVE_SMP_LENGTH ? LEN_BARS[clampi(v, 0, RFM_MOVE_SMP_LENGTHS - 1)] : v; }
int rfm_move_smp_value(const rfm_move_t *m, int k)
{
    return k >= 0 && k < RFM_MOVE_SMP_N ? wire(k, m->s.smp[k]) : -1;
}
static void send(rfm_move_t *m, int k) { rfmv_emit(m, RFM_MOVE_ACT_SMP_SET, k, 0, 0, wire(k, m->s.smp[k])); }
/* (stage 2) an action about the takes (move.h SMP_ROWS .. SMP_UNDO) */
static void emit_t(rfm_move_t *m, int kind, int index, int value, uint32_t take, uint32_t from, uint32_t count)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = (uint8_t)kind, a.index = (uint8_t)index, a.value = (int16_t)value;
    a.take = take, a.from = from, a.count = count;
    if (m->fn)
        m->fn(m->ctx, &a);
}
void rfm_move_smp_sync(rfm_move_t *m)
{
    int k;
    if (!rfmv_smp_cap(m))
        return;
    for (k = 0; k < RFM_MOVE_SMP_N; k++)
        send(m, k);
}
static unsigned offered(const rfm_move_t *m, int k)    /* a list setting's values the host offers (bits) */
{
    return k == RFM_MOVE_SMP_SOURCE ? S(m)->srcs : S(m)->dests;
}
static void put(rfm_move_t *m, int k, int v)            /* a setting changed: SMP_SET, the settings saved */
{
    if (m->s.smp[k] == v)
        return;
    m->s.smp[k] = (int8_t)v;
    send(m, k);
    rfmv_changed(m);
}
/* a value no longer offered: the first one offered */
static void fit(rfm_move_t *m, int k)
{
    const unsigned o = offered(m, k);
    int v;
    if (!o || (o >> m->s.smp[k] & 1))
        return;
    for (v = 0; !(o >> v & 1); v++) {}
    put(m, k, v);
}
static void fit_row(rfm_move_t *m)
{
    int k;
    if (S(m)->sets >> S(m)->row & 1)
        return;
    for (k = 0; k < RFM_MOVE_SMP_N && !(S(m)->sets >> k & 1); k++) {}
    S(m)->row = (uint8_t)(k < RFM_MOVE_SMP_N ? k : 0);
}
static unsigned tabs(const rfm_move_t *m) { return S(m)->tabs & RFM_MOVE_SMP_TABS_BUILT; }
/* (stage 2) the takes are offered: the Takes or the Tools tab */
static int takes_on(const rfm_move_t *m)
{
    return rfmv_smp_cap(m) && (tabs(m) & (1u << RFM_MOVE_SMP_TAB_TAKES | 1u << RFM_MOVE_SMP_TAB_TOOLS));
}
static void fit_tab(rfm_move_t *m)
{
    int t;
    if (tabs(m) >> S(m)->tab & 1)
        return;
    for (t = 0; t < RFM_MOVE_SMP_TAB_N && !(tabs(m) >> t & 1); t++) {}
    S(m)->tab = (uint8_t)(t < RFM_MOVE_SMP_TAB_N ? t : 0);
    S(m)->scr = SCR_TAB;
}
static void defaults_offer(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    if (!p->srcs)                                       /* (rfm_move_init zeroed them: the defaults) */
        p->srcs = 0x3F, p->sets = 0x7F, p->tabs = 1 << RFM_MOVE_SMP_TAB_RECORD, p->dests = 1 << RFM_MOVE_SMP_TO_FILES,
        p->ask_i = NOASK;
}
static void need(rfm_move_t *m);
void rfm_move_smp_offer(rfm_move_t *m, unsigned sources, unsigned settings, unsigned tabs_, unsigned dests)
{
    rfm_move_smp_t *p = S(m);
    defaults_offer(m);
    if (sources & ((1u << RFM_MOVE_SMP_SRC_N) - 1))
        p->srcs = (uint8_t)(sources & ((1u << RFM_MOVE_SMP_SRC_N) - 1));
    if (settings & ((1u << RFM_MOVE_SMP_N) - 1))
        p->sets = (uint8_t)(settings & ((1u << RFM_MOVE_SMP_N) - 1));
    if (tabs_ & ((1u << RFM_MOVE_SMP_TAB_N) - 1))
        p->tabs = (uint8_t)(tabs_ & ((1u << RFM_MOVE_SMP_TAB_N) - 1));
    if (dests & ((1u << RFM_MOVE_SMP_TO_N) - 1))
        p->dests = (uint8_t)(dests & ((1u << RFM_MOVE_SMP_TO_N) - 1));
    if (rfmv_smp_cap(m))
        fit(m, RFM_MOVE_SMP_SOURCE), fit(m, RFM_MOVE_SMP_TO);
    fit_row(m), fit_tab(m);
    if (!takes_on(m))
        p->scr = SCR_TAB, p->days = 0;
}
void rfmv_smp_caps(rfm_move_t *m, int was)
{
    rfm_move_smp_t *p = S(m);
    defaults_offer(m);
    if (rfmv_smp_cap(m) && !was) {
        m->seq.arec = 0;                                /* (the old audio loop is off) */
        fit_row(m), fit_tab(m);                         /* (the values as saved: the host's offer moves them) */
        rfm_move_smp_sync(m);
    } else if (!rfmv_smp_cap(m) && was)
        p->open = 0, p->edit = 0, p->picking = 0, p->msg = 0, p->flash = 0, p->scr = SCR_TAB, p->fade = 0;
}

/* ================================================================ where it is === */
static uint32_t now(const rfm_move_t *m) { return rfmv_now_ms(m); }
/* a held control takes over the app while held (R3): a Track, a step, the sequencer's modifiers, FILL (a modifier the
 * app took itself, Delete in Takes or Shift in Trim, never shows here: its press was the app's) */
static int yield(const rfm_move_t *m)
{
    static const uint8_t MODS[5] = { MR_CC_SHIFT, MR_CC_LOOP, MR_CC_COPY, MR_CC_DELETE, MR_CC_MUTE };
    int i;
    if (m->track_held || m->seq.step_held || m->seq.fill)
        return 1;
    if (m->s.mode == RFM_MOVE_MODE_SEQ)
        for (i = 0; i < 5; i++)
            if (m->btn[MODS[i]].kind != H_NONE)
                return 1;
    return 0;
}
int rfmv_smp_shown(const rfm_move_t *m)
{
    return rfmv_smp_cap(m) && S(m)->open && !m->seq.projects && !m->seq.smenu && !rfmv_st_on(m);
}
int rfm_move_smp_shown(const rfm_move_t *m) { return rfmv_smp_shown(m) && !rfmv_smp_covered(m); }
int rfmv_smp_covered(const rfm_move_t *m)
{
    const uint32_t c = S(m)->chg_ms;
    if (yield(m))
        return 1;
    if (m->pop && rfmv_pop_up(m, m->pop) && (int32_t)(m->pop_ms - c) > 0)
        return 1;                                       /* (a knob touched under it: its popup over the app, R5) */
    return rfmv_seq_msg_text(m) && (int32_t)(m->seq.msg_ms - c) > 0;
}
static void touched(rfm_move_t *m) { S(m)->chg_ms = now(m); }
static int running(const rfm_move_t *m) { return S(m)->st.state != RFM_MOVE_SMP_ST_IDLE; }
/* (the owner's 2026-11-25: the page's Sampler window) a setting from the host, as the popup's jog sets it: SMP_SET
 * back, the settings saved; value as SMP_SET sends it (Length: the bars) */
int rfm_move_smp_set(rfm_move_t *m, int k, int value)
{
    int v = value;
    defaults_offer(m);
    if (!rfmv_smp_cap(m) || k < 0 || k >= RFM_MOVE_SMP_N || !(S(m)->sets >> k & 1) || running(m))
        return -1;
    if (k == RFM_MOVE_SMP_LENGTH)
        for (v = 0; v < RFM_MOVE_SMP_LENGTHS && LEN_BARS[v] != value; v++) {}
    if (!in_range(k, v) || ((k == RFM_MOVE_SMP_SOURCE || k == RFM_MOVE_SMP_TO) && !(offered(m, k) >> v & 1)))
        return -1;
    if (m->s.smp[k] == v)
        return 0;
    put(m, k, v);
    touched(m);
    return 1;
}
/* (stage 2) the screen over the tab: a take's actions, Trim; else the tab's own */
static int in_takes(const rfm_move_t *m) { return S(m)->scr == SCR_TAB && S(m)->tab == RFM_MOVE_SMP_TAB_TAKES; }
static int in_trim(const rfm_move_t *m) { return S(m)->scr == SCR_TRIM; }
/* where Undo is the app's: the takes' screens (Takes, Tools, a take's actions, Trim) */
static int undo_ours(const rfm_move_t *m) { return S(m)->scr != SCR_TAB || S(m)->tab != RFM_MOVE_SMP_TAB_RECORD; }
static void info(rfm_move_t *m, const char *a, const char *b)   /* a short message (ours) */
{
    rfm_move_smp_t *p = S(m);
    p->msg = MSG_INFO, p->msg_ms = now(m);
    snprintf(p->msg_a, sizeof p->msg_a, "%s", a);
    snprintf(p->msg_b, sizeof p->msg_b, "%s", b ? b : "");
    touched(m);
    p->chg_ms = p->msg_ms;
}

/* ================================================================ (stage 2) the list's rows === */
/* the three rows shown: the chosen one in the middle (at the ends: first / last) */
static int first_of(int sel, int total) { return total <= 3 ? 0 : clampi(sel - 1, 0, total - 3); }
static const rfm_move_smp_row_t *row_get(const rfm_move_t *m, uint32_t f, int i)
{
    int k;
    for (k = 0; k < RFM_MOVE_SMP_CACHE; k++)
        if (m->smp.rc[k].ok && m->smp.rc[k].f == f && m->smp.rc[k].i == i)
            return &m->smp.rc[k].r;
    return NULL;
}
static void row_put(rfm_move_t *m, uint32_t f, int i, const rfm_move_smp_row_t *r)
{
    rfm_move_smp_t *p = S(m);
    const uint32_t cf = p->days ? 0 : p->folder;
    const int cs = p->days ? p->dsel : p->sel, ct = p->days ? p->dtotal : p->total, c0 = first_of(cs, ct);
    int k, v = -1;
    for (k = 0; k < RFM_MOVE_SMP_CACHE && v < 0; k++)
        if (p->rc[k].ok && p->rc[k].f == f && p->rc[k].i == i)
            v = k;
    for (k = 0; k < RFM_MOVE_SMP_CACHE && v < 0; k++)
        if (!p->rc[k].ok)
            v = k;
    for (k = 0; k < RFM_MOVE_SMP_CACHE && v < 0; k++)   /* (else one the screen does not show) */
        if (!(p->rc[k].f == cf && p->rc[k].i >= c0 && p->rc[k].i < c0 + 3) &&
            !(p->rc[k].f == p->folder && p->rc[k].i == p->sel))
            v = k;
    if (v < 0)
        v = 0;
    p->rc[v].r = *r, p->rc[v].f = f, p->rc[v].i = (int16_t)i, p->rc[v].ok = 1;
}
static void rows_forget(rfm_move_t *m)
{
    int k;
    for (k = 0; k < RFM_MOVE_SMP_CACHE; k++)
        S(m)->rc[k].ok = 0;
    S(m)->ask_i = NOASK;
}
/* the rows a list shows, asked when one is missing (once per window until it is answered or the list changes) */
static void need_rows(rfm_move_t *m, int dl)
{
    rfm_move_smp_t *p = S(m);
    const uint32_t f = dl ? 0 : p->folder;
    const int sel = dl ? p->dsel : p->sel, total = dl ? p->dtotal : p->total, known = dl ? p->dknown : p->known;
    int first, i, miss = 0;
    if (!dl && !f) {                                    /* (no day yet: the newest one with takes) */
        if (known || (p->ask_f == RFM_MOVE_SMP_NEWEST && p->ask_i == 0))
            return;
        p->ask_f = RFM_MOVE_SMP_NEWEST, p->ask_i = 0;
        emit_t(m, RFM_MOVE_ACT_SMP_ROWS, 0, 0, RFM_MOVE_SMP_NEWEST, 0, 3);
        return;
    }
    if (known && !total)
        return;
    first = known ? first_of(sel, total) : clampi(sel - 1, 0, 0x7FFF);
    for (i = first; i < first + 3 && (!known || i < total); i++)
        miss |= !row_get(m, f, i);
    if (!miss || (p->ask_f == f && p->ask_i == first))
        return;
    p->ask_f = f, p->ask_i = (uint16_t)first;
    emit_t(m, RFM_MOVE_ACT_SMP_ROWS, 0, 0, f, (uint32_t)first, 3);
}
static int is_take(const rfm_move_smp_row_t *r)
{
    return r && r->id && (r->kind == RFM_MOVE_SMP_ROW_TAKE || r->kind == RFM_MOVE_SMP_ROW_STEMS);
}

/* ================================================================ (stage 2) peaks === */
/* Trim's steps: coarse 1/256 of the take, fine (Shift) 1/16 of that; the zoomed view 64 steps wide */
static uint32_t tstep(const rfm_move_t *m)
{
    uint32_t s = m->smp.cur.frames / 256u;
    if (m->smp.shdown)
        s /= 16u;
    return s ? s : 1;
}
static int zoom_on(const rfm_move_t *m)                 /* the marker Trim zooms on (1 start, 2 end), 0 none */
{
    const rfm_move_smp_t *p = &m->smp;
    if (p->t_move)
        return p->t_move;
    if (p->t_knob)
        return p->t_knob;
    if (p->t_turned && now(m) - p->t_turn_ms < RFM_MOVE_POPUP_MS)
        return p->t_turned;
    return 0;
}
/* the frames a wave shows: [*from, *from + *count) of the take p->cur (Trim zoomed: around the marker) */
static void view(const rfm_move_t *m, uint32_t *from, uint32_t *count)
{
    const rfm_move_smp_t *p = &m->smp;
    const int z = in_trim(m) ? zoom_on(m) : 0;
    const uint32_t n = p->cur.frames;
    *from = 0, *count = n;
    if (z) {
        const uint64_t w = (uint64_t)tstep(m) * 64u;
        const uint32_t at = z == 1 ? p->t_start : p->t_end;
        if (w < n) {
            const int64_t f = (int64_t)at - (int64_t)(w / 2);
            *count = (uint32_t)w;
            *from = (uint32_t)(f < 0 ? 0 : f > (int64_t)(n - w) ? (int64_t)(n - w) : f);
        }
    }
}
static int wave_take(const rfm_move_t *m)               /* a take's wave shows (its actions, Trim, Tools) */
{
    return S(m)->scr != SCR_TAB || (S(m)->tab == RFM_MOVE_SMP_TAB_TOOLS && !S(m)->on_tabs && !S(m)->picking);
}
static void need_peaks(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    uint32_t f, c;
    int k;
    if (!p->cur.id || !p->cur.frames || !(p->scr != SCR_TAB || p->tab == RFM_MOVE_SMP_TAB_TOOLS))
        return;
    view(m, &f, &c);
    for (k = 0; k < 2; k++)
        if (p->pk[k].take == p->cur.id && p->pk[k].from == f && p->pk[k].count == c)
            return;
    if (p->pa_take == p->cur.id && p->pa_from == f && p->pa_count == c)
        return;
    p->pa_take = p->cur.id, p->pa_from = f, p->pa_count = c;
    emit_t(m, RFM_MOVE_ACT_SMP_PEAKS, 0, RFM_MOVE_SMP_WPEAKS, p->cur.id, f, c);
}
static void peaks_forget(rfm_move_t *m, uint32_t take)
{
    int k;
    for (k = 0; k < 2; k++)
        if (S(m)->pk[k].take == take)
            S(m)->pk[k].take = 0;
    if (S(m)->pa_take == take)
        S(m)->pa_take = 0;
}
/* column c of 64 over [from, from + count): its peak (0..255), -1 none come yet. An answer for exactly this view, else
 * one of the same take that holds that frame (as it was: the view moves before its answer comes) */
static int col_peak(const rfm_move_t *m, uint32_t from, uint32_t count, int c)
{
    const rfm_move_smp_t *p = &m->smp;
    const uint64_t at = from + ((uint64_t)count * (uint64_t)(2 * c + 1)) / (2u * RFM_MOVE_SMP_WPEAKS);
    int k;
    for (k = 0; k < 2; k++)
        if (p->pk[k].take == p->cur.id && p->pk[k].from == from && p->pk[k].count == count && p->pk[k].n == RFM_MOVE_SMP_WPEAKS)
            return p->pk[k].pk[c];
    for (k = 0; k < 2; k++)
        if (p->pk[k].take == p->cur.id && p->pk[k].n && p->pk[k].count && at >= p->pk[k].from &&
            at < (uint64_t)p->pk[k].from + p->pk[k].count)
            return p->pk[k].pk[(at - p->pk[k].from) * p->pk[k].n / p->pk[k].count];
    return -1;
}
static void need(rfm_move_t *m)                         /* what the screen shows now, asked */
{
    rfm_move_smp_t *p = S(m);
    if (!takes_on(m) || !p->open)
        return;
    if (p->scr == SCR_TAB && p->tab == RFM_MOVE_SMP_TAB_TAKES)
        need_rows(m, p->days);
    if (p->scr == SCR_TAB && p->tab == RFM_MOVE_SMP_TAB_TOOLS && !p->cur.id) {
        /* Tools: the take chosen in Takes (the last whose actions opened: p->cur); none yet: the day's newest */
        const rfm_move_smp_row_t *r;
        need_rows(m, 0);
        if (p->folder && p->known && (r = row_get(m, p->folder, 0)) && is_take(r))
            p->cur = *r;
    }
    need_peaks(m);
}
static void play_stop(rfm_move_t *m)                    /* a take playing: stopped (leaving its screen) */
{
    rfm_move_smp_t *p = S(m);
    if (p->play_id || p->st.play_take)
        emit_t(m, RFM_MOVE_ACT_SMP_PLAY, 0, 0, p->play_id ? p->play_id : p->st.play_take, 0, 0);
    p->play_id = 0;
}
static void play(rfm_move_t *m, uint32_t from, uint32_t count)   /* p->cur: play it (from, count), or stop it */
{
    rfm_move_smp_t *p = S(m);
    if (p->play_id == p->cur.id) {
        play_stop(m);
        return;
    }
    p->play_id = p->cur.id;
    emit_t(m, RFM_MOVE_ACT_SMP_PLAY, 0, 1, p->cur.id, from, count);
}

/* ================================================================ the controls === */
static int nrows(const rfm_move_t *m, int *cur)          /* the rows offered; cur: the jog's row among them */
{
    int k, n = 0;
    *cur = 0;
    for (k = 0; k < RFM_MOVE_SMP_N; k++)
        if (S(m)->sets >> k & 1) {
            if (k == S(m)->row)
                *cur = n;
            n++;
        }
    return n;
}
static int row_at(const rfm_move_t *m, int i)           /* the i-th row offered */
{
    int k;
    for (k = 0; k < RFM_MOVE_SMP_N; k++)
        if ((S(m)->sets >> k & 1) && i-- == 0)
            return k;
    return 0;
}
static int stepv(const rfm_move_t *m, int k, int v, int d)   /* a value d detents on (no wrap) */
{
    if (k == RFM_MOVE_SMP_SOURCE || k == RFM_MOVE_SMP_TO) {
        const unsigned o = offered(m, k);
        const int n = k == RFM_MOVE_SMP_SOURCE ? RFM_MOVE_SMP_SRC_N : RFM_MOVE_SMP_TO_N;
        for (; d; d += d > 0 ? -1 : 1) {
            int w = v + (d > 0 ? 1 : -1);
            while (w >= 0 && w < n && !(o >> w & 1))
                w += d > 0 ? 1 : -1;
            if (w < 0 || w >= n)
                break;
            v = w;
        }
        return v;
    }
    switch (k) {
    case RFM_MOVE_SMP_MODE: return clampi(v + d, 0, RFM_MOVE_SMP_M_N - 1);
    case RFM_MOVE_SMP_LENGTH: return clampi(v + d, 0, RFM_MOVE_SMP_LENGTHS - 1);
    case RFM_MOVE_SMP_THRESHOLD: return clampi(v + d, RFM_MOVE_SMP_THR_MIN, 0);
    case RFM_MOVE_SMP_INPUT: return clampi(v + d, RFM_MOVE_SMP_IN_MIN, RFM_MOVE_SMP_IN_MAX);
    default: return clampi(v + d, 0, 1);
    }
}
static int act_ops(const rfm_move_t *m, const uint8_t **ops)   /* the actions' row for p->cur */
{
    if (m->smp.cur.kind == RFM_MOVE_SMP_ROW_STEMS) {
        *ops = ACT_STEMS;
        return (int)sizeof ACT_STEMS;
    }
    *ops = ACT_TAKE;
    return (int)sizeof ACT_TAKE;
}
static int tools_ok(const rfm_move_t *m) { return m->smp.cur.id && m->smp.cur.kind == RFM_MOVE_SMP_ROW_TAKE; }
static int fade_ms(const rfm_move_t *m, int i)          /* a Fade's length (i 0 in, 1 out) */
{
    const int v = m->smp.fade_v[i];
    return FADE_MS[v ? clampi(v - 1, 0, NFADE - 1) : FADE_DEF];
}
/* Trim: a marker moved d steps (it never passes the other: 64 frames at least are kept) */
static void tmove(rfm_move_t *m, int which, int d)
{
    rfm_move_smp_t *p = S(m);
    const int64_t n = p->cur.frames, gap = n >= 128 ? 64 : 1, st = tstep(m);
    if (which == 1) {
        const int64_t v = (int64_t)p->t_start + d * st;
        p->t_start = (uint32_t)(v < 0 ? 0 : v > (int64_t)p->t_end - gap ? (int64_t)p->t_end - gap : v);
    } else {
        const int64_t v = (int64_t)p->t_end + d * st;
        p->t_end = (uint32_t)(v > n ? n : v < (int64_t)p->t_start + gap ? (int64_t)p->t_start + gap : v);
    }
}
static void trim_open(rfm_move_t *m, int ret)           /* Trim on p->cur; ret: Back's (1 the actions, 0 the tab) */
{
    rfm_move_smp_t *p = S(m);
    play_stop(m);
    p->scr = SCR_TRIM, p->trim_ret = (uint8_t)ret;
    p->t_start = 0, p->t_end = p->cur.frames, p->t_i = 0, p->t_move = 0, p->t_knob = 0, p->t_turned = 0;
}
static void trim_leave(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    play_stop(m);
    p->scr = p->trim_ret ? SCR_ACT : SCR_TAB, p->t_move = 0, p->t_knob = 0, p->t_turned = 0;
}
static void edit(rfm_move_t *m, int op, int value, uint32_t from, uint32_t count)   /* SMP_EDIT on p->cur */
{
    emit_t(m, RFM_MOVE_ACT_SMP_EDIT, op, value, S(m)->cur.id, from, count);
}
static void del(rfm_move_t *m, const rfm_move_smp_row_t *r)   /* a take into the host's trash */
{
    rfm_move_smp_t *p = S(m);
    if (p->play_id == r->id || p->st.play_take == r->id)
        play_stop(m);
    emit_t(m, RFM_MOVE_ACT_SMP_EDIT, RFM_MOVE_SMP_OP_DELETE, 0, r->id, 0, 0);
    if (p->cur.id == r->id)
        p->cur.id = 0;
    info(m, "Deleted", NULL);
}
static void turn(rfm_move_t *m, int d)
{
    rfm_move_smp_t *p = S(m);
    int cur, n;
    if (!d || running(m))
        return;                                         /* (a take running: its screen, no list) */
    p->msg = 0;
    touched(m);
    if (p->scr == SCR_TRIM) {                           /* Trim: an icon, or the marker the click took */
        if (p->t_move)
            tmove(m, p->t_move, d);
        else
            p->t_i = (uint8_t)clampi(p->t_i + d, 0, 3);
        need(m);
        return;
    }
    if (p->scr == SCR_ACT) {                            /* a take's actions: an icon */
        const uint8_t *ops;
        p->ai = (uint8_t)clampi(p->ai + d, 0, act_ops(m, &ops) - 1);
        return;
    }
    if (p->edit) {                                      /* the popup: the value, heard at once */
        put(m, p->row, stepv(m, p->row, m->s.smp[p->row], d));
        return;
    }
    if (p->fade) {                                      /* a Fade's popup: its length */
        const int i = p->fade - 1, v = p->fade_v[i] ? p->fade_v[i] - 1 : FADE_DEF;
        p->fade_v[i] = (uint8_t)(clampi(v + d, 0, NFADE - 1) + 1);
        return;
    }
    if (p->picking) {                                   /* the tabs: the jog picks one */
        int t = p->tab;
        for (; d; d += d > 0 ? -1 : 1) {
            int w = t + (d > 0 ? 1 : -1);
            while (w >= 0 && w < RFM_MOVE_SMP_TAB_N && !(tabs(m) >> w & 1))
                w += d > 0 ? 1 : -1;
            if (w < 0 || w >= RFM_MOVE_SMP_TAB_N)
                break;
            t = w;
        }
        if (t != p->tab)
            p->tab = (uint8_t)t, p->scr = SCR_TAB;
        need(m);
        return;
    }
    if (p->tab == RFM_MOVE_SMP_TAB_TAKES) {             /* the list: a take, or a day */
        int16_t *sel = p->days ? &p->dsel : &p->sel;
        const int total = p->days ? p->dtotal : p->total;
        if (p->on_tabs) {
            if (d <= 0)
                return;
            p->on_tabs = 0, *sel = 0, d--;
        }
        if (*sel + d < 0)
            p->on_tabs = 1, *sel = 0;
        else
            *sel = (int16_t)clampi(*sel + d, 0, total > 0 ? total - 1 : 0);
        need(m);
        return;
    }
    if (p->tab == RFM_MOVE_SMP_TAB_TOOLS) {             /* Tools: an icon (no tools: nothing but the tabs above) */
        if (p->on_tabs) {
            if (d <= 0)
                return;
            p->on_tabs = 0, p->ti = 0, d--;
        }
        if (p->ti + d < 0 || (!tools_ok(m) && d < 0))
            p->on_tabs = 1, p->ti = 0;
        else if (tools_ok(m))
            p->ti = (uint8_t)clampi(p->ti + d, 0, T_N - 1);
        need(m);
        return;
    }
    n = nrows(m, &cur);
    p->turned = 1, p->turn_ms = now(m);
    if (p->on_tabs) {
        if (d <= 0 || !n)
            return;
        p->on_tabs = 0, cur = 0, d--;
    }
    cur += d;
    if (cur < 0)
        p->on_tabs = 1, cur = 0;                        /* (up past the first row: the tabs) */
    p->row = (uint8_t)row_at(m, clampi(cur, 0, n - 1));
}
static void act_open(rfm_move_t *m, const rfm_move_smp_row_t *r, int ai)
{
    rfm_move_smp_t *p = S(m);
    if (p->cur.id != r->id || p->cur.frames != r->frames)
        play_stop(m);
    p->cur = *r, p->scr = SCR_ACT, p->ai = (uint8_t)ai;
}
static void click(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    if (running(m))
        return;
    p->msg = 0, p->turned = 0;
    touched(m);
    if (p->scr == SCR_TRIM) {
        if (p->t_i < 2)                                 /* start / end: the jog moves it (zoomed), a click ends it */
            p->t_move = (uint8_t)(p->t_move ? 0 : p->t_i + 1);
        else if (p->t_i == 2)                           /* the part kept, heard */
            play(m, p->t_start, p->t_end - p->t_start);
        else {                                          /* Keep: the file cut (nothing cut: just back) */
            if (p->t_start > 0 || p->t_end < p->cur.frames) {
                edit(m, RFM_MOVE_SMP_OP_TRIM, 0, p->t_start, p->t_end - p->t_start);
                info(m, "Trimmed", NULL);
            }
            trim_leave(m);
        }
        need(m);
        return;
    }
    if (p->scr == SCR_ACT) {
        const uint8_t *ops;
        const int n = act_ops(m, &ops), op = ops[clampi(p->ai, 0, n - 1)];
        if (op == A_PLAY)
            play(m, 0, 0);
        else if (op == A_TRIM)
            trim_open(m, 1);
        else {
            del(m, &p->cur);
            p->scr = SCR_TAB;
        }
        need(m);
        return;
    }
    if (p->edit)
        p->edit = 0;
    else if (p->fade) {                                 /* a Fade: applied */
        const int i = p->fade - 1;
        p->fade = 0;
        edit(m, i ? RFM_MOVE_SMP_OP_FADE_OUT : RFM_MOVE_SMP_OP_FADE_IN, fade_ms(m, i), 0, 0);
        info(m, i ? "Faded Out" : "Faded In", NULL);
    } else if (p->picking)
        p->picking = 0;
    else if (p->on_tabs)
        p->picking = 1;                                 /* (one tab: nothing changes but the jog's job) */
    else if (p->tab == RFM_MOVE_SMP_TAB_TAKES) {
        const uint32_t f = p->days ? 0 : p->folder;
        const rfm_move_smp_row_t *r = f || p->days ? row_get(m, f, p->days ? p->dsel : p->sel) : NULL;
        if (!r)
            return;                                     /* (not come yet: nothing) */
        if (p->days) {                                  /* a day: its takes, the newest chosen */
            if (r->id != p->folder)
                p->folder = r->id, p->sel = 0, p->known = 0, p->total = 0;
            p->days = 0;
        } else if (is_take(r) && p->ddown)              /* Delete held + the click: gone (Undo brings it back) */
            del(m, r);
        else if (is_take(r))
            act_open(m, r, 0);
    } else if (p->tab == RFM_MOVE_SMP_TAB_TOOLS) {
        if (!tools_ok(m))
            return;
        switch (p->ti) {
        case T_TRIM: trim_open(m, 0); break;
        case T_NORM: edit(m, RFM_MOVE_SMP_OP_NORMALIZE, 0, 0, 0), info(m, "Normalized", NULL); break;
        case T_REV: edit(m, RFM_MOVE_SMP_OP_REVERSE, 0, 0, 0), info(m, "Reversed", NULL); break;
        default: p->fade = (uint8_t)(p->ti == T_FIN ? 1 : 2); break;
        }
    } else
        p->edit = 1;
    need(m);
}
static void app_open(rfm_move_t *m, int t)               /* t: the Track held (0..3), -1 none */
{
    rfm_move_smp_t *p = S(m);
    rfmv_seq_menu_close(m);                             /* (a Shift menu: the app takes its place) */
    p->open = 1, p->edit = 0, p->picking = 0, p->turned = 0, p->msg = 0, p->fade = 0;   /* (a message said while
                                                         * closed: gone) */
    fit_row(m), fit_tab(m);
    if (t >= 0 && !running(m)) {                        /* Track 1..3: that slot as the source; Track 4: the master */
        const int src = t < 3 ? RFM_MOVE_SMP_SRC_SLOT1 + t : RFM_MOVE_SMP_SRC_MASTER;
        if (S(m)->srcs >> src & 1) {
            put(m, RFM_MOVE_SMP_SOURCE, src);
            if (p->sets >> RFM_MOVE_SMP_SOURCE & 1)
                p->row = RFM_MOVE_SMP_SOURCE, p->on_tabs = 0;
            if (tabs(m) & 1)                            /* (stage 2: the source is the Record tab's: shown there) */
                p->tab = RFM_MOVE_SMP_TAB_RECORD, p->scr = SCR_TAB;
        }
    }
    touched(m);
    need(m);
}
static void app_close(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    play_stop(m);
    p->open = 0, p->edit = 0, p->picking = 0, p->turned = 0, p->msg = 0, p->fade = 0;
    p->t_move = 0, p->t_knob = 0, p->t_turned = 0;
}
/* (stage 2) a button the app takes while it shows: Delete (Takes' list), Undo (the takes' screens), Left / Right
 * (Takes), Shift (Trim: fine). down: the press; its release: nothing (taken, as its press was) */
static int smp_button(rfm_move_t *m, int cc, int down)
{
    rfm_move_smp_t *p = S(m);
    uint8_t *held = cc == MR_CC_DELETE ? &p->ddown : cc == MR_CC_UNDO ? &p->udown : cc == MR_CC_LEFT ? &p->ldown
                  : cc == MR_CC_RIGHT ? &p->rdown : cc == MR_CC_SHIFT ? &p->shdown : NULL;
    int ours;
    if (!held)
        return 0;
    if (!down) {
        if (!*held)
            return 0;
        *held = 0;
        if (cc == MR_CC_SHIFT && in_trim(m))
            need(m);                                    /* (coarse again: the zoom wider) */
        return 1;
    }
    if (!takes_on(m) || !rfmv_smp_shown(m) || yield(m) || running(m))
        return 0;
    switch (cc) {
    case MR_CC_DELETE: ours = in_takes(m); break;           /* (the days: dark, dead) */
    case MR_CC_UNDO: ours = undo_ours(m); break;
    case MR_CC_LEFT:
    case MR_CC_RIGHT: ours = in_takes(m); break;
    default: ours = in_trim(m); break;
    }
    if (!ours)
        return 0;
    *held = 1;
    touched(m);
    if (cc == MR_CC_UNDO && p->st.undo) {               /* the host's one step back */
        emit_t(m, RFM_MOVE_ACT_SMP_UNDO, 0, 0, 0, 0, 0);
        info(m, "Undo", NULL);
    } else if (cc == MR_CC_LEFT && !p->days)            /* the days */
        p->days = 1, p->on_tabs = 0, p->picking = 0, p->msg = 0;
    else if (cc == MR_CC_RIGHT && p->days)              /* .. a day: as its click */
        p->on_tabs = 0, p->picking = 0, click(m);
    else if (cc == MR_CC_SHIFT)
        p->msg = 0;
    need(m);
    return 1;
}
/* (stage 2) Trim takes the knobs: 1 the start, 2 the end, the others dark and dead (R3); their touch zooms */
static int smp_knob(rfm_move_t *m, const mr_event_t *ev)
{
    rfm_move_smp_t *p = S(m);
    const int k = ev->index;
    if (k < 0 || k > 7)
        return 0;
    if (ev->type == MR_EV_TOUCH && !ev->value) {
        if (!(p->kdown >> k & 1))
            return 0;
        p->kdown = (uint8_t)(p->kdown & ~(1u << k));
        if (p->t_knob == k + 1)
            p->t_knob = 0, p->t_turned = (uint8_t)(k + 1), p->t_turn_ms = now(m);   /* (zoomed a second more) */
        need(m);
        return 1;
    }
    if (!in_trim(m) || !rfmv_smp_shown(m) || yield(m) || running(m))
        return 0;
    if (ev->type == MR_EV_TOUCH) {
        p->kdown = (uint8_t)(p->kdown | 1u << k);
        if (k < 2)
            p->t_knob = (uint8_t)(k + 1), p->t_i = (uint8_t)k, p->t_move = 0, p->msg = 0, touched(m);
        need(m);
        return 1;
    }
    if (k < 2 && ev->value) {                           /* (MR_EV_KNOB: signed detents) */
        p->t_i = (uint8_t)k, p->t_move = 0, p->msg = 0;
        tmove(m, k + 1, ev->value);
        p->t_turned = (uint8_t)(k + 1), p->t_turn_ms = now(m);
        touched(m);
        need(m);
    }
    return 1;
}
int rfmv_smp_input(rfm_move_t *m, const mr_event_t *ev)
{
    rfm_move_smp_t *p = S(m);
    if (!rfmv_smp_cap(m) || m->seq.projects)
        return 0;                                       /* (the projects view: Sample is the browser's favourite key) */
    switch (ev->type) {
    case MR_EV_BUTTON:
        if (ev->index != MR_CC_SAMPLE)
            return smp_button(m, ev->index, ev->value != 0);
        if (!ev->value) {
            if (!p->sdown)
                return 0;
            p->sdown = 0;                               /* (its release: nothing) */
            return 1;
        }
        {
            const int th = m->track_held & 15, was = rfmv_smp_shown(m);
            int t;
            if (!p->open && !th && !running(m) && m->s.mode == RFM_MOVE_MODE_FM1)
                return 0;                               /* (the FM-1 layout: Sample is the FM-1's SEL) */
            p->sdown = 1;
            if (th) {                                   /* (the Track's hold used up, as Track + Menu: its menu, tap
                                                         * and double click do not come; its release nothing) */
                for (t = 0; !(th >> t & 1); t++) {}
                m->track_held = (uint8_t)(m->track_held & ~th);
                for (int k = 0; k < 4; k++)
                    if (th >> k & 1)
                        m->tbtn_up[k] = 0;
                rfmv_menu(m, MENU_NONE, 0);
                app_open(m, t);
            } else if (!was) {                          /* (opened, or back over a Shift menu: shown) */
                /* (the owner, 2026-11-24: "once you press back, sampler is closed, then recording stops when you press
                 * sampler button again ... rather then having to press the sample button twice") closed with a take
                 * recording: the same press is the record key too. Armed and waiting: only shown */
                const int rec = !p->open && p->st.state == RFM_MOVE_SMP_ST_RECORDING;
                app_open(m, -1);
                if (rec)
                    rfmv_emit(m, RFM_MOVE_ACT_SMP_PRESS, 0, 1, 0, m->s.smp[RFM_MOVE_SMP_MODE]);
            } else {
                p->msg = 0;
                touched(m);
                rfmv_emit(m, RFM_MOVE_ACT_SMP_PRESS, 0, 1, 0, m->s.smp[RFM_MOVE_SMP_MODE]);
            }
        }
        return 1;
    case MR_EV_KNOB:
        return smp_knob(m, ev);
    case MR_EV_JOG:
        if (!rfmv_smp_shown(m) || yield(m))
            return 0;
        turn(m, ev->value);
        return 1;
    case MR_EV_JOG_CLICK:
        if (!ev->value) {
            if (!p->jdown)
                return 0;
            p->jdown = 0;
            return 1;
        }
        if (!rfmv_smp_shown(m) || yield(m))
            return 0;
        p->jdown = 1;
        click(m);
        return 1;
    case MR_EV_TOUCH:                                   /* (the jog's touch: the app's, nothing under it) */
        if (ev->index >= 0 && ev->index < 8)
            return smp_knob(m, ev);
        return ev->index == 9 && rfmv_smp_shown(m) && !yield(m);
    default: return 0;
    }
}
int rfmv_smp_back(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    if (!rfmv_smp_shown(m))
        return 0;
    touched(m);
    p->msg = 0;
    if (p->edit)
        p->edit = 0;
    else if (p->fade)
        p->fade = 0;
    else if (p->picking)
        p->picking = 0;
    else if (p->scr == SCR_TRIM) {                      /* a marker moving: it stops; else the take as it was */
        if (p->t_move)
            p->t_move = 0;
        else
            trim_leave(m);
    } else if (p->scr == SCR_ACT)                       /* the list (or the tab a preview came over) */
        play_stop(m), p->scr = SCR_TAB;
    else if (in_takes(m) && p->days)                    /* the days: the day's takes again */
        p->days = 0;
    else
        app_close(m);                                   /* (a take goes on; Sample brings the app back and ends it) */
    need(m);
    return 1;
}

/* ================================================================ the host's news === */
int rfm_move_smp_status(rfm_move_t *m, const rfm_move_smp_status_t *st)
{
    rfm_move_smp_t *p = S(m);
    const int was = p->st.state;
    const uint32_t was_play = p->st.play_take;
    if (!rfmv_smp_cap(m) || !st)
        return 0;
    p->st = *st;
    if (p->st.npeaks > RFM_MOVE_SMP_PEAKS)
        p->st.npeaks = RFM_MOVE_SMP_PEAKS;
    if (p->st.level > 32767)
        p->st.level = 32767;
    if (p->st.state > RFM_MOVE_SMP_ST_SAVING)
        p->st.state = RFM_MOVE_SMP_ST_IDLE;
    if (p->st.play_take != was_play)                    /* (the host's word on what plays) */
        p->play_id = p->st.play_take;
    if (p->st.state != was) {
        p->st_ms = now(m);
        if (p->st.state != RFM_MOVE_SMP_ST_SAVING)
            touched(m), p->edit = 0, p->picking = 0, p->turned = 0, p->fade = 0, p->t_move = 0;
        if (!rfmv_smp_shown(m) && (p->st.state == RFM_MOVE_SMP_ST_ARMED || p->st.state == RFM_MOVE_SMP_ST_RECORDING))
            rfmv_say_text(m, p->st.state == RFM_MOVE_SMP_ST_ARMED ? "Armed" : "Recording");   /* (shown: its words) */
    }
    rfmv_say_check(m);
    return rfm_move_smp_shown(m);
}
static void secs(int ms, char *o, int cap)
{
    if (ms < 0)
        ms = 0;
    if (ms < 10000)
        snprintf(o, (size_t)cap, "%d.%02d s", ms / 1000, ms % 1000 / 10);
    else if (ms < 100000)
        snprintf(o, (size_t)cap, "%d.%d s", ms / 1000, ms % 1000 / 100);
    else
        snprintf(o, (size_t)cap, "%d s", ms / 1000);
}
static int ms_of(uint32_t frames, uint32_t rate) { return rate ? (int)((uint64_t)frames * 1000u / rate) : 0; }
static void msg_words(const rfm_move_t *m, char *o, int cap)
{
    const rfm_move_smp_t *p = &m->smp;
    if (p->msg == RFM_MOVE_SMP_EV_SAVED) {
        if (p->ev_bars > 0)
            snprintf(o, (size_t)cap, "%s, %d %s", p->msg_a, p->ev_bars, p->ev_bars == 1 ? "bar" : "bars");
        else
            snprintf(o, (size_t)cap, "%s, %d.%d seconds", p->msg_a, p->ev_ms / 1000, p->ev_ms % 1000 / 100);
    } else if (p->msg == RFM_MOVE_SMP_EV_NOTHING)
        snprintf(o, (size_t)cap, "Nothing to capture");
    else if (p->msg == MSG_INFO) {                      /* ("Faded In": said as words) */
        snprintf(o, (size_t)cap, "%s%s%s", p->msg_a, p->msg_b[0] ? " " : "", p->msg_b);
        for (char *c = o + 1; *c; c++)
            if (c[-1] == ' ' && *c >= 'A' && *c <= 'Z')
                *c = (char)(*c - 'A' + 'a');
    } else
        snprintf(o, (size_t)cap, "%s%s%s", p->msg_a, p->msg_b[0] ? " " : "", p->msg_b);
}
int rfm_move_smp_event(rfm_move_t *m, int kind, int bars, int bpm10, int ms, const char *reason)
{
    rfm_move_smp_t *p = S(m);
    if (!rfmv_smp_cap(m) || kind < RFM_MOVE_SMP_EV_SAVED || kind > RFM_MOVE_SMP_EV_ERROR)
        return 0;
    p->msg = (uint8_t)kind, p->msg_ms = now(m), p->msg_a[0] = p->msg_b[0] = 0;
    p->edit = 0, p->picking = 0, p->turned = 0;
    if (kind == RFM_MOVE_SMP_EV_SAVED) {
        p->ev_bars = (int16_t)clampi(bars, 0, 999), p->ev_bpm10 = (int16_t)clampi(bpm10, 0, 9999), p->ev_ms = ms < 0 ? 0 : ms;
        snprintf(p->msg_a, sizeof p->msg_a, "%s", m->s.smp[RFM_MOVE_SMP_MODE] == RFM_MOVE_SMP_M_CAPTURE ? "Captured" : "Saved");
        if (p->ev_bars > 0)
            snprintf(p->msg_b, sizeof p->msg_b, "%d Bar%s", p->ev_bars, p->ev_bars == 1 ? "" : "s");
        else
            secs(p->ev_ms, p->msg_b, sizeof p->msg_b);
        p->flash = 1, p->flash_ms = now(m);
    } else if (kind == RFM_MOVE_SMP_EV_NOTHING) {
        snprintf(p->msg_a, sizeof p->msg_a, "Nothing");
        snprintf(p->msg_b, sizeof p->msg_b, "to capture");
    } else {                                            /* a reason: printable, on two rows of 10 at most */
        char r[32];
        int i, n = 0, cut = -1;
        for (i = 0; reason && reason[i] && n < 31; i++)
            if (reason[i] >= 32 && reason[i] < 127)
                r[n++] = reason[i];
        r[n] = 0;
        if (!n)
            snprintf(r, sizeof r, "Error"), n = 5;
        if (n <= 10)
            snprintf(p->msg_a, sizeof p->msg_a, "%s", r);
        else {
            for (i = 0; i <= 10 && i < n; i++)
                if (r[i] == ' ')
                    cut = i;
            if (cut < 0)
                cut = 10;
            snprintf(p->msg_a, sizeof p->msg_a, "%.*s", cut, r);
            snprintf(p->msg_b, sizeof p->msg_b, "%.10s", r + cut + (r[cut] == ' '));
        }
    }
    touched(m);
    p->chg_ms = p->msg_ms;
    if (!rfmv_smp_shown(m)) {                           /* (closed: said; the light shows a save) */
        char w[48];
        msg_words(m, w, sizeof w);
        rfmv_say_text(m, w);
    }
    rfmv_say_check(m);
    return rfm_move_smp_shown(m);
}
/* (stage 2) the host's answers */
int rfm_move_smp_rows(rfm_move_t *m, uint32_t asked, uint32_t folder, int total, int from, int n,
                      const rfm_move_smp_row_t *rows)
{
    rfm_move_smp_t *p = S(m);
    int i;
    if (!takes_on(m) || total < 0 || from < 0)
        return 0;
    if (p->ask_i != NOASK && asked == p->ask_f)
        p->ask_i = NOASK;                               /* (answered: a missing row may be asked again) */
    total = total > 0x7FFF ? 0x7FFF : total;
    if (asked == 0 && folder == 0) {                    /* the days */
        p->dtotal = (uint16_t)total, p->dknown = 1;
        p->dsel = (int16_t)clampi(p->dsel, 0, total ? total - 1 : 0);
    } else {
        if (asked == RFM_MOVE_SMP_NEWEST && !p->folder)
            p->folder = folder, p->sel = 0;             /* (the newest day: the list starts there) */
        if (folder != p->folder || (asked != folder && asked != RFM_MOVE_SMP_NEWEST))
            return 0;                                   /* (an older ask's: not the day shown) */
        if (folder && !total) {                         /* (the day is gone, its last take deleted: the newest day) */
            p->folder = 0, p->known = 0, p->sel = 0;
            need(m);
            return rfm_move_smp_shown(m);
        }
        p->total = (uint16_t)total, p->known = 1;
        p->sel = (int16_t)clampi(p->sel, 0, total ? total - 1 : 0);
        if (!folder)
            p->total = 0;                               /* (no takes at all) */
    }
    for (i = 0; rows && i < n && from + i < total; i++)
        row_put(m, folder, from + i, &rows[i]);
    need(m);
    rfmv_say_check(m);
    return rfm_move_smp_shown(m);
}
int rfm_move_smp_peaks(rfm_move_t *m, uint32_t take, uint32_t from, uint32_t count, int n, const uint8_t *peaks)
{
    rfm_move_smp_t *p = S(m);
    int k, v = -1;
    if (!takes_on(m) || !take || !peaks || n <= 0)
        return 0;
    n = n > RFM_MOVE_SMP_WPEAKS ? RFM_MOVE_SMP_WPEAKS : n;
    if (p->pa_take == take && p->pa_from == from && p->pa_count == count)
        p->pa_take = 0;
    for (k = 0; k < 2 && v < 0; k++)                    /* (the same view; else one not this take's; else not whole) */
        if (p->pk[k].take == take && p->pk[k].from == from && p->pk[k].count == count)
            v = k;
    for (k = 0; k < 2 && v < 0; k++)
        if (p->pk[k].take != p->cur.id)
            v = k;
    for (k = 0; k < 2 && v < 0; k++)
        if (p->pk[k].from || p->pk[k].count != p->cur.frames)
            v = k;
    if (v < 0)
        v = 1;
    p->pk[v].take = take, p->pk[v].from = from, p->pk[v].count = count, p->pk[v].n = (uint8_t)n;
    memcpy(p->pk[v].pk, peaks, (size_t)n);
    need(m);
    return rfm_move_smp_shown(m) && wave_take(m) && take == p->cur.id;
}
void rfm_move_smp_changed(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    if (!takes_on(m))
        return;
    rows_forget(m);
    p->known = 0, p->dknown = 0;
    need(m);
}
int rfm_move_smp_done(rfm_move_t *m, int op, int ok, const rfm_move_smp_row_t *row)
{
    rfm_move_smp_t *p = S(m);
    if (!takes_on(m))
        return 0;
    if (row && row->id)
        peaks_forget(m, row->id);                       /* (its file changed: its waves again) */
    if (row && row->id && row->id == p->cur.id) {
        p->cur = *row;
        if (p->t_end > row->frames || p->scr != SCR_TRIM)
            p->t_start = 0, p->t_end = row->frames;
    }
    if (!ok) {                                          /* (the message said at the click replaced) */
        info(m, "Not done", NULL);
        if (!rfmv_smp_shown(m))
            rfmv_say_text(m, "Not done");
    }
    (void)op;
    rfm_move_smp_changed(m);
    rfmv_say_check(m);
    return rfm_move_smp_shown(m);
}
int rfm_move_smp_saved(rfm_move_t *m, const rfm_move_smp_row_t *take, int mode)
{
    rfm_move_smp_t *p = S(m);
    if (!takes_on(m) || !take || !is_take(take))
        return 0;
    if (p->folder == take->folder && !p->days && p->known)
        p->sel++;                                       /* (the take chosen stays chosen: a newer one is above it) */
    rows_forget(m);
    p->known = 0, p->dknown = 0;
    if (p->open && p->st.state != RFM_MOVE_SMP_ST_ARMED && p->st.state != RFM_MOVE_SMP_ST_RECORDING &&
        (mode == RFM_MOVE_SMP_M_CAPTURE || mode == RFM_MOVE_SMP_M_MANUAL || mode == RFM_MOVE_SMP_M_THRESHOLD)) {
        /* (the host's status may still say SAVING: its last status came before the file was whole; a new take armed
         * or recording: nothing comes up over it) */
        /* (the owner: "should launch to a preview afterwards since I don't know what it recorded"; Trim comes up after
         * a Manual or Threshold take) over the tab shown; the list goes to it */
        p->folder = take->folder, p->sel = 0, p->days = 0;
        p->edit = 0, p->picking = 0, p->fade = 0, p->turned = 0;
        if (tabs(m) >> RFM_MOVE_SMP_TAB_TAKES & 1 && p->tab == RFM_MOVE_SMP_TAB_TAKES)
            p->on_tabs = 0;
        if (mode == RFM_MOVE_SMP_M_CAPTURE) {
            act_open(m, take, 0);
            if (!m->seq.playing)                        /* (over a running song: not heard by itself) */
                play(m, 0, 0);
        } else if (take->kind == RFM_MOVE_SMP_ROW_STEMS) {
            /* (no Trim for a stems take in this stage: nothing comes up) */
        } else {
            play_stop(m);
            p->cur = *take;
            trim_open(m, 0);
        }
    }
    need(m);
    rfmv_say_check(m);
    return rfm_move_smp_shown(m);
}

/* ================================================================ the light === */
void rfmv_smp_leds(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3])
{
    const rfm_move_smp_t *p = &m->smp;
    const uint32_t t = now(m);
    rfmv_rgb_t c;
    if (!rfmv_smp_cap(m) || m->seq.projects || m->seq.smenu == RFM_MOVE_SM_GLOBAL ||
        (rfmv_tk_claims(m) & RFM_MOVE_TK_BUTTONS))
        return;                                         /* (Sample is someone else's there) */
    if (p->flash && t - p->flash_ms < RFM_MOVE_SMP_FLASH_MS)
        c = RFMV_GREEN;                                 /* a take saved */
    else if (p->st.state == RFM_MOVE_SMP_ST_RECORDING || p->st.state == RFM_MOVE_SMP_ST_SAVING)
        c = RFMV_RED;                                   /* (the app open or not) */
    else if (p->st.state == RFM_MOVE_SMP_ST_ARMED)
        c = rfmv_sc(RFMV_RED, t % 500u < 250u ? 256 : 30);
    else if (p->open)
        c = RFMV_WHITE;
    else if (m->s.mode == RFM_MOVE_MODE_FM1)
        return;                                         /* (the FM-1 layout, closed: Sample is SEL, its light) */
    else if (m->s.smp[RFM_MOVE_SMP_MODE] == RFM_MOVE_SMP_M_CAPTURE && p->st.listening)
        c = rfmv_sc(RFMV_WHITE, 40);                    /* closed, Capture listening: dim */
    else
        c = rfmv_sc(RFMV_WHITE, 12);                    /* closed: faint (it opens the app, R1) */
    w[L_SAMPLE][0] = c.r, w[L_SAMPLE][1] = c.g, w[L_SAMPLE][2] = c.b;
    if (!takes_on(m) || !rfm_move_smp_shown(m) || running(m))
        return;
    /* (stage 2) what the takes' screens take: lit with a job (R4, R1: full while held), dark and dead without (R3) */
    if (in_takes(m)) {
        w[L_WHITE0 + W_LEFT][0] = p->ldown ? 127 : p->days ? 0 : 60;
        w[L_WHITE0 + W_RIGHT][0] = p->rdown ? 127 : p->days ? 60 : 0;
        w[L_WHITE0 + W_DELETE][0] = p->ddown ? 127 : p->days ? 0 : 60;
    }
    if (undo_ours(m))
        w[L_WHITE0 + W_UNDO][0] = p->udown ? 127 : p->st.undo ? 60 : 0;
    if (in_trim(m)) {
        static const rfmv_rgb_t MK = { 255, 72, 0 };    /* (a menu's knobs: deep orange) */
        int i;
        for (i = 0; i < 8; i++) {
            const rfmv_rgb_t k = i < 2 ? rfmv_sc(MK, (p->kdown >> i & 1) ? 256 : 110) : RFMV_OFF;
            w[L_RING0 + i][0] = k.r, w[L_RING0 + i][1] = k.g, w[L_RING0 + i][2] = k.b;
        }
        w[L_WHITE0 + W_SHIFT][0] = p->shdown ? 127 : 40;
    }
}

/* ================================================================ words === */
static void val_text(const rfm_move_t *m, int k, int pop, char *o, int cap)
{
    const int v = m->s.smp[k];
    switch (k) {
    case RFM_MOVE_SMP_SOURCE: snprintf(o, (size_t)cap, "%s", SRC_NAME[clampi(v, 0, RFM_MOVE_SMP_SRC_N - 1)]); break;
    case RFM_MOVE_SMP_MODE: snprintf(o, (size_t)cap, "%s", MODE_NAME[clampi(v, 0, RFM_MOVE_SMP_M_N - 1)]); break;
    case RFM_MOVE_SMP_LENGTH: {
        const int b = LEN_BARS[clampi(v, 0, RFM_MOVE_SMP_LENGTHS - 1)];
        if (!b)
            snprintf(o, (size_t)cap, "%s", m->s.smp[RFM_MOVE_SMP_MODE] == RFM_MOVE_SMP_M_CAPTURE ? "Auto" : "Open");
        else
            snprintf(o, (size_t)cap, "%d Bar%s", b, b == 1 ? "" : "s");
        break;
    }
    case RFM_MOVE_SMP_THRESHOLD: snprintf(o, (size_t)cap, pop ? "%d" : "%d dB", v); break;
    case RFM_MOVE_SMP_INPUT: snprintf(o, (size_t)cap, v ? (pop ? "%+d" : "%+d dB") : (pop ? "%d" : "%d dB"), v); break;
    case RFM_MOVE_SMP_MONITOR: snprintf(o, (size_t)cap, "%s", v ? "On" : "Off"); break;
    default: snprintf(o, (size_t)cap, "%s", TO_NAME[clampi(v, 0, RFM_MOVE_SMP_TO_N - 1)]); break;
    }
}
static void row_words(const rfm_move_t *m, int k, char *o, int cap)
{
    char v[24];
    val_text(m, k, 0, v, sizeof v);
    if (k == RFM_MOVE_SMP_LENGTH)                       /* ("4 bars") */
        for (char *c = v; *c; c++)
            *c = *c == 'B' && c > v ? 'b' : *c;
    snprintf(o, (size_t)cap, "%s: %s", SAY_NAME[k], v);
}
/* (stage 2) a take's length: "4 Bars", "7.25 s" (say: "4 bars", "7.2 seconds") */
static void take_len(const rfm_move_smp_row_t *r, int say, char *o, int cap)
{
    const int ms = ms_of(r->frames, r->rate);
    if (r->bars)
        snprintf(o, (size_t)cap, say ? "%d bar%s" : "%d Bar%s", r->bars, r->bars == 1 ? "" : "s");
    else if (say)
        snprintf(o, (size_t)cap, "%d.%d seconds", ms / 1000, ms % 1000 / 100);
    else
        secs(ms, o, cap);
}
static void take_words(const rfm_move_smp_row_t *r, char *o, int cap)
{
    char l[24];
    take_len(r, 1, l, sizeof l);
    snprintf(o, (size_t)cap, "%s, %s", r->src < RFM_MOVE_SMP_SRC_N ? SRC_NAME[r->src] : "Take", l);
}
static void day_text(const rfm_move_smp_row_t *r, int say, char *o, int cap)
{
    if (r->today)
        snprintf(o, (size_t)cap, "Today");
    else
        snprintf(o, (size_t)cap, "%d %s", r->day, (say ? MONTH_SAY : MONTH)[clampi(r->month, 1, 12) - 1]);
}
/* Trim: a marker's time ("1.84 s"; Shift held: to the ms, "1.843 s") */
static void t_time(const rfm_move_t *m, int which, int say, char *o, int cap)
{
    const rfm_move_smp_t *p = &m->smp;
    const int ms = ms_of(which == 1 ? p->t_start : p->t_end, p->cur.rate);
    if (say)
        snprintf(o, (size_t)cap, "%d.%03d seconds", ms / 1000, ms % 1000);
    else if (p->shdown)
        snprintf(o, (size_t)cap, "%d.%03d s", ms / 1000, ms % 1000);
    else
        secs(ms, o, cap);
}
int rfmv_smp_pop_words(const rfm_move_t *m, char *o, int cap, uint16_t *subj, uint32_t *stamp)
{
    const rfm_move_smp_t *p = &m->smp;
    o[0] = 0, *stamp = p->chg_ms;
    if (p->msg && now(m) - p->msg_ms < RFM_MOVE_POPUP_MS) {
        msg_words(m, o, cap);
        *subj = (uint16_t)(0x800 | p->msg), *stamp = p->msg_ms;
    } else if (p->st.state == RFM_MOVE_SMP_ST_ARMED || p->st.state == RFM_MOVE_SMP_ST_RECORDING ||
               p->st.state == RFM_MOVE_SMP_ST_SAVING) {
        snprintf(o, (size_t)cap, "%s", p->st.state == RFM_MOVE_SMP_ST_ARMED ? "Armed" : "Recording");
        *subj = 0x810, *stamp = 0;
    } else if (p->scr == SCR_TRIM) {                    /* (stage 2) Trim: the chosen icon, a marker's time */
        char t[32];
        if (p->t_i < 2) {
            t_time(m, p->t_i + 1, 1, t, sizeof t);
            snprintf(o, (size_t)cap, "%s, %s", TRIM_SAY[p->t_i], t);
        } else
            snprintf(o, (size_t)cap, "%s", TRIM_SAY[p->t_i & 3]);
        *subj = (uint16_t)(0x860 | p->t_i | p->t_move << 2), *stamp = 0;
    } else if (p->scr == SCR_ACT) {                     /* a take's actions: the chosen one's name */
        const uint8_t *ops;
        const int n = act_ops(m, &ops);
        snprintf(o, (size_t)cap, "%s", ACT_NAME[ops[clampi(p->ai, 0, n - 1)]]);
        *subj = (uint16_t)(0x850 | p->ai), *stamp = 0;
    } else if (p->edit) {
        row_words(m, p->row, o, cap);
        *subj = (uint16_t)(0x820 | p->row);
    } else if (p->fade) {                               /* a Fade's popup */
        snprintf(o, (size_t)cap, "%s: %d ms", TOOL_SAY[p->fade == 1 ? T_FIN : T_FOUT], fade_ms(m, p->fade - 1));
        *subj = 0x870;
    } else if (p->picking || p->on_tabs) {
        snprintf(o, (size_t)cap, p->picking ? "Tab: %s" : "Tabs, %s", TAB_NAME[p->tab % RFM_MOVE_SMP_TAB_N]);
        *subj = (uint16_t)(p->picking ? 0x830 : 0x831);
    } else if (p->tab == RFM_MOVE_SMP_TAB_TAKES) {      /* the list: the chosen row (not come yet: nothing) */
        const rfm_move_smp_row_t *r = p->days ? row_get(m, 0, p->dsel) : p->folder ? row_get(m, p->folder, p->sel) : NULL;
        if ((p->days ? p->dknown && !p->dtotal : p->known && !p->total))
            snprintf(o, (size_t)cap, "No takes");
        else if (r && p->days)
            day_text(r, 1, o, cap);
        else if (is_take(r))
            take_words(r, o, cap);
        *subj = (uint16_t)(p->days ? 0x881 : 0x880), *stamp = 0;
    } else if (p->tab == RFM_MOVE_SMP_TAB_TOOLS) {      /* Tools: the chosen tool */
        if (tools_ok(m))
            snprintf(o, (size_t)cap, "%s", TOOL_SAY[p->ti % T_N]);
        else
            snprintf(o, (size_t)cap, "%s", p->cur.id ? "Stems" : "No takes");
        *subj = 0x890, *stamp = 0;
    } else {
        row_words(m, p->row, o, cap);
        *subj = 0x840;
    }
    return 1;
}

/* ================================================================ the screen === */
/* log2(x) in 1/65536 (x > 0), no libm */
static int32_t log2_q16(uint32_t x)
{
    int ip = 31, i;
    uint64_t v;
    int32_t frac = 0;
    while (!(x >> ip))
        ip--;
    v = (uint64_t)x << (30 - ip);                       /* [2^30, 2^31): 1.0 .. 2.0 in Q30 */
    for (i = 1; i <= 16; i++) {
        v = (v * v) >> 30;
        if (v >= (1ull << 31))
            v >>= 1, frac |= 1 << (16 - i);
    }
    return ip * 65536 + frac;
}
/* a level 0..32767 as w pixels of a -60..0 dB scale (rounded) */
int rfmv_smp_lvpx(int level, int w)
{
    int32_t cdb;
    if (level <= 0)
        return 0;
    cdb = (int32_t)((int64_t)(log2_q16((uint32_t)clampi(level, 1, 32767)) - log2_q16(32767)) * 60206 / 6553600);
    return clampi((int)(((int64_t)(cdb + 6000) * w * 2 + 6000) / 12000), 0, w);   /* (cdb: 1/100 dB) */
}
static int dbpx(int db, int w) { return clampi(((db + 60) * w * 2 + 60) / 120, 0, w); }
static void sign(const rfm_move_t *m, mr_canvas_t *cv)  /* the source's sign, top right */
{
    const int s = clampi(m->s.smp[RFM_MOVE_SMP_SOURCE], 0, RFM_MOVE_SMP_SRC_N - 1);
    if (s == RFM_MOVE_SMP_SRC_INPUT)
        ic(cv, I_IN, 117, 1, 1);
    else
        v2_T(cv, 128 - v2_tw(SRC_SIGN[s], 2), 0, SRC_SIGN[s], 2, 1);
}
/* the top line: the tabs as icons (the shown one underlined; the jog on them: a white block), the source's sign (the
 * Record tab's) */
static void top(const rfm_move_t *m, mr_canvas_t *cv, int on)
{
    const rfm_move_smp_t *p = &m->smp;
    int t, j = 0;
    for (t = 0; t < RFM_MOVE_SMP_TAB_N; t++) {
        const int x = 2 + j * 18, cur = t == p->tab;
        if (!(tabs(m) >> t & 1))
            continue;
        if (cur && on)
            v2_R(cv, x - 2, 0, 15, 14, 1);
        ic(cv, *TAB_ICON[t], x, 1, !(cur && on));
        if (cur && !on)
            v2_R(cv, x, 13, 11, 1, 1);
        j++;
    }
    if (p->tab == RFM_MOVE_SMP_TAB_RECORD)
        sign(m, cv);
}
static void rowi(const rfm_move_t *m, mr_canvas_t *cv, int y, int k, int chosen)
{
    char v[24];
    val_text(m, k, 0, v, sizeof v);
    if (chosen)
        v2_R(cv, 0, y, 128, 15, 1);
    ic(cv, *ROW_ICON[k], 3, y + 2, !chosen);
    v2_T(cv, 20, y + 1, v, 2, !chosen);
}
static void marks(mr_canvas_t *cv, int n, int cur, int y)
{
    int i;
    for (i = 0; i < n; i++)
        v2_R(cv, 3 + i * 18, i == cur ? y - 1 : y, 12, i == cur ? 3 : 1, 1);
}
static void meter(mr_canvas_t *cv, int level, int mark)
{
    const int lv = rfmv_smp_lvpx(level, 28);
    if (lv)
        v2_R(cv, 121, 45 - lv, 6, lv, 1);
    v2_dotV(cv, 124, 17, 28);
    if (mark >= 0)
        v2_R(cv, 117, 45 - mark, 11, 1, 1);
}
/* the wave: RFM_MOVE_SMP_COLS columns 2 px apart from x 0, about the line y0 (h each way); a column with no peak a dot */
static void wave(const rfm_move_t *m, mr_canvas_t *cv, int y0, int h)
{
    const rfm_move_smp_status_t *s = &m->smp.st;
    int c;
    for (c = 0; c < RFM_MOVE_SMP_COLS; c++) {
        int i, a = 0;
        if (s->span)
            i = c * s->span / RFM_MOVE_SMP_COLS;        /* (a take with bars: its whole length) */
        else
            i = s->npeaks - RFM_MOVE_SMP_COLS + c;      /* (a history: the newest at the right) */
        if (i >= 0 && i < s->npeaks)
            a = (s->peaks[i] * h + 127) / 255;
        v2_R(cv, c * 2, y0 - a, 1, a ? a * 2 : 1, 1);
    }
}
static void run_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_smp_status_t *s = &m->smp.st;
    const int armed = s->state == RFM_MOVE_SMP_ST_ARMED;
    char b[16];
    if (!armed || now(m) % 500u < 250u)
        ic(cv, I_DOT, 2, 1, 1);                         /* (armed: blinking) */
    if (s->bars)
        snprintf(b, sizeof b, "%d/%d", s->bar, s->bars);
    else {
        const uint32_t sec = s->rate ? s->frames / s->rate : 0;
        snprintf(b, sizeof b, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    }
    v2_T(cv, 20, 0, b, 2, 1);
    sign(m, cv);
    if (!armed)
        wave(m, cv, 34, 15);
    else {
        int c;
        for (c = 0; c < RFM_MOVE_SMP_COLS; c++)         /* (no wave yet: the line) */
            v2_R(cv, c * 2, 34, 1, 1, 1);
    }
    if (s->bars > 1 && s->bars <= 16) {
        int k;
        for (k = 1; k < s->bars; k++)
            v2_dotV(cv, k * 116 / s->bars, 18, 34);
    }
    meter(cv, s->level, armed && m->s.smp[RFM_MOVE_SMP_MODE] == RFM_MOVE_SMP_M_THRESHOLD
                        ? dbpx(m->s.smp[RFM_MOVE_SMP_THRESHOLD], 28) : -1);
    if (s->bars)
        v2_strip(cv, s->bars > 32 ? 32 : s->bars, s->bar ? 1u << ((s->bar - 1) & 31) : 0,
                 armed || !s->span ? -1.0 : (double)(s->npeaks < s->span ? s->npeaks : s->span) / s->span, 0, 116);
}

/* ---- (stage 2) the takes' screens */
/* a take's row: its source's sign, its length, its wave as a small picture (14 columns from x 99); the mock's take() */
static void take_row(mr_canvas_t *cv, int y, const rfm_move_smp_row_t *r, int chosen)
{
    char l[24];
    int c;
    if (chosen)
        v2_R(cv, 0, y, 128, 15, 1);
    if (!is_take(r))
        return;                                         /* (not come yet: empty) */
    if (r->src == RFM_MOVE_SMP_SRC_INPUT)
        ic(cv, I_IN, 3, y + 2, !chosen);
    else
        v2_T(cv, 3, y + 1, r->src < RFM_MOVE_SMP_SRC_N ? SRC_SIGN[r->src] : "?", 2, !chosen);
    take_len(r, 0, l, sizeof l);
    v2_T(cv, 22, y + 1, l, 2, !chosen);
    for (c = 0; c < RFM_MOVE_SMP_THUMB; c++) {
        const int a = (r->thumb[c] * 5 + 127) / 255;
        v2_R(cv, 99 + c * 2, y + 7 - a, 1, a ? a * 2 : 1, !chosen);
    }
}
static void day_row(mr_canvas_t *cv, int y, const rfm_move_smp_row_t *r, int chosen)
{
    char t[16];
    if (chosen)
        v2_R(cv, 0, y, 128, 15, 1);
    if (!r || r->kind != RFM_MOVE_SMP_ROW_DAY)
        return;
    day_text(r, 0, t, sizeof t);
    ic(cv, I_DIR, 3, y + 2, !chosen);
    v2_T(cv, 20, y + 1, t, 2, !chosen);
}
static void takes_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_smp_t *p = &m->smp;
    const int dl = p->days, on = p->on_tabs || p->picking;
    const uint32_t f = dl ? 0 : p->folder;
    const int sel = dl ? p->dsel : p->sel, total = dl ? p->dtotal : p->total, known = dl ? p->dknown : p->known;
    int first, i;
    top(m, cv, on);
    if (known && !total) {
        v2_TC(cv, 64, 32, "No takes", 2, 1);
        return;
    }
    first = known ? first_of(sel, total) : clampi(sel - 1, 0, 0x7FFF);
    for (i = 0; i < 3 && (!known || first + i < total); i++) {
        const rfm_move_smp_row_t *r = (f || dl) ? row_get(m, f, first + i) : NULL;
        const int ch = !on && first + i == sel;
        if (dl)
            day_row(cv, 16 + i * 16, r, ch);
        else
            take_row(cv, 16 + i * 16, r, ch);
    }
}
/* a take's wave over [from, from + count): 64 columns 2 px apart, about y0 (h each way); the kept part [ka, kb) a white
 * block with black columns (the mock's wavec); the playing position a line */
static void take_wave(const rfm_move_t *m, mr_canvas_t *cv, uint32_t from, uint32_t count, int y0, int h, int64_t ka,
                      int64_t kb)
{
    const rfm_move_smp_t *p = &m->smp;
    int c, xa = 0, xb = 0;
    if (!count)
        return;
    if (kb > ka) {
        xa = (int)clampi((int)(((ka - (int64_t)from) * 128 + (int64_t)count / 2) / (int64_t)count), 0, 128);
        xb = (int)clampi((int)(((kb - (int64_t)from) * 128 + (int64_t)count / 2) / (int64_t)count), 0, 128);
        if (xb > xa)
            v2_R(cv, xa, y0 - h - 1, xb - xa, 2 * h + 2, 1);
    }
    for (c = 0; c < RFM_MOVE_SMP_WPEAKS; c++) {
        const int pk = col_peak(m, from, count, c), x = c * 2, in = x >= xa && x < xb;
        int a;
        if (pk < 0)
            continue;                                   /* (not come yet) */
        a = (pk * h + 127) / 255;
        v2_R(cv, x, y0 - a, 1, a ? a * 2 : 1, !in);
    }
    if (p->play_id == p->cur.id && p->st.play_take == p->cur.id && p->st.play_pos >= from &&
        p->st.play_pos < (uint64_t)from + count)
        v2_XR(cv, (int)(((uint64_t)(p->st.play_pos - from) * 128u) / count), y0 - h - 1, 1, 2 * h + 2);
}
/* a row of icons (x 10 + 25 i, the mock's), the chosen one a white block, its name the only word */
static void icon_row(mr_canvas_t *cv, icon_t *const *icons, int n, int chosen, const char *name)
{
    int i;
    for (i = 0; i < n; i++) {
        const int x = 10 + i * 25;
        if (i == chosen)
            v2_R(cv, x - 2, 26, 15, 15, 1);
        ic(cv, *icons[i], x, 28, i != chosen);
    }
    if (name)
        v2_TC(cv, 64, 47, name, 2, 1);
}
static void act_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_smp_t *p = &m->smp;
    const uint8_t *ops;
    const int n = act_ops(m, &ops), ai = clampi(p->ai, 0, n - 1);
    icon_t *icons[4];
    int i;
    for (i = 0; i < n; i++)
        icons[i] = ACT_ICON[ops[i]];
    take_wave(m, cv, 0, p->cur.frames, 12, 10, 0, 0);
    icon_row(cv, icons, n, ai, ACT_NAME[ops[ai]]);
}
static void tools_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_smp_t *p = &m->smp;
    const int on = p->on_tabs || p->picking;
    if (p->fade) {                                      /* a Fade's popup: its name, its length */
        char v[16];
        snprintf(v, sizeof v, "%d ms", fade_ms(m, p->fade - 1));
        v2_titleBox(cv, TOOL_NAME[p->fade == 1 ? T_FIN : T_FOUT], v, 64, 128);
        return;
    }
    if (on)
        top(m, cv, 1);                                  /* (the jog on the tabs: they replace the wave) */
    else if (p->cur.id)
        take_wave(m, cv, 0, p->cur.frames, 12, 10, 0, 0);
    if (!p->cur.id) {
        if (p->known && !p->total)
            v2_TC(cv, 64, 32, "No takes", 2, 1);
        return;
    }
    if (!tools_ok(m)) {                                 /* (a stems take: no tools in this stage) */
        v2_TC(cv, 64, 47, "Stems", 2, 1);
        return;
    }
    icon_row(cv, TOOL_ICON, T_N, on ? -1 : p->ti, on ? NULL : TOOL_NAME[p->ti % T_N]);
}
static void trim_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_smp_t *p = &m->smp;
    const int z = zoom_on(m), tm = z ? z : p->t_i < 2 ? p->t_i + 1 : 0;
    uint32_t f, c;
    int i;
    if (tm) {                                           /* the marker's time, top left */
        char t[16];
        t_time(m, tm, 0, t, sizeof t);
        v2_T(cv, 0, 0, t, 2, 1);
    }
    view(m, &f, &c);
    take_wave(m, cv, f, c, 31, 13, p->t_start, p->t_end);
    for (i = 0; i < 4; i++) {
        const int x = 12 + i * 31;
        if (i == p->t_i)
            v2_R(cv, x - 2, 48, 15, 15, 1);
        ic(cv, *TRIM_ICON[i], x, 50, i != p->t_i);
    }
    if (p->t_move)                                      /* (the jog moves it: its icon underlined too) */
        v2_R(cv, 12 + (p->t_move - 1) * 31, 63, 11, 1, 1);
}
int rfmv_smp_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_smp_t *p = &m->smp;
    const int thr = m->s.smp[RFM_MOVE_SMP_THRESHOLD];
    int n, cur;
    if (!rfmv_smp_shown(m))
        return 0;
    mr_gfx_clear(cv, 0);
    if (p->msg && now(m) - p->msg_ms < RFM_MOVE_POPUP_MS) {   /* "Captured" / "4 Bars", a second */
        v2_msg(cv, p->msg_a, p->msg_b);
        return 1;
    }
    if (running(m)) {
        run_screen(m, cv);
        return 1;
    }
    if (p->scr == SCR_TRIM) {
        trim_screen(m, cv);
        return 1;
    }
    if (p->scr == SCR_ACT) {
        act_screen(m, cv);
        return 1;
    }
    if (p->tab == RFM_MOVE_SMP_TAB_TAKES) {
        takes_screen(m, cv);
        return 1;
    }
    if (p->tab == RFM_MOVE_SMP_TAB_TOOLS) {
        tools_screen(m, cv);
        return 1;
    }
    if (p->edit) {                                      /* a setting's popup: its name, its value */
        char v[24];
        val_text(m, p->row, 1, v, sizeof v);
        v2_titleBox(cv, POP_NAME[p->row], v, 64, 128);
        if (p->row == RFM_MOVE_SMP_THRESHOLD || p->row == RFM_MOVE_SMP_INPUT) {   /* the level now, the threshold */
            const int lv = rfmv_smp_lvpx(p->st.level, 112);
            v2_dotH(cv, 8, 58, 112);
            if (lv)
                v2_R(cv, 8, 56, lv, 5, 1);
            v2_R(cv, 8 + dbpx(thr, 112), 52, 2, 12, 1);
        }
        return 1;
    }
    n = nrows(m, &cur);
    if (!p->on_tabs && !p->picking && p->turned && now(m) - p->turn_ms < RFM_MOVE_POPUP_MS && n) {
        const int first = n <= 3 ? 0 : clampi(cur - 1, 0, n - 3);   /* three rows, the chosen in the middle */
        const int lv = rfmv_smp_lvpx(p->st.level, 56);
        int i;
        top(m, cv, 0);
        v2_dotH(cv, 44, 7, 56);                         /* (the level a line on top) */
        if (lv)
            v2_R(cv, 44, 5, lv, 5, 1);
        v2_R(cv, 44 + dbpx(thr, 56), 2, 2, 11, 1);
        for (i = 0; i < 3 && first + i < n; i++)
            rowi(m, cv, 16 + i * 16, row_at(m, first + i), first + i == cur);
        return 1;
    }
    top(m, cv, p->on_tabs || p->picking);              /* one row: the wave, the level, the marks, the chosen setting */
    wave(m, cv, 31, 12);
    meter(cv, p->st.level, dbpx(thr, 28));
    marks(cv, n, p->on_tabs || p->picking ? -1 : cur, 47);
    if (n)
        rowi(m, cv, 49, p->on_tabs || p->picking ? row_at(m, 0) : p->row, !(p->on_tabs || p->picking));
    return 1;
}
/* (rfm_move_seq_advance, speech on) a message's second over: 1 once, when the list shows again (its words due) */
/* (stage 2, rfm_move_seq_advance) Trim's zoom after a knob's turn: over after a second, the whole view asked */
void rfmv_smp_poll(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    if (p->t_turned && now(m) - p->t_turn_ms >= RFM_MOVE_POPUP_MS) {
        p->t_turned = 0;
        need(m);
    }
}
int rfmv_smp_tick(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    if (!p->msg || now(m) - p->msg_ms < RFM_MOVE_POPUP_MS)
        return 0;
    p->msg = 0;
    return rfmv_smp_shown(m) && !rfmv_smp_covered(m);
}
int rfmv_smp_timed(const rfm_move_t *m)
{
    const rfm_move_smp_t *p = &m->smp;
    if (!rfmv_smp_cap(m))
        return 0;
    if (p->flash && now(m) - p->flash_ms < RFM_MOVE_SMP_FLASH_MS)
        return 1;
    return rfmv_smp_shown(m) && ((p->turned && now(m) - p->turn_ms < RFM_MOVE_POPUP_MS) ||
                                 (p->msg && now(m) - p->msg_ms < RFM_MOVE_POPUP_MS) || running(m) ||
                                 (p->t_turned && now(m) - p->t_turn_ms < RFM_MOVE_POPUP_MS) ||
                                 (p->play_id && p->st.play_take == p->play_id));
}
