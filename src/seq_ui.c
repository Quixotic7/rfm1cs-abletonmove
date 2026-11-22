/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm_move's Sequencer mode (Track 4): the Move's controls on the sequencer, its LEDs and its screen. The data and
 * the engine are seq.c's; README.md, "Sequencer", has the gestures and the decisions (and where they
 * follow Ableton Move's own sequencer). */
#include <stdio.h>
#include <string.h>

#include "move_int.h"
#include "v2.h"

#define clampi rfmv_clampi
#define sc rfmv_sc
typedef rfmv_rgb_t rgb_t;

#define NUDGE 2                                         /* ticks a Left / Right with a step held (~10 % of a step) */
#define LEN_FINE 2                                      /* ticks a jog detent with a step held (~10 % of a step) */

/* the tracks' colours */
static const rgb_t TRACK_RGB[RFM_MOVE_SEQ_TRACKS] = {
    { 255, 42, 42 },  { 255, 128, 0 },  { 255, 208, 0 },   { 200, 255, 42 }, { 42, 255, 80 },   { 0, 255, 190 },
    { 26, 228, 255 }, { 40, 120, 255 }, { 120, 60, 255 },  { 196, 58, 255 }, { 255, 58, 200 },  { 255, 110, 140 },
    { 255, 170, 110 }, { 180, 255, 160 }, { 140, 220, 255 }, { 220, 200, 255 },
};
/* (0.12.0) the launch quantizations in a list's order (RFM_MOVE_LQ_* are 1 Bar first: the default 0) */
static const uint8_t LQ_ORDER[RFM_MOVE_LQ_N] = { RFM_MOVE_LQ_INSTANT, RFM_MOVE_LQ_BEAT, RFM_MOVE_LQ_BAR, RFM_MOVE_LQ_2BARS,
                                                 RFM_MOVE_LQ_4BARS, RFM_MOVE_LQ_CLIP };
static int touched_knob(const rfm_move_t *m);
/* Shift + step n (0-based), as printed on the Move (its manual): 1 Set Overview (our projects), 2 Setup (our clock
 * source), 3 Workflow Settings, 5 Tempo, 6 Metronome, 7 Groove, 8 Layout, 9 Keys & Scales, 10 Full Velocity (our Velocity menu),
 * 11 Repeat (our arp), 14 New Clip, 15 Double Loop, 16 Quantize */
enum { SH_PROJECTS = 0, SH_CLOCK = 1, SH_WORKFLOW = 2, SH_TEMPO = 4, SH_METRO = 5, SH_GROOVE = 6, SH_LAYOUT = 7,
       SH_KEYS = 8, SH_FIXVEL = 9, SH_ARP = 10, SH_CLEAR = 12, SH_NEWCLIP = 13, SH_DOUBLE = 14, SH_QUANT = 15 };
static int sh_menu(int s)                               /* the menu a Shift + step opens, 0 none */
{
    switch (s) {
    case SH_WORKFLOW: return RFM_MOVE_SM_WORKFLOW;
    case SH_TEMPO: return RFM_MOVE_SM_TEMPO;
    case SH_GROOVE: return RFM_MOVE_SM_GROOVE;
    case SH_LAYOUT: return RFM_MOVE_SM_LAYOUT;
    case SH_KEYS: return RFM_MOVE_SM_KEYS;
    case SH_ARP: return RFM_MOVE_SM_ARP;
    case SH_FIXVEL: return RFM_MOVE_SM_VEL;
    case SH_CLOCK: return RFM_MOVE_SM_GLOBAL;
    default: return 0;
    }
}
/* the icons under the steps while the Shift layer is on (Shift held or locked in the sequencer, Track 1 held in
 * FM-1 mode): every step with a shortcut dim, the ones on (a toggle, its menu open) full; off otherwise */
void rfmv_shift_icons(const rfm_move_t *m, uint8_t icon[16])
{
    const rfm_move_seq_t *q = &m->seq;
    int i;
    memset(icon, 0, 16);
    if (!((m->s.mode == RFM_MOVE_MODE_SEQ && (q->mods & SEQ_M_SHIFT)) || rfmv_t4_menu(m) ||
          (m->seq.projects && m->fw.fx && m->shift_down))) {
        /* (0.12.0, the owner's: "Icons should not be lit in session view or for the track sequencer steps. They get
         * lit up for shortcuts like holding shift or if you're holding the track button") session view at rest: dark
         * (0.11.0 lit the stop steps') */
        return;                                         /* (0.8.0: Track 4 held, Shift outside the FM-1 layout) */
    }
    for (i = 0; i < RFM_MOVE_NSTEPS; i++) {
        const int job = i == SH_PROJECTS || i == SH_CLOCK || i == SH_FIXVEL || i == SH_DOUBLE || i == SH_QUANT ||
                        i == SH_METRO || i == SH_NEWCLIP || i == SH_CLEAR || sh_menu(i);
        const int on = (i == SH_FIXVEL && m->s.vel_mode == RFM_MOVE_VEL_FIXED) || (i == SH_METRO && q->metro) || (i == SH_CLOCK && q->following) ||
                       (i == SH_PROJECTS && q->projects) || (sh_menu(i) && sh_menu(i) == q->smenu) ||
                       (i == SH_ARP && q->tr[q->sel_track].arp.mode != RFM_MOVE_AM_OFF);   /* (0.11.0: the arp on) */
        icon[i] = (uint8_t)(!job ? 0 : on ? 127 : 24);
    }
}


static rfm_move_seq_t *Q(rfm_move_t *m) { return &m->seq; }
int rfmv_sess_on(const rfm_move_t *m) { return m->s.mode == RFM_MOVE_MODE_SEQ && m->seq.view && !m->seq.projects; }
static int step_empty(const rfm_move_t *m, int clip, int abs)
{
    int lo;
    return rfmv_seq_range(m, clip, abs, &lo) == lo;
}
static int edit_clip(const rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    return SEQ_CLIP(q->sel_track, q->tr[q->sel_track].sel);
}
static int clip_len(const rfm_move_t *m) { return m->seq.len[edit_clip(m)]; }
static int vel_of(const rfm_move_t *m, int v) { (void)m; return clampi(v, 1, 127); }   /* (the pads' already through the
                                                         * velocity setting) */
static int running(const rfm_move_seq_t *q) { return q->playing || q->start_pending; }
static int root_note(const rfm_move_t *m) { return clampi(48 + m->s.root + 12 * m->s.octave, 0, 127); }



/* the layers Back closes, the latest first */
enum { LY_MENU = 1, LY_PROJ, LY_LOOP, LY_SHIFT, LY_CLIP, LY_PAGE };
static void layer(rfm_move_seq_t *q, int id)
{
    int i, k = 0;
    for (i = 0; i < q->nlayer; i++)                     /* (once: to the top) */
        if (q->layer[i] != id)
            q->layer[k++] = q->layer[i];
    q->nlayer = (uint8_t)k;
    if (q->nlayer < sizeof q->layer)
        q->layer[q->nlayer++] = (uint8_t)id;
}
static int layer_on(const rfm_move_seq_t *q, int id)
{
    switch (id) {
    case LY_MENU: return q->smenu != 0;
    case LY_PROJ: return q->projects != 0;
    case LY_LOOP: return q->loop_latch != 0;
    case LY_SHIFT: return q->shift_lock != 0;
    case LY_CLIP: return (q->clip_kind && (q->cut || q->paste)) || (q->clip_kind == 2 && q->sv_armed) || q->sc_armed;
    case LY_PAGE: return q->pg_kind != 0;
    default: return 0;
    }
}
/* a clip's last page: the loop's, or a later one with notes (past it: ghosts) */
static int clip_end_page(const rfm_move_t *m, int clip)
{
    int lo, hi = rfmv_seq_clip_range(m, clip, &lo), k, end = (rfmv_seq_loop_hi(m, clip) - 1) / RFM_MOVE_SEQ_PAGE;
    for (k = lo; k < hi; k++)
        if (m->seq.notes[k].step / RFM_MOVE_SEQ_PAGE > end)
            end = m->seq.notes[k].step / RFM_MOVE_SEQ_PAGE;
    return end;
}
int rfmv_seq_layer_open(const rfm_move_t *m)
{
    int id;
    for (id = LY_MENU; id <= LY_PAGE; id++)
        if (layer_on(&m->seq, id))
            return 1;
    return 0;
}
static void layer_close(rfm_move_t *m, int id)
{
    rfm_move_seq_t *q = &m->seq;
    switch (id) {
    case LY_MENU: q->smenu = 0; break;
    case LY_PROJ: q->projects = 0; break;
    case LY_LOOP:                                       /* (0.4.3: the sequencer's screen at once, not a second on) */
        q->loop_latch = 0, q->mods &= (uint8_t)~SEQ_M_LOOP;
        if (m->pop == POP_LOOP)
            m->pop = POP_NONE;
        break;
    case LY_SHIFT: q->shift_lock = 0, q->mods &= (uint8_t)~SEQ_M_SHIFT; break;
    case LY_CLIP:                                       /* (0.12.0: a clip or scene buffer stays, its marks go) */
        q->paste = 0, q->cut = 0, q->sv_armed = 0, q->sc_armed = 0;
        if (q->clip_kind != 2)
            q->clip_kind = 0;
        break;
    case LY_PAGE: q->pg_kind = 0; break;
    default: break;
    }
}
static void grow_to(rfm_move_t *m, int clip, int abs);

void rfmv_seq_msg(rfm_move_t *m, int msg)
{
    m->seq.msg = (uint8_t)msg;
    m->seq.msg_ms = (uint32_t)(m->seq.now_us / 1000u);
    m->msg_n++;                                         /* (0.6.0: speech: each message its own, even the same) */
}

void rfmv_seq_ui_release(rfm_move_t *m)
{
    rfm_move_seq_t *q = Q(m);
    int p;
    for (p = 0; p < RFM_MOVE_NPADS; p++)
        if (q->recn[p].on)
            rfmv_seq_pad_off(m, p, q->recn[p].note);
    q->mods = q->mods_used = 0, q->copy_src = 0, q->copy_pasted = 0, q->chord = 0;
    q->step_held = q->step_add = q->step_edit = 0, q->step_first = -1, q->step_popped = 0;
    q->loop_first = -1, q->fill = 0, q->steps_chord = 0, q->loop_latch = 0;
    q->cs_step = -1, q->cs_shown = 0, q->cs_k = -1;
    rfmv_seq_arp_stop(m);
}

/* the edited clip has notes but the track plays nothing: it plays (at once when stopped, else at the next bar) */
static void ensure_active(rfm_move_t *m)
{
    rfm_move_seq_t *q = Q(m);
    const rfm_move_track_t *t = &q->tr[q->sel_track];
    if (t->active < 0 && t->queued == SEQ_NONE && rfmv_seq_clip_used(m, edit_clip(m)))
        rfmv_seq_launch(m, q->sel_track, t->sel);
}
/* notes went into the edited clip: it plays (when the transport does: only Play starts it, the owner's) */
static void entered(rfm_move_t *m, int was_used)
{
    (void)was_used;
    ensure_active(m);
}

/* step entry's memory: the last notes (a step entered, a held step edited, a chord played), else the root */
static int last_notes(const rfm_move_t *m, uint8_t *note, uint8_t *vel)
{
    const rfm_move_seq_t *q = &m->seq;
    int i;
    if (!q->nlast) {
        note[0] = (uint8_t)root_note(m), vel[0] = 100;
        return 1;
    }
    for (i = 0; i < q->nlast; i++)
        note[i] = q->last_note[i], vel[i] = q->last_vel[i];
    return q->nlast;
}
static void last_add(rfm_move_seq_t *q, int note, int vel)
{
    int i;
    for (i = 0; i < q->nlast; i++)
        if (q->last_note[i] == note) {
            q->last_vel[i] = (uint8_t)vel;
            return;
        }
    if (q->nlast < RFM_MOVE_SEQ_POLY)
        q->last_note[q->nlast] = (uint8_t)note, q->last_vel[q->nlast] = (uint8_t)vel, q->nlast++;
}
static void last_from_step(rfm_move_t *m, int clip, int abs)
{
    rfm_move_seq_t *q = Q(m);
    int lo, hi = rfmv_seq_range(m, clip, abs, &lo), i;
    if (hi == lo)
        return;
    q->nlast = 0;
    for (i = lo; i < hi; i++)
        last_add(q, q->notes[i].note, q->notes[i].vel);
}
static void step_notes_clear(rfm_move_t *m, int clip, int abs)   /* the notes only (the locks stay) */
{
    int lo, hi = rfmv_seq_range(m, clip, abs, &lo);
    while (hi-- > lo)
        rfmv_seq_note_del(m, clip, abs, m->seq.notes[hi].note);
}

/* Copy released with nothing pressed: (0.12.0, the owner's: "Copy button should do nothing on quick click, there is a
 * shift shortcut to double and create new clips") nothing, in every view (it duplicated the clip into the track's next
 * empty slot, and dropped a waiting copy: Back's job) */
static void copy_alone(rfm_move_t *m) { (void)m; }
/* Delete released with nothing pressed (Move): the selected track's clip is deleted */
static void delete_alone(rfm_move_t *m)                 /* (0.6.3, the owner's) Delete is a modifier only, as on the
                                                         * Move: alone (a tap, or a hold let go) it changes nothing
                                                         * (before: the selected clip deleted); (0.6.4) nothing on the
                                                         * screen either: a hint for the screen reader only */
{
    rfmv_say_text(m, "Delete: hold and press a step, pad or clip");
}

/* a clip deleted: its notes and locks, and the clip itself (the slot empty again) */
void rfmv_seq_clip_delete(rfm_move_t *m, int clip)
{
    rfm_move_seq_t *q = Q(m);
    rfm_move_track_t *t = &q->tr[clip / RFM_MOVE_SEQ_SCENES];
    if (t->queued == clip % RFM_MOVE_SEQ_SCENES)
        t->queued = SEQ_STOP;                           /* (its launch: the track stops instead) */
    if (t->active == clip % RFM_MOVE_SEQ_SCENES) {
        rfmv_seq_release(m, clip / RFM_MOVE_SEQ_SCENES);
        if (running(q))
            rfmv_seq_launch(m, clip / RFM_MOVE_SEQ_SCENES, -1);
        else
            t->active = -1;
    }
    rfmv_seq_clip_clear(m, clip);
    rfmv_seq_clip_make(m, clip, 0);
}
/* the clipboard's clip at clip (0.9.2, as the steps'): a clip there is replaced; the cut's mark goes and the
 * clipboard stays: it pastes again and again, until something else is cut or copied */
static void paste_clip_at(rfm_move_t *m, int clip)
{
    rfm_move_seq_t *q = Q(m);
    const int track = clip / RFM_MOVE_SEQ_SCENES, scene = clip % RFM_MOVE_SEQ_SCENES;
    const int full = rfmv_seq_clip_has(m, clip);
    char w[40];
    rfmv_seq_group(m);
    if (full && q->tr[track].active == scene)
        rfmv_seq_release(m, track);                     /* (the replaced clip's sounding notes let go) */
    rfmv_seq_paste_clip(m, clip);
    q->cut = 0, q->sv_armed = 0;                        /* (0.12.0: a paste disarms) */
    if (full)
        snprintf(w, sizeof w, "Track %d clip %d replaced", track + 1, scene + 1);
    else
        snprintf(w, sizeof w, "Pasted to track %d clip %d", track + 1, scene + 1);
    rfmv_say_text(m, w);
}

/* Step record: the cursor a step on, round the loop (the steps are the track's rate) */
static void step_cursor_on(rfm_move_t *m)
{
    rfm_move_seq_t *q = Q(m);
    const int clip = edit_clip(m), lo = rfmv_seq_loop_lo(m, clip), hi = rfmv_seq_loop_hi(m, clip);
    q->step_cur = (int16_t)(q->step_cur + 1 >= hi || q->step_cur + 1 < lo ? lo : q->step_cur + 1);
    q->page = (uint8_t)(q->step_cur / RFM_MOVE_SEQ_PAGE);
}

/* ================================================================ tracks: 16, the Track buttons a bank of 4 === */
/* each track keeps its own pad layout and CC numbers: while in Sequencer mode the settings hold the selected
 * track's (MIDI / Control mode's are kept aside meanwhile) */
void rfmv_seq_sync(rfm_move_t *m)
{
    rfm_move_seq_t *q = Q(m);
    if (!q->in_seq)
        return;
    q->tr[q->sel_track].layout = m->s.layout;
    memcpy(q->tr[q->sel_track].cc, m->s.cc, sizeof m->s.cc);
    if (q->tr[q->sel_track].own & 2)                    /* the key and scale: the track's own, or the project's */
        q->tr[q->sel_track].root = m->s.root, q->tr[q->sel_track].scale = m->s.scale;
    else
        q->root = m->s.root, q->scale = m->s.scale;
}
static void load_track(rfm_move_t *m);
void rfmv_seq_load_track(rfm_move_t *m) { load_track(m); }
static void load_track(rfm_move_t *m)
{
    rfm_move_seq_t *q = Q(m);
    if (!q->in_seq)
        return;
    {
        const rfm_move_track_t *t = &q->tr[q->sel_track];
        m->s.layout = t->layout;
        memcpy(m->s.cc, t->cc, sizeof m->s.cc);
        m->s.root = (t->own & 2) ? t->root : q->root;
        m->s.scale = (t->own & 2) ? t->scale : q->scale;
    }
}
void rfmv_seq_enter(rfm_move_t *m)
{
    rfm_move_seq_t *q = Q(m);
    if (q->in_seq)
        return;
    q->stash_layout = m->s.layout, q->stash_root = m->s.root, q->stash_scale = m->s.scale;
    memcpy(q->stash_cc, m->s.cc, sizeof m->s.cc);
    q->in_seq = 1;
    load_track(m);
}
void rfmv_seq_leave(rfm_move_t *m)
{
    rfm_move_seq_t *q = Q(m);
    if (!q->in_seq)
        return;
    rfmv_seq_sync(m);
    q->in_seq = 0;
    m->s.layout = q->stash_layout, m->s.root = q->stash_root, m->s.scale = q->stash_scale;
    memcpy(m->s.cc, q->stash_cc, sizeof m->s.cc);
}
void rfmv_seq_select(rfm_move_t *m, int track)
{
    rfm_move_seq_t *q = Q(m);
    track = clampi(track, 0, RFM_MOVE_SEQ_TRACKS - 1);
    if (track == q->sel_track)
        return;
    rfmv_seq_sync(m);
    q->sel_track = (uint8_t)track, q->bank = (uint8_t)(track / 4);
    q->page = (uint8_t)(q->lstart[edit_clip(m)] / RFM_MOVE_SEQ_PAGE);
    q->steprec = 0;
    load_track(m);
    rfmv_seq_dirty(m);
}
/* (0.12.0, the owner's) Copy or Delete held + a scene pad (a row across the 16 tracks; its own buffer). Copy: a row
 * with a clip: armed (the last scene function a copy or a delete, not the row just copied): the buffer replaces it,
 * else it is copied (armed); an empty row: the buffer pasted. Delete: every clip of the row deleted, into the buffer
 * (armed); an empty row: nothing. One undo each; never a launch or a stop */
