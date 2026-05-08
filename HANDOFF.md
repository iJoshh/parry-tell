# parry-tell — HANDOFF

**Last session:** 2026-05-07, ~21:00 CT. Phase 3 Step 0 (TAE parser +
database) COMPLETE. Ready for Phase 3 Step 1 (probe v6 design + MVP audio
cue plumbing).

## Where we are

### What's locked and committed (this session)

- ✅ **TAE event-type investigation** — HIGH-confidence verdict that
  `ChrActionFlag FlagType=5` is the canonical "this character is
  parryable right now" marker. Two independent paths converged
  (Codex web research + my coexistence test). Full report:
  `research/phase3-tae-investigation-codex.md`.
- ✅ **Parser source** — `tools/parse_taes.py` (621 lines, stdlib only,
  workers=32 parallel, fixture+single-char sentinels, ASCII-only).
  Codex parser-build-001 wrote it; gpt-5.5, ~2h38min wall, 195K tokens.
- ✅ **Parry database** — `data/parry_data.json` (31 MB), 64,385 anims
  scanned, 0 parse failures, 6,738 parry windows / 25,014 attack
  behaviors / 10,608 bullet behaviors / 97,163 future-proofing
  ChrActionFlag events. 107 characters have parry data.
- ✅ **Sample fixtures** — `data/sample-fixtures/` (3 files, ~50 KB)
  for parser unit tests without needing the SMB mount.
- ✅ Repo restructure: research/ holds investigations, data/ holds
  the database + fixtures, tools/ holds the parser.
- ✅ All committed + pushed: latest `d43f0fb feat: TAE parser +
  parry_data.json database`.

### Notable findings worth remembering

1. **Player (c0000) DOES carry FlagType=5 events** — 4,116 of them.
   My earlier sampling-based "zero" check was wrong. Semantically:
   when the player does certain attacks (kicks, some weapon arts),
   those moves are parryable by enemies. Player anims correctly carry
   FlagType=5 on those moves. The semantic is symmetric.

2. **WitchyBND output layout varies by character.** Two patterns:
   - Nested: `cNNNN-anibnd-dcx-wanibnd/INTERROOT_win64/chr/cNNNN/tae/cNNNN-tae/anim-*.xml`
   - Flat: `cNNNN-anibnd-dcx-wanibnd/tae/cNNNN-tae/anim-*.xml`
   Recursive `**/anim-*.xml` glob handles both. The parser does this.

3. **520 character dirs have zero anim XMLs.** Expected — these are
   auxiliary skeleton/blendshape binders (`cNNNN_aXX_hi`, etc).
   `_witchy-anibnd4.xml` marker is the binder-success signal, not
   anim count.

4. **Multiple files often map to the same `(character_id, animation_id)`.**
   For c4100, 569 files condensed to 220 unique animation_ids
   (~2.6x). This is correct dedup behavior — the same logical anim
   exists in multiple LOD binders. The parser merges events instead
   of overwriting, so all 31 c4100 parry windows are preserved.

5. **5 anomalies in the data** — parry windows >0.9s (normal is
   0.0667s = 2 frames @ 30fps):
   - c3251 a000_003012: 1.7s window
   - c6251 a000_003012: 1.7s window
   - c4370 a001_003010: 1.07s window
   - c5650 a001_003010: 1.07s window
   - c3660 a001_003011: 0.9s window
   Likely intentional design (charged attacks have wider parry windows?).
   Not blocking. Worth a closer look during MVP audio-cue testing —
   if the cue feels off on those bosses specifically, this is the
   first place to check.

6. **gpt-5.5 (Codex) self-improves on long tasks.** The first
   parser run had no progress logging, went silent for ~30 min,
   Codex diagnosed his own visibility problem and rewrote the
   parser to add progress lines. Mature behavior. The current
   parser has progress logging built in and runs through
   discovery → workers in clearly-marked phases.

### Disk hygiene done this session

