# Instrumenting the LAN path on Android (work in progress)

> Bringing up LAN multiplayer between two Android phones is **in progress** — this document is about the *instrumentation* that makes the attempt debuggable, not a working LAN feature. On a device there is no console, so `printf`/`fprintf` land nowhere by default. The port already redirects stdout/stderr to an app-private log (see sibling doc); on top of that, a set of `[LAN86]`-tagged `fprintf(stderr, ...)` breadcrumbs in `udp.cpp:UDP::Bind` and `LanLobbyMenu.cpp:LanLobbyMenuInit` were switched from commented-out to active, so NIC enumeration, `SetLocalIP`, and `UDP::Bind` success/failure become watchable in that same log. Do not read this as "LAN works" — it does not yet. Read it as "here is how you watch it fail."

## Goal: LAN between two Android devices

The target is the plain, no-account path: two Android devices on the same Wi‑Fi, one hosts a LAN game, the other sees it and joins — the same lobby the desktop game reaches through `Multiplayer → Network`.

The engine already has the whole LAN stack (`TheLAN`, the `UDP` socket wrapper, IP enumeration, the lobby UI). The port is not rewriting that stack; it is trying to get the *existing* code to bind a socket and enumerate the right local interface on Android, and to make each step observable when it doesn't.

`../PORTING-NOTES.md` lists this under **In progress** explicitly:

> **LAN multiplayer** between two Android devices (UDP broadcast/multicast discovery; the app holds a `WifiManager.MulticastLock` — see `ZerohourActivity`).

That is the honest scope of everything below. The instrumentation is finished and shipped; the feature it instruments is not.

Both sides of a session exercise the same instrumented code. Whether a device is hosting or joining, opening the LAN lobby runs `LanLobbyMenuInit`, which enumerates local IPs and drives `SetLocalIP` → `UDP::Bind`. So the `[LAN86]` breadcrumbs cover both roles with one set of trace points — you capture a log on each phone and read the same four-line vocabulary on both, which is what makes a two-device failure comparable side by side.

## The core problem: you can't see engine networking on a device

On desktop you debug the LAN path the obvious way: run the game from a terminal, watch `stderr`, set a breakpoint on `UDP::Bind`. None of that exists on a stock Android device.

- **There is no controlling terminal.** The engine runs on SDL's dedicated thread inside an ART process (see `build-shared-library-and-jni-entrypoint.md`); nothing is attached to fd 1 / fd 2. A bare `printf("bind failed")` or `fprintf(stderr, ...)` goes to a file descriptor that, by default, points at nothing — the output is simply gone. It does not even reach `logcat` on its own.
- **The failure is invisible in the UI.** When `SetLocalIP` or `UDP::Bind` fails, the LAN lobby sets an internal error flag (`LANSocketErrorDetected = TRUE`) and moves on. From the player's side you get a lobby that never lists a host — with zero indication of *why*: wrong interface picked, socket refused, bind on a bad address, or broadcast dropped by the OS.
- **A debugger on the network path is impractical mid-bringup.** You want a passive trace you can pull after the fact, not an interactive session that stalls every join attempt at a breakpoint and perturbs the timing of the very socket/discovery code you are trying to observe.

The port already solved the "output goes nowhere" half generally: early in the Android `main()` it redirects stdout/stderr into an **app-private log file** (documented in the sibling doc `logging-storage-and-movie-bootstrap.md`). That single redirect is the load-bearing fact for this whole document — it means any `fprintf(stderr, ...)` the engine emits is now captured and retrievable.

The LAN instrumentation is built directly on it: the breadcrumbs are raw `fprintf(stderr, ...)`, not GUI text and not the engine's `DEBUG_LOG` macro, precisely so they ride the stderr redirect into that log. That choice is what connects this feature to the sibling logging doc — the redirect is the pipe, the `[LAN86]` lines are what flows through it.

## The `[LAN86]` breadcrumbs (UDP::Bind, NIC enumeration, SetLocalIP)

