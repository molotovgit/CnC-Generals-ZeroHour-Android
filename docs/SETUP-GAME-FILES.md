# Setting up your own game files (step by step)

This is the practical, follow-along version of [GAME-FILES.md](GAME-FILES.md). It walks you
through taking **your own** copy of *Command & Conquer: Generals — Zero Hour* and turning it into
an install-and-play APK for your phone.

> **You must own the game.** This port ships **no** game data — the `.big` archives, movies and
> maps are Electronic Arts' copyright. Get them from your retail disc or a digital copy you bought
> (e.g. the EA App / Origin "Command & Conquer: The Ultimate Collection"). Don't download them.

You need a **desktop PC** to prepare the files, the phone connected by USB (with USB debugging on),
and about **10 GB of free disk** on the PC while packaging.

---

## Step 1 — Find your Zero Hour install on the PC

Look for the folder that contains a bunch of `*.big` files and a `Data` folder. It's typically:

| Source | Typical path |
|---|---|
| Retail install | `C:\Program Files (x86)\EA Games\Command & Conquer Generals Zero Hour\` |
| EA App / Origin (Ultimate Collection) | `...\Command and Conquer The Ultimate Collection\Command and Conquer Generals Zero Hour\` |
| Steam | `...\steamapps\common\Command and Conquer Generals - Zero Hour\` |

> Use the **Zero Hour** folder (the expansion), not the base *Generals* folder. This port is the
> Zero Hour engine (`GeneralsMD`).

You should see files like `INIZH.big`, `W3DZH.big`, `TexturesZH.big`, `MusicZH.big`, … and a
`Data\` subfolder.

## Step 2 — Assemble a clean "game data" folder

Make a new empty folder, e.g. `C:\gamedata\`, and copy into it:

1. **Every `*.big` file** from your Zero Hour install folder (just select all `.big` and copy).
2. The whole **`Data\` folder** (this includes `Data\INI\`, `Data\Maps\`, and `Data\Movies\` if
   you want the video cutscenes — they're optional; without them cutscenes are simply skipped).
3. The **`dxvk.conf`** that ships with this port (find it under this repo; it tunes DXVK for
   mobile). Copy it into the root of `C:\gamedata\`.

The result should look like:

```
C:\gamedata\
├── INIZH.big
├── W3DZH.big   W3DEnglishZH.big   ...        # all your *.big files
├── TexturesZH.big   TerrainZH.big   ...
├── MusicZH.big   SpeechZH.big   ...
├── Data\
│   ├── INI\
│   ├── Maps\
│   └── Movies\      (optional — *.bik cutscenes)
└── dxvk.conf
```

That's the whole configuration — no editing of INIs is required. The file names don't have to be
memorised; **copy everything ending in `.big` plus the `Data` folder** and you're done.

## Step 3 — Pack the data into the APK's assets

From the repo root, run the packaging helper (needs Python 3):

```bash
python scripts/package_selfcontained.py  C:\gamedata  android/app/src/main/assets/
```

It zips your data **stored** (uncompressed — `.big`/`.bik` are already compressed) and splits it
into `<2 GB` parts named `gamedata000.pak`, `gamedata001.pak`, … in
`android/app/src/main/assets/`. (The split is required — Android's build can't process a single
>2 GB asset. See [fixes/self-contained-packaging.md](fixes/self-contained-packaging.md).)

## Step 4 — Build and install the APK

Build the self-contained APK and install it (full toolchain details in [../BUILDING.md](../BUILDING.md)):

```bash
cd android
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

(If you don't want to build the native engine yourself, drop the prebuilt `.so` files into
`android/app/libs/arm64-v8a/` first — see [../BUILDING.md](../BUILDING.md).)

## Step 5 — First launch

Tap the **Generals Zero Hour** icon. The first time, it shows **"Installing game data…"** for
~1–2 minutes while it unpacks your data to the app's private storage, then the game boots. Every
launch after that goes straight into the game.

You need **~5 GB free on the phone** (the APK plus the extracted copy of the data).

---

## Where the files end up on the phone

For reference (you don't need to touch this — the app does it): the engine reads its data from its
own private folder, `/data/data/org.generalsx.zerohour/files/`, and `SetupActivity` is what
extracts your `.pak` parts there on first run. You **cannot** copy files there yourself with
`adb push` on a normal phone — SELinux blocks it — which is exactly why the data is bundled in the
APK. See [fixes/self-contained-packaging.md](fixes/self-contained-packaging.md).

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| Stuck on a black screen after the EA logo | A `.big` is missing. Re-copy **all** `*.big` from the Zero Hour install (Step 2). |
| "Installing game data…" then it exits | Not enough free space on the phone (needs ~5 GB), or the `.pak` parts didn't build — re-run Step 3. |
| No cutscenes | You didn't include `Data\Movies\` (optional). Add the `*.bik` files and re-pack. |
| No sound | Turn the media volume up; audio uses OpenSL ES. If still silent, see [fixes/audio-opensl.md](fixes/audio-opensl.md). |
| Wrong language text/speech | Include the matching `*EnglishZH.big` / `SpeechZH.big` (or your language's) in Step 2. |
| Gradle "Java heap space" while packaging | Keep `org.gradle.jvmargs=-Xmx4096m` in `android/gradle.properties` (already set). |

## Legal

Your retail files stay on your devices. **Never** share, upload, or attach `.big`/`.bik`/maps or a
built self-contained APK (it embeds the data) in issues, PRs, or anywhere public. Buy the game —
it keeps this project legal and alive. See [GAME-FILES.md](GAME-FILES.md).
