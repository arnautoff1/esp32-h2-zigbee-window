/**
 * @file zigbee_device.h
 * @brief Заголовочный файл модуля ZigBee для умного окна
 */

#ifndef ZIGBEE_DEVICE_H
#define ZIGBEE_DEVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Состояния подключения ZigBee
 */
typedef enum {
    ZIGBEE_STATE_DISCONNECTED,  // Не подключен к сети
    ZIGBEE_STATE_CONNECTING,    // В процессе подключения
    ZIGBEE_STATE_CONNECTED      // Подключен к сети
} zigbee_device_state_t;

/**
 * @brief Режимы работы окна
 */
typedef enum {
    WINDOW_MODE_CLOSED,     // Окно закрыто
    WINDOW_MODE_OPEN,       // Окно открыто
    WINDOW_MODE_VENTILATE,  // Окно в режиме проветривания
    WINDOW_MODE_CUSTOM      // Пользовательский режим
} window_mode_t;

/**
 * @brief Типы уведомлений
 */
typedef enum {
    ZIGBEE_ALERT_LOW_BATTERY,   // Низкий заряд батареи
    ZIGBEE_ALERT_STUCK,         // Механическое сопротивление
    ZIGBEE_ALERT_MODE_CHANGED,  // Изменение режима
    ZIGBEE_ALERT_PROTECTION     // Сработала защита
} zigbee_device_alert_type_t;

/**
 * @brief Тип колбэка для команд ZigBee
 */
typedef void (*zigbee_command_callback_t)(uint8_t cmd, const uint8_t *data, uint16_t len);

/**
 * @brief Инициализация ZigBee устройства
 * 
 * @param callback Колбэк для обработки команд (может быть NULL)
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t zigbee_device_init(zigbee_command_callback_t callback);

/**
 * @brief Запуск ZigBee-устройства и подключение к сети
 * 
 * @return esp_err_t ESP_OK при успешном запуске
 */
esp_err_t zigbee_device_start(void);

/**
 * @brief Остановка ZigBee-устройства
 * 
 * @return esp_err_t ESP_OK при успешной остановке
 */
esp_err_t zigbee_device_stop(void);

/**
 * @brief Отправка отчета о состоянии окна через ZigBee
 * 
 * @param mode Режим работы окна
 * @param percentage Процент открытия
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t zigbee_device_report_state(window_mode_t mode, uint8_t percentage);

/**
 * @brief Отправка уведомления о событии через ZigBee
 * 
 * @param alert_type Тип уведомления
 * @param value Дополнительные данные уведомления (если нужно)
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t zigbee_device_send_alert(zigbee_device_alert_type_t alert_type, uint8_t value);

/**
 * @brief Получение текущего состояния подключения ZigBee
 * 
 * @return zigbee_device_state_t Текущее состояние подключения
 */
zigbee_device_state_t zigbee_device_get_state(void);

/**
 * @brief Обработчик команд ZigBee
 * 
 * Эта функция должна вызываться периодически для обработки входящих команд.
 * 
 * @return esp_err_t ESP_OK при успешной обработке
 */
esp_err_t zigbee_device_process_commands(void);

/**
 * @brief Сброс ZigBee-устройства
 * 
 * @param clear_network Очистить сетевую информацию
 * @return esp_err_t ESP_OK при успешном сбросе
 */
esp_err_t zigbee_device_reset(bool clear_network);

#ifdef __cplusplus
}
#endif

#endif /* ZIGBEE_DEVICE_H */ 