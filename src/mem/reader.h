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

// Failure codes of open() (SPEC §8). The Win32 codes OpenProcess can return are plain
// uint32 values here so poe2_core and the tests stay Win32-free:
//   kWinAccessDenied - ERROR_ACCESS_DENIED: the game runs elevated, the helper does
//                      not. SPEC §8: error in the log + the "Admin rights required"
//                      tooltip.
//   kOpenReaderFail  - OpenProcess succeeded, the PEB / PE-header reads failed. This
//                      is a sentinel outside the Win32 system-error range (which ends
//                      near 16000), so it can never be read as a Win32 code.
constexpr uint32_t kWinAccessDenied = 5;  // ERROR_ACCESS_DENIED
constexpr uint32_t kOpenReaderFail = 100000;

struct IMemoryReaderFactory {
    virtual ~IMemoryReaderFactory() = default;
    // OpenProcess(VM_READ|QUERY_INFORMATION) + PEB reads. nullptr when the process
    // cannot be opened (access denied, process exiting). err_out (optional, may be
    // nullptr) receives the failure code for SPEC §8:
    //   0                  - success
    //   a Win32 error code - OpenProcess itself failed (the game is elevated and the
    //                        helper is not => kWinAccessDenied); win_reader passes
    //                        GetLastError() straight through
    //   kOpenReaderFail    - OpenProcess worked, the PEB/PE-header reads did not
    // The codes are plain uint32 so poe2_core and the tests stay Win32-free.
    virtual std::unique_ptr<IMemoryReader> open(uint32_t pid, uint32_t* err_out) = 0;
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
