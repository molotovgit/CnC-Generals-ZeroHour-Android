# Touch → RTS control scheme: synthesizing precise mouse/keyboard input from multi-touch

> *Command & Conquer: Generals* was built for a mouse and two keyboards' worth of modifiers. This port never teaches the engine about touch. Instead a thin Android-only layer in `SDL3GameEngine.cpp` reads raw finger events, classifies them into RTS intents — select, drag-box, move/attack, pan, zoom — and **synthesizes the exact mouse and keyboard events the desktop game already handles**, feeding them back through `SDL3Mouse`/`SDL3Keyboard` as if a real device produced them. The whole layer is under `#if defined(__ANDROID__)`; desktop compiles none of it. The hard parts are not the gestures — they are disambiguating one finger from two *before* committing a click, and making a synthesized click survive the GUI's latching widgets.

This is the deeper companion to [`../fixes/touch-controls.md`](../fixes/touch-controls.md). That page is the summary; this one traces every mechanism to the code and explains the non-obvious lessons — the deferred left-down, the minimum-press keep-alive, the draw/hit-test geometry contract, and why the camera reuses a persisted slider.

## Gesture map

| Gesture | Classified as | Synthesized input | Engine entry point |
|---|---|---|---|
| 1-finger tap | select / press a UI button | left button down+up (min-press guaranteed) | `synthButton(SDL_BUTTON_LEFT, …)` |
| 1-finger drag | drag-box select / move a slider | left down at anchor, then motion each frame | `synthLeftDown` → `synthMotion` |
| 2-finger drag | camera pan (grab-world) | `View::userScrollBy(&off)` | `handleFingerMotion` |
| 2-finger pinch | camera zoom | `View::userZoom(zoomH)` | `handleFingerMotion` |
| 2-finger tap | move / attack order | right button down+up | `rightClick` → `synthButton(SDL_BUTTON_RIGHT, …)` |
| tap **☰** (top-left, in a live game) | open the pause/ESC menu | `ToggleQuitMenu()` | `handleFingerUp` |
| tap **▶▶** (top-right, during a video) | skip the cutscene | `KEY_ESC` down+up | `synthKey(SDL_SCANCODE_ESCAPE, …)` |

The two on-screen buttons exist because a phone has no `ESC` key and no persistent menu bar; everything else maps a finger count and a motion profile onto a mouse button. The design goal running through all of it: **a 2-finger gesture must never emit a stray left click**, because a spurious left click deselects the player's army the instant before they issue a move order.

## Taking over touch from SDL (why, and the hint-ordering)

SDL will, by default, invent mouse events from touch (`SDL_HINT_TOUCH_MOUSE_EVENTS`) and touch events from mouse (`SDL_HINT_MOUSE_TOUCH_EVENTS`). For a mouse-driven title that sounds convenient — one finger becomes a left click for free — but it is fatal here: SDL's synthesis collapses *every* finger to the same left button, so there is no way left to tell a one-finger select from the first finger of a two-finger pan. The port must see raw fingers.

`SDL3GameEngine.cpp:init` disables both directions:

```cpp
// take full control of touch. Disable SDL's automatic single-finger→mouse synthesis so we can
// distinguish 1-finger (select) from 2-finger (pan / zoom / command) gestures ourselves.
SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
```

The hint-ordering caveat is documented in the companion fix: the reliable place to set `SDL_HINT_TOUCH_MOUSE_EVENTS` is **before `SDL_InitSubSystem`** (in `SDL3Main.cpp`) — set after the subsystem is up, it does not reliably take effect. `init` re-asserts both hints, but the *load-bearing* set happens at bootstrap; treat the `init` calls as belt-and-suspenders, not the primary switch. Once synthesis is off, finger events arrive raw and `SDL3GameEngine.cpp:pollSDL3Events` routes them:

```cpp
case SDL_EVENT_FINGER_DOWN:     handleFingerDown(event.tfinger);   break;
case SDL_EVENT_FINGER_MOTION:   handleFingerMotion(event.tfinger); break;
case SDL_EVENT_FINGER_UP:
case SDL_EVENT_FINGER_CANCELED: handleFingerUp(event.tfinger);     break;
```

