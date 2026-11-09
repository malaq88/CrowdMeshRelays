#pragma once

#include <Arduino.h>

typedef void (*BleCommandHandler)(const String& line);
typedef void (*BleConnHandler)(bool connected);
typedef void (*BleSubscribedHandler)();

bool bleBridgeBegin(const char* deviceName);
void bleBridgeLoop();
/** Envia agora se BLE estiver pronto; senão coloca na fila (ex.: MSG com app offline). */
void bleBridgeNotify(const String& line);
void bleBridgeUpdateIdentity(const char* nodeId, const char* displayName, const char* pkHex = nullptr);
void bleBridgeSetHandlers(
    BleCommandHandler onCommand,
    BleConnHandler onConn,
    BleSubscribedHandler onSubscribed = nullptr
);
bool bleBridgeIsConnected();
bool bleBridgeIsSubscribed();
/** Quantas linhas aguardando o Companion. */
uint8_t bleBridgeOutboxCount();
