#pragma once
// Real IMemoryReader on ReadProcessMemory (SPEC §4.6, app/exe layer). Compiled into
// the exe only: poe2_core units stay Win32-free and the tests use FakeMemory.
// The reader owns the process handle; the scanner thread is its sole owner (SPEC §4.2).

#include <windows.h>

#include <cstdint>

#include "mem/reader.h"

namespace poe2 {

struct WinMemoryReader : IMemoryReader {
    WinMemoryReader(HANDLE h, uint64_t base, uint32_t size);
    ~WinMemoryReader() override;   // CloseHandle: the handle dies with the reader
    size_t read(uint64_t addr, void* buf, size_t n) override;
    uint64_t image_base() const override { return base_; }
    uint32_t image_size() const override { return size_; }

private:
    HANDLE h_ = nullptr;
    uint64_t base_ = 0;
    uint32_t size_ = 0;
};

// OpenProcess(VM_READ | QUERY_INFORMATION) + PEB reads. nullptr when the process
// cannot be opened (access denied, exiting process, no PEB). err_out (optional)
// receives the failure code: GetLastError() of the failed OpenProcess - with the game
// elevated this is ERROR_ACCESS_DENIED (SPEC §8) - or kOpenReaderFail after the PEB /
// PE-header reads fail; 0 on success. The code is what lets ScannerSession tell an
// admin-rights failure from a generic one without windows.h.
struct WinMemoryReaderFactory : IMemoryReaderFactory {
    std::unique_ptr<IMemoryReader> open(uint32_t pid, uint32_t* err_out) override;
};

}  // namespace poe2
