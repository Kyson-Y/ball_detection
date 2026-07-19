const fs = require("fs");
const { chromium } = require("playwright");

const baseUrl = process.env.ECHO_TELEMETRY_URL || "http://127.0.0.1:8765";
const url = baseUrl + "?transport=webserial";
const outputDirectory = process.env.ECHO_TELEMETRY_SCREENSHOTS ||
  "tests/artifacts/telemetry-web";

async function installSerialFixture(page) {
  await page.addInitScript(() => {
    const writes = [];
    let readController = null;
    let outgoingSequence = 1;
    let telemetryTimer = null;
    let telemetryTimestamp = 10000;

    function crc16(data, start, end) {
      let crc = 0xFFFF;
      for (let index = start; index < end; index += 1) {
        crc ^= data[index] << 8;
        for (let bit = 0; bit < 8; bit += 1) {
          crc = (crc & 0x8000) !== 0
            ? ((crc << 1) ^ 0x1021) & 0xFFFF
            : (crc << 1) & 0xFFFF;
        }
      }
      return crc;
    }

    function finishFrame(frame) {
      const view = new DataView(frame.buffer);
      view.setUint16(frame.length - 2, crc16(frame, 2, frame.length - 2), true);
      readController.enqueue(frame);
    }

    function beginFrame(type, payloadLength, timestamp) {
      const frame = new Uint8Array(16 + payloadLength);
      const view = new DataView(frame.buffer);
      frame[0] = 0xA5;
      frame[1] = 0x5A;
      frame[2] = 1;
      frame[3] = type;
      view.setUint16(4, payloadLength, true);
      view.setUint32(6, outgoingSequence++, true);
      view.setUint32(10, timestamp, true);
      return frame;
    }

    function emitControl(timestamp, leftSpeed, rightSpeed) {
      const frame = beginFrame(1, 96, timestamp);
      const view = new DataView(frame.buffer);
      const base = 14;
      view.setFloat32(base, 0, true);
      view.setFloat32(base + 4, leftSpeed, true);
      view.setFloat32(base + 8, rightSpeed, true);
      view.setFloat32(base + 12, 0, true);
      view.setUint32(base + 16, timestamp / 10000, true);
      view.setUint32(base + 20, 10000, true);
      view.setUint32(base + 24, 37, true);
      view.setUint32(base + 28, 0, true);
      view.setUint32(base + 32, 0, true);
      view.setUint32(base + 36, 0, true);
      view.setFloat32(base + 40, 0, true);
      view.setFloat32(base + 44, 0, true);
      view.setFloat32(base + 80, 3, true);
      view.setFloat32(base + 84, 8, true);
      view.setFloat32(base + 88, 0, true);
      finishFrame(frame);
    }

    function acknowledgeParameter(command) {
      const commandView = new DataView(command.buffer);
      const frame = beginFrame(3, 16, 30000);
      const view = new DataView(frame.buffer);
      view.setUint32(14, commandView.getUint32(14, true), true);
      view.setUint16(18, commandView.getUint16(18, true), true);
      frame[20] = 0;
      frame[21] = 0;
      view.setFloat32(22, commandView.getFloat32(22, true), true);
      view.setUint32(26, writes.length, true);
      finishFrame(frame);
    }

    function acknowledgeActuator(command) {
      const commandView = new DataView(command.buffer);
      const frame = beginFrame(6, 16, 40000);
      const view = new DataView(frame.buffer);
      view.setUint32(14, commandView.getUint32(22, true), true);
      view.setInt16(18, commandView.getInt16(26, true), true);
      view.setInt16(20, commandView.getInt16(28, true), true);
      view.setUint16(22, commandView.getUint16(30, true), true);
      frame[24] = 0;
      frame[25] = commandView.getUint16(32, true);
      view.setUint32(26, 1, true);
      finishFrame(frame);
    }

    const port = {
      readable: null,
      writable: null,
      async open(options) {
        window.__serialOptions = options;
        this.readable = new ReadableStream({
          start(controller) { readController = controller; }
        });
        this.writable = new WritableStream({
          write(chunk) {
            const copy = new Uint8Array(chunk);
            writes.push(Array.from(copy));
            if (copy[3] === 2) {
              acknowledgeParameter(copy);
            } else if (copy[3] === 5) {
              acknowledgeActuator(copy);
            }
          }
        });
        telemetryTimer = setInterval(() => {
          emitControl(telemetryTimestamp, 0, 0);
          telemetryTimestamp += 10000;
        }, 10);
      },
      async setSignals(signals) { window.__serialSignals = signals; },
      async close() {
        if (telemetryTimer !== null) clearInterval(telemetryTimer);
        telemetryTimer = null;
      }
    };

    const serial = new EventTarget();
    serial.requestPort = async () => port;
    serial.getPorts = async () => [port];
    Object.defineProperty(navigator, "serial", {
      configurable: true,
      value: serial
    });
    window.__serialWrites = writes;
  });
}

