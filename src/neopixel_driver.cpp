/**
 * @file neopixel_driver.cpp
 * @brief WS2812/NeoPixel addressable LED strip driver
 */

#include "neopixel_driver.h"
#include "devices.h"
#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel *s_strips[NEOPIXEL_MAX_STRIPS] = {nullptr};
static uint32_t s_last_color[NEOPIXEL_MAX_STRIPS] = {0};

/* Map device-array index (0..MAX_DEVICES-1) → internal strip slot (0..MAX_STRIPS-1).
   -1 means no strip allocated for that device slot. */
static int s_dev_to_strip[MAX_DEVICES];
static bool s_map_init = false;

static void ensureMapInit() {
    if (s_map_init) return;
    for (int i = 0; i < MAX_DEVICES; i++) s_dev_to_strip[i] = -1;
    s_map_init = true;
}

/* Resolve a device slot to an internal strip index, or -1 if none */
static int resolveSlot(int devSlot) {
    ensureMapInit();
    if (devSlot < 0 || devSlot >= MAX_DEVICES) return -1;
    return s_dev_to_strip[devSlot];
}

static neoPixelType neoColorOrderType(uint8_t order) {
    switch (order) {
        default:
        case NEO_ORDER_GRB:  return NEO_GRB  + NEO_KHZ800;
        case NEO_ORDER_RGB:  return NEO_RGB  + NEO_KHZ800;
        case NEO_ORDER_RBG:  return NEO_RBG  + NEO_KHZ800;
        case NEO_ORDER_BRG:  return NEO_BRG  + NEO_KHZ800;
        case NEO_ORDER_BGR:  return NEO_BGR  + NEO_KHZ800;
        case NEO_ORDER_GBR:  return NEO_GBR  + NEO_KHZ800;
        case NEO_ORDER_RGBW: return NEO_RGBW + NEO_KHZ800;
        case NEO_ORDER_GRBW: return NEO_GRBW + NEO_KHZ800;
    }
}

void neopixelInit(int devSlot, uint8_t pin, uint16_t numPixels, uint8_t colorOrder) {
    ensureMapInit();
    if (devSlot < 0 || devSlot >= MAX_DEVICES) return;
    if (numPixels == 0) numPixels = 1;

    /* If this device already has a strip, deinit it first */
    if (s_dev_to_strip[devSlot] >= 0) {
        neopixelDeinit(devSlot);
    }

    /* Find a free internal strip slot */
    int strip = -1;
    for (int i = 0; i < NEOPIXEL_MAX_STRIPS; i++) {
        if (!s_strips[i]) { strip = i; break; }
    }
    if (strip < 0) {
        Serial.printf("NeoPixel: no free strip slot for device %d\n", devSlot);
        return;
    }

    s_dev_to_strip[devSlot] = strip;
    s_strips[strip] = new Adafruit_NeoPixel(numPixels, pin, neoColorOrderType(colorOrder));
    s_strips[strip]->begin();
    s_strips[strip]->clear();
    s_strips[strip]->show();
    s_last_color[strip] = 0;
    Serial.printf("NeoPixel: dev %d -> strip %d, pin %d, %d pixels, order %d\n",
                  devSlot, strip, pin, numPixels, colorOrder);
}

void neopixelDeinit(int devSlot) {
    int strip = resolveSlot(devSlot);
    if (strip < 0 || !s_strips[strip]) return;
    s_strips[strip]->clear();
    s_strips[strip]->show();
    delete s_strips[strip];
    s_strips[strip] = nullptr;
    s_last_color[strip] = 0;
    s_dev_to_strip[devSlot] = -1;
}

void neopixelFill(int devSlot, uint32_t color) {
    int strip = resolveSlot(devSlot);
    if (strip < 0 || !s_strips[strip]) return;
    s_strips[strip]->fill(color);
    s_strips[strip]->show();
    s_last_color[strip] = color;
}

