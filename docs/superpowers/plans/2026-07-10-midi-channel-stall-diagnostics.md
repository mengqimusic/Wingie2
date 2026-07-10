# Wingie2 MIDI 单通道停止响应诊断 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在真实 Wingie2 上复现单一 MIDI 通道接收大量 Ableton 风格 Note On/Note Off 后停止响应的问题，并用串口计数确定故障边界。

**Architecture:** 增加默认关闭、由 `MIDI_DIAGNOSTICS=1` 编译宏启用的固定大小计数器；测试期间不逐事件打印，只通过 USB debug serial 的 `r`/`p` 命令重置和读取快照。CoreMIDI 压力工具生成可重复批次，诊断 app 只写当前 app0 partition，原 flash 和 app0 先只读备份。

**Tech Stack:** Arduino/ESP32 Core 2.0.4、Arduino MIDI Library、CoreMIDI/Swift、pyserial、esptool、Arduino CLI。

---

## 文件结构

- Create: `Tools/midi_stress.swift`：向指定 CoreMIDI destination 发送确定数量的 Note On/Note Off 对或 marker note。
- Create: `Tools/midi_diag_serial.py`：通过 115200 debug serial 发送 `r`/`p` 并读取完整响应。
- Create: `Wingie2/midi_diagnostics.ino`：固定大小计数、错误回调、RX 水位和串口快照实现。
- Modify: `Wingie2/Wingie2.ino`：注册诊断回调，并在 `loop()` 中包裹原有 `MIDI.read()`。
- Modify: `Wingie2/MIDI.ino`：在现有 Note On 路径记录计数，增加只计数的 Note Off 回调。
- Create after test: `docs/superpowers/results/2026-07-10-midi-channel-stall-results.md`：批次数据和根因边界。

### Task 1: 固定工具链并备份当前设备

**Files:**
- Read only: `/dev/cu.usbserial-11310`
- Create outside repository: `/tmp/wingie2-before-midi-debug-full.bin`
- Create outside repository: `/tmp/wingie2-before-midi-debug-partitions.bin`
- Create outside repository: `/tmp/wingie2-before-midi-debug-app0.bin`

- [ ] **Step 1: 读取 flash ID，确认容量为 4 MiB**

Run:

```bash
/Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0-cn/esptool \
  --chip esp32 --port /dev/cu.usbserial-11310 flash-id
```

Expected: ESP32 可连接，Detected flash size 为 4MB。若不是 4MB，停止，不执行固定长度读取。

- [ ] **Step 2: 读取完整 flash、partition table 和 app0**

Run:

```bash
/Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0-cn/esptool \
  --chip esp32 --port /dev/cu.usbserial-11310 read-flash \
  0x0 0x400000 /tmp/wingie2-before-midi-debug-full.bin
/Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0-cn/esptool \
  --chip esp32 --port /dev/cu.usbserial-11310 read-flash \
  0x8000 0x1000 /tmp/wingie2-before-midi-debug-partitions.bin
python3 /Users/mengwu/Library/Arduino15/packages/esp32/hardware/esp32/3.3.7-cn/tools/gen_esp32part.py \
  /tmp/wingie2-before-midi-debug-partitions.bin
/Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0-cn/esptool \
  --chip esp32 --port /dev/cu.usbserial-11310 read-flash \
  0x10000 0x140000 /tmp/wingie2-before-midi-debug-app0.bin
/Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0-cn/esptool \
  image-info /tmp/wingie2-before-midi-debug-app0.bin
shasum -a 256 /tmp/wingie2-before-midi-debug-*.bin
```

Expected: partition output contains `app0, app, ota_0, 0x10000, 0x140000`；app0 通过 ESP32 application image 检查；all three files have non-empty SHA-256 hashes。若 app0 不是有效 application image，停止，不写 `0x10000`。

- [ ] **Step 3: 安装并确认项目规定的 Core 2.0.4**

Run:

```bash
arduino-cli core install esp32:esp32@2.0.4 \
  --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli lib install "MIDI Library" "Adafruit AW9523"
arduino-cli core list
```

Expected: `esp32:esp32 2.0.4`；MIDI Library 和 Adafruit AW9523 已安装。

### Task 2: 创建可重复的 MIDI 和串口测试工具

**Files:**
- Create: `Tools/midi_stress.swift`
- Create: `Tools/midi_diag_serial.py`

- [ ] **Step 1: 先建立工具调用的失败基线**

Run:

```bash
swift Tools/midi_stress.swift --help
python3 Tools/midi_diag_serial.py --help
```

Expected: FAIL，因为两个工具尚不存在。

- [ ] **Step 2: 创建 CoreMIDI 压力工具**

Create `Tools/midi_stress.swift`:

```swift
import CoreMIDI
import Foundation

private let destinationName = "USB MIDI DevicePort 1"

private func endpointName(_ endpoint: MIDIEndpointRef) -> String {
    var value: Unmanaged<CFString>?
    guard MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &value) == noErr,
          let name = value?.takeRetainedValue() else { return "" }
    return name as String
}

private func destination(named name: String) -> MIDIEndpointRef {
    for index in 0..<MIDIGetNumberOfDestinations() {
        let endpoint = MIDIGetDestination(index)
        if endpointName(endpoint) == name { return endpoint }
    }
    return 0
}

private func usage() -> Never {
    FileHandle.standardError.write(Data("usage: midi_stress.swift batch <channel> <pairs> <interval-us> | marker <channel> <pitch>\n".utf8))
    exit(2)
}

let arguments = Array(CommandLine.arguments.dropFirst())
if arguments == ["--help"] || arguments == ["-h"] { usage() }
guard arguments.count == 4 || arguments.count == 3 else { usage() }

let mode = arguments[0]
guard let channel = Int(arguments[1]), (1...16).contains(channel) else { usage() }
let endpoint = destination(named: destinationName)
guard endpoint != 0 else { fatalError("CoreMIDI destination not found: \(destinationName)") }

var client = MIDIClientRef()
var output = MIDIPortRef()
precondition(MIDIClientCreate("Wingie2 MIDI stress" as CFString, nil, nil, &client) == noErr)
precondition(MIDIOutputPortCreate(client, "Wingie2 MIDI stress out" as CFString, &output) == noErr)
defer {
    MIDIPortDispose(output)
    MIDIClientDispose(client)
}

func send(_ bytes: [UInt8]) {
    var list = MIDIPacketList()
    let packet = MIDIPacketListInit(&list)
    bytes.withUnsafeBufferPointer { buffer in
        _ = MIDIPacketListAdd(&list, MemoryLayout<MIDIPacketList>.size, packet, 0,
                              buffer.count, buffer.baseAddress!)
    }
    precondition(MIDISend(output, endpoint, &list) == noErr)
}

let noteOn = UInt8(0x90 | (channel - 1))
let noteOff = UInt8(0x80 | (channel - 1))
let started = ContinuousClock.now

switch mode {
case "batch":
    guard arguments.count == 4,
          let pairs = Int(arguments[2]), pairs > 0,
          let interval = useconds_t(arguments[3]) else { usage() }
    for index in 0..<pairs {
        let pitch = UInt8(36 + (index % 60))
        send([noteOn, pitch, 100])
        usleep(interval)
        send([noteOff, pitch, 0])
        usleep(interval)
    }
    print("sent channel=\(channel) note_on=\(pairs) note_off=\(pairs) elapsed=\(started.duration(to: .now))")
case "marker":
    guard arguments.count == 3, let pitchValue = Int(arguments[2]),
          (0...127).contains(pitchValue) else { usage() }
    let pitch = UInt8(pitchValue)
    send([noteOn, pitch, 100])
    usleep(100_000)
    send([noteOff, pitch, 0])
    print("marker channel=\(channel) pitch=\(pitch)")
default:
    usage()
}
```

