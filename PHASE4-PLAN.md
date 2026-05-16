---
status: draft
phase: 4
started: 2026-05-11
timezone: America/Chicago
project: elden-ring-parry-indicator-mod
codename: parry-tell-probe
release_target: v0.1.0
bundle: A
scope: MVP audio cue + L1 target filter
supersedes: PHASE3-PLAN.md for Bundle A implementation planning
---

# Phase 4 Plan - Bundle A `v0.1.0`

Bundle A ships the first usable release: real-time parry-window audio cue plus
the L1 "boss is targeting Josh" filter. Hue overlay is Bundle B and is
explicitly out of scope here.

The audio cue is the core value-delivery for Bundle A. Timing is what saves
the parry; the hue overlay (Bundle B) is preemptive visual prep, not the
critical path. A future-self re-reading this plan should not second-guess the
decision to ship audio alone first. Bundle A is not a partial product — it is
the timing primitive the entire feature is built around, with target filtering
as a noise-suppression layer for co-op.

The major context change since Phase 3 is that the data layer is no longer a
speculative risk. On 2026-05-11 evening, live qualification
`qualification-20260511-195759` passed end-to-end on real combat footage:
Godrick Soldier, 12,467 samples, 144 s, `field_at_0x064 = 4311 -> c4310` via
family fallback, `TimeAct +0x24` anim time, and 5/8 DB windows matched within
`+/-11 ms` (62.5% vs the 60% PASS gate). Phase 4 should therefore focus on
turning the proven data collection path into a read-only real-time predictor.

## Scope

Bundle A includes:

- Audio cue for predicted parry-window open.
- Runtime lookup from `(c_id_family_or_exact, anim_id)` to parry windows.
- L1 target filter: only cue when the boss's current target-of-attention is
  the local player.
- A regression path that keeps the v6.4 binary capture available behind an
  INI flag.
- Prediction decision logging for offline validation.
- Release packaging for `v0.1.0`.

Bundle A does not include:

- D3D12 hooks.
- Screen-edge hue overlay.
- Lock-on integration, except as a read-only signal that may help future
  research or validation. Bundle A audio fires regardless of lock-on state.
- Co-op partner prioritization beyond the L1 self-target check.
- Multi-boss prioritization beyond the current boss-bar roster behavior.
- Any write to game memory.
- Any read or write of `regulation.bin` or base game files.

## Safety Invariants

- Read-only game memory access only.
- SEH-wrap every memory dereference through `SafeRead` / `SafeReadBytes`
  style helpers. Current source has this pattern at `probe/probe.cpp:572-598`.
- Keep DllMain loader-lock-safe: no heavy work in DllMain, spawn worker thread
  on attach, do not wait inside detach. Current source follows this at
  `probe/probe.cpp:3385-3423`.
- Do not add new third-party dependencies without a written justification.
  MinHook is already vendored and BSD-2-licensed.
- Keep data collection and prediction non-invasive. Failing to read a field
  means skip that boss/tick, log if useful, and continue.

## Source Anchors

- Current offsets live in `probe/probe.cpp:200-260`.
- Current INI parser is hand-rolled at `probe/probe.cpp:390-570`.
- Current enemy snapshot reads:
  - `ChrIns +0x190 -> ChrModuleBag`
  - `ChrModuleBag +0x18 -> TimeAct`
  - `TimeAct +0xD0 -> anim_id`
  - `TimeAct +0x20/+0x24/+0x28/+0x2C -> anim_time candidates`
  - `ChrIns +0x580 -> ai bag +0xC0 -> ai_struct`
  at `probe/probe.cpp:1450-1531`.
- Current player and lock-on reads include `PlayerIns +0x6B0` at
  `probe/probe.cpp:1855-1970`.
- Current boss-bar and roster path is at `probe/probe.cpp:1973-2165`.
- Current worker, hook, F11 watcher, and shutdown path are at
  `probe/probe.cpp:2978-3423`.
- Research 006 establishes offset territory and refuted hypotheses.
- Research 007 confirms player lock-on `+0x6B0` and refutes `+0x6A0`.
- Research 008 confirms enemy anim_id path A: `TimeAct +0xD0`, and anim-time
  slot `+0x24`.
- `tools/qualify_oracle.py` already implements DB family fallback and
  anim-time field selection. Reuse its semantics.

## Phase 4.0 - Gate 0.B Research: Target-Of-Attention Field

### Goal

Find a read-only field or chain that answers this question for a boss-bar
enemy: "Which entity is the boss currently trying to hit or face as its active
target of attention?" This is not the same as Josh's lock-on target, the
enemy's last-attacked entity, or the last entity that damaged the boss. The
field must support the L1 filter: fire the audio cue only when the active boss
is targeting the local player at cue time.

This is the single largest Bundle A risk. If it fails, Bundle A must either
ship MVP-audio-only or stop short of `v0.1.0`; the plan below makes that
decision explicit rather than burying it in implementation.

### Implementation Notes

Add a focused research build before the predictor is considered shippable.
This can be a temporary `v6.5-target` style instrumentation pass or a guarded
mode in Phase 4 code. It must reuse the same read-only probe patterns:

- Start from the current enemy path:
  - boss-bar / roster candidate gives `ChrIns*`
  - `ChrIns +0x190 -> ChrModuleBag`
  - `ChrIns +0x580 -> ai bag`
  - `ai bag +0xC0 -> ai_struct`
- Candidate scan areas:
  - AI bag / AI struct: from `ChrIns +0x580 -> ai bag +0xC0 -> ai_struct`.
    Capture and scan bounded windows around the current `ai_struct`, starting
    with the first `0x1000` bytes and, if needed, the older Phase 1 fallback
    territory `ai_struct +0xE000..0xF000`. Use bounded region records, not
    unbounded pointer chasing.
  - Module bag `+0x38` AI slot: research 008 identifies module-bag slot
    `+0x38` as `ai`. Capture the pointed body separately from the
    `ChrIns +0x580` chain so the two models can be reconciled.
  - ActionRequest module: `ChrModuleBag +0x80`, already defined as
    `MODULE_BAG_ACTION_REQ_PTR` in `probe/probe.cpp:235-252`. It was refuted
    for anim_id at `+0x90`, but target fields can still live nearby.
  - Event / behavior-related slots from research 008's module-bag scan:
    `+0x30` behavior_sync, `+0x50` talk, `+0x58` event, `+0x70/+0x78` unknown.
    These are candidates only because target-like IDs may cross subsystem
    boundaries; they do not override the AI-first hypothesis.
  - Existing raw `ChrIns` fields already captured: `+0x038`, `+0x060`,
    `+0x064`, `+0x068`, `+0x06C`, `+0x080`, `+0x1E8`. Keep these in the
    candidate report, but treat `+0x064` as c-id family/join-key territory
    unless new evidence says otherwise.
