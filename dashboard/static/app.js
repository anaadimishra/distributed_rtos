const tableBody = document.getElementById("telemetry-body");
const emptyState = document.getElementById("empty-state");

const chartCpu = document.getElementById("chart-cpu");
const chartQueue = document.getElementById("chart-queue");
const chartLoad = document.getElementById("chart-load");
const chartMiss = document.getElementById("chart-miss");
const chartSaturation = document.getElementById("chart-saturation");
const chartCpuValue = document.getElementById("chart-cpu-value");
const chartQueueValue = document.getElementById("chart-queue-value");
const chartLoadValue = document.getElementById("chart-load-value");
const chartMissValue = document.getElementById("chart-miss-value");
const chartSaturationValue = document.getElementById("chart-saturation-value");
const statExecAvg = document.getElementById("stat-exec-avg");
const statExecMax = document.getElementById("stat-exec-max");
const statMiss = document.getElementById("stat-miss");
const statWindowReady = document.getElementById("stat-window-ready");
const chartNodeSelect = document.getElementById("chart-node");
const logSessionEl = document.getElementById("log-session");
const restartLoggingBtn = document.getElementById("restart-logging");
const rebootAllBtn = document.getElementById("reboot-all");
const failoverEl = document.getElementById("failover-status");
const controlFeedbackEl = document.getElementById("control-feedback");

let lastSnapshot = {};
let selectedChartNode = null;
const lastWindowSample = {};
const pendingActionByNode = {};
let uiBusyUntilMs = 0;

const LIMITS = {
  loadMin: 0,
  loadMax: 1000,
  loadStep: 100,
  blocksMin: 0,
  blocksMax: 24
};

const HISTORY_POINTS = 120;
const history = {};

function getNodeHistory(node) {
  if (!history[node]) {
    history[node] = { cpu: [], queue: [], load: [], miss: [], saturation: [] };
  }
  return history[node];
}

function pushHistory(node, series, value) {
  const nodeHistory = getNodeHistory(node);
  const arr = nodeHistory[series];
  arr.push(value);
  if (arr.length > HISTORY_POINTS) arr.shift();
}

function drawChart(canvas, values, min, max, color) {
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const w = canvas.width;
  const h = canvas.height;

  ctx.clearRect(0, 0, w, h);

  // Grid lines
  ctx.strokeStyle = "#1f2733";
  ctx.lineWidth = 1;
  for (let i = 1; i < 5; i++) {
    const y = (h * i) / 5;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }

  if (!values.length) return;

  const range = max - min || 1;
  const step = w / (HISTORY_POINTS - 1);

  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.beginPath();
  values.forEach((v, i) => {
    const x = i * step;
    const y = h - ((v - min) / range) * h;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();
}

function updateCharts(node, latest) {
  if (!node || !latest) return;
  if (!latest.window_ready) return;
  const nodeHistory = getNodeHistory(node);

  drawChart(chartCpu, nodeHistory.cpu, 0, 100, "#4cc9f0");
  drawChart(chartQueue, nodeHistory.queue, 0, 16, "#f4d35e");
  drawChart(chartLoad, nodeHistory.load, LIMITS.loadMin, LIMITS.loadMax, "#6be675");
  drawChart(chartMiss, nodeHistory.miss, 0, 100, "#ff6b6b");
  drawChart(chartSaturation, nodeHistory.saturation, 0, 1, "#f4d35e");

  const satNow = (latest.cpu ?? 0) >= 90 || (latest.miss ?? 0) > 0;
  if (chartCpuValue) chartCpuValue.textContent = String(latest.cpu ?? 0);
  if (chartQueueValue) chartQueueValue.textContent = String(latest.queue ?? 0);
  if (chartLoadValue) chartLoadValue.textContent = String(latest.load ?? 0);
  if (chartMissValue) chartMissValue.textContent = String(latest.miss ?? 0);
  if (chartSaturationValue) chartSaturationValue.textContent = satNow ? "1" : "0";

  if (statExecAvg) statExecAvg.textContent = String(latest.exec_avg ?? 0);
  if (statExecMax) statExecMax.textContent = String(latest.exec_max ?? 0);
  if (statMiss) statMiss.textContent = String(latest.miss ?? 0);
  if (statWindowReady) statWindowReady.textContent = latest.window_ready ? "Yes" : "No";
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function markUiBusy(ms = 1500) {
  uiBusyUntilMs = Date.now() + ms;
}

function markPendingAll(action) {
  const nodes = Object.keys(lastSnapshot || {});
  nodes.forEach((node) => {
    pendingActionByNode[node] = action;
  });
  render(lastSnapshot);
}

async function sendAction(node, action) {
  setControlFeedback(`Sending ${action} -> ${node} ...`, true);
  const res = await fetch("/api/control", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ node, action })
  });
  if (!res.ok) {
    const text = await res.text();
    console.error("[control] action failed", action, node, text);
    setControlFeedback(`Failed ${action} -> ${node}`, false);
    window.alert(`Control failed: ${action} ${node}`);
    return false;
  }
  setControlFeedback(`Sent ${action} -> ${node}`, true);
  return true;
}

