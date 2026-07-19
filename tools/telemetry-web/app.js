const SYNC0 = 0xA5;
const SYNC1 = 0x5A;
const VERSION = 1;
const TYPE_CONTROL = 1;
const TYPE_PARAMETER_SET = 2;
const TYPE_PARAMETER_ACK = 3;
const TYPE_ACTUATOR_COMMAND = 5;
const TYPE_ACTUATOR_ACK = 6;
const CONTROL_PAYLOAD_LENGTHS = new Set([40, 44, 96]);
const PARAMETER_ACK_PAYLOAD = 16;
const ACTUATOR_ACK_PAYLOAD = 16;
const MAX_PAYLOAD = 128;
const WINDOW_US = 20000000;
const MAX_POINTS = 3000;
const MAX_CAPTURE_POINTS = 120000;
const ACK_TIMEOUT_MS = 750;
const MAX_ATTEMPTS = 3;
const SPEED_MODE = 1;
const SPEED_LIMIT_RPM = 100;
const ACTUATOR_MAGIC = 0x4543484F;
const ACTUATOR_MAGIC_INVERSE = 0xBABCB7B0;
const SIMULATION_PERIOD_MS = 10;
const SERIAL_SIGNAL_PROBE_MS = 500;
const SERIAL_SIGNAL_PROFILES = [
  { dataTerminalReady: true, requestToSend: false, label: "DTR 1 / RTS 0" },
  { dataTerminalReady: false, requestToSend: false, label: "DTR 0 / RTS 0" },
  { dataTerminalReady: false, requestToSend: true, label: "DTR 0 / RTS 1" },
  { dataTerminalReady: true, requestToSend: true, label: "DTR 1 / RTS 1" }
];

const speedChannels = [
  { key: "leftTarget", label: "左目标", color: "#5ee6b8", enabled: true },
  { key: "rightTarget", label: "右目标", color: "#ff9f7a", enabled: true, dash: [6, 4] },
  { key: "leftSpeed", label: "左轮速", color: "#38bdf8", enabled: true },
  { key: "rightSpeed", label: "右轮速", color: "#f6c85f", enabled: true }
];

const pidChannels = [
  { key: "leftTotal", label: "左总输出", color: "#38bdf8", enabled: true },
  { key: "rightTotal", label: "右总输出", color: "#f6c85f", enabled: true },
  { key: "leftP", label: "左 P", color: "#5ee6b8", enabled: true },
  { key: "rightP", label: "右 P", color: "#ff9f7a", enabled: true, dash: [6, 4] },
  { key: "leftI", label: "左 I", color: "#b9d66b", enabled: true },
  { key: "rightI", label: "右 I", color: "#f59eae", enabled: true, dash: [6, 4] },
  { key: "leftD", label: "左 D", color: "#a78bfa", enabled: true },
  { key: "rightD", label: "右 D", color: "#e879f9", enabled: true, dash: [6, 4] },
  { key: "leftFeedforward", label: "左前馈", color: "#94a3b8", enabled: false },
  { key: "rightFeedforward", label: "右前馈", color: "#d1d5db", enabled: false, dash: [6, 4] }
];

const parameterDefinitions = {
  kp: { id: 1, min: 0, max: 1000 },
  ki: { id: 2, min: 0, max: 1000 },
  kd: { id: 3, min: 0, max: 1000 }
};

const parameterNamesById = { 1: "Kp", 2: "Ki", 3: "Kd" };
const actuatorStatusNames = [
  "已受理", "Magic 错误", "数值错误", "设备忙", "重复命令", "Profile 未解锁"
];

const stat = {
  bytes: 0,
  frames: 0,
  parameterAcks: 0,
  actuatorAcks: 0,
  crcErrors: 0,
  badLength: 0,
  badVersion: 0,
  syncDrops: 0,
  seqGaps: 0,
  seqResets: 0,
  invalidPayload: 0,
  overflow: 0,
  unexpectedAcks: 0,
  captureTruncated: 0
};

const points = [];
const capturedPoints = [];
const selectedSpeedChannels = new Set();
const selectedPidChannels = new Set();
const pendingParameters = new Map();
const pendingActuators = new Map();

let port = null;
let reader = null;
let writer = null;
let readLoopPromise = null;
let writeChain = Promise.resolve();
let connectionPhase = "idle";
let outgoingSequence = 1;
let nextTransaction = crypto.getRandomValues(new Uint32Array(1))[0] || 1;
let nextActuatorSequence = crypto.getRandomValues(new Uint32Array(1))[0] || 1;
let latestFrame = null;
let displayPaused = false;
let frozenPoints = [];
let pidApplying = false;
let speedSending = false;
let lastAcceptedLeftRpm = 0;
let lastAcceptedRightRpm = 0;
let connectedAtMs = 0;
let simulationActive = false;
let simulation = null;
let bridgeActive = false;
let bridgeCursor = 0;
let bridgeAbortController = null;

const forceWebSerial = new URLSearchParams(window.location.search)
  .get("transport") === "webserial";

const baudRate = document.querySelector("#baudRate");
const connectButton = document.querySelector("#connectButton");
const simulationButton = document.querySelector("#simulationButton");
const disconnectButton = document.querySelector("#disconnectButton");
const pauseButton = document.querySelector("#pauseButton");
const clearButton = document.querySelector("#clearButton");
const exportButton = document.querySelector("#exportButton");
const applyPidButton = document.querySelector("#applyPidButton");
const sendSpeedButton = document.querySelector("#sendSpeedButton");
const stopButton = document.querySelector("#stopButton");
const connectionStatus = document.querySelector("#connectionStatus");
const browserStatus = document.querySelector("#browserStatus");
const sampleStatus = document.querySelector("#sampleStatus");
const pidStatus = document.querySelector("#pidStatus");
const speedStatus = document.querySelector("#speedStatus");
const activePid = document.querySelector("#activePid");
const statsElement = document.querySelector("#stats");
const speedChannelsElement = document.querySelector("#speedChannels");
const pidChannelsElement = document.querySelector("#pidChannels");
const speedCanvas = document.querySelector("#speedChart");
const pidCanvas = document.querySelector("#pidChart");

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

function nextNonZero(value) {
  const next = (value + 1) >>> 0;
  return next === 0 ? 1 : next;
}

class Decoder {
  constructor(onFrame) {
    this.buffer = new Uint8Array(0);
    this.onFrame = onFrame;
    this.lastSequence = null;
  }

  reset() {
    this.buffer = new Uint8Array(0);
    this.lastSequence = null;
  }

  push(chunk) {
    stat.bytes += chunk.length;
    const joined = new Uint8Array(this.buffer.length + chunk.length);
    joined.set(this.buffer);
    joined.set(chunk, this.buffer.length);
    this.buffer = joined;
    if (this.buffer.length > 8192) {
      stat.overflow += 1;
      this.buffer = this.buffer.slice(-4096);
    }
    this.parse();
  }

