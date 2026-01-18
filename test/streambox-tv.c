/*
 * StreamBox TV - HDMI Passthrough Application
 *
 * Config-file driven design with dynamic reload support.
 */

#define LOG_MOUDLE_TAG "TV"
#define LOG_CLASS_TAG "StreamBoxTV"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <TvClientWrapper.h>
#include <CTvClientLog.h>

#include "config.h"

#ifdef STREAM_BOX
#include "amvideo.h"

/* Video layer status enum */
typedef enum video_layer_status_e {
    VIDEO_LAYER_STATUS_ENABLE,
    VIDEO_LAYER_STATUS_DISABLE,
    VIDEO_LAYER_STATUS_ENABLE_AND_CLEAN,
    VIDEO_LAYER_STATUS_MAX,
} video_layer_status_t;

/* Video global output mode enum */
typedef enum video_global_output_mode_e {
    VIDEO_GLOBAL_OUTPUT_MODE_DISABLE,
    VIDEO_GLOBAL_OUTPUT_MODE_ENABLE,
    VIDEO_GLOBAL_OUTPUT_MODE_MAX,
} video_global_output_mode_t;
#endif

#define AM_VIDEO_PATH           "/dev/amvideo"

static int run = 1;
static struct TvClientWrapper_t *g_pTvClientWrapper = NULL;

/* VRR force frame lock for HDMI passthrough low latency mode */
#define VRR_DEBUG_PATH        "/sys/class/aml_vrr/vrr2/debug"

/* HDMI TX HPD state sysfs path */
#define HDMI_TX_HPD_STATE_PATH "/sys/class/amhdmitx/amhdmitx0/hpd_state"

/* HDMI TX HDR control sysfs paths */
#define HDMI_TX_TEST_ATTR_PATH "/sys/class/amhdmitx/amhdmitx0/test_attr"
#define HDMI_TX_CONFIG_PATH    "/sys/class/amhdmitx/amhdmitx0/config"

/* HDMI RX HDR info sysfs paths */
#define HDMI_RX_COLORSPACE_PATH   "/sys/class/hdmirx/hdmirx0/colorspace"
#define HDMI_RX_HDR_STATUS_PATH   "/sys/class/hdmirx/hdmirx0/hdmi_hdr_status"


/* HDMI TX HPD monitoring state */
static int g_hdmitx_connected = 0;   /* 0=disconnected, 1=connected */
static int g_tv_started = 0;         /* 0=stopped, 1=started */
static int g_uevent_fd = -1;         /* Netlink socket for uevent */
static pthread_t g_uevent_thread;    /* Uevent monitor thread */

/* Audio passthrough (alsaloop) process management */
static pid_t g_alsaloop_pid = -1;    /* PID of running alsaloop process, -1 if not running */
static pthread_mutex_t g_audio_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Trace macro - uses config value */
#define TRACE(level, fmt, ...) do { \
    if (config_get()->debug.trace_level >= (level)) { \
        printf("[TRACE] streambox-tv: " fmt, ##__VA_ARGS__); \
        fflush(stdout); \
    } \
} while(0)

/* --- Forward Declarations --- */
static void StartAudioPassthrough(void);
static void StopAudioPassthrough(void);
static void StartTvProcessing(struct TvClientWrapper_t *pTvClientWrapper);
static void StopTvProcessing(struct TvClientWrapper_t *pTvClientWrapper);
static void ApplyVrrMode(int vrr_mode);

/* --- Sysfs Helpers --- */

static int WriteSysfs(const char *path, const char *cmd)
{
    int fd;
    fd = open(path, O_CREAT|O_RDWR | O_TRUNC, 0777);

    if (fd >= 0) {
        write(fd, cmd, strlen(cmd));
        close(fd);
        return 0;
    }

    return -1;
}

