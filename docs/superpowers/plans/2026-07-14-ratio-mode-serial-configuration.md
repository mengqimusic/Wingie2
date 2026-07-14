# Ratio Mode、Cave 与纯 HTML 串口配置实施计划

## 目标

新增一个可由键盘和 MIDI 演奏的 Ratio Mode。左右声道共用一个九 ratio profile，ratio
允许低于 `1`，不提供逐共鸣器 mute；同一个页面同时管理现有 Cave Mode 的左右两侧、
三 bank、频率和 mute。配置使用可直接植入现有 HTTPS 网页的单文件 HTML，通过浏览器
Web Serial API 连接 USB 串口；不在 ESP32 上加入 SoftAP、HTTP server 或网页资源。

## 开始前的设计门

以下值在写入 DSP 或 HTML 常量前必须确认并固定，不能从实现细节推导：

- ratio 的最小值、最大值和编辑精度；
- factory profile 的九个初始值；
- Ratio Mode 两灯闪烁节奏；
- 目标桌面浏览器和网页 iframe 的串口权限策略。

协议通过 `limits` 元数据传给网页，因此网页不应复制一份未经确认的 ratio 范围。

## 硬边界

- 保持 ESP32 Arduino Core `2.0.4`、Faust `2.59.6` 和 `44.1 kHz / 32 samples`；
- 不恢复或重新尝试 SoftAP、WebServer、外置天线或金属壳修改；
- Ratio Mode 使用现有 block-rate rotation modal bank；ratio/note 改变保留状态，
  在下一个 block 使用新系数；
- 不加入 reset、crossfade、pitch smoothing、自动 mute 或新的音频时间常数；
- 所有九 ratio 必须作为一个完整 snapshot 校验和应用，禁止部分更新；
- Cave 每次以一个 `side + bank` 的 9-frequency/9-mute snapshot 原子更新；
- Cave 继续使用既有 2 × 3 × 9 Preferences key，不复制新的 Cave 存储格式；
- 串口 parser、JSON 解析和 Preferences 写入不运行在 Faust audio task；
- 网页实时编辑不写 flash，只有明确的 `save` 命令写 Preferences；
- 物理测试只写 app0 `0x10000`，测试后恢复可验证的历史 diagnostic app0；
- 不手改生成的 Faust C++，DSP 改动只在 `Wingie2.dsp` 完成并同批生成。

## 文件边界

### 新增

- `Wingie2/serial_config.ino`：Ratio/Cave framing、请求队列、响应和错误；
- `Wingie2/config_profiles.h`：Ratio profile、Cave bank snapshot、范围元数据、校验和与版本；
- `Tools/wingie_config.html`：Ratio/Cave 单文件 HTML/CSS/JavaScript，直接使用 Web Serial；
- `tests/host/config_profiles_test.cpp`：Ratio/Cave 校验、版本、原子替换和持久化编码测试；
- `tests/host/serial_config_protocol_test.cpp`：framing、请求/响应和错误 fixture；
- `tests/dsp/ratio_mode_reference.py`：基频、ratio、频率边界和 alternate tuning reference；
- `docs/superpowers/results/2026-07-14-ratio-mode-serial-configuration-results.md`：
  编译、串口和真机结果。

### 修改

- `Wingie2.dsp`：加入共享 `ratio_mode_ratio_0..8` 控件、Ratio Mode 频率映射并把模式
  选择扩展到五种；
- 同批生成 `Wingie2/Wingie2.cpp` 和 `Wingie2/Wingie2.h`；
- `Wingie2/Wingie2.ino`：加入 `RATIO_MODE`、第五模式 LED 状态、共享 Ratio profile 全局状态，
  保持 Wi‑Fi 关闭；Cave 继续使用现有数组和 active bank；
- `Wingie2/MIDI.ino`：让 Ratio Mode 复用 String/Bar 的 Note On 路径，并拒绝非法 mode；
- `Wingie2/control.ino`：加载 profile、应用 ratio/Cave、处理第五模式闪烁和串口 service；
- `Wingie2/stuff.ino`：把共享 Ratio profile 纳入现有保存流程，保留 Cave key 写入；
- `README.md`：补充纯 HTML Web Serial 的使用前提和浏览器限制。

## 实施任务

### 1. 固定 Ratio/Cave 数据合同

先完成 `config_profiles.h` 与 host tests，再接入 DSP。合同至少包含：

- schema version；
- 九个有限正 ratio；
- `revision` 和 dirty 状态；
- `min/max/step` 元数据；
- factory profile 与 CRC；
- `getBytes/putBytes` 的固定编码，避免九个独立 NVS key 部分写入。
- Cave bank 的 `side`、`bank`、9 个 frequency、9 个 mute 和 active 查询状态；
- Cave 不新增 NVS 编码，测试必须确认现有 key 的读写兼容。

失败的 profile 必须整体拒绝并保留当前运行值。

### 2. 实现并测试串口协议

按照 [Ratio Mode Web Serial 协议](../specs/2026-07-14-ratio-mode-serial-protocol.md) 实现：

- `@` 请求、`<` 响应、换行 framing；
- `hello`、`get`、`set`、`save`、`reset`、`status`；
- `get_cave`、`set_cave`，每次完整替换一个 side/bank；
- 重复 id、半包、CRLF、超长行、未知命令和 malformed JSON；
- `expected_revision` 冲突检测；
- `set` 的完整 snapshot 原子应用；
- active Cave bank 立即排队到 DSP，inactive bank 只更新配置；
- 响应明确区分 queued、saved 和错误。

