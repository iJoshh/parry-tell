---
date: 2026-05-05
session_id: 1777990915844
topic: Client-side-only mods for Elden Ring + Shadow of the Erdtree compatible with LukeYui's Seamless Co-op for non-host (guest) player
sources_found: 56
sources_dropped: 2
drop_rate: 0.036
wall_clock_sec: 601
backends_used: ["exa", "brave", "gh", "jina", "browse"]
topic_class: default
axis_count: 5
faithfulness_probed: 0
faithfulness_dropped: 0
---

# Client-side mods for Elden Ring + Shadow of the Erdtree — Seamless Co-op (guest)

## 1. Executive Summary

You are a guest in someone else's Seamless Co-op session. The host is mostly vanilla; one of your party is a first-timer. You want to remix your own visual/feel experience without breaking their game or your save. Five things to internalize before installing anything:

1. **The single golden rule, straight from the official Seamless Co-op docs (`ersc-docs.github.io/seamless-modding`):** *"For optimal experience, all players should use the same mods together — with the exception for visual mods, which can remain personal."* Visual mods (textures, models, ReShade, UI skins) can be guest-only. Anything else has to match across the session.

2. **`regulation.bin` is the bright line.** It is Elden Ring's gameplay-data file — params for items, weapons, enemies, balance, AI. Seamless Co-op gates matchmaking on this file. Mods that touch `regulation.bin` (overhauls, randomizers, balance mods, moveset packs, item tweaks) MUST be installed identically by every player or the session won't connect / will desync. Per the official FAQ: *"Make sure everyone is using the same regulation.bin file mods."* If a mod's install includes a `regulation.bin` file, it is **not guest-safe** and you cannot run it solo.

3. **The "other players see your mod" question has a clean answer.** Per the same docs: *"Only you will see the texture and model modifications you are using."* The host and the first-timer will see your character in their own local vanilla armor textures, regardless of what you've reskinned on your end. This is structural to the high/low LOD parts system FromSoft built — your local game renders the high-quality (`.partsbnd.dcx`) variant of YOU; their game renders the low-quality (`_l.partsbnd.dcx`) variant of you, from THEIR files. So a player-character texture mod is invisible to peers by default, and that is *good* for the first-timer-host scenario.

4. **You must launch through `ersc_launcher.exe` or ModEngine 2.** *"DLL injectors like Elden Mod Loader and Lazy Loader won't work."* This is operational, not desync-related — the wrong launcher just means your mods silently don't load. ReShade, ME2-routed texture mods, and DLL plugins loaded under ME2 (with `SeamlessCoop/ersc.dll` listed in `external_dlls` in `config_eldenring.toml`) are all fine. Standalone DLL injectors are not.

5. **Counter-argument worth taking seriously:** Seamless Co-op's recent release pattern (v1.9.7, April 2026) added "additional counter-cheating methods" and fixed networking exploits. The trajectory is anti-cheat *hardening*, not loosening. A mod that worked under Seamless 1.8.x might trip new defenses in 1.9.x. The mitigation is to track the last-updated date on every mod and prefer ones with 2025–2026 maintenance. Mods abandoned before the SotE rewrite (June–July 2024, Seamless v1.8.0) should be approached with suspicion regardless of category.

The critic concern, in one sentence: *"Visual-only" is a category claim, not a guarantee — a mod author can label anything anything, and the only structural guarantee comes from the install footprint (does it ship a `regulation.bin`? does it ship a `.dll` that hooks game logic? does it touch `param/` files?).* Treat all recommendations below as "load it, look at the install, then trust."

## 2. Detailed findings

### Axis A — Seamless Co-op official compatibility rules

The canonical compatibility doc is **not** the GitHub README. It's the Jekyll site at `https://ersc-docs.github.io/`, maintained by the Seamless Co-op community/dev team. Key authoritative statements, all verbatim from there or the linked FAQ:

- **Visual carve-out (the central rule):** "For optimal experience, all players should use the same mods together - with the exception for visual mods, which can remain personal." [ersc-docs.github.io/seamless-modding](https://ersc-docs.github.io/seamless-modding/)
- **regulation.bin parity is required for non-visual mods:** "NOTE: Make sure everyone is using the same regulation.bin file mods." [ersc-docs.github.io/faq](https://ersc-docs.github.io/faq/)
- **Patches that touch regulation.bin are tolerated, version mismatch is not:** "Small changes (like changes to regulation.bin) will not break Seamless Coop. However, everyone has to be on the same game and mod version in order to connect to each other." [ersc-docs.github.io/faq](https://ersc-docs.github.io/faq/)
- **ME2 has no conflict resolution for regulation.bin** — highest priority wins: "modengine 2 currently has no way to resolve conflicting files including regulation.bin." [ersc-docs.github.io/seamless-modding](https://ersc-docs.github.io/seamless-modding/)
- **v1.9.5 unified ME2/ME3 matchmaking pools (March 2026):** "ME2 and ME3 users are no longer in isolated matchmaking pools (providing the regulation file is consistent)." [GitHub release v1.9.5](https://github.com/LukeYui/EldenRingSeamlessCoopRelease/releases/tag/v1.9.5)
- **Host's `ersc_settings.ini` is authoritative for the world:** "Host's ersc_settings.ini determines the world's Scaling, Player Invasions, Rot and Spirit Summons." [ersc-docs.github.io/seamless-modding](https://ersc-docs.github.io/seamless-modding/)
- **DLL injector restriction:** "You MUST launch Seamless Coop with either ersc_launcher.exe or ModEngine2. DLL injectors like Elden Mod Loader and Lazy Loader won't work." [ersc-docs.github.io/seamless-modding](https://ersc-docs.github.io/seamless-modding/)
- **Save-file separation:** "Your save file extension (in the vanilla game this is .sl2). Use any alphanumeric characters." [ersc-docs.github.io/seamless-modding](https://ersc-docs.github.io/seamless-modding/) — Seamless defaults to `.co2` to avoid clobbering vanilla `.sl2` saves.
- **DLC-mismatched co-op is destructive, not graceful:** "you will only be able to play the base game together. Attempts to enter the DLC despite that can result in destroyed characters and infinite loading screens." [ersc-docs.github.io/faq](https://ersc-docs.github.io/faq/)
- **Anti-cheat from other apps (Riot Vanguard, etc.) breaks sessions:** "If you have installed Riot's Vanguard installed try turning it off." [ersc-docs.github.io/faq](https://ersc-docs.github.io/faq/)
- **SotE compatibility was a major rewrite (v1.8.0, July 2024):** "Elden Ring's Popular Seamless Co-Op Mod Playable Once More After Creator Re-Writes It to Work With Shadow of the Erdtree." [IGN](https://www.ign.com/articles/elden-rings-popular-seamless-co-op-mod-playable-once-more-after-creator-re-writes-it-to-work-with-shadow-of-the-erdtree)

There is no per-category allow/deny list anywhere in the official docs. ReShade, texture replacers, UI mods, animation mods, character model swaps, audio mods, and photo-mode mods are not enumerated by name. They all fall under the "visual mods, personal" carve-out by inference. The structural test is: *does the mod ship a `regulation.bin` or a `.param` file in its install?* If yes, it's not guest-safe alone.

### Axis B — Curated Nexus mods

15 candidates surveyed. 13 retained after grounding (mods 7794 and 2498 dropped — Nexus pages 0-overlap on fetch, likely hidden/removed). The verified picks for the "guest-safe visual remix" use case are categorized in §3 below.

### Axis C — Reddit and community consensus

The community center of gravity has shifted to r/EldenRingMods (and the Nexus comments / Steam discussions / Discord) rather than r/SeamlessCoop. Salient confirmations:

- **LukeYui's Steam FAQ (broad consensus):** "for regulation / param file edits everyone in the party must have them active in order for them to be visible. (e.g. if just 1 player uses a randomizer, it will only work for that player)." [Steam discussion](https://steamcommunity.com/app/1245620/discussions/0/3395163747104259416?ctp=5)
- **Custom armor LOD gotcha (multiple voices):** Other players see your modded armor as the *low-quality* `_l` variant from their own files, not from yours. So your custom texture is silently invisible to the host. The "Custom Armor Fixer" mod (Nexus 3818) exists to opt INTO making custom armor visible to friends by duplicating high→low, *which requires both players to install it*. [Nexus mod 3818](https://www.nexusmods.com/eldenring/mods/3818)
- **ME2 + Seamless load order (multiple voices):** "in 'config_eldenring.toml' file, 'SeamlessCoop/ersc.dll' line added under the 'external_dlls = [' parameter." [r/EldenRingMods](https://www.reddit.com/r/EldenRingMods/comments/1r455f3/elden_ring_playable_bosses_mod/)
- **SotE breakage was real and widespread:** "The DLC was not compatible with the mod in the days after its release, or with the subsequent updates." [gameland.gg](https://gameland.gg/how-to-install-seamless-co-op-mod-for-elden-ring-after-dlc/) — every FromSoft patch breaks Seamless until LukeYui pushes an update.
- **Seamless 1.8.8 scaled back anticheat for QoL/perf mods:** "Scaled back some anticheat measures for invasion matchmaking to allow for some QoL and performance improvement mods." [Nexus changelog](https://www.nexusmods.com/eldenring/mods/510) — historically the inflection point for permissive guest-side QoL mods.
- **Experimental Seamless 2.0 is NOT mod-compatible:** "Not compatible with mods; The seamless co-op launcher is required for functionality. Wait for stable release if you require usage of other mods." [Steam discussion](https://steamcommunity.com/app/1245620/discussions/0/3395163747104259416?ctp=5) — relevant if you or the host are running a beta channel.

### Axis D — Visual-only deep dive (the safest tier)

**ReShade is universally safe in Seamless.** ReShade is a DXGI-layer post-process injector. It hooks Direct3D and applies color/lighting filters to the final frame. It does not touch any game file, doesn't ship a `.dll` that the game loads, and runs entirely client-side. Confirmed picks:

- **REVENANT ReShade ([mod 8](https://www.nexusmods.com/eldenring/mods/8))** — long-running preset bundle (LUSH, GRIM variants).
- **Photorealistic Lands Between ReShade ([mod 45](https://www.nexusmods.com/eldenring/mods/45))** — community-confirmed working under Seamless.
- **Snuggly's Cinematic ReShade ([mod 1750](https://www.nexusmods.com/eldenring/mods/1750))** — last updated Sep 2024, post-SotE.
- **Natural Realism ReShade ([mod 366](https://www.nexusmods.com/eldenring/mods/366))** — RTGI-compatible.

**Texture mods via ModEngine2 are safe.** They redirect parts/material asset reads through the ME2 sandbox; they do not merge into `regulation.bin`. Picks:

- **Texture Improvement / Bardo HD ([mod 2431](https://www.nexusmods.com/eldenring/mods/2431))** — 25 GB AI-upscaled environmental textures. *"All armors and characters are untouched"* — environmental only. [dsogaming](https://dsogaming.com/mods/elden-ring-gets-a-25gb-hd-texture-pack-overhauling-over-4500-textures)
- **Enhanced 4K facial textures ([mod 9267](https://www.nexusmods.com/eldenring/mods/9267))** — Jan 2026, Nexus "Safe to use" tag.
- **Verdigris armor and shield 4K ([mod 5753](https://www.nexusmods.com/eldenring/mods/5753))** — post-SotE upload, July 2024.

**FOV / camera mods (DLL plugins, no `regulation.bin`):**

- **Adjust the Field of View ([mod 325](https://www.nexusmods.com/eldenring/mods/325))** — client-side FoV slider.
- **Increase Camera Distance FoV (mod 2498)** — *dropped from grounding (page returned empty), likely hidden/removed.* Skip.

**Player-character texture mods are safe by default in Seamless.** Per the high/low LOD architecture, peers render your character from THEIR local files. So a custom armor texture you install is invisible to peers — they see vanilla. This is exactly what you want when the host is mostly vanilla and the first-timer should not be confused.

### Axis E — Edge cases (animation, transmog, UI)

This is the tier where the answer is "it depends — read the install."

**Animation mods.** The dominant Elden Ring animation mod ecosystem (Clever's Moveset Packs, Moveset Animation Remix v2.4 [Nexus 4524](https://www.nexusmods.com/eldenring/mods/4524)) is **NOT visual-only**. The ERSC docs put moveset packs in the same category as Convergence and ER Reforged: *"mods are usually overhauls like Clever's moveset packs, Convergence and Elden Ring Reforged or something like a armor replacer but can also be simple edits to the regulation.bin."* Movesets touch hitboxes and TAE data. **You cannot install a moveset mod solo as a guest** — every player must run identical files, or you'll desync. There is no widely-used pure-visual animation reskin on Nexus that swaps swing FX without touching frame/hitbox data. **Verdict: skip animation mods entirely for the "lightly remix as guest" use case.**

**Transmog.** This is the most surprising win in this research. **Transmogrify Armor by ThomasJClark ([Nexus 3596](https://www.nexusmods.com/eldenring/mods/3596))** is the rare mod where the author engineered Seamless compatibility as a first-class feature:
- *"Rewrite multiplayer code to use the Steamworks SDK for networking, which should make transmogs reliably show up in Seamless Co-op"* (v2.4.0 changelog)
- *"This mod doesn't impact Seamless Co-op matchmaking, so you can still co-op or invade with people who don't have it installed"* — guest can install solo.
- *"Add client_side_only option to prevent transmogs from being seen online"* — INI flag for guaranteed-safe fallback if propagation breaks.
- *"Playing online with transmogrified armor won't work, and might get you banned if you try anyways."* — vanilla online ban warning; Seamless is safe by design.

This is the canonical "different visual, vanilla stats" mod for the player profile in the brief.

**Companion mod: Armor Dyes ([Nexus 6927](https://www.nexusmods.com/eldenring/mods/6927))** by the same author — recolor any equipment, *"Compatible with Seamless Co-op, The Convergence, Reforged, Transmog, etc."*

**UI / HUD mods.** Texture/asset swaps fall under the visual carve-out; icon-database mods that re-author item params do not. The boundary is: does the mod ship a `regulation.bin` or `.param` file? If no, it's a skin and it's safe. Concrete picks:
- **Bloodborne UI Remastered ([Nexus 3247](https://www.nexusmods.com/eldenring/mods/3247))** — pure asset swap.
- **Closed Network HUD ([Nexus 546](https://www.nexusmods.com/eldenring/mods/546))** — recreates the PS4 Network Test HUD.
- **Fully Toggle HUD UI ([Nexus 4314](https://www.nexusmods.com/eldenring/mods/4314))** — *author explicitly clears Seamless Co-op*: "Do not use this in online mode! Reshade with add-on support might get you banned... (Unless it's seamless co-op)." This is the rare mod author flagging Seamless as the safe online context.

## 3. Recommended mods (organized for the use case)

### Tier 1 — install with full confidence (purely client-side, guest-safe)

| Mod | Nexus | Last updated | Category | Why it's safe |
|---|---|---|---|---|
| **REVENANT ReShade** | [mod 8](https://www.nexusmods.com/eldenring/mods/8) | (preset, version-agnostic) | ReShade preset | Post-process DXGI hook, no game files touched |
| **Photorealistic Lands Between ReShade** | [mod 45](https://www.nexusmods.com/eldenring/mods/45) | 2022-03-04 | ReShade preset | Same — and author explicitly notes "Everything works perfectly fine in Seamless coop" |
| **Snuggly's Cinematic ReShade** | [mod 1750](https://www.nexusmods.com/eldenring/mods/1750) | 2024-09-27 | ReShade preset | Same; recent maintenance |
| **Natural Realism ReShade** | [mod 366](https://www.nexusmods.com/eldenring/mods/366) | 2022-03-24 | ReShade preset | RTGI-compatible |
| **Texture Improvement (Bardo HD)** | [mod 2431](https://www.nexusmods.com/eldenring/mods/2431) | (v4 era) | Environment textures | Environmental only — armors/characters untouched; ME2 sandbox install |
| **Enhanced 4K facial textures** | [mod 9267](https://www.nexusmods.com/eldenring/mods/9267) | 2026-01-27 | Face textures | Nexus "Safe to use" tag; ME2 parts folder install |
| **Verdigris armor and shield 4K** | [mod 5753](https://www.nexusmods.com/eldenring/mods/5753) | 2024-07-16 | Armor texture | Pure texture replacement, post-SotE upload |
| **Adjust the Field of View** | [mod 325](https://www.nexusmods.com/eldenring/mods/325) | (techiew QoL series) | FOV/camera | Client-side DLL plugin, no `regulation.bin` |
| **Skip the intro logos** | [mod 421](https://www.nexusmods.com/eldenring/mods/421) | (techiew QoL series) | QoL | Launch-flow tweak only |
| **EZ Auto Backup (Seamless Co-Op compatible)** | [mod 144](https://www.nexusmods.com/eldenring/mods/144) | (maintained) | QoL | Title literally claims compatibility; provides Seamless save-bank profile |

### Tier 2 — install with the install-footprint check (visual-only "feel" mods)

| Mod | Nexus | Category | Caveat |
|---|---|---|---|
| **Bloodborne UI Remastered** | [mod 3247](https://www.nexusmods.com/eldenring/mods/3247) | UI | Texture/asset swap; safe under the visual carve-out. Older (Aug 2023) — UI assets rarely break across patches but verify launch |
| **Closed Network HUD** | [mod 546](https://www.nexusmods.com/eldenring/mods/546) | UI | Asset/texture-level UI swap; last updated for game v1.13.1 |
| **Fully Toggle HUD UI** | [mod 4314](https://www.nexusmods.com/eldenring/mods/4314) | UI | Author explicitly clears Seamless — *"(Unless it's seamless co-op)"* |
| **Black & Red Recolors** | [mod 2251](https://www.nexusmods.com/eldenring/mods/2251) | Armor textures | 4K retextures of 14 armor sets; "Fair and balanced" tag, last updated Oct 2023 (pre-SotE — confirm still loads) |

### Tier 3 — the "different look, vanilla stats" specials

| Mod | Nexus | Category | Caveat |
|---|---|---|---|
| **Transmogrify Armor (ThomasJClark)** | [mod 3596](https://www.nexusmods.com/eldenring/mods/3596) | Transmog | Author engineered for Seamless — Steamworks SDK networking, `client_side_only` INI flag, doesn't impact matchmaking. Vanilla online = ban risk; Seamless = safe by design |
| **Armor Dyes (ThomasJClark)** | [mod 6927](https://www.nexusmods.com/eldenring/mods/6927) | Color swap | Same author; explicitly Seamless-compatible. Dyes propagate to peers if peers also installed |

### Photo mode

The on-Nexus photomode mod page returned empty during grounding. The off-Nexus alternative referenced in the research is **Otis_Inf's Photomode for Elden Ring** at `https://opm.fransbouma.com/Cameras/eldenring.htm`. Pause-camera + free-fly tooling, post-process only — benign in Seamless. *Not citation-grounded by the same pipeline that verified the Nexus picks above; treat as a lead, not a verdict.*

## 4. Do NOT install

Anything in this list will desync your session, kick you, corrupt saves, or require the host and first-timer to install identical files. Examples gleaned from the research:

- **Anything that ships a `regulation.bin` file in its install.** This is the canonical Seamless Co-op desync trigger. Examples: most "balance fix" mods, randomizers, item drop tweaks, weapon damage tweaks, enemy AI mods.
- **Moveset packs.** *Clever's Moveset Packs, Moveset Animation Remix ([Nexus 4524](https://www.nexusmods.com/eldenring/mods/4524))* and similar. Even when the author is targeting Seamless, hitbox/TAE data must match across all players — guest-solo install will desync.
- **Convergence Mod, Elden Ring Reforged, Elden Ring Unalloyed.** These are the canonical "Seamless 2.0 is an experimental build and not compatible" tier. Out of scope per the brief, named for completeness.
- **Reworked Multiplayer Items, Harder Coop, DAELY's Power Scaling Mod**, and similar gameplay tweaks. Per Nexus warning text on related mods: "If you run DAELY's Power Scaling Mod without using Seamless Co-op or anti-cheat toggler you could get banned from online play!" These are param-edits — unsafe alone.
- **Custom Armor Fixer ([Nexus 3818](https://www.nexusmods.com/eldenring/mods/3818)) installed solo.** This isn't dangerous, just useless without coordination — both players must run the LOD batch and have the same armor mod files for cosmetic armor to render on the OTHER player. If you only want your own modded armor visible to YOURSELF, you don't need this.
- **Uncapped FPS / framerate unlocker mods.** Field reports of severe FPS drops (150 → 15 every 15–20s) when combined with Seamless — frame-pacing layer Seamless adds is incompatible with frame-uncap hooks for some setups.
- **Any mod loaded via Elden Mod Loader or Lazy Loader as standalone DLL injectors.** *"DLL injectors like Elden Mod Loader and Lazy Loader won't work."* The mod simply won't load; some installers chain into `dinput8.dll` and conflict with Seamless's launcher.
- **Seamless Experimental 2.0** running while host is on stable. *"Not compatible with mods; wait for stable release."*
- **Riot Vanguard or other intrusive anti-cheat from non-FromSoft games.** Confirmed cause of session-join failure in the official FAQ.
- **Moving a `.co2` (Seamless) save file back to vanilla `.sl2`.** This is the documented ban risk — Seamless adds unique items (Tiny Great Pot, Effigy of Malenia, etc.) that don't exist in vanilla and flag the account when brought online. *Not a mod issue, but worth flagging.*

## 5. Rules of thumb for Seamless Co-op guest mod safety

A mental model, in order of how to apply them:

1. **Open the mod's zip before installing. Look for `regulation.bin` or any `param/` folder.** If either is present, it is **not guest-safe alone** — full stop.
2. **Look for a `.dll` in the install.** A `.dll` is fine *if* it is loaded under ModEngine 2 (added to `external_dlls = [...]` in `config_eldenring.toml`). It is not fine via Elden Mod Loader, Lazy Loader, or any standalone DLL injector — Seamless requires `ersc_launcher.exe` or ME2.
3. **If it's only in `parts/`, `material/`, `chr/`, `sfx/`, `sound/`, `menu/`, or under a ReShade `Game\reshade-shaders\` directory — it's visual-only.** Per the official docs: *"Only you will see the texture and modle modifications you are using."*
4. **Last-updated date matters.** Anything not maintained since June 2024 (the SotE rewrite) might still load but is one game patch from breaking. Prefer 2025–2026 maintenance.
5. **Animation mods are a trap.** "Just visual swing FX" mods don't really exist in this ecosystem — the popular animation mods all touch TAE/hitbox data. Skip the category for the brief's use case.
6. **Transmog is the exception that proves the rule.** ThomasJClark's transmog and dyes are the rare client-side-by-design Seamless-aware mods. Use them as the gold standard for "different look, vanilla stats."
7. **Test before the first-timer joins.** Launch Seamless solo with all your mods active, walk through Limgrave for 10 minutes, force a save, quit. Then run the same character into a host's session. If you see infinite-loading-screen, "save data corrupted," or framerate cliffs, your stack is suspect — strip back to ReShade-only and re-add layer-by-layer.

## 6. Host-side flags to ask the host about

These live in the host's `seamlesscoopsettings.ini` / `ersc_settings.ini` and are authoritative. Your local copy is ignored for any of these. Worth confirming before the session:

- **`cooppassword`** — must be byte-identical between host and all guests, file must be saved and the game fully restarted for changes to register. Per the official FAQ: *"Make sure everyone has set the same password in the seamlesscoopsettings.ini."*
- **`save_file_extension`** (default `co2`) — host can set custom; if so, you'll need the same extension in your config so your save bank matches. Common practice is `co2` for vanilla-Seamless, custom (e.g. `con2`) for modded sessions to keep the saves cleanly separated. Important: keep a `.sl2` (vanilla) save you never bring online and a separate `.co2` (Seamless) character — never convert one to the other.
- **`allow_invaders`** (host toggle) — gates whether PvP invaders can enter. Affects pacing for a first-timer; ask host to leave at default `1` or set to `0` based on the group's tolerance.
- **Scaling, Rot, Spirit Summons** — all host-authoritative per ERSC docs. *"Host's ersc_settings.ini determines the world's Scaling, Player Invasions, Rot and Spirit Summons."*
- **Seamless Co-op version** — host's version is what your guest version must match. Confirm both of you are on the same release tag (currently v1.9.x line, latest stable v1.9.7/1.9.8 as of April 2026). The start menu shows the version; screenshot to verify per the official troubleshooting guidance.
- **Whether the host is on Seamless Experimental 2.0.** If yes, you cannot run any mods alongside it — *"Not compatible with mods; wait for stable release."*
- **Whether the host has any `regulation.bin`-touching mods at all.** If yes, you must run identical copies. If no, you have full guest freedom over visual-only mods.
- **Both of you must own the Shadow of the Erdtree DLC, or neither of you should enter DLC space.** Per the FAQ, mismatched DLC ownership combined with entering DLC space *"can result in destroyed characters and infinite loading screens"* — character corruption, not graceful refusal.

## 7. Contradictions and open questions

- **Photo mode on Nexus appears removed/hidden.** The on-Nexus photo mode mod page was unavailable during grounding. Otis_Inf's off-Nexus tool is the live alternative but has not been recently maintained per public note. Open question: which photomode is currently maintained and SotE-compatible? Ask in r/EldenRingMods.
- **Moveset Animation Remix v2.4 (April 2026) claims to target Seamless Co-op explicitly** but the ERSC docs put moveset packs in the must-match category. The author's framing is "for me and my friend" who both run it — which is the must-match path, not a client-side carve-out. Likely safe ONLY if all session players install identically. **Treat as `should_be_safe` only when all-players-match.**
- **No high-quality direct quotes from r/SeamlessCoop subreddit specifically.** The community center of gravity has shifted to r/EldenRingMods, the official Discord, and Nexus comments. If primary-source community vetting is desired, the Discord is likely the highest-signal venue (not in this research's scope due to web-accessibility constraints).
- **ReShade DLL load order with the seamlesscoop launcher chain** has community claims of occasional injection failure, but no canonical fix surfaced. If ReShade fails to overlay under `ersc_launcher.exe`, try launching via ModEngine 2 with the ReShade `dxgi.dll` placed in the game directory.

## 8. Source list

| URL | Tier | Note |
|---|---|---|
| https://ersc-docs.github.io/ | primary | Canonical Seamless Co-op compatibility docs (Jekyll site, community/dev maintained) |
| https://ersc-docs.github.io/seamless-modding/ | primary | The single most-cited compatibility doc — "visual mods can remain personal" carve-out lives here |
| https://ersc-docs.github.io/faq/ | primary | regulation.bin parity, password match, DLC-mismatch warning |
| https://github.com/LukeYui/EldenRingSeamlessCoopRelease/releases/tag/v1.9.5 | primary | ME2/ME3 matchmaking pool unification (March 2026) |
| https://www.nexusmods.com/eldenring/mods/510 | primary | Seamless Co-op base mod page; canonical changelog reference |
| https://www.nexusmods.com/eldenring/mods/3596 | primary | Transmogrify Armor — Seamless-aware-by-design |
| https://www.nexusmods.com/eldenring/mods/3596?tab=description | primary | Same — description tab with full changelog citations |
| https://www.nexusmods.com/eldenring/mods/6927 | primary | Armor Dyes — explicit Seamless compatibility |
| https://www.nexusmods.com/eldenring/mods/3818 | primary | Custom Armor Fixer — high/low LOD architecture explanation |
| https://www.nexusmods.com/eldenring/mods/4314 | primary | Fully Toggle HUD UI — author explicitly clears Seamless |
| https://www.nexusmods.com/eldenring/mods/2431 | primary | Bardo HD Texture Improvement |
| https://www.nexusmods.com/eldenring/mods/9267 | primary | Enhanced 4K facial textures (Jan 2026) |
| https://www.nexusmods.com/eldenring/mods/5753 | primary | Verdigris armor 4K (post-SotE) |
| https://www.nexusmods.com/eldenring/mods/45 | secondary | Photorealistic Lands Between ReShade |
| https://www.nexusmods.com/eldenring/mods/8 | secondary | REVENANT ReShade |
| https://www.nexusmods.com/eldenring/mods/1750 | primary | Snuggly's Cinematic ReShade |
| https://www.nexusmods.com/eldenring/mods/366 | primary | Natural Realism ReShade |
| https://www.nexusmods.com/eldenring/mods/325 | secondary | Adjust the Field of View |
| https://www.nexusmods.com/eldenring/mods/421 | secondary | Skip the intro logos |
| https://www.nexusmods.com/eldenring/mods/144 | secondary | EZ Auto Backup (Seamless Co-Op compatible) |
| https://www.nexusmods.com/eldenring/mods/3247 | primary | Bloodborne UI Remastered |
| https://www.nexusmods.com/eldenring/mods/546 | primary | Closed Network HUD |
| https://www.nexusmods.com/eldenring/mods/4524 | primary | Moveset Animation Remix v2.4 — DO NOT install solo as guest |
| https://steamcommunity.com/app/1245620/discussions/0/3395163747104259416?ctp=5 | community | LukeYui's Steam FAQ post — broad consensus reference |
| https://www.reddit.com/r/EldenRingMods/comments/1r455f3/elden_ring_playable_bosses_mod/ | community | ME2 + Seamless `external_dlls` load order documented in comments |
| https://gameland.gg/how-to-install-seamless-co-op-mod-for-elden-ring-after-dlc/ | community | SotE breakage timeline, password rules |
| https://www.ign.com/articles/elden-rings-popular-seamless-co-op-mod-playable-once-more-after-creator-re-writes-it-to-work-with-shadow-of-the-erdtree | secondary | IGN coverage of v1.8.0 SotE rewrite |
| https://dsogaming.com/mods/elden-ring-gets-a-25gb-hd-texture-pack-overhauling-over-4500-textures | secondary | DSOGaming coverage of Bardo HD Texture Pack |
| https://fextralife.com/mods/elden-ring/293 | primary | Fextralife mirror of techiew's AdjustTheFov page |

### URLs that failed grounding (dropped from recommendations)

- `https://www.nexusmods.com/eldenring/mods/7794` — Loading Screen Seamless Coop Fix. Page returned 0-overlap on jina fetch; likely hidden or 404.
- `https://www.nexusmods.com/eldenring/mods/2498` — Increase Camera Distance FoV. Same pattern; the Axis B sub-agent independently noted this mod returned "Mod unavailable."

## 9. Methodology

- **Phases:** 0 skipped (fresh request), 1 skipped (well-defined topic), 2 plan with `TOPIC_CLASS=default` and `AXIS_COUNT=5`, 3 dispatched 5 parallel `general-purpose` Agent sub-agents (one per axis), 4 citation-grounded all 56 findings via `verify-citations.sh` (jina-read primary, exa-contents and browse fallback configured), 5 synthesis (this artifact), 6 reporting back.
- **Sub-agent budget:** 6–8 searches, 15 fetches, 3 minutes per axis. Actual: 6/11/6/4/7 queries, 9/12/0/0/9 jina fetches across axes.
- **Backends used across the run:** Exa (primary search across all axes), Brave (configured fallback, not heavily used), gh (Axis A — GitHub release tags), jina-read (Nexus + reddit + GitHub README content), browse (configured fallback, not used). Phase 4 grounding ran 56 finds through jina-read with 0 fetch failures.
- **Phase 4 stats:** 56 total → 54 verified (≥0.80 token overlap) → 0 weak_match → 2 dropped (<0.60 overlap, both Nexus 0-overlap fetches likely from hidden/removed mod pages). Drop rate **3.6%**, well under the 30% credibility-warning threshold. No faithfulness probe needed (M7 only triggers on weak matches).
- **Wall clock:** 601 seconds (~10 min) from session start to artifact write.
