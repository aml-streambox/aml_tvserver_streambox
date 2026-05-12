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

#ifdef STREAM_BOX
typedef struct {
    int width;
    int height;
    int fps;
    int interlaced;
} HdmiModeInfo;

static int ParseModeName(const char *mode, HdmiModeInfo *info)
{
    int width = 0;
    int height = 0;
    int fps = 0;
    char scan = 0;

    if (mode == NULL || info == NULL) {
        return -1;
    }

    if (sscanf(mode, "%dx%d%c%dhz", &width, &height, &scan, &fps) == 4) {
        info->width = width;
        info->height = height;
        info->fps = fps;
        info->interlaced = (scan == 'i' || scan == 'I');
        return 0;
    }

    if (sscanf(mode, "%d%c%dhz", &height, &scan, &fps) == 3) {
        switch (height) {
        case 2160:
            width = 3840;
            break;
        case 1080:
            width = 1920;
            break;
        case 720:
            width = 1280;
            break;
        case 576:
            width = 720;
            break;
        case 480:
            width = 720;
            break;
        default:
            return -1;
        }

        info->width = width;
        info->height = height;
        info->fps = fps;
        info->interlaced = (scan == 'i' || scan == 'I');
        return 0;
    }

    return -1;
}

static int ReadTxDispCapModes(HdmiModeInfo *modes, int maxModes)
{
    FILE *fp;
    char line[128];
    int count = 0;

    if (modes == NULL || maxModes <= 0) {
        return 0;
    }

    fp = fopen(HDMI_TX_DISP_CAP_PATH, "r");
    if (!fp) {
        LOGE("%s: Failed to open %s: %s\n", __FUNCTION__, HDMI_TX_DISP_CAP_PATH, strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL && count < maxModes) {
        char *p = line;
        char *end;

        while (*p == ' ' || *p == '\t') p++;
        end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == '*' || end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }

        if (ParseModeName(p, &modes[count]) == 0) {
            count++;
        }
    }

    fclose(fp);
    return count;
}

static int ModeSupportedByDispCap(const HdmiModeInfo *modes, int modeCount,
                                  int width, int height, int fps, int interlaced)
{
    int i;

    for (i = 0; i < modeCount; i++) {
        if (modes[i].width == width && modes[i].height == height &&
            modes[i].fps == fps && modes[i].interlaced == interlaced) {
            return 1;
        }
    }

    return 0;
}

static int VicToMode(int vic, HdmiModeInfo *mode)
{
    if (mode == NULL) {
        return -1;
    }

    memset(mode, 0, sizeof(*mode));
    switch (vic) {
    case 1:  mode->width = 640;  mode->height = 480;  mode->fps = 60; break;
    case 2:
    case 3:  mode->width = 720;  mode->height = 480;  mode->fps = 60; break;
    case 4:  mode->width = 1280; mode->height = 720;  mode->fps = 60; break;
    case 5:  mode->width = 1920; mode->height = 1080; mode->fps = 60; mode->interlaced = 1; break;
    case 16: mode->width = 1920; mode->height = 1080; mode->fps = 60; break;
    case 17:
    case 18: mode->width = 720;  mode->height = 576;  mode->fps = 50; break;
    case 19: mode->width = 1280; mode->height = 720;  mode->fps = 50; break;
    case 20: mode->width = 1920; mode->height = 1080; mode->fps = 50; mode->interlaced = 1; break;
    case 31: mode->width = 1920; mode->height = 1080; mode->fps = 50; break;
    case 32: mode->width = 1920; mode->height = 1080; mode->fps = 24; break;
    case 33: mode->width = 1920; mode->height = 1080; mode->fps = 25; break;
    case 34: mode->width = 1920; mode->height = 1080; mode->fps = 30; break;
    case 63: mode->width = 1920; mode->height = 1080; mode->fps = 120; break;
    case 64: mode->width = 1920; mode->height = 1080; mode->fps = 100; break;
    case 93: mode->width = 3840; mode->height = 2160; mode->fps = 24; break;
    case 94: mode->width = 3840; mode->height = 2160; mode->fps = 25; break;
    case 95: mode->width = 3840; mode->height = 2160; mode->fps = 30; break;
    case 96: mode->width = 3840; mode->height = 2160; mode->fps = 50; break;
    case 97: mode->width = 3840; mode->height = 2160; mode->fps = 60; break;
    case 98: mode->width = 4096; mode->height = 2160; mode->fps = 24; break;
    case 99: mode->width = 4096; mode->height = 2160; mode->fps = 25; break;
    case 100: mode->width = 4096; mode->height = 2160; mode->fps = 30; break;
    case 101: mode->width = 4096; mode->height = 2160; mode->fps = 50; break;
    case 102: mode->width = 4096; mode->height = 2160; mode->fps = 60; break;
    default:
        return -1;
    }

    return 0;
}

