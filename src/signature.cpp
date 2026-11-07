#include "signature.h"

#include <cstring>
#include <set>
#include <vector>

namespace poe2 {

namespace {

// "48 83 EC ?? 48 8B F1 33 ED 48 39 2D ^ ?? ?? ?? ??" - the 12 bytes before the
// anchor (kFixed), then a 4-byte signed displacement. -1 marks the "??"; the '^' is
// not a byte, it is the anchor position (pattern index 12).
constexpr int kFixed[12] = {0x48, 0x83, 0xEC, -1, 0x48, 0x8B, 0xF1, 0x33,
                            0xED, 0x48, 0x39, 0x2D};
constexpr uint32_t kAnchor = 12;   // disp32 lives at pattern index 12
constexpr uint32_t kPatLen = 16;   // 12 fixed bytes + 4 displacement bytes

}  // namespace

std::vector<SigHit> scan_signature(IMemoryReader& mem, uint64_t base, uint32_t size,
                                   uint32_t chunk) {
    std::vector<SigHit> hits;
    if (size == 0 || chunk < kPatLen) return hits;

    // Chunks overlap by kPatLen-1 bytes: a match that straddles the end of one
    // chunk is found whole in the next one, and already-seen anchors are dropped, so
    // the overlap cannot double-count. (poe_scan.py skipped such matches outright -
    // a match on the boundary was lost.)
    std::vector<uint8_t> buf(chunk);
    std::set<uint64_t> seen;
    uint32_t pos = 0;
    while (pos < size) {
        const uint32_t want = (size - pos < chunk) ? size - pos : chunk;
        size_t got = mem.read(base + pos, buf.data(), want);
        if (got > want) got = want;
        for (uint32_t i = 0; i + kPatLen <= got; ++i) {
            if (buf[i] != kFixed[0]) continue;
            bool ok = true;
            for (uint32_t j = 1; j < kAnchor; ++j) {
                if (kFixed[j] >= 0 && buf[i + j] != static_cast<uint8_t>(kFixed[j])) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
            int32_t disp = 0;
            std::memcpy(&disp, buf.data() + i + kAnchor, 4);
            const uint64_t anchor_live = base + pos + i + kAnchor;
            if (seen.insert(anchor_live).second) {
                hits.push_back({anchor_live, disp, anchor_live + disp + 4});
            }
        }
        if (got < kPatLen) {
            // Nothing usable in this chunk (unreadable page / region edge): move on
            // by the requested size, never by zero.
            pos += want > 0 ? want : 1;
        } else {
            const uint32_t overlap = kPatLen - 1;
            pos += (want > overlap) ? want - overlap : want;
        }
    }
    return hits;
}

}  // namespace poe2
