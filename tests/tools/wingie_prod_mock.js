(() => {
  "use strict";

  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  const writes = [];
  let controller = null;
  let opened = false;
  let inputBuffer = "";
  let responseDelay = 0;
  let failure = null;
  let firmwareVersion = "dev";
  let legacyFirmware = false;

  const counts = {
    key: {left: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], right: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]},
    mode_button: [0, 0],
    oct_button: {left: [0, 0], right: [0, 0]},
    source_switch: 0,
    pot: [0, 0, 0]
  };

  const live = {
    midiRx: 0,
    note: {left: 48, right: 60}
  };

  const imageSpecs = [
    {name: "bootloader", path: "Wingie2-v9.9.9.bootloader.bin", offset: 0x1000, length: 512, seed: 0x11},
    {name: "partitions", path: "Wingie2-v9.9.9.partitions.bin", offset: 0x8000, length: 256, seed: 0x22},
    {name: "boot_app0", path: "boot_app0-v9.9.9.bin", offset: 0xe000, length: 128, seed: 0x33},
    {name: "app", path: "Wingie2-v9.9.9.bin", offset: 0x10000, length: 2048, seed: 0x44}
  ];
  const images = new Map();
  const failureCount = new Map();
  const esptoolLog = [];

  for (const spec of imageSpecs) {
    const bytes = new Uint8Array(spec.length);
    for (let index = 0; index < bytes.length; index += 1) bytes[index] = (spec.seed + index * 13) & 0xff;
    images.set(spec.path, bytes);
  }

  function scenario() {
    return new URLSearchParams(location.search).get("scenario") || "current";
  }

  function hex(bytes) {
    return Array.from(bytes, value => value.toString(16).padStart(2, "0")).join("");
  }

  async function sha256(bytes) {
    return hex(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)));
  }

  async function manifest() {
    const parts = [];
    for (const spec of imageSpecs) {
      const bytes = images.get(spec.path);
      parts.push({
        name: spec.name,
        path: spec.path,
        offset: spec.offset,
        size: bytes.length,
        sha256: await sha256(bytes)
      });
    }
    return {
      schema: 1,
      name: "Wingie2 Mock Firmware",
      version: "v9.9.9-test",
      chipFamily: "ESP32",
      esptoolJs: "0.6.0",
      flash: {mode: "dio", frequency: "80m", size: "4MB", eraseAll: false},
      preserve: [{name: "nvs", offset: 0x9000, size: 0x5000}],
      parts
    };
  }

  const originalFetch = window.fetch.bind(window);
  window.fetch = async (input, init) => {
    const href = typeof input === "string" ? input : input.href || input.url;
    const url = new URL(href, location.href);
    const path = decodeURIComponent(url.pathname.split("/").pop());
    if (path === "manifest.json") {
      return new Response(JSON.stringify(await manifest()), {
        status: 200,
        headers: {"Content-Type": "application/json"}
      });
    }
    if (images.has(path)) {
      return new Response(images.get(path).slice(), {status: 200});
    }
    return originalFetch(input, init);
  };

  function clone(value) {
    return JSON.parse(JSON.stringify(value));
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
      const response = {
        v: 1, id: request.id, ok: true, op: "hello", device: "Wingie2",
        capabilities: ["settings", "ratio_mode", "cave_config", "mpe"], config_schema: 5,
        transport: {baud: 115200, max_frame: 512}
      };
      if (!legacyFirmware) response.firmware = firmwareVersion;
      return response;
    }
    if (request.op === "get_controls") {
      if (legacyFirmware) return {v: 1, id: request.id, ok: false, error: {code: "unknown_operation"}};
      if (request.reset === true) {
        counts.key.left = counts.key.left.map(() => 0);
        counts.key.right = counts.key.right.map(() => 0);
        counts.mode_button = [0, 0];
        counts.oct_button.left = [0, 0];
        counts.oct_button.right = [0, 0];
        counts.source_switch = 0;
        counts.pot = [0, 0, 0];
      }
      return {
        v: 1, id: request.id, ok: true, op: "get_controls",
        midi_rx: live.midiRx,
        note: clone(live.note),
        counts: clone(counts)
      };
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
      const readable = this.readable;
      const writable = this.writable;
      while ((readable && readable.locked) || (writable && writable.locked)) {
        await new Promise((resolve) => setTimeout(resolve, 5));
      }
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
    value: {
      async requestPort() {
        const current = scenario();
        if (current === "no-port") throw new DOMException("No port selected", "NotFoundError");
        if (current === "port-busy") throw new DOMException("Port is already open", "NetworkError");
        return port;
      },
      addEventListener() {}
    }
  });

  class MockTransport {
    constructor(selectedPort, tracing) {
      this.port = selectedPort;
    }

    async setDTR(value) {}
    async setRTS(value) {}

    async disconnect() {}
  }

  class MockESPLoader {
    constructor(options) {
      this.options = options;
      this.chip = null;
      esptoolLog.push({type: "loader", baudrate: options.baudrate});
    }

    async main(mode) {
      const current = scenario();
      esptoolLog.push({type: "main", mode, scenario: current});
      if (current === "boot-fail") throw new Error("Failed to connect to ESP32: Timed out waiting for packet header");
      this.chip = {CHIP_NAME: current === "wrong-chip" ? "ESP32-S3" : "ESP32"};
      return current === "wrong-chip" ? "ESP32-S3 (revision 0)" : "ESP32-D0WDQ6 (revision 1)";
    }

    async writeFlash(options) {
      const address = options.fileArray[0].address;
      const current = scenario();
      esptoolLog.push({
        type: "write",
        address,
        flashMode: options.flashMode,
        flashFreq: options.flashFreq,
        flashSize: options.flashSize,
        eraseAll: options.eraseAll,
        compress: options.compress
      });
      options.reportProgress(0, 0, options.fileArray[0].data.length);
      options.reportProgress(0, options.fileArray[0].data.length, options.fileArray[0].data.length);

      const key = `${current}:${address}`;
      const failures = failureCount.get(key) || 0;
      if (current === "md5-mismatch-once" && address === 0x8000 && failures === 0) {
        failureCount.set(key, failures + 1);
        throw new Error("MD5 of file does not match data in flash!");
      }
      if (current === "write-fail-once" && address === 0xe000 && failures === 0) {
        failureCount.set(key, failures + 1);
        throw new Error("Serial data write failed");
      }
    }

    async after(mode) {}
  }

  function mockMd5(bytes) {
    let sum = 0;
    for (const value of bytes) sum = (sum + value) >>> 0;
    return sum.toString(16).padStart(32, "0").slice(-32);
  }

  // ---- Web MIDI mock ----

  const midiOutputs = new Map();
  const midiSent = [];
  let midiAcknowledge = true;

  function installMidiAccess() {
    const access = {
      outputs: midiOutputs,
      addEventListener() {}
    };
    Object.defineProperty(navigator, "requestMIDIAccess", {
      configurable: true,
      value: async () => access
    });
    return access;
  }

  navigator.requestMIDIAccess = navigator.requestMIDIAccess || (() => Promise.reject(new Error("not installed")));

  function mockOutput(id, name) {
    return {
      id,
      name,
      send(data) {
        midiSent.push({id, data: Array.from(data)});
        if (midiAcknowledge) {
          if (data[0] === 0x80 || (data[0] & 0xf0) === 0x80) live.midiRx += 1;
          if ((data[0] & 0xf0) === 0x90) live.midiRx += 1;
        }
      }
    };
  }

  window.__WINGIE_PROD_MOCK__ = {
    esptool: {Transport: MockTransport, ESPLoader: MockESPLoader},
    md5: mockMd5,
    writes,
    clearWrites() { writes.length = 0; },
    setDeviceVersion(value) { firmwareVersion = String(value); },
    setLegacyFirmware(value) { legacyFirmware = Boolean(value); },
    setCounts(values) {
      if (values.key) {
        if (values.key.left) counts.key.left = values.key.left.map(Number);
        if (values.key.right) counts.key.right = values.key.right.map(Number);
      }
      if (values.mode_button) counts.mode_button = values.mode_button.map(Number);
      if (values.oct_button) {
        if (values.oct_button.left) counts.oct_button.left = values.oct_button.left.map(Number);
        if (values.oct_button.right) counts.oct_button.right = values.oct_button.right.map(Number);
      }
      if (values.source_switch !== undefined) counts.source_switch = Number(values.source_switch);
      if (values.pot) counts.pot = values.pot.map(Number);
    },
    setMidi(values) {
      if (values.midiRx !== undefined) live.midiRx = Number(values.midiRx);
      if (values.note) Object.assign(live.note, values.note);
    },
    failNext(operation, code = "mock_failure") { failure = {operation, code}; },
    setResponseDelay(milliseconds) { responseDelay = Math.max(0, Number(milliseconds) || 0); },
    esptoolLog,
    midiAccess: installMidiAccess(),
    midi: {
      installDevice(id, name) {
        midiOutputs.set(id, mockOutput(id, name));
      },
      sent: midiSent,
      setAcknowledge(value) {
        midiAcknowledge = Boolean(value);
      },
      snapshot() {
        return {outputs: Array.from(midiOutputs.keys()), sent: midiSent.slice()};
      }
    },
    snapshot() {
      return {opened, firmwareVersion, legacyFirmware, counts: clone(counts), midiRx: live.midiRx, note: clone(live.note), esptoolLog: esptoolLog.slice()};
    }
  };

  window.__WINGIE_PROD_MOCK__.midi.installDevice("midi-out-1", "USB MIDI DevicePort 1");

  // ---- Web Audio mock ----

  const audioEvents = [];
  const audioDevices = [
    {deviceId: "audio-out-1", kind: "audiooutput", label: "USB Audio CODEC"},
    {deviceId: "audio-out-default", kind: "audiooutput", label: "Default"}
  ];

  class MockAudioContext {
    constructor() {
      audioEvents.push({type: "create"});
      this.currentTime = 0;
      this.destination = {mock: "destination"};
      this.sinkId = null;
    }

    async setSinkId(deviceId) {
      this.sinkId = deviceId;
      audioEvents.push({type: "sink", deviceId});
    }

    async resume() {
      audioEvents.push({type: "resume"});
    }

    async close() {
      audioEvents.push({type: "close"});
    }

    createOscillator() {
      return {
        type: "",
        frequency: {value: 0},
        connect() {},
        start(time) { audioEvents.push({type: "osc-start", time}); },
        stop(time) { audioEvents.push({type: "osc-stop", time}); }
      };
    }

    createGain() {
      return {
        gain: {
          value: 0,
          setValueAtTime(value, time) { audioEvents.push({type: "gain-set", value, time}); },
          linearRampToValueAtTime(value, time) { audioEvents.push({type: "gain-ramp", value, time}); },
          exponentialRampToValueAtTime(value, time) { audioEvents.push({type: "gain-exp", value, time}); }
        },
        connect() {}
      };
    }

    createStereoPanner() {
      return {
        pan: {value: 0},
        connect() {}
      };
    }
  }

  const mediaDevicesMock = {
    async getUserMedia() {
      audioEvents.push({type: "getusermedia"});
      return {mock: "stream"};
    },
    async enumerateDevices() {
      return audioDevices.map((device) => ({...device}));
    }
  };

  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: mediaDevicesMock
  });

  window.__WINGIE_PROD_MOCK__.audio = {
    events: audioEvents,
    setAudioContext(value) {
      window.AudioContext = value || MockAudioContext;
      window.webkitAudioContext = value || MockAudioContext;
    }
  };
  window.__WINGIE_PROD_MOCK__.audio.setAudioContext(null);
})();
