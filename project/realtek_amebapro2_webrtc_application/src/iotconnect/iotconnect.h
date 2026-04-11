/* SPDX-License-Identifier: MIT */
#ifndef IOTCONNECT_H
#define IOTCONNECT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

int32_t IoTConnect_Init(void);
int32_t IoTConnect_Start(void);
void IoTConnect_Stop(void);
void IoTConnect_Deinit(void);
bool IoTConnect_IsConnected(void);

/**
 * @brief Credential refresh callback for the KVS signaling controller.
 *
 * Fetches fresh temporary AWS credentials from the IoTConnect credential
 * endpoint and writes them into the caller-supplied buffers.
 * Matches the CredentialRefreshFn_t signature in signaling_controller.h.
 *
 * @return 0 on success, non-zero on failure.
 */
int IoTConnect_KvsCredentialRefreshCallback(
    char     *pAccessKeyId,      size_t maxAccessKeyIdLen,     size_t *pAccessKeyIdLen,
    char     *pSecretAccessKey,  size_t maxSecretAccessKeyLen, size_t *pSecretAccessKeyLen,
    char     *pSessionToken,     size_t maxSessionTokenLen,    size_t *pSessionTokenLen,
    uint64_t *pExpirationSeconds );

#endif
