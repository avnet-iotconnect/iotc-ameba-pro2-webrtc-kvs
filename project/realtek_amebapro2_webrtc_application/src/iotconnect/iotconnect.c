/* SPDX-License-Identifier: MIT
 * IoTConnect Implementation (Simplified - Random Integer Only)
 */

#include "iotconnect.h"
#include "iotconnect_config.h"
#include "iotconnect_mqtt.h"
#include "iotconnect_kvs_creds.h"
#include "iotconnect_discovery.h"
#include "iotcl.h"
#include "iotcl_telemetry.h"
#include "iotcl_c2d.h"
#include "logging.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static TaskHandle_t telemetryTask = NULL;
static IotclClientConfig iotclConfig;
static bool isInitialized = false;
static bool isConnected = false;

// Simple time function - returns ISO 8601 timestamp
// NOTE: This is a placeholder - for production you'd sync with NTP
static char* getCurrentTimestamp(void)
{
    static char timestamp[32];
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    
    // Format: 2026-01-29T12:34:56.000Z
    snprintf(timestamp, sizeof(timestamp), 
             "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec);
    
    return timestamp;
}

static void onCommandCallback(IotclC2dEventData data)
{
    const char *cmd = iotcl_c2d_get_command(data);
    const char *ackId = iotcl_c2d_get_ack_id(data);
    
    LogInfo(("Command received: %s", cmd ? cmd : "NULL"));
    
    if (ackId) {
        char *ack_json = iotcl_c2d_create_cmd_ack_json(ackId, IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK, "OK");
        if (ack_json) {
            IotclMqttConfig *mqttConfig = iotcl_mqtt_get_config();
            if (mqttConfig && mqttConfig->pub_ack) {
                MqttClient_Publish(mqttConfig->pub_ack, ack_json, strlen(ack_json), IOTCONNECT_QOS);
            }
            iotcl_c2d_destroy_ack_json(ack_json);
        }
    }
}

static void onMqttMessage(const char *topic, const uint8_t *payload, uint32_t len)
{
    LogDebug(("MQTT message on %s", topic));
    iotcl_c2d_process_event_with_length(payload, len);
}

static int32_t sendTelemetry(void)
{
    int randomInt = rand() % 100;
    
    IotclMessageHandle msg = iotcl_telemetry_create();
    if (!msg) {
        LogError(("Failed to create telemetry message"));
        return -1;
    }
    
    // Add data set with timestamp
    const char *timestamp = getCurrentTimestamp();
    int ret = iotcl_telemetry_add_new_data_set(msg, timestamp);
    if (ret != 0) {
        LogError(("Failed to add data set"));
        iotcl_telemetry_destroy(msg);
        return -1;
    }
    
    // Set data
    iotcl_telemetry_set_number(msg, "random", (double)randomInt);
    
    char *json = iotcl_telemetry_create_serialized_string(msg, false);
    int32_t result = -2;
    
    if (json) {
        // Log the actual JSON being sent
        LogInfo(("Telemetry JSON: %s", json));
        
        IotclMqttConfig *mqttConfig = iotcl_mqtt_get_config();
        if (mqttConfig && mqttConfig->pub_rpt) {
            LogInfo(("Publishing to: %s", mqttConfig->pub_rpt));
            result = MqttClient_Publish(mqttConfig->pub_rpt, json, strlen(json), IOTCONNECT_QOS);
        } else {
            LogError(("No publish topic configured"));
        }
        iotcl_telemetry_destroy_serialized_string(json);
    } else {
        LogError(("Failed to serialize telemetry"));
    }
    
    iotcl_telemetry_destroy(msg);
    
    if (result == 0) {
        LogInfo(("Sent: random=%d", randomInt));
    } else {
        LogError(("Publish failed: %ld", (long)result));
    }
    
    return result;
}

static void telemetryTask_func(void *param)
{
    (void)param;
    TickType_t last = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(IOTCONNECT_TELEMETRY_INTERVAL_MS);
    
    LogInfo(("Telemetry task running"));
    
    while (1) {
        if (isConnected && MqttClient_IsConnected()) {
            if ((xTaskGetTickCount() - last) >= interval) {
                sendTelemetry();
                last = xTaskGetTickCount();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int32_t IoTConnect_Init(void)
{
    if (isInitialized) return 0;

    LogInfo(("IoTConnect Init"));
    srand(xTaskGetTickCount());

    /* Step 1: Initialise iotc-c-lib in CUSTOM mode.
     * Memory allocation is provided via pvPortMalloc/vPortFree.
     * MQTT host, client ID, and topics are populated by the discovery flow
     * below — not by hard-coded config values. */
    iotcl_configure_dynamic_memory(pvPortMalloc, vPortFree);

    iotcl_init_client_config(&iotclConfig);
    iotclConfig.device.instance_type = IOTCL_DCT_CUSTOM;
    iotclConfig.events.cmd_cb = onCommandCallback;

    int ret = iotcl_init(&iotclConfig);
    if (ret != IOTCL_SUCCESS) {
        LogError(("iotcl_init failed: %ld", (long)ret));
        return ret;
    }

    /* Step 2: Discovery + Identity REST API flow.
     * Calls iotcl_dra_discovery_* / iotcl_dra_identity_configure_library_mqtt()
     * which populates iotcl_mqtt_get_config() with host, client_id, topics,
     * KVS credentials URL, and WebRTC channel ARN. */
    if( IoTConnect_DoDiscovery() != 0 )
    {
        LogError( ( "IoTConnect discovery/identity failed" ) );
        iotcl_deinit();
        return -1;
    }
    
    IotclMqttConfig *mqttConfig = iotcl_mqtt_get_config();
    if (mqttConfig) {
        LogInfo(("  Pub: %s", mqttConfig->pub_rpt ? mqttConfig->pub_rpt : "NULL"));
        LogInfo(("  Sub: %s", mqttConfig->sub_c2d ? mqttConfig->sub_c2d : "NULL"));
    }
    
    ret = MqttClient_Init();
    if (ret != 0) {
        LogError(("MqttClient_Init failed: %ld", (long)ret));
        iotcl_deinit();
        return ret;
    }
    
    MqttClient_SetMessageCallback(onMqttMessage);
    isInitialized = true;
    return 0;
}

int32_t IoTConnect_Start(void)
{
    if (!isInitialized || isConnected) return -1;
    
    LogInfo(("IoTConnect Start"));

    /* Use the MQTT host discovered via the identity REST API. */
    const char *endpoint = IoTConnect_GetDiscoveredMqttHost();
    if( endpoint == NULL || endpoint[ 0 ] == '\0' )
    {
        LogError( ( "No MQTT host from discovery — was IoTConnect_Init() called?" ) );
        return -1;
    }
    
    /* Use the client ID from the identity response — this is the authoritative
     * value from the IoTConnect backend.  For DCT_AWS_DEDICATED it matches the
     * iotcl-generated ID, but using the discovered value is more correct. */
    const char *client_id = IoTConnect_GetDiscoveredClientId();
    if( client_id == NULL || client_id[ 0 ] == '\0' )
    {
        LogError( ( "No client ID from discovery — was IoTConnect_Init() called?" ) );
        return -1;
    }

    IotclMqttConfig *mqttConfig = iotcl_mqtt_get_config();
    if (!mqttConfig) {
        LogError(("Invalid MQTT config"));
        return -1;
    }

    int ret = MqttClient_Connect(endpoint, IOTCONNECT_MQTT_PORT,
        client_id,
        IOTCONNECT_DEVICE_CERT,
        IOTCONNECT_DEVICE_KEY,
        IOTCONNECT_CA_CERT);
    
    if (ret != 0) {
        LogError(("MQTT connect failed: %ld", (long)ret));
        return ret;
    }
    
    if (mqttConfig->sub_c2d) {
        ret = MqttClient_Subscribe(mqttConfig->sub_c2d, IOTCONNECT_QOS);
        if (ret != 0) {
            LogError(("Subscribe failed: %ld", (long)ret));
            MqttClient_Disconnect();
            return ret;
        }
    }
    
    if (xTaskCreate(telemetryTask_func, "IoTC", 
        IOTCONNECT_TASK_STACK_SIZE / sizeof(StackType_t),
        NULL, IOTCONNECT_TASK_PRIORITY, &telemetryTask) != pdPASS) {
        LogError(("Task create failed"));
        MqttClient_Disconnect();
        return -2;
    }
    
    isConnected = true;
    LogInfo(("IoTConnect started"));
    return 0;
}

void IoTConnect_Stop(void)
{
    if (!isConnected) return;
    if (telemetryTask) {
        vTaskDelete(telemetryTask);
        telemetryTask = NULL;
    }
    MqttClient_Disconnect();
    isConnected = false;
}

void IoTConnect_Deinit(void)
{
    if (!isInitialized) return;
    IoTConnect_Stop();
    MqttClient_Deinit();
    iotcl_deinit();
    IoTConnect_DiscoveryDeinit();
    isInitialized = false;
}

bool IoTConnect_IsConnected(void)
{
    return isConnected && MqttClient_IsConnected();
}

int IoTConnect_KvsCredentialRefreshCallback(
    char     *pAccessKeyId,      size_t maxAccessKeyIdLen,     size_t *pAccessKeyIdLen,
    char     *pSecretAccessKey,  size_t maxSecretAccessKeyLen, size_t *pSecretAccessKeyLen,
    char     *pSessionToken,     size_t maxSessionTokenLen,    size_t *pSessionTokenLen,
    uint64_t *pExpirationSeconds )
{
    const char *url = IoTConnect_GetDiscoveredKvsUrl();

    if( url == NULL || url[ 0 ] == '\0' )
    {
        LogError( ( "KVS credentials URL not available — discovery may not have succeeded or"
                    " KVS streaming is not enabled for this device in IoTConnect" ) );
        return -1;
    }

    IoTConnectKvsCredentials_t creds;
    int ret = IoTConnect_FetchKvsCredentials( url, &creds );
    if( ret != 0 )
    {
        LogError( ( "Failed to fetch KVS credentials from IoTConnect" ) );
        return ret;
    }

    size_t akLen  = strlen( creds.access_key_id );
    size_t skLen  = strlen( creds.secret_access_key );
    size_t tokLen = strlen( creds.session_token );

    if( akLen > maxAccessKeyIdLen || skLen > maxSecretAccessKeyLen || tokLen > maxSessionTokenLen )
    {
        LogError( ( "KVS credential buffer too small (ak=%zu sk=%zu tok=%zu)", akLen, skLen, tokLen ) );
        return -1;
    }

    memcpy( pAccessKeyId,     creds.access_key_id,     akLen );
    memcpy( pSecretAccessKey, creds.secret_access_key, skLen );
    memcpy( pSessionToken,    creds.session_token,      tokLen );

    *pAccessKeyIdLen     = akLen;
    *pSecretAccessKeyLen = skLen;
    *pSessionTokenLen    = tokLen;
    *pExpirationSeconds  = creds.expiration_epoch;

    LogInfo( ( "KVS credentials refreshed via IoTConnect. Expiry epoch: %llu",
               ( unsigned long long ) creds.expiration_epoch ) );
    return 0;
}
