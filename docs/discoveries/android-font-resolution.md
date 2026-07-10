# Rendering text on Android: FreeType without a Fontconfig config, resolving to `/system/fonts`

> The cross-platform port draws all text through FreeType, and FreeType needs a concrete font *file*. On desktop, Fontconfig turns a requested family ("Times", "Courier") into a file on disk. Android ships no Fontconfig config file, so `FcInitLoadConfigAndFonts()` fails with *"Cannot load default config file"*, the resolver returns nothing, FreeType has no face to load, and every menu, label, and HUD element renders with **no visible text** — silently, no crash. The fix is two moves: (1) compile the FreeType text path *in* on Android by adding `Android` to the `SAGE_USE_FREETYPE` and link-library lists in `WW3D2/CMakeLists.txt`; (2) short-circuit `render2dsentence.cpp:FontCharsClass::Locate_Font_FontConfig` on Android so it maps the engine's requested families straight to files in `/system/fonts` — `Roboto-Regular.ttf` by default, `NotoSerif-Regular.ttf` for Times/Serif, `DroidSansMono.ttf` for Courier/Mono — never touching Fontconfig at runtime.

The original Windows game drew text through GDI/DirectDraw. Neither exists on Android, so the port relies on the same FreeType path the Linux and macOS builds use. That path has two independent prerequisites — the text stack must be *compiled* (a build concern) and, once compiled, it must be able to *find a font file* (a runtime concern). Both were missing on Android for different reasons, and both fail invisibly. This document traces each.

## Symptom (no text)

There is no error dialog and no crash. The engine boots, the shell map renders, buttons and panels draw their backgrounds — but every string is blank. Menu items are empty rectangles, the resource counters show nothing, tooltips are void. The game is fully playable by muscle memory and completely unusable in practice.

Two distinct failures produce the identical symptom, and they stack:

1. **Build side.** Before the CMake change, Android did not define `SAGE_USE_FREETYPE`, so the FreeType branch of the WW3D2 text renderer was never compiled into the Android binary at all. There was no cross-platform glyph path, and no GDI to fall back to. Text simply had no renderer.
2. **Runtime side.** Once FreeType *is* compiled in, the renderer still has to resolve a family name to a `.ttf` before it can rasterize. On desktop that resolution is Fontconfig's job. On Android, `FcInitLoadConfigAndFonts()` returns `NULL` because there is no config for it to load — so `Locate_Font_FontConfig` hands FreeType no path, `FT_New_Face` is never given a file, and no glyph is ever rasterized.

| Stage | What is missing | Where it fails | Visible result |
| --- | --- | --- | --- |
| Build | `SAGE_USE_FREETYPE` undefined for Android | Compile time — FreeType text path not emitted | Blank text |
| Runtime | Fontconfig default config absent on device | `FcInitLoadConfigAndFonts()` returns `NULL` | Blank text |
| Both fixed | — | — | Text renders |

Because both stages fail with the same visible result (blank text), it is easy to fix one and think the other is still broken. They must be understood — and fixed — separately. The build change alone gets you a compiled-but-fontless renderer; the runtime change alone is dead code in a binary that never compiled the text path. Only the pair produces visible glyphs.

## Build side: enabling the FreeType path on Android

The WW3D2 rendering library gates its FreeType text path behind the `SAGE_USE_FREETYPE` compile definition, and that definition was originally set only for desktop Unix targets. In `Core/Libraries/Source/WWVegas/WW3D2/CMakeLists.txt`:

```cmake
# GeneralsX @build fbraz 11/02/2026 BenderAI - Enable FreeType text rendering on Linux/macOS
target_compile_definitions(corei_ww3d2 INTERFACE
-    $<$<PLATFORM_ID:Linux,Darwin>:SAGE_USE_FREETYPE>
+    $<$<PLATFORM_ID:Linux,Darwin,Android>:SAGE_USE_FREETYPE>
)
```

The comment predates the Android work — the FreeType path was first stood up for Linux and macOS. The port's build change is a single token: add `Android` to the `PLATFORM_ID` generator expression so the same `SAGE_USE_FREETYPE` code compiles for the NDK toolchain. (The NDK's `CMAKE_SYSTEM_NAME` is `Android`, so `$<PLATFORM_ID:Android>` evaluates true for the arm64 build.)

