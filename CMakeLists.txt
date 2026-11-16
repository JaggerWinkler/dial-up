cmake_minimum_required(VERSION 4.3)
project(dial_up)

set(CMAKE_CXX_STANDARD 26)

find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)
find_library(PORTAUDIO_LIBRARY portaudio)

add_executable(dial_up main.cpp
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
        src/speaker.h)

target_include_directories(dial_up PRIVATE ${PORTAUDIO_INCLUDE_DIR})
target_link_libraries(dial_up PRIVATE ${PORTAUDIO_LIBRARY})

if(WIN32)
    target_link_libraries(dial_up PRIVATE iphlpapi)
endif()
