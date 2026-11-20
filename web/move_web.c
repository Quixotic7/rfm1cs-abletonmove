/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm_move for the browser (README.md, "The web player"): a flat C API over the portable core, the session
 * (session.h) and the store (store.h), compiled with Emscripten (web/build.sh). web/move-core.js is its only caller.
 * Each instance is one malloc'd block: the session (which owns the core), the store, and what the last call gave,
 * which JS reads after the call:
 *   the actions, at mw_acts(): 10 bytes each: kind, index, down, vel, value (int16 LE), n, bytes[3]; beside them
 *   each action's frame (uint32, mw_act_frames(): the offset in mw_seq_advance's frames; 0 for every other call). A
 *   PROJECT_SAVE (or FW_LIBRARY) action's blob is copied out: value = its index for mw_blob() / mw_blob_len(); and
 *   (0.4.0) each action's new fields, MW_ACT_EXT bytes at mw_act_ext(): track, slot, fx, param, mix[5], then filt[6]
 *   (uint16 LE). FW_* actions name their firmware by value (its index in the list given), FX_LOAD its module by value;
 *   STATE_* by slot (the firmware there); (0.6.0) a SAY action's words are copied out: value = its index for mw_say();
 *   the session's events (mw_sess_* calls), at mw_evs(): 4 int32 each: type (RFM1CS_EV_*, or MW_EV_LOADED), a, b, c:
 *   ACTION a = the action's index; SEND a = the offset in mw_tx(), b = the length; OPEN a = the attempt; LOST a = the
 *   reason, b = was it live; BYE a = the reason, b = was it live; STATUS a = the changed bits; MW_EV_LOADED a = the
 *   slot, b = 0 loaded / -1 refused.
 * The words a host shows (mw_sess_text) and the project file (mw_file_*: store.h's container) need no instance's
 * session state beyond the status.
 * The store's slot I/O goes to JS synchronously (Module.rfmHosts[instance].read / write / remove; a read answers
 * later through mw_store_answer); its LOAD answer goes straight to the session. Nothing here keeps a pointer into JS
 * memory; no filesystem, no sockets (mr_decode comes from mr_client.c, whose socket code the linker leaves out). */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

#include "rfm1cs/move.h"
#include "rfm1cs/session.h"
#include "rfm1cs/store.h"

#define MW_MAX_ACTS 1024
#define MW_ACT_BYTES 10
#define MW_ACT_EXT 21
#define MW_MAX_BLOBS 4                                  /* PROJECT_SAVEs in one call (a project switch makes 2) */
#define MW_MAX_SAYS 16                                  /* (0.6.0) SAY actions' words in one call */
#define MW_MAX_EVS 1024
#define MW_TX_MAX 65536
#define MW_EV_LOADED 100

typedef struct {
    rfm1cs_session_t s;                                 /* (s.core: the core) */
    rfm1cs_store_t st;
    uint32_t now;                                       /* (the store's LOAD answer: the call's time) */
    int nacts;
    uint8_t acts[MW_MAX_ACTS * MW_ACT_BYTES];
    uint32_t frames[MW_MAX_ACTS];
    uint8_t ext[MW_MAX_ACTS * MW_ACT_EXT];
    /* (0.4.0) what the core points into: the catalog and the FX modules, owned here (mw_fw_*, mw_fx_*) */
    struct mw_own *fw_own, *fx_own[2];
    int nblobs;
    uint32_t blob_len[MW_MAX_BLOBS];
    uint8_t blob[MW_MAX_BLOBS][RFM_MOVE_PROJECT_MAX];
    int nsays;
    char say[MW_MAX_SAYS][RFM_MOVE_SAY_MAX];
    char words[RFM_MOVE_SAY_MAX];
    int nevs;
    int32_t evs[MW_MAX_EVS][4];
    uint32_t ntx, tx_over;
    uint8_t tx[MW_TX_MAX];
    rfm_move_led_t leds[RFM_MOVE_NLEDS];
    uint8_t screen[MR_SCREEN_BYTES];
    uint8_t settings[RFM_MOVE_SETTINGS_BYTES];
    int32_t status[40];
    char text[512];
    int text_tone;
} mw_t;

