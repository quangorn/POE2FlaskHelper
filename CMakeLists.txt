cmake_minimum_required(VERSION 3.16)
project(POE2FlaskHelper VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Version: build/version.txt for build.sh (zip naming), version.h for app.rc and main.cpp.
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${PROJECT_VERSION}")
configure_file("res/version.h.in" "${CMAKE_BINARY_DIR}/version.h" NEWLINE_STYLE CRLF)
configure_file("res/res_paths.rc.in" "${CMAKE_BINARY_DIR}/res_paths.rc" NEWLINE_STYLE CRLF)

# rc.exe finds generated headers via -I.
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I ${CMAKE_BINARY_DIR}")

add_compile_options(/W4 /utf-8 /permissive- /EHsc /GR-)
add_compile_options("$<$<CONFIG:Release>:/O2>" "$<$<CONFIG:Release>:/Gy>")
add_link_options(/OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:NONE /MANIFEST:NO)

# CMake's MSVC defaults add /GR; drop it so our /GR- does not trigger D9025.
string(REPLACE " /GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Core logic, shared by the exe and the tests (plan.md §1).
add_library(poe2_core STATIC src/core_placeholder.cpp)
target_include_directories(poe2_core PUBLIC ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(poe2_core PUBLIC UNICODE _UNICODE)

add_executable(POE2FlaskHelper WIN32 src/main.cpp res/app.rc)
target_link_libraries(POE2FlaskHelper PRIVATE
    poe2_core user32 kernel32 shell32 psapi gdi32 winmm)

enable_testing()
add_executable(poe2_tests tests/test_main.cpp)
target_link_libraries(poe2_tests PRIVATE poe2_core)
# Relative command: works with both cygwin ctest and VS ctest (cwd = build dir).
add_test(NAME unit COMMAND poe2_tests.exe)
