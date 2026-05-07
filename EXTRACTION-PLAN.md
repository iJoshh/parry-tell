# Extraction Plan — One-and-Done Game-Data Pull

**Updated 2026-05-07:** simplified handoff path now that SMB mounts exist. You
drop the zip at a known Projects-share path; I read it directly off the
mount. No GitHub LFS, no cloud share link.

## Purpose

Run UXM Selective Unpacker + WitchyBND **once** against your Elden Ring
install to extract TAE event data for parryable boss attacks. Goal: never
have to do this again.

After: I parse the data into `data/parry_data.json` (with version metadata
so we can detect drift across patches). You run Steam file-verify to
restore vanilla so live online + Seamless both work clean.

## Disable probe before extraction

Before you start UXM, rename the probe DLL so it doesn't load while UXM
is touching game files:

```powershell
Rename-Item "C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mods\parry-tell-probe.dll" "parry-tell-probe.dll.disabled"

# Verified install layout (2026-05-07):
#   eldenring.exe              87 MB   2025-08-21 (Seamless-patched, in-place)
#   ersc_launcher.exe          175 KB  2024-06-28 (Seamless 1.7.x era — older
#                                                  but TAE data is independent)
#   start_protected_game.exe   3.9 MB  2024-06-27 (EAC stub, unused under Seamless)
```

