# pinch под Wayland — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** pinch снимает экран и показывает оверлей в Wayland-сеансах GNOME, KDE Plasma, Hyprland и Sway, сохраняя работу в X11.

**Architecture:** Слой выбора метода снимка (`capturebackend`) с бэкендами X11 / wlr-screencopy / KWin ScreenShot2 / xdg-desktop-portal. Чистые преобразования сырых буферов вынесены в `rawimage`. Оверлей разделяется на `OverlayController` (вся логика, координаты изображения) и `ScreenView` (окно на монитор); `OverlaySession` создаёт и показывает окна для X11 и Wayland. D-Bus-бэкенды тестируются против поддельных сервисов на приватной шине `dbus-daemon`.

**Tech Stack:** C++17, Qt 6.4.2+ (Widgets, DBus, Test), libwayland-client + wayland-scanner, CMake ≥ 3.16.

**Spec:** `docs/superpowers/specs/2026-09-30-pinch-wayland-design.md` (главная) и базовая `docs/superpowers/specs/2026-09-30-hot-screenshot-design.md`.

**Отступление от шаблона:** для чистых модулей план даёт полный код; для кода, завязанного на протоколы Wayland/D-Bus и для разделения оверлея, план задаёт интерфейсы, точные правила и полные тесты, а реализацию пишет исполнитель (так надёжнее, чем переписывать ~700 строк существующего оверлея в тексте плана).

## Global Constraints

- Ubuntu 24.04 (Qt 6.4.2) — машина разработки; код также должен собираться на Arch (новые Qt/GCC). Не использовать API Qt новее 6.4.
- Новые зависимости только: `wayland-client` (pkg-config), `wayland-scanner`, `Qt6::DBus`. Никаких других библиотек (без layer-shell-qt, без KF6).
- `-Wall -Wextra -Wpedantic -Werror` чисто (опция `PINCH_WERROR`); обе сборки: `build/` и `build-asan/` (`-DPINCH_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug`).
- Комментарии — по-русски, идентификаторы — по-английски, все пользовательские тексты — ключи `qtTrId("...")` с записями в обеих таблицах `translations/pinch_{en,ru}.ts` (язык из системы, запасной — английский); новые тексты задач Wayland получают ключи в обеих таблицах.
- Все координаты — координаты изображения (логические, из `QScreen::geometry()`), как в базовой спецификации.
- Никаких своих D-Bus-сервисов, слушающих сокетов, сети; файлы pinch — только через `writeFileAtomic` (0600).
- Агенты не запускают `pinch` без `--help`/`--version`, не выполняют `scripts/set-gnome-shortcut.sh`, не делают `gsettings set`, `sudo`, `cmake --install` вне `/tmp/claude-1000/`.
- Коммит в конце задачи; сообщение оканчивается ровно `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Команды проверки:
  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j && ctest --test-dir build --output-on-failure
  cmake -S . -B build-asan -DPINCH_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
  ```

## Review Focus

1. **Буфер обмена под Wayland** назначается, пока окно в фокусе: `copyRequested` испускается до скрытия окон, `main` кладёт картинку в буфер сразу — Задача 6 (`copyEmittedBeforeHide`).
2. **Выделение через границу мониторов** при окне на каждый монитор: перетаскивание, начатое в одном окне, продолжает выделение на соседнем — Задача 6 (`selectionAcrossViews`).
3. **Файл портала**: удаляется только обычный свой свежий файл; симлинк, старый или чужой файл не трогаются — Задача 5 (`safeDelete*`).
4. **Недоверие к ответам композитора/KWin**: неизвестный формат, короткие данные, неверный stride → ошибка метода и откат на следующий, а не падение — Задачи 2, 4 (`rejects*`).
5. **Отказ всех методов** → понятное русское сообщение со списком причин и выход 1, без пустого оверлея — Задача 1 (`allMethodsFailed` через `captureScreens`), Задача 7 (main).

Не покрывается автотестами: реальные композиторы (GNOME/KDE/Hyprland/Sway), показ полноэкранных окон на нужных мониторах, фокус клавиатуры под Wayland — ручной чек-лист в README (Задача 7).

## Файлы

| Файл | Ответственность |
|---|---|
| `src/capturebackend.{h,cpp}` | `CaptureMethod`, `CaptureEnvironment`, `captureOrder`, `detectEnvironment`, `captureScreens` |
| `src/rawimage.{h,cpp}` | сырые буферы screencopy/KWin → `QImage`; сопоставление мониторов по имени |
| `src/screencopy.{h,cpp}`, `protocols/wlr-screencopy-unstable-v1.xml` | бэкенд wlr-screencopy |
| `src/kwincapture.{h,cpp}` | бэкенд KWin ScreenShot2 |
| `src/portalcapture.{h,cpp}` | бэкенд xdg-desktop-portal, безопасное удаление файла |
| `src/overlaycontroller.{h,cpp}`, `src/screenview.{h,cpp}`, `src/overlaysession.{h,cpp}` | оверлей: логика, окна, показ (заменяют `overlay.{h,cpp}`) |
| `tests/fakes/fake_kwin.cpp`, `tests/fakes/fake_portal.cpp`, `tests/dbustestbus.h` | поддельные D-Bus-сервисы и запуск приватной шины |
| `data/pinch.desktop.in` | шаблон `.desktop` с абсолютным `Exec` и ключом KWin |

---

### Task 1: Выбор метода снимка

**Files:**
- Create: `src/capturebackend.h`, `src/capturebackend.cpp`, `tests/test_capturebackend.cpp`
- Modify: `CMakeLists.txt` (`pinch_core` += новые файлы), `tests/CMakeLists.txt` (`pinch_add_test(test_capturebackend)`), `src/main.cpp`

**Interfaces:**
- Consumes: `Capture`, `captureAllScreens()` (capture.h).
- Produces:
  ```cpp
  enum class CaptureMethod { X11, Screencopy, KWin, Portal };
  struct CaptureEnvironment {
      QString platformName;            // QGuiApplication::platformName()
      bool hasScreencopy = false;      // Задача 3 заполняет
      bool hasKWinScreenShot2 = false; // Задача 4 заполняет
      QString forced;                  // PINCH_CAPTURE
  };
  QVector<CaptureMethod> captureOrder(const CaptureEnvironment& env);
  QString captureMethodName(CaptureMethod m);          // "x11" | "screencopy" | "kwin" | "portal"
  CaptureEnvironment detectEnvironment();              // пока: platformName + forced
  // Пробует методы из captureOrder; errors получает строки «<метод>: <причина>» по-русски.
  // Методы, ещё не реализованные в сборке, дают ошибку «метод недоступен в этой сборке».
  std::optional<Capture> captureScreens(const CaptureEnvironment& env, QStringList* errors);
  ```
  Для тестов `captureScreens` принимает внедряемую таблицу функций:
  ```cpp
  using CaptureFn = std::function<std::optional<Capture>(QString* error)>;
  std::optional<Capture> captureScreensWith(const QVector<CaptureMethod>& order,
                                            const QMap<CaptureMethod, CaptureFn>& fns, QStringList* errors);
  ```
  `captureScreens` = `captureScreensWith(captureOrder(env), <реальные функции>, errors)`. Неизвестное значение `forced` → `captureOrder` пуст → `captureScreens` пишет в errors «неизвестный метод PINCH_CAPTURE=<значение>».

Правила `captureOrder`: `forced` ∈ {x11, screencopy, kwin, portal} → только этот метод; иное непустое `forced` → пусто; `platformName == "wayland"` → `[Screencopy если hasScreencopy] + [KWin если hasKWinScreenShot2] + [Portal]`; любая другая платформа (`xcb`, `offscreen`, …) → `{X11}`.

- [ ] **Step 1: Падающий тест** `tests/test_capturebackend.cpp`:

```cpp
#include <QtTest>

#include "capturebackend.h"

namespace {
CaptureEnvironment env(const char* platform, bool screencopy = false, bool kwin = false, const char* forced = "")
{
    CaptureEnvironment e;
    e.platformName = QString::fromLatin1(platform);
    e.hasScreencopy = screencopy;
    e.hasKWinScreenShot2 = kwin;
    e.forced = QString::fromLatin1(forced);
    return e;
}

Capture fakeCapture()
{
    Capture c;
    c.image = QImage(10, 10, QImage::Format_RGB32);
    c.image.fill(Qt::red);
    c.screens = {QRect(0, 0, 10, 10)};
    return c;
}
}

class TestCaptureBackend : public QObject {
    Q_OBJECT
private slots:
    void x11UsesGrab()
    {
        QCOMPARE(captureOrder(env("xcb")), (QVector<CaptureMethod>{CaptureMethod::X11}));
        QCOMPARE(captureOrder(env("offscreen")), (QVector<CaptureMethod>{CaptureMethod::X11}));
    }

    void waylandOrder()
    {
        QCOMPARE(captureOrder(env("wayland")), (QVector<CaptureMethod>{CaptureMethod::Portal}));
        QCOMPARE(captureOrder(env("wayland", true)),
                 (QVector<CaptureMethod>{CaptureMethod::Screencopy, CaptureMethod::Portal}));
        QCOMPARE(captureOrder(env("wayland", false, true)),
                 (QVector<CaptureMethod>{CaptureMethod::KWin, CaptureMethod::Portal}));
        QCOMPARE(captureOrder(env("wayland", true, true)),
                 (QVector<CaptureMethod>{CaptureMethod::Screencopy, CaptureMethod::KWin, CaptureMethod::Portal}));
    }

    void forcedMethod()
    {
        QCOMPARE(captureOrder(env("wayland", true, true, "portal")), (QVector<CaptureMethod>{CaptureMethod::Portal}));
        QCOMPARE(captureOrder(env("xcb", false, false, "kwin")), (QVector<CaptureMethod>{CaptureMethod::KWin}));
        QVERIFY(captureOrder(env("wayland", true, true, "bogus")).isEmpty());
    }

    void methodNames()
    {
        QCOMPARE(captureMethodName(CaptureMethod::X11), QStringLiteral("x11"));
        QCOMPARE(captureMethodName(CaptureMethod::Screencopy), QStringLiteral("screencopy"));
        QCOMPARE(captureMethodName(CaptureMethod::KWin), QStringLiteral("kwin"));
        QCOMPARE(captureMethodName(CaptureMethod::Portal), QStringLiteral("portal"));
    }

    void firstSuccessWins()
    {
        QStringList calls;
        QMap<CaptureMethod, CaptureFn> fns;
        fns[CaptureMethod::Screencopy] = [&](QString* error) -> std::optional<Capture> {
            calls << QStringLiteral("screencopy");
            *error = QStringLiteral("нет доступа");
            return std::nullopt;
        };
        fns[CaptureMethod::Portal] = [&](QString*) -> std::optional<Capture> {
            calls << QStringLiteral("portal");
            return fakeCapture();
        };
        QStringList errors;
        const auto c = captureScreensWith({CaptureMethod::Screencopy, CaptureMethod::Portal}, fns, &errors);
        QVERIFY(c.has_value());
        QCOMPARE(calls, (QStringList{QStringLiteral("screencopy"), QStringLiteral("portal")}));
        QCOMPARE(errors, (QStringList{QStringLiteral("screencopy: нет доступа")}));
    }

    void allMethodsFailed()
    {
        QMap<CaptureMethod, CaptureFn> fns;
        fns[CaptureMethod::Portal] = [](QString* error) -> std::optional<Capture> {
            *error = QStringLiteral("отказ");
            return std::nullopt;
        };
        QStringList errors;
        QVERIFY(!captureScreensWith({CaptureMethod::KWin, CaptureMethod::Portal}, fns, &errors).has_value());
        QCOMPARE(errors.size(), 2);
        QVERIFY(errors.at(0).startsWith(QStringLiteral("kwin: ")));   // нет функции → «метод недоступен в этой сборке»
        QCOMPARE(errors.at(1), QStringLiteral("portal: отказ"));
    }

    void unknownForcedReported()
    {
        QStringList errors;
        QVERIFY(!captureScreens(env("wayland", false, false, "bogus"), &errors).has_value());
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().contains(QStringLiteral("bogus")));
    }
};

QTEST_MAIN(TestCaptureBackend)
#include "test_capturebackend.moc"
```

- [ ] **Step 2: Убедиться, что падает** (нет `capturebackend.h`).

- [ ] **Step 3: Реализация** `src/capturebackend.h` — интерфейс из блока Interfaces (с `#include <QMap> <QStringList> <functional> <optional>` и `capture.h`). `src/capturebackend.cpp`:

```cpp
#include "capturebackend.h"

#include <QGuiApplication>

QString captureMethodName(CaptureMethod m)
{
    switch (m) {
    case CaptureMethod::X11: return QStringLiteral("x11");
    case CaptureMethod::Screencopy: return QStringLiteral("screencopy");
    case CaptureMethod::KWin: return QStringLiteral("kwin");
    case CaptureMethod::Portal: return QStringLiteral("portal");
    }
    return {};
}

QVector<CaptureMethod> captureOrder(const CaptureEnvironment& env)
{
    if (!env.forced.isEmpty()) {
        for (CaptureMethod m : {CaptureMethod::X11, CaptureMethod::Screencopy, CaptureMethod::KWin, CaptureMethod::Portal})
            if (captureMethodName(m) == env.forced)
                return {m};
        return {};
    }
    if (env.platformName != QLatin1String("wayland"))
        return {CaptureMethod::X11};
    QVector<CaptureMethod> order;
    if (env.hasScreencopy)
        order << CaptureMethod::Screencopy;
    if (env.hasKWinScreenShot2)
        order << CaptureMethod::KWin;
    order << CaptureMethod::Portal;
    return order;
}

CaptureEnvironment detectEnvironment()
{
    CaptureEnvironment env;
    env.platformName = QGuiApplication::platformName();
    env.forced = QString::fromLocal8Bit(qgetenv("PINCH_CAPTURE")).trimmed().toLower();
    return env;
}

std::optional<Capture> captureScreensWith(const QVector<CaptureMethod>& order,
                                          const QMap<CaptureMethod, CaptureFn>& fns, QStringList* errors)
{
    for (CaptureMethod m : order) {
        QString error;
        const auto fn = fns.value(m);
        std::optional<Capture> c;
        if (fn)
            c = fn(&error);
        else
            error = QStringLiteral("метод недоступен в этой сборке");
        if (c)
            return c;
        if (errors)
            *errors << captureMethodName(m) + QStringLiteral(": ") + (error.isEmpty() ? QStringLiteral("неизвестная ошибка") : error);
    }
    return std::nullopt;
}

std::optional<Capture> captureScreens(const CaptureEnvironment& env, QStringList* errors)
{
    const QVector<CaptureMethod> order = captureOrder(env);
    if (order.isEmpty()) {
        if (errors)
            *errors << QStringLiteral("неизвестный метод PINCH_CAPTURE=%1 (допустимо: x11, screencopy, kwin, portal)").arg(env.forced);
        return std::nullopt;
    }
    QMap<CaptureMethod, CaptureFn> fns;
    fns[CaptureMethod::X11] = [](QString* error) {
        auto c = captureAllScreens();
        if (!c)
            *error = QStringLiteral("не удалось снять экран средствами Qt");
        return c;
    };
    // Задачи 3–5 добавляют сюда Screencopy, KWin, Portal.
    return captureScreensWith(order, fns, errors);
}
```

`src/main.cpp`: заменить `captureAllScreens()` на
```cpp
    QStringList captureErrors;
    std::optional<Capture> capture = captureScreens(detectEnvironment(), &captureErrors);
    if (!capture) {
        const QString text = QStringLiteral("Не удалось снять экран:\n") + captureErrors.join(QLatin1Char('\n'));
        qCritical("%s", qPrintable(text));
        QMessageBox::critical(nullptr, QStringLiteral("pinch"), text);
        return 1;
    }
```
(добавить `#include "capturebackend.h"`, `#include <QMessageBox>`).

