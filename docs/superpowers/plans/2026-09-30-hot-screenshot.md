# hot-screenshot v1 — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Скриншотер для Ubuntu/X11: горячая клавиша → выделение → рисование → буфер обмена или файл.

**Architecture:** Разовый процесс на C++/Qt6, запускаемый ярлыком GNOME. Вся логика, кроме `main.cpp`, живёт в статической библиотеке `hs_core` из небольших модулей с чистыми функциями (геометрия, рендер, запись файлов), покрытых Qt Test. Виджеты (`Toolbar`, `Overlay`) тестируются на платформе `offscreen` синтетическими событиями.

**Tech Stack:** C++17, Qt 6.4.2 (Core/Gui/Widgets/Test), CMake ≥ 3.16, g++ 13, glibc 2.39, Linux X11.

**Spec:** `docs/superpowers/specs/2026-09-30-hot-screenshot-design.md` — читать вместе с планом.

## Global Constraints

- Платформа: Ubuntu 24.04, X11, Qt 6.4.2 из `qt6-base-dev`. Других зависимостей нет.
- Никакой сети, D-Bus, сокетов, IPC, временных файлов вне каталога назначения.
- Код Flameshot не копируется.
- Флаги `-Wall -Wextra -Wpedantic -Werror` — сборка без предупреждений.
- Комментарии в коде — на русском, идентификаторы — на английском, все пользовательские тексты — ключи `qtTrId` в таблицах `translations/pinch_{en,ru}.ts` (язык из системы, запасной — английский).
- Все координаты внутри приложения — координаты изображения (0,0 = левый верхний угол виртуального стола).
- Сохранённые файлы — права `0600`, только через `writeFileAtomic`.
- Агенты **не** выполняют `sudo`, **не** меняют `gsettings`, **не** запускают `hot-screenshot` без `--help`/`--version` (оверлей закроет экран пользователя).
- Проверка каждой задачи — в обеих сборках:
  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j && ctest --test-dir build --output-on-failure
  cmake -S . -B build-asan -DHS_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
  ```
- Коммит в конце каждой задачи; сообщение заканчивается строкой `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Русская раскладка.** Клавиши инструментов (P, M, …) и Ctrl+C/S/Z должны работать при активной кириллической раскладке — Задача 9 (`test_keys`, `cyrillicLayoutHotkeys`).
2. **Ввод текста.** Буквы, совпадающие с клавишами инструментов, и кириллица попадают в текст; Esc завершает текст, а не выбрасывает скриншот — Задача 10 (`textLettersDontSwitchTool`, `textCyrillic`, `textEscapeKeepsOverlay`).
3. **Файл или симлинк на месте сохранения.** Существующий файл не перезаписывается быстрым сохранением, запись никогда не идёт «сквозь» симлинк — Задача 6.
4. **Мёртвые зоны виртуального стола.** Клик в зоне без монитора ничего не выделяет; панель инструментов всегда целиком на видимом мониторе — Задача 1 (`placeToolbarDeadZone`), Задача 9 (`clickInDeadZoneSelectsNothing`).
5. **Выделение за краем изображения.** Перетаскивание за пределы окна и перемещение выделения прижимаются к границам — Задача 1 (`applyHandleDrag*`), Задача 9 (`dragBeyondEdgeClamps`).

Не покрывается автотестами, проверяется вручную (Задача 11): жизнь процесса-владельца буфера обмена после Ctrl+C, фокус клавиатуры у override-redirect окна, диалог поверх экрана.

## Файлы

| Файл | Ответственность |
|---|---|
| `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/lsan.supp` | сборка, флаги, тесты |
| `src/geometry.{h,cpp}` | выделение, ручки, привязка к 45°/квадрату, место панели |
| `src/capture.{h,cpp}` | снимок и склейка мониторов |
| `src/instancelock.{h,cpp}` | блокировка единственного экземпляра |
| `src/annotation.h` | модель аннотации, нумерация номерков |
| `src/document.{h,cpp}` | список аннотаций, undo/redo |
| `src/renderer.{h,cpp}` | рисование аннотаций, итоговое изображение |
| `src/output.{h,cpp}` | PNG, атомарная запись, быстрое сохранение, буфер |
| `src/settings.{h,cpp}` | цвет и толщина между запусками |
| `src/toolbar.{h,cpp}` | панель кнопок |
| `src/keys.{h,cpp}` | клавиши независимо от раскладки |
| `src/overlay.{h,cpp}` | окно: выделение, рисование, вывод |
| `src/main.cpp` | жизненный цикл процесса |
| `scripts/set-gnome-shortcut.sh`, `README.md` | установка и документация |

---

### Task 1: Каркас сборки и геометрия

**Files:**
- Create: `CMakeLists.txt`, `.gitignore`, `tests/CMakeLists.txt`, `tests/lsan.supp`, `src/geometry.h`, `src/geometry.cpp`
- Test: `tests/test_geometry.cpp`

**Interfaces:**
- Consumes: —
- Produces: цель `hs_core` (статическая библиотека, `PUBLIC` include `src/`, линкует `Qt6::Widgets` и `hs_options`), функция CMake `hs_add_test(name)`; из `geometry.h`:
  `enum class Handle { None, Move, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };`
  `QRect rectFromPoints(QPoint a, QPoint b);`
  `Handle hitTestHandle(const QRect& sel, QPoint p, int tolerance);`
  `QRect applyHandleDrag(const QRect& original, Handle h, QPoint delta, const QRect& bounds);`
  `QPoint snapLine45(QPoint start, QPoint end);`
  `QPoint snapSquare(QPoint start, QPoint end);`
  `QPoint placeToolbar(const QRect& sel, QSize toolbar, const QVector<QRect>& screens, int margin = 8);`

- [ ] **Step 1: Файлы сборки**

`CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
project(hot-screenshot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(CheckPIESupported)
check_pie_supported()

option(HS_SANITIZE "Собрать с AddressSanitizer и UndefinedBehaviorSanitizer" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Test)

# Общие флаги для всех наших целей: предупреждения как ошибки и hardening.
add_library(hs_options INTERFACE)
target_compile_options(hs_options INTERFACE
    -Wall -Wextra -Wpedantic -Werror
    -fstack-protector-strong -fstack-clash-protection -fcf-protection)
target_link_options(hs_options INTERFACE
    -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
if(HS_SANITIZE)
    target_compile_options(hs_options INTERFACE
        -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -U_FORTIFY_SOURCE)
    target_link_options(hs_options INTERFACE -fsanitize=address,undefined)
else()
    # _FORTIFY_SOURCE требует оптимизации; Ubuntu уже задаёт его сама, поэтому сначала снимаем.
    target_compile_options(hs_options INTERFACE
        "$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=3>")
endif()

add_library(hs_core STATIC
    src/geometry.h src/geometry.cpp
)
target_include_directories(hs_core PUBLIC src)
target_link_libraries(hs_core PUBLIC Qt6::Widgets hs_options)

enable_testing()
add_subdirectory(tests)
```

`tests/CMakeLists.txt`:
```cmake
# Один исполняемый файл на набор тестов; все тесты идут без экрана (offscreen).
function(hs_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE hs_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set(env "QT_QPA_PLATFORM=offscreen")
    if(HS_SANITIZE)
        list(APPEND env "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=0")
    endif()
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
endfunction()

hs_add_test(test_geometry)
```

`tests/lsan.supp`:
```
# Утечки внутри системных библиотек, которые мы не контролируем. Кадры из src/ здесь не подавлять.
leak:libfontconfig
```

`.gitignore`:
```
build*/
```

- [ ] **Step 2: Написать падающий тест** — `tests/test_geometry.cpp`:

```cpp
#include <QtTest>

#include "geometry.h"

namespace {
// Раскладка мониторов пользователя в координатах изображения.
const QVector<QRect> kScreens = {
    QRect(0, 720, 2560, 1440),
    QRect(2560, 0, 1920, 1080),
    QRect(2560, 1080, 1920, 1080),
};
const QSize kToolbar(700, 32);
const QRect kBounds(0, 0, 1000, 800);
const QRect kSel(100, 100, 200, 100); // right = 299, bottom = 199
}

class TestGeometry : public QObject {
    Q_OBJECT
private slots:
    void rectFromPointsNormalizes()
    {
        QCOMPARE(rectFromPoints({10, 20}, {5, 8}), QRect(5, 8, 6, 13));
        QCOMPARE(rectFromPoints({5, 8}, {10, 20}), QRect(5, 8, 6, 13));
        QCOMPARE(rectFromPoints({3, 3}, {3, 3}), QRect(3, 3, 1, 1));
    }

    void hitTestCornersEdgesInside()
    {
        QCOMPARE(hitTestHandle(kSel, {100, 100}, 6), Handle::TopLeft);
        QCOMPARE(hitTestHandle(kSel, {299, 100}, 6), Handle::TopRight);
        QCOMPARE(hitTestHandle(kSel, {299, 199}, 6), Handle::BottomRight);
        QCOMPARE(hitTestHandle(kSel, {100, 199}, 6), Handle::BottomLeft);
        QCOMPARE(hitTestHandle(kSel, {200, 100}, 6), Handle::Top);
        QCOMPARE(hitTestHandle(kSel, {200, 199}, 6), Handle::Bottom);
        QCOMPARE(hitTestHandle(kSel, {100, 150}, 6), Handle::Left);
        QCOMPARE(hitTestHandle(kSel, {299, 150}, 6), Handle::Right);
        QCOMPARE(hitTestHandle(kSel, {200, 150}, 6), Handle::Move);
    }

    void hitTestTolerance()
    {
        QCOMPARE(hitTestHandle(kSel, {95, 150}, 6), Handle::Left);
        QCOMPARE(hitTestHandle(kSel, {93, 150}, 6), Handle::None);
        QCOMPARE(hitTestHandle(kSel, {304, 203}, 6), Handle::BottomRight);
        QCOMPARE(hitTestHandle(kSel, {50, 50}, 6), Handle::None);
        QCOMPARE(hitTestHandle(QRect(), {0, 0}, 6), Handle::None);
    }

    void applyHandleDragMove()
    {
        QCOMPARE(applyHandleDrag(kSel, Handle::Move, {50, 30}, kBounds), QRect(150, 130, 200, 100));
    }

    void applyHandleDragMoveClamps()
    {
        QCOMPARE(applyHandleDrag(kSel, Handle::Move, {900, 0}, kBounds), QRect(800, 100, 200, 100));
        QCOMPARE(applyHandleDrag(kSel, Handle::Move, {-500, -500}, kBounds), QRect(0, 0, 200, 100));
    }

    void applyHandleDragResize()
    {
        QCOMPARE(applyHandleDrag(kSel, Handle::BottomRight, {10, 20}, kBounds), QRect(100, 100, 210, 120));
        QCOMPARE(applyHandleDrag(kSel, Handle::TopLeft, {-10, -20}, kBounds), QRect(90, 80, 210, 120));
        QCOMPARE(applyHandleDrag(kSel, Handle::Top, {0, 30}, kBounds), QRect(100, 130, 200, 70));
        QCOMPARE(applyHandleDrag(kSel, Handle::None, {10, 10}, kBounds), kSel);
    }

    void applyHandleDragCrossesOver()
    {
        // Левая сторона перетянута правее правой — прямоугольник нормализуется.
        QCOMPARE(applyHandleDrag(kSel, Handle::Left, {250, 0}, kBounds), QRect(299, 100, 52, 100));
    }

    void applyHandleDragResizeClamps()
    {
        QCOMPARE(applyHandleDrag(kSel, Handle::Right, {2000, 0}, kBounds), QRect(100, 100, 900, 100));
    }

    void snapLine45Quadrants()
    {
        QCOMPARE(snapLine45({0, 0}, {10, 1}), QPoint(10, 0));
        QCOMPARE(snapLine45({0, 0}, {1, 10}), QPoint(0, 10));
        QCOMPARE(snapLine45({0, 0}, {10, 8}), QPoint(9, 9));
        QCOMPARE(snapLine45({0, 0}, {-10, 8}), QPoint(-9, 9));
        QCOMPARE(snapLine45({0, 0}, {-10, -1}), QPoint(-10, 0));
        QCOMPARE(snapLine45({0, 0}, {0, 0}), QPoint(0, 0));
        QCOMPARE(snapLine45({100, 100}, {110, 108}), QPoint(109, 109));
    }

    void snapSquareQuadrants()
    {
        QCOMPARE(snapSquare({0, 0}, {10, 3}), QPoint(10, 10));
        QCOMPARE(snapSquare({0, 0}, {-3, 10}), QPoint(-10, 10));
        QCOMPARE(snapSquare({0, 0}, {-5, -7}), QPoint(-7, -7));
        QCOMPARE(snapSquare({5, 5}, {5, 5}), QPoint(5, 5));
    }

    void placeToolbarBelow()
    {
        QCOMPARE(placeToolbar(QRect(100, 800, 400, 400), kToolbar, kScreens), QPoint(100, 1207));
    }

    void placeToolbarAbove()
    {
        QCOMPARE(placeToolbar(QRect(100, 1800, 400, 350), kToolbar, kScreens), QPoint(100, 1760));
    }

    void placeToolbarInside()
    {
        QCOMPARE(placeToolbar(QRect(0, 720, 2560, 1440), kToolbar, kScreens), QPoint(0, 2119));
    }

    void placeToolbarClampsX()
    {
        QCOMPARE(placeToolbar(QRect(4000, 100, 300, 300), kToolbar, kScreens), QPoint(3780, 407));
    }

    void placeToolbarDeadZone()
    {
        // Центр выделения в мёртвой зоне — берётся монитор с наибольшим пересечением (DP-2).
        QCOMPARE(placeToolbar(QRect(2000, 100, 1000, 400), kToolbar, kScreens), QPoint(2560, 507));
    }

    void placeToolbarNoScreens()
    {
        QCOMPARE(placeToolbar(QRect(10, 10, 100, 100), kToolbar, {}), QPoint(10, 117));
    }
};

QTEST_MAIN(TestGeometry)
#include "test_geometry.moc"
```

- [ ] **Step 3: Убедиться, что падает**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL — нет `src/geometry.cpp` / `geometry.h`.

- [ ] **Step 4: Реализация** — `src/geometry.h`:

```cpp
#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>

// Чистая геометрия выделения и панели инструментов. Все координаты — координаты изображения.

enum class Handle { None, Move, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

// Нормализованный прямоугольник; обе точки входят в него включительно.
QRect rectFromPoints(QPoint a, QPoint b);

// Что под точкой p: угол, сторона (в пределах tolerance), внутренность (Move) или ничего.
// Углы важнее сторон.
Handle hitTestHandle(const QRect& sel, QPoint p, int tolerance);

// Выделение после перетаскивания ручки h на delta. Move сдвигает без изменения размера и прижимает
// к bounds; остальные ручки двигают свои стороны, результат нормализуется и обрезается по bounds.
QRect applyHandleDrag(const QRect& original, Handle h, QPoint delta, const QRect& bounds);

// Конец отрезка с углом, округлённым до кратного 45°; длина проекции сохраняется.
QPoint snapLine45(QPoint start, QPoint end);

// Конец квадрата: сторона = max(|dx|, |dy|), направления по осям сохраняются.
QPoint snapSquare(QPoint start, QPoint end);

// Левый верхний угол панели размером toolbar рядом с выделением так, чтобы панель
// целиком лежала на одном мониторе.
QPoint placeToolbar(const QRect& sel, QSize toolbar, const QVector<QRect>& screens, int margin = 8);
```

