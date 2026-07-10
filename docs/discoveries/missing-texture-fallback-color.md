# Why the missing-texture fallback must be white, not magenta

> **TL;DR** — The engine's missing-texture placeholder was the classic debug
> magenta `0x7FFF00FF`. On terrain, that placeholder is not *displayed* — it is
> bound into a multi-texture **MODULATE** stage and multiplied over the base
> tile. Magenta `(1, 0, 1)` zeroes the green channel of every terrain pixel and
> tints the whole map pink. The macro/noise texture that triggers this
> (`TSNoiseUrb.tga`) is genuinely absent from the retail `.big` archives, so the
> fallback is hit in *normal* play, not as an error. The fix in
> `missingtexture.cpp:MissingTexture::_Init` swaps the constant to opaque white
> `0xFFFFFFFF` — the multiplicative identity — so an absent modulate texture
> becomes a no-op and present textures are untouched.

## Symptom (pink terrain)

Terrain renders with a uniform pink/magenta cast laid over otherwise-correct
tile detail. Crucially this is a *tint*, not a flat fill: the ground texture,
blends and lighting are all still visible underneath — they are simply pushed
toward magenta. Units, UI and models are unaffected. The cast is present
everywhere terrain is drawn and does not correspond to any specific broken
asset, which is the first clue that a single global constant is at fault rather
than one bad texture.

