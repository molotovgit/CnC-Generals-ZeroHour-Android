# Native-resolution fullscreen (and the empty resolution table that SIGSEGV'd the Options menu)

> **TL;DR** — Win32 render devices hand the engine an enumerable table of display modes.
> DXVK on an Android single-surface backend does not: the table is empty. That one fact caused
> two distinct failures. First, nothing published the real screen size, so the game rendered into
> a 4:3 backbuffer and got pillarboxed with black side bars on the device's wide landscape panel.
> Second, `W3DDisplay.cpp:buildFilteredResolutions` walked the empty table and **SIGSEGV'd
> (fault 0x568)** the instant the Options menu built its resolution list. The fix is a short chain:
> `SDL3Main.cpp:main` reads the SDL display mode and publishes it into two global ints; `GlobalData`
> and `W3DDisplay::init` consume those globals to size the backbuffer, UI and mouse identically; and
> `buildFilteredResolutions` returns exactly **one** entry — the native surface size — because that
> is the only resolution the hardware can actually present.

## Symptom

Two symptoms, one root cause.

**Pillarboxing.** On the target device — a landscape phone whose panel is far wider than 4:3 — the
rendered scene sat in a 4:3 rectangle in the middle of the screen with black bars down the left and
right. The 3D world, the 2D shell UI and the mouse cursor all agreed with each other, but all three
agreed on the *wrong* size: the engine's 4:3 default rather than the device's real extent. The
comment in `SDL3Main.cpp` names the cause directly — the scene was "being scaled from a 4:3 1024x768
buffer (which left black bars on the sides)".

**Options-menu crash.** Opening Options → Resolution — or anything that triggered the engine to
build its list of selectable resolutions — instantly killed the process with a segmentation fault.
The `W3DDisplay.cpp:buildFilteredResolutions` comment records the exact signature: the DXVK render
device "dereferences an empty table and SIGSEGVs (fault 0x568) the moment the Options menu builds
its resolution list."

These look unrelated — a cosmetic aspect-ratio bug and a hard crash — but both are downstream of the
same Win32 assumption: *there exists an enumerable table of display modes, and it has at least one
entry.* On this backend, neither half of that assumption holds.

## Why Android/DXVK has no display-mode table

The engine renders through the WW3D2 layer, which targets **Direct3D 8**. A real D3D8 driver on
Windows exposes an adapter that enumerates display modes — `GetAdapterModeCount` /
`EnumAdapterModes` hand back a list of `(width, height, format, refresh)` tuples the CRT/GPU can
switch to. The engine's `RenderDeviceDescClass::Enumerate_Resolutions()` is the WW3D2 mirror of that
list, and `buildFilteredResolutions` was written assuming it comes back populated.

Android's display model has no equivalent. An app owns a single top-level window — an
`ANativeWindow` sized by the compositor to the display (minus system bars and any cutout). There is
no mode-switching: you do not ask the panel to become 800×600; you render into the surface you were
given, and the compositor scales it. In Vulkan terms, `VkSurfaceCapabilitiesKHR.currentExtent` is
fixed to the window size and the swapchain is created at exactly that extent. There is precisely one
resolution the surface can present — its own.

DXVK sits on top of that Vulkan surface (`libdxvk_d3d8.so` → `libdxvk_d3d9.so` → Vulkan). Because
the underlying surface exposes a single fixed extent rather than a mode list, DXVK's D3D8/D3D9
adapter-mode enumeration on this configuration yields **nothing** — the WW3D2 resolution vector
behind `Enumerate_Resolutions()` is never populated. Crucially it is not *null*; it is a valid
reference to an **empty** `DynamicVectorClass`. Code that assumes at least one element and indexes
`[0]` (or iterates and reads the front) reads past the end of a zero-length array — hence the
segfault.

So the same platform property produces both symptoms:

- **No published size** → the game falls back to its compiled-in 4:3 default → pillarbox.
- **Empty mode table** → any Win32-style enumeration dereferences nothing → `0x568`.