- Freed 96 GB by deleting two stale `/tmp/` files: an old tee'd
  opencode-tail.log (49 GB, 5 days stale) and a runaway
  opencode-telegram session scratch dir (49 GB, 11 days stale).
  VM disk now at 46% used, 106 GB free. Both deletions were
  verified safe before action.

### Workflow gotchas to remember (across sessions)

- **SMB perf is brutal.** 51 min for a full corpus parse over the
  station Projects share. Per-file metadata roundtrips dominate
  (~50ms/file). Local-mirror would be ~1 min total. The reason we
  haven't moved chr-extracted local: it's 2.5 GB and the SMB→local
  copy is ALSO bottlenecked by the same SMB latency (we measured
  ~5 GB/hour, so a full copy would take ~30 min even). Worth doing
  if we expect to re-run the parser; not worth doing if this
  parry_data.json is final.
- **opencode `Bash` tool truncation killed several investigation
  passes.** When a process tree contains the full Codex prompt as
  argv, `ps -ef --forest` and `pgrep -af` produce massive blobs
  (~200 KB each) that flood the context. Avoid those flags when
  Codex is running; use `ps -p PID -o pid,etime,stat`.
- **`codex exec` vs `codex-mcp-server`.** This session, we
  bypassed the npm-wrapper MCP and called `codex exec` directly
  via Bash with fire-and-forget. Way better — non-blocking, real
  exit codes, can dispatch and do parallel work. Pattern:
  `nohup codex exec -s workspace-write --skip-git-repo-check -C $PWD
  --output-last-message $RESULT_FILE "$(cat $PROMPT)" > $LOG 2>&1 &`
  See `research/phase3-tae-investigation-prompt.md` and
  `research/phase3-parser-build-prompt.md` for the dispatch shape.
- **opencode MCP timeout bumped from 10 min → 30 min** for future
  Codex MCP calls. `~/.config/opencode/opencode.json` line 124,
  `mcp.codex.timeout: 1800000`.

## Open questions / followups (not blocking)

1. **`.gitignore` parser:** the original .gitignore had patterns
   like `data/chr-extracted/` to exclude the 2.5 GB raw extraction.
   Still correct. We currently have NO local-mirror; if a future
   session does `cp -r /mnt/station-projects/elden-ring/chr-extracted
   ~/claude/elden-ring/data/raw/` for parser iteration speed, the
   gitignore protects against accidentally committing it.

2. **Use native `codex mcp-server` instead of community npm wrapper.**
   Currently in `~/.config/opencode/opencode.json`. Codex 0.128.0
   ships its own `mcp-server` subcommand. Migration is on the
   "should fix later" list — bumped to TODO in this session.

3. **Hyperarmor sentinel** — `FlagType=24` ("Super Armor"). MEDIUM
   confidence per investigation report. Need hands-on validation
   against a known stagger-resistant boss attack when L2 hue work
   begins. Database has 24-marker events extracted, just need to
   confirm the windows match in-game behavior.

4. **5 anomalies** — investigate when/if MVP cue testing shows
   weirdness on those specific bosses (c3251, c6251, c4370, c5650,
   c3660).

## Immediate next action

**Local chr/ copy in progress (Josh dragging from station to VM).**
Destination: `~/claude/elden-ring/data/raw/chr/`. Size ~6.5 GB. Includes
both raw `c*.anibnd.dcx` files AND the WitchyBND-unpacked
`*-anibnd-dcx-wanibnd/` directories (the latter is what the parser cares
about). Already gitignored.

Once the copy is done, **re-run the parser locally** to confirm:
```bash
cd ~/claude/elden-ring
# Find the right chr-extracted root inside data/raw/chr/ — likely:
python3 tools/parse_taes.py --source data/raw/chr/chr-extracted --workers 32
# Or with whatever the actual subdir name is. Sentinel-only first:
python3 tools/parse_taes.py --sentinel-only
```
Expected wall time: ~60 sec local vs the 51 min we paid over SMB.

This unblocks fast iteration if any parser bug needs fixing, AND
unblocks Phase 3 Step 1 work without needing the station online.

