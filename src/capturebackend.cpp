#include "capturebackend.h"

#include <QCoreApplication>
#include <QGuiApplication>

QString captureMethodName(CaptureMethod m)
{
    switch (m) {
    case CaptureMethod::X11: return QStringLiteral("x11");
    case CaptureMethod::Screencopy: return QStringLiteral("screencopy");
    case CaptureMethod::KWin: return QStringLiteral("kwin");
    case CaptureMethod::Portal: return QStringLiteral("portal");
    }
    return {};
}

QVector<CaptureMethod> captureOrder(const CaptureEnvironment& env)
{
    if (!env.forced.isEmpty()) {
        for (CaptureMethod m : {CaptureMethod::X11, CaptureMethod::Screencopy, CaptureMethod::KWin, CaptureMethod::Portal})
            if (captureMethodName(m) == env.forced)
                return {m};
        return {};
    }
    if (env.platformName != QLatin1String("wayland"))
        return {CaptureMethod::X11};
    QVector<CaptureMethod> order;
    if (env.hasScreencopy)
        order << CaptureMethod::Screencopy;
    if (env.hasKWinScreenShot2)
        order << CaptureMethod::KWin;
    order << CaptureMethod::Portal;
    return order;
}

CaptureEnvironment detectEnvironment()
{
    CaptureEnvironment env;
    env.platformName = QGuiApplication::platformName();
    env.forced = QString::fromLocal8Bit(qgetenv("PINCH_CAPTURE")).trimmed().toLower();
    return env;
}

std::optional<Capture> captureScreensWith(const QVector<CaptureMethod>& order,
                                          const QMap<CaptureMethod, CaptureFn>& fns, QStringList* errors)
{
    for (CaptureMethod m : order) {
        QString error;
        const auto fn = fns.value(m);
        std::optional<Capture> c;
        if (fn)
            c = fn(&error);
        else
            error = qtTrId("error.capture.unavailable_in_build");
        if (c)
            return c;
        if (errors)
            *errors << captureMethodName(m) + QStringLiteral(": ") + (error.isEmpty() ? qtTrId("error.capture.unknown") : error);
    }
    return std::nullopt;
}

std::optional<Capture> captureScreens(const CaptureEnvironment& env, QStringList* errors)
{
    const QVector<CaptureMethod> order = captureOrder(env);
    if (order.isEmpty()) {
        if (errors)
            *errors << qtTrId("error.capture.unknown_method").arg(env.forced);
        return std::nullopt;
    }
    QMap<CaptureMethod, CaptureFn> fns;
    fns[CaptureMethod::X11] = [](QString* error) {
        auto c = captureAllScreens();
        if (!c)
            *error = qtTrId("error.capture.qt_grab");
        return c;
    };
    // Задачи 3–5 добавляют сюда Screencopy, KWin, Portal.
    return captureScreensWith(order, fns, errors);
}
