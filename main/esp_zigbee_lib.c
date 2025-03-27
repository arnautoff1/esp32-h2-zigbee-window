/**
 * @file esp_zigbee_lib.c
 * @brief Реализация библиотеки ZigBee для ESP32-H2
 * @version 1.0
 * @date 2023-03-26
 */

#include "esp_zigbee_lib.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>

// Включение официальных заголовочных файлов ESP-ZB стека
#include "esp_zb_device.h"
#include "esp_zb_zcl.h"
#include "esp_zb_zcl_window_covering.h"

static const char *TAG = "ESP_ZIGBEE_LIB";

// Контекст библиотеки
static struct {
    bool initialized;
    bool started;
    bool pairing_enabled;
    esp_zigbee_config_t config;
    esp_zigbee_device_type_t device_type;
    esp_zb_ep_handle_t window_ep;
    uint8_t endpoint_id;
} zigbee_ctx = {
    .initialized = false,
    .started = false,
    .pairing_enabled = false,
    .device_type = ESP_ZIGBEE_DEVICE_TYPE_END_DEVICE,
    .window_ep = NULL,
    .endpoint_id = 1
};

// Идентификаторы кластера и атрибутов
#define WINDOW_COVERING_CLUSTER_ID        0x0102
#define WINDOW_COVERING_TYPE_ATTRIBUTE_ID 0x0000
#define WINDOW_COVERING_MODE_ATTRIBUTE_ID 0x0008
#define WINDOW_COVERING_POS_ATTRIBUTE_ID  0x0008

// Идентификаторы команд кластера
#define WINDOW_COVERING_UP_CMD_ID         0x00
#define WINDOW_COVERING_DOWN_CMD_ID       0x01
#define WINDOW_COVERING_STOP_CMD_ID       0x02
#define WINDOW_COVERING_GO_TO_POS_CMD_ID  0x05

// Преобразовать команду ZigBee в нашу команду
static uint8_t convert_zb_cmd_to_esp_cmd(uint8_t zb_cmd)
{
    switch (zb_cmd) {
        case WINDOW_COVERING_UP_CMD_ID:
            return ESP_ZIGBEE_CMD_SET_MODE;
        case WINDOW_COVERING_DOWN_CMD_ID:
            return ESP_ZIGBEE_CMD_SET_MODE;
        case WINDOW_COVERING_STOP_CMD_ID:
            return ESP_ZIGBEE_CMD_STOP;
        case WINDOW_COVERING_GO_TO_POS_CMD_ID:
            return ESP_ZIGBEE_CMD_SET_POSITION;
        default:
            return 0xFF; // Неизвестная команда
    }
}

