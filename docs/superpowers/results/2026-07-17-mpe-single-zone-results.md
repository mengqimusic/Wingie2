# MPE 单 Zone 逐音交替 —— 验证结果

日期：2026-07-17
计划：[`docs/superpowers/plans/2026-07-17-mpe-single-zone-alternating.md`](../plans/2026-07-17-mpe-single-zone-alternating.md)
验证范围：Task 6（全门禁 + 真机验证）。Task 1–5 的实现与提交（至 HEAD `aff281f`）为本结果的前置。

## 实现边界

- MPE 常开，无开关：启动即建单 Lower Zone（Manager Ch1 + Member Ch2–7，`claimed=0x007f`），`mpe_enabled` 偏好、set_param 分支、get_settings 字段、网页开关全部移除，hello `config_schema` 升为 4。
- Note On 由固件经 `mpeFlip` 在左右引擎间逐音交替分配；Cave 侧跳过（不消耗交替），两侧均 Cave 则吞掉；Note Off 按 `(channel,note)` ownership 跨两侧释放。
- Manager Ch1 全局语义：PB 作用两侧全部 MPE voice，CC 两侧同效；Member PB 逐音（默认 ±48，Manager ±2）。
- MCM（RPN 6）仅 Ch1 生效，可运行时调整/撤销 Zone；Ch16 Upper MCM 消费但忽略；重启恢复启动布局。
- Ch8–16 保留常规 Left/Right/Both 路由，出厂默认 8/9/10；已存 NVS 不做固件迁移（旧设备保存的 1/2/3 落在 Zone 内将失效，需手动改 8+，见 MPE.md 迁移说明）。
- 本计划无 DSP 变更（`Wingie2.dsp` 与 `Wingie2.cpp` 未改）。

## 自动验证结果（Task 6 Step 1 门禁）

| 门禁 | 结果 |
|---|---|
| host 测试 ×4（g++ -std=c++11 -Wall） | MPE_PASS / PROFILES_PASS / PROTOCOL_PASS / TAP_PASS |
| pytest（tests/tools + tests/dsp） | 69 passed, 6 skipped |
| 浏览器 mock 测试 | `Wingie2 schema 4 configuration browser mock tests passed.` |
| 普通构建 | `/tmp/wingie2-normal-build/Wingie2.ino.bin`，1205389 bytes（91%），SHA-256 `1797e2573439bb4a04c93f45d1afe21e18c9ea6e4378668b62045e882a0196d4` |
| 诊断构建（-DMIDI_DIAGNOSTICS=1） | `/tmp/wingie2-midi-diag-build/Wingie2.ino.bin`，1207917 bytes（92%），SHA-256 `f1cf06c7b1442affee1264aeccc3222d9367d1e3e077bb6a90a13151b9ddc5c6` |

## 真机验证

- 设备：Wingie2（ESP32），串口 `/dev/cu.usbserial-11310` @115200；MIDI 激励经 CoreMIDI destination `USB MIDI DevicePort 1`（未触碰 Port 2）；全程未发送任何 `save` op。
- 烧写：诊断构建 `write_flash 0x10000` + `verify_flash` → `-- verify OK (digest matched)`。
- 驱动方式适配：计划的"常驻串口驱动 + 命令文件"模式改为单一自包含 Python 进程（`/tmp/mpe_single_zone_validate.py`，日志 `/tmp/mpe_single_zone_validate.log`）：打开串口一次 → 收集 boot → 依序执行 Step 4 全部 12 步 → 关闭。串口 open/close 会复位 ESP32，序列中途未重开串口。
- 设备 NVS 为旧值 `midi_ch_l/r/both = 1/2/3`（boot 原文：`midi_ch_l = 1 / midi_ch_r = 2 / midi_ch_both = 3`）；Step 8 仅运行时 `set_param` 改 `midi_left=8`（未保存），刷回普通构建后 get_settings 显示 `dirty:false`、`midi.left` 恢复 1，确认 NVS 未被改写。
- 补充探针（计划外，加强证据，已标注）：Step 7c `mpe-config 6 4` 强化验证 Upper MCM 忽略；Step 8b 追加一个 MPE Note On 验证常规 bend 不泄入 MPE voice。

### 启动

boot 原文确认：`mpe single zone claimed = 0x007f`。

### 逐步期望 vs 实测（34 项检查全部 PASS，0 失败）

