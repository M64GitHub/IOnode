/**
 * @file i2c_devices.cpp
 * @brief I2C bus management and raw Wire.h sensor drivers
 *
 * No external libraries — all register-level I2C communication.
 * Includes drivers for BME280, BH1750, SHT31, ADS1115, and generic register reads.
 */

#include "i2c_devices.h"
#include <Wire.h>

extern bool g_debug;

/*============================================================================
 * I2C Bus Management (reference counted)
 *============================================================================*/

static int  g_i2c_ref_count = 0;
static bool g_i2c_initialized = false;

void i2cInit() {
    if (g_i2c_initialized) {
        g_i2c_ref_count++;
        return;
    }
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setTimeOut(50);
    g_i2c_initialized = true;
    g_i2c_ref_count = 1;
    Serial.printf("I2C: initialized (SDA=%d SCL=%d)\n", I2C_SDA, I2C_SCL);
}

void i2cDeinit() {
    if (!g_i2c_initialized) return;
    g_i2c_ref_count--;
    if (g_i2c_ref_count <= 0) {
        Wire.end();
        g_i2c_initialized = false;
        g_i2c_ref_count = 0;
        Serial.println("I2C: deinitialized");
    }
}

bool i2cActive() {
    return g_i2c_initialized;
}

int i2cScan(uint8_t *addrs, int max_addrs) {
    if (!g_i2c_initialized) return 0;
    int count = 0;
    for (uint8_t a = 1; a < 127 && count < max_addrs; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            addrs[count++] = a;
        }
    }
    return count;
}

bool i2cDetect(uint8_t addr) {
    if (!g_i2c_initialized) return false;
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len) {
    if (!g_i2c_initialized) return false;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = Wire.read();
    }
    return true;
}

bool i2cWriteReg(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t len) {
    if (!g_i2c_initialized) return false;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    for (uint8_t i = 0; i < len; i++) {
        Wire.write(data[i]);
    }
    return Wire.endTransmission() == 0;
}

void i2cRecover() {
    /* Toggle SCL 9 times to unstick any slave holding SDA low */
    Wire.end();
    pinMode(I2C_SCL, OUTPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL, HIGH);
        delayMicroseconds(5);
    }
    /* Re-initialize */
    if (g_i2c_initialized) {
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setTimeOut(50);
    }
    Serial.println("I2C: bus recovery attempted");
}

/*============================================================================
 * Per-Address Reading Cache
 *============================================================================*/

static I2cCache g_i2c_cache[I2C_CACHE_MAX];

float i2cCacheGet(uint8_t addr, uint8_t channel) {
    uint32_t now = millis();
    for (int i = 0; i < I2C_CACHE_MAX; i++) {
        if (g_i2c_cache[i].valid && g_i2c_cache[i].addr == addr &&
            (now - g_i2c_cache[i].last_read_ms) < I2C_CACHE_TTL_MS &&
            channel < g_i2c_cache[i].num_values) {
            return g_i2c_cache[i].values[channel];
        }
    }
    return NAN;
}

static void i2cCacheSet(uint8_t addr, const float *values, uint8_t num) {
    /* Find existing entry or free slot */
    int slot = -1;
    for (int i = 0; i < I2C_CACHE_MAX; i++) {
        if (g_i2c_cache[i].addr == addr) { slot = i; break; }
        if (!g_i2c_cache[i].valid && slot < 0) slot = i;
    }
    if (slot < 0) {
        /* Evict oldest */
        uint32_t oldest = UINT32_MAX;
        for (int i = 0; i < I2C_CACHE_MAX; i++) {
            if (g_i2c_cache[i].last_read_ms < oldest) {
                oldest = g_i2c_cache[i].last_read_ms;
                slot = i;
            }
        }
    }
    if (slot < 0) slot = 0;

    g_i2c_cache[slot].addr = addr;
    g_i2c_cache[slot].last_read_ms = millis();
    g_i2c_cache[slot].num_values = num > 4 ? 4 : num;
    g_i2c_cache[slot].valid = true;
    for (int i = 0; i < g_i2c_cache[slot].num_values; i++) {
        g_i2c_cache[slot].values[i] = values[i];
    }
}

void i2cCacheInvalidate(uint8_t addr) {
    for (int i = 0; i < I2C_CACHE_MAX; i++) {
        if (g_i2c_cache[i].addr == addr) {
            g_i2c_cache[i].valid = false;
        }
    }
}

/*============================================================================
 * i2c_generic — Universal register-read sensor
 *============================================================================*/

