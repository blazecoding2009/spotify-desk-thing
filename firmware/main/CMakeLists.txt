idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES spi_flash vfs fatfs sdmmc driver nvs_flash ili9488 gt911 encoder audio ui)
