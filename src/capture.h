#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QVector>

#include <optional>

// Снимок одного монитора; geometry — в глобальных координатах X.
struct ScreenShot {
    QImage image;
    QRect geometry;
};

// Снимок всех мониторов одним изображением (Format_RGB32).
struct Capture {
    QImage image;
    QPoint origin;          // глобальные координаты X левого верхнего угла изображения
    QVector<QRect> screens; // геометрии мониторов в координатах изображения
};

// Склеивает снимки в изображение размером с их охватывающий прямоугольник; мёртвые зоны чёрные.
Capture composeScreens(const QVector<ScreenShot>& shots);

// Снимает все мониторы. nullopt, если мониторов нет или снимок не удался.
std::optional<Capture> captureAllScreens();
