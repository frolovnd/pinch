#include <QtTest>
#include <cstring>

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

// Изображение по строкам из букв: каждая буква — свой цвет (a, b, c, ... различимы).
QImage letters(const QStringList& rows)
{
    QImage image(rows.first().size(), rows.size(), QImage::Format_RGB32);
    for (int y = 0; y < rows.size(); ++y)
        for (int x = 0; x < rows[y].size(); ++x)
            image.setPixel(x, y, qRgb(rows[y][x].unicode(), 0x10 * x, 0x20 * y));
    return image;
}

// Цвет буквы не зависит от её места: сравниваются буквы, а не координаты.
QStringList toLetters(const QImage& image)
{
    QStringList rows;
    for (int y = 0; y < image.height(); ++y) {
        QString row;
        for (int x = 0; x < image.width(); ++x)
            row += QChar(qRed(image.pixel(x, y)));
        rows << row;
    }
    return rows;
}
}


class TestRawImage : public QObject {
    Q_OBJECT
private slots:
    void xrgbForcesAlpha()
    {
        const QByteArray b = buffer({0x00112233, 0x00445566, 0x00778899, 0x00aabbcc});
        const auto img = imageFromShm(reinterpret_cast<const uchar*>(b.constData()), b.size(), 2, 2, 12, ShmFormat::XRGB8888, false);
        QVERIFY(img.has_value());
        QCOMPARE(img->format(), QImage::Format_RGB32);
        QCOMPARE(img->pixel(0, 0), qRgb(0x11, 0x22, 0x33));
        QCOMPARE(img->pixel(1, 1), qRgb(0xaa, 0xbb, 0xcc));
    }

    void xbgrSwapsChannels()
    {
        // XBGR8888: младший байт — R.
        const QByteArray b = buffer({0x00332211, 0, 0, 0});
        const auto img = imageFromShm(reinterpret_cast<const uchar*>(b.constData()), b.size(), 2, 2, 12, ShmFormat::XBGR8888, false);
        QVERIFY(img.has_value());
        QCOMPARE(img->pixel(0, 0), qRgb(0x11, 0x22, 0x33));
    }

    void yInvertFlips()
    {
        const QByteArray b = buffer({0x00ff0000, 0x00ff0000, 0x000000ff, 0x000000ff});
        const auto img = imageFromShm(reinterpret_cast<const uchar*>(b.constData()), b.size(), 2, 2, 12, ShmFormat::ARGB8888, true);
        QVERIFY(img.has_value());
        QCOMPARE(img->pixel(0, 0), qRgb(0, 0, 0xff));
        QCOMPARE(img->pixel(0, 1), qRgb(0xff, 0, 0));
    }

