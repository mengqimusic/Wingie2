from html.parser import HTMLParser
from pathlib import Path
import re
import shutil
import subprocess
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
HTML_PATH = REPO_ROOT / "Tools/wingie_config.html"
MOCK_PATH = REPO_ROOT / "tests/tools/wingie_serial_mock.js"


class DocumentParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []
        self.external_assets = []
        self.script_count = 0
        self.style_count = 0

    def handle_starttag(self, tag, attributes):
        values = dict(attributes)
        if "id" in values:
            self.ids.append(values["id"])
        if tag == "script":
            self.script_count += 1
            if values.get("src"):
                self.external_assets.append(values["src"])
        if tag == "style":
            self.style_count += 1
        if tag == "link" and values.get("href"):
            self.external_assets.append(values["href"])


class WingieConfigHtmlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = HTML_PATH.read_text(encoding="utf-8")
        cls.mock_source = MOCK_PATH.read_text(encoding="utf-8")
        cls.parser = DocumentParser()
        cls.parser.feed(cls.source)

    def test_is_one_self_contained_document(self):
        self.assertEqual(self.parser.script_count, 1)
        self.assertEqual(self.parser.style_count, 1)
        self.assertEqual(self.parser.external_assets, [])
        self.assertEqual(len(self.parser.ids), len(set(self.parser.ids)))
        self.assertNotRegex(self.source, r"https?://")
        self.assertNotIn("fetch(", self.source)
        self.assertNotIn("WebSocket", self.source)

    def test_contains_realtime_schema_two_protocol(self):
        for operation in (
            "hello",
            "get",
            "save",
            "reset",
            "get_cave",
            "get_state",
            "set_param",
            "set_ratio_value",
            "set_cave_value",
            "set_cave_mute",
        ):
            self.assertRegex(self.source, rf'"{operation}"')
        self.assertIn("navigator.serial.requestPort()", self.source)
        self.assertIn("frequency_min", self.source)
        self.assertIn("config_schema !== 2", self.source)
        self.assertIn('response.event === "changed"', self.source)
        self.assertIn("response.boot_id", self.source)
        self.assertIn("window.setInterval(() => scheduleStateRefresh(0), 1000)", self.source)
        self.assertGreaterEqual(self.source.count("expected_revision:"), 3)
        self.assertIn("response.resource_revision", self.source)

    def test_has_two_sided_complete_product_controls(self):
        for side in ("left", "right"):
            self.assertIn(f'data-side-panel="{side}"', self.source)
            for name in ("mode", "mix", "decay", "volume", "threshold"):
                self.assertIn(f'data-value-key="param:{side}:{name}"', self.source)
            self.assertIn(f'id="wg-{side}-cave-rows"', self.source)
            self.assertIn(f'data-bank-tabs="{side}"', self.source)
        for name in (
            "a3_hz",
            "tuning",
            "pre_clip_gain",
            "post_clip_gain",
            "midi_left",
            "midi_right",
            "midi_both",
        ):
            self.assertIn(f'data-value-key="param:shared:{name}"', self.source)
        for readout in ("note", "fundamental", "octave", "active-bank", "trigger", "origin"):
            self.assertIn(f'id="wg-left-{readout}"', self.source)
            self.assertIn(f'id="wg-right-{readout}"', self.source)

    def test_is_apply_free_and_live_clips_numeric_input(self):
        self.assertNotRegex(self.source, r"(?i)>\s*Apply")
        self.assertNotIn("applyAll", self.source)
        self.assertIn("window.setTimeout(() => commitEdit(key), 150)", self.source)
        self.assertIn("Math.min(spec.max, Math.max(spec.min, value))", self.source)
        self.assertIn('type="text" inputmode="decimal"', self.source)
        self.assertIn('window.confirm("将当前全部运行配置写入 Wingie2 flash？")', self.source)
        self.assertLess(
            self.source.index("await waitForLiveWrites();", self.source.index("async function saveConfiguration")),
            self.source.index('await request("save"', self.source.index("async function saveConfiguration")),
        )

    def test_import_export_cover_complete_configuration(self):
        self.assertIn('format: "wingie2-config"', self.source)
        self.assertIn("version: 2", self.source)
        self.assertIn("channels:", self.source)
        self.assertIn("shared:", self.source)
        self.assertIn("ratio:", self.source)
        self.assertIn("cave:", self.source)
        self.assertIn("await waitForLiveWrites()", self.source)

    def test_mock_supports_realtime_and_external_events(self):
        for operation in (
            "get_state",
            "set_param",
            "set_ratio_value",
            "set_cave_value",
            "set_cave_mute",
            "save",
        ):
            self.assertRegex(self.mock_source, rf'request\.op === "{operation}"')
        for helper in ("hardwareChange", "midiChange", "sharedChange", "failNext", "disconnect"):
            self.assertIn(f"{helper}(", self.mock_source)

    def test_inline_javascript_parses(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node is not installed")
        match = re.search(r"<script>(.*)</script>", self.source, re.DOTALL)
        self.assertIsNotNone(match)
        result = subprocess.run(
            [node, "--check", "-"],
            input=match.group(1),
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_mock_javascript_parses(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node is not installed")
        result = subprocess.run(
            [node, "--check", str(MOCK_PATH)],
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