  parse() {
    while (this.buffer.length >= 2) {
      let syncPosition = -1;
      for (let index = 0; index + 1 < this.buffer.length; index += 1) {
        if (this.buffer[index] === SYNC0 && this.buffer[index + 1] === SYNC1) {
          syncPosition = index;
          break;
        }
      }

      if (syncPosition < 0) {
        const keep = this.buffer[this.buffer.length - 1] === SYNC0 ? 1 : 0;
        stat.syncDrops += this.buffer.length - keep;
        this.buffer = keep ? this.buffer.slice(-1) : new Uint8Array(0);
        return;
      }
      if (syncPosition > 0) {
        stat.syncDrops += syncPosition;
        this.buffer = this.buffer.slice(syncPosition);
      }
      if (this.buffer.length < 16) {
        return;
      }

      const version = this.buffer[2];
      const type = this.buffer[3];
      const payloadLength = this.buffer[4] | (this.buffer[5] << 8);
      if (version !== VERSION) {
        stat.badVersion += 1;
        this.buffer = this.buffer.slice(1);
        continue;
      }
      const badTypedLength =
        (type === TYPE_CONTROL && !CONTROL_PAYLOAD_LENGTHS.has(payloadLength)) ||
        (type === TYPE_PARAMETER_ACK && payloadLength !== PARAMETER_ACK_PAYLOAD) ||
        (type === TYPE_ACTUATOR_ACK && payloadLength !== ACTUATOR_ACK_PAYLOAD);
      if (payloadLength > MAX_PAYLOAD || badTypedLength) {
        stat.badLength += 1;
        this.buffer = this.buffer.slice(1);
        continue;
      }

      const totalLength = 16 + payloadLength;
      if (this.buffer.length < totalLength) {
        return;
      }
      const receivedCrc = this.buffer[14 + payloadLength] |
        (this.buffer[15 + payloadLength] << 8);
      const calculatedCrc = crc16(this.buffer, 2, 14 + payloadLength);
      if (receivedCrc !== calculatedCrc) {
        stat.crcErrors += 1;
        this.buffer = this.buffer.slice(1);
        continue;
      }

      const frame = this.buffer.slice(0, totalLength);
      this.buffer = this.buffer.slice(totalLength);
      const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength);
      const sequence = view.getUint32(6, true);
      const timestamp = view.getUint32(10, true);
      if (this.lastSequence !== null) {
        const delta = (sequence - this.lastSequence) >>> 0;
        if (delta > 1 && delta < 0x80000000) {
          stat.seqGaps += delta - 1;
        } else if (delta >= 0x80000000) {
          stat.seqResets += 1;
        }
      }
      this.lastSequence = sequence;
      this.onFrame({ type, payloadLength, sequence, timestamp, frame, view });
    }
  }
}

function parseControlFrame(decoded) {
  const base = 14;
  const extended = decoded.payloadLength === 96;
  const dualOutput = decoded.payloadLength >= 44;
  const point = {
    sequence: decoded.sequence,
    timestamp: decoded.timestamp,
    hostMs: performance.now(),
    leftTarget: decoded.view.getFloat32(base, true),
    leftSpeed: decoded.view.getFloat32(base + 4, true),
    rightSpeed: decoded.view.getFloat32(base + 8, true),
    leftTotal: decoded.view.getFloat32(base + 12, true),
    loop: decoded.view.getUint32(base + 16, true),
    period: decoded.view.getUint32(base + 20, true),
    execution: decoded.view.getUint32(base + 24, true),
    jitter: decoded.view.getUint32(base + 28, true),
    deadlineMiss: decoded.view.getUint32(base + 32, true),
    flags: decoded.view.getUint32(base + 36, true),
    rightTotal: dualOutput ? decoded.view.getFloat32(base + 40, true) : 0,
    rightTarget: extended ? decoded.view.getFloat32(base + 44, true) :
      decoded.view.getFloat32(base, true),
    leftP: extended ? decoded.view.getFloat32(base + 48, true) : 0,
    leftI: extended ? decoded.view.getFloat32(base + 52, true) : 0,
    leftD: extended ? decoded.view.getFloat32(base + 56, true) : 0,
    leftFeedforward: extended ? decoded.view.getFloat32(base + 60, true) : 0,
    rightP: extended ? decoded.view.getFloat32(base + 64, true) : 0,
    rightI: extended ? decoded.view.getFloat32(base + 68, true) : 0,
    rightD: extended ? decoded.view.getFloat32(base + 72, true) : 0,
    rightFeedforward: extended ? decoded.view.getFloat32(base + 76, true) : 0,
    activeKp: extended ? decoded.view.getFloat32(base + 80, true) : NaN,
    activeKi: extended ? decoded.view.getFloat32(base + 84, true) : NaN,
    activeKd: extended ? decoded.view.getFloat32(base + 88, true) : NaN,
    parameterApplySequence: extended ? decoded.view.getUint32(base + 92, true) : 0,
    extended
  };

  const numericValues = [
    point.leftTarget, point.rightTarget, point.leftSpeed, point.rightSpeed,
    point.leftTotal, point.rightTotal, point.leftP, point.leftI, point.leftD,
    point.rightP, point.rightI, point.rightD
  ];
  if (!numericValues.every(Number.isFinite)) {
    stat.invalidPayload += 1;
    return null;
  }
  return point;
}

function settleParameter(decoded) {
  stat.parameterAcks += 1;
  const transactionId = decoded.view.getUint32(14, true);
  const parameterId = decoded.view.getUint16(18, true);
  const status = decoded.view.getUint8(20);
  const reserved = decoded.view.getUint8(21);
  const appliedValue = decoded.view.getFloat32(22, true);
  const applySequence = decoded.view.getUint32(26, true);
  const pending = pendingParameters.get(transactionId);
  if (!pending) {
    stat.unexpectedAcks += 1;
    return;
  }
  const mismatch = parameterId !== pending.parameterId || reserved !== 0 ||
    status > 4 || !Number.isFinite(appliedValue) ||
    (status === 0 && Math.abs(appliedValue - pending.value) > 0.0001);
  if (mismatch) {
    completePending(pendingParameters, transactionId, false,
      new Error("参数 ACK 不匹配 / transaction " + transactionId));
    stat.unexpectedAcks += 1;
    return;
  }
  if (status === 4) {
    pidStatus.textContent = "设备忙，等待重试 / transaction " + transactionId;
    return;
  }
  if (status !== 0) {
    completePending(pendingParameters, transactionId, false,
      new Error("设备拒绝 " + parameterNamesById[parameterId] +
        " / status " + status));
    return;
  }
  completePending(pendingParameters, transactionId, true, {
    parameterId, appliedValue, applySequence, transactionId
  });
}

