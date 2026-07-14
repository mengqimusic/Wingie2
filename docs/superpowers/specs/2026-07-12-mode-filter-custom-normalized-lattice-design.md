# Wingie2 Custom Normalized-Lattice Mode Filter Design

## Decision Status

This is a new mode-filter feasibility design. It does not resume or modify the rejected
TF2NP, coupled-form, or block-rate rotation implementation plans.

The approved candidate set is:

1. Candidate E: an exact, factorized numerator feeding a custom normalized all-pole
   lattice;
2. Candidate R: the same custom lattice with its raw terminal tap as output.

Both candidates receive the same generation, stability, peak, deadline, and hardware gates.
Candidate E additionally receives historical static-parity gates. Static response and
listening trade-offs between the surviving candidates remain a user decision. No
implementation begins until this spec is reviewed.

## Evidence Sources

The design is based on:

- `docs/superpowers/results/2026-07-10-mode-filter-tf2np-results.md`;
- `docs/superpowers/results/2026-07-11-coupled-mode-filter-results.md`;
- `docs/superpowers/results/2026-07-11-block-rate-modal-bank-results.md`;
- the current `Wingie2.dsp` product graph;
- the current firmware control paths in `Wingie2/MIDI.ino`, `Wingie2/control.ino`, and
  `Wingie2/Wingie2.ino`;
- the pinned Faust 2.59.6 `filters.lib` and `physmodels.lib` sources;
- archived generated artifacts and ESP32 benchmark logs from the three rejected
  experiments.

Archived branches, tags, generated candidates, and measurement infrastructure are evidence.
They are not implementation sources to restore or merge wholesale.

## Goal

Replace the historical direct-form `pm.modeFilter` recurrence with a custom
normalized-lattice bank that:

- remains bounded under arbitrary rapid note changes and future harmonic-ratio jumps;
- gives each isolated mode one principal narrow peak;
- preserves the historical firmware-to-Faust UI parameter paths;
- meets the ESP32 deadline;
- keeps mute as a direct-output gate while the input and lattice state continue;
- does not add control-side compensation, smoothing, reset, crossfade, fallback, or a new
  time constant.

## Fixed Boundaries

- Faust is fixed at `2.59.6`.
- ESP32 Arduino Core is fixed at `2.0.4-cn`.
- Audio runs at 44.1 kHz with 32-sample blocks on a 240 MHz ESP32.
- The hard block period is approximately `725.6 us`.
- The pre-registered CPU gate is p99 `<= 580.5 us`, max `< 725.6 us`, and zero misses over
  100,000 measured blocks.
- Coefficients may be prepared once at the start of each physical 32-sample audio block.
  This is a block scheduling boundary, not interpolation, smoothing, or a new time constant.
- A legal note, mode, Cave frequency, A3 value, or alternate-tuning ratio is never delayed,
  discarded, rate-limited, or rewritten by firmware compensation.
- No note or ratio change clears, copies, or crossfades lattice state.
- No runtime NaN reset, automatic recovery, or fallback filter is allowed.
- Candidate listening acceptance requires the user's explicit physical confirmation.

## Current Product Audit

### Historical Resonator

The current `pm.modeFilter` is:

```text
H(z) = (1 - z^-2) / (1 - 2 rho cos(theta) z^-1 + rho^2 z^-2)

theta = 2 pi frequency / SR
rho   = 0.001^(1 / (t60 SR))
```

Each channel has nine independent filters. They share one conditioned audio input and one
channel Decay trajectory, but have independent frequencies and output mute controls.

The current DSP applies:

- an upper frequency cap at 16 kHz;
- the existing `env_mode_change` envelope to input amplitude and effective T60;
- a 250 ms ASR mute envelope;
- an additional automatic output mute when the capped frequency equals 16 kHz.

This design retains the existing mode-change envelope and its historical time constants. It
changes only mute semantics: the 250 ms mute ASR and automatic 16 kHz output mute are removed.
A mode at either frequency boundary remains active and audible unless its explicit user mute
is set.

### Frequency Contract

Each of the 18 target frequencies is independently clipped inside the DSP to the closed
interval `[16, 16000] Hz` before lattice coefficient preparation.

Frequencies:

- need not be sorted or unique;
- may cross or become equal;
- may jump to either boundary;
- may change as a complete nine-value ratio set on every block;
- take effect together at the next physical block, at most about `0.73 ms` later.

