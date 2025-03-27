set(COMPONENT_SRCDIRS ".")
set(COMPONENT_ADD_INCLUDEDIRS "include")

set(COMPONENT_REQUIRES "nvs_flash" "driver" "esp_timer")

register_component()

idf_component_register(SRCS "esp_zigbee_lib.c"
                      INCLUDE_DIRS "include"
                      REQUIRES nvs_flash esp_timer freertos) 