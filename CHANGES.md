# Changes

Versions are git tags (`vMAJOR.MINOR.PATCH`); hosts take this repository as a submodule pinned to one.
`RFM1CS_VERSION` in `include/rfm1cs/move.h` and `rfm_move_version()` name it.

## 0.14.0 (not released)

The owner's run on the Move of 2026-11-23, seven requests. Tests: `tests/move_sessview.c` `test_o14_*` (dumps
`sessview_o14_*`). New fields at the end of `rfm_move_t`: rebuild everything.

- (item 6, "Turning the sequencer's arp on is not functioning, No notes getting sent to the firmware") the core does
  send them: `test_o14_arp_notes` turns the arp on every way the UI offers (Shift + step 11's click and the menu's jog
  click; Shift + step 11 held; Track 4 held + step 11 held; the Hold mode), with the menu open and closed, stopped and
  running, after a Track tap away and back, and finds the arp's note ons each time as `RFM_MOVE_ACT_MIDI` with
  `track` the selected track (1..16), `dest` 0, stamped with their frame by `rfm_move_seq_advance`. Unlike the pads
  played live (`track` 0: the active slot), they reach a firmware only through the host's routing of that track
  (`ffilt`, the same as a clip's notes); the selected track is routed to the slot on entering a note layout. No core
  change.
  Then, from the host's trace (the owner's own project: track 1's arp with CHANCE 0, GATE 2, OCT DIST -24, QUANT 0,
  MOD step 1 a tie; the engine rightly plays nothing): **an arp that is on but can never sound says so**
  (`rfmv_seq_arp_silent`: CHANCE 0, or every MOD step in its length a rest, a tie or a repeat): turning it on shows
  "Arp" / "Silent" the whole screen (said "Arp Silent"), its menu's header at rest "Arp Silent", and its walk and lanes
  dimmed as when off. Stored values are never changed. (`test_o14_arp_notes`, pictures `sessview_o14_arp_silent*`.)
- (the owner's, the same day: "The arp I don't think worked because chance was set to 0% ... I don't think chance
  makes sense anymore. The arp should be 100% and not changable as knob parameter") **the arp has no CHANCE**: every
  step plays (`arp_tick` no longer rolls it), knob 4 of the arp's page is dark and dead until the arp's knobs are laid
  out anew (the owner's: "rethink some of the arp's other settings ... and rethink the knob order": a proposal first),
  its header and spoken value gone. A project's stored CHANCE is not looked at: read as 100, written as 100 (the blob's
  byte stays, the version too), so the owner's arp sounds again; "Silent" is left for a MOD lane with no step that plays.
- (item 1, "if I have page set to a CC bank or FM-1 knobs, switching to another track and back the knobs reset to the
  trig page, the page I am on should be remembered") each sequencer track keeps its knob page (TRIG, FM-1, banks 1..4:
  `seq.kpage[16]`, `rfmv_seq_kpage`, `rfmv_seq_kpage_restore`), set by the jog, brought back whenever the track is
  selected (`rfmv_seq_select`: a held Track's step, session view's selection) and on entering a note layout (a Track
  tap: was the TRIG page always). A bank page sets `s.bank` as the jog does. RAM only, not in the project: the blob's
  per-track bytes have no room the older cores accept (a version 10 for 16 bytes of view state was not worth it); a
  track never shown starts on TRIG, as before.
- (item 3, "The current state is not obvious on pads, Should be a different contrasting color") the States layer's
  active state's pad is white, breathing slowly (`RFMV_ACTIVE_ON_MS` 700 full, `RFMV_ACTIVE_OFF_MS` 300 at a third,
  from the layer's opening: `rfmv_active_rgb`, `rfmv_st_active_rgb`), for all three slots; was the slot's colour at
  full, next to the stored ones in the same colour dimmed. White is no slot's colour (orange, blue, yellow) nor solo's
  purple; a pressed pad is white and steady, so the beat tells them apart. Two LED writes a second for it.
- (item 4, "Using like Shift + Step 3 for the track settings. I don't like how when you turn the Jogwheel it is copying
  the line up to the top, it should just show a marker or something in front of the row that is selected, or do
  things the same way the global settings does") the jog's selection in every Shift menu that has one (all but the
  arp's and Global settings) is drawn as Global settings draws its row (`sel_row`): inverted while selecting, framed
  with its words inverted between two arrows while editing. Workflow (Shift + step 3) in the selected row itself, its
  top line staying "Workflow"; the picture menus (Tempo, Groove, Layout, Keys, Velocity, Record) in their top line,
  sticky once something is selected (was a plain copy for a second). Pictures: `sessview_mj_*_sel`, `sessview_mj_*_edit`.
- (item 5, "Using SHIFT + a track button should take you to Track settings similar to Ableton's Move software
  shortcuts") Shift held + Track 1..3 (where the Move's Shift is ours: a note layout, session view, the Mixer and
  Routing): at the press, that firmware played as its tap would (only when it is not already), its selected track
  selected, and Shift + step 3's menu (`rfmv_seq_menu_open`) open over it; its title is now the track ("Track 5", was
  "Workflow"), said "Track 5 settings". The Track's release and hold, and Shift's release, then do nothing
  (`tbtn_shift`, a new field at the end of `rfm_move_t`). Before, Shift was ignored there (the Track tapped, held: its
  menu). Not in the FM-1 layout or a takeover (Shift is the firmware's GLO there), not over Global settings or States.
  Shift + Track 4 unchanged: it is no firmware's track. Back closes the menu and changes nothing else.
- (item 2, "On the sequencer with FM-1 knobs it would be nice to be able to click the Jogwheel to make it control one
  of the knobs like it does in the FM-1 layout. Click to exit. Shift + Click to change the target knob, should be
  linked with the Fm-1 layout"; then, after the first cut: "No double click on the Jogwheel to toggle takeover.
  Single click and shift + jogwheel to change jogwheel mode. Single click for changing jogwheel mode is synced with
  FM-1 layout and FM-1 layout keeps the single click behaviour") on the FM-1 knob page **the jog's click is the
  takeover again** (Knobs <-> Knobs+Keys, at the press, as 0.12.x), and taken over the page is the FM-1 layout's
  whole, its jog too: the turn is that layout's jog (`jog_target_turn`, factored out of it: SELECT's turn, or PRESETS
  / ALGO as their knobs, a detent a tick), the target's knob white. **Shift + click changes what the jog turns**:
  taken over, the next target as the FM-1 layout's click (SELECT -> PRESET -> ALGO); in Knobs, the pages -> SELECT ->
  PRESET -> ALGO -> the pages (the jog on a knob while the steps and buttons stay the sequencer's; a click into and
  out of the takeover puts it back on its pages too). The target is one per slot (`jog_enc_s[3]`, new; `jog_enc`
  stays the active slot's, swapped on a slot change), the same one the FM-1 layout's jog turns and its click changes
  (unchanged). Feedback: the new `POP_FWJ` ("Jog" over "Select" / "Preset" / "Algo", or "Pages"; said "Jog: Select"),
  "Jog Select" at 2x on top of the track view while it lasts. Play follows the takeover (the owner's: "If I press the
  Jogwheel for takeover then Play should be the firmware's, if not ... Play should be the Move's host"). A held step,
  Loop, Shift's tempo, a menu, a held Track, Track 4's layer still take the jog over it; a slot change turns it off.
  New fields at the end of `rfm_move_t`: `fwj`, `jog_enc_s`.
- (item 7, "ChoralRoot sequencer using FM-1 knobs, holding shift and turning the knob is changing the engine, which is
  good, but there is no UI communicating what is happening or the new engine") what the core did: the knob's popup
  drew the knob's kept readout whenever there was one (`rd[sl].on`), so with Shift held it showed the unshifted
  page's last value ("Cutoff 1.2k") while the turn changed the engine; with none, the knob's plain name and a count. A
  readout arriving during the gesture (`control` -1 within `RFM_MOVE_RD_PAIR_MS`, or the control named) was taken and
  drawn in place, Shift or not. Now: the popup draws a kept readout only when it is this page's (`rfmv_rd_valid`,
  speech's own test made public: the FM-1 buttons held then and the knob's label then, as now); with the FM-1's GLO
  held and none, the title is "Shift" + the knob's label as it is now (a label the firmware changes while Shift is held
  shows at once). The Move's Shift reaches the firmware as GLO in the FM-1 layout and the FM-1 page's level 2
  (Knobs+Keys) only; on level 1 Shift is the sequencer's and a turn reaches the firmware unshifted, so nothing says
  "Shift" there. Pictures `sessview_o14_shift_knob_*`.
  Then, from the host's trace (ChoralRoot sends no readout while its engine picker is held, one at Shift's release):
  a turn that gets no readout of its own within `RFM_MOVE_RD_PAIR_MS` (250 ms) no longer shows the knob's older
  readout as if current; it shows the turn (the name, "Shift" first with GLO held, the count) until a readout comes,
  which shows at once (`rd_at[8]`, `turn_ms`, new at the end of `rfm_move_t`). The FM-1 layout's knobs and jog the
  same. Picture `sessview_o14_knob_turn_noreadout`, `sessview_o14_shift_knob_level1`.

The owner's six requests for the projects page (Track 4's double click; `project.c`; `test_o14_projects`, pictures
`sessview_o14_proj_*`). New fields at the end of `rfm_move_t`: `pj_press`, `pj_used`, `pj_undo`, `pj_del`, `pj_ack`
and their times.

- (item 8, "No undo for deleted project") Undo right after Delete + a project puts it back in its slot: the core sends
  `PROJECT_PASTE` for that slot (the cut left it in the host's buffer, so the paste carries what a paste carries, the
  firmware state files included) and, for the open project (emptied by its delete), `PROJECT_LOAD` again. Undo lit
  (127) only while it can; "Undo / Project 07". One step: a paste, a copy, another delete or leaving the page ends it.
- (item 9, "Holding a project tab should just show info not load the project") a project pad's press shows its info
  (`POP_PROJ`) at once and loads nothing; the click (released within `RFM_MOVE_TAP_MS`, knob 1 not turned) opens it at
  the release; held past it, info only, the release nothing. Delete / Copy + a pad still act at the press.
- (item 10, "Current selected project color blends in") the open project's pad is white: breathing as the active
  state (`rfmv_active_rgb`) when stopped, white on the beat while playing (was its own colour at full).
- (item 11, "Clicking the open project again should show "Saved" and save it") its click saves it now
  (`PROJECT_SAVE` only, with `value` 1: the player's own save, the host stores the firmwares as they are now too;
  no load, no `PROJECT_FW`), "Saved" (`POP_HOST`) a second, its pad off,
  on, off (`RFMV_PJ_ACK_MS` 120 each).
- (item 12, "The copied pad should not still show as dashed after a paste") a copy's paste unmarks its source
  (`pbuf_src` 0xFF); the copy stays in the buffer for further pastes.
- (item 13, "Deleting a project should show maybe a quick red flash and then the pad light should be off and the
  screen should then show an empty slot") its pad red for `RFMV_PJ_DEL_MS` (250), then dark (was red, blinking, until
  something else); the screen's X only while Delete is held, then the empty slot's dot; the pad's info popup closed so
  the grid shows.

- (item 14, the owner's "Clearing the clip should only clear the steps on that clip and reset to 1 page/bar") Delete
  held + Play clears the clip's notes and locks and sets its loop to one bar from step 1 (`clip_clear_sel` +
  `rfmv_seq_loop_set`, one undo step; was: the length kept), the page shown bar 1. The host's trace: in the FM-1
  page's level 2 (Knobs+Keys) `on_button` gave every mapped button to the firmware before the sequencer saw it, so
  Delete + Play cleared nothing and ChoralRoot got LFO (its LOCK) and PLAY (its looper, its MIDI Start). Now Delete
  and Copy there are the sequencer's modifiers at the press (`H_GATE`, `fwg`): while one is held the other buttons
  and the steps are the sequencer's too; a click (released within `RFM_MOVE_TAP_MS`, nothing else pressed, the
  modifier not used) is also sent to the firmware as a tap of LFO / EDIT at the release; their light full while held.
  Shift, Mute, Loop and the other mapped buttons stay the firmware's in that level; Play alone there is the firmware's
  PLAY only, as before. `test_o14_clear_clip` (levels 1 and 2: nothing reaches the firmware, the clip one bar, Undo
  brings both back), `test_move_seq`'s clear-clip check.

