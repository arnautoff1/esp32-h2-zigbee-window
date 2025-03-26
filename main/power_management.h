/**
 * @file power_management.h
 * @brief Модуль управления питанием для умного окна
 */

#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Режимы питания устройства
 */
typedef enum {
    POWER_MODE_NORMAL = 0,     ///< Нормальный режим работы
    POWER_MODE_LOW_POWER = 1,  ///< Режим низкого энергопотребления
    POWER_MODE_SLEEP = 2       ///< Режим глубокого сна
} power_mode_t;

/**
 * @brief Типы источника питания
 */
typedef enum {
    POWER_SOURCE_BATTERY = 0,  ///< Питание от батареи
    POWER_SOURCE_EXTERNAL = 1  ///< Внешнее питание
} power_source_t;

/**
 * @brief Структура конфигурации управления питанием
 */
typedef struct {
    power_source_t source;            ///< Тип источника питания
    uint16_t low_battery_threshold;   ///< Порог низкого заряда батареи (мВ)
    uint16_t critical_battery_threshold; ///< Критический порог заряда батареи (мВ)
    uint32_t sleep_timeout_ms;        ///< Время до перехода в режим сна (мс)
    bool enable_auto_sleep;           ///< Автоматический переход в режим сна
} power_config_t;

/**
 * @brief Инициализация модуля управления питанием
 * 
 * @param config Конфигурация модуля питания
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t power_init(power_config_t *config);

/**
 * @brief Запуск модуля управления питанием
 * 
 * @return esp_err_t ESP_OK при успешном запуске
 */
esp_err_t power_start(void);

/**
 * @brief Остановка модуля управления питанием
 * 
 * @return esp_err_t ESP_OK при успешной остановке
 */
esp_err_t power_stop(void);

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
 * @brief Получение уровня заряда батареи
 * 
 * @return uint8_t Уровень заряда в процентах (0-100)
 */
uint8_t power_get_battery_level(void);

/**
 * @brief Получение напряжения батареи
 * 
 * @return uint16_t Напряжение батареи в милливольтах
 */
uint16_t power_get_battery_voltage(void);

/**
 * @brief Проверка состояния низкого заряда батареи
 * 
 * @return bool true, если заряд батареи ниже порога
 */
bool power_is_low_battery(void);

/**
 * @brief Проверка критического состояния батареи
 * 
 * @return bool true, если заряд батареи критически низкий
 */
bool power_is_critical_battery(void);

/**
 * @brief Определение типа источника питания
 * 
 * @return power_source_t Тип источника питания
 */
power_source_t power_get_source(void);

/**
 * @brief Переход в режим глубокого сна
 * 
 * @param sleep_duration_ms Длительность сна в миллисекундах
 * @return esp_err_t ESP_OK при успешном переходе в режим сна
 */
esp_err_t power_deep_sleep(uint64_t sleep_duration_ms);

/**
 * @brief Сброс таймера сна
 * 
 * @return esp_err_t ESP_OK при успешном сбросе таймера
 */
esp_err_t power_reset_sleep_timer(void);

/**
 * @brief Обработчик задачи управления питанием
 * 
 * @note Эта функция вызывается автоматически в отдельной задаче
 */
void power_task_handler(void *pvParameter);

#endif /* POWER_MANAGEMENT_H */ 