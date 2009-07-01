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

#define LOG_TAG "HeadsetHandsfreeEventLoop.cpp"

#ifdef USE_BM3_BLUETOOTH
#include "android_bluetooth_common.h"
#include "HeadsetHandsfreeEventLoop.h"
#include "cutils/sockets.h"
#include "jni.h"
#include "utils/Log.h"
#include "utils/misc.h"

#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <cutils/properties.h>
#include <dbus/dbus.h>

/* Prototypes from ScoSocket */
namespace android {

typedef event_loop_native_data_t native_data_t;
static native_data_t* g_nat = NULL;
char* g_session_path = NULL;
pthread_mutex_t g_session_created_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_session_created_cond = PTHREAD_COND_INITIALIZER;
int g_session_created_success = -1;
static pthread_mutex_t g_readhfag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_readhfag_cond = PTHREAD_COND_INITIALIZER;
char g_rfcomm_read_buf[1024]; // TODO: remove magic number
int g_rfcomm_read_buf_len = 0;
const char* g_rfcomm_read_buf_cur = g_rfcomm_read_buf;
char g_incoming_rfcomm_remote_addr[256]; // TODO: remove magic number

bool initializeHsHfNativeData() {
    LOGV("+%s",__FUNCTION__);
    g_nat = (native_data_t *)calloc(1, sizeof(native_data_t));
    if (NULL == g_nat) {
        LOGE("%s: out of memory!", __FUNCTION__);
        LOGV("-%s",__FUNCTION__);
        return false;
    }

    pthread_mutex_init(&(g_nat->thread_mutex), NULL);

    {
        DBusError err;
        dbus_error_init(&err);
        dbus_threads_init_default();

        g_nat->conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
        if (dbus_error_is_set(&err)) {
            LOGE("%s: Could not get onto the system bus!", __FUNCTION__);
            dbus_error_free(&err);
            LOGV("-%s",__FUNCTION__);
            return false;
        }
        dbus_connection_set_exit_on_disconnect(g_nat->conn, FALSE);
    }

    LOGV("-%s",__FUNCTION__);
    return true;
}

static bool registerBm3HfAg() {
    LOGV("+%s",__FUNCTION__);
    DBusMessageIter iter, dict;
    DBusMessage *reply = NULL;
    DBusMessage *msg = NULL;
    DBusError err;
    dbus_error_init(&err);
    bool result = false;
    const char* features_str = "features";
    uint16_t features_u16 = 0; /* nothing special */

    DBusMessageIter dict_entry, variant;

    if (NULL == g_nat) {
        LOGE("%s called with no D-Bus connection!", __FUNCTION__);
        goto done;
    }

    LOGV("Registering BM3 HF AG Profile.");
    msg = dbus_message_new_method_call(BM3_DBUS_HSHF_SVC,
                                       BM3_DBUS_HSHF_PATH,
                                       BM3_DBUS_HSHF_IFC,
                                       BM3_DBUS_HSHF_REGISTER);

    if (msg == NULL)
    {
      LOGE("Unable to allocate new D-Bus message %s!", BM3_DBUS_HSHF_REGISTER);
      goto done;
    }

    dbus_message_iter_init_append(msg, &iter);
    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                          DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                          DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
                                          DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict)) {
        LOGE("Could not open D-Bus container!");
        goto done;
    }

    /* Using default registration parameters: gives us HF AG. */
    // add key: features
    if (!dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY,
                                          NULL, &dict_entry)) {
        LOGE("Could not open D-Bus container!");
        goto done;
    }

    LOGI("Opened dictionary container");

    if (!dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING,
                                        &features_str)) {
        LOGE("Could not append key in D-Bus DICT_ENTRY!");
        goto close_dict_entry;
    }

    LOGI("Appended key");

    if (!dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT,
                                          DBUS_TYPE_UINT16_AS_STRING, &variant)) {
        LOGE("Could not open D-Bus container!");
        goto close_dict_entry;
    }

    LOGI("Opened variant container");

    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT16,
                                        &features_u16)) {
        LOGE("Could not append value in D-Bus DICT_ENTRY!");
    } else {
        result = true;
    }

    LOGI("Appended variant value");

    if (!dbus_message_iter_close_container(&dict_entry, &variant)) {
        LOGE("Could not close D-Bus container!");
        result = false;
        goto done;
    }

    LOGI("Closed variant container");

close_dict_entry:
    if (!dbus_message_iter_close_container(&dict, &dict_entry)) {
        LOGE("Could not close D-Bus container!");
        result = false;
        goto done;
    }

    LOGI("Closed dictionary container");

    dbus_message_iter_close_container(&iter, &dict);

    reply = dbus_connection_send_with_reply_and_block(g_nat->conn,
                                                      msg,
                                                      10*1000,
                                                      &err);
    if (NULL == reply) {
        result = false;
        LOG_AND_FREE_DBUS_ERROR(&err);
    } else {
        dbus_message_unref(reply);
    }

done:
    if (NULL != msg) {
        dbus_message_unref(msg);
    }

    if (result) {
        LOGV("... BM3 HF AG Profile Registration: SUCCESS.");
    } else {
        LOGE("... BM3 HF AG Profile Registration: FAILURE!");
    }

    LOGV("-%s",__FUNCTION__);
    return result;
}

