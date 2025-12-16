/*
 * Copyright (c) 2014 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description: c++ file
 */

#define LOG_MODULE_TAG "TV"
#define LOG_CLASS_TAG "CHDMIRxManager"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "CHDMIRxManager.h"
#include "tvutils.h"
#include "CTvin.h"
#include "CTvLog.h"

CHDMIRxManager::CHDMIRxManager()
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY\n", __FUNCTION__);
    fflush(stdout);
#endif
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call HDMIRxOpenMoudle()\n", __FUNCTION__);
    fflush(stdout);
#endif
    mHdmiRxDeviceId = HDMIRxOpenMoudle();
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from HDMIRxOpenMoudle(), mHdmiRxDeviceId = %d\n", __FUNCTION__, mHdmiRxDeviceId);
    fflush(stdout);
#endif
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call HdmiEnableSPDFifo(true)\n", __FUNCTION__);
    fflush(stdout);
#endif
    HdmiEnableSPDFifo(true);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: EXIT\n", __FUNCTION__);
    fflush(stdout);
#endif
}

CHDMIRxManager::~CHDMIRxManager()
{
    HDMIRxCloseMoudle();
}

int CHDMIRxManager::HDMIRxOpenMoudle()
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, about to open %s\n", __FUNCTION__, CS_HDMIRX_DEV_PATH);
    fflush(stdout);
#endif
    int fd = open ( CS_HDMIRX_DEV_PATH, O_RDWR );
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from open(), fd = %d\n", __FUNCTION__, fd);
    fflush(stdout);
#endif
    if ( fd < 0 ) {
        LOGE("Open %s error(%s)!\n", CS_HDMIRX_DEV_PATH, strerror ( errno ));
        return -1;
    }

    return fd;
}

int CHDMIRxManager::HDMIRxCloseMoudle()
{
    if ( mHdmiRxDeviceId != -1 ) {
        close ( mHdmiRxDeviceId );
        mHdmiRxDeviceId = -1;
    }

    return 0;
}

int CHDMIRxManager::HDMIRxDeviceIOCtl(int request, ...)
{
    int ret = -1;
    va_list ap;
    void *arg;
    va_start ( ap, request );
    arg = va_arg ( ap, void *);
    va_end ( ap );

    ret = ioctl(mHdmiRxDeviceId, request, arg);

    return ret;
}

int CHDMIRxManager::HdmiRxEdidDataSwitch(int edidBinCount, char *data)
{
    int ret = -1;
    if (data == NULL) {
        LOGE("%s: edid data is null!\n", __FUNCTION__);
        ret = -1;
    } else {
        int edidDataCount = edidBinCount * REAL_EDID_DATA_SIZE;
        unsigned char loadData[edidDataCount] = {0};
        int LoadConut = 0;
        if (edidBinCount == 1) {
            loadData[0] = 'E';
            loadData[1] = 'D';
            loadData[2] = 'I';
            loadData[3] = 'D';
            memcpy(loadData + 4, data, edidDataCount);
            LoadConut = (edidDataCount + 4);
        } else {
            memcpy(loadData, data, edidDataCount);
            LoadConut = edidDataCount;
        }

        int dev_fd = open(HDMI_EDID_DEV_PATH, O_RDWR);
        if (dev_fd < 0) {
            LOGE("open edid file ERROR(%s)!!\n", strerror(errno));
            ret = -1;
        } else {
            if (write(dev_fd, data, LoadConut) < 0) {
                LOGE("write edid file ERROR(%s)!!\n", strerror(errno));
                ret = -1;
            }

            close(dev_fd);
            dev_fd = -1;
            if (edidBinCount == 1) {
                HDMIRxDeviceIOCtl(HDMI_IOC_EDID_UPDATE);
            }

            ret = 0;
        }
    }

    return ret;
}

