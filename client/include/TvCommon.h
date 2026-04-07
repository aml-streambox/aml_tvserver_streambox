/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#ifndef __TV_COMMON_H__
#define __TV_COMMON_H__

#ifdef STREAM_BOX
#include "tvin.h"
#else
#include <linux/amlogic/tvin.h>
#endif

#ifndef __TV_SOURCE_INPUT__
#define __TV_SOURCE_INPUT__
typedef enum tv_source_input_e {
    SOURCE_INVALID = -1,
    SOURCE_TV = 0,
    SOURCE_AV1,
    SOURCE_AV2,
    SOURCE_YPBPR1,
    SOURCE_YPBPR2,
    SOURCE_HDMI1,
    SOURCE_HDMI2,
    SOURCE_HDMI3,
    SOURCE_HDMI4,
    SOURCE_VGA,
    SOURCE_MPEG,
    SOURCE_SVIDEO,
    SOURCE_IPTV,
    SOURCE_DUMMY,
    SOURCE_SPDIF,
    SOURCE_MAX,
} tv_source_input_t;
#endif

#ifndef __TV_SOURCE_INPUT_TYPE__
#define __TV_SOURCE_INPUT_TYPE__
typedef enum tv_source_input_type_e {
    SOURCE_TYPE_TV,
    SOURCE_TYPE_AV,
    SOURCE_TYPE_COMPONENT,
    SOURCE_TYPE_HDMI,
    SOURCE_TYPE_VGA,
    SOURCE_TYPE_MPEG,
    SOURCE_TYPE_SVIDEO,
    SOURCE_TYPE_IPTV,
    SOURCE_TYPE_SPDIF,
    SOURCE_TYPE_MAX,
} tv_source_input_type_t;
#endif

typedef enum tvin_port_e tvin_port_t;
typedef enum tvin_aspect_ratio_e tvin_aspect_ratio_t;
typedef enum tvin_color_fmt_e tvin_color_fmt_t;
typedef enum tvin_sig_fmt_e tvin_sig_fmt_t;
typedef enum tvin_trans_fmt tvin_trans_fmt_t;
typedef enum tvin_sig_status_e tvin_sig_status_t;
typedef enum tvin_cn_type_e tvin_cn_type_t;
typedef enum vdin_vrr_mode_e vdin_vrr_mode_t;

#ifndef __TVIN_WORK_MODE__
#define __TVIN_WORK_MODE__
typedef enum vdin_work_mode_e {
    VDIN_WORK_MODE_VFM = 0,
    VDIN_WORK_MODE_V4L2,
    VDIN_WORK_MODE_MAX,
} vdin_work_mode_t;
#endif

#ifndef __TVIN_COLOR_RANGE__
#define __TVIN_COLOR_RANGE__
typedef enum tvin_color_range_e {
    TVIN_COLOR_RANGE_AUTO = 0,
    TVIN_COLOR_RANGE_FULL,
    TVIN_COLOR_RANGE_LIMIT,
    TVIN_COLOR_RANGE_MAX,
} tvin_color_range_t;
#endif

#ifndef __TVIN_LINE_SCAN_MODE__
#define __TVIN_LINE_SCAN_MODE__
typedef enum tvin_line_scan_mode_e {
	TVIN_LINE_SCAN_MODE_NULL = 0,
	TVIN_LINE_SCAN_MODE_PROGRESSIVE,
	TVIN_LINE_SCAN_MODE_INTERLACED,
	TVIN_LINE_SCAN_MODE_MAX,
} tvin_line_scan_mode_t;
#endif

typedef enum tvin_cn_type_e tvin_cn_type_t;

// REAL_EDID_DATA_SIZE defined in libtv/CHDMIRxManager.h
#define REAL_EDID_DATA_SIZE        (256)
#ifdef STREAM_BOX
#define PATCHED_EDID_MAX_SIZE      (384)
#endif

#endif
