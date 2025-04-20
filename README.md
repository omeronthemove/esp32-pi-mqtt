# MQTT-Based Communication Protocol Using ESP32-C3s and Raspberry Pi

This project implements a lightweight and resilient MQTT-based communication protocol between **ESP32-C3 microcontrollers** and a **Raspberry Pi 5**. The ESP32s dynamically requests ID from the Raspberry Pi, then periodically sends messages while monitoring for acknowledgments. The Raspberry Pi assigns IDs, acknowledges device messages, and manages a persistent device registry.

## Features

### ESP32 (Client)
- Connects to a Wi-Fi network and MQTT broker
- Starts in discovery mode with a default ID (`-99`)
- Publishes registration messages until acknowledged by the Raspberry Pi
- Receives a unique ID and enters active messaging mode
- Periodically publishes heartbeat messages to the broker
- Detects lost communication and returns to discovery mode
- Uses onboard LED to visually indicate state (red, orange, green)

### Raspberry Pi (Server)
- Runs Mosquitto MQTT broker
- Receives device registrations and assigns unique IDs per MAC address
- Publishes `ack_request` and `id_response` messages
- Tracks devices in a local file (`device_registry.json`)
- Sends `ack` messages every third message received from each ESP32

## Communication Topics

| Publisher | Topic Pattern                  | Payload Example                         |
|-----------|--------------------------------|------------------------------------------|
| ESP32     | `esp32/register/<mac>`         | `Trying to connect to Pi`               |
| Raspberry Pi | `esp32/ack_request/<mac>`  | `ack_request`                            |
| Raspberry Pi | `esp32/id_response/<mac>`  | `x` (assigned ID)                        |
| ESP32     | `esp32/id/<id>`                | `Hello Pi! I am ESP32 Id: x`            |
| Raspberry Pi | `esp32/ack/<id>`           | `ack` (sent every 3rd message)          |

## LED Behavior on ESP32

| State            | LED Color |
|------------------|-----------|
| Discovery        | Red       |
| Waiting for ID   | Orange    |
| Active           | Green     |

## Project Structure

```text
esp32-pi-mqtt/
├── esp32/                               # ESP32 (ESP-IDF) project folder
│   ├── CMakeLists.txt                   # Top-level CMake file for ESP-IDF
│
│   ├── main/                            # ESP32 application source code
│   │   ├── main.c                       # Main application logic
│   │   ├── CMakeLists.txt               # Build config for this component
│   │   ├── idf_component.yml            # Component manifest
│
├── pi_mqtt_server.py                   # MQTT server script (Raspberry Pi)
├── README.md                           # Project documentation
```

## Getting Started

### Raspberry Pi Setup

1. **Install Mosquitto** (MQTT broker):

   ```bash
   sudo apt update
   sudo apt install -y mosquitto mosquitto-clients
    ```

2.	The server will automatically create and update device_registry.json as ESP32 devices connect.

### ESP32 Setup

1.	Open esp32_main.c and configure your Wi-Fi and broker URI:

    ```c
    #define WIFI_SSID       "YourNetworkName"
    #define WIFI_PASS       "YourPassword"
    #define MQTT_BROKER_URI "mqtt://<raspberry-pi-ip>"
    ```

2.	Flash the code to your ESP32 using ESP-IDF or PlatformIO.
3.	Open the serial monitor to observe the state transitions and message logs.

## Requirements

•	ESP32-C3-DevKitC-02U or compatible ESP32 development board

•	Raspberry Pi 5 with Python 3 and Mosquitto installed

•	Local Wi-Fi network that both devices can access

•	MQTT broker reachable from ESP32


## Example Log Output

### ESP32
```
[DISCOVERY] Publishing: esp32/register/AA:BB:22:1B:C2:D3 → Trying to connect to Pi
[DISCOVERY] Received ack_request from Pi
[WAITING] Received ID from Pi: 1
[ACTIVE] Publishing: esp32/id/1 → Hello Pi! I am ESP32 Id: 1
[ACTIVE] Received ACK from Pi
```

### Raspberry Pi
```
[DISCOVERY] From MAC AA:BB:22:1B:C2:D3: Trying to connect to Pi
[BROKER] Assigned new ID 1 to MAC AA:BB:22:1B:C2:D3
[BROKER] Sent ack_request → esp32/ack_request/AA:BB:22:1B:C2:D3
[BROKER] Sent id_response → esp32/id_response/AA:BB:22:1B:C2:D3: 1
[HELLO] ID 1 says: Hello Pi! I am ESP32 Id: 1
[ACK] Sent to ID 1 → esp32/ack/1
```

## Limitations and Future Directions

### Current Limitations

- **ID is Not Persistent on ESP32**  
  The assigned ID is stored in volatile memory. After reboot, the ESP32 re-registers and starts the discovery process again.

- **Fixed Maximum Device Count**  
  The Raspberry Pi limits registration to a predefined number of devices. This is hardcoded and not dynamically adjustable.

- **No Security in MQTT Communication**  
  All MQTT messages are transmitted in plaintext without authentication or encryption. This setup is suitable for local networks but not secure for broader deployments.

---

### Future Directions

- **Persistent ID Storage**  
  Use NVS (non-volatile storage) on the ESP32 to store its assigned ID and skip re-registration after reboot.

- **Basic Authentication and TLS Support**  
  Add MQTT username/password authentication and optionally enable TLS to secure the communication.

- **Web-Based Device Monitor**  
  Build a simple dashboard on the Raspberry Pi to visualize connected devices, their states, and message activity.

- **Device Timeout Detection**  
  Implement a timeout mechanism on the Pi to track inactive or disconnected ESP32 devices over time.


