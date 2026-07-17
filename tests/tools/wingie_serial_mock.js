(() => {
  "use strict";

  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  const factoryRatios = [1, 2, 3, 4, 5, 6, 7, 8, 9];
  const writes = [];
  let controller = null;
  let opened = false;
  let inputBuffer = "";
  let responseDelay = 0;
  let failure = null;
  let saveCount = 0;
  let ratioRevision = 4;
  let ratios = [0.5, 1, 1.5, 2, 2.5, 3, 4, 5, 7];
  let ratioDirty = false;
  let settingsDirty = false;
  let legacyFirmware = false;

  const settings = {
    source: "startup",
    left: {mode: 4, mix: 0.25, decay: 4.5, volume: 0.5, threshold: 0.165},
    right: {mode: 3, mix: 0.75, decay: 5.5, volume: 0.6, threshold: 0.2475},
    shared: {
      a3_hz: 440,
      tuning: -1,
      pre_clip_gain: 0.495,
      post_clip_gain: 0.55,
      midi: {left: 13, right: 14, both: 15}
    }
  };

  const status = {
    mode: {left: 4, right: 3},
    note: {left: 60, right: 72},
    fundamental_hz: {left: 261.626, right: 523.251},
    cave_active_bank: {left: 0, right: 1}
  };

  const cave = {left: [], right: []};
  for (const side of ["left", "right"]) {
    for (let bank = 0; bank < 3; bank += 1) {
      cave[side][bank] = {
        frequencies: [62, 115, 218, 411, 777, 1500, 2800, 5200, 11000].map((value) => value + bank * 10 + (side === "right" ? 3 : 0)),
        mute: [false, false, bank === 2, false, false, false, false, false, false],
        revision: bank + 1 + (side === "right" ? 10 : 0),
        dirty: false
      };
    }
  }

  function clone(value) {
    return JSON.parse(JSON.stringify(value));
  }

  function decimalsFor(step) {
    const text = String(step);
    return text.includes(".") ? text.length - text.indexOf(".") - 1 : 0;
  }

  function canonicalize(value, min, max, step) {
    const clipped = Math.min(max, Math.max(min, Number(value)));
    const steps = Math.round((clipped - min) / step);
    return Number((min + steps * step).toFixed(decimalsFor(step)));
  }

  function parameterSpec(target, name) {
    const specs = {
      left: {
        mode: [0, 4, 1, true], mix: [0, 1, 0.001, false], decay: [0.1, 10, 0.01, false],
        volume: [0, 1, 0.001, false], threshold: [0.0825, 0.99, 0.0825, true]
      },
      right: {
        mode: [0, 4, 1, true], mix: [0, 1, 0.001, false], decay: [0.1, 10, 0.01, false],
        volume: [0, 1, 0.001, false], threshold: [0.0825, 0.99, 0.0825, true]
      },
      shared: {
        a3_hz: [358.08, 521.91, 0.01, true], tuning: [-1, 7, 1, true],
        pre_clip_gain: [0.0825, 0.99, 0.0825, true], post_clip_gain: [0.385, 0.99, 0.055, true],
        midi_left: [1, 16, 1, true], midi_right: [1, 16, 1, true], midi_both: [1, 16, 1, true]
      }
    };
    return specs[target] && specs[target][name];
  }

  function retuneCaves() {
    for (const side of ["left", "right"]) {
      for (const bank of cave[side]) {
        bank.frequencies = bank.frequencies.map((value) => canonicalize(value + 0.01, 16, 16000, 0.01));
        bank.revision += 1;
        bank.dirty = true;
      }
    }
  }

  function setParameter(request) {
    const spec = parameterSpec(request.target, request.name);
    if (!spec) return {v: 1, id: request.id, ok: false, error: {code: "invalid_param", field: "name"}};
    const value = canonicalize(request.value, spec[0], spec[1], spec[2]);
    const previousTuning = settings.shared.tuning;
    if (request.target === "shared" && request.name.startsWith("midi_")) settings.shared.midi[request.name.slice(5)] = value;
    else settings[request.target][request.name] = value;
    settings.source = "web";
    if (spec[3]) settingsDirty = true;
    const cavesChanged = request.target === "shared" &&
      ((request.name === "tuning" && value !== previousTuning) ||
       (request.name === "a3_hz" && settings.shared.tuning >= 0));
    if (cavesChanged) retuneCaves();
    return {v: 1, id: request.id, ok: true, op: "set_param", target: request.target, name: request.name, value, dirty: settingsDirty, caves_changed: cavesChanged};
  }

  function failIfRequested(request) {
    if (!failure || failure.operation !== request.op) return null;
    const code = failure.code;
    failure = null;
    return {v: 1, id: request.id, ok: false, error: {code}};
  }

  function responseFor(request) {
    const failed = failIfRequested(request);
    if (failed) return failed;
    if (request.op === "hello") {
      return {v: 1, id: request.id, ok: true, op: "hello", device: "Wingie2", capabilities: ["ratio_mode", "cave_config", "settings", "mpe"], config_schema: 4, transport: {baud: 115200, max_frame: 512}};
    }
    if (request.op === "get_settings") {
      return {v: 1, id: request.id, ok: true, op: "get_settings", source: settings.source, dirty: settingsDirty, left: clone(settings.left), right: clone(settings.right), shared: clone(settings.shared)};
    }
    if (request.op === "get") {
      return {v: 1, id: request.id, ok: true, op: "get", profile: {ratios: ratios.slice(), revision: ratioRevision, dirty: ratioDirty}, factory_profile: {ratios: factoryRatios.slice()}, limits: {min: 0.125, max: 32, step: 0.001, frequency_min: 16, frequency_max: 16000}};
    }
    if (request.op === "status") {
      const response = {v: 1, id: request.id, ok: true, op: "status", ...clone(status), profile_revision: ratioRevision};
      if (!legacyFirmware) response.cave_revision = {left: cave.left.map((bank) => bank.revision), right: cave.right.map((bank) => bank.revision)};
      return response;
    }
    if (request.op === "get_cave") {
      const bank = cave[request.side] && cave[request.side][request.bank];
      if (!bank) return {v: 1, id: request.id, ok: false, error: {code: "invalid_cave_bank"}};
      return {v: 1, id: request.id, ok: true, op: "get_cave", side: request.side, bank: request.bank, active: status.cave_active_bank[request.side] === request.bank, frequencies: bank.frequencies.slice(), mute: bank.mute.slice(), revision: bank.revision, dirty: bank.dirty, limits: {min: 16, max: 16000, step: 0.01}};
    }
    if (request.op === "set_param") return setParameter(request);
    if (request.op === "set") {
      if (Number(request.expected_revision) !== ratioRevision) return {v: 1, id: request.id, ok: false, error: {code: "revision_conflict", field: "expected_revision"}};
      if (!Array.isArray(request.ratios) || request.ratios.length !== 9) return {v: 1, id: request.id, ok: false, error: {code: "invalid_ratio"}};
      ratios = request.ratios.map((value) => canonicalize(value, 0.125, 32, 0.001));
      ratioRevision += 1;
      ratioDirty = true;
      return {v: 1, id: request.id, ok: true, op: "set", state: "queued", revision: ratioRevision};
    }
    if (request.op === "set_cave") {
      const bank = cave[request.side] && cave[request.side][request.bank];
      if (!bank) return {v: 1, id: request.id, ok: false, error: {code: "invalid_cave_bank"}};
      if (Number(request.expected_revision) !== bank.revision) return {v: 1, id: request.id, ok: false, error: {code: "revision_conflict", field: "expected_revision"}};
      if (!Array.isArray(request.frequencies) || request.frequencies.length !== 9 || !Array.isArray(request.mute) || request.mute.length !== 9) return {v: 1, id: request.id, ok: false, error: {code: "invalid_cave"}};
      bank.frequencies = request.frequencies.map((value) => canonicalize(value, 16, 16000, 0.01));
      bank.mute = request.mute.map(Boolean);
      bank.revision += 1;
      bank.dirty = true;
      return {v: 1, id: request.id, ok: true, op: "set_cave", state: "queued", revision: bank.revision};
    }
    if (request.op === "reset") {
      if (Number(request.expected_revision) !== ratioRevision) return {v: 1, id: request.id, ok: false, error: {code: "revision_conflict", field: "expected_revision"}};
      ratios = factoryRatios.slice();
      ratioRevision += 1;
      ratioDirty = true;
      return {v: 1, id: request.id, ok: true, op: "reset", state: "queued", revision: ratioRevision};
    }
    if (request.op === "save") {
      saveCount += 1;
      settingsDirty = false;
      ratioDirty = false;
      for (const side of ["left", "right"]) for (const bank of cave[side]) bank.dirty = false;
      return {v: 1, id: request.id, ok: true, op: "save", state: "saved"};
    }
    return {v: 1, id: request.id, ok: false, error: {code: "unknown_operation"}};
  }

  function emit(response) {
    if (controller) controller.enqueue(encoder.encode(`<${JSON.stringify(response)}\n`));
  }

  async function processChunk(chunk) {
    inputBuffer += decoder.decode(chunk, {stream: true});
    let newline = inputBuffer.indexOf("\n");
    while (newline >= 0) {
      const line = inputBuffer.slice(0, newline).trim();
      inputBuffer = inputBuffer.slice(newline + 1);
      if (line.startsWith("@")) {
        const request = JSON.parse(line.slice(1));
        writes.push(clone(request));
        const response = responseFor(request);
        if (responseDelay) await new Promise((resolve) => window.setTimeout(resolve, responseDelay));
        emit(response);
      }
      newline = inputBuffer.indexOf("\n");
    }
  }

  const port = {
    readable: null,
    writable: null,
    async open() {
      if (opened) throw new DOMException("Port is already open", "InvalidStateError");
      opened = true;
      this.readable = new ReadableStream({start(streamController) { controller = streamController; }});
      this.writable = new WritableStream({write: processChunk});
    },
    async close() {
      if (controller) {
        try { controller.close(); } catch {
        }
      }
      controller = null;
      opened = false;
      inputBuffer = "";
      this.readable = null;
      this.writable = null;
    }
  };

  Object.defineProperty(navigator, "serial", {
    configurable: true,
    value: {async requestPort() { return port; }}
  });

  window.__wingieSerialMock = {
    writes,
    clearWrites() { writes.length = 0; },
    snapshot() {
      return {opened, settings: clone(settings), settingsDirty, status: clone(status), ratios: ratios.slice(), ratioRevision, ratioDirty, cave: clone(cave), saveCount};
    },
    setSettings(values) {
      if (values.source !== undefined) settings.source = String(values.source);
      if (values.left) Object.assign(settings.left, values.left);
      if (values.right) Object.assign(settings.right, values.right);
      if (values.shared) {
        if (values.shared.midi) Object.assign(settings.shared.midi, values.shared.midi);
        Object.assign(settings.shared, {...values.shared, midi: settings.shared.midi});
      }
      if (values.dirty !== undefined) settingsDirty = Boolean(values.dirty);
    },
    setRatios(values, dirty = true) {
      ratios = values.map(Number);
      ratioRevision += 1;
      ratioDirty = Boolean(dirty);
    },
    setCave(side, bank, values) {
      const target = cave[side][bank];
      if (values.frequencies) target.frequencies = values.frequencies.map(Number);
      if (values.mute) target.mute = values.mute.map(Boolean);
      if (values.dirty !== undefined) target.dirty = Boolean(values.dirty);
      target.revision += 1;
    },
    setStatus(values) {
      if (values.mode) Object.assign(status.mode, values.mode);
      if (values.note) Object.assign(status.note, values.note);
      if (values.fundamental_hz) Object.assign(status.fundamental_hz, values.fundamental_hz);
      if (values.cave_active_bank) Object.assign(status.cave_active_bank, values.cave_active_bank);
    },
    failNext(operation, code = "mock_failure") { failure = {operation, code}; },
    setResponseDelay(milliseconds) { responseDelay = Math.max(0, Number(milliseconds) || 0); },
    setLegacyFirmware(value) { legacyFirmware = Boolean(value); },
    disconnect() {
      if (controller) {
        try { controller.error(new DOMException("Mock disconnect", "NetworkError")); } catch {
        }
      }
      controller = null;
      opened = false;
      port.readable = null;
      port.writable = null;
    }
  };
})();
