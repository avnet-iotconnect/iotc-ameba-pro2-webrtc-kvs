/* SPDX-License-Identifier: MIT
 * Copyright (C) 2024 Avnet
 *
 * IoTConnect KVS Credential Fetcher
 *
 * Fetches temporary AWS credentials for Kinesis Video Streams from the
 * IoTConnect credential endpoint (p.vs.url from the identity response).
 * The request uses mTLS with the device certificate and key configured
 * in iotconnect_config.h / iotconnect_certs.c.
 */

#ifndef IOTCONNECT_KVS_CREDS_H
#define IOTCONNECT_KVS_CREDS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Buffer sizes — must be >= the signaling controller's ACCESS_KEY_MAX_LEN etc. */
#define IOTC_KVS_ACCESS_KEY_MAX_LEN    ( 128 )
#define IOTC_KVS_SECRET_KEY_MAX_LEN    ( 128 )
#define IOTC_KVS_SESSION_TOKEN_MAX_LEN ( 2048 )
#define IOTC_KVS_EXPIRATION_STR_LEN    ( 32 )  /* ISO 8601: "2026-01-30T22:54:09Z" */

/**
 * @brief Temporary AWS credentials obtained from IoTConnect.
 */
typedef struct {
    char     access_key_id    [ IOTC_KVS_ACCESS_KEY_MAX_LEN    + 1 ];
    char     secret_access_key[ IOTC_KVS_SECRET_KEY_MAX_LEN    + 1 ];
    char     session_token    [ IOTC_KVS_SESSION_TOKEN_MAX_LEN + 1 ];
    uint64_t expiration_epoch;  /* Unix epoch seconds */
    bool     valid;
} IoTConnectKvsCredentials_t;

/**
 * @brief Fetch KVS credentials from IoTConnect via mTLS HTTPS GET.
 *
 * Uses IOTCONNECT_DEVICE_CERT, IOTCONNECT_DEVICE_KEY, and IOTCONNECT_CA_CERT
 * from iotconnect_config.h / iotconnect_certs.c for the TLS mutual auth.
 *
 * @param credentials_url  The URL from the identity response (p.vs.url).
 * @param creds            Output: populated on success.
 * @return 0 on success, non-zero on error.
 */
int IoTConnect_FetchKvsCredentials( const char *credentials_url,
                                    IoTConnectKvsCredentials_t *creds );

/**
 * @brief Return true if the credentials are invalid or have expired.
 *
 * @param creds  Credentials to inspect.
 * @return true if expired or invalid, false if still valid.
 */
bool IoTConnect_KvsCredentialsExpired( const IoTConnectKvsCredentials_t *creds );

/**
 * @brief Seconds remaining until the credentials expire.
 *
 * @param creds  Credentials to inspect.
 * @return Positive seconds remaining, 0 or negative if already expired.
 */
int64_t IoTConnect_KvsCredentialSecsToExpiry( const IoTConnectKvsCredentials_t *creds );

#endif /* IOTCONNECT_KVS_CREDS_H */
