#pragma once

#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Дочерний процесс-трассировщик: подключается к потоку этого процесса через PTRACE_SEIZE (поток не останавливается)
// и держит подключение до release(). При ptrace_scope=1 (Yama) разрешение даёт PR_SET_PTRACER.
class TestTracer {
public:
    TestTracer() = default;
    TestTracer(const TestTracer&) = delete;
    TestTracer& operator=(const TestTracer&) = delete;
    ~TestTracer() { release(); }

    // false — подключиться не удалось (процесс недампируемый или ptrace запрещён окружением).
    bool attach(pid_t target)
    {
        int toChild[2];
        int toParent[2];
        if (::pipe2(toChild, O_CLOEXEC) != 0)
            return false;
        if (::pipe2(toParent, O_CLOEXEC) != 0) {
            ::close(toChild[0]);
            ::close(toChild[1]);
            return false;
        }
        m_pid = ::fork();
        if (m_pid == 0) {
            // Потомок: только async-signal-safe вызовы. Свой экземпляр пишущего конца закрываем, иначе EOF не придёт.
            ::close(toChild[1]);
            ::close(toParent[0]);
            char c = 0;
            if (::read(toChild[0], &c, 1) != 1)
                ::_exit(1);
            const char ok = ::ptrace(PTRACE_SEIZE, target, nullptr, nullptr) == 0 ? '1' : '0';
            if (::write(toParent[1], &ok, 1) != 1)
                ::_exit(1);
            // Ждём, пока отпустят (EOF), но не дольше 10 с: выход потомка снимает трассировку в любом случае.
            pollfd p{toChild[0], POLLIN, 0};
            ::poll(&p, 1, 10000);
            ::_exit(0);
        }
        ::close(toChild[0]);
        ::close(toParent[1]);
        m_release = toChild[1];
        bool ok = false;
        if (m_pid > 0) {
            ::prctl(PR_SET_PTRACER, m_pid, 0, 0, 0); // без Yama — EINVAL, это не важно
            char c = 'g';
            ok = ::write(m_release, &c, 1) == 1 && ::read(toParent[0], &c, 1) == 1 && c == '1';
        }
        ::close(toParent[0]);
        return ok;
    }

    void release()
    {
        if (m_release >= 0) {
            ::close(m_release);
            m_release = -1;
        }
        if (m_pid > 0) {
            ::waitpid(m_pid, nullptr, 0);
            m_pid = -1;
            ::prctl(PR_SET_PTRACER, 0, 0, 0, 0);
        }
    }

private:
    pid_t m_pid = -1;
    int m_release = -1;
};
