declare name        "Wingie";
declare version     "3.1";
declare author      "Meng Qi";
declare author      "Dave Seidel";
declare license     "BSD";
declare copyright   "(c)Meng Qi 2020";
declare date        "2020-09-30";
declare editDate    "2023-06-23";

//-----------------------------------------------
// Wingie
//-----------------------------------------------

import("stdfaust.lib"); 

nHarmonics = 9;
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
bar_factor = 0.44444;

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

note0 = hslider("note0", 36, 12, 127, 1);
note1 = hslider("note1", 36, 12, 96, 1);
mode0 = hslider("mode0", 0, 0, 4, 1);
mode1 = hslider("mode1", 0, 0, 4, 1);

env_mode_change = 1 - en.ar(0.002, env_mode_change_decay, button("mode_changed"));
env_mute(t) = 1 - en.asr(0.25, 1., 0.25, t);

bar_ratios(freq, n) = freq * bar_factor * pow((n + 1) + 0.5, 2);
int_ratios(freq, n) = freq * (n + 1);
//odd_ratios(freq, n) = freq * (2 * n + 1);
//cymbal_808(n) = 130.812792, 193.957204, 235.501256, 333.053319, 344.076511, 392.438376, 509.742979, 581.871611, 706.503769, 999.16, 1032.222378, 1529.218338: ba.selectn(12, n); // chromatic

req(n) = 62, 115, 218, 411, 777, 1500, 2800, 5200, 11000 : ba.selectn(nHarmonics, n);
cave(n) = par(i, nHarmonics, vslider("cave_freq_%i", req(i), 50, 16000, 1)) : ba.selectn(nHarmonics, n);

pn0 = vslider("poly_note_0", 36, 24, 96, 1);
pn1 = vslider("poly_note_1", 36, 24, 96, 1);
pn2 = vslider("poly_note_2", 36, 24, 96, 1);

// standard tuning poly mode
poly(n) = a, a * 2, a * 3, b, b * 2, b * 3, c, c * 2, c * 3 : ba.selectn(nHarmonics, n)
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

//bianzhong(n) = 212.3, 424.6, 530.8, 636.9, 1061.6, 1167.7, 2017.0, 2335.5, 2653.9, 3693 : ba.selectn(10, n);
//cymbal_808(n) = 205.3, 304.4, 369.6, 522.7, 540, 615.9, 800, 913.2, 1108.8, 1568.1 : ba.selectn(10, n); // original
//circular_membrane_ratios(n) = 1, 1.59, 2.14, 2.30, 2.65, 2.92, 3.16, 3.50, 3.60, 3.65 : ba.selectn(10, n);

// note_ratio(note) = pow(2., note / 12);

poly(n) = poly(n), poly_quantized(n) : ba.selectn(2, use_alt_tuning);

note_freq(note) = mtof(note), mtoq(note) : ba.selectn(2, use_alt_tuning);
strings(note, n) = int_ratios(note_freq(note), n);

bars(note, n, tuning) = bar_ratios(mtof(note), n), bar_ratios(mtoq(note), n) : ba.selectn(2, use_alt_tuning);

f(note, n, s) = 
    poly(n),
    strings(note, n),
    bars(note, n),
    //odd_ratios(ba.midikey2hz(note), n),
    //cymbal_808(n) * note_ratio(note - 48),
    cave(n)
  : ba.selectn(4, s);

// scale(x, in_low, in_high, out_low, out_high, e) = (out_low + (out_high - out_low) * ((x - in_low) / (in_high - in_low)) ^ e);

r(note, index, source) = pm.modeFilter(a, b, ba.lin2LogGain(c))
with
{
  a = min(f(note, index, source), 16000);
  //decay_factor = scale(a, 8, 16000, 1, 0, 0.4);
  b = (env_mode_change * decay) + 0.05;
  c = env_mute(button("mute_%index")) * (ba.if(a == 16000, 0, 1) : si.smoo);
};

process = _,_
    : fi.dcblocker, fi.dcblocker
    : (_ <: attach(_, _ : an.amp_follower(amp_follower_decay) : _ > left_thresh : hbargraph("left_trig", 0, 1))),
      (_ <: attach(_, _ : an.amp_follower(amp_follower_decay) : _ > right_thresh : hbargraph("right_trig", 0, 1)))
        : (_ * env_mode_change * volume0), (_ * env_mode_change * volume1)
            <: (_ * resonator_input_gain : fi.lowpass(1, 4000) <: hgroup("left", sum(i, nHarmonics, r(note0, i, mode0))) * pre_clip_gain),
               (_ * resonator_input_gain : fi.lowpass(1, 4000) <: hgroup("right", sum(i, nHarmonics, r(note1, i, mode1))) * pre_clip_gain),
               _,
               _
                : ef.cubicnl(0.01, 0), ef.cubicnl(0.01, 0), _, _
                    : _ * vol_wet0 * post_clip_gain, _ * vol_wet1 * post_clip_gain, _ * vol_dry0, _ * vol_dry1
                //:> co.limiter_1176_R4_mono, co.limiter_1176_R4_mono
                //:> aa.cubic1, aa.cubic1
                        :> (_ * output_gain), (_ * output_gain);
