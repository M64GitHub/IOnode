/**
 * @file setup_portal.cpp
 * @brief WiFi AP captive portal for initial device configuration
 *
 * Starts an open AP ("IOnode-Setup"), runs a DNS captive portal that
 * redirects all domains to 192.168.4.1, and serves a config form on port 80.
 * When the user submits, writes /config.json to LittleFS and reboots.
 */

#include "setup_portal.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WiFiServer.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>

/* LED functions from main.cpp */
extern void led(uint8_t r, uint8_t g, uint8_t b);
extern void ledOff();

#define PORTAL_TIMEOUT_MS 600000 /* 10 minutes */

/*============================================================================
 * HTML Pages (stored in flash)
 *============================================================================*/

static const char SETUP_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>IOnode Setup</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Courier New',monospace;background:#08090e;color:#e8eaf0;padding:20px;max-width:480px;margin:0 auto}
.logo{display:flex;align-items:center;gap:0.5rem;margin-bottom:4px}
h1{color:#ff8c00;font-size:1.5em}
.sub{color:#8b92a8;font-size:0.85em;margin-bottom:20px}
label{display:block;margin:12px 0 4px;color:#ff8c00;font-size:0.9em}
input[type=text],input[type=password]{width:100%;padding:10px;background:#0d1019;border:1px solid rgba(255,255,255,0.06);color:#fff;font-family:inherit;font-size:0.95em;border-radius:4px}
input:focus{outline:none;border-color:#ff8c00}
.opt{color:#4a5068;font-size:0.8em}
.sep{border-top:1px solid rgba(255,255,255,0.06);margin:16px 0}
button{width:100%;padding:12px;margin-top:20px;background:#ff8c00;color:#08090e;border:none;font-family:inherit;font-size:1em;font-weight:bold;cursor:pointer;border-radius:4px}
button:hover{background:#ffa333}
</style></head><body>
<div class="logo"><svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" fill="none"><rect x="4" y="4" width="24" height="24" rx="4" stroke="#ff8c00" stroke-width="2"/><circle cx="16" cy="16" r="4" fill="#ff8c00"/><line x1="16" y1="4" x2="16" y2="10" stroke="#ff8c00" stroke-width="2" stroke-linecap="round"/><line x1="16" y1="22" x2="16" y2="28" stroke="#ff8c00" stroke-width="2" stroke-linecap="round"/><line x1="4" y1="16" x2="10" y2="16" stroke="#ff8c00" stroke-width="2" stroke-linecap="round"/><line x1="22" y1="16" x2="28" y2="16" stroke="#ff8c00" stroke-width="2" stroke-linecap="round"/></svg><h1>&gt; IOnode Setup</h1></div>
<p class="sub">Configure your hardware node</p>
<form method="POST" action="/save">
<label>WiFi SSID *</label>
<input type="text" name="wifi_ssid" required>
<label>WiFi Password *</label>
<input type="password" name="wifi_pass" required>
<div class="sep"></div>
<label>Device Name</label>
<input type="text" name="device_name" value="ionode-01">
<div class="sep"></div>
<label>NATS Host</label>
<input type="text" name="nats_host" placeholder="192.168.1.x">
<label>NATS Port</label>
<input type="text" name="nats_port" value="4222">
<div class="sep"></div>
<label>Timezone</label>
<input type="text" name="timezone" value="UTC0">
<p class="opt">POSIX TZ string (e.g. CET-1CEST,M3.5.0,M10.5.0/3)</p>
<button type="submit">Save &amp; Reboot</button>
</form></body></html>)rawhtml";

static const char SAVED_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>IOnode - Saved</title><style>
body{font-family:'Courier New',monospace;background:#08090e;color:#ff8c00;display:flex;align-items:center;justify-content:center;min-height:100vh;text-align:center}
h1{font-size:1.5em;margin-bottom:8px}p{color:#8b92a8}
</style></head><body>
<div><h1>Config saved!</h1><p>Rebooting...</p></div>
</body></html>)rawhtml";

/*============================================================================
 * Helpers
 *============================================================================*/

static void urlDecode(char *dst, const char *src, int maxLen) {
    int w = 0;
    while (*src && w < maxLen - 1) {
        if (*src == '+') {
            dst[w++] = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            dst[w++] = (char)strtol(hex, nullptr, 16);
            src += 3;
        } else {
            dst[w++] = *src++;
        }
    }
    dst[w] = '\0';
}

/**
 * Extract a URL-encoded form field from a POST body.
 * Handles field names correctly (won't match partial names).
 */
static bool formGetField(const char *body, const char *name,
                          char *dst, int maxLen) {
    char key[64];
    snprintf(key, sizeof(key), "%s=", name);
    int keyLen = strlen(key);

    const char *p = body;
    while ((p = strstr(p, key)) != nullptr) {
        /* Ensure it's at start of body or after & */
        if (p == body || *(p - 1) == '&') {
            p += keyLen;
            /* Extract raw value until & or end */
            char encoded[256];
            int w = 0;
            while (*p && *p != '&' && w < (int)sizeof(encoded) - 1) {
                encoded[w++] = *p++;
            }
            encoded[w] = '\0';
            urlDecode(dst, encoded, maxLen);
            return true;
        }
        p++;
    }
    dst[0] = '\0';
    return false;
}

/** Write a JSON-escaped string value to a file */
static void writeJsonEscaped(File &f, const char *s) {
    f.print('"');
    while (*s) {
        if (*s == '"' || *s == '\\') f.print('\\');
        f.print(*s);
        s++;
    }
    f.print('"');
}

/*============================================================================
 * Config Writing
 *============================================================================*/

static bool saveConfig(const char *body) {
    /* Ensure LittleFS is mounted (format if needed for fresh flash) */
    if (!LittleFS.begin(true)) {
        Serial.printf("[Setup] LittleFS mount failed\n");
        return false;
    }

    File f = LittleFS.open("/config.json", "w");
    if (!f) {
        Serial.printf("[Setup] Failed to open /config.json for writing\n");
        return false;
    }

    char val[128];

    f.print("{\n");

    formGetField(body, "wifi_ssid", val, sizeof(val));
    f.print("  \"wifi_ssid\": "); writeJsonEscaped(f, val); f.print(",\n");

    formGetField(body, "wifi_pass", val, sizeof(val));
    f.print("  \"wifi_pass\": "); writeJsonEscaped(f, val); f.print(",\n");

    formGetField(body, "device_name", val, sizeof(val));
    if (val[0] == '\0') strncpy(val, "ionode-01", sizeof(val));
    f.print("  \"device_name\": "); writeJsonEscaped(f, val); f.print(",\n");

    formGetField(body, "nats_host", val, sizeof(val));
    f.print("  \"nats_host\": "); writeJsonEscaped(f, val); f.print(",\n");

    formGetField(body, "nats_port", val, sizeof(val));
    if (val[0] == '\0') strncpy(val, "4222", sizeof(val));
    f.print("  \"nats_port\": "); writeJsonEscaped(f, val); f.print(",\n");

    formGetField(body, "timezone", val, sizeof(val));
    if (val[0] == '\0') strncpy(val, "UTC0", sizeof(val));
    f.print("  \"timezone\": "); writeJsonEscaped(f, val); f.print("\n");

    f.print("}\n");
    f.close();

    Serial.printf("[Setup] Config saved to /config.json\n");
    return true;
}

/*============================================================================
 * HTTP Handler
 *============================================================================*/

static void sendHtml(WiFiClient &client, const char *html) {
    int len = strlen(html);
    client.printf("HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/html; charset=utf-8\r\n"
                  "Connection: close\r\n"
                  "Content-Length: %d\r\n\r\n", len);
    /* Send in chunks to avoid WiFiClient buffer limits */
    const uint8_t *p = (const uint8_t *)html;
    while (len > 0) {
        int chunk = len > 1024 ? 1024 : len;
        client.write(p, chunk);
        p += chunk;
        len -= chunk;
    }
}

static bool handleClient(WiFiClient &client) {
    /* Wait for data with timeout */
    unsigned long timeout = millis() + 3000;
    while (!client.available() && millis() < timeout) {
        delay(1);
    }
    if (!client.available()) {
        client.stop();
        return false;
    }

    /* Read request line */
    String requestLine = client.readStringUntil('\n');
    requestLine.trim();

    bool isPost = requestLine.startsWith("POST");

    /* Read headers */
    int contentLength = 0;
    while (client.connected()) {
        String header = client.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) break;
        if (header.startsWith("Content-Length:") ||
            header.startsWith("content-length:")) {
            contentLength = header.substring(15).toInt();
        }
    }

    if (isPost && contentLength > 0) {
        /* Read POST body */
        static char body[2048];
        int toRead = contentLength < (int)sizeof(body) - 1
                     ? contentLength : (int)sizeof(body) - 1;
        int bytesRead = client.readBytes(body, toRead);
        body[bytesRead] = '\0';

        if (saveConfig(body)) {
            sendHtml(client, SAVED_HTML);
            client.stop();
            Serial.printf("[Setup] Config saved, rebooting in 2s...\n");
            delay(2000);
            ESP.restart();
            return true; /* unreachable */
        } else {
            client.print("HTTP/1.1 500 Error\r\n"
                         "Content-Type: text/plain\r\n"
                         "Connection: close\r\n\r\n"
                         "Failed to save config. Try again.");
        }
    } else {
        /* Serve the setup form for any GET request */
        sendHtml(client, SETUP_HTML);
    }

    client.stop();
    return false;
}

/*============================================================================
 * Main Portal Entry Point
 *============================================================================*/

void runSetupPortal() {
    /* Wait for serial monitor to connect (USB-CDC can be slow) */
    Serial.printf("[Setup] Waiting for serial connection...\n");
    unsigned long serialWait = millis();
    while (!Serial && millis() - serialWait < 3000) {
        delay(100);
    }

    /* Switch to AP mode */
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("IOnode-Setup");
    delay(500); /* Let AP settle */

    /* Register with watchdog (setup() hasn't done this yet) */
    esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 60000, .idle_core_mask = 0,
                                       .trigger_panic = true };
    esp_task_wdt_reconfigure(&wdt_cfg);
    esp_task_wdt_add(NULL);

    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[Setup] AP started on %s\n", apIP.toString().c_str());
    Serial.printf("[Setup] Connect to WiFi 'IOnode-Setup' to configure\n");
    Serial.printf("[Setup] Portal timeout: %d seconds\n", PORTAL_TIMEOUT_MS / 1000);

    /* DNS captive portal — resolve all domains to our IP */
    DNSServer dns;
    dns.start(53, "*", apIP);

    /* Web server on port 80 */
    WiFiServer server(80);
    server.begin();

    unsigned long startTime = millis();

    while (millis() - startTime < PORTAL_TIMEOUT_MS) {
        esp_task_wdt_reset();
        dns.processNextRequest();

        /* Pulsing cyan LED — triangle wave, 2s period */
        unsigned long ms = millis() % 2000;
        uint8_t brightness = (ms < 1000)
            ? (uint8_t)(ms * 255 / 1000)
            : (uint8_t)((2000 - ms) * 255 / 1000);
        led(brightness, brightness * 140 / 255, 0);

        WiFiClient client = server.accept();
        if (client) {
            handleClient(client);
        }

        delay(5);
    }

    /* Timeout — reboot and try again */
    Serial.printf("[Setup] Portal timeout, rebooting...\n");
    ledOff();
    delay(1000);
    ESP.restart();
}
