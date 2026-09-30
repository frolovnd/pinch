#include "renderer.h"

#include "geometry.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStringList>

#include <algorithm>

int markerWidth(int thickness)
{
    return 4 * thickness;
}

int textPixelSize(int thickness)
{
    return 10 + 3 * thickness;
}

int counterDiameter(int thickness)
{
    return 16 + 4 * thickness;
}

int pixelateBlock(int thickness)
{
    return std::max(12, 4 * thickness);
}

int arrowHeadLength(int thickness)
{
    return std::max(10, 4 * thickness);
}

QFont textFont(int thickness)
{
    QFont font = QGuiApplication::font();
    font.setPixelSize(textPixelSize(thickness));
    return font;
}

namespace {
QRectF boxOf(const Annotation& a)
{
    return QRectF(QPointF(a.points.at(0)), QPointF(a.points.at(1))).normalized();
}

void drawArrow(QPainter& painter, const Annotation& a)
{
    const QPointF start = a.points.at(0);
    const QPointF end = a.points.at(1);
    const double length = QLineF(start, end).length();
    if (length < 1)
        return;
    const double head = std::min<double>(arrowHeadLength(a.style.thickness), length);
    const QPointF dir = (end - start) / length;
    const QPointF normal(-dir.y(), dir.x());
    const QPointF base = end - dir * head;

    painter.setPen(QPen(a.style.color, a.style.thickness, Qt::SolidLine, Qt::FlatCap));
    painter.drawLine(start, base);
    painter.setPen(Qt::NoPen);
    painter.setBrush(a.style.color);
    painter.drawPolygon(QPolygonF{end, base + normal * (head / 2), base - normal * (head / 2)});
}

void drawMarker(QPainter& painter, const Annotation& a)
{
    if (a.points.size() < 2)
        return;
    QPainterPath path(a.points.constFirst());
    for (int i = 1; i < a.points.size(); ++i)
        path.lineTo(a.points.at(i));
    // Контур штриха заливается один раз: самопересечения не темнеют.
    QPainterPathStroker stroker;
    stroker.setWidth(markerWidth(a.style.thickness));
    stroker.setCapStyle(Qt::FlatCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    QPainterPath outline = stroker.createStroke(path);
    outline.setFillRule(Qt::WindingFill);
    QColor color = a.style.color;
    color.setAlpha(100);
    painter.fillPath(outline, color);
}

void drawText(QPainter& painter, const Annotation& a)
{
    const QFont font = textFont(a.style.thickness);
    const QFontMetrics metrics(font);
    painter.setFont(font);
    painter.setPen(a.style.color);
    const QPoint origin = a.points.constFirst();
    const QStringList lines = a.text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i)
        painter.drawText(QPoint(origin.x(), origin.y() + metrics.ascent() + i * metrics.lineSpacing()), lines.at(i));
}

void drawCounter(QPainter& painter, const Annotation& a, int number)
{
    const int d = counterDiameter(a.style.thickness);
    const QPointF c = a.points.constFirst();
    const QRectF circle(c.x() - d / 2.0, c.y() - d / 2.0, d, d);
    painter.setPen(Qt::NoPen);
    painter.setBrush(a.style.color);
    painter.drawEllipse(circle);

    QFont font = QGuiApplication::font();
    font.setBold(true);
    font.setPixelSize(std::max(1, qRound(d * 0.55)));
    painter.setFont(font);
    painter.setPen(a.style.color.lightness() < 140 ? QColor(Qt::white) : QColor(Qt::black));
    painter.drawText(circle, Qt::AlignCenter, QString::number(number));
}

void pixelate(QImage& canvas, QPoint offset, const Annotation& a)
{
    const QRect area = rectFromPoints(a.points.at(0), a.points.at(1)).translated(-offset).intersected(canvas.rect());
    if (area.isEmpty())
        return;
    const int block = pixelateBlock(a.style.thickness);
    const int w = (area.width() + block - 1) / block;
    const int h = (area.height() + block - 1) / block;
    // Уменьшение с усреднением, затем увеличение без сглаживания — однотонные блоки.
    const QImage small = canvas.copy(area).scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage big = small.scaled(area.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QPainter painter(&canvas);
    painter.drawImage(area.topLeft(), big);
}
}

void drawAnnotation(QPainter& painter, const Annotation& a, int counterNumber)
{
    if (a.points.isEmpty())
        return;
    const bool twoPoints = a.points.size() >= 2;
    const QColor color = a.style.color;
    const int t = a.style.thickness;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    switch (a.tool) {
    case Tool::Pen:
        painter.setPen(QPen(color, t, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (a.points.size() == 1)
            painter.drawPoint(a.points.constFirst());
        else
            painter.drawPolyline(a.points.constData(), int(a.points.size()));
        break;
    case Tool::Marker:
        drawMarker(painter, a);
        break;
    case Tool::Line:
        if (twoPoints) {
            painter.setPen(QPen(color, t, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(a.points.at(0), a.points.at(1));
        }
        break;
    case Tool::Arrow:
        if (twoPoints)
            drawArrow(painter, a);
        break;
    case Tool::Rect:
        if (twoPoints) {
            painter.setPen(QPen(color, t, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            painter.drawRect(boxOf(a));
        }
        break;
    case Tool::Ellipse:
        if (twoPoints) {
            painter.setPen(QPen(color, t));
            painter.drawEllipse(boxOf(a));
        }
        break;
    case Tool::Text:
        drawText(painter, a);
        break;
    case Tool::Counter:
        drawCounter(painter, a, counterNumber);
        break;
    case Tool::Pixelate:
        if (twoPoints) {
            painter.setPen(QPen(color, 1, Qt::DashLine));
            painter.drawRect(boxOf(a));
        }
        break;
    case Tool::None:
        break;
    }
    painter.restore();
}

void paintAnnotation(QImage& canvas, QPoint offset, const Annotation& a, int counterNumber)
{
    if (a.tool == Tool::Pixelate) {
        if (a.points.size() >= 2)
            pixelate(canvas, offset, a);
        return;
    }
    QPainter painter(&canvas);
    painter.translate(-offset);
    drawAnnotation(painter, a, counterNumber);
}

QImage render(const QImage& base, const QRect& selection, const QVector<Annotation>& annotations)
{
    const QRect area = selection.intersected(base.rect());
    if (area.isEmpty())
        return {};
    QImage canvas = base.copy(area).convertToFormat(QImage::Format_RGB32);
    const QVector<int> numbers = counterNumbers(annotations);
    for (int i = 0; i < annotations.size(); ++i)
        paintAnnotation(canvas, area.topLeft(), annotations.at(i), numbers.at(i));
    return canvas;
}
