# Firmware for the Waveshare RP2040-PiZero (Pico SDK), one machine per build:
#
#   tools/setup_pico.sh                                   (once: Pico SDK + PicoDVI)
#   cmake -S main/ports/rp2040-pizero -B build/pico-robocop -DGAME=robocop
#   cmake --build build/pico-robocop -j                   -> build/pico-robocop/robocop_pizero.uf2
#
# or ./build_pico.sh for all of them (-> dist/). GAME: indytemp, roadrunn,
# gauntlet, outrun, snowbros, trackfld, pbobble, retofinv, robocop, polepos, polepos2, ql,
# spectrum48, spectrum128, plus3, pentagon, pentagon1024, scorpion.
cmake_minimum_required(VERSION 3.13)

get_filename_component(PORT_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(M "${PORT_DIR}/../.." ABSOLUTE)           # main/
get_filename_component(ROOT "${M}/.." ABSOLUTE)
set(PICO_DEPS "${ROOT}/deps/pico" CACHE PATH "Pico SDK and PicoDVI (tools/setup_pico.sh)")
if(NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "${PICO_DEPS}/pico-sdk")
endif()
set(PICO_PLATFORM rp2040 CACHE STRING "Pico platform")
set(PICO_BOARD waveshare_rp2040_pizero CACHE STRING "Board")

include(${PORT_DIR}/pico_sdk_import.cmake)
project(cmesys_pizero C CXX ASM)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
pico_sdk_init()

if(NOT GAME)
    message(FATAL_ERROR "-DGAME=<indytemp|roadrunn|gauntlet|outrun|snowbros|trackfld|pbobble|retofinv|robocop|polepos|polepos2|ql|spectrum48|spectrum128|plus3|pentagon|pentagon1024|scorpion>")
endif()
if(NOT EXISTS "${PICO_DEPS}/PicoDVI/software/libdvi")
    message(FATAL_ERROR "${PICO_DEPS}/PicoDVI is missing: run tools/setup_pico.sh first")
endif()
add_subdirectory(${PICO_DEPS}/PicoDVI/software/libdvi ${CMAKE_BINARY_DIR}/libdvi)

set(ZX_GAMES spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion)
set(IS_ZXSPEC 0)
set(QL_DIR ${M}/drivers/computers/ql)
if(GAME STREQUAL "ql")
    set(D ${M}/drivers/computers/${GAME})
elseif(GAME IN_LIST ZX_GAMES)
    set(D ${M}/drivers/computers/zxspec)
    set(IS_ZXSPEC 1)
else()
    set(D ${M}/drivers/arcades/${GAME})
endif()
set(AS1 ${M}/drivers/arcades/atarisy1)
set(M68K ${M}/cpu/m68k/m68kcpu.c ${M}/cpu/m68k/m68kops.c ${M}/cpu/m68k/m68kopac.c
         ${M}/cpu/m68k/m68kopdm.c ${M}/cpu/m68k/m68kopnz.c ${D}/m68k_tables.c)

# Per machine: sources, defines, the objects whose code runs from RAM (the
# RP2040's 16 KB flash cache cannot hold an emulator: every miss stalls the
# core), and the rows core 1 renders at a time.
set(BAND 4)
if(GAME STREQUAL "indytemp" OR GAME STREQUAL "roadrunn")
    set(SRCS ${D}/${GAME}_pico.c ${AS1}/atarisy1_core.c ${D}/rom_data.c ${M68K}
             ${M}/sound/ym2151.c ${M}/sound/pokey.c ${M}/sound/tms5220.c ${M}/machine/via6522.c
             ${M}/machine/slapstic.c ${M}/video/atari_mo.c)
    set(DEFS M68K_FLASH_TABLES AS1_HOOK_PC)
    set(RAM_CODE m68kcpu.c.o atarisy1_core.c.o pokey.c.o ym2151.c.o tms5220.c.o)
elseif(GAME STREQUAL "gauntlet")
    set(SRCS ${D}/gauntlet_pico.c ${D}/gauntlet_core.c ${D}/rom_data.c ${M68K}
             ${M}/sound/ym2151.c ${M}/sound/pokey.c ${M}/sound/tms5220.c ${M}/machine/slapstic.c ${M}/video/atari_mo.c)
    set(DEFS M68K_FLASH_TABLES)
    set(RAM_CODE m68kcpu.c.o gauntlet_core.c.o pokey.c.o ym2151.c.o tms5220.c.o)
elseif(GAME STREQUAL "outrun")
    set(SRCS ${D}/outrun_pico.c ${D}/or_core.c ${D}/or_video.c ${D}/or_sound.c ${D}/rom_data.c ${M68K}
             ${M}/cpu/z80/z80.c ${M}/sound/ym2151.c ${M}/sound/segapcm.c ${M}/machine/sega_315_5195.c)
    set(DEFS M68K_FLASH_TABLES)
    set(RAM_CODE m68kcpu.c.o)
    set(HOT_MAX 100)                # (RAM: 128 KB of the board's RAM + the sound board)
    set(BAND 2)
elseif(GAME STREQUAL "snowbros")
    set(SRCS ${D}/snowbros_pico.c ${D}/sb_core.c ${D}/rom_data.c ${M68K}
             ${M}/cpu/z80/z80.c ${M}/sound/emu8950/emu8950.c ${M}/sound/emu8950/emuadpcm.c)
    set(DEFS M68K_FLASH_TABLES)
    set(RAM_CODE m68kcpu.c.o z80.c.o emu8950.c.o sb_core.c.o)
elseif(GAME STREQUAL "trackfld")
    set(SRCS ${D}/trackfld_pico.c ${D}/tf_core.c ${D}/rom_data.c ${M}/cpu/mc6809/mc6809.c ${M}/cpu/z80/z80.c
             ${M}/sound/sn76489.c ${M}/sound/vlm5030.c)
    set(DEFS MC6809_NO_SETJMP)
    set(RAM_CODE mc6809.c.o z80.c.o tf_core.c.o vlm5030.c.o sn76489.c.o)
elseif(GAME STREQUAL "pbobble")
    set(SRCS ${D}/pbobble_pico.c ${D}/pb_core.c ${D}/rom_data.c ${M68K} ${M}/cpu/z80/z80.c
             ${M}/sound/ym2610.c ${M}/machine/tc0140syt.c ${M}/machine/eeprom93c46.c ${M}/video/tc0180vcu.c)
    set(DEFS M68K_FLASH_TABLES)
    set(RAM_CODE m68kcpu.c.o)
    set(BAND 2)                     # (RAM: 64 KB work RAM + 64 KB tile RAM + ...)
elseif(GAME STREQUAL "retofinv")
    set(SRCS ${D}/retofinv_pico.c ${D}/rv_core.c ${D}/rv_video.c ${D}/rom_data.c ${M}/cpu/z80/z80.c
             ${M}/cpu/m6805/m6805.c ${M}/machine/taito68705.c ${M}/sound/sn76489.c)
    set(DEFS)
    set(RAM_CODE z80.c.o rv_core.c.o rv_video.c.o m6805.c.o taito68705.c.o sn76489.c.o)
elseif(GAME STREQUAL "robocop")
    set(SRCS ${D}/robocop_pico.c ${D}/rc_core.c ${D}/rc_video.c ${D}/rc_sound.c ${D}/rom_data.c ${M68K}
             ${M}/cpu/h6280/h6280.c ${M}/sound/ym2610.c ${M}/sound/okim6295.c
             ${M}/sound/emu8950/emu8950.c ${M}/sound/emu8950/emuadpcm.c)
    set(DEFS M68K_FLASH_TABLES)
    set(RAM_CODE m68kcpu.c.o rc_core.c.o rc_sound.c.o ym2610.c.o emu8950.c.o)
elseif(GAME STREQUAL "polepos" OR GAME STREQUAL "polepos2")
    # core in polepos/, shared; the Z8002's helpers and busiest handlers go to
    # RAM through cpu/z8002/z8002_hot.h (.time_critical), not the whole core
    set(PP ${M}/drivers/arcades/polepos)
    set(SRCS ${D}/${GAME}_pico.c ${PP}/pp_core.c ${PP}/pp_video.c ${D}/rom_data.c ${M}/cpu/z80/z80.c
             ${M}/cpu/z8002/z8002.c ${M}/cpu/z8002/z8002_tab.c ${M}/cpu/mb88xx/mb88xx.c
             ${M}/machine/namco5x.c ${M}/sound/polepos_snd.c)
    set(DEFS)
    set(RAM_CODE z80.c.o mb88xx.c.o namco5x.c.o polepos_snd.c.o pp_core.c.o pp_video.c.o)
elseif(GAME STREQUAL "ql")
    set(SRCS ${D}/ql_pico.c ${D}/ql_core.c ${D}/ql_view.c ${D}/ql_cart_host.c ${D}/ql_sd_pico.c ${D}/rom_data.c ${M68K}
             ${M}/cpu/mcs48/mcs48.c ${M}/machine/zx8302.c ${M}/machine/ql_mdv.c ${M}/video/zx8301.c)
    set(DEFS M68K_FLASH_TABLES)
    set(RAM_CODE m68kcpu.c.o mcs48.c.o ql_core.c.o zx8302.c.o ql_mdv.c.o)
    # SD MISO is GP4; move PWM audio off that pin for the QL build.
    set(QL_AUDIO_PIN 22)
elseif(IS_ZXSPEC)
    set(SRCS ${D}/zxspec_pico.c ${D}/zxspec_core.c ${D}/zx48.c ${D}/zx128.c ${D}/zxclone.c ${D}/rom_data_${GAME}.c
             ${QL_DIR}/ql_sd_pico.c ${M}/cpu/z80dsp/z80dsp.c ${M}/machine/zx_tape.c ${M}/machine/zx_snap.c
             ${M}/machine/zx_rzx.c ${M}/machine/zx_inflate.c ${M}/machine/zx_beta.c ${M}/machine/zx_nec765.c
             ${M}/sound/ay8910.c)
    # the model's own file in RAM with the Z80 and the ULA (the others stay in flash)
    if(GAME STREQUAL "spectrum48")
        set(DEFS ZX_MODEL=ZX_SPEC48)
        set(ZX_HOT_OBJ zx48.c.o)
    elseif(GAME STREQUAL "spectrum128")
        set(DEFS ZX_MODEL=ZX_SPEC128)
        set(ZX_HOT_OBJ zx128.c.o)
    elseif(GAME STREQUAL "plus3")
        set(DEFS ZX_MODEL=ZX_PLUS3)
        set(ZX_HOT_OBJ zx128.c.o)
    elseif(GAME STREQUAL "pentagon")
        set(DEFS ZX_MODEL=ZX_PENTAGON)
        set(ZX_HOT_OBJ zxclone.c.o)
    elseif(GAME STREQUAL "pentagon1024")
        set(DEFS ZX_MODEL=ZX_PENTAGON1024)
        set(ZX_HOT_OBJ zxclone.c.o)
    else()
        set(DEFS ZX_MODEL=ZX_SCORPION)
        set(ZX_HOT_OBJ zxclone.c.o)
    endif()
    set(RAM_CODE z80dsp.c.o zxspec_core.c.o ${ZX_HOT_OBJ} ay8910.c.o)
    # Same SD MISO conflict as QL: keep PWM audio on GP22.
    set(QL_AUDIO_PIN 22)
else()
    message(FATAL_ERROR "Unknown GAME '${GAME}'")
endif()

# The game's hottest 68000 opcode handlers (its ESP32 .lf list) in RAM too
file(GLOB LF ${D}/*.lf)
if(LF AND DEFS MATCHES "M68K_FLASH_TABLES")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/pico_hot.py ${LF} ${CMAKE_BINARY_DIR}/m68k_hot.h ${HOT_MAX})
    set_source_files_properties(${M}/cpu/m68k/m68kopac.c ${M}/cpu/m68k/m68kopdm.c ${M}/cpu/m68k/m68kopnz.c
        PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_BINARY_DIR}/m68k_hot.h")
endif()

set(EXE ${GAME}_pizero)
add_executable(${EXE} ${SRCS} ${PORT_DIR}/pico_port.c)
target_include_directories(${EXE} PRIVATE ${PORT_DIR} ${D} ${AS1} ${QL_DIR} ${M}/drivers/arcades/polepos ${M}/cpu ${M}/cpu/m68k ${M}/cpu/m6502
    ${M}/cpu/mc6809 ${M}/cpu/mcs48 ${M}/cpu/z80 ${M}/sound ${M}/machine ${M}/video)
math(EXPR NBUF "${BAND} + 2")
target_compile_definitions(${EXE} PRIVATE ${DEFS} NDEBUG PICO_BAND=${BAND}
    DVI_VERTICAL_REPEAT=2 DVI_N_TMDS_BUFFERS=${NBUF}
    PICO_MALLOC_PANIC=0 RENDER_NO_OWN_BUFFERS
    PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0xE00)
if(DEFINED QL_AUDIO_PIN)
    target_compile_definitions(${EXE} PRIVATE PICO_AUDIO_PIN=${QL_AUDIO_PIN})
endif()
target_compile_definitions(libdvi INTERFACE DVI_VERTICAL_REPEAT=2 DVI_N_TMDS_BUFFERS=${NBUF})
target_compile_options(${EXE} PRIVATE -O2 -Wno-unused-variable -Wno-unused-function -Wno-sign-compare
    -Wno-maybe-uninitialized -Wno-unused-but-set-variable -Wno-implicit-fallthrough -Wno-psabi)
set_source_files_properties(${PORT_DIR}/pico_port.c PROPERTIES COMPILE_OPTIONS -fno-tree-loop-distribute-patterns)
target_link_libraries(${EXE} PRIVATE pico_stdlib pico_multicore pico_sync hardware_dma hardware_pio
    hardware_pwm hardware_clocks hardware_vreg hardware_interp hardware_spi libdvi tinyusb_host)

# Linker script: the SDK's, with the RAM_CODE objects' .text moved to RAM.
file(READ ${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2040/memmap_default.ld ld)
set(excl "")
foreach(o IN LISTS RAM_CODE)
    string(APPEND excl " *${o}")
endforeach()
string(REPLACE "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)"
               "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:${excl}) .text*)" ld2 "${ld}")
if(ld2 STREQUAL ld)
    message(FATAL_ERROR "memmap_default.ld of this SDK has no .text exclusion line to extend")
endif()
file(WRITE ${CMAKE_BINARY_DIR}/memmap_cmesys.ld "${ld2}")
pico_set_linker_script(${EXE} ${CMAKE_BINARY_DIR}/memmap_cmesys.ld)

# Test in the rp2040js simulator (one core, no HDMI / USB / sound): the
# emulation alone, frames logged on the UART.
option(PICO_EMU_TEST "Build for the rp2040js simulator" OFF)
if(PICO_EMU_TEST)
    target_compile_definitions(${EXE} PRIVATE PICO_EMU_TEST)
endif()

pico_enable_stdio_uart(${EXE} 1)
pico_enable_stdio_usb(${EXE} 0)
pico_set_program_name(${EXE} "cMESYS ${GAME}")
pico_add_extra_outputs(${EXE})
