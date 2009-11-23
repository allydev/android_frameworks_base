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

#ifdef USE_BM3_BLUETOOTH
#include <pthread.h>
#include "cutils/sockets.h"
#include "cutils/abort_socket.h"

namespace android {
#define BLUETOOTH_SOCKET_AGENT_PATH "/"
#define BLUETOOTH_SOCKET_MAX_SERVER_SOCKETS 30
#define BLUETOOTH_SOCKET_MAX_SERVER_PATH 50
#define BLUETOOTH_SOCKET_ADDR_SIZE 18 /* 01:23:45:67:89:ac - 17 char + NUL */

// Per server-socket connection struct
struct server_socket_native_data_t {
    bool in_use;
    pthread_mutex_t is_connected_mutex;
    pthread_cond_t is_connected_cond;
    char server_path[BLUETOOTH_SOCKET_MAX_SERVER_PATH];
    char remote_addr[BLUETOOTH_SOCKET_ADDR_SIZE];
    asocket* asock;
};

/* Bluetooth Scoket Globals */
extern pthread_mutex_t g_server_sockets_mutex;
extern server_socket_native_data_t g_server_sockets[];

/* Bluetooth Socket Function Prototypes */
bool initializeBluetoothSocketNativeData();
void cleanupBluetoothSocketNativeData();
bool startBluetoothSocketEventLoop();
void stopBluetoothSocketEventLoop();

int createServerSocketNativeData();
void deleteServerSocketNativeData(int idx);
int findServerSocketNativeData(const char* server_path);
int findServerSocketNativeData(const asocket* asock_ptr);

bool registerBluetoothSocketServer(uint32_t channel, uint32_t security, char* server_path, int server_path_len);
bool deregisterBluetoothSocketServer(const char* server_path);
int connectBluetoothSocket(const char* remote_addr, uint32_t channel, uint32_t security);
int acceptConnectionBluetoothSocketServer(const char* server_path, const char* remote_addr, dbus_bool_t accept);
} /* namespace android */
#endif /* USE_BM3_BLUETOOTH */