static int DetailedTimingToMode(const unsigned char *dtd, HdmiModeInfo *mode)
{
    int pixelClock;
    int hActive, hBlank, vActive, vBlank;
    int hTotal, vTotal;
    int fps;

    if (dtd == NULL || mode == NULL) {
        return -1;
    }

    pixelClock = dtd[0] | (dtd[1] << 8);
    if (pixelClock == 0) {
        return -1;
    }

    hActive = dtd[2] | ((dtd[4] & 0xF0) << 4);
    hBlank = dtd[3] | ((dtd[4] & 0x0F) << 8);
    vActive = dtd[5] | ((dtd[7] & 0xF0) << 4);
    vBlank = dtd[6] | ((dtd[7] & 0x0F) << 8);
    hTotal = hActive + hBlank;
    vTotal = vActive + vBlank;

    if (hTotal <= 0 || vTotal <= 0) {
        return -1;
    }

    fps = (pixelClock * 10000 + (hTotal * vTotal / 2)) / (hTotal * vTotal);
    mode->width = hActive;
    mode->height = vActive;
    mode->fps = fps;
    mode->interlaced = !!(dtd[17] & 0x80);
    return 0;
}

static void RecalculateEdidBlockChecksum(unsigned char *block)
{
    unsigned char checksum = 0;
    int i;

    for (i = 0; i < 127; i++) {
        checksum += block[i];
    }
    block[127] = (unsigned char)((256 - checksum) & 0xFF);
}

static void WriteDetailedTiming(unsigned char *dtd,
                                unsigned int pixelClock,
                                unsigned int hActive, unsigned int hBlank,
                                unsigned int hFrontPorch, unsigned int hSync,
                                unsigned int vActive, unsigned int vBlank,
                                unsigned int vFrontPorch, unsigned int vSync,
                                unsigned int hImageSize, unsigned int vImageSize,
                                unsigned char flags)
{
    dtd[0] = pixelClock & 0xFF;
    dtd[1] = (pixelClock >> 8) & 0xFF;
    dtd[2] = hActive & 0xFF;
    dtd[3] = hBlank & 0xFF;
    dtd[4] = (((hActive >> 8) & 0xF) << 4) | ((hBlank >> 8) & 0xF);
    dtd[5] = vActive & 0xFF;
    dtd[6] = vBlank & 0xFF;
    dtd[7] = (((vActive >> 8) & 0xF) << 4) | ((vBlank >> 8) & 0xF);
    dtd[8] = hFrontPorch & 0xFF;
    dtd[9] = hSync & 0xFF;
    dtd[10] = ((vFrontPorch & 0xF) << 4) | (vSync & 0xF);
    dtd[11] = (((hFrontPorch >> 8) & 0x3) << 6) |
              (((hSync >> 8) & 0x3) << 4) |
              (((vFrontPorch >> 4) & 0x3) << 2) |
              ((vSync >> 4) & 0x3);
    dtd[12] = hImageSize & 0xFF;
    dtd[13] = vImageSize & 0xFF;
    dtd[14] = (((hImageSize >> 8) & 0xF) << 4) | ((vImageSize >> 8) & 0xF);
    dtd[15] = 0x00;
    dtd[16] = 0x00;
    dtd[17] = flags;
}

