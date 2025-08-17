cmake_minimum_required(VERSION 3.16)

project(ZXSpectrum48KEmu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(ZX_HEADLESS "Build without SDL2 window/audio (headless)" OFF)

# Sources
file(GLOB_RECURSE ZX_SOURCES CONFIGURE_DEPENDS
	"src/*.cpp"
)

add_executable(zx48k ${ZX_SOURCES})

# Dependencies: SDL2 for video/audio/input
if(NOT ZX_HEADLESS)
	find_package(PkgConfig)
	if(PkgConfig_FOUND)
		pkg_search_module(SDL2 REQUIRED sdl2)
		target_include_directories(zx48k PRIVATE ${SDL2_INCLUDE_DIRS})
		target_link_libraries(zx48k PRIVATE ${SDL2_LIBRARIES})
	else()
		find_path(SDL2_INCLUDE_DIR SDL2/SDL.h)
		find_library(SDL2_LIBRARY NAMES SDL2)
		if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY)
			message(FATAL_ERROR "SDL2 not found. Install libsdl2-dev or set ZX_HEADLESS=ON.")
		endif()
		target_include_directories(zx48k PRIVATE ${SDL2_INCLUDE_DIR})
		target_link_libraries(zx48k PRIVATE ${SDL2_LIBRARY})
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

# Install rule (optional)
install(TARGETS zx48k RUNTIME DESTINATION bin)