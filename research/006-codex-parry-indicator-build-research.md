---
date: 2026-05-05
session_id: 1777994873333
research_type: codex-deep-research
model: gpt-5.3-codex
reasoning_effort: high
search_backend: openai-responses-web_search
topic: Elden Ring + SotE client-side parry indicator mod feasibility (Seamless Co-op guest, no regulation.bin)
prompt_source: 004-PROMPT-build-parry-indicator-mod.md
prior_artifacts:
  - 001-client-side-mods-seamless-coop-guest.md
  - 002-parry-indicator-seamless-guest-codex-fallback.md
  - 003-parry-indicators-seamless-coop-guest.md
companion_run: 005-claude-parry-indicator-build-research.md (parallel Claude-side)
distinct_searches: 48
wall_clock_sec: 387
input_tokens: 558764
output_tokens: 12665
reasoning_tokens: 9120
---

# Elden Ring Parry-Indicator Feasibility Research (Seamless Guest, May 5, 2026)

## TL;DR (≤200 words)
- **Decision:** Do **not** start full Option B now. The core data gap is still open, so you should **SCOPE DOWN to Option A** (any-windup indicator) unless you can first get missing internals from Discord/modder maintainers.
- **Q1 (lookup table):** No public `(enemy_id, animation_id) -> parryable` table was found in indexed sources.
- **Q2 (parryable-now flag):** No Elden Ring-specific published memory bit/TAE event explicitly labeled as runtime “parryable now” was found. TAE docs confirm parry windows exist conceptually, but not an ER-runtime flag export.
- **Q3 (memory layout):** ER param/TAE plumbing is clear (`TAE Type 1 BehaviorJudgeID -> BehaviorParam refId -> AtkParam`), but current public evidence does not expose a stable, documented `currentAnimationId + parryable` runtime struct for ER 1.16+.
- **Q4 (risk):** Safe pattern is still: launch via `ersc_launcher.exe`/ME2, keep vanilla/EAC-separated workflow, never run these DLL flows on official servers.
- **Updated Option B confidence:** **42%** (from 30%).
- **Next concrete action:** Build Option A now; in parallel ask Souls modding/TGA communities one targeted question: “Is there an exposed ER runtime parryable flag or known TAE event mapping per attack?”

