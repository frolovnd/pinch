#include <QtTest>

#include "screencopy.h"

// Живая проверка против headless Sway. Если sway не установлен — пропуск.
class TestScreencopyLive : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // QSKIP(сообщение, ...) — вариативный макрос: вызов без второго аргумента под clang с -Wpedantic (C++17) даёт
        // -Wgnu-zero-variadic-macro-arguments (с -Werror — ошибку). Второй аргумент макрос отбрасывает.
        if (QStandardPaths::findExecutable(QStringLiteral("sway")).isEmpty())
            QSKIP("sway не установлен — живой тест screencopy пропущен", "");
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