static void scene_edit(rfm_move_t *m, int scene, int del)
{
    rfm_move_seq_t *q = Q(m);
    const int has = rfmv_seq_scene_has(m, scene);
    char w[32];
    if (del) {
        int t;
        if (!has)
            return;
        rfmv_seq_group(m);
        rfmv_seq_scene_copy(m, scene);
        for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
            if (rfmv_seq_clip_has(m, SEQ_CLIP(t, scene)))
                rfmv_seq_clip_delete(m, SEQ_CLIP(t, scene));
        q->sc_kind = 2, q->sc_armed = 1, layer(q, LY_CLIP);
        snprintf(w, sizeof w, "Scene %d deleted", scene + 1);
    } else if (has && !(q->sc_armed && q->sc_kind && !(q->sc_src == scene && q->sc_kind == 1))) {
        rfmv_seq_scene_copy(m, scene);
        q->sc_kind = 1, q->sc_armed = 1, layer(q, LY_CLIP);
        snprintf(w, sizeof w, "Scene %d copied", scene + 1);
    } else if (q->sc_kind) {
        rfmv_seq_group(m);
        rfmv_seq_scene_paste(m, scene);
        q->sc_armed = 0;
        snprintf(w, sizeof w, has ? "Scene %d replaced" : "Pasted to scene %d", scene + 1);
    } else
        return;
    rfmv_say_text(m, w);
    rfmv_seq_dirty(m);
}
/* ================================================================ pads === */
void rfmv_seq_pad(rfm_move_t *m, int p, int vel, uint32_t now)
{
    rfm_move_seq_t *q = Q(m);
    rfm_move_hold_t *h = &m->pad[p];
    (void)now;
    if (!vel) {
        q->chord &= ~(1u << p);
        rfmv_pop_let(m, POP_PROJ, (3 - p / 8) * 8 + p % 8);
        if (h->kind == H_ARP)
            rfmv_seq_arp_off(m, h->b), h->kind = H_NONE;
        if (h->kind == H_NOTE)
            rfmv_seq_pad_off(m, p, h->b);
        if (q->steps_chord & (1u << p)) {              /* Step record: the chord let go, the cursor a step on */
            q->steps_chord &= ~(1u << p);
            if (!q->steps_chord)
                step_cursor_on(m);
        }
        return;
    }
    h->kind = H_MENU;
    if (q->projects) {                                  /* the grid: top-left = project 1 */
        const int slot = (3 - p / 8) * 8 + p % 8;
        if (q->mods & SEQ_M_DELETE) {
            q->mods_used |= SEQ_M_DELETE;
            rfmv_proj_delete(m, slot);
        } else {
            rfmv_pop(m, POP_PROJ, slot);                /* (its popup while the pad is held, also once it is open) */
            rfmv_proj_open(m, slot);
        }
        return;
    }
    if (q->view && p % 8 >= RFM_MOVE_SV_COLS) {         /* (0.10.0, the owner's review: as the Move's Control Live
                                                         * mode) session: the 8th column: the scenes. A press launches
                                                         * the row across the 16 tracks (at the next bar; stopped: at
                                                         * once, playing with Play); tracks with no clip in it stop */
        /* (0.11.0, the owner's) stopped: the scene plays (the one pad besides Play that starts the transport); a
         * double click: now, not at the bar; Shift + the pad: the scene's clips stop (at the bar; a double click: now).
         * A click acts at once: its double click is the same thing sooner, so nothing wrong shows meanwhile */
        const int scene = q->scene_off + (3 - p / 8), shift = (q->mods & SEQ_M_SHIFT) != 0;
        const int dbl = q->pad_tap == p && q->pad_tap_shift == shift && now - q->pad_tap_ms < RFM_MOVE_DOUBLE_MS;
        char w[24];
        if (q->mods & (SEQ_M_COPY | SEQ_M_DELETE | SEQ_M_MUTE)) {
            q->mods_used |= (uint8_t)(q->mods & (SEQ_M_COPY | SEQ_M_DELETE | SEQ_M_MUTE));
            q->pad_tap = -1;
            if (!(q->mods & SEQ_M_MUTE))
                scene_edit(m, scene, (q->mods & SEQ_M_DELETE) != 0);   /* (0.12.0) copy, paste, delete a scene */
            return;                                     /* (Mute + a scene: nothing; never a launch under these) */
        }
        q->pad_tap = (int8_t)(dbl ? -1 : p), q->pad_tap_ms = now, q->pad_tap_shift = (uint8_t)shift;
        if (shift) {
            q->mods_used |= SEQ_M_SHIFT;
            if (!rfmv_seq_stop_scene(m, scene, dbl))
                return;                                 /* (0.12.0, r2: nothing of it plays: nothing changes, nothing
                                                         * said) */
            snprintf(w, sizeof w, "Scene %d stop", scene + 1);
        } else {
            if (dbl)
                rfmv_seq_launch_scene_now(m, scene);
            else
                rfmv_seq_launch_scene(m, scene);
            if (!running(q)) {                          /* (as Play: Rec armed gives the bar's count-in) */
                q->ncap = 0;
                if (q->rec && q->rec_mode == RFM_MOVE_RM_RECORD)
                    rfmv_seq_count_in(m);
                else
                    rfmv_seq_start(m);
            }
            snprintf(w, sizeof w, "Scene %d", scene + 1);
        }
        rfmv_say_text(m, w);
        rfmv_seq_dirty(m);
        return;
    }
    if (q->view) {                                      /* session (the owner's mockup): a column a track (7 in view,
                                                         * Left / Right one track), a row a clip slot (4, Up / Down) */
        const int track = q->track_page + p % 8, scene = q->scene_off + (3 - p / 8);
        const int clip = SEQ_CLIP(track, scene);
        rfm_move_track_t *t = &q->tr[track];
        const int dbl = q->pad_tap == p && !q->pad_tap_shift && now - q->pad_tap_ms < RFM_MOVE_DOUBLE_MS;
        q->pad_tap = (int8_t)p, q->pad_tap_ms = now, q->pad_tap_shift = 0;
        if (q->mods & SEQ_M_SHIFT) {                    /* (0.10.0, as Control Live) Shift + a slot: selected for
                                                         * editing; nothing launched or stopped (the scenes: column 8) */
            char w[32];
            q->mods_used |= SEQ_M_SHIFT;
            rfmv_seq_select(m, track);
            t->sel = (int8_t)scene;
            q->page = (uint8_t)(q->lstart[clip] / RFM_MOVE_SEQ_PAGE);
            snprintf(w, sizeof w, "Track %d clip %d selected", track + 1, scene + 1);
            rfmv_say_text(m, w);
            rfmv_seq_dirty(m);
        } else if (dbl && !t->grow && !(q->mods & (SEQ_M_MUTE | SEQ_M_COPY | SEQ_M_DELETE))) {
            /* (0.11.0, the owner's) a double click: now, not at the bar: the clip plays (an empty slot: the track
             * stops). The first click queued the same thing, so nothing wrong showed meanwhile */
            rfmv_seq_select(m, track);
            if (rfmv_seq_clip_has(m, clip))
                rfmv_seq_launch_now(m, track, scene);
            else if ((t->active >= 0 && running(q)) || t->queued != SEQ_NONE)
                rfmv_seq_launch_now(m, track, -1);      /* (0.12.0, r2: an empty slot stops its track at once only when
                                                         * it plays something; else nothing) */
            t->sel = (int8_t)scene;
            q->pad_tap = -1;
            rfmv_seq_dirty(m);
        } else if (q->mods & SEQ_M_MUTE) {
            q->mods_used |= SEQ_M_MUTE;
            rfmv_seq_mute(m, track, !t->mute);
        } else if (q->mods & SEQ_M_COPY) {              /* (0.12.0, the owner's) Copy held + a clip: armed (the last
                                                         * clip function a copy or a delete): the buffer pasted over it
                                                         * (not over the very clip just copied: copied again); else it
                                                         * is copied (dashed), armed. An empty slot: the buffer pasted */
            const int has = rfmv_seq_clip_has(m, clip);
            q->mods_used |= SEQ_M_COPY;
            if (has && !(q->sv_armed && q->clip_kind == 2 && !(clip == q->clip_src && !q->cut))) {
                rfmv_seq_copy_clip(m, clip);
                q->clip_src = (uint8_t)clip, q->copy_src = 1, q->paste = 0, q->sv_armed = 1, layer(q, LY_CLIP);
            } else if (q->clip_kind == 2) {
                paste_clip_at(m, clip);
                q->copy_pasted = 1, q->paste = 0;
            }
        } else if (q->mods & SEQ_M_DELETE) {            /* (0.12.0, the owner's) Delete held + a clip: deleted at the
                                                         * press, into the clip buffer (an X in its slot), armed; one
                                                         * undo brings it back. Delete never pastes; an empty slot:
                                                         * nothing */
            q->mods_used |= SEQ_M_DELETE;
            if (rfmv_seq_clip_has(m, clip)) {
                rfmv_seq_group(m);
                rfmv_seq_copy_clip(m, clip);
                q->cut = 1, q->paste = 0, q->clip_src = (uint8_t)clip, q->sv_armed = 1, layer(q, LY_CLIP);
                rfmv_seq_clip_delete(m, clip);
            }
        } else {
            const int run = running(q), used = rfmv_seq_clip_has(m, clip);
            rfmv_seq_select(m, track);
            if (t->grow == 1 && t->active == scene)
                t->grow_stop = 1;                       /* a recording clip: done at the next bar */
            else if (!used && !q->rec) {                /* (0.10.0, the owner's review: as Live's stop button, the
                                                         * Move's empty slot) an empty slot: selected, not filled (the
                                                         * first note makes the clip); the track stops (at the bar) */
                t->sel = (int8_t)scene;
                q->page = 0;
                if (t->active >= 0 || t->queued >= 0)
                    rfmv_seq_launch(m, track, -1);
            } else if (!used) {                         /* Rec armed: a new clip there, recorded into (stopped: once
                                                         * Play starts it) */
                t->sel = (int8_t)scene;
                rfmv_seq_clip_make(m, clip, 1);
                if (!run) {
                    rfmv_seq_group(m);
                    q->rec_group = q->group;
                }
                t->grow = 2;                            /* (starts with the launch, at the bar) */
                rfmv_seq_launch(m, track, scene);
            } else if (t->active == scene && t->queued == SEQ_NONE && run)
                t->sel = (int8_t)scene;                 /* the playing clip: it plays on, selected */
            else {
                rfmv_seq_launch(m, track, scene);       /* at the next bar; stopped: at once, playing with Play */
            }
            rfmv_seq_dirty(m);
        }
        return;
    }
    /* track view: the MIDI mode's layout on the selected track's channel */
    {
        const int note = rfm_move_pad_note(&m->s, p), ch = q->tr[q->sel_track].ch, clip = edit_clip(m);
        int s;
        if (note < 0)
            return;
        if (q->mods & SEQ_M_DELETE) {
            char nb[8], w[24];
            q->mods_used |= SEQ_M_DELETE;
            rfmv_seq_group(m);
            rfmv_seq_note_remove_all(m, clip, note);
            snprintf(w, sizeof w, "Deleted %s", rfmv_note_name(note, nb, sizeof nb));
            rfmv_say_text(m, w);                        /* (0.6.3: speech) */
            return;
        }
        vel = vel_of(m, vel);
        if (!q->step_held && !q->steprec && rfmv_seq_arp_on(m, note, vel)) {   /* the track's arp plays it */
            h->kind = H_ARP, h->b = (uint8_t)note;
            return;
        }
        h->kind = H_NOTE, h->a = (uint8_t)ch, h->b = (uint8_t)note;
        rfmv_note_hold(m, ch, note, vel);
        if (m->s.layout == RFM_MOVE_LAYOUT_DRUM)
            m->edit_pad = (int8_t)p;
        if (q->step_held) {                             /* held steps: the note in or out of them */
            const int was = rfmv_seq_clip_used(m, clip);
            for (s = 0; s < RFM_MOVE_NSTEPS; s++)
                if (q->step_held >> s & 1) {
                    const int abs = q->page * RFM_MOVE_SEQ_PAGE + s;
                    if (rfmv_seq_find(m, clip, abs, note) >= 0)
                        rfmv_seq_note_del(m, clip, abs, note);
                    else {
                        const int e = step_empty(m, clip, abs);
                        rfmv_seq_note_put(m, clip, abs, note, vel, RFM_MOVE_SEQ_TPS, 0), grow_to(m, clip, abs);
                        if (e)
                            rfmv_seq_tdef_apply(m, clip, abs, 1);
                    }
                    q->step_edit |= (uint16_t)(1u << s);
                }
            entered(m, was);
            return;                                     /* (it sounds while held; nothing recorded) */
        }
        if (!q->chord)                                  /* a new chord (or note): step entry's memory */
            q->nlast = 0;
        q->chord |= 1u << p, q->pad_vel[p] = (uint8_t)vel;
        last_add(q, note, vel);
        if (q->steprec) {                               /* Step record: into the cursor's step (a new chord replaces
                                                         * what was there) */
            const int was = rfmv_seq_clip_used(m, clip);
            if (!q->steps_chord) {
                rfmv_seq_group(m);
                step_notes_clear(m, clip, q->step_cur);
            }
            q->steps_chord |= 1u << p;
            {
                const int e = step_empty(m, clip, q->step_cur);
                rfmv_seq_note_put(m, clip, q->step_cur, note, vel, RFM_MOVE_SEQ_TPS, 0);
                if (e)
                    rfmv_seq_tdef_apply(m, clip, q->step_cur, 1);
            }
            entered(m, was);
            return;
        }
        rfmv_seq_pad_on(m, p, note, vel);
    }
}

/* a step entered past the loop's end (a pending page, Right past the last): the loop grows to that page's end */
static void grow_to(rfm_move_t *m, int clip, int abs)
{
    const int lo = rfmv_seq_loop_lo(m, clip), hi = rfmv_seq_loop_hi(m, clip);
    if (abs >= hi && rfmv_seq_clip_used(m, clip) && rfmv_seq_range(m, clip, abs, &(int){ 0 }) > 0)
        rfmv_seq_loop_set(m, clip, lo, (abs / RFM_MOVE_SEQ_PAGE + 1) * RFM_MOVE_SEQ_PAGE - lo);
}

/* ---- Loop mode's pages: Delete + a page with notes cuts it (an X), Copy + one copies it (dashed), either + an
 * empty page pastes the page clipboard there (a pasted cut empties it) */
static int page_full(const rfm_move_t *m, int clip, int pg)
{
    int lo, hi = rfmv_seq_clip_range(m, clip, &lo), i;
    for (i = lo; i < hi; i++)
        if (m->seq.notes[i].step / RFM_MOVE_SEQ_PAGE == pg)
            return 1;
    hi = rfmv_seq_clip_lock_range(m, clip, &lo);
    for (i = lo; i < hi; i++)
        if (m->seq.locks[i].step / RFM_MOVE_SEQ_PAGE == pg)
            return 1;
    return 0;
}
static void page_edit(rfm_move_t *m, int clip, int pg, int del)
{
    rfm_move_seq_t *q = Q(m);
    const int a = pg * RFM_MOVE_SEQ_PAGE;
    int lo, hi, i;
    if (page_full(m, clip, pg)) {                       /* into the page clipboard (Delete: and gone) */
        q->pg_nn = q->pg_nl = 0;
        hi = rfmv_seq_clip_range(m, clip, &lo);
        for (i = lo; i < hi; i++)
            if (q->notes[i].step / RFM_MOVE_SEQ_PAGE == pg && q->pg_nn < sizeof q->pg_notes / sizeof q->pg_notes[0])
                q->pg_notes[q->pg_nn] = q->notes[i], q->pg_notes[q->pg_nn++].step -= (uint8_t)a;
        hi = rfmv_seq_clip_lock_range(m, clip, &lo);
        for (i = lo; i < hi; i++)
            if (q->locks[i].step / RFM_MOVE_SEQ_PAGE == pg && q->pg_nl < sizeof q->pg_locks / sizeof q->pg_locks[0])
                q->pg_locks[q->pg_nl] = q->locks[i], q->pg_locks[q->pg_nl++].step -= (uint8_t)a;
        q->pg_kind = (uint8_t)(del ? 2 : 1), q->pg_src = (uint8_t)pg, q->pg_clip = (uint8_t)clip;
        if (del) {
            rfmv_seq_group(m);
            for (i = a; i < a + RFM_MOVE_SEQ_PAGE; i++)
                rfmv_seq_step_clear(m, clip, i);
        }
        layer(q, LY_PAGE);
        return;
    }
    if (!q->pg_kind)
        return;
    rfmv_seq_group(m);                                  /* an empty page: the clipboard pasted */
    for (i = 0; i < q->pg_nn; i++) {
        const rfm_move_note_t *n = &q->pg_notes[i];
        rfmv_seq_note_put(m, clip, a + n->step, n->note, n->vel, n->len, n->micro);
    }
    for (i = 0; i < q->pg_nl; i++)
        rfmv_seq_lock_set(m, clip, a + q->pg_locks[i].step, q->pg_locks[i].cc, q->pg_locks[i].val);
    if (q->pg_kind == 2)
        q->pg_kind = 0;
    ensure_active(m);
}
int rfmv_seq_src_page(const rfm_move_t *m, int clip, int pg)   /* 1 copied, 2 cut, 0 */
{
    const rfm_move_seq_t *q = &m->seq;
    return q->pg_kind && q->pg_clip == clip && q->pg_src == pg ? q->pg_kind : 0;
}

/* the clipboard's source is marked (a step or a clip: dashed when copied, an X when cut) */
static int src_shown(const rfm_move_seq_t *q, int kind)
{
    return q->clip_kind == kind && (q->cut || q->paste || ((q->mods & SEQ_M_COPY) && q->copy_src));
}
int rfmv_seq_src_step(const rfm_move_t *m, int clip, int abs)   /* 1 copied, 2 cut, 0 */
{
    const rfm_move_seq_t *q = &m->seq;
    int i;
    if (!src_shown(q, 1) || q->clip_src != clip)
        return 0;
    for (i = 0; i < q->clip_nn; i++)                    /* (0.6.3: a group: each of its steps) */
        if (q->clip_notes[i].step == abs)
            return 1 + q->cut;
    for (i = 0; i < q->clip_nl; i++)
        if (q->clip_locks[i].step == abs)
            return 1 + q->cut;
    return 0;
}
int rfmv_seq_src_clip(const rfm_move_t *m, int clip)
{
    const rfm_move_seq_t *q = &m->seq;
    if (q->sc_armed && q->sc_kind && (q->mods & (SEQ_M_COPY | SEQ_M_DELETE)) && clip % RFM_MOVE_SEQ_SCENES == q->sc_src &&
        (q->sb_has >> (clip / RFM_MOVE_SEQ_SCENES) & 1))
        return q->sc_kind;                              /* (0.12.0) the scene buffer's row, while Copy or Delete is held */
    return q->clip_kind == 2 && q->sv_armed && q->clip_src == clip ? 1 + q->cut : 0;   /* (0.12.0: while armed) */
}

/* the clipboard's step at (clip, abs); a cut (already gone from its source) is pasted once */
static void paste_step_at(rfm_move_t *m, int clip, int abs)
{
    rfm_move_seq_t *q = Q(m);
    const int was = rfmv_seq_clip_used(m, clip);
    int lo;
    const int full = rfmv_seq_range(m, clip, abs, &lo) > lo;   /* (a step with notes: replaced) */
    char w[32];
    rfmv_seq_group(m);
    rfmv_seq_paste_step(m, clip, abs);
    q->cut = 0;                                         /* (0.6.3: the cut's marks go; the clipboard stays: it pastes
                                                         * again, until something else is cut or copied) */
    grow_to(m, clip, abs);
    entered(m, was);
    if (full)
        snprintf(w, sizeof w, "Step %d replaced", abs % RFM_MOVE_SEQ_PAGE + 1);
    else
        snprintf(w, sizeof w, "Pasted to step %d", abs % RFM_MOVE_SEQ_PAGE + 1);
    rfmv_say_text(m, w);
}
/* (0.6.3) a step into the clipboard (cut: Delete held; copied: Copy held): a new clipboard, or, in the same hold on
 * the same clip, one more step of its group */
static void clip_take(rfm_move_t *m, int clip, int abs, int cut, int more)
{
    rfm_move_seq_t *q = Q(m);
    char w[32];
    if (more && q->clip_kind == 1 && q->clip_src == clip && !q->cut == !cut)
        rfmv_seq_copy_step_add(m, clip, abs);
    else {
        rfmv_seq_copy_step(m, clip, abs);
        q->clip_src = (uint8_t)clip, q->clip_step = (uint8_t)abs;
    }
    q->cut = (uint8_t)(cut != 0), q->paste = 0;
    layer(q, LY_CLIP);
    snprintf(w, sizeof w, "Step %d %s", abs % RFM_MOVE_SEQ_PAGE + 1, cut ? "cut" : "copied");
    rfmv_say_text(m, w);
}

/* ================================================================ steps === */
/* ---- (0.11.0, the owner's UX rules) a held Track's tracks (a step; in session view a column's pad): what the press
 * means is decided before anything shows. A hold acts once held RFM_MOVE_SEQ_HOLD_MS (rfmv_seq_ui_poll: while still
 * down, not at the release); a click acts once RFM_MOVE_DOUBLE_MS have passed with no second press (or the Track
 * button is let go); a second press in that time is the double click, at once. what: 0 selected, 1 routed / not,
 * 2 the firmware's solo */
static void tk_act(rfm_move_t *m, int fw, int track, int what)
{
    rfm_move_seq_t *q = Q(m);
    rfmv_menu(m, MENU_LOCK, track);
    switch (what) {
    case 0:
        if (m->s.lock[fw] != track)
            m->s.lock[fw] = (uint8_t)track, rfmv_changed(m);
        rfmv_route_on(m, fw, track);                    /* (selected: it reaches the firmware; a solo there stays) */
        if (fw == (m->fw.active % RFM_MOVE_FW_SLOTS) && m->s.mode == RFM_MOVE_MODE_SEQ)
            rfmv_seq_select(m, track);                  /* (the slot played: its track view edits it now) */
        m->lk_word = LK_LOCKED;
        break;
    case 1:
        rfmv_route_toggle(m, fw, track);
        m->lk_word = (uint8_t)(q->fx.ffilt[fw] >> track & 1 ? LK_ON : LK_OFF);
        break;
    default: m->lk_word = (uint8_t)(rfmv_fw_solo(m, fw, track) ? LK_SOLO : LK_UNSOLO); break;
    }
}
void rfmv_seq_tk_flush(rfm_move_t *m)                   /* the click that waits acts now */
{
    rfm_move_seq_t *q = Q(m);
    const int track = q->tk_pend;
    if (track < 0)
        return;
    q->tk_pend = -1;
    tk_act(m, q->tk_pend_fw, track, q->tk_pend_kind);
}
static int tk_press(rfm_move_t *m, int fw, int track, int kind)   /* a press: 1 it was the double click (done) */
{
    rfm_move_seq_t *q = Q(m);
    if (q->tk_pend == track && q->tk_pend_kind == kind && q->tk_pend_fw == fw &&
        rfmv_now_ms(m) - q->tk_pend_clk < RFM_MOVE_DOUBLE_MS) {
        q->tk_pend = -1;
        tk_act(m, fw, track, 2);
        return 1;
    }
    rfmv_seq_tk_flush(m);                               /* (another track's click: it acts first) */
    return 0;
}
static void tk_click(rfm_move_t *m, int fw, int track, int kind)
{
    rfm_move_seq_t *q = Q(m);
    q->tk_pend = (int8_t)track, q->tk_pend_kind = (uint8_t)kind, q->tk_pend_fw = (uint8_t)fw;
    q->tk_pend_clk = rfmv_now_ms(m);
}
/* session view, a firmware Track held: a column's pad: a click routes its track to the firmware or not, a double click
 * solos it there (or ends the solo) */
void rfmv_seq_route_pad(rfm_move_t *m, int fw, int p)
{
    rfm_move_seq_t *q = Q(m);
    const int track = q->track_page + p % 8;
    rfmv_menu(m, MENU_TRACK, 0);                        /* (the Track button was used: its release is no tap) */
    if (p % 8 >= RFM_MOVE_SV_COLS || track >= RFM_MOVE_SEQ_TRACKS)
        return;
    if (!tk_press(m, fw, track, 1))
        tk_click(m, fw, track, 1);
}

