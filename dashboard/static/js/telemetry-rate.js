// Optional sending-rate control. Zack's dashboard.js remains unchanged.
// Reuse its existing Socket.IO connection without changing readings, charts,
// target calculations, statistics, or CSV export.
(function () {
    "use strict";

    const selector = document.getElementById("telemetryRate");
    const status = document.getElementById("telemetryRateStatus");
    let appliedRate = null;
    let feedbackStarted = null;
    let feedbackRate = null;
    let feedbackToken = 0;
    let feedbackTimer = null;
    const MIN_FEEDBACK_MS = 450;

    function resetFeedback() {
        ++feedbackToken;
        if (feedbackTimer !== null) clearTimeout(feedbackTimer);
        feedbackTimer = null;
        feedbackStarted = null;
        feedbackRate = null;
    }

    function showProgress(state, requestedRate) {
        selector.disabled = true;
        status.dataset.state = state;
        status.setAttribute("aria-busy", "true");
        status.innerText = state === "verifying"
            ? "Verifying " + requestedRate + " updates/s with ESP32..."
            : "Applying " + requestedRate + " updates/s...";
        status.title = "Waiting for the applied setting to be read back";
    }

    selector.addEventListener("change", () => {
        const rate = Number(selector.value);
        if (![1, 2, 5, 10].includes(rate)) return;
        resetFeedback();
        feedbackStarted = performance.now();
        feedbackRate = rate;
        // Show feedback immediately, before a server round trip.
        showProgress("applying", rate);
        socket.emit("set_telemetry_rate", { rate_hz: rate });
    });

    socket.on("telemetry_rate", (state) => {
        if (state.state === "confirmed" && [1, 2, 5, 10].includes(state.rate_hz)) {
            appliedRate = state.rate_hz;
            const token = feedbackToken;
            const finish = () => {
                if (token !== feedbackToken) return;
                resetFeedback();
                selector.disabled = !state.available;
                selector.value = String(state.rate_hz);
                status.dataset.state = "confirmed";
                status.setAttribute("aria-busy", "false");
                const checkedAt = new Date((state.confirmed_at || Date.now() / 1000) * 1000).toLocaleTimeString();
                status.innerText = "ESP32 rate: " + state.rate_hz + " updates/s";
                status.title = "Last read back from the ESP32 at " + checkedAt;
            };
            const remaining = feedbackStarted === null ? 0
                : Math.max(0, MIN_FEEDBACK_MS - (performance.now() - feedbackStarted));
            if (feedbackTimer !== null) clearTimeout(feedbackTimer);
            if (remaining > 0) feedbackTimer = setTimeout(finish, remaining);
            else finish();
        } else if (state.state === "checking") {
            resetFeedback();
            appliedRate = null;
            selector.disabled = true;
            status.dataset.state = "checking";
            status.setAttribute("aria-busy", "true");
            status.innerText = "Checking ESP32 rate (" + state.attempt
                + "/" + state.max_attempts + ")...";
            status.title = "Reading the applied setting; live readings continue";
        } else if (state.state === "pending" || state.state === "verifying") {
            // Another browser may start a request while our previous success
            // is waiting to display. Its old timer must not hide this request.
            if (feedbackTimer !== null || feedbackRate !== state.requested_hz) resetFeedback();
            if (feedbackStarted === null) feedbackStarted = performance.now();
            feedbackRate = state.requested_hz;
            showProgress(state.state === "pending" ? "applying" : "verifying", state.requested_hz);
        } else if (state.state === "error") {
            resetFeedback();
            selector.disabled = !state.available;
            status.dataset.state = "error";
            status.setAttribute("aria-busy", "false");
            status.innerText = state.error || "Rate change failed";
            status.title = "";
            selector.value = appliedRate === null ? "" : String(appliedRate);
        } else {
            resetFeedback();
            selector.disabled = !state.available;
            status.dataset.state = state.state;
            status.setAttribute("aria-busy", "false");
            appliedRate = null;
            status.innerText = state.state === "unsupported"
                ? "Firmware has no rate control" : "Connect BLE to change rate";
            status.title = "";
        }
    });

    socket.on("disconnect", () => {
        resetFeedback();
        selector.disabled = true;
        status.dataset.state = "disconnected";
        status.setAttribute("aria-busy", "false");
        status.innerText = "Dashboard disconnected; reconnecting...";
        status.title = "";
        appliedRate = null;
    });
})();