function toggleMqttForNode(node, mqttOnFlag, btnEl) {
  const action = mqttOnFlag === 1 ? "FAIL_SILENT_ON" : "FAIL_SILENT_OFF";
  setControlFeedback(`Clicked ${action} -> ${node}`, true);
  if (btnEl) {
    btnEl.disabled = true;
    btnEl.classList.remove("on", "off");
    btnEl.classList.add(action === "FAIL_SILENT_ON" ? "off" : "on");
    btnEl.textContent = action === "FAIL_SILENT_ON" ? "MQTT OFF" : "MQTT ON";
  }
  markUiBusy(2500);
  pendingActionByNode[node] = action;
  render(lastSnapshot);
  sendAction(node, action).then(() => {
    if (action === "FAIL_SILENT_OFF") {
      delete pendingActionByNode[node];
    }
  }).finally(() => {
    if (btnEl) btnEl.disabled = false;
  });
}

function rebootNode(node, btnEl) {
  const confirmed = window.confirm(`REBOOT ${node}?`);
  if (!confirmed) return;
  setControlFeedback(`Clicked REBOOT -> ${node}`, true);
  if (btnEl) {
    btnEl.disabled = true;
    btnEl.classList.add("rebooting");
    btnEl.textContent = "REBOOTING...";
  }
  markUiBusy(3000);
  pendingActionByNode[node] = "REBOOT";
  render(lastSnapshot);
  sendAction(node, "REBOOT").finally(() => {
    if (btnEl) btnEl.disabled = false;
  });
}

function setControlFeedback(message, ok) {
  if (!controlFeedbackEl) return;
  controlFeedbackEl.textContent = `Control: ${message}`;
  controlFeedbackEl.classList.remove("ok", "err");
  controlFeedbackEl.classList.add(ok ? "ok" : "err");
}

if (chartNodeSelect) {
  chartNodeSelect.addEventListener("change", () => {
    selectedChartNode = chartNodeSelect.value;
    const sample = lastWindowSample[selectedChartNode] || lastSnapshot[selectedChartNode];
    updateCharts(selectedChartNode, sample);
  });
}

function isOnline(lastSeen) {
  return Date.now() / 1000 - lastSeen <= 5;
}

