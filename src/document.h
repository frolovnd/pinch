#pragma once

#include "annotation.h"

#include <QVector>

// Нарисованные аннотации с историей. Отменяется и повторяется только добавление.
class Document {
public:
    void add(Annotation annotation); // очищает историю повтора
    bool undo();                     // false, если нечего отменять
    bool redo();                     // false, если нечего повторять
    bool canUndo() const;
    bool canRedo() const;
    const QVector<Annotation>& annotations() const;
    int nextCounterNumber() const;   // номер, который получит следующий Counter

private:
    QVector<Annotation> m_items;
    QVector<Annotation> m_redo;
};
