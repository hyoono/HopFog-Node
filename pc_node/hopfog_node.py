#!/usr/bin/env python3
"""
HopFog Node – Windows PC Proof of Concept

Runs the same headless REST API and XBee protocol as the embedded
firmware, but on a desktop PC with an XBee plugged in via USB.

Usage:
    1. Copy config.example.json → config.json and edit it.
    2. pip install -r requirements.txt
    3. python hopfog_node.py
       or:  python hopfog_node.py --port COM5 --http-port 8080

The PC should be set up as a Wi-Fi hotspot so that nearby clients
can reach the REST API at http://<hotspot-ip>:8080/api/...
"""

import argparse
import json
import os
import sys
import threading
import time

from typing import Optional

import serial
from flask import Flask, jsonify, request

# ── Defaults ─────────────────────────────────────────────────────
DEFAULTS = {
    "node_id": "pc-node-01",
    "xbee_port": "COM3",
    "xbee_baud": 9600,
    "http_host": "0.0.0.0",
    "http_port": 8080,
    "heartbeat_interval": 30,
    "sync_interval": 60,
    "data_dir": "hopfog_data",
}

# ── Configuration ────────────────────────────────────────────────
config: dict = {}


def load_config(path: str = "config.json") -> dict:
    """Load JSON config, falling back to defaults for missing keys."""
    cfg = dict(DEFAULTS)
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            cfg.update(json.load(f))
        print(f"[CONFIG] Loaded {path}")
    else:
        print(f"[CONFIG] {path} not found – using defaults")
    return cfg


# ── Storage (local JSON files) ───────────────────────────────────
FOG_NODES_FILE = "fog_nodes.json"
MESSAGES_FILE = "messages.json"
STATS_FILE = "stats.json"


def _data_path(filename: str) -> str:
    return os.path.join(config["data_dir"], filename)


def _read_json(filename: str, default=None):
    path = _data_path(filename)
    if not os.path.isfile(path):
        return default if default is not None else []
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _write_json(filename: str, data) -> bool:
    path = _data_path(filename)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    return True


def init_storage():
    os.makedirs(config["data_dir"], exist_ok=True)
    if not os.path.isfile(_data_path(FOG_NODES_FILE)):
        _write_json(FOG_NODES_FILE, [])
    if not os.path.isfile(_data_path(MESSAGES_FILE)):
        _write_json(MESSAGES_FILE, [])
    if not os.path.isfile(_data_path(STATS_FILE)):
        _write_json(STATS_FILE, {})
    print(f"[STORAGE] Data directory: {os.path.abspath(config['data_dir'])}")


# ── Statistics ───────────────────────────────────────────────────
stats = {"fog_nodes_count": 0, "active_fog_nodes": 0, "total_messages": 0}
start_time = time.time()


def update_stats():
    nodes = _read_json(FOG_NODES_FILE, [])
    msgs = _read_json(MESSAGES_FILE, [])
    stats["fog_nodes_count"] = len(nodes)
    stats["active_fog_nodes"] = sum(
        1 for n in nodes if n.get("status") == "active"
    )
    stats["total_messages"] = len(msgs)


def save_stats():
    _write_json(STATS_FILE, stats)


# ── Data access ──────────────────────────────────────────────────
def get_fog_nodes() -> list:
    return _read_json(FOG_NODES_FILE, [])


def add_fog_node(name: str, ip: str, status: str = "active") -> bool:
    nodes = _read_json(FOG_NODES_FILE, [])
    nodes.append(
        {
            "id": len(nodes) + 1,
            "device_name": name,
            "ip_address": ip,
            "status": status,
            "added_at": int(time.time()),
        }
    )
    ok = _write_json(FOG_NODES_FILE, nodes)
    if ok:
        update_stats()
    return ok


def get_messages() -> list:
    return _read_json(MESSAGES_FILE, [])


def add_message(from_user: str, to_user: str, message: str) -> bool:
    msgs = _read_json(MESSAGES_FILE, [])
    msgs.append(
        {
            "id": len(msgs) + 1,
            "from": from_user,
            "to": to_user,
            "message": message,
            "timestamp": int(time.time()),
            "node": config["node_id"],
        }
    )
    ok = _write_json(MESSAGES_FILE, msgs)
    if ok:
        update_stats()
    return ok


# ── XBee (USB serial) ───────────────────────────────────────────
xbee_serial: Optional[serial.Serial] = None
xbee_lock = threading.Lock()


