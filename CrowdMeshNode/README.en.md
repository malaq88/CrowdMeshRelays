# CrowdMesh Node

**Language:** [Português (BR)](README.md) · [English (US)](README.en.md)

Firmware for the **ESP32 Dev Module** (no display): a full node with BLE + E2E mesh (Nexus **v3**) + LED.

Repository docs: [README (pt-BR)](../README.md) · [README (en-US)](../README.en.md)

**Difference vs other sketches:** same protocol as the CYD, without a TFT (status LED only).

## Hardware

![ESP32 Dev Module](ESP32%20DEV%20KIT%20MODULE.jpeg)

Reference board: **ESP32 Dev Module** (no display; status LED on GPIO 2).

## What this firmware does

- BLE (NimBLE) with the CrowdMesh Companion
- ESP-NOW mesh **Nexus v3**: encrypted TEXT, store-and-forward (~30 min), ACK
- **BLE outbox:** if the app is not yet connected or subscribed to notify, `MSG`/`ACK`/etc. stay on the ESP and are sent when the Companion is ready
- E2E X25519 + AES-GCM; `PEERKEY` command

## Libraries

- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (h2zero)

## Arduino IDE

- Board: **ESP32 Dev Module**
- **Tools → Partition Scheme → `Huge APP (3MB No OTA/1MB SPIFFS)`** (recommended)

## Node ID in the app

The Node sends `HELLO|<nodeId>|<name>|<pk>` when the phone enables BLE notifications
(and also as a fallback ~1.5 s after connect). Identity is also on the readable GATT characteristic.

On Serial (115200) you should see:

```
NODE ID : AABBCCDDEEFF
```

If the Companion stays on “syncing ID…”, reflash this firmware and reconnect.

Useful outbox logs: `[ble] queued` / `[ble] flushed`.

## Usage

1. Install NimBLE-Arduino.
2. Open `CrowdMeshNode/CrowdMeshNode.ino` and flash.
3. In the Companion, connect to `CrowdMesh-Node-XXXX`.
4. **Also reflash the CYD and BroadCast** with Nexus v3 (incompatible with v2).

## Relevant constants (`config.h`)

| Define | Default | Meaning |
|--------|---------|---------|
| `NEXUS_VERSION` | 3 | Mesh packet version |
| `MSG_LIFETIME_SEC` | 1800 | Store lifetime (~30 min) |
| `STORE_RETRY_MS` | 15000 | Retransmit interval |
| `MAX_STORE_MSG` | 10 | Mesh queue capacity |
