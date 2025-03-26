/**
 * @file power_management.c
 * @brief Реализация модуля управления питанием для умного окна
 */

#include "power_management.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Определение тега для логов
static const char* TAG = "POWER_MGMT";

// Пин GPIO для измерения напряжения батареи
#define BATTERY_ADC_CHANNEL         ADC_CHANNEL_0  // GPIO 0 (настройте в соответствии с вашим устройством)
#define BATTERY_ADC_ATTEN           ADC_ATTEN_DB_11
#define BATTERY_ADC_WIDTH           ADC_BITWIDTH_12
#define BATTERY_ADC_UNIT            ADC_UNIT_1

// Пин GPIO для определения источника питания (внешнее/батарея)
#define POWER_SOURCE_GPIO           5  // Настройте в соответствии с вашим устройством

// Полное напряжение батареи в милливольтах
#define BATTERY_FULL_VOLTAGE        4200  // 4.2V для Li-ion аккумулятора
#define BATTERY_EMPTY_VOLTAGE       3000  // 3.0V для Li-ion аккумулятора

// Периоды проверки и ожидания
#define POWER_CHECK_INTERVAL_MS     10000  // 10 секунд между проверками питания
#define WAKE_UP_GPIO_MASK           ((1ULL << POWER_SOURCE_GPIO))

// Структура данных для управления питанием
typedef struct {
    power_config_t config;               // Конфигурация питания
    power_mode_t current_mode;           // Текущий режим питания
    uint16_t battery_voltage;            // Текущее напряжение батареи (мВ)
    uint8_t battery_level;               // Текущий уровень заряда (0-100%)
    uint32_t last_activity_time;         // Время последней активности
    bool is_initialized;                 // Флаг инициализации
    TaskHandle_t task_handle;            // Хендл задачи управления питанием
    
    // ADC хендлы
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t adc_cali_handle;
} power_state_t;

// Текущее состояние
static power_state_t power_state = {
    .current_mode = POWER_MODE_NORMAL,
    .battery_voltage = 0,
    .battery_level = 0,
    .last_activity_time = 0,
    .is_initialized = false,
    .task_handle = NULL,
    .adc_handle = NULL,
    .adc_cali_handle = NULL
};

// Прототипы вспомогательных функций
static esp_err_t power_init_adc(void);
static esp_err_t power_init_gpio(void);
static esp_err_t power_update_battery_status(void);
static uint16_t power_read_battery_voltage(void);
static uint8_t power_calculate_battery_level(uint16_t voltage);
static bool power_check_external_source(void);

/**
 * @brief Инициализация модуля управления питанием
 */
esp_err_t power_init(power_config_t *config)
{
    ESP_LOGI(TAG, "Инициализация модуля управления питанием");
    
    // Проверка аргументов
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Копирование конфигурации
    memcpy(&power_state.config, config, sizeof(power_config_t));
    
    // Инициализация GPIO
    esp_err_t ret = power_init_gpio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Инициализация ADC для измерения напряжения батареи
    ret = power_init_adc();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Определение типа источника питания
    if (power_check_external_source()) {
        power_state.config.source = POWER_SOURCE_EXTERNAL;
        ESP_LOGI(TAG, "Обнаружено внешнее питание");
    } else {
        power_state.config.source = POWER_SOURCE_BATTERY;
        ESP_LOGI(TAG, "Работа от батареи");
    }
    
    // Обновление статуса батареи
    power_update_battery_status();
    
    // Инициализация времени последней активности
    power_state.last_activity_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Установка флага инициализации
    power_state.is_initialized = true;
    
    ESP_LOGI(TAG, "Модуль управления питанием инициализирован: режим=%d, напряжение=%dmV, заряд=%d%%",
             power_state.current_mode, power_state.battery_voltage, power_state.battery_level);
    
    return ESP_OK;
}

/**
 * @brief Инициализация GPIO для управления питанием
 */
static esp_err_t power_init_gpio(void)
{
    ESP_LOGI(TAG, "Инициализация GPIO для управления питанием");
    
    // Настройка пина определения источника питания
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << POWER_SOURCE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка конфигурации GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

/**
 * @brief Инициализация ADC для измерения напряжения батареи
 */
static esp_err_t power_init_adc(void)
{
    ESP_LOGI(TAG, "Инициализация ADC для измерения напряжения батареи");
    
    // Настройка ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &power_state.adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Настройка канала ADC
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = BATTERY_ADC_WIDTH,
        .atten = BATTERY_ADC_ATTEN,
    };
    
    ret = adc_oneshot_config_channel(power_state.adc_handle, BATTERY_ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка конфигурации канала ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Настройка калибровки ADC
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = BATTERY_ADC_UNIT,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_WIDTH,
    };
    
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &power_state.adc_cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Калибровка ADC не поддерживается на данном устройстве: %s", esp_err_to_name(ret));
        // Продолжаем без калибровки
    }
    
    return ESP_OK;
}