## The resolution-publication chain

The fix threads the device's real size from SDL, where it is first known, to `W3DDisplay`, where the
backbuffer is created. Four hops.

### 1. SDL reads the display mode — `SDL3Main.cpp:main`

Right after `SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO)` and before the window is created,
the Android branch queries the primary display's current mode:

```cpp
SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
const SDL_DisplayMode* dispMode = SDL_GetCurrentDisplayMode(primaryDisplay);
if (dispMode && dispMode->w > 0 && dispMode->h > 0) {
    windowW = dispMode->w;
    windowH = dispMode->h;
    TheAndroidDisplayWidth  = windowW;
    TheAndroidDisplayHeight = windowH;
    fprintf(stderr, "INFO: Android display size %dx%d\n", windowW, windowH);
}
windowFlags |= SDL_WINDOW_FULLSCREEN;
```

`SDL_GetCurrentDisplayMode` returns the live `SDL_DisplayMode` (`w`, `h`, `pixel_density`,
`refresh_rate`); on Android SDL reports the actual usable window/screen size. The window is then
created at `windowW, windowH` — the real size, not the `1024, 768` default it was previously hard
-coded to — and the `SDL_WINDOW_FULLSCREEN` flag is OR'd in, which matches reality: an Android app
window is effectively always fullscreen. Note the guard: if the mode is missing or degenerate
(`w <= 0`), the window falls back to the `1024×768` default and the globals stay `0`.

### 2. The publication globals — `SDL3Main.cpp`

The two values are stored in file-scope globals declared next to `ApplicationHWnd`:

```cpp
// @port Android: real device resolution, published for GlobalData so the game
// renders fullscreen at the screen size instead of a letterboxed 4:3 backbuffer.
int TheAndroidDisplayWidth = 0;
int TheAndroidDisplayHeight = 0;
```

They are initialised to `0` and only overwritten when a valid display mode is read. `0` is the
"not set / fall back to default" sentinel — every consumer checks `> 0` before trusting them.
Consumers pull them in with a local `extern int` declaration rather than a shared header, so no
engine module needs to include an SDL-platform header to read the size.

### 3. GlobalData seeds the resolution — `GlobalData.cpp:GlobalData`

The `GlobalData` constructor sets the engine's default resolution to the 4:3 constants, then, on
Android, overrides them with the published device size:

```cpp
m_xResolution = DEFAULT_DISPLAY_WIDTH;
m_yResolution = DEFAULT_DISPLAY_HEIGHT;
#if defined(__ANDROID__)
    extern int TheAndroidDisplayWidth;
    extern int TheAndroidDisplayHeight;
    if (TheAndroidDisplayWidth > 0 && TheAndroidDisplayHeight > 0) {
        m_xResolution = TheAndroidDisplayWidth;
        m_yResolution = TheAndroidDisplayHeight;
    }
#endif
```

This makes the *very first* value any code reads out of `TheGlobalData->m_xResolution` the device
resolution, not the 4:3 default. It matters because a lot of engine bring-up reads that field before
`W3DDisplay` ever runs.

### 4. W3DDisplay re-asserts and applies it — `W3DDisplay.cpp:W3DDisplay::init`

When the display device initialises (`case 0` in `W3DDisplay::init`), the globals are read once more
and written straight into `TheWritableGlobalData` immediately before the width/height are latched:

```cpp
extern int TheAndroidDisplayWidth;
extern int TheAndroidDisplayHeight;
if (TheAndroidDisplayWidth > 0 && TheAndroidDisplayHeight > 0)
{
    TheWritableGlobalData->m_xResolution = TheAndroidDisplayWidth;
    TheWritableGlobalData->m_yResolution = TheAndroidDisplayHeight;
}
// set our default width and height and bit depth
setWidth( TheGlobalData->m_xResolution );
setHeight( TheGlobalData->m_yResolution );
```

`setWidth`/`setHeight` here size the 3D backbuffer; the same `m_xResolution/m_yResolution` also drive
the 2D UI projection and the mouse coordinate space. Writing all three from one source is exactly why
the comment stresses that this "keeps the 3D backbuffer, the 2D UI and the mouse all in agreement."

