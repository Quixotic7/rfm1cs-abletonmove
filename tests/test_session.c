/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm1cs_session and rfm1cs_store against a fake Move fed byte by byte, on a fake clock: the handshake, pings and
 * silence, the retry schedule and giving up, the patient wait, BYE, garbled and foreign HELLOs, LEDs only when changed,
 * the screen's pacing, the battery, pads to actions, the sequencer by the audio clock, the project round trips, and
 * the store's cut / copy / paste / colour in both its synchronous and its queued form. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "moveremote/proto.h"
#include "rfm1cs/session.h"
#include "rfm1cs/store.h"

/* ---- what came out */
typedef struct {
    int type;
    uint32_t len;
    uint8_t p[1100];
} msg_t;
static msg_t g_out[512];
static int g_nout;
static rfm_move_act_t g_acts[1024];
static int g_nacts, g_opens, g_closes, g_released, g_lost, g_lost_why, g_lost_live, g_bye, g_status;
static uint32_t g_changed;
static uint8_t g_saved[RFM_MOVE_PROJECT_MAX];
static uint32_t g_saved_len, g_settings_len;
static int g_stop_on_lost;

static void on_event(void *ctx, const rfm1cs_event_t *ev)
{
    rfm1cs_session_t *s = ctx;
    switch (ev->type) {
    case RFM1CS_EV_SEND:
        if (g_nout < 512 && ev->len >= 3 && ev->len - 3 <= sizeof g_out[0].p) {
            g_out[g_nout].type = ev->data[2];
            g_out[g_nout].len = ev->len - 3;
            memcpy(g_out[g_nout].p, ev->data + 3, ev->len - 3);
            g_nout++;
        }
        break;
    case RFM1CS_EV_OPEN: g_opens++; break;
    case RFM1CS_EV_CLOSE: g_closes++; break;
    case RFM1CS_EV_RELEASED: g_released++; break;
    case RFM1CS_EV_LOST:
        g_lost++, g_lost_why = ev->reason, g_lost_live = ev->was_live;
        if (g_stop_on_lost)
            rfm1cs_session_stop(s);
        break;
    case RFM1CS_EV_BYE: g_bye = ev->reason; break;
    case RFM1CS_EV_STATUS: g_status++, g_changed |= ev->changed; break;
    case RFM1CS_EV_ACTION:
        if (g_nacts < 1024)
            g_acts[g_nacts++] = *ev->act;
        if (ev->act->kind == RFM_MOVE_ACT_PROJECT_SAVE && ev->len <= sizeof g_saved)
            memcpy(g_saved, ev->data, ev->len), g_saved_len = ev->len;
        if (ev->act->kind == RFM_MOVE_ACT_SETTINGS)
            g_settings_len = ev->len;
        break;
    }
}
static void clear(void) { g_nout = g_nacts = g_opens = g_closes = g_released = g_lost = g_bye = g_status = 0, g_changed = 0; }
static int count(int type)
{
    int i, n = 0;
    for (i = 0; i < g_nout; i++)
        n += g_out[i].type == type;
    return n;
}
static const msg_t *last(int type)
{
    int i;
    for (i = g_nout - 1; i >= 0; i--)
        if (g_out[i].type == type)
            return &g_out[i];
    return NULL;
}
static int led_writes(void)
{
    int i, n = 0;
    for (i = 0; i < g_nout; i++)
        n += g_out[i].type == MR_H_LED_RGB ? (int)g_out[i].len / 5 : g_out[i].type == MR_H_LED ? (int)g_out[i].len / 3 : 0;
    return n;
}
static int find_act(int kind, int index, int down)
{
    int i;
    for (i = 0; i < g_nacts; i++)
        if (g_acts[i].kind == kind && g_acts[i].index == index && g_acts[i].down == down)
            return i;
    return -1;
}

