# parry-tell-probe — HANDOFF

**Last updated:** 2026-05-11 20:30 CDT (post-v6.3 capture analysis, pre-v6.4)

## Where we are

v6.3 capture (qualification-20260511-195759.bin, 144s armed, 12,467 focused
rows of c4311 Godrick Soldier + c4382 Knight at Gatefront) resolved ALL
THREE research-006 offset questions with HIGH confidence.

**The probe was never the bug.** Research 006 was a false alarm caused by
a stationary-enemy sample in v6.2. v6.3 with an actively-fighting enemy
showed the original v6.1.1 offsets are correct.

### Resolved offsets

| Field | Offset | Path | Source of truth |
|---|---|---|---|
| Enemy + player world pos | `bag→+0x68→+0x70` (Vector3) | phys-chain (TGA CT v1.17 + vswarte) | v6.3 byte-verified; player legacy `+0x6C0` also works but has chunk wraps |
| Enemy active anim_id | `TimeAct + 0xD0` | path A (original v6.1.1) | v6.3 8,265 nonzero reads with 89 transitions for c4311; c4382 path_a values cleanly match c4380 anim list |
| Enemy anim_time | `TimeAct + 0x24` AND `+0x28` | candidates 1+2 | v6.3 shows clean monotonic playback with ~16 resets on anim_id transitions, ~50 monotonic segments |
| Player lock-on target | `PlayerIns + 0x6B0` (FieldInsHandle u64) | new in v6.3 | 20 transitions vs 0 for `+0x6A0`; FromSoft handle pattern `0x3C2A2500_NNNNNNNN`; player vtable RVA `0x02A7CB40` confirms PlayerIns subclass |
| Player lock-on target area | `PlayerIns + 0x6B4` (u32) | new in v6.3 | Toggle-paired with `+0x6B0` |

### What v6.3 also fixed

- `in_lock_on` flag now fires correctly (it derived from the dead `+0x6A0`
  in v6.1.1/v6.2). v6.3 introduced `playerLockHandleEffective` that prefers
  `+0x6B0`. focus_reason=FOCUS_LOCK_ON (1) now fires in 74% of v6.3 samples,
  was 0% in v6.2.
- Boss-bar gating (was silently broken because boss-bar correlation gated
  on `in_lock_on`).

## What's NOT resolved (not a probe problem)

These are analytical issues for v6.5+ work, NOT v6.4 blockers:

1. **DB join-key fuzzy mapping.** `field_at_0x064` captures the individual
   c-id variant (e.g. c4382 Knight). `parry_data.json` is keyed by parent
   c-id (e.g. c4380). `qualify_oracle.py` joins exact-match and silently
   filters out variants. Needs a parent-family lookup
   (`int(cid_str[:5]) → parent` or a per-c-id table).
2. **Some fought enemies aren't in the parry DB.** c4311 (Godrick Soldier,
   74% of v6.3 focused rows) is NOT in `parry_data.json` at all. The DB has
   107 chars with parry windows out of 281 with anim data. Soldiers don't
   appear to be parry-eligible per the extractor's rules. **Aim for known-DB
   targets** (c2130 Banished Knights in Stormveil interior, c4380 Knights,
   bosses).

## v6.4 plan — production build for tonight's multi-boss session

**Goals:**
1. Drop the v6.2/v6.3 instrumentation regions that didn't pan out (6/7/8/9).
   Keep schema_version at 2 so v6.2/v6.3 captures stay parseable.
2. Keep ALL the resolved data emitting going forward — no regressions:
   - Player Tier 1 v6.2 block (vtable, phys pos, phys module abs, +0x6B0
     lock, +0x6B4 area, +reserved) — KEEP all 48 bytes
   - Enemy header v6.2 block (anim_id_path_b/c, read_idx, action_request,
     phys_module, world_pos_phys, +20 pad) — KEEP all 44 bytes + 20 pad
3. **Co-op safety fix: exclude all friendly PCs from roster sweep.** Currently
   v6.1.1's player-skip only excludes `playerChrIns` (the local player at
   slot 0 of WCM_PLAYER_ARRAY). For Josh's tonight session with up to 5
   co-op friends, all 4 slots of WCM_PLAYER_ARRAY (and the Seamless Coop
   extension) should be excluded. Otherwise focus_reason=3 (nearest) latches
   onto whichever friendly is closest, not the boss. Boss-bar detection
   should still pick up bosses correctly via boss_bar_handles[].
4. **Audible F11 feedback.** On F11 transition, emit a `MessageBeep` from
   the probe DLL:
   - ARMED → `MB_ICONASTERISK` (system beep, lower tone, 2 short beeps)
   - DISARMED → `MB_ICONEXCLAMATION` (system beep, higher tone, 1 long beep)
   - Make it OBVIOUS which is which. Test on station before deploying.
5. **Status indicator** — write a small PowerShell or batch script that
   tails the probe's `.log.txt` and prints ARMED/DISARMED with a timestamp
   to a console window. Josh can keep it on a second monitor.

**What NOT to change:**
- World pos legacy fields (`+0x6C0` for player, `field_at_0x06C` for enemy)
  — KEEP both legacy + phys reads in wire format. Cost is minimal. Future
  re-analysis stays unambiguous.
- Lock-on legacy `+0x6A0` — KEEP in wire format (probe still writes it,
  parser still reads it) for v6.3 capture comparability.
- The v6.2 schema-v2 instrumentation block in the wire format. Even though
  the regions are dropped, the per-enemy and per-sample v6.2 fields stay
  populated — they're useful (anim_id_path_b/c are still good debug data;
  phys_module pointer enables future re-targeting work).

