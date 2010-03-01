/*
 * Copyright (C) 2009 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "AwesomePlayer"
#include <utils/Log.h>

#include <dlfcn.h>

#include "include/AwesomePlayer.h"
#include "include/Prefetcher.h"
#include "include/SoftwareRenderer.h"

#include <binder/IPCThreadState.h>
#include <media/stagefright/AudioPlayer.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/OMXCodec.h>

#include <surfaceflinger/ISurface.h>

namespace android {

struct AwesomeEvent : public TimedEventQueue::Event {
    AwesomeEvent(
            AwesomePlayer *player,
            void (AwesomePlayer::*method)())
        : mPlayer(player),
          mMethod(method) {
    }

protected:
    virtual ~AwesomeEvent() {}

    virtual void fire(TimedEventQueue *queue, int64_t /* now_us */) {
        (mPlayer->*mMethod)();
    }

private:
    AwesomePlayer *mPlayer;
    void (AwesomePlayer::*mMethod)();

    AwesomeEvent(const AwesomeEvent &);
    AwesomeEvent &operator=(const AwesomeEvent &);
};

struct AwesomeRemoteRenderer : public AwesomeRenderer {
    AwesomeRemoteRenderer(const sp<IOMXRenderer> &target)
        : mTarget(target) {
    }

    virtual void render(MediaBuffer *buffer) {
        void *id;
        if (buffer->meta_data()->findPointer(kKeyBufferID, &id)) {
            mTarget->render((IOMX::buffer_id)id);
        }
    }

private:
    sp<IOMXRenderer> mTarget;

    AwesomeRemoteRenderer(const AwesomeRemoteRenderer &);
    AwesomeRemoteRenderer &operator=(const AwesomeRemoteRenderer &);
};

struct AwesomeLocalRenderer : public AwesomeRenderer {
    AwesomeLocalRenderer(
            bool previewOnly,
            const char *componentName,
            OMX_COLOR_FORMATTYPE colorFormat,
            const sp<ISurface> &surface,
            size_t displayWidth, size_t displayHeight,
            size_t decodedWidth, size_t decodedHeight)
        : mTarget(NULL),
          mLibHandle(NULL) {
            init(previewOnly, componentName,
                 colorFormat, surface, displayWidth,
                 displayHeight, decodedWidth, decodedHeight);
    }

    virtual void render(MediaBuffer *buffer) {
        render((const uint8_t *)buffer->data() + buffer->range_offset(),
               buffer->range_length());
    }

    void render(const void *data, size_t size) {
        mTarget->render(data, size, NULL);
    }

protected:
    virtual ~AwesomeLocalRenderer() {
        delete mTarget;
        mTarget = NULL;

        if (mLibHandle) {
            dlclose(mLibHandle);
            mLibHandle = NULL;
        }
    }

private:
    VideoRenderer *mTarget;
    void *mLibHandle;

    void init(
            bool previewOnly,
            const char *componentName,
            OMX_COLOR_FORMATTYPE colorFormat,
            const sp<ISurface> &surface,
            size_t displayWidth, size_t displayHeight,
            size_t decodedWidth, size_t decodedHeight);

    AwesomeLocalRenderer(const AwesomeLocalRenderer &);
    AwesomeLocalRenderer &operator=(const AwesomeLocalRenderer &);;
};

void AwesomeLocalRenderer::init(
        bool previewOnly,
        const char *componentName,
        OMX_COLOR_FORMATTYPE colorFormat,
        const sp<ISurface> &surface,
        size_t displayWidth, size_t displayHeight,
        size_t decodedWidth, size_t decodedHeight) {
    if (!previewOnly) {
        // We will stick to the vanilla software-color-converting renderer
        // for "previewOnly" mode, to avoid unneccessarily switching overlays
        // more often than necessary.

        mLibHandle = dlopen("libstagefrighthw.so", RTLD_NOW);

        if (mLibHandle) {
            typedef VideoRenderer *(*CreateRendererFunc)(
                    const sp<ISurface> &surface,
                    const char *componentName,
                    OMX_COLOR_FORMATTYPE colorFormat,
                    size_t displayWidth, size_t displayHeight,
                    size_t decodedWidth, size_t decodedHeight);

            CreateRendererFunc func =
                (CreateRendererFunc)dlsym(
                        mLibHandle,
                        "_Z14createRendererRKN7android2spINS_8ISurfaceEEEPKc20"
                        "OMX_COLOR_FORMATTYPEjjjj");

            if (func) {
                mTarget =
                    (*func)(surface, componentName, colorFormat,
                        displayWidth, displayHeight,
                        decodedWidth, decodedHeight);
            }
        }
    }

    if (mTarget == NULL) {
        mTarget = new SoftwareRenderer(
                colorFormat, surface, displayWidth, displayHeight,
                decodedWidth, decodedHeight);
    }
}

