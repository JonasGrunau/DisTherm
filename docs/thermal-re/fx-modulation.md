# fx-modulation: Chorus, Flanger, Phaser (saved by lead from agent message, condensed)

Files: Chorus.h, Flanger.h, Phaser.h, FxModCommon.h, render.cpp (`render_fxmod in.f32 out.f32 sr fx=chorus|flanger|phaser key=value ... block= lag= thermalstart= evN=key,sample,value`), verify.py, fxm.py, fxtrack.py, work/.
Mix law (all): y = Out·(Wet·fx(In·x) + (1−Wet)·In·x); phaser fx already contains dry (MULTI fx = x + 0.5·v; BARBER fx = x + wet). Fresh instance: smoothers ramp from parameter defaults to loaded values at t=0 (startFrom(Params{})); DisTherm: first setParams snaps.

## Chorus (−73..−97 dB)
One voice, Catmull-Rom 4-point, write before read. s = x + Fb·y[n−1]; D = (3 ms + 2 ms·Depth·sin(2π(p + c·Width)))·sr/1000, c=0 L / 1 R. LFO float32 cycles accumulator p += float(rate/sr). Buffer exactly ceil(5 ms·sr) with no guard samples (wrap quirk at Depth≈1, copied). Feedback positive, no filter/saturation, 0..0.9. Smoothing: Rate, Fb, WetDry, gains 5 ms; Depth, Width 50 ms.

## Flanger (static −126..−140 dB; light mod −59..−74; deep mod −18..−36 dB from LFO start-phase residual)
Types: 0 VINTAGE (+fb), 1 MODERN (−fb), only exactly 1 is MODERN; type not smoothed. Teff = min(1, Time − ModRange·sin φ); D = (0.2 + 9.8(1−Teff)³) ms (Teff<0 allowed → up to 78.6 ms); Catmull-Rom, read before write; y = R(y[n−1] + d − d[n−1]) with R = 1/(1 + 2π·20/sr) (20 Hz HP in loop); buf = x + g·y, g = ±0.495(1+Fb). LFO rate = 0.1 + 4.9·ModFreq Hz, float32 radians accumulator; on rate change ph ← ph·f_prev/f_new (phase·rate preserved). Fresh instance: rate smoother starts at 5 Hz, ramp starts 2 samples late, LFO runs 4 samples ahead. Smoothing 5 ms (Time, ModRange, ModFreq, Fb, DryWet, gains).

## Phaser
Type: exactly 0 → MULTI, exactly 1 → BARBER, anything in between → gains only (dry). Each engine has its own LFO/state; inactive engine paused/frozen.
MULTI (−42..−102 dB): f0 = Centre/2 + sr/(8π)(1 + Range·sin φ) (linear Hz); s = x + 0.98·Fb·T(v[n−1]), T(u) = u(27+u²)/(27+9u²) with u clipped ±3; v = 2·floor(Stages) first-order bilinear allpasses (TPT) at f0; fx = x + 0.5v. LFO sine float32 radians, rate in Hz, rate change rescales phase, not smoothed. Centre, WetDry, gains 5 ms; Range, Fb, Rate none; Stages instant. Start-up LFO offset depends on host block size (modelled for 512).
BARBER (L −102..−391 dB; R −50..−61 dB at 48 k via empirical IIR fit, −16..−22 dB at other SRs): s = x + 0.65·Fb·o[n−1]; I = H1(s), Q = H2(s) (Hutchins/Csound hilbert pair, 6 first-order allpasses each, y = c(x − y[n−1]) + x[n−1], c = −(1−α)/(1+α), α = π·15·p/sr, p_I = {0.3609, 2.7412, 11.1573, 44.7581, 179.6242, 798.4578}, p_Q = {1.2524, 5.5671, 22.3423, 89.6271, 364.7914, 2770.1114}); o = x + I cos φ + Q sin φ, φ = 2π·Rate·t (double). Stages/Centre/Range ignored. Right channel in-phase path differs from H1 + left leaks into R at −32 dB (unexplained; empirical fit at 48 k only).

## Open
BARBER right-channel mechanism; flanger fresh-instance LFO start phase; MULTI start offset block-size dependence; load fade not modelled.
