/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (0.13.0, the owner's review of 2026-11-22: "Project should save firmwares") A project holds its firmwares, and the
 * firmwares' states are a layer on the Move. Only for a host with RFM_MOVE_CAP_PROJFW (README "Projects that hold
 * their firmwares", "States"; the mock-ups: remoteFM14Move docs/design/mock_projstates_v1.c).
 *
 *  - The project blob's version 9 part (project.c writes and checks it here): each slot's firmware id, level, layout
 *    settings and sequencer track, the slot played, the drum notes, the velocity. A project load applies them and asks
 *    the host for its firmwares (PROJECT_FW); the loading screen is up until every slot has settled
 *    (rfm_move_proj_fw, or RFM_MOVE_PFW_WAIT_MS with no news); a missing or failed one is said for a second after.
 *  - States as a layer (Track 1..3 held + the top row's pad 7, or the browser's Loop): the 32 states on the pads in the
 *    slot's colour, the clips' gestures (a stored state loads, an empty one saves, Copy / Delete + a state), step 14
 *    New and Undo after it. The layer takes the pads, the steps, the knobs and the jog; Back closes it (its marks
 *    first) and what it was opened over shows again. */
#include <stdio.h>
#include <string.h>

#include "move_int.h"
#include "v2.h"

#define SB RFM_MOVE_PFW_SLOT_BYTES
#define IDM RFM_MOVE_FW_ID_MAX

int rfmv_pfw_cap(const rfm_move_t *m) { return (m->fw.caps & RFM_MOVE_CAP_PROJFW) != 0; }
static int nslots(const rfm_move_t *m) { return rfmv_fw_nslots(m); }
static uint32_t now_ms(const rfm_move_t *m) { return rfmv_now_ms(m); }
static int settled(int st)
{
    return st == RFM_MOVE_PFW_READY || st == RFM_MOVE_PFW_MISSING || st == RFM_MOVE_PFW_FAILED || st == PFW_EMPTY;
}
/* s cut to fit w pixels at k x */
static void cut(const char *s, char *o, int cap, int w, int k)
{
    int n;
    snprintf(o, (size_t)cap, "%s", s);
    for (n = (int)strlen(o); n > 0 && v2_tw(o, k) > w; n--)
        o[n - 1] = 0;
}

/* ================================================================ the blob's version 9 part === */
int rfmv_pfw_check(const uint8_t *p)
{
    int s, k;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++, p += SB) {
        const uint8_t *q = p + IDM;
        int n;
        for (n = 0; n < IDM && p[n]; n++)
            if (p[n] < 32 || p[n] > 126)
                return -1;
        if (n == IDM)
            return -1;                                  /* (no NUL) */
        if (q[0] > 127 || q[1] >= RFM_MOVE_LAY_N || q[2] >= RFM_MOVE_NSCALES || q[3] > 11 || q[4] > 15 ||
            (int8_t)q[5] < RFM_MOVE_OCT_MIN || (int8_t)q[5] > RFM_MOVE_OCT_MAX || q[6 + RFM_MOVE_NKNOBS] >= RFM_MOVE_SEQ_TRACKS)
            return -1;
        for (k = 0; k < RFM_MOVE_NKNOBS; k++)
            if (q[6 + k] > 127)
                return -1;
    }
    if (p[0] >= RFM_MOVE_FW_SLOTS)
        return -1;
    for (k = 0; k < RFM_MOVE_NPADS; k++)
        if (p[1 + k] > 127)
            return -1;
    if (p[1 + RFM_MOVE_NPADS] >= RFM_MOVE_VEL_N || p[2 + RFM_MOVE_NPADS] < 1 || p[2 + RFM_MOVE_NPADS] > 127)
        return -1;
    return 0;
}
void rfmv_pfw_ids(const uint8_t *p, char ids[RFM_MOVE_FW_SLOTS][IDM])
{
    int s;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++, p += SB) {
        memcpy(ids[s], p, IDM);
        ids[s][IDM - 1] = 0;
    }
}
/* what a save keeps for each slot: the firmware loaded there; one still loading: the project's; an empty slot the
 * user has not touched since the project loaded: the firmware the project wanted (a missing one is kept) */
void rfmv_pfw_saved_ids(const rfm_move_t *m, char ids[RFM_MOVE_FW_SLOTS][IDM])
{
    int s;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++) {
        const char *id = m->fw.slot_id[s];
        if (m->pfw_on && m->seq.proj_pending < 0 && !settled(m->pfw_st[s]))
            id = m->pfw_want[s];
        else if (!id[0] && !(m->pfw_touched >> s & 1))
            id = m->pfw_want[s];
        snprintf(ids[s], IDM, "%s", id);
    }
}
void rfmv_pfw_write(rfm_move_t *m, uint8_t *p)
{
    char ids[RFM_MOVE_FW_SLOTS][IDM];
    int s;
    if (!rfmv_pfw_cap(m) && m->pfw_raw_ok) {           /* (an older host: the project's part as it came) */
        memcpy(p, m->pfw_raw, RFM_MOVE_PFW_BYTES);
        return;
    }
    rfmv_slot_push(m);                                  /* (the active slot's record current) */
    rfmv_pfw_saved_ids(m, ids);
    memset(p, 0, RFM_MOVE_PFW_BYTES);
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++, p += SB) {
        const rfm_move_slotset_t *ss = &m->s.slot[s];
        uint8_t *q = p + IDM;
        int k;
        memcpy(p, ids[s], strlen(ids[s]));
        q[0] = (uint8_t)rfmv_clampi(m->fw.level[s], 0, 127);
        q[1] = (uint8_t)(ss->lay < RFM_MOVE_LAY_N ? ss->lay : RFM_MOVE_LAY_FM1);
        q[2] = (uint8_t)(ss->scale < RFM_MOVE_NSCALES ? ss->scale : 0);
        q[3] = (uint8_t)(ss->root % 12), q[4] = (uint8_t)(ss->channel & 15);
        q[5] = (uint8_t)(int8_t)rfmv_clampi(ss->octave, RFM_MOVE_OCT_MIN, RFM_MOVE_OCT_MAX);
        for (k = 0; k < RFM_MOVE_NKNOBS; k++)
            q[6 + k] = (uint8_t)(ss->cc[k] & 127);
        q[6 + RFM_MOVE_NKNOBS] = (uint8_t)(m->s.lock[s] % RFM_MOVE_SEQ_TRACKS);
    }
    p[0] = (uint8_t)(m->fw.active < RFM_MOVE_FW_SLOTS ? m->fw.active : 0);
    for (s = 0; s < RFM_MOVE_NPADS; s++)
        p[1 + s] = (uint8_t)(m->s.drum[s] & 127);
    p[1 + RFM_MOVE_NPADS] = (uint8_t)(m->s.vel_mode < RFM_MOVE_VEL_N ? m->s.vel_mode : 0);
    p[2 + RFM_MOVE_NPADS] = (uint8_t)rfmv_clampi(m->s.vel_level, 1, 127);
}