The owner's notes of 2026-11-24 (`tests/move_sessview.c` `test_n24_*`, pictures `sessview_n24_*`; engine checks in
`tests/test_move_seq.c`).

- (item 1, "Track Button LEDs should stay lit to show which Firmware/Mode I'm on") the played firmware's Track is lit
  at rest in every mode and view (`rfmv_track_leds`): **steady full** in the FM-1 layout and a firmware's takeover,
  **breathing** in the sequencer (a note layout's track view, its Knobs and Knobs+Keys pages, session view, the browser
  over it): full `RFMV_ACTIVE_ON_MS` (700), 60 % `RFMV_ACTIVE_OFF_MS` (300), the beat of the active state's pad (two
  LED writes a second). The other loaded slots a quarter in their colour (silent ones still blinking slowly), an empty
  slot dark (the played one, empty, faint white), Track 4 a quarter except on its own pages (Mixer, Routing, the
  project grid: full, the played Track 60 % steady there) or held. Was: in the sequencer every firmware Track a
  quarter, the played one not told apart, and Track 4 full; an empty slot faint white. A takeover that claims the
  Track buttons still lights them itself. `test_n24_track_leds`, pictures `sessview_n24_tracks_*`.
- (item 2, "Holding back + Play should always trigger play on the Move's side") where Play alone is the firmware's
  (the FM-1 layout, the FM-1 page's Knobs+Keys with Delete / Copy not held, a firmware's takeover: `rfmv_play_fw`),
  Back held + Play is the Move's own Play at the press: it starts or stops the Move's sequencer exactly as Play does
  in the track view (`play_toggle`), nothing reaches the firmware, Back's click does not come; the popup the play /
  stop icon over "Move Play" / "Move Stop" (`POP_BACK`, `BK_PLAY`, `BK_STOP`; said "Move play"). A takeover that
  claims the buttons holds Back for the core already: Play with it is now the core's too and Back is not sent
  (`takeover.c`); one that also claims the Track buttons has Back itself, so there it stays the firmware's. Where Play
  is the Move's (the track and session views, Knobs) Back + Play stays the 0.12.x shortcut, "to bar 1": playing,
  every clip from its start (Shift + Play's restart); stopped, nothing; it never starts or stops. No conflict, kept.
  `test_n24_back_play`, pictures `sessview_n24_back_play_*`.
- (item 3, the owner's test note: "Would be nice if there was a global setting to enable so that a firmware's play
  button also drives the Move's playback") Global settings' new last row **FW Play**: "Firmware" (the default) or
  "Both" (said "FW Play: Both"). On Both, a Play press that goes to the firmware (the FM-1 layout, Knobs+Keys, a
  takeover that claims Play) also starts / stops the Move's transport (`rfmv_play_both`); Shift + Play (the firmware's
  shifted Play) does not; Back + Play stays the Move's only (item 2), once. New field at the end of
  `rfm_move_settings_t`: `fw_play`, saved in the Popup size byte's bit 1 (`rfm_move_save`; the settings' version and
  size unchanged; a core before 0.14.0 reads bit 0 only, so it keeps Popup size and ignores FW Play).
  `test_n24_fw_play`, picture `sessview_n24_fw_play`; `test_move_seq`'s and `test_move_speech`'s row counts.
- (item 4, "Back should exit out of the FM-1 knob lock. If I go to another track or mode and come back, the lock state
  should exit") the lock is the FM-1 knob page taken over (Knobs+Keys, `seq.fwp_full`) or the jog on a knob (`fwj`).
  **Back's click** with nothing else to close (a menu, Loop's latch, the marks close first, as before) ends both: the
  page in Knobs, the jog on its pages, "Knobs" (`POP_FWLV`; the firmware hears nothing); Back is lit (80) while it can
  (`rfmv_back_has`), Back held + Delete ends it with the rest. The owner's own exception to "Back never changes mode
  or view" (it changes neither: the page stays). **Its place gone** ends it too (`rfmv_fwp_ctx`, at the end of
  `rfm_move_input` and in `rfm_move_seq_advance`: the mode, the active slot, the selected track, session view, the
  projects view, the slot's layout; the new `fwp_key` at the end of `rfm_move_t`): coming back shows the FM-1 page
  (the track's page is still remembered) in Knobs with the jog on its pages. Was: Back did nothing there, and the
  level and the jog's knob were kept across a Track tap, a layout or another track. Another knob page and back (the
  same track) still keeps it. `test_n24_knob_lock` (pictures `sessview_n24_lock_*`); `test_fwp_page` and
  `test_o14_fwp_jog` follow the new rule.
- (item 5, "No need to draw the little lines to show which parameter is selected, the highlighted parameter is enough")
  the track settings (Shift + step 3, Shift + Track 1..3) no longer draw the knobs' ticks under the title (a short one
  a knob, the selected one long); the highlighted row is the only mark, at rest, selecting, editing, a knob touched.
  The other Shift menus (Tempo, Groove, Layout, Keys, Velocity, Record) and the arp keep their ticks: there the
  selection is in the top line, over a picture, and the tick is R5's "mark for the knob's place", not a second mark
  of the selection. `test_n24_settings_ticks` (pictures `sessview_n24_settings_rest`, `_sel`, `_edit`);
  `menu_heads`, `menu_over`, `test_mj_menus` expect no ticks in Workflow.