AwesomePlayer::AwesomePlayer()
    : mQueueStarted(false),
      mTimeSource(NULL),
      mVideoRendererIsPreview(false),
      mAudioPlayer(NULL),
      mFlags(0),
      mLastVideoBuffer(NULL),
      mVideoBuffer(NULL),
      mSuspensionState(NULL) {
    CHECK_EQ(mClient.connect(), OK);

    DataSource::RegisterDefaultSniffers();

    mVideoEvent = new AwesomeEvent(this, &AwesomePlayer::onVideoEvent);
    mVideoEventPending = false;
    mStreamDoneEvent = new AwesomeEvent(this, &AwesomePlayer::onStreamDone);
    mStreamDoneEventPending = false;
    mBufferingEvent = new AwesomeEvent(this, &AwesomePlayer::onBufferingUpdate);
    mBufferingEventPending = false;

    mCheckAudioStatusEvent = new AwesomeEvent(
            this, &AwesomePlayer::onCheckAudioStatus);

    mAudioStatusEventPending = false;

    reset();
}

AwesomePlayer::~AwesomePlayer() {
    if (mQueueStarted) {
        mQueue.stop();
    }

    reset();

    mClient.disconnect();
}

void AwesomePlayer::cancelPlayerEvents(bool keepBufferingGoing) {
    mQueue.cancelEvent(mVideoEvent->eventID());
    mVideoEventPending = false;
    mQueue.cancelEvent(mStreamDoneEvent->eventID());
    mStreamDoneEventPending = false;
    mQueue.cancelEvent(mCheckAudioStatusEvent->eventID());
    mAudioStatusEventPending = false;

    if (!keepBufferingGoing) {
        mQueue.cancelEvent(mBufferingEvent->eventID());
        mBufferingEventPending = false;
    }
}

void AwesomePlayer::setListener(const wp<MediaPlayerBase> &listener) {
    Mutex::Autolock autoLock(mLock);
    mListener = listener;
}

status_t AwesomePlayer::setDataSource(
        const char *uri, const KeyedVector<String8, String8> *headers) {
    Mutex::Autolock autoLock(mLock);
    return setDataSource_l(uri, headers);
}

status_t AwesomePlayer::setDataSource_l(
        const char *uri, const KeyedVector<String8, String8> *headers) {
    reset_l();

    mUri = uri;

    if (headers) {
        mUriHeaders = *headers;
    }

    // The actual work will be done during preparation in the call to
    // ::finishSetDataSource_l to avoid blocking the calling thread in
    // setDataSource for any significant time.

    return OK;
}

status_t AwesomePlayer::setDataSource(
        int fd, int64_t offset, int64_t length) {
    Mutex::Autolock autoLock(mLock);

    reset_l();

    sp<DataSource> dataSource = new FileSource(fd, offset, length);

    status_t err = dataSource->initCheck();

    if (err != OK) {
        return err;
    }

    mFileSource = dataSource;

    return setDataSource_l(dataSource);
}

status_t AwesomePlayer::setDataSource_l(
        const sp<DataSource> &dataSource) {
    sp<MediaExtractor> extractor = MediaExtractor::Create(dataSource);

    if (extractor == NULL) {
        return UNKNOWN_ERROR;
    }

    return setDataSource_l(extractor);
}

