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
 * @brief Проверка на наличие механического сопротивления
 * 
 * Примечание: В реальном устройстве здесь будет чтение значения с датчика тока
 * и сравнение его с пороговым значением. Для демонстрации используется заглушка.
 */
bool servo_check_resistance(void)
{
    // В реальном устройстве здесь будет чтение с датчика тока/напряжения
    // Для демонстрации возвращаем значение флага, установленного в других функциях
    return resistance_detected;
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