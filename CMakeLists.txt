cmake_minimum_required(VERSION 4.3)
project(dial_up)

set(CMAKE_CXX_STANDARD 26)

add_executable(dial_up main.cpp
        src/util.h
        src/data_link_layer.cpp
        src/data_link_layer.h)
