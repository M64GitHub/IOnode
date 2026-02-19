/**
 * @file web_config.cpp
 * @brief Web-based configuration portal for IOnode
 *
 * Runs on port 80 during normal operation (not during setup portal).
 * REST API + PROGMEM single-page app for config, devices, pins, status.
 */

#include "web_config.h"
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "version.h"
#include "devices.h"
#include "nats_hal.h"

/* Externs from main.cpp */
extern char cfg_wifi_ssid[64];
extern char cfg_wifi_pass[64];
extern char cfg_device_name[32];
extern char cfg_nats_host[64];
extern int  cfg_nats_port;
extern char cfg_timezone[64];
extern bool g_nats_enabled;
extern bool g_nats_connected;
extern bool g_reboot_pending;
extern unsigned long g_reboot_at;

static WebServer server(80);

/*============================================================================
 * Helpers
 *============================================================================*/

static int wcReadFile(const char *path, char *buf, int buf_len) {
    File f = LittleFS.open(path, "r");
    if (!f) return -1;
    int len = f.readBytes(buf, buf_len - 1);
    buf[len] = '\0';
    f.close();
    return len;
}

static bool wcJsonGetString(const char *json, const char *key,
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
        if (*p == '\\' && *(p + 1)) { p++; }
        dst[w++] = *p++;
    }
    dst[w] = '\0';
    return w > 0;
}

static int wcJsonGetInt(const char *json, const char *key, int default_val) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return default_val;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    return atoi(p);
}

static bool wcJsonGetBool(const char *json, const char *key, bool default_val) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return default_val;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (strncmp(p, "true", 4) == 0) return true;
    if (strncmp(p, "false", 5) == 0) return false;
    return default_val;
}

static void wcWriteJsonEscaped(File &f, const char *s) {
    f.print('"');
    while (*s) {
        if (*s == '"' || *s == '\\') f.print('\\');
        if (*s == '\n') { f.print("\\n"); s++; continue; }
        if ((uint8_t)*s >= 0x20) f.print(*s);
        s++;
    }
    f.print('"');
}

static int jsonEscapeBuf(char *dst, int dst_len, const char *src) {
    int w = 0;
    for (int i = 0; src[i] && w < dst_len - 2; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (w + 2 >= dst_len) break;
            dst[w++] = '\\'; dst[w++] = c;
        } else if (c == '\n') {
            if (w + 2 >= dst_len) break;
            dst[w++] = '\\'; dst[w++] = 'n';
        } else if (c == '\r' || (uint8_t)c < 0x20) {
            /* skip control chars */
        } else {
            dst[w++] = c;
        }
    }
    dst[w] = '\0';
    return w;
}

static void maskSensitive(const char *src, char *dst, int dst_len) {
    int len = strlen(src);
    if (len == 0) { dst[0] = '\0'; return; }
    if (len <= 4) {
        snprintf(dst, dst_len, "****");
    } else {
        snprintf(dst, dst_len, "...%s", src + len - 4);
    }
}

static bool isMasked(const char *val) {
    if (val[0] == '\0') return false;
    if (strncmp(val, "...", 3) == 0) return true;
    if (strncmp(val, "****", 4) == 0) return true;
    return false;
}

/*============================================================================
 * REST API Handlers
 *============================================================================*/

static void handleGetConfig() {
    static char buf[512];
    char masked_pass[16];
    maskSensitive(cfg_wifi_pass, masked_pass, sizeof(masked_pass));

    snprintf(buf, sizeof(buf),
        "{"
        "\"wifi_ssid\":\"%s\","
        "\"wifi_pass\":\"%s\","
        "\"device_name\":\"%s\","
        "\"nats_host\":\"%s\","
        "\"nats_port\":\"%d\","
        "\"timezone\":\"%s\""
        "}",
        cfg_wifi_ssid, masked_pass, cfg_device_name,
        cfg_nats_host, cfg_nats_port, cfg_timezone);

    server.send(200, "application/json", buf);
}

static void handlePostConfig() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    const String &body = server.arg("plain");

    /* Read existing config to preserve masked fields */
    static char existing[512];
    int elen = wcReadFile("/config.json", existing, sizeof(existing));
    if (elen <= 0) existing[0] = '\0';

    struct Field {
        const char *key;
        char val[128];
    };
    static Field fields[6];
    const char *keys[] = {
        "wifi_ssid", "wifi_pass", "device_name",
        "nats_host", "nats_port", "timezone"
    };

    for (int i = 0; i < 6; i++) {
        fields[i].key = keys[i];
        fields[i].val[0] = '\0';

        char newVal[128] = {0};
        bool hasNew = wcJsonGetString(body.c_str(), keys[i], newVal, sizeof(newVal));

        if (hasNew && !isMasked(newVal)) {
            strncpy(fields[i].val, newVal, sizeof(fields[i].val) - 1);
        } else if (existing[0]) {
            wcJsonGetString(existing, keys[i], fields[i].val, sizeof(fields[i].val));
        }
    }

    File f = LittleFS.open("/config.json", "w");
    if (!f) {
        server.send(500, "application/json", "{\"error\":\"write failed\"}");
        return;
    }

    f.print("{\n");
    for (int i = 0; i < 6; i++) {
        f.print("  \""); f.print(fields[i].key); f.print("\": ");
        wcWriteJsonEscaped(f, fields[i].val);
        if (i < 5) f.print(",");
        f.print("\n");
    }
    f.print("}\n");
    f.close();

    Serial.printf("[WebConfig] Config saved to /config.json\n");
    server.send(200, "application/json", "{\"ok\":true,\"message\":\"Config saved. Reboot to apply.\"}");
}