The bank does not know whether a frequency came from Poly, String, Bar, Cave, standard
tuning, alternate tuning, A3, or a future ratio mapping.

### Historical UI Contract

The following existing control families remain addressable with their current names and
left/right grouping:

- `note0`, `note1`;
- `mode0`, `mode1`;
- `/Wingie/{left,right}/poly_note_0..2`;
- `/Wingie/{left,right}/cave_freq_0..8`;
- `/Wingie/{left,right}/decay`;
- `/Wingie/{left,right}/mute_0..8`;
- `/Wingie/{left,right}/mode_changed`;
- `a3_freq`, `use_alt_tuning`, and `alt_tuning_ratio_0..11`;
- the historical input gain, volume, mix, threshold, and pre/post clip controls.

The generated UI inventory must prove that left and right `mode_changed` remain distinct.
The block-rate rotation experiment's compiler-coalesced `/Wingie/mode_changed` path is a
known regression and an immediate rejection condition.

### Control Mutation Sources

The current firmware directly changes resonator frequencies through:

- MIDI Note On in String and Bar modes;
- the three rotating Poly note slots;
- keyboard note and octave changes;
- Tap Sequence advances;
- per-mode Cave MIDI and keyboard edits;
- mode changes;
- A3 changes;
- standard/alternate tuning selection and twelve alternate ratios.

All of these remain direct control paths. The lattice, not firmware filtering, owns stability
under their discontinuities.

## Existing Measurement Audit

### Recorded Results

| Artifact | Generated-class p99 | Complete-product p99 | Outcome |
| --- | ---: | ---: | --- |
| Contemporary historical direct form | `739.5 us` | `715.9 us` | CPU gate failed |
| Direct form with block-rate decay `pow` | `419.1 us` | `456.3 us` | CPU passed; inherited high-rate MIDI trigger stopped wet in old pressure run; later normal retest has wet |
| Coupled Candidate Q | `544.7 us` | `714.6 us` | product timing gate failed |
| Coupled Candidate D | `544.4 us` | `715.2 us` | product timing gate failed |
| Block-rate rotation bank | `450.0 us` | `717.1 us` | product timing gate failed |

The direct-form block-rate experiment proves that moving the two channel decay `pow` calls
off the sample-rate path can recover a large amount of CPU. The old wet disappearance is now
attributed to the already-reproduced high-rate variable-pitch MIDI trigger inherited from the
historical direct form, not to the block-rate coefficient preparation itself. A 2026-07-14
normal-firmware retest confirmed wet. The old experiment decision remains historical evidence,
but it no longer disqualifies the candidate under the current hardware-run rule.

The same follow-up trial also confirmed that normal Coupled D and block-rate modal-bank firmware
work on hardware. Those confirmations do not add unrecorded pressure or sound-preference results.

The coupled and rotation experiments prove that bounded two-state recurrences can pass the
generated-class gate while still failing the recorded complete-product gate. Neither result
contains a candidate listening pass.

### Complete-Product Timer Limitation

The archived complete-product instrumentation starts after blocking `i2s_read` and stops
after blocking `i2s_write(..., portMAX_DELAY)`. It includes:

- deterministic input replacement;
- DSP compute;
- output conversion;
- time blocked waiting for the I2S/DMA write path.

The repeated `708..717 us` distributions therefore cannot be interpreted as CPU compute
cost or used to subtract one recurrence's cost from another. They remain useful as an
end-to-end deadline observation, but the next experiment must add non-invasive segmented
cycle measurements for:

1. input conversion;
2. `fDSP->compute(32)`;
3. output conversion;
4. blocking I2S write wait;
5. the existing full post-read to post-write interval.

The original end-to-end gate remains in force. Segmented measurements explain failures;
they do not weaken or replace that gate.

## Custom Lattice Coefficients

For each channel-level effective T60 and each mode frequency:

```text
rho = 0.001^(1 / (t60 SR))
a1  = -2 rho cos(theta)
a2  = rho^2

s2 = a2
s1 = a1 / (1 + a2)
c2 = sqrt(1 - s2^2)
c1 = sqrt(1 - s1^2)
```

`s1` and `s2` are projected only against the numeric normalized-lattice stability boundary
`[-(1-EPSILON), +(1-EPSILON)]`. This projection is part of the DSP structure, not a
control-side frequency correction.

