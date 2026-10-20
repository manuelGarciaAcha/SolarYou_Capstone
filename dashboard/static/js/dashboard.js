// Live dashboard. Listens for "sensor_update" and "connection_status" events
// from the Flask server and draws the whole page from three values: pitch,
// roll, and power.
//
// The firmware may also send pitch_target and roll_target (the sun angle the
// controller is steering toward). While those arrive, the level card and the
// attitude outline measure error from the target. Without them the target
// is level (0, 0), so older firmware gets the same page as before.
//
// A sensor_update doesn't have to carry every field. For each one:
//   a number  -> show it
//   null      -> the sensor reported itself invalid, show "--" right away
//   missing   -> this message didn't mention it, keep showing the last
//                value (until it goes stale, see STALE_MS)
//
// Messages are only recorded when they arrive. The page is redrawn on a
// timer (at most RENDER_MS apart), so a fast data rate costs almost nothing
// extra in the browser.

// ---- settings the team may want to change ----

const CHART_WINDOW_MS = 30000;      // pitch/roll chart history
const CHART_MIN_GAP_MS = 50;        // at most 20 chart points per second per line
const STATS_WINDOW_MS = 30000;      // window for RMS tilt and the "within" stat
const BAND_DEG = 2;                 // "within N degrees of level" (placeholder, agree as a team)
const POWER_AVG_WINDOW_MS = 60000;  // average power window
const POWER_CHART_MS = 60000;       // power chart history
const LEVEL_RANGE_DEG = 10;         // outer ring of the bubble level
const TRAIL_MS = 3000;              // length of the trail behind the bubble
const ENERGY_MAX_GAP_MS = 5000;     // don't count energy across a gap longer than this
const CSV_MAX_ROWS = 200000;        // about 11 hours at 5 messages per second
const STALE_MS = 5000;              // a readout with no update for this long shows "--"
const NO_DATA_MS = 5000;            // badge says "no recent data" after this long
const RENDER_MS = 100;              // redraw at most 10 times a second while data flows
const IDLE_RENDER_MS = 500;         // and twice a second while it doesn't

// Which way positive angles are drawn. If a drawing tilts the opposite way
// from the real platform, flip the matching sign to -1.
const PITCH_DRAW_SIGN = 1;
const ROLL_DRAW_SIGN = 1;

// Colors come from the CSS variables at the top of style.css, so the theme
// switch and the ?palette= option change the charts too.
function readColors() {
    const css = getComputedStyle(document.documentElement);
    const get = (name) => css.getPropertyValue(name).trim();
    return {
        pitch: get("--pitch"), roll: get("--roll"), power: get("--power"),
        ink2: get("--ink-2"), ink3: get("--ink-3"), grid: get("--grid"), axis: get("--axis"),
    };
}

let colors = readColors();
// Chart options call these on every redraw, so a theme switch only has to
// refresh `colors` and redraw.
const colorOf = (key) => () => colors[key];
const washOf = (key) => () => colors[key] + "1f"; // same color, about 12% opacity

// ---- state ----

const KEYS = ["pitch", "roll", "power", "pitch_target", "roll_target"];
const latest = {};
for (const key of KEYS) latest[key] = { value: null, at: 0 };

// {x: time ms, y: degrees}, one list per chart line
const chartPoints = { pitch: [], roll: [], pitch_target: [], roll_target: [] };
const lastChartPointAt = { pitch: 0, roll: 0, pitch_target: 0, roll_target: 0 };
const tiltSamples = [];      // [time, degrees off target], for the level card stats
const trail = [];            // [time, roll error, pitch error], for the bubble level
const powerSamples = [];     // [time, watts], for the average
const powerPoints = [];      // {x, y}, the power chart
let lastPowerPointAt = 0;
let energyWh = 0;
let lastPowerForEnergy = null;
let runNumber = 1;
const csvRows = [];          // [time, run, pitch, roll, power, pitch_target, roll_target]
let csvDropped = false;

let serverStatus = { status: "connecting" };
let lastDataAt = null;
let dirty = true;
let lastRenderAt = 0;
let lastPruneAt = 0;