void rfmv_seq_step(rfm_move_t *m, int s, int down, uint32_t now)
{
    rfm_move_seq_t *q = Q(m);
    const uint16_t bit = (uint16_t)(1u << s);
    const int clip = edit_clip(m), abs = q->page * RFM_MOVE_SEQ_PAGE + s;
    if (!down) {
        if (q->loop_first == s)
            q->loop_first = -1;
        if (q->cs_step == s)                            /* (0.12.0) a select step let go: its settings go (a hold's
                                                         * release: nothing more) */
            q->cs_step = -1, q->cs_shown = 0, q->cs_k = -1;
        if (q->tstep_held & (1u << s)) {                /* (0.8.0) a firmware Track held, a track's step: a click
                                                         * selects it for that firmware, a double click solos it there,
                                                         * a hold routes it to the firmware or not; (0.11.0) gated: the
                                                         * hold and the double click have acted by now (tk_press, the
                                                         * poll), the click waits out the double click's time */
            const int fw = rfmv_seq_trk_held(m) - 1, fired = (int)(q->tstep_fired >> s & 1);
            q->tstep_held &= ~(1u << s), q->tstep_fired &= ~(1u << s);
            if (fw >= 0 && !fired)
                tk_click(m, fw, s, 0);
            return;
        }
        if (q->step_held & bit) {
            int lo;
            const int has = rfmv_seq_range(m, clip, abs, &lo) > lo;
            const int held = (q->step_popped && q->step_first == s) || rfmv_now_ms(m) - q->step_t[s] >= RFM_MOVE_SEQ_HOLD_MS;
            if (!((q->step_add | q->step_edit) & bit) && !held) {   /* a tap: on (the last notes) or off (Move) */
                if (has)
                    step_notes_clear(m, clip, abs);
                else {
                    uint8_t nn[RFM_MOVE_SEQ_POLY], vv[RFM_MOVE_SEQ_POLY];
                    const int n = last_notes(m, nn, vv), was = rfmv_seq_clip_used(m, clip);
                    int i, put = 0;
                    for (i = 0; i < n; i++)
                        put |= rfmv_seq_note_put(m, clip, abs, nn[i], vel_of(m, vv[i]), RFM_MOVE_SEQ_TPS, 0) == 0;
                    if (put)
                        rfmv_seq_tdef_apply(m, clip, abs, 1), grow_to(m, clip, abs), entered(m, was);
                }
            } else if (has)
                last_from_step(m, clip, abs);           /* entered or edited: its notes are the next empty step's */
            q->step_held &= (uint16_t)~bit, q->step_add &= (uint16_t)~bit, q->step_edit &= (uint16_t)~bit;
            if (q->step_first == s) {
                int i;
                q->step_first = -1;
                for (i = 0; i < RFM_MOVE_NSTEPS && q->step_first < 0; i++)
                    if (q->step_held >> i & 1)
                        q->step_first = (int8_t)i;
            }
            if (m->pop == POP_STEP && m->pop_i == s) {  /* its popup (a hold's): the next held step's, or it
                                                         * goes at the release (the owner's: only while held) */
                if (q->step_first >= 0 && q->step_popped)
                    rfmv_pop(m, POP_STEP, q->step_first);
                else
                    m->pop = POP_NONE;
            }
            if (!q->step_held)
                q->step_popped = 0;
        }
        return;
    }
    if (q->projects && !rfmv_seq_trk_held(m) && !((q->mods & SEQ_M_SHIFT) && m->fw.fx)) {
        if ((q->mods & SEQ_M_SHIFT) && s == 0)          /* (0.9.0: the Mixer / Routing take the Shift shortcuts; a held
                                                         * Track's tracks work over any view) */
            q->projects = 0, q->mods_used |= SEQ_M_SHIFT;
        return;
    }
    if (rfmv_seq_trk_held(m)) {                         /* a Track button held: steps are the 16 tracks */
        rfmv_menu(m, MENU_TRACK, s);                    /* (Track 4 used: its release is no tap) */
        if (q->mods & SEQ_M_COPY) {
            q->mods_used |= SEQ_M_COPY;
            if (!q->copy_src || q->clip_kind != 3) {
                q->clip_kind = 3, q->clip_src = (uint8_t)s, q->copy_src = 1, q->cut = 0;
            } else {
                rfmv_seq_group(m);
                rfmv_seq_track_copy(m, q->clip_src, s);
                q->clip_kind = 3, q->copy_pasted = 1;   /* (paste again elsewhere while Copy is held) */
            }
        } else if (q->mods & SEQ_M_DELETE) {
            char w[32];
            q->mods_used |= SEQ_M_DELETE;
            rfmv_seq_group(m);
            rfmv_seq_track_clear(m, s);
            snprintf(w, sizeof w, "Track %d cleared", s + 1);
            rfmv_say_text(m, w);
            m->lk_word = LK_CLEARED;
        } else if (q->mods & SEQ_M_MUTE) {
            q->mods_used |= SEQ_M_MUTE;
            rfmv_seq_mute(m, s, !q->tr[s].mute);
            m->lk_word = (uint8_t)(q->tr[s].mute ? LK_MUTE : LK_UNMUTE);
        } else if (q->paste && q->clip_kind == 3) {
            rfmv_seq_group(m);
            rfmv_seq_track_copy(m, q->clip_src, s);
            q->paste = 0, q->clip_kind = 0;
        } else {                                        /* (a click selects, a double click solos, a hold routes or
                                                         * not: tk_press, the poll, the release above) */
            q->tstep_fired &= ~(1u << s);
            if (tk_press(m, rfmv_seq_trk_held(m) - 1, s, 0))
                q->tstep_fired |= 1u << s;
            q->tstep_held |= 1u << s;
            q->step_t[s] = rfmv_now_ms(m);
        }
        return;
    }
    if (q->smenu == RFM_MOVE_SM_ARP && q->arp_page && !(q->mods & SEQ_M_SHIFT)) {
        /* (0.11.0, the owner's) an arp lane's page: the 16 step keys are the lane's 16 steps: a press picks one (knob 1
         * its function or semitones). The menu has the steps while it is open: none reaches the clip under it */
        q->arp_sel = (uint8_t)s;
        q->sm_k = 0, q->sm_ms = rfmv_now_ms(m);
        {
            char w[40];
            rfmv_seq_smenu_words(m, 0, w, sizeof w);
            rfmv_say_text(m, w);
        }
        return;
    }
    if ((q->mods & SEQ_M_LOOP) && (q->mods & (SEQ_M_DELETE | SEQ_M_COPY)) && !q->view) {   /* Loop on: Delete / Copy
                                                         * work on pages as on steps (their own clipboard) */
        page_edit(m, clip, s, (q->mods & SEQ_M_DELETE) != 0);
        q->mods_used |= (uint8_t)(SEQ_M_LOOP | (q->mods & (SEQ_M_DELETE | SEQ_M_COPY)));
        return;
    }
    if ((q->mods & SEQ_M_COPY) && !q->view) {           /* Copy + step (the owner's, as Delete): a step with notes
                                                         * or locks is copied (dashed); an empty one gets the
                                                         * clipboard (a copy, or a Delete's cut) pasted */
        int lo, llo;
        q->mods_used |= SEQ_M_COPY;
        if (rfmv_seq_range(m, clip, abs, &lo) > lo || rfmv_seq_lock_range(m, clip, abs, &llo) > llo) {
            clip_take(m, clip, abs, 0, q->copy_src);    /* (0.6.3: more in the same hold: a group) */
            q->copy_src = 1;
        } else if (q->clip_kind == 1) {
            paste_step_at(m, clip, abs);
            q->copy_pasted = 1, q->paste = 0;
        }
        return;
    }
    if (q->mods & SEQ_M_SHIFT) {                        /* the shortcuts */
        q->mods_used |= SEQ_M_SHIFT;
        if (s == SH_TEMPO) {                            /* Shift + step 5: the tempo menu, and each press a tap (tap
                                                         * tempo) */
            rfmv_seq_tap(m, now);
            q->smenu = RFM_MOVE_SM_TEMPO;
            layer(q, LY_MENU);
            return;
        }
        if (sh_menu(s)) {                               /* a menu: open (again: closed) */
            q->smenu = (uint8_t)(q->smenu == sh_menu(s) ? 0 : sh_menu(s));
            q->arp_page = 0, m->gl_edit = 0;
            if (q->smenu)
                layer(q, LY_MENU);
            rfmv_menu(m, MENU_NONE, 0);
            return;
        }
        switch (s) {
        case SH_PROJECTS: rfmv_view_open(m, VIEW_PROJECTS); break;   /* (projects mode: a mode of its own; 0.8.0:
                                                         * the sequencer's projects, as Track 4's double click) */
        case SH_METRO:
            q->metro = (uint8_t)!q->metro;
            rfmv_seq_msg(m, q->metro ? SEQ_MSG_METRO_ON : SEQ_MSG_METRO_OFF);
            break;
        case SH_NEWCLIP: {                              /* the track's next empty slot: a new clip, selected */
            rfm_move_track_t *t = &q->tr[q->sel_track];
            int k;
            for (k = 1; k <= RFM_MOVE_SEQ_SCENES; k++) {
                const int sc2 = (t->sel + k) % RFM_MOVE_SEQ_SCENES;
                if (!rfmv_seq_clip_has(m, SEQ_CLIP(q->sel_track, sc2))) {
                    rfmv_seq_group(m);                  /* (0.12.0: made is journaled: its own undo) */
                    rfmv_seq_clip_make(m, SEQ_CLIP(q->sel_track, sc2), 1);
                    t->sel = (int8_t)sc2, q->page = 0;
                    rfmv_seq_msg(m, SEQ_MSG_NEWCLIP);
                    break;
                }
            }
            if (k > RFM_MOVE_SEQ_SCENES)
                rfmv_seq_msg(m, SEQ_MSG_NOCLIP);
            break;
        }
        case SH_CLEAR: {                                /* Shift + step 13 (the owner's): the clip cleared (steps,
                                                         * locks, trigs), its length kept; one undo */
            int lo, hi;
            rfmv_seq_group(m);
            rfmv_seq_release(m, q->sel_track);
            hi = rfmv_seq_clip_range(m, clip, &lo);
            while (hi-- > lo)
                rfmv_seq_note_del(m, clip, q->notes[hi].step, q->notes[hi].note);
            hi = rfmv_seq_clip_lock_range(m, clip, &lo);
            while (hi-- > lo)
                rfmv_seq_lock_set(m, clip, q->locks[hi].step, q->locks[hi].cc, -1);
            rfmv_seq_dirty(m);
            rfmv_seq_msg(m, SEQ_MSG_CLIPCLEAR);
            break;
        }
        case SH_QUANT:
            rfmv_seq_group(m);
            rfmv_seq_clip_quantize(m, clip);
            rfmv_seq_msg(m, SEQ_MSG_QUANT);
            break;
        case SH_DOUBLE:
            rfmv_seq_group(m);
            if (rfmv_seq_clip_double(m, clip) == 0)
                rfmv_seq_msg(m, SEQ_MSG_DOUBLE);
            break;
        default: break;
        }
        return;
    }
    if (q->mods & SEQ_M_MUTE) {                         /* Mute + step: track 1..16 */
        q->mods_used |= SEQ_M_MUTE;
        rfmv_seq_mute(m, s, !q->tr[s].mute);
        return;
    }
    if (q->mods & SEQ_M_LOOP) {                         /* Loop + step: the pages. A tap shows the page; two held
                                                         * loop the pages from one to the other; a double tap loops
                                                         * that page alone */
        q->mods_used |= SEQ_M_LOOP;
        if (q->loop_first >= 0 && q->loop_first != s) {
            const int a = q->loop_first < s ? q->loop_first : s, b = q->loop_first < s ? s : q->loop_first;
            rfmv_seq_group(m);
            rfmv_seq_loop_set(m, clip, a * RFM_MOVE_SEQ_PAGE, (b - a + 1) * RFM_MOVE_SEQ_PAGE);
            if (q->page < a || q->page > b)
                q->page = (uint8_t)a;                   /* (the page shown: in the new loop) */
            q->loop_tap = -1;
        } else if (s > clip_end_page(m, clip) && s <= q->page) {   /* a ghost page's key: the new bars confirmed
                                                         * (the clip to that page) */
            const int lo = rfmv_seq_loop_lo(m, clip);
            rfmv_seq_group(m);
            rfmv_seq_loop_set(m, clip, lo, (s + 1) * RFM_MOVE_SEQ_PAGE - lo);
            q->page = (uint8_t)s, q->loop_tap = -1, q->loop_first = (int8_t)s;
        } else {
            if (q->loop_tap == s && now - q->loop_tap_ms < 400) {
                rfmv_seq_group(m);
                rfmv_seq_loop_set(m, clip, s * RFM_MOVE_SEQ_PAGE, RFM_MOVE_SEQ_PAGE);
                q->loop_tap = -1;
            } else
                q->loop_tap = (int8_t)s, q->loop_tap_ms = now;
            q->page = (uint8_t)s, q->loop_first = (int8_t)s;
        }
        rfmv_pop(m, POP_LOOP, 0);
        return;
    }
    if (q->view) {                                      /* session (0.10.0, the owner's review: the Move's Control
                                                         * Live steps): the 2 steps under a track's column: the odd
                                                         * one selects the track, the even one stops its clip; step 16:
                                                         * every clip stops (at the bar). Mute: Mute + a clip or a step */
        const int track = q->track_page + s / 2;
        if (s == RFM_MOVE_NSTEPS - 1) {
            int t;
            for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
                if (q->tr[t].active >= 0 || q->tr[t].queued >= 0)
                    rfmv_seq_launch(m, t, -1);
            rfmv_say_text(m, "Stop all clips");
        } else if (s / 2 >= RFM_MOVE_SV_COLS)
            return;                                     /* (step 15, under the scenes: nothing) */
        else if (!(s & 1)) {
            rfmv_seq_select(m, track);
            if (!q->smenu)                              /* (0.12.0, the owner's) held: its clip's settings (the poll,
                                                         * a knob touched); a click: only the select */
                q->cs_step = (int8_t)s, q->cs_shown = 0, q->cs_k = -1, q->cs_clk = rfmv_now_ms(m);
        }
        else if (q->tr[track].active >= 0 || q->tr[track].queued >= 0)
            rfmv_seq_launch(m, track, -1);              /* (at the next bar; stopped: at once) */
        rfmv_seq_dirty(m);
        return;
    }
    if (q->mods & SEQ_M_DELETE) {                       /* Delete held + steps (0.6.3, the owner's): after a cut,
                                                         * any step gets the clipboard pasted (a step with notes:
                                                         * replaced); else (after a paste, or the first press) a step
                                                         * with notes or locks is cut (gone, into the clipboard, an X
                                                         * on it) and an empty one gets the clipboard pasted. So: cut
                                                         * A, A again pastes it back, empty steps paste, B cuts, C right
                                                         * after pastes over C, C again cuts. The clipboard stays */
        int lo, llo;
        const int full = rfmv_seq_range(m, clip, abs, &lo) > lo || rfmv_seq_lock_range(m, clip, abs, &llo) > llo;
        q->mods_used |= SEQ_M_DELETE;
        if (m->del_src != 1 && full) {
            rfmv_seq_group(m);
            clip_take(m, clip, abs, 1, 0);
            rfmv_seq_step_clear(m, clip, abs);
            m->del_src = 1;                             /* (the last: a cut) */
        } else if (q->clip_kind == 1) {
            paste_step_at(m, clip, abs);
            m->del_src = 2;                             /* (the last: a paste) */
        }
        return;
    }
    if (q->paste && q->clip_kind == 1 && !q->step_held) {   /* a copy waiting (Copy released): pasted here */
        paste_step_at(m, clip, abs);
        q->paste = 0;
        return;
    }
    if (q->step_first >= 0 && !(q->step_held & bit)) { /* a second step: the first's notes reach to it */
        const int first = q->page * RFM_MOVE_SEQ_PAGE + q->step_first;
        if (abs > first) {
            int lo, hi = rfmv_seq_range(m, clip, first, &lo), i;
            for (i = lo; i < hi; i++) {
                const rfm_move_note_t n = m->seq.notes[i];
                rfmv_seq_note_put(m, clip, n.step, n.note, n.vel, (abs - first + 1) * RFM_MOVE_SEQ_TPS, n.micro);
            }
            q->step_edit |= (uint16_t)(1u << q->step_first);
        }
        {
            char w[24];                                 /* (0.6.5: speech: just the step) */
            snprintf(w, sizeof w, "Step %d", s + 1);
            rfmv_say_text(m, w);
        }
        return;
    }
    if (!q->step_held)
        rfmv_seq_group(m);                              /* one hold, one undo */
    q->step_held |= bit;
    q->step_t[s] = rfmv_now_ms(m);
    if (q->step_first < 0)
        q->step_first = (int8_t)s, q->step_popped = 0;
    {
        const int was = rfmv_seq_clip_used(m, clip);
        int lo, hi = rfmv_seq_range(m, clip, abs, &lo), p, put = 0;
        if (q->chord) {                                 /* pads held: the step gets exactly those notes */
            const int e = hi == lo;
            step_notes_clear(m, clip, abs);
            q->nlast = 0;
            for (p = 0; p < RFM_MOVE_NPADS; p++)
                if ((q->chord >> p & 1) && m->pad[p].kind == H_NOTE) {
                    const int v = vel_of(m, q->pad_vel[p]);
                    put |= rfmv_seq_note_put(m, clip, abs, m->pad[p].b, v, RFM_MOVE_SEQ_TPS, 0) == 0;
                    last_add(q, m->pad[p].b, v);
                }
            q->step_add |= bit;
            if (put && e)
                rfmv_seq_tdef_apply(m, clip, abs, 1);
            if (put)
                grow_to(m, clip, abs), entered(m, was);
        } else if (hi > lo && !running(q))
            rfmv_seq_preview(m, clip, abs);             /* (stopped only, the owner's: playing, a held step adds
                                                         * nothing) */             /* a step with notes: heard (a tap clears it at the release,
                                                         * an empty one is filled then: holding it leaves it for the
                                                         * pads) */
    }
}

/* ---- (0.12.0, the owner's) session view, a column's select step held (300 ms, or a knob touched or turned while it
 * is down): the settings of its track's selected clip until it is let go: knob 1 Launch (the project's, or its own),
 * knob 2 After (Loop, Next); knobs 3..8 dark and dead; the volume knob: the level of the firmware the track plays */
static int cs_track(const rfm_move_t *m) { return clampi(m->seq.track_page + m->seq.cs_step / 2, 0, RFM_MOVE_SEQ_TRACKS - 1); }
static int cs_clip(const rfm_move_t *m) { return SEQ_CLIP(cs_track(m), m->seq.tr[cs_track(m)].sel); }
int rfmv_seq_cs_on(const rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    return rfmv_sess_on(m) && q->cs_step >= 0 && q->cs_shown && !q->smenu;
}
int rfmv_seq_cs_knobs(const rfm_move_t *m)              /* its knobs (bits), -1 not shown: 1 and 2, none on an empty slot */
{
    return !rfmv_seq_cs_on(m) ? -1 : rfmv_seq_clip_has(m, cs_clip(m)) ? 0x03 : 0;
}
static int cs_launch_idx(int c)                         /* a clip's Launch as the list's place: 0 Global, 1.. Instant .. */
{
    int i;
    if (!(c & 7))
        return 0;
    for (i = 0; i < RFM_MOVE_LQ_N; i++)
        if (LQ_ORDER[i] == (c & 7) - 1)
            return i + 1;
    return 0;
}
static void cs_value(const rfm_move_t *m, int k, char *o, int cap)
{
    const int c = m->seq.cset[cs_clip(m)];
    if (k == 0)
        snprintf(o, (size_t)cap, "%s", (c & 7) ? rfmv_lq_name((c & 7) - 1) : "Global");
    else
        snprintf(o, (size_t)cap, "%s", (c & RFM_MOVE_CS_AFTER_NEXT) ? "Next" : "Loop");
}
void rfmv_seq_cs_words(const rfm_move_t *m, int k, char *o, int cap)
{
    char a[16], b[16];
    if (!rfmv_seq_clip_has(m, cs_clip(m))) {
        snprintf(o, (size_t)cap, "Track %d clip %d, no clip", cs_track(m) + 1, cs_clip(m) % RFM_MOVE_SEQ_SCENES + 1);
        return;
    }
    if (k == 0 || k == 1) {
        cs_value(m, k, a, sizeof a);
        snprintf(o, (size_t)cap, "%s: %s", k ? "After" : "Launch", a);
        return;
    }
    cs_value(m, 0, a, sizeof a), cs_value(m, 1, b, sizeof b);
    snprintf(o, (size_t)cap, "Track %d clip %d, Launch: %s, After: %s", cs_track(m) + 1, cs_clip(m) % RFM_MOVE_SEQ_SCENES + 1,
             a, b);
}
static void cs_show(rfm_move_t *m)
{
    rfm_move_seq_t *q = Q(m);
    char w[64];
    if (q->cs_shown || q->cs_step < 0)
        return;
    q->cs_shown = 1;
    if (m->pop == POP_TRKINFO)
        m->pop = POP_NONE;
    rfmv_seq_cs_words(m, -1, w, sizeof w);
    rfmv_say_text(m, w);
}
int rfmv_seq_cs_touch(rfm_move_t *m, int k)
{
    rfm_move_seq_t *q = Q(m);
    char w[48];
    if (!rfmv_sess_on(m) || q->cs_step < 0 || q->smenu)
        return 0;
    cs_show(m);
    if (k == 8) {                                       /* (the volume knob: its firmware's level, shown) */
        rfmv_seq_volume(m, 0);
        return 1;
    }
    q->cs_k = (int8_t)(k < 2 ? k : -1);
    if (k < 2 && rfmv_seq_clip_has(m, cs_clip(m))) {
        rfmv_seq_cs_words(m, k, w, sizeof w);
        rfmv_say_text(m, w);
    }
    return 1;                                           /* (knobs 3..8: dead, no track popup, no select) */
}
static void cs_turn(rfm_move_t *m, int k, int d)
{
    rfm_move_seq_t *q = Q(m);
    const int clip = cs_clip(m), c = q->cset[clip];
    char w[48];
    cs_show(m);
    if (k > 1 || !rfmv_seq_clip_has(m, clip) || !(d = rfmv_list_ticks(m, k, d)))
        return;                                         /* (knobs 3..8, an empty slot: nothing; a list: whole items) */
    q->cs_k = (int8_t)k;
    rfmv_seq_group(m);
    if (k == 0) {
        const int i = clampi(cs_launch_idx(c) + d, 0, RFM_MOVE_LQ_N);
        rfmv_seq_cset_set(m, clip, (c & ~7) | (i ? LQ_ORDER[i - 1] + 1 : 0));
    } else
        rfmv_seq_cset_set(m, clip, d > 0 ? c | RFM_MOVE_CS_AFTER_NEXT : c & ~RFM_MOVE_CS_AFTER_NEXT);
    rfmv_seq_cs_words(m, k, w, sizeof w);
    rfmv_say_text(m, w);
}
/* the settings on the screen, as the arp menu's: the touched knob's name a header (2x), a tick at the place of each
 * knob in use (the active one long); a row a knob: its name, its value at 2x */
static void cs_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    const int has = rfmv_seq_clip_has(m, cs_clip(m)), k = touched_knob(m) >= 0 && touched_knob(m) < 2 ? touched_knob(m) : q->cs_k;
    char b[24];
    int i;
    snprintf(b, sizeof b, "Track %d  Clip %d", cs_track(m) + 1, cs_clip(m) % RFM_MOVE_SEQ_SCENES + 1);
    v2_T(cv, 0, 0, b, 1, 1);
    if (!has) {
        v2_TC(cv, 64, 28, "No clip", 2, 1);
        return;
    }
    for (i = 0; i < 2; i++) {                           /* (the knobs' places: 1 Launch, 2 After) */
        const int y = 18 + i * 24;
        v2_R(cv, i == k ? 3 + i * 16 : 6 + i * 16, 11, i == k ? 10 : 4, i == k ? 2 : 1, 1);
        v2_T(cv, 0, y + 4, i ? "After" : "Launch", 1, 1);
        if (i == k)
            v2_R(cv, 0, y + 13, v2_tw(i ? "After" : "Launch", 1), 1, 1);
        cs_value(m, i, b, sizeof b);
        v2_T(cv, 128 - v2_tw(b, 2), y, b, 2, 1);
    }
}
/* (advance) the first step held long enough: its popup */
void rfmv_seq_ui_poll(rfm_move_t *m)
{
    rfm_move_seq_t *q = Q(m);
    if (q->step_first >= 0 && !q->step_popped && m->s.mode == RFM_MOVE_MODE_SEQ && !q->view && !q->projects &&
        rfmv_now_ms(m) - q->step_t[q->step_first] >= RFM_MOVE_SEQ_HOLD_MS) {
        q->step_popped = 1;
        rfmv_pop(m, POP_STEP, q->step_first);
    }
    if (q->tstep_held) {                                /* (0.11.0) a held Track's step held long enough: routed or not,
                                                         * now (its release: nothing) */
        const int fw = rfmv_seq_trk_held(m) - 1;
        int s;
        for (s = 0; s < RFM_MOVE_NSTEPS; s++)
            if ((q->tstep_held >> s & 1) && !(q->tstep_fired >> s & 1) &&
                rfmv_now_ms(m) - q->step_t[s] >= RFM_MOVE_SEQ_HOLD_MS) {
                q->tstep_fired |= 1u << s;
                if (fw >= 0)
                    tk_act(m, fw, s, 1);
            }
    }
    if (q->tk_pend >= 0 && rfmv_now_ms(m) - q->tk_pend_clk >= RFM_MOVE_DOUBLE_MS)
        rfmv_seq_tk_flush(m);                           /* (no second press: the click) */
    if (q->cs_step >= 0 && !q->cs_shown && rfmv_sess_on(m) && rfmv_now_ms(m) - q->cs_clk >= RFM_MOVE_SEQ_HOLD_MS)
        cs_show(m);                                     /* (0.12.0) a select step held: its clip's settings, while down */
}
/* ================================================================ held steps: nudge, transpose, velocity === */
static int held_view(const rfm_move_t *m) { return !m->seq.view && !m->seq.projects && m->seq.step_held; }

static void held_nudge(rfm_move_t *m, int d)
{
    rfm_move_seq_t *q = Q(m);
    const int clip = edit_clip(m);
    int s, lo, hi, i;
    for (s = 0; s < RFM_MOVE_NSTEPS; s++)
        if (q->step_held >> s & 1) {
            const int abs = q->page * RFM_MOVE_SEQ_PAGE + s;
            hi = rfmv_seq_range(m, clip, abs, &lo);
            for (i = lo; i < hi; i++) {
                const rfm_move_note_t n = q->notes[i];
                rfmv_seq_note_put(m, clip, abs, n.note, n.vel, n.len, n.micro + d);   /* (clamped to -12..+11) */
            }
            q->step_edit |= (uint16_t)(1u << s);
        }
}
static void held_transpose(rfm_move_t *m, int d)
{
    rfm_move_seq_t *q = Q(m);
    const int clip = edit_clip(m);
    int s, lo, hi, i, k, ok;
    for (s = 0; s < RFM_MOVE_NSTEPS; s++)
        if (q->step_held >> s & 1) {
            const int abs = q->page * RFM_MOVE_SEQ_PAGE + s;
            rfm_move_note_t n[RFM_MOVE_SEQ_POLY];
            hi = rfmv_seq_range(m, clip, abs, &lo);
            for (i = lo, k = 0, ok = 1; i < hi && k < RFM_MOVE_SEQ_POLY; i++, k++) {
                n[k] = q->notes[i];
                ok &= n[k].note + d >= 0 && n[k].note + d <= 127;
            }
            if (!ok || !k)
                continue;                               /* (a note would leave 0..127: the step stays) */
            step_notes_clear(m, clip, abs);
            for (i = 0; i < k; i++)
                rfmv_seq_note_put(m, clip, abs, n[i].note + d, n[i].vel, n[i].len, n[i].micro);
            q->step_edit |= (uint16_t)(1u << s);
        }
}
int rfmv_seq_volume(rfm_move_t *m, int d)
{
    rfm_move_seq_t *q = Q(m);
    const int clip = edit_clip(m);
    int s, lo, hi, i;
    if (rfmv_sess_on(m) && q->cs_step >= 0 && !q->smenu && m->fw.host) {
        /* (0.12.0, the owner's) a select step held + volume: the level of the firmware its track plays: the one played
         * now if it reaches it, else the lowest slot it reaches; none: "Not routed" */
        const int tr = cs_track(m), act = m->fw.active % RFM_MOVE_FW_SLOTS;
        int f = (q->fx.ffilt[act] >> tr & 1) ? act : -1, k;
        cs_show(m);
        for (k = 0; k < RFM_MOVE_FW_SLOTS && f < 0; k++)
            if (k < rfmv_fw_nslots(m) && (q->fx.ffilt[k] >> tr & 1) && rfmv_fw_level_turn(m, k, 0))
                f = k;
        if (f < 0 || !rfmv_fw_level_turn(m, f, d))
            rfmv_fw_level_turn(m, -1, 0), rfmv_seq_msg(m, SEQ_MSG_NOTROUTED);
        return 1;
    }
    if (!held_view(m))
        return 0;
    rfmv_pop(m, POP_TRIG, 7);                           /* (a VELOCITY popup) */
    for (s = 0; s < RFM_MOVE_NSTEPS; s++)
        if (q->step_held >> s & 1) {
            const int abs = q->page * RFM_MOVE_SEQ_PAGE + s;
            hi = rfmv_seq_range(m, clip, abs, &lo);
            for (i = lo; i < hi; i++) {
                const rfm_move_note_t n = q->notes[i];
                rfmv_seq_note_put(m, clip, abs, n.note, clampi(n.vel + d, 1, 127), n.len, n.micro);
            }
            q->step_edit |= (uint16_t)(1u << s);
        }
    return 1;
}

/* ================================================================ buttons, knobs, jog === */
static int mod_bit(int cc)
{
    switch (cc) {
    case MR_CC_SHIFT: return SEQ_M_SHIFT;
    case MR_CC_COPY: return SEQ_M_COPY;
    case MR_CC_DELETE: return SEQ_M_DELETE;
    case MR_CC_MUTE: return SEQ_M_MUTE;
    case MR_CC_LOOP: return SEQ_M_LOOP;
    default: return 0;
    }
}

