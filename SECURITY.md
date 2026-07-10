# Security Policy

This is a community port of a game engine. The realistic attack surface is small, but a few areas
do process untrusted input and are worth reporting if you find a flaw:

- The **network code** (LAN discovery / multiplayer — UDP parsing).
- The **self-contained asset extraction** (`SetupActivity` unpacking `.pak` parts into private
  storage — path handling, zip traversal).
- Any **memory-safety** issue reachable from crafted maps, replays, or INI data.

## Supported versions

This project tracks a single rolling line. Fixes land on **`main`**; there are no long-term
maintenance branches. Always test against the latest `main`.

| Version | Supported |
|---|---|
| `main` (latest) | ✅ |
| older commits / tags | ❌ |

## Reporting a vulnerability

**Please do not open a public issue for security problems.**

Use GitHub's private reporting: go to the repository's **Security** tab →
**"Report a vulnerability"** (Private Vulnerability Reporting), or contact the maintainer
[@molotovgit](https://github.com/molotovgit) directly.

Include, if you can:

- The affected subsystem (network / packaging / rendering / …) and commit hash.
- Steps or a proof-of-concept to reproduce.
- Device model, SoC/GPU, and Android version.

You can expect an acknowledgement as time permits — this is a volunteer, non-commercial project,
so please be patient. Coordinated disclosure is appreciated: give a fix a reasonable window before
going public.

## Please never attach game data

Do **not** include EA-copyrighted game data (`.big`, `.bik`, maps, ISOs) in a report, PoC, or
anywhere else. If a repro needs game assets, describe them — don't upload them.
