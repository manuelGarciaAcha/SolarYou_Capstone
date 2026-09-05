from flask import Flask, render_template_string
from flask_socketio import SocketIO
import threading, time, random

app = Flask(__name__)
socketio = SocketIO(app)

def mock_sensor_loop():
    while True:
        data = {"pitch": round(random.uniform(-10, 10), 2),
                "roll": round(random.uniform(-10, 10), 2),
                "power": round(random.uniform(0.5, 2.0), 2)}
        socketio.emit("sensor_update", data)
        time.sleep(1)

@app.route("/")
def index():
    return render_template_string("""
        <h2>Debug View</h2>
        <p id="data">Waiting for data...</p>
        <script src="https://cdn.socket.io/4.7.2/socket.io.min.js"></script>
        <script>
            const socket = io();
            socket.on("sensor_update", (d) => {
                document.getElementById("data").innerText = JSON.stringify(d);
            });
        </script>
    """)

threading.Thread(target=mock_sensor_loop, daemon=True).start()
socketio.run(app, debug=True, host="0.0.0.0", port=5001)