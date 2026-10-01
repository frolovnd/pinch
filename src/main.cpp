#include "appicon.h"
#include "capture.h"
#include "capturebackend.h"
#include "i18n.h"
#include "instancelock.h"
#include "output.h"
#include "overlaycontroller.h"
#include "overlaysession.h"
#include "settings.h"

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>

#include <sys/prctl.h>

namespace {
void saveStyle(const OverlayController& controller)
{
    Settings settings;
    settings.color = controller.style().color;
    settings.thickness = controller.style().thickness;
    settings.save();
}
}

int main(int argc, char** argv)
{
    // Снимок в памяти не должен попасть в core-дамп или читаться через ptrace другими процессами пользователя.
    // Исключение — только сами вызовы D-Bus к KWin и порталу: они опознают вызывающего через /proc/<pid>, и на время
    // вызова процесс становится дампируемым (ScopedDumpable, src/dumpable.h).
    // PINCH_ALLOW_TRACE=1 (ровно «1») — осознанное исключение пользователя для запуска под strace (иначе strace не читает строки).
    // Предупреждение выводим позже: до установки переводов qtTrId ещё не умеет находить текст.
    const bool dumpableFailed = qgetenv("PINCH_ALLOW_TRACE") != "1" && prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0;
    // Без платформенной темы Qt не подгружает весь стек GTK/ATK и не ходит за настройками темы.
    QApplication::setDesktopSettingsAware(false);
    // Без SESSION_MANAGER Qt не открывает ICE-соединение с менеджером сессии X11.
    qunsetenv("SESSION_MANAGER");
    // На случай, если какой-то GTK-код всё же загрузится: не поднимать мост AT-SPI.
    qputenv("NO_AT_BRIDGE", "1");

    QApplication app(argc, argv);
    // Язык — по системной локали (английский — запасной); до разбора командной строки, чтобы --help был переведён.
    installTranslations(app, QLocale::system());
    if (dumpableFailed)
        qWarning("%s", qPrintable(qtTrId("log.dumpable")));
    QApplication::setApplicationName(QStringLiteral("pinch"));
    QApplication::setApplicationVersion(QStringLiteral(PINCH_VERSION));
    // Иконка окна по умолчанию: её получают и стандартные диалоги (Сохранить как, сообщения).
    QApplication::setWindowIcon(appIcon());
    // После Ctrl+C окно закрыто, но процесс должен жить, пока владеет буфером обмена.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(qtTrId("app.description"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    InstanceLock lock;
    const QString lockPath = defaultLockPath();
    if (lockPath.isEmpty()) {
        qWarning("%s", qPrintable(qtTrId("log.no_runtime_dir")));
    } else {
        switch (lock.tryAcquire(lockPath)) {
        case InstanceLock::Result::Acquired:
            break;
        case InstanceLock::Result::Busy:
            return 0; // оверлей уже открыт
        case InstanceLock::Result::Error:
            qWarning("%s", qPrintable(qtTrId("log.lock_failed").arg(lockPath)));
            break;
        }
    }

    const CaptureEnvironment environment = detectEnvironment();
    if (xwaylandFallback(environment))
        qWarning("%s", qPrintable(qtTrId("warning.capture.xwayland")));
    QStringList captureErrors;
    CaptureMethod method = CaptureMethod::X11;
    std::optional<Capture> capture = captureScreens(environment, &captureErrors, &method);
    if (!capture) {
        const QString text = qtTrId("error.capture.failed") + QLatin1Char('\n') + captureErrors.join(QLatin1Char('\n'));
        qCritical("%s", qPrintable(text));
        QMessageBox::critical(nullptr, qtTrId("error.capture.title"), text);
        return 1;
    }
    // Какой способ сработал — для ручной проверки (README) и отчётов об ошибках.
    qInfo("%s", qPrintable(qtTrId("log.capture.method").arg(captureMethodName(method))));

    auto* controller = new OverlayController(std::move(*capture), Settings::load(), screenshotsDir());
    auto* session = new OverlaySession(controller);

    QObject::connect(controller, &OverlayController::finished, &app, [controller] {
        saveStyle(*controller);
        QApplication::quit();
    });
    QObject::connect(controller, &OverlayController::copyRequested, &app,
                     [controller, session, &lock](const QImage& image) {
        saveStyle(*controller);
        lock.release(); // следующий хоткей может открыть новый оверлей, пока мы держим буфер
        copyToClipboard(image);
        // Снимок всего рабочего стола (с копиями) больше не нужен: освобождаем память, пока владеем буфером.
        // deleteLater, а не delete: мы внутри испускания сигнала контроллера (после него он ещё скроет окна).
        // Сессия — первой: её окна обращаются к контроллеру.
        session->deleteLater();
        controller->deleteLater();
        // На X11 данные буфера отдаёт процесс-владелец: живём, пока буфер не займёт кто-то другой.
        QClipboard* clipboard = QGuiApplication::clipboard();
        QObject::connect(clipboard, &QClipboard::changed, clipboard, [clipboard](QClipboard::Mode mode) {
            if (mode == QClipboard::Clipboard && !clipboard->ownsClipboard())
                QApplication::quit();
        });
    });

    QPointer<OverlaySession> sessionGuard(session);
    QPointer<OverlayController> controllerGuard(controller);
    session->show();
    const int rc = app.exec();
    // Esc/сохранение: сессия и контроллер не удалялись; после deleteLater указатели уже null. Сессия — первой.
    delete sessionGuard.data();
    delete controllerGuard.data();
    return rc;
}
