# Terrain / textures: software BC (DXT) decode & the magenta terrain

## Symptom

- Terrain rendered as flat **magenta** (the classic "missing/failed texture" color).
- Various textures were wrong or missing on the Mali GPU.

## Background

The engine ships textures in **BC/DXT** (S3TC) compressed formats. Desktop GPUs expose these
via `D3DFMT_DXT1/3/5`. On the Mali/Vulkan path through DXVK, the exact legacy compressed formats
the engine assumes aren't always available in the form the old code expects, and some fill paths
bypass the normal texture‑load hook entirely.

## Fixes

1. **Software BC/DXT decode.** Where the hardware/translation layer can't consume the legacy
   compressed format directly, the texture is decoded to an uncompressed format in software so it
   samples correctly on Mali.
2. **The magenta terrain.** Terrain uses a texture *atlas* built by a fill path that
   **bypassed** the BC‑decode hook — so terrain tiles were never decoded and came out as the
   fallback magenta. The fix routes the terrain atlas fill through the same decode path as the
   rest of the texture pipeline.

Result: correct terrain, water and unit textures instead of magenta/garbage.

Related files include the WW3D2 texture/format code and the terrain texture builder
(`TerrainTex.cpp`, `WorldHeightMap.cpp`, `missingtexture.cpp`, `dx8caps.cpp`).
