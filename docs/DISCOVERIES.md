# Engineering discoveries

Deep-dive write-ups of the non-obvious things we learned taking a 2003 **Direct3D 8 / Win32** RTS
and making it run natively on an **Android (arm64)** phone — through DXVK (D3D8→D3D9→Vulkan) on a
Mali-G57, SDL3, OpenAL Soft, and FFmpeg.

Each page follows the same shape: **symptom → why Android is different → investigation → root
cause → the fix (with code references) → gotchas & lessons.** Every technical claim is traceable
to the actual engine change under `src/GeneralsX`. These are the long-form companions to the
quick notes in [fixes/](fixes/) and the chronological journal in [PORTING-NOTES.md](PORTING-NOTES.md).

> If you're porting another old D3D/Win32 game to mobile, the *shape* of these problems —
> platform supersets (`__linux__` includes Android), Win32 assumptions that don't hold on a
> translation layer, implicit non-null/ordering invariants, and stdio/filesystem/SELinux
> realities — will look very familiar.

---

## Build & platform bring-up

- **[Building as a shared library & the JNI entry point](discoveries/build-shared-library-and-jni-entrypoint.md)**
  — Turning a Win32 `WinMain` game into an Android `libmain.so` that `SDLActivity` dlopen's and
  calls via `SDL_main` — the CMake and entry-point mechanics, fully guarded.
- **[Filling bionic / NDK libc++ gaps](discoveries/bionic-and-libcxx-gaps.md)**
  — Three build breaks, one root cause: bionic and NDK libc++ omit `pthread_cancel`,
  `<sys/timeb.h>`, and float `std::from_chars` — and why each fix is safe.
- **[App-private storage, logging & the SELinux movie bootstrap](discoveries/logging-storage-and-movie-bootstrap.md)**
  — On Android the engine boots blind into `/dev/null` stdio and a read-only `/`; here's the
  four-step prologue (and two vcpkg gotchas) that make it survivable.

## Display, rendering & the GPU

- **[Native-resolution fullscreen & the empty resolution table](discoveries/native-fullscreen-and-resolution.md)**
  — The Win32 "display-mode table" is empty on DXVK/Android — one fact behind both the 4:3
  pillarbox and the `0x568` Options-menu crash, and its two-part fix.
- **[DXVK texture-format capabilities](discoveries/dxvk-texture-format-capabilities.md)**
  — One 10-bit DXVK swapchain, two black-radar bugs: an UNKNOWN backbuffer that disables *every*
  texture format, and 24-bit `R8G8B8` that lies about its stride.
- **[Why the missing-texture fallback must be white, not magenta](discoveries/missing-texture-fallback-color.md)**
  — The placeholder is *modulated* over terrain, not shown — so magenta multiplies the whole map
  pink; white is the identity that fixes it.
- **[Defensive null-guards for mobile-GPU resource pressure](discoveries/mobile-gpu-null-guards.md)**
  — Four W3D render-layer guards where "always non-null" assumptions break under Android's VRAM
  limits and load-order timing — and why early-return beats crashing mid-mission.
- **[The 16-bit sorting vertex-buffer overflow](discoveries/sorting-vertexbuffer-16bit-overflow.md)**
  — A latent 16-bit sorting-buffer assumption that DXVK's tight mapped memory turned into a
  delayed heap-corruption `SIGSEGV 0x2000000001` — fixed with one early-flush guard.

## Audio & text

- **[OpenAL Soft: the `__linux__` trap](discoveries/audio-opensl-es-backend.md)**
  — Android is also `__linux__`, so OpenAL Soft ran the desktop driver list, opened the null
  device, and played every sound into silence.
- **[Rendering text without a Fontconfig config](discoveries/android-font-resolution.md)**
  — Android ships no Fontconfig config, so FreeType text renders blank; the port compiles the
  FreeType path in and maps font families straight to `/system/fonts`.

## Input, UX & gameplay

- **[Touch → RTS control scheme](discoveries/touch-rts-control-scheme.md)**
  — Synthesizing precise mouse/keyboard input from multi-touch: deferred left-down for finger
  disambiguation, sustained-press click keep-alive, the draw/hit-test geometry contract, and
  reusing the persisted scroll slider.
- **[Single-player minimap availability](discoveries/single-player-minimap-availability.md)**
  — Keeping the minimap usable for touch navigation in single-player campaign without ever
  loosening the radar gate in skirmish or multiplayer.

## Networking

- **[LAN instrumentation (work in progress)](discoveries/lan-networking-instrumentation.md)**
  — How the activated `[LAN86]` stderr breadcrumbs make NIC enumeration, `SetLocalIP`, and
  `UDP::Bind` watchable in the app-private log while LAN bring-up is still in progress.

---

*These write-ups document code and engineering only. No game data is included anywhere in this
repository — you supply your own retail files (see [GAME-FILES.md](GAME-FILES.md)).*
