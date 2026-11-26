/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm_move's firmware browser (0.4.0): the projects view's Tracks 1..3 are three firmware slots, Track 4 the
 * sequencer's projects as before; Mute there is the catalog, Loop the firmware's states, Session FX setup. The owner's
 * design (move-firmware-browser-design.md) and its mockup's screens (move-firmware-browser-mockup.html), ported
 * pixel for pixel: the mockup's own primitives below (its FB), font5x7 in its 8-row cell. Also the library blob, FX
 * setup's part of the project blob, and the sequencer's MIDI tagged with its track. Portable C99, no allocation:
 * the catalog and the FX modules are the host's arrays (move.h has the contract). */
#include <stdio.h>
#include <string.h>

#include "move_int.h"
#include "../third_party/moveremote/common/font5x7.h"

#define clampi rfmv_clampi
typedef rfmv_rgb_t rgb_t;
typedef mr_canvas_t B;
#define W 128
#define H 64

enum { BUSY_NONE, BUSY_LOAD, BUSY_DOWNLOAD, BUSY_UPDATE };
enum { ST_NEW, ST_UPDATE, ST_OK };                      /* a firmware in the catalog: not on the Move, an update, current */
#define CAT0 3                                          /* the first category step */
#define CAP_MS 1200                                     /* Capture + step: the step's name this long */
#define INFO_ROWS 5
#define LINES 96                                        /* a description's rows at most (20 columns) */
#define LW 22

/* the projects' 16 colours (project.c's PROJ_RGB: the mockup's PAL) */
static const rgb_t PAL[16] = {
    { 255, 36, 36 },  { 255, 120, 0 },  { 255, 200, 0 },  { 190, 255, 40 }, { 40, 255, 70 },  { 0, 255, 170 },
    { 20, 220, 255 }, { 40, 120, 255 }, { 90, 60, 255 },  { 170, 60, 255 }, { 255, 50, 210 }, { 255, 110, 150 },
    { 255, 160, 100 }, { 160, 110, 60 }, { 150, 150, 160 }, { 255, 255, 255 },
};
static const char *const PALNAME[16] = { "Red", "Orange", "Yellow", "Lime", "Green", "Mint", "Cyan", "Blue",
                                         "Indigo", "Purple", "Magenta", "Pink", "Peach", "Brown", "Grey", "White" };
static const char *const CATS[5] = { "Synth", "Groovebox", "Chords", "Drums", "Other" };
static const uint8_t CATCOL[5] = { 6, 1, 9, 0, 5 };    /* a category's default pad colour (palette index) */
static const rgb_t FAVCOL = { 255, 70, 110 }, AMBER = { 255, 170, 30 }, WHITE = { 255, 255, 255 }, OFF = { 0, 0, 0 };
static const rgb_t TRACK4 = { 255, 42, 42 };
static const rgb_t FXCOL[4] = { { 0, 255, 170 }, { 255, 255, 255 }, { 255, 170, 30 }, { 255, 110, 150 } };
static const char *const FXPAGE[4] = { "MIDI FX", "Routing", "Audio FX", "Mixer" };
enum { PG_MIDI, PG_ROUTE, PG_AUDIO, PG_MIX };

static int ifloor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* ================================================================ the mockup's primitives === */
static void px(B *g, int x, int y, int v)
{
    if (x >= 0 && x < W && y >= 0 && y < H)
        g->px[y][x] = (uint8_t)(v != 0);
}
static void fill(B *g, int x, int y, int w, int h, int v)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            px(g, i, j, v);
}
static void rect(B *g, int x, int y, int w, int h)
{
    int i, j;
    for (i = x; i < x + w; i++)
        px(g, i, y, 1), px(g, i, y + h - 1, 1);
    for (j = y; j < y + h; j++)
        px(g, x, j, 1), px(g, x + w - 1, j, 1);
}
static void drect(B *g, int x, int y, int w, int h)  /* a dotted border: every other pixel */
{
    int i, j;
    for (i = x; i < x + w; i += 2)
        px(g, i, y, 1), px(g, i, y + h - 1, 1);
    for (j = y; j < y + h; j += 2)
        px(g, x, j, 1), px(g, x + w - 1, j, 1);
}
static void hline(B *g, int x, int y, int w, int dash)
{
    int i;
    for (i = 0; i < w; i++)
        if (!dash || ((i >> 1) & 1) == 0)
            px(g, x + i, y, 1);
}
static int tw(const char *s, int sc) { const int n = (int)strlen(s); return n * 6 * sc - sc; }
static void text(B *g, int x, int y, const char *s, int sc, int v)
{
    for (; *s; s++, x += 6 * sc) {
        const uint8_t *gl = mr_glyph(*s);
        int col, r;
        for (col = 0; col < 5; col++)
            for (r = 0; r < 8; r++)                     /* (the font's 8-row cell: descenders; the mockup drew 7) */
                if (gl[col] >> r & 1)
                    fill(g, x + col * sc, y + r * sc, sc, sc, v);
    }
}
static void ctext_in(B *g, int y, const char *s, int sc, int x0, int w)
{
    text(g, x0 + ifloor_div(w - tw(s, sc), 2), y, s, sc, 1);
}
static void ctext(B *g, int y, const char *s, int sc) { ctext_in(g, y, s, sc, 0, W); }
static void bits(B *g, const uint8_t *b, int n, int x0, int y0)
{
    const int st = n / 8;
    int x, y;
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++)
            if (b[y * st + (x >> 3)] & (0x80 >> (x & 7)))
                px(g, x0 + x, y0 + y, 1);
}
static void icon(B *g, int x, int y, const char *const *rows, int n)
{
    int i, j;
    for (j = 0; j < n; j++)
        for (i = 0; rows[j][i]; i++)
            if (rows[j][i] == '1')
                px(g, x + i, y + j, 1);
}
static void strip(B *g, int n, int cur)
{
    int seg, i;
    if (n < 2)
        return;
    seg = ifloor_div(W - (n - 1) * 2, n);
    for (i = 0; i < n; i++)
        fill(g, i * (seg + 2), i == cur ? 61 : 62, seg, i == cur ? 3 : 1, 1);
}
static void line(B *g, int x0, int y0, int x1, int y1)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy;
    for (;;) {
        int e2;
        px(g, x0, y0, 1);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * e;
        if (e2 >= dy)
            e += dy, x0 += sx;
        if (e2 <= dx)
            e += dx, y0 += sy;
    }
}
static const char *const HEART[5] = { "01010", "11111", "11111", "01110", "00100" };
static const char *const HEARTO[5] = { "01010", "10101", "10001", "01010", "00100" };
static const char *const UP[5] = { "00100", "01110", "11111", "00100", "00100" };
static const char *const DOWN[5] = { "00100", "00100", "11111", "01110", "00100" };
static const char *const CHEV[3] = { "10001", "01010", "00100" };
static const char *const UPMARK[2] = { "010", "111" };

/* ================================================================ words === */
/* the printable ASCII of s, at most cap - 1 (the mockup's ascii(): the host gives ASCII; anything else is skipped) */
static void asc(const char *s, char *o, int cap)
{
    int n = 0;
    for (; s && *s && n < cap - 1; s++)
        if ((unsigned char)*s >= 32 && (unsigned char)*s < 127)
            o[n++] = *s;
    o[n] = 0;
}
static void cut(char *s, int n)                         /* s.slice(0, n) */
{
    if (n >= 0 && (int)strlen(s) > n)
        s[n] = 0;
}
static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
/* the mockup's wrap(s, n): words to rows of n, long words cut; rows [skip, skip + cap) into out; returns the rows */
static int wrap(const char *s, int n, char out[][LW], int skip, int cap)
{
    int total = 0;
    char ln[LW];
    int ll = 0;
#define PUSH(str, len) do { if (total >= skip && total - skip < cap) { memcpy(out[total - skip], str, (size_t)(len)); \
                                out[total - skip][len] = 0; } total++; } while (0)
    while (*s) {
        ll = 0;
        while (*s && *s != '\n') {                      /* a paragraph */
            const char *w;
            int wl;
            while (*s && *s != '\n' && is_ws(*s))
                s++;
            if (!*s || *s == '\n')
                break;
            w = s;
            while (*s && !is_ws(*s))
                s++;
            wl = (int)(s - w);
            while (wl > n) {
                if (ll) {
                    PUSH(ln, ll);
                    ll = 0;
                }
                PUSH(w, n);
                w += n, wl -= n;
            }
            if (!ll)
                memcpy(ln, w, (size_t)wl), ll = wl;
            else if (ll + 1 + wl <= n)
                ln[ll] = ' ', memcpy(ln + ll + 1, w, (size_t)wl), ll += 1 + wl;
            else {
                PUSH(ln, ll);
                memcpy(ln, w, (size_t)wl), ll = wl;
            }
        }
        if (ll)
            PUSH(ln, ll);
        if (*s == '\n')
            s++;
    }
#undef PUSH
    return total;
}
/* two letters of a name: the first two words' initials, or its first two (the first upper case) */
static void abbr(const char *name, char o[3])
{
    char a[64], s[64];
    int i, n = 0, w = 0;
    const char *p;
    asc(name, a, sizeof a);
    for (i = 0; a[i]; i++)
        if ((a[i] >= 'A' && a[i] <= 'Z') || (a[i] >= 'a' && a[i] <= 'z') || (a[i] >= '0' && a[i] <= '9') || a[i] == ' ')
            s[n++] = a[i];
    s[n] = 0;
    for (p = s; *p == ' '; p++) {}
    memmove(s, p, strlen(p) + 1);
    for (n = (int)strlen(s); n > 0 && s[n - 1] == ' '; n--)
        s[n - 1] = 0;
    for (p = s; *p && *p != ' '; p++) {}
    while (*p == ' ')
        p++;
    if (*p)                                             /* (two words or more) */
        o[0] = s[0], o[1] = *p, o[2] = 0, w = 2;
    else {
        o[0] = s[0], o[1] = s[0] ? s[1] : 0, o[2] = 0;
        w = (int)strlen(o);
    }
    if (w == 2 && o[0] >= 'a' && o[0] <= 'z')
        o[0] = (char)(o[0] - 32);
}

/* ================================================================ the catalog, the library === */
static rfm_move_fw_state_t *F(rfm_move_t *m) { return &m->fw; }
static const rfm_move_fw_t *ent(const rfm_move_t *m, int i) { return i >= 0 && i < m->fw.n ? &m->fw.list[i] : NULL; }
static const char *ename(const rfm_move_t *m, int i) { const rfm_move_fw_t *e = ent(m, i); return e && e->name ? e->name : "?"; }
static int installed(const rfm_move_t *m, int i) { const rfm_move_fw_t *e = ent(m, i); return e && e->installed && e->installed[0]; }
static int status(const rfm_move_t *m, int i)
{
    const rfm_move_fw_t *e = ent(m, i);
    if (!installed(m, i))
        return ST_NEW;
    return e->version && strcmp(e->installed, e->version) ? ST_UPDATE : ST_OK;
}
static int find_id(const rfm_move_t *m, const char *id)
{
    int i;
    if (!id || !id[0])
        return -1;
    for (i = 0; i < m->fw.n; i++)
        if (m->fw.list[i].id && !strcmp(m->fw.list[i].id, id))
            return i;
    return -1;
}
static int rec_of(const rfm_move_t *m, int fw)
{
    int r;
    if (fw < 0)
        return -1;
    for (r = 0; r < m->fw.nrec; r++)
        if (m->fw.rec[r].fw == fw)
            return r;
    return -1;
}
static int fav_pos(const rfm_move_t *m, int r)
{
    int k;
    for (k = 0; r >= 0 && k < m->fw.nfav; k++)
        if (m->fw.fav[k] == r)
            return k;
    return -1;
}
static int rec_used(const rfm_move_t *m, int r)
{
    const rfm_move_lib_rec_t *c = &m->fw.rec[r];
    return c->color || c->tag_set || fav_pos(m, r) >= 0;
}
/* the record of firmware fw, made if needed (-1: the library is full) */
static int rec_make(rfm_move_t *m, int fw)
{
    rfm_move_fw_state_t *f = F(m);
    int r = rec_of(m, fw), k;
    const rfm_move_fw_t *e = ent(m, fw);
    if (r >= 0 || !e || !e->id || strlen(e->id) >= RFM_MOVE_FW_ID_MAX)
        return r;
    if (f->nrec >= RFM_MOVE_FW_LIB_MAX)                 /* full: one that keeps nothing goes */
        for (r = 0; r < f->nrec; r++)
            if (!rec_used(m, r)) {
                f->rec[r] = f->rec[--f->nrec];
                for (k = 0; k < f->nfav; k++)
                    if (f->fav[k] == f->nrec)
                        f->fav[k] = (uint16_t)r;
                break;
            }
    if (f->nrec >= RFM_MOVE_FW_LIB_MAX)
        return -1;
    r = f->nrec++;
    memset(&f->rec[r], 0, sizeof f->rec[r]);
    strcpy(f->rec[r].id, e->id);
    f->rec[r].fw = (int16_t)fw;
    return r;
}
static void lib_changed(rfm_move_t *m) { m->fw.lib_dirty = 1, m->fw.lib_ms = rfmv_now_ms(m); }
static void resolve(rfm_move_t *m)                      /* the records' and the slots' list indexes */
{
    rfm_move_fw_state_t *f = F(m);
    int r, s;
    for (r = 0; r < f->nrec; r++)
        f->rec[r].fw = (int16_t)find_id(m, f->rec[r].id);
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
        f->slot_fw[s] = (int16_t)find_id(m, f->slot_id[s]);
}
static int is_fav(const rfm_move_t *m, int fw) { return fav_pos(m, rec_of(m, fw)) >= 0; }
/* Favorites (0) or All (1) as shown: All while there are no favourites (on entering the view or a firmware track,
 * or the last one removed) until step 1 / 2 is pressed; the setting keeps the user's choice (0.4.1) */
static int scope(const rfm_move_t *m) { return m->s.fw_scope == 1 || m->fw.all_auto; }
static int color(const rfm_move_t *m, int fw)           /* palette index 0..15 */
{
    const int r = rec_of(m, fw);
    const rfm_move_fw_t *e = ent(m, fw);
    if (r >= 0 && m->fw.rec[r].color)
        return m->fw.rec[r].color - 1;
    return e && e->cat < 5 ? CATCOL[e->cat] : 14;
}
static int has_tag_r(const rfm_move_t *m, int fw, int r, int step);
static int has_tag(const rfm_move_t *m, int fw, int step) { return has_tag_r(m, fw, rec_of(m, fw), step); }
#define VIRT_STEP 16                                    /* (0.5.4) Fantasy CFWs (rfm_move_fw_t.virt): built in */
#define RND_STEP 13                                     /* (0.14.0) Random: a firmware of the bank shown (no tag) */
#define RFMV_FWR_MS 240                                 /* .. the selection rolling to it */
static const rgb_t VIRTCOL = { 255, 50, 210 };          /* (its step: magenta) */
static int tag_defined(const rfm_move_t *m, int step)
{
    return (step >= CAT0 && step < CAT0 + 5) || step == VIRT_STEP ||
           (step >= 8 && step < VIRT_STEP && step != RND_STEP && m->fw.tag[step - 8][0]);
}
static void tag_name(const rfm_move_t *m, int step, char *o, int cap)
{
    const int c = step - CAT0;
    if (c >= 0 && c < 5)
        snprintf(o, (size_t)cap, "%s", CATS[c]);
    else if (step == VIRT_STEP)
        snprintf(o, (size_t)cap, "Fantasy");
    else if (step >= 8 && step <= 16 && m->fw.tag[step - 8][0])
        snprintf(o, (size_t)cap, "%s", m->fw.tag[step - 8]);
    else
        snprintf(o, (size_t)cap, "Tag %d", step - 7);
}
/* (0.4.2) a filter step's name, its state, and what the list shows now (the filter popup's words) */
static int scope(const rfm_move_t *m);
static void step_name(const rfm_move_t *m, int step, char *o, int cap)
{
    if (step == 1 || step == 2)
        snprintf(o, (size_t)cap, "%s", m->fw.catalog ? (step == 1 ? "Update ready" : "Not installed")
                                                     : (step == 1 ? "Favorites" : "All"));
    else
        tag_name(m, step, o, cap);
}
static int step_on(const rfm_move_t *m, int step)
{
    if (step == 1 || step == 2)
        return m->fw.catalog ? (m->s.fw_cat_scope & step) != 0 : scope(m) == step - 1;
    return step >= CAT0 && step <= 16 && (m->s.fw_filter >> (step - 1) & 1);
}
static void showing(const rfm_move_t *m, char *o, int cap)
{
    char cats[260], q[40], nm[24];
    int n = 0, s, k = 0, nsel = 0;
    cats[0] = 0;
    for (s = CAT0; s <= 16; s++)
        nsel += m->s.fw_filter >> (s - 1) & 1;
    for (s = CAT0; s <= 16; s++)                        /* "Synth", "Synth or Drums", "Synth, Chords or Drums" */
        if (m->s.fw_filter >> (s - 1) & 1) {
            tag_name(m, s, nm, sizeof nm);
            n += snprintf(cats + n, sizeof cats - (size_t)n, "%s%s", !k ? "" : k == nsel - 1 ? " or " : ", ", nm);
            if (n >= (int)sizeof cats)
                n = (int)sizeof cats - 1;
            k++;
        }
    if (m->fw.catalog) {
        const int c = m->s.fw_cat_scope & 3;
        snprintf(q, sizeof q, "%s", c == 3 ? "update ready or not installed" : c == 1 ? "update ready"
                                    : c == 2 ? "not installed" : "");
    } else
        snprintf(q, sizeof q, "%s", scope(m) ? "installed" : "favorites");
    if (n && q[0])
        snprintf(o, (size_t)cap, "Showing: %s, %s", cats, q);
    else if (n)
        snprintf(o, (size_t)cap, "Showing: %s", cats);
    else
        snprintf(o, (size_t)cap, "Showing: %s", q[0] ? (!m->fw.catalog && scope(m) ? "all installed" : q) : "the whole catalog");
}
static int track_of(const rfm_move_t *m, int fw)        /* the first slot holding it, -1 */
{
    int s;
    for (s = 0; fw >= 0 && s < RFM_MOVE_FW_SLOTS; s++)
        if (m->fw.slot_fw[s] == fw)
            return s;
    return -1;
}

/* ---- the list the pads show: the catalog (Mute) or Favorites / All, then the category / tag filter */
static int has_tag_r(const rfm_move_t *m, int fw, int r, int step)
{
    const int c = step - CAT0;
    const rfm_move_fw_t *e = ent(m, fw);
    if (step == VIRT_STEP)
        return e && e->virt;                            /* (a fact of the catalog's, never a user tag) */
    if (r >= 0 && (m->fw.rec[r].tag_set >> (step - 1) & 1))
        return m->fw.rec[r].tag_on >> (step - 1) & 1;
    return c >= 0 && c < 5 && e && (e->cats >> c & 1);
}
static int passes(const rfm_move_t *m, int fw)
{
    const uint16_t fl = m->s.fw_filter;
    int s, r;
    if (m->fw.catalog && m->s.fw_cat_scope) {
        const int st = status(m, fw);
        if (!(((m->s.fw_cat_scope & 1) && st == ST_UPDATE) || ((m->s.fw_cat_scope & 2) && st == ST_NEW)))
            return 0;
    }
    if (!fl)
        return 1;
    r = rec_of(m, fw);
    for (s = CAT0; s <= 16; s++)
        if ((fl >> (s - 1) & 1) && has_tag_r(m, fw, r, s))
            return 1;
    return 0;
}
static int in_list(const rfm_move_t *m, int fw)
{
    if (fw < 0 || fw >= m->fw.n)
        return 0;
    if (m->fw.catalog)
        return passes(m, fw);
    return installed(m, fw) && (scope(m) || is_fav(m, fw)) && passes(m, fw);
}
/* the list's length; its rows [from, from + cnt) into out (list indexes, -1 past the end) */
static int walk(const rfm_move_t *m, int from, int cnt, int *out)
{
    int i, n = 0;
    for (i = 0; i < cnt; i++)
        out[i] = -1;
    if (m->fw.catalog || scope(m)) {
        for (i = 0; i < m->fw.n; i++)
            if ((m->fw.catalog || installed(m, i)) && passes(m, i)) {
                if (n >= from && n < from + cnt)
                    out[n - from] = i;
                n++;
            }
    } else
        for (i = 0; i < m->fw.nfav; i++) {
            const int fw = m->fw.rec[m->fw.fav[i]].fw;
            if (fw >= 0 && installed(m, fw) && passes(m, fw)) {
                if (n >= from && n < from + cnt)
                    out[n - from] = fw;
                n++;
            }
        }
    return n;
}
static int list_len(const rfm_move_t *m) { return walk(m, 0, 0, NULL); }
static int nbanks_n(int n) { return n > 32 ? (n + 31) / 32 : 1; }
static int nbanks(const rfm_move_t *m) { return nbanks_n(list_len(m)); }
static int bank_key(const rfm_move_t *m) { return m->fw.catalog ? 2 : scope(m) ? 1 : 0; }
static int cur_bank(const rfm_move_t *m)
{
    const int b = m->fw.bank[bank_key(m)], n = nbanks(m);
    return b < n - 1 ? b : n - 1;
}
static int bank_ids(const rfm_move_t *m, int ids[32])   /* the bank shown (list indexes, -1 past its end); its size */
{
    const int n = walk(m, cur_bank(m) * 32, 32, ids);
    const int left = n - cur_bank(m) * 32;
    return left < 0 ? 0 : left > 32 ? 32 : left;
}

