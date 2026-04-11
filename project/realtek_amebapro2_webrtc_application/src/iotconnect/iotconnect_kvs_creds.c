/* SPDX-License-Identifier: MIT
 * Copyright (C) 2024 Avnet
 *
 * IoTConnect KVS Credential Fetcher Implementation
 *
 * Makes a mutual-TLS HTTPS GET to the IoTConnect credential endpoint.
 * JSON parsing is delegated to iotcl_dra_json_credentials_parse().
 */

#include "iotconnect_kvs_creds.h"
#include "iotconnect_config.h"

#include "iotcl.h"
#include "iotcl_dra_credentials.h"

#include "logging.h"
#include "FreeRTOS.h"

#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_RESPONSE_SIZE ( 8 * 1024 )

/* ---- helpers ------------------------------------------------------------ */

static int parse_https_url( const char *url,
                             char *host, size_t host_size,
                             char *path, size_t path_size )
{
    if( strncmp( url, "https://", 8 ) != 0 )
    {
        LogError( ( "KVS creds URL must start with https://" ) );
        return -1;
    }

    const char *start = url + 8;
    const char *slash = strchr( start, '/' );
    size_t host_len   = slash ? ( size_t )( slash - start ) : strlen( start );

    if( host_len >= host_size )
    {
        LogError( ( "KVS creds URL host too long" ) );
        return -1;
    }

    strncpy( host, start, host_len );
    host[ host_len ] = '\0';

    if( slash )
    {
        strncpy( path, slash, path_size - 1 );
        path[ path_size - 1 ] = '\0';
    }
    else
    {
        strncpy( path, "/", path_size - 1 );
    }

    return 0;
}

/* ---- public API --------------------------------------------------------- */

