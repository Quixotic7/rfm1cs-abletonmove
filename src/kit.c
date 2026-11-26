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
 * calls rfm_move_kit_play / _choose / _set.
 *
 * (round A, the owner's 2026-11-25; README "The kit (round A)") pad-first loading: Files opens on the chosen pad's
 * sound (KIT_FIND: its place, folders, row; an empty pad: where the last load came from), the jog at rest
 * RFM_MOVE_KIT_LAND_MS on a sound puts it on the chosen pad (KIT_LOAD) and the pad plays it once, a pad pressed in Files
 * is the target (the list jumps to its sound), the click keeps it and goes to the Pad tab; one Undo for the whole
 * browse (what the pad held when Files was opened for it). The close-up of Start (knob 2, its row) and End (its row's
 * popup): the value large over the frames round the point (KIT_PEAKS), 8 times nearer (Shift: 32, fine), the point a
 * line with a notch, what does not play dithered; heard at rest RFM_MOVE_KIT_AUD_MS (KIT_AUDITION). Slice's Hits: knob 3
 * the sensitivity (the cuts asked again). The Kit tab's Trim silence (KIT_REC's param). Play mode Toggle.
 *
 * (pages, the owner's 2026-11-26; README "The kit (pages)") no rows and no tabs: the jog turns the knob pages Sample
 * Kit, Pad Sample, Pad Sample 2, Stretch, Filter, Pad Mix, Kit Mix as the sequencer's knob pages turn (a detent a
 * page, stopping at the ends; the page's name and the page marks for RFM_MOVE_POPUP_MS, v2_modeName as the ENGINE
 * page's arrival), the knobs always the page's (PAGE_K), drawn as the FX pages' 8 cells under the page's picture
 * (Sample Kit: the 16 lanes). The click: the browser for the chosen pad, as Move's (Back, folders, sounds; Sample Kit
 * and Kit Mix: Kits, Load and Save), a layer with the jog, Back leaving it; Shift + the click: the pad's tools. Shift
 * is fine adjust. */
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
static icon_t I_KIT = { "...........", "####..####.", "####..####.", "####..####.", "####..####.", "...........",
                        "####..####.", "####..####.", "####..####.", "####..####.", "..........." };
static icon_t I_PAD = { "...........", "#########..", "#########..", "##.....##..", "##.....##..", "##.....##..",
                        "##.....##..", "#########..", "#########..", "...........", "..........." };
static icon_t I_SAVE = { "###########", "#.........#", "#..#####..#", "#.........#", "#.........#", "#..#####..#",
                         "#..#####..#", "#..#####..#", "#..#####..#", "###########", "..........." };
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
/* (round C) Freeze: a snowflake */
static icon_t I_FREEZE = { ".....#.....", "..#.###.#..", "...#.#.#...", "#...###...#", ".#...#...#.", "###########",
                           ".#...#...#.", "#...###...#", "...#.#.#...", "..#.###.#..", ".....#....." };
/* (round A) Toggle: play, then stop */
static icon_t I_TOGGLE = { "...........", "#.....####.", "##....####.", "###...####.", "####..####.", "#####.####.",
                           "####..####.", "###...####.", "##....####.", "#.....####.", "..........." };
/* (round B) the pad's filter: Filter (a low-pass slope), Resonance (its peak) */
/* (pages, the owner's browser, as Move's) Back a left arrow, a folder a right arrow, a sample a small wave */
static icon_t I_UP = { "...........", "....#......", "...##......", "..###......", ".#########.", "##########.",
                       ".#########.", "..###......", "...##......", "....#......", "..........." };
static icon_t I_IN = { "...........", "......#....", "......##...", "......###..", ".#########.", ".##########",
                       ".#########.", "......###..", "......##...", "......#....", "..........." };
static icon_t I_SND = { "...........", "....#......", "....#...#..", "..#.#...#..", "..#.##..#..", "#.#.##.##.#",
                        "..#.##..#..", "..#.#...#..", "....#...#..", "....#......", "..........." };
static void ic(mr_canvas_t *cv, icon_t i, int x, int y, int v)
{
    int r, c;
    for (r = 0; r < 11; r++)
        for (c = 0; c < 11 && i[r][c]; c++)
            if (i[r][c] == '#')
                v2_px(cv, x + c, y + r, v);
}

#define KP_FINE RFM_MOVE_KIT_P_N                       /* (round B) the pitch's cents (ENVELOPE's knob 7) */
/* (pages) the KIT page's: the kit's strip (RFM_MOVE_KIT_MIX_*: level, pan, sends), the right half, Trim silence */
enum { KP_KLEVEL = KP_FINE + 1, KP_KPAN, KP_KSEND1, KP_KSEND2, KP_KSEND3, KP_RH, KP_TRIM, KP_ALL };
static const char *const P_NAME[KP_ALL] = { "Pitch", "Start", "End", "Play", "Attack", "Hold", "Release", "Choke",
                                            "Reverse", "Level", "Pan", "Mute", "Stretch", "Send 1", "Send 2", "Send 3",
                                            "Decay", "Sustain", "Filter", "Resonance", "Fine", "Kit Level", "Kit Pan",
                                            "Kit Send 1", "Kit Send 2", "Kit Send 3", "Right Half", "Trim" };
static const char *const MODE_NAME[RFM_MOVE_KIT_M_N] = { "One Shot", "Momentary", "Loop", "Toggle" };
static const char *const MODE_SAY[RFM_MOVE_KIT_M_N] = { "One shot", "Momentary", "Loop", "Toggle" };
static const char *const RH_NAME[RFM_MOVE_KIT_RH_N] = { "Pitches", "In Key", "Off" };
static const char *const SCALE_SHORT[RFM_MOVE_NSCALES] = { "Major", "Minor", "Dorian", "Mixolyd", "PentMaj", "PentMin",
                                                           "HarmMin", "Blues", "Phrygn", "Lydian", "Locrian", "MelMin",
                                                           "Whole", "Dim HW", "Dim WH", "Hungar", "PhrDom", "In Sen",
                                                           "Hirajo", "Iwato", "Pelog", "DblHarm", "Span 8", "Bebop" };
/* (pages, the owner's 2026-11-26: "I want the Jogwheel behaviour to function like it does on the sequencer pages")
 * the jog turns the page, the knobs always follow it (-1 dark and dead); Shift is fine adjust, no page of its own */
static const char *const PG_NAME[RFM_MOVE_KIT_PG_N] = { "Sample Kit", "Pad Sample", "Pad Sample 2", "Stretch",
                                                        "Filter", "Pad Mix", "Kit Mix" };
static const int8_t PAGE_K[RFM_MOVE_KIT_PG_N][8] = {
    { KP_RH, KP_TRIM, -1, -1, -1, -1, -1, -1 },
    { RFM_MOVE_KIT_P_PITCH, KP_FINE, RFM_MOVE_KIT_P_START, RFM_MOVE_KIT_P_END, RFM_MOVE_KIT_P_MODE,
      RFM_MOVE_KIT_P_REVERSE, RFM_MOVE_KIT_P_CHOKE, -1 },
    { RFM_MOVE_KIT_P_ATTACK, RFM_MOVE_KIT_P_HOLD, RFM_MOVE_KIT_P_DECAY, RFM_MOVE_KIT_P_SUSTAIN, RFM_MOVE_KIT_P_RELEASE,
      -1, -1, -1 },
    { RFM_MOVE_KIT_P_STRETCH, -1, -1, -1, -1, -1, -1, -1 },
    { RFM_MOVE_KIT_P_CUTOFF, RFM_MOVE_KIT_P_RESO, -1, -1, -1, -1, -1, -1 },
    { RFM_MOVE_KIT_P_LEVEL, RFM_MOVE_KIT_P_PAN, RFM_MOVE_KIT_P_SEND1, RFM_MOVE_KIT_P_SEND2, RFM_MOVE_KIT_P_SEND3,
      RFM_MOVE_KIT_P_MUTE, -1, -1 },
    { KP_KLEVEL, KP_KPAN, KP_KSEND1, KP_KSEND2, KP_KSEND3, -1, -1, -1 } };
static int kit_page(int pg) { return pg == RFM_MOVE_KIT_PG_KIT || pg == RFM_MOVE_KIT_PG_KITMIX; }   /* (the click: Kits) */
static const rgb_t LOCKC = { 255, 209, 102 };           /* (round B) a step held: the knobs it locks */
static int knob_p(const rfm_move_t *m, int k) { return PAGE_K[m->kit.page % RFM_MOVE_KIT_PG_N][k & 7]; }
static int real_p(int p) { return p == KP_FINE ? RFM_MOVE_KIT_P_PITCH : p; }
static int kit_p(int p) { return p >= KP_KLEVEL; }      /* (the KIT page's: not a pad's setting) */
/* the envelope's times (ms), a detent each */
static const int16_t ENV_MS[] = { 0, 1, 2, 3, 5, 8, 10, 15, 20, 30, 40, 50, 75, 100, 150, 200, 300, 400, 500, 750,
                                  1000, 1500, 2000, 3000, 5000, 10000 };
