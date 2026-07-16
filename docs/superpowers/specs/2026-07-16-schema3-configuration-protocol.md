# Wingie2 Schema 3 配置协议

## 状态与范围

这是当前固件与 `Tools/wingie_config.html` 已实现的配置协议事实记录。它取代
`2026-07-14-ratio-mode-serial-protocol.md` 中的 schema 1 草案。

协议 envelope 的 `v` 仍为 `1`；设备配置能力版本由 `hello.config_schema` 表示，当前值为
`3`。Schema 3 在既有 Ratio/Cave 命令之外增加 `get_settings` 与 `set_param`，不增加
event、heartbeat、后台自动轮询或设备主动推送。

## 传输与 envelope

- USB Serial，`115200` baud；
- 浏览器请求以 `@` 开头，固件协议响应以 `<` 开头；
- 每个 JSON object 占一行，接受 `LF` 或 `CRLF`；
- 单帧最大 `512` bytes；
- 其他不以 `<` 开头的串口输出是调试日志，网页忽略；
- control task 完成初始化与 Preferences 加载前，所有合法请求返回 `busy`。

```text
@{"v":1,"id":17,"op":"get_settings"}\n
<{"v":1,"id":17,"ok":true,"op":"get_settings",...}\n
```

`id` 是请求方提供的无符号整数。响应使用相同 `id` 与对应请求配对。

## `hello`

请求：

```json
{"v":1,"id":1,"op":"hello"}
```

当前响应形状：

```json
{
  "v": 1,
  "id": 1,
  "ok": true,
  "op": "hello",
  "device": "Wingie2",
  "capabilities": ["settings", "ratio_mode", "cave_config"],
  "config_schema": 3,
  "transport": {"baud": 115200, "max_frame": 512}
}
```

网页只接受 `device == "Wingie2"` 且 `config_schema == 3`；不满足时停止读取和写入。若启动期
`hello` 返回 `busy`，网页只重试握手，最长等待约 `5 s`；这段重试不会读取配置，也不会在连接
成功后留下定时任务。

## 网页连接与刷新

连接成功后的读取是一次固定的完整快照，顺序为：

1. `get_settings`；
2. `get`；
3. 左右各 3 个 bank，共 6 次 `get_cave`。

网页不会在连接后启动 event listener、heartbeat 或定时轮询。用户点击 Refresh 时，网页先提交
并等待已排队的编辑写入，再重复同一套完整快照读取。

因此，实体控件或 MIDI 在快照之后造成的设备变化不会自动回写网页；需要手动 Refresh 才重新读取。
`set_param` 返回 `caves_changed: true` 是本次网页写入的因果结果，网页会立即重读 6 个 Cave
bank；这不是后台同步或自动轮询。

网页没有 Apply。当前页面编辑 Mode、Threshold、共享设置、Ratio 和 Cave；select 与 Cave mute
立即发送，其他数值在最后一次输入后约 `150 ms` 提交。Save 只负责持久化。固件仍保留
Mix/Decay/Volume 的 `set_param` 能力，但当前网页不显示或编辑这些仅运行态参数。

## `get_settings`

请求：

```json
{"v":1,"id":2,"op":"get_settings"}
```

响应：

```json
{
  "v": 1,
  "id": 2,
  "ok": true,
  "op": "get_settings",
  "source": "line",
  "dirty": false,
  "left": {
    "mode": 0,
    "mix": 0.5000,
    "decay": 1.0000,
    "volume": 0.5000,
    "threshold": 0.4125
  },
  "right": {
    "mode": 0,
    "mix": 0.5000,
    "decay": 1.0000,
    "volume": 0.5000,
    "threshold": 0.4125
  },
  "shared": {
    "a3_hz": 440.00,
    "tuning": -1,
    "pre_clip_gain": 0.2475,
    "post_clip_gain": 0.8250,
    "midi": {"left": 1, "right": 2, "both": 3}
  }
}
```

- `source` 为 `mic` 或 `line`，schema 3 只读，没有对应 `set_param`；
- 左右 `mode`、`mix`、`decay`、`volume`、`threshold` 分别读取当前设备值；
- 当前网页只使用 `mode` 与 `threshold`；`source`、`mix`、`decay`、`volume` 保留在固件响应中供
  其他客户端兼容，不在本页面显示；