/* ================================================================ the project's firmware settings, watched === */
static uint32_t fnv_add(uint32_t h, const void *v, size_t n)
{
    const uint8_t *p = (const uint8_t *)v;
    size_t i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static uint32_t settings_hash(const rfm_move_t *m)
{
    uint32_t h = 2166136261u;
    h = fnv_add(h, m->fw.slot_id, sizeof m->fw.slot_id);
    h = fnv_add(h, m->fw.level, sizeof m->fw.level);
    h = fnv_add(h, m->s.slot, sizeof m->s.slot);
    h = fnv_add(h, m->s.lock, sizeof m->s.lock);
    h = fnv_add(h, &m->fw.active, 1);
    h = fnv_add(h, m->s.drum, sizeof m->s.drum);
    h = fnv_add(h, &m->s.vel_mode, 1);
    h = fnv_add(h, &m->s.vel_level, 1);
    return h | 1;                                       /* (0: not seen yet) */
}
static void snapshot(rfm_move_t *m)
{
    m->pfw_hash = settings_hash(m);
    memcpy(m->pfw_seen, m->fw.slot_id, sizeof m->pfw_seen);
}
/* a firmware, a level, a slot's layout settings or track, the slot played, the drum notes or the velocity changed by
 * the user: the project is edited (its autosave); a slot's firmware changed: the project no longer wants a missing
 * one there */
void rfmv_pfw_watch(rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    uint32_t h;
    int s;
    if (!rfmv_pfw_cap(m))
        return;
    h = settings_hash(m);
    if (h == m->pfw_hash)
        return;
    if (!m->pfw_on && m->pfw_hash) {
        for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
            if (strcmp(m->pfw_seen[s], m->fw.slot_id[s]))
                m->pfw_touched = (uint8_t)(m->pfw_touched | 1u << s);
        if (m->pfw_live && q->proj_pending < 0 && q->proj_cur >= 0 && q->proj_cur < RFM_MOVE_PROJECTS &&
            (q->proj_used >> q->proj_cur & 1))
            rfmv_seq_dirty(m);                          /* (a project that is stored: never one made up at start) */
    }
    snapshot(m);
}

/* ================================================================ loading a project's firmwares === */
static void act_pfw(rfm_move_t *m, int slot, int proj, const char *id)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_PROJECT_FW, a.slot = (uint8_t)slot, a.index = (uint8_t)proj;
    a.data = (const uint8_t *)id, a.len = (uint32_t)strlen(id);
    a.frame = m->seq.frame;
    if (m->fn)
        m->fn(m->ctx, &a);
}
static void pfw_stop(rfm_move_t *m)                     /* the loading screen goes (Play waiting: now) */
{
    m->pfw_on = 0;
    if (m->pfw_play) {
        m->pfw_play = 0;
        if (!m->seq.playing && !m->seq.start_pending)
            rfmv_seq_start(m);
    }
}
static void settle(rfm_move_t *m)                       /* every slot settled: the screen goes, the messages after */
{
    int s;
    if (!m->pfw_on)
        return;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
        if (!settled(m->pfw_st[s]))
            return;
    m->pfw_msg = m->pfw_fail = 0, m->pfw_msg_k = -1;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++) {
        if (m->pfw_st[s] == RFM_MOVE_PFW_MISSING || m->pfw_st[s] == RFM_MOVE_PFW_FAILED)
            m->pfw_msg = (uint8_t)(m->pfw_msg | 1u << s);
        if (m->pfw_st[s] == RFM_MOVE_PFW_FAILED)
            m->pfw_fail = (uint8_t)(m->pfw_fail | 1u << s);
    }
    m->pfw_msg_ms = now_ms(m);
    pfw_stop(m);
    snapshot(m);
}
/* the project's settings, as the user's own edits make them: the levels (FW_LEVEL), each slot's layout settings and
 * track, the slot played (FW_ACTIVE, its layout played), the drum notes, the velocity (SETTINGS) */
