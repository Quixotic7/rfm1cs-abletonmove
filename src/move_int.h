/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm_move's insides, shared by move.c (the mapping, the MIDI modes), seq.c (the sequencer's data, clock and
 * engine), seq_ui.c (Sequencer mode's controls, LEDs and screen) and project.c (the project blob). Not an API:
 * the tests include it to look at the data model. */
#ifndef RFM_MOVE_INT_H
#define RFM_MOVE_INT_H
#include "moveremote/gfx.h"
#include "rfm1cs/move.h"

typedef struct { uint8_t r, g, b; } rfmv_rgb_t;

enum { H_NONE = 0, H_KEY, H_BTN, H_NOTE, H_MENU, H_SEQ, H_ARP, H_GATE };   /* (H_GATE 0.14.0: Knobs+Keys' Delete / Copy) */
/* the LED list (rfm_move_leds): pads, steps, the RGB CC LEDs, then the 13 white ones (WHITE_CC order) */
enum { L_PAD0 = 0, L_STEP0 = 32, L_PLAY = 48, L_REC, L_SAMPLE, L_TRACK0, L_RING0 = 55, L_WHITE0 = 63, L_ICON0 = 76 };
enum { W_SHIFT, W_MENU, W_BACK, W_CAPTURE, W_DOWN, W_UP, W_UNDO, W_LOOP, W_COPY, W_LEFT, W_RIGHT, W_MUTE, W_DELETE };

#define RFM_MOVE_SCALE_W 9                              /* a scale: its length, then up to 8 degrees */
extern const uint8_t rfmv_scales[RFM_MOVE_NSCALES][RFM_MOVE_SCALE_W];   /* [0] = length */
int rfmv_scale_of_pad(int p);                          /* Track 2 held: the scale a pad picks (0..23) */
extern const char *const rfmv_note_names[12];
extern const char *const rfmv_scale_names[RFM_MOVE_NSCALES];
extern const rfmv_rgb_t rfmv_mode_rgb[4], rfmv_bank_rgb[4];
extern const rfmv_rgb_t RFMV_WHITE, RFMV_RED, RFMV_GREEN, RFMV_OFF;

int rfmv_clampi(int v, int lo, int hi);
rfmv_rgb_t rfmv_sc(rfmv_rgb_t c, int f);               /* c x f / 256 */
rfmv_rgb_t rfmv_add(rfmv_rgb_t a, rfmv_rgb_t b);
rfmv_rgb_t rfmv_midi_pad(const rfm_move_t *m, int p, rfmv_rgb_t mode);
rfmv_rgb_t rfmv_layout_pad(const rfm_move_t *m, int p, rfmv_rgb_t mode);   /* .. as if not pressed */
int rfmv_track_held(const rfm_move_t *m, int t);
int rfmv_half(const rfm_move_t *m);
int rfmv_label(const rfm_move_t *m, int slot, char *out, int cap);   /* (0.5.6) a slot's label to show: 1 if any */
int rfmv_label_held(const rfm_move_t *m, int slot);    /* its control is held */
int rfmv_enc_slot(int enc);
int rfmv_pop_slot(const rfm_move_t *m);                 /* (0.5.7) the knob slot a knob popup is for (-1: none) */                            /* an encoder (RFM_MOVE_E_*): its label slot */
int rfmv_list_ticks(rfm_move_t *m, int k, int d);      /* a list-type menu knob's items (one per RFM_MOVE_LIST_TICKS) */
void rfmv_release_holds(rfm_move_t *m);                /* every held control let go (entering projects mode) */
void rfmv_proj_input(rfm_move_t *m, const mr_event_t *ev, uint32_t now);   /* projects mode's input */
void rfmv_proj_leds(const rfm_move_t *m, rfmv_rgb_t c[L_WHITE0], uint8_t white[13]);
rfmv_rgb_t rfmv_proj_rgb(const rfm_move_t *m, int slot);   /* a project's colour */
void rfmv_proj_enter(rfm_move_t *m);                   /* projects mode on (Shift + step 1, Track 1 + step 1) */
int rfmv_hold_shown(const rfm_move_t *m, int t);       /* Track t's hold screen is due (held long enough, or used) */                    /* FM-1 mode, MIDI's NOTES: left-half popups, turn-only knob ones */
int rfmv_seq_trk_held(const rfm_move_t *m);            /* the sequencer: Track 4 held (the tracks, the track's settings) */

/* actions (stamped with m->seq.frame) */
void rfmv_emit(rfm_move_t *m, uint8_t kind, int index, int down, int vel, int value);
void rfmv_emit_midi(rfm_move_t *m, int status, int d1, int d2);
void rfmv_changed(rfm_move_t *m);
void rfmv_note_hold(rfm_move_t *m, int ch, int n, int vel);
void rfmv_note_let(rfm_move_t *m, int ch, int n);
void rfmv_let_go(rfm_move_t *m, rfm_move_hold_t *h);

int rfmv_level(const rfm_move_t *m, int id);           /* the FM-1 LED level 0..255 of a panel id (0..13, 14 + key) */

/* ---- screen.c: the screen (every mode's picture, the popups, the held Track buttons' menus) */
enum { POP_NONE = 0, POP_ENC, POP_MASTER, POP_CC, POP_SELECT, POP_DRUM, POP_TEMPO, POP_LOOP, POP_BANK, POP_STEP,
       POP_PROJ, POP_TRIG, POP_BAR, POP_TRACK, POP_MODE, POP_FW,
       POP_HOST, POP_LABEL, POP_READ, POP_TK, POP_TRKINFO };   /* (POP_TRKINFO 0.10.0: session view, a knob touched: its track) */   /* (POP_TK 0.7.0: a takeover's name, pop_i its slot) */   /* (POP_READ 0.5.7: a readout of its own, rd_pop) */   /* (POP_LABEL 0.5.6: a button's or key's label, pop_i its slot) */   /* (POP_HOST 0.5.4: rfm_move_popup's words) */   /* (POP_FW 0.4.3: Back + Track, a firmware) */
#define RFM_MOVE_TAP_MS 300                             /* a Track button released this soon: a tap (not its menu) */
enum { MENU_NONE = 0, MENU_LAYOUT, MENU_SCALE, MENU_ROOT, MENU_CHANNEL, MENU_CC, MENU_TRACK, MENU_TCH, MENU_RATE, MENU_SHIFT,
       MENU_T4, MENU_OCT, MENU_LOCK };   /* (0.8.0: MENU_T4 Track 4 held's page, MENU_OCT a slot's octave, MENU_LOCK) */
uint32_t rfmv_now_ms(const rfm_move_t *m);             /* the sequencer clock's ms (what popups time out on) */
void rfmv_pop(rfm_move_t *m, int kind, int i);          /* a popup up (again) now */
void rfmv_pop_let(rfm_move_t *m, int kind, int i);      /* its control let go: it drops RFM_MOVE_POPUP_MS from now */
int rfmv_pop_up(const rfm_move_t *m, int kind);
int rfmv_seq_menu_knobs(const rfm_move_t *m);
int rfmv_seq_held_takes(const rfm_move_t *m);          /* (0.12.0) a held step has the knobs and jog (over a menu too) */
int rfmv_seq_loop_over(const rfm_move_t *m);           /* .. Loop's layer shows (over a menu: held, or latched after it) */
int rfmv_seq_mute_on(const rfm_move_t *m);             /* .. Mute's layer shows */
int rfmv_seq_mod_over(const rfm_move_t *m);
rfmv_rgb_t rfmv_seq_fill_rgb(const rfm_move_t *m);      /* (0.12.0) FILL's wave: its colour */
int rfmv_seq_fill_on(const rfm_move_t *m, int p);       /* .. pad p on it now */            /* .. a held modifier's picture over the open menu */
void rfmv_vel_step(rfm_move_t *m, int d);              /* the velocity option along the jog's order */
void rfmv_tcase(const char *s, char *o, int cap);       /* Title Case ("CUTOFF" -> "Cutoff") */
void rfmv_keys_menu(const rfm_move_t *m, mr_canvas_t *cv, int glob);   /* the key / scale screen (-1: channels) */
#define RFM_MOVE_SV_COLS 7                              /* (0.10.0) session view: the track columns (the 8th: the scenes) */
void rfmv_seq_track_info(const rfm_move_t *m, int track, char row[4][32]);   /* session's track popup: its 4 rows */
void rfmv_seq_track_pop(const rfm_move_t *m, mr_canvas_t *cv, int track);    /* .. drawn */
int rfmv_seq_msg_info(const rfm_move_t *m, const char **a, const char **b, int *icon);   /* the message up: its id */
int rfmv_seq_end_page(const rfm_move_t *m, int clip);   /* the clip's last page (the loop's, or one with notes) */
void rfmv_seq_loop_edit(const rfm_move_t *m, mr_canvas_t *cv);   /* Loop's page edit: the pages as cells */
void rfmv_seq_page_strip(const rfm_move_t *m, mr_canvas_t *cv, int at);   /* Move's strip: pages, the playhead */
void rfmv_seq_steps_at(const rfm_move_t *m, mr_canvas_t *cv, uint32_t marks, int y);   /* .. at y */
void rfmv_seq_steps_cells(const rfm_move_t *m, mr_canvas_t *cv, uint32_t marks);   /* the 16 steps as cells */           /* the knobs the open Shift menu uses (a bit each) */
int rfmv_menu_knobs(const rfm_move_t *m);               /* a menu's or a held Track's knobs (a bit each); -1 none */         /* that popup is up now */
void rfmv_menu(rfm_move_t *m, int what, int i);         /* a held Track button's menu: what changed */
void rfmv_cond_name(int c, char *b, int cap);
const char *rfmv_chord_name(const uint8_t *n, int k, char *out, int cap);   /* "Cmaj7", "C/E", "" */           /* "1:2", "FILL" .. */
void rfmv_tri(mr_canvas_t *cv, int x, int y, int w, int h, int on);   /* a play triangle, pointing right */
const char *rfmv_note_name(int n, char *b, int cap);
void rfmv_fm1_art(const rfm_move_t *m, mr_canvas_t *cv);   /* FM-1 mode's left half: the owner's drawing, live */

/* ---- seq.c: the data model (sorted arrays), the journal, the clock, the engine */
#define SEQ_CLIP(track, scene) ((track) * RFM_MOVE_SEQ_SCENES + (scene))
enum { SEQ_NONE = -2, SEQ_STOP = -1 };
enum { OP_NOTE_ADD = 1, OP_NOTE_DEL, OP_LOCK, OP_LEN, OP_LSTART, OP_CSET, OP_MADE, OP_ARP };   /* (0.14.0 OP_ARP: a byte
                                                         * of a track's arp: n.clip the track, n.step the byte) */   /* (0.12.0: a clip's launch settings) */
#define SEQ_NOLOCK 0xFF

void rfmv_seq_init(rfm_move_t *m);                     /* an empty project, the clock reset */
void rfmv_seq_project_clear(rfm_move_t *m);            /* the project's data only */
int rfmv_seq_range(const rfm_move_t *m, int clip, int step, int *lo);   /* notes at (clip, step): [lo, ret) */
int rfmv_seq_clip_range(const rfm_move_t *m, int clip, int *lo);       /* notes of the clip */
int rfmv_seq_lock_range(const rfm_move_t *m, int clip, int step, int *lo);
int rfmv_seq_clip_lock_range(const rfm_move_t *m, int clip, int *lo);
int rfmv_seq_find(const rfm_move_t *m, int clip, int step, int note);  /* index, -1 */
int rfmv_seq_lock_get(const rfm_move_t *m, int clip, int step, int cc); /* value, -1 */
int rfmv_seq_clip_used(const rfm_move_t *m, int clip);                 /* notes or locks */
int rfmv_seq_track_used(const rfm_move_t *m, int track);
int rfmv_seq_clip_has(const rfm_move_t *m, int clip);  /* used, or made (an empty clip shown as a clip) */
void rfmv_seq_clip_make(rfm_move_t *m, int clip, int on);
int rfmv_seq_loop_lo(const rfm_move_t *m, int clip);   /* the loop: [lo, hi) */
int rfmv_seq_loop_hi(const rfm_move_t *m, int clip);
int rfmv_seq_tps(int rate, int feel);                  /* ticks a step at a rate and feel */
void rfmv_seq_rate_label(int rate, int feel, char *o, int cap);   /* (0.14.0) "1/16", "1/8D", "1/16T", "1 Bar T" */
void rfmv_seq_feel_set(rfm_move_t *m, int track, int feel);   /* (0.14.0) a track's feel (from its next step) */
int rfmv_seq_dead(const rfm_move_t *m, int track, int step);   /* (0.14.0) a Triplet track's fourth step of a group */
int rfmv_seq_live_below(int tri, int k);               /* .. the live steps before step k (tri 0: k) */
int rfmv_seq_live_key(int tri, int i);                 /* .. the i-th live step (tri 0: i) */
/* edits (journaled into the current undo group, the project marked changed) */
void rfmv_seq_group(rfm_move_t *m);                    /* a new undo group: one gesture */
int rfmv_seq_note_put(rfm_move_t *m, int clip, int step, int note, int vel, int len, int micro);   /* add/replace */
int rfmv_seq_note_put_w(rfm_move_t *m, int clip, int step, int note, int vel, int len, int micro, int kw);   /* .. with
                                                         * its kw (1: the replaced note's, else 0; a kit track's only) */
int rfmv_seq_note_copy(rfm_move_t *m, int clip, int step, const rfm_move_note_t *n);   /* n's copy (its kw too) there */
int rfmv_seq_note_del(rfm_move_t *m, int clip, int step, int note);
void rfmv_seq_step_clear(rfm_move_t *m, int clip, int step);
int rfmv_seq_lock_set(rfm_move_t *m, int clip, int step, int cc, int val);   /* val -1: remove */
void rfmv_seq_len_set(rfm_move_t *m, int clip, int len);
void rfmv_seq_loop_set(rfm_move_t *m, int clip, int lo, int len);   /* the loop's start and length (steps) */
void rfmv_seq_preview(rfm_move_t *m, int clip, int step);   /* a step's notes sound once (a held step) */
int rfmv_seq_trig(const rfm_move_t *m, int clip, int step, int id);   /* a trig parameter (RFM_MOVE_TRIG_*), -1 none */
void rfmv_seq_clip_clear(rfm_move_t *m, int clip);
void rfmv_seq_clip_quantize(rfm_move_t *m, int clip);
int rfmv_seq_clip_double(rfm_move_t *m, int clip);
void rfmv_seq_note_remove_all(rfm_move_t *m, int clip, int note);
int rfmv_seq_undo(rfm_move_t *m);                      /* 1 something undone */
int rfmv_seq_redo(rfm_move_t *m);
/* the clipboard: copy (kind 1 step, 2 clip) into it, paste from it */
void rfmv_seq_copy_step(rfm_move_t *m, int clip, int step);
void rfmv_seq_copy_clip(rfm_move_t *m, int clip);
void rfmv_seq_cset_set(rfm_move_t *m, int clip, int v); /* (0.12.0) a clip's launch settings (journaled) */
void rfmv_seq_clip_delete(rfm_move_t *m, int clip);
int rfmv_seq_lq_of(const rfm_move_t *m, int track, int scene);   /* (0.12.0) the launch quantization a launch uses */
const char *rfmv_lq_name(int lq);                       /* .. "1 Bar", "Instant" .. */
int rfmv_lq_step(int lq, int d);                        /* .. the next in the list (Instant .. Clip) */
int rfmv_seq_after_next(const rfm_move_t *m, int track, int scene);   /* .. After = Next's clip */    /* a clip deleted (playing: the track stops at the bar) */
void rfmv_seq_scene_copy(rfm_move_t *m, int scene);     /* (0.12.0) a row into the scene buffer */
void rfmv_seq_scene_paste(rfm_move_t *m, int scene);    /* .. the scene buffer onto a row (all 16 tracks) */
int rfmv_seq_scene_has(const rfm_move_t *m, int scene); /* .. a row has a clip */
void rfmv_seq_paste_step(rfm_move_t *m, int clip, int step);
void rfmv_seq_copy_step_add(rfm_move_t *m, int clip, int step);   /* (0.6.3) one more step of a group */
void rfmv_seq_paste_clip(rfm_move_t *m, int clip);
void rfmv_seq_track_copy(rfm_move_t *m, int from, int to);
void rfmv_seq_track_clear(rfm_move_t *m, int track);
void rfmv_seq_clip_dup(rfm_move_t *m, int from, int to);   /* Copy alone: the clip duplicated */
/* the transport */
void rfmv_seq_start(rfm_move_t *m);                    /* Play: now, or at the external clock's next bar */
void rfmv_seq_stop(rfm_move_t *m);                     /* everything sounding let go, recordings finished */
void rfmv_seq_count_in(rfm_move_t *m);                 /* Rec from stopped: start a bar from now */
void rfmv_seq_restart(rfm_move_t *m);                  /* Shift + Play: every clip from its start */
void rfmv_seq_release(rfm_move_t *m, int track);       /* the track's sounding notes off (-1 all) */
void rfmv_seq_launch(rfm_move_t *m, int track, int scene);   /* scene -1: stop the track */
void rfmv_seq_launch_scene(rfm_move_t *m, int scene);
void rfmv_seq_launch_now(rfm_move_t *m, int track, int scene);   /* a double tap: now, in phase with the bar */
void rfmv_seq_launch_scene_now(rfm_move_t *m, int scene);        /* (0.11.0) a scene's double click: now */
int rfmv_seq_stop_scene(rfm_move_t *m, int scene, int now);      /* (0.11.0) Shift + a scene: its clips stop; tracks stopped */
void rfmv_seq_mute(rfm_move_t *m, int track, int on);
void rfmv_seq_solo(rfm_move_t *m, int track, int on);
int rfmv_seq_groove_of(const rfm_move_t *m, int track);   /* Global (the project's) or the track's own */
int rfmv_seq_audible(const rfm_move_t *m, int track);  /* not muted, and soloed if any track is */
int rfmv_seq_now_tick(const rfm_move_t *m);            /* the song tick now (rounded to the nearest) */
int rfmv_seq_clip_pos(const rfm_move_t *m, int track); /* the active clip's step now, -1 not playing */
int rfmv_seq_clip_tick(const rfm_move_t *m, int track);   /* .. its tick in the clip now, -1 */
/* live pad notes in Sequencer mode: recording and capture */
void rfmv_seq_pad_on(rfm_move_t *m, int pad, int note, int vel);
void rfmv_seq_pad_on_w(rfm_move_t *m, int pad, int note, int vel, int kw);   /* (a kit note: its kw) */
void rfmv_seq_pad_off(rfm_move_t *m, int pad, int note);
int rfmv_seq_capture(rfm_move_t *m);                   /* the clip's scene, -1 nothing captured */
void rfmv_seq_tap(rfm_move_t *m, uint32_t now_ms);
void rfmv_seq_sample(rfm_move_t *m);                   /* Record (Capture mode, stopped): arm / done at the nearest bar */
void rfmv_seq_audio_rec(rfm_move_t *m);                /* Sample: the audio loop armed / ended at the next bar */
void rfmv_seq_ui_poll(rfm_move_t *m);
void rfmv_seq_select(rfm_move_t *m, int track);        /* a track selected: its layout and CC numbers in */
void rfmv_seq_menu_open(rfm_move_t *m, int sm);         /* (0.14.0) a Shift menu opened (Shift + a Track) */
void rfmv_seq_menu_close(rfm_move_t *m);                /* (0.15.0) .. closed (not Global settings) */
int rfmv_seq_kpage(const rfm_move_t *m);               /* (0.14.0) the knob page: 0 TRIG, 1 FM-1, 2..5 banks 1..4 */
void rfmv_seq_kpage_restore(rfm_move_t *m);            /* .. the selected track's, as it was left */
void rfmv_seq_enter(rfm_move_t *m);                    /* Sequencer mode entered / left: the settings swapped */
void rfmv_seq_leave(rfm_move_t *m);
void rfmv_seq_sync(rfm_move_t *m);
void rfmv_seq_load_track(rfm_move_t *m);              /* .. and back (the selected track's, or Global's) */                     /* the selected track's layout and CC numbers from the settings */
void rfmv_seq_arp_default(rfm_move_arp_t *a);
int rfmv_seq_arp_rate_of(int i);                       /* the arp's RATE: notes a bar */
int rfmv_seq_arp_on(rfm_move_t *m, int note, int vel); /* the selected track's arp takes a pad note: 1 */
void rfmv_seq_arp_off(rfm_move_t *m, int track, int note);   /* (0.14.0: the track the pad played) */
void rfmv_seq_arp_stop(rfm_move_t *m);                  /* (0.14.0) the pads' notes out of every arp; the clips' stay */
void rfmv_seq_arp_stop_t(rfm_move_t *m, int tr);
void rfmv_seq_arp_put(rfm_move_t *m, int tr, const rfm_move_arp_t *a);   /* (0.14.0) track tr's arp set to a, undoable
                                                         * (the current undo group) */       /* (0.14.0) track tr's arp emptied (-1: every track's) */                  /* (advance) a step held long enough: its popup */
void rfmv_seq_dirty(rfm_move_t *m);

/* ---- project.c */
void rfmv_proj_open(rfm_move_t *m, int slot);          /* grid: load (current saved first) or a new empty one */
void rfmv_proj_delete(rfm_move_t *m, int slot);
void rfmv_proj_save_now(rfm_move_t *m);                /* PROJECT_SAVE for the current slot */
void rfmv_proj_save_own(rfm_move_t *m);                /* .. value 1: the player's own save (the firmwares now too) */
void rfmv_proj_poll(rfm_move_t *m);                    /* the autosave's debounce (from advance) */
void rfmv_proj_info(const rfm_move_t *m, int slot, rfm_move_proj_info_t *o);   /* the current one: live */

/* ---- seq_ui.c: Sequencer mode */
enum { SEQ_M_SHIFT = 1, SEQ_M_COPY = 2, SEQ_M_DELETE = 4, SEQ_M_MUTE = 8, SEQ_M_LOOP = 16 };
enum { SEQ_MSG_NONE = 0, SEQ_MSG_NOCLIP, SEQ_MSG_FULL, SEQ_MSG_LOADFAIL, SEQ_MSG_CAPTURED, SEQ_MSG_UNDO,
       SEQ_MSG_REDO, SEQ_MSG_COPIED, SEQ_MSG_PASTED, SEQ_MSG_CUT, SEQ_MSG_QUANT, SEQ_MSG_DOUBLE, SEQ_MSG_EXTTAP,
       SEQ_MSG_DUP, SEQ_MSG_CLEARED, SEQ_MSG_CAPCLEAR, SEQ_MSG_CLK_AUTO, SEQ_MSG_CLK_INT, SEQ_MSG_CLK_EXT,
       SEQ_MSG_FULLVEL_ON, SEQ_MSG_FULLVEL_OFF, SEQ_MSG_METRO_ON, SEQ_MSG_METRO_OFF, SEQ_MSG_NEWCLIP, SEQ_MSG_CLIPCLEAR,
       SEQ_MSG_NOTROUTED, SEQ_MSG_NOUNDO };                             /* (0.12.0: a select step held + volume, a track that reaches none) */
void rfmv_seq_ui_release(rfm_move_t *m);
void rfmv_seq_srec_now(rfm_move_t *m);                 /* Capture, playing, the clip empty: Rec records at once */
#define RFM_MOVE_TRIG_NLENS 17                         /* the TRIG page's lengths (seq_ui.c LENS) */
void rfmv_seq_tdef_default(uint8_t tdef[8]);           /* the TRIG page's defaults: as a step has none */
int rfmv_seq_tdef_val(const rfm_move_t *m, int k);     /* the selected track's default for TRIG knob k (trig_val's units) */
void rfmv_seq_tdef_apply(rfm_move_t *m, int clip, int abs, int entry);   /* a new step: the defaults (entry: micro, length too) */
int rfmv_seq_src_step(const rfm_move_t *m, int clip, int abs);   /* the clipboard's source: 1 copied, 2 cut, 0 */
int rfmv_seq_src_clip(const rfm_move_t *m, int clip);
int rfmv_seq_src_page(const rfm_move_t *m, int clip, int pg);   /* Loop mode's page clipboard: 1 copied, 2 cut */
int rfmv_pick_layout(rfm_move_t *m, int p);
int rfmv_seq_len_index(int len);                       /* the TRIG page's lengths: the nearest, the length of */
int rfmv_seq_len_of(int i);
int rfmv_seq_len_inf(int len);
int rfmv_seq_trig_val(const rfm_move_t *m, int clip, int abs, int k);   /* the TRIG page's knob k on a step */            /* a held Track button's pad: layout (top row), scale (row 2) */               /* leaving the mode / release-all: held things let go */
void rfmv_seq_pad(rfm_move_t *m, int p, int vel, uint32_t now);
void rfmv_seq_step(rfm_move_t *m, int s, int down, uint32_t now);
int rfmv_seq_button(rfm_move_t *m, int cc, int down, uint32_t now);   /* 1 taken */
int rfmv_seq_volume(rfm_move_t *m, int d);             /* the volume knob: 1 taken (a step held: velocity) */
void rfmv_seq_knob(rfm_move_t *m, int k, int d, uint32_t now);
void rfmv_seq_jog(rfm_move_t *m, int d, uint32_t now);
int rfmv_seq_menu_click(rfm_move_t *m);                 /* (0.13.0) a Shift menu's jog click: edit / select */
int rfmv_seq_menu_sel(const rfm_move_t *m);
int rfmv_seq_menu_editing(const rfm_move_t *m);
void rfmv_shift_icons(const rfm_move_t *m, uint8_t icon[16]);   /* the Shift layer's icon LEDs (0 off) */
void rfmv_seq_leds(const rfm_move_t *m, rfmv_rgb_t c[L_WHITE0], uint8_t white[13]);
void rfmv_seq_screen(const rfm_move_t *m, mr_canvas_t *cv);
int rfmv_seq_held_icon(const rfm_move_t *m);
int rfmv_seq_cs_on(const rfm_move_t *m);                /* (0.12.0) a select step held: its clip's settings shown */
int rfmv_seq_cs_touch(rfm_move_t *m, int k);
int rfmv_seq_cs_knobs(const rfm_move_t *m);             /* .. its knobs lit (bits), -1 not shown */            /* .. a knob touched while it is held: 1 taken */
void rfmv_seq_cs_words(const rfm_move_t *m, int k, char *o, int cap);   /* .. words (k -1: the whole screen) */
int rfmv_fw_level_turn(rfm_move_t *m, int slot, int d);
int rfmv_fw_level_shown(const rfm_move_t *m);
int rfmv_seq_loop_pad_n(int p);                        /* (0.13.0) the Loop layer's pads: 1..16 steps, 0 none */
int rfmv_seq_loop_steps(const rfm_move_t *m);          /* (0.13.0) the loop length popup with its bar's steps */          /* (0.13.0) a slot's level popup is up */ /* (0.12.0) a slot's level (the Mixer's), its popup; 0 empty */            /* (0.12.0) Delete held: 1 (scissors), Copy: 2 */
void rfmv_seq_track_menu(const rfm_move_t *m, mr_canvas_t *cv);   /* Track 4 held */
void rfmv_seq_msg(rfm_move_t *m, int msg);
const char *rfmv_seq_msg_text(const rfm_move_t *m);    /* the message up now, NULL none */

/* ---- fw.c: the firmware browser, FX setup, the library (0.4.0) */
/* the view's popups (rfm_move_fw_state_t.pop: one at a time, RFM_MOVE_POPUP_MS after the last touch) */
enum { FP_NONE, FP_TRACK, FP_LEVEL, FP_CATALOG, FP_UNLOAD, FP_LOADED, FP_FILTER, FP_BANK, FP_FAV, FP_COLOR, FP_MSG,
       FP_STATE, FP_RPAGE, FP_PARAM, FP_RFILTER, FP_RLOADED, FP_MROUTE, FP_ORDER };   /* (FP_ORDER 0.6.9: a
                                                         * favourite moved: pop_a it, pop_b its place 1..) */   /* (FP_MROUTE 0.6.6: a MIDI route toggled:
                                                         * pop_a the source, pop_b target | on << 4 | refused << 5) */
void rfmv_fw_init(rfm_move_t *m);
void rfmv_fw_enter(rfm_move_t *m);                     /* the projects view entered: the track to show */
void rfmv_view_input(rfm_move_t *m, const mr_event_t *ev, uint32_t now);   /* the projects view's input */
void rfmv_fw_leds(const rfm_move_t *m, rfmv_rgb_t c[L_WHITE0], uint8_t white[13]);
int rfmv_fw_screen(const rfm_move_t *m, mr_canvas_t *cv);   /* 1: drawn (a firmware track, FX setup, a popup) */
int rfmv_fw_timed(const rfm_move_t *m);
int rfmv_fw_volume(rfm_move_t *m, int d);              /* Track 1..3 held + volume: the slot's level (1 taken) */
void rfmv_fw_poll(rfm_move_t *m);                      /* the library's debounce (from advance) */
void rfmv_emit_midi_t(rfm_move_t *m, int track, int status, int d1, int d2);   /* MIDI tagged with its track 1..16 */
void rfmv_fxset_default(rfm_move_fxset_t *x);
void rfmv_fxset_write(const rfm_move_fxset_t *x, uint8_t *p);   /* RFM_MOVE_FXSET_BYTES */
int rfmv_fxset_check(const uint8_t *p, int nv);        /* 0 ok; nv: values a slot (8 version 5, 128 version 6) */
void rfmv_fxset_read(rfm_move_fxset_t *x, const uint8_t *p, int nv);
int rfmv_mroute_check(const uint8_t m[RFM_MOVE_FW_SLOTS]);   /* (0.6.6) 0: the MIDI routes valid (no own bit, no cycle) */
void rfmv_fx_resolve(rfm_move_t *m, rfm_move_fxslot_t *x, int kind);
void rfmv_fx_loaded(rfm_move_t *m);                    /* a project loaded: modules found, the host told */
int rfmv_fw_bank(const rfm_move_t *m, int ids[32]);   /* (tests) the bank shown, its size */
int rfmv_fw_list_len(const rfm_move_t *m);
int rfmv_fw_rec(const rfm_move_t *m, int fw);          /* the library record of list index fw, -1 */
int rfmv_fw_switch(rfm_move_t *m, int slot);           /* (0.4.3) Back held + Track 1..3: that firmware played; 1 done */
void rfmv_fw_slot_name(const rfm_move_t *m, int slot, char *o, int cap);
void rfmv_latch_drop_all(rfm_move_t *m);               /* (0.5.3) every latched note let go */
void rfmv_fw_view_pop(rfm_move_t *m);                 /* (0.5.2) the view named: Back's flip in, Menu */
int rfmv_fw_loaded(const rfm_move_t *m);                /* loaded slots (0 with no host list) */
int rfmv_seq_layer_open(const rfm_move_t *m);          /* (0.4.3) a layer Back closes is open (any mode) */
/* (tests, 0.4.2) the filter popup's words for a step (0: none): its name, its state, what the list shows (lines) */
int rfmv_fw_filter_text(const rfm_move_t *m, int step, char name[24], const char **state, char rows[3][22]);
/* (0.6.0) the projects view's words (speech.c): the base (its subject, 0 not the view), the selection, a popup (1 up) */
int rfmv_fw_say_base(const rfm_move_t *m, char *o, int cap);
void rfmv_fw_say_focus(const rfm_move_t *m, char *o, int cap);
int rfmv_fw_say_pop(const rfm_move_t *m, char *o, int cap);

/* ---- speech.c (0.6.0): the words said when the screen changes (called last by the calls that change it) */
void rfmv_say_check(rfm_move_t *m);
void rfmv_say_press(rfm_move_t *m, int slot);          /* (0.6.1) an FM-1 button pressed: its words (label slot) */
void rfmv_say_text(rfm_move_t *m, const char *text);   /* (0.6.2) words said at the call's end (speech on) */
void rfmv_say_text_always(rfm_move_t *m, const char *text);   /* (0.6.12) .. in any speech mode (Off too) */
void rfmv_say_readout(rfm_move_t *m, int slot, int expl);   /* (0.6.7) a knob readout stored: its label kept
                                                         * (always); expl (0.6.9): the host named its control */
void rfmv_say_turned(rfm_move_t *m);                   /* (0.6.7) an FM-1 knob turned: its readout awaited */
int rfmv_global_input(rfm_move_t *m, const mr_event_t *ev);   /* (0.6.2) Global settings open: it takes the input
                                                         * (1 taken; releases pass, so nothing stays held) */
int rfmv_global_rows(const rfm_move_t *m);              /* (tests) its rows (Exit last with RFM_MOVE_CAP_EXIT) */
void rfmv_global_words(const rfm_move_t *m, char *o, int cap);   /* its row selected, in words */
void rfmv_global_battery_words(const rfm_move_t *m, char *o, int cap);   /* (0.6.10) "battery 87%" */
const char *rfmv_seq_smenu_name(int sm);               /* (0.6.1, seq_ui.c) a Shift menu's name, its knobs' words */
void rfmv_seq_smenu_words(const rfm_move_t *m, int k, char *o, int cap);
void rfmv_say_tick(rfm_move_t *m);                     /* (rfm_move_seq_advance: the transport's start and stop, a
                                                         * held button's label once it shows; nothing else) */

/* ---- takeover.c (0.7.0): a firmware's own Move interface */
int rfmv_tk_mode(const rfm_move_t *m);                  /* the active slot is in its takeover (shown or not) */
int rfmv_tk_on(const rfm_move_t *m);                    /* .. and it is shown (no projects view, no Shift menu) */
unsigned rfmv_tk_claims(const rfm_move_t *m);           /* the claims in force now (0: none shown) */
const char *rfmv_tk_name(const rfm_move_t *m);          /* the active slot's takeover's name */
void rfmv_tk_set(rfm_move_t *m, int on);                /* the active slot's takeover opened / left (FM-1 mode), named */
void rfmv_tk_mode_pop(rfm_move_t *m);                   /* the mode's name popup (the takeover's, or POP_MODE) */
int rfmv_tk_route(rfm_move_t *m, const mr_event_t *ev);   /* input: 1 taken (the firmware's, or volume + Back) */
void rfmv_tk_sync(rfm_move_t *m);                       /* TK_STATE (and releases) when what is shown changed */
void rfmv_tk_release(rfm_move_t *m);                    /* the firmware's held controls let go */
void rfmv_tk_leds(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3]);   /* the claimed LEDs, the firmware's */