async function verifyViewport(browser, name, viewport) {
  const page = await browser.newPage({ viewport });
  await installSerialFixture(page);
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.goto(url, { waitUntil: "networkidle" });
  await page.waitForTimeout(350);

  const result = await page.evaluate(() => {
    const overflowing = Array.from(document.querySelectorAll(
      "button, input, select, canvas"
    )).filter(element => {
      const parent = element.parentElement;
      return parent && element.getBoundingClientRect().right >
        parent.getBoundingClientRect().right + 1;
    }).map(element => element.id || element.tagName);
    const canvases = Array.from(document.querySelectorAll("canvas")).map(canvas => {
      const pixels = canvas.getContext("2d").getImageData(
        0, 0, Math.min(canvas.width, 40), Math.min(canvas.height, 40)).data;
      return {
        id: canvas.id,
        width: canvas.clientWidth,
        height: canvas.clientHeight,
        nonBlank: pixels.some(value => value !== 0)
      };
    });
    return {
      title: document.title,
      horizontalOverflow:
        document.documentElement.scrollWidth > document.documentElement.clientWidth,
      overflowing,
      channelCount: document.querySelectorAll(".channel input").length,
      canvases
    };
  });

  if (errors.length !== 0 || result.title !== "ECHO PID Console" ||
      result.horizontalOverflow || result.overflowing.length !== 0 ||
      result.channelCount !== 14 ||
      result.canvases.some(canvas => canvas.width < 250 ||
        canvas.height < 200 || !canvas.nonBlank)) {
    throw new Error(`${name} failed: ${JSON.stringify({ errors, result })}`);
  }

  await page.click("#pauseButton");
  if ((await page.textContent("#pauseButton")) !== "继续显示") {
    throw new Error(`${name} pause control did not toggle`);
  }

  await page.click("#pauseButton");
  await page.click("#connectButton");
  await page.waitForFunction(() =>
    document.querySelector("#connectionStatus").textContent.startsWith("已连接 /"));
  await page.waitForFunction(() =>
    Number(document.querySelector('[data-stat="frames"]').textContent) >= 2);
  await page.click("#applyPidButton");
  await page.waitForFunction(() =>
    document.querySelector("#pidStatus").textContent.startsWith("已应用："));
  await page.click("#stopButton");
  await page.waitForFunction(() =>
    document.querySelector("#speedStatus").textContent.startsWith("已受理："));
  const serialResult = await page.evaluate(() => ({
    options: window.__serialOptions,
    signals: window.__serialSignals,
    types: window.__serialWrites.map(frame => frame[3]),
    speedPayload: window.__serialWrites.find(frame => frame[3] === 5)
  }));
  if (serialResult.options.baudRate !== 230400 ||
      serialResult.signals.dataTerminalReady !== true ||
      serialResult.signals.requestToSend !== false ||
      serialResult.types.filter(type => type === 2).length !== 3 ||
      serialResult.types.filter(type => type === 5).length !== 2 ||
      serialResult.speedPayload[26] !== 0 || serialResult.speedPayload[27] !== 0 ||
      serialResult.speedPayload[28] !== 0 || serialResult.speedPayload[29] !== 0) {
    throw new Error(`${name} serial fixture failed: ${JSON.stringify(serialResult)}`);
  }
  await page.screenshot({
    path: `${outputDirectory}/${name}.png`,
    fullPage: true
  });
  await page.close();
}

(async () => {
  fs.mkdirSync(outputDirectory, { recursive: true });
  const browser = await chromium.launch({ channel: "msedge", headless: true });
  try {
    await verifyViewport(browser, "desktop-1440x1000", {
      width: 1440,
      height: 1000
    });
    await verifyViewport(browser, "mobile-390x844", {
      width: 390,
      height: 844
    });
  } finally {
    await browser.close();
  }
  process.stdout.write("telemetry web smoke: PASS\n");
})().catch(error => {
  console.error(error);
  process.exit(1);
});
