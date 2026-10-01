#pragma once

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QThread>

// Запускает приватный dbus-daemon (сессионная конфигурация или своя, configFile) и даёт его адрес.
// Останавливает в деструкторе.
class DBusTestBus {
public:
    bool start(const QString& configFile = QString())
    {
        m_daemon.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        const QString config = configFile.isEmpty() ? QStringLiteral("--session") : QStringLiteral("--config-file=") + configFile;
        m_daemon.start(QStringLiteral("dbus-daemon"), {config, QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        if (!m_daemon.waitForStarted(5000))
            return false;
        QElapsedTimer timer;
        timer.start();
        QByteArray line;
        while (!line.contains('\n')) {
            const qint64 left = 5000 - timer.elapsed();
            if (left <= 0 || !m_daemon.waitForReadyRead(static_cast<int>(left)))
                return false;
            line += m_daemon.readAllStandardOutput();
        }
        m_address = QString::fromLocal8Bit(line).section(QLatin1Char('\n'), 0, 0).trimmed();
        return !m_address.isEmpty();
    }

    QString address() const { return m_address; }

    QDBusConnection connect(const QString& name)
    {
        m_connections << name;
        return QDBusConnection::connectToBus(m_address, name);
    }

    // Запускает помощник с DBUS_SESSION_BUS_ADDRESS=address и ждёт появления имени сервиса на шине (тайм-аут 5 с).
    bool startService(QProcess& process, const QString& program, const QStringList& args, const QString& serviceName)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), m_address);
        // Утечки в короткоживущем поддельном процессе (под ASan) не должны валить тест: проверяем клиента, не помощника.
        env.insert(QStringLiteral("ASAN_OPTIONS"), QStringLiteral("detect_leaks=0"));
        process.setProcessEnvironment(env);
        process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        process.start(program, args);
        if (!process.waitForStarted(5000))
            return false;
        QDBusConnection probe = connect(QStringLiteral("probe-") + serviceName + QString::number(m_probes++));
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            if (process.state() == QProcess::NotRunning)
                return false;
            const auto reply = probe.interface()->isServiceRegistered(serviceName);
            if (reply.isValid() && reply.value())
                return true;
            QThread::msleep(25);
        }
        return false;
    }

    ~DBusTestBus()
    {
        for (const QString& name : std::as_const(m_connections))
            QDBusConnection::disconnectFromBus(name);
        if (m_daemon.state() != QProcess::NotRunning) {
            m_daemon.terminate();
            if (!m_daemon.waitForFinished(3000)) {
                m_daemon.kill();
                m_daemon.waitForFinished(3000);
            }
        }
    }

private:
    QProcess m_daemon;
    QString m_address;
    QStringList m_connections;
    int m_probes = 0;
};
