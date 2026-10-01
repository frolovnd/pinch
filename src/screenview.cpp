#include "screenview.h"

#include "overlaycontroller.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

ScreenView::ScreenView(OverlayController* controller, QRect imageRect, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_imageRect(imageRect)
{
    resize(m_imageRect.size());
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
}

QPoint ScreenView::toImage(const QPointF& local) const
{
    return local.toPoint() + m_imageRect.topLeft();
}

void ScreenView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.translate(-m_imageRect.topLeft());
    m_controller->paint(painter, m_imageRect);
}

// Окно, получившее нажатие, получает и все движения до отпускания (неявный захват указателя), даже за своими
// пределами: тот же сдвиг переводит их в координаты изображения, и выделение тянется на соседний монитор.
void ScreenView::mousePressEvent(QMouseEvent* event)
{
    m_controller->mousePress(toImage(event->position()), event->button(), event->modifiers());
}

void ScreenView::mouseMoveEvent(QMouseEvent* event)
{
    m_controller->mouseMove(toImage(event->position()), event->buttons(), event->modifiers());
}

void ScreenView::mouseReleaseEvent(QMouseEvent* event)
{
    m_controller->mouseRelease(toImage(event->position()), event->button(), event->modifiers());
}

// Без переопределения Qt вызвал бы mousePressEvent: двойной клик сработал бы как второе нажатие.
void ScreenView::mouseDoubleClickEvent(QMouseEvent* event)
{
    m_controller->mouseDoubleClick(toImage(event->position()));
    event->accept();
}

// Сюда же приходит колесо, пересланное панелью инструментов (она — дочерний виджет окна).
void ScreenView::wheelEvent(QWheelEvent* event)
{
    m_controller->wheel(event->angleDelta().y());
    event->accept();
}

void ScreenView::keyPressEvent(QKeyEvent* event)
{
    m_controller->keyPress(event);
}
