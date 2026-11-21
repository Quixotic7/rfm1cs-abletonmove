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
/* (0.8.0) a Track tap that no next tap of it turns into a double click; a double click */
static void tap1(rfm_move_t *m, int t);
static void dbl_track(rfm_move_t *m, int t) { tap_track(m, t), tap_track(m, t); }
static void rest_pop(rfm_move_t *m) { adv_ms(m, RFM_MOVE_POPUP_MS + 50); }
static void tap1(rfm_move_t *m, int t) { tap_track(m, t), adv_ms(m, RFM_MOVE_DOUBLE_MS + 20); }
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
    rfm_move_set_speech_default(m, RFM_MOVE_SPEECH_ON);   /* (0.6.12: the mode On, as a screen reader user's) */
    rfm_move_set_speech(m, speech);
    clear();
}

/* 1. a script through every part (layouts, knobs, the browser, FX setup, the sequencer): with speech off no word and
 * no cost; on, the same other actions and the same screen */
static void script(rfm_move_t *m)
{
    int i;
    touch(m, 4, 1), knob(m, 4, 3), knob(m, 4, 2), touch(m, 4, 0), rest_pop(m);
    knob(m, 8, 2), rest_pop(m);
    jog_press(m), jog(m, 1), jog(m, -1), rest_pop(m);   /* (the FM-1 layout: the jog on PRESET) */
    tap1(m, 1);                                         /* slot 2; its layout menu: Drum, a CC number, latch, .. */
    track(m, 1, 1), knob(m, 1, 9), tap_pad(m, 24 + RFM_MOVE_LAY_DRUM), knob(m, 0, 9), tap_pad(m, 31), tap_step(m, 2);
    jog(m, 1), press(m, MR_CC_UP), tap_pad(m, 17), track(m, 1, 0), rest_pop(m);
    tap_pad(m, 0), tap_pad(m, 31);
    knob(m, 0, 1), button(m, MR_CC_SHIFT, 1), knob(m, 0, 1), button(m, MR_CC_SHIFT, 0), rest_pop(m);
    tap1(m, 0), dbl_track(m, 0), tap_step(m, 1), tap_pad(m, 0), jog(m, 1), jog(m, -1);   /* the browser */
    press(m, MR_CC_MUTE), tap_pad(m, 1), press(m, MR_CC_MUTE), tap1(m, 3), touch(m, 0, 1), touch(m, 0, 0);   /* FX */
    pad(m, fxpad(0, 0), 100), jog(m, 1), pad(m, fxpad(0, 0), 0), knob(m, 0, 6), tap1(m, 3), knob(m, 0, -3);
    for (i = 0; i < 6 && m->seq.projects; i++)
        press(m, MR_CC_BACK);
    rest_pop(m);
    tap1(m, 1), pad(m, 0, 100), pad(m, 0, 0), tap_step(m, 0), press(m, MR_CC_PLAY), adv_ms(m, 300);   /* (0.9.0: slot 2's
                                                         * Drum layout: the sequencer) */
    press(m, MR_CC_PLAY), press(m, MR_CC_UNDO), track(m, 0, 1), tap_step(m, 2), track(m, 0, 0);   /* (the lock menu) */
    track(m, 3, 1), tap_step(m, 4), track(m, 3, 0), press(m, MR_CC_BACK), press(m, MR_CC_MENU);   /* (Tempo) */
    for (i = 0; i < 4; i++)
        adv_ms(m, 400);
    tap1(m, 1), press(m, MR_CC_BACK);
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

/* 2, 3, 15: (0.9.0) the Track buttons: a slot and its layout (a note layout is the sequencer, the slot's track);
 * Back with nothing open does nothing; a knob's popup closing says nothing more; the words on demand */
static void test_modes(void)
{
    char w[RFM_MOVE_SAY_MAX];
    fresh(&M, 1);
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "Felucca, FM-1 layout"));
    tap1(&M, 1);                                        /* Track 2: slot 2, its layout (FM-1 at first) */
    SAID("E:Melodee, FM-1 layout|V:Melodee, FM-1|");
    CHECK(M.fw.active == 1 && M.s.mode == RFM_MOVE_MODE_FM1);
    rfm_move_set_slot_layout(&M, 1, RFM_MOVE_LAY_DRUM); /* (the host's call: the active slot's, played at once: the
                                                         * sequencer, slot 2's track: 2) */
    SAID("E:Melodee, Drum layout, Track 2|");
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.seq.sel_track == 1 && M.seq.trig == 1 && M.seq.view == 0);
    CHECK(M.seq.fx.ffilt[1] >> 1 & 1);
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "Melodee, Drum layout, Track 2"));
    tap1(&M, 2);                                        /* an empty slot: played too */
    SAID("E:Track 3, FM-1 layout|V:Track 3, FM-1|");
    CHECK(M.fw.active == 2 && M.s.mode == RFM_MOVE_MODE_FM1);
    press(&M, MR_CC_BACK);                              /* (0.9.0) Back, nothing open: nothing (no Sequencer flip); a Back
                                                         * that changed nothing says where you are (0.6.3) */
    SAID("V:Track 3, FM-1 layout|");
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && M.fw.active == 2 && !M.seq.projects);
    tap1(&M, 1);                                        /* slot 2 again: its Drum layout, its track */
    SAID("E:Melodee, Drum layout, Track 2|V:Melodee, Drum|");
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.fw.active == 1 && M.seq.sel_track == 1);
    press(&M, MR_CC_MENU);                              /* session view; Back closes it */
    SAID("E:Session|");
    press(&M, MR_CC_BACK);
    SAID("E:Melodee, Drum layout, Track 2|");
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && !M.seq.view);
    tap1(&M, 0);
    SAID("E:Felucca, FM-1 layout|V:Felucca, FM-1|");
    rest_pop(&M), clear();
    /* a held Track: its menu, not a slot played */
    track(&M, 1, 1);
    adv_ms(&M, 400);
    SAID("V:Melodee, layouts, Drum, Track 2|");
    track(&M, 1, 0);
    rest_pop(&M);
    CHECK(said[0] == 0 && M.s.mode == RFM_MOVE_MODE_FM1 && M.fw.active == 0);
    /* 3: knob 5 touched in the FM-1 layout: its name; released, its popup gone: nothing more */
    touch(&M, 4, 1);
    SAID("V:K1|");
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "Felucca, FM-1 layout, K1"));
    knob(&M, 4, 2);
    SAID("V:K1: +1|");
    touch(&M, 4, 0);
    rest_pop(&M);
    tap_pad(&M, 5);
    CHECK(said[0] == 0);
    rfm_move_set_mode(&M, RFM_MOVE_MODE_CONTROL);      /* (the host's call says it too; Control (MIDI) is the slot's
                                                         * note layout, the sequencer now) */
    SAID("E:Felucca, Scale layout, Track 1|");          /* (fixed in 0.8.0: slot 1 never had a note layout: Scale at first,
                                                         * not slot 2's Drum) */
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.s.slot[0].lay == RFM_MOVE_LAY_SCALE && M.seq.sel_track == 0);
    rfm_move_set_mode(&M, RFM_MOVE_MODE_FM1);
    SAID("E:Felucca, FM-1 layout|");
    /* no firmware list: one slot, no name; Tracks 2, 3 do nothing */
    rfm_move_init(&M, NULL, on_act, NULL);
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON);
    rfm_move_set_speech(&M, 1);
    clear();
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "FM-1 layout"));
    tap1(&M, 1), tap1(&M, 2);
    SAID("");
    CHECK(M.fw.active == 0 && M.s.mode == RFM_MOVE_MODE_FM1);
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
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "Felucca, FM-1 layout, FM-1 linked"));
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

