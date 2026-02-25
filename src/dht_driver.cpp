/**
 * @file dht_driver.cpp
 * @brief DHT11/DHT22 single-wire temperature & humidity sensor driver
 *
 * Bit-bang protocol with critical section (interrupts disabled) during
 * the ~4ms read window. Per-pin cache with 2s TTL avoids redundant reads
 * when two devices (temp + humi) share the same GPIO pin.
 */

#include "dht_driver.h"
#include <math.h>

extern bool g_debug;

/*============================================================================
 * Per-pin read cache
 *============================================================================*/

struct DhtCache {
    uint8_t  pin;
    uint32_t last_read_ms;
    float    temp;
    float    humi;
    bool     valid;
};

static DhtCache g_dht_cache[DHT_CACHE_MAX];

static DhtCache *dhtCacheFind(uint8_t pin) {
    for (int i = 0; i < DHT_CACHE_MAX; i++) {
        if (g_dht_cache[i].pin == pin && g_dht_cache[i].valid)
            return &g_dht_cache[i];
    }
    return nullptr;
}

static DhtCache *dhtCacheAlloc(uint8_t pin) {
    /* Find existing or free slot */
    for (int i = 0; i < DHT_CACHE_MAX; i++) {
        if (g_dht_cache[i].pin == pin) return &g_dht_cache[i];
    }
    for (int i = 0; i < DHT_CACHE_MAX; i++) {
        if (!g_dht_cache[i].valid) {
            g_dht_cache[i].pin = pin;
            return &g_dht_cache[i];
        }
    }
    /* Evict oldest */
    int oldest = 0;
    for (int i = 1; i < DHT_CACHE_MAX; i++) {
        if (g_dht_cache[i].last_read_ms < g_dht_cache[oldest].last_read_ms)
            oldest = i;
    }
    g_dht_cache[oldest].pin = pin;
    g_dht_cache[oldest].valid = false;
    return &g_dht_cache[oldest];
}

void dhtCacheInvalidate(uint8_t pin) {
    for (int i = 0; i < DHT_CACHE_MAX; i++) {
        if (g_dht_cache[i].pin == pin) {
            g_dht_cache[i].valid = false;
            g_dht_cache[i].last_read_ms = 0;
        }
    }
}

/*============================================================================
 * Bit-bang driver
 *============================================================================*/

static portMUX_TYPE g_dht_mux = portMUX_INITIALIZER_UNLOCKED;

/**
 * Perform a physical DHT read. Returns true on success.
 * Disables interrupts for ~4ms during the 40-bit transfer.
 */
static bool dhtDoRead(uint8_t pin, float *temp, float *humi, bool is_dht22) {
    uint8_t data[5] = {0};

    /* --- Start signal --- */
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delayMicroseconds(1200);   /* >1ms for DHT22, >18ms for DHT11 — 1.2ms works for both */
    digitalWrite(pin, HIGH);
    delayMicroseconds(30);
    pinMode(pin, INPUT_PULLUP);

    /* --- Critical section: read 40 bits --- */
    portENTER_CRITICAL(&g_dht_mux);

    /* Wait for sensor ACK: LOW ~80µs, then HIGH ~80µs */
    uint32_t timeout;

    timeout = 100;
    while (digitalRead(pin) == HIGH) {
        if (--timeout == 0) { portEXIT_CRITICAL(&g_dht_mux); return false; }
        delayMicroseconds(1);
    }

    timeout = 100;
    while (digitalRead(pin) == LOW) {
        if (--timeout == 0) { portEXIT_CRITICAL(&g_dht_mux); return false; }
        delayMicroseconds(1);
    }

    timeout = 100;
    while (digitalRead(pin) == HIGH) {
        if (--timeout == 0) { portEXIT_CRITICAL(&g_dht_mux); return false; }
        delayMicroseconds(1);
    }

    /* Read 40 bits (5 bytes) */
    for (int i = 0; i < 40; i++) {
        /* Wait for LOW→HIGH transition (bit start) */
        timeout = 100;
        while (digitalRead(pin) == LOW) {
            if (--timeout == 0) { portEXIT_CRITICAL(&g_dht_mux); return false; }
            delayMicroseconds(1);
        }

        /* Measure HIGH duration: >40µs = '1', else '0' */
        uint32_t t0 = micros();
        timeout = 100;
        while (digitalRead(pin) == HIGH) {
            if (--timeout == 0) { portEXIT_CRITICAL(&g_dht_mux); return false; }
            delayMicroseconds(1);
        }
        uint32_t dur = micros() - t0;

        data[i / 8] <<= 1;
        if (dur > 40) data[i / 8] |= 1;
    }

    portEXIT_CRITICAL(&g_dht_mux);

    /* --- Checksum --- */
    uint8_t sum = data[0] + data[1] + data[2] + data[3];
    if (sum != data[4]) {
        if (g_debug)
            Serial.printf("[DHT] checksum fail on pin %d: got 0x%02X expect 0x%02X\n",
                          pin, data[4], sum);
        return false;
    }

    /* --- Decode --- */
    if (is_dht22) {
        /* DHT22: 16-bit values × 0.1, humidity first, temp second */
        int16_t raw_humi = ((uint16_t)data[0] << 8) | data[1];
        int16_t raw_temp = ((uint16_t)data[2] << 8) | data[3];
        /* Sign bit for temperature */
        if (raw_temp & 0x8000) {
            raw_temp = -(raw_temp & 0x7FFF);
        }
        *humi = raw_humi * 0.1f;
        *temp = raw_temp * 0.1f;
    } else {
        /* DHT11: integer bytes (data[0]=humi integer, data[2]=temp integer) */
        *humi = (float)data[0];
        *temp = (float)data[2];
    }

    return true;
}

/*============================================================================
 * Public API
 *============================================================================*/

float dhtRead(uint8_t pin, bool is_dht22, uint8_t channel) {
    uint32_t now = millis();

    /* Check cache */
    DhtCache *c = dhtCacheFind(pin);
    if (c && (now - c->last_read_ms) < DHT_CACHE_TTL_MS) {
        return (channel == DHT_CHAN_TEMP) ? c->temp : c->humi;
    }

    /* Cache miss — perform physical read */
    float temp = NAN, humi = NAN;
    bool ok = dhtDoRead(pin, &temp, &humi, is_dht22);

    if (!ok) {
        if (g_debug)
            Serial.printf("[DHT] read failed on pin %d\n", pin);
        return NAN;
    }

    /* Store in cache */
    c = dhtCacheAlloc(pin);
    c->temp = temp;
    c->humi = humi;
    c->valid = true;
    c->last_read_ms = now;

    if (g_debug)
        Serial.printf("[DHT] pin %d: temp=%.1f humi=%.1f (%s)\n",
                      pin, temp, humi, is_dht22 ? "DHT22" : "DHT11");

    return (channel == DHT_CHAN_TEMP) ? temp : humi;
}
