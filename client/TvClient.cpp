/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#define LOG_MODULE_TAG "TV"
#define LOG_CLASS_TAG "TvClient"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>

#include "TvClient.h"
#include "CTvClientLog.h"
#include "tvcmd.h"

const int RET_SUCCESS = 0;
const int RET_FAILED  = -1;

const int EVENT_SIGLE_DETECT = 4;
const int EVENT_SOURCE_CONNECT = 10;

pthread_mutex_t tvclient_mutex = PTHREAD_MUTEX_INITIALIZER;

sp<TvClient> TvClient::mInstance;
sp<TvClient::DeathNotifier> TvClient::mDeathNotifier;

TvClient *TvClient::GetInstance() {
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY\n", __FUNCTION__);
    fflush(stdout);
#endif
    if (mInstance.get() == nullptr) {
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: mInstance is null, about to create new TvClient()\n", __FUNCTION__);
        fflush(stdout);
#endif
        mInstance = new TvClient();
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: Created new TvClient(), mInstance = %p\n", __FUNCTION__, mInstance.get());
        fflush(stdout);
#endif
    } else {
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: mInstance already exists, returning existing instance\n", __FUNCTION__);
        fflush(stdout);
#endif
    }

#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT, returning %p\n", __FUNCTION__, mInstance.get());
    fflush(stdout);
#endif
    return mInstance.get();
}

TvClient::TvClient() {
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY\n", __FUNCTION__);
    fflush(stdout);
#endif
    init_tv_logging();
    LOGD("%s.\n", __FUNCTION__);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to lock mutex\n", __FUNCTION__);
    fflush(stdout);
#endif
    pthread_mutex_lock(&tvclient_mutex);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Mutex locked, about to get ProcessState\n", __FUNCTION__);
    fflush(stdout);
#endif
    sp<ProcessState> proc(ProcessState::self());
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to start thread pool\n", __FUNCTION__);
    fflush(stdout);
#endif
    proc->startThreadPool();
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to get ServiceManager\n", __FUNCTION__);
    fflush(stdout);
#endif
    Parcel send, reply;
    sp<IServiceManager> sm = defaultServiceManager();
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to wait for tvservice (infinite loop)\n", __FUNCTION__);
    fflush(stdout);
#endif
    do {
        tvServicebinder = sm->getService(String16("tvservice"));
        if (tvServicebinder != 0) {
#ifdef STREAM_BOX_TRACE
            printf("[TRACE] %s: Got tvservice binder, breaking loop\n", __FUNCTION__);
            fflush(stdout);
#endif
            break;
        }
        LOGD("TvClient: Waiting tvservice published.\n");
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: tvservice not found, sleeping 500ms...\n", __FUNCTION__);
        fflush(stdout);
#endif
        usleep(500000);
    } while(true);
    if (mDeathNotifier == NULL) {
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: Creating DeathNotifier\n", __FUNCTION__);
        fflush(stdout);
#endif
       mDeathNotifier = new DeathNotifier();
    }
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to linkToDeath\n", __FUNCTION__);
    fflush(stdout);
#endif
    tvServicebinder->linkToDeath(mDeathNotifier);
    LOGD("Connected to tvservice.\n");
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to transact CMD_SET_TV_CB\n", __FUNCTION__);
    fflush(stdout);
#endif
    send.writeStrongBinder(sp<IBinder>(this));
    tvServicebinder->transact(CMD_SET_TV_CB, send, &reply);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from CMD_SET_TV_CB transact\n", __FUNCTION__);
    fflush(stdout);
#endif
    tvServicebinderId = reply.readInt32();
    LOGD("tvServicebinderId:%d.\n",tvServicebinderId);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to unlock mutex\n", __FUNCTION__);
    fflush(stdout);
#endif
    pthread_mutex_unlock(&tvclient_mutex);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT\n", __FUNCTION__);
    fflush(stdout);
#endif
}

TvClient::~TvClient() {
    LOGD("%s.\n", __FUNCTION__);
    mInstance = nullptr;
    mDeathNotifier = nullptr;
}