static int ReadSysfs(const char *path, char *buf, size_t buf_size)
{
    int fd;
    ssize_t bytes_read;

    if (path == NULL || buf == NULL || buf_size == 0) {
        return -1;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    bytes_read = read(fd, buf, buf_size - 1);
    close(fd);

    if (bytes_read < 0) {
        return -1;
    }

    buf[bytes_read] = '\0';
    if (bytes_read > 0 && buf[bytes_read - 1] == '\n') {
        buf[bytes_read - 1] = '\0';
    }
    return 0;
}

#ifdef STREAM_BOX
static int AmVideoOpen(void)
{
    int fd = open(AM_VIDEO_PATH, O_RDWR);
    if (fd < 0) {
        LOGE("Open %s error(%s)!\n", AM_VIDEO_PATH, strerror(errno));
    }
    return fd;
}

static int AmVideoDeviceIOCtl(int fd, int request, ...)
{
    int ret = -1;
    va_list ap;
    void *arg;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    ret = ioctl(fd, request, arg);
    return ret;
}

static int SetVideoLayerStatus(int fd, int status)
{
    int ret = -1;
    ret = AmVideoDeviceIOCtl(fd, AMSTREAM_IOC_SET_VIDEO_DISABLE, &status);
    if (ret < 0) {
        LOGE("%s: SetVideoLayerStatus status=%d failed\n", __FUNCTION__, status);
    } else {
        LOGD("%s: SetVideoLayerStatus status=%d success\n", __FUNCTION__, status);
    }
    return ret;
}

static int SetVideoGlobalOutputMode(int fd, int mode)
{
    int ret = -1;
    ret = AmVideoDeviceIOCtl(fd, AMSTREAM_IOC_GLOBAL_SET_VIDEO_OUTPUT, &mode);
    if (ret < 0) {
        LOGE("%s: SetVideoGlobalOutputMode mode=%d failed\n", __FUNCTION__, mode);
    } else {
        LOGD("%s: SetVideoGlobalOutputMode mode=%d success\n", __FUNCTION__, mode);
    }
    return ret;
}

static int ClearVideo(int fd)
{
    int ret = -1;
    int clear_cmd = 0;
    ret = AmVideoDeviceIOCtl(fd, AMSTREAM_IOC_CLEAR_VIDEO, &clear_cmd);
    if (ret < 0) {
        LOGE("%s: ClearVideo failed\n", __FUNCTION__);
    } else {
        LOGD("%s: ClearVideo success\n", __FUNCTION__);
    }
    return ret;
}

static int ClearVideoBuffer(int fd)
{
    int ret = -1;
    ret = AmVideoDeviceIOCtl(fd, AMSTREAM_IOC_CLEAR_VBUF, 0);
    if (ret < 0) {
        LOGE("%s: ClearVideoBuffer failed\n", __FUNCTION__);
    } else {
        LOGD("%s: ClearVideoBuffer success\n", __FUNCTION__);
    }
    return ret;
}

static int ResetVideoPipeline(void)
{
    int amvideo_fd = -1;
    int ret = 0;

    LOGD("%s: Resetting video pipeline for resolution change\n", __FUNCTION__);

    amvideo_fd = AmVideoOpen();
    if (amvideo_fd < 0) {
        LOGE("%s: Failed to open amvideo device\n", __FUNCTION__);
        return -1;
    }

    ret |= SetVideoLayerStatus(amvideo_fd, VIDEO_LAYER_STATUS_DISABLE);
    ret |= SetVideoGlobalOutputMode(amvideo_fd, VIDEO_GLOBAL_OUTPUT_MODE_DISABLE);
    usleep(10000);

    ret |= ClearVideo(amvideo_fd);
    ret |= ClearVideoBuffer(amvideo_fd);
    usleep(50000);

    ret |= SetVideoLayerStatus(amvideo_fd, VIDEO_LAYER_STATUS_ENABLE);
    ret |= SetVideoGlobalOutputMode(amvideo_fd, VIDEO_GLOBAL_OUTPUT_MODE_ENABLE);

    close(amvideo_fd);

    if (ret == 0) {
        LOGD("%s: Video pipeline reset complete\n", __FUNCTION__);
    } else {
        LOGE("%s: Video pipeline reset failed with errors\n", __FUNCTION__);
    }

    return ret;
}
#endif

/* --- Audio Passthrough --- */

static void StartAudioPassthrough(void)
{
    const audio_config_t *audio = &config_get()->audio;
    char latency_str[16];
    char channels_str[8];
    char rate_str[16];

    pthread_mutex_lock(&g_audio_mutex);

    if (!audio->enabled) {
        LOGD("%s: Audio passthrough disabled in config\n", __FUNCTION__);
        pthread_mutex_unlock(&g_audio_mutex);
        return;
    }

    if (g_alsaloop_pid > 0) {
        LOGD("%s: alsaloop already running (pid=%d)\n", __FUNCTION__, g_alsaloop_pid);
        pthread_mutex_unlock(&g_audio_mutex);
        return;
    }

    LOGD("%s: Starting audio passthrough (alsaloop)\n", __FUNCTION__);
    LOGD("%s: capture=%s playback=%s latency=%d format=%s channels=%d rate=%d\n",
         __FUNCTION__, audio->capture_device, audio->playback_device,
         audio->latency_us, audio->sample_format, audio->channels, audio->sample_rate);

    snprintf(latency_str, sizeof(latency_str), "%d", audio->latency_us);
    snprintf(channels_str, sizeof(channels_str), "%d", audio->channels);
    snprintf(rate_str, sizeof(rate_str), "%d", audio->sample_rate);

    pid_t pid = fork();
    if (pid < 0) {
        LOGD("%s: fork() failed: %s\n", __FUNCTION__, strerror(errno));
        pthread_mutex_unlock(&g_audio_mutex);
        return;
    } else if (pid == 0) {
        /* Child process - execute alsaloop */
        execlp("alsaloop", "alsaloop",
               "-C", audio->capture_device,
               "-P", audio->playback_device,
               "-t", latency_str,
               "-f", audio->sample_format,
               "-c", channels_str,
               "-r", rate_str,
               NULL);
        /* If execlp returns, it failed */
        LOGD("%s: execlp(alsaloop) failed: %s\n", __FUNCTION__, strerror(errno));
        _exit(1);
    } else {
        /* Parent process */
        g_alsaloop_pid = pid;
        LOGD("%s: alsaloop started with pid=%d\n", __FUNCTION__, g_alsaloop_pid);
    }

    pthread_mutex_unlock(&g_audio_mutex);
}

static void StopAudioPassthrough(void)
{
    pthread_mutex_lock(&g_audio_mutex);

    if (g_alsaloop_pid <= 0) {
        LOGD("%s: alsaloop not running\n", __FUNCTION__);
        pthread_mutex_unlock(&g_audio_mutex);
        return;
    }

    LOGD("%s: Stopping audio passthrough (pid=%d)\n", __FUNCTION__, g_alsaloop_pid);

    if (kill(g_alsaloop_pid, SIGTERM) == 0) {
        int status;
        waitpid(g_alsaloop_pid, &status, 0);
        LOGD("%s: alsaloop terminated (status=%d)\n", __FUNCTION__, status);
    } else {
        LOGD("%s: kill() failed: %s\n", __FUNCTION__, strerror(errno));
    }

    g_alsaloop_pid = -1;
    pthread_mutex_unlock(&g_audio_mutex);
}

/* Restart audio with new config (called on audio config change) */
static void RestartAudioPassthrough(void)
{
    LOGD("%s: Restarting audio passthrough with new config\n", __FUNCTION__);
    StopAudioPassthrough();
    usleep(100000); /* 100ms delay */
    StartAudioPassthrough();
}

/* --- VRR Mode Application --- */

static void ApplyVrrMode(int vrr_mode)
{
    const video_config_t *video = &config_get()->video;
    int ret;

    if (video->game_mode != 2) {
        LOGD("%s: Game mode is %d, skipping VRR\n", __FUNCTION__, video->game_mode);
        return;
    }

    LOGD("%s: Applying VRR mode %d\n", __FUNCTION__, vrr_mode);

    switch (vrr_mode) {
    case 0: /* Force VRR Only */
        LOGD("%s: Setting Force VRR mode (vrr mode 1)\n", __FUNCTION__);
        ret = WriteSysfs(VRR_DEBUG_PATH, "mode 1");
        if (ret == 0) {
            ret = WriteSysfs(VRR_DEBUG_PATH, "en 1");
        }
        break;

    case 1: /* VRR On (EDID advertise) */
        LOGD("%s: Setting VRR On mode (vrr mode 0, EDID)\n", __FUNCTION__);
        /* TODO: Modify EDID to advertise VRR support */
        /* For now, enable native VRR if supported */
        if (g_pTvClientWrapper != NULL) {
            int vrr_enabled = GetHdmiVrrEnabled(g_pTvClientWrapper);
            if (vrr_enabled) {
                ret = WriteSysfs(VRR_DEBUG_PATH, "mode 0");
                if (ret == 0) {
                    ret = WriteSysfs(VRR_DEBUG_PATH, "en 1");
                }
            } else {
                LOGD("%s: Native VRR not supported, VRR disabled\n", __FUNCTION__);
                WriteSysfs(VRR_DEBUG_PATH, "en 0");
            }
        }
        break;

    case 2: /* Auto (default) */
    default:
        LOGD("%s: Setting VRR Auto mode\n", __FUNCTION__);
        /* Check if native VRR is supported */
        if (g_pTvClientWrapper != NULL) {
            int vrr_enabled = GetHdmiVrrEnabled(g_pTvClientWrapper);
            if (vrr_enabled) {
                /* Use native VRR */
                LOGD("%s: Native VRR supported, using vrr mode 0\n", __FUNCTION__);
                ret = WriteSysfs(VRR_DEBUG_PATH, "mode 0");
            } else {
                /* Fallback to force VRR */
                LOGD("%s: Native VRR not supported, using force VRR (mode 1)\n", __FUNCTION__);
                ret = WriteSysfs(VRR_DEBUG_PATH, "mode 1");
            }
            if (ret == 0) {
                ret = WriteSysfs(VRR_DEBUG_PATH, "en 1");
            }
        }
        break;
    }
}

/* --- Config Change Callback --- */

static void OnConfigChange(config_change_t changes, const streambox_config_t *old_config, const streambox_config_t *new_config)
{
    LOGD("%s: Config changed, changes=0x%x\n", __FUNCTION__, changes);

    /* Handle audio changes - restart alsaloop without affecting video */
    if (changes & CONFIG_CHANGE_AUDIO) {
        LOGD("%s: Audio config changed, restarting audio passthrough\n", __FUNCTION__);
        if (g_tv_started) {
            RestartAudioPassthrough();
        }
    }

    /* Handle video changes */
    if (changes & CONFIG_CHANGE_VIDEO) {
        LOGD("%s: Video config changed\n", __FUNCTION__);
        
        /* Check if VRR mode changed */
        if (old_config->video.vrr_mode != new_config->video.vrr_mode) {
            LOGD("%s: VRR mode changed %d -> %d\n", __FUNCTION__,
                 old_config->video.vrr_mode, new_config->video.vrr_mode);
            if (g_tv_started) {
                ApplyVrrMode(new_config->video.vrr_mode);
            }
        }

        /* Check if game mode changed */
        if (old_config->video.game_mode != new_config->video.game_mode) {
            LOGD("%s: Game mode changed %d -> %d\n", __FUNCTION__,
                 old_config->video.game_mode, new_config->video.game_mode);
            /* Game mode change requires restart */
            if (g_tv_started && g_pTvClientWrapper != NULL) {
                int game_mode_value = (new_config->video.game_mode == 2) ? 3 : 
                                      (new_config->video.game_mode == 1) ? 1 : 0;
                SetGameMode(g_pTvClientWrapper, new_config->video.game_mode > 0 ? 1 : 0, game_mode_value);
            }
        }

        /* Check if HDMI source changed */
        if (strcmp(old_config->video.hdmi_source, new_config->video.hdmi_source) != 0) {
            LOGD("%s: HDMI source changed %s -> %s (requires restart)\n", __FUNCTION__,
                 old_config->video.hdmi_source, new_config->video.hdmi_source);
            /* Source change requires full restart - not implemented for dynamic reload */
        }
    }

    /* Handle HDCP changes */
    if (changes & CONFIG_CHANGE_HDCP) {
        LOGD("%s: HDCP config changed\n", __FUNCTION__);
        /* HDCP changes would be applied here */
    }

    /* Handle debug changes */
    if (changes & CONFIG_CHANGE_DEBUG) {
        LOGD("%s: Debug config changed, trace_level=%d\n", __FUNCTION__, new_config->debug.trace_level);
        /* Trace level takes effect immediately via config_get() */
    }
}

/* --- HDMI TX HPD Monitoring --- */

static int GetHdmiTxHpdState(void)
{
    char buf[16] = {0};
    int ret = ReadSysfs(HDMI_TX_HPD_STATE_PATH, buf, sizeof(buf));
    if (ret != 0) {
        return -1;
    }
    return (atoi(buf) == 1) ? 1 : 0;
}

static int CreateUeventSocket(void)
{
    struct sockaddr_nl addr;
    int fd;

    fd = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) {
        LOGD("Failed to create uevent socket\n");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid();
    addr.nl_groups = 1;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGD("Failed to bind uevent socket\n");
        close(fd);
        return -1;
    }

    return fd;
}

