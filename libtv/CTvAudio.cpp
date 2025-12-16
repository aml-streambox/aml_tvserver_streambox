/*
 * Copyright (c) 2014 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description: c++ file
 */
#define LOG_MODULE_TAG "TV"
#define LOG_CLASS_TAG "CTvAudio"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "CTvLog.h"
#include "CTvAudio.h"
#include "audio_if.h"

CTvAudio *CTvAudio::mInstance;

CTvAudio::CTvAudio()
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY\n", __FUNCTION__);
    fflush(stdout);
#endif
    iSInit = true;
    patch_handle = 0;  // Initialize to 0 to ensure safe check in release_audio_patch()
    int ret;
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call audio_hw_load_interface()\n", __FUNCTION__);
    fflush(stdout);
#endif
    ret = audio_hw_load_interface(&device);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from audio_hw_load_interface(), ret=%d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    if (ret) {
        LOGE("%s %d error:%d\n", __func__, __LINE__, ret);
        iSInit = false;
    }
    LOGI("hw version: %x\n", device->common.version);
    LOGI("hal api version: %x\n", device->common.module->hal_api_version);
    LOGI("module id: %s\n", device->common.module->id);
    LOGI("module name: %s\n", device->common.module->name);

    if (device->get_supported_devices) {
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: About to call get_supported_devices()\n", __FUNCTION__);
        fflush(stdout);
#endif
        uint32_t support_dev = 0;
        support_dev = device->get_supported_devices(device);
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: Returned from get_supported_devices(), support_dev=0x%x\n", __FUNCTION__, support_dev);
        fflush(stdout);
#endif
        LOGI("supported device: %x\n", support_dev);
    }

#ifdef STREAM_BOX_LEGACY
    // Skip init_check() in legacy mode as old HAL may hang here
    // Audio operations will still work if HAL is functional
    LOGD("Legacy mode: skipping init_check() to avoid potential hang\n");
    // Assume device is initialized - if not, operations will fail gracefully
#else
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call device->init_check()\n", __FUNCTION__);
    fflush(stdout);
#endif
    int inited = device->init_check(device);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from device->init_check(), inited=%d\n", __FUNCTION__, inited);
    fflush(stdout);
#endif
    if (inited) {
        LOGE("device not inited, quit\n");
        iSInit = false;
    }
#endif // STREAM_BOX_LEGACY
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT, iSInit=%d\n", __FUNCTION__, iSInit);
    fflush(stdout);
#endif
}

CTvAudio::~CTvAudio()
{
    if (iSInit) {
        audio_hw_unload_interface(device);
        iSInit = false;
    }
}
CTvAudio *CTvAudio::getInstance()
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, mInstance=%p\n", __FUNCTION__, mInstance);
    fflush(stdout);
#endif
    if (NULL == mInstance) {
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: mInstance is NULL, about to create new CTvAudio()\n", __FUNCTION__);
        fflush(stdout);
#endif
        mInstance = new CTvAudio();
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: Created new CTvAudio(), mInstance=%p\n", __FUNCTION__, mInstance);
        fflush(stdout);
#endif
    } else {
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: Using existing instance, mInstance=%p\n", __FUNCTION__, mInstance);
        fflush(stdout);
#endif
    }
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT, returning %p\n", __FUNCTION__, mInstance);
    fflush(stdout);
#endif
    return mInstance;
}

void CTvAudio::create_audio_patch(int device_type)
{
    int ret;
    if (iSInit) {
        /* create the audio patch*/
        memset(&source, 0 , sizeof(struct audio_port_config));
        source.id = 1;
        source.role = AUDIO_PORT_ROLE_SOURCE;
        source.type = AUDIO_PORT_TYPE_DEVICE;
        source.ext.device.type = device_type;

        memset(&sink, 0 , sizeof(struct audio_port_config));
        sink.id = 2;
        sink.role = AUDIO_PORT_ROLE_SINK;
        sink.type = AUDIO_PORT_TYPE_DEVICE;
        sink.ext.device.type = AUDIO_DEVICE_OUT_SPEAKER;

        LOGD("create mix --> speaker patch...\n");
        ret = device->create_audio_patch(device, 1, &source, 1, &sink, &patch_handle);
        if (ret) {
            LOGE("fail ret:%d\n",ret);
        } else {
            LOGD("success\n");
        }
    }
}

void CTvAudio::release_audio_patch()
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, patch_handle=%d, iSInit=%d\n", __FUNCTION__, patch_handle, iSInit);
    fflush(stdout);
#endif
    if ((patch_handle) && (iSInit)) {
        int ret;
        LOGD("destroy patch...\n");
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: About to call device->release_audio_patch()\n", __FUNCTION__);
        fflush(stdout);
#endif
        ret = device->release_audio_patch(device, patch_handle);
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: Returned from device->release_audio_patch(), ret=%d\n", __FUNCTION__, ret);
        fflush(stdout);
#endif
        if (ret) {
            LOGE("fail ret:%d\n",ret);
        } else {
            LOGD("success\n");
        }
    }
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT\n", __FUNCTION__);
    fflush(stdout);
#endif
}

#ifndef STREAM_BOX_LEGACY
void CTvAudio::set_audio_av_mute(bool mute)
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, mute=%d, iSInit=%d\n", __FUNCTION__, mute, iSInit);
    fflush(stdout);
#endif
    if (iSInit) {
        int ret;
        char parm[64] = {0};
        if (mute) {
            sprintf(parm, "parental_control_av_mute=%s","true");
        } else {
            sprintf(parm, "parental_control_av_mute=%s","false");
        }
        LOGD("%s:set %s\n", __FUNCTION__, parm);
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: About to call device->set_parameters()\n", __FUNCTION__);
        fflush(stdout);
#endif
        ret = device->set_parameters(device, parm);
#ifdef STREAM_BOX_TRACE
        printf("[TRACE] %s: Returned from device->set_parameters(), ret=%d\n", __FUNCTION__, ret);
        fflush(stdout);
#endif
        if (ret) {
            LOGE("%s:fail ret:%d\n",__FUNCTION__,ret);
        } else {
            LOGD("%s:success\n",__FUNCTION__);
        }
    }
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT\n", __FUNCTION__);
    fflush(stdout);
#endif
}
#endif // STREAM_BOX_LEGACY

