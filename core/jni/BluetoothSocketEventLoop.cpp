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

#define LOG_TAG "BluetoothSocketEventLoop.cpp"

#ifdef USE_BM3_BLUETOOTH
#include "android_bluetooth_common.h"
#include "BluetoothSocketEventLoop.h"
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

namespace android {

// subset of session properties that we care about.
static Properties session_properties[] = {
    {"channel",  DBUS_TYPE_UINT32},
    {"socketPath", DBUS_TYPE_STRING},
    {"socketKey", DBUS_TYPE_UINT32}
};

typedef event_loop_native_data_t native_data_t;
static native_data_t* g_nat = NULL;

// global mutex to access array
pthread_mutex_t g_server_sockets_mutex = PTHREAD_MUTEX_INITIALIZER;

// global statics are 0 initialized by C++ language definition
// server socket id = index into array
server_socket_native_data_t g_server_sockets[BLUETOOTH_SOCKET_MAX_SERVER_SOCKETS];

void initializeServerSocketNativeData(int idx) {
    // caller assumed to have taken g_server_sockets_mutex
    if (idx < 0 || idx >= BLUETOOTH_SOCKET_MAX_SERVER_SOCKETS) {
        LOGE("%s: idx out of bounds!", __FUNCTION__);
        return;
    }
    server_socket_native_data_t* data = &g_server_sockets[idx];

    data->in_use = true;
    pthread_mutex_init(&data->is_connected_mutex, NULL);
    pthread_cond_init(&data->is_connected_cond, NULL);
}

void deleteServerSocketNativeData(int idx) {
    // caller assumed to have taken g_server_sockets_mutex
    if (idx < 0 || idx >= BLUETOOTH_SOCKET_MAX_SERVER_SOCKETS) {
        LOGE("%s: idx out of bounds!", __FUNCTION__);
        return;
    }
    server_socket_native_data_t* data = &g_server_sockets[idx];

    data->in_use = false;
    pthread_mutex_destroy(&data->is_connected_mutex);
    pthread_cond_destroy(&data->is_connected_cond);
    data->asock = NULL;
    memset(data->server_path, 0, sizeof(data->server_path));
    memset(data->remote_addr, 0, sizeof(data->remote_addr));
}

// returns -1 if error, else index into g_server_sockets
int createServerSocketNativeData() {
    // caller assumed to have taken g_server_sockets_mutex
    for (int i=0; i<BLUETOOTH_SOCKET_MAX_SERVER_SOCKETS; i++) {
        if(false == g_server_sockets[i].in_use) {
            initializeServerSocketNativeData(i);
            return i;
        }
    }
    return -1;
}

int findServerSocketNativeData(const char* server_path) {
    // caller assumed to have taken g_server_sockets_mutex
    for (int i=0; i<BLUETOOTH_SOCKET_MAX_SERVER_SOCKETS; i++) {
        if(g_server_sockets[i].in_use &&
           0 == strcmp(g_server_sockets[i].server_path, server_path)) {
            return i;
        }
    }
    return -1;
}

int findServerSocketNativeData(const asocket* asock_ptr) {
    // caller assumed to have taken g_server_sockets_mutex
    for (int i=0; i<BLUETOOTH_SOCKET_MAX_SERVER_SOCKETS; i++) {
        if(g_server_sockets[i].in_use && g_server_sockets[i].asock == asock_ptr) {
            return i;
        }
    }
    return -1;
}

bool initializeBluetoothSocketNativeData() {
    LOGV("+%s",__FUNCTION__);
    if (NULL != g_nat) {
        LOGI("BluetoothSocketEventLoop already initialized--reusing.");
        return false;
    }

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

bool registerBluetoothSocketServer(uint32_t channel, uint32_t security, char* server_path, int server_path_len) {
    LOGV("+%s",__FUNCTION__);
    bool result = false;

    DBusMessageIter iter, dict;
    DBusMessage *reply = NULL;
    DBusMessage *msg = NULL;
    DBusError err;
    dbus_error_init(&err);
    const char* temp_path = NULL;
    const char* channel_str = "channel";
    const char* security_str = "security";
    const char* agent_path = BLUETOOTH_SOCKET_AGENT_PATH;

    DBusMessageIter dict_entry, variant;

    if (NULL == g_nat) {
        LOGE("%s called with no D-Bus connection!", __FUNCTION__);
        goto done;
    }

    LOGV("Registering BM3 BluetoothServerSocket channel %d security %d", channel, security);
    msg = dbus_message_new_method_call(BM3_DBUS_RFCOMM_SVC,
                                       BM3_DBUS_RFCOMM_PATH,
                                       BM3_DBUS_RFCOMM_MGR_IFC,
                                       BM3_DBUS_RFCOMM_MGR_REGISTER);

    if (msg == NULL)
    {
      LOGE("Unable to allocate new D-Bus message %s!", BM3_DBUS_RFCOMM_MGR_REGISTER);
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

    // add key: channel
    if (!dbus_append_variant_dict_entry(&dict, channel_str, DBUS_TYPE_UINT32, &channel)) {
        LOGE("Could not add channel to property dict.");
        goto done;
    }

    // add key: security
    if (!dbus_append_variant_dict_entry(&dict, security_str, DBUS_TYPE_UINT32, &security)) {
        LOGE("Could not add security to property dict.");
        goto done;
    }

    dbus_message_iter_close_container(&iter, &dict);
    LOGI("Closed dictionary entry container");

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &agent_path)) {
         LOGE("Could not append agent path to RegisterServer call!");
         goto done;
    }
    reply = dbus_connection_send_with_reply_and_block(g_nat->conn,
                                                      msg,
                                                      10*1000,
                                                      &err);
    if (NULL == reply) {
        result = false;
        LOG_AND_FREE_DBUS_ERROR(&err);
    } else {
        if (dbus_message_get_args(reply, &err,
                                   DBUS_TYPE_OBJECT_PATH, &temp_path,
                                   DBUS_TYPE_INVALID)) {
            result = true;
        } else {
            LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, reply);
        }
        dbus_message_unref(reply);

        if ( (int)(strlen(temp_path)+1) <= server_path_len ) {
            strcpy(server_path, temp_path);
        } else {
            LOGE("RegisterServer returned server path exceeding passed buffer length!");
            result = false;
        }

        // TODO: verify server is listening on correct RFCOMM channel?
    }

done:
    if (NULL != msg) {
        dbus_message_unref(msg);
    }

