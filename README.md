# Command & Conquer: Generals — Zero Hour on Android

An **Android (arm64) port** of *Command & Conquer: Generals — Zero Hour*, running the
original game engine natively on a phone through
[**GeneralsX**](https://github.com/fbraz3/GeneralsX) (the GPLv3 cross-platform fork of the
engine source EA released in 2025), **SDL3** for windowing/input, and **DXVK** translating
the game's Direct3D 8 calls to **Vulkan** on Mobile GPUs (tested on Mali‑G57).

It reaches the main menu, plays skirmish and the single‑player campaigns, renders terrain,
units, water, shadows, the HUD, the minimap and the in‑engine/​video cutscenes — with a
touch‑first control scheme designed for a phone.

> **This repository contains only source code and documentation, licensed under the GPLv3.**
> It does **not** include any game data. The `.big` archives, movies, maps and other art/audio
> are Electronic Arts' copyrighted content — you must supply your own from a retail copy of the
> game. See [docs/GAME-FILES.md](docs/GAME-FILES.md).

---

## Status

| Area | State |
|---|---|
| Boot → main menu | ✅ working, native 1640×720 fullscreen |
| Skirmish (all factions) | ✅ playable |
| Single‑player campaigns (USA / GLA / China) | ✅ playable, intro cutscenes play |
| Rendering (terrain / units / water / shadows / HUD) | ✅ correct on Mali‑G57 |
| Minimap / radar | ✅ (campaign always‑on, skirmish/MP radar‑gated as on desktop) |
| Audio (music / SFX / speech) | ✅ via OpenSL ES |
| Video cutscenes (Bink via FFmpeg) | ✅ decodes on device |
| Touch controls | ✅ full custom scheme (see below) |
| LAN multiplayer | 🚧 in progress |

---

## Touch controls

The game was mouse+keyboard only; this port adds a purpose‑built multi‑touch scheme:

| Gesture | Action |
|---|---|
| 1‑finger tap | select / press a UI button |
| 1‑finger drag | drag‑box select / drag a slider |
| 2‑finger drag | pan the camera |
| 2‑finger pinch | zoom the camera |
| 2‑finger tap | right‑click (move / attack order) |
| On‑screen **☰** button (top‑left, in‑game) | opens the pause/ESC menu |
| On‑screen **▶▶** button (top‑right, during a video) | skip the cutscene |

Camera pan/zoom speed is adjustable and **persisted** via **Options → Scroll Speed**.

---

## Quick start (play now)

1. Get an **arm64 Android** device with **~5 GB free**.
2. Install the **self‑contained APK** (bundles the game data and unpacks it on first launch).
   *(The self‑contained build is not hosted in this repo — it embeds copyrighted game data.
   Build your own from your retail files as described in [BUILDING.md](BUILDING.md), which also
   explains the self‑contained packaging step.)*
3. First launch shows *"Installing game data…"* for ~1–2 minutes, then the game starts.

If you already have the game data deployed to the app's storage, the small engine‑only APK
boots straight into the game.

---

## Build it yourself

Full instructions are in **[BUILDING.md](BUILDING.md)**. In short:

- Cross‑compile the engine for `arm64-v8a` with the Android NDK + CMake (vcpkg pulls SDL3,
  OpenAL, FFmpeg; DXVK is built separately).
- Package the Android app (`android/`) with Gradle; drop the built `libmain.so` into
  `app/libs/arm64-v8a/`.
- Supply your own retail game data (see [docs/GAME-FILES.md](docs/GAME-FILES.md)).

---

## How it works / what was fixed

Getting a 2003 Direct3D 8 RTS to run on a mobile Vulkan driver took a long series of fixes.
Each is written up in detail under **[docs/](docs/)**, and summarized in
**[CHANGELOG.md](CHANGELOG.md)**. Highlights:

- **[Rendering bring‑up on Mali (DXVK → Vulkan)](docs/fixes/rendering-dxvk-mali.md)** — reaching
  the menu and in‑game.
- **[The Mali shader‑compiler crash](docs/fixes/mali-shader-cmpbe-crash.md)** — root‑caused to
  D3D9 user clip planes (`gl_ClipDistance`) crashing the Mali‑G57 shader compiler.
- **[True fullscreen at native resolution](docs/fixes/fullscreen-native-resolution.md)**.
- **[Magenta terrain / software BC (DXT) decode](docs/fixes/terrain-textures-bc-decode.md)**.
- **[Minimap / radar rendering & availability](docs/fixes/minimap-radar.md)**.
- **[Audio via OpenSL ES](docs/fixes/audio-opensl.md)**.
- **[Cutscene playback + the on‑screen Skip button](docs/fixes/cutscenes-video.md)**.
- **[Touch control scheme](docs/fixes/touch-controls.md)**.
- **[Self‑contained APK packaging](docs/fixes/self-contained-packaging.md)**.

See **[docs/PORTING-NOTES.md](docs/PORTING-NOTES.md)** for the full technical journal.

---

## Credits & license

- **Engine:** [GeneralsX](https://github.com/fbraz3/GeneralsX) by fbraz3 and contributors — the
  GPLv3 cross‑platform fork this port builds on.
- **Original source:** *Command & Conquer: Generals* / *Zero Hour*, released by **Electronic Arts**
  under **GPLv3** in 2025.
- **Libraries:** [SDL3](https://libsdl.org), [DXVK](https://github.com/doitsujin/dxvk),
  [OpenAL Soft](https://github.com/kcat/openal-soft), [FFmpeg](https://ffmpeg.org).

This project is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE).

*Command & Conquer, Generals, Zero Hour and all game assets are trademarks/copyright of
Electronic Arts. This is an unofficial, non‑commercial, community port of the GPLv3 engine
source. No game assets are distributed here.*
