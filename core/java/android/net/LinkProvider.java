/**
 * Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * * Neither the name of Code Aurora nor
 *     the names of its contributors may be used to endorse or promote
 *     products derived from this software without specific prior written
 *     permission.
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

package android.net;

import android.net.IConnectivityManager;
import android.os.Binder;
import android.os.RemoteException;
import android.net.LinkInfo;
import android.net.LinkRequirments;
import android.os.ServiceManager;
import android.os.IBinder;
import android.util.Log;

/** {@hide} */
public class LinkProvider {
    static final String LOG_TAG = "LinkProvider";

    /** {@hide} */
    public static final int ROLE_DEFAULT = 0;

    private int mRole;

    private LinkRequirments mLinkReqs = null;

    private int mPid;

    private LinkNotifier mLinkNotifier;

    /*
     * handle to connectivity service obj
     */
    private IConnectivityManager mService;

    /** {@hide} */
    public LinkProvider() {
        /*
         * set the role to invalid and metada null and in start connection if
         * you see this throw exception
         */
        mRole = ROLE_DEFAULT;
        mLinkReqs = null;
        mLinkNotifier = null;
    }

    /** {@hide} */
    public LinkProvider(int role, LinkRequirments reqs, LinkNotifier notifier) {
        mRole = role;
        mLinkReqs = reqs;
        mLinkNotifier = notifier;
        /* get handle to connectivity service */
        IBinder b = ServiceManager.getService("connectivity");
        mService = IConnectivityManager.Stub.asInterface(b);
        /* check for mservice to be null and throw a exception */
        if (mService == null) {
            throw new IllegalArgumentException( // is illegalArgException the
                    // right one?
            "mService can not be null");
        }
    }

    /** {@hide} */
    public boolean getLink() {
        try {
            ConSvcEventListener listener = (ConSvcEventListener)IConSvcEventListener.Stub
            .asInterface(new ConSvcEventListener());
            mPid = listener.getCallingPid();
            return mService.getLink(mRole, mLinkReqs, mPid, listener);

        } catch (RemoteException e) {
            /* to do have to take care of the exception properly */
            Log.e(LOG_TAG, "ConSvc throwed remoteExcept'n on startConn call");
            return false;
        }
    }

    /** {@hide} */
    public boolean reportLinkSatisfaction(LinkInfo info, boolean isSatisfied,
            boolean isNotifyBetterLink) {
        try {
            return mService.reportLinkSatisfaction(mRole, mPid, info, isSatisfied,
                    isNotifyBetterLink);

        } catch (RemoteException e) {
            Log.e(LOG_TAG, "ConSvc throwed remoteExcept'n on reportConnSatis call");
            return false;
        }
    }

    /** {@hide} */
    public boolean switchLink(LinkInfo info, boolean isNotifyBetterLink) {
        try {
            return mService.switchLink(mRole, mPid, info, isNotifyBetterLink);

        } catch (RemoteException e) {
            Log.e(LOG_TAG, "ConSvc throwed remoteExcept'n on reportConnSatis call");
            return false;
        }
    }

    /** {@hide} */
    public boolean rejectSwitch(LinkInfo info, boolean isNotifyBetterLink) {
        try {
            return mService.rejectSwitch(mRole, mPid, info, isNotifyBetterLink);

        } catch (RemoteException e) {
            Log.e(LOG_TAG, "ConSvc throwed remoteExcept'n on reportConnSatis call");
            return false;
        }
    }

    /** {@hide} */
    public boolean releaseLink() {
        try {
            return mService.releaseLink(mRole, mPid);
        } catch (RemoteException e) {
            /* print message */
            return false;
        }
    }

    /** {@hide} */
    /*
     * This class has the remoted function call backs that get called when the
     * ConSvc has to notify things to the app
     */
    private class ConSvcEventListener extends IConSvcEventListener.Stub {

        public void onLinkAvail(LinkInfo info) {
            if (mLinkNotifier != null) {
                mLinkNotifier.onLinkAvail(info);
            }
            return;
        }

        public void onBetterLinkAvail(LinkInfo info) {
            if (mLinkNotifier != null) {
                mLinkNotifier.onBetterLinkAvail(info);
            }
            return;
        }

        public void onLinkLost(LinkInfo info) {
            if (mLinkNotifier != null) {
                mLinkNotifier.onLinkLost(info);
            }
            return;
        }

        public void onGetLinkFailure(int reason) {
            if (mLinkNotifier != null) {
                mLinkNotifier.onGetLinkFailure(reason);
            }
            return;
        }
    };

}
