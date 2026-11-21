/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* 0.7.0: the takeover (rfm_move_set_takeover, RFM_MOVE_ACT_TK_INPUT / TK_STATE; FM1VST docs/RFM1-TAKEOVER.md). Input
 * goes in as the Move's raw USB-MIDI packets (a fake Move); the takeover's actions are logged as text and compared
 * whole: "I:9068 64" a TK_INPUT (slot, the packet's bytes 1..3 in hex), "S1:7f/1" TK_STATE on (claims / flags) for
 * slot 1, "S0:-" off; "K", "B", "T", "M" the FM-1's key, button, turn, MASTER; "E:"/"V:" words said. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "move_int.h"
#include "moveremote/client.h"
#include "rfm1cs/move.h"
#include "move_fwb_data.h"

#define NFW ((int)(sizeof FWB / sizeof FWB[0]))

static char logb[16384];
static int bad_pkt;
static void logf_(const char *s) { snprintf(logb + strlen(logb), sizeof logb - strlen(logb), "%s|", s); }
static void on_act(void *ctx, const rfm_move_act_t *a)
{
    char b[300];
    (void)ctx;
    switch (a->kind) {
    case RFM_MOVE_ACT_TK_INPUT:
        if (a->len != 4 || a->data != a->pkt || (a->pkt[0] & 0x0F) != a->pkt[1] >> 4)
            bad_pkt++;
        snprintf(b, sizeof b, "I%d:%02x%02x %02x", a->slot, a->pkt[1], a->pkt[2], a->pkt[3]);
        break;
    case RFM_MOVE_ACT_TK_STATE:
        if (a->down)
            snprintf(b, sizeof b, "S%d:%x/%d", a->slot, a->tk_claims, a->tk_flags);
        else
            snprintf(b, sizeof b, "S%d:-", a->slot);
        break;
    case RFM_MOVE_ACT_KEY: snprintf(b, sizeof b, "K%d%c", a->index, a->down ? '+' : '-'); break;
    case RFM_MOVE_ACT_BTN: snprintf(b, sizeof b, "B%d%c", a->index, a->down ? '+' : '-'); break;
    case RFM_MOVE_ACT_TURN: snprintf(b, sizeof b, "T%d:%d", a->index, a->value); break;
    case RFM_MOVE_ACT_MASTER: snprintf(b, sizeof b, "M"); break;
    case RFM_MOVE_ACT_SAY:
        snprintf(b, sizeof b, "%c%s:%s", a->index == RFM_MOVE_SAY_EVENT ? 'E' : 'V',
                 a->kind == RFM_MOVE_ACT_SAY && a->slot ? "s" : "", (const char *)a->data);
        if (a->down)
            strcat(b, "!");
        break;
    default: return;                                    /* (settings, the library, FW_ACTIVE ..) */
    }
    logf_(b);
}
static void clear(void) { logb[0] = 0; }
#define LOG(want)                                                                                                   \
    do {                                                                                                            \
        CHECKF(!strcmp(logb, want), "log \"%s\", want \"%s\"", logb, want);                                         \
        clear();                                                                                                    \
    } while (0)

/* ---- the fake Move */
static rfm_move_t M;
static uint32_t now_ms;
static void raw(uint8_t st, uint8_t d1, uint8_t d2)
{
    const uint8_t pkt[4] = { (uint8_t)(st >> 4), st, d1, d2 };
    mr_event_t ev;
    mr_decode(pkt, 0, &ev);
    rfm_move_input(&M, &ev, now_ms += 5);
}
static void adv_ms(uint32_t ms)
{
    while (ms) {
        const uint32_t k = ms < 10 ? ms : 10;
        rfm_move_seq_advance(&M, k * 48, 48000);
        now_ms += k, ms -= k;
    }
}
static void pad(int p, int vel) { raw(vel ? 0x90 : 0x80, (uint8_t)(MR_NOTE_PAD0 + p), (uint8_t)vel); }
static void step(int s, int down) { raw(down ? 0x90 : 0x80, (uint8_t)(MR_NOTE_STEP0 + s), down ? 127 : 0); }
static void button(int cc, int down) { raw(0xB0, (uint8_t)cc, down ? 127 : 0); }
static void press(int cc) { button(cc, 1), button(cc, 0); }
static void touch(int k, int on) { raw(on ? 0x90 : 0x80, (uint8_t)k, on ? 127 : 0); }
static void knob(int k, int d) { raw(0xB0, (uint8_t)(MR_CC_KNOB1 + k), (uint8_t)(d & 127)); }
static void track(int t, int down) { button(MR_CC_TRACK1 - t, down); }
static void adv_ms(uint32_t ms);
/* a Track tap (0.8.0: a pause first, so two in a row are not a double click) */
static void tap_track(int t) { adv_ms(RFM_MOVE_DOUBLE_MS + 20), track(t, 1), track(t, 0); }
static void rest_pop(void) { adv_ms(RFM_MOVE_POPUP_MS + 50); }

static rfm_move_fw_t E[NFW];
static void fresh(int speech)
{
    static const char *const ids[RFM_MOVE_FW_SLOTS] = { "felucca", "melodee", "sloop" };
    int i;
    for (i = 0; i < NFW; i++) {
        E[i].id = FWB[i].id, E[i].name = FWB[i].name, E[i].version = FWB[i].version, E[i].installed = FWB[i].installed;
        E[i].desc = FWB[i].desc, E[i].thumb = FWB[i].thumb, E[i].cats = FWB[i].cats, E[i].cat = FWB[i].cat;
    }
    now_ms = 1000;
    rfm_move_init(&M, NULL, on_act, NULL);
    rfm_move_fw_list(&M, E, NFW);
    rfm_move_fw_slots(&M, ids);
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON);
    rfm_move_set_speech(&M, speech);
    rest_pop();
    clear();
}

