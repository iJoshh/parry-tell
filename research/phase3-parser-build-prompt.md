# TASK: Build the TAE parser for parry-tell.dll

## Context

You completed the semantic investigation (full report at
`/home/joshua.blattner/claude/elden-ring/research/phase3-tae-investigation-codex.md`).
The verdict locked HIGH-confidence: `ChrActionFlag FlagType=5` is the
"boss is parryable right now" window, AttackBehavior is the damage
window, lock-on/targeting is runtime-only.

This task: build the actual parser that transforms the 64,385 anim XMLs
into `data/parry_data.json`, the static database the mod reads at
startup.

## Inputs

- **Source data:** `/mnt/station-projects/elden-ring/chr-extracted/` —
  807 character directories. Two layout variants:
    - **Nested:** `cNNNN-anibnd-dcx-wanibnd/INTERROOT_win64/chr/cNNNN/tae/cNNNN-tae/anim-*.xml`
      and player-style `cNNNN-anibnd-dcx-wanibnd/INTERROOT_win64/chr/cNNNN/tae/aNN-tae/anim-*.xml`
    - **Flat:** `cNNNN-anibnd-dcx-wanibnd/tae/cNNNN-tae/anim-*.xml`
  Recursive glob `**/anim-*.xml` finds all of them. Total: 64,385
  files, 2.51 GB. Some character dirs legitimately contain zero
  anim-*.xml files (auxiliary skeleton/blendshape binders) — that is
  expected, not an error.

- **TAE event-type catalog:**
  `/mnt/station-projects/tools/WitchyBND/Assets/Templates/TAE.Template.ER.xml`
  (authoritative ER event catalog, ~1740 lines).

- **Sample fixtures (committed to repo):**
  `/home/joshua.blattner/claude/elden-ring/data/sample-fixtures/` — 3
  files for unit testing:
    - `c4100-anim-000003000.xml` — canonical positive case. Has 1
      FlagType=5 event spanning 0.6333333s -> 0.7s, and 1
      AttackBehavior at 0.6333333s -> 0.73333335s with
      `BehaviorJudgeID=110, AttackType=0`.
    - `c4100-anim-000000020.xml` — negative case. Boss anim, zero
      FlagType=5.
    - `c0000-anim-000000.xml` — negative case. Player anim, zero
      FlagType=5.

## Output

Write the parser to `/home/joshua.blattner/claude/elden-ring/tools/parse_taes.py`.
Write the database to `/home/joshua.blattner/claude/elden-ring/data/parry_data.json`.
Write a one-page summary to `/home/joshua.blattner/claude/elden-ring/data/parry_data_summary.md`.

## What to extract per anim XML

For every `anim-*.xml` recursively under the source directory:

1. Determine `character_id` from the parent character directory name
   (extract `cNNNN` from `cNNNN-anibnd-dcx-wanibnd`, OR from suffixed
   variants like `cNNNN_aNX-anibnd-dcx-wanibnd` — keep the suffix in the
   character_id so `c0000_a00_hi` does not collide with `c0000`).
2. Read the anim's `<name>` field — strip the `.hkt` suffix to get
   `animation_id` (e.g. `a000_003000`).
3. For each `<event>` in the file:
    - **Parry windows:** if `<type>0</type>` and any
      `<param name="FlagType" value="5">` — record `start_time`,
      `end_time`, derived frame ranges at 30fps and 60fps (rounded to
      nearest int).
    - **Attack behaviors (melee):** if `<type>1</type>` — record
      `start_time`, `end_time`, `AttackType` (int), `AttackIndex`
      (int), `BehaviorJudgeID` (int), `DirectionType` (int), `Source`
      (int), `StateInfo` (int).
    - **Bullet behaviors (projectile/breath):** if `<type>2</type>` —
      record `start_time`, `end_time`, `DummyPolyID` (int),
      `AttackIndex` (int), `BehaviorJudgeID` (int), `AttachmentType`
      (int), `Enable` (bool), `Source` (int), `StateInfo` (int).
    - **Other ChrActionFlag values for future-proofing:** if
      `<type>0</type>` and `FlagType` is any of:
      `24, 49, 55, 63, 71, 73, 78, 79, 86, 94, 102, 119, 132, 143` —
      record `flag_type` (int), `start_time`, `end_time`. Group these
      under a single `chr_action_flags` array per animation; do NOT
      promote them into named keys. We are extracting them so the
      database is L1/L2/v1-ready, but MVP will not consume them.

If a param is absent in the XML (some events skip optional fields),
omit the key from the JSON object (do not write null). Coerce numeric
strings with `int()` for integer fields, `float()` for `start_time` /
`end_time`. Coerce `Enable="True"` / `"False"` to JSON `true` / `false`.

## Output schema — `parry_data.json`

