# True fullscreen at native resolution

## Symptom

The game rendered into a 4:3 pillar‑boxed area in the middle of the screen instead of filling
the phone's 1640×720 display, and the Options → Resolution list could crash.

## Root cause

The engine's resolution handling was written for a Win32 render device that enumerates a table
of display modes. On Android/DXVK there is no such table:

1. `buildFilteredResolutions()` called `WW3D::Get_Render_Device_Desc(0).Enumerate_Resolutions()`
   which dereferenced an empty table and **SIGSEGV'd** the moment the Options menu built its
   resolution list.
2. Nothing told the 3D backbuffer, the 2D UI and the mouse to use the real device size, so they
   defaulted to a 4:3 mode and got letter/pillar‑boxed.

## Fix (`W3DDisplay.cpp`)

- On Android, `buildFilteredResolutions()` returns just the **single native display resolution**
  (published by `SDL3Main.cpp`) instead of enumerating an empty Win32 table — no crash, and the
  Options list shows the correct mode.
- `W3DDisplay::init()` sets `m_xResolution`/`m_yResolution` to the real device size so the 3D
  backbuffer, the 2D UI and the mouse are all in agreement — the game fills the screen edge to
  edge at 1640×720.

Result: native‑resolution fullscreen, correct terrain/water/textures, and an Options menu that
doesn't crash.