`SAGE_USE_FREETYPE` is a *source-level* switch, not just a link flag. It gates the compilation of the entire FreeType branch of the WW3D2 text renderer — the glyph-rasterization code and `Locate_Font_FontConfig` itself. Without the definition, that function is not merely inert on Android; it is not in the binary at all, and neither is any call site that would resolve a font. This is why the build change is a genuine prerequisite rather than an optimization: the runtime fix below lives inside code that only exists when `SAGE_USE_FREETYPE` is defined. Flip the definition on, and the same `#if defined(__ANDROID__)` short-circuit that the port adds becomes reachable.

Turning the definition on is not enough — the code it unlocks calls into FreeType and Fontconfig, so those libraries must be on the link line. The port mirrors the existing Linux/Darwin arms exactly:

```cmake
target_link_libraries(corei_ww3d2 INTERFACE
    core_wwmath
    $<$<PLATFORM_ID:Linux>:Freetype::Freetype>
    $<$<PLATFORM_ID:Darwin>:Freetype::Freetype>
+    $<$<PLATFORM_ID:Android>:Freetype::Freetype>
    $<$<PLATFORM_ID:Linux>:Fontconfig::Fontconfig>
    $<$<PLATFORM_ID:Darwin>:Fontconfig::Fontconfig>
+    $<$<PLATFORM_ID:Android>:Fontconfig::Fontconfig>
    $<$<PLATFORM_ID:Darwin>:Iconv::Iconv>
+    $<$<PLATFORM_ID:Android>:Iconv::Iconv>
)
```

Three libraries, three added lines — one per platform arm, deliberately symmetric with what already existed. `Freetype::Freetype` is the load-bearing one: it is the actual rasterizer, and without it the text path does not link. The `Fontconfig::Fontconfig` and `Iconv::Iconv` additions are less obvious, because the runtime resolver bypasses Fontconfig entirely on Android (below). They are on the link line for two reasons:

- **The translation unit is shared.** `render2dsentence.cpp` is one file compiled for every platform. Its non-Android resolver (the `#else` branch of `Locate_Font_FontConfig`) is written against Fontconfig's API and pulls in Fontconfig's header at file scope. `Fontconfig::Fontconfig` is an imported CMake target that carries both the include directories and the library, so naming it on the Android link keeps that shared source compiling with the same dependency closure as Linux and macOS — even though the Android code path never *calls* a Fontconfig function.
- **Iconv is Fontconfig's transitive dependency.** Note the asymmetry already in the file: Linux links `Fontconfig::Fontconfig` but *not* `Iconv::Iconv`, while Darwin links both. That is because glibc bundles the `iconv` charset-conversion symbols Fontconfig needs, so Linux gets them for free; macOS does not, so it names `libiconv` explicitly. Android's bionic libc is like macOS here, not like glibc — so the Android arm follows the Darwin pattern and links `Iconv::Iconv` alongside `Fontconfig::Fontconfig`. The addition is dictated by build symmetry with the platform that has the same libc shape, not by anything the Android runtime does.

The guiding principle is the one the rest of the port follows: extend the existing platform lists, do not fork them. Every Android line sits next to the Linux/Darwin line it mirrors, so a future edit to the FreeType wiring is hard to apply to two platforms and miss the third.

## Runtime discovery: Fontconfig has no default config on Android

FreeType does not know what "Times New Roman" is. It is a rasterizer: you hand it a file path and a face index, it gives you glyph bitmaps. Family-name resolution — mapping a human font name to a file on disk — is a separate concern, and on Linux/macOS the engine delegates it to Fontconfig inside `render2dsentence.cpp:FontCharsClass::Locate_Font_FontConfig`.

The desktop implementation (the `#else` branch, unchanged by this port) does the standard Fontconfig dance: initialize the library, build a pattern from the requested family, let Fontconfig match it against the installed font set, and read back the matched file path:

```cpp
	//
	//	Initialize Fontconfig library
	//
	...
	FcConfigDestroy( config );

	return font_path;
```