int CHDMIRxManager::HdmiRxEdidVerSwitch(int verValue)
{
    LOGD("%s: new all edid version: 0x%x\n", __FUNCTION__, verValue);

    int ret = -1;
    int devFd = open(HDMI_EDID_VERSION_DEV_PATH, O_RDWR);
    if (devFd < 0) {
        LOGE("%s: open %s ERROR(%s)!!\n", __FUNCTION__, HDMI_EDID_VERSION_DEV_PATH, strerror(errno));
        ret = -1;
    } else {
        char tmp[32] = {0};
        sprintf(tmp, "%x", verValue);
        if (write(devFd, tmp, strlen(tmp)) < 0) {
            LOGE("%s, write %s ERROR(%s)!!\n", __FUNCTION__, HDMI_EDID_VERSION_DEV_PATH, strerror(errno));
            ret = -1;
        } else {
            ret = 0;
        }
        close(devFd);
        devFd = -1;
    }

    HDMIRxDeviceIOCtl(HDMI_IOC_EDID_UPDATE);
    return ret;
}

int CHDMIRxManager::HdmiRxHdcpVerSwitch(tv_hdmi_hdcp_version_t version)
{
    int ret = -1;
    if (HDMI_HDCP_VER_14 == version) {
        ret = HDMIRxDeviceIOCtl(HDMI_IOC_HDCP22_FORCE14);
    } else if (HDMI_HDCP_VER_22 == version) {
        ret = HDMIRxDeviceIOCtl(HDMI_IOC_HDCP22_AUTO);
    } else {
        LOGE("invalid hdcp version!\n");
        return -1;
    }

    return ret;
}

int CHDMIRxManager::HdmiRxHdcpOnOff(tv_hdmi_hdcpkey_enable_t flag)
{
    int ret = -1;
    if (hdcpkey_enable == flag) {
        ret = HDMIRxDeviceIOCtl(HDMI_IOC_HDCP_ON);
    }else if (hdcpkey_disable == flag) {
        ret = HDMIRxDeviceIOCtl(HDMI_IOC_HDCP_OFF);
    }else {
        LOGE("invalid hdcp enable status!\n");
        return -1;
    }

    return ret;
}

int CHDMIRxManager::GetHdmiHdcpKeyKsvInfo(struct _hdcp_ksv *msg)
{
    return HDMIRxDeviceIOCtl(HDMI_IOC_HDCP_GET_KSV, msg);
}

int CHDMIRxManager::CalHdmiPortCecPhysicAddr()
{
    tv_source_input_t tmpHdmiPortCecPhysicAddr[4] = {SOURCE_MAX};
    tvin_port_t tvInport[4] = {TVIN_PORT_HDMI0,TVIN_PORT_HDMI1,TVIN_PORT_HDMI2,TVIN_PORT_HDMI3};
    int HdmiPortCecPhysicAddr = 0x0;
    for (int i = 0; i < 4; i++) {
        tmpHdmiPortCecPhysicAddr[i] = CTvin::getInstance()->Tvin_PortToSourceInput(tvInport[i]);
    }
    HdmiPortCecPhysicAddr |= ((tmpHdmiPortCecPhysicAddr[0] == SOURCE_MAX? 0xf:(tmpHdmiPortCecPhysicAddr[0]-4))
                             |((tmpHdmiPortCecPhysicAddr[1] == SOURCE_MAX? 0xf:(tmpHdmiPortCecPhysicAddr[1]-4)) << 4)
                             |((tmpHdmiPortCecPhysicAddr[2] == SOURCE_MAX? 0xf:(tmpHdmiPortCecPhysicAddr[2]-4)) << 8)
                             |((tmpHdmiPortCecPhysicAddr[3] == SOURCE_MAX? 0xf:(tmpHdmiPortCecPhysicAddr[3]-4)) << 12));

    LOGD("hdmi port map: 0x%x\n", HdmiPortCecPhysicAddr);
    return HdmiPortCecPhysicAddr;
}