static void apply(rfm_move_t *m, const uint8_t *part)
{
    rfm_move_fw_state_t *f = &m->fw;
    const int ns = nslots(m), act = part[RFM_MOVE_FW_SLOTS * SB];
    int s, k, lay_act;
    rfmv_slot_push(m);
    lay_act = m->s.slot[f->active % RFM_MOVE_FW_SLOTS].lay;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++) {
        const uint8_t *q = part + s * SB + IDM;
        rfm_move_slotset_t *ss = &m->s.slot[s];
        if (s < ns)
            rfm_move_fw_set_level(m, s, q[0]);
        ss->lay = q[1], ss->scale = q[2], ss->root = q[3], ss->channel = q[4], ss->octave = (int8_t)q[5];
        for (k = 0; k < RFM_MOVE_NKNOBS; k++)
            ss->cc[k] = q[6 + k];
        m->s.lock[s] = q[6 + RFM_MOVE_NKNOBS];
    }
    memcpy(m->s.drum, part + RFM_MOVE_FW_SLOTS * SB + 1, RFM_MOVE_NPADS);
    m->s.vel_mode = part[RFM_MOVE_FW_SLOTS * SB + 1 + RFM_MOVE_NPADS];
    m->s.vel_level = part[RFM_MOVE_FW_SLOTS * SB + 2 + RFM_MOVE_NPADS];
    if (act < ns && act != f->active)
        rfmv_fw_activate(m, act);                       /* (the slot played: its layout) */
    else {
        const int t = f->active % RFM_MOVE_FW_SLOTS, nl = m->s.slot[t].lay;
        if (nl != lay_act) {
            m->s.slot[t].lay = (uint8_t)lay_act;
            if (rfmv_set_slot_lay(m, t, nl) < 0)
                m->s.slot[t].lay = (uint8_t)nl;         /* (a takeover not claimed yet: kept for when it is) */
        } else
            rfmv_slot_pull(m);
    }
    rfmv_slot_push(m);
    rfmv_changed(m);
}
void rfmv_pfw_loaded(rfm_move_t *m, int slot, const uint8_t *part)
{
    const int ns = nslots(m);
    int s;
    m->pfw_live = 1;
    if (!rfmv_pfw_cap(m)) {                             /* (an older host: the firmwares never touched; the part kept
                                                         * to be saved back as it came) */
        m->pfw_raw_ok = part != NULL;
        if (part)
            memcpy(m->pfw_raw, part, RFM_MOVE_PFW_BYTES);
        return;
    }
    m->pfw_raw_ok = 0, m->pfw_touched = 0;
    if (!part) {                                        /* (versions 1..8, an empty project: no firmwares; the slots
                                                         * and the settings as they are) */
        memset(m->pfw_want, 0, sizeof m->pfw_want);
        if (m->pfw_on)
            pfw_stop(m);
        snapshot(m);
        return;
    }
    rfmv_pfw_ids(part, m->pfw_want);
    memcpy(m->pfw_id, m->pfw_want, sizeof m->pfw_id);
    m->pfw_on = 1, m->pfw_proj = (int8_t)slot, m->pfw_msg = 0;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
        m->pfw_st[s] = (int16_t)(s < ns && m->pfw_want[s][0] ? PFW_WAIT : PFW_EMPTY), m->pfw_ms[s] = now_ms(m);
    apply(m, part);
    for (s = 0; s < ns; s++)
        if (!m->pfw_want[s][0])
            rfmv_fw_slot_set(m, s, "");                 /* (the project's empty slot: emptied) */
    snapshot(m);
    for (s = 0; s < ns; s++)                            /* (the host may answer at once: everything set first) */
        act_pfw(m, s, slot, m->pfw_want[s]);
    settle(m);
    snapshot(m);
}
void rfmv_pfw_load_failed(rfm_move_t *m, int slot)
{
    if (m->pfw_on && m->pfw_proj == slot)
        m->pfw_on = 0, m->pfw_play = 0;
}
/* a stored project's pad: its firmwares (the grid knows them) on the loading screen at the press, before the host
 * has answered (R1) */
void rfmv_pfw_press(rfm_move_t *m, int slot)
{
    const rfm_move_proj_info_t *in;
    int s, any = 0;
    if (!rfmv_pfw_cap(m) || slot < 0 || slot >= RFM_MOVE_PROJECTS)
        return;
    in = &m->seq.info[slot];
    if (!in->ok || !in->has_fw)
        return;
    memcpy(m->pfw_id, in->fw, sizeof m->pfw_id);
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++) {
        const int wait = s < nslots(m) && in->fw[s][0];
        m->pfw_st[s] = (int16_t)(wait ? PFW_WAIT : PFW_EMPTY), m->pfw_ms[s] = now_ms(m);
        any |= wait;
    }
    m->pfw_proj = (int8_t)slot, m->pfw_msg = 0;
    m->pfw_on = (uint8_t)any;
}
/* (0.13.0, the owner's addendum) a new project's routing and channels: tracks 1-4 to firmware 1, 5-8 to 2, 9-12 to 3
 * (13-16 to none); in each group of four the channels 1..4; (the orchestrator's) each Track's sequencer track the
 * first of its group (1, 5, 9). The MIDI FX filters stay "all". As the user's edits make them: ROUTE (the host's
 * rfm_move_route_set path), SETTINGS, the project marked edited. Three slots only (one slot: as before) */
