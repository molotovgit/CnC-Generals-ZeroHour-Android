# Game files (you supply your own)

This project is **engine source code only**. It does **not** contain, and will never contain,
the game's data files. Those are the copyrighted property of **Electronic Arts** and are not
covered by the GPL that applies to the engine.

To build/run, provide the data from a copy of **Command & Conquer: Generals — Zero Hour** that
you own (retail disc, or a digital copy you purchased). This is exactly the model used by the
upstream [GeneralsX](https://github.com/fbraz3/GeneralsX) project and by other GPL C&C engine
re‑releases: **open engine, your own assets.**

## What the engine needs

Placed in the app's data directory (internal storage; the native entry point `chdir()`s there):

```
<data>/
├── INIZH.big          # rules / INI
├── W3D.big  W3DZH.big  W3DEnglishZH.big   # models
├── Textures.big  TexturesZH.big           # textures
├── Terrain.big  TerrainZH.big             # terrain art
├── Maps*.big  Window*.big  Gensec*.big  Patch*.big  Shaders*.big
├── Audio*.big  Music*.big  Speech*.big  English*.big   # audio
├── Data/
│   ├── INI/           # INI overrides
│   ├── Movies/        # .bik cutscenes (optional, for videos)
│   ├── Maps/  Scripts/  ...
└── dxvk.conf          # DXVK tuning (shipped with this port)
```

The exact `.big` set is the standard Zero Hour install set. Movies (`Data/Movies/*.bik`) are
optional — without them, cutscenes are simply skipped.

## Getting them onto a device

On a modern, non‑rooted Android device you **cannot** `adb push` into the app's private data
directory (SELinux). The supported route is the **self‑contained APK**, which bundles your data
inside the APK (built locally, not distributed) and extracts it on first launch. See
[fixes/self-contained-packaging.md](fixes/self-contained-packaging.md).

## Please don't

Do not open issues/PRs that attach `.big` files, movies, ISOs, or links to pirated game data.
They will be removed. Buy the game — it's inexpensive and keeps this legal.