static uint8_t FWSCR[MR_SCREEN_BYTES];
static uint8_t FWLED[RFM_MOVE_NLEDS][3];
static void fw_data(int slot)
{
    int i;
    for (i = 0; i < MR_SCREEN_BYTES; i++)
        FWSCR[i] = (uint8_t)(i * 7 + 1 + slot);
    for (i = 0; i < RFM_MOVE_NLEDS; i++)
        FWLED[i][0] = (uint8_t)(i < RFM_MOVE_L_WHITE0 ? i + 1 : 200), FWLED[i][1] = (uint8_t)(slot + 3), FWLED[i][2] = 9;
    rfm_move_tk_screen(&M, slot, FWSCR);
    rfm_move_tk_leds(&M, slot, (const uint8_t(*)[3])FWLED);
}
static int screen_is_fw(void)
{
    uint8_t o[MR_SCREEN_BYTES];
    rfm_move_screen(&M, NULL, o);
    return !memcmp(o, FWSCR, sizeof o);
}
/* every LED as the core wants it now (all written again: they come out in the list's order) */
static void leds(uint8_t w[RFM_MOVE_NLEDS][3])
{
    rfm_move_led_t o[RFM_MOVE_NLEDS];
    int i, n;
    rfm_move_leds_invalidate(&M);
    n = rfm_move_leds(&M, o, RFM_MOVE_NLEDS);
    CHECK(n == RFM_MOVE_NLEDS);
    for (i = 0; i < n; i++)
        w[i][0] = o[i].r, w[i][1] = o[i].g, w[i][2] = o[i].b;
}

#define NOTRK (RFM_MOVE_TK_ALL & ~RFM_MOVE_TK_TRACKS)

/* (0.8.0, proposal 2) a slot's layout menu: its Track held + a top-row pad (24 FM-1, 25 the takeover) */
static void lay_menu(int t, int p) { adv_ms(RFM_MOVE_DOUBLE_MS + 20), track(t, 1), pad(p, 100), pad(p, 0), track(t, 0); }
static void tk_enter(int t) { lay_menu(t, 25); }
static void tk_leave(int t) { lay_menu(t, 24); }