/* ---- the fake Move: its messages, fed one byte at a time */
static void feed(rfm1cs_session_t *s, uint32_t now, int type, const uint8_t *p, uint32_t n)
{
    static uint8_t m[3 + RFM1CS_RX_MAX + 200];
    uint32_t i, len = rfm1cs_frame(m, type, p, n);
    for (i = 0; i < len; i++)
        rfm1cs_session_rx(s, now, m + i, 1);
}
static void hello(rfm1cs_session_t *s, uint32_t now, int proto, int via, const char *name, int magic_ok)
{
    uint8_t p[46];
    memset(p, 0, sizeof p);
    memcpy(p, magic_ok ? "MOVR" : "NOPE", 4);
    p[4] = (uint8_t)proto;
    p[6] = MR_CAP_SIMULATOR & 255, p[7] = MR_CAP_SIMULATOR >> 8;
    p[8] = 0x44, p[9] = 0xAC;                           /* 44100 */
    p[12] = 128;
    p[14] = 128, p[15] = 64, p[16] = (uint8_t)via;
    strncpy((char *)p + 22, name, 24);
    feed(s, now, MR_D_HELLO, p, sizeof p);
}
static void status(rfm1cs_session_t *s, uint32_t now, int battery)
{
    uint8_t p[MR_STATUS_LEN];
    memset(p, 0, sizeof p);
    p[4] = (uint8_t)battery, p[5] = 0, p[6] = 2;
    feed(s, now, MR_D_STATUS, p, sizeof p);
}
/* INPUT records: frame32 | the USB-MIDI packet (cable 0) */
static void input(rfm1cs_session_t *s, uint32_t now, int st, int d1, int d2)
{
    uint8_t p[8] = { 0, 0, 0, 0, (uint8_t)((st >> 4) & 15), (uint8_t)st, (uint8_t)d1, (uint8_t)d2 };
    feed(s, now, MR_D_INPUT, p, 8);
}
static void bye(rfm1cs_session_t *s, uint32_t now, int reason)
{
    const uint8_t p[1] = { (uint8_t)reason };
    feed(s, now, MR_D_BYE, p, 1);
}
/* started, opened, the Move's HELLO */
static void connect(rfm1cs_session_t *s, uint32_t now)
{
    rfm1cs_session_start(s, now);
    rfm1cs_session_opened(s, now);
    hello(s, now, MR_PROTO, MR_VIA_USB, "Move", 1);
}

/* ---- the store's fake slots */
static uint8_t g_slot[RFM1CS_SLOTS][RFM_MOVE_PROJECT_MAX];
static uint32_t g_slot_len[RFM1CS_SLOTS];
static int g_async, g_reads, g_pending_slot[64], g_npending, g_loaded_slot = -9;
static uint32_t g_loaded_len, g_stored_used;
static char g_log[512];
static void logf_(const char *s) { strncat(g_log, s, sizeof g_log - strlen(g_log) - 1); }
static int io_read(void *ctx, int slot, uint8_t *out, uint32_t cap)
{
    char t[16];
    (void)ctx;
    g_reads++;
    snprintf(t, sizeof t, "r%d ", slot);
    logf_(t);
    if (g_async) {
        g_pending_slot[g_npending++] = slot;
        return RFM1CS_STORE_PENDING;
    }
    if (!g_slot_len[slot] || g_slot_len[slot] > cap)
        return 0;
    memcpy(out, g_slot[slot], g_slot_len[slot]);
    return (int)g_slot_len[slot];
}
static int io_write(void *ctx, int slot, const uint8_t *b, uint32_t n)
{
    char t[16];
    (void)ctx;
    snprintf(t, sizeof t, "w%d ", slot);
    logf_(t);
    memcpy(g_slot[slot], b, n);
    g_slot_len[slot] = n;
    return 0;
}
static int io_remove(void *ctx, int slot)
{
    char t[16];
    (void)ctx;
    snprintf(t, sizeof t, "d%d ", slot);
    logf_(t);
    g_slot_len[slot] = 0;
    return 0;
}
static void io_loaded(void *ctx, int slot, const uint8_t *b, uint32_t n)
{
    rfm1cs_session_t *s = ctx;
    g_loaded_slot = slot, g_loaded_len = n;
    if (s)
        rfm1cs_session_project_answer(s, 0, slot, b, n);
}
static void io_stored(void *ctx, int slot, const uint8_t *b, uint32_t n, uint32_t used)
{
    (void)ctx, (void)slot, (void)b, (void)n;
    g_stored_used = used;
}
/* answer the oldest pending read from the fake slots */
static void answer_one(rfm1cs_store_t *st)
{
    int slot, i;
    if (!g_npending)
        return;
    slot = g_pending_slot[0];
    for (i = 1; i < g_npending; i++)
        g_pending_slot[i - 1] = g_pending_slot[i];
    g_npending--;
    rfm1cs_store_answer(st, slot, g_slot_len[slot] ? g_slot[slot] : NULL, g_slot_len[slot]);
}

