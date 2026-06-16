# scripts/

Helper scripts for building and packaging the Android port. None of them ship or download game
data — you supply your own retail files (see [../docs/GAME-FILES.md](../docs/GAME-FILES.md)).

| Script | What it does |
|---|---|
| `build_engine.sh` | Cross-compile the engine `.so` for `arm64-v8a` with the NDK + CMake. |
| `package_selfcontained.py` | Zip your game data (stored) and split it into `<2 GB` `.pak` assets for the self-contained APK. |
| `deploy_device.sh` | `adb install` the APK and (optionally) launch it. |

See [../BUILDING.md](../BUILDING.md) for the full flow.
