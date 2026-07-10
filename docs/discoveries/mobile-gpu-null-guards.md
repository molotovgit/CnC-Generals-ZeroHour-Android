# Defensive null-guards for mobile-GPU resource pressure and load-order timing

> **TL;DR** — Legacy render code in the W3D device layer carried a pile of unwritten
> assumptions: this render-target texture is always allocated; `TheTerrainRenderObject`
> is always bound; a tree type always has `m_data`; the water skybox is always present.
> On desktop those held in practice, so the pointers were dereferenced blind. On Android
> two things break the assumptions at once — VRAM/texture allocation can genuinely fail on
> a mobile GPU, and render passes can run before the map/terrain globals are bound (during
> load and the water pre-pass). Each site crashed mid-mission. The fix is the same shape
> every time: check the pointer, and if it is null **bail out of the effect for this frame**
> instead of faulting. Four guards, one theme — implicit lifetime and ordering invariants
> do not survive the port.

## The pattern: implicit non-null invariants that break on mobile

The engine was written for a single target — Win32 with a fixed-function DirectX 8 device
on desktop discrete/integrated GPUs. Under that target, several pointers were *effectively*
constants:

- A render-target texture requested at reset time **always** came back from video memory —
  desktop VRAM budgets dwarf a shroud buffer, so `ReAcquireResources` never returned null.
- The terrain render object `TheTerrainRenderObject` and its `WorldHeightMap` were bound
  early and stayed bound, so any render pass that touched them ran *after* they existed.
- Content globals like a tree type's `m_data` and the water `m_skyBox` were populated during
  a load sequence whose ordering was stable on desktop.

Because these were true "in practice," the original code dereferenced them without a check.
That is the classic legacy-code hazard: an invariant that was never *stated*, only *observed*,
and observed on exactly one platform. Port the code to Android — DXVK translating D3D8 onto
Vulkan/Mali, a mobile GPU with a real memory ceiling, and a different init/draw interleaving —
and the observation stops holding. The pointer that was "always" non-null is now sometimes
null, and a blind dereference is a mid-mission SIGSEGV rather than a skipped effect.

The remedy across all four sites is deliberately boring: a null check plus an early return.
The rest of this note walks each guard, why Android specifically triggers it, and why the
early-return is the correct behavior rather than an assert or a "repair."

## Guard 1 — shroud destination texture can fail to allocate (W3DShroud)

`W3DShroud::render` updates the shroud (fog-of-war) by copying from a source texture into a
destination video-memory texture, then sampling that destination. The function already opened
with one guard against a not-yet-ready source:

```cpp
	if (!m_pSrcTexture)
		return; //nothing to update from.  Must be in reset state.
```

The destination texture `m_pDstTexture` had **no** such guard — it was assumed live and was
dereferenced further down through `Get_Filter` / `Get_Surface_Level`. On desktop that was safe:
the destination render target is (re)created in `ReAcquireResources`, and on a desktop GPU that
allocation does not fail. On a mobile GPU it can. The added guard states the invariant that used
to be implicit and enforces it:

```cpp
	// GeneralsX @bugfix Android: on mobile GPUs the destination video-memory texture can fail to
	// allocate (ReAcquireResources leaves m_pDstTexture null), yet render() dereferences it below
	// (Get_Filter/Get_Surface_Level). Also guard the terrain globals used to derive the shroud
	// rectangle. Bail out cleanly instead of crashing mid-mission.
	if (!m_pDstTexture || !TheTerrainRenderObject)
		return;
```

Why Android specifically: a mobile GPU shares a comparatively small pool of graphics memory
with the whole system, and DXVK adds its own allocations on top of the game's. A render-target
texture request that would never be refused on a desktop card can legitimately come back empty
here. When it does, `ReAcquireResources` leaves `m_pDstTexture` null — the object is in a
half-acquired state — and every subsequent frame calls `render()` again. Without the guard,
the *first* such frame after a failed acquire faults. With it, the shroud update is simply
skipped: the destination is not there to write, so there is nothing correct to do but wait.

Note this guard also short-circuits on `!TheTerrainRenderObject`, which belongs to Guard 2 —
the same early return covers both the resource-pressure case and the load-order case, because
either one makes the rest of the function undefined. See
[`dxvk-texture-format-capabilities.md`](./dxvk-texture-format-capabilities.md) for the adjacent
DXVK story on *why* a mobile-target texture format is harder to satisfy in the first place —
that format-support quirk is part of why an allocation can be refused here.

## Guard 2 — terrain/map globals not bound yet (W3DWater, W3DShroud getMap)

