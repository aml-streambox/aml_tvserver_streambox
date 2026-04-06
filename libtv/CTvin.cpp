/*
 * Copyright (c) 2014 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description: c++ file
 */

#define LOG_MODULE_TAG "TV"
#define LOG_CLASS_TAG "CTvin"

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "CTvin.h"
#include "TvConfigManager.h"
#include "tvutils.h"
#include "CTvLog.h"

CTvin *CTvin::mInstance;

CTvin *CTvin::getInstance()
{
    if (NULL == mInstance) {
        mInstance = new CTvin();
    }

    return mInstance;
}

CTvin::CTvin()
{
    mDecoderStarted = false;
    mVdin0DevFd = VDIN_OpenModule();
    mAfeDevFd = AFE_OpenModule();
    memset(&mTvinParam, 0, sizeof(tvin_parm_t));
    memset(mSourceInputToPortMap, SOURCE_INVALID, sizeof(mSourceInputToPortMap));
}

CTvin::~CTvin()
{
    VDIN_CloseModule();
    AFE_CloseModule();
}

int CTvin::VDIN_OpenModule()
{
    int fd = open (VDIN_DEV_PATH, O_RDWR );
    if ( fd < 0 ) {
        LOGE("Open %s error(%s)!\n", VDIN_DEV_PATH, strerror(errno));
        return -1;
    }
    LOGD ( "%s: Open %s module fd = [%d]\n",__FUNCTION__, VDIN_DEV_PATH, fd );
    return fd;
}

int CTvin::VDIN_CloseModule()
{
    if ( mVdin0DevFd != -1 ) {
        close ( mVdin0DevFd );
        mVdin0DevFd = -1;
    }

    return 0;
}

int CTvin::VDIN_DeviceIOCtl ( int request, ... )
{
    int tmp_ret = -1;
    va_list ap;
    void *arg;

    if (mVdin0DevFd < 0) {
        mVdin0DevFd = VDIN_OpenModule();
    }

    if ( mVdin0DevFd >= 0 ) {
        va_start ( ap, request );
        arg = va_arg ( ap, void * );
        va_end ( ap );

        tmp_ret = ioctl ( mVdin0DevFd, request, arg );
        LOGD ( "%s: ret = %d\n",__FUNCTION__,tmp_ret);
        return tmp_ret;
    }

    return -1;
}

int CTvin::VDIN_OpenPort ( tvin_port_t port )
{
    tvin_parm_s vdinParam;
    vdinParam.port = port;
    vdinParam.index = 0;
    int rt = VDIN_DeviceIOCtl ( TVIN_IOC_OPEN, &vdinParam );
    if ( rt < 0 ) {
        LOGE("Vdin open port, error(%s)!\n", strerror(errno));
    }

    return rt;
}

int CTvin::VDIN_ClosePort()
{
    int rt = VDIN_DeviceIOCtl ( TVIN_IOC_CLOSE );
    if ( rt < 0 ) {
        LOGE("Vdin close port, error(%s)!\n", strerror(errno));
    }

    return rt;
}

int CTvin::VDIN_StartDec(tvin_parm_s *vdinParam)
{
    int ret = -1;
    if ( vdinParam == NULL ) {
        return ret;
    }

    LOGD("VDIN_StartDec: index = [%d] port = [0x%x] format = [0x%x]\n",
        vdinParam->index, ( unsigned int ) vdinParam->port, ( unsigned int ) ( vdinParam->info.fmt ));
    ret = VDIN_DeviceIOCtl(TVIN_IOC_START_DEC, vdinParam);
    if ( ret < 0 ) {
        LOGE("Vdin start decode, error(%s)!\n", strerror ( errno ));
    }

    return ret;
}

int CTvin::VDIN_StopDec()
{
    int ret = VDIN_DeviceIOCtl ( TVIN_IOC_STOP_DEC );
    if (ret < 0) {
        LOGE("Vdin stop decode, error(%s).\n", strerror ( errno ));
    }
    return ret;
}

int CTvin::VDIN_GetSignalEventInfo(vdin_event_info_s *SignalEventInfo)
{
    int ret = VDIN_DeviceIOCtl(TVIN_IOC_G_EVENT_INFO, SignalEventInfo);
    if (ret < 0) {
        LOGE("%s error(%s), ret = %d.\n", __FUNCTION__, strerror(errno), ret);
    }

    return ret;
}

