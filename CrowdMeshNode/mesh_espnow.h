#pragma once

#include <Arduino.h>
#include "config.h"

struct NexusPacket {
  uint8_t magic0;
  uint8_t magic1;
  uint8_t version;
  uint8_t type;
  uint8_t ttl;          // saltos por onda (mutável; zerado no AAD)
  uint16_t lifeSec;     // segundos restantes aproximados (mutável; zerado no AAD)
  uint32_t msgId;
  uint8_t origin[6];
  uint8_t dest[6];
  uint8_t payloadLen;
  uint8_t payload[180];
} __attribute__((packed));

typedef void (*MeshTextHandler)(const uint8_t fromMac[6], const char* text);
typedef void (*MeshHelloHandler)(const uint8_t fromMac[6], const char* name, int8_t rssi);
typedef void (*MeshAckHandler)(uint32_t msgId);
typedef void (*MeshExpiredHandler)(uint32_t msgId);

bool meshBegin();
void meshLoop();
void meshBroadcastHello(const char* displayName);
/** Envia TEXT, guarda na fila local e devolve msgId (pode ser nullptr). */
bool meshSendText(const uint8_t destMac[6], const char* text, uint32_t* outMsgId = nullptr);
void meshSetHandlers(MeshTextHandler onText, MeshHelloHandler onHello);
void meshSetDeliveryHandlers(MeshAckHandler onAck, MeshExpiredHandler onExpired);
void meshParseNodeId(const char* nodeIdHex, uint8_t outMac[6]);
void meshFormatNodeId(const uint8_t mac[6], char out[13]);