/* ================================================================ actions === */
static void send(rfm_move_t *m, rfm_move_act_t *a)
{
    a->frame = m->seq.frame;
    if (m->fn)
        m->fn(m->ctx, a);
}
static void act_fw(rfm_move_t *m, int kind, int slot, int fw, int value)
{
    rfm_move_act_t a;
    const rfm_move_fw_t *e = ent(m, fw);
    memset(&a, 0, sizeof a);
    a.kind = (uint8_t)kind, a.index = (uint8_t)(slot < 0 ? 0 : slot), a.slot = a.index, a.value = (int16_t)value;
    if (e && e->id)
        a.data = (const uint8_t *)e->id, a.len = (uint32_t)strlen(e->id);
    send(m, &a);
}
static void act_mix(rfm_move_t *m, int slot)
{
    const rfm_move_fxset_t *x = &m->seq.fx;
    rfm_move_act_t a;
    int k;
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_MIX, a.index = (uint8_t)slot, a.slot = (uint8_t)slot;
    a.mix[0] = m->fw.level[slot], a.mix[1] = x->pan[slot];
    for (k = 0; k < RFM_MOVE_FX_SLOTS; k++)
        a.mix[2 + k] = x->send[slot][k];
    send(m, &a);
}
static void act_route(rfm_move_t *m)
{
    const rfm_move_fxset_t *x = &m->seq.fx;
    rfm_move_act_t a;
    int k;
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_ROUTE;
    for (k = 0; k < 3; k++)
        a.filt[k] = x->ffilt[k], a.filt[3 + k] = x->mfx[k].filt, a.mroute[k] = x->mroute[k];   /* (0.6.6) */
    send(m, &a);
}
static rfm_move_fxslot_t *fxslot(rfm_move_t *m, int kind, int slot)
{
    return kind == RFM_MOVE_FX_MIDI ? &m->seq.fx.mfx[slot] : &m->seq.fx.afx[slot];
}
static const rfm_move_fx_module_t *module(const rfm_move_t *m, int kind, int mod)
{
    return mod >= 0 && mod < m->fw.nmods[kind] ? &m->fw.mods[kind][mod] : NULL;
}
static int nparams(const rfm_move_fx_module_t *md) { return !md ? 0 : md->nparams < RFM_MOVE_FX_PARAMS ? md->nparams : RFM_MOVE_FX_PARAMS; }
/* (0.5.0) a module's parameter pages: the host's plan, or pages of 8 in the params' order */
static int host_pages(const rfm_move_fx_module_t *md) { return md && md->npages > 0 && md->pages; }
static int npages(const rfm_move_fx_module_t *md)
{
    const int np = nparams(md), n = host_pages(md) ? md->npages : (np + RFM_MOVE_FX_KNOBS - 1) / RFM_MOVE_FX_KNOBS;
    return !np ? 0 : n < RFM_MOVE_FX_PAGES ? n : RFM_MOVE_FX_PAGES;
}
static int page_param(const rfm_move_fx_module_t *md, int pg, int k)   /* knob k's parameter on page pg, -1 a gap */
{
    int p;
    if (pg < 0 || pg >= npages(md) || k < 0 || k >= RFM_MOVE_FX_KNOBS)
        return -1;
    p = host_pages(md) ? md->pages[pg].knob[k] : pg * RFM_MOVE_FX_KNOBS + k;
    return p >= 0 && p < nparams(md) ? p : -1;
}
static uint8_t param_def(const rfm_move_fx_param_t *pa)
{
    return pa->nwords > 0 ? (uint8_t)clampi(pa->def, 0, pa->nwords - 1) : (uint8_t)clampi(pa->def, 0, 127);
}
/* a value as it reads: a word, lo..hi with its decimals and unit, or 0..127 */
static void param_text(const rfm_move_fx_param_t *pa, int v, char *o, int cap)
{
    if (v > 127)
        snprintf(o, (size_t)cap, "-");
    else if (pa->nwords > 0 && pa->words && v < pa->nwords)
        asc(pa->words[v], o, cap);
    else if (pa->nwords <= 0 && pa->lo != pa->hi) {
        const double x = pa->lo + (pa->hi - pa->lo) * v / 127.0;
        char u[12];
        asc(pa->unit ? pa->unit : "", u, sizeof u);
        snprintf(o, (size_t)cap, "%.*f%s", pa->decimals < 4 ? pa->decimals : 4, x, u);
    } else
        snprintf(o, (size_t)cap, "%d", v);
}
static void act_fx_load(rfm_move_t *m, int kind, int slot)
{
    const rfm_move_fxslot_t *x = fxslot(m, kind, slot);
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_FX_LOAD, a.fx = (uint8_t)kind, a.index = (uint8_t)slot, a.slot = (uint8_t)slot;
    a.value = x->mod;
    if (x->mod >= 0)
        a.data = (const uint8_t *)x->name, a.len = (uint32_t)strlen(x->name);
    send(m, &a);
}
static void act_fx_param(rfm_move_t *m, int kind, int slot, int k)
{
    const rfm_move_fxslot_t *x = fxslot(m, kind, slot);
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_FX_PARAM, a.fx = (uint8_t)kind, a.index = (uint8_t)slot, a.slot = (uint8_t)slot;
    a.param = (uint8_t)k, a.value = x->val[k];
    send(m, &a);
}

/* the sequencer's MIDI, tagged with its track 1..16 (0: not the sequencer's) */
void rfmv_emit_midi_t(rfm_move_t *m, int track, int status, int d1, int d2)
{
    rfm_move_act_t a;
    memset(&a, 0, sizeof a);
    a.frame = m->seq.frame;
    a.kind = RFM_MOVE_ACT_MIDI, a.n = 3, a.track = (uint8_t)track;
    a.bytes[0] = (uint8_t)status, a.bytes[1] = (uint8_t)(d1 & 127), a.bytes[2] = (uint8_t)(d2 & 127);
    if (m->fn)
        m->fn(m->ctx, &a);
}

/* ================================================================ the popups === */
static void pop(rfm_move_t *m, int kind, int a, int b)
{
    rfm_move_fw_state_t *f = F(m);
    f->pop = (uint8_t)kind, f->pop_a = (int16_t)a, f->pop_b = (int16_t)b, f->pop_ms = rfmv_now_ms(m);
}
static void pop_msg(rfm_move_t *m, int kind, int a, int b, const char *t)
{
    pop(m, kind, a, b);
    snprintf(m->fw.pop_text, sizeof m->fw.pop_text, "%s", t ? t : "");
}
static int pop_up(const rfm_move_t *m, int kind)
{
    const rfm_move_fw_state_t *f = &m->fw;
    return f->pop && (kind < 0 || f->pop == kind) &&
           (rfmv_now_ms(m) - f->pop_ms < RFM_MOVE_POPUP_MS || (f->pop == FP_PARAM && f->pop_b));   /* (0.4.5: an FX
                                                         * parameter's knob touched: up while it is) */
}
static void pop_clear(rfm_move_t *m) { m->fw.pop = FP_NONE; }
/* (0.5.10) the host's capabilities: firmware slots (1 or 3), FX setup */
static int nslots(const rfm_move_t *m) { return m->fw.caps & RFM_MOVE_CAP_SLOTS ? RFM_MOVE_FW_SLOTS : 1; }
static int has_fx(const rfm_move_t *m) { return (m->fw.caps & RFM_MOVE_CAP_FX) != 0; }

/* ================================================================ the host's calls === */
void rfmv_fw_init(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = F(m);
    int s;
    memset(f, 0, sizeof *f);
    f->voices = 1, f->caps = RFM_MOVE_CAPS_ALL;
    f->sel = -1, f->cap_step = -1, f->browse = -1, f->busy_fw = -1, f->busy_slot = -1;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
        f->slot_fw[s] = -1, f->level[s] = 100, f->st_cur[s] = -1;
    f->track = 3;
}
/* (0.6.6) the MIDI routes: a slot reaches b through them (bits 0..2) */
static int mroute_reaches(const uint8_t r[RFM_MOVE_FW_SLOTS], int a, int b)
{
    uint8_t seen = 0, todo = (uint8_t)(1u << a);
    while (todo) {
        int s;
        for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
            if (todo >> s & 1) {
                todo = (uint8_t)(todo & ~(1u << s)), seen = (uint8_t)(seen | 1u << s);
                todo = (uint8_t)(todo | ((r[s] & 7) & ~seen));
            }
    }
    return seen >> b & 1;
}
int rfmv_mroute_check(const uint8_t r[RFM_MOVE_FW_SLOTS])
{
    int s, t;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
        if (r[s] > 15 || (r[s] >> s & 1))
            return -1;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)             /* (a cycle: some target reaches back) */
        for (t = 0; t < RFM_MOVE_FW_SLOTS; t++)
            if ((r[s] >> t & 1) && mroute_reaches(r, t, s))
                return -1;
    return 0;
}
void rfmv_fxset_default(rfm_move_fxset_t *x)
{
    int k;
    memset(x, 0, sizeof *x);
    for (k = 0; k < 3; k++) {
        x->mfx[k].mod = x->afx[k].mod = -1;
        x->mfx[k].filt = 0xFFFF, x->ffilt[k] = 0xFFFF, x->pan[k] = 64;
    }
}

void rfm_move_fw_list(rfm_move_t *m, rfm_move_fw_t *entries, int n)
{
    rfm_move_fw_state_t *f = F(m);
    f->list = entries, f->n = entries && n > 0 ? n : 0;
    f->host = 1;
    resolve(m);
    f->sel = -1, f->jog = 0;
    if (f->busy_kind && (f->busy_fw = (int16_t)find_id(m, f->busy_id)) < 0)
        f->busy_kind = BUSY_NONE;                       /* (a load or download under way: kept by its id) */
}
void rfm_move_fw_installed(rfm_move_t *m, int index, const char *version)
{
    if (index >= 0 && index < m->fw.n)
        m->fw.list[index].installed = version;
}
void rfm_move_fw_slots(rfm_move_t *m, const char *const ids[RFM_MOVE_FW_SLOTS])
{
    rfm_move_fw_state_t *f = F(m);
    int s, any = 0;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++) {
        char id[RFM_MOVE_FW_ID_MAX];
        snprintf(id, sizeof id, "%s", s < nslots(m) && ids && ids[s] && strlen(ids[s]) < RFM_MOVE_FW_ID_MAX ? ids[s] : "");
        if (strcmp(id, f->slot_id[s])) {
            strcpy(f->slot_id[s], id), any = 1;
            f->st_used[s] = 0, f->st_cur[s] = -1;       /* (another firmware: the host tells its states) */
        }
        f->slot_fw[s] = (int16_t)find_id(m, id);
    }
    if (f->busy_kind == BUSY_LOAD && f->busy_slot >= 0 && f->slot_fw[f->busy_slot] == f->busy_fw && f->busy_fw >= 0) {
        pop(m, FP_LOADED, f->busy_fw, f->busy_slot);    /* (the load done) */
        f->busy_kind = BUSY_NONE, f->busy_fw = -1;
    }
    if (any)
        lib_changed(m);
    rfmv_say_check(m);
}
static void fw_progress(rfm_move_t *m, const char *id, int pct)
{
    rfm_move_fw_state_t *f = F(m);
    const int fw = f->busy_fw;
    if (!f->busy_kind || fw < 0 || !id || strcmp(id, f->busy_id))
        return;
    if (pct < 0) {                                      /* failed, or the host is too busy */
        pop_msg(m, FP_MSG, fw, pct, pct == RFM_MOVE_FW_BUSY ? "Too busy"
                                      : f->busy_kind == BUSY_LOAD ? "load failed" : "download failed");
        f->busy_kind = BUSY_NONE, f->busy_fw = -1;
        return;
    }
    f->busy_pct = (int8_t)clampi(pct, 0, 100);
    if (pct < 100)
        return;
    if (f->busy_kind == BUSY_LOAD) {
        const int s = f->busy_slot;
        if (s >= 0 && strlen(id) < RFM_MOVE_FW_ID_MAX && strcmp(f->slot_id[s], id)) {
            strcpy(f->slot_id[s], id), f->slot_fw[s] = (int16_t)fw;
            f->st_used[s] = 0, f->st_cur[s] = -1;
            lib_changed(m);
        }
        pop(m, FP_LOADED, fw, s);
    } else
        pop(m, FP_LOADED, fw, f->busy_kind == BUSY_UPDATE ? 10 : 11);
    f->busy_kind = BUSY_NONE, f->busy_fw = -1;
}
void rfm_move_fw_progress(rfm_move_t *m, const char *id, int pct)
{
    fw_progress(m, id, pct);
    rfmv_say_check(m);
}
void rfm_move_fw_voices(rfm_move_t *m, int n) { m->fw.voices = (uint8_t)clampi(n, 1, nslots(m)); }
void rfm_move_fw_caps(rfm_move_t *m, int caps)
{
    rfm_move_fw_state_t *f = F(m);
    int s;
    const int smp = rfmv_smp_cap(m);
    const int kit = rfmv_kit_cap(m);
    f->caps = (uint8_t)(caps & (RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_PROJFW | RFM_MOVE_CAP_SAMPLER | RFM_MOVE_CAP_KIT | RFM_MOVE_CAP_EXIT));
                                                        /* (0.13.0: PROJFW; 0.15.0: SAMPLER; stage 3a: KIT) */
    rfmv_smp_caps(m, smp);
    rfmv_kit_caps(m, kit);
    if (!(f->caps & RFM_MOVE_CAP_PROJFW))
        rfmv_st_close(m), m->pfw_on = 0, m->pfw_msg = 0;
    if (!(f->caps & RFM_MOVE_CAP_SLOTS)) {             /* one slot: the others emptied, slot 0 played */
        for (s = 1; s < RFM_MOVE_FW_SLOTS; s++)
            f->slot_id[s][0] = 0, f->slot_fw[s] = -1, f->st_used[s] = 0, f->st_cur[s] = -1;
        if (f->active)
            rfmv_slot_leaving(m);
        f->active = 0, f->voices = 1;
        if (f->track > 0 && f->track < 3)
            f->track = 0;
        rfmv_slot_changed(m);                           /* (0.8.0: its layout) */
    }
    if (!(f->caps & RFM_MOVE_CAP_FX))
        f->fx = 0, f->browse = -1, f->fx_pages = 0;
}
int rfm_move_fw_caps_get(const rfm_move_t *m) { return m->fw.caps; }
int rfm_move_fw_active(const rfm_move_t *m) { return m->fw.active; }
int rfm_move_fw_level(const rfm_move_t *m, int slot) { return slot >= 0 && slot < 3 ? m->fw.level[slot] : 0; }
int rfm_move_fw_sounding(const rfm_move_t *m)
{
    const rfm_move_fw_state_t *f = &m->fw;
    int bitsv = 0, n = 0, s;
    if (f->slot_id[f->active][0])
        bitsv |= 1 << f->active, n++;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
        if (s != f->active && f->slot_id[s][0] && n < f->voices)
            bitsv |= 1 << s, n++;
    return bitsv;
}
void rfm_move_fw_states(rfm_move_t *m, int slot, uint32_t used)
{
    if (slot >= 0 && slot < RFM_MOVE_FW_SLOTS)
        m->fw.st_used[slot] = used;
    rfmv_say_check(m);
}
void rfm_move_fx_modules(rfm_move_t *m, int kind, const rfm_move_fx_module_t *list, int n)
{
    int s;
    if (kind != RFM_MOVE_FX_MIDI && kind != RFM_MOVE_FX_AUDIO)
        return;
    m->fw.mods[kind] = list, m->fw.nmods[kind] = list && n > 0 ? n : 0;
    for (s = 0; s < RFM_MOVE_FX_SLOTS; s++)
        rfmv_fx_resolve(m, fxslot(m, kind, s), kind);
    if (m->seq.projects && m->fw.fx && !m->fw.nmods[kind] && m->s.fx_page == (kind ? PG_AUDIO : PG_MIDI))
        m->s.fx_page = PG_ROUTE;                        /* (that page has gone) */
}
void rfmv_fx_resolve(rfm_move_t *m, rfm_move_fxslot_t *x, int kind)
{
    int i;
    x->mod = -1;
    for (i = 0; x->name[0] && i < m->fw.nmods[kind]; i++)
        if (m->fw.mods[kind][i].name && !strcmp(m->fw.mods[kind][i].name, x->name)) {
            x->mod = (int16_t)i;
            break;
        }
    if (x->mod >= 0) {                                  /* (0.5.0) values an older project did not keep: defaults */
        const rfm_move_fx_module_t *md = &m->fw.mods[kind][x->mod];
        for (i = 0; i < nparams(md); i++)
            if (x->val[i] > 127)
                x->val[i] = param_def(&md->params[i]);
    }
}
void rfm_move_fx_refused(rfm_move_t *m, int kind, int slot)
{
    rfm_move_fxslot_t *x;
    if ((kind != RFM_MOVE_FX_MIDI && kind != RFM_MOVE_FX_AUDIO) || slot < 0 || slot >= RFM_MOVE_FX_SLOTS)
        return;
    x = fxslot(m, kind, slot);
    memset(x->name, 0, sizeof x->name), x->mod = -1;
    memset(x->val, 0, sizeof x->val);
    rfmv_seq_dirty(m);
    pop_msg(m, FP_RLOADED, kind, slot, "Too busy");
}
void rfm_move_fx_sync(rfm_move_t *m)
{
    int kind, s, k;
    if (!has_fx(m))
        return;                                         /* (0.5.10: a host with no FX hears none of it) */
    for (kind = 0; kind < 2; kind++)
        for (s = 0; s < RFM_MOVE_FX_SLOTS; s++) {
            const rfm_move_fxslot_t *x = fxslot(m, kind, s);
            act_fx_load(m, kind, s);
            for (k = 0; k < nparams(module(m, kind, x->mod)); k++)
                act_fx_param(m, kind, s, k);
        }
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
        act_mix(m, s);
    act_route(m);
}
/* a project was loaded or cleared: its modules found in the host's lists, and the host told (the browser on) */
void rfmv_fx_loaded(rfm_move_t *m)
{
    int s;
    for (s = 0; s < RFM_MOVE_FX_SLOTS; s++)
        rfmv_fx_resolve(m, &m->seq.fx.mfx[s], RFM_MOVE_FX_MIDI), rfmv_fx_resolve(m, &m->seq.fx.afx[s], RFM_MOVE_FX_AUDIO);
    if (m->fw.host)
        rfm_move_fx_sync(m);
}

/* ================================================================ the library blob === */
/*  0 "RMVL"  4 version (1)  5 the active slot 0..2  6 the three levels 0..127  9 records u16  11 favourites u16
 * 13..15 zero  16 the three slots' ids (32 bytes each, NUL padded; "" empty)  112 the nine user tags' names (16 bytes
 * each, NUL padded; "" not made)  256 records x { id (32, NUL padded), colour 0..16, tagged u16, set u16 } (bit s - 1:
 * step s, 3..16 only)  then the favourites in order: u16 record indexes (each once)  FNV-1a (32 bit) of all before */
#define LIB_HDR 256
#define LIB_REC (RFM_MOVE_FW_ID_MAX + 5)
static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static void w16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v & 0xFF), p[1] = (uint8_t)(v >> 8 & 0xFF); }
static unsigned r16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static int str_ok(const uint8_t *p, int cap, int empty_ok)   /* NUL-terminated printable ASCII, padded with NULs */
{
    int i, end = -1;
    for (i = 0; i < cap; i++) {
        if (end < 0 && !p[i])
            end = i;
        else if (end < 0 && (p[i] < 32 || p[i] > 126))
            return 0;
        else if (end >= 0 && p[i])
            return 0;
    }
    return end > 0 || (end == 0 && empty_ok);
}

uint32_t rfm_move_fw_library_save(rfm_move_t *m, uint8_t *out, uint32_t cap)
{
    const rfm_move_fw_state_t *f = &m->fw;
    int16_t map[RFM_MOVE_FW_LIB_MAX];
    uint8_t *p;
    int r, k, nr = 0;
    uint32_t need;
    for (r = 0; r < f->nrec; r++)
        map[r] = (int16_t)(rec_used(m, r) ? nr++ : -1);
    need = LIB_HDR + (uint32_t)nr * LIB_REC + (uint32_t)f->nfav * 2u + 4u;
    if (!out || cap < need)
        return 0;
    memset(out, 0, LIB_HDR);
    memcpy(out, "RMVL", 4);
    out[4] = RFM_MOVE_FW_LIB_VERSION, out[5] = f->active;
    out[6] = f->level[0], out[7] = f->level[1], out[8] = f->level[2];
    w16(out + 9, (unsigned)nr), w16(out + 11, f->nfav);
    for (k = 0; k < RFM_MOVE_FW_SLOTS; k++)
        memcpy(out + 16 + k * RFM_MOVE_FW_ID_MAX, f->slot_id[k], strlen(f->slot_id[k]));
    for (k = 0; k < RFM_MOVE_FW_TAGS; k++)
        memcpy(out + 112 + k * RFM_MOVE_FW_TAG_NAME, f->tag[k], strlen(f->tag[k]));
    p = out + LIB_HDR;
    for (r = 0; r < f->nrec; r++) {
        if (map[r] < 0)
            continue;
        memset(p, 0, LIB_REC);
        memcpy(p, f->rec[r].id, strlen(f->rec[r].id));
        p[RFM_MOVE_FW_ID_MAX] = f->rec[r].color;
        w16(p + RFM_MOVE_FW_ID_MAX + 1, f->rec[r].tag_on), w16(p + RFM_MOVE_FW_ID_MAX + 3, f->rec[r].tag_set);
        p += LIB_REC;
    }
    for (k = 0; k < f->nfav; k++, p += 2)
        w16(p, (unsigned)map[f->fav[k]]);
    {
        const uint32_t h = fnv(out, (uint32_t)(p - out));
        w16(p, h & 0xFFFF), w16(p + 2, h >> 16);
    }
    return (uint32_t)(p - out) + 4;
}

int rfm_move_fw_library(rfm_move_t *m, const uint8_t *b, uint32_t n)
{
    rfm_move_fw_state_t *f = F(m);
    unsigned nr, nf, k, j;
    const uint8_t *p;
    f->nrec = f->nfav = 0;
    memset(f->tag, 0, sizeof f->tag);
    f->lib_dirty = 0;
    if (!b || !n)
        return 0;
    if (n < LIB_HDR + 4 || memcmp(b, "RMVL", 4) || b[4] != RFM_MOVE_FW_LIB_VERSION || b[5] > 2 || b[6] > 127 ||
        b[7] > 127 || b[8] > 127 || b[13] || b[14] || b[15])
        return -1;
    nr = r16(b + 9), nf = r16(b + 11);
    if (nr > RFM_MOVE_FW_LIB_MAX || nf > nr || n != LIB_HDR + nr * LIB_REC + nf * 2 + 4 ||
        (r16(b + n - 4) | (uint32_t)r16(b + n - 2) << 16) != fnv(b, n - 4))
        return -1;
    for (k = 0; k < RFM_MOVE_FW_SLOTS; k++)
        if (!str_ok(b + 16 + k * RFM_MOVE_FW_ID_MAX, RFM_MOVE_FW_ID_MAX, 1))
            return -1;
    for (k = 0; k < RFM_MOVE_FW_TAGS; k++)
        if (!str_ok(b + 112 + k * RFM_MOVE_FW_TAG_NAME, RFM_MOVE_FW_TAG_NAME, 1))
            return -1;
    for (k = 0, p = b + LIB_HDR; k < nr; k++, p += LIB_REC) {
        if (!str_ok(p, RFM_MOVE_FW_ID_MAX, 0) || p[RFM_MOVE_FW_ID_MAX] > RFM_MOVE_PROJ_COLORS ||
            (r16(p + RFM_MOVE_FW_ID_MAX + 1) & 3) || (r16(p + RFM_MOVE_FW_ID_MAX + 3) & 3) ||
            (r16(p + RFM_MOVE_FW_ID_MAX + 1) & ~r16(p + RFM_MOVE_FW_ID_MAX + 3)))
            return -1;
        for (j = 0; j < k; j++)                         /* (an id once) */
            if (!memcmp(p, b + LIB_HDR + j * LIB_REC, RFM_MOVE_FW_ID_MAX))
                return -1;
    }
    for (k = 0; k < nf; k++) {
        const unsigned v = r16(p + k * 2);
        if (v >= nr)
            return -1;
        for (j = 0; j < k; j++)
            if (r16(p + j * 2) == v)
                return -1;
    }
    /* all checked: take it */
    f->active = b[5], f->level[0] = b[6], f->level[1] = b[7], f->level[2] = b[8];
    for (k = 0; k < RFM_MOVE_FW_SLOTS; k++)
        memcpy(f->slot_id[k], b + 16 + k * RFM_MOVE_FW_ID_MAX, RFM_MOVE_FW_ID_MAX);
    if (f->active >= nslots(m))                         /* (0.5.10: one slot: slot 0, the others empty) */
        f->active = 0;
    for (k = nslots(m); k < RFM_MOVE_FW_SLOTS; k++)
        f->slot_id[k][0] = 0;
    rfmv_slot_changed(m);                               /* (0.8.0: the slot played: its own layout) */
    for (k = 0; k < RFM_MOVE_FW_TAGS; k++)
        memcpy(f->tag[k], b + 112 + k * RFM_MOVE_FW_TAG_NAME, RFM_MOVE_FW_TAG_NAME);
    for (k = 0, p = b + LIB_HDR; k < nr; k++, p += LIB_REC) {
        rfm_move_lib_rec_t *c = &f->rec[k];
        memcpy(c->id, p, RFM_MOVE_FW_ID_MAX);
        c->color = p[RFM_MOVE_FW_ID_MAX];
        c->tag_on = (uint16_t)r16(p + RFM_MOVE_FW_ID_MAX + 1), c->tag_set = (uint16_t)r16(p + RFM_MOVE_FW_ID_MAX + 3);
    }
    f->nrec = (uint16_t)nr;
    if (f->tag[RND_STEP - 8][0]) {                      /* (0.14.0) step 13 is Random now: its tag moves to a free step
                                                         * 8..15, else it goes (as step 16's below) */
        int to = -1, jj;
        for (jj = 0; jj < 8 && to < 0; jj++)
            if (!f->tag[jj][0] && jj != RND_STEP - 8)
                to = jj;
        if (to >= 0)
            memcpy(f->tag[to], f->tag[RND_STEP - 8], RFM_MOVE_FW_TAG_NAME);
        for (jj = 0; jj < (int)nr; jj++) {
            rfm_move_lib_rec_t *rc = &f->rec[jj];
            const uint16_t bf = (uint16_t)(1u << (RND_STEP - 1));
            if (to >= 0) {
                const uint16_t bt = (uint16_t)(1u << (to + 7));
                rc->tag_on = (uint16_t)((rc->tag_on & ~bt) | (rc->tag_on & bf ? bt : 0));
                rc->tag_set = (uint16_t)((rc->tag_set & ~bt) | (rc->tag_set & bf ? bt : 0));
            }
            rc->tag_on &= (uint16_t)~bf, rc->tag_set &= (uint16_t)~bf;
        }
        memset(f->tag[RND_STEP - 8], 0, RFM_MOVE_FW_TAG_NAME);
    }
    if (f->tag[8][0]) {                                 /* (0.5.4) step 16 is Fantasy now: a ninth user tag moves to a
                                                         * free step 8..15, else it goes */
        int to = -1, jj;
        for (jj = 0; jj < 8 && to < 0; jj++)
            if (!f->tag[jj][0] && jj != RND_STEP - 8)  /* (0.14.0: not step 13) */
                to = jj;
        if (to >= 0)
            memcpy(f->tag[to], f->tag[8], RFM_MOVE_FW_TAG_NAME);
        for (jj = 0; jj < (int)nr; jj++) {
            rfm_move_lib_rec_t *rc = &f->rec[jj];
            if (to >= 0) {
                const uint16_t bt = (uint16_t)(1u << (to + 7));
                rc->tag_on = (uint16_t)((rc->tag_on & ~bt) | (rc->tag_on >> 15 & 1 ? bt : 0));
                rc->tag_set = (uint16_t)((rc->tag_set & ~bt) | (rc->tag_set >> 15 & 1 ? bt : 0));
            }
        }
        memset(f->tag[8], 0, RFM_MOVE_FW_TAG_NAME);
    }
    for (k = 0; k < nr; k++)
        f->rec[k].tag_on &= 0x7FFF, f->rec[k].tag_set &= 0x7FFF;
    for (k = 0; k < nf; k++)
        f->fav[k] = (uint16_t)r16(p + k * 2);
    f->nfav = (uint16_t)nf;
    resolve(m);
    return 0;
}