float i2cGenericRead(uint8_t addr, uint8_t reg, uint8_t reg_len, float scale) {
    if (!g_i2c_initialized) return NAN;
    if (reg_len < 1) reg_len = 1;
    if (reg_len > 2) reg_len = 2;

    uint8_t buf[2] = {0};
    if (!i2cReadReg(addr, reg, buf, reg_len)) return NAN;

    uint16_t raw = buf[0];
    if (reg_len == 2) raw = (raw << 8) | buf[1];

    return (float)raw * scale;
}

/*============================================================================
 * BME280 — Temperature / Humidity / Pressure
 *
 * Bosch BME280 datasheet compensation algorithm (32-bit integer version).
 * Calibration data cached per address.
 *============================================================================*/

#define BME280_CALIB_MAX 2  /* max 2 BME280s on same bus */

struct Bme280Calib {
    uint8_t  addr;
    bool     valid;
    /* Temperature */
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    /* Pressure */
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    /* Humidity */
    uint8_t  dig_H1, dig_H3;
    int16_t  dig_H2, dig_H4, dig_H5;
    int8_t   dig_H6;
};

static Bme280Calib g_bme_calib[BME280_CALIB_MAX];

static Bme280Calib *bme280GetCalib(uint8_t addr) {
    for (int i = 0; i < BME280_CALIB_MAX; i++) {
        if (g_bme_calib[i].valid && g_bme_calib[i].addr == addr)
            return &g_bme_calib[i];
    }
    return nullptr;
}

static bool bme280LoadCalib(uint8_t addr) {
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < BME280_CALIB_MAX; i++) {
        if (!g_bme_calib[i].valid) { slot = i; break; }
    }
    if (slot < 0) return false;

    Bme280Calib *c = &g_bme_calib[slot];

    /* Read calibration bank 1: 0x88..0xA1 (26 bytes) */
    uint8_t buf[26];
    if (!i2cReadReg(addr, 0x88, buf, 26)) return false;

    c->dig_T1 = (uint16_t)(buf[1] << 8 | buf[0]);
    c->dig_T2 = (int16_t)(buf[3] << 8 | buf[2]);
    c->dig_T3 = (int16_t)(buf[5] << 8 | buf[4]);
    c->dig_P1 = (uint16_t)(buf[7] << 8 | buf[6]);
    c->dig_P2 = (int16_t)(buf[9] << 8 | buf[8]);
    c->dig_P3 = (int16_t)(buf[11] << 8 | buf[10]);
    c->dig_P4 = (int16_t)(buf[13] << 8 | buf[12]);
    c->dig_P5 = (int16_t)(buf[15] << 8 | buf[14]);
    c->dig_P6 = (int16_t)(buf[17] << 8 | buf[16]);
    c->dig_P7 = (int16_t)(buf[19] << 8 | buf[18]);
    c->dig_P8 = (int16_t)(buf[21] << 8 | buf[20]);
    c->dig_P9 = (int16_t)(buf[23] << 8 | buf[22]);
    c->dig_H1 = buf[25]; /* 0xA1 */

    /* Read calibration bank 2: 0xE1..0xE7 (7 bytes) */
    uint8_t buf2[7];
    if (!i2cReadReg(addr, 0xE1, buf2, 7)) return false;

    c->dig_H2 = (int16_t)(buf2[1] << 8 | buf2[0]);
    c->dig_H3 = buf2[2];
    c->dig_H4 = (int16_t)((buf2[3] << 4) | (buf2[4] & 0x0F));
    c->dig_H5 = (int16_t)((buf2[5] << 4) | ((buf2[4] >> 4) & 0x0F));
    c->dig_H6 = (int8_t)buf2[6];

    c->addr = addr;
    c->valid = true;

    if (g_debug) Serial.printf("BME280: calibration loaded for 0x%02X\n", addr);
    return true;
}

static bool bme280Init(uint8_t addr) {
    /* Check chip ID (should be 0x60 for BME280) */
    uint8_t id = 0;
    if (!i2cReadReg(addr, 0xD0, &id, 1) || id != 0x60) {
        Serial.printf("BME280: wrong chip ID 0x%02X at 0x%02X\n", id, addr);
        return false;
    }

    /* Reset */
    uint8_t reset = 0xB6;
    i2cWriteReg(addr, 0xE0, &reset, 1);
    delay(10);

    /* Load calibration */
    if (!bme280LoadCalib(addr)) return false;

    /* Set humidity oversampling (must be set before ctrl_meas) */
    uint8_t ctrl_hum = 0x01;  /* 1x oversampling */
    i2cWriteReg(addr, 0xF2, &ctrl_hum, 1);

    /* Set temp + pressure oversampling + forced mode */
    uint8_t ctrl_meas = 0x27; /* temp 1x, press 1x, normal mode */
    i2cWriteReg(addr, 0xF4, &ctrl_meas, 1);

    /* Config: standby 1000ms, filter off */
    uint8_t config = 0xA0;    /* t_sb=1000ms, filter=off, spi3w_en=off */
    i2cWriteReg(addr, 0xF5, &config, 1);

    Serial.printf("BME280: initialized at 0x%02X\n", addr);
    return true;
}