/* move.c, project.c for fw.c */
void rfmv_master_turn(rfm_move_t *m, int d);           /* the volume knob as MASTER */
void rfmv_proj_back(rfm_move_t *m);                    /* Back in the projects view: out */

/* ---- (0.8.0) the Track buttons, proposal 2 (move.c; fw.c's view opened and closed from them) */
void rfmv_slot_push(rfm_move_t *m);                    /* the active slot's mirror (s.layout ..) into s.slot[] */
void rfmv_slot_pull(rfm_move_t *m);                    /* .. and back (a layout played again) */
int rfmv_slot_lay(const rfm_move_t *m, int slot);
int rfmv_set_slot_lay(rfm_move_t *m, int slot, int lay);   /* a slot's layout set (its menu's top row) */      /* RFM_MOVE_LAY_* it plays (TK with no takeover: FM1) */
int rfmv_rd_valid(const rfm_move_t *m, int sl);        /* (0.14.0) knob slot sl's readout is this page's (the FM-1
                                                         * buttons held now, the knob's label now) */
void rfmv_slot_leaving(rfm_move_t *m);                 /* (fw.c) the active slot changes: what is held let go */
void rfmv_slot_changed(rfm_move_t *m);                 /* (fw.c) .. it changed: its layout played */
int rfmv_fw_activate(rfm_move_t *m, int slot);          /* (fw.c) a Track's tap: that slot played (FW_ACTIVE), any
                                                         * slot the host offers, loaded or not; 1 changed */
