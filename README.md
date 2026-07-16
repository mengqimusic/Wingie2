# Wingie2

![Wingie2 Front Small](https://user-images.githubusercontent.com/4593629/158756306-aa6c1218-f6ec-44c0-8c54-b04b49531801.jpg)

[**如何建立编程环境**](https://github.com/mengqimusic/Wingie2#中文)（适用于想更改或编写固件的人，如果只想重刷固件请见右侧 Releases）

[**How to build programming environment**](https://github.com/mengqimusic/Wingie2#english) (For those who want to modify or write their own firmware, if you just want to flash original firmware, see Releases on the right side)

## Wingie2 USB 配置

[`Tools/wingie_config.html`](Tools/wingie_config.html) 是可直接部署或植入现有网站的单文件
配置页。CSS 与 JavaScript 全部内联，不需要 SoftAP、Wi‑Fi、CDN、Node 或 Python；浏览器
通过 USB Web Serial 与 Wingie2 通信。当前页面要求设备运行 `config schema 3` 固件，不兼容
旧的 schema 1 或 schema 2 配置固件。

1. 使用桌面版 Chrome、Edge 或其他支持 Web Serial 的 Chromium 浏览器，通过 HTTPS 打开页面；
2. 关闭 Arduino Serial Monitor 等占用 Wingie2 串口的软件；
3. 点击“连接 Wingie2”，选择对应的 USB 串口；
4. 连接成功后，页面会读取一次完整设备快照；有效编辑会立即写入设备并作用于当前运行状态，
   不需要 Apply；
5. 页面不会自动轮询设备。若实体控件、MIDI 或其他软件改变了设备，请点击 Refresh 重新读取完整快照；
6. 确认声音后点击“Save to Flash”并确认，才会写入 flash 并在重启后保留。

左右声道分别设置 Mode、Input Threshold、Mix、Decay 与 Volume。Mode 和 Input Threshold 可以
保存；Mix、Decay、Volume 只影响当前运行状态，不会写入 flash。Wingie2 上的 Mix、Decay、
Volume 是左右声道共用的实体旋钮，移动任一实体旋钮后，该旋钮会重新接管左右两侧的对应参数。

Save 会持久化 A3、Tuning、Pre/Post Clip Gain、三个 MIDI 通道路由、左右 Mode 与 Input
Threshold、共享 Ratio profile，以及左右各 3 个 Cave bank 的频率和 mute。Ratio profile 由
左右声道共用；Factory Ratio 和导入配置都先改变运行状态，仍需 Save。Cave 频率范围为
`16.00–16000.00 Hz`，以 `0.01 Hz` 为步进；页面不提供 Cave factory reset。

若页面放在 iframe 中，iframe 需要 `allow="serial"`，服务器也必须允许相应的
`Permissions-Policy: serial=(self)`。Safari、Firefox、移动浏览器和非安全 HTTP 页面不在
当前支持范围内。

## Wingie2 USB Configuration

[`Tools/wingie_config.html`](Tools/wingie_config.html) is a self-contained page that can be deployed
or embedded directly. It uses USB Web Serial and has no SoftAP, Wi-Fi, CDN, Node, or Python
dependency. The current page requires firmware that reports `config schema 3`; older schema 1 and
schema 2 configuration firmware is not compatible.

Open it from an HTTPS origin in a desktop Chromium browser, close any other software using the serial
port, and connect the Wingie2 port. The page reads one complete device snapshot when it connects.
Every valid edit is written to the running instrument immediately, with no Apply step. The page does
not poll the device: if hardware controls, MIDI, or other software changes the instrument, use Refresh
to read a new complete snapshot. Use the confirmed “Save to Flash” action only when the current
persistent settings should be written to flash.

Mode, Input Threshold, Mix, Decay, and Volume are independent for the left and right channels. Mode
and Input Threshold can be saved; Mix, Decay, and Volume are runtime-only. Moving one of Wingie2's
shared physical Mix, Decay, or Volume knobs takes over that parameter on both channels again.

Save persists A3, Tuning, Pre/Post Clip Gain, all three MIDI channel routes, left/right Mode and Input
Threshold, the shared Ratio profile, and all three Cave banks for each channel, including frequency
and mute. Factory Ratio and imported settings first change the running state and still require Save.
Cave frequencies cover `16.00–16000.00 Hz` in `0.01 Hz` steps. The page has no Cave factory reset.

For an iframe, add `allow="serial"` and serve a compatible
`Permissions-Policy: serial=(self)` header. Safari, Firefox, mobile browsers, and insecure HTTP
origins are not supported.

## 网页刷机 / Web Flasher

[`Tools/wingie_flasher.html`](Tools/wingie_flasher.html) 是独立的发布刷机页，不与配置页共用
Web Serial 状态机。它从 ESP32 ROM bootloader 识别芯片后，只写 `0x1000`、`0x8000`、
`0xe000`、`0x10000` 四段；标准流程不整片擦除、不读取 app0，也不写 `0x9000` NVS。
发布维护者使用 [`Tools/firmware_release/README.md`](Tools/firmware_release/README.md) 中的工具
验证镜像、生成 manifest、SHA256SUMS、中英文说明及内嵌全部固件的 standalone HTML。
Squarespace 应使用按钮打开部署在 GitHub Pages 等静态 HTTPS 主机上的 standalone 页面，
不要使用 iframe 或 Code Block。支持范围是 HTTPS 上的桌面 Chrome/Edge。

[`Tools/wingie_flasher.html`](Tools/wingie_flasher.html) is a separate release flasher. It identifies
the chip through the ESP32 ROM bootloader and writes only the four fixed images at `0x1000`, `0x8000`,
`0xe000`, and `0x10000`. The normal flow never performs a full-chip erase, reads app0, or writes the
NVS region at `0x9000`. See [`Tools/firmware_release/README.md`](Tools/firmware_release/README.md) for
the validated release-package workflow and generated single-file flasher. Squarespace should link to
the standalone page as a top-level HTTPS navigation rather than embedding an iframe. Desktop
Chrome/Edge over HTTPS is supported.

## 编译过程 Compiling

安装 [Faust](https://faustide.grame.fr/)  并将路径加入 PATH（osx）Install [Faust](https://faustide.grame.fr/)  and add it to PATH (osx) 

在编写完 .dsp 文件之后，使用 `faust2esp32 -ac101 -lib <Your File Name>.dsp` 命令编译 Faust .dsp 文件，解压所生成的 zip 文件，并将内容移至你的 Arduino 代码文件夹下。可以通过建立批处理文档将此过程自动化，以节省时间。下面的例子是 OSX 上的做法。

To compile Faust .dsp file, use the command `faust2esp32 -ac101 -lib <Your File Name>.dsp` then unzip the generated file and put them into your Arduino sketch folder. You can also automate this process by creating a batch processing file. Following is an example for OSX.


```
#!/bin/bash
cd "$(dirname "$BASH_SOURCE")" || {
    echo Error getting script directory >&2
    exit 1
}

faust2esp32 -ac101 -lib <Your File Name>.dsp

unzip -o <Your File Name>.zip

mv -f ./<Your File Name>/*.* ./<Your Arduino Sketch Folder>
```

在 Windows 上，可创建相同功能的 .bat 文件。

A .bat file of identical functions can be created for Windows.

## 如何建立编程环境 How to build programming environment

### 中文

1. 下载 Arduino https://www.arduino.cc/en/software

2. 打开下列菜单，在“附加开发版管理器网址“中填入下面的链接 https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

![c1](https://user-images.githubusercontent.com/4593629/158773019-20344ef1-9385-4675-83df-5215cc4624f3.jpg)
![c2](https://user-images.githubusercontent.com/4593629/158773026-535a0e4e-a833-4929-b494-8c8754f4ec60.jpg)

3. 在 Arduino 软件中，打开菜单 工具-开发版-开发板管理器，搜索 ESP32 并安装。安装完毕之后选择 ESP32 Dev Module

![c3](https://user-images.githubusercontent.com/4593629/158773034-031a8ea6-4bb5-45df-b1db-b1e0d5831ea9.jpg)
![c4](https://user-images.githubusercontent.com/4593629/158773127-2696f499-18b2-4192-ad1d-3f975e0981c3.jpg)

4. 将 “Libraries” 中的内容放到 文稿/Arduino/Libraries 下面

5. 打开 Arduino 软件，点击菜单 项目->加载库->库管理器。搜索并安装 Francois Best 的 MIDI Library 和 Adafruit AW9523 库

![c5](https://user-images.githubusercontent.com/4593629/158773259-5106c61e-e7c4-4058-86ce-c3d557acafad.jpg)
![c6](https://user-images.githubusercontent.com/4593629/158773832-44c7d6b6-0509-4fd3-877c-ded94093ca39.jpg)

6. 打开 Wingie2.ino。选择端口（cu.usbserial-xxxxx 或者 cu.SLAB_USBtoUART）。点击菜单 工具->Upload Speed 改成 460800，点击上传

### English

1. Download Arduino https://www.arduino.cc/en/software

2. Open the preferences and paste this web address in "Addtional Boards Manager URLs" https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

![c1](https://user-images.githubusercontent.com/4593629/158773019-20344ef1-9385-4675-83df-5215cc4624f3.jpg)
![c2](https://user-images.githubusercontent.com/4593629/158773026-535a0e4e-a833-4929-b494-8c8754f4ec60.jpg)

3. In Arduino software, open the menu Tools-Development Board-Board Manager, search for ESP32 and install (use version 2.0.4 -- this project does not work properly yet with version 2.0.5)

![c3](https://user-images.githubusercontent.com/4593629/158773034-031a8ea6-4bb5-45df-b1db-b1e0d5831ea9.jpg)
![c4](https://user-images.githubusercontent.com/4593629/158773127-2696f499-18b2-4192-ad1d-3f975e0981c3.jpg)

4. Copy the files in the "Libraries" directory of the project to Documents/Arduino/Libraries

5. Open Arduino software, click menu Sketch->Include Library->Manage Libraries. Install MIDI Library by Francois Best and Adafruit AW9523 Library

![c5](https://user-images.githubusercontent.com/4593629/158773259-5106c61e-e7c4-4058-86ce-c3d557acafad.jpg)
![c6](https://user-images.githubusercontent.com/4593629/158773832-44c7d6b6-0509-4fd3-877c-ded94093ca39.jpg)

6. Open Wingie2.ino. Choose port (on macOS, is should be named cu.usbserial-xxxxx or cu.SLAB_USBtoUART; on Windows, it will probably be named COMxxx). click menu Tools->Upload Speed, change it to 51200, hit upload

---

http://mengqimusic.com

Including a part of the I2C Device Library http://i2cdevlib.com/
