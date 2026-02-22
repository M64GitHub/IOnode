/**
 * @file nats_config.cpp
 * @brief NATS remote configuration handler
 *
 * Subscribes to {device_name}.config.> and routes to sub-handlers for
 * remote device management, tag/group config, heartbeat, and events.
 */

#include <Arduino.h>
#include "nats_config.h"
#include "devices.h"
#include "nats_hal.h"

/* Externs from main.cpp */
extern char cfg_device_name[32];
extern char cfg_wifi_ssid[64];
extern char cfg_nats_host[64];
extern int  cfg_nats_port;
extern char cfg_timezone[64];
extern char cfg_tag[32];
extern int  cfg_heartbeat_interval;
extern bool g_debug;
extern bool g_config_dirty;
extern unsigned long g_config_dirty_ms;
extern bool g_reboot_pending;
extern unsigned long g_reboot_at;

extern NatsClient natsClient;
extern bool g_nats_connected;

/* Forward declarations for NATS subscription management */
void natsSubscribeDeviceSensors();
void natsUnsubscribeDevice(const char *name);
void configSave();

/* Reply and JSON buffers */
static char g_cfg_reply[512];
static char g_cfg_json[2048];

/* Cached prefix length: strlen(cfg_device_name) + strlen(".config.") */
static int s_cfg_prefix_len = 0;

static int cfgPrefixLen() {
    if (s_cfg_prefix_len == 0)
        s_cfg_prefix_len = strlen(cfg_device_name) + 8; /* ".config." */
    return s_cfg_prefix_len;
}

/*============================================================================
 * Helpers
 *============================================================================*/

static void cfgError(nats_client_t *client, const nats_msg_t *msg,
                     const char *error, const char *detail) {
    snprintf(g_cfg_reply, sizeof(g_cfg_reply),
             "{\"error\":\"%s\",\"detail\":\"%s\"}", error, detail);
    if (msg->reply_len > 0)
        nats_msg_respond_str(client, msg, g_cfg_reply);
}

static void cfgOk(nats_client_t *client, const nats_msg_t *msg) {
    if (msg->reply_len > 0)
        nats_msg_respond_str(client, msg, "{\"ok\":true}");
}

/* Simple JSON string extractor (matches project pattern) */
static bool cfgJsonGetString(const char *json, const char *key,
                              char *dst, int dst_len) {
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return false;
    p++;
    int w = 0;
    while (*p && *p != '"' && w < dst_len - 1) {
        if (*p == '\\' && *(p + 1)) p++;
        dst[w++] = *p++;
    }
    dst[w] = '\0';
    return w > 0;
}

static int cfgJsonGetInt(const char *json, const char *key, int default_val) {
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return default_val;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    return atoi(p);
}

static bool cfgJsonGetBool(const char *json, const char *key, bool default_val) {
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return default_val;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (strncmp(p, "true", 4) == 0) return true;
    if (strncmp(p, "false", 5) == 0) return false;
    return default_val;
}

static float cfgJsonGetFloat(const char *json, const char *key, float default_val) {
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return default_val;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    char *end = nullptr;
    float v = strtof(p, &end);
    return (end != p) ? v : default_val;
}

static int cfgJsonEscapeStr(char *dst, int dst_len, const char *src) {
    int w = 0;
    for (int i = 0; src[i] && w < dst_len - 2; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (w + 2 >= dst_len) break;
            dst[w++] = '\\'; dst[w++] = c;
        } else if ((uint8_t)c >= 0x20) {
            dst[w++] = c;
        }
    }
    dst[w] = '\0';
    return w;
}

/*============================================================================
 * config.device.add / config.device.remove / config.device.list
 *============================================================================*/

