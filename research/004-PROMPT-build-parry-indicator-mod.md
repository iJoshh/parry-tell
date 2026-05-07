---
artifact_type: research_prompt
created: 2026-05-05
created_by: deep-research-prompt-builder skill
target_skills: [deep-research, codex-deep-research]
purpose: Provide both research agents with a focused, technical prompt to gather everything needed to build a Seamless-Co-op-compatible parry indicator mod for Elden Ring + SotE. Generated mid-session before compact so the build session has self-contained context.
prior_artifacts:
  - research/001-client-side-mods-seamless-coop-guest.md (Claude — what mods exist)
  - research/002-parry-indicator-seamless-guest-codex-fallback.md (Codex parallel)
  - research/003-parry-indicators-seamless-coop-guest.md (Claude — confirmed no such mod exists)
context_for_post_compact_self: |
  You (post-compact Claude) are about to dispatch this prompt to deep-research and codex-deep-research in parallel. Background: Josh wants a parry indicator mod for Elden Ring + SotE that fires a visual + audio cue when an enemy is in a parryable attack window. Must be Seamless Co-op safe for guests (no regulation.bin edits, no param changes, only client-side). Prior research (003-) confirmed no such mod exists — this prompt targets the technical gaps that would let you BUILD it. Confidence on Option B (full parry indicator with hardcoded attack-ID list) was 30% pre-research; the goal of running this prompt is to push that to 60-80% by closing specific data gaps. After both research runs return, decision point: start building (Option B) or scope down to Option A (any-attack-windup indicator). The architectural template is PostureBarMod (Nexus 3405) — open-source, Seamless-compatible, ME2 external_dlls hook, ImGui overlay.
---

# Research Objective

Gather every technical piece of knowledge needed to build a client-side Elden Ring + Shadow of the Erdtree mod that displays a visual + audio cue when an enemy is currently performing a **parryable attack**. The mod must be Seamless Co-op compatible for guest (non-host) players. The user is the eventual builder.

This is a **technical / reverse-engineering research request**, not a product comparison. The agents must reach into modder communities (Discord, GitHub repos, modding wikis) for the data layer, while leaning on the published gameplay wikis (Fextralife, Eldenpedia, Reddit) only as the cross-reference layer for "what does this animation ID correspond to in human terms."

# Context and Scope

- **Purpose:** Decide whether to build (vs. commission, vs. scope down). The deciding factor is whether a published parryable-attack-ID lookup table OR an in-game "parryable window open" flag exists in any modder community resource.
- **Boundaries:**
  - Elden Ring patch 1.16+ (current as of May 2026); Shadow of the Erdtree is installed.
  - Seamless Co-op v1.9.x compatibility is non-negotiable.
  - Guest-side only — no host-side mod requirements.
  - No `regulation.bin` modification, no param edits, no TAE animation edits — read-only memory inspection plus client-side render overlay only.
- **Focus areas (in priority order):**
  1. **The data gap (CRITICAL):** Is there a published lookup of which animation IDs are parryable per enemy type? Or an in-memory "parryable now" flag the game itself maintains?
  2. **The memory layout:** Where is the current enemy animation ID + animation frame stored in memory for ER 1.16+? PostureBarMod reads stagger from somewhere; the analogous "current attack" offset is what we need.
  3. **The architectural template:** PostureBarMod's source repo, license, and how it hooks ME2. Confirm we can fork/learn-from it. Find any other ME2 client-side overlays that could serve as additional templates.
  4. **Risk management:** Anti-cheat (EAC) ban risk if the mod's DLL is loaded when the game launches without Seamless. Document the safe-launch pattern (separate ME2 profile, online-launch prevention).

# Research Requirements

## Investigation Depth

### Primary research questions (all four MUST be addressed)