void rfmv_pfw_defaults(rfm_move_t *m)
{
    static const int FILT[RFM_MOVE_FW_SLOTS] = { 0x000F, 0x00F0, 0x0F00 };
    int t;
    if (!rfmv_pfw_cap(m) || nslots(m) < RFM_MOVE_FW_SLOTS)
        return;
    for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
        m->seq.tr[t].ch = (uint8_t)(t % 4);
    for (t = 0; t < RFM_MOVE_FW_SLOTS; t++)
        m->fsolo[t] = 0;
    if (rfm_move_route_set(m, FILT, NULL) < 0)          /* (no FX setup: the filters still, no ROUTE) */
        for (t = 0; t < RFM_MOVE_FW_SLOTS; t++)
            m->seq.fx.ffilt[t] = (uint16_t)FILT[t];
    for (t = 0; t < RFM_MOVE_FW_SLOTS; t++)
        m->s.lock[t] = (uint8_t)(t * 4);
    if (m->s.mode == RFM_MOVE_MODE_SEQ && !m->seq.view && m->s.slot[m->fw.active % RFM_MOVE_FW_SLOTS].lay >= RFM_MOVE_LAY_SCALE)
        rfmv_seq_select(m, m->s.lock[m->fw.active % RFM_MOVE_FW_SLOTS]);   /* (a note layout: its slot's track) */
    rfmv_changed(m);
    rfmv_seq_dirty(m);
    snapshot(m);
}
/* the host's rfm_move_project_dir with no project stored (a fresh install): the open empty project is a new one */
void rfmv_pfw_first(rfm_move_t *m, uint32_t used)
{
    if (!used && !m->pfw_live && rfmv_pfw_cap(m) && !m->seq.nnotes && !m->seq.nlocks)
        rfmv_pfw_defaults(m), m->pfw_live = 1;
}
/* a new project: its three slots empty (the host unloads them); everything else as it is */
void rfmv_pfw_new(rfm_move_t *m, int slot)
{
    int s;
    if (!rfmv_pfw_cap(m))
        return;
    rfmv_pfw_defaults(m);                               /* (the owner's: the new project's routing and channels) */
    memset(m->pfw_want, 0, sizeof m->pfw_want);
    m->pfw_touched = 0, m->pfw_on = 0, m->pfw_msg = 0;
    for (s = 0; s < nslots(m); s++) {
        rfmv_fw_slot_set(m, s, "");
        act_pfw(m, s, slot, "");
    }
    snapshot(m);
}
int rfmv_pfw_play(rfm_move_t *m)
{
    if (!m->pfw_on || m->seq.playing || m->seq.start_pending)
        return 0;
    m->pfw_play = (uint8_t)!m->pfw_play;                /* (pressed again: not after all) */
    return 1;
}
void rfm_move_proj_fw(rfm_move_t *m, int slot, int status)
{
    if (m->pfw_on && slot >= 0 && slot < RFM_MOVE_FW_SLOTS && !settled(m->pfw_st[slot]) && m->seq.proj_pending < 0) {
        if (status >= 0 && status < RFM_MOVE_PFW_READY)
            m->pfw_st[slot] = (int16_t)status, m->pfw_ms[slot] = now_ms(m);
        else if (status == RFM_MOVE_PFW_READY) {
            m->pfw_st[slot] = RFM_MOVE_PFW_READY;
            rfmv_fw_slot_set(m, slot, m->pfw_want[slot]);
        } else if (status == RFM_MOVE_PFW_MISSING || status == RFM_MOVE_PFW_FAILED) {
            m->pfw_st[slot] = (int16_t)status;
            rfmv_fw_slot_set(m, slot, "");              /* (left empty; the project keeps wanting it) */
        }
        snapshot(m);
        settle(m);
    }
    rfmv_say_check(m);
}
int rfm_move_proj_fw_loading(const rfm_move_t *m) { return m->pfw_on != 0; }
void rfm_move_fw_state_cur(rfm_move_t *m, int slot, int k)
{
    if (slot >= 0 && slot < RFM_MOVE_FW_SLOTS && k >= -1 && k < RFM_MOVE_FW_STATES)
        m->fw.st_cur[slot] = (int8_t)k;
    rfmv_say_check(m);
}
static int msg_slot(const rfm_move_t *m)                /* the missing / failed slot said now, -1 none */
{
    int k, s;
    if (!m->pfw_msg || m->pfw_on)
        return -1;
    k = (int)((now_ms(m) - m->pfw_msg_ms) / RFM_MOVE_POPUP_MS);
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
        if ((m->pfw_msg >> s & 1) && k-- == 0)
            return s;
    return -1;
}
void rfmv_pfw_poll(rfm_move_t *m)
{
    int s, chg = 0;
    if (m->pfw_on) {                                    /* (no news for RFM_MOVE_PFW_WAIT_MS: failed) */
        for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
            if (!settled(m->pfw_st[s]) && now_ms(m) - m->pfw_ms[s] >= RFM_MOVE_PFW_WAIT_MS) {
                m->pfw_st[s] = RFM_MOVE_PFW_FAILED, chg = 1;
                if (m->seq.proj_pending < 0)
                    rfmv_fw_slot_set(m, s, "");
            }
        if (chg) {
            if (m->seq.proj_pending >= 0)
                m->seq.proj_pending = -1;               /* (the host never answered the project either) */
            settle(m);
        }
    }
    if (m->pfw_msg && !m->pfw_on) {
        const int k = msg_slot(m);
        if (k < 0)
            m->pfw_msg = 0, chg = 1;
        else if (k != m->pfw_msg_k)
            m->pfw_msg_k = (int8_t)k, chg = 1;
    }
    if (m->fw.sl && !rfmv_fw_slot_loaded(m, m->fw.sl - 1))
        rfmv_st_close(m), chg = 1;                      /* (its firmware gone: the layer with it) */
    rfmv_pfw_watch(m);
    if (chg)
        rfmv_say_check(m);
}
int rfmv_pfw_timed(const rfm_move_t *m) { return m->pfw_on || m->pfw_msg; }
void rfmv_pfw_track_leds(const rfm_move_t *m, rfmv_rgb_t c[L_WHITE0])
{
    int t;
    if (!m->pfw_on)
        return;
    for (t = 0; t < RFM_MOVE_FW_SLOTS; t++)             /* (a Track lights once its firmware is ready) */
        c[L_TRACK0 + t] = m->pfw_st[t] == RFM_MOVE_PFW_READY ? RFMV_SLOT_RGB[t] : RFMV_OFF;
}