function render(data, options = {}) {
  const skipTable = options.skipTable === true;
  const nodes = Object.keys(data).sort();

  if (nodes.length === 0) {
    emptyState.hidden = false;
    return;
  }
  emptyState.hidden = true;

  lastSnapshot = data;

  // Update history only when window_ready=1 to align graphs with windowed stats.
  nodes.forEach((node) => {
    const item = data[node];
    if (item.window_ready) {
      lastWindowSample[node] = item;
      pushHistory(node, "cpu", item.cpu ?? 0);
      pushHistory(node, "queue", item.queue ?? 0);
      pushHistory(node, "load", item.load ?? 0);
      pushHistory(node, "miss", item.miss ?? 0);
      const saturated = (item.cpu ?? 0) >= 90 || (item.miss ?? 0) > 0;
      pushHistory(node, "saturation", saturated ? 1 : 0);
    }
  });


  // Populate select and keep selection stable.
  if (chartNodeSelect) {
    const current = selectedChartNode || chartNodeSelect.value;
    chartNodeSelect.innerHTML = "";
    nodes.forEach((node) => {
      const opt = document.createElement("option");
      opt.value = node;
      opt.textContent = node;
      chartNodeSelect.appendChild(opt);
    });

    let selected = current && nodes.includes(current) ? current : nodes[0];
    selectedChartNode = selected;
    chartNodeSelect.value = selectedChartNode;
    const sample = lastWindowSample[selectedChartNode] || data[selectedChartNode];
    updateCharts(selectedChartNode, sample);
  }

  if (skipTable) {
    return;
  }

  tableBody.innerHTML = "";

  nodes.forEach((node) => {
    const item = data[node];
    const online = isOnline(item.last_seen);
    console.log("[render] node", node, item);

    const pendingAction = pendingActionByNode[node] || "";
    const faultMode = item.fault_mode ?? "NORMAL";
    if (pendingAction === "FAIL_SILENT_ON" && faultMode === "SILENT") {
      delete pendingActionByNode[node];
    } else if (pendingAction === "FAIL_SILENT_OFF" && faultMode === "NORMAL") {
      delete pendingActionByNode[node];
    } else if (pendingAction === "REBOOT" && faultMode !== "REBOOTING") {
      // Node published again after reboot transition.
      delete pendingActionByNode[node];
    }
    const isSilent = faultMode === "SILENT" || pendingAction === "FAIL_SILENT_ON";
    const isRebooting = faultMode === "REBOOTING" || pendingAction === "REBOOT";
    const row = document.createElement("tr");
    row.innerHTML = `
    <td>${node}</td>
    <td>${item.fw ?? ""}</td>
    <td>${item.state ?? "SCHEDULABLE"}</td>
    <td>${item.cpu}</td>
    <td>${item.queue}</td>
    <td>${item.load}</td>
    <td>${item.eff_blocks ?? 0}</td>
    <td>${item.ctrl_latency_ms ?? 0}</td>
    <td>${item.telemetry_latency_ms ?? 0}</td>
    <td>${item.window_ready ? "Yes" : "No"}</td>
    <td>${faultMode}</td>
    <td class="status ${online ? "online" : "offline"}">
      ${online ? "Online" : "Offline"}
    </td>
    <td>
      <div class="slider">
        <input
          type="range"
          min="${LIMITS.loadMin}"
          max="${LIMITS.loadMax}"
          step="${LIMITS.loadStep}"
          value="${clamp(item.load ?? 0, LIMITS.loadMin, LIMITS.loadMax)}"
          data-node="${node}"
          data-type="load-slider"
        />
        <span class="slider-value">${clamp(item.load ?? 0, LIMITS.loadMin, LIMITS.loadMax)}</span>
      </div>
      <div class="controls">
        <label>
          Load
        <input
          type="number"
          min="${LIMITS.loadMin}"
          max="${LIMITS.loadMax}"
          step="${LIMITS.loadStep}"
          value="${clamp(item.load ?? 0, LIMITS.loadMin, LIMITS.loadMax)}"
            data-node="${node}"
            data-type="load-input"
          />
        </label>
        <label>
          Blocks
        <input
          type="number"
          min="${LIMITS.blocksMin}"
          max="${LIMITS.blocksMax}"
          step="1"
          value="${clamp(item.eff_blocks ?? 0, LIMITS.blocksMin, LIMITS.blocksMax)}"
            data-node="${node}"
            data-type="blocks-input"
          />
        </label>
      </div>
      <div class="node-actions">
        <button
          type="button"
          data-type="mqtt-toggle"
          data-node="${node}"
          data-mqtt-on="${isSilent ? "0" : "1"}"
          class="mqtt-toggle ${isSilent ? "off" : "on"}"
        >${isSilent ? "MQTT OFF" : "MQTT ON"}</button>
        <button
          type="button"
          data-type="reboot-btn"
          data-node="${node}"
          class="reboot-btn ${isRebooting ? "rebooting" : ""}"
        >${isRebooting ? "REBOOTING..." : "REBOOT"}</button>
      </div>
    </td>
  `;

    const mqttBtn = row.querySelector("button[data-type='mqtt-toggle']");
    if (mqttBtn) {
      mqttBtn.addEventListener("click", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        const mqttOn = mqttBtn.dataset.mqttOn === "1";
        toggleMqttForNode(node, mqttOn ? 1 : 0, mqttBtn);
      });
    }

    const rebootBtn = row.querySelector("button[data-type='reboot-btn']");
    if (rebootBtn) {
      rebootBtn.addEventListener("click", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        rebootNode(node, rebootBtn);
      });
    }

    tableBody.appendChild(row);
  });
}

