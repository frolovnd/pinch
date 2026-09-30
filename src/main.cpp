#include "capture.h"
#include "instancelock.h"
#include "output.h"
#include "overlay.h"
#include "settings.h"

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QPointer>
#include <QTranslator>

#include <sys/prctl.h>

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
    // Снимок в памяти не должен попасть в core-дамп или читаться через ptrace другими процессами пользователя.
    // HS_ALLOW_TRACE=1 (ровно «1») — осознанное исключение пользователя для запуска под strace (иначе strace не читает строки).
    if (qgetenv("HS_ALLOW_TRACE") != "1" && prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0)
        qWarning("не удалось отключить дампы памяти (PR_SET_DUMPABLE)");
    // Без платформенной темы Qt не подгружает весь стек GTK/ATK и не ходит за настройками темы.
    QApplication::setDesktopSettingsAware(false);
    // Без SESSION_MANAGER Qt не открывает ICE-соединение с менеджером сессии X11.
    qunsetenv("SESSION_MANAGER");
    // На случай, если какой-то GTK-код всё же загрузится: не поднимать мост AT-SPI.
    qputenv("NO_AT_BRIDGE", "1");

    QApplication app(argc, argv);
    // Стандартные диалоги Qt (Сохранить как, подтверждение перезаписи) — на русском; при неудаче молча остаёмся на английском.
    QTranslator qtTranslator;
    if (qtTranslator.load(QStringLiteral("qtbase_ru"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
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

    auto* overlay = new Overlay(std::move(*capture), Settings::load(), screenshotsDir());

    QObject::connect(overlay, &Overlay::finished, &app, [overlay] {
        saveStyle(*overlay);
        QApplication::quit();
    });
    QObject::connect(overlay, &Overlay::copyRequested, &app, [overlay, &lock](const QImage& image) {
        saveStyle(*overlay);
        lock.release(); // следующий хоткей может открыть новый оверлей, пока мы держим буфер
        copyToClipboard(image);
        // Снимок всего рабочего стола (с копиями) больше не нужен: освобождаем память, пока владеем буфером.
        // deleteLater, а не delete: мы внутри испускания сигнала самого оверлея.
        overlay->deleteLater();
        // На X11 данные буфера отдаёт процесс-владелец: живём, пока буфер не займёт кто-то другой.
        QClipboard* clipboard = QGuiApplication::clipboard();
        QObject::connect(clipboard, &QClipboard::changed, clipboard, [clipboard](QClipboard::Mode mode) {
            if (mode == QClipboard::Clipboard && !clipboard->ownsClipboard())
                QApplication::quit();
        });
    });

    QPointer<Overlay> guard(overlay);
    overlay->start();
    const int rc = app.exec();
    delete guard.data(); // Esc/сохранение: оверлей не удалялся; после deleteLater указатель уже null
    return rc;
}
