
if(CONFIG_APP_MODE_RECEIVER)
    set(APP_SOURCE "receptor_lora.c")
else()
    set(APP_SOURCE "transmissor_lora.c")
endif()

idf_component_register(
    SRCS "${APP_SOURCE}"
    PRIV_REQUIRES spi_flash mbedtls ra01s esp_driver_usb_serial_jtag
    INCLUDE_DIRS ""
)