status_t AwesomePlayer::setDataSource_l(const sp<MediaExtractor> &extractor) {
    bool haveAudio = false;
    bool haveVideo = false;
    for (size_t i = 0; i < extractor->countTracks(); ++i) {
        sp<MetaData> meta = extractor->getTrackMetaData(i);

        const char *mime;
        CHECK(meta->findCString(kKeyMIMEType, &mime));

        if (!haveVideo && !strncasecmp(mime, "video/", 6)) {
            if (setVideoSource(extractor->getTrack(i)) == OK) {
                haveVideo = true;
            }
        } else if (!haveAudio && !strncasecmp(mime, "audio/", 6)) {
            if (setAudioSource(extractor->getTrack(i)) == OK) {
                haveAudio = true;
            }
        }

        if (haveAudio && haveVideo) {
            break;
        }
    }

    return !haveAudio && !haveVideo ? UNKNOWN_ERROR : OK;
}

void AwesomePlayer::reset() {
    Mutex::Autolock autoLock(mLock);
    reset_l();
}

void AwesomePlayer::reset_l() {
    while (mFlags & PREPARING) {
        mPreparedCondition.wait(mLock);
    }

    cancelPlayerEvents();

    if (mPrefetcher != NULL) {
        CHECK_EQ(mPrefetcher->getStrongCount(), 1);
    }
    mPrefetcher.clear();

    // Shutdown audio first, so that the respone to the reset request
    // appears to happen instantaneously as far as the user is concerned
    // If we did this later, audio would continue playing while we
    // shutdown the video-related resources and the player appear to
    // not be as responsive to a reset request.
    mAudioSource.clear();

    if (mTimeSource != mAudioPlayer) {
        delete mTimeSource;
    }
    mTimeSource = NULL;

    delete mAudioPlayer;
    mAudioPlayer = NULL;

    mVideoRenderer.clear();

    if (mLastVideoBuffer) {
        mLastVideoBuffer->release();
        mLastVideoBuffer = NULL;
    }

    if (mVideoBuffer) {
        mVideoBuffer->release();
        mVideoBuffer = NULL;
    }

    if (mVideoSource != NULL) {
        mVideoSource->stop();

        // The following hack is necessary to ensure that the OMX
        // component is completely released by the time we may try
        // to instantiate it again.
        wp<MediaSource> tmp = mVideoSource;
        mVideoSource.clear();
        while (tmp.promote() != NULL) {
            usleep(1000);
        }
        IPCThreadState::self()->flushCommands();
    }

    mDurationUs = -1;
    mFlags = 0;
    mVideoWidth = mVideoHeight = -1;
    mTimeSourceDeltaUs = 0;
    mVideoTimeUs = 0;

    mSeeking = false;
    mSeekTimeUs = 0;

    mUri.setTo("");
    mUriHeaders.clear();

    mFileSource.clear();

    delete mSuspensionState;
    mSuspensionState = NULL;
}

void AwesomePlayer::notifyListener_l(int msg, int ext1, int ext2) {
    if (mListener != NULL) {
        sp<MediaPlayerBase> listener = mListener.promote();

        if (listener != NULL) {
            listener->sendEvent(msg, ext1, ext2);
        }
    }
}

void AwesomePlayer::onBufferingUpdate() {
    Mutex::Autolock autoLock(mLock);
    if (!mBufferingEventPending) {
        return;
    }
    mBufferingEventPending = false;

    if (mDurationUs >= 0) {
        int64_t cachedDurationUs = mPrefetcher->getCachedDurationUs();
        int64_t positionUs = 0;
        if (mVideoSource != NULL) {
            positionUs = mVideoTimeUs;
        } else if (mAudioPlayer != NULL) {
            positionUs = mAudioPlayer->getMediaTimeUs();
        }

        cachedDurationUs += positionUs;

        double percentage = (double)cachedDurationUs / mDurationUs;
        notifyListener_l(MEDIA_BUFFERING_UPDATE, percentage * 100.0);

        postBufferingEvent_l();
    }
}

void AwesomePlayer::onStreamDone() {
    // Posted whenever any stream finishes playing.

    Mutex::Autolock autoLock(mLock);
    if (!mStreamDoneEventPending) {
        return;
    }
    mStreamDoneEventPending = false;

    if (mStreamDoneStatus == ERROR_END_OF_STREAM && (mFlags & LOOPING)) {
        seekTo_l(0);

        if (mVideoSource != NULL) {
            postVideoEvent_l();
        }
    } else {
        if (mStreamDoneStatus == ERROR_END_OF_STREAM) {
            LOGV("MEDIA_PLAYBACK_COMPLETE");
            notifyListener_l(MEDIA_PLAYBACK_COMPLETE);
        } else {
            LOGV("MEDIA_ERROR %d", mStreamDoneStatus);

            notifyListener_l(
                    MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, mStreamDoneStatus);
        }

        pause_l();

        mFlags |= AT_EOS;
    }
}

