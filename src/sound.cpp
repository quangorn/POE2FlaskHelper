#include "sound.h"

#include <vector>

#include "wav.h"

namespace poe2 {

namespace {

// RT_RIFF (11): the resource type app.rc's WAVE statement produces.
constexpr uint16_t kRtRiff = 11;

}  // namespace

bool play_wav(uint16_t res_id, HINSTANCE hInst, int volume_percent) {
    HRSRC res = FindResourceW(hInst, MAKEINTRESOURCEW(res_id), MAKEINTRESOURCEW(kRtRiff));
    HGLOBAL loaded = res ? LoadResource(hInst, res) : nullptr;
    const size_t size = loaded ? SizeofResource(hInst, res) : 0;
    const uint8_t* data = loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
    if (data && size > 0) {
        std::vector<uint8_t> gain = apply_pcm_gain(
            std::vector<uint8_t>(data, data + size), volume_percent);
        if (!gain.empty()) {
            // SND_MEMORY: pdSound is a WAV-data pointer, the signature takes LPCWSTR.
            return PlaySoundW(reinterpret_cast<LPCWSTR>(gain.data()), hInst,
                              SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != 0;
        }
    }
    // Missing resource / non-PCM16 payload: the original resource path (SPEC §4.5).
    return PlaySoundW(MAKEINTRESOURCEW(res_id), hInst,
                      SND_RESOURCE | SND_ASYNC | SND_NODEFAULT) != 0;
}

void stop_sound() {
    PlaySoundW(nullptr, nullptr, SND_ASYNC | SND_NODEFAULT);
}

}  // namespace poe2