- Candidate value shapes to look for:
  - `FieldInsHandle`-shaped u64 matching the local player's handle or a
    co-op partner / summon handle.
  - `ChrIns*`-shaped pointer equal to local player `ChrIns*` or another known
    friendly `ChrIns*`.
  - u32 entity IDs / c-ids that map to player-like or summon-like actors.
  - paired handle-area forms similar to player lock-on `+0x6B0/+0x6B4`.
- Instrument candidate transitions with enough context:
  - timestamp
  - boss handle and boss `ChrIns*`
  - candidate location `(region, offset, width, value)`
  - local player handle / `ChrIns*`
  - all friendly `ChrIns*` from WCM player array
  - lock-on handle `PlayerIns +0x6B0` for comparison only
  - current boss anim_id / anim_time
  - visible target label from Josh's session notes when available

No memory writes are allowed. Do not poke target fields, force aggro, patch AI,
or read/write game data files.

### Validation: Target-Of-Attention vs Lookalikes

A candidate is target-of-attention only if it satisfies all of these:

- It equals the local player when the boss is visibly attacking Josh.
- It changes away from the local player when the boss commits to a co-op
  partner, NPC summon, or Mimic.
- It can change while Josh's own lock-on stays constant. This rules out "Josh
  is locked onto boss" as the explanation.
- It can change without requiring Josh to damage the boss. This helps rule out
  "last attacker" or "last damaged by" fields.
- It changes before or at attack commitment, not only after a hit lands. This
  helps rule out "last hit target".
- It remains stable enough through an attack windup that a cue-time check is
  meaningful.

A candidate is suspicious and should be rejected or demoted if:

- It only changes when Josh toggles lock-on.
- It only changes after damage events.
- It equals the nearest entity rather than the attacked entity.
- It holds stale values after the boss clearly switches targets.
- It works solo but cannot distinguish Josh from an NPC summon or co-op
  partner.

### Capture Scenarios

Run short captures with explicit spoken or written markers in the log. The
research tool should support manual marker lines if possible, but approximate
timestamps from Josh are acceptable.

1. Solo control:
   - No summons, one boss-bar enemy.
   - Expected target is local player for nearly all active attacks.
   - Purpose: eliminate fields that are always zero/sentinel and establish
     player handle/pointer shape.

2. Solo with NPC summon or Mimic:
   - Boss visibly attacks summon for several attacks, then Josh for several.
   - Josh avoids damaging during at least one switch to test non-damage aggro.
   - Purpose: distinguish target-of-attention from last-attacked and nearest.

3. Co-op two-player:
   - Josh and partner alternate baiting attacks.
   - Josh changes lock-on state independently: locked on, unlocked, locked to
     same boss while boss attacks partner.
   - Purpose: distinguish boss target from player lock-on and validate the
     co-op noise case Bundle A exists to suppress.

4. Boss switch mid-combat:
   - Capture clear target switches during the same boss phase.
   - Prefer a slow boss or large arena where camera evidence is readable.
   - Purpose: verify transition timing relative to attack windup.

5. Damage decoy:
   - Partner or summon damages boss, but Josh baits the next attack, and vice
     versa.
   - Purpose: rule out last-damager / threat-owner fields.

### Test Plan

- Extend the analyzer used for research 006-008 or add
  `tools/analyze_target_field.py`.
- Input: `.bin` capture plus optional marker file.
- Output: ranked target-field candidates with:
  - match rate against observed target labels
  - transition lag relative to labels
  - false match rate against player lock-on transitions
  - false match rate against damage-only events if those are observable
  - stability during windup intervals
- Repeat on at least two enemy families before treating the offset as
  production-worthy. One Godrick Soldier-only capture is not enough for L1.

### Done Criteria

- A single candidate chain is documented with:
  - exact dereference path
  - value type and sentinel values
  - validation captures used
  - known failure modes
- Candidate distinguishes local-player target from summon/co-op target in at
  least one multi-target capture.
- Candidate does not merely mirror Josh's `PlayerIns +0x6B0` lock-on field.
- Candidate is read through SEH-wrapped helpers and skipped on failure.
- If the field is not found after the research budget, the fallback decision is
  recorded in `PHASE4-PLAN.md`, `CHANGELOG.md`, or a follow-up note before
  implementation proceeds.

### Failure To Find: Fallback Decision

If no candidate reaches confidence after the Phase 4.0 research budget:

- Default recommendation: scope-reduce Bundle A to MVP-audio-only and tag it
  `v0.1.0-alpha` or do not tag a stable `v0.1.0` until Josh explicitly accepts
  false positives in co-op.
- Do not silently ship "target_filter_enabled = true" with an unvalidated
  field. If target filtering is not real, the default must be
  `target_filter_enabled = false` for that build, and release notes must say
  MVP-audio-only.
- Do not spend Bundle A time on hue overlay or D3D12 as a substitute. Target
  research either lands or Bundle A scope reduces.

### Risks + Mitigations

- Risk: Target field is not in the first AI struct window.
  Mitigation: scan known module-bag AI territory and the older
  `ai_struct +0xE000..0xF000` range in bounded chunks.
- Risk: Candidate is actually last-attacker.
  Mitigation: require damage-decoy captures and pre-hit transition timing.
- Risk: Candidate works for one enemy family only.
  Mitigation: validate on at least two distinct boss/enemy families before
  defaulting `target_filter_enabled = true`.
- Risk: Field is a handle requiring area bits or handle normalization.
  Mitigation: compare both raw u64 and split high/low forms, using the
  `+0x6B0/+0x6B4` lock-on analysis as a model.
- Risk: Capture labels are subjective.
  Mitigation: use slow, readable fights and prefer repeated switches over a
  single anecdotal transition.

### Estimated Effort

2-4 work sessions. Bias high because this is unresolved reverse engineering,
not ordinary implementation.

## Phase 4.1 - Prediction Thread Architecture + Hash Table Init

### Goal

Convert the probe from "log everything to disk" into a real-time predictor
while preserving the current data-collection path behind `data_collection_mode`.
The predictor should poll game state, read boss anim_id and anim_time, look up
parry windows in memory, compute lead time, apply the L1 target filter, and
emit a cue decision.

### Implementation Notes

Use a worker-owned polling predictor, not a new game-tick hook. The current
probe already hooks `UpdateUIBarStructs` for data collection
(`probe/probe.cpp:3266-3291`), but Bundle A does not need to add more hook
surface. Recommended architecture:

- DllMain remains loader-lock-safe and only starts the worker thread.
- Worker initialization keeps existing steps:
  - resolve DLL paths
  - load INI
  - check ER version
  - sig-scan WCM / CSFeManImp / needed functions
  - validate roster
- After init, start a prediction loop on the worker thread or a dedicated
  prediction thread owned by the worker.
