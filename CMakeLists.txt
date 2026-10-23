cmake_minimum_required(VERSION 3.16)
project(claude-beep CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(AYATANA REQUIRED IMPORTED_TARGET ayatana-appindicator3-0.1)

add_executable(claude-beep
    src/main.cpp
    src/state.cpp
    src/sound.cpp
    src/notify.cpp
    src/indicator.cpp
)

target_link_libraries(claude-beep PRIVATE PkgConfig::GTK3 PkgConfig::AYATANA)
target_compile_options(claude-beep PRIVATE -Wall -Wextra)