int CTvin::VDIN_GetSignalInfo ( struct tvin_info_s *SignalInfo )
{
    int ret = VDIN_DeviceIOCtl ( TVIN_IOC_G_SIG_INFO, SignalInfo );
    if ( ret < 0 ) {
        LOGE("%s failed, error(%s).\n", __FUNCTION__, strerror ( errno ));
    }
    return ret;
}

int CTvin::VDIN_GetAllmInfo(tvin_latency_s *AllmInfo)
{
    int ret = -1;
    if (AllmInfo == NULL) {
        LOGE("%s: param is NULL;\n", __FUNCTION__);
    } else {
        ret = VDIN_DeviceIOCtl(TVIN_IOC_GET_LATENCY_MODE, AllmInfo);
        if (ret < 0) {
            LOGE("%s: ioctl failed!\n", __FUNCTION__);
        }
    }

    return ret;
}

int CTvin::VDIN_SetVdinParam (tvin_parm_s *vdinParam)
{
    int ret = VDIN_DeviceIOCtl ( TVIN_IOC_S_PARM, vdinParam );
    if ( ret < 0 ) {
        LOGE ( "Vdin set signal param, error(%s).\n", strerror ( errno ) );
    }

    return ret;
}

int CTvin::VDIN_GetVdinParam(tvin_parm_s *vdinParam)
{
    int ret = VDIN_DeviceIOCtl ( TVIN_IOC_G_PARM, vdinParam );
    if ( ret < 0 ) {
        LOGE ( "Vdin get signal param, error(%s).\n", strerror ( errno ) );
    }

    return ret;
}

int CTvin::VDIN_SetColorRangeMode(tvin_color_range_t range_mode)
{
    LOGD("mode = %d\n", range_mode);
    int ret = VDIN_DeviceIOCtl ( TVIN_IOC_SET_COLOR_RANGE, &range_mode );
    if ( ret < 0 ) {
        LOGE ( "Vdin Set ColorRange Mode error(%s)!\n", strerror(errno ));
    }

    return ret;
}

int CTvin::VDIN_GetColorRangeMode(void)
{
    int range_mode = TVIN_COLOR_RANGE_AUTO;
    int ret = VDIN_DeviceIOCtl ( TVIN_IOC_GET_COLOR_RANGE, &range_mode );
    if ( ret < 0 ) {
        LOGE ( "Vdin Get ColorRange Mode error(%s)!\n", strerror(errno ));
    }
    LOGD("%s: mode = %d\n", __FUNCTION__, range_mode);

    return range_mode;
}

// AFE
int CTvin::AFE_OpenModule ( void )
{
    int fd = open ( AFE_DEV_PATH, O_RDWR );
    if ( fd < 0 ) {
        LOGE ( "Open tvafe module, error(%s).\n", strerror ( errno ) );
        return -1;
    }
    LOGD ( "%s: Open %s module fd = [%d]\n",__FUNCTION__, AFE_DEV_PATH, fd );
    return fd;
}

int CTvin::AFE_CloseModule ( void )
{
    if ( mAfeDevFd >= 0 ) {
        close ( mAfeDevFd );
        mAfeDevFd = -1;
    }

    return 0;
}

int CTvin::AFE_DeviceIOCtl ( int request, ... )
{
    int tmp_ret = -1;
    va_list ap;
    void *arg;

    if (mAfeDevFd < 0) {
        mAfeDevFd = AFE_OpenModule();
    }

    if ( mAfeDevFd >= 0 ) {
        va_start ( ap, request );
        arg = va_arg ( ap, void * );
        va_end ( ap );

        tmp_ret = ioctl ( mAfeDevFd, request, arg );
        LOGD ( "%s: ret = %d\n",__FUNCTION__,tmp_ret);
        AFE_CloseModule();

        return tmp_ret;
    }

    return -1;
}

