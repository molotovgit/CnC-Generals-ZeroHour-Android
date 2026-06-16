# Changelog — Android port

All changes are relative to the [GeneralsX](https://github.com/fbraz3/GeneralsX) baseline
(`@0cdbc81`). Grouped by area; see [docs/](docs/) for detailed write‑ups.

## Platform / build
- Android (`arm64-v8a`) target: NDK 27 toolchain, CMake, Gradle app project.
- `SDL3Main.cpp` native entry point (replaces `WinMain`): SDL3 + DXVK WSI init, `chdir()` to the
  app's internal data dir, publish native display size, run `GameMain()`.
- CMake: `SAGE_USE_OPENAL` backend, SDL3/FFmpeg via vcpkg, DXVK `.so` integration
  (`cmake/sdl3.cmake`, per‑target `CMakeLists.txt`, `vcpkg.json`).

## Rendering (DXVK → Vulkan on Mali‑G57)
- **Fixed** the Mali shader‑compiler crash in missions — root cause: D3D user clip planes
  (`gl_ClipDistance`). Disabling them unblocked gameplay and full model deployment.
- **Fixed** a DXVK heap‑corruption crash (gameplay blocker).
- **Fixed** the texture‑format caps: an unmapped DXVK backbuffer format made the engine disable
  **all** texture formats (blanked the radar). Fall back to `X8R8G8B8` for the caps query.
- **Fixed** magenta terrain: the terrain atlas fill path bypassed the BC/DXT decode hook.
- **Added** software BC/DXT decode where Mali lacks the legacy compressed formats.
- **Fixed** true fullscreen at native 1640×720 (was 4:3 pillar‑boxed); Options→Resolution no
  longer crashes on the empty Win32 mode table.
- Pipeline‑creation serialization as defense against compiler races.

## Minimap / radar
- **Fixed** black radar texture on the 10‑bit swapchain: prefer 32‑bit `X8R8G8B8` over 24‑bit
  `R8G8B8` for the CPU‑filled radar terrain texture (`W3DRadar.cpp`).
- **Changed** minimap availability: on Android, campaign keeps the minimap always‑on (touch
  navigation); skirmish/multiplayer keep desktop radar‑gating (`GameUtility.cpp`).

## Input (touch)
- **Added** a full multi‑touch control scheme in `SDL3GameEngine.cpp` (tap/drag/pinch/2‑finger,
  min‑press + motion‑keepalive for synthesized clicks).
- **Added** the on‑screen **☰** in‑game menu (ESC) button (`W3DInGameUI.cpp`).
- **Added** the on‑screen **▶▶** cutscene Skip button (synthesizes `KEY_ESC`).
- **Added** a persisted touch sensitivity control (repurposes Options→Scroll Speed for pan+zoom).
- Disabled SDL's built‑in touch→mouse synthesis (set before `SDL_InitSubSystem`).

## Audio
- **Fixed** total silence: `SDL3Main.cpp` forced desktop OpenAL drivers on Android (`__linux__`
  branch) → the `null` device. Force the **OpenSL ES** backend on Android.

## Video / cutscenes
- FFmpeg Bink (`.bik`) playback; movies optional.
- First‑launch bootstrap to stage movies from external → internal storage (dev route).

## Packaging
- **Added** the self‑contained APK: game data bundled as split `.pak` assets + a `SetupActivity`
  first‑run extractor. Documents the SELinux constraints on writing internal storage.
- App icon + label ("Generals Zero Hour").

## In progress
- LAN multiplayer between two Android devices (UDP discovery; `MulticastLock` held by the
  Activity; `udp.cpp`, `LanLobbyMenu.cpp`).
