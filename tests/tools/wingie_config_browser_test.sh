#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SESSION="wingie-config-schema4-$$"

cleanup() {
  agent-browser --session "$SESSION" close >/dev/null 2>&1 || true
}
trap cleanup EXIT

agent-browser --session "$SESSION" open --init-script "$ROOT/tests/tools/wingie_serial_mock.js" "file://$ROOT/Tools/wingie_config.html" >/dev/null

agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(async () => {
  const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const waitFor = async (predicate, label, timeout = 6000) => {
    const started = performance.now();
    while (!predicate()) {
      if (performance.now() - started > timeout) throw new Error("Timeout: " + label);
      await sleep(20);
    }
  };
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
  };
  const element = (selector) => document.querySelector(selector);
  const edit = (control, value) => {
    control.focus();
    control.value = value;
    control.dispatchEvent(new InputEvent("input", {bubbles: true, data: value}));
  };

  for (const selector of ["#wg-left-source", "#wg-right-source", "#wg-left-note", "#wg-right-note", "#wg-left-fundamental", "#wg-right-fundamental", "#wg-left-active-bank", "#wg-right-active-bank", "#wg-left-mix-number", "#wg-right-mix-number", "#wg-left-decay-number", "#wg-right-decay-number", "#wg-left-volume-number", "#wg-right-volume-number"]) assert(!element(selector), `obsolete control remains: ${selector}`);
  const mock = window.__wingieSerialMock;
  mock.failNext("hello", "busy");
  element("#wg-connect").click();
  await waitFor(() => window.__wingieConfigTest.state().connected, "schema 4 connection");
  const initialOps = mock.writes.map((request) => request.op);
  assert(JSON.stringify(initialOps) === JSON.stringify([
    "hello", "hello", "get_settings", "get",
    "get_cave", "get_cave", "get_cave", "get_cave", "get_cave", "get_cave"
  ]), "connection did not retry startup busy before one fixed snapshot");
  const initialCount = mock.writes.length;
  await sleep(350);
  assert(mock.writes.length === initialCount, "page polled before the one-second interval");
  assert(element('[data-cave-input="left:0"]').value === "62.00", "Cave values do not show 0.01 Hz precision");
  assert(window.__wingieConfigTest.state().pollTimer !== null, "one-second polling did not start after connection");
  assert(!document.querySelector('[data-side-panel="left"] .wg-help') && !document.querySelector('[data-side-panel="right"] .wg-help'), "channel helper text duplicated the channel title");
  assert(document.querySelector(".wg-footer").textContent === "桌面 Chrome / Edge · Web Serial · HTTPS · 无网络请求 · 每秒后台同步", "Chinese footer was mixed or truncated");
  for (const selector of ["#wg-left-mode", "#wg-left-threshold", '[data-cave-input="left:0"]', '[data-value-key="ratio:0"]', "#wg-a3"]) {
    assert(!element(selector).disabled, `${selector} remained disabled after connection snapshot`);
  }
  assert(element("#wg-language").textContent === "中文 / EN" && element("#wg-left-title").textContent === "左通道 / Left Channel".split(" / ")[0], "Chinese language did not initialize");
  assert(element("#wg-poll-status").textContent === "每秒从设备读取一次完整配置", "Chinese polling status did not show the one-second device read");
  assert(getComputedStyle(element(".wg-connect")).justifyItems === "end", "header controls are not right aligned");
  assert(getComputedStyle(element("#wg-poll-status")).textAlign === "right", "polling status is not right aligned");
  element("#wg-language").click();
  assert(element("#wg-language").textContent === "EN / 中文" && element("#wg-left-title").textContent === "Left Channel", "English language toggle failed");
  assert(element("#wg-poll-status").textContent === "Reading the full device configuration every second", "English polling status did not show the one-second device read");
  assert(document.querySelector("#wg-ratio-title").parentElement.querySelector(".wg-help").textContent === "Both sides share 9 ratios; valid numeric edits reach the running state after 150 ms.", "English Ratio help remained mixed");
  assert(document.querySelector("#wg-shared-title").parentElement.querySelector(".wg-help").textContent === "Global tuning, gain and MIDI routing.", "English Shared Settings help remained mixed");
  assert(document.querySelector(".wg-footer").textContent === "Desktop Chrome / Edge · Web Serial · HTTPS · No network requests · 1-second background sync", "English footer was mixed or truncated");
  element("#wg-language").click();

  window.__wingieConfigTest.stopPolling();
  mock.setSettings({left: {mode: 3}});
  mock.setRatios([0.75, 1, 1.5, 2, 2.5, 3, 4, 5, 7]);
  mock.setCave("left", 0, {
    frequencies: [63.25, 115, 218, 411, 777, 1500, 2800, 5200, 11000],
    mute: [true, false, false, false, false, false, false, false, false]
  });
  mock.clearWrites();
  await window.__wingieConfigTest.poll();
  assert(JSON.stringify(mock.writes.map((request) => request.op)) === JSON.stringify([
    "get_settings", "get",
    "get_cave", "get_cave", "get_cave", "get_cave", "get_cave", "get_cave"
  ]), "background poll did not read one complete snapshot");
  assert(element("#wg-left-mode").value === "3", "background poll did not update external settings");
  assert(element('[data-value-key="ratio:0"]').value === "0.750", "background poll did not update Ratio");
  assert(element('[data-cave-input="left:0"]').value === "63.25", "background poll did not update Cave frequency");
  assert(element('[data-cave-mute="left:0"]').checked, "background poll did not update Cave mute");
  assert(!element('[data-cave-input="left:0"]').disabled && !element('[data-value-key="ratio:0"]').disabled, "background poll locked editable fields");

  mock.clearWrites();
  const pendingThreshold = element("#wg-left-threshold");
  edit(pendingThreshold, "0.33");
  assert(element("#wg-poll-status").textContent === "实时写入期间暂停轮询", "polling status did not show the live-write pause");
  await window.__wingieConfigTest.poll();
  assert(mock.writes.length === 0 && pendingThreshold.value === "0.33", "background poll did not skip a pending edit");
  await window.__wingieConfigTest.idle();
  mock.clearWrites();

  const mode = element("#wg-left-mode");
  mode.value = "2";
  mode.dispatchEvent(new Event("change", {bubbles: true}));
  await waitFor(() => mock.snapshot().settings.left.mode === 2, "immediate mode selection");
  const modeWrite = mock.writes.find((request) => request.op === "set_param" && request.name === "mode");
  assert(modeWrite && modeWrite.target === "left" && modeWrite.value === 2, "set_param shape is wrong");
  assert(!Object.prototype.hasOwnProperty.call(modeWrite, "expected_revision"), "set_param sent an unsupported revision");
})()
JS

agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(async () => {
  const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const waitFor = async (predicate, label, timeout = 6000) => {
    const started = performance.now();
    while (!predicate()) {
      if (performance.now() - started > timeout) throw new Error("Timeout: " + label);
      await sleep(20);
    }
  };
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
  };
  const element = (selector) => document.querySelector(selector);
  const edit = (control, value) => {
    control.focus();
    control.value = value;
    control.dispatchEvent(new InputEvent("input", {bubbles: true, data: value}));
  };
  const mock = window.__wingieSerialMock;

  mock.clearWrites();
  mock.setResponseDelay(100);
  const staleThreshold = element("#wg-left-threshold");
  edit(staleThreshold, "0.4125");
  await waitFor(() => mock.writes.some((request) => request.op === "set_param" && request.name === "threshold"), "first scalar write started");
  edit(staleThreshold, "invalid");
  await waitFor(() => mock.snapshot().settings.left.threshold === 0.4125, "stale scalar device write");
  await waitFor(() => window.__wingieConfigTest.state().settings.left.threshold === 0.4125, "stale scalar acknowledgement updated committed model");
  assert(staleThreshold.value === "invalid", "stale scalar acknowledgement replaced the active invalid edit");
  mock.setResponseDelay(0);
})()
JS

agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(async () => {
  const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const waitFor = async (predicate, label, timeout = 6000) => {
    const started = performance.now();
    while (!predicate()) {
      if (performance.now() - started > timeout) throw new Error("Timeout: " + label);
      await sleep(20);
    }
  };
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
  };
  const element = (selector) => document.querySelector(selector);
  const edit = (control, value) => {
    control.focus();
    control.value = value;
    control.dispatchEvent(new InputEvent("input", {bubbles: true, data: value}));
  };
  const mock = window.__wingieSerialMock;

  mock.clearWrites();
  mock.setResponseDelay(100);
  const saveCount = mock.snapshot().saveCount;
  const ratio = element('[data-value-key="ratio:0"]');
  edit(ratio, "1.1");
  await waitFor(() => mock.writes.some((request) => request.op === "set"), "first Ratio write started");
  edit(ratio, "1.2");
  element("#wg-save").click();
  await waitFor(() => mock.snapshot().saveCount === saveCount + 1, "Save waited for Ratio writes", 9000);
  const ratioAndSave = mock.writes.filter((request) => request.op === "set" || request.op === "save");
  assert(ratioAndSave.length === 3, "rapid Ratio edits were not coalesced into two full writes plus Save");
  assert(ratioAndSave[0].ratios.length === 9 && ratioAndSave[0].ratios[0] === 1.1, "first Ratio snapshot is wrong");
  assert(ratioAndSave[1].ratios.length === 9 && ratioAndSave[1].ratios[0] === 1.2, "latest Ratio snapshot is wrong");
  assert(ratioAndSave[2].op === "save", "Save ran before pending Ratio writes");
  assert(ratio.value === "1.200", "stale Ratio acknowledgement replaced the latest edit");
  mock.setResponseDelay(0);
})()
JS

agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(async () => {
  const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const waitFor = async (predicate, label, timeout = 6000) => {
    const started = performance.now();
    while (!predicate()) {
      if (performance.now() - started > timeout) throw new Error("Timeout: " + label);
      await sleep(20);
    }
  };
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
  };
  const element = (selector) => document.querySelector(selector);
  const edit = (control, value) => {
    control.focus();
    control.value = value;
    control.dispatchEvent(new InputEvent("input", {bubbles: true, data: value}));
  };
  const mock = window.__wingieSerialMock;

  mock.clearWrites();
  const cave = element('[data-cave-input="left:0"]');
  edit(cave, "123.45");
  await waitFor(() => mock.snapshot().cave.left[0].frequencies[0] === 123.45, "debounced full Cave write");
  const caveWrite = mock.writes.find((request) => request.op === "set_cave");
  assert(caveWrite.frequencies.length === 9 && caveWrite.mute.length === 9, "Cave edit was not a full bank write");
  const mute = element('[data-cave-mute="left:0"]');
  mute.checked = true;
  mute.dispatchEvent(new Event("change", {bubbles: true}));
  await waitFor(() => mock.snapshot().cave.left[0].mute[0], "immediate Cave mute");
  await waitFor(() => window.__wingieConfigTest.state().pendingWrites === 0, "Cave mute acknowledgement");
  assert(mock.writes.filter((request) => request.op === "set_cave").length === 2, "mute was not sent immediately as a full bank");

  mock.clearWrites();
  const threshold = element("#wg-left-threshold");
  edit(threshold, "0.33");
  element("#wg-refresh").click();
  await waitFor(() => mock.snapshot().settings.left.threshold === 0.33 && !window.__wingieConfigTest.state().refreshing, "Refresh flushed a pending scalar write");
  const refreshAfterEditOps = mock.writes.map((request) => request.op);
  assert(JSON.stringify(refreshAfterEditOps) === JSON.stringify([
    "set_param", "get_settings", "get",
    "get_cave", "get_cave", "get_cave", "get_cave", "get_cave", "get_cave"
  ]), "Refresh did not flush the pending edit before reading its snapshot");

  mock.clearWrites();
  const caveRevisionBeforeTuning = mock.snapshot().cave.left[0].revision;
  const tuning = element("#wg-tuning");
  tuning.value = "0";
  tuning.dispatchEvent(new Event("change", {bubbles: true}));
  await waitFor(() => window.__wingieConfigTest.state().cave.left[0].revision === caveRevisionBeforeTuning + 1, "Tuning dependency Cave refresh");
  const tuningOps = mock.writes.map((request) => request.op);
  assert(JSON.stringify(tuningOps) === JSON.stringify([
    "set_param", "get_cave", "get_cave", "get_cave", "get_cave", "get_cave", "get_cave"
  ]), "Tuning did not perform exactly one causal Cave refresh");
  mock.clearWrites();
  edit(cave, "234.56");
  await waitFor(() => mock.snapshot().cave.left[0].frequencies[0] === 234.56, "Cave write after Tuning");
  const postTuningCaveWrite = mock.writes.find((request) => request.op === "set_cave");
  assert(postTuningCaveWrite.expected_revision === caveRevisionBeforeTuning + 1, "Cave write used a stale pre-Tuning revision");

  mock.clearWrites();
  mock.failNext("get_cave", "mock_dependency_failure");
  edit(element("#wg-a3"), "441.00");
  await waitFor(() => !window.__wingieConfigTest.state().caveCacheValid, "failed dependency invalidated Cave cache");
  assert(window.__wingieConfigTest.state().settings.shared.a3_hz === 441, "dependency failure rolled back an applied scalar value");
  assert(element('[data-cave-input="left:0"]').disabled, "invalid Cave cache remained editable");
  element("#wg-refresh").click();
  await waitFor(() => window.__wingieConfigTest.state().caveCacheValid && !window.__wingieConfigTest.state().refreshing, "Refresh recovered invalid Cave cache");

  mock.setSettings({source: "hardware", left: {mode: 3}});
  mock.clearWrites();
  element("#wg-refresh").click();
  await waitFor(() => element("#wg-left-mode").value === "3", "manual Refresh snapshot");
  const refreshOps = mock.writes.map((request) => request.op);
  assert(JSON.stringify(refreshOps) === JSON.stringify([
    "get_settings", "get",
    "get_cave", "get_cave", "get_cave", "get_cave", "get_cave", "get_cave"
  ]), "Refresh did not perform exactly one full snapshot");

  let factoryPrompt = "";
  window.confirm = (message) => { factoryPrompt = message; return true; };
  mock.clearWrites();
  element("#wg-factory").click();
  await waitFor(() => mock.snapshot().ratios[0] === 1, "Factory Ratio");
  assert(factoryPrompt.includes("恢复出厂比例？") && !factoryPrompt.includes("Restore Factory Ratio?"), "Chinese Factory confirmation is wrong");
  assert(document.querySelector("#wg-alert").textContent.includes("已恢复出厂比例") && !document.querySelector("#wg-alert").textContent.includes("Factory Ratio restored"), "Chinese Factory success alert is wrong");
  element("#wg-language").click();
  assert(document.querySelector("#wg-alert").textContent.includes("Factory Ratio restored"), "English Factory success alert is wrong");
  element("#wg-language").click();
  const reset = mock.writes.find((request) => request.op === "reset");
  assert(reset && Number.isInteger(reset.expected_revision), "Factory omitted Ratio revision");

  const exported = window.__wingieConfigTest.exportObject();
  exported.settings.right.mode = 1;
  exported.settings.shared.midi.left = 16;
  exported.settings.shared.midi.both = 9;
  exported.ratio.ratios[0] = 0.333;
  exported.cave.right[1].frequencies[0] = 444.44;
  exported.cave.right[1].mute[0] = true;
  await window.__wingieConfigTest.importObject(exported);
  const imported = mock.snapshot();
  assert(imported.settings.right.mode === 1, "import omitted channel settings");
  assert(imported.settings.shared.midi.left === 16, "import omitted shared settings");
  assert(imported.settings.shared.midi.both === 9, `import omitted shared midi_both: ${JSON.stringify(mock.writes.filter((request) => request.op === "set_param"))}`);
  assert(imported.ratios[0] === 0.333, "import omitted Ratio");
  assert(imported.cave.right[1].frequencies[0] === 444.44 && imported.cave.right[1].mute[0], "import omitted Cave");
  assert(!document.querySelector("[id*='apply']"), "Apply control still exists");

  mock.clearWrites();
  mock.disconnect();
  await waitFor(() => !window.__wingieConfigTest.state().connected && !element("#wg-connect").disabled, "unexpected disconnect recovery");
  element("#wg-connect").click();
  await waitFor(() => window.__wingieConfigTest.state().connected, "reconnect after transport loss");
  assert(mock.writes[0] && mock.writes[0].op === "hello", "reconnect did not restart the schema handshake");
})()
JS

agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(async () => {
  const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const waitFor = async (predicate, label, timeout = 6000) => {
    const started = performance.now();
    while (!predicate()) {
      if (performance.now() - started > timeout) throw new Error("Timeout: " + label);
      await sleep(20);
    }
  };
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
  };
  const element = (selector) => document.querySelector(selector);
  const edit = (control, value) => {
    control.focus();
    control.value = value;
    control.dispatchEvent(new InputEvent("input", {bubbles: true, data: value}));
  };
  const mock = window.__wingieSerialMock;

  window.__wingieConfigTest.stopPolling();
  mock.clearWrites();
  mock.setResponseDelay(120);
  const pollPromise = window.__wingieConfigTest.poll();
  await waitFor(() => mock.writes.some((request) => request.op === "get_settings"), "race poll started");
  const ratioInput = element('[data-value-key="ratio:0"]');
  edit(ratioInput, "1.5");
  await pollPromise;
  await window.__wingieConfigTest.idle();
  await waitFor(() => window.__wingieConfigTest.state().ratio.revision === mock.snapshot().ratioRevision, "raced poll rolled back the local ratio revision");
  assert(window.__wingieConfigTest.state().writeErrors.size === 0, "raced poll left a write error");
  assert(!document.querySelector("#wg-alert").textContent.includes("revision_conflict"), "raced poll triggered a revision conflict");
  assert(mock.snapshot().ratios[0] === 1.5, "raced ratio write did not reach the device");
  assert(window.__wingieConfigTest.state().ratio.draft[0] === 1.5, "raced poll overwrote the committed ratio draft");
  mock.setResponseDelay(0);
})()
JS

agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(async () => {
  const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
  const waitFor = async (predicate, label, timeout = 6000) => {
    const started = performance.now();
    while (!predicate()) {
      if (performance.now() - started > timeout) throw new Error("Timeout: " + label);
      await sleep(20);
    }
  };
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
  };
  const element = (selector) => document.querySelector(selector);
  const edit = (control, value) => {
    control.focus();
    control.value = value;
    control.dispatchEvent(new InputEvent("input", {bubbles: true, data: value}));
  };
  const mock = window.__wingieSerialMock;

  window.__wingieConfigTest.stopPolling();
  mock.clearWrites();
  mock.setRatios([0.9, 1, 1.5, 2, 2.5, 3, 4, 5, 7]);
  const ratioInput = element('[data-value-key="ratio:0"]');
  edit(ratioInput, "2.5");
  await waitFor(() => mock.writes.filter((request) => request.op === "set").length === 2, "conflicted ratio write did not retry after resync");
  await window.__wingieConfigTest.idle();
  await waitFor(() => window.__wingieConfigTest.state().ratio.revision === mock.snapshot().ratioRevision, "ratio revision did not resync after conflict");
  const ratioWrites = mock.writes.filter((request) => request.op === "set");
  assert(ratioWrites[1].expected_revision === ratioWrites[0].expected_revision + 1, "ratio conflict retry did not use the resynced revision");
  assert(mock.snapshot().ratios[0] === 2.5, "ratio conflict retry did not apply the user draft");
  assert(window.__wingieConfigTest.state().ratio.draft[0] === 2.5, "ratio conflict resync lost the pending draft");
  assert(window.__wingieConfigTest.state().writeErrors.size === 0, "ratio conflict self-heal left a write error");
  assert(!document.querySelector("#wg-alert").textContent.includes("revision_conflict"), "ratio conflict self-heal surfaced an error alert");

  mock.clearWrites();
  mock.setCave("left", 0, {frequencies: [65, 115, 218, 411, 777, 1500, 2800, 5200, 11000]});
  edit(element('[data-cave-input="left:0"]'), "123.45");
  await waitFor(() => mock.writes.filter((request) => request.op === "set_cave").length === 2, "conflicted cave write did not retry after resync");
  await window.__wingieConfigTest.idle();
  await waitFor(() => window.__wingieConfigTest.state().cave.left[0].revision === mock.snapshot().cave.left[0].revision, "cave revision did not resync after conflict");
  assert(mock.snapshot().cave.left[0].frequencies[0] === 123.45, "cave conflict retry did not apply the user draft");
  assert(window.__wingieConfigTest.state().cave.left[0].draftFrequencies[0] === 123.45, "cave conflict resync lost the pending draft");
  assert(window.__wingieConfigTest.state().writeErrors.size === 0, "cave conflict self-heal left a write error");

  window.confirm = () => true;
  mock.setRatios([0.8, 1, 1.5, 2, 2.5, 3, 4, 5, 7]);
  mock.clearWrites();
  element("#wg-factory").click();
  await waitFor(() => mock.snapshot().ratios[0] === 1, "factory reset did not retry after conflict");
  assert(mock.writes.filter((request) => request.op === "reset").length === 2, "factory reset did not retry exactly once after conflict");
  assert(mock.writes.some((request) => request.op === "get"), "factory reset did not resync before retrying");
  assert(!document.querySelector("#wg-alert").textContent.includes("失败"), "factory reset self-heal surfaced an error alert");

  mock.clearWrites();
  mock.failNext("set", "mock_failure");
  edit(ratioInput, "3.5");
  await waitFor(() => document.querySelector("#wg-alert").textContent.includes("实时写入失败"), "non-conflict failure did not surface the error alert");
  assert(window.__wingieConfigTest.state().writeErrors.size > 0, "non-conflict failure did not record a write error");
})()
JS

