/**
 * @file zigbee_device.c
 * @brief Реализация модуля ZigBee для умного окна
 */

#include "zigbee_device.h"
#include "esp_zigbee_lib.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"

#define TAG "ZIGBEE_DEVICE"

// Флаги событий ZigBee
#define ZIGBEE_EVENT_CONNECTED      (1 << 0)
#define ZIGBEE_EVENT_DISCONNECTED   (1 << 1)
#define ZIGBEE_EVENT_COMMAND        (1 << 2)

// Конфигурация ZigBee по умолчанию
static const esp_zigbee_config_t default_config = {
    .device_name = "ESP32-H2-Window",    // Имя устройства
    .pan_id = 0x0000,                    // Автоматический выбор PAN ID
    .channel = 0,                        // Автоматический выбор канала
    .auto_join = true,                   // Автоматическое подключение
    .join_timeout_ms = 30000,            // Таймаут подключения (30 секунд)
    .on_connected = NULL,                // Будет установлен ниже
    .on_disconnected = NULL,             // Будет установлен ниже
    .on_command = NULL                   // Будет установлен ниже
};

// Контекст ZigBee устройства
static struct {
    bool initialized;                    // Статус инициализации
    zigbee_device_state_t state;         // Состояние устройства
    window_mode_t current_mode;          // Текущий режим окна
    uint8_t current_percentage;          // Текущий процент открытия
    TaskHandle_t command_task_handle;    // Задача обработки команд
    EventGroupHandle_t event_group;      // Группа событий
    zigbee_command_callback_t command_callback; // Колбэк команд
} zigbee_ctx = {
    .initialized = false,
    .state = ZIGBEE_STATE_DISCONNECTED,
    .current_mode = WINDOW_MODE_CLOSED,
    .current_percentage = 0,
    .command_task_handle = NULL,
    .event_group = NULL,
    .command_callback = NULL
};

// Прототипы функций
static void zigbee_command_task(void *pvParameters);
static void zigbee_on_connected(void);
static void zigbee_on_disconnected(void);
static void zigbee_on_command(uint8_t cmd, const uint8_t *data, uint16_t len);

/**
 * @brief Инициализация ZigBee устройства
 */
esp_err_t zigbee_device_init(zigbee_command_callback_t callback)
{
    ESP_LOGI(TAG, "Инициализация модуля ZigBee");
    
    if (zigbee_ctx.initialized) {
        ESP_LOGW(TAG, "Модуль ZigBee уже инициализирован");
        return ESP_OK;
    }
    
    // Инициализация контекста
    memset(&zigbee_ctx, 0, sizeof(zigbee_ctx));
    zigbee_ctx.command_callback = callback;
    zigbee_ctx.state = ZIGBEE_STATE_DISCONNECTED;
    
    // Создание очереди и группы событий
    zigbee_ctx.event_group = xEventGroupCreate();
    if (zigbee_ctx.event_group == NULL) {
        ESP_LOGE(TAG, "Не удалось создать группу событий");
        return ESP_ERR_NO_MEM;
    }
    
    // Инициализация ZigBee библиотеки
    esp_zigbee_config_t config = default_config;
    config.on_connected = zigbee_on_connected;
    config.on_disconnected = zigbee_on_disconnected;
    config.on_command = zigbee_on_command;
    
    esp_err_t err = esp_zigbee_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации ZigBee: %s", esp_err_to_name(err));
        vEventGroupDelete(zigbee_ctx.event_group);
        return err;
    }
    
    // Создание задачи обработки команд
    BaseType_t xReturn = xTaskCreate(
        zigbee_command_task,
        "zigbee_cmd",
        4096,
        NULL,
        5,
        &zigbee_ctx.command_task_handle
    );
    
    if (xReturn != pdPASS) {
        ESP_LOGE(TAG, "Не удалось создать задачу обработки команд ZigBee");
        esp_zigbee_stop();
        vEventGroupDelete(zigbee_ctx.event_group);
        return ESP_ERR_NO_MEM;
    }
    
    zigbee_ctx.initialized = true;
    ESP_LOGI(TAG, "Модуль ZigBee успешно инициализирован");
    
    return ESP_OK;
}

/**
 * @brief Запуск ZigBee-устройства
 */
