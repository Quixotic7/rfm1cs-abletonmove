/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm1cs_session (session.h): the Move Remote link, transport-free. Ported from the web player's MoveLink
 * (RemoteFM1 web/site/js/move-link.js, a3946f6), whose timing and order it keeps: HELLO -> the Move's HELLO -> CONFIG
 * audio clock off, every LED written again, the screen; PING every second, 3 s of silence lost; BYE; retries after
 * 1, 2, 4, 4 .. s; the project flushed and everything let go when the link goes. */
#include "rfm1cs/session.h"

#include <string.h>

#include "moveremote/proto.h"

const uint32_t rfm1cs_retry_ms[RFM1CS_NRETRY] = { 1000, 2000, 4000 };

static inline int due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

static void emit(rfm1cs_session_t *s, rfm1cs_event_t *ev)
{
    if (s->fn)
        s->fn(s->ctx, ev);
}
static void emit_simple(rfm1cs_session_t *s, int type)
{
    rfm1cs_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    emit(s, &ev);
}
static void status_changed(rfm1cs_session_t *s, uint32_t bits)
{
    rfm1cs_event_t ev;
    if (!bits)
        return;
    memset(&ev, 0, sizeof ev);
    ev.type = RFM1CS_EV_STATUS;
    ev.changed = bits;
    emit(s, &ev);
}
static void set_state(rfm1cs_session_t *s, int state, uint32_t more)
{
    if (s->st.state != state)
        more |= RFM1CS_CH_STATE;
    s->st.state = state;
    status_changed(s, more);
}

uint32_t rfm1cs_frame(uint8_t *out, int type, const uint8_t *payload, uint32_t n)
{
    const uint32_t len = n + 1;
    out[0] = (uint8_t)len, out[1] = (uint8_t)(len >> 8), out[2] = (uint8_t)type;
    if (n && payload != out + 3)
        memcpy(out + 3, payload, n);
    return n + 3;
}
/* the message already built at s->tx + 3 (n payload bytes) out */
static void send_tx(rfm1cs_session_t *s, int type, uint32_t n)
{
    rfm1cs_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.type = RFM1CS_EV_SEND;
    ev.data = s->tx;
    ev.len = rfm1cs_frame(s->tx, type, s->tx + 3, n);
    s->st.sent++;
    emit(s, &ev);
}

int rfm1cs_parse_hello(const uint8_t *p, uint32_t n, rfm1cs_status_t *st)
{
    int i;
    if (n < 46 || memcmp(p, "MOVR", 4) != 0)
        return -1;
    st->proto = rd16(p + 4);
    st->caps = rd16(p + 6);
    st->via = p[16];
    for (i = 0; i < 24 && p[22 + i]; i++)
        st->name[i] = (char)p[22 + i];
    st->name[i] = 0;
    return 0;
}

/* ---- the core's actions out */
static void on_core_act(void *ctx, const rfm_move_act_t *a)
{
    rfm1cs_session_t *s = ctx;
    rfm1cs_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.type = RFM1CS_EV_ACTION;
    ev.act = a;
    ev.bpm10 = rfm_move_seq_bpm10(&s->core);
    if (a->kind == RFM_MOVE_ACT_SETTINGS) {
        ev.len = rfm_move_save(&s->core.s, s->settings);
        ev.data = s->settings;
    } else if (a->kind == RFM_MOVE_ACT_PROJECT_SAVE) {
        ev.data = a->data;
        ev.len = a->len;
    } else if (a->kind == RFM_MOVE_ACT_MIDI) {
        s->st.midi++;
    }
    emit(s, &ev);
}

