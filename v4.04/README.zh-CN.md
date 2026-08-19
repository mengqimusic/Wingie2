# Wingie2 v4.04 网页刷机说明

本发布包面向桌面版 Chrome 或 Edge，并要求从 HTTPS 页面运行。刷机页直接连接 ESP32 ROM bootloader，不依赖 Wingie2 应用固件响应，因此支持空白 Flash、v1.0/v1.1、v3.1/当前固件，以及 app 损坏但 ROM bootloader 正常的设备。

普通用户推荐直接打开单文件 `Wingie2-v4.04.standalone.html`。它已经内嵌 manifest、四段固件和固定版本浏览器依赖，不需要选择固件包，也不会下载同目录资源。Squarespace 只放一个跳转到该 HTTPS 页面的按钮；不要把刷机页放进 iframe 或 Code Block。

## 安全边界

- 标准安装只写四个固定区域，不执行整片擦除。
- `0x9000` 开始的 20 KiB NVS 保持不变，MIDI、调律、Cave、Ratio 等设置不会被标准安装主动擦除。
- 标准刷机不会读取或备份设备当前 app0。
- 恢复出厂设置或擦除配置不属于此页面的标准流程。

## 写入布局

- `0x1000`：`Wingie2-v4.04.bootloader.bin`
- `0x8000`：`Wingie2-v4.04.partitions.bin`
- `0xe000`：`Wingie2-v4.04.boot_app0.bin`
- `0x10000`：`Wingie2-v4.04.app.bin`

## 安装步骤

1. 关闭正在使用 Wingie2 串口的配置页、MIDI 工具和串口终端。
2. 从 HTTPS 地址打开 `Wingie2-v4.04.standalone.html`，点击“连接设备”。浏览器只会在用户选择端口后授权访问。
3. 页面先进入 ROM bootloader 并确认芯片为 ESP32；芯片不符时禁止继续。
4. 确认版本和四个写入地址后开始刷写。保持 USB 连接，直到四段写入和校验全部完成。
5. 页面提示成功后重新启动 Wingie2，再使用配置页检查固件版本和原有设置。

## 故障处理

- **端口占用**：关闭其他配置页、Arduino Serial Monitor、DAW/MIDI 工具后重试。
- **找不到串口**：更换可传输数据的 USB 线和 USB 端口，并安装设备所需的 USB 串口驱动。
- **无法进入 bootloader**：先让页面自动切换；仍失败时按住 BOOT，短按 RESET/EN，开始连接后再松开 BOOT。
- **错误芯片**：不要继续；本发布包只支持原始 ESP32，不支持 ESP32-S2/S3/C3。
- **写入失败**：不要拔线，先重试当前安装；重复失败时更换 USB 线或端口。标准流程不会通过整片擦除来绕过错误。

## 文件校验

在发布目录中运行 `shasum -a 256 -c SHA256SUMS.txt`。只有全部显示 `OK` 时才使用该发布包。自动化测试不能替代以下真机门禁：整片擦除设备的首次安装，以及从 v3.1 升级后确认 NVS 设置保留。
