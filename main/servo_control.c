/**
 * @file servo_control.c
 * @brief Реализация модуля управления сервоприводами для умного окна
 */

#include "servo_control.h"
#include "esp_log.h"
#include "driver/mcpwm_prelude.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// Определение тега для логов
static const char* TAG = "SERVO_CONTROL";

// Константы для управления сервоприводами
#define SERVO_MIN_PULSEWIDTH_US 500   // Минимальная длительность импульса в микросекундах
#define SERVO_MAX_PULSEWIDTH_US 2500  // Максимальная длительность импульса в микросекундах
#define SERVO_FREQUENCY         50    // Частота PWM (50Hz для большинства сервоприводов)

// Задержка плавного движения сервопривода (мс)
#define SERVO_SMOOTH_DELAY_MS   15

// Максимальное значение датчика тока для обнаружения сопротивления
#define DEFAULT_RESISTANCE_THRESHOLD 2000

// Углы поворота для разных режимов
#define ANGLE_CLOSED 0    // Закрыто - 0 градусов
#define ANGLE_OPEN   90   // Открыто - 90 градусов
#define ANGLE_VENT   180  // Проветривание - 180 градусов

// Определения для ADC измерения тока
#define SERVO_CURRENT_ADC_UNIT     ADC_UNIT_1           // Блок АЦП (ADC1)
#define SERVO_CURRENT_ADC_CHANNEL  ADC_CHANNEL_0        // Канал ADC (GPIO36)
#define SERVO_CURRENT_ADC_ATTEN    ADC_ATTEN_DB_11      // Ослабление (0-3.3В)
#define SERVO_CURRENT_ADC_WIDTH    ADC_BITWIDTH_12      // Разрядность (12 бит)

// Структура для хранения состояния сервоприводов
typedef struct {
    mcpwm_timer_handle_t timer;               // Таймер MCPWM
    mcpwm_cmpr_handle_t comparator;           // Компаратор MCPWM
    mcpwm_oper_handle_t operator;             // Оператор MCPWM
    mcpwm_gen_handle_t generator;             // Генератор MCPWM
    int current_angle;                         // Текущий угол (0-180)
    int target_angle;                          // Целевой угол (0-180)
    bool is_enabled;                           // Флаг включения
    uint8_t gpio_pin;                          // Пин GPIO
} servo_t;

// Переменные состояния
static servo_t handle_servo = {0};              // Сервопривод ручки
static servo_t gap_servo = {0};                 // Сервопривод зазора
static window_mode_t current_window_mode = WINDOW_MODE_CLOSED;  // Текущий режим окна
static uint8_t current_gap_percentage = 0;                      // Текущий процент зазора
static uint16_t resistance_threshold = DEFAULT_RESISTANCE_THRESHOLD; // Порог сопротивления
static bool resistance_detected = false;                        // Флаг обнаружения сопротивления

// Добавляем переменные для ADC
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle;
static bool adc_cali_enabled = false;

// Прототипы вспомогательных функций
static esp_err_t setup_servo(servo_t *servo, uint8_t gpio_pin);
static esp_err_t set_servo_angle(servo_t *servo, int angle);
static esp_err_t move_servo_smooth(servo_t *servo, int target_angle);

/**
 * @brief Инициализация сервоприводов
 */
esp_err_t servo_init(uint8_t handle_servo_pin, uint8_t gap_servo_pin)
{
    ESP_LOGI(TAG, "Инициализация сервоприводов: ручка на пине %d, зазор на пине %d", 
             handle_servo_pin, gap_servo_pin);
    
    esp_err_t ret;
    
    // Инициализация сервопривода ручки
    ret = setup_servo(&handle_servo, handle_servo_pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации сервопривода ручки");
        return ret;
    }
    
    // Инициализация сервопривода зазора
    ret = setup_servo(&gap_servo, gap_servo_pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации сервопривода зазора");
        return ret;
    }
    
    // Инициализация ADC для измерения тока
    ret = init_adc_for_current_sensing();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Не удалось инициализировать ADC для измерения тока: %s", esp_err_to_name(ret));
        // Продолжаем работу, но без функции измерения тока
    }
    
    // Установка начальных значений
    current_window_mode = WINDOW_MODE_CLOSED;
    current_gap_percentage = 0;
    resistance_detected = false;
    
    ESP_LOGI(TAG, "Сервоприводы успешно инициализированы");
    return ESP_OK;
}

/**
 * @brief Настройка одного сервопривода
 */
