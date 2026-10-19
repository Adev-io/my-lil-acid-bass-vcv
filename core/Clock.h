// Clock.h — where the song position goes, one sample at a time, so the sequencer engine can run without a host transport.
// Internal: the Tempo knob moves it. External: each clock pulse is one 16th step; between pulses it moves at the speed
// the last two pulses set (so swing and gate length still work), and it never passes the next step before that pulse.
// Every tick gives a range [a, b) that starts where the last one ended.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <algorithm>

struct SongClock
{
    double pos = 0.0;          // song position in quarter notes
    long long edges = -1;      // external pulses since reset; -1 = none yet
    double since = 0.0, period = 0.0;   // samples since the last pulse, samples between the last two

    void reset() { pos = 0.0; edges = -1; since = 0.0; }   // the period estimate survives a reset

    void tickInternal (double bpm, double sampleTime, double& a, double& b)
    {
        a = pos; pos += bpm / 60.0 * sampleTime; b = pos;
    }

    // While stopped: keep measuring the pulse spacing, but do not move. The next pulse after Run plays the next step.
    void idleExternal (bool edge)
    {
        since += 1.0;
        if (edge) { if (edges >= 0) period = since; since = 0.0; }
    }

    // False before the first pulse since reset (nothing plays until the clock does). fallbackPeriod is in samples per step.
    bool tickExternal (bool edge, double fallbackPeriod, double& a, double& b)
    {
        since += 1.0;
        if (edge)
        {
            if (edges >= 0) period = since;
            ++edges; since = 0.0;
        }
        if (edges < 0) return false;
        const double per = period > 0.0 ? period : std::max (1.0, fallbackPeriod);
        a = pos;
        const double base = std::max (pos, edges * 0.25);
        b = std::max (a, std::min (base + 0.25 / per, (edges + 1) * 0.25 - 1.0e-9));
        pos = b;
        return true;
    }
};