`src/geometry.cpp`:
```cpp
#include "geometry.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdlib>

QRect rectFromPoints(QPoint a, QPoint b)
{
    return QRect(QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                 QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
}

Handle hitTestHandle(const QRect& sel, QPoint p, int tolerance)
{
    if (sel.isEmpty())
        return Handle::None;
    const bool inX = p.x() >= sel.left() - tolerance && p.x() <= sel.right() + tolerance;
    const bool inY = p.y() >= sel.top() - tolerance && p.y() <= sel.bottom() + tolerance;
    if (!inX || !inY)
        return Handle::None;

    const auto near = [tolerance](int value, int edge) { return std::abs(value - edge) <= tolerance; };
    const bool l = near(p.x(), sel.left());
    const bool r = near(p.x(), sel.right());
    const bool t = near(p.y(), sel.top());
    const bool b = near(p.y(), sel.bottom());
    if (t && l)
        return Handle::TopLeft;
    if (t && r)
        return Handle::TopRight;
    if (b && r)
        return Handle::BottomRight;
    if (b && l)
        return Handle::BottomLeft;
    if (t)
        return Handle::Top;
    if (b)
        return Handle::Bottom;
    if (l)
        return Handle::Left;
    if (r)
        return Handle::Right;
    return sel.contains(p) ? Handle::Move : Handle::None;
}

QRect applyHandleDrag(const QRect& original, Handle h, QPoint delta, const QRect& bounds)
{
    if (h == Handle::None || original.isEmpty())
        return original;

    if (h == Handle::Move) {
        QRect r = original.translated(delta);
        // Сначала правый и нижний края, потом левый и верхний: выделение больше bounds
        // прижмётся к левому верхнему углу.
        if (r.right() > bounds.right())
            r.moveRight(bounds.right());
        if (r.bottom() > bounds.bottom())
            r.moveBottom(bounds.bottom());
        if (r.left() < bounds.left())
            r.moveLeft(bounds.left());
        if (r.top() < bounds.top())
            r.moveTop(bounds.top());
        return r;
    }

    int left = original.left();
    int top = original.top();
    int right = original.right();
    int bottom = original.bottom();
    switch (h) {
    case Handle::TopLeft:
        left += delta.x();
        top += delta.y();
        break;
    case Handle::Top:
        top += delta.y();
        break;
    case Handle::TopRight:
        right += delta.x();
        top += delta.y();
        break;
    case Handle::Right:
        right += delta.x();
        break;
    case Handle::BottomRight:
        right += delta.x();
        bottom += delta.y();
        break;
    case Handle::Bottom:
        bottom += delta.y();
        break;
    case Handle::BottomLeft:
        left += delta.x();
        bottom += delta.y();
        break;
    case Handle::Left:
        left += delta.x();
        break;
    case Handle::None:
    case Handle::Move:
        break;
    }
    return rectFromPoints(QPoint(left, top), QPoint(right, bottom)).intersected(bounds);
}

QPoint snapLine45(QPoint start, QPoint end)
{
    const double dx = end.x() - start.x();
    const double dy = end.y() - start.y();
    if (dx == 0 && dy == 0)
        return end;
    constexpr double step = 3.14159265358979323846 / 4;
    const double angle = std::round(std::atan2(dy, dx) / step) * step;
    const double ux = std::cos(angle);
    const double uy = std::sin(angle);
    const double length = dx * ux + dy * uy;
    return start + QPoint(qRound(length * ux), qRound(length * uy));
}

QPoint snapSquare(QPoint start, QPoint end)
{
    const int dx = end.x() - start.x();
    const int dy = end.y() - start.y();
    const int side = std::max(std::abs(dx), std::abs(dy));
    return start + QPoint(dx < 0 ? -side : side, dy < 0 ? -side : side);
}

QPoint placeToolbar(const QRect& sel, QSize toolbar, const QVector<QRect>& screens, int margin)
{
    if (screens.isEmpty())
        return QPoint(sel.left(), sel.bottom() + margin);

    // Монитор: содержащий центр выделения, иначе с наибольшим пересечением, иначе первый.
    QRect screen;
    for (const QRect& s : screens) {
        if (s.contains(sel.center())) {
            screen = s;
            break;
        }
    }
    if (screen.isNull()) {
        qint64 bestArea = -1;
        for (const QRect& s : screens) {
            const QRect i = s.intersected(sel);
            const qint64 area = i.isEmpty() ? 0 : qint64(i.width()) * i.height();
            if (area > bestArea) {
                bestArea = area;
                screen = s;
            }
        }
    }

    const int h = toolbar.height();
    const int candidates[] = {sel.bottom() + margin, sel.top() - margin - h, sel.bottom() - margin - h};
    int y = screen.bottom() - h + 1; // ни один кандидат не подошёл — прижать к нижнему краю монитора
    for (int c : candidates) {
        if (c >= screen.top() && c + h - 1 <= screen.bottom()) {
            y = c;
            break;
        }
    }
    const int x = std::max(screen.left(), std::min(sel.left(), screen.right() - toolbar.width() + 1));
    return QPoint(x, y);
}
```

- [ ] **Step 5: Тесты проходят** — обе команды из Global Constraints. Expected: `100% tests passed`.

- [ ] **Step 6: Commit**
```bash
git add CMakeLists.txt .gitignore tests/ src/geometry.h src/geometry.cpp
git commit -m "feat: каркас сборки и геометрия выделения" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: Захват и склейка мониторов

**Files:**
- Create: `src/capture.h`, `src/capture.cpp`
- Modify: `CMakeLists.txt` (добавить `src/capture.h src/capture.cpp` в `hs_core`), `tests/CMakeLists.txt` (`hs_add_test(test_capture)`)
- Test: `tests/test_capture.cpp`

**Interfaces:**
- Consumes: `hs_core`, `hs_add_test`
- Produces:
  `struct ScreenShot { QImage image; QRect geometry; };`
  `struct Capture { QImage image; QPoint origin; QVector<QRect> screens; };` — `origin` = глобальные координаты X левого верхнего угла изображения; `screens` — в координатах изображения
  `Capture composeScreens(const QVector<ScreenShot>& shots);`
  `std::optional<Capture> captureAllScreens();`

- [ ] **Step 1: Падающий тест** — `tests/test_capture.cpp`:

```cpp
#include <QtTest>

#include "capture.h"

namespace {
QImage solid(QSize size, QRgb color)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image;
}
const QRgb kRed = qRgb(255, 0, 0);
const QRgb kGreen = qRgb(0, 255, 0);
const QRgb kBlue = qRgb(0, 0, 255);
const QRgb kBlack = qRgb(0, 0, 0);
}

class TestCapture : public QObject {
    Q_OBJECT
private slots:
    void composesUserLayout()
    {
        const Capture c = composeScreens({
            {solid({2560, 1440}, kRed), QRect(0, 720, 2560, 1440)},
            {solid({1920, 1080}, kGreen), QRect(2560, 0, 1920, 1080)},
            {solid({1920, 1080}, kBlue), QRect(2560, 1080, 1920, 1080)},
        });
        QCOMPARE(c.image.size(), QSize(4480, 2160));
        QCOMPARE(c.image.format(), QImage::Format_RGB32);
        QCOMPARE(c.origin, QPoint(0, 0));
        QCOMPARE(c.image.pixel(10, 800), kRed);
        QCOMPARE(c.image.pixel(3000, 10), kGreen);
        QCOMPARE(c.image.pixel(3000, 2000), kBlue);
        QCOMPARE(c.image.pixel(10, 10), kBlack); // мёртвая зона
        QCOMPARE(c.screens, (QVector<QRect>{QRect(0, 720, 2560, 1440), QRect(2560, 0, 1920, 1080),
                                            QRect(2560, 1080, 1920, 1080)}));
    }

    void negativeOrigin()
    {
        const Capture c = composeScreens({
            {solid({1920, 1080}, kGreen), QRect(-1920, 0, 1920, 1080)},
            {solid({2560, 1440}, kRed), QRect(0, 0, 2560, 1440)},
        });
        QCOMPARE(c.image.size(), QSize(4480, 1440));
        QCOMPARE(c.origin, QPoint(-1920, 0));
        QCOMPARE(c.image.pixel(10, 10), kGreen);
        QCOMPARE(c.image.pixel(1930, 10), kRed);
        QCOMPARE(c.image.pixel(10, 1300), kBlack);
        QCOMPARE(c.screens, (QVector<QRect>{QRect(0, 0, 1920, 1080), QRect(1920, 0, 2560, 1440)}));
    }

    void emptyInput()
    {
        const Capture c = composeScreens({});
        QVERIFY(c.image.isNull());
        QVERIFY(c.screens.isEmpty());
    }
};

QTEST_MAIN(TestCapture)
#include "test_capture.moc"
```

- [ ] **Step 2: Убедиться, что падает** — сборка. Expected: FAIL, нет `capture.h`.

- [ ] **Step 3: Реализация** — `src/capture.h`:

```cpp
#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QVector>

#include <optional>

// Снимок одного монитора; geometry — в глобальных координатах X.
struct ScreenShot {
    QImage image;
    QRect geometry;
};

// Снимок всех мониторов одним изображением (Format_RGB32).
struct Capture {
    QImage image;
    QPoint origin;          // глобальные координаты X левого верхнего угла изображения
    QVector<QRect> screens; // геометрии мониторов в координатах изображения
};

// Склеивает снимки в изображение размером с их охватывающий прямоугольник; мёртвые зоны чёрные.
Capture composeScreens(const QVector<ScreenShot>& shots);

// Снимает все мониторы. nullopt, если мониторов нет или снимок не удался.
std::optional<Capture> captureAllScreens();
```

`src/capture.cpp`:
```cpp
#include "capture.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>

Capture composeScreens(const QVector<ScreenShot>& shots)
{
    QRect bounds;
    for (const ScreenShot& s : shots)
        bounds = bounds.united(s.geometry);

    Capture result;
    if (bounds.isEmpty())
        return result;
    result.origin = bounds.topLeft();
    result.image = QImage(bounds.size(), QImage::Format_RGB32);
    result.image.fill(Qt::black);

    QPainter painter(&result.image);
    for (const ScreenShot& s : shots) {
        const QRect local = s.geometry.translated(-bounds.topLeft());
        painter.drawImage(local, s.image);
        result.screens.append(local);
    }
    return result;
}

std::optional<Capture> captureAllScreens()
{
    QVector<ScreenShot> shots;
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        // На X11 grabWindow(0) снимает корневое окно в пределах этого монитора.
        const QPixmap pixmap = screen->grabWindow(0);
        if (pixmap.isNull())
            return std::nullopt;
        shots.append({pixmap.toImage(), screen->geometry()});
    }
    if (shots.isEmpty())
        return std::nullopt;
    return composeScreens(shots);
}
```

- [ ] **Step 4: Тесты проходят** — обе сборки. Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/test_capture.cpp src/capture.h src/capture.cpp
git commit -m "feat: снимок и склейка мониторов" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Блокировка единственного экземпляра

**Files:**
- Create: `src/instancelock.h`, `src/instancelock.cpp`
- Modify: `CMakeLists.txt` (`hs_core` += `src/instancelock.h src/instancelock.cpp`), `tests/CMakeLists.txt` (`hs_add_test(test_instancelock)`)
- Test: `tests/test_instancelock.cpp`

**Interfaces:**
- Produces:
  `class InstanceLock { enum class Result { Acquired, Busy, Error }; Result tryAcquire(const QString& path); void release(); bool isHeld() const; ~InstanceLock(); }` (некопируемый)
  `QString defaultLockPath();` — `$XDG_RUNTIME_DIR/hot-screenshot.lock` или пустая строка

- [ ] **Step 1: Падающий тест** — `tests/test_instancelock.cpp`:

```cpp
#include <QtTest>

#include "instancelock.h"

#include <unistd.h>

class TestInstanceLock : public QObject {
    Q_OBJECT
private slots:
    void secondLockIsBusy()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.lock"));
        InstanceLock a;
        InstanceLock b;
        QCOMPARE(a.tryAcquire(path), InstanceLock::Result::Acquired);
        QVERIFY(a.isHeld());
        QCOMPARE(b.tryAcquire(path), InstanceLock::Result::Busy);
        QVERIFY(!b.isHeld());
        a.release();
        QVERIFY(!a.isHeld());
        QCOMPARE(b.tryAcquire(path), InstanceLock::Result::Acquired);
    }

    void destructorReleases()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.lock"));
        {
            InstanceLock a;
            QCOMPARE(a.tryAcquire(path), InstanceLock::Result::Acquired);
        }
        InstanceLock b;
        QCOMPARE(b.tryAcquire(path), InstanceLock::Result::Acquired);
    }

    void symlinkIsRejected()
    {
        QTemporaryDir dir;
        const QByteArray target = QFile::encodeName(dir.filePath(QStringLiteral("target")));
        const QString link = dir.filePath(QStringLiteral("x.lock"));
        QCOMPARE(::symlink(target.constData(), QFile::encodeName(link).constData()), 0);
        InstanceLock a;
        QCOMPARE(a.tryAcquire(link), InstanceLock::Result::Error);
        QVERIFY(!QFile::exists(QFile::decodeName(target)));
    }

    void defaultPathUsesRuntimeDir()
    {
        const QByteArray saved = qgetenv("XDG_RUNTIME_DIR");
        qputenv("XDG_RUNTIME_DIR", "/run/user/4242");
        QCOMPARE(defaultLockPath(), QStringLiteral("/run/user/4242/hot-screenshot.lock"));
        qunsetenv("XDG_RUNTIME_DIR");
        QVERIFY(defaultLockPath().isEmpty());
        if (!saved.isEmpty())
            qputenv("XDG_RUNTIME_DIR", saved);
    }
};

QTEST_MAIN(TestInstanceLock)
#include "test_instancelock.moc"
```

- [ ] **Step 2: Убедиться, что падает.** Expected: FAIL, нет `instancelock.h`.

- [ ] **Step 3: Реализация** — `src/instancelock.h`:

```cpp
#pragma once

#include <QString>

// Не даёт запустить второй оверлей, пока открыт первый (иначе второй снимет наш же оверлей).
// Блокировка — flock на файле; снимается при закрытии дескриптора, в том числе при падении процесса.
class InstanceLock {
public:
    enum class Result { Acquired, Busy, Error };

    InstanceLock() = default;
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    // O_NOFOLLOW: симлинк на месте файла блокировки — ошибка, а не переход по нему.
    Result tryAcquire(const QString& path);
    void release(); // идемпотентно
    bool isHeld() const { return m_fd >= 0; }

private:
    int m_fd = -1;
};

// $XDG_RUNTIME_DIR/hot-screenshot.lock (каталог 0700, только для пользователя);
// пустая строка, если переменная не задана.
QString defaultLockPath();
```

`src/instancelock.cpp`:
```cpp
#include "instancelock.h"

#include <QFile>

#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

InstanceLock::~InstanceLock()
{
    release();
}

