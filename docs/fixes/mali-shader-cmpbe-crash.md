# The Mali shader‑compiler crash (the gameplay blocker)

For a long time, launching an actual mission crashed the app inside the **Mali‑G57 shader
compiler** (the crash surfaced in the driver's `cmpbe`/compiler‑backend thread). The menu and
shell map were fine; loading a real battlefield killed it.

## Investigation

- It first looked like a **thread race** in DXVK's pipeline creation — serializing pipeline
  compilation made it *more* stable and pushed the crash later, which is why an early fix
  serialized pipeline creation. But it wasn't the true cause: a specific complex shader still
  crashed the compiler deterministically once enough content loaded.
- Ruling things out: it was **not** a bad app shader, not fixable by op‑kill / explicit LOD /
  further serialization. The game was stable *without* the base `W3D.big` model archive and
  crashed deterministically *with* it — i.e. a specific model's shader tripped the compiler
  (a models‑vs‑stability tradeoff, until fixed).

## Root cause

The crashing shaders all used **Direct3D user clip planes**. DXVK translates D3D clip planes
into a **`gl_ClipDistance`** vertex‑shader output. The **Mali‑G57 driver's shader compiler
crashes** when compiling vertex shaders that write `gl_ClipDistance`.

## Fix

Disable the user‑clip‑plane path so no shader emits `gl_ClipDistance` on this GPU. With clip
planes off:

- the Mali compiler no longer crashes,
- the full `W3D.big` model archive can be deployed,
- base models render, and missions run stably (verified 8+ minutes under stress).

Clip planes in this engine are used for a few reflection/water and cut‑off effects; losing them
is a minor visual compromise versus not being able to play at all.

## Related stability fixes

- **DXVK heap corruption** — a separate early crash was traced to heap corruption and fixed as
  its own gameplay blocker.
- **Pipeline‑creation serialization** — retained as defense‑in‑depth against compiler races,
  even though the clip‑plane fix is the real cure.

This is the single most important fix in the port: it's the difference between "nice menu" and
"you can actually play."