/* ---- the screens (the mock-ups) */
void rfmv_pfw_rows(const rfm_move_t *m, mr_canvas_t *cv, const char ids[RFM_MOVE_FW_SLOTS][IDM], const int16_t *st,
                   int y0)
{
    int s;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++) {
        const int y = y0 + s * 12;
        char n[2] = { (char)('1' + s), 0 }, nm[40], c[40];
        v2_T(cv, 0, y, n, 1, 1);
        if (!ids[s][0]) {
            v2_dotH(cv, 12, y + 3, 40);                 /* (an empty slot: a dotted line) */
            continue;
        }
        rfmv_fw_id_name(m, ids[s], nm, sizeof nm);
        cut(nm, c, sizeof c, st ? 70 : 116, 1);
        v2_T(cv, 12, y, c, 1, 1);
        if (st) {                                       /* (a bar: waiting dotted, then it fills; full when ready) */
            const int x = 84, w = 43;
            if (st[s] == PFW_WAIT)
                v2_dotH(cv, x, y + 3, w);
            else if (st[s] >= 0 && st[s] <= RFM_MOVE_PFW_READY) {
                v2_O(cv, x, y + 1, w, 5, 1);
                v2_R(cv, x, y + 1, w * st[s] / RFM_MOVE_PFW_READY, 5, 1);
            }
        }
    }
}
int rfmv_pfw_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    char b[48], c[48];
    int s;
    if (m->pfw_on) {                                    /* the project's number at 3x, "Loading", a row a slot */
        mr_gfx_clear(cv, 0);
        snprintf(b, sizeof b, "%02d", m->pfw_proj + 1);
        v2_T(cv, 0, 0, b, 3, 1);
        v2_T(cv, 41, 4, "Loading", 2, 1);
        rfmv_pfw_rows(m, cv, m->pfw_id, m->pfw_st, 28);
        return 1;
    }
    if ((s = msg_slot(m)) < 0)
        return 0;
    mr_gfx_clear(cv, 0);                                /* a missing one: its name, "not installed", the slot empty */
    rfmv_fw_id_name(m, m->pfw_id[s], b, sizeof b);
    cut(b, c, sizeof c, 128, 2);
    v2_TC(cv, 64, 8, c, 2, 1);
    v2_TC(cv, 64, 32, m->pfw_fail >> s & 1 ? "failed" : "not installed", 1, 1);
    snprintf(b, sizeof b, "Track %d empty", s + 1);
    v2_TC(cv, 64, 46, b, 1, 1);
    return 1;
}
int rfmv_pfw_words(const rfm_move_t *m, char *o, int cap, uint16_t *subj, uint32_t *stamp)
{
    char nm[48];
    int s;
    if (m->pfw_on) {
        snprintf(o, (size_t)cap, "Loading project %d", m->pfw_proj + 1);
        *subj = (uint16_t)(0x500 | m->pfw_proj), *stamp = 0;
        return 1;
    }
    if ((s = msg_slot(m)) < 0)
        return 0;
    rfmv_fw_id_name(m, m->pfw_id[s], nm, sizeof nm);
    snprintf(o, (size_t)cap, "%s %s, Track %d empty", nm, m->pfw_fail >> s & 1 ? "failed" : "not installed", s + 1);
    *subj = (uint16_t)(0x540 | s), *stamp = m->pfw_msg_ms;
    return 1;
}
void rfmv_pfw_proj_words(const rfm_move_t *m, const rfm_move_proj_info_t *in, char *o, int cap)
{
    int s, n = 0;
    if (cap <= 0)
        return;
    o[0] = 0;
    if (!rfmv_pfw_cap(m) || !in->has_fw)
        return;
    for (s = 0; s < RFM_MOVE_FW_SLOTS && n < cap - 1; s++)
        if (in->fw[s][0]) {
            char nm[48];
            rfmv_fw_id_name(m, in->fw[s], nm, sizeof nm);
            n += snprintf(o + n, (size_t)(cap - n), ", %s", nm);
        }
}

/* ================================================================ States as a layer === */
static const char *const WORDS[5] = { "saved", "loaded", "copied", "pasted", "deleted" };
enum { SP_NONE, SP_NAME, SP_STATE, SP_NEW, SP_UNDO, SP_EMPTY };
enum { SW_SAVED, SW_LOADED, SW_COPIED, SW_PASTED, SW_DELETED };

int rfmv_st_on(const rfm_move_t *m) { return m->fw.sl && rfmv_fw_slot_loaded(m, m->fw.sl - 1) ? m->fw.sl : 0; }
int rfm_move_states_open(const rfm_move_t *m) { return rfmv_st_on(m) - 1; }
int rfmv_st_pad7(const rfm_move_t *m, int t)
{
    return rfmv_pfw_cap(m) && m->fw.host && t >= 0 && t < nslots(m) && rfmv_fw_slot_loaded(m, t);
}
static void st_pop(rfm_move_t *m, int kind, int k, int w, int slot)
{
    rfm_move_fw_state_t *f = &m->fw;
    f->sl_pop = (uint8_t)kind, f->sl_pk = (int8_t)k, f->sl_pw = (int8_t)w, f->sl_pslot = (int8_t)slot;
    f->sl_pop_ms = now_ms(m);
}
static int pop_up(const rfm_move_t *m) { return m->fw.sl_pop && now_ms(m) - m->fw.sl_pop_ms < RFM_MOVE_POPUP_MS; }
void rfmv_st_open(rfm_move_t *m, int t, int from)
{
    rfm_move_fw_state_t *f = &m->fw;
    if (!rfmv_st_pad7(m, t))
        return;
    if (f->undo && f->undo != t + 1)
        f->undo = 0;                                    /* (New's undo is the slot's own) */
    f->sl = (uint8_t)(t + 1), f->sl_from = (uint8_t)from, f->sl_ms = now_ms(m);
    st_pop(m, SP_NAME, -1, 0, t);                       /* (the owner's: "clearly communicate that it's changing
                                                         * states": its name a second, the pads swept) */
}
void rfmv_st_close(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = &m->fw;
    f->sl = 0, f->sl_mods = 0, f->sb_armed = 0, f->undo = 0, f->sl_pop = 0;
}
static int buf_ok(const rfm_move_t *m, int t)            /* the buffer is this firmware's */
{
    return m->fw.sb_kind && !strcmp(m->fw.sb_id, m->fw.slot_id[t]);
}
static void st_act(rfm_move_t *m, int kind, int t, int k, int value)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = (uint8_t)kind, a.slot = (uint8_t)t, a.index = (uint8_t)(k < 0 ? 255 : k), a.value = (int16_t)value;
    a.data = (const uint8_t *)m->fw.slot_id[t], a.len = (uint32_t)strlen(m->fw.slot_id[t]);
    a.frame = m->seq.frame;
    if (m->fn)
        m->fn(m->ctx, &a);
}
static void st_say(rfm_move_t *m, int k, int w)
{
    char b[32];
    snprintf(b, sizeof b, "State %d %s", k + 1, WORDS[w]);
    rfmv_say_text(m, b);
}
/* a state's pad, as a clip's in session view (the owner's choice): a stored one loads, an empty one saves (the active
 * one is autosaved by the host); Copy + a state copies it, or pastes when armed or empty; Delete + a state deletes it
 * into the buffer. Nothing loads while Copy or Delete is held */