// ---- small helpers ----

function isNumber(value) {
    return typeof value === "number" && isFinite(value);
}

function freshValue(key, now) {
    const entry = latest[key];
    if (entry.value === null || now - entry.at > STALE_MS) return null;
    return entry.value;
}

// Pitch and roll measured from the target, or from level when no target is
// arriving. null if either angle is missing or stale.
function currentError(now) {
    const pitch = freshValue("pitch", now);
    const roll = freshValue("roll", now);
    if (pitch === null || roll === null) return null;
    const pitchTarget = freshValue("pitch_target", now);
    const rollTarget = freshValue("roll_target", now);
    const tracking = pitchTarget !== null && rollTarget !== null;
    return {
        pitch: tracking ? pitch - pitchTarget : pitch,
        roll: tracking ? roll - rollTarget : roll,
        tracking: tracking,
    };
}

function isTracking(now) {
    return freshValue("pitch_target", now) !== null && freshValue("roll_target", now) !== null;
}

function pruneBefore(list, cutoff, timeOf) {
    let i = 0;
    while (i < list.length && timeOf(list[i]) < cutoff) i++;
    if (i > 0) list.splice(0, i);
}

const timeOfPair = (item) => item[0];
const timeOfPoint = (item) => item.x;

function pruneAll(now) {
    for (const key in chartPoints) pruneBefore(chartPoints[key], now - CHART_WINDOW_MS, timeOfPoint);
    pruneBefore(tiltSamples, now - STATS_WINDOW_MS, timeOfPair);
    pruneBefore(trail, now - TRAIL_MS, timeOfPair);
    pruneBefore(powerSamples, now - POWER_AVG_WINDOW_MS, timeOfPair);
    pruneBefore(powerPoints, now - POWER_CHART_MS, timeOfPoint);
}

function computeStats(samples) {
    if (samples.length === 0) return null;
    let sumSquares = 0;
    let within = 0;
    for (const sample of samples) {
        sumSquares += sample[1] * sample[1];
        if (sample[1] <= BAND_DEG) within++;
    }
    return {
        rms: Math.sqrt(sumSquares / samples.length),
        withinPercent: (100 * within) / samples.length,
    };
}

function formatEnergy(wh) {
    const mwh = wh * 1000;
    if (Math.abs(mwh) < 1000) return mwh.toFixed(Math.abs(mwh) < 10 ? 2 : 1) + " mWh";
    return wh.toFixed(3) + " Wh";
}

function formatDegrees(value, digits) {
    if (value === null) return "--";
    // a real minus sign: the hyphen looks detached in the big tabular numbers
    return value.toFixed(digits).replace("-", "\u2212") + "°";
}

// ---- incoming data ----

function handleReading(reading) {
    if (!reading || typeof reading !== "object") return;
    const now = Date.now();
    lastDataAt = now;

    for (const key of KEYS) {
        if (key in reading) {
            latest[key] = { value: isNumber(reading[key]) ? reading[key] : null, at: now };
        }
    }

    // CSV keeps every message as received (missing and null both become blank cells)
    csvRows.push([now, runNumber, reading.pitch, reading.roll, reading.power,
                  reading.pitch_target, reading.roll_target]);
    if (csvRows.length > CSV_MAX_ROWS) {
        csvRows.splice(0, Math.floor(CSV_MAX_ROWS / 10));
        csvDropped = true;
    }

    // chart points, thinned so a fast stream doesn't flood the chart
    for (const key in chartPoints) {
        if (isNumber(reading[key]) && now - lastChartPointAt[key] >= CHART_MIN_GAP_MS) {
            chartPoints[key].push({ x: now, y: reading[key] });
            lastChartPointAt[key] = now;
        }
    }

    // error needs both angles to be current
    if ("pitch" in reading || "roll" in reading) {
        const error = currentError(now);
        if (error) {
            tiltSamples.push([now, Math.hypot(error.pitch, error.roll)]);
            trail.push([now, error.roll, error.pitch]);
        }
    }

    // power: rolling average and energy total
    if ("power" in reading) {
        if (isNumber(reading.power)) {
            powerSamples.push([now, reading.power]);
            if (now - lastPowerPointAt >= CHART_MIN_GAP_MS) {
                powerPoints.push({ x: now, y: reading.power });
                lastPowerPointAt = now;
            }
            const last = lastPowerForEnergy;
            if (last && now > last.at && now - last.at <= ENERGY_MAX_GAP_MS) {
                energyWh += ((reading.power + last.watts) / 2) * ((now - last.at) / 3600000);
            }
            lastPowerForEnergy = { at: now, watts: reading.power };
        } else {
            lastPowerForEnergy = null; // invalid reading, don't bridge across it
        }
    }

    // keep lists bounded even while the tab is hidden and nothing redraws
    if (now - lastPruneAt > 1000) {
        pruneAll(now);
        lastPruneAt = now;
    }
    dirty = true;
}

