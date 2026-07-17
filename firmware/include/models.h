#pragma once

#include <Arduino.h>

struct SlotState {
  int amsId = 0;
  int slot = 0;
  bool empty = true;
  bool active = false;
  bool rfid = false;
  String amsKind = "ams2pro";
  String color = "#202020";
  String colorName;
  String manufacturer;
  String material;
  String nozzleMin;
  String nozzleMax;
  String remainingGrams;   // e.g. "142"
  String costRemaining;    // e.g. "1.20"
  String lastUsed;         // e.g. "3d ago" or date
  String backupBadge;      // e.g. "Backup B·3"
  int remainingPercent = -1;
};

struct PrinterState {
  String name = "H2C";
  String state = "offline";
  String jobName;
  String nozzle;
  String nozzleTarget;
  String nozzle2;
  String nozzle2Target;
  String bed;
  String bedTarget;
  String chamber;
  String filamentUsed;
  String remainingTime;
  String etaClock;
  String speedPreset;
  String fanPart;
  String fanAux;
  String fanChamber;
  String wifiRssi;
  String hms;
  String nextJob;
  String chamberLight;
  String door;
  String filamentSensor;
  String activeColor = "#202020";
  String activeMaterial;
  String activeLabel;
  int progress = -1;
  int layer = -1;
  int totalLayers = -1;
  int remainingSeconds = -1;
  int remainingMinutes = -1;
  int speedLevel = -1;
  int activeExtruder = 0;
};

struct AmsMetaState {
  int amsId = 0;
  String amsKind;
  String temperature;
  String humidity;
};
