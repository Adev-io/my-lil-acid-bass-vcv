// AcidVoice.h — the mono acid voice: PolyBLEP saw/square into two cascaded 12 dB lowpasses (24 dB/oct, resonance on the
// second), filter and amp ADSR envelopes, an accent, and a legato glide. Same constants as the web page's `play()` and the
// FL Studio plugin, so all three sound alike.
// Rack runs one sample at a time: setParams() does the per-block work (call it every few samples and after every note
// event), process() makes one sample. render() is the plugin's block call, kept for the tests.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include "Maps.h"
#include "Svf.h"
#include <algorithm>
#include <cmath>
#include <vector>

struct AcidParams
{
    float cutoffHz = 460.0f;   // resting cutoff
    float res = 0.5f;          // 0..1
    float envmod = 0.6f;       // 0..1, depth of the filter envelope (up to 4.5 octaves)
    float decaySec = 0.5f;     // filter decay
    float accent = 0.6f;       // 0..1, how hard an accented note hits
    float volume = 0.7f;       // 0..1 (applied by the caller, after the voice)
    float tuneCents = 0.0f;    // +-100
    bool square = false;       // false = saw
    float hpHz = 20.0f;        // input high-pass (20 = off)
    float keyFollow = 0.0f;    // 0..1, how far the cutoff follows the note (C3 neutral)
    float minDecSec = 0.03f;   // floor for the filter decay
    float pw = 0.5f;           // pulse width fraction (square only)
    float lfoHz = 1.0f;        // LFO rate
    float lfoPw = 0.0f;        // 0..1, LFO depth on the pulse width
    float fAtkSec = 0.002f, fSus = 0.0f, fRelSec = 0.2940f;        // filter attack, sustain (fraction of the env depth), release
    float aAtkSec = 0.002f, aDecSec = 0.2733f, aSus = 1.0f, aRelSec = 0.0060f;   // amp ADSR; the numbers are the web defaults
};

class AcidVoice
{
public:
    AcidVoice() { held.reserve (16); }   // here, not in prepare(): Rack calls prepare() from the audio thread

