#pragma once

#include "annotation.h"
#include "capture.h"
#include "document.h"
#include "geometry.h"
#include "settings.h"

#include <QImage>
#include <optional>
#include <QString>
#include <QWidget>

class Toolbar;

// Окно поверх всех мониторов: выделение области, рисование и вывод результата.
class Overlay : public QWidget {
    Q_OBJECT
public:
    Overlay(Capture capture, Settings settings, QString saveDir, QWidget* parent = nullptr);

    void start(); // показать, поднять и захватить клавиатуру
    QRect selection() const { return m_selection; }
    Tool tool() const { return m_tool; }
    Style style() const { return m_style; }
    const Document& document() const { return m_document; }
    Toolbar* toolbar() const { return m_toolbar; }
    bool isEditingText() const { return m_textEditing; }
    QImage result() const; // выделенная область со всеми аннотациями

signals:
    void copyRequested(const QImage& result);
    void finished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class Drag { None, Selecting, Moving, Resizing, Drawing };

    void setSelection(const QRect& selection);
    void setTool(Tool tool);
    void setColor(const QColor& color);
    void setThickness(int thickness);
    void undo();
    void redo();
    void copyResult();
    void saveQuick();
    void saveAs();
    void cancel();
    void closeOverlay(); // отпустить клавиатуру и спрятать окно
    void showError(const QString& text);
    void documentChanged();
    void updateToolbar();
    void updateCursor(QPoint pos);
    QRect screenAt(QPoint pos) const;
    QRect bounds() const;
    void beginAnnotation(QPoint pos);
    void finishAnnotation();
    void commitText();                    // завершить ввод текста; пустой текст отбрасывается
    bool handleTextKey(QKeyEvent* event); // true — клавиша поглощена вводом текста
    void paintCurrent(QPainter& painter) const;

    void paintSelectionFrame(QPainter& painter) const;
    void paintSizeLabel(QPainter& painter) const;
    void paintHint(QPainter& painter) const;

    Capture m_capture;
    QImage m_dimmed; // снимок с затемнением — фон вне выделения
    QString m_saveDir;
    Style m_style;
    Tool m_tool = Tool::None;
    Document m_document;
    QRect m_selection;
    QRect m_hintScreen; // монитор под курсором, пока выделения нет
    Toolbar* m_toolbar = nullptr;

    Drag m_drag = Drag::None;
    Handle m_handle = Handle::None;
    QPoint m_pressPos;
    QRect m_selectionAtPress;
    std::optional<Annotation> m_current; // рисуемая аннотация или вводимый текст
    bool m_textEditing = false;
    int m_wheelAccumulator = 0; // накопленные единицы колеса (120 = один шаг)

    mutable QImage m_cache; // render() для текущего выделения и документа
    mutable bool m_cacheValid = false;
};
