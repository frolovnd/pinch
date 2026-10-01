// Поддельный композитор Wayland для test_screencopy_fake: wl_shm (встроенный в libwayland-server), wl_output v4
// с именами и zwlr_screencopy_manager_v1 v3. Только для тестов; в pinch не входит.
// Запуск: fake_compositor <имя сокета> <режим>; сокет создаётся в $XDG_RUNTIME_DIR, затем в stdout пишется «ready».
// Режимы: ok, yinvert, xbgr, hidpi, twoout, manyoutputs, fail, hang, bigbuf, badformat, nomanager, noshm,
// disconnect, protoerror, dupbuffer, readyfirst, rot90, badtransform.
#include "wlr-screencopy-unstable-v1-server-protocol.h"

#include <wayland-server.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string g_mode;

// Метка верхних строк кадра (XRGB, красный) — по ней тест проверяет y_invert и порядок строк.
constexpr uint32_t kMarker = 0x00ff0000;
// Метка левого верхнего пикселя буфера (XRGB, синий) — только в режиме rot90: по ней тест проверяет поворот целиком.
constexpr uint32_t kCorner = 0x000000ff;

struct OutInfo {
    std::string name;
    int width;  // размер режима (буфера), не логический
    int height;
    uint32_t fill; // XRGB8888
    int32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
};
std::vector<OutInfo> g_outputs;

void outputRelease(wl_client*, wl_resource* resource)
{
    wl_resource_destroy(resource);
}
const struct wl_output_interface kOutputImpl = {outputRelease};

void bindOutput(wl_client* client, void* data, uint32_t version, uint32_t id)
{
    auto* info = static_cast<OutInfo*>(data);
    wl_resource* r = wl_resource_create(client, &wl_output_interface, int(version), id);
    if (!r) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &kOutputImpl, info, nullptr);
    wl_output_send_geometry(r, 0, 0, 300, 200, WL_OUTPUT_SUBPIXEL_UNKNOWN, "fake", "fake", info->transform);
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT, info->width, info->height, 60000);
    if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
        wl_output_send_scale(r, 1);
    if (version >= WL_OUTPUT_NAME_SINCE_VERSION)
        wl_output_send_name(r, info->name.c_str());
    if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
        wl_output_send_done(r);
}

// Параметры буфера, объявленные клиенту для кадра.
struct FrameInfo {
    const OutInfo* output;
    int width;
    int height;
    int stride;
    uint32_t format;
};

void frameCopy(wl_client* client, wl_resource* frame, wl_resource* buffer)
{
    const auto* info = static_cast<FrameInfo*>(wl_resource_get_user_data(frame));
    if (g_mode == "hang")
        return;
    if (g_mode == "fail") {
        zwlr_screencopy_frame_v1_send_failed(frame);
        return;
    }
    if (g_mode == "disconnect") {
        wl_client_destroy(client);
        return;
    }
    if (g_mode == "protoerror") {
        wl_resource_post_error(frame, ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER, "fake protocol error");
        return;
    }
    wl_shm_buffer* shm = wl_shm_buffer_get(buffer);
    if (!shm || wl_shm_buffer_get_width(shm) != info->width || wl_shm_buffer_get_height(shm) != info->height
        || wl_shm_buffer_get_stride(shm) != info->stride) {
        wl_resource_post_error(frame, ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER, "buffer does not match");
        return;
    }
    // Метка занимает столько строк буфера, сколько их приходится на одну логическую строку (HiDPI).
    const int markerRows = info->height / info->output->height;
    wl_shm_buffer_begin_access(shm);
    auto* data = static_cast<uint8_t*>(wl_shm_buffer_get_data(shm));
    for (int y = 0; y < info->height; ++y) {
        for (int x = 0; x < info->width; ++x) {
            uint32_t v = y < markerRows ? kMarker : info->output->fill;
            if (g_mode == "rot90" && x == 0 && y == 0)
                v = kCorner;
            std::memcpy(data + y * info->stride + x * 4, &v, 4);
        }
    }
    wl_shm_buffer_end_access(shm);
    zwlr_screencopy_frame_v1_send_flags(frame, g_mode == "yinvert" ? ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT : 0);
    zwlr_screencopy_frame_v1_send_ready(frame, 0, 1, 0);
}

void frameDestroy(wl_client*, wl_resource* resource)
{
    wl_resource_destroy(resource);
}

void frameCopyWithDamage(wl_client* client, wl_resource* frame, wl_resource* buffer)
{
    frameCopy(client, frame, buffer);
}

const struct zwlr_screencopy_frame_v1_interface kFrameImpl = {frameCopy, frameDestroy, frameCopyWithDamage};

void frameResourceDestroyed(wl_resource* resource)
{
    delete static_cast<FrameInfo*>(wl_resource_get_user_data(resource));
}