    void prepare (double sr)
    {
        sampleRate = sr;
        for (Svf* f : { &f1, &f2, &hp }) { f->prepare (sr); f->setType (Svf::Lowpass); }
        hp.setType (Svf::Highpass); hp.setResonance (0.7071f);
        f1.setResonance (0.5f);
        glideCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));   // ~60 ms to settle
        reset();
    }

    void reset()
    {
        held.clear(); gate = false; phase = 0.0; freq = targetFreq = 110.0; ampEnv = 0.0f; filtEnv = 0.0f; sweep = 0.0f;
        filtAtkInc = 0.0f; ampAtkCoef = 0.0f; ampAtkLeft = 0.0; filtRising = false;
        f1.reset(); f2.reset(); hp.reset(); lfoPhase = 0.0; keyMulNow = 1.0f; tick = 0;
    }

    bool active() const { return gate || ampEnv > 1.0e-4f; }

    // A note that arrives while another is held glides to the new pitch without restarting the envelope (the 303 slide).
    void noteOn (int note, float velocity01, const AcidParams& p)
    {
        const bool legato = ! held.empty();
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        held.push_back (note);
        targetFreq = freqOf (note, p.tuneCents);
        accented = velocity01 >= 110.0f / 127.0f;
        if (legato) return;
        keyMulNow = (float) acidmaps::keyMul (p.keyFollow, note);
        freq = targetFreq;
        sweep = accented ? std::min (1.0f, sweep + 0.35f) : sweep * 0.5f;
        filtEnv = 0.0f; filtRising = true;
        filtAtkInc = 1.0f / (std::max (p.fAtkSec, 0.001f) * (float) sampleRate);
        const float aa = std::max (p.aAtkSec, 0.002f);
        ampAtkLeft = aa * sampleRate; ampAtkCoef = 1.0f - std::exp (-1.0f / (aa / 4.0f * (float) sampleRate));
        gate = true;
    }

    void noteOff (int note, const AcidParams& p)
    {
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        if (held.empty()) gate = false;
        else targetFreq = freqOf (held.back(), p.tuneCents);   // back to the note still held, gliding
    }

    void allOff() { held.clear(); gate = false; }

    void setParams (const AcidParams& p)
    {
        cur = p;
        const float decSec = std::max (accented ? std::min (p.decaySec, 0.2f) : p.decaySec, p.minDecSec);
        decCoef = std::exp (-1.0f / (std::max (decSec, 0.005f) / 3.0f * (float) sampleRate));
        fRelCoef = std::exp (-1.0f / (std::max (p.fRelSec, 0.002f) / 3.0f * (float) sampleRate));
        aDecCoef = 1.0f - std::exp (-1.0f / (std::max (p.aDecSec, 0.005f) / 3.0f * (float) sampleRate));
        aRelCoef = 1.0f - std::exp (-1.0f / (std::max (p.aRelSec, 0.002f) / 3.0f * (float) sampleRate));
        const float acc = accented ? p.accent : 0.0f;
        oct = p.envmod * 4.5f + acc * 1.6f + sweep * p.accent * 0.8f;
        level = 0.45f * (1.0f + acc * 0.9f) / (1.0f + p.res * 0.6f);   // resonance eats level, as on the hardware
        f2.setResonance (0.7f + std::pow (p.res, 1.6f) * 24.0f);
        maxHz = 0.45f * (float) sampleRate;
        hp.setCutoffFrequency (std::min (p.hpHz, 0.45f * (float) sampleRate));
        lfoInc = p.lfoHz / sampleRate;
        tick = 0;
    }

    float process()
    {
        if ((tick++ & 7) == 0)   // the filter coefficients move every 8 samples: plenty for a sweep, cheap enough to run live
        {
            const float hz = std::min (cur.cutoffHz * keyMulNow * std::pow (2.0f, oct * filtEnv), maxHz);
            f1.setCutoffFrequency (hz); f2.setCutoffFrequency (hz);
        }
        freq += (targetFreq - freq) * (double) glideCoef;
        const double dt = freq / sampleRate;
        phase += dt; if (phase >= 1.0) phase -= 1.0;
        float s = sawBlep (phase, dt);
        if (cur.square)
        {   // the web's pulse: the saw minus a copy of itself shifted by the width (zero-mean, narrow = skewed)
            lfoPhase += lfoInc; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            const double w = (double) cur.pw + 0.4 * (double) cur.lfoPw * std::sin (6.283185307179586 * lfoPhase);
            double shifted = phase - w; shifted -= std::floor (shifted);
            s -= sawBlep (shifted, dt);
        }
        s = f2.processSample (f1.processSample (hp.processSample (s)));
        if (gate)
        {
            if (ampAtkLeft > 0.0) { ampEnv += (level - ampEnv) * ampAtkCoef; ampAtkLeft -= 1.0; }
            else                  { ampEnv += (level * cur.aSus - ampEnv) * aDecCoef; }
            if (filtRising) { filtEnv += filtAtkInc; if (filtEnv >= 1.0f) { filtEnv = 1.0f; filtRising = false; } }
            else            { filtEnv = cur.fSus + (filtEnv - cur.fSus) * decCoef; }
        }
        else { ampEnv -= ampEnv * aRelCoef; filtEnv *= fRelCoef; filtRising = false; }
        return s * ampEnv;   // volume and the soft clip are the caller's
    }

    void render (float* out, int n, const AcidParams& p)
    {
        setParams (p);
        for (int i = 0; i < n; ++i) out[i] = process();
    }

private:
    static float polyBlep (double t, double dt)
    {
        if (t < dt)       { t /= dt;           return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }
    static float sawBlep (double t, double dt) { return (float) (2.0 * t - 1.0) - polyBlep (t, dt); }
    static double freqOf (int note, float cents) { return 440.0 * std::pow (2.0, (note - 69 + cents / 100.0) / 12.0); }

    Svf f1, f2, hp;
    std::vector<int> held;
    AcidParams cur;
    double sampleRate = 44100.0, phase = 0.0, freq = 110.0, targetFreq = 110.0, lfoPhase = 0.0, lfoInc = 0.0;
    float glideCoef = 0.0f, filtAtkInc = 0.0f, ampAtkCoef = 0.0f, ampEnv = 0.0f, filtEnv = 0.0f, sweep = 0.0f, keyMulNow = 1.0f;
    float decCoef = 0.0f, fRelCoef = 0.0f, aDecCoef = 0.0f, aRelCoef = 0.0f, oct = 0.0f, level = 0.0f, maxHz = 20000.0f;
    double ampAtkLeft = 0.0;
    unsigned tick = 0;
    bool gate = false, accented = false, filtRising = false;
};