static esp_err_t setup_servo(servo_t *servo, uint8_t gpio_pin)
{
    // Сохраняем пин GPIO
    servo->gpio_pin = gpio_pin;
    servo->current_angle = 0;
    servo->target_angle = 0;
    servo->is_enabled = false;
    
    // Настройка таймера
    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  // 1MHz, 1us на тик
        .period_ticks = 20000,     // 20000 тиков = 20ms = 50Hz
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    
    ESP_RETURN_ON_ERROR(mcpwm_new_timer(&timer_config, &servo->timer), TAG, "Ошибка создания таймера");
    
    // Настройка оператора
    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    
    ESP_RETURN_ON_ERROR(mcpwm_new_operator(&operator_config, &servo->operator), TAG, "Ошибка создания оператора");
    
    // Подключение оператора к таймеру
    ESP_RETURN_ON_ERROR(mcpwm_operator_connect_timer(servo->operator, servo->timer), TAG, "Ошибка подключения таймера");
    
    // Настройка компаратора
    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    
    ESP_RETURN_ON_ERROR(mcpwm_new_comparator(servo->operator, &comparator_config, &servo->comparator), TAG, "Ошибка создания компаратора");
    
    // Настройка генератора
    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = gpio_pin,
    };
    
    ESP_RETURN_ON_ERROR(mcpwm_new_generator(servo->operator, &generator_config, &servo->generator), TAG, "Ошибка создания генератора");
    
    // Настройка действий генератора
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_action_on_timer_event(
        servo->generator,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)),
        TAG, "Ошибка установки действия HIGH");
    
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_action_on_compare_event(
        servo->generator,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, servo->comparator, MCPWM_GEN_ACTION_LOW)),
        TAG, "Ошибка установки действия LOW");
    
    // Запуск таймера
    ESP_RETURN_ON_ERROR(mcpwm_timer_enable(servo->timer), TAG, "Ошибка включения таймера");
    ESP_RETURN_ON_ERROR(mcpwm_timer_start_stop(servo->timer, MCPWM_TIMER_START), TAG, "Ошибка запуска таймера");
    
    // Установка начального положения в 0 градусов
    set_servo_angle(servo, 0);
    
    // Отмечаем, что сервопривод включен
    servo->is_enabled = true;
    
    return ESP_OK;
}

/**
 * @brief Установка угла поворота сервопривода
 */
