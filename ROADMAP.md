# Roadmap

A living list of where the port is and where it's going. This is a volunteer, community project —
**everything in "Help wanted" is open for contributors.** See [CONTRIBUTING.md](CONTRIBUTING.md)
to get started, and open a [Discussion](https://github.com/molotovgit/CnC-Generals-ZeroHour-Android/discussions)
if you want to claim an area or talk through an approach.

## ✅ Done

- Boot → main menu, native fullscreen (1640×720) on Mali-G57.
- Skirmish (all factions) and the USA / GLA / China campaigns, playable.
- Rendering: terrain, units, water, shadows, HUD, minimap/radar — correct on Mali via
  DXVK (D3D8 → Vulkan).
- Audio (music / SFX / speech) via OpenSL ES.
- Bink video cutscenes via FFmpeg, with an on-screen skip button.
- Purpose-built multi-touch control scheme (pan/zoom/select/right-click), persisted settings.
- Self-contained APK packaging (bundle-your-own-data, unpack on first launch).

## 🚧 In progress

- **LAN multiplayer** between two Android devices — UDP broadcast/multicast discovery and the
  lobby flow. This is the headline next feature.

## 🙌 Help wanted

Good places to jump in, roughly easiest → hardest:

- **GPU compatibility** beyond Mali-G57 — Adreno, PowerVR, Xclipse. Start from the clip-plane and
  swapchain-format notes in [docs/fixes/](docs/fixes/); most porting pain is shader-compiler and
  surface-format quirks per driver.
- **Controls polish** — gesture tuning, an on-screen keyboard for hotkeys, gamepad support.
- **Performance** — frame pacing, texture-memory budgeting, load-time reduction.
- **Packaging UX** — a friendlier first-run "add your game files" flow; better error messages when
  a `.big` is missing.
- **Translations / localization** — the setup docs and in-app strings.
- **Documentation** — expand [docs/DEVICE-SUPPORT.md](docs/DEVICE-SUPPORT.md) with community test
  results (device, GPU, Android version, what worked).

## 💡 Ideas / later

- Cloud/remote-config-free build presets for common devices.
- Save-game and settings sync.
- Controller-first UI mode.

---

Nothing here is a promise or a schedule — it's a map. If something matters to you, a PR or a
Discussion moves it up the list.
