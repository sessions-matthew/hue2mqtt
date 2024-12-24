#include <algorithm>
#include <dbus/dbus.h>
#include <fstream>
#include <ifaddrs.h>
#include <iostream>
#include <syslog.h>
#include <thread>
#include <unistd.h>

#include "mqtt.hpp"
#include <json.hpp>

#include <BleDevice.hpp>
#include <BleManager.hpp>
#include <HueDevice.hpp>

using namespace std;
using nlohmann::json;

// Reference:
// https://github.com/wware/stuff/blob/master/dbus-example/dbus-example.c

int main()
{
	for (int i = 0; i < 128; i++) {
		bleManager.ble_power_get();
	}
	cout << "DONE" << endl;

	return 0;
}