/**
 * @file main.c
 * @brief Главный файл проекта умного окна на ESP32-H2 с ZigBee
 * @version 1.0
 * @date 2023-03-24
 */

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

// Подключение заголовочных файлов модулей
#include "servo_control.h"
#include "zigbee_handler.h"
#include "ota_update.h"
#include "power_management.h"
#include "state_management.h"

// Определение тегов для логов
static const char* TAG = "WINDOW_MAIN";

// Определение пинов GPIO
#define HANDLE_SERVO_PIN 4    // Пин для сервопривода ручки
#define GAP_SERVO_PIN    5    // Пин для сервопривода зазора

// Определение задержек и периодов (в миллисекундах)
#define ZIGBEE_REPORT_INTERVAL  10000  // Интервал отправки состояния в ZigBee
#define STATE_SAVE_INTERVAL     60000  // Интервал сохранения состояния
#define OTA_CHECK_INTERVAL     300000  // Интервал проверки обновлений (5 минут)

// Определение приоритетов задач
#define TASK_PRIORITY_ZIGBEE    5
#define TASK_PRIORITY_OTA       3
#define TASK_PRIORITY_POWER     4
#define TASK_PRIORITY_MAIN      2

// Размеры стека задач (в словах)
#define STACK_SIZE_ZIGBEE       4096
#define STACK_SIZE_OTA          4096
#define STACK_SIZE_POWER        2048
#define STACK_SIZE_MAIN         2048

// Определение битов событий
#define EVENT_WINDOW_MODE_CHANGED   (1 << 0)
#define EVENT_GAP_CHANGED           (1 << 1)
#define EVENT_RESISTANCE_DETECTED   (1 << 2)
#define EVENT_CALIBRATION_REQUIRED  (1 << 3)

// Дескрипторы задач
TaskHandle_t xTaskZigBee = NULL;
TaskHandle_t xTaskOTA = NULL;
TaskHandle_t xTaskPower = NULL;

// Группа событий
EventGroupHandle_t xWindowEvents;

// Прототипы функций
static void init_nvs(void);
static void start_services(void);
static void init_device(void);
static void main_task(void *pvParameter);
static void zigbee_task(void *pvParameter);
static void handle_window_events(void);

/**
 * @brief Точка входа в программу
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Запуск приложения умного окна на ESP32-H2 с ZigBee");
    
    // Инициализация NVS (энергонезависимая память)
    init_nvs();
    
    // Инициализация группы событий
    xWindowEvents = xEventGroupCreate();
    
    // Инициализация устройства и его компонентов
    init_device();
    
    // Запуск сервисов в отдельных задачах
    start_services();
    
    // Запуск основной задачи
    xTaskCreate(main_task, "main_task", STACK_SIZE_MAIN, NULL, TASK_PRIORITY_MAIN, NULL);
    
    ESP_LOGI(TAG, "Инициализация завершена");
}

/**
 * @brief Инициализация NVS (энергонезависимая память)
 */
static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Стирание NVS и повторная инициализация");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS инициализирован");
}

/**
 * @brief Инициализация устройства и его компонентов
 */
static void init_device(void)
{
    ESP_LOGI(TAG, "Инициализация компонентов устройства");
    
    // Инициализация модуля управления состоянием
    ESP_ERROR_CHECK(state_init());
    
    // Загрузка состояния из памяти
    ESP_ERROR_CHECK(state_load());
    
    // Инициализация модуля управления сервоприводами
    ESP_ERROR_CHECK(servo_init(HANDLE_SERVO_PIN, GAP_SERVO_PIN));
    
    // Проверка необходимости калибровки
    if (state_is_calibration_required()) {
        ESP_LOGI(TAG, "Требуется калибровка сервоприводов");
        ESP_ERROR_CHECK(servo_calibrate());
        ESP_ERROR_CHECK(state_update_calibration(true));
    }
    
    // Инициализация ZigBee
    zigbee_config_t zigbee_config = {
        .device_name = "Smart Window",
        .manufacturer = "Custom",
        .model = "ESP32-H2-Window-1.0",
        .pan_id = 0x1234,
        .channel = 15,
        .dev_type = ZIGBEE_DEVICE_TYPE_COVER // Тип "шторы" для Алисы
    };
    ESP_ERROR_CHECK(zigbee_init(&zigbee_config));
    
    // Инициализация OTA
    ota_config_t ota_config = {
        .server_url = "https://example.com/firmware",
        .firmware_version = "1.0.0",
        .check_interval_ms = OTA_CHECK_INTERVAL,
        .auto_check = true,
        .auto_update = false
    };
    ESP_ERROR_CHECK(ota_init(&ota_config));
    
    // Инициализация управления питанием
    power_config_t power_config = {
        .source = POWER_SOURCE_BATTERY,
        .low_battery_threshold = 3300,         // 3.3V
        .critical_battery_threshold = 3000,    // 3.0V
        .sleep_timeout_ms = 300000,            // 5 минут
        .enable_auto_sleep = true
    };
    ESP_ERROR_CHECK(power_init(&power_config));
}

/**
 * @brief Запуск сервисов в отдельных задачах
 */