static void cfgDeviceAdd(nats_client_t *client, const nats_msg_t *msg,
                          const char *payload) {
    char name[DEV_NAME_LEN];
    char kind_str[24];

    if (!cfgJsonGetString(payload, "n", name, sizeof(name))) {
        cfgError(client, msg, "missing_field", "n (name)");
        return;
    }
    if (!cfgJsonGetString(payload, "k", kind_str, sizeof(kind_str))) {
        cfgError(client, msg, "missing_field", "k (kind)");
        return;
    }

    int pin = cfgJsonGetInt(payload, "p", PIN_NONE);
    char unit[DEV_UNIT_LEN] = "";
    cfgJsonGetString(payload, "u", unit, sizeof(unit));
    bool inverted = cfgJsonGetBool(payload, "i", false);

    char nats_subj[32] = "";
    cfgJsonGetString(payload, "ns", nats_subj, sizeof(nats_subj));
    uint32_t baud = (uint32_t)cfgJsonGetInt(payload, "bd", 0);

    /* Map kind string to enum (kindFromString is static in devices.cpp) */
    DeviceKind kind = DEV_SENSOR_DIGITAL;
    if (strcmp(kind_str, "digital_in") == 0)     kind = DEV_SENSOR_DIGITAL;
    else if (strcmp(kind_str, "analog_in") == 0)  kind = DEV_SENSOR_ANALOG_RAW;
    else if (strcmp(kind_str, "ntc_10k") == 0)    kind = DEV_SENSOR_NTC_10K;
    else if (strcmp(kind_str, "ldr") == 0)        kind = DEV_SENSOR_LDR;
    else if (strcmp(kind_str, "internal_temp") == 0) kind = DEV_SENSOR_INTERNAL_TEMP;
    else if (strcmp(kind_str, "clock_hour") == 0) kind = DEV_SENSOR_CLOCK_HOUR;
    else if (strcmp(kind_str, "clock_minute") == 0) kind = DEV_SENSOR_CLOCK_MINUTE;
    else if (strcmp(kind_str, "clock_hhmm") == 0) kind = DEV_SENSOR_CLOCK_HHMM;
    else if (strcmp(kind_str, "nats_value") == 0) kind = DEV_SENSOR_NATS_VALUE;
    else if (strcmp(kind_str, "serial_text") == 0) kind = DEV_SENSOR_SERIAL_TEXT;
    else if (strcmp(kind_str, "digital_out") == 0) kind = DEV_ACTUATOR_DIGITAL;
    else if (strcmp(kind_str, "relay") == 0)      kind = DEV_ACTUATOR_RELAY;
    else if (strcmp(kind_str, "pwm") == 0)        kind = DEV_ACTUATOR_PWM;
    else if (strcmp(kind_str, "rgb_led") == 0)    kind = DEV_ACTUATOR_RGB_LED;
    else if (strcmp(kind_str, "i2c_generic") == 0) kind = DEV_SENSOR_I2C_GENERIC;
    else if (strcmp(kind_str, "i2c_bme280") == 0)  kind = DEV_SENSOR_I2C_BME280;
    else if (strcmp(kind_str, "i2c_bh1750") == 0)  kind = DEV_SENSOR_I2C_BH1750;
    else if (strcmp(kind_str, "i2c_sht31") == 0)   kind = DEV_SENSOR_I2C_SHT31;
    else if (strcmp(kind_str, "i2c_ads1115") == 0)  kind = DEV_SENSOR_I2C_ADS1115;
    else if (strcmp(kind_str, "ssd1306") == 0)     kind = DEV_ACTUATOR_SSD1306;
    else {
        cfgError(client, msg, "unknown_kind", kind_str);
        return;
    }

    /* I2C fields */
    uint8_t i2c_addr = (uint8_t)cfgJsonGetInt(payload, "ia", 0);
    char disp_tmpl[128] = "";
    cfgJsonGetString(payload, "dt", disp_tmpl, sizeof(disp_tmpl));
    uint8_t i2c_reg_len = (uint8_t)cfgJsonGetInt(payload, "rl", 1);
    float i2c_scale = cfgJsonGetFloat(payload, "sc", 1.0f);

    bool ok = deviceRegister(name, kind, (uint8_t)pin, unit[0] ? unit : nullptr,
                        inverted, nats_subj[0] ? nats_subj : nullptr, baud,
                        i2c_addr, disp_tmpl[0] ? disp_tmpl : nullptr,
                        i2c_reg_len, i2c_scale);
    if (!ok) {
        cfgError(client, msg, "register_failed", "duplicate name or registry full");
        return;
    }

    devicesSave();

    /* If nats_value, subscribe to its NATS subject */
    if (kind == DEV_SENSOR_NATS_VALUE && g_nats_connected) {
        natsSubscribeDeviceSensors();
    }

    cfgOk(client, msg);
    Serial.printf("[Config] Device added: %s (%s)\n", name, kind_str);
}

