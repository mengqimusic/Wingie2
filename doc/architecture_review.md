# Wingie2 架构优雅性评审

评审日期：2026-08-08
评审方式：四个并行 subagent 分别扫描固件核心、MIDI/MPE、串口配置/持久化、工具链/测试，主 session 复核 DSP 源。

## 总体判断

架构轴（分层、纯头测试边界、fail-closed 发布链）是优雅的；实现层的状态组织和"单一事实来源"是主要失分点。

亮点（保持不动）：
- `Pressed0/1` 中断仅置 volatile 标志、扫描放任务侧的并发范式（Wingie2.ino:329-335）
- `mpe_state.h` / `tap_sequence.h` / `serial_config_protocol.h` 的 Arduino-free 边界，host 测试直编固件头
- `build_release.py` 的 fail-closed 校验链（SHA256 钉死、镜像/分区精确匹配、原子 rename）
- 协议层：magic+version+scale+crc32 blob + `static_assert` 布局守卫 + decode 原子失败
- 迁移策略用 legacy key 作校验真相而非盲目单向迁移
- 配置帧再同步（吞字节到 `\n`）、CRLF 容忍、hello 自描述

## 高优先级发现

### H1. 跨核共享状态无数据契约

双核共享 `Mode` / `cm_freq` / `a3_freq` 等约 60 个全局变量（Wingie2.ino:136-272），无同步机制。
- `noInterrupts()` 在 ESP32 Arduino Core 2.0.4 是空操作，却被用于"保护"跨核快照读取（serial_config.ino:430-438）
- `mpeMonoState` 是双任务写者，struct 级三字段复位事务可被 MIDI 中断插入（MPE.ino:137-142）
- volatile 使用错位：单核内读写的中计数标了 volatile，真正跨核的 `Mode`/`cm_freq`/`a3_freq` 反而不标
- `mark_cave_changed` 两步写（revision++ 与 dirty=true）跨核读者可观察到中间态

### H2. 同一数据多份手工维护（SSOT 缺失）

- 参数 min/max/step 表：页面 `parameterSpecs`（wingie_config.html:737-750）与固件 `applyScalarParameter` 双份
- 分区布局：build_release.py / flash_mode_filter_candidate.py / wingie_flasher.html ≥4 处
- CC 映射：固件宏 / PD 补丁 / TouchOSC / midi_stress.swift 四份
- NVS key 字符串：`l_cf_%d_%d` 等在三处拼写
- `config_schema` 散落 4 处，靠字符串测试交叉验证
- 常量对拷：`0.0051f`、`*9.9+0.1`、`(ch?12:0)`、fscale 曲线参数等

### H3. 映射层未集中（违反全局原则"映射层集中在一处"）

- pot 与 MIDI CC 对 mix/decay/volume 的归一化是逐行复制（MIDI.ino:162-206 vs control.ino:424-441）
- DSP 路径串在 MPE.ino 与 midi_diagnostics.ino 重复
- MPE.ino:121-126 的 pitch 刷新"顺带"重写 decay boost，model 更新与 DSP push 未分离，遗漏即状态漂移
- `refresh_mpe_zone_pitch` / `refresh_mpe_member_pitch` / `refresh_mpe_member_expression` 三份近似循环

### H4. alternate tuning 状态机四入口不收敛，存在真实缺陷

boot 按键（control.ino:313-345）/ MIDI CC23（MIDI.ino:49-79）/ Web（serial_config.ino:227-248）/ 启动加载各自重复实现启用/禁用。
真实缺陷：boot 右键禁用路径（control.ino:329-338）只关 `use_alt_tuning`，不 `restore_caves_to_unq()` 也不清 `unq_caves_store`；残留标志会让下次启用时用已量化频率覆盖未量化备份，破坏"恢复原音"不变量。
模式切换三路径也不收敛：按键/MIDI 经标志汇入 control 任务，Web 路径直接写 `Mode` 绕过队列。

### H5. 重复代码族

- 保存循环双份（serial_config.ino:690-795），~40 行逐行重复
- 键盘矩阵位反转索引三份（control.ino:283-298 / 545-554 / 564-572）
- `dirty[11]` 魔数索引贯通 4 文件，无命名常量
- `save_general_preferences` 名不副实（stuff.ino:108-137 还管 tuning 与 unq 备份）
- `CaveBankState::dirty` 死字段与全局 `cave_config_dirty[]` 两套约定并存

## 中优先级发现

