/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
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
 *
 */

package android.net;

import android.net.IConnectivityManager;
import android.os.Binder;
import android.os.RemoteException;
import android.net.LinkInfo;
import android.net.LinkRequirements;
import android.os.ServiceManager;
import android.os.IBinder;
import android.util.Log;

/** {@hide}
 * This class provides a means for applications to specify their requirements
 * and request for a link. Apps can also report their satisfaction with the
 * assigned link and switch links when a new link is available.
 */
public class LinkProvider
{
    static final String LOG_TAG = "LinkProvider";

    /** {@hide}
     * Default Role Id, applies to any packet data traffic pattern that
     * doesn't have another role defined explicitly.
     */
    public static final int ROLE_DEFAULT =  0;

    /* role that the app wants to register.
     */
    private int mRole;

    /* link requirements of the app for the registered role.
     */
    private LinkRequirements mLinkReqs = null;

    /* Apps process id
     */
    private int mPid;

    /* LinkNotifier object to provide notification to the app.
     */
    private LinkNotifier mLinkNotifier;

    /* handle to connectivity service obj
     */
    private IConnectivityManager mService;

    /** {@hide}
     * This is the default constructor that initializes the role to default
     * and requirements and notifier to null.
     */
    public LinkProvider(){
        /* set the role to invalid and metada null and in start
         * connection if you see this throw exception
         */
        mRole = ROLE_DEFAULT;
        mLinkReqs = null;
        mLinkNotifier = null;
    }

    /** {@hide}
     * This constructor can be used by apps to specify a role and optional
     * requirements and a link notifier object to receive notifications.
     * @param role Role that the app wants to register
     * @param reqs Requirements of the app for that role
     * @param notifier LinkNotifier object to provide notification to the app
     */
    public LinkProvider(int role, LinkRequirements reqs, LinkNotifier notifier){
        mRole = role;
        mLinkReqs = reqs;
        mLinkNotifier = notifier;
        /* get handle to connectivity service */
        IBinder b = ServiceManager.getService("connectivity");
        mService = IConnectivityManager.Stub.asInterface(b);
        /* check for mservice to be null and throw a exception */
        if(mService == null){
            throw new IllegalArgumentException(
                "mService can not be null");
        }
    }


    /** {@hide}
     * This function will be used by apps to request a system after they have
     * specified their role and/or requirements.
     * @return {@code true}if the request has been accepted.
     * {@code false} otherwise.  A return value of true does NOT mean that a
     * link is available for the app to use. That will delivered via the
     * LinkNotifier.
     */
    public boolean getLink(){
        try {
            ConSvcEventListener listener = (ConSvcEventListener)
              IConSvcEventListener.Stub.asInterface( new ConSvcEventListener());
            mPid = listener.getCallingPid();
            Log.d(LOG_TAG,"GetLink called with role="+mRole+"pid="+mPid);
            return mService.getLink(mRole,mLinkReqs,mPid,listener);

        } catch ( RemoteException e ) {
            Log.e(LOG_TAG,"ConSvc throwed remoteExcept'n on startConn call");
            return false;
        }
    }

    /** {@hide}
     * This function will be used by apps to report to CnE whether they are
     * satisfied or dissatisfied with the link that was assigned to them.
     * @param info {@code LinkInfo} about the Link assigned to the app. The
     * app needs to pass back the same LinkInfo object it received via the
     * {@code LinkNotifier}
     * @param isSatisfied whether the app is satisfied with the link or not
     * @param isNotifyBetterLink whether the app wants to be notified when
     * another link is available which CnE believes is "better" for the app
     * @return {@code true} if the request has been accepted by Android
     * framework, {@code false} otherwise.
     */
    public boolean reportLinkSatisfaction
    (
      LinkInfo info,
      boolean isSatisfied,
      boolean isNotifyBetterLink
    ){
        try {
            return mService.reportLinkSatisfaction(mRole,
                                                   mPid,
                                                   info,
                                                   isSatisfied,
                                                   isNotifyBetterLink);

        } catch ( RemoteException e ) {
            Log.e(LOG_TAG,"ConSvc throwed remoteExcept'n on reportConnSatis call");
            return false;
        }
    }

    /** {@hide}
     * When a "better link available" notification is delivered to the app,
     * the app has a choice on whether to switch to the new link or continue
     * with the old one. The app needs to call this API if it wants to switch
     * to the new link.
     * @param info {@code LinkInfo} about the new link provided to the app
     * @param isNotifyBetterLink Whether the app wants to be notified if a
     * "better" network for its role is available
     * @return {@code true}if the request has been accepted by Android
     * framework, {@code false} otherwise.
     */
    public boolean
    switchLink(LinkInfo info, boolean isNotifyBetterLink){
        try {
            return mService.switchLink(mRole,
                                       mPid,
                                       info,
                                       isNotifyBetterLink);

        } catch ( RemoteException e ) {
            Log.e(LOG_TAG,"ConSvc throwed remoteExcept'n on reportConnSatis call");
            return false;
        }
    }

    /** {@hide}
     * When a "better link available" notification is delivered to the app,
     * the app has a choice on whether to switch to the new link or continue
     * with the old one. The app needs to call this API if it wants to stay
     * with the old link.
     * @param info {@code LinkInfo} about the new link provided to the app
     * @param isnotifyBetterLink whether the app wants to be notified if a
     * "better" network for its role is available
     * @return {@code true} if the request has been accepted by Android
     * framework, {@code false} otherwise.
     */
    public boolean
    rejectSwitch(LinkInfo info, boolean isNotifyBetterLink){
        try {
            return mService.rejectSwitch(mRole,
                                         mPid,
                                         info,
                                         isNotifyBetterLink);

        } catch ( RemoteException e ) {
            Log.e(LOG_TAG,"ConSvc throwed remoteExcept'n on reportConnSatis call");
            return false;
        }
    }



    /** {@hide}
     * This function will be used by apps to release the network assigned to
     * them for a given role.
     * @return {@code true} if the request has been accepted by Android
     * framework, {@code false} otherwise.
     */
    public boolean releaseLink(){
        try {
            return mService.releaseLink(mRole,mPid);
        } catch ( RemoteException e ) {
            /* print message */
            return false;
        }
    }





    /** {@hide} */
    /* This class has the remoted function call backs that get called
     * when the ConSvc has to notify things to the app
     */
    private class ConSvcEventListener extends IConSvcEventListener.Stub {

        public  void onLinkAvail(LinkInfo info) {
            Log.d(LOG_TAG,"Sending OnLinkAvail with nwId="+info.getNwId()+
                  "to App");
            if(mLinkNotifier != null){
                mLinkNotifier.onLinkAvail(info);
            }
            return;
        }

        public  void onBetterLinkAvail(LinkInfo info) {
            Log.d(LOG_TAG,"Sending onBetterLinkAvail with nwId="+info.getNwId()+
                  "to App");
            if(mLinkNotifier != null){
                mLinkNotifier.onBetterLinkAvail(info);
            }
            return;
        }

        public  void onLinkLost(LinkInfo info) {
            Log.d(LOG_TAG,"Sending onLinkLost with nwId="+info.getNwId()+
                  "to App");
            if(mLinkNotifier != null){
                mLinkNotifier.onLinkLost(info);
            }
            return;
        }

        public  void onGetLinkFailure(int reason) {
            Log.d(LOG_TAG,"Sending onGetLinkFailure with reason="+reason+
                  "to App");
            if(mLinkNotifier != null){
                mLinkNotifier.onGetLinkFailure(reason);
            }
            return;
        }
    };

}

