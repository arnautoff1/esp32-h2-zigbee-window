/**
 * @file power_management.h
 * @brief Модуль управления питанием для умного окна
 */

#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Состояние батареи
 */
typedef enum {
    BATTERY_STATE_NORMAL = 0,     ///< Нормальный заряд
    BATTERY_STATE_LOW = 1,        ///< Низкий заряд
    BATTERY_STATE_CRITICAL = 2,   ///< Критически низкий заряд
    BATTERY_STATE_CHARGING = 3,   ///< Заряжается
    BATTERY_STATE_EXTERNAL = 4    ///< Внешнее питание
} battery_state_t;

/**
 * @brief Режим питания
 */
typedef enum {
    POWER_MODE_NORMAL = 0,        ///< Нормальный режим
    POWER_MODE_LOW_POWER = 1,     ///< Режим низкого потребления
    POWER_MODE_DEEP_SLEEP = 2     ///< Режим глубокого сна
} power_mode_t;

/**
 * @brief Конфигурация управления питанием
 */
typedef struct {
    uint8_t battery_adc_channel;    ///< Канал АЦП для измерения напряжения батареи
    uint8_t external_power_gpio;    ///< GPIO для определения внешнего питания
    float low_battery_threshold;    ///< Порог низкого заряда батареи (В)
    float critical_battery_threshold; ///< Порог критического заряда батареи (В)
    uint32_t check_interval_ms;     ///< Интервал проверки состояния батареи (мс)
} power_config_t;

/**
 * @brief Инициализация модуля управления питанием
 * 
 * @param config Конфигурация модуля управления питанием
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t power_init(power_config_t *config);

/**
 * @brief Получение текущего напряжения батареи
 * 
 * @return float Текущее напряжение батареи (В)
 */
float power_get_battery_voltage(void);

/**
 * @brief Получение процента заряда батареи
 * 
 * @return uint8_t Процент заряда (0-100)
 */
uint8_t power_get_battery_percentage(void);

/**
 * @brief Получение текущего состояния батареи
 * 
 * @return battery_state_t Текущее состояние батареи
 */
battery_state_t power_get_battery_state(void);

/**
 * @brief Проверка наличия внешнего питания
 * 
 * @return bool true если подключено внешнее питание
 */
bool power_is_external_power_connected(void);

/**
 * @brief Установка режима питания
 * 
 * @param mode Режим питания
 * @return esp_err_t ESP_OK при успешной установке режима
 */
esp_err_t power_set_mode(power_mode_t mode);

/**
 * @brief Получение текущего режима питания
 * 
 * @return power_mode_t Текущий режим питания
 */
power_mode_t power_get_mode(void);

/**
 * @brief Переход в режим глубокого сна
 * 
 * @param sleep_time_ms Время сна в миллисекундах (0 - бесконечно)
 * @return esp_err_t ESP_OK при успешном переходе в сон
 */
esp_err_t power_enter_deep_sleep(uint32_t sleep_time_ms);

/**
 * @brief Обработчик мониторинга питания
 * 
 * Эта функция вызывается периодически для проверки состояния батареи.
 * 
 * @return esp_err_t ESP_OK при успешной обработке
 */
esp_err_t power_monitor_task(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGEMENT_H */ 