rfmv_rgb_t rfmv_fw_rgb(const rfm_move_t *m, int slot);  /* a slot's colour (0.12.0: its own, not its firmware's; empty:
                                                         * white) */
extern const rfmv_rgb_t RFMV_SLOT_RGB[RFM_MOVE_FW_SLOTS]; /* (0.12.0) orange, blue, yellow */
int rfmv_fw_step_icons(const rfm_move_t *m, uint8_t icon[16]);   /* (0.12.0) the browser's filter icons; 0 not up */
int rfmv_proj_knob(const rfm_move_t *m);                /* (0.12.0) the project grid's knob 1 has a job (a pad held) */
const char *rfmv_proj_colour_name(int c);               /* .. a project colour's name (1..16) */
int rfmv_back_has(const rfm_move_t *m);                 /* (0.12.0) Back's click has a layer to close */
#define RFMV_SOLO_RGB ((rfmv_rgb_t){ 170, 60, 255 })    /* (0.12.0) solo: purple (blue is slot 2's) */
#define RFMV_ACTIVE_ON_MS 700                               /* (0.14.0) the active state / open project: white so long */
#define RFMV_ACTIVE_OFF_MS 300                              /* .. then a third as bright so long */
rfmv_rgb_t rfmv_active_rgb(uint32_t ms);                    /* (0.14.0) white, breathing, ms into its beat */
int rfmv_fw_nslots(const rfm_move_t *m);                /* the slots the Track buttons play (1: no list, one slot) */
enum { VIEW_SLOT0 = 0, VIEW_PROJECTS = 3, VIEW_MIXER = 4, VIEW_ROUTING = 5 };
void rfmv_view_open(rfm_move_t *m, int what);           /* the projects view on a slot's browser, the projects, FX */
void rfmv_view_close(rfm_move_t *m);                    /* .. left, as Back leaves it */
int rfmv_fx_ok(const rfm_move_t *m);                    /* the host has FX setup (the Mixer, Routing) and a list */
void rfmv_route_toggle(rfm_move_t *m, int slot, int track);   /* a sequencer track to a firmware slot or not */
void rfmv_route_on(rfm_move_t *m, int slot, int track);       /* (0.9.0) .. routed (selected for the slot) */
int rfmv_fw_solo(rfm_move_t *m, int slot, int track);         /* (0.9.0) the slot's solo toggled: 1 on */
rfmv_rgb_t rfmv_lock_rgb(const rfm_move_t *m, int slot, int track);   /* (0.9.0) a track's step in the lock menu */
int rfmv_fw_activate_keep(rfm_move_t *m, int slot);     /* (0.11.0, fw.c) session view: the slot played, the view kept */
void rfmv_seq_tk_flush(rfm_move_t *m);                  /* (0.11.0) a held Track's waiting click acts now */
void rfmv_seq_route_pad(rfm_move_t *m, int fw, int p);  /* (0.11.0) session, a Track held: a column's pad */
void rfmv_seq_route_leds(const rfm_move_t *m, int fw, rfmv_rgb_t c[L_WHITE0]);   /* .. its pads, steps and knobs */
int rfmv_sess_on(const rfm_move_t *m);                  /* (0.11.0) session view is what the pads show */
void rfmv_sv_toggle(rfm_move_t *m);                     /* (0.12.0) session view in / out, from any layout */
void rfmv_seq_arp_toggle(rfm_move_t *m);
void rfmv_seq_arp_click(rfm_move_t *m);
uint32_t rfmv_ui_rnd(rfm_move_t *m);                    /* (0.14.0) the interface's dice: the next number */                 /* (0.14.0) the arp menu's jog click: Delete held reset, else toggle / random */                /* (0.11.0) the arp menu's jog click: on / off */
const char *rfmv_seq_arp_mode_name(int mode);           /* (0.12.0) "Off", "On", "First" .. */
int rfmv_seq_arp_silent(const rfm_move_t *m, int tr);   /* (0.14.0) on, but it can never sound (CHANCE 0, MOD all rests) */
void rfmv_seq_al_take(rfm_move_t *m);                   /* (0.12.0) a held arp lane step takes the pads */
int rfmv_seq_al_holding(const rfm_move_t *m);           /* .. a lane step is held: a pad press is its shortcut */
void rfmv_seq_oct_range(const rfm_move_t *m, char *o, int cap);   /* (0.12.0) the pads' notes: "C2 - G4" */
void rfmv_seq_held_names(const rfm_move_t *m, char *o, int cap, int px);   /* a held step's notes by name (px: the
                                                         * width at 2x, 0 any: the first ones and "+n") */
