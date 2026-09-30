# hot-screenshot — спецификация

Дата: 2026-09-30

## 1. Цель

Собственный скриншотер для Ubuntu по образцу Flameshot. Сценарий: горячая клавиша → «замороженный» экран → выделение области → рисование поверх → копирование в буфер обмена или сохранение в файл.

Мотивация — не доверять чужой большой кодовой базе (у Flameshot до 14.0.0 была CVE-2026-62294: запись во временный файл по предсказуемому пути с переходом по симлинкам). Весь код пишется с нуля и проверяется нами. Flameshot — образец UX, **его код не копируется** (чтобы не наследовать GPLv3 и не тащить непроверенное).

**Критерий успеха:** `Shift+Super+S` → выделил → нарисовал стрелку/рамку/текст → `Ctrl+C` → вставил в мессенджер. Кода столько, что его можно прочитать целиком (ориентир 2–3 тыс. строк без тестов).

## 2. Принципы безопасности

- Никакой сети.
- Никакого собственного IPC: приложение не регистрирует служб D-Bus, не открывает слушающих сокетов, не выходит в сеть, нет «Open With». Клиентские соединения — только с X-сервером и, возможно, с шиной AT-SPI/сессионной шиной через Qt. GTK-тема и менеджер сессии отключены в `main()`.
- Никаких временных файлов вне каталога назначения. Буфер обмена — прямо из памяти.
- Запись файлов только атомарно через временный файл в том же каталоге (`mkostemp`, `O_EXCL`), затем `renameat2`. Путь назначения никогда не открывается напрямую, поэтому симлинк на месте файла не может перенаправить запись.
- Сохранённые файлы имеют права `0600` (скриншоты бывают конфиденциальными).
- Сборка с hardening-флагами, тесты гоняются под ASan/UBSan.

## 3. Что НЕ делаем в v1

Wayland; HiDPI (масштаб ≠ 1); трей и фоновый демон; собственный перехват клавиш (`XGrabKey`); уведомления; выгрузка куда-либо; «закрепить на экране»; перемещение и редактирование уже нарисованных объектов; лупа; выбор окна кликом; форматы кроме PNG; конфигурируемые горячие клавиши.

## 4. Окружение

- Ubuntu 24.04, GNOME, **X11**, NVIDIA RTX 3070 Ti.
- Три монитора: `DP-0` 2560×1440 в (0,720), `DP-2` 1920×1080 в (2560,0), `DP-4` 1920×1080 в (2560,1080). Виртуальный рабочий стол 4480×2160 с «мёртвыми» зонами, не покрытыми ни одним монитором.
- Зависимости: `qt6-base-dev` (Qt 6.4.2: Core, Gui, Widgets, Test), cmake 3.28, g++ 13, glibc 2.39. Больше ничего.
- Домашний каталог на ext4 (`renameat2(RENAME_NOREPLACE)` поддерживается).

## 5. Архитектура

Разовый процесс, запускаемый ярлыком GNOME. Жизненный цикл:

1. `main` создаёт `QApplication` (`setQuitOnLastWindowClosed(false)`).
2. Берёт блокировку единственного экземпляра. Если занято — тихо выходит с кодом 0 (иначе второй запуск снял бы наш же оверлей).
3. Снимает все мониторы в одно изображение. Если не удалось — сообщение в stderr, код 1.
4. Загружает настройки (цвет, толщина), показывает оверлей.
5. Завершение:
   - **Отмена (Esc)** — выход, код 0.
   - **Сохранение** — запись файла, выход, код 0.
   - **Копирование** — оверлей закрывается, блокировка освобождается, изображение кладётся в `QClipboard`. Процесс остаётся жить невидимым, пока владеет буфером (так на X11 устроен буфер: данные отдаёт процесс-владелец). Как только `QClipboard::changed(Clipboard)` и `!ownsClipboard()` — выход. Так работает `xclip`, и это не зависит от наличия менеджера буфера.