function settleActuator(decoded) {
  stat.actuatorAcks += 1;
  const sequence = decoded.view.getUint32(14, true);
  const leftDeciRpm = decoded.view.getInt16(18, true);
  const rightDeciRpm = decoded.view.getInt16(20, true);
  const durationMs = decoded.view.getUint16(22, true);
  const status = decoded.view.getUint8(24);
  const reserved = decoded.view.getUint8(25);
  const acceptedRequestCount = decoded.view.getUint32(26, true);
  const pending = pendingActuators.get(sequence);
  if (!pending) {
    stat.unexpectedAcks += 1;
    return;
  }
  const mismatch = leftDeciRpm !== pending.leftDeciRpm ||
    rightDeciRpm !== pending.rightDeciRpm || durationMs !== 0 ||
    reserved !== SPEED_MODE || status > 5;
  if (mismatch) {
    completePending(pendingActuators, sequence, false,
      new Error("速度 ACK 不匹配 / sequence " + sequence));
    stat.unexpectedAcks += 1;
    return;
  }
  if (status === 3) {
    speedStatus.textContent = "设备忙，等待重试 / sequence " + sequence;
    return;
  }
  if (status !== 0 && status !== 4) {
    completePending(pendingActuators, sequence, false,
      new Error((actuatorStatusNames[status] || "未知错误") +
        " / sequence " + sequence));
    return;
  }
  completePending(pendingActuators, sequence, true, {
    sequence, leftDeciRpm, rightDeciRpm, status, acceptedRequestCount
  });
}

function handleFrame(decoded) {
  if (decoded.type === TYPE_CONTROL) {
    const point = parseControlFrame(decoded);
    if (!point) {
      return;
    }
    points.push(point);
    if (points.length > MAX_POINTS) {
      points.shift();
    }
    capturedPoints.push(point);
    if (capturedPoints.length > MAX_CAPTURE_POINTS) {
      capturedPoints.shift();
      stat.captureTruncated += 1;
    }
    stat.frames += 1;
    latestFrame = point;
  } else if (decoded.type === TYPE_PARAMETER_ACK) {
    settleParameter(decoded);
  } else if (decoded.type === TYPE_ACTUATOR_ACK) {
    settleActuator(decoded);
  }
}

const decoder = new Decoder(handleFrame);

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function rateLimit(current, requested, maximumDelta) {
  return current + clamp(requested - current, -maximumDelta, maximumDelta);
}

function createSimulationWheel(feedforwardOffset, feedforwardGain,
    plantOffset, plantGain, timeConstant) {
  return {
    feedforwardOffset,
    feedforwardGain,
    plantOffset,
    plantGain,
    timeConstant,
    requestedTarget: 0,
    rampedTarget: 0,
    speed: 0,
    filteredSpeed: 0,
    previousFilteredSpeed: 0,
    observationCount: 0,
    integrator: 0,
    proportional: 0,
    derivative: 0,
    feedforward: 0,
    output: 0
  };
}

function updateSimulationWheel(wheel, periodSeconds, loopCount) {
  const measuredSpeed = wheel.speed + 0.12 * Math.sin(loopCount * 0.37);
  wheel.previousFilteredSpeed = wheel.filteredSpeed;
  if (wheel.observationCount === 0) {
    wheel.filteredSpeed = measuredSpeed;
  } else {
    wheel.filteredSpeed += 0.30 * (measuredSpeed - wheel.filteredSpeed);
  }
  wheel.observationCount += 1;
  wheel.rampedTarget = rateLimit(wheel.rampedTarget, wheel.requestedTarget,
    150 * periodSeconds);

  if (Math.abs(wheel.rampedTarget) < 0.01) {
    wheel.rampedTarget = 0;
    wheel.integrator = 0;
    wheel.proportional = 0;
    wheel.derivative = 0;
    wheel.feedforward = 0;
    wheel.output = 0;
  } else {
    const sign = Math.sign(wheel.rampedTarget);
    const error = wheel.rampedTarget - wheel.filteredSpeed;
    wheel.feedforward = sign * (wheel.feedforwardOffset +
      wheel.feedforwardGain * Math.abs(wheel.rampedTarget));
    wheel.proportional = simulation.kp * error;
    wheel.derivative = -simulation.kd *
      (wheel.filteredSpeed - wheel.previousFilteredSpeed) / periodSeconds;
    if (Math.abs(wheel.requestedTarget - wheel.rampedTarget) <= 0.01) {
      wheel.integrator = clamp(wheel.integrator +
        simulation.ki * error * periodSeconds, -90, 90);
    }
    let requestedOutput = clamp(wheel.feedforward + wheel.proportional +
      wheel.integrator + wheel.derivative, -650, 650);
    if (Math.sign(requestedOutput) !== sign) {
      requestedOutput = 0;
    }
    wheel.output = rateLimit(wheel.output, requestedOutput,
      3000 * periodSeconds);
  }

  const outputSign = Math.sign(wheel.output);
  const desiredSpeed = outputSign * Math.max(0,
    (Math.abs(wheel.output) - wheel.plantOffset) / wheel.plantGain);
  wheel.speed += (desiredSpeed - wheel.speed) *
    clamp(periodSeconds / wheel.timeConstant, 0, 1);
}

function createSimulationFrame(type, payloadLength) {
  const frame = new Uint8Array(16 + payloadLength);
  const view = new DataView(frame.buffer);
  frame[0] = SYNC0;
  frame[1] = SYNC1;
  frame[2] = VERSION;
  frame[3] = type;
  view.setUint16(4, payloadLength, true);
  view.setUint32(6, simulation.sequence, true);
  simulation.sequence = nextNonZero(simulation.sequence);
  view.setUint32(10, simulation.timestampUs >>> 0, true);
  return frame;
}

function finishSimulationFrame(frame) {
  const view = new DataView(frame.buffer);
  view.setUint16(frame.length - 2, crc16(frame, 2, frame.length - 2), true);
  decoder.push(frame);
}

function emitSimulationControl() {
  const frame = createSimulationFrame(TYPE_CONTROL, 96);
  const view = new DataView(frame.buffer);
  const base = 14;
  const left = simulation.left;
  const right = simulation.right;
  view.setFloat32(base, left.requestedTarget, true);
  view.setFloat32(base + 4, left.speed, true);
  view.setFloat32(base + 8, right.speed, true);
  view.setFloat32(base + 12, left.output, true);
  view.setUint32(base + 16, simulation.loopCount, true);
  view.setUint32(base + 20, 10000, true);
  view.setUint32(base + 24, 12, true);
  view.setUint32(base + 28, 0, true);
  view.setUint32(base + 32, 0, true);
  view.setUint32(base + 36,
    (left.requestedTarget !== 0 || right.requestedTarget !== 0) ? (1 << 5) : 0,
    true);
  view.setFloat32(base + 40, right.output, true);
  view.setFloat32(base + 44, right.requestedTarget, true);
  view.setFloat32(base + 48, left.proportional, true);
  view.setFloat32(base + 52, left.integrator, true);
  view.setFloat32(base + 56, left.derivative, true);
  view.setFloat32(base + 60, left.feedforward, true);
  view.setFloat32(base + 64, right.proportional, true);
  view.setFloat32(base + 68, right.integrator, true);
  view.setFloat32(base + 72, right.derivative, true);
  view.setFloat32(base + 76, right.feedforward, true);
  view.setFloat32(base + 80, simulation.kp, true);
  view.setFloat32(base + 84, simulation.ki, true);
  view.setFloat32(base + 88, simulation.kd, true);
  view.setUint32(base + 92, simulation.parameterApplySequence, true);
  finishSimulationFrame(frame);
}

