# Filling bionic / NDK libc++ gaps

> **TL;DR** — Three tiny, unrelated-looking build breaks share one root cause: Android's C
> library (**bionic**) and the NDK's C++ standard library (**LLVM libc++**) omit things that
> glibc and MSVC provide. `pthread_cancel` is absent from bionic, `<sys/timeb.h>` does not exist
> in bionic, and libc++ ships no floating-point `std::from_chars` overload. All three fail at
> **build time**, not runtime — a header that will not resolve, an identifier that will not
> compile/link, and a template that will not instantiate. Each fix is a narrow `#if
> defined(__ANDROID__)` (or `if constexpr`) that removes the dependency without changing behavior
> on any platform. The `from_chars` one has a subtlety worth reading twice: routing floats to
> `strtod` is not enough — you must *also* stop the compiler from instantiating the missing
> overload.

These are not algorithmic problems. They are the friction a large legacy Win32/POSIX codebase
hits the first time it is pointed at the NDK toolchain: bionic and libc++ are POSIX-ish and
C++17-ish, but "-ish" is where a million-line engine snags. This note documents the three we hit,
why each is missing specifically on Android, what breaks and when, and why each patch is safe.

## The three gaps

| Gap | `path:function` | Missing on Android because | Breaks at | Fix |
|-----|-----------------|----------------------------|-----------|-----|
| `pthread_cancel` | `GeneralsMD/Code/CompatLib/Source/thread_compat.cpp:TerminateThread` | bionic deliberately does not implement thread cancellation | **Build** — undeclared identifier / no symbol | `#if defined(__ANDROID__)` → best-effort no-op returning `0` |
| `<sys/timeb.h>` | `Core/Libraries/Source/WWVegas/WWDownload/FTP.cpp` (top-of-file includes) | header is a removed POSIX legacy interface; bionic never shipped it | **Build** — preprocessor, `file not found` | `#if !defined(__ANDROID__)` around the `#include` |
| float `std::from_chars` | `Core/GameEngine/Source/Common/INI/INI.cpp:scanType` | libc++ shipped only the integer overloads | **Build** — template instantiation, no matching function | route floats to `strtod` **and** `if constexpr` guard the `from_chars` call |

The common shape: a symbol or header the desktop build takes for granted simply is not there. None
of these produce a wrong answer at runtime that you would chase with a debugger — they stop the
compiler or linker cold. That is the good news. The bad news is that a codebase this size hides
dozens of them behind `#ifdef _WIN32` walls and platform assumptions, so you find them one failed
translation unit at a time.

---

### `pthread_cancel`

**Symptom.** `CompatLib` is the engine's Win32→POSIX shim layer. It reimplements a handful of
Win32 threading calls (`CreateThread`, `GetCurrentThreadId`, `TerminateThread`) on top of
pthreads so the rest of the game can keep calling the Windows-shaped API. `TerminateThread`
mapped straight onto `pthread_cancel`:

```cpp
int TerminateThread(void *hThread, unsigned long dwExitCode)
{
	return pthread_cancel((pthread_t)hThread);
}
```

On Linux/glibc and Apple's libc this compiles and links. On Android the build fails — bionic's
`<pthread.h>` neither declares nor defines `pthread_cancel`, so the reference to it does not
resolve. This is a compile/link failure, never something you reach at runtime.

**Why Android.** bionic intentionally omits POSIX thread cancellation. Deferred/asynchronous
cancellation is famously difficult to implement safely — it interacts badly with C++ destructors,
locks held across cancellation points, and unwind state — and the bionic maintainers chose not to
ship it rather than ship a footgun. There is no `pthread_cancel`, `pthread_setcancelstate`, or
`pthread_testcancel` on Android. glibc provides all of them; that is the divergence.

**Fix (`thread_compat.cpp:TerminateThread`).** Guard the Android path to a best-effort no-op:

```cpp
#if defined(__ANDROID__)
	// GeneralsX @port Android — bionic libc has no pthread_cancel; forced thread
	// termination is unsupported (threads exit cooperatively on shutdown). Best-effort no-op.
	(void)hThread; (void)dwExitCode;
	return 0;
#else
	return pthread_cancel((pthread_t)hThread);
#endif
```

The parameters are cast to void to silence unused-argument warnings, and the function returns `0`
(success) so callers that check the result proceed normally.