- (item 6, the owner's test note: "While not in takeover mode, the Move's shift button plus a FM-1 knob turn should
  also do the shift and show the engine change") on the FM-1 knob page's Knobs (level 1) the Move's Shift held + a
  turn of one of the eight FM-1 knobs is the firmware's shifted turn, as in the FM-1 layout: the firmware's GLO
  (what the Move's Shift is there, `RFM_MOVE_B_GLO`) goes down just before the first such turn and up at Shift's
  release (`fwp_glo`, new at the end of `rfm_move_t`; let go with every hold too), and the knob's popup is the 0.14.0
  item 7 one: "Shift" + the knob's label, the firmware's readout (its engine) at once. A Shift press with no knob turn
  sends nothing; Shift + a step, + the jog's turn (the tempo), + the jog's click (the jog on a knob) are unchanged and
  send nothing; Shift's release after such a turn latches nothing. Was: the turn reached the firmware unshifted.
  Knobs+Keys unchanged (Shift is GLO at its own press there). `test_n24_shift_knob` (pictures
  `sessview_n24_shift_knob`, `_engine`); `test_o14_shift_knob`'s level 1 check follows.
- (item 7, "Arp does not work for the sequenced notes"; the owner regards the arp as a MIDI effect after the
  sequencer) **a track's clip feeds its arp**: with the track's arp on, each of the clip's notes goes into the arp at its
  time and is held for its length (`clip_note`, `arp_in`, `arp_ends`); the arp's notes go out on the track
  (`track` 1..16: the host's routing as for the clip), the clip's own notes are not sent. Every track has its own
  arp now (`rfm_move_arpst_t arps[16]` in `rfm_move_seq_t`, RAM, in place of the one `arp_*` set that followed the
  selected track's pads); the pads of the selected track feed the same arp as its clip (a note held by both stays
  till both let go; a pad's release goes to the track it was played on: `rfmv_seq_arp_off` takes the track). The
  arps run after the clips each tick (`arp_tick` moved after `play_tick`), so a clip's note starts its arp on that
  tick (QUANT's grid as before). The Hold and one-pass (Once, 1-st) modes work with the clip's notes as with the pads;
  the transport's stop and a clip's launch or stop let go of what the clip held, a set it latched too
  (`arp_clip_drop`, from `rfmv_seq_release`): no notes hang; a muted track's (or one not soloed) clip arp is silent,
  as its clip; the pads' notes still sound through a muted track's arp as they do live.
  **Recording with the arp on records what was played, not the arp's output** (a change): the pad's note goes into
  the clip at its press with its real length at its release (`rfmv_seq_pad_on` / `_off`, as with the arp off; Capture
  too), and the arp's notes are no longer recorded (`arp_record` gone). Played back, the clip feeds the arp, so the
  phrase is arpeggiated once, not twice. `test_n24_arp_clip` (off: the chord, on: 16ths and never the chord, the stop,
  mute, solo, Once, Hold, a pad with it; picture `sessview_n24_arp_clip`); `test_move_seq`'s recording check now
  expects the two notes played with their lengths.
- (item 8, "Changing Arp rate breaks the arp from working right until you retrigger the notes") the cause: a run's
  notes were timed as `t0 + k x BAR / RATE` with k the notes played so far, and RATE read afresh each time; turned
  while it ran, k notes at the new rate put the next note far behind (faster: a note every tick until it caught up, a
  burst) or far ahead (slower: silence for as long as k slower notes take, bars, "until you retrigger"). Now each arp
  keeps the RATE its walk is timed by (`rfm_move_arpst_t.rate`); at a change it goes on from the run's start at the
  new rate, the next note the first of the new grid after now (`arp_tick`), so it stays in time (on QUANT's grid
  while the transport runs) and keeps its place in the pattern and lanes. `test_n24_arp_rate` (held note, 1/16 ->
  1/8 -> 1/32, stopped and running: the counts a second at each rate, no burst; it failed before the fix: 1 and 0
  notes a second), picture `sessview_n24_arp_rate`.

## 0.13.0 (2026-11-23)

The first tag since 0.9.1: it carries 0.9.2, 0.10.0, 0.11.0 and 0.12.0 below, which were never tagged by themselves.

- (found by the on-device run of 2026-11-22) The view's name is whole on the screen: `v2_modeName` draws it at 4x,
  3x or 2x, whichever fits ("Sequencer" at 3x is wider than the screen and showed as "equence").

The owner's review of 2026-11-22, cards P.1..P.11: a project holds its firmwares, and the firmwares' states are a layer
on the Move. Everything new is for a host that sets the new capability `RFM_MOVE_CAP_PROJFW` (not in
`RFM_MOVE_CAPS_ALL`); an older host keeps 0.12.0's behaviour. New fields at the end of `rfm_move_fw_state_t`,
`rfm_move_proj_info_t` and `rfm_move_t`: rebuild everything. The project blob is version 9 (versions 1..8 load);
`RFM_MOVE_PROJECT_MAX` 40572. A new source file, `src/projfw.c` (CMake and `web/build.sh` list it).

**A project holds its firmwares** (P.1, P.2, P.11; `projfw.c`, `project.c`)
- Blob version 9 adds `RFM_MOVE_PFW_BYTES` (176): per slot the firmware's id, level, `rfm_move_slotset_t`, `lock`; the
  slot played, the drum notes, the velocity mode and level. Every field checked (`rfmv_pfw_check`). A version 1..8
  project holds no firmwares: its load leaves the slots and settings alone, its next save writes them. An older host
  saves a loaded version 9 part back unchanged (`pfw_raw`).
- A load applies the settings as the user's edits do (`FW_LEVEL`, `FW_ACTIVE` with its layout, `SETTINGS`) and sends
  the new `RFM_MOVE_ACT_PROJECT_FW` per slot; the host answers with the new `rfm_move_proj_fw` (0..99,
  `RFM_MOVE_PFW_READY`, `_MISSING`, `_FAILED`; `RFM_MOVE_PFW_WAIT_MS` 20 s with no news: failed).
- A change of any of these values by the user marks the open stored project edited (`rfmv_pfw_watch`, at the end of
  `rfm_move_input` and in `rfm_move_seq_advance`). A missing firmware stays wanted (saved again) until the user changes
  that slot (`pfw_want`, `pfw_touched`, `rfmv_pfw_saved_ids`).
- `rfm_move_proj_info_t` has `has_fw` and the three ids (`rfm_move_project_info`, the live project's).
- A new project (an empty pad): its three slots empty (`PROJECT_FW` with ""), the rest as it is (`rfmv_pfw_new`).
- (the owner's addendum) A new project's routing and channels (`rfmv_pfw_defaults`): tracks 1-4 to firmware 1, 5-8 to
  2, 9-12 to 3, 13-16 to none; channels 1..4 in each group of four; the Track buttons' tracks 1, 5, 9 (the
  orchestrator's call); MIDI FX filters "all". Also a fresh install's first project (`rfm_move_project_dir(m, 0)`,
  `rfmv_pfw_first`). Three slots only; a loaded project keeps what it stored.

**Opening a project** (P.3, mock `proj_loading`, `proj_missing`)
- The loading screen at the pad's press (`rfmv_pfw_press`), before the host answers: the number at 3x, "Loading", a
  row a slot with its bar; a Track lit in its slot's colour once ready (`rfmv_pfw_track_leds`). Play meanwhile: the
  transport once all are settled (`pfw_play`, `rfmv_pfw_play`). Then each missing / failed firmware a second ("Felucca
  / not installed / Track 3 empty"). Another project's pad meanwhile loads that one.

**A held project shows its firmwares** (P.4, mocks `proj_held`, `proj_held_empty`; `screen.c` `project_popup`): the
number at 3x, the tempo and clips at 2x, a row a slot; the activity bars gone (with the capability).

**Duplicating a project** (P.5's note): Copy held + a project, then an empty pad, as before; the README says it, the
host carries the firmware state files with the project buffer; tested.

**States as a layer** (P.6, P.7, P.9; mocks `trk_menu`, `states`, `state_new`, `state_undo`)
- Track 1..3 held + the top row's pad 7 (lit in the slot's colour with a firmware; `lay_pad`, `lay_menu_leds`), or Loop
  in the browser. The layer (`rfmv_st_*`) sits under Back and the Track buttons and over everything else; Back (its
  click: the marks, then the layer) returns to what it was opened over; Loop to the browser when opened there. A
  takeover is not shown under it (`rfmv_tk_on`). `design/UX-RULES.md`'s layer list has it; `rfmv_back_has` too.
- Opening: "States" over the firmware's name a second, the pads swept column by column in the slot's colour
  (`RFMV_ST_SWEEP_MS`, 50 ms a column, at most 8 LED writes a frame, 59 in all; a test counts them).
- The screen: the name at 2x (Copy / Delete held: their icon at its end, `rfmv_held_icon`), 32 states in 4 rows of 8
  (framed stored, filled active, a dot empty; dashed copied, crossed deleted while armed). The pads in the slot's
  colour (active full, stored 108/256, empty dark, pressed white).
- The gestures, as clips in session view: a stored state loads, an empty one saves (no save-over: the host autosaves
  the active state); Copy + a state copies (`RFM_MOVE_ACT_STATE_COPY`), pastes on an empty one
  (`RFM_MOVE_ACT_STATE_PASTE`) or over a stored one while armed (value 1 on the active one: the host loads it too);
  Delete + a state deletes it into the buffer (armed). Copy / Delete alone do nothing; nothing loads while held; the
  buffer is one firmware's.
- Step 14 New (`RFM_MOVE_ACT_STATE_NEW`; its icon lit, white at its press), Undo right after it
  (`RFM_MOVE_ACT_STATE_UNDO`; Undo lit only while there; one step). An undo after a load was not agreed: not built.
- `rfm_move_fw_state_cur` (the host names the active state after a project's copy loads), `rfm_move_states_open`,
  `rfm_move_proj_fw_loading`.
- Words: "States, ChoralRoot", "State 5 loaded / saved / copied / pasted / deleted", "New state, ChoralRoot", "Undo,
  new state", "Track 3, empty", "Loading project 7", "Felucca not installed, Track 3 empty", the held project's
  firmwares.

**Tests**: `tests/move_sessview.c` `test_ps_*` (nine functions; dumps `sessview_ps_*`). Older-version conversions in
`test_move_seq.c`, `test_move_fw.c`, `move_sessview.c` strip the version 9 part too; `test_session.c`'s file size.

### Shortcuts on a held Back (the owner's card, 2026-11-22)
- Back held + a button, in the sequencer's track and session views (also under a Shift menu), at the button's
  press; the button's own job and Back's click do not happen (an open menu stays):
  - + Left: home: track view bar 1 ("Bar 1"), session view track 1 ("Track 1", the tracks' strip).
  - + Up: session view scene 1 ("Scene 1", the scenes' strip). Track view: dead.
  - + Loop: the selected clip's loop back to the whole clip (to its loop's end or its last note's bar), one undo
    step; already whole: the popup only ("Loop", the length).
  - + Play: the transport to bar 1, never started or stopped (playing: Shift + Play's restart; stopped: nothing).
  - + Delete: every layer Back's clicks would close, at once ("All closed"; nothing open: "None").
  - + Right, + Down and the other buttons: dead.
- Held past a click's time (or once something is pressed with it), the shortcuts light (Left, Up in session view,
  Loop, Play, Delete, Menu; a pressed one full) and the other buttons go dark; nothing of this shows on a click.
- Not in the FM-1 layout, a takeover, the projects view, the States layer, or with a Track or a step held.
- move.c rfmv_back_keys, on_button, wanted; seq_ui.c rfmv_seq_back_key, rfmv_seq_back_lit, rfmv_seq_back_pop;
  POP_BACK (screen.c, speech.c); rfm_move_t.back_cut, back_clk.

### A held Track + the volume knob shows the level (the owner's, 2026-11-22)
- "Holding a track button and turning the volume knob should change that firmware's volume level and also visually
  update to communicate the change." The level changed, but its popup ("Track 2 level", the value, the firmware)
  was drawn only in the firmware browser: everywhere else the held Track's menu covered it. Now the popup goes over
  the Track's menu in every layout and view (a note layout, session view, the FM-1 layout, a takeover's screen), a
  second after the last turn the menu is back, and a touch of the volume knob with the Track held shows the level
  without changing it (R5). fw.c rfmv_fw_level_shown; screen.c; move.c on_touch; test_track_volume.

### A held Track's step selects at the press (the owner's, 2026-11-22)
- "Track Held: Clicking a step should instantly show selected, not wait the 300ms": the step's track is selected (and
  routed to the firmware if it was not) at the press; the double click solos at its second press, the track staying
  selected; the hold flips the routing at 300 ms from what it was before the press (a routed track un-routed, still
  selected; a not-routed one stays routed, "Routed"); the window and the release show nothing more. Session view's
  steps under a held Track the same; its pads keep their gated click. UX-RULES R2: a second "the same thing sooner"
  exception. (seq_ui.c rfmv_seq_step, rfmv_seq_tk_flush, rfmv_seq_ui_poll, route_rgb; seq.tstep_rt0)

### The FM-1 knob page in the note layouts (the owner's, 2026-11-22)
- The knob pages are TRIG, FM-1, banks 1..4 (the strip six cells; the stored bank unchanged; the page RAM only).
- Level 1: the knobs are the FM-1 layout's (SELECT, PRESETS, ALGO, -, K1..K4) by the same paths: their lights (no
  job: dark), touch name, turn readout / detents, words; popups over the track view, Popup Full or Half (the FM-1's
  screen on the right). A step held: its TRIG knobs.
- The jog's click (at the press, "FM-1 / Knobs+Keys" / "Knobs" a second): level 2 adds the steps (the FM-1's keys,
  its LEDs, icons dark) and the FM-1 layout's buttons but Up (Capture FX .. Play PLAY, Rec REC, Left / Right OCT- /
  OCT+), the screen the FM-1's. The pads, Up / Down (octave), the jog, Back, the Track buttons, the volume stay ours.
  Another page gives the steps and buttons back (what was held let go); the page again: its level. An empty slot:
  "Empty slot", knobs dark and dead. rfm_move_fwp_level() (new): 0 / 1 / 2. No host change needed.
  (move.c rfmv_fwp_*, fwp_sync, fwp_leds, on_step / on_button / on_knob / on_touch / on_jog_click, rfmv_half,
  label_pop, readout; screen.c; speech.c; POP_FWLV)
- Gold: GOLD_CC (the strip's six cells).

### A Track tap flashes the selected track (the owner's, 2026-11-22)
- A Track 1..3 tap into a note layout's track view: the steps show the slot's 16 tracks for 600 ms (routed: the
  slot's colour, not routed: dark, the selected one blinking white / slot colour, 100 ms each), then the clip's steps.
  Also on the slot already played. A step pressed ends it and is a clip step. Not in the FM-1 layout, a takeover,
  session view, the FM-1 page's level 2, under the held Track's menu or the browser. At most 24 step writes in all.
  (seq_ui.c rfmv_seq_tflash_*, move.c track_tap)

### Session view: no X left behind, a double click stops the playing clip (the owner's, 2026-11-22, on 0.12.0)
- "Deleting a clip from session leaves a X box hanging out": a deleted clip's X (screen and pad), a deleted scene
  row's and a deleted state's show only until Delete is let go; then the slot is an empty one. The buffer and the
  armed state stay as they were (Copy held + an empty slot pastes; Back still ends the armed state).
  (seq.sv_xshow, sc_xshow, fw.sb_xshow; rfmv_seq_src_clip, projfw.c)
- "While a clip is playing if you double click on it it should stop": a double click on the clip its track was
  playing before the first click stops it now (0.11.0 played it on). A clip that was not playing: played now, as
  before, with Launch on Instant too. (seq.pad_tap_play)

### The loop's length in steps (the owner's, 2026-11-22, on 0.12.0)
- "While in loop and changing the step length of the bars, we need to show the steps so you can see which steps are
  not in the loop when reducing smaller than 16 steps": the jog's length popup says "5 Steps" / "1 Bar +8" (it said
  "1.5") and, when the loop ends inside a bar, shows that bar's 16 steps (cells in the loop, dots after its end); for
  the popup's second the step lights are that bar's steps. (screen.c loop_popup; seq_ui.c rfmv_seq_loop_steps)
- "while in the loop menu use the pads to quickly set step length ... 16 steps for bar 1 + 8 steps for bar two": Loop
  held or latched, the two lower pad rows are the last bar's length, 1..16 steps, lit up to the length; the upper
  rows dark and dead; the pads do not play there. One undo a press. (seq_ui.c rfmv_seq_loop_pad_n)
- "If a bar is like 5 steps, then pressing and adding a step outsite that should not make the loop length of the
  page become a bar": a step entered after the loop's end in its own last bar no longer grows the loop (grow_to).

### The jog in the Shift menus (the owner's, 2026-11-22)
- "In a menu like the track settings, we have the paramets mapped to knobs but also the jogwheel turn is changing the
  first one. The jogwheel should work like it does for the global settings in which it selects a parameter, then click
  to edit that parameter, click to go back to selecting." In Workflow, Tempo, Groove, Layout, Keys & Scales, Velocity
  and Record the jog's turn selects one of the menu's knobs' parameters (knob order, stopping at the ends; the first at
  each opening) and changes nothing: the header names it and its value a second, its knob's tick is long (the cursor).
  The jog's click edits it: the header stays, its words between two arrows, the turn changes it by the knob's code path
  (`smenu_turn`: the same limits and words; a list an item a detent; Shift fine for Tempo and Groove as before);
  the click again selects ("Done"). Back closes the menu from either state. A knob turned works as before and moves the
  cursor to itself (editing another parameter ends). Words: "Rate: 1/16", "Editing Rate: 1/16", "Done".
- What the jog did before, per menu (all replaced by the above): Workflow the channel, Tempo the tempo (1 BPM, Shift
  0.1), Groove the amount, Layout the layout, Keys & Scales the root, Record the mode: each is knob 1's, which keeps
  it. Velocity: the jog was the option (Dynamic .. Hard), on no knob: it is knob 2 now (lit, named, a list), so the jog
  selects the level (knob 1) or the option (knob 2). The jog's click did nothing in these menus (in a note layout's
  FM-1 page it flipped the page's level under the menu: the menu has it now, R3).
- Kept: the arp menu (the jog its pages, its click the arp off / on), Global settings (its list), a held step, Loop
  or Track 4's layer over a menu (they keep the jog while held; a menu a held Track 4 opened has the jog and its click).
  Groove's and Keys' GLOBAL / TRACK (a toggle by the turn's direction) edit the same way: the click enters editing, a
  turn right TRACK, left GLOBAL. Tempo following the clock: no parameter, the jog does nothing.
- In every place a Shift menu opens: track view, session view, over the Mixer / Routing, over the FM-1 layout, from a
  held Track 4. design/UX-RULES.md R3 has the rule in the owner's words.
- seq_ui.c smenu_jogsel, smenu_sel, smenu_jog_turn, rfmv_seq_menu_click (move.c on_jog_click), smenu_hk, smenu_top
  (head_arrow), smenu_turn (jog: no list ticks); rfm_move_seq_t.sm_sel, sm_edit (new, at the end).
- Tests: move_sessview test_mj_menus (a loop over the seven menus), test_mj_places; tests that turned the jog to change
  a menu's value now click first (test_move.c, test_move_seq.c, test_move_speech.c, move_sessview.c).

### A page held + the jog: its notes' length (the owner's, 2026-11-22)
- "Also in the loop bar, while holding on to a page/bar you should be able to turn the jogwheel and change all the note
  lengths in that bar like you can with the default Move sequencer." Loop held or latched, one page's step held: the
  jog changes the length of every note starting on that page by a held step's jog step (`LEN_FINE` ticks, Shift a
  whole step; the same clamps), one undo step for the hold; the loop's length is not touched. The popup (POP_LOOP,
  pop_val 2): "Bar n" 2x with "Length" 1x, the change so far at 4x ("+1/4", "-1"), the page's 16 steps (a note
  filled); "Bar 2 length: plus 1/4 step". An empty page: the same popup with "--" (pop_val 3), nothing changes, "Bar 4:
  no notes". Two page steps held: the jog does nothing (the range is set as before). A page step used with the jog is
  not a double tap's first tap; its release does nothing. (seq_ui.c loop_page_len, rfmv_seq_jog, rfmv_seq_step;
  screen.c loop_popup; speech.c loop_words; rfm_move_seq_t.loop_down, loop_jogd, loop_dlen.) Test: test_mj_page_len.

### The Loop layer's double tap always makes that bar the loop (the owner's, 2026-11-22)
- "In the loop page double clicking a step should always solo that bar, not extend the page range to that bar, you do
  that by holding one step button then pressing a second." A page past the clip's end: its first tap showed it, and the
  second then counted as a ghost bar's confirmation (the loop extended to it) instead of the double tap. Now the double
  tap is checked first: any page (in the loop, the clip's outside it, past the end, a ghost bar reached with Right) is
  the loop alone. A tap only shows a page; a ghost bar's key confirms the new bars only when the bar was reached with
  Left / Right (rfm_move_seq_t.loop_shown: set by a tap, cleared by the arrows and when the Loop layer opens or
  closes). A ghost bar reached with Right and double tapped: the first tap confirms, the second makes it alone, in one
  undo step (loop_tap_grp): one Undo brings back the loop of before. Held + a second step: the range, unchanged.
  (seq_ui.c rfmv_seq_step, rfmv_seq_button.) Test: test_loop_dbl.

### An empty project made current keeps a new project's routing together (found by the host's latch_e2e)
- `rfm_move_project_load` with no blob (the host at start with nothing stored in the slot, a cut of the open
  project) cleared the channels and filters back to the old defaults while the settings kept a new project's Track
  tracks (1, 5, 9): slot 2's pads then played channel 5. It applies a new project's routing, channels and tracks
  now (rfmv_pfw_defaults; hosts without RFM_MOVE_CAP_PROJFW as before; the project is not marked edited by it).

## 0.12.0 (2026-11-22, released in 0.13.0)

The owner's answers on the review page, card by card. New fields in `rfm_move_seq_t` (after `tk_pend_clk`): rebuild
everything. The project blob is version 8 (versions 1..7 load, with the defaults); `RFM_MOVE_PROJECT_MAX` 40396.

**Session view**
- **The stop icon** (n12): a clip on its way out is drawn as the playing one is, filled, with a black square in it
  (was an outline with a white square); recording, which had that square, is a black dot now (`v2_cell` 'S', 'R').
- **No icons lit at rest** (n16): the icons under the steps are dark in session view (0.11.0 lit the stop steps');
  Shift (held, latched) and Track 4 light the shortcuts' as in every view (`rfmv_shift_icons`).
- **Columns that do not reach the firmware played** (n9): their pads at a quarter in every state (a press: full; not
  with no firmware list, nor while a Track is held), their cells' outlines dotted on the screen (`sv_unrouted`,
  `v2_cellDots`).
- **Paging** (c11): Shift + Left / Right 7 tracks, Shift + Up / Down 4 rows (Shift + arrows had no job there).
- **Session view from anywhere** (c9, `rfmv_sv_toggle`): Shift + Menu in a note layout (it did what Menu does);
  Back held + Menu everywhere; Track 1..3 held + Menu (the FM-1 layout's and a takeover's Move-side Shift). From the
  FM-1 layout or a takeover the sequencer runs session view and the slot keeps its layout; Menu (alone or with those)
  goes back to the active slot's own layout. The Track's hold is used up (no menu, tap or double click after it);
  Back's click does not come; a takeover that has Back gets neither Back nor Menu (`takeover.c`). Named (`POP_VIEW`;
  the FM-1 layout: its layout popup). **Back no longer leaves session view** (`back_click`).
- **Clips: Copy and Delete** (k6, c12): Delete held + a clip deletes it at the press into the clip buffer (armed; one
  undo); Delete never pastes (an empty slot: nothing). Copy held + an empty slot pastes; Copy held + a clip pastes over
  it when armed (the last clip function a copy or a delete; not the very clip just copied), else copies it. A paste
  and Back disarm (Back keeps the buffer). Nothing waits after Copy is let go (`sv_armed`).
- **Scenes: copy, paste, delete** (c13, `scene_edit`, `rfmv_seq_scene_copy` / `_paste`): a scene buffer of its own
  (16 clips with their loops and launch settings); Copy + a row copies it or (armed) replaces with the buffer, Copy +
  an empty row pastes, Delete + a row deletes it into the buffer; all 16 tracks; one undo; never a launch. Its cells
  dashed / crossed and its pads blinking while the modifier is held; "Scene 3 copied", "Scene 3 deleted", "Pasted to
  scene 5", "Scene 5 replaced".
- **Copy and Delete held show an icon** at the press (scissors, two sheets): track view the roll's top right,
  session view the scene column (`rfmv_seq_held_icon`). Nothing of the kind was in this repository's history: 0.6.3's
  "Hold Delete / + a step" text (on the release) went in 0.6.4, and the cut mark (the X) shows only after a cut.
- **Copy tapped alone does nothing in every view** (track view: it duplicated the clip, "Duplicated", and dropped a
  waiting copy). Delete tapped alone: nothing (its spoken hint stays).
- **Launch quantization** (c15): Global settings' new last row **Launch**: Instant, 1 Beat, 1 Bar (default), 2 Bars,
  4 Bars, Clip (the end of the playing clip's loop; none: the next bar), saved with the project. **Each clip's own
  Launch** (Global or one of those) and **After** (Loop; Next: at its loop's end the clip below it, or the top of its
  run, seamless). Copied, pasted, duplicated with the clip; journaled (`OP_CSET`); `launch_due`, `rfmv_seq_lq_of`,
  `rfmv_seq_after_next`. Instant launches at once in the bar's phase (as a double click), so nothing waits or flashes.
- **A column's select step held** (c16, `cs_*`): held 300 ms, or a knob touched or turned while down: the track's
  selected clip's Launch (knob 1) and After (knob 2), the knob's tick and row marked; knobs 3..8 dark and dead; an
  empty slot "No clip". The volume knob sets the level of the firmware the track plays (`rfmv_fw_level_turn`, the
  level popup) or says "Not routed". A quick click only selects.
- **r2**: a double click on an empty slot stops its track at once only when it plays something; Shift + a scene with
  nothing of it playing changes and says nothing.
- A clip's "made" bit is journaled (`OP_MADE`): an undone paste or delete leaves the slot as it was (it left a clip
  with no notes).

**The arp, octaves and held steps** (n2, n3, a9, a11)
- **Shift + step 11 is gated**: the press turns the step white and nothing else. A click (let go before 300 ms) opens
  the arp menu at the release, or closes it. A hold of 300 ms turns the arp off or back on as it was, while still
  down; the menu stays as it was and the release does nothing.
- The hold and the jog's click show "Arp" over "On" / "Off" (or the mode it came back to) on the whole screen for a
  second, spoken (`POP_ARPW`, `rfmv_seq_arp_mode_name`). While the arp is off the menu's pictures (the walk, the
  lanes) are dimmed with a 50 % checker; the header stays plain.
- **The lane pages take the step keys, the main page does not.** On entering a lane page the 16 steps flash white
  and a wave settles them left to right (360 ms, 30 LED writes; a step pressed meanwhile works at once and ends it).
  Back on the main page the clip's lights come back at once. A step past the lane's length is dark and dead.
- **Step 1 held + step n** sets the lane's length to n ("Length 12"); pressing n again gives 1. Step n is not picked.
- **A lane step held 300 ms** (or a pad pressed while it is down) takes the pads as its shortcuts, and none plays
  (`rfmv_seq_al_take`, `rfmv_seq_al_holding`). MOD: the bottom row play, rest, tie, repeat, lowest, highest, power
  chord, chord; the row above the played notes 1..6. TRANSPOSE: -16..+15 from the bottom-left, 0 and the octaves
  marked. The step's value is white; a pad sets it at the press ("5 Tie", "3 +7"). Letting go gives the pads back at
  once; a quick click only picks the step. Notes already held play on until let go.
- **Up / Down in track view** show "Octave +2" with the pads' note range ("C4 - E6") for a second, spoken, and a
  white row sweeps the pads the way the octave went (a row every 50 ms; 64 pad writes). At the range's end the popup
  shows the unchanged octave and there is no sweep (`POP_OCT`, `rfmv_seq_oct_range`).
- **A held step's Up / Down** shows its notes after the move ("B2 Eb3 Gb3"; `POP_TRIG` 8, `rfmv_seq_held_names`).
- **A held step + a later step is a toggle**: the span's length, or 1 step when the held step already lasts more
  than span - 1 steps and at most span. Step 1 held + step 2 again and again gives 2, 1, 2.
- New `rfm_move_seq_t` fields after `arp_was`.

**Buttons, menus and the audit** (l2, l3, a2, a5..a8, a10, a12..a16, a17, q1, q2)
- **Held controls take over an open Shift menu** (l3, a17): a held step has the knobs, the jog, the volume knob and the
  arrows over it (its popups drawn over the menu; `rfmv_seq_held_takes`); Loop held shows its pages and has the jog,
  latched after the menu was opened the same until Back (`rfmv_seq_loop_over`, by the layers' order); Copy, Delete,
  Mute held show their pictures (`rfmv_seq_mod_over`); Shift's shortcuts as ever (the arp's lane pages give the step
  keys to any held modifier). The menu keeps its knobs, lit, under all but a held step, and is back at the release. A
  Track 1..3 tap under a menu: the slot changes, its popup a second, the menu stays (tested now).
- **Menu never closes a menu** (l2): in the FM-1 layout Menu is the FM-1's HOME with a menu open; Global settings
  lets Menu pass too (the view flips under it). Only Back (and Shift + the menu's own step) closes one.
- **Loop and Mute** (a2): a quick click (let go within `RFM_MOVE_SEQ_HOLD_MS`, nothing else pressed: `mods_other`)
  latches the layer, a click again or Back ends it; a hold is the layer until the release (held longer and let go
  unused: no latch). Mute's latch is a layer of its own (`LY_MUTE`, `mute_latch`; Back's list and light); Mute's tap
  no longer mutes the selected track. Said "Mute on" / "Mute off".
- **Every Shift menu names its knobs** (a5): the arp's header generalised (`smenu_hk`, `smenu_head`, `smenu_top`):
  the touched or just turned knob's name and value on top (the jog's when it turned), a tick a knob in use at y 17,
  the active one long; at rest the menu's own top. Workflow, Tempo, Groove, Layout, Keys & Scales (its own screen now:
  the key on top, the root row, the layouts' icons, Global / Track), Velocity, Record laid out for it (Groove's amount
  no longer runs into Global / Track). A turn at a range's end says the header's words.
