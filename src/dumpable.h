#pragma once

// Процесс pinch по умолчанию недампируемый (PR_SET_DUMPABLE=0, см. main): снимок не попадает в core-дамп, а другие
// процессы пользователя не читают его память через ptrace. Но KWin и xdg-desktop-portal опознают вызывающего через
// /proc/<pid>/exe и /proc/<pid>/root, а у недампируемого процесса ядро не даёт чужим процессам этих ссылок.
// ScopedDumpable делает процесс дампируемым только на время такого вызова D-Bus.
class ScopedDumpable {
public:
    // PR_SET_DUMPABLE=1, только если процесс сейчас недампируемый. Уже дампируемый (PINCH_ALLOW_TRACE=1) — ничего
    // не делает. Если к недампируемому процессу уже подключён трассировщик, окно не открывается: restore() вернёт false.
    ScopedDumpable();
    ~ScopedDumpable(); // restore(), если ещё не вызван
    ScopedDumpable(const ScopedDumpable&) = delete;
    ScopedDumpable& operator=(const ScopedDumpable&) = delete;

    // Возвращает PR_SET_DUMPABLE=0 и проверяет трассировщики. false — к процессу подключён трассировщик (ptrace),
    // снимок надо прервать. Если окно не открывалось (процесс и так дампируемый), всегда true. Идемпотентно.
    bool restore();

    // true, если ни к одному потоку процесса не подключён трассировщик (TracerPid: 0 в /proc/self/task/*/status).
    // Если это не удаётся прочитать — false (считаем, что подключён).
    static bool noTracerAttached();

private:
    bool m_raised = false;       // окно открыто: процесс сделан дампируемым и ещё не возвращён обратно
    bool m_tracedBefore = false; // трассировщик был подключён ещё до окна
    bool m_result = true;        // итог restore()
};
