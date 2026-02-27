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
    _test(3, "GET /api/fognodes")
    try:
        r = requests.get(f"{BASE_URL}/api/fognodes", timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_add_fognode():
    _test(4, "POST /api/fognodes/add")
    data = {
        "device_name": "TestNode-Python",
        "ip_address": "192.168.1.50",
        "status": "active",
    }
    print(f"Payload: {data}")
    try:
        r = requests.post(f"{BASE_URL}/api/fognodes/add", data=data, timeout=5)
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
    _test(6, "POST /api/messages/add")
    data = {
        "from": "user1",
        "to": "user2",
        "message": "Hello from Python test script via node",
    }
    print(f"Payload: {data}")
    try:
        r = requests.post(f"{BASE_URL}/api/messages/add", data=data, timeout=5)
        print(f"Status: {r.status_code}")
        _pprint(r.json())
        return r.status_code == 200
    except Exception as e:
        print(f"Error: {e}")
        return False


def test_relay():
    _test(7, "POST /api/relay")
    payload = json.dumps({"cmd": "PING", "node_id": "test"})
    print(f"Payload: {payload}")
    try:
        r = requests.post(
            f"{BASE_URL}/api/relay",
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
    total = 7

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
