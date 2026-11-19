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
#define RFM1CS_VERSION_MINOR 5
#define RFM1CS_VERSION_PATCH 5
#define RFM1CS_VERSION "0.5.5"

#define RFM_MOVE_NKEYS 27
#define RFM_MOVE_NBTN 14
#define RFM_MOVE_NENC 7
#define RFM_MOVE_NPADS 32
#define RFM_MOVE_NSTEPS 16
#define RFM_MOVE_NKNOBS 8
#define RFM_MOVE_NBANKS 4
#define RFM_MOVE_NLEVELS 44                             /* the FM-1's LEDs: levels[col * 4 + line], 11 columns */
#define RFM_MOVE_LABEL_MAX 16

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

/* the four modes (Track 1..4) and the MIDI modes' pad layouts */
enum { RFM_MOVE_MODE_FM1 = 0, RFM_MOVE_MODE_MIDI = 1, RFM_MOVE_MODE_CONTROL = 2, RFM_MOVE_MODE_SEQ = 3 };
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
    RFM_MOVE_ACT_PROJECT_SAVE = 8,                      /* index slot 0..31: store data/len (a project blob) */
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
    RFM_MOVE_ACT_STATE_DELETE = 29,                     /* .. forget it */
};
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
                                                         * note off: the slot it was played on) */
} rfm_move_act_t;
typedef void (*rfm_move_act_fn)(void *ctx, const rfm_move_act_t *a);
/* pressure 0..127 as emu_hal_t / fm1panel_t key_lean 0..100 (as rfm_panel_lean) */
static inline uint8_t rfm_move_lean(int p) { return (uint8_t)(((p < 0 ? 0 : p > 127 ? 127 : p) * 100 + 63) / 127); }

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
} rfm_move_settings_t;
enum { RFM_MOVE_VEL_DYNAMIC, RFM_MOVE_VEL_FIXED, RFM_MOVE_VEL_SOFT, RFM_MOVE_VEL_HARD, RFM_MOVE_VEL_LINEAR, RFM_MOVE_VEL_N };
/* (the jog's order: Dynamic, Fixed, Soft, Linear, Hard; LINEAR came last, so older settings keep their values) */
const char *rfm_move_vel_name(int v);                   /* "DYNAMIC", "FIXED", "SOFT", "HARD", "LINEAR" */
int rfm_move_vel(const rfm_move_settings_t *s, int v);  /* a pad's velocity 1..127 through the setting */
#define RFM_MOVE_LIST_TICKS 5                           /* a list-type menu knob (scale, root, layout ..): ticks an item */
#define RFM_MOVE_KNOB_TICKS 2                           /* the Move's knob ticks an FM-1 encoder detent (the jog: 1) */
#define RFM_MOVE_SETTINGS_VERSION 4                     /* (versions 1, 2 and 3, 77, 78 and 80 bytes, load too: no
                                                         * MIDI screen, no velocity setting, no projects view state) */
#define RFM_MOVE_SETTINGS_BYTES_V3 (6 + 7 + RFM_MOVE_NBANKS * RFM_MOVE_NKNOBS + RFM_MOVE_NPADS + 3)
#define RFM_MOVE_SETTINGS_BYTES (RFM_MOVE_SETTINGS_BYTES_V3 + 6)
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

/* ---- (0.4.0) the firmware browser: the projects view's Tracks 1..3 are three firmware slots (README.md,
 * "Firmwares"; the host guide's v0.4.0 section has the whole contract)
 *
 * The catalog is the host's array (rfm_move_fw_list): the core keeps the pointer, reads it only on the thread that
 * calls the core, inside its calls, and never allocates. The array, and every string and thumbnail its entries point
 * to, stay valid and unchanged until the next rfm_move_fw_list call (a new list: a new array, then the call; never an
 * edit of the live one). The one exception is the core's own: rfm_move_fw_installed writes entries[i].installed.
 * Entries come in the order the pads show them (A-Z by name, as the catalog lists them). */
