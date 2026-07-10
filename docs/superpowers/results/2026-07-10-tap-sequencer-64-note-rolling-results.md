# Tap Sequencer 64-Note Rolling Results

## Outcome

The 64-note rolling Tap Sequencer candidate passed the host regression test,
normal and diagnostic ESP32 builds, and the user-confirmed Wingie2 smoke
matrix. The device was restored to the verified pre-test baseline app0 after
candidate validation.

## Host Regression Test

The test was first compiled before `Wingie2/tap_sequence.h` existed and failed
with the expected missing-header error. After the bounded state object was
added, the exact verification command was:

```bash
clang++ -std=c++11 -Wall -Wextra -pedantic \
  tests/host/tap_sequence_test.cpp -o /tmp/tap_sequence_test
/tmp/tap_sequence_test
```

Both commands exited 0 with no warnings or test output. The final source scan
also returned no matches:

```bash
rg -n '\b(seq|seqLen|writeHeadPos|playHeadPos)\b' Wingie2
```

## Firmware Builds

Board details reported ESP32 Arduino Core `2.0.4-cn`.

| Build | Program report | Binary size | RAM report | SHA-256 |
|---|---:|---:|---:|---|
| Normal | 1,183,977 / 1,310,720 bytes | 1,189,744 bytes | 50,692 / 327,680 bytes | `7d02277ac41c6f6c7825c53d9a2e118c92c4590c4a191c668f96c23c62f09d3b` |
| `MIDI_DIAGNOSTICS=1` | 1,185,601 / 1,310,720 bytes | 1,191,376 bytes | 50,740 / 327,680 bytes | `e74ac44742c2609a065cc1b999355ea42fd950db21c94a1990169769af4bb44b` |

Commands:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --output-dir /tmp/wingie2-tap-normal-build Wingie2
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --build-property compiler.cpp.extra_flags=-DMIDI_DIAGNOSTICS=1 \
  --output-dir /tmp/wingie2-tap-diag-build Wingie2
```

## app0 Evidence

Only app0 at `0x10000` was written. No bootloader, partition table, otadata, or
NVS offset was written.

The stub-based baseline read returned variable short SLIP frames (`0xff0` and
`0xffe` instead of `0x1000`). A 64 KiB ROM bootloader read matched the previous
known-good baseline byte-for-byte, so the complete baseline was captured with
the ROM 64-byte read path:

```bash
/tmp/wingie2-esptool-venv/bin/esptool.py --chip esp32 \
  --port /dev/cu.usbserial-11310 --no-stub read_flash --no-progress \
  0x10000 0x140000 /tmp/wingie2-before-tap-fix-app0.bin
```

The complete read was 1,310,720 bytes, passed ESP32 image checksum and
validation-hash checks, and matched the previous baseline byte-for-byte.

| Image | Size | SHA-256 |
|---|---:|---|
| Baseline app0 | 1,310,720 bytes | `6da9aca6795cc6ef9352705089c5c8aef58a91dc51d37c73592052ef2fa8c36b` |
| Tap candidate | 1,189,744 bytes | `7d02277ac41c6f6c7825c53d9a2e118c92c4590c4a191c668f96c23c62f09d3b` |

Candidate write and independent `verify_flash` both succeeded at `0x10000`;
the verify reported `digest matched`. After the physical matrix, the full
baseline app0 was written back at `0x10000`. Its independent verification was
repeated after all testing and reported:

```text
Verifying 0x140000 (1310720) bytes @ 0x00010000 ...
-- verify OK (digest matched)
```

## Physical Smoke Matrix

The user explicitly confirmed: `左右 String/Bar 全部通过，dry/thru 和双 wet 正常`.
Each side used the same sequence of more than 12 panel notes and at least two
complete trigger cycles.

| Side | Mode | Pitch set and order | Other side | Pulse / reset | Audio paths |
|---|---|---|---|---|---|
| Left | String | PASS, user confirmed | Unchanged | No extreme pulse, out-of-range pitch, or reset | PASS |
| Right | String | PASS, user confirmed | Unchanged | No extreme pulse, out-of-range pitch, or reset | PASS |
| Left | Bar | PASS, user confirmed | Unchanged | No extreme pulse, out-of-range pitch, or reset | PASS |
| Right | Bar | PASS, user confirmed | Unchanged | No extreme pulse, out-of-range pitch, or reset | PASS |

After baseline restoration, startup reported MIDI channels `1/2/3`, A3
`440.00`, left/right mode `0/0` (Poly), and standard tuning. The user then
explicitly confirmed that both restored wet paths were audible.

## Commits

- `5b10511 fix: 添加有界 TapSequence 状态`
- `6844664 fix: 接入 64 音 Tap Sequencer`