static bool bme280DoRead(uint8_t addr, float *temp, float *humi, float *pres) {
    Bme280Calib *c = bme280GetCalib(addr);
    if (!c) {
        if (!bme280Init(addr)) return false;
        c = bme280GetCalib(addr);
        if (!c) return false;
    }

    /* Read raw data: 0xF7..0xFE (8 bytes: press[3] + temp[3] + hum[2]) */
    uint8_t buf[8];
    if (!i2cReadReg(addr, 0xF7, buf, 8)) return false;

    int32_t adc_P = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | (buf[2] >> 4);
    int32_t adc_T = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | (buf[5] >> 4);
    int32_t adc_H = ((int32_t)buf[6] << 8) | buf[7];

    /* Temperature compensation */
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)c->dig_T1 << 1))) * ((int32_t)c->dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)c->dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)c->dig_T1))) >> 12) *
                    ((int32_t)c->dig_T3)) >> 14;
    int32_t t_fine = var1 + var2;
    *temp = (float)((t_fine * 5 + 128) >> 8) / 100.0f;

    /* Pressure compensation */
    int64_t p_var1 = (int64_t)t_fine - 128000;
    int64_t p_var2 = p_var1 * p_var1 * (int64_t)c->dig_P6;
    p_var2 = p_var2 + ((p_var1 * (int64_t)c->dig_P5) << 17);
    p_var2 = p_var2 + (((int64_t)c->dig_P4) << 35);
    p_var1 = ((p_var1 * p_var1 * (int64_t)c->dig_P3) >> 8) +
             ((p_var1 * (int64_t)c->dig_P2) << 12);
    p_var1 = (((((int64_t)1) << 47) + p_var1)) * ((int64_t)c->dig_P1) >> 33;
    if (p_var1 == 0) {
        *pres = 0.0f;
    } else {
        int64_t p = 1048576 - adc_P;
        p = (((p << 31) - p_var2) * 3125) / p_var1;
        p_var1 = (((int64_t)c->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
        p_var2 = (((int64_t)c->dig_P8) * p) >> 19;
        p = ((p + p_var1 + p_var2) >> 8) + (((int64_t)c->dig_P7) << 4);
        *pres = (float)p / 25600.0f;  /* Pa -> hPa */
    }

    /* Humidity compensation */
    int32_t v_x1_u32r = t_fine - (int32_t)76800;
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)c->dig_H4) << 20) -
                     (((int32_t)c->dig_H5) * v_x1_u32r)) +
                    (int32_t)16384) >> 15) *
                 (((((((v_x1_u32r * ((int32_t)c->dig_H6)) >> 10) *
                      (((v_x1_u32r * ((int32_t)c->dig_H3)) >> 11) +
                       (int32_t)32768)) >> 10) +
                    (int32_t)2097152) * ((int32_t)c->dig_H2) + 8192) >> 14));
    v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                                ((int32_t)c->dig_H1)) >> 4);
    v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
    v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;
    *humi = (float)(v_x1_u32r >> 12) / 1024.0f;

    return true;
}

float i2cBme280Read(uint8_t addr, uint8_t channel) {
    if (!g_i2c_initialized || channel > 2) return NAN;

    /* Check cache first */
    float cached = i2cCacheGet(addr, channel);
    if (!isnan(cached)) return cached;

    /* Full read + cache all 3 channels */
    float temp, humi, pres;
    if (!bme280DoRead(addr, &temp, &humi, &pres)) return NAN;

    float values[3] = { temp, humi, pres };
    i2cCacheSet(addr, values, 3);

    return values[channel];
}

/*============================================================================
 * BH1750 — Ambient Light Sensor
 *============================================================================*/

static bool g_bh1750_inited[2] = {false, false};
static uint8_t g_bh1750_addrs[2] = {0, 0};

static int bh1750Slot(uint8_t addr) {
    for (int i = 0; i < 2; i++) {
        if (g_bh1750_addrs[i] == addr) return i;
    }
    for (int i = 0; i < 2; i++) {
        if (g_bh1750_addrs[i] == 0) { g_bh1750_addrs[i] = addr; return i; }
    }
    return -1;
}

