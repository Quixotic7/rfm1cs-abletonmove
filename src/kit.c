/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (stage 3a, the owner's 2026-11-25 kit round) the kit: 16 sampled pads, Track 4's third view (move.h "the kit";
 * README "The kit (stage 3a)"; remoteFM14Move-kit docs/design/KIT-SPEC.md and mock_kit_v1.c). Only with
 * RFM_MOVE_CAP_KIT. The view is the sequencer's track view of the chosen pad's track (13..16) with the kit's own pads,
 * steps, knobs, jog, screen and Back: the left 4 x 4 the pads (pad 1 bottom left, rows upward), the right 4 x 4 the
 * chosen pad's pitches (Pitches, In Key or Off), the steps the chosen pad's notes, the knobs its settings (1 Pitch,
 * 2 Start, 4 Play mode, 5 Attack, 6 Release, 7 Level; 3 Stretch and 8 Send 1 dark and dead until stage 3b), the jog
 * the rows of three tabs (Pad, Kit, Files). Play, Record, Capture, Loop, Copy, Mute, the arrows, Undo and Shift + a
 * step stay the sequencer's. The host owns the sounds (KIT_LOAD, KIT_ROWS, KIT_PREVIEW, KIT_REC) and the voices
 * (KIT_NOTE, KIT_SET); the core asks, never waits, and draws what has come.
 *
 * A kit track's note says which pad plays at which pitch (rfm_move_kit_note): the pad's own note 36 + pad, or one of
 * the track's other notes for the right half's pitches, so a step keeps that pitch through a save and a load.
 *
 * (stage 3b) Stretch (knob 3: start..end repitched to last N steps at the tempo, re-fitted when it changes; while
 * Pitch, Stretch, Start or End change the step keys light the steps it lasts, board D), the sends (knob 8 Send 1;
 * rows Send 1..3; the kit's own strip in the Mixer, fw.c), a whole folder (Shift + the click on a folder: All To Pads,
 * board K), kits saved and loaded (the Kit tab's Load, Save; Files' Kits; boards Q, R), the sampler's To Kit (the next
 * empty pad lit white, board U). One Undo brings back the kit a folder or a kit replaced.
 *
 * (stage 3c, making sounds; README "The kit (stage 3c)") Copy held, a pad, then another: Copy / Mix / Join (board N;
 * Mix and Join the host's new file, KIT_MERGE; board O the join's seam), Copy let go first: nothing; the Sound row's
 * tools Play, Trim, Normalize, Reverse, Delete (board V; an edit the pad's own new file, KIT_EDIT); a take's To Kit
 * then Slice (board P: KIT_SLICE's cuts, the pads the slices, knobs 1 and 2 the chosen cut, the click the slices onto
 * the pads from the chosen one, a region each of the one file); Undo brings a pad or the kit back. The page's Kit tab
 * calls rfm_move_kit_play / _choose / _set. */
#include <stdio.h>
#include <string.h>

#include "move_int.h"
#include "v2.h"

#define clampi rfmv_clampi
#define K(m) (&(m)->kit)
typedef rfmv_rgb_t rgb_t;

/* the kit's colours: the view (Track 4, the knobs) pink; the four kit tracks a row each */
static const rgb_t KIT = { 255, 60, 160 };
static const rgb_t ROWC[4] = { { 255, 60, 160 }, { 255, 140, 40 }, { 120, 220, 255 }, { 170, 120, 255 } };
#define sc rfmv_sc

/* ---- the icons (mock_kit_v1.c's, 11 x 11) */
typedef const char *const icon_t[11];
static icon_t I_LIST = { "...........", "##..#######", "##..#######", "...........", "##..#######", "##..#######",
                         "...........", "##..#######", "##..#######", "...........", "..........." };
static icon_t I_DOT = { "...........", "...#####...", "..#######..", ".#########.", ".#########.", ".#########.",
                        ".#########.", ".#########.", "..#######..", "...#####...", "..........." };
static icon_t I_PLAY = { "...........", "..##.......", "..####.....", "..######...", "..########.", "..########.",
                         "..######...", "..####.....", "..##.......", "...........", "..........." };
static icon_t I_X = { "...........", ".##.....##.", "..##...##..", "...##.##...", "....###....", "....###....",
                      "...##.##...", "..##...##..", ".##.....##.", "...........", "..........." };
static icon_t I_SRC = { "........##.", "....#...##.", ".....#..##.", "......#.##.", "#######.##.", "#######.##.",
                        "......#.##.", ".....#..##.", "....#...##.", "........##.", "..........." };
static icon_t I_DIR = { "...........", ".####......", "#....#####.", "#.........#", "#.........#", "#.........#",
                        "#.........#", "#.........#", "###########", "...........", "..........." };
static icon_t I_REV = { "......#....", "......##...", "##########.", "......##...", "......#....", "....#......",
                        "...##......", ".##########", "...##......", "....#......", "..........." };
static icon_t I_LOOP = { "...#####...", "..#.....#..", ".#.......#.", ".#.......#.", ".#.......#.", ".#.....####",
                         ".#......###", "..#......#.", "...###.....", "...........", "..........." };
static icon_t I_START = { "##.........", "#..........", "#....#.....", "#...##.....", "#..########", "#..########",
                          "#...##.....", "#....#.....", "#..........", "##.........", "..........." };
static icon_t I_END = { ".........##", "..........#", ".....#....#", ".....##...#", "########..#", "########..#",
                        ".....##...#", ".....#....#", "..........#", ".........##", "..........." };
static icon_t I_KIT = { "...........", "####..####.", "####..####.", "####..####.", "####..####.", "...........",
                        "####..####.", "####..####.", "####..####.", "####..####.", "..........." };
static icon_t I_PAD = { "...........", "#########..", "#########..", "##.....##..", "##.....##..", "##.....##..",
                        "##.....##..", "#########..", "#########..", "...........", "..........." };
static icon_t I_PITCH = { "....#......", "...###.....", "..#.#.#....", "....#......", "....#......", "....#...#..",
                          "....#...#..", "........#..", "......#.#.#", ".......###.", "........#.." };
static icon_t I_ENV = { "...........", "...#####...", "..#.....#..", "..#......#.", ".#........#", ".#........#",
                        "#.........#", "#..........", "#..........", "###########", "..........." };
static icon_t I_CHOKE = { "##.....##..", "##.....##..", "##.....##..", "##..#..##..", "##...#.##..", "##....###..",
                          "##.....##..", "##.....##..", "##.....##..", "##.....##..", "..........." };
static icon_t I_LVL = { "...........", "........##.", "........##.", ".....##.##.", ".....##.##.", "..##.##.##.",
                        "..##.##.##.", "..##.##.##.", "..##.##.##.", "##########.", "..........." };
static icon_t I_PAN = { "...........", "...........", ".#.......#.", "##.......##", "###########", "##.......##",
                        ".#..###..#.", "....###....", "....###....", "...........", "..........." };
static icon_t I_STRETCH = { "#.........#", "#.........#", "#..#...#..#", "#.##...##.#", "###########", "#.##...##.#",
                            "#..#...#..#", "#.........#", "#.........#", "...........", "..........." };
static icon_t I_SEND = { "...........", "......#....", "......##...", "###########", "......##...", "......#....",
                         "#..........", "#..........", "#..........", "#..........", "..........." };
static icon_t I_SAVE = { "###########", "#.........#", "#..#####..#", "#.........#", "#.........#", "#..#####..#",
                         "#..#####..#", "#..#####..#", "#..#####..#", "###########", "..........." };
static icon_t I_KEYS = { "###########", "#.#.#.#.#.#", "#.#.#.#.#.#", "#.#.#.#.#.#", "#.#.#.#.#.#", "#..#...#..#",
                         "#..#...#..#", "#..#...#..#", "#..#...#..#", "###########", "..........." };
/* (stage 3c) mix and join (board N), slicing (board P), a pad's own edits (board V) */
static icon_t I_COPY = { "#######....", "#.....#....", "#.....#....", "#...#######", "#...#.....#", "#####.....#",
                         "....#.....#", "....#.....#", "....#.....#", "....#######", "..........." };
static icon_t I_MIX = { "#.........#", ".#.......#.", "..#.....#..", "...#...#...", "....#.#....", ".....#.....",
                        ".....#.....", ".....#.....", ".....#.....", ".....#.....", "..........." };
static icon_t I_JOIN = { "...........", "...........", "####.######", "####.######", "####.######", "####.######",
                         "####.######", "...........", "...........", "...........", "..........." };
static icon_t I_SLICE = { "..#..#..#..", "..#..#..#..", "#.#..#..#.#", "#.##.#.##.#", "####.####.#",
                          "#.##.#.##.#", "#.#..#..#.#", "..#..#..#..", "..#..#..#..", "...........", "..........." };
static icon_t I_TRIM = { "##.......##", "#.........#", "#.........#", "#..#...#..#", "#.##...##.#", "#.##...##.#",
                         "#..#...#..#", "#.........#", "#.........#", "##.......##", "..........." };
static icon_t I_NORM = { "###########", "...........", "..#.....#..", ".###...###.", "#####.#####", "..#.....#..",
                         "..#..#..#..", "..#..#..#..", "..#..#..#..", "..#..#..#..", "..........." };
static void ic(mr_canvas_t *cv, icon_t i, int x, int y, int v)
{
    int r, c;
    for (r = 0; r < 11; r++)
        for (c = 0; c < 11 && i[r][c]; c++)
            if (i[r][c] == '#')
                v2_px(cv, x + c, y + r, v);
}

/* ---- the Pad tab's rows (3b: Stretch after Pitch, the sends at the end) */
enum { KR_SOUND, KR_PITCH, KR_STRETCH, KR_START, KR_END, KR_MODE, KR_ATTACK, KR_HOLD, KR_RELEASE, KR_CHOKE, KR_REVERSE,
       KR_LEVEL, KR_PAN, KR_SEND1, KR_SEND2, KR_SEND3, KR_N };
static const int8_t ROW_P[KR_N] = { -1, RFM_MOVE_KIT_P_PITCH, RFM_MOVE_KIT_P_STRETCH, RFM_MOVE_KIT_P_START,
                                    RFM_MOVE_KIT_P_END, RFM_MOVE_KIT_P_MODE, RFM_MOVE_KIT_P_ATTACK, RFM_MOVE_KIT_P_HOLD,
                                    RFM_MOVE_KIT_P_RELEASE, RFM_MOVE_KIT_P_CHOKE, RFM_MOVE_KIT_P_REVERSE,
                                    RFM_MOVE_KIT_P_LEVEL, RFM_MOVE_KIT_P_PAN, RFM_MOVE_KIT_P_SEND1,
                                    RFM_MOVE_KIT_P_SEND2, RFM_MOVE_KIT_P_SEND3 };
static const char *const P_NAME[RFM_MOVE_KIT_P_N] = { "Pitch", "Start", "End", "Play", "Attack", "Hold", "Release",
                                                      "Choke", "Reverse", "Level", "Pan", "Mute", "Stretch",
                                                      "Send 1", "Send 2", "Send 3" };
static const char *const MODE_NAME[RFM_MOVE_KIT_M_N] = { "One Shot", "Momentary", "Loop" };
static const char *const MODE_SAY[RFM_MOVE_KIT_M_N] = { "One shot", "Momentary", "Loop" };
static icon_t *const MODE_ICON[RFM_MOVE_KIT_M_N] = { &I_PLAY, &I_PAD, &I_LOOP };
static const char *const RH_NAME[RFM_MOVE_KIT_RH_N] = { "Pitches", "In Key", "Off" };
static const char *const SCALE_SHORT[RFM_MOVE_NSCALES] = { "Major", "Minor", "Dorian", "Mixolyd", "PentMaj", "PentMin",
                                                           "HarmMin", "Blues", "Phrygn", "Lydian", "Locrian", "MelMin",
                                                           "Whole", "Dim HW", "Dim WH", "Hungar", "PhrDom", "In Sen",
                                                           "Hirajo", "Iwato", "Pelog", "DblHarm", "Span 8", "Bebop" };
/* the knobs: 1 Pitch, 2 Start, 3 Stretch, 4 Play mode, 5 Attack, 6 Release, 7 Level, 8 Send 1 */
static const int8_t KNOB_P[8] = { RFM_MOVE_KIT_P_PITCH, RFM_MOVE_KIT_P_START, RFM_MOVE_KIT_P_STRETCH, RFM_MOVE_KIT_P_MODE,
                                  RFM_MOVE_KIT_P_ATTACK, RFM_MOVE_KIT_P_RELEASE, RFM_MOVE_KIT_P_LEVEL,
                                  RFM_MOVE_KIT_P_SEND1 };
/* the envelope's times (ms), a detent each */
static const int16_t ENV_MS[] = { 0, 1, 2, 3, 5, 8, 10, 15, 20, 30, 40, 50, 75, 100, 150, 200, 300, 400, 500, 750,
                                  1000, 1500, 2000, 3000, 5000, 10000 };
#define NENV ((int)(sizeof ENV_MS / sizeof ENV_MS[0]))
#define LEVEL_MIN (-60)
#define GAP 16                                          /* START and END keep this much (of RFM_MOVE_KIT_FRAC) apart */
#define NOASK 0xFFFFu
#define GLOW_MS 150                                     /* a pad sounds: bright so long after its note */
enum { PL_N = 3 };                                      /* Files' places: Takes, Samples, Kits */
enum { KT_LOAD, KT_SAVE, KT_RH, KT_N };                 /* the Kit tab's rows */
#define MIX_LEVEL0 100                                  /* the kit's strip at first: as a firmware slot's */
/* (3c) the Sound row's tools (board V): Play, Trim, Normalize, Reverse, Delete */
enum { TL_PLAY, TL_TRIM, TL_NORM, TL_REV, TL_DEL, TL_N };
static icon_t *const TOOL_ICON[TL_N] = { &I_PLAY, &I_TRIM, &I_NORM, &I_REV, &I_X };
static const char *const TOOL_NAME[TL_N] = { "Play", "Trim", "Normalize", "Reverse", "Delete" };
static const char *const TOOL_DONE[TL_N] = { "", "Trimmed", "Normalized", "Reversed", "" };
static const char *const MG_NAME[RFM_MOVE_KIT_MG_N] = { "Copy", "Mix", "Join" };
static icon_t *const MG_ICON[RFM_MOVE_KIT_MG_N] = { &I_COPY, &I_MIX, &I_JOIN };
static const rgb_t MENU_KNOB = { 255, 72, 0 };          /* (3c) the slice screen's knobs: a menu's */

/* ================================================================ basics === */
int rfmv_kit_cap(const rfm_move_t *m) { return (m->fw.caps & RFM_MOVE_CAP_KIT) != 0; }
static uint32_t now(const rfm_move_t *m) { return rfmv_now_ms(m); }
static int trk(int pad) { return RFM_MOVE_KIT_TRACK0 + pad / 4; }
static int pad_p(int k) { return (k / 4) * 8 + k % 4; }  /* kit pad k (0..15) -> the Move's pad (0: bottom left) */
static int kit_of(int p) { return p % 8 < 4 ? (p / 8) * 4 + p % 8 : -1; }   /* .. and back (-1: the right half) */
static int right_of(int p) { return p % 8 >= 4 ? (p / 8) * 4 + p % 8 - 4 : -1; }
static rfm_move_kit_pad_t *PAD(rfm_move_t *m, int k) { return &m->kit.pad[k & 15]; }
static const rfm_move_kit_pad_t *CPAD(const rfm_move_t *m, int k) { return &m->kit.pad[k & 15]; }
static int has_sound(const rfm_move_kit_pad_t *d) { return d->path[0] || d->st == RFM_MOVE_KIT_LOADING; }
int rfmv_kit_active(const rfm_move_t *m)
{
    return rfmv_kit_cap(m) && m->kit.on && m->s.mode == RFM_MOVE_MODE_SEQ && !m->seq.view && !m->seq.projects;
}
int rfmv_kit_shown(const rfm_move_t *m)
{
    return rfmv_kit_active(m) && !m->seq.smenu && !rfmv_st_on(m) && !rfmv_smp_shown(m);
}
int rfm_move_kit_shown(const rfm_move_t *m) { return rfmv_kit_shown(m) && !m->track_held && !m->seq.fill; }
static void touched(rfm_move_t *m) { K(m)->chg_ms = now(m); }
static void defaults(int16_t v[RFM_MOVE_KIT_P_N])
{
    memset(v, 0, sizeof(int16_t) * RFM_MOVE_KIT_P_N);
    v[RFM_MOVE_KIT_P_END] = RFM_MOVE_KIT_FRAC;
}
static int p_min(int p)
{
    switch (p) {
    case RFM_MOVE_KIT_P_PITCH: return -2400;
    case RFM_MOVE_KIT_P_LEVEL: return LEVEL_MIN;
    case RFM_MOVE_KIT_P_PAN: return -50;
    default: return 0;
    }
}
static int p_max(int p)
{
    switch (p) {
    case RFM_MOVE_KIT_P_PITCH: return 2400;
    case RFM_MOVE_KIT_P_START:
    case RFM_MOVE_KIT_P_END: return RFM_MOVE_KIT_FRAC;
    case RFM_MOVE_KIT_P_MODE: return RFM_MOVE_KIT_M_N - 1;
    case RFM_MOVE_KIT_P_ATTACK:
    case RFM_MOVE_KIT_P_HOLD:
    case RFM_MOVE_KIT_P_RELEASE: return ENV_MS[NENV - 1];
    case RFM_MOVE_KIT_P_CHOKE: return 4;
    case RFM_MOVE_KIT_P_LEVEL: return 6;
    case RFM_MOVE_KIT_P_PAN: return 50;
    case RFM_MOVE_KIT_P_STRETCH: return RFM_MOVE_KIT_STRETCH_MAX;
    case RFM_MOVE_KIT_P_SEND1:
    case RFM_MOVE_KIT_P_SEND2:
    case RFM_MOVE_KIT_P_SEND3: return 100;
    default: return 1;
    }
}
int rfm_move_kit_value(const rfm_move_t *m, int pad, int p)
{
    return pad >= 0 && pad < RFM_MOVE_KIT_PADS && p >= 0 && p < RFM_MOVE_KIT_P_N ? m->kit.pad[pad].v[p] : -1;
}

/* ================================================================ the host's actions === */
static void act(rfm_move_t *m, int kind, int index, int down, int vel, int value, uint32_t take, uint32_t from,
                uint32_t count, const char *data)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.frame = m->seq.frame;
    a.kind = (uint8_t)kind, a.index = (uint8_t)index, a.down = (uint8_t)down, a.vel = (uint8_t)vel;
    a.value = (int16_t)value, a.take = take, a.from = from, a.count = count;
    if (data) {
        a.data = (const uint8_t *)data;
        a.len = (uint32_t)strlen(data);
    }
    if (m->fn)
        m->fn(m->ctx, &a);
}
/* (3b) a step of track tr at the tempo, in microseconds */
static uint32_t step_us(const rfm_move_t *m, int tr)
{
    const rfm_move_track_t *t = &m->seq.tr[tr & 15];
    const int bpm10 = m->seq.bpm10 ? m->seq.bpm10 : 1200;
    return (uint32_t)(rfmv_seq_tps(t->rate, t->feel) * 600000000.0 / ((double)bpm10 * 96.0) + 0.5);
}
/* the microseconds pad k's start..end lasts stretched (0: Stretch Off) */
static uint32_t stretch_us(const rfm_move_t *m, int k)
{
    const int n = m->kit.pad[k & 15].v[RFM_MOVE_KIT_P_STRETCH];
    return n > 0 ? (uint32_t)n * step_us(m, trk(k)) : 0;
}
static void send_set(rfm_move_t *m, int k, int p)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_KIT_SET, a.index = (uint8_t)k, a.param = (uint8_t)p, a.value = m->kit.pad[k].v[p];
    if (p == RFM_MOVE_KIT_P_STRETCH)
        a.from = stretch_us(m, k);
    if (m->fn)
        m->fn(m->ctx, &a);
}
/* (3b) the kit's strip to the host: MIX with slot RFM_MOVE_KIT_SLOT */
static void send_mix(rfm_move_t *m)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.frame = m->seq.frame;
    a.kind = RFM_MOVE_ACT_MIX, a.index = RFM_MOVE_KIT_SLOT, a.slot = RFM_MOVE_KIT_SLOT;
    memcpy(a.mix, m->kit.mix, sizeof a.mix);
    if (m->fn)
        m->fn(m->ctx, &a);
}
/* the tempo and the kit tracks' rates now (the stretches are fitted to them) */
static void fit_mark(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    int i;
    t->fit_bpm10 = m->seq.bpm10, t->fit_ok = 1;
    for (i = 0; i < 4; i++)
        t->fit_rate[i] = (uint8_t)(m->seq.tr[RFM_MOVE_KIT_TRACK0 + i].rate | m->seq.tr[RFM_MOVE_KIT_TRACK0 + i].feel << 4);
}
static void send_pad(rfm_move_t *m, int k)              /* the pad's sound and settings to the host */
{
    rfm_move_kit_pad_t *d = PAD(m, k);
    int p;
    if (d->path[0]) {
        d->st = RFM_MOVE_KIT_LOADING, d->fresh = 0;
        act(m, RFM_MOVE_ACT_KIT_LOAD, k, 0, 0, 0, 0, 0, 0, d->path);
    } else {
        d->st = RFM_MOVE_KIT_EMPTY;
        act(m, RFM_MOVE_ACT_KIT_LOAD, k, 0, 0, 0, 0, 0, 0, NULL);
    }
    for (p = 0; p < RFM_MOVE_KIT_P_N; p++)
        send_set(m, k, p);
}
void rfm_move_kit_sync(rfm_move_t *m)
{
    int k;
    if (!rfmv_kit_cap(m))
        return;
    fit_mark(m);
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++)
        send_pad(m, k);
    send_mix(m);
}
/* a pad's note to the host; the pad glows */
static void note_out(rfm_move_t *m, int k, int semis, int vel, int track)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.frame = m->seq.frame;
    a.kind = RFM_MOVE_ACT_KIT_NOTE, a.index = (uint8_t)k, a.value = (int16_t)semis, a.vel = (uint8_t)vel;
    a.down = (uint8_t)(vel > 0), a.track = (uint8_t)(track + 1);
    if (vel)
        m->kit.glow[k & 15] = now(m) ? now(m) : 1;
    if (m->fn)
        m->fn(m->ctx, &a);
}
/* (seq.c) a sequencer note on track (0..15): a track routed to the kit plays its pad */
void rfmv_kit_seq_note(rfm_move_t *m, int track, int note, int vel)
{
    int k, semis;
    if (!rfmv_kit_cap(m) || track < 0 || track >= RFM_MOVE_SEQ_TRACKS || !(m->kit.route >> track & 1))
        return;
    if ((k = rfm_move_kit_pad_of(track, note, &semis)) < 0)
        return;
    note_out(m, k, semis, vel, track);
}
static void preview_stop(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    if (t->prev_id)
        act(m, RFM_MOVE_ACT_KIT_PREVIEW, 0, 0, 0, t->prev_pl, t->prev_id, 0, 0, NULL);
    t->prev_id = 0;
}
static void msg(rfm_move_t *m, const char *a, const char *b)
{
    rfm_move_kit_t *t = K(m);
    snprintf(t->msg, sizeof t->msg, "%s", a);
    snprintf(t->msg2, sizeof t->msg2, "%s", b ? b : "");
    t->msg_ms = now(m) ? now(m) : 1;
    touched(m);
}
static int msg_up(const rfm_move_t *m) { return m->kit.msg_ms && now(m) - m->kit.msg_ms < RFM_MOVE_POPUP_MS; }

