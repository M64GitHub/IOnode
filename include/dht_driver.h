/**
 * @file dht_driver.h
 * @brief DHT11/DHT22 single-wire temperature & humidity sensor driver
 *
 * Hand-rolled bit-bang driver with per-pin read cache (2s TTL).
 * Uses critical section (interrupts disabled) during the ~4ms read window.
 * Register two devices on the same GPIO pin to get both temp and humidity.
 */

#ifndef DHT_DRIVER_H
#define DHT_DRIVER_H

#include <Arduino.h>

#define DHT_CHAN_TEMP     0
#define DHT_CHAN_HUMI     1
#define DHT_CACHE_MAX     4
#define DHT_CACHE_TTL_MS  2000

/**
 * Read a DHT sensor value (temperature or humidity).
 *
 * @param pin       GPIO pin the DHT sensor is connected to
 * @param is_dht22  true for DHT22/AM2302, false for DHT11
 * @param channel   DHT_CHAN_TEMP (0) or DHT_CHAN_HUMI (1)
 * @return          Sensor value as float, or NAN on read error
 */
float dhtRead(uint8_t pin, bool is_dht22, uint8_t channel);

/**
 * Invalidate the cache entry for a given GPIO pin.
 * Call when removing a DHT device.
 *
 * @param pin  GPIO pin to invalidate
 */
void dhtCacheInvalidate(uint8_t pin);

#endif /* DHT_DRIVER_H */