esp_err_t zigbee_device_start(void)
{
    ESP_LOGI(TAG, "Запуск ZigBee-устройства");
    
    if (!zigbee_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль ZigBee не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (zigbee_ctx.state != ZIGBEE_STATE_DISCONNECTED) {
        ESP_LOGW(TAG, "ZigBee-устройство уже запущено, состояние: %d", zigbee_ctx.state);
        return ESP_OK;
    }
    
    // Запускаем библиотеку ZigBee
    esp_err_t err = esp_zigbee_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка запуска библиотеки ZigBee: %s", esp_err_to_name(err));
        return err;
    }
    
    zigbee_ctx.state = ZIGBEE_STATE_CONNECTING;
    ESP_LOGI(TAG, "ZigBee-устройство запущено, подключение...");
    
    return ESP_OK;
}

/**
 * @brief Остановка ZigBee-устройства
 */
esp_err_t zigbee_device_stop(void)
{
    ESP_LOGI(TAG, "Остановка ZigBee-устройства");
    
    if (!zigbee_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль ZigBee не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (zigbee_ctx.state == ZIGBEE_STATE_DISCONNECTED) {
        ESP_LOGW(TAG, "ZigBee-устройство уже остановлено");
        return ESP_OK;
    }
    
    // Останавливаем библиотеку ZigBee
    esp_err_t err = esp_zigbee_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка остановки библиотеки ZigBee: %s", esp_err_to_name(err));
        return err;
    }
    
    zigbee_ctx.state = ZIGBEE_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "ZigBee-устройство успешно остановлено");
    
    return ESP_OK;
}

/**
 * @brief Отправка отчета о состоянии окна
 */
