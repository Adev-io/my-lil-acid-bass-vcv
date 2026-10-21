// test.cpp — checks the core against the FL Studio plugin's smoke test: the same reference numbers and event timings.
// Author: Ari Klopfer / A_Development · MIT
#include "AcidVoice.h"
#include "Clock.h"
#include "Maps.h"
#include "Sequencer.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sr = 44100.0;
constexpr int block = 512;
int failures = 0;

void check (bool ok, const char* what, double got = 0.0)
{
    std::printf ("%s  %s  (%g)\n", ok ? "ok  " : "FAIL", what, got);
    if (! ok) ++failures;
}
bool near (double a, double b, double tol = 1e-6) { return std::abs (a - b) <= tol * std::max (1.0, std::abs (b)); }

struct Note { int at; bool on; int note; int vel; };

// The plugin's processBlock: 512-sample blocks, the voice rendered in segments split at each MIDI event,
// then master volume and the tanh soft clip.
std::vector<float> renderBlocks (const AcidParams& p, double seconds, const std::vector<Note>& notes)
{
    AcidVoice v; v.prepare (sr);
    const int total = (int) (seconds * sr);
    std::vector<float> out ((size_t) total + block, 0.0f);
    for (int start = 0; start < total; start += block)
    {
        int pos = 0;
        for (const Note& n : notes)
        {
            if (n.at < start || n.at >= start + block) continue;
            const int at = n.at - start;
            if (at > pos) v.render (&out[(size_t) (start + pos)], at - pos, p);
            pos = at;
            if (n.on) v.noteOn (n.note, n.vel / 127.0f, p); else v.noteOff (n.note, p);
        }
        if (pos < block) v.render (&out[(size_t) (start + pos)], block - pos, p);
    }
    out.resize ((size_t) total);
    for (float& x : out) x = std::tanh (1.6f * x * p.volume);
    return out;
}

// How the Rack module drives it: one sample at a time, params every 32 samples and after every note event.
std::vector<float> renderRack (const AcidParams& p, double seconds, const std::vector<Note>& notes)
{
    AcidVoice v; v.prepare (sr); v.setParams (p);
    const int total = (int) (seconds * sr);
    std::vector<float> out ((size_t) total);
    for (int i = 0; i < total; ++i)
    {
        bool ev = false;
        for (const Note& n : notes)
            if (n.at == i) { ev = true; if (n.on) v.noteOn (n.note, n.vel / 127.0f, p); else v.noteOff (n.note, p); }
        if (ev || i % 32 == 0) v.setParams (p);
        out[(size_t) i] = std::tanh (1.6f * v.process() * p.volume);
    }
    return out;
}

