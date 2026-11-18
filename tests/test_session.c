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
static uint32_t g_att;                                /* the last RFM1CS_EV_OPEN's attempt */
static char g_conn_text[256];                         /* the status line when CONNECTED came */

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
    case RFM1CS_EV_OPEN: g_opens++, g_att = ev->attempt; break;
    case RFM1CS_EV_CLOSE: g_closes++; break;
    case RFM1CS_EV_RELEASED: g_released++; break;
    case RFM1CS_EV_LOST:
        g_lost++, g_lost_why = ev->reason, g_lost_live = ev->was_live;
        if (g_stop_on_lost)
            rfm1cs_session_stop(s);
        break;
    case RFM1CS_EV_BYE: g_bye = ev->reason; break;
    case RFM1CS_EV_STATUS:
        g_status++, g_changed |= ev->changed;
        if ((ev->changed & RFM1CS_CH_STATE) && s->st.state == RFM1CS_CONNECTED)   /* (what a host shows then) */
            rfm1cs_status_text(&s->st, NULL, g_conn_text, sizeof g_conn_text);
        break;
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
        rfm1cs_session_rx(s, now, g_att, m + i, 1);
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
    rfm1cs_session_opened(s, now, g_att);
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
    rfm1cs_session_opened(s, t, g_att);
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
            rfm1cs_session_closed(s, now, g_att, 0);           /* (refused) */
            ok &= s->st.state == RFM1CS_RETRYING && s->st.retry_ms == want[i] && g_lost_why == RFM1CS_WHY_CLOSED;
        }
        CHECKF(ok, "retries after 1, 2, 4, 4 s");
        /* an opened transport with no HELLO: lost after RFM1CS_HELLO_MS */
        now += s->st.retry_ms;
        rfm1cs_session_tick(s, now);
        rfm1cs_session_opened(s, now, g_att);
        rfm1cs_session_tick(s, now + RFM1CS_HELLO_MS - 1);
        CHECK(s->st.state == RFM1CS_CONNECTING);
        rfm1cs_session_tick(s, now + RFM1CS_HELLO_MS);
        CHECKF(g_lost_why == RFM1CS_WHY_NO_ANSWER && s->st.state == RFM1CS_RETRYING, "no HELLO in 4 s: lost, retrying");
        /* and back: a new connection paints every LED again */
        now += RFM1CS_HELLO_MS + s->st.retry_ms;
        rfm1cs_session_tick(s, now);
        rfm1cs_session_opened(s, now, g_att);
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
        rfm1cs_session_closed(s, t, g_att, 0);
        if (s->st.state == RFM1CS_RETRYING)
            t += s->st.retry_ms, rfm1cs_session_tick(s, t);
    }
    CHECKF(s->st.state == RFM1CS_FAILED && s->st.retries == 2, "give_up 2: FAILED after 3 failures (%d)", s->st.state);
    CHECK(rfm1cs_session_next_ms(s, t) == -1);
    /* give_up 0: the first failure */
    rfm1cs_session_give_up(s, 0);
    rfm1cs_session_start(s, t);
    rfm1cs_session_closed(s, t, g_att, 0);
    CHECK(s->st.state == RFM1CS_FAILED);
    /* a final close: no retry */
    rfm1cs_session_give_up(s, -1);
    rfm1cs_session_start(s, t);
    rfm1cs_session_closed(s, t, g_att, 1);
    CHECK(s->st.state == RFM1CS_FAILED);
    /* the host stops it on LOST: OFF, nothing scheduled */
    rfm1cs_session_start(s, t);
    g_stop_on_lost = 1;
    rfm1cs_session_closed(s, t, g_att, 0);
    g_stop_on_lost = 0;
    CHECK(s->st.state == RFM1CS_OFF && rfm1cs_session_next_ms(s, t) == -1);

    /* patient: the wait passes with the transport still opening: only waiting */
    clear();
    rfm1cs_session_start(s, t);
    rfm1cs_session_wait(s, t, 9000, 1);
    rfm1cs_session_tick(s, t + 9000);
    CHECKF(s->st.state == RFM1CS_CONNECTING && s->st.waiting && (g_changed & RFM1CS_CH_WAITING) && !g_lost,
           "patient: waiting, not lost");
    rfm1cs_session_opened(s, t + 20000, g_att);
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
        rfm1cs_session_rx(s, t, g_att, z, 3);
    }
    CHECK(g_lost && g_lost_why == RFM1CS_WHY_GARBLED);
    rfm1cs_session_stop(s);
    rfm1cs_session_start(s, t);
    rfm1cs_session_opened(s, t, g_att);
    hello(s, t, MR_PROTO, 0, "x", 0);
    CHECK(g_lost_why == RFM1CS_WHY_NOT_MOVE && s->st.state == RFM1CS_RETRYING);
    rfm1cs_session_stop(s);
    rfm1cs_session_start(s, t);
    rfm1cs_session_opened(s, t, g_att);
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
    rfm1cs_session_closed(s, t + 310, g_att, 0);
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

