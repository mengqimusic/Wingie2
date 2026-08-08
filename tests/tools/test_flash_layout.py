import importlib.util
import json
from pathlib import Path
import re
import shlex
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
LAYOUT_PATH = REPO_ROOT / "Tools/firmware_release" / "layout.json"
BUILD_RELEASE_PATH = REPO_ROOT / "Tools/firmware_release" / "build_release.py"
FLASH_FILTER_PATH = REPO_ROOT / "Tools/flash_mode_filter_candidate.py"
DRAFT_SCRIPT_PATH = REPO_ROOT / "Tools/firmware_release" / "create_github_draft.sh"
FLASHER_HTML_PATH = REPO_ROOT / "Tools/wingie_flasher.html"

EXPECTED_PARTITION_ROWS = (
    ("nvs", "data", "nvs", 0x9000, 0x5000, ""),
    ("otadata", "data", "ota", 0xE000, 0x2000, ""),
    ("app0", "app", "ota_0", 0x10000, 0x140000, ""),
    ("app1", "app", "ota_1", 0x150000, 0x140000, ""),
    ("spiffs", "data", "spiffs", 0x290000, 0x170000, ""),
)


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FlashLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.layout = json.loads(LAYOUT_PATH.read_text(encoding="utf-8"))
        cls.build_release = load_module("wingie2_firmware_release", BUILD_RELEASE_PATH)
        cls.flash_filter = load_module("wingie2_flash_mode_filter", FLASH_FILTER_PATH)

    def test_layout_json_has_complete_partition_and_release_truth(self):
        self.assertEqual(
            set(self.layout),
            {
                "partition_table",
                "nvs",
                "otadata",
                "app0",
                "app1",
                "spiffs",
                "release_files",
            },
        )
        for name in ("nvs", "otadata", "app0", "app1", "spiffs"):
            row = self.layout[name]
            self.assertEqual(row["name"], name)
            for field in ("type", "subtype", "offset", "size"):
                self.assertIn(field, row)
        self.assertEqual(
            set(self.layout["partition_table"]),
            {"offset", "size", "bootloader", "partition_bin"},
        )
        self.assertGreaterEqual(len(self.layout["release_files"]), 10)

    def test_layout_partition_rows_match_original_build_release_truth(self):
        for name, expected_type, expected_subtype, offset, size, _ in (
            EXPECTED_PARTITION_ROWS
        ):
            row = self.layout[name]
            self.assertEqual(row["type"], expected_type)
            self.assertEqual(row["subtype"], expected_subtype)
            self.assertEqual(int(row["offset"], 0), offset)
            self.assertEqual(int(row["size"], 0), size)

    def test_build_release_derives_constants_from_layout(self):
        release = self.build_release
        self.assertEqual(release.APP_OFFSET, int(self.layout["app0"]["offset"], 0))
        self.assertEqual(release.APP_OFFSET, 0x10000)
        self.assertEqual(release.APP_MAX_SIZE, int(self.layout["app0"]["size"], 0))
        self.assertEqual(release.APP_MAX_SIZE, 0x140000)
        self.assertEqual(release.NVS_OFFSET, int(self.layout["nvs"]["offset"], 0))
        self.assertEqual(release.NVS_OFFSET, 0x9000)
        self.assertEqual(release.NVS_SIZE, int(self.layout["nvs"]["size"], 0))
        self.assertEqual(release.NVS_SIZE, 0x5000)
        self.assertEqual(release.EXPECTED_PARTITIONS, EXPECTED_PARTITION_ROWS)
        self.assertEqual(
            release.EXPECTED_OFFSETS,
            (
                int(self.layout["partition_table"]["bootloader"], 0),
                int(self.layout["partition_table"]["offset"], 0),
                int(self.layout["partition_table"]["partition_bin"], 0),
                release.APP_OFFSET,
            ),
        )
        self.assertEqual(
            release.EXPECTED_OFFSETS, (0x1000, 0x8000, 0xE000, 0x10000)
        )

    def test_flash_mode_filter_derives_constants_from_layout(self):
        self.assertEqual(self.flash_filter.APP_ADDRESS, int(self.layout["app0"]["offset"], 0))
        self.assertEqual(self.flash_filter.APP_ADDRESS, 0x10000)
        self.assertEqual(self.flash_filter.APP_MAX_SIZE, int(self.layout["app0"]["size"], 0))
        self.assertEqual(self.flash_filter.APP_MAX_SIZE, 0x140000)
        self.assertEqual(
            self.flash_filter.APP_ADDRESS, self.build_release.APP_OFFSET
        )
        self.assertEqual(
            self.flash_filter.APP_MAX_SIZE, self.build_release.APP_MAX_SIZE
        )

    def test_draft_script_artifact_list_matches_layout(self):
        source = DRAFT_SCRIPT_PATH.read_text(encoding="utf-8")
        standalone = re.search(r'standalone="([^"]+)"', source)
        self.assertIsNotNone(standalone)
        for_match = re.search(r"for required in (.*?); do", source)
        self.assertIsNotNone(for_match)
        entries = [
            standalone.group(1) if item == "$standalone" else item
            for item in shlex.split(for_match.group(1))
        ]
        self.assertEqual(entries, self.layout["release_files"])
        self.assertGreaterEqual(len(entries), 13)

    def test_flasher_html_constants_match_layout(self):
        source = FLASHER_HTML_PATH.read_text(encoding="utf-8")

        def js_hex_constant(name):
            match = re.search(rf"const {name} = (0x[0-9a-fA-F]+);", source)
            self.assertIsNotNone(match, name)
            return int(match.group(1), 16)

        self.assertEqual(js_hex_constant("NVS_OFFSET"), int(self.layout["nvs"]["offset"], 0))
        self.assertEqual(js_hex_constant("NVS_SIZE"), int(self.layout["nvs"]["size"], 0))
        self.assertEqual(js_hex_constant("MAX_APP_SIZE"), int(self.layout["app0"]["size"], 0))

        parts_match = re.search(
            r"const EXPECTED_PARTS = \[(?P<body>.*?)\];", source, re.DOTALL
        )
        self.assertIsNotNone(parts_match)
        offsets = [
            int(value, 16)
            for value in re.findall(r"offset: (0x[0-9a-fA-F]+)", parts_match.group("body"))
        ]
        self.assertEqual(
            offsets,
            [
                int(self.layout["partition_table"]["bootloader"], 0),
                int(self.layout["partition_table"]["offset"], 0),
                int(self.layout["partition_table"]["partition_bin"], 0),
                int(self.layout["app0"]["offset"], 0),
            ],
        )
        self.assertEqual(offsets, [0x1000, 0x8000, 0xE000, 0x10000])


if __name__ == "__main__":
    unittest.main()