- [ ] **Step 3: 创建串口命令工具**

Create `Tools/midi_diag_serial.py`:

```python
#!/usr/bin/env python3
import argparse
import time

import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("reset", "print"))
    parser.add_argument("--port", default="/dev/cu.usbserial-11310")
    args = parser.parse_args()

    connection = serial.Serial()
    connection.port = args.port
    connection.baudrate = 115200
    connection.timeout = 0.05
    connection.dtr = False
    connection.rts = False
    connection.open()
    try:
        connection.reset_input_buffer()
        connection.write(b"r" if args.command == "reset" else b"p")
        deadline = time.monotonic() + 2.0
        quiet_deadline = time.monotonic() + 0.5
        output = bytearray()
        while time.monotonic() < deadline and time.monotonic() < quiet_deadline:
            chunk = connection.read(4096)
            if chunk:
                output.extend(chunk)
                quiet_deadline = time.monotonic() + 0.5
        print(output.decode("utf-8", errors="replace"), end="")
    finally:
        connection.close()


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: 验证工具语法和参数保护**

Run:

```bash
swiftc -typecheck Tools/midi_stress.swift
swift Tools/midi_stress.swift --help 2>&1 | rg "usage:"
python3 -m py_compile Tools/midi_diag_serial.py
python3 Tools/midi_diag_serial.py --help
```

Expected: Swift typecheck 和 Python compile 成功；两个 help 命令显示明确用法。

- [ ] **Step 5: 提交测试工具**

```bash
git add Tools/midi_stress.swift Tools/midi_diag_serial.py
git commit -m "test: 添加 Wingie2 MIDI 压力工具"
```

### Task 3: 增加默认关闭的固件诊断

**Files:**
- Create: `Wingie2/midi_diagnostics.ino`
- Modify: `Wingie2/Wingie2.ino:75-80, 221-255`
- Modify: `Wingie2/MIDI.ino:1-19`

- [ ] **Step 1: 验证当前固件不响应诊断命令**

Run:

```bash
python3 Tools/midi_diag_serial.py print
```

Expected: 没有 `MIDI_DIAG` 输出，证明诊断能力在原固件中不存在。

- [ ] **Step 2: 在主 sketch 中接入编译开关**

Add after `MySettings` in `Wingie2/Wingie2.ino`:

```cpp
#ifndef MIDI_DIAGNOSTICS
#define MIDI_DIAGNOSTICS 0
#endif
```

Add after the existing MIDI callback registration in `setup()`:

```cpp
#if MIDI_DIAGNOSTICS
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleError(handleMidiError);
  resetMidiDiagnostics();
#endif
```

Replace the single `MIDI.read();` in `loop()` with:

```cpp
#if MIDI_DIAGNOSTICS
  serviceMidiDiagnostics();
#else
  MIDI.read();
#endif
```

- [ ] **Step 3: 记录 Note On/Note Off 回调边界**

At the start of `handleNoteOn()` in `Wingie2/MIDI.ino` add:

```cpp
#if MIDI_DIAGNOSTICS
  recordMidiNoteOn(channel, pitch, velocity);
#endif
```

Add after `handleNoteOn()`:

```cpp
#if MIDI_DIAGNOSTICS
void handleNoteOff(byte channel, byte pitch, byte velocity) {
  recordMidiNoteOff(channel, pitch, velocity);
}
#endif
```

- [ ] **Step 4: 实现固定大小诊断状态和串口快照**

Create `Wingie2/midi_diagnostics.ino`:

```cpp
#if MIDI_DIAGNOSTICS
struct MidiDiagnosticChannel {
  uint32_t noteOnCount;
  uint32_t noteOffCount;
  byte lastPitch;
  byte lastVelocity;
};

struct MidiDiagnosticState {
  MidiDiagnosticChannel channel[2];
  uint32_t readCalls;
  uint32_t parsedMessages;
  uint32_t parseErrors;
  byte lastError;
  int maxRxAvailable;
  unsigned long startedAt;
};