static bool registerBm3HsAg() {
    LOGV("+%s",__FUNCTION__);
    DBusMessageIter iter, dict;
    DBusMessageIter dict_entry, variant;
    DBusMessage *reply = NULL;
    DBusMessage *msg = NULL;
    DBusError err;
    dbus_error_init(&err);
    bool result = false;
    const char* uuid_str = "uuid16";
    uint16_t uuid_u16 = 0x1112; /* HS AG */
    const char* inband_ring_str = "inbandRing";
    dbus_bool_t inband_ring_bool = FALSE;

    if (NULL == g_nat) {
        LOGE("%s called with no D-Bus connection!", __FUNCTION__);
        goto done;
    }

    LOGV("Registering BM3 HS AG Profile.");
    msg = dbus_message_new_method_call(BM3_DBUS_HSHF_SVC,
                                       BM3_DBUS_HSHF_PATH,
                                       BM3_DBUS_HSHF_IFC,
                                       BM3_DBUS_HSHF_REGISTER);

    if (msg == NULL)
    {
      goto done;
    }

    dbus_message_iter_init_append(msg, &iter);
    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                          DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                          DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
                                          DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict)) {
        LOGE("Could not open D-Bus container!");
        goto done;
    }

    // add key: uuid16 = 0x1112 (HS AG)
    if (!dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY,
                                          NULL, &dict_entry)) {
        LOGE("Could not open D-Bus container!");
        goto done;
    }

    LOGI("Opened dictionary entry container");

    if (!dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING,
                                        &uuid_str)) {
        LOGE("Could not append key in D-Bus DICT_ENTRY!");
        goto close_dict_entry_uuid16;
    }

    LOGI("Appended key");

    if (!dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT,
                                          DBUS_TYPE_UINT16_AS_STRING, &variant)) {
        LOGE("Could not open D-Bus container!");
        goto close_dict_entry_uuid16;
    }

    LOGI("Opened variant container");

    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT16,
                                        &uuid_u16)) {
        LOGE("Could not append value in D-Bus DICT_ENTRY!");
    } else {
        result = true;
    }

    LOGI("Appended variant value");

    if (!dbus_message_iter_close_container(&dict_entry, &variant)) {
        LOGE("Could not close D-Bus container!");
        result = false;
        goto done;
    }

    LOGI("Closed variant container");

close_dict_entry_uuid16:
    if (!dbus_message_iter_close_container(&dict, &dict_entry)) {
        LOGE("Could not close D-Bus container!");
        result = false;
        goto done;
    }

    LOGI("Closed dictionary entry container");

    // add key: inbandRing = FALSE
    // TODO: verify that we want FALSE here.
    if (!dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY,
                                          NULL, &dict_entry)) {
        LOGE("Could not open D-Bus container!");
        result = false;
        goto done;
    }

    LOGI("Opened dictionary entry container");

    if (!dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING,
                                        &inband_ring_str)) {
        LOGE("Could not append key in D-Bus DICT_ENTRY!");
        result = false;
        goto close_dict_entry_inband_ring;
    }

    LOGI("Appended key");

    if (!dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT,
                                          DBUS_TYPE_BOOLEAN_AS_STRING, &variant)) {
        LOGE("Could not open D-Bus container!");
        result = false;
        goto close_dict_entry_inband_ring;
    }

    LOGI("Opened variant container");

    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN,
                                        &inband_ring_bool)) {
        LOGE("Could not append value in D-Bus DICT_ENTRY!");
        result = false;
    } else {
        result = true;
    }

    LOGI("Appended variant value");

    if (!dbus_message_iter_close_container(&dict_entry, &variant)) {
        LOGE("Could not close D-Bus container!");
        result = false;
        goto done;
    }

    LOGI("Closed variant container");

close_dict_entry_inband_ring:
    if (!dbus_message_iter_close_container(&dict, &dict_entry)) {
        LOGE("Could not close D-Bus container!");
        result = false;
        goto done;
    }

    LOGI("Closed dictionary entry container");
    dbus_message_iter_close_container(&iter, &dict);

    reply = dbus_connection_send_with_reply_and_block(g_nat->conn,
                                                      msg,
                                                      10*1000,
                                                      &err);
    if (NULL == reply) {
        result = false;
        LOG_AND_FREE_DBUS_ERROR(&err);
    } else {
        dbus_message_unref(reply);
    }

done:
    if (NULL != msg) {
        dbus_message_unref(msg);
    }

    if (result) {
        LOGV("... BM3 HS AG Profile Registration: SUCCESS.");
    } else {
        LOGE("... BM3 HS AG Profile Registration: FAILURE!");
    }

    LOGV("-%s",__FUNCTION__);
    return result;
}

bool registerHsHfProfiles() {
    LOGV("+%s",__FUNCTION__);
    bool result = false; // set to true if any profile is registered.
    DBusError err;
    dbus_error_init(&err);

    // Allow user to control which profile is enabled for debugging
    char prop_service_disable[PROPERTY_VALUE_MAX] = "";
    bool dbg_disable_hsp = false;
    bool dbg_disable_hfp = false;
    property_get("dbg.qcom.bm3.disable_hspag", prop_service_disable, "");
    if (!strncmp("1", prop_service_disable, PROPERTY_VALUE_MAX)) {
        LOGV("dbg.qcom.bm3.disable_hspag = 1, not registering HSP AG.");
        dbg_disable_hsp = true;
    }

    property_get("dbg.qcom.bm3.disable_hfpag", prop_service_disable, "");
    if (!strncmp("1", prop_service_disable, PROPERTY_VALUE_MAX)) {
        LOGV("dbg.qcom.bm3.disable_hfpag = 1, not registering HFP AG.");
        dbg_disable_hfp = true;
    }

    if (!dbg_disable_hsp && registerBm3HsAg()) {
        result = true;
    }

    if (!dbg_disable_hfp && registerBm3HfAg()) {
        result = true;
    }

    LOGV("-%s",__FUNCTION__);
    return result;
}

