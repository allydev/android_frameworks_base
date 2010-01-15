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

import android.os.Parcelable;
import android.os.Parcel;

/** {@hide}
 * This class is used by apps to specify their requirements.  In release 1.0,
 * the only requirements supported are forward and reverse link bandwidth.
 *
 */
public class LinkRequirements implements Parcelable {

    /** forward Link bandwidth requirement in kbps
     */
    private int fwLinkBwReqd;
    /** reverse Link bandwidth requirement in kbps
     */
    private int revLinkBwReqd;

    /** Default constructor that sets the forward and reverse link bandwidth
     *  requirement to zero
     */
    public LinkRequirements(){
        fwLinkBwReqd = 0;
        revLinkBwReqd = 0;
    }

    /** {@hide}
     * Apps can set their forward and reverse link bandwidth requirements
     * using this method.
     * @param upLinkBw requested reverse link bandwidth in kbps
     * @param downLinkBw requested forward link bandwidth in kbps
     * @return {@code true} if the requirement specified by the app is valid
     * and has been accepted by the API, {@code false} otherwise.
     */
    public boolean setBwReq(int upLinkBw, int downLinkBw){
        boolean retVal = false;
            fwLinkBwReqd = downLinkBw;
            revLinkBwReqd = upLinkBw;
            retVal = true;
        return retVal;
    }

    /** {@hide}
     * method to get the forward link bandwidth specified by the app in
     * kbps..
     * @return the forward link bandwidth specified by the app in kbps..
     */
    public int getFwLinkBwReq(){
        return fwLinkBwReqd;
    }

    /** {@hide}
     * method to get the reverse link bandwidth specified by the app in
     * kbps..
     * @return the reverse link bandwidth specified by the app in kbps..
     */
    public int getRevLinkBwReq(){
        return revLinkBwReqd;
    }

    /**
     * Implement the Parcelable interface
     * @hide
     */
    public int describeContents() {
        return 0;
    }

    /**
     * Implement the Parcelable interface.
     * @hide
     */
    public void writeToParcel(Parcel dest, int flags) {

        dest.writeInt(fwLinkBwReqd);
        dest.writeInt(revLinkBwReqd);
    }

    /**
     * Implement the Parcelable interface.
     * @hide
     */
    public void readFromParcel(Parcel in) {
        fwLinkBwReqd = in.readInt();
        revLinkBwReqd = in.readInt();
    }

    /**
     * Implement the Parcelable interface.
     * @hide
     */
    public static final Creator<LinkRequirements> CREATOR =
        new Creator<LinkRequirements>() {
            public LinkRequirements createFromParcel(Parcel in) {

                LinkRequirements reqs = new LinkRequirements();
                reqs.fwLinkBwReqd = in.readInt();
                reqs.revLinkBwReqd = in.readInt();
                return reqs;
            }

            public LinkRequirements[] newArray(int size) {
                return new LinkRequirements[size];
            }
        };
}
