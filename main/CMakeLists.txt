# Sources of the selected game (GAME: indytemp, snowbros, trackfld).
idf_build_get_property(GAME GAME)
set(D drivers/arcades/${GAME})
set(PORT ports/esp32-cyd/cyd.c ports/esp32-cyd/dac_audio.c ports/esp32-cyd/nunchuk.c)
set(M68K cpu/m68k/m68kcpu.c cpu/m68k/m68kops.c cpu/m68k/m68kopac.c cpu/m68k/m68kopdm.c cpu/m68k/m68kopnz.c)

if(GAME STREQUAL "indytemp")
    # (cpu/m6502/dsp6502.c is compiled inside indytemp_core.c)
    set(SRCS ${D}/indytemp_cyd.c ${D}/indytemp_core.c ${D}/rom_data.c ${D}/m68k_tables.c ${M68K}
             sound/ym2151.c sound/pokey.c sound/tms5220.c machine/via6522.c machine/slapstic.c video/atari_mo.c)
    set(DEFS M68K_FLASH_TABLES INDY_HOOK_PC)
elseif(GAME STREQUAL "snowbros")
    set(SRCS ${D}/snowbros_cyd.c ${D}/sb_core.c ${D}/rom_data.c ${D}/m68k_tables.c ${M68K}
             cpu/z80/z80.c sound/emu8950/emu8950.c sound/emu8950/emuadpcm.c)
    set(DEFS M68K_FLASH_TABLES)
elseif(GAME STREQUAL "trackfld")
    set(SRCS ${D}/trackfld_cyd.c ${D}/tf_core.c ${D}/rom_data.c cpu/mc6809/mc6809.c cpu/z80/z80.c
             sound/sn76489.c sound/vlm5030.c)
    set(DEFS MC6809_NO_SETJMP)
else()
    message(FATAL_ERROR "Unknown GAME '${GAME}' (indytemp, snowbros, trackfld)")
endif()

idf_component_register(
    SRCS ${SRCS} ${PORT}
    INCLUDE_DIRS ${D} ports/esp32-cyd cpu cpu/m68k cpu/m6502 cpu/mc6809 sound machine video
    REQUIRES esp_driver_i2c esp_system xtensa esp_lcd esp_driver_spi esp_driver_gpio esp_driver_dac
             esp_driver_uart nvs_flash esp_timer
    LDFRAGMENTS ${D}/${GAME}.lf)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${DEFS} NDEBUG)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-variable -Wno-maybe-uninitialized -Wno-uninitialized
    -Wno-unused-function -Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-sign-compare -Wno-type-limits
    -Wno-missing-field-initializers -Wno-unused-value -Wno-parentheses -Wno-misleading-indentation -Wno-unused-label)
# QEMU tests: CYD_HEADLESS_TEST=1 idf.py ... (frame hashes on the serial port)
foreach(opt CYD_HEADLESS_TEST CYD_QEMU_DAC CYD_QEMU_FULL CYD_QEMU_SOLO)
    if(DEFINED ENV{${opt}})
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${opt})
    endif()
endforeach()
if(DEFINED ENV{LCD_SPI_MHZ})
    math(EXPR _hz "$ENV{LCD_SPI_MHZ} * 1000000")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LCD_SPI_HZ=${_hz})
endif()
# crash diagnostics (ports/esp32-cyd/cyd.c)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