static bool bh1750Init(uint8_t addr) {
    int slot = bh1750Slot(addr);
    if (slot < 0) return false;
    if (g_bh1750_inited[slot]) return true;

    /* Power on */
    Wire.beginTransmission(addr);
    Wire.write(0x01);
    if (Wire.endTransmission() != 0) return false;

    /* Continuous high-resolution mode (1 lux resolution, 120ms) */
    Wire.beginTransmission(addr);
    Wire.write(0x10);
    if (Wire.endTransmission() != 0) return false;

    g_bh1750_inited[slot] = true;
    Serial.printf("BH1750: initialized at 0x%02X\n", addr);
    return true;
}

float i2cBh1750Read(uint8_t addr) {
    if (!g_i2c_initialized) return NAN;

    if (!bh1750Init(addr)) return NAN;

    if (Wire.requestFrom(addr, (uint8_t)2) != 2) return NAN;
    uint16_t raw = (Wire.read() << 8) | Wire.read();

    return (float)raw / 1.2f;
}

/*============================================================================
 * SHT31 — Temperature / Humidity
 *============================================================================*/

static bool sht31DoRead(uint8_t addr, float *temp, float *humi) {
    /* Send single-shot measurement command: high repeatability, clock stretching */
    Wire.beginTransmission(addr);
    Wire.write(0x2C);
    Wire.write(0x06);
    if (Wire.endTransmission() != 0) return false;

    delay(16);  /* Wait for measurement (~15ms high repeatability) */

    /* Read 6 bytes: temp_msb, temp_lsb, temp_crc, humi_msb, humi_lsb, humi_crc */
    if (Wire.requestFrom(addr, (uint8_t)6) != 6) return false;

    uint8_t buf[6];
    for (int i = 0; i < 6; i++) buf[i] = Wire.read();

    uint16_t raw_t = (buf[0] << 8) | buf[1];
    uint16_t raw_h = (buf[3] << 8) | buf[4];

    *temp = -45.0f + 175.0f * (float)raw_t / 65535.0f;
    *humi = 100.0f * (float)raw_h / 65535.0f;

    return true;
}

float i2cSht31Read(uint8_t addr, uint8_t channel) {
    if (!g_i2c_initialized || channel > 1) return NAN;

    /* Check cache */
    float cached = i2cCacheGet(addr, channel);
    if (!isnan(cached)) return cached;

    float temp, humi;
    if (!sht31DoRead(addr, &temp, &humi)) return NAN;

    float values[2] = { temp, humi };
    i2cCacheSet(addr, values, 2);

    return values[channel];
}

/*============================================================================
 * ADS1115 — 16-bit ADC (4 single-ended channels)
 *============================================================================*/

float i2cAds1115Read(uint8_t addr, uint8_t channel) {
    if (!g_i2c_initialized || channel > 3) return NAN;

    /*
     * Config register (0x01): 16 bits
     * Bit 15: OS = 1 (start single conversion)
     * Bits 14-12: MUX (channel select, single-ended)
     *   0b100=AIN0, 0b101=AIN1, 0b110=AIN2, 0b111=AIN3
     * Bits 11-9: PGA = 001 (+/-4.096V)
     * Bit 8: MODE = 1 (single-shot)
     * Bits 7-5: DR = 100 (128 SPS)
     * Bits 4-0: defaults
     */
    uint16_t mux = (0x04 + channel) & 0x07;
    uint16_t config = 0x8000 |      /* OS: start conversion */
                      (mux << 12) |  /* MUX: channel */
                      0x0200 |       /* PGA: +/-4.096V */
                      0x0100 |       /* MODE: single-shot */
                      0x0080;        /* DR: 128 SPS */

    uint8_t cfg_bytes[2] = { (uint8_t)(config >> 8), (uint8_t)(config & 0xFF) };
    if (!i2cWriteReg(addr, 0x01, cfg_bytes, 2)) return NAN;

    /* Wait for conversion (128 SPS = ~8ms, add margin) */
    delay(10);

    /* Read conversion register (0x00): 2 bytes, big-endian signed */
    uint8_t result[2];
    if (!i2cReadReg(addr, 0x00, result, 2)) return NAN;

    int16_t raw = (int16_t)((result[0] << 8) | result[1]);

    /* Convert to millivolts: +/-4.096V range, 16-bit signed */
    return (float)raw * 0.125f;  /* 1 LSB = 0.125mV */
}
