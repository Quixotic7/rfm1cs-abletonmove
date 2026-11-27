/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (looper stage 1, the owner's 2026-11-26 round: every card lp_* approved as its option a, and lp_scene "A and b and
 * c"; README "The looper (stage 1)"; remoteFM14Move docs/design/LOOPER-SPEC.md, mock_looper_v1.c) the audio looper,
 * Track 4's fourth view. Only with RFM_MOVE_CAP_LOOPER.
 *
 * The grid is Move's own Session Mode: the pads' rows are 4 lanes (lane 1 the top row, beside Track 1), the columns 8
 * scenes; a lane plays one loop at a time. A tap on an empty slot records into it from the next bar (stopped: armed,
 * from Play); a tap on the red pad (or Sample) ends it at the next bar (a lane's Length 1..16 bars: it ends by itself);
 * it plays at once from that bar. A tap on a loop plays it at the launch quantization (the project's Launch setting),
 * a tap on a playing one stops it; a double click: now (UX-RULES R2's "same thing sooner"). Scenes three ways (the
 * owner's "A and b and c"): the left step under a column, Shift + any pad of the column, a slide down the column;
 * step 16 stops all. Choosing a slot without playing it (the owner's: Shift + a pad launches the column now): Loop
 * held + a pad, or the arrows (Left / Right a scene, Up / Down a lane). Copy held + a loop, then slots; Delete held + a
 * loop (or a scene's step); one Undo. Mute held + a loop: muted. The jog turns five pages (Loops, Loop, Record, Loop
 * Mix, Looper Mix) as the kit's, a knob says its name on touch and its value on turn. A slot held + the volume knob:
 * that loop's level; Track 4 held + the volume knob: the Loops strip's.
 *
 * The host owns the audio (LOOP_REC, LOOP_LAUNCH, LOOP_SET, LOOP_LOAD, LOOP_COPY; the status ~30 a second); the core
 * the slots' settings (the project's, version 19) and what is asked; what the host has not confirmed yet is drawn as
 * asked (a launch: waiting), so every press shows at once (R1). The sampler app's Loops tab (looper_tab_*) is the same
 * looper with the jog, for the pads to stay the firmware's. */
#include <stdio.h>
#include <string.h>

#include "move_int.h"
#include "v2.h"

#define clampi rfmv_clampi
#define sc rfmv_sc
#define LP(m) (&(m)->loop)
typedef rfmv_rgb_t rgb_t;

static const rgb_t LOOPC = { 0, 210, 190 };             /* the looper's colour (card lp_colour: teal) */
static const rgb_t LGREEN = { 0, 255, 60 };             /* playing (the mock-up's green) */
#define LANES RFM_MOVE_LOOP_LANES
#define SCENES RFM_MOVE_LOOP_SCENES
#define NSLOT RFM_MOVE_LOOP_SLOTS
#define STOP SCENES                                     /* a lane's next: a stop */

/* ---- the icons (mock_looper_v1.c's, 11 x 11) */
typedef const char *const icon_t[11];
static icon_t I_SRC = { "........##.", "....#...##.", ".....#..##.", "......#.##.", "#######.##.", "#######.##.",
                        "......#.##.", ".....#..##.", "....#...##.", "........##.", "..........." };
static icon_t I_LEN = { "...........", "...........", "#.........#", "#.........#", "#.........#", "###########",
                        "#.........#", "#.........#", "#.........#", "...........", "..........." };
static icon_t I_DOT = { "...........", "...#####...", "..#######..", ".#########.", ".#########.", ".#########.",
                        ".#########.", ".#########.", "..#######..", "...#####...", "..........." };
static icon_t I_LOOP = { "...#####...", "..#.....#..", ".#.......#.", ".#.......#.", ".#.......#.", ".#.....####",
                         ".#......###", "..#......#.", "...###.....", "...........", "..........." };
static icon_t I_PLAY = { "...........", "..##.......", "..####.....", "..######...", "..########.", "..########.",
                         "..######...", "..####.....", "..##.......", "...........", "..........." };
static icon_t I_STOP = { "...........", "...........", "..#######..", "..#######..", "..#######..", "..#######..",
                         "..#######..", "..#######..", "..#######..", "...........", "..........." };
static icon_t I_IN = { "....###....", "....###....", "....###....", "....###....", "..#.###.#..", "..#.###.#..",
                       "...#...#...", "....###....", ".....#.....", "...#####...", "..........." };
static icon_t PI_MIX = { ".....#.....", ".....#.....", ".....#.....", "...#####...", "...#####...", ".....#.....",
                         ".....#.....", ".....#.....", ".....#.....", ".....#.....", ".....#....." };
static icon_t PI_MIXES = { ".#...#...#.", ".#...#...#.", "###..#...#.", "###..#...#.", ".#...#..###", ".#..###.###",
                           ".#..###..#.", ".#...#...#.", ".#...#...#.", ".#...#...#.", ".#...#...#." };
static icon_t PI_LANES = { "...........", "#######....", "#######....", "...........", "##########.", "##########.",
                           "...........", "####.......", "####.......", "...........", "..........." };
static icon_t PI_REC = { "...........", "...###.....", "..#####....", "..#####....", "...###.....", "...........",
                         "#.........#", "#.........#", "###########", "#.........#", "#.........#" };
static void ic(mr_canvas_t *cv, icon_t i, int x, int y, int v)
{
    int r, c;
    for (r = 0; r < 11; r++)
        for (c = 0; c < 11 && i[r][c]; c++)
            if (i[r][c] == '#')
                v2_px(cv, x + c, y + r, v);
}
static void ic2(mr_canvas_t *cv, icon_t i, int x, int y, int v)
{
    int r, c;
    for (r = 0; r < 11; r++)
        for (c = 0; c < 11 && i[r][c]; c++)
            if (i[r][c] == '#')
                v2_R(cv, x + 2 * c, y + 2 * r, 2, 2, v);
}
static void rowi(mr_canvas_t *cv, int y, icon_t i, const char *val, int chosen)
{
    if (chosen)
        v2_R(cv, 0, y, 128, 15, 1);
    ic(cv, i, 3, y + 2, !chosen);
    v2_T(cv, 20, y + 1, val, 2, !chosen);
}

/* ---- the pages (card lp_pages a) */
enum { PG_LOOPS, PG_LOOP, PG_RECORD, PG_LOOPMIX, PG_LOOPERMIX, PG_N };
static icon_t *const PG_ICON[PG_N] = { &PI_LANES, &I_LOOP, &PI_REC, &PI_MIX, &PI_MIXES };
static const char *const PG_NAME[PG_N] = { "Loops", "Loop", "Record", "Loop Mix", "Looper Mix" };
/* a knob's job on a page: a slot's setting (0..), a lane's (KL_SOURCE.. of the chosen slot's lane), the lanes' levels
 * (KL_LVL0 + lane), the strip's (KS_*), -1 dark and dead */
enum { KL_SOURCE = 100, KL_LENGTH, KL_LVL0 = 110, KS_LEVEL = 120, KS_PAN, KS_SEND1, KS_SEND2, KS_SEND3 };
static const int16_t PAGE_K[PG_N][8] = {
    { KL_LVL0, KL_LVL0 + 1, KL_LVL0 + 2, KL_LVL0 + 3, -1, -1, -1, -1 },
    { RFM_MOVE_LOOP_P_START, RFM_MOVE_LOOP_P_LENGTH, RFM_MOVE_LOOP_P_PITCH, RFM_MOVE_LOOP_P_REVERSE, RFM_MOVE_LOOP_P_WARP,
      -1, -1, -1 },                                     /* (6: Launch mode, stage 2) */
    { KL_SOURCE, KL_LENGTH, -1, -1, -1, -1, -1, -1 },
    { RFM_MOVE_LOOP_P_LEVEL, RFM_MOVE_LOOP_P_PAN, RFM_MOVE_LOOP_P_SEND1, RFM_MOVE_LOOP_P_SEND2, RFM_MOVE_LOOP_P_SEND3,
      RFM_MOVE_LOOP_P_MUTE, -1, -1 },
    { KL_LVL0, KL_LVL0 + 1, KL_LVL0 + 2, KL_LVL0 + 3, KS_LEVEL, KS_PAN, KS_SEND1, KS_SEND2 },
};
static const char *const P_NAME[RFM_MOVE_LOOP_P_N] = { "Start", "Length", "Pitch", "Reverse", "Warp", "Level", "Pan",
                                                       "Send 1", "Send 2", "Send 3", "Mute" };
static const char *const SRC_NAME[RFM_MOVE_LOOP_SRC_N] = { "Played", "Track 1", "Track 2", "Track 3", "Kit", "Input",
                                                           "Mix", "Loops" };
static const uint8_t LEN_BARS[RFM_MOVE_LOOP_LENGTHS] = { 0, 1, 2, 4, 8, 16 };
#define PG_CELL 28
#define PG_SLIDE_MS 150
#define DEAD_TICKS 3                                    /* a list value: detents a step */

/* ================================================================ basics === */
int rfmv_loop_cap(const rfm_move_t *m) { return (m->fw.caps & RFM_MOVE_CAP_LOOPER) != 0; }
static uint32_t now(const rfm_move_t *m) { return rfmv_now_ms(m); }
static rfm_move_loop_slot_t *SL(rfm_move_t *m, int s) { return &m->loop.slot[s & (NSLOT - 1)]; }
static const rfm_move_loop_slot_t *CSL(const rfm_move_t *m, int s) { return &m->loop.slot[s & (NSLOT - 1)]; }
static int lane_of(int s) { return s / SCENES; }
static int scene_of(int s) { return s % SCENES; }
static int pad_of(int s) { return (LANES - 1 - lane_of(s)) * 8 + scene_of(s); }   /* lane 1 the top row */
static int slot_of_pad(int p) { return (LANES - 1 - p / 8) * SCENES + p % 8; }
static int has_loop(const rfm_move_loop_slot_t *d) { return d->path[0] != 0; }
static void touched(rfm_move_t *m) { LP(m)->chg_ms = now(m); }
int rfmv_loop_active(const rfm_move_t *m)
{
    return rfmv_loop_cap(m) && m->loop.on && m->s.mode == RFM_MOVE_MODE_SEQ && !m->seq.view && !m->seq.projects;
}
int rfmv_loop_shown(const rfm_move_t *m)
{
    return rfmv_loop_active(m) && !m->seq.smenu && !rfmv_st_on(m) && !rfmv_smp_shown(m);
}
int rfm_move_loop_shown(const rfm_move_t *m) { return rfmv_loop_shown(m) && !m->track_held && !m->seq.fill; }
static int running(const rfm_move_t *m) { return m->seq.playing || m->seq.start_pending; }
static void slot_name(int s, char *o, int cap) { snprintf(o, (size_t)cap, "%d-%d", lane_of(s) + 1, scene_of(s) + 1); }
static int loop_steps(const rfm_move_loop_slot_t *d) { return d->bars ? d->bars * RFM_MOVE_LOOP_STEPS : RFM_MOVE_LOOP_STEPS; }
static void defaults(int16_t v[RFM_MOVE_LOOP_P_N], int bars)
{
    memset(v, 0, sizeof(int16_t) * RFM_MOVE_LOOP_P_N);
    v[RFM_MOVE_LOOP_P_LENGTH] = (int16_t)((bars > 0 ? bars : 1) * RFM_MOVE_LOOP_STEPS);
    v[RFM_MOVE_LOOP_P_LEVEL] = 100, v[RFM_MOVE_LOOP_P_PAN] = 64;
}
static int p_min(int p) { return p == RFM_MOVE_LOOP_P_PITCH ? -2400 : p == RFM_MOVE_LOOP_P_LENGTH ? 1 : 0; }
static int p_max(const rfm_move_loop_slot_t *d, int p)
{
    switch (p) {
    case RFM_MOVE_LOOP_P_START: return loop_steps(d) - 1;
    case RFM_MOVE_LOOP_P_LENGTH: return loop_steps(d);
    case RFM_MOVE_LOOP_P_PITCH: return 2400;
    case RFM_MOVE_LOOP_P_REVERSE:
    case RFM_MOVE_LOOP_P_MUTE: return 1;
    case RFM_MOVE_LOOP_P_WARP: return RFM_MOVE_LOOP_W_N - 1;
    default: return 127;
    }
}
static void empty_slot(rfm_move_loop_slot_t *d)
{
    memset(d, 0, sizeof *d);
    d->st = RFM_MOVE_LOOP_EMPTY;
    defaults(d->v, 1);
}

/* ================================================================ the host's actions === */
static void act(rfm_move_t *m, int kind, int index, int down, int vel, int value, int param, uint32_t take, const char *data)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.frame = m->seq.frame;
    a.kind = (uint8_t)kind, a.index = (uint8_t)index, a.down = (uint8_t)down, a.vel = (uint8_t)vel;
    a.value = (int16_t)value, a.param = (uint8_t)param, a.take = take;
    a.slot = m->fw.active < RFM_MOVE_FW_SLOTS ? m->fw.active : 0;
    if (data) {
        a.data = (const uint8_t *)data;
        a.len = (uint32_t)strlen(data);
    }
    if (m->fn)
        m->fn(m->ctx, &a);
}
static void send_set(rfm_move_t *m, int s, int p) { act(m, RFM_MOVE_ACT_LOOP_SET, s, 0, 0, CSL(m, s)->v[p], p, 0, NULL); }
static void send_lane(rfm_move_t *m, int l, int p)
{
    act(m, RFM_MOVE_ACT_LOOP_SET, RFM_MOVE_LOOP_LANE0 + l, 0, 0, m->loop.lane[l][p], p, 0, NULL);
}
static void send_mix(rfm_move_t *m)
{
    rfm_move_act_t a;
    int j;
    memset(&a, 0, sizeof a);
    a.frame = m->seq.frame;
    a.kind = RFM_MOVE_ACT_MIX, a.slot = RFM_MOVE_LOOP_SLOT;
    for (j = 0; j < RFM_MOVE_KIT_MIX_N; j++)
        a.mix[j] = m->loop.mix[j];
    if (m->fn)
        m->fn(m->ctx, &a);
}
static void send_slot(rfm_move_t *m, int s)             /* its sound and settings */
{
    const rfm_move_loop_slot_t *d = CSL(m, s);
    int p;
    act(m, RFM_MOVE_ACT_LOOP_LOAD, s, 0, 0, 0, 0, 0, has_loop(d) ? d->path : NULL);
    for (p = 0; p < RFM_MOVE_LOOP_P_N; p++)
        send_set(m, s, p);
}
void rfm_move_loop_sync(rfm_move_t *m)
{
    int s, l, p;
    if (!rfmv_loop_cap(m))
        return;
    for (s = 0; s < NSLOT; s++) {
        if (has_loop(CSL(m, s)))
            SL(m, s)->st = RFM_MOVE_LOOP_LOADING;
        send_slot(m, s);
    }
    for (l = 0; l < LANES; l++)
        for (p = 0; p < RFM_MOVE_LOOP_L_N; p++)
            send_lane(m, l, p);
    send_mix(m);
}
static int lq(const rfm_move_t *m) { return m->seq.launch_q < RFM_MOVE_LQ_N ? m->seq.launch_q : RFM_MOVE_LQ_BAR; }
static void msg(rfm_move_t *m, const char *a, const char *b)
{
    rfm_move_loop_t *t = LP(m);
    snprintf(t->msg, sizeof t->msg, "%s", a);
    snprintf(t->msg2, sizeof t->msg2, "%s", b ? b : "");
    t->msg_ms = now(m) ? now(m) : 1;
    t->kpop = -1, t->lvl = 0;
    touched(m);
}
static int msg_up(const rfm_move_t *m) { return m->loop.msg_ms && now(m) - m->loop.msg_ms < RFM_MOVE_POPUP_MS; }

/* ================================================================ the host's status === */
static int lane_play(const rfm_move_t *m, int l) { return m->loop.st_ok ? m->loop.st.lane[l].play : -1; }
/* what waits for its launch time in lane l: a scene, STOP, -1 nothing (asked and not confirmed: as asked) */
static int lane_next(const rfm_move_t *m, int l)
{
    if (m->loop.ask[l] >= 0)
        return m->loop.ask[l];
    return m->loop.st_ok ? m->loop.st.lane[l].next : -1;
}
/* a slot's recording: RFM_MOVE_LOOP_RS_* (the host's when it lists it, else as asked) */
static int rec_state(const rfm_move_t *m, int s)
{
    const rfm_move_loop_t *t = &m->loop;
    int i;
    if (t->st_ok)
        for (i = 0; i < 2; i++)
            if (t->st.rec[i].slot == s && t->st.rec[i].state) {
                if (t->rs[s] == RFM_MOVE_LOOP_RS_ENDING && t->st.rec[i].state == RFM_MOVE_LOOP_RS_RECORDING)
                    return RFM_MOVE_LOOP_RS_ENDING;
                return t->st.rec[i].state;
            }
    return t->rs[s];
}
static int rec_any(const rfm_move_t *m)                 /* the slot recording or armed (the first), -1 */
{
    int s;
    for (s = 0; s < NSLOT; s++)
        if (rec_state(m, s) == RFM_MOVE_LOOP_RS_RECORDING || rec_state(m, s) == RFM_MOVE_LOOP_RS_ENDING)
            return s;
    for (s = 0; s < NSLOT; s++)
        if (rec_state(m, s) == RFM_MOVE_LOOP_RS_ARMED)
            return s;
    return -1;
}
int rfmv_loop_recording(const rfm_move_t *m) { return rfmv_loop_cap(m) && rec_any(m) >= 0; }
int rfm_move_loop_status(rfm_move_t *m, const rfm_move_loop_status_t *st)
{
    rfm_move_loop_t *t = LP(m);
    int l, changed;
    if (!rfmv_loop_cap(m) || !st)
        return 0;
    changed = !t->st_ok || memcmp(&t->st, st, sizeof *st) != 0;
    t->st = *st, t->st_ok = 1;
    for (l = 0; l < LANES; l++) {                       /* (an ask the host now has: its own state from here on) */
        const int a = t->ask[l], pl = st->lane[l].play, nx = st->lane[l].next;
        if (a < 0)
            continue;
        if (nx == a || (a == STOP && pl < 0 && nx < 0) || (a != STOP && pl == a && nx < 0))
            t->ask[l] = -1;
        else if (now(m) - t->ask_ms[l] > 4000u && nx < 0)
            t->ask[l] = -1;                             /* (a host that never took it: given up) */
    }
    {                                                   /* (a recording the host has: its own state; one it no longer
                                                         * lists and has not answered: as asked) */
        int s, i;
        for (s = 0; s < NSLOT; s++) {
            int listed = 0;
            for (i = 0; i < 2; i++)
                listed |= st->rec[i].slot == s && st->rec[i].state;
            if (listed && t->rs[s] == RFM_MOVE_LOOP_RS_ARMED && (st->rec[0].slot == s ? st->rec[0].state : st->rec[1].state) ==
                                                                    RFM_MOVE_LOOP_RS_RECORDING)
                t->rs[s] = RFM_MOVE_LOOP_RS_RECORDING;
        }
    }
    return changed && (rfmv_loop_shown(m) || (rfmv_smp_shown(m) && m->smp.tab == RFM_MOVE_SMP_TAB_LOOPS));
}

/* ================================================================ the project's part === */
static void lanes_default(rfm_move_t *m)
{
    int l;
    for (l = 0; l < LANES; l++) {
        m->loop.lane[l][RFM_MOVE_LOOP_L_SOURCE] = RFM_MOVE_LOOP_SRC_PLAYED;
        m->loop.lane[l][RFM_MOVE_LOOP_L_LENGTH] = 0;
        m->loop.lane[l][RFM_MOVE_LOOP_L_LEVEL] = 100;
    }
}
void rfmv_loop_clear(rfm_move_t *m)
{
    rfm_move_loop_t *t = LP(m);
    int s;
    for (s = 0; s < NSLOT; s++)
        empty_slot(&t->slot[s]);
    lanes_default(m);
    memset(t->mix, 0, sizeof t->mix);
    t->mix[RFM_MOVE_KIT_MIX_LEVEL] = 100, t->mix[RFM_MOVE_KIT_MIX_PAN] = 64;
    t->cur = 0, t->page = PG_LOOPS;
    t->und = 0, t->und_rec = 0;
}
static void w16(uint8_t *p, int v) { p[0] = (uint8_t)(v & 0xFF), p[1] = (uint8_t)((v >> 8) & 0xFF); }
static int r16s(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | p[1] << 8); }
void rfmv_loop_write(const rfm_move_t *m, uint8_t *p)
{
    const rfm_move_loop_t *t = &m->loop;
    int s, l, j;
    memset(p, 0, RFM_MOVE_LOOP_BYTES);
    p[0] = 1, p[1] = t->cur, p[2] = t->page;
    for (j = 0; j < RFM_MOVE_KIT_MIX_N; j++)
        p[3 + j] = t->mix[j];
    p += 16;
    for (l = 0; l < LANES; l++, p += 4)
        p[0] = t->lane[l][0], p[1] = t->lane[l][1], p[2] = t->lane[l][2];
    for (s = 0; s < NSLOT; s++, p += RFM_MOVE_LOOP_SLOT_BYTES) {
        const rfm_move_loop_slot_t *d = &t->slot[s];
        if (!has_loop(d))
            continue;                                   /* (an empty slot: zeros) */
        memcpy(p, d->path, RFM_MOVE_LOOP_PATH);
        p[RFM_MOVE_LOOP_PATH - 1] = 0;
        p[RFM_MOVE_LOOP_PATH] = d->bars;
        w16(p + RFM_MOVE_LOOP_PATH + 1, d->bpm10);
        for (j = 0; j < RFM_MOVE_LOOP_P_N; j++)
            w16(p + RFM_MOVE_LOOP_PATH + 3 + 2 * j, d->v[j]);
    }
}
static int str_ok(const uint8_t *p, int cap)            /* printable, NUL padded */
{
    int i, end = 0;
    for (i = 0; i < cap; i++) {
        if (!p[i])
            end = 1;
        else if (end || p[i] < 32 || p[i] > 126)
            return 0;
    }
    return end;
}
int rfmv_loop_check(const uint8_t *p)
{
    int s, l, j;
    if (p[0] != 1 || p[1] >= NSLOT || p[2] >= PG_N)
        return -1;
    for (j = 0; j < RFM_MOVE_KIT_MIX_N; j++)
        if (p[3 + j] > 127)
            return -1;
    p += 16;
    for (l = 0; l < LANES; l++, p += 4)
        if (p[0] >= RFM_MOVE_LOOP_SRC_N || p[1] >= RFM_MOVE_LOOP_LENGTHS || p[2] > 127)
            return -1;
    for (s = 0; s < NSLOT; s++, p += RFM_MOVE_LOOP_SLOT_BYTES) {
        rfm_move_loop_slot_t d;
        if (!str_ok(p, RFM_MOVE_LOOP_PATH))
            return -1;
        if (!p[0])
            continue;
        if (!p[RFM_MOVE_LOOP_PATH] || p[RFM_MOVE_LOOP_PATH] > RFM_MOVE_LOOP_MAX_BARS)
            return -1;
        d.bars = p[RFM_MOVE_LOOP_PATH];
        for (j = 0; j < RFM_MOVE_LOOP_P_N; j++) {
            const int v = r16s(p + RFM_MOVE_LOOP_PATH + 3 + 2 * j);
            if (v < p_min(j) || v > p_max(&d, j))
                return -1;
        }
    }
    return 0;
}
void rfmv_loop_read(rfm_move_t *m, const uint8_t *p)
{
    rfm_move_loop_t *t = LP(m);
    int s, l, j;
    rfmv_loop_clear(m);
    t->cur = p[1], t->page = p[2];
    for (j = 0; j < RFM_MOVE_KIT_MIX_N; j++)
        t->mix[j] = p[3 + j];
    p += 16;
    for (l = 0; l < LANES; l++, p += 4)
        t->lane[l][0] = p[0], t->lane[l][1] = p[1], t->lane[l][2] = p[2];
    for (s = 0; s < NSLOT; s++, p += RFM_MOVE_LOOP_SLOT_BYTES) {
        rfm_move_loop_slot_t *d = &t->slot[s];
        if (!p[0])
            continue;
        memcpy(d->path, p, RFM_MOVE_LOOP_PATH);
        d->path[RFM_MOVE_LOOP_PATH - 1] = 0;
        d->bars = p[RFM_MOVE_LOOP_PATH];
        d->bpm10 = (uint16_t)r16s(p + RFM_MOVE_LOOP_PATH + 1);
        for (j = 0; j < RFM_MOVE_LOOP_P_N; j++)
            d->v[j] = (int16_t)r16s(p + RFM_MOVE_LOOP_PATH + 3 + 2 * j);
        d->st = RFM_MOVE_LOOP_LOADING;
    }
}
static void live_reset(rfm_move_t *m)                   /* what was asked or held goes */
{
    rfm_move_loop_t *t = LP(m);
    int l;
    for (l = 0; l < LANES; l++)
        t->ask[l] = -1;
    memset(t->rs, 0, sizeof t->rs);
    t->st_ok = 0, t->copy_src = -1, t->tap_slot = -1, t->kpop = -1, t->lvl = 0;
    t->und = 0, t->und_rec = 0;
}
void rfmv_loop_loaded_project(rfm_move_t *m)
{
    if (!rfmv_loop_cap(m))
        return;
    live_reset(m);
    rfm_move_loop_sync(m);
}
void rfmv_loop_init(rfm_move_t *m)
{
    rfmv_loop_clear(m);
    live_reset(m);
}
void rfmv_loop_caps(rfm_move_t *m, int was)
{
    if (rfmv_loop_cap(m) && !was)
        rfm_move_loop_sync(m);
    else if (!rfmv_loop_cap(m) && was) {
        m->loop.on = 0;
        if (m->s.t4_kit == 2)
            m->s.t4_kit = 0;
    }
}

/* ================================================================ the view === */
void rfmv_loop_open(rfm_move_t *m)
{
    rfm_move_loop_t *t = LP(m);
    rfm_move_seq_t *q = &m->seq;
    if (!rfmv_loop_cap(m))
        return;
    if (q->projects)
        rfmv_view_close(m);
    rfmv_seq_menu_close(m);
    if (m->kit.on) {                                    /* (from the Kit view: what it was opened over is ours now) */
        if (!t->on && m->kit.prev_ok)
            t->prev_mode = m->kit.prev_mode, t->prev_track = m->kit.prev_track, t->prev_view = m->kit.prev_view,
            t->prev_ok = 1;
        m->kit.prev_ok = 0;
        rfmv_kit_close(m, 0);
    }
    if (!t->on) {
        if (!t->prev_ok)
            t->prev_mode = m->s.mode, t->prev_track = q->sel_track, t->prev_view = q->view, t->prev_ok = 1;
        t->turned = 0, t->kpop = -1;
    }
    t->on = 1;
    if (m->s.mode != RFM_MOVE_MODE_SEQ)
        rfmv_mode_set(m, RFM_MOVE_MODE_SEQ);
    q->view = 0;
    if (m->s.t4_kit != 2)
        m->s.t4_kit = 2, rfmv_changed(m);
    rfmv_pop(m, POP_T4, 3);
    touched(m);
}
void rfmv_loop_close(rfm_move_t *m, int restore)
{
    rfm_move_loop_t *t = LP(m);
    if (!t->on)
        return;
    t->on = 0, t->kpop = -1, t->copy_src = -1, t->down = 0, t->st_down = 0, t->lvl = 0;
    if (restore && t->prev_ok) {
        if (t->prev_mode != RFM_MOVE_MODE_SEQ)
            rfmv_mode_set(m, t->prev_mode);
        else {
            rfmv_seq_select(m, t->prev_track);
            m->seq.view = t->prev_view;
        }
    }
    t->prev_ok = 0;
}

/* ================================================================ the gestures === */
static void choose(rfm_move_t *m, int s, int page_follows)
{
    rfm_move_loop_t *t = LP(m);
    t->cur = (uint8_t)(s & (NSLOT - 1));
    if (page_follows && !has_loop(CSL(m, s)) && (t->page == PG_LOOP || t->page == PG_LOOPMIX))
        t->page = PG_RECORD;                            /* (an empty slot chosen opens on Record) */
    rfmv_seq_dirty(m);
    touched(m);
}
static void undo_keep(rfm_move_t *m, const int *slots, int n, const char *what)
{
    rfm_move_loop_t *t = LP(m);
    int i;
    t->und = 1, t->und_n = (uint8_t)n;
    for (i = 0; i < n && i < LANES; i++)
        t->und_s[i] = (uint8_t)slots[i], t->und_slot[i] = t->slot[slots[i]];
    snprintf(t->und_what, sizeof t->und_what, "%s", what);
}
static void launch(rfm_move_t *m, int s, int now_)
{
    rfm_move_loop_t *t = LP(m);
    const int l = lane_of(s);
    act(m, RFM_MOVE_ACT_LOOP_LAUNCH, s, 1, 0, now_ ? RFM_MOVE_LQ_INSTANT : lq(m), 0, 0, NULL);
    t->ask[l] = (int8_t)scene_of(s), t->ask_ms[l] = now(m);
}
static void stop_lane(rfm_move_t *m, int l, int now_)
{
    rfm_move_loop_t *t = LP(m);
    act(m, RFM_MOVE_ACT_LOOP_LAUNCH, l * SCENES, 0, 0, now_ ? RFM_MOVE_LQ_INSTANT : lq(m), 0, 0, NULL);
    t->ask[l] = STOP, t->ask_ms[l] = now(m);
}
static void rec_cancel(rfm_move_t *m, int s)
{
    rfm_move_loop_t *t = LP(m);
    act(m, RFM_MOVE_ACT_LOOP_REC, s, 0, 0, 0, RFM_MOVE_LOOP_R_CANCEL, 0, NULL);
    t->rs[s] = 0;
    if (t->und_rec == s + 1)
        t->und_rec = 0;
}
static void rec_end(rfm_move_t *m, int s)
{
    act(m, RFM_MOVE_ACT_LOOP_REC, s, 0, 0, 0, RFM_MOVE_LOOP_R_END, 0, NULL);
    LP(m)->rs[s] = RFM_MOVE_LOOP_RS_ENDING;
}
/* a recording into s from the next bar (stopped: armed); one running elsewhere ends at that bar, one armed elsewhere
 * goes */
static void rec_start(rfm_move_t *m, int s)
{
    rfm_move_loop_t *t = LP(m);
    const int l = lane_of(s), o = rec_any(m);
    if (o >= 0 && o != s) {
        if (rec_state(m, o) == RFM_MOVE_LOOP_RS_ARMED)
            rec_cancel(m, o);
        else if (rec_state(m, o) == RFM_MOVE_LOOP_RS_RECORDING)
            rec_end(m, o);
    }
    act(m, RFM_MOVE_ACT_LOOP_REC, s, 1, t->lane[l][RFM_MOVE_LOOP_L_SOURCE],
        LEN_BARS[t->lane[l][RFM_MOVE_LOOP_L_LENGTH] % RFM_MOVE_LOOP_LENGTHS], RFM_MOVE_LOOP_R_START, 0, NULL);
    t->rs[s] = RFM_MOVE_LOOP_RS_ARMED;
    t->und_rec = (uint8_t)(s + 1), t->und_rec_was = t->slot[s];
    t->ask[l] = -1;
    choose(m, s, 0);
}
/* a scene: each lane plays its loop in the column, a lane with none there stops; stopped: the transport starts (as
 * session view's scene pads) */
static void scene_launch(rfm_move_t *m, int sc_, int now_)
{
    int l;
    for (l = 0; l < LANES; l++) {
        const int s = l * SCENES + sc_;
        if (rec_state(m, s))
            continue;                                   /* (a recording there goes on) */
        if (has_loop(CSL(m, s)))
            launch(m, s, now_);
        else if (lane_play(m, l) >= 0 || lane_next(m, l) >= 0)
            stop_lane(m, l, now_);
    }
    if (!running(m))
        rfmv_seq_start(m);
    touched(m);
}
static void stop_all(rfm_move_t *m, int now_)
{
    int l;
    for (l = 0; l < LANES; l++)
        if (lane_play(m, l) >= 0 || (lane_next(m, l) >= 0 && lane_next(m, l) != STOP))
            stop_lane(m, l, now_);
    touched(m);
}
static int dbl(const rfm_move_t *m, int slot, int kind)
{
    const rfm_move_loop_t *t = &m->loop;
    return t->tap_slot == slot && t->tap_kind == kind && now(m) - t->tap_ms < RFM_MOVE_DOUBLE_MS;
}
static void tap_mark(rfm_move_t *m, int slot, int kind, int was_dbl)
{
    rfm_move_loop_t *t = LP(m);
    t->tap_slot = (int8_t)(was_dbl ? -1 : slot), t->tap_kind = (uint8_t)kind, t->tap_ms = now(m);
}
/* a pad's tap on slot s (the gestures card lp_rec_gesture a, lp_launch a) */
static void slot_tap(rfm_move_t *m, int s)
{
    const int l = lane_of(s), rs = rec_state(m, s);
    if (rs == RFM_MOVE_LOOP_RS_ARMED) {                 /* (armed: tapped again, it goes) */
        rec_cancel(m, s);
        msg(m, "Cancelled", NULL);
        return;
    }
    if (rs == RFM_MOVE_LOOP_RS_RECORDING) {             /* (the red pad: it ends at the next bar) */
        rec_end(m, s);
        touched(m);
        return;
    }
    if (rs == RFM_MOVE_LOOP_RS_ENDING)
        return;
    if (!has_loop(CSL(m, s))) {
        rec_start(m, s);
        return;
    }
    choose(m, s, 0);
    if (dbl(m, s, 1)) {                                 /* (a double click: the same thing now) */
        launch(m, s, 1);
        tap_mark(m, s, 1, 1);
    } else if (dbl(m, s, 2)) {
        stop_lane(m, l, 1);
        tap_mark(m, s, 2, 1);
    } else if (lane_play(m, l) == scene_of(s) && lane_next(m, l) < 0) {
        stop_lane(m, l, 0);
        tap_mark(m, s, 2, 0);
    } else {
        launch(m, s, 0);
        tap_mark(m, s, 1, 0);
    }
}
static void scene_tap(rfm_move_t *m, int sc_)
{
    const int key = 64 + sc_, d = dbl(m, key, 3);
    scene_launch(m, sc_, d);
    tap_mark(m, key, 3, d);
}
static void slot_delete(rfm_move_t *m, int s)
{
    rfm_move_loop_t *t = LP(m);
    char nm[8];
    if (rec_state(m, s)) {
        rec_cancel(m, s);
        msg(m, "Cancelled", NULL);
        return;
    }
    if (!has_loop(CSL(m, s)))
        return;
    slot_name(s, nm, sizeof nm);
    undo_keep(m, &s, 1, nm);
    if (lane_play(m, lane_of(s)) == scene_of(s) || lane_next(m, lane_of(s)) == scene_of(s))
        t->ask[lane_of(s)] = -1;
    empty_slot(SL(m, s));
    send_slot(m, s);
    rfmv_seq_dirty(m);
    msg(m, "Deleted", nm);
}
static void scene_delete(rfm_move_t *m, int sc_)
{
    int l, sl[LANES], n = 0;
    char nm[12];
    for (l = 0; l < LANES; l++)
        if (has_loop(CSL(m, l * SCENES + sc_)) && !rec_state(m, l * SCENES + sc_))
            sl[n++] = l * SCENES + sc_;
    if (!n)
        return;
    snprintf(nm, sizeof nm, "Scene %d", sc_ + 1);
    undo_keep(m, sl, n, nm);
    for (l = 0; l < n; l++) {
        empty_slot(SL(m, sl[l]));
        send_slot(m, sl[l]);
    }
    rfmv_seq_dirty(m);
    msg(m, "Deleted", nm);
}
static void slot_copy(rfm_move_t *m, int from, int to)
{
    rfm_move_loop_t *t = LP(m);
    char a[8], b[8], w[16];
    int p;
    if (from == to || rec_state(m, to))
        return;
    slot_name(from, a, sizeof a), slot_name(to, b, sizeof b);
    undo_keep(m, &to, 1, b);
    t->slot[to] = t->slot[from];
    act(m, RFM_MOVE_ACT_LOOP_COPY, to, 0, 0, 0, 0, (uint32_t)from, NULL);
    for (p = 0; p < RFM_MOVE_LOOP_P_N; p++)
        send_set(m, to, p);
    choose(m, to, 0);
    rfmv_seq_dirty(m);
    snprintf(w, sizeof w, "%s>%s", a, b);
    msg(m, "Copied", w);
}
static void undo(rfm_move_t *m)
{
    rfm_move_loop_t *t = LP(m);
    int i;
    if (!t->und)
        return;
    for (i = 0; i < t->und_n; i++) {
        const int s = t->und_s[i];
        if (rec_state(m, s))
            rec_cancel(m, s);
        t->slot[s] = t->und_slot[i];
        if (has_loop(&t->slot[s]))
            t->slot[s].st = RFM_MOVE_LOOP_LOADING;
        send_slot(m, s);
    }
    t->und = 0;
    rfmv_seq_dirty(m);
    msg(m, "Undo", t->und_what);
}
static void mute_flip(rfm_move_t *m, int s)
{
    rfm_move_loop_slot_t *d = SL(m, s);
    char nm[8];
    if (!has_loop(d))
        return;
    d->v[RFM_MOVE_LOOP_P_MUTE] = (int16_t)!d->v[RFM_MOVE_LOOP_P_MUTE];
    send_set(m, s, RFM_MOVE_LOOP_P_MUTE);
    rfmv_seq_dirty(m);
    slot_name(s, nm, sizeof nm);
    choose(m, s, 0);
    msg(m, d->v[RFM_MOVE_LOOP_P_MUTE] ? "Muted" : "Unmuted", nm);
}
static int held_slot(const rfm_move_t *m)               /* a slot's pad held (the latest pressed), -1 */
{
    const rfm_move_loop_t *t = &m->loop;
    int p, best = -1;
    uint32_t at = 0;
    for (p = 0; p < 32; p++)
        if ((t->down >> p & 1) && (best < 0 || t->pad_ms[p] - at < 0x80000000u))
            best = p, at = t->pad_ms[p];
    return best < 0 ? -1 : slot_of_pad(best);
}
static void pad_down(rfm_move_t *m, int p)
{
    rfm_move_loop_t *t = LP(m);
    rfm_move_seq_t *q = &m->seq;
    const int s = slot_of_pad(p);
    const uint32_t at = now(m);
    t->down |= 1u << p;
    t->pad_ms[p] = at;
    t->msg_ms = 0;
    if (q->mods & SEQ_M_COPY) {                         /* Copy held: the source, then each slot a target */
        q->mods_used |= SEQ_M_COPY;
        if (t->copy_src < 0) {
            if (has_loop(CSL(m, s)))
                t->copy_src = (int8_t)s, choose(m, s, 0);
        } else
            slot_copy(m, t->copy_src, s);
        touched(m);
        return;
    }
    if (q->mods & SEQ_M_DELETE) {
        q->mods_used |= SEQ_M_DELETE;
        slot_delete(m, s);
        return;
    }
    if (t->mdown) {                                     /* Mute held + a loop */
        t->mdown = 2;
        mute_flip(m, s);
        return;
    }
    if (t->ldown) {                                     /* Loop held + a pad: chosen, silent (the owner's Shift + a
                                                         * pad launches the column now) */
        t->ldown = 2;
        choose(m, s, 1);
        return;
    }
    if (q->mods & SEQ_M_SHIFT) {                        /* (lp_scene b) Shift + any pad of the column: its scene */
        q->mods_used |= SEQ_M_SHIFT;
        scene_tap(m, scene_of(s));
        return;
    }
    if (lane_of(s) > 0) {                               /* (lp_scene c) a slide down the column: the pad above pressed
                                                         * just now (held, or let go a moment ago) */
        const int pa = p + 8;
        const int above = (t->down >> pa & 1) || at - t->up_ms[pa] < 80u;
        if (above && at - t->pad_ms[pa] < RFM_MOVE_LOOP_SLIDE_MS && t->pad_ms[pa]) {
            const int sa = slot_of_pad(pa);
            if (rec_state(m, sa) == RFM_MOVE_LOOP_RS_ARMED && t->und_rec == sa + 1)
                rec_cancel(m, sa);                      /* (the first pad's tap armed it: the slide's scene instead) */
            if (!(t->tap_kind == 3 && t->tap_slot == 64 + scene_of(s) && at - t->tap_ms < RFM_MOVE_LOOP_SLIDE_MS))
                scene_launch(m, scene_of(s), 0), tap_mark(m, 64 + scene_of(s), 3, 0);
            choose(m, s, 0);
            return;
        }
    }
    slot_tap(m, s);
}
static void pad_up(rfm_move_t *m, int p)
{
    rfm_move_loop_t *t = LP(m);
    t->down &= ~(1u << p);
    t->up_ms[p] = now(m);
}
static void step_down(rfm_move_t *m, int i)
{
    rfm_move_seq_t *q = &m->seq;
    LP(m)->st_down |= (uint16_t)(1u << i);
    if (i == 15) {                                      /* step 16: stop all (at the bar; a double click: now) */
        const int d = dbl(m, 80, 3);
        if (q->mods & SEQ_M_DELETE)
            return;
        stop_all(m, d);
        tap_mark(m, 80, 3, d);
        return;
    }
    if (i % 2)
        return;                                         /* (the even steps: dark, dead) */
    if (q->mods & SEQ_M_DELETE) {                       /* Delete + a scene's step: the scene cleared */
        q->mods_used |= SEQ_M_DELETE;
        scene_delete(m, i / 2);
        return;
    }
    scene_tap(m, i / 2);
}

/* ================================================================ the knobs === */
static int knob_job(const rfm_move_t *m, int k)
{
    const rfm_move_loop_t *t = &m->loop;
    const int j = PAGE_K[t->page % PG_N][k & 7];
    if (j >= 0 && j < RFM_MOVE_LOOP_P_N && !has_loop(CSL(m, t->cur)))
        return -1;                                      /* (an empty slot: its settings dark) */
    return j;
}
static int job_val(const rfm_move_t *m, int j)
{
    const rfm_move_loop_t *t = &m->loop;
    const int l = lane_of(t->cur);
    if (j >= 0 && j < RFM_MOVE_LOOP_P_N)
        return CSL(m, t->cur)->v[j];
    if (j == KL_SOURCE)
        return t->lane[l][RFM_MOVE_LOOP_L_SOURCE];
    if (j == KL_LENGTH)
        return t->lane[l][RFM_MOVE_LOOP_L_LENGTH];
    if (j >= KL_LVL0 && j < KL_LVL0 + LANES)
        return t->lane[j - KL_LVL0][RFM_MOVE_LOOP_L_LEVEL];
    if (j >= KS_LEVEL && j <= KS_SEND3)
        return t->mix[j - KS_LEVEL];
    return 0;
}
static int div_of(int j)
{
    switch (j) {
    case RFM_MOVE_LOOP_P_REVERSE:
    case RFM_MOVE_LOOP_P_WARP:
    case RFM_MOVE_LOOP_P_MUTE:
    case KL_SOURCE:
    case KL_LENGTH: return 5;
    case RFM_MOVE_LOOP_P_START:
    case RFM_MOVE_LOOP_P_LENGTH:
    case RFM_MOVE_LOOP_P_PITCH: return 2;
    default: return 1;
    }
}
/* Length's values: a beat, half a bar, then each whole bar up to the bars recorded */
static int len_step(const rfm_move_loop_slot_t *d, int v, int n)
{
    int vals[RFM_MOVE_LOOP_MAX_BARS + 2], k = 0, i, at = 0;
    vals[k++] = 4, vals[k++] = 8;
    for (i = 1; i <= (d->bars ? d->bars : 1); i++)
        vals[k++] = i * RFM_MOVE_LOOP_STEPS;
    for (i = 0; i < k; i++)
        if (vals[i] <= v)
            at = i;
    return vals[clampi(at + n, 0, k - 1)];
}
static void level_set(rfm_move_t *m, int j, int v)
{
    rfm_move_loop_t *t = LP(m);
    if (j >= KL_LVL0 && j < KL_LVL0 + LANES) {
        t->lane[j - KL_LVL0][RFM_MOVE_LOOP_L_LEVEL] = (uint8_t)v;
        send_lane(m, j - KL_LVL0, RFM_MOVE_LOOP_L_LEVEL);
    } else if (j >= KS_LEVEL && j <= KS_SEND3) {
        t->mix[j - KS_LEVEL] = (uint8_t)v;
        send_mix(m);
    }
    rfmv_seq_dirty(m);
}
static void job_turn(rfm_move_t *m, int j, int n, int fine)
{
    rfm_move_loop_t *t = LP(m);
    const int s = t->cur, l = lane_of(s);
    rfm_move_loop_slot_t *d = SL(m, s);
    int v;
    if (j >= 0 && j < RFM_MOVE_LOOP_P_N) {
        v = d->v[j];
        if (j == RFM_MOVE_LOOP_P_LEVEL)
            v = rfmv_lvl_step(v, n);
        else if (j == RFM_MOVE_LOOP_P_PITCH)
            v = fine ? v + n : (v >= 0 ? (v + 50) / 100 : (v - 50) / 100) * 100 + 100 * n;
        else if (j == RFM_MOVE_LOOP_P_LENGTH)
            v = len_step(d, v, n);
        else if (j == RFM_MOVE_LOOP_P_PAN)
            v = v + n;
        else
            v = v + n;
        v = clampi(v, p_min(j), p_max(d, j));
        if (v != d->v[j]) {
            d->v[j] = (int16_t)v;
            send_set(m, s, j);
            rfmv_seq_dirty(m);
        }
        return;
    }
    if (j == KL_SOURCE || j == KL_LENGTH) {
        const int p = j == KL_SOURCE ? RFM_MOVE_LOOP_L_SOURCE : RFM_MOVE_LOOP_L_LENGTH;
        const int mx = j == KL_SOURCE ? RFM_MOVE_LOOP_SRC_N - 1 : RFM_MOVE_LOOP_LENGTHS - 1;
        v = clampi(t->lane[l][p] + n, 0, mx);
        if (v != t->lane[l][p]) {
            t->lane[l][p] = (uint8_t)v;
            send_lane(m, l, p);
            rfmv_seq_dirty(m);
        }
        return;
    }
    v = job_val(m, j);
    if ((j >= KL_LVL0 && j < KL_LVL0 + LANES) || j == KS_LEVEL)
        v = rfmv_lvl_step(v, n);
    else
        v = clampi(v + n, 0, 127);
    if (v != job_val(m, j))
        level_set(m, j, v);
}
static void knob_touch(rfm_move_t *m, int k, int on)
{
    rfm_move_loop_t *t = LP(m);
    if (!on) {
        t->kdown = (uint8_t)(t->kdown & ~(1u << k));
        if (t->kpop == k)
            t->kpop_ms = now(m);
        return;
    }
    t->kdown = (uint8_t)(t->kdown | 1u << k);
    if (knob_job(m, k) < 0)
        return;
    t->kpop = (int8_t)k, t->kpop_ms = now(m), t->msg_ms = 0, t->lvl = 0;
    touched(m);
}
static void knob_turn(rfm_move_t *m, int k, int d)
{
    rfm_move_loop_t *t = LP(m);
    const int j = knob_job(m, k), fine = (m->seq.mods & SEQ_M_SHIFT) != 0;
    int n, a;
    if (!d || j < 0)
        return;
    if (fine)
        m->seq.mods_used |= SEQ_M_SHIFT;
    t->kpop = (int8_t)k, t->kpop_ms = now(m), t->msg_ms = 0, t->lvl = 0;
    touched(m);
    if (fine && j == RFM_MOVE_LOOP_P_PITCH)
        n = d;
    else {
        a = t->kacc[k];
        if ((a > 0 && d < 0) || (a < 0 && d > 0))
            a = 0;
        a += d;
        n = a / div_of(j);
        t->kacc[k] = (int8_t)(a - n * div_of(j));
    }
    if (n)
        job_turn(m, j, n, fine);
}
static int kpop_up(const rfm_move_t *m)
{
    const rfm_move_loop_t *t = &m->loop;
    return t->kpop >= 0 && knob_job(m, t->kpop) >= 0 && ((t->kdown >> t->kpop & 1) || now(m) - t->kpop_ms < RFM_MOVE_POPUP_MS);
}
/* the volume knob: 1 a held slot's level, 2 the Loops strip's (Track 4 held) */
static int lvl_up(const rfm_move_t *m)
{
    const rfm_move_loop_t *t = &m->loop;
    return t->lvl && (t->lvl_touch || now(m) - t->lvl_ms < RFM_MOVE_POPUP_MS);
}
static void lvl_show(rfm_move_t *m, int which, int touch)
{
    rfm_move_loop_t *t = LP(m);
    if (t->lvl != which)
        t->lvl_acc = 0;
    t->lvl = (uint8_t)which, t->lvl_ms = now(m);
    if (touch)
        t->lvl_touch = 1;
    t->kpop = -1, t->msg_ms = 0;
    touched(m);
}
int rfmv_loop_volume(rfm_move_t *m, int d)
{
    if (!rfmv_loop_cap(m) || !rfmv_loop_active(m))
        return 0;
    if (d)
        level_set(m, KS_LEVEL, rfmv_lvl_step(m->loop.mix[RFM_MOVE_KIT_MIX_LEVEL], d));
    lvl_show(m, 2, !d);
    return 1;
}
void rfmv_loop_mix_turn(rfm_move_t *m, int par, int d)
{
    const int j = KS_LEVEL + clampi(par, 0, RFM_MOVE_KIT_MIX_N - 1);
    int v = job_val(m, j);
    if (!rfmv_loop_cap(m) || !d)
        return;
    v = j == KS_LEVEL ? rfmv_lvl_step(v, d) : clampi(v + d, 0, 127);
    level_set(m, j, v);
}

/* ================================================================ the jog, Back === */
static void turn(rfm_move_t *m, int d)
{
    rfm_move_loop_t *t = LP(m);
    int pg = t->page % PG_N, n = d;
    t->msg_ms = 0, t->kpop = -1, t->lvl = 0;
    touched(m);
    while (n) {
        const int st = n > 0 ? 1 : -1, nx = pg + st;
        if (nx < 0 || nx >= PG_N)
            break;
        pg = nx, n -= st;
    }
    if (pg != t->page) {
        memset(t->kacc, 0, sizeof t->kacc);
        t->pg_ms = now(m) ? now(m) : 1, t->pg_dir = (int8_t)(pg > t->page ? 1 : -1);
        rfmv_seq_dirty(m);
    }
    t->page = (uint8_t)pg;
    t->turned = 1, t->turn_ms = now(m);
}
int rfmv_loop_back_has(const rfm_move_t *m)
{
    const rfm_move_loop_t *t = &m->loop;
    return rfmv_loop_active(m) && (kpop_up(m) || msg_up(m) || (t->turned && now(m) - t->turn_ms < RFM_MOVE_POPUP_MS));
}
int rfmv_loop_back(rfm_move_t *m)
{
    rfm_move_loop_t *t = LP(m);
    if (!rfmv_loop_shown(m) || !rfmv_loop_back_has(m))
        return 0;
    t->kpop = -1, t->msg_ms = 0, t->turned = 0;
    touched(m);
    return 1;
}

/* ================================================================ the input === */
int rfmv_loop_input(rfm_move_t *m, const mr_event_t *ev)
{
    rfm_move_loop_t *t = LP(m);
    rfm_move_seq_t *q = &m->seq;
    const int i = ev->index;
    if (!rfmv_loop_cap(m))
        return 0;
    if (t->copy_src >= 0 && !(q->mods & SEQ_M_COPY))
        t->copy_src = -1;                               /* (Copy let go: its source too) */
    /* the releases of what the looper took, wherever it is now */
    if (ev->type == MR_EV_PAD && !ev->value && i >= 0 && i < 32 && (t->down >> i & 1)) {
        pad_up(m, i);
        return 1;
    }
    if (ev->type == MR_EV_STEP && !ev->value && i >= 0 && i < 16 && (t->st_down >> i & 1)) {
        t->st_down &= (uint16_t)~(1u << i);
        return 1;
    }
    if (ev->type == MR_EV_TOUCH && !ev->value && i == 8 && t->lvl_touch)
        t->lvl_touch = 0, t->lvl_ms = now(m);
    if (ev->type == MR_EV_TOUCH && !ev->value && i >= 0 && i < 8 && (t->kdown >> i & 1)) {
        knob_touch(m, i, 0);
        return 1;
    }
    if (ev->type == MR_EV_BUTTON && !ev->value) {
        uint8_t *b = i == MR_CC_SAMPLE ? &t->sdown : i == MR_CC_UNDO ? &t->udown : i == MR_CC_LOOP ? &t->ldown
                   : i == MR_CC_MUTE ? &t->mdown
                   : i == MR_CC_UP || i == MR_CC_DOWN || i == MR_CC_LEFT || i == MR_CC_RIGHT ? &t->adown : NULL;
        if (b && *b) {
            *b = 0;
            return 1;
        }
        return 0;
    }
    if (!rfmv_loop_active(m) || m->track_held || rfmv_st_on(m) || q->smenu == RFM_MOVE_SM_GLOBAL)
        return 0;
    if ((ev->type == MR_EV_KNOB || ev->type == MR_EV_TOUCH) && i >= 0 && i < 8 && rfmv_smp_loops_tab(m))
        return 0;                                       /* (the sampler's Loops tab has the knobs) */
    switch (ev->type) {
    case MR_EV_PAD:
        if (i < 0 || i >= 32 || !ev->value || q->fill || q->smenu)
            return 0;
        pad_down(m, i);
        return 1;
    case MR_EV_PAD_PRESSURE:
        return i >= 0 && i < 32 && (t->down >> i & 1);
    case MR_EV_STEP:
        if (i < 0 || i >= 16 || !ev->value || q->smenu || (q->mods & (SEQ_M_SHIFT | SEQ_M_COPY)))
            return 0;                                   /* (Shift: the shortcuts) */
        step_down(m, i);
        return 1;
    case MR_EV_KNOB:
        if (i == 8 && held_slot(m) >= 0 && ev->value && !q->smenu) {   /* a slot held + the volume knob: its level */
            const int s = held_slot(m);
            if (has_loop(CSL(m, s))) {                  /* (0.5 dB a detent, as every level) */
                choose(m, s, 0);
                lvl_show(m, 1, 0);
                job_turn(m, RFM_MOVE_LOOP_P_LEVEL, ev->value, 0);
            }
            return 1;
        }
        if (i < 0 || i > 7 || q->smenu)
            return 0;
        knob_turn(m, i, ev->value);
        return 1;
    case MR_EV_TOUCH:
        if (i == 8 && ev->value && held_slot(m) >= 0 && !q->smenu && has_loop(CSL(m, held_slot(m)))) {
            choose(m, held_slot(m), 0);
            lvl_show(m, 1, 1);
            return 1;
        }
        if (i >= 0 && i < 8 && !q->smenu) {
            knob_touch(m, i, ev->value != 0);
            return 1;
        }
        return i == 9 && rfmv_loop_shown(m);
    case MR_EV_JOG:
        if (!rfmv_loop_shown(m))
            return 0;
        turn(m, ev->value);
        return 1;
    case MR_EV_JOG_CLICK:
        return ev->value && rfmv_loop_shown(m);         /* (stage 2: a loop's To Kit / To Takes) */
    case MR_EV_BUTTON:
        if (i == MR_CC_SAMPLE && !rfmv_smp_shown(m)) {
            const int h = held_slot(m), r = rec_any(m);
            if (h >= 0) {                               /* a slot held + Sample: records into it, full or not */
                t->sdown = 1;
                rec_start(m, h);
                return 1;
            }
            if (r >= 0) {                               /* Sample: a recording ends at the bar (armed: it goes) */
                t->sdown = 1;
                if (rec_state(m, r) == RFM_MOVE_LOOP_RS_ARMED)
                    rec_cancel(m, r), msg(m, "Cancelled", NULL);
                else if (rec_state(m, r) == RFM_MOVE_LOOP_RS_RECORDING)
                    rec_end(m, r);
                touched(m);
                return 1;
            }
            return 0;
        }
        if (i == MR_CC_UNDO && t->und && !(q->mods & SEQ_M_SHIFT)) {
            t->udown = 1;
            undo(m);
            return 1;
        }
        if (i == MR_CC_LOOP && !(q->mods & SEQ_M_SHIFT)) {   /* Loop held: a pad chosen without playing */
            t->ldown = 1;
            return 1;
        }
        if (i == MR_CC_MUTE && !(q->mods & SEQ_M_SHIFT)) {   /* Mute held + a loop: muted */
            t->mdown = 1;
            return 1;
        }
        if ((i == MR_CC_LEFT || i == MR_CC_RIGHT || i == MR_CC_UP || i == MR_CC_DOWN) && !(q->mods & SEQ_M_SHIFT) &&
            !q->step_held) {                            /* the arrows: the chosen slot, silent */
            const int l = lane_of(t->cur), s = scene_of(t->cur);
            t->adown = 1;
            if (i == MR_CC_LEFT || i == MR_CC_RIGHT)
                choose(m, l * SCENES + clampi(s + (i == MR_CC_RIGHT ? 1 : -1), 0, SCENES - 1), 1);
            else
                choose(m, clampi(l + (i == MR_CC_DOWN ? 1 : -1), 0, LANES - 1) * SCENES + s, 1);
            t->msg_ms = 0, t->kpop = -1;
            return 1;
        }
        return 0;
    default: return 0;
    }
}
void rfmv_loop_poll(rfm_move_t *m)
{
    rfm_move_loop_t *t = LP(m);
    if (t->copy_src >= 0 && !(m->seq.mods & SEQ_M_COPY))
        t->copy_src = -1;
}

/* ================================================================ the answers === */
static void peaks_put(rfm_move_loop_slot_t *d, int n, const uint8_t *pk)
{
    d->npk = (uint8_t)(pk && n > 0 ? (n > RFM_MOVE_LOOP_PEAKS ? RFM_MOVE_LOOP_PEAKS : n) : 0);
    if (d->npk)
        memcpy(d->pk, pk, d->npk);
}
int rfm_move_loop_recorded(rfm_move_t *m, int slot, int status, const char *path, uint32_t frames, uint32_t rate, int bars,
                           int bpm10, int n, const uint8_t *peaks)
{
    rfm_move_loop_t *t = LP(m);
    char nm[8];
    if (!rfmv_loop_cap(m) || slot < 0 || slot >= NSLOT)
        return 0;
    t->rs[slot] = 0;
    slot_name(slot, nm, sizeof nm);
    if (status == RFM_MOVE_LOOP_OK && path && path[0] && bars > 0) {
        rfm_move_loop_slot_t *d = &t->slot[slot];
        int p;
        if (t->und_rec == slot + 1) {
            t->und = 1, t->und_n = 1, t->und_s[0] = (uint8_t)slot, t->und_slot[0] = t->und_rec_was;
            snprintf(t->und_what, sizeof t->und_what, "Loop %s", nm);
        }
        memset(d, 0, sizeof *d);
        snprintf(d->path, sizeof d->path, "%s", path);
        d->st = RFM_MOVE_LOOP_OK, d->bars = (uint8_t)clampi(bars, 1, RFM_MOVE_LOOP_MAX_BARS);
        d->bpm10 = (uint16_t)clampi(bpm10, 0, 65535), d->frames = frames, d->rate = rate;
        defaults(d->v, d->bars);
        peaks_put(d, n, peaks);
        for (p = 0; p < RFM_MOVE_LOOP_P_N; p++)
            send_set(m, slot, p);
        if (t->ask[lane_of(slot)] >= 0)
            t->ask[lane_of(slot)] = -1;
        rfmv_seq_dirty(m);
    } else
        msg(m, status == RFM_MOVE_LOOP_TOO_LONG ? "Too Long" : status == RFM_MOVE_LOOP_NOTHING ? "Nothing" : "Not Saved", nm);
    if (t->und_rec == slot + 1)
        t->und_rec = 0;
    touched(m);
    return rfmv_loop_shown(m) || rfmv_smp_shown(m);
}
int rfm_move_loop_loaded(rfm_move_t *m, int slot, int status, uint32_t frames, uint32_t rate, int bars, int bpm10, int n,
                         const uint8_t *peaks)
{
    rfm_move_loop_slot_t *d;
    if (!rfmv_loop_cap(m) || slot < 0 || slot >= NSLOT)
        return 0;
    d = SL(m, slot);
    if (!has_loop(d))
        return 0;                                       /* (emptied since) */
    d->st = (uint8_t)(status == RFM_MOVE_LOOP_OK ? RFM_MOVE_LOOP_OK : status == RFM_MOVE_LOOP_MISSING ? RFM_MOVE_LOOP_MISSING
                                                                                                    : RFM_MOVE_LOOP_FAILED);
    if (status == RFM_MOVE_LOOP_OK) {
        d->frames = frames, d->rate = rate;
        if (bars > 0)
            d->bars = (uint8_t)clampi(bars, 1, RFM_MOVE_LOOP_MAX_BARS);
        if (bpm10 > 0)
            d->bpm10 = (uint16_t)bpm10;
        peaks_put(d, n, peaks);
    }
    return rfmv_loop_shown(m) && m->loop.cur == slot;
}
const char *rfm_move_loop_path(const rfm_move_t *m, int slot) { return slot >= 0 && slot < NSLOT ? m->loop.slot[slot].path : NULL; }
int rfm_move_loop_value(const rfm_move_t *m, int slot, int p)
{
    return slot >= 0 && slot < NSLOT && p >= 0 && p < RFM_MOVE_LOOP_P_N ? m->loop.slot[slot].v[p] : -1;
}
int rfm_move_loop_lane(const rfm_move_t *m, int lane, int p)
{
    return lane >= 0 && lane < LANES && p >= 0 && p < RFM_MOVE_LOOP_L_N ? m->loop.lane[lane][p] : -1;
}
int rfm_move_loop_mix(const rfm_move_t *m, int which) { return which >= 0 && which < RFM_MOVE_KIT_MIX_N ? m->loop.mix[which] : -1; }
int rfm_move_loop_refs(const rfm_move_t *m, const char **out, int cap)
{
    const rfm_move_loop_t *t = &m->loop;
    int n = 0, s, i, j;
    const char *c[NSLOT + 2 * LANES];
    int nc = 0;
    for (s = 0; s < NSLOT; s++)
        c[nc++] = t->slot[s].path;
    if (t->und)
        for (i = 0; i < t->und_n; i++)
            c[nc++] = t->und_slot[i].path;
    if (t->und_rec)
        c[nc++] = t->und_rec_was.path;
    for (i = 0; i < nc && n < cap; i++) {
        int seen = 0;
        if (!c[i][0])
            continue;
        for (j = 0; j < n && !seen; j++)
            seen = !strcmp(out[j], c[i]);
        if (!seen)
            out[n++] = c[i];
    }
    return n;
}

/* ================================================================ the lights === */
static int blink(const rfm_move_t *m, uint32_t per) { return now(m) % per < per / 2; }
static rgb_t slot_rgb(const rfm_move_t *m, int s)
{
    const rfm_move_loop_slot_t *d = CSL(m, s);
    const int l = lane_of(s), rs = rec_state(m, s);
    if (rs == RFM_MOVE_LOOP_RS_ARMED)
        return blink(m, 500) ? RFMV_RED : sc(RFMV_RED, 40);
    if (rs)
        return RFMV_RED;
    if (!has_loop(d))
        return RFMV_OFF;
    if (lane_play(m, l) == scene_of(s)) {
        const int nx = lane_next(m, l);
        if (nx >= 0 && nx != scene_of(s))
            return blink(m, 400) ? sc(LOOPC, 200) : sc(LOOPC, 50);   /* (about to stop) */
        return d->v[RFM_MOVE_LOOP_P_MUTE] ? sc(LGREEN, 70) : LGREEN;
    }
    if (lane_next(m, l) == scene_of(s))
        return blink(m, 400) ? LGREEN : sc(LGREEN, 40);   /* (lined up) */
    return d->v[RFM_MOVE_LOOP_P_MUTE] ? sc(RFMV_WHITE, 40) : sc(LOOPC, 90);
}
void rfmv_loop_leds(const rfm_move_t *m, rgb_t c[L_WHITE0], uint8_t wv[13])
{
    const rfm_move_loop_t *t = &m->loop;
    const rfm_move_seq_t *q = &m->seq;
    int i, s;
    if (!rfmv_loop_active(m))
        return;
    for (s = 0; s < NSLOT; s++) {
        rgb_t v = slot_rgb(m, s);
        const int busy = rec_state(m, s) || lane_play(m, lane_of(s)) == scene_of(s) || lane_next(m, lane_of(s)) == scene_of(s);
        if (s == t->cur && !busy && t->copy_src < 0)
            v = RFMV_WHITE;                             /* (the chosen slot; playing or waiting: its state shows) */
        if (s == t->copy_src)
            v = blink(m, 400) ? sc(RFMV_WHITE, 160) : sc(RFMV_WHITE, 40);
        c[L_PAD0 + pad_of(s)] = v;
    }
    if (!(q->mods & (SEQ_M_SHIFT | SEQ_M_COPY))) {
        for (i = 0; i < 16; i++) {
            rgb_t v = RFMV_OFF;
            if (i == 15)
                v = sc(RFMV_RED, 140);
            else if (!(i % 2)) {
                int pl = 0, nx = 0, l;
                for (l = 0; l < LANES; l++)
                    pl |= lane_play(m, l) == i / 2, nx |= lane_next(m, l) == i / 2;
                v = nx ? (blink(m, 400) ? LGREEN : sc(LGREEN, 40)) : pl ? LGREEN : sc(RFMV_WHITE, 50);
            }
            if (t->st_down >> i & 1)
                v = RFMV_WHITE;
            c[L_STEP0 + i] = v;
        }
    }
    for (i = 0; i < 8; i++)
        c[L_RING0 + i] = knob_job(m, i) < 0 ? RFMV_OFF : (t->kdown >> i & 1) || t->kpop == i ? RFMV_WHITE : sc(LOOPC, 150);
    wv[W_COPY] = wv[W_COPY] > 60 ? wv[W_COPY] : 60;
    wv[W_DELETE] = wv[W_DELETE] > 60 ? wv[W_DELETE] : 60;
    wv[W_UNDO] = t->und ? 100 : wv[W_UNDO];
    wv[W_LOOP] = t->ldown ? 127 : 60;
    wv[W_MUTE] = t->mdown ? 127 : 60;
    wv[W_LEFT] = wv[W_RIGHT] = wv[W_UP] = wv[W_DOWN] = 60;
}
void rfmv_loop_leds_w(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3])
{
    rgb_t c;
    if (!rfmv_loop_active(m))
        return;
    if (!m->track_held)
        w[L_TRACK0 + 3][0] = LOOPC.r, w[L_TRACK0 + 3][1] = LOOPC.g, w[L_TRACK0 + 3][2] = LOOPC.b;
    if (!(m->seq.mods & (SEQ_M_SHIFT | SEQ_M_COPY)))
        w[L_ICON0 + 15][0] = 127;                       /* (step 16 Stop All: a function, its icon lit) */
    if (m->smp.open)
        return;
    {
        const int r = rec_any(m);
        if (r >= 0 && rec_state(m, r) == RFM_MOVE_LOOP_RS_ARMED)
            c = blink(m, 500) ? RFMV_RED : sc(RFMV_RED, 30);
        else if (r >= 0)
            c = RFMV_RED;
        else if (rfmv_smp_cap(m) && m->smp.st.state != RFM_MOVE_SMP_ST_IDLE)
            return;
        else
            c = sc(RFMV_WHITE, 60);
    }
    w[L_SAMPLE][0] = c.r, w[L_SAMPLE][1] = c.g, w[L_SAMPLE][2] = c.b;
    if (m->loop.kpop >= 0 && knob_job(m, m->loop.kpop) == KL_SOURCE && kpop_up(m)) {   /* (the Source popup: the
                                                         * Track it records lit bright, board H) */
        const int src = m->loop.lane[lane_of(m->loop.cur)][RFM_MOVE_LOOP_L_SOURCE];
        const int tr = src == RFM_MOVE_LOOP_SRC_PLAYED ? m->fw.active : src >= RFM_MOVE_LOOP_SRC_TRACK1 && src <= RFM_MOVE_LOOP_SRC_TRACK3
                                                                    ? src - RFM_MOVE_LOOP_SRC_TRACK1 : -1;
        if (tr >= 0 && tr < 3) {
            int j;
            for (j = 0; j < 3; j++)                     /* (its own colour at full) */
                if (w[L_TRACK0 + tr][j] < 255 && w[L_TRACK0 + tr][j])
                    w[L_TRACK0 + tr][j] = (uint8_t)(w[L_TRACK0 + tr][j] * 4 > 255 ? 255 : w[L_TRACK0 + tr][j] * 4);
        }
    }
}

/* ================================================================ the screens === */
static int pg_off(const rfm_move_t *m)
{
    const rfm_move_loop_t *t = &m->loop;
    const uint32_t el = now(m) - t->pg_ms;
    float x;
    if (!t->pg_ms || el >= PG_SLIDE_MS)
        return 0;
    x = 1.0f - (float)el / PG_SLIDE_MS;
    return (int)(t->pg_dir * PG_CELL * x * x * x + (t->pg_dir > 0 ? 0.5f : -0.5f));
}
static void page_switch(const rfm_move_t *m, mr_canvas_t *cv)
{
    const int pg = m->loop.page % PG_N, off = pg_off(m), y = 7;
    int i;
    for (i = 0; i < PG_N; i++) {
        const int x = 64 - PG_CELL / 2 + (i - pg) * PG_CELL + off;
        if (x <= -PG_CELL || x >= 128)
            continue;
        if (i == pg)
            v2_R(cv, x, y, PG_CELL, PG_CELL, 1);
        ic2(cv, *PG_ICON[i], x + 3, y + 3, i != pg);
    }
    v2_TC(cv, 64, y + PG_CELL + 6, PG_NAME[pg], 2, 1);
}
/* the slot's place: the 8 x 4 grid in small (3 x 2 cells, a px apart), the chosen cell solid, a loop a dot */
static void locator(const rfm_move_t *m, mr_canvas_t *cv, int x0, int y0, int cs)
{
    int s;
    for (s = 0; s < NSLOT; s++) {
        const int x = x0 + scene_of(s) * 4, y = y0 + lane_of(s) * 3;
        if (s == cs)
            v2_R(cv, x, y, 3, 2, 1);
        else if (has_loop(CSL(m, s)))
            v2_px(cv, x + 1, y, 1), v2_px(cv, x + 1, y + 1, 1);
    }
}
static void bars_row(mr_canvas_t *cv, int x0, int w, int y, int h, int n, int k)
{
    int i;
    if (n < 1)
        return;
    if (n > 16)
        n = 16;
    for (i = 0; i < n; i++) {
        const int a = x0 + i * w / n, e = x0 + (i + 1) * w / n - 2;
        if (i < k)
            v2_R(cv, a, y, e - a, h, 1);
        else
            v2_O(cv, a, y, e - a, h, 1);
    }
}
/* a loop's wave about y0 (h each way) over x0..x0+w, a column every 2 px; dim_from: past it dotted (-1 none) */
static void wave(mr_canvas_t *cv, const rfm_move_loop_slot_t *d, int x0, int w, int y0, int h, int dim_from)
{
    int x;
    if (!d->npk) {
        v2_R(cv, x0, y0, w, 1, 1);
        return;
    }
    for (x = 0; x < w; x += 2) {
        const int i = x * d->npk / (w > 0 ? w : 1);
        int a = 1 + d->pk[i < d->npk ? i : d->npk - 1] * (h - 1) / 255;
        if (dim_from >= 0 && x0 + x >= dim_from) {
            int yy;
            for (yy = y0 - a; yy < y0 + a; yy += 2)
                v2_px(cv, x0 + x, yy, 1);
        } else
            v2_R(cv, x0 + x, y0 - a, 1, a * 2, 1);
    }
}
static int bars_of_len(const rfm_move_loop_slot_t *d)
{
    const int st = d->v[RFM_MOVE_LOOP_P_LENGTH];
    return st >= RFM_MOVE_LOOP_STEPS ? (st + RFM_MOVE_LOOP_STEPS - 1) / RFM_MOVE_LOOP_STEPS : 1;
}
/* the Loops page: a lane a row (16 px) */
static void lanes_page(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_loop_t *t = &m->loop;
    int l, i;
    for (l = 0; l < LANES; l++) {
        const int y = l * 16, x0 = 18, w = 110, pl = lane_play(m, l), nx = lane_next(m, l);
        int rs = -1, s;
        char sv[4];
        for (s = l * SCENES; s < (l + 1) * SCENES; s++)
            if (rec_state(m, s) == RFM_MOVE_LOOP_RS_RECORDING || rec_state(m, s) == RFM_MOVE_LOOP_RS_ENDING)
                rs = s;
        if (rs >= 0) {                                  /* recording: the dot, the wave growing over the bar */
            const int npk = t->st_ok ? t->st.npk : 0, fill = npk * w / RFM_MOVE_LOOP_RPEAKS;
            ic(cv, I_DOT, 2, y + 2, 1);
            for (i = 0; i < npk; i++) {
                const int x = x0 + i * w / RFM_MOVE_LOOP_RPEAKS, a = 1 + t->st.pk[i] * 5 / 255;
                v2_R(cv, x, y + 7 - a, 1, a * 2, 1);
            }
            v2_dotH(cv, x0 + fill, y + 7, w - fill);
            continue;
        }
        if (nx >= 0 && nx != pl) {                      /* waiting: the scene it goes to (- a stop), dashed, the beats */
            snprintf(sv, sizeof sv, nx == STOP ? "-" : "%d", nx + 1);
            v2_T(cv, 2, y + 1, sv, 2, 1);
            v2_dashO(cv, x0, y + 2, w - 2, 11);
            for (i = 0; i < 4; i++)
                v2_R(cv, x0 + 8 + i * 8, y + 6, 4, 3, running(m) && i <= (t->st_ok ? t->st.beat : 0) ? 1 : 0);
            continue;
        }
        if (pl < 0) {                                   /* stopped: a line */
            v2_dotH(cv, x0, y + 7, w);
            continue;
        }
        {                                               /* playing: its scene, its bars, filled as it plays */
            const rfm_move_loop_slot_t *d = CSL(m, l * SCENES + pl);
            const int n = clampi(bars_of_len(d), 1, 16), fill = t->st_ok ? (int)((long)w * t->st.lane[l].pos / 65536) : 0;
            snprintf(sv, sizeof sv, "%d", pl + 1);
            v2_T(cv, 2, y + 1, sv, 2, 1);
            for (i = 0; i < n; i++)
                v2_R(cv, x0 + i * w / n, y + 2, 1, 11, 1);
            v2_R(cv, x0 + w - 1, y + 2, 1, 11, 1);
            v2_R(cv, x0, y + 5, fill, 5, 1);
            v2_R(cv, x0, y + 7, w, 1, 1);
        }
    }
}
/* where the chosen loop plays now (0..1 of its recording; -1 not playing) */
static double play_frac(const rfm_move_t *m, int s)
{
    const rfm_move_loop_slot_t *d = CSL(m, s);
    const int l = lane_of(s), tot = loop_steps(d);
    double f;
    if (!m->loop.st_ok || lane_play(m, l) != scene_of(s) || !running(m))
        return -1;
    f = (double)d->v[RFM_MOVE_LOOP_P_LENGTH] * m->loop.st.lane[l].pos / 65536.0;
    if (d->v[RFM_MOVE_LOOP_P_REVERSE])
        f = d->v[RFM_MOVE_LOOP_P_LENGTH] - f;
    f += d->v[RFM_MOVE_LOOP_P_START];
    while (f >= tot)
        f -= tot;
    return f / tot;
}
static void loop_page(const rfm_move_t *m, mr_canvas_t *cv)
{
    const int s = m->loop.cur;
    const rfm_move_loop_slot_t *d = CSL(m, s);
    const double f = play_frac(m, s);
    const int ph = f >= 0 ? (int)(f * 128) : -1, n = clampi(d->bars, 1, 16);
    wave(cv, d, 0, 128, 31, 13, ph);
    if (ph >= 0) {
        v2_R(cv, ph, 16, 1, 31, 1);
        bars_row(cv, 0, 130, 53, 9, n, (int)(f * n));
        {
            const int k = (int)(f * n), a = k * 130 / n + 2, e = (k + 1) * 130 / n - 4;
            v2_R(cv, a, 55, (int)((e - a) * (f * n - k)), 5, 1);
        }
    } else
        bars_row(cv, 0, 130, 53, 9, n, 0);
    if (d->st == RFM_MOVE_LOOP_MISSING || d->st == RFM_MOVE_LOOP_FAILED)
        v2_TC(cv, 48, 2, d->st == RFM_MOVE_LOOP_MISSING ? "Missing" : "Failed", 1, 1);
    locator(m, cv, 96, 0, s);
}
/* a lane's Source as a row shows it: Played as the Track it means now ("Track 2"); long 1: the popup's ("Played 2") */
static void src_text_l(const rfm_move_t *m, int src, char *o, int cap, int lng)
{
    if (src == RFM_MOVE_LOOP_SRC_PLAYED)
        snprintf(o, (size_t)cap, lng ? "Played %d" : "Track %d", m->fw.active % 3 + 1);
    else
        snprintf(o, (size_t)cap, "%s", SRC_NAME[clampi(src, 0, RFM_MOVE_LOOP_SRC_N - 1)]);
}
static void src_text(const rfm_move_t *m, int src, char *o, int cap) { src_text_l(m, src, o, cap, 0); }
static void len_text(int li, char *o, int cap)
{
    const int b = LEN_BARS[li % RFM_MOVE_LOOP_LENGTHS];
    if (!b)
        snprintf(o, (size_t)cap, "Open");
    else
        snprintf(o, (size_t)cap, "%d Bar%s", b, b > 1 ? "s" : "");
}
static void record_page(const rfm_move_t *m, mr_canvas_t *cv)
{
    const int l = lane_of(m->loop.cur);
    char v[16];
    ic2(cv, PI_REC, 2, 2, 1);
    locator(m, cv, 96, 0, m->loop.cur);
    src_text(m, m->loop.lane[l][RFM_MOVE_LOOP_L_SOURCE], v, sizeof v);
    rowi(cv, 31, I_SRC, v, 0);
    len_text(m->loop.lane[l][RFM_MOVE_LOOP_L_LENGTH], v, sizeof v);
    rowi(cv, 48, I_LEN, v, 0);
}
static void fader(mr_canvas_t *cv, int x, int level, int pan, int hollow)
{
    const int h = (int)(rfmv_db_frac(rfmv_lvl_db10(level)) * 46 + 0.5);
    v2_O(cv, x, 4, 12, 50, 1);
    if (!hollow)
        v2_R(cv, x + 2, 52 - h, 8, h, 1);
    else if (h > 0)
        v2_O(cv, x + 2, 52 - h, 8, h, 1);
    v2_R(cv, x - 1, 60, 14, 1, 1);
    v2_R(cv, x - 1 + (pan * 12 + 63) / 127, 57, 2, 5, 1);
}
static void sends(mr_canvas_t *cv, int x, const int *v)
{
    int i;
    for (i = 0; i < 3; i++) {
        v2_O(cv, x, 8 + i * 16, 22, 7, 1);
        v2_R(cv, x + 2, 10 + i * 16, v[i] * 18 / 127, 3, 1);
    }
}
static void loopmix_page(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_loop_slot_t *d = CSL(m, m->loop.cur);
    const int sv[3] = { d->v[RFM_MOVE_LOOP_P_SEND1], d->v[RFM_MOVE_LOOP_P_SEND2], d->v[RFM_MOVE_LOOP_P_SEND3] };
    wave(cv, d, 0, 82, 32, 13, -1);
    fader(cv, 88, d->v[RFM_MOVE_LOOP_P_LEVEL], d->v[RFM_MOVE_LOOP_P_PAN], d->v[RFM_MOVE_LOOP_P_MUTE] != 0);
    sends(cv, 105, sv);
}
static void loopermix_page(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_loop_t *t = &m->loop;
    const int sv[3] = { t->mix[2], t->mix[3], t->mix[4] };
    int i;
    for (i = 0; i < LANES; i++) {
        const int x = 4 + i * 18, h = (int)(rfmv_db_frac(rfmv_lvl_db10(t->lane[i][RFM_MOVE_LOOP_L_LEVEL])) * 40 + 0.5);
        v2_R(cv, x, 56, 12, 1, 1);
        v2_R(cv, x + 3, 56 - h, 6, h, 1);
        if (lane_play(m, i) >= 0)
            v2_R(cv, x + 1, 56 - h - 4, 10, 2, 1);      /* (a cap: the lane sounds) */
        if (i == lane_of(t->cur))
            v2_R(cv, x + 1, 60, 10, 2, 1);              /* (the chosen loop's lane) */
    }
    v2_dotV(cv, 78, 2, 60);
    fader(cv, 84, t->mix[RFM_MOVE_KIT_MIX_LEVEL], t->mix[RFM_MOVE_KIT_MIX_PAN], 0);
    sends(cv, 102, sv);
}
/* the sign of what a recording takes: its track's number, K, the input's icon, M, L */
static void src_sign(const rfm_move_t *m, mr_canvas_t *cv, int src, int x, int y)
{
    char s[4];
    if (src == RFM_MOVE_LOOP_SRC_INPUT) {
        ic(cv, I_IN, x, y + 1, 1);
        return;
    }
    if (src == RFM_MOVE_LOOP_SRC_PLAYED)
        snprintf(s, sizeof s, "%d", m->fw.active % 3 + 1);
    else if (src >= RFM_MOVE_LOOP_SRC_TRACK1 && src <= RFM_MOVE_LOOP_SRC_TRACK3)
        snprintf(s, sizeof s, "%d", src - RFM_MOVE_LOOP_SRC_TRACK1 + 1);
    else
        snprintf(s, sizeof s, "%s", src == RFM_MOVE_LOOP_SRC_KIT ? "K" : src == RFM_MOVE_LOOP_SRC_MIX ? "M" : "L");
    v2_T(cv, x, y, s, 2, 1);
}
/* (boards I, J, K) a recording: armed (the beats to go), Open (the bar, the beat, the wave, the level, the bars so
 * far), fixed ("2/2") */
static void rec_screen(const rfm_move_t *m, mr_canvas_t *cv, int s)
{
    const rfm_move_loop_t *t = &m->loop;
    const int l = lane_of(s), rs = rec_state(m, s), src = t->lane[l][RFM_MOVE_LOOP_L_SOURCE];
    int bar = 0, bars = LEN_BARS[t->lane[l][RFM_MOVE_LOOP_L_LENGTH] % RFM_MOVE_LOOP_LENGTHS], i;
    char v[12];
    if (t->st_ok)
        for (i = 0; i < 2; i++)
            if (t->st.rec[i].slot == s)
                bar = t->st.rec[i].bar, bars = t->st.rec[i].bars;
    ic(cv, I_DOT, 2, 1, 1);
    ic(cv, I_SRC, 18, 1, 1);
    src_sign(m, cv, src, 32, 0);
    if (rs == RFM_MOVE_LOOP_RS_ARMED || !bar) {
        const int b = running(m) && t->st_ok ? t->st.beat : -1;
        for (i = 0; i < 4; i++) {
            if (i < b + 1 && b >= 0)
                v2_R(cv, 8 + i * 30, 26, 22, 22, 1);
            else
                v2_O(cv, 8 + i * 30, 26, 22, 22, 1);
        }
        locator(m, cv, 96, 0, s);
        return;
    }
    if (bars) {
        snprintf(v, sizeof v, "%d/%d", bar, bars);
        v2_TC(cv, 70, 0, v, 2, 1);
    } else {
        snprintf(v, sizeof v, "%d", bar);
        v2_TC(cv, 64, 0, v, 2, 1);
        for (i = 0; i < 4; i++) {                       /* (the beat: a mark a beat, the one now solid) */
            const int on = t->st_ok && t->st.beat == i;
            v2_R(cv, 84 + i * 6, on ? 3 : 5, 4, on ? 6 : 2, 1);
        }
    }
    {
        const int npk = t->st_ok ? t->st.npk : 0, w = 116, fill = npk * w / RFM_MOVE_LOOP_RPEAKS;
        for (i = 0; i < npk; i++) {
            const int x = i * w / RFM_MOVE_LOOP_RPEAKS, a = 1 + t->st.pk[i] * 13 / 255;
            v2_R(cv, x, 36 - a, 2, a * 2, 1);
        }
        v2_dotH(cv, fill, 36, w - fill);
    }
    {
        const int lv = t->st_ok ? t->st.level : 0, h = lv * 28 / 32767;
        v2_R(cv, 121, 45 - h, 6, h, 1), v2_dotV(cv, 124, 17, 28 - h);
    }
    bars_row(cv, 0, 116, 56, 6, bars ? bars : bar, bar - 1);
    if (rs == RFM_MOVE_LOOP_RS_ENDING)
        v2_T(cv, 2, 14, "End", 1, 1);
}
static void copy_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    if (m->loop.copy_src >= 0) {
        char nm[8];
        slot_name(m->loop.copy_src, nm, sizeof nm);
        ic(cv, I_LOOP, 2, 1, 1);
        v2_T(cv, 18, 0, nm, 2, 1);
        v2_L(cv, 60, 7, 108, 7), v2_L(cv, 104, 3, 108, 7), v2_L(cv, 104, 11, 108, 7);
    }
    v2_TC(cv, 64, 30, "Copy", 2, 1);
}
static void val_text(const rfm_move_t *m, int j, int v, char *o, int cap)
{
    const rfm_move_loop_slot_t *d = CSL(m, m->loop.cur);
    switch (j) {
    case RFM_MOVE_LOOP_P_START: snprintf(o, (size_t)cap, v ? "+%d Step%s" : "Start", v, v == 1 ? "" : "s"); return;
    case RFM_MOVE_LOOP_P_LENGTH:
        if (v % RFM_MOVE_LOOP_STEPS == 0)
            snprintf(o, (size_t)cap, "%d Bar%s", v / RFM_MOVE_LOOP_STEPS, v == RFM_MOVE_LOOP_STEPS ? "" : "s");
        else
            snprintf(o, (size_t)cap, "%d Steps", v);
        return;
    case RFM_MOVE_LOOP_P_PITCH:
        if (v % 100 == 0)
            snprintf(o, (size_t)cap, "%+d st", v / 100);
        else
            snprintf(o, (size_t)cap, "%+d ct", v);
        if (!v)
            snprintf(o, (size_t)cap, "0 st");
        return;
    case RFM_MOVE_LOOP_P_REVERSE:
    case RFM_MOVE_LOOP_P_MUTE: snprintf(o, (size_t)cap, v ? "On" : "Off"); return;
    case RFM_MOVE_LOOP_P_WARP: snprintf(o, (size_t)cap, v == RFM_MOVE_LOOP_W_OFF ? "Off" : "Repitch"); return;
    case RFM_MOVE_LOOP_P_PAN:
    case KS_PAN:
        if (v == 64)
            snprintf(o, (size_t)cap, "C");
        else
            snprintf(o, (size_t)cap, "%c%d", v < 64 ? 'L' : 'R', v < 64 ? (64 - v) * 50 / 64 : (v - 64) * 50 / 63);
        return;
    case RFM_MOVE_LOOP_P_SEND1:
    case RFM_MOVE_LOOP_P_SEND2:
    case RFM_MOVE_LOOP_P_SEND3:
    case KS_SEND1:
    case KS_SEND2:
    case KS_SEND3: snprintf(o, (size_t)cap, "%d %%", (v * 100 + 63) / 127); return;
    case KL_SOURCE: src_text_l(m, v, o, cap, 1); return;
    case KL_LENGTH: len_text(v, o, cap); return;
    default: rfmv_db_text(o, cap, rfmv_lvl_db10(v), 1); return;
    }
    (void)d;
}
static void job_name(const rfm_move_t *m, int j, char *o, int cap)
{
    if (j >= 0 && j < RFM_MOVE_LOOP_P_N)
        snprintf(o, (size_t)cap, "%s", P_NAME[j]);
    else if (j == KL_SOURCE)
        snprintf(o, (size_t)cap, "Source");
    else if (j == KL_LENGTH)
        snprintf(o, (size_t)cap, "Length");
    else if (j >= KL_LVL0 && j < KL_LVL0 + LANES)
        snprintf(o, (size_t)cap, "Lane %d", j - KL_LVL0 + 1);
    else
        snprintf(o, (size_t)cap, "%s", j == KS_LEVEL ? "Loops" : j == KS_PAN ? "Loops Pan" : j == KS_SEND1 ? "Loops Send 1"
                                                                          : j == KS_SEND2 ? "Loops Send 2" : "Loops Send 3");
    (void)m;
}
static int is_level(int j) { return j == RFM_MOVE_LOOP_P_LEVEL || (j >= KL_LVL0 && j < KL_LVL0 + LANES) || j == KS_LEVEL; }
static void level_screen(const rfm_move_t *m, mr_canvas_t *cv, int j)
{
    const rfm_move_loop_t *t = &m->loop;
    int sib[LANES + 1], n = 0, cur = 0, i;
    char ttl[24];
    if (j == RFM_MOVE_LOOP_P_LEVEL) {
        char nm[8];
        slot_name(t->cur, nm, sizeof nm);
        snprintf(ttl, sizeof ttl, "Loop %s Level", nm);
        rfmv_level_popup(cv, ttl, rfmv_lvl_db10(job_val(m, j)), NULL, NULL, 0, 0);
        return;
    }
    for (i = 0; i < LANES; i++)
        sib[n++] = rfmv_lvl_db10(t->lane[i][RFM_MOVE_LOOP_L_LEVEL]);
    sib[n++] = rfmv_lvl_db10(t->mix[RFM_MOVE_KIT_MIX_LEVEL]);
    cur = j == KS_LEVEL ? LANES : j - KL_LVL0;
    if (j == KS_LEVEL)
        snprintf(ttl, sizeof ttl, "Loops Level");
    else
        snprintf(ttl, sizeof ttl, "Lane %d Level", j - KL_LVL0 + 1);
    rfmv_level_popup(cv, ttl, rfmv_lvl_db10(job_val(m, j)), NULL, sib, n, cur);
}
static void popup(const rfm_move_t *m, mr_canvas_t *cv, int j)
{
    const rfm_move_loop_slot_t *d = CSL(m, m->loop.cur);
    const int v = job_val(m, j);
    char name[24], val[24];
    int i;
    if (is_level(j)) {
        level_screen(m, cv, j);
        return;
    }
    job_name(m, j, name, sizeof name);
    val_text(m, j, v, val, sizeof val);
    v2_titleBox(cv, name, val, 64, 128);
    switch (j) {
    case RFM_MOVE_LOOP_P_PITCH: v2_vbar(cv, v / 2400.0, 1, 8, 112, 55); break;
    case RFM_MOVE_LOOP_P_LENGTH: {                      /* (board E) the bars recorded, those played filled */
        const int n = clampi(d->bars, 1, 16);
        bars_row(cv, 8, 114, 54, 7, n, (v + RFM_MOVE_LOOP_STEPS - 1) / RFM_MOVE_LOOP_STEPS);
        break;
    }
    case RFM_MOVE_LOOP_P_START: {                       /* (board F) a bar's 16 steps, the start on, still on the bar */
        const int tot = loop_steps(d), n = tot > 16 ? 16 : tot, at = n * v / tot;
        for (i = 0; i <= n; i++)
            v2_R(cv, 4 + i * 120 / n, 52, 1, i % 4 == 0 ? 5 : 3, 1);
        v2_R(cv, 4 + at * 120 / n, 58, 120 - at * 120 / n, 4, 1);
        break;
    }
    case RFM_MOVE_LOOP_P_PAN:
    case KS_PAN: v2_vbar(cv, (v - 64) / 64.0, 1, 8, 112, 55); break;
    case RFM_MOVE_LOOP_P_SEND1:
    case RFM_MOVE_LOOP_P_SEND2:
    case RFM_MOVE_LOOP_P_SEND3:
    case KS_SEND1:
    case KS_SEND2:
    case KS_SEND3: v2_vbar(cv, v / 127.0, 0, 8, 112, 55); break;
    default: break;
    }
}
int rfmv_loop_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_loop_t *t = &m->loop;
    int r;
    if (!rfmv_loop_shown(m))
        return 0;
    mr_gfx_clear(cv, 0);
    if (msg_up(m)) {
        v2_msg(cv, t->msg, t->msg2);
        return 1;
    }
    if (m->seq.mods & SEQ_M_COPY) {
        copy_screen(m, cv);
        return 1;
    }
    if (m->seq.mods & SEQ_M_DELETE) {
        v2_TC(cv, 64, 30, "Delete", 2, 1);
        return 1;
    }
    if (kpop_up(m)) {
        popup(m, cv, knob_job(m, t->kpop));
        return 1;
    }
    if (t->turned && now(m) - t->turn_ms < RFM_MOVE_POPUP_MS) {
        page_switch(m, cv);
        return 1;
    }
    if ((r = rec_any(m)) >= 0) {
        rec_screen(m, cv, r);
        return 1;
    }
    switch (t->page % PG_N) {
    case PG_LOOPS: lanes_page(m, cv); break;
    case PG_LOOP:
        if (has_loop(CSL(m, t->cur)))
            loop_page(m, cv);
        else
            record_page(m, cv);
        break;
    case PG_RECORD: record_page(m, cv); break;
    case PG_LOOPMIX:
        if (has_loop(CSL(m, t->cur)))
            loopmix_page(m, cv);
        else
            record_page(m, cv);
        break;
    default: loopermix_page(m, cv); break;
    }
    return 1;
}
int rfmv_loop_level_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_loop_t *t = &m->loop;
    if (!rfmv_loop_cap(m) || !lvl_up(m) || (t->lvl == 1 && !rfmv_loop_shown(m)))
        return 0;
    mr_gfx_clear(cv, 0);
    level_screen(m, cv, t->lvl == 2 ? KS_LEVEL : RFM_MOVE_LOOP_P_LEVEL);
    return 1;
}
int rfmv_loop_timed(const rfm_move_t *m)
{
    const rfm_move_loop_t *t = &m->loop;
    int l;
    if (rfmv_loop_cap(m) && t->lvl && now(m) - t->lvl_ms < RFM_MOVE_POPUP_MS + 100)
        return 1;
    if (!rfmv_loop_active(m))
        return 0;
    if (msg_up(m) || pg_off(m) || (t->turned && now(m) - t->turn_ms < RFM_MOVE_POPUP_MS) ||
        (t->kpop >= 0 && now(m) - t->kpop_ms < RFM_MOVE_POPUP_MS + 100) || rec_any(m) >= 0 || t->copy_src >= 0)
        return 1;
    for (l = 0; l < LANES; l++)
        if (lane_next(m, l) >= 0 || (lane_play(m, l) >= 0 && running(m)))
            return 1;
    return 0;
}

