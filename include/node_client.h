#ifndef NODE_CLIENT_H
#define NODE_CLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ── Node state ─────────────────────────────────────────────────────
enum NodeState {
    STATE_UNREGISTERED,   // Sending REGISTER every 10s
    STATE_REGISTERED,     // Got REGISTER_ACK, now syncing
    STATE_SYNCING,        // Sent SYNC_REQUEST, waiting for SYNC_DATA
    STATE_RUNNING         // Fully synced, sending HEARTBEAT every 30s
};

/// Initialize the node client (call once in setup, after xbeeInit).
void nodeClientInit();

/// Call from loop() — handles timers for REGISTER/HEARTBEAT/SYNC.
void nodeClientLoop();

/// Handle an incoming JSON command from admin (call from XBee RX callback).
/// Returns true if it was a recognized admin command.
bool nodeClientHandleCommand(const char* payload, size_t len);

/// Get current state (for diagnostic display).
NodeState nodeClientGetState();

#endif // NODE_CLIENT_H
