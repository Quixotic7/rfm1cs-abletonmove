# Changes

Versions are git tags (`vMAJOR.MINOR.PATCH`); hosts take this repository as a submodule pinned to one.
`RFM1CS_VERSION` in `include/rfm1cs/move.h` and `rfm_move_version()` name it.

## 0.6.8 (2026-11-19)

Knob speech, the second sweep (ChoralRoot 0.42 rd1). New fields at the end of `rfm_move_t`: rebuild everything.

- **A readout belongs to its page:** it is kept with the FM-1 buttons held when it came (the firmware's Shift page:
  GLO) as well as the control's label, so Shift + knob 1 no longer says the plain page's "FM6: FM BELL" (readouts
  given for control -1 and paired with the knob in use included); Shift + knob 6 still gets its own readout.
- **Each words once per touch-and-turn:** a readout answering a turn with the same words is not said again at once;
  the range's end repeat (a turn with no new words within 250 ms) says "Name: value" once, and nothing when no value
  is known (was the label again: "Presets", "Presets").
- **The full name on touch:** the full titles seen are kept by control, page and label (24); a touch before this
  page's readout says the title seen ("Bass Register"), else the label ("Bass Regis").
- Build warnings (the host's arm64 GCC): Global Settings' scroll bar folded a `/ 0` in a branch never taken (five rows
  fit; not a runtime bug), now guarded; fw.c's compile-time page check compared two enum types, now as ints;
  `rfm_move_save`'s definition now matches its array-parameter declaration (`-Warray-parameter`). Clean with Arm GCC
  14.2 `-Wall -Wextra -Wshadow`.

**Tests:** `move_speech` 285 checks: the plain page's preset readout, Shift held (its own readout, the plain one never
said), the same readout back said once by the range's end repeat, each page's title on touch, a turn with no value
known saying nothing; the 0.6.7 cases updated (the same value once, the full title on the plain page).

## 0.6.7 (2026-11-19)

