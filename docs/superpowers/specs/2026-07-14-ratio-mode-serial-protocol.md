# Ratio Mode Web Serial 协议

## 状态

这是 Ratio Mode 的第一版串口协议草案，供固件和单文件 HTML 共用。协议不依赖
SoftAP、HTTP server、Python bridge 或网页构建工具。Ratio 的最终数值范围、默认 profile
和 Preferences 字节布局仍由实施计划前的设计门确认。

## 传输

- USB Serial，波特率 `115200`；
- UTF-8，单个 JSON object 占一行，行尾接受 `LF` 或 `CRLF`；
- 浏览器只发送以 `@` 开头的请求行；
- 固件只发送以 `<` 开头的协议响应行；
- 其他串口输出视为调试日志，网页忽略；
- 单行长度上限为 `512` bytes，超过上限返回 `frame_too_large`；
- `id` 为请求方生成的无符号整数，同一连接内不得重复使用未完成的 id。

请求和响应的共同 envelope：

```text
@{"v":1,"id":17,"op":"get"}\n
<{"v":1,"id":17,"ok":true,"op":"get",...}\n
```

协议不允许音频 task 直接读取或写入串口。串口 parser 校验完成后，把配置 snapshot
交给 control task；DSP 在下一个 audio block 读取完整 snapshot。

## 握手

网页打开端口后，先发送：

```json
{"v":1,"id":1,"op":"hello"}
```

固件响应：

```json
{
  "v": 1,
  "id": 1,
  "ok": true,
  "op": "hello",
  "device": "Wingie2",
  "capabilities": ["ratio_mode"],
  "config_schema": 1,
  "transport": {"baud": 115200, "max_frame": 512}
}
```

若 `v` 或 `config_schema` 不兼容，网页停止写入，只显示协议版本错误。

## 配置对象

Ratio Mode 使用一个左右共用的 profile：

```json
{
  "ratios": [1, 2, 3, 4, 5, 6, 7, 8, 9],
  "revision": 4,
  "dirty": false
}
```

`ratios` 必须恰好有 9 个有限正数。最终的 `min`、`max` 和 `step` 不写死在网页中，
由 `get` 返回的 `limits` 元数据提供；网页只负责展示和输入，固件是唯一校验者。

`revision` 在每次成功的 `set`、`reset` 或 `save` 后递增。网页可在 `set` 中携带
`expected_revision`，用于拒绝两个配置页面同时覆盖彼此修改的情况。

## 命令

### `get`

读取当前运行 profile、工厂 profile 和参数限制：

```json
{"v":1,"id":2,"op":"get"}
```

```json
{
  "v": 1,
  "id": 2,
  "ok": true,
  "op": "get",
  "profile": {"ratios": [1,2,3,4,5,6,7,8,9], "revision": 4, "dirty": false},
  "factory_profile": {"ratios": [1,2,3,4,5,6,7,8,9]},
  "limits": {"min": 0.125, "max": 32, "step": 0.001}
}
```

示例中的限制值是协议形状，不冻结产品参数；实现前必须由设计决定替换或确认。

### `set`

替换整个运行 profile，不写 Preferences：

```json
{
  "v": 1,
  "id": 3,
  "op": "set",
  "expected_revision": 4,
  "ratios": [0.5, 1, 1.5, 2, 2.5, 3, 4, 5, 7]
}
```

固件必须先完整校验 9 个值，再将 snapshot 排队给 control task。任何一个值失败，
整个 profile 不改变；成功后运行值立即标记 dirty，并在下一个 audio block 应用。

### `save`

将当前运行 profile 写入 Preferences：

```json
{"v":1,"id":4,"op":"save"}
```

只有 `save` 可以写 flash。返回成功前必须完成完整 blob 写入和校验；写入失败时运行值
保持不变，响应返回 `save_failed`。

### `reset`

将运行 profile 恢复到 factory profile，但不写 Preferences：

```json
{"v":1,"id":5,"op":"reset"}
```

网页需要用户再次执行 `save`，才把 factory profile 持久化。

### `status`

读取网页预览所需的当前状态，不改变声音：

```json
{"v":1,"id":6,"op":"status"}
```

响应包含左右模式、当前 note、当前 fundamental 和 profile revision。若某项在固件中
暂时不可用，返回 `null`，网页显示 unknown，不自行猜测：

```json
{
  "v": 1,
  "id": 6,
  "ok": true,
  "op": "status",
  "mode": {"left": 4, "right": 4},
  "note": {"left": 60, "right": 60},
  "fundamental_hz": {"left": 261.626, "right": 261.626},
  "profile_revision": 5
}
```

Profile 的导出、导入和本地历史由 HTML 在浏览器中完成，不增加设备命令。

## 错误

错误仍使用与请求相同的 `id`：

```json
{
  "v": 1,
  "id": 3,
  "ok": false,
  "error": {
    "code": "invalid_ratio",
    "field": "ratios[2]",
    "message": "ratio is outside the device limits"
  }
}
```

首版错误码：

- `invalid_json`；
- `unsupported_version`；
- `unknown_operation`；
- `frame_too_large`；
- `invalid_profile`；
- `invalid_ratio`；
- `revision_conflict`；
- `save_failed`；
- `busy`。

错误不会隐式裁剪、mute、reset 或写 Preferences。

## HTML 端连接生命周期

1. 用户点击“连接设备”；
2. HTML 调用 `navigator.serial.requestPort()`，用户选择 USB 串口；
3. 以 `115200` 打开端口并发送 `hello`；
4. `hello` 成功后读取 `get`，用设备返回的 `limits` 建立输入约束；
5. 编辑时发送完整 `set`，保存时单独发送 `save`；
6. 断开时停止写入，未保存的 dirty 状态只留在设备 RAM 中。

网页必须提供明确的 unsupported-browser、permission-denied、disconnect 和 malformed
response 状态，不把串口沉默失败解释为设备没有 Ratio Mode。

## 测试边界

- host parser 测试覆盖 envelope、CRLF、垃圾日志、重复 id、半包和超长行；
- profile validator 测试覆盖少于/多于 9 个值、NaN/Inf、越界、revision conflict 和
  全量原子替换；
- HTML 手测覆盖连接、读取、编辑、reset、save、断开和重新连接；
- 固件验证覆盖 set 后下一个 block 生效、save 后重启持久化、失败 set 不改变运行值；
- 不用 Web Serial 页面替代 Arduino CLI 编译，也不用网页成功替代真机音频 smoke test。
