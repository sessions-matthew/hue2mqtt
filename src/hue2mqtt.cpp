#include <iostream>
#include <fstream>
#include <unistd.h>
#include <thread>
#include <dbus/dbus.h>
#include <syslog.h>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/program_options.hpp>

#include <async_mqtt5.hpp>
#include <json.hpp>

#include <BleManager.hpp>
#include <BleDevice.hpp>
#include <HueDevice.hpp>

constexpr auto use_nothrow_awaitable = boost::asio::as_tuple(boost::asio::use_awaitable);
using client_type = async_mqtt5::mqtt_client<boost::asio::ip::tcp::socket>;
using boost::asio::awaitable;
using json = nlohmann::json;
namespace po = boost::program_options;

struct hue_config_s {
	std::string availability_topic;
	std::string set_topic;
	std::string status_topic;
	std::string mac;
};

struct config_s {
	std::string mqtt_host;
	std::string mqtt_user;
	std::string mqtt_pass;
	std::vector<struct hue_config_s> hue_lights;
} config;

void from_json(const json &j, struct config_s &c)
{
	j.at("mqtt_host").get_to(c.mqtt_host);
	j.at("mqtt_user").get_to(c.mqtt_user);
	j.at("mqtt_pass").get_to(c.mqtt_pass);

	for (auto &light : j.at("hue_lights")) {
		c.hue_lights.emplace_back(light.at("availability_topic"), light.at("set_topic"), light.at("status_topic"),
					  light.at("mac"));
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
	// json config;
	if (configPath.length()) {
		std::ifstream f(configPath);
		config = json::parse(f);
	} else {
		std::cout << "No config file provoded" << std::endl;
		exit(0);
	}

	// Save current process id to file to use in rc scripts
	// if (isDaemon) {
	FILE *pidfile = fopen("/var/run/hue2mqtt.pid", "w+");
	if (pidfile) {
		fprintf(pidfile, "%d", getpid());
		fclose(pidfile);
	}
	// }

	// Start system log
	openlog("hue2mqtt", LOG_PID, LOG_DAEMON);
	syslog(LOG_NOTICE, "hue2mqtt starting up...");

	// Get hostname
	char hostname[50];
	if (gethostname(hostname, sizeof(hostname))) {
		std::cerr << "Could not get hostname" << std::endl;
		return 0;
	}
	syslog(LOG_NOTICE, "hostname is %s..", hostname);

	// Initialize bluetooth class
	if (!isTesting) {
		while (!bleManager.ble_power_get()) {
			syslog(LOG_NOTICE, "waiting for bluetooth to power on...");
			bleManager.ble_power_set(1);
			sleep(1);
		}
	}

	// Initialize MQTT library
	boost::asio::io_context ioc;
	async_mqtt5::mqtt_client<boost::asio::ip::tcp::socket> c(ioc);
	c.credentials(hostname, config.mqtt_user, config.mqtt_pass).brokers(config.mqtt_host, 1883).async_run(boost::asio::detached);

	co_spawn(
		ioc,
		[](client_type *client) -> awaitable<void> {
			std::map<std::string, HueDevice> lights;
			for (auto &light : config.hue_lights) {
				lights.emplace(std::piecewise_construct, std::forward_as_tuple(light.mac),
					       std::forward_as_tuple(light.mac));
			}
			auto subscribe = [](client_type *c) -> awaitable<bool> {
				std::vector<async_mqtt5::subscribe_topic> subs;
				for (auto &light : config.hue_lights) {
					subs.emplace_back(
						light.set_topic,
						async_mqtt5::subscribe_options{
							async_mqtt5::qos_e::exactly_once, // All messages will arrive at QoS 2.
							async_mqtt5::no_local_e::no, // Forward message from Clients with same ID.
							async_mqtt5::retain_as_published_e::retain, // Keep the original RETAIN flag.
							async_mqtt5::retain_handling_e::
								send // Send retained messages when the subscription is established.
						});
				}
				auto &&[ec, sub_codes, sub_props] =
					co_await c->async_subscribe(subs, async_mqtt5::subscribe_props{}, use_nothrow_awaitable);
				if (ec)
					syslog(LOG_DEBUG, "Subscribe error occurred: %s", ec.message().c_str());
				else
					syslog(LOG_DEBUG, "Result of subscribe request: %s", sub_codes[0].message().c_str());

				co_return !ec && !sub_codes[0];
			};

			if (!(co_await subscribe(client))) {
				syslog(LOG_DEBUG, "subscribe failed");
				co_return;
			}

			// Update the current state of each light for home assistant (initialization)
			if (!isTesting) {
				for (auto &light : config.hue_lights) {
					auto &bleDevice = lights.at(light.mac);
					bleDevice.device_connected_set(1);
					auto res = json{ { "state", bleDevice.light_power_get() ? "ON" : "OFF" },
							 { "brightness", bleDevice.light_brightness_get() } };
					syslog(LOG_DEBUG, "publish availability for %s", bleDevice.devicePath.c_str());
					co_await client->async_publish<async_mqtt5::qos_e::at_most_once>(
						light.availability_topic, bleDevice.device_connected_get() ? "online" : "offline",
						async_mqtt5::retain_e::yes, async_mqtt5::publish_props{});
					syslog(LOG_DEBUG, "publish status for %s", bleDevice.devicePath.c_str());
					co_await client->async_publish<async_mqtt5::qos_e::at_most_once>(
						light.status_topic, res.dump(), async_mqtt5::retain_e::yes,
						async_mqtt5::publish_props{});
				}
			}
			for (;;) {
				// Get next MQTT message (from currently subscribed topics)
				auto &&[ec, topic, payload, publish_props] = co_await client->async_receive(use_nothrow_awaitable);

				syslog(LOG_DEBUG, "Received message from the Broker...");
				syslog(LOG_DEBUG, "\t topic: %s", topic.c_str());
				syslog(LOG_DEBUG, "\t payload: %s", payload.c_str());

				// Handle connection loss to MQTT server
				if (ec == async_mqtt5::client::error::session_expired) {
					if (co_await subscribe(client))
						continue;
					else
						break;
				} else if (ec)
					break;

				// Handle requests from home assistant/node red
				if (!isTesting) {
					for (auto &light : config.hue_lights) {
						auto &bleDevice = lights.at(light.mac);
						if (topic == light.set_topic) {
							bool available = false;
							int brightness = 0;
							std::string state = "UNK";

							if (available = bleDevice.device_connected_get()) {
								if (payload.starts_with("{")) {
									// Handle JSON requests
									auto req = json::parse(payload);
									if (req.contains("state")) {
										req.at("state").get_to(state);
									}
									if (req.contains("brightness")) {
										req.at("brightness").get_to(brightness);
									}
									if (state == "ON" || state == "OFF") {
										bleDevice.light_power_set(state == "ON");
									}
									if (brightness) {
										bleDevice.light_brightness_set(brightness);
									}
								} else if (payload == "ON" || payload == "OFF") {
									// Handle non-json requests, get current brightness to send back
									bleDevice.light_power_set(payload == "ON");
									payload = json{
										{ "state", payload },
										{ "brightness", bleDevice.light_brightness_get() }
									}.dump();
								}
								// Update current status in home assistant
								co_await client->async_publish<async_mqtt5::qos_e::at_most_once>(
									light.status_topic, payload, async_mqtt5::retain_e::yes,
									async_mqtt5::publish_props{});
							}
						}
					}
				}
			}
		}(&c),
		boost::asio::detached);

	co_spawn(
		ioc,
		[&ioc](client_type *client) -> awaitable<void> {
			std::map<std::string, bool> lastConnected;
			std::map<std::string, HueDevice> lights;
			for (auto &light : config.hue_lights) {
				lastConnected[light.mac] = false;
				lights.emplace(std::piecewise_construct, std::forward_as_tuple(light.mac),
					       std::forward_as_tuple(light.mac));
			}
			boost::asio::steady_timer t(ioc);
			while (1) {
				if (!isTesting) {
					for (auto &light : config.hue_lights) {
						auto &bleDevice = lights.at(light.mac);
						bool isConnected = bleDevice.device_connected_get();
						bool wasConnected = lastConnected.at(light.mac);
						if (isConnected && !wasConnected) {
							std::string payload = json{
								{ "state", "ON" },
								{ "brightness", bleDevice.light_brightness_get() }
							}.dump();
							// Update current status in home assistant
							co_await client->async_publish<async_mqtt5::qos_e::at_most_once>(
								light.status_topic, payload, async_mqtt5::retain_e::yes,
								async_mqtt5::publish_props{});
							lastConnected.at(light.mac) = true;
						}
						if (!isConnected) {
							bleDevice.device_connected_set(1);
						}
						if (isConnected != wasConnected) {
							lastConnected.at(light.mac) = isConnected;
							// Update device availablility in home assistant
							co_await client->async_publish<async_mqtt5::qos_e::at_most_once>(
								light.availability_topic,
								isConnected ? "online" : "offline",
								async_mqtt5::retain_e::yes, async_mqtt5::publish_props{});
						}
					}
				}
				t.expires_after(boost::asio::chrono::seconds(5));
				co_await t.async_wait();
			}
		}(&c),
		boost::asio::detached);

	ioc.run();

	closelog();
	return 0;
}
