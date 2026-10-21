// plugin.hpp — the plugin's models, plus the bits both modules share: knob maps, panel-colour lights, the cutoff glow.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <rack.hpp>
#include <cmath>
using namespace rack;
extern Plugin* pluginInstance;
extern Model* modelAcidBass;
extern Model* modelAcidVoice;

static const float cutLo = 60.f, cutHi = 8000.f;   // the plugin's cutoff range, exponential on the knob
inline float cutoffKnob (float hz) { return std::log (hz / cutLo) / std::log (cutHi / cutLo); }
inline float decayKnob (float sec) { return std::log (sec / 0.03f) / std::log (83.f); }   // inverse of acidmaps::envDecSec

// The panel colours as lights (tools/panel.py uses the same two).
struct LimeLight : GrayModuleLightWidget { LimeLight() { addBaseColor (nvgRGB (0xc8, 0xff, 0x3a)); } };
struct MagentaLight : GrayModuleLightWidget { MagentaLight() { addBaseColor (nvgRGB (0xff, 0x3d, 0x9a)); } };

// A lime halo behind a knob that breathes with a light's brightness: the filter envelope under CUTOFF, so the sweep you
// hear is the sweep you see. Draws on layer 1 (self-lit), behind the knob added after it.
struct GlowRing : TransparentWidget
{
    Module* module = nullptr;
    int lightId = 0;
    float radius = 0.f;   // px, the knob's radius

    void drawLayer (const DrawArgs& args, int layer) override
    {
        if (layer != 1 || ! module) return;
        const float b = module->lights[lightId].getBrightness();
        if (b <= 0.01f) return;
        NVGcontext* vg = args.vg;
        const Vec c = box.size.div (2.f);
        const float r = radius * (1.05f + 0.3f * b);
        NVGpaint p = nvgRadialGradient (vg, c.x, c.y, radius * 0.9f, r, nvgRGBAf (0.78f, 1.f, 0.23f, 0.9f * b), nvgRGBAf (0.78f, 1.f, 0.23f, 0.f));
        nvgBeginPath (vg); nvgCircle (vg, c.x, c.y, r); nvgFillPaint (vg, p); nvgFill (vg);
    }
};

inline GlowRing* glowRing (Vec center, Module* module, int lightId, float knobRadiusMm)
{
    GlowRing* g = new GlowRing;
    g->box.size = mm2px (Vec (knobRadiusMm * 2.8f, knobRadiusMm * 2.8f));
    g->box.pos = center.minus (g->box.size.div (2.f));
    g->module = module; g->lightId = lightId; g->radius = mm2px (knobRadiusMm);
    return g;
}