static rfm1cs_session_t S;                            /* (large: static) */

static void test_handshake(void)
{
    rfm1cs_session_t *s = &S;
    const msg_t *m;
    uint32_t t = 1000;
    rfm1cs_session_init(s, NULL, 0, "rfm1cs test", on_event, s);
    clear();
    CHECK(s->st.state == RFM1CS_OFF && rfm1cs_session_next_ms(s, t) == -1);
    rfm1cs_session_start(s, t);
    CHECKF(g_opens == 1 && s->st.state == RFM1CS_CONNECTING && g_nout == 0, "start: OPEN, nothing sent yet");
    rfm1cs_session_opened(s, t);
    m = last(MR_H_HELLO);
    CHECKF(m && m->len == 34 && m->p[0] == MR_PROTO && !strcmp((const char *)m->p + 2, "rfm1cs test"), "opened: HELLO");
    CHECK(rfm1cs_session_next_ms(s, t) == (int32_t)RFM1CS_HELLO_MS);
    clear();
    hello(s, t, MR_PROTO, MR_VIA_USB, "Move", 1);
    m = &g_out[0];
    CHECKF(g_nout >= 3 && m->type == MR_H_CONFIG && m->p[0] == MR_CFG_AUDIO_CLOCK && m->p[1] == 0, "CONFIG audio clock off first");
    CHECKF(led_writes() == RFM_MOVE_NLEDS, "every LED painted (%d)", led_writes());
    CHECKF(count(MR_H_DISPLAY) == 1, "one DISPLAY (%d)", count(MR_H_DISPLAY));
    CHECK(s->st.state == RFM1CS_CONNECTED && s->st.via == MR_VIA_USB && !strcmp(s->st.name, "Move") && s->st.connects == 1);
    CHECK((g_changed & (RFM1CS_CH_STATE | RFM1CS_CH_HELLO)) == (RFM1CS_CH_STATE | RFM1CS_CH_HELLO));
    CHECK(s->st.caps & MR_CAP_SIMULATOR);

    /* pings every second; STATUS keeps it alive; the battery */
    clear();
    rfm1cs_session_tick(s, t + 999);
    CHECK(count(MR_H_PING) == 0);
    CHECK(rfm1cs_session_next_ms(s, t + 999) == 1);
    rfm1cs_session_tick(s, t + 1000);
    m = last(MR_H_PING);
    CHECKF(m && m->len == 12 && m->p[0] == 1, "a PING at 1 s, token 1");
    CHECK(s->core.battery == -1 && s->st.battery == -1);
    status(s, t + 1500, 57);
    CHECKF(s->st.battery == 57 && s->core.battery == 57 && (g_changed & RFM1CS_CH_BATTERY), "STATUS: battery 57");
    g_changed = 0;
    status(s, t + 2000, 57);
    CHECK(!(g_changed & RFM1CS_CH_BATTERY));
    rfm1cs_session_tick(s, t + 2000);
    rfm1cs_session_tick(s, t + 3000);
    rfm1cs_session_tick(s, t + 4000);
    rfm1cs_session_tick(s, t + 5000);
    CHECKF(count(MR_H_PING) == 5 && s->st.state == RFM1CS_CONNECTED, "pings at 1..5 s, alive (last heard 2 s)");
    /* silence: 3 s after the last STATUS, the next ping finds it lost */
    rfm1cs_session_tick(s, t + 6000);
    CHECKF(g_lost == 1 && g_lost_why == RFM1CS_WHY_SILENT && g_lost_live && g_released == 1 && g_closes == 1,
           "silent 4 s: lost (went quiet), let go, closed");
    CHECKF(s->st.state == RFM1CS_RETRYING && s->st.retry_ms == 1000 && rfm1cs_session_next_ms(s, t + 6000) == 1000,
           "retry in 1 s");
    /* the retries: 1, 2, 4, 4 s (it was live once: never given up) */
    {
        const uint32_t want[] = { 2000, 4000, 4000, 4000 };
        uint32_t now = t + 6000;
        int i, ok = 1;
        for (i = 0; i < 4; i++) {
            g_opens = 0;
            now += s->st.retry_ms;
            rfm1cs_session_tick(s, now);
            ok &= g_opens == 1 && s->st.state == RFM1CS_CONNECTING;
            rfm1cs_session_closed(s, now, 0);           /* (refused) */
            ok &= s->st.state == RFM1CS_RETRYING && s->st.retry_ms == want[i] && g_lost_why == RFM1CS_WHY_CLOSED;
        }
        CHECKF(ok, "retries after 1, 2, 4, 4 s");
        /* an opened transport with no HELLO: lost after RFM1CS_HELLO_MS */
        now += s->st.retry_ms;
        rfm1cs_session_tick(s, now);
        rfm1cs_session_opened(s, now);
        rfm1cs_session_tick(s, now + RFM1CS_HELLO_MS - 1);
        CHECK(s->st.state == RFM1CS_CONNECTING);
        rfm1cs_session_tick(s, now + RFM1CS_HELLO_MS);
        CHECKF(g_lost_why == RFM1CS_WHY_NO_ANSWER && s->st.state == RFM1CS_RETRYING, "no HELLO in 4 s: lost, retrying");
        /* and back: a new connection paints every LED again */
        now += RFM1CS_HELLO_MS + s->st.retry_ms;
        rfm1cs_session_tick(s, now);
        rfm1cs_session_opened(s, now);
        clear();
        hello(s, now, MR_PROTO, MR_VIA_WIFI, "Move 2", 1);
        CHECKF(s->st.state == RFM1CS_CONNECTED && s->st.retries == 0 && led_writes() == RFM_MOVE_NLEDS &&
                   s->st.via == MR_VIA_WIFI && s->st.connects == 2,
               "reconnected: every LED painted again (%d), Wi-Fi", led_writes());
    }
    rfm1cs_session_stop(s);
    CHECK(s->st.state == RFM1CS_OFF && rfm1cs_session_next_ms(s, t) == -1);
}