/* 1. in by Track 1 held + pad 2, out by Track 1 held + pad 1; a tap names it; the name, the words, the screen */
static void test_entry(void)
{
    fresh(1);
    tap_track(0);                                       /* (a tap: the slot's layout, named; no takeover) */
    LOG("V:Felucca, FM-1|");
    CHECK(rfm_move_mode(&M) == RFM_MOVE_MODE_FM1 && rfm_move_tk_shown(&M) == -1 && M.pop == POP_LAYOUT);
    tk_enter(0);                                        /* (no takeover: pad 2 dark, nothing) */
    CHECK(rfm_move_slot_layout(&M, 0) == RFM_MOVE_LAY_FM1 && rfm_move_tk_shown(&M) == -1);
    rest_pop();
    clear();
    rfm_move_set_takeover(&M, 0, NOTRK, "CHORDS");      /* (capable: nothing shown until chosen) */
    fw_data(0);
    LOG("");
    tk_enter(0);
    CHECKF(strstr(logb, "S0:7f/1|") && strstr(logb, "Chords, takeover|"), "%s", logb);
    clear();
    CHECK(rfm_move_mode(&M) == RFM_MOVE_MODE_TAKEOVER && M.s.mode == RFM_MOVE_MODE_FM1 && rfm_move_tk_shown(&M) == 0);
    CHECK(rfm_move_slot_layout(&M, 0) == RFM_MOVE_LAY_TK);
    CHECK(!strcmp(rfm_move_mode_name(RFM_MOVE_MODE_TAKEOVER), "TAKEOVER"));
    tap_track(0);                                       /* a tap in it (TRACKS free): stays, named and said again */
    LOG("V:Chords, takeover|");
    CHECK(rfm_move_tk_shown(&M) == 0 && M.pop == POP_TK && !screen_is_fw());   /* (the name over it, a second) */
    CHECK(rfm_move_screen_timed(&M));
    {
        char w[RFM_MOVE_SAY_MAX];
        rfm_move_words(&M, w, sizeof w);
        CHECK(!strcmp(w, "Chords, takeover"));
        rfm_move_set_takeover(&M, 0, NOTRK, "ChoralRoot");    /* a name in its own case: said as written */
        rfm_move_words(&M, w, sizeof w);
        CHECK(!strcmp(w, "ChoralRoot, takeover"));
        rfm_move_set_takeover(&M, 0, NOTRK, "CHORDS");
    }
    rest_pop();
    CHECK(screen_is_fw());
    clear();
    {                                                   /* Track 1 held in it: the layout menu takes the pads (none
                                                         * to the firmware) and the screen */
        char w[RFM_MOVE_SAY_MAX];
        track(0, 1);
        adv_ms(RFM_MOVE_TAP_MS + 50);
        CHECK(rfmv_hold_shown(&M, 0) && !screen_is_fw());
        rfm_move_words(&M, w, sizeof w);
        CHECKF(strstr(w, "layouts") != NULL, "\"%s\"", w);
        pad(3, 100), pad(3, 0);                         /* (a scale: slot 0's) */
        pad(30, 100), pad(30, 0);                       /* (pad 7: nothing) */
        CHECKF(strstr(logb, "I0") == NULL, "%s", logb);
        CHECK(rfm_move_tk_shown(&M) == 0 && !screen_is_fw());
        track(0, 0);
        clear();
        rest_pop();
        CHECK(rfm_move_tk_shown(&M) == 0 && screen_is_fw());
        pad(3, 100), pad(3, 0);                         /* (let go: the firmware's again) */
        LOG("I0:9047 64|I0:8047 00|");
    }
    {                                                   /* (settings: the slot's layout, the takeover) */
        uint8_t img[RFM_MOVE_SETTINGS_BYTES];
        rfm_move_settings_t s;
        rfm_move_save(&M.s, img);
        CHECK(rfm_move_load(&s, img, sizeof img) == 0 && s.mode == RFM_MOVE_MODE_FM1 && s.slot[0].lay == RFM_MOVE_LAY_TK);
    }
    tk_leave(0);                                        /* Track 1 held + pad 1: the FM-1 layout */
    CHECKF(!strncmp(logb, "S0:-|", 5), "%s", logb);
    clear();
    CHECK(rfm_move_mode(&M) == RFM_MOVE_MODE_FM1 && rfm_move_slot_layout(&M, 0) == RFM_MOVE_LAY_FM1);
    rest_pop();
    CHECK(!screen_is_fw());
    tap_track(0);                                       /* (a tap: the FM-1 layout still) */
    CHECK(rfm_move_tk_shown(&M) == -1);
    clear();
    tk_enter(0);                                        /* and in again */
    CHECKF(!strncmp(logb, "S0:7f/1|", 8), "%s", logb);
    clear();
    rfm_move_set_mode(&M, RFM_MOVE_MODE_FM1);           /* (the host's: the FM-1 layout itself) */
    LOG("S0:-|E:Felucca, FM-1 layout|V:Felucca, FM-1|");
    rfm_move_set_mode(&M, RFM_MOVE_MODE_TAKEOVER);
    LOG("S0:7f/1|E:Chords, takeover|V:Felucca, Chords|");
    rfm_move_set_slot_layout(&M, 0, RFM_MOVE_LAY_FM1);  /* (the host's: the slot's layout) */
    LOG("S0:-|E:Felucca, FM-1 layout|V:Felucca, FM-1|");
    rfm_move_set_slot_layout(&M, 0, RFM_MOVE_LAY_TK);
    LOG("S0:7f/1|E:Chords, takeover|V:Felucca, Chords|");
    rfm_move_set_mode(&M, RFM_MOVE_MODE_MIDI);
    CHECKF(!strncmp(logb, "S0:-|", 5), "%s", logb);
    clear();
    CHECK(M.s.mode == RFM_MOVE_MODE_MIDI && rfm_move_tk_shown(&M) == -1);
    rfm_move_set_takeover(&M, 1, 0, NULL);
    rfm_move_fw_set_active(&M, 1);
    clear();
    rfm_move_set_mode(&M, RFM_MOVE_MODE_TAKEOVER);      /* (a slot with none: refused) */
    CHECK(rfm_move_set_slot_layout(&M, 1, RFM_MOVE_LAY_TK) == -1);
    CHECK(rfm_move_mode(&M) == rfm_move_slot_layout(&M, 1) && rfm_move_tk_shown(&M) == -1);
    LOG("");
}

