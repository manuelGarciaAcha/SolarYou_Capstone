# Design Decisions

Reference for major architecture and process decisions

## RTOS task development approach 
**Date:** [08-15-26]
**Decision:** Build core functionality as standalone, self-contained C functions first; wrap into FreeRTOS tasks afterward. This lets the team develop in parallel without waiting on the full task/scheduling structure to be finalized. 
**Pons's input:** "Develop functions to establish functionality then migrate to RTOS, main concern is that data is shared in multi-task environment so it needs to be protected with mutex or semaphores to avoid partial changes to data."

---

## Communication protocol: WiFi 
**Date:** [08-20-26] 
**Decision:** WiFi selected over Bluetooth Low Energy and LoRa. **Reasoning:** LoRa's bandwidth doesn't support a live dashboard, which both the recreational and commercial user profiles require (Intended Users section). WiFi draws more power than BLE but fits the dashboard architecture already in progress, and is already onboard the ESP32-S3 with no new hardware needed. 
**Pons's input:** "I would go with WiFi with the intention to use more energy efficient communications in the next version." 
**Future consideration:** more energy-efficient comms in a future version.

---

## Dashboard architecture: browser-based, not a desktop app 
**Date:** [08-25-26] 
**Decision:** Dashboard is a Flask + Socket.IO web app, not a Python/matplotlib desktop application. 
**Reasoning:** Needs to run on whichever team laptop is available at demo time, and be viewable from a phone browser as well, since it isn't guaranteed to be Zack's own device running the demo. A desktop app can't run on a phone at all; a browser-based dashboard works on any device with a browser, Mac or Windows, laptop or phone. **Architecture:** ESP32 sends JSON over WiFi to a Flask server running on a team laptop; any browser on the same network (laptop or phone) connects to that server to view the dashboard.

--- 

## Debug view before demo view 
**Date:** [08-27-26] 
**Decision:** Build a minimal "debug view" (raw live sensor/motor values, no history or styling) before the polished "demo view" (historical logging, baseline comparison vs. fixed panel). 
**Reasoning:** The team needs live visibility into sensor/motor output during active hardware tuning, not just for the final demo. Treating the dashboard as low-priority-until-later would leave the team debugging off raw serial output in the meantime.

--- 

## Waterproofing / marine certification: explicitly out of scope this semester 
**Date:** [07-02-26] 
**Decision:** The prototype is not waterproof or marine-certified. Testing is indoor/lab-based per the Limitations section. Water-resistant components and a CAD-designed enclosure are being pursued as a hedge toward future deployment, not a current certification claim. 

---

## Cloud hosting / MQTT: deferred as future scope 
**Date:** [08-27-26] 
**Decision:** No cloud backend or MQTT broker for this semester. The dashboard connects directly to the ESP32 over the local network only. **Reasoning:** Nothing in the current requirements needs remote access from outside the demo network. A cloud layer adds setup time and new failure points without a corresponding requirement to justify it this semester.