bool acceptIncomingConnection(const char* profile_path) {
    LOGV("+%s",__FUNCTION__);
    DBusError err;
    dbus_error_init(&err);

    g_session_created_success = -1;

    if (g_session_path && g_nat) {
        LOGV("... Accepting incoming connection.");
        dbus_bool_t accept_incoming = TRUE;
        DBusMessage *reply = dbus_func_args_error(NULL, g_nat->conn, &err,
                                            BM3_DBUS_HSHF_SVC,
                                            profile_path,
                                            BM3_DBUS_HSHF_PROFILE_IFC,
                                            BM3_DBUS_HSHF_PROFILE_ACC_INCOMING,
                                            DBUS_TYPE_OBJECT_PATH, &g_session_path,
                                            DBUS_TYPE_BOOLEAN, &accept_incoming,
                                            DBUS_TYPE_INVALID);

        if (NULL == reply) {
            LOGE("HSHF connection accept failure!");
            LOG_AND_FREE_DBUS_ERROR(&err);
            free(g_session_path);
            g_session_path = NULL;
            LOGV("-%s",__FUNCTION__);
            return false;
        } else {
            dbus_message_unref(reply);

            LOGV("... Waiting for session %s to establish connection.", g_session_path);
            if (0 == pthread_mutex_lock(&g_session_created_mutex)) {
                while (g_session_created_success < 0) {
                    if (0 != pthread_cond_wait(&g_session_created_cond, &g_session_created_mutex)) {
                        LOGE("Error waiting for session %s establishment!", g_session_path);
                        // TODO: BAD.
                    }
                }

                pthread_mutex_unlock(&g_session_created_mutex);
            } else {
                LOGE("HSHF session connection mutex lock failure!");
                free(g_session_path);
                g_session_path = NULL;
                LOGV("-%s",__FUNCTION__);
                return false;
            }

            if (g_session_created_success == 1) {
                LOGV("... Session %s connected.", g_session_path);
            } else if (g_session_created_success == 0) {
                LOGV("... Session %s connection failure.", g_session_path);
                free(g_session_path);
                g_session_path = NULL;
                LOGV("-%s",__FUNCTION__);
                return false;
            }

            LOGV("-%s",__FUNCTION__);
            return true;
        }
    }

    LOGV("-%s",__FUNCTION__);
    return false;
}

bool requestOutgoingHfAgConnection(const char* profile_path, const char* addr) {
    LOGV("+%s",__FUNCTION__);
    DBusError err;
    dbus_error_init(&err);

    g_session_created_success = -1;

    if (NULL == g_nat) {
        LOGE("%s called with no D-Bus connection!", __FUNCTION__);
        LOGV("-%s",__FUNCTION__);
        return false;
    }

    DBusMessage *reply = dbus_func_args_error(NULL, g_nat->conn, &err,
                                              BM3_DBUS_HSHF_SVC,
                                              profile_path,
                                              BM3_DBUS_HSHF_PROFILE_IFC,
                                              BM3_DBUS_HSHF_PROFILE_REQ_OUTGOING,
                                              DBUS_TYPE_STRING, &addr,
                                              DBUS_TYPE_INVALID);

    if (NULL == reply) {
        LOG_AND_FREE_DBUS_ERROR(&err);
        LOGV("-%s",__FUNCTION__);
        return false;
    }

    const char *msg_session_path;
    dbus_error_init(&err);
    if (dbus_message_get_args(reply, &err,
                              DBUS_TYPE_OBJECT_PATH, &msg_session_path,
                              DBUS_TYPE_INVALID)) {
        LOGV("... Session Created = %s", msg_session_path);
    } else {
        LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, reply);
        LOGV("-%s",__FUNCTION__);
        return false;
    }

    g_session_path = (char*) malloc(strlen(msg_session_path) + 1);
    if (NULL != g_session_path) {
        strcpy(g_session_path, msg_session_path);
    } else {
        LOGE("Could not allocate memory for storing session object path!");
        dbus_message_unref(reply);
        LOGV("-%s",__FUNCTION__);
        return false;
    }

    dbus_message_unref(reply);

    LOGV("-%s",__FUNCTION__);
    return true;
}

void disconnectHfAgConnection() {
    LOGV("+%s",__FUNCTION__);
    if (g_session_path && g_nat) {
        DBusMessage *reply = dbus_func_args(NULL, g_nat->conn,
                                                  BM3_DBUS_HSHF_SVC,
                                                  g_session_path,
                                                  BM3_DBUS_HSHF_SESSION_IFC,
                                                  BM3_DBUS_HSHF_SESSION_DISCONNECT_SESSION,
                                                  DBUS_TYPE_INVALID);

        if (NULL != reply) {
            dbus_message_unref(reply);
        }

        free(g_session_path);
        g_session_path = NULL;
    }
    LOGV("-%s",__FUNCTION__);
}

static void readHfAgComplete(DBusPendingCall *pending, void *user_data) {
    // pending, user_data are unused--readHfAgComplete() simply acts like a signal 
    LOGV("+%s",__FUNCTION__);
    if (0 == pthread_mutex_lock(&g_readhfag_mutex)) {
        g_headset_base_read_err = 0; // 0 indicates success
        if (0 != pthread_cond_signal(&g_readhfag_cond)) {
            LOGE("Error signalling read completion!");
            // TODO: BAD.
        }

        pthread_mutex_unlock(&g_readhfag_mutex);
        sched_yield();
    } else {
        LOGE("HSHF read completion mutex lock failure!");
    }
    LOGV("-%s",__FUNCTION__);
}

