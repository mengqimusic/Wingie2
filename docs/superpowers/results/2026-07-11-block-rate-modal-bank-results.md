# Wingie2 Block-Rate Modal Bank Feasibility Result

## Decision

`historical gate rejection / do not merge the experiment branch`.

The independent block-rate rotation candidate passed its mathematical contract, generated
structure, complete host matrix, and generated-class ESP32 deadline gate. It failed the
first complete-product ESP32 deadline gate: String p99 was `717.1 us`, above the fixed
`580.5 us` limit. The fixed gate rejects the candidate even though max remained just below
the block deadline and deadline misses were zero.

The first failed gate skipped the Cave deadline capture, diagnostic firmware, hardware
pressure, and user listening A/B in the original experiment. Task 9 and Task 10 were not
executed then.

## 2026-07-14 Follow-Up Trial

A normal, non-instrumented product image was later built: 1,189,504 bytes, SHA-256
`9f381eaa28592f8cd4636530d0203444c8fd4a52542844c1797fa92a68585f64`.
The image was flashed and the user confirmed that modal-bank works normally. This adds normal
build/flash/run evidence without changing the historical p99 result or creating an unrecorded
pressure matrix or qualitative sound judgment.

The old instrumented complete-product log below still records a shared `mode_changed` path
difference. The successful normal run shows that it did not prevent basic operation in this
exposure; it does not by itself prove that every mode-change control path was separately tested.

Because the first rejection gate is ESP32 deadline, a new custom normalized-lattice design
session is authorized by the candidate order. It must be a new design and plan; it must not
resume `fi.tf2np` or either rejected TF2NP/coupled implementation plan.

## Toolchain Identity

The experiment used:

- Faust `2.59.6`;
- ESP32 Arduino Core `2.0.4-cn` at 240 MHz;
- NumPy `2.4.2` and SciPy `1.17.1`;
- 44.1 kHz sample rate and 32-sample audio blocks.

Pinned Faust hashes:

```text
464d9ba249bea4b3589ad0ba5c7e8bf266b4f7e649523e68a022f73a164032c2  faust
6f82542b67778a5f46edc0fc29bf18b5b7d0337b43689f6f2dd07a08c6da45fa  filters.lib
4f7e5f369400f06c16343240db61bfec56f3f93c96f817250262cbc9c9499d71  oscillators.lib
a35260d0c5a00ebf81367107bf3f626619a9fd85995b596b06a045ec45c80e45  esp32.cpp
627c103f5d35aa331ca1e26638cee92f96ab08c511f1603921cd84e260d0f2e3  esp32.h
569c14e6cec03b5d8e51fec6ce5cbd990d685601f580b6a0c16c6429fad25921  esp32-dsp.h
```

Historical product baseline:

```text
fad94a49a0fe888aea854cdb3b02ab073a982909939db433431b609d1b6fbbd1  Wingie2.dsp
b9414abf86b17e42993479861ce81d3e70c56158351b13ab9d9ea1af195f6c39  Wingie2/Wingie2.cpp
525d057742d2894b281f87b072e73b6f52e3c83140385d52268bc7d5baaf5263  Wingie2/Wingie2.h
```

Git-external candidate batch:

```text
2ab02168ba7365a38973b633f05d2a30e0cac99479d1de28d0d72d56dd915b2f  Wingie2.dsp
bc3118e041742dc52837ef153469f4bb44e63e8ffff10d9c191595a5c9540ee8  Wingie2/Wingie2.cpp
525d057742d2894b281f87b072e73b6f52e3c83140385d52268bc7d5baaf5263  Wingie2/Wingie2.h
3cf04b0af2845d69138bd3492522e598fcb93622f4b26a087d5471c6bcb1e8a3  Wingie2.zip
```

The manifest reverified all seven tool/source/generated entries after the decision.

## Host And Structure Gates

The isolated five-scenario contract (`boundary`, `duplicates`, `crossing`, `reverse`, and
`ratio-jump`) passed across 8,192 samples. Every state norm was contractive after the input
impulse, every tail remained active, and every 32-sample slice held `rho/sin/cos` constant.

The generated complete product passed with:

```text
block modulus                 32
verified compute block size   32
guarded decay pow calls        2
compute-rate sin calls        18
compute-rate cos calls        18
rho-scaled state updates      36
sqrt/tf2np/nlf2 calls          0/0/0
```

Faust hoisted all 18 frequency `sin/cos` pairs into the `compute()` prologue. The checker
accepted this only after independently proving `fDSP->compute(fBufferSize, ...)` and the
product constructor's 32-sample buffer. The complete structure JSON SHA-256 was:

```text
86c37d7f5462461950c073f8aeb527a8fbc5c01098500c01b62d2c0bbf320fc3
```

All `642` host rows passed. The exhaustive row sets were:

| Row set | Cartesian coverage | Rows | Failures |
| --- | --- | ---: | ---: |
| Historical dynamics | 4 scenarios x 2 tunings x 3 artifacts x 2 channels | 48 | 0 |
| New dynamics | 2 scenarios x 2 tunings x candidate x 2 channels | 8 | 0 |
| Tracked/reference parity | 4 scenarios x 2 tunings x 2 channels | 16 | 0 |
| Static mode matrix | 3 modes x 2 tunings x 3 indices x 5 notes x 3 decays x 2 channels | 540 | 0 |
| Cave static matrix | 3 indices x 5 targets x 2 channels | 30 | 0 |