/* ================================================================ words === */
int rfmv_loop_base_words(const rfm_move_t *m, char *o, int cap)
{
    char nm[8];
    if (!rfmv_loop_shown(m))
        return 0;
    slot_name(m->loop.cur, nm, sizeof nm);
    snprintf(o, (size_t)cap, "Looper, %s %s", has_loop(CSL(m, m->loop.cur)) ? "loop" : "empty", nm);
    return 1;
}
int rfmv_loop_pop_words(const rfm_move_t *m, char *o, int cap, uint16_t *subj, uint32_t *stamp)
{
    const rfm_move_loop_t *t = &m->loop;
    char v[24], nm[24];
    int r;
    o[0] = 0, *stamp = t->chg_ms;
    if (msg_up(m)) {
        snprintf(o, (size_t)cap, "%s%s%s", t->msg, t->msg2[0] ? " " : "", t->msg2);
        *subj = 0xB00, *stamp = t->msg_ms;
    } else if (kpop_up(m)) {
        const int j = knob_job(m, t->kpop);
        job_name(m, j, nm, sizeof nm);
        val_text(m, j, job_val(m, j), v, sizeof v);
        snprintf(o, (size_t)cap, "%s: %s", nm, v);
        *subj = (uint16_t)(0xB10 | t->kpop);
    } else if (t->turned && now(m) - t->turn_ms < RFM_MOVE_POPUP_MS) {
        snprintf(o, (size_t)cap, "%s page", PG_NAME[t->page % PG_N]);
        *subj = (uint16_t)(0xB20 | t->page), *stamp = t->turn_ms;
    } else if ((r = rec_any(m)) >= 0) {
        slot_name(r, nm, sizeof nm);
        snprintf(o, (size_t)cap, "%s %s", rec_state(m, r) == RFM_MOVE_LOOP_RS_ARMED ? "Armed" : "Recording", nm);
        *subj = (uint16_t)(0xB30 | rec_state(m, r)), *stamp = 0;
    }
    return o[0] != 0;
}

