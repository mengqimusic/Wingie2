# 6d78147 模式 Wet 与 WDT 修复结果

## 结论

修复分支从 `6d78147` 建立。原固件的四个逻辑模式保持不变，但持续 DSP 从四个物理
source 收敛为 Poly 与 programmable modal bank 两个 source。String 与 Bar 的九个频率改为
仅在音符、调律或模式事件发生时由固件计算并写入 modal bank。

原 `6d78147` 的四-source + 逐模态反啸叫图在完整固件中约 13 秒触发 CPU0 IDLE WDT。
修复后的生成 DSP 与已验证的 `e1a0120` 两-source 文件逐字一致，保留逐模态反啸叫控制。

## Wet 修复

- Cave bank 仅在进入 Cave 或 octave 实际变化时写入，不再在每轮 control loop 重写。
- Cave 保留各 bank 的九个 mute 状态。
- Poly、String、Bar 进入时统一清除共享 modal bank 的九个 mute。
- String 与 Bar 继续使用原有频率公式及 standard/alternate tuning。

## 验证

- DSP、反啸叫与模式映射 Python 测试：14 PASS。
- Tap Sequence host test：PASS。
- ESP32 anti-feedback benchmark sketch：完整编译通过。
- Arduino Core `2.0.4-cn` 完整产品编译通过：program `1,188,613 B`，RAM `50,692 B`。
- App 大小 `1,194,384 B`，SHA-256：
  `2826ffdf2f86eb0a4dd8a19a9e8a48b6cfc0c5acb3bbe9c76241b08607c1e1cb`。
- Standalone 发布包四镜像及全部发布文件 SHA-256：PASS。
- 用户真机确认：持续切换模式未出现 WDT，左右 `Cave → Poly` 后 wet 正常恢复。
