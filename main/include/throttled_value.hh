#pragma once

#include <mutex>

template <typename T>
class ThrottledValue
{
    T lastValue;
    unsigned long lastSendTime = 0;
    const unsigned long throttleInterval;

    std::mutex mutex;

public:
    explicit ThrottledValue(const unsigned long intervalMs)
        : throttleInterval(intervalMs)
    {
    }

    bool shouldSend(const unsigned long now, const T& newValue)
    {
        if (!mutex.try_lock())
            return false;

        if (now - lastSendTime < throttleInterval || newValue == lastValue)
        {
            mutex.unlock();
            return false;
        }
        mutex.unlock();
        return true;
    }

    void setLastSent(const unsigned long time, const T& value)
    {
        std::lock_guard lock(mutex);
        this->lastValue = value;
        this->lastSendTime = time;
    }
};
