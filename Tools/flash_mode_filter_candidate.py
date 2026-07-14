#!/usr/bin/env python3
"""Safely install, inspect, and flash Wingie2 mode-filter images."""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from typing import Any, Iterable


APP_ADDRESS = 0x10000
APP_MAX_SIZE = 0x140000
DEFAULT_MANIFEST = Path(__file__).with_name("mode_filter_candidates.json")
DEFAULT_CACHE = Path(
    os.environ.get(
        "WINGIE2_MODE_FILTER_CACHE",
        "~/.wingie2/mode-filter-firmware",
    )
).expanduser()
PORT_PATTERNS = (
    "/dev/cu.usbserial-*",
    "/dev/cu.SLAB_USBtoUART*",
    "/dev/cu.wchusbserial*",
    "/dev/cu.usbmodem*",
)


class FlashError(RuntimeError):
    pass


def parse_int(value: int | str) -> int:
    return value if isinstance(value, int) else int(value, 0)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise FlashError(f"无法读取候选清单 {path}: {exc}") from exc

    if data.get("schema_version") != 1:
        raise FlashError("候选清单 schema_version 必须为 1")
    flash = data.get("flash", {})
    if parse_int(flash.get("app_address", -1)) != APP_ADDRESS:
        raise FlashError("安全边界错误：app_address 必须是 0x10000")
    candidates = data.get("candidates")
    if not isinstance(candidates, list):
        raise FlashError("候选清单缺少 candidates 数组")
    ids = [candidate.get("id") for candidate in candidates]
    if any(not isinstance(candidate_id, str) for candidate_id in ids):
        raise FlashError("每个候选都必须有字符串 id")
    if len(ids) != len(set(ids)):
        raise FlashError("候选 id 不能重复")
    return data


def find_candidate(manifest: dict[str, Any], candidate_id: str) -> dict[str, Any]:
    for candidate in manifest["candidates"]:
        if candidate["id"] == candidate_id:
            return candidate
    known = ", ".join(candidate["id"] for candidate in manifest["candidates"])
    raise FlashError(f"未知候选 {candidate_id!r}；可选：{known}")


def candidate_path(candidate: dict[str, Any], cache_dir: Path) -> Path:
    artifact = candidate.get("artifact")
    if not artifact:
        raise FlashError(f"{candidate['id']} 没有可烧录镜像：{candidate['note']}")
    if Path(artifact).name != artifact:
        raise FlashError(f"候选 {candidate['id']} 的 artifact 必须是单个文件名")
    return cache_dir / artifact


def verify_candidate(candidate: dict[str, Any], path: Path) -> str:
    if candidate.get("availability") != "available":
        raise FlashError(f"{candidate['id']} 不可用：{candidate['note']}")
    if not path.is_file():
        raise FlashError(
            f"缺少 {candidate['id']} 镜像：{path}\n"
            f"使用 install {candidate['id']} <已验证的 bin> 安装到缓存。"
        )
    actual_size = path.stat().st_size
    expected_size = candidate.get("size")
    if actual_size != expected_size:
        raise FlashError(
            f"{candidate['id']} 大小不符：期望 {expected_size}，实际 {actual_size}"
        )
    if actual_size > APP_MAX_SIZE:
        raise FlashError(f"{candidate['id']} 超过 app0 边界 0x140000 bytes")
    actual_hash = sha256_file(path)
    if actual_hash != candidate.get("sha256"):
        raise FlashError(
            f"{candidate['id']} SHA-256 不符：\n"
            f"  期望 {candidate.get('sha256')}\n"
            f"  实际 {actual_hash}"
        )
    return actual_hash


def command_text(command: Iterable[str]) -> str:
    return shlex.join(str(part) for part in command)


def run_command(command: list[str], dry_run: bool) -> None:
    print(f"$ {command_text(command)}", flush=True)
    if dry_run:
        return
    try:
        subprocess.run(command, check=True)
    except OSError as exc:
        raise FlashError(f"无法执行 {command[0]}: {exc}") from exc
    except subprocess.CalledProcessError as exc:
        raise FlashError(f"命令失败，退出码 {exc.returncode}: {command_text(command)}") from exc


