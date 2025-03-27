/**
 * @file power_management.c
 * @brief Реализация модуля управления питанием для умного окна
 */

#include "power_management.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <string.h>

#define TAG "POWER"

// Настройки для пересчета значения АЦП в напряжение
#define ADC_VREF_MV   3300    // Опорное напряжение АЦП в милливольтах
#define ADC_WIDTH     ADC_BITWIDTH_12   // Разрядность АЦП
#define ADC_ATTENUATION ADC_ATTEN_DB_11 // Затухание АЦП
#define ADC_CHANNEL_VBAT 0  // Канал АЦП для измерения напряжения батареи

// Коэффициенты для расчета процента заряда батареи
#define BATTERY_MAX_VOLTAGE 4.2f  // Максимальное напряжение батареи
#define BATTERY_MIN_VOLTAGE 3.0f  // Минимальное напряжение батареи

// Интервал проверки состояния батареи по умолчанию
#define DEFAULT_CHECK_INTERVAL_MS 60000 // 1 минута

// Текущее состояние питания
static struct {
    bool initialized;                   // Статус инициализации
    power_config_t config;              // Конфигурация
    battery_state_t battery_state;      // Состояние батареи
    power_mode_t power_mode;            // Режим питания
    float battery_voltage;              // Напряжение батареи
    uint8_t battery_percentage;         // Процент заряда
    bool external_power;                // Наличие внешнего питания
    uint64_t last_check_time;           // Время последней проверки
    adc_oneshot_unit_handle_t adc_handle; // Указатель на инициализированный АЦП
} power_ctx = {
    .initialized = false,
    .battery_state = BATTERY_STATE_NORMAL,
    .power_mode = POWER_MODE_NORMAL,
    .battery_voltage = 0.0f,
    .battery_percentage = 0,
    .external_power = false,
    .last_check_time = 0
};

// Прототипы вспомогательных функций
static esp_err_t power_check_battery(void);
static void power_setup_gpio(void);
static float power_adc_reading_to_voltage(int adc_reading);

/**
 * @brief Инициализация модуля управления питанием
 */
