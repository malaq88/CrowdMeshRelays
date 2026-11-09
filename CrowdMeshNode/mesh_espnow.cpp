#include "mesh_espnow.h"

#include <stddef.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "credentials.h"
#include "crypto_util.h"
#include "config.h"

static MeshTextHandler gOnText = nullptr;
static MeshHelloHandler gOnHello = nullptr;
static MeshAckHandler gOnAck = nullptr;
static MeshExpiredHandler gOnExpired = nullptr;
static uint32_t gLastHelloMs = 0;
static uint32_t gLastStoreFlushMs = 0;
static uint32_t gMsgCounter = 1;

struct SeenEntry {
  uint32_t msgId;
  uint8_t origin[6];
  uint32_t seenAt;
  bool used;
};

struct StoreEntry {
  bool used;
  bool isOrigin;       // nós originamos — notificar EXPIRED
  uint32_t expiresAt;  // millis deadline
  uint32_t lastSentAt;
  NexusPacket pkt;     // cópia (ttl/lifeSec atualizados ao reenviar)
};

struct DeliveredEntry {
  bool used;
  uint32_t msgId;
  uint8_t origin[6];
};

static SeenEntry gSeen[MAX_SEEN_MSG];
static StoreEntry gStore[MAX_STORE_MSG];
static DeliveredEntry gDelivered[MAX_DELIVERED];

static bool macEqual(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

void meshFormatNodeId(const uint8_t mac[6], char out[13]) {
  snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void meshParseNodeId(const char* nodeIdHex, uint8_t outMac[6]) {
  memset(outMac, 0, 6);
  if (nodeIdHex == nullptr) return;
  char buf[13];
  strncpy(buf, nodeIdHex, 12);
  buf[12] = '\0';
  for (int i = 0; i < 6; i++) {
    char byteStr[3] = { buf[i * 2], buf[i * 2 + 1], 0 };
    outMac[i] = (uint8_t)strtoul(byteStr, nullptr, 16);
  }
}

static bool alreadySeen(uint32_t msgId, const uint8_t origin[6]) {
  for (int i = 0; i < MAX_SEEN_MSG; i++) {
    if (gSeen[i].used && gSeen[i].msgId == msgId && macEqual(gSeen[i].origin, origin)) {
      return true;
    }
  }
  return false;
}

static void rememberSeen(uint32_t msgId, const uint8_t origin[6]) {
  int slot = -1;
  uint32_t oldest = UINT32_MAX;
  for (int i = 0; i < MAX_SEEN_MSG; i++) {
    if (!gSeen[i].used) { slot = i; break; }
    if (gSeen[i].seenAt < oldest) {
      oldest = gSeen[i].seenAt;
      slot = i;
    }
  }
  if (slot < 0) return;
  gSeen[slot].used = true;
  gSeen[slot].msgId = msgId;
  memcpy(gSeen[slot].origin, origin, 6);
  gSeen[slot].seenAt = millis();
}

static bool wasDelivered(uint32_t msgId, const uint8_t origin[6]) {
  for (int i = 0; i < MAX_DELIVERED; i++) {
    if (gDelivered[i].used && gDelivered[i].msgId == msgId &&
        macEqual(gDelivered[i].origin, origin)) {
      return true;
    }
  }
  return false;
}

static void markDelivered(uint32_t msgId, const uint8_t origin[6]) {
  for (int i = 0; i < MAX_DELIVERED; i++) {
    if (gDelivered[i].used && gDelivered[i].msgId == msgId &&
        macEqual(gDelivered[i].origin, origin)) {
      return;
    }
  }
  for (int i = 0; i < MAX_DELIVERED; i++) {
    if (!gDelivered[i].used) {
      gDelivered[i].used = true;
      gDelivered[i].msgId = msgId;
      memcpy(gDelivered[i].origin, origin, 6);
      return;
    }
  }
  // overflow: overwrite slot 0
  gDelivered[0].used = true;
  gDelivered[0].msgId = msgId;
  memcpy(gDelivered[0].origin, origin, 6);
}

static void ensurePeer(const uint8_t mac[6]) {
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_add_peer(&peer);
  }
}

static void sendRaw(const uint8_t* mac, const uint8_t* data, size_t len) {
  ensurePeer(mac);
  esp_now_send(mac, data, len);
}

static size_t aadLen() {
  return offsetof(NexusPacket, payload);
}

/** TTL e lifeSec mudam no relay; não entram na autenticação. */
static void fillAad(const NexusPacket* pkt, NexusPacket* aadOut) {
  *aadOut = *pkt;
  aadOut->ttl = 0;
  aadOut->lifeSec = 0;
}

static bool isBroadcastMac(const uint8_t mac[6]) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0xFF) return false;
  }
  return true;
}

