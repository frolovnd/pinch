#include "appicon.h"

QIcon appIcon()
{
    QIcon icon;
    for (const int size : {32, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/%1x%1/apps/pinch.png").arg(size), QSize(size, size));
    return icon;
}