static int ParseHdmiTxHpdEvent(const char *buf, int len)
{
    const char *ptr = buf;
    const char *end = buf + len;

    while (ptr < end) {
        if (strncmp(ptr, "hdmitx_hpd=", 11) == 0) {
            int state = atoi(ptr + 11);
            LOGD("Received HDMI TX HPD event: hdmitx_hpd=%d\n", state);
            return state;
        }
        ptr += strlen(ptr) + 1;
    }
    return -1;
}

/* --- TV Processing --- */

static void StartTvProcessing(struct TvClientWrapper_t *pTvClientWrapper)
{
    const video_config_t *video = &config_get()->video;
    tv_source_input_t source;

    if (g_tv_started) {
        LOGD("%s: TV already started\n", __FUNCTION__);
        return;
    }

    LOGD("%s: Starting TV processing\n", __FUNCTION__);
    LOGD("%s: game_mode=%d, vrr_mode=%d, hdmi_source=%s\n",
         __FUNCTION__, video->game_mode, video->vrr_mode, video->hdmi_source);

#ifdef STREAM_BOX
    if (video->game_mode > 0) {
        int ret;

        /* Enable ALLM (Auto Low Latency Mode) - always enabled */
        ret = SetHdmiAllmEnabled(pTvClientWrapper, 1);
        if (ret == 0) {
            LOGD("ALLM enabled successfully\n");
        } else {
            LOGD("Failed to enable ALLM, ret=%d\n", ret);
        }

        /* Enable VRR (Variable Refresh Rate) */
        ret = SetHdmiVrrEnabled(pTvClientWrapper, 1);
        if (ret == 0) {
            LOGD("VRR enabled successfully\n");
        } else {
            LOGD("Failed to enable VRR, ret=%d\n", ret);
        }

        /* Set Game Mode */
        int game_mode_value = (video->game_mode == 2) ? 3 : 1;
        ret = SetGameMode(pTvClientWrapper, 1, game_mode_value);
        if (ret == 0) {
            LOGD("Game Mode enabled successfully (game_mode_value=%d)\n", game_mode_value);
        } else {
            LOGD("Failed to enable Game Mode, ret=%d\n", ret);
        }
    }
#endif

    source = (tv_source_input_t)config_get_source_input(video->hdmi_source);
    StartTv(pTvClientWrapper, source);
    g_tv_started = 1;
    LOGD("%s: TV processing started\n", __FUNCTION__);
}