6. При `copyRequested` и `finished` `main` сохраняет текущий стиль оверлея (`overlay.style()`) в настройки.

### Система координат

Все координаты внутри приложения — **координаты изображения**: (0,0) = левый верхний угол виртуального рабочего стола. Окно оверлея имеет геометрию виртуального стола, поэтому координаты виджета совпадают с координатами изображения. Геометрии мониторов переводятся в эту же систему при захвате. Аннотации хранятся в этих же координатах, поэтому при перемещении выделения они остаются на месте относительно экрана (как во Flameshot).

## 6. Модули

Всё, кроме `main.cpp`, собирается в статическую библиотеку `hs_core`, с которой линкуются приложение и тесты. Комментарии в коде — на русском, идентификаторы — на английском.

### `capture.{h,cpp}` — захват экрана
```cpp
struct ScreenShot { QImage image; QRect geometry; };     // один монитор, geometry в глобальных координатах X
struct Capture   { QImage image; QPoint origin; QVector<QRect> screens; }; // origin — глобальные координаты X угла изображения; screens — в координатах изображения
// Чистая функция: склеить снимки мониторов в одно изображение размером с их общий охватывающий прямоугольник.
// Мёртвые зоны заливаются чёрным. Возвращает также геометрии мониторов, сдвинутые в координаты изображения.
Capture composeScreens(const QVector<ScreenShot>& shots);
// Снимает каждый QScreen через screen->grabWindow(0) и вызывает composeScreens. nullopt, если экранов нет или снимок пустой.
std::optional<Capture> captureAllScreens();
```
Итоговое изображение — `QImage::Format_RGB32`.

### `instancelock.{h,cpp}` — один экземпляр
```cpp
class InstanceLock {
public:
    enum class Result { Acquired, Busy, Error };
    Result tryAcquire(const QString& path); // open(O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC, 0600) + flock(LOCK_EX|LOCK_NB)
    void release();                         // закрыть fd; идемпотентно
    ~InstanceLock();                        // release()
};
QString defaultLockPath(); // $XDG_RUNTIME_DIR/hot-screenshot.lock; пустая строка, если переменная не задана
```
`Busy` → выход с кодом 0. `Error` или пустой путь → предупреждение в stderr, работаем без блокировки.

### `geometry.{h,cpp}` — чистая геометрия (без виджетов)
```cpp
enum class Handle { None, Move, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
QRect  rectFromPoints(QPoint a, QPoint b);                       // нормализованный, обе точки включительно
Handle hitTestHandle(const QRect& sel, QPoint p, int tolerance); // углы важнее сторон; внутри → Move; снаружи → None
QRect  applyHandleDrag(const QRect& original, Handle h, QPoint delta, const QRect& bounds);
       // Move — сдвиг с прижатием к bounds без изменения размера; остальные — ресайз соответствующей стороны/угла,
       // результат нормализуется (можно «перетянуть» через противоположную сторону), обрезается по bounds, минимум 1×1
QPoint snapLine45(QPoint start, QPoint end);   // угол округляется до кратного 45°, длина проекции сохраняется
QPoint snapSquare(QPoint start, QPoint end);   // сторона = max(|dx|,|dy|), знаки dx/dy сохраняются
QPoint placeToolbar(const QRect& sel, QSize toolbar, const QVector<QRect>& screens, int margin = 8);
```
`placeToolbar`: монитор = тот, что содержит центр выделения; иначе с наибольшим пересечением с выделением; иначе первый. Кандидаты по порядку: под выделением (`y = sel.bottom()+margin`), над ним (`y = sel.top()-margin-h`), внутри у нижнего края (`y = sel.bottom()-margin-h`). Берётся первый, который целиком помещается в монитор по вертикали. `x = sel.left()`, прижатый к `[screen.left(), screen.right()-w+1]`. Если не подошёл ни один кандидат, панель прижимается к нижнему краю монитора.

