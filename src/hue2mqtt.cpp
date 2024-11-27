// 1. Factory reset the bulb (on for 8 off for 2 until it blinks)
// 2. Pair the bulb from bluetoothctl
// 3. Add device to this list

#include <algorithm>
#include <dbus/dbus.h>
#include <fstream>
#include <iostream>
#include <syslog.h>
#include <thread>
#include <unistd.h>
#include <ifaddrs.h>

#include "mqtt.hpp"
#include <json.hpp>

#include <BleDevice.hpp>
#include <BleManager.hpp>
#include <HueDevice.hpp>

using namespace std;
using nlohmann::json;

typedef struct hue_device_handle_s {
	HueDevice *device;
	unsigned int nextAvailable;
	unsigned int nextBrightness;
	unsigned int nextPower;
} hue_device_handle;

struct hue_config_s {
	std::string name;
	std::string config_topic;
	std::string availability_topic;
	std::string set_topic;
	std::string status_topic;
	std::string mac;
};

struct config_s {
	std::string mqtt_host;
	std::string mqtt_user;
	std::string mqtt_pass;
	std::string client_name;
	std::vector<struct hue_config_s> hue_lights;

	string toString()
	{
		string res = "mqtt_host: " + mqtt_host + "\n";
		res += "mqtt_user: " + mqtt_user + "\n";
		for (auto &light : hue_lights) {
			res += "config_topic: " + light.config_topic + "\n";
			res += "availability_topic: " + light.availability_topic + "\n";
			res += "set_topic: " + light.set_topic + "\n";
			res += "status_topic: " + light.status_topic + "\n";
			res += "mac: " + light.mac + "\n";
			res += "name: " + light.name + "\n";
		}
		return res;
	}
} config;

void from_json(const json &j, struct config_s &c)
{
	j.at("mqtt_host").get_to(c.mqtt_host);
	j.at("mqtt_user").get_to(c.mqtt_user);
	j.at("mqtt_pass").get_to(c.mqtt_pass);
	j.at("client_name").get_to(c.client_name);

	for (auto &light : j.at("hue_lights")) {
		c.hue_lights.emplace_back(light.at("name"), light.at("config_topic"), light.at("availability_topic"),
					  light.at("set_topic"), light.at("status_topic"), light.at("mac"));
	}
}

bool isDaemon = false, isTesting = false;

