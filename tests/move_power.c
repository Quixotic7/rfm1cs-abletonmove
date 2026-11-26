/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (2026-11-26, the owner's) the power button as Move's own software has it: a press puts up "Shut Down?", the jog
 * click sends RFM_MOVE_ACT_POWEROFF, Back / the power button again / any other press closes it and does nothing else.
 * The presses go in as the surface's own power SysEx reports, through mr_decode (a fake Move). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "move_int.h"
#include "moveremote/client.h"
#include "rfm1cs/move.h"

static rfm_move_act_t A[512];
static int nact;
static char said[4096];
static void on_act(void *ctx, const rfm_move_act_t *a)
{
    (void)ctx;
    if (a->kind == RFM_MOVE_ACT_SAY) {
        snprintf(said + strlen(said), sizeof said - strlen(said), "%s|", (const char *)a->data);
        return;
    }
    if (nact < (int)(sizeof A / sizeof A[0]))
        A[nact] = *a, A[nact].data = NULL, nact++;
}
static void clear(void) { nact = 0, said[0] = 0; }
static int count(int kind)
{
    int i, n = 0;
    for (i = 0; i < nact; i++)
        n += A[i].kind == kind;
    return n;
}

static rfm_move_t M;
static uint32_t now_ms;
static void pkt(const uint8_t p[4])
{
    mr_event_t ev;
    mr_decode(p, 0, &ev);
    rfm_move_input(&M, &ev, now_ms += 5);
}
static void raw(uint8_t st, uint8_t d1, uint8_t d2)
{
    const uint8_t p[4] = { (uint8_t)(st >> 4), st, d1, d2 };
    pkt(p);
}
static void adv_ms(uint32_t ms)
{
    while (ms) {
        const uint32_t k = ms < 10 ? ms : 10;
        rfm_move_seq_advance(&M, k * 48, 48000);
        now_ms += k, ms -= k;
    }
}
/* the surface's report: F0 00 21 1D 01 01 3A bits battery 00 F7, bit 3 the button held */
static void power_report(int held)
{
    const uint8_t p[4][4] = { { 0x04, 0xF0, 0x00, 0x21 }, { 0x04, 0x1D, 0x01, 0x01 },
                              { 0x04, 0x3A, (uint8_t)(held ? 0x08 : 0x00), 80 }, { 0x06, 0x00, 0xF7, 0x00 } };
    int i;
    for (i = 0; i < 4; i++)
        pkt(p[i]);
}
static void power_press(void) { power_report(1), adv_ms(80), power_report(0), adv_ms(200); }
static void pad(int p, int vel) { raw(vel ? 0x90 : 0x80, (uint8_t)(MR_NOTE_PAD0 + p), (uint8_t)vel); }
static void step(int s, int down) { raw(down ? 0x90 : 0x80, (uint8_t)(MR_NOTE_STEP0 + s), down ? 127 : 0); }
static void button(int cc, int down) { raw(0xB0, (uint8_t)cc, down ? 127 : 0); }
static void press(int cc) { button(cc, 1), button(cc, 0); }
static void jog_click(void) { raw(0xB0, MR_CC_JOG_CLICK, 127), raw(0xB0, MR_CC_JOG_CLICK, 0); }

static uint8_t SCR[MR_SCREEN_BYTES], POP[MR_SCREEN_BYTES];
static void screen(void) { rfm_move_screen(&M, NULL, SCR); }

static void fresh(void)
{
    now_ms = 1000;
    rfm_move_init(&M, NULL, on_act, NULL);
    adv_ms(RFM_MOVE_POPUP_MS + 50);
    now_ms += RFM_MOVE_DOUBLE_MS;
    clear();
}

int main(void)
{
    int mode;
    /* the press: the popup, said; the release keeps it */
    fresh();
    rfm_move_set_speech(&M, 1);
    clear();
    screen();
    memcpy(POP, SCR, sizeof SCR);
    power_report(1);
    CHECK(rfm_move_power_asked(&M));
    CHECKF(strstr(said, "Shut Down?") != NULL, "said %s", said);
    power_report(1);                                    /* (a held report again: not another press) */
    CHECK(rfm_move_power_asked(&M));
    adv_ms(80), power_report(0), adv_ms(200);
    CHECK(rfm_move_power_asked(&M));
    screen();
    CHECK(memcmp(POP, SCR, sizeof SCR) != 0);
    memcpy(POP, SCR, sizeof SCR);
    {                                                   /* (the existing popup style: "Shut Down?" alone) */
        mr_canvas_t cv;
        uint8_t want[MR_SCREEN_BYTES];
        rfmv_pop_screen(&cv, NULL, "Shut Down?", 0, 0, NULL);
        mr_gfx_pack(&cv, want);
        CHECK(memcmp(want, SCR, sizeof SCR) == 0);
    }
    /* knob and jog turns, touches: ignored */
    clear();
    mode = rfm_move_mode(&M);
    raw(0xB0, MR_CC_KNOB1, 1), raw(0xB0, MR_CC_JOG, 1), raw(0x90, MR_NOTE_KNOB_TOUCH0, 127), raw(0xB0, MR_CC_VOLUME, 1);
    CHECK(rfm_move_power_asked(&M));
    CHECKF(nact == 0, "%d actions", nact);
    raw(0x80, MR_NOTE_KNOB_TOUCH0, 0);
    /* Back: closed, nothing else (its click would flip the layout) */
    clear();
    press(MR_CC_BACK);
    adv_ms(RFM_MOVE_DOUBLE_MS + 50);
    CHECK(!rfm_move_power_asked(&M));
    CHECKF(rfm_move_mode(&M) == mode, "mode %d -> %d", mode, rfm_move_mode(&M));
    CHECK(count(RFM_MOVE_ACT_POWEROFF) == 0);
    screen();
    CHECK(memcmp(POP, SCR, sizeof SCR) != 0);

    /* the power button again: closed */
    power_press();
    CHECK(rfm_move_power_asked(&M));
    power_press();
    CHECK(!rfm_move_power_asked(&M));
    CHECK(count(RFM_MOVE_ACT_POWEROFF) == 0);

    /* a pad: closed, the pad plays nothing (press and release taken) */
    power_press();
    clear();
    pad(5, 100), pad(5, 0);
    CHECK(!rfm_move_power_asked(&M));
    CHECKF(nact == 0, "%d actions (kind %d)", nact, nact ? A[0].kind : -1);
    /* .. and once closed a pad plays again */
    clear();
    pad(5, 100), pad(5, 0);
    CHECK(nact > 0);

    /* a step, Shift, Play: each closes it and is taken */
    power_press();
    clear();
    step(3, 1), step(3, 0);
    CHECK(!rfm_move_power_asked(&M) && nact == 0);
    power_press();
    clear();
    press(MR_CC_PLAY);
    CHECK(!rfm_move_power_asked(&M) && nact == 0 && !M.seq.playing && !M.seq.start_pending);

    /* a pad held from before: its release still lets the note go */
    clear();
    pad(7, 100);
    CHECK(nact > 0);
    power_press();
    clear();
    pad(7, 0);
    CHECK(rfm_move_power_asked(&M));
    CHECK(nact > 0);

    /* the jog click: shut down, once; its release goes nowhere */
    clear();
    jog_click();
    CHECK(!rfm_move_power_asked(&M));
    CHECK(count(RFM_MOVE_ACT_POWEROFF) == 1);
    CHECK(nact == 1);

    /* a bounce: one press */
    fresh();
    power_report(1), power_report(0), power_report(1), power_report(0);
    CHECK(rfm_move_power_asked(&M));
    adv_ms(300);

    /* over the sequencer and a menu (Global settings) too */
    fresh();
    rfm_move_set_mode(&M, RFM_MOVE_MODE_SEQ);
    power_press();
    CHECK(rfm_move_power_asked(&M));
    clear();
    jog_click();
    CHECK(count(RFM_MOVE_ACT_POWEROFF) == 1);
    fresh();
    M.seq.smenu = RFM_MOVE_SM_GLOBAL;
    power_press();
    CHECK(rfm_move_power_asked(&M));
    clear();
    jog_click();
    CHECK(count(RFM_MOVE_ACT_POWEROFF) == 1);
    CHECK(M.seq.smenu == RFM_MOVE_SM_GLOBAL && !M.gl_edit);   /* (the menu saw no click) */

    /* the host's own call */
    fresh();
    rfm_move_power(&M, 0);
    CHECK(!rfm_move_power_asked(&M));
    rfm_move_power(&M, 1);
    CHECK(rfm_move_power_asked(&M));
    return check_done("move_power");
}