// Колбэк для команд кластера
static esp_err_t window_covering_cluster_handler(esp_zb_zcl_cmd_t *cmd_info)
{
    ESP_LOGI(TAG, "Получена команда ZigBee: ID=%d", cmd_info->cmd_id);
    
    uint8_t esp_cmd = convert_zb_cmd_to_esp_cmd(cmd_info->cmd_id);
    if (esp_cmd == 0xFF) {
        ESP_LOGW(TAG, "Неизвестная команда: %d", cmd_info->cmd_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Проверка на наличие колбэка для команд
    if (zigbee_ctx.config.on_command) {
        const uint8_t *data = cmd_info->payload;
        uint16_t len = cmd_info->payload_size;
        
        // Вызов колбэка
        zigbee_ctx.config.on_command(esp_cmd, data, len);
    }
    
    return ESP_OK;
}

// Колбэк для подключения к сети
static void zigbee_network_state_changed_cb(esp_zb_nwk_state_t state)
{
    ESP_LOGI(TAG, "Изменение состояния ZigBee сети: %d", state);
    
    switch (state) {
        case ESP_ZB_NWK_STATE_CONNECTED:
            ESP_LOGI(TAG, "Устройство подключено к сети ZigBee");
            if (zigbee_ctx.config.on_connected) {
                zigbee_ctx.config.on_connected();
            }
            break;
        case ESP_ZB_NWK_STATE_DISCONNECTED:
            ESP_LOGW(TAG, "Устройство отключено от сети ZigBee");
            if (zigbee_ctx.config.on_disconnected) {
                zigbee_ctx.config.on_disconnected();
            }
            break;
        default:
            break;
    }
}

/**
 * @brief Инициализация ZigBee устройства
 */
esp_err_t esp_zigbee_init(const esp_zigbee_config_t *config)
{
    ESP_LOGI(TAG, "Инициализация ZigBee библиотеки");
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Ошибка: NULL конфигурация");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (zigbee_ctx.initialized) {
        ESP_LOGW(TAG, "ZigBee библиотека уже инициализирована");
        return ESP_OK;
    }
    
    // Сохранение конфигурации
    memcpy(&zigbee_ctx.config, config, sizeof(esp_zigbee_config_t));
    
    // Инициализация стека ZigBee
    esp_zb_platform_config_t platform_config = {
        .radio_config = {
            .radio_mode = RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = HOST_CONNECTION_MODE_NONE,
        }
    };
    
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_config));
    
    // Создание устройства ZigBee
    esp_zb_cfg_t zb_config = {
        .device_type = ESP_ZB_DEVICE_TYPE_END_DEVICE,
        .nwk_cfg = {
            .zed_cfg = {
                .ed_timeout = 5,
                .keep_alive = 1,
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_zb_init(&zb_config));
    
    // Установка колбэка изменения состояния сети
    ESP_ERROR_CHECK(esp_zb_set_network_state_change_cb(zigbee_network_state_changed_cb));
    
    // Создание эндпоинта для устройства окна
    esp_zb_window_covering_cfg_t window_covering_cfg = {
        .type = 0x01,                    // Тип устройства (жалюзи/окно)
        .mode = 0x02,                    // Режим работы (двунаправленный)
        .supported_features = 0x03,      // Поддерживаемые функции
        .current_position = 0,           // Текущее положение 0%
        .target_position = 0             // Целевое положение 0%
    };
    
    // Создание кластера окна
    zigbee_ctx.window_ep = esp_zb_window_covering_ep_create(zigbee_ctx.endpoint_id, &window_covering_cfg);
    if (zigbee_ctx.window_ep == NULL) {
        ESP_LOGE(TAG, "Не удалось создать эндпоинт Window Covering");
        return ESP_FAIL;
    }
    
    // Регистрация колбэка для команд кластера
    ESP_ERROR_CHECK(esp_zb_cluster_update_commands(
        zigbee_ctx.window_ep,
        WINDOW_COVERING_CLUSTER_ID,
        window_covering_cluster_handler));
    
    zigbee_ctx.initialized = true;
    ESP_LOGI(TAG, "ZigBee библиотека успешно инициализирована");
    
    return ESP_OK;
}

/**
 * @brief Запуск ZigBee устройства
 */
esp_err_t esp_zigbee_start(void)
{
    ESP_LOGI(TAG, "Запуск ZigBee библиотеки");
    
    if (!zigbee_ctx.initialized) {
        ESP_LOGE(TAG, "ZigBee библиотека не инициализирована");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (zigbee_ctx.started) {
        ESP_LOGW(TAG, "ZigBee библиотека уже запущена");
        return ESP_OK;
    }
    
    // Запуск ZigBee стека и подключение к сети
    ESP_LOGI(TAG, "Запуск ZigBee стека и поиск сети...");
    esp_zb_start(false);  // False означает, что устройство не является координатором
    
    // Запуск основного цикла ZigBee (должен выполняться в отдельной задаче)
    esp_zb_main_loop_iteration();
    
    zigbee_ctx.started = true;
    ESP_LOGI(TAG, "ZigBee библиотека успешно запущена");
    
    return ESP_OK;
}

/**
 * @brief Остановка ZigBee устройства
 */
esp_err_t esp_zigbee_stop(void)
{
    ESP_LOGI(TAG, "Остановка ZigBee библиотеки");
    
    if (!zigbee_ctx.initialized) {
        ESP_LOGE(TAG, "ZigBee библиотека не инициализирована");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!zigbee_ctx.started) {
        ESP_LOGW(TAG, "ZigBee библиотека уже остановлена");
        return ESP_OK;
    }
    
    // Остановка ZigBee стека
    esp_zb_scheduler_reset();
    
    zigbee_ctx.started = false;
    zigbee_ctx.pairing_enabled = false;
    
    ESP_LOGI(TAG, "ZigBee библиотека успешно остановлена");
    return ESP_OK;
}

/**
 * @brief Установка типа устройства
 */
esp_err_t esp_zigbee_set_device_type(esp_zigbee_device_type_t device_type)
{
    ESP_LOGI(TAG, "Установка типа устройства: %d", device_type);
    
    if (!zigbee_ctx.initialized) {
        ESP_LOGE(TAG, "ZigBee библиотека не инициализирована");
        return ESP_ERR_INVALID_STATE;
    }
    
    zigbee_ctx.device_type = device_type;
    
    // Обновление атрибута типа устройства
    uint8_t zb_device_type = 0;  // Значение по умолчанию
    
    switch (device_type) {
        case ESP_ZIGBEE_DEVICE_TYPE_COVER:
            zb_device_type = 0x01;  // Жалюзи/Окно
            break;
        case ESP_ZIGBEE_DEVICE_TYPE_LIGHT:
            zb_device_type = 0x02;  // Световое устройство
            break;
        case ESP_ZIGBEE_DEVICE_TYPE_SWITCH:
            zb_device_type = 0x03;  // Переключатель
            break;
        case ESP_ZIGBEE_DEVICE_TYPE_SENSOR:
            zb_device_type = 0x04;  // Датчик
            break;
        default:
            zb_device_type = 0x01;  // По умолчанию жалюзи/окно
            break;
    }
    
    // Установка атрибута типа устройства
    esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
        zigbee_ctx.window_ep,
        WINDOW_COVERING_CLUSTER_ID,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        WINDOW_COVERING_TYPE_ATTRIBUTE_ID,
        &zb_device_type,
        sizeof(uint8_t));
    
    if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Не удалось установить атрибут типа устройства: %d", status);
    }
    
    ESP_LOGI(TAG, "Тип устройства успешно установлен");
    return ESP_OK;
}

