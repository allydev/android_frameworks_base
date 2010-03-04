/*
 * Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *        * Redistributions of source code must retain the above copyright
 *            notice, this list of conditions and the following disclaimer.
 *        * Redistributions in binary form must reproduce the above copyright
 *            notice, this list of conditions and the following disclaimer in the
 *            documentation and/or other materials provided with the distribution.
 *        * Neither the name of Code Aurora nor
 *            the names of its contributors may be used to endorse or promote
 *            products derived from this software without specific prior written
 *            permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.    IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOGI printf
#define LOG_TAG "fmradio"

#include "jni.h"
#include "nativehelper/JNIHelp.h"
#include "utils/Log.h"
#include "utils/misc.h"
#include "android_runtime/AndroidRuntime.h"
#include <cutils/properties.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <media/tavarua.h>
#include <linux/videodev2.h>
#include <math.h>

#define RADIO "/dev/radio0"
#define FM_JNI_SUCCESS 0L
#define FM_JNI_FAILURE -1L
#define SEARCH_DOWN 0
#define SEARCH_UP 1
#define TUNE_MULT 16000
enum search_dir_t {
    SEEK_UP,
    SEEK_DN,
    SCAN_UP,
    SCAN_DN
};

using namespace android;

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_acquireFdNative
        (JNIEnv* env, jobject thiz, jstring path)
{
    int fd;
    int i;
    char value = 0;
    int init_success = 0;
    jboolean isCopy;
    const char* radio_path = env->GetStringUTFChars(path, &isCopy);
    if(radio_path == NULL){
        return FM_JNI_FAILURE;
    }
    fd = open(radio_path, O_RDONLY, O_NONBLOCK);
    if(isCopy == JNI_TRUE){
        env->ReleaseStringUTFChars(path, radio_path);
    }
    if(fd < 0){
        return FM_JNI_FAILURE;
    }
    property_set("ctl.start", "fm_dl");
    sleep(1);
    for(i=0;i<3;i++) {
        property_get("hw.fm.init", &value, NULL);
	if(value == '1') {
            init_success = 1;
            break;
        } else {
            sleep(1);
        }
    }
    LOGE("init_success:%d after %d seconds \n", init_success, i);
    if(!init_success) {
        property_set("ctl.stop", "fm_dl");
	// close the fd(power down)
	close(fd);
        return FM_JNI_FAILURE;
    }

    return fd;
}

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_closeFdNative
    (JNIEnv * env, jobject thiz, jint fd)
{
    property_set("ctl.stop", "fm_dl");
    close(fd);
    return FM_JNI_SUCCESS;
}

/********************************************************************
 * Current JNI
 *******************************************************************/

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_getFreqNative
    (JNIEnv * env, jobject thiz, jint fd)
{
    int err;
    struct v4l2_frequency freq;
    freq.type = V4L2_TUNER_RADIO;
    err = ioctl(fd, VIDIOC_G_FREQUENCY, &freq);
    if(err < 0){
      return FM_JNI_FAILURE;
    }
    return ((freq.frequency*1000)/TUNE_MULT);
}

/*native interface */
static jint android_hardware_fmradio_FmReceiverJNI_setFreqNative
    (JNIEnv * env, jobject thiz, jint fd, jint freq)
{
    int err;
    double tune;
    struct v4l2_frequency freq_struct;
    freq_struct.type = V4L2_TUNER_RADIO;
    freq_struct.frequency = (freq*TUNE_MULT/1000);
    err = ioctl(fd, VIDIOC_S_FREQUENCY, &freq_struct);
    if(err < 0){
            return FM_JNI_FAILURE;
    }
    return FM_JNI_SUCCESS;
}

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_setControlNative
    (JNIEnv * env, jobject thiz, jint fd, jint id, jint value)
{
    struct v4l2_control control;
    int i;
    int err;
    LOGE("id(%x) value: %x\n", id, value);
    control.value = value;

    control.id = id;
    for(i=0;i<3;i++) {
        err = ioctl(fd,VIDIOC_S_CTRL,&control);
        if(err >= 0){
            return FM_JNI_SUCCESS;
        }
    }

    return FM_JNI_FAILURE;
}

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_getControlNative
    (JNIEnv * env, jobject thiz, jint fd, jint id)
{
    struct v4l2_control control;
    int err;
    LOGE("id(%x)\n", id);

    control.id = id;
    err = ioctl(fd,VIDIOC_G_CTRL,&control);
    if(err < 0){
        return FM_JNI_FAILURE;
    }
    return control.value;
}

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_startSearchNative
    (JNIEnv * env, jobject thiz, jint fd, jint dir)
{
    struct v4l2_hw_freq_seek hw_seek;
    int err;
    hw_seek.seek_upward = dir;
    err = ioctl(fd,VIDIOC_S_HW_FREQ_SEEK,&hw_seek);
    if(err < 0){
        return FM_JNI_FAILURE;
    }
    return FM_JNI_SUCCESS;
}

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_cancelSearchNative
    (JNIEnv * env, jobject thiz, jint fd)
{
    struct v4l2_control control;
    int err;
    control.id=V4L2_CID_PRIVATE_TAVARUA_SRCHON;
    control.value=0;
    err = ioctl(fd,VIDIOC_S_CTRL,&control);
    if(err < 0){
        return FM_JNI_FAILURE;
    }
    return FM_JNI_SUCCESS;
}

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_getRSSINative
    (JNIEnv * env, jobject thiz, jint fd)
{
    struct v4l2_tuner tuner;
    int err;

    tuner.index = 0;
    tuner.signal = 0;
    err = ioctl(fd, VIDIOC_G_TUNER, &tuner);
    if(err < 0){
        return FM_JNI_FAILURE;
    }
    return tuner.signal;
}

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_setBandNative
    (JNIEnv * env, jobject thiz, jint fd, jint low, jint high)
{
    struct v4l2_tuner tuner;
    int err;

    tuner.index = 0;
    tuner.signal = 0;
    tuner.rangelow = low * (TUNE_MULT/1000);
    tuner.rangehigh = high * (TUNE_MULT/1000);
    err = ioctl(fd, VIDIOC_S_TUNER, &tuner);
    if(err < 0){
        return FM_JNI_FAILURE;
    }
    return FM_JNI_SUCCESS;
}

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_getLowerBandNative
    (JNIEnv * env, jobject thiz, jint fd)
{
    struct v4l2_tuner tuner;
    int err;
    tuner.index = 0;

    err = ioctl(fd, VIDIOC_G_TUNER, &tuner);
    if(err < 0){
        LOGE("low_band value: <%x> \n", tuner.rangelow);
        return FM_JNI_FAILURE;
    }
    return ((tuner.rangelow * 1000)/ TUNE_MULT);
}

