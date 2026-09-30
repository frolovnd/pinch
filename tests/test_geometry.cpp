#include <QtTest>

#include "geometry.h"

namespace {
// Раскладка мониторов пользователя в координатах изображения.
const QVector<QRect> kScreens = {
    QRect(0, 720, 2560, 1440),
    QRect(2560, 0, 1920, 1080),
    QRect(2560, 1080, 1920, 1080),
};
const QSize kToolbar(700, 32);
const QRect kBounds(0, 0, 1000, 800);
const QRect kSel(100, 100, 200, 100); // right = 299, bottom = 199
}

class TestGeometry : public QObject {
    Q_OBJECT
private slots:
    void rectFromPointsNormalizes()
    {
        QCOMPARE(rectFromPoints({10, 20}, {5, 8}), QRect(5, 8, 6, 13));
        QCOMPARE(rectFromPoints({5, 8}, {10, 20}), QRect(5, 8, 6, 13));
        QCOMPARE(rectFromPoints({3, 3}, {3, 3}), QRect(3, 3, 1, 1));
    }

    void hitTestCornersEdgesInside()
    {
        QCOMPARE(hitTestHandle(kSel, {100, 100}, 6), Handle::TopLeft);
        QCOMPARE(hitTestHandle(kSel, {299, 100}, 6), Handle::TopRight);
        QCOMPARE(hitTestHandle(kSel, {299, 199}, 6), Handle::BottomRight);
        QCOMPARE(hitTestHandle(kSel, {100, 199}, 6), Handle::BottomLeft);
        QCOMPARE(hitTestHandle(kSel, {200, 100}, 6), Handle::Top);
        QCOMPARE(hitTestHandle(kSel, {200, 199}, 6), Handle::Bottom);
        QCOMPARE(hitTestHandle(kSel, {100, 150}, 6), Handle::Left);
        QCOMPARE(hitTestHandle(kSel, {299, 150}, 6), Handle::Right);
        QCOMPARE(hitTestHandle(kSel, {200, 150}, 6), Handle::Move);
    }

    void hitTestTolerance()
    {
        QCOMPARE(hitTestHandle(kSel, {95, 150}, 6), Handle::Left);
        QCOMPARE(hitTestHandle(kSel, {93, 150}, 6), Handle::None);
        QCOMPARE(hitTestHandle(kSel, {304, 203}, 6), Handle::BottomRight);
        QCOMPARE(hitTestHandle(kSel, {50, 50}, 6), Handle::None);
        QCOMPARE(hitTestHandle(QRect(), {0, 0}, 6), Handle::None);
    }

    void applyHandleDragMove()
    {
        QCOMPARE(applyHandleDrag(kSel, Handle::Move, {50, 30}, kBounds), QRect(150, 130, 200, 100));
    }

    void applyHandleDragMoveClamps()
    {
        QCOMPARE(applyHandleDrag(kSel, Handle::Move, {900, 0}, kBounds), QRect(800, 100, 200, 100));
        QCOMPARE(applyHandleDrag(kSel, Handle::Move, {-500, -500}, kBounds), QRect(0, 0, 200, 100));
    }

    void applyHandleDragResize()
    {
        QCOMPARE(applyHandleDrag(kSel, Handle::BottomRight, {10, 20}, kBounds), QRect(100, 100, 210, 120));
        QCOMPARE(applyHandleDrag(kSel, Handle::TopLeft, {-10, -20}, kBounds), QRect(90, 80, 210, 120));
        QCOMPARE(applyHandleDrag(kSel, Handle::Top, {0, 30}, kBounds), QRect(100, 130, 200, 70));
        QCOMPARE(applyHandleDrag(kSel, Handle::None, {10, 10}, kBounds), kSel);
    }

    void applyHandleDragCrossesOver()
    {
        // Левая сторона перетянута правее правой — прямоугольник нормализуется.
        QCOMPARE(applyHandleDrag(kSel, Handle::Left, {250, 0}, kBounds), QRect(299, 100, 52, 100));
    }

    void applyHandleDragResizeClamps()
    {
        QCOMPARE(applyHandleDrag(kSel, Handle::Right, {2000, 0}, kBounds), QRect(100, 100, 900, 100));
    }

    void snapLine45Quadrants()
    {
        QCOMPARE(snapLine45({0, 0}, {10, 1}), QPoint(10, 0));
        QCOMPARE(snapLine45({0, 0}, {1, 10}), QPoint(0, 10));
        QCOMPARE(snapLine45({0, 0}, {10, 8}), QPoint(9, 9));
        QCOMPARE(snapLine45({0, 0}, {-10, 8}), QPoint(-9, 9));
        QCOMPARE(snapLine45({0, 0}, {-10, -1}), QPoint(-10, 0));
        QCOMPARE(snapLine45({0, 0}, {0, 0}), QPoint(0, 0));
        QCOMPARE(snapLine45({100, 100}, {110, 108}), QPoint(109, 109));
    }

    void snapSquareQuadrants()
    {
        QCOMPARE(snapSquare({0, 0}, {10, 3}), QPoint(10, 10));
        QCOMPARE(snapSquare({0, 0}, {-3, 10}), QPoint(-10, 10));
        QCOMPARE(snapSquare({0, 0}, {-5, -7}), QPoint(-7, -7));
        QCOMPARE(snapSquare({5, 5}, {5, 5}), QPoint(5, 5));
    }

    void placeToolbarBelow()
    {
        QCOMPARE(placeToolbar(QRect(100, 800, 400, 400), kToolbar, kScreens), QPoint(100, 1207));
    }

    void placeToolbarAbove()
    {
        QCOMPARE(placeToolbar(QRect(100, 1800, 400, 350), kToolbar, kScreens), QPoint(100, 1760));
    }

    void placeToolbarInside()
    {
        QCOMPARE(placeToolbar(QRect(0, 720, 2560, 1440), kToolbar, kScreens), QPoint(0, 2119));
    }

    void placeToolbarClampsX()
    {
        QCOMPARE(placeToolbar(QRect(4000, 100, 300, 300), kToolbar, kScreens), QPoint(3780, 407));
    }

    void placeToolbarDeadZone()
    {
        // Центр выделения в мёртвой зоне — берётся монитор с наибольшим пересечением (DP-2).
        QCOMPARE(placeToolbar(QRect(2000, 100, 1000, 400), kToolbar, kScreens), QPoint(2560, 507));
    }

    void placeToolbarNoScreens()
    {
        QCOMPARE(placeToolbar(QRect(10, 10, 100, 100), kToolbar, {}), QPoint(10, 117));
    }
};

QTEST_MAIN(TestGeometry)
#include "test_geometry.moc"