- [ ] **Step 4: Тесты проходят** — обе сборки, весь набор.
- [ ] **Step 5: Commit** — `feat: выбор метода снимка экрана (X11/Wayland, PINCH_CAPTURE)`.

---

### Task 2: Сырые буферы → QImage, сопоставление мониторов

**Files:**
- Create: `src/rawimage.h`, `src/rawimage.cpp`, `tests/test_rawimage.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  // Коды форматов wl_shm (протокол Wayland).
  namespace ShmFormat { constexpr quint32 ARGB8888 = 0, XRGB8888 = 1, ABGR8888 = 0x34324241, XBGR8888 = 0x34324258; }
  // Буфер wl_shm → глубокая копия в Format_RGB32 (альфа всегда 0xff). nullopt: неизвестный формат,
  // width/height <= 0, stride < width*4, data == nullptr.
  std::optional<QImage> imageFromShm(const uchar* data, int width, int height, int stride, quint32 format, bool yInvert);
  // Ответ KWin ScreenShot2 → Format_RGB32. Разрешены значения QImage::Format: RGB32, ARGB32,
  // ARGB32_Premultiplied, RGBX8888, RGBA8888. nullopt: иной формат, размеры <= 0, stride < width*4,
  // data.size() < stride*height.
  std::optional<QImage> imageFromKWin(const QByteArray& data, int width, int height, int stride, int qimageFormat);
  struct NamedScreen { QString name; QRect geometry; };
  // Геометрии для выходов по именам. Если все имена найдены — по именам; иначе, если количество совпадает, — по порядку;
  // иначе — пусто.
  QVector<QRect> matchScreens(const QStringList& outputNames, const QVector<NamedScreen>& screens);
  ```

- [ ] **Step 1: Падающий тест** `tests/test_rawimage.cpp`:

```cpp
#include <QtTest>

#include "rawimage.h"

namespace {
// 2×2 пикселя, stride с запасом 12 байт (> 8), little-endian 32-битные слова.
QByteArray buffer(std::initializer_list<quint32> words, int stride = 12, int width = 2)
{
    QByteArray b(stride * 2, '\0');
    int i = 0;
    for (quint32 w : words) {
        const int row = i / width, col = i % width;
        memcpy(b.data() + row * stride + col * 4, &w, 4);
        ++i;
    }
    return b;
}
}

class TestRawImage : public QObject {
    Q_OBJECT
private slots:
    void xrgbForcesAlpha()
    {
        const QByteArray b = buffer({0x00112233, 0x00445566, 0x00778899, 0x00aabbcc});
        const auto img = imageFromShm(reinterpret_cast<const uchar*>(b.constData()), 2, 2, 12, ShmFormat::XRGB8888, false);
        QVERIFY(img.has_value());
        QCOMPARE(img->format(), QImage::Format_RGB32);
        QCOMPARE(img->pixel(0, 0), qRgb(0x11, 0x22, 0x33));
        QCOMPARE(img->pixel(1, 1), qRgb(0xaa, 0xbb, 0xcc));
    }

    void xbgrSwapsChannels()
    {
        // XBGR8888: младший байт — R.
        const QByteArray b = buffer({0x00332211, 0, 0, 0});
        const auto img = imageFromShm(reinterpret_cast<const uchar*>(b.constData()), 2, 2, 12, ShmFormat::XBGR8888, false);
        QVERIFY(img.has_value());
        QCOMPARE(img->pixel(0, 0), qRgb(0x11, 0x22, 0x33));
    }

    void yInvertFlips()
    {
        const QByteArray b = buffer({0x00ff0000, 0x00ff0000, 0x000000ff, 0x000000ff});
        const auto img = imageFromShm(reinterpret_cast<const uchar*>(b.constData()), 2, 2, 12, ShmFormat::ARGB8888, true);
        QVERIFY(img.has_value());
        QCOMPARE(img->pixel(0, 0), qRgb(0, 0, 0xff));
        QCOMPARE(img->pixel(0, 1), qRgb(0xff, 0, 0));
    }

    void rejectsBadShm()
    {
        const QByteArray b = buffer({0, 0, 0, 0});
        const auto* p = reinterpret_cast<const uchar*>(b.constData());
        QVERIFY(!imageFromShm(p, 2, 2, 12, 0x12345678, false).has_value()); // неизвестный формат
        QVERIFY(!imageFromShm(p, 2, 2, 4, ShmFormat::XRGB8888, false).has_value()); // stride < width*4
        QVERIFY(!imageFromShm(p, 0, 2, 12, ShmFormat::XRGB8888, false).has_value());
        QVERIFY(!imageFromShm(nullptr, 2, 2, 12, ShmFormat::XRGB8888, false).has_value());
    }

    void kwinFormats()
    {
        const QByteArray b = buffer({0xff112233, 0xff445566, 0xff778899, 0xffaabbcc});
        const auto img = imageFromKWin(b, 2, 2, 12, QImage::Format_ARGB32);
        QVERIFY(img.has_value());
        QCOMPARE(img->format(), QImage::Format_RGB32);
        QCOMPARE(img->pixel(1, 0), qRgb(0x44, 0x55, 0x66));
        QVERIFY(imageFromKWin(b, 2, 2, 12, QImage::Format_RGB32).has_value());
        QVERIFY(imageFromKWin(b, 2, 2, 12, QImage::Format_RGBX8888).has_value());
    }

    void rejectsBadKWin()
    {
        const QByteArray b = buffer({0, 0, 0, 0});
        QVERIFY(!imageFromKWin(b, 2, 2, 12, QImage::Format_Indexed8).has_value()); // запрещённый формат
        QVERIFY(!imageFromKWin(b.left(20), 2, 2, 12, QImage::Format_RGB32).has_value()); // данных меньше stride*height
        QVERIFY(!imageFromKWin(b, 2, 2, 4, QImage::Format_RGB32).has_value());
        QVERIFY(!imageFromKWin(b, -1, 2, 12, QImage::Format_RGB32).has_value());
    }

    void matchByName()
    {
        const QVector<NamedScreen> screens = {{QStringLiteral("DP-2"), QRect(2560, 0, 1920, 1080)},
                                              {QStringLiteral("DP-0"), QRect(0, 720, 2560, 1440)}};
        QCOMPARE(matchScreens({QStringLiteral("DP-0"), QStringLiteral("DP-2")}, screens),
                 (QVector<QRect>{QRect(0, 720, 2560, 1440), QRect(2560, 0, 1920, 1080)}));
    }

    void matchFallsBackToOrder()
    {
        const QVector<NamedScreen> screens = {{QStringLiteral("A"), QRect(0, 0, 10, 10)}, {QStringLiteral("B"), QRect(10, 0, 10, 10)}};
        QCOMPARE(matchScreens({QString(), QStringLiteral("X")}, screens), (QVector<QRect>{QRect(0, 0, 10, 10), QRect(10, 0, 10, 10)}));
        QVERIFY(matchScreens({QStringLiteral("X")}, screens).isEmpty());
    }
};

QTEST_MAIN(TestRawImage)
#include "test_rawimage.moc"
```

- [ ] **Step 2: Убедиться, что падает.**

- [ ] **Step 3: Реализация** `src/rawimage.cpp`:

```cpp
#include "rawimage.h"

#include <cstring>

std::optional<QImage> imageFromShm(const uchar* data, int width, int height, int stride, quint32 format, bool yInvert)
{
    if (!data || width <= 0 || height <= 0 || stride < width * 4)
        return std::nullopt;
    const bool xrgb = format == ShmFormat::XRGB8888 || format == ShmFormat::ARGB8888;
    const bool xbgr = format == ShmFormat::XBGR8888 || format == ShmFormat::ABGR8888;
    if (!xrgb && !xbgr)
        return std::nullopt;
    QImage out(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        const uchar* srcRow = data + qsizetype(yInvert ? height - 1 - y : y) * stride;
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < width; ++x) {
            quint32 v;
            std::memcpy(&v, srcRow + x * 4, 4); // невыровненный буфер — только memcpy
            if (xbgr)
                v = ((v & 0xffu) << 16) | (v & 0xff00u) | ((v >> 16) & 0xffu);
            dst[x] = 0xff000000u | (v & 0x00ffffffu);
        }
    }
    return out;
}

std::optional<QImage> imageFromKWin(const QByteArray& data, int width, int height, int stride, int qimageFormat)
{
    static const int kAllowed[] = {QImage::Format_RGB32, QImage::Format_ARGB32, QImage::Format_ARGB32_Premultiplied,
                                   QImage::Format_RGBX8888, QImage::Format_RGBA8888};
    if (std::find(std::begin(kAllowed), std::end(kAllowed), qimageFormat) == std::end(kAllowed))
        return std::nullopt;
    if (width <= 0 || height <= 0 || stride < width * 4 || data.size() < qsizetype(stride) * height)
        return std::nullopt;
    const QImage view(reinterpret_cast<const uchar*>(data.constData()), width, height, stride,
                      static_cast<QImage::Format>(qimageFormat));
    return view.convertToFormat(QImage::Format_RGB32); // глубокая копия
}

QVector<QRect> matchScreens(const QStringList& outputNames, const QVector<NamedScreen>& screens)
{
    QVector<QRect> byName;
    for (const QString& name : outputNames) {
        const auto it = std::find_if(screens.cbegin(), screens.cend(),
                                     [&](const NamedScreen& s) { return !name.isEmpty() && s.name == name; });
        if (it == screens.cend()) {
            byName.clear();
            break;
        }
        byName << it->geometry;
    }
    if (byName.size() == outputNames.size() && !outputNames.isEmpty())
        return byName;
    if (outputNames.size() != screens.size())
        return {};
    QVector<QRect> byOrder;
    for (const NamedScreen& s : screens)
        byOrder << s.geometry;
    return byOrder;
}
```
(в `.h` — `#include <QImage> <QRect> <QStringList> <QVector> <optional>` и `<algorithm>` в .cpp).

- [ ] **Step 4: Тесты проходят** — обе сборки.
- [ ] **Step 5: Commit** — `feat: преобразование сырых буферов снимка и сопоставление мониторов`.

---
### Task 3: Бэкенд wlr-screencopy (Hyprland, Sway)

**Files:**
- Create: `protocols/wlr-screencopy-unstable-v1.xml`, `protocols/README.md`, `src/screencopy.h`, `src/screencopy.cpp`, `tests/test_screencopy_live.cpp`
- Modify: `CMakeLists.txt` (опция `PINCH_WAYLAND`, pkg-config `wayland-client`, генерация `wayland-scanner`), `tests/CMakeLists.txt`, `src/capturebackend.cpp` (подключение метода и `hasScreencopy`)

**Interfaces:**
- Consumes: `imageFromShm`, `matchScreens`, `NamedScreen` (Task 2); `composeScreens`, `ScreenShot` (capture.h); `CaptureFn` (Task 1).
- Produces:
  ```cpp
  // true, если композитор объявляет zwlr_screencopy_manager_v1. Открывает и закрывает собственное соединение.
  bool screencopyAvailable();
  // Снимок всех выходов. screens — QScreen'ы (имя + логическая геометрия) для сопоставления.
  std::optional<Capture> captureWithScreencopy(const QVector<NamedScreen>& screens, QString* error);
  ```
  В сборке с `PINCH_WAYLAND=OFF` обе функции существуют: `screencopyAvailable()` → false, `captureWithScreencopy` → ошибка «сборка без поддержки Wayland».

**Сборка.**
- `option(PINCH_WAYLAND "Поддержка Wayland (screencopy)" ON)`. При ON: `find_package(PkgConfig REQUIRED)`, `pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)`, `find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)`; `add_custom_command` генерирует в `${CMAKE_CURRENT_BINARY_DIR}/protocols/` файлы `wlr-screencopy-unstable-v1-client-protocol.h` (`client-header`) и `wlr-screencopy-unstable-v1-protocol.c` (`private-code`); `.c` компилируется в `pinch_core` (добавить `C` в `project(... LANGUAGES CXX C)`); `target_compile_definitions(pinch_core PUBLIC PINCH_HAVE_WAYLAND=1)`; `target_link_libraries(pinch_core PUBLIC PkgConfig::WAYLAND_CLIENT)`. Предупреждения `-Wpedantic` в сгенерированном C-коде не должны ломать сборку: флаги `pinch_options` применять только к C++ (`$<$<COMPILE_LANGUAGE:CXX>:...>`), к `.c` — без `-Werror`.
- `protocols/wlr-screencopy-unstable-v1.xml` — скачать из официального репозитория `https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/raw/master/unstable/wlr-screencopy-unstable-v1.xml` без изменений; `protocols/README.md` — источник, дата, лицензия (текст из заголовка XML).

**Алгоритм `captureWithScreencopy`** (весь код — в анонимном namespace `screencopy.cpp`, RAII-обёртки для `wl_display`, объектов, fd и mmap):
1. `wl_display_connect(nullptr)`; нет соединения → ошибка «нет соединения с Wayland».
2. Реестр: собрать `wl_output` (bind версии `min(4, advertised)`; при v4 — слушатель `name`), `wl_shm` (v1), `zwlr_screencopy_manager_v1` (v1…3, достаточно v1). `wl_display_roundtrip` дважды (глобалы, затем события `name`). Нет менеджера или `wl_shm` → ошибка.
3. Для каждого выхода: `zwlr_screencopy_manager_v1_capture_output(manager, 0 /*без курсора*/, output)`; слушатель кадра: `buffer(format, width, height, stride)` → запомнить; `flags` → `y_invert`; `buffer_done` (v3) игнорировать; `ready` → успех; `failed` → ошибка. После `buffer`: `memfd_create("pinch-screencopy", MFD_CLOEXEC)`, `ftruncate(stride*height)`, `mmap(PROT_READ|PROT_WRITE, MAP_SHARED)`, `wl_shm_create_pool` → `create_buffer` → `zwlr_screencopy_frame_v1_copy(frame, buffer)`.
4. Цикл `wl_display_dispatch` с общим тайм-аутом 5 с (через `poll` на `wl_display_get_fd` с `wl_display_prepare_read`/`read_events`/`dispatch_pending`), пока все кадры не завершены.
5. Каждый кадр → `imageFromShm(...)` (nullopt → ошибка «неподдерживаемый формат 0x…»).
6. Геометрии: `matchScreens(имена выходов, screens)`; пусто → ошибка «не удалось сопоставить выходы Wayland с мониторами». Затем `composeScreens` из пар (картинка, геометрия).
7. Освобождение всего в любом случае (деструкторы RAII).

`screencopyAvailable()`: соединение + реестр + один roundtrip; true, если объявлен `zwlr_screencopy_manager_v1`.

В `capturebackend.cpp`: `detectEnvironment()` при `platformName == "wayland"` заполняет `hasScreencopy = screencopyAvailable()`; в `captureScreens` добавить `fns[CaptureMethod::Screencopy]`, передавая `NamedScreen{s->name(), s->geometry()}` для всех `QGuiApplication::screens()`.

- [ ] **Step 1: Тест** `tests/test_screencopy_live.cpp` (живой, против Sway без экрана; без sway — `QSKIP`):

