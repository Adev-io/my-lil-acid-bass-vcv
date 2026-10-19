// Sequencer.h — the web app's step pattern and its event rules, an engine that turns a moving song position into note
// events, and the step notation / pattern JSON text the web app and the FL Studio plugin read and write.
// No Rack in here, so the tests can drive it.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace acidseq
{
constexpr int maxSteps = 32;
constexpr int slots = 32;   // 2 banks x A/B x 8
enum class Gate : int { Note = 0, Rest = 1, Tie = 2 };
struct Step { Gate gate = Gate::Rest; int note = 0, oct = 0; bool accent = false, slide = false; };
struct Pattern { int len = 16; Step steps[maxSteps]; };

inline int midiOf (const Step& s) { return 36 + s.note + 12 * s.oct; }

// acidbass.js stepEvent(): what one step does, given its neighbours (patterns wrap).
struct Event { bool off = true, trig = false, glide = false, hold = false, accent = false; int midi = 0; float len = 0.55f; };

inline Event stepEvent (const Pattern& p, int i)
{
    const int L = std::max (1, p.len);
    auto at = [&] (int j) -> const Step& { return p.steps[((j % L) + L) % L]; };
    const Step& s = at (i); const Step& prev = at (i - 1); const Step& next = at (i + 1);
    Event e;
    if (s.gate == Gate::Rest) return e;
    e.hold = (s.slide && next.gate == Gate::Note) || next.gate == Gate::Tie;
    if (s.gate == Gate::Tie)
    {
        for (int b = 1; b <= L; ++b)
        {
            const Step& q = at (i - b);
            if (q.gate == Gate::Note) { e.off = false; e.midi = midiOf (q); e.len = 0.95f; return e; }
            if (q.gate == Gate::Rest) break;
        }
        e.hold = false; return e;   // nothing but ties: no pitch, treated as a rest
    }
    const bool legato = prev.slide && prev.gate == Gate::Note;
    e.off = false; e.trig = ! legato; e.glide = legato; e.midi = midiOf (s); e.accent = s.accent; e.len = 0.55f;
    return e;
}

// "I-A1" .. "II-B8"
inline std::string slotName (int i)
{
    return std::string (i < 16 ? "I" : "II") + "-" + ((i % 16) < 8 ? "A" : "B") + std::to_string ((i % 8) + 1);
}

// Step notation, one token per step: C..B note, C' the high C, ^ _ octave up/down, * accent, ~ slide, - rest, = tie.
// False (and `out` untouched) for an empty string or any bad token, as the web's parsePattern throws.
inline bool parseNotation (const std::string& text, Pattern& out)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    Pattern p;
    int n = 0;
    size_t i = 0;
    while (true)
    {
        while (i < text.size() && std::isspace ((unsigned char) text[i])) ++i;
        if (i >= text.size()) break;
        size_t j = i;
        while (j < text.size() && ! std::isspace ((unsigned char) text[j])) ++j;
        const std::string tok = text.substr (i, j - i);
        i = j;
        if (n >= maxSteps) break;   // the web keeps the first 32 tokens
        Step& s = p.steps[n++];
        if (tok == "-") continue;
        if (tok == "=") { s.gate = Gate::Tie; continue; }
        size_t k = 0;
        if (tok[k] < 'A' || tok[k] > 'G') return false;
        std::string name (1, tok[k++]);
        if (k < tok.size() && tok[k] == '#') name += tok[k++];
        bool high = false;
        if (k < tok.size() && tok[k] == '\'') { high = true; ++k; }
        int oct = 0;
        if (k < tok.size() && (tok[k] == '^' || tok[k] == '_')) oct = tok[k++] == '^' ? 1 : -1;
        bool acc = false, sl = false;
        for (; k < tok.size(); ++k)
        {
            if (tok[k] == '*') acc = true;
            else if (tok[k] == '~') sl = true;
            else return false;
        }
        int note = -1;
        if (high) { if (name != "C") return false; note = 12; }
        else for (int q = 0; q < 12; ++q) if (name == names[q]) note = q;
        if (note < 0) return false;
        s.gate = Gate::Note; s.note = note; s.oct = oct; s.accent = acc; s.slide = sl;
    }
    if (n == 0) return false;
    p.len = n;
    out = p;
    return true;
}