void rfm_move_fw_flush(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = F(m);
    rfm_move_act_t a;
    uint32_t n;
    if (!f->lib_dirty)
        return;
    f->lib_dirty = 0;
    n = rfm_move_fw_library_save(m, f->blob, sizeof f->blob);
    if (!n)
        return;
    memset(&a, 0, sizeof a);
    a.kind = RFM_MOVE_ACT_FW_LIBRARY, a.data = f->blob, a.len = n;
    send(m, &a);
}
void rfmv_fw_poll(rfm_move_t *m)
{
    if (m->fw.lib_dirty && rfmv_now_ms(m) - m->fw.lib_ms >= RFM_MOVE_FW_LIB_SAVE_MS)
        rfm_move_fw_flush(m);
}

/* ================================================================ FX setup in the project blob (version 5, 6) === */
/* 3 MIDI FX x { module name (24, NUL padded; "" empty), nv values, filter u16 }, 3 audio FX x { name, nv values },
 * 3 firmware filters u16, 3 pans, 3 x 3 sends. nv: 8 (version 5), RFM_MOVE_FX_PARAMS (version 6: by parameter
 * index; 0..127, or RFM_MOVE_FX_UNSET) */
void rfmv_fxset_write(const rfm_move_fxset_t *x, uint8_t *p)
{
    const int nv = RFM_MOVE_FX_PARAMS;
    int k;
    for (k = 0; k < 3; k++, p += RFM_MOVE_FX_NAME + nv + 2) {
        memset(p, 0, RFM_MOVE_FX_NAME);
        memcpy(p, x->mfx[k].name, strlen(x->mfx[k].name));
        memcpy(p + RFM_MOVE_FX_NAME, x->mfx[k].val, (size_t)nv);
        w16(p + RFM_MOVE_FX_NAME + nv, x->mfx[k].filt);
    }
    for (k = 0; k < 3; k++, p += RFM_MOVE_FX_NAME + nv) {
        memset(p, 0, RFM_MOVE_FX_NAME);
        memcpy(p, x->afx[k].name, strlen(x->afx[k].name));
        memcpy(p + RFM_MOVE_FX_NAME, x->afx[k].val, (size_t)nv);
    }
    for (k = 0; k < 3; k++, p += 2)
        w16(p, x->ffilt[k]);
    for (k = 0; k < 3; k++)
        *p++ = x->pan[k];
    for (k = 0; k < 9; k++)
        *p++ = x->send[k / 3][k % 3];
}
int rfmv_fxset_check(const uint8_t *p, int nv)
{
    int k, i;
    for (k = 0; k < 6; k++) {
        const int sz = RFM_MOVE_FX_NAME + nv + (k < 3 ? 2 : 0);
        if (!str_ok(p, RFM_MOVE_FX_NAME, 1))
            return -1;
        for (i = 0; i < nv; i++)
            if (p[RFM_MOVE_FX_NAME + i] > 127 && (nv == RFM_MOVE_FX_PARAMS_V5 || p[RFM_MOVE_FX_NAME + i] != RFM_MOVE_FX_UNSET))
                return -1;
        if (k < 3) {                                    /* (a MIDI FX filter: one track, or all) */
            const unsigned f = r16(p + RFM_MOVE_FX_NAME + nv);
            if (f != 0xFFFF && (!f || (f & (f - 1))))
                return -1;
        }
        p += sz;
    }
    p += 6;
    for (k = 0; k < 12; k++)
        if (p[k] > 127)
            return -1;
    return 0;
}
void rfmv_fxset_read(rfm_move_fxset_t *x, const uint8_t *p, int nv)
{
    int k;
    for (k = 0; k < 3; k++, p += RFM_MOVE_FX_NAME + nv + 2) {
        memcpy(x->mfx[k].name, p, RFM_MOVE_FX_NAME), x->mfx[k].name[RFM_MOVE_FX_NAME - 1] = 0;
        memset(x->mfx[k].val, RFM_MOVE_FX_UNSET, sizeof x->mfx[k].val);   /* (version 5: past the 8, unset) */
        memcpy(x->mfx[k].val, p + RFM_MOVE_FX_NAME, (size_t)nv);
        x->mfx[k].filt = (uint16_t)r16(p + RFM_MOVE_FX_NAME + nv), x->mfx[k].mod = -1;
    }
    for (k = 0; k < 3; k++, p += RFM_MOVE_FX_NAME + nv) {
        memcpy(x->afx[k].name, p, RFM_MOVE_FX_NAME), x->afx[k].name[RFM_MOVE_FX_NAME - 1] = 0;
        memset(x->afx[k].val, RFM_MOVE_FX_UNSET, sizeof x->afx[k].val);
        memcpy(x->afx[k].val, p + RFM_MOVE_FX_NAME, (size_t)nv);
        x->afx[k].filt = 0, x->afx[k].mod = -1;
    }
    for (k = 0; k < 3; k++, p += 2)
        x->ffilt[k] = (uint16_t)r16(p);
    for (k = 0; k < 3; k++)
        x->pan[k] = *p++;
    for (k = 0; k < 9; k++)
        x->send[k / 3][k % 3] = *p++;
}

/* ================================================================ the view's controls === */
static int slot_loaded(const rfm_move_t *m, int s) { return s >= 0 && s < 3 && m->fw.slot_id[s][0]; }
static void slot_name(const rfm_move_t *m, int s, char *o, int cap)
{
    if (m->fw.slot_fw[s] >= 0)
        asc(ename(m, m->fw.slot_fw[s]), o, cap);
    else
        asc(m->fw.slot_id[s], o, cap);
}
static int desc_rows(const rfm_move_t *m, int fw, char out[][LW], int skip, int cap)
{
    const rfm_move_fw_t *e = ent(m, fw);
    const char *t = e && e->desc ? e->desc : "";
    char d[2048];
    int n = 0;
    for (; *t && n < (int)sizeof d - 1; t++)            /* (printable ASCII; a line break is a space) */
        if (*t == '\n')
            d[n++] = ' ';
        else if ((unsigned char)*t >= 32 && (unsigned char)*t < 127)
            d[n++] = *t;
    d[n] = 0;
    if (!n)
        snprintf(d, sizeof d, "No description in the catalog.");
    return wrap(d, 20, out, skip, cap);
}
static int max_jog(const rfm_move_t *m, int fw)
{
    const int n = desc_rows(m, fw, NULL, 0, 0);
    return 1 + (n > INFO_ROWS ? n - INFO_ROWS : 0);
}
static int fx_page_ok(const rfm_move_t *m, int p)
{
    return p == PG_ROUTE || p == PG_MIX || (p == PG_MIDI && m->fw.nmods[RFM_MOVE_FX_MIDI]) ||
           (p == PG_AUDIO && m->fw.nmods[RFM_MOVE_FX_AUDIO]);
}
static int fx_page(const rfm_move_t *m) { return fx_page_ok(m, m->s.fx_page) ? m->s.fx_page : PG_ROUTE; }

void rfmv_fw_enter(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = F(m);
    f->track = (uint8_t)(f->host ? (m->s.fw_track == 3 ? 3 : f->active) : 3);
    f->catalog = 0, f->sel = -1, f->jog = 0, f->capture = 0, f->cap_step = -1, f->states = 0, f->smods = 0;
    f->fx = f->host && has_fx(m) && m->s.fw_fx, f->browse = -1;     /* (0.4.5: FX setup again if it was left showing) */
    f->all_auto = !f->nfav;                             /* (no favourites: All) */
    f->hold_step = 0;
    pop_clear(m);
    if (f->track < 3 && m->pop == POP_MODE)
        m->pop = POP_NONE;                              /* (a firmware track: its own screen at once) */
}
static void leave(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = F(m);
    f->states = 0, f->capture = 0, f->smods = 0, f->browse = -1;   /* (fx: proj_leave remembers it, then clears) */
    pop_clear(m);
    rfm_move_fw_flush(m);
    rfmv_proj_back(m);
}
/* (0.8.0) the slot the controls play: what is held let go first (to the slot it was played on), FW_ACTIVE, the
 * library; then its own layout (move.c) */
static void activate(rfm_move_t *m, int t)
{
    rfm_move_fw_state_t *f = F(m);
    if (t == f->active || t < 0 || t >= RFM_MOVE_FW_SLOTS)
        return;
    rfmv_slot_leaving(m);
    f->active = (uint8_t)t;
    act_fw(m, RFM_MOVE_ACT_FW_ACTIVE, t, f->slot_fw[t], 0);
    lib_changed(m);
    rfmv_slot_changed(m);
}
static void show_track(rfm_move_t *m, int t)
{
    rfm_move_fw_state_t *f = F(m);
    if (t < 3 && t >= nslots(m))
        return;                                         /* (0.5.10: one slot: Tracks 2, 3 do nothing) */
    f->track = (uint8_t)t;
    if (t < 3)
        activate(m, t);                                 /* (the firmware the controls play now) */
    if (m->s.fw_track != t) {
        m->s.fw_track = (uint8_t)t;
        rfmv_changed(m);
    }
    f->sel = -1, f->jog = 0, f->capture = 0, f->cap_step = -1, f->catalog = 0, f->states = 0, f->smods = 0;
    f->all_auto = !f->nfav;
    pop(m, FP_TRACK, t, 0);
}
static void set_level(rfm_move_t *m, int t, int d)
{
    rfm_move_fw_state_t *f = F(m);
    const int v = clampi(f->level[t] + d, 0, 127);
    pop(m, FP_LEVEL, t, 0);
    if (v == f->level[t])
        return;
    f->level[t] = (uint8_t)v;
    act_fw(m, RFM_MOVE_ACT_FW_LEVEL, t, f->slot_fw[t], v);
    lib_changed(m);
}
/* (0.12.0) a slot's level from elsewhere (session view, a select step held + volume): 1 done (its popup up) */
int rfmv_fw_level_turn(rfm_move_t *m, int slot, int d)
{
    if (slot < 0) {                                     /* (-1: the level popup goes) */
        if (pop_up(m, FP_LEVEL))
            pop_clear(m);
        return 0;
    }
    if (!m->fw.host || slot >= nslots(m) || !slot_loaded(m, slot))
        return 0;
    set_level(m, slot, d);
    return 1;
}
/* (0.13.0) a slot's level popup is up (the owner's, 2026-11-22: "Holding a track button and turning the volume knob
 * should change that firmware's volume level and also visually update to communicate the change": the popup goes over
 * the held Track's menu in every layout, not only in the firmware browser) */
int rfmv_fw_level_shown(const rfm_move_t *m) { return pop_up(m, FP_LEVEL); }
/* the volume knob with Track 1..3 held (the browser on, any mode): that slot's level. 1 taken */
int rfmv_fw_volume(rfm_move_t *m, int d)
{
    int t;
    if (!m->fw.host)
        return rfmv_track_held(m, 3) && rfmv_kit_volume(m, d);
    for (t = 0; t < nslots(m); t++)
        if (rfmv_track_held(m, t) && slot_loaded(m, t)) {
            set_level(m, t, d);
            return 1;
        }
    /* (2026-11-26, the owner's: "Holding track 4 button and turning volume knob should change the drum kit level")
     * Track 4 held: the kit's level, as a firmware Track's slot level, wherever Track 4's views include the kit; its
     * popup while the knob is touched (kit.c) */
    return rfmv_track_held(m, 3) && rfmv_kit_volume(m, d);
}
static void after_filter(rfm_move_t *m, int step)
{
    rfm_move_fw_state_t *f = F(m);
    f->bank[bank_key(m)] = 0;
    if (f->sel >= 0 && !in_list(m, f->sel))
        f->sel = -1;
    f->jog = 0;
    rfmv_changed(m);                                    /* (the filter is a setting) */
    pop(m, FP_FILTER, step, 0);
}
/* (0.14.0, the owner's 2026-11-24: "a feature on step 13 with the icon on when in the firmware browser to randomly
 * select a firmware. This should work with your current filter, and also work if you are in the full catalog") step
 * 13: a firmware of the bank shown (the list as it is: Favorites / All, the filter, the catalog), never the one
 * selected; it only selects (loading or downloading stays the jog's / the double tap's), named and said as a pad's
 * selection; the pads roll from the old selection to it (RFMV_FWR_MS). One or none there: "Nothing to pick" */
static int rnd_ok(const rfm_move_t *m) { return list_len(m) >= 2; }   /* (two or more in the list) */
/* (0.14.0, the coordinator's follow-up: the whole list, not the bank shown) a firmware of the whole list as it is
 * (Favorites / All, the filter, the catalog: every bank), never the one selected; the bank it is in shown, the pads
 * rolling to it there (from the old selection's pad when that was in the same bank, else from the first) */
static void fw_random(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = F(m);
    const int n = list_len(m), oldb = cur_bank(m);
    int k, id, pos = -1, r;
    for (k = 0; k < n && pos < 0 && f->sel >= 0; k++)
        if (walk(m, k, 1, &id), id == f->sel)
            pos = k;
    if (n - (pos >= 0) < 1 || n < 2) {                  /* (a list of one or none: nothing to choose) */
        pop_msg(m, FP_MSG, -1, 0, "Nothing to pick");
        return;
    }
    r = (int)(rfmv_ui_rnd(m) % (uint32_t)(n - (pos >= 0)));
    if (pos >= 0 && r >= pos)
        r++;
    walk(m, r, 1, &id);
    f->bank[bank_key(m)] = (uint8_t)(r / 32);           /* (the bank it is in shown) */
    f->sel = (int16_t)id, f->jog = 0, pop_clear(m);
    m->fw_tap_cell = 0;
    m->fwr_ms = rfmv_now_ms(m), m->fwr_to = (int8_t)(r % 32);
    m->fwr_from = (int8_t)(pos >= 0 && pos / 32 == r / 32 && oldb == r / 32 ? pos % 32 : 0);
}
static void tag_toggle(rfm_move_t *m, int fw, int s)
{
    rfm_move_fw_state_t *f = F(m);
    const int cur = has_tag(m, fw, s), r = rec_make(m, fw);
    if (r < 0) {
        pop_msg(m, FP_MSG, fw, 0, "Library full");
        return;
    }
    if (s >= 8 && !f->tag[s - 8][0])                    /* (a free step: a new tag, named in the web manager) */
        snprintf(f->tag[s - 8], sizeof f->tag[s - 8], "Tag %d", s - 7);
    f->rec[r].tag_set |= (uint16_t)(1u << (s - 1));
    if (cur)
        f->rec[r].tag_on &= (uint16_t)~(1u << (s - 1));
    else
        f->rec[r].tag_on |= (uint16_t)(1u << (s - 1));
    f->cap_step = (int8_t)s, f->cap_ms = rfmv_now_ms(m);
    lib_changed(m);
}
static void fav_toggle(rfm_move_t *m, int fw)
{
    rfm_move_fw_state_t *f = F(m);
    const int r = rec_make(m, fw), k = fav_pos(m, r);
    if (r < 0) {
        pop_msg(m, FP_MSG, fw, 0, "Library full");
        return;
    }
    if (k >= 0) {
        memmove(&f->fav[k], &f->fav[k + 1], (size_t)(f->nfav - k - 1) * sizeof f->fav[0]);
        f->nfav--;
        if (!f->nfav)
            f->all_auto = 1;                            /* (the last one gone: All) */
    } else
        f->fav[f->nfav++] = (uint16_t)r;
    pop(m, FP_FAV, fw, k < 0);
    lib_changed(m);
}
static void state_act(rfm_move_t *m, int kind, int k)
{
    rfm_move_act_t a;
    const int t = m->fw.track;
    memset(&a, 0, sizeof a);
    a.kind = (uint8_t)kind, a.slot = (uint8_t)t, a.index = (uint8_t)k;
    a.data = (const uint8_t *)m->fw.slot_id[t], a.len = (uint32_t)strlen(m->fw.slot_id[t]);
    send(m, &a);
}
static void state_pad(rfm_move_t *m, int k)
{
    rfm_move_fw_state_t *f = F(m);
    const int t = f->track, used = f->st_used[t] >> k & 1;
    if (f->smods & 2) {                                 /* Delete + a state: gone */
        if (!used)
            return;
        state_act(m, RFM_MOVE_ACT_STATE_DELETE, k);
        f->st_used[t] &= ~(1u << k);
        if (f->st_cur[t] == k)
            f->st_cur[t] = -1;
        pop(m, FP_STATE, k, 2);
    } else if ((f->smods & 1) || !used) {               /* Copy + a slot, or an empty one: saved there */
        state_act(m, RFM_MOVE_ACT_STATE_SAVE, k);
        f->st_used[t] |= 1u << k, f->st_cur[t] = (int8_t)k;
        pop(m, FP_STATE, k, 0);
    } else {
        state_act(m, RFM_MOVE_ACT_STATE_LOAD, k);
        f->st_cur[t] = (int8_t)k;
        pop(m, FP_STATE, k, 1);
    }
}
static void fw_back(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = F(m);
    pop_clear(m);
    if (f->capture)
        f->capture = 0, f->cap_step = -1;
    else if (f->states)
        f->states = 0, f->smods = 0;
    else if (f->jog > 0)
        f->jog = 0;
    else if (f->sel >= 0)
        f->sel = -1;
    else if (f->catalog)
        f->catalog = 0;
    else
        leave(m);
}
/* (0.5.2) the view's name as it is now, a second (the Back flip in, Menu's toggle): FX & Mix (Mixer on its page),
 * Firmwares, Projects (Track 4) */
void rfmv_fw_view_pop(rfm_move_t *m)
{
    const rfm_move_fw_state_t *f = &m->fw;
    rfmv_pop(m, POP_MODE, f->fx ? (fx_page(m) == PG_MIX ? 23 : 22) : f->track < 3 ? 21 : 20);
}
static void open_fx(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = F(m);
    f->fx = 1, f->browse = -1, f->capture = 0, f->states = 0, f->smods = 0, f->fx_pages = 0;
    pop(m, FP_RPAGE, fx_page(m), 0);
}
static void fw_jog_press(rfm_move_t *m)
{
    rfm_move_fw_state_t *f = F(m);
    const int fw = f->sel;
    if (fw < 0 || f->busy_kind || f->states)
        return;
    pop_clear(m);
    if (f->catalog) {
        const int st = status(m, fw);
        if (st == ST_OK)
            goto load;                                  /* (0.4.2: on the Move and current: loaded, as outside the catalog) */
        f->busy_kind = (int8_t)(st == ST_NEW ? BUSY_DOWNLOAD : BUSY_UPDATE), f->busy_fw = (int16_t)fw;
        f->busy_slot = -1, f->busy_pct = 0;
        snprintf(f->busy_id, sizeof f->busy_id, "%s", ent(m, fw)->id ? ent(m, fw)->id : "");
        act_fw(m, RFM_MOVE_ACT_FW_DOWNLOAD, 0, fw, fw);
        return;
    }
load:
    f->busy_kind = BUSY_LOAD, f->busy_fw = (int16_t)fw, f->busy_slot = (int8_t)f->track, f->busy_pct = 0;
    snprintf(f->busy_id, sizeof f->busy_id, "%s", ent(m, fw)->id ? ent(m, fw)->id : "");
    act_fw(m, RFM_MOVE_ACT_FW_LOAD, f->track, fw, fw);
}

