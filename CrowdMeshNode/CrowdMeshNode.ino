/**
 * CrowdMesh Node — ESP32 Dev Module (sem display)
 *
 * Libraries:
 *   - NimBLE-Arduino (h2zero)
 *
 * Tools → Partition Scheme → "Huge APP (3MB No OTA/1MB SPIFFS)" (recomendado)
 */

#include <WiFi.h>

#include "config.h"
#include "credentials.h"
#include "peers_table.h"
#include "ble_bridge.h"
#include "mesh_espnow.h"
#include "crypto_util.h"

static uint32_t lastStatusMs = 0;
static uint32_t bleConnectedAtMs = 0;
static bool identityPushed = false;

void handleBleCommand(const String& line);
void onBleConnection(bool connected);
void onBleSubscribed();
void onMeshText(const uint8_t fromMac[6], const char* text);
void onMeshHello(const uint8_t fromMac[6], const char* name, int8_t rssi);
void onMeshAck(uint32_t msgId);
void onMeshExpired(uint32_t msgId);
void notifyPeersToPhone();
void sendHelloToPhone();
void pushIdentityToPhone();
void updateStatusLed();
void maybeFallbackIdentityPush();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== CrowdMesh Node v0.1 ==="));

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  credentialsBegin();
  NodeCredentials& creds = credentialsGet();

  Serial.println(F("================================="));
  Serial.printf(" NODE ID : %s\n", creds.nodeId);
  Serial.printf(" NAME    : %s\n", creds.displayName);
  Serial.printf(" MAC     : %02X:%02X:%02X:%02X:%02X:%02X\n",
                creds.mac[0], creds.mac[1], creds.mac[2],
                creds.mac[3], creds.mac[4], creds.mac[5]);
  Serial.println(F("================================="));

  peersBegin();
  peersSetIdentity(creds.nodeId, creds.displayName);

  meshSetHandlers(onMeshText, onMeshHello);
  meshSetDeliveryHandlers(onMeshAck, onMeshExpired);
  if (!meshBegin()) {
    Serial.println(F("[mesh] failed — restart board"));
  }

  bleBridgeSetHandlers(handleBleCommand, onBleConnection, onBleSubscribed);
  char bleName[28];
  snprintf(bleName, sizeof(bleName), "%s-%02X%02X",
           DEVICE_NAME_PREFIX, creds.mac[4], creds.mac[5]);
  bleBridgeBegin(bleName);
  char pkHex[65];
  cryptoPublicKeyHex(pkHex);
  bleBridgeUpdateIdentity(creds.nodeId, creds.displayName, pkHex);

  Serial.printf("[ready] waiting for Companion  nodeId=%s\n", creds.nodeId);
  peersLogStatus();
}

void loop() {
  const uint32_t now = millis();
  meshLoop();
  bleBridgeLoop();
  peersPrune(now);
  updateStatusLed();
  maybeFallbackIdentityPush();

  if (now - lastStatusMs >= STATUS_LOG_MS) {
    lastStatusMs = now;
    peersLogStatus();
    Serial.printf("[id] nodeId=%s ble=%d sub=%d pushed=%d\n",
                  credentialsGet().nodeId,
                  bleBridgeIsConnected() ? 1 : 0,
                  bleBridgeIsSubscribed() ? 1 : 0,
                  identityPushed ? 1 : 0);
  }
}

void maybeFallbackIdentityPush() {
  // Some NimBLE builds delay/miss onSubscribe — still push HELLO after connect.
  if (!bleBridgeIsConnected() || identityPushed || bleConnectedAtMs == 0) return;
  if (millis() - bleConnectedAtMs < 1500) return;
  identityPushed = true;
  Serial.println(F("[ble] fallback identity push (no subscribe yet)"));
  pushIdentityToPhone();
}

void updateStatusLed() {
  // Slow blink = idle; solid = BLE connected.
  if (bleBridgeIsConnected()) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    return;
  }
  digitalWrite(STATUS_LED_PIN, (millis() / 500) % 2 == 0 ? HIGH : LOW);
}

void onBleConnection(bool connected) {
  peersSetBleConnected(connected);
  if (connected) {
    bleConnectedAtMs = millis();
    identityPushed = false;
    Serial.println(F("[ble] Companion connected (wait subscribe)"));
    Serial.printf("[ble] will send HELLO with nodeId=%s\n", credentialsGet().nodeId);
  } else {
    bleConnectedAtMs = 0;
    identityPushed = false;
    Serial.println(F("[ble] Companion disconnected"));
  }
  updateStatusLed();
}

void onBleSubscribed() {
  identityPushed = true;
  Serial.println(F("[ble] notifications ON — pushing HELLO"));
  Serial.printf("[ble] nodeId=%s\n", credentialsGet().nodeId);
  pushIdentityToPhone();
}