- 配置页 E2E 游离：`wingie_config_browser_test.sh`（563 行）不被 run_tests.sh / pytest / CI 任何入口引用；browser 测试在 CI 静默 SkipTest
- 串口帧无超时：`@` 后无 `\n` 即永久挂死，吞掉后续所有字节（serial_config.ino:797-826）
- 协议层只封装解析，响应编码全在 Arduino 侧，host 测试覆盖不到
- NVS 写放大：每次脏保存双写 legacy key + v2 blob（~120 次 NVS 操作/保存）
- 默认值三处定义：首次初始化写（control.ino:100-131）、load 默认参数（137-164）、`cm_freq_prev` 静态初值
- `midi_ch_l/r/both` 无唯一性校验，可互相相等或与 13-16 冲突导致双触发
- `serial_config.ino:803` 溢出错误用 `id:0`，无法关联请求
- 同一串口并存两套协议（`@`-framed JSON 与 midi_diagnostics 单字符协议）
- 通道号 14/15/16、RPN 常量 6/38/100/101 为裸字面量
- DSP `Wingie2.dsp:60-101` 三份 `mtof/mtoq` 副本是注释自认的"partial redundancy"优化，无 CPU 基线测量佐证

## 修复计划（按影响排序，每项独立提交）

1. 收敛跨核写者为单一权威核 + 请求队列（H1）— **已修复** f0d2fce
2. 统一 alternate tuning 单一入口状态机，修复 unq 备份覆盖缺陷（H4）— **已修复** 53ab3ae
3. 表驱动 settings 层替换 `dirty[11]`，默认值/常量并入 `wingie_config`（H3/H5）— **已修复** 21223c7
4. 响应编码下沉纯头 + 保存/迁移状态机 host 测试（H5/中优先级）— **已修复** 395fd04（编码下沉完成；保存状态机 host 测试暂缓，见下）
5. 单一 flash layout.json + 参数 limits 固件下发（H2）— **已修复** 8719fa7（layout.json）、7995b07（参数 limits）

### 修复记录（2026-08-08）

- **f0d2fce**：跨任务共享状态收敛为 `device_state` 模块 + portMUX 临界区。新增 device_state.h/ino，cave bank 事务与模式切换标志原子化；删除 3 处无效 noInterrupts；volatile 修正。行为零改变。
- **53ab3ae**：alternate tuning 统一为 `set_alt_tuning(index, persist_backup)` 单一入口。修复缺陷 A（boot right 禁用残留 unq_caves_store 覆盖备份）与缺陷 B（boot left 启用不建未量化备份）。
- **21223c7**：settings 保存表驱动化（`GeneralSettingEntry` 表 + 循环），`dirty[11]` 魔数改 `SettingsDirtyIndex` 命名枚举；性能参数量化/默认值常量并入 `wingie_config`。
- **395fd04**：serial 响应编码下沉 `serial_response.h`（Arduino-free 纯头，11 个 encode*），新增 host 字节级测试覆盖全部响应格式。
- **8719fa7**：flash 分区布局单一真值 `Tools/firmware_release/layout.json`，build_release / flash_mode_filter_candidate / draft 清单全部对齐 + 6 项一致性测试。
- **7995b07**：参数 limits 由固件 get_settings 下发（named 扁平数组），页面渐进增强回退；`kMaxFrameBytes` 512→1024；config_schema 保持 5。

### 遗留（未做）

- 保存/迁移状态机（dirty 四象限、migration_pending 回滚）host 测试：决策逻辑与全局数组/Preferences 交织，抽纯函数代价较高，单独排期。
- serial 帧无超时（`@` 后无 `\n` 永久挂死）、NVS 写放大（legacy 双写）、midi 通道唯一性校验、`CaveBankState::dirty` 死字段、`wingie_prod_test.html` NVS 常量入 layout.json：属中优先级，未纳入本次修复序列。
- 硬件行为验证：本批 5 个修复均为编译 + host + Python 测试验证，未烧录物理设备；烧录前建议先做一次覆盖受影响的 audio/MIDI/I2C/prefs 路径的 smoke test。

## 验收方式

- 每个修复后跑 host 测试（`g++ -std=c++17 -I. tests/host/<name>_test.cpp`）与 `python3 -m pytest tests/ -q`
- 固件改动必须通过 Arduino CLI 编译（`arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries Wingie2`）
- 硬件行为验证单独报告，与编译结果分开
