/*
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.server;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.IConnectivityManager;
import android.os.Binder;
import android.os.IBinder;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.AsyncResult;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.os.RemoteException;
import android.os.Parcel;
import android.os.Bundle;
import android.os.PowerManager;
import android.os.SystemClock;
import android.telephony.PhoneNumberUtils;
import android.telephony.NeighboringCellInfo;
import android.util.Config;
import android.util.Log;
import android.provider.Settings;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.sql.Timestamp;

import android.net.LinkProvider;
import android.net.LinkRequirements;
import android.net.LinkNotifier;
import android.net.IConSvcEventListener;
import android.net.LinkInfo;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.List;
import java.util.ArrayList;

import com.android.server.BatteryService;
import android.telephony.TelephonyManager;
import android.telephony.CellLocation;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.net.wifi.WifiStateTracker;
import android.net.wifi.ScanResult;
import android.os.ServiceManager;
import com.android.internal.telephony.TelephonyIntents;
import android.net.DhcpInfo;
import android.net.NetworkInfo;
import android.net.NetworkInfo.State;
import java.io.*;
import java.util.StringTokenizer;
import java.net.InetAddress;
import java.net.NetworkInterface;


import com.android.internal.telephony.Phone;
import com.android.internal.telephony.PhoneFactory;
import android.telephony.PhoneStateListener;
import android.telephony.ServiceState;
import android.telephony.SignalStrength;
import java.net.SocketException;

import com.android.internal.telephony.gsm.GsmDataConnectionTracker;



/**
 * {@hide}
 */
class CNERequest
{
    static final String LOG_TAG = "CNEJ";

    //***** Class Variables
    static int sNextSerial = 0;
    static Object sSerialMonitor = new Object();
    private static Object sPoolSync = new Object();
    private static CNERequest sPool = null;
    private static int sPoolSize = 0;
    private static final int MAX_POOL_SIZE = 4;

    //***** Instance Variables
    int mSerial;
    int mRequest;
    Message mResult;
    Parcel mp;
    CNERequest mNext;

    /**
     * Retrieves a new CNERequest instance from the pool.
     *
     * @param request CNE_REQUEST_*
     * @param result sent when operation completes
     * @return a CNERequest instance from the pool.
     */
    static CNERequest obtain(int request) {
        CNERequest rr = null;

        synchronized(sPoolSync) {
            if (sPool != null) {
                rr = sPool;
                sPool = rr.mNext;
                rr.mNext = null;
                sPoolSize--;
            }
        }

        if (rr == null) {
            rr = new CNERequest();
        }

        synchronized(sSerialMonitor) {
            rr.mSerial = sNextSerial++;
        }
        rr.mRequest = request;
        rr.mp = Parcel.obtain();

        // first elements in any CNE Parcel
        rr.mp.writeInt(request);
        rr.mp.writeInt(rr.mSerial);

        return rr;
    }

    /**
     * Returns a CNERequest instance to the pool.
     *
     * Note: This should only be called once per use.
     */
    void release() {
        synchronized (sPoolSync) {
            if (sPoolSize < MAX_POOL_SIZE) {
                this.mNext = sPool;
                sPool = this;
                sPoolSize++;
            }
        }
    }

    private CNERequest()
    {
    }

    static void
    resetSerial()
    {
        synchronized(sSerialMonitor) {
            sNextSerial = 0;
        }
    }

    String
    serialString()
    {
        //Cheesy way to do %04d
        StringBuilder sb = new StringBuilder(8);
        String sn;

        sn = Integer.toString(mSerial);

        //sb.append("J[");
        sb.append('[');
        for (int i = 0, s = sn.length() ; i < 4 - s; i++) {
            sb.append('0');
        }

        sb.append(sn);
        sb.append(']');
        return sb.toString();
    }

    void
    onError(int error)
    {

    }
}

public final class CNE
{
    static final String LOG_TAG = "CNEJ";
    private static final boolean DBG = false;
    static final boolean CNEJ_LOGD = Config.LOGD;
    static final boolean CNEJ_LOGV = DBG ? Config.LOGD : Config.LOGV;

    //***** Instance Variables

    LocalSocket mSocket;
    HandlerThread mSenderThread;
    CNESender mSender;


    Thread mReceiverThread;
    CNEReceiver mReceiver;
    private Context mContext;
    int mRequestMessagesPending;

    ArrayList<CNERequest> mRequestsList = new ArrayList<CNERequest>();


    Object     mLastNITZTimeInfo;

    /* to do move all the constants to one file */
    //***** Events

    static final int EVENT_SEND                 = 1;
    static final int EVENT_WAKE_LOCK_TIMEOUT    = 2;

    //***** Constants

    /* CNE feature flag */
    static final String UseCne = "persist.cne.UseCne";
    static boolean isCndUp = false;
    // match with constant in CNE.cpp
    static final int CNE_MAX_COMMAND_BYTES = (8 * 1024);
    static final int RESPONSE_SOLICITED = 0;
    static final int RESPONSE_UNSOLICITED = 1;

    static final String SOCKET_NAME_CNE = "cnd";

    static final int SOCKET_OPEN_RETRY_MILLIS = 4 * 1000;


    static final int CNE_SRM_ITEM_STATUS_UNKNOWN = 65535;

    /* Different requests types - corresponding to cnd_commands.h */
    static final int CNE_REQUEST_INIT = 1;
    static final int CNE_REQUEST_REG_ROLE = 2;
    static final int CNE_REQUEST_GET_COMPATIBLE_NWS = 3;
    static final int CNE_REQUEST_CONF_NW = 4;
    static final int CNE_REQUEST_DEREG_ROLE = 5;
    static final int CNE_REQUEST_REG_NOTIFICATIONS = 6;
    static final int CNE_REQUEST_UPDATE_BATTERY_INFO = 7;
    static final int CNE_REQUEST_UPDATE_WLAN_INFO = 8;
    static final int CNE_REQUEST_UPDATE_WWAN_INFO = 9;
    static final int CNE_NOTIFY_RAT_CONNECT_STATUS = 10;
    static final int CNE_NOTIFY_DEFAULT_NW_PREF = 11;
    static final int CNE_REQUEST_UPDATE_WLAN_SCAN_RESULTS = 12;
    static final int CNE_NOTIFY_SENSOR_EVENT_CMD = 13;
    static final int CNE_REQUEST_CONFIG_IPROUTE2_CMD = 14;

    /* UNSOL Responses - corresponding to cnd_unsol_messages.h */
    static final int CNE_RESPONSE_REG_ROLE = 1;
    static final int CNE_RESPONSE_GET_BEST_NW = 2;
    static final int CNE_RESPONSE_CONFIRM_NW = 3;
    static final int CNE_RESPONSE_DEREG_ROLE = 4;
    /* UNSOL Events */
    static final int CNE_REQUEST_BRING_RAT_DOWN = 5;
    static final int CNE_REQUEST_BRING_RAT_UP = 6;
    static final int CNE_NOTIFY_MORE_PREFERED_RAT_AVAIL = 7;
    static final int CNE_NOTIFY_RAT_LOST = 8;
    static final int CNE_REQUEST_START_SCAN_WLAN = 9;
    static final int CNE_NOTIFY_INFLIGHT_STATUS = 10;

    /* RAT type - corresponding to CneRatType */
    static final int CNE_RAT_MIN = 0;
    static final int CNE_RAT_WWAN = CNE_RAT_MIN;
    static final int CNE_RAT_WLAN = 1;
    static final int CNE_RAT_ANY = 2;
    static final int CNE_RAT_NONE = 3;
    static final int CNE_RAT_MAX = 4;
    static final int CNE_RAT_INVALID = CNE_RAT_MAX;

    /* different status codes */
    public static final int STATUS_FAILURE  = 0;
    public static final int STATUS_SUCCESS = 1;

    public static final int STATUS_NOT_INFLIGHT= 0;
    public static final int STATUS_INFLIGHT = 1;

    /* */
    static final int CNE_REGID_INVALID = -1;
    static final int CNE_ROLE_INVALID = -1;
    static final int CNE_DEFAULT_CON_REGID = 0;
    static final int CNE_INVALID_PID = -1;

    static final int CNE_LINK_SATISFIED = 1;
    static final int CNE_LINK_NOT_SATISFIED = 0;

    static final int CNE_MASK_ON_LINK_AVAIL_SENT = 0x0001;
    static final int CNE_MASK_ON_BETTER_LINK_AVAIL_SENT = 0x0002;

    static final int CNE_IPROUTE2_ADD_DEFAULT = 0;
    static final int CNE_IPROUTE2_DELETE_DEFAULT = 1;
    static final int CNE_IPROUTE2_DELETE_DEFAULT_FROM_MAIN = 2;
    static final int CNE_IPROUTE2_CHANGE_DEFAULT_FROM_MAIN = 3;