int main(int argc, const char *argv[])
{
	// Get commandline arguments
	std::string configPath = "";

	if (argc < 2) {
		std::cout << "Example useage: " << argv[0] << " -c config.json -d" << std::endl;
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0) {
			isDaemon = true;
		}
		if (strcmp(argv[i], "-t") == 0) {
			isTesting = true;
		}
		if (strcmp(argv[i], "-c") == 0 && i < argc - 1) {
			configPath = argv[i + 1];
			i++;
		}
	}

	// Use commandline arguments
	cout << "Using commandline arguments" << endl;
	cout << "configPath: " << configPath << endl;
	if (configPath.length()) {
		std::ifstream f(configPath);
		config = json::parse(f);
	} else {
		std::cout << "No config file provided" << std::endl;
		exit(0);
	}

	// Save current process id to file to use in rc scripts
	cout << "Saving PID to /var/run/hue2mqtt.pid" << endl;
	FILE *pidfile = fopen("/var/run/hue2mqtt.pid", "w+");
	if (pidfile) {
		fprintf(pidfile, "%d", getpid());
		fclose(pidfile);
	}

	// Start system log
	cout << "Starting system log" << endl;
	openlog("hue2mqtt", LOG_PID, LOG_DAEMON);
	syslog(LOG_NOTICE, "hue2mqtt starting up...");

	// Get hostname
	cout << "Getting hostname" << endl;
	char hostname[50];
	if (gethostname(hostname, sizeof(hostname))) {
		std::cerr << "Could not get hostname" << std::endl;
		return 0;
	}
	syslog(LOG_NOTICE, "hostname is %s..", hostname);

	// get list of ip addressess
	cout << "Getting IP address" << endl;
	struct ifaddrs *ifaddr, *ifa;
	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}
	// convert list of ip addressess into csv string
	string ip = "";
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL) {
			continue;
		}
		if (ifa->ifa_addr->sa_family == AF_INET) {
			char host[NI_MAXHOST];
			int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
			if (s != 0) {
				syslog(LOG_ERR, "getnameinfo() failed: %s", gai_strerror(s));
				exit(EXIT_FAILURE);
			}
			ip += host;
			ip += ",";
		}
	}
	syslog(LOG_NOTICE, "IP address is %s..", ip.c_str());

	// Initialize bluetooth class
	cout << "Initializing bluetooth" << endl;
	if (!isTesting) {
		while (!bleManager.ble_power_get()) {
			syslog(LOG_NOTICE, "waiting for bluetooth to power on...");
			bleManager.ble_power_set(1);
			sleep(1);
		}
	}

	// Print config
	cout << config.toString() << endl;

	// Initialize Hue lights
	vector<hue_device_handle *> lights;
	for (auto &light : config.hue_lights) {
		auto device = new HueDevice(light.mac);
		lights.push_back(new hue_device_handle{ device, 0, 0, 0 });
	}

	// Initialize MQTT library
	Mqtt::Session session;
	cout << "Connecting to MQTT broker..." << endl;
	session.init(config.mqtt_host, 1883, "hue2mqtt", config.mqtt_user, config.mqtt_pass);

	// Put hostname and ip address into MQTT
	session.publish("hue2mqtt/server/" + config.client_name + "/ip", ip, 0, true);

	// Update the current state of each light for home assistant (initialization)
	cout << "Updating light status..." << endl;
	for (auto &config : config.hue_lights) {
		auto search = find_if(lights.begin(), lights.end(),
				      [&config](hue_device_handle *light) { return light->device->mac == config.mac; });
		if (search == lights.end()) {
			continue;
		}
		auto handle = *search;
		auto bleDevice = (*search)->device;
		// Wait for device to connect
		while (!bleDevice->device_connected_get()) {
			syslog(LOG_NOTICE, "waiting for %s to connect...", bleDevice->devicePath.c_str());
			bleDevice->device_connected_set(1);
			sleep(1);
		}
		// Subscribe to the set topic
		session.subscribe(config.set_topic, 0, 1);
		// Publish the availability of the light
		syslog(LOG_DEBUG, "publish availability for %s", bleDevice->devicePath.c_str());
		session.publish(config.availability_topic, bleDevice->device_connected_get() ? "online" : "offline", 0, true);
		// Publish the current state of the light
		handle->nextAvailable = 1;
		handle->nextPower = bleDevice->light_power_get();
		handle->nextBrightness = bleDevice->light_brightness_get();
		// Add light to homeassistant topics
		auto res = json{ { "name", config.name },
				 { "command_topic", config.set_topic },
				 { "state_topic", config.status_topic },
				 { "avty_t", config.availability_topic },
				 { "pl_avail", "online" },
				 { "pl_not_avail", "offline" },
				 { "unique_id", config.mac },
				 { "schema", "json" },
				 { "brightness", true },
				 { "brightness_scale", 250 } };
		syslog(LOG_DEBUG, "publish status for %s", bleDevice->devicePath.c_str());
		session.publish(config.config_topic, res.dump(), 0, true);
	}

	// Main loop
	while (true) {
		// Receive BLE notifications
		for (auto &handle : lights) {
			auto &bleDevice = handle->device;
			const int power_fd = bleDevice->light_power_notify_get();
			const int brightness_fd = bleDevice->light_brightness_notify_get();
			if (power_fd > 0) {
				static char power_buf[1];
				const int s = read(power_fd, power_buf, 1);
				if (s > 0) {
					handle->nextPower = power_buf[0] & 0xFF;
					handle->nextAvailable = 1;
				}
			}
			if (brightness_fd > 0) {
				static char brightness_buf[1];
				const int s = read(brightness_fd, brightness_buf, 1);
				if (s > 0) {
					handle->nextBrightness = brightness_buf[0] & 0xFF;
					handle->nextAvailable = 1;
				}
			}
			if (handle->nextAvailable) {
				// publish the new state
				auto res = json{ { "state", handle->nextPower ? "ON" : "OFF" },
						 { "brightness", handle->nextBrightness } };
				syslog(LOG_DEBUG, "publish status for %s", bleDevice->devicePath.c_str());
				auto search =
					find_if(config.hue_lights.begin(), config.hue_lights.end(),
						[&bleDevice](struct hue_config_s &light) { return light.mac == bleDevice->mac; });
				if (search != config.hue_lights.end()) {
					auto &config = *search;
					session.publish(config.status_topic, res.dump(), 0, true);
				}
				handle->nextAvailable = 0;
			}
		}

		// Verify device is connected
		static int timeout0 = 0;
		if (timeout0++ > 100) {
			timeout0 = 0;
			for (auto &handle : lights) {
				auto &bleDevice = handle->device;
				if (!bleDevice->device_connected_get()) {
					syslog(LOG_NOTICE, "%s is disconnected, reconnecting...", bleDevice->devicePath.c_str());
					bleDevice->light_power_fd = 0;
					bleDevice->light_brightness_fd = 0;
					bleDevice->device_connected_set(1);
				}
			}
		}

		// Handle MQTT protocol
		session.handleSocket();

		// Wait for new MQTT messages
		if (session.publish_queue.empty()) {
			usleep(100000);
			continue;
		}

		// Get next MQTT message (from currently subscribed topics)
		auto msg = session.publish_queue.front();
		session.publish_queue.pop();
		syslog(LOG_DEBUG, "Received message from the Broker...");
		syslog(LOG_DEBUG, "\t topic: %s", msg.topic.c_str());
		syslog(LOG_DEBUG, "\t payload: %s", msg.message.c_str());

		// Handle requests from home assistant/node red
		for (auto &config : config.hue_lights) {
			if (msg.topic == config.set_topic) {
				bool available = false;
				int brightness = 0;
				std::string state = "UNK";
				// Find the light (bluetooth connection) that the message is for
				auto search = find_if(lights.begin(), lights.end(), [&config](hue_device_handle *light) {
					return light->device->mac == config.mac;
				});
				if (search == lights.end()) {
					continue;
				}
				auto handler = *search;
				auto bleDevice = (*search)->device;
				if ((available = bleDevice->device_connected_get())) {
					// Parse the message
					if (msg.message.starts_with("{")) {
						// Handle JSON requests
						auto req = json::parse(msg.message);
						if (req.contains("state")) {
							req.at("state").get_to(state);
						}
						if (req.contains("brightness")) {
							req.at("brightness").get_to(brightness);
						}
					} else if (msg.message == "OFF" || msg.message == "ON") {
						// Handle simple ON/OFF requests
						state = msg.message;
						brightness = bleDevice->light_brightness_get();
					}
					// Set the light to the requested state
					if (state == "ON" || state == "OFF") {
						bleDevice->light_power_set(state == "ON");
						if (bleDevice->light_power_get() == (state == "ON")) {
							handler->nextPower = state == "ON";
							handler->nextAvailable = 1;
						}
					}
					// Set the brightness to the requested level
					if (brightness > 0) {
						bleDevice->light_brightness_set(brightness);
						if (bleDevice->light_brightness_get() == brightness) {
							handler->nextBrightness = brightness;
							handler->nextAvailable = 1;
						}
					}
				}
			}
		}
	}

	closelog();
	return 0;
}