inline std::string formatNotation (const Pattern& p)
{
    static const char* names[13] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C'" };
    std::string t;
    for (int i = 0; i < std::max (1, std::min (p.len, maxSteps)); ++i)
    {
        const Step& s = p.steps[i];
        if (i) t += ' ';
        if (s.gate == Gate::Rest) { t += '-'; continue; }
        if (s.gate == Gate::Tie) { t += '='; continue; }
        t += names[std::max (0, std::min (12, s.note))];
        if (s.oct == 1) t += '^'; else if (s.oct == -1) t += '_';
        if (s.accent) t += '*';
        if (s.slide) t += '~';
    }
    return t;
}

// The same text JSON.stringify(cleanPattern(...)) gives in the page: keys in this order, no spaces.
inline std::string toJson (const Pattern& p)
{
    std::string t = "{\"len\":" + std::to_string (p.len) + ",\"steps\":[";
    for (int i = 0; i < maxSteps; ++i)
    {
        const Step& s = p.steps[i];
        t += i ? "," : "";
        t += std::string ("{\"gate\":\"") + (s.gate == Gate::Note ? "note" : s.gate == Gate::Tie ? "tie" : "rest")
           + "\",\"note\":" + std::to_string (s.note) + ",\"oct\":" + std::to_string (s.oct)
           + ",\"accent\":" + (s.accent ? "true" : "false") + ",\"slide\":" + (s.slide ? "true" : "false") + "}";
    }
    return t + "]}";
}

// The six-line starter set of the web app (loadDemo), for slots I-A1..I-A6.
static const char* const demo[6] = {
    "A* - A A^ - A_* A - G~ A - A* - C^ - E~",
    "E E E^ E E E^* E E E E^ E - E G~ E^* D",
    "C* C~ D#~ G - C^* - G~ F - D#* C - G_ - C",
    "F* = = F - F^ = F* - G# = = A#~ C^* - C'",
    "D* - - - - - D_ - - - A* - - - - -",
    "C - C - C* - C^ - C - C - C* - C~ D#",
};

struct Settings { bool on = true; float swing = 0.0f; float gateFrac = 0.55f; };
struct Out { bool on; int note; float velocity; };

class Engine
{
public:
    int currentStep() const { return stepNow; }   // -1 when silent

    // Moves the song position from ppq0 to ppq1 (quarter notes; one step is a 16th). Events come out in time order.
    // Ranges should be back to back; a gap or overlap bigger than 0.05 counts as a jump (let go and re-sync).
    void process (const Pattern& pat, const Settings& s, bool playing, double ppq0, double ppq1, std::vector<Out>& out)
    {
        out.clear();
        if (! s.on || ! playing) { release (out); haveEnd = false; stepNow = -1; return; }
        if (! haveEnd || std::abs (ppq0 - lastEnd) > 0.05)
        {
            release (out);
            nextK = (long long) std::ceil (ppq0 * 4.0 - 1.0e-6);
        }
        haveEnd = true; lastEnd = ppq1;
        auto timeOf = [&] (long long k) { return k * 0.25 + ((k & 1) ? (double) s.swing * 0.25 * 0.33 : 0.0); };
        for (;;)
        {
            const double tStep = timeOf (nextK), tOff = pendingOff ? offAt : 1.0e300;
            if (tOff < ppq1 && tOff <= tStep) { release (out); continue; }
            if (tStep >= ppq1) break;
            const int len = std::max (1, pat.len), idx = (int) (((nextK % len) + len) % len);
            const Event e = stepEvent (pat, idx);
            if (! e.off)
            {
                stepNow = idx;
                pendingOff = false;   // a step that sounds cancels a gate-off still waiting; a rest leaves it be, as in the page
                if (e.trig || ! sounding) { release (out); start (e, out); }
                else if (e.glide) { const int old = cur; start (e, out); if (old != e.midi) out.push_back ({ false, old, 0.0f }); }   // new note first, then let the old one go: the voice slides (a slide to the same pitch must not release it)
                if (! e.hold) { pendingOff = true; offAt = tStep + 0.25 * (e.len < 0.9f ? s.gateFrac : e.len); }
            }
            ++nextK;
        }
    }

private:
    void start (const Event& e, std::vector<Out>& out) { out.push_back ({ true, e.midi, e.accent ? 1.0f : 0.7f }); cur = e.midi; sounding = true; }
    void release (std::vector<Out>& out)
    {
        if (sounding) out.push_back ({ false, cur, 0.0f });
        sounding = false; pendingOff = false;
    }

    long long nextK = 0; double lastEnd = 0.0, offAt = 0.0; bool haveEnd = false, pendingOff = false, sounding = false; int cur = 0, stepNow = -1;
};
}