This is a *different* failure from the flat, opaque magenta terrain caused by the
BC/DXT atlas-decode bypass — see [Related](#related). There, whole tiles are
replaced by the fallback and shown *directly*, so magenta appears as magenta.
Here, the base tile is present and correct; the fallback is *multiplied on top of
it*, so the fallback's color leaks in as a tint rather than a replacement. Same
placeholder surface, two independent code paths, two distinct visual signatures.

## Background: modulate texture stages & the debug-magenta convention

Generals terrain is drawn with **multi-texture** material stages. A single
terrain pass composites several textures — the base tile texture, plus additional
detail/macro and lightmap stages — by combining them with fixed-function stage
operations. The relevant operation here is `D3DTOP_MODULATE`: the stage's output
is the **per-channel product** of its two arguments,

```
result = arg1 * arg2         // component-wise, each operand in [0, 1]
```

so a modulate stage layered over the base computes `base * stage` for R, G, B
(and A). This is the standard way to apply a macro/noise or lightmap texture: it
darkens or tints the base without replacing it.

Separately, when the engine is asked for a texture it cannot load, it does not
fail the draw — it substitutes a small procedurally-filled placeholder surface
built once in `missingtexture.cpp:MissingTexture::_Init`. The original fill used
the time-honored **debug-magenta** convention: fill the placeholder with a
loud, unnatural color so a missing texture screams at you on screen. Magenta is
the traditional choice precisely because it never occurs in real art.

That convention has one unstated assumption baked into it: **the placeholder is
meant to be looked at directly.** It works when a missing texture is the final
thing sampled for a pixel. It breaks the moment the missing texture is instead an
*input to a blend* — because then you are no longer seeing the debug color, you
are seeing whatever the debug color does to the math.

## Root cause (magenta is not the modulate identity)

The pre-fix constant was:

```c
//*buffer++=missing_image_palette[*pixels++];
*buffer++=0x7FFF00FF;          // ARGB: A=0x7F, R=0xFF, G=0x00, B=0xFF  → half-alpha magenta
```

Decoded as A8R8G8B8, `0x7FFF00FF` is R=255, G=0, B=255 (magenta) with A=127
(~0.5). Normalized to the `[0, 1]` operands the fixed-function pipeline actually
multiplies with, the placeholder is `(R, G, B, A) = (1, 0, 1, 0.5)`.

Now feed that into a terrain modulate stage — `result = base * stage`:

| Channel | Base   | Magenta stage `(1,0,1,0.5)` | Modulated result        |
|---------|--------|-----------------------------|-------------------------|
| R       | `base.r` | `× 1`                     | `base.r`   (unchanged)  |
| **G**   | `base.g` | `× 0`                     | **`0`**  (green killed) |
| B       | `base.b` | `× 1`                     | `base.b`   (unchanged)  |
| A       | `base.a` | `× 0.5`                   | `base.a * 0.5`          |

The green channel of **every** terrain pixel is forced to zero, and its alpha is
halved. Zeroing green on arbitrary ground colors leaves red + blue — i.e. the
whole map is pulled toward magenta/pink. The debug color did exactly what it was
designed to do (be loud and magenta), but because it landed inside a MODULATE
rather than on screen directly, "loud magenta" became "multiply the scene by
magenta." A debug aid turned into a rendering bug.

The trigger is not a packaging mistake on the Android side. The stage-3
lightmap/macro texture the terrain material binds here is `TSNoiseUrb.tga`, and
that asset is **absent from the shipped retail `.big` archives** — the engine
references it as an (effectively optional) macro/noise stage, but the retail data
set never included the file. So on any machine, the terrain material asks for
`TSNoiseUrb.tga`, the load fails, and the engine binds the missing-texture
placeholder into that modulate stage as a matter of course. This is a *normal*,
every-frame code path during ordinary gameplay — not a symptom of corrupted or
incomplete game data. The fallback color is therefore composited into shipping
visuals, which is exactly the situation the debug-magenta convention does not
account for.

## The fix (opaque white)

Change the fill constant to opaque white, the multiplicative identity:

```c
// @port Android: use opaque white instead of magenta (0x7FFF00FF) for the
// missing-texture fallback. Terrain multi-texture stages (e.g. the stage-3
// lightmap/macro "TSNoiseUrb.tga", which is absent from the shipped .big
// archives) MODULATE this texture over the base; a magenta fallback zeroes
// the green channel and tints the whole terrain pink. White is the identity
// for a modulate and leaves genuinely-present textures unchanged.
*buffer++=0xFFFFFFFF;          // ARGB: A=R=G=B=0xFF  → opaque white = (1,1,1,1)
```

White normalizes to `(1, 1, 1, 1)`. Run it through the same modulate:

```
result = base * (1, 1, 1, 1) = base
```

Every channel — including alpha, which `0xFFFFFFFF` also raises from 0.5 to full
— is multiplied by 1. The stage becomes a true no-op: the base tile passes
through unchanged and the pink cast disappears. `1` is the identity for
multiplication, so white is the *only* fill color that guarantees a missing
MODULATE texture contributes nothing regardless of what the base is.

The change is a single constant in one function; it does not touch the terrain
material setup, the stage state, or the texture loader. It fixes the class of bug
— "missing texture consumed by a multiply" — at the one point where the
placeholder is defined, for every stage that modulates it.

## Why this doesn't hide real missing textures / why desktop is effectively unaffected

**It doesn't mask genuine problems.** White is the identity *only* for
multiply/modulate. A texture that is missing and then sampled **directly** (the
common case the debug convention was built for) still renders as a solid,
flat white patch — an obviously wrong, out-of-place block against real art,
just a different loud-and-wrong color than magenta. And a *present* texture is,
by construction, returned unchanged whether the fallback is white or magenta,
because the fallback is never sampled for it. So white loses none of the
"something is missing" signal for directly-sampled textures while removing the
false signal for modulated ones. We also do not *rely* on the fallback color as
our missing-texture detector: the real directly-displayed failures (e.g. the
terrain atlas) are fixed at their source in the decode path, not papered over by
whatever color the placeholder happens to be.

**Why desktop was effectively unaffected.** The original desktop build shipped
with the magenta constant, yet retail desktop terrain is not pink — which is
itself evidence that on the desktop D3D8 path the placeholder surface was not
being multiplied into the terrain the way it is on the Android/DXVK path. The
exact reason the desktop path avoids compositing the fallback into the stage is
outside the scope of this change; the observable fact is that the magenta
constant was harmless on desktop and latent-wrong everywhere. The Android/DXVK
fixed-function translation exercises the modulate stage with the fallback surface
bound, which is what surfaced the long-dormant bug. White is a strictly safer
constant on *every* platform — it can never tint a modulate — so correcting it
carries no desktop regression risk.

## Gotchas & lessons

- **A debug placeholder is not free of semantics.** The instant a "missing"
  surface can be *consumed by a blend* instead of *shown*, its color stops being
  a neutral flag and becomes an operand. Choose the fill to be the identity of
  the operation that will consume it. For MODULATE that is white `(1,1,1,1)`; for
  an additive stage it would be black `(0,0,0,0)`.
- **Magenta is the worst possible modulate fallback**, not the best debug color,
  because it zeroes green — the fix would be equally necessary for cyan `(0,1,1)`
  or yellow `(1,1,0)`; any non-white constant tints a multiply.
- **"Missing" does not imply "broken data."** `TSNoiseUrb.tga` is absent from the
  retail archives by design of the shipped data set, so the fallback path is
  load-bearing, every-frame, normal-play code — not an error handler you only hit
  when something is wrong. Treat frequently-hit fallbacks as production paths.
- **Same file, two unrelated symptoms.** Both this pink-tint bug and the flat
  magenta terrain route through `missingtexture.cpp`, but they are independent:
  one is the fallback *color* being wrong for a modulate consumer, the other is
  the atlas *decode* being skipped so the fallback is shown directly. Fixing one
  does not fix the other; don't conflate them.
- **Alpha rode along.** `0x7FFF00FF` also carried a half alpha (`0x7F`); the white
  constant restores full opacity, so the fix is the identity across all four
  channels, not just RGB.

## Related

- [`../fixes/terrain-textures-bc-decode.md`](../fixes/terrain-textures-bc-decode.md)
  — the *separate* flat-magenta terrain bug: the BC/DXT terrain-atlas fill path
  bypassed the software-decode hook, so tiles were shown as the fallback
  *directly*. Different mechanism, different fix; do not confuse it with the
  modulate-tint issue documented here.
- [`dxvk-texture-format-capabilities.md`](dxvk-texture-format-capabilities.md)
  — how texture-format support is reported through DXVK on Mali, background for
  why the fixed-function translation path exercises stages the desktop driver
  handled differently.
