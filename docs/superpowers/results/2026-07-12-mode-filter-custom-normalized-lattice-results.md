# Wingie2 Custom Normalized-Lattice Feasibility Result

## Decision

`retain E and R as hardware-trial candidates; install neither into main`.

The original pre-registered gates rejected Candidate E on `isolated_output_bound`, so its
`601` complete-product host rows were skipped. Candidate R completed all `601` rows but
recorded six 16 Hz FFT peak estimates at `+8.009839 cents`, outside the old `+/-5 cents`
gate.

Later user-authorized normal-firmware trials established that both E and R ran on the
physical device and produced usable audio for the reported exposure. On 2026-07-14 the user
changed the sole elimination gate to:

> `唯一硬门禁是‘方案是否可以在硬件上跑得动’，其他都等真机试用`

E and R therefore remain trial candidates under the current rule. This reclassifies the old
host and p99 results as non-eliminating evidence; it does not erase the historical results,
turn an unrun test into a pass, select a candidate, or install either candidate in `main`.

## Toolchain And Artifact Identity

The fixed toolchain and architecture identities were:

```text
464d9ba249bea4b3589ad0ba5c7e8bf266b4f7e649523e68a022f73a164032c2  faust 2.59.6
6f82542b67778a5f46edc0fc29bf18b5b7d0337b43689f6f2dd07a08c6da45fa  filters.lib
4f7e5f369400f06c16343240db61bfec56f3f93c96f817250262cbc9c9499d71  oscillators.lib
a35260d0c5a00ebf81367107bf3f626619a9fd85995b596b06a045ec45c80e45  esp32.cpp
627c103f5d35aa331ca1e26638cee92f96ab08c511f1603921cd84e260d0f2e3  esp32.h
569c14e6cec03b5d8e51fec6ce5cbd990d685601f580b6a0c16c6429fad25921  esp32-dsp.h
```

Hardware measurements used ESP32 Arduino Core `2.0.4-cn`, a 240 MHz CPU, 44,100 Hz audio,
32-sample blocks, and a block period of about `725.6 us`.

The repository product triplet remained the historical direct-form implementation:

```text
fad94a49a0fe888aea854cdb3b02ab073a982909939db433431b609d1b6fbbd1  Wingie2.dsp
b9414abf86b17e42993479861ce81d3e70c56158351b13ab9d9ea1af195f6c39  Wingie2/Wingie2.cpp
525d057742d2894b281f87b072e73b6f52e3c83140385d52268bc7d5baaf5263  Wingie2/Wingie2.h
```

The immutable candidate manifests reverified all three entries:

| Candidate | DSP SHA-256 | Generated C++ SHA-256 | Header SHA-256 |
| --- | --- | --- | --- |
| E | `8a263f033a8e2cf2b82778b660231486cdeeff52f241d6092a5c483a167926cc` | `6ce72feba061c30f714ee3b7cf3d71169cbe62f2f39e5e95c53df20971425bb5` | `525d057742d2894b281f87b072e73b6f52e3c83140385d52268bc7d5baaf5263` |
| R | `61332a727c48b31705f21534e567293bd326391e9180e8a5d3aebccd5d29cd8a` | `a15689f123c9cb7e055a57135351cbd16d36f6c93adb1366869079f1d59e837e` | `525d057742d2894b281f87b072e73b6f52e3c83140385d52268bc7d5baaf5263` |

Build identities were:

| Image | Bytes | SHA-256 | Validation scope |
| --- | ---: | --- | --- |
| E generated-class benchmark | 559,024 | `e82d5b186e925203b6f744284d62dd6f736d05c703ef0cbd0c38ab39cedf44ce` | Build artifact |
| R generated-class benchmark | 557,440 | `d7540c8b42493ebe4749ec44eb9942bb10529da376f09b54548a60dbd27c2b6c` | Build artifact |
| E normal firmware | 1,192,000 | `3f75faf17ada5b6106ff718b819caab4b0ecfee615c60e3e95e255bbacd1b6a6` | Valid ESP32 image; later flashed and run |
| R normal firmware | 1,190,368 | `bc16f74e69ab0d99517fe51866eee0326666c52d3372ed488efe3e1f5e00fc05` | Valid ESP32 image; later flashed and run |

