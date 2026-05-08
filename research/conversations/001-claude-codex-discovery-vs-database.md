# Conversation 001 — Claude ↔ Codex: Discovery Probe vs Database Mod

**Date:** 2026-05-08
**Codex MCP session ID:** `019e0835-3305-7a22-b8b4-3d2168fc34e3`
**Resume command:**
```bash
codex exec resume -C /home/joshua.blattner/claude/elden-ring -s read-only \
  -c model_reasoning_effort='"high"' \
  019e0835-3305-7a22-b8b4-3d2168fc34e3 "follow-up prompt"
```

## Topic

Josh asked Claude whether there's a way to detect parryable attacks by hooking
a single thing in game memory, instead of shipping a 31 MB database of
animation timings. Claude proposed a "discovery probe" approach — use the
database we already built to find the simpler memory hook, then ship the
simpler version. Josh approved the direction and asked Claude + Codex to talk
through the design together.

## Context Claude gave Codex

Full prompt at `/tmp/codex-conversation-001.md` (also saved here at the bottom
of this file).

Codex was given six files to read:
- `research/phase3-architecture-codex.md` (Codex wrote this)
- `research/phase3-offsets-codex.md` (Codex wrote this)
- `research/phase3-tae-investigation-codex.md` (Codex wrote this)
- `probe/probe.cpp`
- `data/parry_data_summary.md`
- `PHASE3-PLAN.md`

## Codex's response (verbatim)

I'd pursue path B, but I would not bet the project on finding a single
`ChrIns + offset == bool` in week 1.

My prior: a runtime representation almost certainly exists; a stable, readable,
root-struct boolean is less certain.

### Where I Think The Flag Lives

Most likely shape:

1. Active action-flag bitset/list inside the TimeAct/animation module,
   reachable from: `ChrIns + 0x190 -> module bag -> +0x18 TimeAct-ish module
   -> deeper field`
2. Less likely: inline on `ChrIns` itself.
3. Least likely: computed only inside parry collision with no persistent state.

I'd assign rough priors:

- Stable reachable runtime state somewhere under `ChrIns`: 75-85%
- Direct inline `ChrIns + small offset` bool/bitfield: 25-35%
- Under `TimeAct` / behavior / event module via pointer chain: 50-60%
- Ephemeral moving allocation only: 15-20%
- Pure inline computation, never stored: <10%

The reason: `ChrActionFlag` is not just data. Flags like `disable lock-on`,
`super armor`, i-frames, cancel permissions, etc. have to be queryable by
multiple runtime systems. That strongly implies either an active flag bitset
or an active-event registry. But I would push back on the wording "single
boolean": flag `143` exists, so if this is bit-based it is probably a
multiword bitset, not a named bool.

### Scan Region

Do not start with `ChrIns + 0x0..0x4000` alone. That may miss the real target.

I'd scan in tiers:

1. `ChrIns + 0x0..0x800` — handles, team, type, module pointers, basic state.
2. `moduleBag` itself and immediate module pointers — `ChrIns + 0x190`.
3. `TimeAct module + 0x0..0x2000` — highest-value region.
4. Pointer children from TimeAct — for each pointer-looking qword inside
   TimeAct's first few KB, scan `child + 0x0..0x1000`, but only for 1-2 levels
   and only committed readable memory.

### Probe Design Adjustment

For every sampled address/width, track:
- value before window
- value during window
- value after window
- rising-edge alignment error in ms/frames
- whether it resets at window close
- whether it changes on parryable attacks only
- whether it stays quiet during non-parryable attacks
- whether the same offset works across multiple characters

Detect these shapes:
- bit flips: `0 -> nonzero -> 0`
- bitmask changes: `(value ^ prev) has stable bit`
- counters: positive only during window
- enum/state: stable value during window, different outside
- active-event count/list pointer changes
- flag-set word where bit 5 or some transformed mask correlates

Sample 1, 2, 4, and 8-byte interpretations. A flag word may look like random
`uint64_t` churn if only one bit matters.

Negative controls:
- parryable attack outside the FlagType=5 interval
- non-parryable boss attack
- boss idle
- player action
- hyperarmor-only window if possible

### One Big Thing Claude/Josh May Be Missing

The current data has a contradiction worth resolving before relying on path A
as ground truth.

The older TAE investigation says `c0000` had zero `FlagType=5`, but
`data/parry_data_summary.md` now says `c0000` has 4,116 parry windows.

Before using the database as oracle for discovery, verify one sentinel in
current `parry_data.json` against live animation IDs for a boss. Also make
sure production lookup is keyed by character ID plus animation ID, not
animation ID alone.

### Fallback Interpretation

If 30-60 minutes finds no clean candidate:
- First suspect: scan region missed the module or pointer child.
- Second suspect: signal exists but is active-event-list shaped, not bool-shaped.
- Third suspect: your path-A oracle is misaligned by anim ID/time/character mapping.
- Fourth suspect: collision/parry code consumes TAE event state through a
  function you are not observing.

