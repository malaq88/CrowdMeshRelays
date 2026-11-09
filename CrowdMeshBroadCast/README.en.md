# CrowdMesh BroadCast (ESP8266 Relay)

**Language:** [Português (BR)](README.md) · [English (US)](README.en.md)

**ESP-NOW repeater** firmware for the **ESP8266 V3 board with a 0.96″ OLED**.

**Difference vs other sketches:** no BLE and no E2E: it only forwards / stores ciphertext (Nexus **v3**) to extend the mesh. CYD/Node are the nodes that talk to the phone.

```text
Phone ──BLE──► ESP32 ──ESP-NOW──► ESP8266 Relay ──ESP-NOW──► another ESP32
                      (store+ACK)   (OLED status)
```

## Hardware

![ESP8266 V3 with 0.96″ OLED](ESP8266%20MINI%20DISPLAY%20MODULE.jpeg)

- ESP8266 + SSD1306 128×64 (I2C)
- OLED pins (same as [openchain / SmartLock](https://github.com/malaq88/openchain)):
  - **SDA** = GPIO14 (D5 on NodeMCU)
  - **SCL** = GPIO12 (D6 on NodeMCU)
  - I2C address **0x3C** (try `0x3D` in `config.h` if the screen stays off)

## Libraries (Arduino Library Manager)

- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library)

Core: **esp8266 by ESP8266 Community**

Suggested board: **NodeMCU 1.0 (ESP-12E Module)**

## What it does

1. Joins ESP-NOW on a fixed Wi-Fi channel (`WIFI_CHANNEL`, default **1**).
2. Accepts `HELLO` / `TEXT` / `ACK` with magic `CM` and `NEXUS_VERSION == 3`.
3. Drops duplicates (`msgId` + origin) and invalid packets.
4. Holds opaque TEXT until ACK or `lifeSec` ends (~30 min); resends about every 15 s.
5. If `TTL > 1`, forwards with TTL−1 (unicast to the destination plus broadcast).
6. Announces itself periodically as `Relay-XXXX` (HELLO without an E2E key).
7. Shows on the OLED: ID, channel, forward count, last packet.

It does **not** decrypt messages. Relays only see ciphertext (E2E stays on the ESP32).

## Flashing

1. Open `CrowdMeshBroadCast/CrowdMeshBroadCast.ino` in the Arduino IDE.
2. Select the correct ESP8266 board and port.
3. Upload.
4. Serial 115200: you should see `NODE ID` and `[ready] store-and-forward CrowdMesh v3`.

Repository docs: [README (pt-BR)](../README.md) · [README (en-US)](../README.en.md)

## Compatibility with CYD / Node

- Use CrowdMesh **v3** firmwares (store + ACK) on the ESP32. Do not mix with v2.
- Everyone on the **same channel**. If the mesh does not cross the relay, align `WIFI_CHANNEL` here with the ESP32’s real channel (Serial / Wireshark / try channels 1–11).

On current ESP32 boards the STA usually stays on channel 1 after `WiFi.mode(WIFI_STA)`.

## Relevant constants (`config.h`)

| Define | Default | Meaning |
|--------|---------|---------|
| `NEXUS_VERSION` | 3 | Must match CYD/Node |
| `MSG_LIFETIME_SEC` | 1800 | Store lifetime |
| `STORE_RETRY_MS` | 15000 | Retransmit interval |
| `MAX_STORE_MSG` | 6 | Capacity (ESP8266 RAM) |
| `WIFI_CHANNEL` | 1 | ESP-NOW channel |

## OLED

Init/draw logic based on the openchain `ESP8266_SmartLock` sketch:

- `new Adafruit_SSD1306` → `Wire.begin(SDA, SCL)` → `begin(SSD1306_SWITCHCAPVCC, 0x3C)`
- Periodic refresh without blocking the loop

## Limitations

- No Companion BLE on this board.
- Per-packet RSSI on ESP8266 ESP-NOW is limited; the OLED may show `rssi 0`.
- Small queue (~6 msgs); range ≈ 2.4 GHz Wi-Fi.
