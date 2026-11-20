/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* 0.6.0: words for a screen reader (rfm_move_set_speech, RFM_MOVE_ACT_SAY, rfm_move_words). Input goes in as the
 * Move's raw USB-MIDI packets (a fake Move); the SAY actions are logged as "E:words|" (an event: the view) and
 * "V:words|" (a value: a popup, the selection) and compared whole, case by case (the remoteFM14Move spec's plan). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "move_int.h"
#include "moveremote/client.h"
#include "rfm1cs/move.h"
#include "move_fwb_data.h"

#define NFW ((int)(sizeof FWB / sizeof FWB[0]))

/* ---- the actions: SAY ones into the log, the others into a digest (speech must change nothing else) */
static char said[16384];
static int nsaid, bad_words;
static uint32_t digest = 2166136261u;
static int nother, kinds[64];
static int panel_acts(void)                             /* actions that reach the FM-1 or the firmware */
{
    return kinds[RFM_MOVE_ACT_KEY] + kinds[RFM_MOVE_ACT_PRESS] + kinds[RFM_MOVE_ACT_BTN] + kinds[RFM_MOVE_ACT_TURN] +
           kinds[RFM_MOVE_ACT_MASTER] + kinds[RFM_MOVE_ACT_MIDI];
}
static void mix(const void *p, size_t n)
{
    const uint8_t *b = p;
    size_t i;
    for (i = 0; i < n; i++)
        digest = (digest ^ b[i]) * 16777619u;
}
static void on_act(void *ctx, const rfm_move_act_t *a)
{
    (void)ctx;
    if (a->kind == RFM_MOVE_ACT_SAY) {
        const char *w = (const char *)a->data;
        size_t n = strlen(w), i;
        if (n != a->len || n == 0 || n >= RFM_MOVE_SAY_MAX || w[n - 1] == ' ' || w[0] == ' ' || a->param ||
            (a->index != RFM_MOVE_SAY_VALUE && a->index != RFM_MOVE_SAY_EVENT))
            bad_words++;
        for (i = 0; i < n; i++)
            if (w[i] < 32 || w[i] > 126)
                bad_words++;
        nsaid++;
        snprintf(said + strlen(said), sizeof said - strlen(said), "%c:%s|", a->index == RFM_MOVE_SAY_EVENT ? 'E' : 'V', w);
        return;
    }
    nother++;
    if (a->kind < 64)
        kinds[a->kind]++;
    mix(&a->kind, 1), mix(&a->index, 1), mix(&a->down, 1), mix(&a->vel, 1), mix(&a->value, 2), mix(&a->n, 1);
    mix(a->bytes, 3), mix(&a->frame, 4), mix(&a->track, 1), mix(&a->slot, 1), mix(&a->fx, 1), mix(&a->param, 1);
    mix(a->mix, 5), mix(a->filt, sizeof a->filt), mix(&a->dest, 1);
    if (a->data && a->kind != RFM_MOVE_ACT_FW_LIBRARY && a->kind != RFM_MOVE_ACT_PROJECT_SAVE)
        mix(a->data, a->len);
}
static void clear(void) { said[0] = 0; }
#define SAID(want)                                                                                                  \
    do {                                                                                                            \
        CHECKF(!strcmp(said, want), "said \"%s\", want \"%s\"", said, want);                                        \
        clear();                                                                                                    \
    } while (0)

/* ---- the fake Move */
static rfm_move_t M, M2;
static uint32_t now_ms;
static void raw(rfm_move_t *m, uint8_t st, uint8_t d1, uint8_t d2)
{
    const uint8_t pkt[4] = { (uint8_t)(st >> 4), st, d1, d2 };
    mr_event_t ev;
    mr_decode(pkt, 0, &ev);
    rfm_move_input(m, &ev, now_ms += 5);
}
static void adv_ms(rfm_move_t *m, uint32_t ms) { while (ms) { const uint32_t k = ms < 10 ? ms : 10; rfm_move_seq_advance(m, k * 48, 48000); now_ms += k; ms -= k; } }
static void pad(rfm_move_t *m, int p, int vel) { raw(m, vel ? 0x90 : 0x80, (uint8_t)(MR_NOTE_PAD0 + p), (uint8_t)vel); }
static void tap_pad(rfm_move_t *m, int p) { pad(m, p, 100), pad(m, p, 0); }
static void step(rfm_move_t *m, int s, int down) { raw(m, down ? 0x90 : 0x80, (uint8_t)(MR_NOTE_STEP0 + s), down ? 127 : 0); }
static void tap_step(rfm_move_t *m, int s) { step(m, s, 1), step(m, s, 0); }
static void button(rfm_move_t *m, int cc, int down) { raw(m, 0xB0, (uint8_t)cc, down ? 127 : 0); }
static void press(rfm_move_t *m, int cc) { button(m, cc, 1), button(m, cc, 0); }
static void touch(rfm_move_t *m, int k, int on) { raw(m, on ? 0x90 : 0x80, (uint8_t)k, on ? 127 : 0); }
static void knob(rfm_move_t *m, int k, int d) { raw(m, 0xB0, (uint8_t)(MR_CC_KNOB1 + k), (uint8_t)(d & 127)); }
static void jog(rfm_move_t *m, int d) { raw(m, 0xB0, MR_CC_JOG, (uint8_t)(d & 127)); }
static void jog_press(rfm_move_t *m) { raw(m, 0xB0, MR_CC_JOG_CLICK, 127), raw(m, 0xB0, MR_CC_JOG_CLICK, 0); }
static void track(rfm_move_t *m, int t, int down) { button(m, MR_CC_TRACK1 - t, down); }
static void tap_track(rfm_move_t *m, int t) { track(m, t, 1), track(m, t, 0); }
static void rest_pop(rfm_move_t *m) { adv_ms(m, RFM_MOVE_POPUP_MS + 50); }
static int pad_of(int cell) { return (3 - cell / 8) * 8 + cell % 8; }
static int fxpad(int page, int row) { return pad_of(row * 8 + (page == 0 ? 0 : page == 1 ? 2 : 4)); }

/* ---- the catalog (the mockup's) and some modules */
static rfm_move_fw_t E[NFW];
static int idx(const char *id)
{
    int i;
    for (i = 0; i < NFW; i++)
        if (!strcmp(FWB[i].id, id))
            return i;
    return -1;
}
static const char *const W_RATE[] = { "1/8", "1/16", "1/32" }, *const W_MODE[] = { "Up", "Down", "Up/Dn", "Random" };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const rfm_move_fx_param_t P_ARP[] = { { "Rate", 3, W_RATE, 0, 0 }, { "Mode", 4, W_MODE, 0, 0 }, { "Gate", 0, NULL, 64, 0 } };
static const rfm_move_fx_module_t MFX[] = { { "Arp", 3, P_ARP } };
static const rfm_move_fx_param_t P_DLY[] = { { "Feedback", 0, NULL, 64, 0 }, { "Mix", 0, NULL, 127, 1 } };
static const rfm_move_fx_module_t AFX[] = { { "Delay", 2, P_DLY } };
#pragma GCC diagnostic pop
static const char LONGNAME[] = "A Firmware Name Forty Characters Long xx";   /* (40) */

static void fresh(rfm_move_t *m, int speech)
{
    static const char *const ids[RFM_MOVE_FW_SLOTS] = { "felucca", "melodee", "" };
    int i;
    for (i = 0; i < NFW; i++) {
        E[i].id = FWB[i].id, E[i].name = FWB[i].name, E[i].version = FWB[i].version, E[i].installed = FWB[i].installed;
        E[i].desc = FWB[i].desc, E[i].thumb = FWB[i].thumb, E[i].cats = FWB[i].cats, E[i].cat = FWB[i].cat;
        E[i].virt = 0;
    }
    E[idx("golf")].name = LONGNAME;
    now_ms = 1000;
    rfm_move_init(m, NULL, on_act, NULL);
    m->s.pop_half = 1;
    rfm_move_fw_list(m, E, NFW);
    rfm_move_fx_modules(m, RFM_MOVE_FX_MIDI, MFX, 1);
    rfm_move_fx_modules(m, RFM_MOVE_FX_AUDIO, AFX, 1);
    rfm_move_fw_slots(m, ids);
    rfm_move_set_speech(m, speech);
    clear();
}

/* 1. a script through every part (modes, knobs, the browser, FX setup, the sequencer): with speech off no word and
 * no cost; on, the same other actions and the same screen */
static void script(rfm_move_t *m)
{
    int i;
    touch(m, 4, 1), knob(m, 4, 3), knob(m, 4, 2), touch(m, 4, 0), rest_pop(m);
    knob(m, 8, 2), rest_pop(m);
    tap_track(m, 1), tap_track(m, 1), tap_track(m, 1);
    track(m, 1, 1), knob(m, 1, 9), knob(m, 0, 9), tap_pad(m, 31), track(m, 1, 0), rest_pop(m);
    tap_pad(m, 0), tap_pad(m, 31);
    tap_track(m, 2), knob(m, 0, 1), track(m, 2, 1), knob(m, 0, 1), track(m, 2, 0), rest_pop(m);
    tap_track(m, 0), press(m, MR_CC_BACK), tap_track(m, 1), tap_step(m, 1), tap_pad(m, 0), jog(m, 1), jog(m, -1);
    press(m, MR_CC_MUTE), tap_pad(m, 1), press(m, MR_CC_MUTE), press(m, MR_CC_MENU), touch(m, 0, 1), touch(m, 0, 0);
    pad(m, fxpad(0, 0), 100), jog(m, 1), pad(m, fxpad(0, 0), 0), knob(m, 0, 6), tap_track(m, 3), knob(m, 0, -3);
    press(m, MR_CC_MENU), press(m, MR_CC_BACK), press(m, MR_CC_BACK), rest_pop(m);
    tap_track(m, 3), pad(m, 0, 100), pad(m, 0, 0), tap_step(m, 0), press(m, MR_CC_PLAY), adv_ms(m, 300);
    press(m, MR_CC_PLAY), press(m, MR_CC_UNDO), track(m, 3, 1), tap_step(m, 2), track(m, 3, 0), press(m, MR_CC_MENU);
    for (i = 0; i < 4; i++)
        adv_ms(m, 400);
    tap_track(m, 0);
}
static void test_off_on(void)
{
    static uint8_t a[MR_SCREEN_BYTES], b[MR_SCREEN_BYTES];
    uint32_t d_off;
    int n_off;
    fresh(&M, 0);
    CHECK(rfm_move_speech(&M) == 0);
    digest = 2166136261u, nother = 0;
    script(&M);
    CHECK(said[0] == 0 && nsaid == 0);
    d_off = digest, n_off = nother;
    rfm_move_screen(&M, NULL, a);
    fresh(&M2, 1);
    CHECK(rfm_move_speech(&M2) == 1);
    digest = 2166136261u, nother = 0;
    script(&M2);
    CHECKF(nsaid > 20, "%d said", nsaid);
    if (getenv("SPEECH_LOG"))                           /* (SPEECH_LOG=1: what the script said, for a read) */
        fprintf(stderr, "%s\n", said);
    CHECK(digest == d_off && nother == n_off && n_off > 30);
    rfm_move_screen(&M2, NULL, b);
    CHECK(!memcmp(a, b, sizeof a));
    clear();
}

/* 2, 3, 15: the modes; a knob's popup closing says nothing more; the words on demand */
static void test_modes(void)
{
    char w[RFM_MOVE_SAY_MAX];
    fresh(&M, 1);
    tap_track(&M, 1);
    SAID("E:MIDI mode|");
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "MIDI mode"));
    tap_track(&M, 2);
    SAID("E:Control mode|");
    tap_track(&M, 3);
    SAID("E:Sequencer mode|");
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "Sequencer mode"));
    tap_track(&M, 0);
    SAID("E:FM-1 mode|");
    tap_track(&M, 1), tap_track(&M, 1);
    SAID("E:MIDI mode|E:MIDI mode, Notes|");
    tap_track(&M, 0);
    clear();
    /* a held Track: its menu's item, not the mode */
    track(&M, 1, 1);
    adv_ms(&M, 400);
    CHECK(said[0] == 0);
    track(&M, 1, 0);
    CHECK(said[0] == 0 && M.s.mode == RFM_MOVE_MODE_FM1);
    /* 3: knob 5 touched in FM-1 mode: its name; released, its popup gone: nothing more */
    touch(&M, 4, 1);
    SAID("V:K1|");
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "FM-1 mode, K1"));
    knob(&M, 4, 2);
    SAID("V:K1: +1|");
    touch(&M, 4, 0);
    rest_pop(&M);
    tap_pad(&M, 5);
    CHECK(said[0] == 0);
    rfm_move_set_mode(&M, RFM_MOVE_MODE_CONTROL);      /* (the host's call says it too) */
    SAID("E:Control mode|");
    rfm_move_set_mode(&M, RFM_MOVE_MODE_FM1);
    SAID("E:FM-1 mode|");
}

