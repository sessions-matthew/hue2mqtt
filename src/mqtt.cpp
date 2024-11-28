#include "mqtt.hpp"
#include <sys/syslog.h>

namespace Mqtt {
const bool controlPacketRequiresIdentifier(uint8_t control_packet_type,
                                           uint8_t qos) {
  return (control_packet_type == ControlPacketType::PUBLISH && qos > 0) ||
         control_packet_type == ControlPacketType::SUBSCRIBE ||
         control_packet_type == ControlPacketType::UNSUBSCRIBE ||
         control_packet_type == ControlPacketType::PUBACK ||
         control_packet_type == ControlPacketType::PUBREC ||
         control_packet_type == ControlPacketType::PUBREL ||
         control_packet_type == ControlPacketType::PUBCOMP;
}

tuple<int, int> decodeInt(uint8_t *buffer) {
  uint8_t encodedByte = 0;
  int n = 0;
  int i = 0;
  int multiplier = 1;
  do {
    encodedByte = buffer[i];
    n += (encodedByte & 127) * multiplier;
    multiplier *= 128;
    i++;
  } while (encodedByte & 128);
  return make_tuple(i, n);
}

int encodeInt(uint8_t *buffer, int n) {
  int i = 0;
  do {
    uint8_t encodedByte = n % 128;
    n = n / 128;
    if (n > 0) {
      encodedByte = encodedByte | 128;
    }
    buffer[i] = encodedByte;
    i++;
  } while (n > 0);
  return i;
}

int encodeString(uint8_t *buffer, const string str) {
  buffer[0] = str.length() >> 8;
  buffer[1] = str.length() & 0xFF;
  for (int i = 0; i < str.length(); i++) {
    buffer[i + 2] = str[i];
  }
  return str.length() + 2;
}

Publish publishFromBytes(uint8_t header, int32_t len, uint8_t *buffer) {
  //   const uint8_t dup = (header >> 3) & 0x01;
  const uint8_t qos = (header >> 1) & 0x03;
  const bool retain = (header & 0x01) == 0x01;
  uint8_t *buffer_iter = buffer;

  const uint16_t topic_len = buffer_iter[0] << 8 | buffer_iter[1];
  const string topic = string((char *)buffer_iter + 2, topic_len);
  buffer_iter += 2 + topic_len;

  uint16_t packet_identifier = 0;
  if (qos > 0) {
    packet_identifier = buffer_iter[0] << 8 | buffer_iter[1];
    buffer_iter += 2;
  }

  // Get Properties if there are any
  if (buffer_iter[0] == 0) {
    buffer_iter++;
  } else {
    auto [properties_len, properties] = decodeInt(buffer_iter);
    buffer_iter += properties_len;

    cout << "Unhandled Properties Length: " << properties << " ? "
         << properties_len << endl;
  }

  const string message =
      string((char *)buffer_iter, len - (buffer_iter - buffer));

  return {qos, retain, packet_identifier, topic, message};
}

ConnAck connAckFromBytes(uint8_t header, int32_t len, uint8_t *buffer) {
  //   uint8_t ack_flags = buffer[0];
  //   uint8_t conn_reason = buffer[1];
  //   auto [property_len, property] = decodeInt(buffer + 2);

  return {};
}

int readInt(boost::asio::ip::tcp::socket &socket) {
  uint8_t encodedByte = 0;
  int n = 0;
  int multiplier = 1;
  do {
    socket.read_some(boost::asio::buffer(&encodedByte, 1));
    n += (encodedByte & 127) * multiplier;
    multiplier *= 128;
  } while (encodedByte & 128);
  return n;
}

void connect(boost::asio::ip::tcp::socket &socket, string client_id,
             string username, string password) {
  uint8_t fixed_header = ControlPacketType::CONNECT << 4;

  size_t packet_len = client_id.length() + 2 + username.length() + 2 +
                      password.length() + 2 + 11;
  uint8_t encoded_control_packet_length[4];
  uint8_t bytes_for_packet_length =
      encodeInt(encoded_control_packet_length, packet_len);
  size_t buffer_size = bytes_for_packet_length + packet_len + 1;
  uint8_t *control_packet = new uint8_t[buffer_size];
  uint8_t *control_packet_iter = control_packet;

  // Add fixed header
  control_packet_iter[0] = fixed_header;
  control_packet_iter++;
  // Add variable header
  //  Add remaining length
  control_packet_iter += encodeInt(control_packet_iter, packet_len);
  //  Add protocol name
  control_packet_iter[0] = 0;
  control_packet_iter[1] = 4;
  control_packet_iter[2] = 'M';
  control_packet_iter[3] = 'Q';
  control_packet_iter[4] = 'T';
  control_packet_iter[5] = 'T';
  control_packet_iter[6] = 0x05;
  control_packet_iter[7] = ConnectFlags::USERNAME | ConnectFlags::PASSWORD |
                           ConnectFlags::CLEAN_SESSION;
  control_packet_iter[8] = 0x00;  // Keep alive
  control_packet_iter[9] = 0x3C;  // Keep alive
  control_packet_iter[10] = 0x00; // Properties
  control_packet_iter += 11;
  // Add payload
  //  Add client id
  control_packet_iter += encodeString(control_packet_iter, client_id);
  //  Add username
  control_packet_iter += encodeString(control_packet_iter, username);
  //  Add password
  control_packet_iter += encodeString(control_packet_iter, password);

  if (debug) {
    cout << "Sending connect packet: ";
    for (int i = 0; i < buffer_size; i++) {
      cout << " " << (unsigned int)control_packet[i];
    }
    cout << endl;
  }

  socket.write_some(boost::asio::buffer(control_packet, buffer_size));
  delete[] control_packet;
}

void publish(boost::asio::ip::tcp::socket &socket, string topic, string message,
             uint8_t qos, bool retain, uint16_t packet_identifier = 1) {
  uint8_t fixed_header = ControlPacketType::PUBLISH << 4;
  if (qos > 0) {
    fixed_header |= qos << 1;
  }
  if (retain) {
    fixed_header |= 0x01;
  }

  size_t packet_len =
      1 + (topic.length() + 2) + (qos > 0 ? 2 : 0) + message.length();
  uint8_t encoded_control_packet_length[4];
  uint8_t bytes_for_packet_length =
      encodeInt(encoded_control_packet_length, packet_len);
  size_t buffer_size = 1 + bytes_for_packet_length + packet_len;
  uint8_t *control_packet = new uint8_t[buffer_size];
  uint8_t *control_packet_iter = control_packet;

  // Add fixed header
  control_packet_iter[0] = fixed_header;
  control_packet_iter++;
  // Add variable header
  //  Add remaining length
  control_packet_iter += encodeInt(control_packet_iter, packet_len);
  //  Add topic
  control_packet_iter += encodeString(control_packet_iter, topic);
  //  Add packet identifier if qos > 0
  if (qos > 0) {
    control_packet_iter[0] = packet_identifier >> 8;
    control_packet_iter[1] = packet_identifier & 0xFF;
    control_packet_iter += 2;
  }
  //  Add properties
  control_packet_iter[0] = 0; // No properties
  control_packet_iter++;
  // Add payload
  memcpy(control_packet_iter, message.c_str(), message.length());

  if (debug) {
    cout << "Sending publish packet: ";
    for (int i = 0; i < buffer_size; i++) {
      cout << " " << (unsigned int)control_packet[i];
    }
    cout << endl;
  }

  socket.write_some(boost::asio::buffer(control_packet, buffer_size));
  delete[] control_packet;
}

void subscribe(boost::asio::ip::tcp::socket &socket, string topic, uint8_t qos,
               uint16_t packet_identifier) {
  uint8_t fixed_header = ControlPacketType::SUBSCRIBE << 4 | 0x02;

  size_t packet_len = 2 + 1 + (topic.length() + 2 + 1);
  uint8_t encoded_control_packet_length[4];
  uint8_t bytes_for_packet_length =
      encodeInt(encoded_control_packet_length, packet_len);
  size_t buffer_size = 1 + bytes_for_packet_length + packet_len;
  uint8_t *control_packet = new uint8_t[buffer_size];
  uint8_t *control_packet_iter = control_packet;

  // Add fixed header
  control_packet_iter[0] = fixed_header;
  control_packet_iter++;
  // Add variable header
  //  Add remaining length
  control_packet_iter += encodeInt(control_packet_iter, packet_len);
  //  Add packet identifier
  control_packet_iter[0] = packet_identifier >> 8;
  control_packet_iter[1] = packet_identifier & 0xFF;
  control_packet_iter += 2;
  //  Add properties
  control_packet_iter[0] = 0;
  control_packet_iter++;
  // Add payload
  //  Add topic
  control_packet_iter += encodeString(control_packet_iter, topic);
  //  Add topic options
  control_packet_iter[0] = qos;
  control_packet_iter++;

  if (debug) {
    cout << "Sending subscribe packet: ";
    for (int i = 0; i < buffer_size; i++) {
      cout << " " << (unsigned int)control_packet[i];
    }
    cout << endl;
  }

  socket.write_some(boost::asio::buffer(control_packet, buffer_size));
  delete[] control_packet;
}

void pingreq(boost::asio::ip::tcp::socket &socket) {
  uint8_t fixed_header = ControlPacketType::PINGREQ << 4;
  const size_t buffer_size = 2;
  uint8_t control_packet[buffer_size];

  control_packet[0] = fixed_header;
  control_packet[1] = 0;

  if (debug) {
    cout << "Sending pingreq packet";
  }

  socket.write_some(boost::asio::buffer(control_packet, buffer_size));
}

bool isValidCommandType(uint8_t control_packet_type) {
  return control_packet_type == ControlPacketType::CONNECT ||
         control_packet_type == ControlPacketType::CONNACK ||
         control_packet_type == ControlPacketType::PUBLISH ||
         control_packet_type == ControlPacketType::PUBACK ||
         control_packet_type == ControlPacketType::PUBREC ||
         control_packet_type == ControlPacketType::PUBREL ||
         control_packet_type == ControlPacketType::PUBCOMP ||
         control_packet_type == ControlPacketType::SUBSCRIBE ||
         control_packet_type == ControlPacketType::SUBACK ||
         control_packet_type == ControlPacketType::UNSUBSCRIBE ||
         control_packet_type == ControlPacketType::UNSUBACK ||
         control_packet_type == ControlPacketType::PINGREQ ||
         control_packet_type == ControlPacketType::PINGRESP ||
         control_packet_type == ControlPacketType::DISCONNECT ||
         control_packet_type == ControlPacketType::AUTH;
}

void Session::process() {
  // reconnect
  if (isDisconnected) {
    connect();
  }

  // publish queued messages
  if (isConnected && !pub_outgoing_queue.empty()) {
    Publish pub = pub_outgoing_queue.front();
    try {
      Mqtt::publish(socket, pub.topic, pub.message, pub.qos, pub.retain);
      pub_outgoing_queue.pop();
    } catch (const std::exception &e) {
      syslog(LOG_ERR, "Error sending publish packet: %s", e.what());
      isDisconnected = true;
      isConnected = false;
    }
  }

  // routinely send a ping
  if (isConnected) {
    if (timeout1++ > 100) {
      if (pingSent && !pingReceived) {
        syslog(LOG_ERR, "Ping not received, disconnecting...");
        isDisconnected = true;
        isConnected = false;
        pingSent = false;
      }
      try {
        Mqtt::pingreq(socket);
        pingSent = true;
        pingReceived = false;
        timeout1 = 0;
      } catch (const std::exception &e) {
        syslog(LOG_ERR, "Error sending pingreq packet: %s", e.what());
        isDisconnected = true;
        isConnected = false;
      }
    }
  } else {
    timeout1 = 0;
  }

  handleSocket();
}

void Session::handleSocket() {
  uint8_t header[1];

  if (socket.available() == 0) {
    return;
  }
  socket.read_some(boost::asio::buffer(header, 1));

  uint8_t command = (header[0] >> 4);
  if (isValidCommandType(command)) {
    int len = readInt(socket);
    uint8_t *recv = new uint8_t[len];
    socket.read_some(boost::asio::buffer(recv, len));

    if (command == ControlPacketType::CONNACK) {
      syslog(LOG_NOTICE, "Received CONNACK");
      ConnAck connack = connAckFromBytes(header[0], len, recv);
      syslog(LOG_NOTICE,
             "Session Expiry Interval: %d Receive Maximum: %d Maximum QoS: %d "
             "Retain Available: %d Maximum Packet Size: %d Assigned Client "
             "Identifier: %s Topic Alias Maximum: %d Reason String: %s User "
             "Property: %s Wildcard Subscription Available: %d Subscription "
             "Identifier Available: %d Shared Subscription Available: %d",
             (int)connack.session_expiry_interval, (int)connack.receive_maximum,
             (int)connack.maximum_qos, connack.retain_available,
             (int)connack.maximum_packet_size,
             connack.assigned_client_identifier.c_str(),
             (int)connack.topic_alias_maximum, connack.reason_string.c_str(),
             connack.user_property.c_str(),
             connack.wildcard_subscription_available,
             connack.subscription_identifier_available,
             connack.shared_subscription_available);
      isConnected = true;
      timeout0 = 0;
    } else if (command == ControlPacketType::PUBLISH) {
      Publish publish = publishFromBytes(header[0], len, recv);
      if (debug) {
        syslog(LOG_NOTICE, "Received PUBLISH");
        syslog(LOG_NOTICE,
               "QoS: %d Retain: %d Packet Identifier: %d Topic: %s Message: %s",
               (int)publish.qos, publish.retain, publish.packet_identifier,
               publish.topic.c_str(), publish.message.c_str());
      }

      pub_incoming_queue.push(publish);
    } else if (command == ControlPacketType::PUBACK) {
      syslog(LOG_NOTICE, "Received PUBACK");
    } else if (command == ControlPacketType::PUBREC) {
      syslog(LOG_NOTICE, "Received PUBREC");
    } else if (command == ControlPacketType::PUBREL) {
      syslog(LOG_NOTICE, "Received PUBREL");
    } else if (command == ControlPacketType::PUBCOMP) {
      syslog(LOG_NOTICE, "Received PUBCOMP");
    } else if (command == ControlPacketType::SUBACK) {
      syslog(LOG_NOTICE, "Received SUBACK");
    } else if (command == ControlPacketType::PINGREQ) {
      syslog(LOG_NOTICE, "Received PINGREQ");
    } else if (command == ControlPacketType::PINGRESP) {
      syslog(LOG_NOTICE, "Received PINGRESP");
      pingReceived = true;
    } else if (command == ControlPacketType::DISCONNECT) {
      syslog(LOG_NOTICE, "Received DISCONNECT");
      isConnected = false;
    } else {
      syslog(LOG_NOTICE, "Unhandled response type for %d", command);
    }

    if (debug) {
      for (int i = 0; i < len; i++) {
        syslog(LOG_DEBUG, " %u", (unsigned int)recv[i]);
      }
      if (len) {
        syslog(LOG_DEBUG, "\n");
      }

      for (int i = 0; i < len; i++) {
        cout << " " << (unsigned int)recv[i];
      }
      if (len) {
        cout << endl;
      }
    }

    delete[] recv;
  } else {
    cout << "Invalid command type: " << (header[0] >> 4) << endl;
  }
}
void Session::init(string addr, int port, string client_id, string username,
                   string password) {
  this->client_id = client_id;
  this->username = username;
  this->password = password;
  this->addr = addr;
  this->port = port;

  try {
    socket.connect(boost::asio::ip::tcp::endpoint(
        boost::asio::ip::address::from_string(addr), port));

    connect();
  } catch (const std::exception &e) {
    syslog(LOG_ERR, "Error connecting to MQTT server: %s", e.what());
    sleep(1);
  }
}
void Session::connect() {
  int timeout = 0;

  if (isDisconnected && socket.is_open()) {
    syslog(LOG_NOTICE, "Disconnecting from MQTT server...");
    socket.close();
    isConnected = false;
    try {
      socket.connect(boost::asio::ip::tcp::endpoint(
          boost::asio::ip::address::from_string(addr), port));
    } catch (const std::exception &e) {
      syslog(LOG_ERR, "Error connecting to MQTT server: %s", e.what());
      sleep(1);
    }
  }

  if (!isConnected) {
    syslog(LOG_NOTICE, "Waiting for MQTT connection...");
    try {
      Mqtt::connect(socket, client_id, username, password);
      isDisconnected = false;
    } catch (const std::exception &e) {
      syslog(LOG_ERR, "Error sending connect packet: %s", e.what());
      isDisconnected = true;
    }
  }

  while (timeout++ < 10) {
    if (isConnected) {
      break;
    }
    syslog(LOG_NOTICE, "Waiting for MQTT connection...");
    handleSocket();
    sleep(1);
  }
  if (isConnected) {
    syslog(LOG_NOTICE, "Connected to MQTT server");
    for (auto &sub : subscriptions) {
      syslog(LOG_NOTICE, "Resubscribing to %s", sub.topic.c_str());
      Mqtt::subscribe(socket, sub.topic, sub.qos, sub.packet_identifier);
    }
  }
}

void Session::publish(string topic, string message, uint8_t qos, bool retain) {
  pub_outgoing_queue.push({qos, retain, 1, topic, message});
}

void Session::subscribe(string topic, uint8_t qos, uint16_t packet_identifier) {
  auto search =
      find_if(subscriptions.begin(), subscriptions.end(),
              [&topic](Subscribe &sub) { return sub.topic == topic; });
  if (search != subscriptions.end()) {
    return;
  }
  subscriptions.push_back({topic, qos, packet_identifier});
  try {
    Mqtt::subscribe(socket, topic, qos, packet_identifier);
  } catch (const std::exception &e) {
    isDisconnected = true;
  }
}
} // namespace Mqtt