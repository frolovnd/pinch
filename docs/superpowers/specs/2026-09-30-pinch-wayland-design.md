# pinch под Wayland — спецификация

Статус: реализовано, ожидает ручной проверки.

Дата: 2026-09-30. Дополняет `2026-09-30-hot-screenshot-design.md` (далее «базовая спецификация»); всё, что здесь не сказано, остаётся как там.

## 1. Цель

pinch работает не только в X11, но и в Wayland-сеансах **GNOME, KDE Plasma, Hyprland и Sway (wlroots)** — все четыре в одном заходе. Основной потребитель — Arch Linux пользователя (отдельный компьютер, ручную проверку делает он сам); GNOME заодно покрывает будущие Ubuntu без X11.

Один бинарник: при запуске pinch сам определяет платформу и композитор. UX тот же: заморозка экрана → выделение → панель → рисование → буфер/файл.

**Выбранный подход (B):** снимок родным путём композитора, портал — запасной и единственный для GNOME:

| Окружение | Снимок | Оверлей |
|---|---|---|
| X11 (как сейчас) | `QScreen::grabWindow` | окно на каждый монитор (override-redirect) |
| Hyprland, Sway | протокол `wlr-screencopy-unstable-v1` (без D-Bus и файлов) | полноэкранное окно на каждый монитор |
| KDE Plasma | D-Bus `org.kde.KWin.ScreenShot2.CaptureWorkspace`, пиксели через pipe (без файлов) | то же |
| GNOME | `org.freedesktop.portal.Screenshot` (D-Bus + файл, который пишет портал) | то же |

Спайк не проводился по решению пользователя; непроверенные факты и защитное поведение — в разделе 9.

## 2. Что НЕ делаем

layer-shell (подход C — возможное продолжение, если полноэкранных окон окажется мало); `ext-image-copy-capture`; чёткий HiDPI (при масштабе ≠ 1 снимок приводится к логическим пикселям); Flatpak; глобальные хоткеи силами pinch (хоткей настраивается в самом окружении, как и в X11).

## 3. Выбор бэкенда снимка

Модуль `capturebackend.{h,cpp}`:

```cpp
enum class CaptureMethod { X11, Screencopy, KWin, Portal };
struct CaptureEnvironment {           // факты о сеансе, собранные при запуске
    QString platformName;              // QGuiApplication::platformName(): "xcb" | "wayland" | ...
    bool hasScreencopy = false;        // композитор предлагает zwlr_screencopy_manager_v1
    bool hasKWinScreenShot2 = false;   // на сессионной шине есть org.kde.KWin с интерфейсом ScreenShot2
    QString forced;                    // значение PINCH_CAPTURE (x11|screencopy|kwin|portal) или пусто
};
// Чистая функция: порядок попыток. xcb → {X11}; wayland* (wayland, wayland-egl, …) → screencopy (если есть), kwin (если есть), portal.
// PINCH_CAPTURE=… оставляет только указанный метод (для отладки и ручной проверки).
QVector<CaptureMethod> captureOrder(const CaptureEnvironment& env);
// Пробует методы по порядку; первый успешный возвращает Capture. Ошибки каждого метода копятся в *errors.
std::optional<Capture> captureScreens(const CaptureEnvironment& env, QStringList* errors);
```

Структура `Capture` (image, origin, screens) не меняется. Во всех бэкендах координаты — логические координаты рабочего стола из `QScreen::geometry()`; картинка каждого монитора масштабируется в свой логический прямоугольник (`composeScreens` уже это делает).

Успешный способ печатается одной строкой в stderr (`pinch: способ снимка — kwin`). Если Qt работает через `xcb`, а `WAYLAND_DISPLAY` задан (Xwayland в сеансе Wayland), в stderr идёт предупреждение: снимок способом `x11` может оказаться чёрным.

Если все методы не удались — `QMessageBox::critical` с перечнем ошибок (тексты — ключи `qtTrId`, таблицы `translations/pinch_{en,ru}.ts`, язык из системы, запасной — английский) и выход с кодом 1.

## 4. Бэкенды

### 4.1 X11 — без изменений (`captureAllScreens`).

