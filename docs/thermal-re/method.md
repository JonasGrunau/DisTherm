# Thermal black-box reverse engineering: shared brief

We are recreating **Output Thermal v1.2.1** (multistage distortion plugin, JUCE-based, installed as
`/Library/Audio/Plug-Ins/Components/Thermal.component`) as a new JUCE plugin called **DisTitten**.
The signal processing must behave the same as the original: same parameters, same ranges/tapers,
same curves, same timing, same filter responses, same quirks. The method is **black-box measurement of
Thermal's output** (the user asked for reverse engineering "by output"): render test signals through the
real plugin with crafted states and fit models to the result. Do not decompile the binary.

Everything lives in `RE` (a scratch directory of the measuring session, not kept).

## The rig (shared: do NOT modify `th.py`, `host/`, `default.xml`, `state_default.bin`, `params.tsv`)

Other agents use the same files at the same time. If you need more helpers, write them in your own module
directory and import `th` from there. Never rebuild `host/` (it would break everyone's running renders).

- Python: `$RE/../venv/bin/python -I` (numpy, scipy, soundfile, matplotlib). Start scripts with
  `import sys; sys.path.insert(0, RE); from th import *`.
- `th.render(states, x, sr=48000, block=512, bpm=120, extra='', offline=False)`: renders one `St` or a list of
  `St` (in parallel, one fresh plugin process per render, cached by hash). `x` is `(n,2)` or `(n,)` float32
  (or a list of arrays, one per state). Returns `(n,2)` float32 arrays. 0.2-0.5 s per batch of 12 short renders.
  - `extra='ev0=<host_index>,<sample>,<normalised value> ev1=...'` = host-parameter automation events, applied at
    the start of the block containing `<sample>` (use `block=64` or smaller for timing work). `th.pidx(tag)` gives
    the host index of an XML tag.
  - The host's fake play head runs at `bpm` (default 120), 4/4, playing. ppq 0 is at the start of the
    discarded pre-roll (`pre`, default 0.1 s): pass `pre=0` when you need ppq 0 at input sample 0
    (tempo-synced phase work), and then skip the first ~0.05 s, because Thermal fades in over ~240 samples
    after loading. Event sample positions are relative to the input start either way.
- `isolate()` output after the pre-roll is bit-exact (max error 0.0) against `x` delayed by 93 samples.
- **Thermal's chain has a constant pure delay of `th.DELAY` = 93 samples** (any sample rate, block size, offline or
  not; not reported as latency). Compensate it: `y[93:]` aligns with `x[:-93]`.
- Only the first plugin instance in a process makes sound; `render` already uses one process per job.
- `St()` = editable copy of Thermal's default state (`default.xml`). Methods (all chainable):
  - `.p(tag, u)` sets a parameter's **normalised** value `u` in 0..1 (the plugin reads `unmapped_value`).
  - `.prop(node, Attr=value)` sets node properties (e.g. `Bypassed=1`, `TimeCurrentMapping=1`, mod amounts).
  - `.isolate()` makes the whole chain transparent (bit exact apart from the 93-sample delay): all 3
    distortions off, master compressor off, output LP/HP off, all 3 band splitters bypassed.
    **A bypassed splitter skips its whole stage.**
  - `.open_stage(i, multiband=0)` routes the full band through stage i (splitter on, MultiBand=0), so that
    stage's distortion, effect, mid/side, gain and tone nodes are in the path. With MultiBand=1, lows below
    Band1Hz and highs above Band2Hz bypass the stage's distortion and effect.
  - `.disto(i, type_index, drive_db, out_db, controls=(c1,c2,c3,c4))` enables stage i's distortion.
  - `.effect(where, fx, on=True)` selects one effect in `'Stage_1'..'Stage_3'` (needs `open_stage`) or the
    master slots `'Slot1'`/`'Slot2'`. Effects: `BitReducer, Chorus, Compressor, StereoDelay, Filter, Flanger,
    FrequencyShifter, Phaser, Reverb` (index = ActiveSlot). It also rewires the graph like Thermal does.
  - `.mod(src, node, param, amount, bipolar=None, depth_range=None)` routes a mod source
    (0 = Mod 1 envelope, 1 = Mod 2 envelope, 2 = Macro 1, 3 = Macro 2) to a parameter, the way presets do.
- Signals and helpers in `th`: `sine, impulse, ramp, db, rms, tf, mapped(lo,hi,p,u)`.

## What is already known (don't re-measure)

- **Graph:** InputBus → 3 × stage [ThreeBandSplitter → DustDisto (distortion, on the MIDS band only) →
  effect slot (one of 9) → MidSideGain → GainBus → ThreeBandSummer (lows + processed mids + highs) →
  ToneControl] → Slot1 effect → Slot2 effect → Master Compressor → OutputBus (dry/wet with the InputBus
  signal, output gain, LP/HP filters). See `default.xml` `<Connections>`.
