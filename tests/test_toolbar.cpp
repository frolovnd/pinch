#include <QtTest>

#include "toolbar.h"

#include <QLabel>
#include <QToolButton>

namespace {
QToolButton* button(Toolbar& tb, const char* name)
{
    auto* b = tb.findChild<QToolButton*>(QString::fromLatin1(name));
    if (!b)
        qFatal("нет кнопки %s", name);
    return b;
}
}

class TestToolbar : public QObject {
    Q_OBJECT
private slots:
    void clickToolEmitsAndChecks()
    {
        Toolbar tb;
        Tool got = Tool::None;
        connect(&tb, &Toolbar::toolChosen, this, [&](Tool t) { got = t; });
        button(tb, "tool-arrow")->click();
        QCOMPARE(got, Tool::Arrow);
        QVERIFY(button(tb, "tool-arrow")->isChecked());
        QVERIFY(!button(tb, "tool-pen")->isChecked());
    }

    void clickActiveToolDeselects()
    {
        Toolbar tb;
        tb.setTool(Tool::Arrow);
        Tool got = Tool::Arrow;
        connect(&tb, &Toolbar::toolChosen, this, [&](Tool t) { got = t; });
        button(tb, "tool-arrow")->click();
        QCOMPARE(got, Tool::None);
        QVERIFY(!button(tb, "tool-arrow")->isChecked());
    }

    void setToolChecksOnlyOne()
    {
        Toolbar tb;
        tb.setTool(Tool::Pen);
        QVERIFY(button(tb, "tool-pen")->isChecked());
        tb.setTool(Tool::Pixelate);
        QVERIFY(!button(tb, "tool-pen")->isChecked());
        QVERIFY(button(tb, "tool-pixelate")->isChecked());
    }

    void colorClick()
    {
        Toolbar tb;
        QColor got;
        connect(&tb, &Toolbar::colorChosen, this, [&](const QColor& c) { got = c; });
        button(tb, "color-4")->click();
        QCOMPARE(got, QColor(0x1E, 0x88, 0xE5));
        QVERIFY(button(tb, "color-4")->isChecked());
        QVERIFY(!button(tb, "color-0")->isChecked());
    }

    void paletteHasEightColors()
    {
        QCOMPARE(Toolbar::palette().size(), 8);
        QCOMPARE(Toolbar::palette().at(0), QColor(0xE5, 0x39, 0x35));
        QCOMPARE(Toolbar::palette().at(7), QColor(0xFF, 0xFF, 0xFF));
    }

    void thicknessLabel()
    {
        Toolbar tb;
        tb.setThickness(7);
        QCOMPARE(tb.findChild<QLabel*>(QStringLiteral("thickness"))->text(), QStringLiteral("7px"));
    }

    void undoRedoEnabled()
    {
        Toolbar tb;
        tb.setUndoRedoEnabled(false, true);
        QVERIFY(!button(tb, "undo")->isEnabled());
        QVERIFY(button(tb, "redo")->isEnabled());
    }

    void actionButtonsEmit()
    {
        Toolbar tb;
        QStringList got;
        connect(&tb, &Toolbar::undoRequested, this, [&] { got << QStringLiteral("undo"); });
        connect(&tb, &Toolbar::redoRequested, this, [&] { got << QStringLiteral("redo"); });
        connect(&tb, &Toolbar::copyRequested, this, [&] { got << QStringLiteral("copy"); });
        connect(&tb, &Toolbar::quickSaveRequested, this, [&] { got << QStringLiteral("quicksave"); });
        connect(&tb, &Toolbar::saveAsRequested, this, [&] { got << QStringLiteral("saveas"); });
        connect(&tb, &Toolbar::closeRequested, this, [&] { got << QStringLiteral("close"); });
        tb.setUndoRedoEnabled(true, true);
        for (const char* name : {"undo", "redo", "copy", "quicksave", "saveas", "close"})
            button(tb, name)->click();
        QCOMPARE(got, (QStringList{QStringLiteral("undo"), QStringLiteral("redo"), QStringLiteral("copy"),
                                   QStringLiteral("quicksave"), QStringLiteral("saveas"), QStringLiteral("close")}));
    }

    void buttonsNeverTakeFocus()
    {
        Toolbar tb;
        QCOMPARE(tb.focusPolicy(), Qt::NoFocus);
        const auto buttons = tb.findChildren<QToolButton*>();
        QCOMPARE(buttons.size(), 9 + 8 + 2 + 4);
        for (QToolButton* b : buttons)
            QCOMPARE(b->focusPolicy(), Qt::NoFocus);
    }
};

QTEST_MAIN(TestToolbar)
#include "test_toolbar.moc"
