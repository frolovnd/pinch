#pragma once

#include "capture.h"
#include "rawimage.h"

#include <QString>
#include <QVector>

#include <optional>

// Снимок через протокол wlr-screencopy (Hyprland, Sway) по собственному соединению wayland-client.
// В сборке без Wayland (PINCH_WAYLAND=OFF): screencopyAvailable() → false, captureWithScreencopy → ошибка.

// true, если композитор объявляет zwlr_screencopy_manager_v1. Открывает и закрывает собственное соединение.
bool screencopyAvailable();
// Снимок всех выходов. screens — QScreen'ы (имя + логическая геометрия) для сопоставления.
std::optional<Capture> captureWithScreencopy(const QVector<NamedScreen>& screens, QString* error);
