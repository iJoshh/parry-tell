# parry-tell — HANDOFF

**Last updated:** 2026-05-08, end of pre-test prep session.
**Status:** READY FOR JOSH'S SMOKE TEST. All non-Josh work complete.

## Read this first

You are Claude (Mae). Josh commissioned parry-tell, an Elden Ring + SotE
mod that gives audio + visual cues for parryable boss attacks during
Seamless Co-op as a guest. v6 is the discovery probe (not the production
mod) — captures memory state across varied gameplay so we can identify
the runtime parry-active flag.

The product strategy is in `PHASE3-PLAN.md`:
- Week 1: MVP audio-only via animation_id + animation_time + database
- Mid-stream: discovery probe runs to hunt for a runtime parry-active flag
- Weeks 2-4: L2 hue / L1 target filter / lock-on / INI config

This session built the v6 probe end-to-end and prepped everything that
can be prepped without Josh playing.

## What's READY

### DLL + configs
- `probe/probe.cpp` v6 source (3076 lines). Built clean first try via
  MSBuild on station. Codex-reviewed; 6 fixes + 1 nit applied; 1 blocker
  (boss-bar fallback) addressed via priority/fill split; 1 blocker
  (detour does compute) declined with explicit reasoning.
- DLL staged at `C:\Projects\elden-ring\probe\stage\parry-tell-probe.dll`
  (visible from VM at `/mnt/station-projects/elden-ring/probe/stage/`).
- INI templates also staged in that same dir:
  `parry-tell-probe.ini.{smoke,qualification,discovery}`.
- Logs directory ready at `C:\Projects\elden-ring\logs\` (the INIs point
  there). VM-side path: `/mnt/station-projects/elden-ring/logs/`.
- Tarball backup: `probe/releases/probe-v6.tar.gz` (committed).

### Analysis pipeline
- `tools/probe_bin.py` — .bin/.csv reader library. Wire format byte-for-
  byte symmetric with `probe.cpp` writer (verified by automated test).
- `tools/probe_status.py` — quick "is this capture alive?" report.
- `tools/qualify_oracle.py` — full qualification analyzer (join key,
  anim-time field, predicted-vs-observed windows).
- `tools/analyze_discovery.py` — discovery-mode byte-correlation
  scaffolding. Identifies bytes that change much more inside parry
  windows than outside; ranks top 50 candidates.
- `tools/test_probe_bin.py` — wire-format round-trip test. PASSES.
- `tools/test_qualify_oracle.py` — end-to-end qualification analyzer
  test on synthetic data mimicking a real fight against c2130. PASSES.

### Documentation
- `probe/v6/PROBE-V6-SPEC.md` (856 lines, rev 4, Codex green-lit) —
  the spec the source implements.
- `probe/v6/RUNBOOK.md` — Josh's runbook for smoke / qualification /
  discovery. **Read this if you're picking up mid-test.**
- `research/conversations/001-005` — Codex review history (4 spec rounds
  + green-light).

## What's WAITING for Josh

DLL + smoke INI are ALREADY in `Game\mods\`. Josh just launches the game.

1. Smoke test (60 sec at a Grace, 8-step deliberate-action script)
2. Qualification (2-3 min vs ONE locked-on parry-eligible enemy)
3. Real discovery session (~1 hour varied gameplay)

Mode swaps are self-service via `swap-mode.bat smoke|qualification|discovery`
on station (in `C:\Projects\elden-ring\probe\stage\`).

After each session, Josh tells Claude "smoke done" / "qualification done" /
"discovery done" and Claude parses the capture from the SMB-mounted logs
dir.

Plain-text gameplay scripts (readable from phone if Josh wants to walk
through the steps untethered from chat):
- `C:\Projects\elden-ring\probe\stage\GAMEPLAY-smoke.txt`
- `C:\Projects\elden-ring\probe\stage\GAMEPLAY-qualification.txt`
- `C:\Projects\elden-ring\probe\stage\GAMEPLAY-discovery.txt`

## What to do when Josh says "smoke done"

1. Find the latest smoke capture base path:
   ```
   ls -t /mnt/station-projects/elden-ring/logs/smoke-*.bin | head -1
   ```
   Strip the `.bin` suffix to get the base path.
2. Run quick sanity check:
   ```
   python tools/probe_status.py /mnt/station-projects/elden-ring/logs/smoke-<ts>
   ```
   First line is a top-level VERDICT — read that to know if capture is
   alive at all.
3. Read the calibration report:
   ```
   cat /mnt/station-projects/elden-ring/logs/smoke-<ts>.calibration.txt
   ```
   Confirm one anim-time candidate has `gate=PASS` (rev3 expects +0x24).
4. If smoke fails, run `python tools/probe_diag.py` to pull both boot.log
   and the latest .log.txt into one view.

## What to do when Josh says "qualification done"

1. Find latest qualification base path.
2. Run `python tools/qualify_oracle.py /mnt/station-projects/elden-ring/logs/qualification-<ts>`.
3. Verdict at end: PASSED → Josh can proceed to discovery. FAILED →
   diagnose from the report's "reason" field.

## What to do when Josh says "discovery done"

1. Find latest discovery base path.
2. Run `python tools/probe_status.py <base>` first to confirm capture
   sizes look right (multi-GB, low drop counters).
3. Run `python tools/analyze_discovery.py <base>`.
4. Read top-50 candidates; look for bytes with very high in-window
   change rate vs out-of-window. The runtime parry-active flag, if
   reachable, sits at the top.

## If Josh needs to rebuild between tests (offset bug, signature drift)

```
bash tools/rebuild-and-stage.sh
```

This script handles: SCP source + project + vendor → MSBuild on station →
verify DLL produced → stage → byte-compare staged vs build artifact.

After the rebuild, the DLL in `Game\mods\` is now stale. Either:
- Drop the new one over it: `cp /mnt/station-projects/elden-ring/probe/stage/parry-tell-probe.dll /mnt/station-mods/parry-tell-probe.dll`
- Or have Josh run a fresh `swap-mode.bat <mode>` (which doesn't update
  the DLL — only the INI). The DLL drop is a separate cp.

## Common pitfalls (preempting future me's mistakes)

1. **Don't "fix" the spec.** It's been Codex-reviewed 4 times; any
   contradiction is a real one and needs Josh's call before changing.
2. **Don't run discovery before qualification PASSES.** The whole point
   of qualification is to prove the join key + anim-time interpretation
   before committing to a 1-hour capture.
3. **Don't try to resolve boss-bar handles outside the roster.** v5e
   debugging proved `GetChrInsFromHandle(wcm, &stack_handle_copy)`
   returns the input back unchanged. The function needs a live struct
   field. The roster sweep is the only safe path; if roster fails,
   boss-bar enemies degrade to Tier-1-only (their handles in the
   `boss_bar_handles[]` array; no Tier 2/3). This is intentional
   per spec.
4. **Don't add JSON loading to the DLL.** The 30 MB `parry_data.json`
   is the post-session oracle, parsed by Python. The DLL has zero JSON
   dependency.
5. **Don't add VirtualQuery to the detour.** v5f learned this lesson —
   30+ syscalls per sample on the game thread caused per-second hitches.
   Use `LooksLikeUserPtrFast` (pure compute) only.

## Game-version cliff

The probe is pinned to ER FileVersion 2.6.1.0 via the version check in
`CheckExpectedGameVersion`. If Steam updates the game, the probe will
log `init_fail: ER FileVersion <new> (expected 2.6.1.0)` and refuse to
install the hook. We then need to re-research signatures (most likely
work; offsets less so) and bump the expected version.

## Build pipeline (if source ever needs to change)

```bash
# 0. Verify SSH is up (Josh starts manually):
timeout 5 ssh -i ~/.ssh/station_key -o ConnectTimeout=3 -o BatchMode=yes \
  claude@station "echo SSH_OK"

