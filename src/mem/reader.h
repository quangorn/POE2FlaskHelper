#pragma once
// IMemoryReader / IMemoryReaderFactory (plan.md §1). Win32-free: the exe layer
// implements it on ReadProcessMemory (src/mem/win_reader.*), the tests on
// FakeMemory (tests/fakes/FakeMemory.*). poe2_core code (signature/chain/session)
// only sees this interface.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace poe2 {

struct IMemoryReader {
    virtual ~IMemoryReader() = default;
    // Bytes actually read; 0 = the read failed (bad address, closed process,
    // region boundary). A short read is reported as the real byte count, which is
    // what ReadProcessMemory does at committed-region boundaries.
    virtual size_t read(uint64_t addr, void* buf, size_t n) = 0;
    // Main module base/size from the PEB + PE header (read once at open).
    virtual uint64_t image_base() const = 0;
    virtual uint32_t image_size() const = 0;
};

struct IMemoryReaderFactory {
    virtual ~IMemoryReaderFactory() = default;
    // OpenProcess(VM_READ|QUERY_INFORMATION) + PEB reads. nullptr when the process
    // cannot be opened (access denied, process exiting).
    virtual std::unique_ptr<IMemoryReader> open(uint32_t pid) = 0;
};

// Little-endian scalar reads on the interface (core code only). nullopt on a short
// or failed read: chain checks treat them as "read did not happen".
inline std::optional<uint64_t> rd_u64(IMemoryReader& m, uint64_t addr) {
    uint64_t v = 0;
    if (m.read(addr, &v, sizeof v) != sizeof v) return std::nullopt;
    return v;
}

inline std::optional<uint32_t> rd_u32(IMemoryReader& m, uint64_t addr) {
    uint32_t v = 0;
    if (m.read(addr, &v, sizeof v) != sizeof v) return std::nullopt;
    return v;
}

inline std::optional<uint8_t> rd_u8(IMemoryReader& m, uint64_t addr) {
    uint8_t v = 0;
    if (m.read(addr, &v, sizeof v) != sizeof v) return std::nullopt;
    return v;
}

}  // namespace poe2
