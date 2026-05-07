# Phase 3 Architecture Research — Codex

**Date:** 2026-05-07 ~10:20 CT
**Model:** gpt-5.3-codex
**Sandbox:** read-only against project root

---

## 1) D3D12 hook for screen-edge overlay

### What `posturebarmod` does (from source)
- Hook targets: `ID3D12CommandQueue::ExecuteCommandLists` (vtable index `54`), `IDXGISwapChain::Present` (`140`), `IDXGISwapChain::ResizeTarget` (`146`).
- Hook library: MinHook (`MH_Initialize`, `MH_CreateHook`, `MH_EnableHook`).
- Install pattern: `DllMain` spins a worker thread, worker sleeps ~3s, then installs hooks; rendering runs from `HookPresent` on render path.
- Threading pattern: queue pointer captured in `HookExecuteCommandLists` once, then used in `Overlay()` called from `HookPresent`.

### Options
| Option | Pros | Cons |
|---|---|---|
| `Present` only | Frame-end stable draw point, one callback per presented frame | Need queue/resource management solved elsewhere |
| `ExecuteCommandLists` only | Earliest GPU submission point | Called many times/frame, harder filtering and higher complexity/risk |
| Hybrid: `Present` + one-time `ExecuteCommandLists` queue capture | Good frame accuracy + simple render injection + robust queue acquisition | Two hooks instead of one |

### Recommendation
- Use **Hybrid**: `IDXGISwapChain::Present` for actual overlay draw + `ID3D12CommandQueue::ExecuteCommandLists` only to latch direct queue pointer.
- Use **MinHook** (same as gameplay hook stack) rather than adding Kiero/Detours dependency.
- Rendering path for this cue should be **ImGui draw list** (no widgets): draw a screen-edge border rectangle with animated alpha for ~500ms.
- This is least code and lowest crash risk versus raw D3D12 pipeline setup for a simple border effect.
- Keep hook/unhook strict and minimal; always call original function.
- Inference: this maximizes coexistence with other mods (PostureBarMod/ERR/ReShade) because chaining is straightforward when each hook forwards correctly.

### Important implementation caution
- `posturebarmod` appears to have an unhook bug (`DisableAll()` passes pointer values into an index-taking function). Do not copy this pattern.

### References
- `posturebarmod/Source/Main/D3DRenderer.cpp:131` — D3D hook creation indices
- `posturebarmod/Source/Main/D3DRenderer.cpp:771` — Present hook calling overlay
- `posturebarmod/Source/Main/D3DRenderer.cpp:788` — Queue capture in ExecuteCommandLists
- `posturebarmod/Source/Main/D3DRenderer.cpp:303` — Dummy D3D objects + vtable table build
- `posturebarmod/Source/Main/Hooking.cpp:11` — MinHook usage in hook coordinator
- `posturebarmod/Source/dllmain.cpp:11` — DllMain thread bootstrap
- `posturebarmod/Source/PostureBarMod.cpp:427` — MainThread delayed hook install
- `posturebarmod/Source/Main/D3DRenderer.cpp:879` — Potential unhook bug
- https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
- https://github.com/Rebzzel/kiero

---

## 2) Audio playback API

### Options summary
| API | Fit for 100ms cue | Complexity | Risk |
|---|---|---|---|
| `PlaySound` (`winmm`) | Good | Very low | Low |
| DirectSound | Legacy | Medium | Unnecessary |
| XAudio2 | Excellent | Medium-high | Overkill for single cue |
| WASAPI raw | Excellent | High | Overkill |
| `miniaudio.h` | Good | Low-medium | Extra third-party integration |

### Recommendation
- Use **`PlaySoundW` with `SND_ASYNC`** for phase 1 production.
- Store cue as embedded WAV resource (`SND_RESOURCE`) or static in-memory buffer (`SND_MEMORY`) with stable lifetime.
- Trigger from gameplay event path without blocking (async); keep any volume scaling simple (pre-scaled cue asset or optional later upgrade to XAudio2/miniaudio).
- Avoid initializing audio systems in `DllMain`; defer all runtime work to worker/game threads.

### References
- https://learn.microsoft.com/en-us/previous-versions/dd743680(v=vs.85)
- https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-introduction
- https://miniaud.io/docs/manual/

---

## 3) State machine architecture

