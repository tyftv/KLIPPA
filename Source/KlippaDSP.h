#pragma once
#include <cmath>
#include <algorithm>

// Pure C++ clip curve, no JUCE dependency so it can be unit tested.
// y = x / (1 + |x|^p)^(1/p)  -> never exceeds +/-1.
// p high = hard knee, p low = round knee. curve: 0 = hard, 1 = soft.
namespace klippa
{
    inline float curveToP (float curve) noexcept
    {
        curve = std::min (1.0f, std::max (0.0f, curve));
        return 1.6f * std::pow (25.0f, 1.0f - curve); // 1.6 (soft) .. 40 (hard)
    }

    inline float clip (float x, float p) noexcept
    {
        if (p >= 39.9f)
            return std::max (-1.0f, std::min (1.0f, x)); // true hard clip

        const float a = std::fabs (x);
        float y;
        if (a <= 1.0f)
            y = a / std::pow (1.0f + std::pow (a, p), 1.0f / p);
        else // overflow-safe form: a / (a * (1 + a^-p)^(1/p))
            y = 1.0f / std::pow (1.0f + std::pow (1.0f / a, p), 1.0f / p);

        y = std::min (1.0f, y); // guarantee: never above ceiling
        return std::copysign (y, x);
    }
}
