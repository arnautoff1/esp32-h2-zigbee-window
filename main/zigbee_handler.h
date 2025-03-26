/**
 * @file zigbee_handler.h
 * @brief Обработчик ZigBee для умного окна
 */

#ifndef ZIGBEE_HANDLER_H
#define ZIGBEE_HANDLER_H

#include <stdint.h>
#include "esp_err.h"
#include "servo_control.h"

/**
 * @brief Состояние ZigBee
 */
typedef enum {
    ZIGBEE_STATE_DISCONNECTED = 0,  ///< Не подключен к сети
    ZIGBEE_STATE_CONNECTING = 1,    ///< В процессе подключения
    ZIGBEE_STATE_CONNECTED = 2      ///< Подключен к сети
} zigbee_state_t;

/**
 * @brief Тип устройства ZigBee
 */
typedef enum {
    ZIGBEE_DEVICE_TYPE_COVER = 0,  ///< Тип "умные шторы" для Яндекс Алисы
    ZIGBEE_DEVICE_TYPE_WINDOW = 1  ///< Тип "окно" (для будущего использования)
} zigbee_device_type_t;

/**
 * @brief Тип уведомления ZigBee
 */
typedef enum {
    ZIGBEE_ALERT_RESISTANCE = 0,   ///< Уведомление о механическом сопротивлении
    ZIGBEE_ALERT_LOW_BATTERY = 1,  ///< Уведомление о низком заряде батареи
    ZIGBEE_ALERT_MODE_CHANGE = 2   ///< Уведомление об изменении режима
} zigbee_alert_type_t;

/**
 * @brief Структура данных конфигурации ZigBee
 */
typedef struct {
    char device_name[32];           ///< Имя устройства
    char manufacturer[32];          ///< Производитель
    char model[32];                 ///< Модель
    uint16_t pan_id;                ///< ID сети PAN
    uint8_t channel;                ///< Канал ZigBee (по умолчанию 15)
    zigbee_device_type_t dev_type;  ///< Тип устройства
} zigbee_config_t;

/**
 * @brief Инициализация ZigBee
 * 
 * @param config Конфигурация ZigBee
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t zigbee_init(zigbee_config_t *config);

/**
 * @brief Запуск ZigBee стека
 * 
 * @return esp_err_t ESP_OK при успешном запуске
 */
esp_err_t zigbee_start(void);

/**
 * @brief Остановка ZigBee стека
 * 
 * @return esp_err_t ESP_OK при успешной остановке
 */
esp_err_t zigbee_stop(void);

/**
 * @brief Отправка команды открытия окна через ZigBee
 * 
 * @param mode Режим окна
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t zigbee_send_window_mode(window_mode_t mode);

/**
 * @brief Отправка команды управления зазором окна через ZigBee
 * 
 * @param percentage Процент открытия (0-100)
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t zigbee_send_gap_position(uint8_t percentage);

/**
 * @brief Отправка обновления состояния в сеть ZigBee
 * 
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t zigbee_report_state(void);

/**
 * @brief Отправка уведомления о событии через ZigBee
 * 
 * @param alert_type Тип уведомления
 * @param value Дополнительные данные уведомления (если нужно)
 * @return esp_err_t ESP_OK при успешной отправке
 */
esp_err_t zigbee_send_alert(zigbee_alert_type_t alert_type, uint8_t value);

/**
 * @brief Получение текущего состояния ZigBee
 * 
 * @return zigbee_state_t Текущее состояние ZigBee
 */
zigbee_state_t zigbee_get_state(void);

/**
 * @brief Включение режима сопряжения ZigBee
 * 
 * @param duration_seconds Длительность режима сопряжения в секундах
 * @return esp_err_t ESP_OK при успешном включении режима
 */
esp_err_t zigbee_enable_pairing_mode(uint16_t duration_seconds);

/**
 * @brief Обработчик входящих команд ZigBee
 * 
 * @note Эта функция вызывается автоматически при получении команды
 */
void zigbee_process_incoming_commands(void);

#endif /* ZIGBEE_HANDLER_H */ 