static void StopTvProcessing(struct TvClientWrapper_t *pTvClientWrapper)
{
    const video_config_t *video = &config_get()->video;
    tv_source_input_t source;

    StopAudioPassthrough();

    if (!g_tv_started) {
        LOGD("%s: TV already stopped\n", __FUNCTION__);
        return;
    }

    LOGD("%s: Stopping TV processing\n", __FUNCTION__);

    /* Disable VRR */
    if (video->game_mode == 2) {
        WriteSysfs(VRR_DEBUG_PATH, "en 0");
    }

    source = (tv_source_input_t)config_get_source_input(video->hdmi_source);
    StopTv(pTvClientWrapper, source);
    g_tv_started = 0;
    LOGD("%s: TV processing stopped\n", __FUNCTION__);
}

/* --- Uevent Monitor Thread --- */

static void *UeventMonitorThread(void *arg)
{
    struct TvClientWrapper_t *pTvClientWrapper = (struct TvClientWrapper_t *)arg;
    char buf[4096];
    struct pollfd pfd;

    pfd.fd = g_uevent_fd;
    pfd.events = POLLIN;

    LOGD("Uevent monitor thread started\n");

    while (run) {
        int ret = poll(&pfd, 1, 1000);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            int len = recv(g_uevent_fd, buf, sizeof(buf) - 1, 0);
            if (len > 0) {
                buf[len] = '\0';
                int hpd_state = ParseHdmiTxHpdEvent(buf, len);
                if (hpd_state >= 0 && hpd_state != g_hdmitx_connected) {
                    g_hdmitx_connected = hpd_state;
                    if (hpd_state == 1) {
                        LOGD("HDMI TX connected (event), starting TV\n");
                        StartTvProcessing(pTvClientWrapper);
                    } else {
                        LOGD("HDMI TX disconnected (event), stopping TV\n");
                        StopTvProcessing(pTvClientWrapper);
                    }
                }
            }
        }
    }

    LOGD("Uevent monitor thread exiting\n");
    return NULL;
}