static void floodPacket(const NexusPacket* pkt) {
  const size_t len = offsetof(NexusPacket, payload) + pkt->payloadLen;
  uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if ((pkt->type == PKT_TEXT || pkt->type == PKT_ACK) && !isBroadcastMac(pkt->dest)) {
    sendRaw(pkt->dest, reinterpret_cast<const uint8_t*>(pkt), len);
  }
  sendRaw(bcast, reinterpret_cast<const uint8_t*>(pkt), len);
}

static int storeFind(uint32_t msgId, const uint8_t origin[6]) {
  for (int i = 0; i < MAX_STORE_MSG; i++) {
    if (gStore[i].used && gStore[i].pkt.msgId == msgId &&
        macEqual(gStore[i].pkt.origin, origin)) {
      return i;
    }
  }
  return -1;
}

static void storeRemoveAt(int idx) {
  if (idx < 0 || idx >= MAX_STORE_MSG) return;
  gStore[idx].used = false;
}

static void storeRemove(uint32_t msgId, const uint8_t origin[6]) {
  int idx = storeFind(msgId, origin);
  if (idx >= 0) storeRemoveAt(idx);
}

static void storeUpsert(const NexusPacket* pkt, bool isOrigin) {
  if (pkt->type != PKT_TEXT) return;
  if (pkt->lifeSec == 0) return;

  int existing = storeFind(pkt->msgId, pkt->origin);
  uint32_t now = millis();
  uint32_t lifeMs = (uint32_t)pkt->lifeSec * 1000UL;
  if (lifeMs < 1000UL) lifeMs = 1000UL;

  if (existing >= 0) {
    // refresh packet body but keep earlier expiry if sooner
    uint32_t newExp = now + lifeMs;
    if (newExp < gStore[existing].expiresAt) {
      gStore[existing].expiresAt = newExp;
    }
    gStore[existing].pkt = *pkt;
    return;
  }

  int slot = -1;
  for (int i = 0; i < MAX_STORE_MSG; i++) {
    if (!gStore[i].used) { slot = i; break; }
  }
  if (slot < 0) {
    // drop oldest by expiry
    uint32_t soonest = UINT32_MAX;
    for (int i = 0; i < MAX_STORE_MSG; i++) {
      if (gStore[i].expiresAt < soonest) {
        soonest = gStore[i].expiresAt;
        slot = i;
      }
    }
  }
  if (slot < 0) return;

  gStore[slot].used = true;
  gStore[slot].isOrigin = isOrigin;
  gStore[slot].expiresAt = now + lifeMs;
  gStore[slot].lastSentAt = now;
  gStore[slot].pkt = *pkt;
}

static void sendDeliveryAck(const NexusPacket* textPkt) {
  NexusPacket ack{};
  ack.magic0 = NEXUS_MAGIC0;
  ack.magic1 = NEXUS_MAGIC1;
  ack.version = NEXUS_VERSION;
  ack.type = PKT_ACK;
  ack.ttl = DEFAULT_TTL;
  ack.lifeSec = 120; // ACKs não entram na store; campo informativo
  ack.msgId = textPkt->msgId;
  memcpy(ack.origin, credentialsGet().mac, 6);
  memcpy(ack.dest, textPkt->origin, 6);
  ack.payloadLen = 0;

  rememberSeen(ack.msgId, ack.origin);
  floodPacket(&ack);
  Serial.printf("[mesh] ACK msgId=%lu -> origin\n", (unsigned long)ack.msgId);
}

static void handleAckPacket(const NexusPacket* pkt) {
  // TEXT: origin=A dest=B msgId=X
  // ACK:  origin=B dest=A msgId=X  → limpa store (X, A)
  storeRemove(pkt->msgId, pkt->dest);

  const NodeCredentials& me = credentialsGet();
  if (macEqual(pkt->dest, me.mac)) {
    if (gOnAck) gOnAck(pkt->msgId);
  }

  if (pkt->ttl > 1) {
    NexusPacket relay = *pkt;
    relay.ttl = (uint8_t)(pkt->ttl - 1);
    floodPacket(&relay);
  }
}

