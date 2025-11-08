#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>

// Configuration
#define SERIAL_BAUD 115200
#define WIFI_CHANNEL 1
#define MAX_DATA_SIZE 250

// Broadcast MAC address (sends to all ESP-NOW devices)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Buffer for incoming serial data
String serialBuffer = "";
bool dataReady = false;

// Structure to hold received ESP-NOW data
typedef struct struct_message {
  char data[MAX_DATA_SIZE];
  uint8_t len;
} struct_message;

struct_message incomingData;

// Callback when data is sent via ESP-NOW
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  if (sendStatus == 0) {
    Serial.println("[ESP-NOW] Delivery success");
  } else {
    Serial.println("[ESP-NOW] Delivery fail");
  }
}

// Callback when data is received via ESP-NOW
void OnDataRecv(uint8_t *mac_addr, uint8_t *data, uint8_t len) {
  // Print MAC address of sender
  Serial.print("[ESP-NOW] Received from: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac_addr[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" | Data: ");

  // Print the received data to serial
  for (int i = 0; i < len; i++) {
    Serial.write(data[i]);
  }
  Serial.println();
}

void initESPNow() {
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Print MAC address
  Serial.print("[INFO] MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Initialize ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("[ERROR] ESP-NOW init failed");
    ESP.restart();
  }

  Serial.println("[INFO] ESP-NOW initialized");

  // Set ESP-NOW role to COMBO (can send and receive)
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);

  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Add peer (broadcast address)
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, NULL, 0);

  Serial.println("[INFO] Bridge ready - Serial <-> ESP-NOW");
}

void setup() {
  // Initialize Serial
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  Serial.println("\n\n=================================");
  Serial.println("ESP-NOW <-> Serial Bridge");
  Serial.println("=================================");

  // Initialize ESP-NOW
  initESPNow();

  Serial.println("\n[READY] Send data via Serial to broadcast via ESP-NOW");
  Serial.println("[READY] ESP-NOW messages will appear on Serial");
}

void loop() {
  // Read from Serial and send to ESP-NOW
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        dataReady = true;
      }
    } else {
      serialBuffer += c;

      // Limit buffer size
      if (serialBuffer.length() >= MAX_DATA_SIZE) {
        dataReady = true;
      }
    }
  }

  // Send buffered data via ESP-NOW
  if (dataReady) {
    uint8_t dataLen = serialBuffer.length();
    if (dataLen > MAX_DATA_SIZE) {
      dataLen = MAX_DATA_SIZE;
    }

    uint8_t dataToSend[MAX_DATA_SIZE];
    serialBuffer.getBytes(dataToSend, dataLen + 1);

    Serial.print("[SERIAL->ESP-NOW] Sending: ");
    Serial.println(serialBuffer);

    // Send data via ESP-NOW
    esp_now_send(broadcastAddress, dataToSend, dataLen);

    // Clear buffer
    serialBuffer = "";
    dataReady = false;
  }

  // Small delay to prevent overwhelming the system
  delay(10);
}
