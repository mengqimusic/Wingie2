(() => {
  "use strict";

  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  const serialListeners = {disconnect: new Set()};
  let controller = null;
  let opened = false;
  let responseDelay = 0;
  let failure = null;
  let saveCount = 0;
  let bootId = 1001;

  const writes = [];
  const factoryRatios = [1, 2, 3, 4, 5, 6, 7, 8, 9];
  let ratios = [0.5, 1, 1.5, 2, 2.5, 3, 4, 5, 7];
  let ratioRevision = 4;
  let ratioDirty = false;

  const runtime = {
    revision: 12,
    dirty: false,
    source: "line",
    shared: {
      a3_hz: 440,
      tuning: -1,
      pre_clip_gain: 0.2475,
      post_clip_gain: 0.825,
      midi: {left: 1, right: 2, both: 3}
    },
    left: {
      mode: 0,
      octave: -1,
      active_cave_bank: 0,
      note: 60,
      fundamental_hz: 261.626,
      mix: 1,
      decay: 0.1,
      volume: 0,
      threshold: 0.4125,
      trigger: false,
      cave_revisions: [0, 1, 2]
    },
    right: {
      mode: 4,
      octave: 0,
      active_cave_bank: 1,
      note: 72,
      fundamental_hz: 523.251,
      mix: 1,
      decay: 0.1,
      volume: 0,
      threshold: 0.4125,
      trigger: true,
      cave_revisions: [10, 11, 12]
    }
  };

  const cave = {left: [], right: []};
  for (const side of ["left", "right"]) {
    for (let bank = 0; bank < 3; bank += 1) {
      cave[side][bank] = {
        frequencies: [62.25, 115.5, 218.75, 411.25, 777.5, 1500.25, 2800.5, 5200.75, 11000.25]
          .map((value) => Number((value + bank * 10 + (side === "right" ? 3 : 0)).toFixed(2))),
        mute: [false, false, bank === 2, false, false, false, false, false, false],
        revision: bank + (side === "right" ? 10 : 0),
        dirty: false
      };
    }
  }

  const parameterSpecs = {
    mode: [0, 4, 1],
    mix: [0, 1, 0.001],
    decay: [0.1, 10, 0.01],
    volume: [0, 1, 0.001],
    threshold: [0.0825, 0.99, 0.0825],
    a3_hz: [358.08, 521.91, 0.01],
    tuning: [-1, 7, 1],
    pre_clip_gain: [0.0825, 0.99, 0.0825],
    post_clip_gain: [0.385, 0.99, 0.055],
    midi_left: [1, 16, 1],
    midi_right: [1, 16, 1],
    midi_both: [1, 16, 1]
  };

  function clone(value) {
    return JSON.parse(JSON.stringify(value));
  }

  function canonicalize(value, minimum, maximum, step) {
    const clipped = Math.min(maximum, Math.max(minimum, Number(value)));
    const canonical = minimum + Math.round((clipped - minimum) / step) * step;
    return Number(Math.min(maximum, Math.max(minimum, canonical)).toFixed(6));
  }

  function emit(payload) {
    if (!controller) return;
    controller.enqueue(encoder.encode(`<${JSON.stringify(payload)}\n`));
  }

  function stateResponse(id) {
    return {
      v: 1,
      id,
      ok: true,
      op: "get_state",
      boot_id: bootId,
      revision: runtime.revision,
      dirty: runtime.dirty,
      source: runtime.source,
      shared: clone(runtime.shared),
      left: clone(runtime.left),
      right: clone(runtime.right),
      ratio_revision: ratioRevision
    };
  }

  function markAllCavesChanged() {
    for (const side of ["left", "right"]) {
      for (let bankIndex = 0; bankIndex < cave[side].length; bankIndex += 1) {
        const bank = cave[side][bankIndex];
        bank.revision += 1;
        bank.dirty = true;
        runtime[side].cave_revisions[bankIndex] = bank.revision;
        runtime.revision += 1;
      }
    }
  }

  function setParameter(request) {
    const spec = parameterSpecs[request.name];
    if (!spec) return {response: {v: 1, id: request.id, ok: false, error: {code: "invalid_parameter"}}};
    if (Number(request.expected_revision) !== runtime.revision) {
      return {response: {v: 1, id: request.id, ok: false, error: {code: "revision_conflict", field: "expected_revision"}}};
    }
    const value = canonicalize(request.value, spec[0], spec[1], spec[2]);
    let previousValue;
    if (request.target === "left" || request.target === "right") {
      if (!["mode", "mix", "decay", "volume", "threshold"].includes(request.name)) {
        return {response: {v: 1, id: request.id, ok: false, error: {code: "invalid_parameter"}}};
      }
      previousValue = runtime[request.target][request.name];
      runtime[request.target][request.name] = value;
    } else if (request.target === "shared") {
      if (request.name.startsWith("midi_")) {
        previousValue = runtime.shared.midi[request.name.slice(5)];
        runtime.shared.midi[request.name.slice(5)] = value;
      } else if (request.name in runtime.shared) {
        previousValue = runtime.shared[request.name];
        runtime.shared[request.name] = value;
      }
      else return {response: {v: 1, id: request.id, ok: false, error: {code: "invalid_parameter"}}};
    } else {
      return {response: {v: 1, id: request.id, ok: false, error: {code: "invalid_parameter"}}};
    }
    const changed = previousValue !== value;
    if (changed && (request.name === "tuning" || (request.name === "a3_hz" && runtime.shared.tuning >= 0))) {
      markAllCavesChanged();
    }
    if (changed) {
      runtime.revision += 1;
      if (!["mix", "decay", "volume"].includes(request.name)) runtime.dirty = true;
    }
    const result = {
      response: {v: 1, id: request.id, ok: true, op: "set_param", value, clipped: value !== request.value, revision: runtime.revision, resource_revision: runtime.revision}
    };
    if (changed) result.event = {v: 1, event: "changed", revision: runtime.revision, origin: "web"};
    return result;
  }

  function responseFor(request) {
    if (failure && (!failure.operation || failure.operation === request.op)) {
      const current = failure;
      failure = null;
      return {response: {v: 1, id: request.id, ok: false, error: {code: current.code || "mock_failure"}}};
    }
    if (request.op === "hello") {
      return {response: {v: 1, id: request.id, ok: true, op: "hello", device: "Wingie2", boot_id: bootId, capabilities: ["realtime_config", "ratio_mode", "cave_config"], config_schema: 2, transport: {baud: 115200, max_frame: 1024}}};
    }
    if (request.op === "get_state") return {response: stateResponse(request.id)};
    if (request.op === "get") {
      return {
        response: {
          v: 1,
          id: request.id,
          ok: true,
          op: "get",
          profile: {ratios: ratios.slice(), revision: ratioRevision, dirty: ratioDirty},
          factory_profile: {ratios: factoryRatios.slice()},
          limits: {min: 0.125, max: 32, step: 0.001, frequency_min: 16, frequency_max: 16000}
        }
      };
    }
    if (request.op === "get_cave") {
      const bank = cave[request.side] && cave[request.side][request.bank];
      if (!bank) return {response: {v: 1, id: request.id, ok: false, error: {code: "invalid_cave_bank"}}};
      return {
        response: {
          v: 1,
          id: request.id,
          ok: true,
          op: "get_cave",
          side: request.side,
          bank: request.bank,
          active: runtime[request.side].active_cave_bank === request.bank,
          frequencies: bank.frequencies.slice(),
          mute: bank.mute.slice(),
          revision: bank.revision,
          dirty: bank.dirty,
          limits: {min: 16, max: 15999.99, step: 0.01}
        }
      };
    }
    if (request.op === "set_param") return setParameter(request);
    if (request.op === "set_ratio_value") {
      if (Number(request.expected_revision) !== ratioRevision) {
        return {response: {v: 1, id: request.id, ok: false, error: {code: "revision_conflict", field: "expected_revision"}}};
      }
      const value = canonicalize(request.value, 0.125, 32, 0.001);
      const changed = ratios[request.index] !== value;
      if (changed) {
        ratios[request.index] = value;
        ratioRevision += 1;
        ratioDirty = true;
        runtime.revision += 1;
        runtime.dirty = true;
      }
      const result = {
        response: {v: 1, id: request.id, ok: true, op: "set_ratio_value", value, clipped: value !== request.value, revision: runtime.revision, resource_revision: ratioRevision}
      };
      if (changed) result.event = {v: 1, event: "changed", revision: runtime.revision, origin: "web"};
      return result;
    }
    if (request.op === "set_cave_value") {
      const bank = cave[request.target] && cave[request.target][request.bank];
      if (!bank) return {response: {v: 1, id: request.id, ok: false, error: {code: "invalid_cave_frequency"}}};
      if (Number(request.expected_revision) !== bank.revision) {
        return {response: {v: 1, id: request.id, ok: false, error: {code: "revision_conflict", field: "expected_revision"}}};
      }
      const value = canonicalize(request.value, 16, 15999.99, 0.01);
      const changed = bank.frequencies[request.index] !== value;
      if (changed) {
        bank.frequencies[request.index] = value;
        bank.revision += 1;
        bank.dirty = true;
        runtime[request.target].cave_revisions[request.bank] = bank.revision;
        runtime.revision += 1;
        runtime.dirty = true;
      }
      const result = {
        response: {v: 1, id: request.id, ok: true, op: "set_cave_value", value, clipped: value !== request.value, revision: runtime.revision, resource_revision: bank.revision}
      };
      if (changed) result.event = {v: 1, event: "changed", revision: runtime.revision, origin: "web"};
      return result;
    }
    if (request.op === "set_cave_mute") {
      const bank = cave[request.target] && cave[request.target][request.bank];
      if (!bank) return {response: {v: 1, id: request.id, ok: false, error: {code: "invalid_cave_mute"}}};
      if (Number(request.expected_revision) !== bank.revision) {
        return {response: {v: 1, id: request.id, ok: false, error: {code: "revision_conflict", field: "expected_revision"}}};
      }
      const value = Boolean(request.mute);
      const changed = bank.mute[request.index] !== value;
      if (changed) {
        bank.mute[request.index] = value;
        bank.revision += 1;
        bank.dirty = true;
        runtime[request.target].cave_revisions[request.bank] = bank.revision;
        runtime.revision += 1;
        runtime.dirty = true;
      }
      const result = {
        response: {v: 1, id: request.id, ok: true, op: "set_cave_mute", mute: value, revision: runtime.revision, resource_revision: bank.revision}
      };
      if (changed) result.event = {v: 1, event: "changed", revision: runtime.revision, origin: "web"};
      return result;
    }
    if (request.op === "reset") {
      if (request.expected_revision !== undefined && Number(request.expected_revision) !== ratioRevision) {
        return {response: {v: 1, id: request.id, ok: false, error: {code: "revision_conflict", field: "expected_revision"}}};
      }
      ratios = factoryRatios.slice();
      ratioRevision += 1;
      ratioDirty = true;
      runtime.revision += 1;
      runtime.dirty = true;
      return {
        response: {v: 1, id: request.id, ok: true, op: "reset", state: "applied", revision: ratioRevision},
        event: {v: 1, event: "changed", revision: runtime.revision, origin: "web"}
      };
    }
    if (request.op === "save") {
      saveCount += 1;
      ratioDirty = false;
      for (const side of ["left", "right"]) for (const bank of cave[side]) bank.dirty = false;
      runtime.revision += 1;
      runtime.dirty = false;
      return {
        response: {v: 1, id: request.id, ok: true, op: "save", state: "saved"},
        event: {v: 1, event: "changed", revision: runtime.revision, origin: "web"}
      };
    }
    return {response: {v: 1, id: request.id, ok: false, error: {code: "unknown_operation"}}};
  }

  async function processChunk(chunk) {
    const lines = decoder.decode(chunk).split("\n").filter(Boolean);
    for (const line of lines) {
      if (!line.startsWith("@")) continue;
      const request = JSON.parse(line.slice(1));
      writes.push(clone(request));
      const result = responseFor(request);
      if (responseDelay) await new Promise((resolve) => window.setTimeout(resolve, responseDelay));
      emit(result.response);
      if (result.event) window.setTimeout(() => emit(result.event), 0);
    }
  }

  const port = {
    readable: null,
    writable: null,
    async open() {
      if (opened) throw new DOMException("Port is already open", "InvalidStateError");
      opened = true;
      this.readable = new ReadableStream({
        start(streamController) {
          controller = streamController;
        }
      });
      this.writable = new WritableStream({
        write: processChunk
      });
    },
    async close() {
      if (controller) {
        try {
          controller.close();
        } catch {
        }
      }
      controller = null;
      opened = false;
      this.readable = null;
      this.writable = null;
    }
  };

  function externalChange(origin, mutate) {
    mutate(runtime);
    runtime.revision += 1;
    runtime.dirty = true;
    emit({v: 1, event: "changed", revision: runtime.revision, origin});
  }

  const serial = {
    async requestPort() {
      return port;
    },
    addEventListener(type, callback) {
      if (serialListeners[type]) serialListeners[type].add(callback);
    },
    removeEventListener(type, callback) {
      if (serialListeners[type]) serialListeners[type].delete(callback);
    }
  };

  Object.defineProperty(navigator, "serial", {
    configurable: true,
    value: serial
  });

  window.__wingieSerialMock = {
    writes,
    clearWrites() {
      writes.length = 0;
    },
    snapshot() {
      return {
        runtime: clone(runtime),
        bootId,
        ratios: ratios.slice(),
        ratioRevision,
        ratioDirty,
        cave: clone(cave),
        saveCount
      };
    },
    hardwareChange(side, values) {
      externalChange("hardware", (target) => {
        Object.assign(target[side], values);
        if (Object.prototype.hasOwnProperty.call(values, "active_cave_bank")) {
          target[side].octave = Number(values.active_cave_bank) - 1;
        }
      });
    },
    midiChange(side, values) {
      externalChange("midi", (target) => Object.assign(target[side], values));
    },
    sharedChange(values, origin = "midi") {
      externalChange(origin, (target) => {
        const tuningChanged = Object.prototype.hasOwnProperty.call(values, "tuning") && values.tuning !== target.shared.tuning;
        const a3Changed = Object.prototype.hasOwnProperty.call(values, "a3_hz") && values.a3_hz !== target.shared.a3_hz;
        if (values.midi) Object.assign(target.shared.midi, values.midi);
        Object.assign(target.shared, {...values, midi: target.shared.midi});
        if (tuningChanged || (a3Changed && target.shared.tuning >= 0)) markAllCavesChanged();
      });
    },
    failNext(operation, code = "mock_failure") {
      failure = {operation, code};
    },
    setResponseDelay(milliseconds) {
      responseDelay = Math.max(0, Number(milliseconds) || 0);
    },
    restart(values = {}) {
      bootId += 1;
      runtime.revision = 0;
      runtime.dirty = false;
      if (values.left) Object.assign(runtime.left, values.left);
      if (values.right) Object.assign(runtime.right, values.right);
      if (values.shared) Object.assign(runtime.shared, values.shared);
    },
    disconnect() {
      for (const listener of serialListeners.disconnect) listener({target: port});
      if (controller) {
        try {
          controller.error(new DOMException("Mock disconnect", "NetworkError"));
        } catch {
        }
      }
      controller = null;
      opened = false;
      port.readable = null;
      port.writable = null;
    }
  };
})();
