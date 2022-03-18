# Wingie2

![Wingie2 Front Small](https://user-images.githubusercontent.com/4593629/158756306-aa6c1218-f6ec-44c0-8c54-b04b49531801.jpg)

**如何建立编程环境**（适用于想更改或编写固件的人，如果只想重刷固件请见右侧 Releases）

**How to build programming environment** (For those who want to modify or write their own firmware, if you just want to flash original firmware, see Releases on the right side)

[**中文**](https://github.com/mengqimusic/Wingie2#中文)

[**English**](https://github.com/mengqimusic/Wingie2#english)

## 中文

1. 下载 Arduino https://www.arduino.cc/en/software

2. 打开下列菜单，在“附加开发版管理器网址“中填入下面的链接 https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

![c1](https://user-images.githubusercontent.com/4593629/158773019-20344ef1-9385-4675-83df-5215cc4624f3.jpg)
![c2](https://user-images.githubusercontent.com/4593629/158773026-535a0e4e-a833-4929-b494-8c8754f4ec60.jpg)

3. 在 Arduino 软件中，打开菜单 工具-开发版-开发板管理器，搜索 ESP32 并安装。安装完毕之后选择 ESP32 Dev Module

![c3](https://user-images.githubusercontent.com/4593629/158773034-031a8ea6-4bb5-45df-b1db-b1e0d5831ea9.jpg)
![c4](https://user-images.githubusercontent.com/4593629/158773127-2696f499-18b2-4192-ad1d-3f975e0981c3.jpg)

4. 将“Libraries”中的内容放到 文稿/Arduino/Libraries 下面

5. 打开 Arduino 软件，点击菜单 项目->加载库->库管理器 搜索 MIDI, 找到 Francois Best 的库并安装

![c5](https://user-images.githubusercontent.com/4593629/158773259-5106c61e-e7c4-4058-86ce-c3d557acafad.jpg)
![c6](https://user-images.githubusercontent.com/4593629/158773832-44c7d6b6-0509-4fd3-877c-ded94093ca39.jpg)

6. 打开 Wingie2.ino。选择端口（cu.usbserial-xxxxx 或者 cu.SLAB_USBtoUART）。点击菜单 工具->Upload Speed 改成 460800，点击上传

## English

1. Download Arduino https://www.arduino.cc/en/software

2. Open the preferences and paste this web address in "Addtional Boards Manager URLs" https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

![c1](https://user-images.githubusercontent.com/4593629/158773019-20344ef1-9385-4675-83df-5215cc4624f3.jpg)
![c2](https://user-images.githubusercontent.com/4593629/158773026-535a0e4e-a833-4929-b494-8c8754f4ec60.jpg)

3. In Arduino software, open the menu Tools-Development Board-Board Manager, search for ESP32 and install

![c3](https://user-images.githubusercontent.com/4593629/158773034-031a8ea6-4bb5-45df-b1db-b1e0d5831ea9.jpg)
![c4](https://user-images.githubusercontent.com/4593629/158773127-2696f499-18b2-4192-ad1d-3f975e0981c3.jpg)

4. Put the files in "Libraries" under Documents/Arduino/Libraries

5. Open Arduino software, click menu Sketch->Include Library->Manage Libraries, search for MIDI, find the one by Francois Best and install

![c5](https://user-images.githubusercontent.com/4593629/158773259-5106c61e-e7c4-4058-86ce-c3d557acafad.jpg)
![c6](https://user-images.githubusercontent.com/4593629/158773832-44c7d6b6-0509-4fd3-877c-ded94093ca39.jpg)

6. Open Wingie2.ino. Choose port (cu.usbserial-xxxxx or cu.SLAB_USBtoUART). click menu Tools->Upload Speed, change it to 460800, hit upload

---

http://mengqimusic.com

*including a slightly modified library of mrmx's AW9523B library (source https://github.com/mrmx/AW9523B)