- Use a waitable shutdown signal. If current code still only uses
  `g_running`, add a manual-reset event for waitable sleeps and keep the
  atomic as a cheap fast-path flag. Do not wait in DllMain.
- Keep the existing hook/ring-buffer writer only when `data_collection_mode`
  is true. F11 should arm/disarm data collection, not the release audio
  predictor. The predictor should run automatically when enabled, because a
  usable mod should not require Josh to press F11 before every fight.

Polling cadence:

- Default `prediction_poll_interval_ms = 4`.
- Valid range `1..33`.
- Rationale: anim_time is measured in seconds within the animation, so the
  lead-time math is frame-rate independent. The only framerate-related issue
  is how often the predictor re-reads state. A 4 ms poll gives roughly 250 Hz
  state checks without spinning at 1 ms permanently. If CPU impact is
  measurable or reads become noisy, tune to 8 ms; if latency is visibly late,
  tune to 1-2 ms for test captures only.
- Use `WaitForSingleObject(shutdown_event, poll_ms)` or equivalent, not a
  tight spin.

State sampling should reuse proven dereference paths:

- Boss handles from CSFeManImp boss bars, using the current code path at
  `probe/probe.cpp:1973-1991`.
- Boss `ChrIns*` from roster matching, preserving v6.4 friendly exclusion at
  `probe/probe.cpp:1873-1901` and `2051-2120`.
- c-id / family key from `field_at_0x064` (`ChrIns +0x064`), already captured
  at `probe/probe.cpp:1462-1469` and qualified as `4311 -> c4310`.
- anim_id from `ChrIns +0x190 -> bag +0x18 -> TimeAct +0xD0`, current source
  at `probe/probe.cpp:1471-1484`, confirmed by research 008.
- anim_time from `TimeAct +0x24`, current source reads all four candidates at
  `probe/probe.cpp:1481-1484`. Phase 4.1's first commit MUST switch the
  production read to `+0x24` only. Keep all four reads available behind a
  diagnostics flag (e.g., `verbose=true`) for future re-qualification, but the
  hot-path predictor reads exactly one slot. Tonight's qualification PASS
  proved `+0x24` against `+0x2C` by two orders of magnitude on forward
  progressions (1292 vs 20); there is no ambiguity to preserve.
- target-of-attention from the Phase 4.0 validated path only.

Hash table initialization:

- Load `parry_data.json` from the same directory as the DLL at worker init.
  The release build expects `parry_data.json` sibling to `parry-tell-probe.dll`
  in the mod directory; the build's deploy step must copy `data/parry_data.json`
  into `probe/releases/<version>/parry_data.json` alongside the DLL. Do not
  hardcode the `data/` development path in production. If the file is missing,
  fail closed for cues with a single startup log line.
- Source JSON schema is:
  - top-level `_meta`
  - `characters`
  - `characters[cXXXX].animations[aXXX_YYYYYY].parry_windows[]`
  - each window has at least `start_time` and `end_time`
- Build an in-memory table keyed by `(resolved_cid, anim_id)`. Recommended
  shape: `std::unordered_map<uint64_t, std::vector<Window>>` where the key is
  packed as `(uint64_t(resolved_cid) << 32) | uint32_t(anim_id)`. This keeps
  lookup at O(1) on the prediction hot path and avoids a 30 MB JSON tree
  staying resident in memory after init. After table construction, free the
  parsed JSON document. Target steady-state RAM for the table: under 10 MB.
- Store each value as a compact vector of `{window_open_s, window_close_s}`.
- Include both animation ID encodings used by `qualify_oracle.py`:
  - full decimal form from `aXXX_YYYYYY` as `XXXYYYYYY`
  - short suffix form `YYYYYY`
  This avoids repeating the v6.3 ambiguity at runtime.
- Family fallback:
  - First lookup exact c-id, for example `c3251`.
  - If exact c-id has no parry-window row, round down to nearest 10, for
    example `c4311 -> c4310`.
  - Exact-match takes precedence over family fallback for the 19 exception
    c-ids with their own DB rows.
  - Do not fallback repeatedly. If `c9990` is absent, it stays absent.
  - Skip c-id values `< 1000`; this preserves the junk-cid fix from
    `qualify_oracle.py`.
- The startup loader should report:
  - number of c-ids loaded
  - number with windows
  - number of `(cid, anim_id)` keys
  - total windows
  - JSON metadata version
- If DB load fails, fail closed for cues: no audio, log the reason, keep the
  process alive.

Prediction logic:

```text
for each poll:
  read active boss-bar bosses
  for each resolved boss:
    read c_id_raw from ChrIns +0x064
    resolve exact-or-family DB key
    read anim_id from TimeAct +0xD0
    read anim_time_s from TimeAct +0x24
    lookup windows by (resolved_cid, anim_id)
    update per-boss/per-window latch state
    if target_filter_enabled:
      read target-of-attention
      require target == local player
    if audio_cue_enabled and lead_time_ms <= audio_cue_lead_ms and not latched:
      fire one cue, latch, log the decision
```

Lead-time math:

- `lead_time_s = window_open_s - current_anim_time_s`
- `lead_time_ms = lead_time_s * 1000.0`
- Cue is configured by a target lead-time `audio_cue_lead_ms`, NOT a one-sided
  reaction budget. Default: `0 ms` (fire exactly at window-open). Range:
  `-200..500` ms. Positive values fire BEFORE the window opens; zero fires
  AT the window-open frame; negative values fire AFTER the window opens (for
  users who want a confirmation tone rather than a prediction cue).
- Fire when `lead_time_ms <= audio_cue_lead_ms` AND the cue has not yet been
  latched for this `(boss_handle, window_id)`. Once fired, latch the
  `(boss_handle, anim_id, window_open_s)` triple. The latch is cleared ONLY
  by the Phase 4.1 reset rules below (anim_id change, anim_time rewind
  greater than `50 ms` tolerance, handle disappearance, c-id change). Small
  sub-tolerance rewinds DO NOT clear the latch — this prevents duplicate cues
  when a positive-lead cue has already fired and anim_time then jitters
  backward by a few milliseconds. This produces ONE cue per window, at the
  precise offset Josh chooses, not "anywhere within a 250ms band".
- Negative-lead handling: if `audio_cue_lead_ms < 0`, the cue can only fire
  once `current_anim_time_s >= window_open_s + (-audio_cue_lead_ms / 1000)`.
  Still subject to the "before window_close" guard. If the target fire-time
  `window_open_s + (-audio_cue_lead_ms / 1000) > window_close_s`, the cue is
  suppressed entirely for that window and logged with
  `suppressed_reason="negative_lead_exceeds_window"`. Example: `audio_cue_lead_ms = -150`
  on a 100ms window means the cue would fire 50ms after window-close;
  suppress.