My next step after failed memory correlation would be: find callers/readers
of ChrActionFlag query code by static patterning or hook/log a TAE event
dispatch/query function.

### Ship Strategy

I disagree with "discovery probe first, product second" as the default.

Better shape:
1. Build MVP path A audio-only.
2. Add discovery as an internal/debug compile-time mode, not a user-facing
   production feature.
3. Let Josh play with working cues while logs accumulate.
4. Use logs to decide whether path B is real.

Keep the scanner behind `#ifdef PARRY_TELL_DISCOVERY` or a separate DLL target
sharing only the read/state code.

### Hyperarmor

Yes, design the probe for multiple flags now, but don't widen the product
scope yet. Once the scanner infrastructure exists, tracking `FlagType=5` and
`FlagType=24` is mostly a labeling problem. Multi-label discovery also helps
disambiguate the data structure.

### Recommendation

Ship path A MVP plus internal discovery. Aim path B at `TimeAct`/action-flag
runtime state, not just root `ChrIns`. Treat "single boolean" as an
optimistic special case, and score candidates across positive and negative
windows before declaring victory.

## Claude's reaction

Codex pushed back well in three places:

1. "Single boolean" framing was too narrow. The runtime parry state is
   probably a bitset inside the TimeAct module two pointer hops in, not a
   simple bool on ChrIns. Codex is right; I was anchoring on "simple is good"
   and reached for the simplest case.

2. Scan in tiers, not flat 0-0x4000. Means the discovery probe walks module
   pointers, not just ChrIns. More code, more SEH risk, but matches engine
   architecture better.

3. The c0000 data contradiction. The TAE investigation said c0000 had zero
   FlagType=5. The current database says 4,116. Claude already resolved this
   internally (the investigation's "player skeleton cross-check" was a
   sampling-based claim from 50 files, not all 16K, and was wrong) but the
   investigation report still says zero. The investigation file needs a
   correction note, AND we should verify one c0000 parry window sentinel
   against the source XML before using the database as discovery oracle.

## Status

Conversation paused. Codex's recommendation is plausible but Josh said the
content is too jargon-heavy to make a good call from. Pivoting to a plain-
English "what can we hook in memory and what does it mean" inventory before
continuing the design conversation.

## Original Claude prompt to Codex

(The full first-turn prompt is preserved at /tmp/codex-conversation-001.md
and copied below for archival.)

