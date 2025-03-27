/**
 * @file zigbee_handler.h
 * @brief Заголовочный файл обработчика ZigBee для умного окна
 * @version 1.0
 * @date 2023-03-26
 */

#ifndef ZIGBEE_HANDLER_H
#define ZIGBEE_HANDLER_H

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
} zigbee_state_t;

/**
 * @brief Типы устройств ZigBee
 */
typedef enum {
    ZIGBEE_DEVICE_TYPE_LIGHT,   // Освещение
    ZIGBEE_DEVICE_TYPE_SWITCH,  // Выключатель
    ZIGBEE_DEVICE_TYPE_SENSOR,  // Датчик
    ZIGBEE_DEVICE_TYPE_COVER    // Шторы/жалюзи/окна
} zigbee_device_type_t;

/**
 * @brief Типы уведомлений
 */
typedef enum {
    ZIGBEE_ALERT_LOW_BATTERY,   // Низкий заряд батареи
    ZIGBEE_ALERT_RESISTANCE,    // Механическое сопротивление
    ZIGBEE_ALERT_MODE_CHANGE,   // Изменение режима
    ZIGBEE_ALERT_PROTECTION     // Сработала защита
} zigbee_alert_type_t;

/**
 * @brief Конфигурация ZigBee устройства
 */
typedef struct {
    const char *device_name;    // Имя устройства
    const char *manufacturer;   // Производитель
    const char *model;          // Модель
    uint16_t pan_id;            // Идентификатор сети
    uint8_t channel;            // Номер канала
    zigbee_device_type_t dev_type; // Тип устройства
} zigbee_config_t;

/**
 * @brief Инициализация модуля ZigBee
 * 
 * @param config Конфигурация устройства
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t zigbee_init(const zigbee_config_t *config);

/**
 * @brief Запуск модуля ZigBee
 * 
 * @return esp_err_t ESP_OK при успешном запуске
 */
esp_err_t zigbee_start(void);

/**
 * @brief Остановка модуля ZigBee
 * 
 * @return esp_err_t ESP_OK при успешной остановке
 */
esp_err_t zigbee_stop(void);

/**
 * @brief Получение текущего состояния подключения ZigBee
 * 
 * @return zigbee_state_t Текущее состояние подключения
 */
zigbee_state_t zigbee_get_state(void);

/**
 * @brief Активация режима сопряжения
 * 
 * @param duration_sec Длительность режима в секундах
 * @return esp_err_t ESP_OK при успешной активации
 */
esp_err_t zigbee_enable_pairing_mode(uint16_t duration_sec);

/**
 * @brief Обработка входящих команд ZigBee
 * 
 * @return esp_err_t ESP_OK при успешной обработке
 */
esp_err_t zigbee_process_incoming_commands(void);

/**
 * @brief Отправка состояния устройства через ZigBee
 * 
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t zigbee_report_state(void);

/**
 * @brief Отправка режима окна через ZigBee
 * 
 * @param mode Режим окна
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t zigbee_send_window_mode(uint8_t mode);

/**
 * @brief Отправка положения зазора через ZigBee
 * 
 * @param gap_percentage Процент открытия
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t zigbee_send_gap_position(uint8_t gap_percentage);

/**
 * @brief Отправка уведомления через ZigBee
 * 
 * @param alert_type Тип уведомления
 * @param value Значение (зависит от типа уведомления)
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t zigbee_send_alert(zigbee_alert_type_t alert_type, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* ZIGBEE_HANDLER_H */ 