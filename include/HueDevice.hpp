#pragma once

#include "BleDevice.hpp"
#include <cstdint>

// To reset bulb turn on for 8 seconds, off for 2. Repeat until bulbs rapidly
// flash.

class HueDevice : public BleDevice {
public:
  HueDevice(std::string mac);
  class Power {
    HueDevice *parent;

  public:
    Power(HueDevice *p);
    void operator=(const uint8_t level);
    int operator()();
  } power;
  class Brightness {
    HueDevice *parent;

  public:
    Brightness(HueDevice *p);
    void operator=(const uint8_t level);
    int operator()();
  } brightness;

  int light_power_fd = 0;
  int light_brightness_fd = 0;

  int light_power_get();
  int light_power_set(uint8_t level);
  int light_power_notify_get();

  int light_brightness_get();
  int light_brightness_set(uint8_t level);
  int light_brightness_notify_get();
};