agent-browser --session "$SESSION" set viewport 390 844 >/dev/null
agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(() => {
  const assert = (condition, message) => { if (!condition) throw new Error(message); };
  const left = document.querySelector('[data-side-panel="left"]');
  const right = document.querySelector('[data-side-panel="right"]');
  assert(getComputedStyle(left).display !== "none" && getComputedStyle(right).display === "none", "mobile did not start on Left only");
  document.querySelector('[data-mobile-side="right"]').click();
  assert(getComputedStyle(left).display === "none" && getComputedStyle(right).display !== "none", "mobile Right switch failed");
  assert(document.querySelector(".wg-ratio-table").scrollWidth > document.querySelector(".wg-table-wrap").clientWidth, "mobile Ratio table is not horizontally scrollable");
  const sharedRows = ["#wg-a3", "#wg-tuning", "#wg-pre-clip", "#wg-post-clip", "#wg-midi-left", "#wg-midi-right", "#wg-midi-both"].map((selector) => document.querySelector(selector).closest(".wg-field").offsetTop);
  assert(new Set(sharedRows).size === sharedRows.length, "mobile shared settings are not single-column");
})()
JS

agent-browser --session "$SESSION" set viewport 1280 900 >/dev/null
agent-browser --session "$SESSION" eval --stdin <<'JS' >/dev/null
(() => {
  const columns = getComputedStyle(document.querySelector(".wg-channel-grid")).gridTemplateColumns.split(" ");
  if (columns.length !== 2) throw new Error("desktop layout is not two columns");
  const top = (selector) => document.querySelector(selector).closest(".wg-field").offsetTop;
  if (top("#wg-pre-clip") !== top("#wg-post-clip")) throw new Error("clip gains are not on one row");
  if (new Set([top("#wg-midi-left"), top("#wg-midi-right"), top("#wg-midi-both")]).size !== 1) throw new Error("MIDI channels are not on one row");
  if (top("#wg-pre-clip") === top("#wg-midi-left")) throw new Error("clip and MIDI fields share the same row");
})()
JS

printf 'Wingie2 schema 4 configuration browser mock tests passed.\n'
