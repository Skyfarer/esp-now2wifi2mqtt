# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP-NOW to WiFi/MQTT Bridge for ESP8266 devices. This project enables communication between ESP-NOW mesh networks and WiFi/MQTT infrastructure using two separate ESP8266 boards connected via serial lines.

**Architecture**: Two-board design where one board handles ESP-NOW communication and the other handles WiFi/MQTT, connected via TX/RX serial lines. This eliminates WiFi channel conflicts between ESP-NOW and WiFi infrastructure.

## Build System: PlatformIO

This project uses PlatformIO for building and uploading firmware. All commands should be run from the project root.

### Common Commands

```bash
# Build a specific environment
pio run -e espnow-bridge    # Build ESP-NOW to Serial bridge
pio run -e wifi-bridge      # Build WiFi/MQTT bridge

# Upload firmware to device
pio run -e espnow-bridge -t upload
pio run -e wifi-bridge -t upload

# Monitor serial output (115200 baud)
pio device monitor

# Upload and monitor in one command
pio run -e wifi-bridge -t upload -t monitor

# Clean build files
pio run -t clean
```

## Architecture

### Two-Board Serial Bridge Design

The system uses TWO separate ESP8266 boards connected via serial (TX/RX) lines:

**Board 1: ESP-NOW Bridge** (espnow-bridge)
- Handles all ESP-NOW communication
- No WiFi connection (ESP-NOW only)
- Source: `src/espnow_bridge.cpp`
- Serial format OUT: `MAC|LEN|DATA\n` (e.g., `AABBCCDDEEFF|011|Hello World`)
- Serial format IN:
  - Specific device: `AABBCCDDEEFF|message data\n` (12 hex chars + pipe + data)
  - Broadcast: `message data\n` (plain text without MAC prefix)

**Board 2: WiFi/MQTT Bridge** (wifi-bridge)
- Handles WiFi and MQTT communication
- No ESP-NOW functionality
- Source: `src/wifi_bridge.cpp`
- Dependencies: PubSubClient (MQTT), ArduinoJson
- Parses incoming serial data and publishes to MQTT as JSON
- Receives MQTT messages and forwards to serial

### Hardware Wiring

**Current Implementation: SoftwareSerial on D1/D2**

The code uses SoftwareSerial for inter-board communication, allowing USB serial to be used for debugging on both boards simultaneously:
- **Board 1 TX** = GPIO4 (D2)
- **Board 1 RX** = GPIO5 (D1)
- **Board 2 TX** = GPIO4 (D2)
- **Board 2 RX** = GPIO5 (D1)

**Wiring:**
```
Board 1 (ESP-NOW)        Board 2 (WiFi/MQTT)
    D2 (GPIO4/TX)   ---->     D1 (GPIO5/RX)
    D1 (GPIO5/RX)   <----     D2 (GPIO4/TX)
    GND             ----      GND
    VIN/5V          ----      External 5V power supply (shared or separate)
```

**Benefits of this approach:**
- USB serial (TX/RX) remains available for debugging on both boards
- You can monitor both boards simultaneously during development
- No need to disconnect USB or switch between boards

**Power Options:**
1. **Development**: Both boards can be powered via USB
2. **Production**: Power both boards externally (5V to VIN or 3.3V to 3V3)

### Message Flow

```
ESP-NOW Device -> Board 1 (ESP-NOW) -> Serial -> Board 2 (WiFi) -> MQTT Broker
                                                                      |
ESP-NOW Device <- Board 1 (ESP-NOW) <- Serial <- Board 2 (WiFi) <- MQTT Broker
```

**Serial Protocol**:
- Board 1 to Board 2: `AABBCCDDEEFF|025|message data here\n` (MAC address in hex, length, data)
- Board 2 to Board 1:
  - Specific device: `AABBCCDDEEFF|message data\n` (MAC in hex + pipe + data)
  - Broadcast: `message data\n` (plain text without MAC)

**MQTT Topics**:
- **From ESP-NOW devices**: `espnow/from_device/AA:BB:CC:DD:EE:FF` (each device has unique topic)
- **To ESP-NOW devices**:
  - Specific device: `espnow/to_device/AA:BB:CC:DD:EE:FF` (targets one device)
  - Broadcast: `espnow/to_device` (sends to all devices)

**MQTT JSON Format** (Board 2 publishes):
```json
{
  "mac": "AA:BB:CC:DD:EE:FF",
  "data": "message data here",
  "len": 25
}
```

### Key Architecture Notes

**Platform**: ESP8266 (NodeMCU v2) running Arduino framework

**Why Two Boards?**: ESP8266 cannot reliably run ESP-NOW and WiFi simultaneously on different channels. By separating ESP-NOW and WiFi to different boards, there are no channel conflicts.

**Serial Baud Rate**: Both boards use 115200 baud (defined in `SERIAL_BAUD`)

**Data Limits**: ESP-NOW messages are limited to 250 bytes (`MAX_DATA_SIZE`)

**Broadcast Mode**: Board 1 uses ESP-NOW broadcast (`FF:FF:FF:FF:FF:FF`) for mesh network compatibility

## Configuration

Before uploading `wifi-bridge`, update the following in `src/wifi_bridge.cpp`:

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "YOUR_MQTT_BROKER";
const int mqtt_port = 1883;
const char* mqtt_user = "";  // Optional
const char* mqtt_password = "";  // Optional
```

MQTT topics:
- **From ESP-NOW**: `espnow/from_device/AA:BB:CC:DD:EE:FF` (each device publishes to its own topic)
  - Subscribe to `espnow/from_device/#` to receive from all devices
- **To ESP-NOW**:
  - Specific device: `espnow/to_device/AA:BB:CC:DD:EE:FF` (targets one device)
  - Broadcast: `espnow/to_device` (sends to all devices)

## Build Flags

The wifi-bridge environment sets:
- `MQTT_MAX_PACKET_SIZE=512`: Allows larger MQTT payloads
- `MQTT_KEEPALIVE=30`: 30-second MQTT keepalive interval
