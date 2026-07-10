# Minimap availability on a phone: a single-player QoL relaxation that preserves competitive parity

> **TL;DR** — On desktop Generals the minimap draw is radar-gated: with no powered radar
> building the minimap stays blank. That is correct for skirmish and multiplayer — radar is a
> structure/upgrade you must build and power, and denying map awareness until you do is part of the
> balance. On a phone the minimap is the *primary* navigation tool, so blanking it cripples campaign
> play. The fix in `GameUtility.cpp:localPlayerHasRadar` adds an `#if defined(__ANDROID__)` branch
> that keeps the terrain minimap available **only** when `TheGameLogic->isInSinglePlayerGame()` is
> true — single-player campaign. Skirmish and multiplayer fall through to the unchanged desktop
> gate, so their behavior is byte-for-byte identical to stock. A scripted radar-hide
> (`TheRadar->isRadarHidden`) short-circuits both paths, so mission scripting that deliberately
> blanks the minimap is still honored. This is the visibility-**gate** half of the minimap work; the
> radar **texture** fixes are documented separately (see [Related](#related)).

## Desktop behavior: the minimap is radar-gated

The minimap terrain draw is gated on `rts::localPlayerHasRadar()` — the HUD's left panel
(`W3DLeftHUDDraw` → `W3DRadar::draw`, per [../fixes/minimap-radar.md](../fixes/minimap-radar.md))
only paints the terrain overview when that predicate returns true. When it returns false, the
minimap panel stays blank.

The stock predicate is a single condition:

```cpp
if (!TheRadar->isRadarHidden(index) && player->hasRadar())
    return true;

return false;
```

Two things must both hold for the minimap to light up:

- **`player->hasRadar()`** — the local player owns a *powered* radar capability. In Generals, radar
  is not free map awareness; it is a structure/upgrade you build and keep powered. Lose power, lose
  the structure, or never build it, and `hasRadar()` is false.
- **`!TheRadar->isRadarHidden(index)`** — the radar is not being suppressed by a scripted hide.
  Mission scripting can force the minimap off (`isRadarHidden`) regardless of whether the player has
  a radar; that hide takes precedence.

If either fails, the function returns false and the minimap is blank. This is deliberate on desktop:
denying map-wide situational awareness until a player invests in — and defends the power for — a
radar structure is a real cost, and it is symmetric across all players in a match. The gate *is* a
piece of the game's economy.

## Why touch changes the priorities

The desktop assumption underneath radar-gating is that the minimap is a *convenience*: even with it
blanked, a mouse-and-keyboard player still scrolls with edge-pan and arrow keys, jumps with control
groups and hotkeys, and reads the main viewport directly. The minimap is one navigation aid among
several, so gating it behind radar costs awareness without costing basic *control* of the game.

On a phone that assumption breaks. There is no edge-of-screen mouse pan, no keyboard, no hardware
hotkey row — the minimap is the primary way you move the camera across the battlefield: tap or drag
the overview to jump the view. Blanking it does not just remove a convenience; it removes the main
navigation surface. A campaign mission with no built radar (early missions, or any stretch before
the player has a radar structure) becomes far harder to *navigate* than the desktop designers ever
intended, because the desktop penalty for "no radar" silently grew from "less awareness" into "no
map-driven camera movement" on touch.

So the phone port has a UX problem the desktop never had: the same radar-gating rule that is a fair
balance cost on desktop is, on touch, an accessibility cliff in the one mode — the story campaign —
where competitive balance is not even a concern.

## The fix: relax only in single-player campaign

The change adds an Android-only branch to `GameUtility.cpp:localPlayerHasRadar`, placed after the
stock desktop check and before the final `return false`:

```cpp
#if defined(__ANDROID__)
	// GeneralsX @feature Android: in SINGLE-PLAYER CAMPAIGN missions keep the terrain minimap
	// available for touch navigation even without a powered radar building (mobile QoL). SKIRMISH
	// and MULTIPLAYER intentionally keep the normal desktop radar-gating — the minimap stays blank
	// until you build/power a radar structure, which is the expected competitive behavior.
	// An explicit scripted hide (isRadarHidden) is still respected.
	if (!TheRadar->isRadarHidden(index)
		&& TheGameLogic && TheGameLogic->isInSinglePlayerGame())
		return true;
#endif
```

Read it against the control flow of the whole function:

1. The **stock desktop check runs first, unchanged**. If the player actually has a powered radar and
   the radar is not scripted-hidden, the function returns true here on every platform — Android
   included. Building a radar in a campaign mission works exactly as it always did.
2. Only if that first check does *not* return does execution reach the Android branch. This branch
   fires when two conditions hold: the radar is **not** scripted-hidden
   (`!TheRadar->isRadarHidden(index)`), **and** the game is a single-player game
   (`TheGameLogic && TheGameLogic->isInSinglePlayerGame()`). The `TheGameLogic` null-guard is there
   because the predicate can be queried before game logic exists; without a live `TheGameLogic` the
   branch simply does not claim the player has radar.
3. If neither the desktop check nor the Android branch returns true, control falls to the original
   `return false` and the minimap stays blank — same as stock.

The net effect: in single-player campaign, the minimap terrain is available for touch navigation
*even without a powered radar building*, because the single-player branch supplies the "true" that
`hasRadar()` did not. In every other mode the branch is inert, and the function's answer is whatever
the stock desktop check produced.

Note the guard is `#if defined(__ANDROID__)` — narrower than the `#if !defined(_WIN32)` used by the
texture-side radar fixes. The visibility relaxation is a deliberate *mobile-input* accommodation, not
a translation-layer correctness fix, so it is scoped to the platform whose input model actually needs
it. No desktop or other non-Windows build compiles this branch.

## Preserving competitive parity (skirmish/MP unchanged; scripted hides respected)

The reason this relaxation is safe is that its scope is drawn exactly around the case where balance
does not apply, and nowhere else.

**Skirmish and multiplayer are untouched.** `isInSinglePlayerGame()` is false in those modes, so the
Android branch's second operand is false and the branch never returns true. What remains is the stock
desktop check — `!isRadarHidden(index) && player->hasRadar()` — bit-for-bit the same predicate a
Windows build runs. A skirmish or multiplayer player on the phone therefore sees the minimap blank
until they build and power a radar structure, identical to desktop. The competitive rule that "map
awareness costs a radar" is preserved precisely because the relaxation cannot fire in a competitive
game. There is no code path in which the phone hands a skirmish/MP player free radar awareness.

**Scripted hides are still honored.** Both the stock check and the Android branch lead with
`!TheRadar->isRadarHidden(index)`. A mission that scripts the radar hidden makes that operand false
in *both* conditions, so both fall through to `return false` and the minimap goes blank — even in a
single-player campaign, even on Android. The QoL relaxation only ever grants the minimap that the
*radar-power* rule would have denied; it never overrides a *scripting* decision to blank the map.
Mission designers who deliberately deny the minimap for a beat of a campaign keep that authorial
control intact on the phone.

Those two properties are what make the fix a genuine parity-preserving change rather than a balance
edit: the only behavior that differs from desktop is "single-player campaign minimap is visible
without a radar building," and that behavior is unreachable in any mode where players compete against
each other or where scripting has asked for the map to be dark.

## Gotchas & lessons (scoping a UX change so it can't affect balance)

- **Scope a UX relaxation by the exact predicate that means "balance does not apply."**
  `isInSinglePlayerGame()` is the right boundary because it is the engine's own notion of "not a
  competitive match." Relaxing on anything broader — every non-Windows build, or unconditionally —
  would have leaked free radar awareness into skirmish and multiplayer and changed the game. The
  discipline is to make the relaxation *structurally* unable to fire where parity matters, not to
  rely on it merely being unlikely to.

- **Add the branch, do not edit the rule.** The stock desktop check is left completely intact and
  runs first; the Android branch only appends an *additional* way to return true, after the original
  logic has had its say. A player who really does have a powered radar still returns true through the
  unchanged path, so nothing about normal radar behavior — powering down, losing the structure —
  changes.

- **Keep the highest-priority veto first in every path.** `!isRadarHidden(index)` leads both
  conditions, so a scripted hide wins over the QoL relaxation automatically. If the single-player
  branch had tested only `isInSinglePlayerGame()` and dropped the `isRadarHidden` guard, it would
  have silently overridden mission scripting. Re-stating the veto in the new branch is what keeps
  authorial intent above the convenience.

- **Match the compile guard to the *reason*, not to the platform group.** This is
  `#if defined(__ANDROID__)`, not `#if !defined(_WIN32)`, because the justification is touch input,
  which is Android-specific here. The texture-caps and stride fixes use `!defined(_WIN32)` because
  their justification is the DXVK/Mali translation layer, which is not Android-only. Two different
  reasons, two different guards — the guard should describe *why* the divergence exists.

- **This is only the visibility gate.** A visible gate over a broken texture is still a black
  rectangle. The minimap being *drawn* (this document) and the minimap terrain texture being *built
  correctly* (the DXVK format-caps and 24-bit-stride fixes) are independent problems that were
  debugged separately — the texture build was verified even while the draw was gated. Do not conflate
  the two halves.

## Related

- [../fixes/minimap-radar.md](../fixes/minimap-radar.md) — the consolidated radar fix log: the
  format-caps fix, the 24-bit stride fix, and this per-mode visibility gate as its section 3. Start
  here for the whole minimap story.
- [dxvk-texture-format-capabilities.md](dxvk-texture-format-capabilities.md) — the sibling
  deep-dive on the *texture* half: why the radar terrain came out black on a 10-bit DXVK swapchain
  (UNKNOWN backbuffer format disabling all texture formats, and 24-bit `R8G8B8` reported supported
  but stored 32-bit). That doc covers making the minimap *render*; this one covers making it
  *visible*.
