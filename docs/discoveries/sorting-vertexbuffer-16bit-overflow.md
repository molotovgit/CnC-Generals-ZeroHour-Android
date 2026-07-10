# The 16-bit sorting vertex-buffer overflow that corrupted the heap (SIGSEGV 0x2000000001)

> **TL;DR** — The translucent-geometry sorting path in `sortingrenderer.cpp` batches an
> unbounded amount of geometry, but every count in that path is 16-bit: the dynamic vertex buffer,
> the dynamic index buffer, and the sorted triangle indices themselves (`ShortVectorIStruct`). The
> heap-corrupting failure was on the **vertex** side — when a batch crossed **65535** vertices, the
> DX8 dynamic-VB allocation truncated to 16 bits while the fill loop still `memcpy`'d the full
> 32-bit vertex total — a write off the end of DXVK's mapped buffer that corrupted the allocator
> heap and produced a delayed, non-deterministic `SIGSEGV 0x2000000001` after **30-70s** of heavy
> particle/smudge rendering. The fix is one guard in
> `sortingrenderer.cpp:SortingRendererClass::Insert_To_Sorting_Pool` that flushes the pool early so
> no batch can exceed the 16-bit limits — bounding **both** the vertex total and the index total,
> the latter belt-and-suspenders alongside a pre-existing index chunk-split already in the source.
> This is the crash that had forced disabling the live 3D shell map; with it fixed, the shell map
> and full gameplay run, and only the intro *movies* remain skipped — for an unrelated reason (the
> FFmpeg video path).

---

## Symptom (delayed SIGSEGV under heavy transparency)

The failure looked like the worst kind of bug: it had no stable stack, no reproducible trigger
line, and a timer instead of a cause. Sitting on the main menu — which renders the live 3D shell
map behind the UI — the app would run cleanly for a while and then hard-crash with
`SIGSEGV 0x2000000001`, consistently somewhere in the **30-70s** window of continuous
particle/smudge rendering. Kicking off a mission with lots of transparent effects did the same,
just faster.

Two properties made it maddening:

- **The fault address wasn't a pointer.** `0x2000000001` is not a plausibly-mapped address the
  code ever computes — it is the kind of value you get when execution dereferences a *corrupted*
  bookkeeping field (a mangled next/size word in an allocator node), not a clean null or a
  simple off-by-one on a real object. The crash site was wherever the heap next walked its own
  metadata, which is to say: anywhere.
- **The timer, not a line.** Runs on the same scene faulted at different points within that
  **30-70s** window rather than at a fixed moment. Nothing in the immediate backtrace pointed at
  rendering — because by the time the process faulted, the rendering code that did the damage was
  long gone.

That signature — implausible fault address, variable delay, backtrace unrelated to the actual
writer — is the fingerprint of **heap corruption**, not a logic error at the crash site. The
job was to find *who wrote out of bounds*, seconds before the process finally tripped over the
wreckage.

## Background: the translucent sorting pool and its 16-bit buffers

Opaque geometry can be drawn in any order because the depth buffer resolves visibility.
Translucent geometry cannot — alpha blending is order-dependent, so transparent surfaces must be
drawn **back-to-front**. Generals' renderer handles this with a *sorting pool*: instead of
drawing each translucent mesh as it is submitted, it defers them, accumulates them, sorts them by
depth, and flushes them as batched draw calls in the correct order.

The relevant entry point is
`sortingrenderer.cpp:SortingRendererClass::Insert_To_Sorting_Pool`. Each incoming node
(`SortingNodeStruct`) carries a `vertex_count` and a `polygon_count`; the pool tracks running
totals across everything accumulated so far:

- `overlapping_node_count` — how many nodes are currently batched,
- `overlapping_vertex_count` — total vertices across those nodes,
- `overlapping_polygon_count` — total triangles across those nodes.

When the pool is flushed (`Flush_Sorting_Pool`), all of that accumulated geometry is written into a
dynamic vertex buffer, sorted, and drawn; the index side is emitted in 16-bit
`DynamicIBAccessClass` chunks. The sorted triangle indices are stored as `ShortVectorIStruct` — a
struct of `unsigned short` index components. That 16-bit width is the crux of the whole bug:

> **Everything on this path is 16-bit.** The dynamic VB is allocated with an unsigned-short
> count. The dynamic IB is 16-bit. The sorted indices (`ShortVectorIStruct`) are 16-bit. A single
> flush therefore *cannot address more than* `65535` **vertices or** `65535` **indices** — that
> is the entire representable range of the index type.

On the original hardware this was a non-issue by construction. DX8-era content and the DX8
dynamic-buffer conventions kept translucent batches comfortably under 64K, and the 16-bit index
buffer was the *native, fast* path — using `short` indices was a deliberate performance choice,
not an oversight. The assumption "a sorting batch never exceeds 16 bits" was simply baked in
everywhere and never written down, because on the platform it shipped on it was always true.

The port broke that invariant. At the real device resolution (the Android build renders at native
resolution — see `GlobalData.cpp`), with dense particle systems and smudge/heat-haze effects
layered across the shell map and gameplay, a *single* accumulation window could and did push the
running vertex or index total past 65535 before anything flushed. Nothing in the sorting path
noticed.

## Root cause (allocation truncates to 16-bit, fill uses 32-bit → heap corruption)

The counts that drive accumulation — `overlapping_vertex_count`, `overlapping_polygon_count`,
`state->vertex_count` — are wide (32-bit) integers. The *buffer allocation* is not. When the pool
flushed, the DX8 dynamic-VB allocation took the vertex total through an **unsigned-short**
parameter. So the two halves of the flush disagreed about how big the batch was:

```
requested vertices : N        (32-bit running total, e.g. 70000)
allocated slots    : N mod 65536   (unsigned-short truncation, e.g. 4464)
filled slots       : N        (fill loop memcpy's the full 32-bit total, 70000)
```

The allocation truncated `N` to `N & 0xFFFF`. The fill loop then walked the *full 32-bit* vertex
total and `memcpy`'d every vertex into a buffer sized for the wrapped-down remainder. Concretely:
ask for 70000 vertices, get storage for 4464, then write 70000. The extra ~65000 vertices land
**past the end of the buffer**.

On DXVK that buffer is a **host-visible, mapped allocation** — a real block of CPU-addressable
memory handed back by DXVK's own allocator, sitting in the process heap next to other live
allocations and the allocator's own metadata. Writing 70000 vertices into a 4464-vertex block
does not fault at the write — the pages immediately after it are mapped and writable (they belong
to *other* allocations). The `memcpy` silently overwrites whatever was there: adjacent objects,
free-list links, chunk headers. The heap is now corrupt, and the renderer returns as if nothing
happened.

The in-code comment on the fix states the mechanism directly:

> `// Exceeding that truncated the DX8 dynamic VB allocation (unsigned short)`
> `// while the fill loop still memcpy'd the full 32-bit vertex total, running`
> `// off the end of the DXVK-mapped buffer and corrupting the allocator heap`
> `// (SIGSEGV 0x2000000001 after 30-70s of particle/smudge rendering).`

There are actually *two* 16-bit ceilings, and the fix respects both — but they are not equally
dangerous:

1. **Vertices (the heap-corruption path).** The dynamic VB is allocated with a 16-bit count, so
   `overlapping_vertex_count + state->vertex_count` must stay ≤ 65535. This is the ceiling whose
   crossing truncated the allocation while the fill wrote the full 32-bit total — the out-of-bounds
   write that corrupted the heap.
2. **Indices (already chunk-split).** Each triangle contributes three 16-bit indices into the
   `ShortVectorIStruct` sorted-index storage, so `(overlapping_polygon_count + state->polygon_count) * 3`
   — the index count, not the polygon count — must stay ≤ 65535. The index *emission* was already
   protected in the source by a separate, pre-existing chunk-splitting fix that walks the sorted
   indices in `DynamicIBAccessClass` chunks bounded to `chunkCount * 3 ≤ 65535`. So crossing the raw
   index ceiling produced wrapped/garbage `unsigned short` indices — a visual defect — rather than
   the heap-corrupting write.

