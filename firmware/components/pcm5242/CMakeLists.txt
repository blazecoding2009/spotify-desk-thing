idf_component_register(
    SRCS "pcm5242.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos esp_common esp_driver_i2c
)
