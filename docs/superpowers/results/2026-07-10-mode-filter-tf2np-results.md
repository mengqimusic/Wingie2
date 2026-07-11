# Wingie2 TF2NP Feasibility Result

## Decision

`rejected / do not execute the existing TF2NP plan`.

The direct-form deadline optimization prerequisite was rejected by its physical audio gate. The subsequent read-only audit also found that the existing direct `fi.tf2np` replacement cannot be generated for the complete product graph with Faust 2.59.6 and alternate tuning enabled. No TF2NP product code was accepted or merged into `main`.

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

The candidate was nevertheless rejected during String channel 2 hardware pressure. MIDI parsing and callbacks completed, but the user reported `当前所有通道都没有 wet`. Later Poly/Bar batches and candidate A/B were skipped. Numerical gates do not override this physical rejection.

The candidate commit was reverted in the archive. The final device recovery wrote only app0 at `0x10000`, verified the complete historical image, compared a full `0x140000`-byte readback, and restored startup state. Historical app0 SHA-256:

```text
6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b
```

The user explicitly confirmed that both restored wet paths were normal.

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