status_t AwesomePlayer::play() {
    Mutex::Autolock autoLock(mLock);
    return play_l();
}

status_t AwesomePlayer::play_l() {
    if (mFlags & PLAYING) {
        return OK;
    }

    if (!(mFlags & PREPARED)) {
        status_t err = prepare_l();

        if (err != OK) {
            return err;
        }
    }

    mFlags |= PLAYING;
    mFlags |= FIRST_FRAME;

    bool deferredAudioSeek = false;

    if (mAudioSource != NULL) {
        if (mAudioPlayer == NULL) {
            if (mAudioSink != NULL) {
                mAudioPlayer = new AudioPlayer(mAudioSink);
                mAudioPlayer->setSource(mAudioSource);
                status_t err = mAudioPlayer->start();

                if (err != OK) {
                    delete mAudioPlayer;
                    mAudioPlayer = NULL;

                    mFlags &= ~(PLAYING | FIRST_FRAME);

                    return err;
                }

                delete mTimeSource;
                mTimeSource = mAudioPlayer;

                deferredAudioSeek = true;

                mWatchForAudioSeekComplete = false;
                mWatchForAudioEOS = true;
            }
        } else {
            mAudioPlayer->resume();
        }

        postCheckAudioStatusEvent_l();
    }

    if (mTimeSource == NULL && mAudioPlayer == NULL) {
        mTimeSource = new SystemTimeSource;
    }

    if (mVideoSource != NULL) {
        // Kick off video playback
        postVideoEvent_l();
    }

    if (deferredAudioSeek) {
        // If there was a seek request while we were paused
        // and we're just starting up again, honor the request now.
        seekAudioIfNecessary_l();
    }

    postBufferingEvent_l();

    if (mFlags & AT_EOS) {
        // Legacy behaviour, if a stream finishes playing and then
        // is started again, we play from the start...
        seekTo_l(0);
    }

    return OK;
}

void AwesomePlayer::initRenderer_l() {
    if (mISurface != NULL) {
        sp<MetaData> meta = mVideoSource->getFormat();

        int32_t format;
        const char *component;
        int32_t decodedWidth, decodedHeight;
        CHECK(meta->findInt32(kKeyColorFormat, &format));
        CHECK(meta->findCString(kKeyDecoderComponent, &component));
        CHECK(meta->findInt32(kKeyWidth, &decodedWidth));
        CHECK(meta->findInt32(kKeyHeight, &decodedHeight));

        mVideoRenderer.clear();

        // Must ensure that mVideoRenderer's destructor is actually executed
        // before creating a new one.
        IPCThreadState::self()->flushCommands();

        if (!strncmp("OMX.", component, 4)) {
            // Our OMX codecs allocate buffers on the media_server side
            // therefore they require a remote IOMXRenderer that knows how
            // to display them.
            mVideoRenderer = new AwesomeRemoteRenderer(
                mClient.interface()->createRenderer(
                        mISurface, component,
                        (OMX_COLOR_FORMATTYPE)format,
                        decodedWidth, decodedHeight,
                        mVideoWidth, mVideoHeight));
        } else {
            // Other decoders are instantiated locally and as a consequence
            // allocate their buffers in local address space.
            mVideoRenderer = new AwesomeLocalRenderer(
                false,  // previewOnly
                component,
                (OMX_COLOR_FORMATTYPE)format,
                mISurface,
                mVideoWidth, mVideoHeight,
                decodedWidth, decodedHeight);
        }
    }
}

status_t AwesomePlayer::pause() {
    Mutex::Autolock autoLock(mLock);
    return pause_l();
}

status_t AwesomePlayer::pause_l() {
    if (!(mFlags & PLAYING)) {
        return OK;
    }

    cancelPlayerEvents(true /* keepBufferingGoing */);

    if (mAudioPlayer != NULL) {
        mAudioPlayer->pause();
    }

    mFlags &= ~PLAYING;

    return OK;
}

