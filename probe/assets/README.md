# probe/assets/ — audio cue resource

## audio_cue.wav

The Phase 4.2 parry-window audio cue, embedded into `parry-tell-probe.dll`
via `parry-tell-probe.rc` and the `IDR_AUDIO_CUE_WAV` resource ID.

### Spec (per PHASE4-PLAN.md lines 583-599)

| Field | Value |
|---|---|
| Duration | 50–100 ms |
| Format | PCM WAV (`WAVEFORMAT_PCM`, code 1) |
| Sample rate | 44.1 kHz or 48 kHz |
| Channels | 1 (mono) |
| Bit depth | 16-bit signed |
| Size target | < 32 KB |
| Size hard cap | 128 KB |

### Current state

`audio_cue.wav` ships as a **50ms silent placeholder** (4,454 bytes,
44.1 kHz / mono / 16-bit PCM). This makes the build complete and tests
the whole audio pipeline end-to-end — load, embed, find resource,
PlaySoundW returns TRUE — without producing audible noise that would
confuse smoke-test analysis.

**Before live test, swap in a real tone.** Suggested options:

1. **System sound**: copy any short Windows `.wav` (e.g.,
   `C:\Windows\Media\chord.wav` truncated to 100ms) into this directory
   as `audio_cue.wav`.
2. **Synthesize a short tone**:
   ```python
   import math, struct, wave
   sr = 44100
   dur_ms = 80
   freq = 880.0   # A5
   amp = 0.55
   n = sr * dur_ms // 1000
   # Apply 5ms linear attack + 20ms release envelope to avoid clicks.
   atk = sr * 5 // 1000
   rel = sr * 20 // 1000
   sus = n - atk - rel
   samples = []
   for i in range(n):
       t = i / sr
       s = math.sin(2 * math.pi * freq * t)
       if i < atk:
           env = i / atk
       elif i < atk + sus:
           env = 1.0
       else:
           env = max(0.0, 1.0 - (i - atk - sus) / rel)
       samples.append(int(s * env * amp * 32767))
   with wave.open('audio_cue.wav', 'wb') as w:
       w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
       w.writeframes(struct.pack('<%dh' % n, *samples))
   ```
3. **Audacity**: Generate → Tone (Sine, 880Hz, 0.08s, amplitude 0.6),
   Effect → Fade In (5ms), Effect → Fade Out (20ms), Tracks → Mix →
   Mix Stereo Down to Mono, Export → WAV (Signed 16-bit PCM, 44.1kHz).

After replacing the file, rebuild the DLL — the resource compiler embeds
the new bytes. No INI change needed.

### Runtime override

Josh can also bypass the embedded resource entirely by setting the
`audio_cue_wav_path` INI key to an absolute path. See
PHASE4-PLAN.md Phase 4.3 for the INI surface. The override path is
heap-loaded once at worker init and freed at shutdown.

### Validation

`audio.cpp::LooksLikeRiffWave` checks the first 12 bytes are `RIFFxxxxWAVE`.
That's the entire validation surface — PlaySoundW handles fmt-chunk
parsing and rejects malformed files by returning FALSE (which triggers
the first-failure-disables-session safeguard in `FireAudioCue`).
