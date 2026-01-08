/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#define LOG_MODULE_TAG "TV"
#define LOG_CLASS_TAG "CTv"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "CTv.h"
#include "tvutils.h"
#include "TvConfigManager.h"
#include "CTvLog.h"
#include "CVpp.h"
#include <sys/ioctl.h>
#include <chrono>

/*
static tv_hdmi_edid_version_t Hdmi1CurrentEdidVer = HDMI_EDID_VER_14;
static tv_hdmi_edid_version_t Hdmi2CurrentEdidVer = HDMI_EDID_VER_14;
static tv_hdmi_edid_version_t Hdmi3CurrentEdidVer = HDMI_EDID_VER_14;
static tv_hdmi_edid_version_t Hdmi4CurrentEdidVer = HDMI_EDID_VER_14;
*/
#define NEW_FRAME_TIME_OUT_COUNT 10

//PORT_NUM define as 3 in the kernel.
#define K_PORT_NUM (3)

CTv::CTv()
{
    mpObserver = NULL;
    UenvInit();
    const char* tvConfigFilePath = getenv(CFG_TV_CONFIG_FILE_PATH_STR);
    if (!tvConfigFilePath) {
        LOGD("%s: read tvconfig file path failed!\n", __FUNCTION__);
        tvConfigFilePath = CONFIG_FILE_PATH_DEF;
    } else {
        LOGD("%s: tvconfig file path is %s!\n", __FUNCTION__, tvConfigFilePath);
    }
    LoadConfigFile(tvConfigFilePath);

    const char * value;
    value = ConfigGetStr( CFG_SECTION_TV, CFG_TVIN_ATV_DISPLAY_SNOW, "null" );
    if (strcmp(value, "enable") == 0 ) {
        mATVDisplaySnow = true;
    } else {
        mATVDisplaySnow = false;
    }
    LOGD("%s: load mATVDisplaySnow status [%d]!\n", __FUNCTION__, mATVDisplaySnow);

    mpAmVideo = new CAmVideo();
    mpTvin = CTvin::getInstance();
    mpTvin->Tvin_AddVideoPath(TV_PATH_VDIN_AMLVIDEO2_PPMGR_DEINTERLACE_AMVIDEO);
    mpTvin->Tvin_LoadSourceInputToPortMap();
    mpHDMIRxManager = new CHDMIRxManager();
    // update cec port sequence from hdmi tv config.
    mpHDMIRxManager->SetHdmiPortCecPhysicAddr();
    tvin_info_t signalInfo;
    signalInfo.trans_fmt = TVIN_TFMT_2D;
    signalInfo.fmt = TVIN_SIG_FMT_NULL;
    signalInfo.status = TVIN_SIG_STATUS_NULL;
    signalInfo.cfmt = TVIN_COLOR_FMT_MAX;
    signalInfo.signal_type= 0;
    signalInfo.fps = 60;
    signalInfo.is_dvi = 0;
    signalInfo.aspect_ratio = TVIN_ASPECT_NULL;
    SetCurrenSourceInfo(signalInfo);

    //EDID load
#ifdef STREAM_BOX
    int edidPassthroughEnable = ConfigGetInt(CFG_SECTION_HDMI, CFG_HDMI_EDID_PASSTHROUGH_TX_TO_RX, 0);
    if (edidPassthroughEnable == 1) {
        LOGD("%s: EDID passthrough from HDMI TX to RX enabled!\n", __FUNCTION__);
        // Pass through EDID from HDMI TX to all HDMI RX ports
        for (int port = 1; port < TVIN_PORT_ID_MAX; port++) {
            mpHDMIRxManager->PassthroughEdidFromTxToRx(port);
        }
        LOGD("%s: EDID passthrough completed, skipping file-based EDID load\n", __FUNCTION__);
    } else {
#endif
    int edidAutoLoadEnable = ConfigGetInt(CFG_SECTION_HDMI, CFG_HDMI_EDID_AUTO_LOAD_EN, 1);
    if (edidAutoLoadEnable == 1) {
        LOGD("%s: EDID data load by tvserver!\n", __FUNCTION__);
        const char* buf = GetUenv(HDMI_UBOOT_EDID_VERSION);
        if (buf == NULL) {
#ifdef STREAM_BOX
            const char* edidDefaultVersion = ConfigGetStr(CFG_SECTION_HDMI, CFG_HDMI_EDID_DEFAULT_VERSION, "2.0");
#else
            const char* edidDefaultVersion = ConfigGetStr(CFG_SECTION_HDMI, CFG_HDMI_EDID_DEFAULT_VERSION, "1.4");
#endif
            int edidSetValue = 0;
            if (strcmp(edidDefaultVersion, "2.0") == 0 ) {
                LOGD("%s: Set EDID default version to 2.0!\n", __FUNCTION__);
                /*
                Hdmi1CurrentEdidVer = HDMI_EDID_VER_20;
                Hdmi2CurrentEdidVer = HDMI_EDID_VER_20;
                Hdmi3CurrentEdidVer = HDMI_EDID_VER_20;
                Hdmi4CurrentEdidVer = HDMI_EDID_VER_20;
                */
                for (int i = 0;i < 4; i ++)
                    edidSetValue |=  (1 << (4*i));
            }
            SetUenv(HDMI_UBOOT_EDID_VERSION,  std::to_string(edidSetValue).c_str());
        }
        bool dolbyVisionEnableState = GetDolbyVisionSupportStatus();
        LoadEdidData(0, dolbyVisionEnableState);
    } else {
        LOGD("%s: EDID data load by customer!\n", __FUNCTION__);
    }
#ifdef STREAM_BOX
    }
#endif
    mTvDevicesPollDetect.setObserver(this);
    mTvDevicesPollDetect.startDetect();
    mTvMsgQueue = new CTvMsgQueue(this);
    mTvMsgQueue->startMsgQueue();
    mLastScreenMode = -1;
}

CTv::~CTv()
{
    if (mpTvin != NULL) {
        mpTvin->Tvin_RemoveVideoPath(TV_PATH_TYPE_TVIN);
        delete mpTvin;
        mpTvin = NULL;
    }

    if (mpHDMIRxManager != NULL) {
        delete mpHDMIRxManager;
        mpHDMIRxManager = NULL;
    }

    if (mpAmVideo != NULL) {
        delete mpAmVideo;
        mpAmVideo = NULL;
    }

    mpObserver = NULL;
    UnloadConfigFile();
}