`SDL_EVENT_FINGER_CANCELED` is deliberately funneled to the same handler as `SDL_EVENT_FINGER_UP` — when the OS steals a touch (a system edge-swipe, palm rejection, a notification shade pull) the finger will never send an `UP`, so treating a cancel as a lift is what keeps the state machine from wedging with a phantom finger held down. After the event loop drains, `pollSDL3Events` calls `touchTick()` once per pump — the per-frame commit point discussed below.

Finger coordinates from SDL are normalized `0..1`. `SDL3GameEngine.cpp:winSize` reads the live window size via `SDL_GetWindowSize` on every access and the handlers scale to pixels as `px = tf.x * w`, so the layer follows a resize or rotation without caching a stale resolution.

### How synthesized events reach the engine

The layer never calls the game's input handlers directly. It re-injects fabricated `SDL_Event`s into the same `SDL3Mouse`/`SDL3Keyboard` buffers a physical device would fill, so downstream code cannot tell the difference. `SDL3GameEngine.cpp:synthButton` is representative:

```cpp
SDL3Mouse *m = dynamic_cast<SDL3Mouse*>(TheMouse); if (!m) return;
SDL_Event e; memset(&e, 0, sizeof(e));
e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
e.button.button = button; e.button.down = down; e.button.clicks = 1;
e.button.x = x; e.button.y = y;
m->addSDLEvent(&e);
```

`synthMotion` fabricates `SDL_EVENT_MOUSE_MOTION` the same way, and `SDL3GameEngine.cpp:synthKey` fabricates `SDL_EVENT_KEY_DOWN`/`SDL_EVENT_KEY_UP` into `SDL3Keyboard::addSDLEvent` — used only by the Skip button, because the movie-abort loops poll `TheKeyboard` for an `ESC` down. Every primitive `dynamic_cast`s `TheMouse`/`TheKeyboard` to the SDL3 concrete type and bails if the cast fails, so the layer is inert if some other device backend is ever installed. Injecting at the event-buffer level rather than at the handler level is what keeps the desktop code path untouched: the click flows through the identical decode, hit-test and dispatch the real mouse uses.

## 1-vs-2 finger disambiguation (the deferred left-down)

The central trick: **the first finger's left-button-down is not emitted when the finger touches down.** It is recorded as *pending* and committed later, once we are confident no second finger is coming. If a second finger arrives inside the window, the gesture is reclassified as two-finger and the left click never happens.

State lives in a small block of file-scope globals in `SDL3GameEngine.cpp`:

```cpp
bool         g_leftPending = false;   // 1st finger down; left-down not yet committed
bool         g_leftHeld    = false;   // a left-button-down was synthesized and is held
const Uint64 TAP_DEFER_MS   = 45;     // defer left-down this long to catch a 2nd finger
const float  MOVE_THRESH_PX = 14.0f;  // finger travel that turns a tap into a drag
```

`handleFingerDown` for the first finger (when it does not land on an on-screen button) sets `g_leftPending = true`, stores the finger id and start point, and stamps `g_leftDownMs = SDL_GetTicks()`. No mouse event is synthesized yet. There are then three ways the pending state resolves:

1. **The defer window elapses with the finger still down and alone.** `SDL3GameEngine.cpp:touchTick` runs every pump and commits the down once `SDL_GetTicks() - g_leftDownMs >= TAP_DEFER_MS` (45 ms), provided `g_tfCount == 1` and the suppression latch is clear:

   ```cpp
   if (g_leftPending && !g_leftHeld && g_tfCount == 1 && !g_suppressLeft) {
       if (SDL_GetTicks() - g_leftDownMs >= TAP_DEFER_MS) {
           synthLeftDown(g_leftSX, g_leftSY);
           g_leftPending = false;
       }
   }
   ```

2. **The finger travels more than the threshold before the window elapses.** `handleFingerMotion` promotes the pending press straight into a drag, anchoring the synthesized down at the *original* touch point (`g_leftSX, g_leftSY`) — never the current position — so a drag-box grows from where the finger first landed, then a motion is emitted to the current point:

   ```cpp
   if (g_leftPending && !g_leftHeld) {
       if (sqrtf(tdx*tdx + tdy*tdy) > MOVE_THRESH_PX) {
           synthLeftDown(g_leftSX, g_leftSY);   // anchor the drag at the touch point
           g_leftPending = false;
           synthMotion(px, py);
       }
   }
   ```

