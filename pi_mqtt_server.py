import paho.mqtt.client as mqtt
import json
import os

# ========================
# Configuration
# ========================
BROKER_ADDRESS = "localhost"
BROKER_PORT = 1883
MAX_DEVICES = 4
STATE_FILE = "device_registry.json"

# ========================
# Device State
# ========================
mac_to_id = {}
id_to_mac = {}
message_counters = {}
next_id = 1

# ========================
# Registry Persistence
# ========================
def load_registry():
    global mac_to_id, id_to_mac, next_id
    if os.path.exists(STATE_FILE):
        with open(STATE_FILE, "r") as f:
            data = json.load(f)
            mac_to_id.update(data.get("mac_to_id", {}))
            id_to_mac.update({int(v): k for k, v in mac_to_id.items()})
            next_id = max([int(i) for i in id_to_mac.keys()] + [0]) + 1
            print(f"[REGISTRY] Loaded {len(mac_to_id)} device(s). Next ID: {next_id}")

def save_registry():
    with open(STATE_FILE, "w") as f:
        json.dump({"mac_to_id": mac_to_id}, f, indent=2)
        print("[REGISTRY] Saved device assignments to disk.")

# ========================
# MQTT Callbacks
# ========================
def on_connect(client, userdata, flags, rc, properties=None):
    print(f"[MQTT] Connected with result code {rc}")
    client.subscribe("esp32/register/+")
    client.subscribe("esp32/id/+")

def on_message(client, userdata, msg):
    global next_id

    topic = msg.topic
    payload = msg.payload.decode().strip()

    if topic.startswith("esp32/register/"):
        mac = topic.split("/")[-1]
        print(f"[DISCOVERY] From MAC {mac}: {payload}")

        if mac not in mac_to_id:
            if len(mac_to_id) >= MAX_DEVICES:
                print(f"[BROKER] Max devices reached. Ignoring {mac}")
                return

            assigned_id = next_id
            mac_to_id[mac] = assigned_id
            id_to_mac[assigned_id] = mac
            message_counters[assigned_id] = 0
            next_id += 1
            save_registry()

            print(f"[BROKER] Assigned new ID {assigned_id} to MAC {mac}")
        else:
            assigned_id = mac_to_id[mac]
            print(f"[BROKER] Known MAC {mac}, re-sending ID {assigned_id}")

        ack_request_topic = f"esp32/ack_request/{mac}"
        client.publish(ack_request_topic, "ack_request")
        print(f"[BROKER] Sent ack_request → {ack_request_topic}")

        id_response_topic = f"esp32/id_response/{mac}"
        client.publish(id_response_topic, str(assigned_id))
        print(f"[BROKER] Sent id_response → {id_response_topic}: {assigned_id}")

    elif topic.startswith("esp32/id/"):
        try:
            device_id = int(topic.split("/")[-1])
        except ValueError:
            print(f"[ERROR] Invalid ID in topic: {topic}")
            return

        print(f"[HELLO] From ID {device_id}: {payload}")

        if device_id not in message_counters:
            message_counters[device_id] = 0
        message_counters[device_id] += 1

        if message_counters[device_id] % 3 == 0:
            ack_topic = f"esp32/ack/{device_id}"
            client.publish(ack_topic, "ack")
            print(f"[ACK] Sent to ID {device_id} → {ack_topic}")

# ========================
# Main Entry Point
# ========================
def main():
    load_registry()

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(BROKER_ADDRESS, BROKER_PORT, 60)

    print("MQTT Pi Server started. Waiting for ESP32 messages...")
    client.loop_forever()

if __name__ == "__main__":
    main()