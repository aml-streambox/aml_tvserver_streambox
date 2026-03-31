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
        
        // Data block collection starts at offset 4 in CEA extension
        int dataBlockOffset = extOffset + 4;
        int dataBlockEnd = extOffset + dtdStart;
        
        // Parse existing data blocks to find video data block
        int offset = dataBlockOffset;
        int videoBlockOffset = -1;
        int videoBlockLength = 0;
        bool vic63Present = false;
        
        while (offset < dataBlockEnd && offset < extOffset + 127) {
            if (offset + 1 > extOffset + 127) break;
            
            unsigned char tag = (edidData[offset] >> 5) & 0x07;
            unsigned char length = edidData[offset] & 0x1F;
            
            if (length == 0 || offset + length + 1 > extOffset + 127) {
                break;
            }
            
            // Check for video data block (tag 0x02 = VIDEO_TAG)
            if (tag == 0x02) {
                videoBlockOffset = offset;
                videoBlockLength = length;
                
                // Check if VIC 63 (1080p@120Hz) is already present
                for (int i = 1; i <= length && (offset + i) < extOffset + 127; i++) {
                    if (edidData[offset + i] == 63) {
                        vic63Present = true;
                        LOGD("%s: VIC 63 (1080p@120Hz) already present in EDID\n", __FUNCTION__);
                        break;
                    }
                }
                break; // Found video block, no need to continue
            }
            
            offset += length + 1;
        }
        
        // Add VIC 63 (1080p@120Hz) if not present
        if (!vic63Present) {
            if (videoBlockOffset >= 0) {
                // Video block exists, try to add VIC 63 to it
                int newLength = videoBlockLength + 1;
                if (videoBlockOffset + newLength + 1 <= dataBlockEnd && 
                    videoBlockOffset + newLength + 1 <= extOffset + 127) {
                    // Update length byte
                    edidData[videoBlockOffset] = (edidData[videoBlockOffset] & 0xE0) | (newLength & 0x1F);
                    // Add VIC 63
                    edidData[videoBlockOffset + videoBlockLength + 1] = 63;
                    LOGD("%s: Added VIC 63 (1080p@120Hz) to existing video data block at offset %d\n", 
                         __FUNCTION__, videoBlockOffset);
                } else {
                    LOGD("%s: Video block full (length=%d, end=%d), cannot add VIC 63\n", 
                         __FUNCTION__, videoBlockLength, dataBlockEnd);
                }
            } else {
                // No video block exists, create one
                // Check if we have space for a new video data block (tag + length + 1 VIC = 3 bytes)
                if (offset + 3 <= dataBlockEnd && offset + 3 <= extOffset + 127) {
                    // Create video data block: tag 0x02, length 1, VIC 63
                    edidData[offset] = (0x02 << 5) | 0x01; // Tag 0x02, length 1
                    edidData[offset + 1] = 63; // VIC 63 = 1080p@120Hz
                    LOGD("%s: Created new video data block with VIC 63 (1080p@120Hz) at offset %d\n", 
                         __FUNCTION__, offset);
                    
                    // Update DTD start if we added a block
                    if (offset + 3 > dtdStart) {
                        edidData[extOffset + 2] = offset + 3;
                        dtdStart = offset + 3;
                    }
                } else {
                    LOGD("%s: No space to add video data block for VIC 63 (offset=%d, end=%d)\n", 
                         __FUNCTION__, offset, dataBlockEnd);
                }
            }
        }
        
        // Add Detailed Timing Descriptors (DTDs) for new modes
        // DTD is 18 bytes each, we need space after current DTD start
        int dtdEnd = extOffset + 127; // Last byte before checksum
        int dtdSpace = dtdEnd - dtdStart;
        
        // Mode table: pixel_clock (10kHz), hActive, hBlank, hFront, hSync, 
        //             vActive, vBlank, vFront, vSync, flags, description
        struct {
            unsigned int pixelClock;
            unsigned int hActive, hBlank, hFrontPorch, hSync;
            unsigned int vActive, vBlank, vFrontPorch, vSync;
            unsigned char flags;
            const char *desc;
        } newModes[] = {
            // 2560x1440@120Hz (already existed, kept for compat)
            { 48300, 2560, 160, 48, 32, 1440, 41, 3, 5, 0x1E, "2560x1440p120" },
            // 2560x1440@60Hz
            { 24150, 2560, 160, 48, 32, 1440, 41, 3, 5, 0x1E, "2560x1440p60" },
            // 2560x1440@144Hz (reduced blanking)
            { 57104, 2560, 160, 48, 32, 1440, 18, 3, 5, 0x1E, "2560x1440p144" },
            // 1080p@144Hz
            { 35640, 1920, 280, 88, 44, 1080, 45, 4, 5, 0x1E, "1920x1080p144" },
            // 1080p@240Hz
            { 59400, 1920, 280, 88, 44, 1080, 45, 4, 5, 0x1E, "1920x1080p240" },
            // 3440x1440@60Hz (reduced blanking)
            { 31975, 3440, 160, 48, 32, 1440, 49, 3, 5, 0x1E, "3440x1440p60" },
        };
        int numModes = sizeof(newModes) / sizeof(newModes[0]);
        int addedCount = 0;
        
        for (int m = 0; m < numModes; m++) {
            if (dtdSpace < 18) {
                LOGD("%s: No more DTD space for %s\n", __FUNCTION__, newModes[m].desc);
                break;
            }
            
            // Check if this DTD already exists by matching pixel clock + resolution
            bool alreadyPresent = false;
            for (int dtdOffset = dtdStart; dtdOffset + 18 <= dtdEnd; dtdOffset += 18) {
                unsigned int pc = edidData[dtdOffset] | (edidData[dtdOffset + 1] << 8);
                unsigned int hA = ((edidData[dtdOffset + 4] >> 4) & 0xF) << 8 | edidData[dtdOffset + 2];
                unsigned int vA = ((edidData[dtdOffset + 7] >> 4) & 0xF) << 8 | edidData[dtdOffset + 5];
                
                // Match resolution and pixel clock within ±1%
                if (hA == newModes[m].hActive && vA == newModes[m].vActive &&
                    pc > 0 && abs((int)pc - (int)newModes[m].pixelClock) < (int)(newModes[m].pixelClock / 100 + 1)) {
                    alreadyPresent = true;
                    LOGD("%s: %s DTD already present\n", __FUNCTION__, newModes[m].desc);
                    break;
                }
                // Stop at empty DTD slot (pixel clock 0)
                if (pc == 0) break;
            }
            
            if (alreadyPresent) continue;
            
            // Find first available DTD slot
            int dtdOffset = dtdStart;
            bool foundSlot = false;
            
            for (; dtdOffset + 18 <= dtdEnd; dtdOffset += 18) {
                bool isEmpty = true;
                for (int i = 0; i < 2; i++) {
                    if (edidData[dtdOffset + i] != 0) {
                        isEmpty = false;
                        break;
                    }
                }
                if (isEmpty) {
                    foundSlot = true;
                    break;
                }
            }
            
            if (!foundSlot && dtdOffset + 18 <= dtdEnd) {
                foundSlot = true;
            }
            
            if (!foundSlot) {
                LOGD("%s: No slot for %s DTD (dtdStart=%d, dtdEnd=%d)\n",
                     __FUNCTION__, newModes[m].desc, dtdStart, dtdEnd);
                continue;
            }
            
            // Encode DTD per EDID spec
            unsigned int pc = newModes[m].pixelClock;
            unsigned int hA = newModes[m].hActive;
            unsigned int hB = newModes[m].hBlank;
            unsigned int hF = newModes[m].hFrontPorch;
            unsigned int hS = newModes[m].hSync;
            unsigned int vA = newModes[m].vActive;
            unsigned int vB = newModes[m].vBlank;
            unsigned int vF = newModes[m].vFrontPorch;
            unsigned int vS = newModes[m].vSync;
            
            edidData[dtdOffset + 0] = pc & 0xFF;
            edidData[dtdOffset + 1] = (pc >> 8) & 0xFF;
            edidData[dtdOffset + 2] = hA & 0xFF;
            edidData[dtdOffset + 3] = hB & 0xFF;
            edidData[dtdOffset + 4] = (((hA >> 8) & 0xF) << 4) | ((hB >> 8) & 0xF);
            edidData[dtdOffset + 5] = vA & 0xFF;
            edidData[dtdOffset + 6] = vB & 0xFF;
            edidData[dtdOffset + 7] = (((vA >> 8) & 0xF) << 4) | ((vB >> 8) & 0xF);
            edidData[dtdOffset + 8] = hF & 0xFF;
            edidData[dtdOffset + 9] = hS & 0xFF;
            edidData[dtdOffset + 10] = ((vF & 0xF) << 4) | (vS & 0xF);
            edidData[dtdOffset + 11] = (((hF >> 8) & 0x3) << 6) |
                                        (((hS >> 8) & 0x3) << 4) |
                                        (((vF >> 4) & 0x3) << 2) |
                                        ((vS >> 4) & 0x3);
            edidData[dtdOffset + 12] = 0x00; // h_image_size
            edidData[dtdOffset + 13] = 0x00; // v_image_size
            edidData[dtdOffset + 14] = 0x00;
            edidData[dtdOffset + 15] = 0x00; // h_border
            edidData[dtdOffset + 16] = 0x00; // v_border
            edidData[dtdOffset + 17] = newModes[m].flags;
            
            addedCount++;
            LOGD("%s: Added %s DTD at offset %d (pixel_clock=%u)\n",
                 __FUNCTION__, newModes[m].desc, dtdOffset, pc);
        }
        
        LOGD("%s: EDID patched: VIC63=%s, added %d DTDs, DTD start at %d\n", 
             __FUNCTION__, vic63Present ? "present" : "added", 
             addedCount, edidData[extOffset + 2]);
        
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

