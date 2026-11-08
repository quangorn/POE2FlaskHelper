cmake_minimum_required(VERSION 3.16)
project(POE2FlaskHelper VERSION 1.2.3 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Version: build/version.txt for build.sh (zip naming), version.h for app.rc and main.cpp.
# The single source of the number is project(... VERSION) above (SPEC §2.4): the user-facing
# README must not hardcode it, so res/README.txt.in is generated with @PROJECT_VERSION@.
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${PROJECT_VERSION}")
configure_file("res/version.h.in" "${CMAKE_BINARY_DIR}/version.h" NEWLINE_STYLE CRLF)
configure_file("res/res_paths.rc.in" "${CMAKE_BINARY_DIR}/res_paths.rc" NEWLINE_STYLE CRLF)

# rc.exe finds generated headers via -I. src/ is added for res_ids.h (shared resource
# ids: app.rc, C++ code and tests use one definition). The flags are quoted: an
# unquoted "-I path" splits on spaces and breaks a project under a path with spaces
# (S0-2 — measured: the spaces-path build is green with the quoted flags). poe2_core's
# PUBLIC include dirs (see target_include_directories below) already propagate to the
# app.rc compile, so these -I are redundant (deleting this line also builds, proven in
# the spaces-path scratch build); decision: keep them quoted as an explicit safety net.
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} \"-I ${CMAKE_BINARY_DIR}\" \"-I ${CMAKE_CURRENT_SOURCE_DIR}/src\"")

# The manifest version must follow project(... VERSION), so app.manifest is generated
# from app.manifest.in (S0-5; same pattern as version.h.in / res_paths.rc.in).
configure_file("res/app.manifest.in" "${CMAKE_BINARY_DIR}/app.manifest" NEWLINE_STYLE CRLF)

# The zip's user document is generated too (SPEC §2.4): the shipped README carries the
# project version, so it cannot be a static file with a hardcoded number. Proven
# byte-parity: replacing @PROJECT_VERSION@ in res/README.txt.in gives the exact shipped
# bytes; CRLF matches the .gitattributes rule for *.txt. build.sh copies build/README.txt.
configure_file("res/README.txt.in" "${CMAKE_BINARY_DIR}/README.txt" NEWLINE_STYLE CRLF)

add_compile_options(/W4 /utf-8 /permissive- /EHsc /GR-)
add_compile_options("$<$<CONFIG:Release>:/O2>" "$<$<CONFIG:Release>:/Gy>")
# /MANIFEST:NO is not in the SPEC §2 link-flag list on purpose: the manifest is
# embedded by app.rc (RT_MANIFEST id 1, per-monitor DPI v2), and a link-time manifest
# would put a second one into the PE (S0-4).
add_link_options(/OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:NONE /MANIFEST:NO)

