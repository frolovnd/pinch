#include <QPainter>
#include <QtTest>

#include "i18n.h"
#include "screencopy.h"

#include <wayland-client.h>

#include <cerrno>
#include <cstdarg>

Q_DECLARE_METATYPE(NamedScreen)

namespace {

const QRgb kMarker = qRgb(0xff, 0, 0); // метка верхней строки кадра у поддельного композитора (XRGB 0x00ff0000)
const QRgb kFillA = qRgb(0x11, 0x22, 0x33);
const QRgb kFillB = qRgb(0x44, 0x55, 0x66);

// Ошибки протокола здесь ожидаемы (режим protoerror): libwayland-client не должен сорить в stderr теста.
void silentWaylandLog(const char*, va_list) {}

// Ожидаемый снимок одного выхода: заливка и строка-метка сверху (или снизу при y_invert).
QImage outputImage(QSize size, QRgb fill, QRgb marker = kMarker, bool markerAtBottom = false)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(fill);
    const int row = markerAtBottom ? size.height() - 1 : 0;
    for (int x = 0; x < size.width(); ++x)
        image.setPixel(x, row, marker);
    return image;
}

// Запоминает переменную окружения и восстанавливает её в деструкторе.
class EnvGuard {
public:
    explicit EnvGuard(const char* name) : m_name(name), m_wasSet(qEnvironmentVariableIsSet(name)), m_value(qgetenv(name)) {}
    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;
    ~EnvGuard()
    {
        if (m_wasSet)
            qputenv(m_name, m_value);
        else
            qunsetenv(m_name);
    }

private:
    const char* m_name;
    bool m_wasSet;
    QByteArray m_value;
};

// Поддельный композитор в отдельном процессе со своим XDG_RUNTIME_DIR (0700) и сокетом. Пока объект жив,
// клиентская сторона (этот процесс) подключается к нему; затем окружение восстанавливается.
class FakeCompositor {
public:
    FakeCompositor() = default;
    FakeCompositor(const FakeCompositor&) = delete;
    FakeCompositor& operator=(const FakeCompositor&) = delete;
    ~FakeCompositor()
    {
        m_process.kill();
        m_process.waitForFinished(5000);
    }

    bool start(const QString& mode)
    {
        if (!m_runtime.isValid()
            || !QFile::setPermissions(m_runtime.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
            return false;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), m_runtime.path());
        m_process.setProcessEnvironment(env);
        m_process.start(QString::fromUtf8(FAKE_COMPOSITOR_PATH), {QString::fromLatin1(kSocket), mode});
        if (!m_process.waitForStarted(5000))
            return false;
        QByteArray out;
        while (!out.contains('\n') && m_process.waitForReadyRead(5000))
            out += m_process.readAllStandardOutput();
        if (out != "ready\n")
            return false;
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(m_runtime.path()));
        qputenv("WAYLAND_DISPLAY", kSocket);
        qunsetenv("WAYLAND_SOCKET");
        return true;
    }

    QString diagnostics() { return QString::fromLocal8Bit(m_process.readAllStandardError()); }

private:
    static constexpr const char* kSocket = "pinch-fake";
    EnvGuard m_xdg{"XDG_RUNTIME_DIR"};
    EnvGuard m_display{"WAYLAND_DISPLAY"};
    EnvGuard m_socket{"WAYLAND_SOCKET"};
    QTemporaryDir m_runtime;
    QProcess m_process;
};

} // namespace

