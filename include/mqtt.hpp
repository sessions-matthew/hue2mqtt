#pragma once

#include <functional>
#include <iostream>
#include <queue>
#include <stdio.h>
#include <vector>

#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

using namespace std;

namespace Mqtt {

const bool debug = false;

namespace ControlPacketType {
const uint8_t CONNECT = 0x01;
const uint8_t CONNACK = 0x02;
const uint8_t PUBLISH = 0x03;
const uint8_t PUBACK = 0x04;
const uint8_t PUBREC = 0x05;
const uint8_t PUBREL = 0x06;
const uint8_t PUBCOMP = 0x07;
const uint8_t SUBSCRIBE = 0x08;
const uint8_t SUBACK = 0x09;
const uint8_t UNSUBSCRIBE = 0x0A;
const uint8_t UNSUBACK = 0x0B;
const uint8_t PINGREQ = 0x0C;
const uint8_t PINGRESP = 0x0D;
const uint8_t DISCONNECT = 0x0E;
const uint8_t AUTH = 0x0F;
} // namespace ControlPacketType

namespace ControlPacketFlags {
const uint8_t CONNECT = 0x00;
const uint8_t CONNACK = 0x00;
const uint8_t PUBLISH = 0x00;
const uint8_t PUBACK = 0x00;
const uint8_t PUBREC = 0x00;
const uint8_t PUBREL = 0x02;
const uint8_t PUBCOMP = 0x00;
const uint8_t SUBSCRIBE = 0x02;
const uint8_t SUBACK = 0x00;
const uint8_t UNSUBSCRIBE = 0x02;
const uint8_t UNSUBACK = 0x00;
const uint8_t PINGREQ = 0x00;
const uint8_t PINGRESP = 0x00;
const uint8_t DISCONNECT = 0x00;
const uint8_t AUTH = 0x00;
} // namespace ControlPacketFlags

namespace ConnectFlags {
const uint8_t USERNAME = 1 << 7;
const uint8_t PASSWORD = 1 << 6;
const uint8_t WILL_RETAIN = 1 << 5;
const uint8_t WILL_QOS = 1 << 4 & 1 << 3;
const uint8_t WILL_FLAG = 1 << 2;
const uint8_t CLEAN_SESSION = 1 << 1;
} // namespace ConnectFlags

namespace ConnectProperties {
const uint8_t SESSION_EXPIRY_INTERVAL = 0x11;
const uint8_t RECEIVE_MAXIMUM = 0x21;
const uint8_t MAXIMUM_PACKET_SIZE = 0x27;
const uint8_t TOPIC_ALIAS_MAXIMUM = 0x22;
const uint8_t REQUEST_RESPONSE_INFORMATION = 0x19;
const uint8_t REQUEST_PROBLEM_INFORMATION = 0x17;
const uint8_t USER_PROPERTY = 0x26;
const uint8_t AUTHENTICATION_METHOD = 0x15;
const uint8_t AUTHENTICATION_DATA = 0x16;
} // namespace ConnectProperties

namespace ConnackProperties {
const uint8_t SESSION_EXPIRY_INTERVAL = 0x11;
const uint8_t RECEIVE_MAXIMUM = 0x21;
const uint8_t MAXIMUM_QOS = 0x24;
const uint8_t RETAIN_AVAILABLE = 0x25;
const uint8_t MAXIMUM_PACKET_SIZE = 0x27;
const uint8_t ASSIGNED_CLIENT_IDENTIFIER = 0x12;
const uint8_t TOPIC_ALIAS_MAXIMUM = 0x22;
const uint8_t REASON_STRING = 0x1F;
const uint8_t USER_PROPERTY = 0x26;
const uint8_t WILDCARD_SUBSCRIPTION_AVAILABLE = 0x28;
const uint8_t SUBSCRIPTION_IDENTIFIER_AVAILABLE = 0x29;
const uint8_t SHARED_SUBSCRIPTION_AVAILABLE = 0x2A;
} // namespace ConnackProperties

namespace SubackProperties {
const uint8_t REASON_STRING = 0x1F;
const uint8_t USER_PROPERTY = 0x26;
} // namespace SubackProperties

struct Publish {
  uint8_t qos;
  bool retain;
  uint16_t packet_identifier;
  string topic;
  string message;
};

struct ConnAck {
  uint8_t session_expiry_interval;
  uint8_t receive_maximum;
  uint8_t maximum_qos;
  bool retain_available;
  uint8_t maximum_packet_size;
  string assigned_client_identifier;
  uint8_t topic_alias_maximum;
  string reason_string;
  string user_property;
  bool wildcard_subscription_available;
  bool subscription_identifier_available;
  bool shared_subscription_available;
};

struct Subscribe {
  string topic;
  uint8_t qos;
  uint16_t packet_identifier;
};

class Session {
  bool isConnected = false;
  bool isDisconnected = false;
  string client_id;
  string username;
  string password;
  string addr;
  int port;
  int timeout0 = 0;
  int timeout1 = 0;

  bool pingSent = false;
  bool pingReceived = false;

public:
  boost::asio::io_context io_context;
  boost::asio::ip::tcp::socket socket;
  queue<Publish> pub_incoming_queue;
  queue<Publish> pub_outgoing_queue;
  vector<Subscribe> subscriptions;

  Session() : socket(io_context) {}
  void init(string addr, int port, string client_id, string username,
            string password);
  void process();
  void handleSocket();
  void connect();
  void publish(string topic, string message, uint8_t qos, bool retain);
  void subscribe(string topic, uint8_t qos, uint16_t packet_identifier);
};
} // namespace Mqtt