The two scattering sections use the `(s, c)` pairs directly. The generated sample loop must
show two paired normalized junctions per mode, not `fi.tf2np`, `fi.nlf2`, a coupled rotation,
or a direct-form biquad hidden behind another library call.

For mode input `x` and stored states `z1/z2`, the specialized recurrence is:

```text
t2      = c2 * x  - s2 * z2
t1      = c1 * t2 - s1 * z1
z2_next = s1 * t2 + c1 * z1
z1_next = t1
raw_terminal = t1
```

For zero input, the two normalized junctions give
`z1_next^2 + z2_next^2 = z1^2 + s2^2 * z2^2`, so stored energy cannot grow for
`abs(s2) <= 1`, independently of an instantaneous `s1` change. Host tests still verify the
floating-point implementation and arbitrary simultaneous coefficient changes.

With the historical effective T60 expression, the widest relevant T60 interval is
approximately `0.05..10.05 s`. At `16 Hz / 10.05 s`:

```text
rho              = 0.999984414207
s1               = -0.999997401560
s2               = 0.999968828657
c1               = 0.002279665023
c2               = 0.007895676986
1 / (c1 c2)      = 55557.102959
```

The reflection coefficients remain inside the stability region. Candidate E's output scale
has a finite but large worst-case value, so finite coefficients alone do not pass its
numeric-output gate.

### Stability Obligation

Each normalized scattering junction must satisfy the instantaneous energy identity implied
by `s^2 + c^2 = 1`, within the registered floating-point tolerance. The proof and generated
structure must cover arbitrary block-to-block changes of both reflection coefficients.

The host contract must demonstrate, rather than assume, that with zero input:

- stored lattice energy satisfies
  `energy[n+1] <= energy[n] + 1e-5 * max(1, energy[n])` at every sample after excitation;
- arbitrary frequency and ratio jumps do not clear the tail;
- all states remain finite;
- repeated initial state and control sequences produce identical output.

## Candidate E: Exact Factorized Output

Candidate E factors the historical fixed-coefficient transfer function into a shared input
difference and a custom normalized all-pole lattice:

```text
d[n] = x[n] - x[n-2]
u_i  = normalized_all_pole_lattice_i(d)
y_i  = u_i * (1 / (c1_i c2)) * gain_i * (1 - mute_i)
```

The two-sample difference is computed once per channel before the input fans out to nine
modes. It adds two delay states per channel, not per mode.

For held coefficients, Candidate E must reproduce:

```text
(1 - z^-2) / (1 + a1 z^-1 + a2 z^-2)
```

within registered numeric tolerances. Factoring the numerator ahead of the all-pole lattice
avoids the three dynamically scaled output taps and their large-number cancellation in the
general TF2NP construction.

When coefficients jump, Candidate E is not required to sample-match the historical
direct-form transient. Its lattice state remains in normalized coordinates and immediately
uses the new reflection coefficients and output scale.

Candidate E risks amplification of floating-point residue by the finite worst-case output
normalization. It is rejected if bounded normalized state still produces a non-finite,
constant, or over-limit product output.

## Candidate R: Raw Terminal Tap

Candidate R feeds the public input directly into the same two-section lattice and exposes
the raw terminal tap:

```text
u_i = normalized_lattice_raw_terminal_i(x)
y_i = u_i * gain_i * (1 - mute_i)
```

It does not apply the shared two-sample input difference or `1/(c1_i c2)` output
normalization.

Candidate R keeps the same pole frequencies and normalized state structure as Candidate E,
but deliberately changes:

- DC and broad-band response;
- relative modal levels across frequency and Decay;
- static gain;
- excitation and rapid-retuning character.

These differences are measured and reported. They are not rejected by comparison with the
historical transfer function. Candidate R is accepted by machine tests only if it retains
one principal narrow peak, satisfies the pitch and boundedness gates, and meets deadline.
Physical desirability remains a user listening decision.

## Mute And State Semantics

For both candidates:

```text
state_i[n+1] = lattice_update(state_i[n], common_input[n], held_coefficients_i)
visible_i[n] = raw_output_i[n] * gain_i * (1 - mute_i)
```

Mute must not affect:

- common input delivery;
- either lattice state update;
- coefficient preparation;
- state energy;
- future optional internal interactions.

A mode muted for a long interval must continue responding internally. Unmuting exposes its
current state without reset or crossfade. The generated sample loop must contain no mute
envelope state and no branch that skips recurrence work.

## Cost Model

### Block-Rate Coefficient Preparation

