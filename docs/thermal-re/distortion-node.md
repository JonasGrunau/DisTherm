# distortion-node (MKUDustDistoNode_Stage_N): findings (saved by lead from agent message, condensed)

Files: DistortionNode.h (dt::DistortionNode, DistortionNodeParams, DistortionNodeTuning; includes Shapes.h), render.cpp (`render_distnode in.f32 out.f32 sr type c1..c4 drive out auto fb time sync div bpm follow release bypass block pre evN=key,sample,value`), verify.py (38 cases, median −133.7 dB), cpp.py, common.py, jucehb.py (JUCE FIR design port), pymodel.py, work/.

## Signal flow
```
x ─► ×drive (20 ms linear ramp of linear gain) ─► 2x up (JUCE Oversampling FIR equiripple half-band, 75 taps) = u
  env  = peak detector on (|uL|+|uR|)/2 at 2x; attack 12.5 ms, release = RELEASE; tau = 1.82048·time (1/ln√3)
  gate = env > 1e-4
  w    = delay line (2x, float, linear interp), d = float(T·2fs), min 1 sample
  pre  = fb(10 ms ramp)·w
  lim  = peak detector on (|preL|+|preR|)/2, attack 0.1 s, release 1 s (BallisticsFilter style)
  gr   = min(1, 0.316228/lim)
  s    = u + gate·gr·B·pre;  B = FOLLOW ? min(1, 5·env) : 1
  v    = shape(type, s) (Shapes.h coefficients ramped linearly over 10 ms)
  delay ← A·v;  A = FOLLOW ? 1 : per-type gain (applied on WRITE)
─► v·typeFade ─► 2x down (51 taps) ─► ×out (20 ms ramp) ×autoComp (20 ms ramp) ─► y
```
- Oversampler = juce::dsp::Oversampling<float>(2, 1, filterHalfBandFIREquiripple, isMaxQuality=false): up designFIRLowpassHalfBandEquirippleMethod(0.06, −70 dB) 75 taps; down (0.075, −60 dB) 51 taps; float coeffs; polyphase. Latency 31 base samples at every SR; bypassed node = input delayed 31 bit exact. 93 = 3 × 31.
- Small-signal combined response: ±0.005 dB to 20 kHz, −0.093 @21k, −0.88 @22k, −3.5 @23k, −6.02 @24k (48 k).
- No DC blocker / emphasis.

## Gains
- DRIVE: linear pre-gain before upsampler, 20 ms linear ramp (first sample already one step).
- OUTPUT: after downsampler, 20 ms linear ramp.
- AUTO (InputGainComp): output × 10^(−0.3·driveDb/20); ON only when normalised ≥ 1.0. Own 20 ms smoother driven by TARGET drive dB, starting at 0 → first switch-on fades in from silence; frozen while off; switching off jumps (no ramp). Copied.

## Feedback
- Loop at 2x. Taps shaper output, returns before shaper; drive/out/auto outside loop; shape control gains inside.
- Delay: float(T·2fs) with linear interpolation; Time mapped with JUCE NormalisableRange<float>(0.0002, 0.3, skew 0.3) in float. Min 1 sample. Buffer 10 s; synced delay ≥ 10 s collapses to 1 sample.
- Time change: linear 30 ms glide in seconds (float SmoothedValue at 2x) → pitch glide.
- Sync (TimeCurrentMapping=1): division index = round(u·18); delay = floor(beats·60/bpm·sr) base samples; no 300 ms clamp.
- Loop gain = fb × A. A per type (dB), FOLLOW off: 0 −5, 1 −10, 2 −10, 3 −10, 4 −10, 5 −5, 6 −5, 7 −10, 8 −10, 9 +0.5, 10 −0.25, 11 −5, 12 −5, 13 −5, 14 −0.25, 15 −5, 16 −1, 17 −5, 18 −5 (32/33 assumed −5/−10).
- FOLLOW (on for any normalised > 0): B = min(1, 5·env), A = 1. Gate applies in both modes. Toggle instant; old A stays for one delay period (A on write).
- Limiter to 0.31623 (−10 dB) on fb·A·w (FOLLOW off) / before ×B (FOLLOW on); stereo linked.
- Smoothing at 2x: feedback 10 ms, time 30 ms, shape coefficients 10 ms; RELEASE coefficient immediate.
- Quirks: DC lock-up when slope·A·fb > 1; Sine Drive self-oscillates while gate open (chaotic).

## Type change
floor(33u). Fade out 100 ms (9600 steps at 2x), switch type + coefficients, clear delay line one 2x sample after switch, fade in 100 ms. Fade outside the loop. Second change during fade-out switches to newest at the bottom. Same fade after state load if stored type ≠ 0.

## Bypass
Node property; crossfade not measurable → model uses an unmeasured 20 ms linear crossfade (click-free).

## Verification (38 cases)
Static −132.9..−145.2 dB; bypass exact; automation −109..−143.5; feedback cases −94.9..−146.9; type change with feedback −71.2; AM Shaper ramp in loop −55; Sine Drive self-osc (chaotic) −43.3; block 37/offline −119.7; 44.1k −120.6/−97.4; 96k −104.2/−93.0.

## Integration notes (lead)
- The delay-line clear on type change is a 4–16 MB memset on the audio thread: replace with an equivalent "valid length" counter (reads beyond the samples written since the clear return 0).
- Brief's claim "0.01 impulse comes out as a unit impulse" was wrong (my test was with disto bypassed); small signal = 0.75 × 63-tap kernel.
