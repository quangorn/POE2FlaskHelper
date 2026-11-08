#include "sound.h"

#include <vector>

#include "wav.h"

namespace poe2 {

namespace {

// The scaled WAV bytes of the last play. PlaySound with SND_MEMORY | SND_ASYNC
// keeps reading this buffer until the sound ends, so it outlives play_wav; the
// next play replaces it (per-play recomputation, SPEC §4.5), stop_sound purges
// before shutdown. Main thread only (SPEC §4.2), no shared state.
std::vector<uint8_t> g_play_buffer;

}  // namespace

bool play_wav(uint16_t res_id, HINSTANCE hInst, int volume_percent, Logger& logger) {
    // Resource lookup measured 2026-11-08: app.rc's WAVE statement compiles a NAMED
    // type "WAVE" (not the RT_RIFF ordinal), and the resources carry language 0x409
    // (app.rc Translation). FindResourceW expands to FindResourceExW with the system
    // default language, which finds nothing on a non-English system - the explicit
    // MAKELANGID(ENGLISH, US) lookup is the one that resolves.
    HRSRC res = FindResourceExW(hInst, L"WAVE", MAKEINTRESOURCEW(res_id),
                                MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
    HGLOBAL loaded = res ? LoadResource(hInst, res) : nullptr;
    const size_t size = loaded ? SizeofResource(hInst, res) : 0;
    const uint8_t* data = loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
    if (data && size > 0) {
        std::vector<uint8_t> gain = apply_pcm_gain(
            std::vector<uint8_t>(data, data + size), volume_percent);
        if (!gain.empty()) {
            g_play_buffer = std::move(gain);  // lives until the sound has finished
            // SND_MEMORY: pdSound is a WAV-data pointer, the signature takes LPCWSTR.
            const bool ok = PlaySoundW(reinterpret_cast<LPCWSTR>(g_play_buffer.data()), hInst,
                                       SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != 0;
            if (ok) {
                logger.log(LogLevel::Debug, "sound: scaled WAV played from memory, volume "
                                            + std::to_string(volume_percent) + "%");
                return true;
            }
            logger.log(LogLevel::Warn, "SND_MEMORY PlaySound failed - resource fallback");
        } else {
            // SPEC §4.5: not PCM16 16-bit - no scaling, the original plays.
            logger.log(LogLevel::Warn, "WAV resource is not PCM16 - playing unamplified");
        }
    } else {
        logger.log(LogLevel::Warn, "WAV resource not found in the exe - unamplified fallback");
    }
    // Fallback: the original resource path (SPEC §4.5).
    return PlaySoundW(MAKEINTRESOURCEW(res_id), hInst,
                      SND_RESOURCE | SND_ASYNC | SND_NODEFAULT) != 0;
}

void stop_sound() {
    PlaySoundW(nullptr, nullptr, SND_ASYNC | SND_NODEFAULT);
    g_play_buffer.clear();  // purge issued first, the buffer is free to release
}

}  // namespace poe2