int rfmv_seq_button(rfm_move_t *m, int cc, int down, uint32_t now)
{
    rfm_move_seq_t *q = Q(m);
    const int mb = mod_bit(cc);
    if (mb) {
        if (down) {
            if (cc == MR_CC_LOOP && (q->mods & SEQ_M_SHIFT)) {   /* Shift + Loop held: FILL (the owner's; Double
                                                         * Loop is Shift + step 15) */
                q->mods_used |= SEQ_M_SHIFT;
                q->fill = 1, q->loop_end = 1;           /* (its release: FILL off, nothing else) */
                return 1;
            }
            if (mb == SEQ_M_SHIFT) {                    /* a double tap latches Shift (a tap, then a press within
                                                         * 300 ms); a press unlatches it */
                if (q->shift_lock)
                    q->shift_lock = 0, q->shift_ms = 0, q->mods_used |= SEQ_M_SHIFT;
                else if (q->shift_ms && now - q->shift_ms < 300)
                    q->shift_lock = 1, q->shift_ms = 0, layer(q, LY_SHIFT);
            }
            if (mb == SEQ_M_LOOP && q->loop_latch) {   /* latched: this press ends it, at once (as Back) */
                layer_close(m, LY_LOOP);
                q->loop_end = 1;                        /* (its release: nothing) */
                return 1;
            }
            q->mods |= (uint8_t)mb, q->mods_used &= (uint8_t)~mb;
            if (mb == SEQ_M_LOOP && !q->view && !q->projects)
                rfmv_pop(m, POP_LOOP, 0);               /* Loop held: the clip's length */
            if (mb == SEQ_M_COPY)
                q->copy_src = 0, q->copy_pasted = 0;
            if (mb == SEQ_M_DELETE)
                m->del_src = 0;
        } else if (mb == SEQ_M_SHIFT && q->shift_lock) {
            ;                                           /* (latched: it stays down) */
        } else if (mb == SEQ_M_LOOP && q->loop_end) {
            q->loop_end = 0, q->fill = 0;               /* (the press that ended the latch, or FILL's) */
        } else if (mb == SEQ_M_LOOP && !(q->mods_used & SEQ_M_LOOP) && !q->loop_latch && !q->view && !q->projects) {
            q->loop_latch = 1, layer(q, LY_LOOP);       /* a tap on Loop: latched (the jog sets the length) until
                                                         * Loop or Back again */
            rfmv_pop(m, POP_LOOP, 0);
        } else if (q->mods & mb) {
            const int used = q->mods_used & mb;
            if (mb == SEQ_M_SHIFT)
                q->shift_ms = used ? 0 : (now ? now : 1);   /* (a tap: the first of a double tap) */
            if (mb == SEQ_M_LOOP && m->pop == POP_LOOP)
                m->pop = POP_NONE;                      /* (0.4.3: Loop let go: the page edit ends at once) */
            const int alone = !used && !q->projects && !(q->mods & SEQ_M_SHIFT) && !q->step_held &&
                              !rfmv_seq_trk_held(m);
            q->mods &= (uint8_t)~mb;
            if (mb == SEQ_M_MUTE && alone)
                rfmv_seq_mute(m, q->sel_track, !q->tr[q->sel_track].mute);   /* Mute alone: the selected track */
            if (mb == SEQ_M_COPY) {
                if (used)                               /* a source and no paste yet: the next press pastes (Move) */
                    q->paste = (uint8_t)(q->copy_src && !q->copy_pasted && q->clip_kind != 2);   /* (0.12.0: not a
                                                         * clip: nothing after Copy is let go) */
                else if (alone)
                    copy_alone(m);
                q->copy_src = 0, q->copy_pasted = 0;
            }
            if (mb == SEQ_M_DELETE)
                m->del_src = 0;                         /* (0.6.3: the last action forgotten; the clipboard stays) */
            if (mb == SEQ_M_DELETE && alone)
                delete_alone(m);
            else if (mb == SEQ_M_DELETE && !used && !q->projects && rfmv_seq_trk_held(m)) {
                char w[24];                             /* (0.6.3) Track 4 held + Delete: the selected track cleared */
                rfmv_seq_group(m);
                rfmv_seq_track_clear(m, q->sel_track);
                rfmv_menu(m, MENU_SHIFT, 0);            /* (Track 4 used: its release is no tap) */
                snprintf(w, sizeof w, "Track %d cleared", q->sel_track + 1);
                rfmv_say_text(m, w);
            }
        }
        return 1;
    }
    if (!down)
        return 1;
    if (rfmv_seq_trk_held(m) && (cc == MR_CC_UP || cc == MR_CC_DOWN)) {   /* a Track button held: the track's rate
                                                         * (Up: faster) */
        rfm_move_track_t *t = &q->tr[q->sel_track];
        const int r = clampi(t->rate + (cc == MR_CC_UP ? -1 : 1), 0, RFM_MOVE_SEQ_RATES - 1);
        rfmv_menu(m, MENU_RATE, r);
        if (r != t->rate) {
            t->rate = (uint8_t)r;                       /* (from the next step) */
            rfmv_seq_dirty(m);
        }
        return 1;
    }
    if (held_view(m)) {                                 /* a step held: the arrows edit its notes (Move) */
        switch (cc) {
        case MR_CC_LEFT:
        case MR_CC_RIGHT:
            held_nudge(m, (cc == MR_CC_RIGHT ? 1 : -1) * ((q->mods & SEQ_M_SHIFT) ? 1 : NUDGE));
            rfmv_pop(m, POP_TRIG, 0);                   /* (the TRIG page's MICRO popup) */
            if (q->mods & SEQ_M_SHIFT)
                q->mods_used |= SEQ_M_SHIFT;
            return 1;
        case MR_CC_UP:
        case MR_CC_DOWN:
            held_transpose(m, (cc == MR_CC_UP ? 1 : -1) * ((q->mods & SEQ_M_SHIFT) ? 12 : 1));
            rfmv_say_text(m, (q->mods & SEQ_M_SHIFT) ? (cc == MR_CC_UP ? "Octave up" : "Octave down")
                                                     : (cc == MR_CC_UP ? "Transpose up" : "Transpose down"));
            if (q->mods & SEQ_M_SHIFT)
                q->mods_used |= SEQ_M_SHIFT;
            return 1;
        default: break;
        }
    }
    switch (cc) {
    case MR_CC_MENU:
        if (q->projects)
            q->projects = 0;
        else {                                          /* (0.11.0, the owner's) an open Shift menu stays (it has the
                                                         * knobs, the jog and the screen; Back closes it): the view
                                                         * flips under it, named a second; (0.12.0) Shift + Menu the
                                                         * same (it had no job of its own); out of session view: the
                                                         * slot's own layout (it may be the FM-1 layout) */
            if (q->mods & SEQ_M_SHIFT)
                q->mods_used |= SEQ_M_SHIFT;
            rfmv_sv_toggle(m);
        }
        return 1;
    case MR_CC_BACK:                                    /* the universal escape: the latest layer still on closes
                                                         * (a Shift menu, the project grid, Loop's latch, Shift's
                                                         * lock, the clipboard's marks), one a press; none: nothing */
        while (q->nlayer) {
            const int id = q->layer[--q->nlayer];
            if (layer_on(q, id)) {
                layer_close(m, id);
                return 1;
            }
        }
        {
            int id;
            for (id = LY_MENU; id <= LY_PAGE; id++)        /* (one opened some other way) */
                if (layer_on(q, id)) {
                    layer_close(m, id);
                    return 1;
                }
        }
        return 1;                                       /* (nothing to close: nothing. Only a Track tap changes the
                                                         * mode, the owner's) */
    case MR_CC_PLAY:
        q->ncap = 0;                                    /* (Move: the transport clears the capture) */
        if (q->mods & SEQ_M_SHIFT) {                    /* Shift + Play: every clip from its start (Move); (0.10.0) in
                                                         * session view too (there it stopped every clip: step 16 now) */
            q->mods_used |= SEQ_M_SHIFT;
            rfmv_seq_restart(m);
        } else if (running(q))
            rfmv_seq_stop(m);
        else if (q->rec && q->rec_mode == RFM_MOVE_RM_RECORD)
            rfmv_seq_count_in(m);                       /* (Rec armed while stopped: Play gives the bar's count-in) */
        else
            rfmv_seq_start(m);
        return 1;
    case MR_CC_REC:
        if (q->mods & SEQ_M_SHIFT) {                    /* Shift + Record: the record settings */
            q->mods_used |= SEQ_M_SHIFT;
            q->smenu = (uint8_t)(q->smenu == RFM_MOVE_SM_REC ? 0 : RFM_MOVE_SM_REC);
            if (q->smenu)
                layer(q, LY_MENU);
            return 1;
        }
        if (q->rec_mode == RFM_MOVE_RM_STEP) {          /* Step: the cursor on (at the page's first step) or off */
            q->steprec = (uint8_t)!q->steprec;
            if (q->steprec) {
                const int clip = edit_clip(m), lo = rfmv_seq_loop_lo(m, clip), hi = rfmv_seq_loop_hi(m, clip);
                const int a = q->page * RFM_MOVE_SEQ_PAGE;
                q->step_cur = (int16_t)(a >= lo && a < hi ? a : lo);
                q->steps_chord = 0;
            }
            return 1;
        }
        if (q->rec_mode == RFM_MOVE_RM_CAPTURE && !q->srec && !q->rec && running(q) && q->playing) {
            int lo;
            if (rfmv_seq_clip_range(m, edit_clip(m), &lo) == lo) {
                rfmv_seq_srec_now(m);                   /* (playing, the clip empty: at once, from this bar) */
                return 1;
            }
        }
        if (q->rec_mode == RFM_MOVE_RM_CAPTURE && (q->srec || (!q->rec && !running(q)))) {
            rfmv_seq_sample(m);                         /* Capture, stopped: armed; recording: done at the bar */
            return 1;
        }
        if (!q->rec) {
            if (!++q->take)
                q->take = 1;
            q->rec = 1;
            rfmv_seq_group(m);
            q->rec_group = q->group;
            /* (stopped: armed; Play starts it with a bar's count-in. Only Play starts the transport, the owner's) */
        } else {
            int t;
            q->rec = 0;
            if (q->start_pending == 3)
                q->start_pending = 0;                   /* (off during the count-in: no start) */
            for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
                if (q->tr[t].grow == 1)
                    q->tr[t].grow_stop = 1;
        }
        return 1;
    case MR_CC_CAPTURE:
        if (q->mods & SEQ_M_SHIFT) {                    /* Shift + Capture: the capture forgotten (Move) */
            q->mods_used |= SEQ_M_SHIFT;
            q->ncap = 0;
            rfmv_seq_msg(m, SEQ_MSG_CAPCLEAR);
        } else if (rfmv_seq_capture(m) >= 0) {
            rfmv_seq_msg(m, SEQ_MSG_CAPTURED);          /* (stopped: the new clip plays with Play) */
        } else
            rfmv_seq_msg(m, SEQ_MSG_NOCLIP);
        return 1;
    case MR_CC_SAMPLE:
        if (q->mods & SEQ_M_SHIFT)
            q->mods_used |= SEQ_M_SHIFT;                /* (Shift + Sample: nothing; the Move has none) */
        else
            rfmv_seq_audio_rec(m);                      /* the audio loop: armed, recording, done at the next bar */
        return 1;
    case MR_CC_UNDO:
        if (q->mods & SEQ_M_SHIFT) {
            q->mods_used |= SEQ_M_SHIFT;
            if (rfmv_seq_redo(m))
                rfmv_seq_msg(m, SEQ_MSG_REDO);
        } else if (rfmv_seq_undo(m))
            rfmv_seq_msg(m, SEQ_MSG_UNDO);
        return 1;
    case MR_CC_LEFT:
    case MR_CC_RIGHT: {
        const int d = cc == MR_CC_RIGHT ? 1 : -1;
        if (q->view) {                                  /* the tracks, one a press (7 of 16 in view); (0.12.0, the
                                                         * owner's) Shift: a page, 7 tracks */
            if (q->mods & SEQ_M_SHIFT)
                q->mods_used |= SEQ_M_SHIFT;
            q->track_page = (uint8_t)clampi(q->track_page + d * ((q->mods & SEQ_M_SHIFT) ? RFM_MOVE_SV_COLS : 1), 0,
                                            RFM_MOVE_SEQ_TRACKS - RFM_MOVE_SV_COLS);
        } else {
            const int clip = edit_clip(m), was = q->page, end = clip_end_page(m, clip);
            if (q->mods & SEQ_M_LOOP)                   /* (page edit: the same) */
                q->mods_used |= SEQ_M_LOOP;
            q->page = (uint8_t)clampi(q->page + d, 0, RFM_MOVE_SEQ_PAGES - 1);   /* the bars there are; Right past
                                                         * the last: a ghost bar (+1, +2 ..: real once a step goes in
                                                         * there, or, in page edit, once its step key confirms it) */
            if (q->page != was && (q->page > end || was > end))
                rfmv_pop(m, POP_BAR, q->page);          /* ("Bar n", a second) */
        }
        return 1;
    }
    case MR_CC_UP:
    case MR_CC_DOWN: {
        const int d = cc == MR_CC_UP ? 1 : -1;
        if (q->view) {                                  /* the clip slots (4 of 16 in view; Up: the ones above);
                                                         * (0.12.0) Shift: a page, 4 rows */
            if (q->mods & SEQ_M_SHIFT)
                q->mods_used |= SEQ_M_SHIFT;
            q->scene_off = (uint8_t)clampi(q->scene_off - d * ((q->mods & SEQ_M_SHIFT) ? 4 : 1), 0, RFM_MOVE_SEQ_SCENES - 4);
        } else {
            const int o = clampi(m->s.octave + d, RFM_MOVE_OCT_MIN, RFM_MOVE_OCT_MAX);
            if (o != m->s.octave) {
                m->s.octave = (int8_t)o;
                rfmv_changed(m);
            }
        }
        return 1;
    }
    default: return 0;
    }
}

/* the TRIG page: knob k's value of a step (shown 0 none: micro 0, ratchet 1, chance 100, cond / func none) */
int rfmv_seq_trig_val(const rfm_move_t *m, int clip, int abs, int k)
{
    int v, lo, hi;
    switch (k) {
    case 0:                                             /* micro: the step's first note's */
        hi = rfmv_seq_range(m, clip, abs, &lo);
        return hi > lo ? m->seq.notes[lo].micro : 0;
    case 1: return (v = rfmv_seq_trig(m, clip, abs, RFM_MOVE_TRIG_RATCHET)) < 0 ? 1 : v;
    case 2: return (v = rfmv_seq_trig(m, clip, abs, RFM_MOVE_TRIG_CHANCE)) < 0 ? 100 : v;
    case 3: return (v = rfmv_seq_trig(m, clip, abs, RFM_MOVE_TRIG_COND)) < 0 ? 0 : v;
    case 4: return (v = rfmv_seq_trig(m, clip, abs, RFM_MOVE_TRIG_FUNC)) < 0 ? 0 : v;
    case 5: {
        const int f = rfmv_seq_trig_val(m, clip, abs, 4);
        if (f == RFM_MOVE_FN_LEAP)
            return (v = rfmv_seq_trig(m, clip, abs, RFM_MOVE_TRIG_LEAP)) < 0 ? 2 : v - 15;
        if (f == RFM_MOVE_FN_JUMP)
            return (v = rfmv_seq_trig(m, clip, abs, RFM_MOVE_TRIG_JUMP)) < 0 ? rfmv_seq_loop_lo(m, clip) : v;
        if (f == RFM_MOVE_FN_REPEAT)
            return (v = rfmv_seq_trig(m, clip, abs, RFM_MOVE_TRIG_REPEAT)) < 0 ? 1 : v;
        return 0;
    }
    case 6:                                             /* length: the step's first note's, ticks (24 a step) */
        hi = rfmv_seq_range(m, clip, abs, &lo);
        return hi > lo ? m->seq.notes[lo].len : RFM_MOVE_SEQ_TPS;
    case 7:                                             /* (held steps' volume knob) velocity */
        hi = rfmv_seq_range(m, clip, abs, &lo);
        return hi > lo ? m->seq.notes[lo].vel : 0;
    default: return 0;
    }
}
/* the TRIG page's lengths (Elektron's): 1/8 .. 64 steps, then INF (the longest a note can be) */
static const uint16_t LENS[] = { 3, 6, 12, 18, 24, 36, 48, 72, 96, 144, 192, 288, 384, 576, 768, 1536,
                                 RFM_MOVE_SEQ_MAXLEN * RFM_MOVE_SEQ_TPS };
#define NLENS ((int)(sizeof LENS / sizeof LENS[0]))
int rfmv_seq_len_index(int len)                         /* the nearest entry */
{
    int i, b = 0;
    for (i = 1; i < NLENS; i++)
        if ((len > LENS[i] ? len - LENS[i] : LENS[i] - len) < (len > LENS[b] ? len - LENS[b] : LENS[b] - len))
            b = i;
    return b;
}
int rfmv_seq_len_of(int i) { return LENS[clampi(i, 0, NLENS - 1)]; }
typedef char lens_n_check[NLENS == RFM_MOVE_TRIG_NLENS ? 1 : -1];

/* ---- the TRIG page's defaults (the owner's): with no step held its knobs set what new steps get, per track (saved
 * with the project); steps already there keep theirs */
void rfmv_seq_tdef_default(uint8_t d[8])
{
    d[0] = 12, d[1] = 1, d[2] = 100, d[3] = 0, d[4] = 0, d[5] = 15 + 2, d[6] = 0, d[7] = 4;   /* (length: 24, a step) */
}
int rfmv_seq_tdef_val(const rfm_move_t *m, int k)
{
    const uint8_t *d = m->seq.tr[m->seq.sel_track].tdef;
    switch (k) {
    case 0: return d[0] - 12;
    case 1: return d[1];
    case 2: return d[2];
    case 3: return d[3];
    case 4: return d[4];
    case 5: return d[4] == RFM_MOVE_FN_LEAP ? d[5] - 15 : d[4] == RFM_MOVE_FN_JUMP ? d[6]
                 : d[4] == RFM_MOVE_FN_REPEAT ? clampi(d[6], 1, 16) : 0;   /* (JUMP's step, REPEAT's n: one byte) */
    case 6: return LENS[d[7] < NLENS ? d[7] : 4];
    default: return 0;
    }
}
static void tdef_turn(rfm_move_t *m, int k, int d)
{
    uint8_t *t = m->seq.tr[m->seq.sel_track].tdef;
    switch (k) {
    case 0: t[0] = (uint8_t)(clampi(t[0] - 12 + d, -RFM_MOVE_SEQ_TPS / 2, RFM_MOVE_SEQ_TPS / 2 - 1) + 12); break;
    case 1: t[1] = (uint8_t)clampi(t[1] + d, 1, 8); break;
    case 2: t[2] = (uint8_t)clampi(t[2] + d, 1, 100); break;
    case 3: t[3] = (uint8_t)clampi(t[3] + d, 0, RFM_MOVE_COND_N - 1); break;
    case 4: t[4] = (uint8_t)clampi(t[4] + d, 0, RFM_MOVE_FN_N - 1); break;
    case 5:
        if (t[4] == RFM_MOVE_FN_LEAP) {
            int n = clampi(t[5] - 15 + d, -15, 15);
            if (!n)
                n = d > 0 ? 1 : -1;                     /* (no LEAP 0) */
            t[5] = (uint8_t)(n + 15);
        } else if (t[4] == RFM_MOVE_FN_JUMP)
            t[6] = (uint8_t)clampi(t[6] + d, 0, RFM_MOVE_SEQ_MAXLEN - 1);
        else if (t[4] == RFM_MOVE_FN_REPEAT)
            t[6] = (uint8_t)clampi(clampi(t[6], 1, 16) + d, 1, 16);
        break;
    case 6: t[7] = (uint8_t)clampi(t[7] + d, 0, NLENS - 1); break;
    default: break;
    }
    rfmv_seq_dirty(m);
}
/* a step that was empty got its first note: the track's default trigs as its locks (entry: its notes' micro and
 * length too; a recording keeps the played ones) */
void rfmv_seq_tdef_apply(rfm_move_t *m, int clip, int abs, int entry)
{
    const uint8_t *t = m->seq.tr[clip / RFM_MOVE_SEQ_SCENES].tdef;
    if (t[1] > 1)
        rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_RATCHET, t[1]);
    if (t[2] < 100)
        rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_CHANCE, t[2]);
    if (t[3])
        rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_COND, t[3]);
    if (t[4]) {
        rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_FUNC, t[4]);
        if (t[4] == RFM_MOVE_FN_LEAP)
            rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_LEAP, t[5]);
        else if (t[4] == RFM_MOVE_FN_JUMP)
            rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_JUMP, t[6]);
        else if (t[4] == RFM_MOVE_FN_REPEAT)
            rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_REPEAT, clampi(t[6], 1, 16));
    }
    if (entry && (t[0] != 12 || t[7] != 4)) {
        int lo, hi = rfmv_seq_range(m, clip, abs, &lo), i, n = hi - lo;
        rfm_move_note_t nn[RFM_MOVE_SEQ_POLY];
        for (i = 0; i < n && i < RFM_MOVE_SEQ_POLY; i++)
            nn[i] = m->seq.notes[lo + i];
        for (i = 0; i < n && i < RFM_MOVE_SEQ_POLY; i++)
            rfmv_seq_note_put(m, clip, abs, nn[i].note, nn[i].vel, LENS[t[7] < NLENS ? t[7] : 4], t[0] - 12);
    }
}

int rfmv_seq_len_inf(int len) { return len >= LENS[NLENS - 1]; }

static void trig_turn(rfm_move_t *m, int clip, int abs, int k, int d)
{
    const int v = rfmv_seq_trig_val(m, clip, abs, k);
    int lo, hi, i;
    switch (k) {
    case 0:                                             /* micro: every note of the step, a tick a detent */
        hi = rfmv_seq_range(m, clip, abs, &lo);
        for (i = lo; i < hi; i++) {
            const rfm_move_note_t n = m->seq.notes[i];
            rfmv_seq_note_put(m, clip, abs, n.note, n.vel, n.len, clampi(n.micro + d, -RFM_MOVE_SEQ_TPS / 2, RFM_MOVE_SEQ_TPS / 2 - 1));
        }
        break;
    case 1: {
        const int r = clampi(v + d, 1, 8);
        rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_RATCHET, r == 1 ? -1 : r);
        break;
    }
    case 2: {
        const int c = clampi(v + d, 1, 100);
        rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_CHANCE, c == 100 ? -1 : c);
        break;
    }
    case 3: {
        const int c = clampi(v + d, 0, RFM_MOVE_COND_N - 1);
        rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_COND, c ? c : -1);
        break;
    }
    case 4: {
        const int f = clampi(v + d, 0, RFM_MOVE_FN_N - 1);
        rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_FUNC, f ? f : -1);
        break;
    }
    case 6: {                                           /* length: every note of the step, through the table */
        const int L = rfmv_seq_len_of(rfmv_seq_len_index(v) + d);
        hi = rfmv_seq_range(m, clip, abs, &lo);
        for (i = lo; i < hi; i++) {
            const rfm_move_note_t n = m->seq.notes[i];
            rfmv_seq_note_put(m, clip, abs, n.note, n.vel, L, n.micro);
        }
        break;
    }
    case 5: {
        const int f = rfmv_seq_trig_val(m, clip, abs, 4);
        if (f == RFM_MOVE_FN_LEAP) {
            int n = clampi(v + d, -15, 15);
            if (!n)
                n = d > 0 ? 1 : -1;                     /* (no LEAP 0) */
            rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_LEAP, n + 15);
        } else if (f == RFM_MOVE_FN_JUMP)
            rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_JUMP, clampi(v + d, 0, RFM_MOVE_SEQ_MAXLEN - 1));
        else if (f == RFM_MOVE_FN_REPEAT) {
            const int n = clampi(v + d, 1, 16);
            rfmv_seq_lock_set(m, clip, abs, RFM_MOVE_TRIG_REPEAT, n);
        }
        break;
    }
    default: break;
    }
}

/* a Shift menu's values: knob k (jog: k -1, the menu's first value) */
/* a Shift menu's knob that picks from a list (a scale, a root, a layout, a rate, a pattern, a mode ..): turned a
 * slower one item a few ticks, the same for every menu (rfmv_list_ticks); numbers (tempo, groove, ..) as before */
static int smenu_list(const rfm_move_seq_t *q, int k)
{
    switch (q->smenu) {
    case RFM_MOVE_SM_WORKFLOW: return k == 0 || k == 1;
    case RFM_MOVE_SM_GROOVE: return k == 1;
    case RFM_MOVE_SM_LAYOUT: return k == 0;
    case RFM_MOVE_SM_KEYS: return k <= 3;
    case RFM_MOVE_SM_ARP:
        if (q->arp_page)
            return (k == 0 && q->arp_page == 1) || k == 1 || k == 2;
        return k == 0 || k == 1 || k == 2 || k == 4 || k == 7;
    case RFM_MOVE_SM_REC: return k <= 1;
    case RFM_MOVE_SM_GLOBAL: return k <= 1 || k == 3;
    default: return 0;
    }
}

/* the knobs a Shift menu uses (bit k: knob k + 1; their ring LEDs lit in the menu colour, the others off) */
int rfmv_seq_menu_knobs(const rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    switch (q->smenu) {
    case RFM_MOVE_SM_WORKFLOW: return 0x07;
    case RFM_MOVE_SM_TEMPO: return q->following ? 0 : 0x03;
    case RFM_MOVE_SM_GROOVE: return 0x03;
    case RFM_MOVE_SM_LAYOUT: return 0x01;
    case RFM_MOVE_SM_KEYS: return 0x0F;
    case RFM_MOVE_SM_ARP: return q->arp_page ? 0x07 : 0xFF;   /* (0.11.0: a lane: its value, length and RESET; the
                                                         * step keys pick its step) */
    case RFM_MOVE_SM_REC: return 0x03;
    case RFM_MOVE_SM_VEL: return 0x01;
    case RFM_MOVE_SM_GLOBAL: return 0;                  /* (0.6.2: the jog's list; the knobs do nothing) */
    default: return 0;
    }
}

/* (0.11.0, the owner's: a quick way to turn the arp on and off) the arp menu's jog click: off, or on again as it was
 * (On at first); its header says which at once */
