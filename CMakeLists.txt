cmake_minimum_required(VERSION 4.3)
project(dial_up)

set(CMAKE_CXX_STANDARD 26)

find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)
find_library(PORTAUDIO_LIBRARY portaudio)

# --- libcorrect (Reed-Solomon only) -----------------------------------------
# Download the source, but don't run libcorrect's own CMakeLists: it forces
# -fsanitize=address in Debug builds and builds its tests/tools. SOURCE_SUBDIR
# points at a folder with no CMakeLists.txt, so FetchContent only downloads.
include(FetchContent)
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

target_include_directories(dial_up_core PUBLIC ${PORTAUDIO_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dial_up_core PUBLIC ${PORTAUDIO_LIBRARY} correct_rs Threads::Threads)

add_executable(dial_up main.cpp)
target_link_libraries(dial_up PRIVATE dial_up_core)

if(WIN32)
    target_link_libraries(dial_up_core PUBLIC iphlpapi)
endif()

enable_testing()
add_subdirectory(test)
