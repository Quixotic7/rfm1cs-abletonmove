# rfm1cs-abletonmove: the Ableton Move as a RemoteFM1 control surface

An Ableton Move running [Move Remote](https://github.com/Quixotic7/abletonmoveremote) drives a RemoteFM1 host's
emulated FM-1: its pads, buttons and knobs become the FM-1's keys, buttons, encoders and MASTER (or MIDI into the
firmware), and the FM-1's LEDs and screen come back to the Move. `rfm_move` is the one portable C99 core every host
compiles (no OS calls, no allocation), so the mapping and the sequencer are written once: the RemoteFM1 web player,
iOS and Android take this repository as a git submodule pinned to a tag. The
design is the owner's: `design/remotefm1move.movedesign.json` (ableton-move-designer v3; `pad-<row>-<col>`, row 1 at
the top). Licence: MPL-2.0 (`LICENSE`). The version is `RFM1CS_VERSION` / `rfm_move_version()`; `CHANGES.md` has the
log. This code lived in RemoteFM1 (`host/src/move/`) until 0.1.0; its history came with it.

| File | What |
|---|---|
| `include/rfm1cs/move.h` | the API (`include/rfm/move.h` includes it, for hosts written against RemoteFM1's path) |
| `src/move.c` | the mapping, the MIDI modes, the LEDs |
| `src/screen.c` | the screen: FM-1, MIDI and Control mode, the popups, the held Track buttons' menus |
| `src/seq.c` | the sequencer: its data, undo journal, clipboard, clock and engine |
| `src/seq_ui.c` | Sequencer mode: the Move's controls on the sequencer, its LEDs and screens |
| `src/project.c` | the project blob, the 32 project slots, the autosave |
| `src/v2.c`, `src/v2.h` | the v2 screens' drawing |
| `src/move_int.h` | what those files share (not an API; the tests read the data model through it) |
| `include/rfm1cs/session.h`, `src/session.c` | the link to the Move, transport-free (below, "The session") |
| `include/rfm1cs/store.h`, `src/store.c` | the 32 project slots as slot I/O: cut / copy / paste / colour |
| `include/rfm1cs/posix_tcp.h`, `src/posix_tcp.c` | optional non-blocking TCP for native hosts |
| `third_party/moveremote/` | libmoveremote's host side, vendored (upstream `bf01bb9`, MPL-2.0) |
| `web/build.sh`, `web/move_web.c` | the core in WebAssembly (`rfm_move.mjs`, one ES module) and its flat API |
| `web/move-core.js`, `web/move-seq.js` | the wasm wrapper and the sequencer's audio clock (SeqClock), for a web host |
| `tests/test_move.c` | ctest `move` |
| `tests/test_move_seq.c` | ctest `move_seq`: the sequencer |
| `tests/test_move_v2.c`, `move_v2_ref.*` | ctest `move_v2`: the v2 screens against the mockup's pixels |
| `tests/move_sessview.c` | ctest `move_sessview` (0.9.2, 0.10.0): session view reached as on the Move (a Track tapped, Menu): the scene column, Live's colours, the selected clip, empty slots and clips with no notes, Shift + a slot, the screen against the pads, the track popup, the select / stop steps, clips cut / copied / pasted as steps, its LED writes |
| `tests/move_e2e.c`, `move_e2e.sh` | ctest `move_e2e`: against Move Remote's simulator |
| `tests/test_session.c` | ctest `session`: the session and the store against a fake Move |
| `tests/session_e2e.c` | ctest `session_e2e`: the session over posix_tcp against the simulator |
| `tests/test_tcp.c` | ctest `tcp`: `host:port` parsing, a loopback connect (IPv4, IPv6) |
| `tests/rmvp/` | project files the web player, the iOS app and the Android app wrote (the `session` test reads them) |
| `tests/web_smoke.mjs` | the wasm build in Node (CI) |

## Building

```
cmake -S . -B build-asan -DRFM1CS_SANITIZE=ON && cmake --build build-asan -j && (cd build-asan && ctest)
web/build.sh [OUT_DIR]          # Emscripten: OUT_DIR/rfm_move.mjs (default build/web/)
```

A host adds the directory and links the core and the session: `add_subdirectory(vendor/rfm1cs-abletonmove)` then
`target_link_libraries(app PRIVATE rfm1cs_session)` (it brings `rfm1cs_move`; add `rfm1cs_tcp` for the socket helper) (the tests are built only when this is the top-level project, or
with `-DRFM1CS_TESTS=ON`). Hosts with their own build compile `src/*.c` and `third_party/moveremote/host/mr_gfx.c`,
`third_party/moveremote/common/font5x7.c` with `include/` and `third_party/moveremote/include/` on the include path
(and `src/session.c`, `src/store.c` with `third_party/moveremote/host/mr_client.c`, whose `mr_decode` the session uses).

## The session

`session.h` is the link every host used to write itself (the web player's `move-link.js`, iOS's and Android's
`MoveLink.cpp`), written once: HELLO and the Move's HELLO (its name, Wi-Fi / USB as `MR_VIA_*`), CONFIG audio clock
off, every LED written again on a new connection and then only the changed ones, the screen at most every 33 ms and
only when it changed, PING every second, 3 s of silence lost, BYE (exit / another host: off; power off: retried),
retries after 1, 2, 4, 4 .. s (`give_up` tries before the first connection: the web uses 2), the project flushed and
everything let go when the link goes, the battery from STATUS, a status struct with counters. The host moves the
bytes (`_rx`, `EV_SEND`), opens and closes the transport when told (`EV_OPEN`, `EV_CLOSE`, `_opened`, `_closed`),
calls `_tick` (or at `_next_ms`), gives the FM-1's LEDs / screen (`_panel`, `_frame`) and the audio clock
(`_advance` per render chunk, the firmware's MIDI out through `_midi_in`), and answers the project actions (itself,
or with `store.h`). One callback, no threads, no allocation; the web player runs it in WebAssembly (`web/move_web.c`
`mw_sess_*`, `web/move-core.js` `sess*`), native hosts as C.

Since 0.3.0:

- **Attempts.** Each `EV_OPEN` carries `attempt`, a new id per try; `_opened`, `_rx` and `_closed` take it, and a call
  with an older try's id is ignored, so a socket that closes late cannot end the next try.
- **The screen.** Call `_frame` at most every 33 ms (`RFM1CS_SCREEN_MS`). Called sooner with the same pixels it only
  asks for the redraw; the thumbnail is taken again when 33 ms have passed or the pixels changed.
- **Words.** `rfm1cs_status_text` (and `_why_text`, `_lost_text`, `_bye_text`) give the web player's status line and
  toasts for any host (`rfm1cs_words_t`: the address shown, the transport's own words for a close, its name, "page" or
  "app"), with a tone (off / busy / on / bad).
- **The project file.** `store.h`'s `rfm1cs_file_pack` / `_unpack` / `_check` / `_error_text`: the `.rfmmove` /
  `.rfmmoveset` container ("RMVP") the web player, iOS and Android each wrote themselves, now once.
- **Addresses.** `rfm1cs_tcp_connect` takes `host:port`, `[v6]:port` or a bare IPv6 address (`rfm1cs_tcp_parse`;
  default port 7720).

### Threads

The session is not thread-safe and assumes no thread: every call from one thread at a time. Two ways work. Queue the
transport's bytes to the thread that owns the session (the web player: one thread). Or call the session from the
network thread and the engine's thread under one mutex (iOS, Android): then `_rx` runs on the network thread and the
actions it causes (KEY, PRESS, BTN, TURN, MIDI, PROJECT_* ..) come out there, inside the callback, so the host copies
each into a queue the engine drains, and never calls into the engine from the callback. A thread that must not block
(an audio or engine thread) takes the mutex with `try_lock` and skips the turn when it is busy (the next tick catches
up: `_advance` takes the frames since its last call). When the engine's thread stops (no firmware running), the
network thread drives the session alone under the same mutex: `_tick` at `_next_ms`, `_rx`, `_closed`.

## The Track buttons (0.8.0, proposal 2; 0.9.0, proposal 3: one main layout)

The owner's boards (`remoteFM14Move/docs/design/track-buttons-v2` and `-v3.movedesign.json`): **Track 1, 2, 3 are
the three firmware slots, always; Track 4 is the sequencer side.** Each slot has its own layout: the FM-1 layout, the
firmware's takeover, or a note layout (Scale, Chroma, Drum, Octave). **A note layout is the sequencer** (0.9.0): the
track view of the slot's selected sequencer track (`settings.lock[slot]`, track n + 1 at first) with the sequencer's
whole feature set; there is no separate Sequencer mode. A tap acts at its release (`RFM_MOVE_TAP_MS`, 300 ms,
nothing else done); a press within `RFM_MOVE_DOUBLE_MS` (300 ms) of that release is a double click (the tap has
already acted, nothing waits); held, the button's menu shows after 300 ms (or at once when used) and its release does
nothing.

| Track | Tap | Held | Double click |
|---|---|---|---|
| 1..3 | play that slot in its layout (`FW_ACTIVE`; an empty slot too); a note layout: its track view (0.14.0: the track's knob page as it was left; TRIG for a track never shown); (0.12.x, the owner's) there the steps show the slot's 16 tracks for 600 ms (routed: the slot's colour, not routed: dark, the selected one blinking white / the slot's colour, 100 ms each), then the clip's steps again; a step pressed meanwhile is a clip step and ends it (not in the FM-1 layout, a takeover or session view; not under the held Track's menu or the browser) | one menu (below) | the firmware browser on that slot (there a pad's double tap loads it) |
| 4 | the Mixer or Routing, the one last shown (the Mixer at first); a tap there flips them. (stage 3a, a host with the kit) the round is Mixer, Routing, Kit: from elsewhere the view it was left on | pad 1 the Mixer, pad 2 Routing; (stage 3a, the kit) pad 3 the Kit, pad 4 the Looper (not built: dark, dead), pad 5 the projects; the steps the Shift shortcuts, Record / Play / Capture / Undo / Loop / Sample their Shift + button, in every layout | the sequencer's projects |

**A note layout** (the sequencer): the pads play the layout into the selected track (live to the slot, recorded with
Record), the steps are its clip's steps, the knobs the TRIG page then the CC banks (the jog), the buttons the
sequencer's (Play, Record, Capture, Loop, Copy, Delete, Undo, Mute, Shift + step), Menu the track view <-> session
view. Entering it selects the slot's track, routes it to the slot and gives it the slot's layout; the track's channel,
key and CC numbers are the ones played. Play starts the sequencer and sends MIDI Start, clock (24 PPQN) and Stop to
every loaded firmware (`RFM_MOVE_DEST_ALL`), so their own sequencers and arps follow.

**Track n held: one menu** (every layout): the top row's pads 1..6 FM-1 (white), the firmware's own takeover (purple;
dark when it has none), Scale, Chroma, Drum, Octave (lime), the slot's own full, the others at 30 %; pad 8 latch
(amber); (0.13.0, a host with `RFM_MOVE_CAP_PROJFW`) pad 7 **the firmware's States** (in the slot's colour while a
firmware is loaded there; dark and dead with none, or with an older host; see
[States](#projects-that-hold-their-firmwares-states-0130); in session view the pads stay the routing); rows 2..4 the 24
scales (the key the tracks use: the project's, or the selected track's own). The steps are the 16 tracks for firmware
n: **a click selects** one (it is routed to firmware n; in a note layout it is the track
edited), **a double click solos** it on firmware n only (one solo a firmware: another moves it; again: off, the
routing as it was; (0.11.0) the solo stays when another track is selected), **a hold** routes it to firmware n or not;
Copy, Delete and Mute + a step copy, clear, mute a track. (0.11.0, `design/UX-RULES.md`) the three are gated: the hold
acts once held 300 ms, while still down; the double click acts at its second press. (0.12.x, the owner's) the click
acts at the press: the track selected (routed to firmware n if it was not) at once, the double click and the hold
start from it (the double click's track stays selected; the hold un-routes a track that was routed before the press,
which stays selected, and leaves a not-routed one routed); the window and the release show nothing more. The
steps' lights: a track not routed is **dark**, a routed one the slot's colour, the selected one **blinks**, the
solo purple (0.12.0), a pressed one white. The jog the root, Up / Down the octave, the knobs (in a note layout) the track's CC
numbers, the volume the slot's level. The screen (0.11.0): the slot at a glance: the firmware, its layout (a padlock:
latch on), the key, and the 16 tracks (a block: routed, a dot: not, underlined: the selected one, a bar over it: the
solo); a step acted: "Felucca / Track 3 / Not Routed" over the same 16 tracks. **In session view** the held Track is
the routing only: a column's pad: a click routes its track to the firmware or not, a double click solos it; the
knobs, the jog and Up / Down do nothing there.

**Shift + Track 1..3** (0.14.0, the owner's: "Using SHIFT + a track button should take you to Track settings similar
to Ableton's Move software shortcuts"): at the press, that firmware played (as its tap; session view stays) with its
selected track, and the track's settings open over it: Shift + step 3's menu, titled with the track ("Track 5"; said
"Track 5 settings"). The Track's release, its hold (no Track menu) and Shift's release (no lock) then do nothing; Back
closes the menu (the view and the layout stay). Where the Move's Shift is ours: a note layout, session view, the
Mixer and Routing; in the FM-1 layout and a takeover Shift is the firmware's GLO and the Track does its own jobs.
**Shift + Track 4** is unchanged (its tap: the Mixer / Routing; its hold: the Shift layer): Track 4 is no firmware's,
so it has no track settings to open.

**Shift shortcuts** (0.9.0): Track 4 held + a step anywhere (its Shift menu on the screen at once), Shift + a step in
a note layout and in the Mixer and Routing (a Shift menu opens over them; Back closes it). **Track 1..3 held** works
over the projects view too.

**Back** closes layers only (a Shift menu, Loop's latch, the clipboard's marks, the projects view's levels); with
nothing open it does nothing (0.9.0; in 0.8.0 it flipped to a Sequencer mode). (0.12.0, the owner's "Back never changes
the mode or the view") it never leaves session view: Menu does. (0.14.0, the owner's own exception, 2026-11-24) On
the FM-1 knob page with nothing else to close, Back ends its lock (Knobs+Keys, the jog on a knob): "Knobs".

**Session view from anywhere** (0.12.0, the owner's): **Shift + Menu** in a note layout, **Back held + Menu** in every
layout, and **Track 1..3 held + Menu** (in the FM-1 layout and a takeover the Move's Shift is the firmware's: a held
Track is the Move-side Shift) open session view, from the FM-1 layout and a takeover too (the sequencer runs it; the
slot keeps its layout). The Track's hold is used up by the Menu press: its menu, tap and double click do not come;
Back's click does not come either (a takeover that has Back: neither Back nor Menu is sent to it). In session view
Menu, alone or with any of them, returns to the active slot's own layout: a note layout's track view, or the FM-1
layout (its takeover). Both ways are named (`POP_VIEW` "Session" / "Sequencer"; the FM-1 layout: its layout popup).

**The LEDs:** Track 1..3 (0.12.0, q1, the owner's) the slot's own colour: Track 1 orange, Track 2 blue, Track 3
yellow, on everything that means that slot (its Track button, the routing lights under a held Track, session view's
knobs and columns, the Mixer's meters and Routing's slot pads; the browser keeps each firmware's own colour; a solo
is purple). An empty slot faint white, full for the one played (on the
sequencer side, dim; a held one full); Track 4 red, full on the sequencer side (the Sequencer, the Mixer, Routing, the
projects). A host with no firmware list (or `RFM_MOVE_CAP_SLOTS` off) has one slot: Tracks 2 and 3 are dark and do
nothing; with no FX (`RFM_MOVE_CAP_FX` off) Track 4's tap opens the projects.

**The layouts.** The FM-1 layout (section 1) reaches the FM-1's keys, buttons and knobs. Every other layout is a note
layout (section 2): the pads play notes on the slot's channel, the knobs send the slot's Control CCs, the buttons are
RemoteFM1's own (Up / Down the octave, Play the sequencer's transport, Shift + a step the Shift shortcuts, Shift + a
knob its CC number); no FM-1 button. The takeover (README "Takeover") is pad 2's layout. Changing to or from the
Sequencer lets go of everything held; switching slots lets go of what is held on the old slot (latched notes stay
there).

### 1. FM-1 mode

In FM-1 mode every popup (a knob, MASTER, a message) keeps to the left half, over the drawing: the FM-1's screen on the right stays in view.
**Track 4 held: the Move's Shift layer** (0.8.0; was Track 1 held), in every layout and the Sequencer (in the FM-1
layout Shift itself stays the FM-1's GLO; in a note layout Shift + a step is the same). With Track 4 held the steps are the
Move's Shift + step shortcuts on the sequencer (projects, setup, Workflow, Tempo with its taps, Metronome, Groove,
Layout, Keys & Scales, the arp, Full Velocity, New Clip, Double Loop, Quantize; the ones with a job lit), and Record,
Play, Capture, Undo, Loop and Sample their Shift + button (Shift + Record: the record settings). A menu opened this way
stays open after Track 4 is let go (full width; the jog, the knobs and, where it uses them, the pads and steps are
the menu's) until Back, which closes it. While Track 4 is held its menu is on the screen (pad 1 the Mixer, pad 2
Routing) and the icons under the steps light. (0.12.0, a8) **Track 4 held is a whole layer**: nothing of the hold
shows before its 300 ms but the button's own light; then the screen, the icons, the step colours and pads 1 and 2 all
at once (or at once when anything is pressed meanwhile: that press is the hold). Up, Down, Left, Right, Menu, Copy,
Delete, Mute, Shift, the other pads, the knobs and the jog have no job in it: dark and dead (their releases pass).
A menu a Track 4 shortcut opened owns the knobs and the jog, lit and named, while Track 4 is still down.
**Popup width (0.5.9, the owner's).** FM-1 mode's knob, label and readout popups (and MIDI's NOTES view's) cover
the whole screen by default, with bigger words: the name on a 2x row, the value at 4x or 3x, a 3-pixel bar. Global
Settings' Popup size row (`Popup Full` / `Popup Half`, the setting `pop_half`; 0.12.0: its knob 4 was a leftover, gone) gives today's left half back, the FM-1's
screen staying on the right. Where the text below says the left half, it is the popup's place, either width.

A knob touched shows its name (the firmware's label, else its printed one) in the left half, with no value yet
(0.5.6, the owner's; the firmware's values come later); turning shows this gesture's detents (from 0 each gesture);
the release drops the popup and its count. MASTER the same, with its level. A label "" (no job): no popup.

**Labels (0.5.6).** The host passes the firmware's 49 live labels (`rfm_move_set_labels`) and its 14 button names
(`rfm_move_set_button_names`, used when a button's live label is NULL). With the jog wheel touched, a mapped button
shows its label at once on press (0.5.8, the owner's); otherwise a button held 300 ms (`RFM_MOVE_LABEL_HOLD_MS`)
shows it in the left half (a tap shows nothing). The label is the one the firmware has while the button is held,
live (a firmware that names a button only once it is pressed shows the name then); words never show only after the
release. While any FM-1 button is held,
every other button, pad or step (its key) and knob pressed shows its label at once, as the firmware has it when
drawn (Shift-style layers). Pads and steps show nothing otherwise: they stay an instrument. A label shows until a
second after its control's release. Never 1x text: a button's or key's label one row at 4x or 3x when it fits, two
rows at 3x, else up to three 2x rows wrapped at spaces, a row that does not fit scrolling.

**Readouts (0.5.7).** The host passes the firmware's readouts (`rfm_move_readout`: what a control does now and its
value, in the firmware's words, from its rd1 record). A knob keeps its last one: touched, the knob shows what it
controls and the last value; turned, the readout replaces the detents and updates live. Without any readout the knob
shows its name and detents as before. Every knob the same, PRESETS too (0.5.8): what it controls (the readout's
title, else the knob's label), then the value, whatever the readout's kind. A readout for no control in use (a preset's name, a message) is a popup of
its own, for the time the firmware gives (else a second). Left half: the title on the first row, the value at 4x or
3x when it fits (else up to two 2x rows), a 2-pixel bar at the bottom when it has one (bipolar: from the middle). The jog wheel (SELECT) has the same popup
while it turns (the firmware's SELECT label, its detents), gone and counted from 0 again a second after it stops.

**For firmware authors (0.12.0, a16).** In the FM-1 layout a knob's turn shows the firmware's own value only when the
firmware sends readouts (rd1, `rfm_move_readout`): a firmware that sends them gets its real values on the Move; one
that does not shows the detents turned ("K1 +3"). A label "" (`rfm_move_set_labels`) says the knob has no job: its
light is dark and it shows nothing (0.12.0; without labels every mapped knob stays lit).

**Over a takeover's screen (0.12.0, a15, the owner's).** A knob the takeover leaves to the FM-1 (not claimed) shows
the same popup over the takeover's screen while touched and its second after a turn, in the popups' place (Global
settings' Popup size: the whole screen, or the left half with the firmware's screen on the right).

| Move | FM-1 |
|---|---|
| pads, row 2 (second from the top) | white keys F G A B C D E F: keys 0 2 4 6 7 9 11 12 (F3..F4) |
| pads, row 1 (top), columns 2 3 4 6 7 | F# G# A# C# D#: keys 1 3 5 8 10 (columns 1, 5, 8 off) |
| pads, row 4 (bottom) | white keys G A B C D E F G: keys 14 16 18 19 21 23 24 26 (G4..G5) |
| pads, row 3, columns 1 2 3 5 6 8 | F# G# A# C# D# F#: keys 13 15 17 20 22 25 (columns 4, 7 off) |
| steps 1..16 | the 16 white keys in order (key 0 F3 .. key 26 G5), velocity 100 |
| pad velocity / poly aftertouch | the key's velocity / pressure (0..127; `rfm_move_lean` gives key_lean 0..100) |
| Capture Sample Mute Delete Copy Shift Menu Undo Loop Play Rec Left Right | FX SEL ENV LFO EDIT GLO HOME SAVE ARP PLAY REC OCT- OCT+ |
| knob 1, 2, 3, 5, 6, 7, 8 (4 unused) | (0.8.0) SELECT, PRESETS, ALGO, K1, K2, K3, K4: an FM-1 detent every 2 of the Move's ticks (`RFM_MOVE_KNOB_TICKS`, the owner's: half as fast; a turn back or the knob let go forgets a half) |
| jog wheel turn, click | (0.8.0) turns SELECT, PRESETS or ALGO, a detent a tick; the click cycles them, SELECT -> PRESET -> ALGO (a popup "Jog / PRESET", three dashes, a second) |
| volume knob | MASTER, 16 a detent, 0..1023 |
| Up | SEQ (0.4.4; FM-1 mode only: the MIDI modes' octave there), its LED SEQ's level |
| Down | nothing in FM-1 mode (0.15.0, the owner's: the Track buttons switch firmwares; 0.4.4 .. 0.14.0 it was the next loaded firmware): see [Up and Down in FM-1 mode](#up-and-down-in-fm-1-mode-044) |

Pads glow white (white keys) or pink `#ff7aa8` (black keys) at the FM-1's own LED level for that key (panel.js's
`ledAlpha` curve, as the OMX-27's `leds(keyLevel)`), never below a faint floor so the keyboard stays visible; a
pressed pad shows the mode colour. Steps mirror their white keys the same way. The button LEDs mirror the FM-1's
button LEDs (Play: white at PLAY's level plus green at PLAY's green level; Rec red; the rest white). Knob rings: the
mapped knobs in the mode colour, bright while touched.

### 2. MIDI mode

Pads play notes into the firmware's MIDI in (`RFM_MOVE_ACT_MIDI`: note on with the pad's velocity, poly
aftertouch, note off) on the slot's channel (default 1). (0.8.0) These are the note layouts of a slot (Scale, Chroma, Drum, Octave), picked
in its layout menu (Track n held: the top row's pads 3..6, steps 1..16 the channel). Up / Down: the octave, -4..+4.

**Latch** (0.5.3, the owner's; the note layouts, shared, RAM only): Track n held + the top row's pad 8 turns it
on or off ("Latch on" / "Latch off" for a second, full screen; the pad amber, bright while on). While on, a released
pad's note keeps sounding. Pressing a pad with no other pad down starts a new chord: the latched notes go and the new
ones latch when released; pads pressed together latch together. Pressing a latched note's pad again lets that note go
(nothing plays). A framed "L" in the screen's top-right corner says it is on. Turning it off, a mode change, a project
load and `rfm_move_release_all` (the link lost) let every latched note go; flipping to the projects view does not.
FM-1 mode and Sequencer mode do not latch. `rfm_move_set_latch(&m, on)`, `rfm_move_latch(&m)` for a host.
(0.5.4) A latched note stays on the firmware slot it was played on: switching the active slot (Down, a Track tap,
the projects view) leaves it sounding there, and the new slot plays its own. A new chord and a re-press act on the
active slot's latched notes only; turning latch off, a mode change, a project load and `rfm_move_release_all` let
every slot's go, each note off to its own slot (`RFM_MOVE_ACT_MIDI` with `dest` = slot + 1).

| Layout | Notes (base = 48 + root + 12 × octave: C3 at the defaults) |
|---|---|
| 1 Isomorphic in scale (default; C major) | bottom-left = root; +1 scale degree a column, +3 degrees (a 4th) a row |
| 2 Isomorphic chromatic | +1 semitone a column, +5 a row |
| 3 Drum | bottom-right = C2 (36), ascending leftwards, then the next row up from the right (36..67); hold a pad and turn the jog wheel to change its note (saved) |
| 4 Octave | each row one octave: 7 degrees and the octave above, rows an octave apart |

Pads: the root in the mode colour, in-scale notes white-ish, the rest dim, pressed ones white, notes past 0..127 off.
The scale and root are settings (`rfm_move_set_scale`: major, minor, dorian, mixolydian, the two pentatonics,
harmonic minor, blues, and since the owner asked for 24: phrygian, lydian, locrian, melodic minor, whole tone,
diminished half-whole and whole-half, Hungarian minor, phrygian dominant, in sen, hirajoshi, iwato, pelog, double
harmonic, Spanish 8-tone, bebop dominant; the first 8 keep their indexes, so older settings and projects mean the
same). **Hold Track 2**: rows 2, 3 and 4 of the pads (24) pick the scale in that order (lit cyan, the current one
full: a colour of their own), the top row the layout, the jog wheel or knob 1 the root (C..B, wrapping), knob 2 the
scale (both list knobs: one a few ticks); the screen shows them (the 24 as 3 rows of tabs, the current one a block),
and while a scale pad is pressed or knob 2 turns that scale's short name alone at 2x, its tab tall. (0.8.0) In a note
layout Shift is RemoteFM1's own: Shift + a step the Shift shortcuts, Shift + a knob its CC number. Each slot keeps its
own settings; the sequencer's GLOBAL key is its project's.

**MIDI mode's screens** (kept in the settings, `midi_view`; 0.8.0: no gesture turns them any more, Track 2 is a
slot): **PADS**, the
pad map (below), full-width popups; **NOTES**, the FM-1's screen on the right half and on the left the notes held
(their names, 2x when they fit) and the chord they make (Cmaj7, Am, Dsus4, G7, m7b5 .., an inversion as a slash chord,
C/E; black at rest). In NOTES the popups keep to the left half and the knobs follow FM-1 mode's rule (a popup while
turning only).

### 3. Control mode (gone in 0.8.0)

Every note layout has the Control CCs on its knobs now: each turn moves a value the core keeps per slot (0..127, 64 at
first, not saved) and sends the slot's CC on its channel; the rings show the value (cyan). The slot's 8 CC numbers
(16..23 at first) are set with Track n held + a knob, or Shift + a knob in the layout ("Knob 1 / CC 74 / Felucca").
No banks. The volume knob is still MASTER.

### 4. Sequencer

The owner's aim: a sequencer that feels like the Move's own, with 16 tracks instead of 4, and conditions, functions
and the rest of the TRIG page on top. It takes over the whole surface except the Track buttons (the four modes, as
everywhere) and the volume knob (MASTER). 16 tracks, each with its own MIDI channel (track n: channel n), rate,
groove (or the project's), key and scale (or the project's), arp, pad layout and CC numbers; 16 scenes; a clip per
track and scene; projects. It keeps playing in every mode (in FM-1 mode a small play triangle over the drawing says
so). Back closes layers, and with none flips back to
the slot's layout (0.8.0, proposal 2).

**Tracks: a firmware Track held** (0.8.0; was Track 4 held): the lock menu (see "The Track buttons"). The 16 steps are
the 16 tracks: **a click selects** one for that firmware (0.12.x: at the press), **a double click solos** it (on /
off), **a hold** (300 ms) routes it to that firmware or not (see "The Track buttons"). Solo: while any track is soloed only the soloed tracks sound.
While it is held the knobs set the bank's CC numbers ("K3 CC74"), Up / Down the rate, the jog the MIDI channel; Copy /
Delete + a step copy / clear a track; Mute + step mutes. The pads do nothing while it is held. The track view shows which of the 16 (a 4 x
4 in the top-right corner: tracks 1-4 the first column, the selected one filled). Session view's Left / Right move
its 8 columns through the 16 tracks.

**Two views** (Menu toggles; Track view first):

| Control | Track view | Session view |
|---|---|---|
| pads | the track's layout, the scale and octave (shared with MIDI mode), on the track's channel; what they play is what an empty step gets | clips (the owner's mockup): a column a track (8 in view), a row a clip slot (4 in view, the top row first) |
| steps | the 16 steps of the page | the 2 under each column: the first stops that track's clips (at the bar; lit while it plays something, blinking while it stops), the second mutes it (the track's colour; muted: red, dim) |
| Left / Right | the bar shown; Right past the last bar: a ghost bar (below); a step held: micro timing (the MICRO popup) | the tracks, one at a time (1..16); (0.12.0) Shift: 7 at a time |
| Up / Down | octave (MIDI mode's); (0.12.0) at the press "Octave +2" a second, the pads' lowest and highest note under it ("C4 - E6"; at the range's end the same, unchanged), and a white row sweeps the pads the way it went (bottom up for Up, top down for Down; a row each 50 ms, 200 ms in all; pads play meanwhile). A step held: transpose, its notes by name a second ("E3 G3 B3", the TRANSPOSE popup) | the clip slots, one at a time; (0.12.0) Shift: 4 at a time |
| knobs | the knob page: TRIG (below), (0.12.x) FM-1 (below), (0.14.0) ENGINE when the track's firmware offers it ("Engine parameters", below), then Control mode's CC banks 1..4, live on the track's channel; (0.14.0, the owner's) each track keeps the page it was left on, brought back whenever it is selected again (a Track tap, a held Track's step, session view's selection; RAM only, TRIG for a track never shown; one left on ENGINE whose firmware no longer offers it: FM-1); a step held: its CC locks (the TRIG, FM-1 and ENGINE pages: its trig values) | nothing; a touch selects the track in that knob's column; (0.12.0) a select step held: knob 1 the clip's Launch, knob 2 its After |
| volume knob | MASTER; a step held: its notes' velocity (a VELO popup) | MASTER; (0.12.0) a select step held: the level of the firmware its track plays |
| jog | the knob page (TRIG, FM-1, (0.14.0) ENGINE, banks 1..4); the ENGINE page's list open: its highlight; a step held: its notes' length (the LENGTH popup); Loop held or latched: the loop's length; a Shift menu: (0.13.0) selects a parameter, or changes the one being edited (the arp: its pages); a drum pad held: its note | the knob page |
| jog pressed | nothing (FILL is Shift + Loop held); (0.14.0) the ENGINE page: its page list, at the release (open / closed on the page highlighted); (0.12.x) the FM-1 page: its level, Knobs <-> Knobs+Keys (0.14.0: taken over the jog turns the FM-1 layout's jog target; Shift + click changes what the jog turns: the next target, or in Knobs the pages -> SELECT -> PRESET -> ALGO -> the pages); (0.13.0) a Shift menu: edit the selected parameter / select again (the arp: off / on) | the same |

**The FM-1 page** (0.12.x, the owner's 2026-11-22: "a page to control the firmware's knobs like you get on the FM-1
Layout .. Then when clicking the jogwheel it should switch the takeover between just knobs and then full shortcuts:
Buttons, steps, ect, but pads stay in the MIDI layout, and Jogwheel is not taken over"). Second of the knob pages
(TRIG, FM-1, banks 1..4; the strip has six cells; RAM only, the stored bank 0..3 is still banks 1..4); its popup
"Knobs" / "FM-1". The page gives the slot's firmware what the FM-1 layout gives it, by the same paths:

- **Level 1** (on arriving): the knobs are the FM-1 layout's (1 SELECT, 2 PRESETS, 3 ALGO, 4 none, 5..8 K1..K4; a
  detent every 2 ticks), lit purple where they have a job (a label "": dark, dead), a touch its name, a turn its
  readout or detents, full screen (Popup Half: the left half, the FM-1's screen on the right), the release drops it.
  Everything else is the note layout's (pads, steps, buttons, the track view). A step held: the knobs are its TRIG
  values, as on the TRIG page.
- **The jog on a knob** (0.14.0, the owner's 2026-11-23: "click the Jogwheel to make it control one of the knobs like it
  does in the FM-1 layout. Click to exit. Shift + Click to change the target knob, should be linked with the Fm-1
  layout"): the jog's **click** (once the double click's 300 ms have passed, R2) makes its turn the FM-1 layout's jog:
  the slot's target, SELECT, PRESET or ALGO (one setting a slot, shared with the FM-1 layout, whose click still
  changes it there); "Jog / Select" the whole screen a second, then "Jog Select" at 2x on top of the track view for as
  long as it lasts, and the target's knob (1, 2 or 3) white instead of purple (white from the press on). The click
  again gives the jog back its pages ("Jog / Pages"). **Shift + click**: the next target, at the press (and the jog on
  it). Whatever takes the jog over the page still does (a held step, Loop, Shift + turn's tempo, a menu, a held
  Track); another slot starts with the jog on its pages. **The double click** (at its second press) is level 2 / level
  1, which was the click before 0.14.0.
- **Level 2** (the jog's double click, at its second press (0.14.0; was its click); "FM-1 / Knobs+Keys" a second, the
  double click again "Knobs"): the steps are
  the FM-1's keys (its LEDs, icons dark) and the buttons the FM-1 layout maps are the FM-1's (Capture FX, Sample SEL,
  Mute ENV, Delete LFO, Copy EDIT, Shift GLO, Menu HOME, Undo SAVE, Loop ARP, Play PLAY, Rec REC, Left OCT-, Right
  OCT+; their lights the firmware's), the screen the FM-1's (as the FM-1 layout). (0.14.0, the owner's "Clearing the clip") **Delete and Copy are gated
  there**: at the press they are the sequencer's modifiers (their light full): Delete + Play clears the clip (one bar
  again), Delete + a pad its notes, Delete / Copy + a step the step's (the step is not the FM-1's key then); a click
  (let go within 300 ms, nothing else pressed) is also the firmware's LFO / EDIT, as a tap at the release; held
  longer or used with something, the firmware hears nothing of it. Shift (GLO), Mute (ENV), Loop (ARP) and the other
  mapped buttons stay the firmware's there, held or not (their sequencer gestures pair with the steps, which are the
  FM-1's keys in this level); Play is the firmware's PLAY only (the Move's transport: Play in level 1). Ours: the pads (the track's notes),
  Up / Down (their octave), the jog (its turn the page or, 0.14.0, the target knob; its double click back to level 1, its click the jog on a knob), Back (as in the FM-1 layout: no
  held-Back shortcuts), the Track buttons, the volume knob. The sequencer's transport: Play in level 1 (or another
  page). Another page lets go of what level 2 held and gives the steps and buttons back; the FM-1 page again: the level
  it was left in (0.14.0: a Track tap brings the track's own page back, the FM-1 page too). (0.14.0, the owner's
  2026-11-24: "Back should exit out of the FM-1 knob lock. If I go to another track or mode and come back, the lock
  state should exit") **Back's click** ends level 2 and the jog on a knob ("Knobs"; Back is lit while it can; a menu
  open closes first; Back held + Delete ends it too), and **another track, layout, mode, slot, session view or the
  projects view** ends it as well: coming back shows the FM-1 page in Knobs with the jog on its pages (the track's
  page itself is remembered; another page and back keeps the lock, as before). An empty slot: the page is there ("Empty slot"), its knobs dark and
  dead, its click nothing. `rfm_move_fwp_level()` says which level shows (the host needs no change: labels, readouts
  and frames reach the core as ever).

**Steps (track view)**, as on the Move, with the owner's changes (2026-11-17):

- **Pads, then a step.** Hold a note or a chord on the pads and press a step: the step gets exactly the held
  notes, with their velocities (what was on it goes).
- **A tap** (a press shorter than `RFM_MOVE_SEQ_HOLD_MS`, 300 ms): on an empty step it puts the *last notes* (the
  last step entered or edited, the last pad chord, or the root) at the release; on a step with notes it clears them.
  A tap shows no popup.
- **A hold** never clears a step. Holding a step with notes plays it once (a preview, on the track's channel) when the transport is stopped (playing: nothing extra, the owner's); after
  300 ms its popup comes up. Holding an empty step leaves it empty: the pads pressed while it is held go into it.
  While a step is held: pads add or remove notes (up to 8); the volume knob sets the velocity; the jog wheel the
  length (2 ticks a detent; Shift: whole steps); Left / Right nudge (Shift: 1 tick); Up / Down transpose (Shift: an
  octave); the knobs set CC locks or (TRIG page) its trig parameters; a later step pressed stretches the notes to it
  (0.12.0, the owner's: a toggle. Span = the steps from the held one to the pressed one, both counted: when the held
  step already lasts more than span - 1 steps and at most span, it goes back to 1 step; otherwise it lasts the span.
  So step 1 held + step 2 again and again: 2, 1, 2 ..; the LENGTH popup shows it at the press).
- **Any page can be edited**, in the loop or not (outside it: dimmer LEDs, the screen's steps row a line).
- **Only Play starts or stops the transport** (the owner's; the Move starts it with the first note into an empty clip,
  this does not). Steps, held steps (their notes heard), pads, double taps, menus, Capture, Sample, Rec and project
  loads leave it as it was. The one exception: Rec armed in CAPTURE mode, whose first note starts playback and the
  recording (the owner's earlier request). A project loaded while playing keeps playing (from its first bar).
- **Step LEDs** (Move's): notes white, an empty step the track's colour dimly (all the same: the Move's LED shapes
  mark the beats), the playhead green, a held step the track's colour, steps outside the loop dim; a copied step
  blinking white, a cut one red; Step record's cursor red.

**Pages and the loop.** A clip's loop is a start and a length (`lstart`, `len`: any pages of the 16).

- **Loop held, or latched: page edit** (0.12.0, a2, the owner's: a quick click on Loop (let go within 300 ms,
  nothing else pressed) latches it, its LED glowing, until a click on Loop or Back; held, the same from the press,
  ending at the release: a hold let go unused does not latch): the
  steps are the 16 pages: the loop's in the track's colour, the one shown white, the playing one pulsing, the clip's
  others (outside the loop) dim, past the clip off. A step: show that page. Two steps held: the loop is those pages and
  the ones between (the page shown moves into it). A double tap (400 ms): the loop is that page alone, any page (0.13.0,
  the owner's: "In the loop page double clicking a step should always solo that bar, not extend the page range to that
  bar, you do that by holding one step button then pressing a second": also a page past the clip's end and a ghost bar;
  one Undo: the loop before the first tap). A tap only shows the page, past the clip's end too. The jog:
  the length, a step a detent (Shift: a page), growing into new pages up to 16 (the owner's; the Move's wheel moves
  bars). The popup: the length in bars (+ steps), a bar over the 16 pages. (0.13.0, the owner's) A length that ends
  inside a bar ("5 Steps", "1 Bar +8"): the popup's bottom row is that bar's 16 steps (a cell: in the loop, filled
  with a note; a dot: after its end), and for the popup's second the step lights are that bar's steps too (in the
  loop the track's colour, a note white; after the end dark, a note there faint). **The pads** (0.13.0, the
  owner's): the two lower rows set the loop's last bar's length, 1..16 steps (the upper of the two 1..8, the bottom
  row 9..16: two bars and pad 8: 16 + 8 steps), lit up to the length (the length itself white); the two upper rows
  are dark and dead; no pad plays while Loop is held or latched.
- **A page's step held + the jog** (0.13.0, the owner's: "while holding on to a page/bar you should be able to turn
  the jogwheel and change all the note lengths in that bar like you can with the default Move sequencer"): every note
  starting on that page longer or shorter as a held step's jog does it (a detent ~10 % of a step, Shift whole steps;
  the same clamps), one undo step a hold; the loop's length stays. The popup: "Bar 2" with "Length", the change so far
  ("+1/4", "-1"), the page's 16 steps under it ("Bar 2 length: plus 1/4 step"). A page with no notes: the popup with
  "--", nothing changes ("Bar 4: no notes"). Two page steps held: the jog does nothing. A page step used with the jog
  is no double tap's first tap.
- **A step entered after the loop's end in the loop's own last bar** leaves the loop's length alone (0.13.0, the
  owner's; it used to stretch the loop to the bar's end): the note is outside the loop, its step dim. A step on a
  later bar still grows the loop to that bar's end.
- **Left / Right** (the owner's, in and out of page edit): the bar shown, through the bars the clip has (no wrap;
  Left stops at bar 1). A step held: micro timing, as before.
- **Ghost bars** (Move's "+" bar): Right past the clip's last bar goes to bars it does not have yet ("Bar n" for a
  second on each press; the strip the real bars as a line, blank for the ghosts, a + at the far end). Outside page
  edit a ghost bar becomes real once a step goes in there (the clip grows to it); in page edit the step keys are
  the pages (a ghost one drawn as a +), and pressing a ghost page's key confirms the new bars (the clip to that
  page; 0.13.0: only for a ghost bar reached with Right, not one a tap showed). Leaving them adds nothing.
- **Pages cut, copied, pasted** (Loop held or latched): Delete + a page with notes cuts it (gone, an X on its step and
  in the popup's page strip), Copy + one copies it (dashed); Delete or Copy + an empty page pastes there (a pasted cut
  empties the clipboard). A page clipboard of its own, apart from the steps'. Back clears its mark.
- **Shift + step 15**: double the loop (its content copied into the new pages). **Shift + Loop held: FILL** (FILL
  conditions pass while it is held; the jog wheel held does nothing). (0.12.0, a10) While it is on the Loop light
  blinks fast, the screen says "Fill" (said once), and a diagonal of pads in the track's colour travels across the
  pads (a diagonal every 100 ms, some 60 LED writes a second); the pads still play; all back at once when it ends.

**The TRIG page** (the jog left of bank 1; its popup at 2x with a picture). Per held step, or with no step held
every step with notes in the clip:

| Knob | Trig parameter (Elektron's) |
|---|---|
| 1 MICRO | the notes' micro timing, a tick (1/24 step) a detent, -12..+11 |
| 2 RATCH | ratchets: 1..8 hits spread over the step (the notes shortened to fit) |
| 3 CHANCE | 1..100 % (100: none) |
| 4 COND | none, A:B (1:2 .. 8:8: plays on pass A of every B), FILL, !FILL, PRE, !PRE (the track's last condition), NEI, !NEI (the track before's), 1ST, !1ST |
| 5 FUNC | FIZZ's step functions: RESTART, REVERSE, FORWARD, FLIP, RANDOM (jump), RND FN, LEAP n, JUMP n (run when the step plays); REPEAT n (the owner's): the step plays again on its next n steps at the track's rate, as if copied there (its notes, lengths, locks, ratchets); the real steps under them are skipped and the playhead goes on after them; its condition and chance are decided once, the copies follow (a step that does not play has silent copies); the repeats stop at the loop's end (Move-like: no wrap), the loop going on from its start |
| 6 AMOUNT | LEAP's n (-15..15, +2 at first), JUMP's target step, or REPEAT's n (1..16) |
| 7 LENGTH | the notes' length through Elektron's table: 1/8, 1/4, 1/2, 3/4, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32, 64 steps, INF |

The trig parameters are CC locks numbered 128..133 (`RFM_MOVE_TRIG_*`, never sent), so undo, copy, cut, paste,
double and the blob carry them. The engine walks each track with a playhead (`pos`, `sub`, `tps`): the next step is
chosen half way through a step (functions, conditions and chance decided then), so early notes of the next step
sound in time.

**Session view** (0.10.0, after the owner's review of 2026-11-21 as an Ableton Live user; the board:
`remoteFM14Move/docs/design/session-view-v2.movedesign.json`): the pads' columns 1..7 are tracks (7 of 16 in view),
the 8th column the scenes, a row a scene (4 of 16 in view), as the Move's Control Live mode.

| Gesture | Does |
|---|---|
| A clip pressed | launched at the next bar ((0.12.0) or as Global settings' Launch says, or the clip's own Launch; Instant: at once, nothing waits) (queued: its pad blinks green, its cell flashes with the play icon; the track's playing clip flashes with a stop icon, its pad between green and its own colour; stopped: the track's clip at once, playing with Play) and the selected one at once: its cell framed on the screen. On the playing clip: it plays on, selected |
| A double click (300 ms) on a slot | (0.11.0) now, not at the bar: a clip plays (in the bar's phase), an empty slot stops its track. The first click queued the same thing. (0.13.0, the owner's) **On the clip its track is playing: it stops now** (what the slot was before the first click decides) |
| An empty slot pressed | selected (white), **no clip made**: the first note makes it (a step, the pads with Rec, Capture). The track stops at the bar, as Live's stop button. With Rec armed: a new clip there, recorded into (growing a bar at a time) |
| Shift + a slot | selected for editing; nothing launched or stopped |
| A scene pad (column 8) | its row launched across the 16 tracks (each track at its clip's own Launch, else the project's); tracks with no clip in the row stop. Says "Scene 2". (0.11.0) **Stopped: the scene plays** (the one pad besides Play that starts the transport; Rec armed: the count-in). A double click: now. Its box on the screen flashes with a play icon while it waits; the scene on its way out flashes with a stop icon. With Mute held: nothing; Copy, Delete: below |
| Shift + a scene pad | (0.11.0) the scene's clips stop at the bar; a double click: now. (0.12.0) Nothing of the scene plays: nothing changes, nothing said |
| Copy held + a scene pad | (0.12.0, the owner's; the scene buffer: a row of 16 clips, its own) a row with a clip: copied (its cells dashed, its pads blinking while Copy is held), or, armed (the last scene function a copy or a delete, not this very row's copy), the buffer replaces it (all 16 tracks; a track with no clip in the buffer: emptied there). An empty row: the buffer pasted. Never a launch. "Scene 3 copied", "Scene 5 replaced", "Pasted to scene 5" |
| Delete held + a scene pad | every clip of the row deleted (a playing one: as a clip's delete), into the scene buffer (crossed until Delete is let go), armed; an empty row: nothing. One undo. "Scene 3 deleted" |
| Track 1..3 tapped | (0.11.0) session view is its own layer: that firmware is the one played, the view stays; its summary a second (the 16 tracks: which reach it). The knobs' lights then show it: a column whose track reaches that firmware in the firmware's colour, the others faint white |
| Track 1..3 held + a column's pad | (0.11.0) a click: its track routed to that firmware or not; a double click: its solo there. The pads show the routing while held: a lit column reaches it |
| The steps under a column | the odd one selects the column's track, the even one stops its clip (at the bar). Step 15: nothing. **Step 16: every clip stops** (the transport runs on). (0.12.0) Their icons are dark (lit only for Shift's shortcuts) |
| A select step held | (0.12.0, the owner's) held 300 ms, or a knob touched or turned while it is down: its track's selected clip's settings until it is let go: "Track 3  Clip 2", **knob 1 Launch** (Global, Instant, 1 Beat, 1 Bar, 2 Bars, 4 Bars, Clip), **knob 2 After** (Loop; Next: after its loop, the clip below it, or the top of its run of clips); a tick at each knob's place, the touched one long, its row underlined. Knobs 1, 2 lit, 3..8 dark and dead. An empty slot: "No clip", the knobs dark. **The volume knob**: the level of the firmware the track plays (the one played if it reaches it, else the lowest slot it reaches; the Mixer's level, its popup naming the firmware); none: "Not routed". A quick click only selects; the release does nothing more |
| Mute + a clip, Mute + a step | the clip's track muted; the 16 tracks' mutes (as in every view) |
| A knob (1..7) touched | its column's track selected, and its popup while touched: the track, the firmwares it reaches (a host with the firmware list; none: "Not routed"), its selected clip and length, its channel |
| Knob 8 touched | (0.11.0) "Scenes": the scene that plays (or waits), the rows in view. Its light is green |
| Copy, Delete tapped alone | nothing, in every view (0.12.0: track view's Copy tap duplicated the clip) |
| Shift + Left / Right, Up / Down | (0.12.0) 7 tracks, 4 rows a press (clamped as the single steps) |
| Shift + Play | every clip from its start, as in track view (before 0.10.0: every clip stopped) |

The pads (Live's colours): **green pulsing** a playing clip (the selected one white on the beat), **green steady** the
track's clip while stopped (Play starts it), **green blinking** waiting for the bar, **the track's colour, dim** a
stopped clip (very dim: a clip with no notes), blinking in the track's colour: stopping, **white** the selected clip
or slot, red recording. (0.12.0, the owner's) A column whose track does not reach the firmware played is dimmed to a
quarter (every state; a press shows at full; not with no firmware list, nor while a Track is held); on the screen its
cells' outlines are dotted (the routed ones solid). On the screen a clip on its way out is filled with a black square
(as the playing one with its black play icon); recording: a black dot. The scene pads are green: very dim no clip in the row, dim clips, pulsing (stopped: full) the
scene that plays (every track with a clip in the row plays it, the others nothing), blinking on its way.
A project loaded: clips with no notes or locks are empty slots again (before 0.10.0 every press on an empty slot
made one). Cut, copy and paste as below.

**Copy and cut** (the owner's; no popups: the source is marked, dashed when copied, an X when cut, on the steps row,
the session grid and the LEDs). One clipboard for both:

The exact gestures (0.6.3, the owner's; Delete is a modifier only, as on the Move). Pastes never empty the clipboard:
it pastes again and again until something else is cut or copied. The owner's sequence, Delete held throughout: cut
A; press A again: pasted back; press empty steps: each pasted; press B (notes): cut; press C (notes) right after:
pasted over ("Step 9 replaced"); press C again: cut.

| Gesture | Does | Says |
|---|---|---|
| Delete held, a step tapped **right after a cut** (in this hold) | the clipboard pasted there; a step with notes is replaced | "Pasted to step 9", "Step 9 replaced" |
| Delete held, a step with notes tapped (the first press, or after a paste) | cut: gone, into the clipboard, an X on it | "Step 5 cut" |
| Delete held, an empty step tapped (the first press, or after a paste) | the clipboard pasted there (an empty clipboard: nothing) | "Pasted to step 9" |
| Delete let go | the last action forgotten (the next hold starts fresh); the clipboard stays | |
| Copy held, a step with notes tapped (several in one hold: a group) | copied (dashed) | "Step 5 copied" |
| Copy held, an empty step tapped | the clipboard pasted there (a group: its first step there) | "Pasted to step 9" |
| Copy released after copying, then a plain step | pasted there once (Move's way) | "Pasted to step 9" |
| (0.12.0, the owner's) Delete held, a clip pressed (session view) | deleted at the press, into the clip buffer (an X in its slot **until Delete is let go**, 0.13.0, the owner's: then the slot is an empty one), armed; one undo brings it back. **Delete never pastes**: an empty slot, the same slot again: nothing | "Cut track 1 clip 2" |
| Copy held, a clip pressed | armed (the last clip function a copy or a delete): the buffer pasted over it (replaced; not the very clip just copied: copied again); else copied (dashed while armed), armed | "Copied track 1 clip 2", "Track 3 clip 1 replaced" |
| Copy held, an empty slot pressed | the buffer pasted there (an empty buffer: nothing); a paste disarms, the buffer stays | "Pasted to track 1 clip 3" |
| Copy let go | nothing waits: a plain press launches (0.12.0; it pasted once) | |
| Back | the marks and armed go; the clip buffer stays | |
| Delete tapped, or held and let go alone | nothing changes, on the screen neither (0.6.4) | "Delete: hold and press a step, pad or clip" (spoken only) |
| Delete or Copy held (track view and session view) | (0.12.0, the owner's) at the press an icon says so: Delete scissors, Copy two sheets (track view: the roll's top right; session view: the scene column, a row each). The roll over the 16 step cells as ever (copied dashed, cut crossed) | |
| Copy tapped alone | nothing, in every view (0.12.0: it duplicated the clip, and dropped a waiting copy: Back does that; Shift + step 14 / 15 make and double clips) | |
| Delete held + a pad (track view) | that note out of the clip | "Deleted C3" |
| Track 1..3 held + Delete (let go) | the selected track cleared (no clipboard) (0.12.0: these three were written "Track 4 held" since the lock menu moved to Tracks 1..3 in 0.8.0; Track 4's hold has no Copy, Delete or Mute) | "Track 1 cleared" |
| Track 1..3 held + Delete held + a step | that track cleared | "Track 3 cleared" |
| Track 1..3 held + Copy held + a step, then another step | a track copied onto another | "Copied track 2" |
| Delete held + Play | (0.12.0, q2) the selected clip cleared, the transport as it was | "Clip cleared" |

- **Copy held**: a step (or clip) with content is copied; an empty step (or slot) gets the clipboard pasted (a copy
  or a cut). Copy never overwrites a full step: pressing one copies it again (a group in the same hold).
- **Delete held**: a step (or clip) with content is cut (gone, kept in the clipboard, an X on it); an empty one gets
  the clipboard pasted; (0.6.3) right after a cut any step gets it (as the table). A paste keeps the clipboard
  (before 0.6.3 a pasted cut emptied it, so a cut pasted once).

**Recording** (the record settings, Shift + Record: knob 1 the mode (0.13.0: the jog selects, its click edits), knob 2 the style; saved with the project):

| Mode | Record |
|---|---|
| CAPTURE (default) | playing, the clip empty (the owner's): Record records at once, from the bar line at or before the press (notes keep their place in the bar); Record again ends it at the nearest bar line, the clip that many pages long; a clip with steps records into it as before. Stopped: the owner's intentional capture. Record arms it (blinking); the first note played clears the selected clip, starts the transport and is its first step; the take records with micro timing; Record again ends it at the nearest bar line (just past one: there, at once; else the next), the clip as many pages as bars. Playing: Record records into the existing pages (as RECORD) |
| RECORD | Move's: armed while stopped (no start); Play then gives a bar's count-in, then the transport; records into the selected track's clip |
| STEP | Record toggles a cursor (a red step, a bar over it on screen): each note or chord played goes into the cursor's step, then the cursor moves a step on (the track's rate), round the loop |

| Style | |
|---|---|
| OVERDUB (default) | the take adds to what is there |
| REPLACE | the playhead clears what the take passes over (not the take's own notes) |

Live notes go in **at the press** (visible at once) at the step nearest where they were heard, with their micro
offset, on the track's rate grid; their played length at the release. The host's lead (`rfm_move_seq_set_lead_us`:
the web player's 30 ms) is taken off. A note being recorded is not played again by the clip while it is held.

**Capture** (the Capture button): the last phrase played on the pads becomes a clip on the selected track: back from
the last note until a silence of more than 2 bars (`RFM_MOVE_SEQ_CAPTURE_GAP`), at most 16 bars, rounded to whole
bars. In an empty project (no clips) and stopped, the phrase's own tempo is found (the best fit of a 16th grid and
whole bars, 70..180 BPM) and set. Playing: aligned to the song's bars. Shift + Capture: the capture forgotten.

**Sample** (the audio loop): a press arms it (blinking); it records the firmware's audio from the next bar line
(stopped: from the first bar once Play starts the transport); a press while recording ends it
at the next bar line, so the take is exactly N bars (`RFM_MOVE_ACT_AUDIO_REC`, frame-stamped; the web player's
worklet cuts at those frames). The take goes to the recorder's Takes, named with its tempo and bars. A waveform in
the screen's corner while it records. (Tap tempo: Shift + step 5.) (0.15.0) With `RFM_MOVE_CAP_SAMPLER` Sample is
the sampler app instead ([The sampler](#the-sampler-0150)).

**Buttons.**

| Move | Sequencer |
|---|---|
| Play | start / stop (following an external clock: at its next bar). Shift + Play: every clip from its start (0.10.0: in session view too; every clip stops: step 16 there) |
| Record | the record mode's (above). Shift + Record: the record settings |
| Capture | Capture (above) |
| Sample | the audio loop (above). (0.15.0) With `RFM_MOVE_CAP_SAMPLER`: the sampler app, in every layout (the FM-1 layout: SEL while it is closed and no take runs); Track 1..3 held + Sample: the app with that slot as the source, Track 4 held: the master ([The sampler](#the-sampler-0150)) |
| Loop | held or latched: pages and the loop (above). Shift + Loop held: FILL |
| Copy, Delete | copy, cut, paste (above). Delete held + Play: the clip cleared (below) |
| Undo | undo (multi-level); Shift + Undo: redo. (0.12.0, a13) Nothing to undo or redo: "None" a second |
| Play with Delete held | (0.12.0, q2, the owner's) the selected track's selected clip cleared (its steps, locks, trigs; its length kept), "Clip Cleared", one undo; the transport as it was (track and session view) |
| Mute | held: the screen shows the 16 tracks (with notes filled, muted crossed out); + step n or + clip: mute track n. (0.12.0, a2, the owner's) A quick click (let go within 300 ms, nothing else pressed) latches that layer (its light on) until Mute's click again or Back; a hold is the layer until its release. The tap no longer mutes the selected track |
| Shift (the icons) | while Shift is held or locked (and in FM-1 mode while Track 1 is held), the white icon LEDs under the steps (CC 16..31) light every step with a shortcut: dim, full where it is on (a toggle on, its menu open); off otherwise and in MIDI and Control mode. Written on change only, like every LED; the step pads' own LEDs are separate |
| Shift | double-tap (a tap, then a press within 300 ms): latched (its LED lit, every Shift combination without holding it); a press unlatches it. Shift + Sample: nothing (the Move has none; 0.12.0: its light dark while Shift is held) |
| Menu | Track <-> Session view (or out of the project grid). (0.11.0) An open Shift menu stays (Back closes it): the view flips under it, named a second. (0.12.0) Shift + Menu, Back held + Menu, Track 1..3 held + Menu: the same, from every layout (above); out of session view: the slot's own layout |
| Back | the universal escape: closes the latest layer still on, one a click (a Shift menu, the project grid, Loop's latch, Mute's latch, Shift's lock, the step / clip clipboard's marks, the page clipboard's); with none: nothing (never a mode change). Leaving Loop shows the sequencer at once. (0.12.0, a13) Its light: on only while there is a layer for its click to close, full at its press, else dark. (0.12.x) Held: shortcuts with Left, Up, Loop, Play, Delete (see [Back](#back-043)) |
| Track 1..4 | the modes (FM-1, MIDI, Control, Sequencer); Track 4 held in the sequencer: the tracks and the track's settings (above; the screen: layout, scale, what changed at 2x, the channel and the rate as strips) |

**Shift + step** (the Move's printed functions, its manual's numbers; a menu opens on the screen, the same Shift +
step or Back closes it):

**The Shift menus** (all but Projects) take only the screen, the knobs (the volume knob where it uses it) and the
jog; Back closes them (0.12.0, l2: Menu never does: in a note layout and session view it flips the view under the
menu, in the FM-1 layout it is the firmware's HOME; Shift + the menu's own step closes it too). The pads and steps
keep doing what the mode under them does, so you can play while one is open (the owner's). Projects mode keeps its
pad grid. The held-Track menus are not Shift menus: their pads and steps stay as they are. (0.12.0, l3 + a17, the
owner's) **Held controls take over an open menu while held**: a held step (its popups over the menu; the knobs, the
jog, the volume knob and the arrows its own), Loop held (its pages and the jog; latched after the menu was opened:
the same until Back), Copy, Delete and Mute (their pictures), Shift (its shortcuts on the steps; Shift + another
menu's step switches to it). Except a held step they leave the menu its knobs, lit. At the release the menu is back
with its knobs, jog and screen. A Track 1..3 tap under a menu: the slot changes, its popup a second, the menu stays.
**Every menu names its knobs** (0.12.0, a5, as the arp's): the touched knob's (or the one turned last, for a second;
or the jog's value when it turned) name and value in the top line, 2x where it fits ("Channel 2", "Scale Dorian");
under it a tick at the place of each knob the menu uses, the active one long; at rest the top line is the menu's own
(its title, its main value). A knob a menu does not use: dark, dead, no header. (0.12.0, a6, a7) An open menu owns
the knob lights, the touch's header (no FM-1, CC or track popup), the turns and the jog in every view: over the
Mixer, Routing, the browser, session view, the FM-1 layout. Global settings has its jog over the projects view too.
(0.13.0, the owner's: "The jogwheel should work like it does for the global settings in which it selects a parameter,
then click to edit that parameter, click to go back to selecting") **The jog in a Shift menu selects, its click
edits** (every menu but the arp's, which keeps its pages and its on / off click, and Global settings, which is its own
list): the turn moves a cursor over the menu's knobs' parameters in knob order, stopping at the ends, and changes
nothing; the header names the selected one and its value for a second and its knob's tick is long (the cursor; at
opening the first). The click edits it: the header stays, its words between two arrows, and the turn changes the value
as its knob does (the same limits; a list an item a detent; Shift fine where the knob has it); the click again: back
to selecting ("Done"). Back closes the menu from either state. A knob turned works as before and takes the cursor
(editing another parameter ends). Words: the parameter and its value; "Editing Rate: 1/16"; "Done". A held control
over the menu (a step, Loop, Track 4's layer) keeps the jog and its click while held.

**Knob LEDs in a menu**: while a held Track's menu or layer is shown, or a Shift menu or projects mode is open, only
the knobs it uses are lit, in the menu colour (deep orange, 255 72 0, which no mode or bank uses; full while touched),
the others off; the knobs' own colours come back when it ends. Each menu declares its knobs (`rfmv_menu_knobs`,
`rfmv_seq_menu_knobs`): Track 2 knobs 1-2, Track 3 and Track 4 all 8, Track 1's Shift layer none, projects knob 1,
Workflow 1-4 (0.14.0: the feel on 3, Quantize on 4), Tempo 1-2 (none while following), Groove 1-2, Layout 1, Keys & Scales 1-4, the arp all 8 (its lanes
1-4), Record 1-2, Velocity 1-2 (0.13.0: knob 2 the option, the jog's before), Global Settings none (0.6.2: the jog's list; 0.12.0: its knobs' old jobs from before
0.6.2 removed: dark and dead everywhere). (0.12.0, a12) The project grid's knob 1 only while a project's pad is held
(its touch: "Colour" and the colour's name); the TRIG page's knob 8 dark with no popup; a held step's CC knobs with no
lock dim (a turn writes one), locked ones by their value.

| Step | Function |
|---|---|
| 1 Set Overview | the project grid |
| 2 Global Settings | (0.6.2) a list on the jog wheel, sticky (Shift + step 2 again, or Back when not editing, closes it; 0.12.0, l2: Menu no longer does, and it has its jog over the Mixer and Routing too). (0.6.10, the owner's direction A) On top the Move's battery: an icon filled to the charge and the percentage at 2x ("--" until the first STATUS: Move Remote's STATUS byte 4 as the surface reports it, taken as 0..100 %, clamped; the host passes it with `rfm_move_set_battery`), "GLOBAL" on the right, a rule under them. Rows (0.6.12: Speech first, then the others; four show, the list scrolls): Speech (Off, On, Jog: see Speech modes), Clock (Auto, Int, Ext; Int is the default (0.5.5); Auto follows a firmware only once it is a clock master: a Start or Continue, then 24 steady clocks, until its Stop), Velocity (the option, as the Velocity menu), Fixed level, Popup size (FM-1 mode's popups: Full, the default, or Half, 0.5.9), (0.12.0) Launch (when a clicked
clip, scene or stop takes effect: Instant, 1 Beat, 1 Bar (the default), 2 Bars, 4 Bars, Clip: the end of the loop the
track plays; saved with the project, version 8; a clip's own Launch wins), (0.14.0) FW Play (Firmware, the default:
a Play that goes to the firmware (the FM-1 layout, the FM-1 page's Knobs+Keys, a takeover that claims Play) is its own
only; Both: that Play press also starts / stops the Move's transport; Shift + Play, the firmware's shifted Play, never;
saved with the settings, `fw_play`, in the Popup size byte's bit 1). Turn the jog: the selected row (inverted) moves; click: edit its value (the row framed, the value inverted between small arrows), the jog changes it; click again or Back: done. While it is open it owns the controls: no knob, pad, step or button reaches the FM-1, the firmware or the sequencer (the knobs do nothing; their touches show nothing); releases pass, so a pad held before it opened lets go; the Track buttons and Shift + step 2 pass |
| 3 Workflow Settings | (0.14.0: also Shift + Track 1..3, for that firmware's track; titled "Track n") menu: knob 1 (0.13.0: the jog's selection, as every menu's) the track's MIDI channel, knob 2 its rate, (0.14.0) knob 3 its feel (Straight, Dotted, Triplet: the triplet layout below), knob 4 Quantize's amount (5 % a detent; was knob 3); three of the four rows shown, the list scrolled to show the selected one, a row too wide for 2x its value alone ("Straight"). (0.14.0, the owner's) the selected row (the jog's, a knob touched or turned) is drawn in its place as Global settings' row: inverted while selecting, framed with its words inverted between two arrows while editing; nothing is copied to the top. (0.14.0, the owner's 2026-11-24) no knob ticks under the title: the highlighted row is the only mark (the rows are knobs 1..4 in order); the picture menus keep theirs (R5's mark for the knob's place) |
| 5 Tempo | each press is a tap (tap tempo: the average of up to 4 taps) and opens the menu: the tempo at 4x; knob 1 1 BPM (Shift, or knob 2: 0.1; 0.13.0: the jog selects one, its click edits it); following the firmware's clock: locked (a padlock, EXT; taps ignored) |
| 6 Metronome | on / off (off at first): a click a beat, the bar's first higher (`RFM_MOVE_ACT_CLICK`, frame-stamped; the web player plays it after the volume, never into a recording); also in the count-in |
| 7 Groove | menu: knob 1 the groove (0.13.0: the jog selects, its click edits), 5 % a detent (Shift: 1 %), 0..150 %: the off 16ths late, 100 % a triplet swing (Move's). Knob 2: GLOBAL (the project's groove, the default) or TRACK (the track's own, starting from the global one) |
| 8 Layout | menu: knob 1 the layout (0.13.0: the jog's click edits it) |
| 9 Keys & Scales | menu: knob 1 the root (0.13.0: the jog selects a knob's parameter, its click edits it), knob 2 the scale, knob 4 the layout. Knob 3: GLOBAL (the project's key and scale, the default; a new project starts with MIDI mode's) or TRACK (the track's own) |
| 10 Velocity | menu, sticky (Back or Shift + step 10 closes it): knob 2 the option (0.13.0; the jog's before: the jog selects knob 1 or 2, its click edits), DYNAMIC (as played), FIXED (every pad at the level; switching to it from another option sets the level to the last velocity played), SOFT (a light touch plays louder), LINEAR (the straight line: as played), HARD (it takes a firmer one); knob 1 the level 1..127 (FIXED's). The screen: the name big, the curve (played in, out), the level big with its bar (dotted while not FIXED). Every pad that plays goes through it, in every mode (MIDI notes, FM-1 keys, the sequencer's live notes, recordings and step entry). A setting (saved with the others), not the project's; its icon LED is full while FIXED |
| 11 Repeat (arp) | menu: the track's arp, FIZZ's ARP (its defaults: 1/8, two octaves, QUANT 1/16). The jog turns its pages. Page 1: knobs PATTERN (UP .. AS PLAYED, 17), RATE (1/1 .. 1/64 of a bar, named as said: 1/16, triplets 1/16T, quintuplets 1/16Q), RANGE (octaves; 0.14.0: -8..-1 down, 1..8 up, no 0, "Oct -2" over the walk, said "Range: 2 octaves down"; down, each pass is an octave step lower and the power chord / chord steps follow), knob 4 nothing (0.14.0: CHANCE gone, every step plays), MODE (OFF, ON, 1-ST, ONCE, HOLD), GATE, OCT DIST, QUANT (when a run starts: at once, or the next 1/n of the song's bars); the pattern's walk drawn. Pages 2 and 3: the MOD lane (x as played, . rest, - tie, R repeat, < lowest -oct, > highest +oct, " power chord, # every held note, 1..6 the n-th played) and the TRANSPOSE lane (-48..+48). (0.11.0) **The 16 step keys are the lane's 16 steps** (a press picks one; none reaches the clip while the lane is open); knob 1 its function or semitones, knob 2 the lane's length, knob 3 RESET (NORM, NOTE: a new note restarts it, MPAT / TPAT: as that lane comes round); knobs 4..8 dark and dead. The MOD lane is drawn as a sign a step (play a block, rest a low dash, tie a line, repeat a hollow block, lowest an arrow down, highest an arrow up, power chord two bars, chord three, a played note its number). **The header** names the touched knob and its value ("Gate 95%", 2x where it fits), or the one turned last for a second; at rest "Arp On" / "Arp Off" .. , "Mod", "Transpose". **The jog's click turns the arp off and on again** (as it was) on the main page; (0.14.0, the owner's 2026-11-24) **on a lane page it rolls a random pattern** into that lane over its length (MOD: step 1 plays, then mostly plays with some rests, ties after notes, now and then a repeat or an accent: lowest, highest, power chord, chord; never all silent. TRANSPOSE: mostly 0, octaves, fifths, a few scale steps), a new one each click, the step keys' wave as on entering the lane, said "Mod random"; one undo step. (0.14.0, the owner's 2026-11-24) **Delete held + the jog's click resets**: on the main page the track's whole arp to its defaults (its lanes too; on / off as it was), "Arp Reset"; on a lane page that lane (its steps and its length 16), "Mod Reset" / "Transpose Reset"; one undo step (Undo brings it all back); Delete is lit while the menu is open. (0.14.0) **An arp that is on but can never sound** (every MOD step a rest, tie or repeat) says "Arp / Silent" when turned on, "Arp Silent" in its header at rest, its pictures dimmed as when off. Step 11's icon is full while the arp is on. (0.12.0, the owner's) **Shift + step 11 is gated**: the press turns the step white and nothing else; a click (let go before 300 ms) opens the menu at the release (open: closes it); held 300 ms it turns the arp off / on as the jog's click does, still down, and the menu stays as it was (its release: nothing). The hold and the jog's click show "Arp" over "On" / "Off" (or the mode it came back to) the whole screen for a second. **Off, the pictures are dimmed** (a 50 % checker over the walk and the lane; the header plain). **The lanes take the step keys** (the main page leaves them to the clip): on entering a lane page they flash white and a wave settles them left to right into the lane's lights (360 ms, 32 LED writes; a step pressed meanwhile works at once and ends it); back on the main page the clip's lights at once. A step past the lane's length is dark and dead. **Step 1 held + step n**: the lane's length n at that press (n again: 1; step n is not picked). **A lane step held** 300 ms (or a pad pressed while it is down) **takes the pads** as its shortcuts, none plays: MOD, the bottom row play, rest, tie, repeat, lowest, highest, power chord, chord (orange), the row above the played notes 1..6 (blue), the rest dark and dead; TRANSPOSE, the semitones -16..+15 from the bottom-left (rows -16..-9, -8..-1, 0..+7, +8..+15; 0 and the octaves green, the others dim blue). The step's value white; a pad sets it at the press, the header names it ("5 Tie", "3 +7"). Let go: the pads back at once; a quick click only picks. Notes held on the pads when it begins play on until let go. On, the track's pads play through it (0.14.0) **and so do its clip's notes**: each held for its length, the arp's notes go out on the track (its routing, mute and solo), the clip's own notes do not; every track has its own arp, running while its clip plays; the transport's stop, the clip's stop or launch let go of what the clip held (a Hold set it latched too). **Recording with the arp on takes what was played** (the held notes, their real lengths), not the arp's notes: the clip then plays them through the arp, arpeggiated once (before 0.14.0 the arp's notes were recorded) |
| 13 | (0.12.0, q2, the owner's) nothing: its icon dark (the clip is cleared with Delete held + Play, 0.14.0: and its loop back to one bar from step 1, one undo step; Clear Clip was here) |
| 14 New Clip | the track's next empty slot gets a new clip, selected |
| 15 Double Loop | double the loop |
| 16 Quantize | the clip's micro timing quantized by the Workflow amount (100 %: on the grid) |

**The jog's selection in a Shift menu** (0.14.0, the owner's 2026-11-23: "it should just show a marker or something
in front of the row that is selected, or do things the same way the global settings does"): one way for all of them,
Global settings' row. Workflow draws it in the selected row; the menus with a picture (Tempo, Groove, Layout, Keys,
Velocity, Record) in their top line, which, once the jog (or a knob's turn) has selected something, holds that
parameter's name and value for as long as the menu is open: inverted while selecting, framed with the words inverted
between two arrows while editing (was: the words copied to the top for a second, then the title again). A knob only
touched still names itself there plainly (R5). The arp's menu keeps its own design.

**Track rates** (0.14.0, the owner's: "Track settings needs more rates. Look at the rates we have available in
Elasticat. I'd add a separate parameter for doing straight, dotted, or triplet rates"): a base rate, Elasticat's
tempo-synced list, 1/32 1/16 1/8 1/4 1/2 1 Bar 2 Bars 4 Bars (a step 12 .. 1536 ticks at 96 PPQN), and a feel:
Straight, Dotted (x 3/2), Triplet (x 2/3), so a step is 8 (1/32T) .. 2304 (4 Bars dotted) ticks. Named "1/16", "1/8D",
"1/16T", "1 Bar T" where one word holds both. Before 0.14.0:
1/32T 1/32 1/16T 1/16 1/8T 1/8 1/4T 1/4 1/2T 1/2, read as their base rate and feel. A step always has 24 micro ticks,
scaled to the rate.

**The triplet layout** (0.14.0, the owner's: "add a layout for triplets which disables to 4th step in each group of 4
steps, So each group is the 3 notes of a triplet"): a track whose feel is Triplet shows triplets on its step keys: in
each group of four (steps 1-4, 5-8 .. of every page) the fourth is dark and dead (no note goes in, no hold, copy,
paste or cut; never played, never the playhead), the three others the group's triplet. A page holds 12 steps and
spans what 4 groups span: at 1/8 Triplet a bar, in phase with straight tracks bar after bar; at 1/16 Triplet half a
bar. The clip is unchanged (16 steps a page, 256 at most): the engine counts time in live steps, so loop lengths,
the launch quantization CLIP, LEAP, RANDOM, JUMP, REPEAT, a recording clip's pages and Capture all follow; a recorded
note lands on the nearest live step; a held step's span (step held + another) counts live steps; Step record's
cursor skips the fourth steps; ratchets and micro timing are those of the live step; a triplet track has no groove.
What is on a fourth step stays: skipped while the feel is Triplet, back (lit, played) with another feel. The steps
row and the piano roll leave the fourth steps out. A project from before 0.14.0 with a triplet rate gets its steps
moved onto the live steps (step n onto n + n / 3, its loops and JUMP targets too), so it plays as it did; the rare
clip longer than 192 of the old steps is cut at 256.

**Projects mode** (the owner's: a mode of its own, just for picking a project; Shift + step 1, Track 4 held +
step 1, or Track 4's double click). The pads are the 32 projects (top-left = project 1), each in its own colour (dim; the open
one (0.14.0) white: breathing slowly when stopped, as pad 1 (the project's own sound) in States; pulsing on the beat while it plays;
loading: blinking; empty: off). Everything else does nothing here:
steps, the jog, the knobs (but one), the Shift shortcuts, the Track buttons.

- **A pad** (0.14.0, gated as R2 says): its **press** shows the project's info at once (its number, tempo, clips and
  firmwares) and loads nothing; **a click** (let go within 300 ms, nothing else done) opens it at the release: the
  open one saved, that one loaded (the host answers `PROJECT_LOAD`), or, for an empty pad, a new empty project there
  (saved at once); **held** past 300 ms it is the info only and its release does nothing. Projects mode stays.
- **The open project's pad clicked** (0.14.0): saved now (`PROJECT_SAVE`), "Saved" across the screen a second, its
  pad off, on, off (120 ms each); nothing reloads.
- **Delete + a project**: cut, into the host's buffer and gone at once (`RFM_MOVE_ACT_PROJECT_CUT`; 0.14.0: its pad
  red for 250 ms, then dark; an X on the screen only while Delete is held, then an empty slot's dot); **Undo**
  (0.14.0; lit only while it can) puts it back in its slot as it was (the buffer pasted there, `PROJECT_PASTE`: the
  firmwares' memories with it; the open one also loads again), "Undo / Project 07"; one step, ended by a paste, a
  copy, another delete (which has its own undo then) or leaving the page. **Copy + a project**: copied (`..._COPY`;
  dashed, blinking); **Delete or Copy + an empty pad**: the buffer pasted there (`..._PASTE`; a cut once, a copy again
  and again; 0.14.0: after a copy's paste its source is no longer dashed, the copy stays in the buffer). The open
  project cut: saved first, an empty project in its slot.
- **Duplicating a project** (the owner's "a way to duplicate a project to make a new variation"): hold **Copy**,
  press the project's pad, then (Copy still held) an empty pad: the copy is pasted there (`PROJECT_COPY` then
  `PROJECT_PASTE`), clips, settings and (0.13.0) firmwares with it. A host with `RFM_MOVE_CAP_PROJFW` copies the
  project's firmware state files along with its blob (its per-project copies of each firmware's memory).
- **A pad held + knob 1**: its colour, one of 16 (the Move's palette; one every few ticks), shown big on the screen;
  stored with the project (blob byte 18, `..._COLOR`: the host sets it in the stored blob and makes its check again).
- **Play**: the selected project plays (still loading: once loaded; 0.13.0: once its firmwares are in); Play again
  stops it.
- **Back** or **Menu** leaves.
- (0.13.0, `RFM_MOVE_CAP_PROJFW`) **A project holds its firmwares**: a stored project's pad shows the loading screen at
  the press (its number, "Loading", a row a firmware slot with a bar), a pad held shows the project's number, tempo,
  clips and its three firmwares (a dotted line: an empty slot, or a project from before 0.13.0), and an empty pad's
  new project has its three slots empty. See [Projects that hold their firmwares](#projects-that-hold-their-firmwares-states-0130).

Changes are autosaved 2 s after the last edit (`RFM_MOVE_AUTOSAVE_MS`), and on every switch. (0.14.0) Blob version 10:
no new bytes, a track's rate byte holds its base rate (bits 0..3) and feel (bits 4..5); versions 1..9 load (their ten
rates as the same timing, a triplet track's steps moved into the triplet layout). A core before 0.14.0 refuses a
version 10 blob ("Load failed"; the project it has stays). Blob version 3 adds
to version 2 the project's groove, key and scale, each track's GLOBAL / TRACK choices, own key and scale and solo,
and the arp's QUANT, RESET and lanes. Version 2 (loop starts, made clips, rates, grooves, arps, layouts, CC numbers,
the record settings) and version 1 blobs still load (a version 2 groove becomes the track's own). The project's
colour is byte 18 of the header in every version (older blobs: 0, the slot's own colour).

**List knobs.** A menu knob that picks from a list (a scale, a root, a layout, a rate, an arp pattern, mode or
QUANT, RESET, a MOD lane value, the record mode and style, GLOBAL / TRACK, a condition, a step function, a project's
colour) moves one item every `RFM_MOVE_LIST_TICKS` (5) of the Move's ticks, the same helper everywhere
(`rfmv_list_ticks`); a turn back or the knob let go forgets the rest. Numbers (tempo, groove, gate, ..) as before;
the jog wheel stays 1:1.

**Screens**: track view, session view and the project grid are pictures (below, "The screen").

**Move's sequencer and this one.** Checked against the Move manual (`AbletonMove/move1-manual-en.md`). Where the
owner chose otherwise, the owner's way: Delete = cut / paste, Copy pastes to empty steps, the record modes (Capture
first), Sample = an audio loop (tap tempo on Shift + step 5), the Loop wheel a step a detent (Shift: a page; the
Move's: a bar, Shift a 16th), a hold never clears a step, an empty slot makes a clip. Left as they are:

- **The design's buttons**: Menu = Track / Session (a view, not a mode), Back = close a layer, the Track buttons = the four modes, so Track
  4 held + step stands in for Move's Track buttons (select, solo, mute) and Mute + step for Mute + Track.
- **Not here**: copying a range of steps, per-step automation shown as red steps, count-in on / off, the
  Move's Repeat for drums (step 11 is the arp).

## Back (0.4.3)

Back works the same in every mode and in the projects view, on its release:

| Back | Does |
|---|---|
| a click (let go within 300 ms, nothing else pressed, no Track held) | closes the latest layer still open ((0.13.0) States first: their copy / delete marks, then the layer; a Shift menu, Loop's latch, Shift's lock, the clipboard's marks; in the projects view Capture, States, the description, the selection, the catalog, FX setup's module list); with nothing to close it flips: (0.8.0) the slot's layout to the Sequencer and back ("Sequencer"; the layout named going out); the projects view back to where it came from. (0.4.5) Each flip is named for a second, as a Track tap names a mode: going out, the mode (FM-1, MIDI, Control, Seq); going in, "Firmwares", "Projects" (Track 4), "FX & Mix", or (0.5.2) "Mixer" when FX setup is on its Mixer. Menu's toggle between the firmware / project pages and FX setup is named the same way (0.5.2). The first press in the view drops the name |
| a click in FX setup (0.4.5) | FX setup is no layer: Back flips straight out (Menu alone toggles FX setup). The view remembers it: the next way in (Track 4's tap) opens FX setup again on the same page. Kept in the settings (`fw_fx`) |
| held + Track 1..3 | (0.8.0) nothing of its own: the Track acts (a tap plays its slot), Back's click is cancelled |
| held long, alone | nothing |
| held, in the sequencer's track or session view (also under a Shift menu) | (0.12.x, the owner's) its shortcuts, each at the second button's press, named a second; the button's own job does not happen and Back's click does not come (a menu open stays open). Past a click's time (or once something is pressed with it) the shortcuts light (Left, Up in session view, Loop, Play, Delete, Menu; a pressed one full) and the other buttons go dark and dead; nothing of this shows on a click. Not in the FM-1 layout, a takeover, the projects view, or with a Track or a step held |
| held + Left | home: track view bar 1 ("Bar 1", as Left's popup); session view track 1 at the left ("Track 1", the 16 tracks' strip) |
| held + Up | session view: scene 1 on top ("Scene 1", the 16 scenes' strip). Track view: dead (no octave) |
| held + Right, Down | dead |
| held + Loop | the selected clip's loop back to the whole clip (from step 1 to its loop's end or its last note's bar, whichever is later), one undo step; already whole: the popup only ("Loop", the length, as the jog's) |
| held + Play | the transport to bar 1, never started or stopped: playing, every clip again from its start (Shift + Play's restart); stopped, nothing (Play starts at bar 1 anyway). Its icon over "Bar 1" |
| held + Play where Play alone is the firmware's | (0.14.0, the owner's "Holding back + Play should always trigger play on the Move's side") the FM-1 layout, the FM-1 page's Knobs+Keys, a takeover (its buttons claimed or not): the Move's Play, at the press: starts or stops the Move's sequencer as Play does in the track view; nothing reaches the firmware (neither Play nor Back), Back's click does not come. The play / stop icon over "Move Play" / "Move Stop" (said "Move play"; a takeover's own screen stays). A takeover that claims the Track buttons too has Back itself: there it stays the firmware's |
| held + Delete | closes at once everything Back's clicks would (a Shift menu, Loop's and Mute's latches, Shift's lock, the clipboards' marks): "All closed"; nothing open: "None" (as Undo). Deletes nothing, arms no delete, the view and mode stay |

Shift + step 1 and Track 4 held + step 1 open the sequencer's projects (0.8.0; as Track 4's double click).

## Up and Down in FM-1 mode (0.4.4)

The owner's layout: Back flips, Up is the FM-1's SEQ. (0.15.0, the owner's 2026-11-25: "Get rid of this, it's confusing and the track buttons work fine for switching firmwares") Down does nothing in FM-1 mode: dark and dead.

| Button | FM-1 mode | MIDI modes (2, 3) | Sequencer mode | projects view |
|---|---|---|---|---|
| Up | the FM-1's SEQ (press and release; its LED SEQ's level) | octave up | as before (its own) | as before (its own) |
| Down | nothing: dark and dead (0.15.0; 0.4.4 .. 0.14.0: the next loaded firmware slot) | octave down | as before | as before |

(0.8.0) In the Sequencer, a firmware Track held + Up / Down is the selected track's rate.

## Takeover (0.7.0)

A firmware can bring its own Move interface (FM1VST `docs/RFM1-TAKEOVER.md`, `tk1`): it draws the screen and its
LEDs and gets the Move's raw input for the parts it **claims** (`RFM_MOVE_TK_SCREEN`, `PADS`, `STEPS`, `KNOBS`,
`JOG`, `VOLUME`, `BUTTONS`, `TRACKS`; `ALL`). The host reads the slot's record and hands the core what it says:
`rfm_move_set_takeover(m, slot, claims, name)`, `rfm_move_tk_screen`, `rfm_move_tk_leds`, `rfm_move_tk_say`; the
core sends the firmware `RFM_MOVE_ACT_TK_INPUT` (the raw packet, for `rfm1_tk_input`) and `RFM_MOVE_ACT_TK_STATE`
(for `rfm1_tk_state`). Under a takeover the settings stay in FM-1 mode (`rfm_move_mode()` says
`RFM_MOVE_MODE_TAKEOVER`), so every part not claimed keeps FM-1 mode's meaning and LEDs.

| | |
|---|---|
| (0.8.0) Track n held + pad 2 (its layout menu) | the slot's takeover, if it has one (pad 2 lit purple; dark with none): its name over the whole screen ("CHORDS / Takeover"), said "Chords, takeover". It is the slot's layout: Track n's tap plays it again |
| Track n held in it (TRACKS not claimed) | the layout menu over it: the pads, steps, knobs and Up / Down are the menu's while held (the firmware gets only releases of what it held); pad 1 FM-1, pads 3..6 a note layout |
| volume touched + Back | FM-1 mode from any takeover; Back is not sent, nor its release |
| a slot switch (a Track tap, Down, the projects view, the host) | the shown takeover closes; the slot switched to opens in its own layout |
| the projects view, a Shift menu (Global Settings) | the takeover closes while it is up and opens again after |
| claimed input | to the firmware only; a release goes to whoever got the press |
| Back with BUTTONS but not TRACKS | held back: press and release together at its release, if no Track was pressed meanwhile (a Track still acts) |
| closing (any way), a claim dropped, `rfm_move_release_all` | a release for everything the firmware holds (pads, steps, buttons, touches), then `TK_STATE` off (or on with the new claims) |
| the screen | the firmware's with SCREEN, under the name popup and the host's messages only; else FM-1 mode's |
| the LEDs | each claimed one the firmware's (the whites: r 0..255, scaled to 0..127), the rest FM-1 mode's |
| words | the takeover's name on entering, the layout on leaving ("ChoralRoot, FM-1 layout"), the firmware's own (`rfm_move_tk_say`, a SAY with its slot) while shown and words are open; `TK_STATE`'s flag says whether speech is on (sent again when it changes) |
| claims 0 | no takeover: pad 2 is dark, an open one closes and the slot's layout is FM-1 again |

## Firmwares (0.4.0): the projects view's Tracks 1..3, FX setup, states

The owner's design (move-firmware-browser-design.md, revision 2) and its mockup's screens (ported pixel for pixel,
`src/fw.c`). It is on once the host calls `rfm_move_fw_list` (a Move-hosted RemoteFM1); a host that never does keeps
the projects view as it was (Track 4, Session leaves). The API, the actions, the library blob and the threading are in
the host guide's v0.4.0 section and `include/rfm1cs/move.h`.

(0.8.0) A firmware Track's double click opens the view on that slot; Track 4's tap the Mixer or Routing, its double
click (and Shift + step 1) the projects. In the view a Track tap leaves it (Track 1..3: that slot played; Track 4: the
Mixer / Routing flip). Track LEDs: the loaded firmware's colour, the played one lit at rest everywhere (0.14.0:
steady full in the FM-1 layout and a takeover, breathing 700 ms full / 300 ms at 60 % in the sequencer: a note layout,
its Knobs / Knobs+Keys pages, session view; 60 % steady on Track 4's pages, where Track 4 is full), the others a
quarter, an empty slot dark (the played one faint white), a slot past the host's voices blinking slowly; in FX setup the
pages' colours.

| On a firmware track | Does |
|---|---|
| Pads | a bank of up to 32 (A-Z; Favorites in their order); a pad selects; this track's firmware breathes, on the screen a white cell; one on another track a dotted cell |
| Jog press / turn | load into this track (`FW_LOAD`; a bar until the host says) / the description, a row a detent (0: the name view) |
| Step 13 | (0.14.0, the owner's 2026-11-24) **Random**: at its press, a firmware of the whole list as it is (every bank of Favorites / All, the filter, the full catalog with what is not installed), never the one selected, is selected (as a pad's tap: named and said; loading or downloading stays the jog's press / a double tap); the bank it is in is shown and the pads there roll white to it (240 ms). Its icon is lit while the browser is open (a function, R6), the step dim white with two or more in the list; one or none: dark, a press says "Nothing to pick". No tag on step 13 any more: a library's tag there moves to a free step 8..15 at load (else it goes) |
| Steps 1 / 2, 3..7, 8..15, 16 | Favorites / All (installed); the catalog's five categories, eight user tags and (0.5.4) Fantasy (magenta: the Fantasy CFWs, virtual firmwares such as Elasticat and Ripple, `rfm_move_fw_t.virt`; built in, never a tag; a ninth user tag from an older library moves to a free step 8..15): (0.4.2) one at a time, radio-style (a step shows just that one; the lit one again: all categories), within Favorites / All (AND). (0.4.1) No favourites: All is shown on entering the view or a firmware track, and when the last one is removed; step 1 / 2 is still the user's choice, kept in the settings. (0.4.2) A tap acts on release; the popup names the step big, its state, and what the list shows (2 lines); holding a step shows that without changing it. (0.12.0, a14, the owner's) A step is white at its press (the toggle still at the release, the hold's popup at 300 ms); the icon under a step is lit only for a filter that is on (with Capture held: the tags the selected firmware has), dark for the others |
| Sampling, knob 1, Capture held + step | favourite, pad colour (16), tag (a free step makes "Tag N"; step 16, Fantasy, only shows) |
| knob 1, knob 2: touched; their LEDs | (0.6.13) a knob with a job is lit (knob 1 the selected firmware's colour; knob 2 the favourites' pink while a favourite is selected in Favorites) and a touch shows its popup at once (the colour; "SLOOP / position 3 of 17"; speech "Colour: Red", "Move SLOOP, 3 of 17"); without a job: dark, a touch nothing |
| knob 2 (Favorites, no filter) | (0.6.9) the selected favourite moves up (left) or down the list, a place every few ticks; the pads follow, the selection stays on it, the bank shown follows it; the popup "SLOOP / position 3 of 17" (the favourite popup's layout); kept in the library (FW_LIBRARY). Nothing in All, the catalog or a filtered list |
| (0.5.4) badge | a Fantasy CFW: a small diamond in its grid cell's top-left corner, a framed diamond on the name view's image (the library and the catalog) |
| Delete | unloads this track's firmware at once (`FW_UNLOAD`), with a message |
| Left / Right | banks (lit when there is one) |
| Mute | the catalog: step 1 Update ready (on the Move, a newer version) / step 2 Not installed, one at a time (the lit one again: everything), 3..16 the same one category or tag; an update breathes with an up arrow, a firmware not on the Move faint white, dashed under; a jog press downloads or updates (`FW_DOWNLOAD`), and (0.4.2) loads one on the Move and current into this track (`FW_LOAD`). A download or update done while that firmware is still selected: the popup offers "Jog: load on Track n" |
| Loop | the firmware's states (Move's Set Overview): a stored state loads, an empty pad saves there, Copy + pad saves over, Delete + pad deletes (`STATE_*`); Loop or Back leaves. (0.13.0) With `RFM_MOVE_CAP_PROJFW`: the States layer and its gestures (below, [States](#projects-that-hold-their-firmwares-states-0130)); Loop or Back returns to the browser |
| Track 1..3 held + volume | that slot's level (`FW_LEVEL`; in any mode with the browser on); the volume alone stays MASTER |
| Session | FX setup (0.5.0: one layer, see [FX parameter pages](#fx-parameter-pages-050)): pad columns 1, 3, 5 the MIDI FX, firmware and audio FX slots; Track 1..3 select those columns, Track 4 the Mixer; steps a MIDI FX slot's track (one, or all: the same step again) or a firmware's 16-track filter; a slot's pad held + the jog chooses its module, the jog's click opens its pages; knobs the module's page, or a firmware's level, pan, sends; (0.4.5) a touch shows the parameter's popup while touched; Delete + pad empties an FX slot; Session toggles FX setup, Back (0.4.5) leaves the view. A host with no modules has no MIDI FX / Audio FX column |
| Back | one level out (Capture, States, the description, the selection, the catalog), then out of the view, back to the mode (0.4.3: a click, as everywhere) |

Decisions where the spec left a gap:

- States sit in a mode inside a firmware track, on **Loop** (unused there; Move's Loop is a mode button), laid out as
  Move's Set Overview (32 pads, top-left = 1, Copy / Delete + pad as there). The host names the stored ones with
  `rfm_move_fw_states`.
- The three levels are the library's (with the slots), not the project's; pan, sends, filters and modules are the
  project's (version 5; version 6 keeps 128 values a module). `MIX` carries the level too; a level change alone is `FW_LEVEL`.
- A MIDI FX slot's filter is one track or all; a firmware's filter any set. Live pads are not tagged (`track` 0) and go
  straight to the shown firmware; the sequencer's MIDI has `track` 1..16.
- The sounding rule for `voices`: the active slot always, then the others in slot order while voices are left.
- The view's state (the track shown, Favorites / All, the filter, the catalog's toggles, the FX page) is in the
  settings (version 4).
- The mockup's text drew font5x7's first 7 rows; the core draws its 8-row cell (descenders), and the reference
  (`tests/move_v2_ref.mjs`) is made the same way.
- (0.4.1) Edits made outside the Move (a web page) go in through `rfm_move_fw_set_active`, `_fw_set_level`,
  `rfm_move_mix_set`, `_route_set`, `rfm_move_fx_set_module`, `_fx_set_param`, `_fx_set_page`: the Move's own edit
  (its action, the project's or the library's save), never a write to `rfm_move_t`'s fields.

### One firmware, no FX (0.5.10)

`rfm_move_fw_caps(m, caps)` says what the host offers: `RFM_MOVE_CAPS_ALL` (the default, the Move's RemoteFM14Move)
three slots and FX setup; 0 one firmware and no FX (remotefm1.com, the phone apps). The browser is the same (the
banks, Favorites / All, categories and tags, colours, the description, the catalog, States, the jog's load), loading
into slot 0. Without `RFM_MOVE_CAP_SLOTS`: Tracks 2 and 3 are dark and do nothing,
and Down (FM-1 mode) switches nothing, Track 2 / 3 held + volume sets nothing, the host's slot ids past 0 and a library's
other slots are dropped, voices are 1, and the popups say "Level" and "loaded" without a track. Without
`RFM_MOVE_CAP_FX`: Session never opens FX setup (on a firmware track it leaves the view, as Back; on Track 4 as before
the browser), and no `FX_LOAD`, `FX_PARAM`, `MIX` or `ROUTE` is emitted (`rfm_move_fx_sync` and project loads send
none); the host-edit calls for FX, pan, sends and routing refuse (-1). A project keeps its FX setup untouched, so one
made on the Move loads back there as it was.

## Projects that hold their firmwares; States (0.13.0)

The owner's review of 2026-11-22 ("Project should save firmwares"; cards P.1..P.11, the mock-ups in
`remoteFM14Move/docs/design/mock_projstates_v1.c`). All of it is for a host that sets **`RFM_MOVE_CAP_PROJFW`**
(`rfm_move_fw_caps(m, RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_PROJFW)`; it is not in `RFM_MOVE_CAPS_ALL`). An older host (the
web player, the phone apps, a RemoteFM14Move before it) keeps 0.12.0's behaviour: a project load never touches the
firmwares (a version 9 project's firmware part is written back unchanged when it is saved), States keep the browser's
Loop and its old gestures, a held Track's pad 7 is dark and dead.

**The project (blob version 9)** adds, per firmware slot: the firmware's id ("" empty), its level, its layout,
scale, root, channel, octave and knob CC numbers (`rfm_move_slotset_t`), the sequencer track its Track selects
(`settings.lock`); and once: the slot played, the Drum layout's 32 notes, the velocity mode and level
(`RFM_MOVE_PFW_BYTES`, 176 bytes before the check). Versions 1..8 load as before and "hold no firmwares": the slots,
the levels and these settings stay as they are, and the next save writes version 9 with what is loaded then. The
settings blob and the library blob keep their copies of these values (an older host, an older project). Each one the
user changes now marks the project edited (its autosave), but only while a stored project is open (never at start,
before the host has loaded one). A slot whose firmware was missing when the project loaded keeps the project's wish
(saved again) until the user puts a firmware there or empties it.

**Opening a project** (a pad in the grid, or the host's `rfm_move_project_load` at start): its settings are applied as
the user's own edits (`FW_LEVEL`, `FW_ACTIVE` and the slot's layout played, `SETTINGS`), then the core sends one
`RFM_MOVE_ACT_PROJECT_FW` per slot (slot 0 only without `RFM_MOVE_CAP_SLOTS`). The loading screen is up from the
pad's press (the grid knows the project's firmwares from `rfm_move_project_info`) until every slot has settled: the
project's number at 3x, "Loading" at 2x, a row a slot (its number, the firmware's name or a dotted line for an empty
slot, a dotted bar while waiting, a bar filling 0..99, full when ready). A Track lights in its slot's colour once its
firmware is ready, dark until then. Play pressed meanwhile starts the transport when all are settled (again: not). A
slot with no news for `RFM_MOVE_PFW_WAIT_MS` (20 s, the sequencer's clock) is taken as failed. Then each missing or
failed firmware is said for a second ("Felucca / not installed / Track 3 empty"; "failed"), its slot empty. Another
project's pad pressed meanwhile loads that one (the host is told again). **A new project** (an empty pad): no clips,
the default tempo, saved at once, and its three slots empty (`PROJECT_FW` with "" for each); its routing (the
owner's): tracks 1-4 to firmware 1, 5-8 to firmware 2, 9-12 to firmware 3, 13-16 to none, and in each group of four
the MIDI channels 1..4 (tracks 1, 5, 9, 13 on channel 1 .. 4, 8, 12, 16 on channel 4); the Track buttons' sequencer
tracks 1, 5 and 9; the MIDI FX filters "all" (a `ROUTE` and `SETTINGS`, as the user's edits). Everything else stays
(levels, layouts, velocity). A fresh install's first project is a new one: the host calls `rfm_move_project_dir(m, 0)`
(no project stored) after `rfm_move_fw_caps`. A loaded project, of any version, keeps what it stored. With one slot
(no `RFM_MOVE_CAP_SLOTS`) a new project keeps 0.12.0's routing (all 16 tracks, track n on channel n). (0.14.0) A
**Step 13 on the project grid** (0.14.0, the owner's 2026-11-24): a new project with random firmwares, lit (its
icon and the step) while there is an empty slot: at its press the open project is stored first (as when another opens),
a new one opens in the first empty slot (never the open one's) with up to three different installed firmwares at
random (fewer installed: the rest empty; `PROJECT_FW` with each id, the loading screen until the host answers
`rfm_move_proj_fw`, as a stored project's load), a random scale and root as its key and a new project's routing; it
is stored at once (`PROJECT_SAVE`, the ids in its part even before the host has loaded them); said "New project 5,
random". The page stays the grid. Undo (lit while it can): the new project deleted (`PROJECT_DELETE`) and the one open
before loaded again (`PROJECT_LOAD`). No empty slot: dark, a press says "No empty slot". (0.14.0) A
host starts one from outside with `rfm_move_project_new(m, slot)`: exactly an empty pad's click (the open project
saved first, the new one current and saved, its slots emptied), 0; -1 refused (the slot holds a project or is out of
range, or a load is pending).

**States** (P.6, P.7, P.9): **Track 1..3 held + the top row's pad 7** (a firmware loaded there), or **Loop** in the
firmware browser. A layer over whatever is under it, which it returns to: **Back** (its click: the copy / delete marks
first, then States) shows the layout or view it was opened over (the browser when opened by Loop there; Loop then
closes it too). Opening: the one you are in, "Project 3" at 2x, over the firmware's name for a second (said "States,
ChoralRoot, Project 3") and the pads swept column by column in the slot's colour (50 ms a column, at most 8 LED writes
a frame), settling into the states' lights; a press meanwhile acts at once.

(0.14.0, the owner's 2026-11-24: "a project should keep it's own unique firmware state which then shows up as slot 1,
so slot 1 always means project state, a unique state per project") **Pad 1 is the open project's own sound**: the
project's copy of the firmware, which every project has, which you are in whenever a project is open and which the
host saves as you play. **Pads 2..32 are a library** shared by all projects (the host's state files 1..31). No state is
ever "active" and nothing saves itself into a library pad.

| In States | Does |
|---|---|
| The screen | the firmware's name at 2x (cut to fit; Copy / Delete held: their icon at its right end), the 32 pads in 4 rows of 8: pad 1 filled with a "P" (the project's own sound), a library state framed with its number, a dot an empty one; dashed the copied one and crossed the deleted one while the buffer is armed. A pad's popup ("Project 3 / saved", "State 5 / loaded") stays while that pad is held |
| Pads | the slot's colour (not the firmware's): (0.14.0, the owner's "should be a different contrasting color") **pad 1 white, breathing** (700 ms full, 300 ms at a third), stored library states dim in the slot's colour, empty dark, a pressed one white and steady |
| A pad pressed | (0.14.0, gated as the Projects page's pads) **its info at once, nothing else**: "Project 3 / ChoralRoot", "State 5 / stored", "State 6 / empty" (said so); it stays while the pad is held. **A click** (let go within `RFM_MOVE_TAP_MS`, 300 ms, nothing else pressed meanwhile) acts at the release, as below; held longer: info only, the release nothing |
| Pad 1 clicked | **the project saved now** (`PROJECT_SAVE` with `value` 1, as the open project's click in Projects: the firmwares as they are now); "Project 3 / saved", its pad off, on, off. Nothing while a project loads |
| A library pad clicked | **a stored one: loaded into the project** (`STATE_LOAD`): the firmware becomes that sound, pad 1 stays the one you are in, the library state is never changed by playing; Undo after it. "State 5 / loaded". **An empty one: the sound as it is now stored there** (`STATE_SAVE`); "State 5 / saved" |
| A pad held + Delete | (0.14.0, the owner's "You should be able to reset a state by holding the state and pressing delete") **that state reset** to the firmware as first installed, at Delete's press: a library pad, stored or empty, gets a fresh state (`STATE_NEW` with its index; the sound you are in is untouched); pad 1 as step 14's New (`STATE_NEW`, Undo after it). "State 5 / reset", "Project 3 / reset", the pad red for 250 ms. Delete is lit full while a pad is held. The pad's release then does nothing |
| Copy held + a pad | pad 1: **the sound now copied** into the state buffer (`STATE_COPY` index 0); a stored library state: **copied** (`STATE_COPY`; armed, dashed); an empty one: **the buffer pasted** there (`STATE_PASTE`); a stored one while armed (the last state function a copy or a delete, not the very state just copied): **replaced** by the buffer. So Copy + pad 1, then Copy + a library pad writes the current sound over a stored state or into an empty one. Pad 1 while armed: **the buffer into the project** (`STATE_PASTE` index 0, `value` 1), Undo after it. A paste disarms; Back's click clears the marks |
| Delete held + a pad | a library state: **deleted**, into the buffer (`STATE_DELETE`; armed, crossed until Delete is let go). Delete never pastes. Pad 1: **reset**, as a held pad 1 + Delete. Copy / Delete held first act at the pad's press |
| Copy, Delete alone | nothing (their icon on the screen while held). Nothing loads while either is held. The buffer is one firmware's: another firmware's States do not offer it |
| Step 14 (its icon lit; white at its press) | **New**: the project's sound becomes the firmware as first installed (`STATE_NEW`); no library state is touched. "New state / ChoralRoot". The other steps are dark and dead |
| Undo (lit only while there) | the sound from before the last New (or reset of pad 1), library load or paste onto pad 1 is back (`STATE_UNDO`). "Undo / New state", "Undo / State 5", "Undo / paste". One step, no redo; the next of those three replaces it, leaving States ends it; a save, a copy, a library paste or delete leave it |
| Track 1..3 tap | that slot's States (named again); an empty slot: "Track 3 / empty", States stay where they were |
| Track 4 tap, Menu | States close, then the button does its own job |
| Knobs, jog, Shift, Capture, the arrows, Mute, Sample, Record | dark and dead. Play and the volume knob are the layer's under it |

### The host's part (RemoteFM14Move)

- **The capability**: `rfm_move_fw_caps(m, RFM_MOVE_CAPS_ALL | RFM_MOVE_CAP_PROJFW)` once it can do all of the below.
- **Per-project state files**: the host keeps, for each project slot, a copy of each of its firmwares' memory (a
  firmware's 1 MB flash image). The copy, cut and paste of a project (`PROJECT_COPY` / `_CUT` / `_PASTE`) carry these
  files with the blob; `PROJECT_DELETE` drops them.
- **`RFM_MOVE_ACT_PROJECT_FW`** (slot 0..2, `index` the project slot 0..31, `data` / `len` the firmware id, "" empty):
  first the host writes what each slot's firmware holds now into the copy of the project it last loaded there (the
  outgoing project); then, for this slot: the same firmware already there: only its memory is swapped for this
  project's copy; another firmware: it is loaded with this project's copy (no copy yet: as stored); "": the slot is
  unloaded. It reports with **`rfm_move_proj_fw(m, slot, status)`**: `0..99` while it works (each report restarts the
  20 s limit), `RFM_MOVE_PFW_READY`, `RFM_MOVE_PFW_MISSING` (the firmware is not on the device: the host leaves the
  slot empty, unloading what was there), or `RFM_MOVE_PFW_FAILED` (the same). Nothing is needed for "". As after
  `FW_LOAD`, it also calls `rfm_move_fw_slots` (and `rfm_move_set_takeover` for the slot) and `rfm_move_fw_states`.
- Opening the last project at start goes the same way (`rfm_move_project_load` sends `PROJECT_FW`; the same firmwares
  already loaded: only the memory, or nothing when it is already this project's).

#### States: the host's contract (0.14.0)

The owner's model of 2026-11-24: pad 1 is the open project's own sound, pads 2..32 a library shared by all projects,
and **no state is ever active**. With `RFM_MOVE_CAP_PROJFW`, every `STATE_*` action's `index` k (`slot` the firmware
slot 0..2, `data` / `len` the firmware's id) means:

- **k 0: the open project's own sound**: the project's copy of that firmware (`move-project-NN.fwK.bin` beside the
  project), which the firmware holds while the project is open. There is no state file 0.
- **k 1..31: the library**: the firmware's state file k (`states/<id>/state-KK.bin`), shared by all projects; shown
  on pad k + 1 ("State k + 1").

**The autosave**: the host writes a firmware's memory only into the open project's copy (and `flash.bin`), as today;
**never into a state file**. There is no `st_cur` any more: every state file is written only by `STATE_SAVE`,
`STATE_PASTE` and the host's own page.

**The undo**: one per slot, the firmware's memory (and its working memory) from before the last `STATE_NEW` with
index 255, `STATE_LOAD`, or `STATE_PASTE` with index 0. Each of those replaces it; a firmware change drops it.

**The state buffer**: one, holding a firmware's memory with that firmware's id; the core pastes it only into the same
firmware's States.

| The core sends | The host does |
|---|---|
| `PROJECT_SAVE`, `index` the open project, `value` 1 (pad 1 clicked) | as today's `value` 1: the firmwares as they are now (synced) into the open project's copies, the blob stored |
| `STATE_SAVE` k 1..31 | the memory now (synced, with its working memory) stored as state k; nothing becomes active. Never sent with k 0 |
| `STATE_LOAD` k 1..31 | state k **into the project**: first the memory now (synced) into the project's copy and kept as the undo, then state k into the firmware (a new instance, as today); the project's copy then follows by the autosave (writing it at once is better). Never with k 0 |
| `STATE_DELETE` k 1..31 | as today: the state into the state buffer, then forgotten. Never with k 0 |
| `STATE_COPY` k 0 | the memory now (synced, with its working memory) into the state buffer, with the firmware's id |
| `STATE_COPY` k 1..31 | as today: state k into the state buffer |
| `STATE_PASTE` k 1..31, `value` 0 | as today: the buffer stored as state k (replacing one there); the firmware is not touched |
| `STATE_PASTE` k 0, `value` 1 | the buffer **into the project**: the memory now kept as the undo, then the buffer loaded into the firmware (as a load); the project's copy follows |
| `STATE_NEW` (`index` 255) | as today: the memory kept as the undo, the firmware restarted as first installed; it is the project's sound now (step 14, or pad 1 reset) |
| `STATE_NEW` `index` k 1..31 | a library pad reset: **the firmware as first installed (a fresh memory, no working memory) stored as state k**, replacing one there, the same as the page's "Reset to default" of a state; the firmware is not touched, the undo is not touched; `rfm_move_fw_states` after |
| `STATE_UNDO`, `value` -1, `index` 255 | the undo back into the firmware (its working memory too); the project's copy follows. Nothing kept (or another firmware's): nothing |

**What the host reports**: `rfm_move_fw_states(m, slot, used)` as today (after a firmware loads, a scan, a save, a
paste, a copy, a delete), bit k for library state k, k 1..31; bit 0 is ignored (send it clear).
`rfm_move_fw_state_cur` is ignored with the capability (calling it is harmless; it need not be called).

**A state file at index 0** (made before 0.14.0; the owner has one for ChoralRoot): once, when the host first scans a
firmware's states, it moves `state-00.bin` (and the files beside it: its working memory, its JSON) to the first free
index 1..31 and reports `rfm_move_fw_states` again. No free index: it is left where it is, never shown, never written,
and the host logs it. The core takes any report at any time.

## Engine parameters (0.14.0)

The owner's words (2026-11-24): "add a page after the FM-1 knobs for Engine parameters ... you reach that page, then
click the jogwheel, now you can scroll and select from different pages for the synth engine ... Each page then
supports up to 8 parameters on the knobs." The protocol is a firmware's **ep1** (engine-params SPEC.md: the EP1D
description, the record, `rfm1_ep_set` / `rfm1_ep_watch`); the design is engine-params DESIGN.md. Stages 1 and 2 of
its build plan are here: the page, its list, the knobs, an engine changed under the page, a layer off. Not yet:
engine locks (stage 3), the Layer setting (stage 4: every track is "Auto", `ep.lset` the seam), the project blob.

**The page.** ENGINE is the track view's third knob page: TRIG 0, FM-1 1, ENGINE 2, banks 1..4 at 3..6
(`rfmv_seq_kpage`; `seq.eng`). It is there only when the selected track's **target** firmware has ep1: the slot
played if the track reaches it (its routing) and that slot has ep1, else the lowest slot it reaches that has
(`rfmv_ep_target`); absent, the jog goes FM-1 -> bank 1 and the strip has six cells, present seven. Its **layer**:
Auto, the layer whose MIDI channel (the record's 0..15) is the track's channel (the core's 0..15: track 2 plays
channel 2, 1 here, ChoralRoot's "MIDI Bass 2"), several the lowest, none layer 0 (`rfmv_ep_layer_of`).

| Gesture | What |
|---|---|
| arriving (the jog onto it, a track selected that was left on it, another slot or layer for the track) | a second: the layer and its engine small at the top left ("Chord  VA"), the page's name big ("FILTER"), the strip; a layer off: "Bass" / "Off". Said "Engine, Filter" |
| the knobs | knob k the page's k-th parameter, lit in the colour of the slot it controls (orange, blue, yellow), dim to full by its value; white while touched; nothing on it or a layer off: dark, dead |
| a touch | the name (2x), the value (4x, 2x when wider), a bar from the range (BIPOLAR from the middle), the strip; full screen. Said "Cutoff, 1.2k" |
| a turn | the value by the parameter's step a tick (0: the range over ~128 ticks); Shift held: one stored step (fine); a LIST one item every `RFM_MOVE_LIST_TICKS`. Sent absolutely, `RFM_MOVE_ACT_EP` SET, merged to one per (slot, layer, pid) per `rfm_move_seq_advance` (the latest value, at frame 0); the value sent is shown until the firmware's values agree (or `RFMV_EP_OVR_MS`, 500 ms) |
| the jog's click (at its release: no double click there) | the **page list** over the whole screen: "Chord  VA" on a title line, the engine's pages three rows at 2x, the page shown inverted, a scroll mark. Said "Pages, Filter", then each page highlighted |
| the jog, the list open | the highlight a page a detent, no wrap; the knobs follow it at once (a knob turned edits that page, its popup over the list a second) |
| the click again, or Back | the list closes on the page highlighted |
| a held step, Delete, Copy, Loop, Mute (and Shift's own jog) over the list | theirs while held, as over any menu; the list back at their release. Pads, steps and buttons keep working |
| Shift + click; Delete held + click | nothing (stage 3: Delete + click clears the clip's engine locks); the list does not open |
| a step held | the knobs are its TRIG values, as on the FM-1 page (stage 3: its engine locks) |
| the layer's engine changes under the page | a second: the layer small, the new engine big ("Chord" / "FM6"); then the new engine's page with the same name, else the one last shown on that engine (RAM), else its first; the list (open) follows |
| the description replaced | the page found again by (engine id, page id) |
| the slot emptied (`describe(NULL)`), a refused description, the routing away | the page goes at the core's next input or block: the track on FM-1; a description given again in the same go keeps it |

A DYN parameter takes its name and words from the watched cells when the cell is that pid and the value shown is the
firmware's own; else its format. Every value's words are the core's own from the description (INT, FIXED(d), TABLE,
NOTE(o) with sharps, PERCENT, ONOFF; BIPOLAR signs; a unit after one space, "%" after none).

**The host's part.** Per slot, after each of its frames: desc_seq moved: `rfm_move_ep_describe(m, slot, blob, len)`
(checked: `rfm_move_ep_check`; refused: no Engine page, `< 0`), then `rfm_move_ep_layers` and `rfm_move_ep_values`
for every layer (a description taken resets them; the page shows only once the layers are given); lay_seq:
`rfm_move_ep_layers`; val_seq: `rfm_move_ep_values` for each layer; cell_seq: `rfm_move_ep_cells`. A slot loaded,
unloaded or replaced: `rfm_move_ep_describe(m, slot, NULL, 0)` first. These calls may themselves send
`RFM_MOVE_ACT_EP_WATCH` (the page watched follows). The actions:

| Action | Fields | When |
|---|---|---|
| `RFM_MOVE_ACT_EP` (38) | `slot`, `op` (`RFM_MOVE_EP_SET` 1), `layer`, `pid`, `value`, `frame` 0 | from `rfm_move_seq_advance` (first thing in it): the knobs' turns since the last call, one a parameter. The host queues `rfm1_ep_set(op, layer, pid, value)` for the slot at the block's first frame |
| `RFM_MOVE_ACT_EP_WATCH` (39) | `slot`, `layer`, `value` the page (an index into the description's pages; -1 none) | the page shown, the list's highlight, the selected track, the layer or its engine changed; -1 (to the slot watched before) when no Engine page shows (another knob page, session view, another mode, a layer off). The host calls `rfm1_ep_watch(layer, page)` |

`rfm_move_t` grows by `rfm_move_ep_t` (296 840 bytes: each slot's 64 KiB description and 32 KiB of values), `rfm_move_act_t` by 8 bytes (64 -> 72: `pid`, `layer`, `op`). Tests:
`move_engine`.

## The sampler (0.15.0)

The owner's words (2026-11-24): "I'd like to be able to think of and pull up the sampler as an app. So you pull up
the sampler, it stays on screen, so takeover of screen, jogwheel, and back button." The host's recorder
(RemoteFM14Move: sources, modes, files) with the Move as its face; the mock-up is remoteFM14Move-recorder
docs/design/mock_recorder_v4.c (boards one, three, tabs, popup, run; v1's captured), and the screens here are its
pixels. Only for a host that sets `RFM_MOVE_CAP_SAMPLER` (`rfm_move_fw_caps`; not in `RFM_MOVE_CAPS_ALL`). Without it
nothing changes: Sample is the audio loop (the web player's). With it the audio loop is off and Sample is the app.

**A layer.** The app takes the screen, the jog (turn, click, touch) and Back, and Sample (its record key); the pads,
steps, knobs, arrows, Play and the rest stay the layer's under it (they work and light as before). A held control
(a Track, a step, Shift / Loop / Copy / Delete / Mute, FILL) takes it over while held, its screen and the jog; a popup
of the layer under it newer than the app's last change (a knob touched or turned: R5) shows over it, a jog turn brings
the app back. A Shift menu opened over it hides it until Back; Sample with a Shift menu open closes the menu (the app
takes its place). Global settings, States and the projects view are above it (the browser keeps Sample: the
favourite key). A takeover that claims the buttons keeps Sample; a takeover is not shown while the app is open (as
under a Shift menu: its unclaimed parts are the FM-1 layout's). Tabs and the row are remembered (RAM), open or closed.

| Gesture | What |
|---|---|
| Sample (a note layout, MIDI) | the app opens at the press (white); in the app: the record key, `RFM_MOVE_ACT_SMP_PRESS` at the press (value the mode), nothing at the release |
| Sample (the FM-1 layout) | the FM-1's SEL, as before, while the app is closed and no take runs; open (or a take running): the app's |
| Track 1..3 held + Sample | the app with that slot as the source (`SMP_SET` Source; the jog on the Source row); Track 4: the master. Nothing records. The Track's hold is used up (as Track + Menu): no menu, no tap. In every layout |
| Back | a popup or the tab choice ends; else the app closes. A take goes on (Sample red); Sample brings the app back and, when the take is recording, is the record key at that same press (the owner's 2026-11-24: one press ends it; armed and waiting: only shown) |
| the jog | a row a detent (no wrap); three rows for 1 s after the last detent (the chosen in the middle where it can be), then one. Up past the first row: the tabs (board "tabs"), down again: the first row. A take running: nothing |
| the jog's click, a row | its popup (board "popup"): the name (2x), the value (4x, 2x when wider), under Threshold and Input level the level now as a line and the threshold's mark. The jog changes the value: `SMP_SET` at once, the settings saved (`SETTINGS`). A click or Back ends it |
| the jog's click, the tabs | the jog picks a tab (offered and built: Record, Takes, Tools), a click or Back ends it |

**The screens** (128 x 64). At rest (board one): the tabs as icons (the shown one underlined; the jog on them: a white
block), the source's sign top right (M, 1, 2, 3, the input's icon, S; L, K), the source's live wave (58 columns 2 px
apart, the newest at the right), the level meter (-60..0 dB, 28 px) with the threshold's mark, a mark a row (the
chosen long), the chosen row: its icon and value at 2x, inverted. Turning (board three): the tabs line, the level as a
line on top with the threshold's mark, three rows. Armed (Bars, Threshold): the record dot blinking, "0/4" (no bars: the
time), the sign, the bar lines, the strip, the level (Threshold: its mark), no wave. Recording (board run): the dot,
the bar of the bars ("2/4") or the time ("1:13"), the take's wave, the bar lines (up to 16 bars), the level, the strip
(the bar thick, where it is). A message a second (v1's captured): "Captured" / "4 Bars" (Capture mode; else "Saved"),
a take with no bars its seconds ("7.25 s"), "Nothing" / "to capture", an error's words (two rows of 10). A message while
the app is closed is said, not shown. No other animation.

| Row (`RFM_MOVE_SMP_*`) | Values (`SMP_SET`'s value) | Default |
|---|---|---|
| Source (0) | Master 0, Slot 1..3 1..3, Input 4, Stems 5, Looper 6, Kit 7 (only those offered) | Master |
| Mode (1) | Capture 0, Bars 1, Threshold 2, Manual 3 | Bars |
| Length (2) | Open, 1, 2, 4, 8, 16, 32 bars (the bars; 0 Open, "Auto" in Capture mode) | Open |
| Threshold (3) | -60..0 dB, 1 a detent | -24 |
| Input level (4) | -24..+24 dB ("In Level" in its popup: the full name is wider than the screen) | 0 |
| Monitor (5) | Off 0, On 1 | Off |
| To (6) | Files 0, Looper 1, Kit 2 (only those offered) | Files |

Saved with the settings (`settings.smp[7]`, version 8: 137 bytes; a version 7 image, 130 bytes, loads with these
defaults; each byte checked, a bad one its default).

**Sample's light.** White while the app is open and idle; red blinking while armed; red recording or saving (open or
closed); green 250 ms when a take is saved (`RFM_MOVE_SMP_FLASH_MS`); closed and idle: dim white (40) while Capture
listens, else faint (12) (it opens the app: R1). The FM-1 layout, closed and idle: SEL's light. The projects view, a
takeover with the buttons, Global settings: theirs. Back's light is on while the app shows.

**The host's part.** `rfm_move_fw_caps(m, ... | RFM_MOVE_CAP_SAMPLER)`: every setting's `SMP_SET` once (also
`rfm_move_smp_sync` after settings the host loaded itself). A host with a face of its own for the settings (its web
page's Sampler window) changes one with `rfm_move_smp_set(m, setting, value)`, the value as `SMP_SET` sends it (Length:
the bars): as the popup's jog, the app shows it at once, `SMP_SET` comes back and the settings are saved; 1 changed
(draw again), 0 the same, -1 refused (no capability, not offered, out of range, a take armed or running).
`rfm_move_smp_offer(m, sources, settings, tabs, dests)`:
bits by `RFM_MOVE_SMP_SRC_*`, `RFM_MOVE_SMP_*` (the rows), `RFM_MOVE_SMP_TAB_*` (Record 0, Takes 1, Tools 2, Loops 3;
only `RFM_MOVE_SMP_TABS_BUILT` show), `RFM_MOVE_SMP_TO_*`; 0 leaves one; defaults 0x3F, 0x7F, 1, 1. A value no longer
offered moves to the first offered (`SMP_SET`). `rfm_move_smp_status(m, &st)` as often as it likes (about 30 a
second; 1: redraw): `state` (`RFM_MOVE_SMP_ST_IDLE`, `_ARMED`, `_RECORDING`, `_SAVING`), `listening` (Capture's buffer
fills), `bar` (1..`bars`, 0 armed) and `bars` (0: none, the time from `frames` / `rate`), `level` 0..32767, `npeaks`
peaks 0..255 oldest first (at most 128) and `span` (0: a rolling history, the newest at the right; n: a take with bars,
its whole length n peaks: column c shows peak c * n / 58, done = npeaks / n), `takes`. `rfm_move_smp_event(m, kind,
bars, bpm10, ms, reason)`: `RFM_MOVE_SMP_EV_SAVED`, `_NOTHING`, `_ERROR`. The core times nothing of a take. The song's
position for the host's bar lines: `rfm_move_seq_pos(m, &per_frame)`, in ticks (96 a beat) at the next
`rfm_move_seq_advance` call's first frame, to a fraction of a tick (-1 stopped); less `frames * per_frame`: the call's
own first frame.

| Action | Fields | When |
|---|---|---|
| `RFM_MOVE_ACT_SMP_SET` (48) | `index` the setting, `value` its value (Length: the bars, 0 Open) | each change (one turn: one, its last value), each setting once at the capability or `rfm_move_smp_sync`, an offer that moves one |
| `RFM_MOVE_ACT_SMP_PRESS` (49) | `down` 1, `value` the mode | Sample pressed in the app (not when it opens or comes back) |

`rfm_move_t` grows by `rfm_move_smp_t`, `rfm_move_settings_t` by 7 bytes. Tests: `move_sampler` (dumps
`sampler_*.pbm` / `.leds`; the boards against the mock-up's, `tests/move_smp_ref.h`).

### The takes (stage 2)

The owner's 2026-11-24 decisions: "better to keep as minimal text as possible, use icons. Use text popups while
adjusting values." Tabs Record, Takes, Tools (Loops later), remembered open or closed; a tab "can take over knobs and
arrows". Only for a host that offers the Takes or Tools tab (`rfm_move_smp_offer`, tabs 0x7); a host that offers only
Record sees stage 1 unchanged. The boards are mock_recorder_v4.c's takes, days, act, trim.

| Screen | What shows | What it takes (else the layer's under it) |
|---|---|---|
| Takes | the tabs line; three rows of one list of every take (the owner's 2026-11-25: "scroll through everything I've recorded, newest at the top"), the newest on top: the source's sign (M, 1, 2, 3, the input's icon, S; "?" not known), the length ("4 Bars", "7.25 s"), the take's picture (14 peaks); the chosen row inverted. No takes: "No takes" | the jog, Delete (held: the next click deletes), Undo, Sample (plays the chosen take) |
| a take's actions (the click) | its whole wave on top, a row of icons Play, Trim, Normalize, Reverse, Fade In, Fade Out, Delete (the owner's 2026-11-25: the full editor on any take; seven icons 17 px apart; a stems take: Play, Delete), the chosen one's name the only word | the jog, Undo, Sample (plays it) |
| Trim | the chosen marker's time top left ("1.84 s", to the ms with Shift), the wave with the part kept a white block, icons start, end, play, keep; while a marker moves the view zooms on it | the jog, knobs 1 (start) and 2 (end) with their touch, the other knobs dark and dead (R3), Shift (fine), Undo |
| Tools | the chosen take's wave, icons Trim, Normalize, Reverse, Fade In, Fade Out, the chosen one's name; the jog on the tabs: the tabs line in the wave's place; a stems take: "Stems", no tools | the jog, Undo |
| a Fade's popup | "Fade In" / "100 ms" (5, 10, 20, 50, 100, 200, 500 ms, 1, 2, 5 s; remembered) | the jog |

The pads and steps play under all of them; the knobs too but in Trim. A held Track, step or modifier the app did not
take covers it as before (R3).

| Gesture | What |
|---|---|
| the jog in Takes | a row a detent (no wrap); the chosen in the middle of three where it can be; up past the first: the tabs |
| the click on a take | its actions, Play chosen (two clicks play a take) |
| Delete held + the click | the take deleted (`SMP_EDIT` DELETE), "Deleted"; Undo brings it back |
| Play (the click) | `SMP_PLAY` value 1, the whole take (a stems take: its Master); again: value 0 (stop). A line moves over the wave at the host's `play_pos`. Leaving the screen (Back, the app closed): stopped |
| Trim (the click) | Trim on it: the start 0, the end the take's length, Play chosen (the knobs are the markers') |
| Sample outside Record | (the owner's 2026-11-25) Takes, Tools, a take's actions, Trim: `SMP_PLAY` of the take chosen or edited (Trim: the part kept); again: stop. No `SMP_PRESS` there; a take armed or recording: the record key as ever |
| Back in Takes / Tools | (the owner's 2026-11-25) from the tab's content: the tabs, the jog choosing one at once (the list's chosen take becomes the tools'); on the tabs, and anywhere in Record: the app closes. A take's actions, Trim, a popup: one level back first |
| Delete (the click) | as Delete held + the click; the list again |
| Trim: the jog | the icon; its click on Start / End: the jog moves that marker (zoomed, the icon underlined) until a click or Back |
| Trim: knob 1 / 2 | the start / the end, a detent a step; touched or turned: zoomed on it until a second after; the jog's icon stays where it is (the owner's 2026-11-25) |
| Trim: steps | 1/256 of the take a detent; Shift held: 1/16 of that. The zoomed view is 64 steps (the wave's 64 columns), centred on the marker (at the take's ends: against them). The markers keep 64 frames between them |
| Trim: Play | the part kept, once (`SMP_PLAY` from the start, its frames); again: stop |
| Trim: Keep | `SMP_EDIT` TRIM (from the start, the frames kept), "Trimmed", back where Trim came from; nothing cut: back, nothing sent |
| Trim: Back | a marker moving: it stops; else the take as it was (nothing sent), back where Trim came from |
| Tools: Trim | Trim on the take; Back: Tools |
| Tools: Normalize, Reverse | `SMP_EDIT` at the click, "Normalized" / "Reversed" |
| Tools: Fade In / Out | the popup; the jog its length; the click `SMP_EDIT` FADE_IN / FADE_OUT (value ms), "Faded In"; Back: not done |
| Undo | `SMP_UNDO` when the host's status says there is one (lit), "Undo"; else dark, dead. The Record tab: the layer's |

**The take Tools works on** is the take chosen in Takes: the last whose actions opened (a click, a preview, Trim);
none yet (or it was deleted): the day's newest. (The jog reaches the tabs only from the list's first row, so the row
the jog is on cannot be the measure.)

**After a take is saved** (`rfm_move_smp_saved(m, &row, mode)`, after `rfm_move_smp_event`'s SAVED), with the app
open: Capture shows the take's actions with Play chosen and plays it once by itself (`SMP_PLAY`) only when the song
is stopped (owner: "should launch to a preview afterwards since I don't know what it recorded"); Manual and Threshold
open Trim on it (a stems take: nothing, it has no Trim in this stage); Bars nothing. A new take armed or recording
by then: nothing comes over it (a status still saying SAVING does not stop it). They come over the tab shown (the save's message a second first): Back (or Keep) goes
back to it, not to the list. The list moves to the take (its day, the newest chosen). The app closed: nothing (the
list follows). Track 1..3 / 4 held + Sample shows the Record tab (the source is its row).

**The host's part.** The core asks and never waits; what has not come is drawn empty (a row's block, no wave) and
filled as it arrives:

| Action | Fields | Answer |
|---|---|---|
| `RFM_MOVE_ACT_SMP_ROWS` (50) | `take` the folder (0 the days, `RFM_MOVE_SMP_NEWEST` the newest day with takes, a day's id), `from` the first row, `count` (3) | `rfm_move_smp_rows(m, asked, folder, total, from, n, rows)`: `asked` the act's `take`, `folder` the day answered (0 and `total` 0: no takes) |
| `RFM_MOVE_ACT_SMP_PEAKS` (51) | `take`, frames `from`, `count`, `value` the columns (64) | `rfm_move_smp_peaks(m, take, from, count, n, peaks)`; a later ask may replace one not answered (the core draws an older view's peaks where they cover a column until its own come) |
| `RFM_MOVE_ACT_SMP_PLAY` (52) | `take`, `value` 1 / 0, `from`, `count` (0: to its end) | `rfm_move_smp_status`'s `play_take` / `play_pos` (0: none) |
| `RFM_MOVE_ACT_SMP_EDIT` (53) | `take`, `index` `RFM_MOVE_SMP_OP_DELETE` / `_TRIM` (`from`, `count` kept) / `_NORMALIZE` / `_REVERSE` / `_FADE_IN` / `_FADE_OUT` (`value` ms) | `rfm_move_smp_done(m, op, ok, row)`: the take as it is now (NULL after a delete); not ok: "Not done" |
| `RFM_MOVE_ACT_SMP_UNDO` (54) | (nothing) | `rfm_move_smp_done(m, RFM_MOVE_SMP_OP_UNDO, ok, row)`; status `undo` says when there is one |

A row (`rfm_move_smp_row_t`): `id` (the host's, never 0, the same while the file is: an edit keeps it), `kind`
(`RFM_MOVE_SMP_ROW_DAY`, `_TAKE`, `_STEMS`), a day's `day` / `month` / `today` / `takes`, a take's `src`
(`RFM_MOVE_SMP_SRC_*`, `_NONE`), `bars`, `bpm10`, `frames`, `rate`, `folder` (its day's id), `thumb[14]`. The core keeps
8 rows (`RFM_MOVE_SMP_CACHE`), asks a window once until it is answered or the list changes; `rfm_move_smp_changed`
(the list changed outside: a take saved, a file changed) and every `_done` forget them and ask again. The new act
fields `take`, `from`, `count` are at the end of `rfm_move_act_t`; the status grows by `play_take`, `play_pos`, `undo`.
Words: the row ("Slot 2, 4 bars", "Today", "23 November"), the icon's name, "Start, 1.840 seconds", "Fade in: 100
ms", the messages. Tests: `move_sampler` 22..30 (dumps `sampler_takes*`, `days`, `act*`, `trim*`, `tools`, `fade`,
`preview`, `auto_trim`, `no_takes`, `deleted`, `normalized`, `tabs3`; the boards against `tests/move_smp_ref2.h`: takes
and days but the third tab's icon (the mock draws two tabs), act but the row's last three places (the mock's five
icons: "to a kit pad", "to the looper" come between Trim and Delete when they are built), trim exactly).

## The kit (stage 3a)

The owner's 2026-11-25 kit round (remoteFM14Move-kit `docs/design/KIT-SPEC.md`, cards `kit_*`: "Please build it with
your best guesses"; the screens are `mock_kit_v1.c`'s boards). Sixteen sampled pads on Track 4's third view. Only for
a host that sets `RFM_MOVE_CAP_KIT` (`rfm_move_fw_caps`; not in `RFM_MOVE_CAPS_ALL`); without it nothing changes. Stage
3a is playing: the pads, their sounds from Takes and Samples, a pad held + Sample, pitch, reverse, start and end, the
play modes with the envelope, choke, level and pan, the right half, tracks 13..16, kept in the project, the sampler's
To Kit. Stretch, the sends, the Mixer's Kit strip, whole folders, saving and loading kits, the sampler's Kit source and
destination are stage 3b (below); mixing, joining, slicing and a pad's own edits are stage 3c (below).

**Where it lives.** Track 4's tap goes round Mixer, Routing, Kit (a double click: the projects); held: pad 1 the
Mixer, 2 Routing, 3 the Kit, 4 the Looper (not built: dark and dead), 5 the projects. From another Track, Track 4
opens the view it was left on (`settings.t4_kit`, version 9). The Kit view is the sequencer's track view of the chosen
pad's track with the kit's own pads, steps, knobs, jog, screen and Back: it enters the Sequencer (a slot in the FM-1
layout too; Track 4 onto the Mixer gives the slot's layout back), Menu flips to session view and back to the kit, a
Track 1..3 tap leaves it for that slot's layout. Play, Record, Capture, Loop, Copy, the arrows, Undo, Shift + a step
stay the sequencer's; Up and Down are dark and dead.

| Control | What |
|---|---|
| pads, left 4 x 4 | the kit: pad 1 bottom left, then left to right, rows upward. A press plays the pad (the Velocity setting) and chooses it (its track selected: pads 1-4 track 13 .. 13-16 track 16); Shift + a pad: chosen, silent |
| pads, right 4 x 4 | the chosen pad's pitches: Pitches (16 semitones up from its own, pitch pad 1 bottom left; its own and its octave bright in its row's colour), In Key (the track's key and scale, rows a fourth apart, four scale notes a row, roots bright), Off (dark, dead) |
| steps | the chosen pad's notes on its track (the page shown): a click toggles its own note at the release; a step held + pads: their notes in or out of it (the click then nothing); Delete + a step: the chosen pad's notes there |
| Delete held + a pad | its notes in its track's clip go first; a pad with none is emptied (the file stays). Undo brings either back (the pad's own undo while no sequencer edit came after it) |
| Mute held + a pad | the pad muted (grey), saved with the kit |
| a pad held + Sample | records into that pad with the sampler's own settings (`KIT_REC`) until the pad is let go (or the length, or the threshold): the pad red, Sample red, the kit's screen the take (boards L armed, M recording) |
| knobs | the chosen pad's, lit pink: 1 Pitch (a semitone; Shift: a cent), 2 Start, 3 Stretch (3b), 4 Play mode, 5 Attack, 6 Release, 7 Level, 8 Send 1 (3b). Touch: the popup, its name and value; turn: changed |
| the jog | the tab's rows: one at rest (under the pad's wave and the steps it lasts), three for a second while it turns; up past the first: the tabs (click, turn, click). A row's click: its popup (the jog its value; Shift fine). Back: a popup, the tools, a folder, the tabs; never out of the view |

**The tabs.** Pad (I_PAD; the chosen pad's number at 2x top right): Sound (its click: Play, Delete), Pitch, Start,
End (seconds), Play (One shot, Momentary, Loop; its popup draws the three and the envelope), Attack, Hold, Release (ms
steps 0, 1, 2, 3, 5 .. 10000; the popup draws the AHR, the part turned thick), Choke (Off, groups 1..4; the popup
lights the group's pads), Reverse, Level (-inf..+6 dB), Pan. Kit (I_KIT): the right half (Pitches, In Key, Off). Files
(I_DIR): Takes (every take, newest first) and Samples (the Move's `UserLibrary/Samples`, folders and WAV / AIFF): a
sound plays as the jog lands on it (`KIT_PREVIEW`), its click loads it onto the chosen pad (`KIT_LOAD`), a pad pressed
chooses the target (blinking white). From the sampler, a take's actions gain To Kit (before Delete): the app closes on
the Kit view with the pads blinking, the next pad press takes the take.

**How a pad plays** (the host's voices): One shot plays start..end to its end (Hold 0: the release is a tail ending
there; Hold > 0: the release starts after attack + hold), Momentary while held, Loop round start..end while held; a
choke group's pad stops the others of its group (a short fade); a pad again takes its own voice back.

**The right half's pitches on the steps.** A kit track's note says the pad and its pitch: the pad's own note is
`36 + pad` (pads 1..16: 36..51, the Move's drum notes) on its track's channel; a pitch `s` (1..31 semitones) is the
track's `n`-th other note, `n = (pad % 4) * 31 + s - 1`, counted over 0..127 without the track's four own notes
(`rfm_move_kit_note`, `rfm_move_kit_pad_of`). Recorded or step-entered, a step keeps the pitch through a save and a
load; the sequencer's notes of a track routed to the kit come to the host as `KIT_NOTE` (pad, semitones).

**Tracks 13..16.** A new project routes them to the kit (and to no firmware); an older project keeps its routing and
gets an empty kit. A firmware Track held shows a track the kit plays in its row's colour (13 pink, 14 orange, 15 light
blue, 16 violet); a hold there routes it to the firmware, again back to the kit.

**The lights.** Each kit row its track's colour: a pad with a sound dim, bright while it sounds, the chosen one white,
empty dark, muted grey, recorded into red. Track 4 pink; the knobs pink (3 and 8 dark); Sample dim white (its job: a
pad held + Sample).

**The host's part.** `rfm_move_fw_caps(m, ... | RFM_MOVE_CAP_KIT)`: every pad's `KIT_LOAD` and `KIT_SET`s once (and
`rfm_move_kit_sync`).

| Action | Fields | Answer |
|---|---|---|
| `RFM_MOVE_ACT_KIT_NOTE` (64) | `index` pad, `down` / `vel` (0: let go), `value` semitones, `track` 1..16, `frame` | (the voices) |
| `RFM_MOVE_ACT_KIT_SET` (65) | `index` pad, `param` `RFM_MOVE_KIT_P_*`, `value` | |
| `RFM_MOVE_ACT_KIT_LOAD` (66) | `index` pad; `data` / `len` a path, or `take` a row's id and `value` its place; neither: emptied | `rfm_move_kit_loaded(m, pad, status, name, path, frames, rate, n, peaks)` (`RFM_MOVE_KIT_OK`, `_TOO_LONG`: past 64 MB, `_FAILED`, `_MISSING`) |
| `RFM_MOVE_ACT_KIT_ROWS` (67) | `value` place, `take` folder (0 its top), `from`, `count` | `rfm_move_kit_rows(m, place, folder, total, from, n, rows)` |
| `RFM_MOVE_ACT_KIT_PREVIEW` (68) | `value` place, `take` row id, `down` 1 / 0 | |
| `RFM_MOVE_ACT_KIT_REC` (69) | `index` pad, `down` 1 (`value` the sampler's mode) / 0 the pad let go | `rfm_move_smp_status` as ever, then `rfm_move_kit_loaded` with the take |

The project keeps the kit (version 11, `RFM_MOVE_KIT_BYTES` at its end: the right half, the routing, the chosen pad,
each pad's path and twelve settings); a project load sends every pad's `KIT_LOAD` (its path) and `KIT_SET`s. Tests:
`move_kit` (dumps `kit_*.pbm` / `.leds`; the boards against `tests/move_kit_ref.h`, packed from `mock_kit_v1.c`'s).

## The kit (stage 3b)

Shaping and sharing, from the same round's picks (KIT-SPEC.md "Stage 3b"; boards D, H, K, Q, R, U).

**Stretch.** Knob 3 (and the row after Pitch): Off, 1..64 steps. The pad's start..end is repitched to last exactly
that many steps at the tempo and its track's rate (`KIT_SET` of `RFM_MOVE_KIT_P_STRETCH` carries `from`, the
microseconds); the core sends it again whenever the tempo or a kit track's rate changes, so the host re-fits it. Pitch
is ignored meanwhile (its popup and row read "Stretch"; a turn keeps the old value for when Stretch is Off). While
Pitch, Stretch, Start or End change (a knob touched or turned, a row's popup) the popup draws the 16 steps with the
length as a bar and the step keys light the steps it lasts in the pad's row colour, the first brightest (board D).

**The sends.** Every pad has Send 1, 2, 3 (0..100 %; knob 8 Send 1, rows Send 1..3; the popup a bar, board H) to the
three audio FX of FX setup. The kit is a fourth strip in the Mixer: knob 4 (lit) sets its level, pan or send as the
right 4 x 4 chose, pad column 4 its meter (pink), the screen four strips ("Kit" the fourth). In Routing the kit has a
pad of its own (column 4, top row, pink): chosen, knobs 1..5 are its level, pan, sends, the step keys 13..16 its tracks
(on: the kit's alone, off the firmwares; 1..12 dark). `MIX` with slot `RFM_MOVE_KIT_SLOT` (3) gives the host the
strip (level 0..127, 100 at first; pan 64 the centre; sends 0..127); a pad's own sends add to the strip's.

**A whole folder.** Shift + the jog's click on a folder in Files: its actions, Open (I_DIR) and All To Pads (I_KIT,
chosen; the pads lit in their rows' colours, board K). All To Pads empties every pad to its defaults and sends
`KIT_FOLDER`; the host answers each pad, a sound (the folder's first 16 in name order onto pads 1..16) or
`RFM_MOVE_KIT_EMPTY`. One Undo brings the kit back (pads, their settings, the right half, the name).

**Kits.** The Kit tab's rows: Load (I_DIR: Files' Kits), Save (I_SAVE), the right half (I_KEYS); the kit's name top
right (board Q). Save sends `KIT_SAVE` ("Saving"); the host copies the sounds into
`Samples/RemoteFM1/Kits/<name>/` with a `kit.json`, names it Kit 1, Kit 2 .., and answers `rfm_move_kit_saved`
("Saved / Kit 3", board R). Files' third place is Kits: a saved kit a row (`RFM_MOVE_KIT_ROW_KIT`, I_KIT, no preview);
its click sends `KIT_OPEN` and the host answers `rfm_move_kit_apply` (every pad's path and settings, the layout, the
strip, the name: one Undo brings the kit before back). The project keeps its own kit whether or not it was saved.

**The sampler.** To Kit (offered by the host) reads "Kit Pad 8": the next empty pad, lit white over the Kit view
while the app is open (board U); the host takes it with `rfm_move_kit_next_empty(m, 1)` when the take is saved and
puts the take there (`rfm_move_kit_loaded`: a sound the host put on a pad gets its whole region). Source Kit is the
host's (the kit's output alone).

| Action | Fields | Answer |
|---|---|---|
| `RFM_MOVE_ACT_KIT_SAVE` (70) | (the kit: `rfm_move_kit_path`, `_value`, `_layout`, `_mix`) | `rfm_move_kit_saved(m, ok, name)` |
| `RFM_MOVE_ACT_KIT_OPEN` (71) | `take` a Kits row's id | `rfm_move_kit_apply(m, name, layout, mix, paths, v)` (paths NULL: failed) |
| `RFM_MOVE_ACT_KIT_FOLDER` (72) | `value` the place, `take` the folder's id | `rfm_move_kit_loaded` for each pad (`_OK` or `_EMPTY`) |
| `RFM_MOVE_ACT_MIX` slot 3 | `mix[]` the kit's level, pan, sends 1..3 | |

The project is version 12 (`RFM_MOVE_KIT_BYTES` 3112: the layout, the routing, the chosen pad, the strip, the name,
16 settings a pad: Stretch and Send 1..3 after Mute); version 11 loads (Stretch Off, no sends, the strip at its
defaults, no name). Tests: `move_kit` 18..24 (Stretch and its refit, the sends, the Mixer and Routing's kit, a folder
and its Undo, Save / Load / apply / Undo, the sampler's To Kit, the version 12 and 11 projects) and boards D, H, K
(below the stand-in wave), R and U (the sampler's own level line masked).

## The kit (stage 3c)

Making sounds, from the same round's picks (KIT-SPEC.md "Stage 3c"; boards N, O, P, V). Every new file is the host's,
written on its file thread; the core asks and draws what comes.

**Mix and join.** Copy held, a pad, then another: the first blinks, the second is white and the screen offers Copy
(I_COPY), Mix (I_MIX), Join (I_JOIN) with the two pads' numbers and an arrow (board N); the jog chooses, the click
does it. Copy puts the first pad's sound and settings on the second (its `KIT_LOAD` and `KIT_SET`s, no new file). Mix
and Join send `KIT_MERGE`: the host makes a new file of the two regions (Mix summed and normalized not to clip, Join
the first then the second) and answers with `rfm_move_kit_loaded` on the second pad; the Pad tab shows it with its
Sound row ("Join 2+5") and a join's seam dotted (board O). Mix and Join want a sound on both pads, Copy on the first
("Empty" else). Copy let go before the click, or Back: nothing done. Undo brings the second pad back.

**A pad's own edits.** The Sound row's click: Play, Trim, Normalize, Reverse, Delete (board V; the knobs and the right
half dark and dead meanwhile). Trim, Normalize and Reverse send `KIT_EDIT`; the host's new file is the pad's own copy,
the file it came from untouched. Trim gives the file cut to the region (then the region whole), Normalize keeps the
region, Reverse mirrors it. Undo brings the pad back (its file and settings); a refused edit keeps the sound.

**Slicing a take.** A take's To Kit opens the Kit view with the pads blinking and two icons, To Pad (I_PAD: the click
puts the take on the chosen pad; a pad's press on that pad) and Slice (I_SLICE). Slice sends `KIT_SLICE`
(`RFM_MOVE_KIT_SL_ASK`, the take, the method, a step's microseconds); the host answers `rfm_move_kit_slices` with the
take's file, its cuts and its wave. The screen (board P): the wave with the cuts dotted, the chosen slice a white
block, the method row (Hits the default, Equal 16 / 8 / 4, Steps: a slice a step at the tempo and the first pad's
track's rate; the jog changes it and the cuts are asked again at once) and the pads they go to ("Pads 1-16"). The
pads light the slices in the take's source colour (a slot's, else the kit's pink) and a press plays one
(`RFM_MOVE_KIT_SL_PLAY`, the preview path) and chooses it; knobs 1 (a column of the wave) and 2 (a millisecond; Shift
on 1 too) move the chosen slice's cut (10 ms from its neighbours at least; the row shows its time while a knob is in
use); Shift + a pad chooses the first pad (the chosen pad at first). The click puts the slices onto the pads from the
first on (as many as fit), every pad the take's one file with its own region (`keep`: the host's answer keeps it), the
pads' other settings at their defaults; one Undo brings the kit back. Back leaves the slicing (`RFM_MOVE_KIT_SL_DONE`).

**The page.** `rfm_move_kit_play` (a pad heard or let go, its glow), `rfm_move_kit_choose` (the chosen pad; its track
selected while the Kit view shows), `rfm_move_kit_set` (a setting as the knobs clamp it: its `KIT_SET`, the project
edited) for the host's Kit tab.

| Action | Fields | Answer |
|---|---|---|
| `RFM_MOVE_ACT_KIT_MERGE` (73) | `index` the second pad, `take` the first, `value` `RFM_MOVE_KIT_MG_MIX` / `_JOIN` | `rfm_move_kit_loaded` (the second pad, the new file) |
| `RFM_MOVE_ACT_KIT_EDIT` (74) | `index` the pad, `value` `RFM_MOVE_KIT_ED_TRIM` / `_NORMALIZE` / `_REVERSE` | `rfm_move_kit_loaded` (the new file) |
| `RFM_MOVE_ACT_KIT_SLICE` (75) | `param` `RFM_MOVE_KIT_SL_ASK` (`take`, `value` `RFM_MOVE_KIT_SM_*`, `from` a step's us), `_PLAY` (`from`, `count` frames), `_DONE` | `rfm_move_kit_slices(m, take, path, frames, rate, n, cuts, npk, peaks)` |

Nothing new is saved with the project (a pad's path and region, as before). Tests: `move_kit` 25..29 (the tools, mix
and join, slicing, the page's calls) and boards N, O, P and V whole (O's one wave column at the seam masked).

## The kit (round A)

The owner tried the kit (2026-11-25) and asked for a rethink of loading sounds: "I'd like to press a pad and then the
sample on that pad should be selected. If I have a pad selected and then select a new sample it should immediately be
on that pad so I can use the pad itself to preview." The look stays (tabs, rows, the pad number top right).

**Pad-first loading.** Opening Files (its tab chosen, the view opened on it) is a browse for the chosen pad: the core
sends `KIT_FIND` with the pad's path and the host answers `rfm_move_kit_found` (the place, its folders, the row in
each, the sound's row id); Files moves there, the jog on the pad's row. An empty pad (or a sound not found) leaves
Files where the last load was. The jog at rest `RFM_MOVE_KIT_LAND_MS` (120 ms) on a sound puts it on the chosen pad
(`KIT_LOAD` with the row; a fast spin: one load, the sound it rests on) and, once the host's answer comes, the pad
plays it once (a held mode let go after 400 ms; Toggle: a second tap); the pad's settings stay, its region whole as
for any new sound. A folder or a kit loads nothing, and the row the pad already has neither. Files no longer sends
`KIT_PREVIEW`. A pad pressed in Files plays and is the target (blinking white): another pad starts a new browse for
it (the list goes to its sound), the same pad only plays (it is how you hear what landed). The jog's click on a sound
keeps it (loaded at once if the jog had not rested yet) and goes to the Pad tab (the Sound row); Back goes up a
folder, then the places, then the tabs, as before. **One Undo for the whole browse:** the pad's undo is kept at the
browse's first load, so Undo brings back what the pad held before Files was opened for it, however many sounds
landed; a pad pressed meanwhile starts a new browse, so Undo restores the last target's pad.

**The close-up of Start and End.** Start (knob 2, or its row's popup) and End (its row's popup) draw the value large
(3x) over the frames round the point: 8 times nearer than the whole sound (`RFM_MOVE_KIT_ZOOM`), 32 with Shift held
(`_ZOOM_FINE`; Shift also makes the turn fine, 4 of 32767 a detent). The point is a line down the middle with a notch
on top; the columns outside start..end are dithered (half lit), past the sound's ends nothing. The peaks are the
host's: `KIT_PEAKS` (`index` the pad, `from` / `count` the window's frames inside the sound, `value` 64) asked again
when the window moves a sixteenth or the zoom changes; `rfm_move_kit_peaks` answers. 150 ms after the last turn
(`RFM_MOVE_KIT_AUD_MS`) the core sends `KIT_AUDITION`: a moved start plays from it (a second at most), a moved end the
region's last second; a further turn cancels the pending one.

**Hits' sensitivity.** In the slicing screen with Hits, knob 3 (dark and dead until now; free in the layout) is the
sensitivity 1..100 (50 at first, lit in the menu's colour; its row shows "Sens 55"); 100 ms after a turn
(`RFM_MOVE_KIT_SENS_MS`) the cuts are asked again (`KIT_SLICE` `_ASK` with `count` the sensitivity). With Equal or
Steps it is dark and dead.

**Trim silence.** The Kit tab's fourth row (I_TRIM, "Trim On" / "Trim Off"; three rows shown, scrolling): its click a
popup, the jog On / Off (default On), kept with the kit (version 13) and in a saved kit's `kit.json`
(`rfm_move_kit_trim`, `_set_trim`). `KIT_REC`'s `param` carries it; the host moves the pad's Start to just before the
take's first sound (`rfm_move_kit_set`), the file whole.

**Toggle.** Play mode's fourth (`RFM_MOVE_KIT_M_TOGGLE`, I_TOGGLE): a tap starts the pad, the next tap stops it (a
short release); it loops while on; a step's note toggles it the same way (the host toggles on each note-on, its
note-off ignored). The Play popup draws the four modes (board E redrawn in the mock).

| Action | Fields | Answer |
|---|---|---|
| `RFM_MOVE_ACT_KIT_FIND` (77) | `index` the pad, `data` / `len` its path | `rfm_move_kit_found(m, pad, place, depth, folder[], sel[], id)` |
| `RFM_MOVE_ACT_KIT_PEAKS` (78) | `index` the pad, `from` / `count` frames, `value` the peaks | `rfm_move_kit_peaks(m, pad, from, count, n, peaks)` |
| `RFM_MOVE_ACT_KIT_AUDITION` (79) | `index` the pad, `from` / `count` frames | none (heard once) |
| `RFM_MOVE_ACT_KIT_REC` (69) | (round A) `param` 1: Trim silence on | as before |
| `RFM_MOVE_ACT_KIT_SLICE` (75) | (round A) `_ASK`'s `count`: the Hits' sensitivity 1..100 (0: 50) | as before |

The project is version 13: the kit's header byte 9 is the Trim silence (0 / 1); 12 and 11 still load (as On); the
Play mode may be 3. Tests: `move_kit` 9 (Files, rewritten: the landing, one load for a fast spin, the click, one Undo,
`KIT_FIND` and its answer, a pad pressed there), 30 (the close-up: `KIT_PEAKS`, Shift's zoom, the point, the
dithering, the 150 ms audition), 31 (the sensitivity knob), 32 (Toggle), 33 (Trim silence saved).

## The kit (round B)

The owner's 2026-11-26 round (remoteFM14Move-kit KIT-SPEC.md "Round B"). Project version 14: 20 settings a pad
(`RFM_MOVE_KIT_P_DECAY`, `_SUSTAIN`, `_CUTOFF`, `_RESO` after Send 3; versions 11..13 read with Decay 0, Sustain 100,
the filter open).

| Control | What |
|---|---|
| knobs | the page chosen at the touch: 1 Pitch, 2 Start, 3 Stretch, 4 Play, 5 Filter, 6 Resonance, 7 Level, 8 Send 1 (pink); Shift held at the touch (blue): 1 Attack, 2 Hold, 3 Decay, 4 Sustain, 5 Release, 6 Choke, 7 Pan, 8 Fine (cents, Shift + knob 1 before). Shift pressed after Start is touched: the 32x close-up and a fine turn |
| a step held + the volume knob | the chosen pad's hits there: their velocity ("Step 5 Velo"); the step keys bright as the hits are hard (100 as before) |
| a step held + a knob | the lockable ones lit yellow (Pitch, Start, Filter, Resonance, Level; Shift: Decay, Pan), the rest dark: that hit's own value. A locked step blinks slowly (a second round) while its pad is chosen; Delete + the step clears its notes and locks |
| Slice, Tap (the sixth method) | the first step plays the take from its start, a cut at 0; each further step a cut where the sound heard is (the core's clock less `RFM_MOVE_KIT_TAP_LAG_US`, 17 ms, or the host's lead_us), 16 at most; steps white a cut each, the playhead's slice in the take's colour; a press after its end: a new pass; Delete + a step: the cuts cleared |

Locks are the sequencer's (clip, step, cc) with cc `RFM_MOVE_KIT_LOCK0` (136) + lock x 4 + pad % 4 (`RFM_MOVE_KIT_LK_*`;
the value 0..254 encoded: Pitch semitones + 24, Start of 254, Filter and Resonance, Level dB + 60, Pan + 50, Decay an
envelope step). A clip's note-on of a kit pad carries its step's locks in `KIT_NOTE`'s `klk_mask` / `klk` (the
settings' units). Undo, copy and save as any lock.

## The kit (round C)

The owner's 2026-11-26 round (remoteFM14Move-kit KIT-SPEC.md "Round C"): a pad's sound is its own, the originals never
changed. The host keeps a folder of sounds for each project and copies a sound in when it is kept; every new file goes
there. No new bytes: project version 14 still.

| What | |
|---|---|
| `RFM_MOVE_KIT_OWN` ("@/") | a path that starts so is a file in the open project's own folder (the host's: Samples/RemoteFM1/Kits/Projects/NN), so a project moved or duplicated keeps its sounds |
| `KIT_KEEP` (80) | Files left after the jog put a sound on the pad (the click on a sound, another tab, the view closed, another pad's browse): index the pad. The host copies the pad's sound into the folder (not a file already there) and answers `rfm_move_kit_owned(pad, from, to)`: the path alone changes, no load, no region reset. The jog's landings themselves write nothing |
| `KIT_LOAD` param 1 | the sound kept: the sampler's To Kit (the take onto the chosen pad), a take's slices; the host loads its copy |
| Freeze | the Sound row's tools are Play, Trim, Normalize, Reverse, Freeze (a snowflake), Delete (six icons, 21 px apart). `KIT_EDIT` `RFM_MOVE_KIT_ED_FREEZE` (4): the host renders the pad as it sounds now; the pad blinks white while it renders ("Freezing"), then its new file comes with pitch, stretch, reverse, the envelope, the filter, level and pan back to the defaults (`put`: KIT_SETs, the project edited) and "Frozen / Pad N"; one Undo brings the pad back as it was |
| `rfm_move_kit_refs` | the paths the kit refers to: every pad's, the Undo's (a pad's, the whole kit's); the host keeps those files of the folder when it cleans it |
| `rfm_move_kit_edit` | the page's Freeze (or any `RFM_MOVE_KIT_ED_*`), as the Sound row's tool |

## The kit (pages)

The owner's 2026-11-26 round (remoteFM14Move-kit KIT-SPEC.md "Pages, 2026-11-26"): "I want the Jogwheel behaviour to
function like it does on the sequencer pages", then their own page design. The Pad / Kit / Files tabs and the jog's
rows are gone; the knobs always follow the page. `rfm_move_kit_t`: `page` (RAM, the view's, not a pad's) replaces
`row`, `on_tabs`, `picking`, `edit`, `kshift`: rebuild everything. Nothing new saved: project version 14 still.

| Page (`RFM_MOVE_KIT_PG_*`) | Picture | Knobs 1..8 (- dark and dead) |
|---|---|---|
| Sample Kit (`_KIT`) | the whole screen: 16 lanes (pad 1 at the bottom), each pad's hits over the 16 steps shown, a pad with no sound empty, the chosen lane underlined, each lane's playhead while playing | Right half, Trim silence, - x 6 |
| Pad Sample (`_SAMPLE`) | the wave over the region (no name: the click's browser opens on it) | Pitch, Fine, Start, End, Play, Reverse, Choke, - |
| Pad Sample 2 (`_ENV`) | the AHDSR | Attack, Hold, Decay, Sustain, Release, - x 3 |
| Stretch (`_STRETCH`) | the wave, the steps it lasts | Stretch, - x 7 |
| Filter (`_FILTER`) | the filter's curve | Filter, Resonance, - x 6 |
| Pad Mix (`_PADMIX`) | the level as a bar, the pan a mark (muted: hollow) | Level, Pan, Send 1, 2, 3, Mute, - x 2 |
| Kit Mix (`_KITMIX`) | the kit's name, its 16 pads (the chosen underlined) | the kit's Level, Pan, Send 1, 2, 3 (the Mixer's Kit strip), - x 3 |

| Control | What |
|---|---|
| jog | a page a detent, stopping at the ends (as the sequencer's knob pages): the page's name big and the seven page marks (`v2_modeName`, the ENGINE page's arrival) for `RFM_MOVE_POPUP_MS` after the last detent, then the page: its picture (top, x 0..101), the chosen pad's number (2x, top right), 8 cells (two rows of four, 32 px: a short name and the value at 1x, as the FX pages; a knob with none: a dash, its light dark) |
| knobs | the page's (pink); touch: the popup (Start / End: the close-up, Shift 32x and fine; the envelope's stage thick; the filter's curve; the right half's layout and key); Shift: fine |
| jog click | the browser for the chosen pad (round A's pad-first loading), a layer with the jog. On Sample Kit and Kit Mix: Kits (Load: the browser's Kits; Save) |
| the browser | as Move's: three rows, an icon each on the left; the root Takes, Samples, Kits (folders: a right arrow); in a folder **Back** first (a left arrow; its click a folder up, at a place's top the root), folders (right arrow), sounds (a small wave), saved kits. The jog on a sound puts it on the pad at rest (`RFM_MOVE_KIT_LAND_MS`); the click on a sound keeps it and leaves; Back (the button) a folder up, the root, then leaves, the sound kept (`KIT_KEEP`); a pad pressed: the target, the list on its sound |
| Shift + jog click | the pad's tools: Play, Trim, Normalize, Reverse, Freeze, Delete |
| a step held + a knob | that hit's lock, for the lockable settings on the page shown (lit yellow, their cells shown; the rest dark, a dash) |

## FX parameter pages (0.5.0)

A module's own parameter pages, as Schwung's `ui_hierarchy` lays them out: each level's 8 knobs a page, its overflow
pages after it, its sub-levels' pages after those. The **host plans** the pages (it reads `module.json`); the core
draws them and maps the knobs. Schwung's preset browsers (`list_param` / `items_param`), `menu` lists, `visible_if`
and the custom JavaScript UIs (`ui_chain.js`) are not part of 0.5.0.

**FX setup is one layer** (the owner's): the pads show the whole chain, rows 1..3 from the top:

| Pads | Are |
|---|---|
| column 1, rows 1..3 | the MIDI FX slots 1..3 (the MIDI FX colour; empty dim) |
| column 3, rows 1..3 | the firmware slots 1..3, for routing and their mix (each firmware's colour; empty dim) |
| column 3, row 4 | (0.6.6) Out: the host's MIDI out (on the Move: USB-A and network MIDI), a MIDI route's target (dark; faint cyan when the selected firmware routes there) |
| column 5, rows 1..3 | the audio FX slots 1..3 (the audio FX colour; empty dim) |

The selected pad is bright (breathing while its module's pages are open). `settings.fx_page` says which column is
selected: `RFM_MOVE_FX_PAGE_MIDI` (column 1), `_ROUTE` (column 3), `_AUDIO` (column 5); Track 1..3 select those
columns too, Track 4 the Mixer (`_MIX`). The screen is the chain, as before.

| Control | Does |
|---|---|
| tap a slot's pad | selects it (a MIDI FX: the steps its one track / All; a firmware: the steps its 16-track filter) |
| hold an FX slot's pad + turn the jog | its module list (empty, then the modules), the name on screen; **letting the pad go loads the one shown** (FX_LOAD), only if the jog turned and it is another module. Back while held: closed, nothing loaded |
| Delete + an FX slot's pad | empties it (Delete alone: nothing) |
| jog click, an FX slot with a module selected | **its own pages**: the page name (or the module's) and `n/m` on top, 8 cells in 2 rows of 4 (short label and value; a gap a dash), the pages as a strip |
| jog turn in the pages | the next / previous page (stops at the ends) |
| Back in the pages | the slots again (a layer); Back on the slots leaves the view (0.4.5). Another slot's pad closes the pages too |
| knobs, an FX slot | its page's parameters (on the slots too: the slot's page as it was left); a gap: nothing |
| knobs, a firmware | 1 level, 2 pan, 3..5 sends to audio FX 1..3 |
| hold a firmware's pad + tap another firmware's pad, or Out under them | (0.6.6) **MIDI routes**: that firmware's MIDI goes there too, or no longer (a toggle; one firmware may feed two and Out). While held its targets and Out light cyan, the others dim. A route that closes a loop (A to B while B reaches A) is refused: "Loop: not set". A plain tap still selects. The popup: "FW1 ChoralRoot / MIDI to / Oberwave / on"; at rest a firmware's box in the chain has a notch top right (it routes to a firmware) and bottom right (to Out) |
| a knob touched | its popup while touched (0.4.5) |

Each slot remembers its page; a newly loaded module starts on its first page. The jog turned with no pad held, or
clicked on an empty slot or a firmware, does nothing; Left / Right do nothing in FX setup.

**The Mixer** (Track 4):

| Pads / knobs | Are |
|---|---|
| columns 1..3 | each firmware slot's meter: its 4 pads light bottom up with the chosen value (a pad 32 steps; the top one dimmer while partial), in the firmware's colour; an empty slot dark |
| right 4 x 4 (columns 5..8) | what knobs 1..3 set: row 1 Level, Pan; row 2 Send 1, 2, 3 (Level white, Pan amber, sends pink; the chosen one bright). The other 9 pads are dark |
| knobs 1..3 | that slot's chosen value (the column above it follows); a touch shows the popup |
| knobs 4..8 | nothing (no FX return levels yet) |

The screen: "Mixer" and the chosen value's name on top, then each slot's name, its value (big) and a bar (pan from
the centre). The meters show the set values, not live audio.

**API** (`include/rfm1cs/move.h`; every new field at the end of its struct, zero = the 0.4 behaviour):

- `rfm_move_fx_module_t` gains `npages`, `pages`: the host's plan, `rfm_move_fx_page_t { name; knob[8] }`, knob n a
  parameter index or -1 (a gap). At most `RFM_MOVE_FX_PAGES` (16) pages. `npages` 0: the core makes pages of 8 in the
  params' order (a flat module with more than 8 parameters gets pages too).
- `rfm_move_fx_param_t` gains `short_name` (a page cell's label, 5 characters shown; NULL: `name`), and `lo`, `hi`,
  `decimals`, `unit`: with `lo != hi` a value 0..127 reads as `lo + (hi - lo) * v / 127` with that many decimals and
  the unit ("38%", "2.4s"); otherwise 0..127, or the word for a list.
- `RFM_MOVE_FX_PARAMS` is 128 (was 8): the parameters a module keeps, `nparams` past it ignored. Values stay 0..127 (a
  list: the word's index) by **parameter index**; `FX_PARAM`'s `param` and `rfm_move_fx_set_param`'s `index` are
  that index, 0..127, on any page. `rfm_move_fx_sync` sends every kept parameter. The host converts to the
  module's float / int / enum.
- `RFM_MOVE_FX_KNOBS` 8 (a page's knobs); `RFM_MOVE_FX_UNSET` 255: a value an older project did not keep, which
  becomes the parameter's default once the module is known (the modules given, or a load with them given).
- Projects are **version 6**: FX setup keeps 128 values a slot (the block is `RFM_MOVE_FXSET_BYTES`, 720 bytes more;
  `RFM_MOVE_PROJECT_MAX` 40136, `RFM1CS_FILE_MAX` 1284680). Version 5 projects load: their 8 values are parameters
  0..7, the rest the defaults.

## Decisions where the design left a gap

The sequencer's (the design gives labels only):

- **Time**: 96 PPQN ticks (24 a 16th step); the external MIDI clock (24 PPQN) is 4 ticks a clock, interpolated at
  the measured tempo and never more than 3 ticks ahead of the last clock (each clock catches up, stamped "now"; 0.5.5:
  at most 3 clocks a block out loud, so a burst of clocks jumps ahead without sounding the steps it skips).
  4/4 bars (16 steps = a page = a bar).
- **Clock source** (per project): INT (0.5.5: the default, and what a project saved before 0.5.5 with AUTO loads
  as) runs on its own clock and ignores the firmware's. AUTO follows a clock master only: a Start or Continue, then 24
  steady clocks (gaps under 125 ms, 20..300 BPM on average), and the sequencer starts at the master's next bar; a
  firmware's own Start / Stop with no clock stream (a looper's play and stop) never takes the transport. It is the
  master's until its Stop or 500 ms with no clock (then the internal clock at the project's tempo, playing on). EXT
  follows every clock, Start, Continue and Stop and waits for clocks; 500 ms with none while playing lets every
  sounding note go and the position holds until clocks come back. Following: the
  clock's Start starts the sequencer at bar 1 on the next clock, Stop stops it, Song Position + Continue resumes at
  the position's next bar; Play on the Move starts at the external clock's next bar (counted from its last Start or
  Song Position, else from the first clock seen). Play on the Move never sends transport to the firmware.
- **Every clip plays from the song's bar it launched on**; launches and stops are quantized to the next bar; while
  stopped a clip press changes the track's clip at once (playing with Play). Each track has one playing clip (`active`, default scene 1
  for all, so Play plays scene 1) and one edited (`sel`, follows launches). Programming a track with no playing clip
  starts its edited clip.
- **Recording** quantizes to the nearest step and keeps the offset (micro), so Shift + Capture (or Shift + step 3)
  quantizes later; a note is written when it ends (stop, mode change and project change end it too). A take (from
  Rec on) is one undo. Recording into a track with no playing clip starts its edited clip at the current bar.
- **The last notes** (step entry's memory) are not saved with the project; a new chord starts when a pad is
  pressed with no other pad held (pads pressed while a step is held edit the step instead).
- **The count-in** is a bar of the internal clock's ticks from the Play press (Rec armed) (T 0 exactly a bar later); following an
  external clock there is none (the start waits for its next bar, as Play does).
- **Capture** keeps the last 4096 pad events (0.5.1; was 1024) (track view). Stopped: the first note lands on step 1; playing: the
  clip starts on the bar of the first note and plays on in phase. 1..8 bars; into the edited clip if empty, else
  the track's first empty clip ("NO EMPTY CLIP" otherwise).
- **CC locks** are stored by CC number (not knob), so they follow the bank; sent at the step (no reset after).
- **Pads are shared with the slot's note layout** (layout, scale, root, octave) on entering; only the channel is per
  track.
- **Undo** is a journal of 2048 edits (note add / remove, lock, length) grouped by gesture (one step hold, one
  paste, one take); the oldest groups go when it is full; a single gesture larger than the journal is not undoable.
  Mutes, launches, tempo and channels are not undone.
- **Cut / paste**: Delete + item cuts it into the clipboard; the next Copy + target pastes it. A track copy goes
  clip by clip; Delete + track clears it without the clipboard.
- **Note-offs**: the sequencer's notes are its own (a table of 64; a note retriggered is ended first; when full the
  one due first ends). Stop, mute, a clip change, a channel change, a project load and `rfm_move_release_all` (the
  link drops: the sequencer stops) end them at once; a mode change does not stop the sequencer, its notes end on
  time. A live pad note and a sequenced note on the same channel and note are not reconciled.

- **Back is the view flip (0.4.3, the owner's).** Until 0.4.2 Back pressed the FM-1's SEQ (the design labels it "OCT-",
  a second OCT- beside Left, and maps nothing to SEQ). Since 0.4.4 Up presses SEQ, in FM-1 mode only.
- **Down cycles only in FM-1 mode (0.4.4).** The MIDI modes need it for the octave and Sequencer mode uses it, and the
  projects view has its Tracks for the firmwares.
- **MIDI modes keep the FM-1 mapping elsewhere**: only the pads, steps and Up / Down change in mode 2 (and the
  knobs and jog wheel in mode 3); Left / Right, the named buttons and the knobs stay the FM-1's. Mode 4 takes over
  everything but the Track buttons and the volume knob.
- **Steps** do nothing in modes 2 and 3 unless Track 2 is held (the channel).
- **Up / Down transpose every layout**, the drum layout's saved notes included.
- **Drum note editing** takes the jog wheel only while a drum pad is held (else the jog wheel is SELECT, or the bank
  in Control mode). The held note still ends where it began.
- **Pads floor**: an FM-1 key whose LED is off still glows faintly (about 10 %), so the keyboard can be seen.
- **Steps' LEDs** are written as RGB note LEDs (LED_RGB kind 0, 16..31), like the pads. Not tried on a real Move
  yet: if the steps take only palette values, `led_id` in move.c is the place to change.
- A white key held by both a pad and a step (or a note by two pads) goes down with the first and up with the last.
- The CC values in Control mode start at 64 and are not saved: the knobs are relative and the target's value is
  unknown.

## API (the sequencer: below the panel's)

```c
rfm_move_t m;                                     /* the host owns it; no allocation */
rfm_move_init(&m, &saved_or_NULL, on_action, ctx);
rfm_move_input(&m, &mr_event, now_ms);            /* from mr_client's on_input */
rfm_move_panel(&m, levels44, play_green);         /* levels[col * 4 + line] 0..255: fm1panel_t led[11][4] */
n = rfm_move_leds(&m, out, max);                  /* only the changed LEDs: mr_led_rgb / mr_led each */
rfm_move_screen(&m, rgb565_240x240_or_NULL, packed1024);   /* then mr_display */
rfm_move_screen_timed(&m);                        /* 1: draw again as the clock runs (a popup up, playing) */
rfm_move_release_all(&m);                         /* the link dropped, or the host leaves Move control */
rfm_move_leds_invalidate(&m);                     /* a new connection: repaint */
rfm_move_set_master(&m, v); rfm_move_set_label(&m, enc, "CUTOFF"); rfm_move_set_keymap(&m, km);
rfm_move_set_labels(&m, lbl49); rfm_move_set_button_names(&m, names14);   /* (0.5.6) DYNAMIC-LABELS order */
rfm_move_readout(&m, ctl, RFM_MOVE_RD_PARAM, "Cutoff", "1.2k", frac, flags, ms); rfm_move_readouts_clear(&m);   /* (0.5.7) */
rfm_move_save(&m.s, bytes); rfm_move_load(&s, bytes, n);   /* "RMV" + version 5, 87 bytes (versions 4..1, 86, 80, 78, 77 bytes, load) */
```

`rfm_move_t` is about 530 KB with the sequencer and (0.14.0) the engine parameters' 290 KB: keep it static or on the heap. The sequencer:

```c
rfm_move_seq_advance(&m, frames, rate);           /* every audio block, at its start, playing or not (rate 1000000: us) */
rfm_move_seq_midi_in(&m, msg, n);                 /* the firmware's MIDI out realtime: F8 FA FB FC, F2 lsb msb */
rfm_move_seq_play(&m, on); rfm_move_seq_set_bpm(&m, bpm10); rfm_move_seq_set_clock(&m, RFM_MOVE_CLOCK_AUTO);
rfm_move_seq_bpm10(&m); rfm_move_seq_following(&m);
n = rfm_move_project_save(&m, out, cap);          /* export: <= RFM_MOVE_PROJECT_MAX (39072) bytes, -1 cap too small */
rfm_move_project_check(blob, n);                  /* import: 0 valid */
rfm_move_project_load(&m, slot, blob, n);         /* PROJECT_LOAD's answer (also at start); NULL/0 = empty; < 0 invalid */
rfm_move_project_dir(&m, used_bits); rfm_move_project_flush(&m); rfm_move_project_slot(&m);
rfm_move_project_info(&m, slot, blob, n);         /* a stored project's content, for the grid's bars and its popup */
rfm_move_project_new(&m, slot);                   /* (0.14.0) a new project in an empty slot, as its pad's click: 0, or -1 refused */
rfm_move_ep_describe(&m, slot, blob, n);          /* (0.14.0) a firmware's ep1 description (NULL: none); < 0 refused */
rfm_move_ep_layers(&m, slot, n, engine, midi_ch); rfm_move_ep_values(&m, slot, layer, val, n);
rfm_move_ep_cells(&m, slot, layer, page, cells8); rfm_move_ep_has(&m, slot); rfm_move_ep_check(blob, n);
```

Every action has `frame`: for actions from `rfm_move_seq_advance`, the offset (in that call's units, from the
call's start) the event is due at; 0 (now) otherwise. Project actions: `PROJECT_SAVE` (index slot; `data` / `len`
the blob, valid during the callback), `PROJECT_LOAD` (index slot: the host calls `rfm_move_project_load`, at once
from the callback or later), `PROJECT_DELETE` (index slot). A blob: `RMSQ`, version 1, header (tempo, clock mode,
fixed velocity, selected track), 16 tracks (channel, mute, playing and edited clip), 256 clip lengths, the notes
(7 bytes each), the locks (4 bytes each), FNV-1a; little-endian; every field range-checked, keys strictly ascending,
at most 8 notes a step, the length exact. Empty: 608 bytes; 600 notes and 200 locks: 5.6 KB; the worst case
(4096 notes, 2048 locks): 37 472 bytes. Limits: 4096 notes and 2048 locks a project, 8 notes a step, clips 1..256
steps, 64 notes sounding.

Actions (`rfm_move_act_t`, the OMX-27 link's vocabulary): `KEY` (key 0..26, down, vel), `PRESS` (key, 0..127),
`BTN` (matrix button 0..13, down), `TURN` (matrix encoder 0..6, detents), `MASTER` (0..1023), `MIDI` (n bytes into
the firmware's MIDI in), `SETTINGS` (save them). Buttons and encoders are in proto/rfm1.h's matrix order, as
libremotefm1's `rfm_event_t`: buttons 0 OCT- 1 OCT+ 2 FX 3 SEL 4 ENV 5 LFO 6 EDIT 7 GLO 8 HOME 9 SAVE 10 ARP 11 SEQ
12 PLAY 13 REC; encoders 0 SELECT 1 ALGO 2..5 K1..K4 6 PRESETS (panel.js `labelToMatrix` / `roleToMatrix`).

## The screen

**v2** (the owner's design canvas "Move Screen UI for RemoteFM1", its v2 boards and the renderer they share,
`V2Screen.dc.html`). `v2.c` is a line-for-line port of that renderer's primitives (font5x7 in its 8-row cell, the
11 x 11 icons, the strip, the edit popup, the key / scale screen, the cells), JavaScript's rounding included, so a
screen drawn with it matches the mockup pixel for pixel; `test_move_v2` draws all 67 mockup screens with it and
compares each with the mockup's own pixels (`tests/move_v2_ref.h`, made by `tests/move_v2_ref.mjs` from the design's
`V2Screen.dc.html`), then sets each situation up on a live core and writes `core_<name>.pbm` beside `v2_<name>.pbm`
for a contact sheet.

**The Sampling waveform**: the host passes the recorded audio's peaks with `rfm_move_sample_levels(m, peaks, n)`
(0..255 each; taken only while the loop records, a new take starts empty). The web page computes them in the worklet,
the louder channel's peak over each ~16 ms window of a loop take (`winPeaks`), posts them with the recorder's level
(`recLevel.wins`, 30 a second), and the page passes them on as the site event `loopLevels` to MoveLink, which draws
the Move's screen again when any were taken.

**Note names** are Ableton's everywhere (popups, MIDI Notes, chords, drum notes, the step popup, the key / scale
screen): MIDI 60 = C3, flats (Db Eb Gb Ab Bb).

The grammar (the owner's rules): **symbols at rest, words only while something is touched or turned**; at most three
rows of 2x text, 4x for the one number that matters, 3x for long mode names, 1x only inside pictures; **Title Case**
("Loop Doubled", "Click On"); **no boxes** around values, parameters or icons; a chosen icon **underlined**, never
inverted (the one inversion: FM-1 mode's touched-knob block); **dashes 2 on, 2 off** (copied, the beats, a muted
track); cut = crossed. **Move's bottom strip** (1 px segments, the current ones 3 px) says where you are: the pages
and the playhead tick in track view, the bank, the arp's page, the modes. The popups time out on the sequencer's
clock (`rfm_move_seq_advance`, called in every mode); `rfm_move_screen_timed` says when the screen changes with the
clock alone.

| Screen | What it draws (the mockup) |
|---|---|
| FM-1 mode (fm1.rest) | left: the owner's 64 × 64 drawing, pixel for pixel, live (below); right: the FM-1's screen, 64 × 64 black and white (`RFM_MOVE_THUMB_LUMA`, no dither). Playing: a small play triangle top left |
| MIDI Pads (midi.pads) | the 8 × 4 pads: the root filled, in the scale outlined, out of it a dot; a pressed pad carries a 6 × 6 square in the opposite colour |
| MIDI Notes (midi.notes, midi.chord) | left black at rest; playing: the chord's name at 3x (2x, 1x when wider), the held notes on a one-octave keyboard (white keys filled, black keys a dot) |
| (Control mode, gone in 0.8.0) | (the note layouts show the pad map; the CCs in their popups) |
| Track view (seq.track) | the shown page as a piano roll, full height (8 px a step, rows of 4 px, the page's notes centred, squeezed past 12 semitones; the beats dashed; the playhead a line); the strip: the clip's pages, the shown one thick, the playhead tick over the whole clip (a ghost page shown: the real pages a line, a + at the far end). The steps row always under it (the owner's: no popping in and out; the roll shrinks for it): notes filled, empty outlined, outside the loop a dashed line, copied dashed, cut crossed, the held ones (and Step record's cursor) marked above; Move's strip under the row. Recording: a record icon and "Recording" on top (rec.capture); the audio loop: a wave icon and "Sample" (armed, blinking) / "Sampling", the take's levels as a waveform scrolling in from the right (bars about the centre line, 60 of them 2 px apart; flat while armed), the bar lines dashed, the strip its bars and where it is (rec.sample, pixel for pixel) |
| Loop page edit (loop.edit) | Loop held or latched: the loop icon, "Bars 3-6", the 16 pages as cells (looped filled, the clip's others outlined, past it a dot, cut crossed, copied dashed), the shown one underlined |
| Session view (sess.rest) | the owner's mockup: (0.10.0) 7 tracks across and the scenes' numbers in the 8th column (the scene that plays underlined, one on its way blinking), 4 slots down; playing filled with a black play icon, (0.10.0) stopped and the track's clip (Play starts it): an outline with the play icon, a clip with no notes: its corners only, recording with a black square, queued doubled (blinking), stopping blinking, a clip outlined, empty a dot, copied dashed, cut crossed; a bar over the selected track, a dashed line over muted ones; (0.9.2) the selected clip (or empty slot) framed: a 16 x 15 outline in the gap round its cell, a filled cell standing clear of it |
| Projects (proj.grid) | the 32 slots as the pads are: the open one filled, saved ones outlined, each with three bars (how full its tracks are), empty a dot; cut crossed, copied dashed; loading blinks. Entering: the set icon over "Projects" (mode.proj) |

**FM-1 mode's drawing, live** (the owner's exact drawing; only these change): the touched knob's 2 × 2 dot inverted
in a 6 × 6 block (the volume knob MASTER, knob 1 PRESETS, knob 2 ALGO, knobs 5..8 K1..K4, the jog SELECT); a button's
cell filled while the FM-1's LED for it is at `RFM_MOVE_SCREEN_LIT` (64) or more, or it is held (the 6 × 2 grid in
the panel's order, OCT- / OCT+ the box of two at the left); a held key's 2 × 4 block dark.

**Popups**: full screen, borderless. In FM-1 mode and MIDI Notes they keep to the left 64 × 64, cleared to black,
the name (2x, 1x past five letters) and the value (4x, else 2x) only, no bar (fm1.knob, fm1.jog, fm1.master,
midi.knob), so the FM-1's screen stays in view.

| Popup | When | What (the mockup) |
|---|---|---|
| mode name | a quick Track tap (mode.fm1, mode.seq, mode.notes) | "FM-1", "MIDI", "Control", "Seq" at 4x (else 3x), the strip the four modes (the two views for Pads / Notes) |
| edit | a knob turned (ctl.knob, seq.lock), the volume knob, Shift + jog, the jog's bank, a drum pad + jog | the name on row 1, the value under it at 4x (2x when wider), no box; a bar (bipolar from the middle for an FM-1 encoder's detents); a held step's CC lock: a padlock; the knob pages on the strip |
| step | a step held 300 ms in track view (seq.step), only while it is held (a quick tap never shows it; the release drops it) | its notes in words (Ableton's names; "+n" past what fits), the velocity with its icon, the length with a bracket; the steps row, the held step marked |
| held-step edits | a step held + the arrows, the jog, the volume knob (seq.micro, seq.len, seq.vel) | "Nudge" (in % of a step), "Length" (steps), "Velocity" (with a bar) at 4x; the page strip with the step's tick |
| trig | a knob on the TRIG page (trig.*) | a picture on the left (micro: the note between its step's ticks; ratchet: the hits; chance: a die; condition: A:B, the A-th tall; function: the playhead's path (Repeat: the step filled, its copies dashed after it); length: a gate bar) and the name and value on the right; the strip the five knob pages, TRIG first |
| loop length | the jog in page edit (loop.len) | "Loop", "4 Bars" at 4x, the looped pages thick on a strip of 16, a second |
| bar | Right past the end in page edit (loop.ghost) | "Bar n" over the ghost strip, a second |
| project | a project pad held (proj.held); + knob 1 (proj.color) | its number at 4x, a bar a track, ▶ its tempo, its clips; the colour once: "Color" and its number at 4x, the 16 colours on the strip |
| message | an action's result, 1 s (sys.msg, menu.quant, loop.doubled, menu.metro, menu.newclip, rec.captured) | one or two centred rows in Title Case ("Quantized", "Loop Doubled", "Undone"); a toggle or one-shot with its icon (metronome, new clip) has no strip, the others keep the page strip (Double Loop: the loop on a strip of 16); Captured: "Captured" and the tempo |
| count-in | Play with Rec armed (seq.count) | a record dot, the beats to go at 4x, the strip filling beat by beat |
| track | a track change | "Track n" |

**The Shift menus** (V2Menus, V2Arp, V2Global): (0.12.0, a5) every one the arp's header: the top line its title or
main value at rest, the touched knob's name and value while touched or just turned; a tick at y 17 a knob in use, the
active one long; the content from y 20. Workflow: "Track 1", then "Ch 3", "Rate 1/16", (0.14.0) "Triplet" (the feel), "Quant 50%" (three shown). Tempo: the tap icon
(following: a padlock), "Tempo", the BPM at 4x, the taps on the strip. Groove: two 16ths (the second late by the
amount, the straight place dashed), "Groove" on top and the amount at 3x, Global / Track as icons (the chosen one
underlined). Layout: Move's four layout icons on top, the chosen one underlined and named. Keys & Scales (0.12.0):
"D Dorian" on top, the root on a 12-key row, the four layouts' icons (the chosen one underlined), Global / Track with
its icon; Track 2 held keeps the build's key / scale screen. Velocity: the option on top, the curve as a graph, the
level at 3x with a bar. Arp: the pattern's walk (outlined while off), its name, rate and octaves, the page on the strip; the lanes:
the 16 steps as bars from a line, the edited one underlined. Record: the three modes as icons (the chosen underlined
and named), the style with its icon. Global Settings (0.6.10, direction A): the battery header (an 18 x 12 icon with a nub, filled 14 x 8 to the charge;
the percentage at 2x; "GLOBAL" small on the right), a rule at y 17, four rows of 11 px from y 19 (the name left, the
value right), the selected row inverted; editing: the row framed, its value inverted between 3 x 5 arrows. (0.6.12)
Five rows: four show and the list scrolls, a 2 px bar at x 126 (the rows keep direction A's 11 px).

**Held Track buttons**: Track 2 the key / scale screen with the channels (midi.hold2, midi.scale); Track 3 the knob
legend (the turned one filled), "CC" and the number it sends at 4x, which knob (ctl.hold3); Track 4 (and Mute held)
the 16 tracks on one row as the 16 step keys: the selected one filled, muted crossed, soloed a square inside, with
notes outlined, empty a dot; "Track n" on top (sess.hold4). A value changed while it is held (the channel, a knob's
CC, the rate, the layout, the scale) shows as its edit popup ("Channel 5", "Knob 3 CC 20", "Rate 1/16") for a second,
then the tracks again.

**Where it differs from the mockups** (behaviour kept, looks as close as the data allows): the arp's page 1 keeps all 8 knobs (the legend shows
8 rings, the mockup 4); the arp's lanes have 16 steps (the mockup draws 8); the arp's rates are named as said (1/32Q for 40 a bar, where the screen once showed 1/40); there is no charging screen (no STATUS power bit is known
to mean charging); the play triangle in FM-1 mode stays; the FM-1
drawing's touch marks follow our drawing's cells (the mockup's examples sit one pixel off).

## Speech (0.6.0)

For a screen reader (Schwung's on the Move; VoiceOver, TalkBack or aria-live on the other hosts later). Off by
default; `rfm_move_set_speech(m, 1)` turns it on and the core then emits `RFM_MOVE_ACT_SAY` with words for what its
screen shows (move.h has the contract). Two kinds, after Schwung's conventions ("label: value", views by their name,
short, digits for numbers, note names as the screen has them):

| kind | when | examples |
|---|---|---|
| EVENT | the base view changes (not when a popup over it closes) | "FM-1 mode", "MIDI mode, Notes", "Control mode", "Felucca, Drum layout, Track 2" (0.9.0), "Session", "Track 3", "Firmwares, Slot 1, Felucca", "Slot 2, empty", "Catalog", "States, Felucca" (0.13.0: the States layer from anywhere), "Projects", "FX setup, MIDI FX", "Routing", "Mixer", "Arp, Page 1" |
| VALUE | a popup's words change, or the selection's | knobs "K1", "K1: +3", "Volume 75%", "CC 16: 65", readouts "Voicing: Wide"; held Track menus "Scale: Dorian", "Root: Db", "Layout: Drum", "Channel: 3", "Knob 1: CC 17", "Track 3, muted", "Rate: 1/16"; "Latch on"; a held label "SEL/KEYS"; host popups "FM-1 linked"; the sequencer "Playing", "Stopped", "Record on", "Step 5", "Chance: 50%", "Loop: 2 bars", "Tempo 120", "Bar 2", "Undo", "Metronome on", "Sync: EXT"; projects "Project 3, 120 BPM, 4 clips" (0.13.0: and its firmwares, "Project 7, 120 BPM, 12 clips, ChoralRoot, SLOOP, Felucca"), "Loading project 7", "Felucca not installed, Track 3 empty"; (0.13.0) States "State 5 loaded" (saved, copied, pasted, deleted), "New state, ChoralRoot", "Undo, new state", "Track 3, empty"; (0.14.0) "States, ChoralRoot, Project 3", "Project 3, ChoralRoot", "State 5 stored" (empty), "Project 3 saved" (copied, pasted, reset), "State 5 reset", "Undo, State 5", "Undo, paste"; Down "Melodee, FM-1"; the browser "All firmwares", "Filter Synth", "Bank 2", the firmware selected ("Felucca"; in the catalog "Felucca, not installed"), its description (the jog's first detent, 250 characters at most), "Downloading Felucca 25%", "Felucca installed", "Felucca added to Favorites", "SLOOP, position 3 of 17", "Tagged Synth"; FX setup "MIDI FX 1: Arp", the module list "Arp", "Rate: 1/8", "Slot 1 Level: 100", "MIDI FX 1: track 3" |

(0.6.1) Every FM-1 button press is said at once (its label, else its name, else the panel's word: "Play", "Octave
down"). Values with no popup are said when they change: the track selected, mutes and solos, a step on or off, the
bar shown, scenes, clips queued / playing / stopped (at the bar), the octave, bank, layout, key, scale, channel,
velocity, tempo, latch, Step record, sampling, Loop's page edit, Fill, the clipboard, the modifiers pressed; the
Shift menus are views ("Tempo", "Arp") that say their knobs ("Pattern: Down").

**Speech modes (0.6.12, the owner's).** Global Settings' first row, Speech: **Off** (the default: nothing is said),
**On** (every word, as before) or **Jog** (only while the jog wheel is touched: the touch says the screen as it is,
`rfm_move_words`, then every word while it is held; let go: silent; a new touch starts again). Saved with the
settings (version 6, `settings.speech`: 0 not chosen yet, 1 + the mode). Changing it is said in any mode ("Speech
off", "Speech on", "Speech jog wheel"). While nothing is chosen the host's default applies:
`rfm_move_set_speech_default(m, RFM_MOVE_SPEECH_ON)` when the system's screen reader is on, so a blind user is never
locked out. Hosts keep `rfm_move_set_speech(m, 1)` on and gate their own words on `rfm_move_speech_open(m)`;
`rfm_move_speech_mode(m)` gives the mode.

**A firmware that speaks for itself (0.6.11).** With fm1-reader (a firmware's own SysEx SAYs, the host speaking them)
the host calls `rfm_move_set_fw_speech(m, 1)`: the core then says nothing for the firmware's controls (an encoder
turned, its readouts and the range's end repeat, a readout's own popup, an FM-1 button's or key's label or press) and
keeps RemoteFM1's own words (modes, the projects view, menus, the volume, held Track menus, Global Settings, the
sequencer, a touched knob's name). `rfm_move_fw_speech(m)` reads it back.

**Every input and its words (0.6.3).** `move_speech`'s input walk presses every button, step and pad, turns every
knob and the jog, clicks the jog and touches knobs in 34 contexts (each mode and view, each Shift menu, each held
modifier, the projects view, FX setup); any input that changes the screen and says nothing fails it, except pads that
play notes (0.6.5: heard, never said).

| Input | Where | Words |
|---|---|---|
| an FM-1 button (Move button mapped to it) | the FM-1 layout | every press: its label, else its name, else the panel's word: "Play", "Select", "Octave down" |
| knob touch / turn | FM-1 mode | (0.14.0, the owner's) the FM-1's Shift (GLO) held (the FM-1 layout, the FM-1 page's level 2): a turn with no readout of that page shows "Shift" before the knob's name (its label now: the firmware's Shift label when it gives one; a long one scrolls) and the count; a readout that comes is shown at once; a knob's kept readout is shown only for the page it came on (the buttons held then, the label then: never the other page's last value), and a turn that gets no readout of its own in 250 ms shows itself (name, count) rather than the older one. On the FM-1 page's level 1 (Knobs) Shift is the sequencer's; (0.14.0, the owner's 2026-11-24: "the Move's shift button plus a FM-1 knob turn should also do the shift and show the engine change") a turn of one of its FM-1 knobs with Shift held is the firmware's shifted turn: its GLO goes down just before the first such turn and up when Shift is let go, and the popup says "Shift" + the knob's name as above (a readout shown at once). Shift alone sends nothing; Shift + a step, + the jog's turn (the tempo), + the jog's click are as before. (0.6.7) touched: "Name: value" when its readout is known (the readout's full title), else its label ("Bass Regis"); turned: the readout's "Name: value" once it comes, never the Move's count when the firmware gives readouts ("K1: +3" only for one that gives none, after 250 ms); a readout of the other (Shift) page never (0.6.8: a readout belongs to its control, the FM-1 buttons held then and the label); a touch before any readout on this page: the full title seen before for it (0.6.9: on another page too when the host named the control), else the label; one touch-and-turn says each words once; a turn with no new readout (its range's end): its "Name: value" again once (nothing when no value is known) |
| volume | anywhere | "Volume 75%"; Track 1..3 held: "Slot 1 Level: 100" |
| (0.8.0) Track 1..3 tap | layouts | the slot and its layout: "Felucca, Drum layout" (the view), "Felucca, Drum" (the popup) |
| Track 1..3 held | layouts | shown: "Felucca, layouts, Drum"; then "Layout: Chroma", "Scale: Minor", "Root: Db", "Channel: 3", "Octave 1", "Knob 1: CC 74"; pad 8 "Latch on" |
| Track 4 tap / held | anywhere | "FX setup, Mixer" / "Routing"; held: "Track 4, Mixer" |
| (0.9.0) a note layout | anywhere | "Felucca, Drum layout, Track 2"; held "Felucca, layouts, Drum, Track 2"; a step "Track 6, selected", "Track 5, solo" |
| jog click | the FM-1 layout | "Jog: Preset" |
| Shift + knob | a note layout | "Knob 2: CC 19" |
| Track 1..3 held + step (tap, hold, double), Up / Down, jog | Sequencer | shown "ChoralRoot, lock, Track 3"; "Track 6, locked", "Track 7, not routed", "Track 3, solo", "Rate: 1/16", "Channel: 2"; + Copy / Delete "Copied track 2" |
| Up / Down | MIDI, Control | "Octave 1" (0.15.0: Down in FM-1 mode does nothing, so says nothing) |
| jog | Control | "Bank 2" |
| knob | Control, Sequencer | "CC 16: 65"; a held step's lock; the TRIG page "Chance: 50%", "Ratchet: 3" |
| Back | anywhere | what it changed: the view ("Firmwares, Slot 1, Felucca", "FM-1 mode"), a layer ("Loop edit off", "Done"), a selection gone (the view named); nothing changed: where you are |
| step tap / hold / second step | Sequencer, track view | "Step 5" (0.6.5, the owner's: the step only, no on / off, no notes); a held step's Up / Down "Transpose up" / "down" (Shift: "Octave up" / "down") |
| pads, keys, steps as keys; notes recorded | any mode | nothing: notes are heard, never said (0.6.5) |
| Delete + pad | Sequencer, track view | "Deleted C3" (that note out of the clip) |
| Copy / Delete / Mute / Shift held | Sequencer | "Copy", "Delete", "Mute", "Shift" |
| Copy + step / clip; Delete + step / clip; + an empty one | Sequencer | "Step 5 copied", "Step 5 cut", "Cut track 1 clip 2"; "Pasted to step 9", "Step 9 replaced", "Pasted" |
| Delete alone, Copy alone | Sequencer | "Delete: hold and press a step, pad or clip" (nothing changes); "Duplicated" |
| Track 1..3 held + Delete; + a step | Sequencer | "Track 1 cleared"; "Track 3 cleared" |
| Undo, Shift + Undo | Sequencer | "Undo", "Redo", and the steps that changed |
| Play, Record, Sample, Capture | Sequencer | "Playing", "Stopped", "Record on", "Audio loop armed", "Recording audio loop", "Captured", "No empty clip" |
| (0.15.0) Sample, the jog, its click, Back; the host's status and events | the sampler app | opening "Sampler" (the view) then the row "Source: Master"; a row "Mode: Bars", "Length: 4 bars"; a value in its popup "Threshold: -23 dB"; "Tabs, Record", "Tab: Record"; "Armed", "Recording" (closed too); "Saved, 4 bars", "Captured, 2 bars", "Saved, 7.2 seconds", "Nothing to capture", an error's words (closed too); the message gone: the row again; closing: the view under it named |
| Loop held, tapped | Sequencer | "Loop: 2 bars"; "Loop edit on" / "off" |
| Left / Right; Up / Down | track view; Session | "Bar 2"; "Tracks 9 to 16", "Scenes 2 to 5" |
| Menu | Sequencer | "Session" / "Track 1" |
| a clip pad | Session | "Track 1, clip 2 queued" then "Track 1, clip 2 playing"; a double tap "Track 1, stop queued", "Track 1 stopped" |
| Shift + step | Sequencer (Track 4 held, or Shift in a note layout) | menus "Tempo", "Arp", "Groove", "Workflow", "Keys and Scales", "Velocity", "Record mode", "Global settings, battery 87%"; "Metronome on", "Quantized", "New clip", "Loop doubled", "Clip cleared", "Projects" (step 1) |
| a menu's knob (turn or touch), jog | Shift menus | "Pattern: Down", "Arp: On", "Mod lane", "Groove: 5%", "Tempo 121", "Record style: Replace" |
| jog, jog click, Back | Global settings | opening: "Global settings, battery 87%"; "Velocity: Dynamic", "Editing Velocity: Dynamic", "Velocity: Fixed", "Done" |
| a project pad | the project grid | "Project 3, 120 BPM, 4 clips" |
| steps 1..16, pads, jog, jog click | the projects view | "Favorites", "All firmwares", "Filter Synth", "No filter"; "Felucca" (the catalog: ", not installed"); its description; "Loading Felucca", "Downloading Felucca 25%", "Felucca installed", "Felucca failed" |
| Sample, Capture + step, Left / Right, Mute, Loop, a state pad, Delete, a Track again | the projects view | "Felucca added to Favorites", "Tagged Synth", "Bank 2", "Catalog", "States, Felucca", "State 1 saved", "Felucca unloaded", "Slot 1, Felucca" |
| Menu, Track 1..4, a slot pad (+ jog, let go), jog click, knobs, steps | FX setup | "FX setup, MIDI FX", "Routing", "Mixer"; "MIDI FX 1: empty", "Arp", "MIDI FX 1: Arp"; "Arp, Page 1"; "Rate: 1/8", "Slot 1 Level: 100"; "MIDI FX 1: track 3" |
| a firmware's pad held + another's or Out | FX setup, Routing | "ChoralRoot to Oberwave, on", "ChoralRoot to Out, off", "Loop" (refused) |

The selection (the firmware picked, its description, the FX slot focused, the module list, the Mixer's choice) is
said when it changes and not again when a popup over it closes. The host debounces (Schwung: 300 ms, a VALUE
replaces the pending VALUE); the core says every change. `rfm_move_words` gives the screen in words at any time.
What the core never draws stays the host's: entering (the host names the first view; turning speech on takes the
screen as said), states saved / loaded / deleted, leaving.

## Tests

ctest runs `move` (the mapping against the design's labels, every button and knob, MASTER's
clamping, mode changes and release-all, the four layouts' notes, drum editing and the settings round trip, the CC
banks and reassignment, LED diffing; the screen: FM-1 mode at rest exactly the owner's drawing (the PNG's rows), the
live encoders, buttons and keys, the popups' life (held, then a second), ten golden screens: their PBMs land in
the build directory's `move_*.pbm`) and
`move_e2e`: Move Remote's simulator (`make sim` in an abletonmoveremote checkout; found beside this repo, or
`-DRFM_MOVEREMOTE_SIM=`, or `$MOVEREMOTE_SIM`) gets presses through its test FIFO, and the test checks the actions,
the LED writes, and the simulator's screen shot against the frame sent, then a short sequencer run (Track 4, two
steps, Play on the internal clock: the notes as MIDI actions at their frames, the step LEDs, the screen). Without the
simulator it is skipped.

`move_seq` (test_move_seq.c): the data model, step entry (held chords, the last notes, the root, a tap clearing,
the transport starting on the first note), the held step's nudge / transpose / velocity / length and its pad colours
(written to `move_seq_held_pads.ppm`), Loop + jog, Move's copy (release, then target), Copy and Delete alone,
polyphony, lengths and pages, playback at exact
frames (48 kHz, 44.1 kHz, microseconds), the external clock (start, stop, continue from a song position, Play at its
next bar, the timeout), quantized launches and scenes, mutes and every path that ends notes, recording (the count-in,
quantize, micro offsets, growing clips), Capture (Shift + Capture, Shift + Play's retrigger), copy / cut / paste / undo / redo and the journal's overflow, CC locks, scale
and root, the project grid's actions (sync and async answers, a failed load, delete), the autosave's debounce, the
blob round trip and a fuzzed loader (every truncation, 3000 random blobs, 20000 mutations with the check fixed up),
the project info for the grid (from blobs, a broken one refused), the popups' life (a held step, Loop, tempo, CCs,
a lock, messages over popups), and eight golden screens (track view, the step popup, session, project grid, the
project popup, the Track 4 menu, the count-in, a message: `move_seq_*.pbm` in the build directory).

`move_speech` (test_move_speech.c, 0.6.0): the words a screen reader gets, case by case (the SAY actions logged as
`E:` / `V:` and compared whole), and one script run with speech off and on (the same other actions, the same screen).

`move_takeover` (0.7.0): the takeover's actions logged and compared whole: entry and exit both ways, volume + Back,
claimed and unclaimed controls, Back held back, the releases before off, each slot's memory, slot switches, the LED
merge, the screen, the firmware's words and the speech flag, claims changing and claims 0, a Shift menu over it.

`move_fw` (test_move_fw.c, 0.4.0): a host with no firmware list, the lists and filters (OR / AND), banks, load /
unload / active, levels and voices (the slow blink), the library's edits and its blob (a round trip, every damaged
byte refused), the catalog's marks and downloads, FX setup's pages and actions, the project's FX block (version 5,
version 4 still loading), states, the sequencer's MIDI track; then the mockup's 34 gallery screens set up on a live
core and compared with the mockup's pixels (`move_fwb_ref.h`; `fw_*.pbm` in the build directory). Regenerate the
reference with `node tests/move_v2_ref.mjs move-firmware-browser-mockup.html [pbm-dir]`.

`move_engine` (0.14.0): the ENGINE knob page: `rfm_move_ep_check` (every truncation, fields out of range, 4000 bit
flips with the FNV fixed half the time, 3000 random blobs, each checked from a buffer of exactly its length), the
page's presence and the target slot, the jog's order, arriving, the knobs (every value format, lights, touch, turn,
Shift, a LIST, a DYN), the SETs merged per block, the watch, the page list, an engine changed, a layer off, the
description replaced and gone, the layer by channel (`engine_*.pbm` and `.leds` in the build directory).

`move_sampler` (0.15.0): the sampler app: the capability off (the audio loop as before), the SETs, opening, the record
key, the rows, three rows' second, the tabs, a popup, Track held + Sample, what passes to the layer under it (the same
actions and lights with the app open or closed), a held step and a knob's popup over it, a take, the events, the
FM-1 layout, the browser, a takeover, a Shift menu, the offer, the settings blob (versions 8 and 7), the lights, the
speech; the mock-up's six boards compared pixel for pixel (`move_smp_ref.h`, made from mock_recorder_v4.c and _v1.c
built per their header comments): equal but for the second tab's icon (Takes: not built here, so not shown) and the
popup's threshold mark (3 px right: -24 dB on the -60..0 scale is at 60 %, the mock drew it at 57 %)
(`sampler_*.pbm` and `.leds` in the build directory; the `.leds` have a last row: Sample, Record, Play, Loop, Mute,
Delete, Copy, Undo, as the mock-up's).

`move_kit` (stage 3a): the kit: the note encoding (every pad and pitch a note of its own, back again), the capability
off, Track 4's round and its held pads, the pads (play, choose, Shift, the right half Pitches / In Key / Off, a track
routed to a firmware), the steps (the chosen pad's only, a held step + pads, Delete + a step), the sequencer's notes
of a kit track, the knobs, the rows, popups and tabs, Files (places, a folder, the preview, a load, the answers),
Delete / Mute + a pad and Undo, a pad held + Sample, the sampler's To Kit, a held Track's hold on track 14, the project
(version 11 round trip, four bad kits refused, a version 10 project, a new one), the settings (version 9, a version 8
image), the layers over it, and thirteen of the mock-up's boards compared pixel for pixel (`move_kit_ref.h`): rest,
three, pitch, mode and attack (their envelopes masked: the pad's own proportions), choke, kittab (the tabs; its Load
and Save rows are 3b), inkey, places (Kits is 3b), files, padact (the wave; 3a's tools are Play and Delete), armed,
recording (`kit_*.pbm` and `.leds` in the build directory).

## Later phases

- **FM1VST (Mac)**: a `MoveLink` beside `RemoteLink.cpp`: `mr_connect` / `mr_poll` on a thread, the actions into
  fm1panel_t, the LEDs and screen out each frame, sound on the Mac or (optional) the Move.
- **iOS (RFMKit)** and **Android (NDK)**: compile `move.c` and the vendored client as they compile libremotefm1.
- **The web player**: done over WebSocket (RemoteFM1's WEB-PLAN.md, "An Ableton Move as the controller": `web/build.sh`,
  `web/move-core.js`, RemoteFM1's `move-link.js`; `mr_client.c` compiles under Emscripten and the linker drops its
  sockets). From https: the `ws://` tried first (Chrome's Local Network Access), else Move Remote 0.1.1's bridge
  window (`MoveBridge`, WEB-PLAN.md).
- **The sequencer in the web player**: done (below, "The web player").

## The web player

`web/move_web.c` exports the sequencer (`mw_seq_*`, `mw_project_*`, `mw_act_frames`, `mw_blob`);
`web/move-core.js` wraps it (kinds 8..10 as `projectSave` / `projectLoad` / `projectDelete`, every action
with its `frame`).

**What the host does there.** The core stays on the page's main thread (the pads, LEDs and screen need it), so
`web/move-seq.js` (SeqClock) advances it ahead of the audio: every 10 ms and before each Move input, in blocks of 128
frames at the context's rate, up to the AudioContext's frame plus 30 ms (`LEAD_S`). An action at `frame` in a block
starting at absolute frame `pos` goes to the worklet as `{type: "midi", bytes, at: pos + frame}`; the worklet
(RemoteFM1's `web/site/worklet/host.js`, TimedMidi) gives it to the engine before the render of the quantum nearest to `at`, so a
note lands within half a quantum (64 frames) of its frame, and the core only ever counts the frames it is given: no
drift. Measured (move-e2e, Felucca at 44.1 kHz, 8 bars of 16ths at 120 BPM): every note in the engine within 63.5
frames of its ideal, mean 0.3, none late. A gap over 250 ms (a frozen tab, a suspended context) is skipped, not caught
up. Without running audio the wall clock stands in (MIDI at once). The sequencer stopping, a disconnect, a lost link
and pagehide cut the worklet's queue (`midiCut`: queued note offs now, note ons dropped); a note off given now drops a
queued note on of the same note. Live pads are not delayed.

**The firmware's MIDI clock.** The worklet posts the firmware's MIDI out with the frame of the quantum that made it;
the page takes F8 FA FB FC and F2 out of it (SysEx skipped) and feeds each to `rfm_move_seq_midi_in` at that frame
plus the lead, in the core's timeline. In AUTO (a master: a start, then 24 steady clocks) the sequencer then follows the firmware's tempo, start and stop, its
notes the lead (30 ms) behind the firmware's own beat, steadily; 500 ms without a clock and it is on its own BPM again.
The Move section of the page says which (`clock INT` / `from the firmware`, and the BPM). Recorded pad notes land the
lead late too (the core runs that far ahead of what is heard); quantize takes it out.

| Firmware (web player) | Sends MIDI clock? |
|---|---|
| Hub firmwares on the emulated engine (fm1-emu) | no: fm1-emu models no MIDI out |
| Felucca (native) | no: receives clock and transport only (`midi_clock.c`); checked headless: PLAY, SEQ, ARP and keys for ~13 s of device time, no F8/FA/FC out |
| Melodee (native) | no: receive only, as Felucca |
| ChoralRoot (native) | yes, by default (Options > MIDI Clock: OUT, `cr_out.c`): F8 at its BPM all the time, FA / FC when its loop starts and stops; none with MIDI Clock IN or OFF |
| FIZZ (.rfm1) | yes while its sequencer plays on its own clock (SETTINGS CLOCK OUT, on by default): FA, F8, FC (`core/app/app.c` mo_clock) |
| fm1-base apps | only if the app sends it: the base's clock code (keremimo's, felucca-bsp `chip/usb.c`) is receive and transport sync |

**Projects.** IndexedDB `remotefm1-move`, store `projects`, a record a slot `{ slot, bytes, saved }`
(RemoteFM1's `js/move-projects.js`), apart from the firmwares' saves. At start the store's slots go to `rfm_move_project_dir`
and the last project opened (prefs `moveProject`) to `rfm_move_project_load`; PROJECT_SAVE / DELETE are written in
order, PROJECT_LOAD is answered from the store; the autosave is flushed on disconnect and pagehide. The page's Move
section lists the projects ("Project N": the blob has no name), with their size and date, Backup, Delete, Back up all
and Import (into a picked slot or the first free one; a set into its own slots).

The file (`.rfmmove` one project, `.rfmmoveset` several), little-endian:

| Bytes | Field |
|---|---|
| 4 | `RMVP` |
| 1 | version, 1 |
| 1 | kind: 1 one project, 2 a set |
| 2 | count, 1..32 (1 for kind 1) |
| per entry: 1 | slot 0..31 (unique) |
| 1 | flags, 0 |
| 4 | saved, Unix seconds (0 unknown) |
| 4 | len, 1..39072 |
| len | the core's blob (`RMSQ`, as `rfm_move_project_save` writes it) |

Nothing may follow the last entry; a file over 8 + 32 x (10 + 39072) bytes is not read; every blob must pass
`rfm_move_project_check`, or nothing is imported.