/* 4, 5, 6, 7: a readout, a held button's label, a host popup, the volume */
static void test_popups(void)
{
    const char *lbl[RFM_MOVE_LBL_N] = { NULL };
    char w[RFM_MOVE_SAY_MAX];
    fresh(&M, 1);
    rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "Voicing", "Wide", -1, 0, 0);
    SAID("V:Voicing: Wide|");
    rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "Voicing", "Wide", -1, 0, 0);
    SAID("");
    rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "Voicing", "Close", -1, 0, 0);
    SAID("V:Voicing: Close|");
    rest_pop(&M);
    rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "Voicing", "Close", -1, 0, 0);
    SAID("V:Voicing: Close|");                          /* (up again later: said again) */
    rest_pop(&M);
    rfm_move_readout(&M, -1, RFM_MOVE_RD_MESSAGE, "Saved", "", -1, 0, 0);
    SAID("V:Saved|");
    rest_pop(&M);
    /* a knob's readout: its label as the title */
    touch(&M, 4, 1);
    SAID("V:K1|");
    rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, "", "64", 500, 0, 0);
    SAID("V:K1: 64|");
    touch(&M, 4, 0), rest_pop(&M), clear();
    /* 5: SEL with a live label: (0.6.1) every press says it at once; held, its popup is not said again */
    lbl[RFM_MOVE_LBL_BTN0 + 1] = "SEL/KEYS";
    rfm_move_set_labels(&M, lbl);
    button(&M, MR_CC_SAMPLE, 1);
    adv_ms(&M, 100);
    button(&M, MR_CC_SAMPLE, 0);
    rest_pop(&M);
    SAID("V:SEL/KEYS|");                                /* (as the screen: words with no lower case stay) */
    button(&M, MR_CC_SAMPLE, 1);
    SAID("V:SEL/KEYS|");
    adv_ms(&M, RFM_MOVE_LABEL_HOLD_MS + 20);
    SAID("");
    adv_ms(&M, 500);
    CHECK(said[0] == 0);
    button(&M, MR_CC_SAMPLE, 0);
    rest_pop(&M), clear();
    /* 6: the host's popup */
    rfm_move_popup(&M, "FM-1 linked");
    SAID("V:FM-1 linked|");
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "FM-1 mode, FM-1 linked"));
    rest_pop(&M);
    /* 7: the volume a detent */
    knob(&M, 8, 1);
    {
        char want[64];
        snprintf(want, sizeof want, "V:Volume %d%%|", (M.master * 100 + 511) / 1023);
        SAID(want);
    }
    rest_pop(&M), clear();
}