/* ================================================================ the project's part === */
void rfmv_kit_clear(rfm_move_t *m)                      /* an empty kit (an older project's, before the routing) */
{
    rfm_move_kit_t *t = K(m);
    int k;
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++) {
        memset(&t->pad[k], 0, sizeof t->pad[k]);
        defaults(t->pad[k].v);
        t->pad[k].st = RFM_MOVE_KIT_EMPTY;
    }
    t->rh = RFM_MOVE_KIT_RH_PITCHES, t->route = 0, t->cur = 0, t->und = 0, t->undk = 0;
    t->name[0] = 0;
    memset(t->mix, 0, sizeof t->mix);
    t->mix[RFM_MOVE_KIT_MIX_LEVEL] = MIX_LEVEL0, t->mix[RFM_MOVE_KIT_MIX_PAN] = 64;
}
/* a new project: the kit empty, tracks 13..16 routed to it (and to no firmware) */
void rfmv_kit_new(rfm_move_t *m)
{
    int s;
    rfmv_kit_clear(m);
    if (!rfmv_kit_cap(m))
        return;
    m->kit.route = 0xF000;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
        m->seq.fx.ffilt[s] = (uint16_t)(m->seq.fx.ffilt[s] & 0x0FFF);
}
int rfmv_kit_used(const rfm_move_t *m)                  /* anything to save (else the project stays version 10) */
{
    const rfm_move_kit_t *t = &m->kit;
    int k;
    if (t->route || t->rh || t->cur || t->name[0] || t->mix[RFM_MOVE_KIT_MIX_LEVEL] != MIX_LEVEL0 ||
        t->mix[RFM_MOVE_KIT_MIX_PAN] != 64 || t->mix[2] || t->mix[3] || t->mix[4])
        return 1;
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++) {
        int16_t d[RFM_MOVE_KIT_P_N];
        defaults(d);
        if (t->pad[k].path[0] || memcmp(t->pad[k].v, d, sizeof d))
            return 1;
    }
    return 0;
}
static void w16(uint8_t *p, int v) { p[0] = (uint8_t)(v & 0xFF), p[1] = (uint8_t)((v >> 8) & 0xFF); }
static int r16s(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | p[1] << 8); }
void rfmv_kit_write(const rfm_move_t *m, uint8_t *p)    /* RFM_MOVE_KIT_BYTES (version 12) */
{
    const rfm_move_kit_t *t = &m->kit;
    int k, i;
    memset(p, 0, RFM_MOVE_KIT_BYTES);
    p[0] = t->rh, w16(p + 1, t->route), p[3] = t->cur;
    memcpy(p + 4, t->mix, RFM_MOVE_KIT_MIX_N);
    p += 16;
    memcpy(p, t->name, strlen(t->name) < RFM_MOVE_KIT_NAME ? strlen(t->name) : RFM_MOVE_KIT_NAME - 1);
    p += RFM_MOVE_KIT_NAME;
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++) {
        for (i = 0; i < RFM_MOVE_KIT_PATH - 1 && t->pad[k].path[i]; i++)   /* (3c: NUL padded, whatever a longer path
                                                         * left after the NUL in RAM) */
            p[i] = (uint8_t)t->pad[k].path[i];
        p += RFM_MOVE_KIT_PATH;
        for (i = 0; i < RFM_MOVE_KIT_P_N; i++, p += 2)
            w16(p, t->pad[k].v[i]);
    }
}
static int str_ok(const uint8_t *p, int cap)            /* printable, NUL padded */
{
    int j, end = -1;
    for (j = 0; j < cap; j++) {
        if (!p[j] && end < 0)
            end = j;
        else if (end >= 0 && p[j])
            return 0;
        else if (end < 0 && p[j] < 32)
            return 0;
    }
    return end >= 0;
}
int rfmv_kit_check(const uint8_t *p, int ver)           /* a version 11 / 12 kit part valid: 0 */
{
    const int np = ver >= 12 ? RFM_MOVE_KIT_P_N : RFM_MOVE_KIT_P_N_V11;
    int k, i;
    if (p[0] >= RFM_MOVE_KIT_RH_N || (r16s(p + 1) & 0x0FFF) || p[3] >= RFM_MOVE_KIT_PADS)
        return -1;
    if (ver >= 12) {
        for (i = 0; i < RFM_MOVE_KIT_MIX_N; i++)
            if (p[4 + i] > 127)
                return -1;
        for (i = 4 + RFM_MOVE_KIT_MIX_N; i < 16; i++)
            if (p[i])
                return -1;
        p += 16;
        if (!str_ok(p, RFM_MOVE_KIT_NAME))
            return -1;
        p += RFM_MOVE_KIT_NAME;
    } else {
        if (p[4] || p[5] || p[6] || p[7])
            return -1;
        p += 8;
    }
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++) {
        if (!str_ok(p, RFM_MOVE_KIT_PATH))
            return -1;
        p += RFM_MOVE_KIT_PATH;
        for (i = 0; i < np; i++)
            if (r16s(p + 2 * i) < p_min(i) || r16s(p + 2 * i) > p_max(i))
                return -1;
        if (r16s(p + 2 * RFM_MOVE_KIT_P_START) >= r16s(p + 2 * RFM_MOVE_KIT_P_END))
            return -1;
        p += 2 * np;
    }
    return 0;
}
void rfmv_kit_read(rfm_move_t *m, const uint8_t *p, int ver)   /* (checked) */
{
    rfm_move_kit_t *t = K(m);
    const int np = ver >= 12 ? RFM_MOVE_KIT_P_N : RFM_MOVE_KIT_P_N_V11;
    int k, i;
    rfmv_kit_clear(m);
    t->rh = p[0], t->route = (uint16_t)r16s(p + 1), t->cur = p[3];
    if (ver >= 12) {
        memcpy(t->mix, p + 4, RFM_MOVE_KIT_MIX_N);
        p += 16;
        memcpy(t->name, p, RFM_MOVE_KIT_NAME), t->name[RFM_MOVE_KIT_NAME - 1] = 0;
        p += RFM_MOVE_KIT_NAME;
    } else
        p += 8;                                         /* (version 11: no name, the strip at its defaults) */
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++) {
        memcpy(t->pad[k].path, p, RFM_MOVE_KIT_PATH);
        t->pad[k].path[RFM_MOVE_KIT_PATH - 1] = 0;
        p += RFM_MOVE_KIT_PATH;
        for (i = 0; i < np; i++, p += 2)
            t->pad[k].v[i] = (int16_t)r16s(p);
    }
}
static void live_off(rfm_move_t *m);
static void slice_leave(rfm_move_t *m);
static void merge_off(rfm_move_t *m);
/* (project.c) a project loaded (or a new one made): its sounds asked for, its settings sent */
void rfmv_kit_loaded_project(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    live_off(m);
    preview_stop(m);
    t->und = 0, t->undk = 0, t->pend_take = 0, t->tools = 0, t->edit = 0, t->fact = 0, t->saving = 0;
    slice_leave(m), merge_off(m);
    if (t->on && rfmv_kit_active(m))
        rfmv_seq_select(m, trk(t->cur));
    rfm_move_kit_sync(m);
}

/* ================================================================ the view === */
void rfmv_mode_set(rfm_move_t *m, int mode);
static void steps_off(rfm_move_t *m) { K(m)->st_down = 0, K(m)->st_used = 0; }
/* every note the kit's pads play now let go (their KIT_NOTE / MIDI off, the recording's lengths) */
static void live_off(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    int p;
    for (p = 0; p < 32; p++)
        if (t->down >> p & 1) {
            if (t->live[p].pad >= 0) {
                if (t->live[p].kit)
                    note_out(m, t->live[p].pad, t->live[p].semis, 0, t->live[p].track);
                else
                    rfmv_emit_midi_t(m, t->live[p].track + 1, 0x80 | t->live[p].ch, t->live[p].note, 0);
                rfmv_seq_pad_off(m, p, t->live[p].note);
            }
            t->live[p].pad = -1;
        }
    t->down = 0;
}
void rfmv_kit_open(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    rfm_move_seq_t *q = &m->seq;
    if (!rfmv_kit_cap(m))
        return;
    if (q->projects)
        rfmv_view_close(m);                             /* (out of the Mixer / Routing / the projects first) */
    rfmv_seq_menu_close(m);
    if (!t->on) {
        t->prev_mode = m->s.mode, t->prev_track = q->sel_track, t->prev_view = q->view, t->prev_ok = 1;
        t->edit = 0, t->tools = 0, t->picking = 0, t->turned = 0, t->kpop = -1;
    }
    t->on = 1;
    if (m->s.mode != RFM_MOVE_MODE_SEQ)
        rfmv_mode_set(m, RFM_MOVE_MODE_SEQ);
    q->view = 0;
    rfmv_seq_select(m, trk(t->cur));
    if (!m->s.t4_kit)
        m->s.t4_kit = 1, rfmv_changed(m);
    rfmv_pop(m, POP_T4, 2);
    touched(m);
}
/* the view left: restore 1 gives back what it was opened over (Track 4 onto the Mixer), 0 not (a Track 1..3 tap
 * plays its slot's layout itself) */
void rfmv_kit_close(rfm_move_t *m, int restore)
{
    rfm_move_kit_t *t = K(m);
    if (!t->on)
        return;
    live_off(m);
    preview_stop(m);
    steps_off(m);
    slice_leave(m), merge_off(m);
    t->on = 0, t->edit = 0, t->tools = 0, t->picking = 0, t->kpop = -1, t->pend_take = 0;
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
void rfmv_kit_caps(rfm_move_t *m, int was)
{
    if (rfmv_kit_cap(m) && !was) {
        if (!m->seq.proj_used && !m->seq.nnotes && !m->seq.nlocks && !rfmv_kit_used(m))
            rfmv_kit_new(m);                            /* (nothing stored yet: a new project's kit) */
        rfm_move_kit_sync(m);
    } else if (!rfmv_kit_cap(m) && was) {
        m->kit.on = 0;
        if (m->s.t4_kit)
            m->s.t4_kit = 0;
    }
}
void rfmv_kit_init(rfm_move_t *m)                       /* (rfm_move_init) */
{
    int p;
    rfmv_kit_clear(m);
    m->kit.kpop = -1;
    for (p = 0; p < 32; p++)
        m->kit.live[p].pad = -1;
}

/* ---- (the sampler's To Kit) a take waits for its pad: the Kit view, the pads blinking */
void rfmv_kit_to(rfm_move_t *m, uint32_t take)
{
    if (!rfmv_kit_cap(m) || !take)
        return;
    rfmv_kit_open(m);
    m->kit.pend_take = take, m->kit.blink_ms = now(m), m->kit.pend_i = 0;
    m->kit.pend_src = (uint8_t)(m->smp.cur.id == take ? m->smp.cur.src : RFM_MOVE_SMP_SRC_MASTER);
    if (m->kit.sl_on)
        slice_leave(m);
    m->kit.tab = 0, m->kit.on_tabs = 0, m->kit.edit = 0, m->kit.tools = 0;
    msg(m, "To Kit", NULL);
}

/* ---- (move.c) a firmware Track held, the hold on track 13..16: the kit's tracks go to the firmware and back.
 * 1 done (0: not a kit track, the toggle as before) */
int rfmv_kit_route_flip(rfm_move_t *m, int fw, int track)
{
    rfm_move_kit_t *t = K(m);
    if (!rfmv_kit_cap(m) || track < RFM_MOVE_KIT_TRACK0 || track >= RFM_MOVE_SEQ_TRACKS || fw < 0 || fw >= RFM_MOVE_FW_SLOTS)
        return 0;
    if (t->route >> track & 1) {                        /* the kit's: to the firmware */
        t->route = (uint16_t)(t->route & ~(1u << track));
        rfmv_seq_release(m, track);
        rfmv_route_on(m, fw, track);
    } else if (m->seq.fx.ffilt[fw] >> track & 1) {      /* the firmware's: back to the kit */
        rfmv_route_toggle(m, fw, track);
        t->route = (uint16_t)(t->route | 1u << track);
    } else
        return 0;
    rfmv_seq_dirty(m);
    return 1;
}
int rfmv_kit_routed(const rfm_move_t *m, int track)
{
    return rfmv_kit_cap(m) && track >= 0 && track < 16 && (m->kit.route >> track & 1);
}
int rfmv_kit_route_set(rfm_move_t *m, int track, int on)
{
    rfm_move_kit_t *t = K(m);
    int s;
    if (!rfmv_kit_cap(m) || track < RFM_MOVE_KIT_TRACK0 || track >= RFM_MOVE_SEQ_TRACKS)
        return 0;
    if (on && !(t->route >> track & 1)) {
        for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)         /* (the kit's alone, as a held Track's flip makes it) */
            if (m->seq.fx.ffilt[s] >> track & 1)
                rfmv_route_toggle(m, s, track);
        rfmv_seq_release(m, track);
        t->route = (uint16_t)(t->route | 1u << track);
        rfmv_seq_dirty(m);
    } else if (!on && (t->route >> track & 1)) {
        rfmv_seq_release(m, track);
        t->route = (uint16_t)(t->route & ~(1u << track));
        rfmv_seq_dirty(m);
    }
    return (t->route >> track & 1) != 0;
}
rgb_t rfmv_kit_row_rgb(int track) { return ROWC[(track - RFM_MOVE_KIT_TRACK0) & 3]; }