- Polling jitter: the predictor polls every `prediction_poll_interval_ms`
  (default 4 ms). The predicate `lead_time_ms <= audio_cue_lead_ms` fires on
  the FIRST poll after the threshold is crossed, so jitter is one-sided:
  actual cue lands in the range `[audio_cue_lead_ms - poll_ms, audio_cue_lead_ms]`.
  Concretely, with `audio_cue_lead_ms = 0` and `poll_ms = 4`, the actual lead
  at fire time is in `[-4ms, 0ms]` — the cue is always at-or-just-after the
  target offset, never before it. This matches Josh's expectation: setting
  `audio_cue_lead_ms = 10` means "fire at most 10ms before the window, never
  earlier than that." Tuning the poll interval down to 1ms tightens the
  range; tuning up to 8ms loosens it.
- Windows audio latency (`PlaySoundW` async kickoff + OS mixer, typically
  10-30 ms) is NOT subtracted from the cue calculation. Josh tunes
  `audio_cue_lead_ms` against the observed end-to-end timing, not a
  theoretical engine time. If the cue feels 20 ms late at `audio_cue_lead_ms=0`,
  Josh raises the knob to `20`. The plan deliberately exposes the single
  tunable knob rather than splitting it into "engine lead" + "audio latency
  estimate" which would multiply the configuration surface for no user gain.
- The window-close guard remains: if `current_anim_time_s > window_close_s`,
  no cue, regardless of lead config. A missed window is a missed window.
- If the current sample is already inside the window
  (`window_open_s <= anim_time <= window_close_s`), allow a cue only if that
  window has not been latched and the late arrival is within a small tolerance
  such as one poll interval plus 16 ms. Log it as `late_inside_window=true`.
- Do not cue for windows already closed.

Latch and reset semantics:

- Key state by stable boss handle when available, not boss-bar slot index.
- Track at least:
  - previous anim_id
  - previous anim_time
  - resolved c-id key
  - last cued window identity
  - per-window consumed flag for current animation instance
- Reset latches when:
  - anim_id changes
  - anim_time rewinds by more than a tolerance, for example `50 ms`
  - resolved boss handle disappears from boss bars for more than a short grace
  - resolved c-id changes
- If anim_id flickers for 1-2 polls and returns to the prior anim while
  anim_time remains monotonic, prefer not to double-cue. Implement a small
  debounce:
  - require a new anim_id to be observed for two consecutive polls before
    clearing all latches, or
  - keep a short recently-cued `(handle, prior_anim_id, window_open_s)` cache
    for `500 ms`.
- If the engine cancels an animation before the window opens, no cue should
  fire. The reset on anim change handles this.
- Target-switch-to-Josh handling has three cases:
  1. **Switch happens BEFORE threshold crossed** (`lead_time_ms > audio_cue_lead_ms`):
     cue normally when lead crosses the threshold. No tag.
  2. **Switch happens AFTER threshold crossed but BEFORE window opens**
     (`0 < lead_time_ms <= audio_cue_lead_ms`): fire immediately. The cue
     would have fired earlier had the target been Josh. Tag
     `late_target_switch=true` in the decision log. This case only applies
     when `audio_cue_lead_ms > 0`.
  3. **Switch happens AFTER window opens but BEFORE it closes**
     (`window_open_s <= current_anim_time_s <= window_close_s`): fire
     immediately. The window is still parry-able. Tag
     `late_target_switch=true` AND `inside_window=true`.
  4. **Switch happens AFTER window closes**: do not fire. Window already
     missed. Tag suppression with `suppressed_reason="target_switch_post_window"`.
  This matches the Phase 1 state machine's mid-attack target switch behavior
  for the audio portion.
- If the target changes from Josh to not-Josh before cue time, suppress the
  cue and log `suppressed_target=false`.

### Test Plan

- Unit-test the DB loader / table builder:
  - parses sample `aXXX_YYYYYY` ids into full and short forms
  - exact c-id match wins over family fallback
  - c4311 falls back to c4310
  - c9990 absent does not loop
  - c0000 / `<1000` junk is ignored for cue purposes
- Unit-test lead-time and latch logic with synthetic sequences:
  - cue once before a window
  - no double-cue across repeated polls in the same window
  - reset on anim change
  - reset on anim_time rewind
  - suppress when target is not local player
  - cue if target switches to local player before the window opens
- Integration-test in `data_collection_mode=true` with prediction decision
  logging enabled, then verify with Phase 4.4 harness.
- Live-test solo first, then summon/co-op only after Phase 4.0 target field is
  validated.

### Done Criteria

- Predictor runs without the data-collection hook when `data_collection_mode`
  is false.
- Data collection still works when `data_collection_mode` is true.
- Hash table builds successfully from the shipped DB and logs expected counts.
- Real-time predictor produces decision logs that can be replayed by
  `tools/verify_predictions.py`.
- No memory read lacks SEH protection.
- No write to game memory, regulation files, or base game files.

### Risks + Mitigations

- Risk: Runtime JSON parsing is too slow or fragile in the DLL.
  Mitigation: keep parsing at startup only; if necessary, generate a compact
  release DB artifact from `parry_data.json` while preserving the same
  semantics and source-of-truth provenance.
- Risk: Polling competes with game performance.
  Mitigation: default to 4 ms waitable sleep, log loop timing, tune via INI.
- Risk: Roster resolution misses a boss-bar handle.
  Mitigation: preserve current boss-bar and roster diagnostics, and suppress
  cues rather than guessing.
- Risk: anim_id is sometimes full vs short encoding.
  Mitigation: table both encodings from DB.
- Risk: c-id variant mapping causes false negatives.
  Mitigation: reuse exact-first/family-fallback behavior already qualified in
  `qualify_oracle.py`.

### Estimated Effort

2-3 work sessions after Phase 4.0 lands.

## Phase 4.2 - Audio Cue Path

### Goal

Provide a short, reliable, low-latency Windows audio cue when the predictor
decides a parry window is approaching. Audio failure must never crash or stall
the game.

### Implementation Notes

Use `PlaySoundW` as the first implementation:

- Canonical call for embedded memory:
  `PlaySoundW(buf, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)`.
- Windows-only. This is acceptable because the mod is Windows-only.
- Link `winmm.lib` in `probe/probe.vcxproj`; current dependencies are only
  `version.lib`.
- Keep the sound buffer alive for the process lifetime. Do not allocate a
  stack buffer and pass it to async playback.
- `PlaySoundW` does not expose a volume parameter. Volume is controlled by the
  WAV file content and Windows mixer. Document this as a known limitation.
- `PlaySoundW` is simple, but not a mixer. Without `SND_NOSTOP`, a new cue can
  interrupt a current cue; with `SND_NOSTOP`, a cue can be skipped if another
  sound is playing. For v0.1.0, rely on latch/cooldown so overlapping cues are
  rare. Prefer "skip cue" over "block or crash".