async function fetchState() {
  try {
    const res = await fetch("/api/state");
    if (!res.ok) return;
    const data = await res.json();
    console.log("[fetchState] data", data);
    const active = document.activeElement;
    const isEditing =
      active &&
      active.tagName === "INPUT" &&
      (active.dataset.type === "load-input" || active.dataset.type === "blocks-input");
    const isBusy = Date.now() < uiBusyUntilMs;
    render(data, { skipTable: isEditing || isBusy });
  } catch (err) {
    // ignore transient errors
  }
}

async function fetchLogSession() {
  try {
    const res = await fetch("/api/logging/status");
    if (!res.ok) return;
    const data = await res.json();
    if (logSessionEl && data.session_id) {
      logSessionEl.textContent = `Log session: ${data.session_id}`;
    }
  } catch (err) {
    // ignore transient errors
  }
}

async function fetchFailover() {
  try {
    const res = await fetch("/api/failover");
    if (!res.ok) return;
    const data = await res.json();
    if (!failoverEl) return;
    const items = data.events || [];
    const lines = items.map((e) => {
      const status = e.status === "failed" ? "FAILED" : "OK";
      return `${e.node_id}: ${status} (age=${e.age_sec}s)`;
    });
    failoverEl.textContent = lines.join(" | ");
  } catch (err) {
    // ignore transient errors
  }
}
async function restartLogging() {
  try {
    const res = await fetch("/api/logging/restart", { method: "POST" });
    if (!res.ok) return;
    const data = await res.json();
    if (logSessionEl && data.session_id) {
      logSessionEl.textContent = `Log session: ${data.session_id}`;
    }
  } catch (err) {
    // ignore transient errors
  }
}

async function sendControl(node, load) {
  await fetch("/api/control", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ node, load })
  });
}

document.addEventListener("input", (e) => {
  const slider = e.target.closest("input[data-type='load-slider']");
  if (!slider) return;

  const node = slider.dataset.node;
  const raw = parseInt(slider.value, 10);
  if (Number.isNaN(raw)) return;

  const value = clamp(raw, LIMITS.loadMin, LIMITS.loadMax);
  const valueEl = slider.parentElement?.querySelector(".slider-value");
  if (valueEl) valueEl.textContent = String(value);

  const row = slider.closest("tr");
  const loadInput = row?.querySelector("input[data-type='load-input']");
  if (loadInput) loadInput.value = String(value);
});

document.addEventListener("change", (e) => {
  const slider = e.target.closest("input[data-type='load-slider']");
  if (!slider) return;

  const node = slider.dataset.node;
  const raw = parseInt(slider.value, 10);
  if (Number.isNaN(raw)) return;

  const value = clamp(raw, LIMITS.loadMin, LIMITS.loadMax);
  sendControl(node, value);
});

if (tableBody) {
  tableBody.addEventListener("mousedown", () => markUiBusy(1800));
  tableBody.addEventListener("touchstart", () => markUiBusy(1800), { passive: true });
}

document.addEventListener("change", (e) => {
  const input = e.target.closest("input[data-type='load-input']");
  if (!input) return;

  const node = input.dataset.node;
  const raw = parseInt(input.value, 10);
  if (Number.isNaN(raw)) return;

  const value = clamp(raw, LIMITS.loadMin, LIMITS.loadMax);
  input.value = String(value);

  const row = input.closest("tr");
  const slider = row?.querySelector("input[data-type='load-slider']");
  if (slider) slider.value = String(value);
  const valueEl = row?.querySelector(".slider-value");
  if (valueEl) valueEl.textContent = String(value);

  sendControl(node, value);
});

document.addEventListener("change", (e) => {
  const input = e.target.closest("input[data-type='blocks-input']");
  if (!input) return;

  const node = input.dataset.node;
  const raw = parseInt(input.value, 10);
  if (Number.isNaN(raw)) return;

  const value = clamp(raw, LIMITS.blocksMin, LIMITS.blocksMax);
  input.value = String(value);

  fetch("/api/control", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      node,
      blocks: value
    })
  });
});

fetchState();
setInterval(fetchState, 1000);
fetchLogSession();
setInterval(fetchFailover, 1000);

if (restartLoggingBtn) {
  restartLoggingBtn.addEventListener("click", restartLogging);
}

if (rebootAllBtn) {
  rebootAllBtn.addEventListener("click", () => {
    if (!window.confirm("REBOOT all nodes?")) return;
    setControlFeedback(`Clicked REBOOT -> ALL (${Object.keys(lastSnapshot || {}).length} nodes)`, true);
    markUiBusy(4000);
    markPendingAll("REBOOT");
    sendAction("all", "REBOOT");
  });
}