int CTv::StartTv(tv_source_input_t source)
{
    LOGD("%s: source = %d!\n", __FUNCTION__, source);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, source = %d\n", __FUNCTION__, source);
    fflush(stdout);
#endif
    int ret = 0;

    if (SOURCE_MPEG == source) {
    LOGD("%s: new source is %d! RETURN\n", __FUNCTION__,source);
    return ret;
    }
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to write VIDEO_FREERUN_MODE\n", __FUNCTION__);
    fflush(stdout);
#endif
    tvWriteSysfs(VIDEO_FREERUN_MODE, "0");
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call CVpp::setVideoaxis()\n", __FUNCTION__);
    fflush(stdout);
#endif
    CVpp::getInstance()->setVideoaxis();//when open source set video axis full screen

#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to get source port\n", __FUNCTION__);
    fflush(stdout);
#endif
    tvin_port_t source_port = mpTvin->Tvin_GetSourcePortBySourceInput(source);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Got source_port = 0x%x, about to call Tvin_OpenPort()\n", __FUNCTION__, source_port);
    fflush(stdout);
#endif
    ret = mpTvin->Tvin_OpenPort(source_port);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from Tvin_OpenPort(), ret = %d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    mCurrentSource = source;
#ifdef HAVE_AUDIO
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to create audio patch\n", __FUNCTION__);
    fflush(stdout);
#endif
    CTvAudio::getInstance()->create_audio_patch(mapSourcetoAudiotupe(source));
#ifndef STREAM_BOX_LEGACY
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to set audio av mute\n", __FUNCTION__);
    fflush(stdout);
#endif
    CTvAudio::getInstance()->set_audio_av_mute(true);
#endif
#endif
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT, returning ret = %d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    return ret;
}

int CTv::StopTv(tv_source_input_t source)
{
    LOGD("%s: source = %d!\n", __FUNCTION__, source);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, source = %d\n", __FUNCTION__, source);
    fflush(stdout);
#endif
    int ret = 0;
    if (SOURCE_MPEG == source) {
    LOGD("%s:current source is %d! return\n", __FUNCTION__,source);
    return ret;
    }

#ifdef HAVE_AUDIO
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to release audio patch\n", __FUNCTION__);
    fflush(stdout);
#endif
    CTvAudio::getInstance()->release_audio_patch();
#ifndef STREAM_BOX_LEGACY
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to set audio av mute false\n", __FUNCTION__);
    fflush(stdout);
#endif
    CTvAudio::getInstance()->set_audio_av_mute(false);
#endif
#endif
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to disable video layer\n", __FUNCTION__);
    fflush(stdout);
#endif
    mpAmVideo->SetVideoLayerStatus(VIDEO_LAYER_STATUS_DISABLE);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to disable video global output mode\n", __FUNCTION__);
    fflush(stdout);
#endif
    mpAmVideo->SetVideoGlobalOutputMode(VIDEO_GLOBAL_OUTPUT_MODE_DISABLE);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to stop decoder\n", __FUNCTION__);
    fflush(stdout);
#endif
    mpTvin->Tvin_StopDecoder();
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to get source port\n", __FUNCTION__);
    fflush(stdout);
#endif
    tvin_port_t source_port = mpTvin->Tvin_GetSourcePortBySourceInput(source);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Got source_port = 0x%x, about to call Tvin_ClosePort()\n", __FUNCTION__, source_port);
    fflush(stdout);
#endif
    mpTvin->Tvin_ClosePort(source_port);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from Tvin_ClosePort()\n", __FUNCTION__);
    fflush(stdout);
#endif

    mVdinWorkMode = VDIN_WORK_MODE_VFM;
    mCurrentSource = SOURCE_INVALID;
    tvin_info_t tempSignalInfo;
    tempSignalInfo.trans_fmt = TVIN_TFMT_2D;
    tempSignalInfo.fmt = TVIN_SIG_FMT_NULL;
    tempSignalInfo.status = TVIN_SIG_STATUS_NULL;
    tempSignalInfo.cfmt = TVIN_COLOR_FMT_MAX;
    tempSignalInfo.signal_type= 0;
    tempSignalInfo.fps = 60;
    tempSignalInfo.is_dvi = 0;
    tempSignalInfo.aspect_ratio = TVIN_ASPECT_NULL;
    SetCurrenSourceInfo(tempSignalInfo);

#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT, returning ret = %d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    return ret;
}

int CTv::SwitchSource(tv_source_input_t dest_source)
{
    if (dest_source != mCurrentSource) {
        StopTv(mCurrentSource);
        StartTv(dest_source);
    } else {
        LOGD("same source,no need switch!\n");
    }

    return 0;
}

void CTv::onSourceConnect(int source, int connect_status)
{
    LOGD("onSourceConnect: source = %d, connect_status= %d!\n", source, connect_status);
    //To do
    TvEvent::SourceConnectEvent event;
    event.mSourceInput = source;
    event.connectionState = connect_status;
    sendTvEvent(event);
}

