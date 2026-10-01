#include <QtTest>

#include "rawimage.h"

namespace {
// 2×2 пикселя, stride с запасом 12 байт (> 8), little-endian 32-битные слова.
QByteArray buffer(std::initializer_list<quint32> words, int stride = 12, int width = 2)
{
    QByteArray b(stride * 2, '\0');
    int i = 0;
    for (quint32 w : words) {
        const int row = i / width, col = i % width;
        memcpy(b.data() + row * stride + col * 4, &w, 4);
        ++i;
    }
    return b;
}
}

class TestRawImage : public QObject {
    Q_OBJECT
private slots:
    void xrgbForcesAlpha()
    {
        const QByteArray b = buffer({0x00112233, 0x00445566, 0x00778899, 0x00aabbcc});
        const auto img = imageFromShm(reinterpret_cast<const uchar*>(b.constData()), 2, 2, 12, ShmFormat::XRGB8888, false);
        QVERIFY(img.has_value());
        QCOMPARE(img->format(), QImage::Format_RGB32);
        QCOMPARE(img->pixel(0, 0), qRgb(0x11, 0x22, 0x33));
        QCOMPARE(img->pixel(1, 1), qRgb(0xaa, 0xbb, 0xcc));
    }

    void xbgrSwapsChannels()
    {
        // XBGR8888: младший байт — R.
        const QByteArray b = buffer({0x00332211, 0, 0, 0});
        const auto img = imageFromShm(reinterpret_cast<const uchar*>(b.constData()), 2, 2, 12, ShmFormat::XBGR8888, false);
        QVERIFY(img.has_value());
        QCOMPARE(img->pixel(0, 0), qRgb(0x11, 0x22, 0x33));
    }

    void yInvertFlips()
    {
        const QByteArray b = buffer({0x00ff0000, 0x00ff0000, 0x000000ff, 0x000000ff});
        const auto img = imageFromShm(reinterpret_cast<const uchar*>(b.constData()), 2, 2, 12, ShmFormat::ARGB8888, true);
        QVERIFY(img.has_value());
        QCOMPARE(img->pixel(0, 0), qRgb(0, 0, 0xff));
        QCOMPARE(img->pixel(0, 1), qRgb(0xff, 0, 0));
    }

    void rejectsBadShm()
    {
        const QByteArray b = buffer({0, 0, 0, 0});
        const auto* p = reinterpret_cast<const uchar*>(b.constData());
        QVERIFY(!imageFromShm(p, 2, 2, 12, 0x12345678, false).has_value()); // неизвестный формат
        QVERIFY(!imageFromShm(p, 2, 2, 4, ShmFormat::XRGB8888, false).has_value()); // stride < width*4
        QVERIFY(!imageFromShm(p, 0, 2, 12, ShmFormat::XRGB8888, false).has_value());
        QVERIFY(!imageFromShm(nullptr, 2, 2, 12, ShmFormat::XRGB8888, false).has_value());
    }

    void kwinFormats()
    {
        const QByteArray b = buffer({0xff112233, 0xff445566, 0xff778899, 0xffaabbcc});
        const auto img = imageFromKWin(b, 2, 2, 12, QImage::Format_ARGB32);
        QVERIFY(img.has_value());
        QCOMPARE(img->format(), QImage::Format_RGB32);
        QCOMPARE(img->pixel(1, 0), qRgb(0x44, 0x55, 0x66));
        QVERIFY(imageFromKWin(b, 2, 2, 12, QImage::Format_RGB32).has_value());
        QVERIFY(imageFromKWin(b, 2, 2, 12, QImage::Format_RGBX8888).has_value());
    }

    void rejectsBadKWin()
    {
        const QByteArray b = buffer({0, 0, 0, 0});
        QVERIFY(!imageFromKWin(b, 2, 2, 12, QImage::Format_Indexed8).has_value()); // запрещённый формат
        QVERIFY(!imageFromKWin(b.left(20), 2, 2, 12, QImage::Format_RGB32).has_value()); // данных меньше stride*height
        QVERIFY(!imageFromKWin(b, 2, 2, 4, QImage::Format_RGB32).has_value());
        QVERIFY(!imageFromKWin(b, -1, 2, 12, QImage::Format_RGB32).has_value());
    }

    void matchByName()
    {
        const QVector<NamedScreen> screens = {{QStringLiteral("DP-2"), QRect(2560, 0, 1920, 1080)},
                                              {QStringLiteral("DP-0"), QRect(0, 720, 2560, 1440)}};
        QCOMPARE(matchScreens({QStringLiteral("DP-0"), QStringLiteral("DP-2")}, screens),
                 (QVector<QRect>{QRect(0, 720, 2560, 1440), QRect(2560, 0, 1920, 1080)}));
    }

    void matchFallsBackToOrder()
    {
        const QVector<NamedScreen> screens = {{QStringLiteral("A"), QRect(0, 0, 10, 10)}, {QStringLiteral("B"), QRect(10, 0, 10, 10)}};
        QCOMPARE(matchScreens({QString(), QStringLiteral("X")}, screens), (QVector<QRect>{QRect(0, 0, 10, 10), QRect(10, 0, 10, 10)}));
        QVERIFY(matchScreens({QStringLiteral("X")}, screens).isEmpty());
    }
};

QTEST_MAIN(TestRawImage)
#include "test_rawimage.moc"
