#include "settings.h"

#include <QSettings>

namespace {
QSettings store()
{
    return QSettings(QStringLiteral("pinch"), QStringLiteral("pinch"));
}

// Принимает только «#rrggbb».
bool parseColor(const QString& text, QColor* color)
{
    if (text.size() != 7 || !text.startsWith(QLatin1Char('#')))
        return false;
    bool ok = false;
    const uint rgb = text.mid(1).toUInt(&ok, 16);
    if (!ok)
        return false;
    *color = QColor::fromRgb(QRgb(rgb | 0xff000000u));
    return true;
}
}

Settings Settings::load()
{
    Settings result;
    const QSettings s = store();
    QColor color;
    if (parseColor(s.value(QStringLiteral("color")).toString(), &color))
        result.color = color;
    bool ok = false;
    const int thickness = s.value(QStringLiteral("thickness")).toInt(&ok);
    if (ok)
        result.thickness = qBound(1, thickness, 40);
    return result;
}

void Settings::save() const
{
    QSettings s = store();
    s.setValue(QStringLiteral("color"), color.name());
    s.setValue(QStringLiteral("thickness"), thickness);
    s.sync();
}
