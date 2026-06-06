# g510s — Logitech G510/G510s keyboard utility

Утилита на C для полноценной работы клавиатуры Logitech G510/G510s под Linux.
Заменяет g15daemon и gnome15. Запускается от пользователя (не root).

## Что умеет

- Цифровые часы на LCD-экране клавиатуры
- 4 профиля (M1/M2/M3/MR) с сохранением цвета подсветки и G-команд
- Запуск скриптов/команд по G-клавишам
- Системный трей (AppIndicator) с GUI
- TCP-сервер на 127.0.0.1:15550 (совместимость с libg15daemon-client)
- Hotplug USB-устройств (встроенный аудиоадаптер)
- uinput для виртуальных нажатий клавиш
- LCD-экраны: часы / CPU / sysmon (RAM+диски+сеть) / Claude usage

## Карта исходников

| Файл | Назначение |
|---|---|
| `g510s.c` | `main()`, GTK-инициализация, AppIndicator, системный трей |
| `g510s-threads.c` | Рабочие потоки: опрос клавиш, обновление LCD, сокет-сервер |
| `g510s-keys.c` | Обработка G-клавиш, uinput, запуск команд |
| `g510s-signals.c` | GTK-сигналы (GUI события) |
| `g510s-config.c` | Загрузка/сохранение профилей M1/M2/M3/MR |
| `g510s-clock.c` | Цифровые часы на LCD-экране клавиатуры |
| `g510s-cpu.c` | CPU-экран на LCD |
| `g510s-sysmon.c` | Экран мониторинга системы (RAM, диски, сеть, температура) |
| `g510s-claude-lcd.c` | Экран использования Claude Code (JSONL из ~/.claude/projects/) |
| `g510s-net.c` | TCP-сервер (совместимость с libg15daemon-client) |
| `g510s-list.c` | Список LCD-клиентов (связный список) |
| `g510s-misc.c` | Вспомогательные функции |
| `g510s.h` | Общие типы и объявления (lcd_t, lcdnode_t, g510s_data_s) |
| `g510s.glade` | UI в GTK3 Builder XML |
| `99-g510s.rules` | udev-правила для USB-доступа без root |
| `g510s.desktop` | Autostart-запись для автозапуска при входе |
| `g510s.svg` / `g510s-alert.svg` | Иконки трея |

## Зависимости

| Библиотека | Откуда | Примечание |
|---|---|---|
| libg15 | `libg15/` в репо (bundled) | Портирована на libusb-1.0, содержит патчи |
| libg15render | `libg15render/` в репо (bundled) | v1.3.1, исправлен UAF-баг в g15r_loadG15Font |
| GTK3 | `apt: libgtk-3-dev` | |
| AppIndicator3 | `apt: libayatana-appindicator3-dev` | Ayatana-форк, требует compat-шим |
| libusb-1.0 | `apt: libusb-1.0-0-dev` | libg15 портирована на 1.0 |
| pthreads / libudev | системные | |

## Сборка

```bash
./build.sh
```

Скрипт делает:
1. Устанавливает системные пакеты
2. Создаёт compat-шим appindicator3-0.1 → ayatana без sudo
3. Клонирует и собирает libg15render из vividnightmare/libg15render (фикс UAF)
4. Копирует и собирает patched libg15 из `libg15/` репозитория
5. Собирает g510s через `make`
6. Устанавливает (`sudo make install`)
7. Добавляет пользователя в группу `plugdev`, перезагружает udev с `--action=add`

После сборки: `g510s` или `/usr/local/bin/g510s`

## Известные нюансы сборки (Linux Mint 22 / Ubuntu 24.04)

**libg15render:** apt-пакет `libg15render 1.3.0~svn316-3` содержит heap-use-after-free
в `g15r_loadG15Font` (realloc + fread в освобождённый указатель). Исходники
vividnightmare/libg15render v1.3.1 **bundled в `libg15render/`** — build.sh копирует их в /tmp
(как libg15), git clone больше не нужен. `configure.ac` уже пропатчен: `AC_PREREQ([2.71])`.

**libg15:** вшита в репо (`libg15/`), не клонируется — портирована на libusb-1.0,
исправлены buffer overflow и exitLibG15. Сборка копирует `libg15/` в `/tmp/g510s-build-deps/libg15/`.

**AppIndicator:** на Mint 22 заголовок находится по пути
`/usr/include/libayatana-appindicator3-0.1/libayatana-appindicator/app-indicator.h`
(версионированный подкаталог). build.sh ищет его через `find`.

