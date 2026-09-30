#include <QtTest>

#include "overlay.h"
#include "toolbar.h"

#include <QToolButton>

namespace {
// Два «монитора»: 400×300 и 400×200 справа; зона (400..799, 200..299) — мёртвая.
Capture makeCapture()
{
    Capture c;
    c.image = QImage(800, 300, QImage::Format_RGB32);
    c.image.fill(QColor(100, 100, 100));
    c.origin = QPoint(0, 0);
    c.screens = {QRect(0, 0, 400, 300), QRect(400, 0, 400, 200)};
    return c;
}

struct Env {
    QTemporaryDir dir;
    Overlay ov{makeCapture(), Settings{}, dir.path()};
    bool finished = false;
    QImage copied;
    bool wasCopied = false;

    Env()
    {
        QObject::connect(&ov, &Overlay::finished, [this] { finished = true; });
        QObject::connect(&ov, &Overlay::copyRequested, [this](const QImage& image) {
            copied = image;
            wasCopied = true;
        });
        ov.show();
    }
};

void mouse(QWidget* w, QEvent::Type type, QPoint pos, Qt::MouseButton button, Qt::MouseButtons buttons,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent e(type, QPointF(pos), w->mapToGlobal(QPointF(pos)), button, buttons, mods);
    QApplication::sendEvent(w, &e);
}

void drag(QWidget* w, QPoint from, QPoint to, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouse(w, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, mods);
    mouse(w, QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton, mods);
    mouse(w, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
    mouse(w, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
}

void click(QWidget* w, QPoint pos)
{
    mouse(w, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
}

// Нажатие клавиши так, как его присылает X11 в кириллической раскладке.
void rawKey(QWidget* w, int key, Qt::KeyboardModifiers mods, quint32 scanCode, const QString& text)
{
    QKeyEvent e(QEvent::KeyPress, key, mods, scanCode, 0, 0, text);
    QApplication::sendEvent(w, &e);
}
}

class TestOverlay : public QObject {
    Q_OBJECT
private slots:
    void dragSelects()
    {
        Env e;
        QVERIFY(!e.ov.toolbar()->isVisible());
        drag(&e.ov, {10, 10}, {110, 60});
        QCOMPARE(e.ov.selection(), QRect(10, 10, 101, 51));
        QVERIFY(e.ov.toolbar()->isVisible());
    }

    void clickSelectsScreen()
    {
        Env e;
        click(&e.ov, {500, 100});
        QCOMPARE(e.ov.selection(), QRect(400, 0, 400, 200));
    }

    void clickInDeadZoneSelectsNothing()
    {
        Env e;
        click(&e.ov, {500, 250});
        QVERIFY(e.ov.selection().isEmpty());
        QVERIFY(!e.ov.toolbar()->isVisible());
    }

    void dragBeyondEdgeClamps()
    {
        Env e;
        drag(&e.ov, {700, 250}, {900, 400});
        QCOMPARE(e.ov.selection(), QRect(700, 250, 100, 50));
    }

    void ctrlASelectsAll()
    {
        Env e;
        QTest::keyClick(&e.ov, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(e.ov.selection(), QRect(0, 0, 800, 300));
    }

    void moveSelection()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        drag(&e.ov, {50, 30}, {70, 40});
        QCOMPARE(e.ov.selection(), QRect(30, 20, 101, 51));
    }

    void resizeByCorner()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        drag(&e.ov, {110, 60}, {150, 80});
        QCOMPARE(e.ov.selection(), QRect(10, 10, 141, 71));
    }

    void dragOutsideStartsNewSelection()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        drag(&e.ov, {200, 100}, {300, 200});
        QCOMPARE(e.ov.selection(), QRect(200, 100, 101, 101));
    }

    void toolKeys()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        const QList<QPair<Qt::Key, Tool>> keys = {
            {Qt::Key_P, Tool::Pen}, {Qt::Key_M, Tool::Marker}, {Qt::Key_L, Tool::Line},
            {Qt::Key_A, Tool::Arrow}, {Qt::Key_R, Tool::Rect}, {Qt::Key_E, Tool::Ellipse},
            {Qt::Key_T, Tool::Text}, {Qt::Key_N, Tool::Counter}, {Qt::Key_B, Tool::Pixelate},
            {Qt::Key_V, Tool::None},
        };
        for (const auto& [key, tool] : keys) {
            QTest::keyClick(&e.ov, key);
            QCOMPARE(e.ov.tool(), tool);
        }
    }

