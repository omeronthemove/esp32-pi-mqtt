#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "led_strip.h"
#include "driver/rmt_tx.h"
#include "esp_random.h"
#include "esp_timer.h"

// Wi-Fi and MQTT config
#define WIFI_SSID       "MY_WIFI"
#define WIFI_PASS       "MY_PASSWORD"
#define MQTT_BROKER_URI "mqtt://RASPBERRY_PI_IP"

// LED config
#define LED_GPIO        8
#define LED_COUNT       1

// Default state and constants
#define DEFAULT_ID      -99
#define MAX_TOPIC_LEN   64
#define ACK_TIMEOUT_MULTIPLIER 4

// Device states
typedef enum {
    STATE_DISCOVERY,
    STATE_WAITING_FOR_ID,
    STATE_ACTIVE
} device_state_t;

// Globals
static device_state_t current_state = STATE_DISCOVERY;
static int device_id = DEFAULT_ID;

static const char *TAG = "ESP32_SMART_DEVICE";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static led_strip_handle_t led_strip = NULL;

static char mac_str[18] = {0}; // MAC as string
static EventGroupHandle_t event_group;

// Event bits
#define WIFI_CONNECTED_BIT BIT0
#define ACK_REQUEST_BIT    BIT1
#define ID_RECEIVED_BIT    BIT2
#define ACK_RECEIVED_BIT   BIT3

static int msg_counter = 0;
static int missed_acks = 0;

float publish_interval_sec = 0;

// ------------------------------------------------------
// Helper: LED blink color based on RGB values
// ------------------------------------------------------
static void led_blink(uint8_t r, uint8_t g, uint8_t b) {
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
    vTaskDelay(pdMS_TO_TICKS(200));
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
}

// ------------------------------------------------------
// MQTT event handler: responds to data and connection events
// ------------------------------------------------------
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    if (event_id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT connected");

        // Subscribe to ACK request and ID response topics dynamically using our MAC
        char ack_request_topic[MAX_TOPIC_LEN];
        char id_response_topic[MAX_TOPIC_LEN];

        snprintf(ack_request_topic, sizeof(ack_request_topic), "esp32/ack_request/%s", mac_str);
        snprintf(id_response_topic, sizeof(id_response_topic), "esp32/id_response/%s", mac_str);

        esp_mqtt_client_subscribe(mqtt_client, ack_request_topic, 1);
        esp_mqtt_client_subscribe(mqtt_client, id_response_topic, 1);

        ESP_LOGI(TAG, "Subscribed to: %s and %s", ack_request_topic, id_response_topic);

    } else if (event_id == MQTT_EVENT_DATA) {
        // Extract topic and data
        char *topic = strndup(event->topic, event->topic_len);
        char *payload = strndup(event->data, event->data_len);

        // Handle responses based on topic
        if (strstr(topic, "ack_request/")) {
            xEventGroupSetBits(event_group, ACK_REQUEST_BIT);
        } else if (strstr(topic, "id_response/")) {
            device_id = atoi(payload);
            xEventGroupSetBits(event_group, ID_RECEIVED_BIT);
        } else if (strstr(topic, "ack/")) {
            xEventGroupSetBits(event_group, ACK_RECEIVED_BIT);
        }

        free(topic);
        free(payload);
    }
}

// ------------------------------------------------------
// MQTT setup and connection
// ------------------------------------------------------
static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// ------------------------------------------------------
// Wi-Fi event handler
// ------------------------------------------------------
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(event_group, WIFI_CONNECTED_BIT);
    }
}

// ------------------------------------------------------
// Wi-Fi initialization
// ------------------------------------------------------
static void wifi_init_sta(void) {
    event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {.ssid = WIFI_SSID, .password = WIFI_PASS},
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    // Wait for Wi-Fi connection
    xEventGroupWaitBits(event_group, WIFI_CONNECTED_BIT, false, true, pdMS_TO_TICKS(10000));
}

// ------------------------------------------------------
// LED initialization
// ------------------------------------------------------
static void init_led_strip(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    led_strip_clear(led_strip);
}

