/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm1cs_session: the link to an Ableton Move running Move Remote, written once for every host. C99, no allocation,
 * no OS calls, no threads, no transport: the host moves the bytes and tells the time, the session does the protocol
 * (Move Remote's docs/PROTOCOL.md: u16 len | u8 type | payload) and drives the core (rfm_move_t, which it owns).
 *
 *   rfm1cs_session_t s;  rfm1cs_session_init(&s, settings, n, "My host", on_event, ctx);
 *   rfm1cs_session_start(&s, now)          -> RFM1CS_EV_OPEN: open the transport (TCP to <move>:7720, a WebSocket..)
 *   transport open:   rfm1cs_session_opened(&s, now)          (the session says HELLO)
 *   bytes received:   rfm1cs_session_rx(&s, now, bytes, n)    (any chunking)
 *   transport gone:   rfm1cs_session_closed(&s, now, final)
 *   every few ms:     rfm1cs_session_tick(&s, now)            (or at rfm1cs_session_next_ms(&s, now))
 *   the FM-1's state: rfm1cs_session_panel(&s, levels44, play_green), _frame(&s, now, rgb565), the core's own
 *                     rfm_move_set_master / _set_label / _set_keymap (then _redraw / _flush_leds)
 *   the audio clock:  rfm1cs_session_advance(&s, now, frames, rate) per render chunk; the firmware's MIDI out:
 *                     rfm1cs_session_midi_in(&s, now, msg, n)
 *   projects:         answer RFM_MOVE_ACT_PROJECT_* actions yourself, or hand them to store.h
 *   rfm1cs_session_stop(&s)                -> everything let go, RFM1CS_EV_CLOSE
 *
 * Everything comes out through the one callback, in order, synchronously, inside the call that caused it:
 *   RFM1CS_EV_SEND      data/len: one whole message to write to the transport
 *   RFM1CS_EV_OPEN      open the transport now (a first try or a retry); RFM1CS_EV_CLOSE: close it
 *   RFM1CS_EV_ACTION    act: the core's action (rfm_move_act_t), passed through; for RFM_MOVE_ACT_SETTINGS data/len
 *                       is the settings image to keep (rfm_move_save); bpm10 is the tempo when it happened (CLICK,
 *                       AUDIO_REC: the take's bars are act->index, its frame act->frame)
 *   RFM1CS_EV_RELEASED  everything the Move held was let go (a lost link, a stop): cut any queued timed MIDI
 *   RFM1CS_EV_LOST      reason (RFM1CS_WHY_*), was_live: the link went; RETRYING or FAILED follows in a STATUS
 *   RFM1CS_EV_BYE       reason (MR_BYE_*), was_live: the Move ended the link (not SHUTDOWN, a LOST); state OFF
 *   RFM1CS_EV_STATUS    changed (RFM1CS_CH_* bits): read rfm1cs_session_status()
 *
 * Threading: none assumed. Every call must come from one thread at a time (a host with a network thread queues the
 * bytes for the thread that owns the session, which is usually the engine's: the core's sequencer runs there).
 * The callback may call rfm1cs_session_stop() (e.g. on LOST); it must not call the other session functions, nor the
 * core's, while an RFM1CS_EV_ACTION from inside the core is being delivered. Data pointers are valid during the
 * callback only. Times are milliseconds on any monotonic clock (uint32_t, wrapping is fine). */
#ifndef RFM1CS_SESSION_H
#define RFM1CS_SESSION_H
#include <stdint.h>

#include "rfm1cs/move.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the link's timing, as the web player has always had it */
#define RFM1CS_PING_MS 1000u      /* PING this often while connected */
#define RFM1CS_SILENT_MS 3000u    /* nothing heard this long (STATUS comes every 500 ms): lost */
#define RFM1CS_HELLO_MS 4000u     /* from opening the transport to the Move's HELLO */
#define RFM1CS_SCREEN_MS 33u      /* the Move's screen at most ~30 a second, and only when it changed */
#define RFM1CS_NRETRY 3
#define RFM1CS_RX_MAX (2 + 16384)  /* the longest message the session reads whole (u16 len + type + payload) */
extern const uint32_t rfm1cs_retry_ms[RFM1CS_NRETRY];   /* 1000, 2000, 4000 (then 4000 again) */

enum {                            /* rfm1cs_status_t.state */
    RFM1CS_OFF = 0,               /* not wanted (never started, stopped, or the Move said BYE) */
    RFM1CS_CONNECTING = 1,        /* the transport opening, or waiting for the Move's HELLO */
    RFM1CS_CONNECTED = 2,         /* live: input, LEDs, screen, pings */
    RFM1CS_RETRYING = 3,          /* lost; the next try in retry_ms */
    RFM1CS_FAILED = 4,            /* gave up (see give_up, the final close, a protocol version mismatch) */
};
enum {                            /* why the link went (RFM1CS_EV_LOST) */
    RFM1CS_WHY_CLOSED = 1,        /* the host said the transport closed or failed to open (rfm1cs_session_closed) */
    RFM1CS_WHY_NO_ANSWER = 2,     /* no HELLO within the wait */
    RFM1CS_WHY_SILENT = 3,        /* nothing heard for RFM1CS_SILENT_MS */
    RFM1CS_WHY_GARBLED = 4,       /* a message that does not parse (a zero length, one too long) */
    RFM1CS_WHY_NOT_MOVE = 5,      /* the HELLO is not a Move Remote one */
    RFM1CS_WHY_SHUTDOWN = 6,      /* BYE: the Move is powering off (it is tried again) */
    RFM1CS_WHY_PROTO = 7,         /* the Move speaks another protocol version (status.proto): no retry */
};
enum {                            /* RFM1CS_EV_STATUS changed bits */
    RFM1CS_CH_STATE = 1 << 0,     /* state, retries, retry_ms */
    RFM1CS_CH_HELLO = 1 << 1,     /* name, via, proto, caps (a new connection) */
    RFM1CS_CH_BATTERY = 1 << 2,   /* battery, power, jacks */
    RFM1CS_CH_PLAYING = 1 << 3,
    RFM1CS_CH_PROJECT = 1 << 4,   /* the open project's slot */
    RFM1CS_CH_CLOCK = 1 << 5,     /* following (the firmware's clock drives the sequencer), bpm10 */
    RFM1CS_CH_MODE = 1 << 6,
    RFM1CS_CH_WAITING = 1 << 7,   /* waiting: the transport not open after the wait (patient mode) */
};
enum {
    RFM1CS_EV_SEND = 1, RFM1CS_EV_OPEN = 2, RFM1CS_EV_CLOSE = 3, RFM1CS_EV_ACTION = 4, RFM1CS_EV_RELEASED = 5,
    RFM1CS_EV_LOST = 6, RFM1CS_EV_BYE = 7, RFM1CS_EV_STATUS = 8,
};

typedef struct {
    int type;                     /* RFM1CS_EV_* */
    const uint8_t *data;          /* SEND: the message; ACTION SETTINGS: the settings image */
    uint32_t len;
    const rfm_move_act_t *act;    /* ACTION */
    int reason;                   /* LOST: RFM1CS_WHY_*; BYE: MR_BYE_* */
    int was_live;                 /* LOST, BYE: it was connected */
    uint32_t changed;             /* STATUS: RFM1CS_CH_* */
    int bpm10;                    /* ACTION: the sequencer's tempo then */
} rfm1cs_event_t;
typedef void (*rfm1cs_event_fn)(void *ctx, const rfm1cs_event_t *ev);

typedef struct {
    int state;                    /* RFM1CS_* */
    int ever_live;                /* connected at least once since start() */
    int waiting;                  /* patient mode: the wait passed with the transport still opening */
    int retries;                  /* retries scheduled since the last connection */
    uint32_t retry_ms;            /* RETRYING: the wait before the next try */
    /* the Move (its HELLO; kept after the link goes, so a host can still name it) */
    char name[25];
    int via;                      /* MR_VIA_* as the Move said it (0 Wi-Fi, 1 USB, 2 this computer), -1 none */
    int proto, caps;
    /* its STATUS (every 500 ms) */
    int battery;                  /* as the surface reports it, -1 unknown */
    int power, jacks, led_pending;
    /* the core */
    int mode;                     /* RFM_MOVE_MODE_* */
    int project;                  /* the open project's slot */
    int playing, following, bpm10;
    /* counters (since init) */
    uint32_t connects, inputs, led_writes, led_msgs, screens, pings, pongs, sent, midi, rx_bytes;
} rfm1cs_status_t;

typedef struct rfm1cs_session {
    rfm_move_t core;              /* the core: the host may call its rfm_move_* functions between session calls */
    rfm1cs_status_t st;
    rfm1cs_event_fn fn;
    void *ctx;
    char host_name[32];
    int want, live, opened, patient, give_up, deliver;
    uint32_t last_rx, hello_at, next_ping, retry_at, shot_at, token;
    int hello_due, retry_due, shot_ok, screen_due, dirty, timed_screen, shown_ok;
    uint8_t shown[MR_SCREEN_BYTES];
    uint8_t scr[MR_SCREEN_BYTES];
    rfm_move_led_t leds[RFM_MOVE_NLEDS];
    uint8_t settings[RFM_MOVE_SETTINGS_BYTES];
    uint8_t tx[3 + MR_SCREEN_BYTES];      /* one message being sent (the longest: DISPLAY; LED_RGB is 3 + 92 x 5) */
    uint32_t rx_len;              /* the deframer: a partial message */
    uint8_t rx[RFM1CS_RX_MAX];    /* (HELLO 46, STATUS 32, INPUT n x 8: longer ones, AUDIO, are skipped) */
    uint32_t rx_skip;             /* bytes of a skipped message still to come */
} rfm1cs_session_t;

/* the core made with these settings (an rfm_move_save image; NULL / not one: the defaults); host_name goes in the
 * HELLO (31 bytes at most). Nothing is sent until start(). */
void rfm1cs_session_init(rfm1cs_session_t *s, const uint8_t *settings, uint32_t n, const char *host_name,
                         rfm1cs_event_fn fn, void *ctx);
static inline rfm_move_t *rfm1cs_session_core(rfm1cs_session_t *s) { return &s->core; }
static inline const rfm1cs_status_t *rfm1cs_session_status(const rfm1cs_session_t *s) { return &s->st; }

/* ---- the link */
void rfm1cs_session_start(rfm1cs_session_t *s, uint32_t now);   /* wanted: a fresh first try (RFM1CS_EV_OPEN) */
void rfm1cs_session_stop(rfm1cs_session_t *s);   /* not wanted: the project flushed, all let go (always), closed */
/* this try's wait for the HELLO, from now (default RFM1CS_HELLO_MS from EV_OPEN); patient: when it passes with the
 * transport not yet open, only say so (status.waiting) and wait on (a browser asking the user about local network
 * access); once open, RFM1CS_HELLO_MS more */
void rfm1cs_session_wait(rfm1cs_session_t *s, uint32_t now, uint32_t ms, int patient);
/* tries that fail before the first connection, then FAILED (-1, the default: never give up; the web uses 2) */
void rfm1cs_session_give_up(rfm1cs_session_t *s, int tries);
void rfm1cs_session_opened(rfm1cs_session_t *s, uint32_t now);
void rfm1cs_session_rx(rfm1cs_session_t *s, uint32_t now, const uint8_t *bytes, uint32_t n);
/* the transport closed or never opened; final: it cannot be opened again (no retry: FAILED) */
void rfm1cs_session_closed(rfm1cs_session_t *s, uint32_t now, int final);
void rfm1cs_session_tick(rfm1cs_session_t *s, uint32_t now);
/* ms until the next thing tick() must do (0: now), or -1: nothing pending */
int32_t rfm1cs_session_next_ms(const rfm1cs_session_t *s, uint32_t now);

/* ---- the FM-1's side */
void rfm1cs_session_panel(rfm1cs_session_t *s, const uint8_t levels[RFM_MOVE_NLEVELS], uint8_t play_green);
/* a new FM-1 screen (240 x 240 RGB565, host order): its thumbnail taken now, the Move's screen redrawn when due */
void rfm1cs_session_frame(rfm1cs_session_t *s, uint32_t now, const uint16_t *rgb565);
void rfm1cs_session_redraw(rfm1cs_session_t *s, uint32_t now);   /* after a label, the battery, a Sample level.. */
void rfm1cs_session_flush_leds(rfm1cs_session_t *s);              /* after a keymap change: the changed LEDs out */

/* ---- the sequencer */
/* the core's clock by frames at rate (per render chunk; or us with rate 1000000), then the status, LEDs, screen */
void rfm1cs_session_advance(rfm1cs_session_t *s, uint32_t now, uint32_t frames, uint32_t rate);
void rfm1cs_session_midi_in(rfm1cs_session_t *s, uint32_t now, const uint8_t *msg, int n);   /* F8 FA FB FC F2 */
/* after the host drove the core itself (rfm_move_seq_advance / _midi_in / _project_load ..): the status, the LEDs, the
 * screen if it moves with the clock */
void rfm1cs_session_pumped(rfm1cs_session_t *s, uint32_t now);
/* only the status from the core (a STATUS event for what changed), after the host drove it */
void rfm1cs_session_update(rfm1cs_session_t *s);

/* ---- projects (the core's PROJECT_* actions ask; the host, or store.h, answers) */
/* at start: which slots hold a project, and the one to open (blob NULL / 0: none); -1 when that blob could not be read
 * (it starts empty; the stored copy is not touched) */
int rfm1cs_session_projects_init(rfm1cs_session_t *s, uint32_t now, uint32_t dir_bits, int slot, const uint8_t *blob,
                                 uint32_t n);
/* RFM_MOVE_ACT_PROJECT_LOAD's answer (blob NULL / 0: nothing stored there); -1: not a valid project (nothing loaded:
 * answer again with NULL for an empty one) */
int rfm1cs_session_project_answer(rfm1cs_session_t *s, uint32_t now, int slot, const uint8_t *blob, uint32_t n);
void rfm1cs_session_flush(rfm1cs_session_t *s);       /* the autosave now, if the project changed (pagehide, quit) */
void rfm1cs_session_release(rfm1cs_session_t *s);     /* let go of everything held now (RFM1CS_EV_RELEASED) */

/* ---- the protocol's messages (exposed for tests and hosts that frame by hand) */
uint32_t rfm1cs_frame(uint8_t *out, int type, const uint8_t *payload, uint32_t n);   /* 3 + n bytes */
int rfm1cs_parse_hello(const uint8_t *p, uint32_t n, rfm1cs_status_t *st);          /* 0 ok, -1 not "MOVR" */

#ifdef __cplusplus
}
#endif
#endif