```json
{
  "_meta": {
    "schema_version": "1.0.0",
    "parser_version": "1.0.0",
    "extracted_at": "<ISO8601 UTC>",
    "tae_template_source": "WitchyBND v3.0.0.1 / TAE.Template.ER.xml",
    "game_version_marker": "Elden Ring 1.16 + SOTE (Steam install Game/eldenring.exe mtime 2025-08-21)",
    "extraction_method": "UXM Selective Unpack 2.4.2 + WitchyBND v3.0.0.1",
    "fps_assumption": 30,
    "extraction_rules": {
      "parry_window_event_type": 0,
      "parry_window_flag_type": 5,
      "attack_behavior_event_type": 1,
      "bullet_behavior_event_type": 2,
      "chr_action_flag_extracted_values": [5, 24, 49, 55, 63, 71, 73, 78, 79, 86, 94, 102, 119, 132, 143]
    },
    "totals": {
      "characters": <int>,
      "characters_with_parry_data": <int>,
      "animations_scanned": <int>,
      "animations_with_parry_windows": <int>,
      "parry_windows": <int>,
      "attack_behaviors": <int>,
      "bullet_behaviors": <int>,
      "chr_action_flag_events": <int>,
      "parse_failures": <int>
    }
  },
  "characters": {
    "c4100": {
      "animations": {
        "a000_003000": {
          "parry_windows": [
            { "start_time": 0.6333333, "end_time": 0.7, "frame_30": [19, 21], "frame_60": [38, 42] }
          ],
          "attack_behaviors": [
            { "start_time": 0.6333333, "end_time": 0.73333335,
              "attack_type": 0, "attack_index": <int>, "behavior_judge_id": 110,
              "direction_type": <int>, "source": <int>, "state_info": <int> }
          ],
          "bullet_behaviors": [],
          "chr_action_flags": [
            { "flag_type": 24, "start_time": 0.5, "end_time": 0.8 }
          ]
        }
      }
    }
  }
}
```

Empty arrays are fine (`"bullet_behaviors": []`). If an animation has
no events of any tracked type, OMIT the animation from
`characters[cNNNN].animations` entirely — do not record empty husks. If
a character has zero animations with any tracked events, OMIT the
character from `characters` entirely.

## Determinism + sort order

- Sort `characters` keys alphabetically.
- Sort `animations` keys alphabetically within each character.
- Sort all event arrays by `start_time` ascending. Ties: keep insertion
  order (first occurrence in the source XML wins).
- JSON output: `indent=2`, no trailing whitespace, UTF-8.

## Workflow

### Phase 1 — Sentinel gate (DO FIRST)

Run the parser on JUST the three sample fixtures at
`/home/joshua.blattner/claude/elden-ring/data/sample-fixtures/`. The
parser's output for those three files MUST satisfy:

- `c4100-anim-000003000.xml` -> 1 parry window
  (start=0.6333333, end=0.7) AND 1 attack_behavior
  (start=0.6333333, end=0.73333335, behavior_judge_id=110)
- `c4100-anim-000000020.xml` -> 0 parry windows
- `c0000-anim-000000.xml` -> 0 parry windows

If any of those checks fail, STOP, fix the parser, and retry. Do not
proceed to Phase 2 until the sentinel is green.

### Phase 2 — Single-character sentinel

Run the parser on the full `c4100-anibnd-dcx-wanibnd/` directory. It
should report:
- 28 animations with FlagType=5 events
- 31 distinct parry-window events total
- All parry windows ~0.0667s long (2 frames @ 30fps), within rounding

If those numbers diverge, STOP and report.

### Phase 3 — Full corpus parse

Run on all 807 character directories. Expected wall time: ~60 seconds
(stdlib parser benchmarked at ~1800 anim/sec on local disk).

NOTE: The data lives on an SMB share. Direct recursive parsing over
SMB is much slower (~10x) than parsing local data, but should still
complete in 5-10 minutes. If you find SMB parse perf is unacceptable,
the OPTIONAL fallback is to copy the entire `chr-extracted/` tree
locally to `/tmp/chr-extracted/` first (it's 2.51 GB, fits in /tmp).
Use the SMB path first; only fall back if SMB is too slow.

### Phase 4 — Summary

Write `data/parry_data_summary.md` with:
- Total characters / total parry windows / total attack behaviors /
  total bullet behaviors
- Top 20 characters by parry-window count
- Top 5 anomalies (parse failures, unusual parry-window durations,
  character dirs with zero anim XMLs but the directory exists)
- Wall-clock time for full parse

## Constraints

- **Pure stdlib.** Use `xml.etree.ElementTree`. No external deps. No
  `lxml`, no `BeautifulSoup`.
- **Python 3.10+.** Match the system Python.
- **ASCII-only source.** No em-dashes, no curly quotes, no unicode in
  the parser script. UTF-8 in JSON output is fine.
- **MIT-compatible.** This script ships in the repo.
- **Single file.** Everything in `tools/parse_taes.py`. No package
  layout.
- **Idempotent.** Running twice produces identical output bytes.
- **Determinism guaranteed.** Sorts above + ISO8601 timestamp captured
  ONCE at start of run + recorded in `_meta.extracted_at`.

## CLI

The script should accept:
```
python3 tools/parse_taes.py [--source DIR] [--output FILE] [--summary FILE] [--sentinel-only] [--single-char cNNNN]
```

Defaults:
- `--source /mnt/station-projects/elden-ring/chr-extracted`
- `--output  data/parry_data.json`
- `--summary data/parry_data_summary.md`
- No `--sentinel-only` flag = full run.
- `--sentinel-only` runs only Phase 1 (the 3-fixture check) and exits
  with code 0/1 based on pass/fail. Useful for CI.
- `--single-char cNNNN` runs Phase 2-style on one character only and
  writes a partial JSON file.

## Deliverables

1. `tools/parse_taes.py` — the parser script (committed to repo).
2. `data/parry_data.json` — the database (committed to repo).
3. `data/parry_data_summary.md` — the summary report (committed to
   repo).
4. A short final stdout summary listing what landed and any anomalies
   that need human attention.

## Final note

The repo's git workflow uses Conventional Commits. Don't commit; we
will. Just write the files.
