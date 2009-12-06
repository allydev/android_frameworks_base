/*
 * Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of Code Aurora nor
 *      the names of its contributors may be used to endorse or promote
 *      products derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _HEADSET_HANDSFREE_EVENT_LOOP_H
#define _HEADSET_HANDSFREE_EVENT_LOOP_H

#ifdef USE_BM3_BLUETOOTH
#include <pthread.h>

namespace android {
/* HSP/HFP globals used for sharing/communicating state for BM3 implementation */
extern char* g_session_path;
extern pthread_mutex_t g_session_created_mutex;
extern pthread_cond_t g_session_created_cond;
extern int g_session_created_success;

extern pthread_mutex_t g_incoming_sco_connection_mutex;
extern pthread_cond_t g_incoming_sco_connection_cond;
extern int g_incoming_sco_connection_success;

extern pthread_mutex_t g_incoming_rfcomm_connection_mutex;
extern pthread_cond_t g_incoming_rfcomm_connection_cond;
extern int g_incoming_rfcomm_connection_type;
extern char g_incoming_rfcomm_remote_addr[];

extern char g_rfcomm_read_buf[];
extern int g_rfcomm_read_buf_len;
extern const char* g_rfcomm_read_buf_cur;
extern int g_headset_base_read_err;

/* HSP/HFP Function Prototypes */
bool initializeHsHfNativeData();
bool registerHsHfProfiles();
void cleanupHsHfNativeData();
bool startHsHfEventLoop();
void stopHsHfEventLoop();
bool acceptIncomingConnection(const char* profile_path);
bool requestOutgoingHfAgConnection(const char* profile_path, const char* addr);
void disconnectHfAgConnection();
const char* readHfAg(uint32_t timeout);
int writeHfAg(const char* data);
int requestVoiceConnect(const char* parameters);
int disconnectVoice();
void closeConnectedSco();
} /* namespace android */
#endif /* USE_BM3_BLUETOOTH */
#endif /* _HEADSET_HANDSFREE_EVENT_LOOP_H */