def executable_prefix(value: str) -> list[str]:
    path = Path(value).expanduser()
    if path.parent != Path(".") or "/" in value:
        if not path.is_file():
            raise FlashError(f"找不到 esptool：{path}")
        if os.access(path, os.X_OK):
            return [str(path)]
        return [sys.executable, str(path)]
    located = shutil.which(value)
    if not located:
        raise FlashError(f"PATH 中找不到 esptool：{value}")
    return [located]


def discover_esptool(explicit: str | None) -> list[str]:
    if explicit:
        return executable_prefix(explicit)
    from_environment = os.environ.get("ESPTOOL")
    if from_environment:
        return executable_prefix(from_environment)
    for name in ("esptool.py", "esptool"):
        located = shutil.which(name)
        if located:
            return [located]

    arduino_tools = sorted(
        glob.glob(
            str(
                Path.home()
                / "Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool"
            )
        ),
        reverse=True,
    )
    if arduino_tools:
        return executable_prefix(arduino_tools[0])
    raise FlashError("找不到 esptool；请使用 --esptool 指定路径")


def resolve_port(explicit: str | None) -> str:
    if explicit:
        if not Path(explicit).exists():
            raise FlashError(f"串口不存在：{explicit}")
        return explicit
    matches = sorted(
        {
            port
            for pattern in PORT_PATTERNS
            for port in glob.glob(pattern)
            if Path(port).exists()
        }
    )
    if not matches:
        raise FlashError("没有找到 Wingie2 串口；请使用 --port 指定")
    if len(matches) > 1:
        raise FlashError(
            "发现多个可能串口，拒绝猜测：\n  "
            + "\n  ".join(matches)
            + "\n请使用 --port 指定。"
        )
    return matches[0]


def esptool_offline(prefix: list[str], chip: str, command: list[str]) -> list[str]:
    return prefix + ["--chip", chip] + command


def esptool_connected(
    prefix: list[str], chip: str, port: str, baud: int, command: list[str]
) -> list[str]:
    return prefix + [
        "--chip",
        chip,
        "--port",
        port,
        "--baud",
        str(baud),
        "--after",
        "hard_reset",
    ] + command


def image_info(prefix: list[str], chip: str, path: Path, dry_run: bool) -> None:
    run_command(
        esptool_offline(prefix, chip, ["image_info", str(path)]),
        dry_run,
    )


def command_list(manifest: dict[str, Any], cache_dir: Path) -> int:
    print("ID\t缓存状态\t测试状态\t方案")
    for candidate in manifest["candidates"]:
        if candidate.get("availability") != "available":
            cache_state = "不可用"
        else:
            path = candidate_path(candidate, cache_dir)
            if not path.is_file():
                cache_state = "缺少镜像"
            else:
                try:
                    verify_candidate(candidate, path)
                except FlashError:
                    cache_state = "校验失败"
                else:
                    cache_state = "就绪/注意" if candidate.get(
                        "known_issue"
                    ) else "就绪"
        print(
            f"{candidate['id']}\t{cache_state}\t"
            f"{candidate['test_state']}\t{candidate['name']}"
        )
    return 0


def command_inspect(
    args: argparse.Namespace,
    manifest: dict[str, Any],
    cache_dir: Path,
) -> int:
    candidate = find_candidate(manifest, args.candidate)
    print(f"ID: {candidate['id']}")
    print(f"方案: {candidate['name']}")
    print(f"测试状态: {candidate['test_state']}")
    print(f"说明: {candidate['note']}")
    print(f"来源: {candidate['source']}")
    if candidate.get("availability") != "available":
        print("镜像: 不可用")
        return 0
    path = candidate_path(candidate, cache_dir)
    digest = verify_candidate(candidate, path)
    print(f"镜像: {path}")
    print(f"大小: {path.stat().st_size} bytes")
    print(f"SHA-256: {digest}")
    prefix = discover_esptool(args.esptool)
    image_info(prefix, manifest["flash"]["chip"], path, args.dry_run)
    return 0


