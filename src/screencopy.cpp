#include "screencopy.h"

#include <QStringList>
#include <QtGlobal>

#ifdef PINCH_HAVE_WAYLAND

#include "wlr-screencopy-unstable-v1-client-protocol.h"

#include <wayland-client.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Срок ожидания: момент и исходная длительность (для текста ошибки).
struct Deadline {
    explicit Deadline(int ms) : at(Clock::now() + std::chrono::milliseconds(ms)), ms(ms) {}
    Clock::time_point at;
    int ms;
};

// unique_ptr для объектов libwayland: Destroy — функция освобождения объекта.
template <typename T, void (*Destroy)(T*)>
struct WlDeleter {
    void operator()(T* p) const { Destroy(p); }
};
template <typename T, void (*Destroy)(T*)>
using WlPtr = std::unique_ptr<T, WlDeleter<T, Destroy>>;

// wl_output версии 3+ освобождается запросом release, более старые — только на стороне клиента.
void releaseOutput(wl_output* output)
{
    if (wl_output_get_version(output) >= WL_OUTPUT_RELEASE_SINCE_VERSION)
        wl_output_release(output);
    else
        wl_output_destroy(output);
}

// Владение файловым дескриптором.
class UniqueFd {
public:
    UniqueFd() = default;
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    ~UniqueFd() { reset(-1); }

    void reset(int fd)
    {
        if (m_fd >= 0)
            ::close(m_fd);
        m_fd = fd;
    }
    int get() const { return m_fd; }

private:
    int m_fd = -1;
};

// Владение отображением памяти (mmap/munmap).
class Mapping {
public:
    Mapping() = default;
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;
    ~Mapping()
    {
        if (m_data != MAP_FAILED)
            ::munmap(m_data, m_size);
    }

    // false — mmap не удался (errno сохранён).
    bool map(int fd, size_t size)
    {
        void* data = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED)
            return false;
        m_data = data;
        m_size = size;
        return true;
    }
    const uchar* data() const { return m_data == MAP_FAILED ? nullptr : static_cast<const uchar*>(m_data); }
    qsizetype size() const { return qsizetype(m_size); }

private:
    void* m_data = MAP_FAILED;
    size_t m_size = 0;
};

struct Output {
    WlPtr<wl_output, releaseOutput> proxy;
    QString name; // wl_output.name (v4); пусто, если композитор его не прислал
    int transform = WL_OUTPUT_TRANSFORM_NORMAL; // wl_output.geometry: ориентация буфера относительно экрана
};

// Один кадр screencopy: объект кадра, memfd с отображением и wl_buffer поверх него.
struct Frame {
    enum class State { Pending, Ready, Failed };

    wl_shm* shm = nullptr; // принадлежит Session, живёт дольше кадра
    WlPtr<zwlr_screencopy_frame_v1, zwlr_screencopy_frame_v1_destroy> proxy;
    UniqueFd fd;
    Mapping mapping;
    WlPtr<wl_buffer, wl_buffer_destroy> buffer;
    quint32 format = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
    bool yInvert = false;
    bool bufferAnnounced = false;
    State state = State::Pending;
    QString error;

    void fail(const QString& text)
    {
        state = State::Failed;
        error = text;
    }
};

// Соединение и все объекты. Порядок полей важен: уничтожаются в обратном порядке, wl_display — последним.
struct Session {
    WlPtr<wl_display, wl_display_disconnect> display;
    WlPtr<wl_registry, wl_registry_destroy> registry;
    WlPtr<wl_shm, wl_shm_destroy> shm;
    WlPtr<zwlr_screencopy_manager_v1, zwlr_screencopy_manager_v1_destroy> manager;
    std::vector<std::unique_ptr<Output>> outputs; // не больше SCREENCOPY_MAX_OUTPUTS
    std::vector<std::unique_ptr<Frame>> frames;
    bool bindObjects = false;      // false — только проверить наличие менеджера (screencopyAvailable)
    bool managerAnnounced = false; // композитор объявил zwlr_screencopy_manager_v1
    int outputsAnnounced = 0;      // сколько wl_output объявлено (в т. ч. сверх лимита)
};

bool setError(QString* error, const QString& text)
{
    if (error)
        *error = text;
    return false;
}

