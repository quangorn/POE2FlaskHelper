// WAV gain tests for src/wav.cpp (SPEC §4.5 sound.volume, backlog "Громкость WAV"):
// scaling, saturation, chunk/byte preservation, non-PCM and malformed fallbacks.
// Synthetic PCM buffers, no Win32 (SPEC §3.1).
#include "test.h"

#include <string>
#include <vector>

#include "wav.h"

using poe2::apply_pcm_gain;

namespace {

std::vector<uint8_t> bytes_of(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// PCM16 mono WAV: RIFF header + 16-byte fmt chunk + data chunk of int16 samples.
// format: 1 = PCM, 3 = IEEE float (the non-PCM case of the backlog note).
std::vector<uint8_t> make_wav(const std::vector<int16_t>& samples, uint16_t format) {
    std::vector<uint8_t> out;
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    };
    auto u16 = [&](uint16_t v) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    };
    const size_t data_len = samples.size() * 2;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    u32(4 + (8 + 16) + (8 + data_len));  // RIFF body: WAVE + fmt chunk + data chunk
    out.insert(out.end(), {'W', 'A', 'V', 'E'});
    out.insert(out.end(), {'f', 'm', 't', ' '});
    u32(16); u16(format); u16(1); u32(44100); u32(44100 * 2); u16(2); u16(16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    u32(static_cast<uint32_t>(data_len));
    for (int16_t s : samples) u16(static_cast<uint16_t>(s));
    return out;
}

// int16 sample i of the data chunk (payload starts at 12 + 8 + 16 + 8 = 44).
int16_t sample_at(const std::vector<uint8_t>& wav, size_t i) {
    const uint8_t* p = wav.data() + 44 + 2 * i;
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

}  // namespace

TEST(wav_gain_scaling) {
    auto w = make_wav({1000, -1000, 32767, -32768}, 1);
    auto half = apply_pcm_gain(w, 50);
    CHECK(!half.empty());
    CHECK_EQ(sample_at(half, 0), int16_t(500));
    CHECK_EQ(sample_at(half, 1), int16_t(-500));
    CHECK_EQ(sample_at(half, 2), int16_t(16383));   // truncation toward zero
    CHECK_EQ(sample_at(half, 3), int16_t(-16384));
    auto quarter = apply_pcm_gain(w, 25);            // the shipped config volume
    CHECK_EQ(sample_at(quarter, 0), int16_t(250));
    CHECK_EQ(sample_at(quarter, 2), int16_t(8191));
}

TEST(wav_gain_zero_and_full) {
    auto w = make_wav({12345, -12345}, 1);
    auto zero = apply_pcm_gain(w, 0);  // volume 0 = silence, the buffer stays valid
    CHECK(!zero.empty());
    CHECK_EQ(sample_at(zero, 0), int16_t(0));
    CHECK_EQ(sample_at(zero, 1), int16_t(0));
    auto full = apply_pcm_gain(w, 100);
    CHECK(full == w);  // 100% is byte-identical: headers, chunks and samples untouched
}

TEST(wav_gain_keeps_headers_and_extra_chunks) {
    auto w = make_wav({200}, 1);
    // A LIST chunk (odd size + pad byte) between fmt and data must survive untouched.
    std::vector<uint8_t> w2;
    w2.insert(w2.end(), w.begin(), w.begin() + 36);
    w2.insert(w2.end(), {'L', 'I', 'S', 'T'});            // chunk id at 36
    w2.insert(w2.end(), {5, 0, 0, 0});                    // size 5 at 40
    w2.insert(w2.end(), {'t', 'a', 'g', '!', 'x', 0});    // body + word-align pad at 44..49
    w2.insert(w2.end(), w.begin() + 36, w.end());         // data chunk at 50
    auto out = apply_pcm_gain(w2, 50);
    CHECK(!out.empty());
    CHECK_EQ(out.size(), w2.size());                      // byte layout preserved
    // Everything before the scaled payload (RIFF header, fmt, LIST) is unchanged.
    CHECK(std::vector<uint8_t>(out.begin(), out.begin() + 54)
          == std::vector<uint8_t>(w2.begin(), w2.begin() + 54));
    CHECK_EQ(sample_at(out, 0), int16_t(100));            // data chunk scaled
}

TEST(wav_non_pcm_returns_empty) {
    auto w = make_wav({1000}, 3);  // IEEE float (format 3): no scaling, play original
    CHECK(apply_pcm_gain(w, 25).empty());
}

TEST(wav_malformed_returns_empty) {
    CHECK(apply_pcm_gain(bytes_of("RIFF"), 25).empty());
    CHECK(apply_pcm_gain(bytes_of("not a wav"), 25).empty());
    auto w = make_wav({1000}, 1);
    w.resize(w.size() - 3);  // data chunk runs past the end of the file
    CHECK(apply_pcm_gain(w, 25).empty());
    // "WAVE" marker missing
    w = make_wav({1000}, 1);
    w[8] = 'X';
    CHECK(apply_pcm_gain(w, 25).empty());
}

TEST(wav_odd_data_chunk_scales_whole_samples_only) {
    auto w = make_wav({200}, 1);
    // Grow the data chunk to 3 bytes (odd): one whole sample + one trailing byte.
    w[40] = 3;  // data chunk size field (data chunk starts at pos 36, size at 40)
    w.push_back(0x77);
    auto out = apply_pcm_gain(w, 50);
    CHECK(!out.empty());
    CHECK_EQ(out.size(), w.size());
    CHECK_EQ(sample_at(out, 0), int16_t(100));  // whole sample scaled
    CHECK_EQ(out.back(), 0x77);                 // the odd trailing byte untouched
}