static void test_give_up_bye_garbled(void)
{
    rfm1cs_session_t *s = &S;
    uint32_t t = 50;
    int i;
    rfm1cs_session_init(s, NULL, 0, "x", on_event, s);
    /* never connected, give_up 2: the third failure gives up (the web player) */
    rfm1cs_session_give_up(s, 2);
    rfm1cs_session_start(s, t);
    for (i = 0; i < 3; i++) {
        rfm1cs_session_closed(s, t, 0);
        if (s->st.state == RFM1CS_RETRYING)
            t += s->st.retry_ms, rfm1cs_session_tick(s, t);
    }
    CHECKF(s->st.state == RFM1CS_FAILED && s->st.retries == 2, "give_up 2: FAILED after 3 failures (%d)", s->st.state);
    CHECK(rfm1cs_session_next_ms(s, t) == -1);
    /* give_up 0: the first failure */
    rfm1cs_session_give_up(s, 0);
    rfm1cs_session_start(s, t);
    rfm1cs_session_closed(s, t, 0);
    CHECK(s->st.state == RFM1CS_FAILED);
    /* a final close: no retry */
    rfm1cs_session_give_up(s, -1);
    rfm1cs_session_start(s, t);
    rfm1cs_session_closed(s, t, 1);
    CHECK(s->st.state == RFM1CS_FAILED);
    /* the host stops it on LOST: OFF, nothing scheduled */
    rfm1cs_session_start(s, t);
    g_stop_on_lost = 1;
    rfm1cs_session_closed(s, t, 0);
    g_stop_on_lost = 0;
    CHECK(s->st.state == RFM1CS_OFF && rfm1cs_session_next_ms(s, t) == -1);

    /* patient: the wait passes with the transport still opening: only waiting */
    clear();
    rfm1cs_session_start(s, t);
    rfm1cs_session_wait(s, t, 9000, 1);
    rfm1cs_session_tick(s, t + 9000);
    CHECKF(s->st.state == RFM1CS_CONNECTING && s->st.waiting && (g_changed & RFM1CS_CH_WAITING) && !g_lost,
           "patient: waiting, not lost");
    rfm1cs_session_opened(s, t + 20000);
    CHECK(!s->st.waiting && rfm1cs_session_next_ms(s, t + 20000) == (int32_t)RFM1CS_HELLO_MS);
    rfm1cs_session_tick(s, t + 20000 + RFM1CS_HELLO_MS);
    CHECK(g_lost && g_lost_why == RFM1CS_WHY_NO_ANSWER);
    rfm1cs_session_stop(s);

    /* BYE: the exit gesture ends it; a power off is tried again */
    connect(s, t);
    clear();
    bye(s, t, MR_BYE_EXIT);
    CHECKF(g_bye == MR_BYE_EXIT && s->st.state == RFM1CS_OFF && g_closes == 1 && g_released == 1 && !g_lost,
           "BYE exit: off, closed, let go");
    connect(s, t);
    clear();
    bye(s, t, MR_BYE_SHUTDOWN);
    CHECKF(g_lost && g_lost_why == RFM1CS_WHY_SHUTDOWN && s->st.state == RFM1CS_RETRYING, "BYE shutdown: retrying");
    rfm1cs_session_stop(s);
    connect(s, t);
    bye(s, t, MR_BYE_REPLACED);
    CHECK(g_bye == MR_BYE_REPLACED && s->st.state == RFM1CS_OFF);

    /* a zero length, a HELLO that is not Move Remote's, another protocol version */
    connect(s, t);
    clear();
    {
        const uint8_t z[3] = { 0, 0, 0 };
        rfm1cs_session_rx(s, t, z, 3);
    }
    CHECK(g_lost && g_lost_why == RFM1CS_WHY_GARBLED);
    rfm1cs_session_stop(s);
    rfm1cs_session_start(s, t);
    rfm1cs_session_opened(s, t);
    hello(s, t, MR_PROTO, 0, "x", 0);
    CHECK(g_lost_why == RFM1CS_WHY_NOT_MOVE && s->st.state == RFM1CS_RETRYING);
    rfm1cs_session_stop(s);
    rfm1cs_session_start(s, t);
    rfm1cs_session_opened(s, t);
    hello(s, t, 2, 0, "Future", 1);
    CHECKF(g_lost_why == RFM1CS_WHY_PROTO && s->st.state == RFM1CS_FAILED && s->st.proto == 2, "protocol 2: FAILED");
    /* a message too long to keep is skipped, the stream goes on */
    connect(s, t);
    clear();
    {
        static uint8_t big[RFM1CS_RX_MAX + 100];
        memset(big, 0, sizeof big);
        feed(s, t, MR_D_AUDIO, big, sizeof big);
        status(s, t, 80);
        CHECK(s->st.state == RFM1CS_CONNECTED && s->st.battery == 80);
    }
    rfm1cs_session_stop(s);
}

