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
    bool hasKWinScreenShot2 = false; // Задача 4 заполняет
    QString forced;                  // PINCH_CAPTURE
};

QVector<CaptureMethod> captureOrder(const CaptureEnvironment& env);
QString captureMethodName(CaptureMethod m); // "x11" | "screencopy" | "kwin" | "portal"
CaptureEnvironment detectEnvironment();     // platformName, forced; под Wayland — hasScreencopy

// Пробует методы из captureOrder; errors получает строки «<метод>: <причина>» (локализованные).
// Методы, ещё не реализованные в сборке, дают ошибку «метод недоступен в этой сборке».
std::optional<Capture> captureScreens(const CaptureEnvironment& env, QStringList* errors);

// Для тестов: таблица функций снимка внедряется снаружи.
using CaptureFn = std::function<std::optional<Capture>(QString* error)>;
std::optional<Capture> captureScreensWith(const QVector<CaptureMethod>& order,
                                          const QMap<CaptureMethod, CaptureFn>& fns, QStringList* errors);
