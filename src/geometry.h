#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>

// Чистая геометрия выделения и панели инструментов. Все координаты — координаты изображения.

enum class Handle { None, Move, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

// Нормализованный прямоугольник; обе точки входят в него включительно.
QRect rectFromPoints(QPoint a, QPoint b);

// Что под точкой p: угол, сторона (в пределах tolerance), внутренность (Move) или ничего.
// Углы важнее сторон.
Handle hitTestHandle(const QRect& sel, QPoint p, int tolerance);

// Выделение после перетаскивания ручки h на delta. Move сдвигает без изменения размера и прижимает
// к bounds; остальные ручки двигают свои стороны, результат нормализуется и обрезается по bounds.
QRect applyHandleDrag(const QRect& original, Handle h, QPoint delta, const QRect& bounds);

// Конец отрезка с углом, округлённым до кратного 45°; длина проекции сохраняется.
QPoint snapLine45(QPoint start, QPoint end);

// Конец квадрата: сторона = max(|dx|, |dy|), направления по осям сохраняются.
QPoint snapSquare(QPoint start, QPoint end);

// Левый верхний угол панели размером toolbar рядом с выделением так, чтобы панель
// целиком лежала на одном мониторе.
QPoint placeToolbar(const QRect& sel, QSize toolbar, const QVector<QRect>& screens, int margin = 8);

// Левый верхний угол надписи с размером выделения. Над выделением, если эта точка лежит на мониторе,
// содержащем левый верхний угол выделения; иначе — внутри, у левого верхнего угла пересечения выделения
// с первым (по порядку screens) монитором, которого оно касается, со сдвигом (4, 4).
QPoint sizeLabelPosition(const QRect& sel, QSize label, const QVector<QRect>& screens);