#define NENV ((int)(sizeof ENV_MS / sizeof ENV_MS[0]))
#define LEVEL_MIN (-60)
#define GAP 16                                          /* START and END keep this much (of RFM_MOVE_KIT_FRAC) apart */
#define NOASK 0xFFFFu
#define GLOW_MS 150                                     /* a pad sounds: bright so long after its note */
enum { PL_N = 3 };                                      /* Files' places: Takes, Samples, Kits */
enum { KT_LOAD, KT_SAVE, KT_N };                        /* Kits' rows (pages: the right half, Trim on the KIT page) */
#define MIX_LEVEL0 100                                  /* the kit's strip at first: as a firmware slot's */
/* (3c) the Sound row's tools (board V): Play, Trim, Normalize, Reverse, Delete */
enum { TL_PLAY, TL_TRIM, TL_NORM, TL_REV, TL_FREEZE, TL_DEL, TL_N };   /* (round C: Freeze) */
static icon_t *const TOOL_ICON[TL_N] = { &I_PLAY, &I_TRIM, &I_NORM, &I_REV, &I_FREEZE, &I_X };
static const char *const TOOL_NAME[TL_N] = { "Play", "Trim", "Normalize", "Reverse", "Freeze", "Delete" };
static const char *const TOOL_DONE[TL_N] = { "", "Trimmed", "Normalized", "Reversed", "Freezing", "" };
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
    v[RFM_MOVE_KIT_P_SUSTAIN] = 100, v[RFM_MOVE_KIT_P_CUTOFF] = 100;   /* (round B: today's sound, the filter open) */
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
    case RFM_MOVE_KIT_P_RELEASE:
    case RFM_MOVE_KIT_P_DECAY: return ENV_MS[NENV - 1];
    case RFM_MOVE_KIT_P_CHOKE: return 4;
    case RFM_MOVE_KIT_P_SUSTAIN:
    case RFM_MOVE_KIT_P_CUTOFF:
    case RFM_MOVE_KIT_P_RESO: return 100;
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
static void send_pad_as(rfm_move_t *m, int k, int own)   /* the pad's sound and settings to the host (round C: own 1,
                                                         * the sound kept: copied into the project's folder) */
{
    rfm_move_kit_pad_t *d = PAD(m, k);
    int p;
    if (d->path[0]) {
        rfm_move_act_t a;
        d->st = RFM_MOVE_KIT_LOADING, d->fresh = 0;
        memset(&a, 0, sizeof a);
        a.frame = m->seq.frame;
        a.kind = RFM_MOVE_ACT_KIT_LOAD, a.index = (uint8_t)k, a.param = (uint8_t)(own != 0);
        a.data = (const uint8_t *)d->path, a.len = (uint32_t)strlen(d->path);
        if (m->fn)
            m->fn(m->ctx, &a);
    } else {
        d->st = RFM_MOVE_KIT_EMPTY;
        act(m, RFM_MOVE_ACT_KIT_LOAD, k, 0, 0, 0, 0, 0, 0, NULL);
    }
    for (p = 0; p < RFM_MOVE_KIT_P_N; p++)
        send_set(m, k, p);
}
static void send_pad(rfm_move_t *m, int k) { send_pad_as(m, k, 0); }
static void send_pad_own(rfm_move_t *m, int k) { send_pad_as(m, k, 1); }
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
static int note_locks(const rfm_move_t *m, int k, int16_t *v);
static void note_out_lk(rfm_move_t *m, int k, int semis, int vel, int track, int lk)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    if (lk && vel)                                      /* (round B: the step's locks on this hit) */
        a.klk_mask = (uint8_t)note_locks(m, k, a.klk);
    a.frame = m->seq.frame;
    a.kind = RFM_MOVE_ACT_KIT_NOTE, a.index = (uint8_t)k, a.value = (int16_t)semis, a.vel = (uint8_t)vel;
    a.down = (uint8_t)(vel > 0), a.track = (uint8_t)(track + 1);
    if (vel)
        m->kit.glow[k & 15] = now(m) ? now(m) : 1;
    if (m->fn)
        m->fn(m->ctx, &a);
}
static void note_out(rfm_move_t *m, int k, int semis, int vel, int track) { note_out_lk(m, k, semis, vel, track, 0); }
/* (seq.c) a sequencer note on track (0..15): a track routed to the kit plays its pad */
void rfmv_kit_seq_note(rfm_move_t *m, int track, int note, int vel)
{
    int k, semis;
    if (!rfmv_kit_cap(m) || track < 0 || track >= RFM_MOVE_SEQ_TRACKS || !(m->kit.route >> track & 1))
        return;
    if ((k = rfm_move_kit_pad_of(track, note, &semis)) < 0)
        return;
    note_out_lk(m, k, semis, vel, track, 1);
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
    t->rh = RFM_MOVE_KIT_RH_PITCHES, t->route = 0, t->cur = 0, t->und = 0, t->undk = 0, t->trim = 1;
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
    if (t->route || t->rh || t->cur || t->name[0] || t->trim != 1 || t->mix[RFM_MOVE_KIT_MIX_LEVEL] != MIX_LEVEL0 ||
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
void rfmv_kit_write(const rfm_move_t *m, uint8_t *p)    /* RFM_MOVE_KIT_BYTES (version 13) */
{
    const rfm_move_kit_t *t = &m->kit;
    int k, i;
    memset(p, 0, RFM_MOVE_KIT_BYTES);
    p[0] = t->rh, w16(p + 1, t->route), p[3] = t->cur;
    memcpy(p + 4, t->mix, RFM_MOVE_KIT_MIX_N);
    p[4 + RFM_MOVE_KIT_MIX_N] = t->trim ? 1 : 0;       /* (round A, version 13) */
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
int rfmv_kit_check(const uint8_t *p, int ver)           /* a version 11 .. 14 kit part valid: 0 */
{
    const int np = ver >= 14 ? RFM_MOVE_KIT_P_N : ver >= 12 ? RFM_MOVE_KIT_P_N_V12 : RFM_MOVE_KIT_P_N_V11;
    int k, i;
    if (p[0] >= RFM_MOVE_KIT_RH_N || (r16s(p + 1) & 0x0FFF) || p[3] >= RFM_MOVE_KIT_PADS)
        return -1;
    if (ver >= 12) {
        for (i = 0; i < RFM_MOVE_KIT_MIX_N; i++)
            if (p[4 + i] > 127)
                return -1;
        if (ver >= 13 && p[4 + RFM_MOVE_KIT_MIX_N] > 1)
            return -1;                                  /* (round A: the Trim silence) */
        for (i = 4 + RFM_MOVE_KIT_MIX_N + (ver >= 13); i < 16; i++)
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
    const int np = ver >= 14 ? RFM_MOVE_KIT_P_N : ver >= 12 ? RFM_MOVE_KIT_P_N_V12 : RFM_MOVE_KIT_P_N_V11;   /* (round B:
                                                         * an older one's Decay 0, Sustain 100, the filter open) */
    int k, i;
    rfmv_kit_clear(m);
    t->rh = p[0], t->route = (uint16_t)r16s(p + 1), t->cur = p[3];
    if (ver >= 12) {
        memcpy(t->mix, p + 4, RFM_MOVE_KIT_MIX_N);
        t->trim = ver >= 13 ? p[4 + RFM_MOVE_KIT_MIX_N] : 1;   /* (round A; before it: on) */
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
static void files_open(rfm_move_t *m, int k);
static void files_close(rfm_move_t *m);
static void browse_keep(rfm_move_t *m);
/* (project.c) a project loaded (or a new one made): its sounds asked for, its settings sent */
void rfmv_kit_loaded_project(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    live_off(m);
    preview_stop(m);
    t->und = 0, t->undk = 0, t->pend_take = 0, t->tools = 0, t->fact = 0, t->saving = 0;
    t->br_pad = 0, t->br_kept = 0, t->land = 0, t->land_id = 0, t->find_pad = 0, t->aud_pad = 0, t->aud = 0;
    t->za_pad = 0, t->zpad = 0;
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
        t->tools = 0, t->turned = 0, t->kpop = -1;
    }
    t->on = 1;
    if (m->s.mode != RFM_MOVE_MODE_SEQ)
        rfmv_mode_set(m, RFM_MOVE_MODE_SEQ);
    q->view = 0;
    rfmv_seq_select(m, trk(t->cur));
    if (t->tab == 2 && !t->br_pad)
        files_open(m, t->cur);                          /* (round A: Files on the pad's sound) */
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
    browse_keep(m);                                     /* (round C: the view left from Files: the sound kept) */
    t->on = 0, t->tools = 0, t->kpop = -1, t->pend_take = 0;
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
    m->kit.kpop = -1, m->kit.sl_sens = 50;
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
    if (m->kit.tab == 2)
        files_close(m);
    m->kit.tab = 0, m->kit.tools = 0;
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
    case RFM_MOVE_KIT_P_RELEASE:
    case RFM_MOVE_KIT_P_DECAY: return ENV_MS[clampi(env_idx(v) + d, 0, NENV - 1)];
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
    if (p == RFM_MOVE_KIT_P_START || p == RFM_MOVE_KIT_P_END)   /* (round A) heard once the knob rests (a turn more:
                                                         * later) */
        m->kit.aud = 1, m->kit.aud_p = (uint8_t)p, m->kit.aud_ms = now(m);
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
/* (round B) the filter's cutoff 0..100 in Hz: 20 x 1000^(v / 100) */
static int cutoff_hz(int v) { return (int)((uint64_t)20u * pitch_q16(clampi(v, 0, 100) * 11959 / 100) / 65536u); }
static void cutoff_text(int v, char *o, int cap)
{
    const int hz = cutoff_hz(v);
    if (v >= 100)
        snprintf(o, (size_t)cap, "Open");
    else if (hz >= 1000)
        snprintf(o, (size_t)cap, "%d.%d kHz", hz / 1000, hz % 1000 / 100);
    else
        snprintf(o, (size_t)cap, "%d Hz", hz);
}
/* a setting's words for value v: row 1 (a row's value: "A 20 ms"), else a popup's ("20 ms") */
static void vtext(const rfm_move_t *m, int k, int p, int v, int row, char *o, int cap)
{
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    switch (p) {
    case KP_FINE: pitch_text(v, o, cap); break;
    case RFM_MOVE_KIT_P_CUTOFF:
        if (row) {
            char b[16];
            cutoff_text(v, b, sizeof b);
            snprintf(o, (size_t)cap, "F %s", b);
        } else
            cutoff_text(v, o, cap);
        break;
    case RFM_MOVE_KIT_P_RESO: snprintf(o, (size_t)cap, row ? "Q %d%%" : "%d%%", v); break;
    case RFM_MOVE_KIT_P_SUSTAIN: snprintf(o, (size_t)cap, row ? "S %d%%" : "%d%%", v); break;
    case RFM_MOVE_KIT_P_DECAY: snprintf(o, (size_t)cap, row ? "D %d ms" : "%d ms", v); break;
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
    case KP_KLEVEL:
    case KP_KSEND1:
    case KP_KSEND2:
    case KP_KSEND3: snprintf(o, (size_t)cap, "%d", v); break;   /* (the Mixer's Kit strip, as it shows them) */
    case KP_KPAN:
        if (v == 64)
            snprintf(o, (size_t)cap, "C");
        else
            snprintf(o, (size_t)cap, "%c %d", v < 64 ? 'L' : 'R', v < 64 ? 64 - v : v - 64);
        break;
    case KP_RH: snprintf(o, (size_t)cap, "%s", RH_NAME[clampi(v, 0, RFM_MOVE_KIT_RH_N - 1)]); break;
    default: snprintf(o, (size_t)cap, "%s", v ? "On" : "Off"); break;
    }
}
/* (pages) a knob's value: the chosen pad's setting, or the kit's (the KIT page) */
static int kval(const rfm_move_t *m, int k, int p)
{
    if (p >= KP_KLEVEL && p <= KP_KSEND3)
        return m->kit.mix[p - KP_KLEVEL];
    if (p == KP_RH)
        return m->kit.rh;
    if (p == KP_TRIM)
        return m->kit.trim != 0;
    return CPAD(m, k)->v[real_p(p)];
}
static void val_text(const rfm_move_t *m, int k, int p, int row, char *o, int cap)
{
    vtext(m, k, p, kval(m, k, p), row, o, cap);
}
/* (pages) a cell's words for value v, 5 characters at most (29 px at 1x) */
static void cell_text(const rfm_move_t *m, int k, int p, int v, char *o, int cap)
{
    static const char *const MS[RFM_MOVE_KIT_M_N] = { "Shot", "Mom", "Loop", "Togl" };
    static const char *const RS[RFM_MOVE_KIT_RH_N] = { "Pitch", "Key", "Off" };
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    switch (p) {
    case RFM_MOVE_KIT_P_PITCH:
        if (d->v[RFM_MOVE_KIT_P_STRETCH])
            snprintf(o, (size_t)cap, "Fit");            /* (stretched: the Pitch ignored) */
        else
            snprintf(o, (size_t)cap, v / 100 ? "%+d" : "0", v / 100);   /* (whole semitones; Fine: the cents) */
        break;
    case KP_FINE: snprintf(o, (size_t)cap, v % 100 ? "%+dc" : "0c", v % 100); break;
    case RFM_MOVE_KIT_P_START:
    case RFM_MOVE_KIT_P_END: {
        const int ms = frac_ms(d, v);
        if (!d->rate || ms < 0)
            snprintf(o, (size_t)cap, "%d%%", (int)(v * 100L / RFM_MOVE_KIT_FRAC));
        else if (ms < 10000)
            snprintf(o, (size_t)cap, "%d.%02d", ms / 1000, ms % 1000 / 10);
        else if (ms < 100000)
            snprintf(o, (size_t)cap, "%d.%d", ms / 1000, ms % 1000 / 100);
        else
            snprintf(o, (size_t)cap, "%d", ms / 1000);
        break;
    }
    case RFM_MOVE_KIT_P_MODE: snprintf(o, (size_t)cap, "%s", MS[clampi(v, 0, RFM_MOVE_KIT_M_N - 1)]); break;
    case RFM_MOVE_KIT_P_STRETCH:
        if (v)
            snprintf(o, (size_t)cap, "%d", v);
        else
            snprintf(o, (size_t)cap, "Off");
        break;
    case RFM_MOVE_KIT_P_LEVEL:
        if (v <= LEVEL_MIN)
            snprintf(o, (size_t)cap, "-inf");
        else
            snprintf(o, (size_t)cap, v > 0 ? "+%ddB" : "%ddB", v);
        break;
    case RFM_MOVE_KIT_P_PAN:
        if (v)
            snprintf(o, (size_t)cap, "%c%d", v < 0 ? 'L' : 'R', v < 0 ? -v : v);
        else
            snprintf(o, (size_t)cap, "C");
        break;
    case RFM_MOVE_KIT_P_ATTACK:
    case RFM_MOVE_KIT_P_HOLD:
    case RFM_MOVE_KIT_P_DECAY:
    case RFM_MOVE_KIT_P_RELEASE:
        if (v < 1000)
            snprintf(o, (size_t)cap, "%dms", v);
        else if (v % 1000)
            snprintf(o, (size_t)cap, "%d.%ds", v / 1000, v % 1000 / 100);
        else
            snprintf(o, (size_t)cap, "%ds", v / 1000);
        break;
    case RFM_MOVE_KIT_P_CHOKE:
        if (v)
            snprintf(o, (size_t)cap, "%d", v);
        else
            snprintf(o, (size_t)cap, "Off");
        break;
    case RFM_MOVE_KIT_P_CUTOFF: {
        const int hz = cutoff_hz(v);
        if (v >= 100)
            snprintf(o, (size_t)cap, "Open");
        else if (hz >= 1000)
            snprintf(o, (size_t)cap, "%d.%dk", hz / 1000, hz % 1000 / 100);
        else
            snprintf(o, (size_t)cap, "%d", hz);
        break;
    }
    case RFM_MOVE_KIT_P_SUSTAIN:
    case RFM_MOVE_KIT_P_RESO:
    case RFM_MOVE_KIT_P_SEND1:
    case RFM_MOVE_KIT_P_SEND2:
    case RFM_MOVE_KIT_P_SEND3: snprintf(o, (size_t)cap, "%d%%", v); break;
    case KP_KPAN:
        if (v == 64)
            snprintf(o, (size_t)cap, "C");
        else
            snprintf(o, (size_t)cap, "%c%d", v < 64 ? 'L' : 'R', v < 64 ? 64 - v : v - 64);
        break;
    case KP_RH: snprintf(o, (size_t)cap, "%s", RS[clampi(v, 0, RFM_MOVE_KIT_RH_N - 1)]); break;
    default: vtext(m, k, p, v, 0, o, cap); break;      /* (Reverse, Mute, Trim: On / Off; the kit's strip: a number) */
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
/* (round B) the loudest of the pad's notes there (0: none): the step key's brightness */
static int step_vel(const rfm_move_t *m, int k, int abs)
{
    const int tr = trk(k), clip = edit_clip_of(m, tr);
    int lo, hi = rfmv_seq_range(m, clip, abs, &lo), i, v = 0;
    for (i = lo; i < hi; i++)
        if (rfm_move_kit_pad_of(tr, m->seq.notes[i].note, NULL) == k && m->seq.notes[i].vel > v)
            v = m->seq.notes[i].vel;
    return v;
}
/* ---- (round B) a kit pad's step locks (move.h RFM_MOVE_KIT_LOCK0): which settings, their encoding */
static int lk_of(int p)
{
    switch (p) {
    case RFM_MOVE_KIT_P_PITCH: return RFM_MOVE_KIT_LK_PITCH;
    case RFM_MOVE_KIT_P_START: return RFM_MOVE_KIT_LK_START;
    case RFM_MOVE_KIT_P_CUTOFF: return RFM_MOVE_KIT_LK_CUTOFF;
    case RFM_MOVE_KIT_P_RESO: return RFM_MOVE_KIT_LK_RESO;
    case RFM_MOVE_KIT_P_LEVEL: return RFM_MOVE_KIT_LK_LEVEL;
    case RFM_MOVE_KIT_P_PAN: return RFM_MOVE_KIT_LK_PAN;
    case RFM_MOVE_KIT_P_DECAY: return RFM_MOVE_KIT_LK_DECAY;
    default: return -1;
    }
}
static int lk_max(int l) { static const uint8_t X[RFM_MOVE_KIT_LK_N] = { 48, 254, 100, 100, 66, 100, NENV - 1 }; return X[l]; }
static int lk_id(int l, int k) { return RFM_MOVE_KIT_LOCK0 + l * 4 + (k & 3); }
static int lk_enc(int l, int v)
{
    switch (l) {
    case RFM_MOVE_KIT_LK_PITCH: return clampi((v + (v < 0 ? -50 : 50)) / 100 + 24, 0, 48);
    case RFM_MOVE_KIT_LK_START: return clampi((int)(((int64_t)v * 254 + RFM_MOVE_KIT_FRAC / 2) / RFM_MOVE_KIT_FRAC), 0, 254);
    case RFM_MOVE_KIT_LK_LEVEL: return clampi(v + 60, 0, 66);
    case RFM_MOVE_KIT_LK_PAN: return clampi(v + 50, 0, 100);
    case RFM_MOVE_KIT_LK_DECAY: return env_idx(v);
    default: return clampi(v, 0, 100);
    }
}
static int lk_dec(int l, int e)
{
    switch (l) {
    case RFM_MOVE_KIT_LK_PITCH: return (e - 24) * 100;
    case RFM_MOVE_KIT_LK_START: return (int)((int64_t)e * RFM_MOVE_KIT_FRAC / 254);
    case RFM_MOVE_KIT_LK_LEVEL: return e - 60;
    case RFM_MOVE_KIT_LK_PAN: return e - 50;
    case RFM_MOVE_KIT_LK_DECAY: return ENV_MS[clampi(e, 0, NENV - 1)];
    default: return e;
    }
}
/* the pad's locks at step abs of its edited clip: any (bits of RFM_MOVE_KIT_LK_*) */
static int step_locks(const rfm_move_t *m, int k, int abs)
{
    const int clip = edit_clip_of(m, trk(k));
    int l, b = 0;
    for (l = 0; l < RFM_MOVE_KIT_LK_N; l++)
        b |= (rfmv_seq_lock_get(m, clip, abs, lk_id(l, k)) >= 0) << l;
    return b;
}
static void locks_clear(rfm_move_t *m, int k, int clip, int abs)
{
    int l;
    for (l = 0; l < RFM_MOVE_KIT_LK_N; l++)
        if (rfmv_seq_lock_get(m, clip, abs, lk_id(l, k)) >= 0)
            rfmv_seq_lock_set(m, clip, abs, lk_id(l, k), -1);
}
/* (seq.c) a note-on of a kit pad from its clip: the step's locks on it (the setting's units; mask 0 none) */
static int note_locks(const rfm_move_t *m, int k, int16_t *v)
{
    const rfm_move_seq_t *q = &m->seq;
    int l, b = 0;
    if (!q->kit_cok)
        return 0;
    for (l = 0; l < RFM_MOVE_KIT_LK_N; l++) {
        const int e = rfmv_seq_lock_get(m, q->kit_cclip, q->kit_cstep, lk_id(l, k));
        if (e >= 0)
            b |= 1 << l, v[l] = (int16_t)lk_dec(l, e);
    }
    return b;
}
static void step_clear(rfm_move_t *m, int k, int abs)
{
    const int tr = trk(k), clip = edit_clip_of(m, tr);
    int lo, hi = rfmv_seq_range(m, clip, abs, &lo);
    while (hi-- > lo)
        if (rfm_move_kit_pad_of(tr, m->seq.notes[hi].note, NULL) == k)
            rfmv_seq_note_del(m, clip, abs, m->seq.notes[hi].note);
    locks_clear(m, k, clip, abs);                       /* (round B: its locks with them) */
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
    t->undk_trim = t->trim;
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
    t->trim = t->undk_trim;
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
    t->br_kept = 0, t->land = 0, t->land_id = 0, t->aud_pad = 0;   /* (round A: a browse's Undo; the next landing a new one) */
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
            {
                const int st = m->seq.notes[hi].step;
                rfmv_seq_note_del(m, clip, st, m->seq.notes[hi].note);
                locks_clear(m, k, clip, st);            /* (round B) */
            }
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
    t->tab = 0, t->tools = 0, t->turned = 0;
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
    d->keep = (uint8_t)(tool == TL_NORM ? 1 : tool == TL_REV ? 2 : tool == TL_FREEZE ? 4 : 0);
    if (tool == TL_FREEZE)
        K(m)->frz_ms = now(m);                          /* (round C: the pad blinks while it renders) */
    act(m, RFM_MOVE_ACT_KIT_EDIT, k, 0, 0,
        tool == TL_TRIM ? RFM_MOVE_KIT_ED_TRIM : tool == TL_NORM ? RFM_MOVE_KIT_ED_NORMALIZE :
        tool == TL_FREEZE ? RFM_MOVE_KIT_ED_FREEZE : RFM_MOVE_KIT_ED_REVERSE, 0, 0, 0, NULL);
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
    t->sl_ask = 0;
    slice_act(m, RFM_MOVE_KIT_SL_ASK, step_us(m, trk(t->sl_first)), t->sl_sens ? t->sl_sens : 50);
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
    t->sl_on = 0, t->sl_ok = 0, t->kpop = -1, t->sl_ask = 0, t->tap_on = 0;
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
        send_pad_own(m, k);                             /* (round C: the take copied into the project's folder once) */
    }
    if (n == 1)
        snprintf(w, sizeof w, "Pad %d", t->sl_first + 1);
    else
        snprintf(w, sizeof w, "Pads %d-%d", t->sl_first + 1, t->sl_first + n);
    slice_leave(m);
    choose(m, t->sl_first);
    t->tab = 0, t->tools = 0;
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
    t->sl_ok = 1, t->sl_n = (uint8_t)n, t->sl_frames = frames, t->sl_rate = rate ? rate : 48000, t->tap_on = 0;
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
        {                                               /* (round C: param 1, kept: the project's own copy) */
            rfm_move_act_t a;
            memset(&a, 0, sizeof a);
            a.kind = RFM_MOVE_ACT_KIT_LOAD, a.index = (uint8_t)t->cur, a.param = 1;
            a.value = RFM_MOVE_KIT_PL_TAKES, a.take = t->pend_take;
            if (m->fn)
                m->fn(m->ctx, &a);
        }
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
        t->tools = 0, t->kpop = -1;
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
        if (t->tab == 2 && t->br_pad != kp + 1)
            files_open(m, kp);                          /* (round A: Files' target, the list on its sound) */
        return;
    }
    if (kp >= 0) {
        k = kp, semis = 0;
        choose(m, k);
        t->blink_ms = now(m);
        if (t->tab == 2 && t->br_pad != kp + 1)
            files_open(m, kp);                          /* (round A: another pad: a new browse for it; the same
                                                         * pad plays what landed on it) */
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
                if (rfmv_seq_find(m, clip, abs, n) >= 0) {
                    rfmv_seq_note_del(m, clip, abs, n);
                    if (!step_has(m, k, abs))
                        locks_clear(m, k, clip, abs);   /* (round B) */
                } else
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
/* (pages, the owner's browser) a folder's rows: Back (row -1, drawn first) then its own; the first shown (visual: 0 Back)
 * and the first of its own shown */
static int list_vfirst(const rfm_move_kit_t *t)
{
    return first_of(t->fsel[t->depth] + 1, (t->known ? t->total : 0x7FFE) + 1);
}
static int list_first(const rfm_move_kit_t *t) { const int v = list_vfirst(t); return v ? v - 1 : 0; }
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
static int in_list(const rfm_move_t *m) { return m->kit.tab == 2 && m->kit.place; }
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
    (void)sel;
    first = list_first(t);
    for (i = first; i < first + (list_vfirst(t) ? 3 : 2) && (!t->known || i < t->total); i++)
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
    t->fsel[t->depth] = (int16_t)clampi(t->fsel[t->depth], -1, t->total ? t->total - 1 : -1);   /* (-1: Back) */
    for (i = 0; rows && i < n && from + i < t->total; i++) {
        int k, v = -1;
        for (k = 0; k < RFM_MOVE_KIT_CACHE && v < 0; k++)
            if (t->rc[k].ok && t->rc[k].pl == place && t->rc[k].f == folder && t->rc[k].i == from + i)
                v = k;
        for (k = 0; k < RFM_MOVE_KIT_CACHE && v < 0; k++)
            if (!t->rc[k].ok)
                v = k;
        if (v < 0) {                                    /* (one the screen does not show) */
            const int c0 = list_first(t);
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
/* ---- (round A) pad-first loading */
/* where the pad's sound sits asked (the answer moves Files there); an empty pad: Files stays where the last load was */
static void find_ask(rfm_move_t *m, int k)
{
    rfm_move_kit_t *t = K(m);
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    t->find_pad = 0;
    if (!d->path[0])
        return;
    t->find_pad = (uint8_t)(k + 1);
    act(m, RFM_MOVE_ACT_KIT_FIND, k, 0, 0, 0, 0, 0, 0, d->path);
}
/* (round C) the browse's sound kept: the pad's sound now copied into the project's folder (KIT_KEEP; once a landing) */
static void browse_keep(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    if (t->br_pad && t->br_kept && !t->br_own) {
        t->br_own = 1;
        act(m, RFM_MOVE_ACT_KIT_KEEP, t->br_pad - 1, 0, 0, 0, 0, 0, 0, NULL);
    }
}
/* Files opened for the chosen pad (or a pad pressed there): a new browse, its Undo not kept yet */
static void files_open(rfm_move_t *m, int k)
{
    rfm_move_kit_t *t = K(m);
    browse_keep(m);                                     /* (round C: the browse before, another pad's: kept) */
    t->br_pad = (uint8_t)(k + 1), t->br_kept = 0, t->br_own = 0, t->land = 0, t->land_id = 0, t->aud_pad = 0;
    find_ask(m, k);
}
static void files_close(rfm_move_t *m)
{
    browse_keep(m);                                     /* (round C: Files left, the jog resting on a sound: kept) */
    K(m)->br_pad = 0, K(m)->br_kept = 0, K(m)->br_own = 0, K(m)->land = 0, K(m)->find_pad = 0;
}
/* the jog moved in the list: the row it rests on loaded once it is at rest RFM_MOVE_KIT_LAND_MS */
static void land_mark(rfm_move_t *m) { K(m)->land = 1, K(m)->land_ms = now(m); }
/* a row's sound onto the browse's pad (its Undo kept at the first: one for the whole browse); heard once it comes */
static void land_load(rfm_move_t *m, const rfm_move_kit_row_t *r, int heard)
{
    rfm_move_kit_t *t = K(m);
    const int k = t->cur;
    rfm_move_kit_pad_t *d = PAD(m, k);
    if (t->br_pad != k + 1)
        files_open(m, k), t->find_pad = 0;              /* (a new browse: this pad's) */
    if (!t->br_kept)
        und_keep(m, k), t->br_kept = 1;
    d->st = RFM_MOVE_KIT_LOADING, d->fresh = 1, d->keep = 0;
    act(m, RFM_MOVE_ACT_KIT_LOAD, k, 0, 0, t->place, r->id, 0, 0, NULL);
    t->br_own = 0;                                      /* (round C: this sound kept when Files is left) */
    t->land_id = r->id, t->land_pl = t->place, t->aud_pad = (uint8_t)(heard ? k + 1 : 0);
    rfmv_seq_dirty(m);
}
static void land_poll(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    const rfm_move_kit_row_t *r;
    if (!t->land || now(m) - t->land_ms < RFM_MOVE_KIT_LAND_MS)
        return;
    if (!in_list(m) || !rfmv_kit_active(m)) {
        t->land = 0;
        return;
    }
    if (t->fsel[t->depth] < 0) {
        t->land = 0;                                    /* (the Back row: nothing) */
        return;
    }
    if (!(r = row_get(m, t->place, t->folder[t->depth], t->fsel[t->depth])))
        return;                                         /* (its row not come yet: when it does) */
    t->land = 0;
    if (r->kind != RFM_MOVE_KIT_ROW_FILE || (r->id == t->land_id && t->place == t->land_pl))
        return;                                         /* (a folder, a kit: nothing; the pad's own already) */
    land_load(m, r, 1);
}
int rfm_move_kit_found(rfm_move_t *m, int pad, int place, int depth, const uint32_t *folder, const int16_t *sel,
                       uint32_t id)
{
    rfm_move_kit_t *t = K(m);
    int i;
    if (!rfmv_kit_cap(m) || pad < 0 || pad >= RFM_MOVE_KIT_PADS || t->find_pad != pad + 1)
        return 0;                                       /* (an older ask's) */
    t->find_pad = 0;
    if (place < RFM_MOVE_KIT_PL_TAKES || place > RFM_MOVE_KIT_PL_KITS || depth < 0 || depth > RFM_MOVE_KIT_DEPTH ||
        !folder || !sel || t->tab != 2)
        return 0;
    t->place = (uint8_t)place, t->psel = (uint8_t)(place - 1), t->depth = (uint8_t)depth;
    for (i = 0; i <= depth; i++)
        t->folder[i] = i ? folder[i] : 0, t->fsel[i] = sel[i] < 0 ? 0 : sel[i];
    t->land_id = id, t->land_pl = (uint8_t)place, t->land = 0;
    rows_forget(m);
    need(m);
    touched(m);
    rfmv_say_check(m);
    return rfm_move_kit_shown(m);
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
                {   /* (2026-11-26, the owner's older project: its tracks 13..16 still went to firmwares, so the pad
                     * played nothing) a new sound on a pad whose track the kit does not play: an empty track becomes
                     * the kit's; one with notes stays, said */
                    const int tr = trk(k);
                    char tn[16];
                    snprintf(tn, sizeof tn, "Track %d", tr + 1);
                    if (!(m->kit.route >> tr & 1)) {
                        if (!rfmv_seq_track_used(m, tr)) {
                            rfmv_kit_route_set(m, tr, 1);
                            msg(m, tn, "To Kit");
                        } else
                            msg(m, tn, "Not Kit");
                    }
                }
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
        const int frozen = d->keep == 4;
        if (d->fresh) {                                 /* (a new sound: its whole region) */
            d->fresh = 0;
            put(m, k, RFM_MOVE_KIT_P_START, 0), put(m, k, RFM_MOVE_KIT_P_END, RFM_MOVE_KIT_FRAC);
        } else if (d->keep == 2) {                      /* (3c: reversed, the region where it went) */
            const int a = d->v[RFM_MOVE_KIT_P_START], b = d->v[RFM_MOVE_KIT_P_END];
            put(m, k, RFM_MOVE_KIT_P_START, RFM_MOVE_KIT_FRAC - b), put(m, k, RFM_MOVE_KIT_P_END, RFM_MOVE_KIT_FRAC - a);
        }
        d->keep = 0;
        if (frozen) {                                   /* (round C) Freeze: what it baked back to neutral */
            static const uint8_t NP[] = { RFM_MOVE_KIT_P_PITCH, RFM_MOVE_KIT_P_STRETCH, RFM_MOVE_KIT_P_REVERSE,
                                          RFM_MOVE_KIT_P_ATTACK, RFM_MOVE_KIT_P_HOLD, RFM_MOVE_KIT_P_DECAY,
                                          RFM_MOVE_KIT_P_SUSTAIN, RFM_MOVE_KIT_P_RELEASE, RFM_MOVE_KIT_P_CUTOFF,
                                          RFM_MOVE_KIT_P_RESO, RFM_MOVE_KIT_P_LEVEL, RFM_MOVE_KIT_P_PAN };
            int16_t dv[RFM_MOVE_KIT_P_N];
            char pn[8];
            defaults(dv);
            for (i = 0; i < (int)sizeof NP; i++)
                put(m, k, NP[i], dv[NP[i]]);
            snprintf(pn, sizeof pn, "Pad %d", k + 1);
            msg(m, "Frozen", pn);
        }
        if (m->kit.aud_pad == k + 1) {                  /* (round A) a landed sound: the pad plays it once */
            m->kit.aud_pad = 0;
            note_out(m, k, 0, 100, trk(k));
            if (d->v[RFM_MOVE_KIT_P_MODE] != RFM_MOVE_KIT_M_ONESHOT)
                m->kit.tool_note = (uint8_t)(k + 1), m->kit.tool_ms = now(m);
        }
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
        t->psel = (uint8_t)clampi(t->psel + d, 0, PL_N - 1);
        return;
    }
    {
        int16_t *sel = &t->fsel[t->depth];
        *sel = (int16_t)clampi(*sel + d, -1, t->known ? t->total - 1 : 0x7FFE);   /* (-1: the Back row) */
        need(m);
        land_mark(m);                                   /* (round A: at rest, the sound onto the pad) */
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
            t->tap_on = 0, slice_ask(m);                /* (round B: Tap's answer one slice, the whole take) */
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
    if (t->tab == 2) {                                  /* Files (its own layer) */
        files_turn(m, d);
        return;
    }
    if (t->tab == 1) {                                  /* Kits: Load, Save */
        t->kitrow = (uint8_t)clampi(t->kitrow + d, 0, KT_N - 1);
        return;
    }
    {                                                   /* (pages) a detent a page, as the sequencer's knob pages
                                                         * (seq_ui.c's jog: clamped at the ends); its name and the
                                                         * page marks for RFM_MOVE_POPUP_MS after the last detent */
        const int pg = clampi((int)t->page + d, 0, RFM_MOVE_KIT_PG_N - 1);
        if (pg != t->page)
            memset(t->kacc, 0, sizeof t->kacc);
        t->page = (uint8_t)pg;
        t->turned = 1, t->turn_ms = now(m);
    }
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
        if (t->ti == TL_TRIM || t->ti == TL_NORM || t->ti == TL_REV || t->ti == TL_FREEZE) {
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
    if (t->tab == 0) {                                  /* (pages) the pages' click: Shift, the pad's tools; the KIT
                                                         * page, Kits; else Files for the chosen pad */
        if (m->seq.mods & SEQ_M_SHIFT) {
            m->seq.mods_used |= SEQ_M_SHIFT;
            t->tools = 1, t->ti = 0;
        } else if (kit_page(t->page))
            t->tab = 1, t->kitrow = 0;
        else {
            preview_stop(m);
            t->tab = 2, t->fact = 0;
            files_open(m, k);                           /* (round A: Files on the pad's sound) */
            need(m);
        }
        return;
    }
    if (t->tab == 1) {                                  /* (3b) Load: Files' Kits; Save */
        if (t->kitrow == KT_LOAD) {
            preview_stop(m);
            t->tab = 2, t->place = RFM_MOVE_KIT_PL_KITS, t->psel = RFM_MOVE_KIT_PL_KITS - 1;
            t->depth = 0, t->folder[0] = 0, t->fsel[0] = 0;
            files_open(m, k), t->find_pad = 0;          /* (round A: the kits; no sound looked for) */
            rows_forget(m);
            need(m);
        } else {
            if (!t->saving) {
                t->saving = 1;
                act(m, RFM_MOVE_ACT_KIT_SAVE, 0, 0, 0, 0, 0, 0, 0, NULL);
            }
            msg(m, "Saving", NULL);
        }
        return;
    }
    if (!t->place) {                                    /* a place */
        t->place = (uint8_t)(t->psel + 1), t->depth = 0, t->folder[0] = 0, t->fsel[0] = 0;
        rows_forget(m);
        need(m);
        return;
    }
    if (t->fsel[t->depth] < 0) {                        /* (the owner's browser) the Back row: a folder up, the root */
        preview_stop(m);
        if (t->depth)
            t->depth--, rows_forget(m), need(m);
        else
            t->psel = (uint8_t)(t->place - 1), t->place = 0, rows_forget(m);
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
        {                                               /* a sound: kept on the chosen pad (round A: loaded as the
                                                         * jog landed; not yet: now), Files closed: the page */
            char pn[8];
            preview_stop(m);
            if (!(r->id == t->land_id && t->place == t->land_pl && t->br_pad == k + 1))
                land_load(m, r, 0);
            snprintf(pn, sizeof pn, "Pad %d", k + 1);
            msg(m, r->name, pn);
            files_close(m);
            t->tab = 0, t->turned = 0;
        }
    }
}
int rfmv_kit_back_has(const rfm_move_t *m)
{
    const rfm_move_kit_t *t = &m->kit;
    return rfmv_kit_shown(m) && (t->tools || t->fact || t->pend_take || t->kpop >= 0 || merge_up(m) || t->sl_on ||
                                 t->tab != 0);
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
    else if (t->tools)
        t->tools = 0;
    else if (t->tab == 2) {                             /* Files: a folder up, the places, then closed (the page) */
        preview_stop(m);
        if (t->depth)
            t->depth--, rows_forget(m), need(m);
        else if (t->place)
            t->psel = (uint8_t)(t->place - 1), t->place = 0, rows_forget(m);
        else
            files_close(m), t->tab = 0;
    } else if (t->tab == 1)
        t->tab = 0;                                     /* (Kits closed) */
    /* (the pages: nothing more; Back never leaves the view) */
    return 1;
}

/* ================================================================ the knobs === */
/* (round B) a step held (the Kit view, not slicing): the knobs lock the chosen pad's hit there; the step shown */
static int lock_layer(const rfm_move_t *m) { return m->kit.st_down && !m->kit.sl_on && !m->kit.tools && !m->kit.pend_take; }
static int held_step(const rfm_move_t *m)              /* the first step held (0..15), -1 */
{
    int s;
    for (s = 0; s < 16; s++)
        if (m->kit.st_down >> s & 1)
            return s;
    return -1;
}
/* (3c) the knobs: dark and dead under the tools, the Copy menu, a waiting take; the slices' cut on 1 and 2; (round B)
 * a step held: only the lockable ones */
static int knob_dead_p(const rfm_move_t *m, int k, int p)   /* (p: the knob's setting on its page) */
{
    const rfm_move_kit_t *t = &m->kit;
    if (t->sl_on)                                       /* (round A: knob 3 the Hits' sensitivity) */
        return k > 2 || (k == 2 && t->sl_meth != RFM_MOVE_KIT_SM_HITS);
    if (lock_layer(m))
        return lk_of(p) < 0;
    return p < 0 || t->tools || merge_up(m) || t->pend_take;
}
static int knob_dead(const rfm_move_t *m, int k) { return knob_dead_p(m, k, knob_p(m, k)); }
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
    t->kpop = (int8_t)k, t->kpop_ms = now(m), t->msg_ms = 0, t->vpop = 0;
    t->lpop = (uint8_t)(lock_layer(m) ? k + 1 : 0);
    if (t->lpop)
        t->st_used |= t->st_down;                       /* (a lock looked at: the step's release toggles nothing) */
    touched(m);
}
/* (round B) a lock turned: the chosen pad's hits on the held steps get their own value (n detents) */
static void lock_turn(rfm_move_t *m, int k, int n)
{
    rfm_move_kit_t *t = K(m);
    const int p = knob_p(m, k), l = lk_of(p), pad = t->cur, clip = edit_clip_of(m, trk(pad));
    int s;
    if (l < 0 || !n)
        return;
    for (s = 0; s < 16; s++)
        if (t->st_down >> s & 1) {
            const int abs = m->seq.page * RFM_MOVE_SEQ_PAGE + s;
            int e;
            if (!step_has(m, pad, abs))
                continue;                               /* (no hit of the pad there: nothing to lock) */
            if ((e = rfmv_seq_lock_get(m, clip, abs, lk_id(l, pad))) < 0)
                e = lk_enc(l, CPAD(m, pad)->v[p]);
            if (!t->lk_grp)
                rfmv_seq_group(m), t->lk_grp = 1;
            rfmv_seq_lock_set(m, clip, abs, lk_id(l, pad), clampi(e + n, 0, lk_max(l)));
            rfmv_seq_dirty(m);
        }
    t->st_used |= t->st_down;
}
/* the lock the popup shows: the first held step's (its value, else the pad's own); 0: no hit there */
static int lock_val(const rfm_move_t *m, int p, int *v)
{
    const int l = lk_of(p), s = held_step(m), pad = m->kit.cur, abs = m->seq.page * RFM_MOVE_SEQ_PAGE + (s < 0 ? 0 : s);
    int e;
    if (l < 0 || s < 0 || !step_has(m, pad, abs))
        return 0;
    e = rfmv_seq_lock_get(m, edit_clip_of(m, trk(pad)), abs, lk_id(l, pad));
    *v = e >= 0 ? lk_dec(l, e) : CPAD(m, pad)->v[p];
    return 1;
}
/* (round B) a step held + the volume knob: the chosen pad's hits there, their velocity */
static void vel_turn(rfm_move_t *m, int d)
{
    rfm_move_kit_t *t = K(m);
    const int pad = t->cur, tr = trk(pad), clip = edit_clip_of(m, tr);
    int s, lo, hi, i;
    t->vpop = 1, t->vpop_ms = now(m), t->kpop = -1, t->lpop = 0, t->msg_ms = 0;
    touched(m);
    for (s = 0; s < 16; s++)
        if (t->st_down >> s & 1) {
            const int abs = m->seq.page * RFM_MOVE_SEQ_PAGE + s;
            hi = rfmv_seq_range(m, clip, abs, &lo);
            for (i = lo; i < hi; i++) {
                const rfm_move_note_t n = m->seq.notes[i];
                if (rfm_move_kit_pad_of(tr, n.note, NULL) != pad)
                    continue;
                if (!t->vel_grp)
                    rfmv_seq_group(m), t->vel_grp = 1;
                rfmv_seq_note_put(m, clip, abs, n.note, clampi(n.vel + d, 1, 127), n.len, n.micro);
                rfmv_seq_dirty(m);
                hi = rfmv_seq_range(m, clip, abs, &lo); /* (the same notes, put back in place) */
            }
        }
    t->st_used |= t->st_down;
}
/* (pages) the KIT page's knobs: the kit's strip (as the Mixer's Kit strip turns it), the right half, Trim silence */
static void kit_turn(rfm_move_t *m, int p, int n)
{
    rfm_move_kit_t *t = K(m);
    if (p >= KP_KLEVEL && p <= KP_KSEND3)
        rfmv_kit_mix_turn(m, p - KP_KLEVEL, n);
    else if (p == KP_RH) {
        const int v = clampi(t->rh + n, 0, RFM_MOVE_KIT_RH_N - 1);
        if (v != t->rh)
            t->rh = (uint8_t)v, rfmv_seq_dirty(m);
    } else if (p == KP_TRIM) {
        const int v = clampi(t->trim + n, 0, 1);
        if (v != t->trim)
            t->trim = (uint8_t)v, rfmv_seq_dirty(m);
    }
}
static void knob_turn(rfm_move_t *m, int k, int d)
{
    rfm_move_kit_t *t = K(m);
    int p, fine = (m->seq.mods & SEQ_M_SHIFT) != 0;
    int n;
    static const uint8_t DIV[KP_ALL] = { 2, 1, 1, 5, 2, 2, 2, 5, 5, 2, 1, 5, 3, 1, 1, 1, 2, 1, 1, 1, 1,
                                         1, 1, 1, 1, 1, 5, 5 };
    if (!d)
        return;
    if (knob_dead(m, k))
        return;
    p = knob_p(m, k);
    if (fine)
        m->seq.mods_used |= SEQ_M_SHIFT;
    if (t->sl_on) {                                     /* (3c) the chosen slice's cut: 1 coarse, 2 (or Shift) fine */
        t->kpop = (int8_t)k, t->kpop_ms = now(m), t->msg_ms = 0;
        touched(m);
        if (k == 2) {                                   /* (round A) knob 3: the Hits' sensitivity, asked again at rest */
            const int v = clampi(t->sl_sens + d, 1, 100);
            if (v != t->sl_sens)
                t->sl_sens = (uint8_t)v, t->sl_ask = 1, t->sl_ms = now(m);
            return;
        }
        cut_move(m, d, k == 1 || fine);
        return;
    }
    t->kpop = (int8_t)k, t->kpop_ms = now(m), t->msg_ms = 0, t->vpop = 0;
    t->lpop = (uint8_t)(lock_layer(m) ? k + 1 : 0);
    touched(m);
    if (p == KP_FINE)
        fine = 1;                                       /* (ENVELOPE's knob 7: the cents) */
    if (fine && !t->lpop && (p == RFM_MOVE_KIT_P_PITCH || p == RFM_MOVE_KIT_P_START || p == KP_FINE))
        n = d;
    else {
        int a = t->kacc[k];
        if ((a > 0 && d < 0) || (a < 0 && d > 0))
            a = 0;
        a += d;
        n = a / DIV[p];
        t->kacc[k] = (int8_t)(a - n * DIV[p]);
    }
    if (!n)
        return;
    if (t->lpop)
        lock_turn(m, k, n);
    else if (kit_p(p))
        kit_turn(m, p, n);
    else
        turn_p(m, real_p(p), n, fine);
}

/* ---- (round B) Tap: the take plays from its start at the first step pressed (a cut at 0), each further press a cut
 * where the sound heard is (the core's clock less RFM_MOVE_KIT_TAP_LAG_US), 16 slices at most; a press after the take
 * ended: a new pass, its cuts replacing the last's */
static uint32_t tap_lag(const rfm_move_t *m) { return m->seq.lead_us ? m->seq.lead_us : RFM_MOVE_KIT_TAP_LAG_US; }
static int tap_pos(const rfm_move_t *m, uint32_t *pos)  /* the frame heard now; 0: the pass is over */
{
    const rfm_move_kit_t *t = &m->kit;
    const uint64_t el = m->seq.now_us - t->tap_us0, lag = tap_lag(m);
    const uint64_t f = el > lag ? (el - lag) * (t->sl_rate ? t->sl_rate : 48000) / 1000000u : 0;
    if (!t->tap_on || f >= t->sl_frames)
        return 0;
    *pos = (uint32_t)f;
    return 1;
}
static void tap_clear(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    if (t->sl_ok != 1)
        return;
    if (t->tap_on)
        slice_act(m, RFM_MOVE_KIT_SL_PLAY, 0, 0);       /* (the pass stopped) */
    t->tap_on = 0, t->sl_n = 1, t->sl_cut[0] = 0, t->sl_cut[1] = t->sl_frames, t->sl_cur = 0;
    msg(m, "Cuts", "Cleared");
}
static void tap_press(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    uint32_t pos;
    touched(m);
    if (t->sl_ok != 1 || !t->sl_frames)
        return;
    if (!tap_pos(m, &pos)) {                            /* a new pass: from the start, a cut at 0 */
        t->tap_on = 1, t->tap_us0 = m->seq.now_us;
        t->sl_n = 1, t->sl_cut[0] = 0, t->sl_cut[1] = t->sl_frames, t->sl_cur = 0;
        slice_act(m, RFM_MOVE_KIT_SL_PLAY, 0, t->sl_frames);
        return;
    }
    {
        const uint32_t gap = t->sl_rate / 100 ? t->sl_rate / 100 : 1;   /* (10 ms from a cut, from the end) */
        if (t->sl_n >= RFM_MOVE_KIT_SLICES || pos < t->sl_cut[t->sl_n - 1] + gap || pos + gap > t->sl_frames)
            return;
        t->sl_cut[t->sl_n] = pos, t->sl_n++, t->sl_cut[t->sl_n] = t->sl_frames, t->sl_cur = (uint8_t)(t->sl_n - 1);
    }
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
    if (ev->type == MR_EV_STEP && !ev->value && i >= 0 && i < 16 && (t->tap_down >> i & 1)) {
        t->tap_down = (uint16_t)(t->tap_down & ~(1u << i));   /* (round B: Tap's) */
        return 1;
    }
    if (ev->type == MR_EV_STEP && !ev->value && i >= 0 && i < 16 && (t->st_down >> i & 1)) {
        const uint16_t bit = (uint16_t)(1u << i);
        t->st_down &= (uint16_t)~bit;
        if (!t->st_down) {                              /* (round B: the next hold its own undo; its popups gone) */
            if (t->lpop)
                t->kpop = -1;
            t->lk_grp = 0, t->vel_grp = 0, t->lpop = 0, t->vpop = 0;
        }
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
        if (t->sl_on) {                                 /* (round B) slicing: the steps Tap's, else dark and dead */
            t->tap_down = (uint16_t)(t->tap_down | 1u << i);
            if (t->sl_meth == RFM_MOVE_KIT_SM_TAP) {
                if (q->mods & SEQ_M_DELETE)
                    q->mods_used |= SEQ_M_DELETE, tap_clear(m);
                else
                    tap_press(m);
            }
            return 1;
        }
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
        if (i == 8 && t->st_down && !q->smenu && !t->sl_on && ev->value) {
            vel_turn(m, ev->value);                     /* (round B) a step held + the volume knob: its velocity */
            return 1;
        }
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
            {                                           /* (round A: param, the Trim silence) */
                rfm_move_act_t a;
                memset(&a, 0, sizeof a);
                a.frame = m->seq.frame;
                a.kind = RFM_MOVE_ACT_KIT_REC, a.index = (uint8_t)k, a.down = 1, a.value = m->s.smp[RFM_MOVE_SMP_MODE];
                a.param = (uint8_t)(t->trim ? 1 : 0);
                if (m->fn)
                    m->fn(m->ctx, &a);
            }
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
/* ---- (round A) the close-up of Start and End */
static int kpop_up(const rfm_move_t *m);
/* the setting the close-up shows now (START, END), -1 none */
static int zoom_p(const rfm_move_t *m)
{
    const rfm_move_kit_t *t = &m->kit;
    int p = -1;
    if (!rfmv_kit_shown(m) || msg_up(m) || rec_running(m) || merge_up(m) || t->sl_on || t->pend_take)
        return -1;
    if (kpop_up(m))
        p = t->lpop ? -1 : knob_p(m, t->kpop);
    return p == RFM_MOVE_KIT_P_START || p == RFM_MOVE_KIT_P_END ? p : -1;
}
static int zoom_of(const rfm_move_t *m) { return (m->seq.mods & SEQ_M_SHIFT) ? RFM_MOVE_KIT_ZOOM_FINE : RFM_MOVE_KIT_ZOOM; }
/* its window: frames [*lo, *lo + *span) of the chosen pad's sound, the point in the middle (lo < 0 near the start) */
static int zoom_win(const rfm_move_t *m, int p, int64_t *lo, uint32_t *span)
{
    const rfm_move_kit_pad_t *d = CPAD(m, m->kit.cur);
    uint32_t sp;
    if (!ready(d) || p < 0)
        return 0;
    sp = d->frames / (uint32_t)zoom_of(m);
    if (sp < 64)
        sp = 64;
    *lo = (int64_t)((uint64_t)d->frames * (uint32_t)d->v[p] / RFM_MOVE_KIT_FRAC) - sp / 2, *span = sp;
    return 1;
}
/* its peaks asked (the part of the window in the sound), again when it has moved a sixteenth or its zoom changed */
static void zoom_need(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    const int p = zoom_p(m);
    int64_t lo, a, b;
    uint32_t span;
    if (!zoom_win(m, p, &lo, &span))
        return;
    a = lo < 0 ? 0 : lo, b = lo + span > CPAD(m, t->cur)->frames ? CPAD(m, t->cur)->frames : lo + span;
    if (b <= a)
        return;
    if (t->za_pad == t->cur + 1 && (a > t->za_from ? a - t->za_from : t->za_from - a) < span / 16 &&
        ((b - a) > t->za_count ? (b - a) - t->za_count : t->za_count - (b - a)) < span / 16)
        return;
    t->za_pad = (uint8_t)(t->cur + 1), t->za_from = (uint32_t)a, t->za_count = (uint32_t)(b - a);
    act(m, RFM_MOVE_ACT_KIT_PEAKS, t->cur, 0, 0, RFM_MOVE_KIT_PEAKS, 0, t->za_from, t->za_count, NULL);
}
int rfm_move_kit_peaks(rfm_move_t *m, int pad, uint32_t from, uint32_t count, int n, const uint8_t *peaks)
{
    rfm_move_kit_t *t = K(m);
    if (!rfmv_kit_cap(m) || pad < 0 || pad >= RFM_MOVE_KIT_PADS || !count || n <= 0 || !peaks)
        return 0;
    t->zpad = (uint8_t)(pad + 1), t->zfrom = from, t->zcount = count;
    t->zn = (uint8_t)(n > RFM_MOVE_KIT_PEAKS ? RFM_MOVE_KIT_PEAKS : n);
    memcpy(t->zpk, peaks, t->zn);
    return zoom_p(m) >= 0 && pad == t->cur;
}
/* Start / End at rest: a moved start heard from it (a second at most), a moved end the region's last second */
static void audition(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    const rfm_move_kit_pad_t *d = CPAD(m, t->cur);
    uint32_t s, e, sec, from, count;
    t->aud = 0;
    if (!ready(d))
        return;
    s = (uint32_t)((uint64_t)d->frames * (uint32_t)d->v[RFM_MOVE_KIT_P_START] / RFM_MOVE_KIT_FRAC);
    e = (uint32_t)((uint64_t)d->frames * (uint32_t)d->v[RFM_MOVE_KIT_P_END] / RFM_MOVE_KIT_FRAC);
    sec = d->rate ? d->rate : 48000;
    if (e <= s)
        return;
    count = e - s < sec ? e - s : sec;
    from = t->aud_p == RFM_MOVE_KIT_P_END ? e - count : s;
    act(m, RFM_MOVE_ACT_KIT_AUDITION, t->cur, 0, 0, 0, 0, from, count, NULL);
}
/* (seq advance) the timed parts: a tool's held-mode audition let go */
void rfmv_kit_poll(rfm_move_t *m)
{
    rfm_move_kit_t *t = K(m);
    if (t->mg_a && !(m->seq.mods & SEQ_M_COPY))
        merge_off(m);                                   /* (3c) Copy let go before a choice: nothing done */
    if (t->tool_note && now(m) - t->tool_ms >= 400) {
        const int k = t->tool_note - 1;
        if (CPAD(m, k)->v[RFM_MOVE_KIT_P_MODE] == RFM_MOVE_KIT_M_TOGGLE)
            note_out(m, k, 0, 100, trk(k));             /* (round A: Toggle's next tap stops it) */
        note_out(m, k, 0, 0, trk(k));
        t->tool_note = 0;
    }
    if (!rfmv_kit_cap(m))
        return;
    land_poll(m);                                       /* (round A) the jog at rest on a sound: onto the pad */
    if (t->aud && now(m) - t->aud_ms >= RFM_MOVE_KIT_AUD_MS)
        audition(m);                                    /* .. Start / End at rest: heard */
    if (t->sl_ask && t->sl_on && now(m) - t->sl_ms >= RFM_MOVE_KIT_SENS_MS)
        slice_ask(m);                                   /* .. the sensitivity at rest: the cuts again */
    zoom_need(m);
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
    return t->kpop >= 0 && knob_p(m, t->kpop) >= 0 && ((t->kdown >> t->kpop & 1) || now(m) - t->kpop_ms < RFM_MOVE_POPUP_MS);
}
/* (3b, board D) Pitch, Stretch, Start or End changing: the steps it lasts on the step keys */
static int len_pop(const rfm_move_t *m)
{
    const rfm_move_kit_t *t = &m->kit;
    if (msg_up(m))
        return 0;
    return kpop_up(m) && !t->lpop && len_param(knob_p(m, t->kpop));
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
    const int choke_pop = t->kpop >= 0 && knob_p(m, t->kpop) == RFM_MOVE_KIT_P_CHOKE;
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
                v = t->tab == 2 ? (blink_on(m) ? RFMV_WHITE : sc(RFMV_WHITE, 60)) : RFMV_WHITE;
            if (d->st == RFM_MOVE_KIT_LOADING && d->keep == 4)   /* (round C) Freezing: blinking */
                v = (now(m) - t->frz_ms) % 300u < 150u ? RFMV_WHITE : sc(ROWC[i / 4], 60);
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
            const int vel = step_vel(m, k0, abs);       /* (round B: soft dim, hard bright; 100 as before) */
            const int br = clampi(30 + (vel * 190 + 50) / 100, 0, 255);
            rgb_t v = vel ? sc(ROWC[k0 / 4], in ? br : br * 60 / 220) : in ? sc(RFMV_WHITE, 16) : RFMV_OFF;
            if (vel && (now(m) / 500u) % 2u && step_locks(m, k0, abs))
                v = sc(ROWC[k0 / 4], (in ? br : 60) / 4);   /* (round B: locked: a slow
                                                         * blink, 1 s round) */
            if (in && pos == abs && (q->playing || q->start_pending))
                v = sc(RFMV_WHITE, 200);                /* (the playhead) */
            if (lp)                                     /* (3b, board D) the steps the sound lasts, the first brightest */
                v = i < ln ? sc(ROWC[k0 / 4], i == 0 ? 255 : 110) : RFMV_OFF;
            if (t->sl_on)                               /* (3c, board P: dark under the slices) */
                v = RFMV_OFF;
            if (t->sl_on && t->sl_meth == RFM_MOVE_KIT_SM_TAP && t->sl_ok == 1) {   /* (round B) Tap: a step a cut
                                                         * made (white), the slice the playhead is in (its colour) */
                uint32_t hp;
                if (i < t->sl_n)
                    v = sc(RFMV_WHITE, 150);
                if (tap_pos(m, &hp) && i < t->sl_n && hp >= t->sl_cut[i] && hp < t->sl_cut[i + 1])
                    v = slice_rgb(m);
                if (t->tap_down >> i & 1)
                    v = RFMV_WHITE;
            }
            if (t->st_down >> i & 1)
                v = RFMV_WHITE;
            c[L_STEP0 + i] = v;
        }
    }
    for (i = 0; i < 8; i++) {                           /* the knobs: the page's (pages: pink, dark where the page has
                                                         * none; a step held: the lockable ones yellow) */
        const rgb_t kc = t->sl_on ? MENU_KNOB : lock_layer(m) ? LOCKC : KIT;
        c[L_RING0 + i] = knob_dead(m, i) ? RFMV_OFF : (t->kdown >> i & 1) || t->kpop == i ? RFMV_WHITE : sc(kc, 150);
    }
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
/* (pages) a layer's header: its icon (Files, Kits) at the top left; the chosen pad's number at 2x top right */
static void pad_no(mr_canvas_t *cv, int pad)
{
    char n[8];
    snprintf(n, sizeof n, "%d", pad);
    v2_T(cv, 128 - v2_tw(n, 2), 0, n, 2, 1);
}
static void header(mr_canvas_t *cv, icon_t i, int pad)
{
    ic(cv, i, 2, 1, 1);
    if (pad)
        pad_no(cv, pad);
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
    const int sp = n > 5 ? 21 : 25;                     /* (round C: six tools fit closer) */
    int i;
    for (i = 0; i < n; i++) {
        const int x = 64 - n * sp / 2 + 7 + i * sp;
        if (i == chosen)
            v2_R(cv, x - 2, y - 2, 15, 15, 1);
        ic(cv, *icons[i], x, y, i != chosen);
    }
    if (name)
        v2_TC(cv, 64, y + 19, name, 2, 1);
}
/* (round B) the envelope: attack, hold, decay, sustain, release as parts of w (in 1/100), sus the sustain's level
 * (0..100); thick 1..5 the part turned (A H D S R) */
static void ahdsr(mr_canvas_t *cv, int x0, int y0, int w, int ht, const int pt[5], int sus, int thick)
{
    const int xa = x0 + w * pt[0] / 100, xh = xa + w * pt[1] / 100, xd = xh + w * pt[2] / 100, xs = xd + w * pt[3] / 100;
    const int xr = xs + w * pt[4] / 100, ys = y0 - ht * sus / 100;
    v2_L(cv, x0, y0, xa, y0 - ht);
    v2_L(cv, xa, y0 - ht, xh, y0 - ht);
    v2_L(cv, xh, y0 - ht, xd, ys);
    v2_L(cv, xd, ys, xs, ys);
    v2_L(cv, xs, ys, xr, y0);
    v2_dotH(cv, x0, y0, w);
    if (thick == 1)
        v2_L(cv, x0 + 1, y0, xa + 1, y0 - ht), v2_L(cv, x0 + 2, y0, xa + 2, y0 - ht);
    if (thick == 2)
        v2_R(cv, xa, y0 - ht + 1, xh - xa, 2, 1);
    if (thick == 3)
        v2_L(cv, xh + 1, y0 - ht, xd + 1, ys), v2_L(cv, xh + 2, y0 - ht, xd + 2, ys);
    if (thick == 4)
        v2_R(cv, xd, ys + 1, xs - xd, 2, 1), v2_R(cv, xd, ys - 2, xs - xd, 2, 1);
    if (thick == 5)
        v2_L(cv, xs - 1, ys, xr - 1, y0), v2_L(cv, xs - 2, ys, xr - 2, y0);
}
static int isqrt(int v)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v)
        r++;
    return r;
}
/* the pad's envelope as parts of 100 (a square root of the times, so short ones show): a, h, d, the sustain, r */
static void env_parts(const rfm_move_t *m, int k, int pt[5])
{
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    int w[5], tot = 0, i, used = 0;
    w[0] = isqrt(d->v[RFM_MOVE_KIT_P_ATTACK]), w[1] = isqrt(d->v[RFM_MOVE_KIT_P_HOLD]);
    w[2] = isqrt(d->v[RFM_MOVE_KIT_P_DECAY]), w[4] = isqrt(d->v[RFM_MOVE_KIT_P_RELEASE]);
    w[3] = (w[0] + w[1] + w[2] + w[4]) / 2 > 10 ? (w[0] + w[1] + w[2] + w[4]) / 2 : 10;   /* (held, or to the end) */
    for (i = 0; i < 5; i++)
        tot += w[i];
    for (i = 0; i < 5; i++)
        pt[i] = i == 3 ? 0 : w[i] * 95 / tot, used += pt[i];
    pt[3] = 95 - used;
}
/* the pad's wave over [start, end) as n columns 2 px apart about y0 (h each way) */
static void pad_wave_n(const rfm_move_t *m, mr_canvas_t *cv, int k, int y0, int h, int whole, int n)
{
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    const int s = whole ? 0 : d->v[RFM_MOVE_KIT_P_START], e = whole ? RFM_MOVE_KIT_FRAC : d->v[RFM_MOVE_KIT_P_END];
    int c;
    if (!d->npk || d->st != RFM_MOVE_KIT_OK)
        return;
    for (c = 0; c < n; c++) {
        const int64_t den = n * (int64_t)RFM_MOVE_KIT_FRAC;
        const int i0 = (int)(((int64_t)s * n + (int64_t)(e - s) * c) * d->npk / den);
        int i1 = (int)(((int64_t)s * n + (int64_t)(e - s) * (c + 1)) * d->npk / den), i, pk = 0, a;
        if (i1 <= i0)
            i1 = i0 + 1;
        for (i = i0; i < i1 && i < d->npk; i++)
            pk = d->pk[i] > pk ? d->pk[i] : pk;
        a = (pk * h + 127) / 255;
        v2_R(cv, c * 2, y0 - a, 1, a ? a * 2 : 1, 1);
    }
}
static void pad_wave(const rfm_move_t *m, mr_canvas_t *cv, int k, int y0, int h, int whole)
{
    pad_wave_n(m, cv, k, y0, h, whole, 64);
}
/* (round B) the filter's curve over x0..x0 + w: flat at yb to the cutoff, the peak (the resonance), then down a pixel
 * every 2; clamped to ylo..yhi; mark: the cutoff's place dotted */
static void filter_curve(mr_canvas_t *cv, int x0, int w, int yb, int ylo, int yhi, int c, int q, int mark)
{
    const int xc = x0 + c * w / 100;
    int x, py = -1;
    for (x = x0; x < x0 + w; x++) {
        const int dx = x - xc;
        int y = yb - (dx > -6 && dx < 6 ? q * (6 - (dx < 0 ? -dx : dx)) / 60 : 0) + (dx > 0 ? dx / 2 : 0);
        y = clampi(y, ylo, yhi);
        if (py >= 0)
            v2_L(cv, x - 1, py, x, y);
        py = y;
    }
    if (mark)
        v2_dotV(cv, clampi(xc, 0, 127), ylo, yhi - ylo);
}
/* (pages, the owner's browser) a row: its icon on the left (Back, a folder, a sample, a saved kit), its name */
static void brow(mr_canvas_t *cv, int y, icon_t i, const char *name, int chosen)
{
    char nm[RFM_MOVE_KIT_NAME];
    const int maxc = (128 - 20) / 12;
    snprintf(nm, sizeof nm, "%s", name);
    if ((int)strlen(nm) > maxc)
        nm[maxc] = 0;
    rowi(cv, y, i, nm, chosen);
}
static void file_row(mr_canvas_t *cv, int y, const rfm_move_kit_row_t *r, int chosen)
{
    if (!r) {
        if (chosen)
            v2_R(cv, 0, y, 128, 15, 1);
        return;
    }
    brow(cv, y, r->kind == RFM_MOVE_KIT_ROW_KIT ? I_KIT : r->kind == RFM_MOVE_KIT_ROW_DIR ? I_IN : I_SND, r->name, chosen);
}
static void files_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_kit_t *t = &m->kit;
    int i;
    header(cv, I_DIR, t->cur + 1);
    if (!t->place) {                                    /* the root: Takes, Samples, Kits (folders) */
        static const char *const PN[PL_N] = { "Takes", "Samples", "Kits" };
        for (i = 0; i < PL_N; i++)
            brow(cv, 16 + i * 16, I_IN, PN[i], i == t->psel);
        return;
    }
    {                                                   /* Back first, then the folder's own */
        const int sel = t->fsel[t->depth], v0 = list_vfirst(t);
        for (i = 0; i < 3; i++) {
            const int v = v0 + i, r = v - 1;
            if (t->known && r >= t->total)
                break;
            if (v == 0)
                brow(cv, 16, I_UP, "Back", sel < 0);
            else
                file_row(cv, 16 + i * 16, row_get(m, t->place, t->folder[t->depth], r), r == sel);
        }
    }
}
/* ---- (pages) a knob's cell, as the FX pages' 8 cells (fw.c, 0.5.0): its short name, its value under it, both at 1x
 * and 5 characters at most, 32 px wide, four across, two rows (knobs 1..4 above 5..8); a knob with none: a dash */
static const char *const P_SHORT[KP_ALL] = { "Pitch", "Start", "End", "Play", "Att", "Hold", "Rel", "Choke", "Rev",
                                             "Level", "Pan", "Mute", "Strch", "Send1", "Send2", "Send3", "Decay", "Sus",
                                             "Filt", "Reso", "Fine", "Level", "Pan", "Send1", "Send2", "Send3", "Right",
                                             "Trim" };
static void cells(const rfm_move_t *m, mr_canvas_t *cv)
{
    const int lk = lock_layer(m);
    int k;
    for (k = 0; k < 8; k++) {
        const int p = knob_p(m, k), x = (k % 4) * 32, y = k < 4 ? 25 : 45;
        char v[16];
        if (p < 0 || (lk && lk_of(p) < 0)) {            /* (dark and dead: a dash; a step held: the lockable ones) */
            v2_R(cv, x + 12, y + 9, 7, 1, 1);
            continue;
        }
        v2_TC(cv, x + 16, y, P_SHORT[p], 1, 1);
        if (lk) {                                       /* (the held step's hit: its lock, else the pad's own) */
            int lv;
            if (lock_val(m, p, &lv))
                cell_text(m, m->kit.cur, p, lv, v, sizeof v);
            else
                snprintf(v, sizeof v, "-");
        } else
            cell_text(m, m->kit.cur, p, kval(m, m->kit.cur, p), v, sizeof v);
        v[5] = 0;
        v2_TC(cv, x + 16, y + 10, v, 1, 1);
    }
}
/* (Sample Kit) the whole kit's steps: 16 lanes of 4 px, pad 1 at the bottom, each its pad's hits over the 16 steps
 * shown (8 px a step); a pad with no sound: empty; the chosen pad's lane underlined; while playing, each lane's
 * playhead a tick */
static void lanes(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    int k, s;
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++) {
        const int y = (15 - k) * 4, tr = trk(k);
        const int pos = (q->playing || q->start_pending) && q->tr[tr].active == q->tr[tr].sel ? rfmv_seq_clip_pos(m, tr) : -1;
        if (!has_sound(CPAD(m, k)))
            continue;
        for (s = 0; s < 16; s++) {
            const int abs = q->page * RFM_MOVE_SEQ_PAGE + s;
            if (step_has(m, k, abs))
                v2_R(cv, s * 8 + 1, y, 6, 3, 1);
            else
                v2_px(cv, s * 8 + 4, y + 1, 1);
            if (pos == abs)
                v2_R(cv, s * 8, y, 1, 3, 1);
        }
        if (k == m->kit.cur)
            v2_R(cv, 0, y + 3, 128, 1, 1);
    }
}
/* the page's picture, compact, in x 0..101, y 0..22 (the pad's number top right) */
static void page_pic(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_kit_t *t = &m->kit;
    const int k = t->cur;
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    char nm[24];
    int pt[5], i;
    switch (t->page) {
    case RFM_MOVE_KIT_PG_SAMPLE:                        /* the wave over the region (a join's seam); no name (the
                                                         * click's browser opens on it) */
    case RFM_MOVE_KIT_PG_STRETCH:                       /* .. Stretch: and the steps it lasts */
        if (t->page == RFM_MOVE_KIT_PG_SAMPLE)
            pad_wave_n(m, cv, k, 11, 10, 0, 50);
        else
            pad_wave_n(m, cv, k, 8, 7, 0, 50);
        {
            const int a = d->v[RFM_MOVE_KIT_P_START], b = d->v[RFM_MOVE_KIT_P_END];
            if (d->cut && d->st == RFM_MOVE_KIT_OK && d->cut > a && d->cut < b)
                v2_dotV(cv, (int)((int64_t)(d->cut - a) * 100 / (b - a)), 0, 22);
        }
        if (t->page == RFM_MOVE_KIT_PG_STRETCH) {
            const int n = region_steps(m, k);
            for (i = 0; i <= 16; i++)
                v2_R(cv, i * 99 / 16, 17, 1, i % 4 == 0 ? 4 : 2, 1);
            v2_R(cv, 0, 21, n * 99 / 16, 2, 1);
        }
        break;
    case RFM_MOVE_KIT_PG_ENV:                           /* (round B) the AHDSR */
        env_parts(m, k, pt);
        ahdsr(cv, 0, 21, 98, 19, pt, d->v[RFM_MOVE_KIT_P_SUSTAIN], 0);
        break;
    case RFM_MOVE_KIT_PG_FILTER:                        /* (round B) the filter's curve */
        filter_curve(cv, 0, 98, 12, 1, 22, d->v[RFM_MOVE_KIT_P_CUTOFF], d->v[RFM_MOVE_KIT_P_RESO], 0);
        break;
    case RFM_MOVE_KIT_PG_PADMIX:                        /* the pad's level as a bar, its pan a mark on it */
        {
            const int lv = d->v[RFM_MOVE_KIT_P_LEVEL], w = lv <= LEVEL_MIN ? 0 : (lv - LEVEL_MIN) * 98 / (6 - LEVEL_MIN);
            v2_R(cv, 0, 12, 98, 1, 1);
            v2_R(cv, 0, 9, w, 7, 1);
            v2_R(cv, 49 + d->v[RFM_MOVE_KIT_P_PAN] * 48 / 50, 4, 1, 17, 1);
            if (d->v[RFM_MOVE_KIT_P_MUTE])              /* (muted: the bar hollow) */
                v2_R(cv, 1, 10, w > 2 ? w - 2 : 0, 5, 0);
        }
        break;
    default:                                            /* Kit Mix: the kit's name, its 16 pads (a sound filled, empty a
                                                         * line, muted dithered, the chosen underlined) */
        snprintf(nm, sizeof nm, "%s", t->name[0] ? t->name : "Kit");
        nm[16] = 0;
        v2_T(cv, 0, 0, nm, 1, 1);
        for (i = 0; i < RFM_MOVE_KIT_PADS; i++) {
            const rfm_move_kit_pad_t *e = CPAD(m, i);
            const int x = i * 6;
            if (!has_sound(e))
                v2_R(cv, x, 19, 5, 1, 1);
            else if (e->v[RFM_MOVE_KIT_P_MUTE]) {
                int xx, yy;
                for (yy = 11; yy < 20; yy++)
                    for (xx = x; xx < x + 5; xx++)
                        if (((xx + yy) & 1) == 0)
                            v2_px(cv, xx, yy, 1);
            } else
                v2_R(cv, x, 11, 5, 9, 1);
            if (i == k)
                v2_R(cv, x, 22, 5, 1, 1);
        }
        break;
    }
}
static void page_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    if (m->kit.page == RFM_MOVE_KIT_PG_KIT) {            /* (Sample Kit: the lanes, the whole screen) */
        lanes(m, cv);
        return;
    }
    page_pic(m, cv);
    pad_no(cv, m->kit.cur + 1);
    cells(m, cv);
}
/* (round A) the close-up of Start / End: the value large, the frames round the point (a column every 2 px, the
 * host's peaks), the point a line down the middle with a notch on top, what does not play dithered */
static void zoom_screen(const rfm_move_t *m, mr_canvas_t *cv, int p)
{
    const rfm_move_kit_t *t = &m->kit;
    const rfm_move_kit_pad_t *d = CPAD(m, t->cur);
    char v[32];
    int64_t lo, s0, e0;
    uint32_t span;
    int c, y, sc3;
    val_text(m, t->cur, p, 0, v, sizeof v);
    sc3 = v2_tw(v, 3) <= 128 ? 3 : 2;
    v2_TC(cv, 64, 0, v, sc3, 1);
    if (!zoom_win(m, p, &lo, &span))
        return;
    s0 = (int64_t)((uint64_t)d->frames * (uint32_t)d->v[RFM_MOVE_KIT_P_START] / RFM_MOVE_KIT_FRAC);
    e0 = (int64_t)((uint64_t)d->frames * (uint32_t)d->v[RFM_MOVE_KIT_P_END] / RFM_MOVE_KIT_FRAC);
    for (c = 0; c < 64; c++) {
        const int64_t f0 = lo + (int64_t)span * c / 64, f1 = lo + (int64_t)span * (c + 1) / 64;
        int pk = 0, a;
        if (f1 <= 0 || f0 >= (int64_t)d->frames)
            continue;                                   /* (past the sound's ends: nothing) */
        if (t->zpad == t->cur + 1 && t->zn && t->zcount) {
            int64_t i0 = (f0 - (int64_t)t->zfrom) * t->zn / (int64_t)t->zcount;
            int64_t i1 = (f1 - (int64_t)t->zfrom) * t->zn / (int64_t)t->zcount, i;
            if (i1 <= i0)
                i1 = i0 + 1;
            for (i = i0 < 0 ? 0 : i0; i < i1 && i < t->zn; i++)
                pk = t->zpk[i] > pk ? t->zpk[i] : pk;
        }
        a = (pk * 16 + 127) / 255;
        if (f0 >= s0 && f0 < e0)
            v2_R(cv, c * 2, 44 - a, 1, a ? a * 2 : 1, 1);
        else                                            /* (not played: half lit) */
            for (y = 44 - a; y < (a ? 44 + a : 45); y++)
                if (((y + c) & 1) == 0)
                    v2_px(cv, c * 2, y, 1);
    }
    v2_R(cv, 64, 24, 1, 40, 1);                         /* the point */
    v2_R(cv, 61, 24, 7, 1, 1), v2_R(cv, 62, 25, 5, 1, 1), v2_R(cv, 63, 26, 3, 1, 1);   /* .. its notch */
}
/* a setting's popup (a row's click, a knob touched or turned) */
static void popup(const rfm_move_t *m, mr_canvas_t *cv, int p)
{
    const int k = m->kit.cur;
    const rfm_move_kit_pad_t *d = CPAD(m, k);
    char v[32];
    int pt[5];
    if (p == RFM_MOVE_KIT_P_START || p == RFM_MOVE_KIT_P_END) {   /* (round A) the close-up */
        zoom_screen(m, cv, p);
        return;
    }
    val_text(m, k, p, 0, v, sizeof v);
    if (p == RFM_MOVE_KIT_P_MODE) {                     /* (board E) how it plays, with its envelope (round A: four) */
        static icon_t *const P[RFM_MOVE_KIT_M_N] = { &I_PLAY, &I_PAD, &I_LOOP, &I_TOGGLE };
        v2_TC(cv, 64, 0, "Play", 2, 1);
        icon_row(cv, P, RFM_MOVE_KIT_M_N, d->v[p], NULL, 18);
        v2_TC(cv, 64, 32, MODE_NAME[clampi(d->v[p], 0, RFM_MOVE_KIT_M_N - 1)], 2, 1);
        env_parts(m, k, pt);
        ahdsr(cv, 14, 63, 100, 12, pt, d->v[RFM_MOVE_KIT_P_SUSTAIN], 0);
        return;
    }
    if (p == RFM_MOVE_KIT_P_ATTACK || p == RFM_MOVE_KIT_P_HOLD || p == RFM_MOVE_KIT_P_DECAY ||
        p == RFM_MOVE_KIT_P_SUSTAIN || p == RFM_MOVE_KIT_P_RELEASE) {   /* (board F; round B: the five stages) */
        v2_TC(cv, 64, 0, P_NAME[p], 2, 1);
        v2_TC(cv, 64, 17, v, 2, 1);
        env_parts(m, k, pt);
        ahdsr(cv, 4, 60, 120, 22, pt, d->v[RFM_MOVE_KIT_P_SUSTAIN],
              p == RFM_MOVE_KIT_P_ATTACK ? 1 : p == RFM_MOVE_KIT_P_HOLD ? 2 : p == RFM_MOVE_KIT_P_DECAY ? 3
              : p == RFM_MOVE_KIT_P_SUSTAIN ? 4 : 5);
        return;
    }
    if (p == RFM_MOVE_KIT_P_CUTOFF || p == RFM_MOVE_KIT_P_RESO) {   /* (round B) the filter's curve, the cutoff's place */
        v2_TC(cv, 64, 0, P_NAME[p], 2, 1);
        v2_TC(cv, 64, 17, v, 2, 1);
        filter_curve(cv, 4, 120, 50, 34, 63, d->v[RFM_MOVE_KIT_P_CUTOFF], d->v[RFM_MOVE_KIT_P_RESO],
                     p == RFM_MOVE_KIT_P_CUTOFF);
        return;
    }
    if (p == KP_RH) {                                   /* (board S) the right half: its layout, the key it plays */
        int root, scale;
        key_of(m, trk(k), &root, &scale);
        if (m->kit.rh == RFM_MOVE_KIT_RH_INKEY)
            snprintf(v, sizeof v, "%s %s", rfmv_note_names[root % 12], SCALE_SHORT[scale % RFM_MOVE_NSCALES]);
        else
            snprintf(v, sizeof v, "%s", m->kit.rh == RFM_MOVE_KIT_RH_PITCHES ? "16 Semis" : "Off");
        v2_titleBox(cv, m->kit.rh == RFM_MOVE_KIT_RH_OFF ? "Right" : RH_NAME[m->kit.rh % RFM_MOVE_KIT_RH_N], v, 64, 128);
        return;
    }
    if (p >= KP_KLEVEL && p <= KP_KSEND3) {             /* (the Kit strip's values: a bar, pan from the middle) */
        const int x = kval(m, k, p);
        v2_titleBox(cv, P_NAME[p], v, 64, 124);
        v2_vbar(cv, p == KP_KPAN ? (x - 64) / 63.0 : x / 127.0, p == KP_KPAN, 4, 120, 52);
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
/* (round B) a held step's lock: "Step 5", the setting, its value for that hit (the pad's own until locked) */
static void lock_popup(const rfm_move_t *m, mr_canvas_t *cv, int p)
{
    char t[24], v[32];
    int lv;
    snprintf(t, sizeof t, "Step %d %s", held_step(m) + 1, P_NAME[p]);
    if (v2_tw(t, 2) > 128)
        snprintf(t, sizeof t, "Step %d", held_step(m) + 1), v2_TC(cv, 64, 21, P_NAME[p], 1, 1);
    v2_TC(cv, 64, 3, t, 2, 1);
    if (!lock_val(m, p, &lv)) {
        v2_TC(cv, 64, 34, "No Hit", 2, 1);
        return;
    }
    vtext(m, m->kit.cur, p, lv, 0, v, sizeof v);
    v2_TC(cv, 64, v2_tw(v, 3) <= 128 ? 32 : 36, v, v2_tw(v, 3) <= 128 ? 3 : 2, 1);
    ic(cv, I_DOT, 0, 52, 1);                            /* (a lock: the dot, as a step key's blink) */
}
static int held_vel(const rfm_move_t *m)
{
    const int s = held_step(m);
    return s < 0 ? 0 : step_vel(m, m->kit.cur, m->seq.page * RFM_MOVE_SEQ_PAGE + s);
}
static int vpop_up(const rfm_move_t *m) { return m->kit.vpop && (m->kit.st_down || now(m) - m->kit.vpop_ms < RFM_MOVE_POPUP_MS); }
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
    static const char *const W[RFM_MOVE_KIT_SM_N] = { "Hits", "Equal", "Equal", "Equal", "Steps", "Tap" };
    static const uint8_t N[RFM_MOVE_KIT_SM_N] = { 0, 16, 8, 4, 0, 0 };
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
        {                                               /* (round B) Tap: the playhead over the wave */
            uint32_t hp;
            if (t->sl_meth == RFM_MOVE_KIT_SM_TAP && tap_pos(m, &hp))
                v2_R(cv, slice_x(t, hp), 2, 2, 30, 1);
        }
        for (i = 0; i <= t->sl_n; i++)                  /* (the cuts between the slices; the ends where they cut) */
            if ((i > 0 && i < t->sl_n) || (i == 0 && t->sl_cut[0] > 0) || (i == t->sl_n && t->sl_cut[i] < t->sl_frames))
                v2_dotV(cv, slice_x(t, t->sl_cut[i]), 5, 26);
    }
    if (t->kpop == 2 && ((t->kdown >> 2 & 1) || now(m) - t->kpop_ms < RFM_MOVE_POPUP_MS)) {   /* (round A) */
        snprintf(v, sizeof v, "Sens %d", t->sl_sens);
        rowi(cv, 33, I_SLICE, v, 1);
    } else if (knob && t->sl_ok == 1) {
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
    if (vpop_up(m)) {                                   /* (round B) a held step + the volume knob */
        char ttl[16], b[8];
        const int v = held_vel(m);
        snprintf(ttl, sizeof ttl, "Step %d Velo", held_step(m) + 1);
        snprintf(b, sizeof b, "%d", v);
        v2_titleBox(cv, ttl, v ? b : "-", 64, 124);
        v2_vbar(cv, v / 127.0, 0, 4, 120, 52);
        return 1;
    }
    if (kpop_up(m)) {
        if (t->lpop)
            lock_popup(m, cv, knob_p(m, t->kpop));
        else
            popup(m, cv, knob_p(m, t->kpop));
        return 1;
    }
    if (t->tab == 2 && t->fact) {                       /* (3b, board K) a folder's actions: Open, All To Pads */
        static icon_t *const A[2] = { &I_DIR, &I_KIT };
        icon_row(cv, A, 2, t->fai, t->fai ? "All To Pads" : "Open", 26);
        return 1;
    }
    if (t->tools) {                                     /* (board V) Shift + the click: the pad's tools: Play, Trim,
                                                         * Normalize, Reverse, Freeze, Delete */
        pad_wave(m, cv, k, 12, 10, 1);
        icon_row(cv, TOOL_ICON, TL_N, t->ti, TOOL_NAME[t->ti % TL_N], 28);
        return 1;
    }
    if (t->tab == 2) {
        files_screen(m, cv);
        return 1;
    }
    if (t->tab == 1) {                                  /* (board Q) Kits: Load, Save; the kit's name */
        header(cv, I_KIT, 0);
        if (t->name[0])
            v2_T(cv, 128 - v2_tw(t->name, 2), 0, t->name, 2, 1);
        rowi(cv, 16, I_DIR, "Load", t->kitrow == KT_LOAD);
        rowi(cv, 32, I_SAVE, "Save", t->kitrow == KT_SAVE);
        return 1;
    }
    if (t->turned && now(m) - t->turn_ms < RFM_MOVE_POPUP_MS) {   /* (pages) the jog turning: the page's name and
                                                         * the page marks, as the sequencer's pages (v2_modeName,
                                                         * the ENGINE page's arrival) */
        v2_modeName(cv, PG_NAME[t->page % RFM_MOVE_KIT_PG_N], RFM_MOVE_KIT_PG_N, t->page % RFM_MOVE_KIT_PG_N);
        return 1;
    }
    page_screen(m, cv);
    return 1;
}
int rfmv_kit_timed(const rfm_move_t *m)
{
    const rfm_move_kit_t *t = &m->kit;
    int i;
    if (!rfmv_kit_active(m))
        return 0;
    if (t->vpop || (t->st_down && t->kpop >= 0))
        return 1;                                       /* (round B: the held step's popups) */
    for (i = 0; i < 16; i++)                            /* (round B: a locked step blinks) */
        if (step_locks(m, t->cur, m->seq.page * RFM_MOVE_SEQ_PAGE + i) && step_has(m, t->cur, m->seq.page * RFM_MOVE_SEQ_PAGE + i))
            return 1;
    if (t->land || t->aud || t->sl_ask || msg_up(m) || rec_running(m) || t->pend_take || merge_up(m) || t->sl_on || t->tab == 2 || t->tool_note || to_kit(m) ||
        (t->tab == 0 && t->page == RFM_MOVE_KIT_PG_KIT && (m->seq.playing || m->seq.start_pending)) ||
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
        if (t->kpop == 2) {
            snprintf(o, (size_t)cap, "Sensitivity %d", t->sl_sens);
            *subj = 0xA7A;
        } else {
            snprintf(o, (size_t)cap, "Slice: %s, slice %d", v, t->sl_cur + 1);
            *subj = 0xA78, *stamp = 0;
        }
    } else if (t->pend_take) {
        snprintf(o, (size_t)cap, "%s", t->pend_i ? "Slice" : "To pad");
        *subj = 0xA79, *stamp = 0;
    } else if (vpop_up(m)) {                            /* (round B) a held step's velocity */
        snprintf(o, (size_t)cap, "Step %d velocity: %d", held_step(m) + 1, held_vel(m));
        *subj = 0xA7B;
    } else if (t->kpop >= 0 && t->lpop) {               /* (round B) a held step's lock */
        const int p = knob_p(m, t->kpop);
        int lv;
        if (lock_val(m, p, &lv))
            vtext(m, t->cur, p, lv, 0, v, sizeof v);
        else
            snprintf(v, sizeof v, "no hit");
        snprintf(o, (size_t)cap, "Step %d %s: %s", held_step(m) + 1, P_NAME[p], v);
        *subj = (uint16_t)(0xA80 | t->kpop);
    } else if (t->kpop >= 0 && knob_p(m, t->kpop) >= 0) {
        val_text(m, t->cur, knob_p(m, t->kpop), 0, v, sizeof v);
        snprintf(o, (size_t)cap, "%s: %s", P_NAME[knob_p(m, t->kpop)],
                 knob_p(m, t->kpop) == RFM_MOVE_KIT_P_MODE ? MODE_SAY[CPAD(m, t->cur)->v[RFM_MOVE_KIT_P_MODE] % RFM_MOVE_KIT_M_N] : v);
        *subj = (uint16_t)(0xA10 | t->kpop);
    } else if (t->tools) {
        snprintf(o, (size_t)cap, "%s", TOOL_NAME[t->ti % TL_N]);
        *subj = 0xA20, *stamp = 0;
    } else if (t->fact && t->tab == 2) {
        snprintf(o, (size_t)cap, "%s", t->fai ? "All to pads" : "Open");
        *subj = 0xA21, *stamp = 0;
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
        snprintf(o, (size_t)cap, "%s", t->kitrow == KT_LOAD ? "Load" : "Save");
        *subj = (uint16_t)(0xA50 | t->kitrow);
    } else {                                            /* (pages) the page */
        if (t->page == RFM_MOVE_KIT_PG_SAMPLE)          /* (the screen draws no name: said) */
            sound_text(m, t->cur, v, sizeof v), snprintf(o, (size_t)cap, "%s page, %s", PG_NAME[t->page], v);
        else
            snprintf(o, (size_t)cap, "%s page", PG_NAME[t->page % RFM_MOVE_KIT_PG_N]);
        *subj = (uint16_t)(0xA60 | t->page), *stamp = t->turn_ms;
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
    else if (p == RFM_MOVE_KIT_P_ATTACK || p == RFM_MOVE_KIT_P_HOLD || p == RFM_MOVE_KIT_P_RELEASE ||
             p == RFM_MOVE_KIT_P_DECAY)
        v = ENV_MS[env_idx(v)];
    if (v == d->v[p])
        return 0;
    put(m, pad, p, v);
    return 1;
}

/* ================================================================ (round A) the Trim silence === */
int rfm_move_kit_trim(const rfm_move_t *m) { return m->kit.trim != 0; }
void rfm_move_kit_set_trim(rfm_move_t *m, int on)
{
    if (!rfmv_kit_cap(m) || (m->kit.trim != 0) == (on != 0))
        return;
    m->kit.trim = (uint8_t)(on != 0);
    rfmv_seq_dirty(m);
}
/* ---- (round C) the project's own sounds */
int rfm_move_kit_owned(rfm_move_t *m, int pad, const char *from, const char *to)
{
    rfm_move_kit_pad_t *d;
    if (!rfmv_kit_cap(m) || pad < 0 || pad >= RFM_MOVE_KIT_PADS || !from || !to || !to[0] ||
        strlen(to) >= RFM_MOVE_KIT_PATH)
        return 0;
    d = PAD(m, pad);
    if (strcmp(d->path, from) || !strcmp(d->path, to))
        return 0;                                       /* (another sound there now; or the same already) */
    memset(d->path, 0, sizeof d->path);
    snprintf(d->path, sizeof d->path, "%s", to);
    rfmv_seq_dirty(m);
    touched(m);
    return 1;
}
static int refs_add(const char **out, int n, int cap, const char *p)
{
    int i;
    if (!p[0] || n >= cap)
        return n;
    for (i = 0; i < n; i++)
        if (!strcmp(out[i], p))
            return n;
    out[n] = p;
    return n + 1;
}
int rfm_move_kit_refs(const rfm_move_t *m, const char **out, int cap)
{
    const rfm_move_kit_t *t = &m->kit;
    int n = 0, k;
    if (!out || cap <= 0)
        return 0;
    for (k = 0; k < RFM_MOVE_KIT_PADS; k++)
        n = refs_add(out, n, cap, t->pad[k].path);
    if (t->und)
        n = refs_add(out, n, cap, t->und_pad.path);
    for (k = 0; t->undk && k < RFM_MOVE_KIT_PADS; k++)
        n = refs_add(out, n, cap, t->undk_pad[k].path);
    return n;
}
int rfm_move_kit_edit(rfm_move_t *m, int pad, int op)
{
    static const uint8_t TOOL[] = { 0, TL_TRIM, TL_NORM, TL_REV, TL_FREEZE };
    if (!rfmv_kit_cap(m) || pad < 0 || pad >= RFM_MOVE_KIT_PADS || op < RFM_MOVE_KIT_ED_TRIM ||
        op > RFM_MOVE_KIT_ED_FREEZE || !ready(CPAD(m, pad)))
        return 0;
    pad_edit(m, pad, TOOL[op]);
    return 1;
}
