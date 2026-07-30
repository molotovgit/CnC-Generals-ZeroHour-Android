# FAQ

Short answers to the questions that come up most. Longer write-ups live in
[docs/](.) — this page is the index.

## Playing

**Do I need to own the game?**
Yes. This repo is engine source and documentation only, GPLv3. The `.big` archives, movies
and maps are EA's copyrighted content and are not distributed here. Bring your own retail
copy — see [GAME-FILES.md](GAME-FILES.md).

**What device do I need?**
An arm64 Android phone with a Vulkan driver and about 5 GB free. Development and testing
happen on a Mali-G57. Other GPUs are largely untested — post results in
[Device compatibility](https://github.com/molotovgit/CnC-Generals-ZeroHour-Android/discussions/4).

**Is this an emulator or streaming?**
Neither. The original engine is cross-compiled to native arm64 and its Direct3D 8 calls are
translated to Vulkan by DXVK, running on the phone's own GPU.

**How do I get my game files onto the phone?**
Follow [SETUP-GAME-FILES.md](SETUP-GAME-FILES.md): locate your install, copy the `.big` files
and `Data/`, run the packaging command, build, install.

**Why is the first launch slow?**
The self-contained build unpacks the bundled game data on first start — one to two minutes,
once.

**How do I control an RTS without a mouse?**
A purpose-built multi-touch scheme: tap to select, drag to box-select, two-finger drag to pan,
pinch to zoom, two-finger tap for right-click. Full table in [CONTROLS.md](CONTROLS.md).

**Does multiplayer work?**
Not yet. LAN between two devices is in progress; the instrumentation to debug it is shipped,
the feature is not. See [lan-networking-instrumentation.md](discoveries/lan-networking-instrumentation.md).

## Building

**Where do I start?**
[BUILDING.md](../BUILDING.md). Engine cross-compiled with the NDK and CMake (vcpkg supplies
SDL3, OpenAL, FFmpeg), DXVK built separately, app packaged with Gradle.

**Why does DXVK need its own build?**
It targets Android differently from the desktop build — see
[BUILDING-DXVK-ANDROID.md](BUILDING-DXVK-ANDROID.md).

**The build fails on `pthread_cancel` / `sys/timeb.h` / `std::from_chars`.**
Expected — bionic and NDK libc++ omit all three. Each has a narrow `__ANDROID__` guard;
[bionic-and-libcxx-gaps.md](discoveries/bionic-and-libcxx-gaps.md) explains why the
`from_chars` one needs two changes rather than one.

## Things that looked broken and why

**The terrain is pink.** The missing-texture placeholder was debug magenta, and on terrain it
is multiplied into a MODULATE stage rather than displayed, so it zeroes the green channel of
every pixel. It is now white — the multiplicative identity.
[Write-up](discoveries/missing-texture-fallback-color.md).

**No text anywhere.** Two independent failures with one symptom: FreeType was not compiled in
on Android, and Fontconfig has no config on Android so nothing resolved a family to a `.ttf`.
Both had to be fixed. [Write-up](discoveries/android-font-resolution.md).

**Random crash after 30-70 seconds of heavy combat.** The translucent-geometry sorting pool
batches unbounded geometry through 16-bit counts; past 65535 vertices the buffer allocation
truncated while the fill loop copied the full amount, corrupting the heap.
[Write-up](discoveries/sorting-vertexbuffer-16bit-overflow.md).

**Shader compiler crash on Mali.** D3D9 user clip planes emit `gl_ClipDistance`, which crashes
the Mali-G57 shader compiler. [Write-up](fixes/mali-shader-cmpbe-crash.md).

## Contributing

**What helps most?**
A screenshot or a short clip from a device that is not a Mali-G57, and a device report either
way. Reach is the bottleneck, not code.

**What must never be posted?**
Built APKs containing game data, or the game data itself. Media and logs only.

Anything not covered here belongs in
[Discussions](https://github.com/molotovgit/CnC-Generals-ZeroHour-Android/discussions).
