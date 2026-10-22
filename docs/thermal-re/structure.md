# structure (routing / utility nodes): findings (saved by lead from agent message, condensed)

Files: Structure.h (dt::InputBus, GainStage, MidSideWidth, ToneControl, Crossover3, BandSplitCoupler + bandaxis::pos/freq + DEV table, DryCompensation, OutputBus, StructureChain), render.cpp (`render_structure`), verify.py (56 cases, −88..−388 dB; ~−70 dB floor at low cutoffs from Thermal's float32), axis.py, probe.py, probehost/ (private host with `probe` dumping plugin state XML), scripts/sNN_*.py.

## Graph & latency
```
in → InputBus(gain) ─┬→ Stage1 → Stage2 → Stage3 → [Slot1, Slot2, master comp] → OutputBus wet
                     └→ dry: delay 93 → per-stage allpass compensation ───────────→ OutputBus dry
Stage (splitter on): splitter → mids: [31-sample latency = DustDisto node] → effect → MidSide → GainBus → summer
                               lows/highs delayed 31 ──────────────────────────────────────────→ summer → ToneControl
Stage (splitter Bypassed): pure 31-sample delay → ToneControl (tone NOT skipped)
```
Dry = InputBus output (post InputGain), delayed 93, not filtered by output LP/HP; OutputGain applies to both.

## Gains
dB = −60 + 66u (−60 dB = 0.001). Linear SmoothedValue on linear gain, floor(0.005·sr) steps, per sample. InputGain and OutputGain start at 0 after load (= the start-up fade-in); GainBus starts at unity. GainBus Bypassed → unity.

## Crossover
RBJ biquads Q 1/√2, TDF2. lows = LP4(f1)·AP(f2); mids = HP4(f1)·LP4(f2); highs = HP4(f1)·HP4(f2); sum = AP(f1)·AP(f2). lows/highs delayed 31. MultiBand=0: mids = full band bit exact, lows = highs = 0 (Refilter/Solo no effect). MultiBand fractional → Thermal outputs NaN (treat as bool).
Solo: output = processed mids only (hard switch at summer). Refilter: mids' = HP4(f1)·LP4(f2)·mids_proc; lows' = AP(f1)·lows; highs' = AP(f2)·highs. Solo/Refilter on for any u > 0. Summer node props are mirrors.
Smoothing: linear in Hz over floor(0.05·sr) steps, advanced 5 steps per 5-sample tick, ceil(B/5) ticks per host block (coefficients updated per 5-sample chunk) → glide length block-size dependent. At load smoothers start at 30/22000 and glide in; MultiBand 0 → glide back to 30/22000; enabling glides in again from 30 with zero filter state; Refilter has its own pair starting at 30/22000 on enable.

## LOW/HIGH coupling (Thermal writes the result back into its parameters, once per block, only while splitter on and MultiBand=1)
Axis: 855 px, 4 segments of 213.75 px: 30–100, 100–500, 500–5000, 5000–22000 Hz; inside [fa,fb]: f = A + (fa − A)(1 + (√10−1)t)², A = (10fa − fb)/9. Min gap 30 px.
If LOW changed (LOW wins): x1 = floor(pos(f1)); x1 > 825 → 825, f1 = freq(825); if pos(f2) < x1+30 → f2 = freq(x1+30) (if different); re-read x2 = floor(pos(f2 stored)); if pos(f1) > x2−30 → f1 = freq(max(x2−30, 0)); 134 pixels re-read as x2−1 (DEV table).
Else if HIGH changed: x2 = floor(pos(f2)); x2 < 30 → 30, f2 = 35.437 Hz; if pos(f1) > x2−30 → f1 = freq(x2−30).
Coupled values glide together (50 ms). Factory presets are affected (e.g. (824.25, 1119) → (816.88, 1250.83)). 0 mismatches over ~13,100 probes.

## MidSide (Bypassed default 1)
a = cos(π/4 − w/2), d = sin(π/4 − w/2), k = min(2w, 1); D = floor(0.015·sr)
L' = a·L + k·d·L[n−D]; R' = d·R + k·a·L[n−D]; M = (L'+R')/2; S = g(L'−R')/2; out = M±S. Constant −3 dB when enabled at w=0. WIDTH puts only the delayed LEFT input into both outputs. g, w linear 10 ms ramps per sample; start g=1, w=1.0 after load.

## Tone (Bypassed default 1)
RBJ low shelf 500 Hz, high shelf 5000 Hz, Q 1/√2, gain dB −18..+18; TDF2; gains smoothed as linear amplitude with the 50 ms 5-sample-tick smoother, starting at 0 dB.

## OutputBus
out = OutGain·((1−v)^(5/7)·dry + v^(5/7)·HP(LP(wet))). v linear 5 ms ramp per sample, starts at 1. LP/HP: RBJ Butterworth 12 dB/oct, DF1, cutoff linear in Hz over 5 ms per sample, coefficients per sample, start 22000/30. FiltersBypassed removes both.
Dry compensation: per stage with splitter on and MultiBand=1: dry passes AP(f1) → [AP(f1) if Refilter] → AP(f2) → [AP(f2) if Refilter] with coupled freqs and the same tick glide; pair starts zero-state from 30/22000 when stage becomes active; refilter pair switches in hard.
OutputBus Stage* props: UI mirrors only.

## Open
Thermal float32 floor at low cutoffs not copied (−70 dB at HP 30). DEV table measured (134 px). Processor must write coupled values back to parameters (coupledBand1/2()). Splitter bypass toggling mid-stream not measured (model freezes the stage's state while skipped). Summer-bypass-with-splitter-on (lows only) not implemented (never in presets).
