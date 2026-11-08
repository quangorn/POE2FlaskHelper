#pragma once
// PlaySound from the exe resources (SPEC §4.5). SND_ASYNC keeps the UI non-blocking:
// PlaySound returns immediately, the wave is played by the system. Which WAV
// corresponds to which state is decided in poe2_core (state.cpp wav_res_id(),
// tested), this layer only plays the given resource id.

#include <windows.h>

#include <cstdint>

#include <mmsystem.h>

#include "res_ids.h"

namespace poe2 {

// res_id: ID_ENABLE_WAV / ID_DISABLE_WAV from res_ids.h.
// volume_percent 0..100 (SPEC §5 sound.volume, backlog "Громкость WAV"): the WAV
// bytes are read from the resource and scaled by poe2_core apply_pcm_gain on EVERY
// play, so the value comes from the live config - the seam for a future runtime
// config reload. SND_MEMORY plays the scaled bytes; PlaySound with SND_ASYNC
// copies the data into its own buffer, so the temporary vector is safe.
// Fallback (SPEC §4.5): resource missing, or data not PCM16 - the original
// SND_RESOURCE path plays the file unamplified.
// Returns false when PlaySound failed: the caller logs a warning and the toggle
// proceeds without sound (SPEC §8).
bool play_wav(uint16_t res_id, HINSTANCE hInst, int volume_percent);

// Stops any playing sample (shutdown path).
void stop_sound();

}  // namespace poe2