void neopixelSetPixel(int devSlot, uint16_t pixel, uint32_t color) {
    int strip = resolveSlot(devSlot);
    if (strip < 0 || !s_strips[strip]) return;
    if (pixel >= s_strips[strip]->numPixels()) return;
    s_strips[strip]->setPixelColor(pixel, color);
    s_strips[strip]->show();
}

void neopixelSetBrightness(int devSlot, uint8_t brightness) {
    int strip = resolveSlot(devSlot);
    if (strip < 0 || !s_strips[strip]) return;
    s_strips[strip]->setBrightness(brightness);
    s_strips[strip]->show();
}

void neopixelClear(int devSlot) {
    int strip = resolveSlot(devSlot);
    if (strip < 0 || !s_strips[strip]) return;
    s_strips[strip]->clear();
    s_strips[strip]->show();
    s_last_color[strip] = 0;
}

uint8_t neopixelGetBrightness(int devSlot) {
    int strip = resolveSlot(devSlot);
    if (strip < 0 || !s_strips[strip]) return 0;
    return s_strips[strip]->getBrightness();
}

uint16_t neopixelGetCount(int devSlot) {
    int strip = resolveSlot(devSlot);
    if (strip < 0 || !s_strips[strip]) return 0;
    return s_strips[strip]->numPixels();
}

uint32_t neopixelGetColor(int devSlot) {
    int strip = resolveSlot(devSlot);
    if (strip < 0) return 0;
    return s_last_color[strip];
}

/* Parse hex color string (e.g. "FF0000") to uint32_t */
static uint32_t parseHexColor(const char *s) {
    uint32_t color = 0;
    for (int i = 0; i < 6 && s[i]; i++) {
        char c = s[i];
        uint8_t nibble = 0;
        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'a' && c <= 'f') nibble = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') nibble = 10 + c - 'A';
        else break;
        color = (color << 4) | nibble;
    }
    return color;
}

/* Extract a JSON string value for a key into dst. Returns true if found. */
static bool jsonStr(const char *json, const char *key, char *dst, int dst_len) {
    char pattern[32];
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

/* Extract a JSON integer value for a key. Returns default_val if not found. */
static int jsonInt(const char *json, const char *key, int default_val) {
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return default_val;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    return atoi(p);
}

/* Check if a JSON boolean key is true */
static bool jsonBool(const char *json, const char *key) {
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    return strncmp(p, "true", 4) == 0;
}

bool neopixelHandleJson(int devSlot, const char *json, char *reply, int reply_len) {
    int slot = resolveSlot(devSlot);
    if (slot < 0 || !s_strips[slot]) return false;

    char color_str[8];

    /* {"clear":true} */
    if (jsonBool(json, "clear")) {
        neopixelClear(devSlot);
        snprintf(reply, reply_len, "ok");
        return true;
    }

    /* {"brightness":128} */
    if (strstr(json, "\"brightness\"")) {
        int b = jsonInt(json, "brightness", -1);
        if (b >= 0 && b <= 255) {
            neopixelSetBrightness(devSlot, (uint8_t)b);
            snprintf(reply, reply_len, "ok");
            return true;
        }
    }

    /* {"pixel":3,"color":"FF0000"} */
    if (strstr(json, "\"pixel\"")) {
        int pixel = jsonInt(json, "pixel", -1);
        if (pixel >= 0 && jsonStr(json, "color", color_str, sizeof(color_str))) {
            uint32_t color = parseHexColor(color_str);
            neopixelSetPixel(devSlot, (uint16_t)pixel, color);
            snprintf(reply, reply_len, "ok");
            return true;
        }
    }

    /* {"fill":"00FF00"} */
    if (jsonStr(json, "fill", color_str, sizeof(color_str))) {
        uint32_t color = parseHexColor(color_str);
        neopixelFill(devSlot, color);
        snprintf(reply, reply_len, "ok");
        return true;
    }

    return false;
}