/* the core's state into the status; a STATUS event for what changed (the web player's afterSeq) */
static void after(rfm1cs_session_t *s)
{
    rfm1cs_status_t *st = &s->st;
    uint32_t ch = 0;
    const int playing = s->core.seq.playing != 0, project = rfm_move_project_slot(&s->core);
    const int following = rfm_move_seq_following(&s->core) != 0, bpm10 = rfm_move_seq_bpm10(&s->core);
    const int mode = s->core.s.mode;
    if (playing != st->playing)
        st->playing = playing, ch |= RFM1CS_CH_PLAYING;
    if (project != st->project)
        st->project = project, ch |= RFM1CS_CH_PROJECT;
    if (following != st->following || bpm10 != st->bpm10)
        st->following = following, st->bpm10 = bpm10, ch |= RFM1CS_CH_CLOCK;
    if (mode != st->mode)
        st->mode = mode, ch |= RFM1CS_CH_MODE;
    status_changed(s, ch);
}

void rfm1cs_session_init(rfm1cs_session_t *s, const uint8_t *settings, uint32_t n, const char *host_name,
                         rfm1cs_event_fn fn, void *ctx)
{
    rfm_move_settings_t set;
    memset(s, 0, sizeof *s);
    if (!settings || rfm_move_load(&set, settings, n) != 0)
        rfm_move_settings_default(&set);
    rfm_move_init(&s->core, &set, on_core_act, s);
    s->fn = fn;
    s->ctx = ctx;
    strncpy(s->host_name, host_name ? host_name : "", sizeof s->host_name - 1);
    s->give_up = -1;
    s->st.state = RFM1CS_OFF;
    s->st.via = -1;
    s->st.battery = -1;
    s->st.mode = s->core.s.mode;
    s->st.project = rfm_move_project_slot(&s->core);
    s->st.playing = s->core.seq.playing != 0;
    s->st.following = rfm_move_seq_following(&s->core) != 0;
    s->st.bpm10 = rfm_move_seq_bpm10(&s->core);
}

/* ---- the Move's LEDs and screen */
void rfm1cs_session_flush_leds(rfm1cs_session_t *s)
{
    int pass, i, n, k;
    if (!s->live)
        return;
    for (pass = 0; pass < 4; pass++) {               /* (rfm_move_leds gives every due one: one pass is enough) */
        n = rfm_move_leds(&s->core, s->leds, RFM_MOVE_NLEDS);
        if (n <= 0)
            return;
        s->st.led_writes += (uint32_t)n;
        for (k = 0, i = 0; i < n; i++)               /* the RGB ones in one LED_RGB */
            if (s->leds[i].rgb) {
                uint8_t *o = s->tx + 3 + k * 5;
                o[0] = s->leds[i].cc ? 1 : 0, o[1] = s->leds[i].number & 127;
                o[2] = s->leds[i].r, o[3] = s->leds[i].g, o[4] = s->leds[i].b;
                k++;
            }
        if (k) {
            s->st.led_msgs++;
            send_tx(s, MR_H_LED_RGB, (uint32_t)k * 5);
        }
        for (k = 0, i = 0; i < n; i++)               /* the palette ones in one LED */
            if (!s->leds[i].rgb) {
                uint8_t *o = s->tx + 3 + k * 3;
                o[0] = s->leds[i].cc ? 0xB0 : 0x90, o[1] = s->leds[i].number & 127, o[2] = s->leds[i].r & 127;
                k++;
            }
        if (k) {
            s->st.led_msgs++;
            send_tx(s, MR_H_LED, (uint32_t)k * 3);
        }
    }
}

static void draw(rfm1cs_session_t *s, uint32_t now)
{
    if (!s->live || !s->dirty)
        return;
    s->dirty = 0;
    s->shot_at = now, s->shot_ok = 1;
    rfm_move_screen(&s->core, NULL, s->scr);
    if (s->shown_ok && memcmp(s->shown, s->scr, MR_SCREEN_BYTES) == 0)
        return;
    memcpy(s->shown, s->scr, MR_SCREEN_BYTES);
    s->shown_ok = 1;
    s->st.screens++;
    memcpy(s->tx + 3, s->scr, MR_SCREEN_BYTES);
    send_tx(s, MR_H_DISPLAY, MR_SCREEN_BYTES);
}
/* the screen, at most every RFM1CS_SCREEN_MS, only when it changed */
static void screen_soon(rfm1cs_session_t *s, uint32_t now)
{
    if (!s->live)
        return;
    s->dirty = 1;
    if (s->screen_due)
        return;
    if (!s->shot_ok || (int32_t)(now - s->shot_at) >= (int32_t)RFM1CS_SCREEN_MS)
        draw(s, now);
    else
        s->screen_due = 1;
}
void rfm1cs_session_redraw(rfm1cs_session_t *s, uint32_t now) { screen_soon(s, now); }

