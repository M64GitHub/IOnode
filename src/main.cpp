/**
 * @file main.cpp
 * @brief IOnode - NATS Hardware Node
 *
 * A lightweight ESP32 firmware that turns any ESP32 into a NATS-addressable
 * hardware node. Provides GPIO, ADC, PWM, UART, and system access via NATS
 * request/reply, plus a device registry for named sensors and actuators.
 *
 * Use 115200 baud serial. /help for commands.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#if !defined(CONFIG_IDF_TARGET_ESP32)
#include "driver/temperature_sensor.h"
#endif
#include "devices.h"
#include "nats_hal.h"
#include "setup_portal.h"
#include "web_config.h"
#include "version.h"
#include <nats_atoms.h>

/*============================================================================
 * Configuration
 *============================================================================*/

#define LED_BRIGHTNESS          20
#define SERIAL_BUF_SIZE         256
#define HEARTBEAT_INTERVAL_MS   3000
#define NATS_RECONNECT_DELAY_MS 30000

/* Runtime config - loaded from LittleFS */
char cfg_wifi_ssid[64];
char cfg_wifi_pass[64];
char cfg_device_name[32];
char cfg_nats_host[64];
int  cfg_nats_port = 4222;
char cfg_timezone[64];

static void configDefaults() {
    cfg_wifi_ssid[0] = '\0';
    cfg_wifi_pass[0] = '\0';
    strncpy(cfg_device_name, "ionode-01", sizeof(cfg_device_name));
    cfg_nats_host[0] = '\0';
    cfg_nats_port = 4222;
    strncpy(cfg_timezone, "UTC0", sizeof(cfg_timezone));
}

/*============================================================================
 * LED Helpers
 *============================================================================*/

static uint8_t ledBrightness = LED_BRIGHTNESS;

void led(uint8_t r, uint8_t g, uint8_t b) {
    r = (uint8_t)((r * ledBrightness) / 255);
    g = (uint8_t)((g * ledBrightness) / 255);
    b = (uint8_t)((b * ledBrightness) / 255);
#ifdef RGB_BUILTIN
    rgbLedWrite(RGB_BUILTIN, r, g, b);
#elif defined(LED_BUILTIN)
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, (r || g || b) ? HIGH : LOW);
#endif
}

void ledOff()    { led(0, 0, 0); }
void ledRed()    { led(255, 0, 0); }
void ledOrange() { led(255, 80, 0); }
void ledGreen()  { led(0, 255, 0); }
void ledCyan()   { led(0, 255, 255); }

/*============================================================================
 * Globals
 *============================================================================*/

bool g_debug = false;
bool g_reboot_pending = false;
unsigned long g_reboot_at = 0;
#if !defined(CONFIG_IDF_TARGET_ESP32)
temperature_sensor_handle_t g_temp_sensor = NULL;
#endif

static char serialBuf[SERIAL_BUF_SIZE];
static int  serialPos = 0;

/*============================================================================
 * Temperature Sensor
 *============================================================================*/

#if !defined(CONFIG_IDF_TARGET_ESP32)
static void initTempSensor() {
    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t err = temperature_sensor_install(&config, &g_temp_sensor);
    if (err != ESP_OK) { Serial.printf("Temp sensor install failed: %d\n", err); return; }
    err = temperature_sensor_enable(g_temp_sensor);
    if (err != ESP_OK) { Serial.printf("Temp sensor enable failed: %d\n", err); }
}
#endif

/*============================================================================
 * LittleFS Config Loading
 *============================================================================*/

static bool jsonGetString(const char *json, const char *key,
                           char *dst, int dst_len) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;

    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return false;
    p++;

    int w = 0;
    while (*p && *p != '"' && w < dst_len - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
        }
        dst[w++] = *p++;
    }
    dst[w] = '\0';
    return w > 0;
}

static int readFile(const char *path, char *buf, int buf_len) {
    File f = LittleFS.open(path, "r");
    if (!f) return -1;

    int len = f.readBytes(buf, buf_len - 1);
    buf[len] = '\0';
    f.close();
    return len;
}

