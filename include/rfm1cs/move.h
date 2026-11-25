/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm_move: an Ableton Move (running Move Remote) as a RemoteFM1 host's panel. Portable C99, no OS calls, no
 * allocation: the state is a plain struct the host owns. The mapping is the owner's design
 * (design/remotefm1move.movedesign.json); README.md has the tables and the decisions.
 *
 *   rfm_move_t m;  rfm_move_init(&m, &settings_or_NULL, on_action, ctx);
 *   Move input  : rfm_move_input(&m, &mr_event, now_ms)        -> actions through the callback
 *   FM-1 LEDs   : rfm_move_panel(&m, levels44, play_green)     (levels[col * 4 + line], 0..255, as hosts have them)
 *   Move LEDs   : n = rfm_move_leds(&m, out, max)              only the LEDs that changed since the last call
 *   Move screen : rfm_move_screen(&m, rgb565_240x240_or_NULL, out1024)
 *   link lost / mode changed: rfm_move_release_all(&m)        (every held key, button and note let go)
 *
 * Actions use the protocol's MATRIX order (proto/rfm1.h, libremotefm1's rfm_event_t): buttons 0 OCT- 1 OCT+ 2 FX ..
 * 13 REC, encoders 0 SELECT 1 ALGO 2..5 K1..K4 6 PRESETS; keys 0..26 (key 0 = F3). The web player's panel.js names
 * them by label / role: labelToMatrix / roleToMatrix convert. */
#ifndef RFM_MOVE_H
#define RFM_MOVE_H
#include <stdint.h>

#include "moveremote/client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* rfm1cs-abletonmove's version (CHANGES.md); rfm_move_version() gives the built library's */
#define RFM1CS_VERSION_MAJOR 0
#define RFM1CS_VERSION_MINOR 14
#define RFM1CS_VERSION_PATCH 0
#define RFM1CS_VERSION "0.14.0"

#define RFM_MOVE_NKEYS 27
#define RFM_MOVE_NBTN 14
#define RFM_MOVE_NENC 7
#define RFM_MOVE_NPADS 32
#define RFM_MOVE_NSTEPS 16
#define RFM_MOVE_NKNOBS 8
#define RFM_MOVE_NBANKS 4
#define RFM_MOVE_NLEVELS 44                             /* the FM-1's LEDs: levels[col * 4 + line], 11 columns */
#define RFM_MOVE_LABEL_MAX 16
/* (0.5.6) the firmware's live labels, fm1core's DYNAMIC-LABELS slots (FM1CORE_LBL_*): 0..7 the knobs by FM1P_E_* role
 * (SELECT ALGO PRESETS K1..K4 MASTER), 8..21 the buttons by FM1P_B_* (FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY
 * REC OCT- OCT+), 22..37 the white keys F3..G5, 38..48 the black keys F#3..F#5 */
#define RFM_MOVE_LBL_KNOB0 0
#define RFM_MOVE_LBL_BTN0 8
#define RFM_MOVE_LBL_WHITE0 22
#define RFM_MOVE_LBL_BLACK0 38
#define RFM_MOVE_LBL_N 49
#define RFM_MOVE_SAY_MAX 256                            /* (0.6.0) a SAY action's words with their NUL */
#define RFM_MOVE_LABEL_HOLD_MS 300                      /* FM-1 mode: a button held this long shows its label */
/* (0.5.7) readouts: what a control does now and its value, in the firmware's words (the labels plan's rd1) */
enum { RFM_MOVE_RD_PARAM = 1, RFM_MOVE_RD_PRESET = 2, RFM_MOVE_RD_MESSAGE = 3 };
#define RFM_MOVE_RD_BIPOLAR 1                           /* flags: the bar from the middle */
#define RFM_MOVE_RD_PAIR_MS 250                         /* control -1: the gesture this recent takes it */
typedef struct {
    char title[RFM_MOVE_LABEL_MAX + 1], value[RFM_MOVE_LABEL_MAX + 1];
    int16_t frac;                                       /* 0..1000 a bar, -1 none */
    uint8_t kind, flags, on;
} rfm_move_readout_t;

/* the FM-1's matrix buttons and encoders (proto/rfm1.h order) */
enum {
    RFM_MOVE_B_OCTDN = 0, RFM_MOVE_B_OCTUP = 1, RFM_MOVE_B_FX = 2, RFM_MOVE_B_SEL = 3, RFM_MOVE_B_ENV = 4,
    RFM_MOVE_B_LFO = 5, RFM_MOVE_B_EDIT = 6, RFM_MOVE_B_GLO = 7, RFM_MOVE_B_HOME = 8, RFM_MOVE_B_SAVE = 9,
    RFM_MOVE_B_ARP = 10, RFM_MOVE_B_SEQ = 11, RFM_MOVE_B_PLAY = 12, RFM_MOVE_B_REC = 13,
};
enum {
    RFM_MOVE_E_SELECT = 0, RFM_MOVE_E_ALGO = 1, RFM_MOVE_E_K1 = 2, RFM_MOVE_E_K2 = 3, RFM_MOVE_E_K3 = 4,
    RFM_MOVE_E_K4 = 5, RFM_MOVE_E_PRESETS = 6,
    RFM_MOVE_E_MASTER = 7,                              /* (labels only: MASTER is not an encoder) */
};

/* what the controls play now (rfm_move_mode). (0.8.0, 0.9.0: the Track buttons' proposals 2 and 3) Track 1..3 are the
 * firmware slots and each slot has its own layout (RFM_MOVE_LAY_*): FM1 is the FM-1 layout, TAKEOVER the firmware's own
 * (rfm_move_set_takeover), SEQ a note layout: the sequencer on the slot's selected track (settings.lock). MIDI and
 * CONTROL are no longer entered (rfm_move_set_mode(m, MIDI or CONTROL) is SEQ). TAKEOVER is never in the settings */
enum { RFM_MOVE_MODE_FM1 = 0, RFM_MOVE_MODE_MIDI = 1, RFM_MOVE_MODE_CONTROL = 2, RFM_MOVE_MODE_SEQ = 3,
       RFM_MOVE_MODE_TAKEOVER = 4 };
/* (0.8.0) a firmware slot's layout, its Track button's menu's top row (pad 1 FM-1, pad 2 the firmware's own takeover,
 * pads 3..6 the note layouts) */
enum { RFM_MOVE_LAY_FM1 = 0, RFM_MOVE_LAY_TK = 1, RFM_MOVE_LAY_SCALE = 2, RFM_MOVE_LAY_CHROMA = 3, RFM_MOVE_LAY_DRUM = 4,
       RFM_MOVE_LAY_OCTAVE = 5, RFM_MOVE_LAY_N = 6 };
#define RFM_MOVE_FW_SLOTS 3
#define RFM_MOVE_DOUBLE_MS 300                          /* (0.8.0) a Track button's double click: pressed again this
                                                         * soon after a tap's release */
typedef struct {                                        /* (0.8.0) a firmware slot's own settings (Track n held) */
    uint8_t lay;                                        /* RFM_MOVE_LAY_* */
    uint8_t scale, root, channel;                       /* the note layouts' scale, root 0..11, MIDI channel 0..15 */
    int8_t octave;                                      /* RFM_MOVE_OCT_MIN..MAX */
    uint8_t cc[8];                                      /* the knobs' CC numbers in a note layout (16..23 at first) */
} rfm_move_slotset_t;
enum { RFM_MOVE_LAYOUT_SCALE = 0, RFM_MOVE_LAYOUT_CHROMATIC = 1, RFM_MOVE_LAYOUT_DRUM = 2, RFM_MOVE_LAYOUT_OCTAVE = 3 };
enum {
    RFM_MOVE_SCALE_MAJOR = 0, RFM_MOVE_SCALE_MINOR, RFM_MOVE_SCALE_DORIAN, RFM_MOVE_SCALE_MIXOLYDIAN,
    RFM_MOVE_SCALE_PENTA_MAJOR, RFM_MOVE_SCALE_PENTA_MINOR, RFM_MOVE_SCALE_HARM_MINOR, RFM_MOVE_SCALE_BLUES,
    RFM_MOVE_SCALE_PHRYGIAN, RFM_MOVE_SCALE_LYDIAN, RFM_MOVE_SCALE_LOCRIAN, RFM_MOVE_SCALE_MELODIC_MINOR,
    RFM_MOVE_SCALE_WHOLE_TONE, RFM_MOVE_SCALE_DIM_HW, RFM_MOVE_SCALE_DIM_WH, RFM_MOVE_SCALE_HUNGARIAN_MINOR,
    RFM_MOVE_SCALE_PHRYGIAN_DOM, RFM_MOVE_SCALE_IN_SEN, RFM_MOVE_SCALE_HIRAJOSHI, RFM_MOVE_SCALE_IWATO,
    RFM_MOVE_SCALE_PELOG, RFM_MOVE_SCALE_DOUBLE_HARM, RFM_MOVE_SCALE_SPANISH8, RFM_MOVE_SCALE_BEBOP,
    RFM_MOVE_NSCALES                                    /* 24 (the first 8 as before): Track 2 held, rows 2..4 */
};
#define RFM_MOVE_OCT_MIN (-4)                           /* the octave: the lowest pad of layouts 1, 2, 4 is */
#define RFM_MOVE_OCT_MAX 4                              /* 48 + root + 12 * octave (C3 = 48 at octave 0) */
#define RFM_MOVE_DRUM_BASE 36                           /* the drum layout's bottom-right pad (C2) */
#define RFM_MOVE_THUMB_LUMA 80                          /* screen thumbnail: an FM-1 pixel above this luma (0..255) is white */
#define RFM_MOVE_POPUP_MS 1000                          /* a popup stays this long after its control's release / last turn */
#define RFM_MOVE_SCREEN_LIT 64                          /* FM-1 mode's drawing: a button is drawn lit from this LED level */
#define RFM_MOVE_MASTER_STEP 16                        /* MASTER per volume knob detent */

/* ---- actions (the OMX-27 link's vocabulary: web/site/js/omx27-views.js) */
enum {
    RFM_MOVE_ACT_KEY = 1,                               /* index key 0..26, down 1/0, vel 1..127 on a down */
    RFM_MOVE_ACT_PRESS = 2,                             /* index key, value pressure 0..127 (lean: rfm_move_lean) */
    RFM_MOVE_ACT_BTN = 3,                               /* index matrix button 0..13, down 1/0 */
    RFM_MOVE_ACT_TURN = 4,                              /* index matrix encoder 0..6, value detents (+ clockwise) */
    RFM_MOVE_ACT_MASTER = 5,                            /* value 0..1023 */
    RFM_MOVE_ACT_MIDI = 6,                              /* n bytes: one whole message into the firmware's MIDI in */
    RFM_MOVE_ACT_SETTINGS = 7,                          /* the settings changed: save them when convenient */
    /* the sequencer's projects (32 slots the host stores; README.md, "Projects") */
    RFM_MOVE_ACT_PROJECT_SAVE = 8,                      /* index slot 0..31: store data/len (a project blob); value 1
                                                         * (0.14.0): the player's own save: the firmwares as now too */
    RFM_MOVE_ACT_PROJECT_LOAD = 9,                      /* index slot: answer with rfm_move_project_load(m, slot, ..) */
    RFM_MOVE_ACT_PROJECT_DELETE = 10,                   /* index slot: forget it */
    RFM_MOVE_ACT_AUDIO_REC = 12,                        /* Sample's audio loop (frame-stamped, on a bar line): value 1
                                                         * start, 0 stop (index: the bars recorded, at a stop) */
    RFM_MOVE_ACT_PROJECT_CUT = 13,                      /* index slot: into the host's project buffer, then gone */
    RFM_MOVE_ACT_PROJECT_COPY = 14,                     /* index slot: into the buffer (it stays) */
    RFM_MOVE_ACT_PROJECT_PASTE = 15,                    /* index slot (empty): the buffer stored there */
    RFM_MOVE_ACT_PROJECT_COLOR = 16,                    /* index slot, value its colour 1..16: the host sets the stored
                                                         * blob's byte 18 (and its check) */
    RFM_MOVE_ACT_CLICK = 11,                            /* the metronome (frame-stamped): value 1 a bar's first beat,
                                                         * 0 another beat; the host plays a click */
    /* (0.4.0) the firmware slots, the library, FX setup and the firmwares' states (README.md, "Firmwares"; the host
     * guide's v0.4.0 section). FW_*: slot 0..2 in both index and slot; a firmware's id in data (NUL-terminated, len
     * without the NUL) and its index in rfm_move_fw_list's array in value */
    RFM_MOVE_ACT_FW_LOAD = 17,                          /* slot, id: load that firmware into the slot (answer with
                                                         * rfm_move_fw_slots, and rfm_move_fw_progress meanwhile) */
    RFM_MOVE_ACT_FW_UNLOAD = 18,                        /* slot: empty it (the core shows it empty at once) */
    RFM_MOVE_ACT_FW_ACTIVE = 19,                        /* slot: the firmware the Move's controls play now */
    RFM_MOVE_ACT_FW_DOWNLOAD = 20,                      /* id: download it, or update it to the catalog's version */
    RFM_MOVE_ACT_FW_LIBRARY = 21,                       /* data/len: the library blob changed; store it */
    RFM_MOVE_ACT_FW_LEVEL = 22,                         /* slot, value 0..127: that slot's level in the mix */
    RFM_MOVE_ACT_FX_LOAD = 23,                          /* fx kind, slot, value: the module's index in the kind's
                                                         * list (-1: empty the slot); data its name (NULL: empty) */
    RFM_MOVE_ACT_FX_PARAM = 24,                         /* fx kind, slot, param (its index), value 0..127 or a word's
                                                         * index */
    RFM_MOVE_ACT_MIX = 25,                              /* slot (firmware 0..2), mix[] = level, pan (64 centre), the
                                                         * sends to audio FX 1..3 */
    RFM_MOVE_ACT_ROUTE = 26,                            /* filt[0..2]: each firmware's track filter, filt[3..5]: each
                                                         * MIDI FX slot's (bit t: sequencer track t + 1) */
    RFM_MOVE_ACT_STATE_SAVE = 27,                       /* slot (firmware 0..2), index state slot 0..31, data the
                                                         * firmware's id: store its state there */
    RFM_MOVE_ACT_STATE_LOAD = 28,                       /* .. load that state into the firmware */
    RFM_MOVE_ACT_STATE_DELETE = 29,                     /* .. forget it ((0.13.0) with RFM_MOVE_CAP_PROJFW: it goes
                                                         * into the host's state buffer first (Delete is a cut); the
                                                         * active one deleted: the firmware keeps what it holds, no
                                                         * state is active) */
};
/* (0.6.0) words for a screen reader (rfm_move_set_speech): index RFM_MOVE_SAY_*, data / len the words (printable ASCII,
 * NUL-terminated, len < RFM_MOVE_SAY_MAX; valid during the callback only), param 0. Only while speech is on. A
 * #define (not an enum value) so a host can #ifdef it */
#define RFM_MOVE_ACT_SAY 30
#define RFM_MOVE_DEST_ALL 255                           /* (0.9.0) a MIDI action's dest: every loaded firmware */
/* (0.7.0) the takeover (rfm_move_set_takeover; README "Takeover"). The host calls the slot's firmware with them:
 *  - TK_INPUT: rfm1_tk_input(pkt): slot, pkt[4] (and data / len 4) the raw packet of a claimed control (a release the
 *    core makes up, as the takeover closes: note off 0x80 vel 0, a button's CC value 0);
 *  - TK_STATE: rfm1_tk_state(down, tk_claims, tk_flags): slot, down 1 the takeover is shown now with these claims
 *    (sent again when the claims or the flags change), 0 it is gone (after a release for every control the firmware
 *    was sent as held). SAY actions carry slot too for a firmware's words (rfm_move_tk_say) */
#define RFM_MOVE_ACT_TK_INPUT 31
#define RFM_MOVE_ACT_TK_STATE 32
/* (0.13.0) a project holds its firmwares; firmware states on the Move (README "Projects that hold their firmwares",
 * "States"). Only for a host that sets RFM_MOVE_CAP_PROJFW (rfm_move_fw_caps); a firmware's id is in data / len (no
 * NUL counted, valid during the callback only), as STATE_SAVE's.
 *  - PROJECT_FW: slot 0..2 (and in index the PROJECT slot 0..31 it is for), data / len the firmware's id ("" / 0:
 *    empty the slot). Sent once per firmware slot (slot 0 only without RFM_MOVE_CAP_SLOTS) when a version 9 project
 *    has loaded, and with "" for each slot when a new (empty) project is made. The host, before anything else, writes
 *    what each slot's firmware holds now into the copy of the project it last loaded (the outgoing one); then: the same
 *    firmware already in the slot: only its memory is swapped for THIS project's copy; another firmware: it is loaded
 *    with this project's copy (a project with no copy yet: the firmware as it is stored); "": the slot unloaded. It
 *    answers with rfm_move_proj_fw (0..99 while it works, then READY / MISSING / FAILED; nothing is needed for "")
 *    and, as after FW_LOAD, rfm_move_fw_slots, rfm_move_fw_states and rfm_move_fw_state_cur.
 *  - STATE_COPY: slot, index the state 0..31: the host copies that stored state into its state buffer (one buffer,
 *    with the firmware's id: the core offers it only to that firmware's States).
 *  - STATE_PASTE: slot, index the target state: the buffer stored there (over a stored state: replaced). value 1: the
 *    target is the active state, so the host also loads it into the firmware (else the autosave would write the
 *    firmware's old memory back over it); value 0: the firmware is left as it is.
 *  - STATE_NEW: slot: the host keeps what the firmware holds now (the undo), then restarts it as first installed (its
 *    factory memory). No stored state is touched; none is active now (the core's st_cur -1).
 *  - STATE_UNDO: slot, value the state active before New (-1 none; index the same, 255 for none): the memory kept at
 *    New goes back into the firmware and that state is the active one again. Only right after a New (one step).
 * (0.14.0, the owner's 2026-11-24; README "States: the host's contract") pad 1 is the open project's own sound, pads
 * 2..32 a library shared by all projects, and no state is ever active. With RFM_MOVE_CAP_PROJFW every STATE_* index k
 * means: 0 the open project's own sound (the project's copy of the firmware, which the firmware holds while the project
 * is open), 1..31 the library's state file k. The host never autosaves into a state file (only into the project's copy
 * and flash.bin, as before), and keeps one undo per slot (the memory before the last NEW 255, LOAD or PASTE onto 0).
 *  - pad 1 clicked: PROJECT_SAVE, index the open project, value 1 (as the open project's click in Projects).
 *  - STATE_SAVE (k 1..31 only): the memory now (synced) stored as state k. It is not made active.
 *  - STATE_LOAD (k 1..31 only): state k INTO the project: the memory now kept as the undo (and written into the
 *    project's copy), then state k into the firmware; the project's copy follows (its autosave). State k is never
 *    written by playing afterwards.
 *  - STATE_DELETE (k 1..31 only): as before.
 *  - STATE_COPY: k 0: the memory now (synced) into the state buffer; k 1..31: that state, as before.
 *  - STATE_PASTE: k 1..31, value 0: the buffer stored as state k (replacing one there); k 0, value 1: the buffer
 *    loaded into the firmware as the project's sound, the memory before it kept as the undo.
 *  - STATE_NEW: index 255 (pad 1 reset, step 14): as above; it is the project's sound now (no state active, none
 *    touched). Index 1..31 (a library pad held + Delete: reset): the firmware as first installed (a fresh memory, no
 *    working memory) stored as state k, as the page's "Reset to default"; the firmware is not touched, no undo kept.
 *  - STATE_UNDO: value -1, index 255 always: the undo back into the firmware (nothing kept: nothing).
 * The host reports rfm_move_fw_states as before (bit k: library state k; bit 0 is ignored); rfm_move_fw_state_cur is
 * ignored. A state file at index 0 (made before 0.14.0): the host moves it once to the first free index 1..31. */
#define RFM_MOVE_ACT_PROJECT_FW 33
#define RFM_MOVE_ACT_STATE_COPY 34
#define RFM_MOVE_ACT_STATE_PASTE 35
#define RFM_MOVE_ACT_STATE_NEW 36
#define RFM_MOVE_ACT_STATE_UNDO 37
/* (0.14.0, the owner's 2026-11-24: "add a page after the FM-1 knobs for Engine parameters") engine parameters, the
 * firmware's ep1 (engine-params SPEC.md; README "Engine parameters"). The host turns them into the slot's queued
 * rfm1_ep_set / rfm1_ep_watch calls, due at the block's first frame + frame:
 *  - EP: slot, op (RFM_MOVE_EP_SET: a knob on the ENGINE page), layer, pid, value, frame. A knob's turns are merged to
 *    one per (slot, layer, pid) per rfm_move_seq_advance call: the latest value, sent at frame 0 of that call.
 *  - EP_WATCH: slot, layer, value the page (an index into the description's pages section; -1: none): the page whose
 *    cells the core wants (rfm_move_ep_cells). Sent when the ENGINE page shown, the page list's highlight or the
 *    selected track changes; -1 to the slot watched before when no Engine page shows any more */