### `annotation.h` — модель аннотации
```cpp
enum class Tool { None, Pen, Marker, Line, Arrow, Rect, Ellipse, Text, Counter, Pixelate };
struct Style { QColor color; int thickness; };   // thickness 1..40
struct Annotation {
    Tool tool; Style style;
    QVector<QPoint> points; // Pen/Marker — путь; Line/Arrow/Rect/Ellipse/Pixelate — [start, end]; Text/Counter — [точка]
    QString text;           // только Text, строки через '\n'
};
```
Номер у `Counter` не хранится: он равен порядковому номеру среди `Counter` в списке (1, 2, 3…), функция `counterNumbers(annotations)` в `annotation.h`. После undo нумерация пересчитывается сама.

### `document.{h,cpp}` — список аннотаций и undo/redo
```cpp
class Document {
public:
    void add(Annotation a);   // очищает redo
    bool undo(); bool redo(); // false, если нечего
    bool canUndo() const; bool canRedo() const;
    const QVector<Annotation>& annotations() const;
    int nextCounterNumber() const;
};
```
Undo/redo действует только на добавление аннотаций. Изменения выделения в историю не попадают.

### `renderer.{h,cpp}` — рисование (единый путь для превью и результата)
```cpp
// Рисует аннотацию любым QPainter'ом в координатах изображения (для превью на виджете и внутри paintAnnotation).
// Pixelate здесь не поддерживается — для неё рисуется пунктирная рамка (превью).
void drawAnnotation(QPainter& p, const Annotation& a, int counterNumber);
// Рисует одну аннотацию на canvas; offset — координаты изображения, соответствующие (0,0) canvas.
// Для Pixelate читает и заменяет пиксели canvas; для остальных открывает собственный QPainter,
// сдвигает на -offset и вызывает drawAnnotation. QPainter закрывается до выхода из функции.
void paintAnnotation(QImage& canvas, QPoint offset, const Annotation& a, int counterNumber);
// Итоговое изображение: base.copy(selection) в Format_RGB32, затем все аннотации по порядку.
// Всё, что выходит за выделение, обрезается.
QImage render(const QImage& base, const QRect& selection, const QVector<Annotation>& annotations);
```
Поведение инструментов (сглаживание включено везде, кроме Pixelate):

| Инструмент | Рисование |
|---|---|
| Pen | путь, перо шириной `thickness`, круглые концы и стыки; одна точка — точка |
| Marker | путь одним `drawPath` (самопересечения не темнеют), ширина `4×thickness`, цвет с альфой 100, плоские концы |
| Line | отрезок шириной `thickness`, круглые концы |
| Arrow | отрезок + залитый треугольник; длина наконечника `L = max(10, 4×thickness)`, полуширина `L/2`; отрезок заканчивается у основания наконечника |
| Rect | контур прямоугольника шириной `thickness` |
| Ellipse | контур эллипса, вписанного в прямоугольник `[start, end]` |
| Text | шрифт по умолчанию, `pixelSize = 10 + 3×thickness`, цвет стиля, без фона; левый верхний угол текста — точка; многострочный |
| Counter | залитый круг диаметром `16 + 4×thickness` с центром в точке; номер жирным `pixelSize = 0.55×диаметра`; цвет цифры белый, если `color.lightness() < 140`, иначе чёрный |
| Pixelate | область `[start, end]` ∩ canvas: уменьшить до `ceil(w/b)×ceil(h/b)` с `SmoothTransformation` (усреднение), увеличить обратно с `FastTransformation`; блок `b = max(12, 4×thickness)`; пикселизуется всё, что уже нарисовано под областью |

Pixelate необратима при таком размере блока, кроме атак на известный шрифт (Depix). Для паролей надёжнее не захватывать их вовсе. Это ограничение фиксируем в README.

