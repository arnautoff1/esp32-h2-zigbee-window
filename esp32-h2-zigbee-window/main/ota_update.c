/**
 * @file ota_update.c
 * @brief Реализация модуля OTA-обновлений для умного окна
 */

#include "ota_update.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>

#define TAG "OTA"

// Задача OTA-обновлений
static TaskHandle_t ota_task_handle = NULL;

// Текущее состояние OTA-обновлений
static struct {
    bool initialized;              // Статус инициализации
    ota_config_t config;           // Конфигурация
    ota_state_t state;             // Текущее состояние
    uint64_t last_check_time;      // Время последней проверки
    uint8_t download_progress;     // Процент загрузки
    bool new_version_available;    // Доступность новой версии
    char new_version[16];          // Номер новой версии
    bool update_task_running;      // Статус задачи обновления
} ota_ctx = {
    .initialized = false,
    .state = OTA_STATE_IDLE,
    .last_check_time = 0,
    .download_progress = 0,
    .new_version_available = false,
    .new_version = {0},
    .update_task_running = false
};

// Прототипы вспомогательных функций
static esp_err_t ota_check_for_update_internal(void);
static esp_err_t ota_download_update_internal(void);
static esp_err_t ota_apply_update_internal(void);
static void ota_reset_state(void);

/**
 * @brief Инициализация модуля OTA
 */