static void cfgDeviceRemove(nats_client_t *client, const nats_msg_t *msg,
                             const char *payload) {
    char name[DEV_NAME_LEN];
    if (!cfgJsonGetString(payload, "n", name, sizeof(name))) {
        cfgError(client, msg, "missing_field", "n (name)");
        return;
    }

    /* Unsub NATS if it was a nats_value device */
    natsUnsubscribeDevice(name);

    bool ok = deviceRemove(name);
    if (!ok) {
        cfgError(client, msg, "not_found", name);
        return;
    }

    devicesSave();
    cfgOk(client, msg);
    Serial.printf("[Config] Device removed: %s\n", name);
}

static void cfgDeviceList(nats_client_t *client, const nats_msg_t *msg) {
    int w = 0;
    w += snprintf(g_cfg_json + w, sizeof(g_cfg_json) - w, "[");

    Device *devs = deviceGetAll();
    bool first = true;
    for (int i = 0; i < MAX_DEVICES && w < (int)sizeof(g_cfg_json) - 200; i++) {
        if (!devs[i].used) continue;
        Device *d = &devs[i];
        if (!first) g_cfg_json[w++] = ',';
        first = false;

        if (deviceIsSensor(d->kind)) {
            float val = deviceReadSensor(d);
            w += snprintf(g_cfg_json + w, sizeof(g_cfg_json) - w,
                "{\"name\":\"%s\",\"kind\":\"%s\",\"value\":%.1f,\"unit\":\"%s\"}",
                d->name, deviceKindName(d->kind), val, d->unit);
        } else {
            w += snprintf(g_cfg_json + w, sizeof(g_cfg_json) - w,
                "{\"name\":\"%s\",\"kind\":\"%s\",\"pin\":%d,\"value\":%d}",
                d->name, deviceKindName(d->kind), d->pin, d->last_value);
        }
    }

    w += snprintf(g_cfg_json + w, sizeof(g_cfg_json) - w, "]");

    if (msg->reply_len > 0)
        nats_msg_respond_str(client, msg, g_cfg_json);
}

/*============================================================================
 * config.tag.set / config.tag.get (Phase 3)
 *============================================================================*/

/* Forward: group subscription management in main.cpp */
void natsGroupResubscribe(const char *old_tag, const char *new_tag);

static void cfgTagSet(nats_client_t *client, const nats_msg_t *msg,
                       const char *payload) {
    /* Payload is the raw tag string (or JSON with "tag" field) */
    char new_tag[32];
    if (payload[0] == '{') {
        if (!cfgJsonGetString(payload, "tag", new_tag, sizeof(new_tag)))
            new_tag[0] = '\0';
    } else {
        /* Bare string payload */
        strncpy(new_tag, payload, sizeof(new_tag) - 1);
        new_tag[sizeof(new_tag) - 1] = '\0';
        /* Trim whitespace */
        int len = strlen(new_tag);
        while (len > 0 && (new_tag[len-1] == ' ' || new_tag[len-1] == '\n'
               || new_tag[len-1] == '\r')) new_tag[--len] = '\0';
    }

    char old_tag[32];
    strncpy(old_tag, cfg_tag, sizeof(old_tag));

    strncpy(cfg_tag, new_tag, sizeof(cfg_tag) - 1);
    cfg_tag[sizeof(cfg_tag) - 1] = '\0';

    natsGroupResubscribe(old_tag, new_tag);

    g_config_dirty = true;
    g_config_dirty_ms = millis();

    cfgOk(client, msg);
    Serial.printf("[Config] Tag set: '%s'\n", cfg_tag);
}

static void cfgTagGet(nats_client_t *client, const nats_msg_t *msg) {
    snprintf(g_cfg_reply, sizeof(g_cfg_reply), "{\"tag\":\"%s\"}", cfg_tag);
    if (msg->reply_len > 0)
        nats_msg_respond_str(client, msg, g_cfg_reply);
}

/*============================================================================
 * config.heartbeat.set (Phase 4)
 *============================================================================*/

static void cfgHeartbeatSet(nats_client_t *client, const nats_msg_t *msg,
                             const char *payload) {
    int val = atoi(payload);
    if (val < 0 || val > 3600) {
        cfgError(client, msg, "invalid_value", "0-3600 seconds (0=disabled)");
        return;
    }

    cfg_heartbeat_interval = val;
    g_config_dirty = true;
    g_config_dirty_ms = millis();

    cfgOk(client, msg);
    Serial.printf("[Config] Heartbeat interval: %ds\n", cfg_heartbeat_interval);
}

