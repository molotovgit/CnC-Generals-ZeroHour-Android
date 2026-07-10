# DXVK texture-format capabilities: when the backbuffer format is UNKNOWN and 24-bit lies about its stride

> **TL;DR** — On Mali-G57, DXVK brings the display up as a 10-bit swapchain
> (`VK_FORMAT_A2B10G10R10_UNORM_PACK32`). That single decision detonated two independent bugs in
> the legacy D3D8 caps code, and both blacked out the radar/minimap terrain. First: the 10-bit (or
> sRGB) backbuffer format has no legacy `WW3DFormat`, so it reverse-maps to `WW3D_FORMAT_UNKNOWN`,
> and the original caps probe responds by marking **every** texture format unsupported. Second:
> once that was fixed and the caps table came back populated, the *changed* support results caused
> the radar to pick 24-bit `R8G8B8` — which DXVK reports as "supported" but stores at 32-bit, so
> the radar's 3-byte-stride CPU fill produced garbage. Both fixes are gated `#if !defined(_WIN32)`;
> Windows behavior is byte-for-byte unchanged. Fixes live in
> `dx8caps.cpp:DX8Caps::Check_Texture_Format_Support` /
> `dx8caps.cpp:DX8Caps::Check_Render_To_Texture_Support` and
> `W3DRadar.cpp:W3DRadar::initializeTextureFormats`.

## Symptom (black radar terrain)

The game reached the main menu and started a mission, but the radar/minimap terrain rendered as a
flat black rectangle. HUD chrome and unit blips drew fine; only the terrain overview texture was
wrong. Nothing in the engine logged an error — the texture was being created and filled, it just
came out black.

Two things made this hard to read:

1. It was intermittent across bring-up builds. The failure mode *changed* as other parts of the
   display path landed — which, it turned out, was the whole story.
2. There were actually **two** distinct root causes stacked on top of each other, both producing
   the identical black-terrain symptom, and fixing the first is what exposed the second.

Both bugs are downstream of one device fact: on this hardware DXVK selects a 10-bit swapchain
surface. That is the thread that ties the two discoveries together, so it is worth understanding
the D3D8 caps model first.

## Background: the D3D8 caps/format model

The engine renders through Direct3D 8 (the WW3D2 layer). On Android there is no D3D8, so the port
stacks translation layers — `libdxvk_d3d8.so` → `libdxvk_d3d9.so` → Vulkan on the Mali-G57 driver
(see [../fixes/rendering-dxvk-mali.md](../fixes/rendering-dxvk-mali.md)). The important consequence
here is that everything the engine "knows" about texture-format support is answered by DXVK, not by
a real GPU driver.

In the D3D8 model, format support is **not** a flat yes/no per format. It is conditional on the
adapter's current display (backbuffer) format. The runtime API for this is
`IDirect3D8::CheckDeviceFormat`, which asks "given *this adapter format*, is *that resource format*
usable for *this usage* (texture, render target, …)?" A format that is valid against one display
mode can be reported unusable against another. That conditionality is the crux of both bugs.

WW3D2 caches the answers up front. `DX8Caps` walks every known `WW3DFormat` and builds two boolean
tables:

- `SupportTextureFormat[WW3D_FORMAT_COUNT]` — filled by
  `dx8caps.cpp:DX8Caps::Check_Texture_Format_Support`.
- `SupportRenderToTextureFormat[WW3D_FORMAT_COUNT]` — filled by
  `dx8caps.cpp:DX8Caps::Check_Render_To_Texture_Support`.

Both functions take the current `display_format` as their seed and translate it to a real D3D
format with `WW3DFormat_To_D3DFormat(display_format)` before probing each candidate. Everything in
the engine that later asks `Support_Texture_Format(...)` — the radar, render-target-backed UI, and
more — reads out of these two tables. If the tables are wrong, or empty, those subsystems silently
get the wrong answer.

The seed matters. `display_format` is derived by taking the actual backbuffer format the device
came up with and **reverse-mapping** it into the engine's `WW3DFormat` enum. On desktop D3D8 the
backbuffer is always one of a handful of legacy formats (`X8R8G8B8`, `A8R8G8B8`, `R5G6B5`, …), each
of which has a `WW3DFormat`, so the reverse map always succeeds. On DXVK it does not — which is
Discovery 1.

## Discovery 1 — UNKNOWN backbuffer format disables every texture format

