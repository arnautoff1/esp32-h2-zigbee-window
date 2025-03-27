/**
 * @file main.c
 * @brief Главный файл проекта умного окна на базе ESP32-H2 с ZigBee
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "servo_control.h"
#include "zigbee_device.h"
#include "ota_update.h"
#include "power_management.h"
#include "state_management.h"

#define TAG "MAIN"

// Задачи FreeRTOS
static TaskHandle_t state_task_handle = NULL;
static TaskHandle_t zigbee_task_handle = NULL;
static TaskHandle_t power_task_handle = NULL;
static TaskHandle_t ota_task_handle = NULL;

// Конфигурации компонентов
static servo_config_t handle_servo_config = {
    .gpio_pin = 4,                  // GPIO для сервопривода ручки
    .min_pulse_width_us = 500,      // Минимальная ширина импульса (мкс)
    .max_pulse_width_us = 2500,     // Максимальная ширина импульса (мкс)
    .max_angle_deg = 180,           // Максимальный угол поворота (градусы)
    .invert_direction = false       // Не инвертировать направление
};

static servo_config_t gap_servo_config = {
    .gpio_pin = 5,                  // GPIO для сервопривода зазора
    .min_pulse_width_us = 500,      // Минимальная ширина импульса (мкс)
    .max_pulse_width_us = 2500,     // Максимальная ширина импульса (мкс)
    .max_angle_deg = 180,           // Максимальный угол поворота (градусы)
    .invert_direction = false       // Не инвертировать направление
};

static power_config_t power_config = {
    .battery_adc_channel = 0,       // Канал АЦП для измерения напряжения батареи
    .external_power_gpio = 6,       // GPIO для определения внешнего питания
    .low_battery_threshold = 3.2,   // Порог низкого заряда батареи (В)
    .critical_battery_threshold = 2.8, // Порог критического заряда батареи (В)
    .check_interval_ms = 60000      // Интервал проверки состояния батареи (мс)
};

static state_config_t state_config = {
    .save_to_nvs = true,            // Сохранять состояние в NVS
    .save_interval_ms = 300000,     // Интервал автоматического сохранения (мс)
    .restore_on_boot = true         // Восстанавливать состояние при загрузке
};

static ota_config_t ota_config = {
    .server_url = "https://example.com/firmware",  // URL сервера обновлений
    .firmware_version = "1.0.0",                   // Текущая версия прошивки
    .check_interval_ms = 86400000,                 // Интервал проверки обновлений (мс)
    .auto_check = true,                            // Автоматическая проверка обновлений
    .auto_update = false                           // Автоматическое применение обновлений
};

// Структура конфигурации ZigBee
typedef struct {
    const char *device_name;        // Имя устройства
    const char *manufacturer;       // Производитель
    const char *model;              // Модель
    uint16_t pan_id;                // ID сети PAN
    uint8_t channel;                // Канал ZigBee
    uint8_t endpoint;               // Номер конечной точки
    bool pairing_mode_on_start;     // Включить режим сопряжения при запуске
    uint32_t pairing_mode_timeout_ms; // Таймаут режима сопряжения (мс)
} zigbee_device_config_t;

static zigbee_device_config_t zigbee_config = {
    .device_name = "Smart Window",                // Имя устройства
    .manufacturer = "ESP32-H2",                   // Производитель
    .model = "Window-ZB-01",                      // Модель
    .pan_id = 0x1234,                            // ID сети PAN
    .channel = 15,                               // Канал ZigBee
    .endpoint = 1,                               // Номер конечной точки
    .pairing_mode_on_start = true,               // Включить режим сопряжения при запуске
    .pairing_mode_timeout_ms = 300000            // Таймаут режима сопряжения (мс)
};

// Обработчик команд ZigBee
static void zigbee_command_handler(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    ESP_LOGI(TAG, "Получена команда ZigBee: cmd=%d, len=%d", cmd, len);
    
    if (len < 2) {
        ESP_LOGW(TAG, "Недостаточно данных в команде");
        return;
    }
    
    window_mode_t mode = (window_mode_t)data[0];
    uint8_t percentage = data[1];
    
    ESP_LOGI(TAG, "Параметры команды: режим %d, процент %d", mode, percentage);
    
    // Устанавливаем режим работы окна
    esp_err_t err = state_set_window_mode(mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка установки режима работы окна: %s", esp_err_to_name(err));
        return;
    }
    
    // Если режим "открыто" или "пользовательский", устанавливаем процент открытия
    if (mode == WINDOW_MODE_OPEN || mode == WINDOW_MODE_CUSTOM) {
        err = state_set_gap_percentage(percentage);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ошибка установки процента открытия: %s", esp_err_to_name(err));
            return;
        }
    }
    
    // Отправляем отчет о состоянии
    zigbee_device_report_state(mode, percentage);
}

// Задача управления состоянием
static void state_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Запуск задачи управления состоянием");
    
    while (1) {
        // Обрабатываем состояние окна
        state_task_handler();
        
        // Проверяем наличие механического сопротивления
        if (servo_check_resistance(SERVO_TYPE_HANDLE)) {
            zigbee_device_send_alert(ZIGBEE_ALERT_STUCK, 0);
        }
        
        // Задержка
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Задача ZigBee
static void zigbee_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Запуск задачи ZigBee");
    
    while (1) {
        // Обрабатываем команды ZigBee
        zigbee_device_process_commands();
        
        // Задержка
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Задача управления питанием
static void power_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Запуск задачи управления питанием");
    
    while (1) {
        // Мониторим состояние питания
        power_monitor_task();
        
        // Проверяем состояние батареи
        battery_state_t battery_state = power_get_battery_state();
        if (battery_state == BATTERY_STATE_LOW || battery_state == BATTERY_STATE_CRITICAL) {
            // Отправляем уведомление о низком заряде батареи
            zigbee_device_send_alert(ZIGBEE_ALERT_LOW_BATTERY, power_get_battery_percentage());
            
            // В случае критического заряда переходим в режим низкого потребления
            if (battery_state == BATTERY_STATE_CRITICAL) {
                power_set_mode(POWER_MODE_LOW_POWER);
            }
        }
        
        // Задержка
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Запуск умного окна на базе ESP32-H2 с ZigBee ===");
    
    // Инициализация NVS (Non-Volatile Storage)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS требует форматирования");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    
    // Вывод информации о системе
    ESP_LOGI(TAG, "ESP-IDF Версия: %s", esp_get_idf_version());
    
    // Инициализация модулей
    ESP_LOGI(TAG, "Инициализация модулей...");
    
    // Инициализация управления сервоприводами
    ESP_ERROR_CHECK(servo_init(&handle_servo_config, &gap_servo_config));
    ESP_LOGI(TAG, "Модуль сервоприводов инициализирован");
    
    // Инициализация управления питанием
    ESP_ERROR_CHECK(power_init(&power_config));
    ESP_LOGI(TAG, "Модуль управления питанием инициализирован");
    
    // Инициализация управления состоянием
    ESP_ERROR_CHECK(state_init(&state_config));
    ESP_LOGI(TAG, "Модуль управления состоянием инициализирован");
    
    // Инициализация OTA-обновлений
    ESP_ERROR_CHECK(ota_init(&ota_config));
    ESP_LOGI(TAG, "Модуль OTA-обновлений инициализирован");
    
    // Инициализация ZigBee-устройства
    ESP_ERROR_CHECK(zigbee_device_init(zigbee_command_handler));
    ESP_LOGI(TAG, "Модуль ZigBee инициализирован");
    
    // Запуск модулей
    ESP_LOGI(TAG, "Запуск модулей...");
    
    // Запуск ZigBee-устройства
    ESP_ERROR_CHECK(zigbee_device_start());
    ESP_LOGI(TAG, "Модуль ZigBee запущен");
    
    // Запуск OTA-обновлений
    ESP_ERROR_CHECK(ota_start());
    ESP_LOGI(TAG, "Модуль OTA-обновлений запущен");
    
    // Восстановление состояния
    if (state_config.restore_on_boot) {
        ESP_LOGI(TAG, "Восстановление состояния...");
        esp_err_t restore_err = state_restore();
        if (restore_err != ESP_OK) {
            ESP_LOGW(TAG, "Ошибка восстановления состояния: %d", restore_err);
            // Устанавливаем состояние по умолчанию
            state_set_window_mode(WINDOW_MODE_CLOSED);
            state_set_gap_percentage(0);
        }
    }
    
    // Создание задач
    ESP_LOGI(TAG, "Создание задач...");
    
    // Задача управления состоянием
    xTaskCreate(state_task, "state_task", 4096, NULL, 5, &state_task_handle);
    
    // Задача ZigBee
    xTaskCreate(zigbee_task, "zigbee_task", 4096, NULL, 5, &zigbee_task_handle);
    
    // Задача управления питанием
    xTaskCreate(power_task, "power_task", 4096, NULL, 3, &power_task_handle);
    
    // Задача OTA-обновлений (запускается внутри ota_start)
    ESP_LOGI(TAG, "Все модули запущены, система готова к работе");
} 