#define RFM_MOVE_ACT_EP 38
#define RFM_MOVE_ACT_EP_WATCH 39
/* (0.15.0, the owner's 2026-11-24) the sampler app (README "The sampler"; only with RFM_MOVE_CAP_SAMPLER):
 *  - SMP_SET: index the setting (RFM_MOVE_SMP_*), value its value: Source RFM_MOVE_SMP_SRC_*, Mode RFM_MOVE_SMP_M_*,
 *    Length the bars (0 Open; in Capture mode Open is "Auto": as much as the buffer holds), Threshold dB -60..0, Input
 *    level dB -24..24, Monitor 0 / 1, To RFM_MOVE_SMP_TO_*. Sent at every change, and once for each setting when the
 *    capability is set (rfm_move_fw_caps) or the host asks (rfm_move_smp_sync, after settings it loaded itself).
 *  - SMP_PRESS: the record key (Sample, in the app) pressed: value the mode (as SMP_SET last said). Sent at the press,
 *    no release. The host's recorder does what the mode means (Capture: keep the buffer; Bars: arm, start on the bar,
 *    stop after the bars; Threshold: arm, start above the threshold; Manual: start / stop; a press while armed or
 *    recording: the host's choice, as a stop). The core times nothing: it shows rfm_move_smp_status */
#define RFM_MOVE_ACT_SMP_SET 48
#define RFM_MOVE_ACT_SMP_PRESS 49
/* (0.15.0, stage 2, the owner's 2026-11-24) the sampler's takes: the Takes and Tools tabs, a take's actions, Trim (README
 * "The sampler: the takes"; only for a host that offers the Takes or Tools tab, rfm_move_smp_offer). The host owns the
 * files; the core asks, never waits, and draws what has come (a row not answered yet: empty). Each carries the new act
 * fields take, from, count (below):
 *  - SMP_ROWS: take the folder (RFM_MOVE_SMP_ALL: every take, the newest first, which is what the app asks since the
 *    owner's 2026-11-25 "scroll through everything I've recorded, newest at the top"; 0: the days;
 *    RFM_MOVE_SMP_NEWEST: the newest day that has takes; else a day's id), from
 *    the first row, count the rows wanted (a window of the list, newest first). Answer: rfm_move_smp_rows.
 *  - SMP_PEAKS: take a take's id, frames [from, from + count), value the columns (1..RFM_MOVE_SMP_WPEAKS): each column's
 *    peak 0..255 (linear). Answer: rfm_move_smp_peaks (a newer ask may replace an older one not yet answered).
 *  - SMP_PLAY: take, value 1: play frames [from, from + count) once (count 0: to its end; a stems take: its Master)
 *    through the Move's output, after the recorder's tap (never recorded); value 0: stop. One plays at a time (a new
 *    play replaces it). Where it is: rfm_move_smp_status's play_take / play_pos.
 *  - SMP_EDIT: take, index the op (RFM_MOVE_SMP_OP_*): DELETE (into the host's trash; a stems take: its folder), TRIM
 *    (keep frames [from, from + count) only, sample-exact, a few ms of fade at a cut end), NORMALIZE (its peak to
 *    -0.3 dBFS), REVERSE, FADE_IN / FADE_OUT (value: ms). Each one atomic on the file, the file before it kept as the
 *    one undo (a new edit replaces it). Answer: rfm_move_smp_done.
 *  - SMP_UNDO: the last DELETE or edit undone (one step; status undo 1 while there is one). Answer: rfm_move_smp_done
 *    (RFM_MOVE_SMP_OP_UNDO). */
#define RFM_MOVE_ACT_SMP_ROWS 50
#define RFM_MOVE_ACT_SMP_PEAKS 51
#define RFM_MOVE_ACT_SMP_PLAY 52
#define RFM_MOVE_ACT_SMP_EDIT 53
#define RFM_MOVE_ACT_SMP_UNDO 54
enum { RFM_MOVE_SMP_OP_DELETE = 1, RFM_MOVE_SMP_OP_TRIM, RFM_MOVE_SMP_OP_NORMALIZE, RFM_MOVE_SMP_OP_REVERSE,
       RFM_MOVE_SMP_OP_FADE_IN, RFM_MOVE_SMP_OP_FADE_OUT, RFM_MOVE_SMP_OP_UNDO };   /* SMP_EDIT's index; _UNDO: answers */
enum { RFM_MOVE_EP_SET = 1, RFM_MOVE_EP_LOCK = 2, RFM_MOVE_EP_FREE = 3, RFM_MOVE_EP_FREE_ALL = 4 };   /* (EP's op; only
                                                         * SET is sent before the engine locks) */
typedef struct {
    uint8_t kind;                                       /* RFM_MOVE_ACT_* */
    uint8_t index, down, vel;
    int16_t value;
    uint8_t n, bytes[3];
    /* when: actions from rfm_move_seq_advance carry the frame (in that call's units, 0..frames-1, from the start
     * of the call) the event is due at; every other action has 0 = now */
    uint32_t frame;
    const uint8_t *data;                                /* PROJECT_SAVE: the blob, valid during the callback only */
    uint32_t len;
    /* (0.4.0, new fields at the end) */
    uint8_t track;                                      /* MIDI: the sequencer track 1..16 that sent it; 0 anything
                                                         * else (the pads played live, Control mode's knobs) */
    uint8_t slot;                                       /* FW_*, MIX, STATE_*: the firmware slot 0..2; FX_*: the FX
                                                         * slot 0..2 */
    uint8_t fx;                                         /* FX_*: RFM_MOVE_FX_MIDI or RFM_MOVE_FX_AUDIO */
    uint8_t param;                                      /* FX_PARAM: the parameter's index */
    uint8_t mix[5];                                     /* MIX: level, pan, send 1, send 2, send 3 (0..127) */
    uint16_t filt[6];                                   /* ROUTE: firmware 1..3, MIDI FX 1..3 track filters */
    /* (0.5.4) */
    uint8_t dest;                                       /* MIDI (track 0): 0 the active slot, as always; 1..3 firmware
                                                         * slot dest - 1, whichever is active now (a latched note's
                                                         * note off: the slot it was played on); (0.9.0)
                                                         * RFM_MOVE_DEST_ALL every loaded firmware (the sequencer's
                                                         * clock: FA, F8 at 24 PPQN, FC; one byte, n 1) */
    /* (0.6.6) */
    uint8_t mroute[3];                                  /* ROUTE: each firmware slot's MIDI routes (rfm_move_fxset_t
                                                         * mroute: bits 0..2 the slots, RFM_MOVE_MROUTE_OUT Out) */
    /* (0.7.0) the takeover (TK_INPUT, TK_STATE; slot: the firmware slot) */
    uint8_t pkt[4];                                     /* TK_INPUT: the Move's USB-MIDI packet (byte 0 cable 0 / CIN),
                                                         * for rfm1_tk_input as a u32 little-endian; data / len too */
    uint16_t tk_claims;                                 /* TK_STATE: the claims granted (RFM_MOVE_TK_*), 0 when off */
    uint8_t tk_flags;                                   /* TK_STATE: RFM_MOVE_TKF_SPEECH */
    /* (0.14.0) engine parameters (EP, EP_WATCH; slot: the firmware slot) */
    uint16_t pid;                                       /* EP: the parameter's id (1..0xFFFE) */
    uint8_t layer, op;                                  /* EP, EP_WATCH: the layer 0..7; EP: RFM_MOVE_EP_* */
    /* (0.15.0, stage 2) the sampler's takes (SMP_ROWS .. SMP_EDIT) */
    uint32_t take;                                      /* a take's id (SMP_ROWS: the folder's) */
    uint32_t from, count;                               /* frames (SMP_ROWS: rows) */
} rfm_move_act_t;
typedef void (*rfm_move_act_fn)(void *ctx, const rfm_move_act_t *a);
/* pressure 0..127 as emu_hal_t / fm1panel_t key_lean 0..100 (as rfm_panel_lean) */
static inline uint8_t rfm_move_lean(int p) { return (uint8_t)(((p < 0 ? 0 : p > 127 ? 127 : p) * 100 + 63) / 127); }

/* (0.15.0) the sampler app's settings, the rows of its list in this order (README "The sampler"; the API below) */
enum { RFM_MOVE_SMP_SOURCE, RFM_MOVE_SMP_MODE, RFM_MOVE_SMP_LENGTH, RFM_MOVE_SMP_THRESHOLD, RFM_MOVE_SMP_INPUT,
       RFM_MOVE_SMP_MONITOR, RFM_MOVE_SMP_TO, RFM_MOVE_SMP_N };

/* ---- the settings a host keeps (rfm_move_save / rfm_move_load: a versioned byte image) */
typedef struct {
    uint8_t mode;                                       /* RFM_MOVE_MODE_* */
    uint8_t layout;                                     /* RFM_MOVE_LAYOUT_* */
    uint8_t channel;                                    /* MIDI channel 0..15 (shown 1..16) */
    int8_t octave;                                      /* RFM_MOVE_OCT_MIN..MAX */
    uint8_t scale, root;                                /* RFM_MOVE_SCALE_*, 0..11 (0 = C) */
    uint8_t bank;                                       /* Control mode's CC bank 0..3 */
    uint8_t cc[RFM_MOVE_NBANKS][RFM_MOVE_NKNOBS];       /* each bank's CC numbers, knob 1..8 */
    uint8_t drum[RFM_MOVE_NPADS];                       /* the drum layout's notes, by pad (0 = bottom-left) */
    uint8_t midi_view;                                  /* MIDI mode's screen: RFM_MOVE_MV_* */
    uint8_t vel_mode, vel_level;                        /* the pads' velocity: RFM_MOVE_VEL_*; FIXED's level 1..127 */
    /* (version 4) the projects view as it was left (the firmware browser) */
    uint8_t fw_track;                                   /* the track shown, 0..3 (3: Sequencer Projects) */
    uint8_t fw_scope;                                   /* step 1 Favorites (0) or step 2 All (1) */
    uint16_t fw_filter;                                 /* the category / tag filter: bit s - 1 for step s (3..16);
                                                         * (0.4.2) one bit at most, 0 all */
    uint8_t fw_cat_scope;                               /* the catalog's scope: 0 everything, 1 Update ready, 2 Not
                                                         * installed (0.4.2: one at a time; 3 loads as 1) */
    uint8_t fx_page;                                    /* FX setup's page 0..3 */
    uint8_t fw_fx;                                      /* (0.4.5) FX setup was showing when the view was left: the
                                                         * view opens on it again (saved as fx_page's bit 7) */
    /* (version 5) */
    uint8_t pop_half;                                   /* FM-1 mode's label, readout and knob popups: 0 the whole
                                                         * screen (the default, 0.5.9), 1 the left half (the FM-1's
                                                         * screen stays on the right) */
    /* (version 6, 0.6.12) */
    uint8_t speech;                                     /* the screen reader: 0 not chosen yet (the host's default,
                                                         * rfm_move_set_speech_default), 1 + RFM_MOVE_SPEECH_* */
    /* (version 7, 0.8.0: the Track buttons, proposal 2) each firmware slot's layout and note settings (the fields
     * above, layout to octave, are the active slot's while it is played: a mirror), and the sequencer track locked
     * to each slot (Track n's tap in the Sequencer selects it) */
    rfm_move_slotset_t slot[RFM_MOVE_FW_SLOTS];
    uint8_t lock[RFM_MOVE_FW_SLOTS];                    /* 0..15 (slot n: track n + 1 at first) */
    /* (0.14.0) */
    uint8_t fw_play;                                    /* Global settings' FW Play: 0 Firmware (the default: a Play
                                                         * that goes to the firmware is its own only), 1 Both (it also
                                                         * starts / stops the Move's transport). Saved in pop_half's
                                                         * byte, bit 1 (no new version: older cores read bit 0 only) */
    /* (version 8, 0.15.0) the sampler app's settings, by RFM_MOVE_SMP_*: the source (RFM_MOVE_SMP_SRC_*), the mode
     * (RFM_MOVE_SMP_M_*), the length (an index: Open, 1, 2, 4, 8, 16, 32 bars), the threshold -60..0 dB, the input
     * level -24..24 dB, the monitor 0 / 1, where a take goes (RFM_MOVE_SMP_TO_*). Older images load the defaults */
    int8_t smp[RFM_MOVE_SMP_N];
} rfm_move_settings_t;
enum { RFM_MOVE_VEL_DYNAMIC, RFM_MOVE_VEL_FIXED, RFM_MOVE_VEL_SOFT, RFM_MOVE_VEL_HARD, RFM_MOVE_VEL_LINEAR, RFM_MOVE_VEL_N };
/* (the jog's order: Dynamic, Fixed, Soft, Linear, Hard; LINEAR came last, so older settings keep their values) */
const char *rfm_move_vel_name(int v);                   /* "DYNAMIC", "FIXED", "SOFT", "HARD", "LINEAR" */
int rfm_move_vel(const rfm_move_settings_t *s, int v);  /* a pad's velocity 1..127 through the setting */
#define RFM_MOVE_LIST_TICKS 5                           /* a list-type menu knob (scale, root, layout ..): ticks an item */
#define RFM_MOVE_KNOB_TICKS 2                           /* the Move's knob ticks an FM-1 encoder detent (the jog: 1) */
#define RFM_MOVE_SETTINGS_VERSION 8                     /* (versions 1..7, 77, 78, 80, 86, 87, 88 and 130 bytes, load
                                                         * too: no MIDI screen, no velocity setting, no projects view
                                                         * state, no popup width (full), no speech mode (the host's
                                                         * default), no slots (each slot gets the one mode, layout and
                                                         * bank), (0.15.0) the sampler's defaults) */
#define RFM_MOVE_SETTINGS_BYTES_V3 (6 + 7 + RFM_MOVE_NBANKS * RFM_MOVE_NKNOBS + RFM_MOVE_NPADS + 3)
#define RFM_MOVE_SETTINGS_BYTES_V6 (RFM_MOVE_SETTINGS_BYTES_V3 + 8)
#define RFM_MOVE_SETTINGS_BYTES_V7 (RFM_MOVE_SETTINGS_BYTES_V6 + RFM_MOVE_FW_SLOTS * 14)   /* (130) */
#define RFM_MOVE_SETTINGS_BYTES (RFM_MOVE_SETTINGS_BYTES_V7 + RFM_MOVE_SMP_N)   /* (137, version 8) */
enum { RFM_MOVE_MV_PADS, RFM_MOVE_MV_NOTES, RFM_MOVE_MV_N };   /* MIDI mode's screens (a tap on Track 2 turns them) */
void rfm_move_settings_default(rfm_move_settings_t *s);
/* "RMV" version8 len16, then the fields; returns RFM_MOVE_SETTINGS_BYTES */
uint32_t rfm_move_save(const rfm_move_settings_t *s, uint8_t out[RFM_MOVE_SETTINGS_BYTES]);
/* 0 ok (every field checked and clamped); -1 not a settings image (s is left as the defaults) */
int rfm_move_load(rfm_move_settings_t *s, const uint8_t *b, uint32_t n);

/* ---- a Move LED write (only the changed ones come out of rfm_move_leds) */
typedef struct {
    uint8_t rgb;                                        /* 1: LED_RGB (r g b); 0: LED (r = palette / brightness) */
    uint8_t cc;                                         /* 0 a note LED (pads 68..99, steps 16..31), 1 a CC LED */
    uint8_t number;
    uint8_t r, g, b;
} rfm_move_led_t;
/* every LED rfm_move drives: 32 pads and 16 steps (RGB note LEDs), 15 RGB CC LEDs (Play, Rec, Sample, Track 1..4,
 * knob rings 1..8), 13 white CC LEDs (Shift, Menu, Back, Capture, Down, Up, Undo, Loop, Copy, Left, Right, Mute,
 * Delete) */
#define RFM_MOVE_NLEDS 92                               /* (the last 16: the white icon LEDs under the steps, CC 16..31) */
/* (0.7.0) the LED list's order (rfm_move_tk_leds; the takeover spec's section 4.2): 0..31 the pads, 32..47 the steps,
 * 48 Play, 49 Rec, 50 Sample, 51..54 Track 1..4, 55..62 the knob rings, 63..75 the whites (Shift, Menu, Back, Capture,
 * Down, Up, Undo, Loop, Copy, Left, Right, Mute, Delete), 76..91 the icons under the steps */
enum { RFM_MOVE_L_PAD0 = 0, RFM_MOVE_L_STEP0 = 32, RFM_MOVE_L_PLAY = 48, RFM_MOVE_L_REC = 49, RFM_MOVE_L_SAMPLE = 50,
       RFM_MOVE_L_TRACK0 = 51, RFM_MOVE_L_RING0 = 55, RFM_MOVE_L_WHITE0 = 63, RFM_MOVE_L_ICON0 = 76 };

/* ---- (0.4.0) the firmware browser: the projects view's Tracks 1..3 are three firmware slots (README.md,
 * "Firmwares"; the host guide's v0.4.0 section has the whole contract)
 *
 * The catalog is the host's array (rfm_move_fw_list): the core keeps the pointer, reads it only on the thread that
 * calls the core, inside its calls, and never allocates. The array, and every string and thumbnail its entries point
 * to, stay valid and unchanged until the next rfm_move_fw_list call (a new list: a new array, then the call; never an
 * edit of the live one). The one exception is the core's own: rfm_move_fw_installed writes entries[i].installed.
 * Entries come in the order the pads show them (A-Z by name, as the catalog lists them). */
#define RFM_MOVE_LATCH_MAX (RFM_MOVE_NPADS * RFM_MOVE_FW_SLOTS)   /* (0.5.4) notes latched at once, all slots */
#define RFM_MOVE_FW_ID_MAX 32                           /* an id's bytes with its NUL (longer ids are refused) */
#define RFM_MOVE_FW_THUMB 64                            /* a thumbnail: 64 x 64, 1 bit, rows top first, MSB left */
#define RFM_MOVE_FW_THUMB_BYTES (RFM_MOVE_FW_THUMB * RFM_MOVE_FW_THUMB / 8)
#define RFM_MOVE_FW_TAGS 9                              /* the user tags on steps 8..16 */
#define RFM_MOVE_FW_TAG_NAME 16                         /* a tag's name with its NUL */
#define RFM_MOVE_FW_LIB_MAX 256                         /* firmwares the library keeps something for (a favourite, a
                                                         * colour, a tag) */
#define RFM_MOVE_FW_LIB_SAVE_MS 1000                    /* FW_LIBRARY: this long after the last change */
enum { RFM_MOVE_CAT_SYNTH = 1, RFM_MOVE_CAT_GROOVEBOX = 2, RFM_MOVE_CAT_CHORDS = 4, RFM_MOVE_CAT_DRUMS = 8,
       RFM_MOVE_CAT_OTHER = 16 };                       /* steps 3..7, the catalog's five categories in its order */
enum { RFM_MOVE_FW_FAILED = -1, RFM_MOVE_FW_BUSY = -2 };   /* rfm_move_fw_progress: it failed / the host is too busy */
typedef struct {
    const char *id;                                     /* the catalog's id (ASCII, < RFM_MOVE_FW_ID_MAX bytes) */
    const char *name;                                   /* the name shown (ASCII; other bytes are skipped) */
    const char *version;                                /* the catalog's version */
    const char *installed;                              /* the version installed on the host (the Move, or the web
                                                         * page's / phone app's store); NULL or "": not installed */
    const char *desc;                                   /* the description (ASCII), NULL: none */
    const uint8_t *thumb;                               /* RFM_MOVE_FW_THUMB_BYTES, NULL: none (initials stand in) */
    uint8_t cats;                                       /* RFM_MOVE_CAT_* bits */
    uint8_t cat;                                        /* its main category, 0..4 (Synth .. Other; its default pad
                                                         * colour), 255 none */
    uint8_t virt;                                       /* (0.5.4) 1: a Fantasy CFW (a virtual firmware: Elasticat,
                                                         * Ripple); badged, and step 16's filter */
} rfm_move_fw_t;

/* ---- (0.4.0) FX setup (Session in the projects view): 3 MIDI FX slots in series, each firmware's track filter, 3
 * audio FX slots on send / return busses, and the mix; saved with the project (version 5) */
enum { RFM_MOVE_FX_MIDI = 0, RFM_MOVE_FX_AUDIO = 1 };
enum { RFM_MOVE_FX_PAGE_MIDI = 0, RFM_MOVE_FX_PAGE_ROUTE = 1, RFM_MOVE_FX_PAGE_AUDIO = 2, RFM_MOVE_FX_PAGE_MIX = 3 };   /* (0.4.1)
                                                         * settings.fx_page: Track 1..4 in FX setup */