InstanceLock::Result InstanceLock::tryAcquire(const QString& path)
{
    release();
    const QByteArray native = QFile::encodeName(path);
    const int fd = ::open(native.constData(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return Result::Error;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int err = errno;
        ::close(fd);
        return err == EWOULDBLOCK ? Result::Busy : Result::Error;
    }
    m_fd = fd;
    return Result::Acquired;
}

void InstanceLock::release()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

QString defaultLockPath()
{
    const QByteArray dir = qgetenv("XDG_RUNTIME_DIR");
    if (dir.isEmpty())
        return {};
    return QFile::decodeName(dir) + QStringLiteral("/hot-screenshot.lock");
}
```

- [ ] **Step 4: Тесты проходят** — обе сборки. Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/test_instancelock.cpp src/instancelock.h src/instancelock.cpp
git commit -m "feat: блокировка единственного экземпляра" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Модель аннотаций и история

**Files:**
- Create: `src/annotation.h`, `src/document.h`, `src/document.cpp`
- Modify: `CMakeLists.txt` (`hs_core` += `src/annotation.h src/document.h src/document.cpp`), `tests/CMakeLists.txt` (`hs_add_test(test_document)`)
- Test: `tests/test_document.cpp`

**Interfaces:**
- Produces:
  `enum class Tool { None, Pen, Marker, Line, Arrow, Rect, Ellipse, Text, Counter, Pixelate };`
  `struct Style { QColor color; int thickness = 4; };`
  `struct Annotation { Tool tool = Tool::None; Style style; QVector<QPoint> points; QString text; };`
  `inline QVector<int> counterNumbers(const QVector<Annotation>& annotations);` — 1, 2, 3… для Counter, 0 для остальных
  `class Document { void add(Annotation); bool undo(); bool redo(); bool canUndo() const; bool canRedo() const; const QVector<Annotation>& annotations() const; int nextCounterNumber() const; }`

- [ ] **Step 1: Падающий тест** — `tests/test_document.cpp`:

```cpp
#include <QtTest>

#include "document.h"

namespace {
Annotation make(Tool tool)
{
    Annotation a;
    a.tool = tool;
    a.style = Style{QColor(255, 0, 0), 4};
    a.points = {QPoint(1, 1), QPoint(10, 10)};
    return a;
}
}

class TestDocument : public QObject {
    Q_OBJECT
private slots:
    void emptyDocument()
    {
        Document d;
        QVERIFY(d.annotations().isEmpty());
        QVERIFY(!d.canUndo());
        QVERIFY(!d.canRedo());
        QVERIFY(!d.undo());
        QVERIFY(!d.redo());
        QCOMPARE(d.nextCounterNumber(), 1);
    }

    void undoRedo()
    {
        Document d;
        d.add(make(Tool::Rect));
        d.add(make(Tool::Arrow));
        QCOMPARE(d.annotations().size(), 2);
        QVERIFY(d.undo());
        QCOMPARE(d.annotations().size(), 1);
        QCOMPARE(d.annotations().at(0).tool, Tool::Rect);
        QVERIFY(d.canRedo());
        QVERIFY(d.redo());
        QCOMPARE(d.annotations().size(), 2);
        QCOMPARE(d.annotations().at(1).tool, Tool::Arrow);
        QVERIFY(!d.canRedo());
    }

    void addClearsRedo()
    {
        Document d;
        d.add(make(Tool::Rect));
        QVERIFY(d.undo());
        d.add(make(Tool::Line));
        QVERIFY(!d.canRedo());
        QCOMPARE(d.annotations().size(), 1);
        QCOMPARE(d.annotations().at(0).tool, Tool::Line);
    }

    void counterNumbering()
    {
        Document d;
        d.add(make(Tool::Counter));
        d.add(make(Tool::Rect));
        d.add(make(Tool::Counter));
        QCOMPARE(d.nextCounterNumber(), 3);
        QVERIFY(d.undo());
        QCOMPARE(d.nextCounterNumber(), 2);
        d.add(make(Tool::Counter));
        d.add(make(Tool::Counter));
        QCOMPARE(counterNumbers(d.annotations()), (QVector<int>{1, 0, 2, 3}));
    }
};

QTEST_MAIN(TestDocument)
#include "test_document.moc"
```

- [ ] **Step 2: Убедиться, что падает.** Expected: FAIL, нет `document.h`.

- [ ] **Step 3: Реализация** — `src/annotation.h`:

```cpp
#pragma once

#include <QColor>
#include <QPoint>
#include <QString>
#include <QVector>

enum class Tool { None, Pen, Marker, Line, Arrow, Rect, Ellipse, Text, Counter, Pixelate };

struct Style {
    QColor color;
    int thickness = 4; // 1..40
};

// Одна нарисованная фигура в координатах изображения.
struct Annotation {
    Tool tool = Tool::None;
    Style style;
    // Pen/Marker — путь; Line/Arrow/Rect/Ellipse/Pixelate — [начало, конец]; Text/Counter — [точка].
    QVector<QPoint> points;
    QString text; // только Text, строки через '\n'
};

// Номер каждого Counter по порядку (1, 2, 3…); у остальных аннотаций 0.
// Номер не хранится в аннотации, поэтому после отмены нумерация пересчитывается сама.
inline QVector<int> counterNumbers(const QVector<Annotation>& annotations)
{
    QVector<int> numbers;
    numbers.reserve(annotations.size());
    int next = 0;
    for (const Annotation& a : annotations)
        numbers.append(a.tool == Tool::Counter ? ++next : 0);
    return numbers;
}
```

`src/document.h`:
```cpp
#pragma once

#include "annotation.h"

#include <QVector>

// Нарисованные аннотации с историей. Отменяется и повторяется только добавление.
class Document {
public:
    void add(Annotation annotation); // очищает историю повтора
    bool undo();                     // false, если нечего отменять
    bool redo();                     // false, если нечего повторять
    bool canUndo() const;
    bool canRedo() const;
    const QVector<Annotation>& annotations() const;
    int nextCounterNumber() const;   // номер, который получит следующий Counter

private:
    QVector<Annotation> m_items;
    QVector<Annotation> m_redo;
};
```

`src/document.cpp`:
```cpp
#include "document.h"

#include <algorithm>

void Document::add(Annotation annotation)
{
    m_items.append(std::move(annotation));
    m_redo.clear();
}

bool Document::undo()
{
    if (m_items.isEmpty())
        return false;
    m_redo.append(m_items.takeLast());
    return true;
}

bool Document::redo()
{
    if (m_redo.isEmpty())
        return false;
    m_items.append(m_redo.takeLast());
    return true;
}

bool Document::canUndo() const
{
    return !m_items.isEmpty();
}

bool Document::canRedo() const
{
    return !m_redo.isEmpty();
}

const QVector<Annotation>& Document::annotations() const
{
    return m_items;
}

int Document::nextCounterNumber() const
{
    const auto isCounter = [](const Annotation& a) { return a.tool == Tool::Counter; };
    return int(std::count_if(m_items.cbegin(), m_items.cend(), isCounter)) + 1;
}
```

- [ ] **Step 4: Тесты проходят** — обе сборки. Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/test_document.cpp src/annotation.h src/document.h src/document.cpp
git commit -m "feat: модель аннотаций и история отмены" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Рендер аннотаций

**Files:**
- Create: `src/renderer.h`, `src/renderer.cpp`
- Modify: `CMakeLists.txt` (`hs_core` += `src/renderer.h src/renderer.cpp`), `tests/CMakeLists.txt` (`hs_add_test(test_renderer)`)
- Test: `tests/test_renderer.cpp`

**Interfaces:**
- Consumes: `annotation.h` (Task 4), `rectFromPoints` (Task 1)
- Produces:
  `int markerWidth(int t); int textPixelSize(int t); int counterDiameter(int t); int pixelateBlock(int t); int arrowHeadLength(int t); QFont textFont(int t);`
  `void drawAnnotation(QPainter& painter, const Annotation& a, int counterNumber);` — любым QPainter в координатах изображения; Pixelate рисуется пунктирной рамкой (превью)
  `void paintAnnotation(QImage& canvas, QPoint offset, const Annotation& a, int counterNumber);` — `offset` = координаты изображения, соответствующие (0,0) canvas
  `QImage render(const QImage& base, const QRect& selection, const QVector<Annotation>& annotations);`

Текст рисуется построчно: строка `i` — базовая линия `y + ascent + i × lineSpacing` по метрикам `textFont(t)`. Оверлей (Task 10) рисует курсор по тем же метрикам.

- [ ] **Step 1: Падающий тест** — `tests/test_renderer.cpp`:

```cpp
#include <QtTest>

#include "renderer.h"

namespace {
const QRgb kWhite = qRgb(255, 255, 255);
const QRgb kRed = qRgb(255, 0, 0);

QImage whiteImage(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(kWhite);
    return image;
}

Annotation make(Tool tool, QVector<QPoint> points, int thickness = 4, QColor color = QColor(255, 0, 0))
{
    Annotation a;
    a.tool = tool;
    a.style = Style{color, thickness};
    a.points = std::move(points);
    return a;
}

bool isReddish(QRgb p)
{
    return qRed(p) > 200 && qGreen(p) < 80 && qBlue(p) < 80;
}
}

class TestRenderer : public QObject {
    Q_OBJECT
private slots:
    void cropsToSelection()
    {
        QImage base = whiteImage(100, 100);
        base.setPixel(30, 40, qRgb(1, 2, 3));
        const QImage out = render(base, QRect(20, 30, 50, 50), {});
        QCOMPARE(out.size(), QSize(50, 50));
        QCOMPARE(out.format(), QImage::Format_RGB32);
        QCOMPARE(out.pixel(10, 10), qRgb(1, 2, 3));
    }

    void emptySelectionGivesNullImage()
    {
        QVERIFY(render(whiteImage(10, 10), QRect(), {}).isNull());
    }

    void rectOutlineWithOffset()
    {
        const QImage out = render(whiteImage(200, 200), QRect(50, 50, 100, 100),
                                  {make(Tool::Rect, {{60, 60}, {120, 110}})});
        QCOMPARE(out.pixel(10, 35), kRed);   // левая сторона x=60 → 10 в результате
        QCOMPARE(out.pixel(40, 35), kWhite); // середина не закрашена
    }

    void ellipseOutline()
    {
        const QImage out = render(whiteImage(100, 100), QRect(0, 0, 100, 100),
                                  {make(Tool::Ellipse, {{10, 10}, {90, 90}})});
        QVERIFY(isReddish(out.pixel(10, 50)));
        QCOMPARE(out.pixel(50, 50), kWhite);
    }

    void arrowHasHead()
    {
        const QImage out = render(whiteImage(100, 100), QRect(0, 0, 100, 100),
                                  {make(Tool::Arrow, {{10, 50}, {90, 50}})});
        QVERIFY(isReddish(out.pixel(40, 50))); // древко
        QCOMPARE(out.pixel(40, 56), kWhite);    // древко толщиной 4
        QVERIFY(isReddish(out.pixel(85, 50))); // наконечник
        QVERIFY(isReddish(out.pixel(80, 53))); // наконечник шире древка
    }

    void markerIsTranslucent()
    {
        const QImage out = render(whiteImage(100, 100), QRect(0, 0, 100, 100),
                                  {make(Tool::Marker, {{10, 50}, {90, 50}})});
        const QRgb p = out.pixel(50, 50);
        QVERIFY(qRed(p) >= 250);
        QVERIFY2(qGreen(p) >= 140 && qGreen(p) <= 170, qPrintable(QString::number(qGreen(p))));
    }

    void markerSelfOverlapDoesNotDarken()
    {
        const QImage out = render(whiteImage(100, 100), QRect(0, 0, 100, 100),
                                  {make(Tool::Marker, {{10, 50}, {90, 50}, {50, 10}, {50, 90}})});
        const int crossing = qGreen(out.pixel(50, 50));
        const int single = qGreen(out.pixel(70, 50));
        QVERIFY2(std::abs(crossing - single) <= 3, qPrintable(QStringLiteral("%1 vs %2").arg(crossing).arg(single)));
    }

    void pixelateMakesUniformBlocks()
    {
        QImage base(100, 60, QImage::Format_RGB32);
        for (int y = 0; y < base.height(); ++y)
            for (int x = 0; x < base.width(); ++x)
                base.setPixel(x, y, qRgb((x * 37) % 256, (y * 53) % 256, ((x + y) * 11) % 256));
        // thickness 3 → блок max(12, 12) = 12; область 48×24 → ровно 4×2 блока.
        const QImage out = render(base, base.rect(), {make(Tool::Pixelate, {{0, 0}, {47, 23}}, 3)});
        bool changed = false;
        for (int y = 0; y < 24; ++y) {
            for (int x = 0; x < 48; ++x) {
                QCOMPARE(out.pixel(x, y), out.pixel(x / 12 * 12, y / 12 * 12));
                changed = changed || out.pixel(x, y) != base.pixel(x, y);
            }
        }
        QVERIFY(changed);
        QCOMPARE(out.pixel(60, 30), base.pixel(60, 30)); // вне области без изменений
    }

    void pixelateCoversEarlierDrawing()
    {
        const QImage out = render(whiteImage(100, 60), QRect(0, 0, 100, 60),
                                  {make(Tool::Line, {{0, 5}, {47, 5}}, 2, QColor(0, 0, 0)),
                                   make(Tool::Pixelate, {{0, 0}, {47, 23}}, 3)});
        const QRgb p = out.pixel(5, 5);
        QVERIFY(qRed(p) > 0 && qRed(p) < 255); // чёрная линия усреднена с белым фоном
    }

    void textIsDrawn()
    {
        Annotation a = make(Tool::Text, {{10, 10}});
        a.text = QStringLiteral("ЖЖ\nЖ");
        const QImage out = render(whiteImage(120, 120), QRect(0, 0, 120, 120), {a});
        int red = 0;
        for (int y = 0; y < out.height(); ++y)
            for (int x = 0; x < out.width(); ++x)
                red += isReddish(out.pixel(x, y)) ? 1 : 0;
        QVERIFY2(red > 20, qPrintable(QString::number(red)));
    }

    void counterIsFilledCircle()
    {
        // thickness 4 → диаметр 32, центр (50,50).
        const QImage out = render(whiteImage(100, 100), QRect(0, 0, 100, 100), {make(Tool::Counter, {{50, 50}})});
        QVERIFY(isReddish(out.pixel(38, 50)));
        QCOMPARE(out.pixel(50, 75), kWhite);
    }

    void sizesFromThickness()
    {
        QCOMPARE(markerWidth(4), 16);
        QCOMPARE(textPixelSize(4), 22);
        QCOMPARE(counterDiameter(4), 32);
        QCOMPARE(pixelateBlock(1), 12);
        QCOMPARE(pixelateBlock(5), 20);
        QCOMPARE(arrowHeadLength(1), 10);
        QCOMPARE(arrowHeadLength(4), 16);
        QCOMPARE(textFont(4).pixelSize(), 22);
    }
};

QTEST_MAIN(TestRenderer)
#include "test_renderer.moc"
```

- [ ] **Step 2: Убедиться, что падает.** Expected: FAIL, нет `renderer.h`.

- [ ] **Step 3: Реализация** — `src/renderer.h`:

```cpp
#pragma once

#include "annotation.h"

#include <QFont>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QVector>

class QPainter;

// Размеры, выводимые из толщины; общие для превью, результата и тестов.
int markerWidth(int thickness);     // 4 × t
int textPixelSize(int thickness);   // 10 + 3 × t
int counterDiameter(int thickness); // 16 + 4 × t
int pixelateBlock(int thickness);   // max(12, 4 × t)
int arrowHeadLength(int thickness); // max(10, 4 × t)
QFont textFont(int thickness);

// Рисует аннотацию в координатах изображения любым QPainter (превью на виджете и paintAnnotation).
// Pixelate здесь только обозначается пунктирной рамкой.
void drawAnnotation(QPainter& painter, const Annotation& a, int counterNumber);

// Рисует аннотацию на canvas; offset — координаты изображения, соответствующие (0,0) canvas.
// Pixelate читает и заменяет пиксели canvas. QPainter закрывается до выхода из функции.
void paintAnnotation(QImage& canvas, QPoint offset, const Annotation& a, int counterNumber);

// Итог: base.copy(selection) в Format_RGB32 со всеми аннотациями по порядку; всё вне выделения обрезается.
QImage render(const QImage& base, const QRect& selection, const QVector<Annotation>& annotations);
```

`src/renderer.cpp`:
```cpp
#include "renderer.h"

#include "geometry.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStringList>

#include <algorithm>

int markerWidth(int thickness)
{
    return 4 * thickness;
}

int textPixelSize(int thickness)
{
    return 10 + 3 * thickness;
}

int counterDiameter(int thickness)
{
    return 16 + 4 * thickness;
}

int pixelateBlock(int thickness)
{
    return std::max(12, 4 * thickness);
}

int arrowHeadLength(int thickness)
{
    return std::max(10, 4 * thickness);
}

QFont textFont(int thickness)
{
    QFont font = QGuiApplication::font();
    font.setPixelSize(textPixelSize(thickness));
    return font;
}

namespace {
QRectF boxOf(const Annotation& a)
{
    return QRectF(QPointF(a.points.at(0)), QPointF(a.points.at(1))).normalized();
}

void drawArrow(QPainter& painter, const Annotation& a)
{
    const QPointF start = a.points.at(0);
    const QPointF end = a.points.at(1);
    const double length = QLineF(start, end).length();
    if (length < 1)
        return;
    const double head = std::min<double>(arrowHeadLength(a.style.thickness), length);
    const QPointF dir = (end - start) / length;
    const QPointF normal(-dir.y(), dir.x());
    const QPointF base = end - dir * head;

    painter.setPen(QPen(a.style.color, a.style.thickness, Qt::SolidLine, Qt::FlatCap));
    painter.drawLine(start, base);
    painter.setPen(Qt::NoPen);
    painter.setBrush(a.style.color);
    painter.drawPolygon(QPolygonF{end, base + normal * (head / 2), base - normal * (head / 2)});
}

void drawMarker(QPainter& painter, const Annotation& a)
{
    if (a.points.size() < 2)
        return;
    QPainterPath path(a.points.constFirst());
    for (int i = 1; i < a.points.size(); ++i)
        path.lineTo(a.points.at(i));
    // Контур штриха заливается один раз: самопересечения не темнеют.
    QPainterPathStroker stroker;
    stroker.setWidth(markerWidth(a.style.thickness));
    stroker.setCapStyle(Qt::FlatCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    QPainterPath outline = stroker.createStroke(path);
    outline.setFillRule(Qt::WindingFill);
    QColor color = a.style.color;
    color.setAlpha(100);
    painter.fillPath(outline, color);
}

void drawText(QPainter& painter, const Annotation& a)
{
    const QFont font = textFont(a.style.thickness);
    const QFontMetrics metrics(font);
    painter.setFont(font);
    painter.setPen(a.style.color);
    const QPoint origin = a.points.constFirst();
    const QStringList lines = a.text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i)
        painter.drawText(QPoint(origin.x(), origin.y() + metrics.ascent() + i * metrics.lineSpacing()), lines.at(i));
}

void drawCounter(QPainter& painter, const Annotation& a, int number)
{
    const int d = counterDiameter(a.style.thickness);
    const QPointF c = a.points.constFirst();
    const QRectF circle(c.x() - d / 2.0, c.y() - d / 2.0, d, d);
    painter.setPen(Qt::NoPen);
    painter.setBrush(a.style.color);
    painter.drawEllipse(circle);

    QFont font = QGuiApplication::font();
    font.setBold(true);
    font.setPixelSize(std::max(1, qRound(d * 0.55)));
    painter.setFont(font);
    painter.setPen(a.style.color.lightness() < 140 ? QColor(Qt::white) : QColor(Qt::black));
    painter.drawText(circle, Qt::AlignCenter, QString::number(number));
}

void pixelate(QImage& canvas, QPoint offset, const Annotation& a)
{
    const QRect area = rectFromPoints(a.points.at(0), a.points.at(1)).translated(-offset).intersected(canvas.rect());
    if (area.isEmpty())
        return;
    const int block = pixelateBlock(a.style.thickness);
    const int w = (area.width() + block - 1) / block;
    const int h = (area.height() + block - 1) / block;
    // Уменьшение с усреднением, затем увеличение без сглаживания — однотонные блоки.
    const QImage small = canvas.copy(area).scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage big = small.scaled(area.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QPainter painter(&canvas);
    painter.drawImage(area.topLeft(), big);
}
}

void drawAnnotation(QPainter& painter, const Annotation& a, int counterNumber)
{
    if (a.points.isEmpty())
        return;
    const bool twoPoints = a.points.size() >= 2;
    const QColor color = a.style.color;
    const int t = a.style.thickness;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    switch (a.tool) {
    case Tool::Pen:
        painter.setPen(QPen(color, t, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (a.points.size() == 1)
            painter.drawPoint(a.points.constFirst());
        else
            painter.drawPolyline(a.points.constData(), int(a.points.size()));
        break;
    case Tool::Marker:
        drawMarker(painter, a);
        break;
    case Tool::Line:
        if (twoPoints) {
            painter.setPen(QPen(color, t, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(a.points.at(0), a.points.at(1));
        }
        break;
    case Tool::Arrow:
        if (twoPoints)
            drawArrow(painter, a);
        break;
    case Tool::Rect:
        if (twoPoints) {
            painter.setPen(QPen(color, t, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            painter.drawRect(boxOf(a));
        }
        break;
    case Tool::Ellipse:
        if (twoPoints) {
            painter.setPen(QPen(color, t));
            painter.drawEllipse(boxOf(a));
        }
        break;
    case Tool::Text:
        drawText(painter, a);
        break;
    case Tool::Counter:
        drawCounter(painter, a, counterNumber);
        break;
    case Tool::Pixelate:
        if (twoPoints) {
            painter.setPen(QPen(color, 1, Qt::DashLine));
            painter.drawRect(boxOf(a));
        }
        break;
    case Tool::None:
        break;
    }
    painter.restore();
}

void paintAnnotation(QImage& canvas, QPoint offset, const Annotation& a, int counterNumber)
{
    if (a.tool == Tool::Pixelate) {
        if (a.points.size() >= 2)
            pixelate(canvas, offset, a);
        return;
    }
    QPainter painter(&canvas);
    painter.translate(-offset);
    drawAnnotation(painter, a, counterNumber);
}

QImage render(const QImage& base, const QRect& selection, const QVector<Annotation>& annotations)
{
    const QRect area = selection.intersected(base.rect());
    if (area.isEmpty())
        return {};
    QImage canvas = base.copy(area).convertToFormat(QImage::Format_RGB32);
    const QVector<int> numbers = counterNumbers(annotations);
    for (int i = 0; i < annotations.size(); ++i)
        paintAnnotation(canvas, area.topLeft(), annotations.at(i), numbers.at(i));
    return canvas;
}
```

- [ ] **Step 4: Тесты проходят** — обе сборки. Expected: PASS. Если `markerSelfOverlapDoesNotDarken` или `pixelateMakesUniformBlocks` падают — это сигнал ошибки в реализации, не в тесте: не ослаблять проверку, сообщить ревьюеру.

- [ ] **Step 5: Commit**
```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/test_renderer.cpp src/renderer.h src/renderer.cpp
git commit -m "feat: рендер аннотаций" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Вывод — PNG, атомарная запись, буфер обмена

**Files:**
- Create: `src/output.h`, `src/output.cpp`
- Modify: `CMakeLists.txt` (`hs_core` += `src/output.h src/output.cpp`), `tests/CMakeLists.txt` (`hs_add_test(test_output)`)
- Test: `tests/test_output.cpp`

**Interfaces:**
- Produces:
  `QByteArray encodePng(const QImage& image);` — пустой массив при ошибке
  `struct WriteResult { enum Status { Ok, Exists, Error }; Status status = Error; QString error; };`
  `enum class WriteMode { NoReplace, Replace };`
  `WriteResult writeFileAtomic(const QString& path, const QByteArray& data, WriteMode mode);`
  `QString quickSaveFileName(const QDateTime& now, int attempt);`
  `WriteResult quickSave(const QImage& image, const QString& dir, const QDateTime& now, QString* savedPath);`
  `QString screenshotsDir();`
  `void copyToClipboard(const QImage& image);`

- [ ] **Step 1: Падающий тест** — `tests/test_output.cpp`:

```cpp
#include <QtTest>

#include "output.h"

#include <sys/stat.h>
#include <unistd.h>

namespace {
QByteArray readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

void writeRaw(const QString& path, const QByteArray& data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

void makeSymlink(const QString& target, const QString& link)
{
    QCOMPARE(::symlink(QFile::encodeName(target).constData(), QFile::encodeName(link).constData()), 0);
}

QStringList leftovers(const QString& dir)
{
    return QDir(dir).entryList({QStringLiteral(".hot-screenshot-*")}, QDir::Files | QDir::Hidden);
}

const QDateTime kNow(QDate(2026, 9, 30), QTime(14, 5, 33));
}

class TestOutput : public QObject {
    Q_OBJECT
private slots:
    void pngRoundTrip()
    {
        QImage image(20, 10, QImage::Format_RGB32);
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 20; ++x)
                image.setPixel(x, y, qRgb(x * 10, y * 20, 7));
        const QByteArray png = encodePng(image);
        QVERIFY(png.startsWith("\x89PNG"));
        QCOMPARE(QImage::fromData(png, "PNG").convertToFormat(QImage::Format_RGB32), image);
    }

    void writesNewFileWithMode0600()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a.png"));
        const WriteResult r = writeFileAtomic(path, "data", WriteMode::NoReplace);
        QCOMPARE(r.status, WriteResult::Ok);
        QCOMPARE(readAll(path), QByteArray("data"));
        struct stat st {};
        QCOMPARE(::stat(QFile::encodeName(path).constData(), &st), 0);
        QCOMPARE(int(st.st_mode & 0777), 0600);
        QVERIFY(leftovers(dir.path()).isEmpty());
    }

    void noReplaceKeepsExistingFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a.png"));
        writeRaw(path, "old");
        const WriteResult r = writeFileAtomic(path, "new", WriteMode::NoReplace);
        QCOMPARE(r.status, WriteResult::Exists);
        QCOMPARE(readAll(path), QByteArray("old"));
        QVERIFY(leftovers(dir.path()).isEmpty());
    }

    void noReplaceDoesNotFollowDanglingSymlink()
    {
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("target"));
        const QString link = dir.filePath(QStringLiteral("a.png"));
        makeSymlink(target, link);
        const WriteResult r = writeFileAtomic(link, "new", WriteMode::NoReplace);
        QCOMPARE(r.status, WriteResult::Exists);
        QVERIFY(!QFile::exists(target));
        QVERIFY(QFileInfo(link).isSymLink());
        QVERIFY(leftovers(dir.path()).isEmpty());
    }

    void replaceSwapsSymlinkNotTarget()
    {
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("secret"));
        const QString link = dir.filePath(QStringLiteral("a.png"));
        writeRaw(target, "secret");
        makeSymlink(target, link);
        const WriteResult r = writeFileAtomic(link, "new", WriteMode::Replace);
        QCOMPARE(r.status, WriteResult::Ok);
        QVERIFY(!QFileInfo(link).isSymLink());
        QCOMPARE(readAll(link), QByteArray("new"));
        QCOMPARE(readAll(target), QByteArray("secret"));
    }

    void replaceOverwritesRegularFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a.png"));
        writeRaw(path, "old");
        QCOMPARE(writeFileAtomic(path, "new", WriteMode::Replace).status, WriteResult::Ok);
        QCOMPARE(readAll(path), QByteArray("new"));
    }

    void missingDirectoryIsError()
    {
        QTemporaryDir dir;
        const WriteResult r = writeFileAtomic(dir.filePath(QStringLiteral("no/such/a.png")), "x", WriteMode::NoReplace);
        QCOMPARE(r.status, WriteResult::Error);
        QVERIFY(!r.error.isEmpty());
    }

    void quickSaveFileNames()
    {
        QCOMPARE(quickSaveFileName(kNow, 0), QStringLiteral("2026-09-30_14-05-33.png"));
        QCOMPARE(quickSaveFileName(kNow, 2), QStringLiteral("2026-09-30_14-05-33_2.png"));
    }

    void quickSaveAvoidsCollision()
    {
        QTemporaryDir dir;
        writeRaw(dir.filePath(QStringLiteral("2026-09-30_14-05-33.png")), "old");
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(Qt::red);
        QString saved;
        const WriteResult r = quickSave(image, dir.path(), kNow, &saved);
        QCOMPARE(r.status, WriteResult::Ok);
        QCOMPARE(saved, dir.filePath(QStringLiteral("2026-09-30_14-05-33_1.png")));
        QCOMPARE(readAll(dir.filePath(QStringLiteral("2026-09-30_14-05-33.png"))), QByteArray("old"));
        QVERIFY(!QImage(saved).isNull());
    }
};

QTEST_MAIN(TestOutput)
#include "test_output.moc"
```

- [ ] **Step 2: Убедиться, что падает.** Expected: FAIL, нет `output.h`.

- [ ] **Step 3: Реализация** — `src/output.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>

// PNG в памяти; пустой массив при ошибке кодирования.
QByteArray encodePng(const QImage& image);

struct WriteResult {
    enum Status { Ok, Exists, Error };
    Status status = Error;
    QString error; // текст для пользователя, если status != Ok
};

enum class WriteMode {
    NoReplace, // существующая запись в каталоге (файл, симлинк) → Exists
    Replace,   // заменить запись в каталоге; цель симлинка не трогается
};

// Атомарная запись: временный файл 0600 в том же каталоге (O_EXCL), fsync, затем renameat2.
// Путь назначения никогда не открывается напрямую, поэтому запись не может пойти «сквозь» симлинк.
// После вызова временных файлов .hot-screenshot-* не остаётся.
WriteResult writeFileAtomic(const QString& path, const QByteArray& data, WriteMode mode);

// «2026-09-30_14-05-33.png»; при attempt > 0 — «2026-09-30_14-05-33_<attempt>.png».
QString quickSaveFileName(const QDateTime& now, int attempt);

// Сохраняет в dir под первым свободным именем (attempt 0..99) без перезаписи.
WriteResult quickSave(const QImage& image, const QString& dir, const QDateTime& now, QString* savedPath);

// ~/Pictures/Screenshots (создаётся при необходимости).
QString screenshotsDir();

void copyToClipboard(const QImage& image);
```

`src/output.cpp`:
```cpp
#include "output.h"

#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace {
QString sysError(const char* what, int err)
{
    return QStringLiteral("%1: %2").arg(QString::fromLatin1(what), QString::fromLocal8Bit(std::strerror(err)));
}

bool writeAll(int fd, const QByteArray& data)
{
    const char* p = data.constData();
    qsizetype left = data.size();
    while (left > 0) {
        const ssize_t n = ::write(fd, p, static_cast<size_t>(left));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        p += n;
        left -= n;
    }
    return true;
}
}

QByteArray encodePng(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return {};
    return bytes;
}

WriteResult writeFileAtomic(const QString& path, const QByteArray& data, WriteMode mode)
{
    const QFileInfo info(path);
    const QByteArray target = QFile::encodeName(info.absoluteFilePath());
    QByteArray temp = QFile::encodeName(info.absolutePath() + QStringLiteral("/.hot-screenshot-XXXXXX"));

    // mkostemp создаёт файл с O_CREAT | O_EXCL и правами 0600.
    const int fd = ::mkostemp(temp.data(), O_CLOEXEC);
    if (fd < 0)
        return {WriteResult::Error, sysError("mkostemp", errno)};

    bool ok = writeAll(fd, data);
    int err = ok ? 0 : errno;
    if (ok && ::fsync(fd) != 0) {
        ok = false;
        err = errno;
    }
    if (::close(fd) != 0 && ok) {
        ok = false;
        err = errno;
    }
    if (!ok) {
        ::unlink(temp.constData());
        return {WriteResult::Error, sysError("write", err)};
    }

    // Переименование подменяет запись в каталоге и не следует по симлинку на месте target.
    const int rc = mode == WriteMode::NoReplace
        ? ::renameat2(AT_FDCWD, temp.constData(), AT_FDCWD, target.constData(), RENAME_NOREPLACE)
        : ::rename(temp.constData(), target.constData());
    if (rc != 0) {
        err = errno;
        ::unlink(temp.constData());
        return {err == EEXIST ? WriteResult::Exists : WriteResult::Error, sysError("rename", err)};
    }
    return {WriteResult::Ok, {}};
}

QString quickSaveFileName(const QDateTime& now, int attempt)
{
    QString name = now.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    if (attempt > 0)
        name += QLatin1Char('_') + QString::number(attempt);
    return name + QStringLiteral(".png");
}

WriteResult quickSave(const QImage& image, const QString& dir, const QDateTime& now, QString* savedPath)
{
    const QByteArray png = encodePng(image);
    if (png.isEmpty())
        return {WriteResult::Error, QStringLiteral("не удалось закодировать PNG")};
    for (int attempt = 0; attempt < 100; ++attempt) {
        const QString path = QDir(dir).filePath(quickSaveFileName(now, attempt));
        const WriteResult r = writeFileAtomic(path, png, WriteMode::NoReplace);
        if (r.status == WriteResult::Exists)
            continue;
        if (r.status == WriteResult::Ok && savedPath)
            *savedPath = path;
        return r;
    }
    return {WriteResult::Error, QStringLiteral("все имена файлов на эту секунду заняты")};
}

QString screenshotsDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Screenshots");
    QDir().mkpath(dir);
    return dir;
}

void copyToClipboard(const QImage& image)
{
    QGuiApplication::clipboard()->setImage(image, QClipboard::Clipboard);
}
```

- [ ] **Step 4: Тесты проходят** — обе сборки. Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/test_output.cpp src/output.h src/output.cpp
git commit -m "feat: атомарная запись PNG и буфер обмена" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Настройки

**Files:**
- Create: `src/settings.h`, `src/settings.cpp`
- Modify: `CMakeLists.txt` (`hs_core` += `src/settings.h src/settings.cpp`), `tests/CMakeLists.txt` (`hs_add_test(test_settings)`)
- Test: `tests/test_settings.cpp`

**Interfaces:**
- Produces: `struct Settings { QColor color = QColor(0xE5, 0x39, 0x35); int thickness = 4; static Settings load(); void save() const; };` — хранятся в `QSettings("hot-screenshot", "hot-screenshot")`, ключи `color` (`#rrggbb`) и `thickness`.

- [ ] **Step 1: Падающий тест** — `tests/test_settings.cpp`:

```cpp
#include <QtTest>

#include "settings.h"

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_dir.path());
    }

    void init()
    {
        raw().clear();
    }

    void defaultsWhenEmpty()
    {
        const Settings s = Settings::load();
        QCOMPARE(s.color, QColor(0xE5, 0x39, 0x35));
        QCOMPARE(s.thickness, 4);
    }

    void roundTrip()
    {
        Settings s;
        s.color = QColor(0x1E, 0x88, 0xE5);
        s.thickness = 12;
        s.save();
        const Settings loaded = Settings::load();
        QCOMPARE(loaded.color, QColor(0x1E, 0x88, 0xE5));
        QCOMPARE(loaded.thickness, 12);
    }

    void invalidValuesFallBack()
    {
        {
            QSettings r = raw();
            r.setValue(QStringLiteral("color"), QStringLiteral("not-a-color"));
            r.setValue(QStringLiteral("thickness"), 999);
        }
        Settings s = Settings::load();
        QCOMPARE(s.color, QColor(0xE5, 0x39, 0x35));
        QCOMPARE(s.thickness, 40);

        {
            QSettings r = raw();
            r.setValue(QStringLiteral("color"), QStringLiteral("#12345g"));
            r.setValue(QStringLiteral("thickness"), QStringLiteral("abc"));
        }
        s = Settings::load();
        QCOMPARE(s.color, QColor(0xE5, 0x39, 0x35));
        QCOMPARE(s.thickness, 4);

        raw().setValue(QStringLiteral("thickness"), 0);
        QCOMPARE(Settings::load().thickness, 1);
    }

private:
    static QSettings raw() { return QSettings(QStringLiteral("hot-screenshot"), QStringLiteral("hot-screenshot")); }
    QTemporaryDir m_dir;
};

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
```

Примечание: `QSettings` некопируем, поэтому `raw()` возвращает объект через гарантированный copy elision (C++17). Если компилятор всё же откажет, заменить на локальные `QSettings r(QStringLiteral("hot-screenshot"), QStringLiteral("hot-screenshot"));` в каждом месте.

- [ ] **Step 2: Убедиться, что падает.** Expected: FAIL, нет `settings.h`.

- [ ] **Step 3: Реализация** — `src/settings.h`:

```cpp
#pragma once

#include <QColor>

// Цвет и толщина между запусками: ~/.config/hot-screenshot/hot-screenshot.conf.
struct Settings {
    QColor color = QColor(0xE5, 0x39, 0x35);
    int thickness = 4;

    // Невалидные значения заменяются значениями по умолчанию, толщина прижимается к 1..40.
    static Settings load();
    void save() const;
};
```

`src/settings.cpp`:
```cpp
#include "settings.h"

#include <QSettings>

namespace {
QSettings store()
{
    return QSettings(QStringLiteral("hot-screenshot"), QStringLiteral("hot-screenshot"));
}

// Принимает только «#rrggbb».
bool parseColor(const QString& text, QColor* color)
{
    if (text.size() != 7 || !text.startsWith(QLatin1Char('#')))
        return false;
    bool ok = false;
    const uint rgb = text.mid(1).toUInt(&ok, 16);
    if (!ok)
        return false;
    *color = QColor::fromRgb(QRgb(rgb | 0xff000000u));
    return true;
}
}

Settings Settings::load()
{
    Settings result;
    const QSettings s = store();
    QColor color;
    if (parseColor(s.value(QStringLiteral("color")).toString(), &color))
        result.color = color;
    bool ok = false;
    const int thickness = s.value(QStringLiteral("thickness")).toInt(&ok);
    if (ok)
        result.thickness = qBound(1, thickness, 40);
    return result;
}

void Settings::save() const
{
    QSettings s = store();
    s.setValue(QStringLiteral("color"), color.name());
    s.setValue(QStringLiteral("thickness"), thickness);
    s.sync();
}
```

- [ ] **Step 4: Тесты проходят** — обе сборки. Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/test_settings.cpp src/settings.h src/settings.cpp
git commit -m "feat: сохранение цвета и толщины" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Панель инструментов

**Files:**
- Create: `src/toolbar.h`, `src/toolbar.cpp`
- Modify: `CMakeLists.txt` (`hs_core` += `src/toolbar.h src/toolbar.cpp`), `tests/CMakeLists.txt` (`hs_add_test(test_toolbar)`)
- Test: `tests/test_toolbar.cpp`

**Interfaces:**
- Consumes: `Tool` (Task 4)
- Produces: `class Toolbar : public QWidget` — слоты `setTool(Tool)`, `setColor(const QColor&)`, `setThickness(int)`, `setUndoRedoEnabled(bool canUndo, bool canRedo)`; сигналы `toolChosen(Tool)`, `colorChosen(const QColor&)`, `undoRequested()`, `redoRequested()`, `copyRequested()`, `quickSaveRequested()`, `saveAsRequested()`, `closeRequested()`; `static const QVector<QColor>& palette()`.
  Имена объектов для тестов: `tool-pen`, `tool-marker`, `tool-line`, `tool-arrow`, `tool-rect`, `tool-ellipse`, `tool-text`, `tool-counter`, `tool-pixelate`, `color-0`…`color-7`, `thickness` (QLabel), `undo`, `redo`, `copy`, `quicksave`, `saveas`, `close`.
  Клик по инструменту сам вызывает `setTool` и эмитит `toolChosen`; клик по активному инструменту выбирает `Tool::None`. Клик по цвету сам вызывает `setColor` и эмитит `colorChosen`.

- [ ] **Step 1: Падающий тест** — `tests/test_toolbar.cpp`:

```cpp
#include <QtTest>

#include "toolbar.h"

#include <QLabel>
#include <QToolButton>

namespace {
QToolButton* button(Toolbar& tb, const char* name)
{
    auto* b = tb.findChild<QToolButton*>(QString::fromLatin1(name));
    if (!b)
        qFatal("нет кнопки %s", name);
    return b;
}
}

class TestToolbar : public QObject {
    Q_OBJECT
private slots:
    void clickToolEmitsAndChecks()
    {
        Toolbar tb;
        Tool got = Tool::None;
        connect(&tb, &Toolbar::toolChosen, this, [&](Tool t) { got = t; });
        button(tb, "tool-arrow")->click();
        QCOMPARE(got, Tool::Arrow);
        QVERIFY(button(tb, "tool-arrow")->isChecked());
        QVERIFY(!button(tb, "tool-pen")->isChecked());
    }

    void clickActiveToolDeselects()
    {
        Toolbar tb;
        tb.setTool(Tool::Arrow);
        Tool got = Tool::Arrow;
        connect(&tb, &Toolbar::toolChosen, this, [&](Tool t) { got = t; });
        button(tb, "tool-arrow")->click();
        QCOMPARE(got, Tool::None);
        QVERIFY(!button(tb, "tool-arrow")->isChecked());
    }

    void setToolChecksOnlyOne()
    {
        Toolbar tb;
        tb.setTool(Tool::Pen);
        QVERIFY(button(tb, "tool-pen")->isChecked());
        tb.setTool(Tool::Pixelate);
        QVERIFY(!button(tb, "tool-pen")->isChecked());
        QVERIFY(button(tb, "tool-pixelate")->isChecked());
    }

    void colorClick()
    {
        Toolbar tb;
        QColor got;
        connect(&tb, &Toolbar::colorChosen, this, [&](const QColor& c) { got = c; });
        button(tb, "color-4")->click();
        QCOMPARE(got, QColor(0x1E, 0x88, 0xE5));
        QVERIFY(button(tb, "color-4")->isChecked());
        QVERIFY(!button(tb, "color-0")->isChecked());
    }

    void paletteHasEightColors()
    {
        QCOMPARE(Toolbar::palette().size(), 8);
        QCOMPARE(Toolbar::palette().at(0), QColor(0xE5, 0x39, 0x35));
        QCOMPARE(Toolbar::palette().at(7), QColor(0xFF, 0xFF, 0xFF));
    }

    void thicknessLabel()
    {
        Toolbar tb;
        tb.setThickness(7);
        QCOMPARE(tb.findChild<QLabel*>(QStringLiteral("thickness"))->text(), QStringLiteral("7px"));
    }

    void undoRedoEnabled()
    {
        Toolbar tb;
        tb.setUndoRedoEnabled(false, true);
        QVERIFY(!button(tb, "undo")->isEnabled());
        QVERIFY(button(tb, "redo")->isEnabled());
    }

    void actionButtonsEmit()
    {
        Toolbar tb;
        QStringList got;
        connect(&tb, &Toolbar::undoRequested, this, [&] { got << QStringLiteral("undo"); });
        connect(&tb, &Toolbar::redoRequested, this, [&] { got << QStringLiteral("redo"); });
        connect(&tb, &Toolbar::copyRequested, this, [&] { got << QStringLiteral("copy"); });
        connect(&tb, &Toolbar::quickSaveRequested, this, [&] { got << QStringLiteral("quicksave"); });
        connect(&tb, &Toolbar::saveAsRequested, this, [&] { got << QStringLiteral("saveas"); });
        connect(&tb, &Toolbar::closeRequested, this, [&] { got << QStringLiteral("close"); });
        tb.setUndoRedoEnabled(true, true);
        for (const char* name : {"undo", "redo", "copy", "quicksave", "saveas", "close"})
            button(tb, name)->click();
        QCOMPARE(got, (QStringList{QStringLiteral("undo"), QStringLiteral("redo"), QStringLiteral("copy"),
                                   QStringLiteral("quicksave"), QStringLiteral("saveas"), QStringLiteral("close")}));
    }

    void buttonsNeverTakeFocus()
    {
        Toolbar tb;
        QCOMPARE(tb.focusPolicy(), Qt::NoFocus);
        const auto buttons = tb.findChildren<QToolButton*>();
        QCOMPARE(buttons.size(), 9 + 8 + 2 + 4);
        for (QToolButton* b : buttons)
            QCOMPARE(b->focusPolicy(), Qt::NoFocus);
    }
};

QTEST_MAIN(TestToolbar)
#include "test_toolbar.moc"
```

- [ ] **Step 2: Убедиться, что падает.** Expected: FAIL, нет `toolbar.h`.

- [ ] **Step 3: Реализация** — `src/toolbar.h`:

```cpp
#pragma once

#include "annotation.h"

#include <QColor>
#include <QMap>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QToolButton;

// Панель под выделением. Кнопки не принимают фокус, чтобы клавиатура всегда оставалась у оверлея.
class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget* parent = nullptr);

    static const QVector<QColor>& palette();

public slots:
    void setTool(Tool tool);
    void setColor(const QColor& color);
    void setThickness(int thickness);
    void setUndoRedoEnabled(bool canUndo, bool canRedo);

signals:
    void toolChosen(Tool tool);
    void colorChosen(const QColor& color);
    void undoRequested();
    void redoRequested();
    void copyRequested();
    void quickSaveRequested();
    void saveAsRequested();
    void closeRequested();

private:
    QToolButton* addButton(const QString& objectName, const QString& text, const QString& toolTip);
    void addSeparator();

    QHBoxLayout* m_layout = nullptr;
    QMap<Tool, QToolButton*> m_toolButtons;
    QVector<QToolButton*> m_colorButtons;
    QLabel* m_thickness = nullptr;
    QToolButton* m_undo = nullptr;
    QToolButton* m_redo = nullptr;
    Tool m_tool = Tool::None;
};
```

`src/toolbar.cpp`:
```cpp
#include "toolbar.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace {
struct ToolDef {
    Tool tool;
    const char* name;
    const char* glyph;
    const char* tip;
};

const ToolDef kTools[] = {
    {Tool::Pen, "tool-pen", "✎", "Карандаш (P)"},
    {Tool::Marker, "tool-marker", "▮", "Маркер (M)"},
    {Tool::Line, "tool-line", "╱", "Линия (L)"},
    {Tool::Arrow, "tool-arrow", "↗", "Стрелка (A)"},
    {Tool::Rect, "tool-rect", "▭", "Прямоугольник (R)"},
    {Tool::Ellipse, "tool-ellipse", "◯", "Эллипс (E)"},
    {Tool::Text, "tool-text", "T", "Текст (T)"},
    {Tool::Counter, "tool-counter", "①", "Номерок (N)"},
    {Tool::Pixelate, "tool-pixelate", "▦", "Пикселизация (B)"},
};
}

Toolbar::Toolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("toolbar"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(QStringLiteral(
        "#toolbar { background: rgba(32, 32, 32, 235); border-radius: 6px; }"
        "QToolButton { color: white; background: transparent; border: 1px solid transparent;"
        " border-radius: 4px; font-size: 16px; min-width: 28px; min-height: 28px; }"
        "QToolButton:hover { background: rgba(255, 255, 255, 40); }"
        "QToolButton:checked { background: rgba(61, 139, 253, 160); }"
        "QToolButton:disabled { color: rgba(255, 255, 255, 80); }"
        "QLabel { color: white; padding: 0 4px; }"));

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(2);

    for (const ToolDef& def : kTools) {
        QToolButton* b = addButton(QString::fromLatin1(def.name), QString::fromUtf8(def.glyph), QString::fromUtf8(def.tip));
        b->setCheckable(true);
        const Tool tool = def.tool;
        connect(b, &QToolButton::clicked, this, [this, tool] {
            // Повторное нажатие активного инструмента выключает его.
            const Tool next = tool == m_tool ? Tool::None : tool;
            setTool(next);
            emit toolChosen(next);
        });
        m_toolButtons.insert(tool, b);
    }

    addSeparator();
    const QVector<QColor>& colors = palette();
    for (int i = 0; i < colors.size(); ++i) {
        const QColor color = colors.at(i);
        QToolButton* b = addButton(QStringLiteral("color-%1").arg(i), QString(), color.name());
        b->setCheckable(true);
        b->setStyleSheet(QStringLiteral(
            "QToolButton { background: %1; min-width: 18px; max-width: 18px; min-height: 18px; max-height: 18px;"
            " border: 2px solid rgba(255, 255, 255, 60); border-radius: 3px; }"
            "QToolButton:checked { border: 2px solid white; }").arg(color.name()));
        connect(b, &QToolButton::clicked, this, [this, color] {
            setColor(color);
            emit colorChosen(color);
        });
        m_colorButtons.append(b);
    }

    addSeparator();
    m_thickness = new QLabel(this);
    m_thickness->setObjectName(QStringLiteral("thickness"));
    m_thickness->setToolTip(QStringLiteral("Толщина (колесо мыши)"));
    m_layout->addWidget(m_thickness);

    addSeparator();
    m_undo = addButton(QStringLiteral("undo"), QStringLiteral("↶"), QStringLiteral("Отменить (Ctrl+Z)"));
    m_redo = addButton(QStringLiteral("redo"), QStringLiteral("↷"), QStringLiteral("Повторить (Ctrl+Shift+Z)"));
    connect(m_undo, &QToolButton::clicked, this, &Toolbar::undoRequested);
    connect(m_redo, &QToolButton::clicked, this, &Toolbar::redoRequested);

    addSeparator();
    connect(addButton(QStringLiteral("copy"), QStringLiteral("⧉"), QStringLiteral("Копировать в буфер (Ctrl+C)")),
            &QToolButton::clicked, this, &Toolbar::copyRequested);
    connect(addButton(QStringLiteral("quicksave"), QStringLiteral("↓"), QStringLiteral("Быстро сохранить (Ctrl+S)")),
            &QToolButton::clicked, this, &Toolbar::quickSaveRequested);
    connect(addButton(QStringLiteral("saveas"), QStringLiteral("…"), QStringLiteral("Сохранить как (Ctrl+Shift+S)")),
            &QToolButton::clicked, this, &Toolbar::saveAsRequested);
    connect(addButton(QStringLiteral("close"), QStringLiteral("✕"), QStringLiteral("Закрыть (Esc)")),
            &QToolButton::clicked, this, &Toolbar::closeRequested);

    setTool(Tool::None);
    setThickness(4);
    setUndoRedoEnabled(false, false);
}

const QVector<QColor>& Toolbar::palette()
{
    static const QVector<QColor> colors = {
        QColor(0xE5, 0x39, 0x35), QColor(0xFB, 0x8C, 0x00), QColor(0xFD, 0xD8, 0x35), QColor(0x43, 0xA0, 0x47),
        QColor(0x1E, 0x88, 0xE5), QColor(0x8E, 0x24, 0xAA), QColor(0x00, 0x00, 0x00), QColor(0xFF, 0xFF, 0xFF),
    };
    return colors;
}

void Toolbar::setTool(Tool tool)
{
    m_tool = tool;
    for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it)
        it.value()->setChecked(it.key() == tool);
}

void Toolbar::setColor(const QColor& color)
{
    for (int i = 0; i < m_colorButtons.size(); ++i)
        m_colorButtons.at(i)->setChecked(palette().at(i) == color);
}

void Toolbar::setThickness(int thickness)
{
    m_thickness->setText(QStringLiteral("%1px").arg(thickness));
}

void Toolbar::setUndoRedoEnabled(bool canUndo, bool canRedo)
{
    m_undo->setEnabled(canUndo);
    m_redo->setEnabled(canRedo);
}

QToolButton* Toolbar::addButton(const QString& objectName, const QString& text, const QString& toolTip)
{
    auto* b = new QToolButton(this);
    b->setObjectName(objectName);
    b->setText(text);
    b->setToolTip(toolTip);
    b->setFocusPolicy(Qt::NoFocus);
    m_layout->addWidget(b);
    return b;
}

void Toolbar::addSeparator()
{
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 60);"));
    m_layout->addWidget(line);
}
```

- [ ] **Step 4: Тесты проходят** — обе сборки. Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/test_toolbar.cpp src/toolbar.h src/toolbar.cpp
git commit -m "feat: панель инструментов" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Клавиши вне раскладки и оверлей — выделение и вывод

**Files:**
- Create: `src/keys.h`, `src/keys.cpp`, `src/overlay.h`, `src/overlay.cpp`
- Modify: `CMakeLists.txt` (`hs_core` += `src/keys.h src/keys.cpp src/overlay.h src/overlay.cpp`), `tests/CMakeLists.txt` (`hs_add_test(test_keys)`, `hs_add_test(test_overlay)`)
- Test: `tests/test_keys.cpp`, `tests/test_overlay.cpp`

**Interfaces:**
- Consumes: всё из Task 1–8.
- Produces:
  `int layoutIndependentKey(int key, quint32 nativeScanCode);`
  `class Overlay : public QWidget` — `Overlay(Capture capture, Settings settings, QString saveDir, QWidget* parent = nullptr)`; `void start()`; `QRect selection() const`; `Tool tool() const`; `Style style() const`; `const Document& document() const`; `Toolbar* toolbar() const`; `QImage result() const`; сигналы `copyRequested(const QImage& result)`, `finished()`.
  Рисование мышью в этой задаче не делается: при активном инструменте нажатие внутри выделения пока ничего не делает (добавит Task 10).

- [ ] **Step 1: Падающий тест клавиш** — `tests/test_keys.cpp`:

```cpp
#include <QtTest>

#include "keys.h"

class TestKeys : public QObject {
    Q_OBJECT
private slots:
    void latinLettersPassThrough()
    {
        QCOMPARE(layoutIndependentKey(Qt::Key_P, 0), int(Qt::Key_P));
        QCOMPARE(layoutIndependentKey(Qt::Key_C, 999), int(Qt::Key_C));
    }

    void cyrillicMapsByScanCode()
    {
        QCOMPARE(layoutIndependentKey(0x0417, 33), int(Qt::Key_P)); // «З» на месте P
        QCOMPARE(layoutIndependentKey(0x0421, 54), int(Qt::Key_C)); // «С» на месте C
        QCOMPARE(layoutIndependentKey(0x042B, 39), int(Qt::Key_S)); // «Ы» на месте S
        QCOMPARE(layoutIndependentKey(0x042F, 52), int(Qt::Key_Z)); // «Я» на месте Z
    }

    void otherKeysUnchanged()
    {
        QCOMPARE(layoutIndependentKey(Qt::Key_Escape, 9), int(Qt::Key_Escape));
        QCOMPARE(layoutIndependentKey(Qt::Key_Return, 36), int(Qt::Key_Return));
        QCOMPARE(layoutIndependentKey(Qt::Key_1, 10), int(Qt::Key_1));
        QCOMPARE(layoutIndependentKey(0x0416, 47), 0x0416); // «Ж» на месте «;» — не буква латиницы
        QCOMPARE(layoutIndependentKey(0x0417, 0), 0x0417);  // скан-код неизвестен
    }
};

QTEST_MAIN(TestKeys)
#include "test_keys.moc"
```

- [ ] **Step 2: Падающий тест оверлея** — `tests/test_overlay.cpp`:

```cpp
#include <QtTest>

#include "overlay.h"
#include "toolbar.h"

#include <QToolButton>

namespace {
// Два «монитора»: 400×300 и 400×200 справа; зона (400..799, 200..299) — мёртвая.
Capture makeCapture()
{
    Capture c;
    c.image = QImage(800, 300, QImage::Format_RGB32);
    c.image.fill(QColor(100, 100, 100));
    c.origin = QPoint(0, 0);
    c.screens = {QRect(0, 0, 400, 300), QRect(400, 0, 400, 200)};
    return c;
}

struct Env {
    QTemporaryDir dir;
    Overlay ov{makeCapture(), Settings{}, dir.path()};
    bool finished = false;
    QImage copied;
    bool wasCopied = false;

    Env()
    {
        QObject::connect(&ov, &Overlay::finished, [this] { finished = true; });
        QObject::connect(&ov, &Overlay::copyRequested, [this](const QImage& image) {
            copied = image;
            wasCopied = true;
        });
        ov.show();
    }
};

void mouse(QWidget* w, QEvent::Type type, QPoint pos, Qt::MouseButton button, Qt::MouseButtons buttons,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent e(type, QPointF(pos), w->mapToGlobal(QPointF(pos)), button, buttons, mods);
    QApplication::sendEvent(w, &e);
}

void drag(QWidget* w, QPoint from, QPoint to, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouse(w, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, mods);
    mouse(w, QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton, mods);
    mouse(w, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
    mouse(w, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
}

void click(QWidget* w, QPoint pos)
{
    mouse(w, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
}

// Нажатие клавиши так, как его присылает X11 в кириллической раскладке.
void rawKey(QWidget* w, int key, Qt::KeyboardModifiers mods, quint32 scanCode, const QString& text)
{
    QKeyEvent e(QEvent::KeyPress, key, mods, scanCode, 0, 0, text);
    QApplication::sendEvent(w, &e);
}
}

class TestOverlay : public QObject {
    Q_OBJECT
private slots:
    void dragSelects()
    {
        Env e;
        QVERIFY(!e.ov.toolbar()->isVisible());
        drag(&e.ov, {10, 10}, {110, 60});
        QCOMPARE(e.ov.selection(), QRect(10, 10, 101, 51));
        QVERIFY(e.ov.toolbar()->isVisible());
    }

    void clickSelectsScreen()
    {
        Env e;
        click(&e.ov, {500, 100});
        QCOMPARE(e.ov.selection(), QRect(400, 0, 400, 200));
    }

    void clickInDeadZoneSelectsNothing()
    {
        Env e;
        click(&e.ov, {500, 250});
        QVERIFY(e.ov.selection().isEmpty());
        QVERIFY(!e.ov.toolbar()->isVisible());
    }

    void dragBeyondEdgeClamps()
    {
        Env e;
        drag(&e.ov, {700, 250}, {900, 400});
        QCOMPARE(e.ov.selection(), QRect(700, 250, 100, 50));
    }

    void ctrlASelectsAll()
    {
        Env e;
        QTest::keyClick(&e.ov, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(e.ov.selection(), QRect(0, 0, 800, 300));
    }

    void moveSelection()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        drag(&e.ov, {50, 30}, {70, 40});
        QCOMPARE(e.ov.selection(), QRect(30, 20, 101, 51));
    }

    void resizeByCorner()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        drag(&e.ov, {110, 60}, {150, 80});
        QCOMPARE(e.ov.selection(), QRect(10, 10, 141, 71));
    }

    void dragOutsideStartsNewSelection()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        drag(&e.ov, {200, 100}, {300, 200});
        QCOMPARE(e.ov.selection(), QRect(200, 100, 101, 101));
    }

    void toolKeys()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        const QList<QPair<Qt::Key, Tool>> keys = {
            {Qt::Key_P, Tool::Pen}, {Qt::Key_M, Tool::Marker}, {Qt::Key_L, Tool::Line},
            {Qt::Key_A, Tool::Arrow}, {Qt::Key_R, Tool::Rect}, {Qt::Key_E, Tool::Ellipse},
            {Qt::Key_T, Tool::Text}, {Qt::Key_N, Tool::Counter}, {Qt::Key_B, Tool::Pixelate},
            {Qt::Key_V, Tool::None},
        };
        for (const auto& [key, tool] : keys) {
            QTest::keyClick(&e.ov, key);
            QCOMPARE(e.ov.tool(), tool);
        }
    }

    void toolKeysIgnoredWithoutSelection()
    {
        Env e;
        QTest::keyClick(&e.ov, Qt::Key_P);
        QCOMPARE(e.ov.tool(), Tool::None);
    }

    void toolbarButtonChangesTool()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        e.ov.toolbar()->findChild<QToolButton*>(QStringLiteral("tool-rect"))->click();
        QCOMPARE(e.ov.tool(), Tool::Rect);
    }

    void cyrillicLayoutHotkeys()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        rawKey(&e.ov, 0x0417, Qt::NoModifier, 33, QStringLiteral("з"));
        QCOMPARE(e.ov.tool(), Tool::Pen);
        rawKey(&e.ov, 0x0421, Qt::ControlModifier, 54, QString());
        QVERIFY(e.wasCopied);
    }

    void ctrlCCopiesSelection()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        QTest::keyClick(&e.ov, Qt::Key_C, Qt::ControlModifier);
        QVERIFY(e.wasCopied);
        QCOMPARE(e.copied.size(), QSize(101, 51));
        QCOMPARE(e.copied.pixel(5, 5), qRgb(100, 100, 100));
        QVERIFY(!e.ov.isVisible());
        QVERIFY(!e.finished);
    }

    void enterCopies()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        QTest::keyClick(&e.ov, Qt::Key_Return);
        QVERIFY(e.wasCopied);
    }

    void outputIgnoredWithoutSelection()
    {
        Env e;
        QTest::keyClick(&e.ov, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(&e.ov, Qt::Key_S, Qt::ControlModifier);
        QVERIFY(!e.wasCopied);
        QVERIFY(!e.finished);
        QVERIFY(e.ov.isVisible());
    }

    void quickSaveWritesFile()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        QTest::keyClick(&e.ov, Qt::Key_S, Qt::ControlModifier);
        QVERIFY(e.finished);
        const QStringList files = QDir(e.dir.path()).entryList({QStringLiteral("*.png")}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QCOMPARE(QImage(e.dir.filePath(files.first())).size(), QSize(101, 51));
    }

    void escapeFinishes()
    {
        Env e;
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QVERIFY(e.finished);
        QVERIFY(!e.ov.isVisible());
    }

    void wheelChangesThickness()
    {
        Env e;
        const auto wheel = [&](int dy) {
            QWheelEvent ev(QPointF(50, 50), e.ov.mapToGlobal(QPointF(50, 50)), QPoint(), QPoint(0, dy),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&e.ov, &ev);
        };
        wheel(120);
        QCOMPARE(e.ov.style().thickness, 5);
        for (int i = 0; i < 50; ++i)
            wheel(120);
        QCOMPARE(e.ov.style().thickness, 40);
        for (int i = 0; i < 50; ++i)
            wheel(-120);
        QCOMPARE(e.ov.style().thickness, 1);
    }
};

QTEST_MAIN(TestOverlay)
#include "test_overlay.moc"
```

- [ ] **Step 3: Убедиться, что оба теста падают.** Expected: FAIL, нет `keys.h` и `overlay.h`.

- [ ] **Step 4: Реализация клавиш** — `src/keys.h`:

```cpp
#pragma once

#include <QtGlobal>

// Латинская буква (Qt::Key_A..Key_Z) для буквенной клавиши независимо от раскладки.
// Уже латинская буква возвращается как есть. Символ другой раскладки (например, «З») переводится
// по физической клавише — X11-скан-коду (evdev + 8). Остальные клавиши возвращаются без изменений.
int layoutIndependentKey(int key, quint32 nativeScanCode);
```

`src/keys.cpp`:
```cpp
#include "keys.h"

#include <Qt>

int layoutIndependentKey(int key, quint32 nativeScanCode)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return key;
    if (key >= Qt::Key_Escape) // Esc, Enter, стрелки и прочие спецклавиши
        return key;

    struct Entry {
        quint32 scanCode;
        int key;
    };
    static const Entry kTable[] = {
        {24, Qt::Key_Q}, {25, Qt::Key_W}, {26, Qt::Key_E}, {27, Qt::Key_R}, {28, Qt::Key_T},
        {29, Qt::Key_Y}, {30, Qt::Key_U}, {31, Qt::Key_I}, {32, Qt::Key_O}, {33, Qt::Key_P},
        {38, Qt::Key_A}, {39, Qt::Key_S}, {40, Qt::Key_D}, {41, Qt::Key_F}, {42, Qt::Key_G},
        {43, Qt::Key_H}, {44, Qt::Key_J}, {45, Qt::Key_K}, {46, Qt::Key_L},
        {52, Qt::Key_Z}, {53, Qt::Key_X}, {54, Qt::Key_C}, {55, Qt::Key_V}, {56, Qt::Key_B},
        {57, Qt::Key_N}, {58, Qt::Key_M},
    };
    for (const Entry& e : kTable) {
        if (e.scanCode == nativeScanCode)
            return e.key;
    }
    return key;
}
```

- [ ] **Step 5: Реализация оверлея** — `src/overlay.h`:

```cpp
#pragma once

#include "annotation.h"
#include "capture.h"
#include "document.h"
#include "geometry.h"
#include "settings.h"

#include <QImage>
#include <QString>
#include <QWidget>

class Toolbar;

// Окно поверх всех мониторов: выделение области, рисование и вывод результата.
class Overlay : public QWidget {
    Q_OBJECT
public:
    Overlay(Capture capture, Settings settings, QString saveDir, QWidget* parent = nullptr);

    void start(); // показать, поднять и захватить клавиатуру
    QRect selection() const { return m_selection; }
    Tool tool() const { return m_tool; }
    Style style() const { return m_style; }
    const Document& document() const { return m_document; }
    Toolbar* toolbar() const { return m_toolbar; }
    QImage result() const; // выделенная область со всеми аннотациями

signals:
    void copyRequested(const QImage& result);
    void finished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class Drag { None, Selecting, Moving, Resizing, Drawing };

    void setSelection(const QRect& selection);
    void setTool(Tool tool);
    void setColor(const QColor& color);
    void setThickness(int thickness);
    void undo();
    void redo();
    void copyResult();
    void saveQuick();
    void saveAs();
    void cancel();
    void closeOverlay(); // отпустить клавиатуру и спрятать окно
    void showError(const QString& text);
    void documentChanged();
    void updateToolbar();
    void updateCursor(QPoint pos);
    QRect screenAt(QPoint pos) const;
    QRect bounds() const;

    void paintSelectionFrame(QPainter& painter) const;
    void paintSizeLabel(QPainter& painter) const;
    void paintHint(QPainter& painter) const;

    Capture m_capture;
    QImage m_dimmed; // снимок с затемнением — фон вне выделения
    QString m_saveDir;
    Style m_style;
    Tool m_tool = Tool::None;
    Document m_document;
    QRect m_selection;
    QRect m_hintScreen; // монитор под курсором, пока выделения нет
    Toolbar* m_toolbar = nullptr;

    Drag m_drag = Drag::None;
    Handle m_handle = Handle::None;
    QPoint m_pressPos;
    QRect m_selectionAtPress;

    mutable QImage m_cache; // render() для текущего выделения и документа
    mutable bool m_cacheValid = false;
};
```

`src/overlay.cpp`:
```cpp
#include "overlay.h"

#include "keys.h"
#include "output.h"
#include "renderer.h"
#include "toolbar.h"

#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QTextStream>
#include <QWheelEvent>

namespace {
constexpr int kHandleTolerance = 6;
constexpr int kHandleSize = 8;
constexpr int kClickThreshold = 3;
constexpr int kMinThickness = 1;
constexpr int kMaxThickness = 40;

QColor accentColor()
{
    return QColor(0x3D, 0x8B, 0xFD);
}
}

Overlay::Overlay(Capture capture, Settings settings, QString saveDir, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint)
    , m_capture(std::move(capture))
    , m_saveDir(std::move(saveDir))
    , m_style{settings.color, settings.thickness}
{
    setGeometry(QRect(m_capture.origin, m_capture.image.size()));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);

    m_dimmed = m_capture.image.copy();
    {
        QPainter painter(&m_dimmed);
        painter.fillRect(m_dimmed.rect(), QColor(0, 0, 0, 120));
    }
    m_hintScreen = screenAt(mapFromGlobal(QCursor::pos()));

    m_toolbar = new Toolbar(this);
    m_toolbar->hide();
    m_toolbar->setTool(m_tool);
    m_toolbar->setColor(m_style.color);
    m_toolbar->setThickness(m_style.thickness);
    connect(m_toolbar, &Toolbar::toolChosen, this, &Overlay::setTool);
    connect(m_toolbar, &Toolbar::colorChosen, this, &Overlay::setColor);
    connect(m_toolbar, &Toolbar::undoRequested, this, &Overlay::undo);
    connect(m_toolbar, &Toolbar::redoRequested, this, &Overlay::redo);
    connect(m_toolbar, &Toolbar::copyRequested, this, &Overlay::copyResult);
    connect(m_toolbar, &Toolbar::quickSaveRequested, this, &Overlay::saveQuick);
    connect(m_toolbar, &Toolbar::saveAsRequested, this, &Overlay::saveAs);
    connect(m_toolbar, &Toolbar::closeRequested, this, &Overlay::cancel);
}

void Overlay::start()
{
    show();
    raise();
    activateWindow();
    setFocus(Qt::OtherFocusReason);
    grabKeyboard();
}

QImage Overlay::result() const
{
    return render(m_capture.image, m_selection, m_document.annotations());
}

// ---- Состояние ----

void Overlay::setSelection(const QRect& selection)
{
    m_selection = selection;
    m_cacheValid = false;
    updateToolbar();
    update();
}

void Overlay::setTool(Tool tool)
{
    m_tool = tool;
    m_toolbar->setTool(tool);
    updateCursor(mapFromGlobal(QCursor::pos()));
}

void Overlay::setColor(const QColor& color)
{
    m_style.color = color;
    m_toolbar->setColor(color);
}

void Overlay::setThickness(int thickness)
{
    m_style.thickness = qBound(kMinThickness, thickness, kMaxThickness);
    m_toolbar->setThickness(m_style.thickness);
}

void Overlay::undo()
{
    if (m_document.undo())
        documentChanged();
}

void Overlay::redo()
{
    if (m_document.redo())
        documentChanged();
}

void Overlay::documentChanged()
{
    m_cacheValid = false;
    m_toolbar->setUndoRedoEnabled(m_document.canUndo(), m_document.canRedo());
    update();
}

void Overlay::updateToolbar()
{
    if (m_selection.isEmpty() || m_drag == Drag::Selecting) {
        m_toolbar->hide();
        return;
    }
    const QSize size = m_toolbar->sizeHint();
    m_toolbar->resize(size);
    m_toolbar->move(placeToolbar(m_selection, size, m_capture.screens));
    m_toolbar->show();
    m_toolbar->raise();
}

void Overlay::updateCursor(QPoint pos)
{
    if (m_selection.isEmpty()) {
        setCursor(Qt::CrossCursor);
        return;
    }
    switch (hitTestHandle(m_selection, pos, kHandleTolerance)) {
    case Handle::TopLeft:
    case Handle::BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        return;
    case Handle::TopRight:
    case Handle::BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        return;
    case Handle::Top:
    case Handle::Bottom:
        setCursor(Qt::SizeVerCursor);
        return;
    case Handle::Left:
    case Handle::Right:
        setCursor(Qt::SizeHorCursor);
        return;
    case Handle::Move:
        if (m_tool == Tool::None)
            setCursor(Qt::SizeAllCursor);
        else if (m_tool == Tool::Text)
            setCursor(Qt::IBeamCursor);
        else
            setCursor(Qt::CrossCursor);
        return;
    case Handle::None:
        setCursor(m_tool == Tool::None ? Qt::CrossCursor : Qt::ArrowCursor);
        return;
    }
}

QRect Overlay::screenAt(QPoint pos) const
{
    for (const QRect& s : m_capture.screens) {
        if (s.contains(pos))
            return s;
    }
    return {};
}

QRect Overlay::bounds() const
{
    return m_capture.image.rect();
}

// ---- Вывод ----

void Overlay::closeOverlay()
{
    releaseKeyboard();
    hide();
}

void Overlay::copyResult()
{
    if (m_selection.isEmpty())
        return;
    const QImage image = result();
    closeOverlay();
    emit copyRequested(image);
}

void Overlay::saveQuick()
{
    if (m_selection.isEmpty())
        return;
    QString path;
    const WriteResult r = quickSave(result(), m_saveDir, QDateTime::currentDateTime(), &path);
    if (r.status != WriteResult::Ok) {
        showError(r.error);
        return;
    }
    QTextStream(stdout) << path << Qt::endl;
    closeOverlay();
    emit finished();
}

void Overlay::saveAs()
{
    if (m_selection.isEmpty())
        return;
    const QImage image = result();
    closeOverlay(); // окно поверх всех: иначе диалог окажется под ним

    // Собственный диалог Qt: суффикс .png добавляется до вопроса о перезаписи.
    QFileDialog dialog(nullptr, QStringLiteral("Сохранить скриншот"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QStringLiteral("PNG (*.png)"));
    dialog.setDefaultSuffix(QStringLiteral("png"));
    dialog.setDirectory(m_saveDir);
    dialog.selectFile(quickSaveFileName(QDateTime::currentDateTime(), 0));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        start();
        return;
    }

    const QString path = dialog.selectedFiles().constFirst();
    const QByteArray png = encodePng(image);
    const WriteResult r = png.isEmpty()
        ? WriteResult{WriteResult::Error, QStringLiteral("не удалось закодировать PNG")}
        : writeFileAtomic(path, png, WriteMode::Replace);
    if (r.status != WriteResult::Ok) {
        showError(r.error);
        return;
    }
    QTextStream(stdout) << path << Qt::endl;
    emit finished();
}

void Overlay::cancel()
{
    closeOverlay();
    emit finished();
}

void Overlay::showError(const QString& text)
{
    closeOverlay();
    QMessageBox::critical(nullptr, QStringLiteral("hot-screenshot"), text);
    start();
}

// ---- Мышь и клавиатура ----

void Overlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint pos = event->position().toPoint();
    m_pressPos = pos;
    m_selectionAtPress = m_selection;

    if (m_selection.isEmpty()) {
        m_drag = Drag::Selecting;
        return;
    }
    const Handle handle = hitTestHandle(m_selection, pos, kHandleTolerance);
    if (handle == Handle::None) {
        // Вне выделения: без инструмента — новое выделение (аннотации остаются), с инструментом — ничего.
        if (m_tool == Tool::None) {
            m_drag = Drag::Selecting;
            setSelection(QRect());
        }
        return;
    }
    if (handle != Handle::Move) {
        m_drag = Drag::Resizing;
        m_handle = handle;
        return;
    }
    if (m_tool == Tool::None) {
        m_drag = Drag::Moving;
        m_handle = Handle::Move;
        return;
    }
}

void Overlay::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    switch (m_drag) {
    case Drag::None:
        updateCursor(pos);
        if (m_selection.isEmpty()) {
            const QRect screen = screenAt(pos);
            if (screen != m_hintScreen) {
                m_hintScreen = screen;
                update();
            }
        }
        return;
    case Drag::Selecting:
        setSelection(rectFromPoints(m_pressPos, pos).intersected(bounds()));
        return;
    case Drag::Moving:
    case Drag::Resizing:
        setSelection(applyHandleDrag(m_selectionAtPress, m_handle, pos - m_pressPos, bounds()));
        return;
    case Drag::Drawing:
        return;
    }
}

void Overlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint pos = event->position().toPoint();
    const Drag drag = m_drag;
    m_drag = Drag::None;
    m_handle = Handle::None;

    if (drag == Drag::Selecting) {
        // Клик без перетаскивания выделяет монитор под курсором (в мёртвой зоне — ничего).
        if ((pos - m_pressPos).manhattanLength() < kClickThreshold)
            setSelection(screenAt(pos));
        else
            setSelection(rectFromPoints(m_pressPos, pos).intersected(bounds()));
    }
    updateToolbar();
    updateCursor(pos);
}

void Overlay::wheelEvent(QWheelEvent* event)
{
    const int dy = event->angleDelta().y();
    if (dy != 0)
        setThickness(m_style.thickness + (dy > 0 ? 1 : -1));
    event->accept();
}

void Overlay::keyPressEvent(QKeyEvent* event)
{
    const Qt::KeyboardModifiers mods = event->modifiers()
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool ctrl = mods.testFlag(Qt::ControlModifier);
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    const int key = layoutIndependentKey(event->key(), event->nativeScanCode());

    if (key == Qt::Key_Escape) {
        cancel();
        return;
    }
    if (ctrl) {
        switch (key) {
        case Qt::Key_C:
            copyResult();
            return;
        case Qt::Key_S:
            if (shift)
                saveAs();
            else
                saveQuick();
            return;
        case Qt::Key_Z:
            if (shift)
                redo();
            else
                undo();
            return;
        case Qt::Key_Y:
            redo();
            return;
        case Qt::Key_A:
            setSelection(bounds());
            return;
        default:
            return;
        }
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        copyResult();
        return;
    }
    if (mods != Qt::NoModifier || m_selection.isEmpty())
        return;
    switch (key) {
    case Qt::Key_P: setTool(Tool::Pen); return;
    case Qt::Key_M: setTool(Tool::Marker); return;
    case Qt::Key_L: setTool(Tool::Line); return;
    case Qt::Key_A: setTool(Tool::Arrow); return;
    case Qt::Key_R: setTool(Tool::Rect); return;
    case Qt::Key_E: setTool(Tool::Ellipse); return;
    case Qt::Key_T: setTool(Tool::Text); return;
    case Qt::Key_N: setTool(Tool::Counter); return;
    case Qt::Key_B: setTool(Tool::Pixelate); return;
    case Qt::Key_V: setTool(Tool::None); return;
    default: return;
    }
}

// ---- Отрисовка ----

void Overlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawImage(0, 0, m_dimmed);
    if (m_selection.isEmpty()) {
        if (m_drag == Drag::None)
            paintHint(painter);
        return;
    }
    if (!m_cacheValid) {
        m_cache = render(m_capture.image, m_selection, m_document.annotations());
        m_cacheValid = true;
    }
    painter.drawImage(m_selection.topLeft(), m_cache);
    paintSelectionFrame(painter);
    paintSizeLabel(painter);
}

void Overlay::paintSelectionFrame(QPainter& painter) const
{
    const QRect& s = m_selection;
    painter.setPen(QPen(accentColor(), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(s.adjusted(0, 0, -1, -1));

    const QPoint handles[] = {
        s.topLeft(), QPoint(s.center().x(), s.top()), s.topRight(), QPoint(s.right(), s.center().y()),
        s.bottomRight(), QPoint(s.center().x(), s.bottom()), s.bottomLeft(), QPoint(s.left(), s.center().y()),
    };
    painter.setPen(Qt::NoPen);
    painter.setBrush(accentColor());
    for (const QPoint& p : handles)
        painter.drawRect(QRect(p.x() - kHandleSize / 2, p.y() - kHandleSize / 2, kHandleSize, kHandleSize));
}

void Overlay::paintSizeLabel(QPainter& painter) const
{
    const QString text = QStringLiteral("%1×%2").arg(m_selection.width()).arg(m_selection.height());
    const QFontMetrics metrics(painter.font());
    const QSize size(metrics.horizontalAdvance(text) + 12, metrics.height() + 6);
    // Над выделением, если там есть видимая часть монитора; иначе внутри.
    const QRect screen = screenAt(m_selection.topLeft());
    const int minY = screen.isEmpty() ? 0 : screen.top();
    QPoint topLeft(m_selection.left(), m_selection.top() - size.height() - 4);
    if (topLeft.y() < minY)
        topLeft = m_selection.topLeft() + QPoint(4, 4);
    const QRect box(topLeft, size);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRoundedRect(box, 4, 4);
    painter.setPen(Qt::white);
    painter.drawText(box, Qt::AlignCenter, text);
}

void Overlay::paintHint(QPainter& painter) const
{
    QRect screen = m_hintScreen;
    if (screen.isEmpty())
        screen = m_capture.screens.isEmpty() ? rect() : m_capture.screens.constFirst();
    const QString text = QStringLiteral(
        "Выделите область мышью · клик — весь монитор · Ctrl+A — все мониторы · Esc — отмена");
    QFont font = painter.font();
    font.setPixelSize(18);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const QSize size(metrics.horizontalAdvance(text) + 40, metrics.height() + 24);
    const QRect box(screen.center() - QPoint(size.width() / 2, size.height() / 2), size);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(box, 8, 8);
    painter.setPen(Qt::white);
    painter.drawText(box, Qt::AlignCenter, text);
}
```

- [ ] **Step 6: Тесты проходят** — обе сборки. Expected: PASS, включая все ранее написанные наборы.

- [ ] **Step 7: Commit**
```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/test_keys.cpp tests/test_overlay.cpp src/keys.h src/keys.cpp src/overlay.h src/overlay.cpp
git commit -m "feat: оверлей — выделение, клавиши, копирование и сохранение" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Оверлей — рисование и ввод текста

**Files:**
- Modify: `src/overlay.h`, `src/overlay.cpp`
- Test: `tests/test_overlay.cpp` (новые слоты в `TestOverlay`)

**Interfaces:**
- Consumes: `Overlay` из Task 9, `drawAnnotation`/`textFont` (Task 5), `snapLine45`/`snapSquare`/`rectFromPoints` (Task 1).
- Produces: `bool Overlay::isEditingText() const`.

Правила (из спецификации, раздел 6 «overlay»):
- Нажатие внутри выделения с рисующим инструментом начинает аннотацию. Pen/Marker копят точки (при сдвиге ≥ 1 px), двухточечные инструменты обновляют конец; Shift привязывает (Line/Arrow — 45°, Rect/Ellipse/Pixelate — квадрат).
- Не добавляются: Marker из одной точки, Line/Arrow нулевой длины, Rect/Ellipse/Pixelate со стороной < 2.
- Counter добавляется сразу по нажатию.
- Text: нажатие открывает ввод. Печатные символы дописываются, Backspace удаляет последний, Enter — перевод строки. Esc, клик, смена инструмента, undo/redo, копирование/сохранение и любое сочетание с Ctrl сначала завершают текст; пустой текст отбрасывается. Esc во время ввода **только** завершает текст.
- Смена цвета и толщины во время ввода применяется к вводимому тексту.

- [ ] **Step 1: Падающие тесты** — добавить в класс `TestOverlay` в `tests/test_overlay.cpp` (перед закрывающей `};`):

```cpp
    void drawRectAddsAnnotation()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_R);
        drag(&e.ov, {50, 50}, {150, 120});
        QCOMPARE(e.ov.document().annotations().size(), 1);
        const Annotation& a = e.ov.document().annotations().at(0);
        QCOMPARE(a.tool, Tool::Rect);
        QCOMPARE(a.points, (QVector<QPoint>{{50, 50}, {150, 120}}));
        QCOMPARE(a.style.color, Settings().color);
        QCOMPARE(e.ov.result().pixel(40, 75), Settings().color.rgb()); // левая сторона x=50
        QVERIFY(e.ov.toolbar()->findChild<QToolButton*>(QStringLiteral("undo"))->isEnabled());
    }

    void shiftMakesSquare()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_R);
        drag(&e.ov, {50, 50}, {150, 120}, Qt::ShiftModifier);
        QCOMPARE(e.ov.document().annotations().at(0).points.at(1), QPoint(150, 150));
    }

    void shiftSnapsArrow()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_A);
        drag(&e.ov, {50, 50}, {150, 58}, Qt::ShiftModifier);
        QCOMPARE(e.ov.document().annotations().at(0).points.at(1), QPoint(150, 50));
    }

    void tinyShapesIgnored()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_R);
        click(&e.ov, {50, 50});
        QTest::keyClick(&e.ov, Qt::Key_M);
        click(&e.ov, {60, 60});
        QTest::keyClick(&e.ov, Qt::Key_L);
        click(&e.ov, {70, 70});
        QVERIFY(e.ov.document().annotations().isEmpty());
    }

    void penStroke()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_P);
        drag(&e.ov, {50, 50}, {100, 80});
        QCOMPARE(e.ov.document().annotations().size(), 1);
        QCOMPARE(e.ov.document().annotations().at(0).tool, Tool::Pen);
        QCOMPARE(e.ov.document().annotations().at(0).points.size(), 3);
    }

    void drawingOutsideSelectionIgnored()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_R);
        drag(&e.ov, {350, 250}, {380, 280});
        QVERIFY(e.ov.document().annotations().isEmpty());
        QCOMPARE(e.ov.selection(), QRect(10, 10, 291, 191));
    }

    void undoRedoKeys()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_R);
        drag(&e.ov, {50, 50}, {150, 120});
        QTest::keyClick(&e.ov, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(e.ov.document().annotations().isEmpty());
        QTest::keyClick(&e.ov, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(e.ov.document().annotations().size(), 1);
        QTest::keyClick(&e.ov, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(&e.ov, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(e.ov.document().annotations().size(), 1);
    }

    void counterClicks()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_N);
        click(&e.ov, {60, 60});
        click(&e.ov, {80, 80});
        QCOMPARE(e.ov.document().annotations().size(), 2);
        QCOMPARE(e.ov.document().annotations().at(1).tool, Tool::Counter);
        QCOMPARE(e.ov.document().nextCounterNumber(), 3);
    }

    void textEscapeKeepsOverlay()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QVERIFY(e.ov.isEditingText());
        QTest::keyClicks(&e.ov, QStringLiteral("ab"));
        QTest::keyClick(&e.ov, Qt::Key_Backspace);
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QVERIFY(!e.ov.isEditingText());
        QVERIFY(!e.finished);
        QVERIFY(e.ov.isVisible());
        QCOMPARE(e.ov.document().annotations().size(), 1);
        QCOMPARE(e.ov.document().annotations().at(0).text, QStringLiteral("a"));
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QVERIFY(e.finished);
    }

    void textEnterIsNewline()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClicks(&e.ov, QStringLiteral("a"));
        QTest::keyClick(&e.ov, Qt::Key_Return);
        QTest::keyClicks(&e.ov, QStringLiteral("b"));
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QVERIFY(!e.wasCopied);
        QCOMPARE(e.ov.document().annotations().at(0).text, QStringLiteral("a\nb"));
    }

    void textLettersDontSwitchTool()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClicks(&e.ov, QStringLiteral("pmv"));
        QCOMPARE(e.ov.tool(), Tool::Text);
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QCOMPARE(e.ov.document().annotations().at(0).text, QStringLiteral("pmv"));
    }

    void textCyrillic()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        rawKey(&e.ov, 0x0416, Qt::NoModifier, 47, QStringLiteral("ж"));
        rawKey(&e.ov, 0x0417, Qt::NoModifier, 33, QStringLiteral("з")); // физическая P — не инструмент
        QCOMPARE(e.ov.tool(), Tool::Text);
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QCOMPARE(e.ov.document().annotations().at(0).text, QStringLiteral("жз"));
    }

    void emptyTextDiscarded()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QVERIFY(e.ov.document().annotations().isEmpty());
        QVERIFY(!e.finished);
    }

    void clickCommitsTextAndStartsNew()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClicks(&e.ov, QStringLiteral("a"));
        click(&e.ov, {60, 120});
        QVERIFY(e.ov.isEditingText());
        QTest::keyClicks(&e.ov, QStringLiteral("b"));
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QCOMPARE(e.ov.document().annotations().size(), 2);
        QCOMPARE(e.ov.document().annotations().at(1).points.at(0), QPoint(60, 120));
    }

    void ctrlCCommitsText()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClicks(&e.ov, QStringLiteral("hi"));
        QTest::keyClick(&e.ov, Qt::Key_C, Qt::ControlModifier);
        QVERIFY(e.wasCopied);
        QCOMPARE(e.ov.document().annotations().at(0).text, QStringLiteral("hi"));
    }

    void toolSwitchCommitsText()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClicks(&e.ov, QStringLiteral("x"));
        e.ov.toolbar()->findChild<QToolButton*>(QStringLiteral("tool-rect"))->click();
        QVERIFY(!e.ov.isEditingText());
        QCOMPARE(e.ov.tool(), Tool::Rect);
        QCOMPARE(e.ov.document().annotations().size(), 1);
    }
