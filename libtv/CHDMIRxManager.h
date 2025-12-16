/*
 * Copyright (c) 2014 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description: header file
 */

#ifndef _C_HDMI_RX_MANAGER_H_
#define _C_HDMI_RX_MANAGER_H_

#ifdef STREAM_BOX
#include "hdmi_rx.h"
#else
#include <linux/amlogic/hdmi_rx.h>
#endif

#define CS_HDMIRX_DEV_PATH                "/dev/hdmirx0"
#define HDMI_CEC_PORT_SEQUENCE            "/sys/class/cec/port_seq"
#define HDMI_CEC_PORT_MAP                 "/sys/module/aml_media/parameters/port_map"//for kernel 5.4 & later
#define HDMI_EDID_DEV_PATH                "/sys/class/hdmirx/hdmirx0/edid"
#define HDMI_EDID_VERSION_DEV_PATH        "/sys/class/hdmirx/hdmirx0/edid_select"
#define HDMI_EDID_DATA_DEV_PATH           "/sys/class/hdmirx/hdmirx0/edid_with_port"
#define HDMI_SET_ALLM_PARAM               "/sys/class/hdmirx/hdmirx0/allm_func_ctrl"
#define HDMI_VRR_ENABLED                  "/sys/class/hdmirx/hdmirx0/vrr_func_ctrl"

#define REAL_EDID_DATA_SIZE        (256)

#define  HDMI_UBOOT_EDID_VERSION          "hdmi.edid.version"
#define  HDMI_UBOOT_EDID_FEATURE          "hdmi.edid.feature"

typedef enum tv_hdmi_hdcp_version_e {
    HDMI_HDCP_VER_14 = 0,
    HDMI_HDCP_VER_22 ,
} tv_hdmi_hdcp_version_t;

typedef enum tv_hdmi_edid_version_e {
    HDMI_EDID_VER_14 = 0,
    HDMI_EDID_VER_20,
    HDMI_EDID_VER_AUTO,
    HDMI_EDID_VER_MAX,
} tv_hdmi_edid_version_t;

typedef enum tv_hdmi_hdcpkey_enable_e {
    hdcpkey_enable = 0,
    hdcpkey_disable ,
} tv_hdmi_hdcpkey_enable_t;

class CHDMIRxManager {
public:
    CHDMIRxManager();
    ~CHDMIRxManager();
    int HDMIRxOpenMoudle(void);
    int HDMIRxCloseMoudle(void);
    int HDMIRxDeviceIOCtl(int request, ...);
    int HdmiRxEdidDataSwitch(int edidCount, char *data);
    int HdmiRxEdidVerSwitch(int verValue);
    int HdmiRxHdcpVerSwitch(tv_hdmi_hdcp_version_t version);
    int HdmiRxHdcpOnOff(tv_hdmi_hdcpkey_enable_t flag);
    int GetHdmiHdcpKeyKsvInfo(struct _hdcp_ksv *msg);
    int CalHdmiPortCecPhysicAddr(void);
    int SetHdmiPortCecPhysicAddr(void);
    int UpdataEdidDataWithPort(int port, unsigned char *dataBuf);
    int HdmiEnableSPDFifo(bool enable);
    int HdmiRxGetSPDInfoframe(struct spd_infoframe_st* spd);
    void SetHDMIFeatureInit(int allmEnable, int VrrEnable);
    int SetAllmEnabled(int enable);
    int GetAllmEnabled();
    int SetVrrEnabled(int enable);
    int GetVrrEnabled();
private:
    int mHdmiRxDeviceId;
};

#endif/*_C_HDMI_RX_MANAGER_H_*/
