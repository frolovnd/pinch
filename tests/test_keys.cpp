#include <QtTest>

#include "keys.h"

class TestKeys : public QObject {
    Q_OBJECT
private slots:
    void latinLettersPassThrough()
    {
        QCOMPARE(layoutIndependentKey(Qt::Key_P, 0), int(Qt::Key_P));
        QCOMPARE(layoutIndependentKey(Qt::Key_C, 999), int(Qt::Key_C));
    }

    void cyrillicMapsByScanCode()
    {
        QCOMPARE(layoutIndependentKey(0x0417, 33), int(Qt::Key_P)); // «З» на месте P
        QCOMPARE(layoutIndependentKey(0x0421, 54), int(Qt::Key_C)); // «С» на месте C
        QCOMPARE(layoutIndependentKey(0x042B, 39), int(Qt::Key_S)); // «Ы» на месте S
        QCOMPARE(layoutIndependentKey(0x042F, 52), int(Qt::Key_Z)); // «Я» на месте Z
    }

    void otherKeysUnchanged()
    {
        QCOMPARE(layoutIndependentKey(Qt::Key_Escape, 9), int(Qt::Key_Escape));
        QCOMPARE(layoutIndependentKey(Qt::Key_Return, 36), int(Qt::Key_Return));
        QCOMPARE(layoutIndependentKey(Qt::Key_1, 10), int(Qt::Key_1));
        QCOMPARE(layoutIndependentKey(0x0416, 47), 0x0416); // «Ж» на месте «;» — не буква латиницы
        QCOMPARE(layoutIndependentKey(0x0417, 0), 0x0417);  // скан-код неизвестен
    }
};

QTEST_MAIN(TestKeys)
#include "test_keys.moc"
