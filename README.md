# My lil Acid Bass for VCV Rack

**by A_Development**

The 303-style acid voice and 32-step acid sequencer from [My lil acid bassline](https://adev-io.github.io/my-lil-acid-bassline/)
as one VCV Rack 2 module. Same voice and the same sequencing rules as the web app and the FL Studio VST3, so a pattern
sounds the same in all three. The FL Studio build lives in its own repo and is not touched by this one.

## The module (40 HP)

**Step grid.** 32 steps. Click a pitch cell to place a note (click it again to remove it). Lanes under the keys:
**TIE** holds the previous note, **OCT** cycles up / down / normal, **ACC** accent, **SLD** slide into the next note.
Click a step number in the top row to set the pattern length. The lit column is the step playing.

**Voice.** Tune, Cutoff, Res, Env Mod, Decay, Accent, Volume, Saw/Square. Second row: filter attack / sustain / release,
amp attack / decay / sustain / release, HP (input high-pass), Key follow, Min decay, Pulse width, LFO rate, LFO to PW.
Hover a knob for its value in Hz, seconds or percent.

**Sequencer.** Tempo (40-300 BPM, used when CLOCK is not patched), Swing, Gate length (55 % at noon, like the 303),
Pattern (32 slots, I-A1 to II-B8), Run, Reset.

**Inputs.**
- SEQ IN: CLOCK (one pulse per 16th; replaces the Tempo knob), RESET, PAT (0-10 V picks a slot, added to the knob).
- PLAY IN: V/OCT, GATE, ACC. Play the voice from another sequencer or a keyboard. A pitch change while the gate is high
  slides, as on the 303. ACC high at the start of a note accents it. The voice is mono and shared with the built-in
  sequencer: play one or the other (an empty pattern or Run off leaves the voice to the PLAY inputs).
- CV IN: CUT (1 V/oct), RES, ENV, DEC, ACC (10 V moves the knob through its whole range).

**Outputs.** SEQ OUT V/OCT, GATE, ACC (drive any other voice with the acid line; the gate stays high through slides and
ties), and AUDIO (±5 V).

**Right-click menu.** Copy the pattern as step notation or as JSON, paste either one back (also the web app's exported
`.txt` files), clear the pattern, and **Load demo set**: the web app's six starter lines in I-A1 to I-A6, with their sound.

Step notation, one token per step: `C`..`B` note, `C'` high C, `^` `_` octave up / down, `*` accent, `~` slide,
`-` rest, `=` tie. Example: `A* - A A^ - A_* A - G~ A - A* - C^ - E~`.

Patterns are saved with the patch.

## Build

Needs MSYS2 (mingw64) and the Rack 2 SDK.

```sh
export RACK_DIR=/c/dev/Rack-SDK
make test      # core checks: voice, sequencer, clock, notation (against the FL plugin's own reference numbers)
make install   # builds and copies the plugin into Rack's plugins folder
```

`core/` holds the DSP and sequencer with no Rack code in it; `src/` is the Rack module and panel.

## License

MIT. See [LICENSE](LICENSE).
