/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* (0.14.0, the owner's 2026-11-24: "add a page after the FM-1 knobs for Engine parameters ... you reach that page,
 * then click the jogwheel, now you can scroll and select from different pages for the synth engine ... Each page then
 * supports up to 8 parameters on the knobs") the ENGINE knob page: a firmware's ep1 (engine-params SPEC.md), stages 1
 * and 2 of its BUILD-PLAN.md. The host gives each slot's description (an EP1D blob, checked and copied here), its
 * layers' engines and channels, the values and the watched page's cells; the core draws every name and value itself,
 * sends a knob's value absolutely (RFM_MOVE_ACT_EP, SET, merged to one per block) and says which page it watches
 * (RFM_MOVE_ACT_EP_WATCH). No locks yet (stage 3), no Layer setting (stage 4): every track is "Auto". */
#include <stdio.h>
#include <string.h>

#include "move_int.h"
#include "v2.h"

#define clampi rfmv_clampi
typedef rfmv_rgb_t rgb_t;

/* ================================================================ the description (EP1D) === */
enum { EPF_BIPOLAR = 1, EPF_LIST = 2, EPF_DYN = 4 };    /* (the flags the page uses; NOLOCK .. RETIRED: stage 3) */
enum { FMT_INT, FMT_FIXED, FMT_TABLE, FMT_NOTE, FMT_PERCENT, FMT_ONOFF };
#define NOPAGE 0xFFFF

static uint32_t r16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t r32(const uint8_t *p) { return r16(p) | r16(p + 2) << 16; }
static int s16(const uint8_t *p) { return (int)(int16_t)(uint16_t)r16(p); }
static uint32_t fnv1a(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
/* the sections' places (from a header already checked) */
typedef struct { uint32_t lay, eng, pag, par, tab, str, nstr, np, npg, nt, ne, nl; } lay_t;
static void layout(const uint8_t *d, uint32_t n, lay_t *L)
{
    L->np = r16(d + 8), L->ne = d[10], L->nl = d[11], L->npg = r16(d + 12), L->nt = r16(d + 14);
    L->lay = r16(d + 6);
    L->eng = L->lay + 16 * L->nl;
    L->pag = L->eng + 16 * L->ne;
    L->par = L->pag + 32 * L->npg;
    L->tab = L->par + 32 * L->np;
    L->str = L->tab + 8 * L->nt;
    L->nstr = n - L->str;
}
/* a name field: printable ASCII, NUL-terminated inside its w bytes */
static int name_ok(const uint8_t *p, int w)
{
    int i;
    for (i = 0; i < w; i++) {
        if (!p[i])
            return 1;
        if (p[i] < 0x20 || p[i] > 0x7E)
            return 0;
    }
    return 0;
}
int rfm_move_ep_check(const uint8_t *d, uint32_t n)
{
    static uint8_t seen[8192];                          /* (the pids met: 8 KiB, not on a small stack) */
    lay_t L;
    uint64_t end;
    uint32_t i, j;
    if (!d || n < 32 || n > RFM_MOVE_EP_DESC_MAX)
        return -1;
    if (memcmp(d, "EP1D", 4) != 0)
        return -2;
    if (r16(d + 4) != 1)
        return -3;
    if (r16(d + 6) < 32 || r16(d + 6) > n)              /* (a later version may grow the header: skipped to) */
        return -4;
    {
        const uint32_t np = r16(d + 8), ne = d[10], nl = d[11], npg = r16(d + 12), nt = r16(d + 14);
        if (np < 1 || np > RFM_MOVE_EP_PARAMS || ne < 1 || ne > 64 || nl < 1 || nl > RFM_MOVE_EP_LAYERS || npg < 1 ||
            npg > 512 || nt > 512)
            return -5;
        end = (uint64_t)r16(d + 6) + 16u * nl + 16u * ne + 32u * npg + 32u * np + 8u * nt;
    }
    if (r32(d + 16) != n)
        return -6;
    if (end > n)
        return -7;
    layout(d, n, &L);
    if (fnv1a(d + L.lay, n - L.lay) != r32(d + 20))
        return -8;
    for (i = 0; i < L.nl; i++)                          /* layers: a name */
        if (!name_ok(d + L.lay + 16 * i, 11))
            return -9;
    for (i = 0; i < L.ne; i++) {                        /* engines: a name; their pages consecutive, inside */
        const uint8_t *e = d + L.eng + 16 * i;
        if (!name_ok(e + 2, 11) || r16(e + 14) + e[13] > L.npg)
            return -10;
    }
    for (i = 0; i < L.npg; i++) {                       /* pages: a name; each knob a parameter or none */
        const uint8_t *p = d + L.pag + 32 * i;
        if (!name_ok(p + 1, 11))
            return -11;
        for (j = 0; j < 8; j++)
            if (r16(p + 12 + 2 * j) != 0xFFFF && r16(p + 12 + 2 * j) >= L.np)
                return -12;
    }
    memset(seen, 0, sizeof seen);
    for (i = 0; i < L.np; i++) {                        /* parameters */
        const uint8_t *p = d + L.par + 32 * i;
        const uint32_t pid = r16(p);
        const int mn = s16(p + 14), mx = s16(p + 16), df = s16(p + 18), fmt = p[20], arg = p[21];
        if (pid < 1 || pid > 0xFFFE || (seen[pid >> 3] >> (pid & 7) & 1))
            return -13;                                 /* (a pid unique in the firmware) */
        seen[pid >> 3] = (uint8_t)(seen[pid >> 3] | 1u << (pid & 7));
        if (!name_ok(p + 2, 11) || !name_ok(p + 24, 5))
            return -14;
        if (mx <= mn || df < mn || df > mx || fmt > FMT_ONOFF)
            return -15;
        if ((fmt == FMT_FIXED && (arg < 1 || arg > 4)) || (fmt == FMT_NOTE && (arg < 3 || arg > 4)) ||
            (fmt == FMT_TABLE && r16(p + 22) >= L.nt))
            return -16;
    }
    for (i = 0; i < L.nt; i++) {                        /* tables: their strings inside the strings section */
        const uint8_t *t = d + L.tab + 8 * i, *s = d + L.str;
        uint32_t at = r32(t), c = r16(t + 4), len;
        if (at > L.nstr || (at == L.nstr && c))
            return -17;
        for (j = 0; j < c; j++) {
            for (len = 0; at < L.nstr && s[at]; at++, len++)
                if (s[at] < 0x20 || s[at] > 0x7E || len >= 10)
                    return -18;
            if (at >= L.nstr)
                return -18;                             /* (no NUL before the end) */
            at++;
        }
    }
    return 0;
}

/* ---- a checked description's parts */
static const rfm_move_ep_slot_t *S(const rfm_move_t *m, int slot) { return &m->ep.s[slot % RFM_MOVE_FW_SLOTS]; }
static void lay_of(const rfm_move_ep_slot_t *s, lay_t *L) { layout(s->desc, s->n, L); }
static const uint8_t *eng_at(const rfm_move_ep_slot_t *s, const lay_t *L, int e) { return s->desc + L->eng + 16 * e; }
static const uint8_t *page_at(const rfm_move_ep_slot_t *s, const lay_t *L, int p) { return s->desc + L->pag + 32 * p; }
static const uint8_t *par_at(const rfm_move_ep_slot_t *s, const lay_t *L, int i) { return s->desc + L->par + 32 * i; }
static const char *lay_name(const rfm_move_ep_slot_t *s, const lay_t *L, int l) { return (const char *)s->desc + L->lay + 16 * l; }

/* ================================================================ which page, which slot, which layer === */
/* (SPEC.md 9) a track's firmware: the slot played if the track reaches it (its routing) and it has ep1; else the
 * lowest slot the track reaches that has; -1 none (no Engine page). Session view's volume knob picks the same way */
int rfmv_ep_target(const rfm_move_t *m, int track)
{
    const int a = m->fw.active % RFM_MOVE_FW_SLOTS;
    int k;
    track &= 15;
    if ((m->seq.fx.ffilt[a] >> track & 1) && rfm_move_ep_has(m, a))
        return a;
    for (k = 0; k < RFM_MOVE_FW_SLOTS; k++)
        if ((m->seq.fx.ffilt[k] >> track & 1) && rfm_move_ep_has(m, k))
            return k;
    return -1;
}
/* the layer a track's Engine page edits on that slot: its Layer setting (m->ep.lset: 0 Auto, the only value before
 * stage 4's Workflow row; 1..8 a layer) Auto: the layer whose MIDI channel is the track's (the core's 0..15, as the
 * record's: track 2 plays channel 1 by default, so ChoralRoot's bass on "MIDI Bass 2"), several: the lowest, none:
 * layer 0 (QUESTIONS.md card 2) */
int rfmv_ep_layer_of(const rfm_move_t *m, int track, int slot)
{
    const rfm_move_ep_slot_t *s = S(m, slot);
    const int set = m->ep.lset[track & 15], nl = s->n ? s->desc[11] : 0;
    int l;
    if (set >= 1 && set <= nl)
        return set - 1;
    for (l = 0; l < nl; l++)
        if (s->ch[l] == m->seq.tr[track & 15].ch)
            return l;
    return 0;
}
typedef struct { int slot, layer, eng, page; } cur_t;   /* eng 0xFF: the layer is off; page NOPAGE: none */
static int find_engine(const rfm_move_ep_slot_t *s, const lay_t *L, uint32_t eid)
{
    uint32_t e;
    for (e = 0; e < L->ne; e++)
        if (r16(eng_at(s, L, (int)e)) == eid)
            return (int)e;
    return -1;
}
static int find_page(const rfm_move_ep_slot_t *s, const lay_t *L, int e, int page_id)   /* in engine e, -1 */
{
    const uint8_t *E = eng_at(s, L, e);
    int i;
    for (i = 0; i < E[13]; i++)
        if (page_at(s, L, (int)r16(E + 14) + i)[0] == page_id)
            return (int)r16(E + 14) + i;
    return -1;
}
static int find_named(const rfm_move_ep_slot_t *s, const lay_t *L, int e, const char *name)
{
    const uint8_t *E = eng_at(s, L, e);
    int i;
    for (i = 0; i < E[13]; i++)
        if (!strncmp((const char *)page_at(s, L, (int)r16(E + 14) + i) + 1, name, 11))
            return (int)r16(E + 14) + i;
    return -1;
}
/* the page track t shows on an engine: (DESIGN.md 8) the one it showed on that engine; the engine changed under it:
 * the new engine's page with the same name, else the one last shown on that engine (RAM), else its first */
static int pick_page(const rfm_move_t *m, int t, int slot, const lay_t *L, int e)
{
    const rfm_move_ep_slot_t *s = S(m, slot);
    const uint8_t *E = eng_at(s, L, e);
    const uint32_t eid = r16(E);
    int p, k;
    if (!E[13])
        return NOPAGE;
    if (m->ep.tmem[t].slot == slot + 1) {
        const int me = find_engine(s, L, m->ep.tmem[t].eid);
        if (me == e && (p = find_page(s, L, e, m->ep.tmem[t].page_id)) >= 0)
            return p;
        if (me >= 0 && (p = find_page(s, L, me, m->ep.tmem[t].page_id)) >= 0 &&
            (p = find_named(s, L, e, (const char *)page_at(s, L, p) + 1)) >= 0)
            return p;
    }
    for (k = 0; k < 64; k++)
        if (s->mem[k].used && s->mem[k].eid == eid && (p = find_page(s, L, e, s->mem[k].page_id)) >= 0)
            return p;
    return (int)r16(E + 14);
}
static void resolve_t(const rfm_move_t *m, int t, cur_t *c)
{
    lay_t L;
    c->slot = rfmv_ep_target(m, t), c->layer = 0, c->eng = 0xFF, c->page = NOPAGE;
    if (c->slot < 0)
        return;
    lay_of(S(m, c->slot), &L);
    c->layer = rfmv_ep_layer_of(m, t, c->slot);
    c->eng = S(m, c->slot)->eng[c->layer];
    if (c->eng != 0xFF)
        c->page = pick_page(m, t, c->slot, &L, c->eng);
}
static void resolve(const rfm_move_t *m, cur_t *c) { resolve_t(m, m->seq.sel_track & 15, c); }
static void remember(rfm_move_t *m, int t, const cur_t *c)   /* the page shown: the track's and the engine's */
{
    rfm_move_ep_slot_t *s = &m->ep.s[c->slot];
    lay_t L;
    uint32_t eid;
    int k, free = -1;
    if (c->eng == 0xFF || c->page == NOPAGE)
        return;
    lay_of(s, &L);
    eid = r16(eng_at(s, &L, c->eng));
    m->ep.tmem[t].slot = (uint8_t)(c->slot + 1), m->ep.tmem[t].eid = (uint16_t)eid;
    m->ep.tmem[t].page_id = page_at(s, &L, c->page)[0];
    for (k = 0; k < 64; k++) {
        if (s->mem[k].used && s->mem[k].eid == eid)
            break;
        if (!s->mem[k].used && free < 0)
            free = k;
    }
    if (k == 64)
        k = free >= 0 ? free : (int)(eid % 64);
    s->mem[k].used = 1, s->mem[k].eid = (uint16_t)eid, s->mem[k].page_id = page_at(s, &L, c->page)[0];
}

int rfmv_ep_avail(const rfm_move_t *m) { return rfmv_ep_target(m, m->seq.sel_track) >= 0; }
int rfmv_ep_page(const rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    return m->s.mode == RFM_MOVE_MODE_SEQ && q->eng && !q->trig && !q->view && !q->projects;
}
/* the list on the screen: open, the page shown, nothing held over it (a held step, Copy, Delete: their screens; Loop
 * and Mute come before it in rfmv_seq_screen) */
int rfmv_ep_list_shown(const rfm_move_t *m)
{
    const rfm_move_seq_t *q = &m->seq;
    return q->ep_list && rfmv_ep_page(m) && !q->step_held && !(q->mods & (SEQ_M_COPY | SEQ_M_DELETE));
}

/* ================================================================ values and their words === */
static int ovr_find(const rfm_move_t *m, int slot, int layer, int idx)
{
    int i;
    for (i = 0; i < 16; i++)
        if (m->ep.ovr[i].on && m->ep.ovr[i].slot == slot && m->ep.ovr[i].layer == layer && m->ep.ovr[i].idx == idx &&
            rfmv_now_ms(m) - m->ep.ovr[i].ms < RFMV_EP_OVR_MS)
            return i;
    return -1;
}
/* the value shown: the one sent last while the firmware's values have not caught up (no flicker back), else the
 * record's */
int rfmv_ep_value(const rfm_move_t *m, int slot, int layer, int idx)
{
    const int o = ovr_find(m, slot, layer, idx);
    if (slot < 0 || layer < 0 || layer >= RFM_MOVE_EP_LAYERS || idx < 0 || idx >= RFM_MOVE_EP_PARAMS)
        return RFM_MOVE_EP_NONE;
    return o >= 0 ? m->ep.ovr[o].v : S(m, slot)->val[layer][idx];
}
int rfmv_ep_knob_param(const rfm_move_t *m, int k)      /* the selected track's page now: knob k's parameter, -1 */
{
    cur_t c;
    lay_t L;
    uint32_t i;
    resolve(m, &c);
    if (c.page == NOPAGE || k < 0 || k >= 8)
        return -1;
    lay_of(S(m, c.slot), &L);
    i = r16(page_at(S(m, c.slot), &L, c.page) + 12 + 2 * k);
    return i == 0xFFFF ? -1 : (int)i;
}
/* a value's words (SPEC.md 3 "Showing a value"): INT, FIXED(d), TABLE, NOTE(o), PERCENT, ONOFF; BIPOLAR signs; the
 * unit: no space before "%", one before any other */
static void unit_after(char *o, int cap, const uint8_t *P)
{
    const char *u = (const char *)P + 24;
    const size_t n = strlen(o);
    if (u[0] && (int)n < cap - 1)
        snprintf(o + n, (size_t)cap - n, "%s%.4s", u[0] == '%' ? "" : " ", u);
}
static void as_int(char *o, int cap, const uint8_t *P, int v)
{
    snprintf(o, (size_t)cap, (P[13] & EPF_BIPOLAR) && v > 0 ? "+%d" : "%d", v);
    unit_after(o, cap, P);
}
static void text_of(const rfm_move_ep_slot_t *s, const lay_t *L, const uint8_t *P, int v, char *o, int cap)
{
    const int mn = s16(P + 14), mx = s16(P + 16);
    o[0] = 0;
    switch (P[20]) {
    case FMT_FIXED: {
        static const int POW[5] = { 1, 10, 100, 1000, 10000 };
        const int dd = clampi(P[21], 1, 4), a = v < 0 ? -v : v;
        snprintf(o, (size_t)cap, "%s%d.%0*d", v < 0 ? "-" : (P[13] & EPF_BIPOLAR) && v > 0 ? "+" : "", a / POW[dd], dd,
                 a % POW[dd]);
        unit_after(o, cap, P);
        return;
    }
    case FMT_TABLE: {
        const uint8_t *T = s->desc + L->tab + 8 * r16(P + 22);
        const int at = v - s16(T + 6);
        if (at >= 0 && at < (int)r16(T + 4)) {
            const char *p = (const char *)s->desc + L->str + r32(T);
            int i;
            for (i = 0; i < at; i++)
                p += strlen(p) + 1;
            snprintf(o, (size_t)cap, "%.10s", p);
            return;
        }
        as_int(o, cap, P, v);                           /* (outside the table: as INT) */
        return;
    }
    case FMT_NOTE: {
        const int oct = (v >= 0 ? v / 12 : -((11 - v) / 12)) - (5 - P[21]);
        snprintf(o, (size_t)cap, "%s%d", rfmv_note_names[((v % 12) + 12) % 12], oct);
        return;
    }
    case FMT_PERCENT:
        if (P[13] & EPF_BIPOLAR) {
            const int M = -mn > mx ? -mn : mx, a = v < 0 ? -v : v, r = (a * 200 + M) / (2 * M);
            snprintf(o, (size_t)cap, v > 0 ? "+%d%%" : v < 0 ? "-%d%%" : "%d%%", r);
        } else
            snprintf(o, (size_t)cap, "%d%%", ((v - mn) * 200 + (mx - mn)) / (2 * (mx - mn)));
        return;
    case FMT_ONOFF: snprintf(o, (size_t)cap, "%s", v == mn ? "Off" : "On"); return;
    default: as_int(o, cap, P, v); return;
    }
}
/* the watched cell of a DYN parameter: its words when the cell is that pid (and the value shown is the firmware's) */
static const rfm_move_ep_cell_t *dyn_cell(const rfm_move_t *m, const cur_t *c, uint32_t pid)
{
    const rfm_move_ep_slot_t *s = S(m, c->slot);
    int k;
    if (s->cell_page != c->page || s->cell_layer != c->layer)
        return NULL;
    for (k = 0; k < 8; k++)
        if (s->cell[k].pid == pid)
            return &s->cell[k];
    return NULL;
}
/* knob k of the selected track's page: its name and its value's words; 0: no parameter there (or no value now) */
int rfmv_ep_knob_words(const rfm_move_t *m, int k, char *name, int ncap, char *val, int vcap, double *frac, int *bip)
{
    cur_t c;
    lay_t L;
    const rfm_move_ep_slot_t *s;
    const uint8_t *P;
    int idx, v, mn, mx;
    name[0] = val[0] = 0;
    resolve(m, &c);
    if ((idx = rfmv_ep_knob_param(m, k)) < 0)
        return 0;
    s = S(m, c.slot);
    lay_of(s, &L);
    P = par_at(s, &L, idx);
    if ((v = rfmv_ep_value(m, c.slot, c.layer, idx)) == RFM_MOVE_EP_NONE)
        return 0;
    snprintf(name, (size_t)ncap, "%.10s", (const char *)P + 2);
    text_of(s, &L, P, v, val, vcap);
    if (P[13] & EPF_DYN) {                              /* (the VA's WAVE in NOISE mode is COLOR: the FM-1's words) */
        const rfm_move_ep_cell_t *cl = dyn_cell(m, &c, r16(P));
        if (cl && cl->name[0])
            snprintf(name, (size_t)ncap, "%.10s", cl->name);
        if (cl && cl->text[0] && ovr_find(m, c.slot, c.layer, idx) < 0)
            snprintf(val, (size_t)vcap, "%.10s", cl->text);
    }
    mn = s16(P + 14), mx = s16(P + 16);
    if (bip)
        *bip = (P[13] & EPF_BIPOLAR) != 0;
    if (frac) {
        const int M = -mn > mx ? -mn : mx;
        *frac = (P[13] & EPF_BIPOLAR) ? (double)v / M : (double)(v - mn) / (mx - mn);
    }
    return 1;
}
/* (tests) a value's words as the page shows them */
void rfmv_ep_text(const rfm_move_t *m, int slot, int idx, int v, char *o, int cap)
{
    const rfm_move_ep_slot_t *s = S(m, slot);
    lay_t L;
    o[0] = 0;
    if (!s->n || idx < 0)
        return;
    lay_of(s, &L);
    if ((uint32_t)idx < L.np)
        text_of(s, &L, par_at(s, &L, idx), v, o, cap);
}

/* ================================================================ actions === */
static void ep_emit(rfm_move_t *m, int kind, int slot, int layer, int op, int pid, int value)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = (uint8_t)kind, a.slot = (uint8_t)slot, a.layer = (uint8_t)layer, a.op = (uint8_t)op;
    a.pid = (uint16_t)pid, a.value = (int16_t)value, a.frame = 0;
    if (m->fn)
        m->fn(m->ctx, &a);
}
/* (SPEC.md 6) a knob's SETs, merged: one per (slot, layer, pid) per rfm_move_seq_advance call, the latest value, at
 * frame 0 (rfmv_ep_flush, first thing in the call) */
static void set_queue(rfm_move_t *m, int slot, int layer, int pid, int v)
{
    rfm_move_ep_t *e = &m->ep;
    int i;
    for (i = 0; i < e->nset; i++)
        if (e->set[i].slot == slot && e->set[i].layer == layer && e->set[i].pid == pid) {
            e->set[i].v = (int16_t)v;
            return;
        }
    if (e->nset < (int)(sizeof e->set / sizeof e->set[0])) {
        e->set[e->nset].slot = (uint8_t)slot, e->set[e->nset].layer = (uint8_t)layer;
        e->set[e->nset].pid = (uint16_t)pid, e->set[e->nset].v = (int16_t)v;
        e->nset++;
    } else
        ep_emit(m, RFM_MOVE_ACT_EP, slot, layer, RFM_MOVE_EP_SET, pid, v);   /* (32 knobs' worth waiting: at once) */
}
void rfmv_ep_flush(rfm_move_t *m)
{
    rfm_move_ep_t *e = &m->ep;
    int i;
    for (i = 0; i < e->nset; i++)
        ep_emit(m, RFM_MOVE_ACT_EP, e->set[i].slot, e->set[i].layer, RFM_MOVE_EP_SET, e->set[i].pid, e->set[i].v);
    e->nset = 0;
}
static void ovr_set(rfm_move_t *m, int slot, int layer, int idx, int v)
{
    rfm_move_ep_t *e = &m->ep;
    int i, o = -1;
    uint32_t oldest = 0;
    for (i = 0; i < 16; i++)
        if (e->ovr[i].on && e->ovr[i].slot == slot && e->ovr[i].layer == layer && e->ovr[i].idx == idx) {
            o = i;
            break;
        }
    for (i = 0; o < 0 && i < 16; i++)                   /* (a free one, else the oldest) */
        if (!e->ovr[i].on || rfmv_now_ms(m) - e->ovr[i].ms >= RFMV_EP_OVR_MS)
            o = i;
    for (i = 0; o < 0 && i < 16; i++)
        if (rfmv_now_ms(m) - e->ovr[i].ms >= oldest)
            oldest = rfmv_now_ms(m) - e->ovr[i].ms, o = i;
    if (o < 0)
        o = 0;
    e->ovr[o].on = 1, e->ovr[o].slot = (uint8_t)slot, e->ovr[o].layer = (uint8_t)layer, e->ovr[o].idx = (uint16_t)idx;
    e->ovr[o].v = (int16_t)v, e->ovr[o].ms = rfmv_now_ms(m);
}

/* ================================================================ the controls === */
static int shift_held(const rfm_move_t *m) { return (m->seq.mods & SEQ_M_SHIFT) || m->btn[MR_CC_SHIFT].kind != H_NONE; }
/* a knob turned on the page (the list open or not: its highlighted page): the parameter's step a tick (0: the range
 * over ~128 ticks), Shift one stored step (fine), a LIST one item every RFM_MOVE_LIST_TICKS; absolute, to the firmware
 * (SET, merged per block). A knob with nothing on it, a layer off: dead */
void rfmv_ep_knob(rfm_move_t *m, int k, int d)
{
    cur_t c;
    lay_t L;
    const uint8_t *P;
    int idx, v, nv, mn, mx;
    resolve(m, &c);
    if ((idx = rfmv_ep_knob_param(m, k)) < 0)
        return;
    if ((v = rfmv_ep_value(m, c.slot, c.layer, idx)) == RFM_MOVE_EP_NONE)
        return;
    lay_of(S(m, c.slot), &L);
    P = par_at(S(m, c.slot), &L, idx);
    mn = s16(P + 14), mx = s16(P + 16);
    if (!(rfmv_pop_up(m, POP_EPK) && m->pop_i == k))
        rfmv_pop(m, POP_EPK, k);
    else
        m->pop_ms = rfmv_now_ms(m);
    if (P[13] & EPF_LIST) {
        if (!(d = rfmv_list_ticks(m, k, d)))
            return;
        nv = v + d;
    } else if (shift_held(m)) {
        m->seq.mods_used |= SEQ_M_SHIFT;                /* (Shift's release: nothing of its own) */
        nv = v + d;
    } else {
        const int st = P[29] ? P[29] : (mx - mn + 1) / 128 > 1 ? (mx - mn + 1) / 128 : 1;
        nv = v + d * st;
    }
    nv = clampi(nv, mn, mx);
    if (nv == v)
        return;
    ovr_set(m, c.slot, c.layer, idx, nv);
    set_queue(m, c.slot, c.layer, (int)r16(P), nv);
}
int rfmv_ep_touch(rfm_move_t *m, int k)                 /* a knob touched: its name and value (R5); 1 taken */
{
    char n[16], v[16];
    if (rfmv_ep_knob_words(m, k, n, sizeof n, v, sizeof v, NULL, NULL))
        rfmv_pop(m, POP_EPK, k);
    return 1;
}
/* the jog's click on the page (it has no double click: R2 lets it act, at its release): the page list opens, or
 * closes on the highlighted page. Shift + the click: nothing; Delete + the click: nothing in these stages (stage 3:
 * every engine lock of the clip), the list does not open; a held step, Copy, Mute or Loop: theirs */
void rfmv_ep_click(rfm_move_t *m, int down)
{
    rfm_move_seq_t *q = &m->seq;
    cur_t c;
    if (down) {
        m->ep.click = 0;
        if (shift_held(m)) {
            q->mods_used |= SEQ_M_SHIFT;
            return;
        }
        if (q->mods & SEQ_M_DELETE) {
            q->mods_used |= SEQ_M_DELETE;
            return;
        }
        if (q->step_held || (q->mods & (SEQ_M_COPY | SEQ_M_MUTE | SEQ_M_LOOP)))
            return;
        m->ep.click = 1;
        return;
    }
    if (!m->ep.click)
        return;
    m->ep.click = 0;
    if (q->ep_list) {
        rfmv_seq_ep_list(m, 0);
        return;
    }
    resolve(m, &c);
    if (c.slot < 0)
        return;
    if (c.page == NOPAGE) {                             /* (a layer off: its "Off" again, R1) */
        rfmv_pop(m, POP_EPA, 0);
        return;
    }
    rfmv_seq_ep_list(m, 1);
    {
        char w[40], t[16];
        lay_t L;
        lay_of(S(m, c.slot), &L);
        rfmv_tcase((const char *)page_at(S(m, c.slot), &L, c.page) + 1, t, sizeof t);
        snprintf(w, sizeof w, "Pages, %s", t);
        rfmv_say_text(m, w);
    }
}
/* the list's turn: the highlight a page a detent, stopping at the ends; the knobs follow it at once (it is the page
 * shown) */
void rfmv_ep_list_turn(rfm_move_t *m, int d)
{
    cur_t c;
    lay_t L;
    const rfm_move_ep_slot_t *s;
    const uint8_t *E;
    int at, n;
    resolve(m, &c);
    if (c.page == NOPAGE)
        return;
    s = S(m, c.slot);
    lay_of(s, &L);
    E = eng_at(s, &L, c.eng);
    at = clampi(c.page - (int)r16(E + 14) + d, 0, E[13] - 1);
    n = (int)r16(E + 14) + at;
    if (n == c.page)
        return;
    c.page = n;
    remember(m, m->seq.sel_track & 15, &c);
    {
        char t[16];
        rfmv_tcase((const char *)page_at(s, &L, n) + 1, t, sizeof t);
        rfmv_say_text(m, t);
    }
}

/* ================================================================ the sync: the page shown, its popups, the watch === */
/* After every input and every rfm_move_seq_advance (leave 1), and after the host's ep1 calls (leave 0: a slot emptied
 * then given a firmware again in one go keeps the page): a track left on ENGINE whose firmware no longer offers it
 * comes back on FM-1 (DESIGN.md 2); the list closes off the page; arriving on the page (the jog, a track selected
 * that was left on it, another slot or layer) shows "Chord  VA" over the page's name a second; the layer's engine
 * changed under it: "Chord" over the new engine; the page watched (EP_WATCH) follows */
void rfmv_ep_sync(rfm_move_t *m, int leave)
{
    rfm_move_seq_t *q = &m->seq;
    rfm_move_ep_t *e = &m->ep;
    const int t = q->sel_track & 15;
    cur_t c;
    int shown, tv, ws, wp;
    if (leave) {
        if (q->eng && !rfmv_ep_avail(m)) {
            rfmv_seq_kpage_set(m, 1);
            q->kpage[t] = 1;
        }
        if (q->ep_list && !rfmv_ep_page(m))
            rfmv_seq_ep_list(m, 0);
    }
    resolve(m, &c);
    shown = rfmv_ep_page(m) && c.slot >= 0;
    tv = m->s.mode == RFM_MOVE_MODE_SEQ && !q->view && !q->projects;
    if (shown) {
        const int arrive = (!e->p_shown && e->p_tv) || e->p_track != t || e->p_slot != c.slot || e->p_layer != c.layer;
        if (arrive && !(m->pop == POP_EPA && m->pop_i == 0 && rfmv_pop_up(m, POP_EPA)))
            rfmv_pop(m, POP_EPA, 0);
        else if (!arrive && e->p_shown && e->p_eng != c.eng)
            rfmv_pop(m, POP_EPA, 1);
        remember(m, t, &c);
    }
    e->slot = (int8_t)c.slot, e->layer = (uint8_t)c.layer, e->eng = (uint8_t)c.eng, e->page = (uint16_t)c.page;
    e->p_shown = (uint8_t)shown, e->p_tv = (uint8_t)tv, e->p_track = (int8_t)(shown ? t : -1);
    e->p_slot = (int8_t)c.slot, e->p_layer = (uint8_t)c.layer, e->p_eng = (uint8_t)c.eng;
    ws = shown && c.page != NOPAGE ? c.slot : -1;
    wp = ws >= 0 ? c.page : -1;
    if (ws != e->w_slot || (ws >= 0 && (c.layer != e->w_layer || wp != e->w_page || t != e->w_track))) {
        if (e->w_slot >= 0 && e->w_slot != ws)
            ep_emit(m, RFM_MOVE_ACT_EP_WATCH, e->w_slot, e->w_layer, 0, 0, -1);
        if (ws >= 0)
            ep_emit(m, RFM_MOVE_ACT_EP_WATCH, ws, c.layer, 0, 0, wp);
        e->w_slot = (int8_t)ws, e->w_layer = (uint8_t)c.layer, e->w_page = (int16_t)wp, e->w_track = (uint8_t)t;
    }
}
void rfmv_ep_init(rfm_move_t *m)
{
    int k;
    m->ep.slot = m->ep.p_slot = m->ep.p_track = m->ep.w_slot = -1;
    m->ep.w_page = -1;
    for (k = 0; k < RFM_MOVE_FW_SLOTS; k++)
        m->ep.s[k].cell_page = -1;
}

/* ================================================================ the host's calls === */
static void slot_values_none(rfm_move_ep_slot_t *s)
{
    int l, i;
    for (l = 0; l < RFM_MOVE_EP_LAYERS; l++)
        for (i = 0; i < RFM_MOVE_EP_PARAMS; i++)
            s->val[l][i] = RFM_MOVE_EP_NONE;
}
static void slot_drop(rfm_move_t *m, int slot)          /* what waits for the slot's old firmware: gone */
{
    rfm_move_ep_t *e = &m->ep;
    int i, k = 0;
    for (i = 0; i < e->nset; i++)
        if (e->set[i].slot != slot)
            e->set[k++] = e->set[i];
    e->nset = (uint8_t)k;
    for (i = 0; i < 16; i++)
        if (e->ovr[i].slot == slot)
            e->ovr[i].on = 0;
}
int rfm_move_ep_describe(rfm_move_t *m, int slot, const uint8_t *desc, uint32_t n)
{
    rfm_move_ep_slot_t *s;
    int r = 0, t;
    if (slot < 0 || slot >= RFM_MOVE_FW_SLOTS)
        return -1;
    s = &m->ep.s[slot];
    if (desc && n && (r = rfm_move_ep_check(desc, n)) == 0 && s->n == n && s->fnv == r32(desc + 20) &&
        !memcmp(s->desc, desc, n))
        return 0;                                       /* (the same one again: nothing to do) */
    slot_drop(m, slot);
    if (!desc || !n || r < 0) {                         /* none (or refused: none, the Engine page gone) */
        s->n = 0, s->fnv = 0, s->nlay = 0, s->cell_page = -1;
        memset(s->mem, 0, sizeof s->mem);
        for (t = 0; t < RFM_MOVE_SEQ_TRACKS; t++)
            if (m->ep.tmem[t].slot == slot + 1)
                m->ep.tmem[t].slot = 0;
        rfmv_ep_sync(m, 0);
        return desc && n ? r : 0;
    }
    memcpy(s->desc, desc, n);
    s->n = n, s->fnv = r32(desc + 20);
    s->nlay = 0, s->cell_page = -1;                     /* (the layers and the values again: the host's next calls) */
    memset(s->eng, 0xFF, sizeof s->eng), memset(s->ch, 0xFF, sizeof s->ch);
    slot_values_none(s);
    rfmv_ep_sync(m, 0);
    return 0;
}
void rfm_move_ep_layers(rfm_move_t *m, int slot, int n, const uint8_t engine[], const uint8_t midi_ch[])
{
    rfm_move_ep_slot_t *s;
    int l, nl;
    if (slot < 0 || slot >= RFM_MOVE_FW_SLOTS || !m->ep.s[slot].n || n < 0)
        return;
    s = &m->ep.s[slot];
    nl = s->desc[11];
    for (l = 0; l < RFM_MOVE_EP_LAYERS; l++) {
        const int has = l < nl && l < n;
        s->eng[l] = (uint8_t)(has && engine && engine[l] < s->desc[10] ? engine[l] : 0xFF);
        s->ch[l] = (uint8_t)(has && midi_ch && midi_ch[l] < 16 ? midi_ch[l] : 0xFF);
    }
    s->nlay = (uint8_t)nl;
    rfmv_ep_sync(m, 0);
}
void rfm_move_ep_values(rfm_move_t *m, int slot, int layer, const int16_t *val, int n)
{
    rfm_move_ep_slot_t *s;
    int i, np;
    if (slot < 0 || slot >= RFM_MOVE_FW_SLOTS || !m->ep.s[slot].n || !val || n < 0)
        return;
    s = &m->ep.s[slot];
    if (layer < 0 || layer >= s->desc[11])
        return;
    np = (int)r16(s->desc + 8);
    for (i = 0; i < np; i++)
        s->val[layer][i] = i < n ? val[i] : (int16_t)RFM_MOVE_EP_NONE;
    for (i = 0; i < 16; i++)                            /* (the firmware has the value sent: the record shows it) */
        if (m->ep.ovr[i].on && m->ep.ovr[i].slot == slot && m->ep.ovr[i].layer == layer &&
            s->val[layer][m->ep.ovr[i].idx] == m->ep.ovr[i].v)
            m->ep.ovr[i].on = 0;
}
void rfm_move_ep_cells(rfm_move_t *m, int slot, int layer, int page, const rfm_move_ep_cell_t cells[8])
{
    rfm_move_ep_slot_t *s;
    int k, i;
    if (slot < 0 || slot >= RFM_MOVE_FW_SLOTS || !m->ep.s[slot].n || !cells)
        return;
    s = &m->ep.s[slot];
    s->cell_page = (int16_t)(page >= 0 && page < (int)r16(s->desc + 12) ? page : -1);
    s->cell_layer = (uint8_t)layer;
    for (k = 0; k < 8; k++) {                           /* (printable, NUL-terminated: the screen's own copy) */
        s->cell[k].pid = cells[k].pid;
        for (i = 0; i < 10 && cells[k].name[i]; i++)
            s->cell[k].name[i] = cells[k].name[i] >= 0x20 && cells[k].name[i] < 0x7F ? cells[k].name[i] : '?';
        s->cell[k].name[i] = 0;
        for (i = 0; i < 10 && cells[k].text[i]; i++)
            s->cell[k].text[i] = cells[k].text[i] >= 0x20 && cells[k].text[i] < 0x7F ? cells[k].text[i] : '?';
        s->cell[k].text[i] = 0;
    }
}
int rfm_move_ep_has(const rfm_move_t *m, int slot)
{
    return slot >= 0 && slot < RFM_MOVE_FW_SLOTS && m->ep.s[slot].n && m->ep.s[slot].nlay;
}

/* ================================================================ lights, screen, words === */
/* (R4) knob k is the page's k-th parameter: lit in the colour of the slot it controls (the Track LEDs' orange, blue,
 * yellow), dim to full by its value as the CC knobs; white while touched; nothing on it (or a layer off): dark */
void rfmv_ep_leds(const rfm_move_t *m, rgb_t c[L_WHITE0])
{
    cur_t cu;
    int k;
    if (!rfmv_ep_page(m) || m->seq.smenu || m->seq.step_held)
        return;
    resolve(m, &cu);
    for (k = 0; k < RFM_MOVE_NKNOBS; k++) {
        char n[16], v[16];
        double f = 0;
        int bip = 0;
        if (cu.slot < 0 || !rfmv_ep_knob_words(m, k, n, sizeof n, v, sizeof v, &f, &bip))
            c[L_RING0 + k] = RFMV_OFF;
        else if (m->touched == k)
            c[L_RING0 + k] = RFMV_WHITE;
        else {
            const double u = bip ? (f + 1) / 2 : f;
            c[L_RING0 + k] = rfmv_sc(RFMV_SLOT_RGB[cu.slot], 16 + (int)(clampi((int)(u * 1000), 0, 1000) * 240 / 1000));
        }
    }
}
/* the knob pages' strip: TRIG, FM-1, (ENGINE,) banks 1..4: seven cells with the page, six without */
void rfmv_kpage_cells(const rfm_move_t *m, int *n, int *cur)
{
    const int pg = rfmv_seq_kpage(m);
    if (m->seq.eng || rfmv_ep_avail(m))
        *n = RFM_MOVE_NBANKS + 3, *cur = pg;
    else
        *n = RFM_MOVE_NBANKS + 2, *cur = pg >= 3 ? pg - 1 : pg;
}
static void strip(const rfm_move_t *m, mr_canvas_t *cv)
{
    int n, cur;
    rfmv_kpage_cells(m, &n, &cur);
    v2_strip(cv, n, 1u << cur, -1, 0, 128);
}
/* the layer's and engine's names ("Chord", "VA"; "Off") */
static void names(const rfm_move_t *m, const cur_t *c, char *ln, char *en)
{
    const rfm_move_ep_slot_t *s = S(m, c->slot);
    lay_t L;
    lay_of(s, &L);
    snprintf(ln, 11, "%.10s", lay_name(s, &L, c->layer));
    snprintf(en, 11, "%.10s", c->eng == 0xFF ? "Off" : (const char *)eng_at(s, &L, c->eng) + 2);
}
/* POP_EPA: (pop_i 0) arriving: "Chord  VA" small at the top left, the page's name big ("FILTER"; a layer off: "Off"),
 * the strip; (1) the layer's engine changed: "Chord" small, the new engine big ("FM6"). POP_EPK: a knob's name (2x),
 * its value (4x, 2x when wider than the screen), a bar from the range (BIPOLAR: from the middle), the strip. 1 drawn */
int rfmv_ep_pop_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    cur_t c;
    char ln[11], en[11], top[24];
    resolve(m, &c);
    if (c.slot < 0)
        return 0;
    if (m->pop == POP_EPK) {
        char n[16], v[16];
        double f = 0;
        int bip = 0;
        if (!rfmv_ep_knob_words(m, m->pop_i, n, sizeof n, v, sizeof v, &f, &bip))
            return 0;
        mr_gfx_clear(cv, 0);
        v2_titleBox(cv, n, v, 64, 124);
        v2_vbar(cv, f, bip, 4, 120, 52);
        strip(m, cv);
        return 1;
    }
    names(m, &c, ln, en);
    mr_gfx_clear(cv, 0);
    {
        char big[12];
        int n, cur;
        if (m->pop_i == 1 || c.eng == 0xFF)             /* (the engine changed; a layer off: "Bass" over "Off") */
            snprintf(top, sizeof top, "%s", ln), snprintf(big, sizeof big, "%s", en);
        else {
            lay_t L;
            lay_of(S(m, c.slot), &L);
            snprintf(top, sizeof top, "%s  %s", ln, en);
            snprintf(big, sizeof big, "%.10s", c.page == NOPAGE ? en   /* (an engine with no pages: its name) */
                                               : (const char *)page_at(S(m, c.slot), &L, c.page) + 1);
        }
        v2_T(cv, 0, 1, top, 1, 1);
        rfmv_kpage_cells(m, &n, &cur);
        v2_modeName(cv, big, n, cur);
    }
    return 1;
}
/* the page list (the jog's click): the layer and the engine on a title line, then the engine's pages, three rows at
 * 2x, the page shown inverted (as Global settings' rows), a scroll mark at the right. 1 drawn */