// Клиент screencopy против поддельного композитора: успешные снимки и все отказы, включая враждебные ответы.
class TestScreencopyFake : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // Тексты ошибок сравниваются с qtTrId(...), поэтому язык зафиксирован (не зависит от LANG).
        installTranslations(*QCoreApplication::instance(), QLocale(QStringLiteral("en_US")));
        wl_log_set_handler_client(silentWaylandLog);
    }

    void capture_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QVector<NamedScreen>>("screens");
        QTest::addColumn<int>("timeoutMs");
        QTest::addColumn<bool>("available");
        QTest::addColumn<QImage>("expected");      // пусто — ожидается ошибка
        QTest::addColumn<QVector<QRect>>("rects"); // Capture::screens при успехе
        QTest::addColumn<QString>("error");
        QTest::addColumn<bool>("errorIsPrefix");

        const QVector<NamedScreen> one = {{QStringLiteral("OUT-A"), QRect(0, 0, 64, 48)}};
        const QVector<QRect> oneRect = {QRect(0, 0, 64, 48)};
        const int t = SCREENCOPY_TIMEOUT_MS;
        const QString frameFailed = qtTrId("error.capture.screencopy.frame_failed");
        const QString badBuffer = qtTrId("error.capture.screencopy.bad_buffer");
        const QString unsupported = qtTrId("error.capture.screencopy.unsupported");

        QTest::newRow("normal") << "ok" << one << t << true << outputImage({64, 48}, kFillA) << oneRect << QString() << false;
        QTest::newRow("y_invert") << "yinvert" << one << t << true << outputImage({64, 48}, kFillA, kMarker, true) << oneRect
                                  << QString() << false;
        // XBGR: байты 0x00112233 читаются как R=0x33, G=0x22, B=0x11; метка 0x00ff0000 — как синий.
        QTest::newRow("xbgr") << "xbgr" << one << t << true
                              << outputImage({64, 48}, qRgb(0x33, 0x22, 0x11), qRgb(0, 0, 0xff)) << oneRect << QString()
                              << false;
        QTest::newRow("hidpi buffer scaled to logical") << "hidpi" << one << t << true << outputImage({64, 48}, kFillA)
                                                        << oneRect << QString() << false;

        // Два выхода; мониторы перечислены в обратном порядке — сопоставление по имени.
        QImage two(96, 48, QImage::Format_RGB32);
        two.fill(Qt::black);
        {
            QPainter p(&two);
            p.drawImage(0, 0, outputImage({64, 48}, kFillA));
            p.drawImage(64, 0, outputImage({32, 16}, kFillB));
        }
        QTest::newRow("two outputs by name")
            << "twoout"
            << QVector<NamedScreen>{{QStringLiteral("OUT-B"), QRect(64, 0, 32, 16)}, {QStringLiteral("OUT-A"), QRect(0, 0, 64, 48)}}
            << t << true << two << QVector<QRect>{QRect(0, 0, 64, 48), QRect(64, 0, 32, 16)} << QString() << false;

        QTest::newRow("duplicate buffer event ignored") << "dupbuffer" << one << t << true << outputImage({64, 48}, kFillA)
                                                        << oneRect << QString() << false;
        QTest::newRow("ready before copy") << "readyfirst" << one << t << true << QImage() << QVector<QRect>() << frameFailed
                                           << false;
        QTest::newRow("failed frame") << "fail" << one << t << true << QImage() << QVector<QRect>() << frameFailed << false;
        QTest::newRow("hang") << "hang" << one << 300 << true << QImage() << QVector<QRect>()
                              << qtTrId("error.capture.wayland.timeout").arg(QString::number(300 / 1000.0)) << false;
        QTest::newRow("oversized buffer") << "bigbuf" << one << t << true << QImage() << QVector<QRect>() << badBuffer << false;
        QTest::newRow("bad format") << "badformat" << one << t << true << QImage() << QVector<QRect>() << badBuffer << false;
        QTest::newRow("no manager") << "nomanager" << one << t << false << QImage() << QVector<QRect>() << unsupported << false;
        QTest::newRow("no wl_shm") << "noshm" << one << t << true << QImage() << QVector<QRect>() << unsupported << false;
        // Причина (EPIPE или ECONNRESET) зависит от момента разрыва — проверяем только начало текста.
        QTest::newRow("disconnect") << "disconnect" << one << t << true << QImage() << QVector<QRect>()
                                    << qtTrId("error.capture.wayland.connection_error").arg(QString()) << true;
        QTest::newRow("protocol error") << "protoerror" << one << t << true << QImage() << QVector<QRect>()
                                        << qtTrId("error.capture.wayland.connection_error").arg(qt_error_string(EPROTO))
                                        << false;
        QTest::newRow("17 outputs") << "manyoutputs" << QVector<NamedScreen>() << t << true << QImage() << QVector<QRect>()
                                    << qtTrId("error.capture.screencopy.too_many_outputs").arg(SCREENCOPY_MAX_OUTPUTS)
                                    << false;
    }

    void capture()
    {
        QFETCH(QString, mode);
        QFETCH(QVector<NamedScreen>, screens);
        QFETCH(int, timeoutMs);
        QFETCH(bool, available);
        QFETCH(QImage, expected);
        QFETCH(QVector<QRect>, rects);
        QFETCH(QString, error);
        QFETCH(bool, errorIsPrefix);

        FakeCompositor fake;
        QVERIFY2(fake.start(mode), qPrintable(fake.diagnostics()));
        QCOMPARE(screencopyAvailable(), available);

        QString actualError;
        QElapsedTimer timer;
        timer.start();
        const auto c = captureWithScreencopy(screens, &actualError, timeoutMs);
        QVERIFY(timer.elapsed() < timeoutMs + 2000);

        if (expected.isNull()) {
            QVERIFY(!c.has_value());
            if (errorIsPrefix)
                QVERIFY2(actualError.startsWith(error), qPrintable(actualError));
            else
                QCOMPARE(actualError, error);
            return;
        }
        QVERIFY2(c.has_value(), qPrintable(actualError));
        QCOMPARE(c->image.format(), QImage::Format_RGB32);
        QCOMPARE(c->image.size(), expected.size());
        QCOMPARE(c->image, expected);
        QCOMPARE(c->origin, QPoint(0, 0));
        QCOMPARE(c->screens, rects);
    }
};

QTEST_GUILESS_MAIN(TestScreencopyFake)
#include "test_screencopy_fake.moc"
