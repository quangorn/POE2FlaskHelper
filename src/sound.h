#pragma once
// PlaySound from the exe resources (SPEC §4.5). SND_ASYNC keeps the UI non-blocking:
// PlaySound returns immediately, the wave is played by the system. Which WAV
// corresponds to which state is decided in poe2_core (state.cpp wav_res_id(),
// tested), this layer only plays the given resource id.

#include <windows.h>

#include <cstdint>

#include <mmsystem.h>

#include "log.h"
#include "res_ids.h"

namespace poe2 {

// res_id: ID_ENABLE_WAV / ID_DISABLE_WAV from res_ids.h.
// volume_percent 0..100 (SPEC §5 sound.volume, backlog "Громкость WAV"): the WAV
// bytes are read from the resource and scaled by poe2_core apply_pcm_gain on EVERY
// play, so the value comes from the live config - the seam for a future runtime
// config reload. SND_MEMORY plays the scaled bytes.
// Buffer lifetime (measured 2026-11-08, the reason the first SND_MEMORY build
// played at full volume): with SND_MEMORY | SND_ASYNC PlaySound does NOT copy the
// data - "the memory buffer that contains the sound must remain valid until the
// sound has completed playing" (PlaySound docs). The scaled bytes are kept in a
// module-level buffer (main thread only, SPEC §4.2) and replaced on the next play;
// stop_sound purges before any teardown. This is not a start-time gain cache: the
// scaling still runs on every call.
// Fallback (SPEC §4.5): resource missing, or data not PCM16 - the original
// SND_RESOURCE path plays the file unamplified, logged as warn.
// Returns false when PlaySound failed: the caller logs a warning and the toggle
// proceeds without sound (SPEC §8).
bool play_wav(uint16_t res_id, HINSTANCE hInst, int volume_percent, Logger& logger);

// Stops any playing sample (shutdown path).
void stop_sound();

}  // namespace poe2
