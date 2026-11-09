/**
 * CrowdMesh BroadCast — ESP8266 relay (ESP-NOW) + OLED 0.96"
 *
 * Papel: reencaminha HELLO/TEXT/ACK e guarda TEXT opaco até ACK ou lifeSec.
 * Não cifra, não BLE.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <user_interface.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <string.h>
#include <stddef.h>

#include "config.h"

// ---------------------------------------------------------------------------
// Pacote idêntico ao CrowdMesh Node/CYD (Nexus v2)
// ---------------------------------------------------------------------------
struct NexusPacket {
  uint8_t magic0;
  uint8_t magic1;
  uint8_t version;
  uint8_t type;
  uint8_t ttl;
  uint16_t lifeSec;
  uint32_t msgId;
  uint8_t origin[6];
  uint8_t dest[6];
  uint8_t payloadLen;
  uint8_t payload[180];
} __attribute__((packed));

struct SeenEntry {
  uint32_t msgId;
  uint8_t origin[6];
  uint32_t seenAt;
  bool used;
};

struct StoreEntry {
  bool used;
  uint32_t expiresAt;
  uint32_t lastSentAt;
  NexusPacket pkt;
};

static SeenEntry gSeen[MAX_SEEN_MSG];
static StoreEntry gStore[MAX_STORE_MSG];
static uint32_t gMsgCounter = 1;
static uint32_t gLastHelloMs = 0;
static uint32_t gLastUiMs = 0;
static uint32_t gLastStatusMs = 0;
static uint32_t gLastStoreFlushMs = 0;

static uint32_t gRelayCount = 0;
static uint32_t gDropDup = 0;
static uint32_t gDropBad = 0;
static uint32_t gStoreCount = 0;
static int8_t gLastRssi = 0;
static char gLastFrom[13] = "-";
static char gLastType[8] = "-";
static uint32_t gLastActivityMs = 0;

static uint8_t gMac[6];
static char gNodeId[13];
static char gDisplayName[24];

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool gBcastPeerAdded = false;

// Track a few unicast peers we already added (ESP8266 has no is_peer_exist).
static uint8_t gPeerMacs[8][6];
static uint8_t gPeerCount = 0;

// ---------------------------------------------------------------------------
// OLED (padrão openchain)
// ---------------------------------------------------------------------------
static Adafruit_SSD1306* oledDisp = nullptr;
static bool s_oledOk = false;

static bool macEqual(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

static void formatNodeId(const uint8_t mac[6], char out[13]) {
  snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void oledInit() {
  s_oledOk = false;
  if (oledDisp == nullptr) {
    oledDisp = new Adafruit_SSD1306(OLED_SCREEN_WIDTH, OLED_SCREEN_HEIGHT, &Wire, OLED_RESET);
  }
  if (oledDisp == nullptr) {
    Serial.println(F("[OLED] memoria (new) falhou"));
    return;
  }

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!oledDisp->begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("[OLED] begin falhou. Fios? Tente SCREEN_ADDRESS 0x3D."));
    return;
  }

  s_oledOk = true;
  oledDisp->clearDisplay();
  oledDisp->setTextSize(1);
  oledDisp->setTextColor(SSD1306_WHITE);
  oledDisp->setCursor(0, 0);
  oledDisp->println(F("CrowdMesh"));
  oledDisp->println(F("Relay OLED OK"));
  oledDisp->display();
}

static void oledShowBoot(const char* line2) {
  if (!s_oledOk || oledDisp == nullptr) return;
  oledDisp->clearDisplay();
  oledDisp->setCursor(0, 0);
  oledDisp->println(F("CrowdMesh Relay"));
  if (line2) oledDisp->println(line2);
  oledDisp->display();
}

static void oledRefresh() {
  if (!s_oledOk || oledDisp == nullptr) return;

  char line[22];
  oledDisp->clearDisplay();
  oledDisp->setCursor(0, 0);
  oledDisp->println(F("CrowdMesh Relay"));

  snprintf(line, sizeof(line), "ID %s", gNodeId + 6); // últimos 6 hex
  oledDisp->println(line);

  snprintf(line, sizeof(line), "ch %u  rssi %d", (unsigned)WIFI_CHANNEL, (int)gLastRssi);
  oledDisp->println(line);

  snprintf(line, sizeof(line), "fwd %lu  dup %lu",
           (unsigned long)gRelayCount, (unsigned long)gDropDup);
  oledDisp->println(line);

  snprintf(line, sizeof(line), "last %s %s", gLastType, gLastFrom);
  oledDisp->println(line);

  if (gLastActivityMs == 0) {
    oledDisp->println(F("aguardando mesh"));
  } else {
    unsigned long age = (millis() - gLastActivityMs) / 1000UL;
    snprintf(line, sizeof(line), "ativo ha %lus", age);
    oledDisp->println(line);
  }

  oledDisp->display();
}

// ---------------------------------------------------------------------------
// Seen-cache / ESP-NOW
// ---------------------------------------------------------------------------
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
  uint32_t oldest = 0xFFFFFFFFUL;
  for (int i = 0; i < MAX_SEEN_MSG; i++) {
    if (!gSeen[i].used) {
      slot = i;
      break;
    }
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

static bool peerKnown(const uint8_t* mac) {
  if (macEqual(mac, BCAST)) return gBcastPeerAdded;
  for (uint8_t i = 0; i < gPeerCount; i++) {
    if (macEqual(gPeerMacs[i], mac)) return true;
  }
  return false;
}

static void rememberPeerMac(const uint8_t* mac) {
  if (macEqual(mac, BCAST)) {
    gBcastPeerAdded = true;
    return;
  }
  if (peerKnown(mac)) return;
  if (gPeerCount < 8) {
    memcpy(gPeerMacs[gPeerCount], mac, 6);
    gPeerCount++;
  } else {
    // overwrite slot 0
    memcpy(gPeerMacs[0], mac, 6);
  }
}

static void ensurePeer(const uint8_t* mac) {
  if (peerKnown(mac)) return;
  // ESP8266 API: add_peer(mac, role, channel, key, key_len)
  int rc = esp_now_add_peer(
      const_cast<uint8_t*>(mac),
      ESP_NOW_ROLE_COMBO,
      WIFI_CHANNEL,
      nullptr,
      0);
  if (rc == 0) {
    rememberPeerMac(mac);
  } else {
    Serial.printf("[mesh] add_peer falhou rc=%d\n", rc);
  }
}

static void sendRaw(const uint8_t* mac, const uint8_t* data, size_t len) {
  if (len == 0 || len > 250) return;
  ensurePeer(mac);
  esp_now_send(const_cast<uint8_t*>(mac), const_cast<uint8_t*>(data), (uint8_t)len);
}

static void noteActivity(const NexusPacket* pkt, int8_t rssi) {
  gLastRssi = rssi;
  gLastActivityMs = millis();
  formatNodeId(pkt->origin, gLastFrom);
  if (pkt->type == PKT_HELLO) {
    strncpy(gLastType, "HELLO", sizeof(gLastType) - 1);
  } else if (pkt->type == PKT_TEXT) {
    strncpy(gLastType, "TEXT", sizeof(gLastType) - 1);
  } else if (pkt->type == PKT_ACK) {
    strncpy(gLastType, "ACK", sizeof(gLastType) - 1);
  } else {
    strncpy(gLastType, "PKT", sizeof(gLastType) - 1);
  }
  gLastType[sizeof(gLastType) - 1] = '\0';
}

static bool isBroadcastMac(const uint8_t mac[6]) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0xFF) return false;
  }
  return true;
}

static void floodPacket(const NexusPacket* pkt) {
  const size_t len = offsetof(NexusPacket, payload) + pkt->payloadLen;
  if ((pkt->type == PKT_TEXT || pkt->type == PKT_ACK) && !isBroadcastMac(pkt->dest)) {
    sendRaw(pkt->dest, reinterpret_cast<const uint8_t*>(pkt), len);
  }
  sendRaw(BCAST, reinterpret_cast<const uint8_t*>(pkt), len);
  gRelayCount++;
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

static void storeRemove(uint32_t msgId, const uint8_t origin[6]) {
  int idx = storeFind(msgId, origin);
  if (idx >= 0) {
    gStore[idx].used = false;
    if (gStoreCount > 0) gStoreCount--;
  }
}

static void storeUpsert(const NexusPacket* pkt) {
  if (pkt->type != PKT_TEXT || pkt->lifeSec == 0) return;
  uint32_t now = millis();
  uint32_t lifeMs = (uint32_t)pkt->lifeSec * 1000UL;
  if (lifeMs < 1000UL) lifeMs = 1000UL;

  int existing = storeFind(pkt->msgId, pkt->origin);
  if (existing >= 0) {
    uint32_t newExp = now + lifeMs;
    if (newExp < gStore[existing].expiresAt) gStore[existing].expiresAt = newExp;
    gStore[existing].pkt = *pkt;
    return;
  }

  int slot = -1;
  for (int i = 0; i < MAX_STORE_MSG; i++) {
    if (!gStore[i].used) { slot = i; break; }
  }
  if (slot < 0) {
    uint32_t soonest = 0xFFFFFFFFUL;
    for (int i = 0; i < MAX_STORE_MSG; i++) {
      if (gStore[i].expiresAt < soonest) {
        soonest = gStore[i].expiresAt;
        slot = i;
      }
    }
  }
  if (slot < 0) return;

  if (!gStore[slot].used) gStoreCount++;
  gStore[slot].used = true;
  gStore[slot].expiresAt = now + lifeMs;
  gStore[slot].lastSentAt = now;
  gStore[slot].pkt = *pkt;
}

static void storeFlush() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_STORE_MSG; i++) {
    if (!gStore[i].used) continue;
    if ((int32_t)(now - gStore[i].expiresAt) >= 0) {
      gStore[i].used = false;
      if (gStoreCount > 0) gStoreCount--;
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
  }
}

static void handlePacket(const uint8_t* data, int len, int8_t rssi) {
  if (len < (int)offsetof(NexusPacket, payload)) {
    gDropBad++;
    return;
  }

  const NexusPacket* pkt = reinterpret_cast<const NexusPacket*>(data);
  if (pkt->magic0 != NEXUS_MAGIC0 || pkt->magic1 != NEXUS_MAGIC1) {
    gDropBad++;
    return;
  }
  if (pkt->version != NEXUS_VERSION) {
    gDropBad++;
    return;
  }
  if (pkt->payloadLen > sizeof(pkt->payload)) {
    gDropBad++;
    return;
  }
  if (len < (int)(offsetof(NexusPacket, payload) + pkt->payloadLen)) {
    gDropBad++;
    return;
  }

  if (macEqual(pkt->origin, gMac)) return;

  if (alreadySeen(pkt->msgId, pkt->origin)) {
    if (pkt->type == PKT_ACK) {
      storeRemove(pkt->msgId, pkt->dest);
    }
    gDropDup++;
    return;
  }
  rememberSeen(pkt->msgId, pkt->origin);
  noteActivity(pkt, rssi);

  if (pkt->type == PKT_ACK) {
    storeRemove(pkt->msgId, pkt->dest);
    if (pkt->ttl > 1) {
      NexusPacket relay = *pkt;
      relay.ttl = (uint8_t)(pkt->ttl - 1);
      floodPacket(&relay);
    }
    return;
  }

  if (pkt->type == PKT_HELLO) {
    for (int i = 0; i < MAX_STORE_MSG; i++) {
      if (gStore[i].used && macEqual(gStore[i].pkt.dest, pkt->origin)) {
        gStore[i].lastSentAt = 0;
      }
    }
    if (pkt->ttl > 1) {
      NexusPacket relay = *pkt;
      relay.ttl = (uint8_t)(pkt->ttl - 1);
      floodPacket(&relay);
    }
    return;
  }

  if (pkt->type == PKT_TEXT) {
    Serial.printf("[relay] TEXT ttl=%u life=%u from=%s\n",
                  pkt->ttl, pkt->lifeSec, gLastFrom);
    storeUpsert(pkt);
    if (pkt->ttl > 1) {
      NexusPacket relay = *pkt;
      relay.ttl = (uint8_t)(pkt->ttl - 1);
      floodPacket(&relay);
    }
  }
}

// ESP8266 recv cb: (mac, data, len) — RSSI via wifi_get_rssi não é por pacote;
// usamos 0 ou last known. Algumas builds expõem info extra; mantemos simples.
static void onDataRecv(uint8_t* mac, uint8_t* data, uint8_t len) {
  (void)mac;
  int8_t rssi = 0;
  // wifi_station_get_connect_status não ajuda em ESP-NOW puro;
  // muitos firmwares deixam rssi=0 no relay.
  handlePacket(data, (int)len, rssi);
}

static void broadcastHello() {
  NexusPacket pkt{};
  pkt.magic0 = NEXUS_MAGIC0;
  pkt.magic1 = NEXUS_MAGIC1;
  pkt.version = NEXUS_VERSION;
  pkt.type = PKT_HELLO;
  pkt.ttl = 2;
  pkt.lifeSec = 0;
  pkt.msgId = gMsgCounter++;
  memcpy(pkt.origin, gMac, 6);
  memset(pkt.dest, 0xFF, 6);

  size_t n = strnlen(gDisplayName, 23);
  memcpy(pkt.payload, gDisplayName, n);
  pkt.payloadLen = (uint8_t)n;

  rememberSeen(pkt.msgId, pkt.origin);
  sendRaw(BCAST, reinterpret_cast<const uint8_t*>(&pkt),
          offsetof(NexusPacket, payload) + pkt.payloadLen);
}

static bool meshBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);

  // Canal fixo para falar com ESP32
  wifi_set_channel(WIFI_CHANNEL);

  if (esp_now_init() != 0) {
    Serial.println(F("[mesh] esp_now_init falhou"));
    return false;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onDataRecv);
  ensurePeer(BCAST);

  Serial.printf("[mesh] ESP-NOW relay pronto  ch=%u\n", (unsigned)WIFI_CHANNEL);
  return true;
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== CrowdMesh BroadCast (ESP8266 Relay) ==="));

  memset(gSeen, 0, sizeof(gSeen));
  memset(gStore, 0, sizeof(gStore));

  WiFi.mode(WIFI_STA);
  WiFi.macAddress(gMac);
  formatNodeId(gMac, gNodeId);
  snprintf(gDisplayName, sizeof(gDisplayName), "Relay-%02X%02X", gMac[4], gMac[5]);

  Serial.printf(" NODE ID : %s\n", gNodeId);
  Serial.printf(" NAME    : %s\n", gDisplayName);
  Serial.printf(" MAC     : %02X:%02X:%02X:%02X:%02X:%02X\n",
                gMac[0], gMac[1], gMac[2], gMac[3], gMac[4], gMac[5]);

  oledInit();
  oledShowBoot("ESP-NOW...");

  if (!meshBegin()) {
    oledShowBoot("ESP-NOW ERRO");
    while (true) delay(1000);
  }

  broadcastHello();
  gLastHelloMs = millis();
  oledRefresh();
  Serial.println(F("[ready] store-and-forward CrowdMesh v3"));
}

void loop() {
  const uint32_t now = millis();

  if (now - gLastHelloMs >= HELLO_INTERVAL_MS) {
    gLastHelloMs = now;
    broadcastHello();
  }

  if (now - gLastStoreFlushMs >= 1000) {
    gLastStoreFlushMs = now;
    storeFlush();
  }

  if (now - gLastUiMs >= UI_REFRESH_MS) {
    gLastUiMs = now;
    oledRefresh();
  }

  if (now - gLastStatusMs >= STATUS_LOG_MS) {
    gLastStatusMs = now;
    Serial.printf("[stat] fwd=%lu dup=%lu bad=%lu store=%lu last=%s/%s\n",
                  (unsigned long)gRelayCount,
                  (unsigned long)gDropDup,
                  (unsigned long)gDropBad,
                  (unsigned long)gStoreCount,
                  gLastType,
                  gLastFrom);
  }
}
