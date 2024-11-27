#include "BleDevice.hpp"
#include <iostream>
#include <syslog.h>

BleDevice::BleDevice(std::string m) : mac(m) {
  this->devicePath = dbusPathFromMac(m);
}

std::string &BleDevice::deviceMacReplace(std::string &mac) {
  for (auto &e : mac) {
    if (e == ':')
      e = '_';
  }
  return mac;
}

std::string BleDevice::dbusPathFromMac(std::string &mac) {
  return std::string("/org/bluez/hci0/dev_") + deviceMacReplace(mac);
}

int BleDevice::gatt_read_char_byte(std::string service,
                                   std::string characteristic) {
  uint8_t byte = 0;

  ::dbus_error_init(&dbus_error);
  dbus_msg = dbus_message_new_method_call(
      "org.bluez",
      (this->devicePath + "/service" + service + "/char" + characteristic)
          .c_str(),
      "org.bluez.GattCharacteristic1", "ReadValue");
  if (dbus_msg != nullptr) {
    dbus_message_iter_init_append(dbus_msg, &this->iter0);
    dbus_message_iter_open_container(&this->iter0, DBUS_TYPE_ARRAY, "{sv}",
                                     &this->iter1);
    dbus_message_iter_close_container(&this->iter0, &this->iter1);
    dbus_reply = ::dbus_connection_send_with_reply_and_block(
        bleManager.getConn(), this->dbus_msg, DBUS_TIMEOUT_USE_DEFAULT,
        &dbus_error);
    if (dbus_reply != nullptr) {
      dbus_message_iter_init(dbus_reply, &this->iter0);
      dbus_message_iter_recurse(&this->iter0, &this->iter1);
      dbus_message_iter_get_basic(&this->iter1, &byte);
      dbus_message_unref(dbus_reply);
    } else {
      ::perror(dbus_error.name);
      ::perror(dbus_error.message);
    }
    dbus_message_unref(dbus_msg);
  }
  return byte;
}

int BleDevice::gatt_write_char_byte(std::string service,
                                    std::string characteristic, uint8_t byte) {
  dbus_msg = ::dbus_message_new_method_call(
      "org.bluez",
      (this->devicePath + "/service" + service + "/char" + characteristic)
          .c_str(),
      "org.bluez.GattCharacteristic1", "WriteValue");
  if (dbus_msg != nullptr) {
    dbus_message_iter_init_append(dbus_msg, &this->iter0);
    dbus_message_iter_open_container(&this->iter0, DBUS_TYPE_ARRAY, "y",
                                     &this->iter1); // bytes
    dbus_message_iter_append_basic(&this->iter1, DBUS_TYPE_BYTE, &byte);
    dbus_message_iter_close_container(&this->iter0, &this->iter1);
    dbus_message_iter_open_container(&this->iter0, DBUS_TYPE_ARRAY, "{sv}",
                                     &this->iter1); // flags (empty)
    dbus_message_iter_close_container(&this->iter0, &this->iter1);
    dbus_connection_send(bleManager.getConn(), this->dbus_msg, NULL);
    dbus_message_unref(this->dbus_msg);
  }

  return 0;
}

int BleDevice::gatt_notify_char(std::string service,
                                std::string characteristic) {
  uint16_t byte = 0;

  ::dbus_error_init(&dbus_error);
  dbus_msg = dbus_message_new_method_call(
      "org.bluez",
      (this->devicePath + "/service" + service + "/char" + characteristic)
          .c_str(),
      "org.bluez.GattCharacteristic1", "AcquireNotify");
  if (dbus_msg != nullptr) {
    dbus_message_iter_init_append(dbus_msg, &this->iter0);
    dbus_message_iter_open_container(&this->iter0, DBUS_TYPE_ARRAY, "{sv}",
                                     &this->iter1);
    dbus_message_iter_close_container(&this->iter0, &this->iter1);
    dbus_reply = ::dbus_connection_send_with_reply_and_block(
        bleManager.getConn(), this->dbus_msg, DBUS_TIMEOUT_USE_DEFAULT,
        &dbus_error);

    if (dbus_reply != nullptr) {
      dbus_message_iter_init(dbus_reply, &this->iter0);
      dbus_message_iter_get_basic(&this->iter0, &byte);
      dbus_message_unref(dbus_reply);
    } else {
      ::perror(dbus_error.name);
      ::perror(dbus_error.message);
    }
    dbus_message_unref(dbus_msg);
  }
  return byte;
}

