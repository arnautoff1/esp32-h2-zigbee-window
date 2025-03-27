idf_component_register(
    SRCS 
        "main.c"
        "zigbee_handler.c"
        "power_management.c"
        "ota_update.c"
        "state_management.c"
        "servo_control.c"
    INCLUDE_DIRS "."
    REQUIRES esp_zb
) 