# fx-dynamics-filter: Compressor, Filter, BitReducer (+ MasterCompressor)
(saved by the lead from the agent's final message, condensed; the headers are the exact spec)

Files: BitReducer.h, Compressor.h, Filter.h, FxdfCommon.h (namespace dt / dt::fxdf), render.cpp (`render_fxdf in.f32 out.f32 sr fx=compressor|filter|bitreducer [block=N] key=value ... [ev=sample:key=value]`), verify.py (63 cases), fxd.py, work/*.py.
All three null at −94..−150 dB or bit exact. Measured only.

## 0. Common
- `x_in = InputGain·x; y = OutputGain·(WetDry·fx(x_in) + (1−WetDry)·x_in)` (linear crossfade; dry includes input gain). No clipping anywhere.
- In/Out gain (linear amplitude) and WetDry: juce::LinearSmoothedValue ramp of floor(0.005·fs) samples (240 @48k); first sample after change already moved one step. Host automation applies at block start.
- GRAPH: an effect in Stage i sees the input 31·i samples late; Slot1/Slot2/master comp see it 93 = 3×31 late. Each stage carries 31 samples of the constant 93-sample latency, even when skipped.

## 1. BitReducer (bit exact)
- `N = 505 − Bits` (float32, non-integer ok). Quantize `q = trunc(held·N)/N` in float32 (division, not multiply by 1/N). Truncation toward zero → |x|<1/N gives 0. No dither/clip/filter. Even 0 % quantizes to 1/500.
- Hold `H = ceil(SampleReduction)` using the float32 mapped value (JUCE convertFrom0to1 = start + range·exp(log(u)/skew)); e.g. "33" → 33.000004 → holds 34.
- One counter shared L/R, free-running from prepare: `take = (counter==0); if (++counter >= H) counter = 0`. H change keeps the counter.
- Bits/SampleReduction not smoothed.

## 2. Compressor (= MasterCompressor, identical; no latency; master default on: 0 dB, 4:1, 12 ms, 50 ms)
1. x_in = InputGain·x; d = (|L|+|R|)/2
2. c = d > env ? ca : (slowFlag ? crSlow : cr); env = d + c·(env − d)
3. slowFlag = env < 10^(−30/20) (evaluated after update, used next sample, RESET TO FALSE AT START OF EVERY BLOCK)
4. ca = exp(−ln9/(max(Attack, 0.1 ms)·fs)), cr = exp(−ln9/(Release·fs)), crSlow = exp(−ln9/(5·Release·fs))  (10–90 % times)
5. L = 20log10(env); GR = max(min(0, (Thr − L)(1 − 1/Ratio)), −30)  (hard knee, GR capped at 30 dB before makeup)
6. y = Out·(Wet·x_in·10^(GR/20)·Makeup + (1−Wet)·x_in)
- Peak, linear-domain detector, stereo-linked mean of abs, no sidechain filter, no lookahead, no auto makeup.
- Smoothing: threshold (dB), ratio (ratio units), makeup (linear), gains, wet: per-sample 5 ms linear ramps. Attack/Release times smoothed by 5 ms ramp in seconds that advances ONCE PER BLOCK (skip(numSamples)).
- Attack floor 0.1 ms (bottom 24 % of the knob does nothing).

## 3. Filter = JUCE dsp::LadderFilter<float> algorithm + Thermal's envelope follower
Per sample per channel:
- gain = drive^−2.642·0.6103 + 0.3903; drive2 = 0.04·drive + 0.96; gain2 = drive2^−2.642·0.6103 + 0.3903; r = 0.1 + 0.9·Res (k = 4r, 0.4..3.64, never self-oscillates)
- a1 = exp(−2π·fc/fs), g = 1 − a1, b0 = g·0.76923076923, b1 = g·0.23076923076
- dx = gain·sat(drive·x_in); a = dx − 4r·(gain2·sat(drive2·s4) − comp·dx), comp = 0.5 (LP) / 0 (HP)
- b = b1·s0 + a1·s1 + b0·a; c = b1·s1 + a1·s2 + b0·b; d = b1·s2 + a1·s3 + b0·c; e = b1·s3 + a1·s4 + b0·d; (s0..s4) = (a,b,c,d,e)
- out = 1.2 × LP12 c | LP24 e | HP12 a−2b+c | HP24 a−4b+6c−4d+e
- sat = linear interpolation in a 128-point tanh table on [−5,5] (clamped), points tanh(−5+10i/127)
- Mode = floor(mapped 0..3); mode change resets the filter state (click, copied).
- Drive not smoothed. Cutoff: 50 ms linear ramp (floor(0.05·fs) samples) in the a1 domain; Resonance: 50 ms linear ramp of r. Both smoothers in float32 (matters for nulls).
- Envelope follower (FollowAmount −3..3): per block: for each sample of the RAW effect input (before input gain) d = |L+R|/2, env = d + c(env − d), c = 0.01^(1/(T·fs)) with T = 4 ms attack / 100 ms release (float32); then u = clamp(u_cutoff + 2·Amount·env, 0, 1) with u_cutoff = ((Cutoff−30)/21970)^(1/4); fc = 30 + 21970·u⁴; set cutoff target ONCE PER BLOCK from the envelope at the block's END, then filter the block.
- Small-signal: LP DC gain = 1.2(1+2r)/(1+4r); HP no compensation.

## 6. Implementation notes
- Call setParams then process ONCE PER HOST BLOCK (three per-block behaviours: filter follower target, compressor slow-release flag reset, compressor attack/release smoothing).
- Pass physical values computed in float32 like JUCE's NormalisableRange (BitReducer ceil depends on it).
- setParams before prepare: prepare takes those as unsmoothed start state.

## Verification summary
BitReducer 12 cases: bit exact or −128..−148 dB. Compressor 12 + master 3: −93.5..−132 dB. Filter 36: −99..−139 dB, except HP12 1.2 kHz res 0.9 drive 2.5 at −73.5 dB (Thermal's float32 table-index noise on a −38 dBFS output).

## Open
Slow-release rule's internal reason unknown (fits to 1e-5, nulls −111 dB). Filter −130 dBFS limit cycle at high res not reproduced (inaudible). Effect switching mid-stream not measured.
