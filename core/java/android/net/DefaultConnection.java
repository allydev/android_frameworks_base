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

import android.util.Log;
import android.net.LinkProvider;
import android.net.LinkNotifier;
import android.net.LinkRequirments;
import android.net.LinkInfo;

/** {@hide} */
public class DefaultConnection {
    static final String LOG_TAG = "DEFAULT_CON";

    LinkProvider mLinkProvider;

    LinkRequirments mLinkReqs;

    MyLinkNotifier mLinkNotifier;

    /** {@hide} */
    public DefaultConnection() {
        mLinkReqs = null;
        mLinkProvider = null;
    }

    /** {@hide} */
    public DefaultConnection(LinkRequirments linkReqs) {
        mLinkReqs = linkReqs;
        mLinkProvider = null;
    }

    public void startConnection() {
        Log.d(LOG_TAG, "DefaultConnection startConnection called");
        mLinkNotifier = new MyLinkNotifier();
        mLinkProvider = new LinkProvider(LinkProvider.ROLE_DEFAULT, mLinkReqs, mLinkNotifier);
        if (mLinkProvider != null) {
            mLinkProvider.getLink();
        }
    }

    public void endConnection() {
        Log.d(LOG_TAG, "DefaultConnection endConnection called");
        if (mLinkProvider != null) {
            mLinkProvider.releaseLink();
        }
    }

    private class MyLinkNotifier implements LinkNotifier {
        public MyLinkNotifier() {
        }

        public void onLinkAvail(LinkInfo info) {
            Log.d(LOG_TAG, "DefaultConnection onLinkAvail called");
            if (mLinkProvider != null) {
                mLinkProvider.reportLinkSatisfaction(info, true, true);
                /* notify to ConSvc the default network */
            }
        }

        public void onGetLinkFailure(int reason) {
            Log.d(LOG_TAG, "DefaultConnection onGetLinkFailure called");
            if (mLinkProvider != null) {
                mLinkProvider.releaseLink();
            }
            /* should try this after some time */
            // mLinkProvider.getLink();
        }

        public void onBetterLinkAvail(LinkInfo info) {
            Log.d(LOG_TAG, "DefaultConnection onBetterLinkAvail called");
            if (mLinkProvider != null) {
                mLinkProvider.switchLink(info, true);
                /* notify to mConSvc the default network */
            }
        }

        public void onLinkLost(LinkInfo info) {
            Log.i(LOG_TAG, "DefaultConnection Link is lost");
        }
    };
}