```cpp
#include <QtTest>

#include "screencopy.h"

// Живая проверка против headless Sway. Если sway не установлен — пропуск.
class TestScreencopyLive : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("sway")).isEmpty())
            QSKIP("sway не установлен — живой тест screencopy пропущен");
        QVERIFY(m_runtime.isValid());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), m_runtime.path());
        env.insert(QStringLiteral("WLR_BACKENDS"), QStringLiteral("headless"));
        env.insert(QStringLiteral("WLR_RENDERER"), QStringLiteral("pixman"));
        env.insert(QStringLiteral("WLR_LIBINPUT_NO_DEVICES"), QStringLiteral("1"));
        env.remove(QStringLiteral("WAYLAND_DISPLAY")); // иначе wlroots может попытаться работать вложенно
        env.remove(QStringLiteral("DISPLAY"));
        QFile cfg(m_runtime.filePath(QStringLiteral("sway.cfg")));
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("output HEADLESS-1 resolution 640x480\n"); // фон не задаём: swaybg может быть не установлен
        cfg.close();
        m_sway.setProcessEnvironment(env);
        m_sway.start(QStringLiteral("sway"), {QStringLiteral("-c"), cfg.fileName()});
        QVERIFY(m_sway.waitForStarted());
        // Дождаться сокета композитора (sway сам выбирает имя wayland-N в XDG_RUNTIME_DIR).
        QTRY_VERIFY_WITH_TIMEOUT(!QDir(m_runtime.path()).entryList({QStringLiteral("wayland-*")}, QDir::System).isEmpty(), 10000);
        const QString socket = QDir(m_runtime.path()).entryList({QStringLiteral("wayland-*")}, QDir::System).first();
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(m_runtime.path()));
        qputenv("WAYLAND_DISPLAY", QFile::encodeName(socket.section(QLatin1Char('.'), 0, 0)));
    }

    void availableAndCaptures()
    {
        QVERIFY(screencopyAvailable());
        QString error;
        const auto c = captureWithScreencopy({{QStringLiteral("HEADLESS-1"), QRect(0, 0, 640, 480)}}, &error);
        QVERIFY2(c.has_value(), qPrintable(error));
        QCOMPARE(c->image.size(), QSize(640, 480));
        QCOMPARE(c->image.format(), QImage::Format_RGB32);
    }

    void cleanupTestCase()
    {
        if (m_sway.state() != QProcess::NotRunning) {
            m_sway.terminate();
            m_sway.waitForFinished(5000);
        }
    }

private:
    QTemporaryDir m_runtime;
    QProcess m_sway;
};

QTEST_GUILESS_MAIN(TestScreencopyLive)
#include "test_screencopy_live.moc"
```
Регистрировать `pinch_add_test(test_screencopy_live)` только при `PINCH_WAYLAND`. `QTEST_GUILESS_MAIN` — тест не создаёт окон. Цвет пикселей не проверяется (без swaybg фон не определён) — проверяются успех, размер и формат.

- [ ] **Step 2: Убедиться, что падает** (нет `screencopy.h`). На машине разработки `sway` не установлен → после реализации тест завершится `SKIP`; это ожидаемо, записать в отчёт.
- [ ] **Step 3: Реализация** — по алгоритму выше; `-DPINCH_WAYLAND=OFF` тоже должен собираться (проверить отдельной конфигурацией в `/tmp/claude-1000/...`).
- [ ] **Step 4: Тесты** — обе сборки + конфигурация `PINCH_WAYLAND=OFF` (сборка и ctest).
- [ ] **Step 5: Commit** — `feat: снимок через wlr-screencopy (Hyprland, Sway)`.

---

### Task 4: Бэкенд KWin ScreenShot2 и приватная шина для тестов

**Files:**
- Create: `src/kwincapture.h`, `src/kwincapture.cpp`, `tests/dbustestbus.h`, `tests/fakes/fake_kwin.cpp`, `tests/test_kwincapture.cpp`
- Modify: `CMakeLists.txt` (`find_package(Qt6 ... DBus)`, `pinch_core` линкует `Qt6::DBus`), `tests/CMakeLists.txt` (исполняемый `fake_kwin`, тест с зависимостью и путём к нему через `target_compile_definitions(test_kwincapture PRIVATE FAKE_KWIN_PATH="$<TARGET_FILE:fake_kwin>")`), `src/capturebackend.cpp`

**Interfaces:**
- Consumes: `imageFromKWin` (Task 2), `Capture`, `CaptureFn`.
- Produces:
  ```cpp
  bool kwinScreenShotAvailable(QDBusConnection bus);  // интроспекция /org/kde/KWin/ScreenShot2 у org.kde.KWin
  // screens — логические геометрии мониторов (QScreen::geometry()) в глобальных координатах.
  std::optional<Capture> captureWithKWin(QDBusConnection bus, const QVector<QRect>& screens, QString* error);
  ```
  В `capturebackend.cpp`: `hasKWinScreenShot2 = kwinScreenShotAvailable(QDBusConnection::sessionBus())` (только под Wayland); `fns[CaptureMethod::KWin]` с `QDBusConnection::sessionBus()`.

**Алгоритм `captureWithKWin`:**
1. `pipe2(fds, O_CLOEXEC)`.
2. Поток-читатель (`std::thread`) читает `fds[0]` в `QByteArray` до EOF, `poll` с общим тайм-аутом 5 с; превышение → флаг тайм-аута.
3. `QDBusMessage::createMethodCall("org.kde.KWin", "/org/kde/KWin/ScreenShot2", "org.kde.KWin.ScreenShot2", "CaptureWorkspace")` с аргументами `QVariantMap{{"include-cursor", false}, {"native-resolution", false}}` и `QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]))`; `bus.call(msg, QDBus::Block, 5000)`; сразу `close(fds[1])` (у нас остаётся только копия в сообщении, которая закрывается вместе с ним).
4. Ошибка D-Bus → ошибка метода с текстом `reply.errorMessage()` (частый случай — отказ по `.desktop`: добавить подсказку «установите pinch.desktop: cmake --install …»).
5. Дождаться читателя; из ответа (`QVariantMap`, первый аргумент; учесть, что значения могут прийти как `QDBusArgument`/`uint`) взять `type` (ожидается `raw`), `width`, `height`, `stride`, `format`; `imageFromKWin(...)`; nullopt → ошибка «KWin вернул данные неожиданного формата».
6. `Capture`: `origin` = левый верхний угол объединения `screens`, `screens` — сдвинутые в координаты изображения; если размер картинки ≠ размеру объединения, масштабировать картинку в него (`Qt::SmoothTransformation`).
7. Все fd закрыты при любом исходе.

**Тестовая шина** `tests/dbustestbus.h` (header-only):
```cpp
// Запускает приватный dbus-daemon (сессионная конфигурация) и даёт его адрес. Останавливает в деструкторе.
class DBusTestBus {
public:
    bool start();                      // QProcess "dbus-daemon --session --nofork --print-address=1"; читает адрес со stdout (тайм-аут 5 с)
    QString address() const;
    QDBusConnection connect(const QString& name); // QDBusConnection::connectToBus(address, name)
    // Запускает помощник с DBUS_SESSION_BUS_ADDRESS=address и ждёт появления имени сервиса на шине (тайм-аут 5 с).
    bool startService(QProcess& process, const QString& program, const QStringList& args, const QString& serviceName);
    ~DBusTestBus();                    // terminate/kill dbus-daemon
};
```

**Поддельный KWin** `tests/fakes/fake_kwin.cpp` (QCoreApplication, отдельный процесс): регистрирует на сессионной шине имя `org.kde.KWin` и объект `/org/kde/KWin/ScreenShot2` с интерфейсом `org.kde.KWin.ScreenShot2` (класс с `Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.ScreenShot2")`, `registerObject(..., ExportAllSlots)`), слот `QVariantMap CaptureWorkspace(const QVariantMap& options, const QDBusUnixFileDescriptor& pipe)`. Поведение задаётся аргументом командной строки:
- `ok` — отвечает `{type: "raw", width: 4, height: 2, stride: 16, format: int(QImage::Format_RGB32)}` и в отдельном потоке пишет в `dup(pipe.fileDescriptor())` 32 байта: все пиксели `0xff00ff00` (зелёный), затем закрывает;
- `deny` — отвечает D-Bus-ошибкой `org.kde.KWin.ScreenShot2.Error.NoAuthorized` «The process is not authorized to take a screenshot»;
- `short` — объявляет 4×2, stride 16, но пишет только 8 байт;
- `badformat` — `format: int(QImage::Format_Indexed8)`.

- [ ] **Step 1: Падающий тест** `tests/test_kwincapture.cpp`:

```cpp
#include <QtTest>

#include "dbustestbus.h"
#include "kwincapture.h"

class TestKWinCapture : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        m_bus = std::make_unique<DBusTestBus>();
        QVERIFY(m_bus->start());
    }
    void cleanup()
    {
        stopFake();
        m_bus.reset();
    }

    void capturesWorkspace()
    {
        runFake(QStringLiteral("ok"));
        QDBusConnection c = m_bus->connect(QStringLiteral("t-ok"));
        QVERIFY(kwinScreenShotAvailable(c));
        QString error;
        const auto cap = captureWithKWin(c, {QRect(0, 0, 4, 2)}, &error);
        QVERIFY2(cap.has_value(), qPrintable(error));
        QCOMPARE(cap->image.size(), QSize(4, 2));
        QCOMPARE(cap->image.pixel(3, 1), qRgb(0, 0xff, 0));
        QCOMPARE(cap->screens, (QVector<QRect>{QRect(0, 0, 4, 2)}));
    }

    void notAvailableWithoutService()
    {
        QVERIFY(!kwinScreenShotAvailable(m_bus->connect(QStringLiteral("t-none"))));
    }

    void rejectsDenied()   { expectError(QStringLiteral("deny"), QStringLiteral("authorized")); }
    void rejectsShortData() { expectError(QStringLiteral("short"), QString()); }
    void rejectsBadFormat() { expectError(QStringLiteral("badformat"), QString()); }

private:
    void runFake(const QString& mode)
    {
        QVERIFY(m_bus->startService(m_fake, QStringLiteral(FAKE_KWIN_PATH), {mode}, QStringLiteral("org.kde.KWin")));
    }
    void stopFake()
    {
        if (m_fake.state() != QProcess::NotRunning) {
            m_fake.terminate();
            m_fake.waitForFinished(3000);
        }
    }
    void expectError(const QString& mode, const QString& mustContain)
    {
        runFake(mode);
        QString error;
        QVERIFY(!captureWithKWin(m_bus->connect(QStringLiteral("t-") + mode), {QRect(0, 0, 4, 2)}, &error).has_value());
        QVERIFY(!error.isEmpty());
        if (!mustContain.isEmpty())
            QVERIFY2(error.contains(mustContain, Qt::CaseInsensitive), qPrintable(error));
    }

    std::unique_ptr<DBusTestBus> m_bus;
    QProcess m_fake;
};

QTEST_GUILESS_MAIN(TestKWinCapture)
#include "test_kwincapture.moc"
```

- [ ] **Step 2: Убедиться, что падает.**
- [ ] **Step 3: Реализация** — `DBusTestBus`, `fake_kwin`, `kwincapture` по описанию выше.
- [ ] **Step 4: Тесты** — обе сборки; под ASan поддельный процесс тоже собирается с санитайзерами — утечки в нём не должны валить тест (для `fake_kwin` можно задать `ASAN_OPTIONS=detect_leaks=0` в `startService`, с комментарием).
- [ ] **Step 5: Commit** — `feat: снимок через KWin ScreenShot2 (KDE Plasma)`.

---

### Task 5: Бэкенд xdg-desktop-portal (GNOME и запасной)

**Files:**
- Create: `src/portalcapture.h`, `src/portalcapture.cpp`, `tests/fakes/fake_portal.cpp`, `tests/test_portalcapture.cpp`
- Modify: `tests/CMakeLists.txt` (`fake_portal`, `FAKE_PORTAL_PATH`), `src/capturebackend.cpp`

**Interfaces:**
- Consumes: `DBusTestBus` (Task 4), `Capture`, `CaptureFn`.
- Produces:
  ```cpp
  // Чистое правило: можно ли удалить файл, который вернул портал. true только если lstat успешен,
  // это обычный файл (не симлинк), владелец — getuid(), и mtime >= requestStartSecs - 2.
  bool shouldDeletePortalFile(const QString& path, qint64 requestStartSecs);
  // Путь объекта запроса по правилам портала: /org/freedesktop/portal/desktop/request/<sender>/<token>,
  // где sender — уникальное имя соединения без ':' и с '.' → '_'.
  QString portalRequestPath(const QString& uniqueName, const QString& token);
  std::optional<Capture> captureWithPortal(QDBusConnection bus, const QVector<QRect>& screens, QString* error,
                                           int timeoutMs = 15000);
  ```
  В `capturebackend.cpp`: `fns[CaptureMethod::Portal]` с `QDBusConnection::sessionBus()`.

**Алгоритм `captureWithPortal`:**
1. Если на шине есть `org.freedesktop.host.portal.Registry` (объект `/org/freedesktop/portal/desktop`), вызвать `Register("pinch", {})`; ошибки игнорировать.
2. `token = "pinch" + QString::number(QRandomGenerator::global()->generate())`; `path = portalRequestPath(bus.baseService(), token)`; подписаться (`bus.connect`) на сигнал `Response(uint, QVariantMap)` интерфейса `org.freedesktop.portal.Request` по этому пути **до** вызова.
3. `requestStart = QDateTime::currentSecsSinceEpoch()`; вызов `org.freedesktop.portal.Desktop` `/org/freedesktop/portal/desktop` `org.freedesktop.portal.Screenshot.Screenshot("", {"handle_token": token, "interactive": false})`; ошибка D-Bus → ошибка метода.
4. Локальный `QEventLoop` до сигнала или тайм-аута (`timeoutMs`) → тайм-аут = «портал не ответил».
5. `response != 0` → ошибка «Портал отказал в снимке экрана. В GNOME: Настройки → Приложения → pinch → разрешить снимки экрана».
6. `uri` из результатов; не `file://` → ошибка. `QImage img(localPath)`; пусто → ошибка.
7. Если `shouldDeletePortalFile(localPath, requestStart)` → `unlink`; иначе `qWarning` «файл портала оставлен: …».
8. `Capture` из `img` и `screens` — как в Task 4, шаг 6 (общий код вынести в функцию `captureFromWorkspaceImage(const QImage&, const QVector<QRect>&)` в `capture.{h,cpp}` и использовать в обоих бэкендах; тест на неё — в test_capture: масштабирование и сдвиг экранов).

**Поддельный портал** `tests/fakes/fake_portal.cpp`: имя `org.freedesktop.portal.Desktop`, объект `/org/freedesktop/portal/desktop`, интерфейс `org.freedesktop.portal.Screenshot`, метод `QDBusObjectPath Screenshot(const QString& parent, const QVariantMap& options)` — возвращает путь по правилам (из `message().service()` и `handle_token`), а через 50 мс (QTimer) испускает сигнал `org.freedesktop.portal.Request.Response` на этом пути (`QDBusMessage::createSignal`). Аргументы командной строки: `<mode> <imagePath>`: `ok` — перед ответом пишет PNG 4×2 синего цвета в `imagePath`, ответ `(0, {uri: file://imagePath})`; `deny` — ответ `(1, {})`; `symlink` — `imagePath` делается симлинком на PNG рядом, ответ с `uri` симлинка; `silent` — не отвечает вовсе.

- [ ] **Step 1: Падающий тест** `tests/test_portalcapture.cpp`:

```cpp
#include <QtTest>

#include "dbustestbus.h"
#include "portalcapture.h"

#include <sys/stat.h>
#include <utime.h>

class TestPortalCapture : public QObject {
    Q_OBJECT
private slots:
    void requestPath()
    {
        QCOMPARE(portalRequestPath(QStringLiteral(":1.42"), QStringLiteral("pinch7")),
                 QStringLiteral("/org/freedesktop/portal/desktop/request/1_42/pinch7"));
    }

    void safeDeleteFreshOwnFile()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath(QStringLiteral("shot.png"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QVERIFY(shouldDeletePortalFile(p, QDateTime::currentSecsSinceEpoch()));
    }

    void safeDeleteRejectsOldFile()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath(QStringLiteral("old.png"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        struct utimbuf t {1000, 1000};
        QCOMPARE(::utime(QFile::encodeName(p).constData(), &t), 0);
        QVERIFY(!shouldDeletePortalFile(p, QDateTime::currentSecsSinceEpoch()));
    }

    void safeDeleteRejectsSymlink()
    {
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("t.png"));
        QFile f(target);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        const QString link = dir.filePath(QStringLiteral("l.png"));
        QCOMPARE(::symlink(QFile::encodeName(target).constData(), QFile::encodeName(link).constData()), 0);
        QVERIFY(!shouldDeletePortalFile(link, QDateTime::currentSecsSinceEpoch()));
        QVERIFY(!shouldDeletePortalFile(dir.filePath(QStringLiteral("missing.png")), 0));
    }

    void capturesAndDeletesFile()
    {
        QTemporaryDir dir;
        const QString img = dir.filePath(QStringLiteral("portal.png"));
        DBusTestBus bus;
        QVERIFY(bus.start());
        QProcess fake;
        QVERIFY(bus.startService(fake, QStringLiteral(FAKE_PORTAL_PATH), {QStringLiteral("ok"), img},
                                 QStringLiteral("org.freedesktop.portal.Desktop")));
        QString error;
        const auto cap = captureWithPortal(bus.connect(QStringLiteral("p-ok")), {QRect(0, 0, 4, 2)}, &error);
        QVERIFY2(cap.has_value(), qPrintable(error));
        QCOMPARE(cap->image.pixel(0, 0), qRgb(0, 0, 0xff));
        QVERIFY(!QFile::exists(img)); // свежий свой файл удалён
        fake.terminate();
        fake.waitForFinished(3000);
    }

    void deniedIsExplained()
    {
        expectError(QStringLiteral("deny"), QStringLiteral("Настройки"));
    }

    void timeoutIsReported()
    {
        expectError(QStringLiteral("silent"), QStringLiteral("не ответил"), 500);
    }

    void symlinkNotDeleted()
    {
        QTemporaryDir dir;
        const QString img = dir.filePath(QStringLiteral("portal.png"));
        DBusTestBus bus;
        QVERIFY(bus.start());
        QProcess fake;
        QVERIFY(bus.startService(fake, QStringLiteral(FAKE_PORTAL_PATH), {QStringLiteral("symlink"), img},
                                 QStringLiteral("org.freedesktop.portal.Desktop")));
        QString error;
        QVERIFY2(captureWithPortal(bus.connect(QStringLiteral("p-l")), {QRect(0, 0, 4, 2)}, &error).has_value(), qPrintable(error));
        QVERIFY(QFileInfo(img).isSymLink()); // симлинк не тронут
        fake.terminate();
        fake.waitForFinished(3000);
    }

private:
    void expectError(const QString& mode, const QString& mustContain, int timeoutMs = 15000)
    {
        QTemporaryDir dir;
        DBusTestBus bus;
        QVERIFY(bus.start());
        QProcess fake;
        QVERIFY(bus.startService(fake, QStringLiteral(FAKE_PORTAL_PATH), {mode, dir.filePath(QStringLiteral("x.png"))},
                                 QStringLiteral("org.freedesktop.portal.Desktop")));
        QString error;
        QVERIFY(!captureWithPortal(bus.connect(QStringLiteral("p-") + mode), {QRect(0, 0, 4, 2)}, &error, timeoutMs).has_value());
        QVERIFY2(error.contains(mustContain), qPrintable(error));
        fake.terminate();
        fake.waitForFinished(3000);
    }
};

QTEST_GUILESS_MAIN(TestPortalCapture)
#include "test_portalcapture.moc"
```

- [ ] **Step 2: Убедиться, что падает.**
- [ ] **Step 3: Реализация** — по алгоритму; `captureFromWorkspaceImage` + тест в test_capture (картинка 8×4 при объединении мониторов 4×2 → масштаб; `origin`/`screens` сдвинуты); перевести Task 4 на неё.
- [ ] **Step 4: Тесты** — обе сборки.
- [ ] **Step 5: Commit** — `feat: снимок через xdg-desktop-portal (GNOME и запасной путь)`.

---

### Task 6: Оверлей — контроллер и окно на каждый монитор

**Files:**
- Create: `src/overlaycontroller.{h,cpp}`, `src/screenview.{h,cpp}`, `src/overlaysession.{h,cpp}`
- Delete: `src/overlay.{h,cpp}` (вся логика переезжает в контроллер без изменения поведения)
- Modify: `CMakeLists.txt`, `tests/test_overlay.cpp` (перевод на контроллер и окна), `src/main.cpp` (минимально — чтобы собиралось: сессия вместо `Overlay`; полная доводка в Task 7)

**Interfaces:**
- Consumes: всё, что использовал `Overlay` (Toolbar, Document, renderer, output, geometry, keys, Settings, Capture).
- Produces:
  ```cpp
  class ScreenView;

  // Вся логика оверлея; координаты — координаты изображения. Не QWidget.
  class OverlayController : public QObject {
      Q_OBJECT
  public:
      OverlayController(Capture capture, Settings settings, QString saveDir, QObject* parent = nullptr);
      const Capture& capture() const;
      QRect selection() const;  Tool tool() const;  Style style() const;
      const Document& document() const;  Toolbar* toolbar() const;
      bool isEditingText() const;  QImage result() const;
      void attachView(ScreenView* view);          // окна для перерисовки, курсора и размещения панели
      // События от окон, уже переведённые в координаты изображения:
      void mousePress(QPoint pos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
      void mouseMove(QPoint pos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
      void mouseRelease(QPoint pos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
      void mouseDoubleClick(QPoint pos);
      void wheel(int angleDeltaY);
      void keyPress(QKeyEvent* event);
      // Рисует кадр в координатах изображения; imageRect — видимая окну часть.
      void paint(QPainter& painter, const QRect& imageRect) const;
  signals:
      void copyRequested(const QImage& result); // испускается ДО hideRequested
      void finished();
      void hideRequested();   // скрыть все окна (перед диалогами, после копирования/сохранения/отмены)
      void showRequested();   // снова показать окна (отмена диалога, ошибка записи)
  };

  // Окно одного монитора: переводит события (local + imageRect.topLeft()) и рисует свою часть кадра.
  class ScreenView : public QWidget {
      Q_OBJECT
  public:
      ScreenView(OverlayController* controller, QRect imageRect, QWidget* parent = nullptr);
      QRect imageRect() const;
  protected: // paintEvent, mouse*Event, wheelEvent, keyPressEvent → контроллер
  };

  // Создаёт окно на каждый прямоугольник capture().screens, показывает и прячет их.
  class OverlaySession : public QObject {
      Q_OBJECT
  public:
      explicit OverlaySession(OverlayController* controller, QObject* parent = nullptr);
      ~OverlaySession() override;              // удаляет окна
      void show();
      void hide();
      const QVector<ScreenView*>& views() const;
  };
  ```

**Правила переноса (поведение не меняется, кроме перечисленного):**
- Всё состояние и все методы `Overlay` (выделение, инструменты, текст, колесо, клавиши 1–5 и буквы, карандаш по умолчанию, панель, кэш рендера, подпись размера, подсказка, диалоги, быстрое сохранение) переезжают в `OverlayController`. Обработчики получают координаты изображения; `QMouseEvent`/`QWheelEvent` в контроллер не передаются (кроме `QKeyEvent`, который нужен для текста и скан-кодов).
- `closeOverlay()` → `emit hideRequested()` (+ `releaseKeyboard` делает сессия); `start()` → `emit showRequested()`.
- **Копирование:** `copyResult()` = commitText → проверка выделения → `image = result()` → `emit copyRequested(image)` → `emit hideRequested()`. Порядок обязателен (буфер под Wayland назначается, пока окно видимо и в фокусе).
- **Перерисовка и курсор:** контроллер вызывает `update()` и `setCursor()` у всех прикреплённых окон.
- **Отрисовка:** `ScreenView::paintEvent`: `QPainter p(this); p.translate(-imageRect.topLeft()); controller->paint(p, imageRect);`. В `paint` рисовать только нужную часть фона: `drawImage(imageRect.topLeft(), m_dimmed, imageRect)`.
- **События мыши:** окно, получившее нажатие, получает и все движения до отпускания (неявный захват указателя), даже за своими пределами — переводим их тем же сдвигом; так выделение тянется на соседний монитор.
- **Панель инструментов:** после `placeToolbar` находим окно, чей `imageRect` содержит левый верхний угол панели (иначе — окно с наибольшим пересечением с выделением), `toolbar->setParent(view)` при смене окна, `move(pos - view->imageRect().topLeft())`, `show()`, `raise()`. `Toolbar::wheelEvent` уже пересылает колесо родителю → `ScreenView::wheelEvent` → контроллер.
- **Показ (`OverlaySession::show`):**
  - `QGuiApplication::platformName() == "wayland"`: флаги `Qt::FramelessWindowHint`; `QScreen` для окна — тот, у которого `geometry().translated(-capture.origin) == imageRect` (иначе основной экран); `setScreen(screen)` через `windowHandle()` после `create()`/`winId()`; `showFullScreen()`.
  - иначе (X11, offscreen): флаги `Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint`; `setGeometry(imageRect.translated(capture.origin))`; `show()`.
  - затем `raise()` всех; активировать и дать фокус окну под курсором (`QCursor::pos()`), иначе первому; под X11 — `grabKeyboard()` у него.