The shroud is not only owned by `W3DShroud`; the water renderer also reaches through the global
`TheTerrainRenderObject` to fetch the shroud texture and apply it to the water plane. Four water
sites dereferenced that global with no check. On desktop the terrain object is bound before any
water frame runs, so the global is always valid when these lines execute. On Android, render
passes can run *before* the map and terrain globals are bound — during load, and specifically in
the water pre-pass — so the global is transiently null. All four sites were hardened the same way,
by prepending a `TheTerrainRenderObject &&` term:

```cpp
	if (TheTerrainRenderObject && TheTerrainRenderObject->getShroud()) // @port Android @bugfix guard null terrain object (consistent with rest of file)
	{
		//do second pass to apply the shroud on water plane
		W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
```

```cpp
	if (TheTerrainRenderObject && TheTerrainRenderObject->getShroud() && !m_trapezoidWaterPixelShader) // @port Android @bugfix guard null terrain object
```

```cpp
	if (m_trapezoidWaterPixelShader)
	{	if (TheTerrainRenderObject && TheTerrainRenderObject->getShroud()) // @port Android @bugfix guard null terrain object
```

The commit message's own phrase — *"consistent with rest of file"* — is the tell. Some paths in
`W3DWater.cpp` already null-checked `TheTerrainRenderObject`; others did not. That inconsistency
is exactly the fingerprint of an implicit invariant: whoever added the earlier checks knew the
global could be null in *some* order, but the assumption "it's bound by now" was applied unevenly.
The port forces the question everywhere the global is touched, and the answer is a uniform guard.

The same class of bug lives one call deeper in the shroud itself. After the entry guard,
`W3DShroud::render` derives the rendered heightmap rectangle from the map:

```cpp
	WorldHeightMap *hm=TheTerrainRenderObject->getMap();
	if (!hm)
		return;	// GeneralsX @bugfix Android: no map bound yet (e.g. during load / water pre-pass) - avoid null deref
	Int visStartX=REAL_TO_INT_FLOOR((Real)(hm->getDrawOrgX()-hm->getBorderSizeInline())*MAP_XY_FACTOR/m_cellWidth);	//start of rendered heightmap rectangle
```

Even once `TheTerrainRenderObject` is non-null, its `getMap()` can still return null when no map
is bound yet — again during load or a water pre-pass that runs ahead of map binding. The very next
statement reads `hm->getDrawOrgX()` and `hm->getBorderSizeInline()`, so a null map is an immediate
fault. The guard returns before the arithmetic. This is a second, independent ordering hazard on
the same code path, which is why the fix needed two separate returns: one for the object, one for
the map it hands back.

Why Android specifically: the desktop init path bound terrain and map before water and shroud
ever drew a frame; the Android path interleaves load and draw differently, so a shroud/water pass
can be pumped while the map is still null. The invariant "terrain and map exist before we render
water" was a load-order accident of the original platform, not a guarantee the code enforced.

## Guard 3 — null tree m_data (W3DTreeBuffer)

`W3DTreeBuffer::unitMoved` reacts to a unit walking near a tree — a heavy crusher topples it,
a lighter unit pushes it aside. Both branches read the tree type's data block:

```cpp
if (canTopple && m_treeTypes[m_trees[treeNdx].treeType].m_data && m_treeTypes[m_trees[treeNdx].treeType].m_data->m_doTopple) { // @port Android @bugfix null tree m_data guard
```

```cpp
} else if (m_treeTypes[m_trees[treeNdx].treeType].m_data && m_treeTypes[m_trees[treeNdx].treeType].m_data->m_framesToMoveOutward>1) { // @port Android @bugfix null tree m_data guard
```

The original code went straight from `m_treeTypes[...]` to `.m_data->m_doTopple` and
`.m_data->m_framesToMoveOutward` with no intervening null test. The guard inserts
`m_treeTypes[...].m_data &&` ahead of each dereference so a tree type whose `m_data` was never
populated is treated as "no topple / no push-aside behavior" rather than a crash.

This is the content-side face of the same pattern. On desktop, the tree-type table was fully
populated by the time a unit could move — every entry had its `m_data`. Under the Android load
ordering (and with content paths that can leave a type registered but its data block absent), an
entry can be reached while `m_data` is still null. Because C++'s `&&` short-circuits, adding the
`m_data &&` term means the `->m_doTopple` / `->m_framesToMoveOutward` read never executes when the
block is missing, and the tree quietly does nothing. Losing one frame of topple animation on a
half-loaded tree is invisible; dereferencing null is fatal.

## Guard 4 — null skybox on map load (W3DWater)

`WaterRenderObjClass::Render` centers the skybox on the camera before drawing water:

```cpp
	if (m_skyBox && TheGlobalData && TheGlobalData->m_drawSkyBox) // @port Android @bugfix guard null skybox (crashed WaterRenderObjClass::Render on map load)
	{	//center skybox around camera
		Vector3 pos=rinfo.Camera.Get_Position();
		pos.Z = TheGlobalData->m_skyBoxPositionZ;
```

