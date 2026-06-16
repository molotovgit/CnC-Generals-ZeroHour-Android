# Rendering bring‑up: Direct3D 8 → DXVK → Vulkan on Mali

The engine renders through **Direct3D 8** (via the WW3D2 layer). There is no D3D8 on Android,
so this port stacks translation layers:

```
Game (WW3D2 / D3D8 calls)  →  DXVK (d3d8 → d3d9 → Vulkan)  →  Mali‑G57 Vulkan driver
        window + input  →  SDL3  →  ANativeWindow / Vulkan surface
```

- `libdxvk_d3d8.so` implements D3D8 on top of `libdxvk_d3d9.so`, which targets Vulkan.
- SDL3 owns the window, the Vulkan surface (`SDL_Vulkan_CreateSurface`), and all input.
- The native entry point is `SDL3Main.cpp` (replaces `WinMain`), which sets up SDL + DXVK's
  WSI and then runs the engine's `GameMain()`.

## Getting to the main menu

Reaching a rendered main menu required a chain of platform fixes, each documented separately:

- **Font/text rendering** — the GDI/DirectDraw text path had to be routed through the
  cross‑platform renderer so menus and in‑game text draw at all.
- **Fullscreen at native resolution** — see
  [fullscreen-native-resolution.md](fullscreen-native-resolution.md).
- **Texture format capabilities** — the DXVK backbuffer format didn't map to a legacy
  `WW3DFormat`, which made the caps table report *no* supported texture formats and blanked
  render‑target‑backed UI (radar). See [minimap-radar.md](minimap-radar.md).
- **Software BC/DXT decode** — Mali doesn't expose the exact legacy compressed formats the
  engine assumes; textures are decoded in software where needed. See
  [terrain-textures-bc-decode.md](terrain-textures-bc-decode.md).

## The gameplay blocker: Mali shader‑compiler crash

The single hardest problem was an intermittent, then deterministic, crash inside the Mali
shader compiler during missions. It was root‑caused to **Direct3D user clip planes**
(translated to `gl_ClipDistance` vertex output) crashing the Mali‑G57 compiler. Disabling that
path fixed missions and unblocked deploying the full model archive. Full writeup:
[mali-shader-cmpbe-crash.md](mali-shader-cmpbe-crash.md).

## Device‑specific gotchas

- **10‑bit swapchain.** On this device DXVK selects a 10‑bit surface format
  (`VK_FORMAT_A2B10G10R10_UNORM_PACK32`). Legacy code paths that assume 8‑bit formats must
  tolerate this — see the radar terrain‑texture fix in [minimap-radar.md](minimap-radar.md).
- **Auto‑rotation.** A portrait↔landscape flip recreates/breaks the swapchain. The app is
  locked to landscape in the manifest; keep it that way.
- **Heap corruption** in an early bring‑up build was traced and fixed as a gameplay blocker
  before missions were stable.

See the top‑level [PORTING-NOTES.md](../PORTING-NOTES.md) for the full chronological journal.