- `tuning == -1` 表示 Standard，`0..7` 表示 8 个 alternate tuning；
- `dirty` 只表示一般持久化设置是否有未保存变化，不包含 Ratio profile dirty、各 Cave bank
  dirty，也不包含仅运行态的 Mix/Decay/Volume。

## `set_param`

请求：

```json
{
  "v": 1,
  "id": 3,
  "op": "set_param",
  "target": "left",
  "name": "threshold",
  "value": 0.41
}
```

`target` 只接受 `left`、`right`、`shared`。成功响应返回设备实际采用的 canonical value：

```json
{
  "v": 1,
  "id": 3,
  "ok": true,
  "op": "set_param",
  "value": 0.4125,
  "dirty": true,
  "caves_changed": false
}
```

### 左右独立参数

| `name` | 固件范围与 canonicalization | Save 持久化 | 已实现行为 |
| --- | --- | --- | --- |
| `mode` | 四舍五入为整数后裁剪到 `0..4` | 是 | `0` Poly、`1` String、`2` Bar、`3` Cave、`4` Ratio；仅值改变时执行完整 mode change |
| `mix` | 有限数裁剪到 `0..1` | 否 | 立即写对应侧 DSP；网页输入步长 `0.001`，固件不额外按步长量化 |
| `decay` | 有限数裁剪到 `0.1..10` | 否 | 立即写对应侧 DSP；网页输入步长 `0.01`，固件不额外按步长量化 |
| `volume` | 有限数裁剪到 `0..1` | 否 | 立即写对应侧 DSP；网页输入步长 `0.001`，固件不额外按步长量化 |
| `threshold` | 裁剪到 `0.0825..0.99`，再按以 `0.0825` 为起点的 `0.0825` 步长量化 | 是 | 立即写对应侧 threshold |

通过 `set_param` 写入任一侧 Mix/Decay/Volume 后，该参数的共享实体旋钮进入接管等待；实体旋钮
下一次移动后，重新控制左右两侧。Mix/Decay/Volume 不设置持久化 dirty，Save 不写入它们；当前
网页不提供这些 API 参数的编辑控件。

### Shared 参数

| `name` | 固件范围与 canonicalization | Save 持久化 | `caves_changed` |
| --- | --- | --- | --- |
| `a3_hz` | `358.08..521.91 Hz`，`0.01 Hz` | 是 | alternate tuning 启用时，只有 Cave 频率实际变化超过 `0.0051 Hz` 才为 `true` |
| `tuning` | 四舍五入为整数后裁剪到 `-1..7` | 是 | tuning 选择实际改变时为 `true` |
| `pre_clip_gain` | `0.0825..0.99`，以 `0.0825` 为起点、步长 `0.0825` | 是 | `false` |
| `post_clip_gain` | `0.385..0.99`，以 `0.385` 为起点、步长 `0.055` | 是 | `false` |
| `midi_left` | 四舍五入为整数后裁剪到 `1..16` | 是 | `false` |
| `midi_right` | 四舍五入为整数后裁剪到 `1..16` | 是 | `false` |
| `midi_both` | 四舍五入为整数后裁剪到 `1..16` | 是 | `false` |

更改 A3 会立即重算当前 pitched modes；alternate tuning 启用时还会重算 Cave。更改 tuning 会在
Standard 与 alternate tuning 的 Cave 数据之间切换，并更新当前 pitched modes。

`set_param.dirty` 与 `get_settings.dirty` 使用同一套一般设置 dirty 定义。不属于
`left`/`right`/`shared` 的 target 在 parser 阶段返回 `invalid_json`；合法 target 下未实现的 name
返回 `invalid_parameter`。这两类错误都不会写入运行状态或 Preferences；canonical value 未变化
的持久化参数也不会额外置 dirty。

## Ratio 与 Cave 原操作

Schema 3 保留原有 Ratio/Cave 命令及完整 snapshot 写入语义。

### Ratio

- `get` 返回当前 `profile`、`factory_profile` 和 limits；
- `set` 接受恰好 9 个 Ratio，范围 `0.125..32.000`、步长 `0.001`；
- `set` 可带 `expected_revision`，冲突返回 `revision_conflict`；
- 成功 `set` 先完整校验，再替换运行 profile、递增 revision、标记 Ratio dirty 并应用到当前
  Ratio Mode channel；
