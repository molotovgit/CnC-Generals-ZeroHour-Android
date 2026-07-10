# Building the engine as a shared library and the JNI entry point

> On Android there is no process `main()` for the OS to launch — the Java `SDLActivity` loads the engine as `libmain.so` and calls `SDL_main` through JNI. Two small, guarded changes make a Win32 `WinMain` game boot that way: `add_library(... SHARED)` with `OUTPUT_NAME main`, and `#include <SDL3/SDL_main.h>` in the file that defines `main()`.

This is one of the smallest diffs in the port and one of the most load-bearing. If either half is wrong the app installs, launches, shows a black screen, and exits — no crash log, no window, nothing. Understanding *why* both halves are required is the whole game.

## Why Android is different

On desktop, the loader hands control to a process entry point that the C runtime eventually funnels into your `main()` (or, for a `WIN32` GUI executable, `WinMain`). You own the process from its first instruction.

Android has no such entry point for you to own. An app is an ART (Java/Kotlin) process. The OS starts the VM, instantiates your `Activity`, and calls Java lifecycle methods (`onCreate`, …). Native code only runs once Java loads it with `System.loadLibrary(...)` and then calls into it across the JNI boundary. There is no C `main()` that the platform will ever call on its own.

SDL papers over this with its Android backend. SDL ships a Java class — `org.libsdl.app.SDLActivity` — that the app subclasses (or uses directly) as its `Activity`. `SDLActivity` owns the Android lifecycle and does the bootstrap the platform will not do for you:

1. In `onCreate`, it `System.loadLibrary`'s the app's native libraries.
2. It spins up a dedicated thread ("SDLThread").
3. On that thread it crosses back into native code via JNI, `dlopen`s the app's **main shared object**, `dlsym`s the app's **main function**, and calls it.

Two `SDLActivity` defaults are what the CMake and source changes are built to satisfy:

- the default *main shared object* is **`libmain.so`** (`getMainSharedObject()`), and
- the default *main function* is **`SDL_main`** (`getMainFunction()`).

You can override either default in Java, but the cheapest path is to make the native build produce exactly what SDL already looks for. That is precisely what these two changes do.

One consequence worth internalizing early: because the engine runs on SDLThread rather than the Android UI thread, `SDL_main` behaves like a classic blocking `main` — it enters, runs the whole game loop, and only returns when the game is quitting. When it returns, SDL tears the app down. The engine code does not have to know it is living inside a JNI call; from its point of view it was simply started with `argc`/`argv` and will run until it returns an exit code, exactly as on desktop. That symmetry is the reason a decades-old Win32 game loop survives the move to Android almost unedited.

## The build target (`add_library SHARED` → `libmain.so`)

The desktop target is a windowed executable. In `GeneralsMD/Code/Main/CMakeLists.txt` the original first line was:

```cmake
add_executable(z_generals WIN32)
```

`WIN32` selects the Windows GUI subsystem — the linker wires the entry point to `WinMain`, not a console `main`. On Android that is meaningless: you cannot produce a launchable executable at all; you must produce a shared object the Java side can `dlopen`. The change guards the two cases:

```cmake
if(ANDROID)
    # GeneralsX @port Android — SDLActivity loads the app as libmain.so and invokes SDL_main via JNI.
    add_library(z_generals SHARED)
else()
    add_executable(z_generals WIN32)
endif()
```

`z_generals` is only the *logical* CMake target name — it is not the file that ships. The filename is governed by `OUTPUT_NAME`, and that is the second half of the CMake change:

```cmake
if("${CMAKE_SYSTEM}" MATCHES "Windows")
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "generalszh${RTS_BUILD_OUTPUT_SUFFIX}")
elseif(ANDROID)
    # GeneralsX @port Android — SDLActivity loads "libmain.so" by default.
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME main)
else()
    # GeneralsX @build BenderAI 15/02/2026 Linux branding update
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME GeneralsXZH)
endif()
```

`OUTPUT_NAME main` does not produce a file called `main`. For a `SHARED` library targeting Android (an ELF/Linux platform as far as the toolchain is concerned), CMake applies the platform library prefix and suffix automatically: `lib` + `main` + `.so` = **`libmain.so`**. That is the exact filename `SDLActivity.getMainSharedObject()` resolves to by default inside the APK's `nativeLibraryDir` (e.g. `.../lib/arm64-v8a/libmain.so`).

So the chain is:

