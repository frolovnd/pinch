#include "rawimage.h"

#include <QTransform>

#include <algorithm>
#include <cstring>
#include <iterator>

std::optional<QImage> imageFromShm(const uchar* data, qsizetype dataSize, int width, int height, int stride, quint32 format, bool yInvert)
{
    // Базовые проверки: данные, размеры и stride.
    if (!data || width <= 0 || height <= 0 || stride <= 0)
        return std::nullopt;
    if (width > MAX_SCREEN_DIMENSION || height > MAX_SCREEN_DIMENSION)
        return std::nullopt;
    if (stride % 4 != 0)
        return std::nullopt;

    // Проверка stride на переполнение и соответствие width.
    if (qint64(width) * 4 > stride)
        return std::nullopt;

    // Проверка dataSize на переполнение и соответствие stride*height.
    qint64 needed = qint64(stride) * height;
    if (needed > dataSize)
        return std::nullopt;

    // Проверка формата.
    const bool xrgb = format == ShmFormat::XRGB8888 || format == ShmFormat::ARGB8888;
    const bool xbgr = format == ShmFormat::XBGR8888 || format == ShmFormat::ABGR8888;
    if (!xrgb && !xbgr)
        return std::nullopt;

    // Создание выходного изображения (проверка на переполнение при выделении).
    QImage out(width, height, QImage::Format_RGB32);
    if (out.isNull())
        return std::nullopt;

    for (int y = 0; y < height; ++y) {
        const uchar* srcRow = data + qsizetype(yInvert ? height - 1 - y : y) * stride;
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < width; ++x) {
            quint32 v;
            std::memcpy(&v, srcRow + x * 4, 4); // невыровненный буфер — только memcpy
            if (xbgr)
                v = ((v & 0xffu) << 16) | (v & 0xff00u) | ((v >> 16) & 0xffu);
            dst[x] = 0xff000000u | (v & 0x00ffffffu);
        }
    }
    return out;
}

std::optional<QImage> imageFromKWin(const QByteArray& data, int width, int height, int stride, int qimageFormat)
{
    static const int kAllowed[] = {QImage::Format_RGB32,    QImage::Format_ARGB32,   QImage::Format_ARGB32_Premultiplied,
                                   QImage::Format_RGBX8888, QImage::Format_RGBA8888, QImage::Format_RGBA8888_Premultiplied};
    if (std::find(std::begin(kAllowed), std::end(kAllowed), qimageFormat) == std::end(kAllowed))
        return std::nullopt;

    // Базовые проверки: размеры и stride.
    if (width <= 0 || height <= 0 || stride <= 0)
        return std::nullopt;
    if (width > MAX_SCREEN_DIMENSION || height > MAX_SCREEN_DIMENSION)
        return std::nullopt;
    if (stride % 4 != 0)
        return std::nullopt;

    // Проверка stride на переполнение и соответствие width.
    if (qint64(width) * 4 > stride)
        return std::nullopt;

    // Проверка dataSize на переполнение и соответствие stride*height.
    qint64 needed = qint64(stride) * height;
    if (needed > data.size())
        return std::nullopt;

    // Создание представления изображения (проверка на переполнение при выделении).
    const QImage view(reinterpret_cast<const uchar*>(data.constData()), width, height, stride,
                      static_cast<QImage::Format>(qimageFormat));
    if (view.isNull())
        return std::nullopt;

    // Глубокая копия: всегда через copy() для RGB32 (convertToFormat возвращает shallow copy),
    // иначе convertToFormat + copy() для безопасности.
    const QImage converted = view.convertToFormat(QImage::Format_RGB32);
    if (converted.isNull())
        return std::nullopt;
    return converted.copy();
}

QImage applyOutputTransform(const QImage& image, int wlTransform)
{
    if (wlTransform < 0 || wlTransform > 7)
        return {};
    // Повороты на 90° кратно и отражения у QImage точные (перестановка пикселей, без интерполяции).
    QImage out = image;
    const int quarterTurns = wlTransform & 3; // 90, 180, 270 (младшие биты; бит 4 — flipped)
    if (quarterTurns != 0)
        out = out.transformed(QTransform().rotate(90.0 * quarterTurns)); // в QImage (y вниз) — по часовой стрелке
    if (wlTransform & 4)
        out = out.mirrored(true, false);
    return out;
}

QVector<QRect> matchScreens(const QStringList& outputNames, const QVector<NamedScreen>& screens)
{
    QVector<QRect> byName;
    for (const QString& name : outputNames) {
        const auto it = std::find_if(screens.cbegin(), screens.cend(),
                                     [&](const NamedScreen& s) { return !name.isEmpty() && s.name == name; });
        if (it == screens.cend()) {
            byName.clear();
            break;
        }
        byName << it->geometry;
    }
    if (byName.size() == outputNames.size() && !outputNames.isEmpty())
        return byName;
    if (outputNames.size() != screens.size())
        return {};
    QVector<QRect> byOrder;
    for (const NamedScreen& s : screens)
        byOrder << s.geometry;
    return byOrder;
}