- **A menu over the Mixer, Routing, the FM-1 layout** (a6, a7): an open menu owns the knob lights (`rfmv_menu_knobs`
  asks for it first), the touch (its header; no FM-1, CC or track popup) and the turns (never the firmware). Global
  settings has its jog over the projects view (it went to the clock source). Its knobs' jobs from before 0.6.2 (clock,
  velocity, level, popups) were a leftover: removed, dark and dead.
- **Track 4 held is a whole layer** (a8): nothing of the hold before its 300 ms but its own light (the icons waited no
  more); then everything at once, or at once when anything is pressed meanwhile (`rfmv_hold_shown`). Up, Down, Left,
  Right, Menu, Copy, Delete, Mute, Shift, the other pads, the knobs and the jog are dark and dead in it (`t4_menu_white`;
  Copy, Delete, Mute had no Track 4 job in the code: README's "Track 4 held + Delete / Copy" rows were the lock menu's,
  Tracks 1..3 since 0.8.0, and say so now). A menu a Track 4 shortcut opened owns the knobs and jog while it is down
  (`t4_opened`).
- **FILL shows** (a10): the Loop light blinks every 100 ms, "Fill" on the whole screen, and a diagonal of pads in the
  track's colour travels across the pads (`rfmv_seq_fill_on`; some 60 pad LED writes a second, 8 at most at once);
  pads still play; all back at once.