### 4.2 Screencopy (Hyprland, Sway) — `screencopy.{h,cpp}`
- Собственное соединение `wl_display_connect(nullptr)` (не зависим от внутренностей Qt Wayland; работает на Qt 6.4 и новее).
- Реестр: `wl_output` (версия ≥ 4 ради события `name`), `wl_shm`, `zwlr_screencopy_manager_v1`. Описание протокола `wlr-screencopy-unstable-v1.xml` (MIT) лежит в `protocols/`; код генерирует `wayland-scanner` при сборке.
- Для каждого монитора: `capture_output(overlay_cursor = 0)`; по событию `buffer` создаём `wl_shm_pool` на `memfd_create("pinch-screencopy", MFD_CLOEXEC | MFD_ALLOW_SEALING)` нужного размера; `copy`; ждём `ready` или `failed` (таймаут 5 с на всё).
- Форматы: `XRGB8888`, `ARGB8888` → `QImage::Format_RGB32`; `XBGR8888`, `ABGR8888` → через `rgbSwapped()`; флаг `y_invert` → `mirrored(false, true)`. Другой формат → ошибка метода.
- Поворот монитора: кадр приходит в ориентации буфера выхода; `transform` из `wl_output.geometry` (все 8 значений `wl_output_transform`) применяется до склейки — `applyOutputTransform` (поворот на 90·k° по часовой стрелке, затем для `flipped_*` отражение по горизонтали, как в grim). Неизвестное значение → ошибка метода.
- Сопоставление с `QScreen`: по имени (`wl_output.name` == `QScreen::name()`); если имён нет — по порядку. Геометрия — из `QScreen::geometry()`. Затем `composeScreens`.
- Всё освобождается (munmap, close memfd, destroy объектов, `wl_display_disconnect`) при любом исходе.

### 4.3 KWin (KDE Plasma) — `kwincapture.{h,cpp}`
- Проверка наличия: интерфейс `org.kde.KWin.ScreenShot2` у сервиса `org.kde.KWin`, путь `/org/kde/KWin/ScreenShot2`.
- `pipe2(O_CLOEXEC)`; вызов `CaptureWorkspace(options, QDBusUnixFileDescriptor(writeEnd))`, options: `include-cursor=false`, `native-resolution=false`; сразу закрываем свой writeEnd; читаем readEnd до EOF (таймаут 5 с), ответ D-Bus — словарь `type` (ожидаем `raw`), `width`, `height`, `stride`, `format` (значение `QImage::Format`).
- Проверки: `stride*height` байт получено, формат из разрешённого списка (`RGB32`, `ARGB32`, `ARGB32_Premultiplied`, `RGBX8888`, `RGBA8888`); иначе ошибка метода. Результат → `Format_RGB32`, `origin` = левый верхний угол охватывающего прямоугольника мониторов, `screens` — из `QScreen::geometry()` в координатах изображения.
- KWin пускает к ScreenShot2 только программы, чей `.desktop` содержит `X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2` → добавляем ключ в `io.github.ufna.pinch.desktop`, а сам файл генерируем при установке с **абсолютным** `Exec=<prefix>/<bindir>/pinch` (чтобы KWin сопоставил процесс с файлом). Без установленного `.desktop` метод получит отказ — это штатная ошибка, дальше пробуется портал.

### 4.4 Портал (GNOME и запасной везде) — `portalcapture.{h,cpp}`
- Первым вызовом — `org.freedesktop.host.portal.Registry.Register("io.github.ufna.pinch", {})` у самого портала (сервис `org.freedesktop.portal.Desktop`, путь `/org/freedesktop/portal/desktop`, xdg-desktop-portal ≥ 1.19): идентификатор приложения (имя установленного `.desktop`) для хранения разрешения; ошибка (старый портал без интерфейса) — не ошибка снимка. Тот же идентификатор — `QGuiApplication::setDesktopFileName` (app_id окон Wayland).
- `org.freedesktop.portal.Screenshot.Screenshot("", {interactive: false, handle_token: "pinch<случайное>"})`; ждём сигнал `Response` у объекта запроса (локальный `QEventLoop`, таймаут 15 с). По тайм-ауту (и если вызов не дождался ответа) — `org.freedesktop.portal.Request.Close` на путях запроса, чтобы портал не записал файл со снимком позже.
- `response != 0` (отказ/отмена) → ошибка метода с понятным текстом: «Портал отказал в снимке экрана. В GNOME: Настройки → Приложения → pinch → разрешить снимки экрана».
- `uri` должен быть `file://`; файл читаем в память (`QImage::fromData`).
- **Удаление файла портала** (чтобы снимки не копились): только если `lstat` показывает обычный файл (не симлинк), владелец — текущий пользователь, и `mtime` не раньше момента запроса минус 2 с. Иначе файл не трогаем и пишем предупреждение в stderr.
- Портал отдаёт один снимок всего стола: `origin` и `screens` — из `QScreen::geometry()`, как в 4.3; если размер картинки не совпадает с охватывающим прямоугольником, картинка масштабируется в него. Результат приводится к `Format_RGB32` (PNG может прийти с альфой, палитрой и т. п.).

