#pragma once

#include "annotation.h"

#include <QColor>
#include <QMap>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QToolButton;

// Панель под выделением. Кнопки не принимают фокус, чтобы клавиатура всегда оставалась у оверлея.
class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget* parent = nullptr);

    static const QVector<QColor>& palette();

public slots:
    void setTool(Tool tool);
    void setColor(const QColor& color);
    void setThickness(int thickness);
    void setUndoRedoEnabled(bool canUndo, bool canRedo);

signals:
    void toolChosen(Tool tool);
    void colorChosen(const QColor& color);
    void undoRequested();
    void redoRequested();
    void copyRequested();
    void quickSaveRequested();
    void saveAsRequested();
    void closeRequested();

private:
    QToolButton* addButton(const QString& objectName, const QString& text, const QString& toolTip);
    void addSeparator();

    QHBoxLayout* m_layout = nullptr;
    QMap<Tool, QToolButton*> m_toolButtons;
    QVector<QToolButton*> m_colorButtons;
    QLabel* m_thickness = nullptr;
    QToolButton* m_undo = nullptr;
    QToolButton* m_redo = nullptr;
    Tool m_tool = Tool::None;
};
