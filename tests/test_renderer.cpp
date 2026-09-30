#include <QtTest>

#include "renderer.h"

namespace {
const QRgb kWhite = qRgb(255, 255, 255);
const QRgb kRed = qRgb(255, 0, 0);

QImage whiteImage(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(kWhite);
    return image;
}

Annotation make(Tool tool, QVector<QPoint> points, int thickness = 4, QColor color = QColor(255, 0, 0))
{
    Annotation a;
    a.tool = tool;
    a.style = Style{color, thickness};
    a.points = std::move(points);
    return a;
}

bool isReddish(QRgb p)
{
    return qRed(p) > 200 && qGreen(p) < 80 && qBlue(p) < 80;
}
}

class TestRenderer : public QObject {
    Q_OBJECT
private slots:
    void cropsToSelection()
    {
        QImage base = whiteImage(100, 100);
        base.setPixel(30, 40, qRgb(1, 2, 3));
        const QImage out = render(base, QRect(20, 30, 50, 50), {});
        QCOMPARE(out.size(), QSize(50, 50));
        QCOMPARE(out.format(), QImage::Format_RGB32);
        QCOMPARE(out.pixel(10, 10), qRgb(1, 2, 3));
    }

    void emptySelectionGivesNullImage()
    {
        QVERIFY(render(whiteImage(10, 10), QRect(), {}).isNull());
    }

    void rectOutlineWithOffset()
    {
        const QImage out = render(whiteImage(200, 200), QRect(50, 50, 100, 100),
                                  {make(Tool::Rect, {{60, 60}, {120, 110}})});
        QCOMPARE(out.pixel(10, 35), kRed);   // левая сторона x=60 → 10 в результате
        QCOMPARE(out.pixel(40, 35), kWhite); // середина не закрашена
    }

    void ellipseOutline()
    {
        const QImage out = render(whiteImage(100, 100), QRect(0, 0, 100, 100),
                                  {make(Tool::Ellipse, {{10, 10}, {90, 90}})});
        QVERIFY(isReddish(out.pixel(10, 50)));
        QCOMPARE(out.pixel(50, 50), kWhite);
    }

    void arrowHasHead()
    {
        const QImage out = render(whiteImage(100, 100), QRect(0, 0, 100, 100),
                                  {make(Tool::Arrow, {{10, 50}, {90, 50}})});
        QVERIFY(isReddish(out.pixel(40, 50))); // древко
        QCOMPARE(out.pixel(40, 56), kWhite);    // древко толщиной 4
        QVERIFY(isReddish(out.pixel(85, 50))); // наконечник
        QVERIFY(isReddish(out.pixel(80, 53))); // наконечник шире древка
    }

    void markerIsTranslucent()
    {
        const QImage out = render(whiteImage(100, 100), QRect(0, 0, 100, 100),
                                  {make(Tool::Marker, {{10, 50}, {90, 50}})});
        const QRgb p = out.pixel(50, 50);
        QVERIFY(qRed(p) >= 250);
        QVERIFY2(qGreen(p) >= 140 && qGreen(p) <= 170, qPrintable(QString::number(qGreen(p))));
    }

    void markerSelfOverlapDoesNotDarken()
    {
        const QImage out = render(whiteImage(100, 100), QRect(0, 0, 100, 100),
                                  {make(Tool::Marker, {{10, 50}, {90, 50}, {50, 10}, {50, 90}})});
        const int crossing = qGreen(out.pixel(50, 50));
        const int single = qGreen(out.pixel(70, 50));
        QVERIFY2(std::abs(crossing - single) <= 3, qPrintable(QStringLiteral("%1 vs %2").arg(crossing).arg(single)));
    }

    void pixelateMakesUniformBlocks()
    {
        QImage base(100, 60, QImage::Format_RGB32);
        for (int y = 0; y < base.height(); ++y)
            for (int x = 0; x < base.width(); ++x)
                base.setPixel(x, y, qRgb((x * 37) % 256, (y * 53) % 256, ((x + y) * 11) % 256));
        // thickness 3 → блок max(12, 12) = 12; область 48×24 → ровно 4×2 блока.
        const QImage out = render(base, base.rect(), {make(Tool::Pixelate, {{0, 0}, {47, 23}}, 3)});
        bool changed = false;
        for (int y = 0; y < 24; ++y) {
            for (int x = 0; x < 48; ++x) {
                QCOMPARE(out.pixel(x, y), out.pixel(x / 12 * 12, y / 12 * 12));
                changed = changed || out.pixel(x, y) != base.pixel(x, y);
            }
        }
        QVERIFY(changed);
        QCOMPARE(out.pixel(60, 30), base.pixel(60, 30)); // вне области без изменений
    }

    void pixelateCoversEarlierDrawing()
    {
        const QImage out = render(whiteImage(100, 60), QRect(0, 0, 100, 60),
                                  {make(Tool::Line, {{0, 5}, {47, 5}}, 2, QColor(0, 0, 0)),
                                   make(Tool::Pixelate, {{0, 0}, {47, 23}}, 3)});
        const QRgb p = out.pixel(5, 5);
        QVERIFY(qRed(p) > 0 && qRed(p) < 255); // чёрная линия усреднена с белым фоном
    }

    void textIsDrawn()
    {
        Annotation a = make(Tool::Text, {{10, 10}});
        a.text = QStringLiteral("ЖЖ\nЖ");
        const QImage out = render(whiteImage(120, 120), QRect(0, 0, 120, 120), {a});
        int red = 0;
        for (int y = 0; y < out.height(); ++y)
            for (int x = 0; x < out.width(); ++x)
                red += isReddish(out.pixel(x, y)) ? 1 : 0;
        QVERIFY2(red > 20, qPrintable(QString::number(red)));
    }

    void counterIsFilledCircle()
    {
        // thickness 4 → диаметр 32, центр (50,50).
        const QImage out = render(whiteImage(100, 100), QRect(0, 0, 100, 100), {make(Tool::Counter, {{50, 50}})});
        QVERIFY(isReddish(out.pixel(38, 50)));
        QCOMPARE(out.pixel(50, 75), kWhite);
    }

    void sizesFromThickness()
    {
        QCOMPARE(markerWidth(4), 16);
        QCOMPARE(textPixelSize(4), 22);
        QCOMPARE(counterDiameter(4), 32);
        QCOMPARE(pixelateBlock(1), 12);
        QCOMPARE(pixelateBlock(5), 20);
        QCOMPARE(arrowHeadLength(1), 10);
        QCOMPARE(arrowHeadLength(4), 16);
        QCOMPARE(textFont(4).pixelSize(), 22);
    }
};

QTEST_MAIN(TestRenderer)
#include "test_renderer.moc"