/* ================================================================ the values === */
static int env_idx(int ms)
{
    int i, b = 0;
    for (i = 1; i < NENV; i++)
        if ((ms > ENV_MS[i] ? ms - ENV_MS[i] : ENV_MS[i] - ms) < (ms > ENV_MS[b] ? ms - ENV_MS[b] : ENV_MS[b] - ms))
            b = i;
    return b;
}
static int pitch_step(int v, int d, int fine);
/* setting p of the chosen pad d detents on (fine: Shift) */
static int step_val(const rfm_move_t *m, int k, int p, int d, int fine)
{
    const rfm_move_kit_pad_t *e = CPAD(m, k);
    const int v = e->v[p];
    switch (p) {
    case RFM_MOVE_KIT_P_START:
        return clampi(v + d * (fine ? 4 : 64), 0, e->v[RFM_MOVE_KIT_P_END] - GAP);
    case RFM_MOVE_KIT_P_END:
        return clampi(v + d * (fine ? 4 : 64), e->v[RFM_MOVE_KIT_P_START] + GAP, RFM_MOVE_KIT_FRAC);
    case RFM_MOVE_KIT_P_ATTACK:
    case RFM_MOVE_KIT_P_HOLD:
    case RFM_MOVE_KIT_P_RELEASE: return ENV_MS[clampi(env_idx(v) + d, 0, NENV - 1)];
    case RFM_MOVE_KIT_P_PITCH: return pitch_step(v, d, fine);
    default: return clampi(v + d, p_min(p), p_max(p));
    }
}
/* a pitch d semitones on: whole semitones (a fine value goes to the next whole one first) */
static int pitch_step(int v, int d, int fine)
{
    if (fine)
        return clampi(v + d, -2400, 2400);
    if (v % 100) {
        const int lo = v < 0 ? v - (100 + v % 100) : v - v % 100;   /* (the whole semitone below) */
        v = d > 0 ? lo + 100 : lo;
        d += d > 0 ? -1 : 1;
    }
    return clampi(v + d * 100, -2400, 2400);
}
static void put(rfm_move_t *m, int k, int p, int v)     /* a setting changed: KIT_SET, the project edited */
{
    rfm_move_kit_pad_t *d = PAD(m, k);
    v = clampi(v, p_min(p), p_max(p));
    if (d->v[p] == v)
        return;
    d->v[p] = (int16_t)v;
    send_set(m, k, p);
    rfmv_seq_dirty(m);
}
static void turn_p(rfm_move_t *m, int p, int d, int fine)
{
    const int k = m->kit.cur;
    if (p == RFM_MOVE_KIT_P_PITCH && CPAD(m, k)->v[RFM_MOVE_KIT_P_STRETCH])
        return;                                         /* (3b: stretched, Pitch is ignored: kept as it was) */
    put(m, k, p, step_val(m, k, p, d, fine));
}
/* the region's seconds (ms) */
static int frac_ms(const rfm_move_kit_pad_t *d, int frac)
{
    return d->rate ? (int)((uint64_t)d->frames * (uint64_t)frac * 1000u / ((uint64_t)RFM_MOVE_KIT_FRAC * d->rate)) : -1;
}
/* 2^(cents / 1200) x 65536 (no libm) */
static uint32_t pitch_q16(int cents)
{
    int oct = cents >= 0 ? cents / 1200 : -((-cents + 1199) / 1200);
    const int rem = cents - oct * 1200;                 /* 0..1199 */
    /* 2^x on [0, 1): 1 + x (0.6931 + x (0.2402 + x 0.0555 + x 0.0097)), x = rem / 1200 */
    const double x = rem / 1200.0;
    const double f = 1.0 + x * (0.693147 + x * (0.240227 + x * (0.0555041 + x * 0.00961813)));
    double r = f * 65536.0;
    for (; oct > 0; oct--)
        r *= 2.0;
    for (; oct < 0; oct++)
        r *= 0.5;
    return (uint32_t)(r + 0.5);
}
/* the steps the pad's region lasts on its track (at the tempo, the track's rate, its pitch), 0..16 */
static int region_steps(const rfm_move_t *m, int k)
{
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    const rfm_move_track_t *tr = &m->seq.tr[trk(k)];
    const int ms = frac_ms(d, d->v[RFM_MOVE_KIT_P_END] - d->v[RFM_MOVE_KIT_P_START]);
    double step_ms, len;
    if (ms <= 0 || !d->path[0])
        return 0;
    if (d->v[RFM_MOVE_KIT_P_STRETCH])                   /* (3b) stretched: exactly its steps */
        return clampi(d->v[RFM_MOVE_KIT_P_STRETCH], 0, 16);
    step_ms = rfmv_seq_tps(tr->rate, tr->feel) * 600000.0 / ((double)(m->seq.bpm10 ? m->seq.bpm10 : 1200) * 96.0);
    len = ms * 65536.0 / pitch_q16(d->v[RFM_MOVE_KIT_P_PITCH]);
    return clampi((int)(len / step_ms + 0.5), 0, 16);
}
static void secs(int ms, char *o, int cap)
{
    if (ms < 0)
        snprintf(o, (size_t)cap, "-");
    else if (ms < 10000)
        snprintf(o, (size_t)cap, "%d.%02d s", ms / 1000, ms % 1000 / 10);
    else
        snprintf(o, (size_t)cap, "%d.%d s", ms / 1000, ms % 1000 / 100);
}
static void pitch_text(int c, char *o, int cap)
{
    const int a = c < 0 ? -c : c;
    if (!c)
        snprintf(o, (size_t)cap, "0");
    else if (a % 100)
        snprintf(o, (size_t)cap, "%c%d.%02d", c < 0 ? '-' : '+', a / 100, a % 100);
    else
        snprintf(o, (size_t)cap, "%+d", c / 100);
}
/* a setting's words: row 1 (a row's value: "A 20 ms"), else a popup's ("20 ms"); say 1: spoken */
static void val_text(const rfm_move_t *m, int k, int p, int row, char *o, int cap)
{
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    const int v = d->v[p];
    switch (p) {
    case RFM_MOVE_KIT_P_PITCH:
        if (d->v[RFM_MOVE_KIT_P_STRETCH])
            snprintf(o, (size_t)cap, "Stretch");        /* (3b: the Pitch ignored) */
        else
            pitch_text(v, o, cap);
        break;
    case RFM_MOVE_KIT_P_STRETCH:
        if (!v)
            snprintf(o, (size_t)cap, "Off");
        else
            snprintf(o, (size_t)cap, "%d Step%s", v, v == 1 ? "" : "s");
        break;
    case RFM_MOVE_KIT_P_SEND1:
    case RFM_MOVE_KIT_P_SEND2:
    case RFM_MOVE_KIT_P_SEND3: snprintf(o, (size_t)cap, "%d%%", v); break;
    case RFM_MOVE_KIT_P_START:
    case RFM_MOVE_KIT_P_END:
        if (d->rate)
            secs(frac_ms(d, v), o, cap);
        else
            snprintf(o, (size_t)cap, "%d%%", (int)(v * 100L / RFM_MOVE_KIT_FRAC));
        break;
    case RFM_MOVE_KIT_P_MODE: snprintf(o, (size_t)cap, "%s", MODE_NAME[clampi(v, 0, RFM_MOVE_KIT_M_N - 1)]); break;
    case RFM_MOVE_KIT_P_ATTACK:
    case RFM_MOVE_KIT_P_HOLD:
    case RFM_MOVE_KIT_P_RELEASE:
        if (row)
            snprintf(o, (size_t)cap, "%c %d ms", "AHR"[p - RFM_MOVE_KIT_P_ATTACK], v);
        else
            snprintf(o, (size_t)cap, "%d ms", v);
        break;
    case RFM_MOVE_KIT_P_CHOKE:
        if (v)
            snprintf(o, (size_t)cap, "Group %d", v);
        else
            snprintf(o, (size_t)cap, "Off");
        break;
    case RFM_MOVE_KIT_P_LEVEL:
        if (v <= LEVEL_MIN)
            snprintf(o, (size_t)cap, "-inf dB");
        else
            snprintf(o, (size_t)cap, v > 0 ? "+%d dB" : "%d dB", v);
        break;
    case RFM_MOVE_KIT_P_PAN:
        if (v)
            snprintf(o, (size_t)cap, "%c %d", v < 0 ? 'L' : 'R', v < 0 ? -v : v);
        else
            snprintf(o, (size_t)cap, "C");
        break;
    default: snprintf(o, (size_t)cap, "%s", v ? "On" : "Off"); break;
    }
}
static void sound_text(const rfm_move_t *m, int k, char *o, int cap)
{
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    switch (d->st) {
    case RFM_MOVE_KIT_LOADING: snprintf(o, (size_t)cap, "Loading"); break;
    case RFM_MOVE_KIT_MISSING: snprintf(o, (size_t)cap, "Missing"); break;
    case RFM_MOVE_KIT_FAILED: snprintf(o, (size_t)cap, "Failed"); break;
    case RFM_MOVE_KIT_TOO_LONG: snprintf(o, (size_t)cap, "Too Long"); break;
    default: snprintf(o, (size_t)cap, "%s", d->path[0] ? (d->name[0] ? d->name : "Sound") : "Empty"); break;
    }
}

/* ================================================================ the right half === */
/* a track's key: the selected track's is in the settings (the sequencer's mirror); else its own, or the project's */
static void key_of(const rfm_move_t *m, int tr, int *root, int *scale)
{
    const rfm_move_seq_t *q = &m->seq;
    if (q->in_seq && q->sel_track == tr)
        *root = m->s.root, *scale = m->s.scale;
    else if (q->tr[tr].own & 2)
        *root = q->tr[tr].root, *scale = q->tr[tr].scale;
    else
        *root = q->root, *scale = q->scale;
    *root %= 12, *scale %= RFM_MOVE_NSCALES;
}
/* right-half pad n (0..15): its semitones over the chosen pad's pitch, -1 dead */
int rfmv_kit_right_semis(const rfm_move_t *m, int n)
{
    const int tr = trk(m->kit.cur);
    if (n < 0 || n > 15)
        return -1;
    if (m->kit.rh == RFM_MOVE_KIT_RH_PITCHES)
        return n;
    if (m->kit.rh == RFM_MOVE_KIT_RH_INKEY) {           /* rows a fourth apart, four notes of the scale a row (the
                                                         * project's key, or the track's own) */
        int scale, root;
        key_of(m, tr, &root, &scale);
        const uint8_t *s = rfmv_scales[scale < RFM_MOVE_NSCALES ? scale : 0];
        const int deg = n % 4 + (n / 4) * 3, v = 12 * (deg / s[0]) + s[1 + deg % s[0]];
        return v <= RFM_MOVE_KIT_SEMIS ? v : -1;
    }
    return -1;
}
static int right_root(const rfm_move_t *m, int n)      /* the pad's own note or its octave (Pitches), a root (In Key) */
{
    if (m->kit.rh == RFM_MOVE_KIT_RH_PITCHES)
        return n % 12 == 0;
    {
        int scale, root;
        const uint8_t *s;
        key_of(m, trk(m->kit.cur), &root, &scale);
        s = rfmv_scales[scale < RFM_MOVE_NSCALES ? scale : 0];
        return (n % 4 + (n / 4) * 3) % s[0] == 0;
    }
}

/* ================================================================ the steps === */
static int edit_clip_of(const rfm_move_t *m, int track) { return SEQ_CLIP(track, m->seq.tr[track].sel); }
/* the pad's notes at step abs of its track's edited clip: how many */
static int step_has(const rfm_move_t *m, int k, int abs)
{
    const int tr = trk(k), clip = edit_clip_of(m, tr);
    int lo, hi = rfmv_seq_range(m, clip, abs, &lo), i, n = 0;
    for (i = lo; i < hi; i++)
        n += rfm_move_kit_pad_of(tr, m->seq.notes[i].note, NULL) == k;
    return n;
}
static void step_clear(rfm_move_t *m, int k, int abs)
{
    const int tr = trk(k), clip = edit_clip_of(m, tr);
    int lo, hi = rfmv_seq_range(m, clip, abs, &lo);
    while (hi-- > lo)
        if (rfm_move_kit_pad_of(tr, m->seq.notes[hi].note, NULL) == k)
            rfmv_seq_note_del(m, clip, abs, m->seq.notes[hi].note);
}
/* a pad's own note in or out of step s of the page shown (one undo step) */
static void step_toggle(rfm_move_t *m, int k, int s, int vel)
{
    const int tr = trk(k), clip = edit_clip_of(m, tr), abs = m->seq.page * RFM_MOVE_SEQ_PAGE + s;
    rfmv_seq_group(m);
    if (step_has(m, k, abs))
        step_clear(m, k, abs);
    else
        rfmv_seq_step_enter(m, clip, abs, rfm_move_kit_note(k, 0), vel);
    rfmv_seq_dirty(m);
}

/* ================================================================ the pads === */
static void choose(rfm_move_t *m, int k)
{
    rfm_move_kit_t *t = K(m);
    if (t->cur != k) {
        t->cur = (uint8_t)k;
        t->tools = 0;
        if (t->edit && t->tab == 0 && t->row == KR_SOUND)
            t->edit = 0;
    }
    rfmv_seq_select(m, trk(k));
}
/* the pad's undo: what an emptied pad was */
static void und_keep(rfm_move_t *m, int k)
{
    rfm_move_kit_t *t = K(m);
    t->und = (uint8_t)(k + 1), t->und_pad = t->pad[k], t->undk = 0;
    t->und_un = m->seq.un, t->und_utop = m->seq.utop;
}
/* (3b) the whole kit's undo: what a folder or a kit loaded replaced */
static void undk_keep(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    memcpy(t->undk_pad, t->pad, sizeof t->pad);
    t->undk_rh = t->rh;
    memcpy(t->undk_mix, t->mix, sizeof t->mix);
    snprintf(t->undk_name, sizeof t->undk_name, "%s", t->name);
    t->undk = 1, t->und = 0;
    t->undk_un = m->seq.un, t->undk_utop = m->seq.utop;
}
static int undk_ours(const rfm_move_t *m)
{
    const rfm_move_kit_t *t = &m->kit;
    return t->undk && t->undk_un == m->seq.un && t->undk_utop == m->seq.utop;
}
static void undo_kit(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    int k;
    memcpy(t->pad, t->undk_pad, sizeof t->pad);
    t->rh = t->undk_rh;
    memcpy(t->mix, t->undk_mix, sizeof t->mix);
    snprintf(t->name, sizeof t->name, "%s", t->undk_name);
    t->undk = 0;
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++)
        send_pad(m, k);
    send_mix(m);
    rfmv_seq_dirty(m);
    msg(m, "Undo", NULL);
}
/* every pad emptied to its defaults, loading (a folder's sounds or a kit's come) */
static void pads_reset(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    int k, p;
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++) {
        rfm_move_kit_pad_t *d = &t->pad[k];
        memset(d, 0, sizeof *d);
        defaults(d->v);
        d->st = RFM_MOVE_KIT_LOADING, d->fresh = 1;
        for (p = 0; p < RFM_MOVE_KIT_P_N; p++)
            send_set(m, k, p);
    }
}
/* (3b, board K) Shift + the click on a folder, All To Pads: its first 16 sounds onto pads 1..16 (the host's answers) */
static void all_to_pads(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    undk_keep(m);
    pads_reset(m);
    t->name[0] = 0, t->fact = 0;
    act(m, RFM_MOVE_ACT_KIT_FOLDER, 0, 0, 0, t->place, t->fact_id, 0, 0, NULL);
    rfmv_seq_dirty(m);
    msg(m, "All To Pads", t->fact_name);
}
static int und_ours(const rfm_move_t *m)               /* the pad's undo is the latest (no sequencer edit since) */
{
    const rfm_move_kit_t *t = &m->kit;
    return t->und && t->und_un == m->seq.un && t->und_utop == m->seq.utop;
}
static void empty_pad(rfm_move_t *m, int k)
{
    rfm_move_kit_pad_t *d = PAD(m, k);
    und_keep(m, k);
    d->path[0] = 0, d->name[0] = 0, d->frames = 0, d->rate = 0, d->npk = 0, d->st = RFM_MOVE_KIT_EMPTY;
    act(m, RFM_MOVE_ACT_KIT_LOAD, k, 0, 0, 0, 0, 0, 0, NULL);
    rfmv_seq_dirty(m);
    msg(m, "Emptied", NULL);
}
static void undo_pad(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    const int k = t->und - 1;
    t->pad[k] = t->und_pad;
    t->und = 0;
    send_pad(m, k);
    rfmv_seq_dirty(m);
    msg(m, "Undo", NULL);
}
/* Delete held + a pad: its steps on its track (the edited clip) go first; a pad with none is emptied */
static void pad_delete(rfm_move_t *m, int k)
{
    const int tr = trk(k), clip = edit_clip_of(m, tr);
    int lo, hi = rfmv_seq_clip_range(m, clip, &lo), any = 0;
    while (hi-- > lo)
        if (rfm_move_kit_pad_of(tr, m->seq.notes[hi].note, NULL) == k) {
            if (!any)
                rfmv_seq_group(m), any = 1;
            rfmv_seq_note_del(m, clip, m->seq.notes[hi].step, m->seq.notes[hi].note);
        }
    if (any) {
        rfmv_seq_dirty(m);
        msg(m, "Steps", "Deleted");
    } else if (has_sound(CPAD(m, k)))
        empty_pad(m, k);
}
/* ================================================================ (3c) making sounds === */
/* the frames pad k's region lasts (0: no sound) */
static uint32_t region_frames(const rfm_move_kit_pad_t *d)
{
    const int span = d->v[RFM_MOVE_KIT_P_END] - d->v[RFM_MOVE_KIT_P_START];
    return d->frames && span > 0 ? (uint32_t)((uint64_t)d->frames * (uint32_t)span / RFM_MOVE_KIT_FRAC) : 0;
}
static int ready(const rfm_move_kit_pad_t *d) { return d->path[0] && d->st == RFM_MOVE_KIT_OK && d->frames; }
/* Copy held + a pad, then another: the menu (board N) while Copy stays held */
static int merge_up(const rfm_move_t *m) { return m->kit.mg_a && (m->seq.mods & SEQ_M_COPY); }
static void merge_off(rfm_move_t *m) { K(m)->mg_a = 0, K(m)->mg_b = 0, K(m)->mg_i = 0; }
/* the chosen one done: Copy (the first's sound and settings onto the second), Mix or Join (a new file from the host,
 * KIT_MERGE); the second pad shows on the Pad tab; Undo brings it back */
