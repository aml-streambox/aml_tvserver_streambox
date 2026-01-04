/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#include "TvCommon.h"
#include "CTvin.h"
#include "CTvDevicesPollDetect.h"
#include "CTvEvent.h"
#include "CHDMIRxManager.h"
#include "CAmVideo.h"
#include "CMsgQueue.h"

#ifdef HAVE_AUDIO
#include "CTvAudio.h"
#endif
#ifdef 0
#define CONFIG_FILE_PATH_DEF               "/etc/tvconfig/tvconfig.conf"
#define DOLBY_VISION_TV_KO_PATH            "/usr/lib/modules/dovi_tv.ko"
#define DOLBY_VISION_STB_KO_PATH           "/usr/lib/modules/dovi.ko"
#else
#define CONFIG_FILE_PATH_DEF               "/vendor/etc/tvconfig/tvconfig.conf"
#define DOLBY_VISION_TV_KO_PATH            "/vendor/lib/modules/dovi_tv.ko"
#define DOLBY_VISION_STB_KO_PATH           "/vendor/lib/modules/dovi.ko"
#endif

#define DOLBY_VISION_ENABLE_PATH           "/sys/module/amdolby_vision/parameters/dolby_vision_enable"

#define VIDEO_SYNC_ENABLE   "/sys/class/tsync/enable"
#define VIDEO_SYNC_MODE     "/sys/class/tsync/mode"
//0 means do AV sync, 1 means don't do AV sync,just free run.
#define VIDEO_FREERUN_MODE    "/sys/class/video/freerun_mode"
class CTv : public CTvDevicesPollDetect::ISourceConnectObserver {
public:
    class TvIObserver {
    public:
        TvIObserver() {};
        virtual ~TvIObserver() {};
        virtual void onTvEvent (CTvEvent &event) = 0;
    };
    CTv();
    ~CTv();
    int setTvObserver (TvIObserver *ob);
    int StartTv(tv_source_input_t source);
    int StopTv(tv_source_input_t source);
    int SwitchSource(tv_source_input_t dest_source);
    int SetCurrenSourceInfo(tvin_info_t sig_info);
    tvin_info_t GetCurrentSourceInfo(void);
    int SetEDIDData(tv_source_input_t source, char *data);
    int GetEDIDData(tv_source_input_t source, char *data);
    int LoadEdidData(int isNeedBlackScreen, int isDolbyVisionEnable);
    int SetEdidVersion(tv_source_input_t source, tv_hdmi_edid_version_t edidVer);
    int GetEdidVersion(tv_source_input_t source);
    int SetVdinWorkMode(vdin_work_mode_t vdinWorkMode);
    int GetFrontendInfo(tvin_frontend_info_t *frontendInfo);
    int SetColorRangeMode(tvin_color_range_t range_mode);
    int GetColorRangeMode();
    bool needSnowEffect();
    int SetSnowShowEnable(bool enable);
    int GetSourceConnectStatus(tv_source_input_t source);
    int GetHdmiSPDInfo(tv_source_input_t source, char* data, size_t datalen);
    int SetEdidBoostOn(int bBoostOn);
    virtual void onSigDvAllmChange();
    virtual void onSigVrrChange();
    virtual void onSourceConnect(int source, int connect_status);
    virtual void onVdinSignalChange();
    int GetPanelSize();
    bool GetDolbyVisionSupportStatus(void);
    void GetAllmInfo(tvin_latency_s *info);
    int GetVrrMode();
    int SetHdmiAllmEnabled(int enable);
    int GetHdmiAllmEnabled();
    int SetHdmiVrrEnabled(int enable);
    int GetHdmiVrrEnabled();
#ifdef STREAM_BOX
    int SetGameMode(int enable, int game_mode_value = 3);
    int SetPcMode(int enable);
#endif

private:
    void onSigStatusChange(void);
    void onSigToStable();
    void onSigToUnstable();
    void onSigToUnSupport();
    void onSigToNoSig();
    int sendTvEvent(CTvEvent &event);
    void isVideoFrameAvailable(unsigned int u32NewFrameCount);
#ifdef HAVE_AUDIO
    int mapSourcetoAudiotupe(tv_source_input_t dest_source);
#endif
    void muteVideoOnHDMISource();

    volatile int mLastScreenMode;

    CTvin *mpTvin;
    CAmVideo *mpAmVideo;
    CHDMIRxManager *mpHDMIRxManager;
    tvin_info_t mCurrentSignalInfo;
    tv_source_input_t mCurrentSource;
    CTvDevicesPollDetect mTvDevicesPollDetect;
    TvIObserver *mpObserver;
    vdin_work_mode_t mVdinWorkMode = VDIN_WORK_MODE_VFM;
    int mPanelSize = 0;
    int mBoostOn = 0;
    int mDolbyVisionEnableState = 0;
    bool mATVDisplaySnow;

protected:
    class CTvMsgQueue: public CMsgQueueThread {
    public:
        static const int TV_MSG_ENABLE_VIDEO_LATER = 9;
        CTvMsgQueue(CTv *tv);
        ~CTvMsgQueue();
    private:
        virtual void handleMessage ( CMessage &msg );
        CTv *mpTv;
    };

    CTvMsgQueue *mTvMsgQueue;

};
