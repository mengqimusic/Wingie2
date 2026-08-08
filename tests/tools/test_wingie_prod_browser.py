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
HTML_PATH = REPO_ROOT / "Tools/wingie_prod_test.html"
MOCK_PATH = REPO_ROOT / "tests/tools/wingie_prod_mock.js"

WAIT_JS = """
  const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const waitFor = async (predicate, label, timeout = 4500) => {
    const started = performance.now();
    while (!predicate()) {
      if (performance.now() - started > timeout) throw new Error("Timeout: " + label);
      await sleep(20);
    }
  };
  const assert = (condition, message) => { if (!condition) throw new Error(message); };
  const element = (selector) => document.querySelector(selector);
  const page = () => window.__WINGIE_PROD_PAGE__;
  const mock = window.__WINGIE_PROD_MOCK__;
  const snapshot = () => page().snapshot();
"""


class QuietHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        # nav.js（共享导航栏）在测试环境不存在，返回空 JS 避免 404 阻断页面
        if self.path == "/nav.js" or self.path == "../nav.js":
            self.send_response(200)
            self.send_header("Content-Type", "application/javascript")
            self.end_headers()
            self.wfile.write(b"// test stub")
            return
        super().do_GET()

    def log_message(self, format, *args):
        pass


class WingieProdTestBrowserTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.agent_browser = shutil.which("agent-browser")
        if not cls.agent_browser:
            raise unittest.SkipTest("agent-browser is not installed")

        cls.temporary = tempfile.TemporaryDirectory()
        cls.web_root = Path(cls.temporary.name)
        source = HTML_PATH.read_text(encoding="utf-8")
        mock_source = MOCK_PATH.read_text(encoding="utf-8")
        marker = "  <!-- WINGIE_PROD_TEST_MOCK -->"
        if marker not in source:
            raise AssertionError("Unable to inject the prod test mock before the page script")
        source = source.replace(marker, "  <script>\n" + mock_source + "\n  </script>", 1)
        (cls.web_root / "wingie_prod_test.html").write_text(source, encoding="utf-8")

        handler = partial(QuietHandler, directory=str(cls.web_root))
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls.server_thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.server_thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}/wingie_prod_test.html"
        cls.session = f"wingie-prod-test-{os.getpid()}"

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

    def open_scenario(self, scenario="current"):
        self.browser("open", f"{self.base_url}?scenario={scenario}")
        self.browser(
            "wait",
            "--fn",
            "window.__WINGIE_PROD_PAGE__ && window.__WINGIE_PROD_PAGE__.snapshot().packageReady",
        )

    def evaluate(self, javascript):
        return self.browser("eval", "--stdin", javascript=javascript)

    def test_latest_package_loads_and_version_compare(self):
        self.browser("set", "viewport", "1280", "900")
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              assert(snapshot().packageReady, "latest package did not load");
              assert(element("#wpt-latest-version").textContent === "v9.9.9-test", "latest version label is wrong: " + element("#wpt-latest-version").textContent);
              assert(!element("#wpt-flash").disabled, "flash button should be enabled before connecting");
              assert(element("#wpt-connect").textContent === "连接 Wingie2", "default language is not Chinese");
              assert(element("#wpt-disconnect").hidden, "disconnect should be hidden before connect");
              assert(snapshot().deviceVersion === null, "device version should be unknown before connect");
              assert(element("#wpt-version-status").textContent.includes("无最新固件包可对比") === false, "unexpected version status");
              assert(element("#wpt-version-status").textContent.includes("设备未识别版本") || element("#wpt-version-status").textContent.includes("可更新"), "pre-connect version status is wrong: " + element("#wpt-version-status").textContent);
              return "PASS";
            })()
            """
        )

    def test_connect_reports_version_and_starts_polling(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              mock.setDeviceVersion("v1.2.3");
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "connection");
              assert(snapshot().deviceVersion === "v1.2.3", "device version not read: " + snapshot().deviceVersion);
              assert(element("#wpt-device-version").textContent === "v1.2.3", "device version label is wrong");
              assert(element("#wpt-version-status").textContent.includes("可更新"), "updatable status missing: " + element("#wpt-version-status").textContent);
              assert(!element("#wpt-flash").disabled, "flash must stay enabled while connected (it auto-disconnects)");
              assert(element("#wpt-midi-start").disabled === false, "MIDI start should be enabled while connected");
              assert(mock.writes.some((request) => request.op === "hello"), "no hello request");
              await waitFor(() => mock.writes.some((request) => request.op === "get_controls"), "first get_controls poll");
              mock.clearWrites();
              await sleep(1200);
              assert(mock.writes.filter((request) => request.op === "get_controls").length >= 2, "500ms polling did not run");
              assert(element("#wpt-midi-note-left").textContent === "C3", "live note left is wrong: " + element("#wpt-midi-note-left").textContent);
              assert(element("#wpt-midi-note-right").textContent === "C4", "live note right is wrong: " + element("#wpt-midi-note-right").textContent);
              return "PASS";
            })()
            """
        )

    def test_uptodate_and_downgrade_status(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              mock.setDeviceVersion("v9.9.9-test");
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "connection");
              assert(element("#wpt-version-status").textContent.includes("已是最新"), "up-to-date status missing: " + element("#wpt-version-status").textContent);
              element("#wpt-disconnect").click();
              await waitFor(() => !snapshot().connected && snapshot().portReleased, "disconnect");
              mock.setDeviceVersion("v10.0.0");
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "reconnect");
              assert(element("#wpt-version-status").textContent.includes("更新"), "newer-than-latest status missing: " + element("#wpt-version-status").textContent);
              return "PASS";
            })()
            """
        )

    def test_midi_send_test_sends_notes_and_passes_on_rx(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "connection");
              element("#wpt-midi-refresh").click();
              await waitFor(() => snapshot().midiOutputCount === 1, "MIDI device list");
              assert(element("#wpt-midi-device").options.length === 1, "device dropdown should list one mock device");
              assert(element("#wpt-midi-device").options[0].textContent === "USB MIDI DevicePort 1", "device name is wrong");
              assert(element("#wpt-midi-send").disabled === false, "send button should be enabled after loading devices");
              mock.clearWrites();
              mock.midi.sent.length = 0;
              element("#wpt-midi-send").click();
              await waitFor(() => !snapshot().midiSendActive && mock.midi.sent.length === 10, "send test completion", 6000);
              const messages = mock.midi.sent.map((entry) => entry.data);
              const noteOns = messages.filter((data) => (data[0] & 0xf0) === 0x90);
              const noteOffs = messages.filter((data) => (data[0] & 0xf0) === 0x80);
              assert(noteOns.length === 5 && noteOffs.length === 5, "expected five note-on and five note-off messages");
              assert(noteOns.every((data, index) => data[1] === [60, 64, 67, 72, 76][index]), "note sequence is wrong");
              assert(element("#wpt-midi-send-status").textContent.includes("通过"), "send test did not pass: " + element("#wpt-midi-send-status").textContent);
              assert(element("#wpt-midi-send-result").textContent.includes("10 条"), "send result summary missing");
              assert(element("#wpt-midi-rx").textContent === "10", "receive counter did not track sent messages");
              return "PASS";
            })()
            """
        )

    def test_midi_send_test_fails_when_rx_lost(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "connection");
              element("#wpt-midi-refresh").click();
              await waitFor(() => snapshot().midiOutputCount === 1, "MIDI device list");
              mock.midi.setAcknowledge(false);
              mock.midi.sent.length = 0;
              element("#wpt-midi-send").click();
              await waitFor(() => !snapshot().midiSendActive && element("#wpt-midi-send-status").textContent.includes("失败"), "send test failure", 8000);
              assert(element("#wpt-midi-send-result").textContent.includes("0 条"), "failure summary missing");
              mock.midi.setAcknowledge(true);
              return "PASS";
            })()
            """
        )

    def test_midi_send_requires_loaded_device(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "connection");
              assert(element("#wpt-midi-send").disabled, "send must stay disabled before devices load");
              assert(element("#wpt-midi-refresh").disabled === false, "refresh must be enabled without devices");
              return "PASS";
            })()
            """
        )

    def test_midi_input_test_passes_and_stops(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "connection");
              mock.clearWrites();
              element("#wpt-midi-start").click();
              await waitFor(() => snapshot().midiTestActive, "MIDI test start");
              assert(element("#wpt-midi-stop").hidden === false, "stop button should be visible during test");
              assert(element("#wpt-midi-start").disabled, "start button should be disabled during test");
              mock.setMidi({midiRx: 1});
              await sleep(700);
              assert(!snapshot().midiTestPassed, "one message must not pass (threshold 2)");
              mock.setMidi({midiRx: 3, note: {left: 64, right: 67}});
              await waitFor(() => snapshot().midiTestPassed, "MIDI test pass");
              assert(!snapshot().midiTestActive, "MIDI test did not stop after passing");
              assert(element("#wpt-midi-status").textContent.includes("通过"), "pass status missing");
              assert(element("#wpt-midi-note-left").textContent === "E4", "note update missing after test: " + element("#wpt-midi-note-left").textContent);
              assert(element("#wpt-midi-rx").textContent === "3", "RX counter label is wrong");
              return "PASS";
            })()
            """
        )

    def test_control_monitor_counts_reset_and_finish_check(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "connection");
              page().setMonitor(true);
              await waitFor(() => snapshot().monitorActive, "monitor enable");
              assert(element("#wpt-counts-reset").disabled === false, "reset should be enabled while monitoring");
              assert(element('[data-control="pot:0"] .wpt-cell-count').textContent === "0", "initial pot count should be zero");
              mock.setCounts({key: {left: [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]}, mode_button: [1, 0], source_switch: 1, pot: [2, 0, 0]});
              mock.setMidi({midiRx: 0});
              await sleep(700);
              assert(element('[data-control="key:0:0"] .wpt-cell-count').textContent === "1", "key count not rendered");
              assert(element('[data-control="pot:0"] .wpt-cell-count').textContent === "2", "pot count not rendered");
              assert(element('[data-control="source"] .wpt-cell-count').textContent === "1", "source count not rendered");
              page().finishCheck();
              assert(snapshot().checkFinished, "finish check did not run");
              assert(element("#wpt-counts-status").textContent.includes("未操作"), "missing-control fail status missing: " + element("#wpt-counts-status").textContent);
              assert(element('[data-control="key:0:1"]').dataset.state === "fail", "zero-count control not marked fail");
              assert(element('[data-control="pot:0"]').dataset.state === "pass", "operated control not marked pass");
              page().resetCounts();
              await waitFor(() => mock.snapshot().counts.pot[0] === 0 && mock.snapshot().counts.key.left[0] === 0, "reset reached the device");
              assert(mock.writes.some((request) => request.op === "get_controls" && request.reset === true), "reset request missing");
              await waitFor(() => element('[data-control="pot:0"] .wpt-cell-count').textContent === "0", "reset not reflected in the table");
              mock.setCounts({key: {left: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1], right: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]}, mode_button: [1, 1], oct_button: {left: [1, 1], right: [1, 1]}, source_switch: 1, pot: [1, 1, 1]});
              await sleep(700);
              page().finishCheck();
              assert(element("#wpt-counts-status").textContent.includes("全部通过"), "all-pass status missing: " + element("#wpt-counts-status").textContent);
              assert(document.querySelectorAll(".wpt-cell[data-state='fail']").length === 0, "fail cells remain after all-pass");
              return "PASS";
            })()
            """
        )

    def test_flash_flow_writes_four_parts_and_disconnects_first(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "connection");
              let confirmation = "";
              window.confirm = (message) => { confirmation = message; return true; };
              mock.clearWrites();
              mock.esptoolLog.length = 0;
              element("#wpt-flash").click();
              await waitFor(() => !snapshot().busy && mock.snapshot().esptoolLog.length >= 5, "flash completion", 5000);
              assert(confirmation.includes("v9.9.9-test") && confirmation.includes("保留"), "confirm message is wrong: " + confirmation);
              assert(!snapshot().connected, "app port was not disconnected before flashing");
              assert(mock.snapshot().esptoolLog.some((entry) => entry.type === "main"), "no ROM handshake");
              const writes = mock.snapshot().esptoolLog.filter((entry) => entry.type === "write");
              assert(writes.length === 4, "expected four image writes, got " + writes.length);
              assert(writes.every((entry) => entry.flashMode === "dio" && entry.flashFreq === "80m" && entry.flashSize === "4MB" && entry.eraseAll === false && entry.compress === true), "unsafe flash options");
              assert(writes.map((entry) => entry.address).join(",") === "4096,32768,57344,65536", "wrong flash addresses");
              assert(element("#wpt-flash-status").textContent.includes("完成"), "flash status not complete");
              assert(element("#wpt-page-status").textContent.includes("连接 Wingie2"), "post-flash hint missing");
              assert(element("#wpt-connect").hidden === false, "connect button should be available after flash");
              return "PASS";
            })()
            """
        )

    def test_flash_downgrade_guard_can_abort(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              mock.setDeviceVersion("v10.0.0");
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "connection");
              let confirmation = "";
              window.confirm = (message) => { confirmation = message; return false; };
              mock.esptoolLog.length = 0;
              element("#wpt-flash").click();
              await waitFor(() => confirmation !== "", "downgrade confirm");
              assert(confirmation.includes("降级"), "downgrade warning missing: " + confirmation);
              await sleep(300);
              assert(mock.snapshot().esptoolLog.filter((entry) => entry.type === "write").length === 0, "aborted flash still wrote images");
              assert(snapshot().connected, "aborted flash disconnected the app port");
              return "PASS";
            })()
            """
        )

    def test_old_firmware_warns_and_stops_polling(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              mock.setLegacyFirmware(true);
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "connection");
              assert(element("#wpt-device-version").textContent.includes("未知"), "legacy firmware version should be unknown: " + element("#wpt-device-version").textContent);
              assert(element("#wpt-version-status").textContent.includes("固件过旧"), "old firmware status missing: " + element("#wpt-version-status").textContent);
              await waitFor(() => element("#wpt-serial-status").textContent.includes("固件过旧"), "get_controls unknown_operation warning");
              assert(element("#wpt-midi-start").disabled, "MIDI test must be disabled on old firmware");
              assert(element("#wpt-monitor-toggle").disabled, "monitor must be disabled on old firmware");
              mock.setLegacyFirmware(false);
              element("#wpt-disconnect").click();
              await waitFor(() => !snapshot().connected && snapshot().portReleased, "disconnect");
              element("#wpt-connect").click();
              await waitFor(() => snapshot().connected, "reconnect with new firmware");
              assert(element("#wpt-midi-start").disabled === false, "MIDI test did not re-enable after reconnect");
              return "PASS";
            })()
            """
        )

    def test_flash_failure_scenarios(self):
        for scenario, expected in (
            ("md5-mismatch-once", "校验失败"),
            ("write-fail-once", "刷机失败"),
            ("boot-fail", "无法进入安装模式"),
            ("wrong-chip", "错误芯片"),
        ):
            with self.subTest(scenario=scenario):
                self.open_scenario(scenario)
                self.evaluate(
                    WAIT_JS + f"""
                    (async () => {{
                      window.confirm = () => true;
                      mock.esptoolLog.length = 0;
                      element("#wpt-flash").click();
                      await waitFor(() => !snapshot().busy, "flash finished for {scenario}", 5000);
                      const statusText = element("#wpt-flash-status").textContent;
                      const alertText = element("#wpt-alert").textContent;
                      assert(statusText.includes("失败") || alertText.includes("{expected}"), "failure not surfaced for {scenario}: status=" + statusText + " alert=" + alertText);
                      return "PASS";
                    }})()
                    """
                )

    def test_english_toggle(self):
        self.open_scenario()
        self.evaluate(
            WAIT_JS + """
            (async () => {
              element("#wpt-language").click();
              assert(element("#wpt-connect").textContent === "Connect Wingie2", "English connect label missing");
              assert(document.body.innerText.includes("Install latest firmware"), "English flash heading missing");
              assert(document.body.innerText.includes("MIDI input test"), "English MIDI heading missing");
              assert(document.body.innerText.includes("Control monitor"), "English control heading missing");
              assert(!document.body.innerText.includes("一键刷入最新固件"), "Chinese leaked after toggle");
              localStorage.removeItem("wpt-lang");
              return "PASS";
            })()
            """
        )


if __name__ == "__main__":
    unittest.main()