### `output.{h,cpp}` — буфер и файлы
```cpp
QByteArray encodePng(const QImage& image);   // в памяти, через QBuffer
struct WriteResult { enum Status { Ok, Exists, Error } status; QString error; };
enum class WriteMode { NoReplace, Replace };
WriteResult writeFileAtomic(const QString& path, const QByteArray& data, WriteMode mode);
QString quickSaveFileName(const QDateTime& now, int attempt);  // "2026-09-30_14-05-33.png", при attempt>0 — "..._1.png"
WriteResult quickSave(const QImage& image, const QString& dir, const QDateTime& now, QString* savedPath);
QString screenshotsDir();                     // QStandardPaths::PicturesLocation + "/Screenshots", mkpath
void copyToClipboard(const QImage& image);    // QGuiApplication::clipboard()->setImage
```
`writeFileAtomic`:
1. `mkostemp("<каталог>/.hot-screenshot-XXXXXX", O_CLOEXEC)` — создаётся с `O_EXCL`, права `0600`.
2. Записать все байты (цикл по частичной записи и `EINTR`), `fsync`, `close`.
3. `NoReplace` → `renameat2(..., RENAME_NOREPLACE)`: при `EEXIST` — `Exists`. `Replace` → `rename()`: подменяется запись в каталоге; если там был симлинк, заменяется сам симлинк, а его цель не трогается.
4. При любой ошибке временный файл удаляется. После вызова в каталоге не остаётся `.hot-screenshot-*`.

`quickSave`: пробует `attempt = 0..99` в режиме `NoReplace`, пока не получит не-`Exists`.

### `settings.{h,cpp}` — настройки
```cpp
struct Settings {
    QColor color = QColor("#E53935"); int thickness = 4;
    static Settings load();  // QSettings("hot-screenshot","hot-screenshot"); невалидное → по умолчанию, thickness прижимается к 1..40
    void save() const;
};
```

### `toolbar.{h,cpp}` — панель инструментов
Дочерний `QWidget` оверлея, три ряда `QToolButton` (инструменты; цвета и толщина; история и вывод). **Все кнопки `Qt::NoFocus`**, чтобы клавиатура всегда оставалась у оверлея. Подписи — Unicode-символы, в подсказках — название и клавиша.

| Группа | Кнопки |
|---|---|
| Инструменты (взаимоисключающие, повторное нажатие активной → `Tool::None`) | ✎ Карандаш (P), ▮ Маркер (M), ╱ Линия (L), ↗ Стрелка (A), ▭ Прямоугольник (R), ◯ Эллипс (E), T Текст (T), ① Номерок (N), ▦ Пикселизация (B) |
| Цвета | 8 образцов: `#E53935` `#FB8C00` `#FDD835` `#43A047` `#1E88E5` `#8E24AA` `#000000` `#FFFFFF`; активный выделен рамкой |
| Толщина | 5 кнопок-пресетов (2, 4, 8, 14, 24 px; клавиши 1–5), ползунок 1–40 и надпись «4px»; колесо мыши тоже меняет |
| История | ↶ Отменить (Ctrl+Z), ↷ Повторить (Ctrl+Shift+Z) — неактивны, когда нечего |
| Вывод | ⧉ Копировать (Ctrl+C), ↓ Быстро сохранить (Ctrl+S), … Сохранить как (Ctrl+Shift+S), ✕ Закрыть (Esc) |

Интерфейс: сигналы `toolChosen(Tool)`, `colorChosen(QColor)`, `undoRequested()`, `redoRequested()`, `copyRequested()`, `quickSaveRequested()`, `saveAsRequested()`, `closeRequested()`; слоты `setTool`, `setColor`, `setThickness`, `setUndoRedoEnabled(bool, bool)`.