// PHILIPS_POWER_UUID = "932c32bd-0002-47a2-835a-a8d455b859dd"
// PHILIPS_LEVEL_UUID = "932c32bd-0003-47a2-835a-a8d455b859dd"

// busctl introspect org.bluez
// /org/bluez/hci0/dev_F3_0D_83_C4_62_B1/service002c/char0032

// dbus-send --session           \
//   --system                    \
//   --dest=org.bluez            \
//   --type=method_call          \
//   --print-reply               \
//   /org/bluez/hci0/dev_F3_0D_83_C4_62_B1/service002c/char002f    \
//   org.freedesktop.DBus.Properties.Get string:"org.bluez.GattCharacteristic1" string:"UUID"

int BleDevice::device_connected_get() {
  dbus_bool_t dbus_bool = FALSE;

  ::dbus_error_init(&dbus_error);
  dbus_msg =
      ::dbus_message_new_method_call("org.bluez", this->devicePath.c_str(),
                                     "org.freedesktop.DBus.Properties", "Get");
  if (dbus_msg != nullptr) {
    const char *device = "org.bluez.Device1";
    const char *connected = "Connected";
    ::dbus_message_iter_init_append(dbus_msg, &iter0);
    ::dbus_message_iter_append_basic(&iter0, DBUS_TYPE_STRING, &device);
    ::dbus_message_iter_append_basic(&iter0, DBUS_TYPE_STRING, &connected);
    dbus_reply = ::dbus_connection_send_with_reply_and_block(
        bleManager.getConn(), dbus_msg,
        2000, // 2 seconds
        &dbus_error);
    if (dbus_reply != nullptr) {
      dbus_message_iter_init(dbus_reply, &iter0);
      dbus_message_iter_recurse(&iter0, &iter2);
      dbus_message_iter_get_basic(&iter2, &dbus_bool);
      dbus_message_unref(dbus_reply);
    } else {
      syslog(LOG_DEBUG, "DBUS error at %d %s %s", __LINE__, dbus_error.name,
             dbus_error.message);
    }
    dbus_message_unref(dbus_msg);
  } else {
    syslog(LOG_DEBUG, "DBUS error at %d ", __LINE__);
  }

  return dbus_bool;
}

int BleDevice::device_connected_set(uint8_t level) {
  std::string method = level == 1 ? "Connect" : "Disconnect";
  ::dbus_error_init(&dbus_error);

  dbus_msg = dbus_message_new_method_call("org.bluez", this->devicePath.c_str(),
                                          "org.bluez.Device1", method.c_str());
  if (dbus_msg != nullptr) {
    dbus_reply = ::dbus_connection_send_with_reply_and_block(
        bleManager.getConn(), dbus_msg,
        2000, // 2 seconds
        &dbus_error);
    if (dbus_reply == nullptr) {
      // ::perror(dbus_error.name);
      // ::perror(dbus_error.message);
    } else {
      dbus_message_unref(dbus_reply);
    }
    dbus_message_unref(dbus_msg);
  } else {
    syslog(LOG_DEBUG, "DBUS error at %d ", __LINE__);
  }
  return 0;
}

int BleDevice::device_connect_check() {
  if (!this->device_connected_get()) {
    syslog(LOG_DEBUG, "%s is disconnected, reconnecting...", this->mac.c_str());
    this->device_connected_set(1);
  }
  return 0;
}
