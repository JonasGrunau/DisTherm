# Thermal modulation system: findings (saved by lead from the agent's final message, condensed)

Files: Modulation.h (dt::ModEnvelope, dt::Macro, dt::ModTarget/ModSlotParams, dt::ModBus), render.cpp → render_modulation, verify.py [--subpresets], model.py, m.py, host2/ (private host copy with transport timeline: seg<k>=<hostSample>,<ppq>,<playing>[,<bpm>], tisoff=, noppq=1, notis=1, noph=1), work/e*.py.

## Envelope shape
- Stored y is screen coordinate: output = 1 − y. Control point i shapes segment (i−1 → i); ControlPoint0 unused.
- On load point x/y clamped to [0,1]; control points NOT clamped.
- Segment = quadratic Bézier flattened by juce::PathFlatteningIterator (float) in a 1200×400 space with tolerance 0.6: subdivide at midpoint while |m3 − C|² > tol², each leaf emits 2 lines (2^(d+1) lines uniform in t).
- Sampled into a 2048-entry LUT at x = j/2048: first polyline segment in order with xa ≤ x ≤ xb (backward segments never match), linear interpolation; no match → 0; entry = clamp(1 − y, 0, 1).
- Read: float32 phase: idx = φ·2048; i0 = floor; v = lut[i0&2047]·(1−f) + lut[(i0+1)&2047]·f (wraps in the last cell).
- All 92 factory sub-presets: max error 1.9e-5.

## Envelope timing
- Synced (RateCurrentMapping=1): DSP index = round(u·25) (display floors: copy quirk). Beats: 32,28,24,20,16,12,8,4, 3,2,4/3, 1.5,1,2/3, .75,.5,1/3, .375,.25,1/6, .1875,.125,1/12, .09375,.0625,1/24.
  Period P = floor(60·sr·beats/bpm) samples (exact rational). phase = float(pos mod P)/float(P), pos = timeInSamples + i (NOT ppq). Stopped: internal counter keeps running from last pos; on play pos jumps to host. No playhead: counter from 0. Negative timeline → phase 0.
- Free (=0): f = 0.005 + 29.995·u^(10/3) Hz; P = floor(sr/f); float32 accumulator inc = 1/P, out = LUT(phase); phase += inc; wrap −1. Reset to 0 at first sample of the block where transport goes stopped→playing.
- Humanize h: at every cycle wrap (and at start) each point y_i += h·U(−0.5,0.5), clamped [0,1]; x and controls unchanged; changing h rescales current draw immediately; random per instance.
- Bypassed envelope outputs 0.

## Macros / XY
Macro output = param value unsmoothed; bypassed → 0. XY pad is UI-only (not in state); inferred X→Macro1?? (agent inferred X→MACRO1, Y→MACRO2; UI capture shows Y axis labelled Macro 1, X axis Macro 2).

## Routing (exact)
Connection <src>:EnvelopeOut|MacroOut → <node>:<Param>ModIn<i>, i = source index (0 MOD1, 1 MOD2, 2 MACRO1, 3 MACRO2). Props ModAmount<i>, ModBiPolar<i>, DepthModRange<i>. Depth: Connection <anySource> → <node>:<Param>ModDepthIn<i>.
```
s_i = src_i  |  2·src_i − 1 (bipolar)
d_i = ModAmount_i + DepthModRange_i · Σ(sources on ModDepthIn<i>, each 0..1)
u   = clamp(u_base + Σ_connected d_i·s_i, 0, 1)        (normalised domain, before taper; one clamp)
```
Only connected slots count; depth without ModIn does nothing; effective depth not clamped; bypassed depth source = 0; envelope Rate/Humanize are modulatable (rate rounded after modulation).

## Delivery timing
- Sources per-sample; each destination node sees them delayed by the audio latency in front of it: D = 0 (InputBus, Stage1 splitter, Stage1 DustDisto), 31 (Stage1 effect/MidSide/GainBus/Tone, Stage2 splitter/DustDisto), 62 (Stage2 effect..tone, Stage3 splitter/DustDisto), 93 (Stage3 effect..tone, Slot1, Slot2, Master comp, OutputBus).
- Most params read once per host block at the block's first sample: u = f(src(T − D)).
- InputBus.InputGain, OutputBus.OutputGain, MasterCompressor.InputGain/OutputGain read every 32 samples (chunks restart each host block).
- OutputBus.DryWet: unclear pattern.
- No smoothing added by the mod system.
- Source→source modulation (macro → env rate/humanize, env → other env rate): no delay, dependency order, read once per block at sample 0. Mutual env↔env loop order unmeasured.

## Verification
32 cases vs Thermal: residual −115..−140 dB, worst peak 0.0015 dB. Humanize stats match.
