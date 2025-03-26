/**
 * @file state_management.c
 * @brief Реализация модуля управления состоянием устройства
 */

#include "state_management.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"

// Определение тега для логов
static const char* TAG = "STATE_MGMT";

// Имя пространства имен NVS для хранения состояния
#define NVS_NAMESPACE "window_state"

// Ключи для параметров в NVS
#define NVS_KEY_WINDOW_MODE    "window_mode"
#define NVS_KEY_GAP_PERCENTAGE "gap_pct"
#define NVS_KEY_CALIBRATED     "calibrated"
#define NVS_KEY_ACTIVITY_TIME  "activity_time"

// Текущее состояние устройства
static device_state_t current_state = {
    .window_mode = WINDOW_MODE_CLOSED,
    .gap_percentage = 0,
    .calibrated = false,
    .last_activity_time = 0,
    .resistance_detected = false
};

// Handle для NVS
static nvs_handle_t nvs_handle;

/**
 * @brief Инициализация модуля управления состоянием
 */
esp_err_t state_init(void)
{
    ESP_LOGI(TAG, "Инициализация модуля управления состоянием");
    
    // Открытие NVS для чтения/записи
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка открытия NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Инициализация текущего времени последней активности
    current_state.last_activity_time = esp_timer_get_time() / 1000; // мс
    
    ESP_LOGI(TAG, "Модуль управления состоянием инициализирован");
    return ESP_OK;
}

/**
 * @brief Сохранение текущего состояния в энергонезависимую память
 */
esp_err_t state_save(void)
{
    ESP_LOGI(TAG, "Сохранение состояния: режим=%d, зазор=%d%%, калибровка=%d", 
            current_state.window_mode, current_state.gap_percentage, current_state.calibrated);
    
    esp_err_t err;
    
    // Сохранение режима окна
    err = nvs_set_u8(nvs_handle, NVS_KEY_WINDOW_MODE, (uint8_t)current_state.window_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка сохранения режима окна: %s", esp_err_to_name(err));
        return err;
    }
    
    // Сохранение процента открытия зазора
    err = nvs_set_u8(nvs_handle, NVS_KEY_GAP_PERCENTAGE, current_state.gap_percentage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка сохранения процента зазора: %s", esp_err_to_name(err));
        return err;
    }
    
    // Сохранение флага калибровки
    err = nvs_set_u8(nvs_handle, NVS_KEY_CALIBRATED, (uint8_t)(current_state.calibrated ? 1 : 0));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка сохранения флага калибровки: %s", esp_err_to_name(err));
        return err;
    }
    
    // Сохранение времени последней активности
    err = nvs_set_u32(nvs_handle, NVS_KEY_ACTIVITY_TIME, (uint32_t)current_state.last_activity_time);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка сохранения времени активности: %s", esp_err_to_name(err));
        return err;
    }
    
    // Запись изменений в NVS
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка фиксации изменений в NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Состояние успешно сохранено");
    return ESP_OK;
}

/**
 * @brief Загрузка состояния из энергонезависимой памяти
 */
