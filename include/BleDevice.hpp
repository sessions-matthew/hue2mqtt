#pragma once
#include <BleManager.hpp>
#include <cstdint>
#include <dbus/dbus.h>
#include <string>

// Generic BLE device class for use with Bluez DBus
class BleDevice {
  DBusMessage *dbus_msg = nullptr, *dbus_reply = nullptr;
  DBusMessageIter iter0, iter1, iter2;
  DBusError dbus_error;

protected:
public:
  std::string devicePath;
  std::string mac;

  BleDevice(std::string m);

  std::string &deviceMacReplace(std::string &mac);
  std::string dbusPathFromMac(std::string &mac);

  int device_connected_get();
  int device_connected_set(uint8_t level);
  int device_connect_check();

  int gatt_read_char_byte(std::string service, std::string characteristic);
  int gatt_write_char_byte(std::string service, std::string characteristic,
                           uint8_t byte);
  int gatt_notify_char(std::string service, std::string characteristic);
};
