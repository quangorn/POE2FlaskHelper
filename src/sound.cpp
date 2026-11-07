#include "sound.h"

namespace poe2 {

bool play_wav(uint16_t res_id, HINSTANCE hInst) {
    return PlaySoundW(MAKEINTRESOURCEW(res_id), hInst,
                      SND_RESOURCE | SND_ASYNC | SND_NODEFAULT) != 0;
}

void stop_sound() {
    PlaySoundW(nullptr, nullptr, SND_ASYNC | SND_NODEFAULT);
}

}  // namespace poe2