esp_err_t state_load(void)
{
    ESP_LOGI(TAG, "Загрузка состояния из памяти");
    
    esp_err_t err;
    uint8_t value_u8;
    uint32_t value_u32;
    
    // Загрузка режима окна
    err = nvs_get_u8(nvs_handle, NVS_KEY_WINDOW_MODE, &value_u8);
    if (err == ESP_OK) {
        // Проверка значения на допустимость
        if (value_u8 <= WINDOW_MODE_VENT) {
            current_state.window_mode = (window_mode_t)value_u8;
        } else {
            current_state.window_mode = WINDOW_MODE_CLOSED;
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Ошибка загрузки режима окна: %s", esp_err_to_name(err));
    }
    
    // Загрузка процента открытия зазора
    err = nvs_get_u8(nvs_handle, NVS_KEY_GAP_PERCENTAGE, &value_u8);
    if (err == ESP_OK) {
        // Проверка значения на допустимость
        if (value_u8 <= 100) {
            current_state.gap_percentage = value_u8;
        } else {
            current_state.gap_percentage = 0;
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Ошибка загрузки процента зазора: %s", esp_err_to_name(err));
    }
    
    // Загрузка флага калибровки
    err = nvs_get_u8(nvs_handle, NVS_KEY_CALIBRATED, &value_u8);
    if (err == ESP_OK) {
        current_state.calibrated = (value_u8 == 1);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Ошибка загрузки флага калибровки: %s", esp_err_to_name(err));
    }
    
    // Загрузка времени последней активности
    err = nvs_get_u32(nvs_handle, NVS_KEY_ACTIVITY_TIME, &value_u32);
    if (err == ESP_OK) {
        current_state.last_activity_time = value_u32;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Ошибка загрузки времени активности: %s", esp_err_to_name(err));
    }
    
    ESP_LOGI(TAG, "Загружено состояние: режим=%d, зазор=%d%%, калибровка=%d", 
            current_state.window_mode, current_state.gap_percentage, current_state.calibrated);
            
    return ESP_OK;
}

/**
 * @brief Сброс состояния к заводским настройкам
 */
esp_err_t state_reset(void)
{
    ESP_LOGI(TAG, "Сброс состояния к заводским настройкам");
    
    // Сброс текущего состояния
    current_state.window_mode = WINDOW_MODE_CLOSED;
    current_state.gap_percentage = 0;
    current_state.calibrated = false;
    current_state.last_activity_time = esp_timer_get_time() / 1000; // мс
    current_state.resistance_detected = false;
    
    // Очистка всех записей в пространстве имен
    esp_err_t err = nvs_erase_all(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка очистки NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Запись изменений в NVS
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка фиксации изменений в NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Состояние успешно сброшено");
    return ESP_OK;
}

/**
 * @brief Получение текущего состояния устройства
 */
device_state_t state_get_current(void)
{
    return current_state;
}

/**
 * @brief Обновление режима окна
 */
esp_err_t state_update_window_mode(window_mode_t mode)
{
    // Проверка на допустимость режима
    if (mode > WINDOW_MODE_VENT) {
        ESP_LOGE(TAG, "Недопустимый режим окна: %d", mode);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Обновление режима окна: %d -> %d", current_state.window_mode, mode);
    current_state.window_mode = mode;
    
    // Обновление времени последней активности
    current_state.last_activity_time = esp_timer_get_time() / 1000; // мс
    
    return ESP_OK;
}

/**
 * @brief Обновление процента открытия зазора
 */
esp_err_t state_update_gap_percentage(uint8_t percentage)
{
    // Проверка на допустимость процента
    if (percentage > 100) {
        ESP_LOGE(TAG, "Недопустимый процент зазора: %d", percentage);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Обновление процента зазора: %d%% -> %d%%", current_state.gap_percentage, percentage);
    current_state.gap_percentage = percentage;
    
    // Обновление времени последней активности
    current_state.last_activity_time = esp_timer_get_time() / 1000; // мс
    
    return ESP_OK;
}

/**
 * @brief Обновление флага калибровки
 */
esp_err_t state_update_calibration(bool calibrated)
{
    ESP_LOGI(TAG, "Обновление флага калибровки: %d -> %d", current_state.calibrated, calibrated);
    current_state.calibrated = calibrated;
    
    return ESP_OK;
}

/**
 * @brief Обновление времени последней активности
 */
esp_err_t state_update_activity_time(void)
{
    current_state.last_activity_time = esp_timer_get_time() / 1000; // мс
    return ESP_OK;
}

/**
 * @brief Обновление флага обнаружения сопротивления
 */
esp_err_t state_update_resistance_detected(bool detected)
{
    if (current_state.resistance_detected != detected) {
        ESP_LOGI(TAG, "Обновление флага сопротивления: %d -> %d", current_state.resistance_detected, detected);
        current_state.resistance_detected = detected;
        
        // Если обнаружено сопротивление, обновляем время активности
        if (detected) {
            current_state.last_activity_time = esp_timer_get_time() / 1000; // мс
        }
    }
    
    return ESP_OK;
}

/**
 * @brief Проверка необходимости калибровки
 */
bool state_is_calibration_required(void)
{
    return !current_state.calibrated;
}

/**
 * @brief Получение времени бездействия
 */
uint32_t state_get_inactivity_time(void)
{
    uint32_t current_time = esp_timer_get_time() / 1000; // мс
    return current_time - current_state.last_activity_time;
}

/**
 * @brief Проверка обнаружения сопротивления
 */
bool state_is_resistance_detected(void)
{
    return current_state.resistance_detected;
} 