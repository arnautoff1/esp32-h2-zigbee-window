/**
 * @file zigbee_handler.c
 * @brief Реализация обработчика ZigBee для умного окна
 * @version 1.0
 * @date 2023-03-26
 */

#include "zigbee_handler.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "servo_control.h"

// Включение библиотеки ZigBee (esp_zb) из компонентов ESP-IDF
#include "esp_zigbee_lib.h"

static const char *TAG = "ZIGBEE_HANDLER";

// Текущие настройки ZigBee
static zigbee_config_t current_config;
static zigbee_state_t current_state = ZIGBEE_STATE_DISCONNECTED;

// Таймер режима сопряжения
static TimerHandle_t pairing_timer = NULL;

// Текущие значения состояния окна
static uint8_t current_window_mode = 0; // WINDOW_MODE_CLOSED
static uint8_t current_gap_percentage = 0;

// Прототипы функций колбэков для библиотеки ZigBee
static void zigbee_on_connected(void);
static void zigbee_on_disconnected(void);
static void zigbee_on_command(uint8_t cmd, const uint8_t *data, uint16_t len);
static void pairing_timer_callback(TimerHandle_t xTimer);

/**
 * @brief Инициализация модуля ZigBee
 */
esp_err_t zigbee_init(const zigbee_config_t *config)
{
    ESP_LOGI(TAG, "Инициализация модуля ZigBee");
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Ошибка: NULL конфигурация");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Сохранение конфигурации
    memcpy(&current_config, config, sizeof(zigbee_config_t));
    
    // Создание таймера режима сопряжения
    pairing_timer = xTimerCreate(
        "pairing_timer",
        pdMS_TO_TICKS(60000), // Значение по умолчанию - 1 минута
        pdFALSE,             // Одиночный таймер
        NULL,                // ID таймера не используется
        pairing_timer_callback
    );
    
    if (pairing_timer == NULL) {
        ESP_LOGE(TAG, "Не удалось создать таймер режима сопряжения");
        return ESP_ERR_NO_MEM;
    }
    
    // Конфигурация ZigBee библиотеки
    esp_zigbee_config_t zb_config = {
        .device_name = config->device_name,
        .pan_id = config->pan_id,
        .channel = config->channel,
        .auto_join = true,                    // Автоматическое подключение
        .join_timeout_ms = 30000,            // Таймаут подключения (30 секунд)
        .on_connected = zigbee_on_connected,
        .on_disconnected = zigbee_on_disconnected,
        .on_command = zigbee_on_command
    };
    
    // Инициализация библиотеки ZigBee
    esp_err_t err = esp_zigbee_init(&zb_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации библиотеки ZigBee: %s", esp_err_to_name(err));
        return err;
    }
    
    // Настройка типа устройства (шторы/жалюзи/окна)
    err = esp_zigbee_set_device_type(ESP_ZIGBEE_DEVICE_TYPE_COVER);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка настройки типа устройства: %s", esp_err_to_name(err));
        return err;
    }
    
    current_state = ZIGBEE_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "Модуль ZigBee успешно инициализирован");
    
    return ESP_OK;
}

/**
 * @brief Запуск модуля ZigBee
 */
esp_err_t zigbee_start(void)
{
    ESP_LOGI(TAG, "Запуск модуля ZigBee");
    
    if (current_state != ZIGBEE_STATE_DISCONNECTED) {
        ESP_LOGW(TAG, "ZigBee уже запущен в состоянии: %d", current_state);
        return ESP_OK;
    }
    
    // Запуск библиотеки ZigBee
    esp_err_t err = esp_zigbee_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка запуска библиотеки ZigBee: %s", esp_err_to_name(err));
        return err;
    }
    
    current_state = ZIGBEE_STATE_CONNECTING;
    ESP_LOGI(TAG, "ZigBee запущен, ожидание подключения...");
    
    return ESP_OK;
}

/**
 * @brief Остановка модуля ZigBee
 */
