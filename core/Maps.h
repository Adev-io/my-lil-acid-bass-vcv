// Maps.h — the web app's knob maps, drive curves and reverb impulse response, in C++. Every formula mirrors acidbass.js
// (and the smoke test checks them against numbers computed from it), so the plugin and the web app agree.
// Knob values are normalised 0..1 on both sides.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace acidmaps
{
inline double hpHz (double k)       { return 20.0 * std::pow (25.0, k); }
inline double minDecSec (double k)  { return 0.03 * std::pow (20.0, k); }
inline double pwFrac (double k)     { return 0.05 + 0.9 * k; }
inline double lfoHz (double k)      { return 0.05 * std::pow (400.0, k); }
inline double keyMul (double k, int midi) { return std::pow (2.0, k * (midi - 48) / 12.0); }   // C3 is neutral
inline double drHpHz (double k)     { return 20.0 * std::pow (40.0, k); }
inline double drDb (double k)       { return 30.0 * k; }
inline double drToneHz (double k)   { return 800.0 * std::pow (20.0, k); }
inline double drVolDb (double k)    { return (k - 0.5) * 24.0; }
inline double dbGain (double db)    { return std::pow (10.0, db / 20.0); }
inline double driveMakeupDb (double db) { return 0.0 - 0.4 * db; }
inline double rvPreSec (double k)   { return 0.2 * k; }
inline double rvDecSec (double k)   { return 0.3 * std::pow (20.0, k); }
inline double rvSizeScale (double k) { return 0.5 + k; }
inline double rvLowHz (double k)    { return 20.0 * std::pow (25.0, k); }
inline double rvHighHz (double k)   { return 1000.0 * std::pow (20.0, k); }
inline double dToneHz (double k)    { return 400.0 * std::pow (50.0, k); }
inline double delaySec (double k)   { return 0.02 * std::pow (50.0, k); }
inline double delayFb (double k)    { return 0.9 * k; }
inline double atkSec (double k)    { return 0.002 * std::pow (750.0, k); }
inline double envDecSec (double k) { return 0.03 * std::pow (83.0, k); }
inline double relSec (double k)    { return 0.005 * std::pow (400.0, k); }
inline double gateFrac (double k)  { return 0.1 + 0.9 * k; }

enum class DriveType { Soft = 0, Tube = 1, Wavefold = 2 };
constexpr double driveRange = 1.5;
constexpr int driveCurveLen = 2048;

inline double driveShape (DriveType t, double x)
{
    if (t == DriveType::Tube)     return std::tanh (x + 0.35) - std::tanh (0.35);   // biased tanh: asymmetric, zero at zero
    if (t == DriveType::Wavefold) return std::sin (x * 1.3);
    return std::tanh (x);
}

// A lookup table over input -1..1 (which stands for -driveRange..+driveRange shape units), like the web WaveShaper's curve.
inline std::vector<float> driveCurve (DriveType t)
{
    std::vector<float> c ((size_t) driveCurveLen);
    for (int i = 0; i < driveCurveLen; ++i) c[(size_t) i] = (float) driveShape (t, driveRange * (2.0 * i / (driveCurveLen - 1) - 1.0));
    return c;
}

// Read the table the way a Web Audio WaveShaper does: clamp to -1..1, then interpolate linearly.
inline float shapeLookup (const std::vector<float>& c, float x)
{
    x = std::min (1.0f, std::max (-1.0f, x));
    const float pos = (x + 1.0f) * 0.5f * (float) (c.size() - 1);
    const size_t i = std::min ((size_t) pos, c.size() - 2);
    const float f = pos - (float) i;
    return c[i] + (c[i + 1] - c[i]) * f;
}

// mulberry32, the same generator as the web core's rng(seed): same seed in, same numbers out.
struct Rng
{
    uint32_t a;
    explicit Rng (uint32_t seed) : a (seed) {}
    double next()
    {
        a += 0x6d2b79f5u;
        uint32_t t = a;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return (double) (t ^ (t >> 14)) / 4294967296.0;
    }
};

struct StereoIR { std::vector<float> L, R; };

// Seeded noise with an exponential decay (-60 dB at the decay time), a fade-in set by Size, and a one-pole lowpass that
// closes as the tail ages (Hi damp). size, decay, damp are the 0..1 knob values.
inline StereoIR reverbIR (double sr, uint32_t seed, double size, double decay, double damp)
{
    const double rt = rvDecSec (decay), fade = 0.005 + 0.04 * size;
    const size_t len = (size_t) std::min (std::ceil (sr * rt), sr * 7.0);
    StereoIR out { std::vector<float> (len), std::vector<float> (len) };
    for (int c = 0; c < 2; ++c)
    {
        Rng r (seed * 2u + (uint32_t) c);
        double y = 0.0;
        auto& dst = c == 0 ? out.L : out.R;
        for (size_t i = 0; i < len; ++i)
        {
            const double t = (double) i / sr, env = std::exp (-6.9 * t / rt) * std::min (1.0, t / fade);
            const double a = 1.0 - damp * 0.9 * (1.0 - std::exp (-4.0 * t / rt));
            y += a * ((r.next() * 2.0 - 1.0) * env - y);
            dst[i] = (float) y;
        }
    }
    return out;
}
}