The instrumentation is deliberately dumb: fixed-string `fprintf(stderr, ...)` calls, each prefixed with the grep tag `[LAN86]`, placed at the exact decision points of the LAN bring-up. They already existed in the source as `/* ... */`-commented lines; the change is to **un-comment them** (plus add one explicit failure line). No logic moved — the diff only makes latent trace points compile and run on device.

### One tag across two subsystems

`[LAN86]` is a single grep anchor spanning code that lives in two different trees: the socket layer in `Core/GameEngine/Source/GameNetwork/udp.cpp` and the lobby UI in `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/LanLobbyMenu.cpp`. Tagging both with the same literal means one filter over the app-private log reconstructs the whole attempt — UI-side interface selection *and* the socket-side bind result — in the order they happened, without correlating two different log vocabularies. Keep the tag stable and grep-clean; it is the index into an otherwise noisy engine log.

### `Core/GameEngine/Source/GameNetwork/udp.cpp:UDP::Bind`

`UDP::Bind` is where a local socket is created and bound to an IP/port. Two failure sites — `socket()` returning an error and `bind()` returning an error — are each already logged through `DEBUG_LOG(...)`; the change activates the `[LAN86]` stderr twin next to each so the same information reaches the app-private log:

```c
  fprintf(stderr, "[LAN86] UDP::Bind socket failed %d.%d.%d.%d:%d err=%d\n",
    (ipHostOrder >> 24) & 0xFF, (ipHostOrder >> 16) & 0xFF, (ipHostOrder >> 8) & 0xFF, ipHostOrder & 0xFF,
    portHostOrder, m_lastError);
```

```c
  fprintf(stderr, "[LAN86] UDP::Bind bind failed %d.%d.%d.%d:%d err=%d\n",
    (ipHostOrder >> 24) & 0xFF, (ipHostOrder >> 16) & 0xFF, (ipHostOrder >> 8) & 0xFF, ipHostOrder & 0xFF,
    portHostOrder, m_lastError);
```

What each line gives you on device:

- the **IP** it tried to bind, decoded to a dotted quad from host order (`(ipHostOrder >> 24) & 0xFF`, …) — so you can tell immediately whether the engine picked the phone's Wi‑Fi address or something bogus (loopback, `0.0.0.0`, a stale interface);
- the **port** (`portHostOrder`);
- the platform **error** (`m_lastError`) — the `errno` from the failing `socket()` / `bind()` call, which is what distinguishes the common Android failure modes without guessing.

The `err=%d` value is the whole reason to print rather than infer. On bionic it is a standard POSIX `errno`, and the three you actually expect while bringing this up map to distinct root causes:

- `EADDRNOTAVAIL` — you tried to bind an address that is not on any live interface. Almost always means enumeration handed `SetLocalIP` the wrong IP (a down interface, or an address from before Wi‑Fi associated).
- `EACCES` / `EPERM` — the OS refused the socket or bind. On Android this points at a missing capability or policy rather than a bug in the address logic.
- `EADDRINUSE` — the port is already held. Relevant when a previous session did not release cleanly, or two components fight over the same bind.

These lines fire only on the failure branch and then `return` — a successful bind is silent here. Their whole reason to exist is that on Android the `DEBUG_LOG` sibling is not something you can watch live, whereas the `[LAN86]` stderr line is captured in the app-private log every run.

### `GeneralsMD/…/Menus/LanLobbyMenu.cpp:LanLobbyMenuInit`

This is the front of the pipeline: when the LAN lobby opens, it enumerates the machine's local IPs and asks `TheLAN` to bind to the preferred one. Three breadcrumbs cover it.

**NIC enumeration.** The loop walks the `EnumeratedIP` list (`IPlist`) comparing each candidate against the preferred address (`foundPreferredIP` is set on a match). The activated line prints every candidate the engine sees:

```c
fprintf(stderr, "[LAN86] LanLobbyMenuInit enumerated candidate %d.%d.%d.%d\n",
    PRINTF_IP_AS_4_INTS(candidate->getIP()));
```

On a device with several interfaces (Wi‑Fi, cellular, a `p2p`/`rmnet` link) this is the single most useful trace: it tells you *what the engine believes the local addresses are* before it commits to one. If the Wi‑Fi address never appears here, the problem is upstream of the socket entirely — it is in interface enumeration, not in `bind`.

