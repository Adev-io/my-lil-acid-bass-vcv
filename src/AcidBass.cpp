// AcidBass.cpp — the Rack module: the acid voice, the step sequencer, 32 pattern slots and the step grid on the panel.
// Author: Ari Klopfer / A_Development · MIT
#include "AcidVoice.h"
#include "Clock.h"
#include "Sequencer.h"
#include "layout.hpp"
#include "TalkingFace.hpp"
#include "plugin.hpp"
#include <osdialog.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

using acidseq::Gate;
using acidseq::Pattern;
using acidseq::Step;

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
        TEMPO_PARAM, SWING_PARAM, GATE_PARAM, PATTERN_PARAM, RUN_PARAM, RESET_PARAM,
        LENGTH_PARAM,   // last, so patches saved before it keep their param ids
        PARAMS_LEN };
    enum InputId { CLOCK_INPUT, RESET_INPUT, PATTERN_INPUT, VOCT_INPUT, GATE_INPUT, ACCENT_INPUT,
        CUTOFF_CV_INPUT, RES_CV_INPUT, ENVMOD_CV_INPUT, DECAY_CV_INPUT, ACCENT_CV_INPUT, INPUTS_LEN };
    enum OutputId { SEQ_VOCT_OUTPUT, SEQ_GATE_OUTPUT, SEQ_ACCENT_OUTPUT, AUDIO_OUTPUT, FENV_OUTPUT, AENV_OUTPUT, OUTPUTS_LEN };
    enum LightId { RUN_LIGHT, SQUARE_LIGHT, ENV_LIGHT, LIGHTS_LEN };

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
    FaceTap face;

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
        configParam (LENGTH_PARAM, 1.f, acidseq::maxSteps, 16.f, "Pattern length", " steps");   // mirrors the playing pattern's length (see the widget's step())
        paramQuantities[LENGTH_PARAM]->snapEnabled = true;
        paramQuantities[LENGTH_PARAM]->randomizeEnabled = false;
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
        configOutput (FENV_OUTPUT, "Filter envelope (0-10 V)");
        configOutput (AENV_OUTPUT, "Amp envelope (0-10 V)");
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
        else if (ext) clock.idleExternal (edge);
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
        face.push (outputs[AUDIO_OUTPUT].getVoltage() / 5.f, args.sampleRate);
        outputs[SEQ_VOCT_OUTPUT].setVoltage ((seqNote - 60) / 12.f);
        outputs[SEQ_GATE_OUTPUT].setVoltage (seqGate ? 10.f : 0.f);
        outputs[SEQ_ACCENT_OUTPUT].setVoltage (seqGate && seqAcc ? 10.f : 0.f);
        outputs[FENV_OUTPUT].setVoltage (10.f * voice.filterEnv());
        outputs[AENV_OUTPUT].setVoltage (10.f * voice.ampEnvelope());
        lights[RUN_LIGHT].setBrightness (run ? 1.f : 0.f);
        lights[SQUARE_LIGHT].setBrightness (ap.square ? 1.f : 0.f);
        lights[ENV_LIGHT].setBrightnessSmooth (voice.filterEnv(), args.sampleTime);
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
// Undo for pattern edits (grid clicks, paste, clear, demo set). Rack itself records knob turns as parameter changes, so
// the LENGTH knob's sync in AcidBassWidget::step() records nothing: recording it too would need two Undos per turn.
// Actions hold the moduleId, never a pointer: Rack may delete and restore the module between push and undo.

struct PatternChange : history::ModuleAction
{
    int slot = 0;
    Pattern before, after;
    // An action restores the length only if it changed it, so a length set outside history (e.g. MIDI-Map on LENGTH) survives the Undo of a step edit.
    void set (const Pattern& p) const
    {
        AcidBass* m = dynamic_cast<AcidBass*> (APP->engine->getModule (moduleId));
        if (m) m->edit (slot, [&] (Pattern& dst) { const int keep = dst.len; dst = p; if (before.len == after.len) dst.len = keep; });
    }
    void undo() override { set (before); }
    void redo() override { set (after); }
};

// Applies f to one slot through edit() and returns the undo step for it, or nullptr if f changed nothing
// (for example, a click on the step number that already is the length). UI thread only, like edit().
template <typename F>
static PatternChange* editRecorded (AcidBass* m, int slot, const char* name, F f)
{
    slot = clamp (slot, 0, acidseq::slots - 1);
    PatternChange* a = new PatternChange;
    a->moduleId = m->id; a->slot = slot; a->name = name;
    a->before = m->bank[slot];
    m->edit (slot, f);
    a->after = m->bank[slot];
    if (acidseq::toJson (a->before) == acidseq::toJson (a->after)) { delete a; return nullptr; }
    return a;
}

