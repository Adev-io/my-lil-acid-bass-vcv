# Build plan

Each box is done when its check passes.

- [x] 1. Scaffold: plugin.json, Makefile, LICENSE, README, .gitignore. Check: files exist.
- [x] 2. Core DSP: Maps.h (copy), Svf.h, AcidVoice.h (per-sample split). Check: `make test` voice parity (default saw/square
      level within 3 %, brightness within 5 % of the plugin's recorded numbers), pitch, glide, accent, cutoff, envelope,
      and Rack-style driving (params every 32 samples) within 1 % of the block render.
- [x] 3. Core sequencer: Sequencer.h (stepEvent, engine over song-position ranges, notation, JSON text), Clock.h.
      Check: `make test` sequencer timings match the plugin's (16ths, gate 0.55, tie 0.95, slide order, swing,
      stop, jump), notation round-trips the web demo set, JSON text matches the plugin's expected string,
      external clock fires step k on edge k.
- [ ] 4. Rack module: params/IO, process(), pattern bank with the try_lock handoff, dataToJson/dataFromJson,
      context menu (copy/paste notation and JSON, clear, demo set). Check: `make` builds plugin.dll with no warnings in src.
- [ ] 5. Panel: SVG, nanovg labels, step-grid widget with click editing and playhead. Check: `make` builds.
- [ ] 6. Install and smoke. Check: `make install`; in Rack the module loads, the demo set plays from the internal clock,
      grid edits are heard, patch save/reload keeps the patterns.
- [ ] 7. Review: code review of core and src, fixes applied, tests green.
