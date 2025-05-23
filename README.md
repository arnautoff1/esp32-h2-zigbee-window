# Умное устройство управления окнами на базе ESP32-H2 с ZigBee

Проект умного устройства для автоматизации окон с использованием микроконтроллера ESP32-H2 и протокола ZigBee 3.0.

## Возможности
- Управление окном через два сервопривода:
  - Поворот ручки окна (открытие/закрытие, проветривание)
  - Регулировка расстояния от окна до рамы
- Интеграция с Яндекс Алисой через ZigBee
- Поддержка OTA-обновлений
- Энергосбережение и защита от механических сбоев
- Сохранение состояния в энергонезависимой памяти
- Система уведомлений о состоянии устройства (сопротивление, низкий заряд)

## Функциональные возможности
- **Режимы работы окна**:
  - **Закрыто**: Ручка в положении 0° (вертикально вниз)
  - **Открыто**: Ручка в положении 90° (горизонтально)
  - **Проветривание**: Ручка в положении 180° (вертикально вверх)
- **Управление зазором**: Регулировка расстояния между окном и рамой (0-100%)
- **Интеграция с голосовыми ассистентами**: Управление через Яндекс Алису
- **Расширенные функции**:
  - OTA-обновления прошивки
  - Энергосбережение при работе от батареи
  - Мониторинг состояния батареи
  - Защита от механического сопротивления
  - Сохранение и восстановление состояния
  - Уведомления о событиях и ошибках

## Структура проекта
- `/main` - основной код проекта
  - `main.c` - основной файл проекта
  - `servo_control.c/h` - управление сервоприводами
  - `zigbee_handler.c/h` - обработка ZigBee
  - `ota_update.c/h` - модуль OTA-обновлений
  - `power_management.c/h` - управление питанием
  - `state_management.c/h` - управление состоянием
- `CMakeLists.txt` - конфигурация сборки проекта
- `sdkconfig.defaults` - настройки ESP-IDF по умолчанию
- `INSTALL.md` - инструкция по установке и настройке

## Требования
- ESP32-H2 с поддержкой ZigBee 3.0
- ESP-IDF v5.0 или выше
- Два сервопривода MG996R или аналогичных
- Источник питания (батарея или внешнее питание)

## Быстрый старт
1. Установите ESP-IDF (см. [INSTALL.md](INSTALL.md))
2. Клонируйте репозиторий
3. Выполните сборку и прошивку:
```bash
idf.py set-target esp32h2
idf.py build
idf.py -p PORT flash
```

## Подключение
- **Сервопривод 1 (ручка)**: GPIO4
- **Сервопривод 2 (зазор)**: GPIO5
- **АЦП для измерения батареи**: GPIO0 (ADC1_CH0)
- **Определение внешнего питания**: GPIO5

## Интеграция с Яндекс Алисой
Устройство поддерживает интеграцию с Яндекс Алисой через ZigBee-координатор и Home Assistant. В Алисе устройство отображается как "шторы" (так как отдельной категории для окон нет).

**Голосовые команды:**
- "Алиса, открой окно в спальне"
- "Алиса, закрой окно на кухне"
- "Алиса, проветри комнату"
- "Алиса, открой окно в спальне на 50 процентов"

## Система уведомлений
Устройство отправляет уведомления через ZigBee при следующих событиях:
- **Обнаружение механического сопротивления** - если сервопривод встречает препятствие
- **Низкий уровень заряда батареи** - когда заряд падает ниже порогового значения
- **Изменение режима работы** - при изменении положения окна

Уведомления обрабатываются Home Assistant и могут быть настроены для отправки push-уведомлений в мобильное приложение или для запуска автоматизаций.

## Расширенная интеграция
Хотя основной фокус проекта - интеграция с Яндекс Алисой, устройство также может быть интегрировано с:
- **Google Assistant** - через Home Assistant и соответствующую интеграцию
- **Apple HomeKit** - через Home Assistant и соответствующую интеграцию
- **Другие системы умного дома**, поддерживающие ZigBee (SmartThings, Hubitat, и т.д.)

## Подробная документация
Более подробные инструкции по установке, настройке и использованию можно найти в файле [INSTALL.md](INSTALL.md).

## Поддержка и развитие проекта
Если у вас возникли вопросы или предложения по улучшению проекта, создайте issue в репозитории или свяжитесь с нами напрямую.

## Лицензия
MIT 