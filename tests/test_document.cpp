#include <QtTest>

#include "document.h"

namespace {
Annotation make(Tool tool)
{
    Annotation a;
    a.tool = tool;
    a.style = Style{QColor(255, 0, 0), 4};
    a.points = {QPoint(1, 1), QPoint(10, 10)};
    return a;
}
}

class TestDocument : public QObject {
    Q_OBJECT
private slots:
    void emptyDocument()
    {
        Document d;
        QVERIFY(d.annotations().isEmpty());
        QVERIFY(!d.canUndo());
        QVERIFY(!d.canRedo());
        QVERIFY(!d.undo());
        QVERIFY(!d.redo());
        QCOMPARE(d.nextCounterNumber(), 1);
    }

    void undoRedo()
    {
        Document d;
        d.add(make(Tool::Rect));
        d.add(make(Tool::Arrow));
        QCOMPARE(d.annotations().size(), 2);
        QVERIFY(d.undo());
        QCOMPARE(d.annotations().size(), 1);
        QCOMPARE(d.annotations().at(0).tool, Tool::Rect);
        QVERIFY(d.canRedo());
        QVERIFY(d.redo());
        QCOMPARE(d.annotations().size(), 2);
        QCOMPARE(d.annotations().at(1).tool, Tool::Arrow);
        QVERIFY(!d.canRedo());
    }

    void addClearsRedo()
    {
        Document d;
        d.add(make(Tool::Rect));
        QVERIFY(d.undo());
        d.add(make(Tool::Line));
        QVERIFY(!d.canRedo());
        QCOMPARE(d.annotations().size(), 1);
        QCOMPARE(d.annotations().at(0).tool, Tool::Line);
    }

    void counterNumbering()
    {
        Document d;
        d.add(make(Tool::Counter));
        d.add(make(Tool::Rect));
        d.add(make(Tool::Counter));
        QCOMPARE(d.nextCounterNumber(), 3);
        QVERIFY(d.undo());
        QCOMPARE(d.nextCounterNumber(), 2);
        d.add(make(Tool::Counter));
        d.add(make(Tool::Counter));
        QCOMPARE(counterNumbers(d.annotations()), (QVector<int>{1, 0, 2, 3}));
    }
};

QTEST_MAIN(TestDocument)
#include "test_document.moc"