function acknowledgeSimulationParameter(command) {
  const commandView = new DataView(command.buffer, command.byteOffset,
    command.byteLength);
  const parameterId = commandView.getUint16(18, true);
  const value = commandView.getFloat32(22, true);
  if (parameterId === 1) simulation.kp = value;
  if (parameterId === 2) simulation.ki = value;
  if (parameterId === 3) simulation.kd = value;
  simulation.parameterApplySequence += 1;
  const ack = createSimulationFrame(TYPE_PARAMETER_ACK, 16);
  const view = new DataView(ack.buffer);
  view.setUint32(14, commandView.getUint32(14, true), true);
  view.setUint16(18, parameterId, true);
  ack[20] = 0;
  ack[21] = 0;
  view.setFloat32(22, value, true);
  view.setUint32(26, simulation.parameterApplySequence, true);
  finishSimulationFrame(ack);
}

function acknowledgeSimulationActuator(command) {
  const commandView = new DataView(command.buffer, command.byteOffset,
    command.byteLength);
  const sequence = commandView.getUint32(22, true);
  const leftDeciRpm = commandView.getInt16(26, true);
  const rightDeciRpm = commandView.getInt16(28, true);
  simulation.left.requestedTarget = leftDeciRpm / 10;
  simulation.right.requestedTarget = rightDeciRpm / 10;
  simulation.acceptedRequestCount += 1;
  const ack = createSimulationFrame(TYPE_ACTUATOR_ACK, 16);
  const view = new DataView(ack.buffer);
  view.setUint32(14, sequence, true);
  view.setInt16(18, leftDeciRpm, true);
  view.setInt16(20, rightDeciRpm, true);
  view.setUint16(22, 0, true);
  ack[24] = 0;
  ack[25] = SPEED_MODE;
  view.setUint32(26, simulation.acceptedRequestCount, true);
  finishSimulationFrame(ack);
}

function handleSimulationCommand(frame) {
  setTimeout(() => {
    if (!simulationActive || !simulation) return;
    if (frame[3] === TYPE_PARAMETER_SET) {
      acknowledgeSimulationParameter(frame);
    } else if (frame[3] === TYPE_ACTUATOR_COMMAND) {
      acknowledgeSimulationActuator(frame);
    }
  }, 5);
}

function simulationStep() {
  if (!simulationActive || !simulation) return;
  simulation.loopCount += 1;
  simulation.timestampUs = (simulation.timestampUs + 10000) >>> 0;
  updateSimulationWheel(simulation.left, 0.01, simulation.loopCount);
  updateSimulationWheel(simulation.right, 0.01, simulation.loopCount + 11);
  emitSimulationControl();
}

function startSimulation() {
  if (connectionPhase !== "idle") return;
  simulation = {
    kp: Number(document.querySelector("#kpInput").value),
    ki: Number(document.querySelector("#kiInput").value),
    kd: Number(document.querySelector("#kdInput").value),
    parameterApplySequence: 0,
    acceptedRequestCount: 0,
    sequence: 1,
    timestampUs: 0,
    loopCount: 0,
    left: createSimulationWheel(409, 1.09, 420, 1.12, 0.23),
    right: createSimulationWheel(401, 1.62, 414, 1.70, 0.27),
    timer: null
  };
  simulationActive = true;
  connectionPhase = "connected";
  connectedAtMs = performance.now();
  clearData();
  setConnectionState("模拟运行", "connected");
  speedStatus.textContent = "模拟器不会访问串口或驱动真实电机。";
  simulation.timer = setInterval(simulationStep, SIMULATION_PERIOD_MS);
  updateControls();
}

function stopSimulationEngine() {
  if (simulation && simulation.timer !== null) {
    clearInterval(simulation.timer);
  }
  simulation = null;
  simulationActive = false;
}

function createChannelControls(definitions, selected, container) {
  definitions.forEach(channel => {
    const label = document.createElement("label");
    label.className = "channel";
    label.style.setProperty("--channel-color", channel.color);
    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = channel.enabled;
    input.addEventListener("change", () => {
      if (input.checked) {
        selected.add(channel.key);
      } else {
        selected.delete(channel.key);
      }
    });
    if (channel.enabled) {
      selected.add(channel.key);
    }
    const swatch = document.createElement("span");
    swatch.className = channel.dash ? "line-swatch dashed" : "line-swatch";
    const text = document.createElement("span");
    text.textContent = channel.label;
    label.append(input, swatch, text);
    container.append(label);
  });
}

function resizeCanvas(canvas, context) {
  const rect = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.floor(rect.width * ratio));
  const height = Math.max(1, Math.floor(rect.height * ratio));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  return rect;
}

function visiblePoints(source) {
  if (source.length === 0) {
    return source;
  }
  const latestTimestamp = source[source.length - 1].timestamp;
  return source.filter(point =>
    ((latestTimestamp - point.timestamp) >>> 0) <= WINDOW_US);
}