/*============================================================================
 * config.event.set / config.event.clear / config.event.list (Phase 5)
 *============================================================================*/

static void cfgEventSet(nats_client_t *client, const nats_msg_t *msg,
                         const char *payload) {
    char name[DEV_NAME_LEN];
    if (!cfgJsonGetString(payload, "n", name, sizeof(name))) {
        cfgError(client, msg, "missing_field", "n (device name)");
        return;
    }

    Device *dev = deviceFind(name);
    if (!dev) {
        cfgError(client, msg, "not_found", name);
        return;
    }
    if (!deviceIsSensor(dev->kind)) {
        cfgError(client, msg, "not_sensor", "events only on sensors");
        return;
    }

    float threshold = cfgJsonGetFloat(payload, "t", 0.0f);
    int cooldown = cfgJsonGetInt(payload, "cd", 10);
    char dir_str[8] = "";
    cfgJsonGetString(payload, "d", dir_str, sizeof(dir_str));

    uint8_t direction = EV_DIR_NONE;
    if (strcmp(dir_str, "above") == 0) direction = EV_DIR_ABOVE;
    else if (strcmp(dir_str, "below") == 0) direction = EV_DIR_BELOW;
    else {
        cfgError(client, msg, "invalid_direction", "use 'above' or 'below'");
        return;
    }

    dev->ev_threshold = threshold;
    dev->ev_direction = direction;
    dev->ev_cooldown = (uint16_t)constrain(cooldown, 1, 65535);
    dev->ev_armed = true;
    dev->ev_last_fire_ms = 0;

    devicesMarkDirty();
    cfgOk(client, msg);
    Serial.printf("[Config] Event set: %s %s %.1f (cd=%ds)\n",
                  name, dir_str, threshold, cooldown);
}

static void cfgEventClear(nats_client_t *client, const nats_msg_t *msg,
                           const char *payload) {
    char name[DEV_NAME_LEN];
    if (!cfgJsonGetString(payload, "n", name, sizeof(name))) {
        cfgError(client, msg, "missing_field", "n (device name)");
        return;
    }

    Device *dev = deviceFind(name);
    if (!dev) {
        cfgError(client, msg, "not_found", name);
        return;
    }

    dev->ev_direction = EV_DIR_NONE;
    dev->ev_threshold = 0.0f;
    dev->ev_cooldown = 0;
    dev->ev_armed = false;
    dev->ev_last_fire_ms = 0;

    devicesMarkDirty();
    cfgOk(client, msg);
    Serial.printf("[Config] Event cleared: %s\n", name);
}

static void cfgEventList(nats_client_t *client, const nats_msg_t *msg) {
    int w = 0;
    w += snprintf(g_cfg_json + w, sizeof(g_cfg_json) - w, "[");

    Device *devs = deviceGetAll();
    bool first = true;
    for (int i = 0; i < MAX_DEVICES && w < (int)sizeof(g_cfg_json) - 200; i++) {
        if (!devs[i].used) continue;
        Device *d = &devs[i];
        if (d->ev_direction == EV_DIR_NONE) continue;

        if (!first) g_cfg_json[w++] = ',';
        first = false;

        const char *dir = d->ev_direction == EV_DIR_ABOVE ? "above" : "below";
        w += snprintf(g_cfg_json + w, sizeof(g_cfg_json) - w,
            "{\"name\":\"%s\",\"threshold\":%.1f,\"direction\":\"%s\","
            "\"cooldown\":%d,\"armed\":%s}",
            d->name, d->ev_threshold, dir, d->ev_cooldown,
            d->ev_armed ? "true" : "false");
    }

    w += snprintf(g_cfg_json + w, sizeof(g_cfg_json) - w, "]");

    if (msg->reply_len > 0)
        nats_msg_respond_str(client, msg, g_cfg_json);
}

/*============================================================================
 * config.name.set
 *============================================================================*/