**Why it's safe.** Forcibly terminating another thread is dangerous *everywhere*, not just on
Android — the Win32 `TerminateThread` this shim emulates is documented as a last resort because it
leaves locks held, skips destructors, and can corrupt the target thread's state. The engine only
reaches this path during shutdown, where its worker threads already exit cooperatively; the OS
reclaims every remaining thread when the process exits regardless. So "do nothing and report
success" is behaviorally indistinguishable from the desktop outcome for the way this codebase
actually uses the call. Nothing depends on the thread being torn down *early* — only on shutdown
completing, which it does. The `#else` branch keeps desktop/glibc behavior byte-for-byte
unchanged.

---

### `<sys/timeb.h>`

**Symptom.** `FTP.cpp` (the WWDownload FTP client) opens with a block of platform includes:

```cpp
#include <stdio.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <stdlib.h>
```

On Android the compile dies on the third line — bionic has no `<sys/timeb.h>`, so the preprocessor
reports the header as not found and never gets to the code. This is the earliest possible failure:
a header that does not exist stops the translation unit before a single statement is parsed.

**Why Android.** `<sys/timeb.h>` declares `struct timeb` and `ftime()`, an old BSD-era clock
interface. POSIX marked `ftime()` **LEGACY** in POSIX.1-2001 and **removed** it in POSIX.1-2008,
superseded by `gettimeofday()` and later `clock_gettime()`. glibc still ships the header for
backward compatibility with ancient code; bionic, being a newer libc with no such legacy debt,
simply never provided it. The header's absence on Android is not a bug — it is bionic declining to
carry a deleted interface.

**Fix (`FTP.cpp`).** Guard the include out on Android:

```cpp
#if !defined(__ANDROID__)  // GeneralsX @port Android — <sys/timeb.h> absent in bionic (and unused here)
#include <sys/timeb.h>
#endif
```

**Why it's safe.** The parenthetical in the comment is the whole argument: *and unused here.*
Nothing in this translation unit references `struct timeb` or `ftime()` — the include was
vestigial, inherited from the original source and never pruned. Removing it on Android changes no
behavior because there was no behavior attached to it. The desktop build keeps the include so its
compile output is untouched. If a future edit to `FTP.cpp` did need a millisecond clock, the
correct Android answer is `clock_gettime(CLOCK_MONOTONIC, ...)`, not resurrecting the legacy header
— but today the safest fix is also the smallest: compile the dead include only where it still
exists.

---

### `std::from_chars` for floats

**Symptom.** `INI.cpp:scanType` is the templated primitive that parses one token from an INI file
into a typed value — `Int`, `UnsignedInt`, `Real`, and so on. The generic path used
`std::from_chars` for everything:

```cpp
std::conditional_t<std::is_integral_v<Type>, Int64, Type> result{};
const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), result);

if (ec != std::errc{})
{
	throw INI_INVALID_DATA;
}
```

Instantiate that template with a floating-point `Type` and for the integral case `result` is
`Int64`, but for a float `Type` `result` is the float itself — so the compiler is asked for
`std::from_chars(const char*, const char*, float&)`. That overload does not exist in the NDK's
libc++, and the build fails with a "no matching function" error the moment `scanType<Real>` is
instantiated. Again: compile time, not runtime.

**Why Android.** C++17 specifies `from_chars` for both integer and floating-point types, but the
floating-point overloads landed in the standard-library implementations years after the integer
ones. libc++ — the standard library the NDK ships — carried only the integer overloads for a long
time; the float ones are a recent addition and are not present in the libc++ bundled with the NDK
toolchain used for this port. This is the *same* gap Apple's platform libc++ has, which is why the
existing fix was already `#if defined(__APPLE__)` before Android joined it. glibc + libstdc++ (the
desktop build) has the float overloads, so desktop never noticed.

**Fix (`INI.cpp:scanType`) — two parts, both required.** First, in the
`if constexpr (std::is_floating_point_v<Type>)` branch, route floats through `strtod` on Android
just as on Apple:

```cpp
// GeneralsX @bugfix BenderAI Apple SDKs in our deployment target do not expose std::from_chars for floats.
// GeneralsX @port Android — NDK libc++ likewise has no float std::from_chars overload; use strtod.
#if defined(__APPLE__) || defined(__ANDROID__)
const std::string tokenString(token);
char *end = nullptr;
const double result = std::strtod(tokenString.c_str(), &end);
```

Second — and this is the part that is easy to miss — wrap the *later* `from_chars` call so it is
never instantiated for float types:

```cpp
// GeneralsX @port Android — NDK libc++ has no float std::from_chars overload; floating types
// already returned via the strtod path above, so only instantiate from_chars for non-float types.
if constexpr (!std::is_floating_point_v<Type>)
{
	const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), result);

	if (ec != std::errc{})
	{
		throw INI_INVALID_DATA;
	}
}
```