static esp_err_t set_servo_angle(servo_t *servo, int angle)
{
    if (!servo->is_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Ограничение угла в пределах 0-180 градусов
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    
    // Сохранение текущего угла
    servo->current_angle = angle;
    
    // Расчет длительности импульса (масштабирование угла от 0-180 к SERVO_MIN_PULSEWIDTH_US - SERVO_MAX_PULSEWIDTH_US)
    uint32_t pulse_width_us = SERVO_MIN_PULSEWIDTH_US + (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) * angle / 180;
    
    // Установка сравнения для генерации импульса
    ESP_RETURN_ON_ERROR(mcpwm_comparator_set_compare_value(servo->comparator, pulse_width_us), 
                         TAG, "Ошибка установки значения сравнения");
    
    ESP_LOGD(TAG, "Установлен угол %d° (импульс %u мкс)", angle, pulse_width_us);
    
    return ESP_OK;
}

/**
 * @brief Плавное перемещение сервопривода к целевому углу
 */
static esp_err_t move_servo_smooth(servo_t *servo, int target_angle)
{
    if (!servo->is_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Ограничение целевого угла
    if (target_angle < 0) target_angle = 0;
    if (target_angle > 180) target_angle = 180;
    
    // Текущий угол
    int current = servo->current_angle;
    
    // Плавное перемещение
    ESP_LOGI(TAG, "Плавное перемещение сервопривода от %d° к %d°", current, target_angle);
    
    if (current < target_angle) {
        // Увеличение угла
        for (int angle = current; angle <= target_angle; angle++) {
            esp_err_t ret = set_servo_angle(servo, angle);
            if (ret != ESP_OK) return ret;
            
            // Проверка сопротивления движению
            if (servo_check_resistance()) {
                ESP_LOGW(TAG, "Обнаружено сопротивление при угле %d°", angle);
                return ESP_ERR_TIMEOUT;
            }
            
            vTaskDelay(pdMS_TO_TICKS(SERVO_SMOOTH_DELAY_MS));
        }
    } else if (current > target_angle) {
        // Уменьшение угла
        for (int angle = current; angle >= target_angle; angle--) {
            esp_err_t ret = set_servo_angle(servo, angle);
            if (ret != ESP_OK) return ret;
            
            // Проверка сопротивления движению
            if (servo_check_resistance()) {
                ESP_LOGW(TAG, "Обнаружено сопротивление при угле %d°", angle);
                return ESP_ERR_TIMEOUT;
            }
            
            vTaskDelay(pdMS_TO_TICKS(SERVO_SMOOTH_DELAY_MS));
        }
    }
    
    servo->target_angle = target_angle;
    ESP_LOGI(TAG, "Перемещение завершено");
    
    return ESP_OK;
}

/**
 * @brief Изменение режима окна
 */
esp_err_t servo_set_window_mode(window_mode_t mode)
{
    ESP_LOGI(TAG, "Установка режима окна: %d", mode);
    
    int target_angle;
    
    // Определение целевого угла в зависимости от режима
    switch (mode) {
        case WINDOW_MODE_CLOSED:
            target_angle = ANGLE_CLOSED;
            break;
        case WINDOW_MODE_OPEN:
            target_angle = ANGLE_OPEN;
            break;
        case WINDOW_MODE_VENT:
            target_angle = ANGLE_VENT;
            break;
        default:
            ESP_LOGE(TAG, "Неизвестный режим окна: %d", mode);
            return ESP_ERR_INVALID_ARG;
    }
    
    // Плавное перемещение к целевому углу
    esp_err_t ret = move_servo_smooth(&handle_servo, target_angle);
    
    // Если режим окна изменился успешно, обновляем текущий режим
    if (ret == ESP_OK) {
        current_window_mode = mode;
        
        // Если окно закрыто, устанавливаем зазор в 0
        if (mode == WINDOW_MODE_CLOSED) {
            current_gap_percentage = 0;
            move_servo_smooth(&gap_servo, 0);
        }
    }
    
    return ret;
}

/**
 * @brief Управление зазором окна
 */
esp_err_t servo_set_gap(uint8_t percentage)
{
    ESP_LOGI(TAG, "Установка зазора окна: %d%%", percentage);
    
    // Проверка, что окно в режиме открыто
    if (current_window_mode != WINDOW_MODE_OPEN) {
        ESP_LOGW(TAG, "Настройка зазора доступна только в режиме OPEN");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Ограничение процента в диапазоне 0-100
    if (percentage > 100) {
        percentage = 100;
    }
    
    // Преобразование процента в угол (0-90 градусов)
    int target_angle = percentage * 90 / 100;
    
    // Плавное перемещение к целевому углу
    esp_err_t ret = move_servo_smooth(&gap_servo, target_angle);
    
    // Если зазор изменился успешно, обновляем текущий процент
    if (ret == ESP_OK) {
        current_gap_percentage = percentage;
    }
    
    return ret;
}

/**
 * @brief Получение текущего режима окна
 */
window_mode_t servo_get_window_mode(void)
{
    return current_window_mode;
}

/**
 * @brief Получение текущего положения зазора
 */
uint8_t servo_get_gap(void)
{
    return current_gap_percentage;
}

/**
 * @brief Установка порогового значения для определения сопротивления
 */
esp_err_t servo_set_resistance_threshold(uint16_t threshold)
{
    resistance_threshold = threshold;
    return ESP_OK;
}

/**
 * @brief Функция инициализации ADC для измерения тока
 */
static esp_err_t init_adc_for_current_sensing(void)
{
    ESP_LOGI(TAG, "Инициализация ADC для измерения тока сервопривода");
    
    // Конфигурация ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = SERVO_CURRENT_ADC_UNIT,
    };
    
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));
    
    // Конфигурация канала ADC
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = SERVO_CURRENT_ADC_ATTEN,
        .bitwidth = SERVO_CURRENT_ADC_WIDTH,
    };
    
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, SERVO_CURRENT_ADC_CHANNEL, &chan_cfg));
    
    // Попытка калибровки ADC для более точных измерений
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = SERVO_CURRENT_ADC_UNIT,
        .atten = SERVO_CURRENT_ADC_ATTEN,
        .bitwidth = SERVO_CURRENT_ADC_WIDTH,
    };
    
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle);
    if (ret == ESP_OK) {
        adc_cali_enabled = true;
        ESP_LOGI(TAG, "Калибровка ADC включена");
    } else {
        ESP_LOGW(TAG, "Калибровка ADC невозможна, будут использоваться сырые значения");
        adc_cali_enabled = false;
    }
    
    return ESP_OK;
}

