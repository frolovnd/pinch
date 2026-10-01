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

    // Ни проверка, ни снимок не запускают org.kde.KWin через D-Bus-активацию (setAutoStartService(false)):
    // на шине с активируемой «службой», которая лишь оставляет метку, метки быть не должно.
    void doesNotActivateService()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString marker = dir.filePath(QStringLiteral("activated"));
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("services")));
        QFile service(dir.filePath(QStringLiteral("services/org.kde.KWin.service")));
        QVERIFY(service.open(QIODevice::WriteOnly));
        service.write("[D-BUS Service]\nName=org.kde.KWin\nExec=/bin/sh -c \"touch '" + QFile::encodeName(marker) + "'\"\n");
        service.close();
        QFile config(dir.filePath(QStringLiteral("bus.conf")));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
                     " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                     "<busconfig>\n"
                     "  <type>session</type>\n"
                     "  <listen>unix:dir=" + QFile::encodeName(dir.path()) + "</listen>\n"
                     "  <servicedir>" + QFile::encodeName(dir.filePath(QStringLiteral("services"))) + "</servicedir>\n"
                     "  <policy context=\"default\">\n"
                     "    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
                     "    <allow eavesdrop=\"true\"/>\n"
                     "    <allow own=\"*\"/>\n"
                     "  </policy>\n"
                     "</busconfig>\n");
        config.close();

        DBusTestBus bus;
        QVERIFY(bus.start(config.fileName()));
        QDBusConnection c = bus.connect(QStringLiteral("t-noact"));
        QVERIFY(!kwinScreenShotAvailable(c));
        QString error;
        QVERIFY(!captureWithKWin(c, {QRect(0, 0, 4, 2)}, &error).has_value());
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(marker));
    }

    // Больше буфера канала (2560×1440, ~14 МиБ), и KWin пишет всё до ответа: без параллельного читателя клиент ждал бы
    // ответа, а KWin — свободного места в канале.
    void readsLargeImageWrittenBeforeReply()
    {
        runFake(QStringLiteral("big"));
        QString error;
        QElapsedTimer timer;
        timer.start();
        const auto cap = captureWithKWin(m_bus->connect(QStringLiteral("t-big")), {QRect(0, 0, 2560, 1440)}, &error);
        QVERIFY2(cap.has_value(), qPrintable(error));
        QVERIFY(timer.elapsed() < 4000); // заметно меньше тайм-аута вызова (5 с)
        QCOMPARE(cap->image.size(), QSize(2560, 1440));
        QCOMPARE(cap->image.pixel(0, 0), qRgb(0xff, 0, 0));
        QCOMPARE(cap->image.pixel(2559, 1439), qRgb(0x33, 0x66, 0xcc));
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