/**
 * @brief Включение/отключение режима сопряжения
 */
esp_err_t esp_zigbee_enable_pairing(bool enable)
{
    ESP_LOGI(TAG, "%s режима сопряжения", enable ? "Включение" : "Отключение");
    
    if (!zigbee_ctx.initialized || !zigbee_ctx.started) {
        ESP_LOGE(TAG, "ZigBee библиотека не инициализирована или не запущена");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (enable) {
        // Разрешить присоединение к сети через идентификацию
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
    } else {
        // Отключение режима сопряжения, в ESP-ZB это делается автоматически
        // когда устройство подключается к сети или по таймеру
    }
    
    zigbee_ctx.pairing_enabled = enable;
    ESP_LOGI(TAG, "Режим сопряжения %s", enable ? "включен" : "отключен");
    return ESP_OK;
}

/**
 * @brief Обработка входящих команд ZigBee
 */
esp_err_t esp_zigbee_process_commands(void)
{
    if (!zigbee_ctx.initialized || !zigbee_ctx.started) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Выполняем одну итерацию основного цикла ZigBee
    esp_zb_main_loop_iteration();
    
    // В случае ESP-ZB, команды обрабатываются автоматически через колбэки,
    // поэтому здесь не нужно делать дополнительную обработку
    
    return ESP_OK;
}

/**
 * @brief Отправка состояния окна
 */
esp_err_t esp_zigbee_report_window_state(esp_zigbee_window_mode_t mode, uint8_t position)
{
    ESP_LOGI(TAG, "Отправка состояния окна: режим=%d, положение=%d%%", mode, position);
    
    if (!zigbee_ctx.initialized || !zigbee_ctx.started) {
        ESP_LOGE(TAG, "ZigBee библиотека не инициализирована или не запущена");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Обновление атрибутов в кластере
    uint8_t window_mode = 0;
    
    switch (mode) {
        case ESP_ZIGBEE_WINDOW_MODE_CLOSED:
            window_mode = 0;
            break;
        case ESP_ZIGBEE_WINDOW_MODE_OPEN:
            window_mode = 1;
            break;
        case ESP_ZIGBEE_WINDOW_MODE_VENTILATE:
            window_mode = 2;
            break;
        default:
            window_mode = 0;
            break;
    }
    
    // Установка атрибута режима
    esp_zb_zcl_status_t mode_status = esp_zb_zcl_set_attribute_val(
        zigbee_ctx.window_ep,
        WINDOW_COVERING_CLUSTER_ID,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        WINDOW_COVERING_MODE_ATTRIBUTE_ID,
        &window_mode,
        sizeof(uint8_t));
    
    if (mode_status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Не удалось установить атрибут режима: %d", mode_status);
    }
    
    // Установка атрибута положения
    esp_zb_zcl_status_t pos_status = esp_zb_zcl_set_attribute_val(
        zigbee_ctx.window_ep,
        WINDOW_COVERING_CLUSTER_ID,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        WINDOW_COVERING_POS_ATTRIBUTE_ID,
        &position,
        sizeof(uint8_t));
    
    if (pos_status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Не удалось установить атрибут положения: %d", pos_status);
    }
    
    // Отправка отчета об изменении атрибутов
    esp_zb_zcl_report_attr_cmd_t report_cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,  // Адрес координатора
            .dst_endpoint = 1,
            .src_endpoint = zigbee_ctx.endpoint_id,
        },
        .cluster_id = WINDOW_COVERING_CLUSTER_ID,
        .cluster_role = ZB_ZCL_CLUSTER_SERVER_ROLE,
    };
    
    esp_zb_zcl_report_attr(&report_cmd);
    
    ESP_LOGI(TAG, "Состояние окна успешно отправлено");
    return ESP_OK;
}

