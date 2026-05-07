# parry-tell — HANDOFF

**Last session:** 2026-05-07 morning, through compact at ~10:15 CT.
**Status:** Phase 2 essentially complete. Probe works. Starting PHASE3-PLAN.md next.

## Where we are

**Probe v5f is installed on station and working.** Hook-based, F11-armed,
no crashes across multiple sessions. Production-quality scaffolding for
Phase 3.

**v5f file:** `/mnt/station-mods/parry-tell-probe.dll`
**v5f md5:** `3dc6b79c841bef0be0f4f6e376bb4973`
**v5f source:** `probe/probe.cpp` (HEAD = `95c3e25`)

If a future session needs to disable the probe, rename
`/mnt/station-mods/parry-tell-probe.dll` → `parry-tell-probe.dll.old`
(strip the `.dll` extension so Mod Loader skips it).

## What we know (from in-game v5e + v5f data)

**Player chain (locked):**
- WCM global resolved via signature `48 8B 05 ? ? ? ? 48 85 C0 74 0F 48 39 88` + RIP-relative deref
- `WCM + 0x10EF8` → `ChrIns** playerArray[4]`
- `playerArray[0]` is the local player
- Two derefs (slotEntry → chrIns) + handle round-trip via
  `GetChrInsFromHandle(world, &chrIns->handle)` gives a stable, canonical
  ChrIns pointer that doesn't move for the entire session

**Confirmed playerArray[0] is a `PlayerIns`, NOT a generic `ChrIns`:**
- mapX at chrIns+0x6C0 (float, world coord X) — confirmed by walking and seeing values change smoothly
- mapAngle at chrIns+0x6CC (float, radians, -π..+π) — confirmed
- handle at chrIns+0x8 (uint64_t) — confirmed stable
- blockId at chrIns+0x6D0 — TarnishedTool says this; in our data it's
  NOT actually blockId, returns a few float-ish constants. **Offset is
  wrong for 2.6.1, OR this isn't blockId.** Doesn't matter for Phase 3.
- Generic ChrIns offsets (entity_id +0x1E8, blockId +0x38) DO NOT APPLY
  to this slot. Don't use them.

**Hook architecture:**
- Hook target: `UpdateUIBarStructs` (game's per-frame UI update fn)
- Detour signature: `void(uintptr_t moveMapStep, uintptr_t time)` —
  must match exactly, two args
- Detour calls SampleOnce() iff armed AND >=1s since last (CAS-gated),
  then chains to original
- F11 toggles `g_armed` from a separate watcher thread (zero game-memory
  reads from that thread)

**Module pinned** via `GetModuleHandleEx(PIN | FROM_ADDRESS)` — cannot be
unloaded mid-session.

**Performance:**
- v5d/v5e had a per-second hitch caused by ~30 VirtualQuery syscalls per
  sample on the game thread.
- v5f introduced `LooksLikeUserPtrFast` (pure compute) for hot path; SEH
  catches real faults via SafeRead<T>. Hitch is gone.
- Slow VirtualQuery-backed `LooksReadable<T>` and `LooksLikeUserPtr` are
  retained for init-time use only (sig scan, RIP deref).

## What we DO NOT know (open for Phase 3)

- **Target handle offset.** When you lock onto an enemy, where does your
  PlayerIns store the target? Phase 3.1 needs to find this — likely via
  hit-region memory diff during lock-on/unlock cycles.
- **Animation state offsets.** What field signals "I am being attacked
  by a parryable attack right now"? This is the actually-hard problem
  for the parry-tell mod. Likely lives off the chrModuleBag pointer at
  +0x190 (TarnishedTool) — but THAT offset is the generic ChrIns
  layout, which may or may not apply via the PlayerIns wrapper.
- **HP / stagger offsets** if we want to make the cue smarter (don't
  fire while in iframes / dead).
- **D3D12 hook target** for the visual cue (screen-edge hue). We've
  been hooking gameplay logic; visual rendering needs a different hook.

## Phase 2 → Phase 3 transition

**Phase 2 deliverables (all in repo):**
- `probe/probe.cpp` — production-quality Phase 2 probe
- `probe/vendor/MinHook/` — vendored MinHook (BSD-2)
- `probe/probe.vcxproj` — builds with v145 toolset on station
- `research/SYNTHESIS.md` — Phase 2 research synthesis
- `research/phase2-research-{claude,codex}.md` — parallel blind reads
- `research/v5{,b,d}-codex-review.md` — adversarial review history
- `probe/releases/probe-v5{c,d,e,f}.tar.gz` — release artifacts

**Phase 3 starts with PHASE3-PLAN.md.** Should be drafted post-compact.
Suggested structure:
1. Goal restatement (parry indicator: hue shift + audio cue at
   parry-window-open frame)
2. Phase 3.1: offset hunting for target_handle, animation_id, hit-event
   flags. Probably a more focused v6 of the probe with arming on
   specific events (lock-on toggle, hit taken).
3. Phase 3.2: D3D12 hook for the visual cue (separate hook from
   gameplay logic).
4. Phase 3.3: state machine — multi-boss aware, lock-on aware,
   PvE-only, boss-fights-only.
5. Phase 3.4: audio cue + INI config.
6. Test plan: parry tools (Parrying Dagger from Twin Maiden Husks at
   Roundtable Hold, 1600 runes; or Buckler from Limgrave nomadic
   merchant 1800 runes).

## Resumption checklist for next session

1. `cd /home/joshua.blattner/claude/elden-ring && git status` (should
   be clean)
2. `mount | grep station` (should show both mounts; auto-mount on
   first access if needed)
3. Confirm SSH service status with Josh — manual start, not always on
4. If Josh says "go" → write `PHASE3-PLAN.md` (start with goal +
   open questions; CEO/eng review pass via plan-reviewer subagent
   before locking)
5. If Josh says "test more first" → just sit on the probe; v5f works.

## Workflow notes preserved across compact

- **No PostureBarMod conflict.** Josh doesn't run it. Coexistence with
  it was a hypothetical concern; in his actual rig, slot 0 of
  playerArray is uncontested.
- **SSH service on station is manual-start.** Do not assume it's
  running. Test with a `ssh ... echo SSH_OK` first.
- **Build chain:** scp source → ssh MSBuild → SMB read DLL → cp to
  mods folder. Toolset is v145. ~10s wall time.
- **Codex MCP timeout** is shorter than Codex's actual runtime. If a
  request times out at the MCP layer, codex itself usually keeps
  running — `pgrep -af codex` to check before retrying.
- **Codex's read-only sandbox can't write** to research/. Inline-content
  responses get pasted into a Write tool call. This is fine.
- **Critic plugin auto-fires** after every Write/Edit. Verdicts append
  to tool result. Address before final summary per global protocol.
