#pragma once
// File stamp of config.json (plan-hot-reload.md §3, app layer): LAST_WRITE time + size,
// read with Win32. poe2::FileStamp is the pure core struct (src/config.h) - the stamp
// decision logic (stamp_changed, config_reload) lives in poe2_core and is tested without
// files. Reading the file TEXT is the existing app.h read_text_file: this unit adds no
// second reader, only the metadata probe the 5 s cycle needs.
#include <string>

#include "config.h"  // poe2::FileStamp

namespace poe2 {

// path: UTF-8 with normalized '/' separators (the Paths of app.h - CreateFileW accepts
// '/' as a separator). out is zeroed on failure. False when the file cannot be opened
// (missing, or an exclusive lock that shares nothing) - the caller of plan §3 keeps the
// old stamp and retries on the next cycle.
bool file_stamp(const std::string& path, FileStamp& out);

}  // namespace poe2