#define RFM_MOVE_FX_SLOTS 3
#define RFM_MOVE_FX_PARAMS 128                          /* (0.5.0; was 8) a module's parameters the core keeps: the
                                                         * first 128 of its list (FX_PARAM's param 0..127) */
#define RFM_MOVE_FX_PARAMS_V5 8                         /* (version 5 projects kept 8 values a slot) */
#define RFM_MOVE_FX_KNOBS 8                             /* (0.5.0) a parameter page's knobs */
#define RFM_MOVE_FX_PAGES 16                            /* (0.5.0) a module's parameter pages, at most */
#define RFM_MOVE_FX_UNSET 255                           /* (0.5.0) a slot value not saved (an older project): the
                                                         * parameter's default once the module is known */
#define RFM_MOVE_FX_NAME 24                             /* a module's name with its NUL, as the project keeps it */
typedef struct {
    const char *name;                                   /* "Rate": the popup's long name */
    int nwords;                                         /* 0: a value 0..127; else a list of words (the value: an index) */
    const char *const *words;
    uint8_t def;                                        /* the value a newly loaded module has (as the host loads it) */
    uint8_t dry_wet;                                    /* 1: a dry / wet mix; the popup says it must be 100 % wet */
    /* (0.5.0; zero = as before) how the value reads: on a page and in the popup */
    const char *short_name;                             /* a page cell's label (5 characters show), NULL: name */
    float lo, hi;                                       /* lo != hi: 0..127 shows as lo..hi (linear), with */
    uint8_t decimals;                                   /* .. this many decimals, and */
    const char *unit;                                   /* .. this unit after it ("%", "ms", "Hz"; NULL none) */
} rfm_move_fx_param_t;
typedef struct {                                        /* (0.5.0) a parameter page: a ui_hierarchy level's 8 knobs */
    const char *name;                                   /* the level's name ("Advanced", "Advanced 2"); NULL none */
    int16_t knob[RFM_MOVE_FX_KNOBS];                    /* knob n: a parameter index, -1 a gap */
} rfm_move_fx_page_t;
typedef struct {
    const char *name;                                   /* the module's name (unique in its kind) */
    int nparams;                                        /* chain_params order; the first RFM_MOVE_FX_PARAMS are kept */
    const rfm_move_fx_param_t *params;
    /* (0.5.0; zero = as before) the host's page plan (README "FX parameter pages"); npages 0: the core makes pages
     * of 8 in the params' order. At most RFM_MOVE_FX_PAGES are used */
    int npages;
    const rfm_move_fx_page_t *pages;
} rfm_move_fx_module_t;
typedef struct {
    char name[RFM_MOVE_FX_NAME];                        /* (saved) the module, "" empty */
    int16_t mod;                                        /* .. its index in the host's list now, -1 none */
    uint8_t val[RFM_MOVE_FX_PARAMS];                    /* (saved) by parameter index; RFM_MOVE_FX_UNSET unset */
    uint16_t filt;                                      /* (saved; MIDI FX) bit t: track t + 1 (one track, or all) */
} rfm_move_fxslot_t;
typedef struct {                                        /* (saved with the project, version 5) */
    rfm_move_fxslot_t mfx[RFM_MOVE_FX_SLOTS], afx[RFM_MOVE_FX_SLOTS];
    uint16_t ffilt[RFM_MOVE_FW_SLOTS];                  /* each firmware's track filter (all 16 at first) */
    uint8_t pan[RFM_MOVE_FW_SLOTS];                     /* 0..127, 64 centre */
    uint8_t send[RFM_MOVE_FW_SLOTS][RFM_MOVE_FX_SLOTS];  /* firmware -> audio FX bus, 0..127 */
    /* (0.6.6, saved with the project, version 7; not in RFM_MOVE_FXSET_BYTES) each firmware slot's MIDI routes: bit s
     * (0..2) to firmware slot s (never its own), RFM_MOVE_MROUTE_OUT to "Out", the host's MIDI out; no cycles */
    uint8_t mroute[RFM_MOVE_FW_SLOTS];
} rfm_move_fxset_t;
#define RFM_MOVE_FXSET_BYTES_N(nv) (RFM_MOVE_FX_SLOTS * (RFM_MOVE_FX_NAME + (nv) + 2) + \
                                    RFM_MOVE_FX_SLOTS * (RFM_MOVE_FX_NAME + (nv)) + \
                                    RFM_MOVE_FW_SLOTS * (2 + 1 + RFM_MOVE_FX_SLOTS))
#define RFM_MOVE_FXSET_BYTES RFM_MOVE_FXSET_BYTES_N(RFM_MOVE_FX_PARAMS)       /* (version 6) */
#define RFM_MOVE_FXSET_BYTES_V5 RFM_MOVE_FXSET_BYTES_N(RFM_MOVE_FX_PARAMS_V5)  /* (version 5) */

typedef struct {                                        /* a firmware the library knows */
    char id[RFM_MOVE_FW_ID_MAX];
    uint8_t color;                                      /* 1..16 (RFM_MOVE_PROJ_COLORS), 0 its category's */
    uint16_t tag_on, tag_set;                           /* bit s - 1 for step s: tagged; set by the user (else: the
                                                         * catalog's category for steps 3..7, untagged for 8..16) */
    int16_t fw;                                         /* its index in the host's list, -1 not there */
} rfm_move_lib_rec_t;
/* the library blob (FW_LIBRARY, rfm_move_fw_library; the host guide documents it) */
#define RFM_MOVE_FW_LIB_VERSION 1
#define RFM_MOVE_FW_LIB_BYTES (16 + RFM_MOVE_FW_SLOTS * RFM_MOVE_FW_ID_MAX + RFM_MOVE_FW_TAGS * RFM_MOVE_FW_TAG_NAME + \
                               RFM_MOVE_FW_LIB_MAX * (RFM_MOVE_FW_ID_MAX + 5 + 2) + 4)
#define RFM_MOVE_FW_STATES 32                           /* a firmware's state slots (the pads in States) */

typedef struct {
    /* the host's */
    rfm_move_fw_t *list;                                /* (the host's array: see above) */
    int n;
    uint8_t host;                                       /* rfm_move_fw_list was called: the browser is on */
    uint8_t voices;                                     /* firmwares the host can sound at once, 1..3 */
    const rfm_move_fx_module_t *mods[2];                /* the host's modules, by kind (the same contract) */
    int nmods[2];
    char slot_id[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX];   /* what each slot holds ("" empty) */
    int16_t slot_fw[RFM_MOVE_FW_SLOTS];                 /* .. its index in the list, -1 none or not listed */
    uint8_t level[RFM_MOVE_FW_SLOTS];                   /* (library) each slot's level, 0..127 */
    uint8_t active;                                     /* (library) the slot the controls play */
    uint32_t st_used[RFM_MOVE_FW_SLOTS];                /* each slot's firmware's stored states (bits) */
    int8_t st_cur[RFM_MOVE_FW_SLOTS];                   /* .. the one loaded or saved last, -1 ((0.14.0) not with
                                                         * RFM_MOVE_CAP_PROJFW: none is active) */
    /* the library */
    rfm_move_lib_rec_t rec[RFM_MOVE_FW_LIB_MAX];
    uint16_t nrec;
    uint16_t fav[RFM_MOVE_FW_LIB_MAX];                  /* the favourites, in order: record indexes */
    uint16_t nfav;
    char tag[RFM_MOVE_FW_TAGS][RFM_MOVE_FW_TAG_NAME];   /* the user tags' names ("" not made yet) */
    uint8_t lib_dirty;
    uint32_t lib_ms;
    uint8_t blob[RFM_MOVE_FW_LIB_BYTES];
    /* the view (a firmware track) */
    uint8_t track;                                      /* the projects view's track shown, 0..3 (3: the projects) */
    uint8_t catalog;                                    /* Mute: the catalog */
    uint8_t bank[3];                                    /* the bank shown: Favorites, All, the catalog */
    int16_t sel;                                        /* the firmware selected (its list index), -1 none */
    uint8_t jog;                                        /* 0 the name, 1.. the description's rows */
    uint8_t capture;                                    /* Capture held (tagging) */
    int8_t cap_step;                                    /* .. the step just pressed, -1 */
    uint32_t cap_ms;
    uint8_t states;                                     /* Loop: the firmware's states on the pads */
    uint8_t smods;                                      /* .. Copy (1), Delete (2) held */
    int8_t busy_kind;                                   /* a load (1), download (2), update (3) under way, 0 none */
    int16_t busy_fw;                                    /* .. its list index */
    int8_t busy_slot;                                   /* .. a load's slot */
    int8_t busy_pct;                                    /* .. 0..100 */
    char busy_id[RFM_MOVE_FW_ID_MAX];                   /* .. its id (a new list keeps it) */
    uint8_t pop;                                        /* the view's popup (fw.c FP_*), 0 none */
    int16_t pop_a, pop_b;
    uint32_t pop_ms;
    char pop_text[24];                                  /* .. a message's words */
    uint32_t vol_held;                                  /* the volume knob turned with a Track held (its level) */
    int8_t list_acc;                                    /* knob 1 (colour): the ticks short of a step */
    /* FX setup */
    uint8_t fx;                                         /* open */
    uint8_t fx_focus;                                   /* the slot (pad 1..3) on the page */
    int8_t browse;                                      /* the module list: -1 closed, 0 "(empty)", n the n-th module */
    int8_t knob_acc[RFM_MOVE_NKNOBS];                   /* list parameters: the ticks short of a word */
    /* (0.4.1) */
    uint8_t all_auto;                                   /* no favourites: All shown (settings.fw_scope kept) */
    /* (0.4.2) */
    uint8_t hold_step;                                  /* a filter step down (1..16; 0 none): a tap toggles it on
                                                         * release, a hold (RFM_MOVE_TAP_MS) only shows it */
    uint32_t hold_ms;
    /* (0.5.0) FX parameter pages */
    uint8_t fx_pages;                                   /* the focused module's page shown (Right), not the chain */
    uint8_t fx_pg[2][RFM_MOVE_FX_SLOTS];                /* each slot's page (its knobs' page) */
    uint8_t mix_par;                                    /* the Mixer's knobs 1..3: RFM_MOVE_MIXP_* */
    uint8_t fx_del;                                     /* Delete held in FX setup (Delete + pad empties a slot) */
    /* (0.5.10) */
    uint8_t caps;                                       /* the host's RFM_MOVE_CAP_* (rfm_move_fw_caps) */
    /* (0.12.0) */
    uint16_t steps_down;                                /* the browser's step keys down (white at the press, a14) */
    /* (0.13.0, RFM_MOVE_CAP_PROJFW) States as a layer over any view (projfw.c) */
    uint8_t sl;                                         /* open: the slot + 1, 0 closed */
    uint8_t sl_from;                                    /* 0 a held Track's pad 7, 1 the browser's Loop */
    uint32_t sl_ms;                                     /* opened (the sequencer clock's ms): its name, the sweep */
    uint8_t sl_mods;                                    /* Copy (1), Delete (2) held */
    uint32_t sl_pads;                                   /* the pads the layer took (their releases are its) */
    uint16_t sl_steps;                                  /* .. the steps */
    uint8_t sl_bt[16];                                  /* .. the buttons (bit cc) */
    uint8_t sl_pop;                                     /* its popup: 0 none, 1 its name, 2 a state, 3 New, 4 Undo, 5 an
                                                         * empty slot */
    int8_t sl_pk, sl_pw, sl_pslot;                      /* .. the state, its word (0 saved .. 4 deleted), the slot */
    uint32_t sl_pop_ms;
    uint8_t sb_kind;                                    /* the host's state buffer: 0 none, 1 copied, 2 deleted */
    int8_t sb_src;                                      /* .. the state it came from */
    uint8_t sb_xshow;                                   /* (0.13.0) a deleted state's X: until Delete is let go */
    uint8_t sb_armed;                                   /* .. the last state function a copy or a delete (marks shown;
                                                         * Copy + a stored state pastes over it) */
    char sb_id[RFM_MOVE_FW_ID_MAX];                     /* .. the firmware it belongs to */
    uint8_t undo;                                       /* New's undo is there: the slot + 1 */
    int8_t undo_cur;                                    /* .. (0.14.0) what it undoes: -1 New, 0 a paste onto pad 1,
                                                         * k the load of library state k */
    uint8_t sl_tap;                                     /* (0.14.0) the pad pressed alone, its state + 1 (0 none): a
                                                         * click acts at its release, held + Delete resets it */
} rfm_move_fw_state_t;
enum { RFM_MOVE_MIXP_LEVEL, RFM_MOVE_MIXP_PAN, RFM_MOVE_MIXP_SEND1, RFM_MOVE_MIXP_SEND2, RFM_MOVE_MIXP_SEND3,
       RFM_MOVE_MIXP_N };                               /* (0.5.0) the Mixer's choice (its right-hand pads) */

/* ---- the sequencer (Track 4; README.md, "Sequencer") */
#define RFM_MOVE_SEQ_TRACKS 16                          /* track n plays MIDI channel n by default */
#define RFM_MOVE_SEQ_SCENES 16
#define RFM_MOVE_SEQ_CLIPS (RFM_MOVE_SEQ_TRACKS * RFM_MOVE_SEQ_SCENES)   /* clip id = track * 16 + scene */
#define RFM_MOVE_SEQ_PAGE 16                            /* steps a page: a bar of 16ths */
#define RFM_MOVE_SEQ_PAGES 16
#define RFM_MOVE_SEQ_MAXLEN (RFM_MOVE_SEQ_PAGE * RFM_MOVE_SEQ_PAGES)     /* 256 steps */
#define RFM_MOVE_SEQ_TPS 24                             /* ticks a step (96 PPQN); a MIDI clock = 4 ticks */
#define RFM_MOVE_SEQ_BAR (RFM_MOVE_SEQ_TPS * RFM_MOVE_SEQ_PAGE)          /* 384 ticks, 4/4 */
#define RFM_MOVE_SEQ_POLY 8                             /* notes a step */
#define RFM_MOVE_SEQ_NOTES 4096                         /* notes a project */
#define RFM_MOVE_SEQ_LOCKS 2048                         /* CC locks a project */
#define RFM_MOVE_SEQ_UNDO 2048                          /* undo journal entries */
#define RFM_MOVE_SEQ_SOUNDING 64                        /* sequencer notes sounding at once */
#define RFM_MOVE_SEQ_CAPTURE 4096                       /* Capture: pad events kept (0.5.1: was 1024; 16 bars of
                                                         * 4-note chords a step, on and off) */
#define RFM_MOVE_SEQ_CAPTURE_BARS 16                    /* .. a phrase of at most 16 bars */
#define RFM_MOVE_SEQ_CAPTURE_GAP 2                      /* .. silence of more than 2 bars ends a phrase */
#define RFM_MOVE_SEQ_HOLD_MS 300                        /* a step held this long is a hold (its popup; no tap) */
#define RFM_MOVE_SEQ_RATES 8                            /* (0.14.0) track rates: 1/32 1/16 1/8 1/4 1/2 1 Bar 2 Bars
                                                         * 4 Bars (Elasticat's), each with a feel */
#define RFM_MOVE_SEQ_RATE_16 1                          /* .. 1/16, the default */
/* (0.14.0, the owner's) a track's feel: its rate as is, dotted (x 3/2) or a triplet (x 2/3: the triplet layout, each
 * fourth step key dead) */
enum { RFM_MOVE_FEEL_STRAIGHT, RFM_MOVE_FEEL_DOTTED, RFM_MOVE_FEEL_TRIPLET, RFM_MOVE_FEELS };
/* the trig parameters (the TRIG knob page): kept as CC locks with these numbers (above the MIDI CCs, never sent) */
enum { RFM_MOVE_TRIG_RATCHET = 128,                     /* 2..8 hits in the step (none: 1) */
       RFM_MOVE_TRIG_CHANCE,                            /* 1..99 % (none: 100) */
       RFM_MOVE_TRIG_COND,                              /* 1..43: RFM_MOVE_COND_* (none: always) */
       RFM_MOVE_TRIG_FUNC,                              /* 1..8: RFM_MOVE_FN_* (none: nothing) */
       RFM_MOVE_TRIG_LEAP,                              /* LEAP's n + 15, 0..30 (none: +2) */
       RFM_MOVE_TRIG_JUMP,                              /* JUMP's target step 0..255 (none: the loop's start) */
       RFM_MOVE_TRIG_REPEAT,                            /* REPEAT's n, 1..16 more steps (none: 1) */
       RFM_MOVE_TRIG_LAST = RFM_MOVE_TRIG_REPEAT };
/* conditions: 1..35 A:B (B 2..8, A 1..B: plays on pass A of every B), then these */
enum { RFM_MOVE_COND_AB_N = 35, RFM_MOVE_COND_FILL = 36, RFM_MOVE_COND_NFILL, RFM_MOVE_COND_PRE, RFM_MOVE_COND_NPRE,
       RFM_MOVE_COND_NEI, RFM_MOVE_COND_NNEI, RFM_MOVE_COND_1ST, RFM_MOVE_COND_N1ST, RFM_MOVE_COND_N };
/* the arp (FIZZ's ARP: its patterns, RATE's notes a bar, MODE) */
enum { RFM_MOVE_AP_UP, RFM_MOVE_AP_DN, RFM_MOVE_AP_UPDN, RFM_MOVE_AP_DNUP, RFM_MOVE_AP_UANDD, RFM_MOVE_AP_DANDU,
       RFM_MOVE_AP_CON, RFM_MOVE_AP_DIV, RFM_MOVE_AP_CONDIV, RFM_MOVE_AP_HI1, RFM_MOVE_AP_HI2, RFM_MOVE_AP_LO1,
       RFM_MOVE_AP_LO2, RFM_MOVE_AP_RAND, RFM_MOVE_AP_ROTH, RFM_MOVE_AP_RONC, RFM_MOVE_AP_ASP, RFM_MOVE_AP_N };
enum { RFM_MOVE_AM_OFF, RFM_MOVE_AM_ON, RFM_MOVE_AM_1ST, RFM_MOVE_AM_ONCE, RFM_MOVE_AM_HOLD, RFM_MOVE_AM_N };
#define RFM_MOVE_ARP_RATES 16
#define RFM_MOVE_ARP_LANE 16                            /* the arp's MOD and TRANSPOSE lanes' steps */
enum { RFM_MOVE_AR_NORM, RFM_MOVE_AR_NOTE, RFM_MOVE_AR_MPAT, RFM_MOVE_AR_TPAT, RFM_MOVE_AR_N };   /* RESET */
/* the MOD lane's values (FIZZ's): as played, rest, tie, repeat, lowest -oct, highest +oct, power chord, every held
 * note, then the n-th note played (1..6) */
enum { RFM_MOVE_AL_PLAY, RFM_MOVE_AL_REST, RFM_MOVE_AL_TIE, RFM_MOVE_AL_REPEAT, RFM_MOVE_AL_LO, RFM_MOVE_AL_HI,
       RFM_MOVE_AL_PWR, RFM_MOVE_AL_CHORD, RFM_MOVE_AL_N1, RFM_MOVE_AL_N = RFM_MOVE_AL_N1 + 6 };
typedef struct {
    uint8_t pat, rate, range, chance, mode, gate;       /* RFM_MOVE_AP_*, 0..15, 1..8, 0..100, RFM_MOVE_AM_*, 2..200 % */
    int8_t odist;                                       /* the octaves' step, -24..24 semitones */
    uint8_t quant;                                      /* when a run starts: 0 at once, n: the 1/rate(n-1) grid */
    uint8_t reset;                                      /* RFM_MOVE_AR_* */
    uint8_t mlen, tlen;                                 /* the lanes' lengths 1..16 */
    int8_t lane[2][RFM_MOVE_ARP_LANE];                  /* 0 MOD (RFM_MOVE_AL_*), 1 TRANSPOSE (-48..48) */
    uint8_t down;                                       /* (0.14.0) the octaves go down (Octaves -1..-8): 1. Saved in
                                                         * the blob's CHANCE byte (99 down, 100 up) with OCT DIST
                                                         * stored negated, no new version */
} rfm_move_arp_t;
/* (0.14.0) a track's running arp (RAM): what it holds and where its walk is. Fed by the track's pads (while it is the
 * selected one) and its clip's notes; each held note's holders: pad (bit 0 of src) and the clip's (cnt, until coff) */
typedef struct {
    uint8_t n, down, run, nseq, last;
    uint8_t pitch[8], vel[8], ord[8], seq[32];
    uint8_t src[8], cnt[8];                             /* src bit 0: a pad holds it; cnt: the clip's notes holding it */
    int32_t coff[8];                                    /* .. the last of them ends at this tick */
    uint8_t fromclip;                                   /* the clip has fed it since it last started */
    uint8_t rate;                                       /* the RATE its walk is timed by (t0 + k notes at it) */
    uint16_t pos, oct, steps, mpos, tpos;
    uint8_t lastn, lastp[8];
    int32_t next, k, t0;
} rfm_move_arpst_t;
/* Shift + step (Ableton Move's printed functions, Move manual: step 1 Set Overview, 2 Setup, 3 Workflow Settings,
 * 5 Tempo, 6 Metronome, 7 Groove, 8 Layout (16 Pitches), 9 Keys & Scales, 10 Velocity (a menu), 11 Repeat (Arp),
 * 14 New Clip, 15 Double Loop, 16 Quantize); the menus they open (rfm_move_seq_t.smenu) */
enum { RFM_MOVE_SM_NONE, RFM_MOVE_SM_WORKFLOW, RFM_MOVE_SM_TEMPO, RFM_MOVE_SM_GROOVE, RFM_MOVE_SM_LAYOUT,
       RFM_MOVE_SM_KEYS, RFM_MOVE_SM_ARP, RFM_MOVE_SM_REC, RFM_MOVE_SM_VEL, RFM_MOVE_SM_GLOBAL };
/* the record settings (Shift + Record; saved): what Record does, and how a live take meets what is there */
enum { RFM_MOVE_RM_CAPTURE, RFM_MOVE_RM_RECORD, RFM_MOVE_RM_STEP, RFM_MOVE_RM_N };
enum { RFM_MOVE_RS_OVERDUB, RFM_MOVE_RS_REPLACE, RFM_MOVE_RS_N };
/* step functions (FIZZ's, the OMX-27 Form's) */
enum { RFM_MOVE_FN_NONE, RFM_MOVE_FN_RESTART, RFM_MOVE_FN_REVERSE, RFM_MOVE_FN_FORWARD, RFM_MOVE_FN_FLIP,
       RFM_MOVE_FN_RANDOM, RFM_MOVE_FN_RFUNC, RFM_MOVE_FN_LEAP, RFM_MOVE_FN_JUMP, RFM_MOVE_FN_REPEAT, RFM_MOVE_FN_N };
/* (REPEAT, the owner's: the step plays again on its next n steps, as if copied there; the steps under it skipped; it
 * stops at the loop's end) */
#define RFM_MOVE_SEQ_BPM10_MIN 200                      /* 20.0 .. 300.0 BPM */
#define RFM_MOVE_SEQ_BPM10_MAX 3000
#define RFM_MOVE_PROJECTS 32
#define RFM_MOVE_SAMPLE_BARS 60                         /* the Sampling waveform's bars (2 px apart) */
#define RFM_MOVE_PROJECT_VERSION 10                     /* (versions 1..9 load too; 5 adds FX setup, 6 its 128
                                                         * values a slot, 7 the firmwares' MIDI routes, 8 the launch
                                                         * quantization and each clip's launch settings, 9 (0.13.0) the
                                                         * firmwares: each slot's id, level, settings and track, the
                                                         * slot played, the drum notes, the pads' velocity, 10 (0.14.0)
                                                         * the rate bytes: a base rate and a feel; no new bytes) */
