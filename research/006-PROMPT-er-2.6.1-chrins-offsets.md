# Deep Research Prompt: ER 2.6.1 ChrIns Field Offset Investigation

**Created:** 2026-05-11 (end of session)
**Status:** Ready to dispatch — Claude deep-research + Codex parallel
**Fixture:** `/home/joshua.blattner/claude/elden-ring/data/research-fixture/`
**Source capture:** `/tmp/q2.bin` (89 MB, also at `/mnt/station-projects/elden-ring/logs/qualification-20260511-133002.bin`)

---

## Research Objective

Find the correct memory offsets within the ER 2.6.1 `ChrIns` struct (and its
sub-structs) for THREE specific fields whose offsets in the current
`parry-tell-probe` v6.1.1 are stale and produce wrong values:

1. **World position (x, y, z)** — currently read at `chrIns + 0x6C0` returns
   small numeric values that look like motion deltas or quaternion components,
   not world coordinates. Real position appears to live near `chrIns + 0x80`
   (value `(80.82, -97.56, -57.73)` for a focused Godrick Knight in a captured
   sample, which is consistent with Stormveil Gatefront-area world coords).
   But the probe-reported `player_pos` for the same sample is
   `(32.42, 106.60, 111.12)` — neither 0x6C0 nor 0x80 — so 0x80 might be
   skeleton root, not gameplay position. Need to identify the canonical
   gameplay-position offset for ER 2.6.1.

2. **Active animation ID for enemies** — currently read at `chrIns →
   module_bag (+0x190) → time_act_module (+0x18) → +0xD0`. For the PLAYER
   this returns valid animation IDs (e.g. 2019001, 2022100, etc.). For
   ENEMIES this always returns 0 across 1450 sampled rows of a focused
   Godrick Knight that was demonstrably animating during capture. The
   `time_act_module` payload was captured; bytes at 0x20-0x2C are
   `NaN/0.0/0.0/1.0` and 0xD0 is 0. This suggests the active anim_id for
   enemies lives in a CHILD TimeAct struct one level deeper, not the
   module itself. Eight `time_act_child` regions (256 bytes each) are
   captured per enemy — one of them likely contains the active anim_id.
   Need to identify which child + what offset within that child holds the
   enemy's currently-playing animation ID.

3. **Player lock-on target handle** — currently read at `PlayerIns + 0x6A0`
   returns a pointer-shaped value (`0x7FF3073CBB60`) instead of a 64-bit
   handle integer with the FromSoft handle structure
   (`<class-prefix><instance-index>`, e.g. `0x2001000014A00012`). The
   value is constant across an entire session including when the player
   was locked onto different entities. Need correct offset for the live
   lock-on target handle (or, alternatively, identify that 0x6A0 IS a
   pointer to a target struct that needs one more dereference).

---

## Context and Scope

- **Game:** Elden Ring base game v2.6.1.0 (NO Shadow of the Erdtree DLC
  installed). Binary at `C:\Program Files (x86)\Steam\steamapps\common\
  ELDEN RING\Game\eldenring.exe`. The build_date string captured in the
  probe manifest is `2026-05-11 12:29:53` and `er_file_version=2.6.1.0`.
- **Project:** parry-tell-probe — read-only client-side mod that hooks
  ChrIns to capture animation state for offline parry-window analysis.
  Co-op-safe (no memory writes, no `regulation.bin` touches).
- **Fixture available:** the 89 MB capture and the trimmed 248 KB
  region-payload extracts contain the FULL byte-for-byte memory of a
  focused enemy chr_ins at three timestamps ~20 seconds apart. The
  research can verify any proposed offset against these payloads without
  needing more live captures.
- **Probe source:** `/home/joshua.blattner/claude/elden-ring/probe/probe.cpp`
  is the working v6.1.1 with stale offsets. The `Off::` namespace at the
  top of the file contains the existing offset table.

## Investigation Depth

### Primary questions

