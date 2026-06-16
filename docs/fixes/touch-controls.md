# Touch controls

*Command & Conquer: Generals* is a mouse+keyboard RTS. This port implements a full multi‑touch
control layer so it's playable on a phone. It lives in the Android input path of
`SDL3GameEngine.cpp` (all under `#if defined(__ANDROID__)`).

## Gesture map

| Gesture | Synthesized action |
|---|---|
| 1‑finger tap | left click (select / press UI button) |
| 1‑finger drag | left drag (drag‑box select / move a slider) |
| 2‑finger drag | camera pan (`View::userScrollBy`) |
| 2‑finger pinch | camera zoom (`View::userZoom`) |
| 2‑finger tap | right click (move / attack order) |
| tap **☰** (top‑left, in‑game) | open pause/ESC menu (`ToggleQuitMenu`) |
| tap **▶▶** (top‑right, in a video) | skip cutscene (synthesizes `KEY_ESC`) |

## Design details

- **SDL's built‑in touch→mouse synthesis is disabled** (`SDL_HINT_TOUCH_MOUSE_EVENTS=0`,
  `SDL_HINT_MOUSE_TOUCH_EVENTS=0`) so the port can distinguish one finger from two. This hint
  **must be set before `SDL_InitSubSystem`** (in `SDL3Main.cpp`) — setting it after init does not
  reliably take effect.
- **1‑vs‑2 finger disambiguation.** The first finger's left‑button‑down is deferred a few ms so a
  second finger can arrive first; that way a 2‑finger gesture never emits a spurious left click
  (which would deselect units right before a move order).
- **Synthesized clicks need a minimum press + motion.** A raw down‑then‑up synthesized click did
  *not* register on latching GUI buttons: the WindowManager needs the press **sustained by motion
  events across frames**, like a real held finger, plus a minimum press duration (~150 ms). Both
  are required — implemented with a time‑based deferred‑up queue that also emits a keep‑alive
  motion every frame while a press is pending.
- **On‑screen buttons.** The ☰ (menu) and ▶▶ (skip) buttons are drawn by the engine
  (`W3DInGameUI.cpp` for the in‑game menu button; the skip button is drawn wherever the video
  frame is presented) and hit‑tested in the touch layer. The menu button is only shown while
  actually playing; the skip button only while a video/cutscene is on screen.

## Sensitivity slider

Camera pan/zoom speed is user‑adjustable and **persisted**. Rather than add a new setting, the
port repurposes the existing **Options → Scroll Speed** slider (which already saves to the
options file and reloads at boot) to scale the 2‑finger pan *and* pinch‑zoom. The mapping is
tuned so the default feels natural and the slider max is genuinely fast.
