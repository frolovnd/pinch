#include "i18n.h"

#include <QCoreApplication>
#include <QLocale>
#include <QTest>
#include <QTranslator>

// Тесты ставят переводы через installTranslations; между тестами их надо снять.
class TestI18n : public QObject {
    Q_OBJECT

    static void removeTranslators()
    {
        const auto translators = QCoreApplication::instance()->findChildren<QTranslator*>();
        for (QTranslator* t : translators) {
            QCoreApplication::removeTranslator(t);
            delete t;
        }
    }

private slots:
    void cleanup() { removeTranslators(); }

    void russianLocale()
    {
        const QStringList loaded = installTranslations(*QCoreApplication::instance(), QLocale(QStringLiteral("ru_RU")));
        QVERIFY(loaded.contains(QStringLiteral("pinch_en")));
        QVERIFY(loaded.contains(QStringLiteral("pinch_ru")));
        QCOMPARE(qtTrId("overlay.hint"), QString::fromUtf8("Выделите область · Esc — отмена"));
    }

    void englishLocale()
    {
        const QStringList loaded = installTranslations(*QCoreApplication::instance(), QLocale(QStringLiteral("en_US")));
        QVERIFY(loaded.contains(QStringLiteral("pinch_en")));
        QVERIFY(!loaded.contains(QStringLiteral("pinch_ru")));
        QCOMPARE(qtTrId("overlay.hint"), QString::fromUtf8("Drag to select · Esc to cancel"));
    }

    void unknownLocaleFallsBackToEnglish()
    {
        installTranslations(*QCoreApplication::instance(), QLocale(QStringLiteral("de_DE")));
        QCOMPARE(qtTrId("overlay.hint"), QString::fromUtf8("Drag to select · Esc to cancel"));
    }

    void tablesDiffer()
    {
        installTranslations(*QCoreApplication::instance(), QLocale(QStringLiteral("en_US")));
        const QString en = qtTrId("toolbar.tool.pen");
        removeTranslators();
        installTranslations(*QCoreApplication::instance(), QLocale(QStringLiteral("ru_RU")));
        const QString ru = qtTrId("toolbar.tool.pen");
        QVERIFY(!en.isEmpty() && !ru.isEmpty());
        QVERIFY(en != QLatin1String("toolbar.tool.pen")); // ключ найден, а не возвращён как есть
        QVERIFY(ru != QLatin1String("toolbar.tool.pen"));
        QVERIFY(en != ru);
    }
};

QTEST_GUILESS_MAIN(TestI18n)
#include "test_i18n.moc"
