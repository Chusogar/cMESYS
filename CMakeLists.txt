# Arcade ports for the ESP32 CYD: one game per firmware.
#   idf.py -DGAME=indytemp build      (indytemp | snowbros | trackfld)
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED GAME)
    if(DEFINED ENV{GAME})
        set(GAME $ENV{GAME})
    else()
        set(GAME indytemp)
    endif()
endif()
set(GAME ${GAME} CACHE STRING "Game to build: indytemp, snowbros or trackfld")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(GAME ${GAME})          # read by main/CMakeLists.txt
project(${GAME}_cyd)