const char* readHfAg(uint32_t timeout) {
    LOGV("+%s",__FUNCTION__);

    const char* ret = NULL;

    if (g_session_path) {
        DBusError err;
        char *read_buf;
        DBusMessage *msg = NULL, *reply = NULL;
        DBusPendingCall *pend = NULL;
        g_headset_base_read_err = -1; // < 0 indicates non-completed read
        dbus_error_init(&err);

        // Detect a dropped connection.
        if ( NULL == g_nat ) {
            LOGV("Service connection is closed.  Returning ReceiveData error.");
            goto readhfag_done;
        }

        /* TODO: (long-term) rework event loop to minimize blocking calls */
        LOGV("Sending ReceiveData to D-Bus");
       msg = dbus_message_new_method_call(BM3_DBUS_HSHF_SVC, g_session_path, BM3_DBUS_HSHF_SESSION_IFC, BM3_DBUS_HSHF_SESSION_RECEIVE_DATA);
        if (NULL != msg) {
            // TODO: more error checking here.
            dbus_message_append_args(msg, DBUS_TYPE_UINT32, &timeout, DBUS_TYPE_INVALID);
            dbus_connection_send_with_reply(g_nat->conn, msg, &pend, timeout*2);
            dbus_pending_call_set_notify(pend, readHfAgComplete, NULL, NULL);
            dbus_message_unref(msg);

            // Reply may have arrived before we set the callback, so check for that
            if (dbus_pending_call_get_completed(pend)) {
                LOGV("Not blocking on ReceiveData--already returned!");
            } else {
                LOGV("Blocking on ReceiveData");

                /* wait up to 2*timeout for ReceiveData (don't want to race with timeout in D-Bus call) */
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 2*timeout/1000;
                ts.tv_nsec += (2*timeout%1000)*1000000;
                if (ts.tv_nsec > 1000000000L) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000L;
                }

                if (0 == pthread_mutex_lock(&g_readhfag_mutex)) {
                    while (g_headset_base_read_err < 0) {
                        int status;
                        status = pthread_cond_timedwait(&g_readhfag_cond, &g_readhfag_mutex, &ts);

                        if (status == ETIMEDOUT) {
                            LOGV("HSHF read timeout.");
                            g_headset_base_read_err = 0; // 0 indicates completed read, NULL return buffer indicates timeout
                            pthread_mutex_unlock(&g_readhfag_mutex);
                            dbus_pending_call_cancel(pend);
                            goto readhfag_done;
                        } else if (status != 0) {
                            LOGE("HSHF read error--can't wait for read complete signal!");
                            pthread_mutex_unlock(&g_readhfag_mutex);
                            dbus_pending_call_cancel(pend);
                            goto readhfag_done;
                        }
                    }

                    pthread_mutex_unlock(&g_readhfag_mutex);

                    LOGV("Unblocked for ReceiveData");

                    // In error case, the notify function hasn't tripped yet...
                    if (g_headset_base_read_err > 0) {
                        dbus_pending_call_cancel(pend);
                        LOGV("ReceiveData interrupted due to session close.");
                        goto readhfag_done;
                    }
                } else {
                    LOGE("HSHF read mutex lock failure!");
                    dbus_pending_call_cancel(pend);
                    goto readhfag_done;
                }
            }

            reply = dbus_pending_call_steal_reply(pend);
            if (reply && dbus_set_error_from_message(&err, reply)) {
                // Check if this is a read timeout: we read in a polling fashion,
                // so a timeout is not an error
                if ( (NULL != strstr(err.message, "OI_TIMEOUT")) ||
                     (NULL != strstr(err.message, "BM3 STATUS 112")) ) {
                    LOGV("HSHF read timeout.");
                    dbus_error_free(&err);
                } else {
                    LOG_AND_FREE_DBUS_ERROR(&err);
                }
                goto readhfag_done;
            }
        } else {
            LOGE("Could not allocate ReceiveData message.");
            goto readhfag_done;
        }

        dbus_error_init(&err);
        if (reply && dbus_message_get_args(reply, &err,
                                           DBUS_TYPE_STRING, &read_buf,
                                           DBUS_TYPE_INVALID)) {
            // Copy string, string length to global.
            g_rfcomm_read_buf_len = strnlen(read_buf, sizeof(g_rfcomm_read_buf));
            strncpy(g_rfcomm_read_buf, read_buf, g_rfcomm_read_buf_len);
            g_rfcomm_read_buf[sizeof(g_rfcomm_read_buf)-1] = '\0';

            g_rfcomm_read_buf_cur = g_rfcomm_read_buf;
            ret = g_rfcomm_read_buf;
        } else {
            LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, reply);
        }

readhfag_done:
        if (reply) {
            dbus_message_unref(reply);
        }

        if (pend) {
            dbus_pending_call_unref(pend);
        }
    }

    LOGV("-%s",__FUNCTION__);
    return ret;
}

int writeHfAg(const char* data) {
    LOGV("+%s",__FUNCTION__);

    if (g_session_path && g_nat) {
        DBusMessage *reply = dbus_func_args(NULL, g_nat->conn,
                                                  BM3_DBUS_HSHF_SVC,
                                                  g_session_path,
                                                  BM3_DBUS_HSHF_SESSION_IFC,
                                                  BM3_DBUS_HSHF_SESSION_SEND_DATA,
                                                  DBUS_TYPE_STRING, &data,
                                                  DBUS_TYPE_INVALID);

        if (NULL != reply) {
            dbus_message_unref(reply);
        } else {
            LOGV("-%s",__FUNCTION__);
            return -1;
        }
    } else {
        LOGV("-%s",__FUNCTION__);
        return -1;
    }

    LOGV("-%s",__FUNCTION__);
    return 0;
}

int requestVoiceConnect(const char* parameters) {
    LOGV("+%s",__FUNCTION__);

    if (g_session_path && g_nat) {
        DBusMessage *reply = dbus_func_args(NULL, g_nat->conn,
                                                  BM3_DBUS_HSHF_SVC,
                                                  g_session_path,
                                                  BM3_DBUS_HSHF_SESSION_IFC,
                                                  BM3_DBUS_HSHF_SESSION_REQ_VOICE_CONNECT,
                                                  DBUS_TYPE_STRING, &parameters,
                                                  DBUS_TYPE_INVALID);

        if (NULL != reply) {
            dbus_message_unref(reply);
            LOGV("-%s",__FUNCTION__);
            return 0;
        }
    }

    LOGV("-%s",__FUNCTION__);
    return -1;
}

