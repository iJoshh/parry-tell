# HANDOFF — parry-tell-probe

**Date:** 2026-05-15 (America/Chicago)
**Session tag:** session-close/2026-05-15-140201
**Branch:** main (clean working tree, 3 unpushed commits pending nightly cron)

---

## Where we left off

Phase 4.0 Gate 0.B — finding the boss target-of-attention field — was the
entire focus of this session. The session before this one had locked
PHASE4-PLAN.md (Bundle A scope: MVP audio + L1 target filter together as
v0.1.0). We were supposed to start writing the brain (predictor + audio cue)
but hit a hard blocker on Gate 0.B and spent the session resolving it.

**The blocker is resolved.** Phase 4.0 is done.

---

## Accomplishments this session

### Gate 0.B SOLVED — target-of-attention field at ai_struct +0xC988

- **Path:** `ChrIns +0x580 (AIBag*) → +0xC0 (AIStruct*) → +0xC988`
- **Type:** `FieldInsHandle` (u64) — NOT a `ChrIns*` pointer
- **Sentinel:** `0xFFFFFFFFFFFFFFFF` (no current target)
- **Validation:** 5,521 real-boss samples in v7.3 capture; 63.6% match
  against `player_handle` when boss was targeting Josh; 3 distinct handle
  values matching exactly what was on screen (player, jellyfish summon,
  sentinel); 9 clean transitions; zero false positives.
- **Prior art alignment:** TarnishedTool `+0xC480` = TargetingSystem base,
  `+0xC988` = `+0x508` into that sub-struct, plausibly `currentTarget`.

### Probe iterations (v7.0 → v7.1 → v7.2 → v7.3)

| Version | Key change | Key finding |
|---------|-----------|-------------|
| v7.0 | Added ai_bag_head, ai_struct_head, module_bag_head regions | 0% ChrIns* match across 5.27M slots → field is handle-shaped |
| v7.1 | Added player FieldInsHandle to sample header (backward-compat) | Wire format extended; old captures parse with player_handle=0 |
| v7.2 | Added ai_struct_mid, action_req_head, player_chr_ins regions | False-positive action_req +0x08 (20.8%) — traced to friendly-exclusion bug |
| v7.3 | Added ai_struct_far/deep/tgt (TarnishedTool gap); fixed friendly-exclusion bug; re-enabled module body capture | ai_struct +0xC988 surfaces as unique winner; zero player-as-focused |

### Friendly-exclusion bug (found + fixed in v7.3)

`if (friendlyChr == playerChrIns) already = true` was excluding the player
from `friendlyPCs[]`, so the downstream exclusion check never matched Josh.
This caused the player to be selected as "nearest enemy" in ~21% of v7.2
samples. The `action_req +0x08` false positive was the action's owner pointer
pointing at the player when the focused entity WAS the player — self-reference,
not a target field. Fix confirmed: zero player-as-focused samples in v7.3.

### Codex deep-critic reviews (load-bearing)

Both adversarial passes saved real probe roundtrips:
- **v7.0 pass:** predicted handle-shape signal; guided v7.1 design.
- **v7.2 pass:** named TarnishedTool offsets (+0xC480, +0xDBF0, +0xDF10)
  in the unscanned gap AND caught the self-reference methodology bug.
  ~120s compute per review, saved ~2–3 probe iterations (~15–20 min each).

---

## Next steps (priority order)

1. **Phase 4.1 — prediction thread + hash table init (NEXT)**
   Josh said "you and Codex can start planning the brain." Pair-write
   protocol: Codex drafts prediction-thread architecture (worker thread,
   hash table init, lead-time math, latch semantics), Claude reviews +
   lands. Per PHASE4-PLAN.md locked design.

2. **Phase 4.2 — audio wrapper** (after 4.1)

3. **Phase 4.3 — INI surface** (after 4.2)

4. **Phase 4.4 — regression harness** (after 4.3)

5. **Phase 4.5 — release packaging** (v0.1.0)

---

## Open questions for Josh

- **None blocking.** Phase 4.0 is done; Phase 4.1 is ready to start.
- **SSH on station:** can be stopped now (`Stop-Service sshd` in admin
  PowerShell). Not needed for VM-side coding until the next build cycle.
- **DLL on station** is still v7.3-target-scan instrumentation. It will be
  replaced with v0.1.0 production probe when Phase 4.1–4.4 are done. No
  action needed.