**`SetLocalIP` begin / FAILED.** After `TheLAN->init()`, the lobby calls `SetLocalIP(IP)` with the chosen address. The change activates a begin breadcrumb and adds an explicit failure breadcrumb inside the error branch:

```c
fprintf(stderr, "[LAN86] LanLobbyMenuInit SetLocalIP begin %d.%d.%d.%d\n", PRINTF_IP_AS_4_INTS(IP));
if (TheLAN->SetLocalIP(IP) == FALSE) {
    LANSocketErrorDetected = TRUE;
    fprintf(stderr, "[LAN86] LanLobbyMenuInit SetLocalIP FAILED %d.%d.%d.%d\n", PRINTF_IP_AS_4_INTS(IP));
    ...
```

`PRINTF_IP_AS_4_INTS` expands the address into its four octet arguments, matching the dotted-quad format the `%d.%d.%d.%d` string expects. It is the same conversion the `udp.cpp` hunk does by hand with bit shifts — both ends print the address the same way, so a candidate line and a bind line for the same IP read identically in the log.

Together with the `UDP::Bind` pair, the trio lets you follow one join attempt end to end:

```
[LAN86] LanLobbyMenuInit enumerated candidate <ip> …     (what interfaces were seen)
[LAN86] LanLobbyMenuInit SetLocalIP begin <ip>           (which one we committed to)
[LAN86] UDP::Bind socket/bind failed <ip>:<port> err=<n> (where and why the socket died)
[LAN86] LanLobbyMenuInit SetLocalIP FAILED <ip>          (the lobby's own verdict)
```

That sequence is the artifact of this feature so far — a readable failure trace, not a working join. (One further `[LAN86]` "SetLocalIP failed" line below the `DEBUG_LOG` is intentionally left commented as a duplicate; the active failure breadcrumb is the uppercase `FAILED` one above.)

### Reading a trace from the app-private log

Because everything routes through the stderr redirect, the workflow is entirely offline: reproduce the join attempt on the device, then retrieve the app-private log over `adb` and filter it for the tag. `grep '\[LAN86\]'` collapses a full engine log down to just the LAN timeline above. You are looking for three things, in order: does the phone's real Wi‑Fi address appear as an **enumerated candidate**; does `SetLocalIP` reach **begin** and then avoid **FAILED**; and if a bind failed, what `err=` value `UDP::Bind` reported. Each answer points at a different layer, which is exactly why the breadcrumbs are placed at layer boundaries rather than lumped into one message.

### Relationship to `DEBUG_LOG`

Each activated breadcrumb sits next to a pre-existing `DEBUG_LOG(...)` carrying the same facts — the port did not replace the engine's logging, it added a device-visible parallel. The two are complementary, not redundant: `DEBUG_LOG` is the engine's structured, build-configurable debug channel that a desktop developer already relies on, while the `[LAN86]` `fprintf(stderr, ...)` line is the copy that is guaranteed to reach the app-private log through the redirect. On desktop you would lean on `DEBUG_LOG`; on device you read `[LAN86]`. Keeping both in the source means the same investigation reads the same way on either platform.

### Scope limits of the current breadcrumbs

Be clear about what this instrumentation does and does not observe, so a clean trace is not mistaken for a working feature:

- It covers **interface enumeration**, **`SetLocalIP`**, and **`UDP::Bind` success/failure** — the local-socket bring-up only.
- It does **not** trace packet send/receive, discovery announcements, the host list the lobby builds, or any handshake between two devices. A successful bind logs nothing here at all (the `UDP::Bind` breadcrumbs are failure-only), so "no `[LAN86]` errors" means "the local socket came up", not "a peer was found".
- It says nothing about whether the `MulticastLock` is actually suppressing frame filtering at runtime — that is a Java/OS-level concern the C++ breadcrumbs cannot see.

In other words, the breadcrumbs get you cleanly to the *edge* of the unknown — a bound socket on the right interface — and stop there. Everything past that edge is still open.

## Android specifics (MulticastLock; app-private log)