#define EPL_ROWS 3
int rfmv_ep_list_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    cur_t c;
    const rfm_move_ep_slot_t *s;
    const uint8_t *E;
    lay_t L;
    char ln[11], en[11], top[24];
    int n, first, sel, top_i, i;
    resolve(m, &c);
    if (c.page == NOPAGE)
        return 0;
    s = S(m, c.slot);
    lay_of(s, &L);
    E = eng_at(s, &L, c.eng);
    n = E[13], first = (int)r16(E + 14), sel = c.page - first;
    top_i = n <= EPL_ROWS ? 0 : clampi(sel - 1, 0, n - EPL_ROWS);
    names(m, &c, ln, en);
    snprintf(top, sizeof top, "%s  %s", ln, en);
    v2_T(cv, 0, 1, top, 1, 1);
    v2_R(cv, 0, 9, 128, 1, 1);
    for (i = 0; i < EPL_ROWS && top_i + i < n; i++) {
        const int y = 11 + i * 18, on = top_i + i == sel;
        char pn[12];
        snprintf(pn, sizeof pn, "%.10s", (const char *)page_at(s, &L, first + top_i + i) + 1);
        if (on)
            v2_R(cv, 0, y, 125, 17, 1);
        v2_T(cv, 3, y + 2, pn, 2, !on);
    }
    if (n > EPL_ROWS) {
        const int h = 53 * EPL_ROWS / n;
        v2_R(cv, 126, 11 + (53 - h) * top_i / (n - EPL_ROWS), 2, h, 1);
    }
    return 1;
}
/* the words said (DESIGN.md 10): arriving "Engine, Filter"; a layer off "Engine, Bass off"; the engine changed
 * "Chord, FM6"; a touch or a turn "Cutoff, 1.2k" */
void rfmv_ep_pop_words(const rfm_move_t *m, char *o, int cap)
{
    cur_t c;
    char ln[11], en[11], t[16];
    o[0] = 0;
    resolve(m, &c);
    if (c.slot < 0)
        return;
    if (m->pop == POP_EPK) {
        char n[16], v[16];
        if (rfmv_ep_knob_words(m, m->pop_i, n, sizeof n, v, sizeof v, NULL, NULL))
            snprintf(o, (size_t)cap, "%s, %s", n, v);
        return;
    }
    names(m, &c, ln, en);
    if (m->pop_i == 1)
        snprintf(o, (size_t)cap, "%s, %s", ln, c.eng == 0xFF ? "off" : en);
    else if (c.page == NOPAGE)
        snprintf(o, (size_t)cap, "Engine, %s %s", ln, c.eng == 0xFF ? "off" : en);
    else {
        lay_t L;
        lay_of(S(m, c.slot), &L);
        rfmv_tcase((const char *)page_at(S(m, c.slot), &L, c.page) + 1, t, sizeof t);
        snprintf(o, (size_t)cap, "Engine, %s", t);
    }
}