### The ordering guarantee

Why is it safe for `W3DDisplay::init` to assume the globals are already set? Because the write and the
read are separated by a strict, single-threaded happens-before edge:

- `SDL3Main.cpp:main` assigns `TheAndroidDisplayWidth/Height` **before** it calls the engine's
  `GameMain()`. The window has been created by this point; the assignment is upstream of all engine
  code.
- `W3DDisplay::init` runs deep inside `GameMain()`, during render-device bring-up — "well after the
  SDL window is created," as the in-code comment puts it.

There is no second thread and no reordering across the `GameMain()` call boundary, so by the time
`W3DDisplay::init` executes, the assignment in `main` has already happened. The `> 0` guard is not
there to defend against a race — it defends against the one legitimate case where SDL never gave us a
valid mode, in which the globals remain `0` and every consumer cleanly falls back to `DEFAULT_*`.

The same edge covers `GlobalData::GlobalData`: it is constructed during engine startup, after `main`
has published the globals, so it too sees the real size. In short, publish-in-`main`, read-via-extern
turns a cross-module data dependency into a trivially correct sequential one.

## The Options-menu crash and the single-resolution fix

Sizing the backbuffer fixes the pillarbox, but it does nothing for the Options menu — that path calls
`buildFilteredResolutions`, which on desktop reads the enumerated table:

```cpp
const RenderDeviceDescClass &devDesc = WW3D::Get_Render_Device_Desc(0);
const DynamicVectorClass<ResolutionDescClass> &resolutions = devDesc.Enumerate_Resolutions();
```

On Android that `resolutions` reference is empty, and the subsequent filtering/indexing walks off the
end → `0x568`. The fix short-circuits the whole Win32 path on Android and synthesises a
single-entry list from the native size:

```cpp
int nativeW = 0, nativeH = 0; float density = 1.0f;
if (!DX8Wrapper::GetNativeDisplaySize(nativeW, nativeH, density) || nativeW <= 0 || nativeH <= 0) {
    nativeW = DEFAULT_DISPLAY_WIDTH; nativeH = DEFAULT_DISPLAY_HEIGHT;
}
s_filteredResolutions.push_back({ nativeW, nativeH, DEFAULT_DISPLAY_BIT_DEPTH });
s_filteredDirty = false;
return;
```

Three things worth calling out:

- **It never touches `Enumerate_Resolutions()`.** The `return` fires before the empty table is ever
  dereferenced, so the crash is structurally impossible on Android, not merely papered over.
- **One entry, and it is the truthful one.** The device can present exactly one resolution — its
  native surface extent. A longer list would be fiction: any non-native entry would force the
  compositor to scale, reintroducing the very pillarbox/quality loss we just eliminated. Exposing the
  single native mode makes the Options menu *honest* — you cannot pick a mode the surface cannot
  provide. That is why one resolution is the correct fix, not a stopgap.
- **It reads the size through `DX8Wrapper::GetNativeDisplaySize`, not the SDL globals.** Both
  ultimately reflect the same fixed surface extent, and both fall back to `DEFAULT_*` on failure, so
  the resolution shown in the menu and the resolution the backbuffer runs at agree. (See Gotchas for
  why having two accessors for the same fact is a mild hazard.)

## Why desktop is unaffected

Every change above lives inside `#if defined(__ANDROID__)`. On Windows, Linux and macOS the blocks
compile out entirely, leaving the original behaviour byte-for-byte:

- `SDL3Main.cpp:main` creates the window at its `1024, 768` default with no `SDL_WINDOW_FULLSCREEN`
  flag; `TheAndroidDisplayWidth/Height` do not exist in the build.
- `GlobalData::GlobalData` leaves `m_xResolution/m_yResolution` at `DEFAULT_DISPLAY_WIDTH/HEIGHT`,
  still overridable by `Options.ini` exactly as before.
