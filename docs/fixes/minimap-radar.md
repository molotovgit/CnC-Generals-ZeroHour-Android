# Minimap / radar

The minimap went through two distinct fixes: a **format‑caps** fix (so the radar textures work
at all) and a **visibility‑gate** fix (so the minimap behaves correctly per game mode).

## 1. Format‑caps: the radar was black because *all* texture formats were disabled

**Root cause.** The engine's caps probe (`DX8Caps::Check_Texture_Format_Support` /
`Check_Render_To_Texture_Support`) is seeded with the **backbuffer/display format**. On
DXVK/Mali the swapchain format (e.g. a 10‑bit or sRGB Vulkan format) doesn't map back to any
legacy `WW3DFormat`, so `display_format` arrived as `WW3D_FORMAT_UNKNOWN`. The original code then
marked **every** texture format unsupported — which blanked the radar/minimap terrain texture and
anything else gated on `Support_Texture_Format`.

**Fix.** On non‑Windows, when `display_format == WW3D_FORMAT_UNKNOWN`, query support against a
guaranteed‑valid adapter format (`X8R8G8B8`) instead of disabling everything.

## 2. The radar terrain texture came out black on a 10‑bit swapchain

**Root cause.** The radar builds its terrain overview by **CPU‑filling a locked texture surface**,
using `Get_Bytes_Per_Pixel(surfaceDesc.Format)` for the stride. Its first‑choice format was
24‑bit `R8G8B8`. DXVK reports `R8G8B8` "supported" but stores it as 32‑bit — so a 3‑byte‑stride
CPU fill produced garbage/black. This only started getting picked after the display came up as a
10‑bit swapchain changed the format‑support results.

**Fix (`W3DRadar.cpp`).** On non‑Windows, prefer the byte‑exact 32‑bit `X8R8G8B8`/`A8R8G8B8` for
the radar terrain texture *before* 24‑bit `R8G8B8`, so a 24‑bit stride is never chosen.

## 3. Visibility gate: correct minimap behavior per mode

Even with the texture correct, the minimap was blank — because the draw is gated on
`rts::localPlayerHasRadar()` (`W3DLeftHUDDraw` → `W3DRadar::draw`). With no powered radar
building, desktop Generals blanks the minimap. That's correct for **skirmish/multiplayer** but
undesirable on a **phone campaign**, where the minimap is essential for touch navigation.

**Fix (`GameUtility.cpp`, `localPlayerHasRadar()`).** On Android, keep the terrain minimap
available **only in single‑player campaign** (`GameLogic::isInSinglePlayerGame()`), while
**skirmish and multiplayer keep the normal desktop radar‑gating** (minimap appears once you
build/power a radar). An explicit scripted radar‑hide is still respected.

## Debugging technique

Because the draw was gated, the texture *build* was confirmed independently: logging the filled
surface's format/stride and reading back a center pixel proved the terrain image was generated
with real colors — which is how the problem was isolated to the visibility gate rather than the
rendering.
