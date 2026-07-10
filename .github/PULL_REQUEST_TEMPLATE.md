<!-- Thanks for contributing! Please fill this in so reviews go quickly. -->

## What & why

<!-- What does this change, and what problem does it solve? For fixes, describe the
     symptom → root cause → fix, the same style as the commit history. -->

## Subsystem

<!-- rendering / input / audio / minimap / cutscene / packaging / network / build / docs / other -->

## Testing

- Device model + SoC/GPU:
- Android version:
- What you verified (booted / main menu / skirmish / mission / …):

## Checklist

- [ ] **No game data** is added or linked (`.big`, `.bik`, maps, ISOs, or a built self-contained APK).
- [ ] Android-specific engine changes are guarded (`#if defined(__ANDROID__)` / the existing
      `!defined(_WIN32)` convention) so desktop builds are unaffected.
- [ ] Existing per-file **copyright / GPLv3 license headers** are kept intact.
- [ ] Tested on a real `arm64-v8a` device (kept landscape-locked), or this is docs/build-only.
- [ ] Commits use conventional-ish prefixes (`fix:` / `feat:` / `docs:` / `build:` / `chore:`).
- [ ] I understand this contribution is licensed under **GPLv3**.