1. **For each of the three offsets above, what is the correct value for
   ER 2.6.1 patched binary?** Provide the offset as a decimal or hex
   integer, the struct base it's relative to (`ChrIns`, `PlayerIns`,
   `TimeActModule`, `TimeActChild`, etc.), and the data type at that
   offset.

2. **What is the source-of-truth for each offset answer?** Acceptable
   sources, in descending order of trust:
   - DSMapStudio source code (Cethleann, FBXImporter, related repos)
     where ER struct layouts live
   - Hexinton / hexinton-helper-mod source
   - Erd-Tools / tarnishedtool / posturebarmod published offset tables
     (many are in our existing archaeology/ folder; check those first)
   - TGA Cheat Table for current ER patch
   - Soulsmodding wiki / SoulsTemplate
   - Recent (2025-2026) reverse-engineering blog posts or YouTube
     tutorials that explicitly target ER 2.6.x
   - Verification against the fixture bytes (scan-based)

3. **For the enemy anim_id specifically:** which of the 8 `time_act_child`
   regions in the fixture contains the active animation? The fixture's
   `anim_id_search_targets.json` lists 335 known c4380 animation IDs in
   both full (`prefix*1_000_000+suffix`) and short (`suffix only`) encoding.
   Scan all 8 child payloads for any u32 value matching these IDs. Report
   the (child_index, byte_offset, value, encoding) tuple for any hit.
   If hits exist across multiple samples at the SAME (child_index,
   offset), that offset is the answer.

### Secondary considerations

- Has the `ChrIns` struct shifted between ER patches in a way that would
  explain why the probe's offsets (likely captured against an earlier
  patch like 1.10 or 1.12) are now wrong? If so, what is the patch-level
  delta and is there a published "current offsets for 2.6.1" table?
- Are there alternative paths to the same data? For example, can the
  player's lock-on target be retrieved from `CSFEManager` (already
  successfully probed at `cs_fe_man_ptr_addr=0x00007FF65EDBB880` in the
  fixture) instead of `PlayerIns + 0x6A0`? The probe already reads
  boss-bar handles from CSFEManager — same field family might exist for
  lock-on.
- For the enemy anim_id: is there a `CharacterCtrl` or `AnimController`
  pointer on ChrIns that gives a more direct path than walking module_bag
  → time_act?

### Explicitly exclude

- Active offsets work in Steam (Easy Anti-Cheat). Do NOT propose
  techniques requiring EAC bypass or kernel modules. The probe runs
  client-side with EAC disabled (Seamless Coop offline mode).
- Do NOT propose memory writes. ChrIns is READ ONLY for this project.
- Do NOT propose patching regulation.bin or any game asset. The mod is
  pure read-only memory inspection.
- Out of scope: PvP-relevant cheats, item duplication, anything that
  would violate co-op safety. The mod's whole point is competitive
  parry-timing info; it must not modify game state.

## Evidence Standards

- **Source types:** open-source modding community repos (preferred),
  reverse-engineering blogs, soulsmodding wiki, cheat-table author notes,
  YouTube tutorials with code shown. Closed-source tools' published
  offset tables OK.
- **Recency:** 2025 onwards is ideal (ER 2.6.x era). 2024 may still apply
  if the struct hasn't shifted. Pre-2024 offset tables likely stale;
  cite them only with a "may need re-verification" caveat.
- **Verification standard:** every proposed offset MUST be verified
  against the fixture payloads before being accepted as the answer.
  An offset that LOOKS plausible from a 2023 wiki page but doesn't match
  the fixture's byte values is NOT the answer — keep looking.
- **Citations:** include direct URLs to source repos / files with the
  specific line numbers where the offset is defined. For wiki citations,
  the version/edit timestamp. For Cheat Engine table sources, the table
  filename + author.

## Analysis Framework

For each of the three offset questions:

1. **Survey:** find 2-3 independent published offset tables / source-code
   references for the field in question.
2. **Cross-check:** do the references agree? If yes, use that offset as
   the candidate. If no, note the disagreement and propose the most
   recent / most-cited candidate.
