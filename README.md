# Проектирование вычислительных систем — лабораторные работы

Стенд SDK-1.1MC.427, микроконтроллер STM32F427, вариант 1 «Светофор».

| Работа | Реализация | Запуск и защита |
|---|---|---|
| ЛР 1 | Светофор, кнопка с подавлением дребезга, собственный GPIO-драйвер | [README](lab1/README.md), [DEFENSE](lab1/docs/DEFENSE.md) |
| ЛР 2 | Управление светофором по USART1, HAL и собственный UART, опрос и прерывания | [README](lab2/README.md), [DEFENSE](lab2/docs/DEFENSE.md) |

Каждая работа — самостоятельный проект STM32CubeIDE со своими исходниками,
CMSIS/HAL, linker script и инструментами. Импортировать `lab1` и `lab2` отдельно;
генерация CubeMX не требуется. Команды ниже выполняются из этого каталога.

```bash
python lab1/tools/build.py
python lab2/tools/build.py
python lab2/tools/build.py --backend register
```

Нужны Python 3 и `arm-none-eabi-gcc` (в PATH либо через `ARM_GCC_BIN`).
В Windows скрипты также ищут ARM toolchain установленного CubeIDE в `C:/ST`.
Тесты и ограничения аппаратной проверки описаны в документации каждой работы.

Материалы перенесены из `Design-of-computing-systems/lab1`; история первой
лабораторной сохранена в этом Git-репозитории. Основная ветка — `main`.
Удалённый репозиторий: [computing-systems-labs](https://github.com/SaveliyDanko/computing-systems-labs).