MidiDiagnosticState midiDiagnostic;

int diagnosticChannelIndex(byte channel) {
  if (channel == 1) return 0;
  if (channel == 2) return 1;
  return -1;
}

void resetMidiDiagnostics() {
  memset(&midiDiagnostic, 0, sizeof(midiDiagnostic));
  midiDiagnostic.startedAt = millis();
}

void recordMidiNoteOn(byte channel, byte pitch, byte velocity) {
  int index = diagnosticChannelIndex(channel);
  if (index < 0) return;
  midiDiagnostic.channel[index].noteOnCount++;
  midiDiagnostic.channel[index].lastPitch = pitch;
  midiDiagnostic.channel[index].lastVelocity = velocity;
}

void recordMidiNoteOff(byte channel, byte pitch, byte velocity) {
  int index = diagnosticChannelIndex(channel);
  if (index < 0) return;
  midiDiagnostic.channel[index].noteOffCount++;
  midiDiagnostic.channel[index].lastPitch = pitch;
  midiDiagnostic.channel[index].lastVelocity = velocity;
}

void handleMidiError(int8_t errorCode) {
  midiDiagnostic.parseErrors++;
  midiDiagnostic.lastError = byte(errorCode);
}

void printMidiDiagnostics() {
  Serial.printf("MIDI_DIAG elapsed_ms=%lu read_calls=%lu parsed=%lu errors=%lu last_error=%u rx_high_water=%d\n",
                millis() - midiDiagnostic.startedAt,
                (unsigned long)midiDiagnostic.readCalls,
                (unsigned long)midiDiagnostic.parsedMessages,
                (unsigned long)midiDiagnostic.parseErrors,
                midiDiagnostic.lastError,
                midiDiagnostic.maxRxAvailable);
  for (int ch = 0; ch < 2; ch++) {
    Serial.printf("MIDI_DIAG ch=%d on=%lu off=%lu last_pitch=%u last_velocity=%u mode=%d current_poly=%d\n",
                  ch + 1,
                  (unsigned long)midiDiagnostic.channel[ch].noteOnCount,
                  (unsigned long)midiDiagnostic.channel[ch].noteOffCount,
                  midiDiagnostic.channel[ch].lastPitch,
                  midiDiagnostic.channel[ch].lastVelocity,
                  Mode[ch], currentPoly[ch]);
  }
  Serial.printf("MIDI_DIAG left_note=%.1f left_poly=%.1f,%.1f,%.1f\n",
                dsp.getParamValue("note0"),
                dsp.getParamValue("/Wingie/left/poly_note_0"),
                dsp.getParamValue("/Wingie/left/poly_note_1"),
                dsp.getParamValue("/Wingie/left/poly_note_2"));
  Serial.printf("MIDI_DIAG right_note=%.1f right_poly=%.1f,%.1f,%.1f\n",
                dsp.getParamValue("note1"),
                dsp.getParamValue("/Wingie/right/poly_note_0"),
                dsp.getParamValue("/Wingie/right/poly_note_1"),
                dsp.getParamValue("/Wingie/right/poly_note_2"));
}

void serviceMidiDiagnostics() {
  int available = Serial2.available();
  if (available > midiDiagnostic.maxRxAvailable) {
    midiDiagnostic.maxRxAvailable = available;
  }
  midiDiagnostic.readCalls++;
  if (MIDI.read()) midiDiagnostic.parsedMessages++;

  while (Serial.available()) {
    switch (Serial.read()) {
      case 'r':
        resetMidiDiagnostics();
        Serial.println("MIDI_DIAG reset");
        break;
      case 'p':
        printMidiDiagnostics();
        break;
    }
  }
}
#endif
```

- [ ] **Step 5: 验证正常构建与诊断构建**

Run:

```bash
rm -rf /tmp/wingie2-normal-build /tmp/wingie2-midi-diag-build
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --output-dir /tmp/wingie2-normal-build Wingie2
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries \
  --build-property compiler.cpp.extra_flags=-DMIDI_DIAGNOSTICS=1 \
  --output-dir /tmp/wingie2-midi-diag-build Wingie2