### `overlay.{h,cpp}` — окно выделения и рисования
```cpp
class Overlay : public QWidget {
    Q_OBJECT
public:
    Overlay(Capture capture, Settings settings, QString saveDir); // saveDir — каталог быстрого сохранения (main передаёт screenshotsDir())
    void start();                       // показать и захватить клавиатуру
    QRect selection() const;            // пустой, если не выбрано
    Tool tool() const; Style style() const;
    const Document& document() const;
signals:
    void copyRequested(QImage result);  // main кладёт в буфер и ждёт потери владения
    void finished();                    // сохранено или отменено → main завершает процесс
};
```
Окно X11: `Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint`, геометрия = `QRect(capture.origin, capture.image.size())`, `setMouseTracking(true)`. `start()` = `show(); raise(); activateWindow(); grabKeyboard();`. Работает ли фокус клавиатуры у override-redirect окна на реальном GNOME — проверяется вручную (раздел 9).

**Отрисовка (`paintEvent`)**, по порядку:
1. Снимок целиком.
2. Затемнение `rgba(0,0,0,120)` вне выделения.
3. Кэш `render(base, selection, annotations)` в позиции выделения. Кэш сбрасывается при изменении выделения или документа.
4. Незавершённая аннотация — `drawAnnotation` на `QPainter` виджета с клипом по выделению (для Pixelate это пунктирная рамка).
5. Рамка выделения 1px `#3D8BFD` и 8 квадратных маркеров 8×8.
6. Надпись с размером «W×H» над левым верхним углом (или внутри, если сверху нет места).
7. Текстовый курсор, если идёт ввод текста.

До выделения по центру монитора под курсором выводится подсказка: «Выделите область мышью · клик — весь монитор · Ctrl+A — все мониторы · Esc — отмена».

**Состояния:**
- **Selecting** — выделения нет. ЛКМ-перетаскивание рисует рамку. Отпускание: если сдвиг < 3 px (манхэттенский) — выделяется монитор под курсором; иначе `rectFromPoints`. После выделения появляется панель (`placeToolbar`).
- **Editing** — выделение есть, текущий инструмент `Tool::None` или рисующий. При первом непустом выделении за время жизни оверлея (отпускание рамки, клик по монитору, Ctrl+A), если пользователь ещё не выбирал инструмент, через `setTool` включается `Tool::Pen` (флаг `m_defaultToolApplied`; любой явный выбор инструмента тоже ставит флаг). Во время протяжки рамки инструмент не меняется. После явного выбора `Tool::None` (V) новое выделение оставляет `None`. Чтобы начать новое выделение, нажмите V и перетащите вне рамки.
  - Нажатие на маркере (`hitTestHandle`, допуск 6px) — ресайз при любом инструменте.
  - Нажатие внутри выделения: `Tool::None` → перемещение выделения; рисующий инструмент → рисование.
  - Нажатие вне выделения: `Tool::None` → новое выделение (аннотации сохраняются); рисующий инструмент → ничего.
- **TextEditing** (подсостояние Editing) — после клика инструментом Text внутри выделения.
  - Печатные символы (`QKeyEvent::text()` без Ctrl) дописываются, Backspace удаляет последний, Enter — перевод строки.
  - Клик в другом месте, смена инструмента, Esc или любое сочетание с Ctrl сначала завершают текст (пустой отбрасывается), затем выполняется действие. Esc при этом только завершает текст и не закрывает оверлей.

**Мышь для рисующих инструментов:**
- Pen/Marker: точки добавляются при сдвиге ≥ 1px; Marker из одной точки не добавляется.
- Line/Arrow/Rect/Ellipse/Pixelate: `[start, текущая]`. Shift привязывает: для Line/Arrow — `snapLine45`, для Rect/Ellipse/Pixelate — `snapSquare`. Фигура с нулевой длиной или шириной/высотой < 2 не добавляется.
- Counter: клик добавляет номерок.

Колесо мыши меняет толщину ±1 (1..40).

**Клавиши:**

| Клавиша | Действие |
|---|---|
| Esc | завершить текст, иначе закрыть без результата |
| Ctrl+C, Enter (не в тексте) | копировать в буфер |
| Ctrl+S | быстро сохранить в `~/Pictures/Screenshots` |
| Ctrl+Shift+S | сохранить через диалог |
| Ctrl+Z / Ctrl+Shift+Z, Ctrl+Y | отменить / повторить |
| Ctrl+A | выделить все мониторы (охватывающий прямоугольник) |
| P M L A R E T N B | выбор инструмента (без модификаторов, не в тексте, только при наличии выделения) |