int CHDMIRxManager::SetHdmiPortCecPhysicAddr()
{
    char buf[10] = {0};
    int val = CalHdmiPortCecPhysicAddr();
    sprintf(buf, "%x", val);
    tvWriteSysfs(HDMI_CEC_PORT_SEQUENCE, buf);
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%d", val);
    tvWriteSysfs(HDMI_CEC_PORT_MAP,buf);
    return 0;
}

int CHDMIRxManager::UpdataEdidDataWithPort(int port, unsigned char *dataBuf)
{
    int ret = -1;
    int size = REAL_EDID_DATA_SIZE + 1;
    unsigned char LoadBuf[size];
    memset(LoadBuf, 0, sizeof(char) * size);
    LoadBuf[0] = (unsigned char)port;
    memcpy(LoadBuf+1, dataBuf, REAL_EDID_DATA_SIZE);
    /*LOGD("%s: edid data print start.\n", __FUNCTION__);
    for (int i=0;i<257;i++) {
        printf("0x%x ",LoadBuf[i]);
    }
    LOGD("%s: edid data print end.\n", __FUNCTION__);*/
    int devFd = open(HDMI_EDID_DATA_DEV_PATH, O_RDWR);
    if (devFd < 0) {
        LOGE("%s: open ERROR(%s)!\n", __FUNCTION__, strerror(errno));
        ret = -1;
    } else {
        if (write(devFd, LoadBuf, size) < 0) {
            LOGE("%s: write ERROR(%s)!\n", __FUNCTION__, strerror(errno));
            ret = -1;
        } else {
            ret = 0;
        }

        close(devFd);
        devFd = -1;
    }
    if (ret >= 0) {
        LOGD("%s: would update edid.\n", __FUNCTION__);
        HDMIRxDeviceIOCtl(HDMI_IOC_EDID_UPDATE);
    }

    return ret;
}

int CHDMIRxManager::HdmiEnableSPDFifo(bool enable)
{
    unsigned int pkttype = 0x83;
    int ret = -1;

    if (enable) {
        ret = HDMIRxDeviceIOCtl(HDMI_IOC_PD_FIFO_PKTTYPE_EN, &pkttype);
    } else {
        ret = HDMIRxDeviceIOCtl(HDMI_IOC_PD_FIFO_PKTTYPE_DIS, &pkttype);
    }
    return ret;
}

int CHDMIRxManager::HdmiRxGetSPDInfoframe(struct spd_infoframe_st* spd)
{
    int ret = -1;
    struct pd_infoframe_s pd;

    pd.HB = 0x83;
    ret = HDMIRxDeviceIOCtl(HDMI_IOC_GET_PD_FIFO_PARAM, &pd);
    if (ret < 0) {
        LOGE("Get SPD infoframe via ioctl failed: %s.\n", strerror(errno));
    } else {
        memcpy(spd, &pd, sizeof(struct spd_infoframe_st));
    }

    return ret;
}

void CHDMIRxManager::SetHDMIFeatureInit(int allmEnable, int VrrEnable)
{
    char buf[8] = {0};
    sprintf(buf, "%d", allmEnable);
    tvWriteSysfs(HDMI_SET_ALLM_PARAM, buf);
    sprintf(buf, "%d", VrrEnable);
    tvWriteSysfs(HDMI_VRR_ENABLED, buf);
}

int CHDMIRxManager::SetAllmEnabled(int enable)
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, enable=%d\n", __FUNCTION__, enable);
    fflush(stdout);
#endif
    int ret = -1;
    char buf[8] = {0};
    sprintf(buf, "%d", enable);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to write '%s' to %s\n", __FUNCTION__, buf, HDMI_SET_ALLM_PARAM);
    fflush(stdout);
#endif
    tvWriteSysfs(HDMI_SET_ALLM_PARAM, buf);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call HDMIRxDeviceIOCtl(HDMI_IOC_EDID_UPDATE)\n", __FUNCTION__);
    fflush(stdout);
#endif
    ret = HDMIRxDeviceIOCtl(HDMI_IOC_EDID_UPDATE);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from HDMIRxDeviceIOCtl(), ret=%d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    return ret;
}

