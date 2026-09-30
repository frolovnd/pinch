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
