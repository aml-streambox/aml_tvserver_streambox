/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#ifndef _CAM_VIDEO_H
#define _CAM_VIDEO_H

#ifdef STREAM_BOX
#include "amvideo.h"
#else
#include <linux/amlogic/amvideo.h>
#endif

#define AM_VIDEO_PATH           "/dev/amvideo"
#define PATH_FRAME_COUNT_54     "/sys/module/aml_media/parameters/new_frame_count"
// ***************************************************************************
// ********************* enum type definition ********************************
// ***************************************************************************
typedef enum video_layer_status_e {
    VIDEO_LAYER_STATUS_ENABLE,
    VIDEO_LAYER_STATUS_DISABLE,
    VIDEO_LAYER_STATUS_ENABLE_AND_CLEAN,
    VIDEO_LAYER_STATUS_MAX,
} videolayer_status_t;

typedef enum video_global_output_mode_e {
    VIDEO_GLOBAL_OUTPUT_MODE_DISABLE,
    VIDEO_GLOBAL_OUTPUT_MODE_ENABLE,
    VIDEO_GLOBAL_OUTPUT_MODE_MAX,
} video_global_output_mode_t;

// ***************************************************************************
// ********************* struct type definition ******************************
// ***************************************************************************


// ***************************************************************************
// ********************* IOCTL command definition ****************************
// ***************************************************************************

class CAmVideo {
public:
    CAmVideo();
    ~CAmVideo();
    int SetVideoLayerStatus(int status);
    int GetVideoLayerStatus(int *status);
    int SetVideoGlobalOutputMode(int mode);
    int GetVideoGlobalOutputMode(int *mode);
    int GetVideoFrameCount();
private:
    int AmVideoOpenMoudle(void);
    int AmVideoCloseMoudle(void);
    int AmVideoDeviceIOCtl(int request, ...);
    int mAmVideoFd;
};
#endif