double rms (const std::vector<float>& v, int a, int b)
{
    double s = 0; for (int i = a; i < b; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return std::sqrt (s / (b - a));
}
double brightness (const std::vector<float>& v, int a, int b)
{
    double e = 0, q = 0;
    for (int i = a + 1; i < b; ++i) { e += (double) v[(size_t) i] * v[(size_t) i]; q += std::pow ((double) v[(size_t) i] - v[(size_t) i - 1], 2.0); }
    return q / (e > 0 ? e : 1.0);
}
double hz (const std::vector<float>& v, int a, int b)
{
    int c = 0; for (int i = a + 1; i < b; ++i) if (v[(size_t) i - 1] < 0 && v[(size_t) i] >= 0) ++c;
    return c / ((b - a) / sr);
}
bool finite (const std::vector<float>& v) { for (float x : v) if (! std::isfinite (x) || std::abs (x) > 4.0f) return false; return true; }

using namespace acidseq;
struct Ev { bool on; long long at; int note; float vel; };

// Runs the engine sample by sample from the internal clock, events at absolute sample positions. Windows stop short of the
// next loop's first step: the summed position can reach it one sample early (+-1 sample, as the plugin's checks allow).
std::vector<Ev> runSeq (const Pattern& p, Settings st, double bpm, int samples)
{
    Engine e; SongClock c; std::vector<Out> o; std::vector<Ev> all;
    for (int i = 0; i < samples; ++i)
    {
        double a, b; c.tickInternal (bpm, 1.0 / sr, a, b);
        e.process (p, st, true, a, b, o);
        for (const Out& x : o) all.push_back ({ x.on, i, x.note, x.velocity });
    }
    return all;
}
std::vector<Ev> ons (const std::vector<Ev>& v) { std::vector<Ev> r; for (const Ev& e : v) if (e.on) r.push_back (e); return r; }
std::vector<Ev> offs (const std::vector<Ev>& v) { std::vector<Ev> r; for (const Ev& e : v) if (! e.on) r.push_back (e); return r; }
Step note (int n, int o = 0, bool acc = false, bool sl = false) { Step s; s.gate = Gate::Note; s.note = n; s.oct = o; s.accent = acc; s.slide = sl; return s; }
Step tie() { Step s; s.gate = Gate::Tie; return s; }
Step rest() { return Step(); }
Pattern pat (std::initializer_list<Step> l) { Pattern p; p.len = (int) l.size(); int i = 0; for (const Step& s : l) p.steps[i++] = s; return p; }

AcidParams defaults()
{   // the plugin's parameter defaults, through the same maps
    AcidParams p;
    p.hpHz = (float) acidmaps::hpHz (0); p.minDecSec = (float) acidmaps::minDecSec (0); p.pw = (float) acidmaps::pwFrac (0.5);
    p.lfoHz = (float) acidmaps::lfoHz (0.6157); p.fAtkSec = (float) acidmaps::atkSec (0); p.fRelSec = (float) acidmaps::relSec (0.68);
    p.aAtkSec = (float) acidmaps::atkSec (0); p.aDecSec = (float) acidmaps::envDecSec (0.5); p.aSus = 1.0f; p.aRelSec = (float) acidmaps::relSec (0.03);
    return p;
}
}