/* (0.6.9) favourites reordered: knob 2 in the Favorites list (no filter: every one listed) moves the selected one */
static int fav_order_on(const rfm_move_t *m)
{
    return !m->fw.catalog && !scope(m) && !m->s.fw_filter && !m->fw.states && !m->fw.capture;
}
/* the listed favourites' places in fav[] (installed ones; others kept where they are), n */
static int fav_listed(const rfm_move_t *m, int *at)
{
    int i, n = 0;
    for (i = 0; i < m->fw.nfav; i++) {
        const int fw = m->fw.rec[m->fw.fav[i]].fw;
        if (fw >= 0 && installed(m, fw) && passes(m, fw))
            at[n++] = i;
    }
    return n;
}
static void fav_move(rfm_move_t *m, int fw, int d)
{
    rfm_move_fw_state_t *f = F(m);
    static int at[RFM_MOVE_FW_LIB_MAX];
    const int n = fav_listed(m, at), r = rec_of(m, fw);
    int k, t;
    for (k = 0; k < n && f->fav[at[k]] != r; k++) {}
    if (k == n)
        return;
    t = clampi(k + d, 0, n - 1);
    if (t != k) {
        const int a = at[k], b = at[t];
        const uint16_t e = f->fav[a];
        if (b > a)
            memmove(&f->fav[a], &f->fav[a + 1], (size_t)(b - a) * sizeof f->fav[0]);
        else
            memmove(&f->fav[b + 1], &f->fav[b], (size_t)(a - b) * sizeof f->fav[0]);
        f->fav[b] = e;
        lib_changed(m);
    }
    f->bank[0] = (uint8_t)(t / 32);                     /* (the bank shown follows it) */
    pop(m, FP_ORDER, fw, t + 1);
}
/* (0.6.13) a listed favourite's place 1.., 0 not listed */
static int fav_place(const rfm_move_t *m, int fw)
{
    static int at[RFM_MOVE_FW_LIB_MAX];
    const int n = fav_listed(m, at), r = rec_of(m, fw);
    int k;
    for (k = 0; k < n; k++)
        if (m->fw.fav[at[k]] == r)
            return k + 1;
    return 0;
}
/* (0.6.13) the firmware view's knobs with a job: 1 the selected firmware's colour, 2 its place in Favorites */
static int knob1_job(const rfm_move_t *m) { return m->fw.sel >= 0 && !m->fw.catalog && !m->fw.states; }
static int knob2_job(const rfm_move_t *m) { return m->fw.sel >= 0 && fav_order_on(m) && is_fav(m, m->fw.sel); }
static void fw_input(rfm_move_t *m, const mr_event_t *ev)
{
    rfm_move_fw_state_t *f = F(m);
    const int fw = f->sel, t = f->track;
    switch (ev->type) {
    case MR_EV_PAD: {
        const int p = ev->index, cell = (3 - p / 8) * 8 + p % 8;
        int ids[32];
        if (p < 0 || p >= RFM_MOVE_NPADS)
            return;
        if (!ev->value) {
            m->pad_down[p] = 0;
            return;
        }
        if (m->pad_down[p])
            return;
        m->pad_down[p] = 1;
        if (f->states) {
            state_pad(m, cell);
            return;
        }
        bank_ids(m, ids);
        if (ids[cell] >= 0) {
            const uint32_t now = m->in_ms;              /* (the input's clock, as the Track buttons') */
            const int dbl = m->fw_tap_cell == cell + 1 && f->sel == ids[cell] && (int32_t)(now - m->fw_tap_ms) < RFM_MOVE_DOUBLE_MS;
            f->sel = (int16_t)ids[cell], f->jog = 0, pop_clear(m);
            m->fw_tap_cell = (uint8_t)(cell + 1), m->fw_tap_ms = now;
            if (dbl && (!f->catalog || status(m, ids[cell]) == ST_OK)) {
                m->fw_tap_cell = 0;                     /* (0.9.0, the owner's) a double tap loads it, as the jog's
                                                         * press (the catalog: only one on the Move and current; a
                                                         * download is still the jog's); the browser stays */
                fw_jog_press(m);
            }
        }
        return;
    }
    case MR_EV_STEP: {
        const int s = ev->index + 1;
        if (f->states || s < 1 || s > 16)
            return;
        f->steps_down = (uint16_t)(ev->value ? f->steps_down | 1u << (s - 1) : f->steps_down & ~(1u << (s - 1)));
        if (!ev->value) {                               /* (0.4.2) a filter step let go: a tap toggles, a hold did not */
            if (f->hold_step != s)
                return;
            f->hold_step = 0;
            if (rfmv_now_ms(m) - f->hold_ms >= RFM_MOVE_TAP_MS) {
                pop(m, FP_FILTER, s, 0);                /* (what it is stays up a while; nothing changed) */
                return;
            }
            if (s <= 2) {                               /* (the catalog: everything, Update ready or Not installed) */
                if (f->catalog)
                    m->s.fw_cat_scope = (uint8_t)(m->s.fw_cat_scope == s ? 0 : s);
                else
                    m->s.fw_scope = (uint8_t)(s - 1), f->all_auto = 0;   /* (the user's choice, favourites or not) */
            } else {                                    /* (one category or tag, or the lit one again: all) */
                const uint16_t b = (uint16_t)(1u << (s - 1));
                m->s.fw_filter = m->s.fw_filter == b ? 0 : b;
            }
            after_filter(m, s);
            return;
        }
        if (f->capture && fw >= 0) {
            if (s >= CAT0 && s != VIRT_STEP && s != RND_STEP)   /* (Fantasy is no tag: it shows, nothing toggles;
                                                         * 0.14.0: step 13 is Random, no tag) */
                tag_toggle(m, fw, s);
        } else if (s == RND_STEP)
            fw_random(m);                               /* (0.14.0: at the press) */
        else if (s <= 2 || tag_defined(m, s))
            f->hold_step = (uint8_t)s, f->hold_ms = rfmv_now_ms(m);
        return;
    }
    case MR_EV_BUTTON: {
        const int cc = ev->index, down = ev->value != 0;
        if (cc == MR_CC_CAPTURE) {
            if (down && fw >= 0 && !f->catalog && !f->states)
                f->capture = 1, f->cap_step = -1;
            else if (!down)
                f->capture = 0, f->cap_step = -1;
            return;
        }
        if (cc == MR_CC_COPY || cc == MR_CC_DELETE) {
            const int b = cc == MR_CC_COPY ? 1 : 2;
            if (f->states) {
                f->smods = (uint8_t)(down ? f->smods | b : f->smods & ~b);
                return;
            }
            if (down && cc == MR_CC_DELETE && !f->catalog && slot_loaded(m, t)) {   /* unloaded at once */
                char nm[24];
                slot_name(m, t, nm, sizeof nm);
                act_fw(m, RFM_MOVE_ACT_FW_UNLOAD, t, f->slot_fw[t], 0);
                f->slot_id[t][0] = 0, f->slot_fw[t] = -1, f->st_used[t] = 0, f->st_cur[t] = -1;
                if (f->busy_kind == BUSY_LOAD && f->busy_slot == t)
                    f->busy_kind = BUSY_NONE, f->busy_fw = -1;
                lib_changed(m);
                pop_msg(m, FP_UNLOAD, -1, t, nm);
            }
            return;
        }
        if (!down)
            return;
        switch (cc) {
        case MR_CC_MENU:
            if (f->host && has_fx(m))
                open_fx(m), rfmv_fw_view_pop(m);       /* (0.5.2: named) */
            else
                leave(m);
            return;
        case MR_CC_BACK: fw_back(m); return;
        case MR_CC_SAMPLE:
            if (fw >= 0 && !f->catalog && !f->states)
                fav_toggle(m, fw);
            return;
        case MR_CC_MUTE:
            if (f->states)
                return;
            f->catalog = (uint8_t)!f->catalog, f->sel = -1, f->jog = 0, f->capture = 0;
            if (f->catalog)
                pop(m, FP_CATALOG, 0, 0);
            else
                pop_clear(m);
            return;
        case MR_CC_LOOP:                                /* the firmware's states (a firmware on this track) */
            if (rfmv_pfw_cap(m)) {                      /* (0.13.0) the States layer (Back or Loop: the browser again) */
                if (slot_loaded(m, t) && !f->catalog)
                    rfmv_st_open(m, t, 1);
                pop_clear(m);
                return;
            }
            if (f->states)
                f->states = 0, f->smods = 0;
            else if (slot_loaded(m, t) && !f->catalog)
                f->states = 1, f->smods = 0, f->capture = 0;
            pop_clear(m);
            return;
        case MR_CC_LEFT:
        case MR_CC_RIGHT: {
            const int nb = nbanks(m), b = cur_bank(m) + (cc == MR_CC_LEFT ? -1 : 1);
            if (f->states || b < 0 || b >= nb)
                return;
            f->bank[bank_key(m)] = (uint8_t)b, f->sel = -1, f->jog = 0;
            pop(m, FP_BANK, 0, 0);
            return;
        }
        default: return;
        }
    }
    case MR_EV_KNOB:
        if (ev->index == 0 && fw >= 0 && !f->catalog && !f->states && ev->value) {   /* knob 1: the pad's colour */
            const int a0 = f->list_acc, d0 = ev->value;
            int a = (a0 > 0 && d0 < 0) || (a0 < 0 && d0 > 0) ? 0 : a0, d, r;
            a += d0;
            d = a / RFM_MOVE_LIST_TICKS;
            f->list_acc = (int8_t)(a - d * RFM_MOVE_LIST_TICKS);
            f->jog = 0;
            pop(m, FP_COLOR, fw, 0);
            if (!d)
                return;
            if ((r = rec_make(m, fw)) < 0) {
                pop_msg(m, FP_MSG, fw, 0, "Library full");
                return;
            }
            f->rec[r].color = (uint8_t)(((color(m, fw) + d) % 16 + 16) % 16 + 1);
            lib_changed(m);
        }
        if (ev->index == 1 && fw >= 0 && fav_order_on(m) && is_fav(m, fw) && ev->value) {   /* (0.6.9) knob 2: the
                                                         * selected favourite moved up / down the list, a place a step */
            const int a0 = f->knob_acc[1], d0 = ev->value;
            int a = (a0 > 0 && d0 < 0) || (a0 < 0 && d0 > 0) ? 0 : a0, d;
            a += d0;
            d = a / RFM_MOVE_LIST_TICKS;
            f->knob_acc[1] = (int8_t)(a - d * RFM_MOVE_LIST_TICKS);
            f->jog = 0;
            if (d)
                fav_move(m, fw, d);
        }
        return;
    case MR_EV_TOUCH:
        if (!ev->value && ev->index == 0)
            f->list_acc = 0;
        if (!ev->value && ev->index == 1)
            f->knob_acc[1] = 0;
        if (ev->value && ev->index == 0 && knob1_job(m))   /* (0.6.13) touched: its parameter and value at once */
            f->jog = 0, pop(m, FP_COLOR, fw, 0);
        if (ev->value && ev->index == 1 && knob2_job(m))
            f->jog = 0, pop(m, FP_ORDER, fw, fav_place(m, fw) | 0x1000);   /* (0x1000: a touch; the same popup) */
        return;
    case MR_EV_JOG:
        if (fw >= 0 && ev->value && !f->states) {
            pop_clear(m);
            f->jog = (uint8_t)clampi(f->jog + ev->value, 0, max_jog(m, fw));
        }
        return;
    case MR_EV_JOG_CLICK:
        if (ev->value)
            fw_jog_press(m);
        return;
    default: return;
    }
}

/* ---- FX setup */
static void fx_load(rfm_move_t *m, int kind, int slot, int mod)
{
    rfm_move_fxslot_t *x = fxslot(m, kind, slot);
    const rfm_move_fx_module_t *md = module(m, kind, mod);
    int k;
    memset(x->name, 0, sizeof x->name), memset(x->val, 0, sizeof x->val);
    x->mod = -1;
    if (md && md->name) {
        snprintf(x->name, sizeof x->name, "%s", md->name);
        x->mod = (int16_t)mod;
        for (k = 0; k < nparams(md); k++)
            x->val[k] = param_def(&md->params[k]);
    }
    m->fw.fx_pg[kind][slot] = 0;                        /* (0.5.0: its first page) */
    act_fx_load(m, kind, slot);
    rfmv_seq_dirty(m);
}
/* (0.5.0) a slot's module, its page now (clamped to its pages), knob k's parameter there (-1 a gap or none) */
static const rfm_move_fx_module_t *slot_module(const rfm_move_t *m, int kind, int slot)
{
    const rfm_move_fxslot_t *sl = kind == RFM_MOVE_FX_MIDI ? &m->seq.fx.mfx[slot] : &m->seq.fx.afx[slot];
    return module(m, kind, sl->mod);
}
static int slot_page(const rfm_move_t *m, int kind, int slot)
{
    const int n = npages(slot_module(m, kind, slot));
    return n ? clampi(m->fw.fx_pg[kind][slot], 0, n - 1) : 0;
}
static int knob_param(const rfm_move_t *m, int kind, int slot, int k)
{
    return page_param(slot_module(m, kind, slot), slot_page(m, kind, slot), k);
}
/* (0.5.0) the focused module's pages on screen (Right from the chain; the module list over them) */
static int pages_shown(const rfm_move_t *m)
{
    const int pg = fx_page(m), kind = pg == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO;
    return m->fw.fx_pages && (pg == PG_MIDI || pg == PG_AUDIO) && npages(slot_module(m, kind, m->fw.fx_focus)) > 0;
}
/* (0.4.5) a knob with a parameter on this FX page (the Mixer's Level, Pan, sends; a selected module's parameters) */
static int fx_knob_param(const rfm_move_t *m, int k)
{
    const int pg = fx_page(m), kind = pg == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO;
    if (m->fw.browse >= 0 || k < 0 || k >= RFM_MOVE_NKNOBS)
        return 0;
    if (pg == PG_MIX)
        return k < RFM_MOVE_FW_SLOTS || (k == RFM_MOVE_KIT_SLOT && rfmv_kit_cap(m));   /* (3b: knob 4 the kit's) */
    if (pg == PG_ROUTE)
        return k < RFM_MOVE_MIXP_N;
    if (pg == PG_MIDI || pg == PG_AUDIO)
        return knob_param(m, kind, m->fw.fx_focus, k) >= 0;
    return 0;
}
/* .. the knob still touched (its popup up by a touch) */
static int touching(const rfm_move_t *m, int k) { return m->fw.pop == FP_PARAM && m->fw.pop_a == k && m->fw.pop_b; }
/* (0.5.0) one FX layer: pad column 1 the MIDI FX slots, column 3 the firmwares (routing, the mix knobs), column 5 the
 * audio FX slots, rows 1..3 (top down); settings.fx_page says which column is selected (MIDI, ROUTE, AUDIO). The
 * Mixer (MIX) its own: columns 1..3 the slots' meters, the right 4 x 4 what knobs 1..3 set */
static const int8_t MIXPAD[4][4] = { { RFM_MOVE_MIXP_LEVEL, RFM_MOVE_MIXP_PAN, -1, -1 },
                                     { RFM_MOVE_MIXP_SEND1, RFM_MOVE_MIXP_SEND2, RFM_MOVE_MIXP_SEND3, -1 },
                                     { -1, -1, -1, -1 }, { -1, -1, -1, -1 } };
static int col_page(int col) { return col == 0 ? PG_MIDI : col == 2 ? PG_ROUTE : col == 4 ? PG_AUDIO : -1; }
static int page_col(int pg) { return pg == PG_MIDI ? 0 : pg == PG_ROUTE ? 2 : 4; }
static int slot_pad(int pg, int row) { return (3 - row) * 8 + page_col(pg); }   /* (the pad index) */
static void set_pg(rfm_move_t *m, int p)
{
    if (p != PG_MIX)
        m->fx_route = (uint8_t)p;                       /* (0.8.0: Routing's column, for Track 4's flip) */
    if (m->s.fx_page != p)
        m->s.fx_page = (uint8_t)p, rfmv_changed(m);
}
/* (stage 3b) the kit in FX setup: Routing's pad column 4, top row; the Mixer's knob 4 and pad column 4 */
#define KIT_COL 3
static const rgb_t KITRGB = { 255, 60, 160 };
static int rkit(const rfm_move_t *m) { return m->fw.rt_kit && rfmv_kit_cap(m) && fx_page(m) == PG_ROUTE; }
static uint16_t route_filt(const rfm_move_t *m)        /* Routing's tracks: the chosen slot's (3b: or the kit's) */
{
    return rkit(m) ? m->kit.route : m->seq.fx.ffilt[m->fw.fx_focus];
}
static uint8_t *mix_val(rfm_move_t *m, int slot, int par)   /* (Level: the library's; NULL) */
{
    return par == RFM_MOVE_MIXP_PAN ? &m->seq.fx.pan[slot] : par >= RFM_MOVE_MIXP_SEND1 ? &m->seq.fx.send[slot][par - 2] : NULL;
}
static int mix_get(const rfm_move_t *m, int slot, int par)
{
    if (slot == RFM_MOVE_KIT_SLOT)                      /* (stage 3b) the kit's own strip */
        return rfm_move_kit_mix(m, par);
    return par == RFM_MOVE_MIXP_LEVEL ? m->fw.level[slot] : mix_val((rfm_move_t *)m, slot, par)[0];
}
static void mix_turn(rfm_move_t *m, int slot, int par, int d)
{
    uint8_t *v;
    int nv;
    if (slot == RFM_MOVE_KIT_SLOT) {
        rfmv_kit_mix_turn(m, par, d);
        return;
    }
    v = mix_val(m, slot, par);
    if (!v) {
        set_level(m, slot, d);
        return;
    }
    nv = clampi(*v + d, 0, 127);
    if (nv != *v) {
        *v = (uint8_t)nv;
        act_mix(m, slot);
        rfmv_seq_dirty(m);
    }
}
/* (0.6.6) a MIDI route toggled (bit 3: Out); a cycle refused ("Loop"): the popup says which */
static void mroute_toggle(rfm_move_t *m, int src, int bit)
{
    rfm_move_fxset_t *x = &m->seq.fx;
    const int on = !(x->mroute[src] >> bit & 1);
    if (on && bit < 3 && mroute_reaches(x->mroute, bit, src)) {
        pop(m, FP_MROUTE, src, bit | 1 << 5);
        return;
    }
    x->mroute[src] = (uint8_t)(x->mroute[src] ^ 1u << bit);
    pop(m, FP_MROUTE, src, bit | on << 4);
    act_route(m);
    rfmv_seq_dirty(m);
}
static void fx_input(rfm_move_t *m, const mr_event_t *ev)
{
    rfm_move_fw_state_t *f = F(m);
    const int pg = fx_page(m), i = f->fx_focus, kind = pg == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO;
    const int slotpg = pg == PG_MIDI || pg == PG_AUDIO;
    rfm_move_fxset_t *x = &m->seq.fx;
    switch (ev->type) {
    case MR_EV_PAD: {
        const int p = ev->index, cell = (3 - p / 8) * 8 + p % 8, col = cell % 8, row = cell / 8;
        int np;
        if (p < 0 || p >= RFM_MOVE_NPADS)
            return;
        if (!ev->value) {
            m->pad_down[p] = 0;
            if (slotpg && p == slot_pad(pg, i) && f->browse >= 0) {   /* held + jog: the module shown, loaded */
                const int mod = f->browse - 1;
                f->browse = -1;
                if (mod != fxslot(m, kind, i)->mod)
                    fx_load(m, kind, i, mod), pop(m, FP_RLOADED, kind, i);
            }
            return;
        }
        m->pad_down[p] = 1;
        if (pg == PG_ROUTE && col == 2) {               /* (0.6.6) a firmware's pad held + another firmware's (or the
                                                         * Out pad under them): its MIDI routed there, or no longer */
            const int src = i, held = src != row && m->pad_down[slot_pad(PG_ROUTE, src)];
            if (held && row <= 3 && (row == 3 || row < nslots(m))) {
                mroute_toggle(m, src, row == 3 ? 3 : row);
                return;
            }
            if (row == 3)
                return;
        }
        if (pg == PG_MIX) {                             /* the Mixer: the right 4 x 4 picks what knobs 1..3 set */
            if (col >= 4 && MIXPAD[row][col - 4] >= 0)
                f->mix_par = (uint8_t)MIXPAD[row][col - 4], pop_clear(m);
            return;
        }
        if (col == KIT_COL && row == 0 && rfmv_kit_cap(m) && fx_page_ok(m, PG_ROUTE)) {   /* (stage 3b) the kit: its
                                                         * strip on knobs 1..5, its tracks on the steps */
            if (pg != PG_ROUTE)
                f->fx_pages = 0;
            set_pg(m, PG_ROUTE);
            f->rt_kit = 1, f->browse = -1;
            memset(f->knob_acc, 0, sizeof f->knob_acc);
            pop_clear(m);
            return;
        }
        if (row > 2 || (np = col_page(col)) < 0 || !fx_page_ok(m, np))
            return;
        f->rt_kit = 0;
        if (np != pg || row != i)
            f->fx_pages = 0;
        set_pg(m, np);
        f->fx_focus = (uint8_t)row, f->browse = -1;
        memset(f->knob_acc, 0, sizeof f->knob_acc);
        pop_clear(m);
        if (f->fx_del && np != PG_ROUTE) {             /* Delete + pad: emptied */
            const int k2 = np == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO;
            if (fxslot(m, k2, row)->mod >= 0 || fxslot(m, k2, row)->name[0])
                fx_load(m, k2, row, -1), pop(m, FP_RLOADED, k2, row);
            f->fx_pages = 0;
        }
        return;
    }
    case MR_EV_STEP: {
        const int s = ev->index + 1;
        const uint16_t b = (uint16_t)(1u << (s - 1));
        if (!ev->value)
            return;
        if (pg == PG_MIDI) {                            /* one track, or (the same step again) all */
            x->mfx[i].filt = x->mfx[i].filt == b ? 0xFFFF : b;
            pop(m, FP_RFILTER, s, 1);
        } else if (pg == PG_ROUTE && rkit(m)) {        /* (stage 3b) the kit: tracks 13..16 to it or not */
            if (s - 1 < RFM_MOVE_KIT_TRACK0)
                return;
            pop(m, FP_RFILTER, s, rfmv_kit_route_set(m, s - 1, !rfmv_kit_routed(m, s - 1)));
        } else if (pg == PG_ROUTE) {
            x->ffilt[i] ^= b;
            pop(m, FP_RFILTER, s, (x->ffilt[i] & b) != 0);
        } else
            return;
        act_route(m);
        rfmv_seq_dirty(m);
        return;
    }
    case MR_EV_BUTTON: {
        const int cc = ev->index;
        if (cc == MR_CC_DELETE)
            f->fx_del = ev->value != 0;
        if (!ev->value)
            return;
        if (cc == MR_CC_MENU)                           /* (0.4.5: Menu alone toggles FX setup; 0.5.2: named) */
            f->fx = 0, f->browse = -1, pop_clear(m), rfmv_fw_view_pop(m);
        else if (cc == MR_CC_BACK && f->browse >= 0)
            f->browse = -1;                             /* (the module list: closed, nothing loaded) */
        else if (cc == MR_CC_BACK && pages_shown(m))
            f->fx_pages = 0, pop_clear(m);              /* (0.5.0: the module's pages are a layer) */
        else if (cc == MR_CC_BACK)                      /* (0.4.5) Back: out of the view, FX setup remembered */
            leave(m);
        return;
    }
    case MR_EV_JOG:
        if (!ev->value)
            return;
        if (pages_shown(m)) {                           /* (0.5.0) the module's pages */
            const int n = npages(slot_module(m, kind, i)), cur = slot_page(m, kind, i),
                      nx = clampi(cur + (ev->value > 0 ? 1 : -1), 0, n - 1);
            if (nx != cur) {
                f->fx_pg[kind][i] = (uint8_t)nx;
                pop_clear(m);
                memset(f->knob_acc, 0, sizeof f->knob_acc);
            }
        } else if (slotpg && m->pad_down[slot_pad(pg, i)]) {   /* the slot's pad held: its module list */
            if (f->browse < 0)
                f->browse = (int8_t)(fxslot(m, kind, i)->mod + 1);
            f->browse = (int8_t)clampi(f->browse + ev->value, 0, f->nmods[kind]);
            pop_clear(m);
        }
        return;
    case MR_EV_JOG_CLICK:                               /* (0.5.0) the selected module's own pages */
        if (ev->value && slotpg && f->browse < 0 && !pages_shown(m) && npages(slot_module(m, kind, i)) > 0) {
            f->fx_pages = 1;
            pop_clear(m);
            memset(f->knob_acc, 0, sizeof f->knob_acc);
        }
        return;
    case MR_EV_KNOB: {
        const int k = ev->index, d = ev->value, tch = touching(m, k);
        if (k < 0 || k >= RFM_MOVE_NKNOBS || !d)
            return;
        if (pg == PG_MIX) {                             /* knobs 1..3: each slot's chosen value (3b: 4 the kit's) */
            if (!fx_knob_param(m, k))
                return;
            mix_turn(m, k, f->mix_par, d);
            pop(m, FP_PARAM, k, tch);
            return;
        }
        if (pg == PG_ROUTE) {                           /* a firmware selected: level, pan, sends 1..3 */
            if (k >= RFM_MOVE_MIXP_N)
                return;
            mix_turn(m, rkit(m) ? RFM_MOVE_KIT_SLOT : i, k, d);
            pop(m, FP_PARAM, k, tch);
            return;
        }
        {
            rfm_move_fxslot_t *sl = fxslot(m, kind, i);
            const rfm_move_fx_module_t *md = module(m, kind, sl->mod);
            const int p = knob_param(m, kind, i, k);    /* (0.5.0: the page's parameter) */
            int nv;
            if (!md || p < 0)
                return;
            if (md->params[p].nwords > 0) {             /* a list: a word every few ticks, round */
                const int nw = md->params[p].nwords, a0 = f->knob_acc[k];
                int a = (a0 > 0 && d < 0) || (a0 < 0 && d > 0) ? 0 : a0, n;
                a += d;
                n = a / RFM_MOVE_LIST_TICKS;
                f->knob_acc[k] = (int8_t)(a - n * RFM_MOVE_LIST_TICKS);
                nv = ((sl->val[p] + n) % nw + nw) % nw;
            } else
                nv = clampi(sl->val[p] + d, 0, 127);
            pop(m, FP_PARAM, k, tch);                   /* (the popup: by knob, its page's parameter) */
            if (nv != sl->val[p]) {
                sl->val[p] = (uint8_t)nv;
                act_fx_param(m, kind, i, p);
                rfmv_seq_dirty(m);
            }
        }
        return;
    }
    case MR_EV_TOUCH: {
        const int k = ev->index;
        if (k < 0 || k >= RFM_MOVE_NKNOBS)
            return;
        if (!ev->value) {
            f->knob_acc[k] = 0;
            if (f->pop == FP_PARAM && f->pop_a == k && f->pop_b)
                pop_clear(m);                           /* (let go: gone at once, as FM-1 mode's knob popups) */
            return;
        }
        if (fx_knob_param(m, k))                        /* (0.4.5) touched: its parameter, while it is touched */
            pop(m, FP_PARAM, k, 1);
        return;
    }
    default: return;
    }
}

