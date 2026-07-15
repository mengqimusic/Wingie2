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


class WingieFlasherHtmlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = HTML_PATH.read_text(encoding="utf-8")
        cls.mock_source = MOCK_PATH.read_text(encoding="utf-8")
        cls.parser = DocumentParser()
        cls.parser.feed(cls.source)

    def test_is_one_embeddable_document(self):
        self.assertEqual(self.parser.script_count, 1)
        self.assertEqual(self.parser.style_count, 1)
        self.assertEqual(self.parser.external_assets, [])
        self.assertEqual(len(self.parser.ids), len(set(self.parser.ids)))
        self.assertEqual(self.source.count("<!-- WINGIE_STANDALONE_BUNDLE -->"), 1)
        self.assertEqual(self.source.count("<!-- WINGIE_STANDALONE_LICENSES -->"), 1)

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

    def test_generated_standalone_uses_embedded_images_and_runtime(self):
        for fragment in (
            "window.__WINGIE_EMBEDDED_RELEASE__",
            "window.__WINGIE_ESPTOOL_READY__",
            "decodeBase64Image(part, images[part.name])",
            "await loadEmbeddedImages(manifest)",
            "state.runtime = await withTimeout(runtimeReady",
            "withTimeout(runtimeReady, 5000",
            'embeddedRelease ? "正在读取内嵌固件…"',
            "如果只部署一个文件，请改用发布包中的 standalone HTML",
            "当前静态主机可能阻止了页面内嵌的 JavaScript module",
        ):
            self.assertIn(fragment, self.source)

    def test_rom_chip_check_does_not_depend_on_installed_firmware(self):
        self.assertIn('await loader.main("default_reset")', self.source)
        self.assertIn("loader.chip && loader.chip.CHIP_NAME", self.source)
        self.assertIn("chipName !== EXPECTED_CHIP", self.source)
        self.assertNotIn("readFlash", self.source)
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