# 1. Push:
scp -i ~/.ssh/station_key ~/claude/elden-ring/probe/probe.cpp \
  claude@station:C:/Projects/elden-ring/probe/probe.cpp

# 2. Build:
ssh -i ~/.ssh/station_key claude@station \
  '"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "C:\Projects\elden-ring\probe\probe.vcxproj" /p:Configuration=Release /p:Platform=x64 /t:Rebuild /v:minimal'

# 3. Verify (DLL lands here via SMB):
ls -la /mnt/station-projects/elden-ring/probe/bin/Release/parry-tell-probe.dll

# 4. Re-stage:
ssh -i ~/.ssh/station_key claude@station \
  'copy /Y C:\Projects\elden-ring\probe\bin\Release\parry-tell-probe.dll C:\Projects\elden-ring\probe\stage\parry-tell-probe.dll'
```

## Known follow-ups (post-discovery)

1. **Delta encoding in the worker** (TODO at `probe.cpp:2130`) — would
   shrink .bin from ~5-10 GB to ~1-3 GB. Not blocking; ship correctness
   first.
2. **Build hash** — currently a `BUILD_<date>_<time>` placeholder in
   the manifest. Real SHA-256 of source would require generating a
   header at build time. Low priority.
3. **`tools/analyze_discovery.py` v1 is scaffolding only.** Once we
   have a real .bin from a 1-hour session, the byte-correlation logic
   will need real-data tuning. The top-50 ranking is a reasonable
   starting point but doesn't yet detect "this byte flips on→off→on at
   window boundaries" — that pattern recognition is the next step.
4. **L1/L2 production-mod work** is a separate project that follows
   discovery — see `PHASE3-PLAN.md`.

## Josh's standing instruction

"Do it right. My arrival timeline isn't a concern. Ever. Do it right."

## Confidence levels

- v6 spec correct: HIGH (4 rounds Codex review)
- v6 source compiles: PROVEN (clean first build, then clean rebuild after fixes)
- Wire format symmetric: PROVEN (`tools/test_probe_bin.py` PASSES)
- Qualification logic correct: PROVEN on synthetic data (`tools/test_qualify_oracle.py` PASSES)
- Smoke test passes in-game: HIGH (v5f hook layer already validated; v6 inherits it)
- Qualification passes in-game: MEDIUM-HIGH (uncertainty in field-offset mapping;
  the test is designed to discover it)
- Discovery finds parry-active flag: MEDIUM (Codex prior 75-85%; lower for
  finding it in just 1 hr of capture)
- MVP audio-only ships: HIGH (path A is fallback regardless)
