#pragma once

// CrowdMesh BroadCast — ESP8266 + OLED 0.96" (relay ESP-NOW)
// Placa típica: ESP8266 V3 com display OLED 0.96" (Mercado Livre / NodeMCU-like)

#define DEVICE_NAME_PREFIX "CrowdMesh-Relay"

#ifndef WIFI_CHANNEL
#define WIFI_CHANNEL 1
#endif

#define MAX_SEEN_MSG 48
#define MAX_STORE_MSG 6
#define HELLO_INTERVAL_MS 8000
#define UI_REFRESH_MS 400
#define STATUS_LOG_MS 10000
#define STORE_RETRY_MS 15000
#define MSG_LIFETIME_SEC 1800

#define PKT_HELLO 0x01
#define PKT_TEXT  0x02
#define PKT_ACK   0x03

#define NEXUS_MAGIC0 'C'
#define NEXUS_MAGIC1 'M'
#define NEXUS_VERSION 3
#define DEFAULT_TTL 5

#define OLED_SCREEN_WIDTH 128
#define OLED_SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define OLED_SDA 14
#define OLED_SCL 12