static void deliverText(const NexusPacket* pkt) {
  char plain[CRYPTO_MAX_PLAIN + 1];
  size_t plainLen = 0;
  NexusPacket aadPkt;
  fillAad(pkt, &aadPkt);
  if (!cryptoOpen(
          pkt->origin,
          reinterpret_cast<const uint8_t*>(&aadPkt), aadLen(),
          pkt->payload, pkt->payloadLen,
          reinterpret_cast<uint8_t*>(plain), &plainLen)) {
    Serial.println("[mesh] decrypt failed — drop");
    return;
  }
  plain[plainLen] = '\0';

  const bool first = !wasDelivered(pkt->msgId, pkt->origin);
  if (first) {
    markDelivered(pkt->msgId, pkt->origin);
    if (gOnText) gOnText(pkt->origin, plain);
  }
  sendDeliveryAck(pkt);
}

static void handlePacket(const uint8_t* data, int len, int8_t rssi) {
  if (len < (int)offsetof(NexusPacket, payload)) return;
  const NexusPacket* pkt = reinterpret_cast<const NexusPacket*>(data);
  if (pkt->magic0 != NEXUS_MAGIC0 || pkt->magic1 != NEXUS_MAGIC1) return;
  if (pkt->version != NEXUS_VERSION) return;
  if (pkt->payloadLen > sizeof(pkt->payload)) return;
  if (len < (int)(offsetof(NexusPacket, payload) + pkt->payloadLen)) return;

  const NodeCredentials& me = credentialsGet();

  // Dedup: se já vimos, ainda assim ACK de TEXT para mim (limpa stores perdidas)
  if (alreadySeen(pkt->msgId, pkt->origin)) {
    if (pkt->type == PKT_TEXT && macEqual(pkt->dest, me.mac)) {
      sendDeliveryAck(pkt);
    }
    if (pkt->type == PKT_ACK) {
      storeRemove(pkt->msgId, pkt->dest);
      if (macEqual(pkt->dest, me.mac) && gOnAck) gOnAck(pkt->msgId);
    }
    return;
  }
  rememberSeen(pkt->msgId, pkt->origin);

  if (pkt->type == PKT_HELLO) {
    char name[24];
    memset(name, 0, sizeof(name));
    size_t nameLen = 0;
    while (nameLen < pkt->payloadLen && nameLen < 23 && pkt->payload[nameLen] != 0) {
      name[nameLen] = (char)pkt->payload[nameLen];
      nameLen++;
    }
    name[nameLen] = '\0';

    if (pkt->payloadLen >= nameLen + 1 + CRYPTO_PK_LEN) {
      const uint8_t* pk = pkt->payload + nameLen + 1;
      cryptoRememberPeer(pkt->origin, pk);
    }

    if (gOnHello) gOnHello(pkt->origin, name, rssi);

    // Destino de alguma msg na store reapareceu → forçar reenvio cedo
    for (int i = 0; i < MAX_STORE_MSG; i++) {
      if (gStore[i].used && macEqual(gStore[i].pkt.dest, pkt->origin)) {
        gStore[i].lastSentAt = 0;
      }
    }

    if (pkt->ttl > 1) {
      NexusPacket relay = *pkt;
      relay.ttl = (uint8_t)(pkt->ttl - 1);
      uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      sendRaw(bcast, reinterpret_cast<uint8_t*>(&relay),
              offsetof(NexusPacket, payload) + relay.payloadLen);
    }
    return;
  }

  if (pkt->type == PKT_ACK) {
    handleAckPacket(pkt);
    return;
  }

  if (pkt->type == PKT_TEXT) {
    if (macEqual(pkt->dest, me.mac)) {
      deliverText(pkt);
      return;
    }

    // Guardar e reencaminhar enquanto houver vida útil
    storeUpsert(pkt, false);

    if (pkt->ttl > 1) {
      NexusPacket relay = *pkt;
      relay.ttl = (uint8_t)(pkt->ttl - 1);
      floodPacket(&relay);
    }
  }
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  int8_t rssi = info ? info->rx_ctrl->rssi : 0;
  handlePacket(data, len, rssi);
}
#else
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  handlePacket(data, len, 0);
}
#endif

bool meshBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) {
    Serial.println("[mesh] esp_now_init failed");
    return false;
  }
  esp_now_register_recv_cb(onRecv);

  uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  ensurePeer(bcast);
  Serial.println("[mesh] ESP-NOW ready (Nexus v3 store+ACK)");
  return true;
}

void meshSetHandlers(MeshTextHandler onText, MeshHelloHandler onHello) {
  gOnText = onText;
  gOnHello = onHello;
}

void meshSetDeliveryHandlers(MeshAckHandler onAck, MeshExpiredHandler onExpired) {
  gOnAck = onAck;
  gOnExpired = onExpired;
}