Across both channels and 18 modes, the custom lattice is expected to require per physical
32-sample block:

```text
2 pow        channel rho
18 cos       mode frequency
20 sqrt      18 mode c1 plus 2 channel c2
```

The channel terms `rho`, `rho^2`, `1 + rho^2`, and `c2` must be computed twice and shared
across the nine modes. `s1` and `c1` remain mode-specific.

Candidate E additionally needs approximately 18 mode output-normalization reciprocals per
block. They must be prepared and held outside the sample-rate loop. Candidate R needs no
such reciprocal.

The old direct `fi.tf2np` expansion contained two `pow` and 24 `sqrt` calls per sample in a
reduced product graph, or 768 `sqrt` calls per 32-sample block. The custom bank's estimated
20 `sqrt` calls per block are a different computational design, not a resumed `fi.tf2np`
replacement.

Existing note-to-frequency work, including alternate-tuning `pow` calls, remains part of the
full product and must be counted separately from lattice coefficient preparation.

### Sample-Rate Recurrence

The analytical recurrence estimate per mode per sample is:

```text
two normalized junctions: 6 multiplies + 3 adds/subtracts
combined visible output:  approximately 1 multiply
```

Candidate E adds one shared input subtraction per channel per sample. Candidate R does not.
Both retain 36 filter states across the product. Candidate E adds four channel-level input
delay states.

The normalized-lattice and block-rate rotation cores both use approximately six multiplies
and three adds per mode per sample. Their principal cost difference is block-rate coefficient
preparation: the lattice replaces 18 rotation `sin` calls with 20 normalized-junction `sqrt`
calls, and Candidate E adds output reciprocals. Replacing the current 18 mute ASRs with
direct output gates removes their states and arithmetic. The net Xtensa cost cannot be
established from operation counts alone.

The measured rotation and coupled generated-class p99 values, `450.0 us` and approximately
`544.5 us`, define an empirical feasibility interval, not a prediction. Both lattice
candidates must meet `580.5 us` directly.

## Minimal Falsification Sequence

The experiment order targets the cheapest decisive failure first.

### Gate 1: Complete Faust Generation And UI Inventory

Generate each candidate as a complete two-channel, 18-mode product graph with Faust 2.59.6
and alternate tuning enabled. Do not begin with a reduced no-alt-tuning product as evidence
of integration feasibility.

Reject a candidate immediately if:

- Faust reports a box or recursive-composition failure;
- any historical UI path is absent or merged;
- left and right `mode_changed` are not independently addressable;
- the generated product contains `tf2np`, `nlf2`, the rejected coupled recurrence, or the
  rejected rotation-bank recurrence;
- candidate-specific coefficients or state paths are duplicated beyond the approved count.

### Gate 2: Generated Structure And Operation Placement

The complete generated user section must prove:

- exactly two channel decay `pow` preparations at block rate;
- exactly 18 lattice-frequency `cos` preparations at block rate;
- exactly 18 mode `c1` and two channel `c2` `sqrt` preparations at block rate;
- no lattice `pow`, `sqrt`, `sin`, `cos`, or division in the sample-rate recurrence;
- 36 paired lattice state updates on every sample;
- no mute ASR state and no mute-controlled recurrence branch;
- Candidate E has only four extra shared differentiator delay states;
- Candidate R has no differentiator or output-normalization reciprocal.

The checker must distinguish existing tuning `pow` calls from the two lattice decay `pow`
calls. Merely counting all generated `pow` calls is invalid.

### Gate 3: Isolated Mathematical And Host Contract

Drive both candidates with deterministic impulse, noise, sine, and bounded mixed inputs.
Cover at minimum:

- frequency boundaries `16 Hz` and `16 kHz`;
- effective T60 boundaries `0.05 s` and `10.05 s`;
- duplicate, crossing, reverse, and arbitrary unordered nine-frequency sets;
- block-by-block jumps between the two frequency boundaries;
- slow notes, rapid notes, mode changes, and future-ratio-equivalent nine-value jumps;
- standard and alternate tuning;
- long mute intervals followed by unmute.

Require finite state/output, registered state-energy bounds, deterministic repeatability,
active tails after coefficient jumps, and proof that mute did not stop state progression.
The maximum absolute pre-nonlinearity candidate output is `8.0`. An active response or tail
has RMS greater than `1e-9`; this activity threshold proves continuity but is not a gain or
listening-quality gate.