void CTv::onVdinSignalChange()
{
    vdin_event_info_s SignalEventInfo;
    memset(&SignalEventInfo, 0, sizeof(vdin_event_info_s));
    int ret  = mpTvin->Tvin_GetSignalEventInfo(&SignalEventInfo);
    if (ret < 0) {
        LOGD("Get vidn event error!\n");
    } else {
        tv_source_input_type_t source_type = mpTvin->Tvin_SourceInputToSourceInputType(mCurrentSource);
        tvin_sig_change_flag_t vdinEventType = (tvin_sig_change_flag_t)SignalEventInfo.event_sts;
        switch (vdinEventType) {
        case TVIN_SIG_CHG_SDR2HDR:
        case TVIN_SIG_CHG_HDR2SDR:
        case TVIN_SIG_CHG_DV2NO:
        case TVIN_SIG_CHG_NO2DV: {
            LOGD("%s: hdr info change!\n", __FUNCTION__);
            tvin_info_t vdinSignalInfo;
            memset(&vdinSignalInfo, 0, sizeof(tvin_info_t));
            ret = mpTvin->Tvin_GetSignalInfo(&vdinSignalInfo);
            if (ret < 0) {
                LOGD("%s: Get vidn event error!\n", __FUNCTION__);
            } else {
                if ((mCurrentSignalInfo.status == TVIN_SIG_STATUS_STABLE) && (mCurrentSignalInfo.signal_type != vdinSignalInfo.signal_type)) {
                    if (source_type == SOURCE_TYPE_HDMI) {
                        //tvSetCurrentHdrInfo(vdinSignalInfo.signal_type);
                    }
                    mCurrentSignalInfo.signal_type = vdinSignalInfo.signal_type;
                } else {
                    LOGD("%s: hdmi signal don't stable!\n", __FUNCTION__);
                }
            }
            break;
        }
        case TVIN_SIG_CHG_COLOR_FMT:
            LOGD("%s: no need do any thing for colorFmt change!\n", __FUNCTION__);
            break;
        case TVIN_SIG_CHG_RANGE:
            LOGD("%s: no need do any thing for colorRange change!\n", __FUNCTION__);
            break;
        case TVIN_SIG_CHG_BIT:
            LOGD("%s: no need do any thing for color bit deepth change!\n", __FUNCTION__);
            break;
        case TVIN_SIG_CHG_VS_FRQ:
            LOGD("%s: no need do any thing for VS_FRQ change!\n", __FUNCTION__);
            break;
        case TVIN_SIG_CHG_STS:
            LOGD("%s: vdin signal status change!\n", __FUNCTION__);
            onSigStatusChange();
            break;
        case TVIN_SIG_CHG_AFD: {
            LOGD("%s: AFD info change!\n", __FUNCTION__);
            if (source_type == SOURCE_TYPE_HDMI) {
                tvin_info_t newSignalInfo;
                memset(&newSignalInfo, 0, sizeof(tvin_info_t));
                int ret = mpTvin->Tvin_GetSignalInfo(&newSignalInfo);
                if (ret < 0) {
                    LOGD("%s: Get Signal Info error!\n", __FUNCTION__);
                } else {
                    if ((newSignalInfo.status == TVIN_SIG_STATUS_STABLE)
                        && (mCurrentSignalInfo.aspect_ratio != newSignalInfo.aspect_ratio)) {
                        mCurrentSignalInfo.aspect_ratio = newSignalInfo.aspect_ratio;
                        //tvSetCurrentAspectRatioInfo(newSignalInfo.aspect_ratio);
                    } else {
                        LOGD("%s: signal not stable or same AFD info!\n", __FUNCTION__);
                    }
                }
            }
            break;
        }
        case TVIN_SIG_CHG_DV_ALLM:
            LOGD("%s: allm info change!\n", __FUNCTION__);
            if (source_type == SOURCE_TYPE_HDMI) {
                onSigDvAllmChange();
            } else {
                LOGD("%s: not hdmi source!\n", __FUNCTION__);
            }
            break;
        case TVIN_SIG_CHG_VRR:
            LOGD("%s: vrr change!\n", __FUNCTION__);
            if (source_type == SOURCE_TYPE_HDMI) {
                onSigVrrChange();
            } else {
                LOGD("%s: not hdmi source!\n", __FUNCTION__);
            }
        default:
            LOGD("%s: invalid vdin event!\n", __FUNCTION__);
            break;
        }
    }
}

void CTv::onSigDvAllmChange(void)
{
    tvin_latency_s AllmInfo;

    int ret = mpTvin->Tvin_GetAllmInfo(&AllmInfo);
	if (ret < 0) {
        LOGE("%s: mpTvin->Tvin_GetAllmInfo() failed!\n", __FUNCTION__);
        return;
    }

    TvEvent::SignalDvAllmEvent event;
    event.allm_mode = static_cast<int>(AllmInfo.allm_mode);
    event.it_content = AllmInfo.it_content ? 1 : 0;
    event.cn_type = AllmInfo.cn_type;
    sendTvEvent(event);
}

void CTv::GetAllmInfo(tvin_latency_s *info)
{
    int ret = mpTvin->Tvin_GetAllmInfo(info);
	if (ret < 0) {
        LOGE("%s: mpTvin->Tvin_GetAllmInfo() failed!\n", __FUNCTION__);
    }
}

void CTv::onSigVrrChange(void)
{
    vdin_vrr_freesync_param_s vrrparm;

    int ret = mpTvin->VDIN_GetVrrFreesyncParm(&vrrparm);
    if (ret < 0) {
        LOGE("%s: mpTvin->VDIN_GetVrrFreesyncParm() failed!\n", __FUNCTION__);
        return;
    }

    TvEvent::SignalVrrEvent event;
    event.cur_vrr_status = vrrparm.cur_vrr_status;
    sendTvEvent(event);
}

int CTv::GetVrrMode() {
    vdin_vrr_freesync_param_s vrrparm;
    int ret = mpTvin->VDIN_GetVrrFreesyncParm(&vrrparm);
    if (ret < 0) {
        LOGE("%s: mpTvin->VDIN_GetVrrFreesyncParm() failed!\n", __FUNCTION__);
        return 0;
    }
    return vrrparm.cur_vrr_status;
}

void CTv::onSigStatusChange(void)
{
    LOGD("%s\n", __FUNCTION__);
    tvin_info_s tempSignalInfo;
    int ret = mpTvin->Tvin_GetSignalInfo(&tempSignalInfo);
    if (ret < 0) {
        LOGD("Get Signal Info error!\n");
        return;
    } else {
        SetCurrenSourceInfo(tempSignalInfo);
        LOGD("sig_fmt is %d, status is %d, isDVI is %d, hdr_info is 0x%x\n",
               mCurrentSignalInfo.fmt, mCurrentSignalInfo.status, mCurrentSignalInfo.is_dvi, mCurrentSignalInfo.signal_type);
        if ( mCurrentSignalInfo.status == TVIN_SIG_STATUS_STABLE ) {
            onSigToStable();
        } else if (mCurrentSignalInfo.status == TVIN_SIG_STATUS_UNSTABLE ) {
            onSigToUnstable();
        } else if ( mCurrentSignalInfo.status == TVIN_SIG_STATUS_NOTSUP ) {
            onSigToUnSupport();
        } else if ( mCurrentSignalInfo.status == TVIN_SIG_STATUS_NOSIG ) {
            onSigToNoSig();
        } else {
            LOGD("%s: invalid signal status!\n");
        }

        return;
    }
}

