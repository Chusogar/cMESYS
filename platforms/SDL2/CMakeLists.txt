cmake_minimum_required(VERSION 3.10)

project(cmesys_sdl2
  LANGUAGES C
  DESCRIPTION "cMESYS Track & Field — SDL2 desktop build"
)

set(COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/../../common")

set(SOURCES
  main.c
  ${COMMON_DIR}/tf_core.c
  ${COMMON_DIR}/mc6809.c
  ${COMMON_DIR}/rom_data.c
  ${COMMON_DIR}/z80/z80.c
  ${COMMON_DIR}/sn76489.c
  ${COMMON_DIR}/vlm5030.c
)

add_executable(cmesys ${SOURCES})
set_target_properties(cmesys PROPERTIES C_STANDARD 99)

if (MSVC)
  target_compile_options(cmesys PRIVATE /W4)
else()
  target_compile_options(cmesys PRIVATE -Wall -Wextra -O2
    -Wno-unused-variable -Wno-unused-function -Wno-sign-compare
    -Wno-implicit-fallthrough)
endif()

target_compile_definitions(cmesys PRIVATE MC6809_NO_SETJMP NDEBUG)
target_include_directories(cmesys PRIVATE ${COMMON_DIR})

if (EMSCRIPTEN)
  set(CMAKE_EXECUTABLE_SUFFIX ".html")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s USE_SDL=2")
else()
  find_package(SDL2 REQUIRED)
  if (TARGET SDL2::SDL2)
    target_link_libraries(cmesys PRIVATE SDL2::SDL2)
  else()
    target_include_directories(cmesys PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(cmesys PRIVATE ${SDL2_LIBRARIES})
  endif()
endif()

# Headless regression helper (PPM + raw audio), same as former host/host_test.c
add_executable(cmesys_host_test
  ${CMAKE_CURRENT_LIST_DIR}/../../host/host_test.c
  ${COMMON_DIR}/tf_core.c
  ${COMMON_DIR}/mc6809.c
  ${COMMON_DIR}/rom_data.c
  ${COMMON_DIR}/z80/z80.c
  ${COMMON_DIR}/sn76489.c
  ${COMMON_DIR}/vlm5030.c
)
target_include_directories(cmesys_host_test PRIVATE ${COMMON_DIR})
target_compile_definitions(cmesys_host_test PRIVATE MC6809_NO_SETJMP NDEBUG)
target_compile_options(cmesys_host_test PRIVATE -O2
  -Wno-unused-variable -Wno-unused-function -Wno-sign-compare
  -Wno-implicit-fallthrough)