static void handleGetStatus() {
    static char buf[512];
    unsigned long uptime = millis() / 1000;
    unsigned long days = uptime / 86400;
    unsigned long hours = (uptime % 86400) / 3600;
    unsigned long mins = (uptime % 3600) / 60;
    unsigned long secs = uptime % 60;

    snprintf(buf, sizeof(buf),
        "{"
        "\"version\":\"%s\","
        "\"device_name\":\"%s\","
        "\"uptime\":\"%lud %luh %lum %lus\","
        "\"uptime_seconds\":%lu,"
        "\"heap_free\":%u,"
        "\"heap_total\":%u,"
        "\"wifi_ssid\":\"%s\","
        "\"wifi_ip\":\"%s\","
        "\"wifi_rssi\":%d,"
        "\"nats\":\"%s\""
        "}",
        IONODE_VERSION, cfg_device_name,
        days, hours, mins, secs, uptime,
        ESP.getFreeHeap(), ESP.getHeapSize(),
        cfg_wifi_ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI(),
        g_nats_enabled ? (g_nats_connected ? "connected" : "disconnected") : "disabled");

    server.send(200, "application/json", buf);
}

static void handleReboot() {
    server.send(200, "application/json", "{\"ok\":true,\"message\":\"Rebooting...\"}");
    g_reboot_pending = true;
    g_reboot_at = millis() + 2000;
}

/*============================================================================
 * Devices API
 *============================================================================*/

static bool isInternalDevice(DeviceKind kind) {
    return kind == DEV_SENSOR_INTERNAL_TEMP ||
           kind == DEV_SENSOR_CLOCK_HOUR ||
           kind == DEV_SENSOR_CLOCK_MINUTE ||
           kind == DEV_SENSOR_CLOCK_HHMM;
}

static void handleGetDevices() {
    static char buf[2048];
    int w = 0;

    Device *devs = deviceGetAll();
    w += snprintf(buf + w, sizeof(buf) - w, "[");

    bool first = true;
    for (int i = 0; i < MAX_DEVICES && w < (int)sizeof(buf) - 256; i++) {
        if (!devs[i].used) continue;
        Device *d = &devs[i];

        if (!first) w += snprintf(buf + w, sizeof(buf) - w, ",");
        first = false;

        /* Read current value */
        char val_str[32];
        if (deviceIsActuator(d->kind)) {
            if (d->kind == DEV_ACTUATOR_PWM)
                snprintf(val_str, sizeof(val_str), "%d/255", d->last_value);
            else
                snprintf(val_str, sizeof(val_str), "%s", d->last_value ? "ON" : "OFF");
        } else {
            float val = deviceReadSensor(d);
            if (d->unit[0])
                snprintf(val_str, sizeof(val_str), "%.1f %s", val, d->unit);
            else
                snprintf(val_str, sizeof(val_str), "%.1f", val);
        }

        /* Pin display */
        char pin_str[16];
        if (d->pin == PIN_NONE)
            snprintf(pin_str, sizeof(pin_str), "virtual");
        else
            snprintf(pin_str, sizeof(pin_str), "%d", d->pin);

        /* Extra info: NATS subject or serial baud */
        char extra[48];
        extra[0] = '\0';
        if (d->kind == DEV_SENSOR_NATS_VALUE && d->nats_subject[0])
            snprintf(extra, sizeof(extra), "%s", d->nats_subject);
        else if (d->kind == DEV_SENSOR_SERIAL_TEXT && d->baud > 0)
            snprintf(extra, sizeof(extra), "%u baud", (unsigned)d->baud);

        /* Last message for NATS and serial_text sensors */
        char msg[80];
        msg[0] = '\0';
        if (d->kind == DEV_SENSOR_NATS_VALUE && d->nats_msg[0])
            snprintf(msg, sizeof(msg), "%s", d->nats_msg);
        else if (d->kind == DEV_SENSOR_SERIAL_TEXT && serialTextGetMsg()[0])
            snprintf(msg, sizeof(msg), "%s", serialTextGetMsg());

        /* JSON-escape name, extra, msg */
        char e_name[48], e_extra[64], e_msg[96];
        jsonEscapeBuf(e_name, sizeof(e_name), d->name);
        jsonEscapeBuf(e_extra, sizeof(e_extra), extra);
        jsonEscapeBuf(e_msg, sizeof(e_msg), msg);

        /* Raw numeric value for actuators (used by JS for sliders/toggles) */
        w += snprintf(buf + w, sizeof(buf) - w,
            "{\"name\":\"%s\",\"kind\":\"%s\",\"pin\":\"%s\","
            "\"value\":\"%s\",\"extra\":\"%s\",\"msg\":\"%s\",\"internal\":%s,"
            "\"raw\":%d",
            e_name, deviceKindName(d->kind), pin_str,
            val_str, e_extra, e_msg,
            isInternalDevice(d->kind) ? "true" : "false",
            deviceIsActuator(d->kind) ? d->last_value : (int)deviceReadSensor(d));

        /* Append history array for sensors with recorded readings */
        int hcount = d->history_full ? DEV_HISTORY_LEN : d->history_idx;
        if (hcount > 0) {
            w += snprintf(buf + w, sizeof(buf) - w, ",\"hist\":[");
            int hstart = d->history_full ? d->history_idx : 0;
            for (int h = 0; h < hcount; h++) {
                if (h > 0) buf[w++] = ',';
                int idx = (hstart + h) % DEV_HISTORY_LEN;
                w += snprintf(buf + w, sizeof(buf) - w, "%.1f", d->history[idx]);
            }
            w += snprintf(buf + w, sizeof(buf) - w, "]");
        }

        buf[w++] = '}';
    }

    w += snprintf(buf + w, sizeof(buf) - w, "]");
    server.send(200, "application/json", buf);
}

