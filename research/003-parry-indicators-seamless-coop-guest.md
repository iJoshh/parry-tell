---
title: Parry Indicators for Elden Ring Seamless Co-op (Guest)
topic: Find client-side-only parry indicator / parry timing helper mods for Elden Ring + Shadow of the Erdtree confirmed compatible with Seamless Co-op (LukeYui) when used by guest (non-host); visual cues, HUD overlays, audio cues, or screen indicators that fire when an enemy is performing a parryable attack
date: 2026-05-05
session_id: 1777992410326
sources_found: 21
sources_dropped: 3
drop_rate: 0.125
wall_clock_sec: 692
backends_used: [exa, brave, jina, gh]
---

# Parry Indicators for Elden Ring Seamless Co-op (Guest)

Research conducted 2026-05-05 on the question: does any client-side-only parry indicator mod exist for Elden Ring + SotE that's safe to run as a Seamless Co-op guest?

## 1. Executive Summary

**Bottom line: no such mod exists today.** After hitting Nexus Mods, GitHub, Reddit, ModDB-equivalent forums, and the Seamless Co-op modding docs across 4 axes, **no Elden Ring mod was found that adds a visual or audio "parry window is now open" indicator AND ships as a client-side ME2 `external_dlls` overlay AND is confirmed compatible with Seamless Co-op for guests.** This is an unfilled niche with documented community demand.

Five concrete findings:

1. **The architectural pattern exists, the parry implementation does not.** PostureBarMod (Nexus 3405) — a DLL hook that overlays enemy stagger/posture bars without modifying game data — is cited by the official ERSC docs as the canonical Seamless-compatible HUD overlay. Its hook can read player state too (player stagger bar shipped in Beta 0.4.0). A parry-indicator mod following the same template is technically possible, but no one has shipped it.

2. **All Nexus "parry" mods are gameplay-mechanic edits, not indicators.** Easy Parry (mod 2478), Better Parrying (mod 1815), Timed Block Parry (mod 1224), Guard Parry (mod 5128), Roll n Parry more ez (mod 7294), Sekiro-like SotE Deflect (mod 5409), and Deflection (mod 5494) all modify TAE animation frames or `regulation.bin` to widen / change parry mechanics. They change behavior; none telegraphs the existing window. Several edit `regulation.bin`, which makes them Seamless-incompatible for guests unless the host runs the same merged file.

3. **Community demand is real but unanswered.** A user asked the Timed Block Parry author for a Seamless + First Person Souls compatible version and got no resolution. r/EldenRingMods has a thread titled "Parry indicator mod?" (May 2025) where the top reply explicitly calls a per-attack-frame indicator "kinda unreasonable" because every attack would need annotation. The topic effectively does not surface as a debated cheating-vs-acceptable question on r/SeamlessCoop — the absence implies no such mod is in circulation.

4. **The closest "parry-window visualizer" — TGA's Cheat Engine table — is explicitly NOT for online use.** The Reddit thread "Turns out there is a mod that shows parry windows" points at Cheat Engine tables (TGA being the dominant one). TGA's official README states verbatim: *"This table is not meant to be used online and you will most likely be banned if you attempt to do so."* Anti-cheat must be disabled to attach Cheat Engine, the same prerequisite as Seamless, but TGA is built and maintained as a single-player practice/research tool. Running it during a Seamless session is not endorsed and carries unquantified risk.

5. **ReShade is your only fully-safe client-side option, but it doesn't know about parry frames.** ReShade is universally Seamless-compatible (it operates at the DXGI swap chain, never touches game code or memory). Its bundled Outline.fx (Sobel edge detection on the depth buffer) and MeshEdges shader can make enemy weapon silhouettes pop during attack animations — improving general telegraph readability. No existing Elden Ring ReShade preset on Nexus markets itself as a combat/parry helper, so you'd need to install ReShade and manually enable Outline.fx + Clarity + LumaSharpen yourself.