Embedded WAV:

- Ship one short cue as a resource:
  - duration: 50-100 ms
  - format: PCM WAV
  - sample rate: 44.1 kHz or 48 kHz
  - channels: mono
  - bit depth: 16-bit
  - size target: under 32 KB, hard cap 128 KB
- Add:
  - `probe/resource.h`
  - `probe/parry-tell-probe.rc`
  - `probe/assets/audio_cue.wav`
  - `.vcxproj` resource compile entry
- Load via `FindResourceW`, `LoadResource`, `LockResource`, `SizeofResource`.
- Validate RIFF/WAVE header before using it.
- If resource load fails, log once and disable cue playback for that process.

WAV path override:

- If `audio_cue_wav_path` is non-empty, load that file at startup into a heap
  buffer and use it instead of the embedded resource.
- Validate existence, reasonable size, and WAV header.
- If override load fails, log a warning and fall back to the embedded resource.
- Do not watch the file for changes. DLL/INI changes require restart unless a
  future plan explicitly adds reload.

Playback wrapper:

- Add a small `AudioCue` module or local struct:
  - `InitAudioCue(Config)`
  - `FireAudioCue(CueReason)`
  - `ShutdownAudioCue()` if needed
- `FireAudioCue` should be non-throwing and return a bool for decision logs.
- Rate-limit at the prediction state-machine level, not inside PlaySound. A
  minimum cue spacing of `50 ms` is enough as a hard safety guard; the real
  one-shot behavior comes from per-window latches.
- On any `PlaySoundW` failure, log at most once per session unless verbose
  diagnostics are enabled.

### Test Plan

- Build test: `.rc` compiles and `winmm.lib` links on station.
- Startup test: embedded resource loads and reports byte size.
- Override test: valid custom WAV path plays; invalid path falls back to
  embedded cue; oversized file is rejected.
- Manual trigger test: add a temporary diagnostics-only trigger or reuse an
  internal self-test call before enabling predictor-fire in live combat.
- Live test: cue fires once per predicted window; no rapid stutter inside a
  single window.
- Shutdown test: exiting the game during/after cue playback does not crash.

### Done Criteria

- Embedded cue plays in-game.
- Override path works.
- Missing/invalid WAV cannot crash the DLL.
- Volume limitation is documented in release notes / INI comments.
- Cue wrapper reports playback success/failure into prediction decision logs.

### Risks + Mitigations

- Risk: `PlaySoundW` has higher latency than expected.
  Mitigation: keep the WAV short and preloaded; tune `audio_cue_lead_ms` to
  compensate for observed end-to-end latency.
- Risk: Memory-buffer playback behaves differently across Windows versions.
  Mitigation: test on Josh's station; fallback to resource or filename mode if
  needed, while keeping the same wrapper API.
- Risk: Cues overlap in multi-boss scenarios.
  Mitigation: Bundle A does not solve multi-boss prioritization; latch and
  hard minimum spacing prevent audio spam.

### Estimated Effort

1 work session.

## Phase 4.3 - INI Surface

### Goal

Expose only the knobs Bundle A needs for prediction, audio, target filtering,
and regression capture. Defaults should favor the intended `v0.1.0` behavior:
audio on, target filter on only after Phase 4.0 validates the field, prediction
polling at 4 ms, data collection off.

### Implementation Notes

Extend the current hand-rolled INI parser (`probe/probe.cpp:390-570`) and
update the shipped `parry-tell-probe.ini` examples. Preserve fail-closed
validation for invalid values. Unknown keys can continue to be ignored, but
Bundle A should log warnings for unknown keys when `verbose=true` because
mistyped audio/prediction keys would be hard to notice.

Recommended shape:

```ini
[output]
log_dir = C:\Projects\elden-ring\logs\
session_name = parry-tell

[audio]
audio_cue_enabled = true
audio_cue_lead_ms = 0
audio_cue_wav_path =

[prediction]
target_filter_enabled = true
prediction_poll_interval_ms = 4
data_collection_mode = false
prediction_decision_log_enabled = false

[capture]
mode = qualification
sample_rate_hz = 10
max_enemies_tracked = 16
top_tier_enemies = 8
lesser_tier_rate_hz = 2
budget_ms_per_sample = 3.0

[hotkeys]
arm_toggle = F11

[diagnostics]
verbose = true
```

Knobs:

| Key | Default | Valid range | Controls | When to change |
|---|---:|---|---|---|
| `audio_cue_enabled` | `true` | bool | Master switch for parry-window cue playback. Prediction and logging can still run when false. | Turn off for silent regression captures or if audio is distracting. |
| `audio_cue_lead_ms` | `0` | `-200..500` | Target offset from `window_open_s` at which to fire the cue. Positive = before, zero = exactly at, negative = after. Cue is latched once per window. Negative values larger than a given window's duration suppress that window entirely (target fire-time would be past window-close). | Tune to taste: try `0` (fire at window-open) first; raise to `10`/`50`/`100` if the cue feels late against observed parries; go negative for confirmation-tone mode (be aware short windows may suppress). |
| `audio_cue_wav_path` | empty | existing file, <= 1 MB | Optional custom WAV loaded at startup. Empty uses embedded resource. | Use when Josh wants a different tone or volume. |
| `target_filter_enabled` | `true` after Gate 0.B, otherwise `false` | bool | Requires boss target-of-attention to be local player before cueing. | Turn off to compare MVP-audio-only behavior or if target field is suspect. |
| `prediction_poll_interval_ms` | `4` | `1..33` | Sleep interval between predictor reads. | Lower for latency experiments; raise if CPU/log pressure appears. |
| `data_collection_mode` | `false` | bool | Enables existing binary/CSV capture path for regression. | Turn on for qualification captures and `verify_predictions.py`. |
| `prediction_decision_log_enabled` | `false` | bool | Writes cue decisions / suppressions to a side-channel log. Auto-enable when `data_collection_mode=true`. | Turn on for debugging false positives/negatives without full binary captures. |

Notes:

- `data_collection_mode=true` should not disable prediction. Phase 4.4 depends
  on running collection and prediction simultaneously.
- F11 remains the data-collection arm/disarm control. The predictor should not
  require F11 when `data_collection_mode=false`.
- If Gate 0.B does not land, `target_filter_enabled` must default to `false`
  and release notes must state MVP-audio-only.
- Existing `[capture] mode` can remain for compatibility. In release mode,
  `data_collection_mode=false` means the hook/ring-buffer path is not
  installed or not armed, even if `[capture] mode` is present.

### Test Plan

- Parser unit / smoke tests for each knob:
  - valid default
  - valid min/max
  - invalid type
  - out-of-range value
  - path too long
