---
name: verify
description: Build, flash, and drive Wingie2 hardware for runtime verification (MIDI/MPE, serial config, diagnostics snapshots)
---

# Wingie2 Hardware Verification

## Build

```bash
# normal / diagnostics (MIDI_DIAGNOSTICS=1 enables 'p'/'r' serial snapshot bytes)
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  [--build-property compiler.cpp.extra_flags=-DMIDI_DIAGNOSTICS=1] \
  --output-dir /tmp/wingie2-<tag>-build Wingie2
```

Core is pinned `esp32:esp32 2.0.4-cn` (already installed; 2.0.5 and 3.x incompatible — see AGENTS.md).

## Flash (routine: app0 only, no backup — AGENTS.md sanctions this; git is the recovery source)

```bash
ESPTOOL=$(find /Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py -name esptool -type f | head -1)
"$ESPTOOL" --chip esp32 --port /dev/cu.usbserial-11310 write_flash 0x10000 /tmp/wingie2-<tag>-build/Wingie2.ino.bin
"$ESPTOOL" --chip esp32 --port /dev/cu.usbserial-11310 verify_flash 0x10000 /tmp/wingie2-<tag>-build/Wingie2.ino.bin
```

esptool 3.3.0-cn uses underscore subcommands (`write_flash`, not `write-flash`). After testing a
diagnostics build, reflash the normal build to leave the device as found.

## Serial (observation surface)

- `/dev/cu.usbserial-11310`, 115200 8N1. **Opening/closing the port resets the ESP32** (auto-reset
  circuit; DTR/RTS transient at open) — hold the port open in ONE persistent process for a whole
  test session; never open per command. Boot takes ~3 s; MIDI is only serviced after
  `serial_config_ready` (end of control-task init), so MIDI sent during boot is lost.
- Config frames: `@{"v":1,"id":1,"op":"get_settings"}\n`; responses are `<{json}`.
  `set_param` requires `target` (`left`/`right`/`shared`) + `name` + `value`; settings changes are
  runtime-only (dirty) unless a `save` op is sent — don't save during verification.
- Diagnostics builds: byte `p` prints MIDI/MPE state snapshot (claimed mask, per-voice
  note/ch/active/member_pb/total_pb, manager_pb, mono state), `r` resets counters.
  `Tools/midi_diag_serial.py print|reset` is the one-shot helper.

## MIDI stimulus surface

- `Tools/midi_stress.swift` (compile once: `swiftc -O -o /tmp/midi_stress Tools/midi_stress.swift`)
  sends to CoreMIDI destination **`USB MIDI DevicePort 1`** (independent USB→DIN interface cabled to
  Wingie2 DIN MIDI In). **Never address Port 2.** Subcommands: `mpe-config <lo> <hi>`,
  `note-on <ch> <pitch> <bend>`, `note-off`, `bend`, `pb-range`, `mpe-note`, `batch`, `marker`.
- Expectation math: positive bend b → b/8191×range st; negative → b/8192×range st.
  Defaults: member ±48 st, manager ±2 st, conventional ±2 st.

## Flows worth driving (2026-07-17 reference run)

1. Baseline `p` → `claimed=0x0000` (mpe_enabled defaults false, unsaved).
2. `mpe-config 3 3` → `claimed=0xf00f`; note-on ch2/3/4 with pre-bend → voices 0/1/2 with
   member_pb; manager ch1 bend → all totals shift; note-off → member_pb latches.
3. `pb-range 2 12` → active voices rescale retroactively (member range is per-zone).
4. Conventional coexistence: `set_param midi_left=5`, ch5 note steals rotating voice; ch5 PB must
   NOT appear in MPE-owned voice totals.
5. Probes: MCM resize (`1 1`→`0xc003`, `0 0`→0), overlap (`15 0`→`0xffff`, then `3 3`→`0xf00f`),
   MCM on non-manager channel = no-op, 4th note replaces oldest voice, stale note-off = no-op.
6. String mode via `set_param target=left name=mode value=1`: mono owner latch; member PB stops at
   note-off, manager PB keeps applying.

## Gotchas

- `mpe_enabled` serial toggle only calls `configure_mpe_power_on` on a real 0↔1 transition;
  setting the already-current value is a no-op (MCM-built zones survive it).
- While zones are claimed, ch14/15/16 conventional CC surfaces (cave freq, ch16 global settings)
  are shadowed by the MPE path — by design, but verify before assuming those CCs reach the device.
- `printMidiDiagnostics` reads back `poly_note_*` DSP params but not `poly_pitch_ratio_*`; the
  ratio write path is exercised implicitly via `set_poly_voice_dsp`.
