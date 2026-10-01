#pragma once

#include <QPointer>
#include <QRect>
#include <QWidget>

class OverlayController;

// Окно одного монитора: переводит события (local + imageRect.topLeft()) и рисует свою часть кадра.
// Вся логика — в контроллере; он же задаёт окну курсор и просит перерисовку.
class ScreenView : public QWidget {
    Q_OBJECT
public:
    ScreenView(OverlayController* controller, QRect imageRect, QWidget* parent = nullptr);
    QRect imageRect() const { return m_imageRect; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    QPoint toImage(const QPointF& local) const; // локальные координаты окна → координаты изображения

    QPointer<OverlayController> m_controller; // контроллер живёт дольше окон; null — окну нечего делать
    QRect m_imageRect; // часть изображения (монитор), которую показывает окно
};