    if (result) {
        LOGV("... BM3 BluetoothServerSocket Registration: SUCCESS.");
    } else {
        LOGE("... BM3 BluetoothServerSocket Registration: FAILURE!");
    }

    LOGV("-%s",__FUNCTION__);
    return result;
}

bool deregisterBluetoothSocketServer(const char* server_path) {
    LOGV("+%s",__FUNCTION__);
    bool ret = false;

    if (NULL == g_nat) {
        LOGE("%s called with no D-Bus connection!", __FUNCTION__);
        LOGV("-%s",__FUNCTION__);
        return ret;
    }

    if (server_path) {
        DBusMessage *reply = dbus_func_args(NULL, g_nat->conn,
                BM3_DBUS_RFCOMM_SVC,
                server_path,
                BM3_DBUS_RFCOMM_SVR_IFC,
                BM3_DBUS_RFCOMM_SVR_DEREGISTER,
                DBUS_TYPE_INVALID);

        if (NULL != reply) {
            dbus_message_unref(reply);
            ret = true;
        }
    }

    LOGV("-%s",__FUNCTION__);
    return ret;
}

// Helper function to retrieve and open the RFCOMM socket
//
// session_path - valid path to com.qualcomm.rfcommSession-providing object (i.e., dbus_bt)
// channel - desired RFCOMM channel (1-30), or 0 for don't care
//
// Returns fd of open socket, -1 for error
static int connectRfcommSocket(const char* session_path, uint32_t channel) {
    int sock_ret = -1;

    if (NULL == g_nat) {
        LOGE("%s called with no D-Bus connection!", __FUNCTION__);
        return sock_ret;
    }

    if (NULL == session_path) {
        LOGE("NULL session_path passed to connectRfcommSocket!");
        return sock_ret;
    }

    // Retrieve socketPath, socketKey, channel from the session
    uint32_t socket_key = 0;
    char* socket_path = NULL;
    uint32_t connected_channel = 0;
    DBusMessage *reply = NULL;

    reply = dbus_func_args(NULL, g_nat->conn,
            BM3_DBUS_RFCOMM_SVC,
            session_path,
            BM3_DBUS_RFCOMM_SESSION_IFC,
            BM3_DBUS_RFCOMM_SESSION_GET_PROPS,
            DBUS_TYPE_INVALID);

    if (NULL == reply) {
        LOGE("Could not retrieve RFCOMM session properties!");
        return sock_ret;
    } else {
        DBusMessageIter iter;
        DBusMessageIter dict_entry, dict;
        property_value value;
        int len = 0;
        int prop_index = -1;

        dbus_message_iter_init(reply, &iter);

        if(DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&iter)) {
            dbus_message_iter_recurse(&iter, &dict);
            do {
                len = 0;
                if (DBUS_TYPE_DICT_ENTRY != dbus_message_iter_get_arg_type(&dict)) {
                    break;
                }
                dbus_message_iter_recurse(&dict, &dict_entry);

                // TODO: un-hardcode these index numbers
                if (!get_property(dict_entry, session_properties, 3, &prop_index,
                                  &value, &len)) {
                    switch (prop_index) {
                        case 0: // channel
                            connected_channel = value.int_val;
                            break;
                        case 1: // socketPath
                            socket_path = (char*) malloc(strlen(value.str_val) + 1);
                            if (NULL == socket_path) {
                                LOGE("Could not allocate memory to store RFCOMM socket path.");
                                return sock_ret;
                            }
                            strcpy(socket_path, value.str_val);
                            break;
                        case 2: // socketKey
                            socket_key = value.int_val;
                            break;
                        default:
                            LOGE("Mismatch in session_properties and property indices detected!  Ignoring.");
                            break;
                    }
                }
            } while(dbus_message_iter_next(&dict));
        }

        dbus_message_unref(reply);
        reply = NULL;
    }

    // Verify that RFCOMM connected channel matches expectation, if provided
    if ((channel > 0) && (channel <= 30) && (channel != connected_channel)) {
        LOGE("RFCOMM connected to channel %d, but wanted channel %d!  Closing.",
                connected_channel, channel);

        reply = dbus_func_args(NULL, g_nat->conn,
                BM3_DBUS_RFCOMM_SVC,
                session_path,
                BM3_DBUS_RFCOMM_SESSION_IFC,
                BM3_DBUS_RFCOMM_SESSION_DISCONNECT,
                DBUS_TYPE_INVALID);

        if (NULL != reply) {
            dbus_message_unref(reply);
        }
    } else {
        // Open the UNIX domain socket and provide the socket key
        sock_ret = socket_local_client(socket_path, ANDROID_SOCKET_NAMESPACE_FILESYSTEM,
                SOCK_STREAM);
        if (sock_ret >= 0) {
            uint32_t socket_key_net = htonl(socket_key);
            if (sizeof(socket_key_net) != write(sock_ret, &socket_key_net, sizeof(socket_key_net)) ) {
                LOGE("Could not write socket key to dbus_bt RFCOMM socket! Closing.");
                close(sock_ret);
                sock_ret = -1;
            }
        }
    }

    free(socket_path);
    return sock_ret;
}

