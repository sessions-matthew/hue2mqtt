#include "HueDevice.hpp"
#include <iostream>

HueDevice::HueDevice(std::string mac)
    : BleDevice(mac), power(this), brightness(this) {}

int HueDevice::light_power_get() {
  device_connect_check();
  return this->gatt_read_char_byte("002c", "002f");
}

int HueDevice::light_power_set(uint8_t level) {
  device_connect_check();
  if (level >= 0xFE)
    level = 0xFE;
  return this->gatt_write_char_byte("002c", "002f", level);
}

int HueDevice::light_power_notify_get() {
  if (this->light_power_fd == 0) {
    this->light_power_fd = this->gatt_notify_char("002c", "002f");
  }
  return this->light_power_fd;
}

int HueDevice::light_brightness_get() {
  device_connect_check();
  return this->gatt_read_char_byte("002c", "0032");
}

int HueDevice::light_brightness_set(uint8_t level) {
  device_connect_check();
  return this->gatt_write_char_byte("002c", "0032", level);
}

int HueDevice::light_brightness_notify_get() {
  if (this->light_brightness_fd == 0) {
    this->light_brightness_fd = this->gatt_notify_char("002c", "0032");
  }
  return this->light_brightness_fd;
}

HueDevice::Power::Power(HueDevice *p) : parent(p) {}

void HueDevice::Power::operator=(const uint8_t level) {
  parent->light_power_set(level);
}

int HueDevice::Power::operator()() { return parent->light_power_get(); }

HueDevice::Brightness::Brightness(HueDevice *p) : parent(p) {}

void HueDevice::Brightness::operator=(const uint8_t level) {
  parent->light_brightness_set(level);
}

int HueDevice::Brightness::operator()() {
  return parent->light_brightness_get();
}
