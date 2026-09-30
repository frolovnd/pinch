#include "document.h"

#include <algorithm>

void Document::add(Annotation annotation)
{
    m_items.append(std::move(annotation));
    m_redo.clear();
}

bool Document::undo()
{
    if (m_items.isEmpty())
        return false;
    m_redo.append(m_items.takeLast());
    return true;
}

bool Document::redo()
{
    if (m_redo.isEmpty())
        return false;
    m_items.append(m_redo.takeLast());
    return true;
}

bool Document::canUndo() const
{
    return !m_items.isEmpty();
}

bool Document::canRedo() const
{
    return !m_redo.isEmpty();
}

const QVector<Annotation>& Document::annotations() const
{
    return m_items;
}

int Document::nextCounterNumber() const
{
    const auto isCounter = [](const Annotation& a) { return a.tool == Tool::Counter; };
    return int(std::count_if(m_items.cbegin(), m_items.cend(), isCounter)) + 1;
}