Two Android facts shape both the instrumentation and why LAN is hard here.

**stdout/stderr are invisible — so breadcrumbs must be `fprintf(stderr, ...)` captured in the app log.** This is not a stylistic choice. The engine's normal `DEBUG_LOG` path is not convenient to tail on a phone, and UI-level error text tells a player nothing actionable. The port's stdout/stderr → app-private-log redirect (sibling doc) is the *only* reason a plain `fprintf(stderr, ...)` is worth writing on device — it converts "output to a dead fd" into "a line in a file you can pull off the device". Every `[LAN86]` breadcrumb is written that way on purpose.

If you add more LAN tracing, use `fprintf(stderr, ...)` for the same reason: a `printf` or a `std::cout` rides the same redirect and interleaves in the same timeline, whereas a Java-side `Log.d` would not correlate with the engine's own ordering.

**UDP broadcast/multicast requires a held `WifiManager.MulticastLock`.** By default, Android's Wi‑Fi stack filters multicast and broadcast frames that aren't addressed to the device, to save power — which is exactly the traffic LAN game discovery depends on. Unless the app explicitly acquires a `WifiManager.MulticastLock` (and holds it for the duration), those discovery packets are silently dropped below the socket layer, so the peer never appears even when `UDP::Bind` on both ends succeeds. Per `../PORTING-NOTES.md`, the app holds that lock in `ZerohourActivity`.

The two facts compound. Holding the `MulticastLock` is a necessary precondition for discovery to have any chance — but it does not by itself make the C++ LAN path bind the right interface or complete a handshake. And a dropped-frame problem is invisible at the socket API: both ends can log a clean bind and still never see each other. That is the case the `[LAN86]` breadcrumbs *cannot* diagnose on their own — a clean bind trace with no discovery is precisely the signal that the next thing to check is the lock and the Wi‑Fi frame filtering, not the socket code.

## Current status & what's next

Status, stated plainly:

- **Done:** the LAN path is now *traceable on device*. `UDP::Bind` failures (with IP, port, `errno`), the enumerated NIC candidates, and `SetLocalIP` begin/FAILED all reach the app-private log via activated `[LAN86]` stderr breadcrumbs. The `WifiManager.MulticastLock` is held in `ZerohourActivity` so multicast/broadcast frames are not filtered away.
- **Not done:** LAN multiplayer between two Android devices does not work yet. This document makes **no** claim that discovery, join, or an in-game LAN session functions. The breadcrumbs are a debugging aid for reaching that state, not evidence of it.

Leaving the breadcrumbs active costs effectively nothing, which is why they can stay on during bring-up rather than hiding behind a build flag. The `UDP::Bind` lines are on failure branches that immediately `return` — they never run on a healthy bind. The `LanLobbyMenuInit` lines run once, when the LAN lobby opens, over a short interface list. There is no per-frame or per-packet tracing here, so the instrumentation adds no hot-path overhead to gameplay or to a working session.

What the instrumentation is meant to drive next: run the LAN lobby on two phones, pull each app-private log, and read the `[LAN86]` sequence to answer the first open questions — does the phone's Wi‑Fi address show up as an enumerated candidate; does `SetLocalIP`/`UDP::Bind` succeed or fail (and with what `err=`); and, once binds succeed on both ends, whether discovery packets actually cross with the `MulticastLock` held. Each of those is a distinct failure mode the trace can now separate. Until that loop closes, treat LAN as an in-progress bring-up, exactly as `../PORTING-NOTES.md` labels it.

## Related

- Sibling discovery `logging-storage-and-movie-bootstrap.md` — the stdout/stderr → app-private-log redirect that every `[LAN86]` breadcrumb here depends on; without it these `fprintf(stderr, ...)` lines would go nowhere on device.
- `build-shared-library-and-jni-entrypoint.md` — why the engine runs on SDL's thread inside an ART process with no console in the first place (the root cause of the "you can't see stderr" problem).
- `../PORTING-NOTES.md` — the **In progress** section that lists LAN multiplayer (UDP broadcast/multicast; `WifiManager.MulticastLock` in `ZerohourActivity`) as an open, unfinished feature.
