/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (0.7.0) the takeover: a firmware's own Move interface (move.h, rfm_move_set_takeover; README "Takeover"; FM1VST
 * docs/RFM1-TAKEOVER.md, tk1). The host hands the core each slot's claims, name, screen, LEDs and words; the core
 * shows them while the active slot's takeover is up and sends the firmware the claimed controls (TK_INPUT) and what
 * it is granted (TK_STATE). It is a slot's layout (0.8.0, RFM_MOVE_LAY_TK): the settings' mode stays FM-1 under it,
 * so every part not claimed is the FM-1 layout's. */
#include <string.h>

#include "move_int.h"

#define clampi rfmv_clampi

static int tk_slot(const rfm_move_t *m) { return m->fw.active < RFM_MOVE_FW_SLOTS ? m->fw.active : 0; }

/* the active slot was left in its takeover and has one (shown or not: the projects view, a Shift menu over it) */
int rfmv_tk_mode(const rfm_move_t *m)
{
    const int s = tk_slot(m);                           /* (0.8.0: the slot's layout is its takeover, proposal 2) */
    return m->s.mode == RFM_MOVE_MODE_FM1 && m->s.slot[s].lay == RFM_MOVE_LAY_TK && m->tk[s].claims;
}
/* .. and shown now: the claimed controls are the firmware's */
int rfmv_tk_on(const rfm_move_t *m) { return rfmv_tk_mode(m) && !m->seq.projects && !m->seq.smenu; }
unsigned rfmv_tk_claims(const rfm_move_t *m) { return rfmv_tk_on(m) ? m->tk[tk_slot(m)].claims : 0; }
int rfm_move_tk_shown(const rfm_move_t *m) { return rfmv_tk_on(m) ? tk_slot(m) : -1; }
int rfm_move_mode(const rfm_move_t *m) { return rfmv_tk_mode(m) ? RFM_MOVE_MODE_TAKEOVER : m->s.mode; }
const char *rfmv_tk_name(const rfm_move_t *m) { return m->tk[tk_slot(m)].name; }

/* the name popup on entering, or FM-1 mode's on leaving */
void rfmv_tk_mode_pop(rfm_move_t *m)
{
    if (rfmv_tk_mode(m))
        rfmv_pop(m, POP_TK, tk_slot(m));
    else
        rfmv_pop(m, POP_MODE, m->s.mode);
}
/* the active slot's takeover opened (1) or left for FM-1 mode (0), named */
void rfmv_tk_set(rfm_move_t *m, int on)
{
    const int s = tk_slot(m);
    if (on && !m->tk[s].claims)
        return;
    m->tk[s].mode = (uint8_t)(on != 0);
    rfmv_set_slot_lay(m, s, on ? RFM_MOVE_LAY_TK : RFM_MOVE_LAY_FM1);   /* (0.8.0: the slot's layout) */
    if (on)
        rfmv_pop(m, POP_TK, s);
    else
        rfmv_pop(m, POP_LAYOUT, s);
}

/* ---- the actions */
static void emit_pkt(rfm_move_t *m, int slot, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.frame = m->seq.frame;
    a.kind = RFM_MOVE_ACT_TK_INPUT, a.slot = (uint8_t)slot;
    a.pkt[0] = b0, a.pkt[1] = b1, a.pkt[2] = b2, a.pkt[3] = b3;
    a.data = a.pkt, a.len = 4;
    if (m->fn)
        m->fn(m->ctx, &a);
}
static void emit_state(rfm_move_t *m, int slot, int on, unsigned claims, unsigned flags)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.frame = m->seq.frame;
    a.kind = RFM_MOVE_ACT_TK_STATE, a.slot = (uint8_t)slot, a.down = (uint8_t)on;
    a.tk_claims = (uint16_t)claims, a.tk_flags = (uint8_t)flags;
    if (m->fn)
        m->fn(m->ctx, &a);
}
static int bit(const uint32_t *b, int i) { return (b[i >> 5] >> (i & 31)) & 1; }
static void bit_set(uint32_t *b, int i, int on)
{
    if (on)
        b[i >> 5] |= 1u << (i & 31);
    else
        b[i >> 5] &= ~(1u << (i & 31));
}
/* a release for everything the firmware holds (to the slot shown last), the claims dropped first: mask the claims
 * still kept (their controls stay held) */