After local re-run validates the database is unchanged from
`data/parry_data.json` (it should — Codex's SMB output was sound), the
real next step is:

Phase 3 Step 1: probe v6 design + first MVP audio-cue plumbing.
Per PHASE3-PLAN.md and SESSION-ASKS.md, this means:

1. Probe v6 = 4 hotkey-armed sample groups for offset hunting
   (Phase 3.1 in PHASE3-PLAN). Validates the +0x24 animation-time
   chain that's currently MEDIUM-confidence.
2. After probe v6 proves the offsets, MVP DLL development can
   begin: read animation_id + animation_time at runtime, look up
   parry windows in `data/parry_data.json`, fire `PlaySoundW` cue
   when window opens.
3. Ship target for MVP: `v0.1.0-alpha`, audio-only, week 1 of
   layered ship strategy.

SESSION-ASKS.md Session 2 is the playbook for probe v6 design.

## Key file map

### Top-level docs (VM, `/home/joshua.blattner/claude/elden-ring/`)

- `CLAUDE.md` — project conventions, SMB workflow rules, safety boundaries
- `PHASE1-PLAN.md` — original product spec
- `PHASE3-PLAN.md` — production build plan (status: draft awaiting Josh signoff)
- `EXTRACTION-PLAN.md` — TAE extraction playbook (now complete)
- `SESSION-ASKS.md` — pre-batched playbook for upcoming sessions
- `HANDOFF.md` — this file

### Research (`research/`)

- `SYNTHESIS.md` — Phase 2 research synthesis
- `phase3-architecture-codex.md` — D3D12, audio, state machine
- `phase3-offsets-codex.md` — 5 offset chains with citations
- `phase3-ceo-review.md` — CEO/scope review
- `phase3-eng-review.md` — Eng correctness review
- **`phase3-tae-investigation-prompt.md`** — Codex semantic-investigation prompt
- **`phase3-tae-investigation-codex.md`** — verdict + Part B event-type roadmap
- **`phase3-parser-build-prompt.md`** — parser-build dispatch prompt

### Code

- `probe/probe.cpp` — v5f source (~860 lines), working, currently disabled
- `probe/probe.vcxproj` — v145 toolset
- `probe/vendor/MinHook/` — vendored MinHook (BSD-2)
- `probe/releases/probe-v{5d,5e,5f}.tar.gz` — release tarballs
- **`tools/parse_taes.py`** — TAE parser (621 lines, stdlib, workers=32)

### Data

- **`data/parry_data.json`** — 31 MB, 6,738 parry windows + 25,014
  attack behaviors + 10,608 bullet behaviors + 97,163 future-proofing
  ChrActionFlag events. Ships with the DLL.
- **`data/parry_data_summary.md`** — 1-page summary + top 20 + anomaly list
- **`data/sample-fixtures/`** — 3 small XMLs (~50 KB) for parser tests

### Source-of-truth raw data (Projects share, NOT in repo)

- `/mnt/station-projects/elden-ring/chr-extracted/` — 807 character
  dirs, 64,385 anim-*.xml files, 2.51 GB. Unpacked from
  `chr/c*.anibnd.dcx` via UXM Selective Unpack 2.4.2 + WitchyBND v3.0.0.1.
- `/mnt/station-projects/tools/` — UXM, WitchyBND with TAE.Template.ER.xml

## Confidence reminder

- MVP audio-only ship (v0.1.0-alpha) by 2026-05-14: **80%**
- Full v1 (lock-on + INI + Primary/Alert) by 2026-06-04: **70%**
- v1 ships eventually: **95%**

Phase 3 Step 0 is done in 2 sessions instead of the planned 4. We're
ahead of schedule.

## Next-session opening line (for post-resume me)

"Read this HANDOFF.md. Phase 3 Step 0 (TAE database) is done — the
parser + 31 MB parry_data.json are committed at d43f0fb. Next is
Phase 3 Step 1: probe v6 design + MVP audio-cue plumbing. Confirm
with Josh whether SSH service is started before doing any
station-side work."
