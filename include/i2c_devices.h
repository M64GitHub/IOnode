/**
 * @file i2c_devices.h
 * @brief I2C bus management and sensor/display drivers
 *
 * Provides I2C bus init/deinit with reference counting, bus scan,
 * and raw Wire.h drivers for common I2C sensors and OLED displays.
 * No external libraries — all register-level communication.
 */

#ifndef I2C_DEVICES_H
#define I2C_DEVICES_H

#include <Arduino.h>

/* Fixed I2C pins per chip variant (single bus) */
#if defined(CONFIG_IDF_TARGET_ESP32C6)
#define I2C_SDA  6
#define I2C_SCL  7
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define I2C_SDA  8
#define I2C_SCL  9
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define I2C_SDA  4
#define I2C_SCL  6
#else
#define I2C_SDA  21
#define I2C_SCL  22
#endif

/* Per-address reading cache (avoids redundant I2C reads for multi-channel sensors) */
#define I2C_CACHE_MAX     8
#define I2C_CACHE_TTL_MS  1000

struct I2cCache {
    uint8_t  addr;
    uint32_t last_read_ms;
    float    values[4];     /* up to 4 channels per sensor */
    uint8_t  num_values;
    bool     valid;
};

/*============================================================================
 * I2C Bus Management
 *============================================================================*/

/* Initialize I2C bus (reference counted — safe to call multiple times) */
void i2cInit();

/* Deinitialize I2C bus (decrements refcount, shuts down on last) */
void i2cDeinit();

/* Returns true if I2C bus is currently initialized */
bool i2cActive();

/* Scan I2C bus, fill addrs[] with found addresses. Returns count. */
int i2cScan(uint8_t *addrs, int max_addrs);

/* Check if a specific address responds */
bool i2cDetect(uint8_t addr);

/* Raw I2C read: write register address, then read len bytes */
bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len);

/* Raw I2C write: write register address + data bytes */
bool i2cWriteReg(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t len);

/* Attempt I2C bus recovery by toggling SCL 9 times */
void i2cRecover();

/*============================================================================
 * Sensor Drivers — return float via channel index
 *============================================================================*/

/* Get cached reading for an address/channel, or NAN if stale/missing */
float i2cCacheGet(uint8_t addr, uint8_t channel);

/* Invalidate cache for an address */
void i2cCacheInvalidate(uint8_t addr);

/**
 * Read I2C generic sensor: read register, combine bytes, apply scale.
 * @param addr    I2C slave address
 * @param reg     Register address to read
 * @param reg_len Number of bytes to read (1 or 2)
 * @param scale   Multiplier applied to raw value
 */
float i2cGenericRead(uint8_t addr, uint8_t reg, uint8_t reg_len, float scale);

/**
 * Read BME280 sensor (temperature/humidity/pressure).
 * Initializes calibration on first call per address.
 * Results cached — multiple channel reads within TTL don't re-read.
 * @param addr    I2C address (typically 0x76 or 0x77)
 * @param channel 0=temperature(C), 1=humidity(%), 2=pressure(hPa)
 */
float i2cBme280Read(uint8_t addr, uint8_t channel);

/**
 * Read BH1750 ambient light sensor.
 * @param addr I2C address (typically 0x23 or 0x5C)
 * @return Illuminance in lux
 */
float i2cBh1750Read(uint8_t addr);

/**
 * Read SHT31 temperature/humidity sensor.
 * Results cached per address.
 * @param addr    I2C address (typically 0x44 or 0x45)
 * @param channel 0=temperature(C), 1=humidity(%)
 */
float i2cSht31Read(uint8_t addr, uint8_t channel);

/**
 * Read ADS1115 16-bit ADC.
 * @param addr    I2C address (typically 0x48-0x4B)
 * @param channel ADC channel 0-3
 * @return Voltage in millivolts (default +/-4.096V range)
 */
float i2cAds1115Read(uint8_t addr, uint8_t channel);

/*============================================================================
 * SSD1306 OLED Display Driver
 *============================================================================*/

/**
 * Initialize SSD1306 display.
 * @param addr   I2C address (typically 0x3C or 0x3D)
 * @param height Display height: 64 or 32
 * @return true on success
 */
bool ssd1306Init(uint8_t addr, uint8_t height);

/**
 * Deinitialize SSD1306 display (clear screen, display off).
 * @param addr I2C address
 */
void ssd1306Deinit(uint8_t addr);

/**
 * Clear display buffer and screen.
 * @param addr I2C address
 */
void ssd1306Clear(uint8_t addr);

/**
 * Write text to display at given line.
 * @param addr I2C address
 * @param line Line number (0-based, max 7 for 64px, 3 for 32px)
 * @param text Text to display (max 21 chars per line)
 */
void ssd1306WriteText(uint8_t addr, uint8_t line, const char *text);

/**
 * Render a full template string to the display.
 * Template may contain {device_name} tokens and \n for line breaks.
 * @param addr      I2C address
 * @param tmpl      Template string
 * @param height    Display height (64 or 32)
 */
void ssd1306RenderTemplate(uint8_t addr, const char *tmpl, uint8_t height);

/**
 * Poll all SSD1306 displays — refresh templates with live sensor values.
 * Call from main loop every ~5 seconds.
 */
void displayPoll();

#endif /* I2C_DEVICES_H */