esp_err_t ota_init(ota_config_t *config)
{
    ESP_LOGI(TAG, "Инициализация модуля OTA-обновлений");
    
    if (ota_ctx.initialized) {
        ESP_LOGW(TAG, "Модуль OTA-обновлений уже инициализирован");
        return ESP_OK;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Неверный параметр: config == NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Копируем конфигурацию
    memcpy(&ota_ctx.config, config, sizeof(ota_config_t));
    
    // Инициализируем контекст
    ota_ctx.state = OTA_STATE_IDLE;
    ota_ctx.last_check_time = 0;
    ota_ctx.download_progress = 0;
    ota_ctx.new_version_available = false;
    memset(ota_ctx.new_version, 0, sizeof(ota_ctx.new_version));
    ota_ctx.update_task_running = false;
    
    ota_ctx.initialized = true;
    ESP_LOGI(TAG, "Модуль OTA-обновлений успешно инициализирован");
    ESP_LOGI(TAG, "Текущая версия прошивки: %s", ota_ctx.config.firmware_version);
    
    return ESP_OK;
}

/**
 * @brief Запуск модуля OTA
 */
esp_err_t ota_start(void)
{
    ESP_LOGI(TAG, "Запуск модуля OTA-обновлений");
    
    if (!ota_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль OTA-обновлений не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (ota_ctx.update_task_running) {
        ESP_LOGW(TAG, "Задача OTA-обновлений уже запущена");
        return ESP_OK;
    }
    
    // Создаем задачу OTA-обновлений
    BaseType_t task_created = xTaskCreate(
        ota_task_handler,
        "ota_task",
        4096,
        NULL,
        3,
        &ota_task_handle
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Не удалось создать задачу OTA-обновлений");
        return ESP_ERR_NO_MEM;
    }
    
    ota_ctx.update_task_running = true;
    ESP_LOGI(TAG, "Модуль OTA-обновлений успешно запущен");
    
    return ESP_OK;
}

/**
 * @brief Остановка модуля OTA
 */
esp_err_t ota_stop(void)
{
    ESP_LOGI(TAG, "Остановка модуля OTA-обновлений");
    
    if (!ota_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль OTA-обновлений не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!ota_ctx.update_task_running) {
        ESP_LOGW(TAG, "Задача OTA-обновлений не запущена");
        return ESP_OK;
    }
    
    // Если идет загрузка или применение обновления, не останавливаем задачу
    if (ota_ctx.state == OTA_STATE_DOWNLOADING || ota_ctx.state == OTA_STATE_APPLYING) {
        ESP_LOGW(TAG, "Невозможно остановить: идет процесс обновления");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Удаляем задачу OTA-обновлений
    if (ota_task_handle != NULL) {
        vTaskDelete(ota_task_handle);
        ota_task_handle = NULL;
    }
    
    ota_ctx.update_task_running = false;
    ota_ctx.state = OTA_STATE_IDLE;
    ESP_LOGI(TAG, "Модуль OTA-обновлений успешно остановлен");
    
    return ESP_OK;
}

/**
 * @brief Запрос на проверку обновлений
 */
esp_err_t ota_check_update(void)
{
    ESP_LOGI(TAG, "Запрос на проверку обновлений");
    
    if (!ota_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль OTA-обновлений не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Если уже идет проверка или загрузка, выходим
    if (ota_ctx.state == OTA_STATE_CHECKING || 
        ota_ctx.state == OTA_STATE_DOWNLOADING || 
        ota_ctx.state == OTA_STATE_APPLYING) {
        ESP_LOGW(TAG, "Невозможно проверить: уже идет процесс обновления");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Запускаем проверку обновлений
    ota_ctx.state = OTA_STATE_CHECKING;
    
    // В реальной реализации здесь был бы асинхронный запрос на сервер
    // В заглушке вызываем синхронную функцию
    esp_err_t err = ota_check_for_update_internal();
    if (err != ESP_OK) {
        ota_ctx.state = OTA_STATE_ERROR;
        ESP_LOGE(TAG, "Ошибка проверки обновлений: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

/**
 * @brief Загрузка обновления
 */
esp_err_t ota_download_update(void)
{
    ESP_LOGI(TAG, "Запрос на загрузку обновления");
    
    if (!ota_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль OTA-обновлений не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Если нет доступных обновлений, выходим
    if (!ota_ctx.new_version_available) {
        ESP_LOGW(TAG, "Нет доступных обновлений для загрузки");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Если уже идет загрузка или применение, выходим
    if (ota_ctx.state == OTA_STATE_DOWNLOADING || 
        ota_ctx.state == OTA_STATE_APPLYING) {
        ESP_LOGW(TAG, "Невозможно загрузить: уже идет процесс обновления");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Запускаем загрузку обновления
    ota_ctx.state = OTA_STATE_DOWNLOADING;
    ota_ctx.download_progress = 0;
    
    // В реальной реализации здесь был бы асинхронный запрос на сервер
    // В заглушке вызываем синхронную функцию
    esp_err_t err = ota_download_update_internal();
    if (err != ESP_OK) {
        ota_ctx.state = OTA_STATE_ERROR;
        ESP_LOGE(TAG, "Ошибка загрузки обновления: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

/**
 * @brief Применение обновления
 */
esp_err_t ota_apply_update(void)
{
    ESP_LOGI(TAG, "Запрос на применение обновления");
    
    if (!ota_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль OTA-обновлений не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Если нет готового к применению обновления, выходим
    if (ota_ctx.state != OTA_STATE_READY_TO_APPLY) {
        ESP_LOGW(TAG, "Нет готового к применению обновления");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Запускаем применение обновления
    ota_ctx.state = OTA_STATE_APPLYING;
    
    // В реальной реализации здесь был бы процесс применения обновления
    // В заглушке вызываем синхронную функцию
    esp_err_t err = ota_apply_update_internal();
    if (err != ESP_OK) {
        ota_ctx.state = OTA_STATE_ERROR;
        ESP_LOGE(TAG, "Ошибка применения обновления: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

/**
 * @brief Получение текущего состояния OTA
 */
ota_state_t ota_get_state(void)
{
    if (!ota_ctx.initialized) {
        return OTA_STATE_IDLE;
    }
    
    return ota_ctx.state;
}

/**
 * @brief Получение процентного значения прогресса загрузки
 */
uint8_t ota_get_download_progress(void)
{
    if (!ota_ctx.initialized || ota_ctx.state != OTA_STATE_DOWNLOADING) {
        return 0;
    }
    
    return ota_ctx.download_progress;
}

/**
 * @brief Получение текущей версии прошивки
 */
esp_err_t ota_get_firmware_version(char *version, size_t version_len)
{
    if (!ota_ctx.initialized) {
        ESP_LOGE(TAG, "Модуль OTA-обновлений не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (version == NULL || version_len == 0) {
        ESP_LOGE(TAG, "Неверные параметры: version == NULL или version_len == 0");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Копируем версию прошивки
    strncpy(version, ota_ctx.config.firmware_version, version_len - 1);
    version[version_len - 1] = '\0';
    
    return ESP_OK;
}

/**
 * @brief Обработчик OTA-задачи
 */
void ota_task_handler(void *pvParameter)
{
    ESP_LOGI(TAG, "Запуск задачи OTA-обновлений");
    
    while (1) {
        // Проверяем необходимость автоматической проверки обновлений
        if (ota_ctx.config.auto_check && ota_ctx.state == OTA_STATE_IDLE) {
            uint64_t current_time = esp_timer_get_time() / 1000; // мс
            
            // Если прошел интервал проверки, запускаем проверку
            if (ota_ctx.last_check_time == 0 || 
                (current_time - ota_ctx.last_check_time >= ota_ctx.config.check_interval_ms)) {
                
                ESP_LOGI(TAG, "Запуск автоматической проверки обновлений");
                ota_check_update();
            }
        }
        
        // Обработка состояний OTA
        switch (ota_ctx.state) {
            case OTA_STATE_CHECKING:
                // В заглушке проверка выполняется синхронно
                break;
                
            case OTA_STATE_DOWNLOADING:
                // В заглушке загрузка выполняется синхронно
                break;
                
            case OTA_STATE_READY_TO_APPLY:
                // Если включено автоматическое обновление, применяем обновление
                if (ota_ctx.config.auto_update) {
                    ESP_LOGI(TAG, "Запуск автоматического применения обновления");
                    ota_apply_update();
                }
                break;
                
            case OTA_STATE_APPLYING:
                // В заглушке применение выполняется синхронно
                break;
                
            case OTA_STATE_ERROR:
                // Сбрасываем состояние через некоторое время
                vTaskDelay(pdMS_TO_TICKS(5000)); // 5 секунд
                ota_reset_state();
                break;
                
            default:
                break;
        }
        
        // Задержка
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 секунда
    }
}

/**
 * @brief Внутренняя функция проверки обновлений
 */
static esp_err_t ota_check_for_update_internal(void)
{
    ESP_LOGI(TAG, "Проверка наличия обновлений на сервере: %s", ota_ctx.config.server_url);
    
    // В заглушке симулируем проверку обновлений
    vTaskDelay(pdMS_TO_TICKS(2000)); // 2 секунды
    
    // Случайным образом определяем, доступно ли обновление
    bool update_available = (rand() % 5 == 0); // 20% вероятность наличия обновления
    
    if (update_available) {
        // Генерируем новую версию (увеличиваем текущую)
        char current_version[16];
        strncpy(current_version, ota_ctx.config.firmware_version, sizeof(current_version) - 1);
        current_version[sizeof(current_version) - 1] = '\0';
        
        // Парсим текущую версию
        int major, minor, patch;
        sscanf(current_version, "%d.%d.%d", &major, &minor, &patch);
        
        // Увеличиваем номер патча
        patch++;
        
        // Формируем новую версию
        snprintf(ota_ctx.new_version, sizeof(ota_ctx.new_version), "%d.%d.%d", major, minor, patch);
        ota_ctx.new_version_available = true;
        
        ESP_LOGI(TAG, "Найдено обновление: %s -> %s", ota_ctx.config.firmware_version, ota_ctx.new_version);
    } else {
        ESP_LOGI(TAG, "Обновлений не найдено, текущая версия: %s", ota_ctx.config.firmware_version);
        ota_ctx.new_version_available = false;
        memset(ota_ctx.new_version, 0, sizeof(ota_ctx.new_version));
    }
    
    // Запоминаем время проверки
    ota_ctx.last_check_time = esp_timer_get_time() / 1000; // мс
    
    // Возвращаем состояние в IDLE если обновление не найдено,
    // или READY_TO_DOWNLOAD если обновление найдено
    ota_ctx.state = ota_ctx.new_version_available ? OTA_STATE_DOWNLOADING : OTA_STATE_IDLE;
    
    return ESP_OK;
}

/**
 * @brief Внутренняя функция загрузки обновления
 */
static esp_err_t ota_download_update_internal(void)
{
    ESP_LOGI(TAG, "Загрузка обновления с сервера: %s", ota_ctx.config.server_url);
    
    // В заглушке симулируем процесс загрузки
    for (int i = 0; i <= 10; i++) {
        ota_ctx.download_progress = i * 10;
        ESP_LOGI(TAG, "Прогресс загрузки: %d%%", ota_ctx.download_progress);
        vTaskDelay(pdMS_TO_TICKS(500)); // 0.5 секунды
    }
    
    // Завершаем загрузку
    ota_ctx.download_progress = 100;
    ESP_LOGI(TAG, "Загрузка обновления завершена");
    
    // Переходим в состояние готовности к применению
    ota_ctx.state = OTA_STATE_READY_TO_APPLY;
    
    return ESP_OK;
}

/**
 * @brief Внутренняя функция применения обновления
 */
static esp_err_t ota_apply_update_internal(void)
{
    ESP_LOGI(TAG, "Применение обновления: %s", ota_ctx.new_version);
    
    // В заглушке симулируем процесс применения обновления
    vTaskDelay(pdMS_TO_TICKS(3000)); // 3 секунды
    
    ESP_LOGI(TAG, "Обновление успешно применено");
    ESP_LOGI(TAG, "Необходима перезагрузка устройства");
    
    // В заглушке просто обновляем текущую версию, в реальности здесь был бы перезапуск устройства
    strncpy(ota_ctx.config.firmware_version, ota_ctx.new_version, sizeof(ota_ctx.config.firmware_version) - 1);
    ota_ctx.config.firmware_version[sizeof(ota_ctx.config.firmware_version) - 1] = '\0';
    
    // Сбрасываем состояние
    ota_reset_state();
    
    return ESP_OK;
}

/**
 * @brief Сброс состояния OTA
 */
static void ota_reset_state(void)
{
    ota_ctx.state = OTA_STATE_IDLE;
    ota_ctx.download_progress = 0;
    ota_ctx.new_version_available = false;
    memset(ota_ctx.new_version, 0, sizeof(ota_ctx.new_version));
} 