int CTvin::Tvin_OpenPort(tvin_port_t source_port)
{
    LOGD ("%s, source_port = %x!\n", __FUNCTION__,  source_port);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, source_port = 0x%x\n", __FUNCTION__, source_port);
    fflush(stdout);
#endif

#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call VDIN_OpenPort(source_port=0x%x)\n", __FUNCTION__, source_port);
    fflush(stdout);
#endif
    int ret = VDIN_OpenPort(source_port);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from VDIN_OpenPort(), ret = %d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif

#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT, returning ret = %d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    return ret;
}

int CTvin::Tvin_ClosePort(tvin_port_t source_port)
{
    LOGD ("%s, source_port = %x!\n", __FUNCTION__,  source_port);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, source_port = 0x%x\n", __FUNCTION__, source_port);
    fflush(stdout);
#endif

#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call VDIN_ClosePort()\n", __FUNCTION__);
    fflush(stdout);
#endif
    int ret = VDIN_ClosePort();
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from VDIN_ClosePort(), ret = %d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif

#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT, returning ret = %d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    return ret;
}

tv_source_input_type_t CTvin::Tvin_SourceInputToSourceInputType ( tv_source_input_t source_input )
{
    tv_source_input_type_t ret = SOURCE_TYPE_MPEG;
    switch (source_input) {
        case SOURCE_TV:
            ret = SOURCE_TYPE_TV;
            break;
        case SOURCE_AV1:
        case SOURCE_AV2:
            ret = SOURCE_TYPE_AV;
            break;
        case SOURCE_YPBPR1:
        case SOURCE_YPBPR2:
            ret = SOURCE_TYPE_COMPONENT;
            break;
        case SOURCE_VGA:
            ret = SOURCE_TYPE_VGA;
            break;
        case SOURCE_HDMI1:
        case SOURCE_HDMI2:
        case SOURCE_HDMI3:
        case SOURCE_HDMI4:
            ret = SOURCE_TYPE_HDMI;
            break;
        case SOURCE_IPTV:
            ret = SOURCE_TYPE_IPTV;
            break;
        case SOURCE_SPDIF:
            ret = SOURCE_TYPE_SPDIF;
            break;
        default:
            ret = SOURCE_TYPE_MPEG;
            break;
    }

    return ret;
}

tv_source_input_type_t CTvin::Tvin_SourcePortToSourceInputType ( tvin_port_t source_port )
{
    tv_source_input_t source_input = Tvin_PortToSourceInput(source_port);
    return Tvin_SourceInputToSourceInputType(source_input);
}

tvin_port_t CTvin::Tvin_GetSourcePortBySourceType ( tv_source_input_type_t source_type )
{
    tv_source_input_t source_input;

    switch (source_type) {
        case SOURCE_TYPE_TV:
            source_input = SOURCE_TV;
            break;
        case SOURCE_TYPE_AV:
            source_input = SOURCE_AV1;
            break;
        case SOURCE_TYPE_COMPONENT:
            source_input = SOURCE_YPBPR1;
            break;
        case SOURCE_TYPE_VGA:
            source_input = SOURCE_VGA;
            break;
        case SOURCE_TYPE_HDMI:
            source_input = SOURCE_HDMI1;
            break;
        case SOURCE_TYPE_IPTV:
            source_input = SOURCE_IPTV;
            break;
        case SOURCE_TYPE_SPDIF:
            source_input = SOURCE_SPDIF;
            break;
        default:
            source_input = SOURCE_MPEG;
            break;
    }

    return Tvin_GetSourcePortBySourceInput(source_input);
}

tvin_port_t CTvin::Tvin_GetSourcePortBySourceInput ( tv_source_input_t source_input )
{
    tvin_port_t source_port = TVIN_PORT_NULL;

    if ( source_input < SOURCE_TV || source_input >= SOURCE_MAX ) {
        source_port = TVIN_PORT_NULL;
    } else {
        source_port = ( tvin_port_t ) mSourceInputToPortMap[ ( int ) source_input];
    }

    return source_port;
}

tv_source_input_t CTvin::Tvin_PortToSourceInput ( tvin_port_t port )
{
    for ( int i = SOURCE_TV; i < SOURCE_MAX; i++ ) {
        if ( mSourceInputToPortMap[i] == (int)port ) {
            return (tv_source_input_t)i;
        }
    }

    return SOURCE_MAX;
}