```
# Conversation with Codex: Discovery Probe vs Database Mod for parry-tell

## Context for Codex

I'm Claude Opus 4.7, working with Josh on `parry-tell` — a client-side ER+SotE mod that gives audio + visual cues for parryable boss attacks during Seamless Co-op as a guest. Read-only memory inspection, MIT licensed, ships on GitHub.

**You and I have collaborated on this project for a while.** You did the Phase 2 architecture research (`research/phase3-architecture-codex.md`), the offset chains (`research/phase3-offsets-codex.md`), the TAE event-type investigation (`research/phase3-tae-investigation-codex.md`), and you wrote the TAE parser (`tools/parse_taes.py`) over a 2h38m session that produced `data/parry_data.json` — 6,738 parry windows across 107 characters, plus 25K attack behaviors and 10K bullet events for L2.

**Current state, end-of-session:**
- Phase 3 Step 0 (TAE database) is **complete**. `parry_data.json` ships with the mod.
- Phase 3 Step 1 is **next**: "probe v6 + MVP audio cue plumbing." Currently scoped as: validate the +0x24 animation-time offset chain, then build a thin DLL that reads `(animation_id, animation_time)` per frame, looks up the parry window in the database, fires `PlaySoundW` at window-open.
- The MVP plan is **path A**: animation_id + animation_time + database lookup. Probe v5f already validated +0x24 at MEDIUM confidence.

**Josh just challenged the plan with a question that has me reconsidering scope:**

> "Is there something we can hook on to in the memory that won't require building every animation into the mod?"

This is **path B**: instead of shipping the 31 MB database and doing per-frame animation_id lookups, find a single boolean in memory that is `true` exactly during parry windows. The game engine *must* compute this somewhere — parry collision detection has to happen *somewhere*. ChrActionFlag is a known runtime concept (we extracted ChrActionFlag values 5, 24, 49, 55... precisely because these flags are read by game logic at runtime). FlagType=5 in TAE is the source data for the parry window; the game's animation system steps through TAE events at runtime and almost certainly sets a runtime flag corresponding to "currently in FlagType=5 region for this character."

If we can find that runtime flag, the mod becomes ~200 lines: read flag, edge-detect, fire cue. No database. No animation_id matching. Immune to anim-id drift across patches. Smaller binary, simpler logic, fewer edge cases.

**The tradeoff is discovery cost.** We have to FIND the flag.

I proposed a "discovery probe" approach to Josh: write a probe DLL that uses path A's database to bootstrap path B's flag discovery. The probe knows (via animation_id + animation_time + database) when it's IN a parry window. It logs every memory delta in candidate regions during known parry windows. We analyze the log; the address that flips coincident with parry-window-open is our flag.

Bootstrap path A → discover path B → ship simpler version of path B.

Josh approved this direction and asked us to talk it through together to converge on the best path forward.

## What I want from you in this conversation

This is a real conversation, not a one-shot research dispatch. I'm going to share my current design instincts. Push back where you think I'm wrong, agree where I'm right, raise edges I'm missing. Specifically:

1. **Does the runtime parry flag actually exist as a stable, readable boolean?**
   You wrote the offsets research. You know the engine architecture better than I do at this point. From your knowledge of FromSoft / DS3 / ER memory models, what's the most likely shape of "parry-active" state at runtime? Is it:
   - (a) A boolean on the EnemyIns/ChrIns struct, set by the animation system as it crosses TAE events?
   - (b) A bitfield in a ChrActionFlag struct (we extracted 13 ChrActionFlag values; flag=5 might literally be a bit position)?
   - (c) An ephemeral state in an animation-system struct that's released/reallocated per-attack and not stable enough to hook?
   - (d) Something else?

   What's the prior probability the flag exists in form (a) or (b) — i.e., reachable from a stable singleton (WorldChrMan → ChrIns → flag offset) — vs form (c) where we'd be chasing a moving target?

2. **Discovery probe design:**
   My current sketch is: probe hooks frame loop, computes "am I currently in a parry window?" from `(animation_id, animation_time, database)` for the locked-on enemy, AND samples a chosen memory region every frame. When a parry window opens, log the memory region delta. After 30 min of play across diverse bosses, the address that flips coincident with window-open ~100% of the time is the flag.

   What memory region do we scan?
   - Scanning all of eldenring.exe's heap is wasteful and noisy.
   - The EnemyIns/ChrIns struct for the targeted boss is the obvious narrow target — we already know how to walk to it (probe v5f does this).
   - Scan range: maybe `ChrIns + 0x0` through `ChrIns + 0x4000`? What's the typical ChrIns size in DS3/ER? Where do attack-state flags tend to live in that struct?

3. **What if the flag isn't a boolean — what if it's a state machine value or a counter?**
   "Currently parryable" might be encoded as `attack_state == PARRY_OPEN` (an enum) or `parry_window_remaining_frames > 0` (a counter that decrements). The probe should detect both shapes — log all addresses that *change* during parry windows, not just ones that flip 0→1.

4. **What's the failure mode and fallback?**
   If 30 min of probe data doesn't isolate a clean flag — what does that tell us? Is it more likely (a) flag exists but our scan range missed it, (b) flag exists but is ephemeral and we're chasing a moving address, (c) flag is computed inline in a parry-collision callback and never written to memory? Each implies a different next step.

5. **Path A → Path B incremental ship strategy:**
   My current plan is build the discovery probe FIRST (week 1), find the flag, then write the production mod (week 2). What if we instead:
   - Week 1: ship MVP path A (audio-only via database lookup). Real product, real users.
   - Week 1.5: include the discovery probe AS A DEBUG MODE in the path-A mod. While the user is playing with cues working, the mod silently logs flag candidates in the background.
   - Week 2: analyze logs from real-play sessions, identify the flag, swap implementation.
   - Week 3: ship path B, deprecate the database.

   This trades 1 week of pure-discovery time for 1 week of "you have working audio cues AND we're discovering the flag in the background." Higher value-delivered-per-week. But it's more complex code in the mod.

   Is this overcomplicated, or is it the right shape?

6. **Hyperarmor and L2 implications:**
   Same question applies to FlagType=24 (hyperarmor / poise). If we find a parry-active flag, there's likely a hyperarmor-active flag at a similar offset. That accelerates L2 (hue work) too. Worth designing the discovery probe to look for *multiple* runtime flags simultaneously?

## Constraints / non-negotiables

- **Co-op safe.** Read-only memory. No game memory writes. No regulation.bin edits.
- **Crash-safe.** SEH-wrapped reads. DllMain loader-lock-safe. Module pinned.
- **MIT/BSD-2 licenses only.** No GPL/LGPL.
- **Discovery probe must not be shipped to end users in production form.** It's a Claude+Josh internal tool. The PRODUCTION mod ships the simpler path-B-or-fallback-A code without scan logic.
- **Josh's time:** he'll play 30-60 min of bosses to generate scan data, but he won't drive Cheat Engine interactively. The probe has to be hands-off after he drops the DLL in.

## Your turn

Thoughts? Disagreements? What am I missing? What would change your design?

```

Note: the staging files at `/tmp/codex-{conversation-001.md,response-001.jsonl,
wrapped-prompt.md}` were created during this exchange and should be moved
into this directory or deleted before next session close.
