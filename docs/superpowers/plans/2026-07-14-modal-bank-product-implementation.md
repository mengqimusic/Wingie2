# Modal Bank 产品实施计划

最终选型理由与替代方案取舍见
`docs/superpowers/results/2026-07-14-mode-filter-decision.md`。

## 目标

用已选定的全系数 block-rate rotation-damping modal bank 替换
`pm.modeFilter`，解决高速换音和长 Decay 下的递归状态失稳，同时保持现有
Poly、String、Bar、Cave、alternate tuning、mute 和 wet/dry 控制接口。

本次不实现自由 ratio 或反啸叫控制，也不为它们增加 UI、参数或隐藏状态。

## 产品实现

每个共鸣器使用两个逐 sample 状态：

```text
z[n + 1] = rho * R(theta) * z[n] + B * x[n]
y[n]     = (2 / rho) * q[n]
```

- `rho`、`sin(theta)`、`cos(theta)` 和 `2/rho` 每 32 samples 更新一次。
- 频率限制在 `[16, 16000] Hz`；换频保留状态并在下一 block 生效。
- 9 个共鸣器彼此独立，共享现有通道 Decay 和输入。
- mute 只控制现有输出包络，不 reset 共鸣器状态。
- mode-change 包络保持左右独立的 `/Wingie/left/mode_changed` 和
  `/Wingie/right/mode_changed` 路径。

## 文件

- 修改 `Wingie2.dsp`：安装 modal bank 并修正左右 mode-change 分组。
- 同批再生成 `Wingie2/Wingie2.cpp` 和 `Wingie2/Wingie2.h`。
- 不更新发布产物 `Wingie2.zip`。

## 验证

1. 用固定 Faust 2.59.6 生成完整 alternate-tuning 产品。
2. 校验生成结构为 18 个二维状态、36 个乘 `rho` 的更新，且逐 sample
   路径没有 `pow`、`sin`、`cos`、`sqrt` 或 direct-form `tf2`。
3. 校验左右 `mode_changed`、Decay、mute 和 Cave 参数路径仍与固件一致。
4. 运行边界、重复、交叉、逆序和 ratio-jump contraction 测试。
5. 使用 ESP32 Arduino Core 2.0.4 完整编译固件。
6. 将编译与真机 smoke test 分开记录；真机至少覆盖左右 wet、快速换音、
   长 Decay、mode change 和 dry/thru。

## 提交边界

计划、产品实现和后续反啸叫计划分别提交。产品实现提交只包含 DSP source 与
同批 generated files，不夹带旧候选工具、实验门禁或未来功能。
