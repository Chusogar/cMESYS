# Sources of the selected game (GAME: indytemp, roadrunn, gauntlet, outrun, snowbros,
# trackfld, pbobble, retofinv, robocop, polepos, polepos2) or computer (GAME: ql, spectrum48, spectrum128,
# plus3, pentagon, pentagon1024, scorpion — Spectrum family shares drivers/computers/zxspec
# core; each model has its own rom_data_<GAME>.c from roms/<GAME>/).
idf_build_get_property(GAME GAME)
set(ZX_GAMES spectrum48 spectrum128 plus3 pentagon pentagon1024 scorpion)
set(IS_ZXSPEC 0)
if(GAME STREQUAL "ql")
    set(D drivers/computers/${GAME})
elseif(GAME IN_LIST ZX_GAMES)
    set(D drivers/computers/zxspec)
    set(IS_ZXSPEC 1)
else()
    set(D drivers/arcades/${GAME})
endif()
set(AS1 drivers/arcades/atarisy1)
set(QL_DIR drivers/computers/ql)
set(PP drivers/arcades/polepos)
set(PORT ports/esp32-cyd/cyd.c ports/esp32-cyd/dac_audio.c ports/esp32-cyd/nunchuk.c)
set(M68K cpu/m68k/m68kcpu.c cpu/m68k/m68kops.c cpu/m68k/m68kopac.c cpu/m68k/m68kopdm.c cpu/m68k/m68kopnz.c)
set(QL_SD_REQ)
set(LF_FILE ${D}/${GAME}.lf)
set(EXTRA_INCLUDES)

if(GAME STREQUAL "indytemp")
    # Atari System 1 core shared with roadrunn (cpu/m6502/dsp6502.c is compiled inside it)
    set(SRCS ${D}/indytemp_cyd.c ${AS1}/atarisy1_core.c ${D}/rom_data.c ${D}/m68k_tables.c ${M68K}
             sound/ym2151.c sound/pokey.c sound/tms5220.c machine/via6522.c machine/slapstic.c video/atari_mo.c)
    set(DEFS M68K_FLASH_TABLES AS1_HOOK_PC)
elseif(GAME STREQUAL "roadrunn")
    # Atari System 1 core shared with indytemp (cpu/m6502/dsp6502.c is compiled inside it)
    set(SRCS ${D}/roadrunn_cyd.c ${AS1}/atarisy1_core.c ${D}/rom_data.c ${D}/m68k_tables.c ${M68K}
             sound/ym2151.c sound/pokey.c sound/tms5220.c machine/via6522.c machine/slapstic.c video/atari_mo.c)
    set(DEFS M68K_FLASH_TABLES AS1_HOOK_PC)
elseif(GAME STREQUAL "gauntlet")
    # (cpu/m6502/dsp6502.c is compiled inside gauntlet_core.c)
    set(SRCS ${D}/gauntlet_cyd.c ${D}/gauntlet_core.c ${D}/rom_data.c ${D}/m68k_tables.c ${M68K}
             sound/ym2151.c sound/pokey.c sound/tms5220.c machine/slapstic.c video/atari_mo.c)
    set(DEFS M68K_FLASH_TABLES)
elseif(GAME STREQUAL "outrun")
    set(SRCS ${D}/outrun_cyd.c ${D}/or_core.c ${D}/or_video.c ${D}/or_sound.c ${D}/rom_data.c ${D}/m68k_tables.c ${M68K}
             cpu/z80/z80.c sound/ym2151.c sound/segapcm.c machine/sega_315_5195.c)
    set(DEFS M68K_FLASH_TABLES)
elseif(GAME STREQUAL "snowbros")
    set(SRCS ${D}/snowbros_cyd.c ${D}/sb_core.c ${D}/rom_data.c ${D}/m68k_tables.c ${M68K}
             cpu/z80/z80.c sound/emu8950/emu8950.c sound/emu8950/emuadpcm.c)
    set(DEFS M68K_FLASH_TABLES)
elseif(GAME STREQUAL "trackfld")
    set(SRCS ${D}/trackfld_cyd.c ${D}/tf_core.c ${D}/rom_data.c cpu/mc6809/mc6809.c cpu/z80/z80.c
             sound/sn76489.c sound/vlm5030.c)
    set(DEFS MC6809_NO_SETJMP)
elseif(GAME STREQUAL "pbobble")
    set(SRCS ${D}/pbobble_cyd.c ${D}/pb_core.c ${D}/rom_data.c ${D}/m68k_tables.c ${M68K}
             cpu/z80/z80.c sound/ym2610.c machine/tc0140syt.c machine/eeprom93c46.c video/tc0180vcu.c)
    set(DEFS M68K_FLASH_TABLES)
