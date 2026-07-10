# Porting notes — the full technical journal

A chronological account of taking *Command & Conquer: Generals — Zero Hour* from "GeneralsX
builds on desktop" to "installs and plays on an Android phone." Each stage links to the focused
write‑up under [fixes/](fixes/).

> For the long-form engineering deep-dives — symptom → root cause → fix, grounded in the actual
> code — see **[DISCOVERIES.md](DISCOVERIES.md)**. This page is the timeline; that one is the
> detail.

## The stack

```
Game logic + WW3D2 renderer (Direct3D 8 API)
        │
        ▼
   DXVK  (d3d8 → d3d9 → Vulkan)         ← libdxvk_d3d8.so / libdxvk_d3d9.so
        │
        ▼
   Mali‑G57 Vulkan driver
        ▲
   SDL3 (window, Vulkan surface, input) ← libSDL3.so
        ▲
   Android Activity (SDLActivity)       ← ZerohourActivity / SetupActivity (Java)
```

Audio: OpenAL Soft → **OpenSL ES**. Video: **FFmpeg** decodes Bink `.bik`.

## Stage 1 — Toolchain & de‑risk

Android SDK (platform 35), NDK 27, CMake 3.22, JDK 17. First proved a minimal SDL3 + Vulkan APK
runs on the phone, then cross‑compiled the engine dependencies (SDL3, OpenAL, FFmpeg, DXVK) and
the engine itself for `arm64-v8a`, packaged an APK, and booted the engine.

## Stage 2 — Rendering bring‑up → main menu

Routed text rendering through the cross‑platform path (fonts), reached a rendered main menu.
See [fixes/rendering-dxvk-mali.md](fixes/rendering-dxvk-mali.md).

## Stage 3 — Fullscreen

Native 1640×720 fullscreen instead of a 4:3 pillar‑box, and a non‑crashing Options → Resolution
list. See [fixes/fullscreen-native-resolution.md](fixes/fullscreen-native-resolution.md).

## Stage 4 — Textures / terrain

Software BC/DXT decode on Mali and the magenta‑terrain atlas fill‑path fix.
See [fixes/terrain-textures-bc-decode.md](fixes/terrain-textures-bc-decode.md).

## Stage 5 — Gameplay unlocked (the big one)

The intermittent‑then‑deterministic **Mali shader‑compiler crash** in missions, root‑caused to
Direct3D user clip planes (`gl_ClipDistance`) crashing the Mali‑G57 compiler; disabling them made
missions stable and let the full model archive deploy. Also fixed a DXVK heap‑corruption crash.
See [fixes/mali-shader-cmpbe-crash.md](fixes/mali-shader-cmpbe-crash.md).

## Stage 6 — In‑game interactivity + minimap

Verified camera scroll, unit selection and orders in‑game. Fixed the minimap: first the
format‑caps issue that disabled all texture formats, later the 24‑bit radar‑texture stride and
the campaign‑vs‑skirmish visibility gate.
See [fixes/minimap-radar.md](fixes/minimap-radar.md).

## Stage 7 — Touch controls overhaul

A full custom multi‑touch scheme (tap/drag/pinch/2‑finger), the min‑press + motion‑keepalive fix
for synthesized clicks, and the on‑screen **☰** menu button.
See [fixes/touch-controls.md](fixes/touch-controls.md).

## Stage 8 — Sensitivity slider

Repurposed the persisted **Options → Scroll Speed** slider to scale 2‑finger pan and pinch‑zoom.

## Stage 9 — Cutscenes + Skip button

FFmpeg Bink playback and a themed **▶▶** Skip button drawn in `W3DInGameUI::draw()` (so it shows
during LoadScreen cutscenes) that synthesizes `KEY_ESC` to abort the movie.
See [fixes/cutscenes-video.md](fixes/cutscenes-video.md).

## Stage 10 — Audio

The wrong OpenAL backend (desktop drivers on Android → the `null` device → silence), fixed by
forcing the **OpenSL ES** backend. See [fixes/audio-opensl.md](fixes/audio-opensl.md).

## Stage 11 — Self‑contained packaging

Bundling the game data into the APK (split `.pak` assets) and extracting it on first launch via a
`SetupActivity`, so the APK is install‑and‑play. Documents the SELinux constraints that make this
the only viable on‑device delivery.
See [fixes/self-contained-packaging.md](fixes/self-contained-packaging.md).

## In progress

- **LAN multiplayer** between two Android devices (UDP broadcast/multicast discovery; the app
  holds a `WifiManager.MulticastLock` — see `ZerohourActivity`).

## Testing environment

Redmi (model `25028RN03A`, codename `serenity`), Mali‑G57, `arm64-v8a`, driven over ADB from a
Windows host. Keep the app landscape‑locked (a rotation flip breaks the DXVK swapchain).
