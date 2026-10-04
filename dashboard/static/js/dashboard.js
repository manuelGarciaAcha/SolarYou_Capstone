// Live dashboard: listens for "sensor_update" events pushed by the Flask
// server and updates the chart, the readout, and the rolling average
// power card. Also listens for "connection_status" so the badge shows
// what is actually going on, not just what the server's terminal says.
//
// Each sensor_update carries only some of {pitch, roll, power}:
//   a number  -> show it
//   null      -> the sensor reported itself invalid, show "--" right away
//   missing   -> this message didn't mention it, keep showing the last
//                value (until it goes stale, see STALE_MS)

const MAX_POINTS = 30;
const POWER_AVG_WINDOW = 60;
// A readout that stops receiving values goes back to "--" after this long,
// so a dead sensor can't sit on screen looking current.
const STALE_MS = 5000;
// If the link is up but no message has arrived for this long, the badge says so.
const NO_DATA_MS = 5000;

let powerHistory = [];

const ctx = document.getElementById("orientationChart").getContext("2d");
const orientationChart = new Chart(ctx, {
    type: "line",
    data: {
        labels: [],
        datasets: [
            { label: "Pitch", data: [], borderColor: "#0a84ff", borderWidth: 2, pointRadius: 0, tension: 0.3 },
            { label: "Roll", data: [], borderColor: "#ff453a", borderWidth: 2, pointRadius: 0, tension: 0.3 },
        ],
    },
    options: {
        animation: false,
        responsive: true,
        plugins: { legend: { labels: { usePointStyle: true } } },
        scales: { y: { title: { display: true, text: "Degrees" } } },
    },
});

const powerAvgCtx = document.getElementById("powerAvgChart").getContext("2d");
const powerAvgChart = new Chart(powerAvgCtx, {
    type: "line",
    data: {
        labels: [],
        datasets: [{
            label: "Avg Power", data: [], borderColor: "#30d158",
            backgroundColor: "rgba(48, 209, 88, 0.12)", borderWidth: 2,
            pointRadius: 0, tension: 0.35, fill: true,
        }],
    },
    options: {
        animation: false, responsive: true,
        plugins: { legend: { display: false } },
        scales: { y: { display: false }, x: { display: false } },
    },
});

function isNumber(value) {
    return typeof value === "number" && isFinite(value);
}

function updateChart(reading) {
    // No orientation data in this message: add nothing. Pushing null would
    // fragment the line, and Chart.js can bridge a gap with a straight line
    // through a stretch where we don't actually know what the platform did.
    if (!isNumber(reading.pitch)) return;

    orientationChart.data.labels.push(new Date().toLocaleTimeString());
    orientationChart.data.datasets[0].data.push(reading.pitch);
    orientationChart.data.datasets[1].data.push(isNumber(reading.roll) ? reading.roll : null);
    if (orientationChart.data.labels.length > MAX_POINTS) {
        orientationChart.data.labels.shift();
        orientationChart.data.datasets[0].data.shift();
        orientationChart.data.datasets[1].data.shift();
    }
    orientationChart.update();
}

const READOUTS = {
    pitch: { id: "pitchValue", unit: "\u00b0" },
    roll: { id: "rollValue", unit: "\u00b0" },
    power: { id: "powerValue", unit: " W" },
};
const lastSeen = { pitch: 0, roll: 0, power: 0 };

function updateReadout(reading) {
    const now = Date.now();
    for (const key in READOUTS) {
        if (!(key in reading)) continue; // not in this message, keep what is shown
        const el = document.getElementById(READOUTS[key].id);
        if (isNumber(reading[key])) {
            el.innerText = reading[key].toFixed(2) + READOUTS[key].unit;
            lastSeen[key] = now;
        } else {
            el.innerText = "--"; // the sensor said this value is invalid
            lastSeen[key] = 0;
        }
    }
}

function expireStaleReadouts() {
    const now = Date.now();
    for (const key in READOUTS) {
        if (lastSeen[key] && now - lastSeen[key] > STALE_MS) {
            document.getElementById(READOUTS[key].id).innerText = "--";
            lastSeen[key] = 0;
        }
    }
}

function updatePowerAverage(reading) {
    // Nothing to average if this message had no usable power value.
    if (!isNumber(reading.power)) return;

    powerHistory.push(reading.power);
    if (powerHistory.length > POWER_AVG_WINDOW) powerHistory.shift();
    const average = powerHistory.reduce((t, v) => t + v, 0) / powerHistory.length;
    document.getElementById("powerAvgValue").innerText = average.toFixed(2);
    powerAvgChart.data.labels.push("");
    powerAvgChart.data.datasets[0].data.push(average);
    if (powerAvgChart.data.labels.length > MAX_POINTS) {
        powerAvgChart.data.labels.shift();
        powerAvgChart.data.datasets[0].data.shift();
    }
    powerAvgChart.update();
}

// Connection badge. The server reports whether the link to the ESP32 is up;
// this page adds whether data is actually arriving, because a link can be
// up with nothing coming through it.
const STATUS_TEXT = {
    connecting: "Connecting...",
    connected: "Connected",
    reconnecting: "Reconnecting...",
    error: "Connection Error",
};

let serverStatus = { status: "connecting" };
let lastDataAt = null;

function renderStatus() {
    let cls = serverStatus.status;
    let text = STATUS_TEXT[cls] || cls;
    if (cls === "connected") {
        if (lastDataAt === null) {
            cls = "connecting";
            text = "Connected, waiting for data";
        } else if (Date.now() - lastDataAt > NO_DATA_MS) {
            cls = "reconnecting";
            text = "Connected, no recent data";
        }
    }
    const el = document.getElementById("connectionStatus");
    el.className = "status-badge status-" + cls;
    el.innerText = text;
    el.title = serverStatus.message || ""; // hover for the actual error text
}

const socket = io();
socket.on("sensor_update", (reading) => {
    if (!reading || typeof reading !== "object") return;
    lastDataAt = Date.now();
    updateChart(reading);
    updateReadout(reading);
    updatePowerAverage(reading);
    renderStatus();
});
socket.on("connection_status", (status) => {
    serverStatus = status;
    renderStatus();
});
// The dashboard server itself went away (stopped, laptop asleep, phone out
// of WiFi range). The page keeps retrying on its own; until it succeeds,
// don't leave a green badge sitting over frozen numbers.
socket.on("disconnect", () => {
    serverStatus = {
        status: "reconnecting",
        message: "Lost connection to the dashboard server. This page keeps retrying.",
    };
    renderStatus();
});

setInterval(() => {
    expireStaleReadouts();
    renderStatus();
}, 1000);
