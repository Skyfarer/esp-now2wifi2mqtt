#ifndef CONFIG_H
#define CONFIG_H

// Serial Configuration
#define SERIAL_BAUD 115200

// ESP-NOW Configuration
#define WIFI_CHANNEL 1
#define MAX_DATA_SIZE 250

// Broadcast MAC address (sends to all ESP-NOW devices)
// To target a specific device, replace with its MAC address
// Example: {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF}
#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

// Optional: Define specific peer MAC addresses here
// uint8_t peer1[] = {0x24, 0x6F, 0x28, 0x12, 0x34, 0x56};
// uint8_t peer2[] = {0x24, 0x6F, 0x28, 0x78, 0x9A, 0xBC};

#endif // CONFIG_H
