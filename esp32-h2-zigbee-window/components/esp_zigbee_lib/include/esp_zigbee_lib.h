/**
 * @file esp_zigbee_lib.h
 * @brief Заголовочный файл библиотеки ESP ZigBee для умного окна
 */

#ifndef ESP_ZIGBEE_LIB_H
#define ESP_ZIGBEE_LIB_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Состояния соединения ZigBee
 */
typedef enum {
    ESP_ZIGBEE_STATE_DISCONNECTED,   // Устройство не подключено к сети
    ESP_ZIGBEE_STATE_CONNECTING,     // Устройство находится в процессе подключения
    ESP_ZIGBEE_STATE_CONNECTED,      // Устройство подключено к сети
    ESP_ZIGBEE_STATE_PAIRED,         // Устройство сопряжено с координатором
    ESP_ZIGBEE_STATE_ERROR           // Ошибка соединения
} esp_zigbee_state_t;

/**
 * @brief Типы уведомлений ZigBee
 */
typedef enum {
    ESP_ZIGBEE_ALERT_LOW_BATTERY,    // Низкий заряд батареи
    ESP_ZIGBEE_ALERT_STUCK,          // Механическое сопротивление
    ESP_ZIGBEE_ALERT_MODE_CHANGED,   // Изменение режима
    ESP_ZIGBEE_ALERT_PROTECTION,     // Сработала защита
    ESP_ZIGBEE_ALERT_MAX
} esp_zigbee_alert_type_t;

/**
 * @brief Конфигурация библиотеки ESP ZigBee
 */
typedef struct {
    const char *device_name;         // Имя устройства
    uint16_t pan_id;                 // PAN ID (0x0000 для автоматического)
    uint8_t channel;                 // Канал (0 для автоматического)
    bool auto_join;                  // Автоматическое подключение к сети
    uint32_t join_timeout_ms;        // Таймаут подключения (мс)
    void (*on_connected)(void);      // Колбэк подключения
    void (*on_disconnected)(void);   // Колбэк отключения
    void (*on_command)(uint8_t cmd, const uint8_t *data, uint16_t len); // Колбэк команды
} esp_zigbee_config_t;

/**
 * @brief Режимы окна
 */
typedef enum {
    ESP_ZIGBEE_WINDOW_MODE_CLOSED,   // Окно закрыто
    ESP_ZIGBEE_WINDOW_MODE_OPEN,     // Окно открыто
    ESP_ZIGBEE_WINDOW_MODE_VENTILATE // Окно в режиме проветривания
} esp_zigbee_window_mode_t;

/**
 * @brief Инициализация библиотеки ESP ZigBee
 * 
 * @param config Указатель на структуру конфигурации
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t esp_zigbee_init(esp_zigbee_config_t *config);

/**
 * @brief Запуск ZigBee-устройства и подключение к сети
 * 
 * @return esp_err_t ESP_OK при успешном запуске
 */
esp_err_t esp_zigbee_start(void);

/**
 * @brief Остановка ZigBee-устройства
 * 
 * @return esp_err_t ESP_OK при успешной остановке
 */
esp_err_t esp_zigbee_stop(void);

/**
 * @brief Получение текущего состояния подключения ZigBee
 * 
 * @return esp_zigbee_state_t Текущее состояние
 */
esp_zigbee_state_t esp_zigbee_get_state(void);

/**
 * @brief Отправка отчета о состоянии окна
 * 
 * @param mode Режим работы окна
 * @param percentage Процент открытия
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t esp_zigbee_report_window_state(esp_zigbee_window_mode_t mode, uint8_t percentage);

/**
 * @brief Отправка уведомления о событии
 * 
 * @param alert_type Тип уведомления
 * @param value Дополнительные данные (опционально)
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t esp_zigbee_send_alert(esp_zigbee_alert_type_t alert_type, uint8_t value);

/**
 * @brief Обработка входящих команд ZigBee
 * 
 * @return esp_err_t ESP_OK при успешной обработке
 */
esp_err_t esp_zigbee_process_commands(void);

/**
 * @brief Сброс ZigBee устройства
 * 
 * @param clear_network Очистить сетевую информацию
 * @return esp_err_t ESP_OK при успешном сбросе
 */
esp_err_t esp_zigbee_reset(bool clear_network);

#ifdef __cplusplus
}
#endif

#endif /* ESP_ZIGBEE_LIB_H */ 