3. **Fixture verify:** open the relevant region payload in the
   `data/research-fixture/` directory and read the byte at the proposed
   offset. Does the resulting value make semantic sense (e.g. for
   position, are the three floats reasonable world-coord magnitudes; for
   anim_id, does it match one of the 335 c4380 anim IDs)?
4. **Multi-sample verify:** check the SAME offset across the three
   sample timestamps (early/mid/late, ~20 seconds apart). Position
   should change slightly (the Knight may have moved). Anim_id should
   change across at least 2 of 3 samples (any 20-second window covers
   multiple animation transitions). Constant values across 20 seconds
   of fighting → wrong offset.
5. **Report:** offset, data type, struct base, verification evidence,
   source citations.

## Output Structure

### Required sections

1. **Executive summary** — three offset answers in one table:

   | Field | Struct base | Offset | Data type | Source citation | Fixture verified? |
   |-------|-------------|--------|-----------|-----------------|-------------------|

2. **Per-offset detailed analysis** for each of the three:
   - Background: why this offset matters
   - Sources surveyed (with URLs / file paths / line numbers)
   - Candidate offsets considered
   - Verification against fixture (which sample, which region payload,
     bytes at offset, resulting value, why it makes sense)
   - Final answer with confidence level (high / medium / low)
   - If low confidence: what additional data would raise it

3. **Probe patch diff** — produce a unified diff against
   `probe/probe.cpp` that applies the three offset corrections. This is
   a deliverable, not a suggestion — the diff should be ready to `git
   apply --check`.

4. **Open issues** — any related struct fields that look suspicious or
   should be re-verified in a future session (e.g., if you find that the
   probe's `TIME_ACT_TIME_CAND_0..3` are also wrong, list them but
   don't fix unless the user opens a follow-up).

5. **Confidence levels and limitations**
   - For each answer: high / medium / low confidence
   - For low-confidence answers: what would need to happen to raise it

6. **Further research needed** — anything blocked on data we don't have
   (e.g., "anim_id offset is one of these two candidates; can't
   disambiguate without a second capture of a known-anim-ID enemy state")

### Format requirements

- Markdown with explicit code blocks for any binary offsets or struct
  definitions
- Hex offsets formatted as `0xNNN` (uppercase)
- Direct quotations from source files should include file path + line
  number
- The probe patch diff in a fenced `diff` code block, ready to apply

## Quality Instructions

### Reasoning approach

- For each offset, build from foundational evidence (published struct
  layouts) to specific verification (fixture bytes). Don't propose an
  offset that only "looks right" from a source — always confirm against
  the fixture.
- When sources disagree on an offset, treat the most-recent and the
  most-cited as the strongest candidates and verify BOTH against the
  fixture before picking a winner.
- Distinguish "this offset is documented at X" from "this offset
  empirically reads valid data in our fixture." Only the latter is
  proof.

### Critical evaluation

- An offset is NOT correct if it reads zero or NaN in our fixture when
  the field should be live (e.g., enemy anim_id during active combat).
- An offset is NOT correct if it reads constant values across three
  fixture samples 20 seconds apart when the field should be changing.
- The published offset MUST work for ER 2.6.1 specifically. If a 2024
  source predates 2.6.0, mark as "may need re-verification" until
  fixture-verified.
- Be explicit about confidence levels. Low confidence is fine and useful;
  false high confidence is harmful.

## Parallel-track suggestion for Codex

This same research prompt can run in parallel via the Codex MCP tool
(`mcp_Codex_codex` or `mcp_Codex_websearch`). Codex is faster on
code-spelunking tasks (DSMapStudio source, Cethleann repos). Suggested
split:

- **Claude deep-research:** general survey across modding wiki, blogs,
  YouTube, soulsmodding forums. Wider net.
- **Codex websearch + codex consult:** focused on open-source repo
  spelunking (DSMapStudio, hexinton, tarnishedtool, erd-tools) for
  authoritative struct definitions in code.

Both tracks should produce candidates which I (Claude) then cross-verify
against the fixture in `data/research-fixture/`. Two-vendor agreement on
an offset that ALSO verifies in the fixture → confidence high.
