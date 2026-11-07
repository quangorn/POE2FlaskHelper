// Paths-from-exe-dir tests (SPEC §4.1): cwd must never influence paths.
#include "test.h"

#include <string>

#include "app.h"

TEST(paths_follow_exe_dir) {
    poe2::Paths p = poe2::Paths::from_exe_path("C:\\Users\\karam\\dist\\POE2FlaskHelper.exe");
    CHECK_EQ(p.dir, std::string("C:/Users/karam/dist/"));
    CHECK_EQ(p.config_json(), std::string("C:/Users/karam/dist/config.json"));
    CHECK_EQ(p.offsets_json(), std::string("C:/Users/karam/dist/offsets.json"));
    CHECK_EQ(p.log_file("logs/app.log"), std::string("C:/Users/karam/dist/logs/app.log"));
}

TEST(paths_forward_slash_input) {
    poe2::Paths p = poe2::Paths::from_exe_path("/mnt/c/app/x.exe");  // cygwin-style path
    CHECK_EQ(p.dir, std::string("/mnt/c/app/"));
    CHECK_EQ(p.config_json(), std::string("/mnt/c/app/config.json"));
}

TEST(paths_log_file_absolute) {
    poe2::Paths p = poe2::Paths::from_exe_path("D:/app/x.exe");
    CHECK_EQ(p.log_file("D:/logs/x.log"), std::string("D:/logs/x.log"));  // drive-absolute stays
    CHECK_EQ(p.log_file("logs/app.log"), std::string("D:/app/logs/app.log"));
}

TEST(paths_no_directory_separator) {
    poe2::Paths p = poe2::Paths::from_exe_path("x.exe");
    CHECK_EQ(p.dir, std::string("./"));
    CHECK_EQ(p.config_json(), std::string("./config.json"));
}
