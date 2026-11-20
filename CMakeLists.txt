cmake_minimum_required(VERSION 3.25)
project(dial_up C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release builds are self-contained single files: on Windows the C/C++ runtime
# is linked statically (no VC++ redistributable needed), and on macOS
# -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" makes a universal binary.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

# --- PortAudio (static) -----------------------------------------------------
# Built from source and linked statically so the executable doesn't need
# PortAudio installed. Linux needs the ALSA headers (libasound2-dev).
# PortAudio 19.7's CMakeLists predates CMake 4, which refuses minimums < 3.5.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PA_BUILD_STATIC ON  CACHE BOOL "" FORCE)
set(PA_USE_JACK     OFF CACHE BOOL "" FORCE)   # ALSA only on Linux: no libjack dependency
set(PA_USE_ASIO     OFF CACHE BOOL "" FORCE)   # ASIO needs Steinberg's SDK
set(PA_DLL_LINK_WITH_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
FetchContent_Declare(portaudio
        GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
        GIT_TAG        v19.7.0
        GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(portaudio)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# --- libcorrect (Reed-Solomon only) -----------------------------------------
# Download the source, but don't run libcorrect's own CMakeLists: it forces
# -fsanitize=address in Debug builds and builds its tests/tools. SOURCE_SUBDIR
# points at a folder with no CMakeLists.txt, so FetchContent only downloads.
FetchContent_Declare(libcorrect
        GIT_REPOSITORY https://github.com/quiet/libcorrect.git
        GIT_TAG        ee82e6673a806dfdf0a969b975ab36596ecc5401  # master, Feb 2026
        SOURCE_SUBDIR  do-not-build)
FetchContent_MakeAvailable(libcorrect)

add_library(correct_rs STATIC
        ${libcorrect_SOURCE_DIR}/src/reed-solomon/polynomial.c
        ${libcorrect_SOURCE_DIR}/src/reed-solomon/reed-solomon.c
        ${libcorrect_SOURCE_DIR}/src/reed-solomon/encode.c
        ${libcorrect_SOURCE_DIR}/src/reed-solomon/decode.c)
set_target_properties(correct_rs PROPERTIES C_STANDARD 99)
target_include_directories(correct_rs PUBLIC ${libcorrect_SOURCE_DIR}/include)

# Everything except main.cpp, so the app and the tests share one build.
add_library(dial_up_core STATIC
        src/util.h
        src/util.cpp
        src/modem.h
        src/data_link_layer.cpp
        src/data_link_layer.h
        src/reed_solomon.cpp
        src/reed_solomon.h
        src/interleaver.cpp
        src/interleaver.h
        src/convolutional.cpp
        src/convolutional.h
        src/fsk.cpp
        src/fsk.h
        src/speaker.cpp
        src/speaker.h
        src/message.h
        src/console.cpp
        src/console.h
        src/file.cpp
        src/file.h
        src/sender.cpp
        src/sender.h
        src/receiver.cpp
        src/receiver.h)

find_package(Threads REQUIRED)

target_include_directories(dial_up_core PUBLIC ${portaudio_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dial_up_core PUBLIC portaudio_static correct_rs Threads::Threads)

add_executable(dial_up main.cpp)
target_link_libraries(dial_up PRIVATE dial_up_core)

if(WIN32)
    target_link_libraries(dial_up_core PUBLIC iphlpapi)
endif()

enable_testing()
add_subdirectory(test)
