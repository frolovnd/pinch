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
    return QDir(dir).entryList({QStringLiteral(".pinch-*")}, QDir::Files | QDir::Hidden);
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
        QVERIFY2(r.error.contains(dir.filePath(QStringLiteral("no/such"))), qPrintable(r.error));
        QVERIFY2(r.error.contains(QStringLiteral("Не удалось")), qPrintable(r.error));
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
