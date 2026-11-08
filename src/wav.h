#pragma once
// Pure PCM16 gain for WAV bytes (SPEC §4.5 sound.volume, backlog "Громкость WAV").
// No Win32: the RIFF/WAVE chunk parse and the sample scaling are testable in
// poe2_tests on synthetic buffers (§3.1); sound.cpp loads the resource and plays.

#include <cstdint>
#include <vector>

namespace poe2 {

// bytes: a RIFF/WAVE image (the shipped enable/disable WAV assets are PCM 16-bit
// mono, see res/gen_assets.py). percent: 0..100, config sound.volume.
// Every int16 sample of the data chunk is multiplied by percent/100 (truncation
// toward zero, clamped to the int16 range). Headers, chunk layout and all
// non-data bytes are copied byte-for-byte.
// Returns an empty vector when bytes are NOT a RIFF/WAVE with a complete PCM
// (fmt format 1, 16 bits) chunk followed by a complete data chunk: the caller
// keeps the original sound (SPEC §4.5 - non-PCM plays unamplified).
std::vector<uint8_t> apply_pcm_gain(const std::vector<uint8_t>& bytes, int percent);

}  // namespace poe2
