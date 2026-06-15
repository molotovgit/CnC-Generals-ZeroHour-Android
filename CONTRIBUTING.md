# Contributing

Thanks for wanting to help bring *Generals: Zero Hour* to Android! This is a community,
non‑commercial project.

## Ground rules

- **Never** commit, attach or link game data — `.big` archives, `.bik` movies, maps, ISOs, or
  any EA‑copyrighted assets. PRs/issues containing them will be closed. Everyone supplies their
  own retail files (see [docs/GAME-FILES.md](docs/GAME-FILES.md)).
- The engine is **GPLv3**. Contributions are under GPLv3. Keep the existing per‑file copyright
  and license headers intact.
- Keep Android‑specific engine changes behind `#if defined(__ANDROID__)` (or `!defined(_WIN32)`
  where that's the existing convention) so desktop builds are unaffected.

## Good first areas

- **LAN multiplayer** (in progress) — UDP broadcast/multicast discovery between devices.
- **GPU compatibility** beyond Mali‑G57 (Adreno / PowerVR / Xclipse) — the clip‑plane and
  swapchain‑format issues in [docs/fixes/](docs/fixes/) are good places to check first.
- **Controls polish** — gesture tuning, an on‑screen keyboard for hotkeys, gamepad support.
- **Performance** — frame pacing, texture memory.

## How to build/test

See [BUILDING.md](BUILDING.md). Test on a real `arm64-v8a` device; keep it landscape‑locked.
When reporting a bug, include: device model + GPU, Android version, and the app's private log
(stderr is redirected there — the write‑ups in `docs/fixes/` say where to look for each subsystem).

## Commit style

Conventional‑ish prefixes (`fix:`, `feat:`, `docs:`, `build:`, `chore:`) and a body that explains
the **symptom → root cause → fix** — the same style as the existing history.
