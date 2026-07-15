#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SESSION="wingie-config-browser-test-$$"

cleanup() {
  agent-browser --session "$SESSION" close >/dev/null 2>&1 || true
}
trap cleanup EXIT

agent-browser --session "$SESSION" open --init-script "$ROOT/tests/tools/wingie_serial_mock.js" "file://$ROOT/Tools/wingie_config.html" >/dev/null

agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(async () => {
  const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const waitFor = async (predicate, label, timeout = 4000) => {
    const started = performance.now();
    while (!predicate()) {
      if (performance.now() - started > timeout) throw new Error("Timeout: " + label);
      await sleep(20);
    }
  };
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
  };
  const input = (selector) => document.querySelector(selector);
  const edit = (control, value) => {
    control.focus();
    control.value = value;
    control.dispatchEvent(new InputEvent("input", {bubbles: true, data: value}));
  };

  input("#wg-connect").click();
  await waitFor(() => window.__wingieConfigTest.state().connected, "initial connection");
  const mock = window.__wingieSerialMock;

  const ratio = input('[data-value-key="ratio:0"]');
  edit(ratio, "1");
  ratio.value += ".";
  ratio.dispatchEvent(new InputEvent("input", {bubbles: true, data: "."}));
  assert(ratio.value === "1.", "partial decimal was rewritten");
  ratio.value += "2";
  ratio.dispatchEvent(new InputEvent("input", {bubbles: true, data: "2"}));
  assert(ratio.value === "1.2", "decimal typing failed");
  ratio.blur();
  await waitFor(() => mock.snapshot().ratios[0] === 1.2, "decimal ratio write");

  edit(ratio, "100");
  ratio.blur();
  await waitFor(() => mock.snapshot().ratios[0] === 32, "ratio clipping");
  assert(ratio.value === "32", "ratio clipping was not rendered after blur");

  edit(ratio, "");
  await sleep(175);
  assert(ratio.value === "", "focused empty input was rewritten");
  ratio.blur();
  await waitFor(() => ratio.value === "32" && mock.snapshot().ratios[0] === 32, "empty input restore");

  const cave = input('[data-cave-input="left:0"]');
  edit(cave, "15.004");
  cave.blur();
  await waitFor(() => mock.snapshot().cave.left[0].frequencies[0] === 16, "cave clipping");
  assert(cave.value === "16", "cave clipping was not rendered");

  mock.hardwareChange("left", {mode: 3, note: 48, fundamental_hz: 130.813, active_cave_bank: 2, trigger: true});
  await waitFor(() => input("#wg-left-mode").value === "3", "hardware mode change");
  assert(input('[data-bank-tabs="left"] [data-bank="2"]').getAttribute("aria-pressed") === "true", "active Cave bank did not follow hardware");
  assert(input('[data-cave-input="left:0"]').value === "82.25", "active Cave bank values did not render");

  mock.midiChange("right", {mode: 1, note: 65, fundamental_hz: 349.228, mix: 0.37});
  await waitFor(() => input("#wg-right-mode").value === "1" && input("#wg-right-mix-number").value === "0.37", "MIDI channel change");
  mock.sharedChange({tuning: 2, midi: {left: 14, right: 15, both: 16}}, "midi");
  await waitFor(() => input("#wg-midi-left").value === "14" && input("#wg-tuning").value === "2", "shared MIDI change");

  mock.setResponseDelay(250);
  const staleRuntime = {
    ...structuredClone(window.__wingieConfigTest.state().runtime),
    boot_id: window.__wingieConfigTest.state().bootId
  };
  const staleRefresh = window.__wingieConfigTest.refresh();
  await sleep(40);
  mock.hardwareChange("left", {mode: 4});
  await staleRefresh;
  assert(window.__wingieConfigTest.state().runtime.revision < window.__wingieConfigTest.state().observedRevision, "stale get_state response was applied");
  await waitFor(() => input("#wg-left-mode").value === "4", "stale state retry");
  assert(!window.__wingieConfigTest.applyRuntimePayload(staleRuntime), "old runtime payload was accepted");
  assert(input("#wg-left-mode").value === "4", "old runtime payload rolled hardware mode backward");
  mock.setResponseDelay(0);

  const previousBootId = window.__wingieConfigTest.state().bootId;
  mock.restart({left: {mode: 2}});
  await waitFor(() => window.__wingieConfigTest.state().bootId !== previousBootId, "boot epoch change");
  await waitFor(() => window.__wingieConfigTest.state().runtime.revision === 0 && input("#wg-left-mode").value === "2", "same-port reboot state");

  mock.clearWrites();
  mock.setResponseDelay(250);
  const rapid = input('[data-value-key="ratio:1"]');
  edit(rapid, "1.1");
  await sleep(175);
  edit(rapid, "1.2");
  edit(rapid, "1.234");
  rapid.blur();
  await waitFor(() => mock.snapshot().ratios[1] === 1.234, "rapid ratio write");
  const rapidWrites = mock.writes
    .filter((request) => request.op === "set_ratio_value" && request.index === 1)
    .map((request) => request.value);
  assert(JSON.stringify(rapidWrites) === JSON.stringify([1.1, 1.234]), "stale rapid edit was not coalesced");
  assert(rapid.value === "1.234", "old acknowledgement overwrote a newer edit");
  mock.setResponseDelay(0);
})()
JS

agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(async () => {
  const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const waitFor = async (predicate, label, timeout = 4000) => {
    const started = performance.now();
    while (!predicate()) {
      if (performance.now() - started > timeout) throw new Error("Timeout: " + label);
      await sleep(20);
    }
  };
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
  };
  const input = (selector) => document.querySelector(selector);
  const edit = (control, value) => {
    control.focus();
    control.value = value;
    control.dispatchEvent(new InputEvent("input", {bubbles: true, data: value}));
  };
  const mock = window.__wingieSerialMock;

  let saveCount = mock.snapshot().saveCount;
  window.confirm = () => false;
  input("#wg-save").click();
  await sleep(50);
  assert(mock.snapshot().saveCount === saveCount, "cancelled save wrote flash");

  mock.clearWrites();
  mock.setResponseDelay(120);
  const queued = input('[data-value-key="ratio:2"]');
  edit(queued, "2.345");
  queued.blur();
  window.confirm = () => true;
  input("#wg-save").click();
  await waitFor(() => mock.snapshot().saveCount === saveCount + 1, "confirmed save");
  const ordered = mock.writes
    .filter((request) => request.op === "set_ratio_value" || request.op === "save")
    .map((request) => request.op);
  assert(JSON.stringify(ordered) === JSON.stringify(["set_ratio_value", "save"]), "save did not wait for live writes");
  mock.setResponseDelay(0);

  const mix = input("#wg-left-mix-number");
  edit(mix, "0.501");
  mix.blur();
  await waitFor(() => mock.snapshot().runtime.left.mix === 0.501, "runtime-only mix write");
  assert(!mock.snapshot().runtime.dirty, "runtime-only performance control became flash-dirty");
  assert(!window.__wingieConfigTest.state().runtime.dirty, "page marked runtime-only control as flash-dirty");

  saveCount = mock.snapshot().saveCount;
  mock.failNext("set_ratio_value", "invalid_ratio");
  edit(queued, "2.777");
  queued.blur();
  input("#wg-save").click();
  await waitFor(() => input("#wg-alert").textContent.includes("保存失败"), "failed live write blocks save");
  assert(mock.snapshot().saveCount === saveCount, "save continued after a failed live write");
})()
JS

agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(async () => {
  const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const waitFor = async (predicate, label, timeout = 4000) => {
    const started = performance.now();
    while (!predicate()) {
      if (performance.now() - started > timeout) throw new Error("Timeout: " + label);
      await sleep(20);
    }
  };
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
  };
  const input = (selector) => document.querySelector(selector);
  const edit = (control, value) => {
    control.focus();
    control.value = value;
    control.dispatchEvent(new InputEvent("input", {bubbles: true, data: value}));
  };
  const mock = window.__wingieSerialMock;
  const ratio = input('[data-value-key="ratio:0"]');

  const exported = window.__wingieConfigTest.exportObject();
  exported.channels.left.mode = 2;
  exported.shared.midi.left = 16;
  exported.ratio.ratios[0] = 0.333;
  exported.cave.right[1].frequencies[0] = 123.45;
  exported.cave.right[1].mute[0] = true;
  mock.sharedChange({tuning: -1}, "hardware");
  await waitFor(() => input("#wg-tuning").value === "-1", "standard tuning before import");
  mock.clearWrites();
  await window.__wingieConfigTest.importObject(exported);
  const imported = mock.snapshot();
  assert(imported.runtime.left.mode === 2, "import omitted channel mode");
  assert(imported.runtime.shared.midi.left === 16, "import omitted shared MIDI");
  assert(imported.ratios[0] === 0.333, "import omitted Ratio");
  assert(imported.cave.right[1].frequencies[0] === 123.45 && imported.cave.right[1].mute[0], "import omitted Cave");
  assert(mock.writes.filter((request) => request.op === "set_param").every((request) => Number.isInteger(request.expected_revision)), "scalar import omitted expected revision");

  mock.failNext("set_ratio_value", "invalid_ratio");
  edit(ratio, "0.777");
  ratio.blur();
  await waitFor(() => input("#wg-alert").textContent.includes("实时写入失败"), "write failure feedback");
  assert(mock.snapshot().ratios[0] === 0.333 && ratio.value === "0.333", "failed write did not restore acknowledged value");

  mock.disconnect();
  await waitFor(() => !window.__wingieConfigTest.state().connected, "disconnect");
  assert(input("#wg-left-mode").disabled, "controls stayed enabled after disconnect");
  input("#wg-connect").click();
  await waitFor(() => window.__wingieConfigTest.state().connected, "reconnect");
})()
JS

agent-browser --session "$SESSION" set viewport 640 900 >/dev/null
agent-browser --session "$SESSION" eval '(() => { if (getComputedStyle(document.querySelector(".wg-channel-grid")).gridTemplateColumns.includes(" ")) throw new Error("narrow layout did not stack"); return true; })()' >/dev/null
agent-browser --session "$SESSION" set viewport 1280 900 >/dev/null
agent-browser --session "$SESSION" eval '(() => { if (!getComputedStyle(document.querySelector(".wg-channel-grid")).gridTemplateColumns.includes(" ")) throw new Error("desktop layout is not two columns"); return true; })()' >/dev/null

printf 'Wingie2 configuration browser mock tests passed.\n'