esp_err_t power_init(power_config_t *config)
{
    ESP_LOGI(TAG, "Инициализация модуля управления питанием");
    
    if (power_ctx.initialized) {
        ESP_LOGW(TAG, "Модуль управления питанием уже инициализирован");
        return ESP_OK;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Неверный параметр: config == NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Копируем конфигурацию
    memcpy(&power_ctx.config, config, sizeof(power_config_t));
    
    // Инициализируем GPIO для определения внешнего питания
    power_setup_gpio();
    
    // Инициализируем АЦП для измерения напряжения батареи
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    
    // В заглушке просто пропускаем реальную инициализацию АЦП
    // В реальном коде здесь была бы инициализация АЦП
    /*
    esp_err_t err = adc_oneshot_new_unit(&adc_cfg, &power_ctx.adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации АЦП: %d", err);
        return err;
    }
    
    // Настраиваем канал АЦП для измерения напряжения батареи
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_WIDTH,
        .atten = ADC_ATTENUATION,
    };
    
    err = adc_oneshot_config_channel(power_ctx.adc_handle, config->battery_adc_channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка настройки канала АЦП: %d", err);
        return err;
    }
    */
    
    // Проверяем состояние батареи и наличие внешнего питания
    power_check_battery();
    
    power_ctx.initialized = true;
    power_ctx.last_check_time = esp_timer_get_time() / 1000; // мс
    
    ESP_LOGI(TAG, "Модуль управления питанием успешно инициализирован");
    ESP_LOGI(TAG, "Напряжение батареи: %.2f В (%d%%)", power_ctx.battery_voltage, power_ctx.battery_percentage);
    ESP_LOGI(TAG, "Внешнее питание: %s", power_ctx.external_power ? "подключено" : "отключено");
    
    return ESP_OK;
}

/**
 * @brief Получение текущего напряжения батареи
 */
float power_get_battery_voltage(void)
{
    if (!power_ctx.initialized) {
        return 0.0f;
    }
    
    return power_ctx.battery_voltage;
}

/**
 * @brief Получение процента заряда батареи
 */
uint8_t power_get_battery_percentage(void)
{
    if (!power_ctx.initialized) {
        return 0;
    }
    
    return power_ctx.battery_percentage;
}

/**
 * @brief Получение текущего состояния батареи
 */
battery_state_t power_get_battery_state(void)
{
    if (!power_ctx.initialized) {
        return BATTERY_STATE_NORMAL;
    }
    
    return power_ctx.battery_state;
}

/**
 * @brief Проверка наличия внешнего питания
 */
bool power_is_external_power_connected(void)
{
    if (!power_ctx.initialized) {
        return false;
    }
    
    return power_ctx.external_power;
}

/**
 * @brief Установка режима питания
 */
esp_err_t power_set_mode(power_mode_t mode)
{
    ESP_LOGI(TAG, "Установка режима питания: %d", mode);
    
    if (!power_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления питанием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Проверяем валидность режима
    if (mode != POWER_MODE_NORMAL && 
        mode != POWER_MODE_LOW_POWER && 
        mode != POWER_MODE_DEEP_SLEEP) {
        ESP_LOGE(TAG, "Неверный режим питания: %d", mode);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Если режим не изменился, выходим
    if (mode == power_ctx.power_mode) {
        ESP_LOGW(TAG, "Режим питания уже установлен: %d", mode);
        return ESP_OK;
    }
    
    // Применяем настройки режима питания
    switch (mode) {
        case POWER_MODE_NORMAL:
            // Отключаем энергосбережение
            // В реальном коде здесь были бы соответствующие настройки
            ESP_LOGI(TAG, "Установлен нормальный режим питания");
            break;
            
        case POWER_MODE_LOW_POWER:
            // Включаем режим низкого энергопотребления
            // В реальном коде здесь были бы соответствующие настройки
            ESP_LOGI(TAG, "Установлен режим низкого энергопотребления");
            break;
            
        case POWER_MODE_DEEP_SLEEP:
            // Подготовка к глубокому сну
            // В реальном коде здесь были бы соответствующие настройки
            ESP_LOGI(TAG, "Установлен режим глубокого сна");
            break;
            
        default:
            ESP_LOGE(TAG, "Неподдерживаемый режим питания: %d", mode);
            return ESP_ERR_INVALID_ARG;
    }
    
    power_ctx.power_mode = mode;
    return ESP_OK;
}

/**
 * @brief Получение текущего режима питания
 */
power_mode_t power_get_mode(void)
{
    if (!power_ctx.initialized) {
        return POWER_MODE_NORMAL;
    }
    
    return power_ctx.power_mode;
}

/**
 * @brief Переход в режим глубокого сна
 */
esp_err_t power_enter_deep_sleep(uint32_t sleep_time_ms)
{
    ESP_LOGI(TAG, "Переход в режим глубокого сна на %u мс", sleep_time_ms);
    
    if (!power_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления питанием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Устанавливаем таймер пробуждения, если указано время сна
    if (sleep_time_ms > 0) {
        ESP_LOGI(TAG, "Установка таймера пробуждения: %u мс", sleep_time_ms);
        // В реальном коде здесь была бы настройка таймера пробуждения
        // esp_sleep_enable_timer_wakeup(sleep_time_ms * 1000);
    }
    
    // Настраиваем пробуждение по GPIO (например, при подключении внешнего питания)
    // В реальном коде здесь была бы настройка пробуждения по GPIO
    // esp_sleep_enable_ext0_wakeup(power_ctx.config.external_power_gpio, 1);
    
    ESP_LOGI(TAG, "Переход в глубокий сон...");
    
    // В реальном коде здесь был бы переход в глубокий сон
    // esp_deep_sleep_start();
    
    // В заглушке просто логируем это событие
    ESP_LOGI(TAG, "Это заглушка - переход в глубокий сон пропущен");
    
    return ESP_OK;
}

/**
 * @brief Обработчик мониторинга питания
 */
esp_err_t power_monitor_task(void)
{
    if (!power_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления питанием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Получаем текущее время
    uint64_t current_time = esp_timer_get_time() / 1000; // мс
    
    // Проверяем, прошел ли интервал проверки
    if (current_time - power_ctx.last_check_time >= power_ctx.config.check_interval_ms) {
        // Проверяем состояние батареи
        power_check_battery();
        power_ctx.last_check_time = current_time;
        
        // Автоматическое управление режимом питания в зависимости от состояния батареи
        if (power_ctx.external_power) {
            // При наличии внешнего питания всегда используем нормальный режим
            power_set_mode(POWER_MODE_NORMAL);
        } else {
            // При работе от батареи режим зависит от уровня заряда
            switch (power_ctx.battery_state) {
                case BATTERY_STATE_NORMAL:
                    power_set_mode(POWER_MODE_NORMAL);
                    break;
                case BATTERY_STATE_LOW:
                    power_set_mode(POWER_MODE_LOW_POWER);
                    break;
                case BATTERY_STATE_CRITICAL:
                    // При критическом уровне заряда переходим в глубокий сон
                    power_set_mode(POWER_MODE_DEEP_SLEEP);
                    power_enter_deep_sleep(0); // Бесконечный сон до внешнего пробуждения
                    break;
                default:
                    break;
            }
        }
    }
    
    return ESP_OK;
}

/**
 * @brief Проверка состояния батареи
 */
static esp_err_t power_check_battery(void)
{
    // Проверяем наличие внешнего питания
    power_ctx.external_power = gpio_get_level(power_ctx.config.external_power_gpio) == 1;
    
    // В заглушке используем псевдослучайные значения для симуляции напряжения батареи
    if (power_ctx.external_power) {
        // При наличии внешнего питания батарея заряжается
        power_ctx.battery_voltage = 4.1f + ((float)rand() / RAND_MAX) * 0.1f; // 4.1-4.2В
        power_ctx.battery_state = BATTERY_STATE_CHARGING;
    } else {
        // При работе от батареи напряжение зависит от текущего режима
        switch (power_ctx.power_mode) {
            case POWER_MODE_NORMAL:
                power_ctx.battery_voltage = 3.7f + ((float)rand() / RAND_MAX) * 0.3f; // 3.7-4.0В
                break;
            case POWER_MODE_LOW_POWER:
                power_ctx.battery_voltage = 3.3f + ((float)rand() / RAND_MAX) * 0.4f; // 3.3-3.7В
                break;
            case POWER_MODE_DEEP_SLEEP:
                power_ctx.battery_voltage = 3.0f + ((float)rand() / RAND_MAX) * 0.3f; // 3.0-3.3В
                break;
        }
    }
    
    // В реальной реализации здесь было бы чтение АЦП и преобразование в напряжение
    // int adc_reading = 0;
    // adc_oneshot_read(power_ctx.adc_handle, power_ctx.config.battery_adc_channel, &adc_reading);
    // power_ctx.battery_voltage = power_adc_reading_to_voltage(adc_reading);
    
    // Рассчитываем процент заряда батареи
    power_ctx.battery_percentage = (uint8_t)(100.0f * (power_ctx.battery_voltage - BATTERY_MIN_VOLTAGE) / 
                                           (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE));
    
    // Ограничиваем процент заряда диапазоном 0-100
    if (power_ctx.battery_percentage > 100) {
        power_ctx.battery_percentage = 100;
    }
    
    // Определяем состояние батареи
    if (power_ctx.external_power) {
        power_ctx.battery_state = BATTERY_STATE_EXTERNAL;
    } else if (power_ctx.battery_voltage <= power_ctx.config.critical_battery_threshold) {
        power_ctx.battery_state = BATTERY_STATE_CRITICAL;
    } else if (power_ctx.battery_voltage <= power_ctx.config.low_battery_threshold) {
        power_ctx.battery_state = BATTERY_STATE_LOW;
    } else {
        power_ctx.battery_state = BATTERY_STATE_NORMAL;
    }
    
    ESP_LOGI(TAG, "Проверка батареи: %.2f В, %d%%, состояние %d", 
             power_ctx.battery_voltage, power_ctx.battery_percentage, power_ctx.battery_state);
    
    return ESP_OK;
}

/**
 * @brief Настройка GPIO для определения внешнего питания
 */
static void power_setup_gpio(void)
{
    // Настраиваем GPIO для определения внешнего питания как вход
    // В реальном коде здесь была бы настройка GPIO
    // gpio_config_t io_conf = {
    //     .pin_bit_mask = (1ULL << power_ctx.config.external_power_gpio),
    //     .mode = GPIO_MODE_INPUT,
    //     .pull_up_en = GPIO_PULLUP_DISABLE,
    //     .pull_down_en = GPIO_PULLDOWN_ENABLE,
    //     .intr_type = GPIO_INTR_DISABLE
    // };
    // gpio_config(&io_conf);
}

/**
 * @brief Преобразование значения АЦП в напряжение
 */
static float power_adc_reading_to_voltage(int adc_reading)
{
    // Преобразование значения АЦП в напряжение
    // В реальном коде здесь было бы реальное преобразование в соответствии с характеристиками АЦП
    // и делителем напряжения, если используется
    float voltage = (float)adc_reading * ADC_VREF_MV / (1 << ADC_WIDTH) / 1000.0f;
    
    // Применяем коэффициент делителя напряжения (если используется)
    // Пример: если делитель напряжения 1:2, то умножаем на 2
    voltage *= 2.0f;
    
    return voltage;
} 