/* 2. claimed controls to the firmware only, the rest as in the FM-1 layout; releases before TK_STATE off */
static void test_routing(void)
{
    mr_event_t ev;
    fresh(0);
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_PADS | RFM_MOVE_TK_KNOBS | RFM_MOVE_TK_BUTTONS, "Grid");
    tk_enter(0);
    LOG("S0:4a/0|");
    pad(0, 100);                                        /* claimed: the firmware's packet, no FM-1 key */
    raw(0xA0, MR_NOTE_PAD0, 40);
    raw(0xD0, 33, 0);
    pad(0, 0);
    LOG("I0:9044 64|I0:a044 28|I0:d021 00|I0:8044 00|");
    knob(4, 1), knob(4, -1), touch(4, 1), touch(4, 0);
    LOG("I0:b04b 01|I0:b04b 7f|I0:9004 7f|I0:8004 00|");
    memset(&ev, 0, sizeof ev);                          /* (an event made by hand: its packet made up) */
    ev.type = MR_EV_KNOB, ev.index = 2, ev.value = -3;
    rfm_move_input(&M, &ev, now_ms += 5);
    ev.type = MR_EV_PAD, ev.index = 31, ev.value = 90;
    rfm_move_input(&M, &ev, now_ms += 5);
    ev.value = 0;
    rfm_move_input(&M, &ev, now_ms += 5);
    LOG("I0:b049 7d|I0:9063 5a|I0:8063 00|");
    press(MR_CC_SHIFT), press(MR_CC_PLAY), press(MR_CC_DOWN);
    LOG("I0:b031 7f|I0:b031 00|I0:b055 7f|I0:b055 00|I0:b036 7f|I0:b036 00|");
    step(0, 1), step(0, 0);                             /* not claimed: the FM-1's F3, as in FM-1 mode */
    raw(0xB0, MR_CC_JOG, 1);
    raw(0xB0, MR_CC_VOLUME, 1);
    LOG("K0+|K0-|T0:1|M|");
    CHECK(bad_pkt == 0);
    /* held when it closes (Track 2: slot 1 played): every release, then off */
    pad(5, 80), step(3, 1), touch(1, 1), button(MR_CC_SHIFT, 1), pad(6, 70);
    clear();
    tap_track(1);
    LOG("K6-|I0:8049 00|I0:804a 00|I0:b031 00|I0:8001 00|S0:-|");
    CHECK(rfm_move_fw_active(&M) == 1 && rfm_move_tk_shown(&M) == -1 && M.tk_pad == 0 && M.tk_touch == 0);
    pad(5, 0), pad(6, 0), touch(1, 0), button(MR_CC_SHIFT, 0);   /* (their releases: nobody's now) */
    step(3, 0);                                         /* (the step was the core's: FM-1's key let go at the switch) */
    CHECK(strstr(logb, "I0") == NULL);
    clear();
    /* a pad held in the FM-1 layout, let go in the takeover: the core's (no stuck key) */
    tap_track(0);                                       /* (slot 0 again: its takeover, as it was left) */
    LOG("S0:4a/0|");
    tk_leave(0);
    clear();
    pad(0, 100);
    tk_enter(0);
    pad(0, 0);
    LOG("K14+|S0:4a/0|K14-|");
    rfm_move_release_all(&M);                           /* (the link lost: the firmware's let go too) */
    pad(1, 50);
    clear();
    rfm_move_release_all(&M);
    LOG("I0:8045 00|");
}

