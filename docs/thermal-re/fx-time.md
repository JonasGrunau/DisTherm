# fx-time: Stereo Delay, Reverb, Frequency Shifter (saved by lead from agent message, condensed)

Files: StereoDelay.h, Reverb.h, FrequencyShifter.h, render.cpp (`render_fxtime`), verify.py, common.py (f32map = juce NormalisableRange<float> in float32), work/.
Wrapper (all three): x_in = In·x; y = (x_in·(1−mix) + fx(x_in)·mix)·Out; In/Out/mix = SmoothedValue<float, Linear> 5 ms (floor(0.005·sr)); dry not delayed. dB→gain float pow(10, dB·0.05); −60 dB = 0.001.

## Reverb: bit exact
Two independent juce::Reverb (processMono), BOTH with LEFT tunings (no stereo spread, no cross-feed). JUCE params wetLevel=1, dryLevel=0 → wet1 = 0.5·3·(1+width) (Width = wet gain ×1.5..×3 only). feedback = size·0.28+0.7; damp = damping·0.4; Freeze on when ≥ 0.5 (input gain 0, damp 0, feedback 1). JUCE internal 10 ms smoothers. Float, NO FMA contraction (#pragma clang fp contract(off)). Bit exact at all SRs/blocks/automation.

## Stereo Delay: −66..−106 dB
- s = timeSmoother.next(); d = clamp(ceil(double(s)·sr), 1, 48·sr) integer read, no interpolation; yL = HP(LP(lineL[w−d])) (read before write).
- STEREO: lineL[w] = xL + sat(fb·yL); lineR[w] = xR + sat(fb·yR). PING PONG ((int)Type == 1 only): lineL[w] = xL + sat(fb·yR[n−1]); lineR[w] = sat(yL); xR ignored.
- sat(x) = x(27+x²)/(27+9x²). LP/HP = 2nd-order Butterworth (bilinear, JUCE IIRCoefficients formula in double), DF1, double accumulation, float state, coefficients recomputed per sample while ramping; taper 20 + 19980·u⁴; LP/HP ramps linear in Hz over 10 ms; feedback 5 ms ramp.
- Synced time: index = floor(time + 0.5); beats table (19); target T = floor(float(beats·60/bpm)·sr)/sr. Smoother SmoothedValue<float, Linear> floor(0.15·sr) steps; synced mode retargets every block (first to raw index value, then T) → per-block restarted ramp ≈ exponential τ 0.15 s; float stall (stops a few samples short, block-size dependent, reproduced exactly). Start value after prepare in synced mode = 10.0 s (default index read as seconds) → wet silent ~0.5 s then glides in.
- Free (TimeCurrentMapping=0): T = 0.001 + 2.499·u^(10/3) s, plain linear 0.15 s glide, starts on target.
- Max 48 s. Type switch keeps buffers.
- Must call process once per host block.

## Frequency Shifter: −62..−100 dB
- vi = A_I(x_in), vq = A_Q(x_in): 6 first-order allpasses each (−c + z⁻¹)/(1 − c z⁻¹), c_k = (2sr − a_k)/(2sr + a_k), analog poles (rad/s): I 261088.7, 34382.05, 8447.308, 2105.787, 524.694, 118.042; Q 75257.75, 16929.07, 4218.596, 1051.546, 258.366, 34.015. Float coeffs/state, double mixing.
- wet = vi·sin φ + vq·cos φ; φ += 2π f/sr (double, φ=0 at prepare, used before increment).
- f_L = float(10000·u³); f_R = float(10000·clamp(u + offset, 0, 1)³). Up-shift only.
- Frequency: 10 ms linear ramp in u domain (starts at default 0.5 after prepare → glide, phase permanent). Offset not smoothed.
- No oversampling (folds past Nyquist). Image rejection ≈ −56..−62 dB 20 Hz–10 kHz.

## Chain-level note
After loading, the whole chain fades in over ~240 samples (input-to-slot gain 0.0016 @0, 0.13 @50, 0.34 @100, 0.84 @200, 1.0 from 240) and ~2 % dry leaks during the fade.

## Open
Delay filter float arithmetic not pinned (−66 dB at HP 20 Hz). Shifter 1e-8 relative frequency drift limits null at big shifts. Sync↔free switching untested (model snaps). Delay glide block-size dependent.
