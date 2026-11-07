#pragma once
// Signature scan of the main module image (SPEC §4.6): the pattern
// "48 83 EC ?? 48 8B F1 33 ED 48 39 2D ^ ?? ?? ?? ??" is a binary constant, not an
// offset; '^' marks the 4-byte signed displacement position and
// global_addr = anchor_live + disp + 4 gives the GameStates* static address.
// Pure core code on IMemoryReader (tests: tests/signature_tests.cpp).

#include <cstdint>
#include <vector>

#include "mem/reader.h"

namespace poe2 {

// SPEC §4.6: the image is scanned in 1 MB chunks.
constexpr uint32_t kSigChunk = 1u << 20;

struct SigHit {
    uint64_t anchor_live;  // live address of the disp32 anchor
    int32_t disp;          // signed 4-byte displacement at the anchor
    uint64_t global_addr;  // anchor_live + disp + 4 (RIP-relative target)
};

// All matches in [base, base+size). Chunks do not overlap: a match that runs past
// the end of the current chunk is skipped and picked up whole by the next chunk.
// A chunk read that comes back short (region edge / unreadable page) is scanned as
// far as the bytes that came; pos always advances by the requested size.
// The caller requires exactly one hit - the chain treats 0 or >1 hits as a failure.
std::vector<SigHit> scan_signature(IMemoryReader& mem, uint64_t base, uint32_t size,
                                   uint32_t chunk = kSigChunk);

}  // namespace poe2
