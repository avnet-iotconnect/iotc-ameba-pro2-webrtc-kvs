/* SPDX-License-Identifier: MIT
 * Copyright (C) 2024 Avnet
 *
 * IoTConnect Discovery + Identity REST API implementation.
 *
 * Uses iotc-c-lib's DRA module for URL construction and JSON parsing.
 * This file is responsible only for the HTTPS transport layer.
 */

#include "iotconnect_discovery.h"
#include "iotconnect_config.h"

#include "iotcl.h"
#include "iotcl_dra_url.h"
#include "iotcl_dra_discovery.h"
#include "iotcl_dra_identity.h"

#include "logging.h"
#include "FreeRTOS.h"

#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_RESPONSE_BYTES ( 8 * 1024 )

/* AWS region extracted from the WebRTC channel ARN — the only piece of
 * information the iotc-c-lib does not expose directly. */
static char *s_aws_region = NULL;

/* ---- HTTPS GET ---------------------------------------------------------- */

/**
 * Plain-TLS (no mTLS) HTTPS GET.
 * Returns a heap-allocated null-terminated response body, or NULL on error.
 * Caller must vPortFree() the result.
 */
static char *https_get( const char *host, const char *path )
{
    mbedtls_net_context      server_fd;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_x509_crt         cacert;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context  entropy;

    char *response = NULL;
    int   ret      = -1;

    mbedtls_net_init( &server_fd );
    mbedtls_ssl_init( &ssl );
    mbedtls_ssl_config_init( &conf );
    mbedtls_x509_crt_init( &cacert );
    mbedtls_ctr_drbg_init( &ctr_drbg );
    mbedtls_entropy_init( &entropy );

    ret = mbedtls_ctr_drbg_seed( &ctr_drbg, mbedtls_entropy_func, &entropy,
                                  ( const unsigned char * ) "iotc_disc", 9 );
    if( ret != 0 ) { LogError( ( "ctr_drbg_seed: -0x%x", -ret ) ); goto cleanup; }

    ret = mbedtls_x509_crt_parse( &cacert,
                                   ( const unsigned char * ) IOTCONNECT_DISCOVERY_CA_CERT,
                                   strlen( IOTCONNECT_DISCOVERY_CA_CERT ) + 1 );
    if( ret != 0 ) { LogError( ( "CA cert parse: -0x%x", -ret ) ); goto cleanup; }

    ret = mbedtls_net_connect( &server_fd, host, "443", MBEDTLS_NET_PROTO_TCP );
    if( ret != 0 ) { LogError( ( "TCP connect to %s: -0x%x", host, -ret ) ); goto cleanup; }

    ret = mbedtls_ssl_config_defaults( &conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT );
    if( ret != 0 ) { LogError( ( "ssl_config_defaults: -0x%x", -ret ) ); goto cleanup; }

    mbedtls_ssl_conf_authmode( &conf, MBEDTLS_SSL_VERIFY_REQUIRED );
    mbedtls_ssl_conf_ca_chain( &conf, &cacert, NULL );
    mbedtls_ssl_conf_rng( &conf, mbedtls_ctr_drbg_random, &ctr_drbg );

    /* Force HTTP/1.1 — the discovery servers prefer HTTP/2 by default */
    static const char *alpn_protos[] = { "http/1.1", NULL };
    mbedtls_ssl_conf_alpn_protocols( &conf, alpn_protos );

    ret = mbedtls_ssl_setup( &ssl, &conf );
    if( ret != 0 ) { LogError( ( "ssl_setup: -0x%x", -ret ) ); goto cleanup; }

    ret = mbedtls_ssl_set_hostname( &ssl, host );
    if( ret != 0 ) { LogError( ( "ssl_set_hostname: -0x%x", -ret ) ); goto cleanup; }

    mbedtls_ssl_set_bio( &ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL );

    while( ( ret = mbedtls_ssl_handshake( &ssl ) ) != 0 )
    {
        if( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE )
        {
            LogError( ( "TLS handshake to %s failed: -0x%x", host, -ret ) );
            goto cleanup;
        }
    }

    char req[ 512 ];
    int  req_len = snprintf( req, sizeof( req ),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host );

    ret = mbedtls_ssl_write( &ssl, ( unsigned char * ) req, ( size_t ) req_len );
    if( ret < 0 ) { LogError( ( "ssl_write: -0x%x", -ret ) ); goto cleanup; }

    char *buf = ( char * ) pvPortMalloc( MAX_RESPONSE_BYTES );
    if( !buf ) { LogError( ( "OOM for response buffer" ) ); goto cleanup; }

    int total = 0;
    while( total < MAX_RESPONSE_BYTES - 1 )
    {
        int n = mbedtls_ssl_read( &ssl,
                                   ( unsigned char * )( buf + total ),
                                   MAX_RESPONSE_BYTES - total - 1 );
        if( n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE ) continue;
        if( n <= 0 ) break;
        total += n;
    }
    buf[ total ] = '\0';

    if( total == 0 )
    {
        LogError( ( "No response from %s%s", host, path ) );
        vPortFree( buf );
        goto cleanup;
    }

    /* Skip HTTP headers */
    char *body = strstr( buf, "\r\n\r\n" );
    if( !body )
    {
        LogError( ( "No HTTP body in response from %s%s", host, path ) );
        vPortFree( buf );
        goto cleanup;
    }
    body += 4;

    /* Detect and decode chunked transfer encoding */
    int chunked = ( strstr( buf, "Transfer-Encoding: chunked" ) != NULL ||
                    strstr( buf, "transfer-encoding: chunked" ) != NULL );

    if( chunked )
    {
        response = ( char * ) pvPortMalloc( MAX_RESPONSE_BYTES );
        if( !response ) { vPortFree( buf ); goto cleanup; }

        char  *src     = body;
        char  *dst     = response;
        size_t dst_max = MAX_RESPONSE_BYTES - 1;
        size_t dst_len = 0;

        while( *src != '\0' )
        {
            char *crlf = strstr( src, "\r\n" );
            if( !crlf ) break;
            *crlf = '\0';
            size_t chunk_size = ( size_t ) strtol( src, NULL, 16 );
            src = crlf + 2;
            if( chunk_size == 0 ) break;
            if( dst_len + chunk_size > dst_max ) chunk_size = dst_max - dst_len;
            memcpy( dst + dst_len, src, chunk_size );
            dst_len += chunk_size;
            src     += chunk_size + 2;
        }
        response[ dst_len ] = '\0';
        vPortFree( buf );
    }
    else
    {
        size_t body_len = strlen( body );
        response = ( char * ) pvPortMalloc( body_len + 1 );
        if( response ) memcpy( response, body, body_len + 1 );
        vPortFree( buf );
    }

cleanup:
    mbedtls_ssl_close_notify( &ssl );
    mbedtls_net_free( &server_fd );
    mbedtls_x509_crt_free( &cacert );
    mbedtls_ssl_free( &ssl );
    mbedtls_ssl_config_free( &conf );
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_entropy_free( &entropy );

    return response;
}

