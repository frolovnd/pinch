#pragma once

#include <QColor>
#include <QPoint>
#include <QString>
#include <QVector>

enum class Tool { None, Pen, Marker, Line, Arrow, Rect, Ellipse, Text, Counter, Pixelate };

struct Style {
    QColor color;
    int thickness = 4; // 1..40
};

// Одна нарисованная фигура в координатах изображения.
struct Annotation {
    Tool tool = Tool::None;
    Style style;
    // Pen/Marker — путь; Line/Arrow/Rect/Ellipse/Pixelate — [начало, конец]; Text/Counter — [точка].
    QVector<QPoint> points;
    QString text; // только Text, строки через '\n'
};

// Номер каждого Counter по порядку (1, 2, 3…); у остальных аннотаций 0.
// Номер не хранится в аннотации, поэтому после отмены нумерация пересчитывается сама.
inline QVector<int> counterNumbers(const QVector<Annotation>& annotations)
{
    QVector<int> numbers;
    numbers.reserve(annotations.size());
    int next = 0;
    for (const Annotation& a : annotations)
        numbers.append(a.tool == Tool::Counter ? ++next : 0);
    return numbers;
}