/* ================================================================ the host's edits (0.4.1) === */
/* An edit made outside the Move (a web page), applied as the Move's own edit applies it: the same state change, the
 * same action, the same dirty marks (the project's autosave, the library's save). No popup, except the Track popup
 * of rfm_move_fw_set_active in the projects view (a Track press). -1 refused (out of range; nothing changed), 0 done
 * or nothing to change, 1 done and the Move's screen shows it: redraw (rfm1cs_session_redraw). */
typedef char rfmv_pg_check[(int)PG_MIDI == (int)RFM_MOVE_FX_PAGE_MIDI && (int)PG_ROUTE == (int)RFM_MOVE_FX_PAGE_ROUTE &&
                           (int)PG_AUDIO == (int)RFM_MOVE_FX_PAGE_AUDIO && (int)PG_MIX == (int)RFM_MOVE_FX_PAGE_MIX ? 1 : -1];
                                                        /* (0.6.8: as ints, GCC's -Wenum-compare) */
static int fx_shown(const rfm_move_t *m) { return m->seq.projects && m->fw.fx; }
static int set_active(rfm_move_t *m, int slot)
{
    rfm_move_fw_state_t *f = F(m);
    if (slot < 0 || slot >= nslots(m))
        return -1;
    if (m->seq.projects && !f->fx) {                    /* the projects view: a Track press (the track shown, its popup) */
        show_track(m, slot);
        return 1;
    }
    if (slot == f->active)
        return 0;
    if (f->track < 3)
        f->track = (uint8_t)slot;                       /* (FX setup left: the firmware track played) */
    activate(m, slot);
    return m->seq.projects;
}
int rfm_move_fw_set_active(rfm_move_t *m, int slot)
{
    const int r = set_active(m, slot);
    rfmv_tk_sync(m);                                    /* (0.7.0: a takeover shown or gone) */
    rfmv_say_check(m);
    return r;
}
static int level_to(rfm_move_t *m, int slot, int level)
{
    rfm_move_fw_state_t *f = F(m);
    const int v = clampi(level, 0, 127);
    if (v == f->level[slot])
        return 0;
    f->level[slot] = (uint8_t)v;
    act_fw(m, RFM_MOVE_ACT_FW_LEVEL, slot, f->slot_fw[slot], v);
    lib_changed(m);
    return 1;
}
int rfm_move_fw_set_level(rfm_move_t *m, int slot, int level)
{
    if (slot < 0 || slot >= nslots(m) || level < 0 || level > 127)
        return -1;
    return level_to(m, slot, level) && m->seq.projects;
}
int rfm_move_mix_set(rfm_move_t *m, int slot, int level, int pan, const int sends[RFM_MOVE_FX_SLOTS])
{
    rfm_move_fxset_t *x = &m->seq.fx;
    int k, lv, mix = 0;
    if (slot < 0 || slot >= nslots(m) || level < -1 || level > 127 || pan < -1 || pan > 127 || (!has_fx(m) && pan >= 0))
        return -1;
    for (k = 0; sends && k < RFM_MOVE_FX_SLOTS; k++)
        if (sends[k] < -1 || sends[k] > 127 || (!has_fx(m) && sends[k] >= 0))
            return -1;
    lv = level >= 0 && level_to(m, slot, level);        /* (FW_LEVEL and the library, as the Mixer's knob 1) */
    if (pan >= 0 && pan != x->pan[slot])
        x->pan[slot] = (uint8_t)pan, mix = 1;
    for (k = 0; sends && k < RFM_MOVE_FX_SLOTS; k++)
        if (sends[k] >= 0 && sends[k] != x->send[slot][k])
            x->send[slot][k] = (uint8_t)sends[k], mix = 1;
    if (mix) {                                          /* (MIX and the project, as knobs 2..5) */
        act_mix(m, slot);
        rfmv_seq_dirty(m);
    }
    return (lv || mix) && m->seq.projects;
}
int rfm_move_route_set(rfm_move_t *m, const int fw_filt[RFM_MOVE_FW_SLOTS], const int mfx_filt[RFM_MOVE_FX_SLOTS])
{
    rfm_move_fxset_t *x = &m->seq.fx;
    int k, any = 0;
    if (!has_fx(m))
        return -1;
    for (k = 0; k < 3; k++)
        if ((fw_filt && (fw_filt[k] < -1 || fw_filt[k] > 0xFFFF)) || (mfx_filt && (mfx_filt[k] < -1 || mfx_filt[k] > 0xFFFF)))
            return -1;
    for (k = 0; k < 3; k++) {
        if (fw_filt && fw_filt[k] >= 0 && fw_filt[k] != x->ffilt[k])
            x->ffilt[k] = (uint16_t)fw_filt[k], any = 1;
        if (mfx_filt && mfx_filt[k] >= 0) {
            const unsigned f = (unsigned)mfx_filt[k];
            const uint16_t v = f && !(f & (f - 1)) ? (uint16_t)f : 0xFFFF;   /* (one track, or all) */
            if (v != x->mfx[k].filt)
                x->mfx[k].filt = v, any = 1;
        }
    }
    if (!any)
        return 0;
    act_route(m);
    rfmv_seq_dirty(m);
    return fx_shown(m);
}
/* (0.6.9) the favourites in order (their ids), and the host's reorder */
int rfm_move_fw_favorites(const rfm_move_t *m, const char **ids, int cap)
{
    int i;
    for (i = 0; ids && i < m->fw.nfav && i < cap; i++)
        ids[i] = m->fw.rec[m->fw.fav[i]].id;
    return m->fw.nfav;
}
int rfm_move_fw_favorites_set(rfm_move_t *m, const char *const *ids, int n)
{
    rfm_move_fw_state_t *f = F(m);
    static uint16_t nu[RFM_MOVE_FW_LIB_MAX];
    int i, k, c = 0;
    if (!ids || n < 0)
        return -1;
    for (i = 0; i < n; i++)                             /* (the given ones first, in their order) */
        for (k = 0; ids[i] && k < f->nfav; k++)
            if (f->fav[k] != 0xFFFF && !strcmp(f->rec[f->fav[k]].id, ids[i])) {
                int j, dup = 0;
                for (j = 0; j < c; j++)
                    dup |= nu[j] == f->fav[k];
                if (!dup)
                    nu[c++] = f->fav[k];
                break;
            }
    for (k = 0; k < f->nfav; k++) {                     /* (the rest after them, as they were) */
        int j, there = 0;
        for (j = 0; j < c; j++)
            there |= nu[j] == f->fav[k];
        if (!there)
            nu[c++] = f->fav[k];
    }
    if (!memcmp(nu, f->fav, (size_t)c * sizeof nu[0]))
        return 0;
    memcpy(f->fav, nu, (size_t)c * sizeof nu[0]);
    lib_changed(m);
    return m->seq.projects && !f->fx && f->track < 3;
}
int rfm_move_midi_route(const rfm_move_t *m, int slot)
{
    return slot >= 0 && slot < RFM_MOVE_FW_SLOTS ? m->seq.fx.mroute[slot] : -1;
}
int rfm_move_midi_route_set(rfm_move_t *m, int slot, int mask)
{
    uint8_t r[RFM_MOVE_FW_SLOTS];
    if (!has_fx(m) || slot < 0 || slot >= RFM_MOVE_FW_SLOTS || mask < 0 || mask > 15)
        return -1;
    memcpy(r, m->seq.fx.mroute, sizeof r);
    r[slot] = (uint8_t)mask;
    if (rfmv_mroute_check(r) != 0)
        return -1;
    if (mask == m->seq.fx.mroute[slot])
        return 0;
    m->seq.fx.mroute[slot] = (uint8_t)mask;
    act_route(m);
    rfmv_seq_dirty(m);
    return fx_shown(m);
}
int rfm_move_fx_set_module(rfm_move_t *m, int kind, int slot, int module_index)
{
    if (!has_fx(m) || (kind != RFM_MOVE_FX_MIDI && kind != RFM_MOVE_FX_AUDIO) || slot < 0 || slot >= RFM_MOVE_FX_SLOTS ||
        module_index < -1 || module_index >= m->fw.nmods[kind])
        return -1;
    fx_load(m, kind, slot, module_index);               /* (the defaults, FX_LOAD, the project; as the jog's press) */
    return fx_shown(m);
}
int rfm_move_fx_set_param(rfm_move_t *m, int kind, int slot, int index, int value)
{
    rfm_move_fxslot_t *sl;
    const rfm_move_fx_module_t *md;
    int v;
    if (!has_fx(m) || (kind != RFM_MOVE_FX_MIDI && kind != RFM_MOVE_FX_AUDIO) || slot < 0 || slot >= RFM_MOVE_FX_SLOTS)
        return -1;
    sl = fxslot(m, kind, slot);
    md = module(m, kind, sl->mod);
    if (!md || index < 0 || index >= nparams(md))
        return -1;
    v = md->params[index].nwords > 0 ? clampi(value, 0, md->params[index].nwords - 1) : clampi(value, 0, 127);
    if (v == sl->val[index])
        return 0;
    sl->val[index] = (uint8_t)v;
    act_fx_param(m, kind, slot, index);
    rfmv_seq_dirty(m);
    return fx_shown(m);
}
int rfm_move_fx_set_page(rfm_move_t *m, int page)
{
    if (!has_fx(m) || page < 0 || page > PG_MIX || !fx_page_ok(m, page))
        return -1;
    if (m->s.fx_page == page)
        return 0;
    m->s.fx_page = (uint8_t)page;
    rfmv_changed(m);                                    /* (SETTINGS, as a Track press in FX setup) */
    if (!fx_shown(m))
        return 0;
    m->fw.browse = -1;
    return 1;
}

/* the projects view's input: the Track buttons, then FX setup, a firmware track, or Track 4 (project.c's) */
void rfmv_view_input(rfm_move_t *m, const mr_event_t *ev, uint32_t now)
{
    rfm_move_fw_state_t *f = F(m);
    if (ev->type == MR_EV_BUTTON && ev->index >= MR_CC_TRACK4 && ev->index <= MR_CC_TRACK1) {
        const int t = MR_CC_TRACK1 - ev->index;
        if (!ev->value) {
            m->track_held = (uint8_t)(m->track_held & ~(1u << t));
            return;
        }
        m->track_held = (uint8_t)(m->track_held | 1u << t);
        if (f->fx) {                                    /* FX setup: the pages */
            if (fx_page_ok(m, t)) {
                if (m->s.fx_page != t)
                    m->s.fx_page = (uint8_t)t, rfmv_changed(m), f->fx_pages = 0;
                f->browse = -1;
                pop(m, FP_RPAGE, t, 0);
            }
            return;
        }
        show_track(m, t);
        return;
    }
    if (ev->type == MR_EV_KNOB && ev->index == 8) {    /* volume: a held Track's level, else MASTER */
        int t;
        for (t = 0; t < nslots(m); t++)
            if (rfmv_track_held(m, t)) {
                if (slot_loaded(m, t))
                    set_level(m, t, ev->value);
                return;
            }
        if (f->fx || f->track < 3)
            rfmv_master_turn(m, ev->value);
        return;
    }
    if (f->fx) {
        fx_input(m, ev);
        return;
    }
    if (f->track < 3) {
        fw_input(m, ev);
        return;
    }
    if (f->host && has_fx(m) && ev->type == MR_EV_BUTTON && ev->index == MR_CC_MENU && ev->value) {
        open_fx(m), rfmv_fw_view_pop(m);               /* (0.5.2: named) */
        return;
    }
    if (ev->type == MR_EV_BUTTON && ev->index == MR_CC_BACK && ev->value)
        rfm_move_fw_flush(m);
    rfmv_proj_input(m, ev, now);
}

/* ================================================================ LEDs === */
/* the mockup's lights: a colour at an opacity (0..1 as /256), breathing (0.55..1 over 1.6 s), blinking (0.5 s) */
static int breathe(const rfm_move_t *m)
{
    const int ph = (int)(rfmv_now_ms(m) % 1600u), tri = ph < 800 ? ph : 1600 - ph;   /* 0..800 */
    return 141 + 115 * tri / 800;
}
static int blink(const rfm_move_t *m) { return rfmv_now_ms(m) % 500u < 250u ? 256 : 38; }
static rgb_t lit(rgb_t c, int a256) { return rfmv_sc(c, a256); }
static uint8_t wl(int a256) { return (uint8_t)clampi(a256 * 127 / 256, 0, 127); }
static int pad_of(int cell) { return (3 - cell / 8) * 8 + cell % 8; }
#define A(x) ((int)((x) * 256 + 0.5))

/* (0.8.0) the Track LEDs everywhere (proposal 2): Track 1..3 the loaded firmware's colour (an empty slot faint white),
 * full for the one played (dim in the Sequencer and on Track 4's side: Track 4 full there), past the host's voices
 * blinking slowly; one slot: Tracks 2, 3 dark. A firmware Track held: it is full */
void rfmv_track_leds(const rfm_move_t *m, rgb_t c[L_WHITE0])
{
    const rfm_move_fw_state_t *f = &m->fw;
    const int snd = rfm_move_fw_sounding(m), slow = rfmv_now_ms(m) % 1000u < 500u, ns = rfmv_fw_nslots(m);
    const int t4pg = (m->seq.projects && (f->fx || f->track == 3)) || rfmv_kit_active(m);   /* (stage 3a: the Kit view
                                                         * is Track 4's too) */
    /* (0.14.0, the owner's "Track Button LEDs should stay lit to show which Firmware/Mode I'm on") the played firmware's
     * Track is lit at rest in every mode and view: steady full in the FM-1 layout (and a takeover), breathing in the
     * sequencer (a note layout, its Knobs / Knobs+Keys page, session view; RFMV_ACTIVE_ON_MS full, then
     * RFMV_ACTIVE_OFF_MS at 60 %, the beat of the active state's pad); the others loaded a quarter; an empty slot dark
     * (the played one, empty, faint white); Track 4 full only while its own pages are shown (the played Track 60 % there,
     * steady) or it is held. Was: in the
     * sequencer (and on Track 4's pages) every firmware Track a quarter and Track 4 full */
    const int breath = m->s.mode == RFM_MOVE_MODE_SEQ &&
                       rfmv_now_ms(m) % (RFMV_ACTIVE_ON_MS + RFMV_ACTIVE_OFF_MS) >= RFMV_ACTIVE_ON_MS;
    int t, held = -1;
    for (t = 0; t < ns; t++)
        if (rfmv_track_held(m, t))
            held = t;
    for (t = 0; t < 3; t++) {
        const int top = held >= 0 ? 256 : t4pg ? A(0.6) : 256;   /* (Track 4's pages: Track 4 the brightest) */
        const int on = held >= 0 ? t == held : f->active == t, full = !on ? 0 : held >= 0 || t4pg || !breath ? top : top * 6 / 10;
        rgb_t v;
        if (t >= ns)
            v = OFF;                                    /* (0.5.10: one slot) */
        else if (!f->host)
            v = lit(rfmv_mode_rgb[0], on ? full : A(0.25));   /* (no list: the one firmware) */
        else if (slot_loaded(m, t)) {
            v = lit(RFMV_SLOT_RGB[t], on ? full : A(0.25));   /* (0.12.0, q1: the slot's own colour) */
            if (!(snd >> t & 1) && !slow)               /* (loaded past the host's voices: silent, blinking slowly) */
                v = lit(v, 40);
        } else
            v = on ? lit(WHITE, full * 6 / 10) : OFF;
        c[L_TRACK0 + t] = v;
    }
    c[L_TRACK0 + 3] = lit(TRACK4, (t4pg && held < 0) || rfmv_track_held(m, 3) ? 256 : A(0.25));
    rfmv_pfw_track_leds(m, c);                          /* (0.13.0) a project's firmwares loading: lit when ready */
}

static void fx_leds(const rfm_move_t *m, rgb_t c[L_WHITE0], uint8_t w[13])
{
    static const rgb_t MIXCOL[RFM_MOVE_MIXP_N] = { { 255, 255, 255 }, { 255, 170, 30 }, { 255, 110, 150 },
                                                   { 255, 110, 150 }, { 255, 110, 150 } };
    const rfm_move_fw_state_t *f = &m->fw;
    const rfm_move_fxset_t *x = &m->seq.fx;
    const int pg = fx_page(m), kind = pg == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO;
    const rfm_move_fx_module_t *md = pg == PG_MIDI || pg == PG_AUDIO ? module(m, kind, fxslot((rfm_move_t *)m, kind, f->fx_focus)->mod) : NULL;
    const uint16_t filt = pg == PG_MIDI ? x->mfx[f->fx_focus].filt : pg == PG_ROUTE ? route_filt(m) : 0;
    int i, r;
    if (pg == PG_MIX) {                                 /* (0.5.0) columns 1..3 the slots' meters (bottom up); the
                                                         * right 4 x 4 the choice */
        for (i = 0; i < 3; i++) {
            const int v = mix_get(m, i, f->mix_par) * 4;   /* (0..508: a pad 127) */
            if (!slot_loaded(m, i))
                continue;
            for (r = 0; r < 4; r++) {
                const int part = v - r * 127;
                c[L_PAD0 + pad_of((3 - r) * 8 + i)] =
                    part >= 127 ? RFMV_SLOT_RGB[i] : part > 0 ? lit(RFMV_SLOT_RGB[i], A(0.12) + part * (256 - A(0.12)) / 127 / 2) : OFF;
            }
        }
        if (rfmv_kit_cap(m)) {                          /* (stage 3b) column 4 the kit's */
            const int v = mix_get(m, RFM_MOVE_KIT_SLOT, f->mix_par) * 4;
            for (r = 0; r < 4; r++) {
                const int part = v - r * 127;
                c[L_PAD0 + pad_of((3 - r) * 8 + KIT_COL)] =
                    part >= 127 ? KITRGB : part > 0 ? lit(KITRGB, A(0.12) + part * (256 - A(0.12)) / 127 / 2) : OFF;
            }
        }
        for (r = 0; r < 4; r++)
            for (i = 0; i < 4; i++)
                if (MIXPAD[r][i] >= 0)
                    c[L_PAD0 + pad_of(r * 8 + 4 + i)] = lit(MIXCOL[MIXPAD[r][i]], f->mix_par == MIXPAD[r][i] ? 256 : A(0.25));
        for (i = 0; i < RFM_MOVE_NKNOBS; i++)
            c[L_RING0 + i] = i < nslots(m) || (i == RFM_MOVE_KIT_SLOT && rfmv_kit_cap(m)) ? MIXCOL[f->mix_par] : OFF;   /* (0.6.13: every knob with a job, an
                                                         * empty slot's level too) */
        w[W_MENU] = 127, w[W_BACK] = wl(A(0.35));
        return;
    }
    for (r = 0; r < 3; r++) {                           /* (0.5.0) MIDI FX, firmwares, audio FX: one layer */
        static const int PGS[3] = { PG_MIDI, PG_ROUTE, PG_AUDIO };
        for (i = 0; i < 3; i++) {
            const int p = PGS[i], foc = pg == p && f->fx_focus == r && !(p == PG_ROUTE && rkit(m));
            int empty;
            rgb_t col;
            if (!fx_page_ok(m, p))
                continue;
            if (p == PG_ROUTE)
                empty = !slot_loaded(m, r), col = empty ? WHITE : RFMV_SLOT_RGB[r];   /* (0.12.0, q1) */
            else
                empty = fxslot((rfm_move_t *)m, p == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO, r)->mod < 0,
                col = empty ? WHITE : FXCOL[p];
            c[L_PAD0 + slot_pad(p, r)] = lit(col, foc ? (pages_shown(m) ? breathe(m) : 256) : empty ? A(0.12) : A(0.42));
        }
    }
    if (rfmv_kit_cap(m) && fx_page_ok(m, PG_ROUTE))    /* (stage 3b) the kit's pad (Routing's) */
        c[L_PAD0 + slot_pad(PG_ROUTE, 0) + KIT_COL - page_col(PG_ROUTE)] = lit(KITRGB, rkit(m) ? 256 : A(0.42));
    if (pg == PG_ROUTE && !rkit(m)) {                   /* (0.6.6) the MIDI routes: the focused firmware's pad held,
                                                         * its targets (and Out, the pad under them) lit in the routes'
                                                         * colour; at rest Out only where it is on, faint */
        static const rgb_t MROUTE = { 20, 220, 255 };
        const int src = f->fx_focus, held = m->pad_down[slot_pad(PG_ROUTE, src)];
        const uint8_t mr = x->mroute[src];
        if (held) {
            for (i = 0; i < nslots(m); i++)
                if (i != src)
                    c[L_PAD0 + slot_pad(PG_ROUTE, i)] = mr >> i & 1 ? MROUTE : lit(WHITE, A(0.12));
            c[L_PAD0 + slot_pad(PG_ROUTE, 3)] = mr & RFM_MOVE_MROUTE_OUT ? MROUTE : lit(WHITE, A(0.12));
        } else if (mr & RFM_MOVE_MROUTE_OUT)
            c[L_PAD0 + slot_pad(PG_ROUTE, 3)] = lit(MROUTE, A(0.2));
    }
    for (i = 0; i < 16; i++)
        c[L_STEP0 + i] = (pg == PG_MIDI || pg == PG_ROUTE) && !(rkit(m) && i < RFM_MOVE_KIT_TRACK0)
                       ? lit(WHITE, filt >> i & 1 ? 256 : A(0.15)) : OFF;   /* (3b: the kit's, 13..16 only) */
    for (i = 0; i < RFM_MOVE_NKNOBS; i++)               /* (0.5.0: the page's knobs with a parameter) */
        c[L_RING0 + i] = (pg == PG_ROUTE ? i < RFM_MOVE_MIXP_N : md && knob_param(m, kind, f->fx_focus, i) >= 0) ? FXCOL[pg] : OFF;
    w[W_MENU] = 127, w[W_BACK] = wl(A(0.35));
    w[W_DELETE] = pg != PG_ROUTE ? wl(f->fx_del ? 256 : A(0.3)) : 0;
}

