set(COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../common")

idf_component_register(
    SRCS
        "main.c" "nunchuk.c" "dac_audio.c"
        "${COMMON_DIR}/tf_core.c"
        "${COMMON_DIR}/mc6809.c"
        "${COMMON_DIR}/rom_data.c"
        "${COMMON_DIR}/z80/z80.c"
        "${COMMON_DIR}/sn76489.c"
        "${COMMON_DIR}/vlm5030.c"
    INCLUDE_DIRS "." "${COMMON_DIR}"
    REQUIRES esp_driver_i2c esp_lcd esp_driver_spi esp_driver_gpio esp_driver_dac esp_driver_uart nvs_flash esp_timer
    LDFRAGMENTS "linker.lf")

target_compile_definitions(${COMPONENT_LIB} PRIVATE MC6809_NO_SETJMP NDEBUG)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-variable -Wno-maybe-uninitialized -Wno-uninitialized
    -Wno-unused-function -Wno-implicit-fallthrough -Wno-sign-compare)

if(DEFINED ENV{TF_HEADLESS_TEST})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TF_HEADLESS_TEST)
endif()
if(DEFINED ENV{TF_QEMU_DAC})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TF_QEMU_DAC)
endif()
if(DEFINED ENV{LCD_SPI_MHZ})
    math(EXPR _hz "$ENV{LCD_SPI_MHZ} * 1000000")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LCD_SPI_HZ=${_hz})
endif()