int CHDMIRxManager::PatchEdidMonitorName(unsigned char *edidData, int edidSize)
{
    if (edidData == NULL || edidSize < 128) {
        LOGE("%s: Invalid EDID data or size\n", __FUNCTION__);
        return -1;
    }

    // Read the downstream display's EDID from HDMI TX
    unsigned char txEdid[REAL_EDID_DATA_SIZE] = {0};
    int ret = ReadEdidFromHdmiTx(txEdid, REAL_EDID_DATA_SIZE);
    if (ret < 0) {
        LOGD("%s: No HDMI TX EDID available (no display connected?), keeping original name\n", __FUNCTION__);
        return 0; // Not an error -- just keep the original name
    }

    // Find the Monitor Name descriptor (tag 0xFC) in the TX EDID base block.
    // EDID base block has 4 descriptor slots at offsets 0x36, 0x48, 0x5A, 0x6C,
    // each 18 bytes long. A display descriptor (non-DTD) has bytes 0-1 == 0x00,0x00
    // and byte 3 is the tag: 0xFC = Monitor Name.
    int txNameOffset = -1;
    for (int off = 0x36; off <= 0x6C; off += 18) {
        if (txEdid[off] == 0x00 && txEdid[off + 1] == 0x00 &&
            txEdid[off + 3] == 0xFC) {
            txNameOffset = off;
            break;
        }
    }

    if (txNameOffset < 0) {
        LOGD("%s: HDMI TX EDID has no Monitor Name descriptor, keeping original name\n", __FUNCTION__);
        return 0;
    }

    // Find the Monitor Name descriptor in the RX EDID base block
    int rxNameOffset = -1;
    for (int off = 0x36; off <= 0x6C; off += 18) {
        if (edidData[off] == 0x00 && edidData[off + 1] == 0x00 &&
            edidData[off + 3] == 0xFC) {
            rxNameOffset = off;
            break;
        }
    }

    if (rxNameOffset < 0) {
        LOGD("%s: RX EDID has no Monitor Name descriptor, cannot patch\n", __FUNCTION__);
        return 0;
    }

    // Copy the 13-byte name payload from TX to RX.
    // The name field is bytes 5-17 of the descriptor (13 bytes).
    // Format: ASCII chars terminated by 0x0A ('\n'), padded with 0x20 (' ').
    memcpy(edidData + rxNameOffset + 5, txEdid + txNameOffset + 5, 13);

    // Log the patched name (extract for debugging)
    char nameStr[14] = {0};
    for (int i = 0; i < 13; i++) {
        unsigned char c = edidData[rxNameOffset + 5 + i];
        if (c == 0x0A) break; // newline terminator
        nameStr[i] = (char)c;
    }
    LOGD("%s: Patched EDID monitor name to '%s' from HDMI TX\n", __FUNCTION__, nameStr);

    // Recalculate base block checksum (bytes 0-126, checksum at byte 127)
    unsigned char checksum = 0;
    for (int i = 0; i < 127; i++) {
        checksum += edidData[i];
    }
    edidData[127] = 256 - checksum;

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