- **Knob lights that match** (a12): the TRIG page's knob 8 has no popup; a held step's unlocked CC knobs dim; the
  project grid's knob 1 only while a project's pad is held, its touch "Colour" and the colour's name
  (`rfmv_proj_colour_name`); the FM-1 layout's knobs with a "" label (no job) dark.
- **Dead presses** (a13): Undo / Redo with nothing: "None" a second, spoken (`SEQ_MSG_NOUNDO`); Back's light only
  while its click has a layer to close (`rfmv_back_has`), full at its press; Shift + Sample's light dark.
- **The browser's filter icons** (a14): lit only for a filter that is on (Capture held: the firmware's tags;
  `rfmv_fw_step_icons`); a step white at its press (`fw.steps_down`).
- **Knob popups over a takeover's screen** (a15): an unclaimed knob's popup over the firmware's screen, in Global
  settings' popup place (whole screen or the left half). README: a firmware that sends readouts shows its values;
  without, the detents (a16).
- **A colour per firmware slot** (q1): orange, blue, yellow (`RFMV_SLOT_RGB`; `rfmv_fw_rgb` is the slot's now) on the
  Track buttons, the routing lights, session view's knobs, the Mixer's meters, Routing's slot pads; session view's
  columns the lowest slot their track reaches (none: grey; no host: the track colours). The browser keeps the
  firmware's colour. Solo purple (was blue).
- **Step 13 and clearing a clip** (q2): Shift + step 13 has no job (dark, dead); Delete held + Play clears the selected
  clip (what step 13 did), the transport as it was, in track and session view.
- `design/UX-RULES.md`: R3 and R6 additions, the layers reordered (held controls above the Shift menus).
- New `rfm_move_seq_t` fields after `oct_ms` (`mute_latch`, `mute_end`, `loop_ms`, `mute_ms`, `fill_ms`, `t4_opened`,
  `mods_other`), `rfm_move_fw_state_t.steps_down` at its end: rebuild everything.

## 0.11.0 (2026-11-21, released in 0.13.0)

The owner's feedback on the arp, the held Track buttons and session view, and their UX rules (`design/UX-RULES.md`:
feedback at once, gated click / hold / double click, a layer takes what it uses, lit knobs, knob labels, step icons).
The board, drawn from this build: `remoteFM14Move/docs/design/ux-0.11.movedesign.json`.