/* the projects view's LEDs (any track, FX setup) */
void rfmv_fw_leds(const rfm_move_t *m, rgb_t c[L_WHITE0], uint8_t w[13])
{
    const rfm_move_fw_state_t *f = &m->fw;
    const int t = f->track, fw = f->sel;
    int i, ids[32];
    if (!f->fx && t == 3) {                             /* Track 4: the projects, as before */
        rfmv_proj_leds(m, c, w);
        rfmv_track_leds(m, c);
        return;
    }
    memset(c, 0, sizeof(rgb_t) * L_WHITE0);
    memset(w, 0, 13);
    rfmv_track_leds(m, c);
    if (f->fx) {
        fx_leds(m, c, w);
        return;
    }
    if (f->states) {                                    /* the firmware's states, as Move's Set Overview */
        const rgb_t col = PAL[color(m, f->slot_fw[t])];
        for (i = 0; i < RFM_MOVE_FW_STATES; i++)
            c[L_PAD0 + pad_of(i)] = f->st_cur[t] == i ? col : f->st_used[t] >> i & 1 ? lit(col, A(0.42)) : OFF;
        w[W_LOOP] = 127, w[W_BACK] = wl(A(0.35));
        w[W_COPY] = f->smods & 1 ? 127 : wl(A(0.3)), w[W_DELETE] = f->smods & 2 ? 127 : wl(A(0.3));
        return;
    }
    bank_ids(m, ids);
    for (i = 0; i < 32; i++) {
        const int id = ids[i], sel = id >= 0 && id == fw;
        rgb_t v = OFF;
        if (id < 0)
            continue;
        if (f->catalog) {
            const int st = status(m, id);
            v = lit(st == ST_NEW ? WHITE : PAL[color(m, id)], sel ? 256 : st == ST_NEW ? A(0.22) : A(0.42));
            if (st == ST_UPDATE)
                v = lit(v, breathe(m));
        } else {
            v = lit(PAL[color(m, id)], sel ? 256 : A(0.42));
            if (track_of(m, id) == t)
                v = lit(v, breathe(m));
        }
        if (f->busy_kind && f->busy_fw == id)
            v = lit(v, blink(m));
        c[L_PAD0 + pad_of(i)] = v;
    }
    {                                                   /* (0.14.0) Random: the selection rolling to its pad, white */
        const uint32_t el = rfmv_now_ms(m) - m->fwr_ms;
        if (m->fwr_ms && el < RFMV_FWR_MS)
            c[L_PAD0 + pad_of(clampi(m->fwr_from + (m->fwr_to - m->fwr_from) * (int)el / RFMV_FWR_MS, 0, 31))] = WHITE;
    }
    for (i = 1; i <= 16; i++) {
        rgb_t v = OFF;
        if (f->capture && fw >= 0) {
            if (i >= CAT0)
                v = has_tag(m, fw, i) ? (i < CAT0 + 5 ? PAL[CATCOL[i - CAT0]] : i == VIRT_STEP ? VIRTCOL : WHITE)
                                      : lit(WHITE, tag_defined(m, i) ? A(0.15) : A(0.05));
        } else if (i <= 2) {
            if (f->catalog)
                v = lit(i == 1 ? AMBER : WHITE, m->s.fw_cat_scope & i ? 256 : A(0.2));
            else
                v = lit(i == 1 ? FAVCOL : WHITE, scope(m) == i - 1 ? 256 : A(0.2));
        } else if (i == RND_STEP)
            v = rnd_ok(m) ? lit(WHITE, A(0.5)) : OFF;    /* (0.14.0: Random, with two or more to pick from) */
        else if (tag_defined(m, i))
            v = lit(i < CAT0 + 5 ? PAL[CATCOL[i - CAT0]] : i == VIRT_STEP ? VIRTCOL : WHITE,
                    m->s.fw_filter >> (i - 1) & 1 ? 256 : A(0.18));
        if ((f->steps_down >> (i - 1) & 1) && (v.r || v.g || v.b))
            v = WHITE;                                  /* (0.12.0, a14: pressed: white at once, R1; the toggle at the
                                                         * release, the hold's popup at 300 ms, as before) */
        c[L_STEP0 + i - 1] = v;
    }
    c[L_SAMPLE] = fw >= 0 && !f->catalog ? lit(FAVCOL, is_fav(m, fw) ? 256 : A(0.2)) : OFF;
    w[W_CAPTURE] = fw >= 0 && !f->catalog ? (f->capture ? 127 : wl(A(0.2))) : 0;
    w[W_MUTE] = f->catalog ? 127 : wl(A(0.2));
    w[W_DELETE] = !f->catalog && slot_loaded(m, t) ? wl(A(0.3)) : 0;
    w[W_LOOP] = !f->catalog && slot_loaded(m, t) ? wl(A(0.2)) : 0;
    w[W_SHIFT] = wl(A(0.2)), w[W_BACK] = wl(A(0.35)), w[W_MENU] = wl(A(0.2));
    {
        const int nb = nbanks(m), b = cur_bank(m);
        w[W_LEFT] = nb > 1 && b > 0 ? wl(A(0.6)) : 0;
        w[W_RIGHT] = nb > 1 && b < nb - 1 ? wl(A(0.6)) : 0;
    }
    if (knob1_job(m))
        c[L_RING0] = PAL[color(m, fw)];                 /* knob 1: the colour */
    if (knob2_job(m))
        c[L_RING0 + 1] = FAVCOL;                        /* (0.6.13) knob 2: the favourite's place */
}

/* ================================================================ the screens (the mockup's render()) === */
static int rnd(double x) { return (int)(x + 0.5); }     /* (Math.round, x >= 0) */
static void header(B *g, const char *l, const char *r)
{
    char t[40];
    snprintf(t, sizeof t, "%s", l);
    cut(t, r && r[0] ? 20 - (int)strlen(r) : 21);
    fill(g, 0, 0, W, 9, 1);
    text(g, 1, 1, t, 1, 0);
    if (r && r[0])
        text(g, W - 1 - tw(r, 1), 1, r, 1, 0);
}
static void name_cut(const rfm_move_t *m, int fw, int n, char *o, int cap)
{
    asc(ename(m, fw), o, cap);
    cut(o, n);
}
static void plural(char *o, int cap, int n, const char *w) { snprintf(o, (size_t)cap, "%d %s%s", n, w, n == 1 ? "" : "s"); }
static void ver_cut(const char *v, int n, char *o, int cap)
{
    asc(v ? v : "", o, cap);
    cut(o, n);
}
static void thumb(B *g, const rfm_move_t *m, int fw, int x0)
{
    const rfm_move_fw_t *e = ent(m, fw);
    char a[64], ini[3];
    int i, n = 0;
    if (e && e->thumb) {
        bits(g, e->thumb, 64, x0, 0);
        return;
    }
    rect(g, x0 + 2, 2, 60, 60);
    asc(ename(m, fw), a, sizeof a);
    for (i = 0; a[i] && n < 2; i++)
        if ((a[i] >= 'A' && a[i] <= 'Z') || (a[i] >= 'a' && a[i] <= 'z') || (a[i] >= '0' && a[i] <= '9'))
            ini[n++] = (char)(a[i] >= 'a' && a[i] <= 'z' ? a[i] - 32 : a[i]);
    ini[n] = 0;
    ctext_in(g, 18, ini, 3, x0, 64);
    ctext_in(g, 48, "no image", 1, x0, 64);
}
/* (0.5.4) a Fantasy CFW's badge: a diamond, 3 x 3 in a grid cell (colour v), 5 x 5 framed on the name view's image */
static int is_virt(const rfm_move_t *m, int fw)
{
    const rfm_move_fw_t *e = ent(m, fw);
    return e && e->virt;
}
static void badge_small(B *g, int x, int y, int v)
{
    px(g, x + 1, y, v), px(g, x, y + 1, v), px(g, x + 2, y + 1, v), px(g, x + 1, y + 2, v);
}
static void badge(B *g, int x, int y)                   /* 9 x 9 at x, y */
{
    fill(g, x, y, 9, 9, 0);
    rect(g, x, y, 9, 9);
    px(g, x + 4, y + 2, 1), px(g, x + 3, y + 3, 1), px(g, x + 5, y + 3, 1), px(g, x + 2, y + 4, 1);
    px(g, x + 6, y + 4, 1), px(g, x + 3, y + 5, 1), px(g, x + 5, y + 5, 1), px(g, x + 4, y + 6, 1);
}
static void scope_title(const rfm_move_t *m, char *o, int cap)
{
    snprintf(o, (size_t)cap, "%s", m->fw.catalog ? "Catalog" : !scope(m) ? "Favorites" : "All");
}

/* the name view, the colour popup, the description (a firmware selected) */
static void selected(B *g, const rfm_move_t *m, int fw)
{
    const rfm_move_fw_state_t *f = &m->fw;
    const rfm_move_fw_t *e = ent(m, fw);
    char nm[64], l[5][LW], v[16], t[32];
    int nl, sc, i;
    if (f->jog == 0 || pop_up(m, FP_COLOR)) {
        thumb(g, m, fw, 64);
        if (is_virt(m, fw))
            badge(g, W - 9, 0);                         /* (0.5.4) a Fantasy CFW */
        if (pop_up(m, FP_COLOR)) {
            const int c = color(m, fw);
            const int s2 = strlen(PALNAME[c]) <= 5;
            ctext_in(g, 4, "Colour", 1, 0, 64);
            text(g, ifloor_div(64 - tw(PALNAME[c], s2 ? 2 : 1), 2), s2 ? 22 : 24, PALNAME[c], s2 ? 2 : 1, 1);
            snprintf(t, sizeof t, "%d/16", c + 1);
            ctext_in(g, 48, t, 1, 0, 64);
            return;
        }
        asc(ename(m, fw), nm, sizeof nm);
        {                                               /* 2x in rows of 5 when it fits in 3, else 1x in rows of 10 */
            int longw = 0, run = 0;
            const char *p;
            for (p = nm;; p++) {
                if (*p == ' ' || !*p) {
                    longw |= run > 5, run = 0;
                    if (!*p)
                        break;
                } else
                    run++;
            }
            nl = wrap(nm, 5, l, 0, 5);
            if (nl <= 3 && !longw)
                sc = 2;
            else
                sc = 1, nl = wrap(nm, 10, l, 0, 5);
        }
        if (f->catalog) {
            const int st = status(m, fw);
            for (i = 0; i < nl && i < (sc == 2 ? 2 : 4); i++)
                text(g, 0, 1 + i * (sc == 2 ? 17 : 9), l[i], sc, 1);
            ver_cut(e ? e->version : "", 9, v, sizeof v);
            if (st == ST_UPDATE) {
                char fr[40];
                snprintf(fr, sizeof fr, "from %s", e->installed);
                asc(fr, t, sizeof t), cut(t, 10);
                text(g, 0, 41, t, 1, 1), icon(g, 0, 52, UP, 5), text(g, 8, 51, v, 1, 1);
            } else if (st == ST_NEW)
                text(g, 0, 41, "not here", 1, 1), icon(g, 0, 52, DOWN, 5), text(g, 8, 51, v, 1, 1);
            else {
                snprintf(t, sizeof t, "v%s", v);
                text(g, 0, 41, "installed", 1, 1), text(g, 0, 51, t, 1, 1);
            }
            return;
        }
        for (i = 0; i < nl && i < (sc == 2 ? 3 : 4); i++)
            text(g, 0, 1 + i * (sc == 2 ? 17 : 9), l[i], sc, 1);
        icon(g, 0, 51, is_fav(m, fw) ? HEART : HEARTO, 5);
        if ((i = track_of(m, fw)) >= 0) {
            snprintf(t, sizeof t, "T%d", i + 1);
            text(g, 9, 50, t, 1, 1);
        }
        icon(g, 57, 57, CHEV, 3);
        return;
    }
    {                                                   /* the description: a row a detent */
        char rows[INFO_ROWS][LW];
        const int top = f->jog - 1, n = desc_rows(m, fw, rows, top, INFO_ROWS);
        name_cut(m, fw, 12, nm, sizeof nm);
        fill(g, 0, 0, W, 9, 1);
        text(g, 1, 1, nm, 1, 0);
        ver_cut(e ? e->version : "", 9, v, sizeof v);
        snprintf(t, sizeof t, "v%s", v);
        text(g, W - 1 - tw(t, 1), 1, t, 1, 0);
        for (i = 0; i < INFO_ROWS && top + i < n; i++)
            text(g, 0, 13 + i * 10, rows[i], 1, 1);
        if (n > INFO_ROWS) {
            const int h = rnd(52.0 * INFO_ROWS / n) > 6 ? rnd(52.0 * INFO_ROWS / n) : 6;
            fill(g, 126, 12 + rnd((double)(52 - h) * top / (n - INFO_ROWS)), 2, h, 1);
        }
    }
}

static void tagging(B *g, const rfm_move_t *m, int fw)
{
    const rfm_move_fw_state_t *f = &m->fw;
    char nm[32], t[24];
    int s;
    fill(g, 0, 0, W, 9, 1);
    text(g, 1, 1, "Tags", 1, 0);
    name_cut(m, fw, 14, nm, sizeof nm);
    text(g, W - 1 - tw(nm, 1), 1, nm, 1, 0);
    if (f->cap_step >= CAT0 && rfmv_now_ms(m) - f->cap_ms < CAP_MS) {
        tag_name(m, f->cap_step, t, sizeof t);
        if (strlen(t) <= 10)                            /* (0.4.2: a long name small, never cut) */
            ctext(g, 16, t, 2);
        else
            ctext(g, 20, t, 1);
        ctext(g, 36, has_tag(m, fw, f->cap_step) ? "tagged" : "untagged", 1);
    } else {
        char all[256], rows[3][LW];
        int n = 0, k;
        all[0] = 0;
        for (s = CAT0; s <= 16; s++)
            if (has_tag(m, fw, s)) {
                tag_name(m, s, t, sizeof t);
                n += snprintf(all + n, sizeof all - (size_t)n, "%s%s", n ? ", " : "", t);
                if (n >= (int)sizeof all)
                    n = (int)sizeof all - 1;
            }
        k = wrap(n ? all : "no tags", 21, rows, 0, 3);
        for (s = 0; s < k && s < 3; s++)
            text(g, 0, 14 + s * 9, rows[s], 1, 1);
    }
    for (s = CAT0; s <= 16; s++) {
        const int x = (s - 1) * 8;
        if (has_tag(m, fw, s))
            fill(g, x + 1, 50, 6, 10, 1);
        else if (tag_defined(m, s))
            rect(g, x + 1, 50, 6, 10);
        else
            px(g, x + 3, 55, 1);
        if (f->cap_step == s && rfmv_now_ms(m) - f->cap_ms < CAP_MS)
            hline(g, x + 1, 62, 6, 0);
    }
    hline(g, 16, 47, 40, 1);
}

static void rest(B *g, const rfm_move_t *m)             /* the bank: 8 x 4 cells of 16 x 16, two letters each */
{
    const rfm_move_fw_state_t *f = &m->fw;
    int ids[32], i;
    if (!bank_ids(m, ids)) {
        if (!f->catalog && !scope(m) && !m->s.fw_filter)
            ctext(g, 12, f->n ? "None yet" : "Empty", 2), ctext(g, 38, f->n ? "Sampling adds one" : "no firmwares listed", 1);
        else
            ctext(g, 12, "Nothing", 2), ctext(g, 38, "matches the filter", 1);
        return;
    }
    for (i = 0; i < 32; i++) {
        const int x = (i % 8) * 16, y = (i / 8) * 16, id = ids[i];
        char ab[3];
        if (id < 0) {
            fill(g, x + 7, y + 7, 2, 2, 1);
            continue;
        }
        abbr(ename(m, id), ab);
        if (!f->catalog && track_of(m, id) == f->track) {
            fill(g, x, y, 16, 16, 1);
            text(g, x + 3, y + 5, ab, 1, 0);
        } else
            text(g, x + 3, y + 5, ab, 1, 1);
        if (is_virt(m, id))                             /* (0.5.4) a Fantasy CFW: the top-left corner */
            badge_small(g, x + 1, y + 1, !(!f->catalog && track_of(m, id) == f->track));
        if (f->catalog) {
            const int st = status(m, id);
            if (st == ST_UPDATE)
                icon(g, x + 11, y, UPMARK, 2);
            else if (st == ST_NEW)
                hline(g, x + 3, y + 13, 11, 1);
        } else {
            const int t = track_of(m, id);
            if (t >= 0 && t != f->track)
                drect(g, x, y, 16, 16);
        }
    }
}

static void states_grid(B *g, const rfm_move_t *m)      /* Move's Set Overview: the 32 state slots */
{
    const rfm_move_fw_state_t *f = &m->fw;
    const int t = f->track;
    int i;
    for (i = 0; i < RFM_MOVE_FW_STATES; i++) {
        const int x = (i % 8) * 16 + 1, y = (i / 8) * 16 + 1, cur = f->st_cur[t] == i;
        char n[4];
        if (!cur && !(f->st_used[t] >> i & 1)) {
            fill(g, x + 6, y + 6, 2, 2, 1);
            continue;
        }
        snprintf(n, sizeof n, "%d", i + 1);
        if (cur)
            fill(g, x, y, 14, 14, 1);
        else
            rect(g, x, y, 14, 14);
        text(g, x + ifloor_div(14 - tw(n, 1), 2), y + 4, n, 1, !cur);
    }
}

