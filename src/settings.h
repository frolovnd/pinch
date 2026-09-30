#pragma once

#include <QColor>

// Цвет и толщина между запусками: ~/.config/pinch/pinch.conf.
struct Settings {
    QColor color = QColor(0xE5, 0x39, 0x35);
    int thickness = 4;

    // Невалидные значения заменяются значениями по умолчанию, толщина прижимается к 1..40.
    static Settings load();
    void save() const;
};