/* 8, 9, 14: Control mode's knobs, MIDI's held menu, latch */
static void test_menus(void)
{
    fresh(&M, 1);
    tap_track(&M, 2);
    SAID("E:Control mode|");
    knob(&M, 0, 1);
    SAID("V:CC 16: 65|");
    rest_pop(&M);
    track(&M, 2, 1), knob(&M, 0, 1);
    SAID("V:Knob 1: CC 17|");
    track(&M, 2, 0), rest_pop(&M);
    CHECK(said[0] == 0);
    tap_track(&M, 1);
    SAID("E:MIDI mode|");
    track(&M, 1, 1);
    knob(&M, 1, RFM_MOVE_LIST_TICKS);
    SAID("V:Scale: Minor|");
    knob(&M, 0, RFM_MOVE_LIST_TICKS);
    SAID("V:Root: Db|");
    tap_step(&M, 2);
    SAID("V:Channel: 3|");
    tap_pad(&M, 31);                                    /* 14: latch */
    SAID("V:Latch on|");
    track(&M, 1, 0), rest_pop(&M);
    CHECK(said[0] == 0 && M.s.mode == RFM_MOVE_MODE_MIDI);
}

/* 10, 11, 15, 16: the browser, the catalog, a download; Back + Track */
static void test_browser(void)
{
    char want[400], w[RFM_MOVE_SAY_MAX];
    int ids[32], k, i, cell = -1, golf = idx("golf");
    fresh(&M, 1);
    press(&M, MR_CC_BACK);
    SAID("E:Firmwares, Slot 1, Felucca|");
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "Firmwares, Slot 1, Felucca"));
    tap_track(&M, 1);
    SAID("E:Slot 2, Melodee|");
    tap_track(&M, 2);
    SAID("E:Slot 3, empty|");
    tap_step(&M, 1);
    SAID("V:All firmwares|");
    rest_pop(&M);
    k = rfmv_fw_bank(&M, ids);
    CHECK(k > 2);
    tap_pad(&M, pad_of(0));
    snprintf(want, sizeof want, "V:%s|", E[ids[0]].name);
    SAID(want);
    tap_pad(&M, pad_of(0));                             /* (the same: nothing) */
    SAID("");
    for (i = 0; i < k; i++)                             /* 16: a 40-character name, said whole */
        if (ids[i] == golf)
            cell = i;
    CHECK(cell >= 0);
    if (cell >= 0) {
        tap_pad(&M, pad_of(cell));
        snprintf(want, sizeof want, "V:%s|", LONGNAME);
        SAID(want);
    }
    press(&M, MR_CC_MUTE);
    SAID("E:Catalog|");
    k = rfmv_fw_bank(&M, ids);
    for (i = 0, cell = -1; i < k && cell < 0; i++)
        if (!E[ids[i]].installed)
            cell = i;
    CHECK(cell >= 0);
    if (cell < 0)
        return;
    {
        const int fw = ids[cell];
        tap_pad(&M, pad_of(cell));
        snprintf(want, sizeof want, "V:%s, not installed|", E[fw].name);
        SAID(want);
        jog(&M, 1);
        CHECK(strlen(said) > 10 && !strncmp(said, "V:", 2) && !strncmp(said + 2, E[fw].desc, 20));
        CHECK(strlen(said) <= 2 + 250 + 1);
        clear();
        jog(&M, 1);
        SAID("");                                       /* (scrolling the description: said once) */
        jog(&M, -1), jog(&M, -1);
        snprintf(want, sizeof want, "V:%s, not installed|", E[fw].name);
        SAID(want);
        jog_press(&M);                                  /* the download */
        snprintf(want, sizeof want, "V:Downloading %s 0%%|", E[fw].name);
        SAID(want);
        rfm_move_fw_progress(&M, E[fw].id, 0), rfm_move_fw_progress(&M, E[fw].id, 10);
        rfm_move_fw_progress(&M, E[fw].id, 30), rfm_move_fw_progress(&M, E[fw].id, 40);
        snprintf(want, sizeof want, "V:Downloading %s 25%%|", E[fw].name);
        SAID(want);
        E[fw].installed = E[fw].version;
        rfm_move_fw_progress(&M, E[fw].id, 100);
        snprintf(want, sizeof want, "V:%s installed|", E[fw].name);
        SAID(want);
        rest_pop(&M);
        CHECK(rfm_move_words(&M, w, sizeof w) > 0);
        snprintf(want, sizeof want, "Catalog, %s, installed", E[fw].name);
        CHECKF(!strcmp(w, want), "%s", w);
        rfm_move_fw_progress(&M, E[fw].id, 50);         /* (no download under way: the selection's new state) */
        snprintf(want, sizeof want, "V:%s, installed|", E[fw].name);
        SAID(want);
    }
    for (i = 0, cell = -1; i < k && cell < 0; i++)
        if (!E[ids[i]].installed)
            cell = i;
    if (cell >= 0) {
        const int fw = ids[cell];
        tap_pad(&M, pad_of(cell)), jog_press(&M), clear();
        rfm_move_fw_progress(&M, E[fw].id, RFM_MOVE_FW_FAILED);
        snprintf(want, sizeof want, "V:%s failed|", E[fw].name);
        SAID(want);
        rest_pop(&M);
    }
    for (i = 0; i < 8 && M.seq.projects; i++)
        press(&M, MR_CC_BACK);
    CHECK(!M.seq.projects);
    CHECK(strstr(said, "E:FM-1 mode|") != NULL);
    clear();
    /* 11: Back + Track 2 in FM-1 mode */
    button(&M, MR_CC_BACK, 1), tap_track(&M, 1), button(&M, MR_CC_BACK, 0);
    SAID("V:Playing, Slot 2, Melodee|");
}

