#include "ble_bridge.h"

#include <NimBLEDevice.h>
#include "config.h"

#ifndef BLE_OUTBOX_MAX
#define BLE_OUTBOX_MAX 24
#endif
#ifndef BLE_OUTBOX_LINE_MAX
#define BLE_OUTBOX_LINE_MAX 220
#endif

static NimBLEServer* gServer = nullptr;
static NimBLECharacteristic* gTx = nullptr;
static NimBLECharacteristic* gRx = nullptr;
static NimBLECharacteristic* gId = nullptr;
static bool gConnected = false;
static bool gSubscribed = false;
static BleCommandHandler gOnCommand = nullptr;
static BleConnHandler gOnConn = nullptr;
static BleSubscribedHandler gOnSubscribed = nullptr;
static String gLineBuf;

// Fila circular de linhas para o Companion (MSG etc. enquanto BLE não está pronto)
static char gOutbox[BLE_OUTBOX_MAX][BLE_OUTBOX_LINE_MAX];
static uint8_t gOutboxHead = 0;
static uint8_t gOutboxTail = 0;
static uint8_t gOutboxCount = 0;
static uint32_t gLastFlushMs = 0;

static bool canNotifyPhone() {
  return gConnected && gSubscribed && gTx != nullptr;
}

static bool outboxPush(const String& line) {
  if (line.length() == 0) return false;
  if (gOutboxCount >= BLE_OUTBOX_MAX) {
    // Descarta a mais antiga para abrir espaço
    gOutboxHead = (uint8_t)((gOutboxHead + 1) % BLE_OUTBOX_MAX);
    gOutboxCount--;
    Serial.println(F("[ble] outbox cheia — drop oldest"));
  }
  size_t n = line.length();
  if (n >= BLE_OUTBOX_LINE_MAX) n = BLE_OUTBOX_LINE_MAX - 1;
  memcpy(gOutbox[gOutboxTail], line.c_str(), n);
  gOutbox[gOutboxTail][n] = '\0';
  gOutboxTail = (uint8_t)((gOutboxTail + 1) % BLE_OUTBOX_MAX);
  gOutboxCount++;
  Serial.printf("[ble] queued (%u): %.40s%s\n",
                (unsigned)gOutboxCount,
                line.c_str(),
                line.length() > 40 ? "…" : "");
  return true;
}

static bool outboxPeek(char* out, size_t outLen) {
  if (gOutboxCount == 0 || outLen == 0) return false;
  strncpy(out, gOutbox[gOutboxHead], outLen - 1);
  out[outLen - 1] = '\0';
  return true;
}

static void outboxPop() {
  if (gOutboxCount == 0) return;
  gOutboxHead = (uint8_t)((gOutboxHead + 1) % BLE_OUTBOX_MAX);
  gOutboxCount--;
}

static bool sendNotifyRaw(const char* payload) {
  if (!canNotifyPhone() || payload == nullptr) return false;
  gTx->setValue(payload);
  gTx->notify();
  return true;
}

static void flushOutboxBurst(uint8_t maxLines) {
  uint8_t sent = 0;
  char buf[BLE_OUTBOX_LINE_MAX];
  while (sent < maxLines && gOutboxCount > 0 && canNotifyPhone()) {
    if (!outboxPeek(buf, sizeof(buf))) break;
    if (!sendNotifyRaw(buf)) break;
    outboxPop();
    sent++;
    delay(8); // evita flood no ATT
  }
  if (sent > 0) {
    Serial.printf("[ble] flushed %u line(s), left=%u\n",
                  (unsigned)sent, (unsigned)gOutboxCount);
  }
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    (void)server;
    (void)connInfo;
    gConnected = true;
    gSubscribed = false;
    if (gOnConn) gOnConn(true);
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    (void)server;
    (void)connInfo;
    (void)reason;
    gConnected = false;
    gSubscribed = false;
    if (gOnConn) gOnConn(false);
    NimBLEDevice::startAdvertising();
  }
};

class TxCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* characteristic,
                   NimBLEConnInfo& connInfo,
                   uint16_t subValue) override {
    (void)characteristic;
    (void)connInfo;
    const bool notifyEnabled = (subValue & 0x01) != 0;
    gSubscribed = notifyEnabled;
    if (notifyEnabled) {
      flushOutboxBurst(BLE_OUTBOX_MAX);
      if (gOnSubscribed) gOnSubscribed();
    }
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    (void)connInfo;
    std::string raw = characteristic->getValue();
    if (raw.empty()) return;
    gLineBuf += String(raw.c_str());
    int idx;
    while ((idx = gLineBuf.indexOf('\n')) >= 0) {
      String line = gLineBuf.substring(0, idx);
      gLineBuf.remove(0, idx + 1);
      line.trim();
      if (line.length() > 0 && gOnCommand) {
        gOnCommand(line);
      }
    }
  }
};

bool bleBridgeBegin(const char* deviceName) {
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);

  gServer = NimBLEDevice::createServer();
  gServer->setCallbacks(new ServerCallbacks());

  NimBLEService* service = gServer->createService(BLE_SERVICE_UUID);

  gTx = service->createCharacteristic(
      BLE_CHARACTERISTIC_TX_UUID,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  gTx->setCallbacks(new TxCallbacks());

  gRx = service->createCharacteristic(
      BLE_CHARACTERISTIC_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  gRx->setCallbacks(new RxCallbacks());

  gId = service->createCharacteristic(
      BLE_CHARACTERISTIC_ID_UUID,
      NIMBLE_PROPERTY::READ);
  gId->setValue("000000000000|unknown");

  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(deviceName);
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->enableScanResponse(true);
  advertising->start();

  Serial.printf("[ble] NimBLE advertising as %s\n", deviceName);
  return true;
}

void bleBridgeSetHandlers(
    BleCommandHandler onCommand,
    BleConnHandler onConn,
    BleSubscribedHandler onSubscribed
) {
  gOnCommand = onCommand;
  gOnConn = onConn;
  gOnSubscribed = onSubscribed;
}

void bleBridgeUpdateIdentity(const char* nodeId, const char* displayName, const char* pkHex) {
  if (gId == nullptr || nodeId == nullptr) return;
  String value = String(nodeId);
  value += "|";
  value += (displayName && displayName[0]) ? displayName : nodeId;
  if (pkHex != nullptr && pkHex[0] != '\0') {
    value += "|";
    value += pkHex;
  }
  gId->setValue(value.c_str());
  Serial.printf("[ble] identity char = %s\n", value.c_str());
}

void bleBridgeNotify(const String& line) {
  if (gTx == nullptr) return;
  String payload = line;
  if (!payload.endsWith("\n")) payload += "\n";

  if (canNotifyPhone()) {
    // Tenta enviar; se falhar, ainda enfileira
    if (sendNotifyRaw(payload.c_str())) return;
  }
  outboxPush(payload);
}

bool bleBridgeIsConnected() { return gConnected; }
bool bleBridgeIsSubscribed() { return gSubscribed; }
uint8_t bleBridgeOutboxCount() { return gOutboxCount; }

void bleBridgeLoop() {
  if (!canNotifyPhone() || gOutboxCount == 0) return;
  uint32_t now = millis();
  if (now - gLastFlushMs < 50) return;
  gLastFlushMs = now;
  flushOutboxBurst(3);
}
