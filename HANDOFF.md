# parry-tell-probe — HANDOFF

**Last updated:** 2026-05-11 ~22:00 CDT (second session-close; tag `session-close/2026-05-11-<HHMMSS>`)

## Where we left off

Previous session-close (21:10 CDT tonight) had landed probe v6.4 production
build, identified DB join-key fuzzy mapping as the only blocker before
qualification could PASS, and rolled into the co-op session. The prior
HANDOFF's top-priority next task was: "implement DB join-key fuzzy mapping in
`qualify_oracle.py`."

## Accomplishments this session

1. **DB family-fallback shipped in `qualify_oracle.py`.** Captured c-ids that
   don't have their own DB row (e.g., c4311 Godrick Soldier) now fall back to
   the parent family entry (c4310, round-down-to-nearest-10). 262/281 c-ids in
   `data/parry_data.json` are keyed at the family-parent level; the 19
   unique-entity exceptions (e.g., c3251 Erdtree Avatar) still take exact-match
   precedence. New `JoinKeyVerdict.matched_via_family_fallback` flag for honest
   reporting. Commit `b269d7f`, tag
   `checkpoint/2026-05-11-212223-qualifyoracle-db-family-fallba`.

2. **Junk-cid filter (bonus fix).** The original `find_join_key` 7-field scan
   would match `field_at_0x1E8 = 0` against the player-skeleton row `c0000`
   (4039 player parry windows). Two new filters: candidates with value < 1000
   are skipped (real enemy c-ids are always >= 1000), and candidates whose
   matched character has zero parry windows are skipped. Same commit as #1.

3. **Anim_time duration-slot rejection in `qualify_oracle.py`.** Gate now
   requires `forward_progressions >= 50` (count of strictly-positive
   within-anim deltas) AND ranks passing candidates by `forward_progressions`
   instead of `max_segment_dur`. Duration-style fields (`TimeAct + 0x2C`:
   discrete values like 1.000 / 2.500 held constant within an anim, jumping
   between anims) used to slip through the original gate because sequences of
   identical values satisfy `val + 1e-6 >= prev_val`. New discriminator is two
   orders of magnitude wide on real data: `+0x24` = 1292 forward progressions,
   `+0x2C` = 20. Commit `b739a82`, tag
   `checkpoint/2026-05-11-212653-qualifyoracle-reject-duration`.

4. **Test fixture upgraded.** `test_qualify_oracle.py` slot 3 (`+0x2C`)
   previously held `-1.0`, short-circuiting the new rejection by failing range
   check first. Fixture now populates `+0x2C` with realistic duration decoys
   (cycles 1.0 / 2.5 / 1.5 / 3.0 across anims, held constant within each).
   New `test_family_fallback_lookup` unit test with 5 cases: exact match
   (c2130), family fallback (c4311 → c4310), exact-takes-precedence (c3251 has
   own row AND c3250 exists, must return c3251), completely-absent (c9999),
   already-multiple-of-10-but-absent (c9990, must not loop). New assertion
   locks winner at `+0x24`.

5. **End-to-end qualification PASSED on real combat footage for the first
   time.** Live capture `qualification-20260511-195759` (12,467 samples, 6,814
   focused-enemy rows, 144 s Godrick Soldier fight): join key
   `field_at_0x064 = 4311 → c4310` via family fallback, anim_time field `+0x24`
   (1760 forward progressions), 5/8 windows matched within ±11 ms (62.5% vs
   60% threshold). Verdict: **PASSED**. Data pipeline risk fully eliminated.

6. **Launch perf investigation.** Josh reported 6-minute launches. Built and
   deployed `tools/launch-monitor.ps1` (ETW kernel-process trace + 500 ms
   process snapshot loop + auto-extract DLL load timeline via `tracerpt`) to
   `C:\Projects\elden-ring\launch-monitor.ps1`. A/B test (probe disabled vs
   enabled) showed 4–5 min launch WITHOUT probe — probe is not the cause.
   DebugView capture (`STATION.log`, 611 KB) revealed a 222.5 s (3:42) silent
   gap between early init events and first audio activity. ETW trace captured
   first ~50 s of eldenring.exe activity then ran out of buffer (4 MB default);
   non-admin SSH can't enumerate eldenring.exe modules due to EAC anti-debug.
   Conclusion: launch stall is NOT in the probe; it's somewhere in the 3:42
   silent gap, likely EOS init / EAC stub / DRM check / asset preload. Not
   urgent; tracked for future investigation.

7. **Codex deep-critic applied on `launch-monitor.ps1`.** Two bugs caught and
   fixed before deploy: (a) `$LastEldenEnd` not latched → eldenring_exit branch
   would emit every 500 ms and prematurely auto-stop; (b) CSV row builder used
   naive comma-join without quoting → embedded commas / quotes / newlines could
   corrupt the CSV. Also fixed: em-dash characters broke Windows PowerShell 5.1
   parsing — replaced all em-dashes with `--`, saved as UTF-8-with-BOM.

8. **Bundle A plan decision locked.** MVP audio + L1 target filter will ship
   together as `v0.1.0` (skipping the separate MVP-only ship). L2 hue overlay
   deferred to Bundle B after real-play feedback. Risk #1 (data pipeline) is
   fully eliminated by tonight's PASS; risks #2 (D3D12 hook) and #3
   (target-of-boss field) still exist and will be addressed in PHASE4-PLAN.md.

## Next steps (priority order)

