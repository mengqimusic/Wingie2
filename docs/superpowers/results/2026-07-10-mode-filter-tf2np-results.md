# Wingie2 TF2NP Feasibility Result

## Decision

`rejected / do not execute the existing TF2NP plan`.

The direct-form deadline optimization prerequisite was historically rejected by its physical audio gate. A later normal-firmware retest confirmed that the candidate has wet; the old disappearance is attributable to the inherited high-rate variable-pitch MIDI trigger described below, not to a static absence of candidate wet. The subsequent read-only audit still found that the existing direct `fi.tf2np` replacement cannot be generated for the complete product graph with Faust 2.59.6 and alternate tuning enabled. No TF2NP product code was accepted or merged into `main`.

The complete experiment history is preserved at tag:

```text
archive/tf2np-deadline-rejected-2026-07-11
```

Its final commit is `6711eee`. This summary is the durable `main` record; the archived branch is evidence, not an implementation source to merge wholesale.

## Deadline Optimization Outcome

The 32-sample direct-form coefficient candidate passed its host and ESP32 machine gates:

- generated class, 100,000 blocks at 240 MHz: p99 `419.1 us`, max `424.071 us`, zero deadline misses;
- end-to-end audio, 100,000 blocks at 240 MHz: p99 `456.3 us`, max `469.642 us`, zero deadline misses;
- three settled 180-case matrices and one 24-case dynamic matrix: zero numerical failures.

The candidate was nevertheless rejected during the original String channel 2 hardware pressure. MIDI parsing and callbacks completed, but the user reported `当前所有通道都没有 wet`. Later Poly/Bar batches and candidate A/B were skipped under the then-current gate.

The candidate commit was reverted in the archive. The final device recovery wrote only app0 at `0x10000`, verified the complete historical image, compared a full `0x140000`-byte readback, and restored startup state. Historical app0 SHA-256:

```text
6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b
```

The user explicitly confirmed that both restored wet paths were normal.

## 2026-07-14 Follow-Up Trial

A normal product image from the same frozen block-rate direct-form candidate was rebuilt as
1,189,424 bytes with SHA-256
`e110a3e11a362e06f1e5c37b467d33143d5b9b42b0eaebadafa7a9a4e89a3ce5` and flashed through
the candidate tool. The user confirmed that block-rate-direct has wet.

The earlier disappearance was triggered by the high-rate variable-pitch MIDI pressure, not
by a candidate-specific missing-wet condition. The historical direct-form diagnostic had
already reproduced the same sufficient trigger independently on both sides: String mode,
about 1 kHz pitch changes, intact parser/callback/Faust parameter updates, and loss of the
pressured side's wet/resonator output. In the block-rate run, all wet was still audible after
the channel 1 batch and disappeared only after the channel 2 batch. That sequence supports
the pressure trigger, but it does not prove that two independent per-side failures occurred
in that run. The exact internal mechanism and the reason all wet disappeared remain
unmeasured; the retained evidence does not distinguish NaN/Inf, a large finite state, or
another realtime floating-point state failure.

## Direct TF2NP Audit

The pinned Faust 2.59.6 `filters.lib` implements `fi.tf2np` as an epsilon-protected normalized-ladder filter. It is not a lightweight `fi.tf2` substitution:

- a representative dynamic-T60 filter expands to one `pow` and four `sqrt` calls per sample;
- a product expansion with alternate tuning disabled contains two `pow` and 24 `sqrt` calls per sample across both 9-resonator channels;
- retaining the real alternate-tuning path makes Faust 2.59.6 fail box compilation with `recursive composition A~B`.

The contemporary direct-form end-to-end baseline already measured p99 `715.9 us`, above the `580.5 us` p99 gate. Therefore sample-rate coefficient preparation with the existing direct `fi.tf2np` source graph is not viable under the fixed toolchain.

This does not prove normalized-ladder filtering is mathematically impossible. A future attempt requires a new design and plan based on a complete DSP/generated-code audit, potentially evaluating a custom normalized-ladder form or shared/block-rate coefficient preparation. It must not resume or edit the rejected TF2NP plan as an implementation checklist.

## Mainline Boundary

- Accepted and merged separately: the 64-note rolling Tap Sequencer.
- Not merged: TF2NP candidate/revert commits, deadline experiment plans, generated candidates, and experiment-only benchmark infrastructure.
- Fixed constraints retained: Faust 2.59.6, ESP32 Arduino Core 2.0.4, no control-side compensation, and physical listening PASS only after explicit user confirmation.