| 步 | 激励/操作 | 期望 | 实测 | 结果 |
|---|---|---|---|---|
| 1 | `p` 基线 | `claimed=0x007f flip=0`，errors=0 | `claimed=0x007f flip=0 manager_pb=0.000`，errors=0 | PASS |
| 2 | `note-on 2 60 0` | 左 voice0 note=60 ch=2 active=1，flip=1 | `side=0 voice=0 note=60 ch=2 active=1`，`flip=1` | PASS |
| 3 | `note-on 2 64 0` | 右 voice0 note=64 ch=2 active=1，flip=0 | `side=1 voice=0 note=64 ch=2 active=1`，`flip=0` | PASS |
| 4 | `note-on 3 67 2048` | 左 voice1 note=67 ch=3 member_pb≈12.001，flip=1 | `side=0 voice=1 note=67 ch=3 active=1 member_pb=12.001 total_pb=12.001`，`flip=1` | PASS |
| 5 | `bend 1 4096` | manager_pb≈1.000；左 voice1 total≈13.001；右 voice0 total≈1.000 | `manager_pb=1.000`；左 voice1 `total_pb=13.002`；右 voice0 `total_pb=1.000` | PASS（见偏差注 1） |
| 6 | `note-off 2 60` | 左 voice0 active=0（跨侧 ownership 释放） | `side=0 voice=0 note=60 ch=2 active=0` | PASS |
| 7 | `mpe-config 0 0` → `mpe-config 6 0` | claimed 0x0000 → 0x007f，无 0x8000 段 | `claimed=0x0000` → `claimed=0x007f` | PASS |
| 7c（补充） | `mpe-config 6 4` | Upper 4 Member 被忽略，claimed 仍 0x007f | `claimed=0x007f`（若 Upper 生效应为 0xf87f） | PASS |
| 8 | `set shared midi_left 8`；`note-on 8 50 0`；`bend 8 4096` | ok:true；左 voice ch=0 total≈1.000；MPE/右侧隔离 | `ok:true`；`side=0 voice=0 note=50 ch=0 total_pb=1.000`；`manager_pb=0.000`；右侧全部 voice `total_pb=0.000` | PASS |
| 8b（补充） | `note-on 2 55 0` | 新 MPE voice total 不含常规 +1.000 | `side=1 voice=0 note=55 ch=2 active=1 total_pb=0.000` | PASS |
| 9 | `get_settings` | 无 `mpe_enabled`，含 `"midi":{"left":8` | 均无 `mpe_enabled`；`"midi":{"left":8,"right":2,"both":3}` | PASS |
| 10 | `set shared mpe_enabled 1` | `ok:false` | `{"ok":false,"error":{"code":"invalid_parameter","field":"mpe_enabled"}}` | PASS |
| 11 | `set left mode 3`；`note-on 2 70 0`；`set left mode 0` | 音符落右侧（跳过 Cave），左侧无新 voice，flip 不翻转 | `side=1 voice=1 note=70 ch=2 active=1`；左侧无 note=70 且无活跃 MPE voice；`flip=0` 不变；恢复 ok:true | PASS |
| 12 | `mpe-config 0 0` 收尾 | claimed=0x0000，全程 errors=0 | `claimed=0x0000`，`errors=0`（parsed=55） | PASS |

关键快照原文摘录：

```
MPE claimed=0x007f flip=1 manager_pb=1.000 pb_count=4 last_pb_ch=1 last_pb=4096
MPE side=0 voice=1 note=67 ch=3 active=1 member_pb=12.001 total_pb=13.002
MPE side=1 voice=0 note=64 ch=2 active=1 member_pb=0.000 total_pb=1.000      # Step 5：Manager PB 全局两侧

MPE claimed=0x007f flip=1 manager_pb=0.000 pb_count=6 last_pb_ch=8 last_pb=4096
MPE side=0 voice=0 note=50 ch=0 active=0 member_pb=0.000 total_pb=1.000      # Step 8：常规 bend 走 ch=0 路径
MPE side=1 voice=0 note=55 ch=2 active=1 member_pb=0.000 total_pb=0.000      # Step 8b：MPE voice 不泄入常规 bend

MIDI_DIAG ch=1 on=0 off=0 last_pitch=0 last_velocity=0 mode=3 current_poly=1
MPE side=1 voice=1 note=70 ch=2 active=1 member_pb=0.000 total_pb=0.000      # Step 11：左 Cave（mode=3）时落右侧

<{"v":1,"id":1,"ok":false,"error":{"code":"invalid_parameter","field":"mpe_enabled"}}   # Step 10
<{"v":1,"id":1,"ok":true,"op":"get_settings",...,"midi":{"left":8,"right":2,"both":3}}}  # Step 9：无 mpe_enabled
```

### 偏差与说明

1. Step 5 左 voice1 `total_pb=13.002`（计划期望 13.001）：纯浮点显示舍入（1.00012+12.00146=13.0016，`%.3f` → 13.002），数值正确，非功能偏差。
2. Step 9 计划用 get_settings 验证路由替代 CC 数值验证（swift 工具无裸 CC 子命令）——按计划执行；Manager CC 两侧同效未在真机逐项验证，其代码路径与 PB 同为 Manager 全局分支。
3. 验证期间 USB 串口曾因 hub 重新枚举短暂消失，设备自动恢复后一次性跑完全部序列，无影响。

## 刷回普通构建（Task 6 Step 5）

- `write_flash 0x10000 /tmp/wingie2-normal-build/Wingie2.ino.bin` + `verify_flash` → `-- verify OK (digest matched)`。
- 最终 get_settings（普通构建）：响应无 `mpe_enabled` 字段；`dirty:false`，`midi.left=1`（NVS 原值，运行时改动未保存）。

## 剩余边界

- 未听音：交替分配的听感、音色与力度响应未评价；未评价演奏手感。
- 旧设备迁移：本机 NVS 保存的 `midi_ch_l/r/both = 1/2/3` 落在 MPE Zone 内已失效（Ch1 为 Manager、Ch2/3 为 Member），需在 USB 配置页手动改为 8+；固件按既定策略不擅自改写 NVS（见 MPE.md `Migrating from the dual-zone firmware`）。
- Channel Pressure 与 CC 74 不映射（实现范围外，文档已注明）。
- Manager CC（Ch1）真机数值验证未做（见偏差注 2）。