static void cfgNameSet(nats_client_t *client, const nats_msg_t *msg,
                        const char *payload) {
    char new_name[32];
    if (payload[0] == '{') {
        if (!cfgJsonGetString(payload, "name", new_name, sizeof(new_name))) {
            cfgError(client, msg, "missing_field", "name");
            return;
        }
    } else {
        strncpy(new_name, payload, sizeof(new_name) - 1);
        new_name[sizeof(new_name) - 1] = '\0';
        int len = strlen(new_name);
        while (len > 0 && (new_name[len-1] == ' ' || new_name[len-1] == '\n'
               || new_name[len-1] == '\r')) new_name[--len] = '\0';
    }

    if (strlen(new_name) < 1 || strlen(new_name) > 30) {
        cfgError(client, msg, "invalid_name", "1-30 characters");
        return;
    }

    strncpy(cfg_device_name, new_name, sizeof(cfg_device_name) - 1);
    cfg_device_name[sizeof(cfg_device_name) - 1] = '\0';

    /* Immediate save (not debounced — critical config change) */
    configSave();

    cfgOk(client, msg);
    Serial.printf("[Config] Name changed to '%s', rebooting...\n", cfg_device_name);

    /* Deferred reboot to flush response */
    g_reboot_pending = true;
    g_reboot_at = millis() + 2000;
}

/*============================================================================
 * config.get — sanitized config dump (no wifi_pass)
 *============================================================================*/

static void cfgGet(nats_client_t *client, const nats_msg_t *msg) {
    char esc_ssid[128], esc_name[64], esc_host[128], esc_tz[128], esc_tag[64];
    cfgJsonEscapeStr(esc_ssid, sizeof(esc_ssid), cfg_wifi_ssid);
    cfgJsonEscapeStr(esc_name, sizeof(esc_name), cfg_device_name);
    cfgJsonEscapeStr(esc_host, sizeof(esc_host), cfg_nats_host);
    cfgJsonEscapeStr(esc_tz, sizeof(esc_tz), cfg_timezone);
    cfgJsonEscapeStr(esc_tag, sizeof(esc_tag), cfg_tag);

    snprintf(g_cfg_json, sizeof(g_cfg_json),
        "{\"device_name\":\"%s\",\"wifi_ssid\":\"%s\","
        "\"nats_host\":\"%s\",\"nats_port\":%d,"
        "\"timezone\":\"%s\",\"tag\":\"%s\","
        "\"heartbeat_interval\":%d}",
        esc_name, esc_ssid, esc_host, cfg_nats_port,
        esc_tz, esc_tag, cfg_heartbeat_interval);

    if (msg->reply_len > 0)
        nats_msg_respond_str(client, msg, g_cfg_json);
}

/*============================================================================
 * Main router: onNatsConfig()
 *============================================================================*/

void onNatsConfig(nats_client_t *client, const nats_msg_t *msg, void *userdata) {
    (void)userdata;

    /* Skip prefix: "{device_name}.config." */
    if ((int)msg->subject_len <= cfgPrefixLen()) return;
    const char *suffix = msg->subject + cfgPrefixLen();

    /* Copy payload into null-terminated stack buffer */
    char payload[256];
    size_t plen = msg->data_len < sizeof(payload) - 1
                  ? msg->data_len : sizeof(payload) - 1;
    if (msg->data && plen > 0)
        memcpy(payload, msg->data, plen);
    payload[plen] = '\0';

    if (g_debug)
        Serial.printf("[NATS] config: %s (payload='%s')\n", suffix, payload);

    /* Route by suffix */
    if (strcmp(suffix, "device.add") == 0)         cfgDeviceAdd(client, msg, payload);
    else if (strcmp(suffix, "device.remove") == 0)  cfgDeviceRemove(client, msg, payload);
    else if (strcmp(suffix, "device.list") == 0)    cfgDeviceList(client, msg);
    else if (strcmp(suffix, "tag.set") == 0)        cfgTagSet(client, msg, payload);
    else if (strcmp(suffix, "tag.get") == 0)        cfgTagGet(client, msg);
    else if (strcmp(suffix, "heartbeat.set") == 0)  cfgHeartbeatSet(client, msg, payload);
    else if (strcmp(suffix, "event.set") == 0)      cfgEventSet(client, msg, payload);
    else if (strcmp(suffix, "event.clear") == 0)    cfgEventClear(client, msg, payload);
    else if (strcmp(suffix, "event.list") == 0)     cfgEventList(client, msg);
    else if (strcmp(suffix, "name.set") == 0)       cfgNameSet(client, msg, payload);
    else if (strcmp(suffix, "get") == 0)            cfgGet(client, msg);
    else cfgError(client, msg, "unknown_command", suffix);
}