DXVK chooses a Vulkan surface format from what the Mali driver offers. On this device that is a
10-bit format — the sibling doc records it precisely as
`VK_FORMAT_A2B10G10R10_UNORM_PACK32` — and on other configurations it can be an sRGB format. When
that backbuffer format is handed back to WW3D2 and reverse-mapped into the `WW3DFormat` enum, there
is no legacy entry for a 10-bit `A2B10G10R10` (or an sRGB) surface. The reverse map falls through
to `WW3D_FORMAT_UNKNOWN`.

So `Check_Texture_Format_Support` and `Check_Render_To_Texture_Support` are both seeded with
`display_format == WW3D_FORMAT_UNKNOWN`. The original code has a guard for exactly that case — and
the guard is catastrophic on DXVK:

```cpp
if (display_format==WW3D_FORMAT_UNKNOWN) {
    for (unsigned i=0;i<WW3D_FORMAT_COUNT;++i) {
        SupportTextureFormat[i]=false;
    }
    return;
}
```

The logic is defensible on paper: if we do not know the adapter format, we cannot call
`CheckDeviceFormat` meaningfully, so conservatively declare *nothing* supported and bail. On
desktop this branch is effectively dead code — the display format is always known, so it never
runs. On DXVK it runs every launch, and it zeroes the entire `SupportTextureFormat` table (and,
in the sibling function, `SupportRenderToTextureFormat`). Every subsequent `Support_Texture_Format`
query in the engine returns false. The radar/minimap terrain texture — and anything else gated on
these tables — cannot be created, so it renders black.

The fix does not try to invent a `WW3DFormat` for the 10-bit surface. It just refuses to treat
"unknown display format" as "no formats work," because the *texture* format support we actually
care about does not depend on the exotic backbuffer format — it depends on having *some* valid,
representative adapter format to probe against. `X8R8G8B8` is guaranteed valid on any adapter, so
the non-Windows path substitutes it:

```cpp
#if !defined(_WIN32)
    // @port Android/DXVK: the Vulkan backbuffer format (e.g. A2B10G10R10 / an sRGB
    // format) does not always map back to a known WW3DFormat, so display_format arrives
    // as WW3D_FORMAT_UNKNOWN. The original code then marks EVERY texture format
    // unsupported ... Query support against a guaranteed-valid adapter format (X8R8G8B8)
    // instead of disabling everything.
    if (display_format==WW3D_FORMAT_UNKNOWN)
        display_format = WW3D_FORMAT_X8R8G8B8;
#else
    if (display_format==WW3D_FORMAT_UNKNOWN) {
        for (unsigned i=0;i<WW3D_FORMAT_COUNT;++i) {
            SupportTextureFormat[i]=false;
        }
        return;
    }
#endif
```

After the substitution, execution continues into the normal probe loop — `d3d_display_format =
WW3DFormat_To_D3DFormat(display_format)` now resolves to a real format, and each candidate
`WW3DFormat` is checked against it as usual. The identical change is applied to
`Check_Render_To_Texture_Support`, whose comment points back at the texture function:

```cpp
#if !defined(_WIN32)
    // @port Android/DXVK: see Check_Texture_Format_Support — don't disable all formats
    // when the backbuffer format doesn't map to a known WW3DFormat.
    if (display_format==WW3D_FORMAT_UNKNOWN)
        display_format = WW3D_FORMAT_X8R8G8B8;
#else
    ... disable-all fallback unchanged ...
#endif
```

With Discovery 1 fixed, the caps tables come back populated instead of all-false. The radar could
now create its terrain texture — and immediately hit the second bug.

## Discovery 2 — R8G8B8 "supported" but stored 32-bit (the stride trap)

The radar does not render its terrain overview on the GPU. It **CPU-fills a locked texture
surface**: it locks the terrain texture, walks the surface pixel by pixel, and writes color values
into the mapped memory. The *row-to-row* stride it uses is the `pitch` returned by the lock
(`Lock(&pitch)`) — that value comes from DXVK and is correct. The trap is the *per-pixel* step: to
place each pixel within a row and to size each write, the fill uses the surface format's
bytes-per-pixel from `Get_Bytes_Per_Pixel(surfaceDesc.Format)` — effectively
`dst = pBits + y*pitch + x*bytesPerPixel`, then a `memcpy` of `bytesPerPixel` bytes.

Which format the surface has is decided by `W3DRadar.cpp:W3DRadar::initializeTextureFormats`, which
holds an ordered preference list and picks the first entry the caps table reports supported:

```cpp
const WW3DFormat terrainFormats[] =
{
    WW3D_FORMAT_R8G8B8,
    WW3D_FORMAT_X8R8G8B8,
    WW3D_FORMAT_R5G6B5,
    ...
};
```

The first choice is 24-bit `R8G8B8`, 3 bytes per pixel. On real D3D8 hardware this was fine for the
lifetime of the shipped game. On DXVK it is a trap: DXVK reports `R8G8B8` as **supported**, so the
radar happily selects it — but DXVK does not actually store a 24-bit texture. It backs `R8G8B8`
with a 32-bit allocation. The caps query and the real memory layout disagree by one byte per pixel.

The radar's CPU fill trusts the format name: it steps `bytesPerPixel = 3` per pixel and writes
3 bytes per texel, into a surface DXVK actually laid out at **4** bytes per pixel. (The row-to-row
`pitch` came correctly from the lock — it is the intra-row, per-pixel arithmetic that is wrong.)
Every pixel after the first in a row drifts one byte further out of alignment, and each 3-byte write
leaves one byte of every 4-byte texel untouched; the readback the game samples is effectively
garbage, which presented as black terrain. There was no crash and no error — the lock, fill, and
unlock all "succeeded"; only the per-pixel arithmetic was wrong.

The fix is to never let 24-bit be chosen on this path. On non-Windows the preference list is
prepended with the byte-exact 32-bit formats, so a 32-bit format is always selected first and the
stride the CPU fill computes matches the memory DXVK actually allocated:

```cpp
#if !defined(_WIN32)
    // GeneralsX @bugfix Android/DXVK: R8G8B8 (24-bit) is reported "supported" by the
    // DXVK caps query, but its CPU surface fill assumes a 3-byte stride while DXVK
    // actually stores the texture as 32-bit — so the radar terrain comes out BLACK.
    // (This became visible once the display switched to a 10-bit swapchain, which
    // changed the format-support results so R8G8B8 got picked instead of X8R8G8B8.)
    // Prefer the byte-exact 32-bit formats first so 24-bit is never chosen.
    WW3D_FORMAT_X8R8G8B8,
    WW3D_FORMAT_A8R8G8B8,
#endif
    WW3D_FORMAT_R8G8B8,
    WW3D_FORMAT_X8R8G8B8,
    WW3D_FORMAT_R5G6B5,
```

`X8R8G8B8` and `A8R8G8B8` are both 4 bytes per pixel; whichever is picked, `Get_Bytes_Per_Pixel`
returns 4, the stride matches, and the fill lands where the sampler reads. `R8G8B8` stays in the
list as a last-resort fallback for any adapter that genuinely only offers 24-bit — it is simply no
longer the *first* candidate.

Note the deliberate byte-exactness framing: the point is not that 32-bit is "better," it is that
the CPU-side stride assumption and the driver-side allocation must agree to the byte. `R8G8B8`
breaks that agreement only because DXVK's report and storage differ; the fix restores the agreement
by choosing a format where report and storage cannot diverge.

## The 10-bit swapchain that connected them

These two bugs look unrelated — one is in the caps probe, one is in the radar's fill loop — but
they share a single trigger, and that is the crux of the whole investigation.

The 10-bit swapchain does two things at once:

1. **It has no legacy `WW3DFormat`.** `A2B10G10R10` (and sRGB) reverse-map to
   `WW3D_FORMAT_UNKNOWN`, seeding the caps probe with UNKNOWN → the disable-all fallback →
   Discovery 1.
2. **It changes the set of formats the caps table reports supported.** Because D3D8 format support
   is probed *against the display format*, a 10-bit display produces a different support result than
   an 8-bit display would. The radar's first-match selection is sensitive to that set. The commit
   comment states it directly: the stride trap "became visible once the display switched to a
   10-bit swapchain, which changed the format-support results so R8G8B8 got picked instead of
   X8R8G8B8." → Discovery 2.

So the causal chain is:

```
DXVK picks 10-bit swapchain (VK_FORMAT_A2B10G10R10_UNORM_PACK32)
        │
        ├─▶ backbuffer reverse-maps to WW3D_FORMAT_UNKNOWN
        │       └─▶ original caps guard disables ALL texture formats  ── Discovery 1 ─▶ black radar
        │               (fixed: substitute X8R8G8B8 for the probe)
        │
        └─▶ format-support results now differ from an 8-bit display
                └─▶ radar's first-match selects 24-bit R8G8B8
                        └─▶ DXVK reports it supported but stores 32-bit
                                └─▶ 3-byte CPU-fill stride is wrong  ── Discovery 2 ─▶ black radar
                                        (fixed: list 32-bit formats first)
```