void TvClient::Release() {
    LOGD("%s.\n", __FUNCTION__);
    pthread_mutex_lock(&tvclient_mutex);
    if (tvServicebinder != NULL) {
        Parcel send, reply;
        send.writeInt32(tvServicebinderId);
        tvServicebinder->transact(CMD_CLR_TV_CB, send, &reply);
        tvServicebinder = NULL;
    }
    pthread_mutex_unlock(&tvclient_mutex);
    IPCThreadState::self()->stopProcess();
    mInstance = nullptr;
    mDeathNotifier = nullptr;
}

int TvClient::SendMethodCall(char *CmdString)
{
    LOGD("%s.\n", __FUNCTION__);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, CmdString = %s\n", __FUNCTION__, CmdString);
    fflush(stdout);
#endif
    int ReturnVal = 0;
    Parcel send, reply;

    if (tvServicebinder != NULL) {
        send.writeCString(CmdString);
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: About to call binder->transact(CMD_TV_ACTION, ...)\n", __FUNCTION__);
        fflush(stdout);
#endif
        if (tvServicebinder->transact(CMD_TV_ACTION, send, &reply) != 0) {
        ReturnVal = reply.readExceptionCode();
        LOGE("%s: tvServicebinder failed, error code:%d\n", __FUNCTION__, ReturnVal);
        } else {
        ReturnVal = reply.readInt32();
        }
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: Returned from binder->transact(), ReturnVal = %d\n", __FUNCTION__, ReturnVal);
        fflush(stdout);
#endif
    } else {
        LOGE("%s: tvServicebinder is NULL.\n", __FUNCTION__);
    }
    return ReturnVal;
}

int TvClient::SendDataRequest(char *CmdString, char *data, size_t datalen)
{
    LOGD("%s.\n", __FUNCTION__);
    int ReturnVal = 0;
    Parcel send, reply;

    pthread_mutex_lock(&tvclient_mutex);
    if (tvServicebinder != NULL) {
        send.writeCString(CmdString);
        if (tvServicebinder->transact(CMD_DATA_REQ, send, &reply) != 0) {
            LOGE("TvClient: call %s failed.\n", CmdString);
            ReturnVal = -1;
        } else {
            ReturnVal = reply.readInt32();
            if (reply.read(data, datalen) != 0)
                ReturnVal = -1;
        }
    }
    pthread_mutex_unlock(&tvclient_mutex);
   return ReturnVal;
}

int TvClient::SendTvClientEvent(CTvEvent &event)
{
    LOGD("%s\n", __FUNCTION__);

    int clientSize = mTvClientObserver.size();
    LOGD("%s: now has %d tvclient.\n", __FUNCTION__, clientSize);
    int i = 0;
    for (i = 0; i < clientSize; i++) {
        if (mTvClientObserver[i] != NULL) {
            mTvClientObserver[i]->onTvClientEvent(event);
        } else {
            LOGD("%s: mTvClientObserver[%d] is NULL.\n", __FUNCTION__, i, mTvClientObserver[i]);
        }
    }

    LOGD("send event for %d count TvClientObserver!\n", i);
    return 0;
}

int TvClient::HandSourceConnectEvent(const void* param)
{
    Parcel *parcel = (Parcel *) param;
    TvEvent::SourceConnectEvent event;
    event.mSourceInput = parcel->readInt32();
    event.connectionState = parcel->readInt32();
    mInstance->SendTvClientEvent(event);

    return 0;
}

int TvClient::HandSignalDetectEvent(const void* param)
{
    Parcel *parcel = (Parcel*) param;
    TvEvent::SignalDetectEvent event;
    event.mSourceInput = parcel->readInt32();
    event.mFmt = parcel->readInt32();
    event.mTrans_fmt = parcel->readInt32();
    event.mStatus = parcel->readInt32();
    event.mDviFlag = parcel->readInt32();
    event.mhdr_info = parcel->readUint32();
    mInstance->SendTvClientEvent(event);

    return 0;

}

