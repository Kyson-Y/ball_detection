const fs = require("fs");
const { chromium } = require("playwright");

const url = process.env.ECHO_TELEMETRY_URL || "http://127.0.0.1:8765";
const outputDirectory = process.env.ECHO_TELEMETRY_SCREENSHOTS ||
  "tests/artifacts/telemetry-web";

async function applyPid(page, kp, ki, kd) {
  await page.fill("#kpInput", String(kp));
  await page.fill("#kiInput", String(ki));
  await page.fill("#kdInput", String(kd));
  await page.click("#applyPidButton");
  await page.waitForFunction(() =>
    document.querySelector("#pidStatus").textContent.startsWith("已应用："));
}

async function setTarget(page, left, right) {
  await page.fill("#leftTargetInput", String(left));
  await page.fill("#rightTargetInput", String(right));
  await page.click("#sendSpeedButton");
  await page.waitForFunction(() =>
    document.querySelector("#speedStatus").textContent.startsWith("已受理："));
}

async function sampleResponse(page, durationMs) {
  const started = Date.now();
  const samples = [];
  while (Date.now() - started < durationMs) {
    const values = await page.evaluate(() => ({
      left: Number(document.querySelector('[data-live="leftSpeed"]').value),
      right: Number(document.querySelector('[data-live="rightSpeed"]').value),
      leftOutput: Number(document.querySelector('[data-live="leftTotal"]').value),
      rightOutput: Number(document.querySelector('[data-live="rightTotal"]').value)
    }));
    samples.push({ timeMs: Date.now() - started, ...values });
    await page.waitForTimeout(25);
  }
  return samples.filter(sample => Number.isFinite(sample.left) &&
    Number.isFinite(sample.right));
}

function summarize(samples, target) {
  const threshold = target * 0.9;
  const tail = samples.filter(sample =>
    sample.timeMs >= samples[samples.length - 1].timeMs - 500);
  const average = (items, key) => items.reduce((sum, item) =>
    sum + item[key], 0) / Math.max(1, items.length);
  const first90 = side => {
    const sample = samples.find(item => item[side] >= threshold);
    return sample ? sample.timeMs : null;
  };
  return {
    leftT90Ms: first90("left"),
    rightT90Ms: first90("right"),
    leftPeakRpm: Math.max(...samples.map(sample => sample.left)),
    rightPeakRpm: Math.max(...samples.map(sample => sample.right)),
    leftTailRpm: average(tail, "left"),
    rightTailRpm: average(tail, "right"),
    leftTailErrorRpm: target - average(tail, "left"),
    rightTailErrorRpm: target - average(tail, "right")
  };
}

(async () => {
  fs.mkdirSync(outputDirectory, { recursive: true });
  const browser = await chromium.launch({ channel: "msedge", headless: true });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  try {
    await page.goto(url, { waitUntil: "networkidle" });
    await page.click("#simulationButton");
    await page.waitForFunction(() =>
      document.querySelector("#connectionStatus").textContent === "模拟运行");

    await applyPid(page, 0.5, 0, 0);
    await setTarget(page, 70, 70);
    const softSamples = await sampleResponse(page, 3000);
    const soft = summarize(softSamples, 70);

    await page.click("#stopButton");
    await page.waitForTimeout(1500);
    await page.click("#clearButton");
    await applyPid(page, 3, 8, 0);
    await setTarget(page, 70, 70);
    const tunedSamples = await sampleResponse(page, 3000);
    const tuned = summarize(tunedSamples, 70);

    if (Math.abs(tuned.leftTailErrorRpm) >= 2 ||
        Math.abs(tuned.rightTailErrorRpm) >= 2 ||
        Math.abs(soft.leftTailErrorRpm) <= Math.abs(tuned.leftTailErrorRpm) ||
        Math.abs(soft.rightTailErrorRpm) <= Math.abs(tuned.rightTailErrorRpm)) {
      throw new Error(`simulation tuning gate failed: ${JSON.stringify({ soft, tuned })}`);
    }

    const result = { model: "513X-4S illustrative first-order plant", soft, tuned };
    fs.writeFileSync(`${outputDirectory}/simulation-result.json`,
      JSON.stringify(result, null, 2));
    await page.screenshot({
      path: `${outputDirectory}/simulation-tuned-1440x1000.png`,
      fullPage: true
    });
    process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
    process.stdout.write("PID console simulation: PASS\n");
  } finally {
    await browser.close();
  }
})().catch(error => {
  console.error(error);
  process.exit(1);
});
