(() => {
  "use strict";

  const encoder = new TextEncoder();
  const imageSpecs = [
    {name: "bootloader", path: "Wingie2-v9.9.9.bootloader.bin", offset: 0x1000, length: 512, seed: 0x11},
    {name: "partitions", path: "Wingie2-v9.9.9.partitions.bin", offset: 0x8000, length: 256, seed: 0x22},
    {name: "boot_app0", path: "boot_app0-v9.9.9.bin", offset: 0xe000, length: 128, seed: 0x33},
    {name: "app", path: "Wingie2-v9.9.9.bin", offset: 0x10000, length: 2048, seed: 0x44}
  ];
  const images = new Map();
  const log = [];
  const failureCount = new Map();

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
    const url = new URL(typeof input === "string" ? input : input.url, location.href);
    const path = decodeURIComponent(url.pathname.split("/").pop());
    if (path === "manifest.json") {
      log.push({type: "fetch-manifest", cache: init && init.cache});
      return new Response(JSON.stringify(await manifest()), {
        status: 200,
        headers: {"Content-Type": "application/json"}
      });
    }
    if (images.has(path)) {
      log.push({type: "fetch-image", path, cache: init && init.cache});
      return new Response(images.get(path).slice(), {status: 200});
    }
    return originalFetch(input, init);
  };

  const port = {
    async open() {},
    async close() {},
    async setSignals(signals) {
      log.push({type: "signals", signals});
    }
  };

  Object.defineProperty(navigator, "serial", {
    configurable: true,
    value: {
      async requestPort() {
        const current = scenario();
        log.push({type: "request-port", scenario: current});
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
      log.push({type: "transport", tracing});
    }

    async disconnect() {
      log.push({type: "disconnect"});
    }
  }

  class MockESPLoader {
    constructor(options) {
      this.options = options;
      this.chip = null;
      log.push({type: "loader", baudrate: options.baudrate, debugLogging: options.debugLogging});
    }

    async main(mode) {
      const current = scenario();
      log.push({type: "main", mode, scenario: current});
      if (current === "boot-fail") throw new Error("Failed to connect to ESP32: Timed out waiting for packet header");
      this.chip = {CHIP_NAME: current === "wrong-chip" ? "ESP32-S3" : "ESP32"};
      return current === "wrong-chip" ? "ESP32-S3 (revision 0)" : "ESP32-D0WDQ6 (revision 1)";
    }

    async writeFlash(options) {
      const address = options.fileArray[0].address;
      const current = scenario();
      log.push({
        type: "write",
        scenario: current,
        address,
        fileCount: options.fileArray.length,
        flashMode: options.flashMode,
        flashFreq: options.flashFreq,
        flashSize: options.flashSize,
        eraseAll: options.eraseAll,
        compress: options.compress,
        md5: options.calculateMD5Hash(options.fileArray[0].data)
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

    async after(mode) {
      log.push({type: "after", mode});
    }
  }

  function mockMd5(bytes) {
    let sum = 0;
    for (const value of bytes) sum = (sum + value) >>> 0;
    return sum.toString(16).padStart(32, "0").slice(-32);
  }

  window.__WINGIE_FLASH_MOCK__ = {
    esptool: {Transport: MockTransport, ESPLoader: MockESPLoader},
    md5: mockMd5,
    log,
    supportedDevices: ["blank", "v1", "v3", "current", "broken-app"],
    snapshot() {
      return {scenario: scenario(), log: log.slice()};
    },
    reset() {
      log.length = 0;
      failureCount.clear();
    }
  };
})();