#define RFM_MOVE_AUTOSAVE_MS 2000                       /* an edited project is saved this long after the last edit */
/* the blob: header 24, tracks 16 x 4, clip lengths 256 x 2, notes 2 + n x 7, locks 2 + n x 4, (version 2) loop
 * starts 256, clips made 32, track rates 16, grooves 16, arps 16 x 7, FNV-1a 4 */
#define RFM_MOVE_ARP_BYTES_V2 7                         /* a track's arp in version 2: pattern rate range chance mode
                                                         * gate odist */
#define RFM_MOVE_ARP_BYTES (RFM_MOVE_ARP_BYTES_V2 + 4 + 2 * RFM_MOVE_ARP_LANE)   /* + quant reset mlen tlen, the lanes */
#define RFM_MOVE_PROJECT_EXT_V2 (RFM_MOVE_SEQ_CLIPS + RFM_MOVE_SEQ_CLIPS / 8 + \
                                 RFM_MOVE_SEQ_TRACKS * (2 + RFM_MOVE_ARP_BYTES_V2 + 1 + RFM_MOVE_NBANKS * RFM_MOVE_NKNOBS))
#define RFM_MOVE_PROJECT_EXT_V3 (RFM_MOVE_PROJECT_EXT_V2 + RFM_MOVE_SEQ_TRACKS * (RFM_MOVE_ARP_BYTES - RFM_MOVE_ARP_BYTES_V2 + 4))
#define RFM_MOVE_PROJECT_EXT_V4 (RFM_MOVE_PROJECT_EXT_V3 + RFM_MOVE_SEQ_TRACKS * 8)   /* + the trig defaults (version 4) */
#define RFM_MOVE_PROJECT_EXT_V5 (RFM_MOVE_PROJECT_EXT_V4 + RFM_MOVE_FXSET_BYTES_V5)   /* + FX setup (version 5) */
#define RFM_MOVE_PROJECT_EXT_V6 (RFM_MOVE_PROJECT_EXT_V4 + RFM_MOVE_FXSET_BYTES)   /* .. 128 values a slot (version 6) */
#define RFM_MOVE_PROJECT_EXT_V7 (RFM_MOVE_PROJECT_EXT_V6 + RFM_MOVE_FW_SLOTS)   /* + the MIDI routes (version 7, 0.6.6) */
#define RFM_MOVE_PROJECT_EXT_V8 (RFM_MOVE_PROJECT_EXT_V7 + 1 + RFM_MOVE_SEQ_CLIPS)   /* + the launch quantization,
                                                         * each clip's launch settings (version 8, 0.12.0) */
/* (version 9, 0.13.0) the firmwares: 3 slots x { id 32 (NUL padded, "" empty), level 0..127, layout, scale, root,
 * channel, octave, 8 CC numbers, the sequencer track 0..15 }, then the slot played 0..2, the drum layout's 32 notes,
 * the velocity mode and its fixed level 1..127 */
#define RFM_MOVE_PFW_SLOT_BYTES (RFM_MOVE_FW_ID_MAX + 1 + 5 + RFM_MOVE_NKNOBS + 1)
#define RFM_MOVE_PFW_BYTES (RFM_MOVE_FW_SLOTS * RFM_MOVE_PFW_SLOT_BYTES + 1 + RFM_MOVE_NPADS + 2)   /* (176) */
#define RFM_MOVE_PROJECT_EXT (RFM_MOVE_PROJECT_EXT_V8 + RFM_MOVE_PFW_BYTES)
#define RFM_MOVE_PROJECT_FIXED (24 + RFM_MOVE_SEQ_TRACKS * 4 + RFM_MOVE_SEQ_CLIPS * 2 + 2 + 2 + RFM_MOVE_PROJECT_EXT + 4)
#define RFM_MOVE_PROJECT_MAX (RFM_MOVE_PROJECT_FIXED + RFM_MOVE_SEQ_NOTES * 7 + RFM_MOVE_SEQ_LOCKS * 4)
enum { RFM_MOVE_CLOCK_AUTO = 0, RFM_MOVE_CLOCK_INTERNAL = 1, RFM_MOVE_CLOCK_EXTERNAL = 2 };
/* (0.12.0) launch quantization: when a clicked clip, scene or stop takes effect (1 Bar the default, 0). CLIP: at the end
 * of the loop of the clip the track plays (none: the next bar) */
enum { RFM_MOVE_LQ_BAR = 0, RFM_MOVE_LQ_INSTANT, RFM_MOVE_LQ_BEAT, RFM_MOVE_LQ_2BARS, RFM_MOVE_LQ_4BARS, RFM_MOVE_LQ_CLIP,
       RFM_MOVE_LQ_N };
#define RFM_MOVE_CS_AFTER_NEXT 0x08                     /* a clip's settings: after its loop, the next clip */

typedef struct {                                        /* a note in a clip; sorted by (clip, step, note) */
    uint8_t clip, step, note, vel;
    int8_t micro;                                       /* ticks off the step, -12..+11 */
    uint8_t rsv;
    uint16_t len;                                       /* ticks, 1..256 x 24 */
} rfm_move_note_t;
typedef struct { uint8_t clip, step, cc, val; } rfm_move_lock_t;   /* a CC lock; sorted by (clip, step, cc) */
typedef struct {                                        /* an undo journal entry */
    uint16_t group;
    uint8_t type, rsv;
    uint16_t old, nu;
    rfm_move_note_t n;
} rfm_move_op_t;
typedef struct {
    uint8_t ch, mute;                                   /* (saved) */
    int8_t active, sel;                                 /* (saved) the clip that plays (-1 none), the one edited */
    int8_t queued;                                      /* -2 nothing, -1 stop, 0..15: launch at the next bar */
    uint8_t grow, grow_stop;                            /* recording into an empty clip: it grows a bar at a time */
    uint8_t rate;                                       /* (saved) 0..RFM_MOVE_SEQ_RATES - 1 */
    uint8_t feel;                                       /* (saved, 0.14.0) RFM_MOVE_FEEL_* */
    uint8_t groove;                                     /* (saved) Move's groove 0..150 %: 100 = a triplet swing */
    rfm_move_arp_t arp;                                 /* (saved) */
    uint8_t layout;                                     /* (saved) the pads' layout on this track */
    uint8_t own;                                        /* (saved) bits: 1 its own groove, 2 its own key and scale
                                                         * (else the project's: Global) */
    uint8_t root, scale;                                /* (saved) its own key and scale */
    uint8_t solo;                                       /* (saved) */
    uint8_t tdef[8];                                    /* (saved, version 4) the TRIG page's defaults for new steps:
                                                         * micro + 12, ratchet 1..8, chance 1..100, condition, function,
                                                         * leap + 15, jump step, length (an index of the TRIG lengths) */
    uint8_t cc[RFM_MOVE_NBANKS][RFM_MOVE_NKNOBS];       /* (saved) its knobs' CC numbers, bank by bank */
    int32_t t0;                                         /* the song tick the active clip started at */
    /* the playhead (the engine's) */
    int16_t pos, next;                                  /* the step playing (-1: not yet), the next (-1: not chosen) */
    int16_t rsrc, nrsrc;                                /* REPEAT: the step whose content plays at pos (-1: pos's own), at
                                                         * next */
    uint8_t rleft, nrleft;                              /* .. repeats still to come after pos, after next */
    uint16_t sub, tps;                                  /* ticks into the step, ticks the step lasts (0.14.0: 16 bit,
                                                         * up to 4 Bars dotted, 2304) */
    int8_t dir;                                         /* +1, -1 (REVERSE) */
    uint8_t fire, nfire;                                /* the step's (the next one's) condition and chance passed */
    uint8_t pre;                                        /* the last condition's result (PRE, NEI) */
    uint16_t pass, npass;                               /* loop passes since the launch (1: the first) */
} rfm_move_track_t;
typedef struct { uint8_t ch, note, track, rsv; int32_t off; } rfm_move_sounding_t;
typedef struct { int32_t t; uint8_t note, vel, rsv[2]; } rfm_move_cap_t;   /* t: timeline tick, vel 0 = off */
typedef struct { int32_t t; uint8_t clip, note, vel, on, step; int8_t micro; uint8_t rsv[2]; } rfm_move_recnote_t;
typedef struct {                                        /* a stored project in brief (rfm_move_project_info) */
    uint8_t ok;                                         /* 1: known */
    uint8_t clips;                                      /* clips with notes or locks */
    uint16_t bpm10;
    uint16_t notes[RFM_MOVE_SEQ_TRACKS];                /* notes on each track */
    uint8_t color;                                      /* its colour 1..16 (RFM_MOVE_PROJ_COLORS), 0 the slot's own */
    /* (0.13.0) its firmwares (version 9; older: has_fw 0, ids "") */
    uint8_t has_fw;
    char fw[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX];     /* each firmware slot's id ("" empty) */
} rfm_move_proj_info_t;
#define RFM_MOVE_PROJ_COLORS 16                         /* the projects' colours (the Move's palette) */

typedef struct {
    /* ---- the project (what a blob holds) */
    uint16_t bpm10;                                     /* the internal clock, BPM x 10 */
    uint8_t clock_mode, sel_track;
    rfm_move_track_t tr[RFM_MOVE_SEQ_TRACKS];
    uint16_t len[RFM_MOVE_SEQ_CLIPS];                   /* clip loop lengths, steps 1..256 */
    uint8_t lstart[RFM_MOVE_SEQ_CLIPS];                 /* .. the loop's first step (lstart + len <= 256) */
    uint8_t made[RFM_MOVE_SEQ_CLIPS / 8];               /* bits: clips made (an empty clip, a session pad press) */
    uint16_t nnotes, nlocks;
    rfm_move_note_t notes[RFM_MOVE_SEQ_NOTES];
    rfm_move_lock_t locks[RFM_MOVE_SEQ_LOCKS];
    /* ---- the clock */
    uint64_t now_us, us_rem;                            /* time advanced so far */
    uint64_t ph;                                        /* progress to the next tick, of den */
    uint32_t den_rate;                                  /* the rate den was made for */
    uint32_t num_last;                                  /* (0.15.0) the last advance's tick rate, of den a frame */
    int32_t tl;                                         /* timeline tick (counts every tick; the clock's position) */
    int32_t T;                                          /* song tick while playing */
    uint8_t playing, start_pending, following, ext_run;   /* start_pending 1 now, 2 at the timeline's next bar, 3 count-in */
    int16_t count_in;                                   /* start_pending 3: ticks to go (Rec from stopped: a bar) */
    int32_t ext_clocks;                                 /* MIDI clocks since the reference (-1 before the first) */
    uint64_t ext_last_us, ext_win_us;
    int32_t ext_win_n;
    uint32_t ext_period_us;                             /* one MIDI clock, measured (0 unknown) */
    /* (0.5.5) AUTO, not following yet: a start (FA 1 / FB 2) seen, and the steady clocks counted since */
    uint8_t ext_arm, ext_stall, hush;                   /* ext_stall: EXT, no clock for 500 ms: what sounded let go;
                                                         * hush: ticks that sound nothing (a burst's catch-up) */
    int16_t ext_arm_n;
    uint64_t ext_arm_us, ext_arm_last_us;
    uint16_t ext_cu;                                    /* catch-up ticks since the last rfm_move_seq_advance */
    uint16_t spp;                                       /* the last song position (16ths) */
    uint32_t frame;                                     /* the frame actions are stamped with now */
    rfm_move_sounding_t snd[RFM_MOVE_SEQ_SOUNDING];
    uint8_t nsnd;
    /* ---- recording, capture */
    uint8_t rec;                                        /* armed */
    uint8_t srec;                                       /* Sample's take: 1 armed (the first note starts it), 2 on */
    uint8_t fill;                                       /* FILL (the jog wheel held down) */
    uint32_t rng;                                       /* chance, random functions */
    uint32_t lead_us;                                   /* the host runs the clock this far ahead of what sounds */
    uint8_t metro;                                      /* the metronome (off at first) */
    uint8_t groove, root, scale;                        /* (saved) the project's (Global) groove, key and scale */
    uint8_t color;                                      /* (saved) this project's colour, 0 its slot's */
    uint8_t pbuf_kind, pbuf_src;                        /* projects mode: the host's buffer: 0 none, 1 copied, 2 cut */
    rfm_move_proj_info_t pbuf_info;
    int8_t proj_held;                                   /* .. the project pad held (its colour on knob 1), -1 none */
    uint8_t play_after_load;                            /* .. Play while the project loads: it plays once loaded */
    uint8_t arp_page, arp_sel;                          /* the arp menu's page (0 ARP, 1 its MOD lane, 2 TRANSPOSE),
                                                         * the lane step selected */
    uint32_t tstep_held;                                /* Track 4 held: the track steps down (tap / hold / double) */
    int8_t tstep_tap;                                   /* .. the last tapped, for a double tap */
    uint32_t tstep_tap_ms;
    /* Loop mode's pages: their own clipboard (cut / copy / paste a page as a step) */
    uint8_t pg_kind, pg_src;                            /* 0 empty, 1 copied, 2 cut; the page it came from */
    uint8_t pg_clip;
    uint16_t pg_nn, pg_nl;
    rfm_move_note_t pg_notes[RFM_MOVE_SEQ_POLY * RFM_MOVE_SEQ_PAGE];
    rfm_move_lock_t pg_locks[RFM_MOVE_SEQ_PAGE * 40];
    uint8_t rec_mode, rec_style;                        /* (saved) RFM_MOVE_RM_*, RFM_MOVE_RS_* */
    uint8_t steprec;                                    /* Step mode on: the cursor's step gets what is played */
    int16_t step_cur;                                   /* .. the cursor (a step of the edited clip) */
    uint32_t steps_chord;                               /* .. pads of the chord being written (bits) */
    uint8_t take;                                       /* the live take's mark on its notes (Replace keeps them) */
    uint8_t arec;                                       /* Sample's audio loop: 1 armed, 2 recording, 3 to stop */
    uint8_t samp[RFM_MOVE_SAMPLE_BARS];                 /* .. its recent levels (the Sampling waveform: 1..17 a bar) */
    uint8_t samp_n, samp_head;                          /* .. how many (a take: from none), the next one's place */
    int32_t arec_T;                                     /* .. the song tick it started at */
    uint8_t qstrength;                                  /* Quantize's amount, % (Workflow Settings) */
    uint8_t smenu;                                      /* a Shift + step menu open (RFM_MOVE_SM_*) */
    uint8_t shift_lock;                                 /* Shift latched (a double tap) */
    uint8_t bank;                                       /* the Track buttons' bank: tracks bank*4+1 .. +4 */
    int8_t pad_tap;                                     /* session: the clip pad tapped last (a double tap launches now) */
    uint32_t pad_tap_ms;
    /* (0.11.0, the owner's UX rules) */
    uint8_t pad_tap_shift;                              /* .. that tap was with Shift (a scene: stop) */
    uint8_t pad_tap_play;                               /* (0.13.0) .. that tap was on the clip its track was playing
                                                         * (its double click stops it) */
    int8_t sm_k;                                        /* a Shift menu's knob turned last (-1 the jog / a step), for
                                                         * its header: shown RFM_MOVE_POPUP_MS from sm_ms */
    uint32_t sm_ms;
    uint8_t arp_was[RFM_MOVE_SEQ_TRACKS];               /* each arp's mode before it was switched off (0: On) */
    /* (0.12.0, the owner's) the arp's quick gestures */
    uint8_t arp_sh;                                     /* Shift + step 11 down: 1 waiting (a click opens the menu at
                                                         * the release), 2 held: the arp toggled (the release: nothing) */
    uint32_t arp_sh_ms;                                 /* .. its press (the sequencer clock's ms) */
    uint32_t al_wave_ms;                                /* the step keys passed to a lane: their flash and wave, from */
    uint8_t al_wave;                                    /* .. running (a step pressed ends it) */
    uint16_t al_down;                                   /* a lane page: its step keys down */
    uint8_t al_hold;                                    /* .. the held step + 1 (0 none): held 300 ms (or a pad pressed),
                                                         * the pads are its shortcuts */
    uint8_t al_pads;                                    /* .. the pads taken */
    uint8_t al_lenset;                                  /* .. step 1 held set the lane's length (step 1 + step n) */
    uint32_t al_hold_ms;                                /* .. the held step's press */
    int8_t oct_dir;                                     /* Up / Down: the octave's sweep over the pad rows (1 up, -1) */
    uint32_t oct_ms;                                    /* .. from */
    /* (0.12.0, the owner's) Loop and Mute: a quick click (let go within RFM_MOVE_SEQ_HOLD_MS, nothing else pressed)
     * latches the layer, a hold is the same layer until the release */
    uint8_t mute_latch;                                 /* Mute's layer latched (until Mute's click again or Back) */
    uint8_t mute_end;                                   /* the Mute press that ended the latch (its release: nothing) */
    uint32_t loop_ms, mute_ms;                          /* Loop, Mute pressed (the sequencer clock's ms) */
    uint32_t fill_ms;                                   /* FILL on since (its light, its pads' wave) */
    uint8_t t4_opened;                                  /* Track 4 held opened a Shift menu (it owns the knobs then) */
    uint8_t mods_other;                                 /* Loop, Mute (SEQ_M_*) held and something else pressed: no click */
    /* a held Track's track (a step, or in session view a column's pad): the press's verdict, gated. A hold acts once
     * held RFM_MOVE_SEQ_HOLD_MS (not at the release); a click acts once RFM_MOVE_DOUBLE_MS have passed with no second
     * press (or the Track button is let go); a second press in that time is the double click, at once */
    uint32_t tstep_fired;                               /* bits: this press has acted (its release: nothing) */
    int8_t tk_pend;                                     /* the track whose click waits, -1 none */
    uint8_t tk_pend_kind, tk_pend_fw;                   /* .. 0 select (a step), 1 routed / not (a pad); its firmware */
    uint32_t tk_pend_clk;                               /* .. since when (the sequencer clock's ms) */
    uint32_t tstep_rt0;                                 /* (0.12.x) bits: that track was routed before its step's press
                                                         * (the click routes at the press; the hold toggles from this) */
    /* (0.12.0) session view's clip and scene buffers. The clip buffer is the clipboard (clip_kind 2); armed: the last
     * clip function was a copy or a delete, so Copy + a clip pastes over it (cleared by a paste, Back) */
    uint8_t sv_armed;
    uint8_t sv_xshow, sc_xshow;                         /* (0.13.0, the owner's: "Deleting a clip from session leaves a X
                                                         * box hanging out") a deleted clip's / scene's X shows only
                                                         * until Delete is let go */
    uint8_t sc_kind, sc_src, sc_armed;                  /* the scene buffer: 0 empty, 1 copied, 2 deleted; its row; armed */
    uint16_t sb_has;                                    /* .. bits: the tracks with a clip in it */
    uint16_t sb_nn, sb_nl;
    uint16_t sb_len[RFM_MOVE_SEQ_TRACKS];
    uint8_t sb_lstart[RFM_MOVE_SEQ_TRACKS], sb_cset[RFM_MOVE_SEQ_TRACKS];
    rfm_move_note_t sb_notes[RFM_MOVE_SEQ_POLY * RFM_MOVE_SEQ_MAXLEN];   /* (clip: the track) */
    rfm_move_lock_t sb_locks[RFM_MOVE_SEQ_LOCKS / 2];
    /* (0.12.0) launch quantization: the project's (RFM_MOVE_LQ_*, saved) and each clip's own settings (saved, version
     * 8): bits 0..2 its launch (0 the project's, else RFM_MOVE_LQ_* + 1), bit 3 after its loop: the next clip */
    uint8_t launch_q;
    uint8_t cset[RFM_MOVE_SEQ_CLIPS];
    uint8_t clip_cset;                                  /* the clip clipboard's settings */
    /* (0.12.0) session view: a column's select step held: the clip settings of its track's selected slot */
    int8_t cs_step;                                     /* the step held (-1 none) */
    uint8_t cs_shown;                                   /* .. its settings are shown (held 300 ms, or a knob touched) */
    uint32_t cs_clk;                                    /* .. pressed when (the sequencer clock's ms) */
    int8_t cs_k;                                        /* .. the knob touched or turned last (-1 none) */
    uint8_t layer[8], nlayer;                           /* what Back closes, the latest last (a Shift menu, the grid,
                                                         * Loop's latch, Shift's lock, the clipboard's marks) */
    uint8_t in_seq;                                     /* the settings hold the selected track's layout / CCs */
    uint8_t stash_layout, stash_cc[RFM_MOVE_NBANKS][RFM_MOVE_NKNOBS];   /* .. MIDI / Control mode's, meanwhile */
    uint8_t stash_root, stash_scale;
    uint32_t shift_ms;                                  /* .. its last press */
    /* the arp: (0.14.0) one a track, fed by its pads and its clip (was one, the selected track's pads) */
    rfm_move_arpst_t arps[RFM_MOVE_SEQ_TRACKS];
    uint8_t jr_n, jr_clip[8], jr_note[8];              /* (before 0.14.0: the arp's notes just recorded; unused) */
    uint16_t rec_group;
    rfm_move_recnote_t recn[RFM_MOVE_NPADS];            /* notes being recorded, by pad */
    rfm_move_cap_t cap[RFM_MOVE_SEQ_CAPTURE];
    uint16_t ncap, cap_head;
    /* ---- undo */
    rfm_move_op_t ops[RFM_MOVE_SEQ_UNDO];
    uint16_t un, utop, group, broken;
    /* ---- the clipboard */
    uint8_t clip_kind, cut;                             /* 0 empty, 1 a step, 2 a clip, 3 a track; cut: Shift + Copy */
    uint8_t clip_src, clip_step;                        /* where it came from (track; clip, step) */
    uint16_t clip_len, clip_nn, clip_nl;
    uint8_t clip_lstart;
    rfm_move_note_t clip_notes[RFM_MOVE_SEQ_POLY * RFM_MOVE_SEQ_MAXLEN];
    rfm_move_lock_t clip_locks[RFM_MOVE_SEQ_LOCKS];
    /* ---- projects */
    int8_t proj_cur, proj_pending;
    uint32_t proj_used;
    uint8_t dirty;
    uint64_t dirty_us;
    uint8_t blob[RFM_MOVE_PROJECT_MAX];
    /* ---- the controls */
    uint8_t view;                                       /* 0 track, 1 session */
    uint8_t projects;                                   /* the project grid is up */
    uint8_t mods;                                       /* SEQ_M_* held */
    uint8_t mods_used;                                  /* .. and used with something (no release action) */
    uint8_t copy_src;                                   /* this Copy hold has a source */
    uint8_t page, track_page, scene_off;
    uint8_t nlast;                                      /* step entry's last notes (0: the root note) */
    uint8_t last_note[RFM_MOVE_SEQ_POLY], last_vel[RFM_MOVE_SEQ_POLY];
    uint32_t chord;                                     /* bits: pads held as a chord (track view, no step held) */
    uint8_t pad_vel[RFM_MOVE_NPADS];                    /* .. their velocities */
    uint8_t paste;                                      /* a copy waits for its target (Copy released) */
    uint8_t copy_pasted;                                /* this Copy hold pasted */
    uint16_t step_held;                                 /* bits: steps held (track view) */
    uint16_t step_add, step_edit;                       /* .. pressed onto an empty note / edited while held */
    int8_t step_first;                                  /* the first step held, -1 none */
    uint32_t step_t[RFM_MOVE_NSTEPS];                   /* the clock (ms) when each held step was pressed */
    uint8_t step_popped;                                /* .. its popup is up (a hold) */
    uint8_t trig;                                       /* the knobs are on the TRIG page (before bank 1) */
    int8_t loop_first;                                  /* Loop held: the page step held first, -1 none */
    int8_t loop_tap;                                    /* .. the page tapped last, for a double tap */
    uint32_t loop_tap_ms;
    uint8_t loop_latch;                                 /* Loop tapped: held on (the jog sets the length) */
    uint8_t loop_end;                                   /* the Loop press that ended the latch (its release: nothing) */
    uint32_t tap_ms[4];
    uint8_t ntap;
    uint8_t msg;                                        /* a short message on the screen (SEQ_MSG_*) */
    uint32_t msg_ms, ui_ms;
    rfm_move_proj_info_t info[RFM_MOVE_PROJECTS];       /* what the project grid draws of each slot */
    rfm_move_fxset_t fx;                                /* (saved, version 5) FX setup: the routing, the modules, the mix */
    /* (0.12.x, the owner's 2026-11-22) the FM-1 knob page of a note layout (second: TRIG, FM-1, banks 1..4; not saved,
     * s.bank keeps the CC bank): fwp the page shown (with trig 0); fwp_full its level 2 (the jog's click): the
     * firmware's buttons and the steps too, kept while another page is shown */
    uint8_t fwp, fwp_full;
    /* (0.12.x, the owner's) a Track 1..3 tap into a note layout: the steps show the 16 tracks a moment (tflash_ms: since
     * when, the sequencer clock's ms; tflash: on) */
    uint8_t tflash;
    uint32_t tflash_ms;
    /* (0.13.0, the owner's 2026-11-22) the jog in a Shift menu (not the arp's, not Global settings): sm_sel the knob
     * whose parameter the jog has selected (0: knob 1, at each opening); sm_edit: its click edits it (the turn changes
     * that value as the knob does), a click again selects */
    int8_t sm_sel;
    uint8_t sm_edit;
    /* (0.13.0, the owner's 2026-11-22) the Loop layer: loop_down its page steps down; a page held alone + the jog: the
     * length of every note on that page (loop_jogd: the jog used in this hold, one undo step; loop_dlen: the change so
     * far, ticks, for the popup) */
    uint16_t loop_down;
    uint8_t loop_jogd;
    int16_t loop_dlen;
    /* (0.13.0, the owner's) loop_shown: the page shown was shown by a page step's tap (not reached with Left / Right):
     * its ghost key does not confirm it; loop_tap_grp: the tap waiting for its double tap confirmed ghost bars (the
     * double tap joins its undo step) */
    uint8_t loop_shown, loop_tap_grp;
    /* (0.14.0, the owner's 2026-11-23) each track's knob page, as it was left (0 TRIG, 1 FM-1, 2 ENGINE, 3..6 banks
     * 1..4; not saved): selecting the track again, by any way, brings it back */
    uint8_t kpage[RFM_MOVE_SEQ_TRACKS];
    /* (0.14.0, the owner's 2026-11-24) the ENGINE knob page shown (with trig 0, fwp 0; s.bank keeps the CC bank), and
     * its page list open (the jog's click) */
    uint8_t eng, ep_list;
} rfm_move_seq_t;