Буквенные клавиши и сочетания работают в любой раскладке: символ нелатинской раскладки переводится в латинскую букву по X11-скан-коду физической клавиши (`keys.{h,cpp}`, `layoutIndependentKey`). Вводимый текст берётся из `QKeyEvent::text()` и не зависит от этого перевода.
| V | `Tool::None` (перемещение/ресайз выделения), те же условия |

Действия вывода без выделения игнорируются.

**Курсоры:** Selecting → крест; над маркерами → соответствующий ресайз; внутри при `Tool::None` → `SizeAllCursor`; Text → `IBeamCursor`; прочие инструменты → крест.

**Диалоги.** Override-redirect окно всегда выше обычных, поэтому перед любым диалогом (сохранение, ошибка) оверлей делает `releaseKeyboard(); hide();`, а если нужно вернуться — `start()`.
- Сохранить как: собственный `QFileDialog` Qt (`DontUseNativeDialog`, `setDefaultSuffix("png")` — суффикс добавляется до вопроса о перезаписи), стартовый путь `saveDir/<quickSaveFileName(now,0)>`, фильтр `PNG (*.png)`. Подтверждение перезаписи — средствами диалога, запись в режиме `Replace`. Отмена диалога → вернуться в оверлей.
- Ошибка записи (оба вида сохранения) → `QMessageBox::critical` с текстом ошибки, затем возврат в оверлей.
- Успешное сохранение → путь в stdout, `finished()`.

### `main.cpp`
`QCommandLineParser` с `--help` и `--version`, затем порядок из раздела 5. При `copyRequested`: освободить блокировку, `copyToClipboard`, закрыть оверлей, подписаться на `QClipboard::changed`.

## 7. Сборка

- CMake ≥ 3.16, C++17, `CMAKE_AUTOMOC ON`, `find_package(Qt6 REQUIRED COMPONENTS Widgets Test)`.
- Цели: `hs_core` (статическая библиотека), `hot-screenshot` (приложение), тесты `test_*` через `add_test`.
- Предупреждения для наших целей: `-Wall -Wextra -Wpedantic -Werror`.
- Hardening:
  - компиляция: `-O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection -fcf-protection`, PIE;
  - линковка: `-Wl,-z,relro,-z,now,-z,noexecstack`.
- Опция `HS_SANITIZE=ON` → `-fsanitize=address,undefined -fno-omit-frame-pointer` (без `_FORTIFY_SOURCE`).
- Тесты запускаются с `QT_QPA_PLATFORM=offscreen` (задаётся свойством `ENVIRONMENT` у `add_test`).
- `install(TARGETS hot-screenshot DESTINATION bin)`; префикс задаётся при установке (раздел 10).

Структура:
```
CMakeLists.txt  README.md
src/    main.cpp capture.* instancelock.* geometry.* keys.* annotation.h document.* renderer.* output.* settings.* toolbar.* overlay.*
tests/  CMakeLists.txt test_capture.cpp test_instancelock.cpp test_geometry.cpp test_document.cpp
        test_renderer.cpp test_output.cpp test_settings.cpp test_toolbar.cpp test_keys.cpp test_overlay.cpp
scripts/set-gnome-shortcut.sh
docs/superpowers/{specs,plans}/
```

## 8. Автотесты (Qt Test, `ctest`)