void rfmv_seq_arp_toggle(rfm_move_t *m)
{
    rfm_move_seq_t *q = Q(m);
    rfm_move_arp_t *a = &q->tr[q->sel_track].arp;
    if (a->mode == RFM_MOVE_AM_OFF) {
        const int was = q->arp_was[q->sel_track];
        a->mode = (uint8_t)(was > RFM_MOVE_AM_OFF && was < RFM_MOVE_AM_N ? was : RFM_MOVE_AM_ON);
    } else {
        q->arp_was[q->sel_track] = a->mode;
        a->mode = RFM_MOVE_AM_OFF;
        rfmv_seq_arp_stop(m);
    }
    q->sm_k = 4, q->sm_ms = rfmv_now_ms(m);             /* (its header: the mode, as knob 5 shows it) */
    rfmv_say_text(m, a->mode == RFM_MOVE_AM_OFF ? "Arp off" : "Arp on");
    rfmv_seq_dirty(m);
}
static void smenu_turn(rfm_move_t *m, int k, int d)
{
    rfm_move_seq_t *q = Q(m);
    rfm_move_track_t *t = &q->tr[q->sel_track];
    rfm_move_arp_t *a = &t->arp;
    const rfm_move_arp_t a0 = *a;
    const int shown = q->sm_k == k && rfmv_now_ms(m) - q->sm_ms < RFM_MOVE_POPUP_MS;
    if (k >= 0 && !(rfmv_seq_menu_knobs(m) >> k & 1))
        return;                                         /* (0.11.0: a knob the menu has no job for: nothing) */
    q->sm_k = (int8_t)k, q->sm_ms = rfmv_now_ms(m);     /* (0.11.0: its name and value in the menu's header) */
    if (k >= 0 && smenu_list(q, k) && !(d = rfmv_list_ticks(m, k, d)))
        return;                                         /* (not a whole item yet) */
    rfmv_menu(m, MENU_NONE, k < 0 ? 0 : k);
    switch (q->smenu) {
    case RFM_MOVE_SM_WORKFLOW:                          /* channel, rate, quantize amount */
        if (k <= 0) {
            const int ch = clampi(t->ch + d, 0, 15);
            if (ch != t->ch)
                rfmv_seq_release(m, q->sel_track), t->ch = (uint8_t)ch;
        } else if (k == 1)
            t->rate = (uint8_t)clampi(t->rate + d, 0, RFM_MOVE_SEQ_RATES - 1);
        else if (k == 2)
            q->qstrength = (uint8_t)clampi(q->qstrength + d * 5, 0, 100);
        break;
    case RFM_MOVE_SM_TEMPO:                             /* (following the firmware's clock: locked) */
        if (!q->following && k <= 1)                    /* (Move: Shift + the wheel, or knob 2: fine) */
            rfm_move_seq_set_bpm(m, q->bpm10 + d * (k == 1 || (q->mods & SEQ_M_SHIFT) ? 1 : 10));
        break;
    case RFM_MOVE_SM_GROOVE:                            /* the jog: the amount; knob 2: GLOBAL (the project's) or
                                                         * TRACK (its own) */
        if (k <= 0) {                                   /* (Move: Shift fine; ours: 5 % a detent, Shift 1 %) */
            uint8_t *g = (t->own & 1) ? &t->groove : &q->groove;
            *g = (uint8_t)clampi(*g + d * ((q->mods & SEQ_M_SHIFT) ? 1 : 5), 0, 150);
        } else if (k == 1 && (d > 0) != !!(t->own & 1)) {
            if (d > 0)
                t->groove = q->groove;                  /* (its own starts from the global one) */
            t->own = (uint8_t)(d > 0 ? t->own | 1 : t->own & ~1);
        }
        break;
    case RFM_MOVE_SM_LAYOUT:
        if (k <= 0) {
            const int l = clampi(m->s.layout + d, 0, 3);
            if (l != m->s.layout)
                m->s.layout = (uint8_t)l, rfmv_changed(m);
        }
        break;
    case RFM_MOVE_SM_KEYS:                              /* the root (jog, knob 1), the scale (knob 2) */
        if (k <= 0) {
            m->s.root = (uint8_t)(((m->s.root + d) % 12 + 12) % 12);
            rfmv_changed(m);
        } else if (k == 1) {
            const int sc2 = clampi(m->s.scale + d, 0, RFM_MOVE_NSCALES - 1);
            if (sc2 != m->s.scale)
                m->s.scale = (uint8_t)sc2, rfmv_changed(m);
        } else if (k == 2 && (d > 0) != !!(t->own & 2)) {   /* knob 3: GLOBAL or TRACK (its own key and scale) */
            rfmv_seq_sync(m);
            if (d > 0)
                t->root = q->root, t->scale = q->scale;
            t->own = (uint8_t)(d > 0 ? t->own | 2 : t->own & ~2);
            load_track(m);
            rfmv_changed(m);
        } else if (k == 3) {                            /* knob 4: the layout (the pads play, the owner's) */
            const int l = clampi(m->s.layout + d, 0, 3);
            if (l != m->s.layout)
                m->s.layout = (uint8_t)l, rfmv_changed(m);
        }
        rfmv_seq_sync(m);
        break;
    case RFM_MOVE_SM_ARP:                               /* FIZZ's ARP: the jog the page; page 1: PATTERN RATE RANGE
                                                         * CHANCE MODE GATE OCT DIST QUANT; pages 2, 3: the MOD and
                                                         * TRANSPOSE lanes: (0.11.0, the owner's) a step key picks the
                                                         * lane's step; knob 1 its function or semitones, knob 2 the
                                                         * lane's length, knob 3 RESET */
        if (k < 0) {
            q->arp_page = (uint8_t)clampi(q->arp_page + d, 0, 2);
            q->sm_ms = 0;                               /* (the page's own header) */
            break;
        }
        if (q->arp_page) {
            const int L = q->arp_page - 1;
            if (k == 0)
                a->lane[L][q->arp_sel & 15] = (int8_t)(L ? clampi(a->lane[L][q->arp_sel & 15] + d, -48, 48)
                                                         : clampi(a->lane[L][q->arp_sel & 15] + d, 0, RFM_MOVE_AL_N - 1));
            else if (k == 1) {
                uint8_t *len = L ? &a->tlen : &a->mlen;
                *len = (uint8_t)clampi(*len + d, 1, 16);
            } else if (k == 2)
                a->reset = (uint8_t)clampi(a->reset + d, 0, RFM_MOVE_AR_N - 1);
            break;
        }
        switch (k) {
        case 0: a->pat = (uint8_t)clampi(a->pat + d, 0, RFM_MOVE_AP_N - 1); break;
        case 1: a->rate = (uint8_t)clampi(a->rate + d, 0, RFM_MOVE_ARP_RATES - 1); break;
        case 2: a->range = (uint8_t)clampi(a->range + d, 1, 8); break;
        case 3: a->chance = (uint8_t)clampi(a->chance + d * 5, 0, 100); break;
        case 4:
            a->mode = (uint8_t)clampi(a->mode + d, 0, RFM_MOVE_AM_N - 1);
            if (a->mode == RFM_MOVE_AM_OFF)
                rfmv_seq_arp_stop(m);
            else
                q->arp_was[q->sel_track] = a->mode;     /* (the jog's click brings this one back) */
            break;
        case 5: a->gate = (uint8_t)clampi(a->gate + d * 5, 2, 200); break;
        case 6: a->odist = (int8_t)clampi(a->odist + d, -24, 24); break;
        case 7: a->quant = (uint8_t)clampi(a->quant + d, 0, 16); break;
        default: break;
        }
        break;
    case RFM_MOVE_SM_GLOBAL:                            /* the jog or knob 1: the clock source; knob 2: the velocity
                                                         * option; knob 3: its level; (0.5.9) knob 4: the FM-1
                                                         * popups' width (Full, Half) */
        if (k <= 0)
            rfm_move_seq_set_clock(m, clampi(q->clock_mode + d, 0, 2));
        else if (k == 1)
            rfmv_vel_step(m, d), rfmv_changed(m);
        else if (k == 2)
            m->s.vel_level = (uint8_t)clampi(m->s.vel_level + d, 1, 127), rfmv_changed(m);
        else if (k == 3) {
            const int h = clampi(m->s.pop_half + d, 0, 1);
            if (h != m->s.pop_half)
                m->s.pop_half = (uint8_t)h, rfmv_changed(m);
        }
        break;
    case RFM_MOVE_SM_VEL:                               /* the jog: the option (1:1, five); knob 1: the level */
        if (k < 0)
            rfmv_vel_step(m, d);
        else if (k == 0)
            m->s.vel_level = (uint8_t)clampi(m->s.vel_level + d, 1, 127);
        rfmv_changed(m);
        break;
    case RFM_MOVE_SM_REC:                               /* mode, style */
        if (k <= 0)
            q->rec_mode = (uint8_t)clampi(q->rec_mode + d, 0, RFM_MOVE_RM_N - 1), q->steprec = 0;
        else if (k == 1)
            q->rec_style = (uint8_t)clampi(q->rec_style + d, 0, RFM_MOVE_RS_N - 1);
        break;
    default: break;
    }
    if (q->smenu == RFM_MOVE_SM_ARP && k >= 0 && !shown && !memcmp(&a0, a, sizeof a0) && m->speech) {
        char w[48];                                     /* (its header came up with the value at its end: said) */
        rfmv_seq_smenu_words(m, k, w, sizeof w);
        rfmv_say_text(m, w);
    }
    rfmv_seq_dirty(m);
}

void rfmv_seq_knob(rfm_move_t *m, int k, int d, uint32_t now)
{
    rfm_move_seq_t *q = Q(m);
    const int b = m->s.bank, cc = m->s.cc[b][k];
    (void)now;
    if (rfmv_seq_cs_on(m) || (rfmv_sess_on(m) && q->cs_step >= 0 && !q->smenu && !rfmv_seq_trk_held(m))) {
        cs_turn(m, k, d);                               /* (0.12.0) a select step held: its clip's settings */
        return;
    }
    if (q->view && !q->smenu)
        return;                                         /* (session: the knobs do nothing; a touch shows a track.
                                                         * 0.11.0: an open Shift menu has them there too; a held Track
                                                         * has none there) */
    if (rfmv_seq_trk_held(m)) {                         /* a Track button held: the knob's CC number (this bank) */
        const int c = clampi(cc + d, 0, 127);
        if (q->view)
            return;
        rfmv_menu(m, MENU_CC, k);
        if (c != cc) {
            m->s.cc[b][k] = (uint8_t)c;
            rfmv_changed(m);
        }
        return;
    }
    if (q->smenu) {                                     /* a Shift menu open: its values */
        smenu_turn(m, k, d);
        return;
    }
    if (q->trig && !q->view && !q->projects) {          /* the TRIG page: the held steps' (none held: every step
                                                         * with notes in the clip) */
        const int clip = edit_clip(m);
        int s;
        rfmv_pop(m, POP_TRIG, k);
        if (k > 6)
            return;
        if ((k == 3 || k == 4) && !(d = rfmv_list_ticks(m, k, d)))
            return;                                     /* (COND, FUNC: lists, one item a few ticks) */
        if (!q->step_held) {                            /* none held: the track's defaults for new steps (the
                                                         * owner's: steps already there keep theirs) */
            tdef_turn(m, k, d);
            return;
        }
        if (q->step_held) {
            for (s = 0; s < RFM_MOVE_NSTEPS; s++)
                if (q->step_held >> s & 1) {
                    trig_turn(m, clip, q->page * RFM_MOVE_SEQ_PAGE + s, k, d);
                    q->step_edit |= (uint16_t)(1u << s);
                }
        } else {
            int lo, hi = rfmv_seq_clip_range(m, clip, &lo), prev = -1, i;
            uint8_t steps[RFM_MOVE_SEQ_MAXLEN];
            int ns = 0;
            for (i = lo; i < hi; i++)
                if (q->notes[i].step != prev)
                    steps[ns++] = (uint8_t)(prev = q->notes[i].step);
            for (i = 0; i < ns; i++)
                trig_turn(m, clip, steps[i], k, d);
        }
        return;
    }
    rfmv_pop(m, POP_CC, k);
    if (!q->view && q->step_held) {                     /* held steps: a CC lock (Delete held: removed) */
        const int clip = edit_clip(m);
        int s;
        for (s = 0; s < RFM_MOVE_NSTEPS; s++)
            if (q->step_held >> s & 1) {
                const int abs = q->page * RFM_MOVE_SEQ_PAGE + s, old = rfmv_seq_lock_get(m, clip, abs, cc);
                const int v = clampi((old >= 0 ? old : m->cc_val[b][k]) + d, 0, 127);
                rfmv_seq_lock_set(m, clip, abs, cc, (q->mods & SEQ_M_DELETE) ? -1 : v);
                q->step_edit |= (uint16_t)(1u << s);
            }
        if (q->mods & SEQ_M_DELETE)
            q->mods_used |= SEQ_M_DELETE;
        return;
    }
    {
        const int v = clampi(m->cc_val[b][k] + d, 0, 127);
        if (v != m->cc_val[b][k]) {
            m->cc_val[b][k] = (uint8_t)v;
            rfmv_emit_midi(m, 0xB0 | q->tr[q->sel_track].ch, cc, v);
        }
    }
}

void rfmv_seq_jog(rfm_move_t *m, int d, uint32_t now)
{
    rfm_move_seq_t *q = Q(m);
    (void)now;
    if (rfmv_seq_trk_held(m)) {                         /* a Track button held: the selected track's channel */
        rfm_move_track_t *t = &q->tr[q->sel_track];
        const int ch = clampi(t->ch + d, 0, 15);
        if (q->view)
            return;                                     /* (0.11.0: session: the held Track is the routing only) */
        rfmv_menu(m, MENU_TCH, ch);
        if (ch != t->ch) {
            rfmv_seq_release(m, q->sel_track);
            t->ch = (uint8_t)ch;
            rfmv_seq_dirty(m);
        }
        return;
    }
    if (q->smenu) {                                     /* a Shift menu: its first value */
        smenu_turn(m, -1, d);
        return;
    }
    if (!q->view && q->step_held) {                     /* held steps: the notes' length (Move: ~10 % of a step a
                                                         * detent; Shift: whole steps) */
        const int clip = edit_clip(m), whole = (q->mods & SEQ_M_SHIFT) != 0;
        int s, lo, hi, i;
        if (whole)
            q->mods_used |= SEQ_M_SHIFT;
        for (s = 0; s < RFM_MOVE_NSTEPS; s++)
            if (q->step_held >> s & 1) {
                const int abs = q->page * RFM_MOVE_SEQ_PAGE + s;
                hi = rfmv_seq_range(m, clip, abs, &lo);
                for (i = lo; i < hi; i++) {
                    const rfm_move_note_t n = q->notes[i];
                    const int len = whole ? (n.len / RFM_MOVE_SEQ_TPS + d) * RFM_MOVE_SEQ_TPS : n.len + d * LEN_FINE;
                    rfmv_seq_note_put(m, clip, n.step, n.note, n.vel,
                                      clampi(len, whole ? RFM_MOVE_SEQ_TPS : 1, clip_len(m) * RFM_MOVE_SEQ_TPS), n.micro);
                }
                q->step_edit |= (uint16_t)(1u << s);
            }
        rfmv_pop(m, POP_TRIG, 6);                       /* (the TRIG page's LENGTH popup) */
        return;
    }
    if (q->mods & SEQ_M_LOOP) {                         /* Loop held (or latched): the clip's length (Move's), a
                                                         * step a detent (Shift: a page) */
        const int clip = edit_clip(m), unit = (q->mods & SEQ_M_SHIFT) ? RFM_MOVE_SEQ_PAGE : 1;
        const int L = q->len[clip], nl = L + d * unit;
        if (!(q->mods_used & SEQ_M_LOOP))
            rfmv_seq_group(m);
        q->mods_used |= SEQ_M_LOOP | ((q->mods & SEQ_M_SHIFT) ? SEQ_M_SHIFT : 0);
        rfmv_seq_len_set(m, clip, clampi(nl, unit, RFM_MOVE_SEQ_MAXLEN));
        rfmv_pop(m, POP_LOOP, 0);
        m->pop_val = 1;                                 /* (the jog: Move's "Loop Length") */
        return;
    }
    if (q->mods & SEQ_M_SHIFT) {                        /* Shift + jog: the tempo, 1 BPM a detent */
        q->mods_used |= SEQ_M_SHIFT;
        if (q->following)
            rfmv_seq_msg(m, SEQ_MSG_EXTTAP);
        else {
            rfm_move_seq_set_bpm(m, q->bpm10 + d * 10);
            rfmv_pop(m, POP_TEMPO, 0);
        }
        return;
    }
    if (m->s.layout == RFM_MOVE_LAYOUT_DRUM && m->edit_pad >= 0 && m->pad_down[m->edit_pad] && !q->view) {
        const int p = m->edit_pad, n = clampi(m->s.drum[p] + d, 0, 127);
        if (n != m->s.drum[p]) {
            m->s.drum[p] = (uint8_t)n;
            rfmv_changed(m);
        }
        rfmv_pop(m, POP_DRUM, p);
        return;
    }
    {                                                   /* the knob pages: TRIG, then banks 1..4 */
        const int pg = clampi((q->trig ? 0 : m->s.bank + 1) + d, 0, RFM_MOVE_NBANKS);
        q->trig = (uint8_t)(pg == 0);
        if (pg && pg - 1 != m->s.bank) {
            m->s.bank = (uint8_t)(pg - 1);
            rfmv_changed(m);
        }
        rfmv_pop(m, POP_BANK, 0);
    }
}

/* ================================================================ LEDs === */
static int blink(const rfm_move_seq_t *q) { return q->tl >= 0 && (q->tl / (RFM_MOVE_SEQ_TPS * 2)) & 1; }   /* 8ths */
/* a beat: bright, fading, in RFM_MOVE_PULSE_STEPS steps (0.9.2: was a new level every tick, 96 a beat: every playing
 * clip in session view an RGB write a tick, some 630 a second with four clips at 99 BPM, against 13 in track view) */
#define RFM_MOVE_PULSE_STEPS 6
static int pulse(const rfm_move_seq_t *q)
{
    return q->playing && q->T >= 0 ? 256 - (int)(q->T % 96) / (96 / RFM_MOVE_PULSE_STEPS) * (160 / RFM_MOVE_PULSE_STEPS) : 256;
}


/* a clip's state in session view (the LEDs and the screen's grid) */
/* (0.10.0) CS_ARMED: stopped, the track's clip (Play starts it); CS_HOLLOW: a clip with no notes or locks */
enum { CS_EMPTY, CS_CONTENT, CS_PLAYING, CS_QUEUED, CS_STOPPING, CS_REC, CS_REC_WAIT, CS_ARMED, CS_HOLLOW };
static int clip_state(const rfm_move_t *m, int track, int scene)
{
    const rfm_move_seq_t *q = &m->seq;
    const rfm_move_track_t *t = &q->tr[track];
    const int used = rfmv_seq_clip_has(m, SEQ_CLIP(track, scene));
    if (t->grow && (t->active == scene || t->queued == scene))
        return t->grow == 1 ? CS_REC : CS_REC_WAIT;
    if (t->queued == scene)
        return CS_QUEUED;
    if (t->active == scene && used && running(q) && (t->queued == SEQ_STOP || t->queued >= 0))
        return CS_STOPPING;                             /* (0.11.0: also when another clip of the track waits) */
    if (t->active == scene && used)
        return q->playing ? CS_PLAYING : CS_ARMED;
    return !used ? CS_EMPTY : rfmv_seq_clip_used(m, SEQ_CLIP(track, scene)) ? CS_CONTENT : CS_HOLLOW;
}
/* (0.10.0) a scene (a row across the 16 tracks): 0 no clip in it, 1 clips, 2 the one that plays (stopped: the one Play
 * starts): every track with a clip in the row plays it and the others play nothing, 3 on its way (waiting for the bar),
 * (0.11.0) 4 the one that plays, on its way out (another scene, or a stop, waits for the bar) */
static int scene_state(const rfm_move_t *m, int scene)
{
    const rfm_move_seq_t *q = &m->seq;
    int t, any = 0, is = 1, will = 1, wait = 0;
    for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++) {
        const int has = rfmv_seq_clip_has(m, SEQ_CLIP(t, scene));
        const int act = q->tr[t].active;                /* (a track on an empty slot plays nothing) */
        const int now = act >= 0 && rfmv_seq_clip_has(m, SEQ_CLIP(t, act)) ? act : -1;
        const int then = q->tr[t].queued == SEQ_NONE ? now : q->tr[t].queued == SEQ_STOP ? -1 : q->tr[t].queued;
        any |= has;
        wait |= q->tr[t].queued != SEQ_NONE;
        if (has ? now != scene : now >= 0)
            is = 0;
        if (has ? then != scene : then >= 0)
            will = 0;
    }
    return !any ? 0 : wait && will && !is ? 3 : wait && is && !will ? 4 : is ? 2 : 1;
}

/* (0.12.0, the owner's) session view: a column's track does not reach the firmware played (its pads dimmed to a quarter,
 * its cells' outlines dotted). Not with no firmware list (no host), nor while a Track is held (the pads: the routing) */
static int sv_unrouted(const rfm_move_t *m, int track)
{
    return m->fw.host && !rfmv_seq_trk_held(m) && track < RFM_MOVE_SEQ_TRACKS &&
           !(m->seq.fx.ffilt[m->fw.active % RFM_MOVE_FW_SLOTS] >> track & 1);
}

/* (0.8.0, 0.9.0) the lock menu's steps (the board's colours): the track selected for the firmware in its colour, the
 * firmware's solo blue, not routed to it dim red, the others by their content */
/* (0.11.0, the owner's) not routed: dark; routed: the firmware's colour (a track with nothing in it dimmer); the
 * track selected for the firmware blinks; its solo blue; pressed (and while its click waits): white */