esp_err_t zigbee_stop(void)
{
    ESP_LOGI(TAG, "Остановка модуля ZigBee");
    
    if (current_state == ZIGBEE_STATE_DISCONNECTED) {
        ESP_LOGW(TAG, "ZigBee уже остановлен");
        return ESP_OK;
    }
    
    // Остановка режима сопряжения, если он активен
    if (xTimerIsTimerActive(pairing_timer)) {
        xTimerStop(pairing_timer, 0);
    }
    
    // Остановка библиотеки ZigBee
    esp_err_t err = esp_zigbee_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка остановки библиотеки ZigBee: %s", esp_err_to_name(err));
        return err;
    }
    
    current_state = ZIGBEE_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "ZigBee успешно остановлен");
    
    return ESP_OK;
}

/**
 * @brief Получение текущего состояния подключения ZigBee
 */
zigbee_state_t zigbee_get_state(void)
{
    return current_state;
}

/**
 * @brief Активация режима сопряжения
 */
esp_err_t zigbee_enable_pairing_mode(uint16_t duration_sec)
{
    ESP_LOGI(TAG, "Включение режима сопряжения на %d секунд", duration_sec);
    
    if (current_state == ZIGBEE_STATE_DISCONNECTED) {
        ESP_LOGE(TAG, "ZigBee не запущен, невозможно активировать режим сопряжения");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Если таймер уже активен, остановим его
    if (xTimerIsTimerActive(pairing_timer)) {
        xTimerStop(pairing_timer, 0);
    }
    
    // Активация режима сопряжения в библиотеке ZigBee
    esp_err_t err = esp_zigbee_enable_pairing(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка активации режима сопряжения: %s", esp_err_to_name(err));
        return err;
    }
    
    // Перезапуск таймера с новой длительностью
    xTimerChangePeriod(pairing_timer, pdMS_TO_TICKS(duration_sec * 1000), 0);
    xTimerStart(pairing_timer, 0);
    
    ESP_LOGI(TAG, "Режим сопряжения активирован");
    return ESP_OK;
}

/**
 * @brief Обработка входящих команд ZigBee
 */
esp_err_t zigbee_process_incoming_commands(void)
{
    if (current_state == ZIGBEE_STATE_DISCONNECTED) {
        return ESP_OK; // Не выполняем обработку, если не подключены
    }
    
    // Обработка команд из библиотеки ZigBee
    esp_err_t err = esp_zigbee_process_commands();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Ошибка обработки команд: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

/**
 * @brief Отправка состояния устройства через ZigBee
 */
esp_err_t zigbee_report_state(void)
{
    ESP_LOGI(TAG, "Отправка состояния через ZigBee");
    
    if (current_state != ZIGBEE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "ZigBee не подключен, невозможно отправить состояние");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Получение текущего режима и зазора
    uint8_t mode = servo_get_window_mode();
    uint8_t gap = servo_get_gap();
    
    // Отправка состояния через библиотеку ZigBee
    esp_err_t err = esp_zigbee_report_window_state(mode, gap);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка отправки состояния: %s", esp_err_to_name(err));
        return err;
    }
    
    current_window_mode = mode;
    current_gap_percentage = gap;
    
    return ESP_OK;
}

/**
 * @brief Отправка режима окна через ZigBee
 */
esp_err_t zigbee_send_window_mode(uint8_t mode)
{
    ESP_LOGI(TAG, "Отправка режима окна: %d", mode);
    
    if (current_state != ZIGBEE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "ZigBee не подключен, невозможно отправить режим");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = esp_zigbee_report_window_mode(mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка отправки режима: %s", esp_err_to_name(err));
        return err;
    }
    
    current_window_mode = mode;
    return ESP_OK;
}

/**
 * @brief Отправка положения зазора через ZigBee
 */
esp_err_t zigbee_send_gap_position(uint8_t gap_percentage)
{
    ESP_LOGI(TAG, "Отправка положения зазора: %d%%", gap_percentage);
    
    if (current_state != ZIGBEE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "ZigBee не подключен, невозможно отправить положение зазора");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = esp_zigbee_report_position(gap_percentage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка отправки положения зазора: %s", esp_err_to_name(err));
        return err;
    }
    
    current_gap_percentage = gap_percentage;
    return ESP_OK;
}

/**
 * @brief Отправка уведомления через ZigBee
 */
esp_err_t zigbee_send_alert(zigbee_alert_type_t alert_type, uint8_t value)
{
    ESP_LOGI(TAG, "Отправка уведомления: тип=%d, значение=%d", alert_type, value);
    
    if (current_state != ZIGBEE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "ZigBee не подключен, невозможно отправить уведомление");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_zigbee_alert_type_t zb_alert_type;
    switch (alert_type) {
        case ZIGBEE_ALERT_LOW_BATTERY:
            zb_alert_type = ESP_ZIGBEE_ALERT_LOW_BATTERY;
            break;
        case ZIGBEE_ALERT_RESISTANCE:
            zb_alert_type = ESP_ZIGBEE_ALERT_STUCK;
            break;
        case ZIGBEE_ALERT_MODE_CHANGE:
            zb_alert_type = ESP_ZIGBEE_ALERT_MODE_CHANGE;
            break;
        case ZIGBEE_ALERT_PROTECTION:
            zb_alert_type = ESP_ZIGBEE_ALERT_PROTECTION;
            break;
        default:
            ESP_LOGE(TAG, "Неизвестный тип уведомления: %d", alert_type);
            return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = esp_zigbee_send_alert(zb_alert_type, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка отправки уведомления: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

/**
 * @brief Колбэк при подключении к сети ZigBee
 */
static void zigbee_on_connected(void)
{
    ESP_LOGI(TAG, "Подключено к сети ZigBee");
    current_state = ZIGBEE_STATE_CONNECTED;
    
    // Если режим сопряжения был активен, отключаем его
    if (xTimerIsTimerActive(pairing_timer)) {
        xTimerStop(pairing_timer, 0);
        esp_zigbee_enable_pairing(false);
    }
    
    // Отправляем текущее состояние
    zigbee_report_state();
}

/**
 * @brief Колбэк при отключении от сети ZigBee
 */
static void zigbee_on_disconnected(void)
{
    ESP_LOGW(TAG, "Отключено от сети ZigBee");
    current_state = ZIGBEE_STATE_DISCONNECTED;
}

/**
 * @brief Колбэк при получении команды от сети ZigBee
 */
static void zigbee_on_command(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    ESP_LOGI(TAG, "Получена команда ZigBee: %d", cmd);
    
    switch (cmd) {
        case ESP_ZIGBEE_CMD_SET_MODE:
            if (len >= 1) {
                uint8_t mode = data[0];
                ESP_LOGI(TAG, "Команда изменения режима: %d", mode);
                
                // Применяем новый режим к сервоприводу
                esp_err_t err = servo_set_window_mode(mode);
                if (err == ESP_OK) {
                    // Обновляем текущий режим и отправляем подтверждение
                    current_window_mode = mode;
                    zigbee_send_window_mode(mode);
                }
            }
            break;
            
        case ESP_ZIGBEE_CMD_SET_POSITION:
            if (len >= 1) {
                uint8_t position = data[0];
                ESP_LOGI(TAG, "Команда изменения положения: %d%%", position);
                
                // Применяем новое положение к сервоприводу
                esp_err_t err = servo_set_gap(position);
                if (err == ESP_OK) {
                    // Обновляем текущее положение и отправляем подтверждение
                    current_gap_percentage = position;
                    zigbee_send_gap_position(position);
                }
            }
            break;
            
        case ESP_ZIGBEE_CMD_CALIBRATE:
            ESP_LOGI(TAG, "Команда калибровки");
            
            // Запускаем калибровку сервоприводов
            esp_err_t err = servo_calibrate();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Ошибка калибровки: %s", esp_err_to_name(err));
            }
            break;
            
        default:
            ESP_LOGW(TAG, "Неизвестная команда: %d", cmd);
            break;
    }
}

/**
 * @brief Колбэк таймера режима сопряжения
 */
static void pairing_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Таймер режима сопряжения истек");
    
    // Отключаем режим сопряжения
    esp_zigbee_enable_pairing(false);
} 