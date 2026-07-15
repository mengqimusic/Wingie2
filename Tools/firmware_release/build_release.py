#!/usr/bin/env python3

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


APP_OFFSET = 0x10000
APP_MAX_SIZE = 0x140000
BOOT_APP0_SHA256 = "f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0"
ESPTOOL_JS_VERSION = "0.6.0"
ESPTOOL_BUNDLE_SHA256 = "7c361337d5bba7271cb0d9741f165a3b87137ff9284c13f112a6e197c48cd0da"
MD5_SCRIPT_SHA256 = "6164d009d3fcf65edd5c47c4b76a0d0580dea4bce929eec89bec744fdec10e15"
FLASH_SECTOR_SIZE = 0x1000
NVS_OFFSET = 0x9000
NVS_SIZE = 0x5000
PACKAGE_PREFIX = "wingie2-firmware-"
VERSION_PATTERN = re.compile(r"^[0-9A-Za-z][0-9A-Za-z._+-]*$")


EXPECTED_PARTITIONS = (
    ("nvs", "data", "nvs", 0x9000, 0x5000, ""),
    ("otadata", "data", "ota", 0xE000, 0x2000, ""),
    ("app0", "app", "ota_0", 0x10000, 0x140000, ""),
    ("app1", "app", "ota_1", 0x150000, 0x140000, ""),
    ("spiffs", "data", "spiffs", 0x290000, 0x170000, ""),
)


@dataclass(frozen=True)
class ReleaseInputs:
    build_dir: Path
    boot_app0: Path
    version: str
    esptool_bundle: Path
    md5_script: Path
    flasher_page: Path
    esptool: Path
    gen_esp32part: Path
    output_root: Path


@dataclass(frozen=True)
class FlashPart:
    name: str
    source: Path
    filename: str
    offset: int
    maximum_size: int


class ReleaseError(RuntimeError):
    pass


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_file(path, label):
    if not path.is_file():
        raise ReleaseError(f"{label} 不存在或不是文件：{path}")
    if path.stat().st_size == 0:
        raise ReleaseError(f"{label} 是空文件：{path}")


def run_validator(command, label):
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode != 0:
        details = result.stderr.strip() or result.stdout.strip() or "没有错误输出"
        raise ReleaseError(f"{label}失败：{details}")
    return result.stdout


def validate_image(esptool, image, label, require_validation_hash=False):
    output = run_validator(
        [sys.executable, str(esptool), "--chip", "esp32", "image_info", str(image)],
        f"{label} image_info 校验",
    )
    if "(invalid)" in output.lower():
        raise ReleaseError(f"{label} image_info 报告镜像无效")
    if not re.search(r"Checksum:\s*[0-9a-fA-F]+\s*\(valid\)", output):
        raise ReleaseError(f"{label} image_info 未报告有效 checksum")
    if require_validation_hash and not re.search(
        r"Validation Hash:\s*[0-9a-fA-F]+\s*\(valid\)", output
    ):
        raise ReleaseError(f"{label} image_info 未报告有效 validation hash")


def parse_size(value):
    value = value.strip()
    multiplier = 1
    if value.upper().endswith("K"):
        multiplier = 1024
        value = value[:-1]
    elif value.upper().endswith("M"):
        multiplier = 1024 * 1024
        value = value[:-1]
    return int(value, 0) * multiplier


def parse_partition_output(output):
    partitions = []
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "," not in stripped:
            continue
        fields = [field.strip() for field in stripped.split(",")]
        if len(fields) != 6:
            raise ReleaseError(f"无法解析分区表输出：{line}")
        try:
            partitions.append(
                (
                    fields[0],
                    fields[1],
                    fields[2],
                    parse_size(fields[3]),
                    parse_size(fields[4]),
                    fields[5],
                )
            )
        except ValueError as error:
            raise ReleaseError(f"无法解析分区表数值：{line}") from error
    return tuple(partitions)


