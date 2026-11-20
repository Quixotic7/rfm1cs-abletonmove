// SPDX-License-Identifier: MPL-2.0
// rfm_move in the browser: the Ableton Move core (src/move.c, README.md has the mapping) as
// WebAssembly (web/build.sh -> rfm_move.mjs), wrapped for a web host (RemoteFM1 js/move-link.js). No DOM: Node runs it
// (RemoteFM1 web/site/test.mjs).
//   const core = await MoveCore.load(settingsBytes?)          (the module once per page, a core per call)
//   core.input(packet, frame, nowMs) -> actions                packet: the INPUT record's 4 bytes as a LE uint32
//   core.panel(levels44, playGreen); core.leds() -> [{ rgb, cc, number, r, g, b }]   (only the changed ones)
//   core.screen(rgb565 | null) -> Uint8Array(1024)             (the Move's packed 128 x 64)
//   core.save() -> Uint8Array(80); core.load(bytes)             (load: a fresh core with those settings)
//   core.setLabel(enc, text); core.setMaster(v); core.setKeymap(km | null); core.setMode(m) / releaseAll() -> actions
// Actions (move.h rfm_move_act_t, matrix order):
//   { kind: "key", k, down, vel }  { kind: "press", k, value }  { kind: "btn", m, down }  { kind: "turn", enc, steps }
//   { kind: "master", value }  { kind: "midi", bytes }  { kind: "settings" }
//   { kind: "projectSave", slot, bytes }  { kind: "projectLoad", slot }  { kind: "projectDelete", slot }
// and every action has `frame`: for seqAdvance's, the offset from the call's start it is due at (0 otherwise).
// The sequencer (README.md, "Sequencer"):
//   core.seqAdvance(frames, rate) -> actions (every block, playing or not); core.seqMidiIn(bytes) -> actions
//   core.seqPlay(on); core.setBpm(bpm10); core.setLead(us); core.setClock(mode); core.bpm10(); core.following(); core.playing()
//   actions also: { kind: "click", accent } (the metronome), { kind: "audioRec", on, bars } (Sample's loop), frame-stamped
//   core.projectLoad(slot, bytes | null) -> actions, or null (an invalid blob); core.projectDir(bits);
//   core.projectFlush() -> actions; core.projectSlot(); core.projectSave() -> the current blob; core.projectCheck(bytes)
//   core.projectInfo(slot, bytes): a stored project's content for the screen's project grid (0 ok)
// The session (include/rfm1cs/session.h: the link to the Move, transport-free) and the store (store.h): every sess* /
// store* call returns { evs, res }, evs in order:
//   { type: "send", bytes }  { type: "open", attempt }  { type: "close" }  { type: "action", act }  { type: "released" }
//   { type: "lost", why (WHY.*), wasLive }  { type: "bye", reason (BYE_*), wasLive }  { type: "status", changed (CH.*) }
//   { type: "loaded", slot, ok }   (the store's LOAD answer went to the session; ok false: not a valid project)
//   core.sessStart(now) / sessStop(now) / sessTick(now); the transport's, with the attempt its "open" gave (an older
//   one's are ignored): sessOpened(now, attempt) / sessRx(now, attempt, bytes) / sessClosed(now, attempt, final)
//   core.sessWait(now, ms, patient); core.sessGiveUp(tries); core.sessNextMs(now) -> ms | -1
//   core.sessPanel(now, levels, green) / sessFrame(now, rgb565 | null) / sessRedraw(now) / sessFlushLeds(now)
//   core.sessPumped(now) / sessUpdate(now) / sessFlush(now) / sessRelease(now)
//   core.sessProjectsInit(now, bits, slot, bytes | null) / sessProjectAnswer(now, slot, bytes | null): res 0 / -1
//   core.sessStatus() -> { state, everLive, waiting, retries, retryMs, via, proto, caps, battery, ... }; sessName(), sessShown()
//   the words to show (session.h; words { host, closed, link, self }): core.sessText(words) -> { tone, text } (tone
//   "off" / "busy" / "on" / "bad"); core.whyText(why, words); core.lostText(why, wasLive, words);
//   core.byeText(reason, wasLive) -> { tone, text }
//   the project file (store.h): core.filePack([{ slot, saved (Unix s), bytes }], kind) -> Uint8Array;
//   core.fileUnpack(bytes, self) -> { kind, entries: [{ slot, saved, bytes }] } or { error: "Not a Move project file: .." }
//   core.setStoreHost({ read(slot) -> STORE_PENDING | 0, write(slot, bytes) -> 0, remove(slot) -> 0 }); core.storeInit(used)
//   core.storeAction(now, action) (a project action: kinds projectSave .. projectColor); core.storeAnswer(now, slot, bytes | null)
// (0.4.0) the firmware browser, the library, FX setup (move.h; the host guide's v0.4.0 section):
//   core.fwList([{ id, name, version, installed, desc, thumb (Uint8Array(512) | null), cats, cat, virt }]) -> actions
//   (0.5.11) core.fwSetLevel(slot, level) -> actions | null; core.setLabels(49 | null); core.setButtonNames(14 | null);
//   core.readout(control, kind, title, value, frac, flags, ms); core.readoutsClear(); core.popup(text)
//   core.fwInstalled(i, version | null); core.fwSlots([id | null, x3]) -> actions; core.fwProgress(id, pct) -> actions
//   core.fwLibrary(bytes | null) -> 0 / -1; core.fwLibrarySave() -> bytes; core.fwFlush() -> actions
//   core.fwCaps(0) (0.5.10: one firmware, no FX; before fwList); core.fwVoices(n); core.fwSounding() -> bits; core.fwActive(); core.fwLevel(slot); core.fwStates(slot, usedBits)
//   core.fxModules(kind, [{ name, params: [{ name, words?: [..], def, dryWet }] }]); core.fxRefused(kind, slot) -> actions
//   core.fxSync() -> actions
//   actions: { kind: "fwLoad", slot, fw } { kind: "fwUnload", slot } { kind: "fwActive", slot } { kind: "fwDownload", fw }
//   { kind: "fwLibrary", bytes } { kind: "fwLevel", slot, level } { kind: "fxLoad", fx, slot, module } (-1 empty)
//   { kind: "fxParam", fx, slot, param, value } { kind: "mix", slot, level, pan, sends: [3] }
//   { kind: "route", fw: [3 filters], midiFx: [3 filters] } (bit t: track t + 1) { kind: "stateSave" / "stateLoad" /
//   "stateDelete", slot, state }; fw is the index in fwList's array; "midi" has `track` (1..16 the sequencer's, 0 live)
export const ACT = { KEY: 1, PRESS: 2, BTN: 3, TURN: 4, MASTER: 5, MIDI: 6, SETTINGS: 7, PROJECT_SAVE: 8, PROJECT_LOAD: 9, PROJECT_DELETE: 10,
  CLICK: 11, AUDIO_REC: 12, PROJECT_CUT: 13, PROJECT_COPY: 14, PROJECT_PASTE: 15, PROJECT_COLOR: 16,
  FW_LOAD: 17, FW_UNLOAD: 18, FW_ACTIVE: 19, FW_DOWNLOAD: 20, FW_LIBRARY: 21, FW_LEVEL: 22, FX_LOAD: 23, FX_PARAM: 24,
  MIX: 25, ROUTE: 26, STATE_SAVE: 27, STATE_LOAD: 28, STATE_DELETE: 29 };
