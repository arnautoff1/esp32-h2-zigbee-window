/**
 * @file ota_update.c
 * @brief Реализация модуля OTA-обновлений для умного окна
 */

#include "ota_update.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_ota_ops.h"

// Определение тега для логов
static const char* TAG = "OTA_UPDATE";

// Размер буфера для хранения версии
#define OTA_VERSION_BUFFER_SIZE 32

// Определение битов событий
#define OTA_EVENT_CHECK_UPDATE     (1 << 0)
#define OTA_EVENT_START_DOWNLOAD   (1 << 1)
#define OTA_EVENT_APPLY_UPDATE     (1 << 2)

// Структура данных состояния OTA
typedef struct {
    ota_config_t config;               // Конфигурация OTA
    ota_state_t state;                 // Текущее состояние OTA
    uint8_t download_progress;         // Прогресс загрузки (0-100%)
    char current_version[OTA_VERSION_BUFFER_SIZE];  // Текущая версия прошивки
    char new_version[OTA_VERSION_BUFFER_SIZE];      // Новая версия прошивки
    TaskHandle_t task_handle;          // Хендл задачи OTA
    EventGroupHandle_t event_group;    // Группа событий OTA
} ota_state_data_t;

// Текущее состояние OTA
static ota_state_data_t ota_data = {
    .state = OTA_STATE_IDLE,
    .download_progress = 0,
    .task_handle = NULL,
};

// Прототипы вспомогательных функций
static esp_err_t http_event_handler(esp_http_client_event_t *evt);
static esp_err_t validate_image_header(esp_app_desc_t *new_app_info);
static esp_err_t ota_check_for_update(void);
static esp_err_t ota_download_and_apply_update(void);

/**
 * @brief Инициализация модуля OTA
 */
esp_err_t ota_init(ota_config_t *config)
{
    ESP_LOGI(TAG, "Инициализация модуля OTA");
    
    // Проверка аргументов
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Копирование конфигурации
    memcpy(&ota_data.config, config, sizeof(ota_config_t));
    
    // Получение текущей версии прошивки
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc != NULL) {
        strncpy(ota_data.current_version, app_desc->version, OTA_VERSION_BUFFER_SIZE - 1);
        ota_data.current_version[OTA_VERSION_BUFFER_SIZE - 1] = '\0';
        
        // Обновление версии в конфигурации
        strncpy(ota_data.config.firmware_version, ota_data.current_version, sizeof(ota_data.config.firmware_version) - 1);
        ota_data.config.firmware_version[sizeof(ota_data.config.firmware_version) - 1] = '\0';
    } else {
        strncpy(ota_data.current_version, config->firmware_version, OTA_VERSION_BUFFER_SIZE - 1);
        ota_data.current_version[OTA_VERSION_BUFFER_SIZE - 1] = '\0';
    }
    
    // Создание группы событий
    ota_data.event_group = xEventGroupCreate();
    if (ota_data.event_group == NULL) {
        ESP_LOGE(TAG, "Ошибка создания группы событий");
        return ESP_ERR_NO_MEM;
    }
    
    // Установка начального состояния
    ota_data.state = OTA_STATE_IDLE;
    ota_data.download_progress = 0;
    
    ESP_LOGI(TAG, "Модуль OTA инициализирован. Текущая версия: %s", ota_data.current_version);
    
    return ESP_OK;
}

/**
 * @brief Запуск модуля OTA
 */
esp_err_t ota_start(void)
{
    ESP_LOGI(TAG, "Запуск модуля OTA");
    
    // Создание задачи OTA
    BaseType_t task_created = xTaskCreate(
        ota_task_handler,
        "ota_task",
        8192,   // Размер стека
        NULL,   // Параметры
        5,      // Приоритет
        &ota_data.task_handle
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Ошибка создания задачи OTA");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Модуль OTA запущен");
    
    return ESP_OK;
}

/**
 * @brief Остановка модуля OTA
 */
esp_err_t ota_stop(void)
{
    ESP_LOGI(TAG, "Остановка модуля OTA");
    
    // Проверка существования задачи
    if (ota_data.task_handle != NULL) {
        // Удаление задачи
        vTaskDelete(ota_data.task_handle);
        ota_data.task_handle = NULL;
    }
    
    // Сброс состояния
    ota_data.state = OTA_STATE_IDLE;
    ota_data.download_progress = 0;
    
    ESP_LOGI(TAG, "Модуль OTA остановлен");
    
    return ESP_OK;
}

/**
 * @brief Запрос на проверку обновлений
 */