Candidate E must also meet the historical fixed-coefficient transfer-function comparison.
Candidate R records gain, T60, DC, and full-band response without treating historical parity
as a gate.

### Gate 4: Peak Identity

Reuse the pre-registered block-rate modal-bank peak rule rather than tuning a new detector
after observing the candidates:

- 524,288-sample impulse render at Decay `10 s`;
- one-sided FFT;
- target is the maximum local peak;
- significant-peak prominence threshold is `6 dB`;
- exactly one significant peak per isolated mode;
- center-frequency error `<= +/-5 cents` relative to the clipped target.

For Candidate E, comparison with the historical fixed-coefficient filter must additionally
meet center-frequency error `<= 1 cent`, relative T60 error `<= 2%`, and peak-gain error
`<= 0.25 dB`. Candidate R is not required to match historical static gain, T60, or spectral
tilt.

### Gate 5: Generated-Class ESP32 Deadline

Measure each exact generated class over 100,000 blocks after warmup at 240 MHz, 44.1 kHz,
and 32 samples per block. During measurement, change complete legal note/frequency/ratio
sets every block and use nonzero deterministic audio input.

Reject immediately on:

- p99 `> 580.5 us`;
- max `>= 725.6 us`;
- any deadline miss;
- watchdog reset;
- non-finite or constant output.

No product candidate firmware or listening test is built after this failure.

### Gate 6: Segmented Complete-Product Deadline

Only generated-class survivors enter complete-product measurement. Record 100,000 blocks
for each approved segment and the original end-to-end interval while applying real rapid
String and Cave/future-ratio-equivalent control pressure.

The original p99/max/miss gate remains mandatory for the complete interval. Segment data
must identify whether any failure comes from DSP compute, conversion, I2S wait, or probe
overhead. Instrumentation identity, overhead, and placement must be reported so that the
probe does not silently redefine the tested path.

### Gate 7: Firmware Pressure And User Listening

Only candidates that pass every machine gate receive normal and diagnostic app0 images.
Pressure covers both channels in String, Poly, Bar, Cave, and future-ratio-equivalent
stimuli, including mute/unmute after long state evolution.

For each candidate, report separately:

- MIDI sent, parsed, callback, and error counts;
- DSP parameter markers;
- target wet, other wet, and dry/thru behavior;
- reset, stall, dropout, and deadline observations;
- static pitch, Decay, gain, dynamics, and rapid-retuning listening notes.

If both candidates reach listening, the user chooses between E, R, or neither. Machine
metrics do not select the sound.

## Hardware And Recovery Boundary

Any later hardware experiment writes candidate data only to app0 at `0x10000`. It must not
write bootloader, partition table, otadata, or NVS.

After every candidate hardware stage:

1. restore the historical app0;
2. verify the complete historical image;
3. read back and compare `0x140000` bytes from app0;
4. confirm MIDI channels `1/2/3`, A3 `440`, Poly/Poly, and standard tuning;
5. wait for the user's explicit confirmation that both restored wet paths are normal.

## Product And Repository Boundary

Implementation and measurement require a new isolated worktree and branch. The rejected
TF2NP, coupled, and rotation branches remain archives.

Before all machine and physical gates pass:

- candidate DSP and generated products stay outside the product tree;
- `Wingie2.dsp`, `Wingie2/Wingie2.cpp`, and `Wingie2/Wingie2.h` remain historical;
- no candidate is merged into `main`;
- no candidate is left installed on the device by default.

Only one user-approved candidate may eventually update the DSP source and its same-batch
Faust 2.59.6 generated C++ and header. If both candidates fail, main receives only a concise
result document and the custom normalized-lattice route is closed until a new design is
approved.

## Acceptance Summary

A candidate is product-eligible only if all of the following are true:

1. complete Faust 2.59.6 generation with alternate tuning succeeds;
2. all historical UI paths remain correct and distinct;
3. generated structure and coefficient-sharing checks pass;
4. arbitrary coefficient jumps remain finite, bounded, deterministic, and state-continuous;
5. every isolated mode has one principal narrow peak within `+/-5 cents`;
6. generated-class and segmented complete-product ESP32 deadline gates pass;
7. full two-channel hardware pressure passes;
8. the user explicitly accepts its physical response and listening behavior.

Candidate E additionally requires historical fixed-coefficient transfer-function parity.
Candidate R explicitly does not; its static response is a reported listening trade-off.