1. **Does a published parryable-attack-ID lookup exist?** Search SoulsModding wiki, FromSoftware Modding GitHub orgs, the TAE Tool documentation, the ?ServerName? Discord (find an invite link if so), the Souls Modding Discord, modder personal blogs, and YouTube reverse-engineering tutorials. The data we want looks like a table of `(enemy_id, animation_id) → parryable: bool` or equivalent. Even a partial table covering common humanoids (Godrick Soldiers, Crucible Knights, Banished Knights, Tree Sentinels' light attacks if any are parryable) is valuable.

2. **Does Elden Ring expose a "parryable now" flag in memory or in a TAE event?** Sekiro and Bloodborne are documented to have a `bParryable` (or similar) TAE event that fires during the parry window. If ER inherits this from FromSoft's shared engine, **the entire data gap collapses** — read the flag, render the indicator, ship. Search for: TAE event 200/201/202/etc., `BehaviorParam`/`AtkParam_Pc`/`AtkParam_Npc` fields named "parryable" or "guardable", any community-documented reverse-engineering of ER's attack params.

3. **What's the current memory layout for enemy attack state in ER 1.16+?**
   - Where does PostureBarMod read stagger/poise from? (its source code is the answer)
   - The same `WorldChrManImp`/`ChrIns` pattern used for stagger: does it also expose `currentAnimationId`, `animationFrame`, `attackParamId`?
   - Has the layout changed across recent patches? Do we need version-specific offsets or does PostureBarMod handle that already?

4. **What's the realistic anti-cheat ban risk for a custom client-side DLL?**
   - Does Easy Anti-Cheat scan ME2-loaded DLLs by hash, by signature, or only at process launch?
   - What's the documented "safe practice" pattern? Separate ME2 profile? Renamed ER executable? `start_protected_game.exe` vs. `eldenring.exe`?
   - Are there documented bans of users running PostureBarMod or Transmogrify in offline mode? (If those mods are safe in practice, ours should be too — same architecture.)
   - **Concrete failure mode:** if our DLL is loaded when ER launches *without* Seamless attached → EAC visible → permanent account flag. Confirm the mitigation pattern.

### Secondary research questions

- Is there a published ME2 client-side DLL boilerplate / template the modding community uses?
- Are there active modder Discord servers where we can ask one specific question if we get stuck? (Surface invite links + the right channel name.)
- What's the maintenance burden? How often does an ER patch break PostureBarMod or Transmogrify, and how fast does the community ship offset updates?
- Does Seamless Co-op host integrity-check `external_dlls` against an allowlist? (Prior research suggests no — Transmogrify and PostureBarMod both work for guests without host approval — but confirm.)

### Explicit exclusions (do NOT spend research budget on these)

- Mods that already exist for parry indication — prior research (003) already confirmed none exist that meet our constraints.
- Cheat Engine tables — already covered in prior research; not a viable path because they're explicitly online-banned by the author.
- Player-facing parry guides on Fextralife / Eldenpedia / Reddit — only useful as the *cross-reference* layer for question 1; do not redo the gameplay-mechanics research.
- Mods that change parry windows / mechanics — out of scope; we want indicator-only.

## Evidence Standards

- **Source priority (high to low):**
  1. Source code of existing Seamless-compatible client-side ME2 mods (PostureBarMod GitHub, Transmogrify GitHub, any others surfaced)
  2. SoulsModding wiki + FromSoftware Modding org GitHub repos
  3. Modder Discord invite links + named channels (agents can't read Discord but can surface "ask in #elden-ring-dev on this server" as actionable findings)
  4. Reverse-engineering blog posts and YouTube tutorials with specific offsets / IDs
  5. Recent (2025-2026) Reddit threads on r/EldenRingMods discussing memory offsets or modding internals
  6. Player-facing wikis (Fextralife, Eldenpedia) — for cross-referencing animation IDs to human-readable attack names ONLY
- **Recency:** ER 1.16+ era only for memory offsets. Older info is fine for architectural concepts (TAE structure, FromSoft engine patterns) since the engine is conserved across ER patches.
- **Citation requirements:** Every memory offset, animation ID, or attack-param reference must be cited with the source it came from. The prior research had a 3.6% drop rate on grounding — aim to match or beat that.

## Analysis Framework

### Technical research framework (custom — beyond standard Technical category)

For each primary question above, the agent should:

1. **Identify the canonical source** if it exists (a single GitHub repo, a single wiki page, a specific modder's blog).
2. **Quote the relevant data** verbatim where possible — actual offsets, actual TAE event numbers, actual struct field names.
3. **Cross-reference** between modder data and player-facing wikis where applicable. Example: "TAE event 232 (`bParryStart` per SoulsModding wiki page X) corresponds to the parry-window start of attacks Fextralife describes as 'parryable wind-up frames' on page Y."
4. **Flag confidence:** for each finding, mark as `verified` (multiple corroborating sources), `single-source` (one source only), or `community-asserted-unverified` (Reddit comment / Discord screenshot without source code backing).
5. **Surface gaps explicitly:** if a primary question has NO good answer, say so directly. Better a clean "no published lookup table exists; the SoulsModding Discord has one but we'd need to ask there" than a confident wrong answer.

# Output Structure

## Required structure (this is non-negotiable for both runs)

The artifact must use this exact top structure so post-compact Claude can re-orient in 30 seconds:

```
# Title

## TL;DR (≤200 words)
- Decision-relevant answer to: "Should Josh build the parry indicator mod?"
- The single most important finding for each of the 4 primary questions
- Updated confidence estimate on Option B (vs. the pre-research 30%)
- The next concrete action

## Critical Findings (≤500 words)
For each of the 4 primary questions:
- Question
- Answer (1-3 sentences)
- Confidence (verified / single-source / community-asserted-unverified)
- Source URL(s) — direct, clickable

## Decision Point
- If the data gap closes (parryable-flag exists OR lookup table exists) → recommend BUILD with concrete next steps
- If the data gap stays open → recommend either SCOPE DOWN (Option A: any-windup indicator) or COMMISSION
- Be opinionated, not equivocal

## Appendices (everything else, organized by question)
### Appendix A: Data layer (parryable IDs / flags)
### Appendix B: Memory layout (offsets, struct shapes)
### Appendix C: Architectural template (PostureBarMod analysis)
### Appendix D: Anti-cheat risk + safe-launch pattern
### Appendix E: Modder communities (Discord links, GitHub orgs, key people to ask)
### Appendix F: Sources + verification stats
```

The TL;DR + Critical Findings + Decision Point sections are what post-compact Claude reads first. Everything else is reference material for the build session.

# Quality Instructions

## Reasoning Approach

- **Build from concrete to abstract.** Start by finding source code (PostureBarMod), reading what it actually does, and using that as the ground truth. Then validate community claims against the source. Reddit comments asserting "X is at offset 0xABCD" are not trustworthy unless backed by source code or a reverse-engineering writeup with screenshots.
- **Treat Discord-only knowledge as a known unknown.** Both research skills can find Discord invite links via web search, but neither can read Discord history. When a research thread leads "into the Discord" (e.g., "the parryable ID list is pinned in #elden-ring-modding on the SoulsModding Discord"), surface that as a finding with the invite link + channel name, marked `community-asserted-unverified`. Don't try to fabricate the answer.
- **Be honest about gaps.** A clean "we couldn't find this" with a concrete next step (which Discord, which person to ask) is more valuable than a fabricated answer. The downstream cost of a wrong memory offset is "we waste a day debugging crashes."

## Critical Evaluation

- **Cross-reference memory offsets across at least 2 sources** before treating them as verified. Patches can change them; community resources can lag.
- **Distinguish "Sekiro pattern" from "ER pattern."** It's tempting to assume ER inherits Sekiro's TAE event for parryable attacks. They share an engine but ER has different params. Don't assert ER has feature X just because Sekiro does — verify.
- **Note confidence levels everywhere.** The downstream decision-maker (post-compact Claude) is going to act on this artifact. False confidence costs us a day of game-modding debugging; appropriate hedging keeps the decision honest.

# Topic-Specific Instructions

## Discord-aware research

Both deep-research and codex-deep-research can surface Discord invite links via web search. They cannot read Discord. The expected workflow when a finding lives in Discord:

1. Surface the server name, invite link, and the most likely channel name.
2. Mark it `community-asserted-unverified` in the findings.
3. In the TL;DR, note "X piece of info requires asking in Y Discord — not blocking but would tighten Z confidence."
4. Do NOT fabricate the answer.

## Source code reading

PostureBarMod (Nexus 3405) is open-source. Find its GitHub repo (likely linked from the Nexus page or the author's profile). Once found, the agents should:

1. Read the README for architectural overview.
2. Find the file/function where it reads stagger/poise. That's our template for reading attack state.
3. Quote the relevant memory offset / hook pattern verbatim in Appendix C.
4. Note what license the code is under (license affects whether we can fork directly or need to write from scratch).

## Cross-reference parryable mechanics with player wikis

The player wikis (Fextralife, Eldenpedia) have the *human-readable* parryable list. The modder data has the *machine-readable* IDs. The artifact should provide at least 2-3 examples of the cross-reference: "Animation ID 30220 on enemy class HumanSoldier corresponds to Fextralife's 'standard sword overhead' parryable attack." This bridges the two knowledge layers and validates that we're looking at the right data.

## Bias correction: don't over-research the easy stuff

Prior research (artifacts 001 and 003) thoroughly covered:
- What Seamless Co-op tolerates client-side
- Which Nexus mods are co-op safe
- The PostureBarMod / Transmogrify architectural pattern at a high level

Don't re-do that work. Reference the prior artifacts and build on them. The research budget should go almost entirely to questions 1, 2, and 4 — the technical gaps that block building.

# Final Note to the Research Agents

The user (Josh) is willing to spend a day building this mod IF the technical pieces line up. He's not interested in spending a day building it on a bad data foundation and crashing the game 50 times. The single highest-leverage finding either of you can return is **a published parryable-flag (TAE event or memory bit) that the game itself maintains**. If that exists, this becomes a 4-hour mod. If it doesn't, it becomes a multi-week reverse-engineering project, and Josh will probably take Option D (ReShade + practice solo) instead.

So: prioritize the parryable-flag question. Spend disproportionate budget on it. Even a "not exposed in memory but documented to exist as TAE event N" answer is valuable — it tells us whether the underlying game has the concept (good news for a heuristic-based approach) or doesn't (we have to brute-force the ID list ourselves).

Save the artifact under `/home/joshua.blattner/claude/elden-ring/research/`. Use sequential numbering (next is 005 or 006 depending on which run finishes first; coordinate via filename if both arrive simultaneously — `005-claude-` and `005-codex-` prefix is fine).
