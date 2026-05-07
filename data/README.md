# `data/` — Static data for parry-tell.dll

This directory holds the static data the mod ships with at runtime.

## Layout

```
data/
├── README.md                # this file
├── parry_data.json          # (TBD) parser output, ships with the DLL
└── sample-fixtures/         # known-good anim XML samples used by parser tests
    ├── c0000-anim-000000.xml         # player anim, expect zero FlagType=5
    ├── c4100-anim-000000020.xml      # boss anim with NO FlagType=5 (negative case)
    └── c4100-anim-000003000.xml      # boss attack WITH FlagType=5 (canonical sentinel)
```

## `parry_data.json` (planned)

Parser output. Schema and `_meta` block are spec'd in
`PHASE3-PLAN.md` Phase 3.0 ("TAE database extraction").

Source-of-truth raw extraction lives on the Projects share at
`/mnt/station-projects/elden-ring/chr-extracted/` (807 character dirs,
64,385 anim-*.xml files, 2.51 GB). That data is NOT committed to this
repo — it's too large and we don't own redistribution rights for the
TAE files. The repo only commits the *parsed and aggregated*
`parry_data.json` derived from that data.

The parser script lives at `tools/parse_taes.py` (TBD). Run it from the
repo root; it reads from the Projects share and writes
`data/parry_data.json` here.

## `sample-fixtures/` — parser unit-test inputs

Three small anim XML files, ~50 KB total, that exercise the parser's
discrimination:

| File | Purpose | Expected parser output |
|---|---|---|
| `c4100-anim-000003000.xml` | Crucible Knight sword swing — canonical parryable boss attack | 1 parry window at 0.6333s -> 0.7000s |
| `c4100-anim-000000020.xml` | Boss anim with zero parry windows (negative case) | 0 parry windows |
| `c0000-anim-000000.xml` | Player anim — players are never parry-ee | 0 parry windows |

These are committed because they're tiny, they ship the parser test
suite for free, and they let the parser be developed without an SMB
mount of the full extraction.

## Why this lives in the repo

The DLL needs to load the JSON at startup. Shipping a baked database
with the mod (instead of forcing players to run an extractor) is the
clean UX, and it means the parser is a one-time build step, not a
runtime dependency.

## Provenance

`parry_data.json` is generated from `chr-extracted/` data unpacked
with:
- UXM Selective Unpack 2.4.2
- WitchyBND v3.0.0.1 (Windows build)
- Raw `chr/c*.anibnd.dcx` files from a Steam-installed ER 1.16 + SOTE
  (Game executable mtime 2025-08-21).

The TAE event-type catalog used to interpret these XMLs is WitchyBND's
`Assets/Templates/TAE.Template.ER.xml`. Investigation findings on which
event types matter for parry-tell live in
`research/phase3-tae-investigation-codex.md`.