It helps to be precise about what Fontconfig *is* on Android. Fontconfig is not part of the platform — Android does not ship `libfontconfig` for apps, and nothing in the system depends on it. The only reason the symbols exist in this build is that the port compiles and links its own copy (that is what `Fontconfig::Fontconfig` on the link line resolves to). So the desktop resolver is calling into a library that runs perfectly well as code but is a foreigner on the device: it looks for a Fontconfig-style config in the Fontconfig-standard locations, and those locations are empty because no Android component ever populated them. The library is present; its *environment* is not. That is the precise shape of the failure, and it is also why the runtime fix is a bypass rather than a config file — there is no supported place to install a `fonts.conf` that the platform would preserve, so the port stops depending on one.

`FcInitLoadConfigAndFonts()` is the first call, and it is where Android falls off a cliff. That function loads Fontconfig's *default configuration* — on a normal Linux system, `/etc/fonts/fonts.conf` and the directory/match rules it references. That config is what tells Fontconfig which directories to scan and how to resolve generic families. **Android ships no such file.** Android is not a Fontconfig platform: its own text stack (Minikin/Skia in the framework) reads a completely different, Android-specific file — `/system/etc/fonts.xml` — that `libfontconfig` cannot consume. There is no `/etc/fonts/fonts.conf` anywhere on the device.

So on Android, `FcInitLoadConfigAndFonts()` fails and logs *"Cannot load default config file"*. With no config, there is no font set to match against; the resolver returns without a usable path; FreeType is never given a file; no glyph is rasterized. That is the runtime half of the blank-text symptom, and it persists even after the build change compiles the FreeType path in.

The two resolution paths diverge at exactly one step — where the family name becomes a file path:

```
Desktop:  "Times New Roman"  →  Fontconfig (reads /etc/fonts/fonts.conf,
                                 matches installed set)  →  /usr/share/fonts/.../times.ttf  →  FT_New_Face  ✓

Android:  "Times New Roman"  →  FcInitLoadConfigAndFonts()  →  NULL  ✗  →  (no path)  →  no glyphs
  (fix)   "Times New Roman"  →  strstr("Times")  →  /system/fonts/NotoSerif-Regular.ttf  →  FT_New_Face  ✓
```

Everything downstream of the resolved path — FreeType rasterization, glyph caching, the WW3D2 sentence renderer — is already platform-agnostic and works unchanged. The port only has to make the one divergent step produce a valid file path.

## The fix: resolve families directly to `/system/fonts`

The port sidesteps Fontconfig at runtime. Since the device's fonts live at a known, stable location, the Android branch of `render2dsentence.cpp:FontCharsClass::Locate_Font_FontConfig` skips the config machinery and returns a concrete `/system/fonts` path directly:

```cpp
#if defined(__ANDROID__)
	//
	// @port Android: Android ships no fontconfig config file, so
	// FcInitLoadConfigAndFonts() fails ("Cannot load default config file")
	// and no text renders. Resolve font families directly to the phone's
	// built-in system fonts in /system/fonts instead.
	//
	const char *path = "/system/fonts/Roboto-Regular.ttf"; // sans-serif (Arial substitute)
	if ( font_name != nullptr ) {
		if ( strstr( font_name, "Times" ) != nullptr || strstr( font_name, "Serif" ) != nullptr )
			path = "/system/fonts/NotoSerif-Regular.ttf";
		else if ( strstr( font_name, "Courier" ) != nullptr || strstr( font_name, "Mono" ) != nullptr )
			path = "/system/fonts/DroidSansMono.ttf";
	}
	FreetypeFontPath = path;
	return FreetypeFontPath;
#else
	// ... original Fontconfig implementation ...
#endif
```

`/system/fonts` is the right target because it is the one font location Android *guarantees*:

- **It is part of the read-only system image.** `/system` is a core AOSP partition mounted at boot on every certified Android device. `/system/fonts` is where AOSP places the framework's bundled fonts — the framework itself loads from there (via `/system/etc/fonts.xml`), so the directory is always present and populated. There is no code path where a booted Android device lacks it.
- **It needs no permission and is exempt from scoped storage.** The files are world-readable system assets, not user data. An app `open()`s them by absolute path with no runtime permission, no MediaStore, no Storage Access Framework — exactly what FreeType's `FT_New_Face` wants.
- **It costs the APK nothing.** The alternative — bundling a `.ttf` in the app and extracting it to a readable path at startup — adds font bytes to the package and an extraction step. Reading the platform fonts ships zero font assets, which fits a port that deliberately ships no game assets at all.

`Roboto-Regular.ttf` is the natural default: Roboto has been Android's system sans-serif since Android 4.0, so it is the most reliably-present file of the three and stands in for the engine's default Arial-like family. `NotoSerif-Regular.ttf` and `DroidSansMono.ttf` are likewise AOSP-bundled files covering the serif and monospace cases.

Assigning through `FreetypeFontPath` and returning it keeps the Android branch's contract identical to the desktop branch — the caller receives a `const char *` file path either way and never learns whether Fontconfig or the short-circuit produced it. The caller then hands that path to FreeType to open the face, so the substring resolution feeds straight into face loading with no intermediate lookup service. The whole Android branch is also cheap and side-effect-free: no library init, no config parse, no filesystem scan — a couple of `strstr` calls and a pointer assignment, run once per `FontCharsClass` when its face is first needed. The desktop branch, by contrast, spins up and tears down the Fontconfig library (`FcConfigDestroy( config )`) on every resolution. Dropping that machinery is a small latency win on top of being the thing that makes text appear at all.

## The family-matching heuristic

Desktop Fontconfig does fuzzy, rule-driven matching over the whole installed font set. The Android replacement is three lines of `strstr`: a sans-serif default, overridden to serif or to monospace by a substring test on the requested family name.

```cpp
	const char *path = "/system/fonts/Roboto-Regular.ttf";     // default: sans-serif
	if ( strstr( font_name, "Times" ) || strstr( font_name, "Serif" ) )
		path = "/system/fonts/NotoSerif-Regular.ttf";          // serif
	else if ( strstr( font_name, "Courier" ) || strstr( font_name, "Mono" ) )
		path = "/system/fonts/DroidSansMono.ttf";              // monospace
```

The full mapping is three buckets:

| Requested family contains | Class | Resolved file |
| --- | --- | --- |
| `"Times"` or `"Serif"` | serif | `/system/fonts/NotoSerif-Regular.ttf` |
| `"Courier"` or `"Mono"` | monospace | `/system/fonts/DroidSansMono.ttf` |
| anything else (default) | sans-serif | `/system/fonts/Roboto-Regular.ttf` |

Substring rather than exact match is the key design choice. The engine does not request bare generic names — it asks for concrete families like *"Times New Roman"* or *"Courier New"* pulled from the UI/`.ini` font definitions. An exact-match table would have to enumerate every spelling the engine might use. A substring test on the distinctive token instead captures the whole *class*:

- `"Times"` catches "Times", "Times New Roman", and similar; `"Serif"` catches generic serif requests — either routes to `NotoSerif-Regular.ttf`.
- `"Courier"` catches "Courier", "Courier New"; `"Mono"` catches generic monospace requests — either routes to `DroidSansMono.ttf`.
- Anything else — the overwhelmingly common case, the sans-serif UI font — falls through to `Roboto-Regular.ttf`.

Evaluation order matters and is correct: sans-serif is the default, and only an explicit serif or mono token overrides it, so an unrecognized name can never be misclassified — it degrades to the safe, always-present default. All three targets are the base `-Regular` face; the heuristic resolves the *family class* only. Weight and slant are the caller's concern (and FreeType can synthesize emboldening/obliquing), so the mapping intentionally does not try to pick bold or italic files here.

This is a coarse map — three buckets versus Fontconfig's full matcher — but it covers exactly the families a Latin-script RTS UI actually requests, which is all this port needs.

## Why desktop is unaffected

Both halves of the fix are strictly additive and fully guarded, so the shared source tree still builds an unchanged Windows, Linux, and macOS game:

- **CMake.** Every change *adds* `Android` to a list that already contained Linux and/or Darwin; not one existing arm is edited. The `SAGE_USE_FREETYPE` generator expression still evaluates identically for Linux and Darwin, and Windows — which never defined it and uses its native GDI text path — is untouched. The new `$<$<PLATFORM_ID:Android>:...>` arms are inert on every non-Android configure.
- **Source.** The entire runtime change lives behind `#if defined(__ANDROID__)`, and the `#else` is the verbatim original Fontconfig implementation. On Linux and macOS the compiler sees byte-for-byte the pre-port resolver, still calling `FcInitLoadConfigAndFonts()` and returning the Fontconfig-matched path. No desktop behavior changes.

A desktop developer can build and run with no awareness that the Android font path exists — the same property the rest of this port maintains.

## Gotchas & lessons (system-font availability across OEMs)

- **`/system/fonts` filenames are AOSP conventions, not a frozen contract.** The directory is guaranteed to exist and to be populated, but the *specific* filenames are not guaranteed identical across every vendor and Android version. `Roboto-Regular.ttf` is the safest bet — it is the framework default and present essentially everywhere. `DroidSansMono.ttf` is an older AOSP name that some newer builds have dropped or renamed in favor of other monospace fonts, and serif coverage varies. The hardcoded paths are a pragmatic bet on the common case.
- **A missing file re-creates the blank-text bug for that family.** The current code does not `stat()` the path before handing it to FreeType. If a device lacks, say, `DroidSansMono.ttf`, `FT_New_Face` fails on that path and monospace text renders blank again — the exact symptom this fix set out to kill, now scoped to one family. The robustness lesson: check the file exists and fall back to `Roboto-Regular.ttf` (or enumerate `/system/fonts`) when it does not, so an unusual OEM image degrades to sans-serif instead of to nothing.
- **The default is the resilient path.** Because unrecognized families fall through to Roboto, the UI's primary sans-serif text is the least likely to break. Only requests that deliberately steer to serif/mono ride on the two less-guaranteed files — which is the right risk to take, since the bulk of the game's text is the default family.
- **Reading system fonts needs no permission — leaning on the platform is deliberate.** No manifest permission, no scoped-storage dance. The fully-robust alternative is to bundle a fallback `.ttf` in the APK and extract it, guaranteeing the file regardless of OEM — but that costs package size and an extraction step, and this port avoids shipping font assets on principle. The `/system/fonts` bet trades a small portability risk for zero shipped bytes.
- **Latin-script assumption.** Roboto / NotoSerif / DroidSansMono cover the Latin glyphs the shipped strings use. A CJK or other non-Latin localization would need a wider font (e.g. a Noto CJK face from `/system/fonts`) and a broader heuristic; the three-bucket map is sized to the current content, not to arbitrary locales.
- **Diagnosing it is a two-line `logcat` check.** The failure is otherwise mute. If text is blank, look for Fontconfig's *"Cannot load default config file"* in `adb logcat` — that confirms the runtime half (the resolver reached the desktop branch and Fontconfig choked). If you see nothing font-related at all, suspect the build half: `SAGE_USE_FREETYPE` was never defined, so no resolver ran. The two log signatures tell you which stage to fix.
- **FreeType resolves files, Fontconfig resolves names — keep them separate in your head.** The build change and the runtime change are independent, and each fails to blank text on its own. When bringing up text on a new SDL/FreeType Android target, confirm `SAGE_USE_FREETYPE` is actually compiled in *and* that the resolver returns a path that exists on the device — fixing only one leaves the identical empty-string symptom.

## Related

- [`../fixes/rendering-dxvk-mali.md`](../fixes/rendering-dxvk-mali.md) — the rendering bring-up overview; its "Font/text rendering" bullet is this document, and it sits alongside the DXVK caps and texture-format fixes that got the main menu to draw at all.
- Sibling discovery [`build-shared-library-and-jni-entrypoint.md`](build-shared-library-and-jni-entrypoint.md) — the other half of "make one Win32-shaped source tree boot on Android": producing `libmain.so` and redirecting `main` → `SDL_main` so the engine runs, the same guarded-additive pattern used here for the FreeType text path.
