// VoiceModule.cpp — Acid Voice: the acid voice on its own, 14 HP, for anyone sequencing it from another module.
// V/Oct, gate, accent and slide in; cutoff and resonance CV; audio out. Same voice and knob maps as Acid Bass.
// Author: Ari Klopfer / A_Development · MIT
#include "AcidVoice.h"
#include "TalkingFace.hpp"
#include "layout.hpp"
#include "plugin.hpp"
#include <cmath>

struct AcidVoiceModule : Module
{
    enum ParamId { TUNE_PARAM, CUTOFF_PARAM, RES_PARAM, ENVMOD_PARAM, DECAY_PARAM, ACCENT_PARAM, VOLUME_PARAM, WAVE_PARAM, CUTOFF_CV_PARAM, PARAMS_LEN };
    enum InputId { VOCT_INPUT, GATE_INPUT, ACCENT_INPUT, SLIDE_INPUT, CUTOFF_CV_INPUT, RES_CV_INPUT, INPUTS_LEN };
    enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
    enum LightId { GATE_LIGHT, ACCENT_LIGHT, SLIDE_LIGHT, SQUARE_LIGHT, ENV_LIGHT, LIGHTS_LEN };

    AcidVoice voice;
    AcidParams ap;   // the Tweaks and envelope extras stay at the web defaults; Acid Bass has the knobs for them
    dsp::SchmittTrigger gateTrig;
    dsp::ClockDivider paramDiv;
    FaceTap face;
    float preparedSr = 0.f;
    bool extGate = false, sliding = false;
    int extNote = 60;

