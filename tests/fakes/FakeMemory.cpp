#include "fakes/FakeMemory.h"

#include <cstring>

namespace poe2 {
namespace testing {

namespace {

// (region bytes, offset of addr inside it); nullptr when addr is outside any region.
std::pair<std::vector<uint8_t>*, size_t> locate(std::map<uint64_t, std::vector<uint8_t>>& r,
                                               uint64_t addr) {
    auto it = r.upper_bound(addr);
    if (it == r.begin()) return {nullptr, 0};
    --it;
    if (addr >= it->first + it->second.size()) return {nullptr, 0};
    return {&it->second, static_cast<size_t>(addr - it->first)};
}

void store_le(uint8_t* p, uint64_t v, size_t n) {
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

}  // namespace

void FakeMemory::add_region(uint64_t base, size_t size, uint8_t fill) {
    regions_[base] = std::vector<uint8_t>(size, fill);
}

void FakeMemory::put_bytes(uint64_t addr, const uint8_t* p, size_t n) {
    auto [r, off] = locate(regions_, addr);
    if (!r || off + n > r->size()) return;   // test bug: outside the region, silent no-op
    std::memcpy(r->data() + off, p, n);
}

void FakeMemory::put_u64(uint64_t addr, uint64_t v) {
    auto [r, off] = locate(regions_, addr);
    if (!r || off + 8 > r->size()) return;
    store_le(r->data() + off, v, 8);
}

void FakeMemory::put_u32(uint64_t addr, uint32_t v) {
    auto [r, off] = locate(regions_, addr);
    if (!r || off + 4 > r->size()) return;
    store_le(r->data() + off, v, 4);
}

void FakeMemory::put_u8(uint64_t addr, uint8_t v) {
    auto [r, off] = locate(regions_, addr);
    if (!r || off + 1 > r->size()) return;
    (*r)[off] = v;
}

size_t FakeMemory::read(uint64_t addr, void* buf, size_t n) {
    ++reads_;
    last_size_ = 0;
    if (n == 0) return 0;
    if (fail_all_ || (fail_at_ > 0 && reads_ == fail_at_)) return 0;
    auto [r, off] = locate(regions_, addr);
    if (!r) return 0;
    size_t got = r->size() - off;
    if (got > n) got = n;   // short read at the region edge, like ReadProcessMemory
    std::memcpy(buf, r->data() + off, got);
    last_size_ = static_cast<int>(got);
    return got;
}

}  // namespace testing
}  // namespace poe2
