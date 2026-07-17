# MPE 真机复测结果（2026-07-17）

复测对象：HEAD（`e998a75`，含 `112c37b` feat(midi): 接入 MPE 逐音弯音）。方法同 2026-07-16：
诊断构建（`MIDI_DIAGNOSTICS=1`）+ CoreMIDI destination `USB MIDI DevicePort 1`（USB→DIN 接口，
仅用 Port 1）+ USB Serial `/dev/cu.usbserial-11310` 快照。新增：MCM 边界探测、String 模式串口切
换验证、普通构建刷回后的在线确认（补齐上次遗留）。

## 构建与烧写

| 镜像 | flash | SHA-256 |
| --- | --- | --- |
| 诊断 `MIDI_DIAGNOSTICS=1` | 1,208,129 B（与 7-16 相同） | `d6df42fc…7b569c8` |
| 普通（测试后刷回） | 1,205,645 B（与 7-16 相同） | `86f2bd28…e5546` |

均 esptool 3.3.0-cn `write_flash 0x10000` + `verify_flash`（digest matched）；只写 app0。

## 结果（全部符合预期，errors=0）

1. 基线：`claimed=0x0000`，`mpe_enabled=false`（开机默认关闭）。
2. MCM `3 3` → `claimed=0xf00f`（运行时建 Zone 不依赖已保存设置）。
3. 左 Poly：Ch2/3/4 带预弯音 Note On → voice 0/1/2，member_pb `+24.003 / -12.000 / 0.000`；
   预弯音在 Note On 前被捕获进 voice。
4. Ch2 Note Off → member_pb 锁存 `24.003`（Off 后 Ch2 PB 8191 不再改变它）；Ch1 Manager PB
   `+2.000` 整侧生效（voice1/2 total `-10.000 / +2.000`）。
5. 右 Zone：Ch15 member `+12.001` + Ch16 manager `-1.000` → total `+11.001`。
6. RPN 0 改 Member range ±12 → 在发 voice 按新量程重算（Ch3 voice `-12.000→-3.000`），新 Note
   半量程 `+6.001`；右 Zone 量程不受影响（per-zone）。
7. 共存隔离：`midi_left=5` 后 Ch5 普通 Note 轮询占用 voice0；Ch5 PB `4096` → voice0 total
   `+1.000`（普通 ±2），MPE voice 1/2 total 不变（7-16 修复的串扰未复现）。
8. 串口 `mpe_enabled`：0→0 为 no-op（MCM 建立的 Zone 保留）；0→1 重建默认双 Zone 并复位全部
   MPE 通道演奏状态；1→0 → `claimed=0x0000`。
9. String 模式（串口 `set_param left mode=1` 切换）：mono owner `ch=2 active=1 member_pb=12.001`；
   Note Off 后 `active=0` 且 member 锁存（后续 Ch2 PB 不改变），Manager PB `+1.000` 仍生效。
10. 探测：MCM `1 1`→`0xc003`；`0 0`→`0x0000`；`15 0`→`0xffff`（重叠撤销对侧 Zone）；随后
    `16→3, 1→3` 最近配置优先回到 `0xf00f`；Ch5 上发 MCM 为 no-op；满量程 PB 精确
    `±48.000`；第四音替换最旧 voice；失效 Note Off（已被替换的音）为 no-op。
11. 刷回普通构建 + `get_settings`：`mpe_enabled=false`、`midi 1/2/3`、`tuning=-1`、`dirty=false`
    —— 运行时改动未持久化；同时补齐 7-16 遗留的“最终二进制重启后在线确认”（session 开始时
    设备刚 POWERON_RESET，普通构建在线应答正常）。

## 观察与边界

- 串口 open/close 会通过自动复位电路复位 ESP32（DTR/RTS 瞬态）；测试须用单一常驻进程持有
  串口。boot 期间（`serial_config_ready` 前）不读 MIDI，此时发送的 MCM 会丢失。
- Zone 生效期间，Ch14/15（Cave 频率 CC）与 Ch16（全局设置 CC：A3、MIDI 路由）被 MPE 路径接管，
  常规功能不可达（MPE.md 只明确写了 Left/Right/Both 路由让位，未提这两个 CC 面）。
- 诊断快照不回读 `poly_pitch_ratio_*` DSP 参数；ratio 写入路径经 `set_poly_voice_dsp` 隐式
  执行，数值验证到 total_pb（semitone）为止。
- 未做扬声器听音与 PB 手感评价（同 7-16 边界）；本次 Volume 读数为 1.0（上次为 0.0）。
- 完整串口日志：session 临时文件 `/tmp/wingie_log.txt`（重启后失效）。
