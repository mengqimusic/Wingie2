# Wingie2 v4.04 Web Flasher Guide

This package supports desktop Chrome and Edge from an HTTPS page. The flasher connects directly to the ESP32 ROM bootloader and does not wait for a Wingie2 firmware greeting. It therefore supports blank flash, v1.0/v1.1, v3.1/current firmware, and a damaged app when the ROM bootloader still works.

For normal users, open the single-file `Wingie2-v4.04.standalone.html`. It embeds the manifest, all four images, and the pinned browser dependencies, so there is no firmware package picker and no adjacent asset download. Squarespace should contain a button that opens this HTTPS page as a top-level page; do not place the flasher in an iframe or Code Block.

## Safety boundaries

- Standard installation writes only four fixed regions and never performs a full-chip erase.
- The 20 KiB NVS region at `0x9000` is preserved, so the standard install does not intentionally erase MIDI, tuning, Cave, or Ratio settings.
- Standard flashing never reads or backs up the existing app0 image.
- Factory reset and configuration erasure are outside this page's standard workflow.

## Flash layout

- `0x1000`: `Wingie2-v4.04.bootloader.bin`
- `0x8000`: `Wingie2-v4.04.partitions.bin`
- `0xe000`: `Wingie2-v4.04.boot_app0.bin`
- `0x10000`: `Wingie2-v4.04.app.bin`

## Installation

1. Close every configuration page, MIDI utility, and serial terminal using the Wingie2 port.
2. Open `Wingie2-v4.04.standalone.html` from its HTTPS address and select “Connect device”. The browser grants access only after you choose a port.
3. The page enters the ROM bootloader and confirms an ESP32 before enabling installation. A different chip must stop the process.
4. Confirm the version and four addresses, then start. Keep USB connected until all four images are written and verified.
5. Restart Wingie2 after success, then use the configuration page to check the firmware version and retained settings.

## Troubleshooting

- **Port busy**: close other configuration pages, Arduino Serial Monitor, and DAW/MIDI utilities, then retry.
- **No serial port**: use a USB data cable, try another port, and install the USB-to-serial driver required by the device.
- **Bootloader entry failed**: let the page try automatic DTR/RTS first. If it still fails, hold BOOT, tap RESET/EN, start connecting, and then release BOOT.
- **Wrong chip**: stop. This package targets the original ESP32, not ESP32-S2/S3/C3.
- **Write failed**: keep the cable connected and retry. If it repeats, change the USB cable or port. The standard flow will not use a full-chip erase as a workaround.

## Integrity check

Run `shasum -a 256 -c SHA256SUMS.txt` in the release directory. Use the package only when every entry reports `OK`. Automated tests do not replace the two hardware gates: first install on a fully erased Wingie2, and a v3.1 upgrade that confirms NVS settings remain intact.
