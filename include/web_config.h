/**
 * @file web_config.h
 * @brief Web-based configuration portal for IOnode
 *
 * Provides a lightweight HTTP server on port 80 with REST API and
 * single-page UI for config, devices, pins, and status.
 * Uses ESPmDNS for <device-name>.local access.
 */

#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

/**
 * Initialize the web config server and mDNS responder.
 * Call from setup() after WiFi is connected.
 */
void webConfigSetup();

/**
 * Process pending HTTP requests.
 * Call from loop() on every iteration.
 */
void webConfigLoop();

#endif /* WEB_CONFIG_H */