static void ParseTxEdidVics(const unsigned char *edid, int edidSize, unsigned char *txVics, int txVicsSize)
{
    int extCount;
    int ext;

    if (edid == NULL || txVics == NULL || txVicsSize < 256 || edidSize < 128) {
        return;
    }

    extCount = edid[0x7E];
    for (ext = 0; ext < extCount; ext++) {
        int extOffset = 128 + ext * 128;
        int dtdStart;
        int dataEnd;
        int offset;

        if (extOffset + 128 > edidSize || edid[extOffset] != 0x02) {
            continue;
        }

        dtdStart = edid[extOffset + 2];
        dataEnd = dtdStart ? dtdStart : 127;
        if (dataEnd < 4 || dataEnd > 127) {
            dataEnd = 127;
        }

        offset = extOffset + 4;
        while (offset < extOffset + dataEnd) {
            int tag = edid[offset] >> 5;
            int len = edid[offset] & 0x1F;
            int i;

            if (len == 0 || offset + len >= extOffset + 127) {
                break;
            }

            if (tag == 2) {
                for (i = 1; i <= len; i++) {
                    txVics[edid[offset + i] & 0x7F] = 1;
                }
            }
            offset += len + 1;
        }
    }
}

static int TxSupportsVic(int vic, const unsigned char *txVics,
                         const HdmiModeInfo *txModes, int txModeCount)
{
    HdmiModeInfo mode;

    if (vic > 0 && vic < 256 && txVics[vic]) {
        return 1;
    }

    if (VicToMode(vic, &mode) == 0) {
        return ModeSupportedByDispCap(txModes, txModeCount,
                                      mode.width, mode.height, mode.fps, mode.interlaced);
    }

    return 0;
}

static void FilterCeaVideoDataBlocks(unsigned char *extBlock, const unsigned char *txVics,
                                     const HdmiModeInfo *txModes, int txModeCount, int *removedCount)
{
    int dtdStart;
    int dataEnd;
    int offset;

    if (extBlock == NULL || extBlock[0] != 0x02) {
        return;
    }

    dtdStart = extBlock[2];
    dataEnd = dtdStart ? dtdStart : 127;
    if (dataEnd < 4 || dataEnd > 127) {
        dataEnd = 127;
    }

    offset = 4;
    while (offset < dataEnd) {
        int tag = extBlock[offset] >> 5;
        int len = extBlock[offset] & 0x1F;

        if (len == 0 || offset + len >= 127) {
            break;
        }

        if (tag == 2 || (tag == 7 && len >= 2 && extBlock[offset + 1] == 0x0e)) {
            int src;
            int payloadStart = (tag == 2) ? (offset + 1) : (offset + 2);
            int dst = payloadStart;
            int newLen;
            int removeBytes;

            for (src = payloadStart; src <= offset + len; src++) {
                int vic = extBlock[src] & 0x7F;
                if (TxSupportsVic(vic, txVics, txModes, txModeCount)) {
                    extBlock[dst++] = extBlock[src];
                } else {
                    LOGD("%s: filtering RX EDID VIC %d not supported by HDMI TX\n", __FUNCTION__, vic);
                    if (removedCount) (*removedCount)++;
                }
            }

            newLen = dst - (offset + 1);
            if (newLen == len) {
                offset += len + 1;
                continue;
            }

            if ((tag == 2 && newLen == 0) || (tag == 7 && newLen <= 1)) {
                removeBytes = len + 1;
                memmove(extBlock + offset, extBlock + offset + removeBytes, 127 - (offset + removeBytes));
                memset(extBlock + 127 - removeBytes, 0, removeBytes);
                if (dtdStart) {
                    extBlock[2] -= removeBytes;
                    dtdStart -= removeBytes;
                    dataEnd -= removeBytes;
                }
                continue;
            }

            removeBytes = len - newLen;
            extBlock[offset] = (unsigned char)((tag << 5) | newLen);
            memmove(extBlock + offset + 1 + newLen,
                    extBlock + offset + 1 + len,
                    127 - (offset + 1 + len));
            memset(extBlock + 127 - removeBytes, 0, removeBytes);
            if (dtdStart) {
                extBlock[2] -= removeBytes;
                dtdStart -= removeBytes;
                dataEnd -= removeBytes;
            }
            offset += newLen + 1;
        } else {
            offset += len + 1;
        }
    }
}