```

- [ ] **Step 2: Убедиться, что падают.** Expected: FAIL — нет `isEditingText`, аннотации не добавляются.

- [ ] **Step 3: `src/overlay.h`** — добавить `#include <optional>` к включениям; в `public:` после `toolbar()`:

```cpp
    bool isEditingText() const { return m_textEditing; }
```

в `private:` после `bounds()`:

```cpp
    void beginAnnotation(QPoint pos);
    void finishAnnotation();
    void commitText();                    // завершить ввод текста; пустой текст отбрасывается
    bool handleTextKey(QKeyEvent* event); // true — клавиша поглощена вводом текста
    void paintCurrent(QPainter& painter) const;
```

и в поля после `m_selectionAtPress`:

```cpp
    std::optional<Annotation> m_current; // рисуемая аннотация или вводимый текст
    bool m_textEditing = false;
```

- [ ] **Step 4: `src/overlay.cpp`** — изменить существующие функции.

`setTool`, `setColor`, `setThickness`, `undo`, `redo` заменить целиком:
```cpp
void Overlay::setTool(Tool tool)
{
    commitText();
    m_tool = tool;
    m_toolbar->setTool(tool);
    updateCursor(mapFromGlobal(QCursor::pos()));
}

void Overlay::setColor(const QColor& color)
{
    m_style.color = color;
    m_toolbar->setColor(color);
    if (m_textEditing)
        m_current->style.color = color;
    update();
}

void Overlay::setThickness(int thickness)
{
    m_style.thickness = qBound(kMinThickness, thickness, kMaxThickness);
    m_toolbar->setThickness(m_style.thickness);
    if (m_textEditing)
        m_current->style.thickness = m_style.thickness;
    update();
}

void Overlay::undo()
{
    commitText();
    if (m_document.undo())
        documentChanged();
}

void Overlay::redo()
{
    commitText();
    if (m_document.redo())
        documentChanged();
}
```

