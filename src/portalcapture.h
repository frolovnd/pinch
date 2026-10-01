#pragma once

#include "capture.h"

#include <QDBusConnection>
#include <QRect>
#include <QString>
#include <QVector>

#include <optional>

// Чистое правило: можно ли удалить файл, который вернул портал. true только если lstat успешен,
// это обычный файл (не симлинк), владелец — getuid(), и mtime >= requestStartSecs - 2.
bool shouldDeletePortalFile(const QString& path, qint64 requestStartSecs);

// Путь объекта запроса по правилам портала: /org/freedesktop/portal/desktop/request/<sender>/<token>,
// где sender — уникальное имя соединения без ':' и с '.' -> '_'.
QString portalRequestPath(const QString& uniqueName, const QString& token);

// Снимок через org.freedesktop.portal.Screenshot (без диалога). Ответ портала и файл недоверенные.
// screens — логические геометрии мониторов (QScreen::geometry()); timeoutMs — предел ожидания ответа.
std::optional<Capture> captureWithPortal(QDBusConnection bus, const QVector<QRect>& screens, QString* error,
                                         int timeoutMs = 15000);
