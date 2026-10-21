idf_component_register(
    SRCS "main.c" "nunchuk.c" "tf_core.c" "mc6809.c" "rom_data.c" "z80/z80.c" "sn76489.c" "vlm5030.c" "dac_audio.c"
    INCLUDE_DIRS "."
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