def xbee_open() -> bool:
    global xbee_serial
    port = config["xbee_port"]
    baud = config["xbee_baud"]
    try:
        xbee_serial = serial.Serial(port, baud, timeout=0.1)
        print(f"[XBEE] Opened {port} at {baud} baud")
        return True
    except serial.SerialException as exc:
        print(f"[XBEE] Could not open {port}: {exc}")
        print("[XBEE] Running without XBee – REST API still works")
        xbee_serial = None
        return False


def xbee_send(data: dict):
    if xbee_serial is None:
        return
    line = json.dumps(data, separators=(",", ":")) + "\n"
    with xbee_lock:
        try:
            xbee_serial.write(line.encode("utf-8"))
            print(f"[XBEE-TX] {line.rstrip()}")
        except serial.SerialException as exc:
            print(f"[XBEE-TX] Error: {exc}")


def xbee_send_command(cmd: str, params: Optional[dict] = None):
    frame: dict = {
        "cmd": cmd,
        "node_id": config["node_id"],
        "ts": int(time.time()),
    }
    if params:
        frame["params"] = params
    xbee_send(frame)


def xbee_register():
    xbee_send_command(
        "REGISTER",
        {
            "ip_address": f"{config['http_host']}:{config['http_port']}",
            "device_name": config["node_id"],
            "status": "active",
            "storage_available": True,
        },
    )


def xbee_heartbeat():
    xbee_send_command(
        "HEARTBEAT",
        {
            "ip_address": f"{config['http_host']}:{config['http_port']}",
            "uptime": int(time.time() - start_time),
            "fog_nodes": stats["fog_nodes_count"],
            "messages": stats["total_messages"],
            "storage_available": True,
        },
    )


def xbee_request_sync():
    xbee_send_command("SYNC_REQUEST")


def xbee_relay_message(from_user: str, to_user: str, message: str):
    xbee_send_command(
        "RELAY_MSG", {"from": from_user, "to": to_user, "message": message}
    )


def xbee_relay_fog_node(name: str, ip: str, status: str):
    xbee_send_command(
        "RELAY_FOG_NODE",
        {"device_name": name, "ip_address": ip, "status": status},
    )


def handle_xbee_line(line: str):
    """Process a single newline-delimited JSON frame from admin."""
    line = line.strip()
    if not line:
        return
    print(f"[XBEE-RX] {line}")

    try:
        doc = json.loads(line)
    except json.JSONDecodeError as exc:
        print(f"[XBEE] JSON parse error: {exc}")
        return

    cmd = doc.get("cmd")
    if not cmd:
        return

    if cmd == "PONG":
        print("[XBEE] Admin responded to heartbeat")
    elif cmd == "REGISTER_ACK":
        print("[XBEE] Registered with admin successfully")
    elif cmd == "SYNC_DATA":
        if "fog_nodes" in doc:
            _write_json(FOG_NODES_FILE, doc["fog_nodes"])
        if "messages" in doc:
            _write_json(MESSAGES_FILE, doc["messages"])
        update_stats()
        save_stats()
        print("[XBEE] Data sync complete")
    elif cmd == "BROADCAST_MSG":
        p = doc.get("params", {})
        if p.get("from") and p.get("to") and p.get("message"):
            add_message(p["from"], p["to"], p["message"])
            print("[XBEE] Broadcast message stored locally")
    elif cmd == "ADD_FOG_NODE":
        p = doc.get("params", {})
        if p.get("device_name") and p.get("ip_address"):
            add_fog_node(
                p["device_name"], p["ip_address"], p.get("status", "active")
            )
            print("[XBEE] Fog node added from admin")
    elif cmd == "GET_STATS":
        resp = {
            "cmd": "STATS_RESPONSE",
            "node_id": config["node_id"],
            "fog_nodes_count": stats["fog_nodes_count"],
            "active_fog_nodes": stats["active_fog_nodes"],
            "total_messages": stats["total_messages"],
            "uptime": int(time.time() - start_time),
            "ip_address": f"{config['http_host']}:{config['http_port']}",
            "storage_available": True,
        }
        xbee_send(resp)
    else:
        print(f"[XBEE] Unknown command: {cmd}")


def xbee_reader_thread():
    """Background thread that reads lines from the XBee serial port."""
    buf = ""
    while True:
        if xbee_serial is None:
            time.sleep(1)
            continue
        try:
            with xbee_lock:
                raw = xbee_serial.read(256)
            if raw:
                buf += raw.decode("utf-8", errors="replace")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    handle_xbee_line(line)
            else:
                time.sleep(0.05)
        except serial.SerialException:
            time.sleep(1)


def xbee_timer_thread():
    """Background thread for heartbeat and sync timers."""
    last_hb = time.time()
    last_sync = time.time()
    while True:
        now = time.time()
        if now - last_hb >= config["heartbeat_interval"]:
            last_hb = now
            xbee_heartbeat()
        if now - last_sync >= config["sync_interval"]:
            last_sync = now
            xbee_request_sync()
        time.sleep(1)


