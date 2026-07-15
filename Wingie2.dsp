declare name        "Wingie";
declare version     "3.1";
declare author      "Meng Qi";
declare author      "Dave Seidel";
declare license     "BSD";
declare copyright   "(c)Meng Qi 2020";
declare date        "2020-09-30";
declare editDate    "2026-07-14";

//-----------------------------------------------
// Wingie
//-----------------------------------------------

import("stdfaust.lib"); 

nHarmonics = 9;
modalBlockSize = 32;
modalBlockPulse = ba.time % modalBlockSize == 0;
modalBlockHold(x) = x : control(modalBlockPulse) : ba.sAndH(modalBlockPulse);
anti_feedback_enabled = hslider("../../anti_feedback_enabled", 1, 0, 1, 1);
anti_feedback_energy_limit = hslider("../../anti_feedback_energy_limit", 1, 0.000001, 65536, 0.000001);
anti_feedback_rho_guard = hslider("../../anti_feedback_rho_guard", 0.998435, 0.001, 0.999999, 0.000001);
decay = hslider("decay", 5, 0.1, 10, 0.01) : si.smoo;
output_gain = 1 : ba.lin2LogGain;
left_thresh = hslider("left_thresh", 0.1, 0, 1, 0.01);
right_thresh = hslider("right_thresh", 0.1, 0, 1, 0.01);
amp_follower_decay = 0.025;
resonator_input_gain = hslider("resonator_input_gain", 0.5, 0, 1, 0.01) : ba.lin2LogGain;
pre_clip_gain = hslider("pre_clip_gain", 0.5, 0, 1, 0.01) : ba.lin2LogGain;
post_clip_gain = hslider("post_clip_gain", 0.5, 0, 1, 0.01) : ba.lin2LogGain;
env_mode_change_decay = hslider("env_mode_change_decay", 0.05, 0, 1, 0.01);
//hp_cutoff = hslider("hp_cutoff", 85, 35, 500, 0.1);
//---- alternate tuning support -----
use_alt_tuning = button("../../use_alt_tuning");

alt_tuning_ratios = (
  hslider("../../alt_tuning_ratio_0",  1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_1",  1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_2",  1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_3",  1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_4",  1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_5",  1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_6",  1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_7",  1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_8",  1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_9",  1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_10", 1.0, 1.0, 2.0, 0.001),
  hslider("../../alt_tuning_ratio_11", 1.0, 1.0, 2.0, 0.001)
);

a3_freq = hslider("../../a3_freq", 440, 300, 600, 0.01);

// standard (unoptimized) version
mtof(note) = a3_freq * pow(2., (note - 69) / 12);

// The reason there are three version of each of these sets of functions is that it is
// the only way I could find to to use alternate tunings in poly mode without excessive 
// computation that gets the device into a boot loop due to the DSP task taking too long 
// to reset the task watchdog in time. This could possibly be regarded as a hand-crafted 
// "partial redundancy" optimization, if I understand that term correctly. Compare poly()
// and poly_quantized().

// optimized version, but disregards a3_freq and just uses 440
// _mtof(note) = 440 * pow(2., (note - 69) / 12);
// mtof(note) = ba.tabulate(0, _mtof, 128, 0, 127, note).val;

// used for poly mode
// _mtof2(note) = _mtof(note) * 2;
// mtof2(note) = ba.tabulate(0, _mtof2, 128, 0, 127, note).val;
mtof2(note) = mtof(note) * 2;

// used for poly mode
// _mtof3(note) = _mtof(note) * 3;
// mtof3(note) = ba.tabulate(0, _mtof3, 128, 0, 127, note).val;
mtof3(note) = mtof(note) * 3;

// convert MIDI note to quantized frequency
// assumes tuning has 12 degrees (11 ratios + assumed octave)!
mtoq(note) = f with {
    n = note % 12;                                          // scale degree (0-11)
    c = note - n;                                           // C note in given octave
    f = mtof(c) * (alt_tuning_ratios : ba.selectn(12, n));  // multiply C frequency by ratio per degree
};

// used for poly mode
mtoq2(note) = f with {
    n = note % 12;                                          // scale degree (0-11)
    c = note - n;                                           // C note in given octave
    f = mtof2(c) * (alt_tuning_ratios : ba.selectn(12, n)); // multiply C frequency by ratio per degree
};

// used for poly mode
mtoq3(note) = f with {
    n = note % 12;                                          // scale degree (0-11)
    c = note - n;                                           // C note in given octave
    f = mtof3(c) * (alt_tuning_ratios : ba.selectn(12, n));  // multiply C frequency by ratio per degree
};
//---- alternate tuning support -----

volume0 = hslider("volume0", 0.25, 0, 1, 0.01) : ba.lin2LogGain : si.smoo;
volume1 = hslider("volume1", 0.25, 0, 1, 0.01) : ba.lin2LogGain : si.smoo;

mix0 = hslider("mix0", 1, 0, 1, 0.01) : si.smoo;
mix1 = hslider("mix1", 1, 0, 1, 0.01) : si.smoo;

vol_wet0 = mix0;
vol_dry0 = (1 - mix0);
vol_wet1 = mix1;
vol_dry1 = (1 - mix1);

mode0 = hslider("mode0", 0, 0, 1, 1);
mode1 = hslider("mode1", 0, 0, 1, 1);

