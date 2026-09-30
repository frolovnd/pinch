#include "geometry.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdlib>

QRect rectFromPoints(QPoint a, QPoint b)
{
    return QRect(QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                 QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
}

Handle hitTestHandle(const QRect& sel, QPoint p, int tolerance)
{
    if (sel.isEmpty())
        return Handle::None;
    const bool inX = p.x() >= sel.left() - tolerance && p.x() <= sel.right() + tolerance;
    const bool inY = p.y() >= sel.top() - tolerance && p.y() <= sel.bottom() + tolerance;
    if (!inX || !inY)
        return Handle::None;

    const auto near = [tolerance](int value, int edge) { return std::abs(value - edge) <= tolerance; };
    const bool l = near(p.x(), sel.left());
    const bool r = near(p.x(), sel.right());
    const bool t = near(p.y(), sel.top());
    const bool b = near(p.y(), sel.bottom());
    if (t && l)
        return Handle::TopLeft;
    if (t && r)
        return Handle::TopRight;
    if (b && r)
        return Handle::BottomRight;
    if (b && l)
        return Handle::BottomLeft;
    if (t)
        return Handle::Top;
    if (b)
        return Handle::Bottom;
    if (l)
        return Handle::Left;
    if (r)
        return Handle::Right;
    return sel.contains(p) ? Handle::Move : Handle::None;
}

QRect applyHandleDrag(const QRect& original, Handle h, QPoint delta, const QRect& bounds)
{
    if (h == Handle::None || original.isEmpty())
        return original;

    if (h == Handle::Move) {
        QRect r = original.translated(delta);
        // Сначала правый и нижний края, потом левый и верхний: выделение больше bounds
        // прижмётся к левому верхнему углу.
        if (r.right() > bounds.right())
            r.moveRight(bounds.right());
        if (r.bottom() > bounds.bottom())
            r.moveBottom(bounds.bottom());
        if (r.left() < bounds.left())
            r.moveLeft(bounds.left());
        if (r.top() < bounds.top())
            r.moveTop(bounds.top());
        return r;
    }

    int left = original.left();
    int top = original.top();
    int right = original.right();
    int bottom = original.bottom();
    switch (h) {
    case Handle::TopLeft:
        left += delta.x();
        top += delta.y();
        break;
    case Handle::Top:
        top += delta.y();
        break;
    case Handle::TopRight:
        right += delta.x();
        top += delta.y();
        break;
    case Handle::Right:
        right += delta.x();
        break;
    case Handle::BottomRight:
        right += delta.x();
        bottom += delta.y();
        break;
    case Handle::Bottom:
        bottom += delta.y();
        break;
    case Handle::BottomLeft:
        left += delta.x();
        bottom += delta.y();
        break;
    case Handle::Left:
        left += delta.x();
        break;
    case Handle::None:
    case Handle::Move:
        break;
    }
    return rectFromPoints(QPoint(left, top), QPoint(right, bottom)).intersected(bounds);
}

QPoint snapLine45(QPoint start, QPoint end)
{
    const double dx = end.x() - start.x();
    const double dy = end.y() - start.y();
    if (dx == 0 && dy == 0)
        return end;
    constexpr double step = 3.14159265358979323846 / 4;
    const double angle = std::round(std::atan2(dy, dx) / step) * step;
    const double ux = std::cos(angle);
    const double uy = std::sin(angle);
    const double length = dx * ux + dy * uy;
    return start + QPoint(qRound(length * ux), qRound(length * uy));
}

QPoint snapSquare(QPoint start, QPoint end)
{
    const int dx = end.x() - start.x();
    const int dy = end.y() - start.y();
    const int side = std::max(std::abs(dx), std::abs(dy));
    return start + QPoint(dx < 0 ? -side : side, dy < 0 ? -side : side);
}

QPoint placeToolbar(const QRect& sel, QSize toolbar, const QVector<QRect>& screens, int margin)
{
    if (screens.isEmpty())
        return QPoint(sel.left(), sel.bottom() + margin);

    // Монитор: содержащий центр выделения, иначе с наибольшим пересечением, иначе первый.
    QRect screen;
    for (const QRect& s : screens) {
        if (s.contains(sel.center())) {
            screen = s;
            break;
        }
    }
    if (screen.isNull()) {
        qint64 bestArea = -1;
        for (const QRect& s : screens) {
            const QRect i = s.intersected(sel);
            const qint64 area = i.isEmpty() ? 0 : qint64(i.width()) * i.height();
            if (area > bestArea) {
                bestArea = area;
                screen = s;
            }
        }
    }

    const int h = toolbar.height();
    const int candidates[] = {sel.bottom() + margin, sel.top() - margin - h, sel.bottom() - margin - h};
    int y = screen.bottom() - h + 1; // ни один кандидат не подошёл — прижать к нижнему краю монитора
    for (int c : candidates) {
        if (c >= screen.top() && c + h - 1 <= screen.bottom()) {
            y = c;
            break;
        }
    }
    const int x = std::max(screen.left(), std::min(sel.left(), screen.right() - toolbar.width() + 1));
    return QPoint(x, y);
}
