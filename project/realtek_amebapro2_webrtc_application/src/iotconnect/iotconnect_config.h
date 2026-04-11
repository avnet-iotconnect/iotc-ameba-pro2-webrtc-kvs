/* SPDX-License-Identifier: MIT*/

#ifndef IOTCONNECT_CONFIG_H
#define IOTCONNECT_CONFIG_H

/* ---- User-configurable values -------------------------------------------- */

/* IoTConnect Company ID (CPID) — found in the IoTConnect portal under
 * Settings → Key Vault. */
#define IOTCONNECT_CPID "97FF86E8728645E9B89F7B07977E4B15"

/* IoTConnect environment name (e.g. "poc", "prod"). */
#define IOTCONNECT_ENV "poc"

/* Device Unique ID — must match the device name in the IoTConnect portal.
 * Also used as the KVS signaling channel name. */
#define IOTCONNECT_DUID "000ameba"

/* ---- Derived / fixed values (do not change) ------------------------------ */

/* MQTT host and KVS credentials URL are discovered automatically at runtime
 * via the IoTConnect Discovery + Identity REST API (see iotconnect_discovery.c).
 * No manual endpoint configuration is needed. */

#define IOTCONNECT_INSTANCE_TYPE IOTCL_DCT_AWS_DEDICATED
#define IOTCONNECT_QOS 1
#define IOTCONNECT_MQTT_PORT 8883
#define IOTCONNECT_TELEMETRY_INTERVAL_MS 5000

extern const char IOTCONNECT_DEVICE_CERT[];
extern const char IOTCONNECT_DEVICE_KEY[];
extern const char IOTCONNECT_CA_CERT[];
extern const char IOTCONNECT_DISCOVERY_CA_CERT[];

#define IOTCONNECT_TASK_PRIORITY (tskIDLE_PRIORITY + 3)
#define IOTCONNECT_TASK_STACK_SIZE 4096

#endif