static void release_fw(rfm_move_t *m, unsigned keep)
{
    const int s = m->tk_sent_slot;
    int i;
    if (!(keep & RFM_MOVE_TK_PADS))
        for (i = 0; i < RFM_MOVE_NPADS; i++)
            if (m->tk_pad >> i & 1) {
                m->tk_pad &= ~(1u << i);
                emit_pkt(m, s, 0x08, 0x80, (uint8_t)(MR_NOTE_PAD0 + i), 0);
            }
    if (!(keep & RFM_MOVE_TK_STEPS))
        for (i = 0; i < RFM_MOVE_NSTEPS; i++)
            if (m->tk_step >> i & 1) {
                m->tk_step = (uint16_t)(m->tk_step & ~(1u << i));
                emit_pkt(m, s, 0x08, 0x80, (uint8_t)(MR_NOTE_STEP0 + i), 0);
            }
    for (i = 0; i < 128; i++) {
        const unsigned c = i >= MR_CC_TRACK4 && i <= MR_CC_TRACK1 ? RFM_MOVE_TK_TRACKS
                         : i == MR_CC_JOG_CLICK ? RFM_MOVE_TK_JOG : RFM_MOVE_TK_BUTTONS;
        if (bit(m->tk_btn, i) && !(keep & c)) {
            bit_set(m->tk_btn, i, 0);
            emit_pkt(m, s, 0x0B, 0xB0, (uint8_t)i, 0);
        }
    }
    for (i = 0; i <= 9; i++) {
        const unsigned c = i < 8 ? RFM_MOVE_TK_KNOBS : i == 8 ? RFM_MOVE_TK_VOLUME : RFM_MOVE_TK_JOG;
        if ((m->tk_touch >> i & 1) && !(keep & c)) {
            m->tk_touch = (uint16_t)(m->tk_touch & ~(1u << i));
            emit_pkt(m, s, 0x08, 0x80, (uint8_t)i, 0);
        }
    }
    if (!(keep & RFM_MOVE_TK_BUTTONS))
        m->tk_back = 0;                                 /* (Back held back: nothing was sent) */
}

static unsigned tk_flags(const rfm_move_t *m)
{
    return m->speech && rfm_move_speech_mode(m) != RFM_MOVE_SPEECH_OFF ? RFM_MOVE_TKF_SPEECH : 0;
}
/* what the host was told last against what is so now: releases and TK_STATE when they differ (the end of every call
 * that can change it) */
void rfmv_tk_sync(rfm_move_t *m)
{
    const int on = rfmv_tk_on(m), s = tk_slot(m);
    const unsigned claims = on ? m->tk[s].claims : 0, flags = tk_flags(m);
    if (m->tk_sent_on && (!on || s != m->tk_sent_slot)) {   /* gone (or another slot's now): let go, then off */
        release_fw(m, 0);
        emit_state(m, m->tk_sent_slot, 0, 0, flags);
        m->tk_sent_on = 0;
    }
    if (!on)
        return;
    if (!m->tk_sent_on || claims != m->tk_sent_claims || flags != m->tk_sent_flags) {
        if (m->tk_sent_on)
            release_fw(m, claims);                      /* (claims dropped: their controls let go) */
        m->tk_sent_on = 1, m->tk_sent_slot = (uint8_t)s, m->tk_sent_claims = (uint16_t)claims;
        m->tk_sent_flags = (uint8_t)flags;
        emit_state(m, s, 1, claims, flags);
    }
}
void rfmv_tk_release(rfm_move_t *m)                     /* rfm_move_release_all: the firmware's held controls too */
{
    if (m->tk_sent_on)
        release_fw(m, 0);
    m->tk_vtouch = 0, m->tk_back = 0, m->tk_back_trk = 0, m->tk_back_eat = 0;
}

