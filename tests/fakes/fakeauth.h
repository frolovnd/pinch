#pragma once

// Опознание вызывающего, как у настоящих служб: PID — у шины (org.freedesktop.DBus.GetConnectionUnixProcessID),
// затем KWin читает ссылку /proc/<pid>/exe, а xdg-desktop-portal открывает /proc/<pid>/root. У недампируемого процесса
// (PR_SET_DUMPABLE=0) ядро не даёт другим процессам этих ссылок — служба отказывает.
#include <QByteArray>
#include <QDBusConnection>
#include <QDBusMessage>

#include <fcntl.h>
#include <unistd.h>

inline uint fakeCallerPid(const QDBusConnection& bus, const QDBusMessage& message)
{
    QDBusMessage query = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                        QStringLiteral("/org/freedesktop/DBus"),
                                                        QStringLiteral("org.freedesktop.DBus"),
                                                        QStringLiteral("GetConnectionUnixProcessID"));
    query << message.service();
    const QDBusMessage reply = bus.call(query, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return 0;
    return reply.arguments().first().toUInt();
}

// Как KWin: readlink("/proc/<pid>/exe").
inline bool fakeCallerExeReadable(const QDBusConnection& bus, const QDBusMessage& message)
{
    const uint pid = fakeCallerPid(bus, message);
    if (pid == 0)
        return false;
    const QByteArray path = "/proc/" + QByteArray::number(pid) + "/exe";
    char target[4096];
    return ::readlink(path.constData(), target, sizeof target) > 0;
}

// Как xdg-desktop-portal: open("/proc/<pid>/root").
inline bool fakeCallerRootOpenable(const QDBusConnection& bus, const QDBusMessage& message)
{
    const uint pid = fakeCallerPid(bus, message);
    if (pid == 0)
        return false;
    const QByteArray path = "/proc/" + QByteArray::number(pid) + "/root";
    const int fd = ::open(path.constData(), O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
}
