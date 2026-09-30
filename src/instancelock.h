#pragma once

#include <QString>

// Не даёт запустить второй оверлей, пока открыт первый (иначе второй снимет наш же оверлей).
// Блокировка — flock на файле; снимается при закрытии дескриптора, в том числе при падении процесса.
class InstanceLock {
public:
    enum class Result { Acquired, Busy, Error };

    InstanceLock() = default;
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    // O_NOFOLLOW: симлинк на месте файла блокировки — ошибка, а не переход по нему.
    Result tryAcquire(const QString& path);
    void release(); // идемпотентно
    bool isHeld() const { return m_fd >= 0; }

private:
    int m_fd = -1;
};

// $XDG_RUNTIME_DIR/pinch.lock (каталог 0700, только для пользователя);
// пустая строка, если переменная не задана.
QString defaultLockPath();
