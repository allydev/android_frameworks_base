/*
** Copyright 2006, The Android Open Source Project
** Copyright (c) 2009, Code Aurora Forum. All rights reserved.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "BluetoothAudioGateway.cpp"

#include "android_bluetooth_common.h"
#include "android_runtime/AndroidRuntime.h"
#ifdef USE_BM3_BLUETOOTH
#include "HeadsetHandsfreeEventLoop.h"
#endif
#include "JNIHelp.h"
#include "jni.h"
#include "utils/Log.h"
#include "utils/misc.h"

#define USE_ACCEPT_DIRECTLY (0)
#define USE_SELECT (0) /* 1 for select(), 0 for poll(); used only when
                          USE_ACCEPT_DIRECTLY == 0 */

#ifdef USE_BM3_BLUETOOTH
#include <pthread.h>
#include <time.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <ctype.h>

#if USE_SELECT
#include <sys/select.h>
#else
#include <sys/poll.h>
#endif

#ifdef HAVE_BLUETOOTH
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sco.h>
#endif

namespace android {

#ifdef HAVE_BLUETOOTH
static jfieldID field_mNativeData;
    /* in */
static jfieldID field_mHandsfreeAgRfcommChannel;
static jfieldID field_mHeadsetAgRfcommChannel;
    /* out */
static jfieldID field_mTimeoutRemainingMs; /* out */

static jfieldID field_mConnectingHeadsetAddress;
static jfieldID field_mConnectingHeadsetRfcommChannel; /* -1 when not connected */
static jfieldID field_mConnectingHeadsetSocketFd;

static jfieldID field_mConnectingHandsfreeAddress;
static jfieldID field_mConnectingHandsfreeRfcommChannel; /* -1 when not connected */
static jfieldID field_mConnectingHandsfreeSocketFd;

#ifdef USE_BM3_BLUETOOTH
pthread_mutex_t g_incoming_rfcomm_connection_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_incoming_rfcomm_connection_cond = PTHREAD_COND_INITIALIZER;
int g_incoming_rfcomm_connection_type; /* <=0 invalid, 1 = HFP, 2 = HSP */
#endif /* USE_BM3_BLUETOOTH */

typedef struct {
    int hcidev;
    int hf_ag_rfcomm_channel;
    int hs_ag_rfcomm_channel;
    int hf_ag_rfcomm_sock;
    int hs_ag_rfcomm_sock;
} native_data_t;

static inline native_data_t * get_native_data(JNIEnv *env, jobject object) {
    return (native_data_t *)(env->GetIntField(object,
                                                 field_mNativeData));
}

static int setup_listening_socket(int dev, int channel);
#endif

static void classInitNative(JNIEnv* env, jclass clazz) {
    LOGV(__FUNCTION__);
#ifdef HAVE_BLUETOOTH

    /* in */
    field_mNativeData = get_field(env, clazz, "mNativeData", "I");
    field_mHandsfreeAgRfcommChannel =
        get_field(env, clazz, "mHandsfreeAgRfcommChannel", "I");
    field_mHeadsetAgRfcommChannel =
        get_field(env, clazz, "mHeadsetAgRfcommChannel", "I");

    /* out */
    field_mConnectingHeadsetAddress =
        get_field(env, clazz,
                  "mConnectingHeadsetAddress", "Ljava/lang/String;");
    field_mConnectingHeadsetRfcommChannel =
        get_field(env, clazz, "mConnectingHeadsetRfcommChannel", "I");
    field_mConnectingHeadsetSocketFd =
        get_field(env, clazz, "mConnectingHeadsetSocketFd", "I");

    field_mConnectingHandsfreeAddress =
        get_field(env, clazz,
                  "mConnectingHandsfreeAddress", "Ljava/lang/String;");
    field_mConnectingHandsfreeRfcommChannel =
        get_field(env, clazz, "mConnectingHandsfreeRfcommChannel", "I");
    field_mConnectingHandsfreeSocketFd =
        get_field(env, clazz, "mConnectingHandsfreeSocketFd", "I");

    field_mTimeoutRemainingMs =
        get_field(env, clazz, "mTimeoutRemainingMs", "I");
#endif
}

static void initializeNativeDataNative(JNIEnv* env, jobject object) {
    LOGV(__FUNCTION__);
#ifdef HAVE_BLUETOOTH
    native_data_t *nat = (native_data_t *)calloc(1, sizeof(native_data_t));
    if (NULL == nat) {
        LOGE("%s: out of memory!", __FUNCTION__);
        return;
    }

    nat->hcidev = BLUETOOTH_ADAPTER_HCI_NUM;

    env->SetIntField(object, field_mNativeData, (jint)nat);
    nat->hf_ag_rfcomm_channel =
        env->GetIntField(object, field_mHandsfreeAgRfcommChannel);
    nat->hs_ag_rfcomm_channel =
        env->GetIntField(object, field_mHeadsetAgRfcommChannel);
    LOGV("HF RFCOMM channel = %d.", nat->hf_ag_rfcomm_channel);
    LOGV("HS RFCOMM channel = %d.", nat->hs_ag_rfcomm_channel);

    /* Set the default values of these to -1. */
    env->SetIntField(object, field_mConnectingHeadsetRfcommChannel, -1);
    env->SetIntField(object, field_mConnectingHandsfreeRfcommChannel, -1);

    nat->hf_ag_rfcomm_sock = -1;
    nat->hs_ag_rfcomm_sock = -1;

#ifdef USE_BM3_BLUETOOTH
    g_incoming_rfcomm_connection_type = 0;
#endif
#endif
}

static void cleanupNativeDataNative(JNIEnv* env, jobject object) {
    LOGV(__FUNCTION__);
#ifdef HAVE_BLUETOOTH
    native_data_t *nat = get_native_data(env, object);
    if (nat) {
        free(nat);
    }
#endif
}

#ifdef HAVE_BLUETOOTH

#if USE_ACCEPT_DIRECTLY==0
static int set_nb(int sk, bool nb) {
    int flags = fcntl(sk, F_GETFL);
    if (flags < 0) {
        LOGE("Can't get socket flags with fcntl(): %s (%d)",
             strerror(errno), errno);
        close(sk);
        return -1;
    }
    flags &= ~O_NONBLOCK;
    if (nb) flags |= O_NONBLOCK;
    int status = fcntl(sk, F_SETFL, flags);
    if (status < 0) {
        LOGE("Can't set socket to nonblocking mode with fcntl(): %s (%d)",
             strerror(errno), errno);
        close(sk);
        return -1;
    }
    return 0;
}
#endif /*USE_ACCEPT_DIRECTLY==0*/

static int do_accept(JNIEnv* env, jobject object, int ag_fd,
                     jfieldID out_fd,
                     jfieldID out_address,
                     jfieldID out_channel) {
#ifdef USE_BM3_BLUETOOTH
    if (/*HFP*/1 == ag_fd) {
        acceptIncomingConnection(BM3_DBUS_HSHF_HFAG_PROFILE_PATH);
    } else { /* HSP */
        acceptIncomingConnection(BM3_DBUS_HSHF_HSAG_PROFILE_PATH);
    }

    env->SetIntField(object, out_fd, ag_fd);
    env->SetIntField(object, out_channel, ag_fd);
    env->SetObjectField(object, out_address, env->NewStringUTF(g_incoming_rfcomm_remote_addr));

    LOGI("Successful accept() on AG fd %d, address %s.",
         ag_fd,
         g_incoming_rfcomm_remote_addr);
    return 0;
#else
#if USE_ACCEPT_DIRECTLY==0
    if (set_nb(ag_fd, true) < 0)
        return -1;
#endif

    struct sockaddr_rc raddr;
    int alen = sizeof(raddr);
    int nsk = accept(ag_fd, (struct sockaddr *) &raddr, &alen);
    if (nsk < 0) {
        LOGE("Error on accept from socket fd %d: %s (%d).",
             ag_fd,
             strerror(errno),
             errno);
#if USE_ACCEPT_DIRECTLY==0
        set_nb(ag_fd, false);
#endif
        return -1;
    }

    env->SetIntField(object, out_fd, nsk);
    env->SetIntField(object, out_channel, raddr.rc_channel);

    char addr[BTADDR_SIZE];
    get_bdaddr_as_string(&raddr.rc_bdaddr, addr);
    env->SetObjectField(object, out_address, env->NewStringUTF(addr));

    LOGI("Successful accept() on AG socket %d: new socket %d, address %s, RFCOMM channel %d",
         ag_fd,
         nsk,
         addr,
         raddr.rc_channel);
#if USE_ACCEPT_DIRECTLY==0
    set_nb(ag_fd, false);
#endif
    return 0;
#endif /* USE_BM3_BLUETOOTH */
}

#if USE_SELECT
static inline int on_accept_set_fields(JNIEnv* env, jobject object,
                                       fd_set *rset, int ag_fd,
                                       jfieldID out_fd,
                                       jfieldID out_address,
                                       jfieldID out_channel) {

    env->SetIntField(object, out_channel, -1);

    if (ag_fd >= 0 && FD_ISSET(ag_fd, &rset)) {
        return do_accept(env, object, ag_fd,
                         out_fd, out_address, out_channel);
    }
    else {
        LOGI("fd = %d, FD_ISSET() = %d",
             ag_fd,
             FD_ISSET(ag_fd, &rset));
        if (ag_fd >= 0 && !FD_ISSET(ag_fd, &rset)) {
            LOGE("WTF???");
            return -1;
        }
    }

    return 0;
}
#endif
#endif /* HAVE_BLUETOOTH */

static jboolean waitForHandsfreeConnectNative(JNIEnv* env, jobject object,
                                              jint timeout_ms) {
    LOGV(__FUNCTION__);
#ifdef HAVE_BLUETOOTH

    env->SetIntField(object, field_mTimeoutRemainingMs, timeout_ms);

    int n = 0;
    native_data_t *nat = get_native_data(env, object);
#if USE_BM3_BLUETOOTH
    LOGV("Blocking for incoming HSP/HFP RFCOMM connection.");

    /* wait up to timeout_ms for HF or HS connection */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms/1000;
    ts.tv_nsec += (timeout_ms%1000)*1000000;
    if (ts.tv_nsec > 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    /* HACK: just setting remaining time to 0--we don't want upper-level thread sleeping! */
    env->SetIntField(object, field_mTimeoutRemainingMs, 0);

    if (0 == pthread_mutex_lock(&g_incoming_rfcomm_connection_mutex)) {
        while (g_incoming_rfcomm_connection_type <= 0) {
            int status;
            status = pthread_cond_timedwait(&g_incoming_rfcomm_connection_cond, &g_incoming_rfcomm_connection_mutex, &ts);

            if (status == ETIMEDOUT) {
                LOGV("No incoming HSP/HFP RFCOMM connection (timeout).");
                pthread_mutex_unlock(&g_incoming_rfcomm_connection_mutex);
                return JNI_FALSE;
            } else if (status != 0) {
                LOGE("No incoming HSP/HFP RFCOMM connection (error).");
                pthread_mutex_unlock(&g_incoming_rfcomm_connection_mutex);
                return JNI_FALSE;
            }
        }

        LOGV("Unblocked for incoming HSP/HFP RFCOMM connection.");
        if (g_incoming_rfcomm_connection_type == 1 /* HFP */) {
            LOGI("Accepting HF connection.");
            do_accept(env, object, g_incoming_rfcomm_connection_type,
                      field_mConnectingHandsfreeSocketFd,
                      field_mConnectingHandsfreeAddress,
                      field_mConnectingHandsfreeRfcommChannel);
        } else {
            LOGI("Accepting HS connection.");
            do_accept(env, object, g_incoming_rfcomm_connection_type,
                      field_mConnectingHeadsetSocketFd,
                      field_mConnectingHeadsetAddress,
                      field_mConnectingHeadsetRfcommChannel);
        }

        /* Reset the connection RFCOMM type... */
        g_incoming_rfcomm_connection_type = 0;
        pthread_mutex_unlock(&g_incoming_rfcomm_connection_mutex);
    } else {
        LOGE("Incoming RFCOMM connection mutex lock failure!");
        return JNI_FALSE;
    }

    return JNI_TRUE;
#else
#if USE_ACCEPT_DIRECTLY
    if (nat->hf_ag_rfcomm_channel > 0) {
        LOGI("Setting HF AG server socket to RFCOMM port %d!",
             nat->hf_ag_rfcomm_channel);
        struct timeval tv;
        int len = sizeof(tv);
        if (getsockopt(nat->hf_ag_rfcomm_channel,
                       SOL_SOCKET, SO_RCVTIMEO, &tv, &len) < 0) {
            LOGE("getsockopt(%d, SOL_SOCKET, SO_RCVTIMEO): %s (%d)",
                 nat->hf_ag_rfcomm_channel,
                 strerror(errno),
                 errno);
            return JNI_FALSE;
        }
        LOGI("Current HF AG server socket RCVTIMEO is (%d(s), %d(us))!",
             (int)tv.tv_sec, (int)tv.tv_usec);
        if (timeout_ms >= 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = 1000 * (timeout_ms % 1000);
            if (setsockopt(nat->hf_ag_rfcomm_channel,
                           SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
                LOGE("setsockopt(%d, SOL_SOCKET, SO_RCVTIMEO): %s (%d)",
                     nat->hf_ag_rfcomm_channel,
                     strerror(errno),
                     errno);
                return JNI_FALSE;
            }
            LOGI("Changed HF AG server socket RCVTIMEO to (%d(s), %d(us))!",
                 (int)tv.tv_sec, (int)tv.tv_usec);
        }

        if (!do_accept(env, object, nat->hf_ag_rfcomm_sock,
                       field_mConnectingHandsfreeSocketFd,
                       field_mConnectingHandsfreeAddress,
                       field_mConnectingHandsfreeRfcommChannel))
        {
            env->SetIntField(object, field_mTimeoutRemainingMs, 0);
            return JNI_TRUE;
        }
        return JNI_FALSE;
    }
#else
#if USE_SELECT
    fd_set rset;
    FD_ZERO(&rset);
    int cnt = 0;
    if (nat->hf_ag_rfcomm_channel > 0) {
        LOGI("Setting HF AG server socket to RFCOMM port %d!",
             nat->hf_ag_rfcomm_channel);
        cnt++;
        FD_SET(nat->hf_ag_rfcomm_sock, &rset);
    }
    if (nat->hs_ag_rfcomm_channel > 0) {
        LOGI("Setting HS AG server socket to RFCOMM port %d!",
             nat->hs_ag_rfcomm_channel);
        cnt++;
        FD_SET(nat->hs_ag_rfcomm_sock, &rset);
    }
    if (cnt == 0) {
        LOGE("Neither HF nor HS listening sockets are open!");
        return JNI_FALSE;
    }

    struct timeval to;
    if (timeout_ms >= 0) {
        to.tv_sec = timeout_ms / 1000;
        to.tv_usec = 1000 * (timeout_ms % 1000);
    }
    n = select(MAX(nat->hf_ag_rfcomm_sock,
                       nat->hs_ag_rfcomm_sock) + 1,
                   &rset,
                   NULL,
                   NULL,
                   (timeout_ms < 0 ? NULL : &to));
    if (timeout_ms > 0) {
        jint remaining = to.tv_sec*1000 + to.tv_usec/1000;
        LOGI("Remaining time %ldms", (long)remaining);
        env->SetIntField(object, field_mTimeoutRemainingMs,
                         remaining);
    }

    LOGI("listening select() returned %d", n);

    if (n <= 0) {
        if (n < 0)  {
            LOGE("listening select() on RFCOMM sockets: %s (%d)",
                 strerror(errno),
                 errno);
        }
        return JNI_FALSE;
    }

    n = on_accept_set_fields(env, object,
                             &rset, nat->hf_ag_rfcomm_sock,
                             field_mConnectingHandsfreeSocketFd,
                             field_mConnectingHandsfreeAddress,
                             field_mConnectingHandsfreeRfcommChannel);

    n += on_accept_set_fields(env, object,
                              &rset, nat->hs_ag_rfcomm_sock,
                              field_mConnectingHeadsetSocketFd,
                              field_mConnectingHeadsetAddress,
                              field_mConnectingHeadsetRfcommChannel);

    return !n ? JNI_TRUE : JNI_FALSE;
#else
    struct pollfd fds[2];
    int cnt = 0;
    if (nat->hf_ag_rfcomm_channel > 0) {
//        LOGI("Setting HF AG server socket %d to RFCOMM port %d!",
//             nat->hf_ag_rfcomm_sock,
//             nat->hf_ag_rfcomm_channel);
        fds[cnt].fd = nat->hf_ag_rfcomm_sock;
        fds[cnt].events = POLLIN | POLLPRI | POLLOUT | POLLERR;
        cnt++;
    }
    if (nat->hs_ag_rfcomm_channel > 0) {
//        LOGI("Setting HS AG server socket %d to RFCOMM port %d!",
//             nat->hs_ag_rfcomm_sock,
//             nat->hs_ag_rfcomm_channel);
        fds[cnt].fd = nat->hs_ag_rfcomm_sock;
        fds[cnt].events = POLLIN | POLLPRI | POLLOUT | POLLERR;
        cnt++;
    }
    if (cnt == 0) {
        LOGE("Neither HF nor HS listening sockets are open!");
        return JNI_FALSE;
    }
    n = poll(fds, cnt, timeout_ms);
    if (n <= 0) {
        if (n < 0)  {
            LOGE("listening poll() on RFCOMM sockets: %s (%d)",
                 strerror(errno),
                 errno);
        }
        else {
            env->SetIntField(object, field_mTimeoutRemainingMs, 0);
//            LOGI("listening poll() on RFCOMM socket timed out");
        }
        return JNI_FALSE;
    }

    //LOGI("listening poll() on RFCOMM socket returned %d", n);
    int err = 0;
    for (cnt = 0; cnt < (int)(sizeof(fds)/sizeof(fds[0])); cnt++) {
        //LOGI("Poll on fd %d revent = %d.", fds[cnt].fd, fds[cnt].revents);
        if (fds[cnt].fd == nat->hf_ag_rfcomm_sock) {
            if (fds[cnt].revents & (POLLIN | POLLPRI | POLLOUT)) {
                LOGI("Accepting HF connection.");
                err += do_accept(env, object, fds[cnt].fd,
                               field_mConnectingHandsfreeSocketFd,
                               field_mConnectingHandsfreeAddress,
                               field_mConnectingHandsfreeRfcommChannel);
                n--;
            }
        }
        else if (fds[cnt].fd == nat->hs_ag_rfcomm_sock) {
            if (fds[cnt].revents & (POLLIN | POLLPRI | POLLOUT)) {
                LOGI("Accepting HS connection.");
                err += do_accept(env, object, fds[cnt].fd,
                               field_mConnectingHeadsetSocketFd,
                               field_mConnectingHeadsetAddress,
                               field_mConnectingHeadsetRfcommChannel);
                n--;
            }
        }
    } /* for */

    if (n != 0) {
        LOGI("Bogus poll(): %d fake pollfd entrie(s)!", n);
        return JNI_FALSE;
    }

    return !err ? JNI_TRUE : JNI_FALSE;
#endif /* USE_SELECT */
#endif /* USE_ACCEPT_DIRECTLY */
#endif /* USE_BM3_BLUETOOTH */
#else
    return JNI_FALSE;
#endif /* HAVE_BLUETOOTH */
}

static jboolean setUpListeningSocketsNative(JNIEnv* env, jobject object) {
    LOGV(__FUNCTION__);
#ifdef HAVE_BLUETOOTH
    native_data_t *nat = get_native_data(env, object);

#ifdef USE_BM3_BLUETOOTH
    LOGI("Initializing HS/HF D-Bus state.");
    if (false == initializeHsHfNativeData()) {
        LOGE("Could not initialize HS/HF D-Bus state!");
        return JNI_FALSE;
    }

    LOGI("Registering for HS/HF profiles with BM3.");
    if (false == registerHsHfProfiles()) {
        LOGE("Could not register for HS/HF profiles with BM3.");
        return JNI_FALSE;
    }

    LOGI("Starting HS/HF D-Bus event processing loop.");
    if (false == startHsHfEventLoop()) {
        LOGE("Could not start HS/HF D-Bus event processing loop!");
        return JNI_FALSE;
    }
#endif /* USE_BM3_BLUETOOTH */

    nat->hf_ag_rfcomm_sock =
        setup_listening_socket(nat->hcidev, nat->hf_ag_rfcomm_channel);
    if (nat->hf_ag_rfcomm_sock < 0)
        return JNI_FALSE;

    nat->hs_ag_rfcomm_sock =
        setup_listening_socket(nat->hcidev, nat->hs_ag_rfcomm_channel);
    if (nat->hs_ag_rfcomm_sock < 0) {
        close(nat->hf_ag_rfcomm_sock);
        nat->hf_ag_rfcomm_sock = -1;
        return JNI_FALSE;
    }

    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif /* HAVE_BLUETOOTH */
}

#ifdef HAVE_BLUETOOTH
static int setup_listening_socket(int dev, int channel) {
    struct sockaddr_rc laddr;
    int sk, lm;

    sk = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (sk < 0) {
        LOGE("Can't create RFCOMM socket");
        return -1;
    }

    if (debug_no_encrypt()) {
        lm = RFCOMM_LM_AUTH;
    } else {
        lm = RFCOMM_LM_AUTH | RFCOMM_LM_ENCRYPT;
    }

	if (lm && setsockopt(sk, SOL_RFCOMM, RFCOMM_LM, &lm, sizeof(lm)) < 0) {
		LOGE("Can't set RFCOMM link mode");
		close(sk);
		return -1;
	}

    laddr.rc_family = AF_BLUETOOTH;
    bacpy(&laddr.rc_bdaddr, BDADDR_ANY);
    laddr.rc_channel = channel;

	if (bind(sk, (struct sockaddr *)&laddr, sizeof(laddr)) < 0) {
		LOGE("Can't bind RFCOMM socket");
		close(sk);
		return -1;
	}

    listen(sk, 10);
    return sk;
}
#endif /* HAVE_BLUETOOTH */

/*
    private native void tearDownListeningSocketsNative();
*/
static void tearDownListeningSocketsNative(JNIEnv *env, jobject object) {
    LOGV(__FUNCTION__);
#ifdef HAVE_BLUETOOTH
    native_data_t *nat = get_native_data(env, object);

    if (nat->hf_ag_rfcomm_sock > 0) {
        if (close(nat->hf_ag_rfcomm_sock) < 0) {
            LOGE("Could not close HF server socket: %s (%d)",
                 strerror(errno), errno);
        }
        nat->hf_ag_rfcomm_sock = -1;
    }
    if (nat->hs_ag_rfcomm_sock > 0) {
        if (close(nat->hs_ag_rfcomm_sock) < 0) {
            LOGE("Could not close HS server socket: %s (%d)",
                 strerror(errno), errno);
        }
        nat->hs_ag_rfcomm_sock = -1;
    }

#ifdef USE_BM3_BLUETOOTH
    LOGI("Stopping HS/HF D-Bus event processing loop.");
    stopHsHfEventLoop();
    cleanupHsHfNativeData();
#endif /* USE_BM3_BLUETOOTH */

#endif /* HAVE_BLUETOOTH */
}

static JNINativeMethod sMethods[] = {
     /* name, signature, funcPtr */

    {"classInitNative", "()V", (void*)classInitNative},
    {"initializeNativeDataNative", "()V", (void *)initializeNativeDataNative},
    {"cleanupNativeDataNative", "()V", (void *)cleanupNativeDataNative},

    {"setUpListeningSocketsNative", "()Z", (void *)setUpListeningSocketsNative},
    {"tearDownListeningSocketsNative", "()V", (void *)tearDownListeningSocketsNative},
    {"waitForHandsfreeConnectNative", "(I)Z", (void *)waitForHandsfreeConnectNative},
};

int register_android_bluetooth_BluetoothAudioGateway(JNIEnv *env) {
    return AndroidRuntime::registerNativeMethods(env,
            "android/bluetooth/BluetoothAudioGateway", sMethods,
            NELEM(sMethods));
}

} /* namespace android */