static void add_ev(mw_t *w, int type, int a, int b, int c)
{
    if (w->nevs >= MW_MAX_EVS)
        return;
    w->evs[w->nevs][0] = type, w->evs[w->nevs][1] = a, w->evs[w->nevs][2] = b, w->evs[w->nevs][3] = c;
    w->nevs++;
}
/* an action into the buffer: its index, or -1 */
static int add_act(mw_t *w, const rfm_move_act_t *a)
{
    uint8_t *o;
    int16_t value = a->value;
    if (w->nacts >= MW_MAX_ACTS)
        return -1;
    if (a->kind == RFM_MOVE_ACT_PROJECT_SAVE || a->kind == RFM_MOVE_ACT_FW_LIBRARY) {
        if (w->nblobs >= MW_MAX_BLOBS || !a->data || a->len > RFM_MOVE_PROJECT_MAX)
            return -1;
        memcpy(w->blob[w->nblobs], a->data, a->len);
        w->blob_len[w->nblobs] = a->len;
        value = (int16_t)w->nblobs++;
    }
    if (a->kind == RFM_MOVE_ACT_SAY) {                  /* (0.6.0) the words, copied out */
        if (w->nsays >= MW_MAX_SAYS || !a->data || a->len >= RFM_MOVE_SAY_MAX)
            return -1;
        memcpy(w->say[w->nsays], a->data, a->len), w->say[w->nsays][a->len] = 0;
        value = (int16_t)w->nsays++;
    }
    w->frames[w->nacts] = a->frame;
    o = w->acts + w->nacts * MW_ACT_BYTES;
    o[0] = a->kind, o[1] = a->index, o[2] = a->down, o[3] = a->vel;
    o[4] = (uint8_t)((uint16_t)value & 0xFF), o[5] = (uint8_t)((uint16_t)value >> 8);
    o[6] = a->n, o[7] = a->bytes[0], o[8] = a->bytes[1], o[9] = a->bytes[2];
    o = w->ext + w->nacts * MW_ACT_EXT;
    o[0] = a->track, o[1] = a->slot, o[2] = a->fx, o[3] = a->param;
    memcpy(o + 4, a->mix, 5);
    {
        int k;
        for (k = 0; k < 6; k++)
            o[9 + k * 2] = (uint8_t)(a->filt[k] & 0xFF), o[10 + k * 2] = (uint8_t)(a->filt[k] >> 8);
    }
    return w->nacts++;
}
static void on_event(void *ctx, const rfm1cs_event_t *ev)
{
    mw_t *w = ctx;
    switch (ev->type) {
    case RFM1CS_EV_ACTION: {
        const int i = add_act(w, ev->act);
        if (i >= 0)
            add_ev(w, ev->type, i, 0, 0);
        return;
    }
    case RFM1CS_EV_SEND:
        if (w->ntx + ev->len > MW_TX_MAX) {
            w->tx_over++;
            return;
        }
        memcpy(w->tx + w->ntx, ev->data, ev->len);
        add_ev(w, ev->type, (int)w->ntx, (int)ev->len, 0);
        w->ntx += ev->len;
        return;
    case RFM1CS_EV_OPEN: add_ev(w, ev->type, (int)ev->attempt, 0, 0); return;
    case RFM1CS_EV_LOST: add_ev(w, ev->type, ev->reason, ev->was_live, 0); return;
    case RFM1CS_EV_BYE: add_ev(w, ev->type, ev->reason, ev->was_live, 0); return;
    case RFM1CS_EV_STATUS: add_ev(w, ev->type, (int)ev->changed, 0, 0); return;
    default: add_ev(w, ev->type, 0, 0, 0); return;
    }
}

/* the store's slot I/O: the page's IndexedDB (js/move-link.js) */
EM_JS(int, js_store_read, (void *w, int slot), {
    const h = Module.rfmHosts && Module.rfmHosts[w];
    return h && h.read ? h.read(slot) : 0;
});
EM_JS(int, js_store_write, (void *w, int slot, const uint8_t *b, uint32_t n), {
    const h = Module.rfmHosts && Module.rfmHosts[w];
    return h && h.write ? h.write(slot, HEAPU8.slice(b, b + n)) : -1;
});
EM_JS(int, js_store_remove, (void *w, int slot), {
    const h = Module.rfmHosts && Module.rfmHosts[w];
    return h && h.remove ? h.remove(slot) : 0;
});
static int io_read(void *ctx, int slot, uint8_t *out, uint32_t cap)
{
    (void)out, (void)cap;
    return js_store_read(ctx, slot);
}
static int io_write(void *ctx, int slot, const uint8_t *b, uint32_t n) { return js_store_write(ctx, slot, b, n); }
static int io_remove(void *ctx, int slot) { return js_store_remove(ctx, slot); }
static void io_loaded(void *ctx, int slot, const uint8_t *b, uint32_t n)
{
    mw_t *w = ctx;
    add_ev(w, MW_EV_LOADED, slot, rfm1cs_session_project_answer(&w->s, w->now, slot, b, n), 0);
}

#define BEGIN(w) ((w)->nacts = 0, (w)->nblobs = 0, (w)->nsays = 0, (w)->nevs = 0, (w)->ntx = 0)

/* a new core (in its session): settings (n bytes, an rfm_move_save image) or NULL / not an image: the defaults */
EMSCRIPTEN_KEEPALIVE mw_t *mw_new(const uint8_t *settings, uint32_t n)
{
    mw_t *w = calloc(1, sizeof *w);
    rfm1cs_store_io_t io;
    if (!w)
        return NULL;
    rfm1cs_session_init(&w->s, settings, n, "RemoteFM1 web", on_event, w);
    memset(&io, 0, sizeof io);
    io.ctx = w, io.read = io_read, io.write = io_write, io.remove = io_remove, io.loaded = io_loaded;
    rfm1cs_store_init(&w->st, &io, 0);
    return w;
}
EMSCRIPTEN_KEEPALIVE const char *mw_version(void) { return rfm_move_version(); }
static void own_free(struct mw_own *o);
EMSCRIPTEN_KEEPALIVE void mw_free(mw_t *w)
{
    if (!w)
        return;
    own_free(w->fw_own), own_free(w->fx_own[0]), own_free(w->fx_own[1]);
    free(w);
}

/* the actions of the last call */
EMSCRIPTEN_KEEPALIVE int mw_nacts(const mw_t *w) { return w->nacts; }
EMSCRIPTEN_KEEPALIVE const uint8_t *mw_acts(const mw_t *w) { return w->acts; }
EMSCRIPTEN_KEEPALIVE const uint32_t *mw_act_frames(const mw_t *w) { return w->frames; }
EMSCRIPTEN_KEEPALIVE const uint8_t *mw_act_ext(const mw_t *w) { return w->ext; }
EMSCRIPTEN_KEEPALIVE const uint8_t *mw_blob(const mw_t *w, int i) { return i >= 0 && i < w->nblobs ? w->blob[i] : NULL; }
EMSCRIPTEN_KEEPALIVE uint32_t mw_blob_len(const mw_t *w, int i) { return i >= 0 && i < w->nblobs ? w->blob_len[i] : 0; }

