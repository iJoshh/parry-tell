---
skill: codex-deep-research
status: completed_with_fallback
completed: 2026-05-05
topic: Elden Ring + Shadow of the Erdtree client-side-only mods for Seamless Co-op guest
notes: Codex CLI runner was blocked in this environment (websocket/network policy). Research was completed manually with aggressive web search and primary-source grounding.
---

# Elden Ring Seamless Co-op (Guest) — Client-Side-Only Mod Research

## Scope and hard constraints applied
- Guest-only perspective (non-host).
- Seamless Co-op (LukeYui) only.
- No `regulation.bin` edits allowed.
- No game-balance/AI/drop/NPC behavior edits.
- Must stay compatible with base game + Shadow of the Erdtree.
- No overhaul recommendations.

## Core rule-of-thumb (mental model)
Use only mods that are purely **visual/UI/post-process/camera** or **self-contained DLL cosmetic QoL** and do **not** ship/require `regulation.bin`.

Why this works:
- Seamless docs explicitly allow personal visual differences as an exception.
- Seamless docs also warn all players must match on `regulation.bin` mods to connect reliably.
- If a mod includes/needs `regulation.bin`, treat it as shared gameplay-state risk and reject for this use case.

Primary references:
- Seamless modding guide: visual mods can remain personal; `regulation.bin` caution; texture visibility behavior.
  - https://ersc-docs.github.io/seamless-modding/
- Seamless FAQ: same game/mod versions; same `regulation.bin` mods; common desync/connectivity causes.
  - https://ersc-docs.github.io/faq/

## 10 viable guest-side recommendations (no overhaul)

1) **Transmogrify Armor** (Transmog, stats unchanged)  
URL: https://www.nexusmods.com/eldenring/mods/3596  
Last updated: **14 March 2026**  
Why viable: Explicitly states DLC compatibility and Seamless compatibility; explicitly says it does not impact Seamless matchmaking; includes `client_side_only` option.

2) **True Color Reshade** (ReShade visual preset)  
URL: https://www.nexusmods.com/eldenring/mods/9455  
Last updated: **23 March 2026**  
Why viable: Pure post-process preset category (client visual only).

3) **Tarnished - Reshade** (ReShade visual preset)  
URL: https://www.nexusmods.com/eldenring/mods/9080  
Last updated: **03 January 2026**  
Why viable: Pure ReShade preset category.

4) **Unreal ReShade** (ReShade visual preset)  
URL: https://www.nexusmods.com/eldenring/mods/6844  
Last updated: **18 March 2026**  
Why viable: Pure ReShade preset category.

5) **NUTShade** (ReShade visual preset)  
URL: https://www.nexusmods.com/eldenring/mods/8620  
Last updated: **24 August 2025**  
Why viable: Pure ReShade preset category.

6) **Nightreign HUD UI** (UI/HUD skin)  
URL: https://www.nexusmods.com/eldenring/mods/8062  
Last updated: **27 February 2026**  
Why viable: UI replacement category; no gameplay/balance intent.

7) **No-HUD-No-Effects** (UI/effects toggles via ShaderToggler)  
URL: https://www.nexusmods.com/eldenring/mods/8110  
Last updated: **31 March 2026**  
Why viable: HUD/effect presentation control; primarily client-side visual behavior.

8) **FoV ajuste** (FOV camera DLL)  
URL: https://www.nexusmods.com/eldenring/mods/7958  
Last updated: **14 March 2026**  
Why viable: Camera/FOV adjustment for base + DLC per mod page.

9) **Free Camera** (Photo-mode style camera detachment)  
URL: https://www.nexusmods.com/eldenring/mods/9420  
Last updated: **04 April 2026**  
Why viable: Camera tool category; does not target balance systems.

10) **Zelden Ring - The Legend of Lost Grace** (Texture/model replacer pack)  
URL: https://www.nexusmods.com/eldenring/mods/8134  
Last updated: **05 June 2025**  
Why viable: Page advertises Seamless compatibility; good for visual remix.  
Caution: Page mentions optional `.bin` files; for this project, use texture/model-only assets and skip any `.bin` component.

