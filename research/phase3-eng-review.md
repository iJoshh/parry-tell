# Phase 3 Engineering Review — Codex (gpt-5.3-codex)

**Date:** 2026-05-07 ~11:15 CT
**Reviewer:** Codex (engineering-manager mode)
**Inputs:** PHASE3-PLAN.md (v2 post-CEO), HANDOFF.md, research/SYNTHESIS.md, research/phase3-architecture-codex.md, research/phase3-offsets-codex.md, probe/probe.cpp

---

## Verdict
**No-go as written.** Ship strategy is good, but the plan has 6 blocking logic/gating gaps that will cause false confidence and likely missed timing behavior.

## Findings (highest severity first)

### 1. State machine is not correct for PHASE1 goal #4 edge cases.
The simplified `BossState[3]` logic is fine for a first MVP spike, but not for production behavior. It fails when the same animation ID replays back-to-back without an intervening ID change (second cue is suppressed because `consumed` never resets), and it is vulnerable to boss-bar slot reorder resets.

**Recommendation:** keep fixed-size per-boss state, but reset attack consumption on `(animId change) OR (animTime rewind)` and key persistent state by `bossHandle`, not slot index. Decouple audio edge detection from hue color logic exactly as originally recommended in `phase3-architecture-codex.md`.

**Early test:** scripted log assertion: same boss repeats same anim ID twice in <2s; expect two audio events.

### 2. Phase boundaries are not truly independent today.
If 3.1.B falls to Option A, L1 is functionally MVP with a new tag; that is not a real layer.

**Recommendation:** if Option A is chosen, skip `v0.2.0` and roll directly to the next real capability layer (L2 or lock-on). Do not ship a cosmetic version bump.

### 3. "Ship L1 and call it done" conflicts with the stated v1 product spec.
PHASE1 v1 explicitly includes hue+audio behavior. PHASE3 says "if snag at L2, ship L1 and call it done," which is a scope change that is currently implicit.

**Recommendation:** define two explicit end states: `v1.0.0` (full hue+audio spec) and `v0.x stable` (audio-only/target-aware degraded path). Do not label degraded path as `v1.0.0`.

### 4. TAE metadata gate has no remediation path when IDs mismatch.
Current gate says "match predicted IDs," but no decision tree exists for mismatch causes (wrong extraction set vs version drift vs parser bug).

**Recommendation:** add a mandatory 3-step remediation branch: validate `_meta.game_version` against runtime, validate extracted archive manifest/hash, then run sentinel attack-ID fixtures before any 3.1 pass.

### 5. Frame-count fallback is not timing-safe as written.
Using UI-hook tick counts as "frames" is not stable across FPS and frame pacing; this will skew parry timing materially.

**Recommendation:** prohibit raw frame-count fallback. Only allow time-based fallback (anim-time field or calibrated game-time delta integration).

**Early test:** 30/60/120 FPS capped runs on same attack; cue timestamp drift must stay <50ms.

### 6. Animation-time confidence is internally inconsistent.
Plan treats `+0x24` as near-ready, but offset research marks target animation time as unknown/low confidence (only practice-tool uses it, via a different hook chain).

**Recommendation:** downgrade 3.1.A confidence and add an explicit kill-criterion date for this read path.

### 7. Co-op friend dependency should not block L1 shipment.
Current gate couples L1 to friend availability.

**Recommendation:** make solo+Mimic target-switch test the blocking gate; friend co-op becomes post-ship validation with known-risk note.

### 8. Mimic/ally filter offsets are not actually verified in probe outputs.
`team-id` and `targetHandle` constants exist but are not being sampled in v5f, and only `chrType` canary is currently logged.

**Recommendation:** add a dedicated 3.1.B sub-step to probe/confirm `chrType`, `team-id`, and do-not-cue entity IDs before wiring the filter.

### 9. "Process-lifetime only, no unload" is acceptable, but must be explicit about restart-only operation.
Pinned-module safe-leak teardown is already the probe's actual behavior and is fine on process exit (ALT-F4/Task Manager).

**Recommendation:** document hard requirement: "No runtime DLL reload/unload supported; any DLL/config change requires full game restart."

---

## Direct Answers to 9 Questions

1. **Architecture correctness:** No, not yet. Simplified machine needs `animTime rewind` handling and per-handle persistence to fully satisfy PHASE1 #4.
2. **Phase boundaries:** Not independent under 3.1.B fallback; collapse L1 if fallback chosen.
3. **Risk timing / D3D12:** If D3D12 fails, you still have a shippable product, but **not PHASE1 v1**. Label it as degraded stable, not v1.0.0.
4. **TAE metadata gate:** currently incomplete; add explicit remediation branch or you will stall.
5. **Co-op dependency:** do not block L1 on friend availability. Ship with solo+Mimic gate, co-op follow-up.
6. **Mimic filter offsets:** add explicit 3.1.B probe sub-step; do not discover inline during 3.4 wiring.
7. **Process-lifetime decision:** safe for process exit; unsafe assumption is reloadability. Declare restart-only.
8. **Frame-count fallback:** reject as primary timing method; it is not accurate enough cross-FPS.
9. **Other implementation bite points:** biggest one is internal confidence mismatch on animation-time read; second is slot-index state reset behavior under bar reorder.

---

## Claude's response (decision log)

**Adopting verbatim:**
- Replace the BossState[3]-by-slot approach with a `map<uint64_t bossHandle, BossState>` keyed by handle — survives slot reorder, doesn't lose state on bar shuffle.
- Reset `consumed` on `(animId change) OR (animTime rewind)`. The animTime-rewind check is the fix for "same boss does same parryable attack twice in a row" — when animation loops, animTime resets to 0 and `prev > now` triggers `consumed=false`.
- Reject UI-hook-tick frame counting as a timing method. Only animTime field reads are acceptable. If +0x24 doesn't track time, escalate to lock-target-hook code-cave or accept v1 doesn't ship until alternative is found.
- Downgrade 3.1.A animation-time confidence from "first try" to "first-try-with-medium-confidence." Add explicit kill-criterion: if 2 days into 3.1.A we don't have a working time read, escalate to Josh.
- Add 3.1.B sub-step to probe `chrType + team-id + summoned-ally entity IDs` before wiring 3.4's PvE-only filter.
- Define `v1.0.0` as full hue+audio with target awareness AND lock-on. Define `v0.x-stable` as the degraded path (any release that ships less than v1's spec gets a 0.x tag, not 1.0).
- Skip cosmetic version bumps: if 3.1.B falls to Option A, roll directly to L2; don't tag v0.2.0 if it's functionally MVP.
- TAE metadata 3-step remediation branch: (1) version check, (2) archive hash check, (3) sentinel-fixture animation-ID test.
- Co-op friend dependency made non-blocking: solo+Mimic test is the L1 ship gate.
- README explicit: "No runtime DLL reload/unload supported; any DLL or INI change requires full game restart."

**Pushing back on:**
- Eng review #5 says "calibrated game-time delta integration" is acceptable as fallback. I disagree — that's effectively the same accuracy as +0x24 anyway (game-time deltas come from the same source). If +0x24 fails, the right move is the lock-target-hook code-cave path that practice-tool uses, not a homebrew time integration.

**Net effect on plan timeline:**
- Adds ~0.5 days for the 3.1.B Mimic-filter probe sub-step
- Adds ~0.5 days for state-machine handle-keyed redesign + tests
- Removes the cosmetic-bump scenarios (collapses some confusion but doesn't change clock time)
- Net: still 14-19 days, confidence numbers unchanged.

The plan is being patched (not rewritten) to incorporate these. See PHASE3-PLAN.md commit history.
