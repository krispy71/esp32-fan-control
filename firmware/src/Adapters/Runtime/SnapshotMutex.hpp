#pragma once
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#else
#include <mutex>
#endif

namespace SmokerController::Adapters::Runtime {
// For bounded memory copies only. Never hold this across I/O or radio operations.
class SnapshotMutex {
public:
    void lock() noexcept {
#ifdef ARDUINO
        portENTER_CRITICAL(&mux_);
#else
        mutex_.lock();
#endif
    }
    void unlock() noexcept {
#ifdef ARDUINO
        portEXIT_CRITICAL(&mux_);
#else
        mutex_.unlock();
#endif
    }
private:
#ifdef ARDUINO
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
#else
    std::mutex mutex_;
#endif
};
class SnapshotLock {
public:
    explicit SnapshotLock(SnapshotMutex& mutex) noexcept : mutex_(mutex) { mutex_.lock(); }
    ~SnapshotLock() { mutex_.unlock(); }
    SnapshotLock(const SnapshotLock&) = delete;
    SnapshotLock& operator=(const SnapshotLock&) = delete;
private:
    SnapshotMutex& mutex_;
};
}