int TvClient::HandSignalDvAllmEvent(const void* param)
{
    Parcel *parcel = (Parcel*) param;
    TvEvent::SignalDvAllmEvent event;

    event.allm_mode = parcel->readInt32();
    event.it_content = parcel->readInt32();
    event.cn_type = parcel->readInt32();
    mInstance->SendTvClientEvent(event);

    return 0;
}

int TvClient::HandSignalVrrEvent(const void* param)
{
    Parcel *parcel = (Parcel*) param;
    TvEvent::SignalVrrEvent event;
    event.cur_vrr_status = parcel->readInt32();
    mInstance->SendTvClientEvent(event);

    return 0;
}

int TvClient::setTvClientObserver(TvClientIObserver *observer)
{
    LOGD("%s\n", __FUNCTION__);
    if (observer != nullptr) {
        LOGD("%s: observer is %p.\n", __FUNCTION__, observer);
        int cookie = -1;
        int clientSize = mTvClientObserver.size();
        for (int i = 0; i < clientSize; i++) {
            if (mTvClientObserver[i] == NULL) {
                cookie = i;
                mTvClientObserver[i] = observer;
                break;
            } else {
                LOGD("%s: mTvClientObserver[%d] has been register.\n", __FUNCTION__, i);
            }
        }

        if (cookie < 0) {
            cookie = clientSize;
            mTvClientObserver[clientSize] = observer;
        }
    } else {
        LOGD("%s: observer is NULL.\n", __FUNCTION__);
    }

    return 0;
}

int TvClient::StartTv(tv_source_input_t source) {
    LOGD("%s\n", __FUNCTION__);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, source = %d\n", __FUNCTION__, source);
    fflush(stdout);
#endif
    char buf[32] = {0};
    sprintf(buf, "control.%d.%d", TV_CONTROL_START_TV, source);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call SendMethodCall(\"%s\")\n", __FUNCTION__, buf);
    fflush(stdout);
#endif
    int ret = SendMethodCall(buf);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from SendMethodCall(), ret = %d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    return ret;
}

int TvClient::StopTv(tv_source_input_t source) {
    LOGD("%s\n", __FUNCTION__);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, source = %d\n", __FUNCTION__, source);
    fflush(stdout);
#endif
    char buf[32] = {0};
    sprintf(buf, "control.%d.%d", TV_CONTROL_STOP_TV, source);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call SendMethodCall(\"%s\")\n", __FUNCTION__, buf);
    fflush(stdout);
#endif
    int ret = SendMethodCall(buf);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from SendMethodCall(), ret = %d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    return ret;
}

int TvClient::SetVdinWorkMode(vdin_work_mode_t vdinWorkMode)
{
    LOGD("%s\n", __FUNCTION__);
    char buf[512] = {0};
    sprintf(buf, "control.%d.%d", TV_CONTROL_VDIN_WORK_MODE_SET, vdinWorkMode);
    return SendMethodCall(buf);
}

int TvClient::SetEdidVersion(tv_source_input_t source, int edidVer)
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "edid.set.%d.%d.%d", HDMI_EDID_VER_SET, source, edidVer);
    return SendMethodCall(buf);
}

int TvClient::GetEdidVersion(tv_source_input_t source)
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "edid.get.%d.%d", HDMI_EDID_VER_GET, source);
    return SendMethodCall(buf);
}

int TvClient::SetEdidData(tv_source_input_t source, char *dataBuf)
{
    LOGD("%s\n", __FUNCTION__);
    char CmdString[32] = {0};
    sprintf(CmdString, "%d.%d.", HDMI_EDID_DATA_SET, source);

    int ret = -1;
    Parcel send, reply;
    if (tvServicebinder != NULL) {
        send.writeCString(CmdString);
        send.writeInt32(256);
        for (int i=0;i<256;i++) {
            send.writeInt32(dataBuf[i]);
        }
        if (tvServicebinder->transact(DATA_SET_ACTION, send, &reply) != 0) {
            LOGE("%s: tvServicebinder failed.\n", __FUNCTION__);
        } else {
            ret = reply.readInt32();
        }
    } else {
        LOGE("%s: tvServicebinder is NULL.\n", __FUNCTION__);
    }

    return ret;

}