解析器使用固定大小 buffer 和固定 schema，不在 audio task 分配内存。若考虑引入
ArduinoJson，必须先用项目工具链测量 program/heap 成本并单独记录决策；默认不增加新
第三方依赖。

### 3. 安装 Ratio Mode DSP

在 `Wingie2.dsp` 中实现：

```text
fundamental = note_freq(note)
frequency_i = clamp(fundamental * ratio_i, 16, 16000)
```

ratio 控件必须位于左右 hgroup 之外，使同一 profile 同时驱动两个声道。逐 sample 路径
只使用现有 modal rotation；frequency、sin/cos 和输出标定继续按现有 block-rate 结构
处理。生成后检查：

- 9 个 ratio 控件只有一份共享 UI inventory；
- 五个 mode source 选择正确；
- standard/alternate tuning 都经过同一个 `note_freq`；
- 没有新增逐 sample 的 `pow/sin/cos/sqrt`；
- 频率上下界和 finite 状态成立。

### 4. 接入固件演奏和 LED

- 将 `MODE_NUM` 扩展到 Ratio Mode；
- 所有 `ledColor[Mode]` 访问先处理 Ratio Mode，禁止数组越界；
- Ratio Mode 用确认后的双灯 blink 节奏，不能与 save routine 混淆；
- 本机键盘沿用 String/Bar 的 first press、tap sequence 和 octave 行为；
- MIDI Note On 沿用 String/Bar 的 `note0/note1` 路径；
- `CC_MODE` 只接受 `0..RATIO_MODE`；
- mode change envelope、wet/dry、decay 和 anti-feedback 行为保持不变。

### 5. 接入 Ratio/Cave Preferences 与运行时更新

- 启动时读取共享 profile；不存在或 CRC 错误时加载 factory profile；
- 每个 `set` 只更新 RAM 和 DSP 参数，标记 dirty；
- `save` 和现有物理保存流程都写同一个 profile blob；
- 重启后 `get` 必须返回已保存 profile；
- reset 只恢复 RAM，除非随后收到 save；
- 不为左右复制两份 ratio 状态。
- Cave 启动时读取现有 2 × 3 × 9 frequency/mute keys；
- `set_cave` 修改 active bank 时调用现有 DSP 参数路径，inactive bank 不触碰当前 DSP；
- `save` 写 Ratio blob 和所有 dirty Cave key；
- 不在首版定义 Cave factory reset，避免覆盖已有用户 Cave 音色。

### 6. 制作纯 HTML 页面

`Tools/wingie_config.html` 必须是一个可独立复制的文件：

- CSS、JavaScript 和 UI 全部内联；
- 不使用 CDN、构建工具、Python bridge 或 ESP32 HTTP server；
- 用户点击 Connect 后调用 `navigator.serial.requestPort()`；
- 自动完成 hello/get，展示设备 limits；
- Ratio 区域显示一个共享的九行 ratio 表，不显示左右重复编辑器；
- 显示 ratio、cents、左右当前 effective Hz；
- Cave 区域显示左右 tab、bank 选择、active bank、9 个频率和 9 个 mute；
- Cave 修改 active bank 时显示 live 状态，修改 inactive bank 时显示 pending 状态；
- 提供 Apply/Save、Ratio Reset、dirty 状态、Export/Import；Cave Reset 暂不实现；
- 处理 unsupported browser、permission denied、disconnect、timeout 和 malformed response；
- 所有串口请求带 request id，响应按 id 配对；
- 不把未收到的 status 猜成零或默认音高。

### 7. 静态和完整构建验证

先跑 host tests 和 DSP reference，再生成 Faust 文件并执行：

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries Libraries Wingie2
```

检查 program storage、global variables、generated UI inventory、Ratio Mode frequency
mapping 和无 Wi‑Fi 依赖。HTML 只做静态语法/协议 fixture 检查，不把页面加载成功当成
固件验证。

### 8. USB 串口真机 smoke test

使用单文件 HTML 和当前 diagnostic 串口确认：

1. hello/get 返回共享 Ratio profile、limits 和 Cave capability；
2. set 九值后 Ratio Mode 在下一个 block 使用新 profile；
3. `get_cave/set_cave` 能读写左右三 bank 的频率和 mute；
4. active Cave bank 修改实时生效，inactive bank 在 octave 选择后生效；
5. standard/alternate tuning 下左右键盘与 MIDI Note On 都能移调；
6. ratio 小于 `1`、接近上下限、快速 note/ratio 变化均保持 wet、finite 和稳定；
7. Ratio reset 不写 flash，save 后断电重启仍保留 Ratio 与 Cave 修改；
8. Save、mode change、Ratio blink、Cave mute、wet/dry、decay 和 anti-feedback 不互相破坏；
9. 退出 Ratio Mode 后 Poly/String/Bar/Cave 和原有 preferences 不回归。

每次候选刷写只写 app0；结果文件分别记录编译结果和用户确认的物理行为。

## 提交边界

建议拆成以下独立提交：

1. `test: 添加 Ratio profile 与串口协议 host fixtures`；
2. `feat(dsp): 添加共享 Ratio Mode`；
3. `feat(firmware): 接入 Ratio 串口配置和 Preferences`；
4. `feat(web): 添加纯 HTML Ratio/Cave Web Serial 配置页`；
5. `test(esp32): 记录 Ratio Mode 串口真机结果`。

任何一项硬件或时序 gate 失败，都停止该候选，不把失败结果写成 PASS。
