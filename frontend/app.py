"""
Sensor Gateway — Flask Frontend Proxy
======================================
Serves the dashboard HTML and proxies API requests to the C backend (port 8080).

Why Flask instead of serving directly from libmicrohttpd?
  - Serving static files in C is verbose (read file, set MIME type manually).
  - Flask + Jinja2 simplifies HTML templating.
  - Separates frontend deployment from the C process lifecycle.

Run:  python3 app.py
Open: http://<BBB_IP>:5000

Author: Nguyen Si Phu
"""

from flask import Flask, render_template, jsonify, request
import requests

app = Flask(__name__)

# Explicit IPv4 to avoid IPv6 mismatch — C server binds AF_INET only.
GATEWAY_API = "http://127.0.0.1:8080"
API_TIMEOUT = 5  # seconds


@app.route("/")
def index():
    """Serve the dashboard page."""
    return render_template("index.html")


@app.route("/api/sensors")
def get_sensors():
    """Proxy: fetch sensor list from C API."""
    try:
        resp = requests.get(f"{GATEWAY_API}/api/sensors", timeout=API_TIMEOUT)
        return jsonify(resp.json()), resp.status_code
    except requests.ConnectionError:
        return jsonify({"error": "Gateway not reachable"}), 502
    except requests.Timeout:
        return jsonify({"error": "Gateway timeout"}), 504
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/sensors/<sensor_id>/data")
def get_sensor_data(sensor_id):
    """Proxy: fetch readings for a sensor from C API."""
    try:
        params = request.args.to_dict()
        resp = requests.get(
            f"{GATEWAY_API}/api/sensors/{sensor_id}/data",
            params=params,
            timeout=API_TIMEOUT,
        )
        return jsonify(resp.json()), resp.status_code
    except requests.ConnectionError:
        return jsonify({"error": "Gateway not reachable"}), 502
    except requests.Timeout:
        return jsonify({"error": "Gateway timeout"}), 504
    except Exception as e:
        return jsonify({"error": str(e)}), 500


if __name__ == "__main__":
    print("=" * 50)
    print("  Sensor Gateway Dashboard")
    print("  Open: http://0.0.0.0:5000")
    print(f"  API backend: {GATEWAY_API}")
    print("=" * 50)
    # host="0.0.0.0": listen on all interfaces so PC can reach BBB.
    app.run(host="0.0.0.0", port=5000, debug=True)
