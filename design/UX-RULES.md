# UX rules for the Move interface

The owner's rules (2026-11-21). Every layer, menu and gesture in this core follows them; a change that breaks one is
a bug. The audit of the code against them, and the layer diagram, are on the review page
(https://claude.ai/artifact/BFFMT5pfrVar1iwSbSqnP4).

The measure the owner uses: **UX = (I x F x C) / (L + N)**: intent on the surface, feedback, continuity, over
learning and noise. **F, feedback, comes first here.**

## R1. Feedback at once

Every input gives an immediate change you can see: a light, the screen, or both.

- A press shows as a press (a pad or step turns white, a button's light goes full) even when what it does comes later.
- A message never waits for the release of the control that caused it.
- A control with nothing to do in a layer is dark, so a dead press is no surprise.

## R2. Click, hold and double click are gated

A control with more than one of these has each decided before anything shows:

- **A hold** acts when its time has passed (`RFM_MOVE_SEQ_HOLD_MS`, 300 ms), while the control is still down. Never at
  the release. Its release then does nothing.
- **A click** acts only once it cannot be anything else: at the release when the control has no double click, else
  when the double click's time (`RFM_MOVE_DOUBLE_MS`, 300 ms) has passed with no second press. Until then the control
  shows it was pressed (white), nothing more.
- **A double click** acts at its second press.
- No message or function of one ever shows for another.

One exception, stated so it is a decision and not an accident: when the double click is **the same thing sooner** (a
clip or a scene: click = at the bar, double click = now; Shift + a scene: stop at the bar / now), the click acts at
once. Its effect is what the double click would do anyway, so nothing wrong shows, and a launch is never 300 ms late.

## R3. A layer takes what it uses

- A layer that draws a menu on the screen (not a plain toggle) takes **the jog wheel**; **Back** closes it and
  returns to the layer under it. Menu does not close it: the view flips under the menu.
- A layer that uses some knobs takes **all eight**: the ones it has no job for are dark and dead, they do not reach
  the layer under it.
- A layer that uses some pads takes all the pads; a layer that uses some step keys takes all the step keys. The ones
  it has no job for are dark and dead.
- What a layer does not touch at all stays with the layer under it (a menu on the knobs and the jog leaves the pads
  playing).
- A held control (a step, Loop, Copy, Delete, Mute, Shift) takes over an open menu for as long as it is held, with
  its screen and the controls it uses; the menu is back at its release. A tap that shows a popup (a Track button)
  shows it over the menu and returns.
- Menu never closes a menu: in a note layout it flips the view under it, in the FM-1 layout it is the firmware's key.

## R4. A knob in use is lit

Whenever something uses a knob, the light under it is on. A knob with no job is dark.

## R5. A knob says what it does

Touching a knob in use shows its name; turning it shows its value.

- By default a full-screen popup: the name, the value.
- Where a feature has its own picture, the picture must make plain which knob does what and show the value change:
  a header naming the touched knob and its value (the arp menu), with a mark for the knob's place.

## R6. The icon under a step key

- The step is **a function** here (a toggle, a shortcut, it opens a menu): its icon is lit.
- The step is **a step, a track, a page or a length**: its icon is dark.
- A function goes on the step whose printed icon means it (the Shift layer follows the Move's own icons); a function
  with no matching icon does not go on a Shift step.
- At rest (no Shift, no Track held) no icon is lit in session view or track view.
- The browser's filter steps light their icon only for a filter that is on.

## Layers

From the top (what takes an input first) down. A layer lists what it takes; everything else falls to the next one.

1. **Global settings** (Shift + step 2): the whole surface.
2. **A firmware's takeover**: what the firmware claims.
3. **Back**, **the Track buttons**: always their own (a click, a double click, a hold's menu). Over States (0.13.0)
   Track 1..3's tap shows that slot's States, Track 4's tap closes States and does its own job.
4. **States** (0.13.0, a host with `RFM_MOVE_CAP_PROJFW`; Track 1..3 held + the top row's pad 7, or the browser's
   Loop): the pads (the 32 states), the step keys (step 14 New; the rest dark and dead), the knobs and the jog (dark,
   dead), Copy, Delete, Undo (lit only while New's undo is there), Loop when opened by the browser, the screen and
   the Track lights; Shift, Capture, the arrows, Mute, Sample and Record dark and dead. Play and the volume knob stay
   the layer's under it; Menu closes States and does its own job. **Back's click** clears the copy / delete marks
   first, then closes States: what it was opened over shows again (the layout, the view, a takeover; the browser when
   opened from there). A firmware's takeover is not shown while States are open.
5. **A held Track's menu**: pads, steps, knobs, jog, Up / Down. In session view: pads and steps only (the routing).
6. **A held Track 4** (0.12.0): the whole layer once held 300 ms (or something pressed meanwhile): the step keys (the
   Shift shortcuts), pads 1 and 2, Record, Play, Capture, Undo, Loop, Sample; everything else dark and dead. A menu
   it opened owns the knobs and the jog.
7. **The Shift layer** (Shift held or latched): the step keys (shortcuts, icons lit).
8. **A held modifier** (Loop, Copy, Delete, Mute; Loop and Mute also latched by a quick click): the step keys, some
   pads, the screen; Loop the jog.
9. **A held step** (track view: its popups, the knobs, the jog, the volume knob, the arrows; an arp lane step: the
   pads; a session select step: the clip's settings).
10. **A Shift menu** (Workflow, Tempo, Groove, Layout, Keys, Velocity, Arp, Record): knobs, jog, jog click, the
   screen; the arp's lanes also the step keys. Back closes it.
11. **The view**: the projects view (browser, Mixer, Routing, project grid), or session view, or the track view.
12. **The layout**: the FM-1 layout or a note layout.

(0.13.0) A project's firmwares loading: the loading screen is over every view until each slot has settled; it takes
no control (the project grid stays usable under it).

The lights and the screen follow the same order: the layer that takes a control lights it and names it.
