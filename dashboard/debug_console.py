from flask import Flask, render_template
from flask_socketio import SocketIO
import threading

app = Flask(__name__)
socketio = SocketIO(app)

def esp32_listener():
    # Real version: websocket-client connects to the ESP32
    # Mock version for now: generate fake JSON in a loop
    while True:
        data = {"pitch": 5.73, "roll": -2.11, "power": 1.23}  # replace with real/mock source
        socketio.emit("sensor_update", data)  # pushes to every connected browser
        # time.sleep(...) goes here

@app.route("/")
def index():
    return render_template("index.html")  # your Chart.js page

threading.Thread(target=esp32_listener, daemon=True).start()
socketio.run(app)