    static final int CNE_NET_SUBTYPE_WLAN_B = 11;
    static final int CNE_NET_SUBTYPE_WLAN_G = 12;


    private static int mRoleRegId = 0;
    private BatteryService mBatteryService;
    private WifiManager mWifiManager;
    private TelephonyManager mTelephonyManager;
    private ConnectivityService mService;
    private int mNetworkPreference;

    private Phone mPhone;
    private SignalStrength mSignalStrength = new SignalStrength();
    ServiceState mServiceState;
    private String activeWlanIfName = null;
    private String activeWwanIfName = null;

    BroadcastReceiver mIntentReceiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            final String action = intent.getAction();

            if (action.equals(Intent.ACTION_BATTERY_CHANGED))
            {
                Log.i(LOG_TAG, "CNE received action: " + action);
                int level = intent.getIntExtra("level", 0);
                int pluginType = intent.getIntExtra("plugged", 0);
                int status = intent.getIntExtra("status", 0);
                updateBatteryStatus(level, pluginType, status);
            }
            else if (action.equals(WifiManager.RSSI_CHANGED_ACTION)) {

                Log.i(LOG_TAG, "CNE received action RSSI Changed events: " + action);
                int rssi = intent.getIntExtra(WifiManager.EXTRA_NEW_RSSI, -200);


            } else if ((action.equals(WifiManager.NETWORK_STATE_CHANGED_ACTION)) ||
                     (action.equals(WifiManager.WIFI_STATE_CHANGED_ACTION)) ){

                Log.i(LOG_TAG, "CNE received action Network/Wifi State Changed: " + action);

                if (mWifiManager != null) {
                  String ifName = null;
                  String ipAddr = null;
                  String gatewayAddr = null;
                  NetworkInfo.State networkState = NetworkInfo.State.UNKNOWN;

                  if(action.equals(WifiManager.NETWORK_STATE_CHANGED_ACTION)){
                      NetworkInfo networkInfo = (NetworkInfo)
                          intent.getParcelableExtra(WifiManager.EXTRA_NETWORK_INFO);
                      networkState = (networkInfo == null ? NetworkInfo.State.UNKNOWN :
                          networkInfo.getState());
                  }

                  if(action.equals(WifiManager.WIFI_STATE_CHANGED_ACTION)){
                    final boolean enabled = intent.getIntExtra(WifiManager.EXTRA_WIFI_STATE,
                    WifiManager.WIFI_STATE_UNKNOWN) == WifiManager.WIFI_STATE_ENABLED;
                    if(!enabled){
                        networkState = NetworkInfo.State.DISCONNECTED;
                    }else{
                        networkState = NetworkInfo.State.UNKNOWN;
                    }
                  }

                  Log.i(LOG_TAG, "CNE received action Network/Wifi State Changed"+
                                 " networkState: " + networkState);

                  if (networkState == NetworkInfo.State.CONNECTED) {
                      try {
                          DhcpInfo dhcpInfo = mWifiManager.getDhcpInfo();
                          int ipAddressInt = dhcpInfo.ipAddress;
                          int gatewayInt = dhcpInfo.gateway;

                          ipAddr = ((ipAddressInt)&0xff) + "."
                                   + ((ipAddressInt>>8)&0xff) + "."
                                   + ((ipAddressInt>>16)&0xff) + "."
                                   + ((ipAddressInt>>24)&0xff);

                          gatewayAddr = ((gatewayInt)&0xff) + "."
                                        + ((gatewayInt>>8)&0xff) + "."
                                        + ((gatewayInt>>16)&0xff) +"."
                                        + ((gatewayInt>>24)&0xff);

                          InetAddress inetAddress = InetAddress.getByName(ipAddr);
                          NetworkInterface deviceInterface
                             = NetworkInterface.getByInetAddress(inetAddress);
                          ifName = deviceInterface.getName();
                          activeWlanIfName = ifName;
                          configureIproute2(CNE_IPROUTE2_ADD_DEFAULT,
                                            ifName,
                                            ipAddr,
                                            gatewayAddr);
                      } catch (Exception e) {
                            Log.w(LOG_TAG, "CNE receiver exception", e);
                      }

                  } else if (networkState == NetworkInfo.State.DISCONNECTED) {
                     configureIproute2(CNE_IPROUTE2_DELETE_DEFAULT, activeWlanIfName,
                                       null, null);
                  }
                  WifiInfo wifiInfo = mWifiManager.getConnectionInfo();
                  String ssid = wifiInfo.getSSID();
                  int rssi = wifiInfo.getRssi();
                  String tsStr = null;
                  long currentTime = System.currentTimeMillis();
                  Timestamp ts = new Timestamp(currentTime);
                  tsStr = ts.toString();


                  Log.i(LOG_TAG, "CNE received action Network/Wifi State Changed - " +
                                "ssid= " + ssid + ",rssi=" + rssi + ",networkState=" +
                                networkState);
                  Log.i(LOG_TAG, "CNE received action Network/Wifi State Changed - ifName:" +
                                ifName + ",ipAddr=" + ipAddr + ",gateway:" + gatewayAddr +
                                ",timeStamp=" + tsStr);

                  updateWlanStatus(CNE_NET_SUBTYPE_WLAN_G,NetworkStateToInt(networkState),
                                   rssi, ssid, ipAddr, tsStr);
                  notifyRatConnectStatus(CNE_RAT_WLAN, NetworkStateToInt(networkState), ipAddr);


                } else {
                  Log.w(LOG_TAG, "CNE received action Network State Changed, null WifiManager");
                }

            } else if (action.equals(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION)) {
                Log.i(LOG_TAG, "CNE received action: " + action);
                if(mWifiManager != null)
                {
                   List<ScanResult> results = mWifiManager.getScanResults();
                   //updateWlanScanResults(results);
                }
            } else {
                Log.w(LOG_TAG, "CNE received unexpected action: " + action);
            }
        }
    };

    private static enum RatTriedStatus {
        RAT_STATUS_TRIED,
        RAT_STATUS_NOT_TRIED
    }

    private class RatInfo {
        int rat;
        RatTriedStatus status;
        /* other rat related info passed by the callback */
        public RatInfo(){
            rat = CNE_RAT_INVALID;
        }
        public boolean equals(Object o) {
            if (o instanceof RatInfo) {
                RatInfo ratInfo = (RatInfo) o;
                if (this.rat == ratInfo.rat){
                    return true;
                }
            }
        return false;
      }
    }


    private class CallbackInfo {
        IConSvcEventListener listener;
        boolean isNotifBetterRat;
    }

    private class RegInfo implements IBinder.DeathRecipient {
        private int role;
        private int regId;
        private int pid;
        private IBinder mBinder;
        private int notificationsSent;
        /* do we want to have the copy of the Link Reqs?? here? if yes which one
         */
        ArrayList<RatInfo> compatibleRatsList;
        int activeRat;
        int betterRat;
        /* pid and callbackInfo list */
        private CallbackInfo cbInfo;
        /* constructor */
        public RegInfo(IBinder binder){
            role = CNE_ROLE_INVALID;
            regId = mRoleRegId++;
            pid = CNE_INVALID_PID;
            compatibleRatsList = new ArrayList<RatInfo>();
            activeRat = CNE_RAT_INVALID;
            betterRat = CNE_RAT_INVALID;
            cbInfo = new CallbackInfo();
            mBinder = binder;
            if(mBinder != null){
                try {
                    Log.d(LOG_TAG,"CNE linking binder to death");
                    mBinder.linkToDeath(this, 0);
                } catch (RemoteException e) {
                    binderDied();
                  }
            }
        }

        public void binderDied() {
            Log.d(LOG_TAG,"CNE binder died role=" + role + "regId =" + regId);
            releaseLink(role,pid);
        }

        public void dump(){
            Log.d(LOG_TAG, "Role: " + role);
            Log.d(LOG_TAG, "RegId: " + regId);
            Log.d(LOG_TAG, "Pid: " + pid);
            Log.d(LOG_TAG, "ActiveRat: " + activeRat);
            Log.d(LOG_TAG, "BetterRat: " + betterRat);
            for(int index = 0; index < compatibleRatsList.size(); index++){
                RatInfo ratInfo = (RatInfo)compatibleRatsList.get(index);
                Log.d(LOG_TAG, "compatibleRat["+index+"]="+ ratInfo.rat +
                  " ratState = " + ratInfo.status);
            }
        }
    };
    /* regId, RegInfo map */
    private HashMap<Integer, RegInfo> activeRegsList;


    /* DefaultConnection Class: this class registers for default role and keeps
     * one connection up always if available ...so that the device is always
     * data connected and all the legacy apps that are not using cne api have
     * connectivity
     */
    class DefaultConnection {
        LinkProvider mLinkProvider;
        LinkRequirements mLinkReqs;
        MyLinkNotifier mLinkNotifier;

        /* Default constructor initialising linkrequirements to null */
        public DefaultConnection(){
            mLinkReqs = null;
            mLinkProvider = null;
        }

        /*  constructor initialising the linkRequirements */
        public DefaultConnection(LinkRequirements linkReqs){
            mLinkReqs = linkReqs;
            mLinkProvider = null;
        }

        /* This fucntion will start the connection with a default role. */
        public void startConnection(){
            Log.d(LOG_TAG, "DefaultConnection startConnection called");
            mLinkNotifier = new MyLinkNotifier();
            try{
                mLinkProvider = new LinkProvider(LinkProvider.ROLE_DEFAULT,
                                                 mLinkReqs,
                                                 mLinkNotifier);
            }catch(Exception e){
                Log.e(LOG_TAG, "LinkProvider Creation threw and excetion"+e);
            }
            if(mLinkProvider != null) {
                mLinkProvider.getLink();
            }
        }

        /* Ends the previously started connection */
        public void endConnection(){
            Log.d(LOG_TAG, "DefaultConnection endConnection called");
            if(mLinkProvider != null) {
                mLinkProvider.releaseLink();
            }
        }


        /* Implementation of the LinkNotifier interface to pass to the
         * LinkProvider class used to start connection
         */
        private class MyLinkNotifier implements LinkNotifier {
            /* default constructor */
            public MyLinkNotifier(){
            }

            /* on the linkavailability notification we have to notify it
             * it to the iproute to set the default routing table
             */
            public void onLinkAvail(LinkInfo info){
                Log.d(LOG_TAG, "DefaultConnection onLinkAvail called");
                if(mLinkProvider != null) {
                    mLinkProvider.reportLinkSatisfaction(info,true,true);
                    /* notify to the default network to iproute2 */
                    notifyDefaultNwChange(info.getNwId());
                }
            }

            /* Link Acquisition/startConnection as failed retry after some time. */
            public void onGetLinkFailure(int reason){
                Log.d(LOG_TAG, "DefaultConnection onGetLinkFailed reason="
                      + reason);
                /* we will not release the link here. we might get a
                 * better link avail call later.
                 */
            }

            /* a better link is available accept it. */
            public void onBetterLinkAvail(LinkInfo info){
                Log.d(LOG_TAG, "DefaultConnection onBetterLinkAvail called");
                if(mLinkProvider != null) {
                    mLinkProvider.switchLink(info,true);
                    /* notify to the default network to iproute2 */
                    notifyDefaultNwChange(info.getNwId());
                }
            }

            /* current connection is lost. */
            public void onLinkLost(LinkInfo info){
                Log.i(LOG_TAG, "DefaultConnection Link is lost");
            }
        };
    }

    private DefaultConnection mDefaultConn;

    class CNESender extends Handler implements Runnable
    {
        public CNESender(Looper looper) {
            super(looper);
        }

        // Only allocated once
        byte[] dataLength = new byte[4];

        //***** Runnable implementation
        public void
        run()
        {
            //setup if needed
        }


        //***** Handler implemementation

        public void
        handleMessage(Message msg)
        {
            CNERequest rr = (CNERequest)(msg.obj);
            CNERequest req = null;


            switch (msg.what) {
                case EVENT_SEND:
                    /**
                     * mRequestMessagePending++ already happened for every
                     * EVENT_SEND, thus we must make sure
                     * mRequestMessagePending-- happens once and only once
                     */
                    boolean alreadySubtracted = false;
                    try {
                        LocalSocket s;

                        s = mSocket;

                        if (s == null) {
                            rr.release();
                            mRequestMessagesPending--;
                            alreadySubtracted = true;
                            return;
                        }

                        synchronized (mRequestsList) {
                            mRequestsList.add(rr);
                        }

                        mRequestMessagesPending--;
                        alreadySubtracted = true;

                        byte[] data;

                        data = rr.mp.marshall();
                        rr.mp.recycle();
                        rr.mp = null;

                        if (data.length > CNE_MAX_COMMAND_BYTES) {
                            throw new RuntimeException(
                                    "Parcel larger than max bytes allowed! "
                                                          + data.length);
                        }

                        // parcel length in big endian
                        dataLength[0] = dataLength[1] = 0;
                        dataLength[2] = (byte)((data.length >> 8) & 0xff);
                        dataLength[3] = (byte)((data.length) & 0xff);

                        //Log.v(LOG_TAG, "writing packet: " + data.length + " bytes");

                        s.getOutputStream().write(dataLength);
                        s.getOutputStream().write(data);
                    } catch (IOException ex) {
                        Log.e(LOG_TAG, "IOException", ex);
                        req = findAndRemoveRequestFromList(rr.mSerial);
                        // make sure this request has not already been handled,
                        // eg, if CNEReceiver cleared the list.
                        if (req != null || !alreadySubtracted) {

                            rr.release();
                        }
                    } catch (RuntimeException exc) {
                        Log.e(LOG_TAG, "Uncaught exception ", exc);
                        req = findAndRemoveRequestFromList(rr.mSerial);
                        // make sure this request has not already been handled,
                        // eg, if CNEReceiver cleared the list.
                        if (req != null || !alreadySubtracted) {
                            //rr.onError(GENERIC_FAILURE);
                            rr.release();
                        }
                    }

                    if (!alreadySubtracted) {
                        mRequestMessagesPending--;
                    }

                    break;


            }
        }
    }

    /**
     * Reads in a single CNE message off the wire. A CNE message consists
     * of a 4-byte little-endian length and a subsequent series of bytes.
     * The final message (length header omitted) is read into
     * <code>buffer</code> and the length of the final message (less header)
     * is returned. A return value of -1 indicates end-of-stream.
     *
     * @param is non-null; Stream to read from
     * @param buffer Buffer to fill in. Must be as large as maximum
     * message size, or an ArrayOutOfBounds exception will be thrown.
     * @return Length of message less header, or -1 on end of stream.
     * @throws IOException
     */
    private static int readCneMessage(InputStream is, byte[] buffer)
            throws IOException
    {
        int countRead;
        int offset;
        int remaining;
        int messageLength;

        // First, read in the length of the message
        offset = 0;
        remaining = 4;
        do {
            countRead = is.read(buffer, offset, remaining);

            if (countRead < 0 ) {
                Log.e(LOG_TAG, "Hit EOS reading message length");
                return -1;
            }

            offset += countRead;
            remaining -= countRead;
        } while (remaining > 0);

        messageLength = ((buffer[0] & 0xff) << 24)
                | ((buffer[1] & 0xff) << 16)
                | ((buffer[2] & 0xff) << 8)
                | (buffer[3] & 0xff);

        // Then, re-use the buffer and read in the message itself
        offset = 0;
        remaining = messageLength;
        do {
            countRead = is.read(buffer, offset, remaining);

            if (countRead < 0 ) {
                Log.e(LOG_TAG, "Hit EOS reading message.  messageLength=" + messageLength
                        + " remaining=" + remaining);
                return -1;
            }

            offset += countRead;
            remaining -= countRead;
        } while (remaining > 0);

        return messageLength;
    }

    class CNEReceiver implements Runnable
    {
        byte[] buffer;


        CNEReceiver()
        {
            buffer = new byte[CNE_MAX_COMMAND_BYTES];
        }

        public void
        run()
        {
            int retryCount = 0;

            try {for (;;) {
                LocalSocket s = null;
                LocalSocketAddress l;

                try {
                    Log.v(LOG_TAG, "CNE creating socket");
                    s = new LocalSocket();
                    l = new LocalSocketAddress(SOCKET_NAME_CNE,
                            LocalSocketAddress.Namespace.RESERVED);
                    s.connect(l);
                } catch (IOException ex){
                    try {
                        if (s != null) {
                            s.close();
                        }
                    } catch (IOException ex2) {
                        //ignore failure to close after failure to connect
                    }

                    // don't print an error message after the the first time
                    // or after the 8th time

                    if (retryCount == 8) {
                        Log.e (LOG_TAG,
                            "Couldn't find '" + SOCKET_NAME_CNE
                            + "' socket after " + retryCount
                            + " times, continuing to retry silently");
                    } else if (retryCount > 0 && retryCount < 8) {
                        Log.i (LOG_TAG,
                            "Couldn't find '" + SOCKET_NAME_CNE
                            + "' socket; retrying after timeout");
                    }

                    try {
                        Thread.sleep(SOCKET_OPEN_RETRY_MILLIS);
                    } catch (InterruptedException er) {
                    }

                    retryCount++;
                    continue;
                }

                retryCount = 0;

                mSocket = s;
                Log.i(LOG_TAG, "Connected to '" + SOCKET_NAME_CNE + "' socket");
                isCndUp = true;

                // send the init request to lowerlayer cne
                sendInitReq();

                /* wait until CNE gets created*/
                synchronized(mService){
                    /* send DefaultNwPref */
                    sendDefaultNwPref(mNetworkPreference);

                    /* start the default connection now */
                    try{
                        mDefaultConn.startConnection();
                    }catch(NullPointerException e){
                        Log.e(LOG_TAG,"Exception mDefaultConn is null" + e);
                    }
                }

                int length = 0;
                try {
                    InputStream is = mSocket.getInputStream();

                    for (;;) {
                        Parcel p;

                        length = readCneMessage(is, buffer);

                        if (length < 0) {
                            // End-of-stream reached
                            break;
                        }

                        p = Parcel.obtain();
                        p.unmarshall(buffer, 0, length);
                        p.setDataPosition(0);

                        //Log.v(LOG_TAG, "Read packet: " + length + " bytes");

                        processResponse(p);
                        p.recycle();
                    }
                } catch (java.io.IOException ex) {
                    Log.i(LOG_TAG, "'" + SOCKET_NAME_CNE + "' socket closed",
                          ex);
                } catch (Throwable tr) {
                    Log.e(LOG_TAG, "Uncaught exception read length=" + length +
                        "Exception:" + tr.toString());
                }

                Log.i(LOG_TAG, "Disconnected from '" + SOCKET_NAME_CNE
                      + "' socket");
                /* end the default connection it will get started
                 * again when connection gets established
                 */
                try{
                    mDefaultConn.endConnection();
                }catch(NullPointerException e){
                    Log.e(LOG_TAG,"Exception mDefaultConn is null" + e);
                }
                isCndUp = false;

                try {
                    mSocket.close();
                } catch (IOException ex) {
                }

                mSocket = null;
                CNERequest.resetSerial();

                // Clear request list on close
                synchronized (mRequestsList) {
                    for (int i = 0, sz = mRequestsList.size() ; i < sz ; i++) {
                        CNERequest rr = mRequestsList.get(i);
                        //rr.onError(RADIO_NOT_AVAILABLE);
                        rr.release();
                    }

                    mRequestsList.clear();
                }
            }} catch (Throwable tr) {
                Log.e(LOG_TAG,"Uncaught exception", tr);
            }
        }
    }


    //***** Constructor

    public
    CNE(Context context, ConnectivityService conn)
    {
        //super(context);

        PowerManager pm = (PowerManager)context.getSystemService(Context.POWER_SERVICE);

        mRequestMessagesPending = 0;

        mContext = context;

        mService = conn;
        mSenderThread = new HandlerThread("CNESender");
        mSenderThread.start();

        Looper looper = mSenderThread.getLooper();
        mSender = new CNESender(looper);

        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_BATTERY_CHANGED);
        filter.addAction(WifiManager.WIFI_STATE_CHANGED_ACTION);
        filter.addAction(WifiManager.RSSI_CHANGED_ACTION);
        filter.addAction(WifiManager.NETWORK_STATE_CHANGED_ACTION);
        filter.addAction(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION);
        filter.addAction(TelephonyIntents.ACTION_SIGNAL_STRENGTH_CHANGED);

        context.registerReceiver(mIntentReceiver, filter);
        activeRegsList = new HashMap<Integer, RegInfo>();

        /* create the default connection no metatdata right now */
        LinkRequirements linkReqs = null;
        mDefaultConn = new DefaultConnection(linkReqs);
        mWifiManager = (WifiManager)mContext.getSystemService(Context.WIFI_SERVICE);
        mTelephonyManager = (TelephonyManager)mContext.getSystemService(mContext.TELEPHONY_SERVICE);

        // register for phone state notifications.
        ((TelephonyManager)mContext.getSystemService(Context.TELEPHONY_SERVICE))
                .listen(mPhoneStateListener,
                          PhoneStateListener.LISTEN_SERVICE_STATE
                        | PhoneStateListener.LISTEN_SIGNAL_STRENGTHS
                        | PhoneStateListener.LISTEN_CALL_STATE
                        | PhoneStateListener.LISTEN_DATA_CONNECTION_STATE
                        | PhoneStateListener.LISTEN_DATA_ACTIVITY);
        mReceiver = new CNEReceiver();
        mReceiverThread = new Thread(mReceiver, "CNEReceiver");
        mReceiverThread.start();

    }

    private void
    send(CNERequest rr)
    {
        Message msg;

        msg = mSender.obtainMessage(EVENT_SEND, rr);

        //acquireWakeLock();

        msg.sendToTarget();


    }

    private void
    processResponse (Parcel p)
    {
        int type;

        type = p.readInt();

        if (type == RESPONSE_UNSOLICITED) {
            processUnsolicited (p);
        } else if (type == RESPONSE_SOLICITED) {
            processSolicited (p);
        }

    }


    private CNERequest findAndRemoveRequestFromList(int serial)
    {
        synchronized (mRequestsList) {
            for (int i = 0, s = mRequestsList.size() ; i < s ; i++) {
                CNERequest rr = mRequestsList.get(i);

                if (rr.mSerial == serial) {
                    mRequestsList.remove(i);
                    return rr;
                }
            }
        }

        return null;
    }

    private void
    processSolicited (Parcel p)
    {
        int serial, error;
        boolean found = false;

        serial = p.readInt();
        error = p.readInt();

        CNERequest rr;

        rr = findAndRemoveRequestFromList(serial);

        if (rr == null) {
            Log.w(LOG_TAG, "Unexpected solicited response! sn: "
                            + serial + " error: " + error);
            return;
        }

        if (error != 0) {
            rr.onError(error);
            rr.release();
            return;
        }
    }

    private void
    processUnsolicited (Parcel p)
    {
        Log.w(LOG_TAG, "processUnsolicited called");
        int response;
        Object ret;

        response = p.readInt();
        switch(response) {
             case CNE_RESPONSE_REG_ROLE: {
                 handleRegRoleRsp(p);
                 break;
             }
             case CNE_RESPONSE_GET_BEST_NW:{
                 handleGetCompatibleNwsRsp(p);
                 break;
             }
             case CNE_RESPONSE_CONFIRM_NW: {
                 handleConfNwRsp(p);
                 break;
             }
             case CNE_RESPONSE_DEREG_ROLE: {
                 handleDeRegRoleRsp(p);
                 break;
             }
            case CNE_REQUEST_BRING_RAT_DOWN: {
                 handleRatDownMsg(p);
                 break;
             }
            case CNE_REQUEST_BRING_RAT_UP: {
                 handleRatUpMsg(p);
                 break;
             }
            case CNE_NOTIFY_MORE_PREFERED_RAT_AVAIL: {
                handleMorePrefNwAvailEvent(p);
                break;
            }
            case CNE_NOTIFY_RAT_LOST: {
                handleRatLostEvent(p);
                break;
            }
            case CNE_REQUEST_START_SCAN_WLAN: {
                handleStartScanWlanMsg(p);
                break;
            }
            case CNE_NOTIFY_INFLIGHT_STATUS: {
                handleNotifyInFlightStatusMsg(p);
                break;
            }

             default: {
                 Log.w(LOG_TAG,"UNKOWN Unsolicited Event "+response);
             }
        }
        return;
    }


    private int
    NetworkStateToInt(NetworkInfo.State state)
    {
        switch(state)
        {
           case CONNECTING: {return 0;}
           case CONNECTED:  {return 1;}
           case SUSPENDED:  {return 2;}
           case DISCONNECTING: {return 3;}
           case DISCONNECTED:  {return 4;}
           case UNKNOWN:       {return 5;}

        }
        return -1;


    }

    private boolean isEvdo() {
        return ( (mServiceState != null)
                 && ((mServiceState.getRadioTechnology()
                        == ServiceState.RADIO_TECHNOLOGY_EVDO_0)
                     || (mServiceState.getRadioTechnology()
                        == ServiceState.RADIO_TECHNOLOGY_EVDO_A)));
    }

    private PhoneStateListener mPhoneStateListener = new PhoneStateListener() {
        @Override
        public void onSignalStrengthsChanged(SignalStrength signalStrength) {
            Log.i(LOG_TAG,"CNE onSignalStrengthsChanged ");
            mSignalStrength = signalStrength;

        }

        @Override
        public void onServiceStateChanged(ServiceState state) {
            mServiceState = state;
            Log.i(LOG_TAG,"CNE onServiceStateChanged ");
        }

        @Override
        public void onCallStateChanged(int state, String incomingNumber) {
            Log.i(LOG_TAG,"CNE onCallStateChanged ");

        }

        @Override
        public void onDataConnectionStateChanged(int dataState, int networkType) {

            Log.i(LOG_TAG,"onDataConnectionStateChanged dataState=" + dataState + ",networkType=" + networkType);

            if (mTelephonyManager != null) {
              String ifName = null;
              String ipAddr = null;
              String gatewayAddr = null;
              NetworkInfo.State networkState = converToNetworkState(dataState);

              if (networkState == NetworkInfo.State.CONNECTED){
                ifName = mTelephonyManager.getActiveInterfaceName(null);
                ipAddr = mTelephonyManager.getActiveIpAddress(null);
                gatewayAddr = mTelephonyManager.getActiveGateway(null);

                if((gatewayAddr == null) || (gatewayAddr.length() == 0)) {
                   Log.w(LOG_TAG, "onDataConnectionStateChanged: gateway address is empty string");
                   gatewayAddr = null;
                }
                if(ipAddr != null && ipAddr.length() !=0 && ifName != null && ifName.length()!=0) {
                   activeWwanIfName = ifName;
                   configureIproute2(CNE_IPROUTE2_ADD_DEFAULT, ifName, ipAddr, gatewayAddr);

                }
              } else if (networkState == NetworkInfo.State.DISCONNECTED) {
                configureIproute2(CNE_IPROUTE2_DELETE_DEFAULT, activeWwanIfName, null, null);
              }

              int roaming =(int)(mTelephonyManager.isNetworkRoaming()?1:0);
              //int type = mTelephonyManager.getNetworkType();
              int signalStrength = getSignalStrength(networkType);
              String tsStr = null;
              long currentTime = System.currentTimeMillis();
              Timestamp ts = new Timestamp(currentTime);
              tsStr = ts.toString();

              Log.i(LOG_TAG, "onDataConnectionStateChanged - networkType= "
                    + networkType + ",networkState=" + networkState +
                    ",signalStr=" + signalStrength + ",roaming=" + roaming);

              Log.i(LOG_TAG, "onDataConnectionStateChanged - ifName="
                    + activeWwanIfName + ",ipAddr=" + ipAddr + "gateway="
                    + gatewayAddr + ",timeStamp=" + tsStr);

              updateWwanStatus(networkType, NetworkStateToInt(networkState),
                               signalStrength, roaming, ipAddr, tsStr);
              notifyRatConnectStatus(CNE_RAT_WWAN, NetworkStateToInt(networkState), ipAddr);


            } else {
              Log.i(LOG_TAG, "onDataConnectionStateChanged null TelephonyManager");
            }
        }
    };


    private NetworkInfo.State converToNetworkState(int dataState){
        switch(dataState){
        case TelephonyManager.DATA_DISCONNECTED :
            return NetworkInfo.State.DISCONNECTED;
        case TelephonyManager.DATA_CONNECTING :
            return NetworkInfo.State.CONNECTING;
        case TelephonyManager.DATA_CONNECTED :
            return NetworkInfo.State.CONNECTED;
        case TelephonyManager.DATA_SUSPENDED :
            return NetworkInfo.State.SUSPENDED;
        default:
            return NetworkInfo.State.UNKNOWN;
        }
    }

    private int
    getSignalStrength(int networkType)
    {

        if (mSignalStrength == null)
        {
          Log.w(LOG_TAG, "getSignalStrength mSignalStrength in null");
          return -1;
        }

        Log.i(LOG_TAG, "getSignalStrength networkType= " + networkType);

        switch(networkType)
	{
           case TelephonyManager.NETWORK_TYPE_GPRS:
           case TelephonyManager.NETWORK_TYPE_EDGE:
           case TelephonyManager.NETWORK_TYPE_UMTS:
           case TelephonyManager.NETWORK_TYPE_HSDPA:
           case TelephonyManager.NETWORK_TYPE_HSUPA:
           case TelephonyManager.NETWORK_TYPE_HSPA:
             return (mSignalStrength.getGsmSignalStrength());
           case TelephonyManager.NETWORK_TYPE_1xRTT:
           case TelephonyManager.NETWORK_TYPE_CDMA:
             return(mSignalStrength.getCdmaDbm());
             //return(mSignalStrength.getCdmaEcio());
           case TelephonyManager.NETWORK_TYPE_EVDO_0:
           case TelephonyManager.NETWORK_TYPE_EVDO_A:
             return(mSignalStrength.getEvdoDbm());
             //return(mSignalStrength.getEvdoSnr());
   	}
        return -1;

    }

    private Object
    responseInts(Parcel p)
    {
        int numInts;
        int response[];

        numInts = p.readInt();

        response = new int[numInts];

        for (int i = 0 ; i < numInts ; i++) {
            response[i] = p.readInt();
        }

        return response;
    }


    private Object
    responseVoid(Parcel p)
    {
        return null;
    }


    private Object
    responseString(Parcel p)
    {
        String response;

        response = p.readString();

        return response;
    }

    private Object
    responseStrings(Parcel p)
    {
        int num;
        String response[];

        response = p.readStringArray();

        if (false) {
            num = p.readInt();

            response = new String[num];
            for (int i = 0; i < num; i++) {
                response[i] = p.readString();
            }
        }

        return response;
    }

    private Object
    responseRaw(Parcel p)
    {
        int num;
        byte response[];

        response = p.createByteArray();

        return response;
    }

    private void cnejLog(String msg) {
        Log.d(LOG_TAG, msg);
    }

    private void cnejLogv(String msg) {
        Log.v(LOG_TAG, msg);
    }


    /* API functions */
    public boolean updateBatteryStatus(int status, int pluginType, int level) {

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_UPDATE_BATTERY_INFO);
        if (rr == null)
        {
             Log.e(LOG_TAG, "updateBatteryStatus: rr=NULL");
             return false;
        }
        Log.i(LOG_TAG, "UpdateBatteryStatus status=" + status + "pluginType="
                      + pluginType + "level=" + level);

        rr.mp.writeInt(3); //num of ints that are getting written
        rr.mp.writeInt(status);
        rr.mp.writeInt(pluginType);
        rr.mp.writeInt(level);

        send(rr);

        return true;
    }

    public boolean updateWlanStatus(int type, int state, int rssi,
                                    String ssid, String ipAddr, String timeStamp) {

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_UPDATE_WLAN_INFO);
        if (rr == null)
        {
             Log.e(LOG_TAG, "updateWlanStatus: rr=NULL - no updated");
             return false;
        }

        Log.i(LOG_TAG, "UpdateWlanStatus type=" + type + ",state=" + state +
              ",rssi=" + rssi + ",ssid=" + ssid + ",ipAddr" + ipAddr +
              ",timeStamp" + timeStamp);

        rr.mp.writeInt(type);
        rr.mp.writeInt(state);
        rr.mp.writeInt(rssi);
        rr.mp.writeString(ssid);
        rr.mp.writeString(ipAddr);
        rr.mp.writeString(timeStamp);

        send(rr);

        return true;
    }


    public boolean updateWlanScanResults(List<ScanResult> scanResults) {

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_UPDATE_WLAN_SCAN_RESULTS);
        if (rr == null)
        {
             Log.e(LOG_TAG, "updateWlanScanResults: rr=NULL");
             return false;
        }

        if (scanResults != null)
        {
             Log.i(LOG_TAG, "CNE- updateWlanScanResults: scanResults size = " + scanResults.size());
             rr.mp.writeInt(scanResults.size());  // write number of elements
             for (int i = scanResults.size() - 1; i >= 0; i--)
             {
                 ScanResult scanResult = scanResults.get(i);
                 rr.mp.writeInt(scanResult.level);
                 rr.mp.writeInt(scanResult.frequency);
                 rr.mp.writeString(scanResult.SSID);
                 rr.mp.writeString(scanResult.BSSID);
                 rr.mp.writeString(scanResult.capabilities);

                 // Need to check for empty string before writeString?
                 //if (TextUtils.isEmpty(scanResult.capabilities)) {
             }

        }

        send(rr);

        return true;
    }

    /** {@hide} */
    public boolean updateWwanStatus(int type, int state, int rssi, int roaming,
                                    String ipAddr, String timeStamp) {

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_UPDATE_WWAN_INFO);
        if (rr == null) {
            Log.e(LOG_TAG, "updateWwanStatus: rr=NULL - no updated");
            return false;
        }
        Log.i(LOG_TAG, "UpdateWwanStatus type=" + type + ",state=" + state + ",rssi="
               + rssi + ",roaming=" + roaming + ",ipAddr" + ipAddr + ",timeStamp" + timeStamp);

        rr.mp.writeInt(type);
        rr.mp.writeInt(state);
        rr.mp.writeInt(rssi);
        rr.mp.writeInt(roaming);
        rr.mp.writeString(ipAddr);
        rr.mp.writeString(timeStamp);

        send(rr);
        return true;
    }

    /** {@hide} */
    public boolean notifyRatConnectStatus(int type, int status, String ipAddr) {

        CNERequest rr = CNERequest.obtain(CNE_NOTIFY_RAT_CONNECT_STATUS);

        if (rr == null) {
            Log.e(LOG_TAG, "notifyRatConnectStatus: rr=NULL");
            return false;
        }
        Log.i(LOG_TAG, "notifyRatConnectStatus ratType=" + type + ",status="
                      + status + ",ipAddr=" + ipAddr);

        rr.mp.writeInt(type);
        rr.mp.writeInt(status);
        rr.mp.writeString(ipAddr);

        send(rr);
        return true;
    }

    /** {@hide} */
    public boolean configureIproute2(int command, String ifName, String ipAddr,
                                     String gatewayAddr) {

      CNERequest rr = CNERequest.obtain(CNE_REQUEST_CONFIG_IPROUTE2_CMD);

      if (rr == null)	{
          Log.e(LOG_TAG, "configureIproute2: rr=NULL");
          return false;
      }
      Log.i(LOG_TAG, "configureIproute2: Command=" + command + ",ifName=" + ifName + 
            ",ipAddr=" + ipAddr + ",gatewayAddr=" + gatewayAddr);

      rr.mp.writeInt(command);
      rr.mp.writeString(ifName);
      rr.mp.writeString(ipAddr);
      rr.mp.writeString(gatewayAddr);

      send(rr);
      return true;
    }



    /** {@hide} */
    public void sendDefaultNwPref2Cne(int preference){
        mNetworkPreference = preference;
    }

    /** {@hide} */
    public void sendDefaultNwPref(int preference){
        CNERequest rr = CNERequest.obtain(CNE_NOTIFY_DEFAULT_NW_PREF);
        if (rr != null) {
            rr.mp.writeInt(1); //num of ints that are getting written
            int rat = CNE_RAT_WLAN;
            if(preference == ConnectivityManager.TYPE_MOBILE) {
                rat = CNE_RAT_WWAN;
            }
            rr.mp.writeInt(rat);
            send(rr);
        }
        else{
            Log.e(LOG_TAG, "sendDefaultNwPref2Cne: rr=NULL");
        }
        return;
    }

    private boolean sendRegRoleReq
    (
        int role,
        int roleRegId,
        int fwLinkBw,
        int revLinkBw
    ) {
        CNERequest rr = CNERequest.obtain(CNE_REQUEST_REG_ROLE);
        if (rr == null) {
            Log.e(LOG_TAG, "sendRegRoleReq: rr=NULL");
            return false;
        }

        rr.mp.writeInt(4); //num of ints that are getting written
        rr.mp.writeInt(role);
        rr.mp.writeInt(roleRegId);
        rr.mp.writeInt(fwLinkBw);
        rr.mp.writeInt(revLinkBw);

        send(rr);

        return true;
    }

    private boolean sendInitReq() {
        CNERequest rr = CNERequest.obtain(CNE_REQUEST_INIT);
        if (rr == null) {
            Log.e(LOG_TAG, "sendinitReq: rr=NULL");
            return false;
        }
        send(rr);
        return true;
    }


    private boolean sendGetCompatibleNwsReq(int roleRegId) {
        CNERequest rr = CNERequest.obtain(CNE_REQUEST_GET_COMPATIBLE_NWS);
        if (rr == null)
        {
             Log.e(LOG_TAG, "sendGetCompatibleNwsReq: rr=NULL");
             return false;
        }
        rr.mp.writeInt(1); //num of ints that are getting written
        rr.mp.writeInt(roleRegId);
        Log.d(LOG_TAG, "Sending GetCompatibleNwsReq roleRegId=" + roleRegId);
        send(rr);
        return true;
    }

    // change name and params one is added
    private boolean sendConfirmNwReq
    (
        int roleRegId,
        int ifaceId,
        int confirmation,
        int notifyIfBetterNwAvail,
        int newIfaceId
    )
    {
        CNERequest rr = CNERequest.obtain(CNE_REQUEST_CONF_NW);
        if (rr == null) {
            Log.e(LOG_TAG, "sendConfirmNwReq: rr=NULL");
            return false;
        }
        rr.mp.writeInt(5); //num of ints that are getting written
        rr.mp.writeInt(roleRegId);
        rr.mp.writeInt(ifaceId);
        rr.mp.writeInt(confirmation);
        rr.mp.writeInt(notifyIfBetterNwAvail);
        rr.mp.writeInt(newIfaceId);

        send(rr);
        return true;

    }

    private boolean sendDeregRoleReq(int roleRegId) {

        CNERequest rr = CNERequest.obtain(CNE_REQUEST_DEREG_ROLE);
        if (rr == null) {
             Log.e(LOG_TAG, "sendDeregRoleReq: rr=NULL");
             return false;
        }
        rr.mp.writeInt(1); //num of ints that are getting written
        rr.mp.writeInt(roleRegId);
        Log.e(LOG_TAG, "sendDeregRoleReq:");

        send(rr);
        return true;
    }


    private int getRegId(int pid, int role){
        int regId = CNE_REGID_INVALID;
        Iterator activeRegsIter = activeRegsList.entrySet().iterator();
        while(activeRegsIter.hasNext()){
            RegInfo regInfo = (RegInfo)((Map.Entry)activeRegsIter.next()).getValue();
            if(regInfo.role == role && regInfo.pid == pid){
                regId = regInfo.regId;
                break;
            }
        }
        return regId;
    }


    private void handleRegRoleRsp(Parcel p){
        int numInts = p.readInt();
        int roleRegId = p.readInt();
        int evtStatus = p.readInt();
        Log.i(LOG_TAG,"handleRegRoleRsp called with numInts = "+ numInts +
              " RoleRegId = "+ roleRegId + " evtStatus = " + evtStatus);
        /* does this role already exists? */
        if(activeRegsList.containsKey(roleRegId)){
            if(evtStatus == STATUS_SUCCESS){
                /* register role was success so get the compatible networks */
                sendGetCompatibleNwsReq(roleRegId);
                return;
            }
            else{
                RegInfo regInfo = activeRegsList.get(roleRegId);
                IConSvcEventListener listener = regInfo.cbInfo.listener;
                if(listener != null){
                    try {
                        listener.onGetLinkFailure(LinkNotifier.FAILURE_GENERAL);
                    }
                    catch ( RemoteException e ) {
                        Log.w(LOG_TAG,"handleRegRoleRsp listener is null");
                   }
                }
            }
        }
        else {
            Log.w(LOG_TAG,"handleRegRoleRsp regId=" + roleRegId + " does not exists");
            return;
        }
    }

    private void handleGetCompatibleNwsRsp(Parcel p){
        int roleRegId = p.readInt();
        int evtStatus = p.readInt();
        Log.i(LOG_TAG,"handleGetCompatibleNwsRsp called for roleRegId = "+
              roleRegId + " evtStatus = " + evtStatus);
        /* does this role already exists? */
        if(activeRegsList.containsKey(roleRegId)){
            RegInfo regInfo = activeRegsList.get(roleRegId);
            if(evtStatus == STATUS_SUCCESS){
                /* save the returned info */
                regInfo.activeRat = p.readInt();
                /* save the oldCompatibleRatsList */
                ArrayList<RatInfo> prevCompatibleRatsList =
                    regInfo.compatibleRatsList;
                ArrayList<RatInfo> newCompatibleRatsList =
                    new ArrayList<RatInfo>();
                for(int i = 0; i< CNE_RAT_MAX; i++){
                    int nextRat = p.readInt();
                    if(nextRat != CNE_RAT_INVALID && nextRat != CNE_RAT_NONE ){
                        RatInfo ratInfo = new RatInfo();
                        ratInfo.rat = nextRat;
                        if(nextRat == regInfo.activeRat){
                            ratInfo.status = RatTriedStatus.RAT_STATUS_TRIED;
                        }
                        else {
                            /* try to preserve the old rat status */
                            int index = prevCompatibleRatsList.indexOf(ratInfo);
                            if(index != -1){
                                RatInfo oldRatInfo =
                                    prevCompatibleRatsList.get(index);
                                ratInfo.status = oldRatInfo.status;
                            }
                            else{
                                ratInfo.status =
                                    RatTriedStatus.RAT_STATUS_NOT_TRIED;
                            }
                        }
                        newCompatibleRatsList.add(ratInfo);
                    }
                }
                String ipAddr = new String(p.readString());
                int flBwEst = p.readInt();
                int rlBwEst = p.readInt();
                Log.i(LOG_TAG,"IPAddr = "+ ipAddr +" flBwEst = "+flBwEst+
                      " rlBwEst = " + rlBwEst);
                prevCompatibleRatsList.clear();
                regInfo.compatibleRatsList = newCompatibleRatsList;
                regInfo.dump();
                /* call the call backs */
                /* over here the implementation is to use what ever the
                 * connectivityEngine has passed as the best rat, if
                 * AndroidConSvc is not satisifed or wants to do give a
                 * different nw then, it can confirm the bestrat given by
                 * Cne negative, and do not call the onLinkAvail
                 * callback yet and request a new one
                 */
                IConSvcEventListener listener = regInfo.cbInfo.listener;
                if(listener != null){
                    try {
                        LinkInfo linkInfo =
                            new LinkInfo(ipAddr,
                                         flBwEst,
                                         rlBwEst,
                                         regInfo.activeRat);
                        regInfo.notificationsSent = regInfo.notificationsSent |
                            CNE_MASK_ON_LINK_AVAIL_SENT;
                        listener.onLinkAvail(linkInfo);
                    }
                    catch ( RemoteException e ) {
                        Log.w(LOG_TAG,"handleGetCompNwsRsp listener is null");
                   }
                }
                return;
            }
            else{
                IConSvcEventListener listener = regInfo.cbInfo.listener;
                if(listener != null){
                    try {
                      listener.onGetLinkFailure(LinkNotifier.FAILURE_NO_LINKS);
                    }
                    catch ( RemoteException e ) {
                        Log.w(LOG_TAG,"handleGetCompNwsRsp listener is null");
                   }
                }
            }
        }
        else {
            Log.w(LOG_TAG,"handleGetCompatibleNwsRsp role does not exists");
            return;
        }

    }

    private void handleConfNwRsp(Parcel p){
        int numInts = p.readInt();
        int roleRegId = p.readInt();
        int evtStatus = p.readInt();
        Log.i(LOG_TAG,"handleConfNwRsp called with numInts = "+ numInts +
              " regRoleId = "+ roleRegId + " evtStatus = " + evtStatus);
    }

    private void handleDeRegRoleRsp(Parcel p){
        int numInts = p.readInt();
        int roleRegId = p.readInt();
        int evtStatus = p.readInt();
        Log.i(LOG_TAG,"handleDeRegRoleRsp called with numInts = "+ numInts +
              " roleRegId = "+ roleRegId + " evtStatus = " + evtStatus);
        /* clean up */
        /* does this role already exists? */
        if(activeRegsList.containsKey(roleRegId)){
            RegInfo regInfo = activeRegsList.get(roleRegId);
            regInfo.compatibleRatsList.clear();
            activeRegsList.remove(roleRegId);
        }
        else {
            Log.w(LOG_TAG,"handleDeRegRoleRsp role does not exists");
            return;
        }
    }

    private void handleMorePrefNwAvailEvent(Parcel p){
        int roleRegId = p.readInt();
        int betterRat = p.readInt();
        String ipAddr = new String(p.readString());
        int flBwEst = p.readInt();
        int rlBwEst = p.readInt();
        Log.i(LOG_TAG,"handleMorePrefNwAvailEvent called for roleRegId = "+
               roleRegId + " betterRat = " + betterRat + " ipAddr = "+ipAddr+
              " flBwEst = " + flBwEst+ " rlBwEst = "+rlBwEst);
        /* does this role already exists? */
        if(activeRegsList.containsKey(roleRegId)){
            RegInfo regInfo = activeRegsList.get(roleRegId);
            /* save the betterRat info */
            regInfo.betterRat = betterRat;
                            /* call the call backs */
            IConSvcEventListener listener = regInfo.cbInfo.listener;
            if(listener != null){
                try {
                    LinkInfo linkInfo =
                        new LinkInfo(ipAddr,
                                     flBwEst,
                                     rlBwEst,
                                     betterRat);
                    regInfo.notificationsSent = regInfo.notificationsSent |
                            CNE_MASK_ON_BETTER_LINK_AVAIL_SENT;
                    listener.onBetterLinkAvail(linkInfo);
                }
                catch ( RemoteException e ) {
                    Log.w(LOG_TAG,"handleMorePrefNwAvailEvt listener is null");
               }
            }
        }
        else {
            Log.w(LOG_TAG,"handleMorePrefNwAvailEvent role does not exists");
            return;
        }
    }


    private void handleRatLostEvent(Parcel p){
        int numInts = p.readInt();
        int roleRegId = p.readInt();
        int rat = p.readInt();
        Log.i(LOG_TAG,"handleRatLostEvent called with numInts = "+ numInts +
              " roleRegId = "+ roleRegId + " rat = " + rat);
        /* does this role already exists? */
        if(activeRegsList.containsKey(roleRegId)){
            RegInfo regInfo = activeRegsList.get(roleRegId);
            if(regInfo.activeRat == rat){
                IConSvcEventListener listener = regInfo.cbInfo.listener;
                if(listener != null ){
                    try {
                        LinkInfo linkInfo =
                        new LinkInfo(null,
                                     LinkInfo.INF_UNSPECIFIED,
                                     LinkInfo.INF_UNSPECIFIED,
                                     rat);
                        listener.onLinkLost(linkInfo);
                    }
                    catch ( RemoteException e ) {
                        Log.w(LOG_TAG,"handleRatLostEvent listener is null");
                    }
                }
            }else{
                Log.d(LOG_TAG,"Rat lost is not for the active rat=" +
                        regInfo.activeRat);
            }
        }
        else {
            Log.w(LOG_TAG,"handleRatLostEvent role does not exists");
            return;
        }
    }


    private void handleRatDownMsg(Parcel p){

        int ratType = p.readInt();

        Log.i(LOG_TAG,"handleRatDownMsg called ratType = "+ ratType);

        if (mService != null) {
          mService.bringDownRat(ratType);
        }

        return;

    }

    private void handleRatUpMsg(Parcel p){

        int ratType = p.readInt();

        Log.i(LOG_TAG,"handleRatUpMsg called ratType = "+ ratType);

        ConnectivityManager cm = (ConnectivityManager)
            mContext.getSystemService(Context.CONNECTIVITY_SERVICE);
        NetworkInfo networkInfo = cm.getNetworkInfo(ratType);
        NetworkInfo.State networkState = (networkInfo == null ? NetworkInfo.State.UNKNOWN :
                        networkInfo.getState());
        String ipAddr = null;
        if(networkState == NetworkInfo.State.CONNECTED){
            try {
                if(ratType == CNE_RAT_WLAN){
                    DhcpInfo dhcpInfo = mWifiManager.getDhcpInfo();
                    int ipAddressInt = dhcpInfo.ipAddress;
                    ipAddr = ((ipAddressInt)&0xff) + "."
                             + ((ipAddressInt>>8)&0xff) + "."
                             + ((ipAddressInt>>16)&0xff) + "."
                             + ((ipAddressInt>>24)&0xff);
                }
                if(ratType == CNE_RAT_WWAN){
                    ipAddr = mTelephonyManager.getActiveIpAddress(null);
                }
                notifyRatConnectStatus(ratType, NetworkStateToInt(networkState), ipAddr);
            } catch (Exception e) {
                 Log.w(LOG_TAG, "HandleRatUp Exception", e);
           }
        }else if (mService != null) {
          mService.bringUpRat(ratType);
        }


        return;


    }

    private void handleStartScanWlanMsg(Parcel p){

      Log.i(LOG_TAG,"handleStartScanWlanMsg called");

      if (!mWifiManager.isWifiEnabled())
          return;

      mWifiManager.startScanActive();


    }

    private void handleNotifyInFlightStatusMsg(Parcel p){

      Log.i(LOG_TAG,"handleNotifyInFlightStatusMsg called");
      boolean on;
      int numInts = p.readInt();
      int status = p.readInt();
      if (status == STATUS_INFLIGHT)
        on = true;
      else
        on = false;
      Settings.System.putInt(
              mContext.getContentResolver(),
              Settings.System.AIRPLANE_MODE_ON,
              on ? 1 : 0);
      Intent intent = new Intent(Intent.ACTION_AIRPLANE_MODE_CHANGED);
      intent.putExtra("state", on);
      mContext.sendBroadcast(intent);

    }


    /** {@hide} */
    public void setDefaultConnectionNwPref(int preference){
        Log.d(LOG_TAG,"setDefaultConnectionNwPref called with pref = " + preference);
        /* does this role already exists? */
        if(activeRegsList.containsKey(CNE_DEFAULT_CON_REGID)){
            RegInfo regInfo = activeRegsList.get(CNE_DEFAULT_CON_REGID);
            /* preference can be either wlan or wwan send the confirmNw
             * with the next best wwan or wlan rat in the compatiblerats list
             * for this role
             */
            if(preference != regInfo.activeRat){
                sendConfirmNwReq(regInfo.regId,
                                 regInfo.activeRat,
                                 0,//notsatisfied with this
                                 regInfo.cbInfo.isNotifBetterRat ? 1:0,
                                 preference);
            }
        }
        else {
            Log.w(LOG_TAG,"Default Registration does not exists");
        }
        return;
    }

    /* the logic of thei function will decide what is the next
     * rat to try. here we follow the order provided by the
     * connectivity engine. AndroidConSvc can change this
     * logic what ever it wants
     */
    private int getNextRatToTry(ArrayList<RatInfo> ratList){
        int candidateRat = CNE_RAT_INVALID;
        /* index 0 is the active rat */
        for(int index = 1; index < ratList.size(); index++){
            RatInfo ratInfo = (RatInfo)ratList.get(index);
            if(ratInfo.status == RatTriedStatus.RAT_STATUS_NOT_TRIED){
                candidateRat = ratInfo.rat;
                ratInfo.status = RatTriedStatus.RAT_STATUS_TRIED;
                break;
            }
        }
        Log.d(LOG_TAG,"getNextRatToTry called NextRatToTry= " + candidateRat);
        return candidateRat;
    }


    /** {@hide} */
    public boolean getLink
    (
      int role,
      LinkRequirements linkReqs,
      int pid,
      IBinder listener
    ){
        Log.d(LOG_TAG,"getLink called for role = " + role);
        /* did the app(pid) register for this role already? */
        if(getRegId(pid,role) != CNE_REGID_INVALID){
            Log.w(LOG_TAG,"Multpl same role reg's not allowed by single app");
            return false;
        }
        else{
            /* new role registration for this app (pid) */
            RegInfo regInfo = new RegInfo(listener);
            regInfo.role = role;
            regInfo.pid = pid;
            IConSvcEventListener evtListener = (IConSvcEventListener)
            IConSvcEventListener.Stub.asInterface(listener);
            regInfo.cbInfo.listener = evtListener;
            regInfo.cbInfo.isNotifBetterRat = false;
            Log.i(LOG_TAG, "activeRegsList.size before = " + activeRegsList.size());
            activeRegsList.put(regInfo.regId,regInfo);
            Log.i(LOG_TAG, "activeRolesList.size after = " + activeRegsList.size());
            int fwLinkBwReq = 0;
            int revLinkBwReq = 0;
            if(linkReqs != null){
                fwLinkBwReq = linkReqs.getFwLinkBwReq();
                revLinkBwReq = linkReqs.getRevLinkBwReq();
            }
            sendRegRoleReq(role,
                           regInfo.regId,
                           fwLinkBwReq,
                           revLinkBwReq);
            return true;
        }
    }

    /** {@hide} */
    public boolean reportLinkSatisfaction
    (
      int role,
      int pid,
      LinkInfo info,
      boolean isSatisfied,
      boolean isNotifyBetterLink
    ){
        Log.d(LOG_TAG,"reporting connection satisfaction role = " + role +
              "isSatisfied = " + isSatisfied + "isNotifyBetterLink" +
              isNotifyBetterLink);
        int regId = getRegId(pid,role);
        if( regId != CNE_REGID_INVALID){
            RegInfo regInfo = activeRegsList.get(regId);
            if((regInfo.notificationsSent & CNE_MASK_ON_LINK_AVAIL_SENT) ==
               CNE_MASK_ON_LINK_AVAIL_SENT){
                regInfo.cbInfo.isNotifBetterRat = isNotifyBetterLink;
                int ratToTry = CNE_RAT_NONE;
                if(!isSatisfied){
                    ratToTry = getNextRatToTry(regInfo.compatibleRatsList);
                }
                sendConfirmNwReq(regInfo.regId,
                                 info==null? regInfo.activeRat:info.getNwId(),
                                 isSatisfied ? 1:0,
                                 isNotifyBetterLink ? 1:0,
                                 ratToTry);
                /* if the app was not satisfied and ratToTry was CNE_RAT_INVALID
                 * then call back the app saying connection not possible
                 */
                if(!isSatisfied && ratToTry == CNE_RAT_INVALID){
                    //call the callback
                    try {
                        IConSvcEventListener evtListener = regInfo.cbInfo.listener;
                        if( evtListener != null){
                          evtListener.onGetLinkFailure(LinkNotifier.FAILURE_NO_LINKS);
                        }
                        return true;
                    }
                    catch ( RemoteException e ) {
                        Log.e(LOG_TAG,"remoteException while calling onConnectionComplete");
                        return false;
                    }
                }
                return true;
            }
            else{
                Log.w(LOG_TAG,"OnLinkAvail notification was not sent yet.");
                return false;
            }
        }
        else{
            Log.w(LOG_TAG,"App did not register for this role");
            return false;
        }
    }

    /** {@hide} */
    public boolean releaseLink(int role,int pid){
        Log.d(LOG_TAG,"releasing link for role = " + role);
        int regId = getRegId(pid,role);
        if( regId != CNE_REGID_INVALID){
            RegInfo regInfo = activeRegsList.get(regId);
            sendDeregRoleReq(regInfo.regId);
            /* clean up */
            regInfo.compatibleRatsList.clear();
            activeRegsList.remove(regId);
            return true;
        }
        else{
            Log.w(LOG_TAG,"App did not register for this role");
            return false;
        }
    }

    /** {@hide} */
    public boolean switchLink(int role,
                              int pid,
                              LinkInfo info,
                              boolean isNotifyBetterLink){
        Log.d(LOG_TAG,"switch link for role = " + role);
        int regId = getRegId(pid,role);
        if( regId != CNE_REGID_INVALID){
            RegInfo regInfo = activeRegsList.get(regId);
            if((regInfo.notificationsSent & CNE_MASK_ON_BETTER_LINK_AVAIL_SENT) ==
               CNE_MASK_ON_BETTER_LINK_AVAIL_SENT){
                regInfo.activeRat = info==null? regInfo.betterRat:info.getNwId();
                sendConfirmNwReq(regInfo.regId,
                                 regInfo.activeRat,
                                 CNE_LINK_SATISFIED,
                                 isNotifyBetterLink ? 1:0,
                                 CNE_RAT_NONE);
                return true;
            }
            else{
                Log.w(LOG_TAG,"OnBetterLinkAvail notification was not sent yet.");
                return false;
            }
        }
        else{
            Log.w(LOG_TAG,"App did not register for this role");
            return false;
        }
    }

    /** {@hide} */
    public boolean rejectSwitch(int role,
                                int pid,
                                LinkInfo info,
                                boolean isNotifyBetterLink){
        Log.d(LOG_TAG,"rejectSwitch for role = " + role);
        int regId = getRegId(pid,role);
        if( regId != CNE_REGID_INVALID){
            RegInfo regInfo = activeRegsList.get(regId);
            if((regInfo.notificationsSent & CNE_MASK_ON_BETTER_LINK_AVAIL_SENT) ==
               CNE_MASK_ON_BETTER_LINK_AVAIL_SENT){
                sendConfirmNwReq(regInfo.regId,
                                 info==null? regInfo.betterRat:info.getNwId(),
                                 CNE_LINK_NOT_SATISFIED,
                                 isNotifyBetterLink ? 1:0,
                                 CNE_RAT_NONE);
                return true;
            }
            else{
                Log.w(LOG_TAG,"OnBetterLinkAvail notification was not sent yet.");
                return false;
            }
        }
        else{
            Log.w(LOG_TAG,"App did not register for this role");
            return false;
        }
    }

    /** {@hide} */
    public static boolean configureSsid ( String newStr)
    {
     try
     {

         boolean strMatched = false;
         File file = new File("/data/ssidconfig.txt");
         if (file == null)
         {
             Log.e(LOG_TAG, "configureSsid: Config File not found");
             return false;
         }
         // Read file to buffer
         BufferedReader reader = new BufferedReader(new FileReader(file));
         String line = "";
         String oldtext = "";
         String oldStr = "";
         // Get token of new string
         StringTokenizer newst = new StringTokenizer(newStr, ":");
         String newToken = newst.nextToken();

         Log.i(LOG_TAG, "configureSsid: newToken: " + newToken);

         while((line = reader.readLine()) != null)
         {
             oldtext += line + "\r\n";
             StringTokenizer oldst = new StringTokenizer(line, ":");
             while(oldst.hasMoreTokens())
             {
                 String oldToken = oldst.nextToken();
                 Log.i(LOG_TAG, "configureSsid: oldToken: " + oldToken);
                 if (newToken.equals(oldToken))
                 {
                       Log.i(LOG_TAG, "configSsid entry matched");
                       // Save string to be replaced
                       oldStr = line;
                       strMatched = true;
                 }

             }
         }
         if (!strMatched) {
             Log.i(LOG_TAG, "configSsid entry not matched");
             return false;
         }
         // To replace new text in file
         String newtext = oldtext.replaceAll(oldStr, newStr);
         reader.close();
         FileWriter writer = new FileWriter("/data/ssidconfig.txt");
         writer.write(newtext);
         writer.close();
         return true;

     }
     catch (IOException ioe)
     {
            ioe.printStackTrace();
     }
     return true;
    }

    /** {@hide} */
    public void notifyDefaultNwChange(int nwId){
     Log.i(LOG_TAG, "notifyDefaultNwChange: nwId: " + nwId + ",activeWwanIfname:"
           + activeWwanIfName);
     if ( nwId == ConnectivityManager.TYPE_WIFI) {
       configureIproute2(CNE_IPROUTE2_DELETE_DEFAULT_FROM_MAIN,
                         activeWwanIfName, null, null);
     } else {
       configureIproute2(CNE_IPROUTE2_CHANGE_DEFAULT_FROM_MAIN,
                         activeWwanIfName, null, null);
     }

     return;
    }

}
