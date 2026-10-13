#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "models.h"

using SlotUpdateCallback = void (*)(const SlotState &slot);
using PrinterUpdateCallback = void (*)(const PrinterState &printer);
using MetaUpdateCallback = void (*)(const AmsMetaState &meta);
using StatusUpdateCallback = void (*)(const String &message);
using BridgeUpdateCallback = void (*)(const BridgeHealth &health);
using AlertsUpdateCallback = void (*)(const AlertItem *alerts, int count);
using OverviewUpdateCallback = void (*)(const OverviewUnit *units, int count);
using PrintersUpdateCallback = void (*)(const PrinterOption *printers, int count, const String &activeId);

class DisplayMqtt {
 public:
  void begin();
  void loop();
  void setSlotCallback(SlotUpdateCallback callback);
  void setPrinterCallback(PrinterUpdateCallback callback);
  void setMetaCallback(MetaUpdateCallback callback);
  void setStatusCallback(StatusUpdateCallback callback);
  void setBridgeCallback(BridgeUpdateCallback callback);
  void setAlertsCallback(AlertsUpdateCallback callback);
  void setOverviewCallback(OverviewUpdateCallback callback);
  void setPrintersCallback(PrintersUpdateCallback callback);

  bool publishDiag(const String &body);
  // MQTT fallback: which printer to monitor (retained on <prefix>/ctrl/printer).
  bool publishMonitoredPrinter(const String &id);
  bool isConnected() { return client_.connected(); }
  // True if the last loop() call handled an inbound MQTT message (or reconnect).
  bool tookWork() const { return tookWork_; }

 private:
  WiFiClient wifiClient_;
  PubSubClient client_{wifiClient_};
  unsigned long lastReconnectAttempt_ = 0;
  SlotUpdateCallback slotCallback_ = nullptr;
  PrinterUpdateCallback printerCallback_ = nullptr;
  MetaUpdateCallback metaCallback_ = nullptr;
  StatusUpdateCallback statusCallback_ = nullptr;
  BridgeUpdateCallback bridgeCallback_ = nullptr;
  AlertsUpdateCallback alertsCallback_ = nullptr;
  OverviewUpdateCallback overviewCallback_ = nullptr;
  PrintersUpdateCallback printersCallback_ = nullptr;
  bool tookWork_ = false;
  String base_;  // topic prefix, cached from config() in begin()

  void reconnect();
  void subscribe();
  void handleMessage(char *topic, byte *payload, unsigned int length);
  void publishStatus(const String &message);
};

extern DisplayMqtt displayMqtt;