static void RemoveCeaDataBlocksByOui(unsigned char *extBlock, int oui0, int oui1, int oui2, int *removedCount)
{
    int dtdStart;
    int dataEnd;
    int offset;

    if (extBlock == NULL || extBlock[0] != 0x02) {
        return;
    }

    dtdStart = extBlock[2];
    dataEnd = dtdStart ? dtdStart : 127;
    if (dataEnd < 4 || dataEnd > 127) {
        dataEnd = 127;
    }

    offset = 4;
    while (offset < dataEnd) {
        int tag = extBlock[offset] >> 5;
        int len = extBlock[offset] & 0x1F;

        if (len == 0 || offset + len >= 127) {
            break;
        }

        if (tag == 3 && len >= 3 &&
            extBlock[offset + 1] == oui0 && extBlock[offset + 2] == oui1 && extBlock[offset + 3] == oui2) {
            int removeBytes = len + 1;
            LOGD("%s: removing RX EDID vendor block OUI %02x-%02x-%02x not supported by HDMI TX\n",
                 __FUNCTION__, oui0, oui1, oui2);
            memmove(extBlock + offset, extBlock + offset + removeBytes, 127 - (offset + removeBytes));
            memset(extBlock + 127 - removeBytes, 0, removeBytes);
            if (dtdStart) {
                extBlock[2] -= removeBytes;
                dtdStart -= removeBytes;
                dataEnd -= removeBytes;
            }
            if (removedCount) (*removedCount)++;
            continue;
        }

        offset += len + 1;
    }
}

static void FilterDetailedTimings(unsigned char *block, int start, int end,
                                  const HdmiModeInfo *txModes, int txModeCount, int *removedCount)
{
    int offset;

    for (offset = start; offset + 18 <= end; offset += 18) {
        HdmiModeInfo mode;

        if (DetailedTimingToMode(block + offset, &mode) != 0) {
            continue;
        }

        if (!ModeSupportedByDispCap(txModes, txModeCount,
                                    mode.width, mode.height, mode.fps, mode.interlaced)) {
            LOGD("%s: filtering RX EDID DTD %dx%d%s%d not supported by HDMI TX\n",
                 __FUNCTION__, mode.width, mode.height, mode.interlaced ? "i" : "p", mode.fps);
            memset(block + offset, 0, 18);
            if (removedCount) (*removedCount)++;
        }
    }
}

static void FilterHfScdbCapabilities(unsigned char *payload, int count, int allmSupported, int vrrSupported)
{
    if (payload == NULL) {
        return;
    }

    if (count >= 8 && !allmSupported) {
        payload[7] &= (unsigned char)~(1 << 1);
    }

    if (count >= 10 && !vrrSupported) {
        payload[7] &= (unsigned char)~((1 << 3) | (1 << 5) | (1 << 6));
        payload[8] = 0;
        payload[9] = 0;
    }
}

static void FilterCeaHfScdb(unsigned char *extBlock, int allmSupported, int vrrSupported)
{
    int dtdStart;
    int dataEnd;
    int offset;

    if (extBlock == NULL || extBlock[0] != 0x02) {
        return;
    }

    dtdStart = extBlock[2];
    dataEnd = dtdStart ? dtdStart : 127;
    if (dataEnd < 4 || dataEnd > 127) {
        dataEnd = 127;
    }

    offset = 4;
    while (offset < dataEnd) {
        int tag = extBlock[offset] >> 5;
        int len = extBlock[offset] & 0x1F;

        if (len == 0 || offset + len >= 127) {
            break;
        }

        if (tag == 3 && len >= 3 &&
            extBlock[offset + 1] == 0xd8 && extBlock[offset + 2] == 0x5d && extBlock[offset + 3] == 0xc4) {
            FilterHfScdbCapabilities(extBlock + offset + 1, len, allmSupported, vrrSupported);
        } else if (tag == 7 && len >= 1 && extBlock[offset + 1] == 0x79) {
            FilterHfScdbCapabilities(extBlock + offset + 1, len, allmSupported, vrrSupported);
        }

        offset += len + 1;
    }
}