3. **A second finger lands first.** `handleFingerDown` at `g_tfCount == 2` cancels any armed button and calls `beginTwoFinger()`, which clears `g_leftPending` (and releases a held left, if the finger had already crossed into a drag). No click is emitted, and the two-finger gesture owns the input from here.

Why 45 ms and not a round 100 ms? Because the defer directly delays every real click, and the tail interacts with the minimum-press logic (next section). Empirically a genuine finger tap is only ~100 ms of contact; a defer long enough to reliably catch a second finger, but short enough that it does not eat the whole tap, sits at 45 ms. The number is a deliberate tension point, not a placeholder.

### The finger-tracking array

The layer tracks at most two fingers in a tiny fixed array — there is no dynamic allocation on the input path:

```cpp
struct TouchFinger { SDL_FingerID id; float sx, sy, x, y; };
TouchFinger  g_tf[2];
int          g_tfCount = 0;
```

Each entry keeps both the start point (`sx, sy`) and the current point (`x, y`); the start point is what anchors a drag-box and what the two-finger centroid math needs. `handleFingerDown` only records a finger while `g_tfCount < 2`, so a **third or later finger is never stored**. `SDL3GameEngine.cpp:findFinger` returns `-1` for an id it does not know, and both `handleFingerMotion` and `handleFingerUp` early-return on that — so a stray third finger's motion and lift are silently ignored rather than corrupting the gesture in progress. `SDL3GameEngine.cpp:dropFinger` compacts the array on lift so the surviving finger always occupies slot 0. Capping at two is not a limitation to apologize for — an RTS needs exactly select, command, pan and zoom, and every one of those is expressible with one or two fingers.

### The `g_suppressLeft` latch

A subtle failure mode: during a two-finger pan the player lifts one finger a fraction before the other. The moment `g_tfCount` drops to 1, the surviving finger looks exactly like a fresh single-finger press that has been moving — and without a guard it would immediately start a drag-box across the middle of the map. `beginTwoFinger` sets `g_suppressLeft = true`, and every single-finger code path in `handleFingerMotion` and `handleFingerUp` is gated on `!g_suppressLeft`. The latch is cleared only when the finger count returns to zero:

```cpp
if (g_tfCount == 0) { g_suppressLeft = false; g_leftPending = false; g_leftHeld = false; }
```

So once a gesture has "gone two-finger", **no left action can start again until the player fully lifts off** — a clean, unambiguous reset. The right-click branch, the menu-arm branch and the skip-arm branch all repeat this zero-count reset so the machine always returns to a known-idle state.

## Making a synthesized click actually register (min press + motion keep-alive)

A synthesized click is not a real one, and the game's GUI notices the difference. The naive implementation — emit `SDL_EVENT_MOUSE_BUTTON_DOWN` immediately followed by `SDL_EVENT_MOUSE_BUTTON_UP` — **did not fire latching shell buttons**. Two independent facts about the `WindowManager` update loop cause this, and both had to be addressed:

- **The press must last a minimum wall-clock duration.** A latching pushbutton arms on the down and expects the up on a *later* client update. If down and up land in the same update, the widget arms and disarms in one tick and the callback never runs — the button just sticks "selected". The `SDL3GameEngine.cpp` comment records the empirical figure: the shell menu needs about 150 ms of press, and the 45 ms commit-defer eats into a real tap's ~100 ms, so a minimum is guaranteed rather than hoped for.
- **The press must be *sustained by motion* across frames.** A held real finger produces a stream of motion events; without them the `WindowManager` never sees the press as continuously held and the pushbutton's up won't fire its callback. So a pending up also emits a keep-alive motion at the button position on every frame it is still waiting.

Both are implemented with a small deferred-up queue plus a per-frame drain. The constant and the queue:

```cpp
struct PendingUp { Uint8 button; float x, y; Uint64 dueMs; bool active; };
PendingUp    g_ups[8] = {};
const Uint64 CLICK_HOLD_MS = 150;      // minimum synthesized press duration
```