tvin_port_id_t CTvin::Tvin_GetHdmiPortIdBySourceInput(tv_source_input_t source_input)
{
    tvin_port_id_t portId = TVIN_PORT_ID_MAX;
    if ((source_input > SOURCE_HDMI4) || (source_input < SOURCE_HDMI1)) {
        LOGD("%s: not HDMI Source.\n", __FUNCTION__);
    } else {
        tvin_port_t portValue = Tvin_GetSourcePortBySourceInput(source_input);
        switch (portValue) {
        case TVIN_PORT_HDMI0:
            portId = TVIN_PORT_ID_1;
            break;
        case TVIN_PORT_HDMI1:
            portId = TVIN_PORT_ID_2;
            break;
        case TVIN_PORT_HDMI2:
            portId = TVIN_PORT_ID_3;
            break;
        case TVIN_PORT_HDMI3:
            portId = TVIN_PORT_ID_4;
            break;
        default:
            portId = TVIN_PORT_ID_MAX;
            break;
        }
    }

    LOGD("%s: source: %d, portId: %d.\n", __FUNCTION__, source_input, portId);
    return portId;
}

tvin_port_t CTvin::Tvin_GetVdinPortByVdinPortID(tvin_port_id_t portId)
{
    tvin_port_t portValue = TVIN_PORT_NULL;
    switch (portId) {
    case TVIN_PORT_ID_1:
        portValue = TVIN_PORT_HDMI0;
        break;
    case TVIN_PORT_ID_2:
        portValue =TVIN_PORT_HDMI1;
        break;
    case TVIN_PORT_ID_3:
        portValue = TVIN_PORT_HDMI2;
        break;
    case  TVIN_PORT_ID_4 :
        portValue = TVIN_PORT_HDMI3;
        break;
    default:
        portValue = TVIN_PORT_NULL;
        break;
    }
    LOGD("%s: portId: %d, portValue: 0x%x.\n", __FUNCTION__, portId, portValue);
    return portValue;
}

ui_hdmi_port_id_t CTvin::Tvin_GetUIHdmiPortIdBySourceInput(tv_source_input_t source_input)
{
    ui_hdmi_port_id_t portId = UI_HDMI_PORT_ID_MAX;
    if ((source_input > SOURCE_HDMI4) || (source_input < SOURCE_HDMI1)) {
        LOGD("%s: not HDMI Source.\n", __FUNCTION__);
    } else {
        switch (source_input) {
        case SOURCE_HDMI1:
            portId = UI_HDMI_PORT_ID_1;
            break;
        case SOURCE_HDMI2:
            portId = UI_HDMI_PORT_ID_2;
            break;
        case SOURCE_HDMI3:
            portId = UI_HDMI_PORT_ID_3;
            break;
        case SOURCE_HDMI4:
            portId = UI_HDMI_PORT_ID_4;
            break;
        default:
            portId = UI_HDMI_PORT_ID_MAX;
            break;
        }
    }

    LOGD("%s: source: %d, UIHDMIportId: %d.\n", __FUNCTION__, source_input, portId);
    return portId;
}

int CTvin::Tvin_GetFrontendInfo(tvin_frontend_info_t *frontendInfo)
{
    int ret = -1;
    if (frontendInfo == NULL) {
        LOGE("%s: frontendInfo is NULL!\n", __FUNCTION__);
    } else {
        ret = VDIN_DeviceIOCtl(TVIN_IOC_G_FRONTEND_INFO, frontendInfo);
        if (ret < 0) {
            LOGE("%s error(%s)!\n", __FUNCTION__, strerror(errno));
        }
    }

    return ret;
}

int CTvin::Tvin_SetColorRangeMode(tvin_color_range_t range_mode)

{
    return VDIN_SetColorRangeMode(range_mode);
}

int CTvin::Tvin_GetColorRangeMode()
{
    return VDIN_GetColorRangeMode();
}

