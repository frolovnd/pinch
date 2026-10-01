#include "rawimage.h"

#include <algorithm>
#include <cstring>
#include <iterator>

std::optional<QImage> imageFromShm(const uchar* data, int width, int height, int stride, quint32 format, bool yInvert)
{
    if (!data || width <= 0 || height <= 0 || stride < width * 4)
        return std::nullopt;
    const bool xrgb = format == ShmFormat::XRGB8888 || format == ShmFormat::ARGB8888;
    const bool xbgr = format == ShmFormat::XBGR8888 || format == ShmFormat::ABGR8888;
    if (!xrgb && !xbgr)
        return std::nullopt;
    QImage out(width, height, QImage::Format_RGB32);
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
    static const int kAllowed[] = {QImage::Format_RGB32, QImage::Format_ARGB32, QImage::Format_ARGB32_Premultiplied,
                                   QImage::Format_RGBX8888, QImage::Format_RGBA8888};
    if (std::find(std::begin(kAllowed), std::end(kAllowed), qimageFormat) == std::end(kAllowed))
        return std::nullopt;
    if (width <= 0 || height <= 0 || stride < width * 4 || data.size() < qsizetype(stride) * height)
        return std::nullopt;
    const QImage view(reinterpret_cast<const uchar*>(data.constData()), width, height, stride,
                      static_cast<QImage::Format>(qimageFormat));
    return view.convertToFormat(QImage::Format_RGB32); // глубокая копия
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