/* ---- (0.14.0) engine parameters: what the firmware's ep1 gives (engine-params SPEC.md, sections 3, 4 and 9) */
#define RFM_MOVE_EP_DESC_MAX 65536                      /* an EP1D description's bytes, at most */
#define RFM_MOVE_EP_LAYERS 8
#define RFM_MOVE_EP_PARAMS 2048
#define RFM_MOVE_EP_NONE (-32768)                       /* a value: 0x8000, not on that layer's engine now */
typedef struct { uint16_t pid; char name[11], text[11]; } rfm_move_ep_cell_t;   /* a watched page's knob: pid 0 none */
typedef struct {                                        /* a firmware slot's (the core's copy) */
    uint8_t desc[RFM_MOVE_EP_DESC_MAX];                 /* the description, checked */
    uint32_t n, fnv;                                    /* its length (0: none), its FNV-1a (header byte 20) */
    uint8_t nlay;                                       /* rfm_move_ep_layers given (0: not yet: no page) */
    uint8_t eng[RFM_MOVE_EP_LAYERS], ch[RFM_MOVE_EP_LAYERS];   /* each layer's engine (an index, 0xFF none), channel */
    int16_t val[RFM_MOVE_EP_LAYERS][RFM_MOVE_EP_PARAMS];   /* the values, the description's order */
    rfm_move_ep_cell_t cell[8];                         /* the watched page's cells, */
    int16_t cell_page;                                  /* .. for this page (-1 none) */
    uint8_t cell_layer;                                 /* .. and this layer */
    struct { uint16_t eid; uint8_t page_id, used; } mem[64];   /* (RAM) each engine's page last shown, by engine id */
} rfm_move_ep_slot_t;
typedef struct {
    rfm_move_ep_slot_t s[RFM_MOVE_FW_SLOTS];
    /* the selected track's page now (engine.c ep_resolve): its slot (-1 none), layer, engine (an index, 0xFF the layer
     * is off) and page (an index into the pages section, 0xFFFF none) */
    int8_t slot;
    uint8_t layer, eng;
    uint16_t page;
    /* .. as it was at the last sync (an arrival, an engine changed under the page) */
    int8_t p_slot, p_track;
    uint8_t p_layer, p_eng, p_shown, p_tv;
    /* each track's Layer setting (0 Auto: by its MIDI channel; 1..8 a layer; DESIGN.md section 7: stage 4, RAM and
     * always 0 until then) and its Engine page last shown: the slot, engine id and page id (RAM; saved from stage 3) */
    uint8_t lset[RFM_MOVE_SEQ_TRACKS];
    struct { uint16_t eid; uint8_t page_id, slot; } tmem[RFM_MOVE_SEQ_TRACKS];
    uint8_t click;                                      /* the jog's click pressed on the page (it acts at the release) */
    /* knob turns waiting for the next rfm_move_seq_advance (one per slot, layer and pid: the latest value) */
    struct { uint8_t slot, layer; uint16_t pid; int16_t v; } set[32];
    uint8_t nset;
    /* values sent and shown until the firmware's values agree (or RFMV_EP_OVR_MS passed) */
    struct { uint8_t slot, layer, on; uint16_t idx; int16_t v; uint32_t ms; } ovr[16];
    /* the watch sent last: slot (-1 none), layer, page (-1 none), the track */
    int8_t w_slot;
    uint8_t w_layer, w_track;
    int16_t w_page;
} rfm_move_ep_t;

/* ---- (0.15.0, the owner's 2026-11-24) the sampler app: its values and the host's status (the API is further down,
 * "the sampler app"; README "The sampler") */
enum { RFM_MOVE_SMP_SRC_MASTER, RFM_MOVE_SMP_SRC_SLOT1, RFM_MOVE_SMP_SRC_SLOT2, RFM_MOVE_SMP_SRC_SLOT3,
       RFM_MOVE_SMP_SRC_INPUT, RFM_MOVE_SMP_SRC_STEMS, RFM_MOVE_SMP_SRC_LOOPER, RFM_MOVE_SMP_SRC_KIT,
       RFM_MOVE_SMP_SRC_N };                            /* Source's values (rfm_move_smp_offer's sources: bit each) */
enum { RFM_MOVE_SMP_M_CAPTURE, RFM_MOVE_SMP_M_BARS, RFM_MOVE_SMP_M_THRESHOLD, RFM_MOVE_SMP_M_MANUAL,
       RFM_MOVE_SMP_M_N };                              /* Mode's */
enum { RFM_MOVE_SMP_TO_FILES, RFM_MOVE_SMP_TO_LOOPER, RFM_MOVE_SMP_TO_KIT, RFM_MOVE_SMP_TO_N };   /* To's */
enum { RFM_MOVE_SMP_TAB_RECORD, RFM_MOVE_SMP_TAB_TAKES, RFM_MOVE_SMP_TAB_TOOLS, RFM_MOVE_SMP_TAB_LOOPS,
       RFM_MOVE_SMP_TAB_N };                            /* the tabs (rfm_move_smp_offer's tabs: bit each) */
#define RFM_MOVE_SMP_TABS_BUILT 0x7                     /* the tabs this core has (an offered tab it has not: not shown):
                                                         * Record, (stage 2) Takes, Tools */
#define RFM_MOVE_SMP_LENGTHS 7                          /* Length's: Open, 1, 2, 4, 8, 16, 32 bars */
#define RFM_MOVE_SMP_THR_MIN (-60)                      /* Threshold, dB (1 a detent) */
#define RFM_MOVE_SMP_IN_MIN (-24)                       /* Input level, dB (1 a detent) */
#define RFM_MOVE_SMP_IN_MAX 24
#define RFM_MOVE_SMP_PEAKS 128                          /* the status's peaks, at most */
#define RFM_MOVE_SMP_COLS 58                            /* the wave's columns on the screen (2 px apart) */
#define RFM_MOVE_SMP_FLASH_MS 250                       /* a saved take: Sample's light green this long */
enum { RFM_MOVE_SMP_ST_IDLE, RFM_MOVE_SMP_ST_ARMED, RFM_MOVE_SMP_ST_RECORDING, RFM_MOVE_SMP_ST_SAVING };
typedef struct {                                        /* the host's recorder now (rfm_move_smp_status) */
    uint8_t state;                                      /* RFM_MOVE_SMP_ST_* */
    uint8_t listening;                                  /* 1: Capture mode's buffer is filling (Capture can keep it) */
    uint8_t bar, bars;                                  /* the bar being recorded (1..bars; 0 armed), the bars wanted
                                                         * (0: no bars: Open, Manual, Threshold with Open) */
    uint16_t takes;                                     /* the takes stored (for the Takes tab) */
    uint16_t level;                                     /* the source's level now, 0..32767 (peak, linear) */
    uint32_t frames, rate;                              /* the take's frames so far and the sample rate (its time) */
    uint8_t npeaks;                                     /* peaks given, 0..RFM_MOVE_SMP_PEAKS, oldest first, 0..255 */
    uint8_t span;                                       /* 0: peaks is a rolling history (the newest last, drawn at the
                                                         * right); n: a take with bars runs and its whole length is n
                                                         * peaks, peaks[0] its start (the screen shows npeaks / n done) */
    uint8_t peaks[RFM_MOVE_SMP_PEAKS];
    /* (stage 2) the takes */
    uint32_t play_take;                                 /* the take SMP_PLAY plays now (0: none) .. */
    uint32_t play_pos;                                  /* .. the frame it is at (from the file's start) */
    uint8_t undo;                                       /* 1: SMP_UNDO has something to undo */
} rfm_move_smp_status_t;
enum { RFM_MOVE_SMP_EV_SAVED = 1, RFM_MOVE_SMP_EV_NOTHING = 2, RFM_MOVE_SMP_EV_ERROR = 3 };   /* rfm_move_smp_event */
/* (stage 2) a row of the takes' list (rfm_move_smp_rows): a day (a folder of takes) or a take */
enum { RFM_MOVE_SMP_ROW_DAY = 1, RFM_MOVE_SMP_ROW_TAKE = 2, RFM_MOVE_SMP_ROW_STEMS = 3 };
#define RFM_MOVE_SMP_THUMB 14                           /* a take row's picture: its whole length in 14 peaks */
#define RFM_MOVE_SMP_WPEAKS 64                          /* a wave's columns on the screen, at most (2 px apart) */
#define RFM_MOVE_SMP_NEWEST 0xFFFFFFFFu                 /* SMP_ROWS: the newest day that has takes */
#define RFM_MOVE_SMP_ALL 0xFFFFFFFEu                    /* SMP_ROWS: every take of every day, the newest first (answered
                                                         * with folder RFM_MOVE_SMP_ALL, also when there are none) */
#define RFM_MOVE_SMP_SRC_NONE 255                       /* a take whose source is not known (its sign: "?") */
typedef struct {
    uint32_t id;                                        /* the host's, never 0, the same while the file is (an edit
                                                         * keeps it) */
    uint8_t kind;                                       /* RFM_MOVE_SMP_ROW_* */
    uint8_t src;                                        /* a take: RFM_MOVE_SMP_SRC_* (STEMS for a stems take), or _NONE */
    uint8_t day, month;                                 /* a day: 1..31, 1..12 */
    uint8_t today;                                      /* a day: 1 it is today ("Today") */
    uint8_t bars;                                       /* a take: its bars (0: none, its seconds show) */
    uint16_t bpm10;                                     /* .. its tempo x 10 (0: none) */
    uint16_t takes;                                     /* a day: its takes */
    uint32_t frames, rate;                              /* a take: its length (a stems take: its Master's) */
    uint32_t folder;                                    /* a take: its day's id */
    uint8_t thumb[RFM_MOVE_SMP_THUMB];                  /* a take: its picture, peaks 0..255 */
} rfm_move_smp_row_t;
#define RFM_MOVE_SMP_CACHE 8                            /* rows the core keeps (a window of the list) */
typedef struct {                                        /* (RAM, sampler.c) */
    uint8_t open;                                       /* the app is open (shown when nothing above it hides it) */
    uint8_t tab;                                        /* the tab shown (remembered) */
    uint8_t row;                                        /* the setting the jog is on (RFM_MOVE_SMP_*; remembered) */
    uint8_t on_tabs;                                    /* the jog is on the tabs (turned up past the first row) */
    uint8_t picking;                                    /* .. their click: the jog chooses a tab until a click / Back */
    uint8_t edit;                                       /* the row's click: its popup (the jog changes its value) */
    uint8_t sdown, jdown;                               /* Sample's / the jog click's press was the app's (its release) */
    uint8_t turned;                                     /* a detent turned the rows: three rows until turn_ms + 1 s */
    uint32_t turn_ms;
    uint32_t chg_ms;                                    /* the app's last change (a newer popup of the layer under it
                                                         * shows over it, R3) */
    uint8_t srcs, sets, tabs, dests;                    /* the host's offer (bits), rfm_move_smp_offer */
    rfm_move_smp_status_t st;                           /* the host's status, as last given */
    uint32_t st_ms;                                     /* .. its state's last change */
    uint8_t msg;                                        /* a message up (RFM_MOVE_SMP_EV_*, 0 none) */
    uint32_t msg_ms;
    char msg_a[16], msg_b[16];                          /* .. its two rows */
    uint8_t flash;                                      /* a take saved: green from flash_ms */
    uint32_t flash_ms;
    int16_t ev_bars, ev_bpm10;                          /* the last take saved: its bars, tempo, length */
    int32_t ev_ms;
    /* (stage 2) the takes: the list, a take's actions, Trim, Tools */
    uint8_t scr;                                        /* over the tab: 0 nothing, 1 a take's actions, 2 Trim */
    uint8_t trim_ret;                                   /* Trim's Back / Keep: 1 the take's actions, 0 the tab */
    uint8_t days;                                       /* Takes: the days shown (the left arrow) */
    uint8_t known, dknown;                              /* .. an answer came for the day's takes / the days */
    uint32_t folder;                                    /* .. the day shown (0: none yet, the newest is asked) */
    int16_t sel, dsel;                                  /* .. the take / the day chosen */
    uint16_t total, dtotal;                             /* .. the rows there (as last answered) */
    struct { rfm_move_smp_row_t r; uint32_t f; int16_t i; uint8_t ok; } rc[RFM_MOVE_SMP_CACHE];   /* rows come */
    uint32_t ask_f;                                     /* the last SMP_ROWS asked (folder, first row), 0xFFFF none */
    uint16_t ask_i;
    rfm_move_smp_row_t cur;                             /* the take the actions, Trim and Tools work on (id 0: none) */
    uint8_t ai, ti, fade;                               /* the actions' / Tools' chosen icon; a Fade's popup (1 in, 2
                                                         * out, 0 none) */
    uint8_t fade_v[2];                                  /* .. their lengths (an index of the steps; remembered) */
    struct { uint32_t take, from, count; uint8_t n; uint8_t pk[RFM_MOVE_SMP_WPEAKS]; } pk[2];   /* peaks come */
    uint32_t pa_take, pa_from, pa_count;                /* the last SMP_PEAKS asked */
    uint32_t play_id;                                   /* the take playing as last asked or said (0 none) */
    uint32_t t_start, t_end;                            /* Trim: the part kept (frames) */
    uint8_t t_i, t_move, t_knob;                        /* .. the chosen icon (start, end, play, keep); the marker the
                                                         * jog moves (1 start, 2 end); a knob touched (1, 2) */
    uint32_t t_turn_ms;                                 /* .. a knob turned: zoomed on its marker until + 1 s */
    uint8_t t_turned;
    uint8_t ddown, udown, ldown, rdown, shdown, kdown;  /* Delete, Undo, Left, Right, Shift, the knobs: taken presses */
} rfm_move_smp_t;

