#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "models.h"

using SlotUpdateCallback = void (*)(const SlotState &slot);
using PrinterUpdateCallback = void (*)(const PrinterState &printer);
using MetaUpdateCallback = void (*)(const AmsMetaState &meta);
using StatusUpdateCallback = void (*)(const String &message);

class DisplayMqtt {
 public:
  void begin();
  void loop();
  void setSlotCallback(SlotUpdateCallback callback);
  void setPrinterCallback(PrinterUpdateCallback callback);
  void setMetaCallback(MetaUpdateCallback callback);
  void setStatusCallback(StatusUpdateCallback callback);

 private:
  WiFiClient wifiClient_;
  PubSubClient client_{wifiClient_};
  unsigned long lastReconnectAttempt_ = 0;
  SlotUpdateCallback slotCallback_ = nullptr;
  PrinterUpdateCallback printerCallback_ = nullptr;
  MetaUpdateCallback metaCallback_ = nullptr;
  StatusUpdateCallback statusCallback_ = nullptr;

  void reconnect();
  void subscribe();
  void handleMessage(char *topic, byte *payload, unsigned int length);
  void publishStatus(const String &message);
};

extern DisplayMqtt displayMqtt;
