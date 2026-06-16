# Self‑contained APK: bundling & first‑run extraction

The engine reads its game data from the app's **internal** storage — `SDL3Main.cpp` `chdir()`s
to `/data/user/0/org.generalsx.zerohour/files` and loads `Data/` + `*.big` relative to that.
The problem: **you can't get 2+ GB of data into that directory on a normal phone.**

## Why not just `adb push`?

On a modern, non‑rooted, production Android device, **SELinux** blocks writing the app's private
data dir from outside the app. Every route was tried and fails:

| Route | Result |
|---|---|
| `adb push` to `/sdcard` → `run-as cp` | Permission denied (app domain can't read `/sdcard`) |
| `adb push` to `/data/local/tmp` → `run-as cp` | "No such file" (SELinux) |
| `adb push` to the app's own external dir → `run-as cp` | Permission denied |
| `adb shell "run-as … cat > file"` (pipe) | corrupts binary (PTY/CRLF mangling) |
| `adb exec-out … ` (pipe) | stdin not forwarded (Windows adb) → hangs |
| direct `adb push` to `/data/data/<pkg>` | reports success, writes nothing the app can read |

The one thing that *does* work: the **app itself** can write its own internal storage, and can
read data bundled in its own APK.

## The design

1. **Bundle the data in the APK.** All `*.big`, `Data/` (incl. `Data/Movies`) and `dxvk.conf`
   are zipped **stored** (no compression — they're already compressed) and the zip is **split
   into <2 GB parts** (`gamedata000.pak`, `gamedata001.pak`, …). The split is required because
   Android's asset pipeline (`compressDebugAssets`) reads each asset fully into a Java `byte[]`:
   a single >2 GB asset fails with *"Required array size too large"*, and a part larger than the
   Gradle heap fails with *"Java heap space"*. ~400 MB parts + `org.gradle.jvmargs=-Xmx4096m`
   build cleanly. `build.gradle` marks them `androidResources { noCompress 'pak' }`.
2. **Extract on first launch.** A dedicated **`SetupActivity`** is the launcher. On first run
   (marker `.gamedata_ok_v1` absent) it shows an *"Installing game data…"* screen, concatenates
   the `gamedataNNN.pak` parts via `SequenceInputStream` back into the original zip stream, and
   unpacks it to `getFilesDir()` on a background thread, writing the completion marker **last**
   (so an interrupted extract re‑runs cleanly). Then it starts `ZerohourActivity` (the game).
   Subsequent launches see the marker and jump straight to the game.

## Result

A single **install‑and‑play APK**. Fresh install → *"Installing game data…"* (~1–2 min) →
game boots. Needs ~5 GB free on the device (APK in `/data/app` + extracted data in `/data/data`).

> The self‑contained APK embeds copyrighted game data, so it is **not** distributed in this
> repo — you build it from your own retail files. The packaging scripts live under `scripts/`.