/* 8, 9, 14: (0.9.0) a slot's held Track: one menu (the layouts, the tracks for that firmware, the key, the selected
 * track's CC numbers in the sequencer), latch; a note layout's knobs are the sequencer's (no slot CC knobs) */
static void test_menus(void)
{
    fresh(&M, 1);
    track(&M, 0, 1), tap_pad(&M, 24 + RFM_MOVE_LAY_SCALE);   /* Track 1 held, pad 3: the Scale layout */
    SAID("E:Felucca, Scale layout, Track 1|V:Layout: Scale|");
    track(&M, 0, 0), rest_pop(&M);
    CHECK(said[0] == 0 && M.s.mode == RFM_MOVE_MODE_SEQ && M.s.slot[0].lay == RFM_MOVE_LAY_SCALE);
    CHECK(M.seq.sel_track == 0 && M.seq.trig == 1 && !M.seq.view);
    knob(&M, 0, 1);                                     /* (0.9.0) the knobs: the sequencer's TRIG page, no slot CC */
    SAID("V:Micro: +1|");
    rest_pop(&M), clear();
    CHECK(M.s.slot[0].cc[0] == 16 && M.s.cc[M.s.bank][0] == 16);
    track(&M, 0, 1), knob(&M, 0, 1);                    /* Track 1 held + a knob: the selected track's CC number */
    SAID("V:Knob 1: CC 17|");
    track(&M, 0, 0), rest_pop(&M);
    CHECK(said[0] == 0 && M.s.cc[M.s.bank][0] == 17 && M.s.slot[0].cc[0] == 16);
    track(&M, 0, 1);
    tap_pad(&M, 17);                                    /* (row 3, pad 2: scale 2) */
    SAID("V:Scale: Minor|");
    jog(&M, 1);
    SAID("V:Root: Db|");
    tap_step(&M, 2);                                    /* (0.9.0) a step: track 3 selected for slot 1 (not a channel) */
    SAID("V:Track 3|V:Track 3, selected|");
    CHECK(rfm_move_seq_lock(&M, 0) == 2 && M.seq.sel_track == 2 && (M.seq.fx.ffilt[0] >> 2 & 1));
    press(&M, MR_CC_UP);
    SAID("V:Octave 1|");
    tap_pad(&M, 31);                                    /* 14: latch */
    SAID("V:Latch on|");
    track(&M, 0, 0), rest_pop(&M);
    CHECK(said[0] == 0 && M.s.mode == RFM_MOVE_MODE_SEQ && M.s.scale == 1 && M.s.root == 1 && M.s.octave == 1);
    /* another slot's menu edits that slot, not the one played */
    track(&M, 1, 1), tap_pad(&M, 24 + RFM_MOVE_LAY_DRUM);
    SAID("V:Layout: Drum|");
    tap_step(&M, 5);                                    /* (a track for slot 2: not the one edited now) */
    SAID("V:Track 6|V:Track 6, selected|");
    track(&M, 1, 0), rest_pop(&M);
    CHECK(said[0] == 0 && M.fw.active == 0 && M.s.slot[1].lay == RFM_MOVE_LAY_DRUM && rfm_move_seq_lock(&M, 1) == 5);
    CHECK((M.seq.fx.ffilt[1] >> 5 & 1) && M.seq.sel_track == 2 && rfm_move_seq_lock(&M, 0) == 2);
    CHECK(M.s.layout == RFM_MOVE_LAYOUT_SCALE && M.s.mode == RFM_MOVE_MODE_SEQ);
    /* the FM-1 layout's menu: a knob is no CC number */
    tap1(&M, 2), rest_pop(&M), clear();
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && M.fw.active == 2);
    track(&M, 2, 1), adv_ms(&M, 400), clear(), knob(&M, 0, 1);
    CHECK(!strstr(said, "CC"));
    track(&M, 2, 0), rest_pop(&M);
    CHECK(M.s.slot[2].cc[0] == 16);
    clear();
}

/* 10, 11, 15, 16: the browser (0.8.0: a Track's double click), the catalog, a download; a Track tap leaves it */
static void test_browser(void)
{
    char want[400], w[RFM_MOVE_SAY_MAX];
    int ids[32], k, i, cell = -1, golf = idx("golf");
    fresh(&M, 1);
    dbl_track(&M, 0);                                   /* (its first tap acts at once: the slot played, named) */
    SAID("V:Felucca, FM-1|E:Firmwares, Slot 1, Felucca|");
    CHECK(rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "Firmwares, Slot 1, Felucca"));
    dbl_track(&M, 1);                                   /* (in the view: the tap leaves it, plays slot 2; the double
                                                         * click its browser) */
    SAID("E:Melodee, FM-1 layout|V:Melodee, FM-1|E:Firmwares, Slot 2, Melodee|");
    dbl_track(&M, 2);
    SAID("E:Track 3, FM-1 layout|V:Track 3, FM-1|E:Firmwares, Slot 3, empty|");
    tap_step(&M, 1);
    SAID("V:All firmwares|");
    rest_pop(&M);
    k = rfmv_fw_bank(&M, ids);
    CHECK(k > 2);
    tap_pad(&M, pad_of(0));
    snprintf(want, sizeof want, "V:%s|", E[ids[0]].name);
    SAID(want);
    adv_ms(&M, RFM_MOVE_DOUBLE_MS + 20);
    tap_pad(&M, pad_of(0));                             /* (the same, later: nothing; at once it would load, below) */
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
    CHECK(strstr(said, "E:Track 3, FM-1 layout|") != NULL);   /* (out: slot 3's, the one browsed last, played) */
    clear();
    /* (0.9.0) a pad tapped twice at once: that firmware loaded (said as the jog's press), the browser stays; in the
     * catalog one not installed is not downloaded by it */
    fresh(&M, 1);
    dbl_track(&M, 1), adv_ms(&M, RFM_MOVE_DOUBLE_MS + 20), tap_step(&M, 1), rest_pop(&M), clear();
    k = rfmv_fw_bank(&M, ids);
    memset(kinds, 0, sizeof kinds);
    tap_pad(&M, pad_of(0)), tap_pad(&M, pad_of(0));
    snprintf(want, sizeof want, "V:%s|V:Loading %s|", E[ids[0]].name, E[ids[0]].name);
    SAID(want);
    CHECK(kinds[RFM_MOVE_ACT_FW_LOAD] == 1 && M.seq.projects);
    rfm_move_fw_progress(&M, E[ids[0]].id, 100), rest_pop(&M), clear();
    fresh(&M, 1);
    dbl_track(&M, 1), adv_ms(&M, RFM_MOVE_DOUBLE_MS + 20), press(&M, MR_CC_MUTE), rest_pop(&M), clear();
    k = rfmv_fw_bank(&M, ids);
    for (i = 0, cell = -1; i < k && cell < 0; i++)
        if (!E[ids[i]].installed)
            cell = i;
    CHECK(cell >= 0);
    if (cell >= 0) {
        memset(kinds, 0, sizeof kinds);
        tap_pad(&M, pad_of(cell)), tap_pad(&M, pad_of(cell));
        snprintf(want, sizeof want, "V:%s, not installed|", E[ids[cell]].name);
        SAID(want);
        CHECK(kinds[RFM_MOVE_ACT_FW_LOAD] == 0 && kinds[RFM_MOVE_ACT_FW_DOWNLOAD] == 0 && M.seq.projects);
    }
    fresh(&M, 1);
    /* 11: (was Back + Track 2) a Track tap in the view: out of it, that slot played */
    dbl_track(&M, 0), adv_ms(&M, RFM_MOVE_DOUBLE_MS + 20), clear();
    tap1(&M, 1);
    SAID("E:Melodee, FM-1 layout|V:Melodee, FM-1|");
    CHECK(!M.seq.projects && M.fw.active == 1);
    /* Back + a Track: no slot switch any more; the Track's own gesture (a tap) */
    button(&M, MR_CC_BACK, 1), tap_track(&M, 0), button(&M, MR_CC_BACK, 0);
    SAID("E:Felucca, FM-1 layout|V:Felucca, FM-1|");
    CHECK(M.s.mode == RFM_MOVE_MODE_FM1 && !M.seq.projects);
}

