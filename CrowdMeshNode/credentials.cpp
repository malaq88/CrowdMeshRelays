#include "credentials.h"

#include <WiFi.h>
#include <esp_mac.h>

#include "crypto_util.h"

static Preferences prefs;
static NodeCredentials creds;

void credentialsFormatNodeId(const uint8_t mac[6], char out[13]) {
  snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void readStaMac(uint8_t out[6]) {
  if (esp_read_mac(out, ESP_MAC_WIFI_STA) == ESP_OK) {
    bool allZero = true;
    for (int i = 0; i < 6; i++) {
      if (out[i] != 0) { allZero = false; break; }
    }
    if (!allZero) return;
  }

  WiFi.mode(WIFI_STA);
  delay(50);
  WiFi.macAddress(out);
}

void credentialsBegin() {
  memset(&creds, 0, sizeof(creds));
  WiFi.mode(WIFI_STA);
  delay(50);
  readStaMac(creds.mac);
  credentialsFormatNodeId(creds.mac, creds.nodeId);

  prefs.begin("crowdmesh", false);
  String name = prefs.getString("name", "");
  if (name.length() == 0) {
    snprintf(creds.displayName, sizeof(creds.displayName), "Node-%02X%02X",
             creds.mac[4], creds.mac[5]);
  } else {
    strncpy(creds.displayName, name.c_str(), sizeof(creds.displayName) - 1);
  }

  if (!cryptoIdentityBegin()) {
    Serial.println("[cred] crypto identity failed");
  }

  creds.loaded = true;
  Serial.printf("[cred] mac=%02X:%02X:%02X:%02X:%02X:%02X nodeId=%s name=%s\n",
                creds.mac[0], creds.mac[1], creds.mac[2],
                creds.mac[3], creds.mac[4], creds.mac[5],
                creds.nodeId, creds.displayName);
}

NodeCredentials& credentialsGet() {
  return creds;
}

void credentialsSetDisplayName(const char* name) {
  if (name == nullptr || name[0] == '\0') return;
  strncpy(creds.displayName, name, sizeof(creds.displayName) - 1);
  creds.displayName[sizeof(creds.displayName) - 1] = '\0';
  credentialsSave();
}

void credentialsSave() {
  prefs.putString("name", creds.displayName);
}
