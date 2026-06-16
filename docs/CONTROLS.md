# Touch controls reference

The port ships a custom multi-touch scheme (there is no mouse). See
[fixes/touch-controls.md](fixes/touch-controls.md) for the implementation.

| Gesture | Action |
|---|---|
| **Tap** | Select unit / building; issue a move or attack order to the tapped point |
| **Tap + hold, then drag** | Rubber-band box-select multiple units |
| **Drag near a screen edge** | Scroll the camera |
| **Two-finger drag** | Pan the camera (speed scales with the sensitivity slider) |
| **Pinch in / out** | Zoom the camera out / in (speed linked to scroll-speed sensitivity) |
| **☰ button** (top-left) | Open the in-game / pause menu (acts as the **ESC** key) |
| **▶▶ button** (top-right, during a cutscene) | Skip the cutscene |

## Sensitivity

**Options → Scroll Speed** is repurposed on Android as a touch-sensitivity multiplier: it scales
both two-finger pan speed and pinch-zoom speed, and it persists across launches (saved with the
rest of the options). Higher = faster camera.

## Minimap

- **Campaign missions:** the minimap is always available (you navigate the map by touching it).
- **Skirmish / multiplayer:** the minimap appears only once you have radar (build the structure
  that provides it), exactly as on desktop.

See [fixes/minimap-radar.md](fixes/minimap-radar.md).