/* ---- Region extraction -------------------------------------------------- */

/**
 * Extract the AWS region from a KVS channel ARN.
 * ARN format: arn:aws:kinesisvideo:{region}:{account}:channel/{name}/{id}
 * Returns a heap-allocated string; caller must vPortFree().
 */
static char *extract_region_from_arn( const char *arn )
{
    if( !arn || arn[ 0 ] == '\0' ) return NULL;

    int         colons       = 0;
    const char *region_start = NULL;
    const char *region_end   = NULL;

    for( const char *c = arn; *c != '\0'; c++ )
    {
        if( *c == ':' )
        {
            colons++;
            if( colons == 3 ) region_start = c + 1;
            if( colons == 4 ) { region_end = c; break; }
        }
    }

    if( !region_start || !region_end || region_end <= region_start ) return NULL;

    size_t len = ( size_t )( region_end - region_start );
    char  *region = ( char * ) pvPortMalloc( len + 1 );
    if( region )
    {
        memcpy( region, region_start, len );
        region[ len ] = '\0';
    }
    return region;
}

/* ---- public API --------------------------------------------------------- */

int IoTConnect_DoDiscovery( void )
{
    IoTConnect_DiscoveryDeinit();

    /* --- Step 1: Discovery --- */
    IotclDraUrlContext discovery_url = { 0 };
    iotcl_dra_discovery_init_url_aws( &discovery_url, IOTCONNECT_CPID, IOTCONNECT_ENV );

    LogInfo( ( "IoTConnect discovery: GET %s", iotcl_dra_url_get_url( &discovery_url ) ) );

    char *disc_body = https_get( iotcl_dra_url_get_hostname( &discovery_url ),
                                  iotcl_dra_url_get_resource( &discovery_url ) );
    iotcl_dra_url_deinit( &discovery_url );

    if( !disc_body )
    {
        LogError( ( "Discovery HTTP GET failed" ) );
        return -1;
    }

    /* --- Step 2: Parse discovery response → get base URL --- */
    IotclDraUrlContext identity_url = { 0 };
    int ret = iotcl_dra_discovery_parse( &identity_url,
                                          strlen( IOTCL_DRA_IDENTITY_PREFIX ) + strlen( IOTCONNECT_DUID ),
                                          disc_body );
    vPortFree( disc_body );

    if( ret != IOTCL_SUCCESS )
    {
        LogError( ( "Discovery response parse failed: %d", ret ) );
        return -1;
    }

    /* --- Step 3: Append /uid/{DUID} to build the identity URL --- */
    iotcl_dra_identity_build_url( &identity_url, IOTCONNECT_DUID );

    LogInfo( ( "IoTConnect identity: GET %s", iotcl_dra_url_get_url( &identity_url ) ) );

    char *id_body = https_get( iotcl_dra_url_get_hostname( &identity_url ),
                                iotcl_dra_url_get_resource( &identity_url ) );
    iotcl_dra_url_deinit( &identity_url );

    if( !id_body )
    {
        LogError( ( "Identity HTTP GET failed" ) );
        return -1;
    }

    /* --- Step 4: Parse identity response → configure library MQTT config --- */
    ret = iotcl_dra_identity_configure_library_mqtt( id_body );
    vPortFree( id_body );

    if( ret != IOTCL_SUCCESS )
    {
        LogError( ( "Identity response parse failed: %d", ret ) );
        return -1;
    }

    IotclMqttConfig *mqtt = iotcl_mqtt_get_config();
    if( mqtt )
    {
        LogInfo( ( "Discovered MQTT host     : %s", mqtt->host      ? mqtt->host      : "(none)" ) );
        LogInfo( ( "Discovered client ID     : %s", mqtt->client_id ? mqtt->client_id : "(none)" ) );
        LogInfo( ( "Discovered KVS creds URL : %s", mqtt->aws.vs_creds_url      ? mqtt->aws.vs_creds_url      : "(none)" ) );
        LogInfo( ( "Discovered channel ARN   : %s", mqtt->aws.webrtc_channel_arn ? mqtt->aws.webrtc_channel_arn : "(none)" ) );
    }

    /* --- Step 5: Extract AWS region from the channel ARN --- */
    if( mqtt && mqtt->aws.webrtc_channel_arn )
    {
        s_aws_region = extract_region_from_arn( mqtt->aws.webrtc_channel_arn );
        if( s_aws_region )
        {
            LogInfo( ( "Discovered AWS region    : %s", s_aws_region ) );
        }
        else
        {
            LogWarn( ( "Could not parse region from channel ARN" ) );
        }
    }
    else
    {
        LogWarn( ( "No channel ARN in identity response — AWS region not auto-discovered" ) );
    }

    return 0;
}

const char *IoTConnect_GetDiscoveredKvsUrl( void )
{
    IotclMqttConfig *mqtt = iotcl_mqtt_get_config();
    return mqtt ? mqtt->aws.vs_creds_url : NULL;
}

const char *IoTConnect_GetDiscoveredMqttHost( void )
{
    IotclMqttConfig *mqtt = iotcl_mqtt_get_config();
    return mqtt ? mqtt->host : NULL;
}

const char *IoTConnect_GetDiscoveredClientId( void )
{
    IotclMqttConfig *mqtt = iotcl_mqtt_get_config();
    return mqtt ? mqtt->client_id : NULL;
}

const char *IoTConnect_GetDiscoveredAwsRegion( void )
{
    return s_aws_region;
}

void IoTConnect_DiscoveryDeinit( void )
{
    if( s_aws_region ) { vPortFree( s_aws_region ); s_aws_region = NULL; }
    /* MQTT config memory is owned by iotcl — freed by iotcl_deinit() */
}
