/**
 * @file servo_control.h
 * @brief Модуль управления сервоприводами для умного окна
 */

#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Типы сервоприводов
 */
typedef enum {
    SERVO_TYPE_HANDLE = 0,   ///< Сервопривод для ручки окна
    SERVO_TYPE_GAP = 1       ///< Сервопривод для зазора окна
} servo_type_t;

/**
 * @brief Положение ручки окна
 */
typedef enum {
    HANDLE_POSITION_CLOSED = 0,    ///< Закрыто (0 градусов)
    HANDLE_POSITION_OPEN = 90,     ///< Открыто (90 градусов)
    HANDLE_POSITION_VENTILATE = 180 ///< Проветривание (180 градусов)
} handle_position_t;

/**
 * @brief Конфигурация сервопривода
 */
typedef struct {
    uint8_t gpio_pin;            ///< Номер GPIO для управления сервоприводом
    uint32_t min_pulse_width_us; ///< Минимальная ширина импульса (мкс)
    uint32_t max_pulse_width_us; ///< Максимальная ширина импульса (мкс)
    uint32_t max_angle_deg;      ///< Максимальный угол поворота (градусы)
    bool invert_direction;       ///< Инвертировать направление
} servo_config_t;

/**
 * @brief Инициализация модуля управления сервоприводами
 * 
 * @param handle_config Конфигурация сервопривода ручки
 * @param gap_config Конфигурация сервопривода зазора
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t servo_init(servo_config_t *handle_config, servo_config_t *gap_config);

/**
 * @brief Установка положения ручки окна
 * 
 * @param position Положение ручки (0° - закрыто, 90° - открыто, 180° - проветривание)
 * @return esp_err_t ESP_OK при успешной установке положения
 */
esp_err_t servo_set_handle_position(handle_position_t position);

/**
 * @brief Установка процента открытия зазора окна
 * 
 * @param percentage Процент открытия (0-100)
 * @return esp_err_t ESP_OK при успешной установке положения
 */
esp_err_t servo_set_gap_percentage(uint8_t percentage);

/**
 * @brief Получение текущего положения ручки
 * 
 * @return handle_position_t Текущее положение ручки
 */
handle_position_t servo_get_handle_position(void);

/**
 * @brief Получение текущего процента открытия зазора
 * 
 * @return uint8_t Текущий процент открытия (0-100)
 */
uint8_t servo_get_gap_percentage(void);

/**
 * @brief Калибровка сервопривода
 * 
 * @param type Тип сервопривода
 * @return esp_err_t ESP_OK при успешной калибровке
 */
esp_err_t servo_calibrate(servo_type_t type);

/**
 * @brief Проверка механического сопротивления
 * 
 * @param type Тип сервопривода
 * @return bool true если обнаружено сопротивление
 */
bool servo_check_resistance(servo_type_t type);

/**
 * @brief Остановка сервопривода
 * 
 * @param type Тип сервопривода
 * @return esp_err_t ESP_OK при успешной остановке
 */
esp_err_t servo_stop(servo_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_CONTROL_H */ 