- Manual INI test on station:
  - missing optional audio keys uses defaults
  - missing required `log_dir` still fails closed if logs are required
  - invalid `prediction_poll_interval_ms` logs config failure
- Verify manifest / logs include all new resolved config values.

### Done Criteria

- Shipped INI template documents each Bundle A knob.
- Invalid values fail closed or fall back exactly as documented.
- `data_collection_mode=false` avoids large binary logs during normal use.
- `data_collection_mode=true` produces old capture files plus prediction
  decision logs.

### Risks + Mitigations

- Risk: Too many knobs confuse the first release.
  Mitigation: keep defaults correct; Josh should only need to edit WAV path or
  `audio_cue_lead_ms`.
- Risk: Existing parser silently ignores typoed keys.
  Mitigation: add verbose unknown-key warnings for `[audio]` and
  `[prediction]`.

### Estimated Effort

0.5-1 work session.

## Phase 4.4 - Regression Harness

### Goal

Validate the real-time predictor artifact, not just the offline oracle. The
existing `tools/qualify_oracle.py` proves that captured anim_id / anim_time
line up with DB windows. Bundle A also needs to prove the predictor fires cue
decisions at the expected lead time and does not fire on non-parry animations.

### Implementation Notes

Add a dual-mode capture path:

- `data_collection_mode=true`
- prediction remains active
- binary/CSV capture records the raw stream as v6.4 does today
- prediction writes a side-channel decision log

Decision log format:

- Prefer JSONL for easy append and robust parsing.
- File location: same directory as existing `.bin` / `.csv` capture outputs
  (controlled by `[output] log_dir` in INI). Default station path:
  `C:\Projects\elden-ring\logs\<session>-<timestamp>.predictions.jsonl`. Pulled
  back to the VM via the existing SMB mount at
  `/mnt/station-projects/elden-ring/logs/`. No new transport mechanism.
- Suggested filename:
  `<session>-<timestamp>.predictions.jsonl`
- One event per meaningful decision:
  - `poll`
  - `candidate_window`
  - `cue_fired`
  - `cue_suppressed_target`
  - `cue_suppressed_latched`
  - `cue_suppressed_no_audio`
  - `anim_reset`
- Required fields:
  - monotonic timestamp in ms
  - session-relative timestamp in ms, aligned with `.bin`
  - boss handle
  - boss `ChrIns*` if available
  - raw c-id and resolved c-id
  - `matched_via_family_fallback`
  - anim_id
  - anim_time_s
  - window_open_s / window_close_s
  - lead_time_ms (actual lead at cue-fire time)
  - configured_lead_ms (the INI `audio_cue_lead_ms` value in effect)
  - target_filter_enabled
  - target_is_local_player
  - target raw value
  - latch state / window key
  - audio result bool

Add `tools/verify_predictions.py`:

- Inputs:
  - `.bin` capture path, including rotated shards
  - `.predictions.jsonl`
  - `data/parry_data.json`
  - optional `--lead-ms` (override what the predictor used, for replay analysis)
  - optional `--tolerance-ms`, default `50` (how close actual lead must be to
    configured lead to count as on-target)
- It should reuse or share code with:
  - `tools/probe_bin.py`
  - `tools/qualify_oracle.py` DB loader and family fallback
- Validation steps:
  1. Parse raw capture and identify focused/boss-bar enemy samples.
  2. Resolve join key with exact-first/family fallback.
  3. Use the qualified anim_time slot `+0x24`, or assert the capture agrees.
  4. Build expected cue opportunities for every observed parry window:
     - same `(resolved_cid, anim_id)` exists in DB
     - anim_time crosses `window_open_s - (configured_lead_ms / 1000)`
     - window is not already consumed in same animation instance
     - the target fire-time is BEFORE `window_close_s` (i.e., for negative
       `configured_lead_ms`, the fire-time `window_open_s - configured_lead_ms/1000`
       must satisfy `< window_close_s`; otherwise the window has no expected
       cue opportunity and the verifier expects suppression with
       `negative_lead_exceeds_window`)
  5. Match `cue_fired` decisions to expected opportunities within
     `+/-tolerance_ms` of `configured_lead_ms`. "On-target" means the actual
     `lead_time_ms` at fire time was within tolerance of the configured value
     (e.g., configured=50, actual=43, tolerance=50 → on-target). The
     `--tolerance-ms` value MUST be at least `prediction_poll_interval_ms +
     16ms` to account for one-sided polling jitter plus logging/timestamp
     uncertainty; the verifier should warn if a smaller tolerance is supplied.
  6. Count false positives:
     - cue fired for anim_id with no parry windows in DB
     - cue fired after the window closed
     - cue fired multiple times for the same window
     - cue fired when target filter says target was not local player
       (only if target filter enabled and validated)
  7. Emit a JSON report plus human-readable summary.

Pass criteria for a capture:

- `>=90%` of recorded parry windows that became cue opportunities produced a
  cue within `+/-50 ms` of the expected lead-time.
- `<=5%` false-positive rate, where false positives are cues on non-parry DB
  anims or invalid window timing.
- Zero crashes, zero unhandled parser errors.
- If target filtering is enabled, no confirmed co-op/summon non-Josh target
  event should produce a cue. Any such event blocks `v0.1.0`.

Ship gate for `v0.1.0`:

- PASS on at least 3 distinct boss/enemy captures.
- At least one capture must include a multi-target scenario if
  `target_filter_enabled=true` by default.
- If only solo captures pass, the release can only be MVP-audio-only or must
  default target filtering off.

Suggested first validation set:

- Godrick Soldier family (`c4311 -> c4310`) because it already produced the
  first qualification PASS.
- A knight or Banished Knight family with more windows, for broader coverage.
- One summon/co-op target-switch capture for L1.

### Test Plan

- Unit tests for `verify_predictions.py` with synthetic captures:
  - one expected cue, exact timing
  - late cue within tolerance
  - missing cue
  - duplicate cue
  - false positive on non-parry anim
  - family fallback exact-takes-precedence
- Regression on `qualification-20260511-195759` once a synthetic prediction log
  can be generated from offline replay. This does not replace live predictor
  validation, but it catches tool bugs before asking Josh for more captures.
- Live dual-mode captures on station.

### Done Criteria

- `verify_predictions.py` produces a PASS/FAIL verdict and JSON report.
- Dual-mode capture produces `.bin`, `.csv`, `.log.txt`, and
  `.predictions.jsonl` with aligned timestamps.
- At least 3 distinct captures meet the pass criteria before `v0.1.0` tag.

### Risks + Mitigations

- Risk: Raw capture and prediction log clocks do not align.
  Mitigation: write session-start epoch and session-relative ms into both
  streams, mirroring `segment_by_f11.py`'s epoch translation approach.
- Risk: The verifier accidentally validates its own assumptions instead of
  predictor behavior.
  Mitigation: decision log must include actual predictor latch/target/audio
  decisions, not just recomputed expected windows.