static void merge_do(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    const int a = t->mg_a - 1, b = t->mg_b - 1, op = t->mg_i;
    const rfm_move_kit_pad_t *s = CPAD(m, a);
    rfm_move_kit_pad_t *d = PAD(m, b);
    char w[16];
    merge_off(m);
    if (a < 0 || b < 0 || a == b)
        return;
    if (!ready(s) || (op != RFM_MOVE_KIT_MG_COPY && !ready(d))) {
        msg(m, "Empty", NULL);                          /* (Copy wants a sound on the first; Mix, Join on both) */
        return;
    }
    und_keep(m, b);
    snprintf(w, sizeof w, "%d+%d", a + 1, b + 1);
    if (op == RFM_MOVE_KIT_MG_COPY) {
        *d = *s;
        send_pad(m, b);                                 /* (its path again: the region and settings as the first's) */
        msg(m, "Copied", w);
    } else {
        const uint32_t la = region_frames(s);
        const uint64_t lb = d->rate && s->rate ? (uint64_t)region_frames(d) * s->rate / d->rate : region_frames(d);
        d->st = RFM_MOVE_KIT_LOADING, d->fresh = 0;
        d->keep = op == RFM_MOVE_KIT_MG_JOIN ? 3 : 0;
        d->cut = op == RFM_MOVE_KIT_MG_JOIN && la + lb ? (uint16_t)((uint64_t)la * RFM_MOVE_KIT_FRAC / (la + lb)) : 0;
        act(m, RFM_MOVE_ACT_KIT_MERGE, b, 0, 0, op, (uint32_t)a, 0, 0, NULL);
        msg(m, MG_NAME[op], w);
    }
    choose(m, b);
    t->tab = 0, t->on_tabs = 0, t->picking = 0, t->edit = 0, t->tools = 0, t->row = KR_SOUND, t->turned = 0;
    rfmv_seq_dirty(m);
}
/* the Sound row's tools: Trim, Normalize, Reverse a new file (the pad's own copy, KIT_EDIT); Undo brings it back */
static void pad_edit(rfm_move_t *m, int k, int tool)
{
    rfm_move_kit_pad_t *d = PAD(m, k);
    char pn[8];
    if (!ready(d))
        return;
    und_keep(m, k);
    d->st = RFM_MOVE_KIT_LOADING, d->fresh = 0;
    d->keep = (uint8_t)(tool == TL_NORM ? 1 : tool == TL_REV ? 2 : 0);
    act(m, RFM_MOVE_ACT_KIT_EDIT, k, 0, 0,
        tool == TL_TRIM ? RFM_MOVE_KIT_ED_TRIM : tool == TL_NORM ? RFM_MOVE_KIT_ED_NORMALIZE : RFM_MOVE_KIT_ED_REVERSE, 0, 0,
        0, NULL);
    rfmv_seq_dirty(m);
    snprintf(pn, sizeof pn, "Pad %d", k + 1);
    msg(m, TOOL_DONE[tool], pn);
}
/* ---- a take sliced over the pads (board P): To Kit, then Slice */
static void slice_act(rfm_move_t *m, int op, uint32_t from, uint32_t count)
{
    rfm_move_kit_t *t = K(m);
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_KIT_SLICE, a.param = (uint8_t)op, a.take = t->sl_take, a.value = t->sl_meth;
    a.from = from, a.count = count;
    if (m->fn)
        m->fn(m->ctx, &a);
}
static void slice_ask(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    slice_act(m, RFM_MOVE_KIT_SL_ASK, step_us(m, trk(t->sl_first)), 0);
}
static void slice_open(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    t->sl_on = 1, t->sl_ok = 0, t->sl_n = 0, t->sl_cur = 0, t->sl_npk = 0;
    t->sl_take = t->pend_take, t->sl_meth = RFM_MOVE_KIT_SM_HITS, t->sl_first = t->cur;
    t->pend_take = 0, t->kpop = -1;
    slice_ask(m);
}
static void slice_leave(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    if (!t->sl_on)
        return;
    slice_act(m, RFM_MOVE_KIT_SL_DONE, 0, 0);
    t->sl_on = 0, t->sl_ok = 0, t->kpop = -1;
}
static int slice_count(const rfm_move_t *m)              /* the slices that go onto the pads */
{
    const rfm_move_kit_t *t = &m->kit;
    const int room = RFM_MOVE_KIT_PADS - t->sl_first;
    return t->sl_n < room ? t->sl_n : room;
}
static void slice_play(rfm_move_t *m, int i)
{
    const rfm_move_kit_t *t = &m->kit;
    if (t->sl_ok == 1 && i >= 0 && i < t->sl_n)
        slice_act(m, RFM_MOVE_KIT_SL_PLAY, t->sl_cut[i], t->sl_cut[i + 1] - t->sl_cut[i]);
}
static int frac_of(const rfm_move_kit_t *t, uint32_t f)
{
    return t->sl_frames ? (int)((uint64_t)(f > t->sl_frames ? t->sl_frames : f) * RFM_MOVE_KIT_FRAC / t->sl_frames) : 0;
}
/* the click: the slices onto pads first.. (each its region of the take, one file); one Undo brings the kit back */
static void slice_apply(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    const int n = slice_count(m);
    char w[16];
    int i;
    if (t->sl_ok != 1 || n <= 0 || !t->sl_path[0])
        return;
    undk_keep(m);
    live_off(m);
    for (i = 0; i < n; i++) {
        const int k = t->sl_first + i;
        rfm_move_kit_pad_t *d = &t->pad[k];
        int a = frac_of(t, t->sl_cut[i]), b = frac_of(t, t->sl_cut[i + 1]);
        if (b - a < GAP)
            b = a + GAP > RFM_MOVE_KIT_FRAC ? RFM_MOVE_KIT_FRAC : a + GAP, a = b - GAP;
        memset(d, 0, sizeof *d);
        defaults(d->v);
        snprintf(d->path, sizeof d->path, "%s", t->sl_path);
        d->v[RFM_MOVE_KIT_P_START] = (int16_t)a, d->v[RFM_MOVE_KIT_P_END] = (int16_t)b;
        d->keep = 1;
        send_pad(m, k);
    }
    if (n == 1)
        snprintf(w, sizeof w, "Pad %d", t->sl_first + 1);
    else
        snprintf(w, sizeof w, "Pads %d-%d", t->sl_first + 1, t->sl_first + n);
    slice_leave(m);
    choose(m, t->sl_first);
    t->tab = 0, t->on_tabs = 0, t->edit = 0, t->tools = 0;
    rfmv_seq_dirty(m);
    msg(m, "Sliced", w);
}
/* knob 1 (coarse: a column of the wave) and 2 (fine: a millisecond) move the chosen slice's cut */
static void cut_move(rfm_move_t *m, int d, int fine)
{
    rfm_move_kit_t *t = K(m);
    const int i = t->sl_cur;
    const int64_t gap = t->sl_rate / 100 > 0 ? t->sl_rate / 100 : 1;   /* (10 ms between two cuts at least) */
    int64_t step = fine ? t->sl_rate / 1000 : t->sl_frames / 128, v, lo, hi;
    if (t->sl_ok != 1 || i >= t->sl_n)
        return;
    if (step < 1)
        step = 1;
    lo = i ? (int64_t)t->sl_cut[i - 1] + gap : 0;
    hi = (int64_t)t->sl_cut[i + 1] - gap;
    v = (int64_t)t->sl_cut[i] + d * step;
    v = v < lo ? lo : v > hi ? hi : v;
    if (hi >= lo)
        t->sl_cut[i] = (uint32_t)v;
}
int rfm_move_kit_slices(rfm_move_t *m, uint32_t take, const char *path, uint32_t frames, uint32_t rate, int n,
                        const uint32_t *cuts, int npk, const uint8_t *peaks)
{
    rfm_move_kit_t *t = K(m);
    int i, ok = 1;
    if (!rfmv_kit_cap(m) || !t->sl_on || take != t->sl_take)
        return 0;                                       /* (an older ask's, or the slicing left) */
    if (n > RFM_MOVE_KIT_SLICES)
        n = RFM_MOVE_KIT_SLICES;
    ok = n > 0 && frames && cuts && path && path[0] && strlen(path) < RFM_MOVE_KIT_PATH;
    for (i = 0; ok && i < n; i++)
        ok = cuts[i] < cuts[i + 1] && cuts[i + 1] <= frames;
    if (!ok) {
        t->sl_ok = 2, t->sl_n = 0;
        msg(m, "Not Sliced", NULL);
        rfmv_say_check(m);
        return rfm_move_kit_shown(m);
    }
    t->sl_ok = 1, t->sl_n = (uint8_t)n, t->sl_frames = frames, t->sl_rate = rate ? rate : 48000;
    for (i = 0; i <= n; i++)
        t->sl_cut[i] = cuts[i];
    snprintf(t->sl_path, sizeof t->sl_path, "%s", path);
    t->sl_npk = (uint8_t)(peaks && npk > 0 ? (npk > RFM_MOVE_KIT_PEAKS ? RFM_MOVE_KIT_PEAKS : npk) : 0);
    if (t->sl_npk)
        memcpy(t->sl_pk, peaks, t->sl_npk);
    if (t->sl_cur >= t->sl_n)
        t->sl_cur = (uint8_t)(t->sl_n - 1);
    touched(m);
    rfmv_say_check(m);
    return rfm_move_kit_shown(m);
}
/* a waiting take's click: To Pad (onto the chosen pad, as a pad's press) or Slice */
static void pend_click(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    if (t->pend_i) {
        slice_open(m);
        return;
    }
    {
        rfm_move_kit_pad_t *d = PAD(m, t->cur);
        d->st = RFM_MOVE_KIT_LOADING, d->fresh = 1, d->keep = 0, d->name[0] = 0;
        act(m, RFM_MOVE_ACT_KIT_LOAD, t->cur, 0, 0, RFM_MOVE_KIT_PL_TAKES, t->pend_take, 0, 0, NULL);
        t->pend_take = 0;
        rfmv_seq_dirty(m);
    }
}
static void pad_down(rfm_move_t *m, int p, int vel)
{
    rfm_move_kit_t *t = K(m);
    rfm_move_seq_t *q = &m->seq;
    const int kp = kit_of(p), rp = right_of(p);
    int k, semis, s;
    t->down |= 1u << p;
    t->live[p].pad = -1;
    touched(m);
    if (t->sl_on) {                                     /* (3c, board P) the slices: a pad plays its slice and chooses
                                                         * it; Shift + a pad: the first pad they go onto */
        if (kp < 0)
            return;
        if (q->mods & SEQ_M_SHIFT) {
            q->mods_used |= SEQ_M_SHIFT;
            t->sl_first = (uint8_t)kp;
        } else if (kp < t->sl_n) {
            t->sl_cur = (uint8_t)kp;
            slice_play(m, kp);
        }
        return;
    }
    if (kp >= 0 && t->pend_take) {                      /* (the sampler's To Kit) the take onto this pad */
        choose(m, kp);
        PAD(m, kp)->st = RFM_MOVE_KIT_LOADING, PAD(m, kp)->fresh = 1, PAD(m, kp)->name[0] = 0;
        act(m, RFM_MOVE_ACT_KIT_LOAD, kp, 0, 0, RFM_MOVE_KIT_PL_TAKES, t->pend_take, 0, 0, NULL);
        t->pend_take = 0;
        rfmv_seq_dirty(m);
        return;
    }
    if (q->mods & SEQ_M_COPY) {                         /* (3c, board N) Copy + a pad, then another: Copy / Mix /
                                                         * Join (the jog chooses, the click does it) */
        q->mods_used |= SEQ_M_COPY;
        if (kp < 0)
            return;
        if (!t->mg_a || kp + 1 == t->mg_a)
            t->mg_a = (uint8_t)(kp + 1), t->mg_b = 0, t->mg_ms = now(m);
        else
            t->mg_b = (uint8_t)(kp + 1);
        t->tools = 0, t->edit = 0, t->kpop = -1;
        return;
    }
    if (kp < 0 && t->tools)
        return;                                         /* (3c, board V: the right half dark while the tools show) */
    if (kp >= 0 && (q->mods & SEQ_M_DELETE)) {
        q->mods_used |= SEQ_M_DELETE;
        pad_delete(m, kp);
        return;
    }
    if (kp >= 0 && ((q->mods & SEQ_M_MUTE) || rfmv_seq_mute_on(m))) {   /* Mute + a pad: muted (grey), saved */
        q->mods_used |= SEQ_M_MUTE;
        put(m, kp, RFM_MOVE_KIT_P_MUTE, !CPAD(m, kp)->v[RFM_MOVE_KIT_P_MUTE]);
        msg(m, CPAD(m, kp)->v[RFM_MOVE_KIT_P_MUTE] ? "Muted" : "Unmuted", NULL);
        return;
    }
    if (kp >= 0 && (q->mods & SEQ_M_SHIFT)) {           /* Shift + a pad: chosen, silent */
        q->mods_used |= SEQ_M_SHIFT;
        choose(m, kp);
        return;
    }
    if (kp >= 0) {
        k = kp, semis = 0;
        choose(m, k);
        t->blink_ms = now(m);
    } else {
        k = t->cur;
        if ((semis = rfmv_kit_right_semis(m, rp)) < 0)
            return;                                     /* (Off, or past the pitches a note can carry: dead) */
    }
    if (t->st_down) {                                   /* a step held: the pad's note in or out of it */
        for (s = 0; s < RFM_MOVE_NSTEPS; s++)
            if (t->st_down >> s & 1) {
                const int tr = trk(k), clip = edit_clip_of(m, tr), abs = q->page * RFM_MOVE_SEQ_PAGE + s;
                const int n = rfm_move_kit_note(k, semis);
                rfmv_seq_group(m);
                if (rfmv_seq_find(m, clip, abs, n) >= 0)
                    rfmv_seq_note_del(m, clip, abs, n);
                else
                    rfmv_seq_step_enter(m, clip, abs, n, vel);
                rfmv_seq_dirty(m);
            }
        t->st_used |= t->st_down;
    }
    {                                                   /* it sounds (the kit's, or the firmware the track reaches) */
        const int tr = trk(k), n = rfm_move_kit_note(k, semis);
        t->live[p].pad = (int8_t)k, t->live[p].semis = (int8_t)semis, t->live[p].track = (uint8_t)tr;
        t->live[p].ch = q->tr[tr].ch, t->live[p].note = (uint8_t)n;
        t->live[p].kit = (uint8_t)(t->route >> tr & 1);
        if (t->live[p].kit)
            note_out(m, k, semis, vel, tr);
        else
            rfmv_emit_midi_t(m, tr + 1, 0x90 | q->tr[tr].ch, n, vel);
        if (!t->st_down)
            rfmv_seq_pad_on(m, p, n, vel);              /* (recorded with Record, kept for Capture) */
    }
}
static void pad_up(rfm_move_t *m, int p)
{
    rfm_move_kit_t *t = K(m);
    t->down &= ~(1u << p);
    if (t->live[p].pad >= 0) {
        if (t->live[p].kit)
            note_out(m, t->live[p].pad, t->live[p].semis, 0, t->live[p].track);
        else
            rfmv_emit_midi_t(m, t->live[p].track + 1, 0x80 | t->live[p].ch, t->live[p].note, 0);
        rfmv_seq_pad_off(m, p, t->live[p].note);
    }
    if (t->rec_held && kit_of(p) == t->rec_pad - 1) {   /* the pad recorded into let go: the take ends (armed: it
                                                         * is dropped, nothing comes for the pad) */
        t->rec_held = 0;
        act(m, RFM_MOVE_ACT_KIT_REC, t->rec_pad - 1, 0, 0, m->s.smp[RFM_MOVE_SMP_MODE], 0, 0, 0, NULL);
        if (m->smp.st.state == RFM_MOVE_SMP_ST_ARMED)
            t->rec_pad = 0;
    }
    t->live[p].pad = -1;
}
static int held_kit_pad(const rfm_move_t *m)           /* a left-half pad held (the latest), -1 */
{
    int p, k = -1;
    for (p = 0; p < 32; p++)
        if ((m->kit.down >> p & 1) && kit_of(p) >= 0)
            k = kit_of(p);
    return k;
}
static int rec_running(const rfm_move_t *m)
{
    return m->kit.rec_pad && m->smp.st.state != RFM_MOVE_SMP_ST_IDLE;
}

