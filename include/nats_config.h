/**
 * @file nats_config.h
 * @brief NATS remote configuration handler
 *
 * Subscribes to {device_name}.config.> and provides remote management of
 * devices, tags, heartbeat, events, and node configuration via NATS.
 */

#ifndef NATS_CONFIG_H
#define NATS_CONFIG_H

#include <nats_atoms.h>

/**
 * NATS message callback for config.> wildcard subscription.
 * Routes to sub-handlers: device.add/remove/list, tag, heartbeat, event, etc.
 */
void onNatsConfig(nats_client_t *client, const nats_msg_t *msg, void *userdata);

#endif /* NATS_CONFIG_H */