static void handleDeleteDevice() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    char name[DEV_NAME_LEN];
    if (!wcJsonGetString(server.arg("plain").c_str(), "name", name, sizeof(name))) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing name\"}");
        return;
    }
    Device *dev = deviceFind(name);
    if (dev && isInternalDevice(dev->kind)) {
        server.send(403, "application/json", "{\"ok\":false,\"error\":\"cannot delete internal device\"}");
        return;
    }
    bool ok = deviceRemove(name);
    if (ok) devicesSave();
    server.send(ok ? 200 : 404, "application/json",
        ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"not found\"}");
}

static void handleAddDevice() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    const char *body = server.arg("plain").c_str();

    char name[DEV_NAME_LEN];
    char kind_str[24];
    if (!wcJsonGetString(body, "name", name, sizeof(name))) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing name\"}");
        return;
    }
    if (!wcJsonGetString(body, "kind", kind_str, sizeof(kind_str))) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing kind\"}");
        return;
    }

    int pin = wcJsonGetInt(body, "pin", PIN_NONE);
    bool inverted = wcJsonGetBool(body, "inverted", false);
    uint32_t baud = (uint32_t)wcJsonGetInt(body, "baud", 0);

    /* Determine DeviceKind */
    DeviceKind kind;
    if (strcmp(kind_str, "ntc_10k") == 0)         kind = DEV_SENSOR_NTC_10K;
    else if (strcmp(kind_str, "ldr") == 0)         kind = DEV_SENSOR_LDR;
    else if (strcmp(kind_str, "analog_in") == 0)   kind = DEV_SENSOR_ANALOG_RAW;
    else if (strcmp(kind_str, "digital_in") == 0)  kind = DEV_SENSOR_DIGITAL;
    else if (strcmp(kind_str, "digital_out") == 0) kind = DEV_ACTUATOR_DIGITAL;
    else if (strcmp(kind_str, "relay") == 0)       kind = DEV_ACTUATOR_RELAY;
    else if (strcmp(kind_str, "pwm") == 0)         kind = DEV_ACTUATOR_PWM;
    else if (strcmp(kind_str, "serial_text") == 0) kind = DEV_SENSOR_SERIAL_TEXT;
    else {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"unknown kind\"}");
        return;
    }

    /* serial_text has no pin */
    if (kind == DEV_SENSOR_SERIAL_TEXT) pin = PIN_NONE;

    /* Default unit */
    const char *unit = "";
    if (kind == DEV_SENSOR_NTC_10K) unit = "C";
    else if (kind == DEV_SENSOR_LDR) unit = "%";

    bool ok = deviceRegister(name, kind, (uint8_t)pin, unit, inverted, nullptr, baud);
    if (ok) devicesSave();

    static char resp[128];
    if (ok)
        snprintf(resp, sizeof(resp), "{\"ok\":true}");
    else
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"register failed (duplicate or full)\"}");
    server.send(ok ? 200 : 400, "application/json", resp);
}

static void handleSetDevice() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    const char *body = server.arg("plain").c_str();

    char name[DEV_NAME_LEN];
    if (!wcJsonGetString(body, "name", name, sizeof(name))) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing name\"}");
        return;
    }

    int value = wcJsonGetInt(body, "value", 0);

    Device *dev = deviceFind(name);
    if (!dev) {
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    if (!deviceIsActuator(dev->kind)) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"not an actuator\"}");
        return;
    }

    bool ok = deviceSetActuator(dev, value);
    server.send(ok ? 200 : 500, "application/json",
        ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"set failed\"}");
}

static void handleGetDevicesJson() {
    static char buf[2048];
    int len = wcReadFile("/devices.json", buf, sizeof(buf));
    if (len <= 0) {
        server.send(200, "text/plain", "[]");
        return;
    }
    server.send(200, "text/plain", buf);
}

