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
// Returns false when PlaySound failed (missing/broken resource): the caller logs a
// warning and the toggle proceeds without sound (SPEC §4.5, §8).
bool play_wav(uint16_t res_id, HINSTANCE hInst);

// Stops any playing sample (shutdown path).
void stop_sound();

}  // namespace poe2