В начало `copyResult`, `saveQuick`, `saveAs` (первой строкой тела, до проверки выделения) добавить:
```cpp
    commitText();
```

В `mousePressEvent` сразу после проверки `event->button()` добавить:
```cpp
    commitText(); // клик в любом месте завершает вводимый текст
```
а последний блок функции заменить так, чтобы он заканчивался вызовом `beginAnnotation`:
```cpp
    if (m_tool == Tool::None) {
        m_drag = Drag::Moving;
        m_handle = Handle::Move;
        return;
    }
    beginAnnotation(pos);
}
```

В `mouseMoveEvent` ветку `case Drag::Drawing:` заменить на:
```cpp
    case Drag::Drawing: {
        if (!m_current)
            return;
        Annotation& a = *m_current;
        if (a.tool == Tool::Pen || a.tool == Tool::Marker) {
            if (pos != a.points.constLast())
                a.points.append(pos);
        } else {
            QPoint end = pos;
            if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                const bool line = a.tool == Tool::Line || a.tool == Tool::Arrow;
                end = line ? snapLine45(a.points.at(0), pos) : snapSquare(a.points.at(0), pos);
            }
            a.points[1] = end;
        }
        update();
        return;
    }
```

В `mouseReleaseEvent` после блока `if (drag == Drag::Selecting) { ... }` добавить:
```cpp
    else if (drag == Drag::Drawing)
        finishAnnotation();
```

