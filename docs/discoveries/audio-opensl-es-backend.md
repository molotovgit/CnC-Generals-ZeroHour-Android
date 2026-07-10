# OpenAL Soft on Android: the `__linux__` trap that routed audio to the null device (total silence)

> **TL;DR** — The port drives audio through OpenAL Soft, whose backend is chosen at runtime from
> the `ALSOFT_DRIVERS` environment variable. `SDL3Main.cpp:FilterPipeWireOpenAL` set that list
> under `#if defined(__linux__)` to work around a desktop PipeWire/SSE crash —
> `ALSOFT_DRIVERS=pulse,alsa,oss,jack,null,wave`. But the Android NDK defines `__linux__` too, so
> Android ran the desktop arm. None of those hardware backends are compiled into the Android
> `libopenal.so`; the one that is — `null`, an always-succeeds sink — sits right there in the list.
> OpenAL "initialized" cleanly and played every source into a bit bucket: total silence. The fix
> is a one-line intent — force `ALSOFT_DRIVERS=opensl` — but the load-bearing detail is *where* it
> goes: an `#if defined(__ANDROID__)` arm placed **before** the `#elif defined(__linux__)` arm, so
> the platform superset does not swallow it.

## Symptom (clean init, zero sound)

The audio subsystem came up looking completely healthy:

- The engine was built with `SAGE_USE_OPENAL`; `libopenal.so` was bundled in the APK and loaded.
- `alcOpenDevice()` / `alcCreateContext()` returned non-null. No `AL_*` error, no `ALC_*` error.
- Master, music, SFX and speech volumes were all non-zero. Sources were created and played.
- No crash, no exception, no missing-symbol link error.

And yet: no music, no sound effects, no unit speech — nothing, on any device. This is the worst
class of audio bug because every observable success signal is green. There is no failing call to
catch in a debugger; the pipeline reports that it is working. When *every* audio channel is silent
at once — rather than one category — the cause is almost never the mixer, the asset paths, or the
volume curves. It is the output device. OpenAL had opened a device; it had simply opened the
*wrong* one.

## Background: OpenAL Soft backend selection & `ALSOFT_DRIVERS`

OpenAL Soft is a portable software implementation of OpenAL. It does not talk to hardware
directly; it delegates to a **backend** — a platform playback plugin. Which backends exist in a
given `libopenal.so` is fixed at *compile* time (CMake feature flags decide what gets built in).
Which backend is actually *used* is decided at *runtime*, the first time a device is opened.

The runtime selection is driven by an ordered, comma-separated preference list. It can come from
`alsoft.conf` (`drivers=`) or, higher priority, from the `ALSOFT_DRIVERS` environment variable.
The algorithm is straightforward and worth internalizing:

1. Parse the list left to right into backend names.
2. For each name, look it up among the **compiled-in** backend factories. A name that was not
   built into this binary is simply skipped — it is not an error, it just isn't there.
3. Take the first backend whose factory is present and whose device successfully opens. That
   backend wins; the rest of the list is never consulted.

Two behaviours of that list matter here:

- **A trailing comma means "then try the remaining defaults."** `pulse,` tries PulseAudio, then
  falls through to every other compiled-in backend in the implementation's default order. A list
  with **no** trailing comma — like the desktop string below — is a *closed set*: only the named
  backends are ever considered. There is no implicit fallback to auto-detection.
- **`null` is a real, always-available backend.** The `null` device is a legitimate output that
  accepts a context, accepts sources, mixes, and then discards every sample. Its factory is always
  registered — it cannot fail to open. Placing `null` in a driver list is placing a guaranteed
  success there; nothing after it can ever run, and anything before it that is absent is skipped
  straight into it.

On a desktop Linux build, the compiled-in set typically includes `pulse`, `alsa`, `oss`, `jack`,
`wave`, `null` and more. On the **Android** build the useful set is exactly one hardware backend:
**OpenSL ES**, which OpenAL Soft names `opensl`. That is the backend that links `libOpenSLES.so`
and carries the `opensl` backend strings — the only real output compiled into this port's
`libopenal.so`. (`null` is also present, as it always is. `pulse`, `alsa`, `oss`, `jack`, `wave`
are not.)

## Root cause (Android is `__linux__` → desktop driver list → null device)

`SDL3Main.cpp:FilterPipeWireOpenAL` exists to pin OpenAL's backend selection before the game opens
a device. Its guard was a single platform test:

```c
// PipeWire/OpenAL workaround is Linux-only; keep macOS CoreAudio backend selection untouched.
#if defined(__linux__)
    ...
    // forced ALSOFT_DRIVERS=pulse,alsa,oss,jack,null,wave
#endif
```

The trap is one fact about the toolchain: **the Android NDK defines both `__ANDROID__` and
`__linux__`.** Android *is* Linux — same kernel, same libc lineage — so `defined(__linux__)` is
true on a phone. There was no Android-specific arm, so Android fell into the desktop arm and
inherited the desktop driver string:

```
ALSOFT_DRIVERS = pulse,alsa,oss,jack,null,wave
```

Now walk OpenAL Soft's selection algorithm against that closed list on the Android binary:

| Requested | Compiled into Android `libopenal.so`? | Outcome |
|-----------|----------------------------------------|---------|
| `pulse`   | no  | skipped |
| `alsa`    | no  | skipped |
| `oss`     | no  | skipped |
| `jack`    | no  | skipped |
| `null`    | **yes** | **opens — wins** |
| `wave`    | (never reached) | — |

The four real backends do not exist in this binary, so they are skipped without complaint. The
fifth entry is `null`, which always opens. OpenAL Soft reports success, hands back a valid device
and context, and mixes every source into the null sink. `opensl` — the one backend that would have
produced sound — was never in the list, so it was never even a candidate. This is not a fallback
gone wrong; it is a driver list that explicitly names silence and never names the hardware.

Note also the closed-set property: because the string has no trailing comma, OpenAL never
auto-probes for a working backend. Even the accidental safety net of "try everything else" was
switched off. The desktop string is a precise instruction — and on Android it precisely instructs
OpenAL to be silent.

## The fix (force the opensl backend, ordered before the linux branch)

Add an Android-specific arm to `SDL3Main.cpp:FilterPipeWireOpenAL`, and — this is the load-bearing
part — order it **before** the generic `__linux__` arm so it wins the `#if/#elif` chain:

```c
#if defined(__ANDROID__)
    // openal-soft's ONLY working playback backend on Android is OpenSL ES ("opensl").
    // Android is also __linux__, so without this the desktop branch below forced
    // ALSOFT_DRIVERS=pulse,alsa,oss,jack,null,wave — none of which exist on Android — and
    // openal-soft fell back to the "null" device => total silence. This branch must come
    // first to win over the generic __linux__ case.
    if (!getenv("ALSOFT_DRIVERS")) {
        setenv("ALSOFT_DRIVERS", "opensl", 1);
        fprintf(stderr, "INFO: OpenAL: ALSOFT_DRIVERS=opensl (Android OpenSL ES backend)\n");
    }
#elif defined(__linux__)
    /* …existing desktop PipeWire / SSE-alignment workaround, unchanged… */
#endif
```

Three design choices in that arm are deliberate:

- **`opensl`, exactly.** It is the OpenAL Soft name for the OpenSL ES backend and the only
  hardware output compiled into this port's `libopenal.so`. A single-entry list means "use this or
  nothing" — appropriate here, since there is no second real backend to fall back to. (Absent the
  fix, "nothing" was exactly what happened via `null`.)
- **`if (!getenv("ALSOFT_DRIVERS"))` — set only if unset.** The port never clobbers an explicit
  override. A developer can still export `ALSOFT_DRIVERS=null` to force silence for a repro, or
  point at another backend for debugging, without editing the binary. The port supplies a working
  default, not a mandate.
- **The `fprintf` to stderr.** It stamps the chosen backend into the app's log so the selection is
  auditable after the fact (see the next section for why stderr, specifically, is the right sink on
  this platform).

The ordering is not stylistic. `#if/#elif` stops at the first true arm. Because `__ANDROID__`
implies `__linux__`, the Android arm and the Linux arm are *both* true on a phone — so whichever is
written first wins, and the other becomes dead code on that platform. Put the `__linux__` arm
first and the `__ANDROID__` arm is unreachable; the bug survives the "fix." Android first is the
whole correction.

## Diagnosing & verifying (where the log goes)

The trap that hides this bug a second time is *where the evidence lives*. On Android a native
process's `stdout`/`stderr` are wired to `/dev/null` by default — they do **not** appear in
`logcat`. OpenAL Soft writes its own diagnostics (including the backend it selected, at higher
`ALSOFT_LOGLEVEL`) to `stderr`. So an engineer tailing `logcat` sees nothing about OpenAL at all
and reasonably concludes audio "isn't logging," when in fact the most important line — the chosen
device — was printed and discarded.

