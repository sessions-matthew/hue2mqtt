#include "BleManager.hpp"
#include "HueDevice.hpp"
#include <syslog.h>
#include <unistd.h>

static const char *adapter = "org.bluez.Adapter1";
static const char *property = "Powered";

BleManager bleManager;

BleManager::BleManager() {}

DBusConnection *BleManager::getConn() {
  DBusError dbus_error;
  dbus_error_init(&dbus_error);
  int err = 0;
  if (this->conn == nullptr)
    err = 1;
  // if (err == 0) if (!dbus_connection_get_is_connected(this->conn)) err = 1;
  if (err) {
    syslog(LOG_DEBUG, "DBUS is not connected, connecting");
    this->conn = dbus_bus_get(DBUS_BUS_SYSTEM, &dbus_error);
    if (this->conn == nullptr) {
      syslog(LOG_DEBUG, "DBUS error at %d %s %s", __LINE__, dbus_error.name,
             dbus_error.message);
    }
  }
  usleep(100);
  return this->conn;
}

int BleManager::ble_power_check() {
  if (!ble_power_get())
    return ble_power_set(1);
  return 0;
}

int BleManager::ble_power_get() {
  DBusMessage *dbus_msg = nullptr, *dbus_reply = nullptr;
  DBusMessageIter iter0, iter1, iter2;
  DBusError dbus_error;
  dbus_bool_t dbus_bool = FALSE;
  auto connPtr = this->getConn();

  if (connPtr == nullptr) {
    syslog(LOG_DEBUG, "DBUS connection is null");
    return 0;
  }

  ::dbus_error_init(&dbus_error);
  dbus_msg = ::dbus_message_new_method_call(
      "org.bluez", "/org/bluez/hci0", "org.freedesktop.DBus.Properties", "Get");
  if (dbus_msg != nullptr) {
    ::dbus_message_iter_init_append(dbus_msg, &iter0);
    ::dbus_message_iter_append_basic(&iter0, DBUS_TYPE_STRING, &adapter);
    ::dbus_message_iter_append_basic(&iter0, DBUS_TYPE_STRING, &property);
    dbus_reply = ::dbus_connection_send_with_reply_and_block(
        connPtr, dbus_msg, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
    if (dbus_reply != nullptr) {
      dbus_message_iter_init(dbus_reply, &iter0);
      dbus_message_iter_recurse(&iter0, &iter1);
      dbus_message_iter_get_basic(&iter1, &dbus_bool);
    }
    dbus_message_unref(dbus_msg);
    if (dbus_reply != nullptr)
      dbus_message_unref(dbus_reply);
  }
  return dbus_bool;
}

int BleManager::ble_power_set(int state) {
  DBusMessage *dbus_msg = nullptr, *dbus_reply = nullptr;
  DBusMessageIter iter0, iter1, iter2;
  DBusError dbus_error;
  dbus_bool_t dbus_bool = state;
  auto connPtr = this->getConn();

  if (connPtr == nullptr) {
    syslog(LOG_DEBUG, "DBUS connection is null");
    return -1;
  }

  ::dbus_error_init(&dbus_error);
  dbus_msg = ::dbus_message_new_method_call(
      "org.bluez", "/org/bluez/hci0", "org.freedesktop.DBus.Properties", "Set");

  if (dbus_msg != nullptr) {
    ::dbus_message_iter_init_append(dbus_msg, &iter0);
    ::dbus_message_iter_append_basic(&iter0, DBUS_TYPE_STRING, &adapter);
    ::dbus_message_iter_append_basic(&iter0, DBUS_TYPE_STRING, &property);
    ::dbus_message_iter_open_container(&iter0, DBUS_TYPE_VARIANT, "b", &iter1);
    ::dbus_message_iter_append_basic(&iter1, DBUS_TYPE_BOOLEAN, &dbus_bool);
    ::dbus_message_iter_close_container(&iter0, &iter1);

    dbus_reply = ::dbus_connection_send_with_reply_and_block(
        connPtr, dbus_msg, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
    dbus_message_unref(dbus_msg);
    if (dbus_reply != nullptr)
      dbus_message_unref(dbus_reply);
  }
  return 0;
}