export const FX = { MIDI: 0, AUDIO: 1 };
export const LBL_N = 49, NBTN = 14;                     // (0.5.11: RFM_MOVE_LBL_N, RFM_MOVE_NBTN)
export const READOUT = { PARAM: 1, PRESET: 2, MESSAGE: 3, BIPOLAR: 1 };   // (RFM_MOVE_RD_*; BIPOLAR is a flag)
export const CAT = { SYNTH: 1, GROOVEBOX: 2, CHORDS: 4, DRUMS: 8, OTHER: 16 };
export const FW_FAILED = -1, FW_BUSY = -2;
export const CLOCK = { AUTO: 0, INT: 1, EXT: 2 };
export const PROJECTS = 32, PROJECT_MAX = 40136;   // (RFM_MOVE_PROJECT_MAX, version 6 blobs; mw_project_max())
export const FILE_ONE = 1, FILE_SET = 2, FILE_MAX = 8 + PROJECTS * (10 + PROJECT_MAX);   // (store.h RFM1CS_FILE_*)
export const TONES = ["off", "busy", "on", "bad"];   // (RFM1CS_TONE_*)
export const MODE = { FM1: 0, MIDI: 1, CONTROL: 2, SEQ: 3 };
export const MODE_NAMES = ["FM-1", "MIDI", "Control", "Sequencer"];
// the session (session.h)
export const STATE = { OFF: 0, CONNECTING: 1, CONNECTED: 2, RETRYING: 3, FAILED: 4 };
export const WHY = { CLOSED: 1, NO_ANSWER: 2, SILENT: 3, GARBLED: 4, NOT_MOVE: 5, SHUTDOWN: 6, PROTO: 7 };
export const CH = { STATE: 1, HELLO: 2, BATTERY: 4, PLAYING: 8, PROJECT: 16, CLOCK: 32, MODE: 64, WAITING: 128 };
export const STORE_PENDING = -2;
const EV_NAMES = { 1: "send", 2: "open", 3: "close", 4: "action", 5: "released", 6: "lost", 7: "bye", 8: "status", 100: "loaded" };
const STORE_KINDS = { projectSave: ACT.PROJECT_SAVE, projectLoad: ACT.PROJECT_LOAD, projectDelete: ACT.PROJECT_DELETE,
  projectCut: ACT.PROJECT_CUT, projectCopy: ACT.PROJECT_COPY, projectPaste: ACT.PROJECT_PASTE, projectColor: ACT.PROJECT_COLOR };
export const SCREEN_BYTES = 1024, SETTINGS_BYTES = 87, NLEDS = 92, POPUP_MS = 1000;   // (RFM_MOVE_POPUP_MS: a popup stays this long)
const ACT_BYTES = 10, ACT_EXT = 21, LED_BYTES = 6;
// pressure 0..127 as key_lean 0..100 (move.h rfm_move_lean)
export const lean = (p) => Math.floor((Math.max(0, Math.min(127, p | 0)) * 100 + 63) / 127);

