# Wingie2 实时配置与网页刷机实施计划

## 配置切片

1. 扩展 host 数据合同：Ratio/Cave canonicalization、Cave centiHz storage、CRC、协议 patch 命令。
2. 建立统一 runtime setter，把硬件、MIDI、Web 的 Mode、实时滑杆、阈值、gain、A3、调律、
   MIDI channel、Ratio 与 Cave 汇入同一副作用路径。
3. 合并 dirty/revision event；以 boot id 分隔重启 epoch，Serial loop 独占协议输出，网页按
   global/resource revision 增量刷新。
4. 把 Cave RAM、alternate-tuning backup 和 Preferences 升级为 `0.01 Hz` V2，并保留旧 key fallback。
5. 统一硬件/Web Save；失败不清 dirty，移除调律变化中的隐式 flash 写入。
6. 将 Ratio LED 改为 `20 ms` 四色循环，并修正 Save 后 phase/timer。
7. 重做单文件配置页为左右实时布局、全产品设置、无 Apply、输入 clamp/quantize 与全量导入导出。
8. 运行 host、网页 mock、Arduino 完整编译；记录未执行的真机手感与迁移门禁，独立提交。

## 刷机切片

1. 建立独立 `wingie_flasher.html`，使用可注入 adapter 以便浏览器 mock，生产 adapter 固定
   `esptool-js 0.6.0` 与 MD5 实现。
2. 严格校验 manifest、四 offset、NVS preserve、SHA-256、芯片类型和 flash 参数。
3. normal path 不引用 `eraseFlash`，固定 `eraseAll:false`，逐段启用 ROM flash MD5。
4. 建立发布打包器，要求显式版本和四个构建输入，生成版本化目录、manifest、SHA 与双语说明。
5. 添加 image_info、partition、app size、offset、4 KiB NVS overlap、vendor SHA 和禁止 erase 自动测试。
6. 用可重复浏览器 mock 覆盖空白、v1.x、v3.1、当前、app 损坏、错误芯片、端口占用、ROM 失败、
   下载/SHA/写入/MD5 失败与重试。
7. 提供只创建 GitHub Draft 的手工脚本，不运行；报告硬件门禁 A/B，独立提交。