- Risk: 90% pass gate is too strict for noisy target-switch captures.
  Mitigation: keep the gate for cue timing; separately report target-filter
  ambiguous samples. Do not relax false positives for confirmed non-Josh
  target events.

### Estimated Effort

1-2 work sessions.

## Phase 4.5 - `v0.1.0` Release Prep

### Goal

Ship Bundle A as an auditable, reproducible `v0.1.0` release only after the
predictor and L1 target filter meet the Phase 4 gates. Keep the release honest
about what is included and what is deferred.

### Implementation Notes

Release work:

- Bump probe/mod version string from v6.4 lineage to `v0.1.0` release naming.
  Keep internal probe lineage in notes if useful, but the user-facing tag is
  `v0.1.0`.
- Update `CHANGELOG.md` with:
  - Bundle A feature summary
  - target-field validation summary
  - prediction harness PASS summaries
  - known limitations
  - explicit "Hue overlay deferred to Bundle B"
- Add release artifact under `probe/releases/`:
  - DLL
  - default INI
  - short README or deploy note
  - checksum file if existing release convention supports it
- Preserve conventional commits:
  - `feat(probe): add real-time parry prediction`
  - `feat(probe): add audio cue playback`
  - `feat(tools): verify real-time prediction decisions`
  - `docs(phase4): add Bundle A plan`
  Actual commit grouping is Claude's call after review.
- Tag convention:
  - release tag: `v0.1.0`
  - optional pre-release checkpoints: `checkpoint/YYYY-MM-DD-HHMMSS-...`
  - session-close tags stay as established.

Deploy notes:

- Drop DLL and INI into Seamless/ME2 external DLL path as before.
- Ensure `parry_data.json` or compact derived DB artifact is present where the
  DLL expects it.
- Game restart required for DLL, INI, DB, or WAV changes.
- Normal use should have `data_collection_mode=false`.
- Regression captures should set `data_collection_mode=true` and may use F11
  to bracket sessions.

Release notes must say:

- `v0.1.0` is audio cue + target filter only.
- No screen overlay yet.
- No game memory writes.
- No regulation.bin access.
- The cue is based on extracted TAE parry windows and live animation time.
- Volume is controlled by the WAV file / Windows mixer, not an INI numeric
  volume knob.
- Known target-field limitations, if any.

### Test Plan

- Clean build on station.
- Smoke launch with `data_collection_mode=false`:
  - no large `.bin` file created
  - boot log says predictor ready
  - no hook required unless implementation keeps the hook for shared sampling
- Audio self-test or first live cue confirms playback.
- Regression launch with `data_collection_mode=true`:
  - existing binary capture path still works
  - prediction side-channel log exists
  - `verify_predictions.py` can read the outputs
- Live validation:
  - at least 3 PASS captures per Phase 4.4
  - at least one multi-target PASS if L1 ships enabled
- No D3D12 hook or hue code appears in the diff.

### Done Criteria

- All Phase 4.0-4.4 done criteria are met, or fallback scope is explicitly
  documented and accepted.
- `CHANGELOG.md` has the `v0.1.0` entry.
- Build artifact exists in `probe/releases/`.
- Tag `v0.1.0` is created only after Claude review and final validation.
- Handoff updated with deployment state, known limitations, and next Bundle B
  planning item.

### Risks + Mitigations

- Risk: Release name implies full Phase 1 product.
  Mitigation: use `v0.1.0`, not `v1.0.0`, and state "audio + target filter".
- Risk: Target field lands late and delays usable audio.
  Mitigation: MVP-audio-only fallback is explicit, but not silently labeled as
  full Bundle A.
- Risk: Regression harness catches timing misses after feature code seems done.
  Mitigation: keep quality-over-speed; no tag until the 3-capture gate passes.

### Estimated Effort

0.5-1 work session after implementation and validation.

## Refuted Hypotheses To Preserve

Do not re-investigate these without new evidence:

- `TimeActModule +0x20 + read_idx*16` as enemy anim queue. It was sentinel in
  v6.2 and v6.3.
- `ActionRequestModule +0x90` as enemy anim_id. It was sentinel / non-winner
  in both captures.
- `PlayerIns +0x6A0` as player lock-on target. It was dead-always;
  `+0x6B0` is live.
- Original 7-field `find_join_key` without value filtering. It caused false
  `c0000` matches.
- `max_segment_dur` as primary anim_time tiebreak. It let `+0x2C` win on real
  data.
- Default 4 MB ETW buffer for launch-monitor. It fills during launch burst.

## Cross-Phase Dependency Summary

Phase 4 order should be:

1. Phase 4.0 target-field research.
2. Phase 4.1 predictor and DB table.
3. Phase 4.2 audio wrapper.
4. Phase 4.3 INI surface.
5. Phase 4.4 regression harness.
6. Phase 4.5 release.

Phases 4.1-4.3 can be implemented while 4.0 is still under review if the
target filter is abstracted behind `IsBossTargetingLocalPlayer(...)` and
defaults fail closed. Do not mark Bundle A ship-ready until 4.0 is resolved or
the MVP-audio-only fallback is explicitly accepted.

## Codex Notes

- I would push back on calling `v0.1.0` "Bundle A" if Gate 0.B does not land.
  A build that cues on any boss parryable anim is useful, but it is
  MVP-audio-only and should be labeled that way.
- Runtime parsing of the full 30 MB `parry_data.json` inside the DLL is
  acceptable only if it happens once at startup and is measured. If it becomes
  brittle, generate a compact release DB from the JSON while keeping
  `parry_data.json` as the source of truth and preserving exact/family
  fallback semantics.
- Verify `PlaySoundW` memory-buffer async behavior on Josh's Windows station
  early. Keep the audio wrapper narrow so the backend can fall back to resource
  or filename playback without touching prediction logic.
- The current source uses `g_running` and handle close in DllMain; the prompt
  references a manual-reset event. I would add the event for the prediction
  polling loop's waitable sleep, while preserving loader-lock-safe detach.
- The target-field validation should be treated as adversarial. The dangerous
  false positive is a field that looks perfect in solo play because every
  plausible "target-ish" value is Josh.

## Session Log

### 2026-05-15 — Phase 4.0 Gate 0.B resolved (target field at ai_struct +0xC988)

**Accomplishments**

- Gate 0.B is DONE. Boss target-of-attention field locked at
  `ChrIns +0x580 → +0xC0 → +0xC988` (FieldInsHandle / u64, sentinel
  `0xFFFFFFFFFFFFFFFF`). Validated on 5,521 real-boss samples: 63.6%
  player_handle match when boss was targeting Josh, 3 distinct handle
  values matching on-screen state, 9 clean transitions, zero false
  positives.