/**
 * @brief Реализация чтения с датчика тока
 * 
 * В реальном устройстве эта функция использует ADC для чтения
 * значения с датчика тока.
 * 
 * @return uint16_t Значение тока (0-4095)
 */
static uint16_t read_current_sensor(void)
{
    int adc_raw = 0;
    int voltage = 0;
    
    // Если ADC не инициализирован, возвращаем значение ниже порога
    if (adc1_handle == NULL) {
        ESP_LOGD(TAG, "ADC не инициализирован, возвращаем значение по умолчанию");
        return 1000; // Значение ниже порога сопротивления
    }
    
    // Получение сырого значения ADC
    esp_err_t ret = adc_oneshot_read(adc1_handle, SERVO_CURRENT_ADC_CHANNEL, &adc_raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Ошибка чтения ADC: %s", esp_err_to_name(ret));
        return 1000; // Значение ниже порога сопротивления
    }
    
    // Если калибровка включена, преобразуем в милливольты
    if (adc_cali_enabled) {
        ret = adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Ошибка преобразования сырого значения в напряжение: %s", esp_err_to_name(ret));
        }
        ESP_LOGD(TAG, "Ток сервопривода: %d мВ", voltage);
    } else {
        ESP_LOGD(TAG, "Ток сервопривода (сырое значение): %d", adc_raw);
    }
    
    // Преобразование напряжения в ток (зависит от используемого датчика тока)
    // В этом примере предполагается, что у нас есть датчик тока,
    // который выдает линейное напряжение, пропорциональное току
    
    // Для демонстрации просто возвращаем сырое значение
    return (uint16_t)adc_raw;
}

/**
 * @brief Проверка на наличие механического сопротивления
 * 
 * Чтение значения с датчика тока и сравнение его с пороговым значением.
 */
bool servo_check_resistance(void)
{
    ESP_LOGD(TAG, "Проверка механического сопротивления");
    
    // Чтение значения тока с датчика
    uint16_t current_value = read_current_sensor();
    
    // Если установлен флаг принудительной симуляции, возвращаем true
    if (resistance_detected) {
        ESP_LOGW(TAG, "Симуляция сопротивления активна");
        return true;
    }
    
    // Проверка превышения порога
    if (current_value > resistance_threshold) {
        ESP_LOGW(TAG, "Обнаружено сопротивление! Значение тока: %d, порог: %d", 
                 current_value, resistance_threshold);
        resistance_detected = true;
        return true;
    }
    
    resistance_detected = false;
    return false;
}

/**
 * @brief Включение режима симуляции сопротивления для тестирования
 * 
 * @param enable Включить/выключить симуляцию сопротивления
 */
void servo_simulate_resistance(bool enable)
{
    if (enable) {
        ESP_LOGW(TAG, "Включен режим симуляции сопротивления");
        resistance_detected = true;
    } else {
        ESP_LOGI(TAG, "Отключен режим симуляции сопротивления");
        resistance_detected = false;
    }
}

/**
 * @brief Отключение сервоприводов
 */
esp_err_t servo_disable(void)
{
    ESP_LOGI(TAG, "Отключение сервоприводов");
    
    // Отключение сервопривода ручки
    if (handle_servo.is_enabled) {
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(handle_servo.generator, 0, true), 
                            TAG, "Ошибка отключения сервопривода ручки");
        handle_servo.is_enabled = false;
    }
    
    // Отключение сервопривода зазора
    if (gap_servo.is_enabled) {
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(gap_servo.generator, 0, true), 
                            TAG, "Ошибка отключения сервопривода зазора");
        gap_servo.is_enabled = false;
    }
    
    return ESP_OK;
}

/**
 * @brief Калибровка сервоприводов
 */