В начало `keyPressEvent`:
```cpp
    if (handleTextKey(event))
        return;
```

В `paintEvent` после `painter.drawImage(m_selection.topLeft(), m_cache);`:
```cpp
    paintCurrent(painter);
```

- [ ] **Step 5: `src/overlay.cpp`** — новые функции (после `bounds()`):

```cpp
// ---- Рисование ----

void Overlay::beginAnnotation(QPoint pos)
{
    Annotation a;
    a.tool = m_tool;
    a.style = m_style;
    a.points = {pos};
    switch (m_tool) {
    case Tool::Counter:
        m_document.add(a);
        documentChanged();
        return;
    case Tool::Text:
        m_current = a;
        m_textEditing = true;
        update();
        return;
    case Tool::Line:
    case Tool::Arrow:
    case Tool::Rect:
    case Tool::Ellipse:
    case Tool::Pixelate:
        a.points.append(pos); // [начало, конец]
        break;
    case Tool::Pen:
    case Tool::Marker:
    case Tool::None:
        break;
    }
    m_current = a;
    m_drag = Drag::Drawing;
    update();
}

void Overlay::finishAnnotation()
{
    if (!m_current)
        return;
    const Annotation a = *m_current;
    m_current.reset();

    bool valid = false;
    switch (a.tool) {
    case Tool::Pen:
        valid = !a.points.isEmpty();
        break;
    case Tool::Marker:
        valid = a.points.size() >= 2;
        break;
    case Tool::Line:
    case Tool::Arrow:
        valid = a.points.at(0) != a.points.at(1);
        break;
    case Tool::Rect:
    case Tool::Ellipse:
    case Tool::Pixelate: {
        const QRect r = rectFromPoints(a.points.at(0), a.points.at(1));
        valid = r.width() >= 2 && r.height() >= 2;
        break;
    }
    case Tool::None:
    case Tool::Text:
    case Tool::Counter:
        break;
    }
    if (valid) {
        m_document.add(a);
        documentChanged();
    } else {
        update();
    }
}

void Overlay::commitText()
{
    if (!m_textEditing)
        return;
    m_textEditing = false;
    const Annotation a = *m_current;
    m_current.reset();
    if (!a.text.isEmpty()) {
        m_document.add(a);
        documentChanged();
    } else {
        update();
    }
}

bool Overlay::handleTextKey(QKeyEvent* event)
{
    if (!m_textEditing)
        return false;
    if (event->key() == Qt::Key_Escape) {
        commitText();
        return true;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        commitText();
        return false; // сочетание обработает keyPressEvent
    }
    switch (event->key()) {
    case Qt::Key_Backspace:
        m_current->text.chop(1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_current->text += QLatin1Char('\n');
        break;
    default: {
        const QString text = event->text();
        if (!text.isEmpty() && text.at(0).isPrint())
            m_current->text += text;
        break;
    }
    }
    update();
    return true;
}

void Overlay::paintCurrent(QPainter& painter) const
{
    if (!m_current)
        return;
    painter.save();
    painter.setClipRect(m_selection);
    drawAnnotation(painter, *m_current, m_document.nextCounterNumber());
    if (m_textEditing) {
        // Курсор в конце последней строки, по тем же метрикам, что и drawAnnotation.
        const QFontMetrics metrics(textFont(m_current->style.thickness));
        const QStringList lines = m_current->text.split(QLatin1Char('\n'));
        const QPoint origin = m_current->points.constFirst();
        const int x = origin.x() + metrics.horizontalAdvance(lines.constLast());
        const int y = origin.y() + int(lines.size() - 1) * metrics.lineSpacing();
        painter.setPen(QPen(m_current->style.color, 2));
        painter.drawLine(x, y, x, y + metrics.height());
    }
    painter.restore();
}
```