static void test_leds_screen_input(void)
{
    rfm1cs_session_t *s = &S;
    uint8_t levels[RFM_MOVE_NLEVELS];
    static uint16_t lcd[240 * 240];
    uint32_t t = 7000;
    int i, k;
    rfm1cs_session_init(s, NULL, 0, "x", on_event, s);
    connect(s, t);
    /* LEDs only when they change */
    memset(levels, 0, sizeof levels);
    clear();
    rfm1cs_session_panel(s, levels, 0);
    CHECKF(led_writes() == 0 && g_nout == 0, "the FM-1's LEDs unchanged: nothing sent");
    levels[3 * 4 + 3] = 255;                            /* key 0 */
    rfm1cs_session_panel(s, levels, 0);
    CHECKF(led_writes() > 0 && s->st.led_writes > 0, "key 0 lit: its LEDs written (%d)", led_writes());
    k = led_writes();
    clear();
    rfm1cs_session_panel(s, levels, 0);
    CHECK(led_writes() == 0);
    clear();
    rfm1cs_session_panel(s, levels, 200);               /* PLAY's green as a level */
    CHECKF(led_writes() >= 1, "PLAY green 200: written (%d, after %d)", led_writes(), k);

    /* a pad: FM-1 key 0 with its velocity; the screen redrawn at once (33 ms since the last) */
    clear();
    t += 100;
    input(s, t, 0x90, MR_NOTE_PAD0 + 16, 100);
    i = find_act(RFM_MOVE_ACT_KEY, 0, 1);
    CHECKF(i >= 0 && g_acts[i].vel == 100 && s->st.inputs == 1, "pad-2-1: key 0 down, velocity 100");
    /* within 33 ms: the next change waits for the tick */
    clear();
    for (i = 0; i < 240 * 240; i++)
        lcd[i] = (uint16_t)((i % 240) < 120 ? 0xFFFF : 0);
    rfm1cs_session_frame(s, t + 10, lcd);
    CHECKF(count(MR_H_DISPLAY) == 0 && rfm1cs_session_next_ms(s, t + 10) == 23, "a frame 10 ms later: drawn in 23 ms");
    rfm1cs_session_tick(s, t + 32);
    CHECK(count(MR_H_DISPLAY) == 0);
    rfm1cs_session_tick(s, t + 33);
    CHECKF(count(MR_H_DISPLAY) == 1, "...drawn at 33 ms");
    /* the same picture again: not sent */
    clear();
    rfm1cs_session_frame(s, t + 100, lcd);
    CHECK(count(MR_H_DISPLAY) == 0 && s->st.screens >= 2);
    input(s, t + 200, 0x80, MR_NOTE_PAD0 + 16, 0);
    CHECK(find_act(RFM_MOVE_ACT_KEY, 0, 0) >= 0);

    /* a held pad, then the link goes: let go */
    clear();
    input(s, t + 300, 0x90, MR_NOTE_PAD0 + 18, 90);
    k = g_nacts;
    rfm1cs_session_closed(s, t + 310, 0);
    CHECKF(g_nacts > k && g_acts[k].kind == RFM_MOVE_ACT_KEY && !g_acts[k].down && g_released == 1,
           "the link lost with a pad held: its key let go, RELEASED");
    /* settings: Track 2 (MIDI mode) asks for them to be kept, with the image */
    rfm1cs_session_stop(s);
    connect(s, t + 400);
    clear();
    input(s, t + 400, 0xB0, MR_CC_TRACK1 - 1, 127);
    input(s, t + 401, 0xB0, MR_CC_TRACK1 - 1, 0);
    CHECKF(s->st.mode == RFM_MOVE_MODE_MIDI && (g_changed & RFM1CS_CH_MODE) && find_act(RFM_MOVE_ACT_SETTINGS, 0, 0) >= 0 &&
               g_settings_len == RFM_MOVE_SETTINGS_BYTES,
           "Track 2: MIDI mode, SETTINGS with its image (%u)", g_settings_len);
    input(s, t + 410, 0x90, MR_NOTE_PAD0, 90);
    i = find_act(RFM_MOVE_ACT_MIDI, 0, 0);
    CHECKF(i >= 0 && g_acts[i].bytes[0] == 0x90 && g_acts[i].bytes[1] == 48 && s->st.midi >= 1, "MIDI mode: the pad is C3");
    input(s, t + 411, 0x80, MR_NOTE_PAD0, 0);
    rfm1cs_session_stop(s);
}