void rfmv_seq_scene_pop(const rfm_move_t *m, mr_canvas_t *cv);   /* (0.11.0) session's knob 8 touched: the scenes */
void rfmv_slot_screen(const rfm_move_t *m, mr_canvas_t *cv, int slot, int changed);   /* (0.11.0, screen.c) a slot's
                                                         * summary: its firmware, layout, key, latch, the 16 tracks */
int rfmv_t4_page(const rfm_move_t *m);                  /* Track 4's page: 0 the Mixer, 1 Routing */
enum { POP_LAYOUT = 40, POP_JOG, POP_T4, POP_CCNUM, POP_LOCK, POP_SEQ, POP_SLOT, POP_VIEW, POP_ARPW, POP_OCT };   /* (0.12.0:
                                                         * POP_ARPW the arp switched on / off, POP_OCT the octave) */   /* (popups,
                                                         * rfm_move_t.pop; 0.11.0: POP_SLOT a slot's summary (session
                                                         * view's Track tap), POP_VIEW the view named under a menu) */
enum { LK_LOCKED, LK_SOLO, LK_UNSOLO, LK_OFF, LK_ON, LK_MUTE, LK_UNMUTE, LK_CLEARED };   /* the lock menu's word */
void rfmv_pop_screen(mr_canvas_t *cv, const char *top, const char *big, int n, int cur, const char *bottom);
/* ---- (2026-11-26, the owner's: "each volume level adjustment feels like it's all part of the whole") one level popup
 * and one unit for every level (screen.c). A 0..127 level (a firmware slot's, the kit's strip) is a linear gain, 127
 * unity (the host's pan_gains and kit_render_bus: level / 127), so its dB is 20 log10(v / 127): 100 = -2.1 dB, 0 = -inf.
 * A pad's level is whole dB (-60 = -inf .. +6). Levels are shown in tenths of a dB (RFMV_DB_INF: -inf). */
