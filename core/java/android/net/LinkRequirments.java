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

//import java.util.EnumMap;
/** {@hide} */
public class LinkRequirments implements Parcelable {

    private int fwLinkBwReqd;

    private int revLinkBwReqd;

    public LinkRequirments() {
        fwLinkBwReqd = 0;
        revLinkBwReqd = 0;
    }

    /* {@hide} */
    public boolean setBwReq(int upLinkBw, int downLinkBw) {
        boolean retVal = false;
        // validate
        // if(valid){
        fwLinkBwReqd = downLinkBw;
        revLinkBwReqd = upLinkBw;
        retVal = true;
        // }
        return retVal;
    }

    /* {@hide} */
    public int getFwLinkBwReq() {
        return fwLinkBwReqd;
    }

    /* {@hide} */
    public int getRevLinkBwReq() {
        return revLinkBwReqd;
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

        dest.writeInt(fwLinkBwReqd);
        dest.writeInt(revLinkBwReqd);
    }

    /**
     * Implement the Parcelable interface.
     * 
     * @hide
     */
    public void readFromParcel(Parcel in) {
        fwLinkBwReqd = in.readInt();
        revLinkBwReqd = in.readInt();
    }

    /**
     * Implement the Parcelable interface.
     * 
     * @hide
     */
    public static final Creator<LinkRequirments> CREATOR = new Creator<LinkRequirments>() {
        public LinkRequirments createFromParcel(Parcel in) {

            LinkRequirments reqs = new LinkRequirments();
            reqs.fwLinkBwReqd = in.readInt();
            reqs.revLinkBwReqd = in.readInt();
            return reqs;
        }

        public LinkRequirments[] newArray(int size) {
            return new LinkRequirments[size];
        }
    };
}