void rfm1cs_session_panel(rfm1cs_session_t *s, const uint8_t levels[RFM_MOVE_NLEVELS], uint8_t play_green)
{
    rfm_move_panel(&s->core, levels, play_green);
    rfm1cs_session_flush_leds(s);
}
void rfm1cs_session_frame(rfm1cs_session_t *s, uint32_t now, const uint16_t *rgb565)
{
    if (rgb565)
        rfm_move_thumb(&s->core, rgb565);
    screen_soon(s, now);
}

/* ---- the link */
void rfm1cs_session_release(rfm1cs_session_t *s)
{
    rfm_move_release_all(&s->core);
    s->st.playing = s->core.seq.playing != 0;     /* (a stop by the release: RELEASED says it, not PLAYING) */
    emit_simple(s, RFM1CS_EV_RELEASED);
}
void rfm1cs_session_flush(rfm1cs_session_t *s) { rfm_move_project_flush(&s->core); }

/* the transport and timers gone, everything held let go (the project saved first, if it changed) */
static void teardown(rfm1cs_session_t *s)
{
    const int had = s->opened || s->hello_due || s->live || s->st.waiting || s->st.state == RFM1CS_CONNECTING;
    rfm1cs_session_flush(s);
    s->hello_due = s->retry_due = s->screen_due = 0;
    s->live = 0;
    s->opened = 0;
    s->st.waiting = 0;
    s->rx_len = s->rx_skip = 0;
    if (had)
        emit_simple(s, RFM1CS_EV_CLOSE);
    rfm1cs_session_release(s);
}

static void begin_try(rfm1cs_session_t *s, uint32_t now)
{
    s->opened = 0;
    s->patient = 0;
    s->st.waiting = 0;
    s->rx_len = s->rx_skip = 0;
    s->hello_at = now + RFM1CS_HELLO_MS;
    s->hello_due = 1;
    set_state(s, RFM1CS_CONNECTING, 0);
    if (s->want && s->st.state == RFM1CS_CONNECTING)
        emit_simple(s, RFM1CS_EV_OPEN);
}

void rfm1cs_session_start(rfm1cs_session_t *s, uint32_t now)
{
    if (s->want || s->live || s->opened || s->hello_due || s->retry_due)
        teardown(s);
    s->want = 1;
    s->st.ever_live = 0;
    s->st.retries = 0;
    s->st.retry_ms = 0;
    begin_try(s, now);
}

void rfm1cs_session_stop(rfm1cs_session_t *s)
{
    s->want = 0;
    teardown(s);                                        /* (flushed and let go even when idle, as the web player) */
    s->retry_due = 0;
    set_state(s, RFM1CS_OFF, 0);
}

void rfm1cs_session_give_up(rfm1cs_session_t *s, int tries) { s->give_up = tries; }

void rfm1cs_session_wait(rfm1cs_session_t *s, uint32_t now, uint32_t ms, int patient)
{
    if (!s->want || s->live || s->st.state != RFM1CS_CONNECTING)
        return;
    s->hello_at = now + ms;
    s->hello_due = 1;
    s->patient = patient;
}