int CTv::SetCurrenSourceInfo(tvin_info_t sig_info)
{
    mCurrentSignalInfo.trans_fmt = sig_info.trans_fmt;
    mCurrentSignalInfo.fmt = sig_info.fmt;
    mCurrentSignalInfo.status = sig_info.status;
    mCurrentSignalInfo.cfmt = sig_info.cfmt;
    mCurrentSignalInfo.signal_type= sig_info.signal_type;
    mCurrentSignalInfo.fps = sig_info.fps;
    mCurrentSignalInfo.is_dvi = sig_info.is_dvi;
    mCurrentSignalInfo.aspect_ratio = sig_info.aspect_ratio;

    return 0;
}

tvin_info_t CTv::GetCurrentSourceInfo(void)
{
    LOGD("mCurrentSource = %d, trans_fmt is %d,fmt is %d, status is %d.\n",
            mCurrentSource, mCurrentSignalInfo.trans_fmt, mCurrentSignalInfo.fmt, mCurrentSignalInfo.status);
    return mCurrentSignalInfo;
}

int CTv::setTvObserver ( TvIObserver *ob )
{
    LOGD("%s\n", __FUNCTION__);
    if (ob != NULL) {
        mpObserver = ob;
    } else {
        LOGD("%s: Observer is NULL.\n", __FUNCTION__);
    }

    return 0;
}

int CTv::SetEDIDData(tv_source_input_t source, char *data)
{
    LOGD("%s\n", __FUNCTION__);
    int ret = -1;
    if (data == NULL) {
        LOGD("%s: data is NULL.\n", __FUNCTION__);
    } else {
        unsigned char edidData[REAL_EDID_DATA_SIZE];
        memcpy(edidData, data, REAL_EDID_DATA_SIZE);
        tvin_port_id_t portId = mpTvin->Tvin_GetHdmiPortIdBySourceInput(source);
        ret = mpHDMIRxManager->UpdataEdidDataWithPort(portId, edidData);
    }

    return ret;
}

int CTv::GetEDIDData(tv_source_input_t source, char *data)
{
    char edidData[REAL_EDID_DATA_SIZE];
    char edidFileName[100] = {0};
    int port = (int)source - SOURCE_HDMI1 + 1;
    int edidVer = (GetEdidVersion(source) == HDMI_EDID_VER_20)?20:14;
    char dv_support[4] = {0};
#ifdef STREAMBOX_LINUX_PATH
    const char *edidFilePath = ConfigGetStr(CFG_SECTION_HDMI, CFG_HDMI_EDID_FILE_PATH, "/etc/tvconfig/hdmi");
#else
    const char *edidFilePath = ConfigGetStr(CFG_SECTION_HDMI, CFG_HDMI_EDID_FILE_PATH, "/vendor/etc/tvconfig/hdmi");
#endif

    if (GetDolbyVisionSupportStatus() == 1)
        strcpy(dv_support, "_dv");

    memset(edidData, 0, REAL_EDID_DATA_SIZE);
    sprintf(edidFileName, "%s/port%d_%d%s.bin", edidFilePath, port, edidVer, dv_support);
    LOGD("%s: EDID file: %s\n", __FUNCTION__, edidFileName);
    ReadDataFromFile(edidFileName, 0, REAL_EDID_DATA_SIZE, edidData);
    memcpy(data, edidData, REAL_EDID_DATA_SIZE);
    return 0;
}

#define MMC_DEVICE      ("/dev/mmcblk0")
#define PANEL_ID_OFFSET 0x9000004
int CTv::GetPanelSize()
{
    int fd, len, panelSize=0;
    off_t offset = 0L;
    char buf[2] = {0}, psize[8]={0};

    if ((fd = open(MMC_DEVICE, O_RDONLY)) < 0) {
        LOGE("open %s error(%s)", MMC_DEVICE, strerror (errno));
        return 0;
    }

    offset = lseek (fd, PANEL_ID_OFFSET, SEEK_SET);
    if ( offset !=  PANEL_ID_OFFSET ) {
        LOGE("Failed to seek. offset[0x%x] requested[0x%x]\n", (uint32_t)offset, PANEL_ID_OFFSET);
        close(fd);
        return 0;
    }

    len = read(fd, buf, 2);
    if (len < 0) {
        LOGE("Read %s error, %s\n", MMC_DEVICE, strerror(errno));
        close(fd);
        return 0;
    }
    sprintf(psize, "%d", buf[1]);
    panelSize = atoi(psize);

    close(fd);
    return panelSize;
}

