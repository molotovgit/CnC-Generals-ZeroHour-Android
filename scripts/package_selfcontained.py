#!/usr/bin/env python3
"""
Package your own game data into split .pak assets for the self-contained APK.

Usage:
    python package_selfcontained.py <gamedata_dir> <android_assets_dir>

<gamedata_dir> is a directory laid out like the app's internal storage, containing YOUR
retail game files:  *.big, Data/ (incl. Data/Movies), dxvk.conf
<android_assets_dir> is  android/app/src/main/assets/

It creates a STORED (uncompressed) zip of the game data and splits it into <2 GB parts named
gamedata000.pak, gamedata001.pak, ...  which SetupActivity concatenates and extracts on first
launch. (A single >2 GB asset breaks Android's asset pipeline; ~400 MB parts + a 4 GB Gradle
heap build cleanly.)

NOTE: game data is EA-copyrighted — supply your own retail files. Nothing here ships data.
"""
import os, sys, zipfile

CHUNK = 400 * 1024 * 1024  # < 2 GB Java array / Gradle heap safe

def main():
    if len(sys.argv) != 3:
        print(__doc__); sys.exit(2)
    src, assets = sys.argv[1], sys.argv[2]
    os.makedirs(assets, exist_ok=True)
    for f in os.listdir(assets):
        if f.startswith("gamedata") and f.endswith(".pak"):
            os.remove(os.path.join(assets, f))

    tmp_zip = os.path.join(assets, "_gamedata.tmp.zip")
    print(f"zipping (stored) {src} -> {tmp_zip}")
    total = 0
    with zipfile.ZipFile(tmp_zip, "w", zipfile.ZIP_STORED, allowZip64=True) as z:
        for root, _dirs, files in os.walk(src):
            for name in files:
                full = os.path.join(root, name)
                rel = os.path.relpath(full, src).replace("\\", "/")
                z.write(full, rel)
                total += os.path.getsize(full)
    print(f"  {total // (1024*1024)} MB packed; splitting into <2GB parts...")

    n = 0
    with open(tmp_zip, "rb") as f:
        while True:
            data = f.read(CHUNK)
            if not data:
                break
            out = os.path.join(assets, f"gamedata{n:03d}.pak")
            with open(out, "wb") as o:
                o.write(data)
            print(f"  wrote {os.path.basename(out)} ({len(data)} bytes)")
            n += 1
    os.remove(tmp_zip)
    print(f"done: {n} part(s) in {assets}")

if __name__ == "__main__":
    main()