```
add_library(z_generals SHARED)      → produce a .so, not an .exe
   + OUTPUT_NAME main               → basename "main"
   + Android lib prefix/.so suffix  → libmain.so
   = the file SDLActivity dlopen's by default
```

Get the name wrong — leave it as the CMake default (`libz_generals.so`) or set some other `OUTPUT_NAME` — and `SDLActivity`'s default `dlopen` fails. You would then have to override `getMainSharedObject()` in Java to point at your actual filename. Matching SDL's default is the smaller, less error-prone change, which is why the port takes it.

The `ANDROID` variable the guard tests is set for you by the NDK's CMake toolchain file — no manual detection is needed. For this port it is an arm64 (`arm64-v8a`) build under NDK 27, so the resulting `libmain.so` is packaged at `lib/arm64-v8a/libmain.so` inside the APK and unpacked into the app's `nativeLibraryDir` at install time. `SDLActivity` resolves its default main shared object against exactly that directory, which is why "produce a `.so` named `libmain`" and "the file the loader opens" are the same statement — the toolchain, the packaging, and SDL's default all agree on the name.

## The entry point (`SDL_main.h` / JNI)

A correctly named `libmain.so` is necessary but not sufficient. `SDLActivity` also `dlsym`s a **function** — by default `SDL_main` — and calls *that*. The engine's entry point is spelled `int main(argc, argv)`, so the symbol in the library is `main`, and `dlsym(handle, "SDL_main")` returns `NULL`. Nothing runs.

The fix is one include, added to `GeneralsMD/Code/Main/SDL3Main.cpp` — the translation unit that defines the engine's `main()`:

```cpp
#if defined(__ANDROID__)
// GeneralsX @port Android — SDLActivity enters the app through SDL_main (via JNI). Including
// SDL_main.h in the file that defines main() renames it to SDL_main so it is actually invoked.
#include <SDL3/SDL_main.h>
#include <sys/stat.h>   // mkdir()/stat() for app-private data dir
#include <dirent.h>     // opendir()/readdir() for the external-movies bootstrap
#endif
```

`<SDL3/SDL_main.h>` is not an ordinary header — on platforms that need an entry-point redirect (Android among them) it does, in effect, `#define main SDL_main`. Because the macro is in scope when the compiler reaches the definition further down the file:

```cpp
int main(int argc, char* argv[])
{
    int exitcode = 1;
    ...
```

the preprocessor rewrites that definition to `int SDL_main(int argc, char* argv[])`. The engine's entry point is now exported under the exact symbol SDL's Android bootstrap looks up. The developer still writes and reads plain `main` — the redirect is invisible in the source and is why the `sys/stat.h`/`dirent.h` includes (used by the Android storage and movie-copy bootstrap at the top of `main`) ride along in the same guarded block.

The redirect must live in the **same translation unit** that defines `main`. `SDL_main.h` only rewrites the `main` token the compiler sees after the include; a redirect in some other `.cpp` would rename nothing here and `SDL_main` would still be missing.

This `#define main SDL_main` trick is the long-standing, idiomatic way SDL apps hand their entry point to the framework — it predates SDL3 and is why almost every SDL tutorial's `main` is really `SDL_main`. The alternative is to define `SDL_MAIN_HANDLED` and call SDL's bootstrap yourself, but that means writing platform entry glue by hand — on Android, a `JNIEXPORT` function and thread management — which is exactly the work `SDL_main.h` exists to spare you. The port takes the idiomatic route: include the header, keep writing plain `main`, and let SDL own the JNI plumbing. Nothing in `SDL3Main.cpp` mentions JNI, `jobject`, or `JNIEnv` — that surface lives entirely inside SDL's prebuilt Java and native bootstrap.

Both halves together:

| Missing piece | Symptom |
| --- | --- |
| Library not named `libmain.so` | `SDLActivity` default `dlopen` fails; no native code enters at all |
| `main` not renamed to `SDL_main` | `dlopen` succeeds, `dlsym("SDL_main")` returns `NULL`; nothing is called |
| Both correct | `SDLActivity` `dlopen`s `libmain.so`, resolves `SDL_main`, calls it on the SDL thread — the engine boots |

That is the entire JNI entry story from the engine's side: the port never writes a `JNIEXPORT` function or touches JNI directly. It relies on SDL's Java+native bootstrap and simply presents SDL the file and symbol it expects.

### The boot sequence, end to end

Putting both changes together, here is the full path from a tap on the launcher icon to the engine's game loop:

1. Android starts the ART process and instantiates the app's `Activity` (an `org.libsdl.app.SDLActivity` subclass).
2. `SDLActivity.onCreate` calls `System.loadLibrary` for each native library — SDL3, its dependencies, and the app's own `libmain.so`.
3. `SDLActivity` starts SDLThread and, over JNI, invokes its native runner with the main shared object (`libmain.so`) and main function (`SDL_main`).
4. The native runner `dlopen`s `libmain.so` and `dlsym`s `SDL_main` — the symbol that `#include <SDL3/SDL_main.h>` produced from the engine's `main()`.
5. It calls `SDL_main(argc, argv)` on SDLThread. Control is now inside `SDL3Main.cpp`, which runs the Android storage/log/movie bootstrap and then hands off to the engine's `GameMain()`.
6. `SDL_main` runs the game to completion, returns an exit code, and SDL tears the process down.

Only steps 2–4 are SDL's prebuilt machinery. The port's contribution is making step 4 succeed: the right filename (from CMake) and the right symbol (from `SDL_main.h`).

## Why desktop is unaffected

Every part of this is guarded so the one source tree still builds an ordinary Windows (and Linux) game with no behavioral change:

- **CMake.** The `if(ANDROID) … else()` branch keeps `add_executable(z_generals WIN32)` verbatim on every non-Android target. The `OUTPUT_NAME` selection still routes Windows to `generalszh${RTS_BUILD_OUTPUT_SUFFIX}` and Linux to `GeneralsXZH`; only the new `elseif(ANDROID)` arm sets `main`.
- **Source.** The `#if defined(__ANDROID__)` guard means `<SDL3/SDL_main.h>` is included *only* on Android. On desktop the token `main` is never redefined by this block, so the Windows `WIN32` executable keeps whatever entry-point arrangement it had before this change — byte-for-byte identical preprocessing on non-Android builds.

The guiding principle: the Android entry-point rewrite is applied exactly where it is needed and nowhere else. A desktop developer can build, run, and debug the game without ever noticing the port exists.

## Gotchas & lessons

- **`OUTPUT_NAME` must be literally `main`.** Not the target name, not `z_generals`, not a branded string. The value is a contract with `SDLActivity.getMainSharedObject()`'s default. If you want a different filename, you must override `getMainSharedObject()` in the Java `Activity` to match — extra code for no benefit.
- **`SHARED`, and keep `SDL_main` exported.** `dlsym` can only find a symbol with external, default visibility. If the build later adds `-fvisibility=hidden`, `SDL_main` needs an explicit export (SDL's own `SDLMAIN_DECLSPEC`/`extern "C"` handling via `SDL_main.h` covers this) or the lookup silently fails again.
- **Include site matters.** `SDL_main.h` must be included in the translation unit that defines `main()`, before the definition. Putting it anywhere else renames nothing useful.
- **No `dlopen` island.** `libmain.so` still depends on `libSDL3.so`, DXVK, and OpenAL at load time; those must be packaged into the same `nativeLibraryDir` or `System.loadLibrary`/`dlopen` fails before `SDL_main` is ever reached. The build change only names and shapes the entry library — packaging is a separate concern (see `../BUILDING.md`).
- **Fail-silent by design.** A wrong library name or missing symbol produces no crash — the app just exits. When bringing up a new SDL-Android target, verify with `adb logcat` that `SDLActivity` found and called `SDL_main` before suspecting engine logic.
- **One tree, many targets.** Guarding with `if(ANDROID)` / `#if defined(__ANDROID__)` rather than forking `CMakeLists.txt` or `SDL3Main.cpp` keeps the desktop and Android builds in lockstep — the pattern the rest of this port follows everywhere.

## Related

- `../BUILDING.md` — full toolchain (NDK 27, CMake, Ninja) and how `libmain.so` is assembled into the APK alongside SDL3, DXVK, and OpenAL.
- Sibling discovery `logging-storage-and-movie-bootstrap.md` — what the rest of the Android `main()` does before `GameMain()`: redirecting stdout/stderr to an app-private log, `chdir` into app storage, and the external-movies copy step (the reason `sys/stat.h`/`dirent.h` were pulled in above).
- Sibling discovery `native-fullscreen-and-resolution.md` — publishing the real device resolution (`TheAndroidDisplayWidth`/`Height`) from `SDL3Main.cpp` so the engine renders edge-to-edge instead of a letterboxed 4:3 backbuffer.