int CTv::LoadEdidData(int isNeedBlackScreen, int isDolbyVisionEnable)
{
    if (isNeedBlackScreen  == 1) {
        mpTvin->Tvin_StopDecoder();
    }

    char edidLoadBuf[(TVIN_PORT_ID_MAX - 1) * 2 * REAL_EDID_DATA_SIZE];
    char edidFileName[100] = {0};
    int loadNum = 1;
    int i = 0;
    tvin_port_t tvin_port = TVIN_PORT_NULL;
    ui_hdmi_port_id_t ui_port = UI_HDMI_PORT_ID_MAX;
    tv_source_input_t source_input;
#ifdef STREAMBOX_LINUX_PATH
    const char *edidFilePath = ConfigGetStr(CFG_SECTION_HDMI, CFG_HDMI_EDID_FILE_PATH, "/etc/tvconfig/hdmi");
#else
    const char *edidFilePath = ConfigGetStr(CFG_SECTION_HDMI,
            CFG_HDMI_EDID_FILE_PATH,
            "/vendor/etc/tvconfig/hdmi");
#endif
    LOGD("%s: sizeof edidLoadBuf:[%d] isDolbyVisionEnable = %d.\n",
            __FUNCTION__,
            sizeof(edidLoadBuf),
            isDolbyVisionEnable);
    memset(edidLoadBuf, 0, sizeof(edidLoadBuf));
    for (loadNum = (int)TVIN_PORT_ID_1; loadNum < (int)TVIN_PORT_ID_MAX; loadNum++) {
        tvin_port = mpTvin->Tvin_GetVdinPortByVdinPortID((tvin_port_id_t)loadNum);
        source_input = mpTvin->Tvin_PortToSourceInput(tvin_port);
        ui_port = mpTvin->Tvin_GetUIHdmiPortIdBySourceInput(source_input);
        LOGD("%s:tvin port ID:%d tvin_port:0x%x source_input:%d ui_port:%d\n",
                __FUNCTION__,
                loadNum,
                tvin_port,
                source_input,
                ui_port);

        // Load EDID 1.4 then load EDID 2.0.
        for (i = 0; i < 2; i++) {
            /* sprintf will add a null-terminated character ('\0') at the end of
               the formatted string. So, do not need to memset the buffer.*/
            sprintf(edidFileName, "%s/port%d_%s%s.bin",
                    edidFilePath,
                    ui_port,
                    i ? "20" : "14",
                    isDolbyVisionEnable ? "_dv" : "");
            if (!isFileExist(edidFileName))
                sprintf(edidFileName, "%s/port%d_%s%s.bin",
                        edidFilePath,
                        UI_HDMI_PORT_ID_1,
                        i ? "20" : "14",
                        isDolbyVisionEnable ? "_dv" : "");
            ReadDataFromFile(edidFileName,
                    0,
                    REAL_EDID_DATA_SIZE,
                    edidLoadBuf + (2 * loadNum - 2 + i) * REAL_EDID_DATA_SIZE);
            LOGD("%s:File:%s\n", __FUNCTION__, edidFileName);
#ifdef STREAM_BOX
            // Patch EDID to enable 120Hz support
            mpHDMIRxManager->PatchEdidFor120Hz(
                (unsigned char *)(edidLoadBuf + (2 * loadNum - 2 + i) * REAL_EDID_DATA_SIZE),
                REAL_EDID_DATA_SIZE);
#endif
        }
    }
    int ret = mpHDMIRxManager->HdmiRxEdidDataSwitch(2 * K_PORT_NUM, edidLoadBuf);
    if (ret == 0) {
        ui_hdmi_port_id_t portId = UI_HDMI_PORT_ID_MAX;
        int edidSetValue = 0;
        int portEdidVersion = 0;
        for (int i=SOURCE_HDMI1;i<SOURCE_VGA;i++) {
            portId = mpTvin->Tvin_GetUIHdmiPortIdBySourceInput((tv_source_input_t)i);
            portEdidVersion = GetEdidVersion((tv_source_input_t)i);
            edidSetValue |=  (portEdidVersion << (4*portId - 4));
        }
        LOGD("%s:[edidSetValue:0x%x]", __FUNCTION__, edidSetValue);
        //for HDMI feature(allm,vrr) status init
        const char *buf = GetUenv(HDMI_UBOOT_EDID_FEATURE);
        int value = 0;
        int allmEnable = 0;
        int VrrEnable = 0;
        if (buf) {
            value = atoi(buf);
            allmEnable = (value & 1);
            VrrEnable = ((value >> 1) & 1);
            LOGD("%s init HDMI feature status allmEnable:%d, VrrEnable:%d.\n",
                __FUNCTION__, allmEnable, VrrEnable);
            mpHDMIRxManager->SetHDMIFeatureInit(allmEnable, VrrEnable);
        } else {
            allmEnable = mpHDMIRxManager->GetAllmEnabled();
            VrrEnable = mpHDMIRxManager->GetVrrEnabled();
            value = ((VrrEnable << 1)|allmEnable);
            LOGD("%s HDMI feature uenv unexist, save it allmEnable:%d, VrrEnable:%d.\n",
                __FUNCTION__ , allmEnable, VrrEnable);
            SetUenv(HDMI_UBOOT_EDID_FEATURE, std::to_string(value).c_str());
        }

        LOGD("%s [UI sequence] edidSetValue:0x%x\n", __FUNCTION__, edidSetValue);
        ret = mpHDMIRxManager->HdmiRxEdidVerSwitch(edidSetValue);
    } else {
        LOGE("%s failed!\n", __FUNCTION__);
    }

    return ret;

}

int CTv::SetEdidVersion(tv_source_input_t source, tv_hdmi_edid_version_t edidVer)
{
    LOGD("%s: setSource: %d, setVersion: %d\n", __FUNCTION__, source, edidVer);
    int ret = -1;
    tv_hdmi_edid_version_t currentVersion = (tv_hdmi_edid_version_t)GetEdidVersion(source);
    if (currentVersion != edidVer) {
        mpTvin->Tvin_StopDecoder();
        ui_hdmi_port_id_t portId = UI_HDMI_PORT_ID_MAX;
        int edidSetValue = 0;
        //We have already set the port map to kernel.So we should use UI sequence.
        int portEdidVersion = 0;
        for (int i=SOURCE_HDMI1;i<SOURCE_VGA;i++) {
            portId = mpTvin->Tvin_GetUIHdmiPortIdBySourceInput((tv_source_input_t)i);
            if (i == source) {
                portEdidVersion = edidVer;
            } else {
                portEdidVersion = GetEdidVersion((tv_source_input_t)i);
            }
            edidSetValue |=  (portEdidVersion << (4*portId - 4));
        }
        LOGD("%s [UI sequence] edidSetValue:0x%x\n", __FUNCTION__, edidSetValue);
        ret = mpHDMIRxManager->HdmiRxEdidVerSwitch(edidSetValue);
        if (mCurrentSource == source) {
            mpHDMIRxManager->HDMIRxDeviceIOCtl(HDMI_IOC_EDID_UPDATE);
        }
        if (ret < 0) {
            LOGE("%s failed.\n", __FUNCTION__);
            ret = -1;
        } else {
            /*
            switch (source) {
            case SOURCE_HDMI1:
                Hdmi1CurrentEdidVer = edidVer;
                break;
            case SOURCE_HDMI2:
                Hdmi2CurrentEdidVer = edidVer;
                break;
            case SOURCE_HDMI3:
                Hdmi3CurrentEdidVer = edidVer;
                break;
            case SOURCE_HDMI4:
                Hdmi4CurrentEdidVer = edidVer;
                break;
            default:
                LOGD("%s: not hdmi source.\n", __FUNCTION__);
                break;
            }
            */
            SetUenv(HDMI_UBOOT_EDID_VERSION, std::to_string(edidSetValue).c_str());
        }
    } else {
        LOGD("%s: same EDID version, no need set.\n", __FUNCTION__);
        ret = 0;
    }

    return ret;

}