// ---- charts ----

function relativeTimeTicks(windowMs, stepMs) {
    return function (axis) {
        const ticks = [];
        for (let t = 0; t <= windowMs; t += stepMs) ticks.push({ value: axis.max - t });
        axis.ticks = ticks.reverse();
    };
}

// Grid, axis line, and label colors shared by both charts
const axisStyle = {
    grid: { color: colorOf("grid"), drawTicks: false },
    border: { color: colorOf("axis") },
};
const tickColor = colorOf("ink3");
const titleStyle = (text) => ({ display: true, text: text, color: colorOf("ink3") });

function secondsAgoLabel(value, axisMax) {
    const seconds = Math.round((axisMax - value) / 1000);
    return seconds === 0 ? "now" : "-" + seconds + "s";
}

const orientationChart = new Chart(document.getElementById("orientationChart").getContext("2d"), {
    type: "line",
    data: {
        datasets: [
            { label: "Pitch", data: chartPoints.pitch, borderColor: colorOf("pitch"), backgroundColor: colorOf("pitch"),
              borderWidth: 2, pointRadius: 0, pointHoverRadius: 4, cubicInterpolationMode: "monotone" },
            { label: "Roll", data: chartPoints.roll, borderColor: colorOf("roll"), backgroundColor: colorOf("roll"),
              borderWidth: 2, pointRadius: 0, pointHoverRadius: 4, cubicInterpolationMode: "monotone" },
            // dashed target lines, only drawn while the firmware sends targets
            { label: "Pitch target", data: chartPoints.pitch_target, borderColor: colorOf("pitch"), backgroundColor: colorOf("pitch"),
              borderWidth: 1.5, borderDash: [6, 4], pointRadius: 0, pointHoverRadius: 3, pointStyle: "line" },
            { label: "Roll target", data: chartPoints.roll_target, borderColor: colorOf("roll"), backgroundColor: colorOf("roll"),
              borderWidth: 1.5, borderDash: [6, 4], pointRadius: 0, pointHoverRadius: 3, pointStyle: "line" },
        ],
    },
    options: {
        animation: false,
        parsing: false,
        normalized: true,
        responsive: true,
        maintainAspectRatio: false,
        interaction: { mode: "nearest", axis: "x", intersect: false },
        plugins: {
            legend: {
                labels: {
                    usePointStyle: true,
                    boxHeight: 6,
                    color: colorOf("ink2"),
                    // hide the target entries until there is something to show
                    filter: (item, data) => data.datasets[item.datasetIndex].data.length > 0,
                },
            },
            tooltip: {
                callbacks: {
                    title: (items) => items.length
                        ? secondsAgoLabel(items[0].parsed.x, items[0].chart.scales.x.max) : "",
                    label: (item) => item.dataset.label + ": " + item.parsed.y.toFixed(2) + "°",
                },
            },
        },
        scales: {
            x: {
                type: "linear",
                min: Date.now() - CHART_WINDOW_MS,
                max: Date.now(),
                afterBuildTicks: relativeTimeTicks(CHART_WINDOW_MS, 5000),
                // "this" is the axis, so labels count back from its right edge
                ticks: { color: tickColor, padding: 6, callback: function (value) { return secondsAgoLabel(value, this.max); } },
                ...axisStyle,
            },
            y: {
                suggestedMin: -5,
                suggestedMax: 5,
                title: titleStyle("Degrees"),
                ticks: { color: tickColor, padding: 6 },
                ...axisStyle,
            },
        },
    },
});