/* 3. each slot keeps its layout: a Track tap plays the slot as it was left (its takeover or not) */
static void test_memory(void)
{
    fresh(1);
    rfm_move_set_takeover(&M, 0, NOTRK, "CHORDS");
    rfm_move_set_takeover(&M, 1, RFM_MOVE_TK_ALL, "Drums");
    tk_enter(0);
    rest_pop();
    clear();
    tap_track(3);                                       /* Track 4: the Mixer (the projects view: closed while up) */
    CHECK(M.seq.projects && rfm_move_tk_shown(&M) == -1);
    CHECKF(!strncmp(logb, "S0:-|", 5), "%s", logb);
    clear();
    tap_track(0);                                       /* Track 1: out of the view, slot 0 as it was left */
    CHECKF(!strncmp(logb, "S0:7f/1|E:Chords, takeover|", 27), "%s", logb);
    CHECK(!M.seq.projects && rfm_move_tk_shown(&M) == 0);
    tk_leave(0);                                        /* the FM-1 layout now */
    tap_track(3);
    rest_pop();
    clear();
    tap_track(0);                                       /* back: the FM-1 layout (as it was left) */
    CHECKF(strstr(logb, "S0:") == NULL && strstr(logb, "FM-1 layout|"), "%s", logb);
    CHECK(rfm_move_mode(&M) == RFM_MOVE_MODE_FM1 && M.pop == POP_LAYOUT && !M.seq.projects);
    /* Track 2 held + pad 2: slot 1's layout (held, not played); slot 0 stays as it is */
    lay_menu(1, 25);
    CHECK(rfm_move_slot_layout(&M, 1) == RFM_MOVE_LAY_TK && rfm_move_fw_active(&M) == 0 && rfm_move_tk_shown(&M) == -1);
    rest_pop();
    clear();
    tap_track(1);                                       /* Track 2: slot 1, its takeover */
    LOG("S1:ff/1|E:Drums, takeover|");
    CHECK(rfm_move_tk_shown(&M) == 1);
    /* slot 1 claims everything: Track 1 and Back are the firmware's; volume touched + Back: out */
    tap_track(0);
    press(MR_CC_BACK);
    LOG("I1:b02b 7f|I1:b02b 00|I1:b033 7f|I1:b033 00|");
    CHECK(rfm_move_fw_active(&M) == 1);
    touch(8, 1);
    button(MR_CC_BACK, 1);
    LOG("I1:9008 7f|I1:8008 00|S1:-|E:Melodee, FM-1 layout|V:Melodee, FM-1|");
    button(MR_CC_BACK, 0), touch(8, 0);                 /* (Back's release and the touch's: nobody's) */
    LOG("");
    CHECK(rfm_move_mode(&M) == RFM_MOVE_MODE_FM1 && rfm_move_slot_layout(&M, 1) == RFM_MOVE_LAY_FM1 && !M.seq.projects &&
          !M.back_down);
    tk_enter(1);                                        /* (in again; Track 1 now: the firmware's, no slot switch) */
    clear();
    track(0, 1);
    LOG("I1:b02b 7f|");
    track(0, 0);
    touch(8, 1), press(MR_CC_BACK), touch(8, 0);
    CHECK(rfm_move_fw_active(&M) == 1 && rfm_move_mode(&M) == RFM_MOVE_MODE_FM1);
    tk_enter(1);
    clear();
    rfm_move_fw_set_active(&M, 0);                      /* (the host's slot switch: closes it) */
    LOG("S1:-|E:Felucca, FM-1 layout|");
    CHECK(rfm_move_mode(&M) == RFM_MOVE_MODE_FM1);
    rfm_move_fw_set_active(&M, 1);                      /* (back: as it was left) */
    LOG("S1:ff/1|E:Drums, takeover|");
    /* volume + Back with VOLUME and BUTTONS free: out too, Back not the core's (no flip to the Sequencer) */
    rfm_move_set_takeover(&M, 1, RFM_MOVE_TK_PADS, "Drums");
    LOG("S1:2/1|");
    touch(8, 1), button(MR_CC_BACK, 1);
    CHECKF(strstr(logb, "S1:-|E:Melodee, FM-1 layout|") != NULL, "%s", logb);
    clear();
    button(MR_CC_BACK, 0), touch(8, 0);
    rest_pop();
    CHECK(!M.seq.projects && rfm_move_mode(&M) == RFM_MOVE_MODE_FM1);
    /* Down (the FM-1 layout's slot cycle, BUTTONS free) leaves the takeover too */
    tk_enter(1);
    clear();
    press(MR_CC_DOWN);
    CHECK(rfm_move_fw_active(&M) == 2 && rfm_move_tk_shown(&M) == -1);
    CHECK(!strncmp(logb, "S1:-|", 5));
}

