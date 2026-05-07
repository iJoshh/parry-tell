# parry-tell — HANDOFF

**Last session:** 2026-05-06 evening through 2026-05-07 ~midnight CT
**Status:** Probe v4 confirmed to cause game crashes. Probe disabled on Windows side. Major workflow infrastructure built (Tailscale + SMB + SSH+MSBuild). Next session: design v5 from minimal-invasion posture.

## Where we are

**Game-side state on station (Windows dev box):**
- `parry-tell-probe.dll` is **renamed to `.old`** in mods folder. Game runs normally without our probe.
- Confirmed by Josh: zero crashes since rename. Was crashing 5x in 73 min with probe loaded. Root cause (mechanism) unknown but field evidence is dispositive.

**Build infrastructure (NEW this session):**
- Tailscale mesh: VM (`codeserver-vm`) + Windows (`station`) on tailnet `ijoshh.github`
- SMB shares: `Projects` (RO at `/mnt/station-projects/`), `mods` (RW at `/mnt/station-mods/`)
- SSH access: VM → station as `claude` user, key auth only, Tailscale-scoped firewall, **MANUAL service start** (Josh starts before sessions, stops after)
- MSBuild path on station: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`
- VM-side minidump tooling installed: `~/.cargo/bin/minidump-stackwalk` and `~/.cargo/bin/dump_syms` (both via cargo, current Rust 1.95)

**Project state on VM:**
- All work committed to git, pushed to private GitHub at https://github.com/iJoshh/parry-tell
- Latest commit: `5072b42 feat: probe v4 — fix stale ChrIns offsets for ER 2.6.1.0`
- Probe runs/ folders: `run-2-2026-05-06-1815-CT/` (v2 plumbing test), `run-3-2026-05-06-2255-CT-v4-CRASH/` (v4 crash run)
- Releases/ folder: probe-v2-patched.tar.gz, probe-v3.tar.gz, probe-v4.tar.gz

## Major findings this session

### Offset bugs in PROBE-SPEC.md

Three CHR_INS offsets were stale (correct for ER ≤1.6.0, wrong for 2.6.1.0):
- `CHR_INS_ENTITY_ID`: should be `0x1E8`, was `0x80`
- `CHR_INS_BLOCK_ID`: should be `0x38`, was `0x6C`
- `CHR_INS_CHR_TYPE`: should be `0x64`, was `0x68`

Verified against TarnishedTool's version-switched offset table for `Version2_6_1`. v4 source has the corrected values.

### `WCM + 0x1E508` may not be the player slot for ER 2.6.1.0

Even with v4's correct CHR_INS_ENTITY_ID, all 20 successful reads at `chr+0x1E8` returned `entity=0x00000000`. Either:
- `WCM + 0x1E508` returns something OTHER than player ChrIns (overlapping field, internal struct pointer)
- Player slot needs an additional dereference layer
- The offset is right but the player entity ID truly is zero in this state (unlikely)

WCM struct memory dump (131KB) preserved at `probe/runs/run-3-2026-05-06-2255-CT-v4-CRASH/parry-tell-probe-wcm-dump.bin` for offline static analysis. **Analyzing this offline is the path to finding the right offset without putting the probe in the game.**

### Probe-induced crashes

5 crashes in 73 min vs baseline of 1-2 per 5 hours. Crash signatures: `STATUS_HEAP_CORRUPTION` and `FAST_FAIL_INVALID_REFERENCE_COUNT`. Crash dumps live on station at `C:\Users\Josh\AppData\Local\CrashDumps\`. All analyzed in this session via minidump-stackwalk + Microsoft symbol server + dump_syms-converted PDB symbols.

**Suspected mechanism (not confirmed):** v4's WCM memory dump (131072 bytes read at probe init) plus aggressive prio queue walk (changed `return std::nullopt` to `continue`, multiplying reads per frame by 10-100x) likely reads memory at offsets that overlap with kernel-managed pages or heap free-list metadata. SEH wraps the reads but only catches HARD faults (unmapped page); valid-but-wrong reads succeed silently and return garbage that can corrupt state when used downstream.

## Critical mistake to avoid in future sessions

**I (Claude) had Josh's empirical answer ("crashes only with probe loaded, none after rename") and tried to argue against it using formal minidump analysis.** That was wrong. Josh's pattern recognition from 5 hours of normal play vs 73 min of crashes is stronger evidence than a stack trace that doesn't show probe symbols. Heap corruption is detected on the wrong thread by design — that's its whole signature.

If a future session gets data that contradicts Josh's lived experience, **default to Josh's empirical read** unless I have a mechanism-level explanation that's MORE specific than "no probe symbols on crash stack."

## Plan for next session — v5 from minimal-invasion posture

**Core insight:** the probe's job is to discover correct offsets. We don't need 60Hz polling for that. We need careful, bounded sampling.

### v5 design (proposed, not yet implemented)

1. **Hotkey-triggered sampling.** Bind F11 (or similar). Player presses → probe samples once, writes ~10 lines to CSV, idles. Default behavior is dormant.
2. **Drop the WCM dump entirely.** Was added in v4 specifically to enable offline analysis — but Josh's existing run-3 dump.bin already provides that data. We don't need to re-dump every game launch.
3. **Drop the prio queue walk for now.** Until we've nailed the player chain, we shouldn't be aggressively walking other game state.
4. **Single deref per hotkey press.** Read WCM. Read player slot. Read entity_id at +0x1E8. Log. Done. Maybe 5-10 reads total per press.
5. **Audible/visual feedback at probe init** so Josh knows the probe is alive. DebugView banner is enough; no in-game UI.
6. **Hard frame budget.** If sampling takes >1ms, abort. We never block the game thread.

### v5 rollout plan

1. Claude writes v5 source at `~/claude/elden-ring/probe/probe.cpp` with above constraints
2. Claude dispatches Codex on adversarial review (writer-pairing rule per global memory)
3. Claude commits + tarballs as `probe/releases/probe-v5.tar.gz`
4. Claude SSHes to station, builds via MSBuild
5. **Josh starts a fresh test session ONLY when he's ready** (no time pressure)
6. Josh launches game, presses hotkey ~5 times during normal play, plays for 15-30 min
7. **If game crashes within first 20 min:** stop, rename .dll back to .old, debrief
8. **If game runs cleanly:** read CSV (will have ~5 sample blocks), see if entity_id at offset 0x1E8 is finally non-zero, decide next move

### Offline work that doesn't require game launches

The 131KB WCM memory dump from run-3 is sitting on the VM. We can:
- Read it as raw bytes
- Look for 64-bit values that look like heap pointers (high prefix `0x00007FF3...` or `0x00007FF4...`) at various offsets
- Cross-reference with PostureBarMod's struct definitions (`archaeology-sources/posturebarmod/Source/Main/Hooking.hpp` shows `playerArray[0x4]` at `0x10EF8`)
- Find the offset that matches a known-good player-chr-ins-shaped value
- THEN ship a v6 with the corrected offset

This is real progress that doesn't risk crashes.

## Suggested next-session opening

1. Read this HANDOFF
2. Read `probe/runs/run-3-2026-05-06-2255-CT-v4-CRASH/README.md`
3. Confirm Tailscale + SMB are still up: `mount | grep station`
4. Confirm SSH service status with Josh (it's set to manual; he may have stopped it)
5. **Start with offline WCM dump analysis** before considering any in-game test
6. Discuss v5 design with Josh, get explicit signoff on the minimal-invasion approach, THEN write code

## Open questions for Josh

1. Is keeping our DLL renamed `.old` indefinitely OK while we plan v5? (Yes, almost certainly)
2. Want to leave the GitHub repo private until v0.1.0 ships? (Discussed: yes, private for now)
3. After we get v1 working, do you want a public release blog post / Reddit post? (Not yet decided)

## Workflow notes

- Build channel via SSH+MSBuild works. Do NOT regress to email-tarball workflow.
- File access via SMB works. `/mnt/station-projects/` (RO), `/mnt/station-mods/` (RW for parry-tell-* files only).
- Audit trail: every Claude write to mods folder is logged in Windows Event Viewer under user `claude`.
- Kill switches for Josh are documented in email msg-id `4fb1ee91-2556-47f5-9693-b6d590cbaae8` (sent 2026-05-06 21:30 CT).
