# CrowdMesh BroadCast (ESP8266 Relay)

**Idioma:** [Português (BR)](README.md) · [English (US)](README.en.md)

Firmware de **repetidor ESP-NOW** para a placa **ESP8266 V3 com OLED 0,96"**.

**Diferença vs outros sketches em `Relays/`:** sem BLE e sem E2E: só reencaminha / guarda ciphertext (Nexus **v3**) para estender a malha. CYD/Node são os nós que falam com o celular.

```text
Celular ──BLE──► ESP32 ──ESP-NOW──► ESP8266 Relay ──ESP-NOW──► outro ESP32
                         (store+ACK)  (OLED status)
```

## Hardware

![ESP8266 V3 com OLED 0,96″](ESP8266%20MINI%20DISPLAY%20MODULE.jpeg)

- ESP8266 + SSD1306 128×64 (I2C)
- Pinos OLED (iguais ao [openchain / SmartLock](https://github.com/malaq88/openchain)):
  - **SDA** = GPIO14 (D5 no NodeMCU)
  - **SCL** = GPIO12 (D6 no NodeMCU)
  - Endereço I2C **0x3C** (tente `0x3D` em `config.h` se a tela não ligar)

## Bibliotecas (Arduino Library Manager)

- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library)

Núcleo: **esp8266 by ESP8266 Community**

Board sugerida: **NodeMCU 1.0 (ESP-12E Module)**

## O que faz

1. Entra em ESP-NOW no canal Wi‑Fi fixo (`WIFI_CHANNEL`, padrão **1**).
2. Recebe `HELLO` / `TEXT` / `ACK` com magic `CM` e `NEXUS_VERSION == 3`.
3. Descarta duplicatas (`msgId` + origin) e pacotes inválidos.
4. Guarda TEXT opaco até ACK ou fim de `lifeSec` (~30 min); reenvia a cada ~15 s.
5. Se `TTL > 1`, reenvia com TTL−1 (unicast ao destino + broadcast).
6. Anuncia-se periodicamente como `Relay-XXXX` (HELLO sem chave E2E).
7. Mostra no OLED: ID, canal, contagem de forwards, último pacote.

**Não** descriptografa mensagens. Relays só veem ciphertext (E2E nos ESP32).

## Como gravar

1. Abra `CrowdMeshBroadCast/CrowdMeshBroadCast.ino` na Arduino IDE.
2. Selecione a placa ESP8266 correta e a porta.
3. Upload.
4. Serial 115200: deve aparecer `NODE ID` e `[ready] store-and-forward CrowdMesh v3`.

Documentação do repositório: [README (pt-BR)](../README.md) · [README (en-US)](../README.en.md)

## Compatibilidade com CYD / Node

- Use firmwares CrowdMesh **v3** (store + ACK) nos ESP32. **Não** mistura com v2.
- Todos no **mesmo canal**. Se a malha não atravessar o relay, alinhe `WIFI_CHANNEL` aqui com o canal real do ESP32 (Serial / Wireshark / teste com canal 1–11).

Nos ESP32 atuais o STA costuma ficar no canal 1 após `WiFi.mode(WIFI_STA)`.

## Constantes relevantes (`config.h`)

| Define | Padrão | Significado |
|--------|--------|-------------|
| `NEXUS_VERSION` | 3 | Deve coincidir com CYD/Node |
| `MSG_LIFETIME_SEC` | 1800 | Vida útil na store |
| `STORE_RETRY_MS` | 15000 | Intervalo de reenvio |
| `MAX_STORE_MSG` | 6 | Capacidade (RAM do ESP8266) |
| `WIFI_CHANNEL` | 1 | Canal ESP-NOW |

## OLED

Lógica de init/desenho baseada no sketch `ESP8266_SmartLock` do openchain:

- `new Adafruit_SSD1306` → `Wire.begin(SDA, SCL)` → `begin(SSD1306_SWITCHCAPVCC, 0x3C)`
- Atualização periódica sem bloquear o loop

## Limitações

- Sem Companion BLE nesta placa.
- RSSI por pacote no ESP8266 ESP-NOW é limitado; o OLED pode mostrar `rssi 0`.
- Fila pequena (~6 msgs); alcancer ≈ Wi‑Fi 2,4 GHz.