- `reset` 只把运行 Ratio profile 恢复为 factory values，递增 revision 并标记 dirty；
- `reset` 与 `set` 一样检查可选的 `expected_revision`；
- `set` 与 `reset` 成功响应为 `state: "queued"` 与新 revision；
- Ratio 的实际 DSP frequency 边界为 `16..16000 Hz`。

### Cave

- `get_cave` 读取指定 `side` (`left`/`right`) 与 `bank` (`0..2`)；
- 响应包含 9 个 frequency、9 个 mute、active、revision、dirty 和 limits；
- frequency 范围 `16.00..16000.00 Hz`，步长 `0.01 Hz`；
- `set_cave` 必须提供完整的 9 个 frequency 与 9 个 mute，可带 `expected_revision`；
- 成功 `set_cave` 先完整校验，再替换该 bank、递增 revision 并标记 Cave dirty；active bank
  立即应用到 DSP，inactive bank 保留到其成为 active；
- Cave frequency 与 mute 都由 Save 持久化。

### `status`

`status` 保留为只读命令，返回左右 mode、当前 note、当前 fundamental、Ratio profile revision
和左右 active Cave bank，供其他客户端使用；当前网页不请求或显示该运行状态快照。

Ratio dirty 与每个 Cave bank dirty 独立于 `get_settings.dirty`，分别由 `get` 与 `get_cave`
返回。

## Save 与持久化边界

所有编辑命令先更新运行状态，不写 Preferences；协议没有 Apply。网页 Save 会先 flush 并等待
所有已排队写入，再发送：

```json
{"v":1,"id":4,"op":"save"}
```

Save 持久化：

- 左右 Mode 与 Threshold；
- A3、Tuning、Pre Clip Gain、Post Clip Gain；
- MIDI Left、Right、Both channel；
- 共享 Ratio profile；
- 左右 3 个 Cave bank 的 frequency 与 mute；
- alternate tuning 使用的 unquantized Cave backup 与相关 metadata。

Save 不持久化左右 Mix、Decay、Volume，也不改变这些运行值。

网页 Save 与实体双 Mode 键触发的 Save 都由 `loopTask` 串行执行；control task 只提交保存请求，
不会在禁中断的实体扫描区直接打开 Preferences。设备初始化完成前也不会处理 MIDI，因此启动期
Preferences 加载不会与 MIDI tuning 的备份写入并发。

启用 alternate tuning 时，Save 先写 unquantized Cave backup 与 tuning metadata，再写当前
quantized Cave；Standard tuning 则先写当前 Cave，再写 tuning metadata。这样若写入中断，重启时
优先保留足以重建当前 tuning 状态的数据；Save 仍会以 dirty 检查报告任何未完成项目。

成功响应：

```json
{"v":1,"id":4,"ok":true,"op":"save","state":"saved"}
```

写入失败或保存结束时仍存在任何应持久化 dirty，响应返回 `save_failed`。失败项目恢复 dirty，
运行值保持不变，可再次 Save。

## 明确未实现的同步机制

Schema 3 不包含以下机制：

- event 或 changed notification；
- heartbeat；
- 后台自动轮询；
- 设备主动向网页推送参数变化；
- `config_runtime` 或持续运行态快照；
- Apply；
- 单个 Ratio/Cave voice 的增量命令。

网页与设备之间的持续动作只有用户编辑触发的单向写入、写入 ACK，以及用户主动 Refresh 的完整
读取。连接时的初始读取、`caves_changed` 后的因果读取和手动 Refresh 是仅有的配置读取时机。

## 当前错误响应

错误 envelope：

```json
{"v":1,"id":3,"ok":false,"error":{"code":"invalid_parameter","field":"unknown_name"}}
```

当前实现会返回的 code 包括：

- `invalid_json`；
- `unsupported_version`；
- `unknown_operation`；
- `frame_too_large`；
- `busy`；
- `revision_conflict`；
- `invalid_ratio`；
- `invalid_cave_bank`；
- `invalid_parameter`；
- `save_failed`；
- `response_too_large`。

解析、校验和 revision 错误不会改变运行状态或 Preferences。`save_failed` 表示一次明确请求的
Save 没有全部完成；其中已经成功的项目可能已经写入，失败或并发变化的项目保持 dirty，供再次
Save。
