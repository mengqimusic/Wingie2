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
        cls.firmware_source = (REPO_ROOT / "Wingie2/serial_config.ino").read_text(encoding="utf-8")
        cls.protocol_source = (REPO_ROOT / "Wingie2/serial_config_protocol.h").read_text(encoding="utf-8")
        cls.control_source = (REPO_ROOT / "Wingie2/control.ino").read_text(encoding="utf-8")
        cls.midi_source = (REPO_ROOT / "Wingie2/MIDI.ino").read_text(encoding="utf-8")
        cls.main_source = (REPO_ROOT / "Wingie2/Wingie2.ino").read_text(encoding="utf-8")
        cls.storage_source = (REPO_ROOT / "Wingie2/stuff.ino").read_text(encoding="utf-8")
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

    def test_uses_schema_three_snapshot_protocol(self):
        self.assertIn("Number(hello.config_schema) !== 3", self.source)
        for operation in (
            "hello",
            "get_settings",
            "get",
            "get_cave",
            "set_param",
            "set",
            "set_cave",
            "reset",
            "save",
        ):
            self.assertRegex(self.source, rf'"{operation}"')
        self.assertIn("navigator.serial.requestPort()", self.source)
        self.assertIn("expected_revision", self.source)
        self.assertIn("setInterval", self.source)
        self.assertNotIn("heartbeat", self.source.lower())
        self.assertNotIn('"changed"', self.source)
        self.assertNotIn("set_ratio_value", self.source)
        self.assertNotIn("set_cave_value", self.source)
        self.assertNotIn("set_cave_mute", self.source)
        self.assertIn('code !== "busy"', self.source)
        self.assertIn("await helloWhenReady();", self.source)

    def test_connect_refresh_and_poll_use_complete_snapshots(self):
        snapshot_reader = re.search(
            r"async function readFullSnapshot\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(snapshot_reader)
        snapshot_block = snapshot_reader.group(1)
        self.assertEqual(snapshot_block.count('request("get_settings")'), 1)
        self.assertEqual(snapshot_block.count('request("get")'), 1)
        self.assertEqual(snapshot_block.count("await readCaveResponses()"), 1)

        refresh = re.search(
            r"async function refreshAll\(announce = true\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(refresh)
        block = refresh.group(1)
        self.assertIn("applyFullSnapshot(await readFullSnapshot(), false)", block)

        poll = re.search(
            r"async function pollFullSnapshot\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(poll)
        self.assertIn("const snapshot = await readFullSnapshot()", poll.group(1))
        self.assertIn("applyFullSnapshot(snapshot, true)", poll.group(1))
        self.assertIn("window.setInterval", self.source)
        self.assertIn("}, 1000);", self.source)
        cave_reader = re.search(
            r"async function readCaveResponses\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(cave_reader)
        self.assertEqual(cave_reader.group(1).count('request("get_cave"'), 1)
        self.assertIn("for (const side of sides)", cave_reader.group(1))
        self.assertIn("for (let bank = 0; bank < bankCount; bank += 1)", cave_reader.group(1))
        self.assertIn("#wg-refresh", self.source)

    def test_has_complete_controls_and_omits_runtime_status(self):
        for target in ("left", "right"):
            for name in ("mode", "threshold"):
                self.assertIn(f'param:{target}:{name}', self.source)
        for name in (
            "a3_hz",
            "tuning",
            "pre_clip_gain",
            "post_clip_gain",
            "midi_left",
            "midi_right",
            "midi_both",
        ):
            self.assertIn(f'param:shared:{name}', self.source)
        for stale_item in ("Source", "Note", "Fundamental", "Active Cave", "mix", "decay", "volume"):
            self.assertNotIn(stale_item, self.source)
        for runtime_key in (
            "param:left:mix", "param:left:decay", "param:left:volume",
            "param:right:mix", "param:right:decay", "param:right:volume",
        ):
            self.assertNotIn(runtime_key, self.source)
        self.assertIn("Factory Ratio", self.source)
        self.assertIn("Export JSON", self.source)
        self.assertIn("Import JSON", self.source)
        self.assertNotIn(">Apply", self.source)

    def test_user_facing_copy_is_bilingual(self):
        for phrase in (
            "连接 Wingie2 / Connect",
            "断开 / Disconnect",
            "模式 / Mode",
            "输入阈值 / Input Threshold",
            "Cave 音库 / Cave Banks",
            "共享 Ratio 配置 / Shared Ratio Profile",
            "共享设置 / Shared Settings",
            "保存到 Flash / Save to Flash",
            "刷新 / Refresh",
            "导出 JSON / Export JSON",
            "导入 JSON / Import JSON",
            "Device snapshot loaded;",
            "Full device snapshot refreshed.",
            "Configuration saved to Wingie2 flash.",
        ):
            self.assertIn(phrase, self.source)

    def test_minimal_responsive_visual_contract(self):
        self.assertIn("background: #fff", self.source)
        self.assertIn("width: min(1120px, 100%)", self.source)
        self.assertIn("grid-template-columns: repeat(2, minmax(0, 1fr))", self.source)
        self.assertIn("border-radius: 4px", self.source)
        self.assertIn("@media (max-width: 760px)", self.source)
        self.assertIn('data-mobile-side="left"', self.source)
        self.assertIn("overflow-x: auto", self.source)
        self.assertNotIn("box-shadow", self.source)
        self.assertNotIn("linear-gradient", self.source)
        self.assertNotIn("radial-gradient", self.source)

    def test_full_resource_debounce_and_save_flush(self):
        self.assertIn("window.setTimeout(run, 150)", self.source)
        self.assertIn('request("set", {expected_revision:', self.source)
        self.assertIn('request("set_cave", {', self.source)
        self.assertIn("await waitForWrites();", self.source)
        self.assertIn('request("save", {}, 6000)', self.source)
        self.assertIn("state.writeChain", self.source)
        self.assertIn("state.resourceVersions", self.source)
        self.assertIn("await refreshDependentCaves();", self.source)
        self.assertIn("await acknowledge(response", self.source)

    def test_mock_matches_schema_three_without_events(self):
        self.assertIn("config_schema: 3", self.mock_source)
        for operation in (
            "get_settings",
            "set_param",
            "set",
            "set_cave",
            "save",
        ):
            self.assertIn(f'request.op === "{operation}"', self.mock_source)
        self.assertIn("setSettings(values)", self.mock_source)
        self.assertIn("max_frame: 512", self.mock_source)
        self.assertIn("caves_changed: cavesChanged", self.mock_source)
        self.assertNotIn('event: "changed"', self.mock_source)

    def test_firmware_uses_snapshot_settings_without_runtime_sync(self):
        self.assertIn('"config_schema\\":3', self.firmware_source)
        self.assertIn("kOperationGetSettings", self.protocol_source)
        self.assertIn("kOperationSetParam", self.protocol_source)
        self.assertIn("void sendSettings(uint32_t id)", self.firmware_source)
        self.assertIn("bool applyScalarParameter", self.firmware_source)
        self.assertIn('"caves_changed\\":%s', self.firmware_source)
        self.assertIn("generalSettingsAreDirty()", self.firmware_source)
        self.assertIn("cavesChanged = tune_caves();", self.firmware_source)
        self.assertRegex(
            self.midi_source,
            r'if \(use_alt_tuning && alt_tuning_index >= 0\) tune_caves\(\);\s*apply_note_profiles_to_dsp\(\);',
        )
        self.assertIn("bool tune_caves()", self.main_source)
        self.assertIn("> 0.0051f", self.main_source)
        self.assertIn("dsp.getParamValue", self.firmware_source)
        self.assertIn("struct CaveBankSnapshot", self.firmware_source)
        self.assertIn("const uint32_t currentRevision", self.firmware_source)
        self.assertIn("bool revisionConflict = false;", self.firmware_source)
        self.assertIn("if (!isfinite(value)) return false;", self.firmware_source)
        reset_block = self.firmware_source.split("case wingie_serial::kOperationReset:", 1)[1]
        self.assertIn("request.expectedRevision != ratio_profile.revision", reset_block)
        self.assertIn("apply_channel_mode_change(ch);", self.control_source)
        self.assertIn("void apply_channel_mode_change(byte ch)", self.main_source)
        self.assertIn("bool save_all_preferences()", self.storage_source)
        self.assertIn("void service_preferences_save()", self.storage_source)
        self.assertIn("request_preferences_save();", self.control_source)
        self.assertNotIn("save_stuff();", self.control_source)
        self.assertIn("service_preferences_save();", self.main_source)
        self.assertIn("if (!serial_config_ready) return;", self.main_source)
        self.assertIn("tuning_preferences_dirty", self.storage_source)
        self.assertRegex(
            self.storage_source,
            r"if \(unq_caves_store\) \{\s*if \(!save_general_preferences\(prefs\)\)",
        )
        for source, index in (
            (self.firmware_source, "performanceIndex"),
            (self.midi_source, "MIX"),
        ):
            sampled = source.index(f"potValSampled[{index}] = potValRealtime[{index}];")
            released = source.index(f"realtime_value_valid[{index}] = false;")
            self.assertLess(sampled, released)
        combined = self.firmware_source + self.protocol_source + self.control_source + self.main_source
        for forbidden in ("config_runtime", "config_event_pending", "sendChangedEvent", "get_state"):
            self.assertNotIn(forbidden, combined)

    def test_inline_javascript_and_mock_parse(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node is not installed")
        match = re.search(r"<script>(.*)</script>", self.source, re.DOTALL)
        self.assertIsNotNone(match)
        for command, source in (
            ([node, "--check", "-"], match.group(1)),
            ([node, "--check", str(MOCK_PATH)], None),
        ):
            result = subprocess.run(
                command,
                input=source,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