env_mode_change(t) = 1 - en.ar(0.002, env_mode_change_decay, t);
env_mute(t) = 1 - en.asr(0.25, 1., 0.25, t);

req(n) = 62, 115, 218, 411, 777, 1500, 2800, 5200, 11000 : ba.selectn(nHarmonics, n);
cave(n) = par(i, nHarmonics, vslider("cave_freq_%i", req(i), 50, 16000, 1)) : ba.selectn(nHarmonics, n);

pn0 = vslider("poly_note_0", 36, 24, 96, 1);
pn1 = vslider("poly_note_1", 36, 24, 96, 1);
pn2 = vslider("poly_note_2", 36, 24, 96, 1);

// standard tuning poly mode
poly_norm(n) = a, a * 2, a * 3, b, b * 2, b * 3, c, c * 2, c * 3 : ba.selectn(nHarmonics, n)
with
{
    a = pn0 : mtof;
    b = pn1 : mtof;
    c = pn2 : mtof;
};

// alt tuning poly mode
poly_quantized(n, tuning) = a1, a2, a3, b1, b2, b3, c1, c2, c3 : ba.selectn(nHarmonics, n)
with
{
    a1 = pn0 : mtoq;
    a2 = pn0 : mtoq2;
    a3 = pn0 : mtoq3;

    b1 = pn1 : mtoq;
    b2 = pn1 : mtoq2;
    b3 = pn1 : mtoq3;

    c1 = pn2 : mtoq;
    c2 = pn2 : mtoq2;
    c3 = pn2 : mtoq3;
};

poly(n) = poly_norm(n), poly_quantized(n) : ba.selectn(2, use_alt_tuning);

f(n, s) =
    poly(n),
    cave(n)
  : ba.selectn(2, s);

controlledModalStep(sine, cosine, rhoUser, rhoGuard, energyLimit, enabled, x,
                    qPrevious, pPrevious, peakPrevious,
                    gainPrevious, rhoPrevious) = q, p, peak, inputGain, rho
with {
  overload = max(1, peakPrevious / energyLimit)
      : control(modalBlockPulse) : ba.sAndH(modalBlockPulse);
  guardedRho = min(rhoUser, rhoGuard)
      : control(modalBlockPulse) : ba.sAndH(modalBlockPulse);
  boundaryGain = select2(enabled > 0, 1, 1 / overload);
  boundaryRho = select2((enabled > 0) & (overload > 1),
                        rhoUser, guardedRho);
  inputGain = select2(modalBlockPulse, gainPrevious, boundaryGain);
  rho = select2(modalBlockPulse, rhoPrevious, boundaryRho);
  q = rho * (sine * pPrevious + cosine * qPrevious);
  p = inputGain * x + rho * (cosine * pPrevious - sine * qPrevious);
  energy = q * q + p * p;
  peak = select2(modalBlockPulse, max(energy, peakPrevious), energy);
};

controlledModalRotation(sine, cosine, rhoUser, rhoGuard,
                        energyLimit, enabled, x) =
    controlledModalStep(sine, cosine, rhoUser, rhoGuard,
                        energyLimit, enabled, x) ~ si.bus(5);

blockRateMode(freq, t60, gain, x) =
    controlledModalRotation(s, c, rhoReference,
                            anti_feedback_rho_guard,
                            anti_feedback_energy_limit,
                            anti_feedback_enabled, x)
    : _,!,!,!,!
    : *(scale) * gain
    : attach(_, t60)
with {
  rhoUser = pow(0.001, 1.0 / (t60 * ma.SR));
  rhoReference = rhoUser : modalBlockHold;
  theta = 2 * ma.PI * freq / ma.SR;
  c = cos(theta) : modalBlockHold;
  s = sin(theta) : modalBlockHold;
  scale = (2.0 / rhoUser) : modalBlockHold;
};

r(index, source) = blockRateMode(a, b, ba.lin2LogGain(c))
with
{
  a = max(16, min(f(index, source), 16000));
  b = decay;
  c = env_mute(button("mute_%index"));
};

process = _,_
    : fi.dcblocker, fi.dcblocker
    : (_ <: attach(_, _ : an.amp_follower(amp_follower_decay) : _ > left_thresh : hbargraph("left_trig", 0, 1))),
      (_ <: attach(_, _ : an.amp_follower(amp_follower_decay) : _ > right_thresh : hbargraph("right_trig", 0, 1)))
        : hgroup("left", _ * env_mode_change(button("mode_changed")) * volume0),
          hgroup("right", _ * env_mode_change(button("mode_changed")) * volume1)
            <: (_ * resonator_input_gain : fi.lowpass(1, 4000) <: hgroup("left", sum(i, nHarmonics, r(i, mode0))) * pre_clip_gain),
               (_ * resonator_input_gain : fi.lowpass(1, 4000) <: hgroup("right", sum(i, nHarmonics, r(i, mode1))) * pre_clip_gain),
               _,
               _
                : ef.cubicnl(0.01, 0), ef.cubicnl(0.01, 0), _, _
                    : _ * vol_wet0 * post_clip_gain, _ * vol_wet1 * post_clip_gain, _ * vol_dry0, _ * vol_dry1
                //:> co.limiter_1176_R4_mono, co.limiter_1176_R4_mono
                //:> aa.cubic1, aa.cubic1
                        :> (_ * output_gain), (_ * output_gain);
