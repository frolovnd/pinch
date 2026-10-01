#pragma once

#include "capture.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>

enum class CaptureMethod { X11, Screencopy, KWin, Portal };

struct CaptureEnvironment {
    QString platformName;            // QGuiApplication::platformName()
    bool hasScreencopy = false;      // композитор объявляет zwlr_screencopy_manager_v1
    bool hasKWinScreenShot2 = false; // на шине есть org.kde.KWin с интерфейсом ScreenShot2
    QString forced;                  // PINCH_CAPTURE
    bool waylandDisplay = false;     // задан WAYLAND_DISPLAY (сеанс Wayland, даже если Qt работает через xcb)
};

// Платформа Qt — Wayland: «wayland», «wayland-egl» и т. п.
bool isWaylandPlatform(const QString& platformName);
QVector<CaptureMethod> captureOrder(const CaptureEnvironment& env);
QString captureMethodName(CaptureMethod m); // "x11" | "screencopy" | "kwin" | "portal"
CaptureEnvironment detectEnvironment();     // platformName, forced, waylandDisplay; под Wayland — hasScreencopy
// Проверять ли наличие KWin ScreenShot2 (вызов D-Bus при запуске).
bool shouldProbeKWin(bool hasScreencopy, const QString& currentDesktop);
// Qt работает через xcb (Xwayland) в сеансе Wayland, а снимать будет способ x11: снимок может оказаться чёрным.
bool xwaylandFallback(const CaptureEnvironment& env);

// Пробует методы из captureOrder; errors получает строки «<метод>: <причина>» (локализованные), used — успешный способ.
// Методы, ещё не реализованные в сборке, дают ошибку «метод недоступен в этой сборке».
std::optional<Capture> captureScreens(const CaptureEnvironment& env, QStringList* errors, CaptureMethod* used = nullptr);

// Для тестов: таблица функций снимка внедряется снаружи.
using CaptureFn = std::function<std::optional<Capture>(QString* error)>;
std::optional<Capture> captureScreensWith(const QVector<CaptureMethod>& order, const QMap<CaptureMethod, CaptureFn>& fns,
                                          QStringList* errors, CaptureMethod* used = nullptr);
