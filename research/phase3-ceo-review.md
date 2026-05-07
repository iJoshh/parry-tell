# Phase 3 CEO Review — Codex (gpt-5.3-codex)

**Date:** 2026-05-07 ~10:55 CT
**Reviewer:** Codex (CEO/founder mode prompt)
**Inputs:** PHASE3-PLAN.md, PHASE1-PLAN.md, HANDOFF.md

---

## 1) Is the scope right for v1?

Short answer: **current "full v1" is too big for first ship by ~25%** for a 2-month vibecoder project, even with strong scaffolding.

What's right:
- Hue + audio + boss-bar trigger + PvE-only + Seamless guest is a good product shape.
- The fallback philosophy (ship reduced precision instead of stalling) is correct.

What to cut or defer from v1:
- Cut "perfect target-awareness or bust." If target-of-boss is not stable fast, ship without it.
- Cut advanced per-attack tracking complexity (details in section 3).
- Cut hotkey from v1; keep startup INI only.

**Hidden 1-day MVP that can ship and prove concept:**
- One boss (Crucible Knight), one known parryable animation, **audio-only beep** at window-open, boss-bar gated, no target-awareness, no hue, no INI.
- If that works in one session, concept is proven and publishable as `v0.1.0-alpha`.

## 2) Are the phase boundaries right?

Current phases are close, but order is suboptimal for risk and motivation.

Recommended structure:
1. `3.0` TAE extraction (keep first).
2. `3.1` offset hunt (split into P0/P1):
   - P0 must-have: animation id/time + boss handle resolution.
   - P1 nice-to-have: lock-on + target-of-boss.
3. **Move audio earlier**: do audio cue before D3D12 overlay.
4. Then D3D12 hue rendering.
5. Merge state machine wiring into audio/hue integration (don't keep as its own big late phase).
6. Hardening + release.

Reason: D3D12 is the highest crash-risk and slowest-feeling work. Audio-first gives usable output early and keeps momentum.

## 3) Where's the over-engineering?

- **Per-attack-instance tracker map (`AttackKey`, `startFrame`, consumed flags)**: too heavy for v1. Use simple per-boss edge detection (`windowOpenNow && !windowOpenPrev`).
- **Hotkey in v1**: unnecessary.
- **Fully configurable cooldown/thickness/fade matrix in v1**: trim to 2-3 knobs max.
- **Detach cleanup perfection**: unnecessary for this use pattern; fail-safe on process exit is enough.

What is **not** over-engineering:
- 3 boss slots: keep it. Cheap, matches boss-bar model, avoids obvious regressions.
- Deferring INI hot-reload to v2: correct call.

## 4) Where's the under-engineering?

- **TAE format/version drift risk** is under-managed. Add a hard metadata gate:
  - record game version + extraction timestamp + parser version in `parry_data.json`.
  - refuse "trusted timing" if versions mismatch.
- **Mimic Tear filter via chrType alone is risky.** Add explicit ally/player-team checks, not only chrType.
- **Runtime compatibility gate is too soft.** You need fail-closed if executable/build signature mismatches expected version.
- **Unhook-on-detach story is ambiguous.** For this project, explicitly declare "no dynamic unload support; process-lifetime only."

## 5) Is "65% in 3 weeks" honest?

For full locked scope by **May 31, 2026**: **too optimistic**.
Codex's call: **50-55% by May 31**, **80% by June 15, 2026**.

Raises confidence:
- Audio-first milestone in week 1.
- Aggressive scope fallback by fixed dates.
- Freeze to one ER build and fail-closed on mismatch.

Lowers confidence:
- If lock-on or target-of-boss needs invasive hooks.
- D3D12 instability across alt-tab/resize.
- Co-op validation availability and patch timing.

Reframe timeline:
- **Week 1:** MVP usable (`audio-only`).
- **Week 2-3:** hue + target nuance + hardening.
- **Week 4:** full v1 polish/release.

## 6) What does a 10-star product look like?

If this were a real product, v1 would include:
- Dual cues: **window-open** and **window-close** audio.
- Per-attack cue profiles (color + sound variants).
- Built-in debug overlay (current animation, target handle, decision reason).
- Version-aware data pipeline (auto regenerate or disable stale data).
- In-game calibration for latency/audio offset.
- Robust compatibility layer across patches and common mod stacks.
- Better co-op semantics (host/friend/mimic differentiation with confidence display).

## 7) Should v1 be built differently? (audio-only vs hue-only)

Yes. Build **audio-first**.

Why:
- Audio delivers the core value (timing) directly.
- No D3D12 hook needed for initial utility.
- Faster validation loop and fewer crash surfaces.

Minimal useful product:
- Boss-bar trigger + animation timing + single beep on parry-window-open.
- Then add hue as phase 2 enhancement, not as co-equal day-1 requirement.

---

**Bottom line:** Keep the vision, but ship in layers.
Target **MVP in 1 week**, then climb to full v1 in 3-4 weeks with explicit off-ramps and hard scope cuts.

---

## Claude's response (decision log)

**Adopting verbatim:**
- Audio-first MVP. Reorder phases so audio ships before D3D12. Cut hotkey from v1.
- Drop per-attack-instance map; use per-boss edge detection.
- Fail-closed FileVersion check (already in v5f, just promote it as a gate, not a warning).
- "Process-lifetime only, no dynamic unload" — make it explicit in README + DllMain detach.
- TAE-data version metadata in `parry_data.json`. If extraction game version != runtime game version, refuse to use the timing data.
- Confidence reframed: MVP audio-only in 1 week (~80%), full v1 hue+audio in 4 weeks (~70%).

**Pushing back on:**
- "Mimic Tear chrType-alone is risky" — agreed in principle, but team-type IS the canonical filter PostureBarMod uses; without it, Mimic still triggers cues from your own ally's perspective. Plan now adds team-type AND chrType AND a do-not-cue list of known summoned-ally entity IDs as belt-and-suspenders.
- "Cut INI knobs" — keeping INI but trimming to 4-5 knobs (color, opacity, audio enable/disable, audio volume). Codex was right that 10 knobs is overkill; the cost of 5 vs 0 is ~50 lines of code that pays for itself the first time Josh wants to recolor.

**Not adopting:**
- 1-day MVP literally on Crucible Knight only. Better to do 1-week MVP across the canonical 3-4 parryable bosses, because the work is the same — once you have ONE boss's TAE timing extracted, parse-script-extending to all parryable enemies is a script change, not a redesign.

The plan is being rewritten as v2 to incorporate this. See PHASE3-PLAN.md commit history.
