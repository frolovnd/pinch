#pragma once

#include "capture.h"
#include "rawimage.h"

#include <QString>
#include <QVector>

#include <optional>

// Снимок через протокол wlr-screencopy (Hyprland, Sway) по собственному соединению wayland-client.
// В сборке без Wayland (PINCH_WAYLAND=OFF): screencopyAvailable() → false, captureWithScreencopy → ошибка.

// Общий тайм-аут обмена с композитором (реестр и все кадры).
constexpr int SCREENCOPY_TIMEOUT_MS = 5000;
// Больше выходов не снимаем: защита от композитора, объявляющего тысячи wl_output (память, memfd).
constexpr int SCREENCOPY_MAX_OUTPUTS = 16;

// true, если композитор объявляет zwlr_screencopy_manager_v1. Открывает и закрывает собственное соединение.
bool screencopyAvailable();
// Снимок всех выходов. screens — QScreen'ы (имя + логическая геометрия) для сопоставления.
// timeoutMs — общий тайм-аут; параметр существует для тестов (короткое ожидание зависшего композитора).
std::optional<Capture> captureWithScreencopy(const QVector<NamedScreen>& screens, QString* error,
                                             int timeoutMs = SCREENCOPY_TIMEOUT_MS);