static rgb_t route_rgb(const rfm_move_t *m, int fw, int i)
{
    const rfm_move_seq_t *q = &m->seq;
    const int on = q->fx.ffilt[fw] >> i & 1, sel = m->s.lock[fw] == i, ph = rfmv_now_ms(m) % 500u < 250u;
    const rgb_t fc = rfmv_fw_rgb(m, fw), blue = { 40, 110, 255 };
    if (q->tk_pend == i && q->tk_pend_fw == fw)
        return sc(RFMV_WHITE, 150);
    if (m->fsolo[fw] == i + 1)
        return sel && !ph ? sc(blue, 70) : blue;
    if (sel)
        return on ? sc(fc, ph ? 256 : 50) : sc(fc, ph ? 60 : 0);
    return !on ? RFMV_OFF : sc(fc, rfmv_seq_track_used(m, i) ? 170 : 100);   /* (plainly lit: dark means not routed) */
}
rgb_t rfmv_lock_rgb(const rfm_move_t *m, int fw, int i)
{
    return m->seq.tstep_held >> i & 1 ? RFMV_WHITE : route_rgb(m, fw, i);
}
/* (0.11.0) session view, a firmware Track held: the pads are the routing: a column lit: its track reaches the firmware */
void rfmv_seq_route_leds(const rfm_move_t *m, int fw, rgb_t c[L_WHITE0])
{
    const rfm_move_seq_t *q = &m->seq;
    int i;
    for (i = 0; i < RFM_MOVE_NPADS; i++) {
        const int track = q->track_page + i % 8;
        c[L_PAD0 + i] = i % 8 >= RFM_MOVE_SV_COLS || track >= RFM_MOVE_SEQ_TRACKS ? RFMV_OFF
                      : m->pad_down[i] ? RFMV_WHITE : route_rgb(m, fw, track);
    }
    for (i = 0; i < RFM_MOVE_NSTEPS; i++)
        c[L_STEP0 + i] = rfmv_lock_rgb(m, fw, i);
    for (i = 0; i < RFM_MOVE_NKNOBS; i++)               /* (the knobs: nothing here) */
        c[L_RING0 + i] = RFMV_OFF;
}
void rfmv_seq_leds(const rfm_move_t *m, rgb_t c[L_WHITE0], uint8_t w[13])
{
    const rfm_move_seq_t *q = &m->seq;
    const rgb_t mode = rfmv_mode_rgb[RFM_MOVE_MODE_SEQ], tc = TRACK_RGB[q->sel_track];
    const int clip = edit_clip(m), bl = blink(q), pu = pulse(q);
    const int t4 = rfmv_seq_trk_held(m);
    int i;
    /* pads */
    for (i = 0; i < RFM_MOVE_NPADS; i++) {
        rgb_t v = RFMV_OFF;
        if (q->projects) {
            const int slot = (3 - i / 8) * 8 + i % 8;
            if (slot == q->proj_pending)
                v = bl ? mode : RFMV_OFF;
            else if (slot == q->proj_cur)
                v = mode;
            else if (q->proj_used >> slot & 1)
                v = sc(RFMV_WHITE, 72);
            else
                v = sc(RFMV_WHITE, 8);
        } else if (q->view && i % 8 >= RFM_MOVE_SV_COLS) {   /* (0.10.0) the scenes: green (Control Live's) */
            switch (scene_state(m, q->scene_off + (3 - i / 8))) {
            case 4: v = sc(RFMV_GREEN, bl ? 40 : 256); break;       /* on its way out (against the one coming) */
            case 3: v = sc(RFMV_GREEN, bl ? 256 : 40); break;       /* on its way */
            case 2: v = sc(RFMV_GREEN, pu); break;                  /* the one that plays (stopped: full) */
            case 1: v = sc(RFMV_GREEN, 70); break;                  /* clips in the row */
            default: v = sc(RFMV_GREEN, 12); break;                 /* none (pressed: every track stops) */
            }
        } else if (q->view) {                           /* (0.10.0, the owner's review) Live's session colours: green
                                                         * plays (pulsing; stopped: steady, Play starts it), green
                                                         * blinking waits for the bar, a stopped clip its track's
                                                         * colour, dim (no notes: very dim), white the selected one
                                                         * (playing: white on the beat) */
            const int track = q->track_page + i % 8, scene = q->scene_off + (3 - i / 8);
            const rfm_move_track_t *t = &q->tr[track];
            const int col = t->mute ? 96 : 256, sel = track == q->sel_track && t->sel == scene;
            const int src = rfmv_seq_src_clip(m, SEQ_CLIP(track, scene));
            const int hollow = !rfmv_seq_clip_used(m, SEQ_CLIP(track, scene));
            switch (src ? -1 : clip_state(m, track, scene)) {
            case -1: v = src == 2 ? sc(RFMV_RED, bl ? 200 : 60) : sc(RFMV_WHITE, bl ? 256 : 40); break;   /* cut, copied */
            case CS_REC: v = sc(RFMV_RED, pu); break;
            case CS_REC_WAIT: v = sc(RFMV_RED, bl ? 256 : 40); break;
            case CS_QUEUED: v = bl ? RFMV_GREEN : sel ? sc(RFMV_WHITE, 60) : sc(RFMV_GREEN, 40); break;
            case CS_STOPPING:                           /* (0.11.0) to stop: green flashing away to its stopped colour,
                                                         * against the flash of the clip that waits */
                v = bl ? sc(TRACK_RGB[track], 80 * col / 256) : sc(RFMV_GREEN, col);
                break;
            case CS_PLAYING:
                v = sel && pu == 256 ? RFMV_WHITE : sc(RFMV_GREEN, pu * (hollow ? 70 : col) / 256);
                break;
            case CS_ARMED: v = sel ? sc(RFMV_WHITE, 150) : sc(RFMV_GREEN, hollow ? 40 : col); break;
            case CS_CONTENT: v = sel ? sc(RFMV_WHITE, 150) : sc(TRACK_RGB[track], 80 * col / 256); break;
            case CS_HOLLOW: v = sel ? sc(RFMV_WHITE, 60) : sc(TRACK_RGB[track], 22); break;
            default: v = sel ? sc(RFMV_WHITE, 60) : RFMV_OFF; break;
            }
            if (sv_unrouted(m, track) && !m->pad_down[i])
                v = sc(v, 64);                          /* (0.12.0) not reaching the firmware played: a quarter */
        } else {
            const int note = rfm_move_pad_note(&m->s, i);
            if (note >= 0 && q->step_first >= 0) {      /* a step held: its notes bright white, the rest dim */
                if (rfmv_seq_find(m, clip, q->page * RFM_MOVE_SEQ_PAGE + q->step_first, note) >= 0)
                    v = RFMV_WHITE;
                else
                    v = sc(rfmv_layout_pad(m, i, mode), 96);
            } else {
                v = rfmv_midi_pad(m, i, mode);
                if (note >= 0 && !m->pad_down[i]) {     /* the notes an empty step gets: the track's colour */
                    uint8_t nn[RFM_MOVE_SEQ_POLY], vv[RFM_MOVE_SEQ_POLY];
                    const int n = last_notes(m, nn, vv);
                    int k;
                    for (k = 0; k < n; k++)
                        if (nn[k] == note && q->nlast)
                            v = sc(tc, 200);
                }
            }
        }
        c[L_PAD0 + i] = v;
    }
    /* steps */
    for (i = 0; i < RFM_MOVE_NSTEPS; i++) {
        rgb_t v = RFMV_OFF;
        if (q->projects)
            v = i == 0 ? sc(RFMV_WHITE, 96) : RFMV_OFF;
        else if (t4 && !(q->mods & SEQ_M_MUTE)) {       /* (0.8.0) the lock menu (the board's colours): the track
                                                         * locked to the held firmware in its colour, soloed blue, not
                                                         * routed to it dim red, the others by their content */
            v = rfmv_lock_rgb(m, t4 - 1, i);
        } else if (t4 || (q->mods & SEQ_M_MUTE)) {      /* the tracks */
            const int used = rfmv_seq_track_used(m, i);
            if (q->tr[i].solo)
                v = sc((rgb_t){ 40, 90, 255 }, t4 && i == q->sel_track ? 256 : 140);   /* soloed: blue (Move's) */
            else if (q->tr[i].mute)
                v = sc(RFMV_RED, t4 && i == q->sel_track ? 256 : 48);
            else if (i == q->sel_track)
                v = TRACK_RGB[i];
            else
                v = sc(TRACK_RGB[i], used ? 64 : 10);
        } else if (q->smenu == RFM_MOVE_SM_ARP && q->arp_page && !(q->mods & SEQ_M_SHIFT)) {
            /* (0.11.0) an arp lane: its steps: the one picked white, the one just played green, a step that does
             * something else than play (a transpose) orange, a rest faint; past the lane's length dark */
            const rfm_move_arp_t *a = &q->tr[q->sel_track].arp;
            const int L = q->arp_page - 1, len = clampi(L ? a->tlen : a->mlen, 1, 16), lv = a->lane[L][i];
            const int at = q->arp_run && q->arp_track == q->sel_track ? ((L ? q->arp_tpos : q->arp_mpos) + len - 1) % len : -1;
            if (i >= len)
                v = RFMV_OFF;
            else if (i == q->arp_sel)
                v = RFMV_WHITE;
            else if (i == at)
                v = RFMV_GREEN;
            else if (L ? lv != 0 : lv != RFM_MOVE_AL_PLAY && lv != RFM_MOVE_AL_REST)
                v = sc((rgb_t){ 255, 72, 0 }, 150);
            else
                v = sc(RFMV_WHITE, !L && lv == RFM_MOVE_AL_REST ? 14 : 70);
        } else if (q->mods & SEQ_M_LOOP) {              /* the pages: the loop's in the track's colour, the one
                                                         * shown white, the playing one pulsing; the clip's others
                                                         * (outside the loop) dim; past the clip off */
            const int a = rfmv_seq_loop_lo(m, clip) / RFM_MOVE_SEQ_PAGE, b = (rfmv_seq_loop_hi(m, clip) - 1) / RFM_MOVE_SEQ_PAGE;
            const int pos = q->tr[q->sel_track].active == q->tr[q->sel_track].sel ? rfmv_seq_clip_pos(m, q->sel_track) : -1;
            const int end = clip_end_page(m, clip);
            int lo, hi = rfmv_seq_clip_range(m, clip, &lo), k, notes = 0;
            for (k = lo; k < hi && !notes; k++)
                notes = q->notes[k].step / RFM_MOVE_SEQ_PAGE == i;
            if (i == q->page)
                v = RFMV_WHITE;
            else if (pos >= 0 && pos / RFM_MOVE_SEQ_PAGE == i)
                v = sc(i >= a && i <= b ? tc : RFMV_WHITE, pu);   /* the playhead's page */
            else if (i >= a && i <= b)
                v = tc;
            else if (i <= end)
                v = sc(RFMV_WHITE, notes ? 48 : 14);
            else
                v = RFMV_OFF;
            if (rfmv_seq_src_page(m, clip, i))           /* the page clipboard's source: cut red, copied blinking */
                v = rfmv_seq_src_page(m, clip, i) == 2 ? sc(RFMV_RED, bl ? 220 : 60) : sc(RFMV_WHITE, bl ? 256 : 30);
        } else if (q->mods & SEQ_M_SHIFT) {             /* the shortcuts */
            if (i == SH_PROJECTS || i == SH_CLOCK || i == SH_FIXVEL || i == SH_DOUBLE || i == SH_QUANT || sh_menu(i) ||
                i == SH_METRO || i == SH_NEWCLIP || i == SH_CLEAR)
                v = sc(RFMV_WHITE, 96);
            if (sh_menu(i) && sh_menu(i) == q->smenu)
                v = rfmv_mode_rgb[RFM_MOVE_MODE_SEQ];
            if (i == SH_METRO && q->metro)
                v = RFMV_WHITE;
            if (i == SH_FIXVEL && m->s.vel_mode == RFM_MOVE_VEL_FIXED)
                v = RFMV_WHITE;
            if (i == SH_CLOCK && q->following)
                v = RFMV_GREEN;
        } else if (q->view && i == RFM_MOVE_NSTEPS - 1) {   /* (0.10.0) step 16: every clip stops: white while
                                                         * something plays, blinking while it all stops */
            int t, any = 0, all = 1;
            for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++) {
                if ((q->tr[t].active >= 0 && running(q)) || q->tr[t].queued >= 0)
                    any = 1;
                if ((q->tr[t].active >= 0 && running(q) && q->tr[t].queued != SEQ_STOP) || q->tr[t].queued >= 0)
                    all = 0;
            }
            v = any ? (all ? sc(RFMV_WHITE, bl ? 256 : 30) : RFMV_WHITE) : sc(RFMV_WHITE, 8);
        } else if (q->view && i / 2 >= RFM_MOVE_SV_COLS) {
            v = RFMV_OFF;                               /* (step 15, under the scenes: nothing) */
        } else if (q->view) {                           /* (0.10.0) the 2 steps under each track's column: select (the
                                                         * track's colour, the selected one full; muted: red), stop
                                                         * (white while it plays something to stop) */
            const int tr = q->track_page + i / 2;
            const rfm_move_track_t *t = &q->tr[tr];
            if (!(i & 1))
                v = t->mute ? sc(RFMV_RED, tr == q->sel_track ? 256 : 60) : sc(TRACK_RGB[tr], tr == q->sel_track ? 256 : 50);
            else
                v = (t->active >= 0 && running(q) && rfmv_seq_clip_has(m, SEQ_CLIP(tr, t->active))) || t->queued >= 0
                        ? (t->queued == SEQ_STOP ? sc(RFMV_WHITE, bl ? 256 : 30) : RFMV_WHITE)
                        : sc(RFMV_WHITE, 8);
        } else {                                        /* Move's step colours */
            const int abs = q->page * RFM_MOVE_SEQ_PAGE + i, src = rfmv_seq_src_step(m, clip, abs);
            int lo, hi = rfmv_seq_range(m, clip, abs, &lo);
            if (q->steprec && abs == q->step_cur)
                v = RFMV_RED;                           /* Step record's cursor */
            else if (src)
                v = src == 2 ? sc(RFMV_RED, bl ? 220 : 60) : sc(RFMV_WHITE, bl ? 256 : 30);   /* cut, copied */
            else if (abs >= rfmv_seq_loop_lo(m, clip) && abs < rfmv_seq_loop_hi(m, clip)) {
                const int pos = q->tr[q->sel_track].active == q->tr[q->sel_track].sel ? rfmv_seq_clip_pos(m, q->sel_track) : -1;
                if (q->step_held >> i & 1)
                    v = tc;                             /* held: the track's colour, full */
                else if (pos == abs)
                    v = RFMV_GREEN;                     /* the playhead */
                else if (hi > lo)
                    v = RFMV_WHITE;                     /* notes */
                else
                    v = sc(tc, 28);                     /* empty: the track's colour, dim (the Move's LED shapes
                                                         * mark the beats) */
            } else if (q->step_held >> i & 1)
                v = tc;
            else
                v = hi > lo ? sc(RFMV_WHITE, 48) : sc(RFMV_WHITE, 6);   /* outside the loop: dim */
        }
        c[L_STEP0 + i] = v;
    }
    /* Play, Rec, Sample (the beat) */
    c[L_PLAY] = q->playing ? RFMV_GREEN : q->start_pending ? sc(RFMV_GREEN, bl ? 256 : 24) : sc(RFMV_WHITE, 40);
    c[L_REC] = q->start_pending == 3 || q->srec == 1 ? sc(RFMV_RED, bl ? 256 : 30)   /* (count-in, Capture armed) */
             : q->rec || q->srec == 2 || q->steprec ? RFMV_RED : sc(RFMV_RED, 30);
    if (q->arec)                                        /* Sample's loop: armed blinking, recording red */
        c[L_SAMPLE] = q->arec >= 2 ? RFMV_RED : sc(RFMV_RED, bl ? 256 : 30);
    else if (q->mods & SEQ_M_SHIFT)                     /* (Shift: tap tempo, the beat) */
        c[L_SAMPLE] = q->tl >= 0 && q->tl % 96 < 24 ? (q->following ? RFMV_GREEN : RFMV_WHITE) : sc(RFMV_WHITE, 16);
    else
        c[L_SAMPLE] = sc(RFMV_WHITE, 40);
    /* knob rings: the held step's locks, else the CC values */
    for (i = 0; i < RFM_MOVE_NKNOBS; i++) {
        const rgb_t bc = rfmv_bank_rgb[m->s.bank & 3];
        rgb_t v;
        if (q->trig && !q->view) {                      /* the TRIG page: the held step's values (lit: not the default) */
            const int ab = q->page * RFM_MOVE_SEQ_PAGE + (q->step_first >= 0 ? q->step_first : 0);
            const int tv = q->step_first >= 0 && i < 7 ? rfmv_seq_trig_val(m, clip, ab, i) : 0;
            const int def = i == 1 ? 1 : i == 2 ? 100 : i == 6 ? (q->step_first >= 0 ? RFM_MOVE_SEQ_TPS : 0) : 0;
            v = i >= 7 ? RFMV_OFF : m->touched == i ? RFMV_WHITE : tv != def ? sc(RFMV_WHITE, 200) : sc(RFMV_WHITE, 24);
        } else if (q->view) {                           /* (0.11.0, the owner's) session: a knob's touch shows its
                                                         * column's track (knob 8: the scenes), so they are lit: a track
                                                         * the firmware played now gets (its Track button) in that
                                                         * firmware's colour, the others faint white; the scenes green */
            const int act = m->fw.active % RFM_MOVE_FW_SLOTS, tr = q->track_page + i;
            if (i >= RFM_MOVE_SV_COLS)
                v = sc(RFMV_GREEN, m->touched == i ? 256 : 90);
            else if (tr >= RFM_MOVE_SEQ_TRACKS)
                v = RFMV_OFF;
            else if (q->fx.ffilt[act] >> tr & 1)
                v = sc(rfmv_fw_rgb(m, act), m->touched == i ? 256 : 120);
            else
                v = sc(RFMV_WHITE, m->touched == i ? 120 : 20);
        } else if (!q->view && q->step_first >= 0) {
            const int l = rfmv_seq_lock_get(m, clip, q->page * RFM_MOVE_SEQ_PAGE + q->step_first, m->s.cc[m->s.bank][i]);
            v = l < 0 ? RFMV_OFF : sc(bc, 32 + l * 224 / 127);
        } else
            v = m->touched == i ? bc : sc(bc, 16 + m->cc_val[m->s.bank][i] * 240 / 127);
        c[L_RING0 + i] = v;
    }
    /* the white LEDs */
    w[W_SHIFT] = (q->mods & SEQ_M_SHIFT) ? 127 : 24;
    w[W_MENU] = q->view ? 127 : 40;
    w[W_BACK] = 40;
    w[W_CAPTURE] = q->ncap ? 100 : 16;
    if (q->view) {
        w[W_UP] = q->scene_off > 0 ? 100 : 8;           /* (the clip slots, the tracks) */
        w[W_DOWN] = q->scene_off < RFM_MOVE_SEQ_SCENES - 4 ? 100 : 8;
        w[W_LEFT] = q->track_page > 0 ? 100 : 8;
        w[W_RIGHT] = q->track_page < RFM_MOVE_SEQ_TRACKS - RFM_MOVE_SV_COLS ? 100 : 8;
    } else if (q->step_held) {
        w[W_UP] = w[W_DOWN] = w[W_LEFT] = w[W_RIGHT] = 100;   /* (transpose, nudge) */
    } else {
        w[W_UP] = m->s.octave > 0 ? 127 : 24;
        w[W_DOWN] = m->s.octave < 0 ? 127 : 24;
        w[W_LEFT] = q->page > 0 ? 100 : 8;
        w[W_RIGHT] = q->page + 1 < RFM_MOVE_SEQ_PAGES ? ((q->page + 1) * RFM_MOVE_SEQ_PAGE < rfmv_seq_loop_hi(m, clip) ? 100 : 40) : 8;
    }
    w[W_UNDO] = q->un ? 60 : 6;
    if (q->loop_latch) {                                /* latched (page edit on): glowing, a second round */
        const int ph = (int)(q->now_us / 1000u % 1000u);
        w[W_LOOP] = (uint8_t)(60 + (ph < 500 ? ph : 1000 - ph) * 67 / 500);
    } else
        w[W_LOOP] = (q->mods & SEQ_M_LOOP) ? 127 : 40;
    w[W_COPY] = (q->mods & SEQ_M_COPY) ? 127 : q->paste ? (bl ? 127 : 30) : q->clip_kind && q->cut ? 70 : 30;
    w[W_MUTE] = (q->mods & SEQ_M_MUTE) ? 127 : q->tr[q->sel_track].mute ? 100 : 30;
    w[W_DELETE] = (q->mods & SEQ_M_DELETE) ? 127 : 30;
}

/* ================================================================ the screen === */
/* v2 (the design canvas's V2Track, V2Loop, V2Session, V2Menus, V2Arp, V2Projects, V2Record; drawn with v2.c):
 * symbols at rest, words only while something is touched or turned; Move's bottom strip says where you are */
static const struct { const char *a, *b; int8_t icon; } MSG[] = {
    { "", "", -1 }, { "No Empty", "Clip", -1 }, { "Full", "", -1 }, { "Load", "Failed", -1 }, { "Captured", "", -1 },
    { "Undone", "", -1 }, { "Redone", "", -1 }, { "Copied", "", -1 }, { "Pasted", "", -1 }, { "Cut", "", -1 },
    { "Quantized", "", -1 }, { "Loop", "Doubled", -1 }, { "Ext Tempo", "", V2I_LOCK }, { "Duplicated", "", -1 },
    { "Cleared", "", -1 }, { "Capture", "Cleared", -1 }, { "Clock Auto", "", -1 }, { "Clock Int", "", -1 },
    { "Clock Ext", "", -1 }, { "Full Vel", "", V2I_VEL }, { "Pad Vel", "", V2I_VEL }, { "Click On", "", V2I_METRO },
    { "Click Off", "", V2I_METRO }, { "New Clip", "", V2I_CLIP }, { "Clip", "Cleared", -1 }, { "Not routed", "", -1 },
};
const char *rfmv_seq_msg_text(const rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    return q->msg && rfmv_now_ms(m) - q->msg_ms < RFM_MOVE_POPUP_MS && q->msg < sizeof MSG / sizeof MSG[0] ? MSG[q->msg].a
                                                                                                         : NULL;
}
int rfmv_seq_msg_info(const rfm_move_t *m, const char **a, const char **b, int *icon)
{
    const rfm_move_seq_t *q = &m->seq;
    const int id = rfmv_seq_msg_text(m) ? q->msg : 0;
    *a = MSG[id].a, *b = MSG[id].b, *icon = MSG[id].icon;
    return id;
}

/* how full a group of notes looks: 0 none, 1 .. cap (a bar's height: the bits of the count) */
static int fullness(int n, int cap)
{
    int b = 0;
    while (n > 0)
        b++, n >>= 1;
    return b < cap ? b : cap;
}

/* a clip's last page: the loop's, or a later one with notes */
int rfmv_seq_end_page(const rfm_move_t *m, int clip) { return clip_end_page(m, clip); }

/* Move's strip in track view: the clip's pages (the shown one thick; a ghost page shown: the real pages a line and a
 * + at the far end), the playhead (or, at >= 0, that step) a tick over the whole clip */
void rfmv_seq_page_strip(const rfm_move_t *m, mr_canvas_t *cv, int at)
{
    const rfm_move_seq_t *q = &m->seq;
    const int clip = edit_clip(m), end = clip_end_page(m, clip), n = end + 1;
    const rfm_move_track_t *t = &q->tr[q->sel_track];
    int pos = at;
    if (q->page > end) {
        v2_ghostStrip(cv, n, q->page + 2);
        return;
    }
    if (pos < 0 && t->active == t->sel && running(q))
        pos = rfmv_seq_clip_pos(m, q->sel_track);
    v2_strip(cv, n, 1u << q->page, pos >= 0 ? (double)pos / (n * RFM_MOVE_SEQ_PAGE) : -1, 0, 128);
}

/* the 16 steps of the shown page as cells (only while a step, Copy or Delete is held, or Step record is on): notes
 * filled, empty outlined, copied dashed, cut crossed; the held ones (marks) underlined above */
void rfmv_seq_steps_cells(const rfm_move_t *m, mr_canvas_t *cv, uint32_t marks)
{
    rfmv_seq_steps_at(m, cv, marks, 52);
}
void rfmv_seq_steps_at(const rfm_move_t *m, mr_canvas_t *cv, uint32_t marks, int y)
{
    const rfm_move_seq_t *q = &m->seq;
    const int clip = edit_clip(m), hi = rfmv_seq_loop_hi(m, clip), lo0 = rfmv_seq_loop_lo(m, clip);
    int i;
    for (i = 0; i < RFM_MOVE_NSTEPS; i++) {
        const int abs = q->page * RFM_MOVE_SEQ_PAGE + i, src = rfmv_seq_src_step(m, clip, abs), x = i * 8;
        int lo;
        const int notes = rfmv_seq_range(m, clip, abs, &lo) > lo;
        if (src == 2)
            v2_O(cv, x, y, 7, 11, 1), v2_X(cv, x + 1, y + 2, 5, 7);
        else if ((abs < lo0 || abs >= hi) && !notes && !src)
            v2_dotH(cv, x, y + 10, 7);                  /* (outside the loop: a dashed line) */
        else if (src == 1) {
            if (notes)
                v2_R(cv, x + 2, y + 3, 3, 5, 1);
            v2_dashO(cv, x, y, 7, 11);
        } else if (notes)
            v2_R(cv, x, y, 7, 11, 1);
        else
            v2_O(cv, x, y, 7, 11, 1);
        if (marks >> i & 1)
            v2_R(cv, x, y - 3, 7, 2, 1);
    }
}

/* V2Projects proj.grid: the 32 slots as the pads are (top-left = 1); the open one filled, a saved one outlined, each
 * with three bars (its tracks with notes in three groups, as fullness()); an empty slot a dot; the clipboard's
 * source crossed (cut) or dashed (copied). A project loading (the host's answer pending) blinks. */
static void projects_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    int i;
    for (i = 0; i < RFM_MOVE_PROJECTS; i++) {
        const int x = (i % 8) * 16 + 1, y = (i / 8) * 16 + 1, cur = i == q->proj_cur || (i == q->proj_pending && blink(q));
        rfm_move_proj_info_t in;
        int used[RFM_MOVE_SEQ_TRACKS], nu = 0, g, t;
        if (q->pbuf_kind && q->pbuf_src == i && !cur) {  /* the buffer's source: cut crossed, copied dashed */
            if (q->pbuf_kind == 2 && !(q->proj_used >> i & 1)) {
                v2_O(cv, x, y, 14, 14, 1), v2_X(cv, x + 3, y + 3, 8, 8);
                continue;
            }
            if (q->pbuf_kind == 1) {
                v2_dashO(cv, x, y, 14, 14);
                continue;
            }
        }
        if (!cur && !(q->proj_used >> i & 1)) {
            v2_R(cv, x + 6, y + 6, 2, 2, 1);
            continue;
        }
        if (cur)
            v2_R(cv, x, y, 14, 14, 1);
        else
            v2_O(cv, x, y, 14, 14, 1);
        rfmv_proj_info(m, i, &in);
        if (!in.ok)
            continue;
        for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
            if (in.notes[t])
                used[nu++] = in.notes[t];
        for (g = 0; g < 3; g++) {                       /* the used tracks in thirds (1 track: one bar, 2: two) */
            const int a = nu * g / 3, b = nu * (g + 1) / 3;
            int n = 0, h;
            for (t = a; t < b; t++)
                n += used[t];
            h = fullness(n, 8);
            if (h)
                v2_R(cv, x + 3 + g * 3, y + 11 - h, 2, h, !cur);
        }
    }
}

/* V2Session sess.rest (the owner's mockup): the 8 tracks in view across, their 4 clip slots down; a 14 x 2 bar over
 * the selected track (muted tracks: a dashed line on top); playing filled with a black play icon, recording filled
 * with a black square, queued doubled (blinking), stopping blinking, a clip outlined, empty a dot; the clipboard's
 * source dashed (copied) or crossed (cut) */
static void session_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    const int bl = blink(q);
    int c, r;
    for (c = 0; c < RFM_MOVE_SV_COLS; c++) {
        const int track = q->track_page + c, x = c * 16 + 1;
        if (track == q->sel_track)
            v2_R(cv, x, 0, 14, 2, 1);
        else if (q->tr[track].mute)
            v2_dotH(cv, x, 1, 14);
    }
    for (r = 0; r < 4; r++) {
        const int scene = q->scene_off + r, ss = scene_state(m, scene);
        char n[4];
        for (c = 0; c < RFM_MOVE_SV_COLS; c++) {
            const int track = q->track_page + c;
            const int src = rfmv_seq_src_clip(m, SEQ_CLIP(track, scene));
            const int hollow = !rfmv_seq_clip_used(m, SEQ_CLIP(track, scene));
            char s = '.';
            if (src)
                s = src == 2 ? 'X' : 'D';
            else
                switch (clip_state(m, track, scene)) {
                case CS_CONTENT: s = 'C'; break;
                case CS_HOLLOW: s = 'H'; break;
                case CS_ARMED: s = hollow ? 'h' : 'A'; break;
                case CS_QUEUED: s = bl ? (hollow ? 'h' : 'A') : (hollow ? 'H' : 'C'); break;   /* (0.11.0: the play
                                                         * icon flashing) */
                case CS_PLAYING: s = hollow ? 'h' : 'P'; break;
                case CS_STOPPING: s = bl ? 'S' : hollow ? 'h' : 'P'; break;   /* (0.11.0: a stop icon flashing) */
                case CS_REC: s = 'R'; break;
                case CS_REC_WAIT: s = bl ? 'C' : 'R'; break;
                default: break;
                }
            v2_cell(cv, c, r, s);
            if (sv_unrouted(m, track))
                v2_cellDots(cv, c, r);                  /* (0.12.0) the firmware played does not get this column */
            if (track == q->sel_track && q->tr[track].sel == scene) {   /* (0.9.2) the selected clip (or empty slot):
                                                         * a frame round its cell, in the gap between the cells; a
                                                         * filled cell (playing, recording) stands clear of it */
                const int x = c * 16 + 1, y = 4 + r * 15;
                if (s == 'P' || s == 'R' || s == 'S')
                    v2_O(cv, x, y, 14, 13, 0);
                v2_O(cv, x - 1, y - 1, 16, 15, 1);
            }
        }
        /* (0.10.0) the 8th column: the scene's number (the one that plays underlined; on its way: blinking) */
        /* (0.11.0) on its way: a play icon flashing; on its way out: a stop icon flashing */
        snprintf(n, sizeof n, "%d", scene + 1);
        if (ss == 3 && bl)
            v2_ic(cv, V2I_PLAY, RFM_MOVE_SV_COLS * 16 + 3, 4 + r * 15 + 1, 1);
        else if (ss == 4 && bl)
            v2_ic(cv, V2I_STOP, RFM_MOVE_SV_COLS * 16 + 3, 4 + r * 15 + 1, 1);
        else
            v2_TC(cv, RFM_MOVE_SV_COLS * 16 + 8, 4 + r * 15 + 3, n, 1, 1);
        if (ss == 2 || (ss == 4 && !bl))
            v2_R(cv, RFM_MOVE_SV_COLS * 16 + 3, 4 + r * 15 + 11, 10, 1, 1);
    }
}