/* --- Framerate Detection --- */

static int GetVdinInputRate(float *input_rate_hz)
{
    const char *vdin_input_rate_path = "/sys/class/vdin/vdin0/input_rate";
    char buf[64] = {0};
    int ret;
    int int_part, frac_part;
    float rate;

    if (input_rate_hz == NULL) {
        return -1;
    }

    ret = ReadSysfs(vdin_input_rate_path, buf, sizeof(buf));
    if (ret != 0) {
        LOGD("%s: Failed to read input_rate from %s\n", __FUNCTION__, vdin_input_rate_path);
        return -1;
    }

    ret = sscanf(buf, "%d.%d", &int_part, &frac_part);
    if (ret != 2) {
        LOGD("%s: Failed to parse input_rate: %s\n", __FUNCTION__, buf);
        return -1;
    }

    rate = (float)int_part + (float)frac_part / 1000.0f;
    *input_rate_hz = rate;

    LOGD("%s: Read input_rate: %s -> %.3f Hz\n", __FUNCTION__, buf, rate);
    return 0;
}

static int DetermineFracRatePolicy(int rounded_fps, float actual_input_rate_hz)
{
    int frac_rate_policy = 0;

    switch (rounded_fps) {
    case 120:
        if (actual_input_rate_hz <= 119.88f) {
            frac_rate_policy = 1;
        }
        break;
    case 60:
        if (actual_input_rate_hz <= 59.94f) {
            frac_rate_policy = 1;
        }
        break;
    case 30:
        if (actual_input_rate_hz <= 29.97f) {
            frac_rate_policy = 1;
        }
        break;
    case 24:
        if (actual_input_rate_hz <= 23.976f) {
            frac_rate_policy = 1;
        }
        break;
    default:
        break;
    }

    return frac_rate_policy;
}

static int FormatHdmitxMode(int width, int height, int fps, char *mode_str, size_t mode_str_size, int fine_tune)
{
    if (mode_str == NULL || mode_str_size < 32) {
        return -1;
    }

    int rounded_fps = fps;

    if (fine_tune) {
        if (fps >= 59 && fps <= 60) rounded_fps = 60;
        else if (fps >= 29 && fps <= 30) rounded_fps = 30;
        else if (fps >= 23 && fps <= 24) rounded_fps = 24;
        else if (fps >= 119 && fps <= 120) rounded_fps = 120;
        else {
            rounded_fps = (fps + 1) / 2 * 2;
            if (rounded_fps < fps) rounded_fps = fps;
        }
    } else {
        if (fps >= 50 && fps < 60) rounded_fps = 60;
        else if (fps >= 100 && fps < 120) rounded_fps = 120;
        else if (fps >= 24 && fps < 30) rounded_fps = 30;
    }

    snprintf(mode_str, mode_str_size, "%dx%dp%dhz", width, height, rounded_fps);
    return 0;
}

/* --- HDMI TX/RX Synchronization --- */


