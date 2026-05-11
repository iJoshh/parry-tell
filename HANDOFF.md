# HANDOFF — parry-tell (elden-ring)

**Last update:** 2026-05-11 evening — three captures done, two probe bugs found
**Branch:** main
**Probe deployed:** v6.1.1 in `Game\mods\`

## Pickup prompt for next session

> "Read HANDOFF.md and SESSION-2026-05-11-FINDINGS.md. Today's runs confirmed
> v6.1 + v6.1.1 architectural fixes work end-to-end but uncovered two probe
> bugs that block qualification PASS: (1) qualification_nearest picks NPCs
> over combat enemies, (2) enemy anim_id always reads 0 because the probe
> reads the TimeAct module not the active child TimeAct. Both are offset/
> design issues not quick patches. Pick one to fix in v7 and plan the
> investigation."

## Today's runs

Three captures, all on the same Gatefront soldier fight against Godrick Knights (c4382 = c4380 family).

| File | Capture | Result |
|------|---------|--------|
| qualification-20260511-121252 | v6.0 fallback (no roster) | FAIL — zero enemy rows; expected because no boss-bar enemy and roster was disabled |
| qualification-20260511-125252 | v6.1 first roster-enabled capture | FAIL — focused entity was the PLAYER (distance-to-self = 0); identified Godrick Knight as c4382 via lesser-tier slots |
| qualification-20260511-133002 | v6.1.1 player-excluded | FAIL — focus alternated between c1000 NPC and c4382 Knight; enemy anim_id always 0 |

OBS recording of run 1: `/mnt/station-projects/elden-ring/logs/2026-05-11 13-01-22.mkv` (800 MB).

## What works

- v6.1 60s WCM grace + F11 roster recheck — `F11: roster ENABLED on retry` confirmed in two captures
- v6.1.1 player exclusion in roster sweep — focused enemy is no longer the player
- field_at_0x060 // 10000 gives clean c-id for any roster enemy
- field_at_0x064 gives c-id base directly for combat enemies
- 20+ distinct enemies captured per session; multiple Godrick Knights resolvable
- SMB perf rule pinned in CLAUDE.md (copy big captures locally before parsing)
- Analyzer RO-filesystem fallback (writes reports to data/qualification-reports/)
- tools/inspect_capture.py for diagnostic dumps when qualification FAILs
- tools/station-ssh.sh password-auth helpers with endpoint+flag-injection guards

## What's broken — one underlying issue with three surface symptoms

**Root cause:** the probe's ChrIns field offset table is stale for ER 2.6.1.
Identity fields (f038, f060, f064 etc.) are correct — they give us clean
c-id lookups — but every "behavioral" field offset appears wrong:

### Symptom 1: position offset wrong

probe.cpp:173 `PLAYER_INS_POS_X = 0x6C0`. Verified against
qualification-20260511-133002.bin chr_ins_root payload:

| Source | x | y | z |
|---|---|---|---|
| `chr_ins + 0x6C0` (what probe reads) | 1.48 | 3.29 | 4.95 |
| `chr_ins + 0x80` (looks like real world coord) | 80.82 | -97.56 | -57.73 |
| player_pos (probe header) | 32.42 | 106.60 | 111.12 |

None of these agree. The values at +0x6C0 look like motion deltas or
quaternion components, not world position. Distance ranking against player
is meaningless until this is fixed.

Critical consequence: Josh fought ONLY ONE enemy (a Godrick Knight) for the
entire ~71-second session, and the probe still rotated focus across 3
different chr_ins. The "nearest" picker is comparing garbage values. The
c4382 Knight got focus for only 1450 rows (~16s) out of 7137 total focused
samples. The other 5687 rows focused on background entities Josh never
saw or interacted with.

### Symptom 2: enemy anim_id always reads 0

probe.cpp:1398 reads `time_act + 0xD0`. For the player this returns valid
anim_ids. For enemies, always 0. The time_act_module IS captured (region 2,
8KB payload), but its bytes at 0x20-0x2C are NaN/0.0/0.0/1.0 — uninitialized
or "outer" struct, not the active child.

8 time_act_child regions are captured per enemy. Likely the active anim
lives in one of them at a different offset than 0xD0.

### Symptom 3: lock-on detection broken

probe.cpp:1731 reads `playerLockHandle` from PlayerIns + 0x6A0. Returns a
pointer-shaped value (`0x7FF3073CBB60`), not a game handle integer. The
value is constant across the entire capture even though Josh's actual
lock-on target was an active enemy. Result: focus_reason=3 (nearest)
instead of 1 (lock_on), which would have bypassed the broken nearest
ranking entirely.

### What v6.1 + v6.1.1 actually fixed

- WCM roster init now works (60s grace + F11 retry)
- Player chr_ins is excluded from the nearest-enemy picker
- Lesser-tier decimation no longer inflates enemy_record_count

These are real fixes and unblock progress, but the captured data is still
not usable for qualification PASS until the offsets above are corrected.

## Files modified today

| File | Change |
|------|--------|
| probe/probe.cpp | v6.1 patch applied + v6.1.1 player exclusion + lesser-tier count fix |
| tools/probe_bin.py | tolerant to enemy_count overshoot bug |
| tools/qualify_oracle.py | RO-fs fallback for report json |
| tools/calibrate_smoke.py | tolerance bumped to 12, whole-anim peak, RO-fs fallback |
| tools/inspect_capture.py | new diagnostic dump tool |
| tools/station-ssh.sh | new — SSH password-auth helpers |
| tools/rebuild-and-stage.sh | migrated to station-ssh.sh, vendor mirror not overlay |
| probe/v6.1/apply-and-build.sh | migrated to station-ssh.sh, signal-trap exit code fix, RO-fs lock dir |
| CLAUDE.md | SMB copy-locally rule pinned |
| HANDOFF.md | this file |

## SSH auth change

Migrated from key auth to password auth. Key files removed from this VM.
Server-side change applied by Josh in admin PowerShell. The orphan key
line in `C:\Users\claude\.ssh\authorized_keys` on station can still be
removed for hygiene (the public key portion ending in
`claude@codeserver-vm-to-station`).

**Password rotation:** the station password was printed to this session
log during debug (sudo cat + bash -x trace). Rotate it at end of session.

## Next session

The 89 MB qualification-20260511-133002.bin has all the data we need to
fix all three offset bugs without re-running the game:

**Position offset (Symptom 1):** the chr_ins_root region (2048 bytes) for
each enemy contains the full first 2KB of the ChrIns. Scan for the offset
that gives the Knight's known world position. Cross-reference against
DSMapStudio or community ChrIns layouts for ER 2.6.1. The +0x80 hit found
during analysis is a candidate but might be skeleton-root not gameplay-pos.

**Anim_id offset (Symptom 2):** the 8 time_act_child regions per enemy
each carry 256 bytes. The c4382 Knight at chr_ins 0x7FF43AE834F0 has 1450
focused samples — its anim_id was definitely non-zero at some point.
Scan the 256-byte payloads for u32 values matching c4382's documented
parry-window anim_ids (in data/parry_data.json under characters.c4382).

**Lock-on offset (Symptom 3):** PlayerIns + 0x6A0 returns a pointer not
a handle. Either 0x6A0 is wrong, or the field IS a pointer-to-target-struct
that needs one more dereference to get the handle. Check DSMapStudio's
PlayerIns layout for the actual target-handle slot.

Saving and analyzing these offset corrections does NOT require Josh to
play more. The existing capture is a fixture — fix the offsets, re-run
the analyzer against the same capture, and verify by checking the
extracted anim_ids match the Knight's known parry-window anim_ids in
parry_data.json.

Only AFTER offsets are fixed should we ask Josh for another live capture
to validate the end-to-end qualification PASS.

## Standing constraints (unchanged)

- Co-op safety: read-only memory, no `regulation.bin` writes
- Crash safety: SEH-wrapped derefs, loader-lock-safe DllMain
- License hygiene: MIT/Apache only for production
- Quality > speed: "Do it right. Arrival timeline isn't a concern. Ever."
- Conventional commits + checkpoint tags
- SMB perf: copy big captures locally before parsing (CLAUDE.md rule)