/* (0.10.0, the owner's note) session view, a knob touched: its column's track, the whole screen: the track, the
 * firmwares it reaches, its selected clip and length, its channel */
void rfmv_seq_track_info(const rfm_move_t *m, int track, char row[4][32])
{
    const rfm_move_seq_t *q = &m->seq;
    const rfm_move_track_t *t = &q->tr[track];
    const int clip = SEQ_CLIP(track, t->sel);
    int f, n = 0;
    snprintf(row[0], 32, "Track %d", track + 1);
    row[1][0] = 0;
    if (m->fw.host) {
        for (f = 0; f < rfmv_fw_nslots(m); f++) {
            char nm[24];
            rfmv_fw_slot_name(m, f, nm, sizeof nm);
            if (nm[0] && (q->fx.ffilt[f] >> track & 1)) {
                const size_t l = strlen(row[1]);
                snprintf(row[1] + l, 32 - l, "%s%s", n ? " + " : "", nm);
                n++;
            }
        }
        if (!n)
            snprintf(row[1], 32, "Not routed");
    }
    if (!rfmv_seq_clip_used(m, clip))
        snprintf(row[2], 32, "Clip %d, empty", t->sel + 1);
    else if (q->len[clip] % RFM_MOVE_SEQ_PAGE)
        snprintf(row[2], 32, "Clip %d, %d steps", t->sel + 1, q->len[clip]);
    else
        snprintf(row[2], 32, "Clip %d, %d %s", t->sel + 1, q->len[clip] / RFM_MOVE_SEQ_PAGE,
                 q->len[clip] == RFM_MOVE_SEQ_PAGE ? "bar" : "bars");
    snprintf(row[3], 32, "Channel %d", t->ch + 1);
}
/* (0.11.0, the owner's) session view, knob 8 touched: the column under it launches scenes: "Scenes", the one that plays
 * (or waits), the rows in view */
void rfmv_seq_scene_pop(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    char b[24];
    int sc2, plays = -1, next = -1;
    for (sc2 = 0; sc2 < RFM_MOVE_SEQ_SCENES; sc2++) {
        const int st = scene_state(m, sc2);
        if (st == 2 || st == 4)
            plays = sc2;
        if (st == 3)
            next = sc2;
    }
    mr_gfx_clear(cv, 0);
    v2_ic(cv, V2I_PLAY, 2, 3, 1);
    v2_T(cv, 18, 2, "Scenes", 2, 1);
    if (next >= 0)
        snprintf(b, sizeof b, "Next %d", next + 1);
    else if (plays >= 0)
        snprintf(b, sizeof b, "Scene %d", plays + 1);
    else
        snprintf(b, sizeof b, "No scene");
    v2_T(cv, 0, 21, b, 2, 1);
    snprintf(b, sizeof b, "Rows %d-%d", q->scene_off + 1, q->scene_off + 4);
    v2_T(cv, 0, 43, b, 1, 1);
    v2_T(cv, 0, 54, q->playing ? "Playing" : "Stopped", 1, 1);
}
void rfmv_seq_track_pop(const rfm_move_t *m, mr_canvas_t *cv, int track)
{
    char row[4][32];
    rfmv_seq_track_info(m, track, row);
    mr_gfx_clear(cv, 0);
    v2_TC(cv, 64, 2, row[0], 2, 1);
    if (m->seq.tr[track].mute)
        v2_ic(cv, V2I_MUTE, 114, 4, 1);
    if (v2_tw(row[1], 2) <= 124)
        v2_TC(cv, 64, 21, row[1], 2, 1);
    else
        v2_TC(cv, 64, 25, row[1], 1, 1);
    v2_TC(cv, 64, 43, row[2], 1, 1);
    v2_TC(cv, 64, 54, row[3], 1, 1);
}

/* the shown page's notes as a piano roll: 8 px a step, 13 rows of 4 px (the page's highest note on top; squeezed past
 * 12 semitones), 3 px tall, the beats dashed, the playhead a line */
static void roll(const rfm_move_t *m, mr_canvas_t *cv, int y0, int h)
{
    const rfm_move_seq_t *q = &m->seq;
    const int clip = edit_clip(m), p0 = q->page * RFM_MOVE_SEQ_PAGE, row = (h - 4) / 12;
    int i, lo, hi, nlo = 127, nhi = 0, s;
    for (s = 4; s < 16; s += 4)
        v2_dotV(cv, s * 8, y0, h);
    hi = rfmv_seq_clip_range(m, clip, &lo);
    for (i = lo; i < hi; i++)
        if (q->notes[i].step >= p0 && q->notes[i].step < p0 + RFM_MOVE_SEQ_PAGE) {
            nlo = q->notes[i].note < nlo ? q->notes[i].note : nlo;
            nhi = q->notes[i].note > nhi ? q->notes[i].note : nhi;
        }
    for (i = lo; i < hi; i++) {
        const rfm_move_note_t *n = &q->notes[i];
        int x, w, r;
        if (n->step < p0 || n->step >= p0 + RFM_MOVE_SEQ_PAGE)
            continue;
        x = (n->step - p0) * 8 + 1 + n->micro / 3;
        w = n->len * 8 / RFM_MOVE_SEQ_TPS - 2;
        r = nhi - nlo <= 12 ? nhi - n->note + (12 - (nhi - nlo)) / 2 : (nhi - n->note) * 12 / (nhi - nlo);   /* (centred) */
        v2_R(cv, x, y0 + 2 + r * row, w < 1 ? 1 : w, 3, 1);
    }
    {
        const rfm_move_track_t *t = &q->tr[q->sel_track];
        const int tick = t->active == t->sel ? rfmv_seq_clip_tick(m, q->sel_track) : -1;
        if (tick >= 0 && tick / RFM_MOVE_SEQ_TPS >= p0 && tick / RFM_MOVE_SEQ_TPS < p0 + RFM_MOVE_SEQ_PAGE)
            v2_R(cv, (tick - p0 * RFM_MOVE_SEQ_TPS) * 8 / RFM_MOVE_SEQ_TPS, y0, 1, h, 1);
    }
}

/* V2Track seq.track: the shown page as a piano roll, full height; Move's strip the pages and the playhead. A step,
 * Copy or Delete held, or Step record: the steps row (seq.clip). Recording: Move's "Recording" row on top
 * (rec.capture); the audio loop: "Sampling" (rec.sample) */
static void track_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    uint32_t marks = q->step_held;
    int y0 = 0;
    if (q->arec) {                                      /* the audio loop (V2Record rec.sample): the wave icon and
                                                         * "Sampling" (armed: "Sample", blinking), the take's levels
                                                         * scrolling from the right as bars about the centre line (flat
                                                         * before), the bar lines dashed; the strip its bars, the tick
                                                         * where it is */
        int i, k;
        if (q->arec >= 2 || !blink(q)) {
            v2_ic(cv, V2I_WAVE, 0, 2, 1);
            v2_T(cv, 16, 1, q->arec == 1 ? "Sample" : "Sampling", 2, 1);
        }
        for (i = 0; i < RFM_MOVE_SAMPLE_BARS; i++) {
            const int back = RFM_MOVE_SAMPLE_BARS - i;  /* (the newest at the right) */
            const int a = back <= q->samp_n ? q->samp[(q->samp_head + RFM_MOVE_SAMPLE_BARS - back) % RFM_MOVE_SAMPLE_BARS] : 1;
            v2_R(cv, i * 2, 38 - a, 1, a * 2, 1);
        }
        for (k = 1; k < 4; k++)
            v2_dotV(cv, k * 32, 20, 36);
        if (q->arec >= 2 && q->T >= q->arec_T) {
            const int32_t in = q->T - q->arec_T;
            const int cur = (int)(in / RFM_MOVE_SEQ_BAR), n = cur + 1 > 4 ? cur + 1 : 4;
            v2_strip(cv, n > 16 ? 16 : n, 1u << (cur < 16 ? cur : 15), (double)in / ((double)n * RFM_MOVE_SEQ_BAR), 0, 128);
        } else
            v2_strip(cv, 4, 0, -1, 0, 128);
        return;
    }
    if (q->rec && running(q) && q->rec_mode != RFM_MOVE_RM_STEP) {
        v2_ic(cv, V2I_REC, 0, 2, 1);
        v2_T(cv, 16, 1, "Recording", 2, 1);
        y0 = 18;
    }
    /* the owner's: the steps always at the bottom (the roll above them), the held ones marked over them; Move's strip
     * under them (the pages, the playhead's tick) */
    roll(m, cv, y0, 44 - y0);
    if (q->steprec && q->step_cur / RFM_MOVE_SEQ_PAGE == q->page)
        marks |= 1u << (q->step_cur % RFM_MOVE_SEQ_PAGE);   /* (Step record's cursor) */
    rfmv_seq_steps_at(m, cv, marks, 47);
    rfmv_seq_page_strip(m, cv, -1);
}

/* the 16 tracks on one row, as the 16 step keys: the selected one filled, muted crossed, soloed a square inside,
 * with notes outlined, empty a dot; a word on top */
static void tracks_row(const rfm_move_t *m, mr_canvas_t *cv, const char *title)
{
    const rfm_move_seq_t *q = &m->seq;
    int i;
    v2_TC(cv, 64, 3, title, 2, 1);
    for (i = 0; i < RFM_MOVE_SEQ_TRACKS; i++) {
        const int x = i * 8, y = 28;
        if (i == q->sel_track) {
            v2_R(cv, x, y, 7, 26, 1);
            if (q->tr[i].mute)
                v2_R(cv, x + 1, y + 9, 5, 8, 0), v2_X(cv, x + 1, y + 9, 5, 8), v2_XR(cv, x + 1, y + 9, 5, 8);
            else if (q->tr[i].solo)
                v2_R(cv, x + 2, y + 10, 3, 6, 0);
        } else if (q->tr[i].mute)
            v2_O(cv, x, y, 7, 26, 1), v2_X(cv, x + 1, y + 9, 5, 8);
        else if (q->tr[i].solo)
            v2_O(cv, x, y, 7, 26, 1), v2_R(cv, x + 2, y + 10, 3, 6, 1);
        else if (rfmv_seq_track_used(m, i))
            v2_O(cv, x, y, 7, 26, 1);
        else
            v2_R(cv, x + 3, y + 12, 1, 2, 1);
    }
}

/* Mute held: the 16 tracks, the muted ones crossed */
static void mute_screen(const rfm_move_t *m, mr_canvas_t *cv) { tracks_row(m, cv, "Mute"); }

/* the Shift menus (V2Menus, V2Arp, V2Global): knobs and the jog only; the knob legend where the design has it */
static const char *const AP_NAMES[RFM_MOVE_AP_N] = { "Up", "Down", "Up Dn", "Dn Up", "U+D", "D+U", "Conv", "Div",
                                                     "Con Div", "Hi Up", "Hi UpDn", "Lo Up", "Lo UpDn", "Rand",
                                                     "Rand Oth", "Shuffle", "Played" };
/* an arp rate (notes a bar) as a musician says it: 1/16, a triplet 1/16T (3, 6, 12 .. a bar), a quintuplet 1/16Q
 * (5, 10, 20, 40: five in the time of four) */
static void arp_rate_name(int n, char *b, int cap)
{
    if (n % 5 == 0)
        snprintf(b, (size_t)cap, "1/%dQ", n * 4 / 5);
    else if (n % 3 == 0)
        snprintf(b, (size_t)cap, "1/%dT", n * 2 / 3);
    else
        snprintf(b, (size_t)cap, "1/%d", n);
}
static int touched_knob(const rfm_move_t *m) { return m->touched >= 0 && m->touched < RFM_MOVE_NKNOBS ? m->touched : -1; }
static void fit2(mr_canvas_t *cv, int x, int y, const char *s, int w)   /* 2x where it fits in w, else 1x */
{
    if (v2_tw(s, 2) <= w)
        v2_T(cv, x, y, s, 2, 1);
    else
        v2_T(cv, x, y + 4, s, 1, 1);
}
/* ---- (0.6.2) Global settings: a list. The jog turns through the rows (the selected one inverted); a click edits
 * its value (the value inverted, the jog changes it), a click again (or Back) leaves the edit; Back closes the menu.
 * While it is open it takes the Move's controls (nothing reaches the FM-1, the firmware or the sequencer); releases
 * pass, so nothing pressed before stays held; Track buttons and Shift + step 2 (its own toggle) pass too */
enum { GL_SPEECH, GL_CLOCK, GL_VEL, GL_LEVEL, GL_POPUP, GL_LAUNCH, GL_N };   /* (0.12.0: Launch, the project's launch
                                                         * quantization) */   /* (0.6.10: the battery a header, no longer a row;
                                                         * 0.6.12: Speech first, the rest scroll) */
static const char *const GL_NAME[GL_N] = { "Speech", "Clock", "Velocity", "Fixed level", "Popup size", "Launch" };
/* (0.12.0) the launch quantizations as a list (the menus' order) and their names */
const char *rfmv_lq_name(int lq)
{
    static const char *const N[RFM_MOVE_LQ_N] = { "1 Bar", "Instant", "1 Beat", "2 Bars", "4 Bars", "Clip" };
    return lq >= 0 && lq < RFM_MOVE_LQ_N ? N[lq] : "?";
}
int rfmv_lq_step(int lq, int d)                         /* the next one in the list, d away (clamped) */
{
    int i, k = 0;
    for (i = 0; i < RFM_MOVE_LQ_N; i++)
        if (LQ_ORDER[i] == lq)
            k = i;
    return LQ_ORDER[clampi(k + d, 0, RFM_MOVE_LQ_N - 1)];
}
#define GL_ROWS 4                                       /* rows on screen (more scroll) */
int rfmv_global_rows(void) { return GL_N; }
static void gl_words(const rfm_move_t *m, int r, const char *pre, char *o, int cap);
void rfmv_global_words(const rfm_move_t *m, char *o, int cap) { gl_words(m, clampi(m->gl_row, 0, GL_N - 1), "", o, cap); }
static void gl_value(const rfm_move_t *m, int r, char *o, int cap, int spoken)
{
    static const char *const CK[3] = { "Auto", "Int", "Ext" }, *const CKS[3] = { "Auto", "Internal", "External" };
    static const char *const SP[3] = { "Off", "On", "Jog" }, *const SPS[3] = { "Off", "On", "Jog wheel" };
    switch (r) {
    case GL_SPEECH: snprintf(o, (size_t)cap, "%s", (spoken ? SPS : SP)[rfm_move_speech_mode(m) % 3]); break;
    case GL_CLOCK: snprintf(o, (size_t)cap, "%s", (spoken ? CKS : CK)[m->seq.clock_mode % 3]); break;
    case GL_VEL: rfmv_tcase(rfm_move_vel_name(m->s.vel_mode), o, cap); break;
    case GL_LEVEL: snprintf(o, (size_t)cap, "%d", m->s.vel_level); break;
    case GL_LAUNCH: snprintf(o, (size_t)cap, "%s", rfmv_lq_name(m->seq.launch_q)); break;
    default: snprintf(o, (size_t)cap, "%s", m->s.pop_half ? "Half" : "Full"); break;
    }
}
/* (0.6.10) the battery as the header says it: "battery 87%", "battery unknown" */
void rfmv_global_battery_words(const rfm_move_t *m, char *o, int cap)
{
    if (m->battery >= 0)
        snprintf(o, (size_t)cap, "battery %d%%", m->battery);
    else
        snprintf(o, (size_t)cap, "battery unknown");
}
static void gl_words(const rfm_move_t *m, int r, const char *pre, char *o, int cap)
{
    char v[24];
    gl_value(m, r, v, sizeof v, 1);
    snprintf(o, (size_t)cap, "%s%s: %s", pre, GL_NAME[r % GL_N], v);
}
static void gl_change(rfm_move_t *m, int r, int d)
{
    switch (r) {
    case GL_SPEECH: {                                   /* (0.6.12) said in any mode: "Speech off" */
        static const char *const W[3] = { "Speech off", "Speech on", "Speech jog wheel" };
        const int md = clampi(rfm_move_speech_mode(m) + d, 0, 2);
        m->s.speech = (uint8_t)(md + 1), rfmv_changed(m);
        rfmv_say_text_always(m, W[md]);
        break;
    }
    case GL_CLOCK: rfm_move_seq_set_clock(m, clampi(m->seq.clock_mode + d, 0, 2)); break;
    case GL_VEL: rfmv_vel_step(m, d), rfmv_changed(m); break;
    case GL_LEVEL: m->s.vel_level = (uint8_t)clampi(m->s.vel_level + d, 1, 127), rfmv_changed(m); break;
    case GL_POPUP: {
        const int h = clampi(m->s.pop_half + d, 0, 1);
        if (h != m->s.pop_half)
            m->s.pop_half = (uint8_t)h, rfmv_changed(m);
        break;
    }
    case GL_LAUNCH: m->seq.launch_q = (uint8_t)rfmv_lq_step(m->seq.launch_q, d); break;   /* (0.12.0: the project's) */
    default: break;
    }
    rfmv_seq_dirty(m);
}
int rfmv_global_input(rfm_move_t *m, const mr_event_t *ev)
{
    rfm_move_seq_t *q = Q(m);
    char w[48];
    if (q->smenu != RFM_MOVE_SM_GLOBAL || q->projects)
        return 0;
    switch (ev->type) {
    case MR_EV_JOG: {
        const int d = ev->value;
        if (!d)
            return 1;
        if (m->gl_edit) {
            gl_change(m, m->gl_row, d);
            if (m->gl_row == GL_SPEECH)
                return 1;                               /* (its own words, said in any mode) */
            gl_words(m, m->gl_row, "", w, sizeof w);
        } else {
            m->gl_row = (uint8_t)clampi(m->gl_row + d, 0, GL_N - 1);
            gl_words(m, m->gl_row, "", w, sizeof w);
        }
        rfmv_say_text(m, w);
        return 1;
    }
    case MR_EV_JOG_CLICK:
        if (!ev->value)
            return 1;
        if (m->gl_edit)
            m->gl_edit = 0, rfmv_say_text(m, "Done");
        else {
            m->gl_edit = 1;
            gl_words(m, m->gl_row, "Editing ", w, sizeof w);
            rfmv_say_text(m, w);
        }
        return 1;
    case MR_EV_BUTTON:
        if (ev->index >= MR_CC_TRACK4 && ev->index <= MR_CC_TRACK1)
            return 0;                                   /* (the Track buttons; Track 4's Shift layer: its own toggle) */
        if (!ev->value)
            return ev->index == MR_CC_BACK || ev->index == MR_CC_MENU;   /* (their press was ours) */
        if (ev->index == MR_CC_SHIFT && m->s.mode != RFM_MOVE_MODE_FM1)
            return 0;                                   /* (Shift + step 2 closes it in Sequencer mode and, 0.8.0, a note
                                                         * layout; in the FM-1 layout Shift is the FM-1's GLO) */
        if (ev->index == MR_CC_BACK || ev->index == MR_CC_MENU) {
            if (m->gl_edit)
                m->gl_edit = 0, rfmv_say_text(m, "Done");
            else
                layer_close(m, LY_MENU);
        }
        return 1;
    case MR_EV_STEP:
        if (!ev->value || rfmv_track_held(m, 3) || (q->mods & SEQ_M_SHIFT) || (m->shift_down && m->s.mode == RFM_MOVE_MODE_MIDI))
            return 0;                                   /* (releases; Shift + step: the menus, this one's toggle) */
        return 1;
    case MR_EV_TOUCH:
        if (ev->index == 9)
            return 0;                                   /* (0.6.12: the jog's touch: Jog speech) */
        return ev->value != 0;                          /* (releases pass: a knob let go) */
    case MR_EV_PAD: return ev->value != 0;              /* (releases pass: a pad let go) */
    case MR_EV_PAD_PRESSURE:
    case MR_EV_KNOB: return 1;
    default: return 0;
    }
}
/* (0.6.10, the owner's direction A: design canvas DQGfSZ6KkaGXG3EgEeASou, boards "Battery header + list" and "editing
 * a value", drawn at 4x there) the battery as a header: an 18 x 12 outline at 3, 3 with a 2 x 6 nub, filled 14 x 8
 * to the charge, the percentage at 2x, "GLOBAL" small on the right; a rule at 17; four rows of 11 from 19 (the
 * name left, the value right at 122), the selected one inverted; editing: the row framed, its value inverted
 * between small arrows */
static void gl_arrow(mr_canvas_t *cv, int x, int y, int right)   /* 3 x 5, pointing left or right, black */
{
    v2_R(cv, right ? x : x + 2, y, 1, 5, 0);
    v2_R(cv, x + 1, y + 1, 1, 3, 0);
    v2_R(cv, right ? x + 2 : x, y + 2, 1, 1, 0);
}
static void global_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const int n = GL_N, sel = clampi(m->gl_row, 0, GL_N - 1);
    const int top = n <= GL_ROWS ? 0 : clampi(sel - GL_ROWS / 2, 0, n - GL_ROWS);
    char v[24];
    int i;
    v2_O(cv, 3, 3, 18, 12, 1);                          /* the battery */
    v2_R(cv, 21, 6, 2, 6, 1);
    if (m->battery >= 0) {
        v2_R(cv, 5, 5, (14 * clampi(m->battery, 0, 100) + 50) / 100, 8, 1);
        snprintf(v, sizeof v, "%d%%", clampi(m->battery, 0, 100));
    } else
        snprintf(v, sizeof v, "--");
    v2_T(cv, 27, 1, v, 2, 1);
    v2_T(cv, 126 - v2_tw("GLOBAL", 1), 4, "GLOBAL", 1, 1);
    v2_R(cv, 0, 17, 128, 1, 1);
    for (i = 0; i < GL_ROWS && top + i < n; i++) {
        const int r = top + i, y = 19 + i * 11, inv = r == sel && !m->gl_edit;
        gl_value(m, r, v, sizeof v, 0);
        if (inv)
            v2_R(cv, 0, y, 125, 11, 1);
        v2_T(cv, 3, y + 2, GL_NAME[r], 1, !inv);
        if (r == sel && m->gl_edit) {                   /* editing: framed; the value inverted between arrows */
            const int tw = v2_tw(v, 1), w = 2 + 3 + 2 + tw + 2 + 3 + 2, x0 = 125 - w;
            v2_O(cv, 0, y, 125, 11, 1);
            v2_R(cv, x0, y + 1, w, 9, 1);
            gl_arrow(cv, x0 + 2, y + 3, 0);
            v2_T(cv, x0 + 7, y + 2, v, 1, 0);
            gl_arrow(cv, x0 + 7 + tw + 2, y + 3, 1);
        } else
            v2_T(cv, 122 - v2_tw(v, 1), y + 2, v, 1, !inv);
    }
    if (n > GL_ROWS) {                                  /* a scroll bar (the divisor never 0, even folded) */
        const int h = 44 * GL_ROWS / n, over = n - GL_ROWS > 0 ? n - GL_ROWS : 1;
        v2_R(cv, 126, 19 + (44 - h) * top / over, 2, h, 1);
    }
}

/* (0.11.0) the arp menu's header: knob k's name and value, short enough for 2x where it can be (k < 0: the page at
 * rest: "Arp On" .., "Mod", "Transpose") */