bool AwesomePlayer::isPlaying() const {
    Mutex::Autolock autoLock(mLock);

    return mFlags & PLAYING;
}

void AwesomePlayer::setISurface(const sp<ISurface> &isurface) {
    Mutex::Autolock autoLock(mLock);

    mISurface = isurface;
}

void AwesomePlayer::setAudioSink(
        const sp<MediaPlayerBase::AudioSink> &audioSink) {
    Mutex::Autolock autoLock(mLock);

    mAudioSink = audioSink;
}

status_t AwesomePlayer::setLooping(bool shouldLoop) {
    Mutex::Autolock autoLock(mLock);

    mFlags = mFlags & ~LOOPING;

    if (shouldLoop) {
        mFlags |= LOOPING;
    }

    return OK;
}

status_t AwesomePlayer::getDuration(int64_t *durationUs) {
    Mutex::Autolock autoLock(mLock);

    if (mDurationUs < 0) {
        return UNKNOWN_ERROR;
    }

    *durationUs = mDurationUs;

    return OK;
}

status_t AwesomePlayer::getPosition(int64_t *positionUs) {
    Mutex::Autolock autoLock(mLock);
    return getPosition_l(positionUs);
}

status_t AwesomePlayer::getPosition_l(int64_t *positionUs) {
    if (mVideoSource != NULL) {
        *positionUs = mVideoTimeUs;
    } else if (mAudioPlayer != NULL) {
        *positionUs = mAudioPlayer->getMediaTimeUs();
    } else {
        *positionUs = 0;
    }

    return OK;
}

status_t AwesomePlayer::seekTo(int64_t timeUs) {
    Mutex::Autolock autoLock(mLock);
    return seekTo_l(timeUs);
}

status_t AwesomePlayer::seekTo_l(int64_t timeUs) {
    mSeeking = true;
    mSeekTimeUs = timeUs;
    mFlags &= ~AT_EOS;

    seekAudioIfNecessary_l();

    return OK;
}

void AwesomePlayer::seekAudioIfNecessary_l() {
    if (mSeeking && mVideoSource == NULL && mAudioPlayer != NULL) {
        mAudioPlayer->seekTo(mSeekTimeUs);

        mWatchForAudioSeekComplete = true;
        mWatchForAudioEOS = true;
        mSeeking = false;
    }
}

status_t AwesomePlayer::getVideoDimensions(
        int32_t *width, int32_t *height) const {
    Mutex::Autolock autoLock(mLock);

    if (mVideoWidth < 0 || mVideoHeight < 0) {
        return UNKNOWN_ERROR;
    }

    *width = mVideoWidth;
    *height = mVideoHeight;

    return OK;
}

status_t AwesomePlayer::setAudioSource(sp<MediaSource> source) {
    if (source == NULL) {
        return UNKNOWN_ERROR;
    }

    if (mPrefetcher != NULL) {
        source = mPrefetcher->addSource(source);
    }

    sp<MetaData> meta = source->getFormat();

    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));

    if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_RAW)) {
        mAudioSource = source;
    } else {
        mAudioSource = OMXCodec::Create(
                mClient.interface(), source->getFormat(),
                false, // createEncoder
                source);
    }

    if (mAudioSource != NULL) {
        int64_t durationUs;
        if (source->getFormat()->findInt64(kKeyDuration, &durationUs)) {
            if (mDurationUs < 0 || durationUs > mDurationUs) {
                mDurationUs = durationUs;
            }
        }
    }

    return mAudioSource != NULL ? OK : UNKNOWN_ERROR;
}

status_t AwesomePlayer::setVideoSource(sp<MediaSource> source) {
    if (source == NULL) {
        return UNKNOWN_ERROR;
    }

    if (mPrefetcher != NULL) {
        source = mPrefetcher->addSource(source);
    }

    mVideoSource = OMXCodec::Create(
            mClient.interface(), source->getFormat(),
            false, // createEncoder
            source);

    if (mVideoSource != NULL) {
        int64_t durationUs;
        if (source->getFormat()->findInt64(kKeyDuration, &durationUs)) {
            if (mDurationUs < 0 || durationUs > mDurationUs) {
                mDurationUs = durationUs;
            }
        }

        CHECK(source->getFormat()->findInt32(kKeyWidth, &mVideoWidth));
        CHECK(source->getFormat()->findInt32(kKeyHeight, &mVideoHeight));

        mVideoSource->start();
    }

    return mVideoSource != NULL ? OK : UNKNOWN_ERROR;
}

