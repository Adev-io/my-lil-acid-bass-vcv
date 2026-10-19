// AcidBass.cpp — the Rack module: the acid voice, the step sequencer, 32 pattern slots and the step grid on the panel.
// Author: Ari Klopfer / A_Development · MIT
#include "AcidVoice.h"
#include "Clock.h"
#include "Sequencer.h"
#include "plugin.hpp"
#include <osdialog.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

using acidseq::Gate;
using acidseq::Pattern;
using acidseq::Step;

static const float cutLo = 60.f, cutHi = 8000.f;   // the plugin's cutoff range, exponential on the knob
static float cutoffKnob (float hz) { return std::log (hz / cutLo) / std::log (cutHi / cutLo); }
static float decayKnob (float sec) { return std::log (sec / 0.03f) / std::log (83.f); }   // inverse of acidmaps::envDecSec

// cleanPattern() of the web app: anything missing or out of range comes out as a playable default.
static Pattern patternFromJson (json_t* v)
{
    Pattern p;
    auto num = [] (json_t* x, int lo, int hi, int d) {
        if (! json_is_number (x)) return d;
        const double c = std::min ((double) hi, std::max ((double) lo, json_number_value (x)));
        return (int) std::floor (c + 0.5);   // Math.round
    };
    if (! json_is_object (v)) return p;
    p.len = num (json_object_get (v, "len"), 1, acidseq::maxSteps, 16);
    json_t* steps = json_object_get (v, "steps");
    if (json_is_array (steps))
        for (size_t i = 0; i < (size_t) acidseq::maxSteps && i < json_array_size (steps); ++i)
        {
            json_t* s = json_array_get (steps, i);
            Step& o = p.steps[i];
            json_t* g = json_object_get (s, "gate");
            const char* gs = json_is_string (g) ? json_string_value (g) : "";
            o.gate = ! std::strcmp (gs, "note") ? Gate::Note : ! std::strcmp (gs, "tie") ? Gate::Tie : Gate::Rest;
            o.note = num (json_object_get (s, "note"), 0, 12, 0);
            o.oct = num (json_object_get (s, "oct"), -1, 1, 0);
            o.accent = json_is_true (json_object_get (s, "accent"));
            o.slide = json_is_true (json_object_get (s, "slide"));
        }
    return p;
}

struct SlotQuantity : ParamQuantity
{
    std::string getDisplayValueString() override { return acidseq::slotName (clamp ((int) std::round (getValue()), 0, acidseq::slots - 1)); }
};

struct AcidBass : Module
{
    enum ParamId { TUNE_PARAM, CUTOFF_PARAM, RES_PARAM, ENVMOD_PARAM, DECAY_PARAM, ACCENT_PARAM, VOLUME_PARAM, WAVE_PARAM,
        FATK_PARAM, FSUS_PARAM, FREL_PARAM, AATK_PARAM, ADEC_PARAM, ASUS_PARAM, AREL_PARAM,
        HP_PARAM, KFOL_PARAM, MIND_PARAM, PW_PARAM, LFOR_PARAM, LFOPW_PARAM,
        TEMPO_PARAM, SWING_PARAM, GATE_PARAM, PATTERN_PARAM, RUN_PARAM, RESET_PARAM, PARAMS_LEN };
    enum InputId { CLOCK_INPUT, RESET_INPUT, PATTERN_INPUT, VOCT_INPUT, GATE_INPUT, ACCENT_INPUT,
        CUTOFF_CV_INPUT, RES_CV_INPUT, ENVMOD_CV_INPUT, DECAY_CV_INPUT, ACCENT_CV_INPUT, INPUTS_LEN };
    enum OutputId { SEQ_VOCT_OUTPUT, SEQ_GATE_OUTPUT, SEQ_ACCENT_OUTPUT, AUDIO_OUTPUT, OUTPUTS_LEN };
    enum LightId { RUN_LIGHT, LIGHTS_LEN };

    // Patterns: the UI thread is the only writer (under bankMutex, then bankRev++). The audio thread copies the playing slot
    // with try_lock only when it changed, so it never waits on the UI.
    Pattern bank[acidseq::slots];
    std::mutex bankMutex;
    std::atomic<int> bankRev { 0 }, curSlot { 0 }, playStep { -1 };