#define RFMV_DB_INF (-32000)
int rfmv_lvl_db10(int v);                               /* a 0..127 level in tenths of a dB */
int rfmv_lvl_step(int v, int d);                        /* .. d detents on: 0.5 dB a detent (at least one step) */
void rfmv_db_text(char *o, int cap, int db10, int unit);   /* "-3.5 dB", "0 dB", "+2.0 dB", "-12 dB", "-inf dB" (unit
                                                         * 0: no " dB") */
double rfmv_db_frac(int db10);                          /* a level bar's length: -40 .. +6 dB as 0 .. 1 */
/* the level popup: the title ("Track 1 Level"), the value large in dB, the source's name, a bar per sibling (n, the one
 * set cur) along the bottom */
void rfmv_level_popup(mr_canvas_t *cv, const char *title, int db10, const char *name, const int *sib, int n, int cur);
int rfmv_fw_track_levels(const rfm_move_t *m, int sib[4]);   /* (fw.c) the slots' levels (dB10), the kit's a fourth:
                                                         * how many */
const char *rfmv_lay_name(const rfm_move_t *m, int slot, int lay);   /* "FM-1", "Chords", "Scale" .. */
int rfmv_lay_menu(const rfm_move_t *m);                 /* the firmware Track held (its layout menu): slot + 1, 0 */
int rfmv_t4_menu(const rfm_move_t *m);                  /* Track 4 held, its menu (not the Sequencer's tracks) */
void rfmv_track_leds(const rfm_move_t *m, rfmv_rgb_t c[L_WHITE0]);   /* (fw.c) Track 1..4 everywhere */
const char *rfmv_jog_name(int i);                       /* "SELECT", "PRESET", "ALGO" */