int CTv::GetEdidVersion(tv_source_input_t source)
{
    //TODO:add user setting read/write flow
#ifdef STREAM_BOX
    int retValue = HDMI_EDID_VER_20; // Default to 2.0 instead of 1.4
#else
    int retValue = HDMI_EDID_VER_14;
#endif
    const char *buf = GetUenv(HDMI_UBOOT_EDID_VERSION);
    int version = 0;
    ui_hdmi_port_id_t port_id = UI_HDMI_PORT_ID_MAX;
    if (buf) {
        version = atoi(buf);
    }
    port_id = mpTvin->Tvin_GetUIHdmiPortIdBySourceInput(source);
    switch (port_id) {
    case UI_HDMI_PORT_ID_1:
        //retValue = Hdmi1CurrentEdidVer;
        retValue = version & 0xf;
#ifdef STREAM_BOX
        if (retValue == 0) retValue = HDMI_EDID_VER_20; // Default to 2.0 if not set
#endif
        break;
    case UI_HDMI_PORT_ID_2:
        //retValue = Hdmi2CurrentEdidVer;
        retValue = (version >> 4) & 0xf;
#ifdef STREAM_BOX
        if (retValue == 0) retValue = HDMI_EDID_VER_20; // Default to 2.0 if not set
#endif
        break;
    case UI_HDMI_PORT_ID_3:
        //retValue = Hdmi3CurrentEdidVer;
        retValue = (version >> 8) & 0xf;
#ifdef STREAM_BOX
        if (retValue == 0) retValue = HDMI_EDID_VER_20; // Default to 2.0 if not set
#endif
        break;
    case UI_HDMI_PORT_ID_4:
        //retValue = Hdmi4CurrentEdidVer;
        retValue = (version >> 12) & 0xf;
#ifdef STREAM_BOX
        if (retValue == 0) retValue = HDMI_EDID_VER_20; // Default to 2.0 if not set
#endif
        break;
    default:
        LOGD("%s: not hdmi source.\n", __FUNCTION__);
        break;
    }
    return retValue;
}

int CTv::SetVdinWorkMode(vdin_work_mode_t vdinWorkMode)
{
    mVdinWorkMode = vdinWorkMode;
    return 0;
}

int CTv::GetHdmiSPDInfo(tv_source_input_t source, char* data, size_t datalen)
{
    if (mCurrentSource < SOURCE_HDMI1 || mCurrentSource > SOURCE_HDMI4) {
        LOGE("Current source is not HDMI input source.\n");
        return -1;
    }
    if (source != mCurrentSource) {
        LOGE("%d is not current input source, skipped.\n", source);
        return -1;
    }

    int ret = -1;
    struct spd_infoframe_st spd;

    memset(&spd, 0, sizeof(spd));
    ret = mpHDMIRxManager->HdmiRxGetSPDInfoframe(&spd);
    if (ret < 0) {
        LOGE("Get SPD infoframe from HDMIRX manager failed.\n");
    } else {
        if (datalen > sizeof(spd)) {
            memcpy(data, &spd, sizeof(spd));
        } else {
            memcpy(data, &spd, datalen);
        }
    }

    return ret;
}

int CTv::GetFrontendInfo(tvin_frontend_info_t *frontendInfo)
{
    int ret = -1;
    if (frontendInfo == NULL) {
        LOGD("%s: param is NULL.\n", __FUNCTION__);
    } else {
        ret = mpTvin->Tvin_GetFrontendInfo(frontendInfo);
        /*LOGD("%s: scan mode:%d, colorfmt:%d, fps:%d, width:%d, height:%d, colordepth:%d.\n", __FUNCTION__, frontendInfo->scan_mode, frontendInfo->cfmt,
            frontendInfo->fps,frontendInfo->width,frontendInfo->height,frontendInfo->colordepth);*/
    }

    if (ret < 0) {
        LOGE("%s failed.\n", __FUNCTION__);
    } else {
        LOGD("%s success.\n", __FUNCTION__);
    }

    return ret;
}

int CTv::SetColorRangeMode(tvin_color_range_t range_mode)
{
    int ret = -1;
    if ((range_mode >= TVIN_COLOR_RANGE_MAX) || (range_mode <TVIN_COLOR_RANGE_AUTO)) {
        LOGD("%s: invalid range mode.\n", __FUNCTION__);
    } else {
        ret = mpTvin->Tvin_SetColorRangeMode(range_mode);
    }

    if (ret < 0) {
        LOGE("%s failed.\n", __FUNCTION__);
    } else {
        LOGD("%s success.\n", __FUNCTION__);
    }

    return ret;
}

int CTv::GetColorRangeMode()
{
    int ret = mpTvin->Tvin_GetColorRangeMode();
    LOGD("%s: mode is %d.\n", __FUNCTION__, ret);

    return ret;
}

int CTv::GetSourceConnectStatus(tv_source_input_t source)
{
    return mTvDevicesPollDetect.GetSourceConnectStatus(source);
}

int CTv::SetEdidBoostOn(int bBoostOn)
{
    int ret = -1;
    if (mBoostOn == bBoostOn ) {
        LOGD("%s: same EDIDs are loaded for %d, no more action\n", __FUNCTION__, bBoostOn);
        return 0;
    } else {
        LOGD("%s: mBoostOn=%d to be switched to %d\n", __FUNCTION__, mBoostOn, bBoostOn);
        // pass flag for dynamic loading EDID data, switch data to kernel and switch version.
        mBoostOn = bBoostOn;
        ret = LoadEdidData(0, mDolbyVisionEnableState);
        return ret;
    }
}

void CTv::onSigToStable()
{
    LOGD("%s: mVdinWorkMode is %d\n", __FUNCTION__, mVdinWorkMode);
    //start decoder
    /*if (mVdinWorkMode == VDIN_WORK_MODE_VFM) {
        mpTvin->Tvin_StartDecoder(mCurrentSignalInfo);
    } else {
        LOGD("%s: not VFM mode.\n", __FUNCTION__);
    }*/
    mpTvin->Tvin_StartDecoder(mCurrentSignalInfo);

#ifdef HAVE_AUDIO
#ifndef STREAM_BOX_LEGACY
            CTvAudio::getInstance()->set_audio_av_mute(false);
#endif
#endif

    CMessage msg;
    msg.mDelayMs = std::chrono::milliseconds(0);
    msg.mType = CTvMsgQueue::TV_MSG_ENABLE_VIDEO_LATER;
    msg.mpPara[0] = 5;
    mTvMsgQueue->sendMsg ( msg );

    //send signal to apk
    TvEvent::SignalDetectEvent event;
    event.mSourceInput = mCurrentSource;
    event.mFmt = mCurrentSignalInfo.fmt;
    event.mTrans_fmt = mCurrentSignalInfo.trans_fmt;
    event.mStatus = mCurrentSignalInfo.status;
    event.mDviFlag = mCurrentSignalInfo.is_dvi;
    event.mhdr_info = mCurrentSignalInfo.signal_type;
    sendTvEvent(event);
}