static void handlePostDevicesJson() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    const String &body = server.arg("plain");

    File f = LittleFS.open("/devices.json", "w");
    if (!f) {
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"write failed\"}");
        return;
    }
    f.print(body);
    f.close();

    devicesReload();

    Serial.printf("[WebConfig] devices.json overwritten + reloaded\n");
    server.send(200, "application/json", "{\"ok\":true,\"message\":\"Devices reloaded.\"}");
}

/*============================================================================
 * Pins API
 *============================================================================*/

static void handlePins() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    const char *body = server.arg("plain").c_str();

    int pin = wcJsonGetInt(body, "pin", -1);
    if (pin < 0 || pin >= SOC_GPIO_PIN_COUNT) {
        server.send(400, "application/json", "{\"error\":\"invalid pin\"}");
        return;
    }

    char type[8];
    if (!wcJsonGetString(body, "type", type, sizeof(type))) {
        server.send(400, "application/json", "{\"error\":\"missing type\"}");
        return;
    }

    char action[8];
    if (!wcJsonGetString(body, "action", action, sizeof(action))) {
        server.send(400, "application/json", "{\"error\":\"missing action\"}");
        return;
    }

    static char resp[64];

    if (strcmp(type, "GPIO") == 0) {
        if (strcmp(action, "read") == 0) {
            int val = digitalRead(pin);
            snprintf(resp, sizeof(resp), "{\"value\":%d}", val);
        } else {
            int val = wcJsonGetInt(body, "value", 0);
            pinMode(pin, OUTPUT);
            digitalWrite(pin, val ? HIGH : LOW);
            snprintf(resp, sizeof(resp), "{\"ok\":true}");
        }
    } else if (strcmp(type, "ADC") == 0) {
        int val = analogRead(pin);
        snprintf(resp, sizeof(resp), "{\"value\":%d}", val);
    } else if (strcmp(type, "PWM") == 0) {
        if (strcmp(action, "read") == 0) {
            int val = halPwmGet((uint8_t)pin);
            snprintf(resp, sizeof(resp), "{\"value\":%d}", val);
        } else {
            int val = wcJsonGetInt(body, "value", 0);
            halPwmSet((uint8_t)pin, (uint8_t)constrain(val, 0, 255));
            snprintf(resp, sizeof(resp), "{\"ok\":true}");
        }
    } else {
        server.send(400, "application/json", "{\"error\":\"unknown type\"}");
        return;
    }

    server.send(200, "application/json", resp);
}

/*============================================================================
 * HTML UI (PROGMEM)
 *============================================================================*/