This is also why the symptom appeared to "move." Before the caps fix, the radar was black because
*no* format was supported. After the caps fix, the radar was black again — but now because it had
successfully picked the *wrong* format. Same pixels, different cause. It was only once Discovery 1
stopped masking the format-selection path that Discovery 2 could surface at all.

## Why desktop is unaffected

Both changes are wrapped in `#if !defined(_WIN32)`, with the original code preserved verbatim in
the `#else` branch (Discovery 1) or simply not compiled at all (Discovery 2's two prepended
entries). On a Windows/native-D3D8 build, the compiled code is identical to the stock engine. There
is no behavioral risk to the desktop target — the fixes cannot execute there.

Beyond the compile guard, neither failure mode occurs on native D3D8 in the first place:

- **Discovery 1 does not trigger.** A real D3D8 adapter presents a legacy backbuffer format
  (`X8R8G8B8`, `A8R8G8B8`, `R5G6B5`, …), each of which reverse-maps to a known `WW3DFormat`.
  `display_format` is never `WW3D_FORMAT_UNKNOWN`, so the disable-all branch never runs. It was
  defensive code for a case that only DXVK's exotic swapchain formats actually produce.
- **Discovery 2 does not trigger.** The radar's CPU fill shipped for years against native drivers
  without this bug, because on those drivers the format the caps table reports and the memory the
  driver allocates agree on stride. The report-24-bit-but-store-32-bit discrepancy is a DXVK
  translation artifact, not a property of the D3D8 contract. Windows never sees the mismatch, so it
  never picks up a wrong stride.

In short: the 10-bit swapchain, the UNKNOWN reverse-map, and the stride discrepancy are all
DXVK/Mali-specific. The `#if !defined(_WIN32)` gate keeps the desktop code path exactly as it was.

## Gotchas & lessons

- **"Supported" is a claim, not a guarantee of layout.** A caps query answers *can you use this
  format*, not *how is it stored*. DXVK reporting `R8G8B8` supported while backing it with 32-bit is
  internally consistent from the API's point of view — the leak is only in code that assumes the
  format name implies the byte stride. Any CPU-side lock-and-fill must derive stride from the
  surface it actually locked, and even then treat 24-bit formats with suspicion on translation
  layers.

- **D3D8 format support is relative to the display format.** This is easy to forget because on a
  fixed desktop it feels absolute. On a device where the swapchain format is chosen by the driver,
  the *seed* of every caps table can change out from under you, which is exactly how one swapchain
  decision perturbed two subsystems.

- **A "conservative" fallback can be the most destructive branch.** The disable-all guard was
  written to fail safe. On DXVK it failed loud-and-total: one unmapped format silently disabled the
  entire texture pipeline. When you cannot answer a caps question, substituting a representative
  valid format is usually safer than declaring universal failure.

- **Fixing one masking bug reveals the next.** Discovery 2 was physically unreachable until
  Discovery 1 was fixed, because the all-false caps table short-circuited format selection. When a
  symptom persists unchanged after a fix that should have addressed it, suspect a second cause with
  the same signature rather than assuming the first fix was wrong.

- **Confirm the build independently of the draw.** Because the radar draw is separately gated (see
  [../fixes/minimap-radar.md](../fixes/minimap-radar.md)), the texture *build* was verified on its
  own — logging the locked surface's format and stride and reading back a center pixel — which is
  how the stride mismatch was isolated from the visibility gate.

- **Keep platform divergence surgical.** Every change here is `#if !defined(_WIN32)` and leaves the
  Windows path untouched. Legacy caps/format code is load-bearing for the original target; the
  discipline is to add the translation-layer case, not to rewrite the model.

## Related

- [../fixes/minimap-radar.md](../fixes/minimap-radar.md) — the radar fix log: format-caps fix,
  the 24-bit stride fix, and the separate per-mode visibility gate.
- [../fixes/rendering-dxvk-mali.md](../fixes/rendering-dxvk-mali.md) — the D3D8 → DXVK → Vulkan
  rendering stack, the 10-bit swapchain note, and the broader bring-up chain.
- [native-fullscreen-and-resolution.md](native-fullscreen-and-resolution.md) — how the native
  fullscreen/resolution path brings up the swapchain that selects the 10-bit surface.
- [missing-texture-fallback-color.md](missing-texture-fallback-color.md) — a related
  format/fill discovery: the missing-texture fallback color and why magenta tinted the terrain.