unsigned int CTvin::Tvin_TransPortStringToValue(const char *port_str)
{
    if (strcasecmp(port_str, "TVIN_PORT_CVBS0") == 0) {
        return TVIN_PORT_CVBS0;
    } else if (strcasecmp(port_str, "TVIN_PORT_CVBS1") == 0) {
        return TVIN_PORT_CVBS1;
    } else if (strcasecmp(port_str, "TVIN_PORT_CVBS2") == 0) {
        return TVIN_PORT_CVBS2;
    } else if (strcasecmp(port_str, "TVIN_PORT_CVBS3") == 0) {
        return TVIN_PORT_CVBS3;
    } else if (strcasecmp(port_str, "TVIN_PORT_HDMI0") == 0) {
        return TVIN_PORT_HDMI0;
    } else if (strcasecmp(port_str, "TVIN_PORT_HDMI1") == 0) {
        return TVIN_PORT_HDMI1;
    } else if (strcasecmp(port_str, "TVIN_PORT_HDMI2") == 0) {
        return TVIN_PORT_HDMI2;
    } else if (strcasecmp(port_str, "TVIN_PORT_HDMI3") == 0) {
        return TVIN_PORT_HDMI3;
    }

    return TVIN_PORT_NULL;
}

void CTvin::Tvin_LoadSourceInputToPortMap()
{
    const char *config_value = NULL;
    config_value = ConfigGetStr(CFG_SECTION_SRC_INPUT, CFG_TVCHANNEL_ATV, "TVIN_PORT_CVBS3");
    mSourceInputToPortMap[SOURCE_TV] = Tvin_TransPortStringToValue(config_value);
    config_value = ConfigGetStr(CFG_SECTION_SRC_INPUT, CFG_TVCHANNEL_AV1, "TVIN_PORT_CVBS1");
    mSourceInputToPortMap[SOURCE_AV1] = Tvin_TransPortStringToValue(config_value);
    config_value = ConfigGetStr(CFG_SECTION_SRC_INPUT, CFG_TVCHANNEL_AV2, "TVIN_PORT_CVBS2");
    mSourceInputToPortMap[SOURCE_AV2] = Tvin_TransPortStringToValue(config_value);
    config_value = ConfigGetStr(CFG_SECTION_SRC_INPUT, CFG_TVCHANNEL_YPBPR1, "TVIN_PORT_COMP0");
    mSourceInputToPortMap[SOURCE_YPBPR1] = Tvin_TransPortStringToValue(config_value);
    config_value = ConfigGetStr(CFG_SECTION_SRC_INPUT, CFG_TVCHANNEL_YPBPR2, "TVIN_PORT_COMP1");
    mSourceInputToPortMap[SOURCE_YPBPR2] = Tvin_TransPortStringToValue(config_value);
    config_value = ConfigGetStr(CFG_SECTION_SRC_INPUT, CFG_TVCHANNEL_HDMI1, "TVIN_PORT_HDMI0");
    mSourceInputToPortMap[SOURCE_HDMI1] = Tvin_TransPortStringToValue(config_value);
    config_value = ConfigGetStr(CFG_SECTION_SRC_INPUT, CFG_TVCHANNEL_HDMI2, "TVIN_PORT_HDMI2");
    mSourceInputToPortMap[SOURCE_HDMI2] = Tvin_TransPortStringToValue(config_value);
    config_value = ConfigGetStr(CFG_SECTION_SRC_INPUT, CFG_TVCHANNEL_HDMI3, "TVIN_PORT_HDMI1");
    mSourceInputToPortMap[SOURCE_HDMI3] = Tvin_TransPortStringToValue(config_value);
    config_value = ConfigGetStr(CFG_SECTION_SRC_INPUT, CFG_TVCHANNEL_HDMI4, "TVIN_PORT_HDMI3");
    mSourceInputToPortMap[SOURCE_HDMI4] = Tvin_TransPortStringToValue(config_value);
    config_value = ConfigGetStr(CFG_SECTION_SRC_INPUT, CFG_TVCHANNEL_VGA, "TVIN_PORT_VGA0");
    mSourceInputToPortMap[SOURCE_VGA] = Tvin_TransPortStringToValue(config_value);
    mSourceInputToPortMap[SOURCE_MPEG] = TVIN_PORT_MPEG0;
    mSourceInputToPortMap[SOURCE_IPTV] = TVIN_PORT_BT656;
    mSourceInputToPortMap[SOURCE_SPDIF] = TVIN_PORT_CVBS3;
}

