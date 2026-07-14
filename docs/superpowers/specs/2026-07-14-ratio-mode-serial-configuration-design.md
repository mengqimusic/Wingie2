# Ratio Mode 与串口配置设计决策

## 状态

本文记录 2026-07-14 设计讨论中已经确认的产品边界，以及 SoftAP 真机试验后确定的
连接路线。实施计划尚未编写；未确认的参数不得在计划或代码中自行补全。

## Ratio Mode

新增一个可移调的九共鸣器模式。键盘和 MIDI Note On 与 String、Bar 模式一样改变
基频，九个共鸣器保持各自相对基频的比例：

```text
fundamental = note_freq(note)
frequency_i = fundamental * ratio_i
```

用户已确认的行为：

- 左右声道共用同一个九 ratio profile；
- ratio 为正数并允许低于 `1`，因此可以产生 subharmonic；
- 不提供逐共鸣器 mute；
- Ratio Mode 是第五个模式，面板以闪烁和四个常亮模式区分。

当前设计基线（实施计划中仍需验证）：

- 标准与 alternate tuning 都先决定 `fundamental`，ratio 不改写 tuning；
- 音符和 ratio 变化保留 modal state，在下一个 32-sample block 使用新系数；
- 不加入 reset、crossfade、pitch smoothing 或其他隐藏时间常数；
- 沿用当前 modal bank 的 `[16, 16000] Hz` 频率边界，网页应显示边界后的实际频率，
  不能把超界情况静默解释为 mute；
- 键盘和 MIDI 的音符路径沿用 String、Bar 的基频驱动方式。

当前默认沿用 String、Bar 的 Note On、Tap Sequencer 和 Note Off 语义。若要改变这些
演奏行为，必须先作为新的乐器设计决定讨论，不能由实现细节顺带改变。

## 配置连接路线

当前产品不采用 ESP32 SoftAP 作为配置入口。最终网页是可直接植入现有站点的单文件
HTML，使用浏览器 Web Serial API 连接 USB 串口与 Wingie2：

```text
single HTML page
  -> navigator.serial
  -> USB serial
  -> one validated RatioConfig command path
  -> control task
  -> DSP parameters at the next block boundary
  -> Preferences only after an explicit save command
```

网页和工程工具必须调用同一个固件配置入口。串口接收路径不得逐字段直接修改 DSP；
它先校验完整九 ratio snapshot，再把完整配置交给 control task 应用。实时编辑只改变
运行值，用户明确执行 Save 时才写 Preferences。

网页包含内联 HTML、CSS 和 JavaScript，不依赖 CDN、Node、Python、ESP32 WebServer 或
无线网络。页面必须由用户手势触发串口选择；固件串口协议、校验和持久化语义独立于网页
的视觉实现。

Web Serial 的浏览器支持和安全上下文是产品使用前提。目标页面应优先运行在 HTTPS 的
桌面 Chromium 浏览器中；`file://`、移动 Safari 或未授权的 iframe 不作为默认兼容目标。

## SoftAP 真机试验

试验只写 app0 `0x10000`，未写 bootloader、partition table、otadata 或 NVS。所有试验
镜像均在 `/tmp` 构建，没有修改仓库源文件。

### 完整产品镜像

- 临时把启动行为改为 `WiFi.mode(WIFI_AP)`，SSID 为 `Wingie2-AP-Test`；
- 编译占用 `1,191,357 / 1,310,720 bytes`；
- 串口确认 `WiFi.softAP` 成功并报告 `192.168.4.1`；
- Mac 未发现该 SSID，未获得 DHCP 地址，ping 与 HTTP 均未建立；
- 固件约每 `13.18 s` 触发 `task_wdt` 并重启；日志记录 CPU 0 正运行
  `Faust DSP Task`，未得到调度的是 CPU 0 idle task。

这证明当前完整产品中直接开启 SoftAP 不是可运行方案，与金属外壳是否阻断射频无关，
单是 watchdog 重启已经足以拒绝该路线。

### 最小 AP 探针

- 另行构建只包含 ESP32 SoftAP 与最小 HTTP server 的探针；
- 编译占用 `711,757 / 1,310,720 bytes`；
- SSID `Wingie2-AP-Probe` 在闭合金属外壳条件下仍未被 Mac 发现；
- 主机未能完成关联、DHCP、ping 或 HTTP。

Mac 在该轮测试中同时出现系统信息显示 Wi-Fi 已连接、`networksetup` 报告未关联的矛盾，
因此不能把最小探针结果单独解释为金属外壳完全屏蔽射频。它仍然足以说明当前硬件、
外壳和主机组合不具备可依赖的 SoftAP 配置链路。

### 恢复

测试后重新编译并只写回仓库当前的 diagnostic app0。写入完成且 hash verify 通过；恢复
镜像 SHA-256 为：

```text
c7773da4d11f06047fec7f9873287816706e6ff0395ec3750b93770ae19cdb06
```

该 hash 与 `docs/superpowers/results/2026-07-14-anti-feedback-control-results.md` 中记录的
当前 diagnostic 产品镜像一致。

## 明确不做

- 当前硬件不再重复尝试产品内 SoftAP 或 HTTP server；
- 不为本功能修改天线、外壳或增加外置无线硬件；
- 不建立左右独立 ratio profile；
- 不增加逐共鸣器 mute；
- 未完成剩余设计决定前不写实施计划。

## 待确认

- ratio 的最小值、最大值、编辑精度和默认 profile；
- Ratio Mode 的具体 LED 闪烁节奏；
- 目标浏览器和站点 iframe 的串口权限配置；
- 串口 framing、schema version、request/response 和错误返回格式；
- profile 的导入、导出、Reset 和 Save 交互；
- Preferences 数据格式、版本迁移与恢复默认值。
