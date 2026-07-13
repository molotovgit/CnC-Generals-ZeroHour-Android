# Building DXVK for Android (arm64)

The port needs `libdxvk_d3d8.so` + `libdxvk_d3d9.so` built for `arm64-v8a`. DXVK uses **meson**,
not CMake, so it's cross-compiled with a meson *cross file* pointing at the NDK toolchain.

> The engine calls Direct3D 8 → DXVK translates D3D8 → D3D9 → **Vulkan** → the phone's GPU driver.

## 1. The cross file

Save as `meson-android-arm64.txt` in the DXVK checkout. Replace `$ANDROID_SDK` with your SDK path
(and adjust the NDK version / `windows-x86_64` host tag for your OS — e.g. `linux-x86_64`,
`darwin-x86_64`). On Linux/macOS drop the `.cmd` / `.exe` suffixes.

```ini
[binaries]
c   = '$ANDROID_SDK/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android28-clang.cmd'
cpp = '$ANDROID_SDK/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android28-clang++.cmd'
ar  = '$ANDROID_SDK/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-ar.exe'
strip = '$ANDROID_SDK/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
pkg-config = '/path/to/pkgconf'

[built-in options]
c_args   = ['-DVK_ENABLE_BETA_EXTENSIONS', '-DVK_USE_PLATFORM_ANDROID_KHR']
cpp_args = ['-DVK_ENABLE_BETA_EXTENSIONS', '-DVK_USE_PLATFORM_ANDROID_KHR']

[host_machine]
system     = 'linux'      # bionic is close enough to linux for meson's purposes
cpu_family = 'aarch64'
cpu        = 'aarch64'
endian     = 'little'
```

Two things matter here:

- **`VK_USE_PLATFORM_ANDROID_KHR`** — selects the Android WSI surface (`VK_KHR_android_surface`).
- **`system = 'linux'`** — meson has no `android` system value that DXVK's build expects; Android's
  bionic libc is close enough for the checks DXVK performs.

DXVK also needs a `pkg-config` that can find SDL3 (the WSI backend). Point `pkg-config` at a real
`pkgconf` binary and give it an `sdl3.pc` describing your cross-compiled SDL3.

## 2. Configure + build

```bash
meson setup build-android --cross-file meson-android-arm64.txt \
      -Dbuildtype=release
ninja -C build-android
```

Output (versioned names):

```
build-android/src/d3d8/libdxvk_d3d8.so.0.20600
build-android/src/d3d9/libdxvk_d3d9.so.0.20600
```

Copy them into the Gradle project's `jniLibs`/`libs` dir as `libdxvk_d3d8.so` and
`libdxvk_d3d9.so` (drop the version suffix — Android's packager only accepts `lib*.so`).

## 3. Mobile-GPU changes

Stock DXVK targets desktop drivers. Running on **Mali-G57** needed a set of fixes — software
BC/DXT decode, dummy descriptors where `nullDescriptor` is missing, serialized pipeline creation,
clip-plane avoidance, and tolerating a persistent `VK_SUBOPTIMAL_KHR`. Each is described in
[DISCOVERIES.md](DISCOVERIES.md).

Where possible these are **runtime capability checks** (e.g. "does this GPU report
`textureCompressionBC`?"), not `#ifdef ANDROID`, so they should degrade sensibly on other GPUs —
but only Mali-G57 is confirmed. Reports from Adreno / PowerVR / Xclipse are very welcome.

## Credits

DXVK is by [doitsujin](https://github.com/doitsujin/dxvk); this port builds on the
[fbraz3](https://github.com/fbraz3) DXVK/GeneralsX fork.