    AcidVoiceModule()
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
        configParam (CUTOFF_CV_PARAM, -1.f, 1.f, 1.f, "Cutoff CV amount (100% = 1 V/oct)", "%", 0.f, 100.f);
        configInput (VOCT_INPUT, "V/OCT (semitones)");
        configInput (GATE_INPUT, "Gate (a pitch change while high slides)");
        configInput (ACCENT_INPUT, "Accent (high at the gate start)");
        configInput (SLIDE_INPUT, "Slide (high at the gate start: glide in from the last note, no retrigger)");
        configInput (CUTOFF_CV_INPUT, "Cutoff CV (scaled by the CUT CV trimpot)");
        configInput (RES_CV_INPUT, "Resonance CV (10 V = full range)");
        configOutput (AUDIO_OUTPUT, "Audio");
        paramDiv.setDivision (32);
    }

    void readParams()
    {
        auto k = [&] (int id) { return params[id].getValue(); };
        ap.tuneCents = k (TUNE_PARAM);
        const float cv = clamp (inputs[CUTOFF_CV_INPUT].getVoltage(), -10.f, 10.f) * k (CUTOFF_CV_PARAM);
        ap.cutoffHz = std::max (5.f, cutLo * std::pow (cutHi / cutLo, k (CUTOFF_PARAM)) * std::pow (2.f, cv));
        ap.res = clamp (k (RES_PARAM) + inputs[RES_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
        ap.envmod = k (ENVMOD_PARAM);
        ap.decaySec = (float) acidmaps::envDecSec (k (DECAY_PARAM));
        ap.accent = k (ACCENT_PARAM);
        ap.volume = k (VOLUME_PARAM);
        ap.square = k (WAVE_PARAM) > 0.5f;
        voice.setParams (ap);
    }

    void process (const ProcessArgs& args) override
    {
        if (args.sampleRate != preparedSr) { preparedSr = args.sampleRate; voice.prepare (args.sampleRate); readParams(); }
        bool event = false;
        if (paramDiv.process()) readParams();

        // a pitch change while the gate is high is a slide; SLIDE high at the gate start slides in from the last note
        gateTrig.process (inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f);
        const bool g = gateTrig.isHigh();
        // ponytail: V/OCT is rounded to the semitone (the voice keys notes by number), as in Acid Bass
        const int n = clamp ((int) std::round (inputs[VOCT_INPUT].getVoltage() * 12.f) + 60, 0, 127);
        const float vel = inputs[ACCENT_INPUT].getVoltage() >= 1.f ? 1.f : 0.7f;
        if (g && ! extGate)
        {
            const bool slide = inputs[SLIDE_INPUT].getVoltage() >= 1.f;
            voice.noteOn (n, vel, ap, slide); extNote = n; sliding = slide; event = true;
        }
        else if (g && n != extNote) { voice.noteOn (n, vel, ap); voice.noteOff (extNote, ap); extNote = n; sliding = true; event = true; }
        else if (! g && extGate) { voice.noteOff (extNote, ap); sliding = false; event = true; }
        extGate = g;

        if (event) voice.setParams (ap);   // accent and the decay floor change with the note
        outputs[AUDIO_OUTPUT].setVoltage (5.f * std::tanh (1.6f * voice.process() * ap.volume));
        face.push (outputs[AUDIO_OUTPUT].getVoltage() / 5.f, args.sampleRate);

        lights[GATE_LIGHT].setBrightnessSmooth (g ? 1.f : 0.f, args.sampleTime);
        lights[ACCENT_LIGHT].setBrightnessSmooth (voice.accentedNow() ? 1.f : 0.f, args.sampleTime);
        lights[SLIDE_LIGHT].setBrightnessSmooth (g && sliding ? 1.f : 0.f, args.sampleTime);
        lights[SQUARE_LIGHT].setBrightness (ap.square ? 1.f : 0.f);
        lights[ENV_LIGHT].setBrightnessSmooth (voice.filterEnv(), args.sampleTime);
    }

    void onReset() override { voice.reset(); extGate = sliding = false; }
};

struct AcidVoiceWidget : ModuleWidget
{
    AcidVoiceWidget (AcidVoiceModule* module)
    {
        namespace L = layout::voice;
        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/AcidVoice.svg")));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        TalkingFace* f = createWidget<TalkingFace> (mm2px (Vec (L::FACE[0] - L::FACE[2], L::FACE[1] - L::FACE[2])));
        f->box.size = mm2px (Vec (2 * L::FACE[2], 2 * L::FACE[2])); if (module) f->tap = &module->face; addChild (f);

        addChild (glowRing (at (L::CUTOFF_PARAM), module, AcidVoiceModule::ENV_LIGHT, 8.8f));
        addParam (createParamCentered<Rogan3PSRed> (at (L::CUTOFF_PARAM), module, AcidVoiceModule::CUTOFF_PARAM));
        addParam (createParamCentered<Rogan2PSWhite> (at (L::RES_PARAM), module, AcidVoiceModule::RES_PARAM));
        addParam (createParamCentered<Rogan2PSWhite> (at (L::ENVMOD_PARAM), module, AcidVoiceModule::ENVMOD_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::DECAY_PARAM), module, AcidVoiceModule::DECAY_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::ACCENT_PARAM), module, AcidVoiceModule::ACCENT_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::VOLUME_PARAM), module, AcidVoiceModule::VOLUME_PARAM));
        addParam (createParamCentered<Trimpot> (at (L::TUNE_PARAM), module, AcidVoiceModule::TUNE_PARAM));
        addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (L::WAVE_PARAM), module, AcidVoiceModule::WAVE_PARAM, AcidVoiceModule::SQUARE_LIGHT));
        addParam (createParamCentered<Trimpot> (at (L::CUTOFF_CV_PARAM), module, AcidVoiceModule::CUTOFF_CV_PARAM));

        addInput (createInputCentered<PJ301MPort> (at (L::VOCT_INPUT), module, AcidVoiceModule::VOCT_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::GATE_INPUT), module, AcidVoiceModule::GATE_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::ACCENT_INPUT), module, AcidVoiceModule::ACCENT_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::SLIDE_INPUT), module, AcidVoiceModule::SLIDE_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::CUTOFF_CV_INPUT), module, AcidVoiceModule::CUTOFF_CV_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::RES_CV_INPUT), module, AcidVoiceModule::RES_CV_INPUT));
        addOutput (createOutputCentered<PJ301MPort> (at (L::AUDIO_OUTPUT), module, AcidVoiceModule::AUDIO_OUTPUT));

        addChild (createLightCentered<MediumLight<LimeLight>> (at (L::GATE_LIGHT), module, AcidVoiceModule::GATE_LIGHT));
        addChild (createLightCentered<MediumLight<MagentaLight>> (at (L::ACCENT_LIGHT), module, AcidVoiceModule::ACCENT_LIGHT));
        addChild (createLightCentered<MediumLight<BlueLight>> (at (L::SLIDE_LIGHT), module, AcidVoiceModule::SLIDE_LIGHT));
    }
};

Model* modelAcidVoice = createModel<AcidVoiceModule, AcidVoiceWidget> ("AcidVoice");
