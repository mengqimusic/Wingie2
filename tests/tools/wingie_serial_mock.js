(() => {
  "use strict";

  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  let controller = null;
  let ratioRevision = 4;
  let ratios = [0.5, 1, 1.5, 2, 2.5, 3, 4, 5, 7];
  let ratioDirty = false;
  const active = {left: 0, right: 1};
  const cave = {left: [], right: []};

  for (const side of ["left", "right"]) {
    for (let bank = 0; bank < 3; bank += 1) {
      cave[side][bank] = {
        frequencies: [62, 115, 218, 411, 777, 1500, 2800, 5200, 11000].map((value) => value + bank * 10 + (side === "right" ? 3 : 0)),
        mute: [false, false, bank === 2, false, false, false, false, false, false],
        revision: bank + (side === "right" ? 10 : 0),
        dirty: false
      };
    }
  }

  function responseFor(request) {
    if (request.op === "hello") {
      return {v: 1, id: request.id, ok: true, op: "hello", device: "Wingie2", capabilities: ["ratio_mode", "cave_config"], config_schema: 1, transport: {baud: 115200, max_frame: 512}};
    }
    if (request.op === "get") {
      return {v: 1, id: request.id, ok: true, op: "get", profile: {ratios, revision: ratioRevision, dirty: ratioDirty}, factory_profile: {ratios: [1, 2, 3, 4, 5, 6, 7, 8, 9]}, limits: {min: 0.125, max: 32, step: 0.001, frequency_min: 16, frequency_max: 16000}};
    }
    if (request.op === "status") {
      return {v: 1, id: request.id, ok: true, op: "status", mode: {left: 4, right: 4}, note: {left: 60, right: 72}, fundamental_hz: {left: 261.626, right: 523.251}, profile_revision: ratioRevision, cave_active_bank: active};
    }
    if (request.op === "get_cave") {
      const bank = cave[request.side][request.bank];
      return {v: 1, id: request.id, ok: true, op: "get_cave", side: request.side, bank: request.bank, active: active[request.side] === request.bank, frequencies: bank.frequencies, mute: bank.mute, revision: bank.revision, dirty: bank.dirty, limits: {min: 8, max: 15999}};
    }
    if (request.op === "set") {
      ratios = request.ratios.slice();
      ratioRevision += 1;
      ratioDirty = true;
      return {v: 1, id: request.id, ok: true, op: "set", state: "queued", revision: ratioRevision};
    }
    if (request.op === "set_cave") {
      const bank = cave[request.side][request.bank];
      bank.frequencies = request.frequencies.slice();
      bank.mute = request.mute.slice();
      bank.revision += 1;
      bank.dirty = true;
      return {v: 1, id: request.id, ok: true, op: "set_cave", state: "queued", revision: bank.revision};
    }
    if (request.op === "reset") {
      ratios = [1, 2, 3, 4, 5, 6, 7, 8, 9];
      ratioRevision += 1;
      ratioDirty = true;
      return {v: 1, id: request.id, ok: true, op: "reset", state: "queued", revision: ratioRevision};
    }
    if (request.op === "save") {
      ratioDirty = false;
      for (const side of ["left", "right"]) for (const bank of cave[side]) bank.dirty = false;
      return {v: 1, id: request.id, ok: true, op: "save", state: "saved"};
    }
    return {v: 1, id: request.id, ok: false, error: {code: "unknown_operation"}};
  }

  const port = {
    readable: null,
    writable: null,
    async open() {
      this.readable = new ReadableStream({
        start(streamController) {
          controller = streamController;
        }
      });
      this.writable = new WritableStream({
        write(chunk) {
          const line = decoder.decode(chunk).trim();
          if (!line.startsWith("@")) return;
          const response = responseFor(JSON.parse(line.slice(1)));
          controller.enqueue(encoder.encode(`<${JSON.stringify(response)}\n`));
        }
      });
    },
    async close() {
      if (controller) controller.close();
      controller = null;
      this.readable = null;
      this.writable = null;
    }
  };

  Object.defineProperty(navigator, "serial", {
    configurable: true,
    value: {
      async requestPort() {
        return port;
      },
      addEventListener() {}
    }
  });
})();