def command_install(
    args: argparse.Namespace,
    manifest: dict[str, Any],
    cache_dir: Path,
) -> int:
    candidate = find_candidate(manifest, args.candidate)
    source = args.image.expanduser().resolve()
    digest = verify_candidate(candidate, source)
    prefix = discover_esptool(args.esptool)
    image_info(prefix, manifest["flash"]["chip"], source, args.dry_run)
    destination = candidate_path(candidate, cache_dir).expanduser().resolve()
    action = "将安装" if args.dry_run else "安装"
    print(f"{action} {candidate['id']} -> {destination}")
    if not args.dry_run:
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + ".partial")
        shutil.copyfile(source, temporary)
        os.replace(temporary, destination)
        verify_candidate(candidate, destination)
    print(f"SHA-256: {digest}")
    if args.dry_run:
        print("演练完成：未修改固件缓存。")
    return 0


def hardware_values(
    args: argparse.Namespace, manifest: dict[str, Any]
) -> tuple[list[str], str, str, int]:
    prefix = discover_esptool(args.esptool)
    chip = manifest["flash"]["chip"]
    port = resolve_port(args.port)
    baud = args.baud or int(manifest["flash"]["default_baud"])
    return prefix, chip, port, baud


def command_flash(
    args: argparse.Namespace,
    manifest: dict[str, Any],
    cache_dir: Path,
) -> int:
    candidate = find_candidate(manifest, args.candidate)
    path = candidate_path(candidate, cache_dir)
    digest = verify_candidate(candidate, path)
    print(f"候选状态：{candidate['test_state']}")
    print(f"候选说明：{candidate['note']}")
    prefix, chip, port, baud = hardware_values(args, manifest)
    image_info(prefix, chip, path, args.dry_run)
    run_command(
        esptool_connected(
            prefix,
            chip,
            port,
            baud,
            ["write_flash", "-z", hex(APP_ADDRESS), str(path)],
        ),
        args.dry_run,
    )
    if args.dry_run:
        print(f"演练完成：未读取或写入设备。候选 {candidate['id']} ({digest})")
    else:
        print(f"烧录完成并通过 esptool flash hash 校验：{candidate['id']} ({digest})")
    return 0


def add_hardware_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--port", help="串口；省略时只接受唯一自动匹配")
    parser.add_argument("--baud", type=int, help="esptool 波特率")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Wingie2 mode-filter 候选固件安全烧录工具",
        epilog=(
            "典型用法：先运行 list，再用 --dry-run flash <ID> 检查命令，"
            "最后用 flash <ID> 烧录。write_flash 会校验 flash hash，"
            "然后执行一次 hard reset 启动新固件。"
        ),
    )
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--esptool", help="esptool 或 esptool.py 路径")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="显示操作但不读取或写入设备，也不修改缓存",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("list", help="列出八个方案及本地镜像状态")

    inspect_parser = subparsers.add_parser("inspect", help="校验并查看一个镜像")
    inspect_parser.add_argument("candidate")

    install_parser = subparsers.add_parser("install", help="校验并安装镜像到缓存")
    install_parser.add_argument("candidate")
    install_parser.add_argument("image", type=Path)

    flash_parser = subparsers.add_parser("flash", help="烧录并校验候选 app0")
    flash_parser.add_argument("candidate")
    add_hardware_arguments(flash_parser)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        manifest = load_manifest(args.manifest.expanduser().resolve())
        cache_dir = args.cache_dir.expanduser().resolve()
        commands = {
            "list": command_list,
            "inspect": command_inspect,
            "install": command_install,
            "flash": command_flash,
        }
        command = commands[args.command]
        if args.command == "list":
            return command(manifest, cache_dir)
        return command(args, manifest, cache_dir)
    except FlashError as exc:
        print(f"错误：{exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
