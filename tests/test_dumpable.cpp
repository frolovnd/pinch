#include <QtTest>

#include "dumpable.h"
#include "testtracer.h"

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <thread>

namespace {

int dumpable()
{
    return prctl(PR_GET_DUMPABLE, 0, 0, 0, 0);
}

void setDumpable(int value)
{
    prctl(PR_SET_DUMPABLE, value, 0, 0, 0);
}

pid_t currentTid()
{
    return static_cast<pid_t>(syscall(SYS_gettid));
}

} // namespace

class TestDumpable : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // Можно ли вообще трассировать этот процесс (ptrace_scope ≥ 2, seccomp в контейнере и т. п. запрещают).
        setDumpable(1);
        TestTracer probe;
        m_ptraceWorks = probe.attach(::getpid());
    }

    void cleanup() { setDumpable(1); }

    void noTracerInNormalRun() { QVERIFY(ScopedDumpable::noTracerAttached()); }

    void raisesAndRestores()
    {
        setDumpable(0);
        {
            ScopedDumpable guard;
            QCOMPARE(dumpable(), 1);
            QVERIFY(guard.restore());
            QCOMPARE(dumpable(), 0);
            QVERIFY(guard.restore()); // идемпотентно
            QCOMPARE(dumpable(), 0);
        }
        QCOMPARE(dumpable(), 0);
        {
            ScopedDumpable guard; // без restore(): возвращает деструктор
            QCOMPARE(dumpable(), 1);
        }
        QCOMPARE(dumpable(), 0);
    }

    void noOpWhenAlreadyDumpable()
    {
        // PINCH_ALLOW_TRACE=1: процесс уже дампируемый — окно не нужно и не закрывается.
        setDumpable(1);
        ScopedDumpable guard;
        QCOMPARE(dumpable(), 1);
        QVERIFY(guard.restore());
        QCOMPARE(dumpable(), 1);
    }

    void detectsTracer()
    {
        if (!m_ptraceWorks)
            QSKIP("ptrace is not permitted in this environment", "");
        TestTracer tracer;
        QVERIFY(tracer.attach(::getpid()));
        QVERIFY(!ScopedDumpable::noTracerAttached());
        tracer.release();
        QVERIFY(ScopedDumpable::noTracerAttached());
    }

    void detectsTracerOnAnotherThread()
    {
        if (!m_ptraceWorks)
            QSKIP("ptrace is not permitted in this environment", "");
        // Поток ждёт на канале; трассировщик подключается только к нему, не к главному потоку.
        int fds[2];
        QCOMPARE(::pipe2(fds, O_CLOEXEC), 0);
        std::atomic<pid_t> tid{0};
        std::thread worker([&tid, fd = fds[0]] {
            tid = currentTid();
            char c = 0;
            while (::read(fd, &c, 1) < 0 && errno == EINTR) {
            }
        });
        while (tid == 0)
            std::this_thread::yield();
        TestTracer tracer;
        const bool attached = tracer.attach(tid);
        const bool detected = !ScopedDumpable::noTracerAttached();
        tracer.release();
        ::close(fds[1]);
        worker.join();
        ::close(fds[0]);
        QVERIFY(attached);
        QVERIFY(detected);
        QVERIFY(ScopedDumpable::noTracerAttached());
    }

    void restoreReportsTracerAttachedInWindow()
    {
        if (!m_ptraceWorks)
            QSKIP("ptrace is not permitted in this environment", "");
        setDumpable(0);
        ScopedDumpable guard;
        QCOMPARE(dumpable(), 1);
        TestTracer tracer;
        QVERIFY(tracer.attach(::getpid())); // подключение возможно только в окне
        QVERIFY(!guard.restore());
        QCOMPARE(dumpable(), 0);
        tracer.release();
    }

    void noWindowForAlreadyTracedProcess()
    {
        if (!m_ptraceWorks)
            QSKIP("ptrace is not permitted in this environment", "");
        // Трассировщик подключился до PR_SET_DUMPABLE=0 (pinch запущен под strace без PINCH_ALLOW_TRACE): память ему
        // недоступна, пока процесс недампируемый, — окно не открывается, снимок прерывается.
        setDumpable(1);
        TestTracer tracer;
        QVERIFY(tracer.attach(::getpid()));
        setDumpable(0);
        ScopedDumpable guard;
        QCOMPARE(dumpable(), 0);
        QVERIFY(!guard.restore());
        QCOMPARE(dumpable(), 0);
        tracer.release();
    }

private:
    bool m_ptraceWorks = false;
};

QTEST_APPLESS_MAIN(TestDumpable)
#include "test_dumpable.moc"
