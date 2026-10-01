#pragma once

#include "capture.h"

#include <QDBusConnection>
#include <QRect>
#include <QString>
#include <QVector>

#include <optional>

// Есть ли у org.kde.KWin объект /org/kde/KWin/ScreenShot2 (интроспекция). Службу не активирует.
bool kwinScreenShotAvailable(QDBusConnection bus);

// Снимок рабочей области через org.kde.KWin.ScreenShot2.CaptureWorkspace.
// screens — логические геометрии мониторов (QScreen::geometry()) в глобальных координатах.
std::optional<Capture> captureWithKWin(QDBusConnection bus, const QVector<QRect>& screens, QString* error);