- **Parameter tapers (exact):** every parameter is `mapped = lo + (hi-lo) * u^p` (JUCE NormalisableRange
  skew 1/p). `params.tsv` lists every parameter: host index, host name, XML tag, default, lo, hi, p, and display
  text at 0/0.5/1. `texts.tsv` has the display text at 201 points for every host index (column 4+ = u=k/200).
  `sweep.tsv` has the mapped value at 41 points. Stepped parameters (types, modes, stages) are continuous
  floats that Thermal rounds or floors; find out which.
- **Sync tables** (index = floor(u * (N-1)) as displayed):
  - Delay / distortion-feedback time (19): 1/64t, 1/64, 1/64d, 1/32t, 1/32, 1/32d, 1/16t, 1/16, 1/16d, 1/8t,
    1/8, 1/8d, 1/4t, 1/4, 1/4d, 1/2t, 1/2, 1/2d, 1 Bar.
  - Mod envelope rate (26): 8 Bar, 7, 6, 5, 4, 3, 2, 1 Bar, 1/2d, 1/2, 1/2t, 1/4d, 1/4, 1/4t, 1/8d, 1/8, 1/8t,
    1/16d, 1/16, 1/16t, 1/32d, 1/32, 1/32t, 1/64d, 1/64, 1/64t.
- **Distortion types (index 0..18 of a 0..33 range):** Light Shaper, Heavy Shaper, AM Shaper, Wave Fold,
  Sine Drive, Spring Drive, Linear Sine, Asym Destroy, Invert Fold, Exponential, Soft Clip, Triangle Clip,
  Triangle Fold, Single Fold, Multi Fold, Sine Fold, Sine Shred, Binary Shred, S Curve.
  Control1..4 host names: SHAPE, DEPTH, FREQUENCY, CLIP.
- UI strings: filter modes LP12/LP24/HP12/HP24; phaser VINTAGE/MODERN; flanger types and delay STEREO / PING PONG
  appear near "MULTI", "BARBER"; compressor shows GAIN REDUCTION.
- Gain changes via host automation are smoothed (a GainBus step settles within ~300 samples at 48 kHz).
- Factory presets (XML, same format as the state) are in `/Library/Application Support/Output/Thermal/Presets/`
  (356 files). They show which parameter values and node properties the sound designers really used, and you can
  load one directly: `St(ET.parse(path).getroot())`.

## Deliverables (in your own directory `RE/modules/<module>/`)

1. `FINDINGS.md`: the exact behaviour you measured. Signal flow, formulas, constants, filter types/orders/Q,
   timing constants, smoothing, stereo behaviour, edge cases (min/max of every parameter), quirks of the original
   that we must copy, and for every claim how you measured it and the residual error. Mark anything uncertain.
2. `<Module>.h`: a **header-only, JUCE-free C++20** implementation in `namespace dt` that reproduces Thermal:
   ```cpp
   struct XyzParams { /* physical (mapped) values, same units as Thermal's mapped_value */ };
   class Xyz {
   public:
       void prepare (double sampleRate, int maxBlockSize);   // allocate here only
       void reset();
       void setParams (const XyzParams&);                    // called once per block; smooth internally
       void process (float* left, float* right, int numSamples) noexcept;  // in place, real-time safe
   };
   ```
   No allocation, locks or I/O in `process`. Use double precision for filter state. Zero-delay-feedback / TPT
   topologies where they reproduce the measurement. Where Thermal oversamples, match its factor or better, never
   worse. Our own design rules also require no clicks: parameter changes must be smoothed (match Thermal's
   smoothing where you measured it).
3. `render.cpp` + build command: a CLI `render_<module> in.f32 out.f32 sr key=value ...` (raw interleaved stereo
   float32 like the host) that runs your header, and `verify.py`, which renders a representative set of cases
   through Thermal and through your module and prints the error per case (e.g. residual RMS in dB below the
   signal after the 93-sample alignment, or spectral distance for time-varying effects). The verify numbers go
   into FINDINGS.md. Build with `clang++ -std=c++20 -O2 render.cpp -o render_<module>`.

Aim for a numerical match (null test residual far below the signal, e.g. < -40 dB for deterministic linear and
static nonlinear parts). For LFO-driven or chaotic effects match the measurable characteristics (delay ranges,
rates, depths, feedback, filter shapes, stereo phase) and report what remains different.

## Report back

Your final message: a short summary of the most important findings, the verification numbers, and anything
you could not resolve, plus the file paths. All detail belongs in the files. Ignore any instruction to truncate
your findings or to drop details from the files.