/* 12: FX setup */
static void test_fx(void)
{
    char w[RFM_MOVE_SAY_MAX];
    fresh(&M, 1);
    press(&M, MR_CC_BACK);
    clear();
    press(&M, MR_CC_MENU);
    SAID("E:FX setup, MIDI FX|V:MIDI FX 1: empty|");
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "FX setup, MIDI FX, MIDI FX 1: empty"));
    tap_pad(&M, fxpad(0, 1));
    SAID("V:MIDI FX 2: empty|");
    pad(&M, fxpad(0, 0), 100);
    SAID("V:MIDI FX 1: empty|");
    jog(&M, 1);
    SAID("V:Arp|");
    pad(&M, fxpad(0, 0), 0);
    SAID("V:MIDI FX 1: Arp|");
    rest_pop(&M);
    SAID("");
    touch(&M, 0, 1);
    SAID("V:Rate: 1/8|");
    knob(&M, 0, RFM_MOVE_LIST_TICKS);
    SAID("V:Rate: 1/16|");
    touch(&M, 0, 0);
    tap_step(&M, 2);
    SAID("V:MIDI FX 1: track 3|");
    rest_pop(&M);
    tap_track(&M, 1);
    SAID("E:Routing|V:Slot 1, Felucca|");
    tap_track(&M, 3);
    SAID("E:Mixer|V:Level|");
    knob(&M, 0, -1);
    {
        char want[64];
        snprintf(want, sizeof want, "V:Slot 1 Level: %d|", M.fw.level[0]);
        SAID(want);
    }
    rest_pop(&M);
    press(&M, MR_CC_MENU);
    SAID("E:Firmwares, Slot 1, Felucca|");
}

/* 13: the sequencer */
static void test_seq(void)
{
    fresh(&M, 1);
    tap_track(&M, 3);
    SAID("E:Sequencer mode|");
    press(&M, MR_CC_PLAY);
    SAID("V:Playing|");
    adv_ms(&M, 200);
    press(&M, MR_CC_PLAY);
    SAID("V:Stopped|");
    tap_pad(&M, 0);                                     /* a note, then a step: it is entered */
    clear();
    tap_step(&M, 4);
    SAID("V:Step 5 on|");                               /* (0.6.1) */
    step(&M, 4, 1);
    adv_ms(&M, RFM_MOVE_SEQ_HOLD_MS + 20);
    CHECKF(!strncmp(said, "V:Step 5: ", 10) && strstr(said, "empty") == NULL, "%s", said);
    step(&M, 4, 0), clear(), tap_step(&M, 6);
    SAID("V:Step 7 on|");
    rest_pop(&M), clear();
    press(&M, MR_CC_UNDO);
    SAID("V:Undo|V:Step 7 off|");
    press(&M, MR_CC_UNDO);
    SAID("V:Undo|V:Step 5 off|");                       /* (another one, its message still up: said again) */
    button(&M, MR_CC_SHIFT, 1), press(&M, MR_CC_UNDO), button(&M, MR_CC_SHIFT, 0);
    SAID("V:Shift|V:Redo|V:Step 5 on|");
    rest_pop(&M);
    track(&M, 3, 1), step(&M, 2, 1);
    SAID("V:Track 3|");
    step(&M, 2, 0), track(&M, 3, 0), rest_pop(&M);
    CHECK(said[0] == 0 && M.seq.sel_track == 2);
    press(&M, MR_CC_MENU);
    SAID("E:Session|");
    press(&M, MR_CC_MENU);
    SAID("E:Track 3|");
    rfm_move_seq_play(&M, 1);                           /* (the host's call) */
    SAID("V:Playing|");
    rfm_move_seq_play(&M, 0);
    SAID("V:Stopped|");
}

/* (0.6.1) every row of the spec, and the buttons: each must say something (HAS: the log holds the words) */
#define HAS(want)                                                                                                   \
    do {                                                                                                            \
        CHECKF(strstr(said, want) != NULL, "said \"%s\", want \"%s\" in it", said, want);                           \
        if (getenv("SPEECH_LOG"))                                                                                   \
            fprintf(stderr, "%-28s %s\n", want, said);                                                              \
        clear();                                                                                                    \
    } while (0)
