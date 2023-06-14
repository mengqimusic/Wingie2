declare name        "Wingie";
declare version     "3.0";
declare author      "Meng Qi";
declare license     "BSD";
declare copyright   "(c)Meng Qi 2020";
declare date        "2020-09-30";
declare editDate    "2022-04-26";

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

use_alt_tuning = button("../../use_alt_tuning");
use_alt_harmonics = button("../../use_alt_harmonics");

alt_tuning_ratio_0 =  hslider("../../alt_tuning_ratio_0",  1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_1 =  hslider("../../alt_tuning_ratio_1",  1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_2 =  hslider("../../alt_tuning_ratio_2",  1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_3 =  hslider("../../alt_tuning_ratio_3",  1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_4 =  hslider("../../alt_tuning_ratio_4",  1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_5 =  hslider("../../alt_tuning_ratio_5",  1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_6 =  hslider("../../alt_tuning_ratio_6",  1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_7 =  hslider("../../alt_tuning_ratio_7",  1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_8 =  hslider("../../alt_tuning_ratio_8",  1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_9 =  hslider("../../alt_tuning_ratio_9",  1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_10 = hslider("../../alt_tuning_ratio_10", 1.0, 1.0, 2.0, 0.001);
alt_tuning_ratio_11 = hslider("../../alt_tuning_ratio_11", 1.0, 1.0, 2.0, 0.001);

alt_tuning_ratios = (
  alt_tuning_ratio_0, 
  alt_tuning_ratio_1, 
  alt_tuning_ratio_2, 
  alt_tuning_ratio_3, 
  alt_tuning_ratio_4, 
  alt_tuning_ratio_5, 
  alt_tuning_ratio_6, 
  alt_tuning_ratio_7, 
  alt_tuning_ratio_8, 
  alt_tuning_ratio_9, 
  alt_tuning_ratio_10,
  alt_tuning_ratio_11
);

a3_freq = hslider("../../a3_freq", 440, 300, 600, 0.01);

// standard (unoptimized) version
// mtof(note) = a3_freq * pow(2., (note - 69) / 12);

// optimized version, but disregards a3_freq and just uses 440
_mtof(note) = 440 * pow(2., (note - 69) / 12);
mtof(note) = ba.tabulate(0, _mtof, 128, 0, 127, note).val;

// convert MIDI note to quantized frequency
// assumes tuning has 12 degrees (11 ratios + assumed octave)!
mtoq(note) = f with {
    n = note % 12;                                // scale degree (0-11)
    c = note - n;                                 // C note in given octave
    f = mtof(c) * (alt_tuning_ratios : ba.selectn(12, n));   // multiply C frequency by ratio per degree
};
//---- alternate tuning support -----

//----- harmonics sets -----
harm_std   = (1, 2, 3, 4, 5,  6,  7,  8,  9);
harm_fib   = (1, 2, 3, 5, 8, 13, 21, 34, 55);
harm_prime = (1, 2, 3, 5, 7, 11, 13, 17, 19);

harm_ratios(freq, n, set) = f with {
  h = set : ba.selectn(nHarmonics, n);
  f = freq * h;
};
//----- harmonics sets -----

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

std_ratios(freq, n)   = harm_ratios(freq, n, harm_std);
fib_ratios(freq, n)   = harm_ratios(freq, n, harm_fib);
prime_ratios(freq, n) = harm_ratios(freq, n, harm_prime);

req(n) = 62, 115, 218, 411, 777, 1500, 2800, 5200, 11000 : ba.selectn(nHarmonics, n);
cave(n) = par(i, nHarmonics, vslider("cave_freq_%i", req(i), 50, 16000, 1)) : ba.selectn(nHarmonics, n);

poly(n) = a, a * 2, a * 3, b, b * 2, b * 3, c, c * 2, c * 3 : ba.selectn(nHarmonics, n)
with
{
    a = vslider("poly_note_0", 36, 24, 96, 1) : mtof;
    b = vslider("poly_note_1", 36, 24, 96, 1) : mtof;
    c = vslider("poly_note_2", 36, 24, 96, 1) : mtof;
};

poly_quantized(n, tuning) = a, a * 2, a * 3, b, b * 2, b * 3, c, c * 2, c * 3 : ba.selectn(nHarmonics, n)
with
{
    a = mtoq(vslider("poly_note_0", 36, 24, 96, 1), tuning);
    b = mtoq(vslider("poly_note_1", 36, 24, 96, 1), tuning);
    c = mtoq(vslider("poly_note_2", 36, 24, 96, 1), tuning);
};

//bianzhong(n) = 212.3, 424.6, 530.8, 636.9, 1061.6, 1167.7, 2017.0, 2335.5, 2653.9, 3693 : ba.selectn(10, n);
//cymbal_808(n) = 205.3, 304.4, 369.6, 522.7, 540, 615.9, 800, 913.2, 1108.8, 1568.1 : ba.selectn(10, n); // original
//circular_membrane_ratios(n) = 1, 1.59, 2.14, 2.30, 2.65, 2.92, 3.16, 3.50, 3.60, 3.65 : ba.selectn(10, n);

// note_ratio(note) = pow(2., note / 12);

get_freq(note) =
  mtof(note),
  mtoq(note)
  : ba.selectn(2, use_alt_tuning);

get_harmonics(note, n) =
  int_ratios(get_freq(note), n),
  std_ratios(get_freq(note), n)
  : ba.selectn(2, use_alt_harmonics);

get_poly(n) =
  poly(n),
  poly_quantized(n)
  : ba.selectn(2, use_alt_tuning);

get_bar(note, n, tuning) =
  bar_ratios(mtof(note), n),
  bar_ratios(mtoq(note), n)
  : ba.selectn(2, use_alt_tuning);

f(note, n, s) = 
    poly(n),
    // get_poly(n),
    get_harmonics(note, n),
    get_bar(note, n),
    //odd_ratios(ba.midikey2hz(note), n),
    //cymbal_808(n) * note_ratio(note - 48),
    cave(n)
  : ba.selectn(4, s);

scale(x, in_low, in_high, out_low, out_high, e) = (out_low + (out_high - out_low) * ((x - in_low) / (in_high - in_low)) ^ e);

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
