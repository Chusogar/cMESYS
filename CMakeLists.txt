# Arcade / computer ports for the ESP32 CYD: one machine per firmware.
#   idf.py -DGAME=indytemp build
# GAME: indytemp | roadrunn | gauntlet | outrun | snowbros | trackfld | pbobble |
#       retofinv | robocop | ql | spectrum48 | spectrum128 | plus3 | pentagon | scorpion
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED GAME)
    if(DEFINED ENV{GAME})
        set(GAME $ENV{GAME})
    else()
        set(GAME indytemp)
    endif()
endif()
set(GAME ${GAME} CACHE STRING "Machine to build (arcade GAME or ql / spectrum48 / spectrum128 / plus3 / pentagon / scorpion)")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(GAME ${GAME})          # read by main/CMakeLists.txt
project(${GAME}_cyd)
