#pragma once

#include <array>

/**
 * Stores <Size> values of <Type> to get their average
 * Every assignment to this class adds a value to the averge
 * and makes the older values get lost
 * Every cast of this class to its original Type will compute
 * the averge and return it
 */
template <typename Type, size_t Size>
class MovingAverage
{
private:
    std::array<Type, Size> values;
    size_t idx = 0;

public:
    [[nodiscard]] constexpr size_t size() const noexcept { return Size; }

    explicit MovingAverage() = default;

    explicit MovingAverage(Type initial) { this->values.fill(initial); }

    MovingAverage& operator+=(const Type& v)
    {
        idx %= values.size();
        values[idx] = v;
        idx++;
        return *this;
    }

    MovingAverage& operator=(const Type& v)
    {
        this->values.fill(v);
        return *this;
    }

    explicit operator Type() const
    {
        Type val = 0;
        for (auto v : values)
        {
            val += v;
        }
        return val /= values.size();
    }
};
