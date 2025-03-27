/**
 * @file state_management.h
 * @brief Модуль управления состоянием для умного окна
 */

#ifndef STATE_MANAGEMENT_H
#define STATE_MANAGEMENT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "servo_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Режим работы окна
 */
typedef enum {
    WINDOW_MODE_CLOSED = 0,      ///< Окно закрыто
    WINDOW_MODE_OPEN = 1,        ///< Окно открыто
    WINDOW_MODE_VENTILATE = 2,   ///< Режим проветривания
    WINDOW_MODE_CUSTOM = 3       ///< Пользовательский режим
} window_mode_t;

/**
 * @brief Состояние окна
 */
typedef struct {
    window_mode_t mode;           ///< Текущий режим окна
    handle_position_t handle_pos; ///< Положение ручки
    uint8_t gap_percentage;       ///< Процент открытия зазора
    bool calibrated;              ///< Статус калибровки
    bool in_motion;               ///< Флаг движения
    uint32_t last_action_time;    ///< Время последнего действия
} window_state_t;

/**
 * @brief Конфигурация сохранения состояния
 */
typedef struct {
    bool save_to_nvs;             ///< Сохранять состояние в NVS
    uint32_t save_interval_ms;    ///< Интервал автоматического сохранения (мс)
    bool restore_on_boot;         ///< Восстанавливать состояние при загрузке
} state_config_t;

/**
 * @brief Инициализация модуля управления состоянием
 * 
 * @param config Конфигурация сохранения состояния
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t state_init(state_config_t *config);

/**
 * @brief Установка режима работы окна
 * 
 * @param mode Режим работы
 * @return esp_err_t ESP_OK при успешной установке режима
 */
esp_err_t state_set_window_mode(window_mode_t mode);

/**
 * @brief Установка положения ручки окна
 * 
 * @param position Положение ручки
 * @return esp_err_t ESP_OK при успешной установке положения
 */
esp_err_t state_set_handle_position(handle_position_t position);

/**
 * @brief Установка процента открытия зазора
 * 
 * @param percentage Процент открытия (0-100)
 * @return esp_err_t ESP_OK при успешной установке положения
 */
esp_err_t state_set_gap_percentage(uint8_t percentage);

/**
 * @brief Получение текущего состояния окна
 * 
 * @return window_state_t Текущее состояние окна
 */
window_state_t state_get_window_state(void);

/**
 * @brief Получение текущего режима работы окна
 * 
 * @return window_mode_t Текущий режим работы
 */
window_mode_t state_get_window_mode(void);

/**
 * @brief Сохранение текущего состояния
 * 
 * @return esp_err_t ESP_OK при успешном сохранении
 */
esp_err_t state_save(void);

/**
 * @brief Восстановление состояния
 * 
 * @return esp_err_t ESP_OK при успешном восстановлении
 */
esp_err_t state_restore(void);

/**
 * @brief Сброс состояния к заводским настройкам
 * 
 * @return esp_err_t ESP_OK при успешном сбросе
 */
esp_err_t state_factory_reset(void);

/**
 * @brief Обработчик задачи управления состоянием
 * 
 * Эта функция вызывается периодически для обработки состояния.
 * 
 * @return esp_err_t ESP_OK при успешной обработке
 */
esp_err_t state_task_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* STATE_MANAGEMENT_H */ 