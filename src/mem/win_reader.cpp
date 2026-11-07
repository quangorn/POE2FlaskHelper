#include "mem/win_reader.h"

#include <memory>

namespace poe2 {

namespace {

// PROCESS_BASIC_INFORMATION, x64 layout (ProcessBasicInformation = 0). Only
// PebBaseAddress is read; its offset (8) matches the NT structure.
struct BasicInfo {
    long ExitStatus;
    long pad0;
    uint64_t PebBaseAddress;
    uint64_t AffinityMask;
    long BasePriority;
    long pad1;
    uint64_t UniqueProcessId;
    uint64_t InheritedFromUniqueProcessId;
};

typedef long NtStatus;
typedef NtStatus (NTAPI* NtQueryInfoProc)(HANDLE, int, void*, unsigned, unsigned*);

NtQueryInfoProc nt_query() {
    HMODULE ntdll = GetModuleHandleA("ntdll");
    if (!ntdll) return nullptr;
    return reinterpret_cast<NtQueryInfoProc>(GetProcAddress(ntdll, "NtQueryInformationProcess"));
}

// Read n bytes from the target with the exact-byte-count check.
bool rd(HANDLE h, uint64_t addr, void* out, size_t n) {
    SIZE_T got = 0;
    return h && ReadProcessMemory(h, reinterpret_cast<LPCVOID>(addr), out,
                                  static_cast<SIZE_T>(n), &got) &&
           got == n;
}

}  // namespace

WinMemoryReader::WinMemoryReader(HANDLE h, uint64_t base, uint32_t size)
    : h_(h), base_(base), size_(size) {}

WinMemoryReader::~WinMemoryReader() {
    if (h_) CloseHandle(h_);
    h_ = nullptr;
}

size_t WinMemoryReader::read(uint64_t addr, void* buf, size_t n) {
    if (!h_ || n == 0) return 0;
    SIZE_T got = 0;
    if (!ReadProcessMemory(h_, reinterpret_cast<LPCVOID>(addr), buf,
                           static_cast<SIZE_T>(n), &got)) {
        return 0;
    }
    // A read crossing a committed-region boundary comes back short: report the real
    // byte count, the chain checks treat it as "the read did not complete".
    return static_cast<size_t>(got);
}

std::unique_ptr<IMemoryReader> WinMemoryReaderFactory::open(uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE,
                           static_cast<DWORD>(pid));
    if (!h) return nullptr;

    auto nt = nt_query();
    BasicInfo info{};
    unsigned got = 0;
    const NtStatus st = nt ? nt(h, 0 /* ProcessBasicInformation */, &info,
                                sizeof info, &got) : -1;
    if (st != 0 || !info.PebBaseAddress) {
        CloseHandle(h);
        return nullptr;
    }

    // PEB.ImageBaseAddress (x64 PEB offset 0x10) = the loaded base of the main module.
    uint64_t base = 0;
    if (!rd(h, info.PebBaseAddress + 0x10, &base, 8) || base == 0) {
        CloseHandle(h);
        return nullptr;
    }

    // SizeOfImage from the PE header: e_lfanew at base+0x3C, then OptionalHeader
    // (PE32+, magic 0x20b) offset 0x38. NOTE: poe_scan.py reads +0x40, which is the
    // CheckSum field of the same header - verified against kernel32.dll and this
    // project's own exe, 0x38 is the SizeOfImage field.
    uint32_t pe = 0;
    if (!rd(h, base + 0x3C, &pe, 4) || pe == 0 || pe > 0x10000) {
        CloseHandle(h);
        return nullptr;
    }
    uint32_t size = 0;
    if (!rd(h, base + pe + 0x18 + 0x38, &size, 4) || size == 0 || size > 0x10000000) {
        CloseHandle(h);
        return nullptr;
    }
    return std::make_unique<WinMemoryReader>(h, base, size);
}

}  // namespace poe2
