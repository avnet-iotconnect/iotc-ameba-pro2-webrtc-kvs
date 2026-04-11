/* SPDX-License-Identifier: MIT
 * TODO: Implement using your MQTT client
 */
#ifndef IOTCONNECT_MQTT_H
#define IOTCONNECT_MQTT_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*MqttMessageCallback_t)(const char *topic, 
                                      const uint8_t *payload,
                                      uint32_t payload_len);

int32_t MqttClient_Init(void);
int32_t MqttClient_Connect(const char *host, uint16_t port, 
                           const char *client_id,
                           const char *cert_pem,
                           const char *key_pem,
                           const char *ca_pem);
void MqttClient_Disconnect(void);
bool MqttClient_IsConnected(void);
int32_t MqttClient_Subscribe(const char *topic, uint8_t qos);
int32_t MqttClient_Publish(const char *topic, const char *payload, 
                           uint32_t payload_len, uint8_t qos);
void MqttClient_SetMessageCallback(MqttMessageCallback_t callback);
void MqttClient_Deinit(void);

#endif