int connectBluetoothSocket(const char* remote_addr, uint32_t channel, uint32_t security) {
    LOGV("+%s",__FUNCTION__);
    bool result = false;
    int sock_ret = -1;

    DBusMessageIter iter, dict;
    DBusMessage *reply = NULL;
    DBusMessage *msg = NULL;
    DBusError err;
    dbus_error_init(&err);
    const char* temp_path = NULL;
    const char* channel_str = "channel";
    const char* security_str = "security";

    DBusMessageIter dict_entry, variant;

    if (NULL == g_nat) {
        LOGE("%s called with no D-Bus connection!", __FUNCTION__);
        goto done;
    }

    LOGV("Connecting BM3 BluetoothSocket bd_addr %s channel %d security %d", remote_addr, channel, security);
    msg = dbus_message_new_method_call(BM3_DBUS_RFCOMM_SVC,
                                       BM3_DBUS_RFCOMM_PATH,
                                       BM3_DBUS_RFCOMM_MGR_IFC,
                                       BM3_DBUS_RFCOMM_MGR_CONNECT);

    if (msg == NULL)
    {
      LOGE("Unable to allocate new D-Bus message %s!", BM3_DBUS_RFCOMM_MGR_CONNECT);
      goto done;
    }

    dbus_message_iter_init_append(msg, &iter);

    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &remote_addr)) {
         LOGE("Could not append remote bd_addr %s to Connect call!", remote_addr);
         goto done;
    }

    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                          DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                          DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
                                          DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict)) {
        LOGE("Could not open D-Bus container!");
        goto done;
    }

    // add key: channel
    if (!dbus_append_variant_dict_entry(&dict, channel_str, DBUS_TYPE_UINT32, &channel)) {
        LOGE("Could not add channel to property dict.");
        goto done;
    }

    // add key: security
    if (!dbus_append_variant_dict_entry(&dict, security_str, DBUS_TYPE_UINT32, &security)) {
        LOGE("Could not add security to property dict.");
        goto done;
    }

    dbus_message_iter_close_container(&iter, &dict);
    LOGI("Closed dictionary entry container");

    reply = dbus_connection_send_with_reply_and_block(g_nat->conn,
                                                      msg,
                                                      10*1000,
                                                      &err);
    if (NULL == reply) {
        result = false;
        LOG_AND_FREE_DBUS_ERROR(&err);
    } else {
        if (dbus_message_get_args(reply, &err,
                                   DBUS_TYPE_OBJECT_PATH, &temp_path,
                                   DBUS_TYPE_INVALID)) {
            result = true;
        } else {
            LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, reply);
        }
        dbus_message_unref(reply);
    }

