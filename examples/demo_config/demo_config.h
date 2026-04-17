/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef DEMO_CONFIG_H
#define DEMO_CONFIG_H

#define AWS_KVS_AGENT_NAME      "AWS-SDK-KVS"

#define AWS_MAX_VIEWER_NUM      ( 2 )

/* Audio codec — only one may be set to 1. */
#define AUDIO_G711_MULAW        1
#define AUDIO_G711_ALAW         0
#define AUDIO_OPUS              0
#if ( AUDIO_G711_MULAW + AUDIO_G711_ALAW + AUDIO_OPUS ) != 1
    #error "Exactly one audio codec must be enabled in demo_config.h"
#endif

/* Enable audio receive path. */
#define MEDIA_PORT_ENABLE_AUDIO_RECV    ( 1 )

#endif /* DEMO_CONFIG_H */
