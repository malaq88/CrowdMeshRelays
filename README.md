# CrowdMeshRelays

**Idioma:** [Português (BR)](README.md) · [English (US)](README.en.md)

Firmwares das placas que formam a malha **CrowdMesh**: chat ponto a ponto sem internet. O celular fala com um ESP32 por **Bluetooth Low Energy**; os nós conversam entre si por **ESP-NOW**. O relay só encaminha **ciphertext**.

Este repositório publica, por enquanto, dois sketches:

| Sketch | Placa | BLE | Tela | Papel |
|--------|-------|-----|------|--------|
| [`CrowdMeshNode`](CrowdMeshNode/) | ESP32 Dev Module | Sim (NimBLE + outbox) | LED (GPIO 2) | Nó completo: Companion + mesh E2E + store/ACK |
| [`CrowdMeshBroadCast`](CrowdMeshBroadCast/) | ESP8266 V3 + OLED 0,96″ | Não | SSD1306 128×64 | Relay: reencaminha e guarda ciphertext |

```text
Celular ──BLE──► ESP32 Node ──ESP-NOW──► ESP8266 BroadCast ──ESP-NOW──► outro ESP32
                         (cifra+store)     (store opaco + OLED)            (decifra)
```

O app Android (**CrowdMesh Companion**) não está neste repositório. Ele se conecta ao anúncio BLE `CrowdMesh-Node-XXXX`.

Protocolo no ar: **Nexus v3** (CMP 0.3). Magic `CM`, `version = 3`. Tipos: `HELLO`, `TEXT` (cifrado no nó) e `ACK`. Não mistura com firmwares v2.

---

## CrowdMesh Node

Firmware para **ESP32 Dev Module** (sem display): nó completo com BLE, mesh E2E e LED de status.

![ESP32 Dev Module](CrowdMeshNode/ESP32%20DEV%20KIT%20MODULE.jpeg)

- BLE (NimBLE) com o CrowdMesh Companion
- Mesh ESP-NOW **Nexus v3**: TEXT cifrado, store-and-forward (~30 min), ACK
- **Fila BLE (outbox):** se o app ainda não estiver inscrito em notify, `MSG` / `ACK` / etc. ficam no ESP e saem quando o Companion ficar pronto
- E2E **X25519 + AES-256-GCM**; comando `PEERKEY`
- Identidade: `HELLO|<nodeId>|<nome>|<pk>` e característica GATT legível

### Bibliotecas e gravação

- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (h2zero)
- Placa: **ESP32 Dev Module**
- **Tools → Partition Scheme → `Huge APP (3MB No OTA/1MB SPIFFS)`**
- Abra `CrowdMeshNode/CrowdMeshNode.ino`, grave, Serial **115200**
- No Companion, conecte em `CrowdMesh-Node-XXXX`

No Serial deve aparecer `NODE ID : AABBCCDDEEFF`. Se o app ficar em “sincronizando ID…”, regrave e reconecte. Logs da fila: `[ble] queued` / `[ble] flushed`.

| Define (`config.h`) | Padrão | Significado |
|---------------------|--------|-------------|
| `DEVICE_NAME_PREFIX` | `CrowdMesh-Node` | Prefixo do anúncio BLE |
| `NEXUS_VERSION` | 3 | Versão do pacote mesh |
| `MSG_LIFETIME_SEC` | 1800 | Vida útil na store (~30 min) |
| `STORE_RETRY_MS` | 15000 | Intervalo de reenvio |
| `MAX_STORE_MSG` | 10 | Capacidade da fila mesh |

Detalhes: [pt-BR](CrowdMeshNode/README.md) · [en-US](CrowdMeshNode/README.en.md).

---

## CrowdMesh BroadCast

Firmware de **repetidor ESP-NOW** para **ESP8266 V3 com OLED 0,96″**. Sem BLE e sem E2E: só reencaminha e guarda ciphertext para estender a malha. Quem fala com o celular é o Node.

![ESP8266 V3 com OLED 0,96″](CrowdMeshBroadCast/ESP8266%20MINI%20DISPLAY%20MODULE.jpeg)

- ESP8266 + SSD1306 128×64 (I2C, endereço **0x3C**; tente `0x3D` em `config.h` se a tela não ligar)
- **SDA** = GPIO14 (D5 no NodeMCU), **SCL** = GPIO12 (D6 no NodeMCU), iguais ao [openchain / SmartLock](https://github.com/malaq88/openchain)

### Bibliotecas e gravação

- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library)
- Núcleo: **esp8266 by ESP8266 Community**
- Board sugerida: **NodeMCU 1.0 (ESP-12E Module)**
- Abra `CrowdMeshBroadCast/CrowdMeshBroadCast.ino`, grave, Serial **115200**
- Deve aparecer `NODE ID` e `[ready] store-and-forward CrowdMesh v3`

### O que faz

1. Entra em ESP-NOW no canal Wi-Fi fixo (`WIFI_CHANNEL`, padrão **1**).
2. Recebe `HELLO` / `TEXT` / `ACK` com magic `CM` e `NEXUS_VERSION == 3`.
3. Descarta duplicatas (`msgId` + origin) e pacotes inválidos.
4. Guarda TEXT opaco até ACK ou fim de `lifeSec` (~30 min); reenvia a cada ~15 s.
5. Se `TTL > 1`, reenvia com TTL−1 (unicast ao destino + broadcast).
6. Anuncia-se como `Relay-XXXX` (HELLO sem chave E2E).
7. OLED: ID, canal, contagem de forwards, último pacote.

Não descriptografa. O E2E fica nos ESP32.

| Define (`config.h`) | Padrão | Significado |
|---------------------|--------|-------------|
| `NEXUS_VERSION` | 3 | Deve coincidir com o Node |
| `MSG_LIFETIME_SEC` | 1800 | Vida útil na store |
| `STORE_RETRY_MS` | 15000 | Intervalo de reenvio |
| `MAX_STORE_MSG` | 6 | Capacidade (RAM do ESP8266) |
| `WIFI_CHANNEL` | 1 | Canal ESP-NOW |

No ESP32 o STA costuma ficar no canal 1 após `WiFi.mode(WIFI_STA)`. Se a malha não atravessar o relay, alinhe `WIFI_CHANNEL` com o canal real do Node.

Detalhes: [pt-BR](CrowdMeshBroadCast/README.md) · [en-US](CrowdMeshBroadCast/README.en.md).

---

## Como começar

1. Grave o **Node** (partição Huge APP + NimBLE-Arduino).
2. Opcional: grave um ou mais **BroadCast** no mesmo canal e na mesma versão Nexus v3.
3. No Companion, conecte em `CrowdMesh-Node-XXXX`.
4. Troque contatos por QR (com chave pública) e envie texto. Com o destino fora do ar, a malha guarda a mensagem cerca de 30 minutos.

Regrave Node e BroadCast juntos. Firmware v3 não fala com v2.

---

## Limitações

- O hop celular ↔ Node ainda vai em claro no BLE. O nó local é confiável; relays não leem o conteúdo.
- Alcance de Wi-Fi 2,4 GHz / ESP-NOW.
- Fila mesh: ~10 mensagens no Node, ~6 no BroadCast. Estouro descarta.
- BroadCast não tem Companion. O RSSI por pacote no ESP8266 é limitado; o OLED pode mostrar `rssi 0`.

---

## Licença

MIT © 2026 Antonio Malaquias. Ver [LICENSE](LICENSE).