static int ParseTxFeatureSupport(const unsigned char *edid, int edidSize, int feature)
{
    int extCount;
    int ext;

    if (edid == NULL || edidSize < 128) {
        return 0;
    }

    extCount = edid[0x7E];
    for (ext = 0; ext < extCount; ext++) {
        int extOffset = 128 + ext * 128;
        int dtdStart;
        int dataEnd;
        int offset;

        if (extOffset + 128 > edidSize || edid[extOffset] != 0x02) {
            continue;
        }

        dtdStart = edid[extOffset + 2];
        dataEnd = dtdStart ? dtdStart : 127;
        if (dataEnd < 4 || dataEnd > 127) {
            dataEnd = 127;
        }

        offset = extOffset + 4;
        while (offset < extOffset + dataEnd) {
            int tag = edid[offset] >> 5;
            int len = edid[offset] & 0x1F;
            unsigned char *payload = NULL;

            if (len == 0 || offset + len >= extOffset + 127) {
                break;
            }

            if (tag == 3 && len >= 3 &&
                edid[offset + 1] == 0xd8 && edid[offset + 2] == 0x5d && edid[offset + 3] == 0xc4) {
                payload = (unsigned char *)(edid + offset + 1);
            } else if (tag == 7 && len >= 1 && edid[offset + 1] == 0x79) {
                payload = (unsigned char *)(edid + offset + 1);
            }

            if (payload != NULL) {
                if (feature == 0 && len >= 8 && (payload[7] & (1 << 1))) {
                    return 1;
                }
                if (feature == 1 && len >= 10) {
                    int vrrMin = payload[8] & 0x3F;
                    int vrrMax = (((payload[8] & 0xC0) >> 6) << 8) | payload[9];
                    if (vrrMin > 0 && vrrMax > 0) {
                        return 1;
                    }
                }
            }

            offset += len + 1;
        }
    }

    return 0;
}
#endif

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

