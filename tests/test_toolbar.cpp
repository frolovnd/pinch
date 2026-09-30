#include <QtTest>

#include "toolbar.h"

#include <QLabel>
#include <QLayout>
#include <QSlider>
#include <QWheelEvent>
#include <QToolButton>

namespace {
QToolButton* button(Toolbar& tb, const char* name)
{
    auto* b = tb.findChild<QToolButton*>(QString::fromLatin1(name));
    if (!b)
        qFatal("нет кнопки %s", name);
    return b;
}

QSlider* slider(Toolbar& tb)
{
    auto* s = tb.findChild<QSlider*>(QStringLiteral("thickness-slider"));
    if (!s)
        qFatal("нет ползунка");
    return s;
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
        QCOMPARE(buttons.size(), 9 + 8 + 5 + 2 + 4);
        for (QToolButton* b : buttons)
            QCOMPARE(b->focusPolicy(), Qt::NoFocus);
        QCOMPARE(slider(tb)->focusPolicy(), Qt::NoFocus);
    }

    void thicknessPresetClickEmits()
    {
        Toolbar tb;
        QVector<int> got;
        connect(&tb, &Toolbar::thicknessChosen, this, [&](int t) { got << t; });
        button(tb, "thickness-2")->click();
        QCOMPARE(got, QVector<int>{8});
        for (int i = 0; i < 5; ++i)
            QCOMPARE(button(tb, QByteArray("thickness-" + QByteArray::number(i)).constData())->isChecked(), i == 2);
        QCOMPARE(slider(tb)->value(), 8);
        QCOMPARE(tb.findChild<QLabel*>(QStringLiteral("thickness"))->text(), QStringLiteral("8px"));
    }

    void setThicknessSyncsWithoutEmitting()
    {
        Toolbar tb;
        int emitted = 0;
        connect(&tb, &Toolbar::thicknessChosen, this, [&](int) { ++emitted; });
        tb.setThickness(14);
        QVERIFY(button(tb, "thickness-3")->isChecked());
        QCOMPARE(slider(tb)->value(), 14);
        QCOMPARE(tb.findChild<QLabel*>(QStringLiteral("thickness"))->text(), QStringLiteral("14px"));
        tb.setThickness(5);
        for (int i = 0; i < 5; ++i)
            QVERIFY(!button(tb, QByteArray("thickness-" + QByteArray::number(i)).constData())->isChecked());
        QCOMPARE(slider(tb)->value(), 5);
        QCOMPARE(emitted, 0);
    }

    void presetsValues()
    {
        QCOMPARE(Toolbar::thicknessPresets(), (QVector<int>{2, 4, 8, 14, 24}));
    }

    void sliderEmits()
    {
        Toolbar tb;
        int got = 0;
        connect(&tb, &Toolbar::thicknessChosen, this, [&](int t) { got = t; });
        slider(tb)->setValue(20);
        QCOMPARE(got, 20);
        QCOMPARE(tb.findChild<QLabel*>(QStringLiteral("thickness"))->text(), QStringLiteral("20px"));
    }

    void threeRows()
    {
        Toolbar tb;
        tb.adjustSize();
        tb.layout()->activate();
        const auto y = [&](const char* name) { return button(tb, name)->mapTo(&tb, QPoint()).y(); };
        const int toolsY = y("tool-pen");
        for (const char* n : {"tool-marker", "tool-line", "tool-arrow", "tool-rect", "tool-ellipse", "tool-text",
                              "tool-counter", "tool-pixelate"})
            QCOMPARE(y(n), toolsY);
        const int colorsY = y("color-0");
        QVERIFY(colorsY > toolsY);
        for (int i = 1; i < 8; ++i)
            QCOMPARE(y(QByteArray("color-" + QByteArray::number(i)).constData()), colorsY);
        const int actionsY = y("undo");
        QVERIFY(actionsY > colorsY);
        for (const char* n : {"redo", "copy", "quicksave", "saveas", "close"})
            QCOMPARE(y(n), actionsY);
    }

    void wheelOverSliderGoesToParent()
    {
        struct Recorder : QWidget {
            int wheels = 0;
            void wheelEvent(QWheelEvent* e) override
            {
                ++wheels;
                e->accept();
            }
        } parent;
        auto* tb = new Toolbar(&parent);
        tb->move(10, 10);
        slider(*tb)->setValue(10);
        const int before = slider(*tb)->value();
        QWheelEvent ev(QPointF(3, 3), slider(*tb)->mapToGlobal(QPointF(3, 3)), QPoint(), QPoint(0, 120),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(slider(*tb), &ev);
        QCOMPARE(parent.wheels, 1);
        QCOMPARE(slider(*tb)->value(), before);
    }
};

QTEST_MAIN(TestToolbar)
#include "test_toolbar.moc"
