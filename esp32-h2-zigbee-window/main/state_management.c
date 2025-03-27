/**
 * @file state_management.c
 * @brief Реализация модуля управления состоянием для умного окна
 */

#include "state_management.h"
#include "servo_control.h"
#include "zigbee_device.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

#define TAG "STATE"

// Ключи NVS для хранения состояния
#define NVS_NAMESPACE           "window_state"   // Пространство имен NVS
#define NVS_KEY_MODE            "mode"           // Режим работы окна
#define NVS_KEY_HANDLE_POS      "handle_pos"     // Положение ручки
#define NVS_KEY_GAP_PERCENTAGE  "gap_pct"        // Процент открытия зазора
#define NVS_KEY_CALIBRATED      "calibrated"     // Статус калибровки

// Текущее состояние
static struct {
    bool initialized;               // Статус инициализации
    state_config_t config;          // Конфигурация
    window_state_t state;           // Текущее состояние окна
    uint64_t last_save_time;        // Время последнего сохранения
    nvs_handle_t nvs_handle;        // Указатель на NVS
    bool nvs_opened;                // Статус открытия NVS
} state_ctx = {
    .initialized = false,
    .last_save_time = 0,
    .nvs_opened = false
};

// Прототипы вспомогательных функций
static esp_err_t state_save_to_nvs(void);
static esp_err_t state_restore_from_nvs(void);
static esp_err_t state_open_nvs(void);
static void state_close_nvs(void);
static void state_update_last_action_time(void);

/**
 * @brief Инициализация модуля управления состоянием
 */
esp_err_t state_init(state_config_t *config)
{
    ESP_LOGI(TAG, "Инициализация модуля управления состоянием");
    
    if (state_ctx.initialized) {
        ESP_LOGW(TAG, "Модуль управления состоянием уже инициализирован");
        return ESP_OK;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Неверный параметр: config == NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Копируем конфигурацию
    memcpy(&state_ctx.config, config, sizeof(state_config_t));
    
    // Инициализируем состояние окна
    state_ctx.state.mode = WINDOW_MODE_CLOSED;
    state_ctx.state.handle_pos = HANDLE_POSITION_CLOSED;
    state_ctx.state.gap_percentage = 0;
    state_ctx.state.calibrated = false;
    state_ctx.state.in_motion = false;
    state_ctx.state.last_action_time = 0;
    
    // Если настроено сохранение в NVS, открываем NVS
    if (state_ctx.config.save_to_nvs) {
        esp_err_t err = state_open_nvs();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ошибка открытия NVS: %s", esp_err_to_name(err));
            // Продолжаем инициализацию, но без NVS
            state_ctx.config.save_to_nvs = false;
        }
    }
    
    // Если настроено восстановление при загрузке, восстанавливаем состояние
    if (state_ctx.config.restore_on_boot && state_ctx.config.save_to_nvs && state_ctx.nvs_opened) {
        esp_err_t err = state_restore_from_nvs();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Ошибка восстановления состояния из NVS: %s", esp_err_to_name(err));
            // Продолжаем с дефолтными значениями
        } else {
            ESP_LOGI(TAG, "Состояние успешно восстановлено из NVS");
        }
    }
    
    state_ctx.initialized = true;
    state_ctx.last_save_time = esp_timer_get_time() / 1000; // мс
    
    ESP_LOGI(TAG, "Модуль управления состоянием успешно инициализирован");
    ESP_LOGI(TAG, "Режим: %d, положение ручки: %d, проценты: %d%%", 
             state_ctx.state.mode, state_ctx.state.handle_pos, state_ctx.state.gap_percentage);
    
    return ESP_OK;
}

/**
 * @brief Установка режима работы окна
 */