/* ---- (0.13.0) projfw.c: a project holds its firmwares; States as a layer (RFM_MOVE_CAP_PROJFW) */
enum { PFW_WAIT = -3, PFW_EMPTY = 101 };                /* (pfw_st: no news yet; an empty slot: nothing to load) */
#define RFMV_ST_SWEEP_MS 50                             /* the opening sweep: a pad column this long */
int rfmv_pfw_cap(const rfm_move_t *m);                  /* the host has RFM_MOVE_CAP_PROJFW */
int rfmv_pfw_check(const uint8_t *p);                   /* a version 9 part (RFM_MOVE_PFW_BYTES) valid: 0 */
void rfmv_pfw_write(rfm_move_t *m, uint8_t *p);         /* .. written (a save) */
void rfmv_pfw_ids(const uint8_t *p, char ids[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX]);   /* .. its ids */
void rfmv_pfw_saved_ids(const rfm_move_t *m, char ids[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX]);   /* what a save keeps */
void rfmv_pfw_loaded(rfm_move_t *m, int slot, const uint8_t *part);   /* a project loaded (part NULL: none, v1..8) */
void rfmv_pfw_load_failed(rfm_move_t *m, int slot);     /* .. its blob refused */
void rfmv_pfw_press(rfm_move_t *m, int slot);           /* a stored project's pad: the loading screen at once */
void rfmv_pfw_new(rfm_move_t *m, int slot);             /* a new project: its slots emptied */
void rfmv_pfw_new_ids(rfm_move_t *m, int slot, const char ids[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX]);   /* (0.14.0)
                                                         * .. its slots these firmwares ("" empty): asked for, loading */
int rfmv_fw_pick_installed(rfm_move_t *m, char ids[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX]);   /* (0.14.0) up to three
                                                         * different installed firmwares at random: how many */
int rfmv_proj_first_empty(const rfm_move_t *m);         /* (0.14.0) the first empty project slot (not the open one), -1 */
void rfmv_pfw_defaults(rfm_move_t *m);                  /* .. its routing (1-4, 5-8, 9-12), channels, Tracks' tracks */
void rfmv_pfw_first(rfm_move_t *m, uint32_t used);      /* rfm_move_project_dir: none stored: the open one is new */
int rfmv_pfw_play(rfm_move_t *m);                       /* the grid's Play while loading: 1 taken (waits) */
void rfmv_pfw_poll(rfm_move_t *m);                      /* (advance) the time limit, the messages, the edits */
void rfmv_pfw_watch(rfm_move_t *m);                     /* (input) the firmware settings changed: the project edited */
int rfmv_pfw_screen(const rfm_move_t *m, mr_canvas_t *cv);   /* the loading / missing screen: 1 drawn */
int rfmv_pfw_timed(const rfm_move_t *m);
void rfmv_pfw_track_leds(const rfm_move_t *m, rfmv_rgb_t c[L_WHITE0]);
void rfmv_pfw_rows(const rfm_move_t *m, mr_canvas_t *cv, const char ids[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX],
                   const int16_t *st, int y0);          /* a row a slot: its number, the name; st: the bars */