/* the link went: let go, and try again while wanted */
static void lost(rfm1cs_session_t *s, uint32_t now, int why, int final)
{
    rfm1cs_event_t ev;
    const int was = s->live;
    uint32_t ms;
    teardown(s);
    memset(&ev, 0, sizeof ev);
    ev.type = RFM1CS_EV_LOST;
    ev.reason = why;
    ev.was_live = was;
    emit(s, &ev);
    if (!s->want)                                       /* (the host stopped it on LOST) */
        return;
    if (final || why == RFM1CS_WHY_PROTO || (!s->st.ever_live && s->give_up >= 0 && s->st.retries >= s->give_up)) {
        s->want = 0;
        set_state(s, RFM1CS_FAILED, 0);
        return;
    }
    ms = rfm1cs_retry_ms[s->st.retries < RFM1CS_NRETRY ? s->st.retries : RFM1CS_NRETRY - 1];
    s->st.retries++;
    s->st.retry_ms = ms;
    s->retry_at = now + ms;
    s->retry_due = 1;
    set_state(s, RFM1CS_RETRYING, RFM1CS_CH_STATE);
}

void rfm1cs_session_opened(rfm1cs_session_t *s, uint32_t now)
{
    uint8_t *p = s->tx + 3;
    size_t k;
    if (!s->want || s->live || s->st.state != RFM1CS_CONNECTING)
        return;
    s->opened = 1;
    if (s->st.waiting) {                                /* (it waited: the HELLO's whole wait from now) */
        s->st.waiting = 0;
        s->hello_at = now + RFM1CS_HELLO_MS;
        s->hello_due = 1;
        status_changed(s, RFM1CS_CH_WAITING);
    }
    s->last_rx = now;
    memset(p, 0, 34);                                   /* HELLO: proto16, name[32] */
    p[0] = MR_PROTO;
    k = strlen(s->host_name);
    memcpy(p + 2, s->host_name, k > 31 ? 31 : k);
    send_tx(s, MR_H_HELLO, 34);
}

void rfm1cs_session_closed(rfm1cs_session_t *s, uint32_t now, int final)
{
    if (!s->want || (s->st.state != RFM1CS_CONNECTING && s->st.state != RFM1CS_CONNECTED))
        return;
    lost(s, now, RFM1CS_WHY_CLOSED, final);
}

static void on_hello(rfm1cs_session_t *s, uint32_t now, const uint8_t *p, uint32_t n)
{
    rfm1cs_status_t h = s->st;
    uint8_t *c = s->tx + 3;
    const int first = !s->live;
    if (rfm1cs_parse_hello(p, n, &h) != 0) {
        lost(s, now, RFM1CS_WHY_NOT_MOVE, 0);
        return;
    }
    memcpy(s->st.name, h.name, sizeof h.name);
    s->st.via = h.via, s->st.proto = h.proto, s->st.caps = h.caps;
    if (h.proto != MR_PROTO) {
        lost(s, now, RFM1CS_WHY_PROTO, 1);
        return;
    }
    s->hello_due = 0;
    s->live = 1;
    s->st.ever_live = 1;
    s->st.retries = 0;
    s->st.waiting = 0;
    if (first)
        s->st.connects++;
    c[0] = MR_CFG_AUDIO_CLOCK;                          /* (no AUDIO every 6 ms: the host plays its own sound) */
    wr32(c + 1, 0);
    send_tx(s, MR_H_CONFIG, 5);
    rfm_move_leds_invalidate(&s->core);                 /* every LED written again */
    s->shown_ok = 0;
    rfm1cs_session_flush_leds(s);
    s->dirty = 1, s->shot_ok = 0, s->screen_due = 0;
    screen_soon(s, now);
    s->next_ping = now + RFM1CS_PING_MS;
    s->timed_screen = 0;
    set_state(s, RFM1CS_CONNECTED, RFM1CS_CH_HELLO);
    after(s);
}