/* ---- 0.3.0: the attempt id, the frame's pacing, the words, the project file */
static void test_attempt(void)
{
    rfm1cs_session_t *s = &S;
    uint32_t t = 100, a1, a2;
    rfm1cs_session_init(s, NULL, 0, "x", on_event, s);
    clear();
    rfm1cs_session_start(s, t);
    a1 = g_att;
    CHECKF(g_opens == 1 && a1 != 0 && a1 == rfm1cs_session_attempt(s), "OPEN carries the attempt (%u)", a1);
    rfm1cs_session_closed(s, t, a1, 0);
    CHECK(s->st.state == RFM1CS_RETRYING);
    t += s->st.retry_ms;
    rfm1cs_session_tick(s, t);
    a2 = g_att;
    CHECKF(g_opens == 2 && a2 != a1 && s->st.state == RFM1CS_CONNECTING, "the retry: a new attempt (%u, was %u)", a2, a1);
    /* the old socket's close arrives late: ignored, the new try goes on */
    clear();
    rfm1cs_session_closed(s, t + 5, a1, 0);
    CHECKF(s->st.state == RFM1CS_CONNECTING && !g_lost && !g_closes, "a stale close (attempt %u): ignored", a1);
    rfm1cs_session_closed(s, t + 5, a1, 1);
    CHECK(s->st.state == RFM1CS_CONNECTING && !g_lost);
    rfm1cs_session_opened(s, t + 6, a1);
    CHECKF(g_nout == 0, "a stale open: no HELLO");
    rfm1cs_session_opened(s, t + 7, a2);
    CHECKF(count(MR_H_HELLO) == 1, "the current one: HELLO");
    {
        const int was = s->st.state;
        g_att = a1;                                     /* (a stale read: the Move's HELLO from the old socket) */
        hello(s, t + 8, MR_PROTO, MR_VIA_USB, "Old", 1);
        CHECKF(s->st.state == was && s->st.rx_bytes == 0, "stale bytes: ignored");
        g_att = a2;
        hello(s, t + 8, MR_PROTO, MR_VIA_USB, "Move", 1);
        CHECK(s->st.state == RFM1CS_CONNECTED && !strcmp(s->st.name, "Move"));
    }
    /* and when live: a stale close cannot end it */
    rfm1cs_session_closed(s, t + 9, a1, 0);
    CHECK(s->st.state == RFM1CS_CONNECTED);
    rfm1cs_session_closed(s, t + 9, a2, 0);
    CHECK(s->st.state == RFM1CS_RETRYING && g_lost_why == RFM1CS_WHY_CLOSED);
    /* a fresh start: a new id too */
    rfm1cs_session_start(s, t + 10);
    CHECK(g_att != a2 && g_att == rfm1cs_session_attempt(s));
    rfm1cs_session_stop(s);
}

static void test_frame_pacing(void)
{
    rfm1cs_session_t *s = &S;
    static uint16_t a[240 * 240], b[240 * 240];
    uint32_t t = 5000;
    int i;
    for (i = 0; i < 240 * 240; i++)
        a[i] = 0, b[i] = (i % 240) < 120 ? 0xFFFF : 0;
    rfm1cs_session_init(s, NULL, 0, "x", on_event, s);
    connect(s, t);
    rfm1cs_session_frame(s, t, a);
    CHECK(s->thumb_ok && s->thumb_at == t && s->core.thumb[10][10] == 0);
    rfm1cs_session_frame(s, t + 5, a);
    CHECKF(s->thumb_at == t, "the same picture 5 ms later: the thumbnail not taken again");
    rfm1cs_session_frame(s, t + 10, b);
    CHECKF(s->thumb_at == t + 10 && s->core.thumb[10][10] == 1 && s->core.thumb[10][60] == 0,
           "a new picture 10 ms later: taken at once");
    memcpy(a, b, sizeof a);                             /* (the host's one buffer, its pixels changed in place) */
    a[240 * 120 + 200] = 0xFFFF;
    rfm1cs_session_frame(s, t + 12, a);
    CHECKF(s->thumb_at == t + 12, "the same buffer, changed: taken");
    rfm1cs_session_frame(s, t + 40, a);
    CHECK(s->thumb_at == t + 12);
    rfm1cs_session_frame(s, t + 45, a);
    CHECKF(s->thumb_at == t + 45, "33 ms on: taken again");
    clear();
    rfm1cs_session_frame(s, t + 46, NULL);              /* (NULL: only the redraw) */
    CHECK(s->thumb_at == t + 45);
    rfm1cs_session_stop(s);
}

