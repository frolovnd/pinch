#include <QtTest>

#include "capture.h"

namespace {
QImage solid(QSize size, QRgb color)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image;
}
const QRgb kRed = qRgb(255, 0, 0);
const QRgb kGreen = qRgb(0, 255, 0);
const QRgb kBlue = qRgb(0, 0, 255);
const QRgb kBlack = qRgb(0, 0, 0);
}

class TestCapture : public QObject {
    Q_OBJECT
private slots:
    void composesUserLayout()
    {
        const Capture c = composeScreens({
            {solid({2560, 1440}, kRed), QRect(0, 720, 2560, 1440)},
            {solid({1920, 1080}, kGreen), QRect(2560, 0, 1920, 1080)},
            {solid({1920, 1080}, kBlue), QRect(2560, 1080, 1920, 1080)},
        });
        QCOMPARE(c.image.size(), QSize(4480, 2160));
        QCOMPARE(c.image.format(), QImage::Format_RGB32);
        QCOMPARE(c.origin, QPoint(0, 0));
        QCOMPARE(c.image.pixel(10, 800), kRed);
        QCOMPARE(c.image.pixel(3000, 10), kGreen);
        QCOMPARE(c.image.pixel(3000, 2000), kBlue);
        QCOMPARE(c.image.pixel(10, 10), kBlack); // мёртвая зона
        QCOMPARE(c.screens, (QVector<QRect>{QRect(0, 720, 2560, 1440), QRect(2560, 0, 1920, 1080),
                                            QRect(2560, 1080, 1920, 1080)}));
    }

    void negativeOrigin()
    {
        const Capture c = composeScreens({
            {solid({1920, 1080}, kGreen), QRect(-1920, 0, 1920, 1080)},
            {solid({2560, 1440}, kRed), QRect(0, 0, 2560, 1440)},
        });
        QCOMPARE(c.image.size(), QSize(4480, 1440));
        QCOMPARE(c.origin, QPoint(-1920, 0));
        QCOMPARE(c.image.pixel(10, 10), kGreen);
        QCOMPARE(c.image.pixel(1930, 10), kRed);
        QCOMPARE(c.image.pixel(10, 1300), kBlack);
        QCOMPARE(c.screens, (QVector<QRect>{QRect(0, 0, 1920, 1080), QRect(1920, 0, 2560, 1440)}));
    }

    void emptyInput()
    {
        const Capture c = composeScreens({});
        QVERIFY(c.image.isNull());
        QVERIFY(c.screens.isEmpty());
    }

    void workspaceImageScalesAndShiftsScreens()
    {
        // Изображение 8x4 (физические пиксели), мониторы объединяются в 4x2 логических со смещением (-4, 10).
        const Capture c = captureFromWorkspaceImage(solid({8, 4}, kRed), {QRect(-4, 10, 2, 2), QRect(-2, 10, 2, 2)});
        QCOMPARE(c.image.size(), QSize(4, 2));
        QCOMPARE(c.origin, QPoint(-4, 10));
        QCOMPARE(c.screens, (QVector<QRect>{QRect(0, 0, 2, 2), QRect(2, 0, 2, 2)}));
        QCOMPARE(c.image.pixel(3, 1), kRed);
    }

    void workspaceImageOfMatchingSizeIsKept()
    {
        const Capture c = captureFromWorkspaceImage(solid({4, 2}, kGreen), {QRect(0, 0, 4, 2)});
        QCOMPARE(c.image.size(), QSize(4, 2));
        QCOMPARE(c.origin, QPoint(0, 0));
        QCOMPARE(c.image.pixel(0, 0), kGreen);
    }
};

QTEST_MAIN(TestCapture)
#include "test_capture.moc"