void AwesomePlayer::onVideoEvent() {
    Mutex::Autolock autoLock(mLock);
    if (!mVideoEventPending) {
        // The event has been cancelled in reset_l() but had already
        // been scheduled for execution at that time.
        return;
    }
    mVideoEventPending = false;

    if (mSeeking) {
        if (mLastVideoBuffer) {
            mLastVideoBuffer->release();
            mLastVideoBuffer = NULL;
        }

        if (mVideoBuffer) {
            mVideoBuffer->release();
            mVideoBuffer = NULL;
        }
    }

    if (!mVideoBuffer) {
        MediaSource::ReadOptions options;
        if (mSeeking) {
            LOGV("seeking to %lld us (%.2f secs)", mSeekTimeUs, mSeekTimeUs / 1E6);

            options.setSeekTo(mSeekTimeUs);
        }
        for (;;) {
            status_t err = mVideoSource->read(&mVideoBuffer, &options);
            options.clearSeekTo();

            if (err != OK) {
                CHECK_EQ(mVideoBuffer, NULL);

                if (err == INFO_FORMAT_CHANGED) {
                    LOGV("VideoSource signalled format change.");

                    if (mVideoRenderer != NULL) {
                        mVideoRendererIsPreview = false;
                        initRenderer_l();
                    }
                    continue;
                }

                postStreamDoneEvent_l(err);
                return;
            }

            if (mVideoBuffer->range_length() == 0) {
                // Some decoders, notably the PV AVC software decoder
                // return spurious empty buffers that we just want to ignore.

                mVideoBuffer->release();
                mVideoBuffer = NULL;
                continue;
            }

            break;
        }
    }

    int64_t timeUs;
    CHECK(mVideoBuffer->meta_data()->findInt64(kKeyTime, &timeUs));

    mVideoTimeUs = timeUs;

    if (mSeeking) {
        if (mAudioPlayer != NULL) {
            LOGV("seeking audio to %lld us (%.2f secs).", timeUs, timeUs / 1E6);

            mAudioPlayer->seekTo(timeUs);
            mWatchForAudioSeekComplete = true;
            mWatchForAudioEOS = true;
        } else {
            // If we're playing video only, report seek complete now,
            // otherwise audio player will notify us later.
            notifyListener_l(MEDIA_SEEK_COMPLETE);
        }

        mFlags |= FIRST_FRAME;
        mSeeking = false;
    }

    if (mFlags & FIRST_FRAME) {
        mFlags &= ~FIRST_FRAME;

        mTimeSourceDeltaUs = mTimeSource->getRealTimeUs() - timeUs;
    }

    int64_t realTimeUs, mediaTimeUs;
    if (mAudioPlayer != NULL
        && mAudioPlayer->getMediaTimeMapping(&realTimeUs, &mediaTimeUs)) {
        mTimeSourceDeltaUs = realTimeUs - mediaTimeUs;
    }

    int64_t nowUs = mTimeSource->getRealTimeUs() - mTimeSourceDeltaUs;

    int64_t latenessUs = nowUs - timeUs;

    if (latenessUs > 40000) {
        // We're more than 40ms late.
        LOGV("we're late by %lld us (%.2f secs)", latenessUs, latenessUs / 1E6);

        mVideoBuffer->release();
        mVideoBuffer = NULL;

        postVideoEvent_l();
        return;
    }

    if (latenessUs < -10000) {
        // We're more than 10ms early.

        postVideoEvent_l(10000);
        return;
    }

    if (mVideoRendererIsPreview || mVideoRenderer == NULL) {
        mVideoRendererIsPreview = false;

        initRenderer_l();
    }

    if (mVideoRenderer != NULL) {
        mVideoRenderer->render(mVideoBuffer);
    }

    if (mLastVideoBuffer) {
        mLastVideoBuffer->release();
        mLastVideoBuffer = NULL;
    }
    mLastVideoBuffer = mVideoBuffer;
    mVideoBuffer = NULL;

    postVideoEvent_l();
}