esp_err_t state_set_window_mode(window_mode_t mode)
{
    ESP_LOGI(TAG, "Установка режима работы окна: %d", mode);
    
    if (!state_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления состоянием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Проверяем валидность режима
    if (mode != WINDOW_MODE_CLOSED && 
        mode != WINDOW_MODE_OPEN && 
        mode != WINDOW_MODE_VENTILATE && 
        mode != WINDOW_MODE_CUSTOM) {
        ESP_LOGE(TAG, "Неверный режим работы: %d", mode);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Если режим не изменился, выходим
    if (mode == state_ctx.state.mode) {
        ESP_LOGW(TAG, "Режим работы уже установлен: %d", mode);
        return ESP_OK;
    }
    
    // Устанавливаем положение ручки в зависимости от режима
    handle_position_t new_handle_pos;
    uint8_t new_gap_percentage;
    
    switch (mode) {
        case WINDOW_MODE_CLOSED:
            new_handle_pos = HANDLE_POSITION_CLOSED;
            new_gap_percentage = 0;
            break;
            
        case WINDOW_MODE_OPEN:
            new_handle_pos = HANDLE_POSITION_OPEN;
            new_gap_percentage = 100;
            break;
            
        case WINDOW_MODE_VENTILATE:
            new_handle_pos = HANDLE_POSITION_VENTILATE;
            new_gap_percentage = 20; // 20% для проветривания
            break;
            
        case WINDOW_MODE_CUSTOM:
            // В пользовательском режиме не меняем положение ручки и зазор
            new_handle_pos = state_ctx.state.handle_pos;
            new_gap_percentage = state_ctx.state.gap_percentage;
            break;
            
        default:
            ESP_LOGE(TAG, "Неподдерживаемый режим: %d", mode);
            return ESP_ERR_INVALID_ARG;
    }
    
    // Устанавливаем положение ручки
    esp_err_t err = servo_set_handle_position(new_handle_pos);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка установки положения ручки: %s", esp_err_to_name(err));
        return err;
    }
    
    // Устанавливаем процент открытия зазора, если не пользовательский режим
    if (mode != WINDOW_MODE_CUSTOM) {
        err = servo_set_gap_percentage(new_gap_percentage);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ошибка установки процента открытия: %s", esp_err_to_name(err));
            return err;
        }
    }
    
    // Обновляем состояние
    state_ctx.state.mode = mode;
    state_ctx.state.handle_pos = new_handle_pos;
    
    if (mode != WINDOW_MODE_CUSTOM) {
        state_ctx.state.gap_percentage = new_gap_percentage;
    }
    
    state_ctx.state.in_motion = true;
    state_update_last_action_time();
    
    // Отправляем отчет о состоянии через ZigBee
    zigbee_device_report_state(mode, state_ctx.state.gap_percentage);
    
    // Отправляем уведомление об изменении режима
    zigbee_device_send_alert(ZIGBEE_ALERT_MODE_CHANGED, (uint8_t)mode);
    
    // Если настроено сохранение в NVS, сохраняем состояние
    if (state_ctx.config.save_to_nvs && state_ctx.nvs_opened) {
        state_save_to_nvs();
    }
    
    ESP_LOGI(TAG, "Режим работы установлен: %d", mode);
    
    return ESP_OK;
}

/**
 * @brief Установка положения ручки окна
 */
