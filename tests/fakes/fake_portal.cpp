// Поддельный xdg-desktop-portal: org.freedesktop.portal.Desktop, интерфейс org.freedesktop.portal.Screenshot.
// Режим и путь к картинке — аргументы командной строки: <mode> <imagePath>; mode: ok | deny | symlink | silent | fifo | host | http.
// Как настоящий портал, опознаёт вызывающего по /proc/<pid>/root (fakeauth.h): недампируемому — AccessDenied.
#include "fakeauth.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QFile>
#include <QImage>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <sys/stat.h>

class FakeScreenshot : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Screenshot")
public:
    FakeScreenshot(QString mode, QString imagePath) : m_mode(std::move(mode)), m_imagePath(std::move(imagePath)) {}

public slots:
    QDBusObjectPath Screenshot(const QString& parent, const QVariantMap& options)
    {
        Q_UNUSED(parent);
        if (!fakeCallerRootOpenable(connection(), message())) {
            sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"),
                           QStringLiteral("Unable to open /proc/<pid>/root of the caller"));
            return {};
        }
        QString sender = message().service();
        sender.remove(QLatin1Char(':'));
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/") + sender + QLatin1Char('/')
            + options.value(QStringLiteral("handle_token")).toString();

        uint code = 0;
        QVariantMap results;
        if (m_mode == QLatin1String("ok")) {
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
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString mode = app.arguments().value(1, QStringLiteral("ok"));
    const QString imagePath = app.arguments().value(2);
    QDBusConnection bus = QDBusConnection::sessionBus();
    FakeScreenshot object(mode, imagePath);
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), &object, QDBusConnection::ExportAllSlots))
        return 2;
    if (!bus.registerService(QStringLiteral("org.freedesktop.portal.Desktop")))
        return 3;
    return app.exec();
}

#include "fake_portal.moc"