#define RFM_MOVE_FW_SLOTS 3
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
    const char *installed;                              /* the version on the Move; NULL or "": not on the Move */
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
    int8_t st_cur[RFM_MOVE_FW_SLOTS];                   /* .. the one loaded or saved last, -1 */
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
#define RFM_MOVE_SEQ_RATES 10                           /* track rates: 1/32T 1/32 1/16T 1/16 1/8T 1/8 1/4T 1/4 1/2T 1/2 */
#define RFM_MOVE_SEQ_RATE_16 3                          /* .. 1/16, the default */
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
} rfm_move_arp_t;
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
#define RFM_MOVE_PROJECT_VERSION 6                      /* (versions 1..5 load too; 5 adds FX setup, 6 its 128
                                                         * values a slot) */
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
#define RFM_MOVE_PROJECT_EXT (RFM_MOVE_PROJECT_EXT_V4 + RFM_MOVE_FXSET_BYTES)   /* .. 128 values a slot (version 6) */
#define RFM_MOVE_PROJECT_FIXED (24 + RFM_MOVE_SEQ_TRACKS * 4 + RFM_MOVE_SEQ_CLIPS * 2 + 2 + 2 + RFM_MOVE_PROJECT_EXT + 4)
#define RFM_MOVE_PROJECT_MAX (RFM_MOVE_PROJECT_FIXED + RFM_MOVE_SEQ_NOTES * 7 + RFM_MOVE_SEQ_LOCKS * 4)
enum { RFM_MOVE_CLOCK_AUTO = 0, RFM_MOVE_CLOCK_INTERNAL = 1, RFM_MOVE_CLOCK_EXTERNAL = 2 };

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
    uint8_t sub, tps;                                   /* ticks into the step, ticks the step lasts */
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
    uint8_t layer[8], nlayer;                           /* what Back closes, the latest last (a Shift menu, the grid,
                                                         * Loop's latch, Shift's lock, the clipboard's marks) */
    uint8_t in_seq;                                     /* the settings hold the selected track's layout / CCs */
    uint8_t stash_layout, stash_cc[RFM_MOVE_NBANKS][RFM_MOVE_NKNOBS];   /* .. MIDI / Control mode's, meanwhile */
    uint8_t stash_root, stash_scale;
    uint32_t shift_ms;                                  /* .. its last press */
    /* the arp (the selected track's pads) */
    uint8_t arp_n, arp_down, arp_run, arp_track, arp_nseq, arp_last;
    uint8_t arp_pitch[8], arp_vel[8], arp_ord[8], arp_seq[32];
    uint16_t arp_pos, arp_oct, arp_steps, arp_mpos, arp_tpos;
    uint8_t arp_lastn, arp_lastp[8];
    uint8_t jr_n, jr_clip[8], jr_note[8];              /* the arp's notes just recorded (not played twice) */
    int32_t arp_next, arp_k, arp_t0;
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
} rfm_move_seq_t;

/* ---- the state */
typedef struct {
    uint8_t kind;                                       /* what a pressed control did, for its release */
    uint8_t a, b;                                       /* key / button / (channel, note) */
} rfm_move_hold_t;

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
    /* (0.5.3) latch, MIDI and Control modes (Track 2 held + the top row's pad 8): RAM only */
    uint8_t latch;                                      /* on */
    uint8_t nlat;                                       /* notes latched now */
    uint8_t lat_ch[RFM_MOVE_LATCH_MAX], lat_n[RFM_MOVE_LATCH_MAX];   /* .. each one's channel and note (one hold each) */
    /* (0.5.4) */
    uint8_t lat_slot[RFM_MOVE_LATCH_MAX];               /* .. the firmware slot it went to (bit 7: parked, its slot no
                                                         * longer the active one: it holds no note_count) */
    uint8_t emit_dest;                                  /* (emit_midi's MIDI dest while a latched note lets go) */
    char pop_text[32];                                  /* rfm_move_popup's words */
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
void rfm_move_set_mode(rfm_move_t *m, int mode);       /* as a Track button does (releases all on a change) */
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
void rfm_move_seq_set_clock(rfm_move_t *m, int mode);  /* RFM_MOVE_CLOCK_* */
/* how far ahead of what sounds the host advances the clock (the web player: its 30 ms lead): recordings and
 * Capture place what is played this much earlier, where it was heard */
void rfm_move_seq_set_lead_us(rfm_move_t *m, uint32_t us);
const char *rfm_move_seq_rate_name(int rate);           /* "1/16" .. */
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
