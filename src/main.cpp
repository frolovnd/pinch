#include "capture.h"
#include "instancelock.h"
#include "output.h"
#include "overlay.h"
#include "settings.h"

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>

namespace {
void saveStyle(const Overlay& overlay)
{
    Settings settings;
    settings.color = overlay.style().color;
    settings.thickness = overlay.style().thickness;
    settings.save();
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("hot-screenshot"));
    QApplication::setApplicationVersion(QStringLiteral(HS_VERSION));
    // После Ctrl+C окно закрыто, но процесс должен жить, пока владеет буфером обмена.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Скриншот области экрана с рисованием"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    InstanceLock lock;
    const QString lockPath = defaultLockPath();
    if (lockPath.isEmpty()) {
        qWarning("XDG_RUNTIME_DIR не задан — работаю без блокировки экземпляра");
    } else {
        switch (lock.tryAcquire(lockPath)) {
        case InstanceLock::Result::Acquired:
            break;
        case InstanceLock::Result::Busy:
            return 0; // оверлей уже открыт
        case InstanceLock::Result::Error:
            qWarning("не удалось взять блокировку %s — работаю без неё", qPrintable(lockPath));
            break;
        }
    }

    std::optional<Capture> capture = captureAllScreens();
    if (!capture) {
        qCritical("не удалось снять экран");
        return 1;
    }

    Overlay overlay(std::move(*capture), Settings::load(), screenshotsDir());

    QObject::connect(&overlay, &Overlay::finished, &app, [&overlay] {
        saveStyle(overlay);
        QApplication::quit();
    });
    QObject::connect(&overlay, &Overlay::copyRequested, &app, [&overlay, &lock](const QImage& image) {
        saveStyle(overlay);
        lock.release(); // следующий хоткей может открыть новый оверлей, пока мы держим буфер
        copyToClipboard(image);
        // На X11 данные буфера отдаёт процесс-владелец: живём, пока буфер не займёт кто-то другой.
        QClipboard* clipboard = QGuiApplication::clipboard();
        QObject::connect(clipboard, &QClipboard::changed, clipboard, [clipboard](QClipboard::Mode mode) {
            if (mode == QClipboard::Clipboard && !clipboard->ownsClipboard())
                QApplication::quit();
        });
    });

    overlay.start();
    return app.exec();
}