- `W3DDisplay::init` performs no forced device-size write.
- `buildFilteredResolutions` runs the real `Get_Render_Device_Desc(0).Enumerate_Resolutions()` path.
  On a desktop windowing system the adapter — whether a genuine D3D8 driver or desktop DXVK — *does*
  enumerate the monitor's supported modes, so the table is populated and the Options menu builds a
  full multi-entry list with no crash.

The desktop assumption "there is a non-empty mode table" is correct on desktop; the port simply
declines to make that assumption on the one backend where it is false. Nothing about the fix narrows
or reshapes the desktop code path.

## Gotchas & lessons

- **An empty container is not a null container.** `Enumerate_Resolutions()` returns a valid reference
  to a zero-length vector, so a null check sails right past it. The Win32-era invariant "there's
  always at least one display mode" is what actually broke; port such code by checking *size*, or by
  not calling the enumerator at all where the platform can't populate it.
- **Publish once, high up; read via `extern`, low down.** Doing the SDL query in `main` before
  `GameMain()` turns a cross-module dependency (SDL layer → GameEngine → GameEngineDevice) into a
  plain sequential happens-before with no headers, no init-order fiasco, and no locking. The `> 0`
  sentinel is the entire contract.
- **Belt and suspenders on `m_xResolution` is deliberate.** The size is written in *two* places —
  `GlobalData::GlobalData` and `W3DDisplay::init`. The constructor seeds it so early readers see the
  device size; `W3DDisplay::init` re-asserts it immediately before `setWidth/setHeight` because INI /
  options loading between construction and device bring-up can reset the field to a default. Set it in
  only one place and a later `Options.ini` load can silently clobber it back to 4:3.
- **Two accessors for one fact — keep their fallbacks identical.** The applied size comes from the SDL
  globals; the Options list comes from `DX8Wrapper::GetNativeDisplaySize`. They must not diverge, so
  both fall back to `DEFAULT_DISPLAY_WIDTH/HEIGHT`. If you ever change one fallback, change the other,
  or the menu will advertise a resolution the backbuffer isn't using.
- **Fullscreen flag and window size must be set together.** Sizing the SDL window to the display mode
  *and* passing `SDL_WINDOW_FULLSCREEN` keeps the requested window extent and the actual surface
  extent in lockstep; setting one without the other invites a mismatch on some SDL video backends.
- **Guard every read; degrade, don't divide by zero.** A bogus or zero display mode leaves the globals
  at `0`, and every consumer's `> 0` check routes back to the old 4:3 default — an ugly render, never
  a `0×0` swapchain or a divide-by-zero in the projection math.
- **A residual side bar after this fix is a different layer.** Once the engine renders at native
  resolution, a phone with a punch-hole/notch **display cutout** can still show a black bar on one
  short edge in landscape. That is Android reserving the cutout region, not the engine letterboxing —
  it is fixed purely in the app theme with
  `android:windowLayoutInDisplayCutoutMode=shortEdges`, no engine or SDL code involved. Do not chase
  it in render code. Full detail in the fix note linked below.

## Related

- [`../fixes/fullscreen-native-resolution.md`](../fixes/fullscreen-native-resolution.md) — the
  concise fix note, including the `styles.xml` `windowLayoutInDisplayCutoutMode=shortEdges` app-theme
  change that removes the display-cutout side bar (the separate follow-up referenced above).
- [`build-shared-library-and-jni-entrypoint.md`](build-shared-library-and-jni-entrypoint.md) — how
  `SDL3Main.cpp:main` becomes the app's real entry point via `SDL_main`/JNI, i.e. why the code that
  publishes `TheAndroidDisplayWidth/Height` runs before any engine code.
- [`dxvk-texture-format-capabilities.md`](dxvk-texture-format-capabilities.md) — the sibling
  "Win32 caps table is empty/wrong on DXVK" discovery, where the backbuffer format doesn't map to a
  legacy `WW3DFormat` and the capabilities query reports the wrong answer — same class of bug as the
  empty resolution table, different table.