// Ошибка соединения: код из wl_display (если он есть), иначе переданный errno.
bool connectionError(wl_display* display, QString* error, int fallbackErrno)
{
    const int code = wl_display_get_error(display);
    return setError(error, qtTrId("error.capture.wayland.connection_error").arg(qt_error_string(code ? code : fallbackErrno)));
}

bool timeoutError(QString* error, const Deadline& deadline)
{
    return setError(error, qtTrId("error.capture.wayland.timeout").arg(QString::number(deadline.ms / 1000.0)));
}

// Обрабатывает события, пока done() не вернёт true. Никогда не ждёт дольше deadline.
// false — тайм-аут или ошибка соединения (текст в error).
template <typename Done>
bool dispatchUntil(wl_display* display, const Deadline& deadline, Done done, QString* error)
{
    for (;;) {
        while (wl_display_prepare_read(display) != 0) {
            if (wl_display_dispatch_pending(display) < 0)
                return connectionError(display, error, errno);
        }
        if (done()) {
            wl_display_cancel_read(display);
            return true;
        }
        // Отправить накопленные запросы; EAGAIN — сокет занят, допишем, когда станет доступен для записи.
        const int flushed = wl_display_flush(display);
        if (flushed < 0 && errno != EAGAIN) {
            const int err = errno;
            wl_display_cancel_read(display);
            return connectionError(display, error, err);
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline.at - Clock::now()).count();
        if (left <= 0) {
            wl_display_cancel_read(display);
            return timeoutError(error, deadline);
        }
        pollfd pfd = {};
        pfd.fd = wl_display_get_fd(display);
        pfd.events = short(POLLIN | (flushed < 0 ? POLLOUT : 0));
        const int ready = ::poll(&pfd, 1, int(left));
        if (ready < 0) {
            const int err = errno;
            wl_display_cancel_read(display);
            if (err == EINTR)
                continue;
            return connectionError(display, error, err);
        }
        if (ready == 0) {
            wl_display_cancel_read(display);
            return timeoutError(error, deadline);
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_read_events(display) < 0)
                return connectionError(display, error, errno);
        } else {
            wl_display_cancel_read(display);
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                return connectionError(display, error, EPIPE);
        }
        if (wl_display_dispatch_pending(display) < 0)
            return connectionError(display, error, errno);
    }
}

void onSyncDone(void* data, wl_callback*, uint32_t)
{
    *static_cast<bool*>(data) = true;
}

const wl_callback_listener kSyncListener = {onSyncDone};

// Аналог wl_display_roundtrip, но с тайм-аутом: ждём ответа на wl_display.sync.
bool roundtrip(wl_display* display, const Deadline& deadline, QString* error)
{
    bool done = false;
    WlPtr<wl_callback, wl_callback_destroy> callback(wl_display_sync(display));
    if (!callback)
        return connectionError(display, error, ENOMEM);
    wl_callback_add_listener(callback.get(), &kSyncListener, &done);
    return dispatchUntil(display, deadline, [&done] { return done; }, error);
}

// --- wl_output: нужны имя (событие name, v4) и поворот (transform из geometry) ---

void onOutputGeometry(void* data, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*,
                      int32_t transform)
{
    static_cast<Output*>(data)->transform = transform;
}
void onOutputMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void onOutputDone(void*, wl_output*) {}
void onOutputScale(void*, wl_output*, int32_t) {}
void onOutputName(void* data, wl_output*, const char* name)
{
    static_cast<Output*>(data)->name = QString::fromUtf8(name);
}
void onOutputDescription(void*, wl_output*, const char*) {}

const wl_output_listener kOutputListener = {onOutputGeometry, onOutputMode, onOutputDone,
                                            onOutputScale,    onOutputName, onOutputDescription};

// --- Реестр ---

void onGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    auto* s = static_cast<Session*>(data);
    if (version < 1)
        return; // некорректное объявление
    if (std::strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        s->managerAnnounced = true;
        // Достаточно версии 1: buffer (wl_shm) гарантирован, buffer_done не нужен.
        if (s->bindObjects && !s->manager)
            s->manager.reset(static_cast<zwlr_screencopy_manager_v1*>(
                wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1)));
        return;
    }
    if (!s->bindObjects)
        return;
    if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        if (!s->shm)
            s->shm.reset(static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1)));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        ++s->outputsAnnounced;
        if (s->outputs.size() >= size_t(SCREENCOPY_MAX_OUTPUTS))
            return; // сверх лимита не привязываем; снимок завершится ошибкой
        auto output = std::make_unique<Output>();
        output->proxy.reset(static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min<uint32_t>(4, version))));
        if (!output->proxy)
            return;
        wl_output_add_listener(output->proxy.get(), &kOutputListener, output.get());
        s->outputs.push_back(std::move(output));
    }
}

void onGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kRegistryListener = {onGlobal, onGlobalRemove};

// --- Кадр screencopy ---

void onFrameBuffer(void* data, zwlr_screencopy_frame_v1* proxy, uint32_t format, uint32_t width, uint32_t height,
                   uint32_t stride)
{
    auto* f = static_cast<Frame*>(data);
    // Протокол обещает одно событие buffer; повторы и события после завершения игнорируем.
    if (f->bufferAnnounced || f->state != Frame::State::Pending)
        return;
    f->bufferAnnounced = true;

    // Недоверие к композитору: размеры в пределах экрана, stride не меньше строки, всё в int32 протокола wl_shm.
    const quint64 size = quint64(stride) * height;
    if (width == 0 || height == 0 || width > quint32(MAX_SCREEN_DIMENSION) || height > quint32(MAX_SCREEN_DIMENSION)
        || quint64(stride) < quint64(width) * 4 || size > quint64(INT32_MAX)) {
        f->fail(qtTrId("error.capture.screencopy.bad_buffer"));
        return;
    }

    // Запрет уменьшения memfd: иначе композитор мог бы обрезать файл, и чтение отображения дало бы SIGBUS.
    f->fd.reset(memfd_create("pinch-screencopy", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    if (f->fd.get() < 0 || ftruncate(f->fd.get(), off_t(size)) != 0
        || fcntl(f->fd.get(), F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL) != 0 || !f->mapping.map(f->fd.get(), size_t(size))) {
        const int err = errno;
        f->fail(qtTrId("error.capture.screencopy.alloc_failed").arg(qt_error_string(err)));
        return;
    }
    {
        // Пул нужен только для создания буфера: память живёт, пока жив wl_buffer.
        WlPtr<wl_shm_pool, wl_shm_pool_destroy> pool(wl_shm_create_pool(f->shm, f->fd.get(), int32_t(size)));
        if (pool)
            f->buffer.reset(wl_shm_pool_create_buffer(pool.get(), 0, int32_t(width), int32_t(height), int32_t(stride), format));
    }
    if (!f->buffer) {
        f->fail(qtTrId("error.capture.screencopy.alloc_failed").arg(qt_error_string(ENOMEM)));
        return;
    }
    f->format = format;
    f->width = int(width);
    f->height = int(height);
    f->stride = int(stride);
    zwlr_screencopy_frame_v1_copy(proxy, f->buffer.get());
}

void onFrameFlags(void* data, zwlr_screencopy_frame_v1*, uint32_t flags)
{
    static_cast<Frame*>(data)->yInvert = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

void onFrameReady(void* data, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t)
{
    auto* f = static_cast<Frame*>(data);
    if (f->state != Frame::State::Pending)
        return;
    if (f->buffer)
        f->state = Frame::State::Ready;
    else
        f->fail(qtTrId("error.capture.screencopy.frame_failed")); // ready без нашего copy — нарушение протокола
}

void onFrameFailed(void* data, zwlr_screencopy_frame_v1*)
{
    auto* f = static_cast<Frame*>(data);
    if (f->state == Frame::State::Pending)
        f->fail(qtTrId("error.capture.screencopy.frame_failed"));
}

void onFrameDamage(void*, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t, uint32_t) {}
void onFrameLinuxDmabuf(void*, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t) {}
void onFrameBufferDone(void*, zwlr_screencopy_frame_v1*) {} // v3: игнорируем, copy уже отправлен по buffer

const zwlr_screencopy_frame_v1_listener kFrameListener = {onFrameBuffer, onFrameFlags,       onFrameReady,     onFrameFailed,
                                                          onFrameDamage, onFrameLinuxDmabuf, onFrameBufferDone};

// Подключение и реестр. false — нет соединения или ошибка обмена (текст в error).
bool openSession(Session& s, const Deadline& deadline, int roundtrips, QString* error)
{
    s.display.reset(wl_display_connect(nullptr));
    if (!s.display)
        return setError(error, qtTrId("error.capture.wayland.no_connection"));
    s.registry.reset(wl_display_get_registry(s.display.get()));
    if (!s.registry)
        return connectionError(s.display.get(), error, ENOMEM);
    wl_registry_add_listener(s.registry.get(), &kRegistryListener, &s);
    for (int i = 0; i < roundtrips; ++i)
        if (!roundtrip(s.display.get(), deadline, error))
            return false;
    return true;
}

} // namespace

bool screencopyAvailable()
{
    Session s;
    return openSession(s, Deadline(SCREENCOPY_TIMEOUT_MS), 1, nullptr) && s.managerAnnounced;
}

std::optional<Capture> captureWithScreencopy(const QVector<NamedScreen>& screens, QString* error, int timeoutMs)
{
    const auto fail = [error](const QString& text) {
        setError(error, text);
        return std::nullopt;
    };
    const Deadline deadline(timeoutMs);

    Session s;
    s.bindObjects = true;
    // Первый обмен — глобальные объекты, второй — события привязанных wl_output (в т. ч. name).
    if (!openSession(s, deadline, 2, error))
        return std::nullopt;
    if (!s.manager || !s.shm)
        return fail(qtTrId("error.capture.screencopy.unsupported"));
    if (s.outputsAnnounced > SCREENCOPY_MAX_OUTPUTS)
        return fail(qtTrId("error.capture.screencopy.too_many_outputs").arg(SCREENCOPY_MAX_OUTPUTS));

    for (const auto& output : s.outputs) {
        auto frame = std::make_unique<Frame>();
        frame->shm = s.shm.get();
        frame->proxy.reset(zwlr_screencopy_manager_v1_capture_output(s.manager.get(), 0 /*без курсора*/, output->proxy.get()));
        if (!frame->proxy) {
            connectionError(s.display.get(), error, ENOMEM);
            return std::nullopt;
        }
        zwlr_screencopy_frame_v1_add_listener(frame->proxy.get(), &kFrameListener, frame.get());
        s.frames.push_back(std::move(frame));
    }

    // Ждём, пока все кадры будут готовы; первый неудачный кадр прекращает ожидание.
    const auto finished = [&s] {
        bool allReady = true;
        for (const auto& f : s.frames) {
            if (f->state == Frame::State::Failed)
                return true;
            allReady = allReady && f->state == Frame::State::Ready;
        }
        return allReady;
    };
    if (!dispatchUntil(s.display.get(), deadline, finished, error))
        return std::nullopt;

    QStringList names;
    QVector<QImage> images;
    for (size_t i = 0; i < s.frames.size(); ++i) {
        const Frame& f = *s.frames[i];
        if (f.state == Frame::State::Failed)
            return fail(f.error);
        const auto image = imageFromShm(f.mapping.data(), f.mapping.size(), f.width, f.height, f.stride, f.format, f.yInvert);
        if (!image)
            return fail(qtTrId("error.capture.screencopy.bad_buffer"));
        // Кадр — в ориентации буфера выхода; повёрнутый монитор приводим к экранной ориентации до склейки.
        const QImage oriented = applyOutputTransform(*image, s.outputs[i]->transform);
        if (oriented.isNull())
            return fail(qtTrId("error.capture.screencopy.bad_buffer"));
        images << oriented;
        names << s.outputs[i]->name;
    }

    const QVector<QRect> geometries = matchScreens(names, screens);
    if (geometries.isEmpty() || geometries.size() != images.size())
        return fail(qtTrId("error.capture.screencopy.no_match"));
    QVector<ScreenShot> shots;
    for (int i = 0; i < images.size(); ++i)
        shots.append({images[i], geometries[i]});
    Capture capture = composeScreens(shots);
    if (capture.image.isNull())
        return fail(qtTrId("error.capture.screencopy.no_match"));
    return capture;
}

#else // PINCH_HAVE_WAYLAND

bool screencopyAvailable()
{
    return false;
}

std::optional<Capture> captureWithScreencopy(const QVector<NamedScreen>&, QString* error, int)
{
    if (error)
        *error = qtTrId("error.capture.wayland.not_built");
    return std::nullopt;
}

#endif // PINCH_HAVE_WAYLAND
