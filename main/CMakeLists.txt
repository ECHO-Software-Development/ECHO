file(GLOB_RECURSE srcs "source/*.c")

idf_component_register(SRCS "source/main.c" "${srcs}"
                       PRIV_REQUIRES bt nvs_flash esp_driver_gpio
                       INCLUDE_DIRS "./include")