1. **Write PHASE4-PLAN.md for Bundle A** (plan-mode with Codex MCP). Bundle A
   = audio cue + target-of-boss filter, shipping as `v0.1.0`. Plan should
   cover: (a) Gate 0.B research to find boss-target-of-attention field in AI
   struct, (b) prediction-thread design inside `probe.cpp` reusing the existing
   dereference path, (c) hash-table lookup for `(cid_family, anim_id)` → parry
   windows, (d) lead-time computation + reaction-budget offset, (e) `PlaySoundW`
   + WAV resource embed, (f) INI knobs for tuning, (g) regression test plan vs
   the qualification harness we already have.
2. **Dispatch Bundle A implementation** once PHASE4 plan is locked. Expected
   ~2 work sessions.
3. **Plan Bundle B** (L2 hue overlay) after Bundle A survives a few co-op
   sessions, with real-use opinions from play.

## Open questions for Josh

- None blocking.
- Future question for Bundle B: hue color choices (primary vs alert), edge
  thickness, opacity. Best answered after MVP play.

## Tried and ruled out

- **Original `find_join_key` 7-field scan with no value filter** — silently
  matched player-skeleton `c0000` via `field_at_0x1E8 = 0`. Fix: min-value-1000
  + must-have-parry-windows filters.
- **`max_segment_dur` as primary anim_time tiebreak** — let `+0x2C` duration
  slot win despite carrying discrete jumps not playback. Fix: `forward_progressions`
  counter, gate threshold 50, tiebreak by progressions.
- **Default 4 MB ETW buffer in `launch-monitor.ps1`** — fills during the
  launch burst (msedge/webview spawn explosion), dropping image-load events
  past ~50 s of eldenring.exe activity. Future fix: larger buffer
  (`-bs 65536 -nb 32 256`) plus rundown provider for image-name resolution.
- **Reading boot log while game is running** — `fopen_s` opens
  `parry-tell-probe.boot.log` with exclusive locking, refusing concurrent
  reads. Trivial fix in next session: pass `FILE_SHARE_READ` to the open call.
  Not blocking.

## Files modified

**Source / tools:**
- `tools/qualify_oracle.py` — two fixes (family fallback + duration rejection),
  new helper `_lookup_with_family_fallback`, `AnimTimeVerdict` gains
  `forward_progressions` field
- `tools/test_qualify_oracle.py` — new `test_family_fallback_lookup` (5 cases),
  synthetic fixture upgraded to populate duration decoy on slot 3, new
  assertion on winner offset
- `tools/launch-monitor.ps1` — NEW; deployed to
  `C:\Projects\elden-ring\launch-monitor.ps1` on station

**Docs:**
- `CHANGELOG.md` — three entries prepended today (family-fallback,
  duration-rejection, and this session-close)
- `PHASE3-PLAN.md` — Session Log entry appended for tonight's second session

**Captures (read-only, copied local for analysis):**
- `/tmp/launch-2147/{timeline.csv,dlls.csv,trace.etl}` — launch monitor outputs
- `/tmp/STATION.log` — DebugView from station, 611 KB
- `/tmp/boot.log` — probe boot log snapshot

## Services / processes

- Probe v6.4 re-enabled at `/mnt/station-mods/parry-tell-probe.dll` after the
  A/B perf test.
- ETW trace `ParryTellLaunchTrace` started + stopped cleanly during the
  monitored launch.
- No long-running services changed.

## Git state at session close

- **Branch:** `main`
- **Uncommitted before session-close commit:** `tools/launch-monitor.ps1`
  (untracked) + CHANGELOG / PHASE3-PLAN / HANDOFF modifications from this
  session-close run
- **Unpushed commits:** 22 prior to session-close (will be 23+ after this run)
- **Recent commits (last 10):**
  - `b739a82` chore(cp): qualify_oracle: reject duration-slot anim_time candidates
  - `b269d7f` chore(cp): qualify_oracle: DB family-fallback + junk-cid filter
  - `14071b9` chore(sc): Probe v6.4 production + co-op tooling; three offsets resolved
  - `63885eb` docs(handoff): document tonight's co-op session operating manual
  - `7cc9a45` feat(probe-v6.4): production cleanup + co-op safety + audible F11 + tooling
  - `6016681` chore(cp): v6.4 plan logged in HANDOFF.md; starting cleanup
  - `9d1ce95` chore(cp): bundle v6.3 DLL artifact
  - `b8222ef` feat(probe-v6.3): module-bag-wide enemy anim_id instrumentation + lock-on derivation fix
  - `023ad0e` chore(cp): pre-v6.3 module-bag-wide instrumentation
  - `ec4a140` fix(probe-v6.2): apply Codex deep-review P1 findings before capture session
- **Session-close tag:** `session-close/2026-05-11-<HHMMSS>` (check
  `git tag --list | tail` after close)

## Pickup prompt for next session

```
Resume parry-tell-probe. Read HANDOFF.md first.

Primary task: write PHASE4-PLAN.md for Bundle A (audio cue + target-of-boss
filter, v0.1.0). Use plan-mode with Codex MCP. Plan must cover:
  (a) Gate 0.B research to find boss-target-of-attention field in AI struct
  (b) prediction-thread design inside probe.cpp reusing existing dereference path
  (c) hash-table lookup for (cid_family, anim_id) -> parry windows
  (d) lead-time computation + reaction-budget offset
  (e) PlaySoundW + WAV resource embed
  (f) INI knobs for tuning
  (g) regression test plan vs the qualification harness we already have

Data pipeline is fully validated. qualify_oracle.py produces PASSED verdicts
on real combat captures (qualification-20260511-195759, 5/8 windows, 62.5%).
All tests green. Probe v6.4 deployed and correct.

Minor known issue: parry-tell-probe.boot.log is exclusively locked while the
game is running (fopen_s, no FILE_SHARE_READ). Fix in next session if it
causes friction; not blocking PHASE4 planning.
```
