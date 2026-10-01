// Поддельный xdg-desktop-portal: org.freedesktop.portal.Desktop, интерфейсы org.freedesktop.portal.Screenshot и
// org.freedesktop.host.portal.Registry (как в xdg-desktop-portal ≥ 1.19 — на том же объекте).
// Режим и путь к картинке — аргументы командной строки: <mode> <imagePath>;
// mode: ok | deny | symlink | silent | fifo | host | http | noregistry | late.
// Как настоящий портал, опознаёт вызывающего по /proc/<pid>/root (fakeauth.h): недампируемому — AccessDenied.
// Screenshot отвечает только соединению, которое прежде зарегистрировалось с идентификатором PINCH_APP_ID;
// в режиме noregistry (старый портал) интерфейса Registry нет и регистрация не нужна.
#include "fakeauth.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QFile>
#include <QImage>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <sys/stat.h>

// Объект запроса для режима late: «портал» отвечает через 1 с и пишет файл со снимком, если запрос не закрыли раньше.
// Close оставляет метку <imagePath>.closed — по ней тест видит, что pinch закрыл запрос.
class FakeRequest : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
public:
    FakeRequest(QString path, QString imagePath, QObject* parent)
        : QObject(parent), m_path(std::move(path)), m_imagePath(std::move(imagePath))
    {
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, &FakeRequest::respond);
        m_timer.start(1000);
    }

public slots:
    void Close()
    {
        m_timer.stop();
        QFile marker(m_imagePath + QStringLiteral(".closed"));
        if (marker.open(QIODevice::WriteOnly))
            marker.close();
        finish();
    }

private:
    void respond()
    {
        QImage img(4, 2, QImage::Format_RGB32);
        img.fill(qRgb(0, 0, 0xff));
        img.save(m_imagePath, "PNG");
        QDBusMessage signal = QDBusMessage::createSignal(m_path, QStringLiteral("org.freedesktop.portal.Request"),
                                                         QStringLiteral("Response"));
        signal << 0u << QVariantMap{{QStringLiteral("uri"), QUrl::fromLocalFile(m_imagePath).toString()}};
        QDBusConnection::sessionBus().send(signal);
        finish();
    }

    void finish()
    {
        QDBusConnection::sessionBus().unregisterObject(m_path);
        deleteLater();
    }

    QString m_path;
    QString m_imagePath;
    QTimer m_timer;
};

class FakeScreenshot : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Screenshot")
public:
    FakeScreenshot(QString mode, QString imagePath) : m_mode(std::move(mode)), m_imagePath(std::move(imagePath)) {}

    bool registryEnabled() const { return m_mode != QLatin1String("noregistry"); }

    // Registry.Register (вызывается адаптером; контекст D-Bus Qt выставляет у этого объекта).
    void registerApp(const QString& appId)
    {
        if (!fakeCallerRootOpenable(connection(), message())) {
            sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"),
                           QStringLiteral("Unable to open /proc/<pid>/root of the caller"));
            return;
        }
        if (appId != QLatin1String(PINCH_APP_ID)) {
            sendErrorReply(QStringLiteral("org.freedesktop.portal.Error.InvalidArgument"),
                           QStringLiteral("Unexpected application id: ") + appId);
            return;
        }
        m_registered.insert(message().service());
    }

public slots:
    QDBusObjectPath Screenshot(const QString& parent, const QVariantMap& options)
    {
        Q_UNUSED(parent);
        if (!fakeCallerRootOpenable(connection(), message())) {
            sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"),
                           QStringLiteral("Unable to open /proc/<pid>/root of the caller"));
            return {};
        }
        if (registryEnabled() && !m_registered.contains(message().service())) {
            sendErrorReply(QStringLiteral("org.freedesktop.portal.Error.NotAllowed"),
                           QStringLiteral("The application did not register through org.freedesktop.host.portal.Registry"));
            return {};
        }
        QString sender = message().service();
        sender.remove(QLatin1Char(':'));
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/") + sender + QLatin1Char('/')
            + options.value(QStringLiteral("handle_token")).toString();

        uint code = 0;
        QVariantMap results;
        if (m_mode == QLatin1String("ok") || m_mode == QLatin1String("noregistry")) {
            QImage img(4, 2, QImage::Format_RGB32);
            img.fill(qRgb(0, 0, 0xff));
            img.save(m_imagePath, "PNG");
            results.insert(QStringLiteral("uri"), QUrl::fromLocalFile(m_imagePath).toString());
        } else if (m_mode == QLatin1String("symlink")) {
            QImage img(4, 2, QImage::Format_RGB32);
            img.fill(qRgb(0, 0, 0xff));
            const QString target = m_imagePath + QStringLiteral(".real.png");
            img.save(target, "PNG");
            QFile::remove(m_imagePath);
            QFile::link(target, m_imagePath);
            results.insert(QStringLiteral("uri"), QUrl::fromLocalFile(m_imagePath).toString());
        } else if (m_mode == QLatin1String("fifo")) {
            ::mkfifo(QFile::encodeName(m_imagePath).constData(), 0600);
            results.insert(QStringLiteral("uri"), QUrl::fromLocalFile(m_imagePath).toString());
        } else if (m_mode == QLatin1String("host")) {
            results.insert(QStringLiteral("uri"), QStringLiteral("file://otherhost/x.png"));
        } else if (m_mode == QLatin1String("http")) {
            results.insert(QStringLiteral("uri"), QStringLiteral("http://example.com/x.png"));
        } else if (m_mode == QLatin1String("deny")) {
            code = 1;
        } else if (m_mode == QLatin1String("late")) {
            connection().registerObject(path, new FakeRequest(path, m_imagePath, this), QDBusConnection::ExportAllSlots);
            return QDBusObjectPath(path);
        } else {
            return QDBusObjectPath(path); // silent: сигнал не посылаем
        }
        QTimer::singleShot(50, this, [path, code, results] {
            QDBusMessage signal = QDBusMessage::createSignal(path, QStringLiteral("org.freedesktop.portal.Request"),
                                                             QStringLiteral("Response"));
            signal << code << results;
            QDBusConnection::sessionBus().send(signal);
        });
        return QDBusObjectPath(path);
    }

private:
    QString m_mode;
    QString m_imagePath;
    QSet<QString> m_registered; // уникальные имена соединений, прошедших Register
};

class FakeRegistry : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.host.portal.Registry")
public:
    explicit FakeRegistry(FakeScreenshot* portal) : QDBusAbstractAdaptor(portal), m_portal(portal) {}

public slots:
    void Register(const QString& appId, const QVariantMap& options)
    {
        Q_UNUSED(options);
        m_portal->registerApp(appId);
    }

private:
    FakeScreenshot* m_portal;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString mode = app.arguments().value(1, QStringLiteral("ok"));
    const QString imagePath = app.arguments().value(2);
    QDBusConnection bus = QDBusConnection::sessionBus();
    FakeScreenshot object(mode, imagePath);
    if (object.registryEnabled())
        new FakeRegistry(&object); // дочерний объект: удаляется вместе с object
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), &object,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAdaptors))
        return 2;
    if (!bus.registerService(QStringLiteral("org.freedesktop.portal.Desktop")))
        return 3;
    return app.exec();
}

#include "fake_portal.moc"
