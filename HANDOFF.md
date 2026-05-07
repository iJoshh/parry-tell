# parry-tell — HANDOFF

**Last session:** 2026-05-07 ~12:00 CT, through compact before TAE extraction.
**Status:** Phase 3 plan locked. Josh approved layered ship strategy.
EXTRACTION-PLAN refreshed for SMB workflow. SESSION-ASKS playbook written
for all upcoming Phase 3 sessions. **Josh is ready to start extraction.**

## Where we are

- Probe v5f works (HEAD before this session's docs: `95c3e25`).
- PHASE3-PLAN.md drafted, two adversarial review passes (CEO + eng) baked
  in. Josh approved the layered ship strategy: MVP at week 1, full v1 at
  week 4. Confidence 80% MVP / 70% full v1 / 95% v1 eventually. Josh said
  he doesn't think it'll take 4 weeks but is fine breaking it into 4 parts.
- HEAD (post-this-session-docs): `a76807c`, all pushed to GitHub.

## Immediate next action

**Josh starts TAE extraction.** Doc: `EXTRACTION-PLAN.md`. Workflow:
1. Disable probe v5f DLL (rename to `.disabled`)
2. UXM unpack ER (~30 min wall, 5 min active)
3. WitchyBND batch-extract `c*.anibnd.dcx` (~30-45 min wall, 20 min active)
4. Zip the folder, drop at
   `C:\Projects\elden-ring\extracted\parry-tell-extraction-2026-05-XX.zip`
5. Tell Claude "done with extraction"
6. Claude reads via SMB at `/mnt/station-projects/elden-ring/extracted/`,
   parses, runs sentinel-fixture validation, messages "safe to file-verify"
7. Josh runs Steam file-verify to restore vanilla
8. Josh re-enables probe v5f if desired

## Critical correction noted this session

**Evergaols DO NOT block spirit summons under Seamless Co-op.** I had it
backwards in the first draft of SESSION-ASKS.md (assumed vanilla rules).
Josh corrected: Seamless removes the vanilla "no summons in evergaols"
restriction, so Mimic Tear works at Stormhill Evergaol Crucible Knight.
Stormhill Evergaol is the recommended boss for ALL FOUR Phase 3.1.A-D
scenarios; only need to switch to Tree Sentinel if testing outside
Seamless. Already fixed in SESSION-ASKS.md.

## What needs to happen post-compact

When Claude resumes (after Josh's compact):

1. `mount | grep station` — confirm SMB mounts live
2. `ls /mnt/station-projects/elden-ring/extracted/` — check if zip arrived
3. If zip not there yet: tell Josh "ready when you start extraction"
4. If zip IS there:
   - Read `EXTRACTION-PLAN.md` Phase D for the parsing checklist
   - Unzip locally (`/tmp/parry-extraction/` is fine)
   - Walk every `*.tae.xml` file
   - Build `data/parry_data.json` with `_meta` block:
     ```json
     "_meta": {
       "game_version": "2.6.1.0",
       "extracted_at": "<from zip mtime>",
       "parser_version": "1.0.0",
       "extraction_method": "UXM + WitchyBND",
       "archive_sha256": { ... }
     }
     ```
   - Run sentinel-fixture validation: pick a known-stable animation ID
     (Crucible Knight idle stance), confirm it appears in extracted data
     with plausible frame data
   - If sentinel passes: message Josh "extraction parsed clean, safe to
     file-verify"
   - If sentinel fails: walk the 3-step remediation branch from
     PHASE3-PLAN.md Phase 3.0 (version check, archive hash check,
     escalate to soulstruct if format drift)

## Key file map

- `PHASE3-PLAN.md` — 536-line build plan, status: draft, awaiting Josh
  signoff (Josh has approved verbally; status flip is the ceremonial
  acknowledgment that triggers Phase 3.0 start)
- `EXTRACTION-PLAN.md` — refreshed for SMB workflow, ready to follow
- `SESSION-ASKS.md` — pre-batched playbook for all 4 upcoming sessions
  (extraction, probe v6, co-op, hue smoke test)
- `research/phase3-{architecture,offsets,ceo-review,eng-review}-codex.md`
  — research artifacts that fed the plan
- `probe/probe.cpp` — v5f source (HEAD `95c3e25`)
- `/mnt/station-mods/parry-tell-probe.dll` — installed v5f, currently
  active. Josh will rename to `.disabled` before extraction.

## What survives across compact

All the workflow rules from the prior HANDOFF.md still apply. Specifically:

- **Build chain:** scp source → ssh MSBuild → SMB read DLL → cp to mods.
  Toolset v145.
- **SSH service is manual-start.** Test before assuming.
- **No PostureBarMod conflict.** Josh doesn't run it.
- **Codex MCP timeout** is shorter than Codex's actual runtime.
- **Critic plugin auto-fires** after every Write/Edit. Verdicts append to
  tool result. Address before final summary per global protocol.
- **Decision: same-attack replay handled via animTime-rewind detection,**
  not animId change alone. State machine is handle-keyed not slot-keyed.
- **Animation time read confidence is medium, not high.** First-try
  +0x24; fallback is TarnishedTool's lock-target code-cave hook (NOT
  raw frame counting — eng review rejected that as not FPS-stable).

## Open questions Josh hasn't answered yet

(Not blocking extraction; can answer anytime)

1. Co-op friend availability for Session 3.B (target validation, after
   L1 ships) — Josh said "I'll play with friends again over the next
   couple nights" so this should resolve naturally
2. Session 3.A (Seamless guest slot probe with v5f) — bonus mode, Josh
   can run during any co-op session this week, ~10 min extra

## Confidence reminder

- MVP audio-only by 2026-05-14: 80%
- Full v1 by 2026-06-04: 70%
- v1 ships eventually: 95%

Josh said he expects faster than 4 weeks; not worth re-quoting upward
without data. Layered ship gives him visible progress weekly regardless.