/**
 * @brief Отправка режима окна
 */
esp_err_t esp_zigbee_report_window_mode(uint8_t mode)
{
    ESP_LOGI(TAG, "Отправка режима окна: %d", mode);
    
    if (!zigbee_ctx.initialized || !zigbee_ctx.started) {
        ESP_LOGE(TAG, "ZigBee библиотека не инициализирована или не запущена");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Установка атрибута режима
    esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
        zigbee_ctx.window_ep,
        WINDOW_COVERING_CLUSTER_ID,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        WINDOW_COVERING_MODE_ATTRIBUTE_ID,
        &mode,
        sizeof(uint8_t));
    
    if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Не удалось установить атрибут режима: %d", status);
        return ESP_FAIL;
    }
    
    // Отправка отчета об изменении атрибута
    esp_zb_zcl_report_attr_cmd_t report_cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,  // Адрес координатора
            .dst_endpoint = 1,
            .src_endpoint = zigbee_ctx.endpoint_id,
        },
        .cluster_id = WINDOW_COVERING_CLUSTER_ID,
        .cluster_role = ZB_ZCL_CLUSTER_SERVER_ROLE,
    };
    
    esp_zb_zcl_report_attr(&report_cmd);
    
    ESP_LOGI(TAG, "Режим окна успешно отправлен");
    return ESP_OK;
}

/**
 * @brief Отправка положения окна
 */
esp_err_t esp_zigbee_report_position(uint8_t position)
{
    ESP_LOGI(TAG, "Отправка положения окна: %d%%", position);
    
    if (!zigbee_ctx.initialized || !zigbee_ctx.started) {
        ESP_LOGE(TAG, "ZigBee библиотека не инициализирована или не запущена");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Установка атрибута положения
    esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
        zigbee_ctx.window_ep,
        WINDOW_COVERING_CLUSTER_ID,
        ZB_ZCL_CLUSTER_SERVER_ROLE,
        WINDOW_COVERING_POS_ATTRIBUTE_ID,
        &position,
        sizeof(uint8_t));
    
    if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Не удалось установить атрибут положения: %d", status);
        return ESP_FAIL;
    }
    
    // Отправка отчета об изменении атрибута
    esp_zb_zcl_report_attr_cmd_t report_cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,  // Адрес координатора
            .dst_endpoint = 1,
            .src_endpoint = zigbee_ctx.endpoint_id,
        },
        .cluster_id = WINDOW_COVERING_CLUSTER_ID,
        .cluster_role = ZB_ZCL_CLUSTER_SERVER_ROLE,
    };
    
    esp_zb_zcl_report_attr(&report_cmd);
    
    ESP_LOGI(TAG, "Положение окна успешно отправлено");
    return ESP_OK;
}

/**
 * @brief Отправка уведомления о событии
 */
esp_err_t esp_zigbee_send_alert(esp_zigbee_alert_type_t alert_type, uint8_t value)
{
    ESP_LOGI(TAG, "Отправка уведомления: тип=%d, значение=%d", alert_type, value);
    
    if (!zigbee_ctx.initialized || !zigbee_ctx.started) {
        ESP_LOGE(TAG, "ZigBee библиотека не инициализирована или не запущена");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Формируем данные уведомления (в ZCL используем кластер Alarms)
    uint8_t alarm_code = 0;
    
    switch (alert_type) {
        case ESP_ZIGBEE_ALERT_LOW_BATTERY:
            alarm_code = 0x01;  // Низкий заряд батареи
            break;
        case ESP_ZIGBEE_ALERT_STUCK:
            alarm_code = 0x02;  // Сервопривод заблокирован
            break;
        case ESP_ZIGBEE_ALERT_MODE_CHANGE:
            alarm_code = 0x03;  // Изменение режима
            break;
        case ESP_ZIGBEE_ALERT_PROTECTION:
            alarm_code = 0x04;  // Сработала защита
            break;
        default:
            alarm_code = 0xFF;  // Неизвестный тип
            break;
    }
    
    // Отправка уведомления через кластер Alarms
    esp_zb_zcl_alarm_cmd_t alarm_cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,  // Адрес координатора
            .dst_endpoint = 1,
            .src_endpoint = zigbee_ctx.endpoint_id,
        },
        .alarm_code = alarm_code,
        .cluster_id = WINDOW_COVERING_CLUSTER_ID,
    };
    
    esp_zb_zcl_alarm(&alarm_cmd);
    
    ESP_LOGI(TAG, "Уведомление успешно отправлено");
    return ESP_OK;
} 