The retained host result JSON had SHA-256
`16c79d8e33815b940914083b437a6af5627c4d61445a8f029e1f6c088f6359e7`.

## Candidate Structures

E and R use the same two-section normalized lattice. Candidate E feeds each channel bank
with the shared `1-z^-2` input difference and applies held output normalization. Candidate R
feeds the public input directly to the same recurrence and exposes the raw terminal tap,
with no output-normalization reciprocal.

The generated structures were:

| Property | E | R |
| --- | ---: | ---: |
| Block modulus | 32 | 32 |
| Guarded decay `pow` calls | 2 | 2 |
| Mode `cos` calls per block | 18 | 18 |
| `sqrt` calls per block | 20 | 20 |
| Expensive calls in the sample loop | 0 | 0 |
| Paired lattice state updates | 36 | 36 |
| Differentiator states | 4 | 0 |
| Output reciprocals | 18 | 0 |
| Mute ASR states | 0 | 0 |
| Mute-controlled recurrence branches | 0 | 0 |

The 20 block-rate square roots are 18 mode-specific `c1` terms plus two channel-shared
`c2` terms. For both candidates mute is a direct visible-output gate: the recurrence is not
skipped while muted, and there is no historical ASR mute envelope.

## Generated Structure And Host Evidence

Both candidates generated as complete two-channel products with alternate tuning intact.
The exact generated counts above confirm block-rate coefficient preparation and sample-rate
lattice recurrence; they do not by themselves establish sound quality.

Under the original host gate, Candidate E exceeded the output bound in three isolated
scenarios and was labeled `rejected`; all `601` subsequent complete-product rows were
skipped. These three values are output-normalization magnitude labels, not evidence of state
divergence:

| Scenario | E normalized-output max abs | Energy bounded | Violations/checks | State max abs |
| --- | ---: | --- | ---: | ---: |
| boundary | `14.6763868` | `true` | `0/6141` | `0.000441193959` |
| crossing | `10.3700294` | `true` | `0/6141` | `0.000478422153` |
| decay-jump | `139.9256287` | `true` | `0/6141` | `0.002773977350` |

Candidate R completed `601` rows: `7` dynamic, `540` static, `30` boundary-static, and `24`
peak rows. Its historical status was `completed_with_failures`, with zero skipped rows. The
six failed peak labels were all at 16 Hz, indices `0/4/8`, on both channels. Each finite
render produced an FFT estimate of `16.074198 Hz`, or `+8.009839 cents`, beyond the old
`+/-5 cents` estimate gate. This is a finite-render FFT estimate label, not a conclusion
that the physical instrument sounded detuned.

## ESP32 Generated-Class Deadline

Each generated-class capture covered 100,000 blocks at 240 MHz:

| Candidate | Median | p95 | p99 | Max | Misses |
| --- | ---: | ---: | ---: | ---: | ---: |
| E | `386.6 us` | `394.5 us` | `395.8 us` | `396.246 us` | 0 |
| R | `356.1 us` | `368.4 us` | `370.6 us` | `371.113 us` | 0 |

Both captures reported finite output, varied left and right outputs, and zero hard deadline
misses. Both also passed the original generated-class timing gate. These runs establish the
generated audio class under the registered benchmark stimulus, not normal-firmware
listening or complete-product pressure.

## Normal Firmware, Pressure, And Listening

Both valid normal-firmware images were flashed and ran on hardware. The user's exact
Candidate R listening observation was:

> `听起来没问题`

The exact Candidate E observation was:

> `E 也非常好，而且音量明显比 R 要高。`

The reported wet/dry observation for the E/R auditions was:

> `左侧 wet、右侧 wet 和 dry/thru 都正常。`

These conversation-sourced statements are physical user observations for those exposures;
they are not machine evidence, calibrated loudness measurements, or a final E/R selection.

Only Candidate E has attributable retained pressure batches:

- left: 1,000 MIDI pairs at 1 ms;
- left: 10,000 MIDI pairs at 1 ms;
- right: 10,000 MIDI pairs at 1 ms, run twice.

No parser error, reset, stall, wet loss, or dry/thru problem was reported during those
retained E exposures. This is not a complete all-mode pressure matrix. A complete R pressure
matrix was not run or is not attributable in the retained evidence. Candidate-specific
mute/unmute physical evidence is also insufficient to label either candidate PASS, even
though the generated structure proves that mute does not skip recurrence.

