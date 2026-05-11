# HANDOFF — parry-tell (elden-ring)

**Last update:** 2026-05-11 — session-day analyzer fix
**Branch:** main — ready to play
**Probe v6 DLL:** live in `Game\mods\` (smoke INI loaded)

## Session-day note (2026-05-11)

### SSH auth changed: key → password

Per Josh's request, station SSH switched from key auth to password auth so
SMB and SSH share a single credential surface. Changes:

- Local `~/.ssh/station_key` + `station_key.pub` deleted from this VM.
- Station sshd_config: `PasswordAuthentication yes` (line 51), service
  restarted. Done by Josh in an admin PowerShell — the `claude` account
  is non-admin so I can't touch sshd config from this side.
- New helper `tools/station-ssh.sh` provides `station_ssh` / `station_scp`
  / `station_scp_recursive`. All reads the password from
  `/etc/ssh-credentials-station` (root:600) via `sudo -n cat`. Endpoint
  enforcement (claude@station only), option-injection guard (no caller-
  supplied dash args), pinned host key (StrictHostKeyChecking=yes).
- `tools/rebuild-and-stage.sh` and `probe/v6.1/apply-and-build.sh` both
  updated to source the helper.
- Local-side cleanup still owed: Josh should remove the line ending in
  `claude@codeserver-vm-to-station` from `C:\Users\claude\.ssh\authorized_keys`
  on station. That kills the now-orphan key-auth path entirely.

### Analyzer fix

Pre-session validation against the existing 9,867-sample 2026-05-09 capture (continued):
caught two analyzer bugs that would have masked a successful smoke today:

1. `ANIM_TRANSITION_TOLERANCE_SAMPLES` was 2 in `calibrate_smoke.py` and
   3 in `qualify_oracle.py` (`ANIM_TRANSITION_LAG_SAMPLES`). Real data
   shows lag distribution 1-10 samples (median 4, P90 9). Bumped both to
   12, with a comment block explaining the measured distribution.

2. Rewind detector compared the new-anim value against `cur_seg_max` /
   `prev_val` (the local segment peak or last-sample value), which gets
   corrupted by within-anim micro-rewinds in looping anims. Both analyzers
   now track `cur_anim_max` (the actual peak across the entire prior anim)
   and use that as the comparison baseline. This eliminated the last two
   false-positive failures on +0x24 in the real data.

After the fix: pre-session run against `smoke-20260509-170547` produces
**PASS** with TimeAct +0x24 and +0x28 as winners (109 monotonic segments,
5.87s max segment, 38/38 anim transitions show rewind), exactly matching
the v6 spec prediction. +0x20 and +0x2C correctly FAIL.

Test fixtures strictened: `test_qualify_oracle.py` now uses LAG_SAMPLES=9
(real P90) instead of 2. All three regression tests (probe_bin,
calibrate_smoke, qualify_oracle) PASS.

---

## Pickup prompt for next session

> "Tomorrow's session: read HANDOFF.md. Probe v6 is live on station. The
> probe captures clean data (verified by 9,867 samples at 91.3 Hz on
> 2026-05-09). The analyzer pipeline is fixed and self-tested. Josh will
> power through smoke → qualification → discovery in one push.
>
> First move when Josh signals he's playing: confirm SMB mounts (`mount |
> grep station`). Wait for 'smoke done'. Run `python tools/calibrate_smoke.py
> /mnt/station-projects/elden-ring/logs/smoke-<ts>`. Report verdict.
> Follow the SEQUENCE block in HANDOFF.md from there."

---

## Where we are right now

**Probe v6 has captured real data and the data is good.** On 2026-05-09 Josh
ran three smoke attempts. The third was clean: 108 seconds, 9,867 samples,
91.3 Hz effective rate. The probe is working.

**The analyzer pipeline had two bugs that masked this success.** Both fixed
tonight:

1. `probe_bin.py` had `SRD0_MAGIC` byte-reversed (`0x53524430` should have
   been `0x30445253`). The synthetic self-test was using the same wrong
   constant on both ends, so it passed; the real probe wrote correct
   little-endian bytes and was rejected. Fix in commit `6d41b24` (nightly
   cron picked it up). All three records on disk now parse cleanly.

2. Both calibration analyzers had an anim-transition tolerance bug. When
   the game emits a new `anim_id`, the `anim_time` field can take 1-2
   samples (~22 ms at 91 Hz) to catch up to the new value. The rewind
   check was firing on the first post-transition sample, where val still
   equals prev_val (lag carry-over), so it incorrectly concluded "value
   did not rewind." Fix: defer the rewind check until N samples after the
   transition. Both `qualify_oracle.find_anim_time_field` and the new
   `calibrate_smoke.py` now handle this correctly. Verified against
   lag-modeled synthetic fixtures.

**What's true about the v6.0 probe behavior:**

- Roster (WCM) init fails because the user is on the title screen / save
  menu during the probe's 15-second grace window. Probe falls back to
  "player + boss bars only" mode. **This is fine for smoke.** It might
  not be fine for discovery (Stormveil trash mobs don't have boss bars).

- A `v6.1` patch is drafted and ready at `probe/v6.1/`. Three changes:
  - Extend WCM init grace from 15s to 60s.
  - On F11-arm, re-attempt WCM init if currently disabled (gives 5s window
    after the user actually loads into the world).
  - De-spam the per-iteration "WCM not yet readable" boot log.
  - Apply with `bash probe/v6.1/apply-and-build.sh` — script handles
    SCP + MSBuild + DLL drop + rollback on any failure.
  - Self-tested: `git apply --check` passes; bash syntax checks; all
    safety paths (mountpoint verify, single-instance flock, signal traps)
    in place.

---

## SEQUENCE for tomorrow's session

This is the exact flow. Steps in CAPS are Josh's actions; the rest is mine.

### Phase A — Smoke (60 sec, optional replay)

1. JOSH: launch Elden Ring. Game loads with v6 DLL + smoke INI.
2. JOSH: load save, walk to any Site of Grace, press F11.
3. JOSH: follow `probe/v6/GAMEPLAY-smoke.txt` (8 deliberate actions, ~5 sec
   each). Walk → light attack → heavy attack → gesture → use item → roll →
   sprint → walk again.
4. JOSH: press F11 to disarm. Quit Elden Ring cleanly (so worker thread
   exits and `.calibration.txt` writes).
5. JOSH: ping "smoke done".
6. ME: `python tools/calibrate_smoke.py /mnt/station-projects/elden-ring/logs/smoke-<latest-ts>`
7. ME: report verdict. Expected: PASS with one or both of +0x24/+0x28 as
   anim-time winner.

**OPTIONAL:** We may also skip the replay since 9,867 samples from
2026-05-09 already exist on station. If the station is up I can re-run
the analyzer against the old capture and confirm before Josh touches the
game.

### Phase B — Qualification (2-3 min)

1. JOSH: on station, open cmd window. Run:
   ```
   cd C:\Projects\elden-ring\probe\stage
   swap-mode.bat qualification
   ```
2. JOSH: launch Elden Ring.
3. JOSH: travel to Stormveil entrance. Lock onto a Banished Knight.
4. JOSH: press F11. Fight 2-3 minutes — let the Knight throw their full
   attack rotation, parry attempts are fine but not required.
5. JOSH: press F11 to disarm. Quit cleanly.
6. JOSH: ping "qualification done".
7. ME: `python tools/qualify_oracle.py /mnt/station-projects/elden-ring/logs/qualification-<latest-ts>`
8. ME: report verdict.

**Three outcomes possible:**

- **PASS** with Banished Knight detected as `c2130`, anim-time field
  identified, parry windows match within ±11ms: proceed to Phase C.
- **PASS** but with low window-match rate (<70%): proceed cautiously to
  Phase C; we'll need to investigate why some windows miss.
- **FAIL** with no enemy records captured: Banished Knight was not in
  `boss_bar_handles[]` AND not in the disabled-roster fallback. **Apply
  v6.1 patch:**

  ```
  bash probe/v6.1/apply-and-build.sh
  ```

  This needs station SSH up and game closed. ~5 min build + drop. Retry
  Phase B from step 1.

### Phase C — Discovery (~1 hour)

1. JOSH: on station, run `swap-mode.bat discovery` in `probe\stage\`.
2. JOSH: launch Elden Ring.
3. JOSH: follow `probe/v6/GAMEPLAY-discovery.txt`. Suggested route:
   - Stormveil mob route, ~25 min (Banished Knights, soldiers, dogs)
   - Roundtable Hold, ~10 min (non-combat baseline)
   - Boss attempt, ~25 min (ideally Crucible Knight in Stormveil)
4. JOSH: F11 to arm at session start. F11 to disarm at session end.
   No mid-session toggling.
5. JOSH: quit cleanly. Ping "discovery done".
6. ME: `python tools/analyze_discovery.py /mnt/station-projects/elden-ring/logs/discovery-<latest-ts>`
7. ME: top-50 byte candidates ranked by in-window vs out-of-window mutation
   rate. We iterate.

**Discovery may need multiple analysis passes** even on one capture. The
analyzer is scaffolding; real-data tuning happens here. Be patient with
iterating on candidate filters.

---

## Tonight's prep work (2026-05-10 evening)

Six things landed:

1. **`probe_bin.py`** — `SRD0_MAGIC` corrected (`0x30445253`). Committed.
2. **`tools/calibrate_smoke.py`** — standalone Python equivalent of the
   in-DLL calibration report. Necessary because `WriteCalibrationReport()`
   only fires on clean worker-thread exit, which doesn't reliably happen
   when the user quits the game. Run anytime against any smoke .bin.
3. **`tools/test_calibrate_smoke.py`** — regression gate. Synthetic
   fixture modeled on the 2026-05-09 smoke data. Verifies +0x20 FAILs
   (always-zero junk), +0x24/+0x28 PASS (real anim-time, multiple
   monotonic segments), +0x2C FAILs (animation total duration, no
   accumulation). PASSES.
4. **`tools/qualify_oracle.py`** — `find_anim_time_field` patched with
   3-sample anim-transition lag tolerance. Now correctly identifies
   anim-time field on lag-modeled data. Existing `test_qualify_oracle.py`
   extended to emit the lag pattern; PASSES with verdict=PASSED, 8/8
   windows match within ±11ms.
5. **`probe/v6.1/`** — patch dir with:
   - `CHANGES.md` — detailed explanation of the three v6.1 changes
   - `probe-v6.1.patch` — unified diff (`git apply --check` passes)
   - `apply-and-build.sh` — one-command deploy with full rollback
     (atomic-ish DLL swap, mountpoint preflight, flock for single-instance,
     ERR/INT/TERM/HUP traps, station-side and local-side backup tracking
     so partial failures restore cleanly)
6. **HANDOFF.md** — this file, rewritten.

**Total deltas:**
- 2 new tools (calibrate_smoke + its test)
- 1 patched analyzer (qualify_oracle + its test)
- 1 new probe patch dir with 3 files
- 1 rewritten handoff

All self-tests pass. The build is healthy. Tomorrow's path is clear.

---

## Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| Smoke replay produces no `.calibration.txt` | High | Calibrate via `tools/calibrate_smoke.py` from the .bin; the in-DLL writer is unreliable (worker exit race), and we don't need it. |
| Smoke shows both +0x24 and +0x28 as winners | High | They're nearly identical; qualification disambiguates by parry-window match rate against `parry_data.json`. Don't pick prematurely. |
| Banished Knight not in `boss_bar_handles[]` and roster disabled | Medium | v6.1 patch is ready. ~5 min to apply + rebuild + drop. Game must be closed during DLL swap. |
| Discovery's `analyze_discovery.py` doesn't surface a clean parry-active flag in top-25 | Medium | Tune the analyzer (different correlation metric, longer windows, region-focused). No new gameplay needed; iterate on existing .bin. |
| Station SSH service is down | Low | Josh starts it manually at session begin. Test with `ssh claude@station 'echo SSH_OK'` before any deploy. |
| SMB mounts not live | Low | `x-systemd.automount` remounts on access. `mount \| grep station` to verify. |
| Game version differs from 2.6.1 expected | Very low | Probe logs `init_ok: ER FileVersion X.Y.Z.W`. Manifest captures the version. If FromSoft patched between sessions, sig-scan may need to be re-keyed; that's a v6.2 problem. |

---

## Files modified or created in tonight's prep

| File | Status |
|---|---|
| `tools/probe_bin.py` | `SRD0_MAGIC` corrected (committed via cron 6d41b24) |
| `tools/calibrate_smoke.py` | New — standalone smoke calibration analyzer |
| `tools/test_calibrate_smoke.py` | New — regression test (PASSES) |
| `tools/qualify_oracle.py` | `find_anim_time_field` patched with lag tolerance |
| `tools/test_qualify_oracle.py` | Lag pattern added to synthetic fixture |
| `probe/v6.1/CHANGES.md` | New — v6.1 patch description |
| `probe/v6.1/probe-v6.1.patch` | New — unified diff (passes `git apply --check`) |
| `probe/v6.1/apply-and-build.sh` | New — one-command deploy with rollback |
| `HANDOFF.md` | Rewritten (this file) |

---

## Git state (will update at commit time)

- HEAD before tonight's commit: `6d41b24` (nightly backup picked up
  probe_bin.py fix)
- Tonight's commit will land calibrate_smoke + qualify_oracle patch +
  v6.1 patch dir + this HANDOFF.

---

## Standing constraints (don't drop these)

- **Co-op safety:** read-only memory only, no `regulation.bin` writes.
- **Crash safety:** SEH-wrapped derefs, loader-lock-safe DllMain, module
  pinned via `GET_MODULE_HANDLE_EX_FLAG_PIN`.
- **License hygiene:** MIT/Apache only for production. MinHook BSD-2 vendored.
- **Quality > speed:** "Do it right. Arrival timeline isn't a concern. Ever."
- **Dual-visibility for in-chat tests:** N/A this project (no Vera-style
  webhook integration).
- **Conventional commits + checkpoint tags:** use `~/bin/checkpoint.sh` for
  proactive saves before risky operations.