- [ ] **Step 6: Тесты проходят** — обе сборки. Expected: PASS, все наборы.

- [ ] **Step 7: Commit**
```bash
git add src/overlay.h src/overlay.cpp tests/test_overlay.cpp
git commit -m "feat: рисование и ввод текста в оверлее" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Приложение, ярлык GNOME, README

**Files:**
- Create: `src/main.cpp`, `scripts/set-gnome-shortcut.sh`, `README.md`
- Modify: `CMakeLists.txt` (исполняемый файл и установка)

**Interfaces:**
- Consumes: `InstanceLock`/`defaultLockPath` (Task 3), `captureAllScreens` (Task 2), `Settings` (Task 7), `screenshotsDir`/`copyToClipboard` (Task 6), `Overlay` (Task 9–10).
- Produces: исполняемый файл `hot-screenshot` с `--help` и `--version`.

- [ ] **Step 1: `CMakeLists.txt`** — после `target_link_libraries(hs_core ...)` добавить:

```cmake
add_executable(hot-screenshot src/main.cpp)
target_link_libraries(hot-screenshot PRIVATE hs_core)
target_compile_definitions(hot-screenshot PRIVATE HS_VERSION="${PROJECT_VERSION}")
install(TARGETS hot-screenshot DESTINATION bin)
```

- [ ] **Step 2: `src/main.cpp`**

```cpp
#include "capture.h"
#include "instancelock.h"
#include "output.h"
#include "overlay.h"
#include "settings.h"

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>

