/**
 * @file state_management.h
 * @brief Модуль управления состоянием устройства
 */

#ifndef STATE_MANAGEMENT_H
#define STATE_MANAGEMENT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "servo_control.h"

/**
 * @brief Структура данных состояния устройства
 */
typedef struct {
    window_mode_t window_mode;     ///< Режим окна
    uint8_t gap_percentage;        ///< Процент открытия зазора
    bool calibrated;               ///< Флаг калибровки
    uint32_t last_activity_time;   ///< Время последней активности в миллисекундах
    bool resistance_detected;      ///< Флаг обнаружения сопротивления
} device_state_t;

/**
 * @brief Инициализация модуля управления состоянием
 * 
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t state_init(void);

/**
 * @brief Сохранение текущего состояния в энергонезависимую память
 * 
 * @return esp_err_t ESP_OK при успешном сохранении
 */
esp_err_t state_save(void);

/**
 * @brief Загрузка состояния из энергонезависимой памяти
 * 
 * @return esp_err_t ESP_OK при успешной загрузке
 */
esp_err_t state_load(void);

/**
 * @brief Сброс состояния к заводским настройкам
 * 
 * @return esp_err_t ESP_OK при успешном сбросе
 */
esp_err_t state_reset(void);

/**
 * @brief Получение текущего состояния устройства
 * 
 * @return device_state_t Структура с текущим состоянием
 */
device_state_t state_get_current(void);

/**
 * @brief Обновление режима окна
 * 
 * @param mode Новый режим окна
 * @return esp_err_t ESP_OK при успешном обновлении
 */
esp_err_t state_update_window_mode(window_mode_t mode);

/**
 * @brief Обновление процента открытия зазора
 * 
 * @param percentage Новый процент открытия (0-100)
 * @return esp_err_t ESP_OK при успешном обновлении
 */
esp_err_t state_update_gap_percentage(uint8_t percentage);

/**
 * @brief Обновление флага калибровки
 * 
 * @param calibrated Новое значение флага калибровки
 * @return esp_err_t ESP_OK при успешном обновлении
 */
esp_err_t state_update_calibration(bool calibrated);

/**
 * @brief Обновление времени последней активности
 * 
 * @return esp_err_t ESP_OK при успешном обновлении
 */
esp_err_t state_update_activity_time(void);

/**
 * @brief Обновление флага обнаружения сопротивления
 * 
 * @param detected Новое значение флага обнаружения сопротивления
 * @return esp_err_t ESP_OK при успешном обновлении
 */
esp_err_t state_update_resistance_detected(bool detected);

/**
 * @brief Проверка необходимости калибровки
 * 
 * @return bool true, если требуется калибровка
 */
bool state_is_calibration_required(void);

/**
 * @brief Получение времени бездействия
 * 
 * @return uint32_t Время бездействия в миллисекундах
 */
uint32_t state_get_inactivity_time(void);

/**
 * @brief Проверка обнаружения сопротивления
 * 
 * @return bool true, если обнаружено сопротивление
 */
bool state_is_resistance_detected(void);

#endif /* STATE_MANAGEMENT_H */ 