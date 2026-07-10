# App-private storage, logging, and the SELinux-aware movie bootstrap

> On Android the engine boots into a hostile environment: its stdout/stderr are wired to `/dev/null`, its working directory is a read-only `/`, and nothing outside the app can write its private data dir. Four small, guarded steps at the very top of `SDL3Main.cpp:main` make the port survivable — `freopen` stderr to an app-private log and `dup2` stdout onto it; `mkdir`+`chdir` into `files/` so the engine's relative paths resolve; and a launch-time copy that pulls any `.bik` from the app's *external* dir (which `adb` can write) into *internal* `Data/Movies/` (which it cannot). Two cross-build discoveries ride along: SDL3_image's real option prefix is `SDLIMAGE_`, not `SDL3IMAGE_`, and the Bink decode path needs `ffmpeg` pulled via vcpkg.

This document covers the Android prologue of `SDL3Main.cpp:main` — everything that runs *before* the engine's `GameMain()` — plus the two `cmake`/`vcpkg` changes that make that prologue's dependencies build for `arm64-android`. All of it is guarded by `#if defined(__ANDROID__)` (source) or `if(ANDROID)` (CMake); desktop is byte-for-byte unchanged. See the sibling discovery `build-shared-library-and-jni-entrypoint.md` for how control even reaches `main` on Android (SDL's `libmain.so`/`SDL_main` bootstrap), and why the `sys/stat.h`/`dirent.h` includes used below ride in the same guarded block.

### The prologue, in order

The four steps are not independent — each depends on the one before it. Ordering is part of the correctness, not an accident of how the code reads top to bottom:

| # | Step | Depends on | Why the order matters |
| --- | --- | --- | --- |
| 1 | `freopen` stderr → `ccg_log.txt`; `dup2` stdout onto it | nothing | Must be first — every later step's diagnostics are only recoverable once logging works |
| 2 | `mkdir` + `chdir` into `files/` | step 1 (to log a failure) | The engine's relative-path data creation, and step 3's relative `mkdir`, resolve against this CWD |
| 3 | Copy external `Data/Movies/*` → internal `Data/Movies/` | steps 1 and 2 | Destination is a *relative* path — only correct because CWD is already internal `files/` |
| 4 | `GameMain()` (the engine proper) | steps 1–3 | Loads `Data/`, `*.big`, and movies relative to the CWD app-private storage |

The rest of this document walks steps 1–3, then the two build-system changes (SDL3_image options, FFmpeg via vcpkg) that let step 3 and the cutscene path actually compile and link for `arm64-android`.

## Getting a log off the device (stdout/stderr → app-private file)

On desktop the engine narrates its boot to the console via `fprintf(stderr, ...)`. On Android there is no console, and worse, an app process's native stdout and stderr are effectively connected to `/dev/null` — anything written to fd 1 or fd 2 is silently discarded unless the developer opts in through a system-wide, root/eng-only property (`log.redirect-stdio`). A stock, non-rooted, `userdebug`-or-lower device gives you nothing. A black screen and a clean exit look identical to a crash, a missing asset, and a successful-but-invisible boot.

The port's answer is the first thing `main` does on Android — redirect stderr to a real file and fold stdout into the same file:

```cpp
#if defined(__ANDROID__)
	// GeneralsX @port Android — native stdout/stderr are sent to /dev/null on Android.
	// Redirect them to an app-private log file so the boot sequence is retrievable via
	// `adb shell run-as org.generalsx.zerohour cat /data/data/org.generalsx.zerohour/ccg_log.txt`.
	{
		FILE* androidLog = freopen("/data/user/0/org.generalsx.zerohour/ccg_log.txt", "w", stderr);
		if (androidLog != nullptr) {
			setvbuf(stderr, nullptr, _IONBF, 0);
			dup2(fileno(stderr), fileno(stdout));
		}
	}
```

Reading this line by line:

- **`freopen(..., "w", stderr)`** reassigns the `stderr` stream so its underlying fd points at `ccg_log.txt` in the app's private storage. Mode `"w"` truncates on open, so the log is per-run — each launch starts a fresh file, and a previous session's log is gone. That is the right tradeoff here: you almost always want the log from *this* boot, and unbounded growth on a phone is a liability.
- **`setvbuf(stderr, nullptr, _IONBF, 0)`** makes the stream unbuffered. Every `fprintf(stderr, ...)` hits the file immediately. This is the difference between recovering the last line before a crash and losing it in a never-flushed buffer — with a segfault there is no orderly `fclose`/flush, so buffered tail output would vanish. Unbuffered stderr is standard on desktop too; here it is load-bearing.
- **`dup2(fileno(stderr), fileno(stdout))`** points fd 1 (stdout) at the *same* open file description as fd 2. Anything the engine or a dependency writes to stdout now lands in the same log, interleaved with stderr. Note that only `stderr`'s `FILE*` buffering was set to `_IONBF`; `stdout`'s `FILE*` keeps its default (typically fully buffered when not a TTY), so `printf` output can still be buffered at the C-library layer and lost on a hard crash. In practice the engine's boot diagnostics all go through `fprintf(stderr, ...)`, so this is rarely an issue — but it is why stderr, not stdout, is the one made unbuffered.
- **The `if (androidLog != nullptr)` guard.** If `freopen` fails (e.g. the directory does not exist yet), the code does not touch stdout and does not crash — it simply proceeds with the original (null) streams. Boot is never blocked on logging.

### Retrieving the log

The comment gives the exact command:

```
adb shell run-as org.generalsx.zerohour cat /data/data/org.generalsx.zerohour/ccg_log.txt
```

Two details are worth making explicit:

1. **`run-as <pkg>` is what makes this readable.** The `adb shell` user is the `shell` domain, which under SELinux cannot read another app's `app_data_file`-labeled private storage. `run-as` re-executes the `cat` inside the app's own uid and SELinux domain — allowed only because the app is `debuggable`. From inside the app domain, `ccg_log.txt` is just an ordinary file the app owns.
2. **`/data/data/...` and `/data/user/0/...` are the same file.** The code writes to `/data/user/0/org.generalsx.zerohour/ccg_log.txt`; the retrieval command reads `/data/data/org.generalsx.zerohour/ccg_log.txt`. These are not two paths — on multi-user Android, user 0's per-app data lives under `/data/user/0/<pkg>`, and `/data/data` is the legacy bind/symlink that resolves to `/data/user/0`. Same inode, either spelling works. The code uses the canonical `/data/user/0` form; humans usually type the shorter historical `/data/data` form.

Note also *where* the log sits: `/data/user/0/org.generalsx.zerohour/ccg_log.txt` is at the root of the app's data directory, not inside `files/`. That is deliberate and it works because the OS creates the package data directory at install time — so the `freopen` target's parent already exists before step 2's `mkdir(".../files")` runs. Game data goes in `files/`; the log sits one level up, out of the way of the extracted `Data/` tree. And because `freopen` runs *before* the `chdir`, the log path is written as an absolute path — a relative `"ccg_log.txt"` at this point would land in the read-only `/`, which is exactly the problem step 2 exists to fix.

### What a healthy prologue looks like in the log

Because every prologue step narrates itself through the just-redirected stderr, the top of `ccg_log.txt` is a checklist you can read directly. A clean boot with one staged cutscene looks like:

```
INFO: OpenAL: ALSOFT_DRIVERS=opensl (Android OpenSL ES backend)
INFO: bootstrap movie EA_LOGO.bik (12345678 bytes)
INFO: Android movie bootstrap: 1 file(s) copied from /storage/emulated/0/Android/data/org.generalsx.zerohour/files/Data/Movies
INFO: Initializing SDL3 video subsystem...
```

The absence of `WARNING: Android chdir to app storage failed` is itself a signal — that line only appears if `chdir` returned non-zero, which means the game will not find its data. If you see it, stop debugging engine logic and fix storage first. Likewise, if the movie bootstrap prints `0 file(s) copied` when you expected a swap, that is the size-keyed idempotency check (below) telling you the destination already matches — not a failure.

## chdir into app-private storage (relative paths vs read-only "/")

An Android app process starts with its current working directory set to `/` — the root of the filesystem, which is read-only on a non-rooted device. That collides head-on with how this decades-old engine locates and creates its data: **by relative path**. The engine builds its data folders as things like `./GeneralsX`, resolved against `getcwd()`. Against a read-only `/`, every such create fails and the game cannot lay down the folders it expects.

The prologue fixes the CWD before any of that runs:

```cpp
	// GeneralsX @port Android — an app's working directory is "/" (read-only). The engine creates
	// its data folders via relative paths (e.g. "./GeneralsX"), so chdir into app-private storage.
	mkdir("/data/user/0/org.generalsx.zerohour/files", 0755);
	if (chdir("/data/user/0/org.generalsx.zerohour/files") != 0) {
		fprintf(stderr, "WARNING: Android chdir to app storage failed\n");
	}
```

- **`mkdir(".../files", 0755)`** ensures the target exists. Its return value is deliberately ignored — if the directory already exists (`EEXIST`, the common case on every launch after the first), that is success for our purposes. `files/` is the standard Android app-private "internal files" directory (`Context.getFilesDir()`), owned by the app, writable by the app, and inside the SELinux `app_data_file` domain.
- **`chdir(".../files")`** moves the process CWD there. From this point on, every relative path the engine forms — `./GeneralsX`, `Data/`, `Data/Movies/`, the `*.big` archives — resolves under app-private internal storage, which the app *can* write. A failure only logs a warning and continues, because there is nothing better to fall back to; but if `chdir` fails the game will not find its data and will not run, so the warning is the breadcrumb you look for in `ccg_log.txt`.

Concretely, once the CWD is `/data/user/0/org.generalsx.zerohour/files`, the engine's path resolution collapses to the right place with no engine changes at all:

| Engine-relative path | Resolves to (after `chdir`) |
| --- | --- |
| `./GeneralsX` | `/data/user/0/org.generalsx.zerohour/files/GeneralsX` |
| `Data/` | `/data/user/0/org.generalsx.zerohour/files/Data/` |
| `Data/Movies/EA_LOGO.bik` | `/data/user/0/org.generalsx.zerohour/files/Data/Movies/EA_LOGO.bik` |
| `*.big` archives | `/data/user/0/org.generalsx.zerohour/files/*.big` |

That is the entire point of the `chdir`: the Win32-era engine keeps forming the same relative paths it always has, and the single line of setup redirects all of them into app-private internal storage. No path constant in the engine had to learn about Android.

This is the same `/data/user/0/org.generalsx.zerohour/files` that the shipping data-extraction path targets: `SetupActivity` unpacks the bundled game data into `getFilesDir()`, and the engine `chdir`s here to read it. The two halves meet at this directory — see `../fixes/self-contained-packaging.md`. The `chdir` is what makes "load `Data/…` relative to the CWD" mean "load from internal storage" rather than "load from a read-only `/`".

## The SELinux wall & the external→internal movie bootstrap

The bulk of the game data reaches internal storage via the shipping route (bundle-in-APK + first-run extraction, `../fixes/self-contained-packaging.md`). But during development you frequently want to drop in or swap a single Bink cutscene without rebuilding a multi-gigabyte APK. The obvious move — `adb push` the `.bik` straight into `/data/data/<pkg>/files/Data/Movies/` — does not work, and the reason is SELinux, not permissions in the POSIX sense.

On a modern non-rooted device, the app's private data dir is labeled `app_data_file` and scoped to the app's own SELinux domain. The `adb`/`shell` domain is not that domain and is denied write access to it — `adb push` to `/data/data/<pkg>` reports success but writes nothing the app can actually read, and every `run-as cp` variant fails with permission-denied or SELinux errors. (The full matrix of dead ends is catalogued in `../fixes/self-contained-packaging.md`.) Internal storage is a wall from the outside.

There is exactly one gap in the wall: the app's **external** files directory. `SDL_GetAndroidExternalStoragePath()` returns the app-scoped external path (`/storage/emulated/0/Android/data/<pkg>/files`). That location lives on the emulated/FUSE external volume, **which the `adb` shell domain *is* permitted to write**, and which the app process is permitted to read. External is writable-from-outside; internal is readable-and-writable-only-from-inside. The bootstrap uses the app itself as the bridge across that asymmetry: on launch it reads from external (where `adb` put the file) and copies into internal (where only the app can write).

The asymmetry, made explicit:

| Location | Example path | SELinux label (app files) | `adb`/shell can write? | App can read/write? |
| --- | --- | --- | --- | --- |
| Internal (private) | `/data/user/0/<pkg>/files` | `app_data_file` (app's domain) | No — different domain, denied | Yes / Yes |
| External (app-scoped) | `/storage/emulated/0/Android/data/<pkg>/files` | media/FUSE volume | Yes | Yes / Yes |

The bootstrap exists solely to move a file from row two (writable by `adb`) into row one (where the engine reads), using the one process that straddles both — the app itself.

```cpp
	// GeneralsX @port Android — cutscene/movie bootstrap. adb cannot write the app's
	// internal asset dir (SELinux blocks it on non-rooted builds), but the app process
	// CAN read its own external dir. So any .bik pushed via `adb push` to
	// <external>/Data/Movies/ is copied into internal Data/Movies/ here on launch
	// (only when missing or a different size), which the engine then loads normally.
	{
		const char* ext = SDL_GetAndroidExternalStoragePath();
		if (ext != nullptr) {
			char srcDir[1024];
			snprintf(srcDir, sizeof(srcDir), "%s/Data/Movies", ext);
			DIR* d = opendir(srcDir);
			if (d != nullptr) {
				mkdir("Data", 0755);
				mkdir("Data/Movies", 0755);
				...
```

The mechanics, traced against the diff:

- **Source directory.** `srcDir` is `<external>/Data/Movies`. If it does not exist (`opendir` returns `nullptr`), the whole block is skipped — no external movies staged, nothing to do. This is why the step is zero-cost on a normal end-user launch.
- **Destination directories are relative.** `mkdir("Data", 0755)` and `mkdir("Data/Movies", 0755)` are *relative* paths — they resolve under the CWD, which the previous step set to `/data/user/0/org.generalsx.zerohour/files`. So the destination is internal `Data/Movies/`. This is a concrete example of why the `chdir` had to happen first: the movie copy's correctness depends on the CWD already being app-private internal storage.
- **Per-entry filtering.** For each `readdir` entry: dotfiles (`entry->d_name[0] == '.'`) are skipped; the source must `stat` as a regular file (`S_ISREG(ss.st_mode)`), so subdirectories and oddities are ignored.
- **Idempotency by size.** `if (stat(dst, &ds) == 0 && ds.st_size == ss.st_size) continue; // already deployed` — if the destination already exists and is the same byte size as the source, the file is treated as already deployed and skipped. This makes the bootstrap safe to run on every launch: a movie is copied once, then left alone. Swapping in a *different* `.bik` of a different size re-triggers the copy; swapping one of the exact same size does not (a known, accepted limitation of a size-only check — it is a dev convenience, not a content-integrity system).
- **The copy itself** is a plain 64 KiB (`char buf[65536]`) `fread`/`fwrite` loop, `fopen`ing source `"rb"` and destination `"wb"`. Each successful copy logs `INFO: bootstrap movie <name> (<bytes> bytes)`, and the block ends with `INFO: Android movie bootstrap: N file(s) copied from <srcDir>` — both landing in `ccg_log.txt`, which is exactly why the logging redirect had to be set up first. The dev feedback loop is: `adb push movie.bik <external>/Data/Movies/`, relaunch, `run-as cat ccg_log.txt`, confirm the copy line.

Once the file is in internal `Data/Movies/`, the engine loads it through the ordinary path — `FFmpegVideoPlayer` decodes the Bink stream and it plays like any other cutscene (see `../fixes/cutscenes-video.md`). The bootstrap is purely a *delivery* shim; it changes nothing about playback. It is explicitly the "intermediate approach used during development" that `../fixes/cutscenes-video.md` refers to; the shipping delivery route for end users is the self-contained APK.

The end-to-end dev loop this enables is short:

1. `adb push movie.bik /storage/emulated/0/Android/data/org.generalsx.zerohour/files/Data/Movies/` — writes to the app's external dir, which the shell domain is allowed to write.
2. Relaunch the app. The prologue's `opendir(srcDir)` finds the file, and (because internal `Data/Movies/` is missing it or has a different size) copies it in.
3. `adb shell run-as org.generalsx.zerohour cat /data/data/org.generalsx.zerohour/ccg_log.txt` — confirm the `INFO: bootstrap movie …` and `… N file(s) copied …` lines.
4. The engine now loads the movie from internal storage on this and every subsequent launch (until it is size-changed or removed).

No APK rebuild, no rooting, no `WRITE_EXTERNAL_STORAGE` grant — the entire round trip is two `adb` commands and a relaunch.

## Build gotcha 1 — the `SDLIMAGE_` vs `SDL3IMAGE_` option prefix

The engine needs SDL3_image for exactly one thing on Android: decoding PNGs for animated (`.ANI`) cursor loading. It does **not** need JPG, TIF, or WebP. And on Android there is no system shared `libpng16.so` to link against — the vcpkg toolchain provides only a static `libpng16.a`. So `cmake/sdl3.cmake` takes an Android-specific branch that (a) resolves PNG to vcpkg's static target and (b) tells SDL3_image to link its dependencies statically and to disable the formats we do not ship.

The "only PNG" scoping is not a guess — it is written into the one option the Android branch leaves untouched. The shared preamble keeps `SDL3IMAGE_PNG ON` with the comment *"Enable PNG support (ANI cursor loading)"*, and that is the single format the game's cursor pipeline actually decodes through SDL3_image. Everything else SDL3_image can do — JPEG, TIFF, WebP, AVIF, X11 cursor (`XCUR`) files — is dead weight on a phone: the assets are not used, and each enabled format would demand a matching decode library (`libjpeg`, `libtiff`, `libwebp`) that nobody built or shipped for `arm64-android`. Disabling them is not just size hygiene; leaving them `ON` would make SDL3_image's own `CMakeLists.txt` try to locate those libraries and fail configuration. So the Android branch turns PNG into a statically-linked, self-contained decoder and turns everything else off.

Resolving PNG to the static vcpkg build:

```cmake
    if(ANDROID)
        # GeneralsX @port Android — no system shared libpng on Android; use vcpkg's
        # static libpng (PNG::PNG) and link it statically (SDL3IMAGE_DEPS_SHARED OFF below).
        find_package(PNG REQUIRED)
    elseif(NOT APPLE)
        ...
```

Then the option block — and this is the discovery:

```cmake
    if(ANDROID)
        # GeneralsX @port Android — link vcpkg static deps directly; disable formats
        # whose libs we don't ship for arm64-android (engine only needs PNG for cursors).
        # NOTE: SDL3_image's real option prefix is SDLIMAGE_ (not SDL3IMAGE_); the latter
        # is silently ignored. Set both to be safe, but SDLIMAGE_* is the effective one.
        set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "Static deps on Android" FORCE)
        set(SDLIMAGE_PNG_SHARED OFF CACHE BOOL "Link libpng statically on Android" FORCE)
        set(SDLIMAGE_JPG OFF CACHE BOOL "Disable JPG on Android" FORCE)
        set(SDLIMAGE_TIF OFF CACHE BOOL "Disable TIF on Android" FORCE)
        set(SDLIMAGE_WEBP OFF CACHE BOOL "Disable WebP on Android" FORCE)
        set(SDL3IMAGE_DEPS_SHARED OFF CACHE BOOL "Static deps on Android" FORCE)
        set(SDL3IMAGE_JPG OFF CACHE BOOL "Disable JPG on Android" FORCE)
        set(SDL3IMAGE_TIF OFF CACHE BOOL "Disable TIF on Android" FORCE)
        set(SDL3IMAGE_WEBP OFF CACHE BOOL "Disable WebP on Android" FORCE)
    else()
        ...
```

The trap: the pre-existing code (and the desktop `else()` branch) used the `SDL3IMAGE_*` spelling — `SDL3IMAGE_DEPS_SHARED`, `SDL3IMAGE_JPG`, `SDL3IMAGE_TIF`, `SDL3IMAGE_WEBP`, `SDL3IMAGE_PNG`. That looks right — the library is called "SDL3_image", so surely its options are `SDL3IMAGE_*`. **They are not.** SDL3_image's actual CMake option prefix is `SDLIMAGE_`. Setting `SDL3IMAGE_JPG OFF` does nothing at all — the variable is never read by SDL3_image's `CMakeLists.txt`, so it is silently ignored and the format keeps SDL3_image's own default (which is `ON`). No warning, no error; the option simply has no effect, and you discover it only when the build tries to pull in a libjpeg/libtiff/libwebp you never provided for `arm64-android` and fails to link — or, worse, when it *succeeds* at building formats you did not want and bloats the artifact.

The fix is defensive: set **both** prefixes. `SDLIMAGE_*` is the effective one and does the real work (disable JPG/TIF/WebP, force static deps, link libpng statically via `SDLIMAGE_PNG_SHARED OFF`); `SDL3IMAGE_*` is set to the same values purely so the code reads consistently and survives a hypothetical future rename. The comment nails the rule so the next person does not re-lose the afternoon: *"SDL3_image's real option prefix is SDLIMAGE_ (not SDL3IMAGE_); the latter is silently ignored. Set both to be safe, but SDLIMAGE_* is the effective one."*

`SDLIMAGE_DEPS_SHARED OFF` plus `SDLIMAGE_PNG_SHARED OFF` are what turn the static `PNG::PNG` from `find_package(PNG REQUIRED)` into a statically-linked dependency of SDL3_image, rather than SDL3_image trying to `dlopen` a shared `libpng` at runtime that does not exist in the APK's `nativeLibraryDir`. On desktop the `else()` arm keeps the old behavior — `SDL3IMAGE_DEPS_SHARED ON`, JPG/TIF/WebP/XCUR `ON` — because that is what actually took effect there historically via SDL3_image's defaults, and the platform does have system shared image libs.

The reason `find_package(PNG REQUIRED)` resolves to a static library at all is the vcpkg triplet. The Android build consumes vcpkg with an `arm64-android`-style triplet, whose default linkage is static — vcpkg produces `libpng16.a`, not a `.so`. That is a feature here, not a limitation: a phone APK ships a fixed set of `.so`s in `nativeLibraryDir`, and the fewer runtime `dlopen` targets the safer, so folding libpng directly into SDL3_image's object is exactly what you want. This is also why the non-Android `elseif(NOT APPLE)` branch goes out of its way (per its own comment) to *bypass* vcpkg's static `.a` and find the *system* shared `libpng16.so` — desktop SDL3_image wants a shared PNG, Android wants it static, and the same manifest serves both by branching here.

## Build gotcha 2 — FFmpeg via vcpkg for Bink cutscenes

The cutscenes are Bink (`.bik`) files. The port does not use the proprietary Bink runtime; it decodes them with FFmpeg through `FFmpegVideoPlayer` (`../fixes/cutscenes-video.md`). For that to link on Android — and on Linux — FFmpeg's decode/demux/resample libraries must be available from the package manager. The change to `vcpkg.json` adds them for every non-Windows platform:

```json
      {
        "name": "ffmpeg",
        "platform": "!windows",
        "features": ["avcodec", "avformat", "swresample", "swscale"]
      }
```

The four features map directly onto what a Bink playback path needs:

- **`avcodec`** — the actual video (and audio) codec decoders that turn compressed Bink frames into raw data.
- **`avformat`** — the demuxer/container layer that reads the `.bik` file structure and hands packets to `avcodec`.
- **`swscale`** — pixel-format/scale conversion, e.g. YUV→RGB and resizing decoded frames into whatever the renderer wants to upload to the Mali GPU.
- **`swresample`** — audio resampling, to convert the decoded audio stream into the sample rate/format the audio backend consumes.

The `"platform": "!windows"` guard mirrors the existing `curl` dependency right above it in the same `dependencies` array — Windows is left to its own arrangement, while Linux and Android (the two `arm64`/`x64` cross-build consumers of vcpkg here) both get FFmpeg. Without this entry the port would either fail to configure (`FFmpegVideoPlayer` cannot find FFmpeg headers/libs) or silently drop cutscene support; adding it is the small manifest change that makes the whole video path — including anything the movie bootstrap above stages — actually decode.

Two things are worth calling out about scoping the feature set this tightly. First, FFmpeg is a large dependency and each feature drags in transitive libraries; requesting only `avcodec`/`avformat`/`swresample`/`swscale` keeps the `arm64-android` static build — and the resulting `.so` payload in the APK — as small as the Bink path allows, rather than pulling the full FFmpeg feature surface (network protocols, extra muxers, filters) the game never touches. Second, the manifest asks for capabilities, not codecs: vcpkg resolves `avcodec` to a build that includes the Bink decoder, so the port does not name Bink anywhere — it names the FFmpeg libraries and lets `FFmpegVideoPlayer` open the `.bik` container (`avformat`) and decode it (`avcodec`) at runtime. That indirection is the whole reason the proprietary Bink runtime could be dropped: the same four features decode Bink on Android exactly as they do on Linux.

This is the load-bearing partner to the movie bootstrap. The bootstrap gets a `.bik` file *onto the device and into internal storage*; the `vcpkg.json` FFmpeg entry is what gives the engine something to decode it *with*. Neither is useful without the other — a staged movie with no FFmpeg is an unreadable blob, and FFmpeg with no staged movie has nothing to play.

## Why desktop is unaffected

Every change here is a no-op on non-Android targets, by construction:

- **The whole `main` prologue** — the log redirect, the `mkdir`/`chdir`, and the movie bootstrap — lives inside a single `#if defined(__ANDROID__) … #endif` block at the top of `SDL3Main.cpp:main`. On Windows and Linux the preprocessor deletes it entirely. Desktop keeps its real console, its normal working directory, and has no external→internal asymmetry to bridge. The `sys/stat.h`/`dirent.h` includes those calls depend on are themselves inside the Android-only include guard (see `build-shared-library-and-jni-entrypoint.md`).
- **`cmake/sdl3.cmake`** wraps the static-PNG `find_package` and the `SDLIMAGE_*` option overrides in `if(ANDROID) … elseif(NOT APPLE) … else()`. Non-Android platforms take the branches they always took — system shared libpng on Linux, Homebrew/framework PNG on macOS, and the `SDL3IMAGE_DEPS_SHARED ON` + JPG/TIF/WebP/XCUR `ON` desktop option set. The Android arm is purely additive.
- **`vcpkg.json`** gates FFmpeg on `"platform": "!windows"`, so Windows resolves the manifest exactly as before. Linux gains FFmpeg too, which is intended — the FFmpeg-based `FFmpegVideoPlayer` is the cross-platform cutscene path, not an Android-only one.

The guiding principle is the same one the rest of the port follows: apply the Android-specific behavior exactly where it is needed and nowhere else, so one source tree keeps building an unchanged desktop game.

## Gotchas & lessons

- **No log, no diagnosis.** Set up the stderr→file redirect as the *first* thing `main` does. Everything after it — the `chdir` warning, the movie-bootstrap `INFO` lines, any engine boot message — is only recoverable because the redirect already ran. If you are bringing up a new Android SDL target and seeing a black-screen-then-exit, wire this up before you debug anything else.
- **`freopen("w")` truncates every launch.** The log is this-run-only. If you need to compare two runs, `run-as cat` the file into a host file *between* launches; a relaunch will overwrite it.
- **Unbuffered stderr matters on crashes.** `setvbuf(stderr, nullptr, _IONBF, 0)` is why the last line before a segfault survives. Note stdout is *not* made unbuffered (only `dup2`'d onto the same fd), so prefer `fprintf(stderr, ...)` for anything you must not lose.
- **`/data/data/<pkg>` == `/data/user/0/<pkg>`.** Do not waste time reconciling the two paths in the code vs the retrieval command — they are the same file (legacy alias for user 0). The code uses the canonical `/data/user/0` form.
- **`chdir` must precede anything using relative paths.** The engine's `./GeneralsX`/`Data/...` creation and the movie bootstrap's relative `mkdir("Data/Movies")` both assume the CWD is already internal `files/`. Order is not cosmetic here.
- **SELinux, not `chmod`, is the wall.** `adb push` to internal storage "succeeds" and writes nothing usable. Do not chase POSIX permissions — the app's data dir is a different SELinux domain than the `adb` shell. The only writable-from-outside surface is the app's *external* dir (`SDL_GetAndroidExternalStoragePath()`), which is the entire reason the bootstrap exists.
- **The movie bootstrap is size-keyed and idempotent.** A same-size replacement will *not* re-copy (`ds.st_size == ss.st_size` short-circuits). If a swapped `.bik` does not take effect, that is why — change the size, or clear internal `Data/Movies/` via `run-as`.
- **`SDLIMAGE_`, not `SDL3IMAGE_`.** The single most expensive gotcha in this file: the intuitive prefix is silently ignored. When an SDL3_image option "does nothing", check the prefix first. The port sets both and documents which one is real.
- **Provide FFmpeg or lose cutscenes.** `FFmpegVideoPlayer` has no fallback decoder; the `vcpkg.json` `ffmpeg` entry (`!windows`) is what makes the Bink path build and link at all.
- **`run-as` needs a debuggable build.** The log-retrieval command only works because the APK is `debuggable` — `run-as` refuses on a release build. On a non-debuggable device the log still gets written (the app can always write its own storage), you just cannot pull it with `run-as`; use the external-dir trick or a `debuggable` variant when you need the log.
- **The external staging dir needs no storage permission.** `SDL_GetAndroidExternalStoragePath()` is the *app-scoped* external dir (`Android/data/<pkg>/files`), which is readable/writable by the app and writable by `adb` without any `READ/WRITE_EXTERNAL_STORAGE` grant or scoped-storage dance. That is why the bootstrap is a one-line `adb push` in the dev loop rather than a permissions saga.
- **Keep the FFmpeg feature list minimal.** Adding features to the `vcpkg.json` `ffmpeg` entry grows every non-Windows build (Linux included) and the on-device `.so`. Only add a feature when a code path actually needs it; the four present are exactly what the Bink decode path requires.

## Related

- `../fixes/self-contained-packaging.md` — the shipping delivery route: bundling game data in the APK and extracting it into `getFilesDir()` (the same `/data/user/0/org.generalsx.zerohour/files` this doc `chdir`s into) via `SetupActivity`, plus the full matrix of why `adb push` to internal storage fails.
- `../fixes/cutscenes-video.md` — how the staged `.bik` files actually play: `FFmpegVideoPlayer`, the two playback paths, and the on-screen Skip button. This doc's movie bootstrap is the development-time delivery shim that doc refers to.
- Sibling discovery `audio-opensl-es-backend.md` — another guarded `SDL3Main.cpp` Android branch: forcing `ALSOFT_DRIVERS=opensl` so openal-soft uses the OpenSL ES backend instead of falling back to the "null" device.
- Sibling discovery `build-shared-library-and-jni-entrypoint.md` — how control reaches `main` on Android at all (`libmain.so` + `SDL_main` via SDL's JNI bootstrap), and where the `sys/stat.h`/`dirent.h` includes this prologue uses are declared.
