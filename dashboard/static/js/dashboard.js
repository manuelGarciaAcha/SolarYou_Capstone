// Live dashboard: listens for "sensor_update" events pushed by the Flask
// server and updates the chart, the raw readout values, and the rolling
// average power card. Also listens for "connection_status" so the
// dashboard itself shows whether the data source is actually connected,
// rather than that only being visible in the server's terminal output.

const MAX_POINTS = 30;
const POWER_AVG_WINDOW = 60;
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

function updateChart(reading) {
    // Skip entirely if this update didn't include orientation data --
    // pushing null would either fragment the line with gaps or, worse,
    // get bridged over by Chart.js into a straight line through a
    // stretch where we genuinely don't know what the platform was
    // doing. Better to just not add a point than to show either.
    if (reading.pitch === null || reading.pitch === undefined) return;

    const label = new Date().toLocaleTimeString();
    orientationChart.data.labels.push(label);
    orientationChart.data.datasets[0].data.push(reading.pitch);
    orientationChart.data.datasets[1].data.push(reading.roll);
    if (orientationChart.data.labels.length > MAX_POINTS) {
        orientationChart.data.labels.shift();
        orientationChart.data.datasets[0].data.shift();
        orientationChart.data.datasets[1].data.shift();
    }
    orientationChart.update();
}

function updateReadout(reading) {
    // reading.pitch/roll/power may be null if that sensor group wasn't
    // included in this particular message (they update at different
    // rates on the ESP32 side) -- show "--" rather than literally
    // printing "null" on screen.
    const fmt = (value, unit) => (value === null || value === undefined) ? "--" : `${value}${unit}`;
    document.getElementById("pitchValue").innerText = fmt(reading.pitch, "°");
    document.getElementById("rollValue").innerText = fmt(reading.roll, "°");
    document.getElementById("powerValue").innerText = fmt(reading.power, " W");
}

function updatePowerAverage(reading) {
    // skip entirely if this update didn't include a power reading --
    // averaging in a null would corrupt the running average
    if (reading.power === null || reading.power === undefined) return;

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

// Connection status: mirrors what the server prints to its own terminal,
// but visible on the dashboard itself, since that's what's actually
// being watched during a demo.
const STATUS_TEXT = {
    connecting: "Connecting...",
    connected: "Connected",
    reconnecting: "Reconnecting...",
    error: "Connection Error",
};

function updateConnectionStatus(status) {
    const el = document.getElementById("connectionStatus");
    el.className = `status-badge status-${status.status}`;
    el.innerText = STATUS_TEXT[status.status] || status.status;
    if (status.message) {
        el.title = status.message; // hover to see the actual error text
    }
}

const socket = io();
socket.on("sensor_update", (reading) => {
    updateChart(reading);
    updateReadout(reading);
    updatePowerAverage(reading);
});
socket.on("connection_status", updateConnectionStatus);
