# Changes

Versions are git tags (`vMAJOR.MINOR.PATCH`); hosts take this repository as a submodule pinned to one.
`RFM1CS_VERSION` in `include/rfm1cs/move.h` and `rfm_move_version()` name it.

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
