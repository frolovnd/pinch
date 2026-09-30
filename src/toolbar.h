#pragma once

#include "annotation.h"

#include <QColor>
#include <QMouseEvent>
#include <QWheelEvent>
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

protected:
    // Клики по фону панели не должны доходить до оверлея (иначе меняют выделение или ставят номерок).
    // Колесо передаём оверлею явно: им меняют толщину.
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* event) override { event->accept(); }
    void mouseMoveEvent(QMouseEvent* event) override { event->accept(); }

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
