/*
 * Copyright (c) 2014 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description: header file
 */

#ifndef _CTVIN_H
#define _CTVIN_H

#include "TvCommon.h"

// ***************************************************************************
// *** TVIN general definition/enum/struct ***********************************
// ***************************************************************************
typedef enum tvin_port_id_e {
    TVIN_PORT_ID_1 = 1,
    TVIN_PORT_ID_2,
    TVIN_PORT_ID_3,
    TVIN_PORT_ID_4,
    TVIN_PORT_ID_MAX,
} tvin_port_id_t;

//The name index of EDID files in the path /vendor/etc/tvconfig/hdmi/, is start from 1.
//port1_14_dv.bin
//...
//port3_14_dv.bin
typedef enum ui_hdmi_port_id_e {
    UI_HDMI_PORT_ID_1 = 1,//Means HDMI1 in UI
    UI_HDMI_PORT_ID_2,
    UI_HDMI_PORT_ID_3,
    UI_HDMI_PORT_ID_4,
    UI_HDMI_PORT_ID_MAX,
} ui_hdmi_port_id_t;

typedef struct tvin_info_s  tvin_info_t;

typedef struct tvin_parm_s tvin_parm_t;

typedef struct tvin_frontend_info_s tvin_frontend_info_t;

typedef enum tvin_sg_chg_flg tvin_sig_change_flag_t;

typedef struct vdin_event_info vdin_event_info_s;

// ***************************************************************************
// *** function type definition **********************************************
// ***************************************************************************
typedef enum tv_path_type_e {
    TV_PATH_TYPE_DEFAULT,
    TV_PATH_TYPE_TVIN,
    TV_PATH_TYPE_MAX,
} tv_path_type_t;

typedef enum tv_path_status_e {
    TV_PATH_STATUS_NO_DEV = -2,
    TV_PATH_STATUS_ERROR = -1,
    TV_PATH_STATUS_INACTIVE = 0,
    TV_PATH_STATUS_ACTIVE = 1,
    TV_PATH_STATUS_MAX,
} tv_path_status_t;

enum {
    TV_PATH_VDIN_AMLVIDEO2_PPMGR_DEINTERLACE_AMVIDEO,
    TV_PATH_DECODER_AMLVIDEO2_PPMGR_DEINTERLACE_AMVIDEO,
};

#define VDIN_DEV_PATH               "/dev/vdin0"
#define AFE_DEV_PATH                "/dev/tvafe0"
#define AMLVIDEO2_DEV_PATH          "/dev/video11"
#define SYS_VFM_MAP_PATH            "/sys/class/vfm/map"

class CTvin {
public:
    CTvin();
    ~CTvin();
    static CTvin *getInstance();
    int Tvin_OpenPort(tvin_port_t source_port);
    int Tvin_ClosePort(tvin_port_t source_port);
    int Tvin_StartDecoder(tvin_info_t info);
    int Tvin_StopDecoder(void);
    int Tvin_SwitchSnow(bool enable);
    int Tvin_GetSignalEventInfo(vdin_event_info_s *SignalEventInfo);
    int Tvin_GetSignalInfo(tvin_info_s *SignalInfo);
    int Tvin_GetAllmInfo(tvin_latency_s *AllmInfo);
    int VDIN_GetVrrFreesyncParm(struct vdin_vrr_freesync_param_s *vrrparm);
#ifdef STREAM_BOX
    int Tvin_SetGameMode(unsigned int enable);
    int Tvin_SetPcMode(unsigned int enable);
#endif
    int Tvin_GetVdinDeviceFd(void);
    int Tvin_CheckVideoPathComplete(tv_path_type_t path_type);
    int Tvin_AddVideoPath(int selPath);
    int Tvin_RemoveVideoPath(tv_path_type_t pathtype);
    void Tvin_LoadSourceInputToPortMap(void);
    tv_source_input_type_t Tvin_SourcePortToSourceInputType(tvin_port_t source_port);
    tv_source_input_type_t Tvin_SourceInputToSourceInputType(tv_source_input_t source_input);
    tvin_port_t Tvin_GetSourcePortBySourceType(tv_source_input_type_t source_type);
    tvin_port_t Tvin_GetSourcePortBySourceInput(tv_source_input_t source_input);
    unsigned int Tvin_TransPortStringToValue(const char *port_str);
    tv_source_input_t Tvin_PortToSourceInput(tvin_port_t port);
    tvin_port_id_t Tvin_GetHdmiPortIdBySourceInput(tv_source_input_t source_input);
    tvin_port_t Tvin_GetVdinPortByVdinPortID(tvin_port_id_t portId);
    ui_hdmi_port_id_t Tvin_GetUIHdmiPortIdBySourceInput(tv_source_input_t source_input);
    int Tvin_GetFrontendInfo(tvin_frontend_info_t *frontendInfo);
    int Tvin_SetColorRangeMode(tvin_color_range_t range_mode);
    int Tvin_GetColorRangeMode(void);
private:
    //VDIN
    int VDIN_OpenModule();
    int VDIN_CloseModule();
    int VDIN_DeviceIOCtl(int request, ...);
    int VDIN_OpenPort(tvin_port_t port);
    int VDIN_ClosePort();
    int VDIN_StartDec(tvin_parm_s *vdinParam);
    int VDIN_StopDec(void);
    int VDIN_GetSignalEventInfo(vdin_event_info_s *SignalEventInfo);
    int VDIN_GetSignalInfo(tvin_info_s *SignalInfo);
    int VDIN_GetAllmInfo(tvin_latency_s *AllmInfo);
    int VDIN_AddPath(const char *videopath );
    int VDIN_RemovePath(tv_path_type_t pathtype);
    int VDIN_SetVdinParam(tvin_parm_s *vdinParam);
    int VDIN_GetVdinParam(tvin_parm_s *vdinParam);
    int VDIN_SetColorRangeMode(tvin_color_range_t range_mode);
    int VDIN_GetColorRangeMode(void);
    //AFE
    int AFE_OpenModule(void);
    int AFE_CloseModule(void );
    int AFE_DeviceIOCtl(int request, ...);

private:
    static CTvin *mInstance;
    int mVdin0DevFd;
    int mAfeDevFd;
    bool mDecoderStarted;
    int mSourceInputToPortMap[SOURCE_MAX];
    tvin_parm_t mTvinParam;
};
#endif
