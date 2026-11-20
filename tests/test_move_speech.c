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
static int nother;
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
    /* 5: SEL held with a live label: its words once it shows (time alone brings it); a tap says nothing */
    lbl[RFM_MOVE_LBL_BTN0 + 1] = "SEL/KEYS";
    rfm_move_set_labels(&M, lbl);
    button(&M, MR_CC_SAMPLE, 1);
    adv_ms(&M, 100);
    button(&M, MR_CC_SAMPLE, 0);
    rest_pop(&M);
    CHECK(said[0] == 0);
    button(&M, MR_CC_SAMPLE, 1);
    adv_ms(&M, RFM_MOVE_LABEL_HOLD_MS + 20);
    SAID("V:SEL/KEYS|");                                /* (as the screen: words with no lower case stay) */
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
    CHECK(said[0] == 0);
    step(&M, 4, 1);
    adv_ms(&M, RFM_MOVE_SEQ_HOLD_MS + 20);
    CHECKF(!strncmp(said, "V:Step 5: ", 10) && strstr(said, "empty") == NULL, "%s", said);
    step(&M, 4, 0), tap_step(&M, 6), rest_pop(&M), clear();
    press(&M, MR_CC_UNDO);
    SAID("V:Undo|");
    press(&M, MR_CC_UNDO);
    SAID("V:Undo|");                                    /* (another one, its message still up: said again) */
    button(&M, MR_CC_SHIFT, 1), press(&M, MR_CC_UNDO), button(&M, MR_CC_SHIFT, 0);
    SAID("V:Redo|");
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

int main(void)
{
    test_off_on();
    test_modes();
    test_popups();
    test_menus();
    test_browser();
    test_fx();
    test_seq();
    CHECKF(bad_words == 0, "%d SAY actions with bad words", bad_words);
    return check_done("move_speech");
}
