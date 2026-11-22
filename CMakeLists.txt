# Arcade / computer ports for the ESP32 CYD: one machine per firmware.
#   idf.py -DGAME=indytemp build
# GAME: indytemp | roadrunn | gauntlet | outrun | snowbros | trackfld | pbobble |
#       retofinv | robocop | ql | spectrum48 | spectrum128 | plus3 | pentagon | pentagon1024 | scorpion
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED GAME)
    if(DEFINED ENV{GAME})
        set(GAME $ENV{GAME})
    else()
        set(GAME indytemp)
    endif()
endif()
set(GAME ${GAME} CACHE STRING "Machine to build (arcade GAME or ql / spectrum48 / spectrum128 / plus3 / pentagon / pentagon1024 / scorpion)")
# BOARD: cyd (the ESP32 Cheap Yellow Display, by default) or vga32 (LilyGO TTGO
# VGA32 v1.4: VGA, PS/2, 4 MB of PSRAM; its own sdkconfig in the build folder).
if(NOT DEFINED BOARD)
    if(DEFINED ENV{BOARD})
        set(BOARD $ENV{BOARD})
    else()
        set(BOARD cyd)
    endif()
endif()
set(BOARD ${BOARD} CACHE STRING "Board: cyd or vga32")
if(BOARD STREQUAL "vga32")
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.vga32")
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(GAME ${GAME})          # read by main/CMakeLists.txt
idf_build_set_property(BOARD ${BOARD})
project(${GAME}_${BOARD})