/* ================================================================ Files' rows === */
static int first_of(int sel, int total) { return total <= 3 ? 0 : clampi(sel - 1, 0, total - 3); }
static const rfm_move_kit_row_t *row_get(const rfm_move_t *m, int pl, uint32_t f, int i)
{
    int k;
    for (k = 0; k < RFM_MOVE_KIT_CACHE; k++)
        if (m->kit.rc[k].ok && m->kit.rc[k].pl == pl && m->kit.rc[k].f == f && m->kit.rc[k].i == i)
            return &m->kit.rc[k].r;
    return NULL;
}
static void rows_forget(rfm_move_t *m)
{
    int k;
    for (k = 0; k < RFM_MOVE_KIT_CACHE; k++)
        m->kit.rc[k].ok = 0;
    m->kit.ask_i = NOASK, m->kit.known = 0, m->kit.total = 0;
}
static int in_list(const rfm_move_t *m) { return m->kit.tab == 2 && m->kit.place && !m->kit.on_tabs && !m->kit.picking; }
static void need(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    const uint32_t f = t->folder[t->depth];
    const int sel = t->fsel[t->depth];
    int first, i, miss = 0;
    if (!in_list(m) || !rfmv_kit_active(m))
        return;
    if (t->known && !t->total)
        return;
    first = t->known ? first_of(sel, t->total) : clampi(sel - 1, 0, 0x7FFF);
    for (i = first; i < first + 3 && (!t->known || i < t->total); i++)
        miss |= !row_get(m, t->place, f, i);
    if (!miss || (t->ask_pl == t->place && t->ask_f == f && t->ask_i == first))
        return;
    t->ask_pl = t->place, t->ask_f = f, t->ask_i = (uint16_t)first;
    act(m, RFM_MOVE_ACT_KIT_ROWS, 0, 0, 0, t->place, f, (uint32_t)first, 3, NULL);
}
int rfm_move_kit_rows(rfm_move_t *m, int place, uint32_t folder, int total, int from, int n, const rfm_move_kit_row_t *rows)
{
    rfm_move_kit_t *t = K(m);
    int i;
    if (!rfmv_kit_cap(m) || total < 0 || from < 0)
        return 0;
    if (t->ask_pl == place && t->ask_f == folder)
        t->ask_i = NOASK;
    if (place != t->place || folder != t->folder[t->depth])
        return 0;                                       /* (an older ask's) */
    t->total = (uint16_t)clampi(total, 0, 0x7FFF), t->known = 1;
    t->fsel[t->depth] = (int16_t)clampi(t->fsel[t->depth], 0, t->total ? t->total - 1 : 0);
    for (i = 0; rows && i < n && from + i < t->total; i++) {
        int k, v = -1;
        for (k = 0; k < RFM_MOVE_KIT_CACHE && v < 0; k++)
            if (t->rc[k].ok && t->rc[k].pl == place && t->rc[k].f == folder && t->rc[k].i == from + i)
                v = k;
        for (k = 0; k < RFM_MOVE_KIT_CACHE && v < 0; k++)
            if (!t->rc[k].ok)
                v = k;
        if (v < 0) {                                    /* (one the screen does not show) */
            const int c0 = first_of(t->fsel[t->depth], t->total);
            for (k = 0; k < RFM_MOVE_KIT_CACHE && v < 0; k++)
                if (!(t->rc[k].pl == place && t->rc[k].f == folder && t->rc[k].i >= c0 && t->rc[k].i < c0 + 3))
                    v = k;
            if (v < 0)
                v = 0;
        }
        t->rc[v].r = rows[i], t->rc[v].r.name[RFM_MOVE_KIT_NAME - 1] = 0;
        t->rc[v].f = folder, t->rc[v].i = (int16_t)(from + i), t->rc[v].pl = (uint8_t)place, t->rc[v].ok = 1;
    }
    need(m);
    rfmv_say_check(m);
    return rfm_move_kit_shown(m) && in_list(m);
}
/* the row the jog is on: heard (a sound), once */
static void preview(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    const rfm_move_kit_row_t *r = row_get(m, t->place, t->folder[t->depth], t->fsel[t->depth]);
    if (!r || r->kind != RFM_MOVE_KIT_ROW_FILE) {
        preview_stop(m);
        return;
    }
    if (t->prev_id == r->id && t->prev_pl == t->place)
        return;
    t->prev_id = r->id, t->prev_pl = t->place;
    act(m, RFM_MOVE_ACT_KIT_PREVIEW, 0, 1, 0, t->place, r->id, 0, 0, NULL);
}
int rfm_move_kit_loaded(rfm_move_t *m, int k, int status, const char *name, const char *path, uint32_t frames,
                        uint32_t rate, int n, const uint8_t *peaks)
{
    rfm_move_kit_pad_t *d;
    int i;
    if (!rfmv_kit_cap(m) || k < 0 || k >= RFM_MOVE_KIT_PADS)
        return 0;
    d = PAD(m, k);
    if (m->kit.rec_pad == k + 1)
        m->kit.rec_pad = 0, m->kit.rec_held = 0;
    if (status == RFM_MOVE_KIT_EMPTY) {                 /* (3b: All To Pads past the folder's sounds) the pad emptied */
        if (d->path[0])
            rfmv_seq_dirty(m);
        d->path[0] = 0, d->name[0] = 0, d->frames = 0, d->rate = 0, d->npk = 0, d->fresh = 0, d->st = RFM_MOVE_KIT_EMPTY;
        touched(m);
        return rfm_move_kit_shown(m);
    }
    if (status == RFM_MOVE_KIT_OK) {
        if (path && path[0]) {
            if (strlen(path) >= RFM_MOVE_KIT_PATH) {    /* (the project could not keep it) */
                d->st = RFM_MOVE_KIT_FAILED;
                msg(m, "Too Long", "Path");
                return rfm_move_kit_shown(m);
            }
            if (strcmp(d->path, path)) {                /* (3b: a sound the host put there itself, a take: its whole;
                                                         * 3c: an edit's file keeps or mirrors the region) */
                d->fresh = d->keep != 1 && d->keep != 2;
                memset(d->path, 0, sizeof d->path);
                snprintf(d->path, sizeof d->path, "%s", path), rfmv_seq_dirty(m);
                if (d->keep != 3)
                    d->cut = 0;                         /* (a join's seam: its own file's only) */
            }
        }
        snprintf(d->name, sizeof d->name, "%s", name ? name : "");
        for (i = 0; d->name[i]; i++)
            if (d->name[i] < 32 || d->name[i] > 126)
                d->name[i] = '?';
        d->frames = frames, d->rate = rate, d->st = RFM_MOVE_KIT_OK;
        d->npk = (uint8_t)(peaks && n > 0 ? (n > RFM_MOVE_KIT_PEAKS ? RFM_MOVE_KIT_PEAKS : n) : 0);
        if (d->npk)
            memcpy(d->pk, peaks, d->npk);
        if (d->fresh) {                                 /* (a new sound: its whole region) */
            d->fresh = 0;
            put(m, k, RFM_MOVE_KIT_P_START, 0), put(m, k, RFM_MOVE_KIT_P_END, RFM_MOVE_KIT_FRAC);
        } else if (d->keep == 2) {                      /* (3c: reversed, the region where it went) */
            const int a = d->v[RFM_MOVE_KIT_P_START], b = d->v[RFM_MOVE_KIT_P_END];
            put(m, k, RFM_MOVE_KIT_P_START, RFM_MOVE_KIT_FRAC - b), put(m, k, RFM_MOVE_KIT_P_END, RFM_MOVE_KIT_FRAC - a);
        }
        d->keep = 0;
    } else if ((d->fresh || d->keep) && d->frames) {    /* (a new sound refused: the pad keeps the one it had) */
        d->fresh = 0, d->keep = 0, d->st = RFM_MOVE_KIT_OK;
        msg(m, status == RFM_MOVE_KIT_TOO_LONG ? "Too Long" : "Not Loaded", NULL);
    } else {
        d->st = (uint8_t)(status == RFM_MOVE_KIT_TOO_LONG || status == RFM_MOVE_KIT_MISSING ? status : RFM_MOVE_KIT_FAILED);
        d->frames = 0, d->rate = 0, d->npk = 0, d->fresh = 0, d->keep = 0;
        msg(m, status == RFM_MOVE_KIT_TOO_LONG ? "Too Long" : status == RFM_MOVE_KIT_MISSING ? "Missing" : "Not Loaded",
            NULL);
    }
    touched(m);
    rfmv_say_check(m);
    return rfm_move_kit_shown(m);
}

/* ================================================================ the jog, the click, Back === */
static void files_turn(rfm_move_t *m, int d)
{
    rfm_move_kit_t *t = K(m);
    if (!t->place) {                                    /* the places */
        if (t->psel + d < 0) {
            t->on_tabs = 1, t->psel = 0;
            return;
        }
        t->psel = (uint8_t)clampi(t->psel + d, 0, PL_N - 1);
        return;
    }
    {
        int16_t *sel = &t->fsel[t->depth];
        if (*sel + d < 0 && !t->depth) {
            preview_stop(m);
            t->on_tabs = 1, *sel = 0;
            return;
        }
        *sel = (int16_t)clampi(*sel + d, 0, t->total > 0 ? t->total - 1 : 0);
        need(m);
        preview(m);
    }
}
static void turn(rfm_move_t *m, int d)
{
    rfm_move_kit_t *t = K(m);
    t->msg_ms = 0;
    touched(m);
    t->kpop = -1;
    if (rec_running(m))
        return;
    if (merge_up(m)) {                                  /* (3c, board N) Copy, Mix, Join */
        if (t->mg_b)
            t->mg_i = (uint8_t)clampi(t->mg_i + d, 0, RFM_MOVE_KIT_MG_N - 1);
        return;
    }
    if (t->sl_on) {                                     /* (3c, board P) the method: the cuts asked again at once */
        const int was = t->sl_meth;
        t->sl_meth = (uint8_t)clampi(t->sl_meth + d, 0, RFM_MOVE_KIT_SM_N - 1);
        if (t->sl_meth != was)
            slice_ask(m);
        return;
    }
    if (t->pend_take) {                                 /* (3c) a take waiting: To Pad, Slice */
        t->pend_i = (uint8_t)clampi(t->pend_i + d, 0, 1);
        return;
    }
    if (t->tools) {
        t->ti = (uint8_t)clampi(t->ti + d, 0, TL_N - 1);
        return;
    }
    if (t->fact) {                                      /* (3b) a folder's actions: Open, All To Pads */
        t->fai = (uint8_t)clampi(t->fai + d, 0, 1);
        return;
    }
    if (t->edit) {                                      /* a row's popup: its value */
        const int fine = (m->seq.mods & SEQ_M_SHIFT) != 0;
        if (fine)
            m->seq.mods_used |= SEQ_M_SHIFT;
        if (t->tab == 1)
            t->rh = (uint8_t)clampi(t->rh + d, 0, RFM_MOVE_KIT_RH_N - 1), rfmv_seq_dirty(m);
        else if (ROW_P[t->row] >= 0)
            turn_p(m, ROW_P[t->row], d, fine);
        return;
    }
    if (t->picking) {
        const int was = t->tab;
        t->tab = (uint8_t)clampi(t->tab + d, 0, 2);
        if (t->tab != was)
            preview_stop(m), need(m);
        return;
    }
    if (t->on_tabs) {
        if (d <= 0)
            return;
        t->on_tabs = 0, d--;
        if (t->tab == 0)
            t->row = 0;
        if (t->tab == 1)
            t->kitrow = 0;
        if (t->tab == 2) {
            if (!t->place)
                t->psel = 0;
            else
                t->fsel[t->depth] = 0;
            need(m);
            if (t->place)
                preview(m);
        }
        if (!d)
            return;
    }
    if (t->tab == 2) {
        files_turn(m, d);
        return;
    }
    if (t->tab == 1) {                                  /* (3b) Load, Save, the right half */
        if ((int)t->kitrow + d < 0)
            t->on_tabs = 1, t->kitrow = 0;
        else
            t->kitrow = (uint8_t)clampi(t->kitrow + d, 0, KT_N - 1);
        return;
    }
    t->turned = 1, t->turn_ms = now(m);
    if ((int)t->row + d < 0) {
        t->on_tabs = 1, t->row = 0;
        return;
    }
    t->row = (uint8_t)clampi(t->row + d, 0, KR_N - 1);
}
static void click(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    const int k = t->cur;
    t->msg_ms = 0, t->turned = 0, t->kpop = -1;
    touched(m);
    if (rec_running(m))
        return;
    if (merge_up(m)) {                                  /* (3c) the chosen one done (no second pad yet: nothing) */
        if (t->mg_b)
            merge_do(m);
        return;
    }
    if (t->sl_on) {
        slice_apply(m);
        return;
    }
    if (t->pend_take) {
        pend_click(m);
        return;
    }
    if (t->tools) {                                     /* the Sound row's tools: Play, (3c) Trim, Normalize, Reverse,
                                                         * Delete */
        if (t->ti == TL_TRIM || t->ti == TL_NORM || t->ti == TL_REV) {
            pad_edit(m, k, t->ti);
            t->tools = 0;
            return;
        }
        if (t->ti == TL_PLAY) {
            if (has_sound(CPAD(m, k))) {
                note_out(m, k, 0, 100, trk(k));         /* (heard once: a one shot's whole; held modes: at once off) */
                if (CPAD(m, k)->v[RFM_MOVE_KIT_P_MODE] != RFM_MOVE_KIT_M_ONESHOT)
                    t->tool_note = (uint8_t)(k + 1), t->tool_ms = now(m);
            }
        } else {
            if (has_sound(CPAD(m, k)))
                empty_pad(m, k);
            t->tools = 0;
        }
        return;
    }
    if (t->fact) {                                      /* (3b) a folder's actions */
        if (t->fai) {
            all_to_pads(m);
            return;
        }
        t->fact = 0;
        if (t->depth < RFM_MOVE_KIT_DEPTH) {            /* (Open: as its click) */
            preview_stop(m);
            t->depth++, t->folder[t->depth] = t->fact_id, t->fsel[t->depth] = 0;
            rows_forget(m);
            need(m);
        }
        return;
    }
    if (t->edit) {
        t->edit = 0;
        return;
    }
    if (t->picking) {
        t->picking = 0;
        return;
    }
    if (t->on_tabs) {
        t->picking = 1;
        return;
    }
    if (t->tab == 0) {
        if (t->row == KR_SOUND)
            t->tools = 1, t->ti = 0;
        else
            t->edit = 1;
        return;
    }
    if (t->tab == 1) {                                  /* (3b) Load: Files' Kits; Save; the right half's popup */
        if (t->kitrow == KT_LOAD) {
            preview_stop(m);
            t->tab = 2, t->on_tabs = 0, t->place = RFM_MOVE_KIT_PL_KITS, t->psel = RFM_MOVE_KIT_PL_KITS - 1;
            t->depth = 0, t->folder[0] = 0, t->fsel[0] = 0;
            rows_forget(m);
            need(m);
        } else if (t->kitrow == KT_SAVE) {
            if (!t->saving) {
                t->saving = 1;
                act(m, RFM_MOVE_ACT_KIT_SAVE, 0, 0, 0, 0, 0, 0, 0, NULL);
            }
            msg(m, "Saving", NULL);
        } else
            t->edit = 1;
        return;
    }
    if (!t->place) {                                    /* a place */
        t->place = (uint8_t)(t->psel + 1), t->depth = 0, t->folder[0] = 0, t->fsel[0] = 0;
        rows_forget(m);
        need(m);
        return;
    }
    {
        const rfm_move_kit_row_t *r = row_get(m, t->place, t->folder[t->depth], t->fsel[t->depth]);
        if (!r)
            return;                                     /* (not come yet) */
        if (r->kind == RFM_MOVE_KIT_ROW_DIR && (m->seq.mods & SEQ_M_SHIFT)) {   /* (3b) Shift: its actions */
            m->seq.mods_used |= SEQ_M_SHIFT;
            preview_stop(m);
            t->fact = 1, t->fai = 1, t->fact_id = r->id;
            snprintf(t->fact_name, sizeof t->fact_name, "%s", r->name);
            return;
        }
        if (r->kind == RFM_MOVE_KIT_ROW_KIT) {          /* (3b) a saved kit: loaded */
            preview_stop(m);
            act(m, RFM_MOVE_ACT_KIT_OPEN, 0, 0, 0, RFM_MOVE_KIT_PL_KITS, r->id, 0, 0, NULL);
            msg(m, r->name, "Loading");
            return;
        }
        if (r->kind == RFM_MOVE_KIT_ROW_DIR) {
            if (t->depth >= RFM_MOVE_KIT_DEPTH)
                return;
            preview_stop(m);
            t->depth++, t->folder[t->depth] = r->id, t->fsel[t->depth] = 0;
            rows_forget(m);
            need(m);
            return;
        }
        {                                               /* a sound: onto the chosen pad */
            rfm_move_kit_pad_t *d = PAD(m, k);
            char pn[8];
            preview_stop(m);
            d->st = RFM_MOVE_KIT_LOADING, d->fresh = 1;
            act(m, RFM_MOVE_ACT_KIT_LOAD, k, 0, 0, t->place, r->id, 0, 0, NULL);
            rfmv_seq_dirty(m);
            snprintf(pn, sizeof pn, "Pad %d", k + 1);
            msg(m, r->name, pn);
        }
    }
}
int rfmv_kit_back_has(const rfm_move_t *m)
{
    const rfm_move_kit_t *t = &m->kit;
    return rfmv_kit_shown(m) && (t->edit || t->tools || t->fact || t->picking || t->pend_take || t->kpop >= 0 ||
                                 merge_up(m) || t->sl_on ||
                                 (t->tab == 2 && !t->on_tabs && (t->place || t->depth)) ||
                                 (t->tab != 0 && !t->on_tabs));
}
int rfmv_kit_back(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    if (!rfmv_kit_shown(m))
        return 0;
    touched(m);
    t->msg_ms = 0, t->kpop = -1;
    if (merge_up(m))                                    /* (3c) the Copy menu, the slices: closed, nothing done */
        merge_off(m);
    else if (t->sl_on)
        slice_leave(m);
    else if (t->pend_take)
        t->pend_take = 0;
    else if (t->fact)
        t->fact = 0;
    else if (t->edit)
        t->edit = 0;
    else if (t->tools)
        t->tools = 0;
    else if (t->picking)
        t->picking = 0;
    else if (t->tab == 2 && !t->on_tabs) {             /* Files: a folder up, the places, then the tabs */
        preview_stop(m);
        if (t->depth)
            t->depth--, rows_forget(m), need(m);
        else if (t->place)
            t->psel = (uint8_t)(t->place - 1), t->place = 0, rows_forget(m);
        else
            t->on_tabs = 1;
    } else if (t->tab == 1 && !t->on_tabs)
        t->on_tabs = 1;
    /* (the Pad tab's rows, the tabs: nothing more; Back never leaves the view) */
    return 1;
}