int disconnectVoice() {
    LOGV("+%s",__FUNCTION__);

    if (g_session_path && g_nat) {
        DBusMessage *reply = dbus_func_args(NULL, g_nat->conn,
                                                  BM3_DBUS_HSHF_SVC,
                                                  g_session_path,
                                                  BM3_DBUS_HSHF_SESSION_IFC,
                                                  BM3_DBUS_HSHF_SESSION_DISCONNECT_VOICE,
                                                  DBUS_TYPE_INVALID);

        if (NULL != reply) {
            dbus_message_unref(reply);
            LOGV("-%s",__FUNCTION__);
            return 0;
        }
    }

    LOGV("-%s",__FUNCTION__);
    return -1;
}

void cleanupHsHfNativeData() {
    LOGV("+%s",__FUNCTION__);

    if (g_nat) {
        pthread_mutex_destroy(&(g_nat->thread_mutex));
        free(g_nat);
        g_nat = NULL;
    }

    LOGV("-%s",__FUNCTION__);
}

static DBusHandlerResult event_filter(DBusConnection *conn, DBusMessage *msg,
                                      void *data);

static unsigned int unix_events_to_dbus_flags(short events) {
    return (events & DBUS_WATCH_READABLE ? POLLIN : 0) |
           (events & DBUS_WATCH_WRITABLE ? POLLOUT : 0) |
           (events & DBUS_WATCH_ERROR ? POLLERR : 0) |
           (events & DBUS_WATCH_HANGUP ? POLLHUP : 0);
}

static short dbus_flags_to_unix_events(unsigned int flags) {
    return (flags & POLLIN ? DBUS_WATCH_READABLE : 0) |
           (flags & POLLOUT ? DBUS_WATCH_WRITABLE : 0) |
           (flags & POLLERR ? DBUS_WATCH_ERROR : 0) |
           (flags & POLLHUP ? DBUS_WATCH_HANGUP : 0);
}

static jboolean setUpEventLoop(native_data_t *nat) {
    LOGV(__FUNCTION__);
    dbus_threads_init_default();
    DBusError err;
    dbus_error_init(&err);

    if (nat != NULL && nat->conn != NULL) {
        // Add a filter for all incoming messages
        if (!dbus_connection_add_filter(nat->conn, event_filter, nat, NULL)){
            return JNI_FALSE;
        }

        // Set which messages will be processed by this dbus connection
        dbus_bus_add_match(nat->conn,
                "type='signal',interface='"BM3_DBUS_HSHF_PROFILE_IFC"'",
                &err);
        if (dbus_error_is_set(&err)) {
            LOG_AND_FREE_DBUS_ERROR(&err);
            return JNI_FALSE;
        }

        dbus_bus_add_match(nat->conn,
                "type='signal',interface='"BM3_DBUS_HSHF_SESSION_IFC"'",
                &err);
        if (dbus_error_is_set(&err)) {
            LOG_AND_FREE_DBUS_ERROR(&err);
            return JNI_FALSE;
        }

        return JNI_TRUE;
    }

    return JNI_FALSE;
}

static void tearDownEventLoop(native_data_t *nat) {
    LOGV(__FUNCTION__);
    if (nat != NULL && nat->conn != NULL) {

        DBusError err;
        dbus_error_init(&err);

        dbus_bus_remove_match(nat->conn,
                "type='signal',interface='"BM3_DBUS_HSHF_SESSION_IFC"'",
                &err);
        if (dbus_error_is_set(&err)) {
            LOG_AND_FREE_DBUS_ERROR(&err);
        }

        dbus_bus_remove_match(nat->conn,
                "type='signal',interface='"BM3_DBUS_HSHF_PROFILE_IFC"'",
                &err);
        if (dbus_error_is_set(&err)) {
            LOG_AND_FREE_DBUS_ERROR(&err);
        }

        dbus_connection_remove_filter(nat->conn, event_filter, nat);
    }
}


#define EVENT_LOOP_EXIT 1
#define EVENT_LOOP_ADD  2
#define EVENT_LOOP_REMOVE 3

static dbus_bool_t dbusAddWatch(DBusWatch *watch, void *data) {
    native_data_t *nat = (native_data_t *)data;

    if (dbus_watch_get_enabled(watch)) {
        // note that we can't just send the watch and inspect it later
        // because we may get a removeWatch call before this data is reacted
        // to by our eventloop and remove this watch..  reading the add first
        // and then inspecting the recently deceased watch would be bad.
        char control = EVENT_LOOP_ADD;
        write(nat->controlFdW, &control, sizeof(char));

        int fd = dbus_watch_get_fd(watch);
        write(nat->controlFdW, &fd, sizeof(int));

        unsigned int flags = dbus_watch_get_flags(watch);
        write(nat->controlFdW, &flags, sizeof(unsigned int));

        write(nat->controlFdW, &watch, sizeof(DBusWatch*));
    }
    return TRUE;
}

static void dbusRemoveWatch(DBusWatch *watch, void *data) {
    native_data_t *nat = (native_data_t *)data;

    char control = EVENT_LOOP_REMOVE;
    write(nat->controlFdW, &control, sizeof(char));

    int fd = dbus_watch_get_fd(watch);
    write(nat->controlFdW, &fd, sizeof(int));

    unsigned int flags = dbus_watch_get_flags(watch);
    write(nat->controlFdW, &flags, sizeof(unsigned int));
}

static void dbusToggleWatch(DBusWatch *watch, void *data) {
    if (dbus_watch_get_enabled(watch)) {
        dbusAddWatch(watch, data);
    } else {
        dbusRemoveWatch(watch, data);
    }
}

