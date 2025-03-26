/**
 * @file zigbee_handler.c
 * @brief Реализация обработчика ZigBee для умного окна
 */

#include "zigbee_handler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_zb_include.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "servo_control.h"
#include "esp_check.h"

// Определение тега для логов
static const char* TAG = "ZIGBEE";

// Идентификаторы кластеров ZigBee
#define ESP_ZB_HA_WINDOW_COVERING_CLUSTER_ID       0x0102
#define ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL        0x0008

// Атрибуты
#define ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POS_LIFT_PERCENTAGE_ID   0x0008
#define ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POS_TILT_PERCENTAGE_ID   0x0009

// Команды
#define ESP_ZB_ZCL_CMD_WINDOW_COVERING_UP_OPEN                           0x00
#define ESP_ZB_ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE                        0x01
#define ESP_ZB_ZCL_CMD_WINDOW_COVERING_STOP                              0x02
#define ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE             0x05
#define ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE             0x08

// Настройки конечной точки (endpoint)
#define WINDOW_ENDPOINT                  1
#define HA_WINDOW_COVERING_DEVICE_ID     0x0202  // Window Covering Device ID

// Определение кластера для уведомлений и алармов
#define ZB_CLUSTER_ID_ALARMS                 0x0009
#define ZB_CLUSTER_ID_DIAGNOSTICS            0x0B05

// Коды уведомлений
#define ALERT_CODE_RESISTANCE                0x01
#define ALERT_CODE_LOW_BATTERY               0x02
#define ALERT_CODE_MODE_CHANGE               0x03

// Структура данных для хранения состояния ZigBee
typedef struct {
    zigbee_state_t state;                     // Текущее состояние ZigBee
    esp_zb_ep_list_t *ep_list;                // Список конечных точек
    uint16_t short_address;                   // Короткий адрес устройства в сети
    uint8_t endpoint;                         // Номер конечной точки
    bool pairing_mode_active;                 // Флаг активности режима сопряжения
    uint32_t pairing_mode_end_time;           // Время окончания режима сопряжения
    zigbee_config_t config;                   // Конфигурация ZigBee
} zigbee_state_data_t;

// Текущее состояние ZigBee
static zigbee_state_data_t zb_state = {
    .state = ZIGBEE_STATE_DISCONNECTED,
    .ep_list = NULL,
    .short_address = 0,
    .endpoint = WINDOW_ENDPOINT,
    .pairing_mode_active = false,
    .pairing_mode_end_time = 0
};

// Очередь команд ZigBee
typedef struct {
    uint8_t command;
    uint8_t param;
} zigbee_cmd_t;

static QueueHandle_t zigbee_cmd_queue = NULL;

// Прототипы функций
static esp_err_t zigbee_create_endpoint(void);
static void zigbee_device_cb(esp_zb_core_device_callback_id_t id, esp_zb_core_device_callback_param_t *param);
static void zigbee_attribute_cb(esp_zb_zcl_attribute_callback_id_t id, esp_zb_zcl_attribute_callback_param_t *param);
static esp_err_t zigbee_update_attribute(uint16_t cluster_id, uint16_t attr_id, void *value);

/**
 * @brief Инициализация ZigBee
 */
esp_err_t zigbee_init(zigbee_config_t *config)
{
    ESP_LOGI(TAG, "Инициализация ZigBee");
    
    // Проверка аргументов
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Копирование конфигурации
    memcpy(&zb_state.config, config, sizeof(zigbee_config_t));
    
    // Инициализация очереди команд
    zigbee_cmd_queue = xQueueCreate(10, sizeof(zigbee_cmd_t));
    if (zigbee_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Ошибка создания очереди команд");
        return ESP_ERR_NO_MEM;
    }
    
    // Инициализация ZigBee стека
    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,
        .install_code_policy = false,
        .it_channel = config->channel,
    };
    
    ESP_ERROR_CHECK(esp_zb_init(&zb_cfg));
    
    // Настройка обратных вызовов для ZigBee
    ESP_ERROR_CHECK(esp_zb_core_device_callback_register(zigbee_device_cb));
    ESP_ERROR_CHECK(esp_zb_zcl_attribute_callback_register(zigbee_attribute_cb));
    
    // Создание конечной точки (endpoint)
    ESP_ERROR_CHECK(zigbee_create_endpoint());
    
    // Установка состояния "не подключен"
    zb_state.state = ZIGBEE_STATE_DISCONNECTED;
    
    ESP_LOGI(TAG, "ZigBee инициализирован успешно");
    return ESP_OK;
}