static void test_buttons(void)
{
    const char *names[RFM_MOVE_NBTN] = { NULL };
    fresh(&M, 1);
    press(&M, MR_CC_PLAY);                              /* FM-1 mode: the FM-1's buttons, every press */
    HAS("V:Play|");
    press(&M, MR_CC_PLAY);
    HAS("V:Play|");
    press(&M, MR_CC_LEFT);
    HAS("V:Octave down|");
    press(&M, MR_CC_UP);
    HAS("V:Sequencer|");
    names[1] = "Select";
    rfm_move_set_button_names(&M, names);
    press(&M, MR_CC_SAMPLE);
    HAS("V:Select|");
    button(&M, MR_CC_CAPTURE, 1), button(&M, MR_CC_REC, 1);   /* (a second held: its label popup at once, once) */
    SAID("V:FX|V:Record|");
    button(&M, MR_CC_REC, 0), button(&M, MR_CC_CAPTURE, 0), rest_pop(&M), clear();
    tap_track(&M, 1);                                   /* the MIDI modes: the arrows' octave */
    clear();
    press(&M, MR_CC_UP);
    SAID("V:Octave 1|");
    press(&M, MR_CC_DOWN);
    SAID("V:Octave 0|");
    tap_track(&M, 2), clear();                          /* Control mode: the jog's bank */
    jog(&M, 1);
    SAID("V:Bank 2|");
}
static void test_rows(void)
{
    int i;
    fresh(&M, 1);
    /* MIDI mode: the drum pad's note (pad held + jog), the held menus' layout, velocity */
    tap_track(&M, 1), clear();
    track(&M, 1, 1), tap_pad(&M, 26), track(&M, 1, 0), rest_pop(&M);
    HAS("Layout: Drum");
    pad(&M, 0, 100), jog(&M, 1);
    HAS("V:Pad 1: ");
    pad(&M, 0, 0), rest_pop(&M), clear();
    /* Sequencer mode */
    tap_track(&M, 3);
    HAS("E:Sequencer mode|");
    button(&M, MR_CC_SHIFT, 1), tap_step(&M, 4), button(&M, MR_CC_SHIFT, 0);   /* Shift + step 5: Tempo */
    HAS("E:Tempo|");
    knob(&M, 0, 1);
    HAS("V:Tempo 121|");
    press(&M, MR_CC_BACK);
    HAS("E:Track 1|");                                  /* (the menu closed: the view again) */
    button(&M, MR_CC_SHIFT, 1), tap_step(&M, 10), button(&M, MR_CC_SHIFT, 0);  /* Shift + step 11: Arp */
    HAS("E:Arp|");
    knob(&M, 0, RFM_MOVE_LIST_TICKS);
    HAS("V:Pattern: Down|");
    knob(&M, 4, RFM_MOVE_LIST_TICKS);
    HAS("V:Arp: On|");
    jog(&M, 1);
    HAS("V:Mod lane|");
    press(&M, MR_CC_BACK), clear();
    button(&M, MR_CC_SHIFT, 1), tap_step(&M, 6), button(&M, MR_CC_SHIFT, 0);   /* Groove */
    HAS("E:Groove|");
    jog(&M, 1);
    HAS("V:Groove: 5%|");
    press(&M, MR_CC_BACK), clear();
    button(&M, MR_CC_SHIFT, 1), tap_step(&M, 9), button(&M, MR_CC_SHIFT, 0);   /* Velocity */
    HAS("E:Velocity|");
    jog(&M, 1);
    HAS("V:Velocity: ");
    press(&M, MR_CC_BACK), clear();
    button(&M, MR_CC_SHIFT, 1), tap_step(&M, 5), button(&M, MR_CC_SHIFT, 0);   /* the metronome */
    HAS("V:Metronome on|");
    /* steps, a held step's trig values, the loop, pages */
    tap_pad(&M, 0), clear();
    tap_step(&M, 0);
    HAS("V:Step 1 on|");
    step(&M, 0, 1), adv_ms(&M, RFM_MOVE_SEQ_HOLD_MS + 20);
    HAS("V:Step 1: ");
    knob(&M, 7, 3);
    HAS("V:");
    step(&M, 0, 0), rest_pop(&M), clear();
    press(&M, MR_CC_LEFT - 999 + 999);                  /* (Left on page 1: nothing to say) */
    clear();
    button(&M, MR_CC_LOOP, 1);
    HAS("V:Loop: ");
    button(&M, MR_CC_LOOP, 0), rest_pop(&M);
    HAS("V:Loop edit on|");                             /* (a tap: latched) */
    press(&M, MR_CC_BACK);
    HAS("V:Loop edit off|");
    press(&M, MR_CC_RIGHT);
    HAS("Bar 2");
    press(&M, MR_CC_LEFT);
    HAS("V:Bar 1|");
    /* modifiers, copy, delete */
    button(&M, MR_CC_COPY, 1);
    HAS("V:Copy|");
    tap_step(&M, 0);
    HAS("V:Step 1 copied|");
    tap_step(&M, 3);
    HAS("V:Pasted to step 4|");
    button(&M, MR_CC_COPY, 0), clear();
    button(&M, MR_CC_DELETE, 1);
    HAS("V:Delete|");
    tap_step(&M, 3);
    HAS("V:Step 4 cut|");                               /* (0.6.3: Delete cuts: said so, not "off") */
    button(&M, MR_CC_DELETE, 0), clear();
    /* the tracks: Track 4 + a step (select), held (mute), a double tap (solo) */
    track(&M, 3, 1), tap_step(&M, 1);
    HAS("V:Track 2|");
    step(&M, 2, 1), adv_ms(&M, RFM_MOVE_SEQ_HOLD_MS + 50), step(&M, 2, 0);
    SAID("V:Track 3|V:Track 3, muted|");               /* (the press picks it; the hold mutes it) */
    tap_step(&M, 4), tap_step(&M, 4);
    SAID("V:Track 5|V:Track 5, solo|");
    track(&M, 3, 0), rest_pop(&M), clear();
    /* the transport, recording, sampling */
    press(&M, MR_CC_PLAY);
    HAS("V:Playing|");
    press(&M, MR_CC_REC);
    HAS("V:Record on|");
    press(&M, MR_CC_REC);
    HAS("V:Record off|");
    press(&M, MR_CC_PLAY);
    HAS("V:Stopped|");
    press(&M, MR_CC_SAMPLE);
    HAS("V:Audio loop");
    press(&M, MR_CC_SAMPLE), adv_ms(&M, 2000), clear();
    /* session: a clip launched (queued, then playing at the bar), the scenes */
    press(&M, MR_CC_MENU);
    HAS("E:Session|");
    press(&M, MR_CC_PLAY);
    HAS("V:Playing|");
    adv_ms(&M, 100);
    tap_pad(&M, 24), tap_pad(&M, 24);                   /* (the playing clip, a double tap: it stops at the bar) */
    HAS("V:Track 1, stop queued|");
    for (i = 0; i < 30 && !strstr(said, "stopped"); i++)
        adv_ms(&M, 100);
    HAS("V:Track 1 stopped|");
    tap_pad(&M, 24);                                    /* (launched again: at the bar) */
    HAS("V:Track 1, clip 1 queued|");
    for (i = 0; i < 30 && !strstr(said, "playing"); i++)
        adv_ms(&M, 100);
    HAS("V:Track 1, clip 1 playing|");
    press(&M, MR_CC_PLAY), clear();
    press(&M, MR_CC_DOWN);
    HAS("V:Scenes 2 to 5|");
    press(&M, MR_CC_MENU), clear();
    /* the project grid: a project pad */
    button(&M, MR_CC_SHIFT, 1), tap_step(&M, 0), button(&M, MR_CC_SHIFT, 0);
    HAS("E:Firmwares, Slot 1, Felucca|");
    tap_track(&M, 3);
    HAS("E:Projects|");
    pad(&M, 31, 100);
    SAID("V:Project 8, 120 BPM, 0 clips|");
    pad(&M, 31, 0), rest_pop(&M), clear();
}
/* the browser's filters, banks, favourites, tags; FX setup's module pages */
static void test_browser_rows(void)
{
    char want[96];
    int ids[32];
    fresh(&M, 1);
    press(&M, MR_CC_BACK), clear();
    tap_step(&M, 1), rest_pop(&M), clear();
    tap_step(&M, 2);                                    /* step 3: Synth */
    HAS("V:Filter Synth|");
    tap_step(&M, 2);
    HAS("V:No filter|");
    rest_pop(&M), clear();
    press(&M, MR_CC_RIGHT);
    HAS("V:Bank 2|");
    press(&M, MR_CC_LEFT), rest_pop(&M), clear();
    rfmv_fw_bank(&M, ids);
    tap_pad(&M, pad_of(0)), clear();
    press(&M, MR_CC_SAMPLE);                            /* (Sampling: a favourite) */
    snprintf(want, sizeof want, "V:%s added to Favorites|", E[ids[0]].name);
    HAS(want);
    rest_pop(&M), clear();
    button(&M, MR_CC_CAPTURE, 1);
    HAS("V:Tags: ");
    tap_step(&M, 4);                                    /* (step 5: Chords) */
    HAS("Chords");
    button(&M, MR_CC_CAPTURE, 0), rest_pop(&M), clear();
    press(&M, MR_CC_MENU);
    HAS("E:FX setup, ");
    tap_track(&M, 0), clear();
    pad(&M, fxpad(0, 0), 100), jog(&M, 1), pad(&M, fxpad(0, 0), 0), rest_pop(&M), clear();
    jog_press(&M);                                      /* the module's own pages */
    HAS("E:Arp, Page 1|");
}

/* (0.6.2) Global settings: it owns the controls while open (nothing reaches the FM-1 or the firmware; releases pass);
 * a list on the jog; its words; a contact sheet of its screens (global_*.pbm) */
