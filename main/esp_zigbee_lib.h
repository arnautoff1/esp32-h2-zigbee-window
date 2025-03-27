/**
 * @file esp_zigbee_lib.h
 * @brief Заголовочный файл библиотеки ZigBee для ESP32-H2
 * @version 1.0
 * @date 2023-03-26
 */

#ifndef ESP_ZIGBEE_LIB_H
#define ESP_ZIGBEE_LIB_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Типы устройств ZigBee
 */
typedef enum {
    ESP_ZIGBEE_DEVICE_TYPE_ROUTER,        // Маршрутизатор
    ESP_ZIGBEE_DEVICE_TYPE_END_DEVICE,    // Конечное устройство
    ESP_ZIGBEE_DEVICE_TYPE_COORD,         // Координатор
    ESP_ZIGBEE_DEVICE_TYPE_LIGHT,         // Освещение
    ESP_ZIGBEE_DEVICE_TYPE_SWITCH,        // Выключатель
    ESP_ZIGBEE_DEVICE_TYPE_SENSOR,        // Датчик
    ESP_ZIGBEE_DEVICE_TYPE_COVER          // Шторы/жалюзи/окна
} esp_zigbee_device_type_t;

/**
 * @brief Режимы окна для отчета через ZigBee
 */
typedef enum {
    ESP_ZIGBEE_WINDOW_MODE_CLOSED,    // Закрыто
    ESP_ZIGBEE_WINDOW_MODE_OPEN,      // Открыто
    ESP_ZIGBEE_WINDOW_MODE_VENTILATE, // Проветривание
    ESP_ZIGBEE_WINDOW_MODE_CUSTOM     // Пользовательский режим
} esp_zigbee_window_mode_t;

/**
 * @brief Типы уведомлений ZigBee
 */
typedef enum {
    ESP_ZIGBEE_ALERT_LOW_BATTERY,   // Низкий заряд батареи
    ESP_ZIGBEE_ALERT_STUCK,         // Механическое сопротивление
    ESP_ZIGBEE_ALERT_MODE_CHANGE,   // Изменение режима
    ESP_ZIGBEE_ALERT_PROTECTION     // Сработала защита
} esp_zigbee_alert_type_t;

/**
 * @brief Команды ZigBee
 */
typedef enum {
    ESP_ZIGBEE_CMD_SET_MODE,        // Установка режима окна
    ESP_ZIGBEE_CMD_SET_POSITION,    // Установка положения
    ESP_ZIGBEE_CMD_STOP,            // Остановка движения
    ESP_ZIGBEE_CMD_CALIBRATE,       // Калибровка
    ESP_ZIGBEE_CMD_PING             // Проверка связи
} esp_zigbee_cmd_t;

/**
 * @brief Тип колбэка для события подключения к сети ZigBee
 */
typedef void (*esp_zigbee_connected_cb_t)(void);

/**
 * @brief Тип колбэка для события отключения от сети ZigBee
 */
typedef void (*esp_zigbee_disconnected_cb_t)(void);

/**
 * @brief Тип колбэка для события получения команды
 */
typedef void (*esp_zigbee_command_cb_t)(uint8_t cmd, const uint8_t *data, uint16_t len);

/**
 * @brief Конфигурация ZigBee устройства
 */
typedef struct {
    const char *device_name;                // Имя устройства
    uint16_t pan_id;                        // Идентификатор сети (0 для автовыбора)
    uint8_t channel;                        // Номер канала (0 для автовыбора)
    bool auto_join;                         // Автоматическое подключение
    uint32_t join_timeout_ms;               // Таймаут подключения в мс
    esp_zigbee_connected_cb_t on_connected;     // Колбэк подключения
    esp_zigbee_disconnected_cb_t on_disconnected; // Колбэк отключения
    esp_zigbee_command_cb_t on_command;         // Колбэк команды
} esp_zigbee_config_t;

/**
 * @brief Инициализация ZigBee устройства
 * 
 * @param config Конфигурация устройства
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t esp_zigbee_init(const esp_zigbee_config_t *config);

/**
 * @brief Запуск ZigBee устройства
 * 
 * @return esp_err_t ESP_OK при успешном запуске
 */
esp_err_t esp_zigbee_start(void);

/**
 * @brief Остановка ZigBee устройства
 * 
 * @return esp_err_t ESP_OK при успешной остановке
 */
esp_err_t esp_zigbee_stop(void);

/**
 * @brief Установка типа устройства
 * 
 * @param device_type Тип устройства
 * @return esp_err_t ESP_OK при успешной установке
 */
esp_err_t esp_zigbee_set_device_type(esp_zigbee_device_type_t device_type);

/**
 * @brief Включение/отключение режима сопряжения
 * 
 * @param enable Включить/выключить режим сопряжения
 * @return esp_err_t ESP_OK при успешной операции
 */
esp_err_t esp_zigbee_enable_pairing(bool enable);

/**
 * @brief Обработка входящих команд ZigBee
 * 
 * @return esp_err_t ESP_OK при успешной обработке или ESP_ERR_NOT_FOUND если команд нет
 */
esp_err_t esp_zigbee_process_commands(void);

/**
 * @brief Отправка состояния окна
 * 
 * @param mode Режим работы окна
 * @param position Положение (процент открытия)
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t esp_zigbee_report_window_state(esp_zigbee_window_mode_t mode, uint8_t position);

/**
 * @brief Отправка режима окна
 * 
 * @param mode Режим работы окна
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t esp_zigbee_report_window_mode(uint8_t mode);

/**
 * @brief Отправка положения окна
 * 
 * @param position Положение (процент открытия)
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t esp_zigbee_report_position(uint8_t position);

/**
 * @brief Отправка уведомления о событии
 * 
 * @param alert_type Тип уведомления
 * @param value Значение (зависит от типа уведомления)
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t esp_zigbee_send_alert(esp_zigbee_alert_type_t alert_type, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* ESP_ZIGBEE_LIB_H */ 