**The arp menu**
- **A header** names the touched knob and its value (2x where it fits), or the knob turned last for a second; at rest
  the page: "Arp On", "Arp Off", "Mod", "Transpose". Under it a tick a knob the page uses, the active one long. (The
  knobs showed nothing: the ring legend only.) The range moved over the walk ("Oct 2").
- **The jog's click turns the arp off and on again** as it was (`rfmv_seq_arp_toggle`); Shift + step 11's icon is
  full while it is on.
- **The lanes**: the 16 step keys are the lane's 16 steps (a press picks one; the menu has the steps while a lane is
  open, none reaches the clip). Knob 1 the step's function or semitones, knob 2 the length, knob 3 RESET (were: knob
  3 the step, knob 2 RESET, knob 4 the length). The MOD lane is drawn as a sign a step, not bars; the step just
  played is ticked on the screen and green on its key.
- **Menu no longer closes a Shift menu**: the view flips under it (track <-> session), named a second (`POP_VIEW`);
  Back closes the menu. The menu's knobs work in session view too (they did nothing there), and a knob's touch there
  no longer selects a track under the menu.

**A held Track 1..3**
- **Gated** (`tk_press`, `tk_click`, `rfmv_seq_ui_poll`): a hold routes the track or not once held 300 ms, while
  still down (it acted at the release); a click selects once the double click's 300 ms have passed (or the Track
  button is let go, or something else is pressed); the double click solos at its second press and selects nothing.
- **The steps' lights**: not routed dark (was dim red), routed the firmware's colour, the selected one blinks, the
  solo blue, pressed white.
- **A solo stays** when another track is selected (`rfmv_route_on`: that track is routed once the solo ends).
- **The screen** at rest: the slot at a glance (`rfmv_slot_screen`): firmware, layout, latch, key, and the 16 tracks
  routed or not, the selected one, the solo. A step acted: the track and what happened over the same row.

**Session view**
- **Its own layer**: a Track 1..3 tap changes the firmware played and stays in session view (`rfmv_fw_activate_keep`),
  its summary a second (`POP_SLOT`). The knobs are lit there: a column whose track reaches that firmware in its colour,
  the others faint white, knob 8 green. A Track held: the pads are the routing (a column: a click routed / not, a double
  click the solo); the knobs, the jog and Up / Down do nothing there.
- **A scene pad starts the transport** when stopped (the owner's exception to "only Play starts"; Rec armed: the
  count-in). Knob 8 touched: "Scenes" (the scene that plays or waits, the rows in view).
- **Handover shown**: a playing clip whose track has another clip (or a stop) waiting flashes with a stop icon, its
  pad between green and its own colour; the clip that waits flashes with the play icon; a scene's box the same (a play
  icon on its way, a stop icon on its way out).