/* ================================================================ the knobs === */
/* (3c) the knobs: dark and dead under the tools, the Copy menu, a waiting take; the slices' cut on 1 and 2 */
static int knob_dead(const rfm_move_t *m, int k)
{
    const rfm_move_kit_t *t = &m->kit;
    if (t->sl_on)
        return k > 1;
    return KNOB_P[k] < 0 || t->tools || merge_up(m) || t->pend_take;
}
static void knob_touch(rfm_move_t *m, int k, int on)
{
    rfm_move_kit_t *t = K(m);
    if (!on) {
        t->kdown = (uint8_t)(t->kdown & ~(1u << k));
        if (t->kpop == k)
            t->kpop_ms = now(m);                        /* (its popup a second more) */
        return;
    }
    t->kdown = (uint8_t)(t->kdown | 1u << k);
    if (knob_dead(m, k))
        return;                                         /* (dark and dead) */
    t->kpop = (int8_t)k, t->kpop_ms = now(m), t->msg_ms = 0;
    touched(m);
}
static void knob_turn(rfm_move_t *m, int k, int d)
{
    rfm_move_kit_t *t = K(m);
    const int p = KNOB_P[k], fine = (m->seq.mods & SEQ_M_SHIFT) != 0;
    int n;
    static const uint8_t DIV[RFM_MOVE_KIT_P_N] = { 2, 1, 1, 5, 2, 2, 2, 5, 5, 2, 1, 5, 3, 1, 1, 1 };
    if (!d || knob_dead(m, k))
        return;
    if (fine)
        m->seq.mods_used |= SEQ_M_SHIFT;
    if (t->sl_on) {                                     /* (3c) the chosen slice's cut: 1 coarse, 2 (or Shift) fine */
        t->kpop = (int8_t)k, t->kpop_ms = now(m), t->msg_ms = 0;
        touched(m);
        cut_move(m, d, k == 1 || fine);
        return;
    }
    t->kpop = (int8_t)k, t->kpop_ms = now(m), t->msg_ms = 0;
    touched(m);
    if (fine && (p == RFM_MOVE_KIT_P_PITCH || p == RFM_MOVE_KIT_P_START))
        n = d;
    else {
        int a = t->kacc[k];
        if ((a > 0 && d < 0) || (a < 0 && d > 0))
            a = 0;
        a += d;
        n = a / DIV[p];
        t->kacc[k] = (int8_t)(a - n * DIV[p]);
    }
    if (n)
        turn_p(m, p, n, fine);
}

/* ================================================================ input === */
/* the layers above the view that take the pads, steps, knobs or jog for themselves (they fall through) */
static int loop_layer(const rfm_move_t *m) { return (m->seq.mods & SEQ_M_LOOP) || rfmv_seq_loop_over(m); }
int rfmv_kit_input(rfm_move_t *m, const mr_event_t *ev)
{
    rfm_move_kit_t *t = K(m);
    rfm_move_seq_t *q = &m->seq;
    const int i = ev->index;
    if (!rfmv_kit_cap(m))
        return 0;
    /* the releases of what the kit took, wherever it is now */
    if (ev->type == MR_EV_PAD && !ev->value && i >= 0 && i < 32 && (t->down >> i & 1)) {
        pad_up(m, i);
        return 1;
    }
    if (ev->type == MR_EV_STEP && !ev->value && i >= 0 && i < 16 && (t->st_down >> i & 1)) {
        const uint16_t bit = (uint16_t)(1u << i);
        t->st_down &= (uint16_t)~bit;
        if (!(t->st_used & bit) && rfmv_kit_active(m))
            step_toggle(m, t->cur, i, 100);             /* (a click: at its release, the pads may fill a held one) */
        t->st_used &= (uint16_t)~bit;
        return 1;
    }
    if (ev->type == MR_EV_TOUCH && !ev->value && i >= 0 && i < 8 && (t->kdown >> i & 1)) {
        knob_touch(m, i, 0);
        return 1;
    }
    if (ev->type == MR_EV_BUTTON && !ev->value) {
        uint8_t *b = i == MR_CC_SAMPLE ? &t->sdown : i == MR_CC_UNDO ? &t->udown : i == MR_CC_UP || i == MR_CC_DOWN ? &t->ud_down : NULL;
        if (b && *b) {
            *b = 0;
            return 1;
        }
        return 0;
    }
    if (!rfmv_kit_active(m) || m->track_held || rfmv_st_on(m) || q->smenu == RFM_MOVE_SM_GLOBAL)
        return 0;
    switch (ev->type) {
    case MR_EV_PAD:
        if (i < 0 || i >= 32 || !ev->value || rfmv_seq_al_holding(m) || q->fill)
            return 0;
        if (loop_layer(m))
            return 0;                                   /* (Loop: the loop's lengths) */
        pad_down(m, i, rfm_move_vel(&m->s, ev->value));
        return 1;
    case MR_EV_PAD_PRESSURE:
        return i >= 0 && i < 32 && (t->down >> i & 1);
    case MR_EV_STEP:
        if (i < 0 || i >= 16 || !ev->value || q->smenu || loop_layer(m) || rfmv_seq_mute_on(m) ||
            (q->mods & (SEQ_M_SHIFT | SEQ_M_COPY | SEQ_M_MUTE)))
            return 0;                                   /* (Shift: the shortcuts; Copy, Mute, Loop: the sequencer's) */
        if (q->mods & SEQ_M_DELETE) {                   /* Delete + a step: the chosen pad's notes there */
            const int abs = q->page * RFM_MOVE_SEQ_PAGE + i;
            q->mods_used |= SEQ_M_DELETE;
            if (step_has(m, t->cur, abs))
                rfmv_seq_group(m), step_clear(m, t->cur, abs), rfmv_seq_dirty(m);
            return 1;
        }
        t->st_down |= (uint16_t)(1u << i);
        touched(m);
        return 1;
    case MR_EV_KNOB:
        if (i < 0 || i > 7 || q->smenu)
            return 0;
        knob_turn(m, i, ev->value);
        return 1;
    case MR_EV_TOUCH:
        if (i >= 0 && i < 8 && !q->smenu) {
            knob_touch(m, i, 1);
            return 1;
        }
        return i == 9 && rfmv_kit_shown(m) && !loop_layer(m) && !rfmv_seq_mute_on(m);
    case MR_EV_JOG:
        if (!rfmv_kit_shown(m) || loop_layer(m) || rfmv_seq_mute_on(m) || q->step_held)
            return 0;
        turn(m, ev->value);
        need(m);
        return 1;
    case MR_EV_JOG_CLICK:
        if (!ev->value || !rfmv_kit_shown(m) || loop_layer(m) || rfmv_seq_mute_on(m) || q->fill)
            return 0;
        click(m);
        need(m);
        return 1;
    case MR_EV_BUTTON:
        if (i == MR_CC_SAMPLE && !rfmv_smp_shown(m)) { /* a pad held + Sample: it records into that pad */
            const int k = held_kit_pad(m);
            if (k < 0 || !rfmv_smp_cap(m) || rec_running(m))
                return 0;
            t->sdown = 1;
            choose(m, k);
            t->rec_pad = (uint8_t)(k + 1), t->rec_held = 1;
            act(m, RFM_MOVE_ACT_KIT_REC, k, 1, 0, m->s.smp[RFM_MOVE_SMP_MODE], 0, 0, 0, NULL);
            touched(m);
            return 1;
        }
        if (i == MR_CC_UNDO && (und_ours(m) || undk_ours(m)) && !(q->mods & SEQ_M_SHIFT)) {   /* the pad emptied
                                                         * last comes back; (3b) the kit a folder or a kit replaced */
            t->udown = 1;
            if (undk_ours(m))
                undo_kit(m);
            else
                undo_pad(m);
            return 1;
        }
        if ((i == MR_CC_UP || i == MR_CC_DOWN) && !q->step_held) {   /* (no job in the kit: dark, dead) */
            t->ud_down = 1;
            return 1;
        }
        return 0;
    default: return 0;
    }
}
/* (seq advance) the timed parts: a tool's held-mode audition let go */
void rfmv_kit_poll(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    if (t->mg_a && !(m->seq.mods & SEQ_M_COPY))
        merge_off(m);                                   /* (3c) Copy let go before a choice: nothing done */
    if (t->tool_note && now(m) - t->tool_ms >= 400) {
        note_out(m, t->tool_note - 1, 0, 0, trk(t->tool_note - 1));
        t->tool_note = 0;
    }
    if (t->rec_pad && m->smp.st.state == RFM_MOVE_SMP_ST_IDLE && !t->rec_held && now(m) - t->chg_ms > 5000)
        t->rec_pad = 0;                                 /* (the host never put a take there: given up) */
    if (rfmv_kit_cap(m) && t->fit_ok) {                 /* (3b) the tempo or a kit track's rate changed: the stretched
                                                         * pads fitted again */
        int i, chg = m->seq.bpm10 != t->fit_bpm10;
        for (i = 0; i < 4 && !chg; i++)
            chg = t->fit_rate[i] != (uint8_t)(m->seq.tr[RFM_MOVE_KIT_TRACK0 + i].rate | m->seq.tr[RFM_MOVE_KIT_TRACK0 + i].feel << 4);
        if (chg) {
            fit_mark(m);
            for (i = 0; i < RFM_MOVE_KIT_PADS; i++)
                if (t->pad[i].v[RFM_MOVE_KIT_P_STRETCH])
                    send_set(m, i, RFM_MOVE_KIT_P_STRETCH);
        }
    }
}

/* ================================================================ the lights === */
static int blink_on(const rfm_move_t *m) { return (now(m) - m->kit.blink_ms) % 400u < 200u; }
/* (3c) the slices' colour: the take's source's (a slot's), else the kit's */
static rgb_t slice_rgb(const rfm_move_t *m)
{
    const int s = m->kit.pend_src;
    return s >= RFM_MOVE_SMP_SRC_SLOT1 && s < RFM_MOVE_SMP_SRC_SLOT1 + RFM_MOVE_FW_SLOTS ? RFMV_SLOT_RGB[s - RFM_MOVE_SMP_SRC_SLOT1] : KIT;
}
static int len_param(int p)
{
    return p == RFM_MOVE_KIT_P_PITCH || p == RFM_MOVE_KIT_P_STRETCH || p == RFM_MOVE_KIT_P_START || p == RFM_MOVE_KIT_P_END;
}
static int kpop_up(const rfm_move_t *m)                 /* a knob's popup is on the screen */
{
    const rfm_move_kit_t *t = &m->kit;
    return t->kpop >= 0 && KNOB_P[t->kpop] >= 0 && ((t->kdown >> t->kpop & 1) || now(m) - t->kpop_ms < RFM_MOVE_POPUP_MS);
}
/* (3b, board D) Pitch, Stretch, Start or End changing: the steps it lasts on the step keys */
static int len_pop(const rfm_move_t *m)
{
    const rfm_move_kit_t *t = &m->kit;
    if (msg_up(m))
        return 0;
    if (kpop_up(m))
        return len_param(KNOB_P[t->kpop]);
    return t->edit && t->tab == 0 && !t->tools && ROW_P[t->row] >= 0 && len_param(ROW_P[t->row]);
}
static int next_empty(const rfm_move_t *m)
{
    int k;
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++)
        if (!has_sound(CPAD(m, k)))
            return k;
    return -1;
}
static int to_kit(const rfm_move_t *m)                  /* the sampler shows, its To the kit */
{
    return rfmv_smp_shown(m) && m->s.smp[RFM_MOVE_SMP_TO] == RFM_MOVE_SMP_TO_KIT;
}
void rfmv_kit_leds(const rfm_move_t *m, rgb_t c[L_WHITE0], uint8_t wv[13])
{
    const rfm_move_kit_t *t = &m->kit;
    const rfm_move_seq_t *q = &m->seq;
    const int k0 = t->cur, cg = CPAD(m, k0)->v[RFM_MOVE_KIT_P_CHOKE];
    const int choke_pop = (t->edit && t->tab == 0 && t->row == KR_CHOKE) || (t->kpop >= 0 && KNOB_P[t->kpop] == RFM_MOVE_KIT_P_CHOKE);
    int i;
    if (!rfmv_kit_active(m))
        return;
    if (!loop_layer(m)) {
        for (i = 0; i < 16; i++) {                      /* the kit's pads */
            const rfm_move_kit_pad_t *d = CPAD(m, i);
            const int p = pad_p(i), glow = t->glow[i] && now(m) - t->glow[i] < GLOW_MS;
            rgb_t v = RFMV_OFF;
            if (has_sound(d))
                v = d->v[RFM_MOVE_KIT_P_MUTE] ? sc(RFMV_WHITE, 40) : sc(ROWC[i / 4], glow ? 256 : 80);
            if (choke_pop && i != k0)                   /* (the Choke popup: the group's pads lit, the others dim) */
                v = cg && d->v[RFM_MOVE_KIT_P_CHOKE] == cg ? sc(ROWC[i / 4], 220) : has_sound(d) ? sc(RFMV_WHITE, 14) : RFMV_OFF;
            if (t->pend_take)                           /* (a take waits for its pad: all blinking) */
                v = blink_on(m) ? sc(RFMV_WHITE, 200) : v;
            else if (i == k0)
                v = t->tab == 2 && !t->on_tabs ? (blink_on(m) ? RFMV_WHITE : sc(RFMV_WHITE, 60)) : RFMV_WHITE;
            if (t->rec_pad == i + 1)
                v = m->smp.st.state == RFM_MOVE_SMP_ST_ARMED && now(m) % 500u >= 250u ? sc(RFMV_RED, 40) : RFMV_RED;
            if (t->fact && rfmv_kit_shown(m))           /* (3b, board K) All To Pads: where the folder's sounds go */
                v = sc(ROWC[i / 4], 200);
            if (to_kit(m) && i == next_empty(m))        /* (3b, board U) the sampler's To Kit: where the take goes */
                v = sc(RFMV_WHITE, 160);
            if (merge_up(m) && i == t->mg_a - 1)        /* (3c, board N) Copy held: the first blinking, the second white */
                v = (now(m) - t->mg_ms) % 400u < 200u ? sc(RFMV_WHITE, 140) : sc(RFMV_WHITE, 40);
            if (merge_up(m) && i == t->mg_b - 1)
                v = RFMV_WHITE;
            if (t->sl_on)                               /* (3c, board P) the slices in the take's colour, the chosen white */
                v = i == t->sl_cur && i < t->sl_n ? RFMV_WHITE : i < t->sl_n ? sc(slice_rgb(m), 110) : RFMV_OFF;
            if (t->down >> p & 1)
                v = RFMV_WHITE;
            c[L_PAD0 + p] = v;
        }
        for (i = 0; i < 16; i++) {                      /* the right half: the chosen pad's pitches */
            const int p = (i / 4) * 8 + 4 + i % 4, s = rfmv_kit_right_semis(m, i);
            rgb_t v = RFMV_OFF;
            if (s >= 0 && !(t->fact && rfmv_kit_shown(m)) && !to_kit(m) && !t->sl_on && !merge_up(m) && !t->tools)
                v = right_root(m, i) ? sc(ROWC[k0 / 4], 230) : sc(RFMV_WHITE, 40);
            if (s >= 0 && (t->down >> p & 1))
                v = RFMV_WHITE;
            c[L_PAD0 + p] = v;
        }
    }
    if (!loop_layer(m) && !rfmv_seq_mute_on(m) && !q->smenu && !(q->mods & (SEQ_M_SHIFT | SEQ_M_COPY | SEQ_M_MUTE))) {
        const int tr = trk(k0), clip = edit_clip_of(m, tr);
        const int pos = q->tr[tr].active == q->tr[tr].sel ? rfmv_seq_clip_pos(m, tr) : -1;
        const int lp = rfmv_kit_shown(m) && len_pop(m), ln = lp ? region_steps(m, k0) : 0;
        for (i = 0; i < 16; i++) {                      /* the chosen pad's steps */
            const int abs = q->page * RFM_MOVE_SEQ_PAGE + i;
            const int in = abs >= rfmv_seq_loop_lo(m, clip) && abs < rfmv_seq_loop_hi(m, clip);
            rgb_t v = step_has(m, k0, abs) ? sc(ROWC[k0 / 4], in ? 220 : 60) : in ? sc(RFMV_WHITE, 16) : RFMV_OFF;
            if (in && pos == abs && (q->playing || q->start_pending))
                v = sc(RFMV_WHITE, 200);                /* (the playhead) */
            if (lp)                                     /* (3b, board D) the steps the sound lasts, the first brightest */
                v = i < ln ? sc(ROWC[k0 / 4], i == 0 ? 255 : 110) : RFMV_OFF;
            if (t->sl_on)                               /* (3c, board P: dark under the slices) */
                v = RFMV_OFF;
            if (t->st_down >> i & 1)
                v = RFMV_WHITE;
            c[L_STEP0 + i] = v;
        }
    }
    for (i = 0; i < 8; i++)                             /* the knobs: the chosen pad's */
        c[L_RING0 + i] = knob_dead(m, i) ? RFMV_OFF : (t->kdown >> i & 1) || t->kpop == i ? RFMV_WHITE
                                                    : sc(t->sl_on ? MENU_KNOB : KIT, 150);
    wv[W_UP] = 0, wv[W_DOWN] = 0;                       /* (no job here) */
    if (und_ours(m) || undk_ours(m))
        wv[W_UNDO] = 60;
}
/* after the sampler's: Sample (a pad recorded into: red; the view's job: a pad held + Sample), Track 4 the kit's */
void rfmv_kit_leds_w(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3])
{
    const rfm_move_kit_t *t = &m->kit;
    rgb_t c;
    if (!rfmv_kit_active(m))
        return;
    if (!m->track_held) {
        w[L_TRACK0 + 3][0] = KIT.r, w[L_TRACK0 + 3][1] = KIT.g, w[L_TRACK0 + 3][2] = KIT.b;
    }
    if (!rfmv_smp_cap(m) || m->smp.open)
        return;
    if (t->rec_pad && m->smp.st.state == RFM_MOVE_SMP_ST_ARMED)
        c = now(m) % 500u < 250u ? RFMV_RED : sc(RFMV_RED, 30);
    else if (t->rec_pad && m->smp.st.state != RFM_MOVE_SMP_ST_IDLE)
        c = RFMV_RED;
    else if (m->smp.st.state != RFM_MOVE_SMP_ST_IDLE || (m->smp.flash && now(m) - m->smp.flash_ms < RFM_MOVE_SMP_FLASH_MS))
        return;
    else
        c = sc(RFMV_WHITE, 60);
    w[L_SAMPLE][0] = c.r, w[L_SAMPLE][1] = c.g, w[L_SAMPLE][2] = c.b;
}

