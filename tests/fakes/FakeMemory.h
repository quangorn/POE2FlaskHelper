#pragma once
// FakeMemory (plan.md §2): regions by address, short reads at region edges, read
// counter, failure injection on the N-th read. The read-count counter is what proves
// "scanner off => zero ReadProcessMemory" (SPEC §4.6, acceptance criterion 7).

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "mem/reader.h"

namespace poe2 {
namespace testing {

struct FakeMemory : IMemoryReader {
    void add_region(uint64_t base, size_t size, uint8_t fill = 0);
    void put_bytes(uint64_t addr, const uint8_t* p, size_t n);
    void put_u64(uint64_t addr, uint64_t v);
    void put_u32(uint64_t addr, uint32_t v);
    void put_u8(uint64_t addr, uint8_t v);

    // Bytes read; clipped at the region end (a read crossing the edge comes back
    // short, like ReadProcessMemory), 0 when the address is outside any region.
    size_t read(uint64_t addr, void* buf, size_t n) override;

    uint64_t image_base() const override { return base_; }
    uint32_t image_size() const override { return size_; }
    void set_image(uint64_t base, uint32_t size) { base_ = base; size_ = size; }

    int reads() const { return reads_; }
    void reset_reads() { reads_ = 0; }
    // Failure injection: the N-th read (1-based) returns 0 bytes.
    void fail_on_read(int n) { fail_at_ = n; }
    void fail_all_reads(bool on) { fail_all_ = on; }
    int last_read_size() const { return last_size_; }

private:
    std::map<uint64_t, std::vector<uint8_t>> regions_;
    uint64_t base_ = 0;
    uint32_t size_ = 0;
    int reads_ = 0;
    int last_size_ = -1;
    int fail_at_ = -1;
    bool fail_all_ = false;
};

// A shared FakeMemory behind the interface, so a factory can hand the same image to
// the session on every open and the read counter stays continuous across re-attaches.
struct SharedMemoryShim : IMemoryReader {
    std::shared_ptr<FakeMemory> mem;
    size_t read(uint64_t addr, void* buf, size_t n) override { return mem->read(addr, buf, n); }
    uint64_t image_base() const override { return mem->image_base(); }
    uint32_t image_size() const override { return mem->image_size(); }
};

// FakeProcessProvider scenarios ("no game / appeared / died / not foreground")
// paired with one shared FakeMemory image.
struct FakeMemFactory : IMemoryReaderFactory {
    std::shared_ptr<FakeMemory> mem;
    bool open_ok = true;   // false = OpenProcess fails
    // The failure code reported on a failed open (SPEC §8 fault injection):
    // 0 = a generic failure, poe2::kWinAccessDenied (5) = the admin-rights case.
    uint32_t open_err = 0;
    int opens = 0;

    std::unique_ptr<IMemoryReader> open(uint32_t pid, uint32_t* err_out) override {
        ++opens;
        if (!open_ok || !mem) {
            if (err_out) *err_out = open_err;
            return nullptr;
        }
        if (err_out) *err_out = 0;
        last_pid = pid;
        auto shim = std::make_unique<SharedMemoryShim>();
        shim->mem = mem;
        return shim;
    }
    uint32_t last_pid = 0;
};

}  // namespace testing
}  // namespace poe2
