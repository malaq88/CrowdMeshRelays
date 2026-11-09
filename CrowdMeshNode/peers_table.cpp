#include "peers_table.h"

static PeerTable gPeers;

void peersBegin() {
  memset(&gPeers, 0, sizeof(gPeers));
}

PeerTable& peersGet() { return gPeers; }

void peersSetIdentity(const char* nodeId, const char* name) {
  strncpy(gPeers.localId, nodeId, sizeof(gPeers.localId) - 1);
  strncpy(gPeers.localName, name, sizeof(gPeers.localName) - 1);
}

void peersSetBleConnected(bool connected) {
  gPeers.bleConnected = connected;
}

void peersUpsert(const char* nodeId, const char* name, int8_t rssi) {
  if (nodeId == nullptr || nodeId[0] == '\0') return;
  if (strcmp(nodeId, gPeers.localId) == 0) return;

  int freeIdx = -1;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (gPeers.peers[i].active && strcmp(gPeers.peers[i].nodeId, nodeId) == 0) {
      gPeers.peers[i].rssi = rssi;
      gPeers.peers[i].lastSeenMs = millis();
      if (name && name[0]) {
        strncpy(gPeers.peers[i].name, name, sizeof(gPeers.peers[i].name) - 1);
      }
      return;
    }
    if (!gPeers.peers[i].active && freeIdx < 0) freeIdx = i;
  }

  if (freeIdx < 0) return;
  gPeers.peers[freeIdx].active = true;
  strncpy(gPeers.peers[freeIdx].nodeId, nodeId, sizeof(gPeers.peers[freeIdx].nodeId) - 1);
  strncpy(gPeers.peers[freeIdx].name, (name && name[0]) ? name : nodeId,
          sizeof(gPeers.peers[freeIdx].name) - 1);
  gPeers.peers[freeIdx].rssi = rssi;
  gPeers.peers[freeIdx].lastSeenMs = millis();

  uint8_t count = 0;
  for (int i = 0; i < MAX_PEERS; i++) if (gPeers.peers[i].active) count++;
  gPeers.peerCount = count;
}

void peersPrune(uint32_t nowMs) {
  bool changed = false;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (gPeers.peers[i].active && (nowMs - gPeers.peers[i].lastSeenMs) > PEER_TIMEOUT_MS) {
      gPeers.peers[i].active = false;
      changed = true;
    }
  }
  if (changed) {
    uint8_t count = 0;
    for (int i = 0; i < MAX_PEERS; i++) if (gPeers.peers[i].active) count++;
    gPeers.peerCount = count;
  }
}

void peersLogStatus() {
  Serial.printf("[status] id=%s name=%s ble=%d peers=%u\n",
                gPeers.localId, gPeers.localName,
                gPeers.bleConnected ? 1 : 0, gPeers.peerCount);
  for (int i = 0; i < MAX_PEERS; i++) {
    if (!gPeers.peers[i].active) continue;
    Serial.printf("  peer %s (%s) rssi=%d\n",
                  gPeers.peers[i].nodeId,
                  gPeers.peers[i].name,
                  gPeers.peers[i].rssi);
  }
}