    void toolKeysIgnoredWithoutSelection()
    {
        Env e;
        QTest::keyClick(&e.ov, Qt::Key_P);
        QCOMPARE(e.ov.tool(), Tool::None);
    }

    void toolbarButtonChangesTool()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        e.ov.toolbar()->findChild<QToolButton*>(QStringLiteral("tool-rect"))->click();
        QCOMPARE(e.ov.tool(), Tool::Rect);
    }

    void cyrillicLayoutHotkeys()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        rawKey(&e.ov, 0x0417, Qt::NoModifier, 33, QStringLiteral("з"));
        QCOMPARE(e.ov.tool(), Tool::Pen);
        rawKey(&e.ov, 0x0421, Qt::ControlModifier, 54, QString());
        QVERIFY(e.wasCopied);
    }

    void ctrlCCopiesSelection()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        QTest::keyClick(&e.ov, Qt::Key_C, Qt::ControlModifier);
        QVERIFY(e.wasCopied);
        QCOMPARE(e.copied.size(), QSize(101, 51));
        QCOMPARE(e.copied.pixel(5, 5), qRgb(100, 100, 100));
        QVERIFY(!e.ov.isVisible());
        QVERIFY(!e.finished);
    }

    void enterCopies()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        QTest::keyClick(&e.ov, Qt::Key_Return);
        QVERIFY(e.wasCopied);
    }

    void outputIgnoredWithoutSelection()
    {
        Env e;
        QTest::keyClick(&e.ov, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(&e.ov, Qt::Key_S, Qt::ControlModifier);
        QVERIFY(!e.wasCopied);
        QVERIFY(!e.finished);
        QVERIFY(e.ov.isVisible());
    }

    void quickSaveWritesFile()
    {
        Env e;
        drag(&e.ov, {10, 10}, {110, 60});
        QTest::keyClick(&e.ov, Qt::Key_S, Qt::ControlModifier);
        QVERIFY(e.finished);
        const QStringList files = QDir(e.dir.path()).entryList({QStringLiteral("*.png")}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QCOMPARE(QImage(e.dir.filePath(files.first())).size(), QSize(101, 51));
    }

    void escapeFinishes()
    {
        Env e;
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QVERIFY(e.finished);
        QVERIFY(!e.ov.isVisible());
    }

    void wheelChangesThickness()
    {
        Env e;
        const auto wheel = [&](int dy) {
            QWheelEvent ev(QPointF(50, 50), e.ov.mapToGlobal(QPointF(50, 50)), QPoint(), QPoint(0, dy),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&e.ov, &ev);
        };
        wheel(120);
        QCOMPARE(e.ov.style().thickness, 5);
        for (int i = 0; i < 50; ++i)
            wheel(120);
        QCOMPARE(e.ov.style().thickness, 40);
        for (int i = 0; i < 50; ++i)
            wheel(-120);
        QCOMPARE(e.ov.style().thickness, 1);
    }

    void drawRectAddsAnnotation()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_R);
        drag(&e.ov, {50, 50}, {150, 120});
        QCOMPARE(e.ov.document().annotations().size(), 1);
        const Annotation& a = e.ov.document().annotations().at(0);
        QCOMPARE(a.tool, Tool::Rect);
        QCOMPARE(a.points, (QVector<QPoint>{{50, 50}, {150, 120}}));
        QCOMPARE(a.style.color, Settings().color);
        QCOMPARE(e.ov.result().pixel(40, 75), Settings().color.rgb()); // левая сторона x=50
        QVERIFY(e.ov.toolbar()->findChild<QToolButton*>(QStringLiteral("undo"))->isEnabled());
    }

    void shiftMakesSquare()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_R);
        drag(&e.ov, {50, 50}, {150, 120}, Qt::ShiftModifier);
        QCOMPARE(e.ov.document().annotations().at(0).points.at(1), QPoint(150, 150));
    }

    void shiftSnapsArrow()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_A);
        drag(&e.ov, {50, 50}, {150, 58}, Qt::ShiftModifier);
        QCOMPARE(e.ov.document().annotations().at(0).points.at(1), QPoint(150, 50));
    }

    void tinyShapesIgnored()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_R);
        click(&e.ov, {50, 50});
        QTest::keyClick(&e.ov, Qt::Key_M);
        click(&e.ov, {60, 60});
        QTest::keyClick(&e.ov, Qt::Key_L);
        click(&e.ov, {70, 70});
        QVERIFY(e.ov.document().annotations().isEmpty());
    }

    void penStroke()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_P);
        drag(&e.ov, {50, 50}, {100, 80});
        QCOMPARE(e.ov.document().annotations().size(), 1);
        QCOMPARE(e.ov.document().annotations().at(0).tool, Tool::Pen);
        QCOMPARE(e.ov.document().annotations().at(0).points.size(), 3);
    }

    void drawingOutsideSelectionIgnored()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_R);
        drag(&e.ov, {350, 250}, {380, 280});
        QVERIFY(e.ov.document().annotations().isEmpty());
        QCOMPARE(e.ov.selection(), QRect(10, 10, 291, 191));
    }

    void undoRedoKeys()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_R);
        drag(&e.ov, {50, 50}, {150, 120});
        QTest::keyClick(&e.ov, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(e.ov.document().annotations().isEmpty());
        QTest::keyClick(&e.ov, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(e.ov.document().annotations().size(), 1);
        QTest::keyClick(&e.ov, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(&e.ov, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(e.ov.document().annotations().size(), 1);
    }

    void counterClicks()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_N);
        click(&e.ov, {60, 60});
        click(&e.ov, {80, 80});
        QCOMPARE(e.ov.document().annotations().size(), 2);
        QCOMPARE(e.ov.document().annotations().at(1).tool, Tool::Counter);
        QCOMPARE(e.ov.document().nextCounterNumber(), 3);
    }

    void textEscapeKeepsOverlay()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QVERIFY(e.ov.isEditingText());
        QTest::keyClicks(&e.ov, QStringLiteral("ab"));
        QTest::keyClick(&e.ov, Qt::Key_Backspace);
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QVERIFY(!e.ov.isEditingText());
        QVERIFY(!e.finished);
        QVERIFY(e.ov.isVisible());
        QCOMPARE(e.ov.document().annotations().size(), 1);
        QCOMPARE(e.ov.document().annotations().at(0).text, QStringLiteral("a"));
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QVERIFY(e.finished);
    }

    void textEnterIsNewline()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClicks(&e.ov, QStringLiteral("a"));
        QTest::keyClick(&e.ov, Qt::Key_Return);
        QTest::keyClicks(&e.ov, QStringLiteral("b"));
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QVERIFY(!e.wasCopied);
        QCOMPARE(e.ov.document().annotations().at(0).text, QStringLiteral("a\nb"));
    }

    void textLettersDontSwitchTool()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClicks(&e.ov, QStringLiteral("pmv"));
        QCOMPARE(e.ov.tool(), Tool::Text);
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QCOMPARE(e.ov.document().annotations().at(0).text, QStringLiteral("pmv"));
    }

    void textCyrillic()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        rawKey(&e.ov, 0x0416, Qt::NoModifier, 47, QStringLiteral("ж"));
        rawKey(&e.ov, 0x0417, Qt::NoModifier, 33, QStringLiteral("з")); // физическая P — не инструмент
        QCOMPARE(e.ov.tool(), Tool::Text);
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QCOMPARE(e.ov.document().annotations().at(0).text, QStringLiteral("жз"));
    }

    void emptyTextDiscarded()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QVERIFY(e.ov.document().annotations().isEmpty());
        QVERIFY(!e.finished);
    }

    void clickCommitsTextAndStartsNew()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClicks(&e.ov, QStringLiteral("a"));
        click(&e.ov, {60, 120});
        QVERIFY(e.ov.isEditingText());
        QTest::keyClicks(&e.ov, QStringLiteral("b"));
        QTest::keyClick(&e.ov, Qt::Key_Escape);
        QCOMPARE(e.ov.document().annotations().size(), 2);
        QCOMPARE(e.ov.document().annotations().at(1).points.at(0), QPoint(60, 120));
    }

    void ctrlCCommitsText()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClicks(&e.ov, QStringLiteral("hi"));
        QTest::keyClick(&e.ov, Qt::Key_C, Qt::ControlModifier);
        QVERIFY(e.wasCopied);
        QCOMPARE(e.ov.document().annotations().at(0).text, QStringLiteral("hi"));
    }

    void toolSwitchCommitsText()
    {
        Env e;
        drag(&e.ov, {10, 10}, {300, 200});
        QTest::keyClick(&e.ov, Qt::Key_T);
        click(&e.ov, {60, 60});
        QTest::keyClicks(&e.ov, QStringLiteral("x"));
        e.ov.toolbar()->findChild<QToolButton*>(QStringLiteral("tool-rect"))->click();
        QVERIFY(!e.ov.isEditingText());
        QCOMPARE(e.ov.tool(), Tool::Rect);
        QCOMPARE(e.ov.document().annotations().size(), 1);
    }
};

QTEST_MAIN(TestOverlay)
#include "test_overlay.moc"