/* 4. BUTTONS without TRACKS: Back held back */
static void test_back(void)
{
    fresh(0);
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_BUTTONS, "Keys");
    rfm_move_set_takeover(&M, 2, RFM_MOVE_TK_BUTTONS, "Other");
    tk_enter(0);
    LOG("S0:40/0|");
    button(MR_CC_BACK, 1);                              /* held back: nothing yet */
    LOG("");
    button(MR_CC_BACK, 0);                              /* let go, no Track: press and release together */
    LOG("I0:b033 7f|I0:b033 00|");
    CHECK(!M.seq.projects && M.s.mode != RFM_MOVE_MODE_SEQ);
    button(MR_CC_BACK, 1);                              /* held long: the same (no flip, no view) */
    adv_ms(800);
    button(MR_CC_BACK, 0);
    LOG("I0:b033 7f|I0:b033 00|");
    adv_ms(RFM_MOVE_DOUBLE_MS + 20);
    button(MR_CC_BACK, 1), track(2, 1), track(2, 0);    /* Back held + a Track 3 tap: the tap's (slot 2; the takeover
                                                         * of slot 0 closes) */
    LOG("S0:-|");
    CHECK(rfm_move_fw_active(&M) == 2 && rfm_move_tk_shown(&M) == -1);
    button(MR_CC_BACK, 0);                              /* (Back nothing for any firmware; no click) */
    LOG("");
    CHECK(!M.seq.projects && rfm_move_mode(&M) == RFM_MOVE_MODE_FM1);
    tap_track(0);                                       /* slot 0: as it was left */
    LOG("S0:40/0|");
    CHECK(rfm_move_tk_shown(&M) == 0);
    /* BUTTONS free: Back is the core's (a click: the Sequencer, which closes the takeover) */
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_PADS, "Keys");
    LOG("S0:2/0|");
    press(MR_CC_BACK);
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && rfm_move_tk_shown(&M) == -1 && !M.seq.projects);
    LOG("S0:-|");
    press(MR_CC_BACK);                                  /* back to the slot's layout: the takeover again, named */
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && rfm_move_tk_shown(&M) == 0 && M.pop == POP_TK);
    LOG("S0:2/0|");
}

