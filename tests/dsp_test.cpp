#include "../Source/KlippaDSP.h"
#include <cstdio>
int main()
{
    int fails = 0;
    for (float c = 0.f; c <= 1.0f; c += 0.1f)
    {
        float p = klippa::curveToP (c);
        for (float x = -50.f; x <= 50.f; x += 0.01f)
        {
            float y = klippa::clip (x, p);
            if (std::fabs (y) > 1.0f) { std::printf ("FAIL >1 c=%f x=%f y=%f\n", c, x, y); ++fails; }
            if (klippa::clip (-x, p) != -y)   { std::printf ("FAIL odd c=%f x=%f\n", c, x); ++fails; }
        }
        if (std::fabs (klippa::clip (0.05f, p) - 0.05f) > 0.01f) { std::printf ("FAIL linear c=%f\n", c); ++fails; }
        std::printf ("curve %.1f p=%.2f  f(0.5)=%.3f f(1)=%.3f f(2)=%.3f\n", c, p,
                     klippa::clip (0.5f, p), klippa::clip (1.f, p), klippa::clip (2.f, p));
    }
    std::printf (fails ? "FAILED\n" : "ALL OK\n");
    return fails ? 1 : 0;
}