So the crash was specifically the **vertex** ceiling. The new early-flush guard still bounds the
index total as well: that is cheap belt-and-suspenders which also keeps each flushed batch
internally coherent (one accumulation window, one consistent set of counts) instead of relying
solely on the downstream chunk-split. A scene heavy in tiny particle quads tends to press the
*index* total first (many triangles, modest vertices); large translucent sheets press the *vertex*
total first — the guard closes the batch on whichever comes first.

## Why the crash is delayed and looked non-deterministic

Heap corruption decouples the *cause* (the bad write) from the *effect* (the fault) in both time
and space, which is exactly why this bug wore a timer instead of a stack trace.

- **The bad write doesn't fault.** The overrun writes into mapped, writable memory that belongs
  to neighboring allocations. There is no page fault at the moment of corruption — the damage is
  latent, recorded silently into the heap.
- **The fault happens on the next traversal.** The process only crashes when the allocator later
  *reads* a field the overrun clobbered — the next `malloc`/`free` that walks a corrupted
  free-list link, or a destructor that follows a mangled pointer. That is what
  `SIGSEGV 0x2000000001` is: a dereference of a bookkeeping word that used to be a valid pointer
  and is now garbage.
- **The delay is a function of allocator churn, not wall-clock time.** How long between the
  corrupting write and the fatal read depends on *which* neighbor got clobbered and *when* that
  region is next touched — which depends on allocation patterns that vary run to run. Hence the
  **30-70s** spread rather than a fixed deadline: it is "however long until the heap next steps on
  the damaged spot," not "N seconds after launch."
- **The backtrace lies.** Whoever trips the corrupted metadata owns the crash, so the stack
  points at innocent allocator code — never at `Flush_Sorting_Pool`. Standard debugging
  ("what's on the stack?") actively misleads here; the writer is already off the stack.

This is the general reason heap-corruption bugs are so expensive: the tools show you the victim,
not the perpetrator, and the delay hides the correlation with the rendering load that actually
caused it. The only reliable tell was statistical — it always happened under sustained
transparency, and the more particles on screen, the sooner.

## Why it surfaced on Android/DXVK

The latent bug is **platform-agnostic** — the 16-bit assumption lives in engine code
(`sortingrenderer.cpp`), not in any Android- or DXVK-specific file. So why did decades of desktop
play never surface it, while Android hit it in under a minute?

Because whether an out-of-bounds *write* becomes an observable *crash* depends entirely on **what
lives immediately after the buffer**, and that layout is a property of the allocator and the
buffer model — not of the buggy code. The mechanism below is an informed inference from how the two
buffer models allocate, not something instrumented in this codebase:

- **On the original DX8 path**, dynamic vertex buffers are driver/GPU-managed. The runtime and
  driver over-allocate and page-round, buffers live in memory the CPU heap doesn't reuse, and the
  discard/no-overwrite lifecycle means the tail of a mis-sized write frequently landed in slack,
  padding, or memory that was about to be thrown away. The overrun was *there* — but it usually
  hit nothing the process would ever read again. Latent, invisible, for years. (And in practice
  desktop content rarely crossed 64K in a single sorting batch to begin with.)
- **On DXVK's mapped-buffer model**, the dynamic VB is a tight, host-visible allocation carved
  from DXVK's own heap and packed next to other live allocations and allocator metadata. There is
  no comfortable slack after it. The same tail write that was harmless on the driver-managed path
  now lands squarely in the CRT/allocator heap — so the identical bug, on the identical engine
  code, converts from "silent no-op" to "reliable heap corruption."

Add native-resolution rendering and dense mobile-era particle/smudge effects — enough geometry to
actually cross 65535 in one accumulation window — and the platform that had *hidden* the bug for
its whole life was also the one that *detonated* it. This is a recurring theme in the port: DXVK
faithfully doing exactly what D3D asks turns previously-benign engine assumptions into crashes,
because DXVK's memory layout is tighter and more honest than the original driver's.

## The fix (flush early to stay within 65535)

The correct behavior is to never let a single batch exceed what a 16-bit buffer can hold. Since
`Flush_Sorting_Pool` already draws and resets the pool, the fix is to flush *before* accepting a
node that would push either total over the line. One guard at the top of
`sortingrenderer.cpp:SortingRendererClass::Insert_To_Sorting_Pool`:

```cpp
// @port Android @bugfix The sorting dynamic vertex/index buffers and the
// sorted triangle indices (ShortVectorIStruct) are all 16-bit, so a single
// Flush_Sorting_Pool cannot hold more than 65535 vertices / 65535 indices.
// Exceeding that truncated the DX8 dynamic VB allocation (unsigned short)
// while the fill loop still memcpy'd the full 32-bit vertex total, running
// off the end of the DXVK-mapped buffer and corrupting the allocator heap
// (SIGSEGV 0x2000000001 after 30-70s of particle/smudge rendering). Flush
// early so every batch stays within the 16-bit limits.
if (overlapping_node_count > 0 &&
    (overlapping_vertex_count + state->vertex_count > 65535u ||
     (overlapping_polygon_count + state->polygon_count) * 3u > 65535u)) {
    Flush_Sorting_Pool();
}
```

Reading it against the two ceilings from the root-cause section:

- **`overlapping_node_count > 0`** — only flush if something is actually batched. Flushing an
  empty pool would be pointless, and the guard's job is to close out the *current* batch before it
  overflows, not to interfere with a fresh, empty one.
- **`overlapping_vertex_count + state->vertex_count > 65535u`** — would this node push the vertex
  total past what the 16-bit VB can address? If so, flush the accumulated geometry now and let
  this node start a clean batch.
- **`(overlapping_polygon_count + state->polygon_count) * 3u > 65535u`** — the *index* ceiling.
  The `* 3u` converts triangles to indices, because it is the index count that the 16-bit
  `ShortVectorIStruct` storage must hold. This is easy to get wrong by comparing `polygon_count`
  directly against 65535 and silently allowing 3× too many indices — the `* 3u` is load-bearing.
- **The `u` suffixes matter.** `65535u` and `* 3u` keep the arithmetic in unsigned 32-bit space.
  The whole point is that the *running* totals are wider than 16 bits — the comparison must be
  done in the wide type so the sum `overlapping_vertex_count + state->vertex_count` and the
  product `... * 3u` don't themselves wrap before they're checked. Doing the test in a 16-bit
  type would reintroduce the exact truncation the guard exists to prevent.

