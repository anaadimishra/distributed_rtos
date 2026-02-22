const tableBody = document.getElementById("telemetry-body");
const emptyState = document.getElementById("empty-state");

function isOnline(lastSeen) {
  return Date.now() / 1000 - lastSeen <= 5;
}

function render(data) {
  const nodes = Object.keys(data).sort();
  tableBody.innerHTML = "";

  if (nodes.length === 0) {
    emptyState.hidden = false;
    return;
  }
  emptyState.hidden = true;

  nodes.forEach((node) => {
    const item = data[node];
    const online = isOnline(item.last_seen);

    const row = document.createElement("tr");
    row.innerHTML = `
      <td>${node}</td>
      <td>${item.cpu}</td>
      <td>${item.queue}</td>
      <td>${item.load}</td>
      <td class="status ${online ? "online" : "offline"}">
        ${online ? "Online" : "Offline"}
      </td>
      <td>
        <div class="controls">
          <button data-node="${node}" data-delta="-100">-100</button>
          <button data-node="${node}" data-delta="100">+100</button>
        </div>
      </td>
    `;
    tableBody.appendChild(row);
  });
}

async function fetchState() {
  try {
    const res = await fetch("/api/state");
    if (!res.ok) return;
    const data = await res.json();
    render(data);
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

document.addEventListener("click", (e) => {
  const btn = e.target.closest("button[data-node]");
  if (!btn) return;
  const node = btn.dataset.node;
  const delta = parseInt(btn.dataset.delta, 10);
  const row = btn.closest("tr");
  const loadCell = row.children[3];
  const current = parseInt(loadCell.textContent, 10);
  if (Number.isNaN(current)) return;
  sendControl(node, current + delta);
});

fetchState();
setInterval(fetchState, 1000);
