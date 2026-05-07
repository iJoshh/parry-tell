# Phase 2 Research — Claude's independent analysis

**Date:** 2026-05-07 ~00:30 CT
**Author:** Claude Opus 4.7
**Status:** Independent. Has NOT read `phase2-research-codex.md` (parallel work by Codex).
**Question:** What offset from WorldChrManImp (WCM) gives us the local player's ChrIns in Elden Ring 2.6.1.0, and how should we access it safely?

---

## Headline finding

**The offset `WCM + 0x10EF8` is correct, but our v3/v4 access pattern was fundamentally wrong.**

`WCM + 0x10EF8` is the start of `ChrIns** playerArray[4]` — an array of 4 **pointer-to-pointer-to-ChrIns**. `playerArray[0]` is the local player. Reading the ChrIns requires:

1. `qword at WCM + 0x10EF8` → `ChrIns**` (call this `pp0`)
2. `qword at pp0` → `ChrIns*` (the actual ChrIns struct pointer)
3. ChrIns struct lives there, accessed by member offsets

Our v4 probe read `WCM + 0x1E508` directly and treated the result as a `ChrIns*`. That offset isn't even in the playerArray region — it's somewhere else in WCM, possibly an unrelated struct. Even worse, **production mods (PostureBarMod) don't read the ChrIns through the pointer chain at all** — they extract the *handle* from `playerArray[0]` and pass it through `GetChrInsFromHandleFunc`, an in-game function that resolves handle→ChrIns safely.

---

## Evidence trail

### Direct evidence from the WCM dump (131,072 bytes captured at probe init, 2026-05-06 22:55 CT)

I read the dump as 64-bit little-endian values, classified pointers by high 16 bits, and walked key offsets.