static void handleWatchAdd(native_data_t *nat) {
    DBusWatch *watch;
    int newFD;
    unsigned int flags;

    read(nat->controlFdR, &newFD, sizeof(int));
    read(nat->controlFdR, &flags, sizeof(unsigned int));
    read(nat->controlFdR, &watch, sizeof(DBusWatch *));
    short events = dbus_flags_to_unix_events(flags);

    for (int y = 0; y<nat->pollMemberCount; y++) {
        if ((nat->pollData[y].fd == newFD) &&
                (nat->pollData[y].events == events)) {
            LOGV("DBusWatch duplicate add");
            return;
        }
    }
    if (nat->pollMemberCount == nat->pollDataSize) {
        LOGV("Bluetooth EventLoop poll struct growing");
        struct pollfd *temp = (struct pollfd *)malloc(
                sizeof(struct pollfd) * (nat->pollMemberCount+1));
        if (!temp) {
            return;
        }
        memcpy(temp, nat->pollData, sizeof(struct pollfd) *
                nat->pollMemberCount);
        free(nat->pollData);
        nat->pollData = temp;
        DBusWatch **temp2 = (DBusWatch **)malloc(sizeof(DBusWatch *) *
                (nat->pollMemberCount+1));
        if (!temp2) {
            return;
        }
        memcpy(temp2, nat->watchData, sizeof(DBusWatch *) *
                nat->pollMemberCount);
        free(nat->watchData);
        nat->watchData = temp2;
        nat->pollDataSize++;
    }
    nat->pollData[nat->pollMemberCount].fd = newFD;
    nat->pollData[nat->pollMemberCount].revents = 0;
    nat->pollData[nat->pollMemberCount].events = events;
    nat->watchData[nat->pollMemberCount] = watch;
    nat->pollMemberCount++;
}

static void handleWatchRemove(native_data_t *nat) {
    int removeFD;
    unsigned int flags;

    read(nat->controlFdR, &removeFD, sizeof(int));
    read(nat->controlFdR, &flags, sizeof(unsigned int));
    short events = dbus_flags_to_unix_events(flags);

    for (int y = 0; y < nat->pollMemberCount; y++) {
        if ((nat->pollData[y].fd == removeFD) &&
                (nat->pollData[y].events == events)) {
            int newCount = --nat->pollMemberCount;
            // copy the last live member over this one
            nat->pollData[y].fd = nat->pollData[newCount].fd;
            nat->pollData[y].events = nat->pollData[newCount].events;
            nat->pollData[y].revents = nat->pollData[newCount].revents;
            nat->watchData[y] = nat->watchData[newCount];
            return;
        }
    }
    LOGW("WatchRemove given with unknown watch");
}

static void *eventLoopMain(void *ptr) {
    native_data_t *nat = (native_data_t *)ptr;

    dbus_connection_set_watch_functions(nat->conn, dbusAddWatch,
            dbusRemoveWatch, dbusToggleWatch, ptr, NULL);

    while (1) {
        for (int i = 0; i < nat->pollMemberCount; i++) {
            LOGV("HSHF Event Loop fd i=%d",i);
            if (!nat->pollData[i].revents) {
                continue;
            }
            if (nat->pollData[i].fd == nat->controlFdR) {
                char data;
                while (recv(nat->controlFdR, &data, sizeof(char), MSG_DONTWAIT)
                        != -1) {
                    switch (data) {
                    case EVENT_LOOP_EXIT:
                    {
                        dbus_connection_set_watch_functions(nat->conn,
                                NULL, NULL, NULL, NULL, NULL);
                        tearDownEventLoop(nat);
                        shutdown(nat->controlFdR,SHUT_RDWR);
                        return NULL;
                    }
                    case EVENT_LOOP_ADD:
                    {
                        handleWatchAdd(nat);
                        break;
                    }
                    case EVENT_LOOP_REMOVE:
                    {
                        handleWatchRemove(nat);
                        break;
                    }
                    }
                }

                // can only do one - it may have caused a 'remove'
                break;
            } else {
                short events = nat->pollData[i].revents;
                unsigned int flags = unix_events_to_dbus_flags(events);
                LOGV("HSHF Event Loop dbus_watch_handle");
                dbus_watch_handle(nat->watchData[i], flags);
                nat->pollData[i].revents = 0;
            }
        }
        LOGV("HSHF Event Loop dbus_connection_dispatch.");
        while (dbus_connection_dispatch(nat->conn) ==
                DBUS_DISPATCH_DATA_REMAINS) {
            LOGI("HSHF Event Loop dbus_connection_dispatch again.");
        }

        LOGV("HSHF Event Loop entering poll");
        poll(nat->pollData, nat->pollMemberCount, -1);
        LOGV("HSHF Event Loop leaving poll");
    }
}