static void SynchronizeHdmitxToHdmirx(struct TvClientWrapper_t *pTvClientWrapper)
{
    const video_config_t *video = &config_get()->video;
    int ret;

    if (pTvClientWrapper == NULL) {
        return;
    }

    TRACE(2, "SynchronizeHdmitxToHdmirx() ENTRY\n");

    usleep(100000);

    int rx_width = GetCurrentSourceFrameWidth(pTvClientWrapper);
    int rx_height = GetCurrentSourceFrameHeight(pTvClientWrapper);
    int rx_fps = GetCurrentSourceFrameFps(pTvClientWrapper);

    if (rx_width <= 0 || rx_height <= 0 || rx_fps <= 0) {
        LOGD("%s: Invalid hdmirx parameters: %dx%d@%dHz, skipping sync\n", 
             __FUNCTION__, rx_width, rx_height, rx_fps);
        return;
    }

    LOGD("%s: HDMIRX input: %dx%d@%dHz\n", __FUNCTION__, rx_width, rx_height, rx_fps);

    char rx_colorspace[64] = {0};
    char rx_hdr_status[64] = {0};
    char tx_attr[64] = {0};
    const char *tx_hdr_cmd = "sdr";
    const char *bit_depth_str = "8bit";
    int rx_color_depth = 8;
    int is_hdr = 0;

    ret = ReadSysfs(HDMI_RX_COLORSPACE_PATH, rx_colorspace, sizeof(rx_colorspace));
    if (ret != 0) {
        LOGD("%s: Failed to read RX colorspace, using default\n", __FUNCTION__);
        strcpy(rx_colorspace, "422");
    }
    LOGD("%s: RX colorspace: %s\n", __FUNCTION__, rx_colorspace);

    if (g_pTvClientWrapper != NULL) {
        rx_color_depth = GetCurrentSourceColorDepth(g_pTvClientWrapper);
        if (rx_color_depth <= 0) {
            LOGD("%s: Failed to get RX color depth, using default 8-bit\n", __FUNCTION__);
            rx_color_depth = 8;
        }
    }
    LOGD("%s: RX color depth: %d bits\n", __FUNCTION__, rx_color_depth);

    ret = ReadSysfs(HDMI_RX_HDR_STATUS_PATH, rx_hdr_status, sizeof(rx_hdr_status));
    if (ret != 0) {
        LOGD("%s: Failed to read RX HDR status, assuming SDR\n", __FUNCTION__);
        strcpy(rx_hdr_status, "SDR");
    }
    LOGD("%s: RX HDR status: %s\n", __FUNCTION__, rx_hdr_status);

    if (strstr(rx_hdr_status, "HDR10Plus") != NULL || strstr(rx_hdr_status, "HDR10+") != NULL) {
        tx_hdr_cmd = "hdr10+";
        is_hdr = 1;
    } else if (strstr(rx_hdr_status, "HLG") != NULL) {
        tx_hdr_cmd = "hlg";
        is_hdr = 1;
    } else if (strstr(rx_hdr_status, "DolbyVision-Lowlatency") != NULL) {
        tx_hdr_cmd = "vsif41";
        is_hdr = 1;
    } else if (strstr(rx_hdr_status, "DolbyVision-Std") != NULL) {
        tx_hdr_cmd = "vsif11";
        is_hdr = 1;
    } else if (strstr(rx_hdr_status, "DolbyVision") != NULL) {
        tx_hdr_cmd = "vsif41";
        is_hdr = 1;
    } else if (strstr(rx_hdr_status, "HDR10") != NULL || strstr(rx_hdr_status, "SMPTE") != NULL) {
        tx_hdr_cmd = "hdr";
        is_hdr = 1;
    } else {
        tx_hdr_cmd = "sdr";
        is_hdr = 0;
    }

    if (rx_color_depth >= 12) {
        bit_depth_str = "12bit";
    } else if (rx_color_depth >= 10 || is_hdr) {
        bit_depth_str = "10bit";
    } else {
        bit_depth_str = "8bit";
    }

    if (strstr(rx_colorspace, "422") != NULL) {
        snprintf(tx_attr, sizeof(tx_attr), "422,%s", bit_depth_str);
    } else if (strstr(rx_colorspace, "444") != NULL) {
        snprintf(tx_attr, sizeof(tx_attr), "444,%s", bit_depth_str);
    } else if (strstr(rx_colorspace, "420") != NULL) {
        snprintf(tx_attr, sizeof(tx_attr), "420,%s", bit_depth_str);
    } else if (strstr(rx_colorspace, "rgb") != NULL || strstr(rx_colorspace, "RGB") != NULL) {
        snprintf(tx_attr, sizeof(tx_attr), "rgb,%s", bit_depth_str);
    } else {
        LOGD("%s: Unknown colorspace '%s', using rgb,%s\n", __FUNCTION__, rx_colorspace, bit_depth_str);
        snprintf(tx_attr, sizeof(tx_attr), "rgb,%s", bit_depth_str);
    }

    int fine_tune_framerate = 0;
#ifdef STREAM_BOX
    int vrr_enabled = GetHdmiVrrEnabled(pTvClientWrapper);
    int vrr_supported = (vrr_enabled != 0);

    if (!vrr_supported) {
        fine_tune_framerate = 1;
    }
#endif

    int rounded_fps = rx_fps;
    if (fine_tune_framerate) {
        if (rx_fps >= 59 && rx_fps <= 60) rounded_fps = 60;
        else if (rx_fps >= 29 && rx_fps <= 30) rounded_fps = 30;
        else if (rx_fps >= 23 && rx_fps <= 24) rounded_fps = 24;
        else if (rx_fps >= 119 && rx_fps <= 120) rounded_fps = 120;
        else {
            rounded_fps = (rx_fps + 1) / 2 * 2;
            if (rounded_fps < rx_fps) rounded_fps = rx_fps;
        }
    } else {
        if (rx_fps >= 50 && rx_fps < 60) rounded_fps = 60;
        else if (rx_fps >= 100 && rx_fps < 120) rounded_fps = 120;
        else if (rx_fps >= 24 && rx_fps < 30) rounded_fps = 30;
    }

    char mode_str[64] = {0};
    ret = FormatHdmitxMode(rx_width, rx_height, rx_fps, mode_str, sizeof(mode_str), fine_tune_framerate);
    if (ret != 0) {
        return;
    }

    float actual_input_rate_hz = 0.0f;
    int frac_rate_policy = 0;
    ret = GetVdinInputRate(&actual_input_rate_hz);
    if (ret == 0) {
        frac_rate_policy = DetermineFracRatePolicy(rounded_fps, actual_input_rate_hz);
    }

    const char *hdmitx_disp_mode_path = "/sys/class/amhdmitx/amhdmitx0/disp_mode";
    const char *hdmitx_frac_rate_policy_path = "/sys/class/amhdmitx/amhdmitx0/frac_rate_policy";

    char frac_policy_str[2] = {0};
    snprintf(frac_policy_str, sizeof(frac_policy_str), "%d", frac_rate_policy);
    WriteSysfs(hdmitx_frac_rate_policy_path, frac_policy_str);
    usleep(10000);

    WriteSysfs(hdmitx_disp_mode_path, "off");
    usleep(500000);

    LOGD("%s: Setting TX test_attr to: %s (must be set BEFORE disp_mode for 4K HDR)\n", __FUNCTION__, tx_attr);
    ret = WriteSysfs(HDMI_TX_TEST_ATTR_PATH, tx_attr);
    if (ret != 0) {
        LOGD("%s: Failed to set TX test_attr\n", __FUNCTION__);
    }
    usleep(10000);

    ret = WriteSysfs(hdmitx_disp_mode_path, mode_str);
    if (ret == 0) {
        LOGD("%s: Successfully set hdmitx mode to: %s\n", __FUNCTION__, mode_str);
        usleep(200000);

        LOGD("%s: Setting TX HDR config to: %s\n", __FUNCTION__, tx_hdr_cmd);
        ret = WriteSysfs(HDMI_TX_CONFIG_PATH, tx_hdr_cmd);
        if (ret != 0) {
            LOGD("%s: Failed to set TX HDR config\n", __FUNCTION__);
        }
        usleep(10000);

#ifdef STREAM_BOX
    ResetVideoPipeline();
    usleep(100000);
#endif
        if (video->game_mode == 2) {
            ApplyVrrMode(video->vrr_mode);
        }
    } else {
        LOGD("%s: Failed to set hdmitx mode to: %s\n", __FUNCTION__, mode_str);
    }

    TRACE(2, "SynchronizeHdmitxToHdmirx() EXIT\n");
}