/**
 * @brief Запуск модуля управления питанием
 */
esp_err_t power_start(void)
{
    ESP_LOGI(TAG, "Запуск модуля управления питанием");
    
    // Проверка инициализации
    if (!power_state.is_initialized) {
        ESP_LOGE(TAG, "Модуль управления питанием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Создание задачи управления питанием
    BaseType_t task_created = xTaskCreate(
        power_task_handler,
        "power_task",
        4096,   // Размер стека
        NULL,   // Параметры
        5,      // Приоритет
        &power_state.task_handle
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Ошибка создания задачи управления питанием");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Модуль управления питанием запущен");
    
    return ESP_OK;
}

/**
 * @brief Остановка модуля управления питанием
 */
esp_err_t power_stop(void)
{
    ESP_LOGI(TAG, "Остановка модуля управления питанием");
    
    // Проверка существования задачи
    if (power_state.task_handle != NULL) {
        // Удаление задачи
        vTaskDelete(power_state.task_handle);
        power_state.task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "Модуль управления питанием остановлен");
    
    return ESP_OK;
}

/**
 * @brief Установка режима питания
 */
esp_err_t power_set_mode(power_mode_t mode)
{
    ESP_LOGI(TAG, "Установка режима питания: %d", mode);
    
    // Проверка инициализации
    if (!power_state.is_initialized) {
        ESP_LOGE(TAG, "Модуль управления питанием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Применение режима питания
    switch (mode) {
        case POWER_MODE_NORMAL:
            // Нормальный режим работы
            ESP_LOGI(TAG, "Переход в нормальный режим питания");
            
            // Отключение режима энергосбережения, если он был активен
            // В реальном устройстве здесь будет код для управления питанием компонентов
            
            break;
            
        case POWER_MODE_LOW_POWER:
            // Режим низкого энергопотребления
            ESP_LOGI(TAG, "Переход в режим низкого энергопотребления");
            
            // Здесь код для снижения энергопотребления
            // Например, уменьшение частоты процессора, отключение неиспользуемых периферийных устройств
            
            break;
            
        case POWER_MODE_SLEEP:
            // Режим глубокого сна
            ESP_LOGI(TAG, "Переход в режим глубокого сна");
            
            // Подготовка к глубокому сну
            // В реальном устройстве здесь будет код для подготовки к глубокому сну
            
            // Переход в режим глубокого сна будет выполнен отдельным вызовом power_deep_sleep()
            
            break;
            
        default:
            ESP_LOGE(TAG, "Неизвестный режим питания: %d", mode);
            return ESP_ERR_INVALID_ARG;
    }
    
    // Обновление текущего режима
    power_state.current_mode = mode;
    
    return ESP_OK;
}

/**
 * @brief Получение текущего режима питания
 */
power_mode_t power_get_mode(void)
{
    return power_state.current_mode;
}

/**
 * @brief Обновление статуса батареи
 */
static esp_err_t power_update_battery_status(void)
{
    // Измерение напряжения батареи
    power_state.battery_voltage = power_read_battery_voltage();
    
    // Расчет уровня заряда
    power_state.battery_level = power_calculate_battery_level(power_state.battery_voltage);
    
    ESP_LOGI(TAG, "Обновлен статус батареи: напряжение=%dmV, заряд=%d%%",
             power_state.battery_voltage, power_state.battery_level);
    
    return ESP_OK;
}

/**
 * @brief Чтение напряжения батареи
 */
static uint16_t power_read_battery_voltage(void)
{
    int adc_raw;
    uint16_t voltage = 0;
    
    // Чтение значения ADC
    if (adc_oneshot_read(power_state.adc_handle, BATTERY_ADC_CHANNEL, &adc_raw) == ESP_OK) {
        // Если калибровка доступна, используем её для получения милливольт
        if (power_state.adc_cali_handle != NULL) {
            int voltage_mv;
            if (adc_cali_raw_to_voltage(power_state.adc_cali_handle, adc_raw, &voltage_mv) == ESP_OK) {
                voltage = (uint16_t)voltage_mv;
            }
        } else {
            // Если калибровка недоступна, используем примерное масштабирование
            // Для ESP32-H2 с 12-битным ADC (0-4095) и аттенюацией 11dB (0-3.3V)
            voltage = (uint16_t)((adc_raw * 3300) / 4095);
        }
    }
    
    // Применение коэффициента делителя напряжения, если используется
    // Пример: если делитель напряжения 1:2, умножаем на 2
    // voltage *= 2;
    
    return voltage;
}

/**
 * @brief Расчет уровня заряда батареи в процентах
 */
static uint8_t power_calculate_battery_level(uint16_t voltage)
{
    // Проверка граничных значений
    if (voltage >= BATTERY_FULL_VOLTAGE) {
        return 100;
    } else if (voltage <= BATTERY_EMPTY_VOLTAGE) {
        return 0;
    }
    
    // Линейное масштабирование от 0% до 100%
    uint8_t level = (uint8_t)(((voltage - BATTERY_EMPTY_VOLTAGE) * 100) / 
                              (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE));
    
    return level;
}

/**
 * @brief Проверка наличия внешнего источника питания
 */
static bool power_check_external_source(void)
{
    // Чтение состояния пина
    int level = gpio_get_level(POWER_SOURCE_GPIO);
    
    // В зависимости от схемы:
    // Если HIGH означает внешнее питание, возвращаем level == 1
    // Если LOW означает внешнее питание, возвращаем level == 0
    return level == 1;
}

/**
 * @brief Получение уровня заряда батареи
 */
uint8_t power_get_battery_level(void)
{
    return power_state.battery_level;
}

/**
 * @brief Получение напряжения батареи
 */
uint16_t power_get_battery_voltage(void)
{
    return power_state.battery_voltage;
}

/**
 * @brief Проверка состояния низкого заряда батареи
 */
bool power_is_low_battery(void)
{
    // Проверка источника питания
    if (power_state.config.source == POWER_SOURCE_EXTERNAL) {
        return false;
    }
    
    // Проверка порога низкого заряда
    return power_state.battery_voltage <= power_state.config.low_battery_threshold;
}

/**
 * @brief Проверка критического состояния батареи
 */
bool power_is_critical_battery(void)
{
    // Проверка источника питания
    if (power_state.config.source == POWER_SOURCE_EXTERNAL) {
        return false;
    }
    
    // Проверка критического порога заряда
    return power_state.battery_voltage <= power_state.config.critical_battery_threshold;
}

/**
 * @brief Определение типа источника питания
 */
power_source_t power_get_source(void)
{
    return power_check_external_source() ? POWER_SOURCE_EXTERNAL : POWER_SOURCE_BATTERY;
}

/**
 * @brief Переход в режим глубокого сна
 */
esp_err_t power_deep_sleep(uint64_t sleep_duration_ms)
{
    ESP_LOGI(TAG, "Переход в режим глубокого сна на %llu мс", sleep_duration_ms);
    
    // Настройка таймера пробуждения, если задана длительность сна
    if (sleep_duration_ms > 0) {
        ESP_LOGI(TAG, "Настройка таймера пробуждения через %llu мс", sleep_duration_ms);
        esp_sleep_enable_timer_wakeup(sleep_duration_ms * 1000); // Перевод из мс в мкс
    }
    
    // Настройка пробуждения по GPIO (если требуется)
    ESP_LOGI(TAG, "Настройка пробуждения по GPIO");
    esp_sleep_enable_ext1_wakeup(WAKE_UP_GPIO_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);
    
    // Сохранение состояния перед переходом в сон (при необходимости)
    
    // Переход в глубокий сон
    ESP_LOGI(TAG, "Переход в глубокий сон...");
    esp_deep_sleep_start();
    
    // Этот код никогда не будет выполнен, поскольку устройство перезагрузится после сна
    return ESP_OK;
}

/**
 * @brief Сброс таймера сна
 */
esp_err_t power_reset_sleep_timer(void)
{
    ESP_LOGI(TAG, "Сброс таймера сна");
    
    // Обновление времени последней активности
    power_state.last_activity_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    return ESP_OK;
}

/**
 * @brief Обработчик задачи управления питанием
 */
void power_task_handler(void *pvParameter)
{
    ESP_LOGI(TAG, "Запуск задачи управления питанием");
    
    uint32_t last_check_time = 0;
    
    // Бесконечный цикл обработки
    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Периодическое обновление статуса питания
        if (current_time - last_check_time >= POWER_CHECK_INTERVAL_MS) {
            // Обновление информации об источнике питания
            power_source_t current_source = power_check_external_source() ? 
                                           POWER_SOURCE_EXTERNAL : POWER_SOURCE_BATTERY;
            
            if (current_source != power_state.config.source) {
                power_state.config.source = current_source;
                ESP_LOGI(TAG, "Изменен источник питания: %d", current_source);
            }
            
            // Обновление статуса батареи
            power_update_battery_status();
            
            // Обновление времени последней проверки
            last_check_time = current_time;
        }
        
        // Проверка необходимости перехода в спящий режим
        if (power_state.config.enable_auto_sleep) {
            uint32_t inactivity_time = current_time - power_state.last_activity_time;
            
            if (inactivity_time >= power_state.config.sleep_timeout_ms) {
                ESP_LOGI(TAG, "Превышен таймаут бездействия (%lu мс). Переход в режим сна", 
                         (unsigned long)inactivity_time);
                
                // Переход в режим сна
                power_set_mode(POWER_MODE_SLEEP);
                
                // В реальном устройстве здесь может быть дополнительная логика
                // для сохранения состояния перед переходом в глубокий сон
                
                // Переход в глубокий сон
                power_deep_sleep(0); // Бесконечный сон до внешнего пробуждения
            }
        }
        
        // Короткая задержка
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // На случай выхода из цикла
    vTaskDelete(NULL);
} 