/* ---- FX setup (the mockup's renderRouting) */
static void range_text(uint16_t set, char *o, int cap)
{
    int s, n = 0, a = -1;
    o[0] = 0;
    if (!set) {
        snprintf(o, (size_t)cap, "none");
        return;
    }
    if (set == 0xFFFF) {
        snprintf(o, (size_t)cap, "all");
        return;
    }
    for (s = 1; s <= 17; s++) {
        const int on = s <= 16 && (set >> (s - 1) & 1);
        if (on && a < 0)
            a = s;
        else if (!on && a >= 0) {
            n += snprintf(o + n, (size_t)(cap - n), a == s - 1 ? "%s%d" : "%s%d-%d", n ? "," : "", a, s - 1);
            if (n >= cap)
                n = cap - 1;
            a = -1;
        }
    }
}
static const char *slot_mod_name(const rfm_move_t *m, int kind, int i)
{
    const rfm_move_fxslot_t *x = kind == RFM_MOVE_FX_MIDI ? &m->seq.fx.mfx[i] : &m->seq.fx.afx[i];
    return x->mod >= 0 ? x->name : NULL;
}
static void focus_name(const rfm_move_t *m, char *o, int cap)
{
    const int pg = fx_page(m), i = m->fw.fx_focus;
    char nm[32];
    if (pg == PG_ROUTE && rkit(m))
        snprintf(o, (size_t)cap, "Kit");                /* (stage 3b) */
    else if (pg == PG_ROUTE || pg == PG_MIX) {
        if (slot_loaded(m, i))
            slot_name(m, i, nm, sizeof nm);
        else
            snprintf(nm, sizeof nm, "empty");
        snprintf(o, (size_t)cap, "FW%d %s", i + 1, nm);
    } else {
        const char *n = slot_mod_name(m, pg == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO, i);
        asc(n ? n : "empty", nm, sizeof nm);
        snprintf(o, (size_t)cap, "%s %d %s", pg == PG_MIDI ? "MIDI FX" : "Audio FX", i + 1, nm);
    }
}
static void pan_text(int v, char *o, int cap)
{
    if (v == 64)
        snprintf(o, (size_t)cap, "C");
    else
        snprintf(o, (size_t)cap, "%c%d", v < 64 ? 'L' : 'R', v < 64 ? 64 - v : v - 64);
}
/* (0.5.0) a firmware's mix value: its name ("Send to Delay" long, "Send 1" short) and value */
static void mix_words(const rfm_move_t *m, int slot, int par, char *t, int tc, char *v, int vc, int lng)
{
    if (par == RFM_MOVE_MIXP_LEVEL)
        snprintf(t, (size_t)tc, "Level"), snprintf(v, (size_t)vc, "%d", mix_get(m, slot, par));
    else if (par == RFM_MOVE_MIXP_PAN)
        snprintf(t, (size_t)tc, "Pan"), pan_text(mix_get(m, slot, par), v, vc);
    else {
        const char *an = slot_mod_name(m, RFM_MOVE_FX_AUDIO, par - 2);
        char a2[32];
        if (an && lng)
            asc(an, a2, sizeof a2);
        else
            snprintf(a2, sizeof a2, "FX %d", par - 1);
        if (lng)
            snprintf(t, (size_t)tc, "Send to %s", a2);
        else
            snprintf(t, (size_t)tc, "Send %d", par - 1);
        snprintf(v, (size_t)vc, "%d", mix_get(m, slot, par));
    }
}
static void fx_screen(B *g, const rfm_move_t *m)
{
    const rfm_move_fw_state_t *f = &m->fw;
    const rfm_move_fxset_t *x = &m->seq.fx;
    const int pg = fx_page(m), i = f->fx_focus, kind = pg == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO;
    const int slotpg = pg == PG_MIDI || pg == PG_AUDIO;
    char fn[48], t[48], v[32];
    int r, c;
    if (pop_up(m, FP_RPAGE)) {
        snprintf(t, sizeof t, "Track %d", pg + 1);
        ctext(g, 12, FXPAGE[pg], 2), ctext(g, 38, t, 1);
        return;
    }
    if (slotpg && f->browse >= 0) {                     /* the module list, Move's list style */
        const int cb = f->browse, n = f->nmods[kind] + 1;
        char it[3][32];
        for (r = -1; r <= 1; r++) {
            const int k = cb + r;
            it[r + 1][0] = 0;
            if (k == 0)
                snprintf(it[r + 1], sizeof it[r + 1], "(empty)");
            else if (k > 0 && k < n)
                asc(f->mods[kind][k - 1].name, it[r + 1], sizeof it[r + 1]);
        }
        snprintf(t, sizeof t, "%s %d", kind ? "Audio FX" : "MIDI FX", i + 1);
        header(g, t, NULL);
        if (cb > 0)
            text(g, 12, 15, it[0], 1, 1);
        text(g, 0, 29, ">", 1, 1);
        cut(it[1], 9);
        text(g, 12, 26, it[1], 2, 1);
        if (cb + 1 < n)
            text(g, 12, 48, it[2], 1, 1);
        return;
    }
    focus_name(m, fn, sizeof fn);
    if (pop_up(m, FP_PARAM)) {
        const int k = f->pop_a;
        int hint = 0;
        if (pg == PG_ROUTE || pg == PG_MIX) {           /* (0.5.0) a firmware's level, pan, sends: Routing's knobs
                                                         * 1..5 for the selected one, the Mixer's 1..3 for each */
            const int sl = pg == PG_MIX ? k : rkit(m) ? RFM_MOVE_KIT_SLOT : i;
            if (sl == RFM_MOVE_KIT_SLOT)
                snprintf(fn, sizeof fn, "Kit");         /* (stage 3b) the kit's strip */
            else if (pg == PG_MIX) {
                char nm[32];
                if (slot_loaded(m, sl))
                    slot_name(m, sl, nm, sizeof nm);
                else
                    snprintf(nm, sizeof nm, "empty");
                snprintf(fn, sizeof fn, "FW%d %s", sl + 1, nm);
            }
            mix_words(m, sl, pg == PG_MIX ? f->mix_par : k, t, sizeof t, v, sizeof v, 1);
        } else {
            const rfm_move_fxslot_t *sl = kind == RFM_MOVE_FX_MIDI ? &x->mfx[i] : &x->afx[i];
            const rfm_move_fx_module_t *md = module(m, kind, sl->mod);
            const int p = knob_param(m, kind, i, k);    /* (0.5.0: knob k's parameter on the slot's page) */
            const rfm_move_fx_param_t *pa;
            if (!md || p < 0)
                return;
            pa = &md->params[p];
            asc(pa->name ? pa->name : "", t, sizeof t);
            param_text(pa, sl->val[p], v, sizeof v);
            hint = pa->dry_wet && kind == RFM_MOVE_FX_AUDIO;
        }
        header(g, fn, NULL);
        {
            const int s3 = strlen(v) <= 7;
            ctext(g, s3 ? 17 : 20, v, s3 ? 3 : 2);
        }
        ctext(g, hint ? 47 : 49, t, 1);
        if (hint)
            ctext(g, 56, "100 % wet for a send", 1);
        return;
    }
    if (pop_up(m, FP_MROUTE)) {                         /* (0.6.6) a MIDI route toggled: from, to, on / off / Loop */
        const int src = f->pop_a, bit = f->pop_b & 15, on = f->pop_b >> 4 & 1, loop = f->pop_b >> 5 & 1;
        char nm[32];
        if (slot_loaded(m, src))
            slot_name(m, src, nm, sizeof nm);
        else
            snprintf(nm, sizeof nm, "empty");
        snprintf(t, sizeof t, "FW%d %s", src + 1, nm);
        header(g, t, NULL);
        ctext(g, 14, "MIDI to", 1);
        if (bit == 3)
            snprintf(v, sizeof v, "Out");
        else if (slot_loaded(m, bit))
            slot_name(m, bit, v, sizeof v);
        else
            snprintf(v, sizeof v, "FW%d", bit + 1);
        {
            const int s2 = strlen(v) <= 10;
            cut(v, 21);
            ctext(g, s2 ? 26 : 29, v, s2 ? 2 : 1);
        }
        ctext(g, 50, loop ? "Loop: not set" : on ? "on" : "off", 1);
        return;
    }
    if (pop_up(m, FP_RFILTER) && (pg == PG_MIDI || pg == PG_ROUTE)) {
        const uint16_t filt = pg == PG_MIDI ? x->mfx[i].filt : route_filt(m);
        range_text(filt, t, sizeof t);
        header(g, fn, NULL);
        ctext(g, 14, pg == PG_MIDI ? "acts on tracks" : "hears tracks", 1);
        {
            const int sh = strlen(t) <= 10;
            cut(t, 21);
            ctext(g, sh ? 26 : 29, t, sh ? 2 : 1);
        }
        if (pg != PG_MIDI) {
            snprintf(t, sizeof t, "track %d%s", f->pop_a, f->pop_b ? " in" : " out");
            ctext(g, 50, t, 1);
        } else
            ctext(g, 50, filt == 0xFFFF ? "same step again: one" : "same step again: all", 1);
        return;
    }
    if (pop_up(m, FP_RLOADED) && slotpg) {
        const char *n = slot_mod_name(m, kind, i);
        snprintf(t, sizeof t, "%s %d", kind ? "Audio FX" : "MIDI FX", i + 1);
        header(g, t, NULL);
        if (f->pop_text[0])
            snprintf(v, sizeof v, "%s", f->pop_text);
        else
            asc(n ? n : "empty", v, sizeof v);
        ctext(g, 22, v, 2);
        return;
    }
    if (pages_shown(m)) {                               /* (0.5.0) the module's parameter page: 2 rows of 4 knobs */
        const rfm_move_fxslot_t *sl = kind == RFM_MOVE_FX_MIDI ? &x->mfx[i] : &x->afx[i];
        const rfm_move_fx_module_t *md = module(m, kind, sl->mod);
        const int np = npages(md), cur = slot_page(m, kind, i);
        char nm[32];                                    /* (the header: the page's name, or the module's; n/m) */
        if (host_pages(md) && md->pages[cur].name && md->pages[cur].name[0])
            asc(md->pages[cur].name, nm, sizeof nm);
        else
            asc(sl->name, nm, sizeof nm);
        snprintf(t, sizeof t, "%d/%d", cur + 1, np);
        header(g, nm, t);
        for (c = 0; c < RFM_MOVE_FX_KNOBS; c++) {
            const int p = page_param(md, cur, c), x0 = (c % 4) * 32, y0 = 12 + (c / 4) * 24;
            char lab[16];
            if (p < 0) {                                /* a gap: a dash */
                hline(g, x0 + 12, y0 + 9, 7, 0);
                continue;
            }
            asc(md->params[p].short_name ? md->params[p].short_name : md->params[p].name ? md->params[p].name : "",
                lab, sizeof lab);
            cut(lab, 5);
            text(g, x0 + ifloor_div(32 - tw(lab, 1), 2), y0, lab, 1, 1);
            param_text(&md->params[p], sl->val[p], v, sizeof v);
            cut(v, 5);
            text(g, x0 + ifloor_div(32 - tw(v, 1), 2), y0 + 10, v, 1, 1);
        }
        strip(g, np, cur);                              /* (the pages, the current one thick) */
        return;
    }
    if (pg == PG_MIX) {                                 /* (0.5.0) the Mixer: what knobs 1..3 set, each slot's */
        mix_words(m, 0, f->mix_par, t, sizeof t, v, sizeof v, 1);
        header(g, "Mixer", t);
        for (r = 0; r < (rfmv_kit_cap(m) ? 4 : 3); r++) {   /* (stage 3b: the kit a fourth strip, narrower) */
            const int SW = rfmv_kit_cap(m) ? 32 : 43;
            const int x0 = r * SW, val = mix_get(m, r, f->mix_par), wfill = rnd((SW - 6) * (double)val / 127);
            char ab[3], nm[12];
            if (r == RFM_MOVE_KIT_SLOT)
                snprintf(nm, sizeof nm, "Kit");
            else if (slot_loaded(m, r)) {
                char sn[32];
                slot_name(m, r, sn, sizeof sn);
                abbr(sn, ab);
                snprintf(nm, sizeof nm, "%s %d", ab, r + 1);
            } else
                snprintf(nm, sizeof nm, "- %d", r + 1);
            ctext_in(g, 12, nm, 1, x0, SW - 2);
            mix_words(m, r, f->mix_par, fn, sizeof fn, v, sizeof v, 0);
            ctext_in(g, 26, v, 2, x0, SW - 2);
            rect(g, x0 + 2, 50, SW - 4, 9);
            if (f->mix_par == RFM_MOVE_MIXP_PAN) {      /* (pan: from the centre) */
                const int c0 = x0 + (SW - 1) / 2, e = x0 + 3 + rnd((SW - 7) * (double)val / 127);
                fill(g, e < c0 ? e : c0, 51, (e < c0 ? c0 - e : e - c0) + 1, 7, 1);
            } else
                fill(g, x0 + 3, 51, wfill, 7, 1);
        }
        return;
    }
    {                                                   /* the chain: MIDI FX, the firmwares, the audio FX */
        static const int X[3] = { 2, 46, 90 }, Y[3] = { 12, 29, 46 };
        const int BW = 34, BH = 15;
        const uint16_t filt = pg == PG_MIDI ? x->mfx[i].filt : route_filt(m);
        range_text(filt, t, sizeof t);
        cut(t, 8);
        header(g, fn, pg == PG_AUDIO ? "" : t);
        for (c = 0; c < 3; c++)
            for (r = 0; r < 3; r++) {
                char lab[32];
                int empty, foc, tx;
                if (c == 1) {
                    empty = !slot_loaded(m, r);
                    if (empty)
                        snprintf(lab, sizeof lab, "-");
                    else
                        slot_name(m, r, lab, sizeof lab);
                } else {
                    const char *n = slot_mod_name(m, c == 0 ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO, r);
                    empty = !n;
                    asc(n ? n : "-", lab, sizeof lab);
                }
                cut(lab, 5);
                foc = (c == 0 ? pg == PG_MIDI : c == 1 ? pg == PG_ROUTE && !rkit(m) : pg == PG_AUDIO) && i == r;
                tx = X[c] + ifloor_div(BW - tw(lab, 1), 2);
                if (foc)
                    fill(g, X[c], Y[r], BW, BH, 1), text(g, tx, Y[r] + 4, lab, 1, 0);
                else if (empty)
                    drect(g, X[c], Y[r], BW, BH), text(g, tx, Y[r] + 4, lab, 1, 1);
                else
                    rect(g, X[c], Y[r], BW, BH), text(g, tx, Y[r] + 4, lab, 1, 1);
                if (c == 1 && x->mroute[r]) {           /* (0.6.6) its MIDI routed on: a notch in the top-right
                                                         * corner (Out too: one in the bottom-right) */
                    fill(g, X[c] + BW - 4, Y[r] + 1, 3, 2, !foc);
                    if (x->mroute[r] & RFM_MOVE_MROUTE_OUT)
                        fill(g, X[c] + BW - 4, Y[r] + BH - 3, 3, 2, !foc);
                }
            }
        fill(g, 19, 27, 1, 2, 1), fill(g, 19, 44, 1, 2, 1);
        hline(g, 36, 53, 6, 0), fill(g, 41, 19, 1, 35, 1);
        hline(g, 41, 19, 5, 0), hline(g, 41, 36, 5, 0), hline(g, 41, 53, 5, 0);
        for (r = 0; r < 3; r++) {
            int any = 0;
            if (!slot_loaded(m, r))
                continue;
            for (c = 0; c < 3; c++)
                if (x->send[r][c] > 0)
                    any = 1, line(g, 80, Y[r] + 7, 89, Y[c] + 7);
            if (!any)
                hline(g, 80, Y[r] + 7, 4, 1);
        }
    }
}

/* the level popup (Track held + volume), the view's own popups that cover any track */
/* (0.4.2) the filter popup: the step pressed (or held), big, and its state; what the list shows now (2 lines at
 * word boundaries, "..." past them); the count */
static int held_peek(const rfm_move_t *m)
{
    return m->fw.hold_step && rfmv_now_ms(m) - m->fw.hold_ms >= RFM_MOVE_TAP_MS;
}
/* its words: the step's name ("" none), its state ("ON", "off", ""), what the list shows (lines 0..2) */
static int filter_words(const rfm_move_t *m, int step, char name[24], const char **state, char rows[3][LW])
{
    char all[260];
    int k;
    name[0] = 0, *state = "";
    if (step >= 1 && step <= 16)
        step_name(m, step, name, 24), *state = step_on(m, step) ? "ON" : "off";
    showing(m, all, sizeof all);
    k = wrap(all, 21, rows, 0, 3);
    if (k > 2)                                          /* (too long: without "Showing: ") */
        k = wrap(all + 9, 21, rows, 0, 3);
    if (k > 2) {                                        /* (still: the second line ends in "...") */
        char *sp;
        int l;
        while (strlen(rows[1]) > 18 && (sp = strrchr(rows[1], ' ')) != NULL)
            *sp = 0;                                    /* (whole words) */
        cut(rows[1], 18);
        for (l = (int)strlen(rows[1]); l > 0 && (rows[1][l - 1] == ' ' || rows[1][l - 1] == ','); l--)
            rows[1][l - 1] = 0;
        strcat(rows[1], "...");
        k = 2;
    }
    return k;
}
static void filter_pop(B *g, const rfm_move_t *m, int step)
{
    char t[40], name[24], rows[3][LW];
    const char *state;
    const int k = filter_words(m, step, name, &state, rows);
    int n;
    if (name[0]) {
        if (strlen(name) <= 10)
            ctext(g, 2, name, 2);
        else
            ctext(g, 6, name, 1);
        if (state[0] == 'O') {
            const int w = tw(state, 1) + 6;             /* (inverted) */
            fill(g, (W - w) / 2, 19, w, 10, 1);
            text(g, (W - tw(state, 1)) / 2, 20, state, 1, 0);
        } else
            ctext(g, 20, state, 1);
    } else {
        scope_title(m, t, sizeof t);
        ctext(g, 6, t, 1);
    }
    for (n = 0; n < k; n++)
        ctext(g, 32 + n * 9, rows[n], 1);
    n = list_len(m);
    plural(t, sizeof t, n, "firmware");
    ctext(g, 52, t, 1);
    strip(g, nbanks_n(n), 0);
}
static int cover_popup(B *g, const rfm_move_t *m)
{
    const rfm_move_fw_state_t *f = &m->fw;
    char t[48], nm[32];
    int k;
    if (pop_up(m, FP_LEVEL)) {
        const int tr = f->pop_a;
        if (nslots(m) < 2)
            snprintf(t, sizeof t, "Level");             /* (0.5.10: one slot) */
        else
            snprintf(t, sizeof t, "Track %d level", tr + 1);
        ctext(g, 2, t, 1);
        snprintf(t, sizeof t, "%d", f->level[tr]);
        ctext(g, 16, t, 3);
        if (slot_loaded(m, tr))
            slot_name(m, tr, nm, sizeof nm), cut(nm, 21);
        else
            snprintf(nm, sizeof nm, "empty");
        ctext(g, 42, nm, 1);
        for (k = 0; k < nslots(m); k++) {
            const int w = nslots(m) < 2 ? rnd(128.0 * f->level[k] / 127) : rnd(40.0 * f->level[k] / 127);
            if (k == tr)
                fill(g, k * 43, 59, w, 5, 1);
            else
                fill(g, k * 43, 62, w, 2, 1);
        }
        return 1;
    }
    if (!m->seq.projects || f->fx)
        return 0;
    if (pop_up(m, FP_TRACK)) {
        const int tr = f->pop_a;
        snprintf(t, sizeof t, "Track %d", tr + 1);
        ctext(g, 8, t, 2);
        if (tr == 3)
            ctext(g, 36, "Projects", 1);
        else if (slot_loaded(m, tr)) {
            slot_name(m, tr, nm, sizeof nm), cut(nm, 21);
            ctext(g, 36, nm, 1);
            ctext(g, 48, rfm_move_fw_sounding(m) >> tr & 1 ? "playing" : "silent", 1);
        } else
            ctext(g, 36, "empty", 1);
        return 1;
    }
    if (pop_up(m, FP_CATALOG)) {
        int u = 0, n = 0, i;
        for (i = 0; i < f->n; i++)
            u += status(m, i) == ST_UPDATE, n += status(m, i) == ST_NEW;
        ctext(g, 12, "Catalog", 2);
        plural(nm, sizeof nm, u, "update");
        snprintf(t, sizeof t, "%s, %d new", nm, n);
        ctext(g, 38, t, 1);
        return 1;
    }
    if (pop_up(m, FP_UNLOAD)) {
        snprintf(nm, sizeof nm, "%s", f->pop_text);
        cut(nm, 10);
        ctext(g, 12, nm, 2);
        if (nslots(m) < 2)
            snprintf(t, sizeof t, "unloaded");
        else
            snprintf(t, sizeof t, "unloaded from Track %d", f->pop_b + 1);
        ctext(g, 38, t, 1);
        return 1;
    }
    return 0;
}

static void fw_screen(B *g, const rfm_move_t *m)
{
    const rfm_move_fw_state_t *f = &m->fw;
    const int fw = f->sel;
    char t[64], nm[32];
    if (f->busy_kind) {                                 /* loading, downloading: a bar */
        const char *verb = f->busy_kind == BUSY_LOAD ? "Loading..." : f->busy_kind == BUSY_UPDATE ? "Updating..." : "Downloading";
        if (f->busy_kind == BUSY_DOWNLOAD)
            ctext(g, 4, verb, 1), name_cut(m, f->busy_fw, 10, nm, sizeof nm), ctext(g, 18, nm, 2);
        else
            ctext(g, 8, verb, 2), name_cut(m, f->busy_fw, 21, nm, sizeof nm), ctext(g, 32, nm, 1);
        rect(g, 13, 50, 102, 6);
        fill(g, 14, 51, f->busy_pct, 4, 1);
        return;
    }
    if (pop_up(m, FP_LOADED)) {
        const rfm_move_fw_t *e = ent(m, f->pop_a);
        name_cut(m, f->pop_a, 10, nm, sizeof nm);
        ctext(g, 10, nm, 2);
        if (f->pop_b == 10) {
            char v[16];
            ver_cut(e ? e->version : "", 10, v, sizeof v);
            snprintf(t, sizeof t, "updated to %s", v);
        } else if (f->pop_b == 11)
            snprintf(t, sizeof t, "on the Move");
        else if (nslots(m) < 2)
            snprintf(t, sizeof t, "loaded");
        else
            snprintf(t, sizeof t, "loaded on Track %d", f->pop_b + 1);
        if (f->pop_b >= 10 && f->catalog && f->sel == f->pop_a && f->track < 3 && status(m, f->pop_a) == ST_OK) {
            ctext(g, 32, t, 1);                         /* (0.4.2: still selected: the jog loads it) */
            if (nslots(m) < 2)
                snprintf(t, sizeof t, "Jog: load it");
            else
                snprintf(t, sizeof t, "Jog: load on Track %d", f->track + 1);
            ctext(g, 46, t, 1);
        } else
            ctext(g, 36, t, 1);
        return;
    }
    if (pop_up(m, FP_MSG)) {
        if (f->pop_a >= 0) {
            name_cut(m, f->pop_a, 10, nm, sizeof nm);
            ctext(g, 12, nm, 2);
            ctext(g, 38, f->pop_text, 1);
        } else
            ctext(g, 22, f->pop_text, 2);
        return;
    }
    if (pop_up(m, FP_STATE)) {
        static const char *const WH[3] = { "saved", "loaded", "deleted" };
        snprintf(t, sizeof t, "State %d", f->pop_a + 1);
        ctext(g, 10, t, 2);
        ctext(g, 36, WH[f->pop_b % 3], 1);
        return;
    }
    if (f->states) {
        states_grid(g, m);
        return;
    }
    if (pop_up(m, FP_FILTER) || held_peek(m)) {
        filter_pop(g, m, held_peek(m) ? f->hold_step : f->pop_a);
        return;
    }
    if (pop_up(m, FP_BANK)) {
        int ids[32], k = bank_ids(m, ids);
        char a[16], b[16];
        scope_title(m, t, sizeof t);
        ctext(g, 4, t, 1);
        snprintf(t, sizeof t, "Bank %d", cur_bank(m) + 1);
        ctext(g, 19, t, 2);
        if (k) {
            name_cut(m, ids[0], 9, a, sizeof a), name_cut(m, ids[k - 1], 9, b, sizeof b);
            snprintf(t, sizeof t, "%s - %s", a, b);
            ctext(g, 43, t, 1);
        }
        strip(g, nbanks(m), cur_bank(m));
        return;
    }
    if (pop_up(m, FP_FAV)) {
        name_cut(m, f->pop_a, 10, nm, sizeof nm);
        ctext(g, 12, nm, 2);
        ctext(g, 38, f->pop_b ? "added to Favorites" : "removed from Favorites", 1);
        return;
    }
    if (pop_up(m, FP_ORDER)) {                          /* (0.6.9) a favourite moved: the FAV popup's layout */
        name_cut(m, f->pop_a, 10, nm, sizeof nm);
        ctext(g, 12, nm, 2);
        snprintf(t, sizeof t, "position %d of %d", f->pop_b & 0xFFF, list_len(m));
        ctext(g, 38, t, 1);
        return;
    }
    if (f->capture && fw >= 0) {
        tagging(g, m, fw);
        return;
    }
    if (fw < 0) {
        rest(g, m);
        return;
    }
    selected(g, m, fw);
}

/* the projects view's screen and the level popup: 1 drawn (else Track 4's grid as before) */
int rfmv_fw_screen(const rfm_move_t *m, mr_canvas_t *cv)
{
    const rfm_move_fw_state_t *f = &m->fw;
    if (!m->seq.projects && !pop_up(m, FP_LEVEL))
        return 0;
    mr_gfx_clear(cv, 0);
    if (cover_popup(cv, m))
        return 1;
    if (f->fx) {
        fx_screen(cv, m);
        return 1;
    }
    if (f->track == 3)
        return 0;
    fw_screen(cv, m);
    return 1;
}
/* the projects view's popups and loads change with the clock */
int rfmv_fw_timed(const rfm_move_t *m)
{
    const rfm_move_fw_state_t *f = &m->fw;
    return pop_up(m, -1) || (m->seq.projects && (f->busy_kind || (f->capture && f->cap_step >= 0) || f->hold_step));
}

/* (0.4.3) Back held + Track 1..3: that slot's firmware played (FW_ACTIVE, the library), no flip. In the projects view
 * (not FX setup) it is that Track's press; elsewhere a popup names it. A slot with nothing loaded, or no browser
 * (no fw_list): nothing */
int rfmv_fw_switch(rfm_move_t *m, int t)
{
    rfm_move_fw_state_t *f = F(m);
    if (t < 0 || t >= nslots(m) || !f->host || !slot_loaded(m, t))
        return 0;
    if (m->seq.projects && !f->fx) {
        show_track(m, t);
        return 1;
    }
    if (t != f->active) {
        if (f->track < 3)
            f->track = (uint8_t)t;
        activate(m, t);
    }
    if (!m->seq.projects)
        rfmv_pop(m, rfmv_tk_mode(m) ? POP_TK : POP_LAYOUT, t);   /* (0.8.0: the slot and its layout named) */
    return 1;
}
int rfmv_fw_loaded(const rfm_move_t *m)             /* loaded slots, 0 with no host list */
{
    int s, n = 0;
    if (!m->fw.host)
        return 0;
    for (s = 0; s < RFM_MOVE_FW_SLOTS; s++)
        n += slot_loaded(m, s);
    return n;
}
void rfmv_fw_slot_name(const rfm_move_t *m, int s, char *o, int cap)
{
    if (s >= 0 && s < RFM_MOVE_FW_SLOTS)
        slot_name(m, s, o, cap);
    else if (cap > 0)
        o[0] = 0;
}

/* ---- (0.8.0) the Track buttons (move.c): the slots they play, the view opened on a slot, the projects, the Mixer or
 * Routing */
int rfmv_fw_nslots(const rfm_move_t *m) { return m->fw.host ? nslots(m) : 1; }
/* (0.12.0, q1, the owner's: "the 3 firmware tracks each have their own color theme, like: Orange, Blue, Yellow") a
 * slot's own colour, on everything that means "this slot": the Track buttons, the routing lights, session view's
 * knobs and columns, the Mixer's and Routing's slot pads. The firmware's own colour stays in the browser */
const rgb_t RFMV_SLOT_RGB[RFM_MOVE_FW_SLOTS] = { { 255, 120, 0 }, { 40, 120, 255 }, { 255, 200, 0 } };
rgb_t rfmv_fw_rgb(const rfm_move_t *m, int slot)        /* a slot's colour (empty, no list: white) */
{
    return m->fw.host && slot_loaded(m, slot) ? RFMV_SLOT_RGB[slot % RFM_MOVE_FW_SLOTS] : WHITE;
}
int rfmv_fx_ok(const rfm_move_t *m) { return m->fw.host && has_fx(m); }
/* (0.12.0, a14, the owner's: "just show icons enabled for the filters that are on") the browser's step icons: full
 * under a filter that is on (the scope steps 1, 2; a category or tag), dark otherwise; with Capture held the tags the
 * selected firmware has. 1: the browser is up (the icons are its) */