```

Expected: 两个构建均成功；诊断构建生成 `/tmp/wingie2-midi-diag-build/Wingie2.ino.bin`，大小小于 app0 的 `0x140000`。

- [ ] **Step 6: 提交独立诊断代码**

```bash
git add Wingie2/Wingie2.ino Wingie2/MIDI.ino Wingie2/midi_diagnostics.ino
git commit -m "test: 添加可选 MIDI 串口诊断"
```

### Task 4: 只写 app0 并验证诊断固件

**Files:**
- Read: `/tmp/wingie2-midi-diag-build/Wingie2.ino.bin`
- Write hardware app0 only: flash offset `0x10000`

- [ ] **Step 1: 验证诊断 image 并写入 app0**

Run:

```bash
/Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0-cn/esptool \
  image-info /tmp/wingie2-midi-diag-build/Wingie2.ino.bin
/Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0-cn/esptool \
  --chip esp32 --port /dev/cu.usbserial-11310 write-flash \
  0x10000 /tmp/wingie2-midi-diag-build/Wingie2.ino.bin
/Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0-cn/esptool \
  --chip esp32 --port /dev/cu.usbserial-11310 verify-flash \
  0x10000 /tmp/wingie2-midi-diag-build/Wingie2.ino.bin
```

Expected: image 类型为 ESP32 app；write 和 verify 均成功，未写入其他 offset。

- [ ] **Step 2: 验证启动与 `r`/`p` 命令**

Run:

```bash
python3 Tools/midi_diag_serial.py reset
python3 Tools/midi_diag_serial.py print
```

Expected: reset 输出 `MIDI_DIAG reset`；print 输出全局行、ch=1、ch=2 和左右 DSP 参数行，所有事件计数为 0。

### Task 5: 执行逐变量压力矩阵

**Files:**
- Create: `docs/superpowers/results/2026-07-10-midi-channel-stall-results.md`

- [ ] **Step 1: channel 1 低速基线**

Run:

```bash
python3 Tools/midi_diag_serial.py reset
swift Tools/midi_stress.swift batch 1 1000 5000
python3 Tools/midi_diag_serial.py print
swift Tools/midi_stress.swift marker 1 60
swift Tools/midi_stress.swift marker 2 67
python3 Tools/midi_diag_serial.py print
```

Expected before markers: ch1 on=1000、off=1000、parsed=2000、errors=0；ch2 on=0、off=0。记录听感上的左右 marker 响应。

- [ ] **Step 2: channel 1 接近满线速批次**

Define one batch command, then invoke one concrete size at a time:

```bash
run_ch1_batch() {
  python3 Tools/midi_diag_serial.py reset
  swift Tools/midi_stress.swift batch 1 "$1" 1000
  python3 Tools/midi_diag_serial.py print
  swift Tools/midi_stress.swift marker 1 60
  swift Tools/midi_stress.swift marker 2 67
  python3 Tools/midi_diag_serial.py print
}
run_ch1_batch 1000
```

After inspecting the 1,000-pair snapshot and marker response, run:

```bash
run_ch1_batch 10000
```

After inspecting the 10,000-pair snapshot and marker response, run:

```bash
run_ch1_batch 100000
```

Expected: each pre-marker snapshot has ch1 on/off equal to the concrete pair count and parsed equal to twice that count. Stop at the first batch where target audio stops or counts diverge; do not run the next command on an unexplained failure.

- [ ] **Step 3: channel 2 执行完整矩阵**

Run the low-speed baseline:

```bash
python3 Tools/midi_diag_serial.py reset
swift Tools/midi_stress.swift batch 2 1000 5000
python3 Tools/midi_diag_serial.py print
swift Tools/midi_stress.swift marker 2 60
swift Tools/midi_stress.swift marker 1 67
python3 Tools/midi_diag_serial.py print
```

Expected before markers: ch2 on=1000、off=1000、parsed=2000、errors=0；ch1 on=0、off=0。

Define the near-line-rate batch and invoke one size at a time:

```bash
run_ch2_batch() {
  python3 Tools/midi_diag_serial.py reset
  swift Tools/midi_stress.swift batch 2 "$1" 1000
  python3 Tools/midi_diag_serial.py print
  swift Tools/midi_stress.swift marker 2 60
  swift Tools/midi_stress.swift marker 1 67
  python3 Tools/midi_diag_serial.py print
}
run_ch2_batch 1000
```

Then, only after inspecting the 1,000-pair result:

```bash
run_ch2_batch 10000
```

Only after inspecting the 10,000-pair result:

```bash
run_ch2_batch 100000
```

Expected: each pre-marker snapshot has ch2 on/off equal to the concrete pair count and parsed equal to twice that count; channel 1 remains the control.

- [ ] **Step 4: 必要时用 Ableton clip 复测**

If CoreMIDI batches do not reproduce, reset counters, play one Ableton clip through `USB MIDI DevicePort 1`, stop playback, then print counters.

Expected: the serial snapshot remains comparable to generated batches because no per-message logging occurs.

- [ ] **Step 5: 写入结果并分类边界**

Create `docs/superpowers/results/2026-07-10-midi-channel-stall-results.md` with one row per batch:

```markdown
| Source | Channel | Pairs | Interval us | Sent on/off | Parsed | Callback on/off | Errors | RX high water | Target marker | Control marker | Result |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|
```

Below the table, select exactly one current boundary conclusion from the design's 判定 section and cite its evidence. If evidence is incomplete, write `尚未定位到单一边界` and list the next single-variable test.

- [ ] **Step 6: 提交实机结果**

```bash
git add docs/superpowers/results/2026-07-10-midi-channel-stall-results.md
git commit -m "test: 记录 MIDI 单通道压力结果"
```

### Task 6: 恢复原 app0 并确认设备状态

**Files:**
- Read: `/tmp/wingie2-before-midi-debug-app0.bin`
- Write hardware app0 only: flash offset `0x10000`

- [ ] **Step 1: 写回并校验原 app0**

Run:

```bash
/Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0-cn/esptool \
  --chip esp32 --port /dev/cu.usbserial-11310 write-flash \
  0x10000 /tmp/wingie2-before-midi-debug-app0.bin
