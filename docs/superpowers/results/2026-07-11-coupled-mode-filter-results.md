# Wingie2 Coupled Mode Filter Feasibility Result

## Decision

`candidate rejected / do not merge the coupled experiment branch`.

Candidate Q and Candidate D both passed host, generated-structure, and generated-class
ESP32 gates, but both failed the first end-to-end product deadline gate. In the original
experiment, neither candidate entered product source, normal or diagnostic candidate firmware,
MIDI pressure, or listening A/B.

The complete experiment history is preserved at branch and tag:

```text
archive/coupled-mode-filter
archive/coupled-mode-filter-2026-07-11
```

Both references end at experiment commit `7437a63`. This summary is the durable `main`
record; the archived branch is evidence, not an implementation source to merge wholesale.

## Candidate Outcome

Both candidates used the pinned Faust `2.59.6` toolchain and generated with alternate tuning
intact. Their machine gates established:

- isolated Q and D renders remained finite and contractive across 8,192 samples;
- each full-product candidate passed all `556` host cases;
- maximum static pitch error was `0.162502 cents` for Q and `2.007266 cents` for D, within
  the fixed `+/-5 cents` gate;
- each generated product contained two guarded decay preparations, 36 rho-scaled state
  updates, and zero `tf2np`, `nlf2`, or `sqrt` calls;
- generated-class ESP32 p99 was `544.7 us` for Q and `544.4 us` for D, within the fixed
  `580.5 us` p99 gate.

Both candidates failed only after integration into the complete ESP32 product path:

| Candidate | Product median/p95/p99/max | Deadline misses | Result |
| --- | --- | ---: | --- |
| Q | `708.0/712.4/714.6/723.625 us` | 0 | FAIL p99 |
| D | `707.8/712.4/715.2/722.804 us` | 0 | FAIL p99 |

Each distribution covered 100,000 blocks at 240 MHz, 44.1 kHz, and 32 samples per block.
The maxima remained below the hard block deadline and misses remained zero, but those facts
do not override the fixed p99 rejection gate.

No physical listening PASS existed for either coupled candidate in that experiment. The failed
machine gate skipped candidate firmware, String/Poly/Bar pressure, and user A/B by design.

## 2026-07-14 Follow-Up Trial

Candidate D was later built as a normal, non-instrumented product image: 1,191,216 bytes,
SHA-256 `69a04121e3fb6f69f0a4e8b0072ad14b01332fe4d50a541cd1fcda82dfbf3e05`.
The image was flashed and the user confirmed that coupled-d works normally. This adds normal
build/flash/run evidence; it does not retroactively change the old p99 decision or establish a
complete pressure matrix, separately itemized wet/dry paths, mute behavior, or a qualitative
sound preference.

## Historical Recovery

Every benchmark write targeted app0 only at `0x10000`. The historical app0 was restored,
fully verified, and compared against a complete `0x140000`-byte readback. Its SHA-256 was:

```text
6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b
```

Startup reported MIDI channels `1/2/3`, A3 `440.00`, Poly/Poly, and standard tuning. The
user explicitly confirmed that both restored wet paths were normal.

## Mainline Boundary

- No candidate modified `Wingie2.dsp`, `Wingie2/Wingie2.cpp`, or `Wingie2/Wingie2.h`.
- Product audio remains the historical direct-form `pm.modeFilter` implementation.
- Coupled plans, candidate-specific tests, benchmark infrastructure, generated candidates,
  and experiment commits remain only in the archive.
- The rejected TF2NP plan was not resumed or modified.
- Future mode-filter work requires a new design and plan; it must not resume either rejected
  implementation plan.
- Fixed constraints remain Faust `2.59.6`, ESP32 Arduino Core `2.0.4`, no control-side
  compensation, and physical listening PASS only after explicit user confirmation.