---

## Tried and ruled out this session (do not re-investigate)

| Hypothesis | Verdict | Evidence |
|-----------|---------|---------|
| `ChrIns +0x6A0` as enemy targetHandle (Erd-Tools-CPP) | REFUTED | 100% zero on enemies in v7.2 region 0 data; this range is player-specific lock-on storage |
| ChrIns* pointer-equality as target field shape | REFUTED | 0% across 5M+ u64 slots in v7.0 + v7.2 combined |
| `ActionRequest +0x08` as target candidate | REFUTED | False positive from friendly-exclusion bug + self-reference (owner pointer, not target field) |

Also refuted in prior sessions (preserved from PHASE4-PLAN.md):
- `TimeAct +0x20 + read_idx*16` as enemy anim queue — sentinel in v6.2/v6.3
- `ActionRequest +0x90` as enemy anim_id — sentinel / non-winner
- `max_segment_dur` as primary anim_time tiebreak — let +0x2C win on real data

---

## Files modified this session

| File | Change |
|------|--------|
| `probe/probe.cpp` | Extended through v7.0–v7.3; friendly-exclusion fix; threaded player_chr_ins through WriteTier3ForEnemy/WriteEnemyRecord for region 15 |
| `tools/probe_bin.py` | Added region names for IDs 10–18; added player_handle field to Sample; reads v7.1 reserved slot |
| `tools/analyze_target_field.py` | **NEW.** Coverage-weighted target field scanner; self-reference penalty; handle-equality testing; hardened through two Codex critic passes |
| `probe/releases/parry-tell-probe-v7.0-target-scan.dll` + tarball | New archive |
| `probe/releases/parry-tell-probe-v7.1-target-scan.dll` + tarball | New archive |
| `probe/releases/parry-tell-probe-v7.2-target-scan.dll` + tarball | New archive |
| `probe/releases/parry-tell-probe-v7.3-target-scan.dll` + tarball | New archive |
| `probe/releases/v7.3-target-field-report.md` | Analyzer output pinning Gate 0.B discovery |

**Deployed:** `/mnt/station-mods/parry-tell-probe.dll` = v7.3 (md5 d9083a17)

**Capture artifacts on station SMB** (not committed; for reference):
- `qualification-20260515-133028` — v7.3 capture, 652 MB, 11,597 samples — **the definitive Gate 0.B capture**
- `qualification-20260515-124940` — v7.2 capture round 2, 262 MB, 7,566 samples (false-positive analysis)
- `qualification-20260512-173128` — v7.2 capture round 1, 262 MB, 9,593 samples

---

## Services / processes

- **SSH service on station:** was left running for build/deploy/capture
  cycles. Josh can stop it now: `Stop-Service sshd` in admin PowerShell.
- **SMB mounts on VM:** `/mnt/station-projects/` (RO) and
  `/mnt/station-mods/` (RW) — both should be live via Tailscale automount.
  Verify with `mount | grep station` at next session start.
- **Probe on station:** v7.3-target-scan instrumentation DLL. Active but
  not needed until next build cycle.

---

## Git state at session close

- **Branch:** main
- **Working tree:** clean
- **Unpushed commits:** 3 (nightly cron will push)
- **Session-close tag:** session-close/2026-05-15-140201
- **Previous session-close tag:** cc322ac (2026-05-11)

Recent commits:
```
d64def6  Phase 4.0 Gate 0.B SOLVED checkpoint
7fab672  v7.3 DLL built + deployed
6e78e7c  v7.3 probe + analyzer changes
d7efab4  v7.2 probe expanded coverage
885ca52  v7.1 player_handle capture
```

---

## Pickup prompt for next session

> Phase 4.0 Gate 0.B is solved — boss target-of-attention field is at
> `ChrIns +0x580 → +0xC0 → +0xC988` (FieldInsHandle u64, sentinel
> 0xFFFFFFFFFFFFFFFF), validated on 5,521 real-boss samples. The plan is
> locked in PHASE4-PLAN.md. Next step is Phase 4.1: Codex drafts the
> prediction-thread architecture (worker thread, hash table init, lead-time
> math, latch semantics), Claude reviews and lands. Start by reading
> PHASE4-PLAN.md Phase 4.1 section and HANDOFF.md, then kick off the
> Codex pair-write.