/* 5. the LEDs: the claimed ones the firmware's, the rest FM-1 mode's */
static void test_leds(void)
{
    uint8_t on[RFM_MOVE_NLEDS][3], off[RFM_MOVE_NLEDS][3];
    int i, bad_c = 0, bad_u = 0;
    const unsigned cl = RFM_MOVE_TK_PADS | RFM_MOVE_TK_BUTTONS | RFM_MOVE_TK_KNOBS;
    fresh(0);
    rfm_move_set_takeover(&M, 0, cl, "Lights");
    fw_data(0);
    tk_enter(0);
    rest_pop();
    leds(on);
    rfm_move_set_mode(&M, RFM_MOVE_MODE_FM1);
    rest_pop();
    leds(off);
    for (i = 0; i < RFM_MOVE_NLEDS; i++) {
        const int claimed = i < 32 || (i >= 48 && i <= 50) || (i >= 55 && i <= 75);
        if (claimed) {
            const int white = i >= RFM_MOVE_L_WHITE0;
            if (white ? !(on[i][0] == 100 && !on[i][1] && !on[i][2]) : memcmp(on[i], FWLED[i], 3) != 0)
                bad_c++;
        } else if (memcmp(on[i], off[i], 3))
            bad_u++;
    }
    CHECKF(bad_c == 0 && bad_u == 0, "claimed wrong %d, unclaimed changed %d", bad_c, bad_u);
    /* STEPS: the steps and the icons; TRACKS: the Track LEDs; the other slot's LEDs are not shown */
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_STEPS | RFM_MOVE_TK_TRACKS, "Lights");
    rfm_move_set_mode(&M, RFM_MOVE_MODE_TAKEOVER);
    leds(on);
    CHECK(!memcmp(on[RFM_MOVE_L_STEP0 + 3], FWLED[RFM_MOVE_L_STEP0 + 3], 3));
    CHECK(on[RFM_MOVE_L_ICON0 + 15][0] == 100 && !memcmp(on[RFM_MOVE_L_TRACK0 + 2], FWLED[RFM_MOVE_L_TRACK0 + 2], 3));
    CHECK(!memcmp(on[RFM_MOVE_L_PAD0 + 3], off[RFM_MOVE_L_PAD0 + 3], 3) && !memcmp(on[RFM_MOVE_L_RING0], off[RFM_MOVE_L_RING0], 3));
    rfm_move_tk_leds(&M, 1, (const uint8_t(*)[3])off);  /* (slot 1's: not this one's) */
    leds(off);
    CHECK(!memcmp(on, off, sizeof on));
    /* a pad lit live: the next rfm_move_leds writes it alone */
    {
        rfm_move_led_t o[8];
        FWLED[RFM_MOVE_L_STEP0 + 5][0] = 250;
        rfm_move_tk_leds(&M, 0, (const uint8_t(*)[3])FWLED);
        CHECK(rfm_move_leds(&M, o, 8) == 1 && o[0].number == MR_NOTE_STEP0 + 5 && o[0].r == 250);
    }
}

/* 6. the screen: the firmware's with SCREEN, popups only for the name and the host's messages */
static void test_screen(void)
{
    uint8_t a[MR_SCREEN_BYTES], b[MR_SCREEN_BYTES];
    fresh(0);
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_PADS, "NoScreen");
    fw_data(0);
    rfm_move_screen(&M, NULL, a);                       /* the FM-1 layout's */
    tk_enter(0);
    rest_pop();
    rfm_move_screen(&M, NULL, b);
    CHECK(!memcmp(a, b, sizeof a));                     /* (SCREEN not claimed: FM-1 mode's screen) */
    knob(4, 2);                                         /* (an unclaimed knob: its FM-1 popup as ever) */
    rfm_move_screen(&M, NULL, b);
    CHECK(memcmp(a, b, sizeof a) && M.pop == POP_ENC);
    rest_pop();
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_SCREEN | RFM_MOVE_TK_PADS, "NoScreen");   /* (now it is) */
    CHECK(screen_is_fw());
    knob(4, 2);                                         /* (the knob still turns the FM-1, its popup not shown) */
    CHECK(screen_is_fw());
    rfm_move_popup(&M, "Load failed");                  /* the host's message over it */
    CHECK(!screen_is_fw());
    rest_pop();
    CHECK(screen_is_fw());
    FWSCR[100] ^= 0xFF;                                 /* a new frame */
    rfm_move_tk_screen(&M, 0, FWSCR);
    CHECK(screen_is_fw());
    tk_leave(0);                                        /* the FM-1 layout: its own again */
    rest_pop();
    rfm_move_screen(&M, NULL, b);
    CHECK(!screen_is_fw());
    {                                                   /* the name popup (a tap in it): two rows at 2x, the whole
                                                         * screen */
        uint8_t n[MR_SCREEN_BYTES], l[MR_SCREEN_BYTES];
        tk_enter(0), rest_pop(), tap_track(0);
        CHECK(M.pop == POP_TK);
        rfm_move_screen(&M, NULL, n);
        rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_PADS, "NoScreen");
        rest_pop(), tap_track(0);
        CHECK(M.pop == POP_TK);
        rfm_move_screen(&M, NULL, l);
        CHECK(!memcmp(n, l, sizeof n));                 /* (the same over either screen: the whole screen) */
    }
}