function drawChart(canvas, definitions, selected, unit) {
  const context = canvas.getContext("2d");
  const rect = resizeCanvas(canvas, context);
  const width = rect.width;
  const height = rect.height;
  const plot = { left: 54, right: 14, top: 14, bottom: 27 };
  const plotWidth = Math.max(1, width - plot.left - plot.right);
  const plotHeight = Math.max(1, height - plot.top - plot.bottom);
  context.clearRect(0, 0, width, height);
  context.fillStyle = "#101314";
  context.fillRect(0, 0, width, height);

  context.strokeStyle = "#303638";
  context.lineWidth = 1;
  context.setLineDash([]);
  for (let line = 0; line <= 4; line += 1) {
    const y = plot.top + (plotHeight * line) / 4;
    context.beginPath();
    context.moveTo(plot.left, y);
    context.lineTo(plot.left + plotWidth, y);
    context.stroke();
  }
  for (let line = 0; line <= 4; line += 1) {
    const x = plot.left + (plotWidth * line) / 4;
    context.beginPath();
    context.moveTo(x, plot.top);
    context.lineTo(x, plot.top + plotHeight);
    context.stroke();
  }

  const active = definitions.filter(channel => selected.has(channel.key));
  const source = displayPaused ? frozenPoints : points;
  const visible = visiblePoints(source);
  if (visible.length < 2 || active.length === 0) {
    context.fillStyle = "#7f8b8e";
    context.font = "13px Segoe UI";
    context.fillText(active.length === 0 ? "请选择曲线" : "等待有效控制帧", 65, 34);
    return;
  }

  let minimum = Infinity;
  let maximum = -Infinity;
  visible.forEach(point => {
    active.forEach(channel => {
      const value = point[channel.key];
      if (Number.isFinite(value)) {
        minimum = Math.min(minimum, value);
        maximum = Math.max(maximum, value);
      }
    });
  });
  if (!Number.isFinite(minimum) || !Number.isFinite(maximum)) {
    return;
  }
  if (minimum === maximum) {
    const padding = Math.max(1, Math.abs(minimum) * 0.1);
    minimum -= padding;
    maximum += padding;
  } else {
    const padding = (maximum - minimum) * 0.08;
    minimum -= padding;
    maximum += padding;
  }

  const firstTimestamp = visible[0].timestamp;
  const lastTimestamp = visible[visible.length - 1].timestamp;
  const spanUs = Math.max(1, (lastTimestamp - firstTimestamp) >>> 0);
  active.forEach(channel => {
    context.strokeStyle = channel.color;
    context.lineWidth = channel.key.includes("Target") ? 1.25 : 1.6;
    context.setLineDash(channel.dash || []);
    context.beginPath();
    let started = false;
    visible.forEach(point => {
      const value = point[channel.key];
      if (!Number.isFinite(value)) {
        return;
      }
      const elapsedUs = (point.timestamp - firstTimestamp) >>> 0;
      const x = plot.left + (elapsedUs / spanUs) * plotWidth;
      const y = plot.top + plotHeight -
        ((value - minimum) / (maximum - minimum)) * plotHeight;
      if (!started) {
        context.moveTo(x, y);
        started = true;
      } else {
        context.lineTo(x, y);
      }
    });
    context.stroke();
  });
  context.setLineDash([]);

  context.fillStyle = "#879194";
  context.font = "11px Segoe UI";
  context.textAlign = "right";
  context.fillText(maximum.toFixed(1), plot.left - 7, plot.top + 4);
  context.fillText(((maximum + minimum) / 2).toFixed(1),
    plot.left - 7, plot.top + plotHeight / 2 + 4);
  context.fillText(minimum.toFixed(1), plot.left - 7, plot.top + plotHeight + 4);
  context.textAlign = "left";
  context.fillText(unit, 8, 13);
  context.textAlign = "center";
  for (let line = 0; line <= 4; line += 1) {
    const secondsAgo = ((4 - line) * spanUs) / 4000000;
    const label = line === 4 ? "0 s" : "-" + secondsAgo.toFixed(1) + " s";
    context.fillText(label, plot.left + (plotWidth * line) / 4, height - 7);
  }
}

function drawCharts() {
  drawChart(speedCanvas, speedChannels, selectedSpeedChannels, "rpm");
  drawChart(pidCanvas, pidChannels, selectedPidChannels, "permille");
  requestAnimationFrame(drawCharts);
}

function setConnectionState(message, mode = "") {
  connectionStatus.textContent = message;
  connectionStatus.className = "status" + (mode ? " " + mode : "");
}

function updateControls() {
  const connected = connectionPhase === "connected";
  connectButton.disabled = connectionPhase !== "idle";
  simulationButton.disabled = connectionPhase !== "idle" && !simulationActive;
  simulationButton.textContent = simulationActive ? "停止模拟" : "启动模拟";
  disconnectButton.disabled = connectionPhase === "idle";
  baudRate.disabled = connectionPhase !== "idle";
  applyPidButton.disabled = !connected || pidApplying;
  sendSpeedButton.disabled = !connected || speedSending;
  stopButton.disabled = !connected || speedSending;
}

function setLiveValue(key, value, digits = 1) {
  const element = document.querySelector(`[data-live="${key}"]`);
  if (element) {
    element.textContent = Number.isFinite(value) ? value.toFixed(digits) : "--";
  }
}

function updateStatus() {
  document.querySelectorAll("[data-stat]").forEach(element => {
    element.textContent = stat[element.dataset.stat];
  });
  const rate = points.length > 1
    ? ((points.length - 1) * 1000000) /
      (((points[points.length - 1].timestamp - points[0].timestamp) >>> 0) || 1)
    : 0;
  statsElement.textContent =
    rate.toFixed(1) + " Hz  |  " +
    (latestFrame ? latestFrame.period : 0) + " us 周期  |  " +
    (latestFrame ? latestFrame.execution : 0) + " us 执行";
  if (!latestFrame) {
    if (connectionPhase === "connected" &&
        performance.now() - connectedAtMs >= 2000) {
      sampleStatus.textContent =
        "串口已打开但没有遥测：检查 MCU PA10(TX) → 适配器 RXD 与共地";
    }
    return;
  }

  const speedMode = (latestFrame.flags & (1 << 5)) !== 0;
  sampleStatus.textContent = "seq " + latestFrame.sequence +
    " / " + (speedMode ? "速度闭环" : "输出关闭或电气模式") +
    (displayPaused ? " / 显示已暂停" : "");
  setLiveValue("leftTarget", latestFrame.leftTarget);
  setLiveValue("rightTarget", latestFrame.rightTarget);
  setLiveValue("leftSpeed", latestFrame.leftSpeed);
  setLiveValue("rightSpeed", latestFrame.rightSpeed);
  setLiveValue("leftTotal", latestFrame.leftTotal, 0);
  setLiveValue("rightTotal", latestFrame.rightTotal, 0);
  setLiveValue("leftP", latestFrame.leftP);
  setLiveValue("rightP", latestFrame.rightP);
  setLiveValue("leftI", latestFrame.leftI);
  setLiveValue("rightI", latestFrame.rightI);
  setLiveValue("leftD", latestFrame.leftD);
  setLiveValue("rightD", latestFrame.rightD);
  if (latestFrame.extended) {
    activePid.textContent = "当前 Kp " + latestFrame.activeKp.toFixed(3) +
      " / Ki " + latestFrame.activeKi.toFixed(3) +
      " / Kd " + latestFrame.activeKd.toFixed(3) +
      " / 应用序号 " + latestFrame.parameterApplySequence;
  } else {
    activePid.textContent = "等待新版 96 B 控制帧";
  }
}

function clearTimer(pending) {
  if (pending && pending.timer !== null) {
    clearTimeout(pending.timer);
    pending.timer = null;
  }
}

function completePending(map, id, success, value) {
  const pending = map.get(id);
  if (!pending) {
    return;
  }
  clearTimer(pending);
  map.delete(id);
  if (success) {
    pending.resolve(value);
  } else {
    pending.reject(value);
  }
}

function cancelPending(map, message) {
  Array.from(map.keys()).forEach(id => {
    completePending(map, id, false, new Error(message));
  });
}

function queueWrite(frame) {
  if (connectionPhase !== "connected" && connectionPhase !== "probing") {
    return Promise.reject(new Error("串口未连接"));
  }
  if (simulationActive) {
    handleSimulationCommand(new Uint8Array(frame));
    return Promise.resolve();
  }
  if (bridgeActive) {
    writeChain = writeChain.catch(() => {}).then(async () => {
      const response = await fetch("/api/serial/write", {
        method: "POST",
        headers: { "Content-Type": "application/octet-stream" },
        body: frame
      });
      const result = await response.json();
      if (!response.ok || result.written !== frame.length) {
        throw new Error(result.error || "本机串口桥写入失败");
      }
    });
    return writeChain;
  }
  if (!writer) {
    return Promise.reject(new Error("串口写入器不可用"));
  }
  writeChain = writeChain.catch(() => {}).then(() => writer.write(frame));
  return writeChain;
}