static bool loadConfig() {
    configDefaults();

    if (!LittleFS.begin(false)) {
        Serial.printf("LittleFS: mount failed (no filesystem?)\n");
        return false;
    }

    Serial.printf("LittleFS: mounted OK\n");

    static char json_buf[512];
    int len = readFile("/config.json", json_buf, sizeof(json_buf));
    if (len > 0) {
        Serial.printf("LittleFS: loaded config.json (%d bytes)\n", len);
        jsonGetString(json_buf, "wifi_ssid", cfg_wifi_ssid, sizeof(cfg_wifi_ssid));
        jsonGetString(json_buf, "wifi_pass", cfg_wifi_pass, sizeof(cfg_wifi_pass));
        jsonGetString(json_buf, "device_name", cfg_device_name, sizeof(cfg_device_name));
        jsonGetString(json_buf, "nats_host", cfg_nats_host, sizeof(cfg_nats_host));
        char port_buf[8];
        if (jsonGetString(json_buf, "nats_port", port_buf, sizeof(port_buf))) {
            cfg_nats_port = atoi(port_buf);
        }
        jsonGetString(json_buf, "timezone", cfg_timezone, sizeof(cfg_timezone));
    } else {
        Serial.printf("LittleFS: no config.json, using defaults\n");
    }

    return true;
}

/*============================================================================
 * WiFi
 *============================================================================*/

static bool connectWiFi() {
    Serial.printf("WiFi: Connecting to %s", cfg_wifi_ssid);
    ledOrange();

    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg_wifi_ssid, cfg_wifi_pass);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (attempts % 2 == 0) ledOrange(); else ledOff();
        if (++attempts > 30) {
            Serial.println(" FAILED!");
            ledRed();
            return false;
        }
    }

    Serial.printf(" OK!\n");
    Serial.printf("WiFi: IP = %s\n", WiFi.localIP().toString().c_str());
    ledGreen();
    return true;
}

/*============================================================================
 * NATS
 *============================================================================*/

NatsClient natsClient;
bool g_nats_enabled = false;
bool g_nats_connected = false;
static unsigned long natsLastReconnect = 0;

static char natsSubjectCapabilities[64];
static char natsSubjectHal[64];
static const char natsSubjectDiscover[] = "_ion.discover";

/* Capabilities response buffer */
static char g_caps_json[2048];

static void onNatsEvent(nats_client_t *client, nats_event_t event,
                        void *userdata) {
    (void)client; (void)userdata;
    switch (event) {
    case NATS_EVENT_CONNECTED:
        Serial.printf("NATS: connected\n");
        g_nats_connected = true;
        break;
    case NATS_EVENT_DISCONNECTED:
        Serial.printf("NATS: disconnected\n");
        g_nats_connected = false;
        break;
    case NATS_EVENT_ERROR:
        Serial.printf("NATS: error: %s\n",
                      nats_err_str(nats_get_last_error(client)));
        break;
    default:
        break;
    }
}

/**
 * NATS capabilities handler - returns device state as JSON.
 * Used for discovery: what devices/hal capabilities are available.
 */
static void onNatsCapabilities(nats_client_t *client, const nats_msg_t *msg,
                               void *userdata) {
    (void)userdata;

    int w = 0;

    /* Chip identification */
    const char *chip_name =
#if defined(CONFIG_IDF_TARGET_ESP32C6)
        "ESP32-C6";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
        "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
        "ESP32-C3";
#else
        "ESP32";
#endif

    w += snprintf(g_caps_json + w, sizeof(g_caps_json) - w,
        "{\"device\":\"%s\",\"firmware\":\"ionode\",\"version\":\"%s\","
        "\"chip\":\"%s\",\"free_heap\":%u,\"ip\":\"%s\",",
        cfg_device_name, IONODE_VERSION, chip_name, ESP.getFreeHeap(),
        WiFi.localIP().toString().c_str());

    /* HAL capabilities */
    w += snprintf(g_caps_json + w, sizeof(g_caps_json) - w,
        "\"hal\":{\"gpio\":true,\"adc\":true,\"pwm\":true,"
        "\"dac\":false,\"uart\":true,\"system_temp\":true},");

    /* Devices */
    w += snprintf(g_caps_json + w, sizeof(g_caps_json) - w, "\"devices\":[");
    Device *devs = deviceGetAll();
    bool firstDev = true;
    for (int i = 0; i < MAX_DEVICES && w < (int)sizeof(g_caps_json) - 200; i++) {
        if (!devs[i].used) continue;
        Device *d = &devs[i];
        if (!firstDev) g_caps_json[w++] = ',';
        firstDev = false;
        if (deviceIsSensor(d->kind)) {
            float val = deviceReadSensor(d);
            w += snprintf(g_caps_json + w, sizeof(g_caps_json) - w,
                "{\"name\":\"%s\",\"kind\":\"%s\",\"value\":%.1f,\"unit\":\"%s\"}",
                d->name, deviceKindName(d->kind), val, d->unit);
        } else {
            w += snprintf(g_caps_json + w, sizeof(g_caps_json) - w,
                "{\"name\":\"%s\",\"kind\":\"%s\",\"pin\":%d,\"value\":%d}",
                d->name, deviceKindName(d->kind), d->pin, d->last_value);
        }
    }
    w += snprintf(g_caps_json + w, sizeof(g_caps_json) - w, "]}");

    if (g_debug) Serial.printf("[NATS] capabilities: %d bytes\n", w);

    if (msg->reply_len > 0) {
        nats_msg_respond_str(client, msg, g_caps_json);
    }
}