done:
    if (NULL != msg) {
        dbus_message_unref(msg);
    }

    if (result) {
        sock_ret = connectRfcommSocket(temp_path, channel);
    }

    if (sock_ret < 0) {
        LOGE("... BM3 BluetoothSocket Connect: FAILURE!");
    } else {
        LOGV("... BM3 BluetoothSocket Connect: SUCCESS.");
    }

    LOGV("-%s",__FUNCTION__);
    return sock_ret;
}

int acceptConnectionBluetoothSocketServer(const char* server_path, const char* remote_addr, dbus_bool_t accept) {
    LOGV("+%s",__FUNCTION__);
    int sock_ret = -1;

    if (NULL == g_nat) {
        LOGE("%s called with no D-Bus connection!", __FUNCTION__);
        return sock_ret;
    }

    DBusError err;
    dbus_error_init(&err);

    if (server_path) {
        DBusMessage *reply = dbus_func_args(NULL, g_nat->conn,
                BM3_DBUS_RFCOMM_SVC,
                server_path,
                BM3_DBUS_RFCOMM_SVR_IFC,
                BM3_DBUS_RFCOMM_SVR_ACCEPT,
                DBUS_TYPE_STRING, &remote_addr,
                DBUS_TYPE_BOOLEAN, &accept,
                DBUS_TYPE_INVALID);

        if (NULL == reply) {
             LOG_AND_FREE_DBUS_ERROR(&err);
        } else {
            const char* temp_path = NULL;
            if (dbus_message_get_args(reply, &err,
                                DBUS_TYPE_OBJECT_PATH, &temp_path,
                                DBUS_TYPE_INVALID)) {
                sock_ret = connectRfcommSocket(temp_path, 0 /* don't care */);
            } else {
                LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, reply);
            }
            dbus_message_unref(reply);
        }
    }

    LOGV("-%s",__FUNCTION__);
    return sock_ret;
}

void cleanupBluetoothSocketNativeData() {
    LOGV("+%s",__FUNCTION__);

    if (g_nat) {
        pthread_mutex_destroy(&(g_nat->thread_mutex));
        free(g_nat);
        g_nat = NULL;
    }

    LOGV("-%s",__FUNCTION__);
}

static DBusHandlerResult agent_event_filter(DBusConnection *conn,
                                            DBusMessage *msg,
                                            void *data);

static const DBusObjectPathVTable agent_vtable = {
    NULL, agent_event_filter, NULL, NULL, NULL, NULL
};

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

    if (nat != NULL && nat->conn != NULL) {
        const char* agent_path = BLUETOOTH_SOCKET_AGENT_PATH;
        if (!dbus_connection_register_object_path(nat->conn, agent_path,
                &agent_vtable, nat)) {
            LOGE("%s: Can't register object path %s for agent!",
                  __FUNCTION__, agent_path);
            return JNI_FALSE;
        }

        return JNI_TRUE;
    }

    return JNI_FALSE;
}

