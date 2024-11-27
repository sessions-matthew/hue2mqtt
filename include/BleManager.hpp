#pragma once
#include "BleDevice.hpp"
#include <dbus/dbus.h>
#include <map>
#include <string>

class BleManager {
  DBusConnection *conn = nullptr;

public:
  BleManager();

  DBusConnection *getConn();

  int ble_power_get();
  int ble_power_set(int state);
  int ble_power_check();
};

extern BleManager bleManager;