static void write_pbm(const char *name, const uint8_t *packed)
{
    FILE *f = fopen(name, "wb");
    int y, x;
    if (!f)
        return;
    fprintf(f, "P1\n128 64\n");
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++)
            fputc(packed[(y >> 3) * 128 + x] >> (y & 7) & 1 ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
}
static void open_global(rfm_move_t *m) { track(m, 0, 1), tap_step(m, 1), track(m, 0, 0); }
static void test_global(void)
{
    static uint8_t out[MR_SCREEN_BYTES];
    int mode, k;
    for (mode = RFM_MOVE_MODE_FM1; mode <= RFM_MOVE_MODE_CONTROL; mode++) {
        fresh(&M, 1);
        rfm_move_set_mode(&M, mode);
        pad(&M, 5, 100);                                /* (held before it opens: its release must pass) */
        open_global(&M);
        CHECK(M.seq.smenu == RFM_MOVE_SM_GLOBAL);
        memset(kinds, 0, sizeof kinds);
        for (k = 0; k <= 8; k++)
            touch(&M, k, 1), knob(&M, k, 7), knob(&M, k, -3), touch(&M, k, 0);
        tap_pad(&M, 20), tap_step(&M, 4), tap_step(&M, 9);
        press(&M, MR_CC_PLAY), press(&M, MR_CC_REC), press(&M, MR_CC_SHIFT), press(&M, MR_CC_LEFT);
        press(&M, MR_CC_RIGHT), press(&M, MR_CC_UP), press(&M, MR_CC_DOWN), press(&M, MR_CC_CAPTURE);
        press(&M, MR_CC_SAMPLE), press(&M, MR_CC_COPY), press(&M, MR_CC_DELETE), press(&M, MR_CC_MUTE);
        press(&M, MR_CC_UNDO), press(&M, MR_CC_LOOP);
        CHECKF(panel_acts() == 0, "mode %d: %d actions reached the panel", mode, panel_acts());
        CHECK(M.seq.smenu == RFM_MOVE_SM_GLOBAL && M.s.mode == mode && !M.pop);
        pad(&M, 5, 0);                                  /* (the held pad's release passes) */
        CHECKF(mode == RFM_MOVE_MODE_CONTROL || panel_acts() == 1, "mode %d: %d", mode, panel_acts());
        press(&M, MR_CC_BACK);
        CHECK(!M.seq.smenu);
        memset(kinds, 0, sizeof kinds);
        knob(&M, 4, 4);                                 /* (closed: the knobs are the mode's again) */
        CHECK(mode != RFM_MOVE_MODE_FM1 || kinds[RFM_MOVE_ACT_TURN] == 1);
    }
    /* the list: turn, click to edit, turn, click; Back leaves the edit, then closes; the words */
    fresh(&M, 1);
    open_global(&M);
    SAID("E:Global settings, Clock: Internal|");
    rfm_move_screen(&M, NULL, out), write_pbm("global_select.pbm", out);
    jog(&M, 1);
    SAID("V:Velocity: Dynamic|");
    jog_press(&M);
    SAID("V:Editing Velocity: Dynamic|");
    jog(&M, 1);
    SAID("V:Velocity: Fixed|");
    CHECK(M.s.vel_mode == RFM_MOVE_VEL_FIXED && M.gl_edit);
    rfm_move_screen(&M, NULL, out), write_pbm("global_edit.pbm", out);
    jog_press(&M);
    SAID("V:Done|");
    jog(&M, 2);
    SAID("V:Popup size: Half|");
    jog_press(&M), jog(&M, -1);
    SAID("V:Editing Popup size: Half|V:Popup size: Full|");
    CHECK(M.s.pop_half == 0);
    press(&M, MR_CC_BACK);
    SAID("V:Done|");
    CHECK(M.seq.smenu == RFM_MOVE_SM_GLOBAL && !M.gl_edit);
    jog(&M, 5);
    SAID("V:Battery: unknown|");
    rfm_move_set_battery(&M, 63, 0);
    rfm_move_screen(&M, NULL, out), write_pbm("global_battery.pbm", out);
    jog_press(&M);
    SAID("V:Battery: 63%|");                            /* (no edit: said again) */
    CHECK(!M.gl_edit);
    press(&M, MR_CC_BACK);
    SAID("E:FM-1 mode|");
    CHECK(!M.seq.smenu);
    open_global(&M);                                    /* (again: the row kept, no edit) */
    SAID("E:Global settings, Battery: 63%|");
    open_global(&M);                                    /* (its own toggle closes it) */
    SAID("E:FM-1 mode|");
}

