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

import android.os.Parcelable;
import android.os.Parcel;

/** {@hide} */
public class LinkInfo implements Parcelable {

    /* to do decide on ipaddr type and ip address may be we can use byte[16] */
    /* currently android supports only v4 */
    private int ipAddr;

    private int availFwLinkBw;

    private int availRevLinkBw;

    private int nwId;

    public static final int INF_UNSPECIFIED = -1;

    public static final int STATUS_FAILURE = 0;

    public static final int STATUS_SUCCESS = 1;

    public LinkInfo() {
        ipAddr = INF_UNSPECIFIED;
        availFwLinkBw = INF_UNSPECIFIED;
        availRevLinkBw = INF_UNSPECIFIED;
        nwId = INF_UNSPECIFIED;
    }

    public LinkInfo(int ip, int fwLinkBw, int revLinkBw, int netId) {
        ipAddr = ip;
        availFwLinkBw = fwLinkBw;
        availRevLinkBw = revLinkBw;
        nwId = netId;
    }

    /**
     * @hide
     */
    public int getIpAddr() {
        return ipAddr;
    }

    /**
     * @hide
     */
    public int getAvailFwLinkBw() {
        return availFwLinkBw;
    }

    /**
     * @hide
     */
    public int getAvailRevLinkBw() {
        return availRevLinkBw;
    }

    /**
     * @hide
     */
    public int getNwId() {
        return nwId;
    }

    /**
     * Implement the Parcelable interface
     * 
     * @hide
     */
    public int describeContents() {
        return 0;
    }

    /**
     * Implement the Parcelable interface.
     * 
     * @hide
     */
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(ipAddr);
        dest.writeInt(availFwLinkBw);
        dest.writeInt(availRevLinkBw);
        dest.writeInt(nwId);
    }

    /**
     * Implement the Parcelable interface.
     * 
     * @hide
     */
    public void readFromParcel(Parcel in) {
        ipAddr = in.readInt();
        availFwLinkBw = in.readInt();
        availRevLinkBw = in.readInt();
        nwId = in.readInt();
    }

    /**
     * Implement the Parcelable interface.
     * 
     * @hide
     */
    public static final Creator<LinkInfo> CREATOR = new Creator<LinkInfo>() {
        public LinkInfo createFromParcel(Parcel in) {
            int ipAddr = in.readInt();
            int availFwLinkBw = in.readInt();
            int availRevLinkBw = in.readInt();
            int nwId = in.readInt();
            LinkInfo info = new LinkInfo(ipAddr, availFwLinkBw, availRevLinkBw, nwId);
            return info;
        }

        public LinkInfo[] newArray(int size) {
            return new LinkInfo[size];
        }
    };
}
