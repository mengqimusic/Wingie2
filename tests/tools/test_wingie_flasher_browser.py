from functools import partial
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import threading
import unittest
from urllib.parse import urlsplit


REPO_ROOT = Path(__file__).resolve().parents[2]
HTML_PATH = REPO_ROOT / "Tools/wingie_flasher.html"
MOCK_PATH = REPO_ROOT / "tests/tools/wingie_flasher_mock.js"
RELEASE_SCRIPT = REPO_ROOT / "Tools/firmware_release/build_release.py"

RELEASE_SPEC = importlib.util.spec_from_file_location(
    "wingie2_firmware_release_browser", RELEASE_SCRIPT
)
RELEASE = importlib.util.module_from_spec(RELEASE_SPEC)
RELEASE_SPEC.loader.exec_module(RELEASE)


class QuietHandler(SimpleHTTPRequestHandler):
    requests = []
    request_lock = threading.Lock()

    def do_GET(self):
        with self.request_lock:
            self.requests.append(self.path)
        super().do_GET()

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
        mock_source = MOCK_PATH.read_text(encoding="utf-8")
        marker = "  <script>\n    (() => {"
        if marker not in source:
            raise AssertionError("Unable to inject the flasher mock before the page script")
        source = source.replace(
            marker,
            "  <script>\n" + mock_source + "\n  </script>\n" + marker,
            1,
        )
        (cls.web_root / "wingie_flasher.html").write_text(source, encoding="utf-8")

        image_specs = (
            ("bootloader", "Wingie2-browser.bootloader.bin", 0x1000, b"BOOT"),
            ("partitions", "Wingie2-browser.partitions.bin", 0x8000, b"PART"),
            ("boot_app0", "Wingie2-browser.boot_app0.bin", 0xE000, b"BOOT_APP0"),
            ("app", "Wingie2-browser.app.bin", 0x10000, b"APP"),
        )
        image_data = {name: data for name, _path, _offset, data in image_specs}
        manifest = {
            "schema": 1,
            "name": "Wingie2 Browser Test",
            "version": "browser-test",
            "chipFamily": "ESP32",
            "esptoolJs": "0.6.0",
            "flash": {"mode": "dio", "frequency": "80m", "size": "4MB", "eraseAll": False},
            "preserve": [{"name": "nvs", "offset": 0x9000, "size": 0x5000}],
            "parts": [
                {
                    "name": name,
                    "path": path,
                    "offset": offset,
                    "size": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                }
                for name, path, offset, data in image_specs
            ],
        }
        standalone = RELEASE.build_standalone_html(
            source,
            manifest,
            image_data,
            "class EmbeddedTransport {}\n"
            "class EmbeddedLoader {}\n"
            "export {EmbeddedTransport as Transport, EmbeddedLoader as ESPLoader};\n",
            "window.md5 = value => String(value.length).padStart(32, '0');",
            "Browser test third-party licenses",
        )
        (cls.web_root / "wingie_standalone.html").write_text(
            standalone, encoding="utf-8"
        )

        handler = partial(QuietHandler, directory=str(cls.web_root))
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls.server_thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.server_thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}/wingie_flasher.html"
        cls.standalone_url = f"http://127.0.0.1:{cls.server.server_port}/wingie_standalone.html"
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
                      const reset = log.filter(entry => entry.type === "dtr" || entry.type === "rts");
                      const expectedReset = [
                        {type: "dtr", value: false},
                        {type: "rts", value: true},
                        {type: "rts", value: false},
                        {type: "dtr", value: false}
                      ];
                      if (JSON.stringify(reset) !== JSON.stringify(expectedReset)) throw new Error("Wrong application reset sequence");
                      if (log.filter(entry => entry.type === "disconnect").length !== 1) throw new Error("Web Serial port was not released");
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

    def test_standalone_loads_without_manifest_image_or_vendor_requests(self):
        with QuietHandler.request_lock:
            QuietHandler.requests.clear()
        self.browser("open", f"{self.standalone_url}?scenario=current")
        self.browser(
            "wait",
            "--fn",
            "window.__WINGIE_FLASH_PAGE__ && window.__WINGIE_FLASH_PAGE__.snapshot().packageReady",
        )
        self.evaluate(
            """
            (async () => {
              const page = window.__WINGIE_FLASH_PAGE__;
              if (!page.snapshot().embedded) throw new Error("Standalone mode was not detected");
              const runtime = await window.__WINGIE_ESPTOOL_READY__;
              if (typeof runtime.Transport !== "function" || typeof runtime.ESPLoader !== "function") {
                throw new Error("Standalone runtime constructors were not initialized");
              }
              if (typeof window.md5 !== "function") throw new Error("Standalone MD5 was not initialized");
              const initialLog = window.__WINGIE_FLASH_MOCK__.snapshot().log;
              if (initialLog.some(entry => entry.type === "fetch-manifest" || entry.type === "fetch-image")) {
                throw new Error("Standalone page fetched adjacent firmware assets");
              }
              const partStates = Array.from(document.querySelectorAll(".wg-part-state"), node => node.textContent);
              if (partStates.length !== 4 || partStates.some(value => !value.includes("SHA-256 通过"))) {
                throw new Error("Standalone images did not pass SHA-256 verification");
              }
              await page.connect();
              await page.flash();
              if (!page.snapshot().completed) throw new Error("Standalone flash flow did not complete");
              const writes = window.__WINGIE_FLASH_MOCK__.snapshot().log.filter(entry => entry.type === "write");
              if (writes.length !== 4 || writes.some(entry => entry.eraseAll !== false)) {
                throw new Error("Standalone flow changed safe flash behavior");
              }
              if (writes.some(entry => entry.fileCount !== 1 || entry.flashMode !== "dio" ||
                  entry.flashFreq !== "80m" || entry.flashSize !== "4MB")) {
                throw new Error("Standalone flow changed fixed flash options");
              }
              const addresses = writes.map(entry => entry.address);
              const expectedAddresses = [0x1000, 0x8000, 0xe000, 0x10000];
              if (JSON.stringify(addresses) !== JSON.stringify(expectedAddresses)) {
                throw new Error(`Unexpected standalone offsets: ${JSON.stringify(addresses)}`);
              }
              return "PASS";
            })()
            """
        )
        with QuietHandler.request_lock:
            requested_paths = [urlsplit(path).path for path in QuietHandler.requests]
        self.assertEqual(requested_paths, ["/wingie_standalone.html"])


if __name__ == "__main__":
    unittest.main()