void CTv::onSigToUnstable()
{
    mpAmVideo->SetVideoLayerStatus(VIDEO_LAYER_STATUS_DISABLE);
    mpTvin->Tvin_StopDecoder();

#ifdef HAVE_AUDIO
#ifndef STREAM_BOX_LEGACY
            CTvAudio::getInstance()->set_audio_av_mute(true);
#endif
#endif

    LOGD("signal to Unstable!\n");
    //To do
}

void CTv::onSigToUnSupport()
{
    LOGD("%s\n", __FUNCTION__);

    mpAmVideo->SetVideoLayerStatus(VIDEO_LAYER_STATUS_DISABLE);
    mpTvin->Tvin_StopDecoder();

#ifdef HAVE_AUDIO
#ifndef STREAM_BOX_LEGACY
            CTvAudio::getInstance()->set_audio_av_mute(true);
#endif
#endif

    TvEvent::SignalDetectEvent event;
    event.mSourceInput = mCurrentSource;
    event.mFmt = mCurrentSignalInfo.fmt;
    event.mTrans_fmt = mCurrentSignalInfo.trans_fmt;
    event.mStatus = mCurrentSignalInfo.status;
    event.mDviFlag = mCurrentSignalInfo.is_dvi;
    event.mhdr_info = mCurrentSignalInfo.signal_type;
    sendTvEvent(event);
    //To do
}

void CTv::onSigToNoSig()
{
    LOGD("%s\n", __FUNCTION__);

    if (needSnowEffect()) {
        SetSnowShowEnable(true);
        mpTvin->Tvin_StartDecoder(mCurrentSignalInfo);
        mpAmVideo->SetVideoLayerStatus(VIDEO_LAYER_STATUS_ENABLE);

    }  else {
        LOGD("%s video layer has disabled \n", __FUNCTION__);
        mpAmVideo->SetVideoLayerStatus(VIDEO_LAYER_STATUS_DISABLE);
        mpTvin->Tvin_StopDecoder();

    #ifdef HAVE_AUDIO
    #ifndef STREAM_BOX_LEGACY
        CTvAudio::getInstance()->set_audio_av_mute(true);
    #endif
    #endif
    }

    TvEvent::SignalDetectEvent event;
    event.mSourceInput = mCurrentSource;
    event.mFmt = mCurrentSignalInfo.fmt;
    event.mTrans_fmt = mCurrentSignalInfo.trans_fmt;
    event.mStatus = mCurrentSignalInfo.status;
    event.mDviFlag = mCurrentSignalInfo.is_dvi;
    event.mhdr_info = mCurrentSignalInfo.signal_type;
    sendTvEvent (event);
    //To do
}

bool CTv::needSnowEffect()
{
    bool isEnable = false;
    LOGD("%s: mCurrentSource = [%d].\n", __FUNCTION__,mCurrentSource);
    if ((SOURCE_TV == mCurrentSource) && mATVDisplaySnow ) {
        isEnable = true;
        LOGD("%s: ATV:snow display is enabled.\n", __FUNCTION__);
    } else {
        LOGD("%s: ATV:snow display is disabled.\n", __FUNCTION__);
    }

    return isEnable;
}

int CTv::SetSnowShowEnable(bool enable)
{
    LOGD("%s: enable = [%d]\n", __FUNCTION__ , enable);
    if (enable) {
        mLastScreenMode = CVpp::getInstance()->getVideoScreenMode();
        LOGD("%s: Get LastScreenMode = %d\n", __FUNCTION__, mLastScreenMode);
        CVpp::getInstance()->setVideoScreenMode(1);//while show snow,need show full screen
    } else {
        LOGD("%s: Set LastScreenMode = %d\n", __FUNCTION__, mLastScreenMode);
        CVpp::getInstance()->setVideoScreenMode(mLastScreenMode);
    }
    return mpTvin->Tvin_SwitchSnow(enable);
}

int CTv::sendTvEvent(CTvEvent &event)
{
    LOGD("%s\n", __FUNCTION__);

    if (mpObserver != NULL) {
        mpObserver->onTvEvent(event);
    } else {
        LOGD("%s: Observer is NULL.\n", __FUNCTION__);
    }

    return 0;
}

#ifdef HAVE_AUDIO
int CTv::mapSourcetoAudiotupe(tv_source_input_t dest_source)
{
    int ret = -1;
    switch (dest_source) {
        case SOURCE_TV:
            ret = AUDIO_DEVICE_IN_TV_TUNER;
            break;
        case SOURCE_AV1:
        case SOURCE_AV2:
            ret = AUDIO_DEVICE_IN_LINE;
            break;
        case SOURCE_HDMI1:
        case SOURCE_HDMI2:
        case SOURCE_HDMI3:
        case SOURCE_HDMI4:
            ret = AUDIO_DEVICE_IN_HDMI;
            break;
        case SOURCE_SPDIF:
            ret = AUDIO_DEVICE_IN_SPDIF;
            break;
        default:
            ret = AUDIO_DEVICE_IN_LINE;
            break;
    }
    return ret;
}
#endif

bool CTv::GetDolbyVisionSupportStatus(void) {
    //return dv support info from current platform device, not display device.
    char buf[1024+1] = {0};
    int fd, len;
    bool ret = false;

    /*bit0: 0-> efuse, 1->no efuse; */
    /*bit1: 1-> ko loaded*/
    /*bit2: 1-> value updated*/
    /*bit3: 1-> tv */
    int supportInfo = 0;

    constexpr int dvDriverEnabled = (1 << 2);
    constexpr int dvSupported = ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3 ));

    if ((fd = open(SYS_DOLBYVISION_SUPPORT_INFO, O_RDONLY)) < 0) {
        LOGE("open %s fail.\n", SYS_DOLBYVISION_SUPPORT_INFO);
        return false;
    } else {
        if ((len = read(fd, buf, 1024)) < 0) {
            LOGE("read %s error: %s\n", SYS_DOLBYVISION_SUPPORT_INFO, strerror(errno));
            ret =  false;
        } else {
            sscanf(buf, "%d", &supportInfo);
            if ((supportInfo & dvDriverEnabled) == 0) {
                LOGD("%s is not ready\n",SYS_DOLBYVISION_SUPPORT_INFO);
            }
            ret = ((supportInfo & dvSupported) == dvSupported) ? true : false;
        }

        close(fd);
        return ret;
    }
}