/* 7. words: the firmware's own, only while shown and speech is open; the speech flag */
static void test_speech(void)
{
    fresh(1);
    rfm_move_set_takeover(&M, 1, NOTRK, "CHORDS");
    rfm_move_tk_say(&M, 1, "Chord D minor seven", 1);   /* (not shown: nothing) */
    LOG("");
    rfm_move_fw_set_active(&M, 1);
    tk_enter(1);
    clear();
    rfm_move_tk_say(&M, 1, "Chord D minor seven", 1);
    rfm_move_tk_say(&M, 1, "Arp, one eighth", 0);
    rfm_move_tk_say(&M, 0, "Not mine", 0);
    rfm_move_tk_say(&M, 1, "  \x01 ", 0);               /* (no words: nothing) */
    LOG("Vs:Chord D minor seven!|Es:Arp, one eighth|");
    rfm_move_set_speech(&M, 0);                         /* off: the flag, and no words */
    LOG("S1:7f/0|");
    rfm_move_tk_say(&M, 1, "Chord", 1);
    LOG("");
    rfm_move_set_speech(&M, 1);
    LOG("S1:7f/1|");
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_OFF);   /* (the mode Off: the flag too) */
    LOG("S1:7f/0|");
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_JOG);   /* Jog: the flag on; words only with the jog touched */
    LOG("S1:7f/1|");
    rfm_move_tk_say(&M, 1, "Chord", 1);
    LOG("");
    touch(9, 1);                                        /* (JOG claimed: the touch is the firmware's too) */
    clear();
    rfm_move_tk_say(&M, 1, "Chord", 1);
    LOG("Vs:Chord!|");
}

/* 8. claims changing while it runs; claims 0 */
static void test_claims(void)
{
    fresh(0);
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_PADS | RFM_MOVE_TK_STEPS, "View");
    tk_enter(0);
    pad(2, 99), step(4, 1);
    clear();
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_PADS | RFM_MOVE_TK_KNOBS, "View");   /* STEPS dropped: its release */
    LOG("I0:8014 00|S0:a/0|");
    step(4, 0);                                         /* (nobody's) */
    step(4, 1), step(4, 0);                             /* (FM-1 mode's now) */
    knob(0, 1);
    LOG("K7+|K7-|I0:b047 01|");
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_PADS | RFM_MOVE_TK_KNOBS, "View");   /* (the same: nothing) */
    LOG("");
    rfm_move_set_takeover(&M, 0, 0, "View");            /* none now: closed, the FM-1 layout */
    LOG("I0:8046 00|S0:-|");
    CHECK(rfm_move_mode(&M) == RFM_MOVE_MODE_FM1 && rfm_move_slot_layout(&M, 0) == RFM_MOVE_LAY_FM1 && M.pop == POP_LAYOUT);
    pad(2, 0);
    tk_enter(0);                                        /* (Track 1's pad 2 offers nothing) */
    LOG("");
    CHECK(rfm_move_slot_layout(&M, 0) == RFM_MOVE_LAY_FM1);
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_PADS, "View");   /* (capable again: not opened by itself) */
    LOG("");
    CHECK(rfm_move_mode(&M) == RFM_MOVE_MODE_FM1);
    rfm_move_set_takeover(&M, 5, RFM_MOVE_TK_PADS, "x");   /* (no such slot) */
    rfm_move_set_takeover(&M, 0, 0x7FFF, "A name far longer than fifteen");
    CHECK(M.tk[0].claims == RFM_MOVE_TK_ALL && strlen(M.tk[0].name) == RFM_MOVE_TK_NAME - 1);
    LOG("");
}

/* 9. a Shift menu (Global Settings, Track 4 held + step 2) over it: closed while up */
static void test_menu(void)
{
    fresh(0);
    rfm_move_set_takeover(&M, 0, RFM_MOVE_TK_PADS, "Menu");
    tk_enter(0);
    track(3, 1), step(1, 1), step(1, 0), track(3, 0);   /* Track 4 held + step 2: Global Settings */
    CHECK(M.seq.smenu == RFM_MOVE_SM_GLOBAL && rfm_move_tk_shown(&M) == -1);
    LOG("S0:2/0|S0:-|");
    press(MR_CC_BACK);
    CHECK(!M.seq.smenu && rfm_move_tk_shown(&M) == 0);
    LOG("S0:2/0|");
}

int main(void)
{
    test_entry();
    test_routing();
    test_memory();
    test_back();
    test_leds();
    test_screen();
    test_speech();
    test_claims();
    test_menu();
    return check_done("move_takeover");
}
