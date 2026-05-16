// audio.h -- Phase 4.2 audio cue interface for parry-tell-probe.
//
// Surface:
//   InitAudioCue     -- called once from the worker thread after LoadParryDb.
//                       Loads a WAV (override file or embedded resource).
//                       No-op when cfg.enabled == false.
//   FireAudioCue     -- called from WritePredictionDecision when an
//                       ACTION_FIRE / ACTION_LATE_INSIDE_WINDOW /
//                       ACTION_LATE_TARGET_SWITCH passes the rate-limit gate.
//                       Cheap no-op when audio is disabled or init failed.
//   ShutdownAudioCue -- called once from worker shutdown. Stops any in-flight
//                       PlaySoundW and frees the heap buffer if we own it.
//
// Threading:
//   All three functions run on the WORKER THREAD ONLY. There is no internal
//   synchronization. FireAudioCue must not be called concurrently with
//   ShutdownAudioCue. The current architecture satisfies this trivially:
//   Init runs once in WorkerMain init, Fire runs from RunPredictorTick
//   (also on WorkerMain), Shutdown runs at WorkerMain teardown after the
//   predictor loop has exited. If a future caller wants Fire from another
//   thread, add a SRWLOCK + shutdown atomic before changing the contract.

#pragma once

struct AudioCueConfig {
    bool        enabled;    // mirror of Config.audio_cue_enabled
    const char* wav_path;   // mirror of Config.audio_cue_wav_path; "" means
                            // use the embedded resource.
};

bool InitAudioCue(const AudioCueConfig& cfg);
void FireAudioCue();
void ShutdownAudioCue();
