#pragma once
#include <dbus/dbus.h>
#include <string>
#include <map>
#include "BleDevice.hpp"

class BleManager {
  DBusConnection *conn = nullptr;
public:
  BleManager ();

  DBusConnection *getConn();

  int ble_power_get();
  int ble_power_set(int state);
  int ble_power_check();
};

extern BleManager bleManager;