The original condition was `if (TheGlobalData && TheGlobalData->m_drawSkyBox)` — it checked the
*setting* that says "draw a skybox," but not the *object* `m_skyBox` it was about to position and
render. On desktop that gap never showed, because by the time water rendered, `m_skyBox` existed
whenever `m_drawSkyBox` was set. The commit message pins the failure precisely: this **crashed
`WaterRenderObjClass::Render` on map load**. During map load on Android, `m_drawSkyBox` can already
be true while `m_skyBox` is still null — the flag is configured before the object is constructed —
so the guarded block ran against a null skybox and faulted.

The fix prepends `m_skyBox &&` so the block runs only when there is an actual skybox to place.
This is a small but instructive variant of the theme: the code checked a *flag* as a proxy for
an *object's existence*, and the two fell out of sync under a different init order. Guard the
thing you are about to dereference, not a boolean that used to imply it.

## Why desktop is unaffected

None of these guards change desktop behavior, and that is by design:

- **Resource pressure (Guard 1).** Desktop GPUs do not refuse a shroud-sized render target, so
  `m_pDstTexture` is never null there. The `!m_pDstTexture` term is simply never taken; the
  function proceeds exactly as before.
- **Load-order timing (Guards 2 and 4).** The Win32 init path binds `TheTerrainRenderObject`,
  its `WorldHeightMap`, and constructs `m_skyBox` before the corresponding render passes run.
  The added `&&` terms are always true on desktop, so the guarded blocks execute unchanged.
- **Content population (Guard 3).** The tree-type table is fully populated before `unitMoved`
  can fire on desktop, so `m_data` is non-null and the short-circuit never trips.

In every case the guard is a no-op on the platform where the invariant genuinely held, and a
life-saver on the platform where it does not. There is no behavioral risk in shipping them to
desktop — they only add a branch that desktop never takes.

## Gotchas & lessons (how to find these; why early-return not assert)

**How these surface.** They present as hard crashes at specific moments — map load, mission
start, the first shroud/water frame, a unit brushing a tree — not as steady-state corruption.
That timing is the clue: a fault that fires *once, early, on a transition* points at an
initialization-order or resource-acquisition race, not a logic bug in the steady loop. When a
render function faults on Android but never on desktop, the first hypotheses should be (1) a
resource it assumes is allocated came back null under memory pressure, and (2) a global it reads
is not bound yet at this point in the Android sequence. Both were true here.

**How to hunt for the rest.** Grep the device layer for chained dereferences through globals and
resource pointers with no preceding null test — `TheTerrainRenderObject->`, `->getMap()->`,
`m_pDstTexture`/`m_pSrcTexture`, `.m_data->`, `m_skyBox`. The `W3DWater.cpp` case shows the
signature to look for: a file where *some* paths guard a global and others do not is a file whose
author already knew the global could be null and missed spots. Those unguarded siblings are the
next crashes.

**Why early-return, not assert or repair.** Three options exist when a pointer is unexpectedly
null: assert/abort, try to construct the missing resource on the spot, or skip the effect and
return. Early-return is right here for concrete reasons:

- An **assert** converts a recoverable, transient condition into a guaranteed crash. A failed
  texture acquire or a not-yet-bound map is not a programming error to trap — it is a real
  runtime state on mobile. Aborting would make the game *less* shippable, not safer.
- **Repairing in the render path** — allocating the render target or forcing the map to bind
  from inside `render()` — is the wrong layer and the wrong moment. Acquisition belongs to the
  reset/reacquire path; render is a hot, per-frame function that must stay side-effect-light.
- **Skipping the effect for a frame is visually free.** The shroud, the water shroud overlay,
  the skybox center, and a tree topple are all per-frame effects. Missing one is imperceptible,
  and the resource will typically be present on a later frame (the destination texture gets
  reacquired; the map finishes binding), at which point the effect resumes automatically. A
  clean `return` degrades gracefully; a dereference does not degrade at all — it ends the mission.

The larger lesson is the one that recurs across this port: legacy render code encodes lifetime
and ordering assumptions as *bare dereferences*, and those assumptions are properties of the
original platform, not of the program. Mobile resource limits break the "always allocated"
assumptions; different init timing breaks the "already bound" assumptions. The cheapest durable
fix is to make each assumption explicit at its point of use and fail soft when it does not hold.

## Related

- [`dxvk-texture-format-capabilities.md`](./dxvk-texture-format-capabilities.md) — why a
  mobile-target texture/render-target format is harder to satisfy under DXVK, which is part of
  why the shroud destination allocation (Guard 1) can be refused.
- [`sorting-vertexbuffer-16bit-overflow.md`](./sorting-vertexbuffer-16bit-overflow.md) — a
  different class of mobile-only crash (16-bit sorting vertex/index-buffer overflow) that also
  hid behind an invariant the original platform never stressed.
