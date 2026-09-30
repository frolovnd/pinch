#pragma once

#include "annotation.h"

#include <QFont>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QVector>

class QPainter;

// Размеры, выводимые из толщины; общие для превью, результата и тестов.
int markerWidth(int thickness);     // 4 × t
int textPixelSize(int thickness);   // 10 + 3 × t
int counterDiameter(int thickness); // 16 + 4 × t
int pixelateBlock(int thickness);   // max(12, 4 × t)
int arrowHeadLength(int thickness); // max(10, 4 × t)
QFont textFont(int thickness);

// Рисует аннотацию в координатах изображения любым QPainter (превью на виджете и paintAnnotation).
// Pixelate здесь только обозначается пунктирной рамкой.
void drawAnnotation(QPainter& painter, const Annotation& a, int counterNumber);

// Рисует аннотацию на canvas; offset — координаты изображения, соответствующие (0,0) canvas.
// Pixelate читает и заменяет пиксели canvas. QPainter закрывается до выхода из функции.
void paintAnnotation(QImage& canvas, QPoint offset, const Annotation& a, int counterNumber);

// Итог: base.copy(selection) в Format_RGB32 со всеми аннотациями по порядку; всё вне выделения обрезается.
QImage render(const QImage& base, const QRect& selection, const QVector<Annotation>& annotations);
