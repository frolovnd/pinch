#pragma once

#include <QImage>
#include <QRect>
#include <QStringList>
#include <QVector>
#include <optional>

// Коды форматов wl_shm (протокол Wayland).
namespace ShmFormat {
constexpr quint32 ARGB8888 = 0;
constexpr quint32 XRGB8888 = 1;
constexpr quint32 ABGR8888 = 0x34324241;
constexpr quint32 XBGR8888 = 0x34324258;
}

// Максимальный размер измерения экрана (санитарный лимит: экраны никогда не больше).
constexpr int MAX_SCREEN_DIMENSION = 32768;

// Буфер wl_shm → глубокая копия в Format_RGB32 (альфа всегда 0xff). nullopt: неизвестный формат,
// width/height <= 0, stride < width*4, stride % 4 != 0, dataSize недостаточно, огромные размеры,
// data == nullptr, переполнение при вычислении размеров.
std::optional<QImage> imageFromShm(const uchar* data, qsizetype dataSize, int width, int height, int stride, quint32 format, bool yInvert);

// Ответ KWin ScreenShot2 → Format_RGB32 (глубокая копия). Разрешены значения QImage::Format: RGB32, ARGB32,
// ARGB32_Premultiplied, RGBX8888, RGBA8888. nullopt: иной формат, размеры <= 0, stride < width*4,
// stride % 4 != 0, огромные размеры, data.size() < stride*height, переполнение при вычислении размеров.
std::optional<QImage> imageFromKWin(const QByteArray& data, int width, int height, int stride, int qimageFormat);

struct NamedScreen {
    QString name;
    QRect geometry;
};

// Геометрии для выходов по именам. Если все имена найдены — по именам; иначе, если количество совпадает, — по порядку;
// иначе — пусто.
QVector<QRect> matchScreens(const QStringList& outputNames, const QVector<NamedScreen>& screens);