# CMake's MSVC defaults add /GR; drop it so our /GR- does not trigger D9025.
string(REPLACE " /GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Core logic, shared by the exe and the tests (plan.md §1): Win32-free, all OS and
# time interaction goes through the injected interfaces (IMemoryReader,
# IMemoryReaderFactory, IProcessProvider, IClock).
add_library(poe2_core STATIC
    src/args.cpp
    src/json.cpp
    src/log.cpp
    src/config.cpp
    src/config_snapshot.cpp
    src/offsets.cpp
    src/keys.cpp
    src/state.cpp
    src/signature.cpp
    src/chain.cpp
    src/vital_math.cpp
    src/vitals.cpp
    src/scanner_session.cpp
    src/wav.cpp
    src/app.cpp)
target_include_directories(poe2_core PUBLIC ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(poe2_core PUBLIC UNICODE _UNICODE)

# SPEC §2 libraries (SPEC.md:39): user32 kernel32 shell32 psapi gdi32 winmm. CMake's
# MSVC default CMAKE_CXX_STANDARD_LIBRARIES appends kernel32 user32 gdi32 advapi32
# shell32 ole32 oleaut32 uuid comdlg32 to every link line; those extras are CMake
# defaults, not app dependencies, and the linker drops unused imports -
# research/pe_res_check.py asserts the shipped exe imports none of
# advapi32/ole32/oleaut32/uuid/comdlg32. The list is restricted here so the link line
# is the SPEC §2 set; psapi/gdi32 are listed per target, and /OPT:REF drops their
# imports when nothing uses them, so the exe imports USER32, KERNEL32, SHELL32, WINMM
# plus the CRT DLLs. Both exes link with the restriction (S0-3).
if(MSVC)
    set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "libraries are listed per target (SPEC §2)" FORCE)
endif()

# App layer: tray/hotkey/sound are Win32 (SPEC §4.2: UI/GDI on the main thread), the
# scanner thread wrapper, the real process provider, the ReadProcessMemory reader, the
# SendInput key sender and the file_stamp metadata probe (plan-hot-reload.md §3) are the
# exe's OS edge - poe2_core units never compile them.
add_executable(POE2FlaskHelper WIN32
    src/main.cpp
    src/tray.cpp
    src/hotkeys.cpp
    src/sound.cpp
    src/file_stamp.cpp
    src/scanner.cpp
    src/game_process.cpp
    src/win_keys.cpp
    src/mem/win_reader.cpp
    res/app.rc)
target_link_libraries(POE2FlaskHelper PRIVATE
    poe2_core user32 kernel32 shell32 psapi gdi32 winmm)

enable_testing()
# All test TUs are compiled directly into the exe (never a static lib) so the
# TEST() static registrars cannot be dropped by the linker (tests/test.h header note).
add_executable(poe2_tests
    # GA-1: src/scanner.cpp - the exe's worker-thread layer - is compiled in here so the
    # start->stop->start cycle (tests/scanner_thread_tests.cpp) runs the real Scanner on
    # real Win32 events with the fakes of tests/session_fixture.h. It is test-only: the
    # shipped exe compiles scanner.cpp once (add_executable above), poe2_core never.
    src/scanner.cpp
    tests/test_main.cpp
    tests/test_tmp.cpp
    tests/args_tests.cpp
    tests/json_tests.cpp
    tests/config_tests.cpp
    tests/reload_tests.cpp
    tests/hotkey_tests.cpp
    tests/keymap_tests.cpp
    tests/state_tests.cpp
    tests/wav_tests.cpp
    tests/scanner_thread_tests.cpp
    tests/offsets_tests.cpp
    tests/log_tests.cpp
    tests/paths_tests.cpp
    tests/fake_memory_tests.cpp
    tests/signature_tests.cpp
    tests/chain_tests.cpp
    tests/vital_math_tests.cpp
    tests/vitals_tests.cpp
    tests/session_tests.cpp
    tests/vital_session_tests.cpp
    tests/system_clock_tests.cpp
    tests/synthetic_layout.cpp
    tests/fakes/FakeClock.cpp
    tests/fakes/FakeMemory.cpp
    tests/fakes/FakeProcess.cpp
    tests/fakes/RecordingKeySender.cpp)
# user32: keymap_tests runs MapVirtualKey (the real scan-code table the Win32Key
# sender sends, SPEC §4.8), shell32: args_tests compares the command-line split against
# CommandLineToArgvW (S1-10) - both test-only, the shipped exe deps stay the SPEC §2 set.
# kernel32: the GA-1 scanner-thread test runs the real event object
# (CreateEventW/WaitForSingleObject/SetEvent/CloseHandle) and samples
# GetProcessHandleCount for the GA-2 handle-count method - also SPEC §2, so the test
# exe stays inside the shipped library set.
target_link_libraries(poe2_tests PRIVATE poe2_core user32 shell32 kernel32)
target_include_directories(poe2_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
# Real rev34 offsets file for the compatibility test (SPEC §6, poe_scan.py --offs-file).
target_compile_definitions(poe2_tests PRIVATE
    "P2_REPO_OFFSETS=\"${CMAKE_CURRENT_SOURCE_DIR}/offsets.json\"")
# Relative command: works with both cygwin ctest and VS ctest (cwd = build dir).
add_test(NAME unit COMMAND poe2_tests.exe)