# ── Flask REST API ───────────────────────────────────────────────
app = Flask(__name__)


@app.errorhandler(404)
def not_found(_e):
    return jsonify({"error": "Not Found"}), 404


@app.route("/api/health", methods=["GET"])
def api_health():
    return jsonify(
        {
            "status": "ok",
            "node_id": config["node_id"],
            "uptime": int(time.time() - start_time),
        }
    )


@app.route("/api/stats", methods=["GET"])
def api_stats():
    update_stats()
    return jsonify(
        {
            "node_id": config["node_id"],
            "fog_nodes_count": stats["fog_nodes_count"],
            "active_fog_nodes": stats["active_fog_nodes"],
            "total_messages": stats["total_messages"],
            "ip_address": f"{config['http_host']}:{config['http_port']}",
            "uptime": int(time.time() - start_time),
            "storage_available": True,
            "storage_type": "local_files",
            "data_dir": os.path.abspath(config["data_dir"]),
        }
    )


@app.route("/api/fog-devices", methods=["GET"])
def api_get_fog_nodes():
    return jsonify(get_fog_nodes())


@app.route("/api/fog-devices/register", methods=["POST"])
def api_add_fog_node():
    name = request.form.get("device_name", "")
    ip = request.form.get("ip_address", "")
    status = request.form.get("status", "active")
    if not name or not ip:
        return (
            jsonify(
                {"success": False, "message": "Missing device_name or ip_address"}
            ),
            400,
        )
    if add_fog_node(name, ip, status):
        xbee_relay_fog_node(name, ip, status)
        return jsonify(
            {"success": True, "message": "Fog node added and relayed to admin"}
        )
    return (
        jsonify({"success": False, "message": "Failed to add fog node"}),
        500,
    )


@app.route("/api/messages", methods=["GET"])
def api_get_messages():
    return jsonify(get_messages())


@app.route("/api/messages", methods=["POST"])
def api_add_message():
    from_user = request.form.get("from", "")
    to_user = request.form.get("to", "")
    message = request.form.get("message", "")
    if not from_user or not to_user or not message:
        return (
            jsonify({"success": False, "message": "Missing from, to, or message"}),
            400,
        )
    if add_message(from_user, to_user, message):
        xbee_relay_message(from_user, to_user, message)
        return jsonify(
            {"success": True, "message": "Message stored and relayed to admin"}
        )
    return (
        jsonify({"success": False, "message": "Failed to store message"}),
        500,
    )


@app.route("/api/xbee/broadcast", methods=["POST"])
def api_relay():
    body = request.get_data(as_text=True)
    if not body:
        return jsonify({"success": False, "message": "Empty body"}), 400
    try:
        data = json.loads(body)
    except json.JSONDecodeError:
        return jsonify({"success": False, "message": "Invalid JSON"}), 400
    xbee_send(data)
    return jsonify({"success": True, "message": "Relayed to admin via XBee"})


# ── Main ─────────────────────────────────────────────────────────
def main():
    global config

    parser = argparse.ArgumentParser(
        description="HopFog Node – Windows PC proof of concept"
    )
    parser.add_argument(
        "--config", default="config.json", help="Path to config JSON file"
    )
    parser.add_argument("--port", help="XBee serial port (e.g. COM3, /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, help="XBee baud rate")
    parser.add_argument("--http-port", type=int, help="HTTP server port")
    parser.add_argument("--node-id", help="Node identifier")
    args = parser.parse_args()

    config = load_config(args.config)
    if args.port:
        config["xbee_port"] = args.port
    if args.baud:
        config["xbee_baud"] = args.baud
    if args.http_port:
        config["http_port"] = args.http_port
    if args.node_id:
        config["node_id"] = args.node_id

    print("\nHopFog Node – PC Proof of Concept")
    print("==================================")
    print(f"[NODE] ID: {config['node_id']}")

    # Storage
    init_storage()
    update_stats()

    # XBee
    xbee_open()

    # Start XBee background threads
    reader = threading.Thread(target=xbee_reader_thread, daemon=True)
    timer = threading.Thread(target=xbee_timer_thread, daemon=True)
    reader.start()
    timer.start()

    # Register with admin
    xbee_register()

    # HTTP server
    print(
        f"[HTTP] Starting on http://{config['http_host']}:{config['http_port']}"
    )
    print("==================================\n")

    app.run(
        host=config["http_host"],
        port=config["http_port"],
        debug=False,
        use_reloader=False,
    )


if __name__ == "__main__":
    main()
