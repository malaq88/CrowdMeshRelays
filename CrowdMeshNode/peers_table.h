#pragma once

#include <Arduino.h>
#include "config.h"

struct MeshPeer {
  char nodeId[13];
  char name[24];
  int8_t rssi;
  uint32_t lastSeenMs;
  bool active;
};

struct PeerTable {
  uint8_t peerCount;
  MeshPeer peers[MAX_PEERS];
  char localName[24];
  char localId[13];
  bool bleConnected;
};

void peersBegin();
void peersSetIdentity(const char* nodeId, const char* name);
void peersSetBleConnected(bool connected);
void peersUpsert(const char* nodeId, const char* name, int8_t rssi);
void peersPrune(uint32_t nowMs);
PeerTable& peersGet();
void peersLogStatus();
