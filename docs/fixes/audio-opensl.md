# Audio: wrong OpenAL backend on Android (total silence)

## Symptom

No audio at all — no music, SFX or speech — even though the audio subsystem initialized without
errors and volumes were non‑zero.

## Background

The port uses **OpenAL Soft** (`SAGE_USE_OPENAL`); `libopenal.so` is bundled and *does* contain
the Android **OpenSL ES** backend (verified: it links `libOpenSLES.so` and has the `opensl`
backend strings).

## Root cause

`SDL3Main.cpp` had a helper (`FilterPipeWireOpenAL`) that forces a driver list to avoid a
PipeWire crash on Linux. It was guarded by `#if defined(__linux__)` — and **Android is also
`__linux__`**. So on Android it ran the desktop branch and set:

```
ALSOFT_DRIVERS = pulse,alsa,oss,jack,null,wave
```

None of those backends exist on Android. openal‑soft tried them, they all failed, and it fell
back to the **`null` device** — i.e. total silence. The one backend that works on Android
(`opensl`) was never in the list.

## Fix

Add an Android branch (before the generic `__linux__` one) that selects the OpenSL ES backend:

```c
#if defined(__ANDROID__)
    if (!getenv("ALSOFT_DRIVERS"))
        setenv("ALSOFT_DRIVERS", "opensl", 1);
#elif defined(__linux__)
    /* …existing desktop PipeWire workaround… */
#endif
```

Verified: the log now shows `ALSOFT_DRIVERS=opensl`, openal‑soft initializes with no errors
(only benign `pthread_setschedparam`/`D‑Bus` warnings), and the game plays audio.

> Note: openal‑soft's own verbose log goes to **stderr**, which `SDL3Main.cpp` redirects to an
> app‑private log file — not to `logcat`. That's where to look when debugging audio.
