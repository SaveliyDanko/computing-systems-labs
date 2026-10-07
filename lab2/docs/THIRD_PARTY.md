# Источники компонентов

ЛР 2 наследует CMSIS/HAL, startup и linker script из ЛР 1 с сохранением лицензий.
Дополнительно добавлены `stm32f4xx_hal_uart.c/.h` из официального тега **v1.8.1**,
совпадающего с версией уже используемого HAL:
https://github.com/STMicroelectronics/stm32f4xx-hal-driver/tree/v1.8.1
Лицензия HAL сохранена в `Drivers/STM32F4xx_HAL_Driver/LICENSE.txt`.
Самостоятельные модули ЛР 2: `serial`, `uart_hal` (адаптер), `uart_register`,
`uart_pins`, `command`, `console`, дополнения `traffic` и `main`.
Unicorn и pyelftools используются только для тестов и устанавливаются отдельно.

* `Drivers/` (кроме добавленного `stm32f427xx.h`) и исходные настройки
  `.project`, `.cproject`, linker script взяты из `SDK_LED`:
  https://github.com/lmtspbru/SDK-1.1M/tree/bd8cba8f4bd5fc2d1460e6ad4da7b7dd13d9e14d/SDK_LED
* `docs/SDK1.1M.pdf` — электрическая схема из того же коммита производителя.
* `Core/Startup/startup_stm32f427xx.s`, `Core/Src/system_stm32f4xx.c`,
  `Drivers/CMSIS/Device/ST/STM32F4xx/Include/stm32f427xx.h`:
  https://github.com/STMicroelectronics/cmsis-device-f4/tree/a833f4af71410f25b01468f976560d7ff63a2fc9
  Текст лицензии: `docs/CMSIS_DEVICE_LICENSE.md`.

Лицензии исходных библиотек сохранены в `Drivers/**/LICENSE.txt`.
Настройки проекта адаптированы для STM32F427, память RAM увеличена до 192 КиБ,
Flash ограничена 1 МиБ для совместимости с вариантами VG и VI.
Код запуска, системный код и библиотеки не являются самостоятельно написанной
частью лабораторной. Собственная реализация GPIO — `Core/Src/gpio_driver.c`.

Тестовый эмулятор Unicorn и PDF-рендерер pypdfium2 устанавливаются отдельно
и не входят в прошивку. Для обычной сборки они не требуются.
