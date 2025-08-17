cmake_minimum_required(VERSION 3.16)

project(ZXSpectrum48KEmu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(ZX_HEADLESS "Build without SDL2 window/audio (headless)" OFF)

# Sources (exclude minivadr_main.cpp from zx48k)
file(GLOB_RECURSE ZX_SOURCES CONFIGURE_DEPENDS
	"src/*.cpp"
)
list(FILTER ZX_SOURCES EXCLUDE REGEX ".*minivadr_main\\.cpp$")

add_executable(zx48k ${ZX_SOURCES})

# MiniVadr runner (only if SDL2 found and not headless)
if(NOT ZX_HEADLESS)
	find_package(PkgConfig)
	if(PkgConfig_FOUND)
		pkg_search_module(SDL2 QUIET sdl2)
	endif()
	if(SDL2_FOUND)
		add_executable(minivadr src/minivadr_main.cpp)
		target_include_directories(minivadr PRIVATE ${SDL2_INCLUDE_DIRS})
		target_link_libraries(minivadr PRIVATE ${SDL2_LIBRARIES})
		target_compile_definitions(minivadr PRIVATE BUILD_MINIVADR_MAIN=1)
	endif()
endif()

# Dependencies: SDL2 for video/audio/input on zx48k
if(NOT ZX_HEADLESS)
	if(PkgConfig_FOUND AND SDL2_FOUND)
		target_include_directories(zx48k PRIVATE ${SDL2_INCLUDE_DIRS})
		target_link_libraries(zx48k PRIVATE ${SDL2_LIBRARIES})
	else()
		find_path(SDL2_INCLUDE_DIR SDL2/SDL.h)
		find_library(SDL2_LIBRARY NAMES SDL2)
		if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
			target_include_directories(zx48k PRIVATE ${SDL2_INCLUDE_DIR})
			target_link_libraries(zx48k PRIVATE ${SDL2_LIBRARY})
		else()
			message(STATUS "SDL2 not found; building zx48k without SDL-specific code paths")
		endif()
	endif()
	
	target_compile_definitions(zx48k PRIVATE ZX_WITH_SDL=1)
else()
	target_compile_definitions(zx48k PRIVATE ZX_HEADLESS=1)
endif()

# Warnings
if(MSVC)
	target_compile_options(zx48k PRIVATE /W4 /permissive-)
else()
	target_compile_options(zx48k PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Output directory
set_target_properties(zx48k PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(TARGET minivadr)
	set_target_properties(minivadr PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
	install(TARGETS minivadr RUNTIME DESTINATION bin)
endif()

# Install rule (optional)
install(TARGETS zx48k RUNTIME DESTINATION bin)