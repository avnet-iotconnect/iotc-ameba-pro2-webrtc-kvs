/* SPDX-License-Identifier: MIT
 * Copyright (C) 2024 Avnet
 *
 * IoTConnect Discovery + Identity REST API client
 *
 * Performs the two-step HTTP REST flow that the IoTConnect Python Lite SDK
 * performs via DeviceRestApi.get_identity_data() before MQTT connection:
 *
 *   Step 1 — Discovery:
 *     GET https://awsdiscovery.iotconnect.io/api/sdk/cpid/{CPID}/lang/M_C/ver/2.0/env/{ENV}
 *     Response: { "d": { "bu": "<base_url>" } }
 *
 *   Step 2 — Identity:
 *     GET {base_url}/api/2.0/dsdk/uid/{DUID}
 *     Response (AWS): {
 *       "d": {
 *         "p": { "h": "<mqtt_host>", "id": "<client_id>",
 *                "topics": { "rpt": "...", "c2d": "..." } },
 *         "vs": { "url": "<kvs_creds_url>", "carn": "<channel_arn>" }
 *       }
 *     }
 *
 * After a successful call to IoTConnect_DoDiscovery() the following getters
 * return the discovered values, which remain valid until IoTConnect_Deinit().
 */

#ifndef IOTCONNECT_DISCOVERY_H
#define IOTCONNECT_DISCOVERY_H

#include <stdbool.h>

/**
 * @brief Perform the discovery → identity REST API flow.
 *
 * Constructs both URLs from IOTCONNECT_CPID, IOTCONNECT_ENV, and
 * IOTCONNECT_DUID defined in iotconnect_config.h.  Uses plain TLS
 * (no client certificate) since these endpoints are public REST APIs.
 *
 * @return 0 on success, non-zero on failure.
 */
int IoTConnect_DoDiscovery(void);

/** @return KVS credentials endpoint URL discovered from identity response,
 *          or NULL if discovery has not been performed or failed. */
const char *IoTConnect_GetDiscoveredKvsUrl(void);

/** @return MQTT host discovered from identity response, or NULL. */
const char *IoTConnect_GetDiscoveredMqttHost(void);

/** @return MQTT client ID from identity response (same as DUID for dedicated
 *          instances), or NULL. */
const char *IoTConnect_GetDiscoveredClientId(void);

/** @return AWS region parsed from the KVS channel ARN (d.vs.carn) in the
 *          identity response, e.g. "us-east-1", or NULL if not available. */
const char *IoTConnect_GetDiscoveredAwsRegion(void);

/** Free all memory allocated during discovery.  Called by IoTConnect_Deinit(). */
void IoTConnect_DiscoveryDeinit(void);

#endif /* IOTCONNECT_DISCOVERY_H */