bool startHsHfEventLoop() {
    bool result = false;

    if (NULL == g_nat) {
        LOGE("%s called before initializeHsHfNativeData!", __FUNCTION__);
        return false;
    }

    pthread_mutex_lock(&(g_nat->thread_mutex));

    if (g_nat->pollData) {
        LOGW("trying to start EventLoop a second time!");
        pthread_mutex_unlock( &(g_nat->thread_mutex) );
        return false;
    }

    g_nat->pollData = (struct pollfd *)malloc(sizeof(struct pollfd) *
            DEFAULT_INITIAL_POLLFD_COUNT);
    if (!g_nat->pollData) {
        LOGE("out of memory error starting EventLoop!");
        goto done;
    }

    g_nat->watchData = (DBusWatch **)malloc(sizeof(DBusWatch *) *
            DEFAULT_INITIAL_POLLFD_COUNT);
    if (!g_nat->watchData) {
        LOGE("out of memory error starting EventLoop!");
        goto done;
    }

    memset(g_nat->pollData, 0, sizeof(struct pollfd) *
            DEFAULT_INITIAL_POLLFD_COUNT);
    memset(g_nat->watchData, 0, sizeof(DBusWatch *) *
            DEFAULT_INITIAL_POLLFD_COUNT);
    g_nat->pollDataSize = DEFAULT_INITIAL_POLLFD_COUNT;
    g_nat->pollMemberCount = 1;

    if (socketpair(AF_LOCAL, SOCK_STREAM, 0, &(g_nat->controlFdR))) {
        LOGE("Error getting BT control socket");
        goto done;
    }
    g_nat->pollData[0].fd = g_nat->controlFdR;
    g_nat->pollData[0].events = POLLIN;

    if (setUpEventLoop(g_nat) != true) {
        LOGE("failure setting up Event Loop!");
        goto done;
    }

    if (pthread_create(&(g_nat->thread), NULL, eventLoopMain, g_nat) != 0) {
        LOGE("%s: pthread_create() failed: %s", __FUNCTION__, strerror(errno));
        goto done;
    }

    result = true;

done:
    if (false == result) {
        if (g_nat->controlFdW || g_nat->controlFdR) {
            shutdown(g_nat->controlFdW, SHUT_RDWR);
            g_nat->controlFdW = 0;
            g_nat->controlFdR = 0;
        }
        if (g_nat->pollData) free(g_nat->pollData);
        g_nat->pollData = NULL;
        if (g_nat->watchData) free(g_nat->watchData);
        g_nat->watchData = NULL;
        g_nat->pollDataSize = 0;
        g_nat->pollMemberCount = 0;
    }

    pthread_mutex_unlock(&(g_nat->thread_mutex));
    return result;
}

void stopHsHfEventLoop() {
    if (NULL == g_nat) {
        LOGE("%s called before initializeHsHfNativeData!", __FUNCTION__);
        return;
    }

    pthread_mutex_lock(&(g_nat->thread_mutex));
    if (g_nat->pollData) {
        char data = EVENT_LOOP_EXIT;
        ssize_t t = write(g_nat->controlFdW, &data, sizeof(char));
        void *ret;
        pthread_join(g_nat->thread, &ret);

        free(g_nat->pollData);
        g_nat->pollData = NULL;
        free(g_nat->watchData);
        g_nat->watchData = NULL;
        g_nat->pollDataSize = 0;
        g_nat->pollMemberCount = 0;
        shutdown(g_nat->controlFdW, SHUT_RDWR);
        g_nat->controlFdW = 0;
        g_nat->controlFdR = 0;
    }
    pthread_mutex_unlock(&(g_nat->thread_mutex));
}