esp_err_t zigbee_device_report_state(window_mode_t mode, uint8_t percentage)
{
    ESP_LOGI(TAG, "Отправка отчета о состоянии окна: режим=%d, процент=%d%%", mode, percentage);
    
    if (!zigbee_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль ZigBee не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Сохраняем текущее состояние
    zigbee_ctx.current_mode = mode;
    zigbee_ctx.current_percentage = percentage;
    
    // Преобразуем режим окна в режим ZigBee
    esp_zigbee_window_mode_t zigbee_mode;
    switch (mode) {
        case WINDOW_MODE_CLOSED:
            zigbee_mode = ESP_ZIGBEE_WINDOW_MODE_CLOSED;
            break;
        case WINDOW_MODE_OPEN:
            zigbee_mode = ESP_ZIGBEE_WINDOW_MODE_OPEN;
            break;
        case WINDOW_MODE_VENTILATE:
            zigbee_mode = ESP_ZIGBEE_WINDOW_MODE_VENTILATE;
            break;
        default:
            ESP_LOGE(TAG, "Неверный режим окна: %d", mode);
            return ESP_ERR_INVALID_ARG;
    }
    
    // Отправляем отчет через библиотеку ZigBee
    esp_err_t err = esp_zigbee_report_window_state(zigbee_mode, percentage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка отправки отчета о состоянии: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Отчет о состоянии успешно отправлен");
    return ESP_OK;
}

/**
 * @brief Отправка уведомления о событии
 */
esp_err_t zigbee_device_send_alert(zigbee_device_alert_type_t alert_type, uint8_t value)
{
    ESP_LOGI(TAG, "Отправка уведомления: тип=%d, значение=%d", alert_type, value);
    
    if (!zigbee_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль ZigBee не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Преобразуем тип уведомления устройства в тип уведомления ZigBee
    esp_zigbee_alert_type_t zigbee_alert;
    switch (alert_type) {
        case ZIGBEE_ALERT_LOW_BATTERY:
            zigbee_alert = ESP_ZIGBEE_ALERT_LOW_BATTERY;
            break;
        case ZIGBEE_ALERT_STUCK:
            zigbee_alert = ESP_ZIGBEE_ALERT_STUCK;
            break;
        case ZIGBEE_ALERT_MODE_CHANGED:
            zigbee_alert = ESP_ZIGBEE_ALERT_MODE_CHANGED;
            break;
        case ZIGBEE_ALERT_PROTECTION:
            zigbee_alert = ESP_ZIGBEE_ALERT_PROTECTION;
            break;
        default:
            ESP_LOGE(TAG, "Неверный тип уведомления: %d", alert_type);
            return ESP_ERR_INVALID_ARG;
    }
    
    // Отправляем уведомление через библиотеку ZigBee
    esp_err_t err = esp_zigbee_send_alert(zigbee_alert, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка отправки уведомления: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Уведомление успешно отправлено");
    return ESP_OK;
}

/**
 * @brief Получение текущего состояния подключения ZigBee
 */
zigbee_device_state_t zigbee_device_get_state(void)
{
    return zigbee_ctx.state;
}

/**
 * @brief Обработчик команд ZigBee
 */
esp_err_t zigbee_device_process_commands(void)
{
    if (!zigbee_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль ZigBee не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Обрабатываем входящие команды через библиотеку ZigBee
    esp_err_t err = esp_zigbee_process_commands();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка обработки команд: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

/**
 * @brief Сброс ZigBee-устройства
 */
esp_err_t zigbee_device_reset(bool clear_network)
{
    ESP_LOGI(TAG, "Сброс ZigBee-устройства, очистка сети: %d", clear_network);
    
    if (!zigbee_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль ZigBee не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Сбрасываем устройство через библиотеку ZigBee
    esp_err_t err = esp_zigbee_reset(clear_network);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка сброса устройства: %s", esp_err_to_name(err));
        return err;
    }
    
    // Сбрасываем состояние, если очищаем сетевую информацию
    if (clear_network) {
        zigbee_ctx.state = ZIGBEE_STATE_DISCONNECTED;
    }
    
    ESP_LOGI(TAG, "Устройство успешно сброшено");
    return ESP_OK;
}

/**
 * @brief Задача обработки команд ZigBee
 */
static void zigbee_command_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Задача обработки команд ZigBee запущена");
    
    EventBits_t bits;
    
    while (1) {
        // Ждем событий ZigBee
        bits = xEventGroupWaitBits(
            zigbee_ctx.event_group,
            ZIGBEE_EVENT_CONNECTED | ZIGBEE_EVENT_DISCONNECTED | ZIGBEE_EVENT_COMMAND,
            pdTRUE,  // Очищать биты после получения
            pdFALSE, // Ждем любое событие (логическое ИЛИ)
            pdMS_TO_TICKS(1000) // Таймаут ожидания
        );
        
        // Обрабатываем события
        if (bits & ZIGBEE_EVENT_CONNECTED) {
            ESP_LOGI(TAG, "Событие: ZigBee подключено");
            zigbee_ctx.state = ZIGBEE_STATE_CONNECTED;
            
            // Отправляем отчет о текущем состоянии
            esp_zigbee_window_mode_t zigbee_mode;
            switch (zigbee_ctx.current_mode) {
                case WINDOW_MODE_CLOSED:
                    zigbee_mode = ESP_ZIGBEE_WINDOW_MODE_CLOSED;
                    break;
                case WINDOW_MODE_OPEN:
                    zigbee_mode = ESP_ZIGBEE_WINDOW_MODE_OPEN;
                    break;
                case WINDOW_MODE_VENTILATE:
                    zigbee_mode = ESP_ZIGBEE_WINDOW_MODE_VENTILATE;
                    break;
                default:
                    zigbee_mode = ESP_ZIGBEE_WINDOW_MODE_CLOSED;
                    break;
            }
            
            esp_zigbee_report_window_state(zigbee_mode, zigbee_ctx.current_percentage);
        }
        
        if (bits & ZIGBEE_EVENT_DISCONNECTED) {
            ESP_LOGI(TAG, "Событие: ZigBee отключено");
            zigbee_ctx.state = ZIGBEE_STATE_DISCONNECTED;
        }
        
        if (bits & ZIGBEE_EVENT_COMMAND) {
            ESP_LOGI(TAG, "Событие: Получена команда ZigBee");
            // Обработка команды производится в колбэке zigbee_on_command
        }
        
        // Обрабатываем входящие команды периодически
        esp_zigbee_process_commands();
        
        // Небольшая задержка
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Колбэк подключения ZigBee
 */
static void zigbee_on_connected(void)
{
    ESP_LOGI(TAG, "Колбэк: ZigBee подключено");
    
    // Устанавливаем событие подключения
    xEventGroupSetBits(zigbee_ctx.event_group, ZIGBEE_EVENT_CONNECTED);
}

/**
 * @brief Колбэк отключения ZigBee
 */
static void zigbee_on_disconnected(void)
{
    ESP_LOGI(TAG, "Колбэк: ZigBee отключено");
    
    // Устанавливаем событие отключения
    xEventGroupSetBits(zigbee_ctx.event_group, ZIGBEE_EVENT_DISCONNECTED);
}

/**
 * @brief Колбэк команды ZigBee
 */
static void zigbee_on_command(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    ESP_LOGI(TAG, "Колбэк: Получена команда ZigBee: cmd=%d, len=%d", cmd, len);
    
    // Устанавливаем событие команды
    xEventGroupSetBits(zigbee_ctx.event_group, ZIGBEE_EVENT_COMMAND);
    
    // Вызываем пользовательский колбэк, если задан
    if (zigbee_ctx.command_callback != NULL) {
        zigbee_ctx.command_callback(cmd, data, len);
    }
} 