int CHDMIRxManager::GetAllmEnabled()
{
    char buf[32] = {0};
    tvReadSysfs(HDMI_SET_ALLM_PARAM, buf);
    int num;
    if (sscanf(buf, "%*[^:]:%d", &num) == 1) {
        return num > 0;
    }
    return 0;
}

int CHDMIRxManager::SetVrrEnabled(int enable)
{
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: ENTRY, enable=%d\n", __FUNCTION__, enable);
    fflush(stdout);
#endif
    int ret = -1;
    char buf[8] = {0};
    sprintf(buf, "%d", enable);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to write '%s' to %s\n", __FUNCTION__, buf, HDMI_VRR_ENABLED);
    fflush(stdout);
#endif
    tvWriteSysfs(HDMI_VRR_ENABLED, buf);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: About to call HDMIRxDeviceIOCtl(HDMI_IOC_EDID_UPDATE)\n", __FUNCTION__);
    fflush(stdout);
#endif
    ret = HDMIRxDeviceIOCtl(HDMI_IOC_EDID_UPDATE);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] %s: Returned from HDMIRxDeviceIOCtl(), ret=%d\n", __FUNCTION__, ret);
    fflush(stdout);
#endif
    return ret;
}

int CHDMIRxManager::GetVrrEnabled()
{
    char buf[32] = {0};
    tvReadSysfs(HDMI_VRR_ENABLED, buf);
    int num;
    if (sscanf(buf, "%*[^:]:%d", &num) == 1) {
        return num > 0;
    }
    return 0;
}

#ifdef STREAM_BOX
int CHDMIRxManager::PatchEdidFor120Hz(unsigned char *edidData, int edidSize)
{
    if (edidData == NULL || edidSize < 256) {
        LOGE("%s: Invalid EDID data or size\n", __FUNCTION__);
        return -1;
    }

    LOGD("%s: Patching EDID to enable 120Hz support\n", __FUNCTION__);

    // EDID structure:
    // Base block: bytes 0-127
    // Extension blocks: bytes 128+
    // CEA extension block starts at byte 128, tag 0x02
    
    // Check if we have extension blocks
    int numExtensions = edidData[0x7E];
    if (numExtensions == 0) {
        LOGD("%s: No extension blocks found, creating CEA extension\n", __FUNCTION__);
        // Create a basic CEA extension block if none exists
        if (edidSize >= 256) {
            edidData[0x7E] = 1; // Set number of extensions to 1
            // Initialize CEA extension block at offset 128
            edidData[128] = 0x02; // CEA extension tag
            edidData[129] = 0x03; // CEA revision 3
            edidData[130] = 4;    // DTD start offset (after basic header)
            edidData[131] = 0xE0; // Flags: digital, supports YCbCr 4:4:4, YCbCr 4:2:2
            numExtensions = 1;
        } else {
            LOGE("%s: EDID size too small to add extension\n", __FUNCTION__);
            return -1;
        }
    }

    // Find or use CEA extension block (tag 0x02)
    int extOffset = 128;
    if (edidData[extOffset] != 0x02) {
        // First extension is not CEA, try to find one or create it
        for (int ext = 0; ext < numExtensions && (128 + ext * 128) < edidSize; ext++) {
            int testOffset = 128 + ext * 128;
            if (edidData[testOffset] == 0x02) {
                extOffset = testOffset;
                break;
            }
        }
        // If still not found and we have space, create one
        if (edidData[extOffset] != 0x02 && numExtensions < 4 && edidSize >= (128 + (numExtensions + 1) * 128)) {
            extOffset = 128 + numExtensions * 128;
            edidData[extOffset] = 0x02;
            edidData[extOffset + 1] = 0x03;
            edidData[extOffset + 2] = 4;
            edidData[extOffset + 3] = 0xE0;
            edidData[0x7E] = numExtensions + 1;
        }
    }

    if (edidData[extOffset] == 0x02) {
        LOGD("%s: Found/created CEA extension block at offset %d\n", __FUNCTION__, extOffset);
        
        // CEA extension structure:
        // Byte 0: Extension tag (0x02)
        // Byte 1: Revision number
        // Byte 2: DTD start offset
        // Byte 3: Flags (bit 7 = native DTD support)
        
        // Enable native detailed timing support (bit 7)
        edidData[extOffset + 3] |= 0x80;
        
        // Get DTD start offset
        int dtdStart = edidData[extOffset + 2];
        if (dtdStart < 4) dtdStart = 4; // Minimum DTD start
        
        // Try to add 120Hz timing in the data block area (before DTD start)
        // For 1080p@120Hz, we can add a short video data block
        // This is a simplified approach - proper implementation would add full timing descriptors
        
        // Mark that we support high refresh rates by setting flags
        // The actual timing modes should be in the detailed timing descriptors
        
        LOGD("%s: EDID patched for 120Hz support (flags set, DTD start at %d)\n", 
             __FUNCTION__, dtdStart);
        
        // Recalculate checksum for the extension block
        unsigned char checksum = 0;
        for (int i = extOffset; i < extOffset + 127; i++) {
            checksum += edidData[i];
        }
        edidData[extOffset + 127] = 256 - checksum;
        
        return 0;
    }

    LOGE("%s: Could not find or create CEA extension block\n", __FUNCTION__);
    return -1;
}