/Users/mengwu/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0-cn/esptool \
  --chip esp32 --port /dev/cu.usbserial-11310 verify-flash \
  0x10000 /tmp/wingie2-before-midi-debug-app0.bin
```

Expected: write 和 verify 成功；不写 bootloader、partition table 或 NVS。

- [ ] **Step 2: 捕获启动日志并核对持久状态**

Run:

```bash
python3 - <<'PY'
import serial
import time

connection = serial.Serial()
connection.port = "/dev/cu.usbserial-11310"
connection.baudrate = 115200
connection.timeout = 0.1
connection.dtr = False
connection.rts = True
connection.open()
try:
    time.sleep(0.15)
    connection.rts = False
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        chunk = connection.read(4096)
        if chunk:
            print(chunk.decode("utf-8", errors="replace"), end="")
finally:
    connection.close()
PY
python3 Tools/midi_diag_serial.py print
```

Expected: 启动日志恢复原固件格式；第二条命令没有 `MIDI_DIAG` 响应；`midi_ch_l=1`、`midi_ch_r=2`、`midi_ch_both=3`，A3、mode 和 tuning 与备份前日志一致。

- [ ] **Step 3: 最终仓库检查**

Run:

```bash
git status --short
git log -5 --oneline
```

Expected: 工作树干净；工具、可选诊断和结果分别位于独立提交。