void AwesomePlayer::postVideoEvent_l(int64_t delayUs) {
    if (mVideoEventPending) {
        return;
    }

    mVideoEventPending = true;
    mQueue.postEventWithDelay(mVideoEvent, delayUs < 0 ? 10000 : delayUs);
}

void AwesomePlayer::postStreamDoneEvent_l(status_t status) {
    if (mStreamDoneEventPending) {
        return;
    }
    mStreamDoneEventPending = true;

    mStreamDoneStatus = status;
    mQueue.postEvent(mStreamDoneEvent);
}

void AwesomePlayer::postBufferingEvent_l() {
    if (mPrefetcher == NULL) {
        return;
    }

    if (mBufferingEventPending) {
        return;
    }
    mBufferingEventPending = true;
    mQueue.postEventWithDelay(mBufferingEvent, 1000000ll);
}

void AwesomePlayer::postCheckAudioStatusEvent_l() {
    if (mAudioStatusEventPending) {
        return;
    }
    mAudioStatusEventPending = true;
    mQueue.postEventWithDelay(mCheckAudioStatusEvent, 100000ll);
}

void AwesomePlayer::onCheckAudioStatus() {
    Mutex::Autolock autoLock(mLock);
    if (!mAudioStatusEventPending) {
        // Event was dispatched and while we were blocking on the mutex,
        // has already been cancelled.
        return;
    }

    mAudioStatusEventPending = false;

    if (mWatchForAudioSeekComplete && !mAudioPlayer->isSeeking()) {
        mWatchForAudioSeekComplete = false;
        notifyListener_l(MEDIA_SEEK_COMPLETE);
    }

    status_t finalStatus;
    if (mWatchForAudioEOS && mAudioPlayer->reachedEOS(&finalStatus)) {
        mWatchForAudioEOS = false;
        postStreamDoneEvent_l(finalStatus);
    }

    postCheckAudioStatusEvent_l();
}

status_t AwesomePlayer::prepare() {
    Mutex::Autolock autoLock(mLock);
    return prepare_l();
}

status_t AwesomePlayer::prepare_l() {
    if (mFlags & PREPARED) {
        return OK;
    }

    if (mFlags & PREPARING) {
        return UNKNOWN_ERROR;
    }

    mIsAsyncPrepare = false;
    status_t err = prepareAsync_l();

    if (err != OK) {
        return err;
    }

    while (mFlags & PREPARING) {
        mPreparedCondition.wait(mLock);
    }

    return mPrepareResult;
}

status_t AwesomePlayer::prepareAsync() {
    Mutex::Autolock autoLock(mLock);

    if (mFlags & PREPARING) {
        return UNKNOWN_ERROR;  // async prepare already pending
    }

    mIsAsyncPrepare = true;
    return prepareAsync_l();
}

status_t AwesomePlayer::prepareAsync_l() {
    if (mFlags & PREPARING) {
        return UNKNOWN_ERROR;  // async prepare already pending
    }

    if (!mQueueStarted) {
        mQueue.start();
        mQueueStarted = true;
    }

    mFlags |= PREPARING;
    mAsyncPrepareEvent = new AwesomeEvent(
            this, &AwesomePlayer::onPrepareAsyncEvent);

    mQueue.postEvent(mAsyncPrepareEvent);

    return OK;
}

status_t AwesomePlayer::finishSetDataSource_l() {
    sp<DataSource> dataSource =
        DataSource::CreateFromURI(mUri.string(), &mUriHeaders);

    if (dataSource == NULL) {
        return UNKNOWN_ERROR;
    }

    sp<MediaExtractor> extractor = MediaExtractor::Create(dataSource);

    if (extractor == NULL) {
        return UNKNOWN_ERROR;
    }

    if (dataSource->flags() & DataSource::kWantsPrefetching) {
        mPrefetcher = new Prefetcher;
    }

    return setDataSource_l(extractor);
}