## 5. Оверлей: окно на каждый монитор

Wayland не позволяет одно окно на весь виртуальный стол и позиционирование окон, поэтому оверлей разделяется на:

- **`OverlayController`** (QObject) — всё состояние и логика нынешнего `Overlay`: снимок, выделение, документ, инструмент, стиль, текст, перетаскивания, клавиши, копирование/сохранение, сигналы `copyRequested`/`finished`, панель инструментов. Работает в координатах изображения. Обработчики получают уже переведённые в эти координаты события: `mousePress(QPoint imagePos, Qt::MouseButton, Qt::KeyboardModifiers)`, `mouseMove(...)`, `mouseRelease(...)`, `mouseDoubleClick(...)`, `wheel(int angleDeltaY)`, `key(QKeyEvent*)`; `paint(QPainter&, const QRect& imageRect)` рисует фрагмент кадра.
- **`ScreenView`** (QWidget) — одно окно на монитор: знает свой прямоугольник в координатах изображения, переводит события (`local + rect.topLeft()`) и отдаёт их контроллеру, в `paintEvent` вызывает `controller.paint(painter, rect)` со сдвигом `-rect.topLeft()`. Курсор задаёт контроллер через `ScreenView::setCursor` всех окон.
- **Показ.** X11: `Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint`, `setGeometry` по глобальной геометрии монитора. Wayland: `Qt::FramelessWindowHint`, `setScreen(screen)` + `showFullScreen()`. Клавиатура: активируется окно под курсором (или первое); события клавиш из любого окна идут в контроллер.
- **Выделение через границу мониторов.** Нажатие и перетаскивание продолжают приходить окну, где нажали (неявный захват указателя в X11 и Wayland); координаты за его пределами переводятся так же — выделение тянется на соседний монитор.
- **Панель инструментов** — дочерний виджет того `ScreenView`, в чьём прямоугольнике её разместил `placeToolbar`; при перемещении выделения на другой монитор панель переподвешивается (`setParent` + `show`).
- Мёртвых зон больше нет физически: окна только на реальных мониторах.
- Все существующие правила (раздел 6 базовой спецификации и доработки: карандаш по умолчанию, пресеты толщины, клавиши 1–5, клики по фону панели и т.д.) переезжают в контроллер без изменения поведения.

## 6. Буфер обмена

В Wayland выделение (selection) можно назначить только пока окно приложения в фокусе. Поэтому порядок при копировании меняется **для обеих платформ**: контроллер эмитит `copyRequested(image)` **до** скрытия окон; `main` кладёт картинку в буфер, затем контроллер скрывает окна. Дальше как раньше: процесс живёт, пока владеет буфером, и завершается на `QClipboard::changed` + `!ownsClipboard()`.

## 7. Модель угроз (обновление README)

- X11, Hyprland, Sway: снимок без D-Bus и без файлов (X11 / Wayland-протокол).
- KDE: клиентский вызов D-Bus к KWin; пиксели через pipe, без файлов.
- GNOME и запасной путь: клиентские вызовы D-Bus к порталу; **портал сам пишет файл со снимком** (место выбирает он); pinch читает его и удаляет по правилу из 4.4.
- Как и прежде: никаких своих D-Bus-сервисов, слушающих сокетов и сети; файлы pinch — только `0600` через `writeFileAtomic`.
- Процесс недампируемый (`PR_SET_DUMPABLE=0`), кроме самих вызовов D-Bus к KWin (`CaptureWorkspace`) и порталу (`Register`, `Screenshot`): службы опознают вызывающего через `/proc/<pid>/exe` и `/proc/<pid>/root`, что ядро запрещает для недампируемого процесса. На время вызова `ScopedDumpable` (`src/dumpable.{h,cpp}`) делает процесс дампируемым, затем возвращает запрет и проверяет `TracerPid` всех потоков; если подключился трассировщик — способ снимка прерывается с ошибкой. Если трассировщик подключён ещё до вызова, окно не открывается.