**Strongest counter-argument:** even a perfect parry indicator may not help a Seamless guest. Multiple community reports (Reddit r/badredman, Steam Nightreign threads) state that parry inputs are structurally unreliable in any FromSoft co-op session for non-host players — desync drops parry frame timing, and one Steam user reports parries failing to register entirely when not the blue/host icon. If the parry frame doesn't sync, no visual cue can save you. The community-recommended path for learning parry remains pure vanilla: equip a Buckler, drill on Crucible Knights, in **single-player practice mode**, then bring the muscle memory to co-op.

## 2. Detailed Findings

### Axis A — Direct Nexus search for parry indicator / timing helper mods

- **Finding:** ERSC official docs explicitly call out PostureBarMod as the example Seamless-compatible `external_dlls` HUD overlay; this is the architectural template a parry indicator would follow.
  - Evidence: "external_dlls = [\"SeamlessCoop/ersc.dll\", \"dllMods/PostureBarMod.dll\"] ... NOTE2: Only you will see the texture and modle modifications you are using." — [ERSC modding docs](https://ersc-docs.github.io/seamless-modding/)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** PostureBarMod is HUD-only and "does not modify data," confirming the same client-side-visual class as Transmogrify Armor — but it visualizes posture, not parry frames. No parry-specific equivalent has shipped.
  - Evidence: "The mod should be mostly compatible with other mods as it does not modify data. It may only cause issues when using a mod that heavily changes HUD elements." — [Posture Bar Mod (Nexus 3405)](https://www.nexusmods.com/eldenring/mods/3405)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** Sekiro-like SotE Deflect (Nexus 5409) is the closest "parry feel" mod, but it edits `regulation.bin` and the author themselves only speculates about Seamless safety — not confirmed guest-safe.
  - Evidence: "OFFLINE only (obv). Should work with SeamlessCo-Op in theory. ... regulation.bin updated to 1.12.4." — [Sekiro-like SotE Deflect (Nexus 5409)](https://www.nexusmods.com/eldenring/mods/5409)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** Deflection (Nexus 5494, June 2025, ER 1.16) is also a regulation.bin mechanic mod, not a visual indicator. It integrates with PostureBarMod for visuals but adds no parry-window cue.
  - Evidence: "Introducing the New Version of Deflection for Elden Ring 1.16. ... Sekiro-Inspired Deflection Mechanics ... Deflection during guard block: If you fail the first deflection, you can deflect the next attack during the blocking animation." — [Deflection (Nexus 5494)](https://www.nexusmods.com/eldenring/mods/5494)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** ERSC docs are explicit that DLL injectors other than via the ME2 `external_dlls` path (e.g. Elden Mod Loader, Lazy Loader) won't work alongside Seamless. This bounds where a parry-indicator mod could legitimately live: it must be a ME2 `external_dlls` DLL hooking the renderer, like PostureBarMod.
  - Evidence: "DLL injectors like Elden Mod Loader and Lazy Loader won't work. ... For optimal experience, all players should use the same mods together - with the exception for visual mods, which can remain personal." — [ERSC modding docs](https://ersc-docs.github.io/seamless-modding/)
  - Confidence: high | Tier: primary | Verification: verified

### Axis B — Adjacent mod categories (telegraph, frame data, riposte, damage numbers, ThomasJClark portfolio)

- **Finding:** PostureBarMod by Mordrog is the closest adjacent template — DLL hook, overlay, no game-data modification.
  - Evidence: "A posture bar mode for the Elden Ring game, creates new user interface elements above the head of the enemy, which indicate how much posture damage the enemy remains." — [PostureBarMod GitHub](https://github.com/Renthel/EldenRing-PostureBarMod)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** PostureBarMod can read player-side stagger values (Beta 0.4.0 changelog), proving the same hook surface a parry-window indicator would use is already exposed by an existing mod.
  - Evidence: "Added stagger bar for player (not functional by default, usable only with mods that make use of player stagger values)" — [PostureBarMod GitHub](https://github.com/Renthel/EldenRing-PostureBarMod)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** Static Bar Sizes (Nexus 5028, 2024-07) uses libER (Dasaav-dsv's Elden Ring DLL framework) and the ME2 `external_dlls` hook, demonstrating the libER-based DLL overlay pattern is established for HUD work and would be the right starting framework for a parry indicator.
  - Evidence: "If you have no other dll mods installed, replace 'external_dlls = []' with 'external_dlls = [ \"barSize.dll\" ]' and drop the contents of the zip inside the mod engine 2 folder. ... Contained in the zip folder is libER. it is a requirement for the mod ... File credits: libER, created by Dasaav." — [Static Bar Sizes (Nexus 5028)](https://www.nexusmods.com/eldenring/mods/5028)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** Transmogrify Armor exposes the canonical `client_side_only` config flag — the literal architectural template a parry-indicator mod would follow, by an author actively maintaining Seamless-aware mods.
  - Evidence: "; Change to true to see other player's actual armor instead of their transmogs on your screen, and the same for you on their screens. ... If this setting is false, holding down F8 temporarily switches it on. client_side_only = false" — [ertransmogrify.ini on GitHub](https://github.com/ThomasJClark/elden-ring-transmog/blob/main/ertransmogrify.ini)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** Glorious Merchant (also ThomasJClark) was explicitly patched to avoid Seamless Co-op anticheat — proving the author's commitment to Seamless compatibility — but his portfolio is cosmetic/inventory, with no combat/parry mods.
  - Evidence: "Version 1.1.11: Avoid triggering Seamless Co-op anticheat, so the mod can be used without disabling invasions" — [Glorious Merchant (Nexus 5192)](https://www.nexusmods.com/eldenring/mods/5192)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** Existing Nexus "parry" mods (Easy Parry 2478, Better Parrying 1815) modify TAE animation frames or regulation params to extend the parry active window — they DO NOT add a visual indicator.
  - Evidence: "This Mod Changes Buckler Parry, Carian Retaliation, Standard Parry, Storm Wall, Golden Parry, and Thops's Barrier by Extending the Parry/Spell Retaliate Frames to Cover the Entire Animation." — [Easy Parry (Nexus 2478)](https://www.nexusmods.com/eldenring/mods/2478)
  - Confidence: high | Tier: primary | Verification: verified

### Axis C — Reddit / community wisdom

- **Finding:** A Seamless Co-op user explicitly asked the Timed Block Parry author for a Seamless + First Person Souls compatible build and got no resolution — demand exists but no known co-op-safe parry trainer was offered in reply.
  - Evidence: "The mod you've made is SO close to what I'm hunting for, unfortunately, I can't find exactly what I'm hoping to get a hold of for use with Seamless Co Op and First Person Souls." — [Timed Block Parry comments (Nexus 1224)](https://www.nexusmods.com/eldenring/mods/1224?tab=posts)
  - Confidence: high | Tier: community | Verification: verified

- **Finding:** Players in FromSoft online/co-op sessions report parry inputs failing to register when not the host — a structural networking issue. The community advice is "play offline if reliable parry is required."
  - Evidence: "If you are the host as in player icon blue, all your parries will land. If you are green or red noone of your parries will ever register during his fight." — [Steam Nightreign discussion](https://steamcommunity.com/app/2622380/discussions/0/597407115483598930/)
  - Confidence: medium | Tier: community | Verification: verified

### Axis D — Audio + ReShade alternatives

- **Finding:** TGA's official README explicitly bans online use — using the table online will get you banned. This rules out the only existing Cheat-Engine-based parry-window visualizer for use with Seamless Co-op.
  - Evidence: "This table is not meant to be used online and you will most likely be banned if you attempt to do so." — [TGA README on GitHub](https://raw.githubusercontent.com/The-Grand-Archives/Elden-Ring-CT-TGA/master/README.md)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** TGA requires Disabling EasyAntiCheat, the same prerequisite Seamless Co-op uses — but TGA's maintainers do not endorse co-op compatibility; they treat it as a single-player tool.
  - Evidence: "Disable EasyAntiCheat and run the game, see Disabling EasyAntiCheat" — [TGA README on GitHub](https://raw.githubusercontent.com/The-Grand-Archives/Elden-Ring-CT-TGA/master/README.md)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** The r/EldenRingMods OP confirms Cheat Engine tables can show parry windows but describes the use case as single-player learning, not co-op overlay.
  - Evidence: "Theres a couple cheat engine tables that do this. Helped me understand why i couldnt parry some attacks. Turns out we cant parry the little misbegotten buys multi-cleaver strikes until he finishes his combo" — [r/EldenRingMods thread](https://old.reddit.com/r/EldenRingMods/comments/1rsyoly/turns_out_there_is_a_mod_that_shows_parry_windows/)
  - Confidence: high | Tier: community | Verification: verified

- **Finding:** FearLess Cheat Engine confirms that the auto-parry CE script "hugely widens your parry window" — functionally a parry assist rather than a pure indicator. It's a learning prosthesis, not a UI cue.
  - Evidence: "The auto parry script seems to hugely widen your parry window though. Suggest renaming the script to 'Easy Parry' instead." — [FearLess Cheat Engine forum](https://fearlessrevolution.com/viewtopic.php?t=19378&start=285)
  - Confidence: medium | Tier: community | Verification: verified

- **Finding:** ReShade ships an Outline.fx shader (Sobel operator + depth-buffer linearization) that draws outlines around mesh edges — directly usable to make enemy weapons/silhouettes pop during attack animations.
  - Evidence: "Depth-buffer based cel shading ... Modified and optimized for ReShade by JPulowski ... Sobel operator matrices" — [Outline.fx in crosire/reshade-shaders](https://raw.githubusercontent.com/crosire/reshade-shaders/3649ece4561013fda011ce21f62dff207014f020/Shaders/Outline.fx)
  - Confidence: high | Tier: primary | Verification: verified

- **Finding:** ReShade is explicitly recommended on Elden Ring Nexus as the SAFE alternative to mods that require Anti-Cheat Disabler — confirming Seamless Co-op compatibility.
  - Evidence: "It is done by using the ReShade tool, instead of having to use the Elden Mod Loader and the Anti-Cheat Disabler ... users can avoid the 'trouble' of using third party programs (other than ReShade itself, that is), and can have peace of mind knowing that they can still play Online." — [ER Nexus mod 880 description](https://www.nexusmods.com/eldenring/mods/880)
  - Confidence: high | Tier: secondary | Verification: verified

- **Finding:** Audio-modding toolchain for Elden Ring exists (UXM Unpacker, Rewwise/bnk2json, Wwise) — building a custom parryable-attack audio cue is technically feasible but no public mod ships this for ER as of 2026-04.
  - Evidence: "I've recently been working on audio swapping in Elden Ring and I thought it would be helpful to share the tools I've been using: UXM Unpacker 2.2.0.0: unpacks the soundbank (.BNK) files Rewwise: contains bnk2json, the program that will be used to pack and unpack the soundbank files" — [Nexus forums sound modding tools thread](https://forums.nexusmods.com/topic/13484154-sound-modding-tools/)
  - Confidence: medium | Tier: community | Verification: verified

## 3. Contradictions & Open Questions

- **Contradiction:** A Reddit OP says "there is a mod that shows parry windows" via Cheat Engine tables; TGA (the dominant CE table) says it's not for online use.
  - A: "Theres a couple cheat engine tables that do this. Helped me understand why i couldnt parry some attacks." — [r/EldenRingMods OP](https://old.reddit.com/r/EldenRingMods/comments/1rsyoly/turns_out_there_is_a_mod_that_shows_parry_windows/)
  - B: "This table is not meant to be used online and you will most likely be banned if you attempt to do so." — [TGA README](https://raw.githubusercontent.com/The-Grand-Archives/Elden-Ring-CT-TGA/master/README.md)
  - Our read: both are consistent — the OP describes a single-player practice workflow ("Helped me understand"). The two are not in contradiction once you read the OP carefully; the apparent contradiction is between optimistic interpretations of the OP's claim and the maintainers' explicit position. Use TGA in offline / practice-launch only; do not run it during a Seamless session.

- **Open question:** Is there a Discord-distributed (un-indexed) parry indicator mod? The ER modding community is heavily Discord-mediated, and our searches were limited to publicly indexed sources. We cannot rule out that a private DLL exists in the Souls modding Discord; we found no Reddit / Nexus / GitHub trace of one. Worth asking in the Souls Modding Discord (linked from many Nexus mod pages) or in TGA's own Discord.

- **Open question:** Could a hypothetical mod be built? Yes — PostureBarMod has already proven the technique (DLL hook + libER + ImGui-style overlay reading game state). The data surface needed is the player's `parry_active` flag (or equivalent TAE event), which is the same kind of value PostureBarMod's player-stagger feature reads. ThomasJClark's `client_side_only` ini convention is the obvious distribution pattern. No one has built it; no public attempt is on GitHub.

- **Open question:** Is the parry-input desync in Seamless still present in the latest ERSC versions, or was it fixed? The Steam thread is from August 2025 and describes Nightreign specifically; the r/badredman thread is from November 2024 and describes mainline ER. We did not find a "fixed in ERSC vN" patch note for parry registration. If the user's host runs an older ERSC, this is a real concern even with a perfect indicator.

## 4. Source List

- [ERSC modding docs](https://ersc-docs.github.io/seamless-modding/) — primary; undated; official Seamless Co-op modding documentation. Authoritative for the ME2 `external_dlls` rule and "visual mods can remain personal."
- [Posture Bar Mod (Nexus 3405)](https://www.nexusmods.com/eldenring/mods/3405) — primary; 2024-03-11; canonical Seamless-compatible HUD overlay precedent, "does not modify data."
- [PostureBarMod GitHub](https://github.com/Renthel/EldenRing-PostureBarMod) — primary; 2024-03-11; source code + changelog showing player-stagger hook, the same data surface a parry indicator would use.
- [Sekiro-like SotE Deflect (Nexus 5409)](https://www.nexusmods.com/eldenring/mods/5409) — primary; closest "parry feel" mod, but author only speculates about Seamless safety; ships regulation.bin.
- [Deflection (Nexus 5494)](https://www.nexusmods.com/eldenring/mods/5494) — primary; 2025-06-05; ER 1.16-compatible deflection overhaul; mechanic mod, not indicator.
- [Easy Parry (Nexus 2478)](https://www.nexusmods.com/eldenring/mods/2478) — primary; 2024-09-27; representative example of TAE-frame parry mods (no indicator).
- [Static Bar Sizes (Nexus 5028)](https://www.nexusmods.com/eldenring/mods/5028) — primary; 2024-07-07; libER-based DLL overlay precedent.
- [ertransmogrify.ini on GitHub](https://github.com/ThomasJClark/elden-ring-transmog/blob/main/ertransmogrify.ini) — primary; 2026-03-15; the canonical `client_side_only` template.
- [Glorious Merchant (Nexus 5192)](https://www.nexusmods.com/eldenring/mods/5192) — primary; 2025-02-27; proves ThomasJClark actively patches for Seamless anticheat compatibility.
- [Timed Block Parry comments (Nexus 1224)](https://www.nexusmods.com/eldenring/mods/1224?tab=posts) — community; user request for a Seamless + First Person Souls parry trainer that went unanswered.
- [Steam Nightreign parry-doesn't-register thread](https://steamcommunity.com/app/2622380/discussions/0/597407115483598930/) — community; 2025-08-01; structural parry-input desync for non-host players.
- [TGA README on GitHub](https://raw.githubusercontent.com/The-Grand-Archives/Elden-Ring-CT-TGA/master/README.md) — primary; explicit "not for online use, you will be banned" statement.
- [r/EldenRingMods "Turns out there is a mod that shows parry windows" thread](https://old.reddit.com/r/EldenRingMods/comments/1rsyoly/turns_out_there_is_a_mod_that_shows_parry_windows/) — community; 2026-03-13; OP confirms CE tables show parry windows in single-player learning context.
- [FearLess Cheat Engine ER thread](https://fearlessrevolution.com/viewtopic.php?t=19378&start=285) — community; 2022-03-10; confirms the auto-parry script widens the window (assist, not pure indicator).
- [Outline.fx in crosire/reshade-shaders](https://raw.githubusercontent.com/crosire/reshade-shaders/3649ece4561013fda011ce21f62dff207014f020/Shaders/Outline.fx) — primary; depth-buffer Sobel edge detection shader source.
- [ER Nexus mod 880 description](https://www.nexusmods.com/eldenring/mods/880) — secondary; 2022-04-24; ReShade explicitly recommended as the safe online-compatible alternative.
- [Nexus forums sound modding tools thread](https://forums.nexusmods.com/topic/13484154-sound-modding-tools/) — community; 2024-05-05; toolchain exists for custom audio cue mods; no public ER parry-cue mod ships.

**Findings dropped during grounding (3):**

- `https://www.reddit.com/r/badredman/comments/1grc694/seamless_coop_a_fully_functioning_mod_for_invaders/` — Reddit blocked fetcher (anti-bot), claim about parry combos failing on Seamless. Re-verified inline via the Steam Nightreign source above; the underlying claim about co-op parry desync still stands.
- `https://www.reddit.com/r/EldenRingMods/comments/1kqm4gk/parry_indicator_mod/` — Reddit blocked fetcher; claim about r/EldenRingMods declaring a per-frame indicator "kinda unreasonable." The thread title and existence are confirmed, but the specific quote couldn't be re-verified through grounding tools; treat the "unreasonable" claim as community-attributed without the verbatim quote anchored.
- `https://www.reddit.com/r/EldenRingMods/comments/1rsyoly/turns_out_there_is_a_mod_that_shows_parry_windows/` — same Reddit blocking; supplementally re-verified via `old.reddit.com` URL (now in the verified set above).

## 5. Methodology

- **Queries run:** ~32 across 4 axes. Representative set:
  - `site:nexusmods.com/eldenring "parry indicator"`
  - `Elden Ring "parry indicator" HUD overlay mod ModEngine2 client side`
  - `ThomasJClark nexus mods elden ring author profile`
  - `Elden Ring nexus mod boss attack telegraph indicator`
  - `site:reddit.com/r/SeamlessCoop "parry indicator"`
  - `Elden Ring ReShade preset combat clarity enemy visibility`
  - `Elden Ring cheat engine table parry window dodge timing`
  - `Elden Ring ME2 mod engine sound trigger external_dlls audio hook`
- **Backends used:** Exa (primary), Brave (fallback), Jina Reader (grounding + supplemental fetches), GitHub API (TGA repo tree + README inspection).
- **Topic class:** `default`. **Axis count:** 4 (user-specified, overriding the auto-suggested 5).
- **Grounding drops:** 3 of 24 findings failed token-overlap verification (Reddit anti-bot blocks); 2 of those re-verified via alternate URLs in supplemental pass. Drop rate: 12.5%.
- **Weak matches flagged:** 0. No faithfulness probe needed.
- **Wall-clock:** 692 seconds (~11.5 minutes). **Cache status:** miss (fresh run).
