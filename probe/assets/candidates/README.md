# Audio cue candidates

A/B options for the parry-tell-probe Phase 4.2 audio cue. Each is mono,
44.1 kHz, 16-bit PCM WAV — already matches the embedded-resource spec.

Pick one, copy it to `probe/assets/audio_cue.wav`, rebuild the DLL.

## Files

| File | Source | Duration | Size | License | Character |
|---|---|---|---|---|---|
| `swordclash-80ms-faded.wav` | freesound.org/s/180820 | 80 ms | 7.1 KB | CC-BY 4.0 (32cheeseman32) | Sword-on-sword metallic clang, attack only |
| `swordclash-120ms-faded.wav` | freesound.org/s/180820 | 120 ms | 10.6 KB | CC-BY 4.0 | Same, with more decay tail |
| `swordclash-200ms-faded.wav` | freesound.org/s/180820 | 200 ms | 17.7 KB | CC-BY 4.0 | Same, with reverb hint |
| `swordclash-80ms.wav` | freesound.org/s/180820 | 80 ms | 7.1 KB | CC-BY 4.0 | Hard cut at 80 ms (no fade) |
| `swordclash-120ms.wav` | freesound.org/s/180820 | 120 ms | 10.6 KB | CC-BY 4.0 | Hard cut at 120 ms |
| `swordclash-200ms.wav` | freesound.org/s/180820 | 200 ms | 17.7 KB | CC-BY 4.0 | Hard cut at 200 ms |
| `swordclash-fullpreview.mp3` | freesound.org/s/180820 | 1.85 s | 44 KB | CC-BY 4.0 | Untouched preview, for re-trimming |
| `uiclick-60ms.wav` | freesound.org/s/677861 | 60 ms | 4.2 KB | CC-BY 4.0 (el_boss) | Snappy synthetic UI click — Pop-Click-class |
| `uiclick-full.wav` | freesound.org/s/677861 | 72 ms | 4.2 KB | CC-BY 4.0 | Same, no trim |

## Why these two

- **SwordClash01** (freesound 180820) — Josh's pick. Thematic match
  for Elden Ring. Onset at 11ms, peak at 27ms, total 1.85s. Trimmed
  to three lengths because the attack envelope is what matters; the
  reverb tail after ~150ms is irrelevant for a "fire NOW" cue and
  longer cues risk overlapping the next window.

- **el_boss UI Click** (freesound 677861) — substitute for Pixabay's
  Pop-Click-312649, which is blocked behind Cloudflare from this VM.
  Same character: synthetic, snappy, high-attack. 47ms native — already
  inside the target duration band.

## Picking guidance

- **For a "this is information" cue** that doesn't blend with sword
  ambient: pick `uiclick-60ms.wav`. UI/synthetic timbre helps it cut
  through.
- **For a "thematic" cue** that feels like part of the game world:
  pick one of the `swordclash-*-faded.wav` variants. 80ms is sharpest
  (just the strike); 120ms includes the post-strike resonance; 200ms
  is the full hit with a hint of decay.
- **Faded vs hard-cut:** prefer faded. The hard-cut versions exist
  for comparison but a sudden waveform truncation can produce an
  audible click artifact at the end. The fade is 15ms — imperceptible
  by itself, prevents the click.

## License compliance

Both source sounds are CC-BY 4.0, which requires attribution. When we
package v0.1.0 for release, add to CHANGELOG.md or a LICENSES.md:

```
Audio cue derived from:
- "SwordClash01" by 32cheeseman32 (freesound.org/s/180820), CC-BY 4.0
- "UI Button Click" by el_boss (freesound.org/s/677861), CC-BY 4.0
```

Trim + format-conversion does NOT change license obligations — derivative
works retain CC-BY-4.0 status. Attribution in release notes is sufficient.

## Re-trim recipe

If you want a different duration from `swordclash-fullpreview.mp3`:

```bash
ffmpeg -i swordclash-fullpreview.mp3 \
       -t 0.150 \
       -af "afade=t=in:st=0:d=0.005,afade=t=out:st=0.135:d=0.015" \
       -ac 1 -ar 44100 -sample_fmt s16 \
       swordclash-custom.wav
```

Adjust `-t 0.150` for total duration. Fade-out start = total - 0.015.

## Station preview path

Copies live at `/mnt/station-mods/parry-tell-audio-candidates/` so you can
double-click to play from the Windows side. After picking one:

```bash
cp probe/assets/candidates/<chosen>.wav probe/assets/audio_cue.wav
```