/* ================================================================ the sampler's Loops tab (card lp_tab a) === */
/* the grid in small: 8 x 4 cells of 13 x 6, lanes from y0: a loop an outline, playing solid with its progress,
 * waiting dashed, recording solid with a black dot, empty a dot; the jog's cell underlined */
void rfmv_loop_tab_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_loop_t *t = &m->loop;
    const int y0 = 16, s0 = t->tab_slot;
    int s;
    char v[24];
    for (s = 0; s < NSLOT; s++) {
        const int l = lane_of(s), x = scene_of(s) * 16 + 1, y = y0 + l * 8, rs = rec_state(m, s);
        if (rs == RFM_MOVE_LOOP_RS_RECORDING || rs == RFM_MOVE_LOOP_RS_ENDING)
            v2_R(cv, x, y, 13, 6, 1), v2_R(cv, x + 5, y + 2, 3, 2, 0);
        else if (rs == RFM_MOVE_LOOP_RS_ARMED || (lane_next(m, l) == scene_of(s) && lane_play(m, l) != scene_of(s)))
            v2_dashO(cv, x, y, 13, 6);
        else if (has_loop(CSL(m, s)) && lane_play(m, l) == scene_of(s))
            v2_R(cv, x, y, 13, 6, 1), v2_R(cv, x + 9, y + 2, 3, 2, 0);
        else if (has_loop(CSL(m, s)))
            v2_O(cv, x, y, 13, 6, 1);
        else
            v2_px(cv, x + 6, y + 3, 1);
        if (s == s0)
            v2_R(cv, x, y + 7, 13, 1, 1);
    }
    {
        const int rs = rec_state(m, s0), l = lane_of(s0);
        const rfm_move_loop_slot_t *d = CSL(m, s0);
        if (rs == RFM_MOVE_LOOP_RS_RECORDING || rs == RFM_MOVE_LOOP_RS_ENDING) {
            int bar = 0, i;
            for (i = 0; i < 2 && t->st_ok; i++)
                if (t->st.rec[i].slot == s0)
                    bar = t->st.rec[i].bar;
            snprintf(v, sizeof v, "Bar %d", bar);
            rowi(cv, 49, I_DOT, v, 1);
        } else if (rs == RFM_MOVE_LOOP_RS_ARMED)
            rowi(cv, 49, I_DOT, "Armed", 1);
        else if (has_loop(d)) {
            const int b = clampi(d->bars, 1, 99);
            snprintf(v, sizeof v, "%d Bar%s", b, b > 1 ? "s" : "");
            rowi(cv, 49, lane_play(m, l) == scene_of(s0) ? I_STOP : I_PLAY, v, 1);
        } else {                                        /* (an empty slot: the source at 2x, its length at 1x) */
            char b[12];
            src_text(m, t->lane[l][RFM_MOVE_LOOP_L_SOURCE], v, sizeof v);
            len_text(t->lane[l][RFM_MOVE_LOOP_L_LENGTH], b, sizeof b);
            rowi(cv, 49, I_DOT, v, 1);
            v2_T(cv, 126 - v2_tw(b, 1), 53, b, 1, 0);
        }
    }
    if (t->tab_kpop >= 0 && (t->tab_kdown >> t->tab_kpop & 1 || now(m) - t->tab_kpop_ms < RFM_MOVE_POPUP_MS)) {
        mr_gfx_clear(cv, 0);
        level_screen(m, cv, KL_LVL0 + t->tab_kpop);
    }
}
int rfmv_loop_tab_turn(rfm_move_t *m, int d)
{
    rfm_move_loop_t *t = LP(m);
    const int to = t->tab_slot + d;
    t->tab_kpop = -1;
    if (to < 0)
        return 0;                                       /* (up past the first slot: the tabs) */
    t->tab_slot = (uint8_t)clampi(to, 0, NSLOT - 1);
    return 1;
}
/* the click: record into an empty slot, end its recording, play a loop, stop a playing one (each at the bar) */
void rfmv_loop_tab_click(rfm_move_t *m)
{
    rfm_move_loop_t *t = LP(m);
    t->tab_kpop = -1;
    slot_tap(m, t->tab_slot);
    t->tap_slot = -1;                                   /* (no double click from the jog) */
}
int rfmv_loop_tab_sample(rfm_move_t *m)
{
    rfm_move_loop_t *t = LP(m);
    const int r = rec_any(m), s = t->tab_slot;
    if (r >= 0) {
        if (rec_state(m, r) == RFM_MOVE_LOOP_RS_ARMED)
            rec_cancel(m, r);
        else if (rec_state(m, r) == RFM_MOVE_LOOP_RS_RECORDING)
            rec_end(m, r);
        return 1;
    }
    rec_start(m, s);                                    /* (Sample: records into the jog's slot, full or not) */
    return 1;
}
int rfmv_loop_tab_knob(rfm_move_t *m, int k, int touch, int d)
{
    rfm_move_loop_t *t = LP(m);
    if (k < 0 || k > 3)
        return 0;
    if (touch > 0)
        t->tab_kdown = (uint8_t)(t->tab_kdown | 1u << k);
    else if (touch == 0)
        t->tab_kdown = (uint8_t)(t->tab_kdown & ~(1u << k));
    t->tab_kpop = (int8_t)k, t->tab_kpop_ms = now(m);
    if (d)
        level_set(m, KL_LVL0 + k, rfmv_lvl_step(t->lane[k][RFM_MOVE_LOOP_L_LEVEL], d));
    return 1;
}
int rfmv_loop_tab_words(const rfm_move_t *m, char *o, int cap)
{
    const rfm_move_loop_t *t = &m->loop;
    char nm[8], v[16];
    const int s = t->tab_slot, rs = rec_state(m, s);
    slot_name(s, nm, sizeof nm);
    if (t->tab_kpop >= 0 && now(m) - t->tab_kpop_ms < RFM_MOVE_POPUP_MS) {
        rfmv_db_text(v, sizeof v, rfmv_lvl_db10(t->lane[t->tab_kpop][RFM_MOVE_LOOP_L_LEVEL]), 1);
        snprintf(o, (size_t)cap, "Lane %d level %s", t->tab_kpop + 1, v);
    } else if (rs)
        snprintf(o, (size_t)cap, "%s %s", rs == RFM_MOVE_LOOP_RS_ARMED ? "Armed" : "Recording", nm);
    else if (has_loop(CSL(m, s)))
        snprintf(o, (size_t)cap, "Loop %s, %d bars", nm, CSL(m, s)->bars);
    else
        snprintf(o, (size_t)cap, "Empty %s", nm);
    return 1;
}
void rfmv_loop_tab_leds(const rfm_move_t *m, rgb_t c[L_WHITE0])
{
    int i;
    for (i = 0; i < 4; i++)
        c[L_RING0 + i] = (m->loop.tab_kdown >> i & 1) ? RFMV_WHITE : sc(LOOPC, 150);
    for (i = 4; i < 8; i++)
        c[L_RING0 + i] = RFMV_OFF;
}
int rfmv_loop_tab_timed(const rfm_move_t *m)
{
    const rfm_move_loop_t *t = &m->loop;
    int l;
    if (rec_any(m) >= 0 || (t->tab_kpop >= 0 && now(m) - t->tab_kpop_ms < RFM_MOVE_POPUP_MS + 100))
        return 1;
    for (l = 0; l < LANES; l++)
        if (lane_next(m, l) >= 0)
            return 1;
    return 0;
}
