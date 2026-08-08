from html.parser import HTMLParser
from pathlib import Path
import re
import shutil
import subprocess
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
HTML_PATH = REPO_ROOT / "Tools/wingie_prod_test.html"
MOCK_PATH = REPO_ROOT / "tests/tools/wingie_prod_mock.js"


class DocumentParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []
        self.external_assets = []
        self.script_count = 0
        self.style_count = 0
        self.i18n_pairs = []

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
        if tag == "link" and values.get("href") and not values["href"].startswith("data:"):
            self.external_assets.append(values["href"])
        if "data-i18n-zh" in values or "data-i18n-en" in values:
            self.i18n_pairs.append((values.get("data-i18n-zh"), values.get("data-i18n-en")))


class WingieProdTestHtmlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = HTML_PATH.read_text(encoding="utf-8")
        cls.mock_source = MOCK_PATH.read_text(encoding="utf-8")
        cls.protocol_source = (REPO_ROOT / "Wingie2/serial_config_protocol.h").read_text(encoding="utf-8")
        cls.config_source = (REPO_ROOT / "Wingie2/serial_config.ino").read_text(encoding="utf-8")
        cls.main_source = (REPO_ROOT / "Wingie2/Wingie2.ino").read_text(encoding="utf-8")
        cls.control_source = (REPO_ROOT / "Wingie2/control.ino").read_text(encoding="utf-8")
        cls.parser = DocumentParser()
        cls.parser.feed(cls.source)

    def test_is_one_self_contained_document(self):
        external_scripts = [a for a in self.parser.external_assets if a.endswith(".js")]
        self.assertEqual(external_scripts, ["../nav.js"])
        self.assertEqual(self.parser.script_count, 2)
        self.assertEqual(self.parser.style_count, 1)
        self.assertEqual(self.parser.external_assets, ["../nav.js"])
        self.assertEqual(len(self.parser.ids), len(set(self.parser.ids)))
        self.assertEqual(self.source.count("<!-- WINGIE_PROD_TEST_MOCK -->"), 1)
        self.assertNotRegex(self.source, r"https?://")
        self.assertNotIn("WebSocket", self.source)
        self.assertNotIn("wss://", self.source)

    def test_bilingual_copy_is_complete(self):
        for chinese, english in self.parser.i18n_pairs:
            self.assertTrue(chinese and english, f"i18n pair incomplete: {chinese!r} / {english!r}")
        for fragment in ("串口与固件版本", "一键刷入最新固件", "MIDI 输入检测", "控件监视", "清零计数"):
            self.assertIn(fragment, self.source)
        for fragment in ("Serial &amp; firmware version", "Install latest firmware", "MIDI input test", "Control monitor", "Reset counts"):
            self.assertIn(fragment, self.source)

    def test_serial_protocol_matches_firmware(self):
        self.assertIn('"get_controls"', self.protocol_source)
        self.assertIn('strcmp(value, "get_controls")', self.protocol_source)
        self.assertIn("kOperationGetControls", self.protocol_source)
        self.assertIn(r'op\":\"get_controls\"', self.config_source)
        self.assertIn(r'firmware\":\"%s', self.config_source)
        self.assertIn("WINGIE_FW_VERSION", self.main_source)
        self.assertIn("volatile uint16_t", self.main_source)
        for field in ("key", "mode_button", "oct_button", "source_switch", "pot"):
            self.assertIn(field, self.config_source)
        self.assertIn(r'"midi_rx\"', self.config_source)
        self.assertIn("request.reset", self.protocol_source)
        self.assertIn('parseBoolean(resetValue, request.reset', self.protocol_source)
        self.assertIn('request("get_controls")', self.source)
        self.assertIn('hello.firmware', self.source)
        self.assertIn("{reset: true}", self.source)

    def test_flash_guardrails_match_flasher_policy(self):
        for fragment in (
            "EXPECTED_CHIP = \"ESP32\"",
            "ESPTOOL_JS_VERSION = \"0.6.0\"",
            "NVS_OFFSET = 0x9000",
            "NVS_SIZE = 0x5000",
            "flash.mode !== \"dio\"",
            "flash.frequency !== \"80m\"",
            "flash.size !== \"4MB\"",
            "flash.eraseAll !== false",
            "preserve.name !== \"nvs\"",
            "manifest.parts.length !== EXPECTED_PARTS.length",
            "part.name === \"app\" && part.size > MAX_APP_SIZE",
            "BAUDRATE = 460800",
            "flashMode: \"dio\"",
            "flashFreq: \"80m\"",
            "flashSize: \"4MB\"",
            "eraseAll: false",
            "calculateMD5Hash: window.md5",
            "hardResetToApplication",
            "loader.main(\"default_reset\")",
        ):
            self.assertIn(fragment, self.source)

    def test_four_expected_parts_present(self):
        self.assertIn("{name: \"bootloader\", offset: 0x1000", self.source)
        self.assertIn("{name: \"partitions\", offset: 0x8000", self.source)
        self.assertIn("{name: \"boot_app0\", offset: 0xe000", self.source)
        self.assertIn("{name: \"app\", offset: 0x10000", self.source)

    def test_control_table_covers_all_34_controls(self):
        controls = set(re.findall(r'data-control="([^"]+)"', self.source))
        expected = set()
        for side in (0, 1):
            for index in range(12):
                expected.add(f"key:{side}:{index}")
        for index in range(2):
            expected.add(f"mode:{index}")
            expected.add(f"oct:0:{index}")
            expected.add(f"oct:1:{index}")
        expected.add("source")
        for index in range(3):
            expected.add(f"pot:{index}")
        self.assertEqual(controls, expected)
        self.assertNotIn("wpt-counts-finish", self.source)
        self.assertNotIn("finishCheck", self.source)
        self.assertNotIn("checkFinished", self.source)
        self.assertNotIn("wpt-counts-result", self.source)
        self.assertIn('cell.dataset.state = count >= 1 ? "pass" : "fail"', self.source)
        self.assertIn("格子全部为红", self.source)
        self.assertIn("实时变绿", self.source)

    def test_midi_test_threshold(self):
        self.assertIn("MIDI_PASS_THRESHOLD = 2", self.source)
        self.assertIn("state.midiRx - state.midiTestBaseline", self.source)

    def test_midi_send_test_uses_web_midi(self):
        self.assertIn('root.querySelector("#wpt-midi-device")', self.source)
        self.assertIn('root.querySelector("#wpt-midi-refresh")', self.source)
        self.assertIn('root.querySelector("#wpt-midi-send")', self.source)
        self.assertIn("navigator.requestMIDIAccess", self.source)
        self.assertIn("state.midiAccess.outputs.forEach", self.source)
        self.assertIn('output.send([0x90, pitch, 100])', self.source)
        self.assertIn('output.send([0x80, pitch, 0])', self.source)
        self.assertIn("MIDI_SEND_NOTES = [60, 64, 67, 72, 76]", self.source)
        self.assertIn("MIDI_SEND_ACCEPT_LOSS = 2", self.source)
        self.assertIn("state.midiRx - pending.baseline", self.source)
        self.assertIn("midiRefresh.addEventListener(\"click\", loadMidiAccess)", self.source)
        self.assertIn("midiSend.addEventListener(\"click\", sendMidiTestSequence)", self.source)
        self.assertIn('localStorage.getItem("wpt-midi-device")', self.source)

    def test_audio_arp_source_uses_web_audio(self):
        self.assertIn('root.querySelector("#wpt-audio-device")', self.source)
        self.assertIn('root.querySelector("#wpt-audio-start")', self.source)
        self.assertIn("navigator.mediaDevices.enumerateDevices", self.source)
        self.assertIn('device.kind === "audiooutput"', self.source)
        self.assertIn("context.setSinkId", self.source)
        self.assertIn("window.AudioContext || window.webkitAudioContext", self.source)
        self.assertIn("ARP_NOTES = [60, 64, 67, 72]", self.source)
        self.assertIn('oscillator.type = "triangle"', self.source)
        self.assertIn("noteToFrequency(pitch)", self.source)
        self.assertIn("context.createStereoPanner", self.source)
        self.assertIn('panner.pan.value = panValue === "left" ? -1 : 1', self.source)
        self.assertIn("state.audioTimer = window.setInterval(scheduleArpNote", self.source)
        self.assertIn("audioStart.addEventListener(\"click\", startArp)", self.source)
        self.assertIn("audioStop.addEventListener(\"click\", stopArp)", self.source)

    def test_version_comparison_handles_unknown_and_numeric(self):
        self.assertIn("function versionNumbers(", self.source)
        self.assertIn("function compareVersions(", self.source)
        self.assertIn("refreshVersionStatus(", self.source)

    def test_footer_text(self):
        self.assertIn("latest/ 目录", self.source)
        self.assertIn("latest/ directory", self.source)

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