static void st_pad(rfm_move_t *m, int k)
{
    rfm_move_fw_state_t *f = &m->fw;
    const int t = f->sl - 1, used = f->st_used[t] >> k & 1;
    if (f->sl_mods & 2) {                               /* Delete + a stored state: gone, into the buffer (armed) */
        if (!used)
            return;
        st_act(m, RFM_MOVE_ACT_STATE_DELETE, t, k, 0);
        f->st_used[t] &= ~(1u << k);
        if (f->st_cur[t] == k)
            f->st_cur[t] = -1;                          /* (the firmware keeps what it holds; none active) */
        f->sb_kind = 2, f->sb_src = (int8_t)k, f->sb_armed = 1;
        snprintf(f->sb_id, sizeof f->sb_id, "%s", f->slot_id[t]);
        f->undo = 0;
        st_say(m, k, SW_DELETED);
        return;
    }
    if (f->sl_mods & 1) {                               /* Copy + a state */
        const int ok = buf_ok(m, t);
        if (used && !(ok && f->sb_armed && !(k == f->sb_src && f->sb_kind == 1))) {
            st_act(m, RFM_MOVE_ACT_STATE_COPY, t, k, 0);   /* (copied: dashed, armed) */
            f->sb_kind = 1, f->sb_src = (int8_t)k, f->sb_armed = 1;
            snprintf(f->sb_id, sizeof f->sb_id, "%s", f->slot_id[t]);
            st_say(m, k, SW_COPIED);
            return;
        }
        if (!ok)
            return;                                     /* (an empty pad, nothing of this firmware's to paste) */
        st_act(m, RFM_MOVE_ACT_STATE_PASTE, t, k, f->st_cur[t] == k);   /* (onto the active one: loaded too) */
        f->st_used[t] |= 1u << k;
        f->sb_armed = 0, f->undo = 0;                   /* (a paste disarms) */
        st_pop(m, SP_STATE, k, SW_PASTED, t);
        return;
    }
    f->undo = 0;
    if (used) {
        st_act(m, RFM_MOVE_ACT_STATE_LOAD, t, k, 0);
        f->st_cur[t] = (int8_t)k;
        st_pop(m, SP_STATE, k, SW_LOADED, t);
    } else {
        st_act(m, RFM_MOVE_ACT_STATE_SAVE, t, k, 0);
        f->st_used[t] |= 1u << k, f->st_cur[t] = (int8_t)k;
        st_pop(m, SP_STATE, k, SW_SAVED, t);
    }
}
static void st_new(rfm_move_t *m)                       /* step 14: the firmware as first installed; Undo after it */
{
    rfm_move_fw_state_t *f = &m->fw;
    const int t = f->sl - 1;
    st_act(m, RFM_MOVE_ACT_STATE_NEW, t, -1, 0);
    f->undo = (uint8_t)(t + 1), f->undo_cur = f->st_cur[t];
    f->st_cur[t] = -1;
    st_pop(m, SP_NEW, -1, 0, t);
}
static void st_undo(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = &m->fw;
    const int t = f->sl - 1;
    if (f->undo != f->sl)
        return;                                         /* (no New to undo: dead) */
    st_act(m, RFM_MOVE_ACT_STATE_UNDO, t, f->undo_cur, f->undo_cur);
    f->st_cur[t] = f->undo_cur, f->undo = 0;
    st_pop(m, SP_UNDO, -1, 0, t);
}
static int bt_get(const rfm_move_fw_state_t *f, int cc) { return f->sl_bt[(cc & 127) >> 3] >> (cc & 7) & 1; }
static void bt_set(rfm_move_fw_state_t *f, int cc, int on)
{
    const uint8_t b = (uint8_t)(1u << (cc & 7));
    f->sl_bt[(cc & 127) >> 3] = (uint8_t)(on ? f->sl_bt[(cc & 127) >> 3] | b : f->sl_bt[(cc & 127) >> 3] & ~b);
}
int rfmv_st_input(rfm_move_t *m, const mr_event_t *ev)
{
    rfm_move_fw_state_t *f = &m->fw;
    const int open = rfmv_st_on(m) != 0, i = ev->index;
    switch (ev->type) {
    case MR_EV_PAD:
        if (i < 0 || i >= RFM_MOVE_NPADS)
            return 0;
        if (!ev->value) {                               /* (a release: the layer's when it took the press) */
            if (!(f->sl_pads >> i & 1))
                return 0;
            f->sl_pads &= ~(1u << i);
            return 1;
        }
        if (!open)
            return 0;
        if (!(f->sl_pads >> i & 1)) {
            f->sl_pads |= 1u << i;
            st_pad(m, (3 - i / 8) * 8 + i % 8);         /* (the top-left pad: state 1) */
        }
        return 1;
    case MR_EV_PAD_PRESSURE: return i >= 0 && i < RFM_MOVE_NPADS && (open || (f->sl_pads >> i & 1));
    case MR_EV_STEP:
        if (i < 0 || i >= RFM_MOVE_NSTEPS)
            return 0;
        if (!ev->value) {
            if (!(f->sl_steps >> i & 1))
                return 0;
            f->sl_steps = (uint16_t)(f->sl_steps & ~(1u << i));
            return 1;
        }
        if (!open)
            return 0;
        if (!(f->sl_steps >> i & 1)) {
            f->sl_steps = (uint16_t)(f->sl_steps | 1u << i);
            if (i == 13)
                st_new(m);                              /* (the other steps: dead) */
        }
        return 1;
    case MR_EV_KNOB: return open && i >= 0 && i < RFM_MOVE_NKNOBS;   /* (the volume knob: as everywhere) */
    case MR_EV_TOUCH: return open && ev->value && i != 8;   /* (releases pass: nothing stays touched) */
    case MR_EV_JOG:
    case MR_EV_JOG_CLICK: return open;
    case MR_EV_BUTTON: {
        const int cc = i & 127;
        if (!ev->value) {
            if (!bt_get(f, cc))
                return 0;
            bt_set(f, cc, 0);
            if (cc == MR_CC_COPY)
                f->sl_mods &= (uint8_t)~1u;
            else if (cc == MR_CC_DELETE)
                f->sl_mods &= (uint8_t)~2u;
            return 1;
        }
        if (!open || cc == MR_CC_PLAY || cc == MR_CC_BACK || (cc >= MR_CC_TRACK4 && cc <= MR_CC_TRACK1))
            return 0;
        if (cc == MR_CC_MENU) {                         /* (Menu: the layer closes, Menu does its job there) */
            rfmv_st_close(m);
            return 0;
        }
        bt_set(f, cc, 1);
        if (cc == MR_CC_COPY)
            f->sl_mods |= 1;
        else if (cc == MR_CC_DELETE)
            f->sl_mods |= 2;
        else if (cc == MR_CC_UNDO)
            st_undo(m);
        else if (cc == MR_CC_LOOP && f->sl_from == 1)
            rfmv_st_close(m);                           /* (opened by the browser's Loop: Loop goes back to it) */
        return 1;                                       /* (the rest: dead) */
    }
    default: return 0;
    }
}
int rfmv_st_back(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = &m->fw;
    if (!rfmv_st_on(m))
        return 0;
    if (f->sb_armed && f->sb_kind)
        f->sb_armed = 0;                                /* (the marks first) */
    else
        rfmv_st_close(m);
    return 1;
}
void rfmv_st_track(rfm_move_t *m, int t)
{
    rfm_move_fw_state_t *f = &m->fw;
    if (t < 0 || t >= nslots(m))
        return;
    if (!rfmv_fw_slot_loaded(m, t))
        st_pop(m, SP_EMPTY, -1, 0, t);                  /* (an empty slot: said; States stay where they were) */
    else if (t + 1 == f->sl)
        st_pop(m, SP_NAME, -1, 0, t);
    else
        rfmv_st_open(m, t, f->sl_from);
}