int CTv::SetHdmiAllmEnabled(int enable)
{
    LOGD("%s: [%s]\n", __FUNCTION__, enable == 1?"enable":"disable");
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, enable=%d\n", __FUNCTION__, enable);
    fflush(stdout);
#endif
    const char *buf = GetUenv(HDMI_UBOOT_EDID_FEATURE);
    int saveValue = 0;
    if (buf) {
        saveValue = atoi(buf);
        if ((saveValue & 0x1) == enable) {
            LOGD("%s: same status return!\n", __FUNCTION__);
#ifdef STREAM_BOX_TRACE
            printf("[TRACE] %s: Same status, returning early\n", __FUNCTION__);
            fflush(stdout);
#endif
            return 0;
        }
    }
    muteVideoOnHDMISource();
    saveValue = enable|(saveValue & 0b0010);
    SetUenv(HDMI_UBOOT_EDID_FEATURE,  std::to_string(saveValue).c_str());
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call mpHDMIRxManager->SetAllmEnabled(%d)\n", __FUNCTION__, enable);
    fflush(stdout);
#endif
    int ret = mpHDMIRxManager->SetAllmEnabled(enable);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from mpHDMIRxManager->SetAllmEnabled(), ret=%d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    return ret;
}

int CTv::GetHdmiAllmEnabled()
{
    int ret = mpHDMIRxManager->GetAllmEnabled();
    LOGD("%s: [%d]\n", __FUNCTION__, ret);
    return ret;
}

int CTv::SetHdmiVrrEnabled(int enable)
{
    LOGD("%s: [%s]\n", __FUNCTION__, enable == 1?"enable":"disable");
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, enable=%d\n", __FUNCTION__, enable);
    fflush(stdout);
#endif
    const char*buf = GetUenv(HDMI_UBOOT_EDID_FEATURE);
    int saveValue = 0;
    if (buf) {
        saveValue = atoi(buf);
        if ((saveValue >> 1) == enable) {
            LOGD("%s: same status return!\n", __FUNCTION__);
#ifdef STREAM_BOX_TRACE
            printf("[TRACE] %s: Same status, returning early\n", __FUNCTION__);
            fflush(stdout);
#endif
            return 0;
        }
    }
    muteVideoOnHDMISource();
    saveValue = (enable << 1)|(saveValue & 0b0001);
    SetUenv(HDMI_UBOOT_EDID_FEATURE,  std::to_string(saveValue).c_str());
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call mpHDMIRxManager->SetVrrEnabled(%d)\n", __FUNCTION__, enable);
    fflush(stdout);
#endif
    int ret = mpHDMIRxManager->SetVrrEnabled(enable);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from mpHDMIRxManager->SetVrrEnabled(), ret=%d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    return ret;
}

int CTv::GetHdmiVrrEnabled()
{
    int ret = mpHDMIRxManager->GetVrrEnabled();
    LOGD("%s: [%d]\n", __FUNCTION__, ret);
    return ret;
}

#ifdef STREAM_BOX
int CTv::SetGameMode(int enable, int game_mode_value)
{
    LOGD("%s: enable=%d, game_mode_value=%d\n", __FUNCTION__, enable, game_mode_value);
    /* When disabled, pass 0. When enabled, pass game_mode_value (default 3 for VDIN_GAME_MODE_2) */
    return mpTvin->Tvin_SetGameMode(enable ? game_mode_value : 0);
}

int CTv::SetPcMode(int enable)
{
    LOGD("%s: enable=%d\n", __FUNCTION__, enable);
    return mpTvin->Tvin_SetPcMode(enable ? 1 : 0);
}

int CTv::SetForceVrrFrameLock(int enable)
{
    LOGD("%s: enable=%d\n", __FUNCTION__, enable);
    int ret = 0;

    if (enable) {
        // Enable low latency frame lock mode first, then enable VRR
        ret = tvWriteSysfs(VRR_DEBUG_PATH, "mode 1");
        if (ret >= 0) {
            ret = tvWriteSysfs(VRR_DEBUG_PATH, "en 1");
        }
    } else {
        // Disable VRR, then disable low latency frame lock mode
        ret = tvWriteSysfs(VRR_DEBUG_PATH, "en 0");
        if (ret >= 0) {
            ret = tvWriteSysfs(VRR_DEBUG_PATH, "mode 0");
        }
    }

    if (ret < 0) {
        LOGE("%s: Failed to %s force VRR frame lock\n", __FUNCTION__, enable ? "enable" : "disable");
    }
    return ret;
}
#endif

void CTv::muteVideoOnHDMISource() {
    if (mCurrentSource < SOURCE_HDMI1 || mCurrentSource > SOURCE_HDMI4) {
        return;
    }
    mpAmVideo->SetVideoLayerStatus(VIDEO_LAYER_STATUS_DISABLE);
}

void CTv::isVideoFrameAvailable(unsigned int u32NewFrameCount)
{
    unsigned int u32TimeOutCount = 0;
    unsigned int  frameCount = 0;
    while (true) {
        frameCount = (unsigned int)mpAmVideo->GetVideoFrameCount();
        if (frameCount >= u32NewFrameCount) {
            LOGD("%s video available\n", __FUNCTION__);
            break;
        } else {
            if (u32TimeOutCount >= NEW_FRAME_TIME_OUT_COUNT) {
                LOGD("%s Not available frame\n", __FUNCTION__);
                break;
            }
        }
        LOGD("%s new frame count = %d, get time = %d\n",__FUNCTION__, frameCount, u32TimeOutCount);
        usleep(50*1000);
        u32TimeOutCount++;
    }
    mpAmVideo->SetVideoLayerStatus(VIDEO_LAYER_STATUS_ENABLE);
    mpAmVideo->SetVideoGlobalOutputMode(VIDEO_GLOBAL_OUTPUT_MODE_ENABLE);
}

CTv::CTvMsgQueue::CTvMsgQueue(CTv *tv)
{
    mpTv = tv;
}

CTv::CTvMsgQueue::~CTvMsgQueue()
{
}

void CTv::CTvMsgQueue::handleMessage ( CMessage &msg )
{
    LOGD ("%s, CTv::CTvMsgQueue::handleMessage type = %d\n", __FUNCTION__,  msg.mType);

    switch ( msg.mType ) {
    case TV_MSG_ENABLE_VIDEO_LATER: {
        int fc = msg.mpPara[0];
        mpTv->isVideoFrameAvailable(fc);
        break;
    }
    default:
        break;
    }
}