esp_err_t servo_calibrate(void)
{
    ESP_LOGI(TAG, "Калибровка сервоприводов");
    
    // Включение сервоприводов, если они отключены
    if (!handle_servo.is_enabled) {
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(handle_servo.generator, 0, false), 
                            TAG, "Ошибка включения сервопривода ручки");
        handle_servo.is_enabled = true;
    }
    
    if (!gap_servo.is_enabled) {
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(gap_servo.generator, 0, false), 
                            TAG, "Ошибка включения сервопривода зазора");
        gap_servo.is_enabled = true;
    }
    
    // Калибровка сервопривода ручки (полный цикл движения)
    ESP_LOGI(TAG, "Калибровка сервопривода ручки");
    
    // Перемещение на 0 градусов
    ESP_RETURN_ON_ERROR(move_servo_smooth(&handle_servo, 0), TAG, "Ошибка калибровки на 0°");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Перемещение на 180 градусов
    ESP_RETURN_ON_ERROR(move_servo_smooth(&handle_servo, 180), TAG, "Ошибка калибровки на 180°");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Возврат на 0 градусов
    ESP_RETURN_ON_ERROR(move_servo_smooth(&handle_servo, 0), TAG, "Ошибка возврата на 0°");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Калибровка сервопривода зазора
    ESP_LOGI(TAG, "Калибровка сервопривода зазора");
    
    // Перемещение на 0 градусов
    ESP_RETURN_ON_ERROR(move_servo_smooth(&gap_servo, 0), TAG, "Ошибка калибровки на 0°");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Перемещение на 90 градусов
    ESP_RETURN_ON_ERROR(move_servo_smooth(&gap_servo, 90), TAG, "Ошибка калибровки на 90°");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Возврат на 0 градусов
    ESP_RETURN_ON_ERROR(move_servo_smooth(&gap_servo, 0), TAG, "Ошибка возврата на 0°");
    
    // Устанавливаем режим окна и зазор
    current_window_mode = WINDOW_MODE_CLOSED;
    current_gap_percentage = 0;
    
    ESP_LOGI(TAG, "Калибровка завершена успешно");
    
    return ESP_OK;
}

/**
 * @brief Освобождение ресурсов ADC
 * 
 * Эта функция должна вызываться при остановке работы устройства
 * для корректного освобождения ресурсов ADC.
 * 
 * @return esp_err_t ESP_OK в случае успеха
 */
esp_err_t servo_deinit(void)
{
    ESP_LOGI(TAG, "Деинициализация модуля сервоприводов");
    
    // Отключение сервоприводов
    esp_err_t ret = servo_disable();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Ошибка отключения сервоприводов: %s", esp_err_to_name(ret));
    }
    
    // Освобождение ресурсов ADC
    if (adc1_handle != NULL) {
        ESP_LOGI(TAG, "Освобождение ресурсов ADC");
        
        // Удаление калибровки ADC
        if (adc_cali_enabled && adc1_cali_handle != NULL) {
            ret = adc_cali_delete_scheme_curve_fitting(adc1_cali_handle);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Ошибка удаления калибровки ADC: %s", esp_err_to_name(ret));
            }
            adc1_cali_handle = NULL;
            adc_cali_enabled = false;
        }
        
        // Удаление блока ADC
        ret = adc_oneshot_del_unit(adc1_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Ошибка удаления блока ADC: %s", esp_err_to_name(ret));
        }
        adc1_handle = NULL;
    }
    
    // Освобождение ресурсов MCPWM для сервоприводов
    if (handle_servo.timer != NULL) {
        ESP_LOGI(TAG, "Освобождение ресурсов сервопривода ручки");
        
        if (handle_servo.generator != NULL) {
            mcpwm_del_generator(handle_servo.generator);
            handle_servo.generator = NULL;
        }
        
        if (handle_servo.comparator != NULL) {
            mcpwm_del_comparator(handle_servo.comparator);
            handle_servo.comparator = NULL;
        }
        
        if (handle_servo.operator != NULL) {
            mcpwm_del_operator(handle_servo.operator);
            handle_servo.operator = NULL;
        }
        
        mcpwm_del_timer(handle_servo.timer);
        handle_servo.timer = NULL;
    }
    
    if (gap_servo.timer != NULL) {
        ESP_LOGI(TAG, "Освобождение ресурсов сервопривода зазора");
        
        if (gap_servo.generator != NULL) {
            mcpwm_del_generator(gap_servo.generator);
            gap_servo.generator = NULL;
        }
        
        if (gap_servo.comparator != NULL) {
            mcpwm_del_comparator(gap_servo.comparator);
            gap_servo.comparator = NULL;
        }
        
        if (gap_servo.operator != NULL) {
            mcpwm_del_operator(gap_servo.operator);
            gap_servo.operator = NULL;
        }
        
        mcpwm_del_timer(gap_servo.timer);
        gap_servo.timer = NULL;
    }
    
    ESP_LOGI(TAG, "Модуль сервоприводов успешно деинициализирован");
    return ESP_OK;
} 