After a flush the pool is empty, `overlapping_*` reset to zero, and the just-arrived node begins a
new batch that is — by construction — within limits (any single node's geometry is far below 64K).
Every batch that ever reaches allocation now honors both 16-bit ceilings, so the allocation count
and the fill count can never disagree, and the overrun can never occur.

**Cost:** under extreme transparency load the guard introduces an extra flush (one additional
draw batch) at each 64K boundary that a batch would otherwise have crossed. That is a handful of
extra draw calls in the very heaviest frames — negligible next to not corrupting the heap. It is a
strict correctness cap, not a heuristic, and it changes nothing for the common case where batches
never approach 65535.

## What it unlocked (live 3D shell map; intro movies remain a separate skip)

This was not a cosmetic stability fix — it is the crash that had been *masked* by turning features
off. The live 3D shell map (the animated 3D scene behind the main menu) is exactly the kind of
sustained, particle-and-smudge-heavy transparency load that drives the sorting pool past 64K, so
before the fix the menu itself would corrupt the heap within **30-70s** of being displayed. The
port's earlier workaround was to avoid rendering that content at all.

With the overflow fixed, that workaround is gone. `GlobalData.cpp:GlobalData::GlobalData` now
leaves the shell map **on** (`m_shellMapOn = TRUE`) and only disables the intro *movies*:

```cpp
m_shellMapName.set("Maps\\ShellMap1\\ShellMap1.map");
m_shellMapOn =TRUE;
m_playIntro = TRUE;
#if defined(__ANDROID__)
// @port Android: skip only the intro movies (FFmpeg video path). The live 3D
// shell map and full gameplay now run — the heap-corruption crash it used to
// trigger was fixed (16-bit sorting-VB overflow in SortingRendererClass, see
// sortingrenderer.cpp Insert_To_Sorting_Pool).
m_playIntro = FALSE;
#endif
```

The comment is explicit on both halves:

- **What the fix unlocked** — "The live 3D shell map and full gameplay now run — the heap-corruption
  crash it used to trigger was fixed (16-bit sorting-VB overflow in SortingRendererClass)." The
  shell map and real gameplay were gated *by this bug*, and are now enabled.
- **Why the intro is still off, and that it is unrelated** — the remaining `m_playIntro = FALSE`
  skips "only the intro movies (FFmpeg video path)." That is a **separate** concern: the intro
  movies go through the video-decode path (Bink `.bik` files decoded via FFmpeg, and their own
  bootstrap/storage plumbing), not the transparency-sorting renderer. It has nothing to do with
  the 16-bit overflow or the heap. Fixing the sorting buffer does not "un-skip" the movies, and
  skipping the movies was never what fixed the crash — the two are decoupled and the comment says
  so plainly.

In other words: the sorting-buffer fix moved the intro movies from *"disabled to dodge a heap
corruption crash"* to *"disabled for their own video-path reasons"*, and moved the shell map and
gameplay from *disabled* to *fully live*. The video path is covered separately (see the movie
bootstrap notes and `../fixes/cutscenes-video.md`); the point here is that after this fix it is
the **only** thing still turned off, and for a reason that has nothing to do with this crash.

## Gotchas & lessons

- **"Never happens on the reference platform" is not "cannot happen."** The 16-bit sorting
  assumption was true-in-practice on DX8 hardware and therefore never guarded. Porting to a
  tighter allocator turned a dormant invariant into a live crash. Latent buffer-size assumptions
  are exactly the class of bug a translation layer like DXVK exposes.
- **A heap-corruption fault address is a clue, not a location.** `SIGSEGV 0x2000000001` names the
  *victim's* dereference, never the *writer's* store. When the address isn't a value the code
  plausibly computes and the delay is variable, stop reading the backtrace and start looking for
  an out-of-bounds write upstream — ideally with an allocator guard/ASan-style tripwire, since the
  natural crash site is useless.
- **Mismatched widths across an allocate/fill boundary are silent and lethal.** The allocation
  count was 16-bit; the fill count was 32-bit; nothing in between complained. Any time a size
  crosses a type boundary — especially a narrowing one — the allocation and the fill must be
  driven by the *same* value in the *same* width, or the compiler will happily let them disagree.
- **Convert to the unit you're bounding.** The index ceiling is on *indices*, not *polygons*;
  `* 3u` is what makes the check correct. Guarding the wrong quantity (polygons vs. their indices)
  would have left a 3× hole that only the densest scenes would find — i.e., a "fix" that still
  crashes, occasionally, later.
- **Do the bounds check in the wide type.** The whole failure was a wide value truncated to 16
  bits. The guard is only correct because it compares in unsigned 32-bit (`65535u`, `* 3u`) — the
  same mistake in a narrow type would silently reintroduce the overflow.
- **When a workaround disables a feature, name the real cause in the code.** The
  `GlobalData.cpp` comment records *why* the intro was skipped and *what* actually fixed the
  underlying crash, so the next engineer doesn't re-enable the movies expecting to re-trigger a
  bug that no longer exists — or leave the shell map off out of superstition. Workarounds should
  point at their root cause so they can be retired deliberately.

## Related

- [mobile-gpu-null-guards.md](mobile-gpu-null-guards.md) — other DXVK/Mali-driven robustness
  fixes in the rendering path, where the desktop engine assumed guarantees the mobile GPU stack
  does not provide.
- [logging-storage-and-movie-bootstrap.md](logging-storage-and-movie-bootstrap.md) — the FFmpeg
  video path and its storage/bootstrap plumbing: the *separate* reason `m_playIntro = FALSE`
  remains after this crash was fixed.
- [../fixes/mali-shader-cmpbe-crash.md](../fixes/mali-shader-cmpbe-crash.md) — the port's other
  headline crash, the Mali-G57 shader-compiler fault on `gl_ClipDistance`. Same theme, different
  layer: a latent desktop assumption (D3D user clip planes) that only the mobile stack punished.
  Between them, these two were the difference between "a menu" and "you can actually play."