namespace {
void saveStyle(const Overlay& overlay)
{
    Settings settings;
    settings.color = overlay.style().color;
    settings.thickness = overlay.style().thickness;
    settings.save();
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("hot-screenshot"));
    QApplication::setApplicationVersion(QStringLiteral(HS_VERSION));
    // После Ctrl+C окно закрыто, но процесс должен жить, пока владеет буфером обмена.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Скриншот области экрана с рисованием"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    InstanceLock lock;
    const QString lockPath = defaultLockPath();
    if (lockPath.isEmpty()) {
        qWarning("XDG_RUNTIME_DIR не задан — работаю без блокировки экземпляра");
    } else {
        switch (lock.tryAcquire(lockPath)) {
        case InstanceLock::Result::Acquired:
            break;
        case InstanceLock::Result::Busy:
            return 0; // оверлей уже открыт
        case InstanceLock::Result::Error:
            qWarning("не удалось взять блокировку %s — работаю без неё", qPrintable(lockPath));
            break;
        }
    }

    std::optional<Capture> capture = captureAllScreens();
    if (!capture) {
        qCritical("не удалось снять экран");
        return 1;
    }

    Overlay overlay(std::move(*capture), Settings::load(), screenshotsDir());

    QObject::connect(&overlay, &Overlay::finished, &app, [&overlay] {
        saveStyle(overlay);
        QApplication::quit();
    });
    QObject::connect(&overlay, &Overlay::copyRequested, &app, [&overlay, &lock](const QImage& image) {
        saveStyle(overlay);
        lock.release(); // следующий хоткей может открыть новый оверлей, пока мы держим буфер
        copyToClipboard(image);
        // На X11 данные буфера отдаёт процесс-владелец: живём, пока буфер не займёт кто-то другой.
        QClipboard* clipboard = QGuiApplication::clipboard();
        QObject::connect(clipboard, &QClipboard::changed, clipboard, [clipboard](QClipboard::Mode mode) {
            if (mode == QClipboard::Clipboard && !clipboard->ownsClipboard())
                QApplication::quit();
        });
    });

    overlay.start();
    return app.exec();
}
```

- [ ] **Step 3: Сборка и безопасная проверка запуска** (оверлей не открывается):

Run:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j
./build/hot-screenshot --version
./build/hot-screenshot --help
```
Expected: `hot-screenshot 0.1.0`; справка с описанием «Скриншот области экрана с рисованием». Больше `hot-screenshot` не запускать.

- [ ] **Step 4: `scripts/set-gnome-shortcut.sh`** (сделать исполняемым: `chmod +x`):

```bash
#!/usr/bin/env bash
# Переназначает существующий ярлык GNOME custom0 (сейчас Shift+Super+S → gnome-screenshot -a)
# на hot-screenshot. Запускает только пользователь. Печатает прежние значения для отката.
set -euo pipefail

BIN="${1:-$HOME/.local/bin/hot-screenshot}"
if [[ ! -x "$BIN" ]]; then
    echo "Не найден исполняемый файл: $BIN" >&2
    exit 1
fi
BIN="$(realpath "$BIN")"
if [[ "$BIN" == *"'"* ]]; then
    echo "Путь не должен содержать апостроф: $BIN" >&2
    exit 1
fi

LIST_SCHEMA=org.gnome.settings-daemon.plugins.media-keys
SCHEMA=org.gnome.settings-daemon.plugins.media-keys.custom-keybinding
KPATH=/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom0/

if ! gsettings get "$LIST_SCHEMA" custom-keybindings | grep -qF "$KPATH"; then
    echo "Ярлык custom0 не найден в custom-keybindings — создайте ярлык в настройках GNOME вручную." >&2
    exit 1
fi

echo "Прежние значения (для отката через gsettings set):"
for key in name command binding; do
    printf '  %s = %s\n' "$key" "$(gsettings get "$SCHEMA:$KPATH" "$key")"
done

gsettings set "$SCHEMA:$KPATH" name "'hot-screenshot'"
gsettings set "$SCHEMA:$KPATH" command "'$BIN'"
echo "Готово: $(gsettings get "$SCHEMA:$KPATH" binding) → $BIN"
```

Проверить только синтаксис: `bash -n scripts/set-gnome-shortcut.sh` → без вывода. Сам скрипт **не запускать**.

- [ ] **Step 5: `README.md`**

````markdown
# hot-screenshot

Скриншот области экрана с рисованием для Ubuntu (X11). По образцу Flameshot, но с маленькой
кодовой базой, которую можно прочитать целиком.

## Сборка и установка

```bash
sudo apt install qt6-base-dev cmake g++
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build --prefix ~/.local
scripts/set-gnome-shortcut.sh   # Shift+Super+S → hot-screenshot
```

Проверка под санитайзерами:

```bash
cmake -S . -B build-asan -DHS_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
```

## Управление

| Клавиша | Действие |
|---|---|
| мышь | выделить область; клик — весь монитор |
| Ctrl+A | все мониторы |
| P M L A R E T N B | карандаш, маркер, линия, стрелка, прямоугольник, эллипс, текст, номерок, пикселизация |
| V | перемещать и растягивать выделение |
| Shift при рисовании | 45° для линий, квадрат/круг для фигур |
| колесо | толщина |
| Ctrl+Z / Ctrl+Shift+Z | отменить / повторить |
| Ctrl+C или Enter | копировать в буфер |
| Ctrl+S | сохранить в ~/Pictures/Screenshots |
| Ctrl+Shift+S | сохранить как… |
| Esc | завершить текст / закрыть |

Клавиши работают в любой раскладке.

## Модель угроз и ограничения

- Нет сети, D-Bus, сокетов и других точек входа: процесс ничего не слушает.
- Файлы пишутся атомарно: временный файл `0600` в каталоге назначения, затем `renameat2`.
  Путь назначения не открывается напрямую, поэтому подложенный симлинк не перенаправит запись.
  Быстрое сохранение никогда не перезаписывает существующие файлы.
- Сохранённые скриншоты доступны только владельцу (`0600`).
- После копирования процесс живёт невидимым, пока буфер обмена не займёт другая программа:
  на X11 данные буфера отдаёт процесс-владелец.
- Пикселизация усредняет блоки от 12 px и необратима на практике, но для известного шрифта
  существуют атаки восстановления (Depix). Пароли надёжнее не захватывать вовсе.
- Только X11 и масштаб 1×. Wayland и HiDPI не поддерживаются.
````

- [ ] **Step 6: Полная проверка** — обе команды из Global Constraints и `bash -n scripts/set-gnome-shortcut.sh`.
Expected: сборка без предупреждений, `100% tests passed` в обеих сборках. Если LSan сообщает об утечках внутри системных библиотек (fontconfig, Qt-плагины), добавить в `tests/lsan.supp` узкое правило с комментарием, какой тест и какой стек его потребовал. Кадры из `src/` подавлять нельзя — такие утечки исправляются.

- [ ] **Step 7: Commit**
```bash
git add CMakeLists.txt src/main.cpp scripts/set-gnome-shortcut.sh README.md tests/lsan.supp
git commit -m "feat: приложение, ярлык GNOME и README" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 8: Ручная проверка (выполняет пользователь вместе с ревьюером, не агент)** — раздел 9 спецификации:
  1. `./build/hot-screenshot` из терминала: оверлей на всех трёх мониторах, снимок совпадает с экраном, панели GNOME перекрыты.
  2. Выделение через границу мониторов; клик выделяет монитор; Ctrl+A — всё.
  3. Клавиатура работает сразу, без клика по окну, в том числе в русской раскладке.
  4. Все 9 инструментов, цвета, колесо, Shift, undo/redo.
  5. Текст по-русски и по-английски.
  6. Ctrl+C → вставка в Telegram/браузер/GIMP. `pgrep hot-screenshot` показывает процесс, пока в буфер не скопировано другое, затем процесс исчезает.
  7. Ctrl+S → файл в `~/Pictures/Screenshots`, `stat -c %a` = `600`. Ctrl+Shift+S → диалог виден поверх, отмена возвращает в оверлей.
  8. Повторный запуск при открытом оверлее ничего не делает.
  9. От нажатия до оверлея субъективно ≤ 0,3 с.
  10. `cmake --install build --prefix ~/.local` и `scripts/set-gnome-shortcut.sh` — запускает пользователь.