/* ================================================================ the screen === */
/* the tabs: Pad, Kit, Files (the shown one underlined; the jog on them: a white block); the chosen pad's number */
static void top(mr_canvas_t *cv, int tab, int on, int pad)
{
    static icon_t *const T[3] = { &I_PAD, &I_KIT, &I_DIR };
    char n[8];
    int i;
    for (i = 0; i < 3; i++) {
        const int x = 2 + i * 18;
        if (i == tab && on)
            v2_R(cv, x - 2, 0, 15, 14, 1);
        ic(cv, *T[i], x, 1, !(i == tab && on));
        if (i == tab && !on)
            v2_R(cv, x, 13, 11, 1, 1);
    }
    if (pad) {
        snprintf(n, sizeof n, "%d", pad);
        v2_T(cv, 128 - v2_tw(n, 2), 0, n, 2, 1);
    }
}
static void rowi(mr_canvas_t *cv, int y, icon_t i, const char *val, int chosen)
{
    if (chosen)
        v2_R(cv, 0, y, 128, 15, 1);
    ic(cv, i, 3, y + 2, !chosen);
    v2_T(cv, 20, y + 1, val, 2, !chosen);
}
/* the steps the sound lasts: a tick a step, the bar's quarters taller, n steps a bar under them */
static void grid(mr_canvas_t *cv, int y, int n)
{
    int i;
    for (i = 0; i <= 16; i++)
        v2_R(cv, i * 127 / 16, y, 1, i % 4 == 0 ? 4 : 2, 1);
    v2_R(cv, 0, y + 5, n * 127 / 16, 2, 1);
}
static void icon_row(mr_canvas_t *cv, icon_t *const *icons, int n, int chosen, const char *name, int y)
{
    int i;
    for (i = 0; i < n; i++) {
        const int x = 64 - n * 25 / 2 + 7 + i * 25;
        if (i == chosen)
            v2_R(cv, x - 2, y - 2, 15, 15, 1);
        ic(cv, *icons[i], x, y, i != chosen);
    }
    if (name)
        v2_TC(cv, 64, y + 19, name, 2, 1);
}
/* the envelope: attack, hold, release as parts of w (in 1/100); thick 1..3 the part turned */
static void ahr(mr_canvas_t *cv, int x0, int y0, int w, int ht, int a, int h, int r, int thick)
{
    const int xa = x0 + w * a / 100, xh = xa + w * h / 100, xr = xh + w * r / 100;
    v2_L(cv, x0, y0, xa, y0 - ht);
    v2_L(cv, xa, y0 - ht, xh, y0 - ht);
    v2_L(cv, xh, y0 - ht, xr, y0);
    v2_dotH(cv, x0, y0, w);
    if (thick == 1)
        v2_L(cv, x0 + 1, y0, xa + 1, y0 - ht), v2_L(cv, x0 + 2, y0, xa + 2, y0 - ht);
    if (thick == 2)
        v2_R(cv, xa, y0 - ht + 1, xh - xa, 2, 1);
    if (thick == 3)
        v2_L(cv, xh - 1, y0 - ht, xr - 1, y0), v2_L(cv, xh - 2, y0 - ht, xr - 2, y0);
}
static int isqrt(int v)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v)
        r++;
    return r;
}
/* the pad's envelope as parts of 100 (a square root of the times, so short ones show): a, h, r */
static void ahr_parts(const rfm_move_t *m, int k, int *a, int *h, int *r)
{
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    int wa = isqrt(d->v[RFM_MOVE_KIT_P_ATTACK]), wh = isqrt(d->v[RFM_MOVE_KIT_P_HOLD]), wr = isqrt(d->v[RFM_MOVE_KIT_P_RELEASE]);
    int tot;
    if (!d->v[RFM_MOVE_KIT_P_HOLD])                     /* (no hold: held to the end, or while held) */
        wh = (wa + wr) > 0 ? wa + wr : 10;
    tot = wa + wh + wr;
    *a = wa * 95 / tot, *r = wr * 95 / tot, *h = 95 - *a - *r;
}
/* the pad's wave over [start, end) as 64 columns 2 px apart about y0 (h each way) */
static void pad_wave(const rfm_move_t *m, mr_canvas_t *cv, int k, int y0, int h, int whole)
{
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    const int s = whole ? 0 : d->v[RFM_MOVE_KIT_P_START], e = whole ? RFM_MOVE_KIT_FRAC : d->v[RFM_MOVE_KIT_P_END];
    int c;
    if (!d->npk || d->st != RFM_MOVE_KIT_OK)
        return;
    for (c = 0; c < 64; c++) {
        const int64_t den = 64 * (int64_t)RFM_MOVE_KIT_FRAC;
        const int i0 = (int)(((int64_t)s * 64 + (int64_t)(e - s) * c) * d->npk / den);
        int i1 = (int)(((int64_t)s * 64 + (int64_t)(e - s) * (c + 1)) * d->npk / den), i, pk = 0, a;
        if (i1 <= i0)
            i1 = i0 + 1;
        for (i = i0; i < i1 && i < d->npk; i++)
            pk = d->pk[i] > pk ? d->pk[i] : pk;
        a = (pk * h + 127) / 255;
        v2_R(cv, c * 2, y0 - a, 1, a ? a * 2 : 1, 1);
    }
}
static void file_row(mr_canvas_t *cv, int y, const rfm_move_kit_row_t *r, int chosen)
{
    char nm[RFM_MOVE_KIT_NAME];
    int c, maxc;
    if (chosen)
        v2_R(cv, 0, y, 128, 15, 1);
    if (!r)
        return;
    snprintf(nm, sizeof nm, "%s", r->name);
    if (r->kind == RFM_MOVE_KIT_ROW_DIR || r->kind == RFM_MOVE_KIT_ROW_KIT) {   /* (3b: a saved kit, I_KIT) */
        ic(cv, r->kind == RFM_MOVE_KIT_ROW_KIT ? I_KIT : I_DIR, 3, y + 2, !chosen);
        maxc = (128 - 20) / 12;
        if ((int)strlen(nm) > maxc)
            nm[maxc] = 0;
        v2_T(cv, 20, y + 1, nm, 2, !chosen);
        return;
    }
    maxc = (99 - 3) / 12;
    if ((int)strlen(nm) > maxc)
        nm[maxc] = 0;
    v2_T(cv, 3, y + 1, nm, 2, !chosen);
    for (c = 0; c < RFM_MOVE_SMP_THUMB; c++) {
        const int a = (r->thumb[c] * 5 + 127) / 255;
        v2_R(cv, 99 + c * 2, y + 7 - a, 1, a ? a * 2 : 1, !chosen);
    }
}
static void files_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_kit_t *t = &m->kit;
    const int on = t->on_tabs || t->picking;
    int i;
    top(cv, 2, on, t->cur + 1);
    if (!t->place) {
        static icon_t *const PI[PL_N] = { &I_LIST, &I_DIR, &I_KIT };
        static const char *const PN[PL_N] = { "Takes", "Samples", "Kits" };
        for (i = 0; i < PL_N; i++)
            rowi(cv, 16 + i * 16, *PI[i], PN[i], !on && i == t->psel);
        return;
    }
    if (t->known && !t->total) {
        v2_TC(cv, 64, 32, "Empty", 2, 1);
        return;
    }
    {
        const int sel = t->fsel[t->depth], first = t->known ? first_of(sel, t->total) : clampi(sel - 1, 0, 0x7FFF);
        for (i = 0; i < 3 && (!t->known || first + i < t->total); i++)
            file_row(cv, 16 + i * 16, row_get(m, t->place, t->folder[t->depth], first + i), !on && first + i == sel);
    }
}
static icon_t *row_icon(int r, int mode)
{
    switch (r) {
    case KR_SOUND: return &I_DIR;
    case KR_PITCH: return &I_PITCH;
    case KR_START: return &I_START;
    case KR_END: return &I_END;
    case KR_MODE: return MODE_ICON[clampi(mode, 0, RFM_MOVE_KIT_M_N - 1)];
    case KR_CHOKE: return &I_CHOKE;
    case KR_REVERSE: return &I_REV;
    case KR_LEVEL: return &I_LVL;
    case KR_PAN: return &I_PAN;
    case KR_STRETCH: return &I_STRETCH;
    case KR_SEND1:
    case KR_SEND2:
    case KR_SEND3: return &I_SEND;
    default: return &I_ENV;
    }
}
static void row_text(const rfm_move_t *m, int r, char *o, int cap)
{
    if (r == KR_SOUND)
        sound_text(m, m->kit.cur, o, cap);
    else
        val_text(m, m->kit.cur, ROW_P[r], 1, o, cap);
}
static void row_draw(const rfm_move_t *m, mr_canvas_t *cv, int y, int r, int chosen)
{
    char v[32];
    row_text(m, r, v, sizeof v);
    rowi(cv, y, *row_icon(r, CPAD(m, m->kit.cur)->v[RFM_MOVE_KIT_P_MODE]), v, chosen);
}
/* a setting's popup (a row's click, a knob touched or turned) */
static void popup(const rfm_move_t *m, mr_canvas_t *cv, int p)
{
    const int k = m->kit.cur;
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    char v[32];
    int a, h, r;
    val_text(m, k, p, 0, v, sizeof v);
    if (p == RFM_MOVE_KIT_P_MODE) {                     /* (board E) how it plays, with its envelope */
        static icon_t *const P[RFM_MOVE_KIT_M_N] = { &I_PLAY, &I_PAD, &I_LOOP };
        v2_TC(cv, 64, 0, "Play", 2, 1);
        icon_row(cv, P, RFM_MOVE_KIT_M_N, d->v[p], NULL, 18);
        v2_TC(cv, 64, 32, MODE_NAME[clampi(d->v[p], 0, RFM_MOVE_KIT_M_N - 1)], 2, 1);
        ahr_parts(m, k, &a, &h, &r);
        ahr(cv, 14, 63, 100, 12, a, h, r, 0);
        return;
    }
    if (p == RFM_MOVE_KIT_P_ATTACK || p == RFM_MOVE_KIT_P_HOLD || p == RFM_MOVE_KIT_P_RELEASE) {   /* (board F) */
        v2_TC(cv, 64, 0, P_NAME[p], 2, 1);
        v2_TC(cv, 64, 17, v, 2, 1);
        ahr_parts(m, k, &a, &h, &r);
        ahr(cv, 4, 60, 120, 22, a, h, r, p - RFM_MOVE_KIT_P_ATTACK + 1);
        return;
    }
    v2_titleBox(cv, P_NAME[p], v, 64, 128);
    if (len_param(p))
        grid(cv, 54, region_steps(m, k));               /* (the steps it lasts now; 3b: Stretch's too, board D) */
    if (p >= RFM_MOVE_KIT_P_SEND1 && p <= RFM_MOVE_KIT_P_SEND3) {   /* (3b, board H) the send as a bar */
        v2_dotH(cv, 8, 58, 112);
        if (d->v[p] > 0)
            v2_R(cv, 8, 56, 112 * d->v[p] / 100, 5, 1);
    }
}
static void rec_screen(const rfm_move_t *m, mr_canvas_t *cv)   /* (boards L, M) a take into the pad */
{
    const rfm_move_smp_status_t *s = &m->smp.st;
    const int armed = s->state == RFM_MOVE_SMP_ST_ARMED;
    char b[16];
    int c;
    if (!armed || now(m) % 500u < 250u)
        ic(cv, I_DOT, 2, 1, 1);
    snprintf(b, sizeof b, "%d", m->kit.rec_pad);
    v2_T(cv, 20, 0, b, 2, 1);
    {
        const int lv = rfmv_smp_lvpx(s->level, 28);
        if (lv)
            v2_R(cv, 121, 45 - lv, 6, lv, 1);
        v2_dotV(cv, 124, 17, 28);
    }
    if (armed) {
        ic(cv, I_SRC, 117, 1, 1);
        for (c = 0; c < 58; c++)
            v2_R(cv, c * 2, 34, 1, 1, 1);
        if (m->s.smp[RFM_MOVE_SMP_MODE] == RFM_MOVE_SMP_M_THRESHOLD) {
            const int db = m->s.smp[RFM_MOVE_SMP_THRESHOLD];
            v2_R(cv, 117, 45 - clampi(((db + 60) * 56 + 60) / 120, 0, 28), 11, 1, 1);
        }
        v2_TC(cv, 58, 48, "Waiting", 2, 1);
        return;
    }
    {
        const uint32_t sec = s->rate ? s->frames / s->rate : 0;
        snprintf(b, sizeof b, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
        v2_T(cv, 40, 0, b, 2, 1);
    }
    for (c = 0; c < 58; c++) {                          /* the take's wave growing from the left, the line after it */
        const int i = s->span ? c * s->span / 58 : c;
        if (i < s->npeaks) {
            const int a = (s->peaks[i] * 15 + 127) / 255;
            v2_R(cv, c * 2, 34 - a, 1, a ? a * 2 : 1, 1);
        } else
            v2_R(cv, c * 2, 34, 1, 1, 1);
    }
}
/* (3c, board N) Copy held: the first pad's number, the arrow onto the second's, Copy / Mix / Join */
static void merge_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_kit_t *t = &m->kit;
    char n[8];
    snprintf(n, sizeof n, "%d", t->mg_a);
    v2_T(cv, 0, 0, n, 2, 1);
    v2_L(cv, 18, 7, 108, 7), v2_L(cv, 104, 3, 108, 7), v2_L(cv, 104, 11, 108, 7);
    if (!t->mg_b)
        return;
    snprintf(n, sizeof n, "%d", t->mg_b);
    v2_T(cv, 128 - v2_tw(n, 2), 0, n, 2, 1);
    icon_row(cv, MG_ICON, RFM_MOVE_KIT_MG_N, t->mg_i, MG_NAME[t->mg_i % RFM_MOVE_KIT_MG_N], 26);
}
static int slice_x(const rfm_move_kit_t *t, uint32_t f)
{
    const int x = t->sl_frames ? (int)((uint64_t)f * 128 / t->sl_frames) : 0;
    return x > 127 ? 127 : x;
}
static void slice_text(const rfm_move_t *m, char *o, int cap)
{
    const rfm_move_kit_t *t = &m->kit;
    static const char *const W[RFM_MOVE_KIT_SM_N] = { "Hits", "Equal", "Equal", "Equal", "Steps" };
    static const uint8_t N[RFM_MOVE_KIT_SM_N] = { 0, 16, 8, 4, 0 };
    const int meth = t->sl_meth % RFM_MOVE_KIT_SM_N;
    const int n = t->sl_ok == 1 ? t->sl_n : N[meth];
    if (n)
        snprintf(o, (size_t)cap, "%d %s", n, W[meth]);
    else
        snprintf(o, (size_t)cap, "%s", W[meth]);
}
/* (3c, board P) the take with its cuts, the chosen slice a white block; the method (a knob in use: the cut's time),
 * the pads they go onto */