elseif(GAME STREQUAL "retofinv")
    set(SRCS ${D}/retofinv_cyd.c ${D}/rv_core.c ${D}/rv_video.c ${D}/rom_data.c cpu/z80/z80.c
             cpu/m6805/m6805.c machine/taito68705.c sound/sn76489.c)
    set(DEFS)
elseif(GAME STREQUAL "robocop")
    set(SRCS ${D}/robocop_cyd.c ${D}/rc_core.c ${D}/rc_video.c ${D}/rc_sound.c ${D}/rom_data.c ${D}/m68k_tables.c ${M68K}
             cpu/h6280/h6280.c sound/ym2610.c sound/okim6295.c
             sound/emu8950/emu8950.c sound/emu8950/emuadpcm.c)
    set(DEFS M68K_FLASH_TABLES)
elseif(GAME STREQUAL "polepos" OR GAME STREQUAL "polepos2")
    # Pole Position core (in polepos/) shared by both games
    set(SRCS ${D}/${GAME}_cyd.c ${PP}/pp_core.c ${PP}/pp_video.c ${D}/rom_data.c cpu/z80/z80.c
             cpu/z8002/z8002.c cpu/z8002/z8002_tab.c cpu/mb88xx/mb88xx.c machine/namco5x.c sound/polepos_snd.c)
    set(DEFS)
elseif(GAME STREQUAL "ql")
    set(SRCS ${D}/ql_cyd.c ${D}/ql_core.c ${D}/ql_view.c ${D}/ql_cart_host.c ${D}/ql_sd_esp.c ${D}/rom_data.c ${D}/m68k_tables.c ${M68K}
             cpu/mcs48/mcs48.c machine/zx8302.c machine/ql_mdv.c video/zx8301.c)
    set(DEFS M68K_FLASH_TABLES)
    set(QL_SD_REQ fatfs sdmmc)
elseif(IS_ZXSPEC)
    set(SRCS ${D}/zxspec_cyd.c ${D}/zxspec_core.c ${D}/zx48.c ${D}/zx128.c ${D}/zxclone.c ${D}/rom_data_${GAME}.c
             ${QL_DIR}/ql_sd_esp.c cpu/z80dsp/z80dsp.c machine/zx_tape.c machine/zx_snap.c machine/zx_rzx.c
             machine/zx_inflate.c machine/zx_pbuf.c machine/zx_beta.c machine/zx_nec765.c sound/ay8910.c)
    if(GAME STREQUAL "spectrum48")
        set(DEFS ZX_MODEL=ZX_SPEC48)
    elseif(GAME STREQUAL "spectrum128")
        set(DEFS ZX_MODEL=ZX_SPEC128)
    elseif(GAME STREQUAL "plus3")
        set(DEFS ZX_MODEL=ZX_PLUS3)
    elseif(GAME STREQUAL "pentagon")
        set(DEFS ZX_MODEL=ZX_PENTAGON)
    elseif(GAME STREQUAL "pentagon1024")
        set(DEFS ZX_MODEL=ZX_PENTAGON1024)
    else()
        set(DEFS ZX_MODEL=ZX_SCORPION)
    endif()
    set(QL_SD_REQ fatfs sdmmc)
    set(LF_FILE ${D}/zxspec.lf)
    set(EXTRA_INCLUDES ${QL_DIR})
else()
    message(FATAL_ERROR "Unknown GAME '${GAME}' (indytemp, roadrunn, gauntlet, outrun, snowbros, trackfld, pbobble, retofinv, robocop, polepos, polepos2, ql, spectrum48, spectrum128, plus3, pentagon, pentagon1024, scorpion)")
endif()

idf_component_register(
    SRCS ${SRCS} ${PORT}
    INCLUDE_DIRS ${D} ${AS1} ${EXTRA_INCLUDES} ${PP} ports/esp32-cyd cpu cpu/m68k cpu/m6502 cpu/mc6809 cpu/mcs48 cpu/z80 sound machine video
    REQUIRES esp_driver_i2c esp_system xtensa esp_lcd esp_driver_spi esp_driver_gpio esp_driver_dac
             esp_driver_uart nvs_flash esp_timer ${QL_SD_REQ}
    LDFRAGMENTS ${LF_FILE})
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${DEFS} NDEBUG)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-variable -Wno-maybe-uninitialized -Wno-uninitialized
    -Wno-unused-function -Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-sign-compare -Wno-type-limits
    -Wno-missing-field-initializers -Wno-unused-value -Wno-parentheses -Wno-misleading-indentation -Wno-unused-label)
# QEMU tests: CYD_HEADLESS_TEST=1 idf.py ... (frame hashes on the serial port)
foreach(opt CYD_HEADLESS_TEST CYD_QEMU_DAC CYD_QEMU_FULL CYD_QEMU_SOLO CYD_DIAG RC_NO_IRAM_DATA)
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
