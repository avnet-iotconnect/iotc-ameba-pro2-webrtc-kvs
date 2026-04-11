/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 * 
 * MODIFIED: IoTConnect integration added
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"

#include "sys_api.h"
#include "sntp/sntp.h"
#include "wifi_conf.h"
#include "lwip_netconf.h"
#include "srtp.h"

#include "demo_config.h"
#include "app_common.h"
#include "app_media_source.h"
#include "logging.h"

// IOTCONNECT: Add header
#include "iotconnect/iotconnect.h"

AppContext_t appContext;
AppMediaSourcesContext_t appMediaSourceContext;

static void Master_Task( void * pParameter );

static int32_t InitTransceiver( void * pMediaCtx,
                                TransceiverTrackKind_t trackKind,
                                Transceiver_t * pTranceiver );
static int32_t OnMediaSinkHook( void * pCustom,
                                MediaFrame_t * pFrame );
static int32_t InitializeAppMediaSource( AppContext_t * pAppContext,
                                         AppMediaSourcesContext_t * pAppMediaSourceContext );

static int32_t InitTransceiver( void * pMediaCtx,
                                TransceiverTrackKind_t trackKind,
                                Transceiver_t * pTranceiver )
{
    int32_t ret = 0;
    AppMediaSourcesContext_t * pMediaSourceContext = ( AppMediaSourcesContext_t * )pMediaCtx;

    if( ( pMediaCtx == NULL ) || ( pTranceiver == NULL ) )
    {
        LogError( ( "Invalid input, pMediaCtx: %p, pTranceiver: %p", pMediaCtx, pTranceiver ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        switch( trackKind )
        {
            case TRANSCEIVER_TRACK_KIND_VIDEO:
                ret = AppMediaSource_InitVideoTransceiver( pMediaSourceContext,
                                                           pTranceiver );
                break;
            case TRANSCEIVER_TRACK_KIND_AUDIO:
                ret = AppMediaSource_InitAudioTransceiver( pMediaSourceContext,
                                                           pTranceiver );
                break;
            default:
                LogError( ( "Unknown track kind: %d", trackKind ) );
                ret = -2;
                break;
        }
    }

    return ret;
}

static int32_t OnMediaSinkHook( void * pCustom,
                                MediaFrame_t * pFrame )
{
    int32_t ret = 0;
    AppContext_t * pAppContext = ( AppContext_t * ) pCustom;
    PeerConnectionResult_t peerConnectionResult;
    Transceiver_t * pTransceiver = NULL;
    PeerConnectionFrame_t peerConnectionFrame;
    int i;

    if( ( pAppContext == NULL ) || ( pFrame == NULL ) )
    {
        LogError( ( "Invalid input, pCustom: %p, pFrame: %p", pCustom, pFrame ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        peerConnectionFrame.version = PEER_CONNECTION_FRAME_CURRENT_VERSION;
        peerConnectionFrame.presentationUs = pFrame->timestampUs;
        peerConnectionFrame.pData = pFrame->pData;
        peerConnectionFrame.dataLength = pFrame->size;

        for( i = 0; i < AWS_MAX_VIEWER_NUM; i++ )
        {
            if( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO )
            {
                pTransceiver = &pAppContext->appSessions[ i ].transceivers[ DEMO_TRANSCEIVER_MEDIA_INDEX_VIDEO ];
            }
            else if( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_AUDIO )
            {
                pTransceiver = &pAppContext->appSessions[ i ].transceivers[ DEMO_TRANSCEIVER_MEDIA_INDEX_AUDIO ];
            }
            else
            {
                LogWarn( ( "Unknown track kind: %d", pFrame->trackKind ) );
                break;
            }

            if( pAppContext->appSessions[ i ].peerConnectionSession.state == PEER_CONNECTION_SESSION_STATE_CONNECTION_READY )
            {
                peerConnectionResult = PeerConnection_WriteFrame( &pAppContext->appSessions[ i ].peerConnectionSession,
                                                                  pTransceiver,
                                                                  &peerConnectionFrame );

                if( peerConnectionResult != PEER_CONNECTION_RESULT_OK )
                {
                    LogError( ( "Fail to write %s frame, result: %d", ( pFrame->trackKind == TRANSCEIVER_TRACK_KIND_VIDEO ) ? "video" : "audio",
                                peerConnectionResult ) );
                    ret = -3;
                }
            }
        }
    }

    return ret;
}

static int32_t InitializeAppMediaSource( AppContext_t * pAppContext,
                                         AppMediaSourcesContext_t * pAppMediaSourceContext )
{
    int32_t ret = 0;

    if( ( pAppContext == NULL ) ||
        ( pAppMediaSourceContext == NULL ) )
    {
        LogError( ( "Invalid input, pAppContext: %p, pAppMediaSourceContext: %p", pAppContext, pAppMediaSourceContext ) );
        ret = -1;
    }

    if( ret == 0 )
    {
        ret = AppMediaSource_Init( pAppMediaSourceContext,
                                   OnMediaSinkHook,
                                   pAppContext );
    }

    return ret;
}

static void Master_Task( void * pParameter )
{
    int32_t ret = 0;

    ( void ) pParameter;

    LogInfo( ( "Start Master_Task." ) );

    ret = AppCommon_Init( &appContext, InitTransceiver, &appMediaSourceContext );

    if( ret == 0 )
    {
        ret = InitializeAppMediaSource( &appContext, &appMediaSourceContext );
    }

    if( ret == 0 )
    {
        memcpy( &( appContext.signalingControllerClientId[ 0 ] ), SIGNALING_CONTROLLER_MASTER_CLIENT_ID, SIGNALING_CONTROLLER_MASTER_CLIENT_ID_LENGTH );
        appContext.signalingControllerClientId[ SIGNALING_CONTROLLER_MASTER_CLIENT_ID_LENGTH ] = '\0';
        appContext.signalingControllerClientIdLength = SIGNALING_CONTROLLER_MASTER_CLIENT_ID_LENGTH;
        appContext.signalingControllerRole = SIGNALING_ROLE_MASTER;
    }

    // IOTCONNECT: Initialize and start
    if( ret == 0 )
    {
        LogInfo( ( "Initializing IoTConnect..." ) );
        ret = IoTConnect_Init();
        if( ret != 0 )
        {
            LogError( ( "IoTConnect init failed: %ld", (long)ret ) );
        }
    }

    if( ret == 0 )
    {
        ret = IoTConnect_Start();
        if( ret != 0 )
        {
            LogError( ( "IoTConnect start failed: %ld", (long)ret ) );
        }
    }

    if( ret == 0 )
    {
        ret = AppCommon_StartSignalingController( &appContext );
    }

    if( ret == 0 )
    {
        AppCommon_WaitSignalingControllerStop( &appContext );
    }

    // IOTCONNECT: Cleanup
    IoTConnect_Stop();
    IoTConnect_Deinit();

    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( 200 ) );
    }
}

void app_example( void )
{
    int ret = 0;

    if( ret == 0 )
    {
        #ifdef BUILD_INFO
        LogInfo( ( "\r\nBuild Info: %s\r\n", BUILD_INFO ) );
        #endif
    }

    if( ret == 0 )
    {
        if( xTaskCreate( Master_Task,
                         ( ( const char * ) "MasterTask" ),
                         4096,
                         NULL,
                         tskIDLE_PRIORITY + 4,
                         NULL ) != pdPASS )
        {
            LogError( ( "xTaskCreate(Master_Task) failed" ) );
            ret = -1;
        }
    }
}