/*============================================================================
 * NATS Virtual Sensor Subscriptions
 *============================================================================*/

static void onNatsValue(nats_client_t *client, const nats_msg_t *msg,
                        void *userdata) {
    (void)client;
    Device *dev = (Device *)userdata;
    if (!dev || !dev->used) return;
    parseNatsPayload(msg->data, msg->data_len,
                     &dev->nats_value, dev->nats_msg, sizeof(dev->nats_msg));
    if (g_debug) Serial.printf("[NATS] %s = %.1f (msg='%s')\n",
                               dev->name, dev->nats_value, dev->nats_msg);
}

void natsSubscribeDeviceSensors() {
    if (!g_nats_connected) return;
    Device *devs = deviceGetAllMutable();
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devs[i].used) continue;
        if (devs[i].kind != DEV_SENSOR_NATS_VALUE) continue;
        if (devs[i].nats_subject[0] == '\0') continue;
        if (devs[i].nats_sid != 0) continue; /* already subscribed */
        uint16_t sid = 0;
        nats_err_t err = natsClient.subscribe(devs[i].nats_subject,
                                              onNatsValue, &devs[i], &sid);
        if (err == NATS_OK) {
            devs[i].nats_sid = sid;
            Serial.printf("[NATS] Subscribed '%s' -> %s (sid=%d)\n",
                          devs[i].name, devs[i].nats_subject, sid);
        } else {
            Serial.printf("[NATS] Subscribe '%s' failed: %s\n",
                          devs[i].nats_subject, nats_err_str(err));
        }
    }
}

void natsUnsubscribeDevice(const char *name) {
    if (!g_nats_connected) return;
    Device *devs = deviceGetAllMutable();
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devs[i].used) continue;
        if (strcmp(devs[i].name, name) != 0) continue;
        if (devs[i].nats_sid != 0) {
            natsClient.unsubscribe(devs[i].nats_sid);
            Serial.printf("[NATS] Unsubscribed '%s' (sid=%d)\n",
                          name, devs[i].nats_sid);
            devs[i].nats_sid = 0;
        }
        break;
    }
}

static void buildNatsSubjects() {
    snprintf(natsSubjectCapabilities, sizeof(natsSubjectCapabilities),
             "%s.capabilities", cfg_device_name);
    snprintf(natsSubjectHal, sizeof(natsSubjectHal),
             "%s.hal.>", cfg_device_name);
}

static bool connectNats() {
    Serial.printf("NATS: connecting to %s:%d...\n", cfg_nats_host, cfg_nats_port);

    natsClient.onEvent(onNatsEvent, nullptr);

    if (!natsClient.connect(cfg_nats_host, (uint16_t)cfg_nats_port, 2000)) {
        Serial.printf("NATS: connection failed\n");
        return false;
    }

    nats_err_t err;

    err = natsClient.subscribe(natsSubjectCapabilities, onNatsCapabilities, nullptr);
    if (err != NATS_OK) {
        Serial.printf("NATS: subscribe %s failed: %s\n",
                      natsSubjectCapabilities, nats_err_str(err));
    }

    err = natsClient.subscribe(natsSubjectDiscover, onNatsCapabilities, nullptr);
    if (err != NATS_OK) {
        Serial.printf("NATS: subscribe %s failed: %s\n",
                      natsSubjectDiscover, nats_err_str(err));
    }

    err = natsClient.subscribe(natsSubjectHal, onNatsHal, nullptr);
    if (err != NATS_OK) {
        Serial.printf("NATS: subscribe %s failed: %s\n",
                      natsSubjectHal, nats_err_str(err));
    }

    /* Publish online event */
    static char onlineMsg[256];
    snprintf(onlineMsg, sizeof(onlineMsg),
             "{\"event\":\"online\",\"device\":\"%s\",\"firmware\":\"ionode\","
             "\"version\":\"%s\",\"ip\":\"%s\"}",
             cfg_device_name, IONODE_VERSION,
             WiFi.localIP().toString().c_str());

    /* Publish to {device_name}.events */
    static char eventsSubject[64];
    snprintf(eventsSubject, sizeof(eventsSubject), "%s.events", cfg_device_name);
    natsClient.publish(eventsSubject, onlineMsg);

    Serial.printf("NATS: subscribed to %s, %s, %s\n",
                  natsSubjectCapabilities, natsSubjectDiscover, natsSubjectHal);

    /* Subscribe NATS virtual sensors */
    natsSubscribeDeviceSensors();

    return true;
}

