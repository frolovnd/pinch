#pragma once

#include <QStringList>

class QCoreApplication;
class QLocale;

// Устанавливает таблицы переводов приложения и Qt для языка locale. Возвращает список загруженных таблиц
// (для тестов и диагностики), например {"pinch_en", "pinch_ru", "qtbase_ru"}.
QStringList installTranslations(QCoreApplication& app, const QLocale& locale);