static void start_services(void)
{
    ESP_LOGI(TAG, "Запуск сервисов");
    
    // Запуск ZigBee
    ESP_ERROR_CHECK(zigbee_start());
    xTaskCreate(zigbee_task, "zigbee_task", STACK_SIZE_ZIGBEE, NULL, TASK_PRIORITY_ZIGBEE, &xTaskZigBee);
    
    // Запуск OTA
    ESP_ERROR_CHECK(ota_start());
    xTaskCreate(ota_task_handler, "ota_task", STACK_SIZE_OTA, NULL, TASK_PRIORITY_OTA, &xTaskOTA);
    
    // Запуск управления питанием
    ESP_ERROR_CHECK(power_start());
    xTaskCreate(power_task_handler, "power_task", STACK_SIZE_POWER, NULL, TASK_PRIORITY_POWER, &xTaskPower);
}

/**
 * @brief Задача ZigBee
 */
static void zigbee_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Запуск задачи ZigBee");
    
    // Включение режима сопряжения при первом запуске
    if (zigbee_get_state() == ZIGBEE_STATE_DISCONNECTED) {
        ESP_LOGI(TAG, "Включение режима сопряжения ZigBee");
        zigbee_enable_pairing_mode(300); // 5 минут
    }
    
    TickType_t xLastReportTime = xTaskGetTickCount();
    
    for (;;) {
        // Обработка входящих команд ZigBee
        zigbee_process_incoming_commands();
        
        // Периодическая отправка состояния
        if ((xTaskGetTickCount() - xLastReportTime) >= pdMS_TO_TICKS(ZIGBEE_REPORT_INTERVAL)) {
            ESP_LOGI(TAG, "Отправка состояния в ZigBee");
            zigbee_report_state();
            xLastReportTime = xTaskGetTickCount();
        }
        
        // Обработка событий окна
        EventBits_t uxBits = xEventGroupGetBits(xWindowEvents);
        
        // Если изменился режим окна или зазор
        if (uxBits & (EVENT_WINDOW_MODE_CHANGED | EVENT_GAP_CHANGED)) {
            window_mode_t mode = servo_get_window_mode();
            uint8_t gap = servo_get_gap();
            
            // Отправляем новое состояние
            zigbee_send_window_mode(mode);
            zigbee_send_gap_position(gap);
            
            // Отправляем уведомление об изменении режима
            zigbee_send_alert(ZIGBEE_ALERT_MODE_CHANGE, (uint8_t)mode);
            
            // Сбрасываем события
            xEventGroupClearBits(xWindowEvents, EVENT_WINDOW_MODE_CHANGED | EVENT_GAP_CHANGED);
        }
        
        // Обработка обнаружения сопротивления
        if (uxBits & EVENT_RESISTANCE_DETECTED) {
            ESP_LOGW(TAG, "Обнаружено механическое сопротивление");
            
            // Сбрасываем событие
            xEventGroupClearBits(xWindowEvents, EVENT_RESISTANCE_DETECTED);
        }
        
        // Проверка состояния батареи
        if (power_is_low_battery()) {
            // Отправка уведомления о низком заряде
            zigbee_send_alert(ZIGBEE_ALERT_LOW_BATTERY, power_get_battery_level());
        }
        
        // Короткая задержка
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Основная задача устройства
 */
static void main_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Запуск основной задачи");
    
    // Восстановление последнего состояния
    device_state_t current_state = state_get_current();
    
    // Установка окна в последнее известное состояние
    ESP_LOGI(TAG, "Восстановление последнего состояния: режим=%d, зазор=%d%%", 
             current_state.window_mode, current_state.gap_percentage);
    
    servo_set_window_mode(current_state.window_mode);
    servo_set_gap(current_state.gap_percentage);
    
    // Отключение сервоприводов после установки
    servo_disable();
    
    TickType_t xLastSaveTime = xTaskGetTickCount();
    
    for (;;) {
        // Обработка событий окна
        handle_window_events();
        
        // Периодическое сохранение состояния
        if ((xTaskGetTickCount() - xLastSaveTime) >= pdMS_TO_TICKS(STATE_SAVE_INTERVAL)) {
            ESP_LOGI(TAG, "Сохранение состояния");
            state_save();
            xLastSaveTime = xTaskGetTickCount();
        }
        
        // Проверка состояния батареи
        if (power_is_low_battery()) {
            ESP_LOGW(TAG, "Низкий заряд батареи: %d%%", power_get_battery_level());
        }
        
        // Если батарея критически разряжена, переходим в режим сна
        if (power_is_critical_battery()) {
            ESP_LOGW(TAG, "Критически низкий заряд батареи. Переход в режим сна");
            power_set_mode(POWER_MODE_SLEEP);
            power_deep_sleep(0); // Бесконечный сон до сброса
        }
        
        // Задержка
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Обработка событий окна
 */
static void handle_window_events(void)
{
    // Проверка обнаружения сопротивления
    if (servo_check_resistance()) {
        ESP_LOGW(TAG, "Обнаружено механическое сопротивление");
        
        // Остановка сервоприводов
        servo_disable();
        
        // Обновление состояния
        state_update_resistance_detected(true);
        
        // Отправка уведомления через ZigBee
        if (zigbee_get_state() == ZIGBEE_STATE_CONNECTED) {
            ESP_LOGI(TAG, "Отправка уведомления о механическом сопротивлении");
            zigbee_send_alert(ZIGBEE_ALERT_RESISTANCE, 1);
        }
        
        // Установка события
        xEventGroupSetBits(xWindowEvents, EVENT_RESISTANCE_DETECTED);
    } else {
        state_update_resistance_detected(false);
    }
} 