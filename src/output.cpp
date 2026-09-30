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
QString sysText(int err)
{
    return QString::fromLocal8Bit(std::strerror(err));
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
    if (fd < 0) {
        const int err = errno; // до построения сообщения: порядок вычисления аргументов не определён
        return {WriteResult::Error,
                QStringLiteral("Не удалось создать временный файл в каталоге «%1»: %2")
                    .arg(info.absolutePath(), sysText(err))};
    }

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
        return {WriteResult::Error,
                QStringLiteral("Не удалось записать «%1»: %2").arg(info.absoluteFilePath(), sysText(err))};
    }

    // Переименование подменяет запись в каталоге и не следует по симлинку на месте target.
    const int rc = mode == WriteMode::NoReplace
        ? ::renameat2(AT_FDCWD, temp.constData(), AT_FDCWD, target.constData(), RENAME_NOREPLACE)
        : ::rename(temp.constData(), target.constData());
    if (rc != 0) {
        err = errno;
        ::unlink(temp.constData());
        if (err == EEXIST)
            return {WriteResult::Exists, QStringLiteral("Файл «%1» уже существует").arg(info.absoluteFilePath())};
        return {WriteResult::Error,
                QStringLiteral("Не удалось сохранить «%1»: %2").arg(info.absoluteFilePath(), sysText(err))};
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