static const char WEB_CONFIG_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>IOnode Config</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
--bg:#08090e;--bg2:#0d1019;--bg3:#141822;
--accent:#00d4aa;--accent-dim:rgba(0,212,170,0.15);--accent-glow:rgba(0,212,170,0.25);
--text:#e8eaf0;--text2:#8b92a8;--text3:#4a5068;
--border:rgba(255,255,255,0.06);--border-a:rgba(0,212,170,0.25);
--red:#ff4757;
--font:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
--mono:"SF Mono","Cascadia Code","Fira Code",Consolas,monospace;
}
body{font-family:var(--font);background:var(--bg);color:var(--text);min-height:100vh}
.wrap{max-width:640px;margin:0 auto;padding:1rem}
header{display:flex;align-items:center;gap:0.75rem;margin-bottom:1.5rem}
header h1{font-size:1.25rem;font-weight:700}
header .ver{font-family:var(--mono);font-size:0.75rem;color:var(--accent);background:var(--accent-dim);
border:1px solid var(--border-a);border-radius:9999px;padding:0.2rem 0.6rem}
nav{display:flex;gap:0.5rem;margin-bottom:1.5rem;border-bottom:1px solid var(--border);padding-bottom:0.5rem}
nav button{background:none;border:none;color:var(--text2);font-family:var(--font);font-size:0.9rem;
font-weight:500;padding:0.5rem 1rem;cursor:pointer;border-radius:8px 8px 0 0;
border-bottom:2px solid transparent;transition:all 0.15s}
nav button:hover{color:var(--text)}
nav button.active{color:var(--accent);border-bottom-color:var(--accent)}
.tab{display:none}.tab.active{display:block}
.card{background:var(--bg3);border:1px solid var(--border);border-radius:12px;padding:1.5rem;margin-bottom:1rem}
label{display:block;font-size:0.8rem;color:var(--accent);font-weight:600;margin:1rem 0 0.25rem;
font-family:var(--mono);text-transform:uppercase;letter-spacing:0.04em}
label:first-child{margin-top:0}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:0.6rem 0.75rem;
background:var(--bg2);border:1px solid var(--border);border-radius:8px;color:var(--text);
font-family:var(--mono);font-size:0.85rem;transition:border-color 0.15s}
input:focus,select:focus{outline:none;border-color:var(--border-a)}
select{appearance:none;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' fill='%238b92a8'%3E%3Cpath d='M6 8L1 3h10z'/%3E%3C/svg%3E");
background-repeat:no-repeat;background-position:right 0.75rem center;padding-right:2rem}
textarea{width:100%;padding:0.75rem;background:var(--bg2);border:1px solid var(--border);
border-radius:8px;color:var(--text);font-family:var(--mono);font-size:0.85rem;
resize:vertical;min-height:200px;line-height:1.6;transition:border-color 0.15s}
textarea:focus{outline:none;border-color:var(--border-a)}
.hint{font-size:0.75rem;color:var(--text3);margin-top:0.2rem}
.btn{display:inline-flex;align-items:center;gap:0.5rem;padding:0.6rem 1.25rem;border:none;
border-radius:8px;font-weight:600;font-size:0.9rem;cursor:pointer;transition:all 0.2s;
font-family:var(--font)}
.btn-primary{background:var(--accent);color:var(--bg)}
.btn-primary:hover{box-shadow:0 0 20px var(--accent-glow)}
.btn-danger{background:var(--red);color:#fff}
.btn-danger:hover{box-shadow:0 0 20px rgba(255,71,87,0.3)}
.btn-outline{background:transparent;color:var(--text);border:1px solid var(--border-a)}
.btn-outline:hover{border-color:var(--accent);color:var(--accent)}
.actions{display:flex;gap:0.75rem;margin-top:1.25rem;flex-wrap:wrap}
.sep{border-top:1px solid var(--border);margin:1rem 0}
.toast{position:fixed;bottom:1.5rem;left:50%;transform:translateX(-50%);padding:0.6rem 1.25rem;
border-radius:8px;font-size:0.85rem;font-weight:500;z-index:999;opacity:0;
transition:opacity 0.3s;pointer-events:none}
.toast.show{opacity:1}
.toast.ok{background:var(--accent);color:var(--bg)}
.toast.err{background:var(--red);color:#fff}
.status-grid{display:grid;grid-template-columns:1fr 1fr;gap:0.75rem}
.status-item{background:var(--bg2);border:1px solid var(--border);border-radius:8px;padding:0.75rem}
.status-item .label{font-size:0.7rem;color:var(--text3);font-family:var(--mono);
text-transform:uppercase;letter-spacing:0.04em;margin-bottom:0.25rem}
.status-item .value{font-size:0.95rem;color:var(--text);font-family:var(--mono);word-break:break-all}
.status-item .value.accent{color:var(--accent)}
.status-item.full{grid-column:1/-1}
.dev{background:var(--bg3);border:1px solid var(--border);border-radius:10px;padding:1rem;margin-bottom:0.75rem}
.dev-hdr{display:flex;align-items:center;gap:0.5rem;margin-bottom:0.4rem}
.dev-name{font-family:var(--mono);font-size:0.85rem;color:var(--accent);font-weight:600}
.kind-badge{font-size:0.65rem;font-weight:700;font-family:var(--mono);padding:0.15rem 0.5rem;
border-radius:9999px;letter-spacing:0.04em;background:var(--accent-dim);color:var(--accent);border:1px solid var(--border-a)}
.del{background:none;border:none;color:var(--text3);font-size:1.1rem;cursor:pointer;
padding:0.1rem 0.4rem;border-radius:4px;line-height:1;transition:all 0.15s;margin-left:auto}
.del:hover{color:var(--red);background:rgba(255,71,87,0.12)}
.dev-meta{font-family:var(--mono);font-size:0.75rem;color:var(--text2);margin-bottom:0.3rem}
.spark{display:flex;align-items:flex-end;gap:2px;height:20px;margin-top:4px}
.spark-bar{width:6px;background:var(--accent);border-radius:1px;min-height:2px}
.toggle-wrap{display:flex;gap:0.5rem;margin-top:0.4rem}
.toggle-btn{padding:0.3rem 0.8rem;border:1px solid var(--border);border-radius:6px;
font-family:var(--mono);font-size:0.8rem;cursor:pointer;background:var(--bg2);color:var(--text2);transition:all 0.15s}
.toggle-btn.on{background:var(--accent-dim);color:var(--accent);border-color:var(--border-a)}
.toggle-btn.off-active{background:rgba(255,71,87,0.12);color:var(--red);border-color:rgba(255,71,87,0.25)}
.pwm-wrap{display:flex;align-items:center;gap:0.75rem;margin-top:0.4rem}
.pwm-wrap input[type=range]{flex:1;accent-color:var(--accent);height:6px}
.pwm-val{font-family:var(--mono);font-size:0.85rem;color:var(--accent);min-width:3ch;text-align:right}
.pin-result{margin-top:0.75rem;padding:0.6rem;background:var(--bg2);border:1px solid var(--border);
border-radius:8px;font-family:var(--mono);font-size:0.85rem;color:var(--accent);min-height:2em}
.empty{text-align:center;color:var(--text3);padding:2rem 0;font-size:0.9rem}
.hidden{display:none}
@media(max-width:480px){
.wrap{padding:0.75rem}
.card{padding:1rem}
.status-grid{grid-template-columns:1fr}
nav button{padding:0.4rem 0.6rem;font-size:0.8rem}
}
</style></head><body>
<div class="wrap">
<header>
<h1>IOnode</h1>
<span class="ver" id="hdr-ver"></span>
</header>
<nav>
<button class="active" onclick="showTab('config',this)">Config</button>
<button onclick="showTab('devices',this)">Devices</button>
<button onclick="showTab('pins',this)">Pins</button>
<button onclick="showTab('status',this)">Status</button>
</nav>

<div id="config" class="tab active">
<div class="card">
<label>WiFi SSID</label>
<input type="text" id="c_wifi_ssid">
<label>WiFi Password</label>
<input type="password" id="c_wifi_pass">
<div class="sep"></div>
<label>Device Name</label>
<input type="text" id="c_device_name">
<div class="sep"></div>
<label>NATS Host</label>
<input type="text" id="c_nats_host">
<label>NATS Port</label>
<input type="number" id="c_nats_port">
<div class="sep"></div>
<label>Timezone</label>
<input type="text" id="c_timezone">
<p class="hint">POSIX TZ string, e.g. CET-1CEST,M3.5.0,M10.5.0/3</p>
<div class="actions">
<button class="btn btn-primary" onclick="saveConfig()">Save Config</button>
<button class="btn btn-danger" onclick="reboot()">Reboot</button>
</div>
<p class="hint" style="margin-top:0.75rem">Reboot required to apply config changes.</p>
</div>

<div class="card">
<label>devices.json</label>
<textarea id="dj_text" rows="8" readonly></textarea>
<div class="actions" id="dj_actions_view">
<button class="btn btn-outline" onclick="djEdit()">Edit</button>
<button class="btn btn-outline" onclick="djLoad()">Reload</button>
</div>
<div class="actions hidden" id="dj_actions_edit">
<button class="btn btn-primary" onclick="djSave()">Save JSON</button>
<button class="btn btn-outline" onclick="djCancel()">Cancel</button>
</div>
</div>
</div>

<div id="devices" class="tab">
<div class="card">
<label>Add Device</label>
<div style="display:grid;grid-template-columns:1fr 1fr;gap:0.5rem;margin-top:0.5rem">
<div>
<label style="margin-top:0">Name</label>
<input type="text" id="ad_name" placeholder="e.g. led1">
</div>
<div>
<label style="margin-top:0">Kind</label>
<select id="ad_kind" onchange="adKindChange()">
<option value="ntc_10k">ntc_10k</option>
<option value="ldr">ldr</option>
<option value="analog_in">analog_in</option>
<option value="digital_in">digital_in</option>
<option value="digital_out">digital_out</option>
<option value="relay">relay</option>
<option value="pwm">pwm</option>
<option value="serial_text">serial_text</option>
</select>
</div>
</div>
<div id="ad_pin_wrap">
<label>Pin</label>
<input type="number" id="ad_pin" placeholder="GPIO number">
</div>
<div id="ad_inv_wrap" class="hidden">
<label><input type="checkbox" id="ad_inv"> Inverted</label>
</div>
<div id="ad_baud_wrap" class="hidden">
<label>Baud Rate</label>
<input type="number" id="ad_baud" value="9600">
</div>
<div class="actions">
<button class="btn btn-primary" onclick="addDevice()">Add Device</button>
</div>
</div>

<div id="devices-list"></div>
</div>

<div id="pins" class="tab">
<div class="card">
<label>Pin Control</label>
<div style="display:grid;grid-template-columns:1fr 1fr;gap:0.5rem;margin-top:0.5rem">
<div>
<label style="margin-top:0">Pin</label>
<input type="number" id="pin_num" placeholder="GPIO number">
</div>
<div>
<label style="margin-top:0">Type</label>
<select id="pin_type" onchange="pinTypeChange()">
<option value="GPIO">GPIO</option>
<option value="ADC">ADC</option>
<option value="PWM">PWM</option>
</select>
</div>
</div>
<div id="pin_val_wrap">
<label>Value</label>
<input type="number" id="pin_val" placeholder="0 or 1 for GPIO, 0-255 for PWM">
</div>
<div class="actions">
<button class="btn btn-outline" onclick="pinAction('read')">Read</button>
<button class="btn btn-primary" id="pin_write_btn" onclick="pinAction('write')">Write</button>
</div>
<div class="pin-result" id="pin_result"></div>
</div>
</div>

<div id="status" class="tab">
<div class="card">
<div class="status-grid" id="status-grid"></div>
<div class="actions">
<button class="btn btn-outline" onclick="loadStatus()">Refresh</button>
<button class="btn btn-danger" onclick="reboot()">Reboot</button>
</div>
</div>
</div>
</div>

<div class="toast" id="toast"></div>

<script>
var devTimer=null;
function showTab(id,btn){
document.querySelectorAll('.tab').forEach(function(t){t.classList.remove('active')});
document.querySelectorAll('nav button').forEach(function(b){b.classList.remove('active')});
document.getElementById(id).classList.add('active');
if(btn)btn.classList.add('active');
if(devTimer){clearInterval(devTimer);devTimer=null}
if(id==='status')loadStatus();
if(id==='devices'){loadDevices();devTimer=setInterval(loadDevices,3000)}
if(id==='config')djLoad();
}
function toast(msg,ok){
var t=document.getElementById('toast');
t.textContent=msg;t.className='toast show '+(ok?'ok':'err');
setTimeout(function(){t.className='toast'},2500);
}
function loadConfig(){
fetch('/api/config').then(function(r){return r.json()}).then(function(d){
var f=['wifi_ssid','wifi_pass','device_name','nats_host','nats_port','timezone'];
f.forEach(function(k){var el=document.getElementById('c_'+k);if(el)el.value=d[k]||''});
}).catch(function(){toast('Failed to load config',false)});
}
function saveConfig(){
var f=['wifi_ssid','wifi_pass','device_name','nats_host','nats_port','timezone'];
var d={};f.forEach(function(k){d[k]=document.getElementById('c_'+k).value});
fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify(d)}).then(function(r){return r.json()}).then(function(j){
toast(j.message||'Saved',j.ok!==false);
}).catch(function(){toast('Save failed',false)});
}
function djLoad(){
fetch('/api/devices/json').then(function(r){return r.text()}).then(function(t){
try{document.getElementById('dj_text').value=JSON.stringify(JSON.parse(t),null,2)}
catch(e){document.getElementById('dj_text').value=t}
}).catch(function(){});
}
function djEdit(){
document.getElementById('dj_text').removeAttribute('readonly');
document.getElementById('dj_actions_view').classList.add('hidden');
document.getElementById('dj_actions_edit').classList.remove('hidden');
}
function djCancel(){
document.getElementById('dj_text').setAttribute('readonly','');
document.getElementById('dj_actions_view').classList.remove('hidden');
document.getElementById('dj_actions_edit').classList.add('hidden');
djLoad();
}
function djSave(){
var t=document.getElementById('dj_text').value;
try{JSON.parse(t)}catch(e){toast('Invalid JSON: '+e.message,false);return}
fetch('/api/devices/json',{method:'POST',headers:{'Content-Type':'application/json'},
body:t}).then(function(r){return r.json()}).then(function(j){
toast(j.message||'Saved',j.ok!==false);
document.getElementById('dj_text').setAttribute('readonly','');
document.getElementById('dj_actions_view').classList.remove('hidden');
document.getElementById('dj_actions_edit').classList.add('hidden');
if(devTimer)loadDevices();
}).catch(function(){toast('Save failed',false)});
}
function adKindChange(){
var k=document.getElementById('ad_kind').value;
document.getElementById('ad_pin_wrap').classList.toggle('hidden',k==='serial_text');
document.getElementById('ad_inv_wrap').classList.toggle('hidden',k!=='relay');
document.getElementById('ad_baud_wrap').classList.toggle('hidden',k!=='serial_text');
}
function addDevice(){
var name=document.getElementById('ad_name').value.trim();
var kind=document.getElementById('ad_kind').value;
if(!name){toast('Name required',false);return}
var d={name:name,kind:kind};
if(kind==='serial_text'){
d.baud=parseInt(document.getElementById('ad_baud').value)||9600;
}else{
var pin=parseInt(document.getElementById('ad_pin').value);
if(isNaN(pin)){toast('Pin required',false);return}
d.pin=pin;
if(kind==='relay')d.inverted=document.getElementById('ad_inv').checked;
}
fetch('/api/devices/add',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify(d)}).then(function(r){return r.json()}).then(function(j){
toast(j.ok?'Device added':(j.error||'Failed'),j.ok);
if(j.ok){document.getElementById('ad_name').value='';loadDevices();djLoad()}
}).catch(function(){toast('Add failed',false)});
}
function loadDevices(){
fetch('/api/devices').then(function(r){return r.json()}).then(function(devs){
var c=document.getElementById('devices-list');
if(!devs.length){c.innerHTML='<div class="empty">No devices registered.</div>';return}
var h='';devs.forEach(function(d){
h+='<div class="dev"><div class="dev-hdr"><span class="dev-name">'+d.name+'</span>';
h+='<span class="kind-badge">'+d.kind+'</span>';
if(!d.internal)h+='<button class="del" onclick="deleteDevice(\''+d.name+'\')">&times;</button>';
h+='</div>';
if(d.kind==='digital_out'||d.kind==='relay'){
var isOn=d.raw!==0;
h+='<div class="dev-meta">pin '+d.pin+'</div>';
h+='<div class="toggle-wrap">';
h+='<button class="toggle-btn'+(isOn?' on':'')+'" onclick="setDev(\''+d.name+'\',1)">ON</button>';
h+='<button class="toggle-btn'+(!isOn?' off-active':'')+'" onclick="setDev(\''+d.name+'\',0)">OFF</button>';
h+='</div>';
}else if(d.kind==='pwm'){
h+='<div class="dev-meta">pin '+d.pin+'</div>';
h+='<div class="pwm-wrap"><input type="range" min="0" max="255" value="'+d.raw+'" oninput="pwmSlide(this,\''+d.name+'\')"><span class="pwm-val">'+d.raw+'</span></div>';
}else{
if(d.pin!=='virtual')h+='<div class="dev-meta">pin '+d.pin+'</div>';
else if(d.extra)h+='<div class="dev-meta">'+d.extra+'</div>';
h+='<div class="dev-meta">'+d.value+'</div>';
if(d.msg)h+='<div class="dev-meta" style="color:var(--text3)">"'+d.msg+'"</div>';
}
if(d.hist&&d.hist.length>1){var mn=Math.min.apply(null,d.hist),mx=Math.max.apply(null,d.hist),rng=mx-mn||1,bars='';d.hist.forEach(function(v){var pct=Math.round(((v-mn)/rng)*100);bars+='<span class="spark-bar" style="height:'+Math.max(pct,5)+'%"></span>'});h+='<div class="spark">'+bars+'</div>'}
h+='</div>'});
c.innerHTML=h;
}).catch(function(){});
}
var pwmTimers={};
function pwmSlide(el,name){
el.nextElementSibling.textContent=el.value;
if(pwmTimers[name])clearTimeout(pwmTimers[name]);
pwmTimers[name]=setTimeout(function(){setDev(name,parseInt(el.value))},300);
}
function setDev(name,val){
fetch('/api/devices/set',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({name:name,value:val})}).then(function(r){return r.json()}).then(function(j){
if(!j.ok)toast(j.error||'Failed',false);
}).catch(function(){toast('Set failed',false)});
}
function deleteDevice(name){
if(!confirm('Delete device "'+name+'"?'))return;
fetch('/api/devices/delete',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({name:name})}).then(function(r){return r.json()}).then(function(j){
toast(j.ok?'Deleted':(j.error||'Failed'),j.ok);
if(j.ok){loadDevices();djLoad()}
}).catch(function(){toast('Delete failed',false)});
}
function loadStatus(){
fetch('/api/status').then(function(r){return r.json()}).then(function(d){
var items=[
{l:'Version',v:d.version,cls:'accent'},
{l:'Device',v:d.device_name},
{l:'Uptime',v:d.uptime},
{l:'Heap',v:Math.round(d.heap_free/1024)+'KB / '+Math.round(d.heap_total/1024)+'KB'},
{l:'WiFi',v:d.wifi_ssid+' ('+d.wifi_rssi+'dBm)',full:true},
{l:'IP Address',v:d.wifi_ip,cls:'accent'},
{l:'NATS',v:d.nats}
];
var h='';items.forEach(function(i){
h+='<div class="status-item'+(i.full?' full':'')+'"><div class="label">'+i.l+
'</div><div class="value'+(i.cls?' '+i.cls:'')+'">'+i.v+'</div></div>';
});
document.getElementById('status-grid').innerHTML=h;
}).catch(function(){toast('Failed to load status',false)});
}
function pinTypeChange(){
var t=document.getElementById('pin_type').value;
var vw=document.getElementById('pin_val_wrap');
var wb=document.getElementById('pin_write_btn');
if(t==='ADC'){vw.classList.add('hidden');wb.classList.add('hidden')}
else{vw.classList.remove('hidden');wb.classList.remove('hidden')}
}
function pinAction(action){
var pin=parseInt(document.getElementById('pin_num').value);
if(isNaN(pin)){toast('Enter a pin number',false);return}
var type=document.getElementById('pin_type').value;
var d={pin:pin,type:type,action:action};
if(action==='write')d.value=parseInt(document.getElementById('pin_val').value)||0;
fetch('/api/pins',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify(d)}).then(function(r){return r.json()}).then(function(j){
var el=document.getElementById('pin_result');
if(j.value!==undefined)el.textContent=type+' pin '+pin+' = '+j.value;
else if(j.ok)el.textContent='OK';
else el.textContent='Error: '+(j.error||'unknown');
}).catch(function(){toast('Request failed',false)});
}
function reboot(){
if(!confirm('Reboot device?'))return;
fetch('/api/reboot',{method:'POST'}).then(function(){
toast('Rebooting...',true);
}).catch(function(){toast('Rebooting...',true)});
}
loadConfig();
djLoad();
fetch('/api/status').then(function(r){return r.json()}).then(function(d){
document.getElementById('hdr-ver').textContent='v'+d.version;
}).catch(function(){});
</script>
</body></html>)rawhtml";