**udev-правила:** `ACTION` убран из `99-g510s.rules`, trigger вызывается с `--action=add`.

**Пути ресурсов:** пути к `.glade` и иконкам вынесены в макрос `G510S_DATA_DIR`
(задаётся в Makefile, по умолчанию `/usr/local/share/g510s`).
Можно переопределить: `make DATA_DIR=/другой/путь`.

## Установка

```
/usr/local/bin/g510s               — бинарник
/usr/local/share/g510s/            — ресурсы (glade, svg)
/lib/udev/rules.d/99-g510s.rules   — права USB без root
/etc/xdg/autostart/g510s.desktop   — автозапуск при входе
```

## Threading-модель

Три рабочих потока, запускаются из `g510s-threads.c`:

| Поток | Что делает | Мьютексы |
|---|---|---|
| `key_function` | `getPressedKeys(timeout=10ms)` | `libusb_mutex` ≤10ms |
| `update_function` | рендеринг экранов + `writePixmapToLCD` каждые 50ms | `libg15_mutex` → `libusb_mutex` |
| `server_function` | TCP-клиенты libg15daemon | `lcdlist_mutex` |

**Порядок захвата:** `libg15_mutex` → `lcdlist_mutex` → `libusb_mutex`. Дедлоков нет.

**Ключевые решения:**
- `timeout=10ms` в `getPressedKeys`: G510 шлёт USB interrupt только при нажатии (не непрерывно), поэтому `timeout=0` (unlimited) блокировал `libusb_mutex` вечно когда клавиатура молчала — фикс: 10ms таймаут освобождает мьютекс каждые ≤10ms, заменяет прежний `usleep(10ms)`
- `claude_maybe_scan()` вызывается вне `libg15_mutex` (занимает до 500ms), рендер claude_screen — внутри

## Ключевые структуры данных (`g510s.h`)

- `lcd_t` — состояние одного LCD-клиента (буфер, подсветка, контраст, m-key)
- `lcdnode_t` / `lcdlist_t` — двусвязный список LCD-клиентов
- `g510s_data_s` — глобальное состояние приложения (профили M1/M2/M3/MR, режим часов)
- `m_data_s` — данные одного профиля (RGB + команды G1–G18)
- `internal_screen` в `g510s_data_s`: 0=clock, 1=cpu, 2=sysmon, 3=claude

## Сетевой протокол

TCP 127.0.0.1:15550, совместим с g15daemon.
Команды клиенту: `k` (keystate), `p` (switch priorities), `v` (foreground), `u` (user selected).
Команды серверу (битовые флаги): `0x80` backlight, `0x40` contrast, `0x20` mkey lights, `0x10` key handler.

## Исправленные баги

**Критические:**
- NULL-dereference при отсутствии `$HOME` (`g510s-config.c`)
- Buffer overread в 'W'-клиентах LCD (`g510s-threads.c`)
- `recv(sock, NULL, ...)` при сбросе лишних данных (`g510s-threads.c`)
- `malloc()` без проверки на NULL (`g510s-threads.c`)
- Segfault при L1: `getPressedKeys` без мьютекса конкурировал с `writePixmapToLCD` (`libg15/libg15.c`)
- Heap corruption при claude→clock: UAF в `g15r_loadG15Font` apt-версии libg15render (фикс: сборка из исходников)

**Высокие:**
- Все мьютексы `lcdlist_mutex` были закомментированы — включены
- Глобальные флаги объявлены `volatile`
- Утечка `clientnode` при сбое `pthread_create` (`g510s-net.c`)
- Двойной `pthread_detach` (`g510s-net.c`)

**Средние:**
- `malloc(sizeof(lcdnode_t))` для `lcdlist_t*` → `sizeof(lcdlist_t)`
- `memset(..., 1024)` для буфера 1048 байт → `sizeof(buf)`
- Жёстко прошитые пути → макрос `G510S_DATA_DIR`
- Спам в лог при отключении устройства → printf вынесен из цикла

**Низкие:**
- Опечатка "invalide" → "invalid"
- Отсутствующий `#include <string.h>` в `g510s.c`

## TODO / направления доработки

- Valgrind (утечки памяти)
- Новая иконка
- DBUS IPC
- Сохранение при выключении системы
- Сохранение `internal_screen` в конфиг
- Запуск экранов из GUI
</content>