    void rejectsBadShm()
    {
        const QByteArray b = buffer({0, 0, 0, 0});
        const auto* p = reinterpret_cast<const uchar*>(b.constData());
        QVERIFY(!imageFromShm(p, b.size(), 2, 2, 12, 0x12345678, false).has_value()); // неизвестный формат
        QVERIFY(!imageFromShm(p, b.size(), 2, 2, 4, ShmFormat::XRGB8888, false).has_value()); // stride < width*4
        QVERIFY(!imageFromShm(p, b.size(), 0, 2, 12, ShmFormat::XRGB8888, false).has_value());
        QVERIFY(!imageFromShm(nullptr, b.size(), 2, 2, 12, ShmFormat::XRGB8888, false).has_value());
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

    // Кадр выхода 3×2 «abc/def» в ориентации буфера → логическая (экранная) ориентация для всех 8 wl_output_transform.
    // Ожидания выведены вручную: поворот на 90·k° по часовой стрелке, затем для flipped_* — отражение по горизонтали.
    void outputTransform_data()
    {
        QTest::addColumn<int>("transform");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("normal") << 0 << QStringList{QStringLiteral("abc"), QStringLiteral("def")};
        QTest::newRow("90") << 1 << QStringList{QStringLiteral("da"), QStringLiteral("eb"), QStringLiteral("fc")};
        QTest::newRow("180") << 2 << QStringList{QStringLiteral("fed"), QStringLiteral("cba")};
        QTest::newRow("270") << 3 << QStringList{QStringLiteral("cf"), QStringLiteral("be"), QStringLiteral("ad")};
        QTest::newRow("flipped") << 4 << QStringList{QStringLiteral("cba"), QStringLiteral("fed")};
        QTest::newRow("flipped_90") << 5 << QStringList{QStringLiteral("ad"), QStringLiteral("be"), QStringLiteral("cf")};
        QTest::newRow("flipped_180") << 6 << QStringList{QStringLiteral("def"), QStringLiteral("abc")};
        QTest::newRow("flipped_270") << 7 << QStringList{QStringLiteral("fc"), QStringLiteral("eb"), QStringLiteral("da")};
    }

    void outputTransform()
    {
        QFETCH(int, transform);
        QFETCH(QStringList, expected);
        const QImage out = applyOutputTransform(letters({QStringLiteral("abc"), QStringLiteral("def")}), transform);
        QCOMPARE(out.format(), QImage::Format_RGB32);
        QCOMPARE(out.size(), QSize(expected.first().size(), expected.size()));
        QCOMPARE(toLetters(out), expected);
    }

    void outputTransformRejectsUnknown()
    {
        const QImage source = letters({QStringLiteral("abc"), QStringLiteral("def")});
        QVERIFY(applyOutputTransform(source, 8).isNull());
        QVERIFY(applyOutputTransform(source, -1).isNull());
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

    void deepCopyKWin()
    {
        // Проверка, что imageFromKWin возвращает глубокую копию (не shallow copy).
        // После очистки исходного буфера пиксели остаются доступны.
        QByteArray b = buffer({0xff112233, 0xff445566, 0xff778899, 0xffaabbcc});
        const auto img = imageFromKWin(b, 2, 2, 12, QImage::Format_RGB32);
        QVERIFY(img.has_value());
        const QRgb pixel00 = img->pixel(0, 0);
        const QRgb pixel10 = img->pixel(1, 0);

        // Очистка исходного буфера (должна не повлиять на скопированное изображение).
        b.clear();

        // Проверка, что пиксели остались неизменны.
        QCOMPARE(img->pixel(0, 0), pixel00);
        QCOMPARE(img->pixel(1, 0), pixel10);
        QCOMPARE(img->pixel(0, 0), qRgb(0x11, 0x22, 0x33));
        QCOMPARE(img->pixel(1, 0), qRgb(0x44, 0x55, 0x66));
    }

    void rejectsHugeDimensions()
    {
        // Проверка, что очень большие размеры отклоняются.
        const QByteArray b = buffer({0, 0, 0, 0});
        const auto* p = reinterpret_cast<const uchar*>(b.constData());

        // Для imageFromShm.
        QVERIFY(!imageFromShm(p, b.size(), 100000, 2, 400000, ShmFormat::XRGB8888, false).has_value());
        QVERIFY(!imageFromShm(p, b.size(), 2, 100000, 12, ShmFormat::XRGB8888, false).has_value());

        // Для imageFromKWin.
        QVERIFY(!imageFromKWin(b, 100000, 2, 400000, QImage::Format_RGB32).has_value());
        QVERIFY(!imageFromKWin(b, 2, 100000, 12, QImage::Format_RGB32).has_value());
    }

    void rejectsStrideNotMultiple4()
    {
        // Проверка, что stride, не кратный 4, отклоняется.
        const QByteArray b = buffer({0, 0, 0, 0});
        const auto* p = reinterpret_cast<const uchar*>(b.constData());

        // Для imageFromShm: stride = 13 (не кратен 4).
        QVERIFY(!imageFromShm(p, b.size(), 2, 2, 13, ShmFormat::XRGB8888, false).has_value());

        // Для imageFromKWin: stride = 13 (не кратен 4).
        QVERIFY(!imageFromKWin(b, 2, 2, 13, QImage::Format_RGB32).has_value());
    }

    void rejectsInsufficientDataSize()
    {
        // Проверка, что недостаточный размер буфера отклоняется.
        const QByteArray b = buffer({0x00112233, 0x00445566, 0x00778899, 0x00aabbcc});
        const auto* p = reinterpret_cast<const uchar*>(b.constData());

        // stride * height = 12 * 2 = 24, но даём только 20 байт.
        QVERIFY(!imageFromShm(p, 20, 2, 2, 12, ShmFormat::XRGB8888, false).has_value());
    }

    void rejectsZeroHeight()
    {
        // Проверка, что height <= 0 отклоняется.
        const QByteArray b = buffer({0, 0, 0, 0});
        const auto* p = reinterpret_cast<const uchar*>(b.constData());

        // Для imageFromShm.
        QVERIFY(!imageFromShm(p, b.size(), 2, 0, 12, ShmFormat::XRGB8888, false).has_value());
        QVERIFY(!imageFromShm(p, b.size(), 2, -1, 12, ShmFormat::XRGB8888, false).has_value());

        // Для imageFromKWin.
        QVERIFY(!imageFromKWin(b, 2, 0, 12, QImage::Format_RGB32).has_value());
        QVERIFY(!imageFromKWin(b, 2, -1, 12, QImage::Format_RGB32).has_value());
    }

    void yInvertWithXBGR()
    {
        // Проверка, что yInvert и XBGR8888 работают вместе.
        const QByteArray b = buffer({0x00332211, 0x00332211, 0x00aabbcc, 0x00aabbcc});
        const auto img = imageFromShm(reinterpret_cast<const uchar*>(b.constData()), b.size(), 2, 2, 12, ShmFormat::XBGR8888, true);
        QVERIFY(img.has_value());
        // Без инверсии: (0,0) был бы 0x00332211, с инверсией — (0,1).
        // Без инверсии: (0,1) был бы 0x00aabbcc, с инверсией — (0,0).
        QCOMPARE(img->pixel(0, 0), qRgb(0xcc, 0xbb, 0xaa)); // из буфера (0,1)
        QCOMPARE(img->pixel(0, 1), qRgb(0x11, 0x22, 0x33)); // из буфера (0,0)
    }
};

QTEST_MAIN(TestRawImage)
#include "test_rawimage.moc"