int rfmv_pfw_words(const rfm_move_t *m, char *o, int cap, uint16_t *subj, uint32_t *stamp);   /* 1 up */
void rfmv_pfw_proj_words(const rfm_move_t *m, const rfm_move_proj_info_t *in, char *o, int cap);   /* ", ChoralRoot .." */
/* ---- sampler.c: (0.15.0) the sampler app (RFM_MOVE_CAP_SAMPLER) */
void rfmv_smp_defaults(int8_t v[RFM_MOVE_SMP_N]);       /* the settings' defaults */
void rfmv_smp_load(int8_t v[RFM_MOVE_SMP_N], const uint8_t *p);   /* a version 8 image's bytes, each checked */
int rfmv_smp_cap(const rfm_move_t *m);                  /* the host has RFM_MOVE_CAP_SAMPLER */
void rfmv_smp_caps(rfm_move_t *m, int was);             /* the caps set (was: the sampler's before): SETs, or off */
int rfmv_smp_shown(const rfm_move_t *m);                /* the app is on the screen (open, nothing above it) */
int rfmv_smp_covered(const rfm_move_t *m);              /* .. but a held control or a newer popup under it shows */
int rfmv_smp_input(rfm_move_t *m, const mr_event_t *ev);   /* 1 taken (Sample; the jog while it shows) */
int rfmv_smp_back(rfm_move_t *m);                       /* Back's click: 1 taken (a popup, the tab choice, the app) */
void rfmv_smp_leds(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3]);   /* Sample's light */
int rfmv_smp_screen(const rfm_move_t *m, mr_canvas_t *cv);   /* 1 drawn */
int rfmv_smp_timed(const rfm_move_t *m);
int rfmv_smp_tick(rfm_move_t *m);                       /* (speech) a message's second over: 1 once (words due) */
void rfmv_smp_poll(rfm_move_t *m);                      /* (stage 2, the clock) Trim's zoom after a knob's turn ends */
int rfmv_smp_pop_words(const rfm_move_t *m, char *o, int cap, uint16_t *subj, uint32_t *stamp);   /* 1 up */
int rfmv_smp_lvpx(int level, int w);                    /* (tests) a level 0..32767 as pixels of a -60..0 dB scale */
/* ---- kit.c: (stage 3a) the kit (RFM_MOVE_CAP_KIT) */
int rfmv_kit_cap(const rfm_move_t *m);
void rfmv_kit_init(rfm_move_t *m);                      /* (rfm_move_init) an empty kit */
void rfmv_kit_caps(rfm_move_t *m, int was);             /* the caps set (was: the kit's before) */
int rfmv_kit_active(const rfm_move_t *m);               /* the Kit view is the view (the sequencer's track view) */
int rfmv_kit_shown(const rfm_move_t *m);                /* .. and its screen shows (no menu, States, sampler over it) */
/* (2026-11-26, the owner's) the Kit view's Layout menu sets the right half's mode: it (-1: not the Kit view), a turn,
 * its name */
int rfmv_kit_rh_of(const rfm_move_t *m);
void rfmv_kit_rh_turn(rfm_move_t *m, int d);
const char *rfmv_kit_rh_name(int rh);
void rfmv_kit_open(rfm_move_t *m);                      /* Track 4's Kit view */
void rfmv_kit_close(rfm_move_t *m, int restore);        /* .. left (restore: what it was opened over) */
void rfmv_kit_to(rfm_move_t *m, uint32_t take);         /* the sampler's To Kit: the take waits for its pad */
int rfmv_kit_input(rfm_move_t *m, const mr_event_t *ev);   /* 1 taken */
int rfmv_kit_back(rfm_move_t *m);                       /* Back's click: 1 taken (the view shown) */
int rfmv_kit_back_has(const rfm_move_t *m);             /* .. it has something to close */
void rfmv_kit_leds(const rfm_move_t *m, rfmv_rgb_t c[L_WHITE0], uint8_t wv[13]);   /* pads, steps, knobs */
void rfmv_kit_leds_w(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3]);   /* Sample, Track 4 */
int rfmv_kit_screen(const rfm_move_t *m, mr_canvas_t *cv);   /* 1 drawn */
int rfmv_kit_timed(const rfm_move_t *m);
void rfmv_kit_poll(rfm_move_t *m);                      /* (advance) */
void rfmv_kit_seq_note(rfm_move_t *m, int track, int note, int vel, int kw);   /* (seq.c) a note of a track (vel 0: off) */
void rfmv_kit_seq_ghosts(rfm_move_t *m, int track, int clip, int step);   /* (seq.c) a step passed: its ghost triggers */
int rfmv_kit_route_flip(rfm_move_t *m, int fw, int track);   /* a held Track's hold on 13..16: kit <-> firmware; 1 done */
int rfmv_kit_routed(const rfm_move_t *m, int track);
int rfmv_kit_route_set(rfm_move_t *m, int track, int on);   /* (3b) Routing's kit: track 12..15 to it (off the
                                                         * firmwares) or not; its state now */
rfmv_rgb_t rfmv_kit_row_rgb(int track);                 /* a kit track's colour (13 pink .. 16 violet) */
#define RFMV_KIT_DEAD (-128)
int rfmv_kit_right_semis(const rfm_move_t *m, int n);   /* the right half's pad n: semitones (its octave shift in),
                                                         * RFMV_KIT_DEAD dead */
void rfmv_kit_clear(rfm_move_t *m);                     /* (project.c) an empty kit, unrouted */
void rfmv_kit_new(rfm_move_t *m);                       /* .. a new project's: empty, tracks 13..16 routed to it */
int rfmv_kit_used(const rfm_move_t *m);                 /* .. anything to save (version 11) */
void rfmv_kit_write(const rfm_move_t *m, uint8_t *p);   /* .. RFM_MOVE_KIT_BYTES */
void rfmv_kit_to_text(const rfm_move_t *m, char *o, int cap);   /* (3b) the sampler's To Kit: "Kit Pad 8" */
int rfmv_kit_volume(rfm_move_t *m, int d);             /* (2026-11-26) Track 4 held + the volume knob: the kit's level */
int rfmv_kit_level_screen(const rfm_move_t *m, mr_canvas_t *cv);   /* .. its popup, a held pad's Level's: 1 drawn */
void rfmv_kit_mix_turn(rfm_move_t *m, int par, int d);  /* (3b) the Mixer's kit strip: a value (RFM_MOVE_MIXP_*) */
int rfmv_kit_check(const uint8_t *p, int ver);          /* .. 0 valid (version 11 or 12's) */
void rfmv_kit_read(rfm_move_t *m, const uint8_t *p, int ver);
void rfmv_kit_loaded_project(rfm_move_t *m);            /* .. loaded or made: the host told */
int rfmv_kit_base_words(const rfm_move_t *m, char *o, int cap);   /* (speech) */
int rfmv_kit_pop_words(const rfm_move_t *m, char *o, int cap, uint16_t *subj, uint32_t *stamp);
/* (pages v2) the Kit view's Trig page shows (the sequencer's TRIG page for the chosen pad's track): its first held
 * step 0..15, -1 none held; -2 not on it (screen.c's TRIG popup) */
int rfmv_kit_trig_first(const rfm_move_t *m);
void rfmv_kit_page_cells(const rfm_move_t *m, int *n, int *cur);   /* .. its pages shown, the one shown */
/* (seq_ui.c) the TRIG page's knob k turned d on the held steps (bits of the page shown) of clip, or (none held) the
 * selected track's defaults for new steps: its popup, as a firmware track's */
void rfmv_seq_trig_knob(rfm_move_t *m, int clip, int k, int d, uint16_t held);
void rfmv_mode_set(rfm_move_t *m, int mode);            /* (move.c) the mode (FM-1 / Sequencer) as a layout change */
void rfmv_seq_step_enter_w(rfm_move_t *m, int clip, int abs, int note, int vel, int kw);
void rfmv_seq_step_enter(rfm_move_t *m, int clip, int abs, int note, int vel);   /* (seq_ui.c) a note into a step as a
                                                         * tap enters it (the TRIG defaults, the loop grown) */