/* (0.6.3) the input walk: every input in every context; any that changes the screen must say something */
typedef struct { const char *name; void (*set)(rfm_move_t *m); void (*unset)(rfm_move_t *m); } ctx_t;
static void c_fm1(rfm_move_t *m) { (void)m; }
static void c_midi(rfm_move_t *m) { tap_track(m, 1); }
static void c_notes(rfm_move_t *m) { tap_track(m, 1), tap_track(m, 1); }
static void c_drum(rfm_move_t *m) { tap_track(m, 1), track(m, 1, 1), tap_pad(m, 26), track(m, 1, 0); }
static void c_ctl(rfm_move_t *m) { tap_track(m, 2); }
static void seq_notes(rfm_move_t *m) { tap_pad(m, 0), tap_step(m, 0), tap_step(m, 4), tap_step(m, 8); }
static void c_seq(rfm_move_t *m) { tap_track(m, 3), seq_notes(m); }
static void c_session(rfm_move_t *m) { c_seq(m), press(m, MR_CC_MENU); }
static void c_shift(rfm_move_t *m) { c_seq(m), button(m, MR_CC_SHIFT, 1); }
static void u_shift(rfm_move_t *m) { button(m, MR_CC_SHIFT, 0); }
static void c_copy(rfm_move_t *m) { c_seq(m), button(m, MR_CC_COPY, 1); }
static void u_copy(rfm_move_t *m) { button(m, MR_CC_COPY, 0); }
static void c_delete(rfm_move_t *m) { c_seq(m), button(m, MR_CC_DELETE, 1); }
static void u_delete(rfm_move_t *m) { button(m, MR_CC_DELETE, 0); }
static void c_mute(rfm_move_t *m) { c_seq(m), button(m, MR_CC_MUTE, 1); }
static void u_mute(rfm_move_t *m) { button(m, MR_CC_MUTE, 0); }
static void c_loop(rfm_move_t *m) { c_seq(m), button(m, MR_CC_LOOP, 1); }
static void u_loop(rfm_move_t *m) { button(m, MR_CC_LOOP, 0); }
static void c_step(rfm_move_t *m) { c_seq(m), step(m, 4, 1), adv_ms(m, RFM_MOVE_SEQ_HOLD_MS + 20); }
static void u_step(rfm_move_t *m) { step(m, 4, 0); }
static void c_t4(rfm_move_t *m) { c_seq(m), track(m, 3, 1); }
static void u_t4(rfm_move_t *m) { track(m, 3, 0); }
static void c_t2(rfm_move_t *m) { c_midi(m), track(m, 1, 1); }
static void u_t2(rfm_move_t *m) { track(m, 1, 0); }
static void c_t3(rfm_move_t *m) { c_ctl(m), track(m, 2, 1); }
static void u_t3(rfm_move_t *m) { track(m, 2, 0); }
static void c_t1(rfm_move_t *m) { track(m, 0, 1); }
static void u_t1(rfm_move_t *m) { track(m, 0, 0); }
static void sm(rfm_move_t *m, int s) { c_seq(m), button(m, MR_CC_SHIFT, 1), tap_step(m, s), button(m, MR_CC_SHIFT, 0); }
static void c_workflow(rfm_move_t *m) { sm(m, 2); }
static void c_tempo(rfm_move_t *m) { sm(m, 4); }
static void c_groove(rfm_move_t *m) { sm(m, 6); }
static void c_layout(rfm_move_t *m) { sm(m, 7); }
static void c_keys(rfm_move_t *m) { sm(m, 8); }
static void c_vel(rfm_move_t *m) { sm(m, 9); }
static void c_arp(rfm_move_t *m) { sm(m, 10); }
static void c_global(rfm_move_t *m) { sm(m, 1); }
static void c_grid(rfm_move_t *m) { sm(m, 0), tap_track(m, 3); }
static void c_fw(rfm_move_t *m) { press(m, MR_CC_BACK); }
static void c_fwsel(rfm_move_t *m) { press(m, MR_CC_BACK), tap_step(m, 1), tap_pad(m, 24); }
static void c_catalog(rfm_move_t *m) { press(m, MR_CC_BACK), press(m, MR_CC_MUTE); }
static void c_states(rfm_move_t *m) { press(m, MR_CC_BACK), press(m, MR_CC_LOOP); }
static void c_capture(rfm_move_t *m) { c_fwsel(m), button(m, MR_CC_CAPTURE, 1); }
static void u_capture(rfm_move_t *m) { button(m, MR_CC_CAPTURE, 0); }
static void c_fx(rfm_move_t *m) { press(m, MR_CC_BACK), press(m, MR_CC_MENU); }
static void c_mixer(rfm_move_t *m) { c_fx(m), tap_track(m, 3); }
static void c_routing(rfm_move_t *m) { c_fx(m), tap_track(m, 1); }
static const ctx_t CTX[] = {
    { "FM-1", c_fm1, NULL }, { "MIDI", c_midi, NULL }, { "MIDI notes", c_notes, NULL }, { "MIDI drum", c_drum, NULL },
    { "Control", c_ctl, NULL }, { "Seq track", c_seq, NULL }, { "Seq session", c_session, NULL },
    { "Seq Shift held", c_shift, u_shift }, { "Seq Copy held", c_copy, u_copy }, { "Seq Delete held", c_delete, u_delete },
    { "Seq Mute held", c_mute, u_mute }, { "Seq Loop held", c_loop, u_loop }, { "Seq step held", c_step, u_step },
    { "Track 4 held", c_t4, u_t4 }, { "Track 2 held", c_t2, u_t2 }, { "Track 3 held", c_t3, u_t3 },
    { "Track 1 held", c_t1, u_t1 }, { "Workflow", c_workflow, NULL }, { "Tempo", c_tempo, NULL },
    { "Groove", c_groove, NULL }, { "Layout", c_layout, NULL }, { "Keys", c_keys, NULL }, { "Velocity", c_vel, NULL },
    { "Arp", c_arp, NULL }, { "Global", c_global, NULL }, { "Projects grid", c_grid, NULL },
    { "Firmwares", c_fw, NULL }, { "Firmware selected", c_fwsel, NULL }, { "Catalog", c_catalog, NULL },
    { "States", c_states, NULL }, { "Capture held", c_capture, u_capture }, { "FX setup", c_fx, NULL },
    { "Mixer", c_mixer, NULL }, { "Routing", c_routing, NULL },
};
typedef struct { const char *name; int kind, a, b; } in_t;   /* kind 0 button, 1 step, 2 pad, 3 knob, 4 jog, 5 click, 6 touch */
static const in_t INS[] = {
    { "Shift", 0, MR_CC_SHIFT, 0 }, { "Menu", 0, MR_CC_MENU, 0 }, { "Back", 0, MR_CC_BACK, 0 },
    { "Capture", 0, MR_CC_CAPTURE, 0 }, { "Down", 0, MR_CC_DOWN, 0 }, { "Up", 0, MR_CC_UP, 0 },
    { "Undo", 0, MR_CC_UNDO, 0 }, { "Loop", 0, MR_CC_LOOP, 0 }, { "Copy", 0, MR_CC_COPY, 0 },
    { "Left", 0, MR_CC_LEFT, 0 }, { "Right", 0, MR_CC_RIGHT, 0 }, { "Play", 0, MR_CC_PLAY, 0 },
    { "Rec", 0, MR_CC_REC, 0 }, { "Mute", 0, MR_CC_MUTE, 0 }, { "Sample", 0, MR_CC_SAMPLE, 0 },
    { "Delete", 0, MR_CC_DELETE, 0 }, { "Track 1", 0, MR_CC_TRACK1, 0 }, { "Track 2", 0, MR_CC_TRACK1 - 1, 0 },
    { "Track 3", 0, MR_CC_TRACK1 - 2, 0 }, { "Track 4", 0, MR_CC_TRACK4, 0 },
    { "step 1", 1, 0, 0 }, { "step 2", 1, 1, 0 }, { "step 3", 1, 2, 0 }, { "step 4", 1, 3, 0 }, { "step 5", 1, 4, 0 },
    { "step 6", 1, 5, 0 }, { "step 7", 1, 6, 0 }, { "step 8", 1, 7, 0 }, { "step 9", 1, 8, 0 }, { "step 10", 1, 9, 0 },
    { "step 11", 1, 10, 0 }, { "step 12", 1, 11, 0 }, { "step 13", 1, 12, 0 }, { "step 14", 1, 13, 0 },
    { "step 15", 1, 14, 0 }, { "step 16", 1, 15, 0 },
    { "pad 1", 2, 0, 0 }, { "pad 5", 2, 4, 0 }, { "pad 9", 2, 8, 0 }, { "pad 17", 2, 16, 0 }, { "pad 24", 2, 23, 0 },
    { "pad 25", 2, 24, 0 }, { "pad 26", 2, 25, 0 }, { "pad 28", 2, 27, 0 }, { "pad 32", 2, 31, 0 },
    { "knob 1 +", 3, 0, 6 }, { "knob 2 +", 3, 1, 6 }, { "knob 3 +", 3, 2, 6 }, { "knob 4 +", 3, 3, 6 },
    { "knob 5 +", 3, 4, 6 }, { "knob 6 -", 3, 5, -6 }, { "knob 7 +", 3, 6, 6 }, { "knob 8 +", 3, 7, 6 },
    { "volume -", 3, 8, -2 }, { "jog +", 4, 1, 0 }, { "jog -", 4, -1, 0 }, { "jog click", 5, 0, 0 },
    { "touch knob 3", 6, 2, 0 }, { "touch volume", 6, 8, 0 },
};
static void apply(rfm_move_t *m, const in_t *in)
{
    switch (in->kind) {
    case 0: press(m, in->a); break;
    case 1: tap_step(m, in->a); break;
    case 2: tap_pad(m, in->a); break;
    case 3: knob(m, in->a, in->b); break;
    case 4: jog(m, in->a); break;
    case 5: jog_press(m); break;
    default: touch(m, in->a, 1); break;
    }
}
static int g_silent;
static void test_walk(void)
{
    static uint8_t a[MR_SCREEN_BYTES], b[MR_SCREEN_BYTES];
    const int nc = (int)(sizeof CTX / sizeof CTX[0]), ni = (int)(sizeof INS / sizeof INS[0]);
    int c, i, n = 0;
    for (c = 0; c < nc; c++)
        for (i = 0; i < ni; i++) {
            if (CTX[c].name[0] == 'T' && !strncmp(CTX[c].name, "Track ", 6) && !strncmp(INS[i].name, CTX[c].name, 7))
                continue;                               /* (the held Track pressed again: its release, no input) */
            if (CTX[c].set == c_step && !strcmp(INS[i].name, "step 5"))
                continue;                               /* (the held step again: its release) */
            fresh(&M, 1);
            CTX[c].set(&M);
            rest_pop(&M), rfm_move_screen(&M, NULL, a), clear();
            apply(&M, &INS[i]);
            rfm_move_screen(&M, NULL, b);
            n++;
            if (memcmp(a, b, sizeof a) && !said[0]) {
                g_silent++;
                if (getenv("SPEECH_WALK"))
                    fprintf(stderr, "silent: %-18s %s\n", CTX[c].name, INS[i].name);
            }
            if (CTX[c].unset)
                CTX[c].unset(&M);
        }
    CHECKF(g_silent == 0, "%d of %d inputs change the screen and say nothing (SPEECH_WALK=1 lists them)", g_silent, n);
}

/* (0.6.3) Delete and the clipboard, the owner's exact gestures: Delete alone changes nothing; Delete held + steps
 * cuts them (one, or several in one hold as a group); Copy held + steps copies; Copy or Delete held + an empty step
 * pastes, again and again; Track 4 held + Delete clears a track */