static void text(int (*fn)(const rfm1cs_status_t *, const rfm1cs_words_t *, char *, uint32_t),
                 const rfm1cs_status_t *st, const rfm1cs_words_t *w, int tone, const char *want)
{
    char out[256];
    const int got = fn(st, w, out, sizeof out);
    CHECKF(got == tone && !strcmp(out, want), "\"%s\" (tone %d), want \"%s\" (%d)", out, got, want, tone);
}
static void test_words(void)
{
    rfm1cs_session_t *s = &S;
    rfm1cs_words_t w = { "move.local", NULL, "WebSocket", NULL };
    char out[256];
    uint32_t t = 100;
    int tone = -1;
    rfm1cs_session_init(s, NULL, 0, "x", on_event, s);
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_OFF, "Not connected.");
    rfm1cs_session_start(s, t);
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_BUSY, "Connecting to move.local...");
    rfm1cs_session_wait(s, t, 100, 1);
    rfm1cs_session_tick(s, t + 100);
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_BUSY, "No answer from move.local yet.");
    rfm1cs_session_opened(s, t + 100, g_att);
    hello(s, t + 100, MR_PROTO, MR_VIA_USB, "Move", 1);
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_ON,
         "Move connected over USB (WebSocket): FM-1 mode. Track 1..4 pick the mode.");
    rfm1cs_session_tick(s, t + 4000);                   /* (silent) */
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_BUSY, "The Move went quiet: trying again in 1 s...");
    CHECK(rfm1cs_lost_text(g_lost_why, g_lost_live, &s->st, &w, out, sizeof out) > 0 &&
          !strcmp(out, "Move: link lost (the Move went quiet)."));
    w.closed = "connection refused";
    rfm1cs_session_tick(s, t + 5000);
    rfm1cs_session_closed(s, t + 5000, g_att, 0);
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_BUSY, "Connection refused: trying again in 2 s...");
    rfm1cs_lost_text(g_lost_why, g_lost_live, &s->st, &w, out, sizeof out);
    CHECKF(!strcmp(out, "The Move: link lost (connection refused)."), "%s", out);
    rfm1cs_session_tick(s, t + 7000);
    rfm1cs_session_closed(s, t + 7000, g_att, 1);       /* (final) */
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_BAD,
         "Connection refused. Is Move Remote running on the Move, and is move.local its address? Over USB-C it is "
         "172.16.254.1.");
    w.closed = NULL;
    rfm1cs_session_stop(s);
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_OFF, "Not connected.");
    /* the mode changed while away: the CONNECTED event's status has it already */
    rfm_move_set_mode(&s->core, RFM_MOVE_MODE_MIDI);
    rfm1cs_session_start(s, t);
    rfm1cs_session_opened(s, t, g_att);
    w.link = NULL;
    hello(s, t, MR_PROTO, MR_VIA_WIFI, "Mine", 1);
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_ON, "Mine connected over Wi-Fi (): MIDI mode. Track 1..4 pick the mode.");
    CHECKF(!strcmp(g_conn_text, "Mine connected over Wi-Fi (): MIDI mode. Track 1..4 pick the mode."), "%s", g_conn_text);
    w.link = "WebSocket";
    rfm1cs_session_stop(s);
    rfm_move_set_mode(&s->core, RFM_MOVE_MODE_FM1);
    /* the protocol mismatch, for an app */
    w.self = "app";
    rfm1cs_session_start(s, t);
    rfm1cs_session_opened(s, t, g_att);
    hello(s, t, 2, 0, "Future", 1);
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_BAD,
         "The Move speaks Move Remote protocol 2; this app speaks 1. Update one of them.");
    w.self = NULL;
    /* BYE: the words stay through the host's own stop() */
    connect(s, t);
    bye(s, t, MR_BYE_EXIT);
    rfm1cs_session_stop(s);
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_OFF, "Move left Move Remote (its exit gesture).");
    connect(s, t);
    bye(s, t, MR_BYE_PROTOCOL);
    text(rfm1cs_status_text, &s->st, &w, RFM1CS_TONE_BAD, "Move closed the link: a protocol error.");
    rfm1cs_bye_text(MR_BYE_REPLACED, 0, &s->st, out, sizeof out, &tone);
    CHECK(!strcmp(out, "Another host took The Move over.") && tone == RFM1CS_TONE_OFF);
    rfm1cs_bye_text(MR_BYE_QUIT, 1, &s->st, out, sizeof out, NULL);
    CHECK(!strcmp(out, "Move closed the link."));
    /* every reason's words */
    {
        static const char *const want[] = { "the link closed", "the link closed", "no answer from move.local",
                                             "the Move went quiet", "a message the page doesn't understand",
                                             "not a Move Remote answer", "the Move is powering off", "the link closed" };
        int why, ok = 1;
        for (why = 0; why <= RFM1CS_WHY_PROTO; why++) {
            rfm1cs_why_text(why, &w, out, sizeof out);
            ok &= !strcmp(out, want[why]);
        }
        CHECK(ok);
        rfm1cs_why_text(RFM1CS_WHY_NO_ANSWER, NULL, out, sizeof out);
        CHECK(!strcmp(out, "no answer from the Move"));
        CHECK(rfm1cs_why_text(RFM1CS_WHY_NO_ANSWER, &w, out, 8) == 7 && !strcmp(out, "no answ"));
    }
}