static const char *const AL_SHORT[RFM_MOVE_AL_N1] = { "Play", "Rest", "Tie", "Repeat", "Lowest", "Highest", "Power", "Chord" };
static void arp_head(const rfm_move_t *m, int k, char *o, int cap)
{
    static const char *const AM[RFM_MOVE_AM_N] = { "Off", "On", "First", "Once", "Hold" };
    static const char *const AR[RFM_MOVE_AR_N] = { "Off", "Note", "Mod", "Trans" };
    const rfm_move_seq_t *q = &m->seq;
    const rfm_move_arp_t *a = &q->tr[q->sel_track].arp;
    char b[16];
    if (q->arp_page) {
        const int L = q->arp_page - 1, st = q->arp_sel & 15, v = a->lane[L][st];
        if (k == 0 && L)
            snprintf(o, (size_t)cap, "%d %+d", st + 1, v);
        else if (k == 0 && v >= RFM_MOVE_AL_N1)
            snprintf(o, (size_t)cap, "%d Note %d", st + 1, v - RFM_MOVE_AL_N1 + 1);
        else if (k == 0)
            snprintf(o, (size_t)cap, "%d %s", st + 1, AL_SHORT[clampi(v, 0, RFM_MOVE_AL_N1 - 1)]);
        else if (k == 1)
            snprintf(o, (size_t)cap, "Length %d", L ? a->tlen : a->mlen);
        else if (k == 2)
            snprintf(o, (size_t)cap, "Reset %s", AR[a->reset % RFM_MOVE_AR_N]);
        else
            snprintf(o, (size_t)cap, "%s", L ? "Transpose" : "Mod");
        return;
    }
    switch (k) {
    case 0: snprintf(o, (size_t)cap, "Pattern"); break;  /* (its name and picture are below) */
    case 1: arp_rate_name(rfmv_seq_arp_rate_of(a->rate), b, sizeof b), snprintf(o, (size_t)cap, "Rate %s", b); break;
    case 2: snprintf(o, (size_t)cap, "Octaves %d", a->range); break;
    case 3: snprintf(o, (size_t)cap, "Chance %d", a->chance); break;
    case 5: snprintf(o, (size_t)cap, "Gate %d%%", a->gate); break;
    case 6: snprintf(o, (size_t)cap, "Spread %+d", a->odist); break;
    case 7:
        if (a->quant)
            arp_rate_name(rfmv_seq_arp_rate_of(a->quant - 1), b, sizeof b), snprintf(o, (size_t)cap, "Sync %s", b);
        else
            snprintf(o, (size_t)cap, "Sync Off");
        break;
    default: snprintf(o, (size_t)cap, "Arp %s", AM[a->mode % RFM_MOVE_AM_N]); break;   /* (knob 5, and at rest) */
    }
}
/* the MOD lane: what a step does, as a sign in its 6 px column: play a block, rest a low dash, tie a line from the
 * step before, repeat a hollow block, lowest an arrow down, highest an arrow up, power chord two bars, chord three,
 * a played note its number */
static void arp_sign(mr_canvas_t *cv, int x, int v)
{
    int r;
    char d[2] = { 0, 0 };
    switch (v) {
    case RFM_MOVE_AL_PLAY: v2_R(cv, x, 28, 6, 12, 1); break;
    case RFM_MOVE_AL_REST: v2_R(cv, x, 38, 6, 2, 1); break;
    case RFM_MOVE_AL_TIE: v2_R(cv, x - 2, 33, 8, 2, 1); break;
    case RFM_MOVE_AL_REPEAT: v2_O(cv, x, 28, 6, 12, 1); break;
    case RFM_MOVE_AL_LO:
        for (r = 0; r < 3; r++)
            v2_R(cv, x + r, 28 + r * 4, 6 - r * 2, 4, 1);
        break;
    case RFM_MOVE_AL_HI:
        for (r = 0; r < 3; r++)
            v2_R(cv, x + r, 36 - r * 4, 6 - r * 2, 4, 1);
        break;
    case RFM_MOVE_AL_PWR: v2_R(cv, x, 28, 6, 5, 1), v2_R(cv, x, 35, 6, 5, 1); break;
    case RFM_MOVE_AL_CHORD: v2_R(cv, x, 28, 6, 3, 1), v2_R(cv, x, 32, 6, 4, 1), v2_R(cv, x, 37, 6, 3, 1); break;
    default:
        d[0] = (char)('1' + clampi(v - RFM_MOVE_AL_N1, 0, 8));
        v2_T(cv, x, 31, d, 1, 1);
        break;
    }
}
static void smenu_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    const rfm_move_track_t *t = &q->tr[q->sel_track];
    const int tk = touched_knob(m);
    char b[32], c[32];
    int i;
    switch (q->smenu) {
    case RFM_MOVE_SM_WORKFLOW:                          /* the track's channel, rate and quantize: a row a knob */
        v2_knobs(cv, 7, tk);
        snprintf(b, sizeof b, "Ch %d", t->ch + 1), v2_T(cv, 0, 14, b, 2, 1);
        snprintf(b, sizeof b, "Rate %s", rfm_move_seq_rate_name(t->rate)), v2_T(cv, 0, 31, b, 2, 1);
        snprintf(b, sizeof b, "Quant %d%%", q->qstrength), v2_T(cv, 0, 48, b, 2, 1);
        break;
    case RFM_MOVE_SM_TEMPO: {                           /* the tap icon (following: a padlock), the BPM at 4x; the
                                                         * taps counted on the strip */
        const int b10 = rfm_move_seq_bpm10(m);
        v2_ic(cv, q->following ? V2I_LOCK : V2I_TAP, 2, 3, 1);
        v2_TC(cv, 64, 3, "Tempo", 2, 1);
        snprintf(b, sizeof b, b10 % 10 && b10 < 1000 ? "%d.%d" : "%d", b10 % 10 && b10 < 1000 ? b10 / 10 : (b10 + 5) / 10, b10 % 10);
        v2_TC(cv, 64, 22, b, v2_tw(b, 4) <= 124 ? 4 : 3, 1);
        if (!q->following)
            v2_strip(cv, 4, (1u << clampi(q->ntap, 0, 4)) - 1, -1, 0, 128);
        break;
    }
    case RFM_MOVE_SM_GROOVE: {                          /* two 16ths, the second late by the groove; Global / Track */
        const int g = rfmv_seq_groove_of(m, q->sel_track), own = t->own & 1;
        v2_knobs(cv, 3, tk);
        v2_R(cv, 4, 12, 3, 30, 1);
        v2_R(cv, 28 + (g * 10 + 50) / 100, 12, 3, 30, 1);
        v2_dotV(cv, 28, 12, 30);
        snprintf(b, sizeof b, "%d%%", g);
        v2_titleBox(cv, "Groove", b, 88, 50);
        if (own)
            v2_ic(cv, V2I_GLOBE, 2, 48, 1), v2_icSel(cv, V2I_TRACK, 18, 48);
        else
            v2_icSel(cv, V2I_GLOBE, 2, 48), v2_ic(cv, V2I_TRACK, 18, 48, 1);
        v2_T(cv, 40, 48, own ? "Track" : "Global", 2, 1);
        break;
    }
    case RFM_MOVE_SM_LAYOUT: {                          /* Move's icon row: the chosen one underlined and named */
        static const int IC[4] = { V2I_INKEY, V2I_CHROM, V2I_DRUM, V2I_OCT };
        static const char *const NM[4] = { "In-Key", "Chromatic", "Drum", "Octaves" };
        for (i = 0; i < 4; i++)
            if (i == m->s.layout)
                v2_icSel(cv, IC[i], 22 + i * 24, 6);
            else
                v2_ic(cv, IC[i], 22 + i * 24, 6, 1);
        v2_TC(cv, 64, 30, NM[m->s.layout & 3], 2, 1);
        v2_strip(cv, 2, 1, -1, 0, 128);
        break;
    }
    case RFM_MOVE_SM_KEYS:                              /* the build's key / scale screen, Global / Track below */
        rfmv_keys_menu(m, cv, !(t->own & 2));
        break;
    case RFM_MOVE_SM_ARP: {
        /* (0.11.0, the owner's) each page has its own picture, so a knob's name and value go in the header (2x where
         * they fit): the touched knob's, or the one turned last (a second); at rest the page says what it is ("Arp
         * On", "Mod", "Transpose"). Under it a tick a knob the page uses, the active one long */
        const rfm_move_arp_t *a = &t->arp;
        const int mk = rfmv_seq_menu_knobs(m);
        int hk = tk >= 0 && (mk >> tk & 1) ? tk : -1;
        if (hk < 0 && q->sm_k >= 0 && (mk >> q->sm_k & 1) && rfmv_now_ms(m) - q->sm_ms < RFM_MOVE_POPUP_MS)
            hk = q->sm_k;
        arp_head(m, hk, b, sizeof b);
        fit2(cv, 0, 0, b, 128);
        for (i = 0; i < 8; i++)
            if (mk >> i & 1)
                v2_R(cv, i == hk ? 3 + i * 16 : 6 + i * 16, 17, i == hk ? 10 : 4, i == hk ? 2 : 1, 1);
        if (q->arp_page) {                              /* a lane: its 16 steps; MOD: what each does, as a sign;
                                                         * TRANSPOSE: bars from the line. The picked one underlined, the
                                                         * one just played ticked; past its length a dot */
            const int L = q->arp_page - 1, len = clampi(L ? a->tlen : a->mlen, 1, 16);
            const int at = q->arp_run && q->arp_track == q->sel_track ? ((L ? q->arp_tpos : q->arp_mpos) + len - 1) % len : -1;
            if (L)
                v2_dotH(cv, 0, 38, 128);
            for (i = 0; i < RFM_MOVE_ARP_LANE; i++) {
                const int x = i * 8 + 1, v = a->lane[L][i];
                if (i == q->arp_sel)
                    v2_R(cv, x, 56, 6, 2, 1);
                if (i >= len) {
                    v2_R(cv, x + 2, L ? 38 : 34, 2, 2, 1);
                    continue;
                }
                if (i == at)
                    v2_R(cv, x, 21, 6, 2, 1);
                if (L) {                                /* transpose: up or down from the line (two octaves: its
                                                         * whole height) */
                    const int d = clampi(v, -24, 24) * 15 / 24;
                    v2_R(cv, x, d > 0 ? 38 - d : d < 0 ? 39 : 37, 6, d ? (d > 0 ? d : -d) : 3, 1);
                } else
                    arp_sign(cv, x, v);
            }
            v2_strip(cv, 3, 1u << q->arp_page, -1, 0, 128);
            break;
        }
        {                                               /* the pattern's walk as a picture, its name, rate, range */
            uint8_t walk[8];
            int n = 0;
            switch (a->pat) {
            case RFM_MOVE_AP_DN: walk[0] = 2, walk[1] = 1, walk[2] = 0, n = 3; break;
            case RFM_MOVE_AP_UPDN: walk[0] = 0, walk[1] = 1, walk[2] = 2, walk[3] = 1, n = 4; break;
            case RFM_MOVE_AP_DNUP: walk[0] = 2, walk[1] = 1, walk[2] = 0, walk[3] = 1, n = 4; break;
            case RFM_MOVE_AP_UANDD: walk[0] = 0, walk[1] = 1, walk[2] = 2, walk[3] = 2, walk[4] = 1, walk[5] = 0, n = 6; break;
            case RFM_MOVE_AP_DANDU: walk[0] = 2, walk[1] = 1, walk[2] = 0, walk[3] = 0, walk[4] = 1, walk[5] = 2, n = 6; break;
            case RFM_MOVE_AP_CON: case RFM_MOVE_AP_CONDIV: walk[0] = 0, walk[1] = 2, walk[2] = 1, n = 3; break;
            case RFM_MOVE_AP_DIV: walk[0] = 1, walk[1] = 2, walk[2] = 0, n = 3; break;
            case RFM_MOVE_AP_HI1: case RFM_MOVE_AP_HI2: walk[0] = 2, walk[1] = 1, walk[2] = 2, walk[3] = 0, n = 4; break;
            case RFM_MOVE_AP_LO1: case RFM_MOVE_AP_LO2: walk[0] = 0, walk[1] = 1, walk[2] = 0, walk[3] = 2, n = 4; break;
            case RFM_MOVE_AP_RAND: case RFM_MOVE_AP_ROTH: case RFM_MOVE_AP_RONC:
                walk[0] = 1, walk[1] = 0, walk[2] = 2, walk[3] = 0, walk[4] = 1, n = 5; break;
            default: walk[0] = 0, walk[1] = 1, walk[2] = 2, n = 3; break;
            }
            for (i = 0; i < n; i++) {                   /* (off: outlined) */
                if (a->mode == RFM_MOVE_AM_OFF)
                    v2_O(cv, 2 + i * 10, 54 - walk[i] * 11, 8, 4, 1);
                else
                    v2_R(cv, 2 + i * 10, 54 - walk[i] * 11, 8, 4, 1);
            }
            snprintf(b, sizeof b, "Oct %d", a->range), v2_T(cv, 2, 22, b, 1, 1);   /* (its range: over the walk) */
            fit2(cv, 66, 23, AP_NAMES[a->pat % RFM_MOVE_AP_N], 62);
            arp_rate_name(rfmv_seq_arp_rate_of(a->rate), b, sizeof b), fit2(cv, 66, 42, b, 62);
            v2_strip(cv, 3, 1, -1, 0, 128);
        }
        break;
    }
    case RFM_MOVE_SM_REC: {                             /* the three modes as icons, the chosen one underlined and
                                                         * named; the style below with its icon */
        static const int IC[3] = { V2I_CAPTURE, V2I_REC, V2I_STEP };
        static const char *const NM[3] = { "Capture", "Record", "Step" };
        for (i = 0; i < 3; i++)
            if (i == q->rec_mode)
                v2_icSel(cv, IC[i], 34 + i * 24, 2);
            else
                v2_ic(cv, IC[i], 34 + i * 24, 2, 1);
        v2_TC(cv, 64, 22, NM[q->rec_mode % 3], 2, 1);
        v2_ic(cv, q->rec_style == RFM_MOVE_RS_REPLACE ? V2I_REPLACE : V2I_OVERDUB, 22, 44, 1);
        v2_T(cv, 40, 42, q->rec_style == RFM_MOVE_RS_REPLACE ? "Replace" : "Overdub", 2, 1);
        break;
    }
    case RFM_MOVE_SM_VEL: {                             /* the curve as a graph (Fixed: a flat line at the level), the
                                                         * option (the jog) and the level (knob 1) with a bar */
        v2_knobs(cv, 1, tk);
        v2_R(cv, 2, 12, 1, 40, 1), v2_R(cv, 2, 51, 40, 1, 1);
        for (i = 0; i < 38; i++) {
            const int v = rfm_move_vel(&m->s, 1 + i * 126 / 37);
            v2_R(cv, 3 + i, 51 - (v * 37 + 63) / 127, 1, 2, 1);
        }
        rfmv_tcase(rfm_move_vel_name(m->s.vel_mode), c, sizeof c);
        v2_T(cv, 50 + v2_tw(c, 2) <= 128 ? 50 : 128 - v2_tw(c, 2), 12, c, 2, 1);
        snprintf(b, sizeof b, "%d", m->s.vel_level), v2_T(cv, 50, 30, b, 2, 1);
        v2_vbar(cv, m->s.vel_level / 127.0, 0, 50, 76, 48);
        break;
    }
    case RFM_MOVE_SM_GLOBAL:                            /* (0.6.2) a list on the jog wheel */
        global_screen(m, cv);
        break;
    default: break;
    }
}

/* V2Loop loop.edit: Loop held or latched (page edit): the loop icon, the range in words, the 16 pages as cells (in
 * the loop filled, the clip's others outlined, past the clip a dot, cut crossed, copied dashed), the shown one
 * underlined */
void rfmv_seq_loop_edit(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    const int clip = edit_clip(m), pa = rfmv_seq_loop_lo(m, clip) / RFM_MOVE_SEQ_PAGE;
    const int pb = (rfmv_seq_loop_hi(m, clip) - 1) / RFM_MOVE_SEQ_PAGE;
    char b[32], mk[16];
    int i;
    v2_ic(cv, V2I_LOOP, 3, 3, 1);
    if (pa == pb)
        snprintf(b, sizeof b, "Bar %d", pa + 1);
    else
        snprintf(b, sizeof b, "Bars %d-%d", pa + 1, pb + 1);
    v2_TC(cv, 76, 4, b, 2, 1);
    for (i = 0; i < 16; i++) {
        const int src = rfmv_seq_src_page(m, clip, i);
        mk[i] = (char)(src == 2 ? 'X' : src == 1 ? 'D' : i > clip_end_page(m, clip) && i <= q->page ? 'G' : 0);
    }
    v2_pages(cv, clip_end_page(m, clip) + 1, pa, pb, q->page, mk);
}

/* (0.12.0, the owner's: "While held it needs to visually communicate with an icon that we are cutting") Delete held:
 * scissors; Copy held: two sheets. 11 x 11, as the v2 icons (bit 10 the left column) */
static const uint16_t IC_CUT[11] = { 0x401, 0x202, 0x104, 0x088, 0x050, 0x020, 0x050, 0x38E, 0x451, 0x451, 0x38E };
static const uint16_t IC_COPY[11] = { 0x7F0, 0x410, 0x410, 0x410, 0x47F, 0x441, 0x7C1, 0x041, 0x041, 0x041, 0x07F };
static void held_icon(mr_canvas_t *cv, const uint16_t ic[11], int x, int y)
{
    int i, j;
    v2_R(cv, x - 1, y - 1, 13, 13, 0);                  /* (clear round it: no box) */
    for (j = 0; j < 11; j++)
        for (i = 0; i < 11; i++)
            if (ic[j] >> (10 - i) & 1)
                v2_px(cv, x + i, y + j, 1);
}
int rfmv_seq_held_icon(const rfm_move_t *m)             /* 1 cutting, 2 copying, 0 none */
{
    const rfm_move_seq_t *q = &m->seq;
    if (m->s.mode != RFM_MOVE_MODE_SEQ || q->projects || q->smenu || (q->mods & (SEQ_M_MUTE | SEQ_M_LOOP | SEQ_M_SHIFT)) ||
        rfmv_seq_trk_held(m))
        return 0;
    return (q->mods & SEQ_M_DELETE) ? 1 : (q->mods & SEQ_M_COPY) ? 2 : 0;
}
void rfmv_seq_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    const int hi = rfmv_seq_held_icon(m);
    int r;
    if (!q->projects && !q->smenu && !q->view && (q->mods & SEQ_M_LOOP) && !(q->mods & SEQ_M_MUTE))
        rfmv_seq_loop_edit(m, cv);
    else if (q->smenu)                                  /* (0.9.0: a Shift menu over the Mixer / Routing too) */
        smenu_screen(m, cv);
    else if (q->projects)
        projects_screen(m, cv);
    else if (q->mods & SEQ_M_MUTE)
        mute_screen(m, cv);
    else if (rfmv_seq_cs_on(m))
        cs_screen(m, cv);                               /* (0.12.0) a select step held: its clip's settings */
    else if (q->view) {
        session_screen(m, cv);
        if (hi)                                         /* (the scene column: the icon in each row: a scene pad there
                                                         * cuts or copies its row) */
            for (r = 0; r < 4; r++)
                held_icon(cv, hi == 1 ? IC_CUT : IC_COPY, RFM_MOVE_SV_COLS * 16 + 3, 4 + r * 15 + 1);
    } else {
        track_screen(m, cv);
        if (hi)                                         /* (the roll's top right) */
            held_icon(cv, hi == 1 ? IC_CUT : IC_COPY, 116, 1);
    }
}

/* Track 4 held (V2Session sess.hold4): the 16 tracks on one row, "Track n" on top; a value changed (the channel, a
 * knob's CC, the rate, the layout, the scale): its edit popup over it for a second, then the tracks again */
void rfmv_seq_track_menu(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_seq_t *q = &m->seq;
    const rfm_move_track_t *t = &q->tr[q->sel_track];
    char l[24], b[24];
    l[0] = 0;
    if (rfmv_now_ms(m) - m->menu_ms < RFM_MOVE_POPUP_MS)
        switch (m->menu_what) {
        case MENU_TCH: snprintf(l, sizeof l, "Channel"), snprintf(b, sizeof b, "%d", t->ch + 1); break;
        case MENU_CC:
            snprintf(l, sizeof l, "Knob %d CC", (m->menu_i & 7) + 1);
            snprintf(b, sizeof b, "%d", m->s.cc[m->s.bank][m->menu_i & 7]);
            break;
        case MENU_RATE: snprintf(l, sizeof l, "Rate"), snprintf(b, sizeof b, "%s", rfm_move_seq_rate_name(t->rate)); break;
        case MENU_LAYOUT: snprintf(l, sizeof l, "Layout"), rfmv_tcase(rfm_move_layout_name(m->s.layout), b, sizeof b); break;
        case MENU_SCALE: snprintf(l, sizeof l, "Scale"), rfmv_tcase(rfm_move_scale_name(m->s.scale), b, sizeof b); break;
        default: break;
        }
    if (l[0]) {
        v2_titleBox(cv, l, b, 64, 124);
        return;
    }
    snprintf(b, sizeof b, "Track %d", q->sel_track + 1);
    tracks_row(m, cv, b);
}

/* ================================================================ (0.6.1) words for a screen reader === */
/* an open Shift menu's name, and item k's words (k -1: the jog's own when it has one; "" none, or one another word
 * says: the tempo, the layout, the key and scale, the clock's message) */
const char *rfmv_seq_smenu_name(int sm)
{
    static const char *const N[] = { "", "Workflow", "Tempo", "Groove", "Layout", "Keys and Scales", "Arp",
                                     "Record mode", "Velocity", "Global settings" };
    return sm >= 0 && sm < (int)(sizeof N / sizeof N[0]) ? N[sm] : "";
}
void rfmv_seq_smenu_words(const rfm_move_t *m, int k, char *o, int cap)
{
    static const char *const AM[RFM_MOVE_AM_N] = { "Off", "On", "First", "Once", "Hold" };
    static const char *const AR[RFM_MOVE_AR_N] = { "Normal", "Note", "Mod pattern", "Transpose pattern" };
    static const char *const AL[RFM_MOVE_AL_N1] = { "Play", "Rest", "Tie", "Repeat", "Lowest down", "Highest up",
                                                    "Power chord", "Chord" };
    static const char *const RM[3] = { "Capture", "Record", "Step" };
    const rfm_move_seq_t *q = &m->seq;
    const rfm_move_track_t *t = &q->tr[q->sel_track];
    const rfm_move_arp_t *a = &t->arp;
    char b[24];
    o[0] = 0;
    switch (q->smenu) {
    case RFM_MOVE_SM_WORKFLOW:
        if (k == 1)
            snprintf(o, (size_t)cap, "Rate: %s", rfm_move_seq_rate_name(t->rate));
        else if (k == 2)
            snprintf(o, (size_t)cap, "Quantize: %d%%", q->qstrength);
        else if (k == 0)
            snprintf(o, (size_t)cap, "Track channel: %d", t->ch + 1);
        break;
    case RFM_MOVE_SM_GROOVE:
        if (k == 0)
            snprintf(o, (size_t)cap, "Groove: %d%%", rfmv_seq_groove_of(m, q->sel_track));
        else if (k == 1)
            snprintf(o, (size_t)cap, "Groove for: %s", t->own & 1 ? "Track" : "Global");
        break;
    case RFM_MOVE_SM_KEYS:
        if (k == 2)
            snprintf(o, (size_t)cap, "Key for: %s", t->own & 2 ? "Track" : "Global");
        break;
    case RFM_MOVE_SM_ARP:
        if (k < 0)
            snprintf(o, (size_t)cap, "%s", q->arp_page == 0 ? "Arp" : q->arp_page == 1 ? "Mod lane" : "Transpose lane");
        else if (q->arp_page) {
            const int L = q->arp_page - 1, s = q->arp_sel & 15, v = a->lane[L][s];
            if (k == 0) {
                if (L)
                    snprintf(o, (size_t)cap, "Step %d: %+d", s + 1, v);
                else if (v >= RFM_MOVE_AL_N1)
                    snprintf(o, (size_t)cap, "Step %d: Note %d", s + 1, v - RFM_MOVE_AL_N1 + 1);
                else
                    snprintf(o, (size_t)cap, "Step %d: %s", s + 1, AL[clampi(v, 0, RFM_MOVE_AL_N1 - 1)]);
            } else if (k == 2)
                snprintf(o, (size_t)cap, "Reset: %s", AR[a->reset % RFM_MOVE_AR_N]);
            else if (k == 1)
                snprintf(o, (size_t)cap, "Length: %d", L ? a->tlen : a->mlen);
        } else
            switch (k) {
            case 0: snprintf(o, (size_t)cap, "Pattern: %s", AP_NAMES[a->pat % RFM_MOVE_AP_N]); break;
            case 1: arp_rate_name(rfmv_seq_arp_rate_of(a->rate), b, sizeof b), snprintf(o, (size_t)cap, "Rate: %s", b); break;
            case 2: snprintf(o, (size_t)cap, "Range: %d %s", a->range, a->range == 1 ? "octave" : "octaves"); break;
            case 3: snprintf(o, (size_t)cap, "Chance: %d%%", a->chance); break;
            case 4: snprintf(o, (size_t)cap, "Arp: %s", AM[a->mode % RFM_MOVE_AM_N]); break;
            case 5: snprintf(o, (size_t)cap, "Gate: %d%%", a->gate); break;
            case 6: snprintf(o, (size_t)cap, "Octave step: %+d", a->odist); break;
            case 7:
                if (a->quant)
                    arp_rate_name(rfmv_seq_arp_rate_of(a->quant - 1), b, sizeof b), snprintf(o, (size_t)cap, "Start on: %s", b);
                else
                    snprintf(o, (size_t)cap, "Start on: any");
                break;
            default: break;
            }
        break;
    case RFM_MOVE_SM_REC:
        if (k == 0)
            snprintf(o, (size_t)cap, "Record mode: %s", RM[q->rec_mode % 3]);
        else if (k == 1)
            snprintf(o, (size_t)cap, "Record style: %s", q->rec_style == RFM_MOVE_RS_REPLACE ? "Replace" : "Overdub");
        break;
    case RFM_MOVE_SM_VEL:
    case RFM_MOVE_SM_GLOBAL:
        if (k == 0 && q->smenu == RFM_MOVE_SM_VEL)
            snprintf(o, (size_t)cap, "Velocity level: %d", m->s.vel_level);
        break;                                          /* (0.6.2: Global settings' list says its own rows) */
    default: break;
    }
}