## Host-side settings/checks the guest should ask for
Ask host to confirm these before the session:
- `ersc_settings.ini` host settings are intentional, because host governs world scaling/invasions/rot/summons.
- Everyone runs same Elden Ring patch + same Seamless version.
- Same password and restarted game after editing password.
- If any shared gameplay mod exists, everyone must match `regulation.bin` mods exactly (your plan is to avoid these entirely).
- Steam networking/friends connectivity is healthy.

References:
- https://ersc-docs.github.io/seamless-modding/
- https://ersc-docs.github.io/faq/

## 2026 breakage patterns and known-broken things to avoid

### Avoid outright for this use case
- Any mod shipping `regulation.bin` or CSV param gameplay edits (disallowed by your hard constraints).  
Example of disallowed pattern on Nexus files tab showing `regulation.bin`:  
https://www.nexusmods.com/eldenring/mods/7495?tab=files

- Overhauls (Convergence/Reforged/etc.), trainers/debug/save-structure tools.

### Recent breakage patterns (2026)
- Windows Defender/Smart App Control quarantining `ersc.dll` caused launch failures for some users in Feb-Apr 2026 threads.  
Examples:
  - https://www.reddit.com/r/EldenRingMods/comments/1qui51k/seamless_coop_not_working/
  - https://www.reddit.com/r/EldenRingMods/comments/1rd6m8i/seamless_coop_not_working_on_friends_pc/
  - https://www.reddit.com/r/EldenRingMods/comments/1s848wy/new_computer_says_seamless_coop_not_designed_to/
  - https://www.reddit.com/r/EldenRingMods/comments/1sszj59/seamless_coop_2026_steam/

- Steam maintenance timing and connection errors are explicitly documented by Seamless FAQ (Tuesday outages can look like mod failure).  
Reference: https://ersc-docs.github.io/faq/

- Seamless + other mods should be launched through Mod Engine 2 workflow; docs stress correct launcher path and mod order matters.
  - https://ersc-docs.github.io/seamless-modding/
  - https://www.nexusmods.com/eldenring/articles/94%27%27

## Notes on confidence
- High confidence: safety model (`regulation.bin` avoidance, visual-only allowance, host-gated behavior) due direct Seamless docs.
- Medium confidence per individual Nexus mod: Nexus pages provide update date/category; not every page explicitly declares Seamless compatibility.
- Highest-confidence pick in the list: **Transmogrify Armor** due explicit Seamless + SotE + matchmaking notes.

## Source list (primary)
- https://ersc-docs.github.io/seamless-modding/
- https://ersc-docs.github.io/faq/
- https://github.com/LukeYui/EldenRingSeamlessCoopRelease
- https://www.nexusmods.com/eldenring/mods/510
- https://www.nexusmods.com/eldenring/articles/94%27%27
- https://www.nexusmods.com/eldenring/mods/3596
- https://www.nexusmods.com/eldenring/mods/9455
- https://www.nexusmods.com/eldenring/mods/9080
- https://www.nexusmods.com/eldenring/mods/6844
- https://www.nexusmods.com/eldenring/mods/8620
- https://www.nexusmods.com/eldenring/mods/8062
- https://www.nexusmods.com/eldenring/mods/8110
- https://www.nexusmods.com/eldenring/mods/7958
- https://www.nexusmods.com/eldenring/mods/9420
- https://www.nexusmods.com/eldenring/mods/8134
- https://www.nexusmods.com/eldenring/mods/144
- https://www.reddit.com/r/EldenRingMods/comments/1rlsnfa/can_you_use_any_of_the_fsr_mods_with_seamless_coop/
- https://www.reddit.com/r/EldenRingMods/comments/1qui51k/seamless_coop_not_working/
- https://www.reddit.com/r/EldenRingMods/comments/1rd6m8i/seamless_coop_not_working_on_friends_pc/
- https://www.reddit.com/r/EldenRingMods/comments/1s848wy/new_computer_says_seamless_coop_not_designed_to/
- https://www.reddit.com/r/EldenRingMods/comments/1sszj59/seamless_coop_2026_steam/
- https://www.youtube.com/watch?v=sV63SfV6fR0
