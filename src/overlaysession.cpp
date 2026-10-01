#include "overlaysession.h"

#include "overlaycontroller.h"
#include "screenview.h"
#include "toolbar.h"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include <utility>

namespace {
// Монитор окна: тот, чья геометрия в координатах изображения совпадает с прямоугольником окна; иначе основной.
QScreen* screenFor(const QRect& imageRect, QPoint origin)
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        if (screen->geometry().translated(-origin) == imageRect)
            return screen;
    }
    return QGuiApplication::primaryScreen();
}

// Смена флагов пересоздаёт окно; при повторном показе (после диалога) флаги те же — не трогаем.
void setFlags(QWidget* window, Qt::WindowFlags flags)
{
    if (window->windowFlags() != flags)
        window->setWindowFlags(flags);
}
}

OverlaySession::OverlaySession(OverlayController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
{
    QVector<QRect> rects = controller->capture().screens;
    if (rects.isEmpty())
        rects = {controller->capture().image.rect()}; // мониторы неизвестны: одно окно на всё изображение, как раньше
    for (const QRect& rect : std::as_const(rects)) {
        auto* view = new ScreenView(controller, rect);
        controller->attachView(view);
        m_views.append(view);
    }
    connect(controller, &OverlayController::hideRequested, this, &OverlaySession::hide);
    connect(controller, &OverlayController::showRequested, this, &OverlaySession::show);
}

OverlaySession::~OverlaySession()
{
    // Панель — дочерний виджет одного из окон, но принадлежит контроллеру: отцепляем её, пока окна живы.
    if (m_controller) {
        Toolbar* toolbar = m_controller->toolbar();
        for (ScreenView* view : std::as_const(m_views)) {
            if (toolbar && toolbar->parentWidget() == view) {
                toolbar->setParent(nullptr);
                break;
            }
        }
    }
    qDeleteAll(m_views);
}

void OverlaySession::show()
{
    if (!m_controller || m_views.isEmpty())
        return;
    const bool wayland = QGuiApplication::platformName() == QLatin1String("wayland");
    const QPoint origin = m_controller->capture().origin;
    for (ScreenView* view : std::as_const(m_views)) {
        if (wayland) {
            // Окно Wayland нельзя расположить: полноэкранное окно на своём мониторе.
            setFlags(view, Qt::Window | Qt::FramelessWindowHint);
            view->winId(); // создать окно, чтобы задать ему монитор до показа
            view->windowHandle()->setScreen(screenFor(view->imageRect(), origin));
            view->showFullScreen();
        } else {
            // X11 (и offscreen): окно поверх всех без оконного менеджера, ровно по глобальной геометрии монитора.
            setFlags(view, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                               | Qt::X11BypassWindowManagerHint);
            view->setGeometry(view->imageRect().translated(origin));
            view->show();
        }
    }
    for (ScreenView* view : std::as_const(m_views))
        view->raise();

    // Клавиатура — окну под курсором, иначе первому; клавиши из любого окна всё равно идут в контроллер.
    const QPoint cursor = QCursor::pos() - origin;
    ScreenView* active = m_views.constFirst();
    for (ScreenView* view : std::as_const(m_views)) {
        if (view->imageRect().contains(cursor)) {
            active = view;
            break;
        }
    }
    active->activateWindow();
    active->setFocus(Qt::OtherFocusReason);
    if (!wayland)
        active->grabKeyboard();
}

void OverlaySession::hide()
{
    for (ScreenView* view : std::as_const(m_views)) {
        view->releaseKeyboard();
        view->hide();
    }
}