static int DisplayInit(void)
{
    WriteSysfs("/sys/class/graphics/fb0/blank", "1");
    return 0;
}

/* --- Event Callback --- */

static void TvEventCallback(event_type_t eventType, void *eventData)
{
    if (eventType == TV_EVENT_TYPE_SIGLE_DETECT) {
        SignalDetectCallback_t *signalDetectEvent = (SignalDetectCallback_t *)(eventData);
        LOGD("%s: source: %d, signalFmt: %d, transFmt: %d, status: %d, isDVI: %d.\n", __FUNCTION__,
                                                   signalDetectEvent->SourceInput,
                                                   signalDetectEvent->SignalFmt,
                                                   signalDetectEvent->TransFmt,
                                                   signalDetectEvent->SignalStatus,
                                                   signalDetectEvent->isDviSignal);

        if (signalDetectEvent->SignalStatus == TVIN_SIG_STATUS_STABLE && g_pTvClientWrapper != NULL) {
            LOGD("%s: HDMIRX input change detected (stable signal), synchronizing HDMITX\n", __FUNCTION__);
            SynchronizeHdmitxToHdmirx(g_pTvClientWrapper);
            StartAudioPassthrough();
        } else if (signalDetectEvent->SignalStatus != TVIN_SIG_STATUS_STABLE) {
            LOGD("%s: HDMIRX signal not stable (status=%d), stopping audio passthrough\n",
                 __FUNCTION__, signalDetectEvent->SignalStatus);
            StopAudioPassthrough();
        }
    } else if (eventType == TV_EVENT_TYPE_SOURCE_CONNECT) {
        SourceConnectCallback_t *sourceConnectEvent = (SourceConnectCallback_t *)(eventData);
        LOGD("%s: source: %d, connectStatus: %d\n", __FUNCTION__,
                  sourceConnectEvent->SourceInput, sourceConnectEvent->ConnectionState);
    }
}

static void signal_handler(int s)
{
    run = 0;
    WriteSysfs("/sys/class/graphics/fb0/blank", "0");
    signal(s, SIG_DFL);
    raise(s);
}

/* --- Usage --- */

static void print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("\n");
    printf("HDMI input passthrough application for StreamBox with config file support.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -c, --config <path>       Config file path (default: %s)\n", CONFIG_FILE_PATH);
    printf("  -g, --game-mode <0|1|2>   Override game mode from config\n");
    printf("  -t, --trace <0-3>         Override trace level from config\n");
    printf("  -h, --help                Display this help and exit\n");
    printf("\n");
    printf("Config file is reloaded on SIGHUP or when file changes.\n");
}