This port already solves that: early in `main()` the Android boot path `freopen`s `stderr` to an
app-private file and `dup2`s `stdout` onto it. Every `fprintf(stderr, …)` from
`FilterPipeWireOpenAL`, and every line OpenAL Soft itself emits, lands there — not in `logcat`.
Retrieve it with:

```
adb shell run-as org.generalsx.zerohour cat /data/data/org.generalsx.zerohour/ccg_log.txt
```

(`/data/user/0/…` and `/data/data/…` are the same location — `/data/user/0` is a symlink to
`/data/data` — so the file `freopen` opens under one path is the file you `cat` under the other.)

Diagnosis and verification then become a single grep against that log:

- **Before the fix** — the log carries no `opensl` line, OpenAL reports a successfully opened
  device, and audio is silent. Bumping `ALSOFT_LOGLEVEL` shows the null device being chosen.
- **After the fix** — the log contains the stamped line
  `INFO: OpenAL: ALSOFT_DRIVERS=opensl (Android OpenSL ES backend)`, OpenAL Soft initializes on
  OpenSL ES with no `AL`/`ALC` errors (only benign `pthread_setschedparam` / D-Bus warnings), and
  music, SFX and speech play.

The confirmation is behavioural, not just textual: seeing `ALSOFT_DRIVERS=opensl` in the log *and*
hearing sound together prove that the backend swapped from `null` to `opensl`. Either alone is
weaker — a log line with no audio would mean OpenSL ES opened but something downstream is muted; a
missing log line means the Android arm never ran (recheck the branch ordering).

## Why desktop is unaffected

The fix is strictly additive for every non-Android target:

- **Desktop Linux.** `__ANDROID__` is not defined, so the new arm is skipped and control falls to
  `#elif defined(__linux__)` exactly as before. That arm still forces
  `ALSOFT_DRIVERS=pulse,alsa,oss,jack,null,wave` and disables CPU extensions to dodge the desktop
  PipeWire / SSE `movaps` alignment crash the function was written for. Its behaviour is
  byte-for-byte unchanged.
- **macOS.** Neither `__ANDROID__` nor `__linux__` is defined; the whole block is skipped and the
  CoreAudio backend selection is left untouched, per the function's own guard comment.
- **Windows and others.** Unaffected — no arm applies.

The desktop driver string that caused the bug is *correct on the desktop*: those backends exist
there and `null` is a sane last-resort tail. The string was only ever wrong because it was
executed on a platform it was never meant to run on. The fix does not change the desktop string; it
stops Android from borrowing it.

## Gotchas & lessons (preprocessor branch ordering; platform supersets)

- **`__ANDROID__` implies `__linux__` — treat Android as a *subset* platform, not a sibling.** Any
  `#if defined(__linux__)` in a codebase that also builds for Android is a latent Android branch.
  Audit every one of them: does the Linux code actually make sense on a phone? Networking, audio,
  filesystem layout, threads and scheduling frequently do not.
- **Order `#if/#elif` from most specific to most general.** When guards form a superset chain
  (`__ANDROID__` ⊂ `__linux__` ⊂ POSIX), the specific test must come first or it is dead code.
  This is the single most important line of the fix, and it is invisible in the diff of intent —
  "force opensl" reads the same whether it wins or loses the chain.
- **"Initialized cleanly" is not "working."** A backend that always succeeds (`null`) turns a
  configuration error into a silent runtime one. When a whole subsystem is uniformly inert with no
  errors, suspect that it bound to a no-op device, sink, or stub — not that the data is missing.
- **Know where a native library logs, and where that sink goes on the target OS.** OpenAL Soft's
  backend choice was printed to `stderr` the whole time; on Android `stderr` is `/dev/null` unless
  redirected. Half of this diagnosis was infrastructure — capturing the log — not audio.
- **Supply defaults with `if (!getenv(...))`, don't mandate.** Setting the backend only when the
  environment hasn't already chosen keeps the door open for overrides and repros without a rebuild.

## Related

- [`../fixes/audio-opensl.md`](../fixes/audio-opensl.md) — the concise fix summary this document
  expands on.
- [`logging-storage-and-movie-bootstrap.md`](logging-storage-and-movie-bootstrap.md) — the sibling
  Android `main()` boot work: the `stderr → ccg_log.txt` redirect that makes this bug diagnosable,
  the app-private `chdir`, and the external-storage movie bootstrap.
