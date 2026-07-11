# Contributing

Thanks for wanting to help bring *Generals: Zero Hour* to Android! This is a community,
non‑commercial project.

By participating you agree to follow our [Code of Conduct](CODE_OF_CONDUCT.md). New here?
The [ROADMAP.md](ROADMAP.md) "Help wanted" list is a good place to find something to work on,
and [Discussions](https://github.com/molotovgit/CnC-Generals-ZeroHour-Android/discussions) is the
place to ask questions or claim an area before you dig in.

## Ways to help (pick your time budget)

You don't need to touch the engine to be useful — most of what the project needs right now isn't code.

- **~5 minutes** — If you own the game, install it and [file a device report](https://github.com/molotovgit/CnC-Generals-ZeroHour-Android/issues/new?template=device_report.yml): your phone, GPU, and how far it got. Working *or* broken both matter — mapping which hardware runs it is the current top priority, and Adreno/PowerVR/Xclipse results are gold since only Mali is confirmed so far. Starring the repo and posting a screenshot in [Show and tell](https://github.com/molotovgit/CnC-Generals-ZeroHour-Android/discussions/7) also genuinely helps other people find it.
- **~30 minutes** — Pick a [good first issue](https://github.com/molotovgit/CnC-Generals-ZeroHour-Android/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22), or improve the docs — a confusing step in [SETUP-GAME-FILES.md](docs/SETUP-GAME-FILES.md), a missing detail in [BUILDING.md](BUILDING.md). Doc PRs are some of the most valuable ones we get.
- **A weekend** — Take on a [help-wanted](https://github.com/molotovgit/CnC-Generals-ZeroHour-Android/issues?q=is%3Aissue+is%3Aopen+label%3A%22help+wanted%22) area: GPU bring-up on non-Mali hardware, LAN multiplayer, controls polish, or performance. Comment on the issue first so we don't double up.

If you're not sure where you fit, open a thread in Discussions and say what you're into — I'll point you at something.

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