- **A double click is now**: a clip plays at once (in the bar's phase), an empty slot stops its track at once, a scene
  launches at once. (A double tap on the playing clip stopped it at the bar: its stop step does that.) **Shift + a
  scene pad** stops the scene's clips at the bar; a double click: at once.
- **The stop steps' icons are lit** (a step that is a function), full with something to stop; the select steps' are not.

Tests: `move_sessview` 958 checks (the handover, the double clicks, the scene stop, the routing pads, the gated steps,
the arp's header, click and lanes); `move_seq`, `move`, `move_tracks`, `move_speech` moved to the gated timing.

## 0.10.0 (2026-11-21, released in 0.13.0)

Session view after the owner's review as an Ableton Live user (14 answers on the review page; the board, made before
the code: `remoteFM14Move/docs/design/session-view-v2.movedesign.json`). Items not answered are unchanged (Delete on
clips, the double tap's stop, the way in, paging, scene copy / clear).

- **A scene column** (as the Move's Control Live mode): the pads' 8th column launches scenes, columns 1..7 are tracks
  (7 of 16 in view; Left / Right still one track). The scene pads are green: very dim no clip in the row, dim clips,
  pulsing the scene that plays, blinking on its way. The screen's 8th column: the scenes' numbers (underlined: the one
  that plays). Shift + a clip no longer launches its row.
- **An empty slot pressed is selected, not filled**: no clip is made (the first note makes it); the track stops at the
  bar, as Live's stop button. With Rec armed it still records a new clip there. **A project loaded: clips with no
  notes or locks are empty slots again** (every press on an empty slot made one before: the owner's project had 8 of
  them in 9 clips). The blob is unchanged.
- **Clips with no notes are marked**: the pad very dim, the cell its corners only (`v2_cell` 'H', 'h').
- **Live's colours**: a playing clip pulses green (the selected one white on the beat), the track's clip while stopped
  is steady green (Play starts it; on the screen an outline with the play icon, `v2_cell` 'A'), a stopped clip its
  track's colour, dim. Queued, stopping, recording, cut and copied as before.
- **Shift + a slot selects it** for editing: nothing launched or stopped.
- **The steps under the columns** (Control Live's): the odd one selects the column's track, the even one stops it;
  step 16 stops every clip. (They were stop and mute.) Mute: Mute + a clip, or Mute + a step (the 16 tracks, as in
  every view). **Shift + Play** restarts the clips in session view too (it stopped every clip there).
- **A knob (1..7) touched** selects its column's track, as before, and shows the track's popup while touched
  (`POP_TRKINFO`, `rfmv_seq_track_info`): the track, the firmwares it reaches, its selected clip and length, its
  channel. Said too.
- **Copy tapped alone does nothing in session view** (the owner's: Copy works held). In track view it still
  duplicates the clip, as the Move's Note Mode.
- Only Play starts the transport, launches wait for the next bar, a scene stops the tracks with no clip in it,
  recording into an empty slot: kept, as answered.
- Tests: `move_sessview` rewritten for it (878 checks); `move_seq`'s session parts and its screen hash.

## 0.9.2 (2026-11-21, released in 0.13.0)

Session view (the owner, on the Move: "Scenes don't launch, clicking a clip won't show as selected, the screen and
the pads don't seem aligned. I cannot cut and copy clips in the same way you can cut and copy steps"). No fault was
found in the launches or the mapping: on the simulator, with the Move's own build, project and firmware states,
clips launch, Shift + a clip launches its row and the pads match the screen. What was found and changed:

- **The pulse in steps.** A playing clip's pad faded through a new level every tick (96 a beat): in session view
  every playing clip was an RGB write a tick, about 630 SysEx writes a second with four clips at 99 BPM, against 13
  in track view. It fades in 6 steps a beat now (`RFM_MOVE_PULSE_STEPS`): about 40 a second in the same view.
  Unconfirmed on the hardware whether the flood was what kept the pads from showing a press.
- **The selected clip on the screen:** a frame round its cell (16 x 15, in the gap between the cells; a filled cell
  stands clear of it), at once when a clip is pressed, also while it waits for the bar. Before, only the bar over
  its track's column said anything. The board: `remoteFM14Move/docs/design/session-view-v1.movedesign.json`.
- **Clips are cut, copied and pasted as steps are** (0.6.3's rules): Delete held: a clip is cut; the press right
  after a cut pastes (a clip there is replaced); an empty slot gets the clipboard; the clipboard stays and pastes
  again (a cut clip pasted once emptied it). Copy held + a clip copies, + an empty slot pastes; Copy let go, then a
  press: pasted there once. Said: "Pasted to track 1 clip 3", "Track 3 clip 1 replaced".

**Tests:** `move_sessview` (new): session view reached as on the Move (a firmware's Track tapped, Menu), a test a
reported problem (scenes; the selected clip on the pad and the screen; the screen against the pads over the 16 x 16;
cut / copy / paste), the LED writes of a playing session view, and session view entered while a takeover's clock
led the sequencer.

## 0.9.1 (2026-11-21)

The takeover with proposal 3 (the move-takeover session's ecb9272, 76923a0 on 0.9.0):

- **Back in a takeover** is the firmware's when it claims the buttons but not the Track buttons (no volume + Back
  exit there: a Track button is the way out). While a takeover is shown its MIDI clock leads the sequencer (as an
  external clock), so the firmware's Play starts and stops RemoteFM1's sequencer.
- **The volume knob** (not claimed) shows the host's level popup over the firmware's screen.

## 0.9.0 (2026-11-20)

Proposal 3, "one main layout" (the owner's board `remoteFM14Move/docs/design/track-buttons-v3.movedesign.json`,
approved 2026-11-20). New fields at the end of `rfm_move_t`: rebuild everything.

- **A note layout is the sequencer.** Scale, Chroma, Drum and Octave play and edit the slot's selected sequencer track
  (`settings.lock[slot]`): the pads into the track (live to the slot), the steps its clip, the knobs the TRIG page first
  (then the CC banks), the buttons the sequencer's, Menu track / session view. Entering it selects the track, routes
  it to the slot and gives it the slot's layout. The track's channel, key and CC numbers win; the slot keeps its layout
  and its track. `rfm_move_set_mode(MIDI)` is the slot's note layout (Sequencer mode).
- **No separate Sequencer mode:** Back with nothing open does nothing (it closes layers, session view too).
- **Track n held: one menu** in every layout: the pads the layout menu, the steps the 16 tracks for firmware n (a tap
  selects and routes, a double tap solos on that firmware, a hold unroutes), the jog the root, Up / Down the octave,
  the scale pads the key the tracks use, the knobs the selected track's CC numbers (in the sequencer).
- **Per-firmware solo, one at a time** (`m->fsolo[]`): only that track reaches the firmware; another solo there moves
  it; again: off, the filter as it was. The sequencer's own solo is untouched.
- **The firmware browser:** a pad double-tapped (300 ms) loads that firmware (as the jog press; the catalog: installed
  and current only); the browser stays.
- **Clock out:** the sequencer playing on its own clock sends MIDI Start, clock (24 PPQN) and Stop to every loaded
  firmware: MIDI actions with `dest` = `RFM_MOVE_DEST_ALL` (255), frame-stamped.
- **Speech:** "Felucca, Drum layout, Track 2", "Felucca, layouts, Drum, Track 2", "Track 6, selected".
- **Latch** (the menu's pad 8) holds a note layout's live notes; a recording keeps the length played.
- **Track 4 held + a step** shows the Shift menu at once (as Shift + step in the sequencer), not after the release.
- **The Mixer and Routing** take the Shift shortcuts (Shift + a step, the icons lit); a Shift menu opens over them and
  Back closes it there. **Track 1..3 held** works over the projects view too (the layout menu and the tracks).
- Fixed on the way: a restart from saved settings plays the slot's note layout; Track n held + a knob sets the CC
  number of the track selected for firmware n; `rfm_move_set_mode(SEQ)` is the slot's note layout; the browser's
  double tap is timed on the input's clock; a Mute is said once.

## 0.8.0 (2026-11-20)

The Track buttons, proposal 2 (the owner's board `remoteFM14Move/docs/design/track-buttons-v2.movedesign.json`,
approved 2026-11-20: "Yes, build it"). Includes 0.7.0's takeover. New fields in `rfm_move_settings_t` and at the end
of `rfm_move_t`: rebuild everything.

- **Track 1..3 are the firmware slots, always.** A tap plays that slot in its own layout (FW_ACTIVE; empty slots too);
  held: the slot's layout menu (top row FM-1, the firmware's takeover (dark with none), Scale, Chroma, Drum, Octave,
  pad 8 latch; rows 2..4 the 24 scales; steps the MIDI channel; the jog the root; Up / Down the octave; knobs the CC
  numbers in a note layout); a double click (pressed again within `RFM_MOVE_DOUBLE_MS`, 300 ms, of a tap's release):
  the firmware browser on that slot. The first tap acts at once.
- **Each slot keeps its own layout, scale, root, channel, octave and 8 CC numbers** (`settings.slot[]`,
  `rfm_move_slotset_t`, `RFM_MOVE_LAY_*`; `rfm_move_set_slot_layout`, `rfm_move_slot_layout`). Settings version 7
  (130 bytes); version 6 and older load with every slot as the one mode was.
- **Control mode is gone:** every note layout sends the Control CCs on its knobs (CC 16..23 at first, per slot);
  Shift + a knob (or Track n held + a knob) sets its CC number. In a note layout the buttons are RemoteFM1's own (no
  FM-1 buttons): Up / Down the octave, Play the sequencer's transport, Shift + a step the Shift shortcuts. Only the
  FM-1 layout reaches the FM-1's buttons and knobs.
- **The FM-1 layout:** knob 1 SELECT, 2 PRESETS, 3 ALGO, 4 nothing, 5..8 K1..K4 (`rfm_move_knob_enc`); the jog turns
  SELECT, PRESETS or ALGO and its click cycles them (a popup "Jog / PRESET" with three dashes).
- **Track 4, the sequencer side:** a tap opens the Mixer or Routing (the last one; the Mixer at first), a tap there
  flips them; held: pad 1 the Mixer, pad 2 Routing, the steps the Shift shortcuts (was Track 1 held), Record, Play,
  Capture, Undo, Loop, Sample their Shift + button; a double click: the sequencer's projects. Without FX (one firmware,
  no FX hosts) a tap opens the projects.
- **Back** with nothing to close flips between the slot's layout and the Sequencer (was: the projects view). Back +
  Track no longer switches slots (Track does).
- **The Sequencer:** Track n's tap selects the track locked to firmware n (`settings.lock[]`, `rfm_move_seq_lock`;
  track n at first); Track n held: the lock menu, the steps the 16 tracks: a tap locks one (its colour), a double tap
  solos it (blue), a hold stops it reaching that firmware (dim red: the firmware's 16-track filter, ROUTE); Copy /
  Delete / Mute + a step, the knobs, the jog and Up / Down as Track 4 held did.
- **Screens** (the board's, `rfmv_pop_screen`): the layout ("Felucca / Drum", six dashes), the Track 4 page, "Knob 1 /
  CC 74 / Felucca", the lock ("ChoralRoot / Track 3 / not routed"), "Jog / PRESET". **LEDs:** the board's colours; the
  Track LEDs everywhere the slots' firmware colours (the one played full), Track 4 red on the sequencer side.
- **Speech:** "Felucca, Drum layout", "ChoralRoot, FM-1 layout"; held menus "Felucca, layouts, Drum", "Track 4,
  Mixer", "ChoralRoot, lock, Track 3"; "Knob 1: CC 74", "Track 7, not routed", "Jog: Preset".
- **The takeover** is pad 2 of its slot's layout menu (was Track 1's tap); while a Track is held its menu takes the
  pads, steps and knobs from the takeover. A firmware that loses its takeover puts its slot back on FM-1.
- `rfm_move_set_mode`: FM1 / MIDI / TAKEOVER set the active slot's layout, SEQ the Sequencer, CONTROL is MIDI.
  `settings.fx_page` defaults to the Mixer.

**Tests:** `move_tracks` (new); the other suites moved to the new gestures.

## 0.7.0 (2026-11-20)

The takeover (FM1VST `docs/RFM1-TAKEOVER.md`, `tk1`): a firmware's own Move interface beside FM-1, MIDI, Control and
Sequencer mode. New fields at the end of `rfm_move_act_t` and `rfm_move_t`: rebuild everything; `src/takeover.c` is
a new source (web/build.sh lists it).

- **Host API:** `rfm_move_set_takeover(m, slot, claims, name)` (claims `RFM_MOVE_TK_*`; 0: none, an open one
  closes), `rfm_move_tk_screen(m, slot, packed)`, `rfm_move_tk_leds(m, slot, rgb[92][3])` (only the claimed ones;
  whites r 0..255, scaled to the Move's 0..127), `rfm_move_tk_say(m, slot, words, interrupt)`, `rfm_move_tk_shown(m)`, `rfm_move_mode(m)`;
  `RFM_MOVE_MODE_TAKEOVER` (4: never in the settings), `RFM_MOVE_L_*` (the LED list's order).
- **Actions:** `RFM_MOVE_ACT_TK_INPUT` (31: slot, `pkt[4]` the raw packet of a claimed control, or a release the
  core makes up) and `RFM_MOVE_ACT_TK_STATE` (32: slot, down on / off, `tk_claims`, `tk_flags` bit 0 speech on).
- **Track 1** in FM-1 mode opens the active slot's takeover (its name, the whole screen; said "Chords, takeover");
  in it (TRACKS free) FM-1 mode again; from MIDI, Control, Sequencer mode whichever the slot was left in. **Volume
  touched + Back** leaves any takeover (Back not sent). A slot switch closes it; the slot switched to opens as it was
  left. The projects view and the Shift menus close it while up.
- **Routing:** claimed input to the firmware only (a release follows its press); the rest as in FM-1 mode. Back with
  BUTTONS but not TRACKS is held back (press and release at its release, no Track meanwhile). Everything the firmware
  holds is let go before `TK_STATE` off, when a claim is dropped, and in `rfm_move_release_all`.
- **Screen and LEDs:** the firmware's screen with SCREEN (only the name popup and the host's messages over it); each
  claimed LED the firmware's, the rest FM-1 mode's.
- `rfm_move_set_mode(m, RFM_MOVE_MODE_FM1)` now leaves a takeover; `RFM_MOVE_MODE_TAKEOVER` opens one.

**Tests:** `move_takeover` (new, 110 checks).

## 0.6.13 (2026-11-20)

Knobs with a job light up and answer a touch (the owner: "The favorite move should light up the LED under the knob,
also the knobs should react to touching them to see the parameter/value").

- **The firmware view:** knob 2's LED is lit in the favourites' colour (pink) while it has a job (a favourite
  selected in the Favorites list, no filter); knob 1's in the firmware's colour while one is selected (not in the
  catalog or the states; was lit there too). A touch shows the knob's popup at once (the layouts as before, no new
  screen): knob 1 the colour, knob 2 "SLOOP / position 3 of 17"; speech "Colour: Red", "Move SLOOP, 3 of 17" (a turn
  still "SLOOP, position 3 of 17"), per the Speech mode. No job: dark, a touch nothing.
- **FX setup** already lit its knobs with a parameter and showed it on touch; the Mixer's knobs 1..3 are now lit for
  every slot they set (an empty slot's level too; they were dark there).

**Tests:** `move_fw` the LEDs dark with nothing selected and in All (knob 2), lit with a favourite selected, the
touch popups; `move_speech` the touch words.

## 0.6.12 (2026-11-20)

Speech modes in Global Settings (the owner: "Off(Default), On(Always on like it works now), or Jogwheel(Only speaks
if you are also touching the jogwheel)"). New fields at the end of `rfm_move_settings_t` and `rfm_move_t`; the
settings image is version 6 (88 bytes; versions 1..5 load, the mode then not chosen): rebuild everything; the web's
`SETTINGS_BYTES` is 88.

- **Off** (the default): the core says nothing (it keeps tracking the screen). **On**: as before. **Jog**: words only
  while the jog wheel is touched; the touch says the screen as it is (`rfm_move_words`), then every word while held;
  let go: silent; a new touch starts again (the jog's touch passes through Global Settings' ownership).
- **The Speech row**, first in Global Settings (Off, On, Jog; said "Off", "On", "Jog wheel"); saved in the settings
  (`settings.speech`: 0 not chosen, 1 + `RFM_MOVE_SPEECH_*`). Its change is said in any mode: "Speech off", "Speech
  on", "Speech jog wheel". Five rows now: four show and the list scrolls (direction A's 11 px rows kept, a 2 px bar).
- **The default:** `rfm_move_set_speech_default(m, mode)`: the mode while the settings have none (the first run, an
  older image): `RFM_MOVE_SPEECH_ON` when the system's screen reader is on.
- **Host API:** `rfm_move_speech_mode(m)` (0 off, 1 on, 2 jog), `rfm_move_speech_open(m)` (1: words may be said now:
  On, or Jog with the jog touched); hosts keep `rfm_move_set_speech(m, 1)` on and gate their own words on it.
- `rfm_move_words` in Global Settings adds the selected row.
- Web: `core.setSpeechDefault(mode)`, `core.speechMode()`, `core.speechOpen()`, `SPEECH`; `SETTINGS_BYTES` 88.
- Build: Arm GCC's `-Wrestrict` in speech.c (snprintf between fields of the core: a bounded copy now) and its
  `-Wformat-truncation` notes (screen.c, seq_ui.c, speech.c: buffers sized to their words) gone; a full `-O2 -Wall
  -Wextra` compile with Arm GCC 14.2 is clean.

**Tests:** `move_speech` 334 checks: Off says nothing; the default only while nothing is chosen; the Speech row's
changes said (Off too) and saved (a SETTINGS action); Jog silent until touched, the screen on touch, words while
held, silent again, a new touch; Jog in Global Settings; `move` the settings image (version 6, versions 5 and 4
loading); `web_smoke` 37 (the mode Off by default, the default On). Contact sheet
`rfm1cs-0.6.12-global-speech.png`.

## 0.6.11 (2026-11-20)

For firmwares that speak for themselves (fm1-reader: their SysEx SAYs, about 30 catalog firmwares, the host speaking
them). New field at the end of `rfm_move_t`: rebuild everything.

- **`rfm_move_set_fw_speech(m, fw_speaks)`** / `rfm_move_fw_speech(m)`: while set, the core says nothing for the
  firmware's controls: an encoder turned (its count, its readouts, the range's end repeat), a readout's own popup, an
  FM-1 button's or key's label or press. It keeps RemoteFM1's own words: modes, the projects view, menus, the volume
  (never a readout then), held Track menus, Global Settings, the sequencer, and a touched knob's name (its label).
  Off by default; set any time. Hosts no longer need to match popup numbers from `move_int.h`.

**Tests:** `move_speech` 309 checks: with it set, a touch says the name, turns, readouts, the end repeat, a readout
popup, a button press and a held label say nothing, the volume, a mode, a held Track menu and Global Settings speak;
off again, the knob and button words return.

## 0.6.10 (2026-11-20)

Global Settings redrawn (the owner: "it should have battery at the top with a battery icon and bigger text"; their
approved direction A, the design canvas's boards "A · Battery header + list" and "A · editing a value").

- **The battery is the header:** an 18 x 12 outline at 3, 3 with a 2 x 6 nub, filled 14 x 8 to the charge; the
  percentage at 2x (the font of the titles and knob popups) at x 27; "GLOBAL" small, right-aligned; unknown: the
  empty outline and "--". A 1 px rule at y 17.
- **Four rows** of 11 px from y 19: Clock, Velocity, Fixed level, Popup size (the name at x 3, the value right-aligned
  to x 122; more air than before); the battery left the list, so all fit and no scroll bar shows. The selected row
  is inverted; editing (a jog click) frames the row and inverts its value between 3 x 5 arrows.
- **Speech:** opening says "Global settings, battery 87%" ("battery unknown"), then the rows as they move. The
  controls' ownership (0.6.2) is unchanged.
- The v2 canvas port's old `menu.global` board in `test_move_v2` stays as that canvas has it (direction A supersedes
  it; its pixels are checked in `move_seq`).

**Tests:** `move_seq` the four rows, the battery's fill (63 %: 9 of 14 px) and empty outline; `move_speech` the
opening words, the rows, the end of the list; the contact sheet `rfm1cs-0.6.10-global-settings.png` (selecting,
editing Velocity, the battery at 63 %).

## 0.6.9 (2026-11-19)

Favourites reordered (the owner: "be able to reorder your favorites. Use knob 2 to change the order").

- **Knob 2 in the Favorites list** (no filter on): the selected favourite moves one place up or down the list a step
  (a step every few ticks, as knob 1's colours); the pads follow live, the selection stays on it, the bank shown
  follows it; the ends hold. The popup reuses the favourite popup's layout ("SLOOP / position 3 of 17"); speech
  "SLOOP, position 3 of 17", once a move. Nothing in All, the catalog or a filtered list (knob 2 did nothing there
  before either).
- The order is the library's (its favourites list, FW_LIBRARY): it survives a restart and every Favorites list shows
  it. Favourites not listed (not installed) keep their places.
- **API:** `rfm_move_fw_favorites(m, ids, cap)` (the ids in order, the count); `rfm_move_fw_favorites_set(m, ids, n)`
  (the given ids first in that order, the rest after as they were; -1 NULL, 0 no change, 1 done and on screen).

- **Knob speech, the host's sweep of 0.6.8:** a touch then a turn said the name twice before the value ("Voicing",
  "Voicing", "Voicing: +1"): the range's end repeat (no readout within 250 ms) said the name when no value was known,
  for a turn short of a detent. Now the repeat says nothing then: the name once on touch, then "Name: value" when the
  readout comes. A full title is reused on another page (the Shift page's first touch) when its readout named the
  control itself (`control` >= 0) and the label is the same ("Bass Register", not "Bass Regis").

**Tests:** `move_fw` `test_fav_order` (down, two up, the top holding, short of a step, the library's round trip, the
getter, the setter, All untouched); `move_speech` the words and the end saying nothing new; the sweep's gesture (touch,
half a detent, a detent, no readout past 250 ms: the name once) and a named control's title on the Shift page.

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