let modP = null;
// the module (once): url the built rfm_move.mjs (default: RemoteFM1 web/build/move/, two levels above this file)
export function loadModule(url) {
  if (!modP) {
    const u = url || new URL("../../build/move/rfm_move.mjs", import.meta.url).href;
    modP = import(/* @vite-ignore */ u).then((m) => (m.default || m.createRfmMove)()).catch((e) => { modP = null; throw e; });
  }
  return modP;
}

export class MoveCore {
  static async load(settings, url) { return new MoveCore(await loadModule(url), settings); }
  constructor(M, settings) {
    this.M = M;
    this.w = 0;
    this.load(settings);
  }
  // a fresh core with these settings (an rfm_move_save image; anything else: the defaults)
  load(settings) {
    const M = this.M;
    if (this.w) { if (M.rfmHosts) delete M.rfmHosts[this.w]; M._mw_free(this.w); }
    let p = 0, n = 0;
    if (settings && settings.length) { n = settings.length; p = M._malloc(n); M.HEAPU8.set(settings, p); }
    this.w = M._mw_new(p, n);
    if (p) M._free(p);
    if (!this.w) throw new Error("rfm_move: out of memory");
    if (!this.px) this.px = M._malloc(240 * 240 * 2);
    if (!this.lv) this.lv = M._malloc(44);
    if (!this.km) this.km = M._malloc(66);
    if (!this.st) this.st = M._malloc(32);
    if (!this.st2) this.st2 = M._malloc(4);
    if (this.host) this.setStoreHost(this.host);
    return this;
  }
  _acts(n) {
    const out = [];
    for (let i = 0; i < n; i++) { const a = this._act(i); if (a) out.push(a); }
    return out;
  }
  // the i-th action of the last call, or null (a kind this wrapper does not name)
  _act(i) {
    const M = this.M, base = M._mw_acts(this.w), fr = M._mw_act_frames(this.w), b = M.HEAPU8;
    const o = base + i * ACT_BYTES, kind = b[o], index = b[o + 1], down = !!b[o + 2], vel = b[o + 3];
    const value = (b[o + 4] | (b[o + 5] << 8)) << 16 >> 16;
    let a = null;
    if (kind === ACT.KEY) a = { kind: "key", k: index, down, vel };
    else if (kind === ACT.PRESS) a = { kind: "press", k: index, value };
    else if (kind === ACT.BTN) a = { kind: "btn", m: index, down };
    else if (kind === ACT.TURN) a = { kind: "turn", enc: index, steps: value };
    else if (kind === ACT.MASTER) a = { kind: "master", value };
    else if (kind === ACT.MIDI) a = { kind: "midi", bytes: b.slice(o + 7, o + 7 + Math.min(3, b[o + 6])), track: b[M._mw_act_ext(this.w) + i * ACT_EXT] };
    else if (kind === ACT.SETTINGS) a = { kind: "settings" };
    else if (kind === ACT.PROJECT_SAVE) {
      const p = M._mw_blob(this.w, value), nb = M._mw_blob_len(this.w, value);
      if (p && nb) a = { kind: "projectSave", slot: index, bytes: M.HEAPU8.slice(p, p + nb) };
    } else if (kind === ACT.PROJECT_LOAD) a = { kind: "projectLoad", slot: index };
    else if (kind === ACT.PROJECT_DELETE) a = { kind: "projectDelete", slot: index };
    else if (kind === ACT.CLICK) a = { kind: "click", accent: !!value };          // the metronome
    else if (kind === ACT.AUDIO_REC) a = { kind: "audioRec", on: !!value, bars: index };   // Sample's loop
    else if (kind === ACT.PROJECT_CUT) a = { kind: "projectCut", slot: index };            // projects mode
    else if (kind === ACT.PROJECT_COPY) a = { kind: "projectCopy", slot: index };
    else if (kind === ACT.PROJECT_PASTE) a = { kind: "projectPaste", slot: index };
    else if (kind === ACT.PROJECT_COLOR) a = { kind: "projectColor", slot: index, color: value };
    else if (kind >= ACT.FW_LOAD && kind <= ACT.STATE_DELETE) {                     // (0.4.0) the browser, FX setup
      const x = M._mw_act_ext(this.w) + i * ACT_EXT, slot = b[x + 1], fx = b[x + 2], f16 = (k) => b[x + 9 + k * 2] | (b[x + 10 + k * 2] << 8);
      if (kind === ACT.FW_LOAD) a = { kind: "fwLoad", slot, fw: value };
      else if (kind === ACT.FW_UNLOAD) a = { kind: "fwUnload", slot };
      else if (kind === ACT.FW_ACTIVE) a = { kind: "fwActive", slot };
      else if (kind === ACT.FW_DOWNLOAD) a = { kind: "fwDownload", fw: value };
      else if (kind === ACT.FW_LIBRARY) {
        const p = M._mw_blob(this.w, value), nb = M._mw_blob_len(this.w, value);
        if (p && nb) a = { kind: "fwLibrary", bytes: M.HEAPU8.slice(p, p + nb) };
      } else if (kind === ACT.FW_LEVEL) a = { kind: "fwLevel", slot, level: value };
      else if (kind === ACT.FX_LOAD) a = { kind: "fxLoad", fx, slot, module: value };
      else if (kind === ACT.FX_PARAM) a = { kind: "fxParam", fx, slot, param: b[x + 3], value };
      else if (kind === ACT.MIX) a = { kind: "mix", slot, level: b[x + 4], pan: b[x + 5], sends: [b[x + 6], b[x + 7], b[x + 8]] };
      else if (kind === ACT.ROUTE) a = { kind: "route", fw: [f16(0), f16(1), f16(2)], midiFx: [f16(3), f16(4), f16(5)] };
      else a = { kind: ["stateSave", "stateLoad", "stateDelete"][kind - ACT.STATE_SAVE], slot, state: index };
    }
    if (a) a.frame = (b[fr + 4 * i] | (b[fr + 4 * i + 1] << 8) | (b[fr + 4 * i + 2] << 16) | (b[fr + 4 * i + 3] << 24)) >>> 0;
    return a;
  }
  // the session's events of the last call (copied out: the next call reuses the buffers)
  _evs(n, res = 0) {
    const M = this.M, base = M._mw_evs(this.w) >> 2, tx = M._mw_tx(this.w), evs = [];
    for (let i = 0; i < n; i++) {
      const t = M.HEAP32[base + i * 4], a = M.HEAP32[base + i * 4 + 1], b = M.HEAP32[base + i * 4 + 2], type = EV_NAMES[t];
      if (type === "send") evs.push({ type, bytes: M.HEAPU8.slice(tx + a, tx + a + b) });
      else if (type === "action") { const act = this._act(a); if (act) evs.push({ type, act }); }
      else if (type === "open") evs.push({ type, attempt: a >>> 0 });
      else if (type === "lost") evs.push({ type, why: a, wasLive: !!b });
      else if (type === "bye") evs.push({ type, reason: a, wasLive: !!b });
      else if (type === "status") evs.push({ type, changed: a });
      else if (type === "loaded") evs.push({ type, slot: a, ok: b === 0 });
      else if (type) evs.push({ type });
    }
    return { evs, res };
  }
  _withRes(fn) {
    const M = this.M;
    const n = fn(this.st2);
    return this._evs(n, M.HEAP32[this.st2 >> 2]);
  }
  // a blob into the module's memory for one call: fn(ptr, n)
  _withBytes(bytes, fn) {
    const M = this.M;
    if (!bytes || !bytes.length) return fn(0, 0);
    const p = M._malloc(bytes.length);
    try { M.HEAPU8.set(bytes, p); return fn(p, bytes.length); } finally { M._free(p); }
  }
  // ---- the sequencer
  seqAdvance(frames, rate) { return this._acts(this.M._mw_seq_advance(this.w, frames >>> 0, rate >>> 0)); }
  seqMidiIn(bytes) { return this._withBytes(bytes, (p, n) => this._acts(this.M._mw_seq_midi_in(this.w, p, n))); }
  seqPlay(on) { return this._acts(this.M._mw_seq_play(this.w, on ? 1 : 0)); }
  setBpm(bpm10) { this.M._mw_seq_set_bpm(this.w, bpm10 | 0); }
  setLead(us) { this.M._mw_seq_set_lead(this.w, Math.max(0, us | 0)); }   // (the clock's lead over what sounds)
  bpm10() { return this.M._mw_seq_bpm10(this.w); }
  setClock(mode) { this.M._mw_seq_set_clock(this.w, mode | 0); }
  following() { return !!this.M._mw_seq_following(this.w); }
  playing() { return !!this.M._mw_seq_playing(this.w); }
  screenTimed() { return !!this.M._mw_screen_timed(this.w); }   // the screen moves with the clock (a popup, playing)
  // ---- projects
  projectCheck(bytes) { return bytes && bytes.length && bytes.length <= PROJECT_MAX ? this._withBytes(bytes, (p, n) => this.M._mw_project_check(p, n)) : -1; }
  projectLoad(slot, bytes) {
    const r = this._withBytes(bytes, (p, n) => this.M._mw_project_load(this.w, slot | 0, p, n));
    return r < 0 ? null : this._acts(r);
  }
  projectDir(bits) { this.M._mw_project_dir(this.w, bits >>> 0); }
  // a stored project, for the grid's bars and the project popup: 0 ok, < 0 not a valid blob
  projectInfo(slot, bytes) { return bytes && bytes.length && bytes.length <= PROJECT_MAX ? this._withBytes(bytes, (p, n) => this.M._mw_project_info(this.w, slot | 0, p, n)) : -1; }
  projectFlush() { return this._acts(this.M._mw_project_flush(this.w)); }
  projectSlot() { return this.M._mw_project_slot(this.w); }
  projectSave() { const n = this.M._mw_project_save(this.w); if (n <= 0) return null; const p = this.M._mw_blob(this.w, 0); return this.M.HEAPU8.slice(p, p + n); }
  input(packet, frame = 0, nowMs = 0) { return this._acts(this.M._mw_input(this.w, packet >>> 0, frame >>> 0, nowMs >>> 0)); }
  releaseAll() { return this._acts(this.M._mw_release_all(this.w)); }
  setMode(mode) { return this._acts(this.M._mw_set_mode(this.w, mode | 0)); }
  setScale(scale, root) { return this._acts(this.M._mw_set_scale(this.w, scale | 0, root | 0)); }
  setMaster(v) { this.M._mw_set_master(this.w, v | 0); }
  get master() { return this.M._mw_master(this.w); }
  // the Move's battery from its STATUS (byte 4, byte 5 the power bits): true when it changed (redraw); -1 unknown
  setBattery(battery, power) { return !!this.M._mw_set_battery(this.w, battery | 0, power | 0); }
  get battery() { return this.M._mw_battery(this.w); }
  // Sample's loop: the recorded audio's window peaks (0..255 each) for the Sampling waveform; how many were taken
  sampleLevels(peaks) {
    const n = Math.min(peaks.length | 0, 256);
    if (!n) return 0;
    const M = this.M, p = M._malloc(n);
    M.HEAPU8.set(Array.from(peaks).slice(0, n).map((v) => Math.max(0, Math.min(255, v | 0))), p);
    const k = M._mw_sample_levels(this.w, p, n);
    M._free(p);
    return k;
  }
  // enc: matrix encoder 0..6, 7 MASTER; text "" or null: the default name
  setLabel(enc, text) {
    const M = this.M, s = String(text || "").slice(0, 16), n = M.lengthBytesUTF8(s) + 1, p = M._malloc(n);
    M.stringToUTF8(s, p, n);
    M._mw_set_label(this.w, enc | 0, p);
    M._free(p);
  }
  // (0.5.11) labels (0.5.6..0.5.9): the firmware's 49 live labels in DYNAMIC-LABELS order (null / undefined entry: none
  // given; "" no job), or null to clear; its 14 button names (FM1P_B_* order); readouts (kind READOUT.*, frac 0..1000
  // or -1, flags READOUT.BIPOLAR, ms 0 the default); a host popup ("FM-1 linked")
  _packLabels(list, n) {
    const M = this.M, E = M._mw_lbl_entry(), p = M._malloc(n * E);
    M.HEAPU8.fill(0, p, p + n * E);
    for (let i = 0; i < n; i++) {
      const v = list[i];
      if (v === null || v === undefined) continue;
      M.HEAPU8[p + i * E] = 1;
      const s = String(v).replace(/[^\x20-\x7e]/g, "").slice(0, 16);
      for (let k = 0; k < s.length; k++) M.HEAPU8[p + i * E + 1 + k] = s.charCodeAt(k);
    }
    return p;
  }
  setLabels(list) {
    if (!list) { this.M._mw_set_labels(this.w, 0); return; }
    const p = this._packLabels(list, LBL_N);
    try { this.M._mw_set_labels(this.w, p); } finally { this.M._free(p); }
  }
  setButtonNames(list) {
    if (!list) { this.M._mw_set_button_names(this.w, 0); return; }
    const p = this._packLabels(list, NBTN);
    try { this.M._mw_set_button_names(this.w, p); } finally { this.M._free(p); }
  }
  readout(control, kind, title, value, frac = -1, flags = 0, ms = 0) {
    this._withStrs([title || "", value || ""], (t, v) => this.M._mw_readout(this.w, control | 0, kind | 0, t, v, frac | 0, flags | 0, ms | 0));
  }
  readoutsClear() { this.M._mw_readouts_clear(this.w); }
  popup(text) { this._withStrs([text || ""], (t) => this.M._mw_popup(this.w, t)); }
  // km: the panel's wiring (6 rows x 11, id or -1: engine info().keymap), or null for the FM-1's
  setKeymap(km) {
    if (!Array.isArray(km) || km.length !== 6) { this.M._mw_set_keymap(this.w, 0); return; }
    const b = new Int8Array(66);
    for (let r = 0; r < 6; r++) for (let c = 0; c < 11; c++) { const v = km[r] && km[r][c]; b[r * 11 + c] = Number.isInteger(v) ? v : -1; }
    this.M.HEAPU8.set(new Uint8Array(b.buffer), this.km);
    this.M._mw_set_keymap(this.w, this.km);
  }
  panel(levels, playGreen = 0) {
    const b = new Uint8Array(44);
    if (levels) b.set(levels.subarray ? levels.subarray(0, 44) : levels.slice(0, 44));
    this.M.HEAPU8.set(b, this.lv);
    this.M._mw_panel(this.w, this.lv, playGreen | 0);
  }
  leds() {
    const M = this.M, n = M._mw_leds(this.w), base = M._mw_leds_buf(this.w), out = [];
    for (let i = 0; i < n; i++) {
      const o = base + i * LED_BYTES, b = M.HEAPU8;
      out.push({ rgb: b[o], cc: b[o + 1], number: b[o + 2], r: b[o + 3], g: b[o + 4], b: b[o + 5] });
    }
    return out;
  }
  ledsInvalidate() { this.M._mw_leds_invalidate(this.w); }
  screen(rgb565) {
    const M = this.M;
    let p = 0;
    if (rgb565 && rgb565.length >= 240 * 240) { new Uint16Array(M.HEAPU8.buffer, this.px, 240 * 240).set(rgb565.subarray(0, 240 * 240)); p = this.px; }
    const o = M._mw_screen(this.w, p);
    return M.HEAPU8.slice(o, o + SCREEN_BYTES);
  }
  save() { const o = this.M._mw_save(this.w); return this.M.HEAPU8.slice(o, o + SETTINGS_BYTES); }
  state() {
    this.M._mw_state(this.w, this.st);
    const s = new Int32Array(this.M.HEAPU8.buffer, this.st, 8);
    return { mode: s[0], layout: s[1], channel: s[2], octave: s[3], scale: s[4], root: s[5], bank: s[6], trackHeld: s[7] };
  }
  padNote(pad) { return this.M._mw_pad_note(this.w, pad | 0); }
  // the FM-1 mode's mapping (tests, help)
  padKey(pad) { return this.M._mw_pad_key(pad | 0); }
  stepKey(step) { return this.M._mw_step_key(step | 0); }
  buttonOf(cc) { return this.M._mw_button_of(cc | 0); }
  knobEnc(knob) { return this.M._mw_knob_enc(knob | 0); }
  decode(packet) {
    const p = this.st; this.M._mw_decode(packet >>> 0, p);
    const s = new Int32Array(this.M.HEAPU8.buffer, p, 3);
    return { type: s[0], index: s[1], value: s[2] };
  }
  // ---- the session (session.h) and the store (store.h)
  sessStart(now) { return this._evs(this.M._mw_sess_start(this.w, now >>> 0)); }
  sessStop(now) { return this._evs(this.M._mw_sess_stop(this.w, now >>> 0)); }
  sessWait(now, ms, patient) { this.M._mw_sess_wait(this.w, now >>> 0, ms >>> 0, patient ? 1 : 0); }
  sessGiveUp(tries) { this.M._mw_sess_give_up(this.w, tries | 0); }
  sessHostName(name) {
    const M = this.M, s = String(name || "").slice(0, 31), n = M.lengthBytesUTF8(s) + 1, p = M._malloc(n);
    M.stringToUTF8(s, p, n); M._mw_sess_host_name(this.w, p); M._free(p);
  }
  sessOpened(now, attempt) { return this._evs(this.M._mw_sess_opened(this.w, now >>> 0, attempt >>> 0)); }
  sessRx(now, attempt, bytes) { return this._withBytes(bytes, (p, n) => this._evs(this.M._mw_sess_rx(this.w, now >>> 0, attempt >>> 0, p, n))); }
  sessClosed(now, attempt, final) { return this._evs(this.M._mw_sess_closed(this.w, now >>> 0, attempt >>> 0, final ? 1 : 0)); }
  sessAttempt() { return this.M._mw_sess_attempt(this.w) >>> 0; }
  sessTick(now) { return this._evs(this.M._mw_sess_tick(this.w, now >>> 0)); }
  sessNextMs(now) { return this.M._mw_sess_next_ms(this.w, now >>> 0); }
  sessPanel(now, levels, playGreen = 0) {
    const b = new Uint8Array(44);
    if (levels) b.set(levels.subarray ? levels.subarray(0, 44) : levels.slice(0, 44));
    this.M.HEAPU8.set(b, this.lv);
    return this._evs(this.M._mw_sess_panel(this.w, now >>> 0, this.lv, playGreen | 0));
  }
  sessFrame(now, rgb565) {
    const M = this.M;
    let p = 0;
    if (rgb565 && rgb565.length >= 240 * 240) { new Uint16Array(M.HEAPU8.buffer, this.px, 240 * 240).set(rgb565.subarray(0, 240 * 240)); p = this.px; }
    return this._evs(M._mw_sess_frame(this.w, now >>> 0, p));
  }
  sessRedraw(now) { return this._evs(this.M._mw_sess_redraw(this.w, now >>> 0)); }
  sessFlushLeds(now) { return this._evs(this.M._mw_sess_flush_leds(this.w, now >>> 0)); }
  sessPumped(now) { return this._evs(this.M._mw_sess_pumped(this.w, now >>> 0)); }
  sessUpdate(now) { return this._evs(this.M._mw_sess_update(this.w, now >>> 0)); }
  sessFlush(now) { return this._evs(this.M._mw_sess_flush(this.w, now >>> 0)); }
  sessRelease(now) { return this._evs(this.M._mw_sess_release(this.w, now >>> 0)); }
  sessProjectsInit(now, bits, slot, bytes) {
    return this._withBytes(bytes, (p, n) => this._withRes((r) => this.M._mw_sess_projects_init(this.w, now >>> 0, bits >>> 0, slot | 0, p, n, r)));
  }
  sessProjectAnswer(now, slot, bytes) {
    return this._withBytes(bytes, (p, n) => this._withRes((r) => this.M._mw_sess_project_answer(this.w, now >>> 0, slot | 0, p, n, r)));
  }
  sessStatus() {
    const s = new Int32Array(this.M.HEAPU8.buffer, this.M._mw_sess_status(this.w), 32);
    return { state: s[0], everLive: !!s[1], waiting: !!s[2], retries: s[3], retryMs: s[4], via: s[5], proto: s[6], caps: s[7],
      battery: s[8], power: s[9], jacks: s[10], ledPending: s[11], mode: s[12], project: s[13], playing: !!s[14],
      following: !!s[15], bpm10: s[16], connects: s[17], inputs: s[18], ledWrites: s[19], ledMsgs: s[20], screens: s[21],
      pings: s[22], pongs: s[23], sent: s[24], midi: s[25], live: !!s[26], txOver: s[27], lostWhy: s[28], bye: s[29],
      goneLive: !!s[30], attempt: s[31] >>> 0 };
  }
  // ---- the words to show (session.h): words { host, closed, link, self } (each optional)
  _str(p) { const M = this.M; let e = p; while (M.HEAPU8[e]) e++; return new TextDecoder().decode(M.HEAPU8.slice(p, e)); }
  _text(what, a, b, words = {}) {
    const M = this.M, ptrs = [words.host, words.closed, words.link, words.self].map((v) => {
      if (v === undefined || v === null) return 0;
      const s = String(v), n = M.lengthBytesUTF8(s) + 1, p = M._malloc(n);
      M.stringToUTF8(s, p, n);
      return p;
    });
    try {
      const text = this._str(M._mw_sess_text(this.w, what, a | 0, b | 0, ...ptrs));
      return { tone: TONES[M._mw_text_tone(this.w)] || "off", text };
    } finally { for (const p of ptrs) if (p) M._free(p); }
  }
  sessText(words) { return this._text(0, 0, 0, words); }                   // the status line now
  whyText(why, words) { return this._text(1, why, 0, words).text; }        // "the Move went quiet" ..
  lostText(why, wasLive, words) { return this._text(2, why, wasLive ? 1 : 0, words).text; }   // the LOST toast
  byeText(reason, wasLive) { return this._text(3, reason, wasLive ? 1 : 0); }                 // { tone, text }
  // ---- the project file (store.h): [{ slot, saved (Unix seconds), bytes }] -> its bytes
  filePack(entries, kind = 0) {
    const M = this.M, n = entries.length, total = entries.reduce((a, e) => a + e.bytes.length, 0);
    const size = 8 + entries.reduce((a, e) => a + 10 + e.bytes.length, 0);
    const meta = M._malloc(Math.max(4, n * 12)), blobs = M._malloc(Math.max(1, total)), out = M._malloc(size);
    try {
      let o = 0;
      entries.forEach((e, i) => {
        M.HEAP32[(meta >> 2) + i * 3] = e.slot | 0; M.HEAP32[(meta >> 2) + i * 3 + 1] = e.saved >>> 0; M.HEAP32[(meta >> 2) + i * 3 + 2] = e.bytes.length;
        M.HEAPU8.set(e.bytes, blobs + o); o += e.bytes.length;
      });
      const len = M._mw_file_pack(meta, blobs, n, kind | 0, out, size);
      if (len !== size) throw new Error("rfm_move: the project file could not be packed");
      return M.HEAPU8.slice(out, out + len);
    } finally { M._free(meta); M._free(blobs); M._free(out); }
  }
  // a file's bytes -> { kind, entries: [{ slot, saved (Unix seconds, 0 unknown), bytes }] } (every blob checked), or
  // { error } in the web player's words (self: "page" by default, or "app")
  fileUnpack(bytes, self) {
    const M = this.M, n = bytes ? bytes.length : 0, p = bytes ? M._malloc(Math.max(1, n)) : 0;   // (empty: not "nothing")
    try {
      if (n) M.HEAPU8.set(bytes, p);
      const r = M._mw_file_unpack(p, n);
      if (r < 0) {
        const s = String(self || "page"), k = M.lengthBytesUTF8(s) + 1, sp = M._malloc(k);
        M.stringToUTF8(s, sp, k);
        try { return { error: this._str(M._mw_file_error(sp)) }; } finally { M._free(sp); }
      }
      const res = M._mw_file_res() >> 2, entries = [];
      for (let i = 0; i < r; i++) {
        const o = res + 1 + i * 4, off = M.HEAP32[o + 2], len = M.HEAP32[o + 3];
        entries.push({ slot: M.HEAP32[o], saved: M.HEAP32[o + 1] >>> 0, bytes: M.HEAPU8.slice(p + off, p + off + len) });
      }
      return { kind: M.HEAP32[res], entries };
    } finally { if (p) M._free(p); }
  }
  sessName() { const M = this.M, p = M._mw_sess_name(this.w); let e = p; while (M.HEAPU8[e]) e++; return new TextDecoder().decode(M.HEAPU8.slice(p, e)); }
  sessShown() { const p = this.M._mw_sess_shown(this.w); return p ? this.M.HEAPU8.slice(p, p + SCREEN_BYTES) : null; }
  setStoreHost(host) {
    const M = this.M;
    this.host = host;
    M.rfmHosts = M.rfmHosts || {};
    if (this.w) M.rfmHosts[this.w] = host;
  }
  storeInit(used) { this.M._mw_store_init(this.w, used >>> 0); }
  storeAction(now, a) {
    const kind = STORE_KINDS[a.kind];
    if (!kind) return { evs: [], res: 0 };
    return this._withBytes(a.kind === "projectSave" ? a.bytes : null,
      (p, n) => this._evs(this.M._mw_store_request(this.w, now >>> 0, kind, a.slot | 0, a.color | 0, p, n)));
  }
  storeAnswer(now, slot, bytes) { return this._withBytes(bytes, (p, n) => this._evs(this.M._mw_store_answer(this.w, now >>> 0, slot | 0, p, n))); }
  // ---- (0.4.0) the firmware browser, the library, FX setup
  _cstr(v) {
    if (v === undefined || v === null) return 0;
    const M = this.M, t = String(v), n = M.lengthBytesUTF8(t) + 1, p = M._malloc(n);
    M.stringToUTF8(t, p, n);
    return p;
  }
  _withStrs(list, fn) { const ps = list.map((v) => this._cstr(v)); try { return fn(...ps); } finally { for (const p of ps) if (p) this.M._free(p); } }
  fwList(entries) {
    const M = this.M, list = entries || [];
    if (M._mw_fw_begin(this.w, list.length) < 0) throw new Error("rfm_move: out of memory");
    list.forEach((e, i) => {
      const t = e.thumb && e.thumb.length >= 512 ? M._malloc(512) : 0;
      if (t) M.HEAPU8.set(e.thumb.subarray ? e.thumb.subarray(0, 512) : e.thumb.slice(0, 512), t);
      try { this._withStrs([e.id, e.name, e.version, e.installed || null, e.desc || null], (a, b, c, d, f) => M._mw_fw_entry(i, a, b, c, d, f, t, e.cats | 0, e.cat === undefined ? 255 : e.cat | 0, e.virt ? 1 : 0)); }
      finally { if (t) M._free(t); }
    });
    return this._acts(M._mw_fw_commit(this.w));
  }
  fwInstalled(i, version) { this._withStrs([version || null], (v) => this.M._mw_fw_installed(this.w, i | 0, v)); }
  fwSlots(ids) { const s = ids || []; return this._acts(this._withStrs([s[0] || null, s[1] || null, s[2] || null], (a, b, c) => this.M._mw_fw_slots(this.w, a, b, c))); }
  fwProgress(id, pct) { return this._acts(this._withStrs([id], (p) => this.M._mw_fw_progress(this.w, p, pct | 0))); }
  fwLibrary(bytes) { return this._withBytes(bytes, (p, n) => this.M._mw_fw_library(this.w, p, n)); }
  fwLibrarySave() { const n = this.M._mw_fw_library_save(this.w), p = this.M._mw_blob(this.w, 0); return n ? this.M.HEAPU8.slice(p, p + n) : null; }
  fwFlush() { return this._acts(this.M._mw_fw_flush(this.w)); }
  fwVoices(n) { this.M._mw_fw_voices(this.w, n | 0); }
  // (0.5.10) the host's capabilities: 0 one firmware, no FX (the web page); 3 (the default) three slots and FX
  fwCaps(caps) { this.M._mw_fw_caps(this.w, caps | 0); }
  // (0.5.11) a slot's level 0..127 from the page: the actions (fwLevel, fwLibrary later), or null refused
  fwSetLevel(slot, level) { const r = this.M._mw_fw_set_level(this.w, slot | 0, level | 0); return r < 0 ? null : this._acts(r); }
  fwCapsGet() { return this.M._mw_fw_caps_get(this.w); }
  fwSounding() { return this.M._mw_fw_sounding(this.w); }
  fwActive() { return this.M._mw_fw_active(this.w); }
  fwLevel(slot) { return this.M._mw_fw_level(this.w, slot | 0); }
  fwStates(slot, used) { this.M._mw_fw_states(this.w, slot | 0, used >>> 0); }
  fxModules(kind, mods) {
    const M = this.M, list = mods || [];
    if (M._mw_fx_begin(kind | 0, list.length) < 0) throw new Error("rfm_move: out of memory");
    list.forEach((md, i) => {
      const ps = md.params || [];
      this._withStrs([md.name], (n) => M._mw_fx_module(kind | 0, i, n, ps.length));
      ps.forEach((pa, k) => {
        const words = pa.words || [];
        this._withStrs([pa.name], (n) => M._mw_fx_param(kind | 0, i, k, n, words.length, pa.def | 0, pa.dryWet ? 1 : 0));
        words.forEach((w, j) => this._withStrs([w], (p) => M._mw_fx_word(kind | 0, i, k, j, p)));
      });
    });
    M._mw_fx_commit(this.w, kind | 0);
  }
  fxRefused(kind, slot) { return this._acts(this.M._mw_fx_refused(this.w, kind | 0, slot | 0)); }
  fxSync() { return this._acts(this.M._mw_fx_sync(this.w)); }
  // the library's version ("0.1.0": RFM1CS_VERSION)
  version() { const M = this.M, p = M._mw_version(); let e = p; while (M.HEAPU8[e]) e++; return String.fromCharCode(...M.HEAPU8.subarray(p, e)); }
  free() { const M = this.M; if (this.w) M._mw_free(this.w); if (this.w && M.rfmHosts) delete M.rfmHosts[this.w]; for (const k of ["px", "lv", "km", "st", "st2"]) if (this[k]) { M._free(this[k]); this[k] = 0; } this.w = 0; }
}

// a USB-MIDI packet (the surface's, cable 0) as INPUT carries it, as a LE uint32: bytes cin|status|d1|d2
export function packet(status, d1, d2, cable = 0) {
  const cin = (status >> 4) & 15;
  return ((cable << 4 | cin) | (status & 255) << 8 | (d1 & 127) << 16 | (d2 & 127) << 24) >>> 0;
}
