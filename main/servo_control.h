/**
 * @file servo_control.h
 * @brief Управление сервоприводами для умного окна
 */

#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Режимы работы окна
 */
typedef enum {
    WINDOW_MODE_CLOSED = 0,  ///< Окно закрыто
    WINDOW_MODE_OPEN = 1,    ///< Окно открыто (поворот на 90 градусов)
    WINDOW_MODE_VENT = 2     ///< Окно в режиме проветривания (поворот на 180 градусов)
} window_mode_t;

/**
 * @brief Инициализация сервоприводов
 * 
 * @param handle_servo_pin Пин GPIO для сервопривода управления ручкой
 * @param gap_servo_pin Пин GPIO для сервопривода управления зазором
 * @return esp_err_t ESP_OK при успешной инициализации
 */
esp_err_t servo_init(uint8_t handle_servo_pin, uint8_t gap_servo_pin);

/**
 * @brief Изменение режима окна
 * 
 * @param mode Режим окна (закрыто, открыто, проветривание)
 * @return esp_err_t ESP_OK при успешном выполнении
 */
esp_err_t servo_set_window_mode(window_mode_t mode);

/**
 * @brief Управление зазором окна (работает только в режиме открыто)
 * 
 * @param percentage Процент открытия (0-100)
 * @return esp_err_t ESP_OK при успешном выполнении или ошибка, если окно закрыто
 */
esp_err_t servo_set_gap(uint8_t percentage);

/**
 * @brief Получение текущего режима окна
 * 
 * @return window_mode_t Текущий режим окна
 */
window_mode_t servo_get_window_mode(void);

/**
 * @brief Получение текущего положения зазора
 * 
 * @return uint8_t Текущий процент открытия (0-100)
 */
uint8_t servo_get_gap(void);

/**
 * @brief Остановка сервоприводов при обнаружении механического сопротивления
 * 
 * @param resistance_threshold Пороговое значение для определения сопротивления
 * @return esp_err_t ESP_OK при успешной установке
 */
esp_err_t servo_set_resistance_threshold(uint16_t resistance_threshold);

/**
 * @brief Проверка на наличие механического сопротивления
 * 
 * @return bool true, если обнаружено сопротивление движению
 */
bool servo_check_resistance(void);

/**
 * @brief Отключение сервоприводов (для экономии энергии)
 * 
 * @return esp_err_t ESP_OK при успешном отключении
 */
esp_err_t servo_disable(void);

/**
 * @brief Калибровка сервоприводов
 * 
 * @return esp_err_t ESP_OK при успешной калибровке
 */
esp_err_t servo_calibrate(void);

/**
 * @brief Включение режима симуляции сопротивления для тестирования
 * 
 * @param enable Включить/выключить симуляцию сопротивления
 */
void servo_simulate_resistance(bool enable);

/**
 * @brief Освобождение ресурсов модуля сервоприводов
 * 
 * @return esp_err_t ESP_OK при успешной деинициализации
 */
esp_err_t servo_deinit(void);

#endif /* SERVO_CONTROL_H */ 