async function readLoop(activePort) {
  const activeReader = activePort.readable.getReader();
  reader = activeReader;
  try {
    while (true) {
      const result = await activeReader.read();
      if (result.done) {
        break;
      }
      if (result.value) {
        decoder.push(result.value);
      }
    }
  } finally {
    if (reader === activeReader) {
      reader = null;
    }
    activeReader.releaseLock();
  }
}

async function bridgeReadLoop() {
  const controller = new AbortController();
  bridgeAbortController = controller;
  try {
    while (bridgeActive) {
      const response = await fetch(
        "/api/serial/read?cursor=" + bridgeCursor,
        { cache: "no-store", signal: controller.signal }
      );
      if (!response.ok) {
        const result = await response.json();
        throw new Error(result.error || "本机串口桥读取失败");
      }
      bridgeCursor = Number(response.headers.get("X-Echo-Cursor")) || bridgeCursor;
      if (response.headers.get("X-Echo-Overflow") === "1") {
        stat.overflow += 1;
      }
      const data = new Uint8Array(await response.arrayBuffer());
      if (data.length > 0) {
        decoder.push(data);
      }
    }
  } catch (error) {
    if (error.name !== "AbortError" && bridgeActive) {
      throw error;
    }
  } finally {
    if (bridgeAbortController === controller) {
      bridgeAbortController = null;
    }
  }
}

async function closeSerialResources() {
  if (bridgeActive || bridgeAbortController) {
    bridgeActive = false;
    if (bridgeAbortController) {
      bridgeAbortController.abort();
    }
    if (readLoopPromise) {
      try {
        await readLoopPromise;
      } catch (error) {
        console.warn(error);
      }
    }
    readLoopPromise = null;
    bridgeAbortController = null;
    return;
  }
  if (reader) {
    try {
      await reader.cancel();
    } catch (error) {
      console.warn(error);
    }
  }
  if (readLoopPromise) {
    try {
      await readLoopPromise;
    } catch (error) {
      console.warn(error);
    }
  }
  readLoopPromise = null;
  reader = null;
  if (writer) {
    try {
      await writeChain;
    } catch (error) {
      console.warn(error);
    }
    try {
      writer.releaseLock();
    } catch (error) {
      console.warn(error);
    }
  }
  writer = null;
  if (port) {
    try {
      await port.close();
    } catch (error) {
      console.warn(error);
    }
  }
  port = null;
}

async function disconnect(disconnectedPort = null) {
  if (disconnectedPort && port && disconnectedPort !== port) {
    return;
  }
  if (connectionPhase === "idle" || connectionPhase === "disconnecting") {
    return;
  }
  connectionPhase = "disconnecting";
  updateControls();
  cancelPending(pendingParameters, "串口已断开");
  cancelPending(pendingActuators, "串口已断开");
  stopSimulationEngine();
  await closeSerialResources();
  decoder.reset();
  connectionPhase = "idle";
  connectedAtMs = 0;
  setConnectionState("已断开");
  speedStatus.textContent = "网页断开不会自动停机；重新连接后发送 0/0 rpm 可停机。";
  updateControls();
}

function waitMilliseconds(milliseconds) {
  return new Promise(resolve => setTimeout(resolve, milliseconds));
}

async function probeSerialSignals(activePort) {
  let sawBytes = false;
  let sawFrames = false;
  for (const profile of SERIAL_SIGNAL_PROFILES) {
    if (port !== activePort || connectionPhase === "disconnecting") {
      return { profile: null, sawBytes, sawFrames };
    }
    const bytesBefore = stat.bytes;
    const framesBefore = stat.frames;
    setConnectionState("检测串口 " + profile.label);
    await activePort.setSignals({
      dataTerminalReady: profile.dataTerminalReady,
      requestToSend: profile.requestToSend
    });
    await waitMilliseconds(SERIAL_SIGNAL_PROBE_MS);
    sawBytes ||= stat.bytes > bytesBefore;
    if (stat.frames <= framesBefore) {
      continue;
    }
    sawFrames = true;
    speedStatus.textContent = "检测 " + profile.label + " 下行 0/0 ACK";
    try {
      await sendActuatorCommand(0, 0);
      return { profile, sawBytes: true, sawFrames: true };
    } catch (error) {
      speedStatus.textContent = profile.label + " 只能接收，继续检测双向串口";
    }
  }
  return { profile: null, sawBytes, sawFrames };
}

async function getBridgeStatus() {
  if (forceWebSerial) {
    return null;
  }
  try {
    const response = await fetch("/api/serial/status", { cache: "no-store" });
    if (!response.ok) {
      return null;
    }
    const status = await response.json();
    return status.bridge ? status : null;
  } catch (error) {
    return null;
  }
}

async function connectBridge() {
  const response = await fetch("/api/serial/connect", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ port: "COM9", baudRate: Number(baudRate.value) })
  });
  const status = await response.json();
  if (!response.ok || !status.connected) {
    throw new Error(status.error || "本机串口桥无法打开 COM9");
  }

  bridgeActive = true;
  bridgeCursor = status.cursor;
  writeChain = Promise.resolve();
  connectionPhase = "probing";
  connectedAtMs = performance.now();
  clearData();
  setConnectionState("检测本机串口桥");
  readLoopPromise = bridgeReadLoop().catch(error => {
    if (bridgeActive) {
      setConnectionState(error.message, "error");
      setTimeout(() => { disconnect(); }, 0);
    }
  });
  updateControls();

  const deadline = performance.now() + 3000;
  while (stat.frames < 3 && performance.now() < deadline) {
    await waitMilliseconds(50);
  }
  if (stat.frames < 3) {
    throw new Error("本机串口桥已打开 COM9，但没有收到有效控制帧");
  }
  speedStatus.textContent = "检测本机串口桥下行 0/0 ACK";
  await sendActuatorCommand(0, 0);
  clearData();
  connectionPhase = "connected";
  connectedAtMs = performance.now();
  setConnectionState("已连接 / 本机桥 COM9", "connected");
  sampleStatus.textContent = "本机串口桥双向检测通过，等待控制帧";
  speedStatus.textContent = "线路自检 0/0 rpm ACK 通过，可以下发速度";
}

