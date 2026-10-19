# My lil Acid Bass for VCV Rack: design

Date: 2026-10-19. Author: Ari Klopfer / A_Development.

## Goal

The acid voice and the 32-step acid sequencer from My lil acid bassline (web app and FL Studio VST3) as one VCV Rack 2 module.
The FL Studio build is not touched: this repo copies the C++ out of `Desktop\_Projects\my-lil-acid-bassline\plugin\Source`
and never writes to that repo.

## Scope

In: the voice (PolyBLEP saw/square, two cascaded 12 dB lowpasses with resonance on the second, filter and amp ADSR,
accent with the stacking sweep, legato glide, input high-pass, key follow, min decay, pulse width and LFO to PW),
the step sequencer (32 steps, note/rest/tie, octave, accent, slide, swing, gate length), 32 pattern slots (I-A1..II-B8),
a clickable step grid on the panel, and import/export of the web app's step notation and pattern JSON.

Out (v1): drive, delay and reverb (Rack has those), song/track chaining (the PATTERN CV input covers it from another
sequencer), MIDI export, a knob-per-step expander (later, if missed).

## Sound parity

`core/` holds the DSP and sequencer with no Rack and no JUCE in it, so a plain g++ test can run it.
- `Maps.h`: copied unchanged.
- `Svf.h`: the topology-preserving-transform state-variable filter, written out with the same equations as
  `juce::dsp::StateVariableTPTFilter`.
- `AcidVoice.h`: the plugin's voice with the per-block preamble split into `setParams()` and the loop body into `process()`
  (one sample). `render()` keeps the plugin's block call for the tests.
- `Sequencer.h`: the plugin's `stepEvent` and engine, with the block/sample API replaced by "advance the song position
  from ppq0 to ppq1". Notation parse/format and the JSON text writer are ported from `acidbass.js` and `Sequencer.h`.
- `Clock.h`: internal clock (Tempo knob) or external clock (one pulse per 16th), producing contiguous song-position ranges.

Parity checks: the plugin smoke test's recorded default saw/square level and brightness (rms 0.231228 / 0.377944,
brightness 0.004067 / 0.002700, within 3 % and 5 %), its map values, and its sequencer event timings.

## Module

40 HP. Step grid on top (13 pitch rows C..C', lanes TIE, OCT, ACC, SLD, header row sets the length).

Knobs: Tune, Cutoff, Res, Env Mod, Decay, Accent, Volume, Saw/Square; filter A/S/R; amp A/D/S/R; HP, Key Follow,
Min Decay, PW, LFO Rate, LFO>PW; Tempo, Swing, Gate, Pattern; Run (latch), Reset.
Every control is a Rack param, with the plugin's defaults, so MIDI-map, presets and randomize work.

Inputs: CLOCK, RESET, PATTERN (0-10 V over the 32 slots), V/OCT, GATE, ACCENT (play the voice from other modules),
Cutoff (1 V/oct), Res, Env Mod, Decay, Accent (each +-10 V = the knob's full range).
Outputs: SEQ V/OCT, SEQ GATE, SEQ ACCENT (drive any other voice), AUDIO (+-5 V).

State: the 32 patterns are saved in the patch. The audio thread reads a copy that it refreshes, with try_lock, only
when the UI changed a pattern.

## Build and verify

MSYS2 mingw64 + Rack SDK at `C:/dev/Rack-SDK` (the Melody Monster rig). `make test` runs the core checks;
`make install` puts the plugin in Rack. Live smoke over the Rack MCP bridge if it is up.