int rfmv_fw_step_icons(const rfm_move_t *m, uint8_t icon[16])
{
    const rfm_move_fw_state_t *f = &m->fw;
    const int fw = f->sel;
    int i;
    if (m->seq.projects && !f->fx && f->track == 3 && !f->states && !m->shift_down && !(m->seq.mods & SEQ_M_SHIFT)) {
        for (i = 0; i < 16; i++)                        /* (0.14.0) the project grid: step 13, a new random project,
                                                         * lit while there is an empty slot */
            icon[i] = 0;
        icon[12] = (uint8_t)(rfmv_proj_first_empty(m) >= 0 ? 127 : 0);
        return 1;
    }
    if (!m->seq.projects || f->fx || f->track >= 3 || f->states)
        return 0;
    for (i = 1; i <= 16; i++) {
        int on = 0;
        if (f->capture && fw >= 0)
            on = i >= CAT0 && has_tag(m, fw, i);
        else if (i <= 2)
            on = f->catalog ? (m->s.fw_cat_scope & i) != 0 : scope(m) == i - 1;
        else if (i == RND_STEP)
            on = 1;                                     /* (0.14.0: Random, a function: lit while the browser is) */
        else
            on = tag_defined(m, i) && (m->s.fw_filter >> (i - 1) & 1);
        icon[i - 1] = (uint8_t)(on ? 127 : 0);
    }
    return 1;
}
/* (0.14.0) up to three different installed firmwares at random (the projects page's random project), "" past them */
int rfmv_fw_pick_installed(rfm_move_t *m, char ids[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX])
{
    int pool[256], n = 0, i, k;
    for (i = 0; i < m->fw.n && n < 256; i++)
        if (installed(m, i) && ent(m, i)->id && ent(m, i)->id[0])
            pool[n++] = i;
    for (k = 0; k < RFM_MOVE_FW_SLOTS; k++) {
        ids[k][0] = 0;
        if (k < n) {
            const int j = k + (int)(rfmv_ui_rnd(m) % (uint32_t)(n - k)), x = pool[j];
            pool[j] = pool[k], pool[k] = x;
            snprintf(ids[k], RFM_MOVE_FW_ID_MAX, "%s", ent(m, x)->id);
        }
    }
    return n < RFM_MOVE_FW_SLOTS ? n : RFM_MOVE_FW_SLOTS;
}
int rfmv_t4_page(const rfm_move_t *m)                  /* (stage 3a: 2 the Kit view, a host with the kit) */
{
    if (!(m->seq.projects && m->fw.fx) && rfmv_kit_cap(m) && m->s.t4_kit)
        return 2;
    return m->s.fx_page == PG_MIX ? 0 : 1;
}
int rfmv_fw_activate(rfm_move_t *m, int t)
{
    rfm_move_fw_state_t *f = F(m);
    if (t < 0 || t >= rfmv_fw_nslots(m) || t == f->active)
        return 0;
    if (f->track < 3)
        f->track = (uint8_t)t;
    activate(m, t);
    return 1;
}
/* (0.11.0) .. from session view: the slot played changes, the view stays (no layout entered: move.c's track_tap) */
int rfmv_fw_activate_keep(rfm_move_t *m, int t)
{
    rfm_move_fw_state_t *f = F(m);
    if (t < 0 || t >= rfmv_fw_nslots(m) || t == f->active)
        return 0;
    if (f->track < 3)
        f->track = (uint8_t)t;
    rfmv_slot_leaving(m);
    f->active = (uint8_t)t;
    act_fw(m, RFM_MOVE_ACT_FW_ACTIVE, t, f->slot_fw[t], 0);
    lib_changed(m);
    return 1;
}
void rfmv_view_close(rfm_move_t *m)
{
    if (m->seq.projects)
        leave(m);
}
void rfmv_view_open(rfm_move_t *m, int what)
{
    rfm_move_fw_state_t *f = F(m);
    if (what < VIEW_PROJECTS && (!f->host || what >= nslots(m)))
        return;
    if ((what == VIEW_MIXER || what == VIEW_ROUTING) && !rfmv_fx_ok(m))
        return;
    if (!m->seq.projects)
        rfmv_proj_enter(m);
    f->states = 0, f->capture = 0, f->smods = 0, f->browse = -1, f->catalog = 0, f->sel = -1, f->jog = 0;
    pop_clear(m);
    if (what < VIEW_PROJECTS) {                         /* a slot's browser (it is played: its Track popup) */
        f->fx = 0;
        show_track(m, what);
        if (m->pop == POP_MODE)
            m->pop = POP_NONE;
    } else if (what == VIEW_PROJECTS) {                 /* the sequencer's projects */
        f->fx = 0, f->track = 3;
        if (m->s.fw_track != 3)
            m->s.fw_track = 3, rfmv_changed(m);
        rfmv_pop(m, POP_MODE, 20);
    } else {                                            /* FX setup: the Mixer, or Routing on its column */
        const int pg = what == VIEW_MIXER ? PG_MIX : fx_page_ok(m, m->fx_route) ? m->fx_route : PG_ROUTE;
        if (fx_page(m) != PG_MIX)
            m->fx_route = (uint8_t)fx_page(m);
        set_pg(m, pg);
        f->fx = 1, f->fx_pages = 0;
        if (m->s.t4_kit)                                /* (stage 3a: Track 4 left on the Mixer / Routing) */
            m->s.t4_kit = 0, rfmv_changed(m);
        rfmv_pop(m, POP_T4, what == VIEW_ROUTING);
    }
}
/* (0.9.0) a firmware's solo (one track at a time; its filter as it was kept aside) given back */
static int solo_end(rfm_move_t *m, int slot)
{
    if (!m->fsolo[slot])
        return 0;
    m->seq.fx.ffilt[slot] = m->fsolo_save[slot];
    m->fsolo[slot] = 0;
    return 1;
}
void rfmv_route_toggle(rfm_move_t *m, int slot, int track)
{
    if (slot < 0 || slot >= RFM_MOVE_FW_SLOTS || track < 0 || track >= RFM_MOVE_SEQ_TRACKS)
        return;
    solo_end(m, slot);                                  /* (0.9.0: a routing change ends the firmware's solo) */
    m->seq.fx.ffilt[slot] = (uint16_t)(m->seq.fx.ffilt[slot] ^ 1u << track);
    act_route(m);
    rfmv_seq_dirty(m);
}
/* (0.9.0) a track selected for a slot reaches it: routed now */
void rfmv_route_on(rfm_move_t *m, int slot, int track)
{
    int ch = 0;
    if (slot < 0 || slot >= RFM_MOVE_FW_SLOTS || track < 0 || track >= RFM_MOVE_SEQ_TRACKS)
        return;
    if (m->fsolo[slot] && m->fsolo[slot] != track + 1) {   /* (0.11.0, the owner's) a solo there stays: the track is
                                                         * routed once it ends */
        m->fsolo_save[slot] = (uint16_t)(m->fsolo_save[slot] | 1u << track);
        return;
    }
    if (!(m->seq.fx.ffilt[slot] >> track & 1))
        m->seq.fx.ffilt[slot] = (uint16_t)(m->seq.fx.ffilt[slot] | 1u << track), ch = 1;
    if (ch) {
        act_route(m);
        rfmv_seq_dirty(m);
    }
}
/* (0.9.0, the owner's) Track n held + a double tap: only that track reaches firmware n (one solo a firmware: another
 * there ends); again: off, the filter as it was. 1: soloed now */
int rfmv_fw_solo(rfm_move_t *m, int slot, int track)
{
    if (slot < 0 || slot >= RFM_MOVE_FW_SLOTS || track < 0 || track >= RFM_MOVE_SEQ_TRACKS)
        return 0;
    if (m->fsolo[slot] == track + 1)
        solo_end(m, slot);
    else {
        if (!m->fsolo[slot])
            m->fsolo_save[slot] = m->seq.fx.ffilt[slot];
        m->seq.fx.ffilt[slot] = (uint16_t)(1u << track);
        m->fsolo[slot] = (uint8_t)(track + 1);
    }
    act_route(m);
    rfmv_seq_dirty(m);
    return m->fsolo[slot] != 0;
}

/* (for the tests) */
int rfmv_fw_bank(const rfm_move_t *m, int ids[32]) { return bank_ids(m, ids); }
int rfmv_fw_list_len(const rfm_move_t *m) { return list_len(m); }
int rfmv_fw_rec(const rfm_move_t *m, int fw) { return rec_of(m, fw); }
int rfmv_fw_filter_text(const rfm_move_t *m, int step, char name[24], const char **state, char rows[3][22])
{
    return filter_words(m, step, name, state, rows);
}

/* ================================================================ (0.6.0) words for a screen reader === */
/* (speech.c says them: the base view as an event when it changes, the popups and the selection as values) */
static void slot_words(const rfm_move_t *m, int s, char *o, int cap)
{
    char nm[64];
    if (slot_loaded(m, s))
        slot_name(m, s, nm, sizeof nm);
    else
        snprintf(nm, sizeof nm, "empty");
    snprintf(o, (size_t)cap, "Slot %d, %s", s + 1, nm);
}
static void fx_slot_words(const rfm_move_t *m, int kind, int i, char *o, int cap)
{
    const char *n = slot_mod_name(m, kind, i);
    char nm[32];
    asc(n ? n : "empty", nm, sizeof nm);
    snprintf(o, (size_t)cap, "%s %d: %s", kind == RFM_MOVE_FX_MIDI ? "MIDI FX" : "Audio FX", i + 1, nm);
}
/* the projects view's base: its subject (0: not the projects view) and its words */
int rfmv_fw_say_base(const rfm_move_t *m, char *o, int cap)
{
    const rfm_move_fw_state_t *f = &m->fw;
    o[0] = 0;
    if (!m->seq.projects)
        return 0;
    if (f->fx) {
        const int pg = fx_page(m), kind = pg == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO, i = f->fx_focus;
        if (pages_shown(m) && f->browse < 0) {          /* a module's parameter page */
            const rfm_move_fxslot_t *sl = kind == RFM_MOVE_FX_MIDI ? &m->seq.fx.mfx[i] : &m->seq.fx.afx[i];
            const rfm_move_fx_module_t *md = module(m, kind, sl->mod);
            const int cur = slot_page(m, kind, i);
            char nm[32], pn[32];
            asc(sl->name, nm, sizeof nm);
            if (host_pages(md) && md->pages[cur].name && md->pages[cur].name[0])
                asc(md->pages[cur].name, pn, sizeof pn);
            else
                snprintf(pn, sizeof pn, "Page %d", cur + 1);
            snprintf(o, (size_t)cap, "%s, %s", nm, pn);
            return 0x200 | kind << 7 | (i & 7) << 4 | (cur & 15);
        }
        snprintf(o, (size_t)cap, "%s", FXPAGE[pg]);
        return 0x100 | pg;
    }
    if (f->track == 3 || !f->host) {
        snprintf(o, (size_t)cap, "Projects");
        return 0x10;
    }
    if (f->states) {
        char t[80];
        slot_words(m, f->track, t, sizeof t);
        snprintf(o, (size_t)cap, "States, %s", t + 8);  /* (past "Slot n, ") */
        return 0x20 | f->track;
    }
    if (f->catalog) {
        const int c = m->s.fw_cat_scope & 3;
        snprintf(o, (size_t)cap, "Catalog%s", c == 3 ? ", Update ready, Not installed" : c == 1 ? ", Update ready"
                                              : c == 2 ? ", Not installed" : "");
        return 0x18 | c;
    }
    slot_words(m, f->track, o, cap);
    return 0x11 + f->track;
}
/* the selection (said when it changes, not again after a popup over it): the firmware selected, its description, a
 * focused FX slot, the module list, the Mixer's choice */
void rfmv_fw_say_focus(const rfm_move_t *m, char *o, int cap)
{
    const rfm_move_fw_state_t *f = &m->fw;
    o[0] = 0;
    if (!m->seq.projects)
        return;
    if (f->fx) {
        const int pg = fx_page(m), kind = pg == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO, i = f->fx_focus;
        if ((pg == PG_MIDI || pg == PG_AUDIO) && f->browse >= 0) {
            if (f->browse == 0 || f->browse > f->nmods[kind])
                snprintf(o, (size_t)cap, "empty");
            else
                asc(f->mods[kind][f->browse - 1].name, o, cap);
        } else if (pages_shown(m))
            ;                                           /* (the base names the page) */
        else if (pg == PG_MIX) {
            char t[48], v[16];
            mix_words(m, 0, f->mix_par, t, sizeof t, v, sizeof v, 1);
            snprintf(o, (size_t)cap, "%s", t);
        } else if (pg == PG_ROUTE)
            slot_words(m, i, o, cap);
        else
            fx_slot_words(m, kind, i, o, cap);
        return;
    }
    if (f->track == 3 || !f->host || f->states || f->sel < 0)
        return;
    if (f->capture) {                                   /* tagging: the firmware's tags */
        char all[200], t[24];
        int s, n = 0;
        all[0] = 0;
        for (s = CAT0; s <= 16; s++)
            if (has_tag(m, f->sel, s)) {
                tag_name(m, s, t, sizeof t);
                n += snprintf(all + n, sizeof all - (size_t)n, "%s%s", n ? ", " : "", t);
                if (n >= (int)sizeof all)
                    n = (int)sizeof all - 1;
            }
        snprintf(o, (size_t)cap, "Tags: %s", n ? all : "none");
        return;
    }
    if (f->jog > 0) {                                   /* the description (the jog: its rows; said once) */
        const rfm_move_fw_t *e = ent(m, f->sel);
        const char *t = e && e->desc ? e->desc : "";
        int n = 0;
        for (; *t && n < cap - 1 && n < 250; t++)
            if (*t == '\n' || *t == '\t')
                o[n++] = ' ';
            else if ((unsigned char)*t >= 32 && (unsigned char)*t < 127)
                o[n++] = *t;
        o[n] = 0;
        if (!n)
            snprintf(o, (size_t)cap, "No description");
        return;
    }
    {
        char nm[64];
        asc(ename(m, f->sel), nm, sizeof nm);
        if (f->catalog) {
            const int st = status(m, f->sel);
            snprintf(o, (size_t)cap, "%s, %s", nm, st == ST_NEW ? "not installed" : st == ST_UPDATE ? "update ready"
                                                                                                  : "installed");
        } else
            snprintf(o, (size_t)cap, "%s", nm);
    }
}
/* a popup's words (1: one is up, the words may be "" when the base says it) */
int rfmv_fw_say_pop(const rfm_move_t *m, char *o, int cap)
{
    const rfm_move_fw_state_t *f = &m->fw;
    char nm[64], t[64], v[32];
    o[0] = 0;
    if (pop_up(m, FP_LEVEL)) {
        if (nslots(m) < 2)
            snprintf(o, (size_t)cap, "Level: %d", f->level[f->pop_a]);
        else
            snprintf(o, (size_t)cap, "Slot %d Level: %d", f->pop_a + 1, f->level[f->pop_a]);
        return 1;
    }
    if (!m->seq.projects)
        return 0;
    if (f->fx) {
        const int pg = fx_page(m), kind = pg == PG_MIDI ? RFM_MOVE_FX_MIDI : RFM_MOVE_FX_AUDIO, i = f->fx_focus;
        const int slotpg = pg == PG_MIDI || pg == PG_AUDIO;
        if (slotpg && f->browse >= 0)
            return 0;
        if (pop_up(m, FP_PARAM)) {
            const int k = f->pop_a;
            if (pg == PG_ROUTE || pg == PG_MIX) {
                const int sl = pg == PG_MIX ? k : rkit(m) ? RFM_MOVE_KIT_SLOT : i;
                mix_words(m, sl, pg == PG_MIX ? f->mix_par : k, t, sizeof t, v, sizeof v, 0);
                if (sl == RFM_MOVE_KIT_SLOT)
                    snprintf(o, (size_t)cap, "Kit %s: %s", t, v);
                else
                    snprintf(o, (size_t)cap, "Slot %d %s: %s", sl + 1, t, v);
            } else {
                const rfm_move_fxslot_t *sl = kind == RFM_MOVE_FX_MIDI ? &m->seq.fx.mfx[i] : &m->seq.fx.afx[i];
                const rfm_move_fx_module_t *md = module(m, kind, sl->mod);
                const int p = knob_param(m, kind, i, k);
                if (md && p >= 0) {
                    asc(md->params[p].name ? md->params[p].name : "", t, sizeof t);
                    param_text(&md->params[p], sl->val[p], v, sizeof v);
                    snprintf(o, (size_t)cap, "%s: %s", t, v);
                }
            }
            return 1;
        }
        if (pop_up(m, FP_MROUTE)) {                     /* (0.6.6) "ChoralRoot to Oberwave, on", "Loop" */
            const int src = f->pop_a, bit = f->pop_b & 15;
            if (f->pop_b >> 5 & 1)
                snprintf(o, (size_t)cap, "Loop");
            else {
                char a[64], b[64];
                if (slot_loaded(m, src))
                    slot_name(m, src, a, sizeof a);
                else
                    snprintf(a, sizeof a, "Slot %d", src + 1);
                if (bit == 3)
                    snprintf(b, sizeof b, "Out");
                else if (slot_loaded(m, bit))
                    slot_name(m, bit, b, sizeof b);
                else
                    snprintf(b, sizeof b, "Slot %d", bit + 1);
                snprintf(o, (size_t)cap, "%s to %s, %s", a, b, f->pop_b >> 4 & 1 ? "on" : "off");
            }
            return 1;
        }
        if (pop_up(m, FP_RFILTER) && (pg == PG_MIDI || pg == PG_ROUTE)) {
            const uint16_t filt = pg == PG_MIDI ? m->seq.fx.mfx[i].filt : route_filt(m);
            char r[48];
            range_text(filt, r, sizeof r);
            if (pg == PG_MIDI)
                fx_slot_words(m, kind, i, t, sizeof t), *strchr(t, ':') = 0;
            else if (rkit(m))
                snprintf(t, sizeof t, "Kit");
            else
                snprintf(t, sizeof t, "Slot %d", i + 1);
            if (filt == 0xFFFF)
                snprintf(o, (size_t)cap, "%s: tracks 1-16", t);
            else if (!filt)
                snprintf(o, (size_t)cap, "%s: no tracks", t);
            else
                snprintf(o, (size_t)cap, "%s: %s %s", t, strpbrk(r, ",-") ? "tracks" : "track", r);
            return 1;
        }
        if (pop_up(m, FP_RLOADED) && slotpg) {
            if (f->pop_text[0]) {
                fx_slot_words(m, kind, i, t, sizeof t), *strchr(t, ':') = 0;
                snprintf(o, (size_t)cap, "%s: %s", t, f->pop_text);
            } else
                fx_slot_words(m, kind, i, o, cap);
            return 1;
        }
        return 0;
    }
    if (pop_up(m, FP_UNLOAD)) {
        asc(f->pop_text, nm, sizeof nm);
        snprintf(o, (size_t)cap, "%s unloaded", nm);
        return 1;
    }
    if (f->track == 3 || !f->host)
        return 0;
    if (f->busy_kind) {
        asc(ename(m, f->busy_fw), nm, sizeof nm);
        if (f->busy_kind == BUSY_LOAD)
            snprintf(o, (size_t)cap, "Loading %s", nm);
        else                                            /* (a quarter at a time) */
            snprintf(o, (size_t)cap, "%s %s %d%%", f->busy_kind == BUSY_UPDATE ? "Updating" : "Downloading", nm,
                     f->busy_pct / 25 * 25);
        return 1;
    }
    if (pop_up(m, FP_LOADED)) {
        asc(ename(m, f->pop_a), nm, sizeof nm);
        if (f->pop_b == 10) {
            const rfm_move_fw_t *e = ent(m, f->pop_a);
            asc(e && e->version ? e->version : "", v, sizeof v);
            snprintf(o, (size_t)cap, "%s updated to %s", nm, v);
        } else
            snprintf(o, (size_t)cap, "%s %s", nm, f->pop_b == 11 ? "installed" : "loaded");
        return 1;
    }
    if (pop_up(m, FP_MSG)) {
        if (f->pop_a >= 0 && f->pop_b == RFM_MOVE_FW_BUSY)
            snprintf(o, (size_t)cap, "Too busy");
        else if (f->pop_a >= 0) {
            asc(ename(m, f->pop_a), nm, sizeof nm);
            snprintf(o, (size_t)cap, "%s failed", nm);
        } else
            asc(f->pop_text, o, cap);
        return 1;
    }
    if (pop_up(m, FP_STATE)) {                          /* (0.6.3: said here; the host drops its own) */
        static const char *const WH[3] = { "saved", "loaded", "deleted" };
        snprintf(o, (size_t)cap, "State %d %s", f->pop_a + 1, WH[f->pop_b % 3]);
        return 1;
    }
    if (f->states)
        return 0;
    if (pop_up(m, FP_FILTER) || held_peek(m)) {
        const int step = held_peek(m) ? f->hold_step : f->pop_a;
        if (step == 1 || step == 2) {
            step_name(m, step, nm, sizeof nm);
            if (!f->catalog)
                snprintf(o, (size_t)cap, "%s", step == 1 ? "Favorites" : "All firmwares");
            else
                snprintf(o, (size_t)cap, "%s%s", nm, step_on(m, step) ? "" : " off");
        } else if (step >= CAT0 && step <= 16) {
            tag_name(m, step, nm, sizeof nm);
            if (step_on(m, step))
                snprintf(o, (size_t)cap, "Filter %s", nm);
            else if (!(m->s.fw_filter >> (CAT0 - 1)))
                snprintf(o, (size_t)cap, "No filter");
            else
                snprintf(o, (size_t)cap, "Filter %s off", nm);
        }
        return 1;
    }
    if (pop_up(m, FP_BANK)) {
        snprintf(o, (size_t)cap, "Bank %d", cur_bank(m) + 1);
        return 1;
    }
    if (pop_up(m, FP_FAV)) {
        asc(ename(m, f->pop_a), nm, sizeof nm);
        snprintf(o, (size_t)cap, "%s %s", nm, f->pop_b ? "added to Favorites" : "removed from Favorites");
        return 1;
    }
    if (pop_up(m, FP_ORDER)) {
        asc(ename(m, f->pop_a), nm, sizeof nm);
        if (f->pop_b & 0x1000)                          /* (0.6.13: touched: what the knob does, where it is) */
            snprintf(o, (size_t)cap, "Move %s, %d of %d", nm, f->pop_b & 0xFFF, list_len(m));
        else
            snprintf(o, (size_t)cap, "%s, position %d of %d", nm, f->pop_b, list_len(m));
        return 1;
    }
    if (pop_up(m, FP_COLOR) && f->sel >= 0) {
        snprintf(o, (size_t)cap, "Colour: %s", PALNAME[color(m, f->sel)]);
        return 1;
    }
    if (f->capture && f->sel >= 0 && f->cap_step >= CAT0 && rfmv_now_ms(m) - f->cap_ms < CAP_MS) {
        tag_name(m, f->cap_step, nm, sizeof nm);
        snprintf(o, (size_t)cap, "%s %s", has_tag(m, f->sel, f->cap_step) ? "Tagged" : "Untagged", nm);
        return 1;
    }
    return 0;
}

/* ---- (0.13.0) for projfw.c: a project's firmwares, States as a layer */
/* a firmware's name by its id: the host's list, else the id itself ("" none) */
void rfmv_fw_id_name(const rfm_move_t *m, const char *id, char *o, int cap)
{
    const int i = find_id(m, id);
    if (i >= 0)
        asc(ename(m, i), o, cap);
    else
        asc(id, o, cap);
}
/* the slot holds that firmware now ("" empty): another firmware forgets the slot's states (the host tells them) */
void rfmv_fw_slot_set(rfm_move_t *m, int s, const char *id)
{
    rfm_move_fw_state_t *f = F(m);
    char b[RFM_MOVE_FW_ID_MAX];
    if (s < 0 || s >= RFM_MOVE_FW_SLOTS)
        return;
    snprintf(b, sizeof b, "%s", id && strlen(id) < RFM_MOVE_FW_ID_MAX ? id : "");
    if (strcmp(f->slot_id[s], b)) {
        strcpy(f->slot_id[s], b);
        f->st_used[s] = 0, f->st_cur[s] = -1;
        if (f->busy_kind == BUSY_LOAD && f->busy_slot == s)
            f->busy_kind = BUSY_NONE, f->busy_fw = -1;
        lib_changed(m);
    }
    f->slot_fw[s] = (int16_t)find_id(m, b);
}
int rfmv_fw_slot_loaded(const rfm_move_t *m, int s) { return slot_loaded(m, s); }
