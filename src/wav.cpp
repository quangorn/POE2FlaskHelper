#include "wav.h"

#include <cstring>

namespace poe2 {

namespace {

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
           | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int16_t rd16(const uint8_t* p) {
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

// int32 scale + clamp, then store little-endian int16.
void wr16_scaled(uint8_t* p, int32_t sample, int percent) {
    int32_t v = sample * percent / 100;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    uint16_t u = static_cast<uint16_t>(v);
    p[0] = static_cast<uint8_t>(u & 0xFF);
    p[1] = static_cast<uint8_t>(u >> 8);
}

}  // namespace

std::vector<uint8_t> apply_pcm_gain(const std::vector<uint8_t>& bytes, int percent) {
    if (bytes.size() < 12) return {};
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0
        || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return {};
    }

    std::vector<uint8_t> out = bytes;
    const size_t n = out.size();
    bool fmt_pcm16 = false;
    bool data_done = false;
    size_t pos = 12;  // the first chunk after the RIFF header

    while (pos + 8 <= n) {
        const uint32_t size = rd32(out.data() + pos + 4);
        const size_t body = pos + 8;
        if (body + size > n) break;  // truncated trailing chunk -> not a valid WAV
        if (std::memcmp(out.data() + pos, "fmt ", 4) == 0) {
            if (size >= 16) {
                // format 1 = PCM, 16 bits per sample: the asset format (backlog note)
                fmt_pcm16 = rd16(out.data() + body) == 1 && rd16(out.data() + body + 6) == 16;
            }
        } else if (std::memcmp(out.data() + pos, "data", 4) == 0) {
            if (fmt_pcm16) {
                const size_t count = size / 2;  // whole int16 samples inside the chunk
                for (size_t i = 0; i < count; ++i) {
                    const uint8_t* p = out.data() + body + 2 * i;
                    wr16_scaled(out.data() + body + 2 * i, rd16(p), percent);
                }
            }
            data_done = fmt_pcm16;
            break;  // the data chunk is the payload, nothing scales after it
        }
        pos = body + size;
        if (size % 2) ++pos;  // RIFF chunks are word-aligned (pad byte)
    }
    return data_done ? out : std::vector<uint8_t>{};
}

}  // namespace poe2