/*============================================================================
 * Serial Commands
 *============================================================================*/

static void handleSerialCommand(const char *input) {
    const char *cmd = input + 1; /* skip leading '/' */

    if (strcmp(cmd, "status") == 0) {
        Serial.printf("Device: %s\n", cfg_device_name);
        Serial.printf("WiFi: %s (%s)\n",
            WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
            WiFi.localIP().toString().c_str());
        Serial.printf("Heap: %u / %u\n", ESP.getFreeHeap(), ESP.getHeapSize());
        Serial.printf("Uptime: %lus\n", millis() / 1000);
        Serial.printf("NATS: %s\n",
            g_nats_enabled
                ? (g_nats_connected ? "connected" : "disconnected")
                : "disabled");
        int devCount = 0;
        Device *devs = deviceGetAll();
        for (int i = 0; i < MAX_DEVICES; i++)
            if (devs[i].used) devCount++;
        Serial.printf("Devices: %d\n", devCount);
        Serial.printf("Debug: %s\n", g_debug ? "ON" : "OFF");
        Serial.printf("> ");
        return;
    }

    if (strcmp(cmd, "devices") == 0) {
        Device *devs = deviceGetAll();
        int count = 0;
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (!devs[i].used) continue;
            Device *d = &devs[i];
            count++;
            if (d->kind == DEV_SENSOR_SERIAL_TEXT) {
                float val = deviceReadSensor(d);
                Serial.printf("  %s [serial_text] %ubaud = %.1f %s\n",
                    d->name, (unsigned)d->baud, val, d->unit);
            } else if (d->kind == DEV_SENSOR_NATS_VALUE) {
                float val = deviceReadSensor(d);
                Serial.printf("  %s [nats_value] %s = %.1f %s\n",
                    d->name, d->nats_subject, val, d->unit);
            } else if (deviceIsSensor(d->kind)) {
                float val = deviceReadSensor(d);
                Serial.printf("  %s [%s] pin=%d = %.1f %s\n",
                    d->name, deviceKindName(d->kind), d->pin, val, d->unit);
            } else {
                Serial.printf("  %s [%s] pin=%d%s\n",
                    d->name, deviceKindName(d->kind), d->pin,
                    d->inverted ? " (inverted)" : "");
            }
        }
        if (count == 0) Serial.printf("  No devices\n");
        Serial.printf("> ");
        return;
    }

    if (strcmp(cmd, "debug") == 0) {
        g_debug = !g_debug;
        Serial.printf("Debug %s\n> ", g_debug ? "ON" : "OFF");
        return;
    }

    if (strcmp(cmd, "reboot") == 0) {
        Serial.printf("Rebooting...\n");
        delay(200);
        ESP.restart();
        return;
    }

    if (strcmp(cmd, "setup") == 0) {
        Serial.printf("Starting setup portal...\n");
        runSetupPortal();
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        Serial.printf("Commands: /status /devices /debug /reboot /setup /help\n> ");
        return;
    }

    Serial.printf("Unknown command: %s (try /help)\n> ", input);
}

/*============================================================================
 * Setup
 *============================================================================*/