def validate_partitions(gen_esp32part, partition_image):
    output = run_validator(
        [sys.executable, str(gen_esp32part), str(partition_image)],
        "gen_esp32part 分区表校验",
    )
    actual = parse_partition_output(output)
    if actual != EXPECTED_PARTITIONS:
        raise ReleaseError(
            "分区表不是 Wingie2 要求的精确布局；拒绝生成可能覆盖 NVS 的发布包"
        )


def ranges_overlap(first_offset, first_size, second_offset, second_size):
    return first_offset < second_offset + second_size and second_offset < first_offset + first_size


def flash_span_size(size):
    return ((size + FLASH_SECTOR_SIZE - 1) // FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE


def validate_flash_parts(parts):
    regions = []
    for part in parts:
        size = part.source.stat().st_size
        if part.offset % FLASH_SECTOR_SIZE:
            raise ReleaseError(f"{part.name} offset 未按 4 KiB flash sector 对齐")
        if size > part.maximum_size:
            raise ReleaseError(
                f"{part.name} 镜像为 0x{size:x} bytes，超过 0x{part.maximum_size:x} bytes 边界"
            )
        span_size = flash_span_size(size)
        if ranges_overlap(part.offset, span_size, NVS_OFFSET, NVS_SIZE):
            raise ReleaseError(f"{part.name} 的 4 KiB 擦写范围与 0x9000 NVS 相交")
        regions.append((part.name, part.offset, span_size))

    for index, (name, offset, size) in enumerate(regions):
        for other_name, other_offset, other_size in regions[index + 1 :]:
            if ranges_overlap(offset, size, other_offset, other_size):
                raise ReleaseError(f"{name} 与 {other_name} 写入范围相交")


def validate_manifest(manifest):
    if set(manifest) != {
        "schema",
        "name",
        "version",
        "chipFamily",
        "esptoolJs",
        "flash",
        "preserve",
        "parts",
    }:
        raise ReleaseError("manifest 顶层字段不符合 schema 1")
    if manifest["schema"] != 1 or manifest["chipFamily"] != "ESP32":
        raise ReleaseError("manifest 芯片或 schema 错误")
    if manifest["esptoolJs"] != ESPTOOL_JS_VERSION:
        raise ReleaseError("manifest esptool-js 版本错误")
    if manifest["flash"] != {
        "mode": "dio",
        "frequency": "80m",
        "size": "4MB",
        "eraseAll": False,
    }:
        raise ReleaseError("manifest flash 参数错误或允许了整片擦除")
    if manifest["preserve"] != [
        {"name": "nvs", "offset": NVS_OFFSET, "size": NVS_SIZE}
    ]:
        raise ReleaseError("manifest 未精确保留 0x9000 NVS")

    expected_offsets = [0x1000, 0x8000, 0xE000, 0x10000]
    expected_names = ["bootloader", "partitions", "boot_app0", "app"]
    if [part.get("offset") for part in manifest["parts"]] != expected_offsets:
        raise ReleaseError("manifest 写入 offset 错误")
    if [part.get("name") for part in manifest["parts"]] != expected_names:
        raise ReleaseError("manifest part 顺序或名称错误")
    for part in manifest["parts"]:
        if set(part) != {"name", "path", "offset", "size", "sha256"}:
            raise ReleaseError("manifest part 字段不符合 schema 1")
        if not re.fullmatch(r"[0-9a-f]{64}", part["sha256"]):
            raise ReleaseError(f"manifest {part['name']} SHA256 格式错误")
        if part["offset"] % FLASH_SECTOR_SIZE:
            raise ReleaseError(f"manifest {part['name']} offset 未按 4 KiB 对齐")
        if ranges_overlap(
            part["offset"], flash_span_size(part["size"]), NVS_OFFSET, NVS_SIZE
        ):
            raise ReleaseError(f"manifest {part['name']} 的 4 KiB 擦写范围与 NVS 相交")


def chinese_instructions(version, package_parts):
    part_lines = "\n".join(
        f"- `0x{part.offset:x}`：`{part.filename}`" for part in package_parts
    )
    return f"""# Wingie2 {version} 网页刷机说明

本发布包面向桌面版 Chrome 或 Edge，并要求从 HTTPS 页面运行。刷机页直接连接 ESP32 ROM bootloader，不依赖 Wingie2 应用固件响应，因此支持空白 Flash、v1.0/v1.1、v3.1/当前固件，以及 app 损坏但 ROM bootloader 正常的设备。

## 安全边界

- 标准安装只写四个固定区域，不执行整片擦除。
- `0x9000` 开始的 20 KiB NVS 保持不变，MIDI、调律、Cave、Ratio 等设置不会被标准安装主动擦除。
- 标准刷机不会读取或备份设备当前 app0。
- 恢复出厂设置或擦除配置不属于此页面的标准流程。

## 写入布局

{part_lines}

## 安装步骤

1. 关闭正在使用 Wingie2 串口的配置页、MIDI 工具和串口终端。
2. 从 HTTPS 地址打开 `wingie_flasher.html`，点击“连接设备”。浏览器只会在用户选择端口后授权访问。
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
"""


def english_instructions(version, package_parts):
    part_lines = "\n".join(
        f"- `0x{part.offset:x}`: `{part.filename}`" for part in package_parts
    )
    return f"""# Wingie2 {version} Web Flasher Guide

This package supports desktop Chrome and Edge from an HTTPS page. The flasher connects directly to the ESP32 ROM bootloader and does not wait for a Wingie2 firmware greeting. It therefore supports blank flash, v1.0/v1.1, v3.1/current firmware, and a damaged app when the ROM bootloader still works.

## Safety boundaries

- Standard installation writes only four fixed regions and never performs a full-chip erase.
- The 20 KiB NVS region at `0x9000` is preserved, so the standard install does not intentionally erase MIDI, tuning, Cave, or Ratio settings.
- Standard flashing never reads or backs up the existing app0 image.
- Factory reset and configuration erasure are outside this page's standard workflow.

## Flash layout

{part_lines}

## Installation

1. Close every configuration page, MIDI utility, and serial terminal using the Wingie2 port.
2. Open `wingie_flasher.html` from its HTTPS address and select “Connect device”. The browser grants access only after you choose a port.
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
"""


def write_checksums(package_dir):
    files = sorted(
        path for path in package_dir.rglob("*") if path.is_file() and path.name != "SHA256SUMS.txt"
    )
    contents = "".join(
        f"{sha256_file(path)}  {path.relative_to(package_dir).as_posix()}\n" for path in files
    )
    (package_dir / "SHA256SUMS.txt").write_text(contents, encoding="utf-8")


def build_release(inputs):
    if not VERSION_PATTERN.fullmatch(inputs.version):
        raise ReleaseError("version 只能包含字母、数字、点、加号、减号和下划线")

    source_files = {
        "bootloader": inputs.build_dir / "Wingie2.ino.bootloader.bin",
        "partitions": inputs.build_dir / "Wingie2.ino.partitions.bin",
        "app": inputs.build_dir / "Wingie2.ino.bin",
    }
    required = {
        "bootloader 镜像": source_files["bootloader"],
        "partition 镜像": source_files["partitions"],
        "app 镜像": source_files["app"],
        "boot_app0 镜像": inputs.boot_app0,
        "esptool.py": inputs.esptool,
        "gen_esp32part.py": inputs.gen_esp32part,
        "刷机页面": inputs.flasher_page,
        "esptool-js bundle": inputs.esptool_bundle,
        "MD5 script": inputs.md5_script,
    }
    for label, path in required.items():
        require_file(path, label)

    boot_app0_hash = sha256_file(inputs.boot_app0)
    if boot_app0_hash != BOOT_APP0_SHA256:
        raise ReleaseError(
            "boot_app0 SHA256 与 ESP32 Arduino Core 2.0.4-cn 固定镜像不符"
        )
    if sha256_file(inputs.esptool_bundle) != ESPTOOL_BUNDLE_SHA256:
        raise ReleaseError("esptool-js bundle SHA256 与固定的 0.6.0 发布文件不符")
    if sha256_file(inputs.md5_script) != MD5_SCRIPT_SHA256:
        raise ReleaseError("js-md5 SHA256 与固定的 0.8.0 发布文件不符")

    validate_image(inputs.esptool, source_files["bootloader"], "bootloader")
    validate_image(
        inputs.esptool, source_files["app"], "app", require_validation_hash=True
    )
    validate_partitions(inputs.gen_esp32part, source_files["partitions"])

    version = inputs.version
    parts = (
        FlashPart(
            "bootloader",
            source_files["bootloader"],
            f"Wingie2-{version}.bootloader.bin",
            0x1000,
            0x7000,
        ),
        FlashPart(
            "partitions",
            source_files["partitions"],
            f"Wingie2-{version}.partitions.bin",
            0x8000,
            0x1000,
        ),
        FlashPart(
            "boot_app0",
            inputs.boot_app0,
            f"Wingie2-{version}.boot_app0.bin",
            0xE000,
            0x2000,
        ),
        FlashPart(
            "app",
            source_files["app"],
            f"Wingie2-{version}.app.bin",
            APP_OFFSET,
            APP_MAX_SIZE,
        ),
    )
    validate_flash_parts(parts)

    inputs.output_root.mkdir(parents=True, exist_ok=True)
    package_name = f"{PACKAGE_PREFIX}{version}"
    destination = inputs.output_root / package_name
    if destination.exists():
        raise ReleaseError(f"发布目录已存在，拒绝覆盖：{destination}")

    with tempfile.TemporaryDirectory(
        prefix=f".{package_name}.", dir=inputs.output_root
    ) as temporary:
        package_dir = Path(temporary) / package_name
        package_dir.mkdir()
        for part in parts:
            shutil.copyfile(part.source, package_dir / part.filename)

        shutil.copyfile(inputs.flasher_page, package_dir / "wingie_flasher.html")
        vendor_dir = package_dir / "vendor"
        vendor_dir.mkdir()
        shutil.copyfile(inputs.esptool_bundle, vendor_dir / "esptool-js.bundle.js")
        shutil.copyfile(inputs.md5_script, vendor_dir / "md5.min.js")

        manifest_parts = []
        for part in parts:
            packaged_image = package_dir / part.filename
            manifest_parts.append(
                {
                    "name": part.name,
                    "path": part.filename,
                    "offset": part.offset,
                    "size": packaged_image.stat().st_size,
                    "sha256": sha256_file(packaged_image),
                }
            )
        manifest = {
            "schema": 1,
            "name": "Wingie2",
            "version": version,
            "chipFamily": "ESP32",
            "esptoolJs": ESPTOOL_JS_VERSION,
            "flash": {
                "mode": "dio",
                "frequency": "80m",
                "size": "4MB",
                "eraseAll": False,
            },
            "preserve": [{"name": "nvs", "offset": NVS_OFFSET, "size": NVS_SIZE}],
            "parts": manifest_parts,
        }
        validate_manifest(manifest)
        (package_dir / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        (package_dir / "README.zh-CN.md").write_text(
            chinese_instructions(version, parts), encoding="utf-8"
        )
        (package_dir / "README.en.md").write_text(
            english_instructions(version, parts), encoding="utf-8"
        )
        write_checksums(package_dir)
        package_dir.rename(destination)

    return destination


def default_core_tool(boot_app0, filename):
    return boot_app0.parent.parent / filename


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(
        description="验证并生成 Wingie2 网页刷机发布目录"
    )
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--boot-app0", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--esptool-bundle", type=Path, required=True)
    parser.add_argument("--md5-script", type=Path, required=True)
    parser.add_argument(
        "--flasher-page",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "wingie_flasher.html",
    )
    parser.add_argument("--esptool", type=Path)
    parser.add_argument("--gen-esp32part", type=Path)
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "dist",
    )
    arguments = parser.parse_args(argv)
    if arguments.esptool is None:
        arguments.esptool = default_core_tool(arguments.boot_app0, "esptool.py")
    if arguments.gen_esp32part is None:
        arguments.gen_esp32part = default_core_tool(
            arguments.boot_app0, "gen_esp32part.py"
        )
    return ReleaseInputs(**vars(arguments))


def main(argv=None):
    try:
        destination = build_release(parse_arguments(argv))
    except (OSError, ReleaseError) as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1
    print(f"发布包已生成：{destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
