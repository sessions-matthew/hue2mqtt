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

void Session::handleSocket() {
  uint8_t header[1];

  if (socket.is_open() == false) {
    syslog(LOG_ERR, "Socket was closed, reconnecting to server...");
    isConnected = false;
    socket.connect(boost::asio::ip::tcp::endpoint(
        boost::asio::ip::address::from_string(addr), port));
  }

  if (!isConnected) {
    syslog(LOG_NOTICE, "Waiting for MQTT connection...");
    if (!timeout0) {
      timeout0 = time(NULL);
    }
    if (time(NULL) - timeout0 > 10) {
      timeout0 = 0;
      syslog(LOG_ERR, "Connection timeout, sending MQTT CONN...");
      Mqtt::connect(socket, client_id, username, password);
    }
  }

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
      cout << "Received CONNACK" << endl;
      ConnAck connack = connAckFromBytes(header[0], len, recv);
      cout << "Session Expiry Interval: "
           << (int)connack.session_expiry_interval
           << " Receive Maximum: " << (int)connack.receive_maximum
           << " Maximum QoS: " << (int)connack.maximum_qos
           << " Retain Available: " << connack.retain_available
           << " Maximum Packet Size: " << (int)connack.maximum_packet_size
           << " Assigned Client Identifier: "
           << connack.assigned_client_identifier
           << " Topic Alias Maximum: " << (int)connack.topic_alias_maximum
           << " Reason String: " << connack.reason_string
           << " User Property: " << connack.user_property
           << " Wildcard Subscription Available: "
           << connack.wildcard_subscription_available
           << " Subscription Identifier Available: "
           << connack.subscription_identifier_available
           << " Shared Subscription Available: "
           << connack.shared_subscription_available << endl;
      isConnected = true;
      timeout0 = 0;
    } else if (command == ControlPacketType::PUBLISH) {
      Publish publish = publishFromBytes(header[0], len, recv);
      if (debug) {
        cout << "Received PUBLISH" << endl;
        cout << "QoS: " << (int)publish.qos << " Retain: " << publish.retain
             << " Packet Identifier: " << publish.packet_identifier
             << " Topic: " << publish.topic << " Message: " << publish.message
             << endl;
      }

      publish_queue.push(publish);
    } else if (command == ControlPacketType::PUBACK) {
      cout << "Received PUBACK" << endl;
    } else if (command == ControlPacketType::PUBREC) {
      cout << "Received PUBREC" << endl;
    } else if (command == ControlPacketType::PUBREL) {
      cout << "Received PUBREL" << endl;
    } else if (command == ControlPacketType::PUBCOMP) {
      cout << "Received PUBCOMP" << endl;
    } else if (command == ControlPacketType::SUBACK) {
      cout << "Received SUBACK" << endl;
    } else if (command == ControlPacketType::PINGREQ) {
      cout << "Received PINGREQ" << endl;
    } else if (command == ControlPacketType::PINGRESP) {
      cout << "Received PINGRESP" << endl;
    } else if (command == ControlPacketType::DISCONNECT) {
      cout << "Received DISCONNECT" << endl;
      isConnected = false;
    } else {
      cout << "Unhandled response type for " << command << endl;
    }

    if (debug) {
      for (int i = 0; i < len; i++) {
        cout << " " << (unsigned int)recv[i];
      }
      cout << endl;
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

  socket.connect(boost::asio::ip::tcp::endpoint(
      boost::asio::ip::address::from_string(addr), port));

  connect();
}
void Session::connect() {
  int timeout = 0;
  Mqtt::connect(socket, client_id, username, password);
  while (!isConnected) {
    if (timeout++ > 10) {
      timeout = 0;
      syslog(LOG_ERR, "Connection timeout...");

      Mqtt::connect(socket, client_id, username, password);
    }

    syslog(LOG_NOTICE, "Waiting for MQTT connection...");
    handleSocket();
    sleep(1);
  }
}
void Session::publish(string topic, string message, uint8_t qos, bool retain) {
  while (!isConnected) {
    handleSocket();
    sleep(1);
  }
  Mqtt::publish(socket, topic, message, qos, retain);
}
void Session::subscribe(string topic, uint8_t qos, uint16_t packet_identifier) {
  while (!isConnected) {
    handleSocket();
    sleep(1);
  }
  Mqtt::subscribe(socket, topic, qos, packet_identifier);
}
} // namespace Mqtt