static void pushUndo (history::Action* a) { if (a) APP->history->push (a); }

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
        pushUndo (editRecorded (module, module->curSlot.load(), row == 0 ? "set pattern length" : "edit step", [&] (Pattern& p) {
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
        }));
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
            if (c == step) bg = nvgRGB (66, 84, 28);
            for (int r = 1; r < gridRows; ++r) cell (c, r, bg, 0.5f);
            cell (c, 0, in ? nvgRGB (70, 70, 78) : nvgRGB (30, 30, 34), 0.5f);
        }
        for (int r = laneTie; r < gridRows; ++r) { nvgBeginPath (vg); nvgRect (vg, 0, r * rh, box.size.x, 0.6f); nvgFillColor (vg, nvgRGB (90, 90, 96)); nvgFill (vg); }
        const NVGcolor noteCol = nvgRGB (200, 255, 58), accCol = nvgRGB (255, 61, 154), dimCol = nvgRGB (96, 120, 36), laneCol = nvgRGB (90, 200, 230);
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
            nvgFillColor (vg, nvgRGB (200, 255, 58));
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

struct AcidBassWidget : ModuleWidget
{
    AcidBassWidget (AcidBass* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/AcidBass.svg")));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        TalkingFace* f = createWidget<TalkingFace> (mm2px (Vec (layout::bass::FACE[0] - layout::bass::FACE[2], layout::bass::FACE[1] - layout::bass::FACE[2])));
        f->box.size = mm2px (Vec (2 * layout::bass::FACE[2], 2 * layout::bass::FACE[2])); if (module) f->tap = &module->face; addChild (f);

        StepGrid* g = createWidget<StepGrid> (mm2px (Vec (5.f, 10.f)));
        g->box.size = mm2px (Vec (193.2f, 50.f)); g->module = module; addChild (g);

        namespace L = layout::bass;
        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };
        addChild (glowRing (at (L::CUTOFF_PARAM), module, AcidBass::ENV_LIGHT, 8.8f));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::TUNE_PARAM), module, AcidBass::TUNE_PARAM));
        addParam (createParamCentered<Rogan3PSRed> (at (L::CUTOFF_PARAM), module, AcidBass::CUTOFF_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::RES_PARAM), module, AcidBass::RES_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::ENVMOD_PARAM), module, AcidBass::ENVMOD_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::DECAY_PARAM), module, AcidBass::DECAY_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::ACCENT_PARAM), module, AcidBass::ACCENT_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::VOLUME_PARAM), module, AcidBass::VOLUME_PARAM));
        addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (L::WAVE_PARAM), module, AcidBass::WAVE_PARAM, AcidBass::SQUARE_LIGHT));
        addParam (createParamCentered<Rogan1PSBlue> (at (L::TEMPO_PARAM), module, AcidBass::TEMPO_PARAM));
        addParam (createParamCentered<BefacoTinyKnob> (at (L::SWING_PARAM), module, AcidBass::SWING_PARAM));
        addParam (createParamCentered<BefacoTinyKnob> (at (L::GATE_PARAM), module, AcidBass::GATE_PARAM));
        static const float* const trims[13] = { L::FATK_PARAM, L::FSUS_PARAM, L::FREL_PARAM, L::AATK_PARAM, L::ADEC_PARAM, L::ASUS_PARAM, L::AREL_PARAM,
            L::HP_PARAM, L::KFOL_PARAM, L::MIND_PARAM, L::PW_PARAM, L::LFOR_PARAM, L::LFOPW_PARAM };
        for (int i = 0; i < 13; ++i) addParam (createParamCentered<Trimpot> (at (trims[i]), module, AcidBass::FATK_PARAM + i));
        addParam (createParamCentered<BefacoTinyKnob> (at (L::PATTERN_PARAM), module, AcidBass::PATTERN_PARAM));
        addParam (createParamCentered<BefacoTinyKnob> (at (L::LENGTH_PARAM), module, AcidBass::LENGTH_PARAM));
        addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<BlueLight>>> (at (L::RUN_PARAM), module, AcidBass::RUN_PARAM, AcidBass::RUN_LIGHT));
        addParam (createParamCentered<VCVButton> (at (L::RESET_PARAM), module, AcidBass::RESET_PARAM));

        static const float* const ins[11] = { L::CLOCK_INPUT, L::RESET_INPUT, L::PATTERN_INPUT, L::VOCT_INPUT, L::GATE_INPUT, L::ACCENT_INPUT,
            L::CUTOFF_CV_INPUT, L::RES_CV_INPUT, L::ENVMOD_CV_INPUT, L::DECAY_CV_INPUT, L::ACCENT_CV_INPUT };
        for (int i = 0; i < 11; ++i) addInput (createInputCentered<PJ301MPort> (at (ins[i]), module, AcidBass::CLOCK_INPUT + i));
        static const float* const outs[4] = { L::SEQ_VOCT_OUTPUT, L::SEQ_GATE_OUTPUT, L::SEQ_ACCENT_OUTPUT, L::AUDIO_OUTPUT };
        for (int i = 0; i < 4; ++i) addOutput (createOutputCentered<PJ301MPort> (at (outs[i]), module, AcidBass::SEQ_VOCT_OUTPUT + i));
        addOutput (createOutputCentered<PJ301MPort> (at (L::FENV_OUTPUT), module, AcidBass::FENV_OUTPUT));
        addOutput (createOutputCentered<PJ301MPort> (at (L::AENV_OUTPUT), module, AcidBass::AENV_OUTPUT));
    }

    // LENGTH knob <-> the playing pattern's length, on the UI thread (the only pattern writer). A turn of the knob sets
    // the length; anything else that changes it (another slot, a header click, a paste) moves the knob to match.
    int lastKnob = -1, lastSlot = -1;
    void step() override
    {
        ModuleWidget::step();
        AcidBass* m = dynamic_cast<AcidBass*> (module);
        if (! m) return;
        const int slot = m->curSlot.load();
        Param& knob = m->params[AcidBass::LENGTH_PARAM];
        const int k = (int) std::round (knob.getValue()), len = clamp (m->bank[slot].len, 1, acidseq::maxSteps);
        if (k != lastKnob && slot == lastSlot) { if (k != len) m->edit (slot, [&] (Pattern& p) { p.len = k; }); }
        else if (k != len) knob.setValue ((float) len);
        lastKnob = (int) std::round (knob.getValue()); lastSlot = slot;
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
            pushUndo (editRecorded (m, slot, "paste pattern", [&] (Pattern& dst) { dst = p; }));
        }));
        menu->addChild (createMenuItem ("Clear pattern", "", [=]() { pushUndo (editRecorded (m, slot, "clear pattern", [] (Pattern& dst) { dst = Pattern(); })); }));
        menu->addChild (new MenuSeparator);
        menu->addChild (createMenuItem ("Load demo set (I-A1..I-A6, and its sound)", "", [=]() {
            history::ComplexAction* all = new history::ComplexAction;
            all->name = "load demo set";
            for (int i = 0; i < 6; ++i)
            {
                Pattern p;
                if (! acidseq::parseNotation (acidseq::demo[i], p)) continue;
                if (PatternChange* a = editRecorded (m, i, "load demo set", [&] (Pattern& dst) { dst = p; })) all->push (a);
            }
            const struct { int id; float v; } sound[] = {
                { AcidBass::CUTOFF_PARAM, cutoffKnob (265.3f) }, { AcidBass::RES_PARAM, 0.72f }, { AcidBass::ENVMOD_PARAM, 0.75f },
                { AcidBass::DECAY_PARAM, 0.59f }, { AcidBass::ACCENT_PARAM, 0.8f }, { AcidBass::VOLUME_PARAM, 0.7f },
                { AcidBass::TEMPO_PARAM, 134.f }, { AcidBass::PATTERN_PARAM, 0.f },
            };
            for (const auto& s : sound)
            {
                const float before = m->paramQuantities[s.id]->getValue();
                if (before == s.v) continue;
                m->paramQuantities[s.id]->setValue (s.v);
                history::ParamChange* pc = new history::ParamChange;
                pc->moduleId = m->id; pc->paramId = s.id;
                pc->oldValue = before; pc->newValue = m->paramQuantities[s.id]->getValue();   // as stored (clamped/snapped)
                all->push (pc);
            }
            if (all->isEmpty()) delete all; else APP->history->push (all);
        }));
    }
};

Model* modelAcidBass = createModel<AcidBass, AcidBassWidget> ("AcidBass");