static void test_sequencer_projects(void)
{
    rfm1cs_session_t *s = &S;
    static rfm1cs_store_t st;
    rfm1cs_store_io_t io = { 0 };
    uint32_t t = 100, used = 0;
    int i, ons = 0, at5 = -1, frames;
    rfm1cs_session_init(s, NULL, 0, "x", on_event, s);
    memset(g_slot_len, 0, sizeof g_slot_len);
    io.ctx = s, io.read = io_read, io.write = io_write, io.remove = io_remove, io.loaded = io_loaded,
    io.stored = io_stored;
    rfm1cs_store_init(&st, &io, 0);
    CHECK(rfm1cs_store_scan(&st, 0, &used) == 0 && used == 0);
    CHECK(rfm1cs_session_projects_init(s, t, used, 0, NULL, 0) == 0 && s->st.project == 0);
    connect(s, t);
    /* Track 4, pad 1 (C3), steps 1 and 5, Play */
    clear();
    input(s, t, 0xB0, MR_CC_TRACK4, 127), input(s, t, 0xB0, MR_CC_TRACK4, 0);
    input(s, t, 0x90, MR_NOTE_PAD0, 100), input(s, t, 0x80, MR_NOTE_PAD0, 0);
    input(s, t, 0x90, MR_NOTE_STEP0, 127), input(s, t, 0x80, MR_NOTE_STEP0, 0);
    input(s, t, 0x90, MR_NOTE_STEP0 + 4, 127), input(s, t, 0x80, MR_NOTE_STEP0 + 4, 0);
    input(s, t, 0xB0, MR_CC_PLAY, 127), input(s, t, 0xB0, MR_CC_PLAY, 0);
    CHECKF(s->st.mode == RFM_MOVE_MODE_SEQ && s->core.seq.nnotes == 2, "Track 4: two steps (%d)", s->core.seq.nnotes);
    clear();
    for (frames = 0; frames < 48000; frames += 128) {
        const int n0 = g_nacts;
        rfm1cs_session_advance(s, t + (uint32_t)frames / 48, 128, 48000);
        for (i = n0; i < g_nacts; i++)
            if (g_acts[i].kind == RFM_MOVE_ACT_MIDI && g_acts[i].bytes[0] == 0x90 && g_acts[i].bytes[1] == 48 && ++ons == 2)
                at5 = frames + (int)g_acts[i].frame;
    }
    CHECKF(ons == 2 && at5 == 24000, "the audio clock: C3 on steps 1 and 5, step 5 at frame %d", at5);
    CHECKF(s->st.playing && (g_changed & RFM1CS_CH_PLAYING) && led_writes() > 0,
           "playing: the status says so (%d, %x), the step LEDs follow (%d screens, %d LEDs)", s->st.playing, g_changed,
           count(MR_H_DISPLAY), led_writes());
    /* the firmware's clock: FA then F8s, AUTO follows it */
    {
        const uint8_t fa = 0xFA, f8 = 0xF8;
        g_changed = 0;
        rfm1cs_session_midi_in(s, t, &fa, 1);
        for (i = 0; i < 48; i++) {
            rfm1cs_session_midi_in(s, t, &f8, 1);
            rfm1cs_session_advance(s, t, 1000, 48000);
        }
        CHECKF(s->st.following && (g_changed & RFM1CS_CH_CLOCK), "the firmware's MIDI clock: following (%d)", s->st.following);
    }
    rfm1cs_session_advance(s, t, 48000, 48000);
    /* the autosave (by the clock, or flushed now) gives PROJECT_SAVE with the blob; the store writes it */
    rfm1cs_session_flush(s);
    CHECKF(g_saved_len > 0 && rfm_move_project_check(g_saved, g_saved_len) == 0, "flush: project 1 saved (%u)", g_saved_len);
    g_log[0] = 0;
    CHECK(rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_SAVE, 0, 0, g_saved, g_saved_len) == 1);
    CHECK(g_slot_len[0] == g_saved_len && rfm1cs_store_used(&st) == 1 && g_stored_used == 1 && !strcmp(g_log, "w0 "));
    CHECK(rfm1cs_store_request(&st, RFM_MOVE_ACT_KEY, 0, 0, NULL, 0) == 0);
    CHECK(rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_SAVE, 32, 0, g_saved, g_saved_len) == -1);
    /* copy 1 -> paste into 3; cut 3 -> paste into 5; colour 5 */
    g_log[0] = 0;
    rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_COPY, 0, 0, NULL, 0);
    rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_PASTE, 2, 0, NULL, 0);
    rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_CUT, 2, 0, NULL, 0);
    rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_PASTE, 4, 0, NULL, 0);
    rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_COLOR, 4, 7, NULL, 0);
    CHECKF(!strcmp(g_log, "r0 w2 r2 d2 w4 r4 w4 "), "copy, paste, cut, paste, colour: %s", g_log);
    CHECK(g_slot_len[2] == 0 && g_slot_len[4] == g_saved_len && rfm1cs_store_used(&st) == (1u | 1u << 4));
    CHECKF(g_slot[4][RFM1CS_COLOR_BYTE] == 7 && rfm_move_project_check(g_slot[4], g_slot_len[4]) == 0,
           "colour 7 at byte 18, the check made again");
    /* LOAD through the store: the session answered; a broken one refused */
    clear();
    rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_LOAD, 4, 0, NULL, 0);
    CHECKF(g_loaded_slot == 4 && g_loaded_len == g_saved_len && s->st.project == 4 && s->core.seq.nnotes == 2 &&
               (g_changed & RFM1CS_CH_PROJECT),
           "LOAD slot 5: loaded, the status says slot 5");
    g_slot[4][40] ^= 0x55;
    CHECK(rfm1cs_session_project_answer(s, t, 4, g_slot[4], g_slot_len[4]) == -1);
    g_slot[4][40] ^= 0x55;
    CHECK(rfm1cs_session_project_answer(s, t, 6, NULL, 0) == 0 && s->core.seq.nnotes == 0 && s->st.project == 6);
    /* the scan at start: the used bits, the open slot's bytes; an unreadable one starts empty */
    used = 0;
    CHECK(rfm1cs_store_scan(&st, 4, &used) == (int)g_saved_len && used == (1u | 1u << 4));
    CHECK(rfm1cs_session_projects_init(s, t, used, 4, st.buf, g_saved_len) == 0 && s->core.seq.nnotes == 2);
    CHECK(rfm1cs_session_projects_init(s, t, used, 3, (const uint8_t *)"junk", 4) == -1 && s->core.seq.nnotes == 0);

    /* the queued form (reads answered later, in order): a cut's delete asked at once, the paste after the answer */
    g_async = 1, g_npending = 0, g_log[0] = 0;
    rfm1cs_store_init(&st, &io, 1u | 1u << 4);
    rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_CUT, 4, 0, NULL, 0);
    rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_PASTE, 9, 0, NULL, 0);
    rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_COLOR, 0, 3, NULL, 0);
    rfm1cs_store_request(&st, RFM_MOVE_ACT_PROJECT_DELETE, 1, 0, NULL, 0);
    CHECKF(!strcmp(g_log, "r4 d4 r0 d1 ") && g_npending == 2, "queued: %s", g_log);
    /* (the host's queue keeps the order: the read of 4 saw it before its delete) */
    memcpy(g_slot[4], g_slot[0], g_slot_len[0]), g_slot_len[4] = g_slot_len[0];
    answer_one(&st);
    CHECKF(!strcmp(g_log, "r4 d4 r0 d1 w9 ") && g_slot_len[9] == g_saved_len, "the cut answered: pasted into 10: %s", g_log);
    answer_one(&st);
    CHECKF(!strcmp(g_log, "r4 d4 r0 d1 w9 w0 ") && g_slot[0][RFM1CS_COLOR_BYTE] == 3 &&
               rfm_move_project_check(g_slot[0], g_slot_len[0]) == 0,
           "the colour answered: written: %s", g_log);
    CHECK(rfm1cs_store_used(&st) == (1u | 1u << 9));
    g_async = 0;
    CHECK(rfm1cs_first_free(0) == 0 && rfm1cs_first_free(7) == 3 && rfm1cs_first_free(0xFFFFFFFFu) == -1);
    rfm1cs_session_stop(s);
    CHECK(!s->st.playing);
}

int main(void)
{
    test_handshake();
    test_give_up_bye_garbled();
    test_leds_screen_input();
    test_sequencer_projects();
    return check_done("session");
}
