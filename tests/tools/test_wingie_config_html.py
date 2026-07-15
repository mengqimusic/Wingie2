from html.parser import HTMLParser
from pathlib import Path
import re
import shutil
import subprocess
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
HTML_PATH = REPO_ROOT / "Tools/wingie_config.html"


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

    def test_contains_complete_ratio_and_cave_protocol(self):
        for operation in (
            "hello",
            "get",
            "set",
            "save",
            "reset",
            "status",
            "get_cave",
            "set_cave",
        ):
            self.assertRegex(self.source, rf'"{operation}"')
        self.assertIn("navigator.serial.requestPort()", self.source)
        self.assertIn("frequency_min", self.source)
        self.assertIn("expected_revision", self.source)

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


if __name__ == "__main__":
    unittest.main()
