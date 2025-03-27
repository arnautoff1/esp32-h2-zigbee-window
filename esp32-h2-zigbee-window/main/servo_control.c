/**
 * @file servo_control.c
 * @brief Реализация модуля управления сервоприводами для умного окна
 */

#include "servo_control.h"
#include "esp_log.h"
#include <string.h>

#define TAG "SERVO"

// Текущее состояние сервоприводов
static struct {
    bool initialized;                // Статус инициализации
    servo_config_t handle_config;    // Конфигурация сервопривода ручки
    servo_config_t gap_config;       // Конфигурация сервопривода зазора
    handle_position_t handle_pos;    // Текущее положение ручки
    uint8_t gap_percentage;          // Текущий процент открытия зазора
    bool handle_calibrated;          // Флаг калибровки сервопривода ручки
    bool gap_calibrated;             // Флаг калибровки сервопривода зазора
} servo_ctx = {
    .initialized = false,
    .handle_pos = HANDLE_POSITION_CLOSED,
    .gap_percentage = 0,
    .handle_calibrated = false,
    .gap_calibrated = false
};

/**
 * @brief Инициализация модуля управления сервоприводами
 */
esp_err_t servo_init(servo_config_t *handle_config, servo_config_t *gap_config)
{
    ESP_LOGI(TAG, "Инициализация модуля управления сервоприводами");
    
    if (servo_ctx.initialized) {
        ESP_LOGW(TAG, "Модуль управления сервоприводами уже инициализирован");
        return ESP_OK;
    }
    
    if (handle_config == NULL || gap_config == NULL) {
        ESP_LOGE(TAG, "Неверные параметры: handle_config == NULL или gap_config == NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Копируем конфигурации
    memcpy(&servo_ctx.handle_config, handle_config, sizeof(servo_config_t));
    memcpy(&servo_ctx.gap_config, gap_config, sizeof(servo_config_t));
    
    // Здесь была бы реальная инициализация сервоприводов через GPIO и PWM
    // В данной заглушке просто устанавливаем флаг инициализации
    
    servo_ctx.initialized = true;
    ESP_LOGI(TAG, "Модуль управления сервоприводами успешно инициализирован");
    
    return ESP_OK;
}

/**
 * @brief Установка положения ручки окна
 */
esp_err_t servo_set_handle_position(handle_position_t position)
{
    ESP_LOGI(TAG, "Установка положения ручки окна: %d", position);
    
    if (!servo_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления сервоприводами не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Проверка валидности положения
    if (position != HANDLE_POSITION_CLOSED && 
        position != HANDLE_POSITION_OPEN && 
        position != HANDLE_POSITION_VENTILATE) {
        ESP_LOGE(TAG, "Неверное положение ручки: %d", position);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Здесь была бы реальная установка положения сервопривода через PWM
    // В данной заглушке просто запоминаем положение
    
    servo_ctx.handle_pos = position;
    ESP_LOGI(TAG, "Положение ручки установлено: %d", position);
    
    return ESP_OK;
}

/**
 * @brief Установка процента открытия зазора окна
 */
esp_err_t servo_set_gap_percentage(uint8_t percentage)
{
    ESP_LOGI(TAG, "Установка процента открытия зазора: %d%%", percentage);
    
    if (!servo_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления сервоприводами не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Проверка валидности процента
    if (percentage > 100) {
        ESP_LOGE(TAG, "Неверный процент открытия: %d", percentage);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Здесь была бы реальная установка положения сервопривода через PWM
    // В данной заглушке просто запоминаем процент
    
    servo_ctx.gap_percentage = percentage;
    ESP_LOGI(TAG, "Процент открытия зазора установлен: %d%%", percentage);
    
    return ESP_OK;
}

/**
 * @brief Получение текущего положения ручки
 */
handle_position_t servo_get_handle_position(void)
{
    return servo_ctx.handle_pos;
}

/**
 * @brief Получение текущего процента открытия зазора
 */
uint8_t servo_get_gap_percentage(void)
{
    return servo_ctx.gap_percentage;
}

/**
 * @brief Калибровка сервопривода
 */
esp_err_t servo_calibrate(servo_type_t type)
{
    ESP_LOGI(TAG, "Калибровка сервопривода: %d", type);
    
    if (!servo_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления сервоприводами не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Здесь была бы реальная калибровка сервопривода
    // В данной заглушке просто устанавливаем флаг калибровки
    
    if (type == SERVO_TYPE_HANDLE) {
        servo_ctx.handle_calibrated = true;
        ESP_LOGI(TAG, "Сервопривод ручки откалиброван");
    } else if (type == SERVO_TYPE_GAP) {
        servo_ctx.gap_calibrated = true;
        ESP_LOGI(TAG, "Сервопривод зазора откалиброван");
    } else {
        ESP_LOGE(TAG, "Неверный тип сервопривода: %d", type);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/**
 * @brief Проверка механического сопротивления
 */
bool servo_check_resistance(servo_type_t type)
{
    // Здесь была бы реальная проверка механического сопротивления
    // В данной заглушке просто возвращаем false (нет сопротивления)
    
    return false;
}

/**
 * @brief Остановка сервопривода
 */
esp_err_t servo_stop(servo_type_t type)
{
    ESP_LOGI(TAG, "Остановка сервопривода: %d", type);
    
    if (!servo_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления сервоприводами не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Здесь была бы реальная остановка сервопривода
    // В данной заглушке просто выводим сообщение
    
    if (type == SERVO_TYPE_HANDLE) {
        ESP_LOGI(TAG, "Сервопривод ручки остановлен");
    } else if (type == SERVO_TYPE_GAP) {
        ESP_LOGI(TAG, "Сервопривод зазора остановлен");
    } else {
        ESP_LOGE(TAG, "Неверный тип сервопривода: %d", type);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
} 