/* 12: FX setup (0.8.0: Track 4's tap, the Mixer <-> Routing, the last one used) */
static void test_fx(void)
{
    char w[RFM_MOVE_SAY_MAX];
    fresh(&M, 1);
    tap1(&M, 3);
    SAID("E:FX setup, Mixer|V:Level|");
    CHECK(M.seq.projects && rfm_move_words(&M, w, sizeof w) > 0 && !strcmp(w, "FX setup, Mixer, Level"));
    knob(&M, 0, -1);
    {
        char want[64];
        snprintf(want, sizeof want, "V:Slot 1 Level: %d|", M.fw.level[0]);
        SAID(want);
    }
    rest_pop(&M);
    tap1(&M, 3);                                        /* Track 4 again: Routing */
    SAID("E:Routing|V:Slot 1, Felucca|");
    tap_pad(&M, fxpad(0, 1));                           /* (the MIDI FX column: its pads, as before) */
    SAID("E:MIDI FX|V:MIDI FX 2: empty|");
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
    tap1(&M, 3);                                        /* the Mixer */
    SAID("E:Mixer|V:Level|");
    rest_pop(&M);
    tap1(&M, 3);                                        /* and back: Routing, its page kept (MIDI FX) */
    SAID("E:MIDI FX|V:MIDI FX 1: Arp|");
    rest_pop(&M), clear();
    tap1(&M, 0);                                        /* a slot's Track: out of the view, played */
    SAID("E:Felucca, FM-1 layout|V:Felucca, FM-1|");
    CHECK(!M.seq.projects);
    rest_pop(&M);
    track(&M, 3, 1), tap_pad(&M, 24), track(&M, 3, 0);  /* Track 4 held, pad 1: the Mixer; pad 2: Routing */
    SAID("E:FX setup, Mixer|V:Level|");
    CHECK(M.seq.projects && M.fw.fx);
    tap1(&M, 0), rest_pop(&M), clear();
    track(&M, 3, 1), tap_pad(&M, 25), track(&M, 3, 0);
    SAID("E:FX setup, MIDI FX|V:MIDI FX 1: Arp|");
    CHECK(M.seq.projects && M.fw.fx);
    tap1(&M, 0), rest_pop(&M), clear();
    dbl_track(&M, 3);                                   /* Track 4's double click: the projects (its tap: FX setup) */
    SAID("E:FX setup, MIDI FX|V:MIDI FX 1: Arp|E:Projects|");   /* (the last used: Routing) */
    CHECK(M.seq.projects && !M.fw.fx && M.fw.track == 3);
}