## Complete-Product Task 8 Observation

Candidate E has two formal complete-product timing distributions, each over 100,000 blocks
at 240 MHz:

| Stimulus | Median | p95 | p99 | Max | Misses | Historical label |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| String | `712.4 us` | `717.4 us` | `719.5 us` | `725.400 us` | 0 | FAIL old p99 gate |
| Cave | `712.4 us` | `717.4 us` | `719.6 us` | `725.158 us` | 0 | FAIL old p99 gate |

The complete interval includes blocking I2S write behavior and is not a CPU-only DSP
measurement. Both distributions failed the old `p99 <= 580.5 us` gate, while recording zero
misses. Under the current sole hardware-run gate, that p99 label is non-eliminating rather
than rewritten as a pass.

The attempted E segmented observer allocated `5 x 8192 x uint32_t` in total for five
histograms, about `160 KiB`, and aborted during boot. Address resolution against
the matching ELF identified `operator new(unsigned int)` at `0x40161376`, followed by
`Wingie2::Wingie2(int, int)` at `0x400d8e4e`. This is an observer-allocation failure, not an
E DSP failure. It invalidates only the segmented observation; the two formal complete-only
distributions above remain valid evidence.

Candidate R complete-product Task 8 timing, including segmented timing, was never run: the
historical host gate skipped it, and it was not rerun after the rule change. It remains
`not measured`, not pass or fail.

## Recovery And Current Device State

After the E segmented-observer failure, the historical app0 recovery image and the full
`0x140000`-byte readback were byte-for-byte equal. Both were 1,310,720 bytes and had
SHA-256:

```text
6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b
```

That comparison proves the historical recovery state at that checkpoint. It is not the
current device state.

As of 2026-07-14, after that recovery checkpoint and later trials, the device runs Coupled Q
normal firmware. The retained Q normal-firmware image identity is:

```text
aa054f2b97f5a0461a718991889594c6feec5120dfff73435d0b9ff201a4faeb  1,191,248 bytes
```

The captured Q startup reported MIDI channels `1/2/3`, A3 `440.00`, left/right Poly/Poly,
and standard tuning. The user's exact Q listening observation was:

> `我听起来没问题。`

The Q image on the device is a later normal-firmware trial state. It does not change the
repository product source, which remains the historical direct form.

## Incomplete Or Reclassified Tests

- E's `601` complete-product host rows, including its complete parity coverage, remain
  skipped by the original isolated-output gate. Later hardware listening does not fill
  those rows.
- E's three isolated output-bound labels remain historical failures. The bounded state data
  reclassifies their interpretation, not their recorded gate result.
- R's six 16 Hz FFT peak labels remain historical failures under the old `+/-5 cents` gate.
  They are non-eliminating under the current rule and are not a physical listening verdict.
- E's two complete-product p99 labels remain failures under the old timing gate. They are
  non-eliminating under the current rule and are not rewritten as timing passes.
- E's five segmented phase histograms were not obtained because the observer allocation
  aborted during construction. No segmented timing result exists.
- R complete-product and segmented Task 8 timing were not run after the rule change.
- Only the four listed E pressure batches are attributable. A complete planned
  String/Poly/Bar/Cave/ratio-equivalent matrix is not established for E, and the full R
  pressure matrix was not run or is not attributable.
- Attributable E/R mute/unmute listening evidence is insufficient; no mute/unmute PASS is
  recorded for either candidate.
- No final user choice of E, R, or neither was recorded, and no exact-candidate installation
  rerun was performed.

Skipped and unmeasured stages are not inferred as passes or failures.

## Mainline And Experiment Boundary

No E or R product source was installed. On `main`, the repository product files remain the
historical direct-form triplet identified above. The main tree identity at the result-writing
start was:

```text
74f18a3c12fc6c4eee914ddf3975777d42753202  main
```

Normalized-lattice experiment code remains on `fix/custom-normalized-lattice` through:

```text
beaa6f691c8d1d40994ea376754428b23e0d40d2  fix/custom-normalized-lattice
```

The experiment branch is evidence and continued trial infrastructure, not installed product
source. No experiment commit was merged or pushed as part of this result, and the currently
flashed Q normal firmware must not be confused with the historical direct-form product in
the repository.