int CTvin::Tvin_StartDecoder(tvin_info_t info)
{
    if (mDecoderStarted) {
        LOGD("decoder already started.\n");
        return 0;
    } else {
        mTvinParam.info = info;

        /*
         * Disable AFBCE (Amlogic Frame Buffer Compression Encoder) and
         * double-write so vdin0 writes full-resolution uncompressed
         * MIF frames.  The DTS defaults (afbce_bit_mode=0x11,
         * double_write_en) cause 4K input to be decimated 4x
         * (3840→960, 2160→540) in the MIF path because double-write
         * stores a shrunk copy while AFBCE stores the full-size
         * compressed frame — but downstream GPU/encoder consumers
         * need the uncompressed full-resolution MIF buffer.
         *
         * afbce_flag must be cleared FIRST because the kernel's
         * vdin_double_write_confirm() only enables double_wr when
         * both double_wr_cfg AND afbce_valid are true.  Clearing
         * afbce_flag ensures afbce_valid=0 at the next session start.
         *
         * Must be set before every TVIN_IOC_START_DEC since the
         * kernel reinitializes these from DTS defaults on each
         * decoder start.
         */
        tvWriteSysfs("/sys/class/vdin/vdin0/attr", "afbce_flag 0");
        tvWriteSysfs("/sys/class/vdin/vdin0/attr", "double_write 0");

        if (VDIN_StartDec(&mTvinParam) >= 0 ) {
            LOGD("StartDecoder succeed.\n");
            mDecoderStarted = true;
            return 0;
        } else {
            LOGE("StartDecoder failed.\n");
            mDecoderStarted = false;
            return -1;
        }
    }
}

int CTvin::Tvin_StopDecoder()
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, mDecoderStarted=%d\n", __FUNCTION__, mDecoderStarted);
    fflush(stdout);
#endif
    if (!mDecoderStarted) {
        LOGD("Decoder don't started!\n");
        return 0;
    } else {
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: About to call VDIN_StopDec()\n", __FUNCTION__);
        fflush(stdout);
#endif
        if ( VDIN_StopDec() >= 0 ) {
            LOGD("StopDecoder ok!\n");
            mDecoderStarted = false;
#ifdef STREAM_BOX_TRACE
            printf("[TRACE] %s: EXIT, success\n", __FUNCTION__);
            fflush(stdout);
#endif
            return 0;
        } else {
            LOGE("StopDecoder failed!\n");
            mDecoderStarted = false;
#ifdef STREAM_BOX_TRACE
            printf("[TRACE] %s: EXIT, failed\n", __FUNCTION__);
            fflush(stdout);
#endif
            return -1;
        }
    }
}

void CTvin::Tvin_ForceResetDecoder()
{
    /*
     * Unconditionally issue STOP_DEC + CLOSE to clear any stale kernel
     * state left by a previous unclean shutdown (e.g. SIGKILL).
     *
     * When the old process is killed without calling TVIN_IOC_STOP_DEC,
     * the kernel vdin0 driver retains VDIN_FLAG_DEC_STARTED (0x02) and
     * VDIN_FLAG_DEC_OPENED (0x04) in devp->flags.  The new process's
     * Tvin_StopDecoder() skips the ioctl because mDecoderStarted is
     * false (freshly constructed), so the stale kernel flags persist.
     * A subsequent TVIN_IOC_START_DEC then returns -EBUSY because the
     * kernel thinks the decoder is already running.
     *
     * We ignore errors here: STOP_DEC returns -EPERM if not started,
     * CLOSE returns -EPERM if not opened — both are harmless.
     */
    LOGD("%s: forcing STOP_DEC + CLOSE to clear any stale vdin0 state\n",
         __FUNCTION__);

    int ret = VDIN_DeviceIOCtl(TVIN_IOC_STOP_DEC);
    if (ret < 0) {
        LOGD("%s: STOP_DEC returned %d (%s) — OK if not started\n",
             __FUNCTION__, ret, strerror(errno));
    } else {
        LOGD("%s: STOP_DEC succeeded — cleared stale DEC_STARTED\n",
             __FUNCTION__);
    }

    ret = VDIN_DeviceIOCtl(TVIN_IOC_CLOSE);
    if (ret < 0) {
        LOGD("%s: CLOSE returned %d (%s) — OK if not opened\n",
             __FUNCTION__, ret, strerror(errno));
    } else {
        LOGD("%s: CLOSE succeeded — cleared stale DEC_OPENED\n",
             __FUNCTION__);
    }

    mDecoderStarted = false;
}