void setup() {
    Serial.begin(115200);
    delay(5000);

    Serial.printf("\n\n");
    Serial.printf("========================================\n");
    Serial.printf("  IOnode v%s\n", IONODE_VERSION);
    Serial.printf("========================================\n\n");

    /* Load config from LittleFS */
    loadConfig();

    Serial.printf("Device: %s\n", cfg_device_name);

    /* Initialize temperature sensor (not available on classic ESP32) */
#if !defined(CONFIG_IDF_TARGET_ESP32)
    initTempSensor();
    if (g_temp_sensor) {
        float temp = 0.0f;
        temperature_sensor_get_celsius(g_temp_sensor, &temp);
        Serial.printf("Chip temp: %.1f C\n", temp);
    }
#endif

    /* Initialize device registry */
    devicesInit();

    if (cfg_wifi_ssid[0] == '\0') {
        Serial.printf("\n[!] No WiFi config — starting setup portal\n");
        runSetupPortal();
    }

    /* Connect WiFi */
    if (!connectWiFi()) {
        Serial.printf("[!] WiFi failed — starting setup portal\n");
        runSetupPortal();
    }

    /* NTP time sync */
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", cfg_timezone, 1);
    tzset();
    Serial.printf("NTP: syncing (TZ=%s)...\n", cfg_timezone);

    /* Watchdog - reconfigure to 60s */
    esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 60000, .idle_core_mask = 0,
                                       .trigger_panic = true };
    esp_task_wdt_reconfigure(&wdt_cfg);
    esp_task_wdt_add(NULL);

    /* Connect NATS (optional) */
    if (cfg_nats_host[0] != '\0') {
        g_nats_enabled = true;
        buildNatsSubjects();
        if (!connectNats()) {
            Serial.printf("NATS: will retry in background\n");
        }
    } else {
        Serial.printf("NATS: disabled (no nats_host in config)\n");
    }

    /* Start web config server */
    webConfigSetup();

    Serial.printf("\nReady! Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Type /help for commands.\n\n");
    Serial.printf("> ");
}

/*============================================================================
 * Loop
 *============================================================================*/

static unsigned long lastHeartbeat = 0;

void loop() {
    esp_task_wdt_reset();

    /* LED heartbeat - brief dim green blink when idle */
    unsigned long now = millis();
    if (now - lastHeartbeat > HEARTBEAT_INTERVAL_MS) {
        lastHeartbeat = now;
        led(0, 40, 0);
        delay(50);
        ledOff();
    }

    /* Check WiFi */
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("\nWiFi disconnected! Reconnecting...\n");
        ledRed();
        if (!connectWiFi()) {
            delay(5000);
            return;
        }
        Serial.printf("> ");
    }

    /* Process web server */
    webConfigLoop();

    /* Process NATS */
    if (g_nats_enabled) {
        if (natsClient.connected()) {
            nats_err_t err = natsClient.process();
            if (err != NATS_OK && err != NATS_ERR_WOULD_BLOCK) {
                if (g_debug) Serial.printf("NATS: process error: %s\n",
                                           nats_err_str(err));
            }
        } else {
            /* Reconnect with backoff */
            if (now - natsLastReconnect > NATS_RECONNECT_DELAY_MS) {
                natsLastReconnect = now;
                connectNats();
            }
        }
    }

    /* Poll serial_text UART for incoming data */
    serialTextPoll();

    /* Keep sensor EMA values warm (every 10s) + history (every 5min) */
    sensorsPoll();

    /* Read serial input character by character */
    while (Serial.available()) {
        char c = Serial.read();

        /* Handle backspace */
        if (c == '\b' || c == 127) {
            if (serialPos > 0) {
                serialPos--;
                Serial.print("\b \b");
            }
            continue;
        }

        /* Handle enter */
        if (c == '\n' || c == '\r') {
            if (serialPos == 0) continue;

            serialBuf[serialPos] = '\0';
            serialPos = 0;
            Serial.println();

            /* Trim whitespace */
            char *input = serialBuf;
            while (*input == ' ') input++;
            int len = strlen(input);
            while (len > 0 && input[len - 1] == ' ') input[--len] = '\0';

            if (len == 0) {
                Serial.printf("> ");
                continue;
            }

            if (input[0] == '/') {
                handleSerialCommand(input);
            } else {
                Serial.printf("Unknown input. Use /help for commands.\n> ");
            }
            continue;
        }

        /* Buffer character */
        if (serialPos < SERIAL_BUF_SIZE - 1) {
            serialBuf[serialPos++] = c;
            Serial.print(c);
        }
    }

    /* Deferred reboot (allows HTTP response to flush) */
    if (g_reboot_pending && millis() >= g_reboot_at) {
        Serial.printf("Rebooting...\n");
        delay(200);
        ESP.restart();
    }

    delay(10);
}
