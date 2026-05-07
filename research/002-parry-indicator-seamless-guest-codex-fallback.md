---
date: 2026-05-05
research_type: codex-deep-research-fallback
reason_for_fallback: codex-cli could not reach wss://api.openai.com from sandbox (Operation not permitted)
topic: Elden Ring + SotE client-side parry indicator mods compatible with Seamless Co-op guest
---

# Elden Ring Parry Indicator Research (Seamless Guest, Client-Side Only)

## Bottom line (as of May 5, 2026)
I did **not** find a Nexus/Reddit-confirmed mod that exactly matches all constraints:
- client-side only,
- Seamless Co-op guest-safe,
- no `regulation.bin` / params,
- no parry-mechanics changes,
- and gives a live cue specifically for the enemy's parryable window.

The strongest negative signal is a dedicated r/EldenRingMods thread asking for this exact thing, where users reported they could not find one and described implementation complexity per-attack.

## 3-5 closest practical recommendations

### 1) Posture bar mod
- URL: https://www.nexusmods.com/eldenring/mods/3405
- Last updated: **March 1, 2026**
- Type: **HUD overlay** (enemy posture/stagger meter)
- Seamless compatibility evidence:
  - ER Seamless modding docs show `.dll` loading with Seamless via ModEngine2 `external_dlls` and use PostureBar as the worked example pattern.
  - Community install examples show `SeamlessCoop/ersc.dll` + `PostureBarMod.dll` in the same load list.
- Caveats:
  - Not a pre-parry attack-window detector.
  - Reported instability for some users (crash/signature mismatch reports in posts).

### 2) GG - Player List Overlay for Seamless Co-op
- URL: https://www.nexusmods.com/eldenring/mods/8088
- Last updated: **March 1, 2026**
- Type: **HUD overlay** (Seamless player/ping/invader utility)
- Seamless compatibility evidence:
  - Mod is explicitly built for Seamless Co-op and lists Seamless as requirement.
  - Updated recently and maintained by Tom Clark (same client-side DLL ecosystem as Transmogrify).
- Caveats:
  - Not a combat timing/parry cue mod.

### 3) Transmogrify Armor
- URL: https://www.nexusmods.com/eldenring/mods/3596
- Last updated: **March 14, 2026**
- Type: **ModEngine2 external DLL**
- Seamless compatibility evidence:
  - Description/changelog states compatibility with Seamless Co-op and includes multiplayer reliability work.
  - Explicitly says it does not break Seamless matchmaking.
- Caveats:
  - Not a parry helper; included as proof of the architecture class you requested (client-only external DLL that is Seamless-safe).

### 4) True Color ReShade (readability fallback)
- URL: https://www.nexusmods.com/eldenring/mods/9455
- Last updated: **March 23, 2026**
- Type: **ReShade visual preset**
- Seamless compatibility evidence:
  - ReShade is post-process visual-only on the local client (no regulation/params).
- Caveats:
  - No event-driven timing indicator; only improves visual readability of telegraphs.

### 5) Guard Parry (offline trainer-style fallback; NOT compliant with your constraints)
- URL: https://www.nexusmods.com/eldenring/mods/5128
- Last updated: **October 26, 2025**
- Type: **Gameplay/animation mechanic edit**
- Why included:
  - It is what appears when searching “parry” heavily on Nexus and is often confused with “parry helper.”
- Caveats (major):
  - Changes parry mechanics/window behavior.
  - Marked offline-use risk in description.
  - Fails your “indicator-only” requirement.

## Exact-match verdict
No solid 2025-2026 evidence found for a **Seamless guest-safe, client-only, indicator-only** parry-window cue mod.

Closest actionable path today is:
1) Use a client-only HUD overlay such as Posture Bar,
2) combine with visual readability ReShade,
3) keep all gameplay/parry-frame edits out of the load order.

## Paste-ready Nexus search queries
Use these in Nexus Elden Ring mod search:

- `"parry indicator" seamless`
- `"parry timing" "elden ring" "seamless"`
- `"riposte indicator" "elden ring"`
- `"attack indicator" "elden ring" mod`
- `"posture bar" "elden ring"`
- `"external_dlls" "SeamlessCoop/ersc.dll"`
- `"Tom Clark" "elden ring" "Seamless"`
- `"ReShade" "elden ring" "visuals"`

## Sources checked
- Seamless modding docs: https://ersc-docs.github.io/seamless-modding/
- Seamless Co-op (Nexus): https://www.nexusmods.com/eldenring/mods/510
- Posture bar mod: https://www.nexusmods.com/eldenring/mods/3405
- GG Player List Overlay: https://www.nexusmods.com/eldenring/mods/8088
- Transmogrify Armor: https://www.nexusmods.com/eldenring/mods/3596
- True Color ReShade: https://www.nexusmods.com/eldenring/mods/9455
- Guard Parry: https://www.nexusmods.com/eldenring/mods/5128
- r/EldenRingMods “Parry indicator mod?” thread: https://www.reddit.com/r/EldenRingMods/comments/1kqm4gk