    AcidVoice voice;
    acidseq::Engine seq;
    SongClock clock;
    AcidParams ap;
    Pattern audioPat;
    int audioRev = -1, audioSlot = -1;
    float preparedSr = 0.f;
    std::vector<acidseq::Out> evs;
    dsp::SchmittTrigger clockTrig, resetTrig, resetBtn, gateTrig;
    dsp::ClockDivider paramDiv;
    bool extGate = false, seqGate = false, seqAcc = false, extClock = false;
    int extNote = 60, seqNote = 36;

    AcidBass()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam (TUNE_PARAM, -100.f, 100.f, 0.f, "Tuning", " cents");
        configParam (CUTOFF_PARAM, 0.f, 1.f, cutoffKnob (460.f), "Cutoff", " Hz", cutHi / cutLo, cutLo);
        configParam (RES_PARAM, 0.f, 1.f, 0.5f, "Resonance", "%", 0.f, 100.f);
        configParam (ENVMOD_PARAM, 0.f, 1.f, 0.6f, "Env mod", "%", 0.f, 100.f);
        configParam (DECAY_PARAM, 0.f, 1.f, decayKnob (0.5f), "Decay", " s", 83.f, 0.03f);
        configParam (ACCENT_PARAM, 0.f, 1.f, 0.6f, "Accent", "%", 0.f, 100.f);
        configParam (VOLUME_PARAM, 0.f, 1.f, 0.7f, "Volume", "%", 0.f, 100.f);
        configSwitch (WAVE_PARAM, 0.f, 1.f, 0.f, "Waveform", { "Saw", "Square" });
        configParam (FATK_PARAM, 0.f, 1.f, 0.f, "Filter attack", " s", 750.f, 0.002f);
        configParam (FSUS_PARAM, 0.f, 1.f, 0.f, "Filter sustain", "%", 0.f, 100.f);
        configParam (FREL_PARAM, 0.f, 1.f, 0.68f, "Filter release", " s", 400.f, 0.005f);
        configParam (AATK_PARAM, 0.f, 1.f, 0.f, "Amp attack", " s", 750.f, 0.002f);
        configParam (ADEC_PARAM, 0.f, 1.f, 0.5f, "Amp decay", " s", 83.f, 0.03f);
        configParam (ASUS_PARAM, 0.f, 1.f, 1.f, "Amp sustain", "%", 0.f, 100.f);
        configParam (AREL_PARAM, 0.f, 1.f, 0.03f, "Amp release", " s", 400.f, 0.005f);
        configParam (HP_PARAM, 0.f, 1.f, 0.f, "High-pass (20 Hz = off)", " Hz", 25.f, 20.f);
        configParam (KFOL_PARAM, 0.f, 1.f, 0.f, "Key follow", "%", 0.f, 100.f);
        configParam (MIND_PARAM, 0.f, 1.f, 0.f, "Min decay", " s", 20.f, 0.03f);
        configParam (PW_PARAM, 0.f, 1.f, 0.5f, "Pulse width (square)", "%", 0.f, 90.f, 5.f);
        configParam (LFOR_PARAM, 0.f, 1.f, 0.6157f, "LFO rate", " Hz", 400.f, 0.05f);
        configParam (LFOPW_PARAM, 0.f, 1.f, 0.f, "LFO to pulse width", "%", 0.f, 100.f);
        configParam (TEMPO_PARAM, 40.f, 300.f, 120.f, "Tempo (internal clock)", " BPM");
        configParam (SWING_PARAM, 0.f, 1.f, 0.f, "Swing", "%", 0.f, 100.f);
        configParam (GATE_PARAM, 0.f, 1.f, 0.5f, "Gate length", "%", 0.f, 90.f, 10.f);
        configParam<SlotQuantity> (PATTERN_PARAM, 0.f, acidseq::slots - 1, 0.f, "Pattern");
        paramQuantities[PATTERN_PARAM]->snapEnabled = true;
        configSwitch (RUN_PARAM, 0.f, 1.f, 1.f, "Run", { "Stopped", "Running" });
        configButton (RESET_PARAM, "Reset to step 1");
        paramQuantities[RUN_PARAM]->randomizeEnabled = false;
        paramQuantities[PATTERN_PARAM]->randomizeEnabled = false;
        configInput (CLOCK_INPUT, "Clock (one pulse per 16th; replaces the tempo knob)");
        configInput (RESET_INPUT, "Reset");
        configInput (PATTERN_INPUT, "Pattern select (0-10 V across the 32 slots, added to the knob)");
        configInput (VOCT_INPUT, "Play: V/OCT (semitones)");
        configInput (GATE_INPUT, "Play: gate (a pitch change while high slides)");
        configInput (ACCENT_INPUT, "Play: accent (high at the gate start)");
        configInput (CUTOFF_CV_INPUT, "Cutoff CV (1 V/oct)");
        configInput (RES_CV_INPUT, "Resonance CV (10 V = full range)");
        configInput (ENVMOD_CV_INPUT, "Env mod CV (10 V = full range)");
        configInput (DECAY_CV_INPUT, "Decay CV (10 V = full range)");
        configInput (ACCENT_CV_INPUT, "Accent amount CV (10 V = full range)");
        configOutput (SEQ_VOCT_OUTPUT, "Sequencer V/OCT");
        configOutput (SEQ_GATE_OUTPUT, "Sequencer gate (stays high through slides and ties)");
        configOutput (SEQ_ACCENT_OUTPUT, "Sequencer accent");
        configOutput (AUDIO_OUTPUT, "Audio");
        paramDiv.setDivision (32);
        evs.reserve (16);
    }

    template <typename F> void edit (int slot, F f)   // UI thread
    {
        std::lock_guard<std::mutex> lock (bankMutex);
        f (bank[clamp (slot, 0, acidseq::slots - 1)]);
        ++bankRev;
    }

    void readParams()
    {
        auto k = [&] (int id) { return params[id].getValue(); };
        auto withCv = [&] (int id, int in) { return clamp (k (id) + inputs[in].getVoltage() / 10.f, 0.f, 1.f); };
        ap.tuneCents = k (TUNE_PARAM);
        ap.cutoffHz = std::max (5.f, cutLo * std::pow (cutHi / cutLo, k (CUTOFF_PARAM)) * std::pow (2.f, clamp (inputs[CUTOFF_CV_INPUT].getVoltage(), -10.f, 10.f)));
        ap.res = withCv (RES_PARAM, RES_CV_INPUT);
        ap.envmod = withCv (ENVMOD_PARAM, ENVMOD_CV_INPUT);
        ap.decaySec = (float) acidmaps::envDecSec (withCv (DECAY_PARAM, DECAY_CV_INPUT));
        ap.accent = withCv (ACCENT_PARAM, ACCENT_CV_INPUT);
        ap.volume = k (VOLUME_PARAM);
        ap.square = k (WAVE_PARAM) > 0.5f;
        ap.hpHz = (float) acidmaps::hpHz (k (HP_PARAM)); ap.keyFollow = k (KFOL_PARAM); ap.minDecSec = (float) acidmaps::minDecSec (k (MIND_PARAM));
        ap.pw = (float) acidmaps::pwFrac (k (PW_PARAM)); ap.lfoHz = (float) acidmaps::lfoHz (k (LFOR_PARAM)); ap.lfoPw = k (LFOPW_PARAM);
        ap.fAtkSec = (float) acidmaps::atkSec (k (FATK_PARAM)); ap.fSus = k (FSUS_PARAM); ap.fRelSec = (float) acidmaps::relSec (k (FREL_PARAM));
        ap.aAtkSec = (float) acidmaps::atkSec (k (AATK_PARAM)); ap.aDecSec = (float) acidmaps::envDecSec (k (ADEC_PARAM));
        ap.aSus = k (ASUS_PARAM); ap.aRelSec = (float) acidmaps::relSec (k (AREL_PARAM));
        voice.setParams (ap);
    }

    void applySeq()   // sequencer events into the voice and the SEQ outputs
    {
        for (const acidseq::Out& e : evs)
        {
            if (e.on) { voice.noteOn (e.note, e.velocity, ap); seqNote = e.note; seqGate = true; seqAcc = e.velocity >= 110.f / 127.f; }
            else { voice.noteOff (e.note, ap); if (e.note == seqNote) seqGate = false; }
        }
    }

    void process (const ProcessArgs& args) override
    {
        if (args.sampleRate != preparedSr) { preparedSr = args.sampleRate; voice.prepare (args.sampleRate); readParams(); }
        bool event = false;
        if (paramDiv.process()) readParams();

        // the playing pattern
        const int slot = clamp ((int) std::round (params[PATTERN_PARAM].getValue() + inputs[PATTERN_INPUT].getVoltage() * acidseq::slots / 10.f), 0, acidseq::slots - 1);
        curSlot.store (slot, std::memory_order_relaxed);
        const int rev = bankRev.load();
        if ((rev != audioRev || slot != audioSlot) && bankMutex.try_lock())
        {
            audioPat = bank[slot];
            bankMutex.unlock();
            audioRev = rev; audioSlot = slot;
        }

        // transport
        acidseq::Settings ss;
        ss.swing = params[SWING_PARAM].getValue();
        ss.gateFrac = (float) acidmaps::gateFrac (params[GATE_PARAM].getValue());
        const bool run = params[RUN_PARAM].getValue() > 0.5f;
        const bool rBtn = resetBtn.process (params[RESET_PARAM].getValue());
        const bool rIn = resetTrig.process (inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
        if (rBtn || rIn)
        {
            seq.process (audioPat, ss, false, 0.0, 0.0, evs);
            applySeq(); event = event || ! evs.empty();
            clock.reset();
        }
        const bool edge = clockTrig.process (inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
        const bool ext = inputs[CLOCK_INPUT].isConnected();
        if (ext != extClock) { extClock = ext; clock.reset(); }   // pulse counting starts over from step 1
        const double bpm = params[TEMPO_PARAM].getValue();
        double a = 0.0, b = 0.0;
        bool playing = run;
        if (run)
        {
            if (ext) playing = clock.tickExternal (edge, args.sampleRate * 60.0 / bpm / 4.0, a, b);
            else clock.tickInternal (bpm, args.sampleTime, a, b);
        }
        seq.process (audioPat, ss, playing, a, b, evs);
        applySeq(); event = event || ! evs.empty();
        playStep.store (seq.currentStep(), std::memory_order_relaxed);

        // play from other modules: a pitch change while the gate is high is a slide
        if (inputs[GATE_INPUT].isConnected())
        {
            gateTrig.process (inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f);
            const bool g = gateTrig.isHigh();
            // ponytail: V/OCT is rounded to the semitone (the voice keys notes by number); pitch CV between notes needs a bend input
            const int n = clamp ((int) std::round (inputs[VOCT_INPUT].getVoltage() * 12.f) + 60, 0, 127);
            const float vel = inputs[ACCENT_INPUT].getVoltage() >= 1.f ? 1.f : 0.7f;
            if (g && ! extGate) { voice.noteOn (n, vel, ap); extNote = n; event = true; }
            else if (g && n != extNote) { voice.noteOn (n, vel, ap); voice.noteOff (extNote, ap); extNote = n; event = true; }
            else if (! g && extGate) { voice.noteOff (extNote, ap); event = true; }
            extGate = g;
        }
        else if (extGate) { voice.noteOff (extNote, ap); extGate = false; event = true; }

        if (event) voice.setParams (ap);   // accent and the decay floor change with the note
        outputs[AUDIO_OUTPUT].setVoltage (5.f * std::tanh (1.6f * voice.process() * ap.volume));
        outputs[SEQ_VOCT_OUTPUT].setVoltage ((seqNote - 60) / 12.f);
        outputs[SEQ_GATE_OUTPUT].setVoltage (seqGate ? 10.f : 0.f);
        outputs[SEQ_ACCENT_OUTPUT].setVoltage (seqGate && seqAcc ? 10.f : 0.f);
        lights[RUN_LIGHT].setBrightness (run ? 1.f : 0.f);
    }

    void onReset() override   // engine is paused
    {
        { std::lock_guard<std::mutex> lock (bankMutex); for (Pattern& p : bank) p = Pattern(); ++bankRev; }
        seq.process (audioPat, acidseq::Settings(), false, 0.0, 0.0, evs);
        voice.reset(); clock.reset(); extGate = seqGate = false;
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_t* pats = json_array();
        std::lock_guard<std::mutex> lock (bankMutex);
        for (const Pattern& p : bank)
        {
            json_t* j = json_loads (acidseq::toJson (p).c_str(), 0, nullptr);
            json_array_append_new (pats, j ? j : json_object());
        }
        json_object_set_new (root, "patterns", pats);
        return root;
    }

    void dataFromJson (json_t* root) override
    {
        json_t* pats = json_object_get (root, "patterns");
        if (! json_is_array (pats)) return;
        std::lock_guard<std::mutex> lock (bankMutex);
        for (int i = 0; i < acidseq::slots; ++i) bank[i] = patternFromJson (json_array_get (pats, (size_t) i));   // a missing slot comes out empty
        ++bankRev;
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// The step grid: header row (click sets the length), 13 pitch rows C' .. C, then lanes TIE, OCT, ACC, SLD.

static const int gridRows = 18, pitchTop = 1, laneTie = 14, laneOct = 15, laneAcc = 16, laneSld = 17;
static const float gridLabelW = 9.f;   // mm

struct StepGrid : OpaqueWidget
{
    AcidBass* module = nullptr;
    std::shared_ptr<Font> font;

    float labelW() const { return mm2px (gridLabelW); }
    float colW() const { return (box.size.x - labelW()) / acidseq::maxSteps; }
    float rowH() const { return box.size.y / gridRows; }

    void onButton (const ButtonEvent& e) override
    {
        if (! module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) { OpaqueWidget::onButton (e); return; }
        const int col = (int) std::floor ((e.pos.x - labelW()) / colW()), row = (int) std::floor (e.pos.y / rowH());
        if (col < 0 || col >= acidseq::maxSteps || row < 0 || row >= gridRows) { OpaqueWidget::onButton (e); return; }
        e.consume (this);
        module->edit (module->curSlot.load(), [&] (Pattern& p) {
            Step& s = p.steps[col];
            if (row == 0) p.len = col + 1;
            else if (row < laneTie)
            {
                const int note = 12 - (row - pitchTop);
                if (s.gate == Gate::Note && s.note == note) s.gate = Gate::Rest;
                else { s.gate = Gate::Note; s.note = note; }
            }
            else if (row == laneTie) s.gate = s.gate == Gate::Tie ? Gate::Rest : Gate::Tie;
            else if (row == laneOct) s.oct = s.oct == 0 ? 1 : s.oct == 1 ? -1 : 0;
            else if (row == laneAcc) s.accent = ! s.accent;
            else if (row == laneSld) s.slide = ! s.slide;
        });
    }

    void drawLayer (const DrawArgs& args, int layer) override
    {
        if (layer != 1) { OpaqueWidget::drawLayer (args, layer); return; }
        NVGcontext* vg = args.vg;
        nvgBeginPath (vg); nvgRect (vg, 0, 0, box.size.x, box.size.y); nvgFillColor (vg, nvgRGB (14, 14, 16)); nvgFill (vg);
        if (! font) font = APP->window->loadFont (asset::system ("res/fonts/ShareTechMono-Regular.ttf"));
        Pattern p;
        int step = -1, slot = 0;
        if (module) { slot = module->curSlot.load(); p = module->bank[slot]; step = module->playStep.load(); }   // UI thread: the only writer
        const float lw = labelW(), cw = colW(), rh = rowH();
        nvgScissor (vg, 0, 0, box.size.x, box.size.y);
        auto cell = [&] (int c, int r, NVGcolor col, float inset) {
            nvgBeginPath (vg); nvgRect (vg, lw + c * cw + inset, r * rh + inset, cw - 2 * inset, rh - 2 * inset); nvgFillColor (vg, col); nvgFill (vg);
        };
        for (int c = 0; c < acidseq::maxSteps; ++c)
        {
            const bool in = c < p.len;
            NVGcolor bg = ! in ? nvgRGB (22, 22, 24) : (c / 4) % 2 ? nvgRGB (36, 36, 40) : nvgRGB (46, 46, 52);
            if (c == step) bg = nvgRGB (90, 80, 50);
            for (int r = 1; r < gridRows; ++r) cell (c, r, bg, 0.5f);
            cell (c, 0, in ? nvgRGB (70, 70, 78) : nvgRGB (30, 30, 34), 0.5f);
        }
        for (int r = laneTie; r < gridRows; ++r) { nvgBeginPath (vg); nvgRect (vg, 0, r * rh, box.size.x, 0.6f); nvgFillColor (vg, nvgRGB (90, 90, 96)); nvgFill (vg); }
        const NVGcolor noteCol = nvgRGB (240, 170, 40), accCol = nvgRGB (255, 80, 40), dimCol = nvgRGB (120, 90, 40), laneCol = nvgRGB (90, 200, 230);
        const int L = std::max (1, p.len);
        for (int c = 0; c < acidseq::maxSteps; ++c)
        {
            const Step& s = p.steps[c];
            const float fade = c < p.len ? 1.f : 0.35f;
            auto faded = [&] (NVGcolor x) { x.a *= fade; return x; };
            if (s.gate == Gate::Note) cell (c, pitchTop + 12 - s.note, faded (s.accent ? accCol : noteCol), 1.5f);
            else if (s.gate == Gate::Tie)
            {
                cell (c, laneTie, faded (laneCol), 1.5f);
                for (int b = 1; b <= L; ++b)   // show the pitch the tie holds
                {
                    const Step& q = p.steps[((c - b) % L + L) % L];
                    if (q.gate == Gate::Note) { cell (c, pitchTop + 12 - q.note, faded (dimCol), 1.5f); break; }
                    if (q.gate == Gate::Rest) break;
                }
            }
            if (s.accent) cell (c, laneAcc, faded (accCol), 1.5f);
            if (s.slide) cell (c, laneSld, faded (laneCol), 1.5f);
            if (s.oct != 0) cell (c, laneOct, faded (s.oct > 0 ? nvgRGB (120, 220, 120) : nvgRGB (200, 120, 220)), 1.5f);
        }
        if (font && font->handle >= 0)
        {
            nvgFontFaceId (vg, font->handle); nvgFontSize (vg, std::min (9.f, rh * 0.95f));
            nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            static const char* names[13] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C'" };
            static const char* lanes[4] = { "TIE", "OCT", "ACC", "SLD" };
            nvgFillColor (vg, nvgRGB (230, 200, 120));
            nvgText (vg, lw * 0.5f, rh * 0.5f, acidseq::slotName (slot).c_str(), NULL);
            for (int r = 0; r < 13; ++r)
            {
                const bool black = std::strchr (names[12 - r], '#') != nullptr;
                nvgFillColor (vg, black ? nvgRGB (130, 130, 140) : nvgRGB (210, 210, 220));
                nvgText (vg, lw * 0.5f, (pitchTop + r + 0.5f) * rh, names[12 - r], NULL);
            }
            nvgFillColor (vg, nvgRGB (170, 210, 230));
            for (int r = 0; r < 4; ++r) nvgText (vg, lw * 0.5f, (laneTie + r + 0.5f) * rh, lanes[r], NULL);
            for (int c = 0; c < acidseq::maxSteps; ++c)
            {
                nvgFillColor (vg, c < p.len ? nvgRGB (230, 230, 235) : nvgRGB (90, 90, 96));
                nvgText (vg, lw + (c + 0.5f) * cw, rh * 0.5f, string::f ("%d", c + 1).c_str(), NULL);
                const Step& s = p.steps[c];
                if (s.oct != 0) { nvgFillColor (vg, nvgRGB (14, 14, 16)); nvgText (vg, lw + (c + 0.5f) * cw, (laneOct + 0.5f) * rh, s.oct > 0 ? "+" : "-", NULL); }
            }
        }
        nvgResetScissor (vg);
        OpaqueWidget::drawLayer (args, layer);
    }
};

// Static panel text in nanovg (Rack does not draw SVG <text>). Positions in mm, centred.
struct PanelLabels : TransparentWidget
{
    std::shared_ptr<Font> font;
    void draw (const DrawArgs& args) override
    {
        if (! font) font = APP->window->loadFont (asset::system ("res/fonts/ShareTechMono-Regular.ttf"));
        if (! font || font->handle < 0) return;
        struct L { const char* text; float xmm, ymm, size; };
        static const L labels[] = {
            { "MY LIL ACID BASS", 101.6f, 5.5f, 12.f },
            { "TUNE", 13.f, 78.5f, 9.f }, { "CUTOFF", 31.f, 78.5f, 9.f }, { "RES", 49.f, 78.5f, 9.f }, { "ENV MOD", 67.f, 78.5f, 9.f },
            { "DECAY", 85.f, 78.5f, 9.f }, { "ACCENT", 103.f, 78.5f, 9.f }, { "VOLUME", 121.f, 78.5f, 9.f }, { "WAVE", 137.f, 78.5f, 9.f },
            { "TEMPO", 155.f, 78.5f, 9.f }, { "SWING", 172.f, 78.5f, 9.f }, { "GATE", 188.f, 78.5f, 9.f },
            { "F.ATK", 11.f, 93.f, 7.5f }, { "F.SUS", 21.f, 93.f, 7.5f }, { "F.REL", 31.f, 93.f, 7.5f },
            { "A.ATK", 45.f, 93.f, 7.5f }, { "A.DEC", 55.f, 93.f, 7.5f }, { "A.SUS", 65.f, 93.f, 7.5f }, { "A.REL", 75.f, 93.f, 7.5f },
            { "HP", 89.f, 93.f, 7.5f }, { "KEY", 99.f, 93.f, 7.5f }, { "MIN.D", 109.f, 93.f, 7.5f }, { "PW", 119.f, 93.f, 7.5f },
            { "LFO", 129.f, 93.f, 7.5f }, { "LFO>PW", 139.f, 93.f, 7.5f },
            { "PATTERN", 155.f, 93.f, 7.5f }, { "RUN", 172.f, 93.f, 7.5f }, { "RESET", 188.f, 93.f, 7.5f },
            { "SEQ IN", 22.f, 98.5f, 7.5f }, { "PLAY IN", 59.f, 98.5f, 7.5f }, { "CV IN", 107.f, 98.5f, 7.5f }, { "SEQ OUT", 159.f, 98.5f, 7.5f }, { "OUT", 189.f, 98.5f, 7.5f },
            { "CLOCK", 11.f, 111.5f, 7.5f }, { "RESET", 22.f, 111.5f, 7.5f }, { "PAT", 33.f, 111.5f, 7.5f },
            { "V/OCT", 48.f, 111.5f, 7.5f }, { "GATE", 59.f, 111.5f, 7.5f }, { "ACC", 70.f, 111.5f, 7.5f },
            { "CUT", 85.f, 111.5f, 7.5f }, { "RES", 96.f, 111.5f, 7.5f }, { "ENV", 107.f, 111.5f, 7.5f }, { "DEC", 118.f, 111.5f, 7.5f }, { "ACC", 129.f, 111.5f, 7.5f },
            { "V/OCT", 148.f, 111.5f, 7.5f }, { "GATE", 159.f, 111.5f, 7.5f }, { "ACC", 170.f, 111.5f, 7.5f }, { "AUDIO", 189.f, 111.5f, 7.5f },
            { "A_DEVELOPMENT", 101.6f, 121.f, 8.f },
        };
        NVGcontext* vg = args.vg;
        nvgFontFaceId (vg, font->handle); nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); nvgFillColor (vg, nvgRGB (34, 34, 34));
        for (const L& l : labels) { nvgFontSize (vg, l.size); Vec p = mm2px (Vec (l.xmm, l.ymm)); nvgText (vg, p.x, p.y, l.text, NULL); }
    }
};

struct AcidBassWidget : ModuleWidget
{
    AcidBassWidget (AcidBass* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/AcidBass.svg")));
        { PanelLabels* pl = createWidget<PanelLabels> (Vec (0, 0)); pl->box.size = box.size; addChild (pl); }
        addChild (createWidget<ScrewSilver> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewSilver> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewSilver> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewSilver> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        StepGrid* g = createWidget<StepGrid> (mm2px (Vec (5.f, 10.f)));
        g->box.size = mm2px (Vec (193.2f, 50.f)); g->module = module; addChild (g);

        const float r1 = 70.f, r2 = 87.f, r3 = 105.f;
        const float big[7] = { 13.f, 31.f, 49.f, 67.f, 85.f, 103.f, 121.f };
        for (int i = 0; i < 7; ++i) addParam (createParamCentered<RoundBlackKnob> (mm2px (Vec (big[i], r1)), module, AcidBass::TUNE_PARAM + i));
        addParam (createParamCentered<CKSS> (mm2px (Vec (137.f, r1)), module, AcidBass::WAVE_PARAM));
        addParam (createParamCentered<RoundBlackKnob> (mm2px (Vec (155.f, r1)), module, AcidBass::TEMPO_PARAM));
        addParam (createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (172.f, r1)), module, AcidBass::SWING_PARAM));
        addParam (createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (188.f, r1)), module, AcidBass::GATE_PARAM));

        const float trim[13] = { 11.f, 21.f, 31.f, 45.f, 55.f, 65.f, 75.f, 89.f, 99.f, 109.f, 119.f, 129.f, 139.f };
        for (int i = 0; i < 13; ++i) addParam (createParamCentered<Trimpot> (mm2px (Vec (trim[i], r2)), module, AcidBass::FATK_PARAM + i));
        addParam (createParamCentered<RoundSmallBlackKnob> (mm2px (Vec (155.f, r2)), module, AcidBass::PATTERN_PARAM));
        addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>> (mm2px (Vec (172.f, r2)), module, AcidBass::RUN_PARAM, AcidBass::RUN_LIGHT));
        addParam (createParamCentered<VCVButton> (mm2px (Vec (188.f, r2)), module, AcidBass::RESET_PARAM));

        const float ins[11] = { 11.f, 22.f, 33.f, 48.f, 59.f, 70.f, 85.f, 96.f, 107.f, 118.f, 129.f };
        for (int i = 0; i < 11; ++i) addInput (createInputCentered<PJ301MPort> (mm2px (Vec (ins[i], r3)), module, AcidBass::CLOCK_INPUT + i));
        const float outs[4] = { 148.f, 159.f, 170.f, 189.f };
        for (int i = 0; i < 4; ++i) addOutput (createOutputCentered<PJ301MPort> (mm2px (Vec (outs[i], r3)), module, AcidBass::SEQ_VOCT_OUTPUT + i));
    }

    void appendContextMenu (Menu* menu) override
    {
        AcidBass* m = dynamic_cast<AcidBass*> (module);
        if (! m) return;
        const int slot = m->curSlot.load();
        menu->addChild (new MenuSeparator);
        menu->addChild (createMenuLabel ("Pattern " + acidseq::slotName (slot) + " (the one the PATTERN knob picks)"));
        menu->addChild (createMenuItem ("Copy as step notation", "", [=]() {
            glfwSetClipboardString (APP->window->win, acidseq::formatNotation (m->bank[slot]).c_str());
        }));
        menu->addChild (createMenuItem ("Copy as JSON (web app / FL plugin)", "", [=]() {
            glfwSetClipboardString (APP->window->win, acidseq::toJson (m->bank[slot]).c_str());
        }));
        menu->addChild (createMenuItem ("Paste step notation or JSON", "", [=]() {
            const char* clip = glfwGetClipboardString (APP->window->win);
            std::string text = clip ? clip : "";
            std::string body;   // drop "# ..." comment lines, as in the web app's exported .txt files
            size_t i = 0;
            while (i <= text.size())
            {
                size_t j = text.find ('\n', i); if (j == std::string::npos) j = text.size();
                const std::string line = text.substr (i, j - i);
                const size_t k = line.find_first_not_of (" \t\r");
                if (k != std::string::npos && line[k] != '#') body += line + " ";
                i = j + 1;
            }
            Pattern p;
            const size_t k = body.find_first_not_of (" \t\r\n");
            bool ok = false;
            if (k != std::string::npos && body[k] == '{')
            {
                json_error_t err;
                json_t* j = json_loads (body.c_str(), 0, &err);
                if (json_is_object (j)) { p = patternFromJson (j); ok = true; }
                if (j) json_decref (j);
            }
            else ok = acidseq::parseNotation (body, p);
            if (! ok)
            {
                osdialog_message (OSDIALOG_WARNING, OSDIALOG_OK, "The clipboard holds no pattern.\n\nUse step notation (for example: A* - A A^ - A_* A - G~ A) or a pattern's JSON from the web app.");
                return;
            }
            m->edit (slot, [&] (Pattern& dst) { dst = p; });
        }));
        menu->addChild (createMenuItem ("Clear pattern", "", [=]() { m->edit (slot, [] (Pattern& dst) { dst = Pattern(); }); }));
        menu->addChild (new MenuSeparator);
        menu->addChild (createMenuItem ("Load demo set (I-A1..I-A6, and its sound)", "", [=]() {
            for (int i = 0; i < 6; ++i) { Pattern p; if (acidseq::parseNotation (acidseq::demo[i], p)) m->edit (i, [&] (Pattern& dst) { dst = p; }); }
            const struct { int id; float v; } sound[] = {
                { AcidBass::CUTOFF_PARAM, cutoffKnob (265.3f) }, { AcidBass::RES_PARAM, 0.72f }, { AcidBass::ENVMOD_PARAM, 0.75f },
                { AcidBass::DECAY_PARAM, 0.59f }, { AcidBass::ACCENT_PARAM, 0.8f }, { AcidBass::VOLUME_PARAM, 0.7f },
                { AcidBass::TEMPO_PARAM, 134.f }, { AcidBass::PATTERN_PARAM, 0.f },
            };
            for (const auto& s : sound) m->paramQuantities[s.id]->setValue (s.v);
        }));
    }
};

Model* modelAcidBass = createModel<AcidBass, AcidBassWidget> ("AcidBass");
