#include "dumpable.h"

#include <dirent.h>
#include <sys/prctl.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

enum class Tracer { None, Attached, Unknown };

// Состояние одного потока по его status. Поток мог завершиться между readdir и fopen — тогда трассировщика нет.
Tracer tracerOf(const std::string& statusPath)
{
    FILE* f = std::fopen(statusPath.c_str(), "re");
    if (!f)
        return errno == ENOENT || errno == ESRCH ? Tracer::None : Tracer::Unknown;
    Tracer result = Tracer::Unknown;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        static const char kKey[] = "TracerPid:";
        if (std::strncmp(line, kKey, sizeof kKey - 1) != 0)
            continue;
        char* end = nullptr;
        const long pid = std::strtol(line + sizeof kKey - 1, &end, 10);
        if (end != line + sizeof kKey - 1)
            result = pid == 0 ? Tracer::None : Tracer::Attached;
        break;
    }
    std::fclose(f);
    return result;
}

} // namespace

ScopedDumpable::ScopedDumpable()
{
    if (prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) != 0)
        return; // уже дампируемый (PINCH_ALLOW_TRACE=1): окно не нужно
    // Трассировщик, подключённый до PR_SET_DUMPABLE=0 (запуск под strace без PINCH_ALLOW_TRACE), не читает память,
    // пока процесс недампируемый; окно дало бы ему доступ к снимку. Вызов пойдёт без окна (служба откажет).
    if (!noTracerAttached()) {
        m_tracedBefore = true;
        return;
    }
    m_raised = prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == 0;
}

ScopedDumpable::~ScopedDumpable()
{
    restore();
}

bool ScopedDumpable::restore()
{
    if (m_tracedBefore)
        return false;
    if (!m_raised)
        return m_result;
    m_raised = false;
    // Сначала закрыть окно, затем проверить: подключившийся в окне трассировщик остаётся подключённым и виден в status.
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    m_result = noTracerAttached();
    return m_result;
}

bool ScopedDumpable::noTracerAttached()
{
    // Трассировщик подключается к отдельному потоку, а память у потоков общая: проверяются все потоки процесса.
    DIR* dir = ::opendir("/proc/self/task");
    if (!dir)
        return false;
    bool clean = true;
    bool any = false;
    while (const dirent* entry = ::readdir(dir)) {
        if (entry->d_name[0] == '.')
            continue;
        any = true;
        if (tracerOf(std::string("/proc/self/task/") + entry->d_name + "/status") != Tracer::None) {
            clean = false;
            break;
        }
    }
    ::closedir(dir);
    return clean && any;
}
