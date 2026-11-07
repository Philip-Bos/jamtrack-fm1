/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: the math the audio's RAM code uses (PLAT_RAM_CODE may call nothing in the flash, so all of it inline):
 * fast approximations, good to a fraction of a percent where it is heard (cutoffs, decays, saturation). */
#pragma once
#include <stdint.h>

#define RMI static inline __attribute__((always_inline))

typedef union { float f; uint32_t u; int32_t i; } rm_bits_t;

RMI float rm_fabs(float x)
{
    rm_bits_t b;
    b.f = x;
    b.u &= 0x7FFFFFFFu;
    return b.f;
}
RMI float rm_max(float a, float b) { return a > b ? a : b; }
RMI float rm_min(float a, float b) { return a < b ? a : b; }
RMI float rm_clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
RMI float rm_flush(float x) { return rm_fabs(x) < 1e-20f ? 0.0f : x; }

RMI float rm_exp2(float x)               /* 2^x, x clamped to [-126, 126]; about 2e-7 relative */
{
    rm_bits_t b;
    int32_t i;
    float f, p;
    x = rm_clamp(x, -126.0f, 126.0f);
    i = (int32_t)x;
    if ((float)i > x)
        i--;
    f = x - (float)i;
    p = 1.0f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    b.u = (uint32_t)(i + 127) << 23;
    return p * b.f;
}
RMI float rm_log2(float x)               /* x > 0; about 1e-4 */
{
    rm_bits_t b;
    float m, e;
    b.f = x;
    e = (float)((int32_t)((b.u >> 23) & 255u) - 127);
    b.u = (b.u & 0x7FFFFFu) | 0x3F800000u;
    m = b.f;                             /* [1, 2) */
    return e + (((-0.056570851f * m + 0.44717955f) * m - 1.4699568f) * m + 2.8212026f) * m - 1.7417939f;
}
RMI float rm_pow(float a, float x) { return a <= 0.0f ? 0.0f : rm_exp2(x * rm_log2(a)); }
RMI float rm_tanh(float x)               /* a soft clip shaped like tanh, exact 1 beyond 3 */
{
    float x2;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
RMI float rm_tan(float x)                /* tan for 0 .. 1.2 (a filter's prewarp up to 17 kHz at 44.1) */
{
    float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}
