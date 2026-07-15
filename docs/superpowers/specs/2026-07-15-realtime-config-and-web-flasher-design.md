# Wingie2 实时配置与网页刷机设计

## 已确认产品行为

- Ratio Mode 使用前四种模式的 LED 颜色，以每色 `20 ms` 循环形成综合色；Save 动画优先。
- 配置页同时显示 Left / Right，设备 RAM 是唯一事实源。硬件、MIDI、网页修改走同一组 setter，
  同时更新 DSP、LED、dirty/revision 和网页状态。
- 页面没有 Apply。编辑立即作用于 RAM/DSP；只有 Save 写 Preferences，并且网页 Save 每次确认。
- Ratio 输入保留编辑中的小数文本；有限数自动 clamp 并按设备 step 量化，空值恢复最后确认值。
- Cave 频率使用 `0.01 Hz`，范围 `16.00..15999.99 Hz`。旧整数 Preferences 只读迁移；只有
  明确 Save 才写带 version/scale/CRC 的新格式，旧 key 保留。
- Mix、Decay、Volume 仍是 runtime-only。网页或 MIDI 单侧接管后，物理共享滑杆沿用 soft takeover；
  重新接管时同时控制左右。
- Octave / active Cave bank 与 Mic/Line 是绝对位置物理开关，网页只读。
- MIDI channel 保留 `1..16`。Note 与设置 CC 不冲突；13--16 的额外设置 CC 与演奏 CC 编号不重叠。

## 实时协议

- USB Serial 继续使用 `@` 请求、`<` 响应、JSON Lines 和 request id。
- envelope 保持 `v:1`，`config_schema` 升为 `2`。
- `hello/get_state` 携带每次启动生成的 `boot_id`；WDT 或重启时即使 USB 串口不断开，网页也会
  清除旧 revision epoch 并重新读取完整设备状态。
- 新增 scalar 与单 resonator patch，避免整 bank/profile 覆盖并发硬件修改。
- Ratio/Cave patch 携带对应 resource revision；设备原子检查 `expected_revision`，ack 返回精确
  `resource_revision`，连续写和多页面写不会用旧状态静默覆盖新状态。
- control/MIDI/hardware 修改只标记单调 state revision；Serial loop 合并发送轻量 change event。
- 网页收到 event 后合并 `get_state`，按 Ratio/Cave revision 只读取变化资源；另有低频 heartbeat。
- 所有串口输出只由 Serial loop 发送；高频 Cave 按键和 ADC 不逐事件排队。

## 持久化

- Web 与硬件保存共用一个 `save_all_configuration()`。
- 持久化 Mode、threshold、pre/post gain、MIDI channels、A3、调律、Ratio、Cave、mute 和
  alternate-tuning 的 unquantized Cave backup。
- 只有对应写入成功后才清 dirty；失败保持运行值与 dirty。
- Save 在短锁内取得 immutable snapshot，在锁外写 NVS，再按 revision 清 dirty；并发 Save 被序列化。
  Alternate-tuning metadata 只在 unquantized Cave backup 全部成功后写入。
- 启用 alternate tuning 不再立即写 flash；A3 在 alternate tuning 中变化时重新计算 Cave。

## 网页刷机选择

独立刷机页直接使用 `esptool-js 0.6.0`，不使用 stock ESP Web Tools 安装组件：

- ESP Web Tools `10.3.0` 对没有 Improv Serial 的设备可把 `eraseFlash()` 带入普通安装流程；
- 它的标准写入路径没有传 `calculateMD5Hash`，因此没有逐段写后 MD5 比对；
- direct 路径能固定 `eraseAll:false`、在 ROM 中识别 `ESP32`、控制四段 offset，并逐段校验。

刷机页使用严格 Wingie2 manifest，不宣称 ESP Web Tools 兼容。普通流程：

1. 用户手势选择串口；不发送固件 hello，也不读取 app0；
2. 下载四段并按 manifest SHA-256 校验；
3. `ESPLoader.main()` 通过 DTR/RTS 尝试进入 ROM bootloader 并识别芯片；
4. 只接受 `ESP32`；
5. 以 `DIO / 80 MHz / 4 MB` 和 `eraseAll:false` 写 `0x1000 / 0x8000 / 0xe000 / 0x10000`；
6. `calculateMD5Hash` 启用每段 flash MD5 校验，成功后 hard reset。

普通刷机页不提供整片 erase 或 NVS erase。若未来增加恢复出厂，只能作为独立高级页面，
只处理 `0x9000..0xdfff`，并有单独警告与确认。

## 发布安全门禁

- manifest 必须声明 `chipFamily=ESP32`、`eraseAll=false`、NVS preserve range 和恰好四段。
- 所有写入范围按 `4 KiB` flash sector 向上取整后检查；partition sector 恰好停在 `0x9000`，
  `boot_app0` 从 `0xe000` 开始。
- app 大小必须 `<= 0x140000`；bootloader/app 必须通过 ESP32 `image_info`；partition 必须解析为
  NVS `0x9000/0x5000`、otadata `0xe000/0x2000`、app0 `0x10000/0x140000`。
- 发布目录包含带版本文件、manifest、`SHA256SUMS.txt`、中英文说明和固定版本网页依赖。
- 同时生成带版本的 standalone HTML，把 manifest、四段镜像和固定 vendor 全部内嵌；页面仍逐段
  校验 SHA-256，且不请求相邻固件资源。Squarespace 只以按钮打开其顶层 HTTPS 地址。
- `esptool-js 0.6.0` 与 `js-md5 0.8.0` 发布文件同时锁定 SHA-256，替换或版本漂移会 fail closed。
- GitHub Release 只允许手工创建 Draft；本任务不 push、不发布、不连接或刷写硬件。

## 硬件门禁

- A：整片擦除过的 Wingie2 从 ROM 首次安装四段并正常启动。
- B：v3.1 设备升级后确认 `0x9000` NVS 中 MIDI、调律、Cave、Ratio 等设置保留。