static void on_bye(rfm1cs_session_t *s, uint32_t now, int reason)
{
    rfm1cs_event_t ev;
    const int was = s->live;
    if (reason == MR_BYE_SHUTDOWN) {
        lost(s, now, RFM1CS_WHY_SHUTDOWN, 0);
        return;
    }
    s->want = 0;
    teardown(s);
    memset(&ev, 0, sizeof ev);
    ev.type = RFM1CS_EV_BYE;
    ev.reason = reason;
    ev.was_live = was;
    emit(s, &ev);
    set_state(s, RFM1CS_OFF, 0);
}

static void message(rfm1cs_session_t *s, uint32_t now, int type, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    switch (type) {
    case MR_D_HELLO: on_hello(s, now, p, n); return;
    case MR_D_BYE: on_bye(s, now, n ? p[0] : 0); return;
    case MR_D_PONG: s->st.pongs++; return;
    case MR_D_STATUS: {
        rfm1cs_status_t *st = &s->st;
        const int led_pending = n >= 26 ? rd16(p + 24) : 0;
        if (n < MR_STATUS_LEN)
            return;
        st->led_pending = led_pending;
        if (st->battery != p[4] || st->power != p[5] || st->jacks != p[6]) {
            st->battery = p[4], st->power = p[5], st->jacks = p[6];
            status_changed(s, RFM1CS_CH_BATTERY);
        }
        if (rfm_move_set_battery(&s->core, p[4], p[5]))   /* (the Global Settings menu shows it) */
            screen_soon(s, now);
        return;
    }
    case MR_D_INPUT:
        if (!s->live)
            return;
        for (i = 0; i + 8 <= n; i += 8) {
            mr_event_t ev;
            s->st.inputs++;
            mr_decode(p + i + 4, rd32(p + i), &ev);
            rfm_move_input(&s->core, &ev, now);
            if (!s->live)                               /* (the host stopped it in an action) */
                return;
        }
        after(s);
        rfm1cs_session_flush_leds(s);
        s->dirty = 1;
        screen_soon(s, now);
        return;
    default: return;                                    /* (AUDIO and anything newer: not asked for) */
    }
}

void rfm1cs_session_rx(rfm1cs_session_t *s, uint32_t now, const uint8_t *b, uint32_t n)
{
    uint32_t i = 0;
    if (!s->want || (s->st.state != RFM1CS_CONNECTING && s->st.state != RFM1CS_CONNECTED))
        return;
    s->last_rx = now;
    s->st.rx_bytes += n;
    while (i < n) {
        uint32_t len, take;
        if (s->rx_skip) {                               /* (a message too long to keep: skipped) */
            take = n - i < s->rx_skip ? n - i : s->rx_skip;
            s->rx_skip -= take, i += take;
            continue;
        }
        if (s->rx_len < 2) {
            s->rx[s->rx_len++] = b[i++];
            continue;
        }
        len = rd16(s->rx);
        if (len == 0) {
            lost(s, now, RFM1CS_WHY_GARBLED, 0);
            return;
        }
        if (len + 2 > RFM1CS_RX_MAX) {
            s->rx_skip = len, s->rx_len = 0;
            continue;
        }
        take = len + 2 - s->rx_len;
        if (take > n - i)
            take = n - i;
        memcpy(s->rx + s->rx_len, b + i, take);
        s->rx_len += take, i += take;
        if (s->rx_len == len + 2) {
            s->rx_len = 0;
            message(s, now, s->rx[2], s->rx + 3, len - 1);
            if (!s->want || (s->st.state != RFM1CS_CONNECTING && s->st.state != RFM1CS_CONNECTED))
                return;                                 /* (lost, BYE, or stopped in a callback) */
        }
    }
}

static void heartbeat(rfm1cs_session_t *s, uint32_t now)
{
    uint8_t *p = s->tx + 3;
    const uint64_t us = (uint64_t)now * 1000u;
    if ((uint32_t)(now - s->last_rx) > RFM1CS_SILENT_MS) {
        lost(s, now, RFM1CS_WHY_SILENT, 0);
        return;
    }
    s->st.pings++;
    wr32(p, ++s->token);
    wr32(p + 4, (uint32_t)us);
    wr32(p + 8, (uint32_t)(us >> 32));
    send_tx(s, MR_H_PING, 12);
}