int IoTConnect_FetchKvsCredentials( const char *credentials_url,
                                    IoTConnectKvsCredentials_t *creds )
{
    char  host[ 256 ];
    char  path[ 512 ];
    char *buf = NULL;
    int   ret = -1;

    mbedtls_net_context      server_fd;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_x509_crt         cacert;
    mbedtls_x509_crt         clicert;
    mbedtls_pk_context       pkey;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context  entropy;

    if( !credentials_url || !creds )
    {
        LogError( ( "IoTConnect_FetchKvsCredentials: NULL parameter" ) );
        return -1;
    }

    if( parse_https_url( credentials_url, host, sizeof( host ),
                         path, sizeof( path ) ) != 0 )
    {
        return -1;
    }

    LogInfo( ( "Fetching KVS credentials from %s%s", host, path ) );

    buf = ( char * ) pvPortMalloc( MAX_RESPONSE_SIZE );
    if( !buf )
    {
        LogError( ( "Failed to allocate KVS response buffer" ) );
        return -1;
    }

    mbedtls_net_init( &server_fd );
    mbedtls_ssl_init( &ssl );
    mbedtls_ssl_config_init( &conf );
    mbedtls_x509_crt_init( &cacert );
    mbedtls_x509_crt_init( &clicert );
    mbedtls_pk_init( &pkey );
    mbedtls_ctr_drbg_init( &ctr_drbg );
    mbedtls_entropy_init( &entropy );

    ret = mbedtls_ctr_drbg_seed( &ctr_drbg, mbedtls_entropy_func, &entropy,
                                  ( const unsigned char * ) "iotc_kvs", 8 );
    if( ret != 0 ) { LogError( ( "mbedtls_ctr_drbg_seed: -0x%x", -ret ) ); goto cleanup; }

    ret = mbedtls_x509_crt_parse( &cacert,
                                   ( const unsigned char * ) IOTCONNECT_CA_CERT,
                                   strlen( IOTCONNECT_CA_CERT ) + 1 );
    if( ret != 0 ) { LogError( ( "CA cert parse: -0x%x", -ret ) ); goto cleanup; }

    ret = mbedtls_x509_crt_parse( &clicert,
                                   ( const unsigned char * ) IOTCONNECT_DEVICE_CERT,
                                   strlen( IOTCONNECT_DEVICE_CERT ) + 1 );
    if( ret != 0 ) { LogError( ( "Device cert parse: -0x%x", -ret ) ); goto cleanup; }

    ret = mbedtls_pk_parse_key( &pkey,
                                 ( const unsigned char * ) IOTCONNECT_DEVICE_KEY,
                                 strlen( IOTCONNECT_DEVICE_KEY ) + 1,
                                 NULL, 0 );
    if( ret != 0 ) { LogError( ( "Device key parse: -0x%x", -ret ) ); goto cleanup; }

    ret = mbedtls_net_connect( &server_fd, host, "443", MBEDTLS_NET_PROTO_TCP );
    if( ret != 0 ) { LogError( ( "TCP connect to %s: -0x%x", host, -ret ) ); goto cleanup; }

    ret = mbedtls_ssl_config_defaults( &conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT );
    if( ret != 0 ) { LogError( ( "SSL config defaults: -0x%x", -ret ) ); goto cleanup; }

    mbedtls_ssl_conf_authmode( &conf, MBEDTLS_SSL_VERIFY_REQUIRED );
    mbedtls_ssl_conf_ca_chain( &conf, &cacert, NULL );
    mbedtls_ssl_conf_own_cert( &conf, &clicert, &pkey );
    mbedtls_ssl_conf_rng( &conf, mbedtls_ctr_drbg_random, &ctr_drbg );

    ret = mbedtls_ssl_setup( &ssl, &conf );
    if( ret != 0 ) { LogError( ( "SSL setup: -0x%x", -ret ) ); goto cleanup; }

    ret = mbedtls_ssl_set_hostname( &ssl, host );
    if( ret != 0 ) { LogError( ( "SSL set_hostname: -0x%x", -ret ) ); goto cleanup; }

    mbedtls_ssl_set_bio( &ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL );

    while( ( ret = mbedtls_ssl_handshake( &ssl ) ) != 0 )
    {
        if( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE )
        {
            LogError( ( "TLS handshake failed: -0x%x", -ret ) );
            goto cleanup;
        }
    }

    char request[ 512 ];
    int  req_len = snprintf( request, sizeof( request ),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "x-amzn-iot-thingname: %s\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host, IOTCONNECT_DUID );

    ret = mbedtls_ssl_write( &ssl, ( unsigned char * ) request, ( size_t ) req_len );
    if( ret < 0 ) { LogError( ( "SSL write: -0x%x", -ret ) ); goto cleanup; }

    int total = 0;
    while( total < MAX_RESPONSE_SIZE - 1 )
    {
        int n = mbedtls_ssl_read( &ssl,
                                   ( unsigned char * )( buf + total ),
                                   MAX_RESPONSE_SIZE - total - 1 );
        if( n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE ) continue;
        if( n <= 0 ) break;
        total += n;
    }
    buf[ total ] = '\0';

    if( total == 0 )
    {
        LogError( ( "No response from KVS credential endpoint" ) );
        ret = -1;
        goto cleanup;
    }

    /* Skip HTTP headers */
    char *body = strstr( buf, "\r\n\r\n" );
    if( !body )
    {
        LogError( ( "No HTTP body in credential response" ) );
        ret = -1;
        goto cleanup;
    }
    body += 4;

    /* Parse credentials JSON using iotc-c-lib */
    IotclDraCredentialsResult parsed = { 0 };
    ret = iotcl_dra_json_credentials_parse( &parsed, body );
    if( ret != IOTCL_SUCCESS )
    {
        LogError( ( "Credential JSON parse failed: %d", ret ) );
        ret = -1;
        goto cleanup;
    }

    /* Copy into fixed-size output struct */
    memset( creds, 0, sizeof( *creds ) );
    strncpy( creds->access_key_id,     parsed.access_key_id,     IOTC_KVS_ACCESS_KEY_MAX_LEN );
    strncpy( creds->secret_access_key, parsed.secret_access_key, IOTC_KVS_SECRET_KEY_MAX_LEN );
    strncpy( creds->session_token,     parsed.session_token,      IOTC_KVS_SESSION_TOKEN_MAX_LEN );
    creds->expiration_epoch = ( uint64_t ) parsed.expiration;
    creds->valid            = true;

    LogInfo( ( "KVS credentials fetched. Access key: %.8s... Expiry: %s",
               creds->access_key_id, parsed.expiration_str ) );

    iotcl_dra_json_credentials_free( &parsed );
    ret = 0;

cleanup:
    mbedtls_ssl_close_notify( &ssl );
    mbedtls_net_free( &server_fd );
    mbedtls_x509_crt_free( &cacert );
    mbedtls_x509_crt_free( &clicert );
    mbedtls_pk_free( &pkey );
    mbedtls_ssl_free( &ssl );
    mbedtls_ssl_config_free( &conf );
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_entropy_free( &entropy );

    if( buf ) vPortFree( buf );

    return ret;
}

bool IoTConnect_KvsCredentialsExpired( const IoTConnectKvsCredentials_t *creds )
{
    if( !creds || !creds->valid ) return true;
    return ( ( uint64_t ) time( NULL ) >= creds->expiration_epoch );
}

int64_t IoTConnect_KvsCredentialSecsToExpiry( const IoTConnectKvsCredentials_t *creds )
{
    if( !creds || !creds->valid ) return -1;
    return ( int64_t ) creds->expiration_epoch - ( int64_t ) time( NULL );
}
