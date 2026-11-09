#pragma once

// CrowdMesh Node — ESP32 Dev Module (sem display)
//
// Este arquivo NÃO é o "ID do usuário" digitável no app.
// Ele só define constantes do firmware (prefixo BLE, timeouts, UUIDs).
//
// Nome BLE anunciado = DEVICE_NAME_PREFIX + últimos 2 bytes do MAC.
// Ex.: MAC ...:A1:B2  →  CrowdMesh-Node-A1B2

#define DEVICE_NAME_PREFIX "CrowdMesh-Node"

#define MAX_PEERS 12
#define MAX_SEEN_MSG 48
#define MAX_STORE_MSG 10
#define MAX_DELIVERED 32
#define PEER_TIMEOUT_MS 30000
#define HELLO_INTERVAL_MS 5000
#define STATUS_LOG_MS 10000
#define STORE_RETRY_MS 15000
/** Tempo de vida da mensagem na malha (store-and-forward). */
#define MSG_LIFETIME_SEC 1800

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 2
#endif

#define PKT_HELLO 0x01
#define PKT_TEXT  0x02
#define PKT_ACK   0x03

#define NEXUS_MAGIC0 'C'
#define NEXUS_MAGIC1 'M'
/** v3: lifeSec + store-and-forward; AAD ignora ttl/lifeSec (mutáveis). */
#define NEXUS_VERSION 3
#define DEFAULT_TTL 5

#define BLE_SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHARACTERISTIC_ID_UUID "6e400004-b5a3-f393-e0a9-e50e24dcca9e"

#define ENABLE_PAYLOAD_CRYPTO 1