/* ---- the state */
typedef struct {
    uint8_t kind;                                       /* what a pressed control did, for its release */
    uint8_t a, b;                                       /* key / button / (channel, note) */
} rfm_move_hold_t;

/* ---- (0.7.0) the takeover: a firmware's own Move interface (FM1VST docs/RFM1-TAKEOVER.md, tk1). The claims: the
 * parts of the Move it takes (their input and their LEDs) */
enum {
    RFM_MOVE_TK_SCREEN = 0x001,                         /* the screen */
    RFM_MOVE_TK_PADS = 0x002,                           /* pads (notes 68..99, poly / channel pressure); LEDs 0..31 */
    RFM_MOVE_TK_STEPS = 0x004,                          /* steps (notes 16..31); LEDs 32..47, the icons 76..91 */
    RFM_MOVE_TK_KNOBS = 0x008,                          /* knobs 1..8 (CC 71..78, touch notes 0..7); rings 55..62 */
    RFM_MOVE_TK_JOG = 0x010,                            /* the wheel (CC 14), its click (CC 3), touch note 9 */
    RFM_MOVE_TK_VOLUME = 0x020,                         /* the volume knob (CC 79), touch note 8 */
    RFM_MOVE_TK_BUTTONS = 0x040,                        /* Shift .. Sample (not the Track buttons); LEDs 48..50, 63..75 */
    RFM_MOVE_TK_TRACKS = 0x080,                         /* Track 1..4 (CC 43..40); LEDs 51..54 */
    RFM_MOVE_TK_ALL = 0x0FF,
};
#define RFM_MOVE_TKF_SPEECH 1                           /* TK_STATE's flags: the screen reader is on */
#define RFM_MOVE_TK_NAME 16                             /* a takeover's name with its NUL (15 printable ASCII) */
typedef struct {
    uint16_t claims;                                    /* RFM_MOVE_TK_*, 0: none (no takeover) */
    uint8_t mode;                                       /* 1: the slot was left in its takeover (Track 1 brings it) */
    char name[RFM_MOVE_TK_NAME];
    uint8_t screen[MR_SCREEN_BYTES];                    /* the firmware's screen, packed */
    uint8_t led[RFM_MOVE_NLEDS][3];                     /* its LEDs (a white one: r its brightness) */
} rfm_move_tk_slot_t;

typedef struct {
    rfm_move_settings_t s;
    rfm_move_act_fn fn;
    void *ctx;
    /* input */
    rfm_move_hold_t pad[RFM_MOVE_NPADS], step[RFM_MOVE_NSTEPS], btn[128];   /* btn: by Move CC */
    uint8_t pad_down[RFM_MOVE_NPADS], pad_press[RFM_MOVE_NPADS];
    uint8_t key_count[RFM_MOVE_NKEYS], btn_count[RFM_MOVE_NBTN], key_press[RFM_MOVE_NKEYS];
    uint8_t note_count[16][128];
    uint8_t track_held;                                 /* bit t: Track t + 1 held */
    uint8_t cc_val[RFM_MOVE_NBANKS][RFM_MOVE_NKNOBS];   /* Control mode's values (not saved), 64 at first */
    uint16_t master;                                    /* MASTER as the host last said or the volume knob set */
    uint8_t vel_last;                                   /* the last velocity a pad played (Fixed starts from it) */
    int16_t battery;                                    /* the Move's battery, 0..100 %, from its STATUS; -1 not yet */
    uint8_t power;                                      /* its power bits (STATUS byte 5; bit 3: the power button held) */
    int8_t edit_pad;                                    /* the drum pad the jog wheel is editing, -1 none */
    uint8_t shift_down;                                 /* Shift is down (MIDI mode: Shift + step 9) */
    int8_t knob_acc[RFM_MOVE_NKNOBS];
    int8_t list_acc[RFM_MOVE_NKNOBS + 1];               /* list-type menu knobs: the ticks short of an item */                   /* FM-1 encoders: the ticks short of a detent (RFM_MOVE_KNOB_TICKS) */
    uint8_t mmenu;                                      /* MIDI mode's Keys & Scales menu is open */
    uint32_t tbtn_ms[4];                                /* a Track button's press (a tap: its release soon after) */
    uint32_t tbtn_clk[4];                               /* .. on the popups' clock (its hold screen after RFM_MOVE_TAP_MS) */
    uint8_t tbtn_sw;                                    /* bits: that press changed the mode */
    /* the screen's popup (README.md, "The screen"): what it shows, for which control, and since when */
    uint8_t pop;                                        /* what (move_int.h POP_*), 0 none */
    uint8_t pop_i;                                      /* its knob (0..7, 8 volume), step, pad or project slot */
    int16_t pop_val;                                    /* an FM-1 encoder's detents in this gesture */
    uint32_t pop_ms;                                    /* the sequencer clock (ms) at its last touch, turn or release */
    uint8_t menu_what, menu_i;                          /* a held Track button's menu: the setting changed last */
    uint32_t menu_ms;                                   /* when it changed last (the sequencer clock's ms) */
    int8_t touched;                                     /* the knob (0..7, 8 volume, 9 jog) touched now, -1 none */
    char label[RFM_MOVE_NENC + 1][RFM_MOVE_LABEL_MAX + 1];   /* the firmware's knob labels ("" = the default) */
    /* the FM-1's LEDs */
    uint8_t levels[RFM_MOVE_NLEVELS], play_green;
    int8_t led_of[41];                                  /* panel id (0..13 button, 14 + k key) -> levels index */
    /* the Move's LEDs as last written (rfm_move_leds) */
    uint8_t sent[RFM_MOVE_NLEDS][3];
    uint8_t sent_ok[RFM_MOVE_NLEDS];
    /* the screen */
    uint8_t thumb[64][64];                              /* the last FM-1 screen, 1 bit a pixel (thresholded) */
    /* the sequencer (about 150 KB: keep rfm_move_t static or on the heap, not on a small stack) */
    rfm_move_seq_t seq;
    /* (0.4.0) the firmware browser, the library, FX setup */
    rfm_move_fw_state_t fw;
    /* (0.4.3) Back: a click (released within RFM_MOVE_TAP_MS, nothing else pressed) closes the latest layer, or with
     * none flips to / from the projects view; held + Track 1..3 switches the firmware played */
    uint8_t back_down, back_used;
    uint8_t back_trk;                                   /* bit t: Track t + 1 pressed with Back held (its release too) */
    uint32_t back_ms;
    /* (0.12.x, the owner's) Back held: its shortcuts (+ Left, Up, Loop, Play, Delete; the other buttons dead) */
    uint16_t back_cut;                                  /* bit: a button pressed with Back held (its release: nothing) */
    uint32_t back_clk;                                  /* Back's press on the popups' clock (the shortcuts' lights
                                                         * after RFM_MOVE_TAP_MS: none on a click) */
    /* (0.5.3) latch, MIDI and Control modes (Track 2 held + the top row's pad 8): RAM only */
    uint8_t latch;                                      /* on */
    uint8_t nlat;                                       /* notes latched now */
    uint8_t lat_ch[RFM_MOVE_LATCH_MAX], lat_n[RFM_MOVE_LATCH_MAX];   /* .. each one's channel and note (one hold each) */
    /* (0.5.4) */
    uint8_t lat_slot[RFM_MOVE_LATCH_MAX];               /* .. the firmware slot it went to (bit 7: parked, its slot no
                                                         * longer the active one: it holds no note_count) */
    uint8_t emit_dest;                                  /* (emit_midi's MIDI dest while a latched note lets go) */
    char pop_text[32];                                  /* rfm_move_popup's words */
    /* (0.5.6) labels: the live ones (lbl_has 1: given; "" = no job) and the buttons' static names (FM1P_B_* order) */
    char lbl[RFM_MOVE_LBL_N][RFM_MOVE_LABEL_MAX + 1];
    uint8_t lbl_has[RFM_MOVE_LBL_N];
    char btn_nm[RFM_MOVE_NBTN][RFM_MOVE_LABEL_MAX + 1];
    uint32_t pop_t0;                                    /* when this popup's subject came up (a long label scrolls) */
    uint16_t pop_delay;                                 /* POP_LABEL: shown once held this long (ms) */
    uint8_t pop_blank;                                  /* a knob touched, not turned: its name, no value yet */
    /* (0.5.7) readouts: the last one for each knob (FM1P_E_* role), and the one shown on its own */
    rfm_move_readout_t rd[8], rd_pop;
    uint16_t rd_ms;                                     /* rd_pop's time up */
    uint8_t jog_touch;                                  /* (0.5.8) the jog wheel touched now */
    uint8_t pop_shown;                                  /* (0.5.8) POP_LABEL: drawn with words while its control was held */
    /* (0.6.0) speech (rfm_move_set_speech): what was said last; not saved, rfm_move_init zeroes it */
    uint8_t speech;                                     /* on */
    char said[RFM_MOVE_SAY_MAX];                        /* the popup's words said last ("": none up) */
    uint16_t said_subj;                                 /* .. its subject */
    uint16_t view_subj;                                 /* the base view said last */
    uint32_t said_t0;                                   /* .. the popup's stamp (a new message, a popup up again) */
    uint32_t focus_h;                                   /* the selection's words said last (a hash) */
    uint8_t say_tr;                                     /* the transport said last: bit 0 playing, 1 recording */
    uint8_t tick_pop;                                   /* the popup rfm_move_seq_advance looked at last (one the */
    uint32_t tick_t0, tick_msg;                         /* .. clock brings up: a held step's), the message's stamp */
    uint32_t tick_menu;                                 /* (0.11.0) .. a held Track's track acted by the clock (said) */
    uint32_t msg_n;                                     /* the sequencer's messages so far (each one's stamp) */
    /* (0.6.1) */
    char say_now[48];                                   /* a pressed FM-1 button's words, said at the call's end */
    uint32_t say_lbl_t0;                                /* .. the label popup they stand for (not said again) */
    uint32_t say_h[32];                                 /* the watched values' words said last (hashes) */
    uint16_t say_mute, say_solo, say_steps;             /* the tracks muted, soloed; the shown page's steps with notes */
    uint32_t say_skey;                                  /* .. which page of which clip those steps are */
    int8_t say_act[RFM_MOVE_SEQ_TRACKS], say_q[RFM_MOVE_SEQ_TRACKS];   /* each track's clip playing, queued */
    uint8_t say_mods;                                   /* the sequencer's modifiers held */
    /* (0.6.2) Global settings (Shift + step 2): a list on the jog wheel */
    uint8_t gl_row;                                     /* the row selected */
    uint8_t gl_edit;                                    /* its value being edited (the jog changes it) */
    /* (0.6.11) the firmware speaks for its own controls (fm1-reader): rfm_move_set_fw_speech */
    uint8_t fw_speaks;
    /* (0.6.12) the screen reader's mode (settings.speech; unset: this default), a jog touch to say the screen, a word
     * that is said in any mode (the mode's own change) */
    uint8_t say_default, say_jogstart, say_now_force;
    /* (0.6.3) */
    uint8_t say_back;                                   /* Back let go this call: say where it left you if nothing else */
    uint16_t say_n;                                     /* words said so far (a call that said nothing: 0 more) */
    uint16_t say_bdown;                                 /* .. when Back went down */
    uint32_t say_fwms;                                  /* the projects view's Track / page / catalog popup said last */
    uint8_t say_stepev;                                 /* (0.6.5) this call is a step press or release */
    /* (0.6.7) knob readouts for speech: each one's control label when it came (a new label, the Shift page: stale),
     * any readout seen (the firmware has them: the Move's detents are never said), a turn waiting for its readout */
    char rd_lbl[8][RFM_MOVE_LABEL_MAX + 1];
    uint8_t rd_any;
    uint8_t say_turn, say_force, say_late;              /* the knob slot + 1 turned, its readout to say even if the
                                                         * same, the turn's words due with no readout */
    uint32_t say_turn_ms;
    /* (0.6.8) a readout's page: the FM-1 buttons held when it came (Shift: GLO); the full titles seen, by control,
     * page and label (a touch before a readout says the title, not the 10-character label); a turn's readout came */
    uint16_t rd_mods[8];
    struct { uint8_t slot, expl; uint16_t mods; char lbl[RFM_MOVE_LABEL_MAX + 1], title[RFM_MOVE_LABEL_MAX + 1]; } say_tc[24];
    uint8_t say_tcn, say_rdnew;
    uint8_t del_src;                                    /* this Delete hold's last step action: 0 none, 1 cut, 2 paste
                                                         * (back to none when Delete is let go; the clipboard stays) */
    /* (0.7.0) the takeover: each slot's (rfm_move_set_takeover, _tk_screen, _tk_leds), what the host was told last
     * (TK_STATE) and what the firmware holds now (its releases are due as the takeover closes) */
    rfm_move_tk_slot_t tk[RFM_MOVE_FW_SLOTS];
    uint8_t tk_sent_on, tk_sent_slot, tk_sent_flags;
    uint16_t tk_sent_claims;
    uint32_t tk_pad;                                    /* bit p: pad p held by the firmware */
    uint16_t tk_step, tk_touch;                         /* steps; touches (knobs 0..7, 8 volume, 9 jog) */
    uint32_t tk_btn[4];                                 /* by CC (buttons, Track buttons, the jog's click) */
    uint8_t tk_vtouch;                                  /* the volume knob touched now (volume + Back: out) */
    uint8_t tk_back;                                    /* Back held back (BUTTONS without TRACKS): 1 down, unsent */
    uint8_t tk_back_trk;                                /* .. a Track pressed meanwhile (nothing sent at its release) */
    uint8_t tk_back_eat;                                /* Back's release is nobody's (volume + Back took its press) */
    /* (0.8.0) the Track buttons, proposal 2 (README "The Track buttons") */
    uint32_t tbtn_up[4];                                /* a tap's release (input ms; 0 none): a press this soon after is
                                                         * a double click (RFM_MOVE_DOUBLE_MS) */
    uint8_t tbtn_dbl;                                   /* bits: this press was a double click (its release: nothing) */
    uint8_t jog_enc;                                    /* the FM-1 layout's jog: 0 SELECT, 1 PRESETS, 2 ALGO */
    uint8_t slot_ccv[RFM_MOVE_FW_SLOTS][RFM_MOVE_NKNOBS];   /* each slot's CC values (a note layout's knobs), 64 */
    uint8_t lk_word;                                    /* the lock menu's last change (LK_* in move.c) */
    uint8_t fx_route;                                   /* Routing's column page (the Mixer and Routing flip) */
    uint8_t say_hold;
    uint8_t tbtn_used;                                  /* bits: something else pressed while that Track was held */
    /* (0.9.0) each firmware's solo (proposal 3): the track + 1 alone reaching it (0 none), its filter as it was */
    uint8_t fsolo[RFM_MOVE_FW_SLOTS];
    uint16_t fsolo_save[RFM_MOVE_FW_SLOTS];
    uint32_t fw_tap_ms;                                 /* (0.9.0) the browser: the pad tapped last (a double tap loads) */
    uint8_t fw_tap_cell;                                /* .. its cell + 1, 0 none */
    uint8_t clk_on;                                     /* (0.9.0) the clock is going out (Start sent) */
    uint32_t in_ms;                                     /* (0.9.0) rfm_move_input's now_ms, the latest */                                   /* (speech) the held Tracks whose menu showed (said once up) */
    /* (0.13.0, RFM_MOVE_CAP_PROJFW) a project's firmwares (projfw.c) */
    char pfw_want[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX];   /* the open project's firmwares (a missing one kept) */
    uint8_t pfw_touched;                                /* bits: the slot changed by the user since the project loaded */
    uint8_t pfw_on;                                     /* the project's firmwares loading (its screen up) */
    int8_t pfw_proj;                                    /* .. the project, 0..31 */
    int16_t pfw_st[RFM_MOVE_FW_SLOTS];                  /* .. each slot's status (projfw.c PFW_*, RFM_MOVE_PFW_*) */
    uint32_t pfw_ms[RFM_MOVE_FW_SLOTS];                 /* .. its last news (the sequencer clock's ms) */
    uint8_t pfw_play;                                   /* .. Play pressed meanwhile: the transport starts once loaded */
    uint8_t pfw_msg;                                    /* bits: the slots whose firmware is missing / failed: said */
    uint8_t pfw_fail;                                   /* .. bits: failed (not missing) */
    uint32_t pfw_msg_ms;                                /* .. from (a second each) */
    uint32_t pfw_hash;                                  /* the project's firmware settings as last seen (an edit: the
                                                         * project marked changed) */
    char pfw_seen[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX];   /* .. the slots' ids as last seen */
    uint8_t pfw_raw_ok;                                 /* a host without RFM_MOVE_CAP_PROJFW: the loaded project's */
    uint8_t pfw_raw[RFM_MOVE_PFW_BYTES];                /* .. version 9 part, saved back unchanged */
    char pfw_id[RFM_MOVE_FW_SLOTS][RFM_MOVE_FW_ID_MAX];  /* the firmwares the loading screen names */
    uint8_t pfw_live;                                   /* a project was loaded or made (its edits may be saved) */
    int8_t pfw_msg_k;                                   /* the missing slot said last (-1 none) */
    /* (0.14.0, the owner's 2026-11-23) */
    uint8_t tbtn_shift;                                 /* bits: that Track was pressed with Shift (its track settings
                                                         * opened at the press; its release: nothing) */
    /* the FM-1 knob page's jog on a knob (the owner's: "click the Jogwheel to make it control one of the knobs like it
     * does in the FM-1 layout", then: "No double click on the Jogwheel to toggle takeover. Single click and shift +
     * jogwheel to change jogwheel mode"): fwj: Shift + click has put the jog on the target (Knobs+Keys: always on it);
     * jog_enc_s each slot's jog target (jog_enc is the active slot's, the FM-1 layout's and the page's) */
    uint8_t fwj;
    uint8_t jog_enc_s[RFM_MOVE_FW_SLOTS];
    /* the projects page (the owner's 2026-11-23): pj_press the project pad pressed (its info at once; a click, let go
     * within RFM_MOVE_TAP_MS with nothing done meanwhile, opens it, or saves the open one), pj_press_ms when, pj_used:
     * something else done with it held (knob 1's colour); pj_undo: the project deleted last can come back with Undo
     * (its slot pbuf_src; 0 none); pj_del / pj_del_ms: the pad deleted (a red flash) and when; pj_ack / pj_ack_ms: the
     * open project saved by its click (its pad blinks) */
    int8_t pj_press;
    uint8_t pj_used, pj_undo;
    int8_t pj_del, pj_ack;
    uint32_t pj_press_ms, pj_del_ms, pj_ack_ms;
    /* (0.14.0) the FM-1 page's level 2 (Knobs+Keys): Delete and Copy are the sequencer's while held (fwg: bit 0
     * Delete, bit 1 Copy; fwg_ms their press, input ms); fwg_other: something else pressed meanwhile (no firmware
     * tap at the release) */
    uint8_t fwg, fwg_other;
    uint32_t fwg_ms[2];
    /* (0.14.0, item 7) rd_at: when each knob slot's readout came (the popups' clock, ms); turn_ms: when the knob
     * popup's turn began (a readout older than it is not this turn's) */
    uint32_t rd_at[8], turn_ms;
    /* (0.14.0, the owner's 2026-11-24: "If I go to another track or mode and come back, the lock state should exit")
     * where the FM-1 knob page's lock (Knobs+Keys, the jog on a knob) was set: the mode, slot, track, view and layout
     * (rfmv_fwp_ctx); another one ends it */
    uint32_t fwp_key;
    /* (0.14.0, the owner's 2026-11-24) the FM-1 page's Knobs: the Move's Shift held + an FM-1 knob turned put the
     * firmware's GLO down (let go with Shift) */
    uint8_t fwp_glo;
    /* (0.14.0) the interface's own dice (the arp lanes' random fill, the browser's and the projects' Random): a
     * xorshift state of its own, so the engine's (seq.rng) sequences stay as they were; 0: the default seed. A host
     * or a test may set it */
    uint32_t ui_rng;
    /* (0.14.0) the browser's Random (step 13): its roll's start (ms) and the bank cells it rolls from and to */
    uint32_t fwr_ms;
    int8_t fwr_from, fwr_to;
    /* (0.14.0) the projects page's step 13 (a new random project): pjr_new its slot, pjr_prev the project open before
     * (Undo: pj_undo 2), pjr_down step 13 held (white) */
    int8_t pjr_new, pjr_prev;
    uint8_t pjr_down;
    /* (0.14.0, the owner's 2026-11-24) engine parameters: each slot's ep1 (about 290 KiB for the three slots), the
     * ENGINE page's state (engine.c) */
    rfm_move_ep_t ep;
    /* (0.15.0, the owner's 2026-11-24) the sampler app (sampler.c; RFM_MOVE_CAP_SAMPLER) */
    rfm_move_smp_t smp;
} rfm_move_t;
/* (0.5.4) a host's message on the screen ("FM-1 linked"), as a mode's name shows: big, a second (RFM_MOVE_POPUP_MS),
 * over any screen; a newer popup replaces it. ASCII; wrapped at spaces onto two rows, cut where it does not fit
 * (at most 31 bytes kept). Call it on the core's thread; rfm_move_screen_timed is 1 while it is up (draw again) */