int CTvin::Tvin_SwitchSnow(bool enable)
{
    int ret = -1;
    if ( enable ) {
        LOGD("%s: set snow enable\n", __FUNCTION__ );
        ret = AFE_DeviceIOCtl( TVIN_IOC_S_AFE_SNOW_ON );
        ret = VDIN_DeviceIOCtl( TVIN_IOC_SNOW_ON );
    } else {
        LOGD("%s: set snow disable\n", __FUNCTION__ );
        ret = AFE_DeviceIOCtl( TVIN_IOC_S_AFE_SNOW_OFF );
        ret = VDIN_DeviceIOCtl( TVIN_IOC_SNOW_OFF );
    }

    return ret;
}

int CTvin::Tvin_GetSignalEventInfo(vdin_event_info_s *SignalEventInfo)
{
    int ret = -1;
    if (SignalEventInfo == NULL) {
        LOGE("Tvin_GetSignalEventInfo: SignalEventInfo is NULL.\n");
    } else {
        ret = VDIN_GetSignalEventInfo(SignalEventInfo);
    }

    return ret;
}

int CTvin::Tvin_GetSignalInfo(tvin_info_s *SignalInfo)
{
    int ret = -1;
    if (SignalInfo == NULL) {
        LOGE("Tvin_GetSignalInfo: SignalInfo is NULL.\n");
    } else {
        ret = VDIN_GetSignalInfo(SignalInfo);
    }

    return ret;
}

int CTvin::Tvin_GetAllmInfo(tvin_latency_s *AllmInfo)
{
    int ret = -1;

    if (AllmInfo == NULL)
        LOGE("%s: param is NULL;\n", __FUNCTION__);
    else
        ret = VDIN_GetAllmInfo(AllmInfo);

    return ret;
}

int CTvin::VDIN_GetVrrFreesyncParm(struct vdin_vrr_freesync_param_s *vrrparm)
{
    int ret = -1;
    if (vrrparm == NULL) {
        LOGE("%s: param is NULL\n", __FUNCTION__);
    } else {
        ret = VDIN_DeviceIOCtl(TVIN_IOC_G_VRR_STATUS, vrrparm);
        if (ret < 0) {
            LOGE("%s failed, error(%s)\n", __FUNCTION__, strerror(errno));
        }
    }

    return ret;
}

#ifdef STREAM_BOX
int CTvin::Tvin_SetGameMode(unsigned int enable)
{
    LOGD("%s: enable=%u\n", __FUNCTION__, enable);
    int ret = VDIN_DeviceIOCtl(TVIN_IOC_GAME_MODE, &enable);
    if (ret < 0) {
        LOGE("%s failed, error(%s)\n", __FUNCTION__, strerror(errno));
    }
    return ret;
}

int CTvin::Tvin_SetPcMode(unsigned int enable)
{
    LOGD("%s: enable=%u\n", __FUNCTION__, enable);
    int ret = VDIN_DeviceIOCtl(TVIN_IOC_S_PC_MODE, &enable);
    if (ret < 0) {
        LOGE("%s failed, error(%s)\n", __FUNCTION__, strerror(errno));
    }
    return ret;
}
#endif

int CTvin::Tvin_GetVdinDeviceFd(void)
{
    int ret = -1;
    if (mVdin0DevFd < 0) {
        ret = VDIN_OpenModule();
    } else {
        ret = mVdin0DevFd;
    }

    return ret;
}

int CTvin::VDIN_AddPath ( const char *videopath )
{
    if (strlen(videopath) > 1024) {
        LOGE("video path too long\n");
        return -1;
    }

    char str[1024 + 1] = {0};
    sprintf (str, "%s", videopath);
    return tvWriteSysfs(SYS_VFM_MAP_PATH, str);
}

int CTvin::VDIN_RemovePath(tv_path_type_t pathtype)