## 8. Сборка

- Новые зависимости: `wayland-client` (pkg-config) и `wayland-scanner` (сборка), `Qt6::DBus` (часть qtbase), плагин `qt6-wayland` во время работы.
  - Ubuntu 24.04: `libwayland-dev libwayland-bin qt6-wayland` (уже стоят на машине разработки).
  - Arch: `qt6-base qt6-wayland wayland cmake gcc noto-fonts`.
- Опция `PINCH_WAYLAND` (по умолчанию ON): при OFF — сборка только с X11 (без wayland-client), `captureOrder` не предлагает `Screencopy`.
- `protocols/wlr-screencopy-unstable-v1.xml` — вложенная копия с указанием источника и лицензии; генерация `wayland-scanner client-header` и `private-code` в каталог сборки.
- `data/io.github.ufna.pinch.desktop.in` → `io.github.ufna.pinch.desktop` через `configure_file` с `@CMAKE_INSTALL_FULL_BINDIR@/pinch`; при установке файл генерируется заново с настоящим префиксом. Все пути установки — `CMAKE_INSTALL_BINDIR` / `CMAKE_INSTALL_DATADIR` (бинарь и `Exec` не расходятся). Идентификатор приложения `PINCH_APP_ID` задан в CMake один раз.

## 9. Риски без спайка и защитное поведение

| Непроверенный факт | Поведение pinch |
|---|---|
| GNOME: первый неинтерактивный снимок может требовать разрешения, а диалог может не показаться, пока у приложения нет окна | Отказ портала → понятное сообщение, как разрешить (4.4); ручная проверка на GNOME обязательна |
| GNOME: куда портал пишет файл, есть ли вспышка/звук | Файл удаляется по безопасному правилу; вспышку принимаем |
| Hyprland: учитывает ли полноэкранное окно указанный монитор | Ручная проверка; если нет — отдельная задача на layer-shell (подход C) |
| KDE: сопоставление процесса с `.desktop` по `Exec` | Абсолютный путь в `Exec`; без установки — штатный откат на портал |
| Wayland: хватает ли фокуса для назначения буфера | Копирование до скрытия окон (раздел 6); ручная проверка во всех 4 окружениях |
| Имена `wl_output` и `QScreen::name()` совпадают | Совпадение по имени, иначе по порядку; ручная проверка на нескольких мониторах |

## 10. Тестирование

Автотесты (offscreen, без композиторов):
- `captureOrder` для всех сочетаний окружения и `PINCH_CAPTURE`.
- Преобразование сырых буферов: форматы screencopy (4 формата + `y_invert`), форматы KWin (разрешённые и запрещённый), проверка размера `stride*height`.
- Разбор ответа портала (код ответа, `uri`), правило безопасного удаления файла (обычный файл/симлинк/чужой mtime) на временном каталоге.
- Сопоставление мониторов по имени и по порядку.
- `OverlayController` + два `ScreenView` на синтетических прямоугольниках: все существующие сценарии test_overlay переносятся на контроллер; новые — выделение через границу двух видов, переподвешивание панели, клавиши из второго окна, копирование до скрытия.

Ручная проверка (пользователь, чек-лист в README): в каждом из GNOME / KDE / Hyprland / Sway на Арче — `PINCH_CAPTURE` не задан (автовыбор) и принудительно `portal`; один и несколько мониторов; вставка из буфера в другое приложение; сохранение; повторный запуск; X11 на Ubuntu — регрессия.

## 11. Готовность

Все автотесты зелёные в обеих сборках (обычной и `PINCH_SANITIZE`), сборка без предупреждений на Ubuntu (Qt 6.4) и по инструкции собирается на Arch (новые Qt/GCC, при необходимости `-DPINCH_WERROR=OFF`); README обновлён (зависимости, модель угроз по бэкендам, `PINCH_CAPTURE`, чек-лист Wayland); ручная проверка пройдена пользователем.
