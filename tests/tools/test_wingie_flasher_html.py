from html.parser import HTMLParser
from pathlib import Path
import re
import shutil
import subprocess
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
HTML_PATH = REPO_ROOT / "Tools/wingie_flasher.html"
MOCK_PATH = REPO_ROOT / "tests/tools/wingie_flasher_mock.js"


class DocumentParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []
        self.script_count = 0
        self.style_count = 0
        self.external_assets = []

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


class VisibleTextParser(HTMLParser):
    VOID_ELEMENTS = {
        "area",
        "base",
        "br",
        "col",
        "embed",
        "hr",
        "img",
        "input",
        "link",
        "meta",
        "param",
        "source",
        "track",
        "wbr",
    }

    def __init__(self):
        super().__init__()
        self.stack = []
        self.suppressed = 0
        self.text = []

    def handle_starttag(self, tag, attributes):
        values = dict(attributes)
        suppress = (
            tag in {"script", "style", "template"}
            or "hidden" in values
            or values.get("aria-hidden") == "true"
        )
        if tag not in self.VOID_ELEMENTS:
            self.stack.append((tag, suppress))
            if suppress:
                self.suppressed += 1

    def handle_endtag(self, tag):
        while self.stack:
            open_tag, suppress = self.stack.pop()
            if suppress:
                self.suppressed -= 1
            if open_tag == tag:
                break

    def handle_data(self, data):
        if self.suppressed == 0 and data.strip():
            self.text.append(data)


class WingieFlasherHtmlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = HTML_PATH.read_text(encoding="utf-8")
        cls.mock_source = MOCK_PATH.read_text(encoding="utf-8")
        cls.parser = DocumentParser()
        cls.parser.feed(cls.source)

    def test_is_one_embeddable_document(self):
        # nav.js（共享导航栏）是唯一允许的外部 script 引用
        external_scripts = [a for a in self.parser.external_assets if a.endswith(".js")]
        self.assertEqual(external_scripts, ["../nav.js"])
        self.assertEqual(self.parser.script_count, 2)
        self.assertEqual(self.parser.style_count, 1)
        self.assertEqual(self.parser.external_assets, ["../nav.js"])
        self.assertEqual(len(self.parser.ids), len(set(self.parser.ids)))
        self.assertEqual(self.source.count("<!-- WINGIE_STANDALONE_BUNDLE -->"), 1)
        self.assertEqual(self.source.count("<!-- WINGIE_STANDALONE_LICENSES -->"), 1)
        self.assertEqual(self.source.count("<!-- WINGIE_TEST_MOCK -->"), 1)

    def test_changelog_and_instructions_carry_both_languages_as_i18n(self):
        parser = VisibleTextParser()
        parser.feed(self.source)
        visible_text = " ".join(" ".join(parser.text).split())
        # The default (zh) text is rendered statically in the markup.
        for fragment in (
            "更新内容",
            "操作说明",
            "新增比例模式（Ratio Mode）",
            "新增 USB 网页配置",
            "新增 MPE 模式",
            "山洞频率精度从整数分辨率提升为",
            "MIDI 模式控制（CC 0）的分段改为五模式均分",
            "银色 Wingie2 需要使用 USB A–C 线",
            "连接 Wingie2，并关闭串口监视器、配置页等占用串口的软件",
        ):
            self.assertIn(fragment, visible_text)
        # The English copy lives in data-i18n-en attributes (toggled at runtime).
        for fragment in (
            "Changelog",
            "Instructions",
            "New Ratio Mode",
            "New USB Web Configuration",
            "New MPE Mode",
            "Cave frequency precision upgraded",
            "MIDI mode control (CC 0) segmentation changed",
            "Silver Wingie2 units require a USB-A-to-USB-C cable",
            "Connect Wingie2, and close serial monitors",
            "choose the Wingie2 USB serial port from the list",
        ):
            self.assertIn(fragment, self.source)
        # No technical detail should leak into the visible markup.
        for fragment in (
            "安全边界",
            "发布固件",
            "0x1000",
            "manifest.json",
            "ROM bootloader",
            "DTR/RTS",
            "SHA-256",
            "MD5",
            "查看技术日志",
            "Third-party licenses",
            "Cave 切换到 Poly",
            "13 秒",
            "6d78147",
            "过载与失真风险",
            "overload and distortion",
        ):
            self.assertNotIn(fragment, visible_text)

    def test_language_toggle_button_is_present(self):
        self.assertIn('id="wg-language"', self.source)
        self.assertIn('data-i18n-aria-zh="切换语言"', self.source)
        self.assertIn('data-i18n-aria-en="Switch language"', self.source)
        self.assertIn("state.language = state.language === \"zh\" ? \"en\" : \"zh\"", self.source)

    def test_uses_strict_wingie_manifest_and_fixed_offsets(self):
        for field in (
            'manifest.schema !== 1',
            'manifest.chipFamily !== EXPECTED_CHIP',
            'manifest.esptoolJs !== ESPTOOL_JS_VERSION',
            'manifest.parts.length !== EXPECTED_PARTS.length',
            'preserve.offset !== NVS_OFFSET',
            'part.size > MAX_APP_SIZE',
            'flashSpanSize(part.size)',
        ):
            self.assertIn(field, self.source)
        for offset in ("0x1000", "0x8000", "0xe000", "0x10000", "0x9000"):
            self.assertIn(offset, self.source)
        self.assertIn('const MANIFEST_URL = "./manifest.json"', self.source)

    def test_downloads_all_images_and_checks_sha256_before_connect(self):
        self.assertIn('crypto.subtle.digest("SHA-256", data)', self.source)
        self.assertIn("actual !== part.sha256", self.source)
        self.assertIn("state.images[index] = await downloadImage", self.source)
        self.assertIn("elements.connect.disabled = !state.packageReady", self.source)
        self.assertIn("state.versionLabel = copy(`固件 ${manifest.version}`, `Firmware ${manifest.version}`)", self.source)

    def test_generated_standalone_uses_embedded_images_and_runtime(self):
        for fragment in (
            "window.__WINGIE_EMBEDDED_RELEASE__",
            "window.__WINGIE_ESPTOOL_READY__",
            "decodeBase64Image(part, images[part.name])",
            "await loadEmbeddedImages(manifest)",
            "state.runtime = await withTimeout(runtimeReady",
            "withTimeout(runtimeReady, 5000",
            'embeddedRelease ? copy("正在读取内嵌固件…", "Reading embedded firmware…")',
            "请改用发布包中的 standalone HTML",
            "请从官方 GitHub Pages HTTPS 地址重新打开页面",
        ):
            self.assertIn(fragment, self.source)

    def test_rom_chip_check_does_not_depend_on_installed_firmware(self):
        self.assertIn('loader.main("default_reset")', self.source)
        self.assertIn("loader.chip && loader.chip.CHIP_NAME", self.source)
        self.assertIn("chipName !== EXPECTED_CHIP", self.source)
        self.assertIn("await transport.setDTR(false)", self.source)
        self.assertIn("await transport.setRTS(true)", self.source)
        self.assertIn("await transport.setRTS(false)", self.source)
        self.assertIn("await disconnectDevice({quiet: true})", self.source)
        self.assertNotIn('state.loader.after("hard_reset")', self.source)
        self.assertNotIn("readFlash", self.source)

    def test_rom_handshake_and_flash_write_have_timeouts(self):
        # USB 挂起时 loader.main / writeFlash 会永久阻塞，按钮全禁用只能刷新页面
        self.assertIn("ROM_HANDSHAKE_TIMEOUT_MS", self.source)
        self.assertIn("FLASH_WRITE_TIMEOUT_MS", self.source)
        self.assertIn('withTimeout(loader.main("default_reset"), ROM_HANDSHAKE_TIMEOUT_MS', self.source)
        self.assertIn("withTimeout(state.loader.writeFlash({", self.source)
        self.assertNotRegex(self.source, r'\{\s*op:\s*["\']hello["\']')

    def test_write_options_are_fixed_and_full_erase_is_unreachable(self):
        write_call = re.search(
            r"state\.loader\.writeFlash\(\{(?P<body>.*?)\n\s*\}\);",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(write_call)
        body = write_call.group("body")
        for option in (
            'flashMode: "dio"',
            'flashFreq: "80m"',
            'flashSize: "4MB"',
            "eraseAll: false",
            "calculateMD5Hash: window.md5",
        ):
            self.assertIn(option, body)
        self.assertNotRegex(self.source, r"\.eraseFlash\s*\(")
        self.assertNotRegex(self.mock_source, r"\.eraseFlash\s*\(")

    def test_md5_failures_resume_at_the_failed_segment(self):
        self.assertIn("state.resumeIndex = index", self.source)
        self.assertIn("state.resumeIndex = index + 1", self.source)
        self.assertIn("isMd5Mismatch(error)", self.source)
        self.assertIn('current === "md5-mismatch-once"', self.mock_source)
        self.assertIn('current === "write-fail-once"', self.mock_source)

    def test_mock_covers_supported_device_starting_states_through_same_rom_main(self):
        for device in ("blank", "v1", "v3", "current", "broken-app"):
            self.assertIn(f'"{device}"', self.mock_source)
        self.assertEqual(self.mock_source.count("async main(mode)"), 1)
        self.assertIn('{type: "main", mode, scenario: current}', self.mock_source)

    def test_mock_bypasses_vendor_loading(self):
        self.assertIn("if (mock)", self.source)
        self.assertIn("state.runtime = mock.esptool", self.source)
        self.assertIn('import(ESPTOOL_VENDOR_URL)', self.source)
        self.assertIn('const ESPTOOL_VENDOR_URL = "./vendor/esptool-js.bundle.js"', self.source)
        self.assertIn('const MD5_VENDOR_URL = "./vendor/md5.min.js"', self.source)

    def test_inline_javascript_and_mock_parse(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node is not installed")
        match = re.search(r"<script>(.*)</script>", self.source, re.DOTALL)
        self.assertIsNotNone(match)
        for label, javascript in (("inline script", match.group(1)), ("mock", self.mock_source)):
            result = subprocess.run(
                [node, "--check", "-"],
                input=javascript,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, f"{label}: {result.stderr}")


if __name__ == "__main__":
    unittest.main()
