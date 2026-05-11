# Post-compact pickup: dispatch research on ER 2.6.1 offsets

**Purpose:** when Josh resumes after compaction, you should be able to launch
the research without re-discovering anything from this session.

## Immediate first move

Read in this order:
1. `HANDOFF.md` — current state of the project + three offset bugs identified
2. `research/006-PROMPT-er-2.6.1-chrins-offsets.md` — the research prompt to dispatch
3. `data/research-fixture/README.json` — what's in the fixture and what it means
4. (Optional context) `data/research-fixture/anim_id_search_targets.json` — 335 c4380 anim IDs

## What to do

**Dispatch BOTH in parallel, single message:**

### Track 1: Claude deep-research

Use the `deep-research` skill (already invokable via `/deep-research` or the
deep-research skill). Feed it the prompt from `research/006-PROMPT-er-2.6.1-chrins-offsets.md`
verbatim. Save artifact under `research/006-claude-deep-research.md`.

### Track 2: Codex parallel pass

Use the `mcp_Codex_codex` tool with `model: gpt-5.3-codex` and
`reasoningEffort: high`. Pass the same prompt with this preamble:

> "Running in parallel with Claude deep-research on this same question.
> Your focus: open-source repo spelunking for authoritative ChrIns struct
> definitions in DSMapStudio, Cethleann, FBXImporter, hexinton-helper-mod,
> tarnishedtool, erd-tools, and posturebarmod. Cross-verify any proposed
> offset against the fixture payloads at
> /home/joshua.blattner/claude/elden-ring/data/research-fixture/. Output
> in the same format the prompt specifies (table + per-offset analysis +
> patch diff + confidence levels)."

Save artifact under `research/006-codex-research.md`.

Both tracks have access to the same fixture. Both should produce a probe
patch diff.

### Track 3 (start immediately, run in background while 1+2 run)

Write a small offset-scan tool that does the brute-force anim-ID search
yourself — this is bounded work and unblocks bug #2 fast:

```python
# tools/scan_for_anim_ids.py
# For each time_act_child region payload in data/research-fixture/,
# scan all u32 values at every 4-byte-aligned offset and report any
# match against the c4380 anim ID list. If multiple samples have hits
# at the same (child-index, offset), that's the anim_id field.
```

This script can run in seconds. If it produces a clean answer for bug #2
(enemy anim_id offset), we may not need Track 1 or 2 to answer it — they
become confirmation rather than primary research.

## Synthesis after research lands

When both tracks complete:

1. **Compare answers.** Three possible patterns:
   - **Two-vendor agreement + fixture verified** → high-confidence answer,
     proceed to patch.
   - **Vendors disagree, only one verifies in fixture** → use the verifier.
   - **Neither verifies** → write a follow-up scan script (like Track 3
     but broader) and re-run with new candidates.

2. **Apply the patch diff** that one of them produced. Verify with
   `git apply --check probe/v6.1.1+offsets.patch` first.

3. **Rebuild + redeploy** via `tools/rebuild-and-stage.sh` (password auth
   via `tools/station-ssh.sh`, already wired up — just needs SSH service
   running on station).

4. **Verify against fixture BEFORE asking Josh to play again.** Re-run
   the analyzer on `/tmp/q2.bin` with the new offsets applied at parser
   level (not probe level — we don't have a re-capture). If the
   FIXTURE-LEVEL parser reads correct anim_ids using the new offsets,
   the probe rebuild is validated.

5. **THEN** ask Josh for a fresh live capture to confirm end-to-end.

## State at compact time

- v6.1.1 probe DLL deployed: `/mnt/station-mods/parry-tell-probe.dll`
  (229,888 bytes, built 13:19 CDT 2026-05-11)
- INI: qualification mode loaded
- SSH: password auth via `tools/station-ssh.sh`; sshd was UP at compact;
  Josh may have stopped it — verify with `station_ssh 'echo ok'`
- Local capture for analysis: `/tmp/q2.bin` (89 MB, may survive compact;
  if not, re-copy from `/mnt/station-projects/elden-ring/logs/qualification-20260511-133002.bin`
  using the SMB-copy-locally rule in `CLAUDE.md`)
- Research fixture: `data/research-fixture/` (committed, 248 KB)

## Reminders

- **Station password rotation** — Josh's station password was printed in
  cleartext during today's SSH debug (sudo cat + bash -x trace). Surface
  this at session-close.
- **SMB perf rule** — pinned in `CLAUDE.md`. Always copy big captures
  locally before parsing.
- **Conventional commits + checkpoints** — use `~/bin/checkpoint.sh
  "<repo>" "<subject>"` for proactive saves.
- **Chicago time** — all timestamps in CLAUDE-to-Josh communication use
  America/Chicago.

## Why the patient path

Today's session ran four rebuild-redeploy-replay cycles and ended without
qualification PASS. Each cycle was ~30 minutes of Josh's time. The
research-first approach is meant to find ALL three offset answers in
ONE offline cycle, then validate against the existing fixture without
Josh playing, THEN ship a single corrected probe. Estimate: 1 hour of
my research + 5 minutes of Josh playing once to confirm. Compare to:
4+ more rebuild-replay cycles at 30 min each.