esp_err_t state_set_handle_position(handle_position_t position)
{
    ESP_LOGI(TAG, "Установка положения ручки окна: %d", position);
    
    if (!state_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления состоянием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Проверяем валидность положения
    if (position != HANDLE_POSITION_CLOSED && 
        position != HANDLE_POSITION_OPEN && 
        position != HANDLE_POSITION_VENTILATE) {
        ESP_LOGE(TAG, "Неверное положение ручки: %d", position);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Если положение не изменилось, выходим
    if (position == state_ctx.state.handle_pos) {
        ESP_LOGW(TAG, "Положение ручки уже установлено: %d", position);
        return ESP_OK;
    }
    
    // Устанавливаем положение ручки
    esp_err_t err = servo_set_handle_position(position);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка установки положения ручки: %s", esp_err_to_name(err));
        return err;
    }
    
    // Обновляем режим в зависимости от положения ручки
    window_mode_t new_mode;
    
    switch (position) {
        case HANDLE_POSITION_CLOSED:
            new_mode = WINDOW_MODE_CLOSED;
            break;
            
        case HANDLE_POSITION_OPEN:
            new_mode = WINDOW_MODE_OPEN;
            break;
            
        case HANDLE_POSITION_VENTILATE:
            new_mode = WINDOW_MODE_VENTILATE;
            break;
            
        default:
            new_mode = WINDOW_MODE_CUSTOM;
            break;
    }
    
    // Обновляем состояние
    state_ctx.state.handle_pos = position;
    state_ctx.state.mode = new_mode;
    state_ctx.state.in_motion = true;
    state_update_last_action_time();
    
    // Отправляем отчет о состоянии через ZigBee
    zigbee_device_report_state(new_mode, state_ctx.state.gap_percentage);
    
    // Если настроено сохранение в NVS, сохраняем состояние
    if (state_ctx.config.save_to_nvs && state_ctx.nvs_opened) {
        state_save_to_nvs();
    }
    
    ESP_LOGI(TAG, "Положение ручки установлено: %d", position);
    
    return ESP_OK;
}

/**
 * @brief Установка процента открытия зазора
 */
esp_err_t state_set_gap_percentage(uint8_t percentage)
{
    ESP_LOGI(TAG, "Установка процента открытия зазора: %d%%", percentage);
    
    if (!state_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления состоянием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Проверяем валидность процента
    if (percentage > 100) {
        ESP_LOGE(TAG, "Неверный процент открытия: %d", percentage);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Если процент не изменился, выходим
    if (percentage == state_ctx.state.gap_percentage) {
        ESP_LOGW(TAG, "Процент открытия уже установлен: %d%%", percentage);
        return ESP_OK;
    }
    
    // Устанавливаем процент открытия зазора
    esp_err_t err = servo_set_gap_percentage(percentage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка установки процента открытия: %s", esp_err_to_name(err));
        return err;
    }
    
    // Если ручка в положении Открыто или Вентиляция, и процент не соответствует режиму,
    // переключаемся в пользовательский режим
    if ((state_ctx.state.handle_pos == HANDLE_POSITION_OPEN && percentage != 100) ||
        (state_ctx.state.handle_pos == HANDLE_POSITION_VENTILATE && percentage != 20) ||
        (state_ctx.state.handle_pos == HANDLE_POSITION_CLOSED && percentage != 0)) {
        state_ctx.state.mode = WINDOW_MODE_CUSTOM;
    }
    
    // Обновляем состояние
    state_ctx.state.gap_percentage = percentage;
    state_ctx.state.in_motion = true;
    state_update_last_action_time();
    
    // Отправляем отчет о состоянии через ZigBee
    zigbee_device_report_state(state_ctx.state.mode, percentage);
    
    // Если настроено сохранение в NVS, сохраняем состояние
    if (state_ctx.config.save_to_nvs && state_ctx.nvs_opened) {
        state_save_to_nvs();
    }
    
    ESP_LOGI(TAG, "Процент открытия установлен: %d%%", percentage);
    
    return ESP_OK;
}

/**
 * @brief Получение текущего состояния окна
 */
window_state_t state_get_window_state(void)
{
    // Обновляем данные из физических устройств
    state_ctx.state.handle_pos = servo_get_handle_position();
    state_ctx.state.gap_percentage = servo_get_gap_percentage();
    
    return state_ctx.state;
}

/**
 * @brief Получение текущего режима работы окна
 */
window_mode_t state_get_window_mode(void)
{
    return state_ctx.state.mode;
}

/**
 * @brief Сохранение текущего состояния
 */
esp_err_t state_save(void)
{
    ESP_LOGI(TAG, "Сохранение текущего состояния");
    
    if (!state_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления состоянием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!state_ctx.config.save_to_nvs) {
        ESP_LOGW(TAG, "Сохранение в NVS отключено в конфигурации");
        return ESP_OK;
    }
    
    if (!state_ctx.nvs_opened) {
        // Пытаемся открыть NVS, если она не открыта
        esp_err_t err = state_open_nvs();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ошибка открытия NVS: %s", esp_err_to_name(err));
            return err;
        }
    }
    
    return state_save_to_nvs();
}

/**
 * @brief Восстановление состояния
 */
esp_err_t state_restore(void)
{
    ESP_LOGI(TAG, "Восстановление состояния");
    
    if (!state_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления состоянием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!state_ctx.config.save_to_nvs) {
        ESP_LOGW(TAG, "Сохранение в NVS отключено в конфигурации");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!state_ctx.nvs_opened) {
        // Пытаемся открыть NVS, если она не открыта
        esp_err_t err = state_open_nvs();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ошибка открытия NVS: %s", esp_err_to_name(err));
            return err;
        }
    }
    
    // Восстанавливаем состояние из NVS
    esp_err_t err = state_restore_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка восстановления из NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Применяем восстановленное состояние
    // Устанавливаем положение ручки
    err = servo_set_handle_position(state_ctx.state.handle_pos);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка установки положения ручки: %s", esp_err_to_name(err));
        return err;
    }
    
    // Устанавливаем процент открытия зазора
    err = servo_set_gap_percentage(state_ctx.state.gap_percentage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка установки процента открытия: %s", esp_err_to_name(err));
        return err;
    }
    
    // Отправляем отчет о состоянии через ZigBee
    zigbee_device_report_state(state_ctx.state.mode, state_ctx.state.gap_percentage);
    
    ESP_LOGI(TAG, "Состояние успешно восстановлено: режим %d, положение %d, проценты %d%%", 
             state_ctx.state.mode, state_ctx.state.handle_pos, state_ctx.state.gap_percentage);
    
    return ESP_OK;
}