### Recommendation
- Use a **per-attack-instance tracker** keyed by `(bossHandle, slotIndex, attackSeq)` with edge detection on `windowOpen`.
- Decouple outputs:
  - **Audio**: timing-only, fires on `windowOpen` rising edge regardless of lock-on/color.
  - **Color**: targeting feedback (`off` / `primary` / `alert`) recalculated every frame.

### Pseudocode
```cpp
enum HueState { Off, Primary, Alert };

struct AttackState {
    bool activePrev = false;
    bool windowPrev = false;
    uint64_t lastOpenFrame = 0;
    bool consumed = false; // one-shot per attack instance
};

map<Key, AttackState> states;
uint64_t frameNo;

void Tick(FrameInputs in) {
    frameNo++;

    vector<Event> openEvents;
    bool anyParryable = false;
    bool anyTargetingPlayer = false;
    bool lockOnTargetIsParryable = false;

    set<Key> seenThisFrame;

    for (auto& slot : in.bossSlots) {
        if (!slot.parryableActive) continue;
        anyParryable = true;

        Key k = {slot.bossHandle, slot.slotIndex, slot.attackSeq};
        seenThisFrame.insert(k);
        auto& st = states[k];

        bool openNow = slot.parryWindowOpen;
        bool risingOpen = (!st.windowPrev && openNow);

        if (risingOpen && !st.consumed) {
            openEvents.push_back({k});
            st.consumed = true;
            st.lastOpenFrame = frameNo;
        }

        st.activePrev = true;
        st.windowPrev = openNow;

        if (slot.targetHandle == in.playerHandle) anyTargetingPlayer = true;
        if (in.lockOnActive && slot.bossHandle == in.lockOnTargetHandle) lockOnTargetIsParryable = true;
    }

    // Cleanup: attacks canceled/ended
    erase states entries not in seenThisFrame;

    // Audio output (timing only)
    for (auto& e : openEvents) {
        EmitAudioCueOnce(e); // optional short cooldown coalescing if multi-open same frame
    }

    // Color output (targeting feedback)
    HueState hue = Off;
    if (anyTargetingPlayer) hue = Alert;
    else if (in.lockOnActive && lockOnTargetIsParryable) hue = Primary;
    else hue = Off;

    EmitHue(hue);
}
```

### Edge-case handling
- Target switches mid-attack: color updates immediately from current target relation; no extra audio unless a new open edge occurs.
- Lock-on switches mid-attack: color can change same frame; audio unaffected.
- Attack cancels before open: state removed, no audio.
- Two bosses open simultaneously: fire once per unique open event, optionally coalesce within ~80ms to avoid cue spam.

### Note (Claude): the pseudocode swaps Primary and Alert vs. PHASE1-PLAN.md

In PHASE1-PLAN.md goals #4: locked-on-the-attacker = primary color (focus correct);
locked on a different boss = alert (focus wrong); not locked on at all but boss
targeting you = primary (proceed-with-defense). Codex's pseudocode flips the alert
case to "any boss targeting you, regardless of lock-on" which loses the
"switch-focus" signal Josh wanted. PHASE3-PLAN.md keeps PHASE1-PLAN.md's wiring.

---

## 4) INI config schema

### Recommendation
- Use a small schema in `Game\mods\...\parry-tell.ini`.
- Prefer **mINI / SimpleIni (header-only)** over hand-rolled parser for lower bug risk and maintainability. PostureBarMod ships mINI; reuse the same header.

### Minimal schema proposal
```ini
[overlay]
enabled = true
primary_rgb = 80,170,255
alert_rgb = 255,90,60
opacity_max = 0.55
thickness_px = 18
fade_in_ms = 60
fade_out_ms = 440

[audio]
enabled = true
volume = 0.80
cooldown_ms = 80

[input]
toggle_hotkey = F10
```

### Notes
- Keep `F11` free (probe reserved), default suggested `F10` for production runtime toggle.
- Clamp and validate values at load.
- If INI missing/invalid, fall back to safe defaults and continue.

### References
- `posturebarmod/Source/Ini/ini.h:97` — current archaeology project uses header-only INI (`mINI`) with read/write API
- `posturebarmod/Source/Ini/ini.h:725` — mINI read/write entry points
- https://brofield.github.io/simpleini/
- https://github.com/brofield/simpleini