void pushIdentityToPhone() {
  sendHelloToPhone();
  notifyPeersToPhone();
}

void sendHelloToPhone() {
  NodeCredentials& c = credentialsGet();
  char pkHex[65];
  cryptoPublicKeyHex(pkHex);
  bleBridgeUpdateIdentity(c.nodeId, c.displayName, pkHex);
  String line = String("HELLO|") + c.nodeId + "|" + c.displayName + "|" + pkHex;
  Serial.printf("[ble] >> %s\n", line.c_str());
  bleBridgeNotify(line);
}

void notifyPeersToPhone() {
  PeerTable& st = peersGet();
  String line = "PEERS|";
  bool first = true;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (!st.peers[i].active) continue;
    if (!first) line += ",";
    first = false;
    line += st.peers[i].nodeId;
    line += ":";
    line += st.peers[i].name;
  }
  bleBridgeNotify(line);

  char status[64];
  snprintf(status, sizeof(status), "STATUS|ble=%d|mesh=1|peers=%u",
           bleBridgeIsConnected() ? 1 : 0, st.peerCount);
  bleBridgeNotify(status);
}

void handleBleCommand(const String& line) {
  Serial.printf("[ble] cmd: %s\n", line.c_str());

  if (line.equalsIgnoreCase("SYNC") || line.equalsIgnoreCase("GETID")) {
    pushIdentityToPhone();
    bleBridgeNotify("OK");
    return;
  }

  if (line.startsWith("SETNAME|")) {
    String name = line.substring(8);
    name.trim();
    credentialsSetDisplayName(name.c_str());
    peersSetIdentity(credentialsGet().nodeId, credentialsGet().displayName);
    sendHelloToPhone();
    bleBridgeNotify("OK");
    return;
  }

  if (line.startsWith("PEERKEY|")) {
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    if (p1 < 0 || p2 < 0) {
      bleBridgeNotify("ERR|formato PEERKEY");
      return;
    }
    String nodeId = line.substring(p1 + 1, p2);
    String pkHex = line.substring(p2 + 1);
    nodeId.trim();
    pkHex.trim();
    pkHex.toUpperCase();
    uint8_t mac[6];
    meshParseNodeId(nodeId.c_str(), mac);
    if (cryptoRememberPeerHex(mac, pkHex.c_str())) {
      Serial.printf("[ble] peer key stored for %s\n", nodeId.c_str());
      bleBridgeNotify("OK");
    } else {
      bleBridgeNotify("ERR|chave invalida");
    }
    return;
  }

  if (line.startsWith("SEND|")) {
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    if (p1 < 0 || p2 < 0) {
      bleBridgeNotify("ERR|formato SEND");
      return;
    }
    String destId = line.substring(p1 + 1, p2);
    String text = line.substring(p2 + 1);
    destId.trim();
    text.trim();

    uint8_t destMac[6];
    meshParseNodeId(destId.c_str(), destMac);
    if (!cryptoHasPeerKey(destMac)) {
      bleBridgeNotify("ERR|sem chave do contato");
      return;
    }
    uint32_t msgId = 0;
    if (meshSendText(destMac, text.c_str(), &msgId)) {
      Serial.printf("[mesh] sent to %s: %s (msgId=%lu)\n",
                    destId.c_str(), text.c_str(), (unsigned long)msgId);
      String ok = String("OK|") + msgId;
      bleBridgeNotify(ok);
    } else {
      bleBridgeNotify("ERR|falha mesh");
    }
    return;
  }

  bleBridgeNotify("ERR|comando desconhecido");
}

void onMeshText(const uint8_t fromMac[6], const char* text) {
  char fromId[13];
  meshFormatNodeId(fromMac, fromId);

  Serial.printf("[mesh] MSG from %s: %s\n", fromId, text);

  String msg = String("MSG|") + fromId + "|" + text;
  bleBridgeNotify(msg);

  peersUpsert(fromId, fromId, 0);
}

void onMeshAck(uint32_t msgId) {
  String line = String("ACK|") + msgId;
  bleBridgeNotify(line);
  Serial.printf("[mesh] delivered ACK to phone msgId=%lu\n", (unsigned long)msgId);
}

void onMeshExpired(uint32_t msgId) {
  String line = String("EXPIRED|") + msgId;
  bleBridgeNotify(line);
  Serial.printf("[mesh] expired to phone msgId=%lu\n", (unsigned long)msgId);
}

void onMeshHello(const uint8_t fromMac[6], const char* name, int8_t rssi) {
  char fromId[13];
  meshFormatNodeId(fromMac, fromId);
  peersUpsert(fromId, name, rssi);
  if (bleBridgeIsConnected()) {
    notifyPeersToPhone();
  }
}
