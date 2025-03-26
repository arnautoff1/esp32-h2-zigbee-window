/**
 * @file ota_update.h
 * @brief Модуль OTA-обновлений для умного окна
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Состояние OTA-обновления
 */
typedef enum {
    OTA_STATE_IDLE = 0,            ///< Нет активных обновлений
    OTA_STATE_CHECKING = 1,        ///< Проверка наличия обновлений
    OTA_STATE_DOWNLOADING = 2,     ///< Загрузка обновления
    OTA_STATE_READY_TO_APPLY = 3,  ///< Готово к применению
    OTA_STATE_APPLYING = 4,        ///< Применение обновления
    OTA_STATE_ERROR = 5            ///< Ошибка обновления
} ota_state_t;

/**
 * @brief Структура конфигурации OTA
 */
typedef struct {
    char server_url[128];         ///< URL сервера обновлений
    char firmware_version[16];    ///< Текущая версия прошивки
    uint32_t check_interval_ms;   ///< Интервал проверки обновлений в миллисекундах
    bool auto_check;              ///< Автоматическая проверка обновлений
    bool auto_update;             ///< Автоматическое применение обновлений
} ota_config_t;

/**
 * @brief Инициализация модуля OTA
 * 
 * @param config Конфигурация модуля OTA
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t ota_init(ota_config_t *config);

/**
 * @brief Запуск модуля OTA
 * 
 * @return esp_err_t ESP_OK при успешном запуске
 */
esp_err_t ota_start(void);

/**
 * @brief Остановка модуля OTA
 * 
 * @return esp_err_t ESP_OK при успешной остановке
 */
esp_err_t ota_stop(void);

/**
 * @brief Запрос на проверку обновлений
 * 
 * @return esp_err_t ESP_OK при успешном запуске проверки
 */
esp_err_t ota_check_update(void);

/**
 * @brief Загрузка обновления
 * 
 * @return esp_err_t ESP_OK при успешном запуске загрузки
 */
esp_err_t ota_download_update(void);

/**
 * @brief Применение обновления
 * 
 * @return esp_err_t ESP_OK при успешном применении обновления
 */
esp_err_t ota_apply_update(void);

/**
 * @brief Получение текущего состояния OTA
 * 
 * @return ota_state_t Текущее состояние OTA
 */
ota_state_t ota_get_state(void);

/**
 * @brief Получение процентного значения прогресса загрузки
 * 
 * @return uint8_t Процент загрузки (0-100)
 */
uint8_t ota_get_download_progress(void);

/**
 * @brief Получение текущей версии прошивки
 * 
 * @param version Буфер для сохранения версии
 * @param version_len Размер буфера
 * @return esp_err_t ESP_OK при успешном получении версии
 */
esp_err_t ota_get_firmware_version(char *version, size_t version_len);

/**
 * @brief Обработчик OTA-задачи
 * 
 * @note Эта функция вызывается автоматически в отдельной задаче
 */
void ota_task_handler(void *pvParameter);

#endif /* OTA_UPDATE_H */ 