const powerChart = new Chart(document.getElementById("powerChart").getContext("2d"), {
    type: "line",
    data: {
        datasets: [{
            label: "Power", data: powerPoints, borderColor: colorOf("power"), backgroundColor: washOf("power"), fill: true,
            borderWidth: 2, pointRadius: 0, pointHoverRadius: 4, cubicInterpolationMode: "monotone",
        }],
    },
    options: {
        animation: false,
        parsing: false,
        normalized: true,
        responsive: true,
        maintainAspectRatio: false,
        interaction: { mode: "nearest", axis: "x", intersect: false },
        plugins: {
            legend: { display: false }, // one line, the card title names it
            tooltip: {
                callbacks: {
                    title: (items) => items.length
                        ? secondsAgoLabel(items[0].parsed.x, items[0].chart.scales.x.max) : "",
                    label: (item) => item.parsed.y.toFixed(2) + " W",
                },
            },
        },
        scales: {
            x: {
                type: "linear",
                min: Date.now() - POWER_CHART_MS,
                max: Date.now(),
                afterBuildTicks: relativeTimeTicks(POWER_CHART_MS, 10000),
                ticks: { color: tickColor, padding: 6, callback: function (value) { return secondsAgoLabel(value, this.max); } },
                ...axisStyle,
            },
            y: {
                beginAtZero: true,
                suggestedMax: 1,
                title: titleStyle("Watts"),
                ticks: { color: tickColor, padding: 6 },
                ...axisStyle,
            },
        },
    },
});

// Re-read the CSS colors after a theme change and redraw both charts.
function applyColors() {
    colors = readColors();
    orientationChart.update("none");
    powerChart.update("none");
}

// ---- drawing ----

const el = (id) => document.getElementById(id);
const LEVEL_SCALE = 90 / LEVEL_RANGE_DEG; // svg units per degree, outer ring is r=90

function levelPoint(roll, pitch) {
    let x = ROLL_DRAW_SIGN * roll * LEVEL_SCALE;
    let y = -PITCH_DRAW_SIGN * pitch * LEVEL_SCALE;
    const distance = Math.hypot(x, y);
    const outside = distance > 90;
    if (outside) {
        x = (x * 90) / distance;
        y = (y * 90) / distance;
    }
    return { x: x, y: y, outside: outside };
}

function renderReadouts(now) {
    el("pitchValue").innerText = formatDegrees(freshValue("pitch", now), 1);
    el("rollValue").innerText = formatDegrees(freshValue("roll", now), 1);
    const pitchTarget = freshValue("pitch_target", now);
    const rollTarget = freshValue("roll_target", now);
    el("pitchSub").innerText = pitchTarget === null ? "degrees" : "target " + formatDegrees(pitchTarget, 1);
    el("rollSub").innerText = rollTarget === null ? "degrees" : "target " + formatDegrees(rollTarget, 1);
    const power = freshValue("power", now);
    el("powerValue").innerText = power === null ? "--" : power.toFixed(2);
}

// Labels follow whether a target is arriving, so the page never calls
// target error "tilt from level".
let shownTracking = null;
function renderMode(now) {
    const tracking = isTracking(now);
    if (tracking === shownTracking) return;
    shownTracking = tracking;
    el("levelTitle").innerText = tracking ? "Error from target" : "Tilt from level";
    el("rmsLabel").innerText = tracking ? "RMS error" : "RMS tilt";
    el("bandRef").innerText = tracking ? "target" : "level";
    el("attitudeNote").innerText = tracking ? "outline = target" : "outline = level";
}

