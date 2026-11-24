/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (0.15.0, the owner's 2026-11-24) the sampler app: the host's recorder as an app (move.h, "the sampler app"; README
 * "The sampler"). Only with RFM_MOVE_CAP_SAMPLER. It takes the screen, the jog and Back, and Sample (its record key);
 * everything else stays the layer's under it. The settings are a list on the jog: at rest one row (the chosen
 * setting) under the source's live wave and level; while the jog turns three rows a second; up past the first row the
 * jog is on the tabs (a click, the jog picks a tab, a click or Back ends it); a row's click is its popup (its name and
 * value, the jog changes it). A take running: its own screen (the bars, its wave, the level). The screens are the
 * mock-up's (remoteFM14Move-recorder docs/design/mock_recorder_v4.c), pixel for pixel. The core times nothing of a
 * take: the host's status says where it is. */
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

/* ---- the icons (the mock-up's, 11 x 11) */
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
static void fit_tab(rfm_move_t *m)
{
    int t;
    if (tabs(m) >> S(m)->tab & 1)
        return;
    for (t = 0; t < RFM_MOVE_SMP_TAB_N && !(tabs(m) >> t & 1); t++) {}
    S(m)->tab = (uint8_t)(t < RFM_MOVE_SMP_TAB_N ? t : 0);
}
static void defaults_offer(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    if (!p->srcs)                                       /* (rfm_move_init zeroed them: the defaults) */
        p->srcs = 0x3F, p->sets = 0x7F, p->tabs = 1 << RFM_MOVE_SMP_TAB_RECORD, p->dests = 1 << RFM_MOVE_SMP_TO_FILES;
}
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
        p->open = 0, p->edit = 0, p->picking = 0, p->msg = 0, p->flash = 0;
}

/* ================================================================ where it is === */
static uint32_t now(const rfm_move_t *m) { return rfmv_now_ms(m); }
/* a held control takes over the app while held (R3): a Track, a step, the sequencer's modifiers, FILL */
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
static void turn(rfm_move_t *m, int d)
{
    rfm_move_smp_t *p = S(m);
    int cur, n;
    if (!d || running(m))
        return;                                         /* (a take running: its screen, no list) */
    p->msg = 0;
    touched(m);
    if (p->edit) {                                      /* the popup: the value, heard at once */
        put(m, p->row, stepv(m, p->row, m->s.smp[p->row], d));
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
        p->tab = (uint8_t)t;
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
static void click(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    if (running(m))
        return;
    p->msg = 0, p->turned = 0;
    touched(m);
    if (p->edit)
        p->edit = 0;
    else if (p->picking)
        p->picking = 0;
    else if (p->on_tabs)
        p->picking = 1;                                 /* (one tab: nothing changes but the jog's job) */
    else
        p->edit = 1;
}
static void app_open(rfm_move_t *m, int t)               /* t: the Track held (0..3), -1 none */
{
    rfm_move_smp_t *p = S(m);
    rfmv_seq_menu_close(m);                             /* (a Shift menu: the app takes its place) */
    p->open = 1, p->edit = 0, p->picking = 0, p->turned = 0, p->msg = 0;   /* (a message said while closed: gone) */
    fit_row(m), fit_tab(m);
    if (t >= 0 && !running(m)) {                        /* Track 1..3: that slot as the source; Track 4: the master */
        const int src = t < 3 ? RFM_MOVE_SMP_SRC_SLOT1 + t : RFM_MOVE_SMP_SRC_MASTER;
        if (S(m)->srcs >> src & 1) {
            put(m, RFM_MOVE_SMP_SOURCE, src);
            if (p->sets >> RFM_MOVE_SMP_SOURCE & 1)
                p->row = RFM_MOVE_SMP_SOURCE, p->on_tabs = 0;
        }
    }
    touched(m);
}
static void app_close(rfm_move_t *m)
{
    rfm_move_smp_t *p = S(m);
    p->open = 0, p->edit = 0, p->picking = 0, p->turned = 0, p->msg = 0;
}
int rfmv_smp_input(rfm_move_t *m, const mr_event_t *ev)
{
    rfm_move_smp_t *p = S(m);
    if (!rfmv_smp_cap(m) || m->seq.projects)
        return 0;                                       /* (the projects view: Sample is the browser's favourite key) */
    switch (ev->type) {
    case MR_EV_BUTTON:
        if (ev->index != MR_CC_SAMPLE)
            return 0;
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
    if (p->edit)
        p->edit = 0;
    else if (p->picking)
        p->picking = 0;
    else
        app_close(m);                                   /* (a take goes on; Sample brings the app back and ends it) */
    return 1;
}

/* ================================================================ the host's news === */
int rfm_move_smp_status(rfm_move_t *m, const rfm_move_smp_status_t *st)
{
    rfm_move_smp_t *p = S(m);
    const int was = p->st.state;
    if (!rfmv_smp_cap(m) || !st)
        return 0;
    p->st = *st;
    if (p->st.npeaks > RFM_MOVE_SMP_PEAKS)
        p->st.npeaks = RFM_MOVE_SMP_PEAKS;
    if (p->st.level > 32767)
        p->st.level = 32767;
    if (p->st.state > RFM_MOVE_SMP_ST_SAVING)
        p->st.state = RFM_MOVE_SMP_ST_IDLE;
    if (p->st.state != was) {
        p->st_ms = now(m);
        if (p->st.state != RFM_MOVE_SMP_ST_SAVING)
            touched(m), p->edit = 0, p->picking = 0, p->turned = 0;
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
    else
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
    } else if (p->edit) {
        row_words(m, p->row, o, cap);
        *subj = (uint16_t)(0x820 | p->row);
    } else if (p->picking || p->on_tabs) {
        snprintf(o, (size_t)cap, p->picking ? "Tab: %s" : "Tabs, %s", TAB_NAME[p->tab % RFM_MOVE_SMP_TAB_N]);
        *subj = (uint16_t)(p->picking ? 0x830 : 0x831);
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
/* the top line: the tabs as icons (the shown one underlined; the jog on them: a white block), the source's sign */
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
                                 (p->msg && now(m) - p->msg_ms < RFM_MOVE_POPUP_MS) || running(m));
}
