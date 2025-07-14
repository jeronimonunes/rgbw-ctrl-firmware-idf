#pragma once

#include <freertos/semphr.h>

class LockGuard
{
    SemaphoreHandle_t& sem;

public:
    explicit LockGuard(SemaphoreHandle_t& s) : sem(s) { xSemaphoreTake(sem, portMAX_DELAY); }
    ~LockGuard() { xSemaphoreGive(sem); }
};

// example of a custom lock implementation using FreeRTOS semaphores.
// class MyClass
// {
//
//     void protectedMethod()
//     {
//         LockGuard lock(getMutex());
//     }
//
//     static SemaphoreHandle_t& getMutex()
//     {
//         static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
//         return mutex;
//     }
// };