// Called by dbus during WaitForAndDispatchEventNative()
static DBusHandlerResult event_filter(DBusConnection *conn, DBusMessage *msg,
                                      void *data) {
    native_data_t *nat;
    DBusError err;

    dbus_error_init(&err);

    nat = (native_data_t *)data;

    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL) {
        LOGV("%s: not interested (not a signal).", __FUNCTION__);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    LOGV("%s: Received signal %s:%s from %s", __FUNCTION__,
         dbus_message_get_interface(msg), dbus_message_get_member(msg),
         dbus_message_get_path(msg));

    if (dbus_message_is_signal(msg,
                               BM3_DBUS_HSHF_PROFILE_IFC,
                               BM3_DBUS_HSHF_PROFILE_CONNECT_REQ)) {

        const char *msg_session_path;
        const char *c_address;
        if (dbus_message_get_args(msg, &err,
                                  DBUS_TYPE_OBJECT_PATH, &msg_session_path,
                                  DBUS_TYPE_STRING, &c_address,
                                  DBUS_TYPE_INVALID)) {
            LOGV("... session = %s address = %s", msg_session_path, c_address);

            if (NULL != g_session_path) {
                LOGV("... Rejecting incoming service connection--already connected!");

                if (NULL == g_nat) {
                    LOGE("Received incoming service connection prior to HS/HF initialization!");
                    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                }

                dbus_bool_t reject_incoming = FALSE;
                DBusMessage *reply = dbus_func_args_error(NULL, g_nat->conn, &err,
                                                          BM3_DBUS_HSHF_SVC,
                                                          dbus_message_get_path(msg),
                                                          BM3_DBUS_HSHF_PROFILE_IFC,
                                                          BM3_DBUS_HSHF_PROFILE_ACC_INCOMING,
                                                          DBUS_TYPE_OBJECT_PATH, &msg_session_path,
                                                          DBUS_TYPE_BOOLEAN, &reject_incoming,
                                                          DBUS_TYPE_INVALID);

                if (NULL != reply) {
                    dbus_message_unref(reply);
                }
            } else {
                g_session_path = (char*) malloc(strlen(msg_session_path) + 1);
                if (NULL != g_session_path) {
                    strcpy(g_session_path, msg_session_path);
                } else {
                    LOGE("Could not allocate memory for storing session object path!");
                    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                }

                // Copy c_address to global.
                int c_address_len = strlen(c_address) + 1;
                strcpy(g_incoming_rfcomm_remote_addr, c_address);

                if (0 == pthread_mutex_lock(&g_incoming_rfcomm_connection_mutex)) {

                    if ( NULL != strstr(g_session_path, "handsfree") ) {
                        g_incoming_rfcomm_connection_type = 1;
                    } else { /* HSP */
                        g_incoming_rfcomm_connection_type = 2;
                    }

                    if (0 != pthread_cond_signal(&g_incoming_rfcomm_connection_cond)) {
                        LOGE("Error signalling incoming RFCOMM connection!");
                        // TODO: BAD.
                    }

                    pthread_mutex_unlock(&g_incoming_rfcomm_connection_mutex);
                    sched_yield();
                } else {
                    LOGE("Incoming RFCOMM connection mutex lock failure!");
                    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                }
            }
        } else LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(msg,
                                      BM3_DBUS_HSHF_PROFILE_IFC,
                                      BM3_DBUS_HSHF_PROFILE_CONNECT_FAIL)) {

        const char *c_session;
        const char *c_error;
        if (dbus_message_get_args(msg, &err,
                                  DBUS_TYPE_OBJECT_PATH, &c_session,
                                  DBUS_TYPE_STRING, &c_error,
                                  DBUS_TYPE_INVALID)) {
            LOGV("... session = %s connection error = %s", c_session, c_error);
            if (0 == pthread_mutex_lock(&g_session_created_mutex)) {
                g_session_created_success = 0;
                if (0 != pthread_cond_signal(&g_session_created_cond)) {
                    LOGE("Error signalling HSHF connection!");
                    // TODO: BAD.
                }

                pthread_mutex_unlock(&g_session_created_mutex);
                sched_yield();
            } else {
                LOGE("HSHF connection mutex lock failure!");
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        } else LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(msg,
                                      BM3_DBUS_HSHF_PROFILE_IFC,
                                      BM3_DBUS_HSHF_PROFILE_CONNECT_COMP)) {

        const char *c_session;
        if (dbus_message_get_args(msg, &err,
                                  DBUS_TYPE_OBJECT_PATH, &c_session,
                                  DBUS_TYPE_INVALID)) {
            LOGV("... session %s connection complete.", c_session);
            if (0 == pthread_mutex_lock(&g_session_created_mutex)) {
                g_session_created_success = 1;
                if (0 != pthread_cond_signal(&g_session_created_cond)) {
                    LOGE("Error signalling HSHF connection!");
                    // TODO: BAD.
                }

                pthread_mutex_unlock(&g_session_created_mutex);
                sched_yield();
            } else {
                LOGE("HSHF connection mutex lock failure!");
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        } else LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(msg,
                                      BM3_DBUS_HSHF_PROFILE_IFC,
                                      BM3_DBUS_HSHF_PROFILE_CONNECT_CLOSED)) {

        const char *c_session;
        const char *c_reason;
        if (dbus_message_get_args(msg, &err,
                                  DBUS_TYPE_OBJECT_PATH, &c_session,
                                  DBUS_TYPE_STRING, &c_reason,
                                  DBUS_TYPE_INVALID)) {
            LOGV("... session = %s closed reason = %s", c_session, c_reason);

            /* clean-up references to D-Bus session object */
            LOGV("... Freeing D-Bus session path = %s", g_session_path);
            free(g_session_path);
            g_session_path = NULL;

            /* Wake up any reads... */
            if (0 == pthread_mutex_lock(&g_readhfag_mutex)) {
                g_headset_base_read_err = 1; // arbitrary > 0 value (indicates error)
                if (0 != pthread_cond_signal(&g_readhfag_cond)) {
                    LOGE("Error signalling read error!");
                    // TODO: BAD.
                }

                pthread_mutex_unlock(&g_readhfag_mutex);
                sched_yield();
            } else {
                LOGE("HSHF read error mutex lock failure!");
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }

        } else LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(msg,
                                      BM3_DBUS_HSHF_SESSION_IFC,
                                      BM3_DBUS_HSHF_SESSION_VOICE_CONNECT_REQ)) {

        LOGV("... VoiceConnectionRequested received.");

        // TODO: Move this into a function?
        // Just accept the connection--the Android userland does this and will close the
        // connection if it deems it unwanted.
        //
        // Note that user audio is not routed to the BT SoC based on this call alone--this just
        // configures the BT SoC to establish the connection and start looking at the PCM samples.
        if (g_session_path) {
            LOGV("... Accepting voice connection.");
            dbus_bool_t accept_voice = TRUE;
            DBusMessage *reply = dbus_func_args(NULL, nat->conn,
                                                BM3_DBUS_HSHF_SVC,
                                                g_session_path,
                                                BM3_DBUS_HSHF_SESSION_IFC,
                                                BM3_DBUS_HSHF_SESSION_ACC_VOICE_CONNECT,
                                                DBUS_TYPE_BOOLEAN, &accept_voice,
                                                DBUS_TYPE_INVALID);

            if (NULL != reply) {
                dbus_message_unref(reply);
            }

            // Notify [blocking] accept thread of incoming SCO connection
            if (0 == pthread_mutex_lock(&g_incoming_sco_connection_mutex)) {
                g_incoming_sco_connection_success = 1;
                if (0 != pthread_cond_signal(&g_incoming_sco_connection_cond)) {
                    LOGE("Error signalling SCO connection!");
                    // TODO: BAD.
                }

                pthread_mutex_unlock(&g_incoming_sco_connection_mutex);
                sched_yield();
            } else {
                LOGE("SCO connection mutex lock failure!");
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        }

        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(msg,
                                      BM3_DBUS_HSHF_SESSION_IFC,
                                      BM3_DBUS_HSHF_SESSION_VOICE_CONNECT_FAIL)) {

        const char *c_reason;
        if (dbus_message_get_args(msg, &err,
                                  DBUS_TYPE_STRING, &c_reason,
                                  DBUS_TYPE_INVALID)) {
            LOGV("... reason = %s", c_reason);
            // TODO: Handle voice connection failure.
        } else LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(msg,
                                      BM3_DBUS_HSHF_SESSION_IFC,
                                      BM3_DBUS_HSHF_SESSION_VOICE_CONNECT_CLOSED)) {

        const char *c_reason;
        if (dbus_message_get_args(msg, &err,
                                  DBUS_TYPE_STRING, &c_reason,
                                  DBUS_TYPE_INVALID)) {
            LOGV("... reason = %s", c_reason);
            closeConnectedSco();
        } else LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else {
        LOGV("... ignored");
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

} /* namespace android */
#endif /* USE_BM3_BLUETOOTH */
