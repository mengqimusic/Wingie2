from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import threading
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
HTML_PATH = REPO_ROOT / "Tools/wingie_flasher.html"
MOCK_PATH = REPO_ROOT / "tests/tools/wingie_flasher_mock.js"


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass


class WingieFlasherBrowserTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.agent_browser = shutil.which("agent-browser")
        if not cls.agent_browser:
            raise unittest.SkipTest("agent-browser is not installed")

        cls.temporary = tempfile.TemporaryDirectory()
        cls.web_root = Path(cls.temporary.name)
        source = HTML_PATH.read_text(encoding="utf-8")
        marker = "  <script>\n    (() => {"
        if marker not in source:
            raise AssertionError("Unable to inject the flasher mock before the page script")
        source = source.replace(
            marker,
            '  <script src="./wingie_flasher_mock.js"></script>\n' + marker,
            1,
        )
        (cls.web_root / "wingie_flasher.html").write_text(source, encoding="utf-8")
        shutil.copyfile(MOCK_PATH, cls.web_root / "wingie_flasher_mock.js")

        handler = partial(QuietHandler, directory=str(cls.web_root))
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls.server_thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.server_thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}/wingie_flasher.html"
        cls.session = f"wingie-flasher-test-{os.getpid()}"

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, "agent_browser", None):
            subprocess.run(
                [cls.agent_browser, "--session", cls.session, "close"],
                text=True,
                capture_output=True,
                timeout=30,
            )
        if getattr(cls, "server", None):
            cls.server.shutdown()
            cls.server.server_close()
        if getattr(cls, "temporary", None):
            cls.temporary.cleanup()

    @classmethod
    def browser(cls, *arguments, javascript=None):
        command = [cls.agent_browser, "--session", cls.session, *arguments]
        result = subprocess.run(
            command,
            input=javascript,
            text=True,
            capture_output=True,
            timeout=60,
        )
        if result.returncode != 0:
            raise AssertionError(
                f"agent-browser failed: {' '.join(command)}\n{result.stdout}\n{result.stderr}"
            )
        return result.stdout

    def open_scenario(self, scenario):
        self.browser("open", f"{self.base_url}?scenario={scenario}")
        self.browser(
            "wait",
            "--fn",
            "window.__WINGIE_FLASH_PAGE__ && window.__WINGIE_FLASH_PAGE__.snapshot().packageReady",
        )

    def evaluate(self, javascript):
        return self.browser("eval", "--stdin", javascript=javascript)

    def test_supported_starting_states_use_the_same_safe_rom_flow(self):
        for scenario in ("blank", "v1", "v3", "current", "broken-app"):
            with self.subTest(scenario=scenario):
                self.open_scenario(scenario)
                self.evaluate(
                    """
                    (async () => {
                      const page = window.__WINGIE_FLASH_PAGE__;
                      await page.connect();
                      if (!page.snapshot().connected) throw new Error("ROM connection did not complete");
                      await page.flash();
                      if (!page.snapshot().completed) throw new Error("Four-part flash did not complete");
                      const log = window.__WINGIE_FLASH_MOCK__.snapshot().log;
                      const main = log.filter(entry => entry.type === "main");
                      const writes = log.filter(entry => entry.type === "write");
                      if (main.length !== 1 || main[0].mode !== "default_reset") throw new Error("Unexpected ROM reset flow");
                      if (writes.length !== 4) throw new Error(`Expected four writes, got ${writes.length}`);
                      const expected = [0x1000, 0x8000, 0xe000, 0x10000];
                      writes.forEach((entry, index) => {
                        if (entry.address !== expected[index]) throw new Error("Wrong flash offset");
                        if (entry.eraseAll !== false) throw new Error("Full erase became reachable");
                        if (entry.flashMode !== "dio" || entry.flashFreq !== "80m" || entry.flashSize !== "4MB") {
                          throw new Error("Unsafe flash options");
                        }
                      });
                      return "PASS";
                    })()
                    """
                )

    def test_connection_errors_are_actionable(self):
        expectations = {
            "no-port": "没有选择串口",
            "port-busy": "串口可能被占用",
            "boot-fail": "ROM bootloader 失败",
            "wrong-chip": "错误芯片",
        }
        for scenario, message in expectations.items():
            with self.subTest(scenario=scenario):
                self.open_scenario(scenario)
                self.evaluate(
                    f"""
                    (async () => {{
                      const page = window.__WINGIE_FLASH_PAGE__;
                      await page.connect();
                      if (page.snapshot().connected) throw new Error("Failure scenario connected unexpectedly");
                      const alert = document.querySelector("#wg-alert").textContent;
                      if (!alert.includes({message!r})) throw new Error(`Missing actionable message: ${{alert}}`);
                      return "PASS";
                    }})()
                    """
                )

    def test_md5_and_serial_failures_resume_from_the_failed_part(self):
        scenarios = {
            "md5-mismatch-once": [0x1000, 0x8000, 0x8000, 0xe000, 0x10000],
            "write-fail-once": [0x1000, 0x8000, 0xe000, 0xe000, 0x10000],
        }
        for scenario, expected_addresses in scenarios.items():
            with self.subTest(scenario=scenario):
                self.open_scenario(scenario)
                self.evaluate(
                    f"""
                    (async () => {{
                      const page = window.__WINGIE_FLASH_PAGE__;
                      await page.connect();
                      await page.flash();
                      if (!page.snapshot().connected) await page.connect();
                      await page.flash();
                      if (!page.snapshot().completed) throw new Error("Retry did not complete");
                      const writes = window.__WINGIE_FLASH_MOCK__.snapshot().log
                        .filter(entry => entry.type === "write")
                        .map(entry => entry.address);
                      const expected = {expected_addresses!r};
                      if (JSON.stringify(writes) !== JSON.stringify(expected)) {{
                        throw new Error(`Unexpected retry writes: ${{JSON.stringify(writes)}}`);
                      }}
                      return "PASS";
                    }})()
                    """
                )


if __name__ == "__main__":
    unittest.main()