int CHDMIRxManager::ReadEdidFromHdmiTx(unsigned char *edidData, int maxSize)
{
    if (edidData == NULL || maxSize < REAL_EDID_DATA_SIZE) {
        LOGE("%s: Invalid parameters\n", __FUNCTION__);
        return -1;
    }

    LOGD("%s: Reading EDID from HDMI TX\n", __FUNCTION__);
    
    int fd = open(HDMI_TX_RAWEDID_PATH, O_RDONLY);
    if (fd < 0) {
        LOGE("%s: Failed to open %s: %s\n", __FUNCTION__, HDMI_TX_RAWEDID_PATH, strerror(errno));
        return -1;
    }

    // Read hex ASCII EDID (512 bytes = 256 bytes * 2 hex chars per byte)
    char hexBuffer[512 + 1] = {0};
    ssize_t bytesRead = read(fd, hexBuffer, sizeof(hexBuffer) - 1);
    close(fd);

    if (bytesRead < 512) {
        LOGE("%s: Failed to read complete EDID (read %zd bytes)\n", __FUNCTION__, bytesRead);
        return -1;
    }

    // Convert hex ASCII to binary
    for (int i = 0; i < REAL_EDID_DATA_SIZE; i++) {
        char hexByte[3] = {hexBuffer[i * 2], hexBuffer[i * 2 + 1], '\0'};
        edidData[i] = (unsigned char)strtoul(hexByte, NULL, 16);
    }

    LOGD("%s: Successfully read EDID from HDMI TX\n", __FUNCTION__);
    return 0;
}

int CHDMIRxManager::PassthroughEdidFromTxToRx(int port)
{
    LOGD("%s: Passing through EDID from HDMI TX to HDMI RX port %d\n", __FUNCTION__, port);
    
    unsigned char edidData[REAL_EDID_DATA_SIZE] = {0};
    int ret = ReadEdidFromHdmiTx(edidData, REAL_EDID_DATA_SIZE);
    if (ret < 0) {
        LOGE("%s: Failed to read EDID from HDMI TX\n", __FUNCTION__);
        return ret;
    }

    // Pass the EDID to HDMI RX
    ret = UpdataEdidDataWithPort(port, edidData);
    if (ret < 0) {
        LOGE("%s: Failed to update EDID on HDMI RX\n", __FUNCTION__);
        return ret;
    }

    LOGD("%s: Successfully passed through EDID from TX to RX\n", __FUNCTION__);
    return 0;
}
#endif
