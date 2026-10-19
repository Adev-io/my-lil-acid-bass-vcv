// Svf.h — topology-preserving-transform state-variable filter (Zavalishin), mono, lowpass or highpass output.
// Same equations, defaults and resonance meaning (R2 = 1 / resonance) as juce::dsp::StateVariableTPTFilter, so the voice
// filters exactly as the FL Studio build does.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <cmath>

class Svf
{
public:
    enum Type { Lowpass, Highpass };

    void prepare (double sr) { sampleRate = sr; update(); reset(); }
    void reset() { s1 = s2 = 0.0f; }
    void setType (Type t) { type = t; }
    void setCutoffFrequency (float hz) { cutoff = hz; update(); }
    void setResonance (float r) { resonance = r; update(); }

    float processSample (float x)
    {
        const float yHP = h * (x - s1 * (g + R2) - s2);
        const float yBP = yHP * g + s1;
        s1 = yHP * g + yBP;
        const float yLP = yBP * g + s2;
        s2 = yBP * g + yLP;
        return type == Lowpass ? yLP : yHP;
    }

private:
    void update()
    {
        g = (float) std::tan (3.141592653589793 * cutoff / sampleRate);
        R2 = (float) (1.0 / resonance);
        h = (float) (1.0 / (1.0 + R2 * g + g * g));
    }

    Type type = Lowpass;
    double sampleRate = 44100.0;
    float cutoff = 1000.0f, resonance = 0.70710678f;
    float g = 0.0f, R2 = 0.0f, h = 0.0f, s1 = 0.0f, s2 = 0.0f;
};