/* ---- input: 1 taken (the firmware's, or the way out) */
static int btn_claim(int cc)
{
    switch (cc) {
    case MR_CC_SHIFT: case MR_CC_MENU: case MR_CC_BACK: case MR_CC_CAPTURE: case MR_CC_DOWN: case MR_CC_UP:
    case MR_CC_UNDO: case MR_CC_LOOP: case MR_CC_COPY: case MR_CC_LEFT: case MR_CC_RIGHT: case MR_CC_MUTE:
    case MR_CC_DELETE: case MR_CC_PLAY: case MR_CC_REC: case MR_CC_SAMPLE:
        return RFM_MOVE_TK_BUTTONS;
    default:
        return cc >= MR_CC_TRACK4 && cc <= MR_CC_TRACK1 ? RFM_MOVE_TK_TRACKS : 0;
    }
}
static uint8_t rel7(int d) { return (uint8_t)(d >= 0 ? clampi(d, 0, 63) : 128 + clampi(d, -64, -1)); }
/* the event's packet: as the Move sent it, or made up (an event built by hand: raw[] empty) */
static void packet(const mr_event_t *ev, uint8_t p[4])
{
    const int on = ev->value != 0;
    if (ev->raw[1] & 0x80) {
        memcpy(p, ev->raw, 4);
        return;
    }
    memset(p, 0, 4);
    switch (ev->type) {
    case MR_EV_PAD:
        p[0] = on ? 0x09 : 0x08, p[1] = on ? 0x90 : 0x80, p[2] = (uint8_t)(MR_NOTE_PAD0 + ev->index);
        p[3] = (uint8_t)clampi(ev->value, 0, 127);
        break;
    case MR_EV_PAD_PRESSURE:
        p[0] = 0x0A, p[1] = 0xA0, p[2] = (uint8_t)(MR_NOTE_PAD0 + ev->index), p[3] = (uint8_t)clampi(ev->value, 0, 127);
        break;
    case MR_EV_PRESSURE: p[0] = 0x0D, p[1] = 0xD0, p[2] = (uint8_t)clampi(ev->value, 0, 127); break;
    case MR_EV_STEP:
    case MR_EV_TOUCH:
        p[0] = on ? 0x09 : 0x08, p[1] = on ? 0x90 : 0x80;
        p[2] = (uint8_t)(ev->type == MR_EV_STEP ? MR_NOTE_STEP0 + ev->index : ev->index), p[3] = on ? 127 : 0;
        break;
    case MR_EV_BUTTON:
    case MR_EV_JOG_CLICK:
        p[0] = 0x0B, p[1] = 0xB0, p[2] = (uint8_t)(ev->type == MR_EV_BUTTON ? ev->index : MR_CC_JOG_CLICK);
        p[3] = on ? 127 : 0;
        break;
    case MR_EV_KNOB:
    case MR_EV_JOG:
        p[0] = 0x0B, p[1] = 0xB0, p[2] = (uint8_t)(ev->type == MR_EV_KNOB ? MR_CC_KNOB1 + ev->index : MR_CC_JOG);
        p[3] = rel7(ev->value);
        break;
    default: break;
    }
}
static void send(rfm_move_t *m, const mr_event_t *ev)
{
    uint8_t p[4];
    packet(ev, p);
    emit_pkt(m, tk_slot(m), p[0], p[1], p[2], p[3]);
}
/* a control with a state: a press goes to the firmware when claimed (then held there), a release to whoever has it */
static int held(rfm_move_t *m, const mr_event_t *ev, int claimed, int was, int *now)
{
    if (ev->value) {
        if (!claimed)
            return 0;
        *now = 1;
    } else {
        if (!was)
            return 0;
        *now = 0;
    }
    send(m, ev);
    return 1;
}
static int route(rfm_move_t *m, const mr_event_t *ev)
{
    const unsigned cl = rfmv_tk_claims(m);
    const int i = ev->index;
    int now = 0, r;
    if (ev->type == MR_EV_TOUCH && i == 8)
        m->tk_vtouch = (uint8_t)(ev->value != 0);
    if (ev->type == MR_EV_BUTTON && i == MR_CC_BACK) {
        if (!ev->value && m->tk_back_eat) {             /* (volume + Back's release: nobody's) */
            m->tk_back_eat = 0;
            return 1;
        }
        if (ev->value && m->tk_vtouch && rfmv_tk_on(m)) {   /* volume + Back: out */
            m->tk_back_eat = 1;
            rfmv_tk_set(m, 0);
            return 1;
        }
    }
    if (!m->tk_sent_on || !rfmv_tk_on(m))               /* (not shown: the core's; the releases went as it closed) */
        return 0;
    if (ev->value && ev->type != MR_EV_BUTTON && (rfmv_lay_menu(m) || rfmv_t4_menu(m)))
        return 0;                                       /* (0.8.0: a held Track's menu takes the presses; the
                                                         * firmware's held controls still get their releases) */
    if (ev->value && ev->type == MR_EV_BUTTON && (rfmv_lay_menu(m) || rfmv_t4_menu(m)) &&
        (btn_claim(i) != RFM_MOVE_TK_TRACKS) && (i == MR_CC_UP || i == MR_CC_DOWN || i == MR_CC_REC ||
        i == MR_CC_PLAY || i == MR_CC_CAPTURE || i == MR_CC_UNDO || i == MR_CC_LOOP || i == MR_CC_SAMPLE))
        return 0;
    switch (ev->type) {
    case MR_EV_PAD:
        if (i < 0 || i >= RFM_MOVE_NPADS || !held(m, ev, (cl & RFM_MOVE_TK_PADS) != 0, m->tk_pad >> i & 1, &now))
            return 0;
        m->tk_pad = now ? m->tk_pad | 1u << i : m->tk_pad & ~(1u << i);
        return 1;
    case MR_EV_PAD_PRESSURE:
        if (i < 0 || i >= RFM_MOVE_NPADS || !(m->tk_pad >> i & 1))
            return 0;
        send(m, ev);
        return 1;
    case MR_EV_PRESSURE:
        if (!(cl & RFM_MOVE_TK_PADS))
            return 0;
        send(m, ev);
        return 1;
    case MR_EV_STEP:
        if (i < 0 || i >= RFM_MOVE_NSTEPS || !held(m, ev, (cl & RFM_MOVE_TK_STEPS) != 0, m->tk_step >> i & 1, &now))
            return 0;
        m->tk_step = (uint16_t)(now ? m->tk_step | 1u << i : m->tk_step & ~(1u << i));
        return 1;
    case MR_EV_TOUCH: {
        const unsigned c = i < 8 ? RFM_MOVE_TK_KNOBS : i == 8 ? RFM_MOVE_TK_VOLUME : RFM_MOVE_TK_JOG;
        if (i < 0 || i > 9 || !held(m, ev, (cl & c) != 0, m->tk_touch >> i & 1, &now))
            return 0;
        m->tk_touch = (uint16_t)(now ? m->tk_touch | 1u << i : m->tk_touch & ~(1u << i));
        return 1;
    }
    case MR_EV_KNOB:
        if (i < 0 || i > 8 || !(cl & (i < 8 ? RFM_MOVE_TK_KNOBS : RFM_MOVE_TK_VOLUME)))
            return 0;
        send(m, ev);
        return 1;
    case MR_EV_JOG:
        if (!(cl & RFM_MOVE_TK_JOG))
            return 0;
        send(m, ev);
        return 1;
    case MR_EV_JOG_CLICK:
        if (!held(m, ev, (cl & RFM_MOVE_TK_JOG) != 0, bit(m->tk_btn, MR_CC_JOG_CLICK), &now))
            return 0;
        bit_set(m->tk_btn, MR_CC_JOG_CLICK, now);
        return 1;
    case MR_EV_BUTTON:
        if (i < 0 || i > 127)
            return 0;
        if (i == MR_CC_BACK && (cl & RFM_MOVE_TK_BUTTONS) && !(cl & RFM_MOVE_TK_TRACKS) && !bit(m->tk_btn, i)) {
            if (ev->value) {                            /* (held back: a Track pressed meanwhile acts and Back is then
                                                         * nobody's) */
                m->tk_back = 1, m->tk_back_trk = 0;
                m->back_down = 1, m->back_used = 1, m->back_ms = 0;   /* (the core's Back + Track; no click) */
                return 1;
            }
            if (!m->tk_back)
                return 0;
            m->tk_back = 0, m->back_down = 0;
            if (!m->tk_back_trk) {                      /* (no Track meanwhile: the firmware's press and release) */
                emit_pkt(m, tk_slot(m), 0x0B, 0xB0, MR_CC_BACK, 127);
                emit_pkt(m, tk_slot(m), 0x0B, 0xB0, MR_CC_BACK, 0);
            }
            return 1;
        }
        if (ev->value && m->tk_back && btn_claim(i) == RFM_MOVE_TK_TRACKS)
            m->tk_back_trk = 1;                         /* (Back + Track: the slots; Back is not the firmware's) */
        r = held(m, ev, (cl & (unsigned)btn_claim(i)) != 0, bit(m->tk_btn, i), &now);
        if (r)
            bit_set(m->tk_btn, i, now);
        return r;
    default: return 0;                                  /* (jacks, MIDI, SysEx: the host's) */
    }
}