- Four probe iterations shipped (v7.0 → v7.1 → v7.2 → v7.3). Each
  iteration was preceded by a Codex deep-critic adversarial review that
  caught real bugs and named specific offsets to scan, saving an estimated
  2–3 probe roundtrips (~15–20 min each).
- Friendly-exclusion bug found and fixed: player was being selected as
  "nearest enemy" in ~21% of v7.2 samples due to `playerChrIns` being
  excluded from `friendlyPCs[]`. Fix confirmed — zero player-as-focused
  samples in v7.3 capture.
- `tools/analyze_target_field.py` written from scratch and hardened
  through two Codex critic passes. Coverage-weighted scoring,
  self-reference penalty, handle-equality testing.
- `tools/probe_bin.py` extended: region names 10–18, `player_handle`
  field, backward-compatible v7.1 wire format.
- 4 DLL archives committed to `probe/releases/`. v7.3 deployed to
  `/mnt/station-mods/parry-tell-probe.dll` (md5 d9083a17).
- Capture artifacts: `qualification-20260515-133028` (652 MB, 11,597
  samples) is the definitive Gate 0.B capture. Report at
  `probe/releases/v7.3-target-field-report.md`.

**Refuted this session (do not re-investigate)**

- `ChrIns +0x6A0` as enemy targetHandle — 100% zero on enemies; this
  range is player-specific lock-on storage.
- ChrIns* pointer-equality as target field shape — 0% across 5M+ slots.
- `ActionRequest +0x08` as target candidate — false positive from
  friendly-exclusion bug + self-reference (owner pointer).

**Current state**

- Phase 4.0 (Gate 0.B research): **DONE**. Offset locked.
- Phase 4.1 (prediction thread + hash table init): **NEXT**.
- Phase 4.2–4.5: as planned, no changes needed.
- Probe on station is v7.3-target-scan instrumentation mode. Production
  v0.1.0 probe will read `ai_struct +0xC988` directly; the v7.x research
  regions will not ship.

**Next steps**

1. Phase 4.1 kickoff: Codex drafts prediction-thread architecture (worker
   thread, hash table init, lead-time math, latch semantics); Claude
   reviews + lands. Per PHASE4-PLAN.md locked design.
2. Josh can stop the SSH service on station (`Stop-Service sshd` in admin
   PowerShell) — not needed for VM-side coding.
3. DLL on station will remain v7.3-target-scan until v0.1.0 production
   probe is ready (Phase 4.1–4.4 complete).

### 2026-05-15 (evening) — Phase 4.2 first audible run achieved

**Accomplishments**

- Phase 4.2 base wiring shipped: PlaySoundW + embedded resource +
  DllMain g_dllModule capture + InitAudioCue/FireAudioCue/ShutdownAudioCue
  in audio.cpp/.h + `[audio]` INI section + winmm.lib linkage. Commit
  `bd08154` plus `61f6981` (INI add + smoke lead bump).
- v8.2.0 built and deployed (SHA `ec8baec0...`). Predictor fired 23 cue
  decisions on cid 4311 over 105s of combat — proven via JSONL. Audio
  not heard during the first run because (a) Josh's Windows master volume
  was at 18% and (b) the sword-sample cue blended with the game's own
  sword ambient.
- Diagnostic-loud Pop Click (Pixabay/SoundReality CC0, +200% gain, 60ms)
  generated and embedded as v8.2.1. SHA `71fb0f3b...`. First audible
  run confirmed at 20:58 CDT.
- INI tuned post-run: `audio_cue_lead_ms` 50 → 200 (50ms minus Windows
  audio latency was perceived as too late to react), `target_filter_enabled`
  false → true (eliminated 3 spurious cues from non-targeting cid 4070).
  Third run: 4 fires, all target_match=True, lead times 178-199ms
  vs 200ms target. Zero spurious cues.
- Audio file work rolled back mid-session (Josh's call): ~32 minutes
  of two-cue-mode design + sound-picking commits discarded. All 28
  audio candidate files preserved in three independent backups.
  Design captured in `TODO-PHASE-4.2-FOLLOWUPS.md`.

**Documentation correction (binding)**

- "Parry" in Elden Ring is bound to L2 (weapon art), not L1 (block).
  Prior conversations said L1 — this was wrong. The 33-67ms parry
  windows in the DB are L2-active-frames windows. All future Claude
  sessions must use L2 terminology when discussing player response to
  the cue. Recorded in `runs/v8.2.1-tuned-observations.md`.

**Current state**

- Phase 4.0 Gate 0.B: DONE (target field at ai_struct +0xC988)
- Phase 4.1 predictor: DONE (predictor pipeline end-to-end proven)
- Phase 4.2 audio cue: **FUNCTIONALLY COMPLETE** (audible + tuned)
- Phase 4.3 INI surface: in progress (knobs added incrementally as
  needed during Phase 4.2 tuning; formal completion pending review)
- Phase 4.4 regression harness: NOT STARTED
- Phase 4.5 release packaging: NOT STARTED

**Next steps (priority order)**

1. **Gather more parry-success data.** Josh fights different enemy
   families with deliberate L2 (parry weapon art) attempts. Per-anim
   record of "cue heard → staggered y/n" tells us whether 4003103 has
   a real DB window or whether the DB might have false positives.
2. **Investigate the anim 4003103 question.** Once Josh has more data,
   determine which of three hypotheses applies (Josh-timing-off vs
   variant-mismatch vs DB-false-positive). Variant mismatch would
   require digging into the TAE-extraction tooling.
3. **Two-cue mode** (`audio_cue_parry_now`) per
   `TODO-PHASE-4.2-FOLLOWUPS.md`. Add a second fire at window_open to
   pair with the existing predictive fire. Deferred mid-session;
   ready for pickup whenever Josh wants it.
4. **Phase 4.4 regression harness.** Once Phase 4.2 tuning settles,
   build `tools/verify_predictions.py` to replay captured JSONL +
   binary data and validate predictor decisions offline.

**Open questions for Josh**

- Continue using the Pop Click diagnostic WAV in production, or swap
  to a quieter sound now that we've proven the pipeline works? The
  diagnostic-loud version was for testing; the un-amplified
  `popclick-pixabay-clean-60ms.wav` is preserved in the archive.
- Does anim 4003103 actually have a parryable window in-game, or is
  the DB row a false positive? Needs more L2 attempts to determine.
- Is the 200ms lead final, or should it move further forward (300?
  400?) once Josh has had more practice?

**Tried and ruled out (this session)**

- 200ms swordclash WAV as production cue: blended too well with sword
  ambient, inaudible during combat. Archived for possible alternative
  use. Decision: use a non-game-y synthetic click instead.
- Single-knob `audio_cue_parry_now` two-cue mode: implementation
  started, rolled back when scope grew larger than session window
  allowed. Design preserved in TODO doc for next-session pickup.
