#include <QtTest>

#include "dbustestbus.h"
#include "i18n.h"
#include "kwincapture.h"
#include "testtracer.h"

#include <QDBusUnixFileDescriptor>

#include <fcntl.h>
#include <sys/prctl.h>
#include <unistd.h>

class TestKWinCapture : public QObject {
    Q_OBJECT
private slots:
    // Переводы нужны для проверки текста ошибок по ключам; язык зафиксирован, чтобы тест не зависел от LANG.
    void initTestCase() { installTranslations(*QCoreApplication::instance(), QLocale(QStringLiteral("en_US"))); }

    void init()
    {
        m_bus = std::make_unique<DBusTestBus>();
        QVERIFY(m_bus->start());
    }
    void cleanup()
    {
        prctl(PR_SET_DUMPABLE, 1, 0, 0, 0); // тесты ниже делают процесс недампируемым, как pinch
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

    void scalesToScreenUnion()
    {
        runFake(QStringLiteral("ok"));
        QString error;
        const QVector<QRect> screens{QRect(100, 50, 4, 1), QRect(100, 51, 4, 1)};
        const auto cap = captureWithKWin(m_bus->connect(QStringLiteral("t-scale")), screens, &error);
        QVERIFY2(cap.has_value(), qPrintable(error));
        QCOMPARE(cap->origin, QPoint(100, 50));
        QCOMPARE(cap->screens, (QVector<QRect>{QRect(0, 0, 4, 1), QRect(0, 1, 4, 1)}));
    }

    // Поддельный KWin, как настоящий, читает /proc/<pid>/exe вызывающего: недампируемому процессу — отказ.
    void fakeRejectsNonDumpableCaller()
    {
        runFake(QStringLiteral("ok"));
        QDBusConnection c = m_bus->connect(QStringLiteral("t-raw"));
        int fds[2];
        QCOMPARE(::pipe2(fds, O_CLOEXEC), 0);
        QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/ScreenShot2"),
                                                          QStringLiteral("org.kde.KWin.ScreenShot2"),
                                                          QStringLiteral("CaptureWorkspace"));
        msg << QVariantMap() << QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]));
        QCOMPARE(prctl(PR_SET_DUMPABLE, 0, 0, 0, 0), 0);
        const QDBusMessage reply = c.call(msg, QDBus::Block, 5000);
        ::close(fds[0]);
        ::close(fds[1]);
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(reply.errorName(), QStringLiteral("org.kde.KWin.ScreenShot2.Error.NoAuthorized"));
    }

    // pinch недампируемый (main): на время вызова CaptureWorkspace ScopedDumpable открывает окно, затем закрывает.
    void capturesWhenNotDumpable()
    {
        runFake(QStringLiteral("ok"));
        QCOMPARE(prctl(PR_SET_DUMPABLE, 0, 0, 0, 0), 0);
        QString error;
        const auto cap = captureWithKWin(m_bus->connect(QStringLiteral("t-nodump")), {QRect(0, 0, 4, 2)}, &error);
        QCOMPARE(prctl(PR_GET_DUMPABLE, 0, 0, 0, 0), 0); // окно закрыто
        QVERIFY2(cap.has_value(), qPrintable(error));
        QCOMPARE(cap->image.pixel(3, 1), qRgb(0, 0xff, 0));
    }

    // Трассировщик подключился ещё до PR_SET_DUMPABLE=0 (запуск под strace без PINCH_ALLOW_TRACE): окно не открывается,
    // снимок прерывается с понятной ошибкой.
    void abortsWhenTraced()
    {
        runFake(QStringLiteral("ok"));
        TestTracer tracer;
        if (!tracer.attach(::getpid()))
            QSKIP("ptrace is not permitted in this environment", "");
        QCOMPARE(prctl(PR_SET_DUMPABLE, 0, 0, 0, 0), 0);
        QString error;
        const bool captured = captureWithKWin(m_bus->connect(QStringLiteral("t-traced")), {QRect(0, 0, 4, 2)}, &error).has_value();
        tracer.release();
        QVERIFY(!captured);
        QCOMPARE(error, qtTrId("error.capture.tracer_attached"));
    }

    void notAvailableWithoutService()
    {
        QVERIFY(!kwinScreenShotAvailable(m_bus->connect(QStringLiteral("t-none"))));
    }

    void rejectsDenied()
    {
        expectError(QStringLiteral("deny"), QStringLiteral("authorized"));
        // Подсказка про установку .desktop — по ключу, не по тексту.
        QVERIFY2(m_lastError.contains(qtTrId("error.capture.kwin.desktop_hint")), qPrintable(m_lastError));
    }
    void rejectsShortData() { expectError(QStringLiteral("short"), qtTrId("error.capture.kwin.bad_reply")); }
    void rejectsBadFormat() { expectError(QStringLiteral("badformat"), qtTrId("error.capture.kwin.bad_reply")); }

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
        m_lastError = error;
    }

    std::unique_ptr<DBusTestBus> m_bus;
    QProcess m_fake;
    QString m_lastError;
};

QTEST_GUILESS_MAIN(TestKWinCapture)
#include "test_kwincapture.moc"