function renderStats(now) {
    const error = currentError(now);
    el("tiltNow").innerText = error ? formatDegrees(Math.hypot(error.pitch, error.roll), 1) : "--";

    const stats = computeStats(tiltSamples);
    el("tiltRms").innerText = stats ? formatDegrees(stats.rms, 1) : "--";
    el("withinBand").innerText = stats ? Math.round(stats.withinPercent) + "%" : "--";

    el("energy").innerText = powerSamples.length || energyWh !== 0 ? formatEnergy(energyWh) : "--";
}

function renderLevel(now) {
    const error = currentError(now);
    const dot = el("levelDot");
    if (!error) {
        dot.setAttribute("visibility", "hidden");
        el("levelTrail").setAttribute("points", "");
        return;
    }
    const spot = levelPoint(error.roll, error.pitch);
    dot.setAttribute("cx", spot.x.toFixed(1));
    dot.setAttribute("cy", spot.y.toFixed(1));
    dot.setAttribute("class", spot.outside ? "level-dot out-of-range" : "level-dot");
    dot.setAttribute("visibility", "visible");

    const points = [];
    for (const sample of trail) {
        const p = levelPoint(sample[1], sample[2]);
        points.push(p.x.toFixed(1) + "," + p.y.toFixed(1));
    }
    el("levelTrail").setAttribute("points", points.join(" "));
}

function renderAttitude(now) {
    const views = [
        ["pitch", "pitchPanel", "pitchGhost", "pitchViewValue", PITCH_DRAW_SIGN],
        ["roll", "rollPanel", "rollGhost", "rollViewValue", ROLL_DRAW_SIGN],
    ];
    // SVG rotates clockwise for positive angles, so negate to tilt the right side up
    const rotate = (degrees, sign) => "rotate(" + (degrees === null ? 0 : -sign * degrees).toFixed(2) + ")";
    for (const [key, groupId, ghostId, labelId, sign] of views) {
        const value = freshValue(key, now);
        const group = el(groupId);
        group.setAttribute("transform", rotate(value, sign));
        group.setAttribute("class", value === null ? "panel-group no-data" : "panel-group");
        el(ghostId).setAttribute("transform", rotate(freshValue(key + "_target", now), sign));
        el(labelId).innerText = formatDegrees(value, 1);
    }
}

function renderCharts(now) {
    const x = orientationChart.options.scales.x;
    x.min = now - CHART_WINDOW_MS;
    x.max = now;
    orientationChart.update("none");

    let sum = 0;
    for (const sample of powerSamples) sum += sample[1];
    el("powerAvgValue").innerText = powerSamples.length ? (sum / powerSamples.length).toFixed(2) : "--";
    const px = powerChart.options.scales.x;
    px.min = now - POWER_CHART_MS;
    px.max = now;
    powerChart.update("none");
}

// Connection badge. The server reports whether the link to the ESP32 is up.
// This page adds whether data is arriving, because a link can be up with
// nothing coming through it.
const STATUS_TEXT = {
    connecting: "Connecting...",
    connected: "Connected",
    reconnecting: "Reconnecting...",
    error: "Connection Error",
};

function renderStatus(now) {
    let cls = serverStatus.status;
    let text = STATUS_TEXT[cls] || cls;
    if (cls === "connected") {
        if (lastDataAt === null) {
            cls = "connecting";
            text = "Connected, waiting for data";
        } else if (now - lastDataAt > NO_DATA_MS) {
            cls = "reconnecting";
            text = "Connected, no recent data";
        }
    }
    const badge = el("connectionStatus");
    badge.className = "status-badge status-" + cls;
    badge.innerText = text;
    badge.title = serverStatus.message || ""; // hover for the actual error text
}

function renderCsvCount() {
    const count = csvRows.length.toLocaleString();
    el("csvCount").innerText = "(" + count + " rows" + (csvDropped ? ", oldest dropped" : "") + ")";
}

function render(now) {
    pruneAll(now);
    renderMode(now);
    renderReadouts(now);
    renderStats(now);
    renderLevel(now);
    renderAttitude(now);
    renderCharts(now);
    renderStatus(now);
    renderCsvCount();
}

function frame() {
    const now = Date.now();
    if (now - lastRenderAt >= (dirty ? RENDER_MS : IDLE_RENDER_MS)) {
        dirty = false;
        lastRenderAt = now;
        render(now);
    }
    scheduleFrame();
}