// ------------------------------------------------------
// Main application logic
// ------------------------------------------------------
void app_main(void) {
    nvs_flash_init();
    wifi_init_sta();
    init_led_strip();

    // Get MAC address and convert to string
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    mqtt_app_start();

    // Random publishing interval (2–5 seconds)
    publish_interval_sec = 2.0 + ((float)esp_random() / UINT32_MAX) * 3.0;
    TickType_t interval_ticks = pdMS_TO_TICKS((int)(publish_interval_sec * 1000));
    int64_t last_ack_time = esp_timer_get_time();  // microseconds

    while (true) {
        if (current_state == STATE_DISCOVERY) {
            // Step 1: Broadcast discovery message
            char topic[64], msg[64];
            snprintf(topic, sizeof(topic), "esp32/register/%s", mac_str);
            snprintf(msg, sizeof(msg), "Trying to connect to Pi");

            ESP_LOGW(TAG, "[DISCOVERY] Publishing: %s → %s", topic, msg);
            esp_mqtt_client_publish(mqtt_client, topic, msg, 0, 1, 0);
            led_blink(255, 0, 0);  // Red

            // Wait for ack_request from Pi
            EventBits_t bits = xEventGroupWaitBits(event_group, ACK_REQUEST_BIT, pdTRUE, pdFALSE, interval_ticks);
            if (bits & ACK_REQUEST_BIT) {
                ESP_LOGI(TAG, "[DISCOVERY] Received ack_request from Pi");
                current_state = STATE_WAITING_FOR_ID;
            }

        } else if (current_state == STATE_WAITING_FOR_ID) {
            // Step 2: Waiting for ID assignment
            ESP_LOGI(TAG, "[WAITING] Blinking orange while waiting for ID...");
            led_blink(255, 128, 0);  // Orange

            EventBits_t bits = xEventGroupWaitBits(event_group, ID_RECEIVED_BIT, pdTRUE, pdFALSE, interval_ticks);
            if (bits & ID_RECEIVED_BIT) {
                // Subscribe to ack topic once ID is received
                char ack_topic[MAX_TOPIC_LEN];
                snprintf(ack_topic, sizeof(ack_topic), "esp32/ack/%d", device_id);
                esp_mqtt_client_subscribe(mqtt_client, ack_topic, 1);
                ESP_LOGI(TAG, "[WAITING] Received ID from Pi: %d", device_id);
                current_state = STATE_ACTIVE;
                msg_counter = 0;
                missed_acks = 0;
                last_ack_time = esp_timer_get_time();
            }

        } else if (current_state == STATE_ACTIVE) {
            // Step 3: Regular publishing
            char topic[64], msg[64];
            snprintf(topic, sizeof(topic), "esp32/id/%d", device_id);
            snprintf(msg, sizeof(msg), "Hello Pi! I am ESP32 Id: %d", device_id);

            ESP_LOGI(TAG, "[ACTIVE] Publishing: %s → %s", topic, msg);
            esp_mqtt_client_publish(mqtt_client, topic, msg, 0, 1, 0);
            led_blink(0, 255, 0);  // Green
            msg_counter++;

            // If ACK received, reset timers
            if (xEventGroupGetBits(event_group) & ACK_RECEIVED_BIT) {
                xEventGroupClearBits(event_group, ACK_RECEIVED_BIT);
                last_ack_time = esp_timer_get_time();
                ESP_LOGI(TAG, "[ACTIVE] Received ACK from Pi");
                missed_acks = 0;
            }

            // Check how long it's been since last ACK
            int64_t now = esp_timer_get_time();
            int64_t elapsed_ms = (now - last_ack_time) / 1000;

            if (elapsed_ms > (ACK_TIMEOUT_MULTIPLIER * publish_interval_sec * 1000)) {
                missed_acks++;
                ESP_LOGW(TAG, "[ACTIVE] ACK timeout. Missed count: %d", missed_acks);
                if (missed_acks >= 1) {
                    ESP_LOGE(TAG, "[RECOVERY] Lost connection. Returning to discovery mode.");
                    current_state = STATE_DISCOVERY;
                    device_id = DEFAULT_ID;
                    continue;
                }
            }

            vTaskDelay(interval_ticks);  // Wait before next message
        }
    }
}