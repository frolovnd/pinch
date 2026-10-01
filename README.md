# pinch

Скриншот области экрана с рисованием для Ubuntu (X11). По образцу Flameshot, но с маленькой
кодовой базой, которую можно прочитать целиком.

## Сборка и установка

```bash
sudo apt install qt6-base-dev qt6-tools-dev qt6-l10n-tools libwayland-dev libwayland-bin cmake g++
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build --prefix ~/.local
scripts/set-gnome-shortcut.sh   # Shift+Super+S → pinch
```

`cmake --install build --prefix ~/.local` ставит также иконку (`~/.local/share/icons/hicolor/…/apps/pinch.png`) и пункт меню (`~/.local/share/applications/pinch.desktop`). Иконки в репозитории уже отрисованы; `scripts/render-icons.sh` пересобирает их из `data/icons/pinch-source.png` (нужен ImageMagick, только для разработчика).

Проверка под санитайзерами:

```bash
cmake -S . -B build-asan -DPINCH_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
```

Wayland (`-DPINCH_WAYLAND=ON`, по умолчанию) требует `wayland-client` и `wayland-scanner`
(Ubuntu — `libwayland-dev libwayland-bin`, Arch — `wayland`); `-DPINCH_WAYLAND=OFF` — сборка только для X11.
Тест `test_screencopy_fake` (клиент screencopy против поддельного композитора) собирается, только если найден
`wayland-server.pc` — он из того же пакета (`libwayland-dev` / `wayland`); без него тест просто не регистрируется.

## Управление

| Клавиша | Действие |
|---|---|
| мышь | выделить область; клик — весь монитор; после выделения сразу активен карандаш: тяните внутри рамки, чтобы рисовать |
| Ctrl+A | все мониторы |
| P M L A R E T N B | карандаш, маркер, линия, стрелка, прямоугольник, эллипс, текст, номерок, пикселизация |
| V | перемещать и растягивать рамку выделения; чтобы начать новое выделение, нажмите V и тяните вне рамки |
| Shift при рисовании | 45° для линий, квадрат/круг для фигур |
| 1 – 5 | толщина из пресетов (2, 4, 8, 14, 24 px) |
| колесо | толщина ±1 (1–40); то же — ползунок и кнопки-пресеты в панели |
| Ctrl+Z / Ctrl+Shift+Z | отменить / повторить |
| Ctrl+C или Enter | копировать в буфер |
| Ctrl+S | сохранить в ~/Pictures/Screenshots |
| Ctrl+Shift+S | сохранить как… |
| Esc | завершить текст / закрыть |

Клавиши работают в любой раскладке.

## Сборка на других дистрибутивах (Arch и др.)

```bash
sudo pacman -S qt6-base qt6-tools wayland cmake gcc noto-fonts   # шрифты нужны для значков кнопок панели
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

Если более новый компилятор добавляет свои предупреждения, а сборка падает из-за `-Werror`,
добавьте `-DPINCH_WERROR=OFF` (остальные предупреждения и hardening остаются).
Приложение работает только в сеансе X11: Wayland пока не поддерживается.

## Язык интерфейса / Localization

Язык берётся из системы (`LANGUAGE`, `LC_MESSAGES`, `LANG`); если для него нет перевода — английский.
Сейчас доступны: en, ru. Попробовать русский: `LANGUAGE=ru ./build/pinch`.
The UI language follows the system locale (`LANGUAGE`/`LC_MESSAGES`/`LANG`), with English as the fallback.

Все тексты в коде — ключи `qtTrId("...")`, сами тексты лежат в таблицах `translations/pinch_<язык>.ts`.
Добавить язык: скопировать `translations/pinch_en.ts` в `translations/pinch_xx.ts`, перевести каждый
`<translation>` (и поправить `language="xx"` в заголовке), добавить файл в `qt_add_translations`
в `CMakeLists.txt`. Зависимость сборки: Ubuntu — `qt6-tools-dev qt6-l10n-tools`, Arch — `qt6-tools`.
Тест `check_i18n` проверяет, что все ключи из кода есть в обеих таблицах.

## Модель угроз и ограничения

- Приложение не регистрирует служб D-Bus, не открывает слушающих сокетов и не выходит в сеть;
  оно говорит только с X-сервером. Мост доступности Qt (AT-SPI) при показе окна всё же может открыть
  клиентское соединение с сессионной шиной пользователя. Это проверяется вручную через `strace` (см. ниже).
  Платформенная тема GTK и соединение с менеджером сессии отключены в `main()`.
- Настройки лежат в `~/.config/pinch/pinch.conf`. Диалог «Сохранить как» (собственный диалог Qt)
  хранит последний каталог и историю в `~/.config/QtProject.conf`.
- Файлы пишутся атомарно: временный файл `0600` в каталоге назначения, затем `renameat2`.
  Путь назначения не открывается напрямую, поэтому подложенный симлинк не перенаправит запись.
  Быстрое сохранение никогда не перезаписывает существующие файлы.
- Сохранённые скриншоты доступны только владельцу (`0600`).
- После копирования процесс живёт невидимым, пока буфер обмена не займёт другая программа:
  на X11 данные буфера отдаёт процесс-владелец.
- Пикселизация усредняет блоки от 12 px и необратима на практике, но для известного шрифта
  существуют атаки восстановления (Depix). Пароли надёжнее не захватывать вовсе.
- Только X11 и масштаб 1×. Wayland и HiDPI не поддерживаются.

## Проверка

Сетевую и IPC-активность проверяют из терминала при настоящем запуске:

```bash
PINCH_ALLOW_TRACE=1 strace -f -e trace=connect ~/.local/bin/pinch 2>&1 | grep 'connect('
```

По умолчанию процесс помечен «недампируемым» (`PR_SET_DUMPABLE=0`: снимок не попадёт в core-дамп и
не читается через `ptrace`), поэтому `strace` не может к нему подключиться и не читает строки.
Переменная `PINCH_ALLOW_TRACE=1` (именно значение `1`) отключает эту защиту на время проверки: приложение запускают сразу под `strace`.
Подключение к уже запущенному процессу (`strace -p`) не подходит: при `ptrace_scope=1` (Ubuntu) без `sudo` оно невозможно
для процесса, не являющегося потомком, а с `sudo` root подключается независимо от `PINCH_ALLOW_TRACE`.

Допустимы только сокет X11 и, самое большее, сессионная шина / шина AT-SPI.
