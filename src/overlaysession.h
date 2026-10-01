#pragma once

#include <QObject>
#include <QPointer>
#include <QVector>

class OverlayController;
class ScreenView;

// Окна оверлея: по одному на каждый различный прямоугольник capture().screens. Показывает и прячет их по сигналам
// контроллера (X11 — окна поверх всех без оконного менеджера, Wayland — полноэкранные на своих мониторах).
// Удалять раньше контроллера.
class OverlaySession : public QObject {
    Q_OBJECT
public:
    explicit OverlaySession(OverlayController* controller, QObject* parent = nullptr);
    ~OverlaySession() override; // удаляет окна

    void show();
    void hide();
    const QVector<ScreenView*>& views() const { return m_views; }

private:
    QPointer<OverlayController> m_controller;
    QVector<ScreenView*> m_views;
};
