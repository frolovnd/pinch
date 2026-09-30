#include <QtTest>

#include "appicon.h"

class TestAppIcon : public QObject {
    Q_OBJECT
private slots:
    void iconIsNotNullAndHasSizes()
    {
        const QIcon icon = appIcon();
        QVERIFY(!icon.isNull());
        const QList<QSize> sizes = icon.availableSizes();
        for (const int n : {32, 64, 128, 256})
            QVERIFY2(sizes.contains(QSize(n, n)), qPrintable(QStringLiteral("нет размера %1").arg(n)));
    }

    void pixmapIsTransparentAtCorner()
    {
        const QImage image = appIcon().pixmap(64, 64).toImage();
        QVERIFY(image.hasAlphaChannel());
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
    }
};

QTEST_MAIN(TestAppIcon)
#include "test_appicon.moc"