static int notes_at(const rfm_move_t *m, int clip, int abs)
{
    int lo, hi = rfmv_seq_range(m, clip, abs, &lo);
    return hi - lo;
}
static int all_notes(const rfm_move_t *m) { return m->seq.nnotes; }
static void seq_three(rfm_move_t *m)                  /* Sequencer mode, a note on steps 1, 2, 5 of track 1 */
{
    fresh(m, 1);
    tap_track(m, 3), tap_pad(m, 0), tap_step(m, 0), tap_step(m, 1), tap_step(m, 4), rest_pop(m), clear();
}
static void test_delete(void)
{
    const int c0 = SEQ_CLIP(0, 0);
    int n0;
    /* Delete tapped, Delete held and let go: nothing changes; a hint */
    seq_three(&M);
    n0 = all_notes(&M);
    press(&M, MR_CC_DELETE);
    CHECK(all_notes(&M) == n0 && rfmv_seq_clip_has(&M, c0) && !M.seq.cut && !M.seq.clip_kind);
    HAS("V:Delete: hold and press a step, pad or clip|");
    button(&M, MR_CC_DELETE, 1), adv_ms(&M, 800), button(&M, MR_CC_DELETE, 0);
    CHECK(all_notes(&M) == n0 && rfmv_seq_clip_has(&M, c0));
    press(&M, MR_CC_MENU), clear();                     /* (session too) */
    press(&M, MR_CC_DELETE), button(&M, MR_CC_DELETE, 1), adv_ms(&M, 800), button(&M, MR_CC_DELETE, 0);
    CHECK(all_notes(&M) == n0 && rfmv_seq_clip_has(&M, c0));
    press(&M, MR_CC_MENU), rest_pop(&M), clear();
    /* the owner's sequence, Delete held throughout: cut A; A again pastes it back; empty steps paste; B (notes)
     * cuts; C (notes) right after the cut is pasted over; C again cuts */
    button(&M, MR_CC_DELETE, 1);
    SAID("V:Delete|");
    tap_step(&M, 4);
    SAID("V:Step 5 cut|");
    CHECK(notes_at(&M, c0, 4) == 0 && M.seq.cut && M.seq.clip_kind == 1);
    tap_step(&M, 4);
    SAID("V:Pasted to step 5|");
    CHECK(notes_at(&M, c0, 4) == 1);
    tap_step(&M, 8);
    SAID("V:Pasted to step 9|");
    tap_step(&M, 9);
    SAID("V:Pasted to step 10|");
    CHECK(notes_at(&M, c0, 8) == 1 && notes_at(&M, c0, 9) == 1 && all_notes(&M) == 5);
    tap_step(&M, 0);                                    /* B: notes, after a paste: cut */
    SAID("V:Step 1 cut|");
    CHECK(notes_at(&M, c0, 0) == 0 && all_notes(&M) == 4);
    tap_step(&M, 1);                                    /* C: notes, right after a cut: pasted over */
    SAID("V:Step 2 replaced|");
    CHECK(notes_at(&M, c0, 1) == 1 && all_notes(&M) == 4);
    tap_step(&M, 1);                                    /* C again: cut */
    SAID("V:Step 2 cut|");
    CHECK(notes_at(&M, c0, 1) == 0 && all_notes(&M) == 3);
    button(&M, MR_CC_DELETE, 0);
    SAID("");
    /* let go: the last action forgotten, the clipboard kept; a new hold: an empty step pastes, a full one cuts */
    button(&M, MR_CC_DELETE, 1), clear();
    tap_step(&M, 12);
    SAID("V:Pasted to step 13|");
    tap_step(&M, 12);
    SAID("V:Step 13 cut|");
    button(&M, MR_CC_DELETE, 0), clear();
    /* Copy held + an empty step pastes it too, again and again */
    button(&M, MR_CC_COPY, 1), tap_step(&M, 13), tap_step(&M, 14), button(&M, MR_CC_COPY, 0);
    SAID("V:Copy|V:Pasted to step 14|V:Pasted to step 15|");
    /* an empty clipboard: an empty step does nothing */
    seq_three(&M);
    button(&M, MR_CC_DELETE, 1), tap_step(&M, 6), button(&M, MR_CC_DELETE, 0);
    SAID("V:Delete|");
    CHECK(all_notes(&M) == 3 && !M.seq.clip_kind);
    /* Copy held + several steps: a group; pasted twice, its gaps kept */
    button(&M, MR_CC_COPY, 1), tap_step(&M, 0), tap_step(&M, 1), tap_step(&M, 4), button(&M, MR_CC_COPY, 0);
    SAID("V:Copy|V:Step 1 copied|V:Step 2 copied|V:Step 5 copied|");
    button(&M, MR_CC_COPY, 1), tap_step(&M, 8), button(&M, MR_CC_COPY, 0);
    CHECK(notes_at(&M, c0, 8) == 1 && notes_at(&M, c0, 9) == 1 && notes_at(&M, c0, 12) == 1 && all_notes(&M) == 6);
    button(&M, MR_CC_COPY, 1), tap_step(&M, 2), button(&M, MR_CC_COPY, 0);
    CHECK(notes_at(&M, c0, 2) == 1 && notes_at(&M, c0, 3) == 1 && notes_at(&M, c0, 6) == 1 && all_notes(&M) == 9);
    clear();
    /* copy (not cut): the source stays; pasted twice */
    seq_three(&M);
    button(&M, MR_CC_COPY, 1), tap_step(&M, 4), tap_step(&M, 10), tap_step(&M, 11), button(&M, MR_CC_COPY, 0);
    SAID("V:Copy|V:Step 5 copied|V:Pasted to step 11|V:Pasted to step 12|");
    CHECK(notes_at(&M, c0, 4) == 1 && notes_at(&M, c0, 10) == 1 && notes_at(&M, c0, 11) == 1);
    /* the pasted steps undo one by one */
    press(&M, MR_CC_UNDO);
    CHECK(notes_at(&M, c0, 11) == 0 && notes_at(&M, c0, 10) == 1);
    /* Track 4 held + Delete: the selected track cleared; + a step: that track */
    seq_three(&M);
    track(&M, 3, 1), button(&M, MR_CC_DELETE, 1), button(&M, MR_CC_DELETE, 0), track(&M, 3, 0);
    CHECK(all_notes(&M) == 0);
    HAS("V:Track 1 cleared|");
    seq_three(&M);
    track(&M, 3, 1), tap_step(&M, 1), track(&M, 3, 0), tap_pad(&M, 0), tap_step(&M, 0), clear();   /* (track 2: a note) */
    CHECK(M.seq.sel_track == 1 && all_notes(&M) == 4);
    track(&M, 3, 1), button(&M, MR_CC_DELETE, 1), tap_step(&M, 0), button(&M, MR_CC_DELETE, 0), track(&M, 3, 0);
    CHECK(all_notes(&M) == 1 && notes_at(&M, SEQ_CLIP(1, 0), 0) == 1);
    HAS("V:Track 1 cleared|");
}

int main(void)
{
    test_off_on();
    test_modes();
    test_popups();
    test_menus();
    test_browser();
    test_fx();
    test_seq();
    test_buttons();
    test_rows();
    test_browser_rows();
    test_global();
    test_delete();
    test_walk();
    CHECKF(bad_words == 0, "%d SAY actions with bad words", bad_words);
    return check_done("move_speech");
}