{
    int ret = -1;
    switch (pathtype) {
        case TV_PATH_TYPE_DEFAULT:
            ret = tvWriteSysfs(SYS_VFM_MAP_PATH, "rm default");
            break;
        case TV_PATH_TYPE_TVIN:
            ret = tvWriteSysfs(SYS_VFM_MAP_PATH, "rm tvpath");
            break;
        default:
            LOGE("invalie videopath type!\n");
            break;
    }

    return ret;
}

int CTvin::Tvin_AddVideoPath(int selPath)
{
    int ret = -1;
    std::string vdinPath;
    std::string suffixVideoPath("deinterlace amvideo");
    bool amlvideo2Exist = isFileExist(AMLVIDEO2_DEV_PATH);
    bool vfmCapExist = isFileExist("/dev/video_cap");
    switch ( selPath ) {
    case TV_PATH_VDIN_AMLVIDEO2_PPMGR_DEINTERLACE_AMVIDEO:
        if (amlvideo2Exist && vfmCapExist)
            vdinPath = "add tvpath vdin0 vfm_cap amlvideo2.0 ";
        else if (amlvideo2Exist)
            vdinPath = "add tvpath vdin0 amlvideo2.0 ";
        else if (vfmCapExist)
            vdinPath = "add tvpath vdin0 vfm_cap ";
        else
            vdinPath = "add tvpath vdin0 ";
        break;

    case TV_PATH_DECODER_AMLVIDEO2_PPMGR_DEINTERLACE_AMVIDEO:
        if (amlvideo2Exist)
            vdinPath = "add default decoder amlvideo2.0 ppmgr ";
        else
            vdinPath = "add default decoder ppmgr ";
        break;

    case TV_PATH_VDIN_VFMCAP_ONLY:
        if (vfmCapExist) {
            vdinPath = "add tvpath vdin0 vfm_cap";
            ret = VDIN_AddPath(vdinPath.c_str());
            return ret; /* skip suffixVideoPath append */
        } else {
            LOGE("TV_PATH_VDIN_VFMCAP_ONLY requested but vfm_cap device not found!\n");
            return -1;
        }

    default:
        break;
    }

    vdinPath += suffixVideoPath;
    ret = VDIN_AddPath (vdinPath.c_str());
    return ret;
}

int CTvin::Tvin_RemoveVideoPath(tv_path_type_t pathtype)
{
    int ret = -1;
    int i = 0, dly = 10;
    for ( i = 0; i < 50; i++ ) {
        ret = VDIN_RemovePath(pathtype);
        if ( ret > 0 ) {
            LOGE("remove default path ok, %d ms gone.\n", (dly * i));
            break;
        } else {
            LOGE("remove default path faild, %d ms gone.\n", (dly * i));
            usleep(dly * 1000);
        }
    }
      return ret;
}

int CTvin::Tvin_CheckVideoPathComplete(tv_path_type_t path_type)
{
    int ret = -1;
    FILE *fp = NULL;
    char path[100] = {0};
    char decoder_str[20] = "default {";
    char tvin_str[20] = "tvpath {";
    char *str_find = NULL;
    char di_str[16] = "deinterlace";
    char ppmgr_str[16] = "ppmgr";
    char amvideo_str[16] = "amvideo";

    fp = fopen(SYS_VFM_MAP_PATH, "r");
    if (!fp) {
        LOGE("%s, can not open %s!\n", SYS_VFM_MAP_PATH);
        return ret;
    }

    while (fgets(path, sizeof(path)-1, fp)) {
        if (path_type == TV_PATH_TYPE_DEFAULT) {
            str_find = strstr(path, decoder_str);
        } else if (path_type == TV_PATH_TYPE_TVIN) {
            str_find = strstr(path, tvin_str);
        } else {
            break;
        }

        if (str_find != NULL) {
            if ((strstr(str_find, di_str)) &&
                 (strstr(str_find, ppmgr_str)) &&
                 (strstr(str_find, amvideo_str))) {
                LOGD("VideoPath is complete!\n");
                ret = 0;
            } else {
                LOGD("VideoPath is not complete!\n");
                ret = -1;
            }
            break;
        }
    }

    fclose(fp);
    fp = NULL;

    return ret;
}