/* the files the web player, the iOS app and the Android app wrote (tests/rmvp: the same two blobs, a .rfmmove of slot
 * 4 saved 2026-11-17 10:00:00 UTC and a .rfmmoveset of slots 1 and 6, 10:00:00 and 2026-11-18 08:30:15; each made
 * by that host's own packFile / MoveProjectFile.pack / MoveProjects.packFile at RemoteFM1 1b8a351, RemoteFM1IOS
 * 3150dbb, RemoteFM1Android a0d0fe6) */
#ifndef RFM1CS_TEST_DATA
#define RFM1CS_TEST_DATA "tests/rmvp"
#endif
static uint32_t slurp(const char *name, uint8_t *b, uint32_t cap)
{
    char path[512];
    FILE *f;
    size_t n;
    snprintf(path, sizeof path, "%s/%s", RFM1CS_TEST_DATA, name);
    f = fopen(path, "rb");
    if (!f)
        return 0;
    n = fread(b, 1, cap, f);
    fclose(f);
    return (uint32_t)n;
}
static void bad_file(const uint8_t *b, uint32_t n, const char *want)
{
    rfm1cs_file_error_t err;
    char out[200];
    const int r = rfm1cs_file_unpack(b, n, NULL, NULL, &err);
    rfm1cs_file_error_text(&err, NULL, out, sizeof out);
    CHECKF(r < 0 && r == -err.code && rfm1cs_file_check(b, n, NULL) == r && !strcmp(out, want), "\"%s\", want \"%s\"",
           out, want);
}
static void test_file(void)
{
    static uint8_t f[RFM1CS_FILE_MAX + 16], g[RFM1CS_FILE_MAX + 16], ba[RFM_MOVE_PROJECT_MAX], bb[RFM_MOVE_PROJECT_MAX];
    static const char *const hosts[] = { "web", "ios", "android" };
    rfm1cs_file_entry_t e[RFM1CS_SLOTS];
    rfm1cs_file_error_t err;
    const uint32_t na = slurp("blob_a.bin", ba, sizeof ba), nb = slurp("blob_b.bin", bb, sizeof bb);
    uint32_t n, m;
    int h, kind = 0, r;
    char name[64], out[200];
    CHECKF(na > 0 && na == nb && rfm_move_project_check(ba, na) == 0 && rfm_move_project_check(bb, nb) == 0,
           "the fixtures' blobs (%u, %u: run from the build directory, or set RFM1CS_TEST_DATA)", na, nb);
    for (h = 0; h < 3; h++) {
        snprintf(name, sizeof name, "%s.rfmmove", hosts[h]);
        n = slurp(name, f, sizeof f);
        r = rfm1cs_file_unpack(f, n, &kind, e, &err);
        CHECKF(r == 1 && kind == RFM1CS_FILE_ONE && e[0].slot == 3 && e[0].saved == 1794909600u && e[0].len == na &&
                   !memcmp(e[0].bytes, ba, na),
               "%s: one project, slot 4, 2026-11-17 10:00 (%d, %s)", name, r, (rfm1cs_file_error_text(&err, 0, out, sizeof out), out));
        m = rfm1cs_file_pack(e, r, 0, g, sizeof g);
        CHECKF(m == n && !memcmp(f, g, n) && rfm1cs_file_size(e, r) == n, "%s: packed again, the same bytes", name);
        snprintf(name, sizeof name, "%s.rfmmoveset", hosts[h]);
        n = slurp(name, f, sizeof f);
        r = rfm1cs_file_unpack(f, n, &kind, e, &err);
        CHECKF(r == 2 && kind == RFM1CS_FILE_SET && e[0].slot == 0 && e[1].slot == 5 && e[0].saved == 1794909600u &&
                   e[1].saved == 1794990615u && !memcmp(e[0].bytes, ba, na) && !memcmp(e[1].bytes, bb, nb) &&
                   e[1].bytes[RFM1CS_COLOR_BYTE] == 7,
               "%s: a set, slots 1 and 6 (%d)", name, r);
        m = rfm1cs_file_pack(e, r, RFM1CS_FILE_SET, g, sizeof g);
        CHECKF(m == n && !memcmp(f, g, n) && rfm1cs_file_check(g, m, NULL) == 0, "%s: packed again, the same bytes", name);
    }
    /* pack: too small, the kind by the count */
    {
        rfm1cs_file_entry_t one = { 3, 1794909600u, ba, na };
        CHECK(rfm1cs_file_pack(&one, 1, 0, g, na + 17) == 0 && rfm1cs_file_pack(&one, 1, 0, g, na + 18) == na + 18 &&
              g[5] == RFM1CS_FILE_ONE);
        CHECK(rfm1cs_file_pack(&one, 1, 0, NULL, 0) == 0 && rfm1cs_file_pack(NULL, -1, 0, g, sizeof g) == 0);
    }
    /* refused, in the web player's words (its unpackFile's messages for the same files) */
    n = slurp("web.rfmmove", f, sizeof f);
#define MUT(stmt, want) do { memcpy(g, f, n); stmt; bad_file(g, n, want); } while (0)
    bad_file(f, n - 1, "Not a Move project file: project 1 is cut off.");
    memcpy(g, f, n), g[n] = 0;
    bad_file(g, n + 1, "Not a Move project file: 1 bytes after the last project.");
    MUT(g[0] = 0x58, "Not a Move project file: it does not start with RMVP.");
    memset(g, 0, RFM1CS_FILE_MAX + 1);                 /* (RFM_MOVE_PROJECT_MAX 39200: the web had 39072 written down) */
    CHECK(RFM1CS_FILE_MAX == 8 + 32 * (10 + 39200));
    bad_file(g, RFM1CS_FILE_MAX + 1, "Not a Move project file: it is 1254729 bytes, more than a full set (1254728).");
    MUT(g[40] ^= 0x55, "Not a Move project file: project 1 (slot 4) is damaged.");
    bad_file(f, 8, "Not a Move project file: project 1 is cut off.");
    bad_file(f, 0, "Not a Move project file: it does not start with RMVP.");
    bad_file(NULL, 0, "Not a Move project file: nothing to read.");
    MUT(g[4] = 2, "Not a Move project file: version 2; this page reads version 1.");
    MUT(g[5] = 9, "Not a Move project file: kind 9.");
    MUT(g[6] = 0, "Not a Move project file: 0 projects.");
    MUT(g[6] = 2, "Not a Move project file: 2 projects.");
    MUT(g[8] = 40, "Not a Move project file: slot 41 out of range.");
    MUT((g[14] = g[15] = g[16] = g[17] = 0), "Not a Move project file: project 1 is 0 bytes.");
    MUT((g[14] = g[15] = 0xFF), "Not a Move project file: project 1 is 65535 bytes.");
    bad_file(f, 12, "Not a Move project file: project 1 is cut off.");
#undef MUT
    {
        rfm1cs_file_entry_t two[2] = { { 2, 0, ba, na }, { 2, 0, ba, na } };
        m = rfm1cs_file_pack(two, 2, RFM1CS_FILE_SET, g, sizeof g);
        bad_file(g, m, "Not a Move project file: slot 3 twice.");
        rfm1cs_file_unpack(g, m, NULL, NULL, &err);
        err.code = RFM1CS_FILE_E_VERSION, err.value = 3;
        rfm1cs_file_error_text(&err, "app", out, sizeof out);
        CHECK(!strcmp(out, "Not a Move project file: version 3; this app reads version 1."));
    }
    /* every truncation refused cleanly */
    {
        uint32_t i, crashed = 0;
        n = slurp("web.rfmmoveset", f, sizeof f);
        for (i = 0; i < n; i++)
            crashed += rfm1cs_file_check(f, i, NULL) == 0;
        CHECKF(crashed == 0 && rfm1cs_file_check(f, n, NULL) == 0, "every truncation of a set refused (%u)", n);
    }
}

int main(void)
{
    test_handshake();
    test_give_up_bye_garbled();
    test_leds_screen_input();
    test_sequencer_projects();
    test_attempt();
    test_frame_pacing();
    test_words();
    test_file();
    return check_done("session");
}