(Or just rename it via Explorer — drop the `.dll` extension. EML skips
anything that's not `.dll`.)

I'll restore it when we move to Phase 3.1.

## Estimated total time

- Disable probe + UXM install + first run: **~30 minutes** (mostly waiting for unpack)
- WitchyBND install + extraction: **~30-45 minutes**
- Zip + drop on Projects share: **~5 minutes** (just `Move-Item` to the mount)
- Steam verify (after I confirm receipt): **~15-30 minutes**

**Total Josh time: ~90 min active, ~2 hours wall.**

---

## Tools you'll install

### 1. UXM Selective Unpacker

- **What it is:** Decrypts and unpacks ER's game archive files. Required because vanilla ER ships with files compressed/encrypted.
- **Where to get it:** https://www.nexusmods.com/eldenring/mods/1494
  (You'll need a free Nexus account if you don't have one.)
- **License:** Free, community-standard. Used by virtually every ER modder.
- **Important:** UXM modifies your `Game/` folder. We undo this with Steam verify when we're done.

### 2. WitchyBND

- **What it is:** Unpacks `.anibnd` archive files into XML files we can read. Modern successor to Yabber.
- **Where to get it:** https://github.com/ividyon/WitchyBND/releases
  (Get the latest release `.zip`.)
- **License:** Open source.
- **Note:** Confirm you're getting the WitchyBND release (.exe + dependencies in a zip), not source code.

---

## Step-by-step

### Phase A — UXM unpack (15-30 min wall, 5 min active)

1. **Download UXM Selective Unpacker** from the Nexus link above. Extract the .zip to a folder of your choice (e.g., a `tools/` folder under your projects directory).

2. **Run UXM.exe.** It opens a small UI.

3. **Point it at your ER executable.** Click "Browse" and navigate to:
   `C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe`
   (Adjust if your Steam library is on a different drive.)

4. **Click "Unpack."** UXM will decrypt the game archives. This takes 15–30 minutes depending on your disk speed. You'll see a progress bar.

5. **When complete,** your `Game/` folder will now have many extra subdirectories like `chr/`, `event/`, `script/`, `param/`, etc. — these are the game's unpacked data files.

✅ **Phase A complete when:** the `Game/chr/` folder exists and contains many `c*.anibnd.dcx` files.

### Phase B — WitchyBND extraction (30–45 min wall, 20 min active)

This phase is where we pull the actual data we want.

6. **Extract WitchyBND.zip** to a folder of your choice (e.g., next to where you put UXM).

7. **Set up the extraction working directory.** Create a folder for our extracted data wherever you keep projects (e.g., `D:\projects\parry-tell-extraction\` or `C:\Users\<you>\Documents\parry-tell-extraction\`). Don't put it under your ER install directory and don't put it at the C:\ root.

   Throughout this doc I'll use `<EXTRACT_DIR>` to refer to whatever path you picked.

8. **Right-click → Send to → Add WitchyBND context menu** (if WitchyBND offers this in its setup), OR plan to run WitchyBND.exe via drag-and-drop. The README in WitchyBND's zip explains either approach.

9. **Extract the character archives.** For each character archive listed in the file list below, you'll:
   - Copy the `.anibnd.dcx` file from `Game/chr/` to `<EXTRACT_DIR>\chr-source\`
   - Right-click on it → WitchyBND → Unpack
     OR drag-and-drop the file onto WitchyBND.exe
   - WitchyBND creates a folder next to it with the extracted XML files

   **You don't have to do these one-at-a-time.** You can copy ALL the `.anibnd.dcx` files at once and select them all → right-click → WitchyBND → Unpack. WitchyBND will batch-process. Faster.

10. **Files to extract (the actual shopping list):**

    All paths are relative to `Game/`. Copy the listed files (and their `.dcx` siblings if separate) to `<EXTRACT_DIR>\chr-source\` and let WitchyBND unpack them.

    **Tier 1: Boss + mini-boss character archives (the priority data)**

    These are the characters whose attack data we MUST have for v1. The extracted XML will give us TAE event data including parry windows.

    All of these are under `Game/chr/`:
    - `c0000.anibnd.dcx` — player character (we want this for reference / future v2 player-side cues)
    - `c2030.anibnd.dcx` — Banished Knight
    - `c2050.anibnd.dcx` — Crucible Knight
    - `c4070.anibnd.dcx` — Black Knife Assassin
    - `c4140.anibnd.dcx` — Godrick Soldier (one of several footsoldier types)
    - `c4180.anibnd.dcx` — Lordsworn / Knight (foot soldier)
    - `c4190.anibnd.dcx` — Misbegotten variant
    - `c4500.anibnd.dcx` — Wandering Noble (basic humanoid)
    - `c5290.anibnd.dcx` — Misbegotten Warrior
    - `c5360.anibnd.dcx` — Black Knife Tiche (and similar)
    - `c5380.anibnd.dcx` — Cemetery Shade
    - `c5450.anibnd.dcx` — Crystallian
    - `c5470.anibnd.dcx` — Royal Knight
    - `c6000.anibnd.dcx` — Soldier (basic)
    - `c6020.anibnd.dcx` — Skeleton
    - `c6040.anibnd.dcx` — Demi-Human
    - `c6050.anibnd.dcx` — Pumpkin Head
    - **All major bosses** — listed below

    **Major boss archives (these are the ones with health bars):**

    Some boss IDs (incomplete list — extract any `c[6789]xxx.anibnd.dcx` you find):
    - `c1000.anibnd.dcx` — Margit / Morgott
    - `c1050.anibnd.dcx` — Godfrey / Hoarah Loux
    - `c1100.anibnd.dcx` — Godrick the Grafted
    - `c2080.anibnd.dcx` — Crucible Knight (Ordovis variant)
    - `c4140-4180` range — armored knights (covered above)
    - `c5390.anibnd.dcx` — Tibia Mariner
    - `c5410.anibnd.dcx` — Mimic Tear (boss version)
    - `c6090.anibnd.dcx` — Erdtree Avatar
    - `c6100.anibnd.dcx` — Ulcerated Tree Spirit
    - `c6110.anibnd.dcx` — Tree Sentinel
    - `c8000-c8999` range — story bosses (Maliketh, Radahn, Rennala, Malenia, Radagon, Elden Beast)
    - `c9000+` range — DLC bosses (SotE)

    **My recommendation: just extract every `c*.anibnd.dcx` file in `Game/chr/`.** It's roughly 200 files. WitchyBND batch-unpacks them in 20–30 minutes. You don't have to know which one is which boss — I'll figure that out from the extracted data.

    **Disk usage:** Each unpacked archive is roughly 10–30MB. Total extracted data: ~3–6GB. Fine on most modern drives.

    **Tier 2: Static game data (also valuable, also free to extract)**

    - `Game/regulation.bin` — the param tables (BehaviorParam, AtkParam, NpcParam). Already triple-confirmed via libER, but having it offline lets us build static lookup tables at our build time. Note: `regulation.bin` is a single file, NOT a `.anibnd` — for this one you don't use WitchyBND, you just **copy the file as-is** to `<EXTRACT_DIR>\regulation.bin`. I'll parse it offline using existing tools.

    - `Game/event/common.emevd.dcx` — global event scripts. Useful for understanding boss-engagement triggers.
    - `Game/event/m*.emevd.dcx` (the per-area event files) — needed if we want to understand boss arena triggers per location.
    - `Game/msg/engus/NpcName.fmg` (and related FMG files in the same folder) — boss names mapped to character IDs. We can use this for the mod's UI showing "you are fighting Margit, parryable attacks: kick, sword overhead, ..."

    **For Tier 2:** Use WitchyBND on each `.dcx` or `.emevd.dcx` or `.fmg` file the same way as the character archives. WitchyBND handles all these formats.

    **Tier 3 (optional but tiny — grab if curious):**
    - `Game/script/talk/*.luabnd.dcx` — NPC dialog scripts. Probably useless to us, but tiny.
    - `Game/sfx/*.ffxbnd.dcx` — SFX archives. Useful only for v3 if we ever want to mirror actual game audio in our cues.

11. **When all WitchyBND extractions complete,** your `<EXTRACT_DIR>\` folder will contain unpacked folders matching the file list above. Each unpacked archive will look like:
    ```
    <EXTRACT_DIR>\chr-source\c2050-anibnd-dcx\
        GR\data\INTERROOT_win64\chr\c2050\tae\
            a000.tae.xml
            a001.tae.xml
            a002.tae.xml
            ...
    ```

    The `.tae.xml` files are the TAE event data we want. WitchyBND has converted them from binary to readable XML.

✅ **Phase B complete when:** your `<EXTRACT_DIR>\` folder has unpacked content for every file in Tier 1 + 2, totaling ~3–6GB.

### Phase C — Drop on Projects share (5 min)

12. **Zip the extraction folder.** Right-click on `<EXTRACT_DIR>\` → Send to
    → Compressed (zipped) folder. ~1-2GB compressed. Name it
    `parry-tell-extraction-2026-05-XX.zip`.

13. **Move it to the Projects share** so I can read it from the VM:

    ```powershell
    Move-Item <EXTRACT_DIR>.zip "C:\Projects\elden-ring\extracted\parry-tell-extraction-2026-05-07.zip"
    ```

    (Create the `C:\Projects\elden-ring\extracted\` folder if it doesn't
    exist. The Projects share is read-only from my side, but you have
    full write on the Windows side — `C:\Projects\` is yours.)

14. **Tell me when it's there.** I'll see it via SMB at
    `/mnt/station-projects/elden-ring/extracted/`. I'll confirm size +
    file count + start parsing.

✅ **Phase C complete when:** I confirm I can see the zip and start
extracting.

### Phase C alt — if the zip is huge

If `<EXTRACT_DIR>` is unexpectedly bigger than ~3GB (e.g., you grabbed
Tier 3 too) and zipping takes forever, skip the zip step:

```powershell
robocopy <EXTRACT_DIR> "C:\Projects\elden-ring\extracted\raw" /E /MT:8
```

I can read the unpacked tree directly. ~20% slower for me to parse but
saves your zip-time.

### Phase D — Restore vanilla install

**Wait for me to confirm the data parsed cleanly before Phase D.** If
parsing fails or I find a hole, I may want you to extract one more
archive — easier if your install is still UXM'd.

15. **I'll Telegram or email you "extraction parsed clean, safe to
    file-verify."**

16. **Steam → Library → right-click Elden Ring → Properties → Local Files
    → Verify integrity of game files.**

17. Steam re-downloads modified files, restoring to vanilla. **~15-30 min.**

18. **Confirm vanilla works:** launch ER through Steam normally (NOT
    through Seamless's `ersc_launcher.exe`). You should see EAC's splash
    screen. Confirm online matchmaking works (or just main menu loads).
    You're back to vanilla.

19. **Re-enable the probe DLL** (if you want to keep using it for testing):
    rename `parry-tell-probe.dll.disabled` back to `parry-tell-probe.dll`.

20. **Seamless still works for the mod** because Seamless intentionally
    bypasses EAC; UXM-modified-then-restored install is the same as
    never-UXM'd from Seamless's perspective.

✅ **Phase D complete when:** Steam verify completes, ER launches with
EAC, and you've told me "vanilla restored."

---

## What's in the extracted data (what I'll do with it)

After you send me the extraction:

1. **TAE parser:** I write a Python script that reads every `.tae.xml` and extracts:
   - Animation ID
   - Animation length (in frames)
   - Every TAE event with type, start frame, end frame, parameters
   - Specifically: events related to attack behavior (`InvokeAttackBehavior`), parry windows (`bParryStart`/`bParryEnd` or equivalent), hit-active windows

2. **Behavior cross-reference:** I parse `regulation.bin` (or its extracted form) to build the `BehaviorParam` table that links `BehaviorJudgeID` (from TAE) to `AtkParam_Npc` rows.

3. **Final lookup table:** I build `parry_data.json` mapping `(character_id, animation_id)` → `(is_parryable, parry_window_start_frame, parry_window_end_frame, atk_param_id)`. This ships embedded in the mod DLL.

4. **Audit:** I cross-check a few entries by hand to make sure parsing is correct. If extraction misses something obvious (Margit's hammer slam known to be parryable, but extraction says it's not), I debug the parser before shipping.

---

## What we lose if extraction fails partway

If you have to abort extraction (Phase B fails on a specific file, your disk fills up, Steam updates ER mid-extraction), we don't lose much. The extracted data is incremental — each `.anibnd` we get is independently useful. Worst case, we ship v1 with whatever subset extracted cleanly, and document which bosses are/aren't supported.

---

## Common gotchas

- **WitchyBND error "couldn't find oo2core_8_win64.dll":** This is an Oodle decompression library that ships with the game. WitchyBND's README explains how to copy it from the ER game folder. Quick fix.
- **UXM error "patch already applied":** Means you already ran UXM. If you're sure you want to re-run, there's an "Unpatch" option.
- **`.anibnd.dcx` shows up as a folder, not a file:** Some Windows configurations show extracted folders even when they look like single files in Explorer. Normal.
- **Disk space:** Total extracted data is ~3–6GB. Make sure you have ~10GB free during extraction (UXM and WitchyBND both need scratch space).

---

## When to do this

**Now is fine** — you said "ready for extraction when you are." Probe
v5f is stable, MVP needs the data, nothing else is blocked on it.

**Sequencing inside the session:**
1. Disable probe DLL (~30 sec)
2. UXM unpack (~30 min wall, 5 min active)
3. WitchyBND batch-extract (~30-45 min wall, 20 min active)
4. Zip + drop on Projects share (~5 min)
5. Tell me. I parse + confirm. (you go do something else for ~30 min)
6. After confirmation, Steam file-verify (~15-30 min wall, passive)
7. Re-enable probe DLL if you want it back

**~90 minutes of your active keyboard time.** The unpack/verify steps
are passive — go grab dinner.

## Asks I'll need answered DURING the session (have phone handy)

These are the only things I'll likely need from you mid-session, listed
upfront so you can pre-empt:

1. **Steam library path.** If your ER install is on D:\ or somewhere
   non-default, paste the full path to `eldenring.exe`.
2. **`oo2core_8_win64.dll` location.** WitchyBND will probably ask for
   it. It lives next to `eldenring.exe` (`Game\oo2core_8_win64.dll`) —
   copy it next to `WitchyBND.exe` if prompted.
3. **Disk space confirm.** ~10GB free needed during extraction. If you
   start at <15GB, free some up before Phase B.
4. **Done signal.** When the zip lands at
   `C:\Projects\elden-ring\extracted\` — message me. I'll start parsing.

That's it. Everything else is pre-baked into this doc.

**When you're ready, just say "starting extraction."** I'll watch the
station-projects mount and confirm receipt the moment the zip lands.
