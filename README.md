# Wingie2

![Wingie2 Front Small](https://user-images.githubusercontent.com/4593629/158756306-aa6c1218-f6ec-44c0-8c54-b04b49531801.jpg)

1. 下载 Arduino
https://www.arduino.cc/en/software

2. 打开下列菜单，在“附加开发版管理器网址“中填入下面的链接
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

3. 在 Arduino 软件中，打开菜单 工具-开发版-开发板管理器，搜索 ESP32 并安装。安装完毕之后选择 ESP32 Dev Module

4. 将“Libraries”中的内容放到 文稿/Arduino/Libraries 下面

5. 打开 Arduino 软件，点击菜单 项目->加载库->库管理器 搜索 MIDI Library, 找到 Francois Best 的库并安装

6. 打开 Wingie2.ino。选择端口（cu.usbserial-xxxxx 或者 cu.SLAB_USBtoUART）。点击菜单 工具->Upload Speed 改成 460800，点击上传

---


1. Download Arduino
https://www.arduino.cc/en/software

2. Open the preferences and paste this web address in "addtional development boards"
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

3. In Arduino software, open the menu Tools-Development Board-Board Manager, search for ESP32 and install

4. Put the files in "Libraries" under Documents/Arduino/Libraries

5. Open Arduino software, click menu Sketch->Include Library->Manage Libraries, search for MIDI Library, find the one by Francois Best and install

6. Open Wingie2.ino. Choose port (cu.usbserial-xxxxx or cu.SLAB_USBtoUART). click menu Tools->Upload Speed, change it to 460800, hit upload

---

http://mengqimusic.com

*including a slightly modified library of mrmx's AW9523B library (source https://github.com/mrmx/AW9523B)