`SDL3GameEngine.cpp:synthLeftDown` emits a motion, then the button-down, marks `g_leftHeld`, and stamps `g_leftDownSynthMs`. `SDL3GameEngine.cpp:releaseLeftHeld` computes the earliest legal up as `g_leftDownSynthMs + CLICK_HOLD_MS`; if that time has already passed (a long press or a drag) it releases immediately, otherwise it queues the up:

```cpp
void releaseLeftHeld(float x, float y) {
    Uint64 due = g_leftDownSynthMs + CLICK_HOLD_MS;
    if (SDL_GetTicks() >= due) synthButton(SDL_BUTTON_LEFT, false, x, y);
    else                       queueUpAt(SDL_BUTTON_LEFT, x, y, due);
    g_leftHeld = false;
}
```

`SDL3GameEngine.cpp:drainUps` runs first thing in `touchTick` every frame. For each pending up it either fires the release (deadline reached) or emits the keep-alive motion:

```cpp
if (now >= g_ups[i].dueMs) {
    synthButton(g_ups[i].button, false, g_ups[i].x, g_ups[i].y);
    g_ups[i].active = false;
} else {
    // Keep the synthesized press "alive" with a motion every frame, just like a real
    // held finger. Without this the WindowManager never sees the press sustained and a
    // pushbutton's up won't fire its callback.
    synthMotion(g_ups[i].x, g_ups[i].y);
}
```

The fast-tap path in `handleFingerUp` is where this pays off. If the finger lifts *before* the 45 ms defer even committed the down (`g_leftPending` still set), the handler synthesizes the down and immediately calls `releaseLeftHeld` — but because the up routes through the queue, it is guaranteed to trail the down by `CLICK_HOLD_MS`:

```cpp
} else if (g_leftPending) {
    // fast tap: finger lifted before the commit-defer — press now, and the up is
    // guaranteed to trail by >= CLICK_HOLD_MS so the click registers.
    synthLeftDown(g_leftSX, g_leftSY);
    releaseLeftHeld(g_leftSX, g_leftSY);
    g_leftPending = false;
}
```

The right-click uses the same guarantee: `SDL3GameEngine.cpp:rightClick` emits motion + right-down and queues the up at `SDL_GetTicks() + CLICK_HOLD_MS`. The queue holds eight slots — comfortably more than the two fingers can generate — and if it ever overflows `queueUpAt` releases immediately rather than dropping the up (a stuck-down button is far worse than a slightly-short press).

## 2-finger pan & pinch-zoom (View integration) and the persisted sensitivity slider

Two fingers drive the camera directly through `View`, bypassing the mouse entirely. `SDL3GameEngine.cpp:beginTwoFinger` snapshots the centroid (`g_startCx/Cy`), the initial finger separation (`g_lastDist`) and a start timestamp. On each `handleFingerMotion` with both fingers down, it recomputes the centroid and separation and drives two independent view operations:

```cpp
const float sens = panSensitivity();   // Options > Scroll Speed slider (persisted)
Coord2D off;
off.x = -(cx - g_lastCx) * PAN_K * sens;   // grab-world pan: terrain follows the fingers
off.y = -(cy - g_lastCy) * PAN_K * sens;
if (off.x != 0.0f || off.y != 0.0f) TheTacticalView->userScrollBy(&off);
float zoomH = (g_lastDist - dist) * PINCH_ZOOM_K * sens;   // spread fingers -> zoom in
if (zoomH != 0.0f) TheTacticalView->userZoom(zoomH);
```

Two design choices are baked into the signs. The pan offset is **negated** so it is grab-world (direct-manipulation): dragging the fingers right slides the *terrain* right under them, which is what a touch user expects — the map follows the fingers, not the camera. The zoom uses `(g_lastDist - dist)` so that spreading the fingers apart (separation increasing → the term goes negative → `userZoom` receives a smaller height) zooms *in*, matching the pinch idiom on every phone. `PAN_K = 1.0` and `PINCH_ZOOM_K = 0.08` are the base scalars — one screen-pixel of centroid travel per world unit of scroll, and a gentle zoom-height-per-pixel so a pinch is not violent.

**`g_twoMoved` gates the right-click.** The same motion handler flags the gesture as moved once the centroid travels past `MOVE_THRESH_PX` *or* the separation changes by more than `MOVE_THRESH_PX`. That flag is what separates a two-finger *tap* (a command) from a two-finger *drag* (a camera move) at lift time.