function scheduleFrame() {
    // requestAnimationFrame pauses while the tab is hidden, so a background
    // dashboard costs nothing to draw. Data is still recorded meanwhile.
    if (window.requestAnimationFrame) window.requestAnimationFrame(frame);
    else setTimeout(frame, RENDER_MS);
}

// ---- buttons ----

function csvCell(value) {
    return isNumber(value) ? String(value) : "";
}

function csvFromRows(rows) {
    // target columns go last so scripts written for the older file still work
    const lines = ["time_iso,elapsed_s,run,pitch_deg,roll_deg,power_w,pitch_target_deg,roll_target_deg"];
    const start = rows.length ? rows[0][0] : 0;
    for (const row of rows) {
        lines.push([
            new Date(row[0]).toISOString(),
            ((row[0] - start) / 1000).toFixed(3),
            row[1],
            csvCell(row[2]),
            csvCell(row[3]),
            csvCell(row[4]),
            csvCell(row[5]),
            csvCell(row[6]),
        ].join(","));
    }
    return lines.join("\n") + "\n";
}

function exportCsv() {
    // local time in the filename, UTC (ISO) inside the file
    const d = new Date();
    const two = (n) => String(n).padStart(2, "0");
    const stamp = d.getFullYear() + "-" + two(d.getMonth() + 1) + "-" + two(d.getDate()) + "_" +
        two(d.getHours()) + "-" + two(d.getMinutes()) + "-" + two(d.getSeconds());
    const blob = new Blob([csvFromRows(csvRows)], { type: "text/csv" });
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = "solaryou_" + stamp + ".csv";
    document.body.appendChild(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 10000);
}

function newRun() {
    runNumber++;
    energyWh = 0;
    lastPowerForEnergy = null;
    tiltSamples.length = 0;
    trail.length = 0;
    el("runNumber").innerText = runNumber;
    el("energySub").innerText = "since run " + runNumber + " started";
    dirty = true;
    lastRenderAt = 0;
}

// Dark (default) or light, remembered in this browser only. If storage is
// blocked, the switch still works but the choice is forgotten on reload.
function saveLook() {
    const root = document.documentElement;
    try {
        localStorage.setItem("solaryou-look", JSON.stringify({
            theme: root.getAttribute("data-theme"),
            palette: root.getAttribute("data-palette"),
        }));
    } catch (e) { /* private window or storage off */ }
}

function toggleTheme() {
    const root = document.documentElement;
    root.setAttribute("data-theme", root.getAttribute("data-theme") === "light" ? "dark" : "light");
    saveLook();
    applyColors();
    dirty = true;
    lastRenderAt = 0;
}

el("themeToggle").addEventListener("click", toggleTheme);
el("exportCsv").addEventListener("click", exportCsv);
el("newRun").addEventListener("click", newRun);
el("bandLabel").innerText = BAND_DEG;
el("bandRing").setAttribute("r", (BAND_DEG * LEVEL_SCALE).toFixed(1));

// ---- connection to the dashboard server ----

const socket = io();
socket.on("sensor_update", handleReading);
socket.on("connection_status", (status) => {
    serverStatus = status;
    dirty = true;
});
// The dashboard server itself went away (stopped, laptop asleep, phone out
// of WiFi range). The page keeps retrying on its own; until it succeeds,
// don't leave a green badge sitting over frozen numbers.
socket.on("disconnect", () => {
    serverStatus = {
        status: "reconnecting",
        message: "Lost connection to the dashboard server. This page keeps retrying.",
    };
    dirty = true;
});

scheduleFrame();

// For checking the page from the browser console or a headless browser test.
window.__dashboard = {
    handleReading, render, newRun, csvFromRows, computeStats, formatEnergy,
    state: () => ({
        latest, chartPoints, tiltSamples, trail, powerSamples,
        powerPoints, energyWh, runNumber, csvRows,
    }),
    charts: { orientationChart, powerChart },
    toggleTheme, applyColors,
};
