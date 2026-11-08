#ifndef POE2_RES_IDS_H
#define POE2_RES_IDS_H
// Win32 resource ids: one source of truth for res/app.rc, the C++ code and the tests.
// Classic #defines (not a namespace) so rc.exe can include this file too
// (CMakeLists adds -I src to CMAKE_RC_FLAGS). The pure state -> WAV mapping in
// state.cpp returns these ids (SPEC §4.5), PlaySound consumes them.
#define IDI_APP_ICON   101
#define ID_ENABLE_WAV  102
#define ID_DISABLE_WAV 103
// reload.wav joins the WAV family on config.json hot reload (plan-hot-reload.md §3):
// app.rc compiles it as the same named WAVE type + language 0x409 the enable/disable
// WAVs use, so play_wav's FindResourceExW(L"WAVE", MAKEINTRESOURCEW(id),
// MAKELANGID(ENGLISH, US)) lookup resolves it too (SPEC §4.5, lesson of 1.1.1).
#define ID_RELOAD_WAV  104
#endif  // POE2_RES_IDS_H