The six dynamic scenarios were `decay-step`, `mode-step`, `slow-notes`, `rapid-notes`,
`frequency-jump`, and `mute-state`. Static rows covered modes `0/1/2`, both tunings,
indices `0/4/8`, notes `24/36/60/84/96`, and Decay `0.1/5/10 s`. Cave targets were
`16/50/440/8000/16000 Hz`.

Maximum static pole error was `0.560759216 cents`, within `+/-5 cents`. All 24 fixed FFT
peak rows (4 targets x 3 indices x 2 channels) reported exactly one significant peak;
maximum peak-row pole error was `0.000202362 cents`. Maximum output across host rows was
`0.666655362`, below the numerical explosion bound `8.0`. Tracked/reference parity maxima
were `0.0000636429` absolute and `0.000000179795` RMS.

The complete row JSON contained `11,226` lines and had SHA-256:

```text
503c84eb733495fa6d74b4de3fa171d5beada60ecefc84d8118decb2fb0a965b
```

The Task 2 unit fixture was corrected before candidate results were observed: a synthetic
quiet second sinusoid tests only the pre-registered FFT peak-identity rule, while the pure
single-mode fixture tests pole error. The real candidate gate still required both tests.

## ESP32 Deadline

Both images had valid ESP32 checksum and validation hash metadata. Build identities:

| Image | Bytes | SHA-256 |
| --- | ---: | --- |
| Generated class | 556128 | `d6a528a652a86b90255130d0e6adfff14a147c3646848b1d01895fad17e92ccf` |
| Complete product | 1191056 | `45a5dfa5fe982101304687706f9480609dc786bd03c42bd0e859144f5f67bee1` |

The complete-product instrumentation identity was:

```text
8deab69c192e6717253f8e3871c95eb9a64aa5fcb80ee038743fbed5a9b44467
```

Its extracted and reinjected user sections both matched:

```text
e9d98acab12b2d041884443273b2b9d69e611f69c4aa94bded25f810642ffad5
```

Each distribution contained 100,000 blocks at 240 MHz:

| Path/stimulus | Median | p95 | p99 | Max | Misses | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Generated class, 18 Cave targets changed every block | `437.6 us` | `447.5 us` | `450.0 us` | `455.071 us` | 0 | PASS |
| Complete product, 40,000 rapid String pairs | `710.2 us` | `714.8 us` | `717.1 us` | `725.071 us` | 0 | **FAIL p99** |

The String sender completed 40,000 Note On and 40,000 Note Off messages in
`133.402580583 s`, covering the complete recorder window. The fixed limits were p99
`<= 580.5 us`, max `< 725.6 us`, and zero misses. Max passed by only `0.529 us`; this does
not override the p99 failure.

The complete product also logged twice:

```text
ERROR : setParamValue '/Wingie/left/mode_changed' not found
```

The candidate's shared mode-change envelope was compiler-coalesced to
`/Wingie/mode_changed`, while firmware still addressed the historical grouped path. This is
a separate product-contract regression to prevent in any future design. No control-side
compensation was added.

Evidence hashes:

```text
7fe5bafc869dad0588ab08ef661e9c1bcd6314e58135c90033c06ff9b02b629a  generated.txt
b137093649aad1fe61d5f1e0f0c6fa4c42e62fb4b3676c18e99f7f0be75bce48  generated.json
33e4b9475a33696bd79f01c96a5e9b77b0e876b194138ff14b47078314a713fa  product-string.txt
c7cc68ca12b3cc49688b68443de39ffe8b01b871e4ce80903f929f06762ff7c6  product-worst.json
```

## Hardware Recovery

Every candidate write targeted app0 only at `0x10000`:

1. Generated-class candidate: write and verify; measure; restore historical app0; verify
   all `0x140000` bytes; read back `0x140000` bytes; `cmp` and SHA-256 pass.
2. Complete-product String candidate: write and verify; measure; restore historical app0;
   verify all `0x140000` bytes; read back `0x140000` bytes; `cmp` and SHA-256 pass.

Historical app0 SHA-256 after both stages:

```text
6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b
```

Both restored startups reported MIDI channels `1/2/3`, A3 `440.00`, Poly/Poly, and
standard tuning. After the generated-class recovery, the user's exact confirmation was
`都正常·`. After the complete-product recovery, it was `正常`.

These confirmations apply only to the restored historical firmware. They are not candidate
audio or listening passes.

## Product And Git Boundary

- `Wingie2.dsp`, `Wingie2/Wingie2.cpp`, and `Wingie2/Wingie2.h` were never modified.
- Product audio remains the historical direct-form `pm.modeFilter` implementation.
- Cave deadline, diagnostic firmware, String/Poly/Bar/Cave pressure, and candidate A/B were
  skipped after the first complete-product machine failure.
- The rejected TF2NP and coupled plans were not restored or modified.
- Candidate source, generated products, firmware, and raw measurement data remained outside
  the repository; only test infrastructure and this conclusion are preserved on the
  experiment branch.
- No branch was merged or pushed automatically.