/*============================================================================
 * Setup & Loop
 *============================================================================*/

void webConfigSetup() {
    /* mDNS */
    if (MDNS.begin(cfg_device_name)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mDNS: http://%s.local/\n", cfg_device_name);
    } else {
        Serial.printf("mDNS: failed to start\n");
    }

    /* Routes */
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", WEB_CONFIG_HTML);
    });

    server.on("/api/config", HTTP_GET, handleGetConfig);
    server.on("/api/config", HTTP_POST, handlePostConfig);
    server.on("/api/status", HTTP_GET, handleGetStatus);
    server.on("/api/devices", HTTP_GET, handleGetDevices);
    server.on("/api/devices/delete", HTTP_POST, handleDeleteDevice);
    server.on("/api/devices/add", HTTP_POST, handleAddDevice);
    server.on("/api/devices/set", HTTP_POST, handleSetDevice);
    server.on("/api/devices/json", HTTP_GET, handleGetDevicesJson);
    server.on("/api/devices/json", HTTP_POST, handlePostDevicesJson);
    server.on("/api/pins", HTTP_POST, handlePins);
    server.on("/api/reboot", HTTP_POST, handleReboot);

    server.begin();
    Serial.printf("WebConfig: http://%s/\n", WiFi.localIP().toString().c_str());
}

void webConfigLoop() {
    server.handleClient();
}