/* --- Main --- */

int main(int argc, char **argv) {
    int opt;
    int option_index = 0;
    const char *config_path = NULL;
    int override_game_mode = -1;
    int override_trace_level = -1;

    static struct option long_options[] = {
        {"config",    required_argument, 0, 'c'},
        {"game-mode", required_argument, 0, 'g'},
        {"trace",     required_argument, 0, 't'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    /* Parse command line arguments */
    while ((opt = getopt_long(argc, argv, "c:g:t:h", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'c':
            config_path = optarg;
            break;
        case 'g':
            override_game_mode = atoi(optarg);
            if (override_game_mode < 0 || override_game_mode > 2) {
                fprintf(stderr, "Error: Invalid game mode '%s'. Must be 0, 1, or 2.\n", optarg);
                print_usage(argv[0]);
                return 1;
            }
            break;
        case 't':
            override_trace_level = atoi(optarg);
            if (override_trace_level < 0 || override_trace_level > 3) {
                fprintf(stderr, "Error: Invalid trace level '%s'. Must be 0-3.\n", optarg);
                print_usage(argv[0]);
                return 1;
            }
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Initialize config system */
    config_init();
    config_load(config_path);

    /* Register config change callback */
    config_register_callback(OnConfigChange);

    /* Start config file watcher */
    config_watch_start();

    /* Apply command line overrides (for backwards compatibility) */
    /* Note: These don't modify the config file, just runtime behavior */
    /* For now, we trust the config file - remove override support to keep it simple */

    const streambox_config_t *cfg = config_get();
    printf("streambox-tv starting with config:\n");
    printf("  video: game_mode=%d, vrr_mode=%d, hdmi_source=%s\n",
           cfg->video.game_mode, cfg->video.vrr_mode, cfg->video.hdmi_source);
    printf("  audio: enabled=%d, capture=%s, playback=%s\n",
           cfg->audio.enabled, cfg->audio.capture_device, cfg->audio.playback_device);
    printf("  debug: trace_level=%d\n", cfg->debug.trace_level);

    TRACE(1, "main() ENTRY\n");

    /* Create uevent socket for HPD monitoring */
    g_uevent_fd = CreateUeventSocket();
    if (g_uevent_fd < 0) {
        LOGD("Warning: Failed to create uevent socket, will use polling fallback\n");
    }

    /* Check initial HDMI TX state */
    g_hdmitx_connected = (GetHdmiTxHpdState() == 1);
    LOGD("Initial HDMI TX state: %s\n", g_hdmitx_connected ? "connected" : "disconnected");

    /* Wait for HDMI TX connection */
    if (!g_hdmitx_connected) {
        LOGD("Waiting for HDMI TX connection...\n");
        while (run && !g_hdmitx_connected) {
            if (g_uevent_fd >= 0) {
                struct pollfd pfd = { .fd = g_uevent_fd, .events = POLLIN };
                if (poll(&pfd, 1, 500) > 0 && (pfd.revents & POLLIN)) {
                    char buf[4096];
                    int len = recv(g_uevent_fd, buf, sizeof(buf) - 1, 0);
                    if (len > 0) {
                        buf[len] = '\0';
                        if (ParseHdmiTxHpdEvent(buf, len) == 1) {
                            g_hdmitx_connected = 1;
                        }
                    }
                }
            } else {
                usleep(500000);
                g_hdmitx_connected = (GetHdmiTxHpdState() == 1);
            }
        }
        if (!run) goto cleanup;
        LOGD("HDMI TX connected, proceeding with initialization\n");
    }

    /* Initialize TV client */
    TRACE(2, "About to call GetInstance()\n");
    struct TvClientWrapper_t *pTvClientWrapper = GetInstance();
    g_pTvClientWrapper = pTvClientWrapper;

    setTvEventCallback(TvEventCallback);
    
    tv_source_input_t source = (tv_source_input_t)config_get_source_input(cfg->video.hdmi_source);
    StopTv(pTvClientWrapper, source);
    sleep(1);

    DisplayInit();

    StartTvProcessing(pTvClientWrapper);

    /* Start uevent monitor thread */
    if (g_uevent_fd >= 0) {
        if (pthread_create(&g_uevent_thread, NULL, UeventMonitorThread, pTvClientWrapper) != 0) {
            LOGD("Failed to create uevent monitor thread\n");
        }
    }

    signal(SIGINT, signal_handler);

    TRACE(1, "Entering main loop\n");
    while (run) {
        sleep(1);
    }

cleanup:
    TRACE(1, "main() cleanup\n");

    /* Stop config watcher */
    config_watch_stop();

    /* Wait for uevent monitor thread */
    if (g_uevent_fd >= 0) {
        pthread_join(g_uevent_thread, NULL);
        close(g_uevent_fd);
        g_uevent_fd = -1;
    }

    /* Stop TV */
    if (g_tv_started && g_pTvClientWrapper) {
        StopTvProcessing(g_pTvClientWrapper);
    }

    /* Cleanup config */
    config_cleanup();

    TRACE(1, "main() EXIT\n");
    return 0;
}
