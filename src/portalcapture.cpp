#include "portalcapture.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <sys/stat.h>
#include <unistd.h>

namespace {

const QString PORTAL_SERVICE = QStringLiteral("org.freedesktop.portal.Desktop");
const QString PORTAL_PATH = QStringLiteral("/org/freedesktop/portal/desktop");

// Получает сигнал Response и будит цикл ожидания.
class ResponseWaiter : public QObject {
    Q_OBJECT
public:
    bool done = false;
    uint response = 1;
    QVariantMap results;
    QEventLoop loop;

public slots:
    void onResponse(uint code, const QVariantMap& map)
    {
        if (done)
            return;
        done = true;
        response = code;
        results = map;
        loop.quit();
    }
};

} // namespace

bool shouldDeletePortalFile(const QString& path, qint64 requestStartSecs)
{
    struct stat st;
    // lstat не следует за симлинками: симлинк сам по себе не обычный файл.
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0)
        return false;
    return S_ISREG(st.st_mode) && st.st_uid == ::getuid() && static_cast<qint64>(st.st_mtime) >= requestStartSecs - 2;
}

QString portalRequestPath(const QString& uniqueName, const QString& token)
{
    QString sender = uniqueName;
    sender.remove(QLatin1Char(':'));
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/") + sender + QLatin1Char('/') + token;
}

std::optional<Capture> captureWithPortal(QDBusConnection bus, const QVector<QRect>& screens, QString* error,
                                         int timeoutMs)
{
    auto fail = [error](const QString& text) -> std::optional<Capture> {
        if (error)
            *error = text;
        return std::nullopt;
    };
    if (screens.isEmpty())
        return fail(qtTrId("error.capture.portal.no_screens"));
    if (!bus.isConnected())
        return fail(qtTrId("error.capture.portal.call_failed").arg(bus.lastError().message()));

    // 1. Регистрация приложения в портале (если реестр есть); ошибки игнорируются.
    const QString registry = QStringLiteral("org.freedesktop.host.portal.Registry");
    if (bus.interface() && bus.interface()->isServiceRegistered(registry).value()) {
        QDBusMessage reg = QDBusMessage::createMethodCall(registry, PORTAL_PATH, registry, QStringLiteral("Register"));
        reg << QStringLiteral("pinch") << QVariantMap();
        bus.call(reg, QDBus::Block, 2000);
    }

    // 2. Подписка на Response до вызова, иначе быстрый ответ потеряется.
    const QString token = QStringLiteral("pinch") + QString::number(QRandomGenerator::global()->generate());
    const QString path = portalRequestPath(bus.baseService(), token);
    ResponseWaiter waiter;
    if (!bus.connect(QString(), path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                     &waiter, SLOT(onResponse(uint, QVariantMap))))
        return fail(qtTrId("error.capture.portal.call_failed").arg(bus.lastError().message()));
    struct Unsubscribe {
        QDBusConnection& bus;
        const QString& path;
        ResponseWaiter& waiter;
        ~Unsubscribe()
        {
            bus.disconnect(QString(), path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                           &waiter, SLOT(onResponse(uint, QVariantMap)));
        }
    } unsubscribe{bus, path, waiter};

    // 3. Вызов Screenshot (неинтерактивный).
    const qint64 requestStart = QDateTime::currentSecsSinceEpoch();
    QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH,
                                                      QStringLiteral("org.freedesktop.portal.Screenshot"),
                                                      QStringLiteral("Screenshot"));
    const QVariantMap options{{QStringLiteral("handle_token"), token}, {QStringLiteral("interactive"), false}};
    msg << QString() << options;
    const QDBusMessage reply = bus.call(msg, QDBus::Block, timeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        QString text = reply.errorMessage();
        if (text.isEmpty())
            text = reply.errorName();
        return fail(qtTrId("error.capture.portal.call_failed").arg(text));
    }
    if (reply.type() != QDBusMessage::ReplyMessage)
        return fail(qtTrId("error.capture.portal.call_failed").arg(bus.lastError().message()));

    // 4. Ожидание ответа с пределом по времени.
    if (!waiter.done) {
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &waiter.loop, &QEventLoop::quit);
        timer.start(timeoutMs);
        waiter.loop.exec();
    }
    if (!waiter.done)
        return fail(qtTrId("error.capture.portal.timeout"));

    // 5. Отказ.
    if (waiter.response != 0)
        return fail(qtTrId("error.capture.portal.denied"));

    // 6. Только file://; содержимое читается как изображение.
    const QString uri = waiter.results.value(QStringLiteral("uri")).toString();
    const QUrl url(uri);
    if (!uri.startsWith(QLatin1String("file://")) || !url.isLocalFile())
        return fail(qtTrId("error.capture.portal.bad_uri"));
    const QString localPath = url.toLocalFile();
    const QImage image(localPath);

    // 7. Файл удаляется, только если он явно наш и свежий; иначе остаётся.
    if (shouldDeletePortalFile(localPath, requestStart))
        ::unlink(QFile::encodeName(localPath).constData());
    else
        qWarning().noquote() << qtTrId("warning.capture.portal.file_kept").arg(localPath);

    if (image.isNull())
        return fail(qtTrId("error.capture.portal.bad_image"));
    return captureFromWorkspaceImage(image, screens);
}

#include "portalcapture.moc"
