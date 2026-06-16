# Cutscenes: video playback + the on‑screen Skip button

## Video playback

The game's cutscenes are **Bink** (`.bik`) files. This port decodes them with **FFmpeg**
(`FFmpegVideoPlayer`) instead of the proprietary Bink runtime, and they decode and play on the
Mali GPU. Movies live in `Data/Movies/` and are optional — if absent, cutscenes are skipped.

Two playback paths exist and both matter for the Skip button:

- **Startup / sizzle movies** and some flows go through `TheDisplay->playMovie()` (the display's
  own `m_videoStream`).
- **Campaign mission intros** play on the **LoadScreen**, which opens the stream via
  `TheVideoPlayer` and draws it into a GUI window while the map loads.

## The Skip button

On a phone there's no `ESC` key, so a themed **▶▶** button is drawn in the top‑right corner
**whenever a video is on screen** and tapping it skips the cutscene.

- **Skip action.** Tapping synthesizes a real `KEY_ESC` keyboard event
  (`SDL3Keyboard::addSDLEvent`). The engine's movie loops
  (`GameClient::isMovieAbortRequested()`) already poll `TheKeyboard` for an unused `KEY_ESC` and
  abort the video — and they pump `serviceWindowsOS()` (→ the touch handler) each iteration, so a
  synthesized ESC is seen and the movie skips.
- **Where it's drawn (the subtle part).** The obvious place — the end of `W3DDisplay::draw()` —
  is **skipped during a LoadScreen cutscene**, because when `m_loadScreenRender` is true the
  display takes an early `TheInGameUI->draw()` → `End_Render()` → `continue` path. So the Skip
  button is drawn inside **`W3DInGameUI::draw()`**, which runs in *both* the loadscreen and normal
  render paths. It's gated on `TheVideoPlayer->firstStream()` (a video is open).
- **Coexistence with the menu button.** During a cutscene the top‑right shows **Skip**; the
  top‑left in‑game **☰** menu button is suppressed. Outside cutscenes it's the reverse.

## Getting the movies onto the device

The movies are part of the game data and, like the `.big` files, must come from your own retail
copy. On a non‑rooted device the delivery route is the self‑contained APK — see
[self-contained-packaging.md](self-contained-packaging.md). (An intermediate approach used
during development was a first‑launch native bootstrap that copies movies from the app's
**external** storage — which `adb` *can* write — into internal storage, since SELinux blocks
writing internal storage directly.)