/* the lights: the states in the slot's colour (the active one full, stored ones dim, empty dark, a pressed one white),
 * swept in column by column on opening (unswept pads keep what they showed: 8 LED writes a frame at most); step 14
 * New; the buttons the layer uses lit, the rest dark */
void rfmv_st_leds(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3])
{
    const rfm_move_fw_state_t *f = &m->fw;
    const int t = rfmv_st_on(m) - 1;
    rfmv_rgb_t col, v;
    uint32_t sw;
    int p;
    if (t < 0)
        return;
    col = RFMV_SLOT_RGB[t];
    sw = (now_ms(m) - f->sl_ms) / RFMV_ST_SWEEP_MS;
    for (p = 0; p < RFM_MOVE_NPADS; p++) {
        const int k = (3 - p / 8) * 8 + p % 8, c = p % 8;
        if (f->sl_pads >> p & 1)
            v = RFMV_WHITE;
        else if (sw < 8 && (uint32_t)c > sw) {          /* (not swept yet: as it was) */
            if (!m->sent_ok[L_PAD0 + p])
                memset(w[L_PAD0 + p], 0, 3);
            else
                memcpy(w[L_PAD0 + p], m->sent[L_PAD0 + p], 3);
            continue;
        } else if (sw < 8 && (uint32_t)c == sw)
            v = col;
        else
            v = f->st_cur[t] == k ? col : f->st_used[t] >> k & 1 ? rfmv_sc(col, 108) : RFMV_OFF;
        w[L_PAD0 + p][0] = v.r, w[L_PAD0 + p][1] = v.g, w[L_PAD0 + p][2] = v.b;
    }
    for (p = 0; p < RFM_MOVE_NSTEPS; p++) {
        v = p != 13 ? RFMV_OFF : f->sl_steps >> 13 & 1 ? RFMV_WHITE : rfmv_sc(RFMV_WHITE, 64);
        w[L_STEP0 + p][0] = v.r, w[L_STEP0 + p][1] = v.g, w[L_STEP0 + p][2] = v.b;
        w[L_ICON0 + p][0] = (uint8_t)(p == 13 ? 127 : 0), w[L_ICON0 + p][1] = w[L_ICON0 + p][2] = 0;   /* (R6: New) */
    }
    for (p = 0; p < RFM_MOVE_NKNOBS; p++)
        memset(w[L_RING0 + p], 0, 3);
    for (p = 0; p < RFM_MOVE_FW_SLOTS; p++) {           /* the Tracks: the slot shown full, the others as loaded */
        v = p >= nslots(m) ? RFMV_OFF : p == t ? col : rfmv_fw_slot_loaded(m, p) ? rfmv_sc(RFMV_SLOT_RGB[p], 64)
          : rfmv_sc(RFMV_WHITE, 26);
        w[L_TRACK0 + p][0] = v.r, w[L_TRACK0 + p][1] = v.g, w[L_TRACK0 + p][2] = v.b;
    }
    memset(w[L_REC], 0, 3), memset(w[L_SAMPLE], 0, 3);
    for (p = 0; p < 13; p++)
        w[L_WHITE0 + p][0] = 0;
    w[L_WHITE0 + W_BACK][0] = (uint8_t)(m->back_down ? 127 : 80);
    w[L_WHITE0 + W_MENU][0] = 40;
    w[L_WHITE0 + W_COPY][0] = (uint8_t)(f->sl_mods & 1 ? 127 : 40);
    w[L_WHITE0 + W_DELETE][0] = (uint8_t)(f->sl_mods & 2 ? 127 : 40);
    w[L_WHITE0 + W_UNDO][0] = (uint8_t)(f->undo == f->sl ? 127 : 0);   /* (only while New's undo is there) */
    w[L_WHITE0 + W_LOOP][0] = (uint8_t)(f->sl_from == 1 ? 127 : 0);
}

