#include "kwincapture.h"
#include "rawimage.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QVariantMap>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

namespace {

constexpr int TIMEOUT_MS = 5000;
// Санитарный предел объёма данных от KWin (больше любого реального экрана в 4 байта на пиксель не бывает разумным).
constexpr qsizetype MAX_DATA_BYTES = qsizetype(1) << 30;

const QString SERVICE = QStringLiteral("org.kde.KWin");
const QString PATH = QStringLiteral("/org/kde/KWin/ScreenShot2");
const QString INTERFACE = QStringLiteral("org.kde.KWin.ScreenShot2");

struct ReadResult {
    QByteArray data;
    bool timedOut = false;
    bool failed = false;
};

// Читает fd до EOF; общий тайм-аут — TIMEOUT_MS. Работает параллельно с вызовом D-Bus, чтобы писатель,
// заполнивший канал до ответа, не вызвал взаимную блокировку.
void readAll(int fd, ReadResult* result)
{
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(TIMEOUT_MS);
    char buffer[65536];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) {
            result->timedOut = true;
            return;
        }
        pollfd p{fd, POLLIN, 0};
        const int r = ::poll(&p, 1, static_cast<int>(left));
        if (r < 0) {
            if (errno == EINTR)
                continue;
            result->failed = true;
            return;
        }
        if (r == 0)
            continue; // тайм-аут проверит начало цикла
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n == 0)
            return; // EOF
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            result->failed = true;
            return;
        }
        if (result->data.size() + n > MAX_DATA_BYTES) {
            result->failed = true;
            return;
        }
        result->data.append(buffer, static_cast<qsizetype>(n));
    }
}

// Значение из ответа KWin: может прийти как QDBusVariant, uint, int и т. п.
std::optional<int> intValue(QVariant v)
{
    while (v.metaType() == QMetaType::fromType<QDBusVariant>())
        v = v.value<QDBusVariant>().variant();
    bool ok = false;
    const qlonglong n = v.toLongLong(&ok);
    if (!ok || n < 0 || n > INT32_MAX)
        return std::nullopt;
    return static_cast<int>(n);
}

QString stringValue(QVariant v)
{
    while (v.metaType() == QMetaType::fromType<QDBusVariant>())
        v = v.value<QDBusVariant>().variant();
    return v.metaType() == QMetaType::fromType<QString>() ? v.toString() : QString();
}

std::optional<QVariantMap> mapFromReply(const QDBusMessage& reply)
{
    if (reply.arguments().isEmpty())
        return std::nullopt;
    QVariant v = reply.arguments().first();
    if (v.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariantMap>(v.value<QDBusArgument>());
    if (v.canConvert<QVariantMap>())
        return v.toMap();
    return std::nullopt;
}

} // namespace

bool kwinScreenShotAvailable(QDBusConnection bus)
{
    if (!bus.isConnected())
        return false;
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE, PATH, QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                                      QStringLiteral("Introspect"));
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return false;
    return reply.arguments().first().toString().contains(INTERFACE);
}

std::optional<Capture> captureWithKWin(QDBusConnection bus, const QVector<QRect>& screens, QString* error)
{
    auto fail = [error](const QString& text) -> std::optional<Capture> {
        if (error)
            *error = text;
        return std::nullopt;
    };
    if (screens.isEmpty())
        return fail(qtTrId("error.capture.kwin.no_screens"));

    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0)
        return fail(qtTrId("error.capture.kwin.pipe_failed").arg(QString::fromLocal8Bit(std::strerror(errno))));

    ReadResult read;
    std::thread reader(readAll, fds[0], &read);

    QDBusMessage reply;
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE, PATH, INTERFACE, QStringLiteral("CaptureWorkspace"));
        const QVariantMap options{{QStringLiteral("include-cursor"), false}, {QStringLiteral("native-resolution"), false}};
        msg << options << QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]));
        reply = bus.call(msg, QDBus::Block, TIMEOUT_MS);
        // Наш конец канала закрываем сразу после вызова; копия в сообщении закроется вместе с ним (конец блока),
        // после чего читатель увидит EOF, как только закроет свой конец и KWin.
        ::close(fds[1]);
    }
    reader.join();
    ::close(fds[0]);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        QString text = reply.errorMessage();
        if (text.isEmpty())
            text = reply.errorName();
        if (reply.errorName().endsWith(QLatin1String("NoAuthorized")))
            text += QStringLiteral(" ") + qtTrId("error.capture.kwin.desktop_hint");
        return fail(qtTrId("error.capture.kwin.call_failed").arg(text));
    }
    if (reply.type() != QDBusMessage::ReplyMessage)
        return fail(qtTrId("error.capture.kwin.call_failed").arg(bus.lastError().message()));
    if (read.timedOut)
        return fail(qtTrId("error.capture.kwin.read_timeout"));
    if (read.failed)
        return fail(qtTrId("error.capture.kwin.bad_reply"));

    // Ответ KWin недоверенный: тип, размеры и формат проверяются.
    const auto map = mapFromReply(reply);
    if (!map || stringValue(map->value(QStringLiteral("type"))) != QLatin1String("raw"))
        return fail(qtTrId("error.capture.kwin.bad_reply"));
    const auto width = intValue(map->value(QStringLiteral("width")));
    const auto height = intValue(map->value(QStringLiteral("height")));
    const auto stride = intValue(map->value(QStringLiteral("stride")));
    const auto format = intValue(map->value(QStringLiteral("format")));
    if (!width || !height || !stride || !format)
        return fail(qtTrId("error.capture.kwin.bad_reply"));
    auto image = imageFromKWin(read.data, *width, *height, *stride, *format);
    if (!image)
        return fail(qtTrId("error.capture.kwin.bad_reply"));

    return captureFromWorkspaceImage(*image, screens);
}