void rfm_move_popup(rfm_move_t *m, const char *text);
/* (0.5.3) latch on or off (MIDI and Control modes' pads); off lets every latched note go. rfm_move_latch: 1 on.
 * (0.5.4) A latched note stays on the firmware slot it was played on: switching the active slot leaves it sounding
 * there, and its note off goes there later (MIDI with dest = slot + 1) */
void rfm_move_set_latch(rfm_move_t *m, int on);
int rfm_move_latch(const rfm_move_t *m);

/* settings NULL: the defaults. fn gets every action (it may be NULL). */
/* the library's version, "MAJOR.MINOR.PATCH" (RFM1CS_VERSION when it was built) */
const char *rfm_move_version(void);
void rfm_move_init(rfm_move_t *m, const rfm_move_settings_t *settings, rfm_move_act_fn fn, void *ctx);
void rfm_move_input(rfm_move_t *m, const mr_event_t *ev, uint32_t now_ms);
/* every held FM-1 key, button and MIDI note let go (ups, pressure 0, note offs); the track buttons forgotten */
void rfm_move_release_all(rfm_move_t *m);
/* (releases all on a change) (0.9.0) the active slot's layout: RFM_MOVE_MODE_FM1 the FM-1 layout (out of a takeover),
 * SEQ, MIDI or CONTROL its note layout (the last one it had, Scale at first: the sequencer on its track), TAKEOVER its
 * takeover (nothing if it has none) */
void rfm_move_set_mode(rfm_move_t *m, int mode);
int rfm_move_mode(const rfm_move_t *m);                 /* (0.7.0) RFM_MOVE_MODE_* now (TAKEOVER: the active slot's) */
/* (0.8.0) a slot's layout (RFM_MOVE_LAY_*), as its Track button's menu sets it: the active slot's is played at once
 * (outside the Sequencer). -1 refused (a slot or layout out of range, TK on a slot with no takeover); 0 done or
 * nothing to change. SETTINGS */
int rfm_move_set_slot_layout(rfm_move_t *m, int slot, int lay);
int rfm_move_slot_layout(const rfm_move_t *m, int slot);   /* -1 no such slot */
int rfm_move_seq_lock(const rfm_move_t *m, int slot);   /* (0.8.0) the sequencer track 0..15 locked to the slot */
void rfm_move_set_scale(rfm_move_t *m, int scale, int root);
void rfm_move_set_master(rfm_move_t *m, int value);    /* MASTER as the host has it (the volume knob moves from it) */
/* the Move's battery from Move Remote's STATUS (battery8, byte 4, as the surface reports it: taken as a percentage,
 * clamped to 100; power8, byte 5); shown in the Global Settings menu. Returns 1 when it changed (a redraw) */
int rfm_move_set_battery(rfm_move_t *m, int battery, int power_bits);
/* Sample's audio loop: the recorded audio's peaks, 0..255 each (the web page: one a ~16 ms window of the take, from
 * the worklet), drawn as the Sampling screen's scrolling waveform. Taken only while the loop records (armed and
 * waiting for its bar: flat); a new take starts empty. Returns how many were taken (a redraw when > 0). */
int rfm_move_sample_levels(rfm_move_t *m, const uint8_t *peaks, int n);
/* a knob's label from the firmware (fm1core labels: by encoder, RFM_MOVE_E_MASTER for MASTER); NULL or "" = default */
void rfm_move_set_label(rfm_move_t *m, int enc, const char *text);
/* (0.5.6) all of the firmware's live labels at once, DYNAMIC-LABELS order (RFM_MOVE_LBL_*, fw_label(fw, i) as it
 * comes); lbl NULL clears them all. An entry NULL: none given (a knob: its printed name; a button: its static name
 * below; a key: nothing); "" = no job (nothing shown, a knob's popup neither). Copied (16 characters at most).
 * FM-1 mode: a button held RFM_MOVE_LABEL_HOLD_MS shows its label in the left half (a tap shows nothing); 0.5.8: with
 * the jog wheel touched, a button pressed shows its label at once; while any FM-1 button is held, every other button,
 * pad, step (their keys) and knob pressed shows its label at once. Read when drawn: the label as the firmware has it
 * now, live while held (one that only gets words after the press shows them then); nothing shown while held: nothing
 * after the release either. The knobs' entries also set rfm_move_set_label's. */
void rfm_move_set_labels(rfm_move_t *m, const char *const lbl[RFM_MOVE_LBL_N]);
/* (0.5.6) the firmware's own button names (its descriptor's button_names, FM1P_B_* order; NULL entries or names
 * NULL: none): what a button shows when its live label is NULL */
void rfm_move_set_button_names(rfm_move_t *m, const char *const names[RFM_MOVE_NBTN]);
/* (0.5.7) a readout from the firmware (its rd1 record when seq moves). control: a DYNAMIC-LABELS slot 0..48, or -1:
 * the control in use (a knob turned or touched, the jog turned, a button held: within RFM_MOVE_RD_PAIR_MS of its last
 * move, else none). kind RFM_MOVE_RD_*; title / value printable ASCII (16 kept; NULL ""); frac 0..1000 a bar, -1
 * none; flags RFM_MOVE_RD_BIPOLAR; ms how long its own popup stays (0: RFM_MOVE_POPUP_MS).
 * A knob's (slots 0..7, or -1 paired with a knob or the jog) is kept: that knob's popup shows it in place of the
 * detents (touched: what it controls and the last value; turned: updated live). Shown at once if its popup is up,
 * else as a popup of its own; anything else (a preset's name, a message, a button's) is a popup of its own. FM-1
 * mode and MIDI's NOTES view only (the left half); elsewhere a knob's is only kept. */
void rfm_move_readout(rfm_move_t *m, int control, int kind, const char *title, const char *value, int frac, int flags,
                      int ms);
void rfm_move_readouts_clear(rfm_move_t *m);            /* (0.5.7) a firmware changed (a slot switch, a load): forget */
/* the panel's wiring (FM1P_KEYMAP_INIT's shape: id at [row][col], rows 1..4 = LED lines 0..3); NULL = the FM-1's */
void rfm_move_set_keymap(rfm_move_t *m, const int8_t keymap[6][11]);
/* the FM-1's LEDs: levels[col * 4 + line] 0..255 and PLAY's green level 0..255 */
void rfm_move_panel(rfm_move_t *m, const uint8_t levels[RFM_MOVE_NLEVELS], uint8_t play_green);
/* the Move LED writes now due (at most max; the rest on the next call). Returns how many */
int rfm_move_leds(rfm_move_t *m, rfm_move_led_t *out, int max);
void rfm_move_leds_invalidate(rfm_move_t *m);          /* a new connection: every LED is written again */
/* the Move's 128 x 64 screen, packed (MR_SCREEN_BYTES). rgb565: the FM-1's 240 x 240 screen, or NULL to keep the
 * last one. Popups time out on the sequencer's clock: call rfm_move_seq_advance in every mode */
void rfm_move_screen(rfm_move_t *m, const uint16_t *rgb565, uint8_t out[MR_SCREEN_BYTES]);
/* the FM-1's screen (240 x 240 RGB565) into the thumbnail now, as rfm_move_screen(m, rgb565, ..) takes it (a host that
 * draws later, with NULL, keeps it current this way) */
void rfm_move_thumb(rfm_move_t *m, const uint16_t *rgb565);
/* 1: the screen changes with the clock now (a popup or a message up, the sequencer playing or counting in): draw it
 * again as rfm_move_seq_advance runs, not only after input or a new FM-1 frame */
int rfm_move_screen_timed(const rfm_move_t *m);

/* ---- the sequencer's clock and transport
 * The clock: call rfm_move_seq_advance for every block of time, at the start of the block (playing or not; it
 * also runs blinking, tap tempo's display and autosave). Actions it gives carry `frame`: the offset in this call's
 * units at which the host plays them (sample accurate when frames are audio frames). rate 1000000 = microseconds.
 * The external clock: the firmware's MIDI out realtime bytes, one message a call: F8 clock (24 PPQN), FA start,
 * FB continue, FC stop, F2 lsb msb song position. RFM_MOVE_CLOCK_INTERNAL (0.5.5: the default, and what a project
 * saved before 0.5.5 with AUTO loads as) ignores it all. In RFM_MOVE_CLOCK_AUTO the external clock wins only once a
 * master shows itself (0.5.5: FA or FB, then 24 steady clocks at 20..300 BPM; the sequencer starts at the master's
 * next bar), so a firmware's own FA / FC with no clock stream (a looper) never takes the transport; it stays the
 * master's until FC or 500 ms with no clock. RFM_MOVE_CLOCK_EXTERNAL follows every F8, FA, FB and FC; 0.5.5: 500 ms
 * with no clock while playing lets every sounding note go and the position holds until clocks come back, and a burst
 * catches up at most 3 clocks out loud a block, the rest skipped silently. Its events are stamped frame 0 (now). */
void rfm_move_seq_advance(rfm_move_t *m, uint32_t frames, uint32_t rate);
void rfm_move_seq_midi_in(rfm_move_t *m, const uint8_t *msg, int n);
void rfm_move_seq_play(rfm_move_t *m, int on);         /* as Play in Sequencer mode (1 start, 0 stop) */
void rfm_move_seq_set_bpm(rfm_move_t *m, int bpm10);   /* the internal clock, BPM x 10 (200..3000) */
int rfm_move_seq_bpm10(const rfm_move_t *m);           /* the tempo in use (measured when following) */
/* (0.15.0, for a host's recorder) the song's position in ticks (RFM_MOVE_SEQ_TPS a step, 96 a beat) at the first frame
 * of the next rfm_move_seq_advance call, to a fraction of a tick; -1 while not playing. per_frame (NULL: not wanted):
 * the ticks a frame (the last call's units) at the last call's tempo, so the position at that call's first frame is
 * the result less frames * per_frame (a jump inside it, to bar 1, shows there as a jump) */
double rfm_move_seq_pos(const rfm_move_t *m, double *per_frame);
void rfm_move_seq_set_clock(rfm_move_t *m, int mode);  /* RFM_MOVE_CLOCK_* */
/* how far ahead of what sounds the host advances the clock (the web player: its 30 ms lead): recordings and
 * Capture place what is played this much earlier, where it was heard */
void rfm_move_seq_set_lead_us(rfm_move_t *m, uint32_t us);
const char *rfm_move_seq_rate_name(int rate);           /* "1/16" .. "4 Bars" */
const char *rfm_move_seq_feel_name(int feel);           /* (0.14.0) "Straight", "Dotted", "Triplet" */
int rfm_move_seq_following(const rfm_move_t *m);       /* 1: the external clock drives the sequencer now */

/* ---- projects: 32 slots the host stores (the core never does IO)
 * A blob: "RMSQ", version, then the project; little-endian, every field checked on load, an FNV-1a check at the
 * end; at most RFM_MOVE_PROJECT_MAX bytes. */
int rfm_move_project_save(rfm_move_t *m, uint8_t *out, uint32_t cap);   /* bytes, or -1 (cap too small) */
int rfm_move_project_check(const uint8_t *b, uint32_t n);                /* 0 a valid blob, else < 0 */
/* the host's answer to PROJECT_LOAD (or at start, the last project): slot 0..31 becomes the current project; b NULL
 * or n 0: an empty project. 0 ok; < 0 an invalid blob: nothing changes. Stops the sequencer (notes released).
 * May be called from inside the action callback. */
int rfm_move_project_load(rfm_move_t *m, int slot, const uint8_t *b, uint32_t n);
void rfm_move_project_dir(rfm_move_t *m, uint32_t used);   /* bit n: slot n holds a project */
/* (0.14.0) a new project in the empty slot 0..31, as a click on an empty project pad does: the open project stored
 * first (PROJECT_SAVE), then the new one current at once (empty, a new project's routing; with RFM_MOVE_CAP_PROJFW
 * its three firmware slots emptied: PROJECT_FW "" each) and stored (PROJECT_SAVE). The transport as it was. 0 done;
 * -1 refused: the slot out of range or holding a project, or a project's load still pending */
int rfm_move_project_new(rfm_move_t *m, int slot);
/* what the project grid and its popup draw of a stored project (its tempo, clips, notes a track): the host passes
 * each stored blob once (at start, after an import); saves, loads and deletes keep it up to date. 0 ok; < 0 an
 * invalid blob (the slot is drawn without its content) */
int rfm_move_project_info(rfm_move_t *m, int slot, const uint8_t *b, uint32_t n);
void rfm_move_project_flush(rfm_move_t *m);            /* the autosave now, if the project changed */
int rfm_move_project_slot(const rfm_move_t *m);        /* the current slot */

/* ---- (0.4.0) the firmware browser, the library, FX setup, states (the host guide's v0.4.0 section) */
/* the catalog: the host's array (the contract above). n 0 or entries NULL: none (the browser is on, empty). The
 * first call turns the browser on: Shift + step 1 then opens on the firmware track played, and Session there is FX
 * setup; a host that never calls it keeps the projects view as it was (Track 4) */
void rfm_move_fw_list(rfm_move_t *m, rfm_move_fw_t *entries, int n);
/* entry index's installed version changed (a download or update done, a firmware removed: NULL). The string stays
 * valid until the next rfm_move_fw_list (the core stores the pointer in entries[index].installed) */
void rfm_move_fw_installed(rfm_move_t *m, int index, const char *version);
/* what the three slots hold now (ids; NULL or "" empty), after FW_LOAD / FW_UNLOAD or at start */
void rfm_move_fw_slots(rfm_move_t *m, const char *const ids[RFM_MOVE_FW_SLOTS]);
/* a load's or download's progress for that firmware: 0..100 (100 done), RFM_MOVE_FW_FAILED, RFM_MOVE_FW_BUSY ("Too
 * busy": the host refused it) */
void rfm_move_fw_progress(rfm_move_t *m, const char *id, int pct);
/* the stored library blob at start (NULL / 0: an empty library). 0 ok; -1 not a library blob (the library is left
 * empty). Its slot ids and levels are the ones the host restores (then rfm_move_fw_slots) */
int rfm_move_fw_library(rfm_move_t *m, const uint8_t *blob, uint32_t n);
/* the library as a blob now (FW_LIBRARY's bytes): its length, 0 cap too small */
uint32_t rfm_move_fw_library_save(rfm_move_t *m, uint8_t *out, uint32_t cap);
/* the library's FW_LIBRARY now, if it changed (the session's flush calls it) */
void rfm_move_fw_flush(rfm_move_t *m);
/* how many firmwares the host sounds at once, 1..3 (default 1). The active slot always sounds; the others sound in
 * slot order while voices are left (rfm_move_fw_sounding); a loaded slot that does not, blinks its Track LED */
void rfm_move_fw_voices(rfm_move_t *m, int n);
int rfm_move_fw_sounding(const rfm_move_t *m);          /* bit s: slot s sounds */
int rfm_move_fw_active(const rfm_move_t *m);            /* the slot the controls play, 0..2 */
int rfm_move_fw_level(const rfm_move_t *m, int slot);   /* 0..127 */
/* the firmware on that slot has these states stored (bit k: state slot k), at start and after a firmware change */
void rfm_move_fw_states(rfm_move_t *m, int slot, uint32_t used);
/* the host's FX modules of a kind (RFM_MOVE_FX_*), the same contract as the catalog's array; n 0: none (a host with
 * no modules: FX setup has no MIDI FX / Audio FX page). Slots keep their module by name */
void rfm_move_fx_modules(rfm_move_t *m, int kind, const rfm_move_fx_module_t *list, int n);
/* the host refused a module (too busy): the slot is emptied, "Too busy" shown */
void rfm_move_fx_refused(rfm_move_t *m, int kind, int slot);
/* the whole FX setup as actions now (FX_LOAD and FX_PARAM for every slot, MIX for every firmware, ROUTE): after the
 * modules are given at start. A project load does it too (when the browser is on) */
void rfm_move_fx_sync(rfm_move_t *m);

/* (0.5.10) what the host offers. RFM_MOVE_CAPS_ALL (the default): three firmware slots and FX setup (the Move's
 * RemoteFM14Move). A host with one firmware and no FX (the web page, the phone apps) passes 0: the browser is the same
 * (favourites, All, categories, colours, info, states, the jog's load) and loads into slot 0, and
 *  - RFM_MOVE_CAP_SLOTS off: one slot. Tracks 2 and 3 are dark and do nothing in the projects view; Back + Track and
 *    Down (FM-1 mode) switch nothing; Track 2 / 3 held + volume sets nothing; the host's slot ids past 0 are ignored;
 *    voices are 1; rfm_move_fw_set_active / _fw_set_level refuse a slot past 0;
 *  - RFM_MOVE_CAP_FX off: no FX setup, no Mixer. Menu (Session) never opens it (a firmware track: out of the view, as
 *    Back; Track 4: as before the browser); no FX_LOAD / FX_PARAM / MIX / ROUTE is emitted (rfm_move_fx_sync and a
 *    project load send none); rfm_move_fx_set_module / _param / _page and rfm_move_route_set refuse (-1), and
 *    rfm_move_mix_set refuses a pan or a send (a level alone is rfm_move_fw_set_level's).
 * Call it after rfm_move_init, before rfm_move_fw_list (any time is safe: the view closes what it no longer has).
 * The project keeps its FX setup untouched, so a project made on the Move loads back there unchanged. */
enum { RFM_MOVE_CAP_SLOTS = 1, RFM_MOVE_CAP_FX = 2, RFM_MOVE_CAPS_ALL = 3 };
/* (0.13.0) RFM_MOVE_CAP_PROJFW (not in RFM_MOVE_CAPS_ALL: a host asks for it, rfm_move_fw_caps(m, RFM_MOVE_CAPS_ALL |
 * RFM_MOVE_CAP_PROJFW)): the host keeps a copy of each firmware's memory per project and can load a project's
 * firmwares (README "Projects that hold their firmwares", "States"). With it:
 *  - a version 9 project's load sets each slot's level, layout settings and track, the slot played, the drum notes and
 *    the velocity, and asks for its firmwares (RFM_MOVE_ACT_PROJECT_FW; the loading screen until rfm_move_proj_fw has
 *    settled every slot); a new project empties the slots; a change of any of these marks the project edited;
 *  - a held project in the grid shows its firmwares;
 *  - States are a layer (Track 1..3 held + the top row's pad 7, or the browser's Loop) with the clips' gestures (a
 *    stored state loads, an empty one saves, Copy / Delete + a state: STATE_COPY / _PASTE / _DELETE), step 14 New
 *    (STATE_NEW) and Undo after it (STATE_UNDO). (0.14.0) Pad 1 is the open project's own sound, the others a
 *    library; no state is active (the STATE_* actions' block above).
 * Without it (an older host) a project load never touches the firmwares (a version 9 project's firmware part is saved
 * back as it was), and States keep the 0.12.0 gestures. */
enum { RFM_MOVE_CAP_PROJFW = 4 };
void rfm_move_fw_caps(rfm_move_t *m, int caps);
int rfm_move_fw_caps_get(const rfm_move_t *m);
/* (0.13.0) the host's answer to RFM_MOVE_ACT_PROJECT_FW: 0..99 loading (a percentage; each one restarts the slot's
 * RFM_MOVE_PFW_WAIT_MS), RFM_MOVE_PFW_READY (the firmware is in the slot with the project's memory; its Track lights),
 * RFM_MOVE_PFW_MISSING (not on the device: the slot is left empty, the project keeps wanting it), RFM_MOVE_PFW_FAILED.
 * A slot with no news for RFM_MOVE_PFW_WAIT_MS (the sequencer's clock) is taken as failed. Ignored with no load under
 * way or for a slot already settled */
#define RFM_MOVE_PFW_READY 100
#define RFM_MOVE_PFW_MISSING (-1)
#define RFM_MOVE_PFW_FAILED (-2)
#define RFM_MOVE_PFW_WAIT_MS 20000
void rfm_move_proj_fw(rfm_move_t *m, int slot, int status);
int rfm_move_proj_fw_loading(const rfm_move_t *m);       /* 1: a project's firmwares are loading (its screen is up) */
/* (0.13.0) the state active on that slot's firmware now (k 0..31, -1 none): after the host loaded a project's copy of
 * the firmware's memory it compares it with the stored states and says which one it equals. Also any time the host
 * knows better (a web page's edit). (0.14.0) Ignored with RFM_MOVE_CAP_PROJFW: no state is active (pad 1 is the
 * project's own sound); a host need not call it */