esp_err_t ota_check_update(void)
{
    ESP_LOGI(TAG, "Запрос на проверку обновлений");
    
    // Проверка состояния
    if (ota_data.state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA уже активен, состояние: %d", ota_data.state);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Установка события проверки обновлений
    xEventGroupSetBits(ota_data.event_group, OTA_EVENT_CHECK_UPDATE);
    
    return ESP_OK;
}

/**
 * @brief Загрузка обновления
 */
esp_err_t ota_download_update(void)
{
    ESP_LOGI(TAG, "Запрос на загрузку обновления");
    
    // Проверка состояния
    if (ota_data.state != OTA_STATE_IDLE && ota_data.state != OTA_STATE_CHECKING) {
        ESP_LOGW(TAG, "OTA уже активен, состояние: %d", ota_data.state);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Установка события загрузки обновления
    xEventGroupSetBits(ota_data.event_group, OTA_EVENT_START_DOWNLOAD);
    
    return ESP_OK;
}

/**
 * @brief Применение обновления
 */
esp_err_t ota_apply_update(void)
{
    ESP_LOGI(TAG, "Запрос на применение обновления");
    
    // Проверка состояния
    if (ota_data.state != OTA_STATE_READY_TO_APPLY) {
        ESP_LOGW(TAG, "OTA не готов к применению, состояние: %d", ota_data.state);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Установка события применения обновления
    xEventGroupSetBits(ota_data.event_group, OTA_EVENT_APPLY_UPDATE);
    
    return ESP_OK;
}

/**
 * @brief Получение текущего состояния OTA
 */
ota_state_t ota_get_state(void)
{
    return ota_data.state;
}

/**
 * @brief Получение процентного значения прогресса загрузки
 */
uint8_t ota_get_download_progress(void)
{
    return ota_data.download_progress;
}

/**
 * @brief Получение текущей версии прошивки
 */
esp_err_t ota_get_firmware_version(char *version, size_t version_len)
{
    if (version == NULL || version_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(version, ota_data.current_version, version_len - 1);
    version[version_len - 1] = '\0';
    
    return ESP_OK;
}

/**
 * @brief Обработчик событий HTTP-клиента
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP клиент подключен");
            break;
        case HTTP_EVENT_ON_DATA:
            {
                // Подсчет прогресса загрузки
                esp_http_client_fetch_headers(evt->client);
                int content_length = esp_http_client_get_content_length(evt->client);
                if (content_length > 0) {
                    static int total_received = 0;
                    total_received += evt->data_len;
                    ota_data.download_progress = (uint8_t)((total_received * 100) / content_length);
                }
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP клиент отключен");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Проверка заголовка образа
 */
static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Проверка заголовка образа: '%s'", new_app_info->version);
    
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Текущая версия: %s, новая версия: %s", 
                 running_app_info.version, new_app_info->version);
        
        // Здесь можно реализовать проверку версии
        // Для демонстрации считаем, что новая версия всегда принимается
    }
    
    return ESP_OK;
}

/**
 * @brief Проверка наличия обновления
 */
static esp_err_t ota_check_for_update(void)
{
    ESP_LOGI(TAG, "Проверка наличия обновления...");
    
    // Обновление состояния
    ota_data.state = OTA_STATE_CHECKING;
    
    // Настройка HTTP-клиента
    esp_http_client_config_t config = {
        .url = ota_data.config.server_url,
        .event_handler = http_event_handler,
        .keep_alive_enable = true,
        .skip_cert_common_name_check = true,
    };
    
    // Инициализация HTTP-клиента
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Ошибка инициализации HTTP-клиента");
        ota_data.state = OTA_STATE_ERROR;
        return ESP_FAIL;
    }
    
    // Выполнение HEAD-запроса для проверки доступности обновления
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка HTTP-запроса: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        ota_data.state = OTA_STATE_ERROR;
        return err;
    }
    
    // Проверка статуса ответа
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Статус HTTP-ответа: %d", status_code);
    
    // Проверка версии прошивки
    // Если доступна новая версия, тогда обновляем состояние
    if (status_code == 200) {
        ESP_LOGI(TAG, "Доступно обновление");
        
        // В реальном устройстве здесь будет проверка версии из заголовков HTTP
        // и сравнение с текущей версией
        
        // Для демонстрации считаем, что всегда доступно обновление
        ota_data.state = OTA_STATE_IDLE;  // Готовы к загрузке
    } else {
        ESP_LOGI(TAG, "Обновлений не найдено");
        ota_data.state = OTA_STATE_IDLE;
    }
    
    // Освобождение ресурсов HTTP-клиента
    esp_http_client_cleanup(client);
    
    return ESP_OK;
}

/**
 * @brief Загрузка и применение обновления
 */