- **capture** — склейка синтетических снимков: трёх мониторов в нашей раскладке, а также с отрицательным началом координат (монитор слева от основного, x = −1920). Проверить размеры, пиксели в каждой зоне, чёрный цвет в мёртвых зонах и сдвинутые геометрии.
- **instancelock** — второй `InstanceLock` на тот же путь → `Busy`; после `release()` → `Acquired`; симлинк на месте файла → `Error` (`O_NOFOLLOW`).
- **geometry** — `rectFromPoints` в любом направлении; приоритет углов в `hitTestHandle`; ресайз за каждую ручку, перетягивание через противоположную сторону, прижатие к границам; `snapLine45` и `snapSquare` во всех квадрантах; `placeToolbar`: под, над, внутри, прижатие по x, выбор монитора.
- **document** — add/undo/redo, очистка redo при add, `nextCounterNumber` после undo.
- **renderer** — `render` обрезает по выделению и учитывает смещение; Rect рисует цвет на контуре и не трогает середину; Pixelate делает блоки однотонными и пикселизует ранее нарисованное; нумерация Counter; Marker полупрозрачный.
- **output** — PNG декодируется обратно в то же изображение; `NoReplace` на существующем файле → `Exists`, файл не изменён; `NoReplace` на висячем симлинке → `Exists`, цель не создана; `Replace` на симлинке → на месте симлинка обычный файл, цель не тронута; права `0600`; нет мусора `.hot-screenshot-*`; `quickSave` при коллизии даёт `_1`.
- **settings** — круговое сохранение и загрузка во временном `XDG_CONFIG_HOME`; невалидные значения → по умолчанию или прижатые.
- **overlay** (offscreen, синтетический `Capture`) — выделение перетаскиванием и кликом (весь монитор); перемещение и ресайз; клавиши инструментов; рисование Rect даёт аннотацию; Esc в тексте не закрывает, второй Esc закрывает (`finished`); `Ctrl+C` эмитит `copyRequested` с изображением размера выделения; Ctrl+Z/Ctrl+Shift+Z.

Весь набор должен проходить в обычной сборке и в сборке `HS_SANITIZE=ON`.

## 9. Ручная проверка (на реальном X11)

1. Запуск из терминала: оверлей на всех трёх мониторах, снимок совпадает с экраном, панели GNOME перекрыты.
2. Выделение через границу мониторов; клик выделяет монитор; Ctrl+A выделяет всё.
3. Клавиатура работает сразу (инструменты, Esc) без клика по окну.
4. Все 9 инструментов, цвета, колесо, Shift-привязка, undo/redo.
5. Текст на русском и английском.
6. Ctrl+C → вставка в Telegram/браузер/GIMP работает. Процесс висит, пока в буфер не скопировано что-то другое, затем завершается (`pgrep hot-screenshot`).
7. Ctrl+S → файл в `~/Pictures/Screenshots` с правами `0600`; Ctrl+Shift+S → диалог виден поверх, отмена возвращает в оверлей.
8. Повторное нажатие хоткея при открытом оверлее ничего не делает.
9. От нажатия до оверлея субъективно ≤ 0,3 с.
10. Сетевая активность: запуск из терминала под `HS_ALLOW_TRACE=1 strace -f -e trace=connect hot-screenshot` (без `1` процесс недампируем и `strace` не читает строки; `strace -p` при `ptrace_scope=1` требует `sudo`, а root подключается независимо от `HS_ALLOW_TRACE`) — только сокет X11 и не более сессионной шины / шины AT-SPI.

## 10. Установка и горячая клавиша

- `cmake --install build --prefix ~/.local` → `~/.local/bin/hot-screenshot`.
- `scripts/set-gnome-shortcut.sh [путь_к_бинарю]` переназначает существующий ярлык `custom0` («gnome-screenshot», `Shift+Super+S`) на hot-screenshot: имя «hot-screenshot», команда — абсолютный путь. Перед изменением скрипт печатает текущие значения, чтобы можно было откатиться.
- **Скрипт запускает только пользователь.** Агенты его не выполняют и не ставят пакеты через sudo.

## 11. Готовность v1

- Все автотесты зелёные в обеих сборках, сборка без предупреждений.
- Ручная проверка из раздела 9 пройдена.
- README: сборка, установка, горячие клавиши, модель угроз и ограничения (X11 only, Pixelate/Depix, права 0600).
