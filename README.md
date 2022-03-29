# Wingie2

![Wingie2 Front Small](https://user-images.githubusercontent.com/4593629/158756306-aa6c1218-f6ec-44c0-8c54-b04b49531801.jpg)

[**如何建立编程环境**](https://github.com/mengqimusic/Wingie2#中文)（适用于想更改或编写固件的人，如果只想重刷固件请见右侧 Releases）

[**How to build programming environment**](https://github.com/mengqimusic/Wingie2#english) (For those who want to modify or write their own firmware, if you just want to flash original firmware, see Releases on the right side)

## 编译过程 Compiling

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

3. In Arduino software, open the menu Tools-Development Board-Board Manager, search for ESP32 and install

![c3](https://user-images.githubusercontent.com/4593629/158773034-031a8ea6-4bb5-45df-b1db-b1e0d5831ea9.jpg)
![c4](https://user-images.githubusercontent.com/4593629/158773127-2696f499-18b2-4192-ad1d-3f975e0981c3.jpg)

4. Put the files in "Libraries" under Documents/Arduino/Libraries

5. Open Arduino software, click menu Sketch->Include Library->Manage Libraries. Install MIDI Library by Francois Best and Adafruit AW9523 Library

![c5](https://user-images.githubusercontent.com/4593629/158773259-5106c61e-e7c4-4058-86ce-c3d557acafad.jpg)
![c6](https://user-images.githubusercontent.com/4593629/158773832-44c7d6b6-0509-4fd3-877c-ded94093ca39.jpg)

6. Open Wingie2.ino. Choose port (cu.usbserial-xxxxx or cu.SLAB_USBtoUART). click menu Tools->Upload Speed, change it to 460800, hit upload

---

http://mengqimusic.com

Including a part of the I2C Device Library http://i2cdevlib.com/
