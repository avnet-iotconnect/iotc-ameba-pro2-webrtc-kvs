/* SPDX-License-Identifier: MIT
 * MQTT Client Implementation using Paho MQTT
 */

#include "iotconnect_mqtt.h"
#include "logging.h"
#include "MQTTClient.h"
#include "MQTTFreertos.h"
#include <string.h>

#define MQTT_COMMAND_TIMEOUT_MS 10000
#define MQTT_KEEP_ALIVE_INTERVAL_SEC 60
#define MQTT_SEND_BUF_SIZE 2048
#define MQTT_READ_BUF_SIZE 2048

static MQTTClient mqttClient;
static Network mqttNetwork;
static unsigned char sendBuf[MQTT_SEND_BUF_SIZE];
static unsigned char readBuf[MQTT_READ_BUF_SIZE];
static bool isConnected = false;
static MqttMessageCallback_t messageCallback = NULL;

static void messageArrived(MessageData* data)
{
    if (messageCallback && data && data->message) {
        MQTTMessage* message = data->message;
        MQTTString* topic = data->topicName;
        
        char topicStr[256];
        int topicLen = (topic->lenstring.len < sizeof(topicStr) - 1) ? 
                       topic->lenstring.len : sizeof(topicStr) - 1;
        memcpy(topicStr, topic->lenstring.data, topicLen);
        topicStr[topicLen] = '\0';
        
        messageCallback(topicStr, 
                       (const uint8_t*)message->payload, 
                       message->payloadlen);
    }
}

int32_t MqttClient_Init(void)
{
    LogInfo(("MqttClient_Init()"));
    
    NetworkInit(&mqttNetwork);
    
    MQTTClientInit(&mqttClient, &mqttNetwork, 
                   MQTT_COMMAND_TIMEOUT_MS,
                   sendBuf, sizeof(sendBuf),
                   readBuf, sizeof(readBuf));
    
    isConnected = false;
    messageCallback = NULL;
    
    return 0;
}

int32_t MqttClient_Connect(const char *host, uint16_t port,
                           const char *client_id,
                           const char *cert_pem,
                           const char *key_pem,
                           const char *ca_pem)
{
    int ret;
    
    if (!host || !client_id) {
        LogError(("Invalid parameters"));
        return -1;
    }
    
    LogInfo(("MqttClient_Connect()"));
    LogInfo(("  Host: %s:%u", host, port));
    LogInfo(("  Client ID: %s", client_id));
    
#if (MQTT_OVER_SSL)
    // Enable SSL
    mqttNetwork.use_ssl = 1;
    mqttNetwork.rootCA = (char*)ca_pem;
    mqttNetwork.clientCA = (char*)cert_pem;
    mqttNetwork.private_key = (char*)key_pem;
#endif
    
    ret = NetworkConnect(&mqttNetwork, (char*)host, port);
    
    if (ret != 0) {
        LogError(("NetworkConnect failed: %d", ret));
        return -2;
    }
    
    LogInfo(("Network connected"));
    
    MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;
    connectData.MQTTVersion = 4;
    connectData.clientID.cstring = (char*)client_id;
    connectData.keepAliveInterval = MQTT_KEEP_ALIVE_INTERVAL_SEC;
    connectData.cleansession = 1;
    
    ret = MQTTConnect(&mqttClient, &connectData);
    if (ret != 0) {
        LogError(("MQTTConnect failed: %d", ret));
        mqttNetwork.disconnect(&mqttNetwork);
        return -3;
    }
    
    LogInfo(("MQTT connected"));
    isConnected = true;
    
    return 0;
}

void MqttClient_Disconnect(void)
{
    if (!isConnected) return;
    
    LogInfo(("MqttClient_Disconnect()"));
    
    MQTTDisconnect(&mqttClient);
    mqttNetwork.disconnect(&mqttNetwork);
    
    isConnected = false;
}

bool MqttClient_IsConnected(void)
{
    return isConnected;
}

int32_t MqttClient_Subscribe(const char *topic, uint8_t qos)
{
    int ret;
    
    if (!isConnected || !topic) {
        LogError(("Not connected or invalid topic"));
        return -1;
    }
    
    LogInfo(("MqttClient_Subscribe(): %s", topic));
    
    ret = MQTTSubscribe(&mqttClient, topic, qos, messageArrived);
    if (ret != 0) {
        LogError(("MQTTSubscribe failed: %d", ret));
        return -2;
    }
    
    LogInfo(("Subscribed"));
    return 0;
}

int32_t MqttClient_Publish(const char *topic, const char *payload,
                           uint32_t payload_len, uint8_t qos)
{
    int ret;
    MQTTMessage message;
    
    if (!isConnected || !topic || !payload) {
        LogError(("Not connected or invalid parameters"));
        return -1;
    }
    
    LogDebug(("MqttClient_Publish(): %s", topic));
    
    memset(&message, 0, sizeof(message));
    message.qos = qos;
    message.retained = 0;
    message.payload = (void*)payload;
    message.payloadlen = payload_len;
    
    ret = MQTTPublish(&mqttClient, topic, &message);
    if (ret != 0) {
        LogError(("MQTTPublish failed: %d", ret));
        return -2;
    }
    
    LogDebug(("Published"));
    return 0;
}

void MqttClient_SetMessageCallback(MqttMessageCallback_t callback)
{
    messageCallback = callback;
}

void MqttClient_Deinit(void)
{
    MqttClient_Disconnect();
    messageCallback = NULL;
}