int rfmv_st_on(const rfm_move_t *m);                    /* the States layer open: its slot + 1, 0 */
int rfmv_st_pad7(const rfm_move_t *m, int t);           /* Track t held: its pad 7 (States) has a job */
void rfmv_st_open(rfm_move_t *m, int t, int from);      /* from 0 a held Track's pad 7, 1 the browser's Loop */
void rfmv_st_close(rfm_move_t *m);
int rfmv_st_input(rfm_move_t *m, const mr_event_t *ev);   /* 1 taken */
int rfmv_st_back(rfm_move_t *m);                        /* Back's click: 1 taken (the marks, then the layer) */
void rfmv_st_track(rfm_move_t *m, int t);               /* Track 1..3 tapped while open: that slot's States */
void rfmv_st_leds(const rfm_move_t *m, uint8_t w[RFM_MOVE_NLEDS][3]);
rfmv_rgb_t rfmv_st_active_rgb(const rfm_move_t *m);     /* (0.14.0) the active state's pad now */
int rfmv_st_screen(const rfm_move_t *m, mr_canvas_t *cv);   /* 1 drawn */
int rfmv_st_timed(const rfm_move_t *m);
int rfmv_st_base_words(const rfm_move_t *m, char *o, int cap);   /* its subject, 0 not open */
int rfmv_st_pop_words(const rfm_move_t *m, char *o, int cap, uint16_t *subj, uint32_t *stamp);   /* 1 up */
/* fw.c and seq_ui.c for projfw.c */
void rfmv_fw_id_name(const rfm_move_t *m, const char *id, char *o, int cap);   /* the list's name, else the id */
void rfmv_fw_slot_set(rfm_move_t *m, int s, const char *id);   /* the slot holds that firmware now ("" empty) */
int rfmv_fw_slot_loaded(const rfm_move_t *m, int s);
void rfmv_held_icon(mr_canvas_t *cv, int kind, int x, int y);   /* 1 scissors (Delete held), 2 two sheets (Copy) */
/* (0.12.x, the owner's) Back held + a button, in the sequencer's track and session views: Left home (bar 1; session:
 * track 1), Up (session) scene 1, Loop the whole clip, Play to bar 1, Delete every layer closed; the rest dead */
enum { POP_BACK = 60 };                                 /* (a popup: pop_i BK_*) */
enum { BK_BAR1, BK_TRACK1, BK_SCENE1, BK_CLOSED, BK_NONE, BK_PLAY, BK_STOP };   /* (0.14.0: BK_PLAY,
                                                         * BK_STOP: Back + Play where Play is the firmware's) */
int rfmv_play_fw(const rfm_move_t *m);                  /* (0.14.0) Play alone is the firmware's here */
void rfmv_play_both(rfm_move_t *m);                     /* (0.14.0) FW Play on Both: the Move's transport too */
int rfmv_back_keys(const rfm_move_t *m);                /* Back held where its shortcuts work */
void rfmv_seq_back_key(rfm_move_t *m, int cc);          /* a button pressed with Back held there (its own job not done) */
int rfmv_seq_back_lit(const rfm_move_t *m, int cc);     /* .. a shortcut there now (lit while Back is held) */
void rfmv_seq_back_pop(const rfm_move_t *m, mr_canvas_t *cv);   /* POP_BACK's screen */

/* (0.12.x, the owner's 2026-11-22) a note layout's FM-1 knob page (move.c): the page shown (track view); the slot has a
 * firmware (or no host list); level 2 (the firmware's buttons and steps too); its knobs are the FM-1's now (no menu, no
 * held step). POP_FWLV: the level toggled (pop_i 0 / 1) */
enum { POP_FWLV = 70, POP_FWJ = 71 };   /* (0.14.0) POP_FWJ: the page's jog: pop_i 0 its pages again, 1..3 on SELECT,
                                                         * PRESET, ALGO */
int rfmv_proj_undo_lit(const rfm_move_t *m);           /* (0.14.0) the projects page: Undo brings a deleted one back */
#define RFMV_PJ_DEL_MS 250                              /* (0.14.0) a deleted project's pad: red so long, then dark */
#define RFMV_PJ_ACK_MS 120                              /* (0.14.0) the open project saved: its pad off, on, off */
int rfmv_fwj_on(const rfm_move_t *m);                   /* (0.14.0) the FM-1 page's jog turns the jog target now */
int rfmv_fwj_lit(const rfm_move_t *m);                  /* .. the jog is on it (taken over, or Shift + click): its knob white */
int rfmv_fwp_page(const rfm_move_t *m);
int rfmv_fwp_live(const rfm_move_t *m);
int rfmv_fwp_locked(const rfm_move_t *m);              /* (0.14.0) Knobs+Keys or the jog on a knob: Back ends it */
void rfmv_fwp_unlock(rfm_move_t *m);
void rfmv_fwp_ctx(rfm_move_t *m);                       /* (0.14.0) another mode, slot, track, view, layout: unlocked */
int rfmv_fwp_full(const rfm_move_t *m);
int rfmv_fwp_knobs(const rfm_move_t *m);
int rfmv_seq_trig_on(const rfm_move_t *m);              /* the knobs do the TRIG page's (TRIG, or a step held on FM-1) */
/* ---- (0.14.0, the owner's 2026-11-24) engine.c: the ENGINE knob page (a firmware's ep1). POP_EPK: a knob of the page
 * touched or turned (pop_i the knob); POP_EPA: the page arrived (pop_i 0) or its layer's engine changed (1) */
enum { POP_EPK = 72, POP_EPA = 73 };
#define RFMV_EP_OVR_MS 500                              /* a value sent is shown until the firmware's agree, or this long */
void rfmv_ep_init(rfm_move_t *m);
int rfmv_ep_target(const rfm_move_t *m, int track);    /* the slot a track's page edits (SPEC.md 9), -1 none */
int rfmv_ep_layer_of(const rfm_move_t *m, int track, int slot);   /* .. its layer (the Layer setting's seam: Auto) */
int rfmv_ep_avail(const rfm_move_t *m);                 /* the selected track has an ENGINE page */
int rfmv_ep_page(const rfm_move_t *m);                  /* .. it is the knob page shown (track view) */
int rfmv_ep_list_shown(const rfm_move_t *m);            /* .. its page list on the screen */
void rfmv_ep_sync(rfm_move_t *m, int leave);            /* after input / advance (leave 1) and the host's ep1 calls */
void rfmv_ep_flush(rfm_move_t *m);                      /* (rfm_move_seq_advance, first) the knobs' merged SETs */
void rfmv_ep_knob(rfm_move_t *m, int k, int d);
int rfmv_ep_touch(rfm_move_t *m, int k);
void rfmv_ep_click(rfm_move_t *m, int down);            /* the jog's click on the page (the list: at the release) */
void rfmv_ep_list_turn(rfm_move_t *m, int d);
void rfmv_ep_leds(const rfm_move_t *m, rfmv_rgb_t c[L_WHITE0]);
int rfmv_ep_list_screen(const rfm_move_t *m, mr_canvas_t *cv);   /* 1 drawn */
int rfmv_ep_pop_screen(const rfm_move_t *m, mr_canvas_t *cv);    /* POP_EPK, POP_EPA: 1 drawn */
void rfmv_ep_pop_words(const rfm_move_t *m, char *o, int cap);
void rfmv_kpage_cells(const rfm_move_t *m, int *n, int *cur);   /* the knob pages' strip: 6 or 7 cells, the one shown */
int rfmv_ep_value(const rfm_move_t *m, int slot, int layer, int idx);   /* the value shown (a sent one first) */
int rfmv_ep_knob_param(const rfm_move_t *m, int k);     /* knob k's parameter on the page now (an index), -1 */
int rfmv_ep_knob_words(const rfm_move_t *m, int k, char *name, int ncap, char *val, int vcap, double *frac, int *bip);
void rfmv_ep_text(const rfm_move_t *m, int slot, int idx, int v, char *o, int cap);   /* (tests) a value's words */
void rfmv_seq_kpage_set(rfm_move_t *m, int pg);         /* (seq_ui.c) the knob page shown (ENGINE absent: FM-1) */
void rfmv_seq_ep_list(rfm_move_t *m, int open);         /* (seq_ui.c) the page list open (a layer Back closes) / closed */
/* (0.12.x) a Track tap's flash: the steps show the 16 tracks (seq_ui.c); RFM_MOVE_TFLASH_MS in all */
#define RFM_MOVE_TFLASH_MS 600
int rfmv_seq_tflash_on(const rfm_move_t *m);
void rfmv_seq_tflash_start(rfm_move_t *m);
void rfmv_seq_tflash_leds(const rfm_move_t *m, rfmv_rgb_t c[L_WHITE0]);

#endif
