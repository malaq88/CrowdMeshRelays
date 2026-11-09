# CrowdMesh Node

**Idioma:** [Português (BR)](README.md) · [English (US)](README.en.md)

Firmware para **ESP32 Dev Module** (sem display): nó completo com BLE + mesh E2E (Nexus **v3**) + LED.

Documentação do repositório: [README (pt-BR)](../README.md) · [README (en-US)](../README.en.md)

**Diferença vs outros sketches em `Relays/`:** mesmo protocolo do CYD, sem TFT (só LED de status).

## Hardware

![ESP32 Dev Module](ESP32%20DEV%20KIT%20MODULE.jpeg)

Placa de referência: **ESP32 Dev Module** (sem display; LED de status no GPIO 2).

## O que este firmware faz

- BLE (NimBLE) com o CrowdMesh Companion
- Mesh ESP-NOW **Nexus v3**: TEXT cifrado, store-and-forward (~30 min), ACK
- **Fila BLE (outbox):** se o app ainda não estiver conectado/inscrito em notify, `MSG`/`ACK`/etc. ficam no ESP e são enviados quando o Companion ficar pronto
- E2E X25519 + AES-GCM; comando `PEERKEY`

## Bibliotecas

- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (h2zero)

## Arduino IDE

- Placa: **ESP32 Dev Module**
- **Tools → Partition Scheme → `Huge APP (3MB No OTA/1MB SPIFFS)`** (recomendado)

## Node ID no app

O Node envia `HELLO|<nodeId>|<nome>|<pk>` quando o celular ativa as notificações BLE
(e também por fallback ~1,5 s após conectar). A identidade também fica na característica GATT legível.

No Serial (115200) você deve ver:

```
NODE ID : AABBCCDDEEFF
```

Se o Companion ficar em “sincronizando ID…”, regrave este firmware e reconecte.

Logs úteis da fila BLE: `[ble] queued` / `[ble] flushed`.

## Uso

1. Instale NimBLE-Arduino.
2. Abra `CrowdMeshNode/CrowdMeshNode.ino` e grave.
3. No Companion, conecte em `CrowdMesh-Node-XXXX`.
4. **Regrave também CYD e BroadCast** com Nexus v3 (incompatível com v2).

## Constantes relevantes (`config.h`)

| Define | Padrão | Significado |
|--------|--------|-------------|
| `NEXUS_VERSION` | 3 | Versão do pacote mesh |
| `MSG_LIFETIME_SEC` | 1800 | Vida útil na store (~30 min) |
| `STORE_RETRY_MS` | 15000 | Intervalo de reenvio |
| `MAX_STORE_MSG` | 10 | Capacidade da fila mesh |
