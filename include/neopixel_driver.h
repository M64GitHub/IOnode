/**
 * @file neopixel_driver.h
 * @brief WS2812/NeoPixel addressable LED strip driver
 *
 * Manages up to NEOPIXEL_MAX_STRIPS strips via Adafruit NeoPixel library.
 * Strips are indexed by device slot in g_devices[].
 */

#ifndef NEOPIXEL_DRIVER_H
#define NEOPIXEL_DRIVER_H

#include <Arduino.h>

#define NEOPIXEL_MAX_STRIPS 4

/* All functions take devSlot = index into g_devices[] array (0..MAX_DEVICES-1).
   The driver internally maps device slots to strip slots (0..MAX_STRIPS-1). */

/* Initialize a NeoPixel strip for a given device slot.
   colorOrder: NEO_ORDER_GRB (0) through NEO_ORDER_GRBW (7) */
void neopixelInit(int devSlot, uint8_t pin, uint16_t numPixels, uint8_t colorOrder = 0);

/* Deinitialize and free a NeoPixel strip */
void neopixelDeinit(int devSlot);

/* Fill all pixels with a color (0xRRGGBB) */
void neopixelFill(int devSlot, uint32_t color);

/* Set a single pixel to a color */
void neopixelSetPixel(int devSlot, uint16_t pixel, uint32_t color);

/* Set brightness (0-255) */
void neopixelSetBrightness(int devSlot, uint8_t brightness);

/* Turn off all pixels */
void neopixelClear(int devSlot);

/* Get current brightness */
uint8_t neopixelGetBrightness(int devSlot);

/* Get pixel count */
uint16_t neopixelGetCount(int devSlot);

/* Get last fill color */
uint32_t neopixelGetColor(int devSlot);

/* Handle a JSON command payload, returns true if handled */
bool neopixelHandleJson(int devSlot, const char *json, char *reply, int reply_len);

#endif /* NEOPIXEL_DRIVER_H */
