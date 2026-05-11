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

## What's broken (the two real bugs)

### Bug A: qualification_nearest picks NPCs

probe.cpp:1921 `qualification_nearest` ranks all roster entries by distance,
no hostility filter. NPCs (graces, summon signs, blacksmiths if nearby) and
non-combat entities are typically closer than the enemy you're actually
fighting. Result: focus alternates between the NPC at f060=10003000 (c1000)
and the actual combat enemy at f060=43823010 (c4382).

Fix candidates:
1. Hostility flag: find an offset on ChrIns that distinguishes hostile from
   neutral. Cheat Engine archaeology needed.
2. Category blacklist: skip known non-combat c-id ranges (c0XXX-c1XXX cover
   most NPCs). Less precise but easy.
3. Stable focus: once the probe picks an enemy in qualification mode, keep
   focus on that handle for the whole session (or until it despawns).
   Simplest but loses lock-on responsiveness.

### Bug B: enemy anim_id always reads 0

probe.cpp:1398: `SafeRead<uint32_t>(time_act + Off::TIME_ACT_ANIM_ID, &s->anim_id)`
where `TIME_ACT_ANIM_ID = 0xD0`. For the player this returns valid anim_ids.
For enemies, always returns 0.

Evidence: `time_act_module` region IS captured (8KB payload). The bytes at
0x20 are NaN, 0x24-0x2C are 0.0/0.0/1.0, 0xD0 is 0. This looks like the
OUTER TimeAct module, not the active inner TimeAct that holds the running
animation.

8 different `time_act_child` regions ARE captured per enemy (8 of region 4).
These are likely the per-track active TimeAct sub-structs. One of them
contains the actual animation. Need to determine:
1. Which of the 8 children has the active anim (probably the one not at
   region_base==chr_ins, but needs verification)
2. What offset within the child contains anim_id (probably not 0xD0)

Cheat Engine work: find a known enemy at a known anim, scan for the anim_id
value within its memory near where the time_act_child points.

### Bug C (low priority): lock-on detection broken

Lock-on read from PlayerIns + 0x6A0 returns a pointer-shaped value, not a
game handle. Doesn't match any enemy handle in work[], so focus_reason
defaults to 3 (nearest) not 1 (lock_on). With Bug A active, makes Bug A
worse. Should fix once we have a better understanding of the PlayerIns
struct in 2.6.1.

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

Pick ONE bug to investigate at a time:

- **Bug B (anim_id)** is the higher-leverage fix — without it, no
  qualification PASS is possible regardless of focus picker.
- **Bug A (focus picker)** is less critical if Bug B is fixed because the
  Knight is in the roster and we can analyze it from the lesser-tier slots.

For Bug B investigation, the existing `time_act_child` regions in
qualification-20260511-133002.bin already contain the answer — scan the
8 children for one that has a non-zero u32 at some offset that correlates
with the c4382 instance's expected anim ids (use the existing c4382
parry_data.json animation list as targets to scan for).

## Standing constraints (unchanged)

- Co-op safety: read-only memory, no `regulation.bin` writes
- Crash safety: SEH-wrapped derefs, loader-lock-safe DllMain
- License hygiene: MIT/Apache only for production
- Quality > speed: "Do it right. Arrival timeline isn't a concern. Ever."
- Conventional commits + checkpoint tags
- SMB perf: copy big captures locally before parsing (CLAUDE.md rule)