Knob speech with firmware readouts (ChoralRoot 0.42's rd1; the owner: "the screen reader is not saying the parameter
name or value"). New fields at the end of `rfm_move_t`: rebuild everything. The screen is unchanged.

- Never the Move's placeholder: a turn short of a detent says nothing (was "Voicing: 0"); once the firmware has sent
  any knob readout, a turn says nothing until its readout comes, then "Bass Register: +1 oct" once (was "Bass Regis:
  +1" too).
- Never a stale readout: each knob readout keeps its control's label as it came; when the label is another (the
  firmware's Shift page, "Opt") the old page's readout is not said (was "Voicing: +1" before "split point: F# split
  off").
- A touch says "Name: value" when the control's readout is known (its full title, not the 10-character label), else
  the label.
- Every turn is heard: a readout answering a turn is said even with the same words; a turn with no readout within
  `RFM_MOVE_SAY_TURN_MS` (250 ms; the range's end) says its "Name: value" again once (a control with no readouts at
  all: its count then).

**Tests:** `move_speech` 269 checks: the first touch and turn, a turn waiting for its readout, the same value said
again, the range's end once, a touch with the value, the Shift page's relabel (no stale readout), another knob, back
on the plain page, readouts cleared.

## 0.6.6 (2026-11-19)

MIDI from one firmware routed to other firmwares and to "Out" (the host's MIDI out; on the Move USB-A and network
MIDI), set on the Routing page (the owner: "hold down a
firmware pad and then click the other firmware pads to enable midi routing to them ... Use the 4th slot at the bottom
of the column ... to route the midi to the main output"). New fields at the end of `rfm_move_fxset_t` and
`rfm_move_act_t`; the project record is version 7 (3 bytes more: `RFM_MOVE_PROJECT_MAX` 40139, was 40136): rebuild
everything; hosts that wrote the number down (the web's `PROJECT_MAX`) update it.

- **The gesture** (FX setup, Routing; column 3): hold a firmware's pad, tap another firmware's pad or the new **Out**
  pad (row 4, under them): that route on, or off again. A plain tap still selects. While held the targets and Out
  light cyan; at rest Out is dark (faint cyan when the selected firmware routes out) and the chain marks a routing
  firmware's box (a notch top right: to a firmware; bottom right: to Out).
- **No cycles:** a route closing a loop (A to B while B reaches A, directly or through the third) is refused: the
  popup "Loop: not set", the words "Loop", nothing changed.
- **The state:** `rfm_move_fxset_t.mroute[3]`: bits 0..2 the firmware slots (never its own), `RFM_MOVE_MROUTE_OUT`
  (8) Out, the host's MIDI out. Saved with the project (version 7; versions 1..6 load with no routes; a blob with an own bit or a
  cycle is refused). Every `RFM_MOVE_ACT_ROUTE` carries all three masks in the new `act.mroute[3]` (the Move's
  toggle, `rfm_move_route_set`, `rfm_move_midi_route_set`, `rfm_move_fx_sync`, a project load).
- **API:** `rfm_move_midi_route(m, slot)` (the mask, -1 no slot); `rfm_move_midi_route_set(m, slot, mask)` (-1
  refused: out of range, its own bit, a cycle, no FX setup; 0 nothing to change; 1 done and on screen).
- Popup (full screen by default): "FW1 ChoralRoot", "MIDI to", the target large, "on" / "off"; speech "ChoralRoot to
  Oberwave, on", "ChoralRoot to Out, off", "Loop".
- Web: the route action has `midiRoute: [3]` (`MW_ACT_EXT` 24); `PROJECT_MAX` 40139.

**Tests:** `move_fw` `test_mroute`: the gesture (three targets, Out, off again, the focus kept, a plain tap selects,
Out alone nothing), the LEDs held and at rest, the cycle refused (direct and through the third), the act's masks,
the host's edit and its refusals, save / load, version 6 loading with none, a blob with an own bit or a cycle
refused, no FX setup refusing; `move_speech` the words; `web_smoke` the route action's `midiRoute`.

## 0.6.5 (2026-11-19)

Shorter speech (the owner: "it should just say: "Step 1", "Step 2", and when playing notes, I can hear the notes,
there is no need to say out loud the names of the notes").

- A step pressed (tapped, held, a second step stretching a held one) says only "Step 5": no "on" / "off", no note
  names, velocity or length. Undo and Redo no longer add the steps they changed.
- Nothing is said for playing: pads and keys in any mode, steps as keys, notes recorded live (the step words come
  only from a step press now, not from notes landing on steps).
- A held step's Up / Down says "Transpose up" / "Transpose down" (Shift: "Octave up" / "Octave down").
- Unchanged: cut and paste ("Step 5 cut", "Pasted to step 9", "Step 9 replaced"), Delete + pad ("Deleted C3": an
  edit, not playing), a drum pad's note edited with the jog.

**Tests:** `move_speech` 245 checks: pads, keys and steps played in every mode and notes recorded live say nothing;
the input walk exempts note playing; the step words.

## 0.6.4 (2026-11-19)

- Delete tapped alone (or held and let go alone) shows nothing new: 0.6.3's "Hold Delete / + a step" message broke
  the v2 grammar (symbols at rest, words only while something is touched or turned) and is gone; the hint is
  spoken only. A lone Delete changes nothing (0.6.3, a5c7d4b; af8e035's "cuts the selected clip" was replaced
  before the tag moved).
- While Delete or Copy is held the screen is the design canvas's own (V2Screen.dc.html `seq.clip`: "the 16 steps as
  cells (only while Copy / Delete / a step is held): notes filled, empty outlined, copied dashed, cut crossed"), as
  the track view has drawn it since 0.5.x (the steps always shown, the owner's); a copied or cut group marks each of
  its steps (0.6.3).

**Tests:** `move_speech` 240 checks: a lone Delete leaves the screen byte for byte as it was; Delete held, a cut, a
paste drawn (`delete_*.pbm`, the contact sheet `rfm1cs-0.6.4-delete.png`). `move_seq`: a lone Delete raises no
message.

## 0.6.3 (2026-11-19)

Speech everywhere, and Delete is cut everywhere on the sequencer (the owner: "many commands with no speech, like
selecting steps, pressing back, sampling. Also since when does the delete button clear the steps?"). New fields at
the end of `rfm_move_t`: rebuild everything.

- **Delete is a modifier only** (the owner: "If I quick click delete or hold the delete key and release it deletes
  every step on the track"). Since 0.1.0 (f79e90c), after Move's own Delete, **Delete alone deleted the selected
  clip**: a tap, or a hold let go with nothing else pressed. Now it changes nothing and says a hint
  ("Delete: hold and press a step, pad or clip"; 0.6.3 also showed one, dropped in 0.6.4). Delete held + a step or clip has cut since 0.1.0,
  unchanged (no regression); since 0.6.1 speech said a cut step as "Step 5 off", now "Step 5 cut".
- **Cut and paste repeat** (the owner: "I can only ever paste once"): since 0.1.0 a pasted cut emptied the clipboard.
  Now a paste keeps it (the cut's X marks go), so it pastes again and again until something else is cut or copied.
  **Delete held + steps follows the last action** (the owner's): right after a cut, any step gets the clipboard
  (a step with notes is replaced); otherwise (the first press in a hold, or after a paste) a step with notes is cut
  and an empty one gets the clipboard (an empty clipboard: nothing). Letting go of Delete forgets the last action;
  the clipboard stays. So: cut A, A again pastes it back, empty steps paste, B cuts, C right after is pasted over, C
  again cuts. Several steps copied in one Copy hold are one group, pasted with its gaps. Words: "Step 5 cut", "Step
  5 copied", "Pasted to step 9", "Step 9 replaced".
- **Track 4 held + Delete** (let go, no step) clears the selected track (the owner: "the right shortcut"); Track 4
  held + Delete + a step clears that track (as before). Both said: "Track 1 cleared". The only Delete gesture that
  clears without the clipboard.
- **Speech:** an input walk (every button, step, pad, knob, jog, jog click, touch in 34 contexts) found 59 inputs
  that changed the screen silently; all speak now: a Track pressed under a Shift menu ("FM-1 mode, Tempo"), the
  projects view's or FX setup's Track pressed again (the view named), Back clearing a selection (the view named),
  Back that changed nothing (where you are), Delete + pad, a second step while one is held (the step's words now
  carry its velocity and length), the states' pads ("State 1 saved", the core's now: hosts drop their own), a Shift
  menu's knob touched (its item).
- README: a table of every input and its words.

**Tests:** `move_speech` 237 checks: the input walk (2002 inputs, none silent when the screen changes); Delete tapped
and held-then-let-go leave every step and clip as they were (track view and Session); the owner's Delete sequence word for word (cut, paste
back, paste on, cut, replace, cut), a new hold, an empty clipboard; a Copy group pasted twice; a copy pasted twice
and undone one by one;
Track 4 + Delete and Track 4 + Delete + a step; every word. `move_seq`: Delete alone changes nothing.

## 0.6.2 (2026-11-19)

Global Settings (Shift + step 2; Track 1 held + step 2 outside Sequencer mode) as a list on the jog wheel, and it
owns the controls while open (the owner: "the knob functions are passing through ... it should be a scrollable list
you interact with using the Jogwheel"). New fields at the end of `rfm_move_t`: rebuild everything.

- **Nothing passes through:** while it is open no knob turn or touch, pad, step, volume or Move button reaches the
  FM-1, the firmware or the sequencer (before, pads, steps, buttons and the volume knob did, and a knob's touch showed
  its FM-1 popup over the menu). Releases pass, so a pad held before it opened lets go; the Track buttons and Shift
  + step 2 (its own toggle) pass.
- **The list:** Clock, Velocity, Fixed level, Popup size, Battery (read only); name left, value right; the jog moves
  the selection (inverted; it scrolls past five rows); a click edits the value (inverted, the row framed), the jog
  changes it; a click or Back leaves the edit; Back closes the menu when not editing. The knobs are dropped as
  shortcuts (they do nothing here, their rings are off).
- **Speech:** opening says "Global settings, Clock: Internal"; each move the row ("Popup size: Full"); a click
  "Editing Velocity: Dynamic"; each value turned the new value ("Velocity: Fixed"); leaving the edit "Done".
- Fix: closing a Shift menu opened outside Sequencer mode no longer says "Sequencer mode" when it opens (0.6.1).

**Tests:** `move_speech` 201 checks: in FM-1, MIDI and Control mode every knob (touch and turn), pad, step and Move
button with the menu open reaches nothing (a pad held before it lets go), the knobs work again once it closes; the
list's moves, edits, Back, the battery row, its toggle, and every word said; `global_*.pbm` for the contact sheet.
`move` and `move_seq`'s Global Settings checks use the list.

## 0.6.1 (2026-11-19)

Speech for the buttons and the sequencer (the owner's first try on the Move: "Knobs work, but there's no speech for
the buttons or the sequencer"). New fields at the end of `rfm_move_t`: rebuild everything. Speech off: unchanged.

- **Every FM-1 button press is said at once**, in every mode that plays the FM-1: its live label, else its static
  name (`rfm_move_set_button_names`), else the panel's word ("Play", "Record", "Select", "Octave down"). The label's
  popup after 300 ms is not said again.
- **Values said when they change** (a button, a menu, the clock): the selected track ("Track 3", ", muted", ",
  solo"), the bar shown, Session's scenes and tracks, the octave, Control mode's bank, layout, root, scale, channel,
  velocity ("Velocity: Fixed 100"), the TRIG page, latch, tempo, Step record, Sample's take and audio loop, Loop's
  page edit, Fill, Shift's lock, the clipboard ("Copied step", "Cut clip", "Pasted").
- **The sequencer:** a step on or off ("Step 5 on", also from Undo or a paste), mutes and solos ("Track 3 muted",
  not twice with the held Track's menu), clips ("Track 1, clip 2 queued", "Track 1, clip 2 playing", "Track 1, stop
  queued", "Track 1 stopped", "Scene 3 playing"; at the bar, from `rfm_move_seq_advance`), the modifiers pressed
  ("Shift", "Copy", "Delete", "Mute"). The Shift menus are views ("Tempo", "Arp", "Groove", "Workflow", "Keys and
  Scales", "Velocity", "Record mode", "Global settings"; closing one names the view again) and say their knobs
  ("Pattern: Down", "Arp: On", "Mod lane", "Groove: 5%", "Quantize: 50%", "Record style: Replace").
- `rfm_move_set_latch` from inside the core no longer says latch twice. The projects view says only its own words
  (a project loaded there changes many values at once).

**Tests:** `move_speech` 165 checks: a test per row of the remoteFM14Move spec and per button family (the FM-1's
buttons, the arrows' octave, the jog's bank, the drum pad's note, the Shift menus, steps, a held step, Loop, pages,
Copy, Delete, Track 4's select / mute / solo, Play, Record, Sample, Session's launches and stops at the bar, scenes,
the project grid, the browser's filters, bank, favourite and tags, FX module pages), each failing if it says
nothing.

## 0.6.0 (2026-11-19)

Words for a screen reader (asked by remoteFM14Move for Schwung's screen reader). New fields at the end of
`rfm_move_t`: rebuild everything. Off by default: a host that never turns it on gets no new action and nothing else
changes (a test runs one script with speech off and on: the same other actions, the same screen).

- **`rfm_move_set_speech(m, on)`**, `rfm_move_speech(m)`, `RFM_MOVE_HAS_SPEECH` (hosts `#ifdef` it). On, the core
  emits **`RFM_MOVE_ACT_SAY`** (30, a `#define`): `index` `RFM_MOVE_SAY_EVENT` (the base view changed: "MIDI mode",
  "Firmwares, Slot 1, Felucca", "FX setup, MIDI FX", "Sequencer mode", "Session") or `RFM_MOVE_SAY_VALUE` (a popup's
  or the selection's words changed: "Volume 75%", "Scale: Dorian", "CC 16: 65", "Felucca, not installed",
  "Downloading Felucca 25%"), `data` / `len` the words (printable ASCII, NUL-terminated, shorter than
  `RFM_MOVE_SAY_MAX` = 256, valid during the callback). A popup closing over a view says nothing; the same words are
  not said twice while they are up; a new message (Undo twice) is said again.
- Words are made at the end of `rfm_move_input`, `_popup`, `_readout`, `_fw_progress`, `_fw_slots`, `_fw_states`,
  `_set_latch`, `_fw_set_active`, `_set_mode`, `_seq_play`; `rfm_move_seq_advance` only checks the transport (an
  external clock's start and stop) and a popup the clock brings up (a held step's, a held button's label after
  `RFM_MOVE_LABEL_HOLD_MS`, the external tempo's message), once each. Turning speech on takes the screen as said (the
  host names the first view).
- **`rfm_move_words(m, out, cap)`**: the screen now in words (the view, then its popup or selection), for a "say it
  again" and the tests; works with speech off.
- The core says its own load and download progress ("Loading Felucca", "Downloading Felucca 25%" a quarter at a time,
  "Felucca installed", "Felucca loaded", "Felucca failed", "Too busy"); states saved / loaded / deleted stay the
  host's.
- The web wrapper: `core.setSpeech(on)`, `core.speech()`, `core.words()`; actions `{ kind: "say", event, text }`
  (`mw_say(i)` holds a call's words); `readout`, `popup` and `fwStates` now return their actions.

**Tests:** `move_speech` (a ninth suite, 100 checks): speech off says nothing and on changes nothing else; the modes;
a knob's popup closing; a readout (and the same again); a held button's label (and a tap); a host popup; the volume;
Control mode's knob and Track 3's CC number; MIDI's held menu (scale, root, channel) and latch; the browser (Back,
Tracks, All, a pad, a 40-character name said whole, the catalog, the description, a download's quarters, installed,
failed); Back + Track; FX setup (slots, the module list, a parameter, the routing, the Mixer); the sequencer (Play,
Stop, a held step, Undo twice, Redo, Track 4 + step, Session); `rfm_move_words` along the way; every SAY's words
printable, trimmed and short enough. `web_smoke`: 35 checks (speech off and on, popup and readout words).

## 0.5.11 (2026-11-19)

The web wrapper catches up (gaps found by remotefm1.com on 0.5.10). The core is unchanged.

- `fwList` entries take `virt` (`mw_fw_entry`'s new last argument): Fantasy CFWs get their badge and step 16's filter
  on the web.
- Labels and readouts on the web: `setLabels(49 | null)`, `setButtonNames(14 | null)` (packed through
  `mw_set_labels` / `mw_set_button_names`, 18 bytes an entry), `readout(control, kind, title, value, frac, flags,
  ms)`, `readoutsClear()`, `popup(text)`; constants `LBL_N`, `NBTN`, `READOUT`.
- `fwSetLevel(slot, level)` (`mw_fw_set_level`): the page's volume sets the slot's level as the Move does
  (`fwLevel`, the library); null when refused.

**Tests:** `web_smoke` (30 checks): `fwSetLevel`, `virt` drawing the badge, a readout's popup and its time, a host
popup, clearing; its FX setup check now picks a module the 0.5.0 way (slot pad held + jog, let go) and passes.

## 0.5.10 (2026-11-19)

One firmware, no FX, for the hosts that are not the Move (remotefm1.com, the iOS and Android apps). `rfm_move_fw_state_t`
has a new last field (`caps`): rebuild everything. Nothing changes for a host that does not call the new function.

- **`rfm_move_fw_caps(m, caps)`** (and `rfm_move_fw_caps_get`): `RFM_MOVE_CAP_SLOTS` (three firmware slots) and
  `RFM_MOVE_CAP_FX` (FX setup: MIDI FX, audio FX, routing, the Mixer); `RFM_MOVE_CAPS_ALL` the default. With 0 the
  firmware browser is as ever and loads into slot 0; Tracks 2 and 3 are dark and inert in the projects view; Back +
  Track, Down (FM-1 mode) and Track 2 / 3 held + volume switch or set nothing; slot ids past 0 (the host's, the
  library's) are dropped and voices are 1; Session never opens FX setup (a firmware track: out, as Back); no
  `FX_LOAD` / `FX_PARAM` / `MIX` / `ROUTE` is emitted; `rfm_move_fw_set_active` / `_fw_set_level` refuse a slot past
  0, `rfm_move_fx_set_*`, `rfm_move_route_set` and a pan or send in `rfm_move_mix_set` refuse. The level and load
  popups drop "Track 1". Safe at any time (an open FX setup closes, slots 2 and 3 empty).
- **Web**: `mw_fw_caps` / `mw_fw_caps_get`, `MoveCore.fwCaps(caps)` / `fwCapsGet()`.

**Tests:** `move_fw` `test_single`: the default, a three-slot library (slot 3 played) loaded into one slot, the Track
LEDs, Tracks 2 / 3 and their volume, a browser load into slot 0, the catalog, Session out, Back + Track and Down in
FM-1 mode, every host-edit call, `fx_sync` and a project load emitting nothing of FX, turning it off with FX setup
open. Screens `fw_single_*.pbm`. `web_smoke` checks `fwCaps`. (`web_smoke`'s "Audio FX: a module loaded" check fails
as in 0.5.9: it picks a module the pre-0.5.0 way; not changed here.)

## 0.5.9 (2026-11-19)

The FM-1 popups over the whole screen (the owner's, with a setting). `rfm_move_settings_t` has a new last field and
the settings image is version 5, 87 bytes (`RFM_MOVE_SETTINGS_BYTES`): rebuild everything; a host that stores the
image in a fixed buffer sizes it by the constant.

- **Whole screen by default**: FM-1 mode's (and MIDI's NOTES view's) knob popups (the name, the detents, MASTER),
  labels (held, the jog touched, a button held) and readouts (a knob's, PRESETS's, a preset's, a message) are drawn
  128 pixels wide with bigger words: the name on a 2x row, the value at 4x or 3x (else two 2x rows), a 3-pixel bar
  along the bottom (bipolar: from the marked middle); a label or message one row at 4x or 3x, two at 3x, else 2x
  rows. Never 1x; a row that does not fit scrolls. A label with no words leaves the FM-1's screen on the right.
- **The setting `pop_half`** (Global Settings, knob 4: `Popup Full` / `Popup Half`): 1 the left half as 0.5.6..0.5.8,
  the FM-1's screen staying on the right (its layout uses the same bigger sizes where they fit: 3x labels). Saved in
  the settings (version 5, its last byte); older images (versions 1..4) load as the whole screen.
- **Global Settings' screen**: three rows now (Clock, Vel, Popup) under the battery; four knob rings; the battery two
  pixels right.

**Tests:** `move` `test_popup_width` (1025 checks): the default, save and load, a version 4 image, knob 4 (and its
end), the same popups in both widths (right half covered or the FM-1's screen kept; the bars' widths), a label with
no words. Contact sheets `move_popups_full_sheet.pbm`, `move_popups_half_sheet.pbm`; `move_global_popup_*.pbm`. The
0.5.6..0.5.8 tests and `test_screen` run with `pop_half` 1; the knob and master goldens changed (the half layout's
name row and value moved); `move_seq`'s Global screen's battery checks moved 2 pixels.

## 0.5.8 (2026-11-19)

The owner's feedback on 0.5.6 on the Move. `rfm_move_t` has two new last fields: rebuild everything.

- **Knob readouts, one rule**: every knob, PRESETS included, shows what it controls (the readout's title, else the
  knob's label) and then the value, whatever the readout's kind (a PRESET or MESSAGE readout for a knob too, in its
  own popup as well). A readout with a title and no value: the title alone, as a knob's name.
- **The jog wheel touched**: a mapped button pressed shows its label at once (and acts as always). The jog's touch
  has its own flag (`jog_touch`): a knob touched meanwhile does not undo it.
- **Otherwise the hold rule** (300 ms), and while a button is held, other controls at once, as 0.5.6.
- **Fix: words shown while held, never only after the release.** The label popup now comes up on the press whatever
  the label is then, and draws the label as it is when drawn: a firmware that gives a button its words once it is
  pressed (Elasticat) shows them while it is held, live as they change. A label that had no words while held shows
  nothing after the release (0.5.6 showed a second of whatever the label had become). A button pressed again during
  its own label's last second starts afresh (0.5.6 kept the old popup).

**Tests:** `move` `test_labels_058` (1007 checks): the jog's at-once rule (with a knob touched meanwhile, a tap),
the hold rule without it, a label named on press and renamed while held, a label named only at the release (nothing
after), PRESETS's readout with no title (its label, then the name), its own popup, a MESSAGE-kind readout on K2.
Contact sheet `move_labels058_sheet.pbm`.

## 0.5.7 (2026-11-19)

Readouts (the labels plan's phase 1, rfm1cs's half; the owner: "the name of what the knob is actively controlling
and also the value"). `rfm_move_t` has new last fields: rebuild everything. No format changes.

- **`rfm_move_readout(core, control, kind, title, value, frac, flags, ms)`**: kind `RFM_MOVE_RD_PARAM` / `PRESET` /
  `MESSAGE`; control a DYNAMIC-LABELS slot 0..48, or -1: the control in use (a knob touched, or turned or the jog
  within `RFM_MOVE_RD_PAIR_MS` = 250 ms); frac 0..1000 or -1; flags `RFM_MOVE_RD_BIPOLAR`; ms its own popup's time
  (0: a second). Printable ASCII, 16 characters kept; title and value both empty: ignored.
- **A knob keeps its last readout** (slots 0..7: SELECT ALGO PRESETS K1..K4 MASTER): its popup shows it in place of
  the detents, touched (what it controls, the last value) or turned (live as new ones come). None yet: as 0.5.6.
- **Its own popup** (`POP_READ`): a readout with no control in use, a button's or key's, or a knob's whose popup is
  down (kept too). FM-1 mode and MIDI's NOTES view only; elsewhere a knob's is only kept.
- **Layout, the left half, never 1x**: the title on row 1 (scrolling when long), the value at 4x or 3x when it fits,
  else up to two 2x rows; a 2-pixel bar along the bottom with frac (bipolar: from the middle, which is marked). A
  message (or a readout with only one of the two): its words wrapped onto up to three rows.
- **`rfm_move_readouts_clear(core)`**: forget them (a slot switch, a firmware loaded or unloaded).

**Tests:** `move` `test_readouts` (989 checks): paired with a touched knob, kept for a later touch, live while
turning, bipolar, a scrolled title, a preset's popup and its time, a long name, a message, a knob's with its popup
down, pairing within and after 250 ms, the jog, ignored when empty, other modes, cleared. Contact sheet
`move_readouts_sheet.pbm`.

## 0.5.6 (2026-11-19)

The firmware's labels on the Move (the labels plan's phase 0; the owner's answers). `rfm_move_t` has new last
fields: rebuild everything. No format changes.

- **`rfm_move_set_labels(core, lbl[49])`**: the live labels in DYNAMIC-LABELS order (`RFM_MOVE_LBL_*`: 0..7 knobs by
  FM1P_E_* role, 8..21 buttons by FM1P_B_*, 22..37 white keys F3..G5, 38..48 black keys F#3..F#5), as the host's
  `fw_label(fw, i)` gives them. NULL entry: none given; "" = no job (nothing shown). The knobs' entries also set
  `rfm_move_set_label`'s (which keeps working; its "" is still the default name). NULL array: all cleared.
- **`rfm_move_set_button_names(core, names[14])`** (FM1P_B_* order): a button's name when its live label is NULL.
- **FM-1 mode**: a mapped button held 300 ms (`RFM_MOVE_LABEL_HOLD_MS`) shows its label (a tap: nothing); while
  any FM-1 button is held every other button, pad or step (its key) and knob pressed shows its label at once, read
  when drawn (a label the firmware changes meanwhile shows changed). Up while held, a second after the release.
- **Knob touch** (FM-1 mode and MIDI's NOTES view): a touch alone shows the knob's name, no value yet (MASTER its
  level); turning shows the detents as before. A knob labelled "" shows no popup.
- **Left half only, 2x only**: a name that does not fit scrolls (still 0.7 s, 30 ms a pixel, still, again); the
  knob popups' 1x fallback is gone. A button's or key's label wraps at spaces onto up to three rows.

**Tests:** `move` `test_labels` (967 checks): the tap and the 300 ms hold, keys only while a button is held, a
static name, a live rename, "" for a button and a knob, the knob touch, the scroll, other modes, cleared labels.
Contact sheet `move_labels_sheet.pbm` (and `move_labels_*.pbm`). The knob and master goldens changed ("Cutoff" and
"Master" scroll at 2x now instead of 1x); FM-1 mode's touch checks now expect the name.

## 0.5.5 (2026-11-19)

The sequencer runs on its own clock (the owner's: it locked up and stalled following a firmware). `rfm_move_seq_t`
has new fields: rebuild everything. Projects: header byte 5 bit 0 now set on save (older readers ignore it).

- **The cause**: the host feeds the active firmware's realtime MIDI out to the core. ChoralRoot 0.42's looper sends
  FA on loop play and FC on stop with no steady F8. In EXT (and AUTO, since FA followed at once) FA stopped,
  relocated and waited for clocks that never came: notes hung; F8 bursts then fired a run of ticks at once (repeats).
- **INT is the default**: a new project, and a project saved before 0.5.5 with AUTO (then only the default; byte 5
  bit 0 clear), run on the internal clock. A project saved since keeps the source it was given.
- **AUTO follows a master only**: a Start (FA) or Continue (FB), then 24 steady clocks (each gap under 125 ms, the
  average 20..300 BPM); then it follows at the master's measured tempo and starts at the master's next bar. FA / FC
  with no clocks, clocks with no start, a burst, or clocks too slow never take the transport; FC only clears the
  start seen. Following ends at FC or 500 ms with no clock, as before.
- **EXT made safe**: following and playing (or waiting to start), 500 ms with no clock lets every sounding note go
  (`ext_stall`); the position holds and goes on when clocks come back. Catching up is capped at 3 clocks (12 ticks)
  a block out loud: the rest of a burst moves the position silently (note ons skipped; offs and locks as usual).

**Tests:** `move_seq` `test_clock_055` (735 checks): AUTO with FA/FC only (the transport stays ours, its FC
stops nothing), 23 vs 24 clocks, a burst, gaps over 125 ms; EXT with FA/FC only, a clock stopped mid-bar (released
at 500 ms, held, resumed), a burst of 48 F8s (the position +48 clocks, at most one note); an old project's AUTO
loading as INT. `test_external`, `test_record_8_bars` and `session` set the source they test.

## 0.5.4 (2026-11-19)

A host's popup, latch kept per slot, Fantasy CFWs in the browser (the owner's). `rfm_move_t`, `rfm_move_act_t`
and `rfm_move_fw_t` have new last fields: rebuild everything. No format changes (the library blob reads as before;
see the ninth tag below).

- **`rfm_move_popup(core, text)`**: a host's message ("FM-1 linked" / "FM-1 lost") shown as a mode's name is: big,
  full screen, a second, over any screen (the projects view and Track menus too); a newer popup replaces it.
  Printable ASCII kept (31 bytes at most), wrapped at a space onto two rows, each cut where it does not fit.
- **Latch per slot**: a latched note keeps the firmware slot it was played on. Switching the active slot leaves it
  sounding there (nothing sent) and the new slot can play the same note. A new chord and a re-press act on the
  active slot's latched notes; off, a mode change, a project load and `rfm_move_release_all` let every slot's go.
  A latched note's note off carries `rfm_move_act_t.dest` = its slot + 1 (MIDI, track 0); 0, as always, is the
  active slot. Up to 96 latched notes (`RFM_MOVE_LATCH_MAX`, 32 a slot).
- **Fantasy CFWs**: `rfm_move_fw_t.virt` 1 marks a virtual firmware (Elasticat, Ripple). A diamond badge in its
  grid cell and on the name view's image (library and catalog). Step 16 is their filter, built in like the
  categories (magenta; one choice, as 0.4.2's model): the user tags are steps 8..15 now. A library whose ninth
  tag (step 16) was made: it moves to the first free step 8..15 with its marks, or goes when all eight are taken.
  Capture + step 16 shows whether the firmware is one, toggles nothing.

**Tests:** `move` `test_latch_slots`, `test_host_popup` (938 checks); `move_fw` `test_fantasy` (543): the filter,
its words and LED, both badges, the catalog, Capture, the ninth tag moved. Tags' screens (`capture_held`,
`capture_held_step_9`) differ from the mockup now (step 16 always there). Renders: `move_host_popup*.pbm`,
`fw_fantasy_*.pbm`.

## 0.5.3 (2026-11-18)

Latch for MIDI and Control modes (the owner's). `rfm_move_t` has new last fields (`latch`, `nlat`, `lat_ch`,
`lat_n`): rebuild everything. No format changes.

- **Track 2 held + the top row's pad 8** (it was free: pads 1..4 pick the layout) toggles latch, with a full-screen
  "Latch on" / "Latch off" popup (over Track 2's menu); the pad is amber, bright while on. Shared by MIDI and
  Control modes, RAM only.
- **While on**: a released pad's note keeps sounding (its hold kept). A press with no other pad down is a new chord:
  the latched notes go first. A latched note's pad pressed again lets it go and plays nothing. A framed "L" in the
  screen's top-right corner while on (MIDI, Control).
- **No stuck notes**: off, a mode change, a project load and `rfm_move_release_all` let every latched note go.
  FM-1 and Sequencer modes do not latch. New `rfm_move_set_latch`, `rfm_move_latch`.

**Tests:** `move` `test_latch` (920 checks in all): the toggle and its popup, a note latched, a new chord replacing
it, a re-press letting one go, off, a mode change, Control mode, release_all, a project load, FM-1 mode unlatched.
Renders: `move_latch_on.pbm`, `move_latch_midi.pbm`.

## 0.5.2 (2026-11-18)

Menu's toggle is named (the owner's). No API, format or struct changes.

- **Menu (Session) in the projects view** shows the Back flip's popup for where it lands, a second, full screen:
  "FX & Mix" into FX setup, "Firmwares" back to a firmware track, "Projects" back to Track 4. FX setup on its
  Mixer page is "Mixer", for Menu and for the Back flip alike (it said "FX & Mix" there). The first press in the
  view drops it, as the Back flip's. (`rfmv_fw_view_pop`: one place for both.)

**Tests:** `move_fw` `test_menu_names` (525 checks in all): into FX setup and back, from Track 4, onto the Mixer, the
first press dropping it; `test_flip_fx` expects "Mixer" on the Mixer. Renders: `fw_menu_to_fx.pbm`,
`fw_menu_to_firmwares.pbm`, `fw_menu_to_projects.pbm`, `fw_menu_to_mixer.pbm`.

## 0.5.1 (2026-11-18)

The owner's "8 bars played, 2 bars recorded, notes skipped". No API or format changes; `rfm_move_t` is larger
(`RFM_MOVE_SEQ_CAPTURE`): rebuild everything.

- **The cause: AUTO followed any clock.** In `RFM_MOVE_CLOCK_AUTO` (the default) a firmware's F8 alone made the
  sequencer follow it, and hosts pass the active firmware's MIDI out to `rfm1cs_session_midi_in`. A firmware that
  sends clock unasked at another rate (OberVave: its own sequencer's clock) then ran the Move's sequencer at that
  rate: a quarter as fast puts 8 bars of real time into 2 bars, and notes played closer than a step apart land on
  the same step (one kept). Now AUTO follows only while the external transport runs: from FA (start) or FB
  (continue) until FC (stop), which gives the internal clock back at once. F8 with no start is ignored; F2 is kept
  for the next FB. `RFM_MOVE_CLOCK_EXTERNAL` follows every F8 as before (the user's choice); INT ignores it all.
  Behaviour change: after the firmware's FC, AUTO no longer waits for its clock's bar on the Move's Play.
- **Capture keeps 4096 pad events** (was 1024): 8 bars of 4-note chords on every step were exactly 1024, so a phrase
  any longer lost its start. 16 bars of 4-note chords a step now fit. (`detect_bpm10`'s buffer is static.)
- The Capture record mode itself had no bar cap or wrap for 8 bars: an 8-bar take is 8 pages.

**Tests:** `move_seq` `test_record_8_bars` (707 checks in all): 8 bars of a note a beat, recorded in Capture mode,
on the internal clock and with a firmware's F8 at a quarter of the tempo (not followed: 8 pages, 32 notes); EXT
follows that clock; AUTO with FA follows and FC gives it back; Capture of 8 and 12 bars of 4-note chords whole.
`test_external` updated for the stop rule.

## 0.5.0 (2026-11-18)

Schwung modules' own parameter pages on the Move (the owner's): each `ui_hierarchy` level's knobs as pages of 8, Left
/ Right through them, every parameter reachable (was the first 8). The host plans the pages; see README "FX parameter
pages (0.5.0)". **Rebuild everything**: `rfm_move_fx_module_t`, `rfm_move_fx_param_t`, `rfm_move_fxslot_t`,
`rfm_move_fw_state_t` changed; projects are version 6.

- **API**: `rfm_move_fx_module_t.npages` / `.pages` (`rfm_move_fx_page_t`: a name, 8 knobs' parameter indexes, -1 a
  gap; at most 16); `rfm_move_fx_param_t.short_name`, `.lo`, `.hi`, `.decimals`, `.unit` (how a value reads). New
  fields are last, zero = as before: a 0.4 host compiles unchanged (positional initializers warn under
  `-Wmissing-field-initializers`) and gets pages of 8 in order.
- `RFM_MOVE_FX_PARAMS` is 128 (was 8). `FX_PARAM`'s `param`, `rfm_move_fx_set_param`'s `index` and the slot's
  `val[]` are by parameter index, 0..127; `rfm_move_fx_sync` sends all of them. New `RFM_MOVE_FX_KNOBS` (8),
  `RFM_MOVE_FX_PAGES` (16), `RFM_MOVE_FX_UNSET` (255), `RFM_MOVE_FX_PARAMS_V5` (8).
- **FX setup is one layer** (the owner's): pad column 1 the MIDI FX slots, column 3 the firmwares, column 5 the audio
  FX slots (rows 1..3 from the top); MIDI FX / Routing / Audio FX are no longer separate pages. `settings.fx_page`
  and `RFM_MOVE_FX_PAGE_*` keep their numbers and say which column is selected (MIDI 0, ROUTE 1, AUDIO 2); Track
  1..3 select those columns, Track 4 the Mixer (3). `rfm_move_fx_set_page` is unchanged. The screen is the chain.
- **Choosing a module**: hold an FX slot's pad and turn the jog (the module list, as before); letting the pad go
  loads the one shown, only if the jog turned and it differs (one FX_LOAD, not one a detent). Back while held closes
  the list. **Delete + pad** empties a slot; Delete alone does nothing now. The jog alone and the jog's click no longer
  open the list or load.
- **A module's pages**: the jog's click on a selected FX slot with a module opens them (page name and n/m, 2 x 4
  cells of label and value, gaps as dashes, a page strip); the jog turns the pages; Back (or another slot's pad)
  returns to the slots, a layer before the 0.4.5 flip. The knobs follow the slot's page (on the slots too); gaps do
  nothing; a touch shows the popup (0.4.5). The selected pad breathes while the pages are open.
- **A firmware selected** (column 3): the steps its 16-track filter (Routing's), the knobs 1 level, 2 pan, 3..5 sends.
- **The Mixer** (the owner's): pad columns 1..3 meter each firmware's chosen value bottom up in its colour (empty
  dark); the right 4 x 4 picks what knobs 1..3 set (row 1 Level, Pan; row 2 Send 1..3; the rest dark); knobs 1..3
  set it for slots 1..3, knobs 4..8 nothing. The screen: the chosen value's name, each slot's value and a bar. New
  `rfm_move_fw_state_t.mix_par` (`RFM_MOVE_MIXP_*`, RAM only) and `.fx_del`.
- **Projects version 6**: 128 values a slot (`RFM_MOVE_FXSET_BYTES` +720; `RFM_MOVE_PROJECT_MAX` 40136,
  `RFM1CS_FILE_MAX` 1284680; `move-core.js` `PROJECT_MAX` too). Version 5 loads: 8 values as parameters 0..7, the
  rest unset (255) until the module is known, then its defaults. A version 6 value over 127 other than 255 is refused.
- **Fix (the owner's): Track 4 held + a pad no longer changes the layout or scale**, in any mode; that is Track 2's
  job (Track 2 held, also in Sequencer mode, where it sets the selected track's layout). The pads do nothing while
  Track 4 is held. Test: `move` `test_track4_pads`.
- Not in 0.5.0: Schwung's preset browsers (`list_param` / `items_param`), `menu` lists, `visible_if`, the
  `ui_chain.js` UIs, and the web player's own module glue (`move_web.c` still gives flat lists: pages of 8).

**Tests:** `move_fw` `test_fx` (hold + jog choosing, the release loading, a hold without a turn, Delete + pad, a
firmware's filter and knobs, the Mixer's choice, meters and knobs; renders `fw_fx_hold_jog_choose.pbm`,
`fw_fx_mixer_level.pbm`, `fw_fx_mixer_send3.pbm`, `fw_fx_mixer_pan.pbm`), `test_flip_fx` (Back closing a held list,
nothing loaded) and `test_fx_pages` (514 checks in all; the jog's click in, the jog through the pages, Back out,
`fw_fx_pages_back_to_slots.pbm`): a 16-parameter module planned as 4 pages (root with a gap
and an overflow page, Advanced, Deep), Right / Left / Back, the gap, the knobs by page, the touch popup, the LEDs,
`rfm_move_fx_set_param` at 14 (and 16 refused), the no-plan pages of 8, `fx_sync`, a version 6 save and load of all 16,
a hand-made version 5 project (with the modules given before and after), a bad version 6 value. Renders:
`fw_fx_pages_root.pbm`, `fw_fx_pages_deep.pbm`, `fw_fx_pages_touch.pbm`, `fw_fx_pages_words.pbm`. `move_seq` and
`session` size checks follow the new maximum. 31 of 34 mockup screens unchanged: the two Mixer screens are
redrawn (the owner's Mixer), as is 0.4.2's filter popup.

## 0.4.5 (2026-11-18)

The Back flip is named, and FX setup stays where the user left it (the owner's). No API or action changes;
`rfm_move_settings_t` has a new last field `fw_fx` (rebuild everything). The settings image keeps its size and
version: `fw_fx` rides in bit 7 of the FX page byte (an older build reading it falls back to FX page 1).

- **A popup on the Back flip**, the Track mode popup's kind and second: going out it names the mode returned to
  (FM-1, MIDI, Control, Seq, with the mode strip); going in "Firmwares", "Projects" (Track 4) or "FX & Mix", full
  screen. The first press, turn or step in the view drops it.
- **FX setup is no layer Back closes.** In the projects view Menu (Session) alone toggles between the firmware /
  project pages and FX setup; a click of Back in FX setup flips straight out to the mode. Back still closes FX
  setup's module list first, and the other layers (Capture, States, the description, the selection, the catalog) as
  in 0.4.3.
- **FX setup is remembered across the flip**, on its page: left showing FX setup (by any way out), the view opens on
  it again (Back, Shift + step 1, Track 1 held + step 1 alike); left on a firmware or project page, it opens there as
  before. Saved in the settings (`fw_fx`), so it survives a restart.
- **FX setup's knob popups on touch** (the owner's): with a module selected (MIDI FX, Audio FX) or on the Mixer, a
  knob touched shows its parameter popup (name, value, a list's word) for as long as it is touched; a turn updates
  it; letting go drops it at once, as FM-1 mode's knob popups. A knob with no parameter there (past the module's
  count, the Mixer's knobs 6..8, the Routing page, the module list open): no popup. Untouched turns (a host without
  touch) keep the 1 s popup.

**Tests:** `move_fw` `test_fx_touch` (touch shows, held without turning stays, a turn keeps it, release hides, a knob
with no parameter, the Mixer's Pan; renders `fw_fx_touch_param.pbm`, `fw_fx_touch_mix_pan.pbm`; 462 checks in all)
and `test_flip_fx`: the popups both ways (FM-1, MIDI, Firmwares, Projects,
FX & Mix) and the first press dropping it, Back out of FX setup, Back in to FX setup on the Mix page, the settings
round trip, Menu still toggling, a module list closed first, Track 1 held + step 1 opening FX setup; `test_back` and
`test_fx` updated. Renders: `fw_flip_out_mode.pbm`, `fw_flip_in_firmwares.pbm`, `fw_flip_in_fx.pbm`.

## 0.4.4 (2026-11-18)

The FM-1 mode button layout the owner chose: Back flips (as in 0.4.3), Up is SEQ, Down cycles the firmwares. No API,
format, action or `rfm_move_t` changes.

- **Up presses the FM-1's SEQ in FM-1 mode** (press and release like the other mapped buttons; its LED shows SEQ's
  level). `rfm_move_button_of(MR_CC_UP)` is now `RFM_MOVE_B_SEQ`. In the MIDI modes Up / Down stay the octave;
  Sequencer mode and the projects view keep theirs; Track 4 held + Up / Down is still the rate.
- **Down cycles the firmware slots in FM-1 mode**: on the press, `FW_ACTIVE` for the next loaded slot after the
  playing one (1 → 2 → 3 → 1, empty slots skipped), with the Back + Track popup naming it. With only one slot
  loaded: the popup names it, nothing is sent. No firmware list: nothing. Held: no repeat. Its LED is lit (127)
  when another loaded slot is there to go to, off otherwise.
- FM-1 mode only, as the MIDI modes need Down for the octave and Sequencer mode uses it.

**Tests:** `move` (the Up mapping, its press / release and LED; Down without a list; the MIDI modes' octave) and
`move_fw` `test_down` (424 checks in all): the cycle with slot 3 empty and with three, one slot, the LED, a long
hold, the MIDI and Sequencer modes. Render: `fw_down_cycle_firmware.pbm`.

## 0.4.3 (2026-11-18)

Back becomes the way between the modes and the projects view (the owner's), and leaving Loop shows the sequencer at
once. No API, format or action changes; `rfm_move_t` has new last fields (`back_down`, `back_used`, `back_trk`,
`back_ms`: rebuild everything).

- **Back no longer presses the FM-1's SEQ** (until now Back was SEQ, not OCT-; Left is OCT-). No Move button presses
  SEQ now; SEQ's LED shows nowhere.
- **A click of Back** (let go within `RFM_MOVE_TAP_MS`, nothing else pressed meanwhile, no Track held) closes the
  latest layer still open, as before (Shift menus, Loop's latch, Shift's lock, the clipboard's marks; in the
  projects view Capture, States, the description, the selection, the catalog, FX setup). With nothing to close it
  flips: from any mode to the projects view, and from the projects view back to the mode. Back now acts on its
  release (a click), in every mode.
- **Back held + Track 1..3** plays that slot's firmware (`FW_ACTIVE`, the library saved; a popup names it), with no
  flip and no mode change; the Track's release does nothing else. An empty slot, Track 4, or a host with no firmware
  list: nothing. In the projects view it is that Track's press there. A long hold of Back alone does nothing.
- Shift + step 1 and Track 1 held + step 1 still open the projects view.
- **Leaving Loop is immediate:** Loop's page edit was its popup, and ending the latch (Loop or Back) or letting a
  held Loop go only started the popup's 1 s timeout (`RFM_MOVE_POPUP_MS`), so the page edit stayed a second. Both
  now drop it at once. The other exits (Shift menus, the projects view and its layers, FX setup) already redraw at
  once; the session's 33 ms pacing is not the cause.

**Tests:** `move_fw` `test_back` (412 checks in all): the flip both ways, one level up first (the description, the
selection, FX setup, Loop's latch), a long hold, a Track held, Back + Track 2 / 1 / 3 / 4 in and out of the view,
Shift + step 1, and the frame right after leaving Loop (latch by Back, by Loop, a held Loop) and the view equal to
the sequencer's own; render `fw_back_track_firmware.pbm`. `move`: the button table without Back (SEQ's LED unmapped),
Back with nothing open flips; `move_seq`: the same.

## 0.4.2 (2026-11-18)

The firmware browser's filters, simpler and clearer, and launching a firmware from the catalog (the owner's
feedback). No API, format or action changes; `rfm_move_fw_state_t` has two new last fields (`hold_step`, `hold_ms`:
rebuild everything).

- **One category at a time:** steps 3..7 (the categories) and 8..16 (the user tags) are one radio group: a step shows
  just that category or tag, the lit one again goes back to all categories (no step lit). Steps 1 / 2 stay the scope
  (Favorites / All); the category applies within it. `settings.fw_filter` holds one bit at most: settings saved
  with several load as the lowest (never an empty list from an old choice).
- **The catalog's scope is one choice too:** step 1 "Update ready" (on the Move, a newer version in the catalog),
  step 2 "Not installed", neither: everything (`fw_cat_scope` 0 / 1 / 2; an old 3 loads as 1). An "Installed" scope
  has no free step (3..7 categories, 8..16 tags); All outside the catalog lists the installed firmwares.
- **The filter popup answers "what did I just press":** the step's name big (small past 10 characters, never cut),
  ON (inverted) or off, then what the list shows now on up to 2 lines at word boundaries ("Showing: Synth, not
  installed"), then the count. (Without "Showing:" when that fits, "..." only past 2 lines.)
- **Hold a filter step to see what it is:** the popup shows its name and state after `RFM_MOVE_TAP_MS` (300 ms);
  released after that, nothing changes. A tap acts on release (Capture + step still tags on the press).
- **Launch from the catalog:** a jog press on a firmware that is on the Move and current loads it into the firmware
  track the view is on (`FW_LOAD`, as on Favorites / All); one not on the Move still downloads, one with an update
  still updates. When a download or update finishes and that firmware is still selected in the catalog, the popup
  adds "Jog: load on Track n"; nothing loads by itself (moved to another pad or out of the catalog: no offer, no load).
- Capture + step's tag name is drawn small past 10 characters instead of cut (tag names up to 15).

**Tests:** `move_fw` (383 checks in all): `test_filters` for the one-choice model and the old settings;
`test_filter_popup` (the words for one category, another replacing it, all categories, the catalog's scopes, hold
and tap, a 15-character tag); `test_download_load` (download -> offer -> jog -> `FW_LOAD` into the track's slot;
installed -> jog loads; update ready -> jog updates; a download finishing after moving on loads nothing); renders
`fw_filter_*.pbm`, `fw_download_offer.pbm`, `fw_capture_long_tag.pbm`. The mockup's `step_favorites_chords` screen is
checked as redrawn (33 of 34 still the mockup's, pixel for pixel).

## 0.4.1 (2026-11-18)

The host's edits: a public way to apply FX setup, mix, routing and firmware-slot edits made outside the Move (a web
page), so a host no longer writes `rfm_move_t`'s fields and copies `fx_load`'s rule (RemoteFM14Move's page edit ops
16..22). Nothing changes for a host that does not call them; no format or action changes.

**API (move.h, new):** `rfm_move_fw_set_active(core, slot)`, `rfm_move_fw_set_level(core, slot, level)`,
`rfm_move_mix_set(core, slot, level, pan, sends[3])`, `rfm_move_route_set(core, fw_filt[3], mfx_filt[3])`,
`rfm_move_fx_set_module(core, kind, slot, module | -1)`, `rfm_move_fx_set_param(core, kind, slot, index, value)`,
`rfm_move_fx_set_page(core, page)`, and `RFM_MOVE_FX_PAGE_MIDI / _ROUTE / _AUDIO / _MIX` (settings.fx_page's values).
Call them on the core's thread. Each makes the Move's own edit: the same change, the same action through the act
callback (`FW_ACTIVE`, `FW_LEVEL`, `MIX`, `ROUTE`, `FX_LOAD`, `FX_PARAM`, `SETTINGS`), the same dirty marks (the
project's autosave; the library for the active slot and levels); nothing when nothing changes. They return -1
refused (out of range, nothing changed), 0 done, 1 done and on the Move's screen now (redraw). `-1` / NULL arguments
leave a value. In the projects view `fw_set_active` is a Track press (the firmware track shown, its popup); other
edits show no popup. The host guide's v0.4.1 section.

**Behaviour (the owner's ask):** with no favourites, a firmware track shows All (step 2 lit, "All") on entering the
view, on switching to a firmware track, and when the last favourite is removed (Sampling). The setting
(`fw_scope`) keeps the user's choice: once favourites exist again, the next entry shows the saved choice; while
browsing, nothing flips back on its own; pressing step 1 / 2 is always obeyed (an empty Favorites included). The
catalog (Mute) is unchanged. `rfm_move_fw_state_t` has a new last field, `all_auto` (`rfm_move_t` grew by a byte or
so: rebuild everything).

**Tests:** `move_fw` has `test_host_edits` and `test_no_favs` (336 checks in all, was 261): the dirty flags, the actions, the library save, and the
setters against the Move's own Track press, Mixer knobs and jog-press load (state and screen identical).

## 0.4.0 (2026-11-18)

The Move firmware browser and FX setup (the owner's design, revision 2, and its mockup), for a RemoteFM1 hosted on
the Move. Nothing changes for a host that does not call the new functions, outside the projects view's new Track
1..3 (an empty firmware track) and two format versions. The host guide's v0.4.0 section has the whole API.

**New source file:** `src/fw.c` (add it to your build; CMake and `web/build.sh` have it).

**Formats (old versions still load; the fuzz and round-trip tests are unchanged):**

- Project blob version 5: FX setup at the end of the extension (`RFM_MOVE_FXSET_BYTES`, 216 bytes):
  `RFM_MOVE_PROJECT_FIXED` 2552, `RFM_MOVE_PROJECT_MAX` 39416 (was 39200), `RFM1CS_FILE_MAX` 1261640. Hosts with
  their own copy of the maximum (Android's `BLOB_MAX`, the web's `PROJECT_MAX`) take the new one.
- Settings version 4: 86 bytes (the projects view's state: `fw_track`, `fw_scope`, `fw_filter`, `fw_cat_scope`,
  `fx_page`); `RFM_MOVE_SETTINGS_BYTES_V3` is version 3's 80.
- New: the library blob ("RMVL" version 1, `FW_LIBRARY`): favourites in order, colours, tags and tag names by
  firmware id, the three slots' ids and levels, the active slot.

**API (move.h):**

- In: `rfm_move_fw_list` (the host's array, kept by pointer, no cap), `_fw_installed`, `_fw_slots`, `_fw_progress`
  (0..100, `RFM_MOVE_FW_FAILED`, `RFM_MOVE_FW_BUSY`), `_fw_library` / `_fw_library_save` / `_fw_flush`,
  `_fw_voices`, `_fw_sounding`, `_fw_active`, `_fw_level`, `_fw_states`, `rfm_move_fx_modules`, `_fx_refused`,
  `_fx_sync`.
- Out: `FW_LOAD` 17, `FW_UNLOAD`, `FW_ACTIVE`, `FW_DOWNLOAD`, `FW_LIBRARY`, `FW_LEVEL`, `FX_LOAD`, `FX_PARAM`, `MIX`,
  `ROUTE`, `STATE_SAVE`, `STATE_LOAD`, `STATE_DELETE` 29. `rfm_move_act_t` has new fields at its end: `track` (the
  sequencer's MIDI: its track 1..16), `slot`, `fx`, `param`, `mix[5]`, `filt[6]`. `rfm_move_t` and
  `rfm_move_seq_t` grew (rebuild everything).
- The session passes the new actions through as `EV_ACTION` (`FW_LIBRARY`'s blob in `ev->data`);
  `rfm1cs_session_flush` also writes a pending library.
- wasm: `mw_fw_*`, `mw_fx_*`, `mw_act_ext`; `move-core.js` `fwList()`, `fwSlots()`, `fwProgress()`, `fwLibrary()`,
  `fxModules()`, `fxSync()` .. and the new action kinds; `SETTINGS_BYTES` 86, `PROJECT_MAX` 39416.

**Behaviour (the browser on):** the projects view opens on the firmware track played; Track 1..3 slots, Track 4 the
projects; Mute the catalog; Loop the firmware's states; Session FX setup; Track 1..3 held + volume a slot's level (any
mode). README.md, "Firmwares (0.4.0)".

**Tests:** new ctest `move_fw` (261 checks; the mockup's 34 gallery screens pixel for pixel); changed expectations,
each for a version bump only: `move` (versions 1 and 2's sizes now from `RFM_MOVE_SETTINGS_BYTES_V3`), `move_seq`
(the project version, a version 3 blob made by also cutting the FX block), `session` (the file maximum);
`session` has a pass-through test; `web_smoke` the browser in wasm.

## 0.3.1 (2026-11-18)

- `rfm1cs_why_text(RFM1CS_WHY_PROTO)` reads "the Move speaks another Move Remote protocol" (it fell through to "the
  link closed"). Found by the Android port.

## 0.3.0 (2026-11-18)

What the iOS and Android ports asked for. Behaviour is unchanged otherwise.

**API changes (a host must update its calls):**

- `rfm1cs_session_opened(s, now, attempt)`, `rfm1cs_session_rx(s, now, attempt, bytes, n)`,
  `rfm1cs_session_closed(s, now, attempt, final)`: the attempt id `RFM1CS_EV_OPEN` now carries (`ev.attempt`, a new
  one each try, never 0; `rfm1cs_session_attempt()` reads the current one). A call with an older try's id is ignored:
  a socket that closes late can no longer end the next try. wasm: `mw_sess_opened/_rx/_closed` take it after `now`;
  `move-core.js`: `{ type: "open", attempt }`, `sessOpened(now, attempt)`, `sessRx(now, attempt, bytes)`,
  `sessClosed(now, attempt, final)`.
- `rfm1cs_status_t` and `rfm1cs_session_t` have new fields at their ends (`lost_why`, `bye`, `gone_live`; `attempt`,
  the thumbnail's pacing): rebuild, do not mix objects from 0.2.0.
- `move-core.js` `PROJECT_MAX` is 39200, `RFM_MOVE_PROJECT_MAX` as the C has it (it said 39072, a stale copy: blobs
  of 39073..39200 bytes were refused by `projectCheck` and by the web's and Android's file readers).

**New:**

- `store.h`: the project file every host wrote itself (`.rfmmove` / `.rfmmoveset`, "RMVP"):
  `rfm1cs_file_pack`, `rfm1cs_file_unpack` (every blob checked), `rfm1cs_file_check`, `rfm1cs_file_size`,
  `rfm1cs_file_error_text` (the web player's words; "this page" or "this app"), `RFM1CS_FILE_MAX`. The web's, the
  iOS app's and the Android app's own writers made byte-identical files; `tests/rmvp/` keeps one pair from each, and
  the `session` test reads and repacks them. wasm `mw_file_*`; `move-core.js` `filePack()`, `fileUnpack()`.
- `session.h`: the words to show, from the web player: `rfm1cs_status_text()` (the status line and its tone, from
  the state and how the link last went), `rfm1cs_why_text()`, `rfm1cs_lost_text()`, `rfm1cs_bye_text()`,
  `rfm1cs_words_t`. wasm `mw_sess_text`; `move-core.js` `sessText()`, `whyText()`, `lostText()`, `byeText()`.
- `posix_tcp.h`: `rfm1cs_tcp_connect()` takes `host:port`, `[v6]:port`, `[v6]` or a bare IPv6 address
  (`rfm1cs_tcp_parse()`; the address's own port, else the argument, else 7720). New ctest `tcp`.
- `rfm1cs_session_frame()`: called again within 33 ms with the same pixels, the thumbnail is not rebuilt (an FNV-1a
  of the pixels decides); call it at most every 33 ms.
- README.md, "Threads": a session shared by a network thread and an engine thread under one mutex (actions come out
  on the network thread: queue them), and a network thread driving it alone while the engine is stopped.

## 0.2.0 (2026-11-18)

The link written once, for every host.

- `session.h` / `session.c`: the Move Remote link, transport-free (C99, no allocation, no OS calls, no threads):
  the web player's MoveLink logic (HELLO, CONFIG, ping 1 s, silence 3 s, HELLO wait 4 s, retries 1/2/4 s, BYE, LED
  flushing, the screen paced at 33 ms, release on loss, LEDs invalidated on a new connection), plus what iOS and
  Android had: `MR_VIA_*` as the Move says it, PLAY green as a level, a status struct with counters, project and
  settings requests as actions (the settings image attached). A patient wait (a browser asking about local network
  access), `give_up` before the first connection, a final close.
- `store.h` / `store.c`: the project actions as slot reads, writes and deletes: the cut / copy / paste buffer, the
  colour (byte 18 and its FNV-1a check), the used bits; reads answered at once or later, in order.
- `posix_tcp.h` / `posix_tcp.c` (optional): non-blocking TCP for native hosts.
- `rfm_move_thumb()`: the FM-1's screen into the thumbnail now (the session draws later).
- wasm: `mw_sess_*`, `mw_store_*` (the store's I/O to JS through `Module.rfmHosts`); `move-core.js`: `sess*`,
  `store*`, `STATE`, `WHY`, `CH`.
- CMake: `rfm1cs_session`, `rfm1cs_tcp`; tests `session` (a fake Move), `session_e2e` (the simulator over TCP).

## 0.1.0 (2026-11-18)

The Ableton Move core moved out of RemoteFM1 (`host/src/move/`, `host/include/rfm/move.h`, the Move tests,
`web/move/`, `web/site/js/move-core.js`, `move-seq.js`, the vendored Move Remote client) with its git history, as of
RemoteFM1 a3946f6. No change in behaviour.

- The header is `rfm1cs/move.h` (`rfm/move.h` still works); the `rfm_move_*` names are unchanged.
- `rfm_move_version()`, `RFM1CS_VERSION`; wasm `mw_version()`, `MoveCore.version()`.
- CMake: the `rfm1cs_move` library for `add_subdirectory()`; the tests when top level.
- `web/build.sh [OUT_DIR]` builds `rfm_move.mjs` (default `build/web/`).
