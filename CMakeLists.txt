cmake_minimum_required(VERSION 3.16)
project(realtime_audio_pipeline VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Find Dependencies ──────────────────────────────────────────────────────────

find_package(PkgConfig REQUIRED)

pkg_check_modules(PULSEAUDIO REQUIRED libpulse-simple)
pkg_check_modules(SAMPLERATE REQUIRED samplerate)

# ── Executable ─────────────────────────────────────────────────────────────────

add_executable(audio_pipeline
    src/main.cpp
    src/audio_pipeline.cpp
)

target_include_directories(audio_pipeline PRIVATE
    src
    ${PULSEAUDIO_INCLUDE_DIRS}
    ${SAMPLERATE_INCLUDE_DIRS}
)

target_link_libraries(audio_pipeline PRIVATE
    ${PULSEAUDIO_LIBRARIES}
    ${SAMPLERATE_LIBRARIES}
    pthread
)

target_compile_options(audio_pipeline PRIVATE
    -Wall -Wextra -O2
    ${PULSEAUDIO_CFLAGS_OTHER}
    ${SAMPLERATE_CFLAGS_OTHER}
)

# ── Install ────────────────────────────────────────────────────────────────────

install(TARGETS audio_pipeline DESTINATION bin)