void captureOutput(wl_client* client, wl_resource* manager, uint32_t id, int32_t, wl_resource* output)
{
    const auto* out = static_cast<const OutInfo*>(wl_resource_get_user_data(output));
    wl_resource* frame =
        wl_resource_create(client, &zwlr_screencopy_frame_v1_interface, wl_resource_get_version(manager), id);
    if (!frame) {
        wl_client_post_no_memory(client);
        return;
    }
    auto* info = new FrameInfo{out, out->width, out->height, out->width * 4 + 16, WL_SHM_FORMAT_XRGB8888};
    if (g_mode == "hidpi") {
        info->width = out->width * 2;
        info->height = out->height * 2;
        info->stride = info->width * 4;
    } else if (g_mode == "xbgr") {
        info->format = WL_SHM_FORMAT_XBGR8888;
    } else if (g_mode == "badformat") {
        info->format = WL_SHM_FORMAT_RGB565;
    }
    wl_resource_set_implementation(frame, &kFrameImpl, info, frameResourceDestroyed);

    if (g_mode == "readyfirst") { // нарушение порядка: ready до buffer и copy
        zwlr_screencopy_frame_v1_send_ready(frame, 0, 0, 0);
        return;
    }
    if (g_mode == "bigbuf") { // размер больше любого экрана и int32 протокола wl_shm
        zwlr_screencopy_frame_v1_send_buffer(frame, info->format, 40000, 40000, 160000);
        return;
    }
    zwlr_screencopy_frame_v1_send_buffer(frame, info->format, uint32_t(info->width), uint32_t(info->height),
                                         uint32_t(info->stride));
    if (g_mode == "dupbuffer") // повтор события buffer с другими параметрами — клиент должен его игнорировать
        zwlr_screencopy_frame_v1_send_buffer(frame, info->format, 4000, 4000, 16000);
    if (wl_resource_get_version(frame) >= ZWLR_SCREENCOPY_FRAME_V1_BUFFER_DONE_SINCE_VERSION)
        zwlr_screencopy_frame_v1_send_buffer_done(frame);
}

void captureOutputRegion(wl_client* client, wl_resource*, uint32_t, int32_t, wl_resource*, int32_t, int32_t, int32_t,
                         int32_t)
{
    wl_client_post_implementation_error(client, "capture_output_region is not supported by the fake");
}

void managerDestroy(wl_client*, wl_resource* resource)
{
    wl_resource_destroy(resource);
}

const struct zwlr_screencopy_manager_v1_interface kManagerImpl = {captureOutput, captureOutputRegion, managerDestroy};

void bindManager(wl_client* client, void*, uint32_t version, uint32_t id)
{
    wl_resource* r = wl_resource_create(client, &zwlr_screencopy_manager_v1_interface, int(version), id);
    if (!r) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &kManagerImpl, nullptr, nullptr);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: fake_compositor <socket> <mode>\n");
        return 2;
    }
    g_mode = argv[2];

    if (g_mode == "twoout") {
        g_outputs = {{"OUT-A", 64, 48, 0x00112233}, {"OUT-B", 32, 16, 0x00445566}};
    } else if (g_mode == "manyoutputs") {
        for (int i = 0; i < 17; ++i)
            g_outputs.push_back({"OUT-" + std::to_string(i), 8, 8, 0x00112233});
    } else if (g_mode == "rot90") {
        // Буфер 64×48 в ориентации монитора; на экране (логически) выход повёрнут: 48×64.
        g_outputs = {{"OUT-A", 64, 48, 0x00112233, WL_OUTPUT_TRANSFORM_90}};
    } else if (g_mode == "badtransform") { // значение вне wl_output_transform (0–7)
        g_outputs = {{"OUT-A", 64, 48, 0x00112233, 9}};
    } else {
        g_outputs = {{"OUT-A", 64, 48, 0x00112233}};
    }

    wl_display* display = wl_display_create();
    if (!display || wl_display_add_socket(display, argv[1]) != 0) {
        std::perror("fake_compositor: add_socket");
        return 1;
    }
    if (g_mode != "noshm") {
        wl_display_init_shm(display); // ARGB8888 и XRGB8888
        wl_display_add_shm_format(display, WL_SHM_FORMAT_XBGR8888);
        wl_display_add_shm_format(display, WL_SHM_FORMAT_RGB565);
    }
    for (OutInfo& out : g_outputs) // g_outputs больше не меняется: адреса стабильны
        wl_global_create(display, &wl_output_interface, 4, &out, bindOutput);
    if (g_mode != "nomanager")
        wl_global_create(display, &zwlr_screencopy_manager_v1_interface, 3, nullptr, bindManager);

    std::printf("ready\n");
    std::fflush(stdout);
    wl_display_run(display); // до завершения процесса тестом
    wl_display_destroy(display);
    return 0;
}