int TvClient::GetEdidData(tv_source_input_t source, char *dataBuf)
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "%d.%d", HDMI_EDID_DATA_GET, source);
    int ret = -1;
    Parcel send, reply;
    if (tvServicebinder != NULL) {
        send.writeCString(buf);
        if (tvServicebinder->transact(DATA_GET_ACTION, send, &reply) != 0) {
            LOGE("%s: tvServicebinder failed.\n", __FUNCTION__);
        } else {
            // dataSize
            reply.readInt32();
            for (int i=0;i<256;i++) {
                dataBuf[i] = (char)reply.readInt32();
            }
            //ret = reply.readInt32();
            ret = 0;
        }
    } else {
        LOGE("%s: tvServicebinder is NULL.\n", __FUNCTION__);
    }

    return ret;
}

int TvClient::GetSPDInfo(tv_source_input_t source, char* dataBuf, size_t datalen)
{
    LOGD("%s\n", __FUNCTION__);
    char buf[512] = {0};
    snprintf(buf, sizeof(buf), "pkttype.get.%d.%d.%d.%s", HDMI_SPD_INFO_GET, source, datalen, dataBuf);
    return SendDataRequest(buf, dataBuf, datalen); // SendMethodCall(buf);
}

int TvClient::GetCurrentSourceFrameHeight()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "control.%d", TV_CONTROL_GET_FRAME_HEIGHT);
    return SendMethodCall(buf);
}

int TvClient::GetCurrentSourceFrameWidth()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "control.%d", TV_CONTROL_GET_FRAME_WIDTH);
    return SendMethodCall(buf);
}

int TvClient::GetCurrentSourceFrameFps()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "control.%d", TV_CONTROL_GET_FRAME_RATE);
    return SendMethodCall(buf);
}

int TvClient::GetCurrentSourceColorDepth()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "control.%d", TV_CONTROL_GET_COLOR_DEPTH);
    return SendMethodCall(buf);
}

tvin_aspect_ratio_t TvClient::GetCurrentSourceAspectRatio()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d", HDMI_GET_ASPECT_RATIO);
    return (tvin_aspect_ratio_t)SendMethodCall(buf);
}

tvin_color_fmt_t TvClient::GetCurrentSourceColorFormat()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d", HDMI_GET_COLOR_FORMAT);
    return (tvin_color_fmt_t)SendMethodCall(buf);
}

int TvClient::SetCurrentSourceColorRange(tvin_color_range_t range_mode)
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d.%d", HDMI_SET_COLOR_RANGE, (int)range_mode);
    return SendMethodCall(buf);
}

tvin_color_range_t TvClient::GetCurrentSourceColorRange()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d", HDMI_GET_COLOR_RANGE);
    return (tvin_color_range_t)SendMethodCall(buf);
}

tvin_line_scan_mode_t TvClient::GetCurrentSourceLineScanMode()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "control.%d", TV_CONTROL_GET_LINE_SCAN_MODE);
    return (tvin_line_scan_mode_t)SendMethodCall(buf);
}

int TvClient::GetSourceConnectStatus(tv_source_input_t source)
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "control.%d.%d", TV_CONTROL_GET_CONNECT_STATUS, source);
    return SendMethodCall(buf);
}

int TvClient::SetEdidBoostOn(int bBoostOn)
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "control.%d.%d", TV_CONTROL_SET_BOOST_ON, bBoostOn);
    return SendMethodCall(buf);
}