static void slice_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_kit_t *t = &m->kit;
    const int knob = t->kpop >= 0 && t->kpop < 2 && ((t->kdown >> t->kpop & 1) || now(m) - t->kpop_ms < RFM_MOVE_POPUP_MS);
    char v[24];
    int c, i;
    if (t->sl_ok == 1) {
        const int xa = slice_x(t, t->sl_cut[t->sl_cur]), xb = slice_x(t, t->sl_cut[t->sl_cur + 1]);
        if (xb > xa)
            v2_R(cv, xa, 18 - 12 - 1, xb - xa, 2 * 12 + 2, 1);
        for (c = 0; c < 64 && t->sl_npk; c++) {
            const int i0 = c * t->sl_npk / 64, i1 = (c + 1) * t->sl_npk / 64 > i0 ? (c + 1) * t->sl_npk / 64 : i0 + 1;
            int j, pk = 0, a;
            for (j = i0; j < i1 && j < t->sl_npk; j++)
                pk = t->sl_pk[j] > pk ? t->sl_pk[j] : pk;
            a = (pk * 12 + 127) / 255;
            v2_R(cv, c * 2, 18 - a, 1, a ? a * 2 : 1, !(c * 2 >= xa && c * 2 < xb));
        }
        for (i = 0; i <= t->sl_n; i++)                  /* (the cuts between the slices; the ends where they cut) */
            if ((i > 0 && i < t->sl_n) || (i == 0 && t->sl_cut[0] > 0) || (i == t->sl_n && t->sl_cut[i] < t->sl_frames))
                v2_dotV(cv, slice_x(t, t->sl_cut[i]), 5, 26);
    }
    if (knob && t->sl_ok == 1) {
        secs((int)((uint64_t)t->sl_cut[t->sl_cur] * 1000u / (t->sl_rate ? t->sl_rate : 48000)), v, sizeof v);
        rowi(cv, 33, I_START, v, 1);
    } else {
        slice_text(m, v, sizeof v);
        rowi(cv, 33, I_SLICE, v, 1);
    }
    {
        const int n = t->sl_ok == 1 ? slice_count(m) : 0;
        if (n > 1)
            snprintf(v, sizeof v, "Pads %d-%d", t->sl_first + 1, t->sl_first + n);
        else
            snprintf(v, sizeof v, "Pad %d", t->sl_first + 1);
        rowi(cv, 49, I_KIT, v, 0);
    }
}
int rfmv_kit_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_kit_t *t = &m->kit;
    const int k = t->cur;
    if (!rfmv_kit_shown(m))
        return 0;
    mr_gfx_clear(cv, 0);
    if (rec_running(m)) {
        rec_screen(m, cv);
        return 1;
    }
    if (msg_up(m)) {
        v2_msg(cv, t->msg, t->msg2);
        return 1;
    }
    if (merge_up(m)) {
        merge_screen(m, cv);
        return 1;
    }
    if (t->sl_on) {
        slice_screen(m, cv);
        return 1;
    }
    if (t->pend_take) {                                 /* (3c) a take waiting: a pad's press, or To Pad / Slice */
        static icon_t *const A[2] = { &I_PAD, &I_SLICE };
        icon_row(cv, A, 2, t->pend_i, t->pend_i ? "Slice" : "To Pad", 26);
        return 1;
    }
    if (kpop_up(m)) {
        popup(m, cv, KNOB_P[t->kpop]);
        return 1;
    }
    if (t->tab == 2 && t->fact && !t->on_tabs) {       /* (3b, board K) a folder's actions: Open, All To Pads */
        static icon_t *const A[2] = { &I_DIR, &I_KIT };
        icon_row(cv, A, 2, t->fai, t->fai ? "All To Pads" : "Open", 26);
        return 1;
    }
    if (t->tab == 0 && t->tools && !t->on_tabs) {      /* (board V) the Sound row's tools: Play, Trim, Normalize,
                                                         * Reverse, Delete */
        pad_wave(m, cv, k, 12, 10, 1);
        icon_row(cv, TOOL_ICON, TL_N, t->ti, TOOL_NAME[t->ti % TL_N], 28);
        return 1;
    }
    if (t->edit && t->tab == 0 && ROW_P[t->row] >= 0) {
        popup(m, cv, ROW_P[t->row]);
        return 1;
    }
    if (t->edit && t->tab == 1) {                       /* (board S) the right half */
        char v[24];
        int root, scale;
        key_of(m, trk(k), &root, &scale);
        if (t->rh == RFM_MOVE_KIT_RH_INKEY)
            snprintf(v, sizeof v, "%s %s", rfmv_note_names[root % 12], SCALE_SHORT[scale % RFM_MOVE_NSCALES]);
        else
            snprintf(v, sizeof v, "%s", t->rh == RFM_MOVE_KIT_RH_PITCHES ? "16 Semis" : "Off");
        v2_titleBox(cv, t->rh == RFM_MOVE_KIT_RH_OFF ? "Right" : RH_NAME[t->rh], v, 64, 128);
        return 1;
    }
    if (t->tab == 2) {
        files_screen(m, cv);
        return 1;
    }
    if (t->tab == 1) {                                  /* (board Q) Load, Save, the right half; the kit's name */
        const int on = t->on_tabs || t->picking;
        top(cv, 1, on, 0);
        if (t->name[0])
            v2_T(cv, 128 - v2_tw(t->name, 2), 0, t->name, 2, 1);
        rowi(cv, 16, I_DIR, "Load", !on && t->kitrow == KT_LOAD);
        rowi(cv, 32, I_SAVE, "Save", !on && t->kitrow == KT_SAVE);
        rowi(cv, 48, I_KEYS, RH_NAME[t->rh % RFM_MOVE_KIT_RH_N], !on && t->kitrow == KT_RH);
        return 1;
    }
    {
        const int on = t->on_tabs || t->picking;
        top(cv, 0, on, k + 1);
        if (!on && t->turned && now(m) - t->turn_ms < RFM_MOVE_POPUP_MS) {   /* (board B) three rows */
            const int first = clampi(t->row - 1, 0, KR_N - 3);
            int i;
            for (i = 0; i < 3; i++)
                row_draw(m, cv, 16 + i * 16, first + i, first + i == t->row);
            return 1;
        }
        pad_wave(m, cv, k, 28, 11, 0);                  /* (board A) the wave, the steps it lasts, one row */
        {                                               /* (3c, board O) a join's seam */
            const rfm_move_kit_pad_t *d = CPAD(m, k);
            const int a = d->v[RFM_MOVE_KIT_P_START], b = d->v[RFM_MOVE_KIT_P_END];
            if (d->cut && d->st == RFM_MOVE_KIT_OK && d->cut > a && d->cut < b)
                v2_dotV(cv, (int)((int64_t)(d->cut - a) * 128 / (b - a)), 16, 26);
        }
        grid(cv, 41, region_steps(m, k));
        row_draw(m, cv, 49, on ? 0 : t->row, !on);
        return 1;
    }
}
int rfmv_kit_timed(const rfm_move_t *m)
{
    const rfm_move_kit_t *t = &m->kit;
    int i;
    if (!rfmv_kit_active(m))
        return 0;
    if (msg_up(m) || rec_running(m) || t->pend_take || merge_up(m) || t->sl_on || (t->tab == 2 && !t->on_tabs) || t->tool_note || to_kit(m) ||
        (t->turned && now(m) - t->turn_ms < RFM_MOVE_POPUP_MS) || (t->kpop >= 0 && now(m) - t->kpop_ms < RFM_MOVE_POPUP_MS))
        return 1;
    for (i = 0; i < 16; i++)
        if (t->glow[i] && now(m) - t->glow[i] < GLOW_MS)
            return 1;
    return 0;
}

/* ================================================================ words === */
int rfmv_kit_base_words(const rfm_move_t *m, char *o, int cap)
{
    if (!rfmv_kit_shown(m))
        return 0;
    snprintf(o, (size_t)cap, "Kit, pad %d", m->kit.cur + 1);
    return 1;
}
int rfmv_kit_pop_words(const rfm_move_t *m, char *o, int cap, uint16_t *subj, uint32_t *stamp)
{
    const rfm_move_kit_t *t = &m->kit;
    char v[32];
    o[0] = 0, *stamp = t->chg_ms;
    if (msg_up(m)) {
        snprintf(o, (size_t)cap, "%s%s%s", t->msg, t->msg2[0] ? " " : "", t->msg2);
        *subj = 0xA00, *stamp = t->msg_ms;
    } else if (rec_running(m)) {
        snprintf(o, (size_t)cap, "%s pad %d", m->smp.st.state == RFM_MOVE_SMP_ST_ARMED ? "Armed," : "Recording,", t->rec_pad);
        *subj = 0xA01, *stamp = 0;
    } else if (merge_up(m)) {                           /* (3c) */
        if (t->mg_b)
            snprintf(o, (size_t)cap, "%s pad %d to %d", MG_NAME[t->mg_i % RFM_MOVE_KIT_MG_N], t->mg_a, t->mg_b);
        else
            snprintf(o, (size_t)cap, "Copy from pad %d", t->mg_a);
        *subj = (uint16_t)(0xA70 | t->mg_i), *stamp = 0;
    } else if (t->sl_on) {
        slice_text(m, v, sizeof v);
        snprintf(o, (size_t)cap, "Slice: %s, slice %d", v, t->sl_cur + 1);
        *subj = 0xA78, *stamp = 0;
    } else if (t->pend_take) {
        snprintf(o, (size_t)cap, "%s", t->pend_i ? "Slice" : "To pad");
        *subj = 0xA79, *stamp = 0;
    } else if (t->kpop >= 0 && KNOB_P[t->kpop] >= 0) {
        val_text(m, t->cur, KNOB_P[t->kpop], 0, v, sizeof v);
        snprintf(o, (size_t)cap, "%s: %s", P_NAME[KNOB_P[t->kpop]],
                 KNOB_P[t->kpop] == RFM_MOVE_KIT_P_MODE ? MODE_SAY[CPAD(m, t->cur)->v[RFM_MOVE_KIT_P_MODE] % 3] : v);
        *subj = (uint16_t)(0xA10 | t->kpop);
    } else if (t->tools) {
        snprintf(o, (size_t)cap, "%s", TOOL_NAME[t->ti % TL_N]);
        *subj = 0xA20, *stamp = 0;
    } else if (t->fact && t->tab == 2) {
        snprintf(o, (size_t)cap, "%s", t->fai ? "All to pads" : "Open");
        *subj = 0xA21, *stamp = 0;
    } else if (t->on_tabs || t->picking) {
        static const char *const TN[3] = { "Pad", "Kit", "Files" };
        snprintf(o, (size_t)cap, "%s %s", t->picking ? "Tab:" : "Tabs,", TN[t->tab % 3]);
        *subj = 0xA30;
    } else if (t->tab == 2) {
        const rfm_move_kit_row_t *r = t->place ? row_get(m, t->place, t->folder[t->depth], t->fsel[t->depth]) : NULL;
        if (!t->place)
            snprintf(o, (size_t)cap, "%s", t->psel == 2 ? "Kits" : t->psel ? "Samples" : "Takes");
        else if (t->known && !t->total)
            snprintf(o, (size_t)cap, "Empty");
        else if (r)
            snprintf(o, (size_t)cap, "%s%s", r->kind == RFM_MOVE_KIT_ROW_DIR ? "Folder " : r->kind == RFM_MOVE_KIT_ROW_KIT ? "Kit " : "", r->name);
        *subj = 0xA40, *stamp = 0;
    } else if (t->tab == 1) {
        if (t->kitrow == KT_LOAD)
            snprintf(o, (size_t)cap, "Load");
        else if (t->kitrow == KT_SAVE)
            snprintf(o, (size_t)cap, "Save");
        else
            snprintf(o, (size_t)cap, "Right half: %s", RH_NAME[t->rh % RFM_MOVE_KIT_RH_N]);
        *subj = (uint16_t)(0xA50 | t->kitrow);
    } else {
        if (t->row == KR_SOUND)
            sound_text(m, t->cur, v, sizeof v), snprintf(o, (size_t)cap, "Sound: %s", v);
        else {
            val_text(m, t->cur, ROW_P[t->row], 0, v, sizeof v);
            snprintf(o, (size_t)cap, "%s: %s", P_NAME[ROW_P[t->row]],
                     ROW_P[t->row] == RFM_MOVE_KIT_P_MODE ? MODE_SAY[CPAD(m, t->cur)->v[RFM_MOVE_KIT_P_MODE] % 3] : v);
        }
        *subj = (uint16_t)(0xA60 | t->row);
    }
    return o[0] != 0;
}

/* ================================================================ (3b) the host's kit === */
const char *rfm_move_kit_path(const rfm_move_t *m, int pad)
{
    return pad >= 0 && pad < RFM_MOVE_KIT_PADS ? m->kit.pad[pad].path : NULL;
}
int rfm_move_kit_layout(const rfm_move_t *m) { return m->kit.rh; }
int rfm_move_kit_mix(const rfm_move_t *m, int which)
{
    return which >= 0 && which < RFM_MOVE_KIT_MIX_N ? m->kit.mix[which] : -1;
}
const char *rfm_move_kit_name(const rfm_move_t *m) { return m->kit.name; }
static void name_set(rfm_move_t *m, const char *name)
{
    rfm_move_kit_t *t = K(m);
    int i;
    snprintf(t->name, sizeof t->name, "%s", name ? name : "");
    for (i = 0; t->name[i]; i++)
        if (t->name[i] < 32 || t->name[i] > 126)
            t->name[i] = '?';
}
int rfm_move_kit_saved(rfm_move_t *m, int ok, const char *name)
{
    rfm_move_kit_t *t = K(m);
    if (!rfmv_kit_cap(m))
        return 0;
    t->saving = 0;
    if (ok && name && name[0]) {
        name_set(m, name);
        rfmv_seq_dirty(m);
        msg(m, "Saved", t->name);                       /* (board R) */
    } else
        msg(m, "Not Saved", NULL);
    rfmv_say_check(m);
    return rfm_move_kit_shown(m);
}
int rfm_move_kit_apply(rfm_move_t *m, const char *name, int layout, const uint8_t *mix,
                       const char *const paths[RFM_MOVE_KIT_PADS], const int16_t (*v)[RFM_MOVE_KIT_P_N])
{
    rfm_move_kit_t *t = K(m);
    int k, p;
    if (!rfmv_kit_cap(m))
        return 0;
    if (!paths) {
        msg(m, "Not Loaded", NULL);
        rfmv_say_check(m);
        return rfm_move_kit_shown(m);
    }
    undk_keep(m);
    live_off(m);
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++) {
        rfm_move_kit_pad_t *d = &t->pad[k];
        memset(d, 0, sizeof *d);
        defaults(d->v);
        if (paths[k] && strlen(paths[k]) < RFM_MOVE_KIT_PATH)
            snprintf(d->path, sizeof d->path, "%s", paths[k]);
        for (p = 0; v && p < RFM_MOVE_KIT_P_N; p++)
            d->v[p] = (int16_t)clampi(v[k][p], p_min(p), p_max(p));
        if (d->v[RFM_MOVE_KIT_P_START] >= d->v[RFM_MOVE_KIT_P_END])
            d->v[RFM_MOVE_KIT_P_START] = 0, d->v[RFM_MOVE_KIT_P_END] = RFM_MOVE_KIT_FRAC;
    }
    t->rh = (uint8_t)clampi(layout, 0, RFM_MOVE_KIT_RH_N - 1);
    for (k = 0; mix && k < RFM_MOVE_KIT_MIX_N; k++)
        t->mix[k] = (uint8_t)clampi(mix[k], 0, 127);
    name_set(m, name);
    fit_mark(m);
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++)
        send_pad(m, k);
    send_mix(m);
    rfmv_seq_dirty(m);
    msg(m, "Loaded", t->name);
    rfmv_say_check(m);
    return rfm_move_kit_shown(m);
}
int rfm_move_kit_next_empty(rfm_move_t *m, int claim)
{
    const int k = rfmv_kit_cap(m) ? next_empty(m) : -1;
    if (k >= 0 && claim) {
        rfm_move_kit_pad_t *d = PAD(m, k);
        d->st = RFM_MOVE_KIT_LOADING, d->fresh = 1, d->name[0] = 0;
        touched(m);
    }
    return k;
}
/* (sampler.c) To's value with the kit: "Kit Pad 8" (the next empty pad), "Kit Full" */
void rfmv_kit_to_text(const rfm_move_t *m, char *o, int cap)
{
    const int k = rfmv_kit_cap(m) ? next_empty(m) : -1;
    if (k >= 0)
        snprintf(o, (size_t)cap, "Kit Pad %d", k + 1);
    else
        snprintf(o, (size_t)cap, "Kit Full");
}
/* (fw.c) the kit's strip in the Mixer: a value (RFM_MOVE_MIXP_*) d detents on */
void rfmv_kit_mix_turn(rfm_move_t *m, int par, int d)
{
    rfm_move_kit_t *t = K(m);
    const int i = par < 0 ? 0 : par >= RFM_MOVE_KIT_MIX_N ? RFM_MOVE_KIT_MIX_N - 1 : par;
    const int nv = clampi(t->mix[i] + d, 0, 127);
    if (nv == t->mix[i])
        return;
    t->mix[i] = (uint8_t)nv;
    send_mix(m);
    rfmv_seq_dirty(m);
}

/* ================================================================ (3c) the page's Kit tab === */
int rfm_move_kit_play(rfm_move_t *m, int pad, int vel)
{
    if (!rfmv_kit_cap(m) || pad < 0 || pad >= RFM_MOVE_KIT_PADS)
        return 0;
    vel = clampi(vel, 0, 127);
    if (vel && !has_sound(CPAD(m, pad)))
        return 0;
    note_out(m, pad, 0, vel, trk(pad));
    return 1;
}
int rfm_move_kit_choose(rfm_move_t *m, int pad)
{
    rfm_move_kit_t *t = K(m);
    if (!rfmv_kit_cap(m) || pad < 0 || pad >= RFM_MOVE_KIT_PADS)
        return 0;
    if (t->on && rfmv_kit_active(m))
        choose(m, pad);                                 /* (its track selected, as Shift + the pad) */
    else
        t->cur = (uint8_t)pad, t->tools = 0;
    touched(m);
    rfmv_seq_dirty(m);
    return 1;
}
int rfm_move_kit_set(rfm_move_t *m, int pad, int p, int value)
{
    const rfm_move_kit_pad_t *d;
    int v;
    if (!rfmv_kit_cap(m) || pad < 0 || pad >= RFM_MOVE_KIT_PADS || p < 0 || p >= RFM_MOVE_KIT_P_N)
        return 0;
    d = CPAD(m, pad);
    v = clampi(value, p_min(p), p_max(p));
    if (p == RFM_MOVE_KIT_P_START)
        v = clampi(v, 0, d->v[RFM_MOVE_KIT_P_END] - GAP);
    else if (p == RFM_MOVE_KIT_P_END)
        v = clampi(v, d->v[RFM_MOVE_KIT_P_START] + GAP, RFM_MOVE_KIT_FRAC);
    else if (p == RFM_MOVE_KIT_P_ATTACK || p == RFM_MOVE_KIT_P_HOLD || p == RFM_MOVE_KIT_P_RELEASE)
        v = ENV_MS[env_idx(v)];
    if (v == d->v[p])
        return 0;
    put(m, pad, p, v);
    return 1;
}
