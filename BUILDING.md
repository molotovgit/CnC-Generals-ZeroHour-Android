# Building the Android port

This is a two‑part build: **(1)** cross‑compile the native engine to a shared library
(`libmain.so`) for `arm64-v8a`, then **(2)** package the Android app (`android/`) that loads it.
You also need to supply your own **game data** to actually run it (see
[docs/GAME-FILES.md](docs/GAME-FILES.md)).

> **Just want to play with your own game files?** The step‑by‑step
> [docs/SETUP-GAME-FILES.md](docs/SETUP-GAME-FILES.md) covers finding your install, copying the
> `.big` files + `Data/`, packing them (one command), and installing — start there.

> Everything below was validated on Windows 11 with the phone attached over ADB, targeting a
> Redmi (Mali‑G57, `arm64-v8a`, Android 14+). It works the same on Linux/macOS hosts with the
> matching toolchains.

## Prerequisites

| Tool | Version used | Notes |
|---|---|---|
| Android SDK | platform 35 | `platform-tools`, `build-tools` |
| Android NDK | 27.x | `aarch64-linux-android28-clang++` |
| CMake | 3.22.1 (from the SDK) | ninja generator |
| JDK | 17 (Temurin) | Gradle needs JVM ≥ 11 |
| vcpkg | bundled/fetched | pulls SDL3, OpenAL Soft, FFmpeg |
| Python | 3.10 | asset packaging helpers |

`minSdkVersion 28`, `targetSdkVersion 35`, single ABI `arm64-v8a`.

## 1. Cross‑compile the engine

The engine is the [GeneralsX](https://github.com/fbraz3/GeneralsX) tree under `engine/` with
the Android changes documented in [docs/](docs/). Configure and build the `z_generals` target:

```bash
cmake -S engine -B engine/build/android-arm64 \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28

cmake --build engine/build/android-arm64 --target z_generals -j8
```

Output: `engine/build/android-arm64/GeneralsMD/Code/Main/libmain.so`.

Key CMake integration points (see the `cmake/` files and `docs/fixes/`): `SAGE_USE_OPENAL=ON`
selects the OpenAL Soft backend; SDL3 and FFmpeg come from vcpkg; DXVK is built as a separate
set of `.so` files (`libdxvk_d3d8.so`, `libdxvk_d3d9.so`).

> DXVK uses **meson**, not CMake, so it's cross-compiled separately — see
> **[docs/BUILDING-DXVK-ANDROID.md](docs/BUILDING-DXVK-ANDROID.md)** for the NDK cross file and
> the exact build steps.

## 2. Package the Android app

Stage the native libraries into the Gradle project, then assemble:

```bash
# copy the freshly built engine + the prebuilt deps into the jniLibs dir
cp engine/build/android-arm64/GeneralsMD/Code/Main/libmain.so android/app/libs/arm64-v8a/
# also needed there: libSDL3.so libSDL3_image.so libdxvk_d3d8.so libdxvk_d3d9.so \
#                    libopenal.so libgamespy.so   (built separately)

cd android
JAVA_HOME=/path/to/jdk-17 ./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The app reads game data from its internal storage: the native entry point (`SDL3Main.cpp`)
`chdir()`s to `/data/user/0/org.generalsx.zerohour/files` and loads `Data/` and the `*.big`
archives relative to that. You must place the game data there — see below.

## 3. Provide game data (two options)

**A. Manual deploy (engine‑only APK).** Copy your retail `*.big` files + `Data/` into the app's
internal storage. Because SELinux blocks `adb`/`run-as` from writing that directory on a
production device, the practical route on‑device is the *self‑contained* build (option B).
For details and the SELinux constraints, see
[docs/fixes/self-contained-packaging.md](docs/fixes/self-contained-packaging.md).

**B. Self‑contained APK (recommended).** Bundle the game data into the APK and extract it on
first launch:

1. Assemble your game data tree (all `*.big`, `Data/` incl. `Data/Movies`, `dxvk.conf`).
2. Zip it **stored** (no compression — `.big`/`.bik` don't compress) and **split into <2 GB
   parts** (Android's asset pipeline can't process a single >2 GB asset). Place the parts as
   `android/app/src/main/assets/gamedata000.pak`, `gamedata001.pak`, …
3. `android/app/build.gradle` already has `androidResources { noCompress 'pak' }`; bump the
   Gradle heap (`org.gradle.jvmargs=-Xmx4096m`) so it can process the parts.
4. `SetupActivity` (the launcher) concatenates the parts and unpacks them to internal storage
   on first run, then hands off to `ZerohourActivity`.

Full writeup: [docs/fixes/self-contained-packaging.md](docs/fixes/self-contained-packaging.md).

> **Legal:** the game data is EA‑copyrighted and is **not** provided here. Use your own retail
> copy. See [docs/GAME-FILES.md](docs/GAME-FILES.md).

## Device notes

- Keep the app **landscape‑locked**; a portrait↔landscape flip can break the DXVK swapchain.
- First self‑contained launch needs ~5 GB free (APK in `/data/app` + extracted data in
  `/data/data`).