int rfmv_tk_route(rfm_move_t *m, const mr_event_t *ev)
{
    const int r = route(m, ev);
    if (r && ev->type == MR_EV_BUTTON && ev->index == MR_CC_BACK)
        m->say_back = 0;                                /* (Back was not the core's: "where you are" not said) */
    return r;
}

/* ---- the LEDs: each claimed one the firmware's */
static unsigned led_claim(int i)
{
    if (i < RFM_MOVE_L_STEP0)
        return RFM_MOVE_TK_PADS;
    if (i < RFM_MOVE_L_PLAY || i >= RFM_MOVE_L_ICON0)
        return RFM_MOVE_TK_STEPS;
    if (i < RFM_MOVE_L_TRACK0 || i >= RFM_MOVE_L_WHITE0)
        return RFM_MOVE_TK_BUTTONS;
    if (i < RFM_MOVE_L_RING0)
        return RFM_MOVE_TK_TRACKS;
    return RFM_MOVE_TK_KNOBS;
}
void rfmv_tk_leds(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3])
{
    const unsigned cl = rfmv_tk_claims(m);
    const rfm_move_tk_slot_t *t = &m->tk[tk_slot(m)];
    int i;
    if (!cl)
        return;
    for (i = 0; i < RFM_MOVE_NLEDS; i++) {
        if (!(cl & led_claim(i)))
            continue;
        if (i >= RFM_MOVE_L_WHITE0)
            w[i][0] = (uint8_t)((t->led[i][0] * 127 + 127) / 255), w[i][1] = 0, w[i][2] = 0;   /* tk1's 0..255 -> the Move's 0..127 */
        else
            memcpy(w[i], t->led[i], 3);
    }
}