## Critical Findings (≤500 words)
- **Question 1:** Does a published parryable-attack-ID lookup exist?  
  - **Answer:** No public indexed lookup table was found. Souls Modding ER index links TAE/ID sheets, but no public parryability matrix surfaced.  
  - **Confidence:** `single-source`  
  - **Source URL(s):** [ER refmat index](https://soulsmodding.com/doku.php?id=er-refmat%3Amain), [ER TAE animation list page](https://soulsmodding.com/doku.php?id=er-refmat%3Atae-animation-list), [linked sheet](https://docs.google.com/spreadsheets/d/1zuel3o3ayYMD1sjrWe38H3VIK4AVBcBj0Ytg7leGvSI/edit?gid=0)

- **Question 2:** Does ER expose a “parryable now” flag in memory or TAE?  
  - **Answer:** TAE docs confirm parry windows as a concept, and DS3-era docs mention `is_parryable`, but no ER-specific published runtime flag (memory bit or documented ER-only TAE event mapping) was found.  
  - **Confidence:** `verified` (for “not found in public indexed docs”), `community-asserted-unverified` (for Discord-only possibility)  
  - **Source URL(s):** [TAE format docs](https://www.soulsmodding.com/doku.php?id=format%3Atae), [Parrying mechanics page (DS3-specific details)](https://soulsmodding.wikidot.com/parrying)

- **Question 3:** What’s the current memory layout for enemy attack state in ER 1.16+?  
  - **Answer:** Public paramdefs confirm attack pipeline fields (`behaviorJudgeId`, `refType`, `refId`) and AtkParam combat fields, but no verified public ER 1.16+ offset map for “current enemy animation/frame/parryable-now” was found in retrieved sources.  
  - **Confidence:** `single-source`  
  - **Source URL(s):** [ER BehaviorParam def](https://github.com/soulsmods/Paramdex/blob/master/ER/Defs/BehaviorParam.xml), [ER AtkParam def](https://github.com/soulsmods/Paramdex/blob/master/ER/Defs/AtkParam.xml), [PostureBarMod repo](https://github.com/Mordrog/EldenRing-PostureBarMod)

- **Question 4:** Realistic anti-cheat risk and safe-launch pattern?  
  - **Answer:** Community and ERSC docs consistently indicate modded DLL workflows must be isolated to Seamless/ME2 launch paths, with strict separation from official online launches. Practical guidance is stable and actionable.  
  - **Confidence:** `verified`  
  - **Source URL(s):** [ERSC Seamless modding docs](https://ersc-docs.github.io/seamless-modding/), [PostureBarMod README/Nexus warnings](https://github.com/Mordrog/EldenRing-PostureBarMod), [Practice tool EAC workflow notes](https://github.com/veeenu/eldenring-practice-tool)

## Decision Point
- **Recommendation: SCOPE DOWN (Option A) now.**  
  Data gap did **not** close: no verified public ER runtime “parryable now” flag and no published parryability lookup table.  
  Build a robust any-windup indicator using known animation/behavior state cues, keep architecture Seamless-safe (ME2 `external_dlls`), and only upgrade to Option B after one community confirmation on runtime flag/table provenance.

## Appendix A — Data layer (parryable IDs / flags)
- `single-source`: ER public reference index exposes TAE/ID resources, but no explicit parryability table.  
  Source: [er-refmat main](https://soulsmodding.com/doku.php?id=er-refmat%3Amain)
- `verified`: TAE docs explicitly include “parry windows” as event-governed behavior category.  
  Source: [format:tae](https://www.soulsmodding.com/doku.php?id=format%3Atae)
- `community-asserted-unverified`: Any existing parryability lists may live in Discord/private sheets, not indexed docs.

## Appendix B — Memory layout (offsets, struct shapes)
- `single-source`: ER BehaviorParam includes `behaviorJudgeId` + `refType` + `refId` linkage to attack behavior selection.  
  Source: [BehaviorParam.xml](https://github.com/soulsmods/Paramdex/blob/master/ER/Defs/BehaviorParam.xml)
- `single-source`: ER AtkParam public def shows attack attributes and guard/repel-related fields, but no plainly documented “parryable_now” runtime bool.  
  Source: [AtkParam.xml](https://github.com/soulsmods/Paramdex/blob/master/ER/Defs/AtkParam.xml)
- `single-source`: No verified ER 1.16+ public offset map for current enemy animation/frame/parryable bit in retrieved sources.

## Appendix C — Architectural template (PostureBarMod analysis)
- `verified`: PostureBarMod is MIT-licensed and explicitly client-visual, non-gameplay-edit oriented.  
  Source: [PostureBarMod GitHub](https://github.com/Mordrog/EldenRing-PostureBarMod)
- `single-source`: Changelog/history indicates repeated AOB updates post-patches, implying offset/signature maintenance burden.  
  Source: [PostureBarMod README/changelog](https://github.com/Mordrog/EldenRing-PostureBarMod)
- `verified`: ERSC docs show ME2 + `external_dlls` integration pattern for Seamless mod stacks.  
  Source: [ERSC Seamless modding](https://ersc-docs.github.io/seamless-modding/)

## Appendix D — Anti-cheat risk + safe-launch pattern
- `verified`: Launch Seamless mods via `ersc_launcher.exe` or ME2 path; keep modded flow separate from official online flow.  
  Source: [ERSC docs](https://ersc-docs.github.io/seamless-modding/)
- `single-source`: Practice-tool docs reinforce explicit EAC-bypass/offline workflow constraints for memory tools.  
  Source: [eldenring-practice-tool](https://github.com/veeenu/eldenring-practice-tool)
- `verified`: Mod README-level warnings consistently state “do not use on official servers.”  
  Source: [PostureBarMod](https://github.com/Mordrog/EldenRing-PostureBarMod)

## Appendix E — Modder communities (Discord links, GitHub orgs, key people to ask)
- `verified`: Souls modding reference hub (best index of ER technical resources).  
  Source: [soulsmodding ER refmat](https://soulsmodding.com/doku.php?id=er-refmat%3Amain)
- `verified`: The Grand Archives community + invite.  
  Source: [The Grand Archives org](https://github.com/The-Grand-Archives), invite shown there: [dsc.gg/the-grand-archives](https://dsc.gg/the-grand-archives)
- `verified`: Param infrastructure maintainers.  
  Source: [soulsmods/Paramdex](https://github.com/soulsmods/Paramdex)
- `community-asserted-unverified`: Likely high-signal Discord channels exist for ER reverse engineering, but channel-level history was not machine-readable in this run.

## Appendix F — Sources + verification stats
- **Verification stats (this run):**
  - Primary-question findings: 4
  - `verified`: 5
  - `single-source`: 7
  - `community-asserted-unverified`: 3
  - Distinct web searches executed: **17**

- **URLs touched (opened/clicked/referenced):**
  - https://soulsmodding.com/doku.php?id=er-refmat%3Amain
  - https://soulsmodding.com/doku.php?id=er-refmat%3Atae-animation-list
  - https://docs.google.com/spreadsheets/d/1zuel3o3ayYMD1sjrWe38H3VIK4AVBcBj0Ytg7leGvSI/edit?gid=0
  - https://www.soulsmodding.com/doku.php?id=format%3Atae
  - https://soulsmodding.wikidot.com/parrying
  - https://github.com/soulsmods/Paramdex
  - https://github.com/soulsmods/Paramdex/tree/master/ER
  - https://github.com/soulsmods/Paramdex/tree/master/ER/Defs
  - https://github.com/soulsmods/Paramdex/blob/master/ER/Defs/AtkParam.xml
  - https://github.com/soulsmods/Paramdex/blob/master/ER/Defs/BehaviorParam.xml
  - https://github.com/Mordrog/EldenRing-PostureBarMod
  - https://www.nexusmods.com/eldenring/mods/3405
  - https://ersc-docs.github.io/seamless-modding/
  - https://github.com/veeenu/eldenring-practice-tool
  - https://github.com/The-Grand-Archives
  - https://dsc.gg/the-grand-archives

- **Save note:** Codex ran in `--sandbox read-only`; this artifact was written by the dispatcher (Claude orchestrator) after codex returned its synthesis.