async function connect() {
  if (connectionPhase !== "idle") {
    return;
  }
  connectionPhase = "connecting";
  updateControls();
  const bridgeStatus = await getBridgeStatus();
  if (bridgeStatus) {
    try {
      await connectBridge();
    } catch (error) {
      await closeSerialResources();
      connectionPhase = "idle";
      setConnectionState(error.message, "error");
    }
    updateControls();
    return;
  }
  if (!("serial" in navigator)) {
    connectionPhase = "idle";
    setConnectionState("浏览器不支持 Web Serial，且本机串口桥未运行", "error");
    updateControls();
    return;
  }
  let candidatePort = null;
  let candidateWriter = null;
  try {
    candidatePort = await navigator.serial.requestPort();
    await candidatePort.open({
      baudRate: Number(baudRate.value),
      dataBits: 8,
      stopBits: 1,
      parity: "none",
      flowControl: "none",
      bufferSize: 1024 * 1024
    });
    candidateWriter = candidatePort.writable.getWriter();
    port = candidatePort;
    writer = candidateWriter;
    candidatePort = null;
    candidateWriter = null;
    writeChain = Promise.resolve();
    connectionPhase = "probing";
    connectedAtMs = performance.now();
    clearData();
    const activePort = port;
    readLoopPromise = readLoop(activePort).catch(error => {
      if (port === activePort) {
        setConnectionState(error.message, "error");
        setTimeout(() => { disconnect(activePort); }, 0);
      }
    });
    updateControls();
    const probe = await probeSerialSignals(activePort);
    if (port !== activePort || connectionPhase === "disconnecting") {
      return;
    }
    if (probe.profile) {
      clearData();
      connectionPhase = "connected";
      connectedAtMs = performance.now();
      setConnectionState("已连接 / " + probe.profile.label, "connected");
      sampleStatus.textContent = "串口线路检测通过，等待控制帧";
      speedStatus.textContent = "线路自检 0/0 rpm ACK 通过，可以下发速度";
    } else {
      connectionPhase = "no-data";
      const reason = probe.sawFrames
        ? "四种 DTR/RTS 都能接收但没有 0/0 ACK：当前浏览器/适配器下行链路不可用"
        : (probe.sawBytes
          ? "收到串口字节但没有有效控制帧：检查 230400 波特率和信号质量"
          : "四种 DTR/RTS 均无 MCU 数据：确认选择 COM9，并检查 PA10(TX) → RXD 与共地");
      setConnectionState("串口双向自检失败", "error");
      sampleStatus.textContent = reason;
    }
  } catch (error) {
    if (candidateWriter) {
      candidateWriter.releaseLock();
    }
    if (candidatePort) {
      await candidatePort.close();
    }
    if (port) {
      await closeSerialResources();
    }
    connectionPhase = "idle";
    setConnectionState(error.message, "error");
  }
  updateControls();
}

function buildParameterFrame(parameterId, value, transactionId) {
  const frame = new Uint8Array(28);
  const view = new DataView(frame.buffer);
  frame[0] = SYNC0;
  frame[1] = SYNC1;
  frame[2] = VERSION;
  frame[3] = TYPE_PARAMETER_SET;
  view.setUint16(4, 12, true);
  view.setUint32(6, outgoingSequence, true);
  outgoingSequence = nextNonZero(outgoingSequence);
  view.setUint32(10, 0, true);
  view.setUint32(14, transactionId, true);
  view.setUint16(18, parameterId, true);
  frame[20] = 1;
  frame[21] = 0;
  view.setFloat32(22, value, true);
  view.setUint16(26, crc16(frame, 2, 26), true);
  return frame;
}

function armRetry(map, id, statusElement) {
  const pending = map.get(id);
  if (!pending) {
    return;
  }
  clearTimer(pending);
  pending.timer = setTimeout(async () => {
    const current = map.get(id);
    if (!current) {
      return;
    }
    if (current.attempts >= MAX_ATTEMPTS) {
      let detail;
      if (stat.frames > current.framesAtSend) {
        detail = "已收到 MCU 遥测，但下行命令没有 ACK：检查适配器 TXD → MCU PA11(RX) 和共地";
      } else if (stat.bytes > current.bytesAtSend) {
        detail = "串口有数据但没有有效遥测帧：确认 230400 波特率并检查串口信号质量";
      } else {
        detail = "串口已打开但没有 MCU 数据：确认选择 COM9，并检查 MCU PA10(TX) → 适配器 RXD";
      }
      completePending(map, id, false,
        new Error("ACK 超时：" + detail));
      return;
    }
    current.attempts += 1;
    statusElement.textContent = "重试 " + current.attempts + "/" +
      MAX_ATTEMPTS + " / id " + id;
    try {
      await queueWrite(current.frame);
      armRetry(map, id, statusElement);
    } catch (error) {
      completePending(map, id, false, error);
    }
  }, ACK_TIMEOUT_MS);
}

function sendParameterValue(name, rawValue) {
  const definition = parameterDefinitions[name];
  const value = Math.fround(rawValue);
  const transactionId = nextTransaction >>> 0;
  nextTransaction = nextNonZero(nextTransaction);
  const frame = buildParameterFrame(definition.id, value, transactionId);
  return new Promise((resolve, reject) => {
    pendingParameters.set(transactionId, {
      parameterId: definition.id,
      value,
      frame,
      attempts: 1,
      bytesAtSend: stat.bytes,
      framesAtSend: stat.frames,
      timer: null,
      resolve,
      reject
    });
    queueWrite(frame).then(() => {
      armRetry(pendingParameters, transactionId, pidStatus);
    }).catch(error => {
      completePending(pendingParameters, transactionId, false, error);
    });
  });
}

function readPidInputs() {
  const values = {
    kp: Number(document.querySelector("#kpInput").value),
    ki: Number(document.querySelector("#kiInput").value),
    kd: Number(document.querySelector("#kdInput").value)
  };
  Object.entries(values).forEach(([name, value]) => {
    const definition = parameterDefinitions[name];
    if (!Number.isFinite(value) || value < definition.min || value > definition.max) {
      throw new Error(name.toUpperCase() + " 范围必须为 " +
        definition.min + ".." + definition.max);
    }
  });
  return values;
}

async function applyPid() {
  const values = readPidInputs();
  pidApplying = true;
  updateControls();
  const applied = [];
  try {
    for (const name of ["kp", "ki", "kd"]) {
      pidStatus.textContent = "正在应用 " + name.toUpperCase() +
        " = " + values[name];
      const ack = await sendParameterValue(name, values[name]);
      applied.push(parameterNamesById[ack.parameterId] + "=" +
        ack.appliedValue.toFixed(4) + " (seq " + ack.applySequence + ")");
    }
    pidStatus.textContent = "已应用：" + applied.join(" / ");
  } finally {
    pidApplying = false;
    updateControls();
  }
}

function buildActuatorFrame(leftDeciRpm, rightDeciRpm, commandSequence) {
  const frame = new Uint8Array(36);
  const view = new DataView(frame.buffer);
  frame[0] = SYNC0;
  frame[1] = SYNC1;
  frame[2] = VERSION;
  frame[3] = TYPE_ACTUATOR_COMMAND;
  view.setUint16(4, 20, true);
  view.setUint32(6, outgoingSequence, true);
  outgoingSequence = nextNonZero(outgoingSequence);
  view.setUint32(10, 0, true);
  view.setUint32(14, ACTUATOR_MAGIC, true);
  view.setUint32(18, ACTUATOR_MAGIC_INVERSE, true);
  view.setUint32(22, commandSequence, true);
  view.setInt16(26, leftDeciRpm, true);
  view.setInt16(28, rightDeciRpm, true);
  view.setUint16(30, 0, true);
  view.setUint16(32, SPEED_MODE, true);
  view.setUint16(34, crc16(frame, 2, 34), true);
  return frame;
}

