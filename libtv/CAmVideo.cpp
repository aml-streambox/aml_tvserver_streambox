/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#define LOG_MODULE_TAG "TV"
#define LOG_CLASS_TAG "CAmVideo"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "tvutils.h"

#include "CAmVideo.h"
#include "CTvLog.h"

CAmVideo::CAmVideo()
{
    mAmVideoFd = AmVideoOpenMoudle();
}

CAmVideo::~CAmVideo()
{
    AmVideoCloseMoudle();
}

int CAmVideo::AmVideoOpenMoudle(void)
{
    int fd = open(AM_VIDEO_PATH, O_RDWR);
    if (fd < 0) {
        LOGE("Open %s error(%s)!\n", AM_VIDEO_PATH, strerror(errno));
        return -1;
    }

    return fd;
}

int CAmVideo::AmVideoCloseMoudle(void)
{
    if (mAmVideoFd != -1) {
        close (mAmVideoFd);
        mAmVideoFd = -1;
    }

    return 0;
}

int CAmVideo::AmVideoDeviceIOCtl(int request, ...)
{
    int ret = -1;
    va_list ap;
    void *arg;
    va_start (ap, request);
    arg = va_arg (ap, void *);
    va_end (ap);

    ret = ioctl(mAmVideoFd, request, arg);

    return ret;
}

int CAmVideo::SetVideoLayerStatus(int status)
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, status=%d\n", __FUNCTION__, status);
    fflush(stdout);
#endif
    int ret = -1;
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call AmVideoDeviceIOCtl(AMSTREAM_IOC_SET_VIDEO_DISABLE)\n", __FUNCTION__);
    fflush(stdout);
#endif
    ret = AmVideoDeviceIOCtl(AMSTREAM_IOC_SET_VIDEO_DISABLE, &status);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from AmVideoDeviceIOCtl(), ret=%d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    if (ret < 0) {
        LOGE("%s:[%d] failed.\n", __FUNCTION__, status);
    } else {
        LOGD("%s:[%d] success.\n", __FUNCTION__, status);
    }

    return ret;
}

int CAmVideo::GetVideoLayerStatus(int *status)
{
    int ret = -1;
    if (status == NULL) {
        LOGE("%s: param is NULL.\n", __FUNCTION__);
    } else {
        int tempVal = 0;
        ret = AmVideoDeviceIOCtl(AMSTREAM_IOC_GET_VIDEO_DISABLE, &tempVal);
        if (ret < 0) {
            LOGE("%s failed.\n", __FUNCTION__);
        } else {
            LOGD("%s success, status is %d.\n", __FUNCTION__, tempVal);
        }
        *status = tempVal;
    }
    return ret;
}

int CAmVideo::SetVideoGlobalOutputMode(int mode)
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, mode=%d\n", __FUNCTION__, mode);
    fflush(stdout);
#endif
    int ret = -1;
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call AmVideoDeviceIOCtl(AMSTREAM_IOC_GLOBAL_SET_VIDEO_OUTPUT)\n", __FUNCTION__);
    fflush(stdout);
#endif
    ret = AmVideoDeviceIOCtl(AMSTREAM_IOC_GLOBAL_SET_VIDEO_OUTPUT, &mode);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from AmVideoDeviceIOCtl(), ret=%d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    if (ret < 0) {
        LOGE("%s failed.\n", __FUNCTION__);
    } else {
        LOGD("%s success.\n", __FUNCTION__);
    }

    return ret;
}

int CAmVideo::GetVideoGlobalOutputMode(int *mode)
{
    int ret = -1;
    if (mode == NULL) {
        LOGE("%s: param is NULL.\n", __FUNCTION__);
    } else {
        int tempVal = 0;
        ret = AmVideoDeviceIOCtl(AMSTREAM_IOC_GLOBAL_GET_VIDEO_OUTPUT, &tempVal);
        if (ret < 0) {
            LOGE("%s failed.\n", __FUNCTION__);
        } else {
            LOGD("%s success, status is %d.\n", __FUNCTION__, tempVal);
        }
        *mode = tempVal;
    }
    return ret;
}

int  CAmVideo::GetVideoFrameCount()
{
    char buf[32] = {0};
    tvReadSysfs(PATH_FRAME_COUNT_54, buf);
    return atoi(buf);

}

