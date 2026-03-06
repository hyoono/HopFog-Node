#!/usr/bin/env python3
"""
HopFog Node API Test Script

Tests the REST API endpoints exposed by the HopFog Node firmware.
Run this from a machine on the same WiFi network as the node.

Usage:
    python test_api.py [<node_ip>]

If no IP is given the default 192.168.1.200 is used.
"""

import json
import sys

import requests

ESP32_IP = "192.168.1.200"
BASE_URL = f"http://{ESP32_IP}"


def _header(text):
    print("\n" + "=" * 60)
    print(text)
    print("=" * 60)


def _test(num, description):
    print(f"\n{num}. {description}")
    print("-" * 60)


def _pprint(data):
    if isinstance(data, str):
        try:
            data = json.loads(data)
        except json.JSONDecodeError:
            print(data)
            return
    print(json.dumps(data, indent=2))


def test_health():
    _test(1, "GET /api/health")
    try:
        r = requests.get(f"{BASE_URL}/api/health", timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_get_stats():
    _test(2, "GET /api/stats")
    try:
        r = requests.get(f"{BASE_URL}/api/stats", timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_get_fognodes():
    _test(3, "GET /api/fog-devices")
    try:
        r = requests.get(f"{BASE_URL}/api/fog-devices", timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_add_fognode():
    _test(4, "POST /api/fog-devices/register")
    data = {
        "device_name": "TestNode-Python",
        "ip_address": "192.168.1.50",
        "status": "active",
    }
    print(f"Payload: {data}")
    try:
        r = requests.post(f"{BASE_URL}/api/fog-devices/register", data=data, timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_get_messages():
    _test(5, "GET /api/messages")
    try:
        r = requests.get(f"{BASE_URL}/api/messages", timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_add_message():
    _test(6, "POST /api/messages")
    data = {
        "from": "user1",
        "to": "user2",
        "message": "Hello from Python test script via node",
    }
    print(f"Payload: {data}")
    try:
        r = requests.post(f"{BASE_URL}/api/messages", data=data, timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_relay():
    _test(7, "POST /api/xbee/broadcast")
    payload = json.dumps({"cmd": "PING", "node_id": "test"})
    print(f"Payload: {payload}")
    try:
        r = requests.post(
            f"{BASE_URL}/api/xbee/broadcast",
            data=payload,
            headers={"Content-Type": "application/json"},
            timeout=5,
        )
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


# ── Mobile app endpoint tests ────────────────────────────────────

def test_mobile_status():
    _test(8, "GET /status")
    try:
        r = requests.get(f"{BASE_URL}/status", timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200 and r.json().get("online") is True
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_mobile_login():
    _test(9, "POST /login")
    payload = {"username": "testuser", "password": "pass"}
    print(f"Payload: {payload}")
    try:
        r = requests.post(
            f"{BASE_URL}/login",
            json=payload,
            timeout=5,
        )
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        # 401 is expected when no users are loaded
        return r.status_code in (200, 401)
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_mobile_conversations():
    _test(10, "GET /conversations")
    try:
        r = requests.get(f"{BASE_URL}/conversations", params={"user_id": 1}, timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_mobile_create_chat():
    _test(11, "POST /create-chat")
    payload = {"user1_id": 1, "user2_id": 2}
    print(f"Payload: {payload}")
    try:
        r = requests.post(f"{BASE_URL}/create-chat", json=payload, timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200 and "conversation_id" in r.json()
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_mobile_send():
    _test(12, "POST /send")
    payload = {"conversation_id": 1, "sender_id": 1, "message_text": "Test msg"}
    print(f"Payload: {payload}")
    try:
        r = requests.post(f"{BASE_URL}/send", json=payload, timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200 and r.json().get("success") is True
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_mobile_messages():
    _test(13, "GET /messages")
    try:
        r = requests.get(
            f"{BASE_URL}/messages",
            params={"conversation_id": 1, "user_id": 1},
            timeout=5,
        )
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200 and isinstance(r.json(), list)
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_mobile_users():
    _test(14, "GET /users")
    try:
        r = requests.get(f"{BASE_URL}/users", params={"user_id": 1}, timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_mobile_announcements():
    _test(15, "GET /announcements")
    try:
        r = requests.get(f"{BASE_URL}/announcements", timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def main():
    _header("HopFog Node – API Test Suite")
    print(f"Target: {BASE_URL}")

    # Connectivity check
    print("\nChecking connectivity ...")
    try:
        requests.get(f"{BASE_URL}/api/health", timeout=5)
        print("✓ Node is reachable")
    except Exception as e:
        print(f"✗ Cannot reach node: {e}")
        print("  1. Is the node powered on and connected to WiFi?")
        print("  2. Is this machine on the same network?")
        print(f"  3. Is the IP correct? (currently {ESP32_IP})")
        sys.exit(1)

    passed = 0
    total = 15

    if test_health():
        passed += 1
    if test_get_stats():
        passed += 1
    if test_get_fognodes():
        passed += 1
    if test_add_fognode():
        passed += 1
    if test_get_messages():
        passed += 1
    if test_add_message():
        passed += 1
    if test_relay():
        passed += 1
    if test_mobile_status():
        passed += 1
    if test_mobile_login():
        passed += 1
    if test_mobile_conversations():
        passed += 1
    if test_mobile_create_chat():
        passed += 1
    if test_mobile_send():
        passed += 1
    if test_mobile_messages():
        passed += 1
    if test_mobile_users():
        passed += 1
    if test_mobile_announcements():
        passed += 1

    _header("Results")
    print(f"Passed: {passed}/{total}")
    if passed == total:
        print("✓ All tests passed!")
    else:
        print(f"⚠ {total - passed} test(s) had issues")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        ESP32_IP = sys.argv[1]
        BASE_URL = f"http://{ESP32_IP}"
    main()
