#include <QtTest>

#include "capturebackend.h"
#include "i18n.h"

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
    // Переводы нужны для подстановки %1 в тексте ошибки; язык зафиксирован, чтобы тест не зависел от LANG.
    void initTestCase() { installTranslations(*QCoreApplication::instance(), QLocale(QStringLiteral("en_US"))); }

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

    // Qt называет платформу Wayland и «wayland-egl» (и другими вариантами с этим началом).
    void waylandPlatformVariants()
    {
        QVERIFY(isWaylandPlatform(QStringLiteral("wayland")));
        QVERIFY(isWaylandPlatform(QStringLiteral("wayland-egl")));
        QVERIFY(!isWaylandPlatform(QStringLiteral("xcb")));
        QVERIFY(!isWaylandPlatform(QStringLiteral("offscreen")));
        QCOMPARE(captureOrder(env("wayland-egl")), (QVector<CaptureMethod>{CaptureMethod::Portal}));
        QCOMPARE(captureOrder(env("wayland-egl", true, true)),
                 (QVector<CaptureMethod>{CaptureMethod::Screencopy, CaptureMethod::KWin, CaptureMethod::Portal}));
    }

    // QT_QPA_PLATFORM=xcb в сеансе Wayland (Xwayland): снимок через X11 может быть чёрным — нужно предупреждение.
    void xwaylandFallbackDetected()
    {
        CaptureEnvironment e = env("xcb");
        e.waylandDisplay = true;
        QVERIFY(xwaylandFallback(e));
        e.forced = QStringLiteral("x11");
        QVERIFY(xwaylandFallback(e));
        e.forced = QStringLiteral("portal"); // способ не X11 — снимок не через Xwayland
        QVERIFY(!xwaylandFallback(e));
        e = env("xcb");
        QVERIFY(!xwaylandFallback(e)); // обычный сеанс X11
        e = env("wayland");
        e.waylandDisplay = true;
        QVERIFY(!xwaylandFallback(e));
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
        CaptureMethod used = CaptureMethod::X11;
        const auto c = captureScreensWith({CaptureMethod::Screencopy, CaptureMethod::Portal}, fns, &errors, &used);
        QVERIFY(c.has_value());
        QCOMPARE(used, CaptureMethod::Portal); // main печатает его в stderr
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