### Two-finger tap vs drag: the right-click

The move/attack order is a two-finger *tap* — both fingers touch and lift quickly without panning or pinching. `handleFingerUp` makes that call when the two-finger gesture ends, using both the no-motion flag and a duration ceiling:

```cpp
const Uint64 TWO_TAP_MAX_MS = 300;    // 2-finger press shorter than this w/o move = right-click
...
if (g_two) {
    bool wasTap = (!g_twoMoved) && (SDL_GetTicks() - g_twoStartMs <= TWO_TAP_MAX_MS);
    g_two = false;
    if (wasTap && inLiveGame()) rightClick(g_startCx, g_startCy);   // move / attack order
    ...
}
```

Two conditions must both hold: the centroid/separation never crossed `MOVE_THRESH_PX` (so it was not a pan or pinch), **and** the whole gesture lasted no longer than `TWO_TAP_MAX_MS` (300 ms). The duration ceiling matters because a slow, deliberate two-finger *hold* that happens not to move should not fire a command — a right-click in an RTS is a committal action (move the army, attack that building), so the classifier demands a crisp, quick tap. The right-click is issued at the gesture's *start* centroid (`g_startCx, g_startCy`), not the lift point, so the order lands where the player originally aimed even if the fingers drifted a pixel or two before lifting. It fires only `inLiveGame()`, so a two-finger tap on a menu does nothing.

### One slider scales the whole camera

Rather than invent a new touch-sensitivity option (a new persisted field, a new options-menu control, a new save-file key), the port **repurposes the setting the game already persists for keyboard edge-scroll speed.** `SDL3GameEngine.cpp:panSensitivity` reads `TheGlobalData->m_keyboardScrollFactor` — the value behind the Options → Scroll Speed slider, which already serializes to the options file and reloads at boot — and maps it to a punchy multiplier:

```cpp
float f = TheGlobalData ? TheGlobalData->m_keyboardScrollFactor : 0.5f;
float mult = f * 6.0f;
if (mult < 0.5f) mult = 0.5f;
if (mult > 8.0f) mult = 8.0f;
return mult;
```

With the default `0.5` the multiplier is ~3×; at the slider's practical max (~1.0) it is ~6×; it is floored at 0.5× so a drag is never dead and clamped at 8× so it never becomes uncontrollable. Because `sens` multiplies **both** the pan offset and the pinch zoom-height, a single existing, already-persisted control tunes the entire camera feel — no new UI, no new storage, and the setting survives an app restart for free. This is the cheapest possible integration: the touch layer leans on state the desktop game was already saving.

The whole camera block is additionally wrapped in `if (inLiveGame())`, so two-finger motion in a menu or shell screen updates the tracking state (centroids, distances) but never calls into `View` — there is no tactical camera to move there.

## On-screen buttons: the draw/hit-test geometry-sync contract

The ☰ menu button and ▶▶ skip button are drawn by one file and hit-tested by another. There is **no shared rectangle** — each side hard-codes the geometry, and the two must be edited together or the visible button and the tappable region drift apart. The code makes this contract explicit in comments on both sides, because it is exactly the kind of thing that silently rots.

Hit-test side, `SDL3GameEngine.cpp`:

```cpp
// KEEP THIS GEOMETRY IN SYNC WITH kMenuBtn* IN W3DInGameUI.cpp (the draw side).
const float  MENU_BTN_X = 16, MENU_BTN_Y = 14, MENU_BTN_W = 108, MENU_BTN_H = 56;
```

Draw side, `W3DInGameUI.cpp:drawAndroidMenuButton`:

```cpp
// KEEP THIS GEOMETRY IN SYNC WITH kMenuBtn* THERE.
const Int kMenuBtnX = 16, kMenuBtnY = 14, kMenuBtnW = 108, kMenuBtnH = 56;
```

The skip button carries the same paired-constant contract, and it is **right-anchored** on both sides so it tracks the screen's right edge regardless of resolution. Draw side computes `bx = TheDisplay->getWidth() - 20 - 150`; hit-test side computes the mirror image in `SDL3GameEngine.cpp:onSkipButton`:

```cpp
float bx = w - SKIP_BTN_MX - SKIP_BTN_W;   // top-right, right-anchored (matches draw)
return px >= bx && px < bx + SKIP_BTN_W && py >= SKIP_BTN_Y && py < SKIP_BTN_Y + SKIP_BTN_H;
```

with `SKIP_BTN_W = 150, SKIP_BTN_H = 54, SKIP_BTN_MX = 20, SKIP_BTN_Y = 18` matching the literal `150, 54, 20, 18` in `W3DInGameUI.cpp:drawAndroidSkipButton`.

**The gating predicates must also match, not just the rectangles.** A button that is drawn but not hit-testable (or the reverse) is a bug. Both sides gate on the same game state:

- The menu button draws when `TheGameLogic->isInGame() && !isInShellGame() && !isQuitMenuVisible()` and there is no open video stream. The hit-test's `SDL3GameEngine.cpp:menuButtonActive` encodes the same predicate via `inLiveGame()`, `(!TheInGameUI || !TheInGameUI->isQuitMenuVisible())`, and `!videoPlaying()` — with the comment "Matches W3DInGameUI.cpp's draw gate." The hit-test folds `videoPlaying()` (both `TheVideoPlayer->firstStream()` and `TheDisplay->isMoviePlaying()`) in explicitly, so it is if anything *stricter* than the draw side — they never disagree in a state that matters.
- The skip button draws when `TheVideoPlayer->firstStream()` is non-null (a video is open) and is hit-tested only while `videoPlaying()` holds. During a cutscene the top-right shows Skip and the menu button is suppressed; outside a cutscene it is the reverse. The two buttons are mutually exclusive by construction.

### Arm-on-down, fire-on-up

Neither button fires on touch-down. `handleFingerDown` only *arms* it (`g_escArmed` / `g_skipArmed`, plus the owning finger id) and, crucially, sets `g_leftPending = false` so arming a button never also starts a select. The action fires from `handleFingerUp` **only if the finger lifts while still inside the button** and the gating still holds:

```cpp
if (g_escArmed && tf.fingerID == g_escFingerId) {
    bool fire = onMenuButton(px, py) && menuButtonActive();
    g_escArmed = false;
    dropFinger(idx);
    if (g_tfCount == 0) { g_suppressLeft = false; g_leftPending = false; g_leftHeld = false; }
    if (fire) ToggleQuitMenu();
    return;
}
```

This is standard press-and-release button semantics — slide off the button before lifting and it does not fire — and it means the menu button invokes the very same `ToggleQuitMenu()` that the desktop `ESC` key runs, while the skip button synthesizes a real `KEY_ESC` through `synthKey` that the movie-abort loops already poll for. A second finger landing while a button is armed cancels the arm (`g_escArmed = false; g_skipArmed = false;` in `handleFingerDown`) and hands control to the two-finger gesture — you cannot accidentally toggle the menu while starting a pan.

## Why the buttons are drawn in W3DInGameUI, not W3DDisplay

The obvious home for an on-screen overlay is the end of `W3DDisplay::draw()`, where the display finishes a frame. That is wrong for the skip button, and the reason is specific to the port's cutscene path. Campaign mission intros play on the **LoadScreen**: when the display is in load-screen render mode it calls `TheInGameUI->draw()` and then takes an early-out — `End_Render()` then `continue` — **before** reaching the display's end-of-frame code. A button drawn at the end of `W3DDisplay::draw()` would therefore be invisible for the entire mission-intro movie, which is exactly when the player most needs a Skip control.

`W3DInGameUI::draw()` runs in *both* the load-screen and the normal render paths, so it is the one draw hook guaranteed to execute during a mission-intro movie. The overlay is emitted there, last, so it sits on top of the HUD and the video:

```cpp
// Drawn last so it sits on top of the HUD/video. During a cutscene show the SKIP button
// (top-right); otherwise, while actually playing, show the ESC/menu button (top-left).
if (TheVideoPlayer && TheVideoPlayer->firstStream())
    drawAndroidSkipButton();
else if (TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !isQuitMenuVisible())
    drawAndroidMenuButton();
```

