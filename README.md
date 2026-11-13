# Project 5 — Поиск ошибок памяти и неопределённого поведения

Учебный проект на C++17. Он содержит четыре изолированных демонстрационных дефекта и четыре исправленных варианта:

| Сценарий | Дефект | Ожидаемый инструмент |
|---|---|---|
| `oob` | Выход за границы динамического массива | AddressSanitizer |
| `uaf` | Чтение после освобождения памяти | AddressSanitizer |
| `overflow` | Переполнение знакового целого | UndefinedBehaviorSanitizer |
| `leak` | Потеря выделенного блока памяти | LeakSanitizer / Valgrind |

Сценарии с дефектами намеренные. Запускайте каждый отдельно: ожидается, что sanitizer сообщит об ошибке и остановит соответствующий процесс.

## Почему сборка sanitizer делается в Linux / WSL2

Основная среда пользователя — Windows 11 + MSVC + Qt. Этот проект специально отделён от Qt: он проверяет базовые ошибки C++, а не поведение сторонней GUI-библиотеки.

Для одной среды, в которой доступны GCC AddressSanitizer, UndefinedBehaviorSanitizer и Valgrind, используйте Ubuntu в WSL2. Valgrind официально поддерживает Linux, но не имеет поддерживаемой нативной Windows-платформы: https://valgrind.org/info/platforms.html

## Подготовка WSL2 (один раз)

В PowerShell Windows проверьте, установлен ли WSL:

```powershell
wsl --status
wsl --list --verbose
```

Если WSL ещё не установлен, откройте PowerShell от имени администратора и выполните:

```powershell
wsl --install -d Ubuntu
```

При необходимости перезагрузите Windows, затем откройте Ubuntu из меню «Пуск» и создайте Linux-пользователя.

В Ubuntu установите инструменты:

```bash
sudo apt update
sudo apt install -y build-essential cmake gdb valgrind
```

## Открытие проекта в WSL

Рекомендуется хранить рабочую копию проекта в файловой системе Linux, например `~/projects/memory-sanitizer-lab`, а не собирать её постоянно через `/mnt/c`. Распакуйте архив туда, затем в Ubuntu:

```bash
cd ~/projects/memory-sanitizer-lab
```

Для редактирования из VS Code установите расширение **WSL** от Microsoft, откройте папку из терминала Ubuntu командой `code .` и убедитесь, что в левом нижнем углу VS Code отображается подключение к WSL.

## 1. Отдельная sanitizer-сборка

Из корня проекта в Ubuntu:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel
```

Проверьте, что бинарник собран с sanitizer-флагами:

```bash
cat build-sanitize/CMakeFiles/memory_lab.dir/flags.make
```

В параметрах компиляции должны присутствовать `-fsanitize=address,undefined` и `-fno-omit-frame-pointer`.

## 2. Демонстрация AddressSanitizer

Запускайте из корня проекта:

```bash
./build-sanitize/memory_lab oob
./build-sanitize/memory_lab uaf
```

Ожидаемые типы сообщений: `heap-buffer-overflow` и `heap-use-after-free`.

## 3. Демонстрация UndefinedBehaviorSanitizer

```bash
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./build-sanitize/memory_lab overflow
```

Ожидаемый тип сообщения: `runtime error: signed integer overflow`.

## 4. LeakSanitizer и Valgrind

На Linux AddressSanitizer обычно включает проверку утечек при завершении процесса. Проверка сценария:

```bash
ASAN_OPTIONS=detect_leaks=1 ./build-sanitize/memory_lab leak
```

Для отдельного анализа Valgrind создайте обычную Debug-сборку **без ASan/UBSan**:

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=OFF
cmake --build build-debug --parallel
```

Запустите Memcheck:

```bash
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./build-debug/memory_lab leak
```

Не запускайте бинарник, собранный с AddressSanitizer, под Valgrind: для Valgrind используйте отдельную обычную Debug-сборку.

## 5. Повторная проверка исправлений

Каждый исправленный вариант запускается отдельной командой:

```bash
./build-sanitize/memory_lab oob-fixed
./build-sanitize/memory_lab uaf-fixed
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./build-sanitize/memory_lab overflow-fixed
ASAN_OPTIONS=detect_leaks=1 ./build-sanitize/memory_lab leak-fixed
```

Ожидается код возврата `0` и отсутствие диагностических сообщений sanitizer.

Для Valgrind проверьте исправленный сценарий:

```bash
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./build-debug/memory_lab leak-fixed
```

## Как разбирать отчёт

В каждом отчёте найдите и зафиксируйте:

1. **Тип ошибки** — например, `heap-use-after-free`.
2. **Операцию, на которой обнаружена ошибка** — чтение/запись и размер доступа.
3. **Строку исходного кода** — первый кадр stack trace, относящийся к `src/main.cpp`.
4. **Причину** — какой инвариант нарушен и почему.
5. **Исправление** — безопасное владение, корректная граница массива или арифметика в более широком типе.
6. **Повторную проверку** — исправленный вариант завершился без исходного отчёта.

Сохраните полный вывод терминала для отчёта и защиты. Не скрывайте диагностику переменными окружения, подавлением отчётов или отключением sanitizer.

## Краткие определения

- **Утечка памяти:** выделенный блок больше недоступен программе и не освобождён.
- **Use-after-free:** программа обращается к объекту после окончания времени жизни выделенной памяти.
- **Undefined behavior:** стандарт C++ не задаёт поведение программы для данной операции; пример в проекте — переполнение знакового `int`.

## Примечание для Windows / MSVC

Нативный MSVC AddressSanitizer существует, но в этом проекте комбинированная конфигурация ASan+UBSan и Valgrind проверяется через GCC в WSL2. Обычная Qt-сборка MSVC остаётся отдельной и не затрагивается.
