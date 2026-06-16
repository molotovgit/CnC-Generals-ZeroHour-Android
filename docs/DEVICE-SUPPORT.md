# Device support

## Reference device (developed & tested on)

| | |
|---|---|
| Model | Redmi (`25028RN03A`, codename `serenity`) |
| SoC / GPU | **Mali-G57** class, Vulkan 1.1+ |
| ABI | `arm64-v8a` |
| Android | 14+ |
| Orientation | **landscape-locked** (a rotation flip breaks the DXVK swapchain) |
| Free space | ~5 GB (APK in `/data/app` + extracted data in `/data/data`) |

## Requirements

- 64-bit ARM (`arm64-v8a`). No 32-bit or x86 build.
- A Vulkan-capable GPU (DXVK targets Vulkan). No GL fallback.
- Enough RAM/VRAM for the base game textures; low-RAM devices may struggle.

## Known GPU notes

- **Mali-G57**: the big one — the Mali shader compiler crashes on Direct3D user clip planes
  (`gl_ClipDistance`); the port disables them. See
  [fixes/mali-shader-cmpbe-crash.md](fixes/mali-shader-cmpbe-crash.md).
- **Adreno / PowerVR / Xclipse**: **untested.** They may not need the clip-plane workaround and
  may have their own quirks. Reports (and fixes) welcome — see [../CONTRIBUTING.md](../CONTRIBUTING.md).

## Reporting a device

Open an issue with: exact model, SoC/GPU, Android version, and the app's private log. Say what
worked and what didn't (boots? menu? in mission? audio? minimap?).
