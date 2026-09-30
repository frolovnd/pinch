#include "keys.h"

#include <Qt>

int layoutIndependentKey(int key, quint32 nativeScanCode)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return key;
    if (key >= Qt::Key_Escape) // Esc, Enter, стрелки и прочие спецклавиши
        return key;

    struct Entry {
        quint32 scanCode;
        int key;
    };
    static const Entry kTable[] = {
        {24, Qt::Key_Q}, {25, Qt::Key_W}, {26, Qt::Key_E}, {27, Qt::Key_R}, {28, Qt::Key_T},
        {29, Qt::Key_Y}, {30, Qt::Key_U}, {31, Qt::Key_I}, {32, Qt::Key_O}, {33, Qt::Key_P},
        {38, Qt::Key_A}, {39, Qt::Key_S}, {40, Qt::Key_D}, {41, Qt::Key_F}, {42, Qt::Key_G},
        {43, Qt::Key_H}, {44, Qt::Key_J}, {45, Qt::Key_K}, {46, Qt::Key_L},
        {52, Qt::Key_Z}, {53, Qt::Key_X}, {54, Qt::Key_C}, {55, Qt::Key_V}, {56, Qt::Key_B},
        {57, Qt::Key_N}, {58, Qt::Key_M},
    };
    for (const Entry& e : kTable) {
        if (e.scanCode == nativeScanCode)
            return e.key;
    }
    return key;
}
