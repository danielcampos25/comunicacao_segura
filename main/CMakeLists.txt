idf_component_register(
    SRCS "transmissor_lora.c"
    PRIV_REQUIRES spi_flash mbedtls ra01s esp_driver_spi esp_driver_gpio
    INCLUDE_DIRS ""
)