The `W3DInGameUI.cpp:drawAndroidSkipButton` comment states the contract in one line — "Drawn here (not W3DDisplay) because the loadscreen render path calls `TheInGameUI->draw()` then early-outs before the display's end-of-frame code." This is the same load-screen early-out documented for video playback in [`../fixes/cutscenes-video.md`](../fixes/cutscenes-video.md); the touch layer's button placement is downstream of that rendering quirk. Both buttons are drawn with the same primitives the HUD uses — `TheDisplay->drawFillRect`, `drawOpenRect`, `drawLine`, and `GameMakeColor` with a tan/gold bevel — so they read as native in-game controls rather than an OS overlay.

## Why desktop is unaffected

Every line of this scheme is inside `#if defined(__ANDROID__)`:

- The touch state machine, the synthesis primitives, the constants, and the two gating helpers are one big Android-only block in `SDL3GameEngine.cpp`; on desktop the file compiles as if none of it exists.
- The hint disables (`SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0")`, `SDL_HINT_MOUSE_TOUCH_EVENTS`) are Android-only, so desktop SDL keeps its normal mouse pipeline.
- The finger-event `case` labels in `pollSDL3Events` and the `touchTick()` call are guarded, so the desktop event loop is byte-for-byte the pre-port loop.
- `W3DInGameUI.cpp:drawAndroidMenuButton`, `drawAndroidSkipButton`, and their call site at the end of `W3DInGameUI::draw()` are guarded, so no overlay is drawn on desktop.

Because the layer only ever *produces* the same `SDL_EVENT_MOUSE_*` and `SDL_EVENT_KEY_*` events the engine already consumes — it adds no new consumer and changes no existing handler — the desktop mouse/keyboard path is untouched. Touch is purely additive input on top of an unchanged core.

## Gotchas & lessons

- **Deferral, not prediction, disambiguates fingers.** You cannot know at touch-down whether a second finger is coming, so do not guess — hold the click for `TAP_DEFER_MS` (45 ms) and let a second finger, a drag past `MOVE_THRESH_PX` (14 px), or the timer decide. Committing the left-down eagerly and "taking it back" is not possible once the GUI has reacted.
- **A synthesized click is not a real click.** Latching GUI widgets need the press *sustained* — a minimum `CLICK_HOLD_MS` (150 ms) of wall-clock press **and** a motion event every frame while the press is pending. Miss either and the button visibly arms but its callback never fires. The `PendingUp` queue + `drainUps` keep-alive exist entirely to fake "a finger is still holding this down."
- **Latch after a multi-finger gesture until full lift.** `g_suppressLeft` prevents the surviving finger of a lifted two-finger pan from spawning a rogue drag-box. Reset only at `g_tfCount == 0`, from every branch.
- **Two independent constant sets, one contract.** The button rectangles are hard-coded on both the draw (`W3DInGameUI.cpp`) and hit-test (`SDL3GameEngine.cpp`) sides. There is no shared source of truth — only paired "KEEP GEOMETRY IN SYNC" comments. Right-anchor math (`W - margin - width`) must be mirrored exactly on both sides or the visible button and its tap target separate on wide screens.
- **Match the *gating*, not just the geometry.** A drawn-but-untappable button is as broken as a misplaced one. `menuButtonActive()` deliberately restates the draw gate and adds `!videoPlaying()` so the two never disagree.
- **Reuse persisted state instead of adding settings.** The camera sensitivity leans on `m_keyboardScrollFactor` (Options → Scroll Speed), which already saves and reloads. One slider scales pan *and* pinch — zero new UI, zero new storage, and it survives a restart.
- **Treat `FINGER_CANCELED` as `FINGER_UP`.** The OS can revoke a touch without an up event; routing cancel to the lift handler is what stops a phantom held finger from wedging the state machine.
- **Draw overlays where the frame actually renders.** During a load-screen cutscene the display early-outs before its end-of-frame code, so the Skip button lives in `W3DInGameUI::draw()` — the one hook that runs on both render paths.

## Related

- [`../fixes/touch-controls.md`](../fixes/touch-controls.md) — the summary version of this scheme (gesture map, the four design details, the sensitivity slider).
- [`../fixes/cutscenes-video.md`](../fixes/cutscenes-video.md) — FFmpeg video playback and the load-screen early-out that dictates where the Skip button is drawn.
- [`logging-storage-and-movie-bootstrap.md`](logging-storage-and-movie-bootstrap.md) — sibling deep-dive on the storage/bootstrap side of the same Android port.