/**
 * @brief Сброс состояния к заводским настройкам
 */
esp_err_t state_factory_reset(void)
{
    ESP_LOGI(TAG, "Сброс состояния к заводским настройкам");
    
    if (!state_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления состоянием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Устанавливаем заводские настройки
    state_ctx.state.mode = WINDOW_MODE_CLOSED;
    state_ctx.state.handle_pos = HANDLE_POSITION_CLOSED;
    state_ctx.state.gap_percentage = 0;
    state_ctx.state.calibrated = false;
    state_ctx.state.in_motion = false;
    state_update_last_action_time();
    
    // Применяем заводские настройки
    // Устанавливаем положение ручки
    esp_err_t err = servo_set_handle_position(HANDLE_POSITION_CLOSED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка установки положения ручки: %s", esp_err_to_name(err));
        return err;
    }
    
    // Устанавливаем процент открытия зазора
    err = servo_set_gap_percentage(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка установки процента открытия: %s", esp_err_to_name(err));
        return err;
    }
    
    // Если NVS открыт, очищаем данные
    if (state_ctx.nvs_opened) {
        err = nvs_erase_all(state_ctx.nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ошибка очистки NVS: %s", esp_err_to_name(err));
            return err;
        }
        
        err = nvs_commit(state_ctx.nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ошибка коммита NVS: %s", esp_err_to_name(err));
            return err;
        }
    }
    
    // Отправляем отчет о состоянии через ZigBee
    zigbee_device_report_state(WINDOW_MODE_CLOSED, 0);
    
    ESP_LOGI(TAG, "Состояние успешно сброшено к заводским настройкам");
    
    return ESP_OK;
}

/**
 * @brief Обработчик задачи управления состоянием
 */
esp_err_t state_task_handler(void)
{
    if (!state_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль управления состоянием не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Проверяем необходимость автоматического сохранения
    if (state_ctx.config.save_to_nvs && state_ctx.nvs_opened) {
        uint64_t current_time = esp_timer_get_time() / 1000; // мс
        
        // Если прошел интервал сохранения, сохраняем состояние
        if (current_time - state_ctx.last_save_time >= state_ctx.config.save_interval_ms) {
            ESP_LOGI(TAG, "Автоматическое сохранение состояния");
            state_save_to_nvs();
            state_ctx.last_save_time = current_time;
        }
    }
    
    // Проверяем статус движения
    if (state_ctx.state.in_motion) {
        uint64_t current_time = esp_timer_get_time() / 1000; // мс
        
        // Если прошло больше 5 секунд с последнего действия, считаем что движение завершено
        if (current_time - state_ctx.state.last_action_time >= 5000) {
            state_ctx.state.in_motion = false;
            ESP_LOGI(TAG, "Движение завершено");
        }
    }
    
    return ESP_OK;
}

/**
 * @brief Внутренняя функция сохранения состояния в NVS
 */
static esp_err_t state_save_to_nvs(void)
{
    if (!state_ctx.nvs_opened) {
        ESP_LOGE(TAG, "NVS не открыта");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Сохраняем режим работы
    esp_err_t err = nvs_set_u8(state_ctx.nvs_handle, NVS_KEY_MODE, (uint8_t)state_ctx.state.mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка сохранения режима: %s", esp_err_to_name(err));
        return err;
    }
    
    // Сохраняем положение ручки
    err = nvs_set_u8(state_ctx.nvs_handle, NVS_KEY_HANDLE_POS, (uint8_t)state_ctx.state.handle_pos);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка сохранения положения ручки: %s", esp_err_to_name(err));
        return err;
    }
    
    // Сохраняем процент открытия зазора
    err = nvs_set_u8(state_ctx.nvs_handle, NVS_KEY_GAP_PERCENTAGE, state_ctx.state.gap_percentage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка сохранения процента открытия: %s", esp_err_to_name(err));
        return err;
    }
    
    // Сохраняем статус калибровки
    err = nvs_set_u8(state_ctx.nvs_handle, NVS_KEY_CALIBRATED, state_ctx.state.calibrated ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка сохранения статуса калибровки: %s", esp_err_to_name(err));
        return err;
    }
    
    // Сохраняем изменения
    err = nvs_commit(state_ctx.nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка коммита NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Состояние успешно сохранено в NVS");
    
    return ESP_OK;
}

/**
 * @brief Внутренняя функция восстановления состояния из NVS
 */
static esp_err_t state_restore_from_nvs(void)
{
    if (!state_ctx.nvs_opened) {
        ESP_LOGE(TAG, "NVS не открыта");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Временные переменные для восстановления
    uint8_t mode_val, handle_pos_val, gap_percentage_val, calibrated_val;
    
    // Восстанавливаем режим работы
    esp_err_t err = nvs_get_u8(state_ctx.nvs_handle, NVS_KEY_MODE, &mode_val);
    if (err == ESP_OK) {
        state_ctx.state.mode = (window_mode_t)mode_val;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Ошибка восстановления режима: %s", esp_err_to_name(err));
        return err;
    }
    
    // Восстанавливаем положение ручки
    err = nvs_get_u8(state_ctx.nvs_handle, NVS_KEY_HANDLE_POS, &handle_pos_val);
    if (err == ESP_OK) {
        state_ctx.state.handle_pos = (handle_position_t)handle_pos_val;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Ошибка восстановления положения ручки: %s", esp_err_to_name(err));
        return err;
    }
    
    // Восстанавливаем процент открытия зазора
    err = nvs_get_u8(state_ctx.nvs_handle, NVS_KEY_GAP_PERCENTAGE, &gap_percentage_val);
    if (err == ESP_OK) {
        state_ctx.state.gap_percentage = gap_percentage_val;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Ошибка восстановления процента открытия: %s", esp_err_to_name(err));
        return err;
    }
    
    // Восстанавливаем статус калибровки
    err = nvs_get_u8(state_ctx.nvs_handle, NVS_KEY_CALIBRATED, &calibrated_val);
    if (err == ESP_OK) {
        state_ctx.state.calibrated = (calibrated_val == 1);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Ошибка восстановления статуса калибровки: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Состояние успешно восстановлено из NVS");
    
    return ESP_OK;
}

/**
 * @brief Внутренняя функция открытия NVS
 */
static esp_err_t state_open_nvs(void)
{
    // Если NVS уже открыта, закрываем ее
    if (state_ctx.nvs_opened) {
        nvs_close(state_ctx.nvs_handle);
        state_ctx.nvs_opened = false;
    }
    
    // Открываем NVS
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &state_ctx.nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка открытия NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    state_ctx.nvs_opened = true;
    ESP_LOGI(TAG, "NVS успешно открыта");
    
    return ESP_OK;
}

/**
 * @brief Внутренняя функция закрытия NVS
 */
static void state_close_nvs(void)
{
    if (state_ctx.nvs_opened) {
        nvs_close(state_ctx.nvs_handle);
        state_ctx.nvs_opened = false;
        ESP_LOGI(TAG, "NVS закрыта");
    }
}

/**
 * @brief Внутренняя функция обновления времени последнего действия
 */
static void state_update_last_action_time(void)
{
    state_ctx.state.last_action_time = esp_timer_get_time() / 1000; // мс
} 