# g510s

Утилита для полноценной работы клавиатуры Logitech G510/G510s под Linux.  
Заменяет g15daemon и gnome15. Запускается от пользователя (без root).

## Возможности

- Цифровые часы / CPU / sysmon (RAM, диски, сеть) / Claude Code usage — на LCD-экране
- 4 профиля (M1/M2/M3/MR) с сохранением цвета подсветки и команд G-клавиш
- Системный трей (AppIndicator) с GUI
- TCP-сервер на 127.0.0.1:15550 (совместимость с libg15daemon-client)
- Hotplug встроенного USB аудиоадаптера
- Виртуальные нажатия клавиш через uinput

## Зависимости

```
sudo apt install libgtk-3-dev libayatana-appindicator3-dev libusb-1.0-0-dev
```

Bundled: `libg15/` (порт на libusb-1.0), `libg15render/` (фикс heap-use-after-free).

## Сборка и установка

```bash
./build.sh
```

Скрипт соберёт зависимости, скомпилирует и установит (`sudo make install`).  
После установки: `g510s` или `/usr/local/bin/g510s`.

Пользовательский путь установки:

```bash
make BINDIR=/usr/bin DATA_DIR=/usr/share/g510s install
```

## Использование

```
g510s [options]

  --help|-h              Показать справку
  --log-level|-l LEVEL   Уровень логов: fatal, error, warn, info (по умолчанию), debug, trace
```

Лог пишется в `~/.g510s/g510s.log`.

## Удаление

```bash
sudo make uninstall
```

Удаляет бинарник, ресурсы, udev-правила и autostart-запись.

## Отключение автозапуска

```bash
sudo rm /etc/xdg/autostart/g510s.desktop
```

## LCD-экраны

Переключение кнопкой **L1** на клавиатуре.

| Экран | Что показывает |
|---|---|
| clock | Цифровые часы |
| cpu | Загрузка CPU по ядрам |
| sysmon | RAM, диски, сеть, температура |
| claude | Использование Claude Code (токены/стоимость) |

## Структура установки

```
/usr/local/bin/g510s
/usr/local/share/g510s/        — ресурсы (glade, svg)
/lib/udev/rules.d/99-g510s.rules
/etc/xdg/autostart/g510s.desktop
```

## Требования к системе

- Linux с uinput (`/dev/uinput`)
- Пользователь в группе `plugdev` (build.sh добавляет автоматически)
- GTK 3 + AppIndicator (Ayatana-форк)