## Multi-boss capture workflow (tonight)

**Game session flow:**
1. Boot game. Probe auto-attaches.
2. Walk to first boss arena.
3. F11 to arm → wait for double-low beep.
4. Engage. Wipe and retry as many times as needed (probe stays armed
   through wipes — the .bin file is never closed mid-session).
5. Kill the boss. F11 to disarm → wait for single-high beep.
6. Tell Claude: "done with <boss name>". Claude pulls the .bin local +
   archives a snapshot.
7. Walk to next boss. F11 to arm. Repeat.
8. End of session: F11 disarm, quit game cleanly, tell Claude.

**Co-op specifics:**
- Up to 6 player characters in lobby (Josh + 5 friends). Sometimes Josh is
  the host; sometimes another friend is host and Josh is a guest using
  someone else's character. **Probe v6.4 excludes all friendlies from
  roster sweep** — focus_reason picks the boss (via boss_bar_handles[])
  when fighting a real boss with an HP bar.
- For non-boss-bar enemies (random mobs), focus_reason=3 (nearest) will
  still pick the closest non-player. That's fine — we're not analyzing
  random mobs.

**File handling on Claude's side:**
- After each "done" signal: copy `qualification-NNNN.bin` from
  `/mnt/station-projects/elden-ring/logs/` to a session-archive dir under
  `/home/joshua.blattner/claude/elden-ring/captures/sessions/YYYYMMDD/`.
- Also copy the `.log.txt` (small) and `.csv` (medium) — even though we
  primarily analyze the .bin, keep the CSV for human inspection if needed.
- The .bin contains ALL boss fights from the session interleaved — segment
  by F11 ARMED/DISARMED log timestamps. The log txt has `F11: armed` and
  `F11: disarmed` lines with the relative ms.

**What we keep forever:**
- Every .bin → `captures/sessions/YYYYMMDD/<session-name>.bin`
- Every .log.txt → same dir
- Every CSV → same dir
- A `captures/sessions/YYYYMMDD/README.md` with the boss list + Josh's
  notes on which c-ids he thinks each boss was

We don't truncate or rewrite anything. Re-analysis with future tooling
(better anim_id correlator, better predictor) will always have raw data
to work from.

## What's NOT in v6.4 (deliberately deferred)

- Drop the Tier 3 region 4/5/8/9 emissions — RETAIN region 4 (time_act_child)
  since it's useful for re-analysis. Drop region 8 (time_act_child_body,
  was for v6.2 deep-body experiment) and region 9 (module_bag_member,
  was for v6.3 wide-scan experiment) since their purpose is done.
- Drop region 6 (phys_module_body) and region 7 (action_request_body) —
  these were instrumentation; the values we needed (phys pos, anim
  candidates) are in the header block now.
- Lock-on legacy `+0x6A0` read in wire format — keep it. Cheap to retain,
  expensive to lose if we ever need to re-cross-reference.

## Tools needed tonight (Claude-side)

- **Per-boss segmenter:** new `tools/segment_by_f11.py` that reads a .bin
  + the .log.txt, finds ARMED/DISARMED boundaries, emits per-boss
  sub-samples. Quick to write (~30 lines).
- **Session archiver:** new `tools/archive_session.sh` that takes a
  date + capture name and tarballs everything into the project tree
  with the right naming convention. ~20 lines.
- **Live tailer for Josh's status indicator** (delivered to Josh's
  station): new `tools/probe_status.ps1` (PowerShell) that tails
  `parry-tell-probe.boot.log` + the relevant `.log.txt`, prints
  `ARMED at HH:MM:SS` / `DISARMED at HH:MM:SS` lines.

## Active files

- `probe/probe.cpp` (v6.3 source, to be v6.4'd)
- `tools/probe_bin.py` (schema v2 parser, no changes needed for v6.4)
- `tools/analyze_v62_capture.py` (research-006/007 analyzer)
- `tools/qualify_oracle.py` (anim_time → parry-window join; needs c-id
  family fuzzy lookup before qualification can PASS — deferred to v6.5)
- `data/parry_data.json` (107 chars with parry windows; 6,738 windows
  total; complete, no bugs)
- `data/research-fixture/` (c4382 sample used in research-006)

## Decision log

- 2026-05-11 ~12:00: probe v6.1.1 deployed; capture qualification-20260511-133002.bin
- 2026-05-11 ~14:00: research-006 (deep-research + Codex deep-research)
  concluded path A was wrong; bundled three-offset fix proposed
- 2026-05-11 ~15:00: fixture verification REFUTED path A AND B (queue all
  sentinels) — research-006's vendor consensus was right in the abstract
  but didn't match c4382 fixture bytes; v6.2 instrumentation build commissioned
- 2026-05-11 ~18:50: v6.2 (instrumentation w/ Codex deep-review fixes) deployed
- 2026-05-11 ~19:20: v6.2 capture (qualification-20260511-191334.bin) →
  research-007 analysis: Q1 phys-chain wins, Q3 +0x6B0 wins, Q2 dead end
- 2026-05-11 ~19:55: v6.3 (module-bag-wide instrumentation + lock-on
  derivation fix) deployed
- 2026-05-11 ~20:08: v6.3 capture (qualification-20260511-195759.bin) →
  Q2 was always correct, path A reads valid anim_ids when enemy is animating.
  v6.2 had a stationary-enemy sample, that's all.
- 2026-05-11 20:30: v6.4 cleanup planned for tonight's multi-boss session.