/* 13: the sequencer (0.9.0: a slot's note layout) */
static void test_seq(void)
{
    fresh(&M, 1);
    rfm_move_set_slot_layout(&M, 0, RFM_MOVE_LAY_SCALE);   /* (0.9.0: slot 1's Scale layout: the sequencer, track 1) */
    SAID("E:Felucca, Scale layout, Track 1|");
    press(&M, MR_CC_PLAY);
    SAID("V:Playing|");
    adv_ms(&M, 200);
    press(&M, MR_CC_PLAY);
    SAID("V:Stopped|");
    tap_pad(&M, 0);                                     /* a note, then a step: it is entered */
    clear();
    tap_step(&M, 4);
    SAID("V:Step 5|");                                  /* (0.6.5: just the step) */
    step(&M, 4, 1);
    adv_ms(&M, RFM_MOVE_SEQ_HOLD_MS + 20);
    SAID("V:Step 5|");                                  /* (held: its popup, the step only) */
    step(&M, 4, 0), clear(), tap_step(&M, 6);
    SAID("V:Step 7|");
    rest_pop(&M), clear();
    press(&M, MR_CC_UNDO);
    SAID("V:Undo|");
    press(&M, MR_CC_UNDO);
    SAID("V:Undo|");                                    /* (another one, its message still up: said again) */
    button(&M, MR_CC_SHIFT, 1), press(&M, MR_CC_UNDO), button(&M, MR_CC_SHIFT, 0);
    SAID("V:Shift|V:Redo|");
    rest_pop(&M);
    track(&M, 0, 1), step(&M, 2, 1);                    /* (0.9.0) Track 1 held, its menu: a step, a track */
    SAID("V:Track 3|");
    step(&M, 2, 0);                                     /* (its tap: selected for slot 1, the one edited now) */
    SAID("V:Track 3, selected|");                       /* (fixed in 0.8.0: speech.c watch: "Track 3" said again after it) */
    track(&M, 0, 0), rest_pop(&M);
    CHECK(said[0] == 0 && M.seq.sel_track == 2 && rfm_move_seq_lock(&M, 0) == 2);
    press(&M, MR_CC_MENU);
    SAID("E:Session|");
    press(&M, MR_CC_MENU);
    SAID("E:Felucca, Scale layout, Track 3|");
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
    jog_press(&M);                                      /* (0.8.0) the jog's click: what it turns */
    SAID("V:Jog: Preset|");
    jog_press(&M);
    SAID("V:Jog: Algo|");
    jog_press(&M), rest_pop(&M), clear();
    rfm_move_set_slot_layout(&M, 0, RFM_MOVE_LAY_SCALE), clear();   /* a note layout: the arrows' octave */
    press(&M, MR_CC_UP);
    SAID("V:Octave 1|");
    press(&M, MR_CC_DOWN);
    SAID("V:Octave 0|");
    memset(kinds, 0, sizeof kinds);                     /* (0.9.0: the buttons are the sequencer's: none reaches the
                                                         * FM-1; Capture with nothing played, Mute alone: the track) */
    press(&M, MR_CC_CAPTURE), press(&M, MR_CC_MUTE), press(&M, MR_CC_LEFT), jog(&M, 1);
    CHECK(kinds[RFM_MOVE_ACT_BTN] == 0 && kinds[RFM_MOVE_ACT_TURN] == 0);
    CHECK(M.seq.tr[0].mute);
    CHECKF(strstr(said, "V:No empty clip|") && strstr(said, "V:Mute|"), "%s", said);
    {                                                   /* the mute said once */
        const char *f = strstr(said, "muted"), *g = f ? strstr(f + 1, "muted") : NULL;
        CHECKF(f && !g, "said \"%s\"", said);         /* CORE? speech.c watch: I_TRACK says "Track 1, muted", then
                                                         * the mutes' watch "Track 1 muted" (said_track sees only the
                                                         * popups' words, not the I_TRACK watch's) */
    }
    clear();
}
static void test_rows(void)
{
    int i;
    fresh(&M, 1);
    /* a note layout: the drum pad's note (pad held + jog), the held menus' layout, velocity */
    track(&M, 0, 1), tap_pad(&M, 24 + RFM_MOVE_LAY_DRUM), track(&M, 0, 0), rest_pop(&M);
    HAS("Layout: Drum");
    pad(&M, 0, 100), jog(&M, 1);
    HAS("V:Pad 1: ");
    pad(&M, 0, 0), rest_pop(&M), clear();
    /* (0.9.0) the Drum layout is the sequencer (track 1); Back with nothing open: where you are */
    CHECK(M.s.mode == RFM_MOVE_MODE_SEQ && M.seq.sel_track == 0);
    press(&M, MR_CC_BACK);
    HAS("V:Felucca, Drum layout, Track 1|");
    button(&M, MR_CC_SHIFT, 1), tap_step(&M, 4), button(&M, MR_CC_SHIFT, 0);   /* Shift + step 5: Tempo */
    HAS("E:Tempo|");
    knob(&M, 0, 1);
    HAS("V:Tempo 121|");
    press(&M, MR_CC_BACK);
    HAS("E:Felucca, Drum layout, Track 1|");            /* (the menu closed: the view again) */
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
    HAS("V:Step 1|");
    step(&M, 0, 1), adv_ms(&M, RFM_MOVE_SEQ_HOLD_MS + 20);
    SAID("V:Step 1|");
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
    /* (0.9.0) the tracks: Track 1 held + a step (selected for slot 1, routed), held (not routed), a double tap
     * (slot 1's solo); Mute + a step (mute) */
    track(&M, 0, 1), tap_step(&M, 1);
    SAID("V:Track 2|V:Track 2, selected|");
    CHECK(M.seq.sel_track == 1 && rfm_move_seq_lock(&M, 0) == 1);
    step(&M, 2, 1), adv_ms(&M, RFM_MOVE_SEQ_HOLD_MS + 50), step(&M, 2, 0);
    SAID("V:Track 3|V:Track 3, not routed|");          /* (the press picks it; the hold stops it reaching slot 1) */
    CHECK(!(M.seq.fx.ffilt[0] >> 2 & 1) && M.seq.fx.ffilt[1] == 0xFFFF);
    {
        const uint16_t f0 = M.seq.fx.ffilt[0];
        tap_step(&M, 4), tap_step(&M, 4);
        HAS("V:Track 5, solo|");                        /* (0.9.0: the firmware's solo, not the sequencer's) */
        CHECK(M.fsolo[0] == 5 && M.seq.fx.ffilt[0] == 1u << 4 && !M.seq.tr[4].solo);
        tap_step(&M, 6), tap_step(&M, 6);               /* another: the solo moves */
        HAS("V:Track 7, solo|");
        CHECK(M.fsolo[0] == 7 && M.seq.fx.ffilt[0] == 1u << 6);
        tap_step(&M, 6), tap_step(&M, 6);               /* again: off, the filter as it was */
        HAS("V:Track 7, solo off|");
        CHECK(!M.fsolo[0] && M.seq.fx.ffilt[0] == f0 && M.seq.fx.ffilt[1] == 0xFFFF);
    }
    button(&M, MR_CC_MUTE, 1), tap_step(&M, 5), button(&M, MR_CC_MUTE, 0);
    HAS("Track 6, muted");
    track(&M, 0, 0), rest_pop(&M), clear();
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
    /* the project grid (0.8.0: Shift + step 1): a project pad */
    button(&M, MR_CC_SHIFT, 1), tap_step(&M, 0), button(&M, MR_CC_SHIFT, 0);
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
    dbl_track(&M, 0), clear();
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
    tap1(&M, 3), tap1(&M, 3);                           /* (0.8.0: Track 4: FX setup, Routing; was Menu, Track 1) */
    HAS("E:FX setup, ");
    tap_pad(&M, fxpad(0, 0)), clear();
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
static void open_global(rfm_move_t *m) { track(m, 3, 1), tap_step(m, 1), track(m, 3, 0); }   /* (0.8.0: Track 4) */
static void test_global(void)
{
    static uint8_t out[MR_SCREEN_BYTES];
    static const int MODES[3] = { RFM_MOVE_MODE_FM1, RFM_MOVE_MODE_MIDI, RFM_MOVE_MODE_SEQ };   /* (0.8.0: no Control) */
    int mode, k, mi;
    for (mi = 0; mi < 3; mi++) {
        mode = MODES[mi];
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
        CHECK(M.seq.smenu == RFM_MOVE_SM_GLOBAL && M.s.mode == (mode == RFM_MOVE_MODE_MIDI ? RFM_MOVE_MODE_SEQ : mode) &&
              !M.pop);                                  /* (0.9.0: MIDI is the note layout: the sequencer) */
        pad(&M, 5, 0);                                  /* (the held pad's release passes) */
        CHECKF(panel_acts() == 1, "mode %d: %d", mode, panel_acts());
        press(&M, MR_CC_BACK);
        CHECK(!M.seq.smenu);
        memset(kinds, 0, sizeof kinds);
        knob(&M, 4, 4);                                 /* (closed: the knobs are the mode's again) */
        CHECK(mode != RFM_MOVE_MODE_FM1 || kinds[RFM_MOVE_ACT_TURN] == 1);
    }
    /* the list: turn, click to edit, turn, click; Back leaves the edit, then closes; the words */
    fresh(&M, 1);
    open_global(&M);
    SAID("E:Global settings, battery unknown|");        /* (0.6.10: the header, the battery) */
    rfm_move_screen(&M, NULL, out), write_pbm("global_select.pbm", out);
    jog(&M, 1);
    SAID("V:Clock: Internal|");                         /* (0.6.12: Speech first) */
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
    SAID("V:Popup size: Full|");                        /* (the last row: it stays, said) */
    rfm_move_set_battery(&M, 63, 0);
    rfm_move_screen(&M, NULL, out), write_pbm("global_battery.pbm", out);
    press(&M, MR_CC_BACK);
    SAID("E:Felucca, FM-1 layout|");
    CHECK(!M.seq.smenu);
    open_global(&M);                                    /* (again: the row kept, no edit; the battery said) */
    SAID("E:Global settings, battery 63%|");
    CHECK(M.gl_row == 4 && !M.gl_edit);
    jog(&M, -3);
    SAID("V:Clock: Internal|");
    open_global(&M);                                    /* (its own toggle closes it) */
    SAID("E:Felucca, FM-1 layout|");
}

/* (0.6.3) the input walk: every input in every context; any that changes the screen must say something */
typedef struct { const char *name; void (*set)(rfm_move_t *m); void (*unset)(rfm_move_t *m); } ctx_t;
/* (0.8.0) the Track buttons' scheme: a slot's layouts (FM-1, note layouts), its held layout menu, Track 4 held, the
 * Sequencer's lock menu; the browser by a double click, FX setup by Track 4 (the MIDI modes' PADS/NOTES and Control
 * mode are gone) */
static void c_fm1(rfm_move_t *m) { (void)m; }
static void c_midi(rfm_move_t *m) { rfm_move_set_mode(m, RFM_MOVE_MODE_MIDI); }
static void c_drum(rfm_move_t *m) { rfm_move_set_slot_layout(m, 0, RFM_MOVE_LAY_DRUM); }
static void c_slot2(rfm_move_t *m) { tap1(m, 1); }
static void c_jogp(rfm_move_t *m) { jog_press(m); }
static void seq_notes(rfm_move_t *m) { tap_pad(m, 0), tap_step(m, 0), tap_step(m, 4), tap_step(m, 8); }
static void c_seq(rfm_move_t *m) { rfm_move_set_slot_layout(m, 0, RFM_MOVE_LAY_SCALE), seq_notes(m); }   /* (0.9.0) */
static void c_session(rfm_move_t *m) { c_seq(m), press(m, MR_CC_MENU); }
static void c_shift(rfm_move_t *m) { c_seq(m), button(m, MR_CC_SHIFT, 1); }
static void u_shift(rfm_move_t *m) { button(m, MR_CC_SHIFT, 0); }
static void c_mshift(rfm_move_t *m) { c_midi(m), button(m, MR_CC_SHIFT, 1); }
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
static void c_t4(rfm_move_t *m) { c_seq(m), track(m, 3, 1), adv_ms(m, RFM_MOVE_TAP_MS + 20); }
static void c_t4f(rfm_move_t *m) { track(m, 3, 1), adv_ms(m, RFM_MOVE_TAP_MS + 20); }
static void u_t4(rfm_move_t *m) { track(m, 3, 0); }
static void c_lock(rfm_move_t *m) { c_seq(m), track(m, 0, 1), adv_ms(m, RFM_MOVE_TAP_MS + 20); }
static void c_t1(rfm_move_t *m) { track(m, 0, 1), adv_ms(m, RFM_MOVE_TAP_MS + 20); }
static void c_t1n(rfm_move_t *m) { c_midi(m), c_t1(m); }
static void u_t1(rfm_move_t *m) { track(m, 0, 0); }
static void c_t2(rfm_move_t *m) { track(m, 1, 1), adv_ms(m, RFM_MOVE_TAP_MS + 20); }
static void u_t2(rfm_move_t *m) { track(m, 1, 0); }
static void sm(rfm_move_t *m, int s) { c_seq(m), button(m, MR_CC_SHIFT, 1), tap_step(m, s), button(m, MR_CC_SHIFT, 0); }
static void c_workflow(rfm_move_t *m) { sm(m, 2); }
static void c_tempo(rfm_move_t *m) { sm(m, 4); }
static void c_groove(rfm_move_t *m) { sm(m, 6); }
static void c_layout(rfm_move_t *m) { sm(m, 7); }
static void c_keys(rfm_move_t *m) { sm(m, 8); }
static void c_vel(rfm_move_t *m) { sm(m, 9); }
static void c_arp(rfm_move_t *m) { sm(m, 10); }
static void c_global(rfm_move_t *m) { sm(m, 1); }
static void c_grid(rfm_move_t *m) { sm(m, 0); }
static void c_fw(rfm_move_t *m) { dbl_track(m, 0), adv_ms(m, RFM_MOVE_DOUBLE_MS + 20); }
static void c_fwsel(rfm_move_t *m) { c_fw(m), tap_step(m, 1), tap_pad(m, 24); }
static void c_catalog(rfm_move_t *m) { c_fw(m), press(m, MR_CC_MUTE); }
static void c_states(rfm_move_t *m) { c_fw(m), press(m, MR_CC_LOOP); }
static void c_capture(rfm_move_t *m) { c_fwsel(m), button(m, MR_CC_CAPTURE, 1); }
static void u_capture(rfm_move_t *m) { button(m, MR_CC_CAPTURE, 0); }
static void c_fx(rfm_move_t *m) { tap1(m, 3), tap_pad(m, fxpad(0, 0)); }
static void c_mixer(rfm_move_t *m) { tap1(m, 3), tap1(m, 3); }
static void c_routing(rfm_move_t *m) { tap1(m, 3); }
static const ctx_t CTX[] = {
    { "FM-1", c_fm1, NULL }, { "Note layout", c_midi, NULL }, { "Drum layout", c_drum, NULL },
    { "Slot 2 FM-1", c_slot2, NULL }, { "FM-1 jog Preset", c_jogp, NULL },
    { "Note Shift held", c_mshift, u_shift },          /* (fixed in 0.8.0: Shift's icon comes and goes unsaid in a note layout) */
    { "Seq track", c_seq, NULL }, { "Seq session", c_session, NULL },
    { "Seq Shift held", c_shift, u_shift }, { "Seq Copy held", c_copy, u_copy }, { "Seq Delete held", c_delete, u_delete },
    { "Seq Mute held", c_mute, u_mute }, { "Seq Loop held", c_loop, u_loop }, { "Seq step held", c_step, u_step },
    { "Track 4 held", c_t4, u_t4 }, { "Track 4 held FM-1", c_t4f, u_t4 }, { "Track 1 lock", c_lock, u_t1 },
    { "Track 1 held", c_t1, u_t1 }, { "Track 1 held notes", c_t1n, u_t1 }, { "Track 2 held", c_t2, u_t2 },
    { "Workflow", c_workflow, NULL }, { "Tempo", c_tempo, NULL },
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
/* (0.6.5) playing notes says nothing: pads and steps as keys in FM-1 mode, pads in the MIDI modes, the sequencer's pads
 * live and recorded */
static void test_playing(void)
{
    int i, mode;
    for (mode = RFM_MOVE_MODE_FM1; mode <= RFM_MOVE_MODE_SEQ; mode++) {
        fresh(&M, 1);
        rfm_move_set_mode(&M, mode), rest_pop(&M), clear();
        for (i = 0; i < 16; i++)
            tap_pad(&M, i * 2), adv_ms(&M, 30);
        if (mode == RFM_MOVE_MODE_FM1)
            for (i = 0; i < 16; i++)
                tap_step(&M, i);                        /* (the steps: the white keys) */
        CHECKF(!said[0], "mode %d: \"%s\"", mode, said);
    }
    fresh(&M, 1);                                       /* recording: the notes land on steps, nothing said */
    rfm_move_set_slot_layout(&M, 0, RFM_MOVE_LAY_SCALE), press(&M, MR_CC_REC), press(&M, MR_CC_PLAY), adv_ms(&M, 2600);
    clear();
    for (i = 0; i < 12; i++)
        pad(&M, i % 8, 100), adv_ms(&M, 60), pad(&M, i % 8, 0), adv_ms(&M, 60);
    CHECKF(!strstr(said, "Step") && M.seq.nnotes > 0, "\"%s\" (%d notes)", said, M.seq.nnotes);
    press(&M, MR_CC_PLAY), clear();
}
/* (0.6.6) MIDI routes on the Routing page: the words of a toggle and of a refused cycle */
static void test_mroute(void)
{
    fresh(&M, 1);
    tap1(&M, 3), tap1(&M, 3), rest_pop(&M), clear();   /* (0.8.0: Track 4 twice: Routing) */
    pad(&M, fxpad(1, 0), 100);
    tap_pad(&M, fxpad(1, 1));
    SAID("V:Felucca to Melodee, on|");
    tap_pad(&M, fxpad(1, 3));
    SAID("V:Felucca to Out, on|");
    tap_pad(&M, fxpad(1, 3));
    SAID("V:Felucca to Out, off|");
    pad(&M, fxpad(1, 0), 0), rest_pop(&M), clear();
    pad(&M, fxpad(1, 1), 100);
    SAID("V:Slot 2, Melodee|");                         /* (the press: slot 2 selected) */
    tap_pad(&M, fxpad(1, 0));
    SAID("V:Loop|");
    pad(&M, fxpad(1, 1), 0);
}
/* (0.6.7) knob words with firmware readouts (ChoralRoot's rd1): the readout's full title and value, never the Move's
 * placeholder or a stale readout of the other (Shift) page; a touch says "Name: value" when known; a turn with no
 * readout (its range's end) says it again once */
static void rd(const char *t, const char *v) { rfm_move_readout(&M, -1, RFM_MOVE_RD_PARAM, t, v, 500, 0, 0); }
static void test_knob_readouts(void)
{
    const char *lbl[RFM_MOVE_LBL_N] = { NULL };
    fresh(&M, 1);
    lbl[RFM_MOVE_LBL_KNOB0 + 3] = "Bass Regis", lbl[RFM_MOVE_LBL_KNOB0 + 4] = "Voicing";   /* (K1, K2: 10 kept) */
    rfm_move_set_labels(&M, lbl);
    clear();
    /* the first touch: the label; a turn short of a detent: no "0"; the readout: its full title and value */
    touch(&M, 4, 1);
    SAID("V:Bass Regis|");
    knob(&M, 4, 1);
    SAID("");
    knob(&M, 4, 1);                                     /* (a detent: before any readout, the count) */
    SAID("V:Bass Regis: +1|");
    rd("Bass Register", "+1 oct");
    SAID("V:Bass Register: +1 oct|");
    /* 1. readouts known: a turn says nothing until its readout, then that once */
    knob(&M, 4, 2);
    SAID("");
    rd("Bass Register", "+2 oct");
    SAID("V:Bass Register: +2 oct|");
    /* 4. the same value back (a readout for the turn): (0.6.8) once, by the range's end repeat, not twice */
    knob(&M, 4, 2), rd("Bass Register", "+2 oct");
    SAID("");
    adv_ms(&M, RFM_MOVE_SAY_TURN_MS + 20);
    SAID("V:Bass Register: +2 oct|");
    /* 4. the range's end: no readout; its words again once, a moment later */
    knob(&M, 4, 2);
    SAID("");
    adv_ms(&M, RFM_MOVE_SAY_TURN_MS + 20);
    SAID("V:Bass Register: +2 oct|");
    adv_ms(&M, 500);
    SAID("");
    touch(&M, 4, 0), rest_pop(&M), clear();
    /* 3, 4. touched again: "Name: value" at once (the full name) */
    touch(&M, 4, 1);
    SAID("V:Bass Register: +2 oct|");
    touch(&M, 4, 0), rest_pop(&M), clear();
    /* 2. the Shift page: the firmware relabels; the old page's readout is never said */
    lbl[RFM_MOVE_LBL_KNOB0 + 3] = "Opt";
    rfm_move_set_labels(&M, lbl);
    touch(&M, 4, 1);
    SAID("V:Opt|");
    knob(&M, 4, 2);
    SAID("");
    rd("split point", "F# split off");
    SAID("V:split point: F# split off|");
    touch(&M, 4, 0), rest_pop(&M), clear();
    /* another knob, no readout of its own yet: its label; its turn waits for its readout */
    touch(&M, 5, 1);
    SAID("V:Voicing|");
    knob(&M, 5, 2);
    SAID("");
    rd("Voicing", "+1");
    SAID("V:Voicing: +1|");
    touch(&M, 5, 0), rest_pop(&M), clear();
    /* back on the plain page: its old readout (the other page's) not said; (0.6.8) its full title seen before */
    lbl[RFM_MOVE_LBL_KNOB0 + 3] = "Bass Regis";
    rfm_move_set_labels(&M, lbl);
    touch(&M, 4, 1);
    SAID("V:Bass Register|");
    touch(&M, 4, 0), rest_pop(&M), clear();
    /* a readout cleared (another firmware): the labels and counts again */
    rfm_move_readouts_clear(&M);
    touch(&M, 5, 1);
    SAID("V:Voicing|");
    touch(&M, 5, 0), rest_pop(&M), clear();
}
/* (0.6.8) readouts given for control -1 (paired with the knob in use) on the FM-1's Shift page (GLO held): the plain
 * page's never said; one touch-and-turn says each words once; the full title remembered per page */
static void test_knob_pages(void)
{
    fresh(&M, 1);
    clear();
    touch(&M, 1, 1);                                    /* (0.8.0: knob 2: PRESETS) */
    SAID("V:Presets|");
    knob(&M, 1, 2);
    SAID("V:Presets: +1|");                             /* (no readout seen yet from this firmware: the count, once) */
    rd("FM6", "FM BELL");
    SAID("V:FM6: FM BELL|");
    adv_ms(&M, RFM_MOVE_SAY_TURN_MS + 20);
    SAID("");                                           /* (answered: no repeat) */
    touch(&M, 1, 0), rest_pop(&M), clear();
    button(&M, MR_CC_SHIFT, 1);                         /* (the FM-1's GLO: the firmware's Shift page) */
    SAID("V:Global|");
    touch(&M, 1, 1);
    SAID("V:Presets|");                                 /* (not the plain page's "FM6: FM BELL") */
    knob(&M, 1, 2);
    SAID("");
    rd("FM TONE", "EP");
    SAID("V:FM TONE: EP|");
    knob(&M, 1, 2), rd("FM TONE", "EP");                /* (the range's end: the same readout back) */
    SAID("");
    adv_ms(&M, RFM_MOVE_SAY_TURN_MS + 20);
    SAID("V:FM TONE: EP|");
    adv_ms(&M, 600);
    SAID("");
    touch(&M, 1, 0), rest_pop(&M), clear();
    touch(&M, 1, 1);                                    /* (Shift still held: this page's own) */
    SAID("V:FM TONE: EP|");
    touch(&M, 1, 0), button(&M, MR_CC_SHIFT, 0), rest_pop(&M), clear();
    touch(&M, 1, 1);                                    /* (plain again: the plain page's title) */
    SAID("V:FM6|");
    touch(&M, 1, 0), rest_pop(&M), clear();
    /* a turn with no readout at all on a firmware with readouts: nothing, not the name again */
    touch(&M, 5, 1);
    SAID("V:K2|");
    knob(&M, 5, 2);
    adv_ms(&M, RFM_MOVE_SAY_TURN_MS + 20);
    SAID("");
    touch(&M, 5, 0), rest_pop(&M), clear();
}
/* (0.6.9) a favourite moved with knob 2: "Name, position N of M" */
static void test_fav_order(void)
{
    int ids[32], n;
    char want[96];
    fresh(&M, 1);
    dbl_track(&M, 0), rest_pop(&M);                     /* (0.8.0: the browser by Track 1's double click) */
    tap_step(&M, 1), rest_pop(&M);                      /* (All) */
    tap_pad(&M, pad_of(0)), press(&M, MR_CC_SAMPLE);   /* (two favourites) */
    tap_pad(&M, pad_of(1)), press(&M, MR_CC_SAMPLE);
    tap_step(&M, 0), rest_pop(&M);                      /* (Favorites) */
    n = rfmv_fw_bank(&M, ids);
    CHECK(n == 2);
    tap_pad(&M, pad_of(0)), clear();
    knob(&M, 1, RFM_MOVE_LIST_TICKS);
    snprintf(want, sizeof want, "V:%s, position 2 of 2|", E[ids[0]].name);
    SAID(want);
    knob(&M, 1, RFM_MOVE_LIST_TICKS);                   /* (the end: no move, nothing new said) */
    SAID("");
    rest_pop(&M), clear();
    touch(&M, 1, 1);                                    /* (0.6.13) touched: what it does, where it is */
    snprintf(want, sizeof want, "V:Move %s, 2 of 2|", E[ids[0]].name);
    SAID(want);
    touch(&M, 1, 0), rest_pop(&M), clear();
    touch(&M, 0, 1);                                    /* (knob 1: the colour) */
    CHECK(!strncmp(said, "V:Colour: ", 10));
    touch(&M, 0, 0), rest_pop(&M), clear();
}
/* (0.6.9) the host sweep's gesture: touch, half a detent, a detent later, no readout yet past 250 ms: the name once;
 * then the readout's "Name: value". A readout naming its control (>= 0): its title reused on the Shift page */
static void test_knob_twice(void)
{
    const char *lbl[RFM_MOVE_LBL_N] = { NULL };
    fresh(&M, 1);
    lbl[RFM_MOVE_LBL_KNOB0 + 4] = "Voicing", lbl[RFM_MOVE_LBL_KNOB0 + 5] = "Bass Regis";
    rfm_move_set_labels(&M, lbl);
    rfm_move_readout(&M, RFM_MOVE_LBL_KNOB0 + 3, RFM_MOVE_RD_PARAM, "Other", "1", -1, 0, 0);   /* (readouts seen) */
    rest_pop(&M), clear();
    touch(&M, 5, 1);                                    /* (K2: Voicing) */
    SAID("V:Voicing|");
    adv_ms(&M, 500);
    knob(&M, 5, 1);                                     /* (half a detent) */
    adv_ms(&M, 300);
    knob(&M, 5, 1);                                     /* (a detent) */
    adv_ms(&M, 300);                                    /* (no readout: the end repeat has no value) */
    SAID("");
    rd("Voicing", "+1");
    SAID("V:Voicing: +1|");
    touch(&M, 5, 0), rest_pop(&M), clear();
    /* control 5 (K3) named by the host on the plain page: its title on the Shift page's first touch */
    rfm_move_readout(&M, RFM_MOVE_LBL_KNOB0 + 5, RFM_MOVE_RD_PARAM, "Bass Register", "+1 oct", -1, 0, 0);
    rest_pop(&M), clear();
    button(&M, MR_CC_SHIFT, 1), clear();
    touch(&M, 6, 1);                                    /* (Move knob 7: K3) */
    SAID("V:Bass Register|");
    touch(&M, 6, 0), button(&M, MR_CC_SHIFT, 0), rest_pop(&M), clear();
}
/* (0.6.11) the firmware speaks for its own controls (fm1-reader): the core steps back from them, keeps its own */
static void test_fw_speaks(void)
{
    const char *lbl[RFM_MOVE_LBL_N] = { NULL };
    char want[48];
    fresh(&M, 1);
    lbl[RFM_MOVE_LBL_KNOB0 + 3] = "Cutoff", lbl[RFM_MOVE_LBL_BTN0 + 1] = "SEL/KEYS";
    rfm_move_set_labels(&M, lbl);
    CHECK(rfm_move_fw_speech(&M) == 0);
    rfm_move_set_fw_speech(&M, 1);
    CHECK(rfm_move_fw_speech(&M) == 1);
    clear();
    touch(&M, 4, 1);                                    /* a touched knob: its name (kept) */
    SAID("V:Cutoff|");
    knob(&M, 4, 2), knob(&M, 4, 2);                     /* turns: nothing (the count, the end repeat) */
    adv_ms(&M, RFM_MOVE_SAY_TURN_MS + 50);
    SAID("");
    rd("Cutoff", "64");                                 /* its readout: the firmware's to say */
    SAID("");
    touch(&M, 4, 0), rest_pop(&M), clear();
    touch(&M, 4, 1);                                    /* (touched again with a readout known: the name only) */
    SAID("V:Cutoff|");
    touch(&M, 4, 0), rest_pop(&M), clear();
    rfm_move_readout(&M, -1, RFM_MOVE_RD_MESSAGE, "Saved", "", -1, 0, 0);   /* a readout's own popup: nothing */
    SAID("");
    rest_pop(&M);
    press(&M, MR_CC_PLAY);                              /* an FM-1 button's press: nothing */
    SAID("");
    button(&M, MR_CC_SAMPLE, 1), adv_ms(&M, RFM_MOVE_LABEL_HOLD_MS + 20);   /* its label held: nothing */
    SAID("");
    button(&M, MR_CC_SAMPLE, 0), rest_pop(&M), clear();
    jog(&M, 1);                                         /* SELECT turned: nothing */
    SAID("");
    rest_pop(&M), clear();
    knob(&M, 8, 1);                                     /* RemoteFM1's own: the volume, modes, menus */
    snprintf(want, sizeof want, "V:Volume %d%%|", (M.master * 100 + 511) / 1023);
    SAID(want);
    rest_pop(&M), clear();
    tap1(&M, 1);
    SAID("E:Melodee, FM-1 layout|V:Melodee, FM-1|");
    rest_pop(&M);                                       /* (its popup gone: not shown again after the menu) */
    track(&M, 1, 1), tap_pad(&M, 17), track(&M, 1, 0);
    SAID("V:Scale: Minor|");
    tap1(&M, 0), rest_pop(&M), clear();
    open_global(&M);
    SAID("E:Global settings, battery unknown|");
    press(&M, MR_CC_BACK), rest_pop(&M), clear();
    /* off again: the core's words for them return */
    rfm_move_set_fw_speech(&M, 0);
    touch(&M, 4, 1);
    SAID("V:Cutoff: 64|");
    knob(&M, 4, 2), rd("Cutoff", "65");
    SAID("V:Cutoff: 65|");
    touch(&M, 4, 0), rest_pop(&M), clear();
    press(&M, MR_CC_PLAY);
    SAID("V:Play|");
}
/* (0.6.12) the screen reader's modes: Off (the default), On, Jog (only while the jog is touched; a touch says the
 * screen); the host's first-run default; the Speech row (its change said in any mode) */
static void test_speech_modes(void)
{
    char want[96];
    fresh(&M, 0);                                       /* (speech tracking on, the default left Off) */
    M.say_default = RFM_MOVE_SPEECH_OFF;
    rfm_move_set_speech(&M, 1);
    clear();
    CHECK(rfm_move_speech_mode(&M) == RFM_MOVE_SPEECH_OFF && !rfm_move_speech_open(&M) && M.s.speech == 0);
    tap1(&M, 1), knob(&M, 8, 1), press(&M, MR_CC_PLAY);
    SAID("");                                           /* Off: nothing at all */
    /* the host's default: only while nothing is chosen */
    rfm_move_set_speech_default(&M, RFM_MOVE_SPEECH_ON);
    CHECK(rfm_move_speech_mode(&M) == RFM_MOVE_SPEECH_ON && rfm_move_speech_open(&M));
    M.s.speech = 1 + RFM_MOVE_SPEECH_OFF;
    CHECK(rfm_move_speech_mode(&M) == RFM_MOVE_SPEECH_OFF && !rfm_move_speech_open(&M));
    rfm_move_set_speech_default(&M, 7);                 /* (out of range: Off) */
    M.s.speech = 0;
    CHECK(rfm_move_speech_mode(&M) == RFM_MOVE_SPEECH_OFF);
    /* the Speech row: Off, yet its change is said; the settings saved */
    tap1(&M, 0), rest_pop(&M), clear();
    open_global(&M);
    SAID("");                                           /* (Off: the menu's name not said) */
    CHECK(M.gl_row == 0);
    jog_press(&M);
    SAID("");
    memset(kinds, 0, sizeof kinds);
    jog(&M, 1);
    SAID("V:Speech on|");
    CHECK(M.s.speech == 1 + RFM_MOVE_SPEECH_ON && kinds[RFM_MOVE_ACT_SETTINGS] == 1);
    jog(&M, 1);
    SAID("V:Speech jog wheel|");
    jog(&M, -2);
    SAID("V:Speech off|");                              /* (said, though Off now) */
    jog(&M, 1);
    SAID("V:Speech on|");
    jog_press(&M);
    SAID("V:Done|");                                    /* (On now: every word again) */
    press(&M, MR_CC_BACK);
    SAID("E:Felucca, FM-1 layout|");
    /* Jog: silent until the jog is touched; a touch says the screen; while held, every word; let go: silent */
    M.s.speech = 1 + RFM_MOVE_SPEECH_JOG;
    rfm_move_set_slot_layout(&M, 0, RFM_MOVE_LAY_SCALE), rest_pop(&M);
    SAID("");
    CHECK(!rfm_move_speech_open(&M));
    touch(&M, 9, 1);
    SAID("V:Felucca, Scale layout, Track 1|");
    CHECK(rfm_move_speech_open(&M));
    knob(&M, 0, 1);
    SAID("V:Micro: +1|");                               /* (0.9.0: the sequencer's TRIG knobs) */
    touch(&M, 9, 0);
    CHECK(!rfm_move_speech_open(&M));
    rest_pop(&M), tap1(&M, 1), rest_pop(&M);
    SAID("");
    touch(&M, 9, 1);                                    /* (a new touch: the screen again) */
    SAID("V:Melodee, FM-1 layout|");
    touch(&M, 9, 0), clear();
    /* Jog in Global Settings: the jog's touch passes (the menu owns the rest) */
    tap1(&M, 0), rest_pop(&M), clear();
    open_global(&M), clear();
    touch(&M, 9, 1);
    snprintf(want, sizeof want, "V:Global settings, battery unknown, Speech: Jog wheel|");
    SAID(want);
    touch(&M, 9, 0), press(&M, MR_CC_BACK), clear();
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
            if (INS[i].kind == 2 && CTX[c].set != c_fw && CTX[c].set != c_fwsel && CTX[c].set != c_catalog &&
                CTX[c].set != c_states && CTX[c].set != c_capture && CTX[c].set != c_fx && CTX[c].set != c_mixer &&
                CTX[c].set != c_routing && CTX[c].set != c_grid && CTX[c].set != c_session && CTX[c].set != c_global &&
                CTX[c].set != c_t1 && CTX[c].set != c_t1n && CTX[c].set != c_t2 && CTX[c].set != c_t4 &&
                CTX[c].set != c_t4f && CTX[c].set != c_lock && CTX[c].set != c_delete)
                continue;                               /* (0.6.5: pads play notes: heard, never said) */
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
static void seq_three(rfm_move_t *m)                  /* the sequencer (0.9.0: slot 1's Scale layout), a note on steps
                                                         * 1, 2, 5 of track 1 */
{
    fresh(m, 1);
    rfm_move_set_slot_layout(m, 0, RFM_MOVE_LAY_SCALE), tap_pad(m, 0), tap_step(m, 0), tap_step(m, 1), tap_step(m, 4), rest_pop(m), clear();
}
static void test_delete(void)
{
    const int c0 = SEQ_CLIP(0, 0);
    int n0;
    /* Delete tapped, Delete held and let go: nothing changes, (0.6.4) the screen neither; a hint, spoken only */
    seq_three(&M);
    n0 = all_notes(&M);
    {
        static uint8_t a[MR_SCREEN_BYTES], b[MR_SCREEN_BYTES];
        rfm_move_screen(&M, NULL, a);
        press(&M, MR_CC_DELETE);
        rfm_move_screen(&M, NULL, b);
        CHECK(!memcmp(a, b, sizeof a) && !M.seq.msg);
        HAS("V:Delete: hold and press a step, pad or clip|");
        button(&M, MR_CC_DELETE, 1);                    /* (held: the track view as ever, the canvas's seq.clip) */
        rfm_move_screen(&M, NULL, b);
        write_pbm("delete_held.pbm", b);
        CHECK(!memcmp(a, b, sizeof a));
        tap_step(&M, 4);
        rfm_move_screen(&M, NULL, b);
        write_pbm("delete_cut.pbm", b);                 /* (step 5 crossed) */
        CHECK(memcmp(a, b, sizeof a) != 0);
        tap_step(&M, 8);
        rfm_move_screen(&M, NULL, b);
        write_pbm("delete_pasted.pbm", b);
        button(&M, MR_CC_DELETE, 0);
        press(&M, MR_CC_UNDO), press(&M, MR_CC_UNDO), rest_pop(&M), clear();
    }
    CHECK(all_notes(&M) == n0 && rfmv_seq_clip_has(&M, c0) && !M.seq.cut && M.seq.clip_kind == 1);   /* (undone; the clipboard stays) */
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
    /* (0.8.0: was Track 4) a firmware Track held (the lock menu) + Delete: the selected track cleared; + a step: that
     * track */
    seq_three(&M);
    track(&M, 0, 1), button(&M, MR_CC_DELETE, 1), button(&M, MR_CC_DELETE, 0), track(&M, 0, 0);
    CHECK(all_notes(&M) == 0);
    HAS("V:Track 1 cleared|");
    seq_three(&M);
    track(&M, 0, 1), tap_step(&M, 1), track(&M, 0, 0), tap_pad(&M, 0), tap_step(&M, 0), clear();   /* (track 2: a note) */
    CHECK(M.seq.sel_track == 1 && all_notes(&M) == 4);
    track(&M, 0, 1), button(&M, MR_CC_DELETE, 1), tap_step(&M, 0), button(&M, MR_CC_DELETE, 0), track(&M, 0, 0);
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
    test_playing();
    test_mroute();
    test_knob_readouts();
    test_knob_pages();
    test_fav_order();
    test_knob_twice();
    test_fw_speaks();
    test_speech_modes();
    test_walk();
    CHECKF(bad_words == 0, "%d SAY actions with bad words", bad_words);
    return check_done("move_speech");
}