void meshBroadcastHello(const char* displayName) {
  NexusPacket pkt{};
  pkt.magic0 = NEXUS_MAGIC0;
  pkt.magic1 = NEXUS_MAGIC1;
  pkt.version = NEXUS_VERSION;
  pkt.type = PKT_HELLO;
  pkt.ttl = 2;
  pkt.lifeSec = 0;
  pkt.msgId = gMsgCounter++;
  memcpy(pkt.origin, credentialsGet().mac, 6);
  memset(pkt.dest, 0xFF, 6);

  size_t n = strnlen(displayName, 23);
  if (n + 1 + CRYPTO_PK_LEN > sizeof(pkt.payload)) {
    n = sizeof(pkt.payload) - 1 - CRYPTO_PK_LEN;
  }
  memcpy(pkt.payload, displayName, n);
  pkt.payload[n] = 0;
  memcpy(pkt.payload + n + 1, cryptoPublicKey(), CRYPTO_PK_LEN);
  pkt.payloadLen = (uint8_t)(n + 1 + CRYPTO_PK_LEN);

  rememberSeen(pkt.msgId, pkt.origin);
  uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  sendRaw(bcast, reinterpret_cast<uint8_t*>(&pkt),
          offsetof(NexusPacket, payload) + pkt.payloadLen);
}

bool meshSendText(const uint8_t destMac[6], const char* text, uint32_t* outMsgId) {
  if (text == nullptr) return false;
  if (!cryptoHasPeerKey(destMac)) {
    Serial.println("[mesh] send aborted: no peer key");
    return false;
  }

  size_t n = strnlen(text, CRYPTO_MAX_PLAIN);
  if (n == 0) return false;

  NexusPacket pkt{};
  pkt.magic0 = NEXUS_MAGIC0;
  pkt.magic1 = NEXUS_MAGIC1;
  pkt.version = NEXUS_VERSION;
  pkt.type = PKT_TEXT;
  pkt.ttl = DEFAULT_TTL;
  pkt.lifeSec = MSG_LIFETIME_SEC;
  pkt.msgId = gMsgCounter++;
  memcpy(pkt.origin, credentialsGet().mac, 6);
  memcpy(pkt.dest, destMac, 6);

  size_t sealedLen = 0;
  pkt.payloadLen = (uint8_t)(CRYPTO_NONCE_LEN + n + CRYPTO_TAG_LEN);

  NexusPacket aadPkt;
  fillAad(&pkt, &aadPkt);

  if (!cryptoSeal(
          destMac,
          reinterpret_cast<const uint8_t*>(&aadPkt), aadLen(),
          reinterpret_cast<const uint8_t*>(text), n,
          pkt.payload, &sealedLen)) {
    return false;
  }
  pkt.payloadLen = (uint8_t)sealedLen;

  rememberSeen(pkt.msgId, pkt.origin);
  storeUpsert(&pkt, true);
  floodPacket(&pkt);

  if (outMsgId) *outMsgId = pkt.msgId;
  Serial.printf("[mesh] TEXT msgId=%lu life=%us\n",
                (unsigned long)pkt.msgId, (unsigned)pkt.lifeSec);
  return true;
}

static void storeFlush() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_STORE_MSG; i++) {
    if (!gStore[i].used) continue;

    if ((int32_t)(now - gStore[i].expiresAt) >= 0) {
      uint32_t expiredId = gStore[i].pkt.msgId;
      bool wasOrigin = gStore[i].isOrigin;
      storeRemoveAt(i);
      if (wasOrigin && gOnExpired) gOnExpired(expiredId);
      Serial.printf("[mesh] store expired msgId=%lu\n", (unsigned long)expiredId);
      continue;
    }

    if (now - gStore[i].lastSentAt < STORE_RETRY_MS) continue;

    uint32_t remainMs = gStore[i].expiresAt - now;
    uint16_t remainSec = (uint16_t)((remainMs + 999UL) / 1000UL);
    if (remainSec == 0) remainSec = 1;

    NexusPacket out = gStore[i].pkt;
    out.ttl = DEFAULT_TTL;
    out.lifeSec = remainSec;
    floodPacket(&out);
    gStore[i].lastSentAt = now;
    gStore[i].pkt.lifeSec = remainSec;
    Serial.printf("[mesh] store retry msgId=%lu rem=%us\n",
                  (unsigned long)out.msgId, (unsigned)remainSec);
  }
}

void meshLoop() {
  uint32_t now = millis();
  if (now - gLastHelloMs >= HELLO_INTERVAL_MS) {
    gLastHelloMs = now;
    meshBroadcastHello(credentialsGet().displayName);
  }
  if (now - gLastStoreFlushMs >= 1000) {
    gLastStoreFlushMs = now;
    storeFlush();
  }
}
