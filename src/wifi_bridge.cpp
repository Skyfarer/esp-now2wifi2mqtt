#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFi Configuration - UPDATE THESE
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// MQTT Configuration - UPDATE THESE
const char* mqtt_server = "YOUR_MQTT_BROKER";
const int mqtt_port = 1883;
const char* mqtt_user = "";  // Leave empty if no auth
const char* mqtt_password = "";  // Leave empty if no auth
const char* mqtt_client_id = "ESP8266_WiFi_Bridge";

// MQTT Topics
const char* topic_espnow_to_mqtt = "espnow/from_device";  // Serial (from ESP-NOW bridge) -> MQTT
const char* topic_mqtt_to_espnow = "espnow/to_device";    // MQTT -> Serial (to ESP-NOW bridge)

// Configuration
#define SERIAL_BAUD 115200
#define MAX_DATA_SIZE 250
#define MQTT_BUFFER_SIZE 512

WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long lastReconnectAttempt = 0;
bool wifiConnected = false;

// Buffer for incoming serial data
String serialBuffer = "";

// Parse and forward serial data from ESP-NOW bridge to MQTT
// Expected format: MAC|LEN|DATA\n (e.g., "AABBCCDDEEFF|011|Hello World")
void processSerialData(String& line) {
  // Find delimiters
  int firstPipe = line.indexOf('|');
  int secondPipe = line.indexOf('|', firstPipe + 1);

  if (firstPipe == -1 || secondPipe == -1) {
    Serial.println("[ERROR] Invalid serial format");
    return;
  }

  // Extract MAC address (12 hex chars -> formatted with colons)
  String macHex = line.substring(0, firstPipe);
  if (macHex.length() != 12) {
    Serial.println("[ERROR] Invalid MAC length");
    return;
  }

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
           macHex[0], macHex[1], macHex[2], macHex[3], macHex[4], macHex[5],
           macHex[6], macHex[7], macHex[8], macHex[9], macHex[10], macHex[11]);

  // Extract length
  String lenStr = line.substring(firstPipe + 1, secondPipe);
  int dataLen = lenStr.toInt();

  // Extract data (only use the first dataLen bytes)
  String fullData = line.substring(secondPipe + 1);
  String data = fullData.substring(0, dataLen);

  // Clean up data by truncating at first null byte
  // This handles cases where sender includes garbage/uninitialized memory
  int nullPos = data.indexOf('\0');
  if (nullPos != -1) {
    data = data.substring(0, nullPos);
  }
  int actualLen = data.length();

  Serial.print("[SERIAL->MQTT] MAC: ");
  Serial.print(macStr);
  Serial.print(" | Data: ");
  Serial.println(data);

  // Forward to MQTT if connected
  if (mqtt.connected()) {
    StaticJsonDocument<MQTT_BUFFER_SIZE> doc;
    doc["mac"] = macStr;
    doc["data"] = data;
    doc["len"] = actualLen;

    char jsonBuffer[MQTT_BUFFER_SIZE];
    serializeJson(doc, jsonBuffer);

    // Build topic with MAC address: espnow/from_device/AA:BB:CC:DD:EE:FF
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/%s", topic_espnow_to_mqtt, macStr);

    if (mqtt.publish(topic, jsonBuffer)) {
      Serial.print("[MQTT] Published to: ");
      Serial.println(topic);
    } else {
      Serial.println("[MQTT] Publish failed");
    }
  } else {
    Serial.println("[MQTT] Not connected - dropped");
  }
}