/**
 * @brief Создание конечной точки ZigBee
 */
static esp_err_t zigbee_create_endpoint(void)
{
    ESP_LOGI(TAG, "Создание конечной точки ZigBee");
    
    // Создание кластера Window Covering для управления окном
    esp_zb_window_covering_cluster_cfg_t window_cluster_cfg = {
        .current_position_lift_percentage = 0,    // 0% - закрыто
        .current_position_tilt_percentage = 0     // 0% - без наклона
    };
    
    // Создание списка кластеров
    esp_zb_cluster_list_t *cluster_list = esp_zb_cluster_list_create();
    
    // Добавление кластера Window Covering как сервера
    esp_zb_window_covering_cluster_add_to_cluster_list(cluster_list, &window_cluster_cfg, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    // Создание списка конечных точек
    zb_state.ep_list = esp_zb_ep_list_create();
    
    // Добавление конечной точки с кластером
    esp_zb_ep_cfg_t endpoint_cfg = {
        .endpoint = WINDOW_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,  // Home Automation профиль
        .app_device_id = HA_WINDOW_COVERING_DEVICE_ID,
        .app_device_version = 0
    };
    
    esp_zb_ep_list_add_ep(zb_state.ep_list, cluster_list, endpoint_cfg);
    
    // Установка информации о производителе
    esp_zb_zcl_basic_cluster_cfg_t basic_cfg = {
        .model_identifier = zb_state.config.model,
        .manufacturer_name = zb_state.config.manufacturer
    };
    
    ESP_ERROR_CHECK(esp_zb_basic_cluster_create(&basic_cfg));
    
    return ESP_OK;
}

/**
 * @brief Запуск ZigBee стека
 */
esp_err_t zigbee_start(void)
{
    ESP_LOGI(TAG, "Запуск ZigBee стека");
    
    // Установка состояния "в процессе подключения"
    zb_state.state = ZIGBEE_STATE_CONNECTING;
    
    // Запуск стека
    ESP_RETURN_ON_ERROR(esp_zb_start(zb_state.ep_list), TAG, "Ошибка запуска ZigBee");
    
    // В реальном устройстве запуск происходит асинхронно
    ESP_LOGI(TAG, "ZigBee стек запущен");
    
    return ESP_OK;
}

/**
 * @brief Остановка ZigBee стека
 */
esp_err_t zigbee_stop(void)
{
    ESP_LOGI(TAG, "Остановка ZigBee стека");
    
    // Остановка ZigBee стека
    ESP_RETURN_ON_ERROR(esp_zb_stack_stop(), TAG, "Ошибка остановки ZigBee");
    
    // Установка состояния "не подключен"
    zb_state.state = ZIGBEE_STATE_DISCONNECTED;
    
    ESP_LOGI(TAG, "ZigBee стек остановлен");
    
    return ESP_OK;
}

/**
 * @brief Отправка команды открытия окна через ZigBee
 */
esp_err_t zigbee_send_window_mode(window_mode_t mode)
{
    ESP_LOGI(TAG, "Отправка команды режима окна: %d", mode);
    
    if (zb_state.state != ZIGBEE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "ZigBee не подключен, команда не будет отправлена");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Перевод режима окна в процент поднятия
    uint8_t lift_percentage;
    switch (mode) {
        case WINDOW_MODE_CLOSED:
            lift_percentage = 0;   // 0% - закрыто
            break;
        case WINDOW_MODE_OPEN:
            lift_percentage = 100; // 100% - открыто
            break;
        case WINDOW_MODE_VENT:
            lift_percentage = 50;  // 50% - проветривание (для Алисы открыто наполовину)
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    // Обновление атрибута в локальной конечной точке
    ESP_RETURN_ON_ERROR(zigbee_update_attribute(ESP_ZB_HA_WINDOW_COVERING_CLUSTER_ID, 
                                               ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POS_LIFT_PERCENTAGE_ID, 
                                               &lift_percentage), 
                        TAG, "Ошибка обновления атрибута");
    
    return ESP_OK;
}

/**
 * @brief Отправка команды управления зазором окна через ZigBee
 */
esp_err_t zigbee_send_gap_position(uint8_t percentage)
{
    ESP_LOGI(TAG, "Отправка команды позиции зазора: %d%%", percentage);
    
    if (zb_state.state != ZIGBEE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "ZigBee не подключен, команда не будет отправлена");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Проверка допустимости процента
    if (percentage > 100) {
        percentage = 100;
    }
    
    // Инвертируем процент для соответствия спецификации ZigBee
    uint8_t tilt_percentage = percentage;
    
    // Обновление атрибута в локальной конечной точке
    ESP_RETURN_ON_ERROR(zigbee_update_attribute(ESP_ZB_HA_WINDOW_COVERING_CLUSTER_ID, 
                                               ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POS_TILT_PERCENTAGE_ID, 
                                               &tilt_percentage), 
                        TAG, "Ошибка обновления атрибута");
    
    return ESP_OK;
}

/**
 * @brief Отправка обновления состояния в сеть ZigBee
 */
esp_err_t zigbee_report_state(void)
{
    ESP_LOGI(TAG, "Отправка обновления состояния");
    
    if (zb_state.state != ZIGBEE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "ZigBee не подключен, состояние не будет отправлено");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Получение текущего состояния
    window_mode_t mode = servo_get_window_mode();
    uint8_t gap_percentage = servo_get_gap();
    
    // Перевод режима в процент поднятия для ZigBee
    uint8_t lift_percentage;
    switch (mode) {
        case WINDOW_MODE_CLOSED:
            lift_percentage = 0;   // 0% - закрыто
            break;
        case WINDOW_MODE_OPEN:
            lift_percentage = 100; // 100% - открыто
            break;
        case WINDOW_MODE_VENT:
            lift_percentage = 50;  // 50% - проветривание
            break;
        default:
            lift_percentage = 0;
    }
    
    // Обновление атрибутов в локальной конечной точке
    ESP_RETURN_ON_ERROR(zigbee_update_attribute(ESP_ZB_HA_WINDOW_COVERING_CLUSTER_ID, 
                                               ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POS_LIFT_PERCENTAGE_ID, 
                                               &lift_percentage), 
                        TAG, "Ошибка обновления атрибута lift");
    
    ESP_RETURN_ON_ERROR(zigbee_update_attribute(ESP_ZB_HA_WINDOW_COVERING_CLUSTER_ID, 
                                               ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POS_TILT_PERCENTAGE_ID, 
                                               &gap_percentage), 
                        TAG, "Ошибка обновления атрибута tilt");
    
    return ESP_OK;
}

/**
 * @brief Получение текущего состояния ZigBee
 */
zigbee_state_t zigbee_get_state(void)
{
    return zb_state.state;
}

/**
 * @brief Включение режима сопряжения ZigBee
 */
esp_err_t zigbee_enable_pairing_mode(uint16_t duration_seconds)
{
    ESP_LOGI(TAG, "Включение режима сопряжения на %d секунд", duration_seconds);
    
    // Установка флага активности режима сопряжения
    zb_state.pairing_mode_active = true;
    
    // Установка времени окончания режима сопряжения
    uint32_t current_time = esp_timer_get_time() / 1000;
    zb_state.pairing_mode_end_time = current_time + (duration_seconds * 1000);
    
    // Открытие сети для сопряжения
    if (zb_state.state == ZIGBEE_STATE_CONNECTED) {
        ESP_LOGI(TAG, "Открытие сети ZigBee для сопряжения");
        ESP_ERROR_CHECK(esp_zb_network_permit_join(duration_seconds));
    }
    
    return ESP_OK;
}

/**
 * @brief Обработчик входящих команд ZigBee
 */
void zigbee_process_incoming_commands(void)
{
    // Проверка и обработка истечения времени режима сопряжения
    if (zb_state.pairing_mode_active) {
        uint32_t current_time = esp_timer_get_time() / 1000;
        if (current_time > zb_state.pairing_mode_end_time) {
            zb_state.pairing_mode_active = false;
            ESP_LOGI(TAG, "Режим сопряжения ZigBee завершен");
        }
    }
    
    // Обработка команд из очереди
    zigbee_cmd_t cmd;
    while (xQueueReceive(zigbee_cmd_queue, &cmd, 0) == pdTRUE) {
        ESP_LOGI(TAG, "Обработка команды ZigBee: %d, параметр: %d", cmd.command, cmd.param);
        
        // Обработка команд
        switch (cmd.command) {
            case ESP_ZB_ZCL_CMD_WINDOW_COVERING_UP_OPEN:
                // Команда открытия
                ESP_LOGI(TAG, "Команда: Открыть окно");
                servo_set_window_mode(WINDOW_MODE_OPEN);
                break;
                
            case ESP_ZB_ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE:
                // Команда закрытия
                ESP_LOGI(TAG, "Команда: Закрыть окно");
                servo_set_window_mode(WINDOW_MODE_CLOSED);
                break;
                
            case ESP_ZB_ZCL_CMD_WINDOW_COVERING_STOP:
                // Команда остановки
                ESP_LOGI(TAG, "Команда: Остановить движение");
                // Здесь можно реализовать остановку движения сервопривода
                break;
                
            case ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE:
                // Команда позиционирования (открытие/закрытие)
                ESP_LOGI(TAG, "Команда: Перейти к положению открытия %d%%", cmd.param);
                
                // Преобразование процента в режим окна
                if (cmd.param == 0) {
                    // 0% - полностью закрыто
                    servo_set_window_mode(WINDOW_MODE_CLOSED);
                } else if (cmd.param < 75) {
                    // Меньше 75% - режим проветривания
                    servo_set_window_mode(WINDOW_MODE_VENT);
                } else {
                    // 75-100% - полностью открыто
                    servo_set_window_mode(WINDOW_MODE_OPEN);
                }
                break;
                
            case ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE:
                // Команда управления зазором
                ESP_LOGI(TAG, "Команда: Установить зазор %d%%", cmd.param);
                
                // Установка зазора
                servo_set_gap(cmd.param);
                break;
                
            default:
                ESP_LOGW(TAG, "Неизвестная команда: %d", cmd.command);
                break;
        }
    }
}

/**
 * @brief Обновление атрибута ZigBee
 */
static esp_err_t zigbee_update_attribute(uint16_t cluster_id, uint16_t attr_id, void *value)
{
    if (zb_state.state != ZIGBEE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "ZigBee не подключен, атрибут не будет обновлен");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Обновление атрибута: кластер=0x%04x, атрибут=0x%04x", cluster_id, attr_id);
    
    // В реальной реализации здесь будет вызов API для обновления атрибута
    // Так как ESP32-H2 с ZigBee - новое решение, специфический код может отличаться
    
    return ESP_OK;
}

/**
 * @brief Обратный вызов для событий устройства ZigBee
 */
static void zigbee_device_cb(esp_zb_core_device_callback_id_t id, esp_zb_core_device_callback_param_t *param)
{
    ESP_LOGI(TAG, "Событие ZigBee устройства: %d", id);
    
    switch (id) {
        case ESP_ZB_CORE_DEVICE_STARTED:
            // Устройство запущено
            ESP_LOGI(TAG, "ZigBee стек запущен успешно");
            zb_state.state = ZIGBEE_STATE_CONNECTING;
            break;
            
        case ESP_ZB_CORE_DEVICE_JOINED:
            // Устройство присоединилось к сети
            ESP_LOGI(TAG, "Устройство присоединилось к сети ZigBee");
            zb_state.state = ZIGBEE_STATE_CONNECTED;
            zb_state.short_address = param->joined.short_addr;
            break;
            
        case ESP_ZB_CORE_DEVICE_RECV_DATA:
            // Получены данные
            ESP_LOGI(TAG, "Получены данные ZigBee");
            // Обработка полученных данных (в реальной реализации)
            break;
            
        default:
            ESP_LOGW(TAG, "Неизвестное событие ZigBee: %d", id);
            break;
    }
}

/**
 * @brief Обратный вызов для атрибутов ZigBee
 */
static void zigbee_attribute_cb(esp_zb_zcl_attribute_callback_id_t id, esp_zb_zcl_attribute_callback_param_t *param)
{
    ESP_LOGI(TAG, "Событие атрибута ZigBee: %d", id);
    
    switch (id) {
        case ESP_ZB_ZCL_ATTR_WRITE_CMD_RCV: {
            // Получена команда записи атрибута
            ESP_LOGI(TAG, "Получена команда записи атрибута: кластер=0x%04x, атрибут=0x%04x", 
                     param->write.cluster, param->write.attr_id);
            
            if (param->write.cluster == ESP_ZB_HA_WINDOW_COVERING_CLUSTER_ID) {
                // Обработка атрибутов кластера Window Covering
                if (param->write.attr_id == ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POS_LIFT_PERCENTAGE_ID) {
                    // Обработка изменения положения окна
                    uint8_t lift_percentage = *(uint8_t*)param->write.new_value;
                    ESP_LOGI(TAG, "Новое значение положения окна: %d%%", lift_percentage);
                    
                    // Добавление команды в очередь
                    zigbee_cmd_t cmd = {
                        .command = ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE,
                        .param = lift_percentage
                    };
                    xQueueSend(zigbee_cmd_queue, &cmd, 0);
                } 
                else if (param->write.attr_id == ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POS_TILT_PERCENTAGE_ID) {
                    // Обработка изменения наклона (зазора)
                    uint8_t tilt_percentage = *(uint8_t*)param->write.new_value;
                    ESP_LOGI(TAG, "Новое значение зазора: %d%%", tilt_percentage);
                    
                    // Добавление команды в очередь
                    zigbee_cmd_t cmd = {
                        .command = ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE,
                        .param = tilt_percentage
                    };
                    xQueueSend(zigbee_cmd_queue, &cmd, 0);
                }
            }
            break;
        }
        
        case ESP_ZB_ZCL_CMD_RCV: {
            // Получена команда кластера
            ESP_LOGI(TAG, "Получена команда кластера: кластер=0x%04x, команда=0x%02x", 
                     param->command.cluster, param->command.cmd_id);
            
            if (param->command.cluster == ESP_ZB_HA_WINDOW_COVERING_CLUSTER_ID) {
                // Обработка команд кластера Window Covering
                uint8_t cmd_id = param->command.cmd_id;
                
                // Добавление команды в очередь
                zigbee_cmd_t cmd = {
                    .command = cmd_id,
                    .param = 0
                };
                
                // Для команд позиционирования получаем параметр
                if (cmd_id == ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE ||
                    cmd_id == ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_TILT_PERCENTAGE) {
                    // В реальной реализации - парсинг параметра из данных команды
                    // cmd.param = param->command.data[0];
                }
                
                xQueueSend(zigbee_cmd_queue, &cmd, 0);
            }
            break;
        }
        
        default:
            ESP_LOGW(TAG, "Неизвестное событие атрибута ZigBee: %d", id);
            break;
    }
}

/**
 * @brief Отправка уведомления о событии через ZigBee
 */
esp_err_t zigbee_send_alert(zigbee_alert_type_t alert_type, uint8_t value)
{
    ESP_LOGI(TAG, "Отправка уведомления: тип=%d, значение=%d", alert_type, value);
    
    if (zb_state.state != ZIGBEE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "Невозможно отправить уведомление: ZigBee не подключен");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Подготовка данных для отправки
    uint8_t alert_code;
    uint16_t cluster_id;
    
    switch (alert_type) {
        case ZIGBEE_ALERT_RESISTANCE:
            alert_code = ALERT_CODE_RESISTANCE;
            cluster_id = ZB_CLUSTER_ID_ALARMS;
            break;
        case ZIGBEE_ALERT_LOW_BATTERY:
            alert_code = ALERT_CODE_LOW_BATTERY;
            cluster_id = ZB_CLUSTER_ID_DIAGNOSTICS;
            break;
        case ZIGBEE_ALERT_MODE_CHANGE:
            alert_code = ALERT_CODE_MODE_CHANGE;
            cluster_id = ZB_CLUSTER_ID_ALARMS;
            break;
        default:
            ESP_LOGE(TAG, "Неизвестный тип уведомления: %d", alert_type);
            return ESP_ERR_INVALID_ARG;
    }
    
    // Создание буфера для сообщения
    uint8_t buffer[3];
    buffer[0] = alert_code;  // Код предупреждения
    buffer[1] = value;       // Дополнительные данные
    buffer[2] = 0;           // Зарезервировано
    
    // Создание и отправка команды ZigBee на кластер уведомлений
    esp_zb_zcl_alarm_cmd_t alarm_cmd;
    memset(&alarm_cmd, 0, sizeof(esp_zb_zcl_alarm_cmd_t));
    
    alarm_cmd.alarm_code = alert_code;
    alarm_cmd.cluster_id = cluster_id;
    
    // Получение дескриптора конечной точки
    esp_zb_ep_handle_t ep_handle = esp_zb_ep_list_get_ep(zb_state.ep_list, zb_state.endpoint);
    if (ep_handle == NULL) {
        ESP_LOGE(TAG, "Не удалось получить дескриптор конечной точки");
        return ESP_FAIL;
    }
    
    // Отправка команды тревоги на координатор
    esp_err_t err = esp_zb_zcl_alarm_cmd_send(ep_handle, ZB_ZCL_CLUSTER_SERVER_ROLE, 0, &alarm_cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка отправки уведомления: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Уведомление успешно отправлено");
    return ESP_OK;
} 