function sendActuatorCommand(leftRpm, rightRpm) {
  const leftDeciRpm = Math.round(leftRpm * 10);
  const rightDeciRpm = Math.round(rightRpm * 10);
  const commandSequence = nextActuatorSequence >>> 0;
  nextActuatorSequence = nextNonZero(nextActuatorSequence);
  const frame = buildActuatorFrame(leftDeciRpm, rightDeciRpm, commandSequence);
  return new Promise((resolve, reject) => {
    pendingActuators.set(commandSequence, {
      leftDeciRpm,
      rightDeciRpm,
      frame,
      attempts: 1,
      bytesAtSend: stat.bytes,
      framesAtSend: stat.frames,
      timer: null,
      resolve,
      reject
    });
    queueWrite(frame).then(() => {
      armRetry(pendingActuators, commandSequence, speedStatus);
    }).catch(error => {
      completePending(pendingActuators, commandSequence, false, error);
    });
  });
}

function validateSpeed(value, side) {
  if (!Number.isFinite(value) || value < -SPEED_LIMIT_RPM ||
      value > SPEED_LIMIT_RPM) {
    throw new Error(side + "目标速度必须在 -" + SPEED_LIMIT_RPM +
      ".." + SPEED_LIMIT_RPM + " rpm");
  }
}

async function sendSpeedTargets(leftRpm, rightRpm) {
  validateSpeed(leftRpm, "左轮");
  validateSpeed(rightRpm, "右轮");
  speedSending = true;
  updateControls();
  try {
    speedStatus.textContent = "等待速度 ACK";
    const ack = await sendActuatorCommand(leftRpm, rightRpm);
    const duplicate = ack.status === 4 ? " / 重复命令已确认" : "";
    speedStatus.textContent = "已受理：左 " + (ack.leftDeciRpm / 10).toFixed(1) +
      " / 右 " + (ack.rightDeciRpm / 10).toFixed(1) +
      " rpm / sequence " + ack.sequence + duplicate;
    lastAcceptedLeftRpm = ack.leftDeciRpm / 10;
    lastAcceptedRightRpm = ack.rightDeciRpm / 10;
  } finally {
    speedSending = false;
    updateControls();
  }
}

function clearData() {
  points.length = 0;
  capturedPoints.length = 0;
  frozenPoints = [];
  latestFrame = null;
  Object.keys(stat).forEach(key => { stat[key] = 0; });
  decoder.reset();
  sampleStatus.textContent = "等待数据";
}

function togglePause() {
  displayPaused = !displayPaused;
  if (displayPaused) {
    frozenPoints = points.slice();
    pauseButton.textContent = "继续显示";
    pauseButton.classList.add("active");
  } else {
    frozenPoints = [];
    pauseButton.textContent = "暂停曲线";
    pauseButton.classList.remove("active");
  }
}

function exportCsv() {
  if (capturedPoints.length === 0) {
    return;
  }
  const columns = [
    "sequence", "timestamp_us", "left_target_rpm", "right_target_rpm",
    "left_speed_rpm", "right_speed_rpm", "left_total_permille",
    "right_total_permille", "left_p_permille", "left_i_permille",
    "left_d_permille", "left_feedforward_permille", "right_p_permille",
    "right_i_permille", "right_d_permille", "right_feedforward_permille",
    "active_kp", "active_ki", "active_kd", "parameter_apply_sequence",
    "loop_count", "period_us", "execution_us", "jitter_us",
    "deadline_miss_count", "flags"
  ];
  const rows = [columns.join(",")];
  capturedPoints.forEach(point => {
    rows.push([
      point.sequence, point.timestamp, point.leftTarget, point.rightTarget,
      point.leftSpeed, point.rightSpeed, point.leftTotal, point.rightTotal,
      point.leftP, point.leftI, point.leftD, point.leftFeedforward,
      point.rightP, point.rightI, point.rightD, point.rightFeedforward,
      point.activeKp, point.activeKi, point.activeKd,
      point.parameterApplySequence, point.loop, point.period, point.execution,
      point.jitter, point.deadlineMiss, point.flags
    ].join(","));
  });
  const url = URL.createObjectURL(new Blob(
    [rows.join("\n")], { type: "text/csv;charset=utf-8" }));
  const link = document.createElement("a");
  link.href = url;
  link.download = "echo-pid-telemetry.csv";
  link.click();
  URL.revokeObjectURL(url);
}

connectButton.addEventListener("click", connect);
simulationButton.addEventListener("click", () => {
  if (simulationActive) {
    disconnect().catch(error => setConnectionState(error.message, "error"));
  } else {
    startSimulation();
  }
});
disconnectButton.addEventListener("click", () => {
  disconnect().catch(error => setConnectionState(error.message, "error"));
});
pauseButton.addEventListener("click", togglePause);
clearButton.addEventListener("click", clearData);
exportButton.addEventListener("click", exportCsv);
applyPidButton.addEventListener("click", () => {
  applyPid().catch(error => { pidStatus.textContent = error.message; });
});
sendSpeedButton.addEventListener("click", () => {
  const left = Number(document.querySelector("#leftTargetInput").value);
  const right = Number(document.querySelector("#rightTargetInput").value);
  sendSpeedTargets(left, right).catch(error => {
    speedStatus.textContent = error.message;
  });
});
stopButton.addEventListener("click", () => {
  document.querySelector("#leftTargetInput").value = "0";
  document.querySelector("#rightTargetInput").value = "0";
  sendSpeedTargets(0, 0).catch(error => {
    speedStatus.textContent = error.message;
  });
});

window.addEventListener("beforeunload", event => {
  if (lastAcceptedLeftRpm !== 0 || lastAcceptedRightRpm !== 0) {
    event.preventDefault();
    event.returnValue = "";
  }
});

if ("serial" in navigator) {
  navigator.serial.addEventListener("disconnect", event => {
    const disconnectedPort = event.port || event.target;
    if (disconnectedPort !== navigator.serial) {
      disconnect(disconnectedPort).catch(error => {
        setConnectionState(error.message, "error");
      });
    }
  });
  browserStatus.textContent = "Edge / Chrome Web Serial";
} else {
  browserStatus.textContent = "请使用新版 Edge 或 Chrome";
}

createChannelControls(speedChannels, selectedSpeedChannels, speedChannelsElement);
createChannelControls(pidChannels, selectedPidChannels, pidChannelsElement);
updateControls();
setInterval(updateStatus, 200);
requestAnimationFrame(drawCharts);