/* one INPUT record: a USB-MIDI packet (4 bytes) and its frame. Returns the number of actions */
EMSCRIPTEN_KEEPALIVE int mw_input(mw_t *w, uint32_t packet, uint32_t frame, uint32_t now_ms)
{
    const uint8_t p[4] = { (uint8_t)packet, (uint8_t)(packet >> 8), (uint8_t)(packet >> 16), (uint8_t)(packet >> 24) };
    mr_event_t ev;
    BEGIN(w);
    mr_decode(p, frame, &ev);
    rfm_move_input(&w->s.core, &ev, now_ms);
    return w->nacts;
}
/* the decoded event alone (tests): type, index, value into out[3] */
EMSCRIPTEN_KEEPALIVE void mw_decode(uint32_t packet, int32_t *out)
{
    const uint8_t p[4] = { (uint8_t)packet, (uint8_t)(packet >> 8), (uint8_t)(packet >> 16), (uint8_t)(packet >> 24) };
    mr_event_t ev;
    mr_decode(p, 0, &ev);
    out[0] = (int32_t)ev.type, out[1] = ev.index, out[2] = ev.value;
}

EMSCRIPTEN_KEEPALIVE int mw_release_all(mw_t *w)
{
    BEGIN(w);
    rfm_move_release_all(&w->s.core);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE int mw_set_mode(mw_t *w, int mode)
{
    BEGIN(w);
    rfm_move_set_mode(&w->s.core, mode);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE int mw_set_scale(mw_t *w, int scale, int root)
{
    BEGIN(w);
    rfm_move_set_scale(&w->s.core, scale, root);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE void mw_set_master(mw_t *w, int v) { rfm_move_set_master(&w->s.core, v); }
EMSCRIPTEN_KEEPALIVE int mw_master(const mw_t *w) { return w->s.core.master; }
EMSCRIPTEN_KEEPALIVE int mw_set_battery(mw_t *w, int battery, int power) { return rfm_move_set_battery(&w->s.core, battery, power); }
EMSCRIPTEN_KEEPALIVE int mw_battery(const mw_t *w) { return w->s.core.battery; }
EMSCRIPTEN_KEEPALIVE int mw_sample_levels(mw_t *w, const uint8_t *peaks, int n) { return rfm_move_sample_levels(&w->s.core, peaks, n); }
EMSCRIPTEN_KEEPALIVE void mw_set_label(mw_t *w, int enc, const char *text) { rfm_move_set_label(&w->s.core, enc, text); }
/* (0.5.11) labels and button names packed: n entries of MW_LBL_ENTRY bytes each, [0] 1 given (0: NULL), [1..17] the
 * text NUL-terminated; buf NULL: clear (labels) / none (button names) */
#define MW_LBL_ENTRY 18
static int unpack(const uint8_t *buf, int n, const char *out[], char txt[][MW_LBL_ENTRY - 1])
{
    int i;
    if (!buf)
        return 0;
    for (i = 0; i < n; i++) {
        const uint8_t *e = buf + i * MW_LBL_ENTRY;
        memcpy(txt[i], e + 1, MW_LBL_ENTRY - 1), txt[i][MW_LBL_ENTRY - 2] = 0;
        out[i] = e[0] ? txt[i] : NULL;
    }
    return 1;
}
EMSCRIPTEN_KEEPALIVE int mw_lbl_entry(void) { return MW_LBL_ENTRY; }
EMSCRIPTEN_KEEPALIVE void mw_set_labels(mw_t *w, const uint8_t *buf)
{
    static const char *l[RFM_MOVE_LBL_N];
    static char t[RFM_MOVE_LBL_N][MW_LBL_ENTRY - 1];
    rfm_move_set_labels(&w->s.core, unpack(buf, RFM_MOVE_LBL_N, l, t) ? l : NULL);
}
EMSCRIPTEN_KEEPALIVE void mw_set_button_names(mw_t *w, const uint8_t *buf)
{
    static const char *l[RFM_MOVE_NBTN];
    static char t[RFM_MOVE_NBTN][MW_LBL_ENTRY - 1];
    rfm_move_set_button_names(&w->s.core, unpack(buf, RFM_MOVE_NBTN, l, t) ? l : NULL);
}
EMSCRIPTEN_KEEPALIVE int mw_readout(mw_t *w, int control, int kind, const char *title, const char *value, int frac,
                                    int flags, int ms)
{
    BEGIN(w);                                           /* (0.6.0: speech's actions) */
    rfm_move_readout(&w->s.core, control, kind, title, value, frac, flags, ms);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE void mw_readouts_clear(mw_t *w) { rfm_move_readouts_clear(&w->s.core); }
EMSCRIPTEN_KEEPALIVE int mw_popup(mw_t *w, const char *text)
{
    BEGIN(w);
    rfm_move_popup(&w->s.core, text);
    return w->nacts;
}
/* (0.6.0) speech: on / off, a SAY action's words (mw_say(i): the action's value), the screen in words now */
EMSCRIPTEN_KEEPALIVE void mw_set_speech(mw_t *w, int on) { rfm_move_set_speech(&w->s.core, on); }
EMSCRIPTEN_KEEPALIVE int mw_speech(const mw_t *w) { return rfm_move_speech(&w->s.core); }
EMSCRIPTEN_KEEPALIVE const char *mw_say(const mw_t *w, int i) { return i >= 0 && i < w->nsays ? w->say[i] : ""; }
EMSCRIPTEN_KEEPALIVE const char *mw_words(mw_t *w)
{
    rfm_move_words(&w->s.core, w->words, sizeof w->words);
    return w->words;
}
/* keymap: 66 int8 (6 rows x 11), or NULL for the FM-1's */
EMSCRIPTEN_KEEPALIVE void mw_set_keymap(mw_t *w, const int8_t *km)
{
    rfm_move_set_keymap(&w->s.core, km ? (const int8_t(*)[11])km : NULL);
}

EMSCRIPTEN_KEEPALIVE void mw_panel(mw_t *w, const uint8_t *levels44, int play_green)
{
    rfm_move_panel(&w->s.core, levels44, (uint8_t)play_green);
}
/* the changed LEDs: 6 bytes each (rgb, cc, number, r, g, b) at mw_leds_buf() */
EMSCRIPTEN_KEEPALIVE int mw_leds(mw_t *w) { return rfm_move_leds(&w->s.core, w->leds, RFM_MOVE_NLEDS); }
EMSCRIPTEN_KEEPALIVE const rfm_move_led_t *mw_leds_buf(const mw_t *w) { return w->leds; }
EMSCRIPTEN_KEEPALIVE void mw_leds_invalidate(mw_t *w) { rfm_move_leds_invalidate(&w->s.core); }

/* the Move's screen (1024 bytes at mw_screen_buf()); rgb565: 240 x 240, or NULL to keep the last */
EMSCRIPTEN_KEEPALIVE const uint8_t *mw_screen(mw_t *w, const uint16_t *rgb565)
{
    rfm_move_screen(&w->s.core, rgb565, w->screen);
    return w->screen;
}

EMSCRIPTEN_KEEPALIVE const uint8_t *mw_save(mw_t *w)
{
    rfm_move_save(&w->s.core.s, w->settings);
    return w->settings;
}
EMSCRIPTEN_KEEPALIVE int mw_settings_bytes(void) { return RFM_MOVE_SETTINGS_BYTES; }

/* the sequencer: the clock (call every audio block, playing or not), the firmware's MIDI clock in, the transport */
EMSCRIPTEN_KEEPALIVE int mw_seq_advance(mw_t *w, uint32_t frames, uint32_t rate)
{
    BEGIN(w);
    rfm_move_seq_advance(&w->s.core, frames, rate);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE int mw_seq_midi_in(mw_t *w, const uint8_t *msg, int n)
{
    BEGIN(w);
    rfm_move_seq_midi_in(&w->s.core, msg, n);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE int mw_seq_play(mw_t *w, int on)
{
    BEGIN(w);
    rfm_move_seq_play(&w->s.core, on);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE void mw_seq_set_bpm(mw_t *w, int bpm10) { rfm_move_seq_set_bpm(&w->s.core, bpm10); }
EMSCRIPTEN_KEEPALIVE void mw_seq_set_lead(mw_t *w, uint32_t us) { rfm_move_seq_set_lead_us(&w->s.core, us); }
EMSCRIPTEN_KEEPALIVE int mw_seq_bpm10(const mw_t *w) { return rfm_move_seq_bpm10(&w->s.core); }
EMSCRIPTEN_KEEPALIVE void mw_seq_set_clock(mw_t *w, int mode) { rfm_move_seq_set_clock(&w->s.core, mode); }
EMSCRIPTEN_KEEPALIVE int mw_seq_following(const mw_t *w) { return rfm_move_seq_following(&w->s.core); }
EMSCRIPTEN_KEEPALIVE int mw_seq_playing(const mw_t *w) { return w->s.core.seq.playing; }
EMSCRIPTEN_KEEPALIVE int mw_screen_timed(const mw_t *w) { return rfm_move_screen_timed(&w->s.core); }

/* projects: the host's store answers PROJECT_LOAD with mw_project_load; slot used bits; a blob for export */
EMSCRIPTEN_KEEPALIVE int mw_project_load(mw_t *w, int slot, const uint8_t *b, uint32_t n)
{
    int r;
    BEGIN(w);
    r = rfm_move_project_load(&w->s.core, slot, b, n);
    return r < 0 ? r : w->nacts;
}
EMSCRIPTEN_KEEPALIVE int mw_project_check(const uint8_t *b, uint32_t n) { return rfm_move_project_check(b, n); }
EMSCRIPTEN_KEEPALIVE void mw_project_dir(mw_t *w, uint32_t used) { rfm_move_project_dir(&w->s.core, used); }
/* a stored project's content for the grid's bars and its popup (rfm_move_project_info) */
EMSCRIPTEN_KEEPALIVE int mw_project_info(mw_t *w, int slot, const uint8_t *b, uint32_t n) { return rfm_move_project_info(&w->s.core, slot, b, n); }
EMSCRIPTEN_KEEPALIVE int mw_project_flush(mw_t *w)
{
    BEGIN(w);
    rfm_move_project_flush(&w->s.core);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE int mw_project_slot(const mw_t *w) { return rfm_move_project_slot(&w->s.core); }
/* the current project as a blob (export) into mw_blob(w, 0); its length, or -1 */
EMSCRIPTEN_KEEPALIVE int mw_project_save(mw_t *w)
{
    const int n = rfm_move_project_save(&w->s.core, w->blob[0], RFM_MOVE_PROJECT_MAX);
    w->nblobs = n > 0, w->blob_len[0] = n > 0 ? (uint32_t)n : 0;
    return n;
}
EMSCRIPTEN_KEEPALIVE int mw_project_max(void) { return RFM_MOVE_PROJECT_MAX; }

/* the state the page shows: mode, layout, channel, octave, scale, root, bank, track_held */
EMSCRIPTEN_KEEPALIVE void mw_state(const mw_t *w, int32_t *out)
{
    out[0] = w->s.core.s.mode, out[1] = w->s.core.s.layout, out[2] = w->s.core.s.channel, out[3] = w->s.core.s.octave;
    out[4] = w->s.core.s.scale, out[5] = w->s.core.s.root, out[6] = w->s.core.s.bank, out[7] = w->s.core.track_held;
}

/* the mapping (tests, help) */
EMSCRIPTEN_KEEPALIVE int mw_pad_key(int pad) { return rfm_move_pad_key(pad); }
EMSCRIPTEN_KEEPALIVE int mw_step_key(int step) { return rfm_move_step_key(step); }
EMSCRIPTEN_KEEPALIVE int mw_button_of(int cc) { return rfm_move_button_of(cc); }
EMSCRIPTEN_KEEPALIVE int mw_knob_enc(int knob) { return rfm_move_knob_enc(knob); }
EMSCRIPTEN_KEEPALIVE int mw_pad_note(const mw_t *w, int pad) { return rfm_move_pad_note(&w->s.core.s, pad); }

/* ---- the session (session.h): each call returns how many events it gave (mw_evs, mw_tx, mw_acts) */
EMSCRIPTEN_KEEPALIVE const int32_t *mw_evs(const mw_t *w) { return &w->evs[0][0]; }
EMSCRIPTEN_KEEPALIVE const uint8_t *mw_tx(const mw_t *w) { return w->tx; }
EMSCRIPTEN_KEEPALIVE void mw_sess_host_name(mw_t *w, const char *name)
{
    strncpy(w->s.host_name, name ? name : "", sizeof w->s.host_name - 1);
}
#define SESS(call) (BEGIN(w), w->now = now, call, w->nevs)
EMSCRIPTEN_KEEPALIVE int mw_sess_start(mw_t *w, uint32_t now) { return SESS(rfm1cs_session_start(&w->s, now)); }
EMSCRIPTEN_KEEPALIVE int mw_sess_stop(mw_t *w, uint32_t now) { return SESS(rfm1cs_session_stop(&w->s)); }
EMSCRIPTEN_KEEPALIVE void mw_sess_wait(mw_t *w, uint32_t now, uint32_t ms, int patient) { rfm1cs_session_wait(&w->s, now, ms, patient); }
EMSCRIPTEN_KEEPALIVE void mw_sess_give_up(mw_t *w, int tries) { rfm1cs_session_give_up(&w->s, tries); }
EMSCRIPTEN_KEEPALIVE int mw_sess_opened(mw_t *w, uint32_t now, uint32_t attempt) { return SESS(rfm1cs_session_opened(&w->s, now, attempt)); }
EMSCRIPTEN_KEEPALIVE int mw_sess_rx(mw_t *w, uint32_t now, uint32_t attempt, const uint8_t *b, uint32_t n)
{
    return SESS(rfm1cs_session_rx(&w->s, now, attempt, b, n));
}
EMSCRIPTEN_KEEPALIVE int mw_sess_closed(mw_t *w, uint32_t now, uint32_t attempt, int final)
{
    return SESS(rfm1cs_session_closed(&w->s, now, attempt, final));
}
EMSCRIPTEN_KEEPALIVE uint32_t mw_sess_attempt(const mw_t *w) { return rfm1cs_session_attempt(&w->s); }
EMSCRIPTEN_KEEPALIVE int mw_sess_tick(mw_t *w, uint32_t now) { return SESS(rfm1cs_session_tick(&w->s, now)); }
EMSCRIPTEN_KEEPALIVE int mw_sess_next_ms(const mw_t *w, uint32_t now) { return rfm1cs_session_next_ms(&w->s, now); }
EMSCRIPTEN_KEEPALIVE int mw_sess_panel(mw_t *w, uint32_t now, const uint8_t *levels44, int play_green)
{
    return SESS(rfm1cs_session_panel(&w->s, levels44, (uint8_t)play_green));
}
EMSCRIPTEN_KEEPALIVE int mw_sess_frame(mw_t *w, uint32_t now, const uint16_t *rgb565) { return SESS(rfm1cs_session_frame(&w->s, now, rgb565)); }
EMSCRIPTEN_KEEPALIVE int mw_sess_redraw(mw_t *w, uint32_t now) { return SESS(rfm1cs_session_redraw(&w->s, now)); }
EMSCRIPTEN_KEEPALIVE int mw_sess_flush_leds(mw_t *w, uint32_t now) { return SESS(rfm1cs_session_flush_leds(&w->s)); }
EMSCRIPTEN_KEEPALIVE int mw_sess_update(mw_t *w, uint32_t now) { return SESS(rfm1cs_session_update(&w->s)); }
EMSCRIPTEN_KEEPALIVE int mw_sess_pumped(mw_t *w, uint32_t now) { return SESS(rfm1cs_session_pumped(&w->s, now)); }
EMSCRIPTEN_KEEPALIVE int mw_sess_flush(mw_t *w, uint32_t now) { return SESS(rfm1cs_session_flush(&w->s)); }
EMSCRIPTEN_KEEPALIVE int mw_sess_release(mw_t *w, uint32_t now) { return SESS(rfm1cs_session_release(&w->s)); }
/* at start: the used slots and the one to open; *res: 0, or -1 unreadable (it starts empty) */
EMSCRIPTEN_KEEPALIVE int mw_sess_projects_init(mw_t *w, uint32_t now, uint32_t bits, int slot, const uint8_t *b, uint32_t n, int32_t *res)
{
    return SESS(*res = rfm1cs_session_projects_init(&w->s, now, bits, slot, b, n));
}
EMSCRIPTEN_KEEPALIVE int mw_sess_project_answer(mw_t *w, uint32_t now, int slot, const uint8_t *b, uint32_t n, int32_t *res)
{
    return SESS(*res = rfm1cs_session_project_answer(&w->s, now, slot, b, n));
}
/* the status: state, ever_live, waiting, retries, retry_ms, via, proto, caps, battery, power, jacks, led_pending, mode,
 * project, playing, following, bpm10, connects, inputs, led_writes, led_msgs, screens, pings, pongs, sent, midi,
 * live, tx_over, lost_why, bye, gone_live, attempt */
EMSCRIPTEN_KEEPALIVE const int32_t *mw_sess_status(mw_t *w)
{
    const rfm1cs_status_t *st = &w->s.st;
    int32_t *o = w->status;
    o[0] = st->state, o[1] = st->ever_live, o[2] = st->waiting, o[3] = st->retries, o[4] = (int32_t)st->retry_ms;
    o[5] = st->via, o[6] = st->proto, o[7] = st->caps, o[8] = st->battery, o[9] = st->power, o[10] = st->jacks;
    o[11] = st->led_pending, o[12] = st->mode, o[13] = st->project, o[14] = st->playing, o[15] = st->following;
    o[16] = st->bpm10, o[17] = (int32_t)st->connects, o[18] = (int32_t)st->inputs, o[19] = (int32_t)st->led_writes;
    o[20] = (int32_t)st->led_msgs, o[21] = (int32_t)st->screens, o[22] = (int32_t)st->pings, o[23] = (int32_t)st->pongs;
    o[24] = (int32_t)st->sent, o[25] = (int32_t)st->midi, o[26] = w->s.live, o[27] = (int32_t)w->tx_over;
    o[28] = st->lost_why, o[29] = st->bye, o[30] = st->gone_live, o[31] = (int32_t)w->s.attempt;
    return o;
}

/* the words to show (session.h): what 0 the status line, 1 why (a), 2 the LOST toast (a why, b was live), 3 the BYE's
 * words (a reason, b was live); the text at the result, its tone at mw_text_tone() (0 off, 1 busy, 2 on, 3 bad) */
EMSCRIPTEN_KEEPALIVE const char *mw_sess_text(mw_t *w, int what, int a, int b, const char *host, const char *closed,
                                              const char *link, const char *self)
{
    const rfm1cs_words_t words = { host, closed, link, self };
    w->text_tone = 0;
    switch (what) {
    case 0: w->text_tone = rfm1cs_status_text(&w->s.st, &words, w->text, sizeof w->text); break;
    case 1: rfm1cs_why_text(a, &words, w->text, sizeof w->text); break;
    case 2: rfm1cs_lost_text(a, b, &w->s.st, &words, w->text, sizeof w->text); break;
    case 3: rfm1cs_bye_text(a, b, &w->s.st, w->text, sizeof w->text, &w->text_tone); break;
    default: w->text[0] = 0; break;
    }
    return w->text;
}
EMSCRIPTEN_KEEPALIVE int mw_text_tone(const mw_t *w) { return w->text_tone; }

/* ---- the project file (store.h): one at a time (the results stay until the next call) */
static struct {
    int32_t res[1 + RFM1CS_SLOTS * 4];                  /* kind, then slot, saved, offset, len per entry */
    rfm1cs_file_error_t err;
    char text[200];
} g_file;
/* a file read and checked: the count of entries (mw_file_res()), or < 0 (mw_file_error()) */
EMSCRIPTEN_KEEPALIVE int mw_file_unpack(const uint8_t *b, uint32_t n)
{
    rfm1cs_file_entry_t e[RFM1CS_SLOTS];
    int kind = 0, i;
    const int r = rfm1cs_file_unpack(b, n, &kind, e, &g_file.err);
    g_file.res[0] = kind;
    for (i = 0; i < r; i++) {
        int32_t *o = g_file.res + 1 + i * 4;
        o[0] = e[i].slot, o[1] = (int32_t)e[i].saved, o[2] = (int32_t)(e[i].bytes - b), o[3] = (int32_t)e[i].len;
    }
    return r;
}
EMSCRIPTEN_KEEPALIVE const int32_t *mw_file_res(void) { return g_file.res; }
EMSCRIPTEN_KEEPALIVE const char *mw_file_error(const char *self)
{
    rfm1cs_file_error_text(&g_file.err, self, g_file.text, sizeof g_file.text);
    return g_file.text;
}
/* n entries (meta: slot, saved, len each; their blobs one after another) into out: its length, 0 too small */
EMSCRIPTEN_KEEPALIVE uint32_t mw_file_pack(const int32_t *meta, const uint8_t *blobs, int n, int kind, uint8_t *out, uint32_t cap)
{
    rfm1cs_file_entry_t e[RFM1CS_SLOTS * 2];
    uint32_t off = 0;
    int i;
    if (n < 0 || n > RFM1CS_SLOTS * 2)
        return 0;
    for (i = 0; i < n; i++) {
        e[i].slot = meta[i * 3], e[i].saved = (uint32_t)meta[i * 3 + 1], e[i].len = (uint32_t)meta[i * 3 + 2];
        e[i].bytes = blobs + off;
        off += e[i].len;
    }
    return rfm1cs_file_pack(e, n, kind, out, cap);
}
EMSCRIPTEN_KEEPALIVE uint32_t mw_file_max(void) { return RFM1CS_FILE_MAX; }
EMSCRIPTEN_KEEPALIVE const char *mw_sess_name(const mw_t *w) { return w->s.st.name; }
/* the last screen sent (1024 bytes), or 0 */
EMSCRIPTEN_KEEPALIVE const uint8_t *mw_sess_shown(const mw_t *w) { return w->s.shown_ok ? w->s.shown : NULL; }

/* ---- the store (store.h): the core's project actions as slot I/O through Module.rfmHosts[w] */
EMSCRIPTEN_KEEPALIVE void mw_store_init(mw_t *w, uint32_t used) { w->st.used = used; }
EMSCRIPTEN_KEEPALIVE int mw_store_request(mw_t *w, uint32_t now, int kind, int slot, int value, const uint8_t *b, uint32_t n)
{
    return SESS(rfm1cs_store_request(&w->st, kind, slot, value, b, n));
}
EMSCRIPTEN_KEEPALIVE int mw_store_answer(mw_t *w, uint32_t now, int slot, const uint8_t *b, uint32_t n)
{
    return SESS(rfm1cs_store_answer(&w->st, slot, b, n));
}
EMSCRIPTEN_KEEPALIVE int mw_store_pending(const mw_t *w) { return w->st.qn; }

/* ---- (0.4.0) the firmware browser, the library, FX setup (move.h): the catalog and the modules are built here in
 * wasm memory and handed to the core (its contract: they stay unchanged until the next list; the old one is freed
 * after the core has the new). Calls that can give actions return their count. */
struct mw_own {                                         /* everything one list allocated */
    void **p;
    int n, cap;
    void *list;                                         /* the array the core keeps */
    int count;
};
static void own_free(struct mw_own *o)
{
    int i;
    if (!o)
        return;
    for (i = 0; i < o->n; i++)
        free(o->p[i]);
    free(o->p), free(o->list), free(o);
}
static void *own_keep(struct mw_own *o, void *x)
{
    if (!x)
        return NULL;
    if (o->n == o->cap) {
        void **q = realloc(o->p, (size_t)(o->cap ? o->cap * 2 : 64) * sizeof *q);
        if (!q) {
            free(x);
            return NULL;
        }
        o->p = q, o->cap = o->cap ? o->cap * 2 : 64;
    }
    return o->p[o->n++] = x;
}
static const char *own_str(struct mw_own *o, const char *s)
{
    char *c;
    if (!s)
        return NULL;
    c = malloc(strlen(s) + 1);
    if (c)
        strcpy(c, s);
    return own_keep(o, c);
}
static struct mw_own *g_fw_new, *g_fx_new[2];          /* (being built) */

/* a new catalog of n entries: mw_fw_entry for each, then mw_fw_commit */
EMSCRIPTEN_KEEPALIVE int mw_fw_begin(mw_t *w, int n)
{
    (void)w;
    own_free(g_fw_new);
    g_fw_new = calloc(1, sizeof *g_fw_new);
    if (!g_fw_new || n < 0)
        return -1;
    g_fw_new->list = calloc((size_t)(n ? n : 1), sizeof(rfm_move_fw_t));
    g_fw_new->count = n;
    return g_fw_new->list ? 0 : -1;
}
/* entry i: strings (installed / desc may be NULL), thumb 512 bytes or NULL, the category bits, the main one 0..4 */
EMSCRIPTEN_KEEPALIVE int mw_fw_entry(int i, const char *id, const char *name, const char *version, const char *installed,
                                     const char *desc, const uint8_t *thumb, int cats, int cat, int virt)
{
    rfm_move_fw_t *e;
    if (!g_fw_new || i < 0 || i >= g_fw_new->count)
        return -1;
    e = (rfm_move_fw_t *)g_fw_new->list + i;
    e->id = own_str(g_fw_new, id), e->name = own_str(g_fw_new, name), e->version = own_str(g_fw_new, version);
    e->installed = installed && installed[0] ? own_str(g_fw_new, installed) : NULL;
    e->desc = own_str(g_fw_new, desc);
    if (thumb) {
        uint8_t *t = malloc(RFM_MOVE_FW_THUMB_BYTES);
        if (t)
            memcpy(t, thumb, RFM_MOVE_FW_THUMB_BYTES);
        e->thumb = own_keep(g_fw_new, t);
    }
    e->cats = (uint8_t)cats, e->cat = (uint8_t)cat, e->virt = (uint8_t)(virt != 0);   /* (0.5.11: virt) */
    return 0;
}
EMSCRIPTEN_KEEPALIVE int mw_fw_commit(mw_t *w)
{
    struct mw_own *old = w->fw_own;
    if (!g_fw_new)
        return -1;
    BEGIN(w);
    w->fw_own = g_fw_new, g_fw_new = NULL;
    rfm_move_fw_list(&w->s.core, w->fw_own->list, w->fw_own->count);
    own_free(old);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE void mw_fw_installed(mw_t *w, int i, const char *version)
{
    if (!w->fw_own || i < 0 || i >= w->fw_own->count)
        return;
    rfm_move_fw_installed(&w->s.core, i, version && version[0] ? own_str(w->fw_own, version) : NULL);
}
EMSCRIPTEN_KEEPALIVE int mw_fw_slots(mw_t *w, const char *a, const char *b, const char *c)
{
    const char *const ids[3] = { a, b, c };
    BEGIN(w);
    rfm_move_fw_slots(&w->s.core, ids);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE int mw_fw_progress(mw_t *w, const char *id, int pct)
{
    BEGIN(w);
    rfm_move_fw_progress(&w->s.core, id, pct);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE int mw_fw_library(mw_t *w, const uint8_t *b, uint32_t n) { return rfm_move_fw_library(&w->s.core, b, n); }
/* the library now into mw_blob(w, 0): its length */
EMSCRIPTEN_KEEPALIVE uint32_t mw_fw_library_save(mw_t *w)
{
    const uint32_t n = rfm_move_fw_library_save(&w->s.core, w->blob[0], RFM_MOVE_PROJECT_MAX);
    w->nblobs = n > 0, w->blob_len[0] = n;
    return n;
}
EMSCRIPTEN_KEEPALIVE int mw_fw_flush(mw_t *w)
{
    BEGIN(w);
    rfm_move_fw_flush(&w->s.core);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE void mw_fw_voices(mw_t *w, int n) { rfm_move_fw_voices(&w->s.core, n); }
/* (0.5.11) a slot's level from the page (FW_LEVEL, the library): -1 refused, else the actions */
EMSCRIPTEN_KEEPALIVE int mw_fw_set_level(mw_t *w, int slot, int level)
{
    int r;
    BEGIN(w);
    r = rfm_move_fw_set_level(&w->s.core, slot, level);
    return r < 0 ? r : w->nacts;
}
EMSCRIPTEN_KEEPALIVE void mw_fw_caps(mw_t *w, int caps) { rfm_move_fw_caps(&w->s.core, caps); }   /* (0.5.10) */
EMSCRIPTEN_KEEPALIVE int mw_fw_caps_get(const mw_t *w) { return rfm_move_fw_caps_get(&w->s.core); }
EMSCRIPTEN_KEEPALIVE int mw_fw_sounding(const mw_t *w) { return rfm_move_fw_sounding(&w->s.core); }
EMSCRIPTEN_KEEPALIVE int mw_fw_active(const mw_t *w) { return rfm_move_fw_active(&w->s.core); }
EMSCRIPTEN_KEEPALIVE int mw_fw_level(const mw_t *w, int slot) { return rfm_move_fw_level(&w->s.core, slot); }
EMSCRIPTEN_KEEPALIVE int mw_fw_states(mw_t *w, int slot, uint32_t used)
{
    BEGIN(w);
    rfm_move_fw_states(&w->s.core, slot, used);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE int mw_fw_lib_bytes(void) { return RFM_MOVE_FW_LIB_BYTES; }

/* the FX modules of a kind: mw_fx_begin(kind, n), mw_fx_module, mw_fx_param (and mw_fx_word), mw_fx_commit */
EMSCRIPTEN_KEEPALIVE int mw_fx_begin(int kind, int n)
{
    if (kind < 0 || kind > 1 || n < 0)
        return -1;
    own_free(g_fx_new[kind]);
    g_fx_new[kind] = calloc(1, sizeof *g_fx_new[kind]);
    if (!g_fx_new[kind])
        return -1;
    g_fx_new[kind]->list = calloc((size_t)(n ? n : 1), sizeof(rfm_move_fx_module_t));
    g_fx_new[kind]->count = n;
    return g_fx_new[kind]->list ? 0 : -1;
}
EMSCRIPTEN_KEEPALIVE int mw_fx_module(int kind, int i, const char *name, int nparams)
{
    struct mw_own *o = kind >= 0 && kind < 2 ? g_fx_new[kind] : NULL;
    rfm_move_fx_module_t *md;
    if (!o || i < 0 || i >= o->count || nparams < 0)
        return -1;
    md = (rfm_move_fx_module_t *)o->list + i;
    md->name = own_str(o, name);
    md->params = own_keep(o, calloc((size_t)(nparams ? nparams : 1), sizeof(rfm_move_fx_param_t)));
    md->nparams = md->params ? nparams : 0;
    return md->params ? 0 : -1;
}
EMSCRIPTEN_KEEPALIVE int mw_fx_param(int kind, int i, int k, const char *name, int nwords, int def, int dry_wet)
{
    struct mw_own *o = kind >= 0 && kind < 2 ? g_fx_new[kind] : NULL;
    rfm_move_fx_param_t *p;
    if (!o || i < 0 || i >= o->count || k < 0 || k >= ((rfm_move_fx_module_t *)o->list)[i].nparams)
        return -1;
    p = (rfm_move_fx_param_t *)((rfm_move_fx_module_t *)o->list)[i].params + k;
    p->name = own_str(o, name), p->def = (uint8_t)def, p->dry_wet = (uint8_t)(dry_wet != 0);
    p->words = nwords > 0 ? own_keep(o, calloc((size_t)nwords, sizeof(char *))) : NULL;
    p->nwords = p->words ? nwords : 0;
    return 0;
}
EMSCRIPTEN_KEEPALIVE int mw_fx_word(int kind, int i, int k, int j, const char *word)
{
    struct mw_own *o = kind >= 0 && kind < 2 ? g_fx_new[kind] : NULL;
    rfm_move_fx_param_t *p;
    if (!o || i < 0 || i >= o->count || k < 0 || k >= ((rfm_move_fx_module_t *)o->list)[i].nparams)
        return -1;
    p = (rfm_move_fx_param_t *)((rfm_move_fx_module_t *)o->list)[i].params + k;
    if (j < 0 || j >= p->nwords)
        return -1;
    ((const char **)p->words)[j] = own_str(o, word);
    return 0;
}
EMSCRIPTEN_KEEPALIVE int mw_fx_commit(mw_t *w, int kind)
{
    struct mw_own *old;
    if (kind < 0 || kind > 1 || !g_fx_new[kind])
        return -1;
    old = w->fx_own[kind];
    w->fx_own[kind] = g_fx_new[kind], g_fx_new[kind] = NULL;
    rfm_move_fx_modules(&w->s.core, kind, w->fx_own[kind]->list, w->fx_own[kind]->count);
    own_free(old);
    return 0;
}
EMSCRIPTEN_KEEPALIVE int mw_fx_refused(mw_t *w, int kind, int slot)
{
    BEGIN(w);
    rfm_move_fx_refused(&w->s.core, kind, slot);
    return w->nacts;
}
EMSCRIPTEN_KEEPALIVE int mw_fx_sync(mw_t *w)
{
    BEGIN(w);
    rfm_move_fx_sync(&w->s.core);
    return w->nacts;
}
