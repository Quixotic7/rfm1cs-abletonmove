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

## Layers

From the top (what takes an input first) down. A layer lists what it takes; everything else falls to the next one.

1. **Global settings** (Shift + step 2): the whole surface.
2. **A firmware's takeover**: what the firmware claims.
3. **Back**, **the Track buttons**: always their own (a click, a double click, a hold's menu).
4. **A held Track's menu**: pads, steps, knobs, jog, Up / Down. In session view: pads and steps only (the routing).
5. **A Shift menu** (Workflow, Tempo, Groove, Layout, Keys, Velocity, Arp, Record): knobs, jog, jog click, the
   screen; the arp's lanes also the step keys. Back closes it.
6. **The Shift layer** (Shift held or latched): the step keys (shortcuts, icons lit).
7. **A held modifier** (Loop, Copy, Delete, Mute): the step keys, some pads.
8. **The view**: the projects view (browser, Mixer, Routing, project grid), or session view, or the track view.
9. **The layout**: the FM-1 layout or a note layout.

The lights and the screen follow the same order: the layer that takes a control lights it and names it.