void AwesomePlayer::onPrepareAsyncEvent() {
    {
        Mutex::Autolock autoLock(mLock);

        if (mUri.size() > 0) {
            status_t err = finishSetDataSource_l();

            if (err != OK) {
                if (mIsAsyncPrepare) {
                    notifyListener_l(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, err);
                }

                mPrepareResult = err;
                mFlags &= ~PREPARING;
                mAsyncPrepareEvent = NULL;
                mPreparedCondition.broadcast();

                return;
            }
        }
    }

    sp<Prefetcher> prefetcher;

    {
        Mutex::Autolock autoLock(mLock);
        prefetcher = mPrefetcher;
    }

    if (prefetcher != NULL) {
        prefetcher->prepare();
        prefetcher.clear();
    }

    Mutex::Autolock autoLock(mLock);

    if (mIsAsyncPrepare) {
        if (mVideoWidth < 0 || mVideoHeight < 0) {
            notifyListener_l(MEDIA_SET_VIDEO_SIZE, 0, 0);
        } else {
            notifyListener_l(MEDIA_SET_VIDEO_SIZE, mVideoWidth, mVideoHeight);
        }

        notifyListener_l(MEDIA_PREPARED);
    }

    mPrepareResult = OK;
    mFlags &= ~PREPARING;
    mFlags |= PREPARED;
    mAsyncPrepareEvent = NULL;
    mPreparedCondition.broadcast();
}

status_t AwesomePlayer::suspend() {
    LOGI("suspend");
    Mutex::Autolock autoLock(mLock);

    if (mSuspensionState != NULL) {
        return INVALID_OPERATION;
    }

    while (mFlags & PREPARING) {
        mPreparedCondition.wait(mLock);
    }

    SuspensionState *state = new SuspensionState;
    state->mUri = mUri;
    state->mUriHeaders = mUriHeaders;
    state->mFileSource = mFileSource;

    state->mFlags = mFlags & (PLAYING | LOOPING | AT_EOS);
    getPosition_l(&state->mPositionUs);

    if (mLastVideoBuffer) {
        size_t size = mLastVideoBuffer->range_length();
        if (size) {
            state->mLastVideoFrameSize = size;
            state->mLastVideoFrame = malloc(size);
            memcpy(state->mLastVideoFrame,
                   (const uint8_t *)mLastVideoBuffer->data()
                        + mLastVideoBuffer->range_offset(),
                   size);

            state->mVideoWidth = mVideoWidth;
            state->mVideoHeight = mVideoHeight;

            sp<MetaData> meta = mVideoSource->getFormat();
            CHECK(meta->findInt32(kKeyColorFormat, &state->mColorFormat));
            CHECK(meta->findInt32(kKeyWidth, &state->mDecodedWidth));
            CHECK(meta->findInt32(kKeyHeight, &state->mDecodedHeight));
        }
    }

    reset_l();

    mSuspensionState = state;

    return OK;
}

status_t AwesomePlayer::resume() {
    LOGI("resume");
    Mutex::Autolock autoLock(mLock);

    if (mSuspensionState == NULL) {
        return INVALID_OPERATION;
    }

    SuspensionState *state = mSuspensionState;
    mSuspensionState = NULL;

    status_t err;
    if (state->mFileSource != NULL) {
        err = setDataSource_l(state->mFileSource);

        if (err == OK) {
            mFileSource = state->mFileSource;
        }
    } else {
        err = setDataSource_l(state->mUri, &state->mUriHeaders);
    }

    if (err != OK) {
        delete state;
        state = NULL;

        return err;
    }

    seekTo_l(state->mPositionUs);

    mFlags = state->mFlags & (LOOPING | AT_EOS);

    if (state->mLastVideoFrame && mISurface != NULL) {
        mVideoRenderer =
            new AwesomeLocalRenderer(
                    true,  // previewOnly
                    "",
                    (OMX_COLOR_FORMATTYPE)state->mColorFormat,
                    mISurface,
                    state->mVideoWidth,
                    state->mVideoHeight,
                    state->mDecodedWidth,
                    state->mDecodedHeight);

        mVideoRendererIsPreview = true;

        ((AwesomeLocalRenderer *)mVideoRenderer.get())->render(
                state->mLastVideoFrame, state->mLastVideoFrameSize);
    }

    if (state->mFlags & PLAYING) {
        play_l();
    }

    delete state;
    state = NULL;

    return OK;
}

}  // namespace android
