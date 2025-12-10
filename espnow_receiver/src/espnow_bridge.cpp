#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ArduinoJson.h>

// Configuration
#define WIFI_CHANNEL 1
#define MAX_DATA_SIZE 250
#define LED_PIN 2  // Onboard LED (active LOW - LOW=on, HIGH=off)

// LED blink control (for non-blocking LED indication)
unsigned long ledOnTime = 0;
bool ledState = false;

// Structure to hold weather station data (must match sender's struct)
typedef struct __attribute__((packed)) struct_weather_data {
  float temperature;
  float humidity;
  float pressure;
  float gas;
  float battery;
  uint32_t checksum;  // Simple checksum for data integrity
} struct_weather_data;

// Structure to hold generic received ESP-NOW data
typedef struct struct_message {
  char data[MAX_DATA_SIZE];
  uint8_t len;
} struct_message;

struct_message incomingData;

// Calculate checksum for weather data (must match sender's algorithm)
uint32_t calculateChecksum(struct_weather_data* data) {
  // Simple checksum: XOR all bytes of the float values
  uint32_t checksum = 0;
  uint8_t* ptr = (uint8_t*)data;

  // Calculate over all fields except the checksum field itself
  size_t dataSize = sizeof(struct_weather_data) - sizeof(uint32_t);

  for (size_t i = 0; i < dataSize; i++) {
    checksum ^= ptr[i];
    checksum = (checksum << 1) | (checksum >> 31);  // Rotate left
  }

  return checksum;
}

// Callback when data is received via ESP-NOW
// CRITICAL: This runs in interrupt context - keep it minimal!
void OnDataRecv(uint8_t *mac_addr, uint8_t *data, uint8_t len) {
  // Signal LED to blink (handled in main loop)
  ledOnTime = millis();
  ledState = true;
  digitalWrite(LED_PIN, LOW);  // Turn LED on (active LOW)

  // Format MAC address as string
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  // Check if this looks like weather data (struct size match)
  if (len == sizeof(struct_weather_data)) {
    struct_weather_data weatherData;
    memcpy(&weatherData, data, sizeof(struct_weather_data));

    // Validate checksum
    uint32_t receivedChecksum = weatherData.checksum;
    uint32_t calculatedChecksum = calculateChecksum(&weatherData);

    // Create JSON document
    StaticJsonDocument<256> doc;
    doc["mac"] = macStr;
    doc["type"] = "weather";

    if (receivedChecksum == 0) {
      doc["error"] = "missing_checksum";
      doc["checksum_valid"] = false;
    } else if (receivedChecksum != calculatedChecksum) {
      doc["error"] = "checksum_failed";
      doc["checksum_valid"] = false;
      doc["received_checksum"] = receivedChecksum;
      doc["calculated_checksum"] = calculatedChecksum;
    } else {
      doc["checksum_valid"] = true;
      doc["temperature"] = weatherData.temperature;
      doc["humidity"] = weatherData.humidity;
      doc["pressure"] = weatherData.pressure;
      doc["gas"] = weatherData.gas;
      doc["battery"] = weatherData.battery;
    }

    // Output JSON to USB serial
    serializeJson(doc, Serial);
    Serial.println();

  } else {
    // Handle as text/generic data
    StaticJsonDocument<512> doc;
    doc["mac"] = macStr;
    doc["type"] = "text";
    doc["len"] = len;

    // Try to parse as text
    bool isPrintable = true;
    for (int i = 0; i < len; i++) {
      if (data[i] < 32 && data[i] != 0) {
        isPrintable = false;
        break;
      }
    }

    if (isPrintable) {
      char textData[MAX_DATA_SIZE + 1];
      int copyLen = (len < MAX_DATA_SIZE) ? len : MAX_DATA_SIZE;
      memcpy(textData, data, copyLen);
      textData[copyLen] = '\0';
      doc["data"] = textData;
    } else {
      // Send as hex string for binary data
      doc["type"] = "binary";
      String hexStr = "";
      for (int i = 0; i < len && i < 64; i++) {
        char hex[3];
        sprintf(hex, "%02X", data[i]);
        hexStr += hex;
      }
      doc["data_hex"] = hexStr;
    }

    // Output JSON to USB serial
    serializeJson(doc, Serial);
    Serial.println();
  }
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

  // Set ESP-NOW role to SLAVE (receive only)
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);

  // Register receive callback only
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("[INFO] Bridge ready - ESP-NOW -> USB Serial (receive only)");
}

void setup() {
  // Initialize USB Serial for JSON output
  Serial.begin(115200);
  delay(1000);

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // Turn LED off initially (active LOW)

  Serial.println("\n\n=================================");
  Serial.println("ESP-NOW to USB/JSON Bridge");
  Serial.println("=================================");

  // Initialize ESP-NOW
  initESPNow();

  Serial.println("\n[READY] ESP-NOW -> USB Serial (Receive Only)");
  Serial.println("[READY] Incoming ESP-NOW messages output as JSON to USB serial");
  Serial.println("[READY] Onboard LED will flash when message received");
  Serial.println("=================================\n");
}

void loop() {
  // Handle LED blink timeout (turn off after 50ms)
  if (ledState && (millis() - ledOnTime >= 50)) {
    digitalWrite(LED_PIN, HIGH);  // Turn LED off (active LOW)
    ledState = false;
  }

  // Small delay to prevent overwhelming the system
  delay(10);
}