static esp_err_t ota_download_and_apply_update(void)
{
    ESP_LOGI(TAG, "Загрузка и применение обновления...");
    
    // Обновление состояния
    ota_data.state = OTA_STATE_DOWNLOADING;
    ota_data.download_progress = 0;
    
    // Настройка HTTP-клиента для OTA
    esp_http_client_config_t config = {
        .url = ota_data.config.server_url,
        .event_handler = http_event_handler,
        .keep_alive_enable = true,
        .skip_cert_common_name_check = true,
    };
    
    // Настройка параметров OTA
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    
    // Инициализация OTA
    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации OTA: %s", esp_err_to_name(err));
        ota_data.state = OTA_STATE_ERROR;
        return err;
    }
    
    // Получение информации о новом образе
    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка получения описания образа: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        ota_data.state = OTA_STATE_ERROR;
        return err;
    }
    
    // Проверка заголовка образа
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка валидации образа: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        ota_data.state = OTA_STATE_ERROR;
        return err;
    }
    
    // Загрузка образа
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        
        // Обновление прогресса
        ota_data.download_progress = (uint8_t)(esp_https_ota_get_image_len_read(https_ota_handle) * 100 / 
                                                 esp_https_ota_get_image_size(https_ota_handle));
        
        // Короткая задержка
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Проверка результата загрузки
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка загрузки образа: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        ota_data.state = OTA_STATE_ERROR;
        return err;
    }
    
    // Завершение OTA и проверка результата
    err = esp_https_ota_finish(https_ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Ошибка валидации образа после загрузки");
        }
        ESP_LOGE(TAG, "Ошибка завершения OTA: %s", esp_err_to_name(err));
        ota_data.state = OTA_STATE_ERROR;
        return err;
    }
    
    // Обновление состояния
    ota_data.state = OTA_STATE_READY_TO_APPLY;
    ota_data.download_progress = 100;
    
    // Сохранение новой версии
    strncpy(ota_data.new_version, app_desc.version, OTA_VERSION_BUFFER_SIZE - 1);
    ota_data.new_version[OTA_VERSION_BUFFER_SIZE - 1] = '\0';
    
    ESP_LOGI(TAG, "Обновление успешно загружено. Новая версия: %s", ota_data.new_version);
    
    // Если включено автоматическое обновление
    if (ota_data.config.auto_update) {
        ESP_LOGI(TAG, "Автоматическое применение обновления...");
        ota_data.state = OTA_STATE_APPLYING;
        
        // Небольшая задержка перед перезагрузкой
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        ESP_LOGI(TAG, "Перезагрузка для применения обновления");
        esp_restart();
    }
    
    return ESP_OK;
}

/**
 * @brief Обработчик OTA-задачи
 */
void ota_task_handler(void *pvParameter)
{
    ESP_LOGI(TAG, "Запуск задачи OTA");
    
    // Бесконечный цикл обработки
    while (1) {
        // Проверка событий OTA
        EventBits_t bits = xEventGroupWaitBits(
            ota_data.event_group,
            OTA_EVENT_CHECK_UPDATE | OTA_EVENT_START_DOWNLOAD | OTA_EVENT_APPLY_UPDATE,
            pdTRUE,  // Сброс битов после чтения
            pdFALSE, // Любое событие
            pdMS_TO_TICKS(1000)  // Таймаут 1 секунда
        );
        
        // Обработка события проверки обновлений
        if (bits & OTA_EVENT_CHECK_UPDATE) {
            ESP_LOGI(TAG, "Обработка события проверки обновлений");
            ota_check_for_update();
        }
        
        // Обработка события загрузки обновления
        if (bits & OTA_EVENT_START_DOWNLOAD) {
            ESP_LOGI(TAG, "Обработка события загрузки обновления");
            ota_download_and_apply_update();
        }
        
        // Обработка события применения обновления
        if (bits & OTA_EVENT_APPLY_UPDATE) {
            ESP_LOGI(TAG, "Обработка события применения обновления");
            
            // Установка состояния "применение обновления"
            ota_data.state = OTA_STATE_APPLYING;
            
            // Небольшая задержка перед перезагрузкой
            vTaskDelay(pdMS_TO_TICKS(3000));
            
            ESP_LOGI(TAG, "Перезагрузка для применения обновления");
            esp_restart();
        }
        
        // Проверка необходимости автоматической проверки обновлений
        if (ota_data.config.auto_check && ota_data.state == OTA_STATE_IDLE) {
            static uint32_t last_check_time = 0;
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            
            if (current_time - last_check_time >= ota_data.config.check_interval_ms) {
                ESP_LOGI(TAG, "Автоматическая проверка обновлений");
                last_check_time = current_time;
                ota_check_for_update();
            }
        }
        
        // Короткая задержка
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // На случай выхода из цикла
    vTaskDelete(NULL);
} 