void rfm_move_fw_state_cur(rfm_move_t *m, int slot, int k);
int rfm_move_states_open(const rfm_move_t *m);           /* (0.13.0) the States layer's slot, -1 closed */

/* ---- (0.4.1) the host's edits: FX setup, the mix, the routing and the firmware slots changed outside the Move (a web
 * page), applied as the Move's own edit applies them. Call them on the core's thread (where rfm_move_input is
 * called). Each makes the same change, emits the same action through the act callback (the session passes it on as
 * EV_ACTION, so the host's own handler follows) and marks the same things dirty: the project (its autosave, as after
 * any edit) or the library (FW_LIBRARY, RFM_MOVE_FW_LIB_SAVE_MS later). Nothing is emitted when nothing changes.
 * No popup is shown, except the Track popup of rfm_move_fw_set_active in the projects view.
 * Returns -1 refused (an argument out of range: nothing changed), 0 done (or nothing to change), 1 done and the Move's
 * screen shows it now (the projects view, FX setup): redraw (rfm1cs_session_redraw). */
/* the firmware the Move's controls play (FW_ACTIVE, the library). In the projects view (not FX setup) it is a Track
 * 1..3 press: that firmware track shown with its Track popup (FW_ACTIVE only if the slot changes; the view's track
 * may change SETTINGS). Any slot 0..2, loaded or not, as the Track button */
int rfm_move_fw_set_active(rfm_move_t *m, int slot);
/* a slot's level 0..127 (FW_LEVEL, the library), as the Mixer's knob 1 or Track held + volume */
int rfm_move_fw_set_level(rfm_move_t *m, int slot, int level);
/* a firmware slot's mix; -1 (or sends NULL, or a send -1) leaves that value. The level as rfm_move_fw_set_level
 * (FW_LEVEL, the library); pan 0..127 (64 centre) and the sends to the audio FX busses 0..127: one MIX for the slot
 * (it carries all five values), the project. A level-only change emits FW_LEVEL alone, as the Move does */
int rfm_move_mix_set(rfm_move_t *m, int slot, int level, int pan, const int sends[RFM_MOVE_FX_SLOTS]);
/* the routing: each firmware's track filter (bit t: track t + 1; any of the 16, 0 none) and each MIDI FX slot's (one
 * track's bit, or 0xFFFF all; any other value is all, as the Move offers only these); NULL or -1 leaves one. One
 * ROUTE (all six filters), the project */
int rfm_move_route_set(rfm_move_t *m, const int fw_filt[RFM_MOVE_FW_SLOTS], const int mfx_filt[RFM_MOVE_FX_SLOTS]);
/* a slot's module: an index in the kind's rfm_move_fx_modules list, -1 empty. Its parameters take their defaults
 * (each param's def, clamped to its words or 0..127), even when it is the module already there, as the jog's press
 * loads it. FX_LOAD (no FX_PARAM for the defaults: the host's module starts at them), the project */
int rfm_move_fx_set_module(rfm_move_t *m, int kind, int slot, int module_index);
/* a loaded module's parameter (index < its nparams and RFM_MOVE_FX_PARAMS, any page): a word's index (clamped to its words) or
 * 0..127 (clamped). FX_PARAM, the project. -1: no module there, or no such parameter */
int rfm_move_fx_set_param(rfm_move_t *m, int kind, int slot, int index, int value);
/* FX setup's page (RFM_MOVE_FX_PAGE_*; settings.fx_page), as Track 1..4 pressed in FX setup: SETTINGS. -1: a page
 * the host has no modules for (MIDI FX, Audio FX). 1 when FX setup is on screen (its module list closed) */
int rfm_move_fx_set_page(rfm_move_t *m, int page);

/* ---- (0.6.0) words for a screen reader: what the Move's screen shows, as text, when it changes (README "Speech").
 * Off by default: no words are made and nothing else changes. On, the core says (RFM_MOVE_ACT_SAY) at the end of each
 * call that can change the screen (rfm_move_input, _popup, _readout, _fw_progress, _fw_slots, _fw_states,
 * _set_latch, _fw_set_active, _set_mode, _seq_play; rfm_move_seq_advance only for the transport's start and stop):
 *  - RFM_MOVE_SAY_EVENT: the base view, only when it changes ("MIDI mode", "Firmwares, Slot 1, Felucca", "FX setup,
 *    MIDI FX", "Sequencer mode", "Session"); a popup closing over it says nothing;
 *  - RFM_MOVE_SAY_VALUE: a popup's or the selection's words whenever they change ("Volume 75%", "Scale: Dorian",
 *    "Felucca, not installed"). The host debounces (a VALUE replaces a pending VALUE); the core has no rate limit.
 * Turning it on takes the screen as said (the host names the first view itself) */
#define RFM_MOVE_HAS_SPEECH 1                           /* (hosts test this to compile their wiring) */
#define RFM_MOVE_SAY_TURN_MS 250                        /* (0.6.7) a knob turned and no readout this long after: its
                                                         * words again (the end of its range) */
enum { RFM_MOVE_SAY_VALUE = 0, RFM_MOVE_SAY_EVENT = 1 };
void rfm_move_set_speech(rfm_move_t *m, int on);
int rfm_move_speech(const rfm_move_t *m);              /* 1 on */
/* (0.6.11) the firmware speaks for its own controls (fm1-reader's SysEx SAYs): while set, the core says nothing for
 * them: an encoder turned (its count, its readouts, the range's end repeat), a readout's own popup, an FM-1 button's
 * or key's label or press. RemoteFM1's own words stay: modes, the projects view, menus, the volume, held Track
 * menus, Global Settings, the sequencer, and a touched knob's name. Off by default; any time */
void rfm_move_set_fw_speech(rfm_move_t *m, int fw_speaks);
/* (0.6.12) the screen reader's mode, the owner's: Off (the default), On (always), Jog (only while the jog wheel is
 * touched: a touch says the screen as it is, rfm_move_words, then every word while held; let go: silent). The user
 * picks it in Global Settings (row Speech; saved in the settings, version 6); its change is said in any mode
 * ("Speech off"). Hosts keep rfm_move_set_speech(m, 1) on (the core then tracks the screen) and gate their own words
 * on rfm_move_speech_open. rfm_move_set_speech_default: the mode while the settings have none (never chosen: an
 * older image, the first run): RFM_MOVE_SPEECH_ON when the system's screen reader is on, so a blind user is never
 * locked out */
enum { RFM_MOVE_SPEECH_OFF = 0, RFM_MOVE_SPEECH_ON = 1, RFM_MOVE_SPEECH_JOG = 2 };
void rfm_move_set_speech_default(rfm_move_t *m, int mode);
int rfm_move_speech_mode(const rfm_move_t *m);         /* RFM_MOVE_SPEECH_* now */
int rfm_move_speech_open(const rfm_move_t *m);         /* 1: words may be said now (On, or Jog with the jog touched) */
int rfm_move_fw_speech(const rfm_move_t *m);           /* 1 set */
/* what the screen shows now, in words: its base view, then ", " and its popup or selection if any (a host's "say it
 * again", the tests). Works with speech off too. Returns the length written (out NUL-terminated; cut to cap - 1) */
int rfm_move_words(const rfm_move_t *m, char *out, int cap);

/* ---- (0.6.6) MIDI from a firmware routed to other firmwares and out (the Routing page: hold a firmware's pad, tap
 * another firmware's pad or the Out pad under them). A slot's mask: bit s (0..2) its MIDI goes to firmware slot s
 * too (never its own), RFM_MOVE_MROUTE_OUT to "Out", the host's MIDI out (on the Move: USB-A and network MIDI); no
 * cycle (A to B to A) is ever set. The host forwards each slot's MIDI out (its channel messages) to its targets and,
 * with OUT, to its MIDI out. The masks travel in
 * every RFM_MOVE_ACT_ROUTE (act.mroute[3]) and are saved with the project (version 7; older ones load with none) */
#define RFM_MOVE_MROUTE_OUT 8
/* ---- (0.6.9) the favourites' order (Favorites list; knob 2 there moves the selected one). The ids in order (n of
 * them; ids[] at most cap filled; the pointers live until the library changes): returns how many favourites there
 * are. The host's reorder: the ids given first, in that order (ids not favourites ignored), the rest after them as
 * they were; FW_LIBRARY later as for any library change. -1 refused (NULL), 0 nothing to change, 1 done and on screen */
int rfm_move_fw_favorites(const rfm_move_t *m, const char **ids, int cap);
int rfm_move_fw_favorites_set(rfm_move_t *m, const char *const *ids, int n);
int rfm_move_midi_route(const rfm_move_t *m, int slot);   /* a slot's mask (0..15), -1 no such slot */
/* the host's edit (a web page): the slot's whole mask. -1 refused (a slot out of range, a bit past 15, its own bit,
 * a cycle, no FX setup: RFM_MOVE_CAP_FX off); 0 done or nothing to change; 1 done and on screen. ROUTE, the project */
int rfm_move_midi_route_set(rfm_move_t *m, int slot, int mask);

/* ---- (0.7.0) the takeover (README "Takeover"; FM1VST docs/RFM1-TAKEOVER.md). The host reads a slot's tk1 record and
 * hands the core what it says; the core shows it and sends the firmware its controls (TK_INPUT, TK_STATE).
 * Track 1 tapped in FM-1 mode opens the active slot's takeover (its name shown and said: "<name>, takeover"), Track 1
 * tapped in it (TRACKS not claimed) goes back to FM-1 mode; from MIDI, Control or Sequencer mode Track 1 goes back to
 * whichever of the two the slot was left in (each slot remembers). Volume touched + Back: FM-1 mode from any takeover
 * (Back not sent). A slot switch closes the shown takeover; the slot switched to opens as it was left. Unclaimed
 * parts keep FM-1 mode's meaning and LEDs. The projects view and the Shift menus (Global Settings) close it while
 * they are up. Call these on the core's thread; slot 0..2 (others ignored). rfm_move_tk_screen / _leds only store: while
 * the slot is shown (rfm_move_tk_shown) the host redraws and flushes the LEDs after them, as after any change. The core
 * never clears a slot's takeover itself: a firmware unloaded or replaced needs rfm_move_set_takeover(m, slot, 0, ..) */
/* claims RFM_MOVE_TK_* (0: no takeover: Track 1 does not offer it, an open one closes and the slot is in FM-1 mode
 * again); name: the mode's name (printable ASCII, 15 kept; NULL ""), shown and said on entering. Any time (a firmware
 * that changes its claims while it runs: the record's claims after every call); a new firmware in the slot: claims 0
 * first, or its own */
void rfm_move_set_takeover(rfm_move_t *m, int slot, unsigned claims, const char *name);
/* the firmware's screen (the record's, when scr_seq moved): shown while its takeover is with SCREEN claimed */
void rfm_move_tk_screen(rfm_move_t *m, int slot, const uint8_t packed[MR_SCREEN_BYTES]);
/* its LEDs (the record's, when led_seq moved), the LED list's order (RFM_MOVE_L_*): only the claimed ones are used;
 * a white one (63..91) takes r as its brightness (0..255, scaled to the Move's 0..127) */
void rfm_move_tk_leds(rfm_move_t *m, int slot, const uint8_t rgb[RFM_MOVE_NLEDS][3]);
/* its words (the record's say, when say_seq moved): a SAY (slot set) now, only while that slot's takeover is shown
 * and words may be said (rfm_move_speech_open). interrupt 1: SAY_VALUE with down 1 (stop what is being said first);
 * 0: SAY_EVENT (after it). Printable ASCII, RFM_MOVE_SAY_MAX - 1 kept */
void rfm_move_tk_say(rfm_move_t *m, int slot, const char *words, int interrupt);
int rfm_move_tk_shown(const rfm_move_t *m);             /* the slot whose takeover is shown now, -1 none */
/* (0.12.x) a note layout's FM-1 knob page shown now: 0 no, 1 its knobs reach the firmware, 2 its buttons and steps too
 * (the screen then the FM-1's). For a host's information only: it passes labels, readouts and frames as ever */
int rfm_move_fwp_level(const rfm_move_t *m);

/* ---- (0.14.0, the owner's 2026-11-24) engine parameters: a firmware's ep1 (engine-params SPEC.md section 9; README
 * "Engine parameters"). Call them on the core's thread (where rfm_move_input runs); slot 0..2 (others ignored).
 * The host, for each slot after each of its frames: desc_seq moved: _describe (refused: no Engine page for the slot),
 * then _layers and _values for every layer (a description taken resets them); lay_seq: _layers; val_seq: _values for
 * each layer; cell_seq: _cells. A slot loaded, unloaded or replaced: _describe(m, slot, NULL, 0) first, then the new
 * firmware's once it has booted */
int rfm_move_ep_check(const uint8_t *desc, uint32_t n);   /* 0: a valid EP1D blob (every field in range, every index
                                                         * inside, every string printable); else < 0. Never reads past n */
int rfm_move_ep_describe(rfm_move_t *m, int slot, const uint8_t *desc, uint32_t n);   /* copied; NULL / 0: none. 0 ok,
                                                         * < 0 refused (the slot then has none) */
void rfm_move_ep_layers(rfm_move_t *m, int slot, int n, const uint8_t engine[], const uint8_t midi_ch[]);   /* each
                                                         * layer's engine (an index into the engines, 0xFF off) and MIDI
                                                         * channel 0..15 (0xFF none), the record's */
void rfm_move_ep_values(rfm_move_t *m, int slot, int layer, const int16_t *val, int n);   /* the description's order;
                                                         * RFM_MOVE_EP_NONE: not on that layer's engine now */
void rfm_move_ep_cells(rfm_move_t *m, int slot, int layer, int page, const rfm_move_ep_cell_t cells[8]);   /* the
                                                         * watched page's cells (EP_WATCH) */
int rfm_move_ep_has(const rfm_move_t *m, int slot);     /* 1: an Engine page can show for this slot (a description and
                                                         * its layers taken) */

/* ---- (0.15.0, the owner's 2026-11-24) the sampler app: the host's recorder (RemoteFM14Move) as an app on the Move
 * (README "The sampler"). RFM_MOVE_CAP_SAMPLER (not in RFM_MOVE_CAPS_ALL: rfm_move_fw_caps(m, ... |
 * RFM_MOVE_CAP_SAMPLER)): Sample is the app (Sample's audio loop, RFM_MOVE_ACT_AUDIO_REC, is off); without it nothing
 * changes. The app takes the screen, the jog and Back (and Sample, its record key); the pads, steps, knobs and arrows
 * stay the layer's under it. Sample opens it (the FM-1 layout: Sample stays the FM-1's SEL unless the app is open or a
 * take runs; Track 1..4 held + Sample opens it there too); Track 1..3 held + Sample: that slot as the source, Track 4:
 * the master (nothing records); Back: a popup or the tab choice ends, else the app closes (a take goes on: Sample
 * brings it back). The projects view keeps Sample (the favourite key); a takeover that claims the buttons keeps it; a
 * takeover is not shown while the app is open (as under a Shift menu). Call these on the core's thread. */
enum { RFM_MOVE_CAP_SAMPLER = 8 };
/* what the host offers (bits; any time): sources by RFM_MOVE_SMP_SRC_* (default Master .. Stems, 0x3F), settings by
 * RFM_MOVE_SMP_* (the rows; default all, 0x7F), tabs by RFM_MOVE_SMP_TAB_* (default Record; only the ones in
 * RFM_MOVE_SMP_TABS_BUILT show), To's values by RFM_MOVE_SMP_TO_* (default Files). A value no longer offered moves to
 * the first one offered (SMP_SET). 0 for a mask: no change */
void rfm_move_smp_offer(rfm_move_t *m, unsigned sources, unsigned settings, unsigned tabs, unsigned dests);
/* the recorder now (as often as the host likes, about 30 a second). Returns 1 when the app shows it (draw again) */
int rfm_move_smp_status(rfm_move_t *m, const rfm_move_smp_status_t *st);
/* an event: RFM_MOVE_SMP_EV_SAVED (a take stored: bars 0 for none, bpm10 its tempo x 10, ms its length; in Capture
 * mode "Captured", else "Saved"; Sample's light green RFM_MOVE_SMP_FLASH_MS), _NOTHING (Capture had nothing),
 * _ERROR (reason: a few printable words, "Disk low"). A message a second on the app; said. Returns 1 when shown */
int rfm_move_smp_event(rfm_move_t *m, int kind, int bars, int bpm10, int ms, const char *reason);
void rfm_move_smp_sync(rfm_move_t *m);                 /* every setting's SMP_SET again (with the capability) */
int rfm_move_smp_shown(const rfm_move_t *m);           /* 1: the app is on the screen now */
int rfm_move_smp_value(const rfm_move_t *m, int setting);   /* a setting's value as SMP_SET sends it */
/* (the owner's 2026-11-25: a host's own face for the settings, its page's Sampler window) a setting changed by the host,
 * as the app's popup changes it: value as SMP_SET sends it (Length: the bars, 0 Open). The app shows it at once, the
 * SMP_SET comes back (the host's recorder follows it as always), the settings are saved (SETTINGS). Returns 1 changed
 * (draw again), 0 the same value, -1 refused: no capability, a setting or a value not offered, out of range (Length:
 * not one of its bars), a take armed or running (as the app: no settings then) */
int rfm_move_smp_set(rfm_move_t *m, int setting, int value);
/* (stage 2, the owner's 2026-11-24) the takes (README "The sampler: the takes"; the actions SMP_ROWS .. SMP_UNDO above).
 * Offer the Takes and Tools tabs (rfm_move_smp_offer, tabs 0x7) and answer these; a host that offers only Record sees
 * none of it. The tabs: Record, Takes (three rows of every take, the newest on top: the source's sign, the length, the
 * wave; Delete held + the jog's click deletes; the click: the take's actions Play, the five tools, Delete), Tools (the
 * chosen take: Trim, Normalize, Reverse, Fade in, Fade out); Trim takes knobs 1 (start) and 2 (end) and Shift (fine);
 * Undo (the host's one step) in Takes and Tools; outside Record Sample plays the take (SMP_PLAY, no SMP_PRESS) and Back
 * goes to the tabs before it closes the app. Every answer may come late or not at all. */
/* SMP_ROWS' answer: asked the folder the act asked for (its take), folder the one answered (the newest day's id when
 * RFM_MOVE_SMP_NEWEST was asked; 0 with total 0: no takes at all), total its rows, rows[0..n) its rows from `from`.
 * Returns 1 when the screen shows them (draw again) */
int rfm_move_smp_rows(rfm_move_t *m, uint32_t asked, uint32_t folder, int total, int from, int n,
                      const rfm_move_smp_row_t *rows);
/* SMP_PEAKS' answer: n peaks of the take's frames [from, from + count). Returns 1 when shown */
int rfm_move_smp_peaks(rfm_move_t *m, uint32_t take, uint32_t from, uint32_t count, int n, const uint8_t *peaks);
/* SMP_EDIT's / SMP_UNDO's answer: op (RFM_MOVE_SMP_OP_*), ok 1 done (0: a short message "Not done"), row the take as it
 * is now (an edit: its new length; UNDO: the take brought back or restored; NULL: none, as after DELETE). The list is
 * asked again. Returns 1 when shown */
int rfm_move_smp_done(rfm_move_t *m, int op, int ok, const rfm_move_smp_row_t *row);
/* the list changed (a take saved, a file changed outside): the rows shown are asked again */
void rfm_move_smp_changed(rfm_move_t *m);
/* a take was saved (after rfm_move_smp_event's SAVED): its row and the mode it was recorded in (RFM_MOVE_SMP_M_*). With
 * the app open and Takes offered: Capture shows its actions with Play chosen and plays it once (SMP_PLAY) when the song
 * is stopped; Manual and Threshold open Trim on it; Bars nothing. Back from them: the tab they came over. The list asked
 * again. Returns 1 when shown */
int rfm_move_smp_saved(rfm_move_t *m, const rfm_move_smp_row_t *take, int mode);

/* ---- the mapping, for hosts' help pages and the tests */
int rfm_move_pad_key(int pad);                          /* FM-1 mode: the key a pad plays, -1 none */
int rfm_move_step_key(int step);                        /* FM-1 mode: the white key a step plays */
int rfm_move_button_of(int move_cc);                    /* FM-1 button a Move button presses, -1 none */
int rfm_move_knob_enc(int knob);                        /* FM-1 mode: the encoder knob 0..7 turns, -1 none */
/* the MIDI modes: the note a pad plays now (-1 none: out of range) with these settings */
int rfm_move_pad_note(const rfm_move_settings_t *s, int pad);
const char *rfm_move_mode_name(int mode);
const char *rfm_move_layout_name(int layout);
const char *rfm_move_scale_name(int scale);
const char *rfm_move_enc_name(int enc);                 /* "SELECT" .. "PRESETS", "MASTER" */

#ifdef __cplusplus
}
#endif
#endif