int TvClient::GetCurrentSourceAllmInfo(tvin_latency_s *info)
{
    LOGD("%s\n", __FUNCTION__);
    int ret = 0;
    char CmdString[32] = {0};
    sprintf(CmdString, "control.%d.", TV_CONTROL_GET_ALLM_INFO);
    Parcel send, reply;
    if (tvServicebinder != NULL) {
        send.writeCString(CmdString);
        if (tvServicebinder->transact(CMT_GET_ALLM_INFO, send, &reply) != 0) {
            LOGE("%s: tvServicebinder failed.\n", __FUNCTION__);
            ret = -1;
        } else {
            info-> allm_mode  = reply.readInt32();
            info->it_content  = reply.readInt32();
            info->cn_type     = (tvin_cn_type_t)reply.readInt32();
        }
    } else {
        LOGE("%s: tvServicebinder is NULL.\n", __FUNCTION__);
        ret = -1;
    }
    return ret;
}

int TvClient::SetHdmiAllmEnabled(bool enable)
{
    LOGD("%s: %s\n", __FUNCTION__, enable?"enable":"disable");
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d.%d", HDMI_SET_ALLM_ENABLED, enable?1:0);
    return SendMethodCall(buf);
}

bool TvClient::GetHdmiAllmEnabled()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d", HDMI_GET_ALLM_ENABLED);
    if (SendMethodCall(buf)) {
        return true;
    }
    return false;
}

int TvClient::SetHdmiVrrEnabled(bool enable)
{
    LOGD("%s: %s\n", __FUNCTION__, enable?"enable":"disable");
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d.%d", HDMI_SET_VRR_ENABLED, enable?1:0);
    return SendMethodCall(buf);
}

bool TvClient::GetHdmiVrrEnabled()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d", HDMI_GET_VRR_ENABLED);
    if (SendMethodCall(buf)) {
        return true;
    }
    return false;
}

vdin_vrr_mode_t TvClient::GetHdmiVrrMode()
{
    LOGD("%s\n", __FUNCTION__);
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d", HDMI_GET_VRR_MODE);
    return (vdin_vrr_mode_t)SendMethodCall(buf);
}

#ifdef STREAM_BOX
int TvClient::SetGameMode(bool enable)
{
    LOGD("%s: %s\n", __FUNCTION__, enable?"enable":"disable");
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d.%d", HDMI_SET_GAME_MODE, enable?1:0);
    return SendMethodCall(buf);
}

int TvClient::SetPcMode(bool enable)
{
    LOGD("%s: %s\n", __FUNCTION__, enable?"enable":"disable");
    char buf[32] = {0};
    sprintf(buf, "hdmi.%d.%d", HDMI_SET_PC_MODE, enable?1:0);
    return SendMethodCall(buf);
}
#endif

status_t TvClient::onTransact(uint32_t code,
                                const Parcel& data, Parcel* reply,
                                uint32_t flags) {
    pthread_mutex_lock(&tvclient_mutex);
    LOGD("TvClient get tanscode: %u\n", code);
    switch (code) {
        case EVT_SRC_CT_CB: {
            HandSourceConnectEvent(&data);
            break;
        }
        case EVT_SIG_DT_CB: {
            HandSignalDetectEvent(&data);
            break;
        }
        case EVT_SIG_DV_ALLM: {
            HandSignalDvAllmEvent(&data);
            break;
        }
        case EVT_SIG_VRR_CB: {
            HandSignalVrrEvent(&data);
            break;
        }
        case CMD_START:
        default:
            pthread_mutex_unlock(&tvclient_mutex);
            return BBinder::onTransact(code, data, reply, flags);
    }
    pthread_mutex_unlock(&tvclient_mutex);
    return (0);
}

void TvClient::binderDied(const wp<IBinder> &who)
{
    LOGV("ITv died");
    //notifyCallback(1, 2, 0);
}

void TvClient::DeathNotifier::binderDied(const wp<IBinder> &who)
{
    LOGV("-----Tv server died,reconnect-----\n");
    if (TvClient::mInstance != nullptr) {
        TvClient::mInstance.clear();
    }
    mInstance = TvClient::GetInstance();
    LOGV("-----reconnect success-----\n");
}

