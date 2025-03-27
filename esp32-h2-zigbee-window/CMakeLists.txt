# CMakeLists для проекта умного окна на базе ESP32-H2 с ZigBee

cmake_minimum_required(VERSION 3.5)

# Включаем компоненты ESP-IDF
set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/led_strip
                         ${CMAKE_CURRENT_LIST_DIR}/components)

# Имя проекта
project(zigbee-smart-window)

# Добавляем компоненты проекта
include($ENV{IDF_PATH}/tools/cmake/project.cmake) 