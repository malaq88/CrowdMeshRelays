# CrowdMeshRelays

**Language:** [Português (BR)](README.md) · [English (US)](README.en.md)

Board firmwares for the **CrowdMesh** network: peer-to-peer chat without the internet. The phone talks to an ESP32 over **Bluetooth Low Energy**; nodes talk to each other over **ESP-NOW**. The relay only forwards **ciphertext**.

This repository currently publishes two sketches:

| Sketch | Board | BLE | Display | Role |
|--------|-------|-----|---------|------|
| [`CrowdMeshNode`](CrowdMeshNode/) | ESP32 Dev Module | Yes (NimBLE + outbox) | LED (GPIO 2) | Full node: Companion + E2E mesh + store/ACK |
| [`CrowdMeshBroadCast`](CrowdMeshBroadCast/) | ESP8266 V3 + 0.96″ OLED | No | SSD1306 128×64 | Relay: forwards and stores ciphertext |

```text
Phone ──BLE──► ESP32 Node ──ESP-NOW──► ESP8266 BroadCast ──ESP-NOW──► another ESP32
                    (encrypt+store)       (opaque store + OLED)           (decrypt)
```

The Android app (**CrowdMesh Companion**) is not in this repository. It connects to the BLE advertisement `CrowdMesh-Node-XXXX`.

On-air protocol: **Nexus v3** (CMP 0.3). Magic `CM`, `version = 3`. Types: `HELLO`, `TEXT` (encrypted on the node), and `ACK`. Do not mix with v2 firmwares.

---

## CrowdMesh Node

Firmware for the **ESP32 Dev Module** (no display): a full node with BLE, E2E mesh, and a status LED.

![ESP32 Dev Module](CrowdMeshNode/ESP32%20DEV%20KIT%20MODULE.jpeg)

- BLE (NimBLE) with the CrowdMesh Companion
- ESP-NOW mesh **Nexus v3**: encrypted TEXT, store-and-forward (~30 min), ACK
- **BLE outbox:** if the app is not yet subscribed to notify, `MSG` / `ACK` / etc. stay on the ESP and flush when the Companion is ready
- E2E **X25519 + AES-256-GCM**; `PEERKEY` command
- Identity: `HELLO|<nodeId>|<name>|<pk>` and a readable GATT characteristic

### Libraries and flashing

- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (h2zero)
- Board: **ESP32 Dev Module**
- **Tools → Partition Scheme → `Huge APP (3MB No OTA/1MB SPIFFS)`**
- Open `CrowdMeshNode/CrowdMeshNode.ino`, flash, Serial **115200**
- In the Companion, connect to `CrowdMesh-Node-XXXX`

Serial should show `NODE ID : AABBCCDDEEFF`. If the app stays on “syncing ID…”, reflash and reconnect. Outbox logs: `[ble] queued` / `[ble] flushed`.

| Define (`config.h`) | Default | Meaning |
|---------------------|---------|---------|
| `DEVICE_NAME_PREFIX` | `CrowdMesh-Node` | BLE advertisement prefix |
| `NEXUS_VERSION` | 3 | Mesh packet version |
| `MSG_LIFETIME_SEC` | 1800 | Store lifetime (~30 min) |
| `STORE_RETRY_MS` | 15000 | Retransmit interval |
| `MAX_STORE_MSG` | 10 | Mesh queue capacity |

Details: [pt-BR](CrowdMeshNode/README.md) · [en-US](CrowdMeshNode/README.en.md).

---

## CrowdMesh BroadCast

**ESP-NOW repeater** firmware for the **ESP8266 V3 with a 0.96″ OLED**. No BLE and no E2E: it only forwards and stores ciphertext to extend the mesh. The Node is what talks to the phone.

![ESP8266 V3 with 0.96″ OLED](CrowdMeshBroadCast/ESP8266%20MINI%20DISPLAY%20MODULE.jpeg)

- ESP8266 + SSD1306 128×64 (I2C, address **0x3C**; try `0x3D` in `config.h` if the screen stays off)
- **SDA** = GPIO14 (D5 on NodeMCU), **SCL** = GPIO12 (D6 on NodeMCU), same as [openchain / SmartLock](https://github.com/malaq88/openchain)

### Libraries and flashing

- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library)
- Core: **esp8266 by ESP8266 Community**
- Suggested board: **NodeMCU 1.0 (ESP-12E Module)**
- Open `CrowdMeshBroadCast/CrowdMeshBroadCast.ino`, flash, Serial **115200**
- You should see `NODE ID` and `[ready] store-and-forward CrowdMesh v3`

### What it does

1. Joins ESP-NOW on a fixed Wi-Fi channel (`WIFI_CHANNEL`, default **1**).
2. Accepts `HELLO` / `TEXT` / `ACK` with magic `CM` and `NEXUS_VERSION == 3`.
3. Drops duplicates (`msgId` + origin) and invalid packets.
4. Holds opaque TEXT until ACK or `lifeSec` ends (~30 min); resends about every 15 s.
5. If `TTL > 1`, forwards with TTL−1 (unicast to the destination plus broadcast).
6. Announces itself as `Relay-XXXX` (HELLO without an E2E key).
7. OLED: ID, channel, forward count, last packet.

It does not decrypt. E2E stays on the ESP32 nodes.

| Define (`config.h`) | Default | Meaning |
|---------------------|---------|---------|
| `NEXUS_VERSION` | 3 | Must match the Node |
| `MSG_LIFETIME_SEC` | 1800 | Store lifetime |
| `STORE_RETRY_MS` | 15000 | Retransmit interval |
| `MAX_STORE_MSG` | 6 | Capacity (ESP8266 RAM) |
| `WIFI_CHANNEL` | 1 | ESP-NOW channel |

On current ESP32 boards the STA usually stays on channel 1 after `WiFi.mode(WIFI_STA)`. If the mesh does not cross the relay, align `WIFI_CHANNEL` with the Node’s real channel.

Details: [pt-BR](CrowdMeshBroadCast/README.md) · [en-US](CrowdMeshBroadCast/README.en.md).

---

## Getting started

1. Flash the **Node** (Huge APP partition + NimBLE-Arduino).
2. Optional: flash one or more **BroadCast** boards on the same channel and the same Nexus v3.
3. In the Companion, connect to `CrowdMesh-Node-XXXX`.
4. Exchange contacts via QR (with the public key) and send text. If the destination is offline, the mesh holds the message for about 30 minutes.

Reflash Node and BroadCast together. v3 firmware does not talk to v2.

---

## Limitations

- The phone ↔ Node hop is still plaintext over BLE. The local node is trusted; relays cannot read the content.
- Range is 2.4 GHz Wi-Fi / ESP-NOW.
- Mesh queue: ~10 messages on the Node, ~6 on the BroadCast. Overflow drops messages.
- BroadCast has no Companion. Per-packet RSSI on the ESP8266 is limited; the OLED may show `rssi 0`.

---

## License

MIT © 2026 Antonio Malaquias. See [LICENSE](LICENSE).
