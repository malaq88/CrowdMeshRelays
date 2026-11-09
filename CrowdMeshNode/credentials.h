#pragma once

#include <Arduino.h>
#include <Preferences.h>

struct NodeCredentials {
  uint8_t mac[6];
  char nodeId[13];
  char displayName[24];
  bool loaded;
};

void credentialsBegin();
NodeCredentials& credentialsGet();
void credentialsSetDisplayName(const char* name);
void credentialsSave();
void credentialsFormatNodeId(const uint8_t mac[6], char out[13]);
