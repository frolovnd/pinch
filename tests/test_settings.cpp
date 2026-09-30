#include <QtTest>

#include "settings.h"

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_dir.path());
    }

    void init()
    {
        raw().clear();
    }

    void defaultsWhenEmpty()
    {
        const Settings s = Settings::load();
        QCOMPARE(s.color, QColor(0xE5, 0x39, 0x35));
        QCOMPARE(s.thickness, 4);
    }

    void roundTrip()
    {
        Settings s;
        s.color = QColor(0x1E, 0x88, 0xE5);
        s.thickness = 12;
        s.save();
        const Settings loaded = Settings::load();
        QCOMPARE(loaded.color, QColor(0x1E, 0x88, 0xE5));
        QCOMPARE(loaded.thickness, 12);
    }

    void invalidValuesFallBack()
    {
        {
            QSettings r = raw();
            r.setValue(QStringLiteral("color"), QStringLiteral("not-a-color"));
            r.setValue(QStringLiteral("thickness"), 999);
        }
        Settings s = Settings::load();
        QCOMPARE(s.color, QColor(0xE5, 0x39, 0x35));
        QCOMPARE(s.thickness, 40);

        {
            QSettings r = raw();
            r.setValue(QStringLiteral("color"), QStringLiteral("#12345g"));
            r.setValue(QStringLiteral("thickness"), QStringLiteral("abc"));
        }
        s = Settings::load();
        QCOMPARE(s.color, QColor(0xE5, 0x39, 0x35));
        QCOMPARE(s.thickness, 4);

        raw().setValue(QStringLiteral("thickness"), 0);
        QCOMPARE(Settings::load().thickness, 1);
    }

private:
    static QSettings raw() { return QSettings(QStringLiteral("hot-screenshot"), QStringLiteral("hot-screenshot")); }
    QTemporaryDir m_dir;
};

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
