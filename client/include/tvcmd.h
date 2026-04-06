/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#ifndef AMLOGIC_TVCMD_H
#define AMLOGIC_TVCMD_H

enum tvcmd_e {
    //Tv CMD(0~99)
    TV_CONTROL_CMD_START          = 0,
    TV_CONTROL_OPEN_TV            = 1,
    TV_CONTROL_CLOSE_TV           = 2,
    TV_CONTROL_START_TV           = 3,
    TV_CONTROL_STOP_TV            = 4,
    TV_CONTROL_GET_FRAME_HEIGHT   = 5,
    TV_CONTROL_GET_FRAME_WIDTH    = 6,
    TV_CONTROL_GET_COLOR_DEPTH    = 7,
    TV_CONTROL_GET_FRAME_RATE     = 8,
    TV_CONTROL_GET_LINE_SCAN_MODE = 9,
    TV_CONTROL_GET_CONNECT_STATUS = 10,
    TV_CONTROL_SET_BOOST_ON       = 11,
    TV_CONTROL_GET_ALLM_INFO      = 12,
    TV_CONTROL_SET_HEADLESS_MODE  = 13,
    TV_CONTROL_VDIN_WORK_MODE_SET = 98,
    TV_CONTROL_CMD_MAX            = 99,

    //hdmi CMD(101~199)
    HDMI_CONTROL_CMD_START        = 100,
    HDMI_EDID_VER_SET             = 101,
    HDMI_EDID_VER_GET             = 102,
    HDMI_EDID_DATA_SET            = 103,
    HDMI_EDID_DATA_GET            = 104,
    HDMI_SET_COLOR_FORMAT         = 105,
    HDMI_GET_COLOR_FORMAT         = 106,
    HDMI_SET_COLOR_RANGE          = 107,
    HDMI_GET_COLOR_RANGE          = 108,
    HDMI_SET_COLOR_DEPTH          = 109,
    HDMI_GET_COLOR_DEPTH          = 110,
    HDMI_SET_ASPECT_RATIO         = 111,
    HDMI_GET_ASPECT_RATIO         = 112,
    HDMI_SET_ALLM_ENABLED         = 113,
    HDMI_GET_ALLM_ENABLED         = 114,
    HDMI_SET_VRR_ENABLED          = 115,
    HDMI_GET_VRR_ENABLED          = 116,
    HDMI_GET_VRR_MODE             = 117,
    HDMI_SET_GAME_MODE            = 118,
    HDMI_SET_PC_MODE              = 119,
    HDMI_SPD_INFO_GET             = 198,
    HDMI_CONTROL_CMD_MAX          = 199,
};

#endif  //AMLOGIC_TVCMD_H
