cmake_minimum_required(VERSION 2.8.11)
project(selyp C)

set(CMAKE_VERBOSE_MAKEFILE on)

file(GLOB SOURCES "./*.c")

set(C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall -ledit")

add_executable(selyp ${SOURCES})
