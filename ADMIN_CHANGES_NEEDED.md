# HopFog-Web Admin – Required Changes for Node Integration

> **Target repo:** [hyoono/HopFog-Web](https://github.com/hyoono/HopFog-Web)
>
> **Context:** HopFog-Node sends and expects specific XBee JSON commands.
> The admin currently only broadcasts raw text and buffers received text —
> it does **not** parse or respond to any of the node protocol commands.
>
> This document lists every change needed in HopFog-Web so that nodes
> can register, heartbeat, sync data, and relay mobile-app traffic.

---

## Table of Contents

1. [Current State (what the admin has today)](#1-current-state)
2. [Gap Analysis (what's missing)](#2-gap-analysis)
3. [Required Changes](#3-required-changes)
   - [3.1 Upgrade xbee_service.py — command router](#31-upgrade-xbee_servicepy)
   - [3.2 New file: services/node_protocol.py — command handlers](#32-new-file-node_protocolpy)
   - [3.3 New file: services/node_registry.py — node tracking](#33-new-file-node_registrypy)
   - [3.4 Update broadcast_dispatcher.py — XBee JSON framing](#34-update-broadcast_dispatcherpy)
   - [3.5 New API route: routes/node_api.py — admin dashboard for nodes](#35-new-route-node_apipy)
   - [3.6 Database migration — add node tracking table](#36-database-migration)
4. [XBee Protocol Reference](#4-xbee-protocol-reference)
5. [Daisy Chaining Analysis](#5-daisy-chaining-analysis)
6. [Implementation Priority](#6-implementation-priority)

---

## 1. Current State

### `services/xbee_service.py`
- Uses `digi.xbee.devices.XBeeDevice` for serial communication
- `send_broadcast(text)` — sends raw text to all XBee devices
- `_on_receive()` — stores raw text in an in-memory list (`self._rx`)
- **No JSON parsing of received data**
- **No command routing** — all received frames are treated as opaque strings

### `services/broadcast_dispatcher.py`
- Polls the `broadcast_messages` DB table for queued broadcasts
- Calls `send_broadcast(body_or_subject)` — sends the message body as raw text
- **Does not wrap broadcasts in the JSON protocol nodes expect**

### `routes/xbee_api.py`
- `POST /api/xbee/broadcast` — sends raw text
- `GET /api/xbee/received` — returns the raw RX buffer
- **No node-specific endpoints**

---

## 2. Gap Analysis

### Commands nodes SEND that admin does NOT handle

| Node → Admin Command | What the node expects | Admin handles? |
|---|---|---|
| `REGISTER` | Admin should store the node and reply `REGISTER_ACK` | ❌ No |
| `HEARTBEAT` | Admin should update node status and reply `PONG` | ❌ No |
| `SYNC_REQUEST` | Admin should reply with `SYNC_DATA` (users, conversations, messages, announcements) | ❌ No |
| `RELAY_MSG` | Admin should store the relayed message in its database | ❌ No |
| `RELAY_FOG_NODE` | Admin should register the fog device | ❌ No |
| `RELAY_CHAT_MSG` | Admin should store the chat message in its database | ❌ No |
| `SOS_ALERT` | Admin should create a `ResidentAdminMessage` (SOS) | ❌ No |
| `CHANGE_PASSWORD` | Admin should update the user's password hash | ❌ No |
| `STATS_RESPONSE` | Admin should display/store the node's stats | ❌ No |

### Commands admin should SEND that it does NOT

| Admin → Node Command | When to send | Admin sends? |
|---|---|---|
| `REGISTER_ACK` | In response to `REGISTER` | ❌ No |
| `PONG` | In response to `HEARTBEAT` | ❌ No |
| `SYNC_DATA` | In response to `SYNC_REQUEST` — includes users, conversations, chat_messages, announcements | ❌ No |
| `BROADCAST_MSG` | When admin creates a broadcast message | ❌ No (sends raw text instead) |
| `ADD_FOG_NODE` | When a new fog device is registered on admin | ❌ No |
| `GET_STATS` | When admin wants to query a node's stats | ❌ No |

---

## 3. Required Changes

### 3.1 Upgrade `xbee_service.py`

**What to change:** Add JSON command parsing to `_on_receive()` and a callback mechanism.

```python
# services/xbee_service.py  —  changes needed

import json

class XBeeService:
    def __init__(self):
        # ... existing init ...
        self._command_handler = None  # callback for parsed commands

    def set_command_handler(self, handler):
        """Register a callback: handler(command_dict, from_64bit_addr)"""
        self._command_handler = handler

    def _on_receive(self, xbee_message):
        """Called automatically when RF data is received."""
        try:
            text = xbee_message.data.decode(errors="replace")
        except Exception:
            text = "<decode error>"

        from_addr = str(xbee_message.remote_device.get_64bit_addr())

        # Store in raw buffer (existing behavior)
        item = {"text": text, "from_64bit": from_addr, "ts": time.time()}
        self._rx.append(item)
        if len(self._rx) > 200:
            self._rx = self._rx[-200:]
        print("XBee RX:", item)

        # NEW: Parse as JSON command and dispatch
        try:
            doc = json.loads(text)
            if isinstance(doc, dict) and "cmd" in doc:
                if self._command_handler:
                    self._command_handler(doc, from_addr)
        except json.JSONDecodeError:
            pass  # not a JSON command — ignore

    def send_json(self, data: dict):
        """Send a JSON-encoded command via broadcast."""
        text = json.dumps(data, separators=(",", ":"))
        self.send_broadcast(text)
```

### 3.2 New file: `services/node_protocol.py`

**Purpose:** Handle all incoming node commands and send responses.

```python
# services/node_protocol.py

import json
import time
from database.connection import SessionLocal
from database.models import (
    User, FogDevice, ResidentAdminMessage,
    Message, MessageRecipient,
    BroadcastMessage,
)
from services.xbee_service import xbee_service
from services.node_registry import node_registry


def handle_node_command(doc: dict, from_addr: str):
    """Dispatch an incoming XBee JSON command from a node."""
    cmd = doc.get("cmd", "")
    node_id = doc.get("node_id", "unknown")
    params = doc.get("params", {})

    if cmd == "REGISTER":
        _handle_register(node_id, params, from_addr)
    elif cmd == "HEARTBEAT":
        _handle_heartbeat(node_id, params, from_addr)
    elif cmd == "SYNC_REQUEST":
        _handle_sync_request(node_id)
    elif cmd == "RELAY_MSG":
        _handle_relay_msg(node_id, params)
    elif cmd == "RELAY_FOG_NODE":
        _handle_relay_fog_node(params)
    elif cmd == "RELAY_CHAT_MSG":
        _handle_relay_chat_msg(node_id, params)
    elif cmd == "SOS_ALERT":
        _handle_sos_alert(node_id, params)
    elif cmd == "CHANGE_PASSWORD":
        _handle_change_password(params)
    elif cmd == "STATS_RESPONSE":
        _handle_stats_response(node_id, params)
    else:
        print(f"[NODE-PROTO] Unknown command from {node_id}: {cmd}")


def _handle_register(node_id, params, from_addr):
    """Node registered — store it and reply REGISTER_ACK."""
    node_registry.register(node_id, params, from_addr)

    # Register as fog device in DB
    db = SessionLocal()
    try:
        device_name = params.get("device_name", node_id)
        existing = db.query(FogDevice).filter(
            FogDevice.device_name == device_name
        ).first()
        if existing:
            existing.status = "active"
            db.commit()
        else:
            db.add(FogDevice(device_name=device_name, status="active"))
            db.commit()
    finally:
        db.close()

    xbee_service.send_json({"cmd": "REGISTER_ACK", "node_id": node_id})
    print(f"[NODE-PROTO] Registered node {node_id}")


def _handle_heartbeat(node_id, params, from_addr):
    """Node heartbeat — update status, reply PONG."""
    node_registry.heartbeat(node_id, params, from_addr)
    xbee_service.send_json({"cmd": "PONG", "node_id": node_id})


def _handle_sync_request(node_id):
    """Node wants full data — send SYNC_DATA with users, conversations, etc."""
    db = SessionLocal()
    try:
        # Build users list
        users = db.query(User).filter(User.is_active == 1).all()
        users_data = [
            {
                "id": u.id,
                "username": u.username,
                "email": u.email,
                "role": u.role,
                "is_active": bool(u.is_active),
                "has_agreed_sos": getattr(u, "has_agreed_sos", False),
            }
            for u in users
        ]

        # Build announcements from recent broadcast messages
        broadcasts = (
            db.query(BroadcastMessage)
            .filter(BroadcastMessage.status == "sent")
            .order_by(BroadcastMessage.created_at.desc())
            .limit(50)
            .all()
        )
        announcements_data = [
            {
                "id": b.id,
                "title": b.subject,
                "message": b.body,
                "created_at": str(int(b.created_at.timestamp())) if b.created_at else "0",
            }
            for b in broadcasts
        ]

        # TODO: Build conversations and chat_messages from your DB schema
        # The node expects these keys in SYNC_DATA:
        #   conversations: [{id, participants: [int, int], name, last_message, last_timestamp}]
        #   chat_messages: [{id, conversation_id, sender_id, message_text, sent_at}]
        # You may need to add a Conversation model or map from existing Message model.

        sync_payload = {
            "cmd": "SYNC_DATA",
            "node_id": node_id,
            "users": users_data,
            "announcements": announcements_data,
            "conversations": [],       # TODO: populate from DB
            "chat_messages": [],        # TODO: populate from DB
            "fog_nodes": [],            # TODO: populate from FogDevice table
            "messages": [],             # TODO: populate from Message table
        }

        xbee_service.send_json(sync_payload)
        print(f"[NODE-PROTO] Sent SYNC_DATA to {node_id}")
    finally:
        db.close()


def _handle_relay_msg(node_id, params):
    """Store a relayed user message."""
    # params: {from, to, message}
    print(f"[NODE-PROTO] Relayed message from {node_id}: "
          f"{params.get('from')} -> {params.get('to')}")
    # TODO: store in Message / MessageRecipient tables


def _handle_relay_fog_node(params):
    """Register a fog device that was reported by a node."""
    db = SessionLocal()
    try:
        name = params.get("device_name", "")
        if not name:
            return
        existing = db.query(FogDevice).filter(
            FogDevice.device_name == name
        ).first()
        if existing:
            existing.status = params.get("status", "active")
            db.commit()
        else:
            db.add(FogDevice(
                device_name=name,
                status=params.get("status", "active"),
            ))
            db.commit()
    finally:
        db.close()


def _handle_relay_chat_msg(node_id, params):
    """Store a chat message relayed from a node's mobile user."""
    # params: {conversation_id, sender_id, message_text}
    print(f"[NODE-PROTO] Chat message from node {node_id}: "
          f"conv={params.get('conversation_id')} "
          f"sender={params.get('sender_id')}")
    # TODO: store in your messaging tables
    # This needs a Conversation / ChatMessage model or mapping to
    # your existing Message / MessageRecipient tables.


def _handle_sos_alert(node_id, params):
    """Create an SOS request from a mobile user via a node."""
    user_id = params.get("user_id", 0)
    db = SessionLocal()
    try:
        user = db.query(User).filter(User.id == user_id).first()
        username = user.username if user else f"User {user_id}"
        sos = ResidentAdminMessage(
            sender_id=user_id,
            kind="sos_request",
            subject=f"SOS from {username} (via node {node_id})",
            body=f"SOS alert received via fog node {node_id}",
            priority=100,
            status="queued",
        )
        db.add(sos)
        db.commit()
        print(f"[NODE-PROTO] SOS alert created for user {user_id} via {node_id}")
    finally:
        db.close()


def _handle_change_password(params):
    """Change a user's password (relayed from node)."""
    user_id = params.get("user_id", 0)
    new_password = params.get("new_password", "")
    if not user_id or not new_password:
        return
    db = SessionLocal()
    try:
        user = db.query(User).filter(User.id == user_id).first()
        if user:
            from routes.auth import get_password_hash
            user.password_hash = get_password_hash(new_password)
            db.commit()
            print(f"[NODE-PROTO] Password changed for user {user_id}")
    finally:
        db.close()


def _handle_stats_response(node_id, params):
    """Store stats reported by a node."""
    node_registry.update_stats(node_id, params)
```

### 3.3 New file: `services/node_registry.py`

**Purpose:** In-memory tracking of connected nodes (status, last heartbeat, stats).

```python
# services/node_registry.py

import time
from typing import Dict, Optional


class NodeInfo:
    def __init__(self, node_id: str, params: dict, xbee_addr: str):
        self.node_id = node_id
        self.xbee_addr = xbee_addr
        self.ip_address = params.get("ip_address", "")
        self.device_name = params.get("device_name", node_id)
        self.status = "active"
        self.registered_at = time.time()
        self.last_heartbeat = time.time()
        self.stats: dict = {}

    def to_dict(self) -> dict:
        return {
            "node_id": self.node_id,
            "xbee_addr": self.xbee_addr,
            "ip_address": self.ip_address,
            "device_name": self.device_name,
            "status": self.status,
            "registered_at": self.registered_at,
            "last_heartbeat": self.last_heartbeat,
            "seconds_since_heartbeat": int(time.time() - self.last_heartbeat),
            "stats": self.stats,
        }


class NodeRegistry:
    def __init__(self):
        self._nodes: Dict[str, NodeInfo] = {}

    def register(self, node_id: str, params: dict, xbee_addr: str):
        self._nodes[node_id] = NodeInfo(node_id, params, xbee_addr)

    def heartbeat(self, node_id: str, params: dict, xbee_addr: str):
        if node_id not in self._nodes:
            self.register(node_id, params, xbee_addr)
        node = self._nodes[node_id]
        node.last_heartbeat = time.time()
        node.status = "active"
        node.ip_address = params.get("ip_address", node.ip_address)

    def update_stats(self, node_id: str, stats: dict):
        if node_id in self._nodes:
            self._nodes[node_id].stats = stats

    def get_node(self, node_id: str) -> Optional[NodeInfo]:
        return self._nodes.get(node_id)

    def get_all(self) -> list:
        # Mark nodes stale if no heartbeat for > 90 seconds
        now = time.time()
        for n in self._nodes.values():
            if now - n.last_heartbeat > 90:
                n.status = "stale"
        return [n.to_dict() for n in self._nodes.values()]

    def count_active(self) -> int:
        now = time.time()
        return sum(1 for n in self._nodes.values()
                   if now - n.last_heartbeat <= 90)


node_registry = NodeRegistry()
```

### 3.4 Update `broadcast_dispatcher.py`

**What to change:** When dispatching a broadcast, wrap it in the JSON protocol format that nodes understand.

```python
# In broadcast_dispatcher.py, change send_broadcast() call:

# BEFORE (sends raw text):
send_broadcast(broadcast.body or broadcast.subject)

# AFTER (sends JSON command nodes can parse):
import json
from services.xbee_service import xbee_service

payload = json.dumps({
    "cmd": "BROADCAST_MSG",
    "params": {
        "from": "admin",
        "to": "all",
        "message": broadcast.body or broadcast.subject,
        "subject": broadcast.subject,
        "msg_type": broadcast.msg_type,
        "severity": broadcast.severity,
    }
}, separators=(",", ":"))
send_broadcast(payload)
```

### 3.5 New route: `routes/node_api.py`

**Purpose:** API endpoints for admin dashboard to see connected nodes.

```python
# routes/node_api.py

from fastapi import APIRouter
from services.node_registry import node_registry
from services.xbee_service import xbee_service

router = APIRouter(prefix="/api/nodes", tags=["Node Management"])


@router.get("/")
def list_nodes():
    """List all registered fog nodes with their status."""
    nodes = node_registry.get_all()
    return {
        "total": len(nodes),
        "active": node_registry.count_active(),
        "nodes": nodes,
    }


@router.post("/{node_id}/sync")
def trigger_sync(node_id: str):
    """Manually trigger a data sync to a specific node."""
    from services.node_protocol import _handle_sync_request
    _handle_sync_request(node_id)
    return {"success": True, "message": f"Sync sent to {node_id}"}


@router.post("/{node_id}/get-stats")
def request_stats(node_id: str):
    """Ask a node to report its stats."""
    xbee_service.send_json({"cmd": "GET_STATS", "node_id": node_id})
    return {"success": True, "message": f"Stats request sent to {node_id}"}
```

**Register in `app/main.py`:**
```python
from routes.node_api import router as node_router
app.include_router(node_router)
```

### 3.6 Database Migration

**Optional but recommended:** Add a `has_agreed_sos` column to the `users` table. The node protocol includes this field; the admin currently doesn't have it.

```sql
ALTER TABLE users ADD COLUMN has_agreed_sos INTEGER DEFAULT 0;
```

Or in SQLAlchemy models:
```python
class User(Base):
    # ... existing columns ...
    has_agreed_sos = Column(Integer, default=0, nullable=False)
```

### 3.7 Wire it all together in `app/main.py`

```python
# In app/main.py startup:

from services.xbee_service import xbee_service
from services.node_protocol import handle_node_command

@app.on_event("startup")
def start_xbee_protocol():
    try:
        xbee_service.open()
        xbee_service.set_command_handler(handle_node_command)
        print("[XBEE] Node protocol handler registered")
    except Exception as e:
        print(f"[XBEE] Could not start: {e}")
```

---

## 4. XBee Protocol Reference

All communication is newline-delimited JSON. Every frame has at minimum:

```json
{"cmd": "COMMAND_NAME", "node_id": "node-01", "ts": 1234567890}
```

### Node → Admin

| Command | Params | Expected Response |
|---|---|---|
| `REGISTER` | `{ip_address, device_name, status, free_heap, storage_available}` | `REGISTER_ACK` |
| `HEARTBEAT` | `{ip_address, uptime, free_heap, fog_nodes, messages, storage_available}` | `PONG` |
| `SYNC_REQUEST` | *(none)* | `SYNC_DATA` |
| `RELAY_MSG` | `{from, to, message}` | *(none)* |
| `RELAY_FOG_NODE` | `{device_name, ip_address, status}` | *(none)* |
| `RELAY_CHAT_MSG` | `{conversation_id, sender_id, message_text}` | *(none)* |
| `SOS_ALERT` | `{user_id, conversation_id}` | *(none)* |
| `CHANGE_PASSWORD` | `{user_id, old_password, new_password}` | *(none)* |
| `STATS_RESPONSE` | `{fog_nodes_count, active_fog_nodes, total_messages, free_heap, uptime, ip_address, storage_available}` | *(none)* |

### Admin → Node

| Command | Params / Payload |
|---|---|
| `REGISTER_ACK` | `{node_id}` |
| `PONG` | `{node_id}` |
| `SYNC_DATA` | `{users: [...], conversations: [...], chat_messages: [...], announcements: [...], fog_nodes: [...], messages: [...]}` |
| `BROADCAST_MSG` | `{params: {from, to, message}}` |
| `ADD_FOG_NODE` | `{params: {device_name, ip_address, status}}` |
| `GET_STATS` | `{node_id}` |

### SYNC_DATA Payload Schemas

```json
{
  "cmd": "SYNC_DATA",
  "users": [
    {"id": 1, "username": "admin", "email": "admin@hopfog.com", "role": "admin", "is_active": true, "has_agreed_sos": true}
  ],
  "conversations": [
    {"id": 1, "participants": [1, 2], "name": "", "last_message": "Hello", "last_timestamp": "1234567890"}
  ],
  "chat_messages": [
    {"id": 1, "conversation_id": 1, "sender_id": 2, "message_text": "Hello", "sent_at": "1234567890"}
  ],
  "announcements": [
    {"id": 1, "title": "Test", "message": "Test announcement", "created_at": "1234567890"}
  ],
  "fog_nodes": [
    {"id": 1, "device_name": "node-01", "ip_address": "192.168.4.1", "status": "active", "added_at": 1234567890}
  ],
  "messages": [
    {"id": 1, "from": "user1", "to": "user2", "message": "Hello", "timestamp": 1234567890, "node": "node-01"}
  ]
}
```

> **Timestamp convention:** All timestamps are Unix seconds (integers)
> or stringified Unix seconds (e.g. `"1234567890"`). The node firmware
> uses `millis()/1000` (uptime-based, not wall-clock). The admin should
> convert its `datetime` columns to Unix seconds when building SYNC_DATA.

---

## 5. Daisy Chaining Analysis

### Question: Can a second node connect to the first node (instead of directly to admin) to extend range further?

### Short Answer: **Partially — at the radio level yes, at the application level no (not yet).**

### Detailed Explanation

#### ✅ What already works: XBee mesh routing

If you use **XBee S2C** or **XBee3** modules in **ZigBee mesh mode**, the XBee radios themselves support multi-hop routing transparently:

```
Admin (Coordinator) ←──XBee──→ Node A (Router) ←──XBee──→ Node B (Router)
```

In this topology, Node B's XBee frames automatically route through Node A's XBee to reach the Admin's XBee. **This happens at the XBee firmware level** — the application code on Node B doesn't need to know about Node A. It just sends, and the XBee mesh routes it.

**Requirements for this to work:**
1. Admin XBee must be configured as **Coordinator** (CE=1)
2. Node XBees must be configured as **Routers** (CE=0, JV=1)
3. All XBees must be on the same PAN ID and channel
4. Nodes must be within XBee radio range of at least one other device in the mesh

#### ❌ What does NOT work: application-level WiFi daisy chaining

The current node architecture has each node running its own independent WiFi AP. Mobile phones connect to a node's WiFi, and that node talks to admin via XBee. **There is no WiFi-level relaying between nodes.**

```
Phone → WiFi → Node B → XBee mesh → (Node A relays) → Admin
                                       ✅ XBee layer handles this

Phone → WiFi → Node B → WiFi → Node A → XBee → Admin
                                       ❌ NOT supported
```

#### ❌ What does NOT work: application-level message forwarding

If a node can't reach the admin via XBee, it does not currently try to forward messages to another node via WiFi HTTP. Each node assumes it has a direct (or mesh-routed) XBee link to admin.

### What would be needed for full application-level daisy chaining

If you wanted Node B to connect to Node A via WiFi (instead of XBee), you would need:

1. **Node-to-node discovery** — nodes would need to discover each other (mDNS, broadcast, or config)
2. **HTTP forwarding** — Node B would forward API calls to Node A's REST API over WiFi
3. **Relay tracking** — messages would need hop-count / TTL to prevent loops
4. **This is significant additional work** and is not recommended — use XBee mesh instead

### Recommendation

**Use XBee ZigBee mesh mode.** Configure the admin XBee as Coordinator and all node XBees as Routers. The XBee radios will automatically form a mesh network and route frames through intermediate nodes. This gives you daisy chaining at the radio level with zero application code changes.

```
┌─────────────┐    XBee     ┌─────────────┐    XBee     ┌─────────────┐
│   Admin     │◄───mesh────►│   Node A    │◄───mesh────►│   Node B    │
│  Coordinator│   (auto)    │   Router    │   (auto)    │   Router    │
└─────────────┘             └─────────────┘             └─────────────┘
      ▲                          ▲                           ▲
 WiFi │                     WiFi │                      WiFi │
      ▼                          ▼                           ▼
  ┌────────┐               ┌────────┐                  ┌────────┐
  │ Phones │               │ Phones │                  │ Phones │
  └────────┘               └────────┘                  └────────┘
```

Node B's XBee frames reach Admin via Node A's XBee automatically. No code changes needed.

---

## 6. Implementation Priority

### Phase 1 — Minimum viable (get nodes working)
1. ✅ Add JSON command parsing to `xbee_service.py` (`_on_receive`)
2. ✅ Create `node_protocol.py` with handlers for `REGISTER`, `HEARTBEAT`, `SYNC_REQUEST`
3. ✅ Create `node_registry.py` for in-memory node tracking
4. ✅ Wire up in `app/main.py` startup
5. ✅ Send `REGISTER_ACK` and `PONG` responses

### Phase 2 — Data sync
6. ✅ Implement `SYNC_DATA` response with users and announcements
7. ✅ Handle `RELAY_CHAT_MSG` — store in DB
8. ✅ Handle `SOS_ALERT` — create ResidentAdminMessage
9. ✅ Handle `CHANGE_PASSWORD` — update user hash

### Phase 3 — Full integration
10. ✅ Update `broadcast_dispatcher.py` to send JSON-framed broadcasts
11. ✅ Add `routes/node_api.py` for admin dashboard
12. ✅ Add `has_agreed_sos` column to users table
13. ✅ Build conversations/chat_messages sync into SYNC_DATA
14. ✅ Handle `RELAY_MSG` and `RELAY_FOG_NODE`

---

## Files to Create/Modify Summary

| File | Action | Description |
|---|---|---|
| `services/xbee_service.py` | **Modify** | Add JSON parsing, `send_json()`, command handler callback |
| `services/node_protocol.py` | **Create** | All incoming command handlers + response senders |
| `services/node_registry.py` | **Create** | In-memory node tracking (heartbeats, stats) |
| `services/broadcast_dispatcher.py` | **Modify** | Wrap broadcasts in JSON protocol format |
| `routes/node_api.py` | **Create** | REST endpoints for node management (`/api/nodes/`) |
| `app/main.py` | **Modify** | Register node protocol handler on startup, include node router |
| `database/models.py` | **Modify** | Add `has_agreed_sos` to User model |