**`WCM + 0x10EF8` (PostureBarMod's offset):**
```
WCM+0x10EF8: 0x00007FF45AA4DE00  <- heap pointer, well-formed
WCM+0x10F00: 0x15C7D958FFFFFFFF
WCM+0x10F08: 0x00007FF621191858  <- image-region (vtable-shaped)
WCM+0x10F10: 0x00007FF3B2B4BFB0  <- heap pointer
```

`0x10EF8` contains a heap-shaped pointer at probe-init time. That's a viable starting point — the slot is populated.

**`WCM + 0x1E508` (our PROBE-SPEC's offset):**
```
WCM+0x1E500: 0x0000000000000000
WCM+0x1E508: 0x0000000000000000
WCM+0x1E510: 0x0000000000000000
```

**Flat zero at probe-init time.** This is empirically not a populated slot when our probe loads. The v4 probe later observed reads from this offset returning `0x00007FF3FA5AA5B0` during play — which means the slot DID get populated post-load, but to what? With no struct definition for that location, we were reading some entirely different game struct's fields and treating them as ChrIns members. That's why entity_id at +0x1E8 came back zero — there's no entity_id at that offset in whatever struct that pointer was pointing to.

**Sanity check:** `0x00007FF3FA5AA5B0` (the v4-observed pointer) does NOT appear anywhere in the WCM dump. So the dump is from a different game-state moment than the v4 reads, but the dump's "zero at 0x1E508" is consistent — that slot just isn't a stable player-ChrIns pointer.

### Pointer population statistics in the dump

Total heap-shaped pointers in 131KB: 1,478. Unique values: 644. The most-repeated values look like vtables (top-3 each appear 100+ times):

```
0x00007FF621191858  appears 239x  (likely a per-entry vtable)
0x00007FF62008E318  appears 115x
0x00007FF6211932A8  appears 113x
```

This pattern — same pointer at regular intervals through the dump — is the signature of an **array of structs with vtables in matching positions**. WCM is mostly composed of such arrays.

### PostureBarMod source (the smoking gun)

`Source/Main/Hooking.hpp:155-158`:
```cpp
struct WorldChrMan {
    char unk[0x10EF8];
    ChrIns** playerArray[0x4];   // 4 pointer-to-pointers
};
```

The `[0x4]` is the **array length** (4 elements), NOT an index. I initially misread this. Each element is `ChrIns**`. So `playerArray[0]` is the first ChrIns**, occupying bytes `0x10EF8 .. 0x10EFF` of WCM.

`Source/Main/PostureBarUI.cpp:452`:
```cpp
auto&& chrIns = g_Hooking->GetChrInsFromHandleFunc(
    worldChar,
    &(*worldChar->playerArray[0])->handle
);
```

Three things:

1. **`playerArray[0]` IS the local player.** Not [4], not [3], not "host." Index 0.
2. **They don't trust the raw pointer** — they extract the `handle` (a stable 64-bit identity) and pass it to `GetChrInsFromHandleFunc`, an in-game function that re-resolves handle → ChrIns. This survives state changes (death, respawn, area transitions) because handles persist while pointers don't.
3. **They guard everything** — line 441 checks `if (!worldChar || !feMan || !g_Hooking->GetChrInsFromHandleFunc) return;` before touching anything.

The ChrIns struct definition (Hooking.hpp:140-153):
```cpp
struct ChrIns
{
    uint8_t undefined[0x8];        // +0x00: vtable
    unsigned long long handle;     // +0x08: 8-byte stable handle
    uint8_t undefined2[0x50];      // +0x10..+0x60
    int npcParam;                  // +0x60
    int modelNumber;               // +0x64
    int chrType;                   // +0x68: 4-byte chrType
    uint8_t teamType;              // +0x6C: 1-byte teamType
    uint8_t undefined3[0x123];     // +0x6D..+0x190
    ChrModuleBag* chrModulelBag;   // +0x190
    uint8_t undefined4[0x508];
    unsigned long long targetHandle; // +0x6A0
};
```

This **directly contradicts** the offsets we used in v4:
- We had `entity_id at +0x1E8` → PostureBarMod has `handle at +0x08`
- We had `block_id at +0x38` → PostureBarMod doesn't even have block_id in its struct
- We had `chr_type at +0x64` → PostureBarMod has `chrType at +0x68`

This is significant. We grabbed offsets from TarnishedTool's table, but PostureBarMod uses a **different struct layout**. They might both be right for different reasons (TarnishedTool's struct may be a deeper inspection that adds fields PostureBarMod doesn't expose), or one of them might be wrong, or they might be looking at different ChrIns variants. **I have not yet read TarnishedTool's source carefully enough to resolve this** — it's an open question.

---

## Reconciliation: why did 0x10EF8 vs 0x1E508 disagree?

Likely answer: **`0x1E508` was never the player ChrIns pointer.** It was probably copied from an older / wrong reference (a different game version, a different struct, a different mod's documentation that we misread). Our PROBE-SPEC.md should be considered untrustworthy until I find its citation.

The reason v4's reads at `0x1E508` returned heap-pointer-shaped values is that WCM is large (>0x20000 bytes) and densely packed with pointers. **Any random offset in WCM has a non-trivial chance of containing something pointer-shaped.** Reading garbage that happens to look like a pointer is exactly what you'd expect.

The reason `entity_id` at `+0x1E8` from that bogus pointer came back zero is that the pointer led to memory laid out under some other struct convention. Whatever was at `+0x1E8` of that random object was zero (or zero-aligned).

---

## ChrIns layout for 2.6.1 — UNRESOLVED

PostureBarMod's struct gives us:
- handle at +0x08 (this is the canonical "who is this entity" ID)
- chrType at +0x68
- teamType at +0x6C
- chrModulelBag at +0x190
- targetHandle at +0x6A0

Our v4 used (from TarnishedTool, supposedly):
- entity_id at +0x1E8
- block_id at +0x38
- chr_type at +0x64

**The offsets aren't even close.** Either TarnishedTool's table I borrowed from is wrong, or PostureBarMod's `ChrIns` struct is a smaller/different view than TarnishedTool's "ChrIns," or I borrowed the wrong fields. I have **not** yet read TarnishedTool's source code directly to resolve this. That's a gap I'd want filled before designing v5 — possibly Codex covered it.

**Provisional read:** trust PostureBarMod's offsets for v5 (they're shipping in production for the same game version). Treat TarnishedTool's offsets as suspect until verified against actual TarnishedTool source.

---

## Safety patterns from PostureBarMod

From reading `PostureBarUI.cpp:435-475`:

1. **Always check the global pointers before deref.** Pattern: `if (!worldChar || !feMan || !g_Hooking->GetChrInsFromHandleFunc) return;`
2. **Use the game's own handle→ChrIns function (`GetChrInsFromHandleFunc`) — not raw pointer chains.** This means we need to find that function's address via signature scan (PostureBarMod does this in its hook setup). Handle-based access is stable across state changes; raw pointers can dangle.
3. **Cache nothing across frames.** PostureBarMod re-reads `worldChar` from its signature every call. No assumption that yesterday's pointer is still valid.
4. **Bail out on any null in the chain.** Each chain step has its own `if (!ptr) return;` check.
5. **The hook runs in-context** (it's a hook, not a thread). PostureBarMod uses MinHook to insert at a known game-flow point, so the read happens when the game has already validated state. Our probe runs in a **separate thread**, which is structurally less safe — we have no game-flow synchronization.

The thread-vs-hook distinction is important. Our v4 polling thread reads structures the main thread is potentially writing. PostureBarMod is *single-threaded relative to the game* because it hooks into the game's UI render path. v5 should consider the same approach.

---

## Confidence and what would change my mind

**Confidence in headline finding (`0x10EF8` is the correct offset, two derefs needed, handle-based access preferred): MEDIUM-HIGH.**

What would raise it to high:
- TarnishedTool source confirming the same offset and access pattern
- A successful in-game test where we read `playerArray[0]` via PostureBarMod's pattern and get a stable, consistent ChrIns for many frames

What would lower it:
- Finding evidence that PostureBarMod's struct definition is for a *different* ER version than 2.6.1 (would need to check their git tags / changelog)
- Discovering that `playerArray[0]` is the host, not the local player, in Seamless Co-op guest mode (our actual use case is guest)

The Seamless Co-op question is genuinely open. Vanilla ER has one local player who is always slot 0. Seamless Co-op may rewrite this — Josh as guest might be slot 1, 2, or 3, not slot 0. **PostureBarMod doesn't ship for Seamless Co-op** so its assumption of "slot 0 = local player" might break for our use case. This is the highest-risk uncertainty in this analysis.

---

## Open questions

1. **Seamless Co-op slot question:** is the guest at `playerArray[0]` or at a higher index? PostureBarMod's pattern works for solo + host but may break for guest.
2. **GetChrInsFromHandleFunc address:** what's the signature for this game function in 2.6.1.0? PostureBarMod's hook setup shows how to find it but I haven't extracted the signature.
3. **TarnishedTool source vs my borrow:** are the offsets I used in v4 actually correct per TarnishedTool's source, or did I copy them wrong from someone else's notes?
4. **Hook vs polling thread posture:** is in-game-thread access (via a hook) genuinely safer than our worker-thread polling, or was the v4 crash caused by something more fundamental than thread context?

---

## Recommendations for v5

**Hard rules:**
1. Use `WCM + 0x10EF8` as the playerArray base. Never read `0x1E508`.
2. Implement TWO derefs through `playerArray[0]` to reach the ChrIns. Do not treat `playerArray[0]` directly as a ChrIns pointer.
3. Prefer handle-based access via `GetChrInsFromHandleFunc` over raw pointer derefs. Find that function's address via signature scan first.
4. Null-guard every step. SEH-wrap every read.
5. Drop the WCM startup dump entirely. We have a good one already; we don't need another.
6. Drop the prio queue walk for v5 — focus on player chain only.
7. Read on a hotkey press, not in a polling thread. Single sample, one log line, idle.

**Test-blocking unknowns:**
- Resolve the Seamless Co-op slot question before any test (read PostureBarMod git history, search for SC compatibility notes, OR test with Josh as host first to validate pattern then re-test as guest).

**Codex review priority:**
- Highest priority is the TarnishedTool-vs-PostureBarMod struct disagreement. Need second-vendor confirmation before locking offsets.

---

## Audit trail

Workspace: `/home/joshua.blattner/claude/elden-ring/research/claude-workspace/`
Tools used: Python (struct.unpack), bash (xxd, grep, ls), Read tool for file inspection.
Sources verified by direct file read:
- `posturebarmod/Source/Main/Hooking.hpp:140-159`
- `posturebarmod/Source/Main/PostureBarUI.cpp:435-475`
- `probe/runs/run-3-2026-05-06-2255-CT-v4-CRASH/parry-tell-probe-wcm-dump.bin` (full dump scan)

Sources NOT verified, gaps in this analysis:
- TarnishedTool source (assumed offsets borrowed from it; should verify directly)
- Liber, Erd-Tools-CPP source (not consulted)
- Seamless Co-op source (not in `.archaeology-sources/`)