/* ---- the host's calls */
static void copy_name(char *o, const char *s)
{
    int i, k = 0;
    for (i = 0; s && s[i] && k < RFM_MOVE_TK_NAME - 1; i++)
        if (s[i] >= 32 && s[i] < 127)
            o[k++] = s[i];
    o[k] = 0;
}
void rfm_move_set_takeover(rfm_move_t *m, int slot, unsigned claims, const char *name)
{
    rfm_move_tk_slot_t *t;
    int shown;
    if (slot < 0 || slot >= RFM_MOVE_FW_SLOTS)
        return;
    t = &m->tk[slot];
    shown = slot == tk_slot(m) && rfmv_tk_mode(m);
    copy_name(t->name, name);
    t->claims = (uint16_t)(claims & RFM_MOVE_TK_ALL);
    if (!t->claims && m->s.slot[slot].lay == RFM_MOVE_LAY_TK) {   /* (no takeover now: the open one closes, the
                                                         * FM-1 layout) */
        t->mode = 0;
        m->s.slot[slot].lay = RFM_MOVE_LAY_FM1;
        rfmv_changed(m);
        if (shown)
            rfmv_pop(m, POP_LAYOUT, slot);
    }
    rfmv_tk_sync(m);
    rfmv_say_check(m);
}
void rfm_move_tk_screen(rfm_move_t *m, int slot, const uint8_t packed[MR_SCREEN_BYTES])
{
    if (slot >= 0 && slot < RFM_MOVE_FW_SLOTS && packed)
        memcpy(m->tk[slot].screen, packed, MR_SCREEN_BYTES);
}
void rfm_move_tk_leds(rfm_move_t *m, int slot, const uint8_t rgb[RFM_MOVE_NLEDS][3])
{
    if (slot >= 0 && slot < RFM_MOVE_FW_SLOTS && rgb)
        memcpy(m->tk[slot].led, rgb, sizeof m->tk[slot].led);
}
void rfm_move_tk_say(rfm_move_t *m, int slot, const char *words, int interrupt)
{
    char w[RFM_MOVE_SAY_MAX];
    rfm_move_act_t a;
    int i, k = 0;
    if (!words || slot != rfm_move_tk_shown(m) || !m->speech || !rfm_move_speech_open(m) || !m->fn)
        return;
    for (i = 0; words[i] && k < RFM_MOVE_SAY_MAX - 1; i++)
        if (words[i] >= 32 && words[i] < 127)
            w[k++] = words[i];
    while (k && w[k - 1] == ' ')
        k--;
    w[k] = 0;
    for (i = 0; w[i] == ' '; i++) {}
    if (!w[i])
        return;
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_SAY, a.index = interrupt ? RFM_MOVE_SAY_VALUE : RFM_MOVE_SAY_EVENT;
    a.down = (uint8_t)(interrupt != 0), a.slot = (uint8_t)slot;
    a.data = (const uint8_t *)(w + i), a.len = (uint32_t)strlen(w + i);
    m->say_n++;
    m->fn(m->ctx, &a);
}