int main()
{
    using namespace acidmaps;
    // ---- maps (numbers from the plugin's smoke test) ----
    check (near (hpHz (0), 20) && near (hpHz (0.5), 100) && near (hpHz (1), 500), "hpHz");
    check (near (minDecSec (0), 0.03) && near (minDecSec (0.5), 0.134164079) && near (minDecSec (1), 0.6), "minDecSec");
    check (near (pwFrac (0), 0.05) && near (pwFrac (1), 0.95), "pwFrac");
    check (near (lfoHz (0), 0.05) && near (lfoHz (0.5), 1.0, 1e-3) && near (lfoHz (1), 20), "lfoHz");
    check (near (keyMul (1, 60), 2) && near (keyMul (0.5, 36), 0.70710678) && near (keyMul (0, 90), 1), "keyMul");
    check (near (atkSec (0), 0.002) && near (atkSec (1), 1.5), "atkSec");
    check (near (envDecSec (0), 0.03) && near (envDecSec (0.5), 0.27331300737) && near (envDecSec (1), 2.49), "envDecSec");
    check (near (relSec (0.03), 0.00598455419) && near (relSec (0.68), 0.29401588734) && near (relSec (1), 2.0), "relSec");
    check (near (gateFrac (0.5), 0.55) && near (gateFrac (0), 0.1) && near (gateFrac (1), 1.0), "gateFrac");

    // ---- voice ----
    {   // defaults sound as the plugin does (its recorded phase-1 numbers)
        const double refRms[2] = { 0.231228, 0.377944 }, refBright[2] = { 0.004067, 0.002700 };
        for (int w = 0; w < 2; ++w)
        {
            AcidParams p = defaults(); p.square = w == 1;
            const std::vector<Note> n = { { 0, true, 45, 90 }, { (int) (0.4 * sr), false, 45, 0 } };
            const auto a = renderBlocks (p, 0.6, n);
            check (std::abs (rms (a, 2000, 15000) / refRms[w] - 1.0) < 0.03, w ? "default square matches the plugin's level" : "default saw matches the plugin's level", rms (a, 2000, 15000));
            check (std::abs (brightness (a, 2000, 15000) / refBright[w] - 1.0) < 0.05, w ? "default square matches the plugin's tone" : "default saw matches the plugin's tone", brightness (a, 2000, 15000));
            const auto r = renderRack (p, 0.6, n);
            check (std::abs (rms (r, 2000, 15000) / rms (a, 2000, 15000) - 1.0) < 0.01, w ? "square: per-sample driving within 1 % of the block render" : "saw: per-sample driving within 1 % of the block render", rms (r, 2000, 15000) / rms (a, 2000, 15000));
            check (std::abs (brightness (r, 2000, 15000) / brightness (a, 2000, 15000) - 1.0) < 0.01, "and the same tone", brightness (r, 2000, 15000) / brightness (a, 2000, 15000));
        }
    }
    {   // sound on, sound off, pitch
        AcidParams p = defaults(); p.envmod = 0.0f; p.cutoffHz = 4000.0f;
        const auto a = renderRack (p, 0.8, { { 0, true, 57, 90 }, { (int) (0.4 * sr), false, 57, 0 } });
        check (finite (a), "output is finite and bounded");
        check (rms (a, 4000, 16000) > 0.03, "a held note makes sound", rms (a, 4000, 16000));
        check (rms (a, (int) (0.55 * sr), (int) (0.8 * sr)) < 0.002, "silent after release", rms (a, (int) (0.55 * sr), (int) (0.8 * sr)));
        check (std::abs (hz (a, 4000, 16000) - 220.0) < 8.0, "A3 plays at 220 Hz", hz (a, 4000, 16000));
    }
    {   // cutoff brightens
        AcidParams lo = defaults(), hi = defaults(); lo.envmod = hi.envmod = 0.0f; lo.cutoffHz = 120.0f; hi.cutoffHz = 6000.0f;
        const auto a = renderRack (lo, 0.4, { { 0, true, 45, 90 } }), b = renderRack (hi, 0.4, { { 0, true, 45, 90 } });
        check (brightness (b, 6000, 16000) > brightness (a, 6000, 16000) * 3.0, "opening the cutoff brightens the note", brightness (b, 6000, 16000) / brightness (a, 6000, 16000));
    }
    {   // envelope sweep
        AcidParams p = defaults(); p.cutoffHz = 150.0f; p.envmod = 1.0f; p.decaySec = 0.2f;
        const auto a = renderRack (p, 0.8, { { 0, true, 45, 90 } });
        const double early = brightness (a, 200, 2400), late = brightness (a, (int) (0.6 * sr), (int) (0.75 * sr));
        check (early > late * 3.0, "the envelope closes the filter over the decay", early / late);
    }
    {   // accent
        AcidParams p = defaults(); p.accent = 1.0f; p.envmod = 0.0f; p.cutoffHz = 3000.0f;
        const auto a = renderRack (p, 0.3, { { 0, true, 45, 70 } }), b = renderRack (p, 0.3, { { 0, true, 45, 120 } });
        check (rms (b, 3000, 10000) > rms (a, 3000, 10000) * 1.3, "an accented note is louder", rms (b, 3000, 10000) / rms (a, 3000, 10000));
    }
    {   // slide
        AcidParams p = defaults(); p.envmod = 0.0f; p.cutoffHz = 4000.0f;
        const auto a = renderRack (p, 0.8, { { 0, true, 45, 90 }, { (int) (0.3 * sr), true, 52, 90 } });
        check (std::abs (hz (a, 4000, 12000) - 110.0) < 6.0, "first note is A2 (110 Hz)", hz (a, 4000, 12000));
        check (std::abs (hz (a, (int) (0.6 * sr), (int) (0.78 * sr)) - 164.8) < 8.0, "the overlapping note glides to E3", hz (a, (int) (0.6 * sr), (int) (0.78 * sr)));
        const double dip = rms (a, (int) (0.30 * sr), (int) (0.32 * sr)) / rms (a, (int) (0.2 * sr), (int) (0.28 * sr));
        check (dip > 0.6, "no re-attack on a slide (level holds)", dip);
    }
    {   // forced legato (the Acid Voice module's SLIDE input): after a rest, glide in without restarting the envelope
        AcidParams p = defaults(); p.envmod = 0.0f; p.cutoffHz = 4000.0f;
        AcidVoice v; v.prepare (sr); v.setParams (p);
        std::vector<float> a;
        auto run = [&] (double sec) { for (int i = 0; i < (int) (sec * sr); ++i) a.push_back (v.process()); };
        v.noteOn (45, 0.7f, p); run (0.3); v.noteOff (45, p); run (0.05);
        v.noteOn (52, 0.7f, p, true);
        check (v.filterEnv() < 0.5f && v.active(), "forced legato keeps the filter envelope where it was and gates on", v.filterEnv());
        run (0.4);
        check (std::abs (hz (a, (int) (0.55 * sr), (int) (0.74 * sr)) - 164.8) < 8.0, "forced legato lands on E3", hz (a, (int) (0.55 * sr), (int) (0.74 * sr)));
        v.noteOff (52, p); run (0.3);
        check (! v.active(), "and lets go on note off", v.active());
    }
    {   // tweaks stay finite at the extremes, square with LFO on the width
        AcidParams p = defaults(); p.square = true; p.pw = 0.05f; p.lfoPw = 1.0f; p.lfoHz = 20.0f; p.res = 1.0f; p.hpHz = 500.0f; p.keyFollow = 1.0f;
        const auto a = renderRack (p, 0.5, { { 0, true, 84, 127 } });
        check (finite (a) && rms (a, 4000, 20000) > 0.01, "extreme tweaks stay finite and audible", rms (a, 4000, 20000));
    }

    // ---- sequencer (timings from the plugin's smoke test: 120 bpm, 44.1 kHz) ----
    Settings on; on.swing = 0.0f; on.gateFrac = 0.55f;
    const double stepS = 5512.5;
    {
        const auto t = runSeq (pat ({ note (0), note (2), note (4), note (5) }), on, 120.0, 22000);
        const auto tOn = ons (t), tOff = offs (t);
        const long long wantOn[4] = { 0, 5512, 11025, 16537 }; const int wantMidi[4] = { 36, 38, 40, 41 };
        bool timing = tOn.size() == 4; for (size_t i = 0; timing && i < 4; ++i) timing = std::abs (tOn[i].at - wantOn[i]) <= 1 && tOn[i].note == wantMidi[i];
        check (timing, "sequencer: four notes start on the 16ths with the right pitches", (double) tOn.size());
        bool gateOk = tOff.size() == 4; for (size_t i = 0; gateOk && i < 4; ++i) gateOk = std::abs (tOff[i].at - (tOn[i].at + 3032)) <= 1 && (i == 3 || tOff[i].at < tOn[i + 1].at) && tOff[i].note == tOn[i].note;
        check (gateOk, "sequencer: each note ends 0.55 of a step after it starts");
    }
    {
        const auto r = runSeq (pat ({ note (0), rest(), note (0), rest() }), on, 120.0, 22000);
        check (ons (r).size() == 2 && offs (r).size() == 2 && r.size() == 4, "sequencer: rests make no events", (double) r.size());
        const auto a = ons (runSeq (pat ({ note (0, 0, true), note (0) }), on, 120.0, 11000));
        check (a.size() == 2 && a[0].vel >= 110.0f / 127.0f && a[1].vel < 110.0f / 127.0f, "sequencer: an accent is loud, a plain step is not");
    }
    {
        const auto sl = runSeq (pat ({ note (0, 0, false, true), note (2), rest(), rest() }), on, 120.0, 22000);
        int iOnD = -1, iOffC = -1, offCBefore = 0;
        for (size_t i = 0; i < sl.size(); ++i)
        {
            if (sl[i].on && sl[i].note == 38) iOnD = (int) i;
            if (! sl[i].on && sl[i].note == 36 && iOffC < 0) iOffC = (int) i;
        }
        for (int i = 0; i < iOnD; ++i) if (! sl[(size_t) i].on && sl[(size_t) i].note == 36) ++offCBefore;
        check (iOnD >= 0 && iOffC == iOnD + 1 && sl[(size_t) iOffC].at == sl[(size_t) iOnD].at && offCBefore == 0, "sequencer: a slide plays the new note first, then lets the old one go");
    }
    {
        const auto ti = runSeq (pat ({ note (0), tie(), rest(), rest() }), on, 120.0, 22000);
        const auto o = ons (ti), f = offs (ti);
        check (o.size() == 1 && f.size() == 1 && f[0].note == 36 && std::abs (f[0].at - (long long) (stepS + 0.95 * stepS)) <= 1, "sequencer: a tie holds the note and ends 0.95 into its own step", f.empty() ? 0.0 : (double) f[0].at);
        const auto oc = ons (runSeq (pat ({ note (0, 1), note (0, -1) }), on, 120.0, 11000));
        check (oc.size() == 2 && oc[0].note == 48 && oc[1].note == 24, "sequencer: octave +1 adds 12, -1 subtracts 12");
    }
    {   // stop releases once
        Engine e; std::vector<Out> o; const Pattern p = pat ({ note (0, 0, false, true), note (2, 0, false, true) });
        e.process (p, on, true, 0.0, 0.1, o);
        e.process (p, on, false, 0.1, 0.2, o);
        const bool one = o.size() == 1 && ! o[0].on;
        e.process (p, on, false, 0.2, 0.3, o);
        check (one && o.empty() && e.currentStep() == -1, "sequencer: stopping releases the note once, then silence");
    }
    {   // a jump (reset) releases, then restarts step 0 at once
        Engine e; std::vector<Out> o; const Pattern p = pat ({ note (0, 0, false, true), note (2, 0, false, true), note (4, 0, false, true), note (5, 0, false, true) });
        e.process (p, on, true, 1.0, 2.0, o);
        e.process (p, on, true, 0.0, 0.01, o);
        check (o.size() == 2 && ! o[0].on && o[1].on && o[1].note == 36, "sequencer: a position jump releases, then restarts step 0", (double) o.size());
    }
    {
        Settings sw = on; sw.swing = 1.0f;
        const auto sv = ons (runSeq (pat ({ note (0), note (2) }), sw, 120.0, 11000));
        check (sv.size() == 2 && std::abs (sv[1].at - (long long) (stepS + 0.33 * stepS)) <= 1, "sequencer: swing 1.0 delays the odd step by 0.33 of a step", sv.size() > 1 ? (double) sv[1].at : 0.0);
        Settings s2 = sw; s2.gateFrac = 1.0f;
        const auto e2 = runSeq (pat ({ note (0), note (2), note (4), rest() }), s2, 120.0, 22000);
        long long onNext = -1; int offs38 = 0; bool dropped = true;
        for (const Ev& x : e2) if (x.on && x.note == 40) onNext = x.at;
        for (const Ev& x : e2) if (! x.on && x.note == 38) { ++offs38; if (x.at > onNext) dropped = false; }
        check (onNext > 0 && dropped && offs38 == 1, "sequencer: a swung note's late gate-off does not land after the next note-on");
    }

    // ---- external clock ----
    {   // a pulse every 5000 samples: step k starts on pulse k, and nothing plays before the first pulse
        const Pattern p = pat ({ note (0), note (2), note (4), note (5) });
        Engine e; SongClock c; std::vector<Out> o; std::vector<Ev> all;
        for (int i = 0; i < 30000; ++i)
        {
            const bool edge = i >= 1000 && (i - 1000) % 5000 == 0;
            double a, b;
            const bool run = c.tickExternal (edge, 5512.5, a, b);
            e.process (p, on, run, run ? a : 0.0, run ? b : 0.0, o);
            for (const Out& x : o) all.push_back ({ x.on, i, x.note, x.velocity });
        }
        const auto on4 = ons (all);
        const int want[6] = { 36, 38, 40, 41, 36, 38 };   // pulses at 1000, 6000 .. 26000
        bool ok = on4.size() == 6;
        for (size_t k = 0; ok && k < on4.size(); ++k) ok = on4[k].at == 1000 + 5000 * (long long) k && on4[k].note == want[k];
        check (ok, "external clock: step k starts exactly on pulse k", (double) on4.size());
        const auto off4 = offs (all);
        check (! off4.empty() && std::abs (off4[1].at - (6000 + 0.55 * 5000)) <= 2, "external clock: gate length follows the pulse spacing", off4.size() > 1 ? (double) off4[1].at : 0.0);
    }
    {   // the clock stops: the position waits just short of the next step, so the next note does not play early
        SongClock c; double a = 0, b = 0;
        for (int i = 0; i < 100; ++i) c.tickExternal (i == 0 || i == 50, 50.0, a, b);
        for (int i = 0; i < 1000; ++i) c.tickExternal (false, 50.0, a, b);
        check (b < 0.5 && b > 0.49, "external clock: a stopped clock holds before the next step", b);
    }
    {   // stopped (Run off) with pulses every 400: the spacing is still measured, the position does not move,
        // and after Run the next pulse plays the next step at the new speed
        SongClock c; double a = 0, b = 0;
        for (int i = 0; i < 2; ++i) c.tickExternal (true, 100.0, a, b);
        const double held = c.pos;
        for (int i = 0; i < 1200; ++i) c.idleExternal (i % 400 == 0);
        const bool still = c.pos == held && c.period == 400.0;
        c.tickExternal (true, 100.0, a, b);
        check (still && a == held && b > 0.5 && std::abs ((b - 0.5) - 0.25 / 400.0) < 1e-12, "external clock: Run off keeps the pulse spacing and holds the position", c.period);
    }

    // ---- notation and JSON text ----
    {
        bool round = true;
        for (const char* d : demo) { Pattern p; round = round && parseNotation (d, p) && formatNotation (p) == d; }
        check (round, "notation: the web demo set parses and formats back to the same text");
        Pattern p;
        check (parseNotation ("C#'", p) == false && parseNotation ("E#", p) == false && parseNotation ("H", p) == false && parseNotation ("C*x", p) == false && parseNotation ("  ", p) == false, "notation: bad tokens and empty text are refused");
        check (parseNotation ("C' A_*~ = - D#^~", p) && p.len == 5 && p.steps[0].note == 12 && p.steps[1].oct == -1 && p.steps[1].accent && p.steps[1].slide
               && p.steps[2].gate == Gate::Tie && p.steps[3].gate == Gate::Rest && p.steps[4].note == 3 && p.steps[4].oct == 1, "notation: every token kind");
        std::string many; for (int i = 0; i < 40; ++i) many += "C ";
        check (parseNotation (many, p) && p.len == 32, "notation: keeps the first 32 steps", p.len);
    }
    {   // the plugin's expected JSON text for its clamped test pattern (made by node from acidbass.js)
        Pattern p; p.len = 32;
        p.steps[0] = note (12, 1, true, true);
        p.steps[1].gate = Gate::Rest; p.steps[1].note = 3; p.steps[1].oct = -1;
        p.steps[2].gate = Gate::Tie; p.steps[2].note = 7; p.steps[2].oct = -1; p.steps[2].accent = true;
        p.steps[3] = note (2);
        std::string want = R"J({"len":32,"steps":[{"gate":"note","note":12,"oct":1,"accent":true,"slide":true},{"gate":"rest","note":3,"oct":-1,"accent":false,"slide":false},{"gate":"tie","note":7,"oct":-1,"accent":true,"slide":false},{"gate":"note","note":2,"oct":0,"accent":false,"slide":false})J";
        for (int i = 4; i < 32; ++i) want += R"J(,{"gate":"rest","note":0,"oct":0,"accent":false,"slide":false})J";
        want += "]}";
        check (toJson (p) == want, "json: same text as the web app and the plugin");
        check (slotName (0) == "I-A1" && slotName (9) == "I-B2" && slotName (31) == "II-B8", "slot names");
    }

    std::printf ("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
