// Поддельный KWin: имя org.kde.KWin и org.kde.KWin.ScreenShot2 на сессионной шине. Режим — аргумент командной строки:
// ok | deny | short | badformat. Запускается отдельным процессом (приватная шина из tests/dbustestbus.h).
// Как настоящий KWin, опознаёт вызывающего по /proc/<pid>/exe (fakeauth.h): недампируемому — NoAuthorized.
#include "fakeauth.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusUnixFileDescriptor>
#include <QImage>
#include <QVariantMap>

#include <unistd.h>

#include <cstdint>
#include <thread>
#include <vector>

class FakeScreenShot2 : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.ScreenShot2")
public:
    explicit FakeScreenShot2(QString mode) : m_mode(std::move(mode)) {}

public slots:
    QVariantMap CaptureWorkspace(const QVariantMap& options, const QDBusUnixFileDescriptor& pipe)
    {
        Q_UNUSED(options);
        if (m_mode == QLatin1String("deny") || !fakeCallerExeReadable(connection(), message())) {
            sendErrorReply(QStringLiteral("org.kde.KWin.ScreenShot2.Error.NoAuthorized"),
                           QStringLiteral("The process is not authorized to take a screenshot"));
            return {};
        }
        // Пишем в дубликат дескриптора из отдельного потока, чтобы ответ не ждал читателя.
        const int fd = ::dup(pipe.fileDescriptor());
        const size_t bytes = m_mode == QLatin1String("short") ? 8 : 32;
        std::thread([fd, bytes] {
            std::vector<uint32_t> pixels(8, 0xff00ff00u);
            const char* p = reinterpret_cast<const char*>(pixels.data());
            size_t done = 0;
            while (done < bytes) {
                const ssize_t n = ::write(fd, p + done, bytes - done);
                if (n <= 0)
                    break;
                done += static_cast<size_t>(n);
            }
            ::close(fd);
        }).detach();

        const QImage::Format format = m_mode == QLatin1String("badformat") ? QImage::Format_Indexed8 : QImage::Format_RGB32;
        return {{QStringLiteral("type"), QStringLiteral("raw")},
                {QStringLiteral("width"), 4u},
                {QStringLiteral("height"), 2u},
                {QStringLiteral("stride"), 16u},
                {QStringLiteral("format"), static_cast<uint>(format)}};
    }

private:
    QString m_mode;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString mode = app.arguments().value(1, QStringLiteral("ok"));
    QDBusConnection bus = QDBusConnection::sessionBus();
    FakeScreenShot2 object(mode);
    if (!bus.registerObject(QStringLiteral("/org/kde/KWin/ScreenShot2"), &object, QDBusConnection::ExportAllSlots))
        return 2;
    if (!bus.registerService(QStringLiteral("org.kde.KWin")))
        return 3;
    return app.exec();
}

#include "fake_kwin.moc"