static void tearDownEventLoop(native_data_t *nat) {
    LOGV(__FUNCTION__);
    if (nat != NULL && nat->conn != NULL) {
        const char* agent_path = BLUETOOTH_SOCKET_AGENT_PATH;
        dbus_connection_unregister_object_path(nat->conn, agent_path);
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
            LOGV("BluetoothSocket Event Loop fd i=%d",i);
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
                LOGV("Bluetooth Socket Event Loop dbus_watch_handle");
                dbus_watch_handle(nat->watchData[i], flags);
                nat->pollData[i].revents = 0;
            }
        }
        LOGV("Bluetooth Socket Event Loop dbus_connection_dispatch.");
        while (dbus_connection_dispatch(nat->conn) ==
                DBUS_DISPATCH_DATA_REMAINS) {
            LOGI("Bluetooth Socket Event Loop dbus_connection_dispatch again.");
        }

        LOGV("Bluetooth Socket Event Loop entering poll");
        poll(nat->pollData, nat->pollMemberCount, -1);
        LOGV("Bluetooth Socket Event Loop leaving poll");
    }
}

bool startBluetoothSocketEventLoop() {
    bool result = false;

    if (NULL == g_nat) {
        LOGE("%s called with no D-Bus connection!", __FUNCTION__);
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

void stopBluetoothSocketEventLoop() {
    if (NULL == g_nat) {
        LOGE("%s called with no D-Bus connection!", __FUNCTION__);
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

static DBusHandlerResult agent_event_filter(DBusConnection *conn,
                                            DBusMessage *msg, void *data) {
    native_data_t *nat = (native_data_t *)data;
    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        LOGV("%s: not interested (not a method call).", __FUNCTION__);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    LOGI("%s: Received method %s:%s", __FUNCTION__,
         dbus_message_get_interface(msg), dbus_message_get_member(msg));

    if (nat == NULL) return DBUS_HANDLER_RESULT_HANDLED;

    DBusError err;
    dbus_error_init(&err);
    if (dbus_message_is_method_call(msg,
            BM3_DBUS_RFCOMM_SRV_AGENT_IFC, BM3_DBUS_RFCOMM_SRV_AGENT_INCOMING)) {
        const char *msg_server_path;
        const char *c_address;
        if (dbus_message_get_args(msg, &err,
                                  DBUS_TYPE_OBJECT_PATH, &msg_server_path,
                                  DBUS_TYPE_STRING, &c_address,
                                  DBUS_TYPE_INVALID)) {
            LOGV("... server = %s address = %s", msg_server_path, c_address);

            /* Match the request using the server_path */
            pthread_mutex_lock(&g_server_sockets_mutex);
            int server_socket_idx = findServerSocketNativeData(msg_server_path);

            if (server_socket_idx < 0) {
                LOGE("Received incoming BluetoothSocket connection for unknown server.  Dropping.");
                pthread_mutex_unlock(&g_server_sockets_mutex);
                goto failure;
            }

            pthread_mutex_t* accept_mutex = &(g_server_sockets[server_socket_idx].is_connected_mutex);
            pthread_cond_t* accept_cond = &(g_server_sockets[server_socket_idx].is_connected_cond);
            pthread_mutex_unlock(&g_server_sockets_mutex);

            if (0 == pthread_mutex_lock(accept_mutex)) {
                /* Populate the remote device address */
                strcpy(g_server_sockets[server_socket_idx].remote_addr, c_address);

                if (0 != pthread_cond_signal(accept_cond)) {
                    LOGE("Error signaling incoming BluetoothSocket connection.");
                    pthread_mutex_unlock(accept_mutex);
                    goto failure;
                }

                pthread_mutex_unlock(accept_mutex);
                sched_yield();
            } else {
                LOGE("Incoming BluetoothScoket connection mutex lock failure!");
                goto failure;
            }
        } else {
            LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, msg);
            goto failure;
        }
        goto success;
    } else if (dbus_message_is_method_call(msg,
            BM3_DBUS_RFCOMM_SRV_AGENT_IFC, BM3_DBUS_RFCOMM_SRV_AGENT_RELEASE)) {
        const char *c_server;
        if (dbus_message_get_args(msg, &err,
                                  DBUS_TYPE_OBJECT_PATH, &c_server,
                                  DBUS_TYPE_INVALID)) {
            LOGV("... server = %s", c_server);
            // *****************
            // TODO: something here
        } else {
            LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, msg);
            goto failure;
        }
        goto success;
    } else {
        LOGV("%s:%s is ignored", dbus_message_get_interface(msg), dbus_message_get_member(msg));
    }

failure:
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

success:
    return DBUS_HANDLER_RESULT_HANDLED;
}

} /* namespace android */
#endif /* USE_BM3_BLUETOOTH */