// MQTT callback for incoming messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT->SERIAL] Topic: ");
  Serial.print(topic);
  Serial.print(" | Data: ");

  // Copy payload to buffer
  char message[MAX_DATA_SIZE + 1];
  unsigned int copyLen = (length < MAX_DATA_SIZE) ? length : MAX_DATA_SIZE;
  memcpy(message, payload, copyLen);
  message[copyLen] = '\0';

  Serial.println(message);

  // Extract MAC address from topic if present
  // Expected format: espnow/to_device/AA:BB:CC:DD:EE:FF
  String topicStr = String(topic);
  int lastSlash = topicStr.lastIndexOf('/');

  if (lastSlash != -1 && lastSlash < topicStr.length() - 1) {
    String macStr = topicStr.substring(lastSlash + 1);

    // Check if it looks like a MAC address (17 chars with colons)
    if (macStr.length() == 17 && macStr.indexOf(':') != -1) {
      // Remove colons from MAC address for serial format
      String macHex = "";
      for (int i = 0; i < macStr.length(); i++) {
        if (macStr[i] != ':') {
          macHex += macStr[i];
        }
      }

      // Send to serial in format: MAC|DATA\n
      // The ESP-NOW bridge will parse this and send to specific device
      Serial.print(macHex);
      Serial.print("|");
      Serial.println(message);

      Serial.print("[SERIAL] Sent to specific MAC: ");
      Serial.println(macStr);
    } else {
      // No valid MAC in topic, send as broadcast (just data)
      Serial.println(message);
      Serial.println("[SERIAL] Sent as broadcast");
    }
  } else {
    // No MAC in topic, send as broadcast (just data)
    Serial.println(message);
    Serial.println("[SERIAL] Sent as broadcast");
  }
}

void setupWiFi() {
  delay(10);
  Serial.println("\n[WiFi] Connecting...");
  Serial.print("[WiFi] SSID: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n[WiFi] Connected!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WiFi] MAC: ");
    Serial.println(WiFi.macAddress());
  } else {
    Serial.println("\n[WiFi] Connection failed!");
    wifiConnected = false;
  }
}

boolean reconnectMQTT() {
  Serial.print("[MQTT] Connecting to ");
  Serial.print(mqtt_server);
  Serial.print(":");
  Serial.println(mqtt_port);

  boolean connected;
  if (strlen(mqtt_user) > 0) {
    connected = mqtt.connect(mqtt_client_id, mqtt_user, mqtt_password);
  } else {
    connected = mqtt.connect(mqtt_client_id);
  }

  if (connected) {
    Serial.println("[MQTT] Connected!");

    // Subscribe to base topic (broadcast)
    Serial.print("[MQTT] Subscribing to: ");
    Serial.println(topic_mqtt_to_espnow);
    mqtt.subscribe(topic_mqtt_to_espnow);

    // Subscribe to device-specific topics with wildcard
    char wildcardTopic[64];
    snprintf(wildcardTopic, sizeof(wildcardTopic), "%s/#", topic_mqtt_to_espnow);
    Serial.print("[MQTT] Subscribing to: ");
    Serial.println(wildcardTopic);
    mqtt.subscribe(wildcardTopic);

    // Publish connection message
    StaticJsonDocument<200> doc;
    doc["status"] = "connected";
    doc["mac"] = WiFi.macAddress();
    doc["ip"] = WiFi.localIP().toString();

    char jsonBuffer[200];
    serializeJson(doc, jsonBuffer);
    mqtt.publish(topic_espnow_to_mqtt, jsonBuffer);

    Serial.println("[MQTT] Ready");
  } else {
    Serial.print("[MQTT] Failed, rc=");
    Serial.println(mqtt.state());
  }

  return connected;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  Serial.println("\n\n=================================");
  Serial.println("Serial <-> WiFi/MQTT Bridge");
  Serial.println("=================================");

  // Setup WiFi
  setupWiFi();

  if (wifiConnected) {
    // Setup MQTT
    mqtt.setServer(mqtt_server, mqtt_port);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(MQTT_BUFFER_SIZE);

    // Initial MQTT connection
    if (reconnectMQTT()) {
      Serial.println("\n[READY] Bridge Active: Serial <-> MQTT");
      Serial.println("[READY] Waiting for serial data from ESP-NOW bridge...");
    }
  } else {
    Serial.println("\n[ERROR] Cannot initialize without WiFi");
  }
}

void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) {
      Serial.println("[WiFi] Lost connection! Reconnecting...");
      wifiConnected = false;
    }
    setupWiFi();
    delay(5000);
    return;
  } else if (!wifiConnected) {
    wifiConnected = true;
    Serial.println("[WiFi] Reconnected!");
  }

  // Handle MQTT connection
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (reconnectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqtt.loop();
  }

  // Read from serial (data from ESP-NOW bridge)
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        processSerialData(serialBuffer);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;

      // Prevent buffer overflow
      if (serialBuffer.length() >= MQTT_BUFFER_SIZE) {
        Serial.println("[ERROR] Serial buffer overflow - clearing");
        serialBuffer = "";
      }
    }
  }

  delay(10);
}