- **`hide()`**: `releaseKeyboard()` и `hide()` у всех окон. Сессия подключает `hideRequested`/`showRequested` к `hide`/`show`.
- **main (минимально):** `auto* controller = new OverlayController(...)`; `auto* session = new OverlaySession(controller)`; сигналы `finished`/`copyRequested` — как было у `Overlay` (`saveStyle` из `controller->style()`); освобождение: при копировании `session->deleteLater(); controller->deleteLater();`, при выходе — через `QPointer` как сейчас.

**Перевод тестов `tests/test_overlay.cpp`:** все сценарии и проверки сохраняются, меняется только доставка событий.
- `Env`: `OverlayController ctl{makeCapture(), Settings{}, dir.path()}; OverlaySession session{&ctl};` в конструкторе `session.show()`; сигналы — к `ctl`. Фикстура: `c.image` левая половина `qRgb(100,100,100)`, правая (x ≥ 400) — `qRgb(150,150,150)`; экраны `{QRect(0,0,400,300), QRect(400,0,400,200)}`.
- Помощники принимают `Env&` и координаты изображения: нажатие отправляется окну, содержащему точку (`viewAt(pos)`), последующие move/release — тому же окну с локальными координатами `pos - view->imageRect().topLeft()` (могут выходить за окно). Клавиши — любому окну (по умолчанию первому).
- `clickInDeadZoneSelectsNothing` → `noViewCoversDeadZone`: ни одно окно не содержит (500,250); число окон = 2.
- `dragBeyondEdgeClamps`: нажатие (700,150) (второе окно) → (900,400) → `QRect(700,150,100,150)`.
- Проверки «оверлей скрыт» → «все окна скрыты».
- Новые тесты:
  - `selectionAcrossViews`: нажатие (350,100) в первом окне, перетаскивание к (450,150) → `QRect(350,100,101,51)`.
  - `toolbarMovesToOtherView`: клик (500,100) → выделен второй монитор `QRect(400,0,400,200)`, родитель панели — второе окно; затем клавиша V и перетаскивание в первом окне (10,10)→(300,250) (вне текущего выделения → новое выделение `QRect(10,10,291,241)`) → родитель панели — первое окно.
  - `keysFromSecondView`: после выделения (карандаш включён по умолчанию) клавиша V, отправленная второму окну → `Tool::None`; затем P второму окну → `Tool::Pen`.
  - `copyEmittedBeforeHide`: в обработчике `copyRequested` запомнить, видимы ли окна (все `isVisible()`), → true; после — все скрыты.
  - `viewPaintsItsRegion`: `session.views()[1]->grab()` — пиксель (10,10) равен затемнённому `qRgb(150,150,150)` (вычислить ожидаемое тем же затемнением, что в контроллере: `QColor(0,0,0,120)` поверх), а у первого окна — затемнённому `qRgb(100,100,100)`.

- [ ] **Step 1: Перевести тесты** (Env/помощники/изменённые/новые) — они не компилируются без новых классов.
- [ ] **Step 2: Убедиться, что падает** (нет `overlaycontroller.h`).
- [ ] **Step 3: Реализация** — перенос кода `Overlay` в контроллер по правилам выше, `ScreenView`, `OverlaySession`, удаление `overlay.{h,cpp}`, минимальная правка `main.cpp`.
- [ ] **Step 4: Тесты** — обе сборки, весь набор (включая все прежние сценарии test_overlay).
- [ ] **Step 5: Commit** — `refactor: оверлей — контроллер и окно на каждый монитор (под Wayland)`.

---

### Task 7: Интеграция, `.desktop` для KWin, README

**Files:**
- Create: `data/pinch.desktop.in`
- Delete: `data/pinch.desktop`
- Modify: `CMakeLists.txt` (`include(GNUInstallDirs)`, `configure_file`, установка сгенерированного файла), `src/main.cpp`, `README.md`, спецификации (статус и ссылки)

**Interfaces:**
- Consumes: `captureScreens`/`detectEnvironment` (Tasks 1, 3–5), `OverlayController`/`OverlaySession` (Task 6).

**Шаги:**
- [ ] **Step 1: `data/pinch.desktop.in`** — содержимое прежнего `data/pinch.desktop`, но `Exec=@CMAKE_INSTALL_FULL_BINDIR@/pinch` и строка `X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2`. В CMake: `configure_file(data/pinch.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/pinch.desktop @ONLY)` и `install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pinch.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)`. Проверка: `desktop-file-validate build/pinch.desktop` (подсказка о двух основных категориях допустима), установка в `/tmp/claude-1000/pinch-stage` и `grep Exec` показывает абсолютный путь в этом префиксе.
- [ ] **Step 2: `main.cpp`** — окончательно: `captureScreens(detectEnvironment(), &errors)` (из Task 1), контроллер + сессия (из Task 6); в обработчике `copyRequested` порядок: `saveStyle` → `lock.release()` → `copyToClipboard(image)` → подписка на `QClipboard::changed` → `session->deleteLater(); controller->deleteLater();`. Проверить, что `setQuitOnLastWindowClosed(false)` на месте. `./build/pinch --version` работает.
- [ ] **Step 3: README** — обновить:
  - зависимости: Ubuntu (`qt6-base-dev qt6-wayland libwayland-dev libwayland-bin`), Arch (`qt6-base qt6-wayland wayland cmake gcc noto-fonts`); опция `-DPINCH_WAYLAND=OFF`;
  - таблица «окружение → способ снимка → что происходит» (раздел 1 спецификации Wayland) и `PINCH_CAPTURE=x11|screencopy|kwin|portal`;
  - модель угроз по бэкендам (раздел 7 спецификации Wayland), включая файл портала и правило его удаления;
  - KDE: способ `kwin` работает только после `cmake --install` (нужен установленный `pinch.desktop` с абсолютным путём); без установки pinch откатится на портал;
  - GNOME: при первом снимке может понадобиться разрешение: Настройки → Приложения → pinch → Снимки экрана;
  - хоткеи под Wayland: GNOME — `scripts/set-gnome-shortcut.sh` (работает и под Wayland); KDE — Параметры системы → Комбинации клавиш → Добавить команду `pinch`; Hyprland — `bind = SUPER SHIFT, S, exec, pinch`; Sway — `bindsym $mod+Shift+s exec pinch`;
  - чек-лист ручной проверки для каждого из GNOME / KDE / Hyprland / Sway: автоматический выбор и `PINCH_CAPTURE=portal`; один и несколько мониторов (окна на своих мониторах, выделение через границу, панель на нужном мониторе); клавиатура работает сразу; Ctrl+C → вставка в другое приложение, процесс живёт до смены буфера; Ctrl+S/Ctrl+Shift+S; повторный запуск; X11 на Ubuntu — регрессия.
- [ ] **Step 4: Спецификации** — в Wayland-спецификации строка «Статус: реализовано, ожидает ручной проверки».
- [ ] **Step 5: Полная проверка** — обе сборки, `PINCH_WAYLAND=OFF`-сборка в `/tmp/claude-1000/`, `--version`, `desktop-file-validate`, `bash -n` скриптов.
- [ ] **Step 6: Commit** — `feat: pinch под Wayland — интеграция, .desktop для KWin, README`.
