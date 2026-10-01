#include <QtTest>

#include "dbustestbus.h"
#include "i18n.h"
#include "portalcapture.h"
#include "testtracer.h"

#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

class TestPortalCapture : public QObject {
    Q_OBJECT
private slots:
    // Язык зафиксирован, чтобы текст ошибок проверялся по ключам и не зависел от LANG.
    void initTestCase() { installTranslations(*QCoreApplication::instance(), QLocale(QStringLiteral("en_US"))); }
    void cleanup() { prctl(PR_SET_DUMPABLE, 1, 0, 0, 0); } // тесты ниже делают процесс недампируемым, как pinch

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

    // Поддельный портал, как настоящий, открывает /proc/<pid>/root вызывающего: недампируемому процессу — отказ.
    void fakeRejectsNonDumpableCaller()
    {
        QTemporaryDir dir;
        DBusTestBus bus;
        QVERIFY(bus.start());
        QProcess fake;
        QVERIFY(bus.startService(fake, QStringLiteral(FAKE_PORTAL_PATH), {QStringLiteral("ok"), dir.filePath(QStringLiteral("x.png"))},
                                 QStringLiteral("org.freedesktop.portal.Desktop")));
        QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
                                                          QStringLiteral("/org/freedesktop/portal/desktop"),
                                                          QStringLiteral("org.freedesktop.portal.Screenshot"),
                                                          QStringLiteral("Screenshot"));
        msg << QString() << QVariantMap{{QStringLiteral("handle_token"), QStringLiteral("raw")}};
        QDBusConnection c = bus.connect(QStringLiteral("p-raw"));
        QCOMPARE(prctl(PR_SET_DUMPABLE, 0, 0, 0, 0), 0);
        const QDBusMessage reply = c.call(msg, QDBus::Block, 5000);
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(reply.errorName(), QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"));
        fake.terminate();
        fake.waitForFinished(3000);
    }

    // pinch недампируемый (main): на время вызовов портала ScopedDumpable открывает окно, затем закрывает.
    void capturesWhenNotDumpable()
    {
        QTemporaryDir dir;
        const QString img = dir.filePath(QStringLiteral("portal.png"));
        DBusTestBus bus;
        QVERIFY(bus.start());
        QProcess fake;
        QVERIFY(bus.startService(fake, QStringLiteral(FAKE_PORTAL_PATH), {QStringLiteral("ok"), img},
                                 QStringLiteral("org.freedesktop.portal.Desktop")));
        QCOMPARE(prctl(PR_SET_DUMPABLE, 0, 0, 0, 0), 0);
        QString error;
        const auto cap = captureWithPortal(bus.connect(QStringLiteral("p-nodump")), {QRect(0, 0, 4, 2)}, &error);
        QCOMPARE(prctl(PR_GET_DUMPABLE, 0, 0, 0, 0), 0); // окно закрыто
        QVERIFY2(cap.has_value(), qPrintable(error));
        QCOMPARE(cap->image.pixel(0, 0), qRgb(0, 0, 0xff));
        fake.terminate();
        fake.waitForFinished(3000);
    }

    // Трассировщик подключился ещё до PR_SET_DUMPABLE=0: окно не открывается, снимок прерывается с понятной ошибкой.
    void abortsWhenTraced()
    {
        QTemporaryDir dir;
        DBusTestBus bus;
        QVERIFY(bus.start());
        QProcess fake;
        QVERIFY(bus.startService(fake, QStringLiteral(FAKE_PORTAL_PATH), {QStringLiteral("ok"), dir.filePath(QStringLiteral("x.png"))},
                                 QStringLiteral("org.freedesktop.portal.Desktop")));
        TestTracer tracer;
        if (!tracer.attach(::getpid()))
            QSKIP("ptrace is not permitted in this environment", "");
        QCOMPARE(prctl(PR_SET_DUMPABLE, 0, 0, 0, 0), 0);
        QString error;
        const bool captured = captureWithPortal(bus.connect(QStringLiteral("p-traced")), {QRect(0, 0, 4, 2)}, &error).has_value();
        tracer.release();
        QVERIFY(!captured);
        QCOMPARE(error, qtTrId("error.capture.tracer_attached"));
        fake.terminate();
        fake.waitForFinished(3000);
    }

    void deniedIsExplained()
    {
        expectError(QStringLiteral("deny"), qtTrId("error.capture.portal.denied"));
    }

    void timeoutIsReported()
    {
        expectError(QStringLiteral("silent"), qtTrId("error.capture.portal.timeout"), 500);
    }

    void fifoIsRejectedWithoutHang()
    {
        QElapsedTimer t;
        t.start();
        expectError(QStringLiteral("fifo"), qtTrId("error.capture.portal.not_regular_file"));
        QVERIFY(t.elapsed() < 10000);
    }

    void foreignHostIsRejected()
    {
        expectError(QStringLiteral("host"), qtTrId("error.capture.portal.bad_uri"));
    }

    void nonFileSchemeIsRejected()
    {
        expectError(QStringLiteral("http"), qtTrId("error.capture.portal.bad_uri"));
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
        QTest::ignoreMessage(QtWarningMsg, qPrintable(qtTrId("warning.capture.portal.file_kept").arg(img)));
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