int CHDMIRxManager::UpdataEdidDataWithPort(int port, unsigned char *dataBuf, int dataSize, int edidType)
{
    int ret = -1;
    int size = dataSize + 1;
    unsigned char LoadBuf[size];
    memset(LoadBuf, 0, sizeof(char) * size);
    // Header byte: low nibble = port (1-based), high nibble = edid_type
    LoadBuf[0] = (unsigned char)((edidType << 4) | (port & 0x0F));
    memcpy(LoadBuf+1, dataBuf, dataSize);
    LOGD("%s: port=%d, dataSize=%d, edidType=%d, header=0x%02x\n",
         __FUNCTION__, port, dataSize, edidType, LoadBuf[0]);
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
        unsigned char portIdx = (unsigned char)port;
        HDMIRxDeviceIOCtl(HDMI_IOC_EDID_UPDATE_WITH_PORT, &portIdx);
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
    if (edidData == NULL || edidSize < PATCHED_EDID_MAX_SIZE) {
        LOGE("%s: Invalid EDID data or buffer size (need %d, got %d)\n",
             __FUNCTION__, PATCHED_EDID_MAX_SIZE, edidSize);
        return -1;
    }

    LOGD("%s: Patching EDID for high-refresh and non-standard modes\n", __FUNCTION__);

    // Verify base block has at least 1 extension
    int numExtensions = edidData[0x7E];
    if (numExtensions == 0) {
        LOGE("%s: No extension blocks found\n", __FUNCTION__);
        return -1;
    }

    // Find CEA extension block (tag 0x02)
    int extOffset = -1;
    for (int ext = 0; ext < numExtensions; ext++) {
        int testOffset = 128 + ext * 128;
        if (testOffset + 128 > REAL_EDID_DATA_SIZE) break;
        if (edidData[testOffset] == 0x02) {
            extOffset = testOffset;
            break;
        }
    }
    if (extOffset < 0) {
        LOGE("%s: No CEA extension block found\n", __FUNCTION__);
        return -1;
    }

    LOGD("%s: CEA extension at offset %d\n", __FUNCTION__, extOffset);

    // Parse the CEA extension to find the video data block
    int dtdStart = edidData[extOffset + 2];
    if (dtdStart < 4) dtdStart = 4;
    int dataBlockOffset = extOffset + 4;
    int dataBlockEnd = extOffset + dtdStart;

    int videoBlockOffset = -1;
    int videoBlockLength = 0;
    bool vic63Present = false;

    int offset = dataBlockOffset;
    while (offset < dataBlockEnd && offset < extOffset + 127) {
        unsigned char tag = (edidData[offset] >> 5) & 0x07;
        unsigned char length = edidData[offset] & 0x1F;
        if (length == 0 || offset + length + 1 > extOffset + 127) break;

        if (tag == 0x02) { // Video data block
            videoBlockOffset = offset;
            videoBlockLength = length;
            for (int i = 1; i <= length; i++) {
                if ((edidData[offset + i] & 0x7F) == 63) {
                    vic63Present = true;
                    break;
                }
            }
            break;
        }
        offset += length + 1;
    }

    // ---- Step 1: Add VIC 63 (1080p120) to the existing video data block ----
    // We do this by shifting all subsequent data blocks right by 1 byte
    if (!vic63Present && videoBlockOffset >= 0 && videoBlockLength < 31) {
        int insertPos = videoBlockOffset + videoBlockLength + 1;
        int shiftEnd = extOffset + 127; // byte before checksum
        int shiftBytes = shiftEnd - insertPos;

        if (shiftBytes > 0 && dtdStart + 1 <= 127) {
            // Shift everything after the video block right by 1 byte
            memmove(edidData + insertPos + 1, edidData + insertPos, shiftBytes);
            // Insert VIC 63
            edidData[insertPos] = 63;
            // Update video block length
            videoBlockLength++;
            edidData[videoBlockOffset] = (edidData[videoBlockOffset] & 0xE0) | (videoBlockLength & 0x1F);
            // Update DTD start offset
            dtdStart++;
            edidData[extOffset + 2] = dtdStart;
            LOGD("%s: Added VIC 63 (1080p120) to video block, new length=%d, dtdStart=%d\n",
                 __FUNCTION__, videoBlockLength, dtdStart);
        } else {
            LOGD("%s: Cannot add VIC 63 - no room to shift (dtdStart=%d)\n",
                 __FUNCTION__, dtdStart);
        }
    } else if (vic63Present) {
        LOGD("%s: VIC 63 already present\n", __FUNCTION__);
    }

    // Recalculate checksum for the first extension block
    unsigned char checksum = 0;
    for (int i = extOffset; i < extOffset + 127; i++) {
        checksum += edidData[i];
    }
    edidData[extOffset + 127] = (256 - checksum) & 0xFF;

    // ---- Step 1b: Add 2560x1440 DTDs to the base block ----
    // Some sources only consume the first 256 bytes reliably. Keep the high
    // refresh timings in base DTD slots as well as the CTA extension block.
    {
        struct {
            int offset;
            unsigned int pixelClock;
            unsigned int vBlank;
            const char *desc;
        } baseModes[] = {
            { 0x36, 57104, 18, "2560x1440p144" },
            { 0x48, 49775, 85, "2560x1440p120" },
            { 0x5A, 24150, 41, "2560x1440p60" },
        };
        int i;

        for (i = 0; i < (int)(sizeof(baseModes) / sizeof(baseModes[0])); i++) {
            WriteDetailedTiming(edidData + baseModes[i].offset,
                                baseModes[i].pixelClock,
                                2560, 160, 48, 32,
                                1440, baseModes[i].vBlank, 3, 5,
                                800, 450, 0x1E);
            LOGD("%s: Added %s DTD to base EDID slot 0x%02x\n",
                 __FUNCTION__, baseModes[i].desc, baseModes[i].offset);
        }
    }

    // Recalculate base block checksum after slot 3 replacement
    checksum = 0;
    for (int i = 0; i < 127; i++) {
        checksum += edidData[i];
    }
    edidData[127] = (256 - checksum) & 0xFF;

    // ---- Step 2: Create a second CEA extension block with DTDs ----
    // The second extension block goes at offset 256 (base=128, ext1=128, ext2=128)
    int ext2Offset = 256;

    // Initialize the second CEA extension block
    memset(edidData + ext2Offset, 0, 128);
    edidData[ext2Offset + 0] = 0x02; // CEA extension tag
    edidData[ext2Offset + 1] = 0x03; // CEA revision 3
    // Byte 2 = DTD start offset (relative to ext block start)
    // No data blocks in this extension, DTDs start at offset 4
    edidData[ext2Offset + 2] = 4;
    edidData[ext2Offset + 3] = 0x00; // No native DTDs, no additional capabilities

    // DTD mode table for non-standard modes
    // pixel_clock in 10kHz units, timing parameters per EDID spec
    struct {
        unsigned int pixelClock;
        unsigned int hActive, hBlank, hFrontPorch, hSync;
        unsigned int vActive, vBlank, vFrontPorch, vSync;
        unsigned char flags; // byte 17: signal features
        const char *desc;
    } newModes[] = {
        // 2560x1440@120Hz timing supported by the T7 HDMI TX table
        { 49775, 2560, 160, 48, 32, 1440, 85, 3, 5, 0x1E, "2560x1440p120" },
        // 2560x1440@60Hz CVT-RBv2
        { 24150, 2560, 160, 48, 32, 1440, 41, 3, 5, 0x1E, "2560x1440p60" },
        // 1920x1080@144Hz CVT-RBv2
        { 35640, 1920, 280, 88, 44, 1080, 45, 4, 5, 0x1E, "1920x1080p144" },
        // 1920x1080@240Hz CVT-RBv2
        { 59400, 1920, 280, 88, 44, 1080, 45, 4, 5, 0x1E, "1920x1080p240" },
        // 3440x1440@60Hz CVT-RBv2
        { 31975, 3440, 160, 48, 32, 1440, 49, 3, 5, 0x1E, "3440x1440p60" },
        // 2560x1440@144Hz CVT-RBv2
        { 57104, 2560, 160, 48, 32, 1440, 18, 3, 5, 0x1E, "2560x1440p144" },
    };
    int numModes = sizeof(newModes) / sizeof(newModes[0]);

    int dtd2Start = 4; // DTDs start at offset 4 in the second extension
    int dtd2End = 127; // byte before checksum
    int dtdOffset = ext2Offset + dtd2Start;
    int addedCount = 0;

    for (int m = 0; m < numModes; m++) {
        if (dtdOffset + 18 > ext2Offset + dtd2End) {
            LOGD("%s: No more DTD space for %s in ext block 2\n",
                 __FUNCTION__, newModes[m].desc);
            break;
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
        // Image size matching stock DTDs (800mm x 450mm)
        edidData[dtdOffset + 12] = 0x20; // h_image_size low (800 & 0xFF = 0x20)
        edidData[dtdOffset + 13] = 0xC2; // v_image_size low (450 & 0xFF = 0xC2)
        edidData[dtdOffset + 14] = 0x31; // high nibbles ((800>>8)<<4 | (450>>8) = 0x31)
        edidData[dtdOffset + 15] = 0x00; // h_border
        edidData[dtdOffset + 16] = 0x00; // v_border
        edidData[dtdOffset + 17] = newModes[m].flags;

        addedCount++;
        LOGD("%s: Added %s DTD at offset %d (pixel_clock=%u)\n",
             __FUNCTION__, newModes[m].desc, dtdOffset, pc);
        dtdOffset += 18;
    }

    // Update base block extension count
    edidData[0x7E] = 2; // base + ext1 + ext2

    // Recalculate base block checksum
    checksum = 0;
    for (int i = 0; i < 127; i++) {
        checksum += edidData[i];
    }
    edidData[127] = (256 - checksum) & 0xFF;

    // Recalculate second extension block checksum
    checksum = 0;
    for (int i = ext2Offset; i < ext2Offset + 127; i++) {
        checksum += edidData[i];
    }
    edidData[ext2Offset + 127] = (256 - checksum) & 0xFF;

    LOGD("%s: EDID patched: VIC63=%s, %d DTDs added in ext block 2, total size=%d bytes\n",
         __FUNCTION__, vic63Present ? "already present" : "added",
         addedCount, PATCHED_EDID_MAX_SIZE);

    return addedCount > 0 ? 0 : -1;
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

int CHDMIRxManager::GetTxAllmSupported(void)
{
    unsigned char txEdid[REAL_EDID_DATA_SIZE] = {0};

    if (ReadEdidFromHdmiTx(txEdid, REAL_EDID_DATA_SIZE) < 0) {
        return 0;
    }

    return ParseTxFeatureSupport(txEdid, REAL_EDID_DATA_SIZE, 0);
}

int CHDMIRxManager::GetTxVrrSupported(void)
{
    unsigned char txEdid[REAL_EDID_DATA_SIZE] = {0};

    if (ReadEdidFromHdmiTx(txEdid, REAL_EDID_DATA_SIZE) < 0) {
        return 0;
    }

    return ParseTxFeatureSupport(txEdid, REAL_EDID_DATA_SIZE, 1);
}

int CHDMIRxManager::FilterEdidByTxCapabilities(unsigned char *edidData, int edidSize)
{
    unsigned char txEdid[REAL_EDID_DATA_SIZE] = {0};
    unsigned char txVics[256] = {0};
    HdmiModeInfo txModes[128];
    int txModeCount;
    int extCount;
    int ext;
    int removedCount = 0;
    int allmSupported;
    int vrrSupported;

    if (edidData == NULL || edidSize < 128) {
        LOGE("%s: Invalid EDID data or size\n", __FUNCTION__);
        return -1;
    }

    if (ReadEdidFromHdmiTx(txEdid, REAL_EDID_DATA_SIZE) < 0) {
        LOGD("%s: No HDMI TX EDID available, skipping TX capability filter\n", __FUNCTION__);
        return 0;
    }

    memset(txModes, 0, sizeof(txModes));
    txModeCount = ReadTxDispCapModes(txModes, sizeof(txModes) / sizeof(txModes[0]));
    if (txModeCount <= 0) {
        LOGD("%s: No HDMI TX disp_cap modes available, skipping TX capability filter\n", __FUNCTION__);
        return 0;
    }

    ParseTxEdidVics(txEdid, REAL_EDID_DATA_SIZE, txVics, sizeof(txVics));
    allmSupported = ParseTxFeatureSupport(txEdid, REAL_EDID_DATA_SIZE, 0);
    vrrSupported = ParseTxFeatureSupport(txEdid, REAL_EDID_DATA_SIZE, 1);

    FilterDetailedTimings(edidData, 0x36, 0x7E, txModes, txModeCount, &removedCount);
    RecalculateEdidBlockChecksum(edidData);

    extCount = edidData[0x7E];
    for (ext = 0; ext < extCount; ext++) {
        int extOffset = 128 + ext * 128;
        int dtdStart;

        if (extOffset + 128 > edidSize) {
            break;
        }

        if (edidData[extOffset] != 0x02) {
            RecalculateEdidBlockChecksum(edidData + extOffset);
            continue;
        }

        FilterCeaVideoDataBlocks(edidData + extOffset, txVics, txModes, txModeCount, &removedCount);
        FilterCeaHfScdb(edidData + extOffset, allmSupported, vrrSupported);
        if (!vrrSupported) {
            RemoveCeaDataBlocksByOui(edidData + extOffset, 0x1a, 0x00, 0x00, &removedCount);
        }

        dtdStart = edidData[extOffset + 2];
        if (dtdStart >= 4 && dtdStart < 127) {
            FilterDetailedTimings(edidData + extOffset, dtdStart, 127, txModes, txModeCount, &removedCount);
        }
        RecalculateEdidBlockChecksum(edidData + extOffset);
    }

    LOGD("%s: HDMI TX capability filter complete: removed=%d, allm=%d, vrr=%d\n",
         __FUNCTION__, removedCount, allmSupported, vrrSupported);
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
