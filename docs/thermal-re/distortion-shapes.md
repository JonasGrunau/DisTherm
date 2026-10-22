# Thermal distortion shapes (MKUDustDistoNode): findings
(saved by the lead from the agent's final message; the agent could not write files of this kind)

Files: `Shapes.h` (`dt::shapeEval(type,x,c)`, `dt::shapePrepare`/`dt::shapeApply`, `dt::shapeTypeFromNormalised(u)`), `render.cpp` (`render_shapes in.f32 out.f32 sr type=N drive=dB out=dB c1= c2= c3= c4= [os=N]`), `verify.py` (output `work/verify_out.txt`), `model.py` (Python mirror, matches C++ to 3e-8), `sh.py`, `work/*.py`.

## 1. Summary
```
y = OutGain * shape_t(DriveGain * x_in; c),  DriveGain = 10^(DRIVE/20), OutGain = 10^(OUTPUT/20)
shape_t = raw_t(x; c) * 10^(-sum_k w_tk * c_k^(1/s_tk) / 20)
```
- All types memoryless, per channel (L and R independent, same curve).
- No DC blocker, dry/wet or post filter in the shaper (Asym Destroy keeps its DC).
- Shaper runs 2x oversampled with linear-phase filters (section 7).
- QUIRK: every control a type doesn't use lowers the output by 1 dB per unit.
- Type index = floor(33u). Indices 19..31 crash Thermal (segfault). 32 and 33 work (hidden shapes).

Notation: fold(u) = triangle folder (identity on [-1,1], reflects at ±1, period 4); tw(th) = triangle wave period 1, sine phase (tw(.25)=1); clip = clamp [-1,1]; p(c,s) = c^(1/s).

| # | name | raw_t | controls |
|---|---|---|---|
|0|Light Shaper|0.5 tanh(kx), k=1.5+48.5 p(c1,0.6)|c1|
|1|Heavy Shaper|tanh(kx), k=1.5+664.5 p(c1,0.3)|c1|
|2|AM Shaper|u=tanh(kx)+D sin(wx); y=\|u\|>T ? clip(u) : 0; k=2.5+22.5p(c1,.7), D=0.1+0.9p(c2,.7), w=π(0.9+29.1p(c3,.7)), T=0.9p(c4,.7)|all|
|3|Wave Fold|fold(sin(4x)/(1−0.88 p(c1,1.25)))|c1|
|4|Sine Drive|sin(wx), w=π(1.32+28.68 p(c1,0.4))|c1|
|5|Spring Drive|x'=x for \|x\|≤3 else sign(x)(1+fmod(\|x\|−1,2)); y=tw(x'\|x'\|/(0.64−0.6 p(c1,1.9)))|c1|
|6|Linear Sine|clip(x(1+c1 sin(b\|x\|))), b=2π(1+29 p(c2,0.4))|c1,c2|
|7|Asym Destroy|x<0: fold(sin(4x)/(1−0.96 p(c3,1.99))); x≥0: clip(16/15 sin(w1x) − 1/15 sin(w2x)), w1=2.65+47.35c1, w2=666 p(c2,1.4)|c1,c2,c3|
|8|Invert Fold|s=tanh(kx); y=s−tanh(ks), k=2+18c1²|c1|
|9|Exponential|sign(x)(1−e^−\|x\|)|–|
|10|Soft Clip|\|x\|<1 ? x−x³/3 : sign(x)·0.666666 (float 0.666665971)|–|
|11|Triangle Clip|a=\|x\|: sign(x)(a<.5 ? 2a : a<1.5 ? 2−2a : −1)|–|
|12|Triangle Fold|sign(x)(1−\|1−2frac(\|x\|)\|)|–|
|13|Single Fold|2x/(1+x²)|–|
|14|Multi Fold|fold(x)|–|
|15|Sine Fold|sign(x)\|sin 2x\||–|
|16|Sine Shred|1.2 sin(fmod(x,1)) (C fmod)|–|
|17|Binary Shred|\|x\|>0.2 ? 0.8 sign(x) : 0|–|
|18|S Curve|tanh(e·x)|–|
|32/33|hidden|0.5 tanh(c1 x) / tanh(c1 x), −1 dB per unit on all four controls; silent at c1=0|c1|

## 3. Control gain law: gain_dB = −Σ w·c^(1/s)   (entries: w, s)
| type | c1 | c2 | c3 | c4 |
|---|---|---|---|---|
|0|10, 1.5|1,1|1,1|1,1|
|1|12, 1.65|1,1|1,1|1,1|
|2|2, 1|3, 1.9|0.1, 1|1, 1|
|3|5, 1.9|1,1|1,1|1,1|
|4|6, 1.9|1,1|1,1|1,1|
|5|9, 1|1,1|1,1|1,1|
|6|1, 1|0.1, 1.9|1,1|1,1|
|7|3, 1.9|0.1, 1.9|3, 1.9|1,1|
|8|8, 1.4|1,1|1,1|1,1|
|9–18, 32, 33|1,1|1,1|1,1|1,1|

## Method notes
- Capture: slow triangle ±64 (96000 samples per half) and fine ±4 (192000); pre-roll 0.5 s (type-change fade also runs at state load).
- Drive/output placement verified −24..+36 dB / −12..+6 dB: `Out·shape(Drive·x)` to 1.2e-5. Stereo independent.
- Hysteresis: none beyond 2x-grid effects.

## 5. Type index
floor(33u) (3.0..3.999 → 3). 19–31 segfault the host. 32/33 as above. Hypothesis: shape chosen by type mod 32 with per-type tables read out of range.

## 6. Type change (node-level)
Output ramps linearly to 0 over 4800 samples (100 ms at 48 k), switches type, ramps back up over 4800 samples (slopes ∓2.083e-4/sample). Starts at the block containing the change. SR dependence not measured.

## 7. Oversampling evidence
- Jumps/corners ring symmetrically ±8 samples → linear phase, delay inside the 93-sample chain delay.
- Factor 2: 1001 Hz sine into Binary Shred: alias lines folding at multiples of 2·fs present (−38 dB), lines folding at 1·fs and 3·fs absent (< −73 dB). H25 → 22.975 kHz at −50 dB (transition band).
- Linear response (Linear Sine c=0, |x|<1): zero phase, ~0.005 dB ripple; −0.0016 dB @20k, −0.093 @21k, −0.88 @22k, −3.50 @23k, −5.22 @23.5k, −6.02 @24k (48 kHz).
- An ideal 2x render nulls 10–20 dB better than 1x or 8x.

## 8. Verification
Static curves x∈±4: max error outside jumps 1e-7..1e-4 for most; 1e-3 Linear Sine; 1e-2 AM Shaper; 2e-2 Spring Drive (wrap jumps).
10 Hz sine nulls: smooth types −100..−131 dB; Sine Drive/Wave Fold limited (−67..−80 dB) by Thermal's filter ripple; high-energy settings (AM, Spring, Linear Sine, Invert Fold, Shreds) limited by Thermal's own 2x aliasing.
Full per-type table: see agent transcript / work/verify_out.txt.

## 9. Open
float vs double inside Thermal indistinguishable (Shapes.h uses double); > vs >= at thresholds pinned to 1e-6; UI control visibility per type unknown (UI greys unused controls).