int rfmv_st_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_fw_state_t *f = &m->fw;
    const int t = rfmv_st_on(m) - 1;
    char nm[48], c[48], b[16];
    int i;
    if (t < 0)
        return 0;
    mr_gfx_clear(cv, 0);
    rfmv_fw_slot_name(m, t, nm, sizeof nm);
    if (pop_up(m)) {                                    /* (a whole-screen word: 2x, the name or the state under it) */
        const char *top = "States", *sub = nm;
        switch (f->sl_pop) {
        case SP_STATE:
            snprintf(b, sizeof b, "State %d", f->sl_pk + 1);
            top = b, sub = WORDS[f->sl_pw % 5];
            break;
        case SP_NEW: top = "New state"; break;
        case SP_UNDO: top = "Undo", sub = "New state"; break;
        case SP_EMPTY:
            snprintf(b, sizeof b, "Track %d", f->sl_pslot + 1);
            top = b, sub = "empty";
            break;
        default: break;
        }
        v2_TC(cv, 64, 10, top, 2, 1);
        cut(sub, c, sizeof c, 128, 1);
        v2_TC(cv, 64, 36, c, 1, 1);
        return 1;
    }
    {                                                   /* the name at 2x (Copy / Delete held: its icon at the end) */
        const int icon = f->sl_mods & 2 ? 1 : f->sl_mods & 1 ? 2 : 0;
        cut(nm, c, sizeof c, icon ? 112 : 127, 2);
        v2_T(cv, 1, 0, c, 2, 1);
        if (icon)
            rfmv_held_icon(cv, icon, 116, 1);
    }
    {
        const int marks = f->sb_armed && buf_ok(m, t);  /* (copied: dashed, deleted: crossed, while armed) */
        for (i = 0; i < RFM_MOVE_FW_STATES; i++) {
            const int x = (i % 8) * 16 + 1, y = 16 + (i / 8) * 12, cur = f->st_cur[t] == i, used = f->st_used[t] >> i & 1;
            const int src = marks && i == f->sb_src;
            if (src && f->sb_kind == 2 && !used) {
                v2_X(cv, x, y, 14, 11);
                continue;
            }
            if (!cur && !used) {
                v2_R(cv, x + 6, y + 5, 2, 2, 1);
                continue;
            }
            snprintf(b, sizeof b, "%d", i + 1);
            if (cur) {
                v2_R(cv, x, y, 14, 11, 1);
                if (src && f->sb_kind == 1)
                    v2_dashO(cv, x - 1, y - 1, 16, 13);
            } else if (src && f->sb_kind == 1)
                v2_dashO(cv, x, y, 14, 11);
            else
                v2_O(cv, x, y, 14, 11, 1);
            v2_T(cv, x + (14 - v2_tw(b, 1)) / 2, y + 2, b, 1, !cur);
        }
    }
    return 1;
}
int rfmv_st_timed(const rfm_move_t *m)
{
    return rfmv_st_on(m) && (pop_up(m) || now_ms(m) - m->fw.sl_ms < 9 * RFMV_ST_SWEEP_MS);
}
int rfmv_st_base_words(const rfm_move_t *m, char *o, int cap)
{
    const int t = rfmv_st_on(m) - 1;
    char nm[48];
    if (t < 0)
        return 0;
    rfmv_fw_slot_name(m, t, nm, sizeof nm);
    snprintf(o, (size_t)cap, "States, %s", nm);
    return 0x480 + t;
}
int rfmv_st_pop_words(const rfm_move_t *m, char *o, int cap, uint16_t *subj, uint32_t *stamp)
{
    const rfm_move_fw_state_t *f = &m->fw;
    char nm[48];
    if (!pop_up(m))
        return 0;
    rfmv_fw_slot_name(m, rfmv_st_on(m) - 1, nm, sizeof nm);
    switch (f->sl_pop) {
    case SP_NAME: snprintf(o, (size_t)cap, "States, %s", nm); break;
    case SP_STATE: snprintf(o, (size_t)cap, "State %d %s", f->sl_pk + 1, WORDS[f->sl_pw % 5]); break;
    case SP_NEW: snprintf(o, (size_t)cap, "New state, %s", nm); break;
    case SP_UNDO: snprintf(o, (size_t)cap, "Undo, new state"); break;
    default: snprintf(o, (size_t)cap, "Track %d, empty", f->sl_pslot + 1); break;
    }
    *subj = (uint16_t)(0x4A0 | f->sl_pop), *stamp = f->sl_pop_ms;
    return 1;
}