void rfm1cs_session_tick(rfm1cs_session_t *s, uint32_t now)
{
    if (s->hello_due && due(now, s->hello_at)) {
        s->hello_due = 0;
        if (s->patient && !s->opened) {
            s->st.waiting = 1;
            status_changed(s, RFM1CS_CH_WAITING);
        } else {
            lost(s, now, RFM1CS_WHY_NO_ANSWER, 0);
        }
    }
    while (s->live && due(now, s->next_ping)) {
        s->next_ping += RFM1CS_PING_MS;
        heartbeat(s, now);
    }
    if (s->retry_due && due(now, s->retry_at)) {
        s->retry_due = 0;
        if (s->want)
            begin_try(s, now);
    }
    if (s->screen_due && due(now, s->shot_at + RFM1CS_SCREEN_MS)) {
        s->screen_due = 0;
        draw(s, now);
    }
}

int32_t rfm1cs_session_next_ms(const rfm1cs_session_t *s, uint32_t now)
{
    int32_t best = -1;
    uint32_t at[4];
    int n = 0, i;
    if (s->hello_due)
        at[n++] = s->hello_at;
    if (s->live)
        at[n++] = s->next_ping;
    if (s->retry_due)
        at[n++] = s->retry_at;
    if (s->screen_due)
        at[n++] = s->shot_at + RFM1CS_SCREEN_MS;
    for (i = 0; i < n; i++) {
        int32_t d = (int32_t)(at[i] - now);
        if (d < 0)
            d = 0;
        if (best < 0 || d < best)
            best = d;
    }
    return best;
}

/* ---- the sequencer */
void rfm1cs_session_update(rfm1cs_session_t *s) { after(s); }
void rfm1cs_session_pumped(rfm1cs_session_t *s, uint32_t now)
{
    if (!s->live)
        return;
    after(s);
    rfm1cs_session_flush_leds(s);
    if (s->st.playing || s->core.s.mode == RFM_MOVE_MODE_SEQ || s->timed_screen)   /* (the playhead, blinks, popups) */
        screen_soon(s, now);
    s->timed_screen = rfm_move_screen_timed(&s->core);  /* (one more draw after a popup drops) */
}
void rfm1cs_session_advance(rfm1cs_session_t *s, uint32_t now, uint32_t frames, uint32_t rate)
{
    if (!s->live)
        return;
    rfm_move_seq_advance(&s->core, frames, rate);
    rfm1cs_session_pumped(s, now);
}
void rfm1cs_session_midi_in(rfm1cs_session_t *s, uint32_t now, const uint8_t *msg, int n)
{
    (void)now;
    if (!s->live || n <= 0)
        return;
    rfm_move_seq_midi_in(&s->core, msg, n);
    after(s);
}

/* ---- projects */
int rfm1cs_session_projects_init(rfm1cs_session_t *s, uint32_t now, uint32_t dir_bits, int slot, const uint8_t *blob,
                                 uint32_t n)
{
    int r = 0;
    (void)now;
    rfm_move_project_dir(&s->core, dir_bits);
    if (rfm_move_project_load(&s->core, slot, blob && n ? blob : NULL, blob ? n : 0) < 0) {
        rfm_move_project_load(&s->core, slot, NULL, 0);   /* (unreadable: it starts empty; the stored copy stays) */
        r = -1;
    }
    after(s);
    return r;
}
int rfm1cs_session_project_answer(rfm1cs_session_t *s, uint32_t now, int slot, const uint8_t *blob, uint32_t n)
{
    const int r = rfm_move_project_load(&s->core, slot, blob && n ? blob : NULL, blob ? n : 0) < 0 ? -1 : 0;
    after(s);
    rfm1cs_session_flush_leds(s);
    screen_soon(s, now);
    return r;
}