**The subtlety.** It is tempting to think the `strtod` branch alone fixes floats — after all, the
float branch does its parse and returns, so the `from_chars` call "can't be reached" for a float.
But *reachability at runtime is not the same as instantiation at compile time.* A function template
is type-checked and compiled **as a whole** for every type it is instantiated with. The
`std::from_chars(...)` line sits outside any `if constexpr`, so the compiler still has to compile it
for `Type = Real` even though that code never runs for floats — and compiling it means resolving
`from_chars(const char*, const char*, float&)`, which does not exist. The build breaks regardless of
the early return.

`if constexpr (!std::is_floating_point_v<Type>)` is what actually fixes it: a *discarded*
`if constexpr` branch is not instantiated for the false type. Wrapping the `from_chars` call means
that for float `Type` the branch is dropped at compile time and the missing overload is never
named. So the two edits do two different jobs: the `strtod` block gives floats a working parse
path, and the `if constexpr` guard stops the compiler from tripping over the overload that only
exists for integers. Remove either one and Android fails to build.

**Why it's safe.** Integers are untouched — they still take the `from_chars` path with identical
error handling (`ec != std::errc{}` → `throw INI_INVALID_DATA`). Floats on Android now take the
exact path Apple builds have shipped and tested, so Android inherits already-vetted behavior rather
than something new. The `if constexpr` guard removes only a *compile-time* instantiation of a branch
floats never execute at runtime, so no runtime behavior changes for any type on any platform. One
thing to keep in mind for future edits: `strtod` honors the current C locale's decimal separator
where `from_chars` is locale-independent — for the engine's default-locale INI parsing this matches
desktop behavior, but it is the one place these two parse paths could diverge if someone ever
switched `LC_NUMERIC`.

---

## Lessons for porters

- **bionic is not glibc, and libc++ is not libstdc++.** Both are close enough to lull you and
  different enough to bite you. When a translation unit that builds on Linux fails on Android,
  suspect a missing symbol, a missing header, or a missing standard-library overload *before* you
  suspect your own code. All three cases here were the platform, not the port.

- **These gaps surface at build time — treat that as a gift.** A missing `pthread_cancel`, a
  missing header, and a missing template overload all stop the compiler or linker. That is far
  cheaper to diagnose than an ABI mismatch or a silent runtime divergence. The compiler is telling
  you exactly which platform assumption failed; read the error literally.

- **"Unreachable at runtime" ≠ "not compiled."** The `from_chars` case is the reusable lesson.
  Templates instantiate every non-discarded statement for every type, regardless of runtime
  reachability. When a call is only valid for *some* of the types a template serves, `if constexpr`
  is the tool that prevents instantiation for the rest — an early `return` does not.

- **Prefer the narrowest guard that removes the dependency.** Each fix is one `#if
  defined(__ANDROID__)` or one `if constexpr`. None of them refactor the desktop path, so desktop
  and Apple output is provably unchanged and the diff is trivial to review. Reach for a
  cross-platform abstraction only when a gap recurs; a one-off gap deserves a one-line guard.

- **Check whether the missing thing is even used.** `<sys/timeb.h>` was a dead include — the safest
  fix was to compile it only where it still exists rather than to find a bionic replacement for
  functionality nobody called. Confirm the dependency is real before you engineer around it.

- **Route to the old, portable primitive when the new one is absent.** `strtod` predates
  `from_chars` by decades and is present everywhere. When a modern standard-library facility has a
  hole on your target, the classic C function it replaced is usually the safe fallback — and if
  another platform (here, Apple) already took that route, share the branch instead of inventing a
  third one.

## Related

- `GeneralsMD/Code/CompatLib/Source/thread_compat.cpp` — the full Win32→POSIX threading shim
  (`CreateThread`, `GetCurrentThreadId`, `TerminateThread`); the `pthread_cancel` gap lives here.
- `GeneralsMD/Code/CompatLib/Include/socket_compat.h` — sibling compat layer mapping Winsock onto
  POSIX sockets, referenced by `FTP.cpp`; the same "fill the bionic gap" pattern applied to
  networking.
- `Core/GameEngine/Source/Common/INI/INI.cpp:scanType` — the templated INI token parser; see also
  the pre-existing `__APPLE__` float-parsing fix that Android's `__ANDROID__` branch now joins.
- `docs/PORTING-NOTES.md` — overview of the Win32/glibc → NDK porting effort and where the compat
  shims fit.