static jint android_hardware_fmradio_FmReceiverJNI_setMonoStereoNative
    (JNIEnv * env, jobject thiz, jint fd, jint val)
{

    struct v4l2_tuner tuner;
    int err;

    tuner.index = 0;
    err = ioctl(fd, VIDIOC_G_TUNER, &tuner);

    if(err < 0)
        return FM_JNI_FAILURE;

    tuner.audmode = val;
    err = ioctl(fd, VIDIOC_S_TUNER, &tuner);

    if(err < 0)
        return FM_JNI_FAILURE;

    return FM_JNI_SUCCESS;

}



/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_getBufferNative
 (JNIEnv * env, jobject thiz, jint fd, jbooleanArray buff, jint index)
{
    int err;
    jboolean isCopy;
    struct v4l2_requestbuffers reqbuf;
    struct v4l2_buffer v4l2_buf;
    memset(&reqbuf, 0, sizeof (reqbuf));
    enum v4l2_buf_type type = V4L2_BUF_TYPE_PRIVATE;
    reqbuf.type = V4L2_BUF_TYPE_PRIVATE;
    reqbuf.memory = V4L2_MEMORY_USERPTR;
    jboolean *bool_buffer = env->GetBooleanArrayElements(buff,&isCopy);
    memset(&v4l2_buf, 0, sizeof (v4l2_buf));
    v4l2_buf.index = index;
    v4l2_buf.type = type;
    v4l2_buf.length = 128;
    v4l2_buf.m.userptr = (unsigned long)bool_buffer;
    err = ioctl(fd,VIDIOC_DQBUF,&v4l2_buf) ;
    if(err < 0){
        return FM_JNI_FAILURE;
    }

    /* Always copy buffer and free up the memory */
    env->ReleaseBooleanArrayElements(buff, bool_buffer, 0);

    return v4l2_buf.bytesused;
}

/* native interface */
static jint android_hardware_fmradio_FmReceiverJNI_getRawRdsNative
 (JNIEnv * env, jobject thiz, jint fd, jbooleanArray buff, jint count)
{

    return (read (fd, buff, count));

}

/*
 * JNI registration.
 */
static JNINativeMethod gMethods[] = {
        /* name, signature, funcPtr */
        { "acquireFdNative", "(Ljava/lang/String;)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_acquireFdNative},
        { "closeFdNative", "(I)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_closeFdNative},
        { "getFreqNative", "(I)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_getFreqNative},
        { "setFreqNative", "(II)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_setFreqNative},
        { "getControlNative", "(II)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_getControlNative},
        { "setControlNative", "(III)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_setControlNative},
        { "startSearchNative", "(II)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_startSearchNative},
        { "cancelSearchNative", "(I)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_cancelSearchNative},
        { "getRSSINative", "(I)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_getRSSINative},
        { "setBandNative", "(III)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_setBandNative},
        { "getLowerBandNative", "(I)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_getLowerBandNative},
        { "getBufferNative", "(I[BI)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_getBufferNative},
        { "setMonoStereoNative", "(II)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_setMonoStereoNative},
        { "getRawRdsNative", "(I[BI)I",
            (void*)android_hardware_fmradio_FmReceiverJNI_getRawRdsNative},
};

int register_android_hardware_fm_fmradio(JNIEnv* env)
{
        return jniRegisterNativeMethods(env, "android/hardware/fmradio/FmReceiverJNI", gMethods, NELEM(gMethods));
}
