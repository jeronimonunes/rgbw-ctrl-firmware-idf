#pragma once

#include <cstdint>
#include <functional>

void async_call(std::function<void()> callback, uint32_t usStackDepth, uint32_t delayMs);
