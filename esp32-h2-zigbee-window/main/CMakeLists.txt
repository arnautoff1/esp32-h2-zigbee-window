# CMakeLists для основного кода проекта умного окна

idf_component_register(SRCS "main.c"
                            "servo_control.c"
                            "zigbee_device.c"
                            "ota_update.c"
                            "power_management.c"
                            "state_management.c"
                      INCLUDE_DIRS "."
                      REQUIRES esp_zigbee_lib nvs_flash esp_timer esp_common) 