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
