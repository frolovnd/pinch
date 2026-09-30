#include "capture.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>

Capture composeScreens(const QVector<ScreenShot>& shots)
{
    QRect bounds;
    for (const ScreenShot& s : shots)
        bounds = bounds.united(s.geometry);

    Capture result;
    if (bounds.isEmpty())
        return result;
    result.origin = bounds.topLeft();
    result.image = QImage(bounds.size(), QImage::Format_RGB32);
    result.image.fill(Qt::black);

    QPainter painter(&result.image);
    for (const ScreenShot& s : shots) {
        const QRect local = s.geometry.translated(-bounds.topLeft());
        painter.drawImage(local, s.image);
        result.screens.append(local);
    }
    return result;
}

std::optional<Capture> captureAllScreens()
{
    QVector<ScreenShot> shots;
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        // На X11 grabWindow(0) снимает корневое окно в пределах этого монитора.
        const QPixmap pixmap = screen->grabWindow(0);
        if (pixmap.isNull())
            return std::nullopt;
        shots.append({pixmap.toImage(), screen->geometry()});
    }
    if (shots.isEmpty())
        return std::nullopt;
    return composeScreens(shots);
}
