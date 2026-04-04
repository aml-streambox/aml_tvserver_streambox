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
#include <time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <stdint.h>
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
#define HDMI_TX_DISP_MODE_PATH "/sys/class/amhdmitx/amhdmitx0/disp_mode"

/* DRM CRTC mode sysfs path — writing triggers a full DRM atomic modeset */
#define DRM_CRTC0_MODE_PATH    "/sys/class/drm/card0/crtc0/mode"

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

/* No-signal UI state */
static int g_force_no_signal_ui = 0;
static int g_no_signal_ui_active = 0;
static int g_no_signal_thread_created = 0;
static pthread_t g_no_signal_thread;
static pthread_mutex_t g_no_signal_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_no_signal_renderer_mode = 1; /* 0=fb0 fallback, 1=fb1 dedicated osd */
static const char *g_no_signal_fb_device = "/dev/fb0";
static const char *g_no_signal_fb_sysfs = "/sys/class/graphics/fb0";

typedef struct no_signal_fb_s {
    int fd;
    unsigned char *map;
    size_t map_size;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
    struct fb_var_screeninfo original_vinfo;
    int width;
    int height;
    int stride;
    int bytes_per_pixel;
    int page_count;
    int current_page;
} no_signal_fb_t;

typedef struct no_signal_surface_s {
    unsigned char *buf;
    size_t size;
    int width;
    int height;
    int stride;
    int bytes_per_pixel;
} no_signal_surface_t;

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
static void UpdateNoSignalUiFromCurrentSource(struct TvClientWrapper_t *pTvClientWrapper, const char *reason);
static void ResetFb0AfterTxModeChange(void);

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

static void WriteSysfsIfExists(const char *path, const char *cmd)
{
    if (access(path, F_OK) == 0) {
        WriteSysfs(path, cmd);
    }
}

static void SetConsoleCursorVisible(int visible)
{
    int fd;
    const char *seq = visible ? "\033[?25h" : "\033[?25l";

    fd = open("/dev/tty0", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        fd = open("/dev/console", O_WRONLY | O_CLOEXEC);
    }
    if (fd < 0) {
        return;
    }

    write(fd, seq, strlen(seq));
    close(fd);
}

static const char *NoSignalFbBlankPath(void)
{
    if (strcmp(g_no_signal_fb_sysfs, "/sys/class/graphics/fb1") == 0) {
        return "/sys/class/graphics/fb1/blank";
    }
    return "/sys/class/graphics/fb0/blank";
}

static void ResetNoSignalFramebuffer(const no_signal_fb_t *fb)
{
    if (fb == NULL || fb->map == NULL) {
        return;
    }

    WriteSysfs(NoSignalFbBlankPath(), "1");
    memset(fb->map, 0, fb->map_size);
    WriteSysfs(NoSignalFbBlankPath(), "0");
}

static unsigned char *GetNoSignalFramebufferPage(no_signal_fb_t *fb, int page)
{
    if (page < 0 || page >= fb->page_count) {
        return fb->map;
    }
    return fb->map + (size_t)page * (size_t)fb->stride * (size_t)fb->height;
}

static void PresentNoSignalFramebufferPage(no_signal_fb_t *fb, int page)
{
    struct fb_var_screeninfo pan = fb->vinfo;

    if (page < 0 || page >= fb->page_count) {
        page = 0;
    }

    pan.xoffset = 0;
    pan.yoffset = (uint32_t)(page * fb->height);
    ioctl(fb->fd, FBIOPAN_DISPLAY, &pan);
    fb->current_page = page;
}

static int GetCurrentHdmiTxResolution(int *width, int *height)
{
    FILE *fp;
    char line[128];

    if (!width || !height) {
        return -1;
    }

    fp = fopen(HDMI_TX_DISP_MODE_PATH, "r");
    if (!fp) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (sscanf(line, "width/height: %d/%d", width, height) == 2) {
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return -1;
}

/*
 * SyncDrmCrtcMode - Update the DRM CRTC mode to match the actual HDMI TX output.
 *
 * When SynchronizeHdmitxToHdmirx() sets the HDMI TX mode via amhdmitx sysfs,
 * the DRM CRTC mode is NOT automatically updated (the flow is unidirectional:
 * DRM -> hdmitx, never hdmitx -> DRM). This causes the OSD plane destination
 * rectangle to use the stale CRTC mode dimensions, resulting in the no-signal
 * UI rendering in the top-left corner instead of filling the full screen.
 *
 * This function reads the current TX mode's short name (sname) from disp_mode
 * and writes it to the DRM crtc0/mode sysfs node to trigger a full DRM atomic
 * modeset. If the sname doesn't match a DRM connector mode, it falls back to
 * the long WIDTHxHEIGHTpFPShz format.
 *
 * @param mode_str  The long-format mode string (e.g. "3840x2160p60hz") that
 *                  was written to amhdmitx disp_mode. Used as fallback.
 */
static void SyncDrmCrtcMode(const char *mode_str)
{
    char sname[64] = {0};
    char crtc_mode_before[64] = {0};
    char crtc_mode_after[64] = {0};
    FILE *fp;
    char line[128];
    int fd;
    int attempt;

    /* Read current DRM CRTC mode for comparison */
    ReadSysfs(DRM_CRTC0_MODE_PATH, crtc_mode_before, sizeof(crtc_mode_before));

    /* Extract sname from TX disp_mode — this is the DRM-compatible mode name */
    fp = fopen(HDMI_TX_DISP_MODE_PATH, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (sscanf(line, "sname: %63s", sname) == 1) {
                break;
            }
        }
        fclose(fp);
    }

    if (sname[0] == '\0') {
        LOGE("%s: could not read sname from disp_mode, skipping DRM CRTC sync\n",
             __FUNCTION__);
        return;
    }

    LOGE("%s: TX sname='%s', DRM CRTC mode='%s'\n",
         __FUNCTION__, sname, crtc_mode_before);

    /* If DRM CRTC mode already matches, nothing to do */
    if (strcmp(sname, crtc_mode_before) == 0) {
        LOGE("%s: DRM CRTC mode already matches TX mode, no sync needed\n",
             __FUNCTION__);
        return;
    }

    /*
     * Try writing the mode with retries. The DRM crtc_mode_store calls
     * fill_modes() to enumerate connector modes from HDMI TX — if the TX
     * link isn't fully established yet, the target mode won't be found.
     * Use O_WRONLY (not O_CREAT|O_TRUNC) for sysfs bin_attribute files.
     */
    const char *candidates[2] = { sname, mode_str };
    int num_candidates = (mode_str && mode_str[0] && strcmp(mode_str, sname) != 0) ? 2 : 1;

    for (attempt = 0; attempt < 3; attempt++) {
        int c;
        for (c = 0; c < num_candidates; c++) {
            LOGE("%s: attempt %d: writing '%s' to DRM CRTC\n",
                 __FUNCTION__, attempt + 1, candidates[c]);

            fd = open(DRM_CRTC0_MODE_PATH, O_WRONLY);
            if (fd < 0) {
                LOGE("%s: failed to open %s: %s\n",
                     __FUNCTION__, DRM_CRTC0_MODE_PATH, strerror(errno));
                continue;
            }
            if (write(fd, candidates[c], strlen(candidates[c])) < 0) {
                LOGE("%s: write failed: %s\n", __FUNCTION__, strerror(errno));
            }
            close(fd);
            usleep(200000);

            ReadSysfs(DRM_CRTC0_MODE_PATH, crtc_mode_after, sizeof(crtc_mode_after));
            if (strcmp(crtc_mode_after, crtc_mode_before) != 0) {
                LOGE("%s: DRM CRTC mode updated to '%s' (attempt %d, candidate '%s')\n",
                     __FUNCTION__, crtc_mode_after, attempt + 1, candidates[c]);
                return;
            }
        }

        /* Wait before retry — give TX more time to stabilize */
        LOGE("%s: attempt %d failed, waiting 500ms before retry...\n",
             __FUNCTION__, attempt + 1);
        usleep(500000);
    }

    LOGE("%s: WARNING: failed to update DRM CRTC mode after %d attempts (still '%s')\n",
         __FUNCTION__, 3, crtc_mode_after);
}

static void ResetFb0AfterTxModeChange(void)
{
    int fd;
    int width = 0;
    int height = 0;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    unsigned char *map = NULL;
    size_t map_size = 0;

    if (GetCurrentHdmiTxResolution(&width, &height) != 0 || width <= 0 || height <= 0) {
        LOGE("%s: unable to read current TX resolution, skipping fb0 reset\n", __FUNCTION__);
        return;
    }

    fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        LOGE("%s: failed to open /dev/fb0 (%s)\n", __FUNCTION__, strerror(errno));
        return;
    }

    WriteSysfsIfExists("/sys/class/graphics/fb0/blank", "1");
    WriteSysfsIfExists("/sys/class/graphics/fb0/free_scale", "0");
    WriteSysfsIfExists("/sys/class/graphics/fb0/scale", "0");
    WriteSysfsIfExists("/sys/class/graphics/fb0/osd_display_debug", "0");

    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        LOGE("%s: failed to query fb0 info (%s)\n", __FUNCTION__, strerror(errno));
        goto done;
    }

    LOGE("%s: BEFORE reset - var: xres=%u yres=%u xres_virtual=%u yres_virtual=%u "
         "xoffset=%u yoffset=%u bits_per_pixel=%u\n",
         __FUNCTION__, var.xres, var.yres, var.xres_virtual, var.yres_virtual,
         var.xoffset, var.yoffset, var.bits_per_pixel);
    LOGE("%s: BEFORE reset - fix: line_length=%u smem_len=%u smem_start=0x%lx type=%u "
         "visual=%u\n",
         __FUNCTION__, fix.line_length, fix.smem_len,
         (unsigned long)fix.smem_start, fix.type, fix.visual);

    {
        char sysfs_buf[128] = {0};
        if (ReadSysfs("/sys/class/graphics/fb0/stride", sysfs_buf, sizeof(sysfs_buf)) == 0)
            LOGE("%s: BEFORE reset - sysfs stride: %s\n", __FUNCTION__, sysfs_buf);
        memset(sysfs_buf, 0, sizeof(sysfs_buf));
        if (ReadSysfs("/sys/class/graphics/fb0/virtual_size", sysfs_buf, sizeof(sysfs_buf)) == 0)
            LOGE("%s: BEFORE reset - sysfs virtual_size: %s\n", __FUNCTION__, sysfs_buf);
    }

    var.xres = (uint32_t)width;
    var.yres = (uint32_t)height;
    var.xres_virtual = (uint32_t)width;
    var.yres_virtual = (uint32_t)height;
    var.xoffset = 0;
    var.yoffset = 0;
    var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    LOGE("%s: requesting fb0 resize to %dx%d (bpp=%u)\n", __FUNCTION__, width, height, var.bits_per_pixel);

    if (ioctl(fd, FBIOPUT_VSCREENINFO, &var) < 0) {
        LOGE("%s: FBIOPUT_VSCREENINFO failed (%s)\n", __FUNCTION__, strerror(errno));
        goto done;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        LOGE("%s: failed to refresh fb0 info after set (%s)\n", __FUNCTION__, strerror(errno));
        goto done;
    }

    LOGE("%s: AFTER reset - var: xres=%u yres=%u xres_virtual=%u yres_virtual=%u "
         "xoffset=%u yoffset=%u bits_per_pixel=%u\n",
         __FUNCTION__, var.xres, var.yres, var.xres_virtual, var.yres_virtual,
         var.xoffset, var.yoffset, var.bits_per_pixel);
    LOGE("%s: AFTER reset - fix: line_length=%u smem_len=%u smem_start=0x%lx type=%u "
         "visual=%u\n",
         __FUNCTION__, fix.line_length, fix.smem_len,
         (unsigned long)fix.smem_start, fix.type, fix.visual);
    LOGE("%s: AFTER reset - expected_stride=%u (xres*bpp/8=%u*%u/8=%u) actual_line_length=%u %s\n",
         __FUNCTION__,
         var.xres * var.bits_per_pixel / 8,
         var.xres, var.bits_per_pixel, var.xres * var.bits_per_pixel / 8,
         fix.line_length,
         (fix.line_length != var.xres * var.bits_per_pixel / 8) ? "*** STRIDE MISMATCH ***" : "OK");

    {
        char sysfs_buf[128] = {0};
        if (ReadSysfs("/sys/class/graphics/fb0/stride", sysfs_buf, sizeof(sysfs_buf)) == 0)
            LOGE("%s: AFTER reset - sysfs stride: %s\n", __FUNCTION__, sysfs_buf);
        memset(sysfs_buf, 0, sizeof(sysfs_buf));
        if (ReadSysfs("/sys/class/graphics/fb0/virtual_size", sysfs_buf, sizeof(sysfs_buf)) == 0)
            LOGE("%s: AFTER reset - sysfs virtual_size: %s\n", __FUNCTION__, sysfs_buf);
    }

    var.xoffset = 0;
    var.yoffset = 0;
    ioctl(fd, FBIOPAN_DISPLAY, &var);

    map_size = (size_t)fix.line_length * (size_t)var.yres_virtual;
    map = (unsigned char *)mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map != MAP_FAILED) {
        memset(map, 0, map_size);
        munmap(map, map_size);
        map = NULL;
    }

    WriteSysfsIfExists("/sys/class/graphics/fb0/blank", "0");
    LOGE("%s: reset fb0 to %dx%d stride=%d (expected=%d)\n", __FUNCTION__,
         width, height, fix.line_length, width * (int)(var.bits_per_pixel / 8));

done:
    if (map && map != MAP_FAILED) {
        munmap(map, map_size);
    }
    close(fd);
}

static int ScaleFromTxToFb(int value, int tx_base, int fb_base)
{
    if (tx_base <= 0 || fb_base <= 0) {
        return value;
    }
    return (value * fb_base + tx_base / 2) / tx_base;
}

/* --- Minimal No-Signal UI --- */

typedef struct glyph_s {
    char c;
    unsigned char rows[7];
} glyph_t;

static const glyph_t kNoSignalGlyphs[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}},
    {'I', {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
};

static void DrawText(const no_signal_fb_t *fb, int x, int y, const char *text, int scale, uint32_t color);
static int MeasureTextWidth(const char *text, int scale);
static int InitNoSignalSurface(no_signal_surface_t *surf, const no_signal_fb_t *fb, int width, int height);
static void FreeNoSignalSurface(no_signal_surface_t *surf);
static void DrawSurfaceText(const no_signal_surface_t *surf, int x, int y, const char *text, int scale, uint32_t color);
static void BlitSurfaceToFb(const no_signal_surface_t *surf, const no_signal_fb_t *fb, int dst_x, int dst_y);
static void *NoSignalUiThread(void *arg);

static const unsigned char *GetGlyphRows(char c)
{
    size_t i;

    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    for (i = 0; i < sizeof(kNoSignalGlyphs) / sizeof(kNoSignalGlyphs[0]); i++) {
        if (kNoSignalGlyphs[i].c == c) {
            return kNoSignalGlyphs[i].rows;
        }
    }

    return kNoSignalGlyphs[0].rows;
}

static uint32_t PackFbColor(const no_signal_fb_t *fb, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t value = 0;
    uint32_t rv = (uint32_t)((r * ((1u << fb->vinfo.red.length) - 1u)) / 255u);
    uint32_t gv = (uint32_t)((g * ((1u << fb->vinfo.green.length) - 1u)) / 255u);
    uint32_t bv = (uint32_t)((b * ((1u << fb->vinfo.blue.length) - 1u)) / 255u);

    value |= rv << fb->vinfo.red.offset;
    value |= gv << fb->vinfo.green.offset;
    value |= bv << fb->vinfo.blue.offset;
    if (fb->vinfo.transp.length > 0) {
        value |= ((1u << fb->vinfo.transp.length) - 1u) << fb->vinfo.transp.offset;
    }
    return value;
}

static int OpenNoSignalFramebuffer(no_signal_fb_t *fb)
{
    memset(fb, 0, sizeof(*fb));
    fb->fd = open(g_no_signal_fb_device, O_RDWR);
    if (fb->fd < 0) {
        LOGE("%s: failed to open %s (%s)\n", __FUNCTION__, g_no_signal_fb_device, strerror(errno));
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->finfo) < 0 ||
        ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vinfo) < 0) {
        LOGE("%s: failed to query fb info (%s)\n", __FUNCTION__, strerror(errno));
        close(fb->fd);
        fb->fd = -1;
        return -1;
    }

    fb->original_vinfo = fb->vinfo;

    fb->width = (int)fb->vinfo.xres;
    fb->height = (int)fb->vinfo.yres;
    fb->stride = (int)fb->finfo.line_length;
    fb->bytes_per_pixel = (int)(fb->vinfo.bits_per_pixel / 8);
    fb->map_size = (size_t)fb->stride * (size_t)fb->height;
    fb->page_count = fb->vinfo.yres_virtual / fb->vinfo.yres;
    if (fb->page_count < 1) {
        fb->page_count = 1;
    }
    fb->current_page = 0;

    /* Guard: never mmap beyond the physical fb memory. */
    if (fb->map_size > (size_t)fb->finfo.smem_len) {
        LOGE("%s: map_size %zu exceeds smem_len %u, clamping\n",
             __FUNCTION__, fb->map_size, fb->finfo.smem_len);
        fb->map_size = (size_t)fb->finfo.smem_len;
        fb->page_count = 1;
    }

    LOGE("%s: opened fb - xres=%u yres=%u xres_virtual=%u yres_virtual=%u bpp=%u "
         "line_length=%u smem_len=%u width=%d height=%d stride=%d map_size=%zu page_count=%d\n",
         __FUNCTION__, fb->vinfo.xres, fb->vinfo.yres,
         fb->vinfo.xres_virtual, fb->vinfo.yres_virtual, fb->vinfo.bits_per_pixel,
         fb->finfo.line_length, fb->finfo.smem_len,
         fb->width, fb->height, fb->stride, fb->map_size, fb->page_count);

    fb->map = (unsigned char *)mmap(NULL, fb->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->map == MAP_FAILED) {
        LOGE("%s: failed to mmap fb0 (%s)\n", __FUNCTION__, strerror(errno));
        close(fb->fd);
        fb->fd = -1;
        fb->map = NULL;
        return -1;
    }

    return 0;
}

static int RemapNoSignalFramebuffer(no_signal_fb_t *fb)
{
    if (fb->map != NULL && fb->map != MAP_FAILED) {
        munmap(fb->map, fb->map_size);
        fb->map = NULL;
    }

    fb->width = (int)fb->vinfo.xres;
    fb->height = (int)fb->vinfo.yres;
    fb->stride = (int)fb->finfo.line_length;
    fb->bytes_per_pixel = (int)(fb->vinfo.bits_per_pixel / 8);
    fb->map_size = (size_t)fb->stride * (size_t)fb->vinfo.yres_virtual;
    fb->page_count = fb->vinfo.yres_virtual / fb->vinfo.yres;
    if (fb->page_count < 1) {
        fb->page_count = 1;
    }

    /* Guard: never mmap beyond the physical fb memory. */
    if (fb->map_size > (size_t)fb->finfo.smem_len) {
        LOGE("%s: map_size %zu exceeds smem_len %u, clamping to single page\n",
             __FUNCTION__, fb->map_size, fb->finfo.smem_len);
        fb->map_size = (size_t)fb->stride * (size_t)fb->vinfo.yres;
        fb->page_count = 1;
        if (fb->map_size > (size_t)fb->finfo.smem_len) {
            LOGE("%s: even single page (%zu) exceeds smem_len (%u), cannot map\n",
                 __FUNCTION__, fb->map_size, fb->finfo.smem_len);
            return -1;
        }
    }
    fb->current_page = 0;

    fb->map = (unsigned char *)mmap(NULL, fb->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->map == MAP_FAILED) {
        LOGE("%s: failed to remap fb0 (%s)\n", __FUNCTION__, strerror(errno));
        fb->map = NULL;
        return -1;
    }

    return 0;
}

static int ResizeNoSignalFramebuffer(no_signal_fb_t *fb, int width, int height)
{
    struct fb_var_screeninfo desired;

    if (width <= 0 || height <= 0) {
        return -1;
    }

    desired = fb->original_vinfo;
    desired.xres = (uint32_t)width;
    desired.yres = (uint32_t)height;
    desired.xres_virtual = (uint32_t)width;
    /* Request double-buffered virtual height so we can draw on one page
     * while displaying the other.  Single-buffered rendering causes visible
     * flashing because memset(black) on the visible page is seen before the
     * new content is drawn.  Only request 2 pages if smem_len is large enough. */
    {
        size_t one_page = (size_t)width * 4 * (size_t)height;
        if (fb->finfo.smem_len >= one_page * 2) {
            desired.yres_virtual = (uint32_t)(height * 2);
        } else {
            desired.yres_virtual = (uint32_t)height;
        }
    }

    /* Fully reset OSD framebuffer state before applying the new mode. */
    WriteSysfs(NoSignalFbBlankPath(), "1");
    ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->original_vinfo);

    if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &desired) < 0) {
        LOGE("%s: failed to resize fb0 to %dx%d (%s)\n", __FUNCTION__, width, height, strerror(errno));
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->finfo) < 0 ||
        ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vinfo) < 0) {
        LOGE("%s: failed to refresh fb info after resize (%s)\n", __FUNCTION__, strerror(errno));
        return -1;
    }

    LOGE("%s: after resize - var: xres=%u yres=%u xres_virtual=%u yres_virtual=%u bpp=%u\n",
         __FUNCTION__, fb->vinfo.xres, fb->vinfo.yres,
         fb->vinfo.xres_virtual, fb->vinfo.yres_virtual, fb->vinfo.bits_per_pixel);
    LOGE("%s: after resize - fix: line_length=%u smem_len=%u\n",
         __FUNCTION__, fb->finfo.line_length, fb->finfo.smem_len);
    LOGE("%s: after resize - expected_stride=%u actual_line_length=%u %s\n",
         __FUNCTION__,
         fb->vinfo.xres * fb->vinfo.bits_per_pixel / 8,
         fb->finfo.line_length,
         (fb->finfo.line_length != fb->vinfo.xres * fb->vinfo.bits_per_pixel / 8)
             ? "*** STRIDE MISMATCH ***" : "OK");

    if (RemapNoSignalFramebuffer(fb) != 0) {
        return -1;
    }

    LOGE("%s: after remap - fb->width=%d fb->height=%d fb->stride=%d map_size=%zu page_count=%d\n",
         __FUNCTION__, fb->width, fb->height, fb->stride, fb->map_size, fb->page_count);

    memset(fb->map, 0, fb->map_size);
    fb->current_page = 0;
    {
        struct fb_var_screeninfo pan = fb->vinfo;
        pan.xoffset = 0;
        pan.yoffset = 0;
        ioctl(fb->fd, FBIOPAN_DISPLAY, &pan);
    }
    WriteSysfs(NoSignalFbBlankPath(), "0");
    return 0;
}

static void CloseNoSignalFramebuffer(no_signal_fb_t *fb)
{
    if (fb->map != NULL && fb->map != MAP_FAILED) {
        munmap(fb->map, fb->map_size);
        fb->map = NULL;
    }

    if (fb->fd >= 0) {
        ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->original_vinfo);
    }

    if (fb->fd >= 0) {
        close(fb->fd);
        fb->fd = -1;
    }
}

static void PutFbPixel(const no_signal_fb_t *fb, int x, int y, uint32_t color)
{
    unsigned char *p;

    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) {
        return;
    }

    p = fb->map + (size_t)y * (size_t)fb->stride + (size_t)x * (size_t)fb->bytes_per_pixel;
    switch (fb->bytes_per_pixel) {
    case 4:
        *(uint32_t *)p = color;
        break;
    case 3:
        p[0] = (unsigned char)(color & 0xff);
        p[1] = (unsigned char)((color >> 8) & 0xff);
        p[2] = (unsigned char)((color >> 16) & 0xff);
        break;
    case 2:
        *(uint16_t *)p = (uint16_t)color;
        break;
    default:
        *p = (unsigned char)color;
        break;
    }
}

static void FillFbRect(const no_signal_fb_t *fb, int x, int y, int w, int h, uint32_t color)
{
    int xx;
    int yy;

    if (w <= 0 || h <= 0) {
        return;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > fb->width) {
        w = fb->width - x;
    }
    if (y + h > fb->height) {
        h = fb->height - y;
    }

    for (yy = y; yy < y + h; yy++) {
        for (xx = x; xx < x + w; xx++) {
            PutFbPixel(fb, xx, yy, color);
        }
    }
}

static int InitNoSignalSurface(no_signal_surface_t *surf, const no_signal_fb_t *fb, int width, int height)
{
    memset(surf, 0, sizeof(*surf));
    surf->width = width;
    surf->height = height;
    surf->bytes_per_pixel = fb->bytes_per_pixel;
    surf->stride = width * surf->bytes_per_pixel;
    surf->size = (size_t)surf->stride * (size_t)height;
    surf->buf = (unsigned char *)malloc(surf->size);
    if (surf->buf == NULL) {
        return -1;
    }
    memset(surf->buf, 0, surf->size);
    return 0;
}

static void FreeNoSignalSurface(no_signal_surface_t *surf)
{
    free(surf->buf);
    memset(surf, 0, sizeof(*surf));
}

static void PutSurfacePixel(const no_signal_surface_t *surf, int x, int y, uint32_t color)
{
    unsigned char *p;

    if (x < 0 || y < 0 || x >= surf->width || y >= surf->height) {
        return;
    }

    p = surf->buf + (size_t)y * (size_t)surf->stride + (size_t)x * (size_t)surf->bytes_per_pixel;
    switch (surf->bytes_per_pixel) {
    case 4:
        *(uint32_t *)p = color;
        break;
    case 3:
        p[0] = (unsigned char)(color & 0xff);
        p[1] = (unsigned char)((color >> 8) & 0xff);
        p[2] = (unsigned char)((color >> 16) & 0xff);
        break;
    case 2:
        *(uint16_t *)p = (uint16_t)color;
        break;
    default:
        *p = (unsigned char)color;
        break;
    }
}

static void FillSurfaceRect(const no_signal_surface_t *surf, int x, int y, int w, int h, uint32_t color)
{
    int xx;
    int yy;

    for (yy = y; yy < y + h; yy++) {
        for (xx = x; xx < x + w; xx++) {
            PutSurfacePixel(surf, xx, yy, color);
        }
    }
}

static void BlitSurfaceToFb(const no_signal_surface_t *surf, const no_signal_fb_t *fb, int dst_x, int dst_y)
{
    int row;

    if (dst_x < 0 || dst_y < 0 || dst_x + surf->width > fb->width || dst_y + surf->height > fb->height) {
        return;
    }

    for (row = 0; row < surf->height; row++) {
        memcpy(fb->map + (size_t)(dst_y + row) * (size_t)fb->stride + (size_t)dst_x * (size_t)fb->bytes_per_pixel,
               surf->buf + (size_t)row * (size_t)surf->stride,
               (size_t)surf->stride);
    }
}

static void BlitSurfaceToSurface(const no_signal_surface_t *src, const no_signal_surface_t *dst, int dst_x, int dst_y)
{
    int row;

    if (dst_x < 0 || dst_y < 0 || dst_x + src->width > dst->width || dst_y + src->height > dst->height) {
        return;
    }

    for (row = 0; row < src->height; row++) {
        memcpy(dst->buf + (size_t)(dst_y + row) * (size_t)dst->stride + (size_t)dst_x * (size_t)dst->bytes_per_pixel,
               src->buf + (size_t)row * (size_t)src->stride,
               (size_t)src->stride);
    }
}

static void DrawLogoBox(const no_signal_fb_t *fb, int x, int y, int box_w, int box_h,
                        int logo_scale, const char *logo, uint32_t border_color,
                        uint32_t inner_color, uint32_t text_color)
{
    int logo_x;
    int logo_y;

    FillFbRect(fb, x, y, box_w, box_h, border_color);
    FillFbRect(fb, x + 5, y + 5, box_w - 10, box_h - 10, inner_color);

    logo_x = x + (box_w - MeasureTextWidth(logo, logo_scale)) / 2;
    logo_y = y + (box_h - 7 * logo_scale) / 2;
    DrawText(fb, logo_x, logo_y, logo, logo_scale, text_color);
}

static void DrawGlyph(const no_signal_fb_t *fb, int x, int y, char c, int scale, uint32_t color)
{
    const unsigned char *rows = GetGlyphRows(c);
    int row;
    int col;
    int dx;
    int dy;

    for (row = 0; row < 7; row++) {
        for (col = 0; col < 5; col++) {
            if ((rows[row] >> (4 - col)) & 0x1) {
                for (dy = 0; dy < scale; dy++) {
                    for (dx = 0; dx < scale; dx++) {
                        PutFbPixel(fb, x + col * scale + dx, y + row * scale + dy, color);
                    }
                }
            }
        }
    }
}

static void DrawText(const no_signal_fb_t *fb, int x, int y, const char *text, int scale, uint32_t color)
{
    int i;
    int advance = 6 * scale;
    for (i = 0; text[i] != '\0'; i++) {
        DrawGlyph(fb, x + i * advance, y, text[i], scale, color);
    }
}

static int MeasureTextWidth(const char *text, int scale)
{
    return (int)strlen(text) * 6 * scale;
}

static void DrawSurfaceGlyph(const no_signal_surface_t *surf, int x, int y, char c, int scale, uint32_t color)
{
    const unsigned char *rows = GetGlyphRows(c);
    int row;
    int col;
    int dx;
    int dy;

    for (row = 0; row < 7; row++) {
        for (col = 0; col < 5; col++) {
            if ((rows[row] >> (4 - col)) & 0x1) {
                for (dy = 0; dy < scale; dy++) {
                    for (dx = 0; dx < scale; dx++) {
                        PutSurfacePixel(surf, x + col * scale + dx, y + row * scale + dy, color);
                    }
                }
            }
        }
    }
}

static void DrawSurfaceText(const no_signal_surface_t *surf, int x, int y, const char *text, int scale, uint32_t color)
{
    int i;
    int advance = 6 * scale;
    for (i = 0; text[i] != '\0'; i++) {
        DrawSurfaceGlyph(surf, x + i * advance, y, text[i], scale, color);
    }
}

typedef struct moving_box_s {
    int x;
    int y;
    int prev_x;
    int prev_y;
    int w;
    int h;
    int dx;
    int dy;
    no_signal_surface_t surf;
} moving_box_t;

#if 0
typedef struct osd_box_s {
    int x;
    int y;
    int w;
    int h;
    int dx;
    int dy;
    const char *text;
    int text_scale;
    uint32_t border_color;
    uint32_t inner_color;
    uint32_t text_color;
} osd_box_t;
#endif

static int ClampInt(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int RandomStep(int min_speed, int max_speed)
{
    int magnitude = min_speed + (rand() % (max_speed - min_speed + 1));
    return (rand() & 1) ? magnitude : -magnitude;
}

static int BoxesOverlap(const moving_box_t *a, const moving_box_t *b)
{
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x ||
             a->y + a->h <= b->y || b->y + b->h <= a->y);
}

static void ClampAndBounceBox(moving_box_t *box, int min_x, int min_y, int max_x, int max_y)
{
    if (box->x < min_x) {
        box->x = min_x;
        box->dx = abs(box->dx);
    } else if (box->x + box->w > max_x) {
        box->x = max_x - box->w;
        box->dx = -abs(box->dx);
    }

    if (box->y < min_y) {
        box->y = min_y;
        box->dy = abs(box->dy);
    } else if (box->y + box->h > max_y) {
        box->y = max_y - box->h;
        box->dy = -abs(box->dy);
    }
}

static void ResolveBoxCollision(moving_box_t *a, moving_box_t *b)
{
    int overlap_x;
    int overlap_y;
    int tmp;

    if (!BoxesOverlap(a, b)) {
        return;
    }

    overlap_x = (a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w) -
                (a->x > b->x ? a->x : b->x);
    overlap_y = (a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h) -
                (a->y > b->y ? a->y : b->y);

    if (overlap_x <= overlap_y) {
        if (a->x < b->x) {
            a->x -= overlap_x / 2 + 1;
            b->x += overlap_x / 2 + 1;
        } else {
            a->x += overlap_x / 2 + 1;
            b->x -= overlap_x / 2 + 1;
        }
        tmp = a->dx;
        a->dx = b->dx;
        b->dx = tmp;
    } else {
        if (a->y < b->y) {
            a->y -= overlap_y / 2 + 1;
            b->y += overlap_y / 2 + 1;
        } else {
            a->y += overlap_y / 2 + 1;
            b->y -= overlap_y / 2 + 1;
        }
        tmp = a->dy;
        a->dy = b->dy;
        b->dy = tmp;
    }
}

/* Dedicated GE2D/OSD path is under development; keep current fb renderer as
 * fallback for now, and use fb1 as the dedicated OSD plane path. */
#if 0
static int BoxesOverlapInt(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return !(ax + aw <= bx || bx + bw <= ax || ay + ah <= by || by + bh <= ay);
}

static void ClampAndBounceOsdBox(osd_box_t *box, int min_x, int min_y, int max_x, int max_y)
{
    if (box->x < min_x) {
        box->x = min_x;
        box->dx = abs(box->dx);
    } else if (box->x + box->w > max_x) {
        box->x = max_x - box->w;
        box->dx = -abs(box->dx);
    }

    if (box->y < min_y) {
        box->y = min_y;
        box->dy = abs(box->dy);
    } else if (box->y + box->h > max_y) {
        box->y = max_y - box->h;
        box->dy = -abs(box->dy);
    }
}

static void ResolveOsdCollision(osd_box_t *a, osd_box_t *b)
{
    int overlap_x;
    int overlap_y;
    int tmp;

    if (!BoxesOverlapInt(a->x, a->y, a->w, a->h, b->x, b->y, b->w, b->h)) {
        return;
    }

    overlap_x = (a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w) -
                (a->x > b->x ? a->x : b->x);
    overlap_y = (a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h) -
                (a->y > b->y ? a->y : b->y);

    if (overlap_x <= overlap_y) {
        if (a->x < b->x) {
            a->x -= overlap_x / 2 + 1;
            b->x += overlap_x / 2 + 1;
        } else {
            a->x += overlap_x / 2 + 1;
            b->x -= overlap_x / 2 + 1;
        }
        tmp = a->dx;
        a->dx = b->dx;
        b->dx = tmp;
    } else {
        if (a->y < b->y) {
            a->y -= overlap_y / 2 + 1;
            b->y += overlap_y / 2 + 1;
        } else {
            a->y += overlap_y / 2 + 1;
            b->y -= overlap_y / 2 + 1;
        }
        tmp = a->dy;
        a->dy = b->dy;
        b->dy = tmp;
    }
}

static void DrawOsdGlyph(AM_OSD_Surface_t *surf, int x, int y, char c, int scale, uint32_t pix)
{
    const unsigned char *rows = GetGlyphRows(c);
    int row;
    int col;
    int dx;
    int dy;

    for (row = 0; row < 7; row++) {
        for (col = 0; col < 5; col++) {
            if ((rows[row] >> (4 - col)) & 0x1) {
                for (dy = 0; dy < scale; dy++) {
                    for (dx = 0; dx < scale; dx++) {
                        AM_OSD_DrawPixel(surf, x + col * scale + dx, y + row * scale + dy, pix);
                    }
                }
            }
        }
    }
}

static void DrawOsdText(AM_OSD_Surface_t *surf, int x, int y, const char *text, int scale, uint32_t pix)
{
    int i;
    int advance = 6 * scale;
    for (i = 0; text[i] != '\0'; i++) {
        DrawOsdGlyph(surf, x + i * advance, y, text[i], scale, pix);
    }
}

static void DrawOsdBox(AM_OSD_Surface_t *surf, const osd_box_t *box, int border)
{
    AM_OSD_Rect_t rect;

    rect.x = box->x;
    rect.y = box->y;
    rect.w = box->w;
    rect.h = box->h;
    AM_OSD_DrawFilledRect(surf, &rect, box->border_color);

    rect.x = box->x + border;
    rect.y = box->y + border;
    rect.w = box->w - border * 2;
    rect.h = box->h - border * 2;
    AM_OSD_DrawFilledRect(surf, &rect, box->inner_color);

    DrawOsdText(surf,
                box->x + (box->w - MeasureTextWidth(box->text, box->text_scale)) / 2,
                box->y + (box->h - 7 * box->text_scale) / 2,
                box->text,
                box->text_scale,
                box->text_color);
}

static void *NoSignalUiThreadOsd(void *arg)
{
    AM_OSD_OpenPara_t osd_para;
    AM_OSD_Surface_t *surf = NULL;
    AM_OSD_Color_t color;
    uint32_t bg;
    uint32_t white;
    uint32_t blue;
    uint32_t red;
    osd_box_t title_box;
    osd_box_t logo_box;
    int out_width = 1920;
    int out_height = 1080;
    int min_dim;
    int padding;
    int border;
    int min_y;
    int min_speed = 1;
    int max_speed = 3;
    int attempt = 0;
    int max_attempts = 64;

    (void)arg;

    if (GetCurrentHdmiTxResolution(&out_width, &out_height) != 0) {
        out_width = 1920;
        out_height = 1080;
    }

    memset(&osd_para, 0, sizeof(osd_para));
    osd_para.format = AM_OSD_FMT_COLOR_8888;
    osd_para.width = out_width;
    osd_para.height = out_height;
    osd_para.output_width = out_width;
    osd_para.output_height = out_height;
    osd_para.enable_double_buffer = AM_TRUE;
    osd_para.vout_dev_no = 0;

    if (AM_OSD_Open(0, &osd_para) != AM_SUCCESS) {
        LOGE("%s: failed to open OSD renderer, fallback to fb renderer\n", __FUNCTION__);
        return NoSignalUiThread(arg);
    }

    AM_OSD_GetSurface(0, &surf);
    if (!surf) {
        AM_OSD_Close(0);
        return NoSignalUiThread(arg);
    }

    color.r = 0; color.g = 0; color.b = 0; color.a = 0xff; AM_OSD_MapColor(surf->format, &color, &bg);
    color.r = 255; color.g = 255; color.b = 255; color.a = 0xff; AM_OSD_MapColor(surf->format, &color, &white);
    color.r = 0; color.g = 102; color.b = 204; color.a = 0xff; AM_OSD_MapColor(surf->format, &color, &blue);
    color.r = 220; color.g = 30; color.b = 30; color.a = 0xff; AM_OSD_MapColor(surf->format, &color, &red);

    min_dim = out_width < out_height ? out_width : out_height;
    padding = ClampInt(min_dim / 45, 8, 64);
    border = ClampInt(min_dim / 216, 2, 10);
    min_y = ClampInt(min_dim / 34, 16, 96);

    memset(&title_box, 0, sizeof(title_box));
    memset(&logo_box, 0, sizeof(logo_box));
    title_box.text = "NO SIGNAL";
    title_box.text_scale = ClampInt(min_dim / 108, 3, 28);
    title_box.w = MeasureTextWidth(title_box.text, title_box.text_scale) + padding * 2;
    title_box.h = 7 * title_box.text_scale + padding * 2;
    title_box.border_color = red;
    title_box.inner_color = bg;
    title_box.text_color = white;
    title_box.x = rand() % (out_width - title_box.w);
    title_box.y = min_y + rand() % (out_height - min_y - title_box.h);
    title_box.dx = RandomStep(min_speed, max_speed);
    title_box.dy = RandomStep(min_speed, max_speed);

    logo_box.text = "STREAMBOX";
    logo_box.text_scale = ClampInt(min_dim / 180, 2, 18);
    logo_box.w = MeasureTextWidth(logo_box.text, logo_box.text_scale) + padding * 2;
    logo_box.h = 7 * logo_box.text_scale + padding * 2;
    logo_box.border_color = blue;
    logo_box.inner_color = bg;
    logo_box.text_color = white;
    do {
        logo_box.x = rand() % (out_width - logo_box.w);
        logo_box.y = min_y + rand() % (out_height - min_y - logo_box.h);
        attempt++;
    } while (BoxesOverlapInt(title_box.x, title_box.y, title_box.w, title_box.h,
                             logo_box.x, logo_box.y, logo_box.w, logo_box.h) && attempt < max_attempts);
    logo_box.dx = RandomStep(min_speed, max_speed);
    logo_box.dy = RandomStep(min_speed, max_speed);

    while (run) {
        int active;
        AM_OSD_Rect_t full = {0, 0, out_width, out_height};

        pthread_mutex_lock(&g_no_signal_mutex);
        active = g_no_signal_ui_active;
        pthread_mutex_unlock(&g_no_signal_mutex);
        if (!active) {
            break;
        }

        AM_OSD_DrawFilledRect(surf, &full, bg);

        title_box.x += title_box.dx;
        title_box.y += title_box.dy;
        logo_box.x += logo_box.dx;
        logo_box.y += logo_box.dy;
        ClampAndBounceOsdBox(&title_box, 0, min_y, out_width, out_height);
        ClampAndBounceOsdBox(&logo_box, 0, min_y, out_width, out_height);
        ResolveOsdCollision(&title_box, &logo_box);
        ClampAndBounceOsdBox(&title_box, 0, min_y, out_width, out_height);
        ClampAndBounceOsdBox(&logo_box, 0, min_y, out_width, out_height);

        DrawOsdBox(surf, &title_box, border);
        DrawOsdBox(surf, &logo_box, border);
        AM_OSD_Update(0, NULL);
        usleep(33000);
    }

    {
        AM_OSD_Rect_t full = {0, 0, out_width, out_height};
        AM_OSD_DrawFilledRect(surf, &full, bg);
        AM_OSD_Update(0, NULL);
    }
    AM_OSD_Close(0);

    pthread_mutex_lock(&g_no_signal_mutex);
    g_no_signal_thread_created = 0;
    pthread_mutex_unlock(&g_no_signal_mutex);
    return NULL;
}
#endif

static void *NoSignalUiThread(void *arg)
{
    no_signal_fb_t fb;
    moving_box_t title_box;
    moving_box_t logo_box;
    const char *title = "NO SIGNAL";
    const char *logo = "STREAMBOX";
    int min_dim;
    int title_scale;
    int logo_scale;
    int padding;
    int border;
    int min_y;
    int min_speed;
    int max_speed;
    uint32_t bg;
    uint32_t white;
    uint32_t blue;
    uint32_t red;
    int max_attempts = 64;
    int attempt;
    int out_width = 0;
    int out_height = 0;
    int surfaces_initialized = 0;

    (void)arg;

    memset(&fb, 0, sizeof(fb));
    memset(&title_box, 0, sizeof(title_box));
    memset(&logo_box, 0, sizeof(logo_box));

reinitialize_ui:
    if (surfaces_initialized) {
        FreeNoSignalSurface(&title_box.surf);
        FreeNoSignalSurface(&logo_box.surf);
        surfaces_initialized = 0;
    }
    if (fb.fd >= 0) {
        CloseNoSignalFramebuffer(&fb);
        memset(&fb, 0, sizeof(fb));
    }

    if (OpenNoSignalFramebuffer(&fb) != 0) {
        return NULL;
    }

    /* Always render the no-signal UI at 1080p.  The OSD hardware scaler
     * will upscale to whatever the current HDMI TX mode is (e.g. 4K60).
     * This avoids an mmap overcommit: fb0 smem_len is typically ~16 MB
     * which is enough for 1080p@32bpp (8 MB) but not 4K@32bpp (33 MB). */
    out_width = 1920;
    out_height = 1080;

    if (ResizeNoSignalFramebuffer(&fb, out_width, out_height) != 0) {
        out_width = fb.width;
        out_height = fb.height;
    }

    bg = PackFbColor(&fb, 0, 0, 0);
    white = PackFbColor(&fb, 255, 255, 255);
    blue = PackFbColor(&fb, 0, 102, 204);
    red = PackFbColor(&fb, 220, 30, 30);
    srand((unsigned int)(time(NULL) ^ getpid()));

    min_dim = fb.width < fb.height ? fb.width : fb.height;
    title_scale = ClampInt(min_dim / 108, 3, 28);
    logo_scale = ClampInt(min_dim / 180, 2, 18);
    padding = ClampInt(min_dim / 45, 8, 64);
    border = ClampInt(min_dim / 216, 2, 10);
    min_y = ClampInt(min_dim / 34, 16, 96);
    min_speed = 1;
    max_speed = 3;

    memset(&title_box, 0, sizeof(title_box));
    memset(&logo_box, 0, sizeof(logo_box));
    title_box.w = MeasureTextWidth(title, title_scale) + padding * 2;
    title_box.h = 7 * title_scale + padding * 2;
    logo_box.w = MeasureTextWidth(logo, logo_scale) + padding * 2;
    logo_box.h = 7 * logo_scale + padding * 2;

    if (InitNoSignalSurface(&title_box.surf, &fb, title_box.w, title_box.h) != 0 ||
        InitNoSignalSurface(&logo_box.surf, &fb, logo_box.w, logo_box.h) != 0) {
        FreeNoSignalSurface(&title_box.surf);
        FreeNoSignalSurface(&logo_box.surf);
        CloseNoSignalFramebuffer(&fb);
        return NULL;
    }
    surfaces_initialized = 1;

    FillSurfaceRect(&title_box.surf, 0, 0, title_box.w, title_box.h, red);
    FillSurfaceRect(&title_box.surf, border, border, title_box.w - border * 2, title_box.h - border * 2, bg);
    DrawSurfaceText(&title_box.surf,
                    (title_box.w - MeasureTextWidth(title, title_scale)) / 2,
                    (title_box.h - 7 * title_scale) / 2,
                    title, title_scale, white);

    FillSurfaceRect(&logo_box.surf, 0, 0, logo_box.w, logo_box.h, blue);
    FillSurfaceRect(&logo_box.surf, border, border, logo_box.w - border * 2, logo_box.h - border * 2, bg);
    DrawSurfaceText(&logo_box.surf,
                    (logo_box.w - MeasureTextWidth(logo, logo_scale)) / 2,
                    (logo_box.h - 7 * logo_scale) / 2,
                    logo, logo_scale, white);

    title_box.x = rand() % (fb.width - title_box.w);
    title_box.y = min_y + rand() % (fb.height - min_y - title_box.h);
    title_box.dx = RandomStep(min_speed, max_speed);
    title_box.dy = RandomStep(min_speed, max_speed);
    title_box.prev_x = title_box.x;
    title_box.prev_y = title_box.y;

    do {
        logo_box.x = rand() % (fb.width - logo_box.w);
        logo_box.y = min_y + rand() % (fb.height - min_y - logo_box.h);
        attempt++;
    } while (BoxesOverlap(&title_box, &logo_box) && attempt < max_attempts);
    if (BoxesOverlap(&title_box, &logo_box)) {
        logo_box.x = ClampInt(fb.width - logo_box.w - padding, 0, fb.width - logo_box.w);
        logo_box.y = ClampInt(fb.height - logo_box.h - padding, min_y, fb.height - logo_box.h);
    }
    logo_box.dx = RandomStep(min_speed, max_speed);
    logo_box.dy = RandomStep(min_speed, max_speed);
    logo_box.prev_x = logo_box.x;
    logo_box.prev_y = logo_box.y;

    ResetNoSignalFramebuffer(&fb);
    SetConsoleCursorVisible(0);
    PresentNoSignalFramebufferPage(&fb, 0);

    while (run) {
        int active;
        int next_page;
        unsigned char *page_buf;

        pthread_mutex_lock(&g_no_signal_mutex);
        active = g_no_signal_ui_active;
        pthread_mutex_unlock(&g_no_signal_mutex);

        if (!active) {
            break;
        }

        /* No TX mode change detection needed — we always render at 1080p
         * and the OSD hardware scaler upscales to the current TX mode. */

        title_box.x += title_box.dx;
        title_box.y += title_box.dy;
        logo_box.x += logo_box.dx;
        logo_box.y += logo_box.dy;

        ClampAndBounceBox(&title_box, 0, min_y, fb.width, fb.height);
        ClampAndBounceBox(&logo_box, 0, min_y, fb.width, fb.height);

        ResolveBoxCollision(&title_box, &logo_box);
        ClampAndBounceBox(&title_box, 0, min_y, fb.width, fb.height);
        ClampAndBounceBox(&logo_box, 0, min_y, fb.width, fb.height);

        next_page = fb.page_count > 1 ? (fb.current_page ^ 1) : 0;
        page_buf = GetNoSignalFramebufferPage(&fb, next_page);
        memset(page_buf, 0, (size_t)fb.stride * (size_t)fb.height);

        {
            no_signal_fb_t draw_fb = fb;
            draw_fb.map = page_buf;
            BlitSurfaceToFb(&title_box.surf, &draw_fb, title_box.x, title_box.y);
            BlitSurfaceToFb(&logo_box.surf, &draw_fb, logo_box.x, logo_box.y);
        }
        PresentNoSignalFramebufferPage(&fb, next_page);

        usleep(33000);
    }

    FillFbRect(&fb, 0, 0, fb.width, fb.height, bg);
    SetConsoleCursorVisible(1);
    WriteSysfs(NoSignalFbBlankPath(), "1");
    if (surfaces_initialized) {
        FreeNoSignalSurface(&title_box.surf);
        FreeNoSignalSurface(&logo_box.surf);
    }
    CloseNoSignalFramebuffer(&fb);

    pthread_mutex_lock(&g_no_signal_mutex);
    g_no_signal_thread_created = 0;
    pthread_mutex_unlock(&g_no_signal_mutex);

    return NULL;
}

static void StartNoSignalUi(const char *reason)
{
    void *(*thread_func)(void *) = NULL;

    pthread_mutex_lock(&g_no_signal_mutex);
    if (g_no_signal_ui_active) {
        pthread_mutex_unlock(&g_no_signal_mutex);
        return;
    }

    g_no_signal_ui_active = 1;
    LOGE("%s: Starting no-signal UI (%s)\n", __FUNCTION__, reason ? reason : "n/a");
    thread_func = NoSignalUiThread;
    if (!g_no_signal_thread_created) {
        if (pthread_create(&g_no_signal_thread, NULL, thread_func, NULL) == 0) {
            g_no_signal_thread_created = 1;
        } else {
            g_no_signal_ui_active = 0;
            LOGE("%s: Failed to create no-signal UI thread\n", __FUNCTION__);
        }
    }
    pthread_mutex_unlock(&g_no_signal_mutex);
}

static void StopNoSignalUi(void)
{
    int need_join = 0;

    pthread_mutex_lock(&g_no_signal_mutex);
    if (g_no_signal_ui_active) {
        LOGE("%s: Stopping no-signal UI\n", __FUNCTION__);
    }
    g_no_signal_ui_active = 0;
    need_join = g_no_signal_thread_created;
    pthread_mutex_unlock(&g_no_signal_mutex);

    if (need_join) {
        pthread_join(g_no_signal_thread, NULL);
    }
}

static void UpdateNoSignalUiFromCurrentSource(struct TvClientWrapper_t *pTvClientWrapper, const char *reason)
{
    const video_config_t *video = &config_get()->video;
    tv_source_input_t source;
    int connected;
    int width;
    int height;

    if (g_force_no_signal_ui || pTvClientWrapper == NULL) {
        return;
    }

    source = (tv_source_input_t)config_get_source_input(video->hdmi_source);
    connected = GetSourceConnectStatus(pTvClientWrapper, source);
    width = GetCurrentSourceFrameWidth(pTvClientWrapper);
    height = GetCurrentSourceFrameHeight(pTvClientWrapper);

    LOGE("%s: reason=%s source=%d connected=%d size=%dx%d\n",
         __FUNCTION__, reason ? reason : "unknown", source, connected, width, height);

    if (connected == 1 && width > 0 && height > 0) {
        StopNoSignalUi();
    } else {
        StartNoSignalUi(reason ? reason : "current-source-invalid");
        StopAudioPassthrough();
    }
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
        else if (fps >= 143 && fps <= 144) rounded_fps = 144;
        else if (fps >= 239 && fps <= 240) rounded_fps = 240;
        else {
            rounded_fps = (fps + 1) / 2 * 2;
            if (rounded_fps < fps) rounded_fps = fps;
        }
    } else {
        if (fps >= 50 && fps < 60) rounded_fps = 60;
        else if (fps >= 100 && fps < 120) rounded_fps = 120;
        else if (fps >= 130 && fps < 144) rounded_fps = 144;
        else if (fps >= 200 && fps < 240) rounded_fps = 240;
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
        LOGE("%s: Invalid hdmirx parameters: %dx%d@%dHz, skipping sync\n", 
             __FUNCTION__, rx_width, rx_height, rx_fps);
        return;
    }

    LOGE("%s: HDMIRX input: %dx%d@%dHz\n", __FUNCTION__, rx_width, rx_height, rx_fps);

    StopAudioPassthrough();
    LOGD("%s: Audio passthrough stopped before resolution change\n", __FUNCTION__);

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
        else if (rx_fps >= 143 && rx_fps <= 144) rounded_fps = 144;
        else if (rx_fps >= 239 && rx_fps <= 240) rounded_fps = 240;
        else {
            rounded_fps = (rx_fps + 1) / 2 * 2;
            if (rounded_fps < rx_fps) rounded_fps = rx_fps;
        }
    } else {
        if (rx_fps >= 50 && rx_fps < 60) rounded_fps = 60;
        else if (rx_fps >= 100 && rx_fps < 120) rounded_fps = 120;
        else if (rx_fps >= 130 && rx_fps < 144) rounded_fps = 144;
        else if (rx_fps >= 200 && rx_fps < 240) rounded_fps = 240;
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

        /*
         * NOTE: Do NOT call ResetFb0AfterTxModeChange() here.
         * Touching fb0/OSD state (blank, free_scale, FBIOPUT_VSCREENINFO)
         * during active video passthrough disrupts the VPP compositor and
         * causes horizontal tearing artifacts on the HDMI TX output.
         * The no-signal UI thread handles its own fb0 setup independently.
         */

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

    LOGD("%s: Waiting for HDMI TX to stabilize before restarting audio...\n", __FUNCTION__);
    usleep(500000);

    /*
     * Sync the DRM CRTC mode to match the new HDMI TX output mode.
     * This must be done AFTER the TX has fully stabilized, because
     * crtc_mode_store() calls fill_modes() to enumerate connector modes
     * from the HDMI TX — if the TX link isn't established yet, the
     * mode won't be found and the modeset will silently fail.
     */
    SyncDrmCrtcMode(mode_str);

    LOGD("%s: HDMI TX stable, restarting audio passthrough\n", __FUNCTION__);
    StartAudioPassthrough();
    LOGD("%s: Audio passthrough restarted after HDMI TX stabilization\n", __FUNCTION__);
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
        LOGE("%s: source: %d, signalFmt: %d, transFmt: %d, status: %d, isDVI: %d.\n", __FUNCTION__,
                                                   signalDetectEvent->SourceInput,
                                                   signalDetectEvent->SignalFmt,
                                                   signalDetectEvent->TransFmt,
                                                   signalDetectEvent->SignalStatus,
                                                   signalDetectEvent->isDviSignal);

        if (g_force_no_signal_ui) {
            StartNoSignalUi("forced-test-mode");
            StopAudioPassthrough();
            return;
        }

        if (signalDetectEvent->SignalStatus == TVIN_SIG_STATUS_STABLE && g_pTvClientWrapper != NULL) {
            StopNoSignalUi();
            LOGE("%s: HDMIRX input change detected (stable signal), synchronizing HDMITX\n", __FUNCTION__);
            SynchronizeHdmitxToHdmirx(g_pTvClientWrapper);
        } else if (signalDetectEvent->SignalStatus != TVIN_SIG_STATUS_STABLE) {
            LOGE("%s: HDMIRX signal not stable (status=%d), stopping audio passthrough\n",
                 __FUNCTION__, signalDetectEvent->SignalStatus);
            StartNoSignalUi(signalDetectEvent->SignalStatus == TVIN_SIG_STATUS_NOSIG ? "no-signal" : "unstable");
            StopAudioPassthrough();
        }
    } else if (eventType == TV_EVENT_TYPE_SOURCE_CONNECT) {
        SourceConnectCallback_t *sourceConnectEvent = (SourceConnectCallback_t *)(eventData);
        LOGE("%s: source: %d, connectStatus: %d\n", __FUNCTION__,
                  sourceConnectEvent->SourceInput, sourceConnectEvent->ConnectionState);

        if (!g_force_no_signal_ui && sourceConnectEvent->ConnectionState == 0) {
            StartNoSignalUi("source-disconnected");
            StopAudioPassthrough();
        }
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
    printf("      --test-no-signal-ui   Force the minimal no-signal UI for validation\n");
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
    int test_no_signal_ui = 0;
    const char *renderer_env = getenv("STREAMBOX_NO_SIGNAL_RENDERER");

    if (renderer_env && !strcmp(renderer_env, "fb1")) {
        g_no_signal_renderer_mode = 1;
        g_no_signal_fb_device = "/dev/fb1";
        g_no_signal_fb_sysfs = "/sys/class/graphics/fb1";
    } else {
        g_no_signal_renderer_mode = 0;
        g_no_signal_fb_device = "/dev/fb0";
        g_no_signal_fb_sysfs = "/sys/class/graphics/fb0";
    }

    static struct option long_options[] = {
        {"config",    required_argument, 0, 'c'},
        {"game-mode", required_argument, 0, 'g'},
        {"test-no-signal-ui", no_argument, 0, 1000},
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
        case 1000:
            test_no_signal_ui = 1;
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
    printf("  no_signal_ui_test=%d\n", test_no_signal_ui);

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

    if (test_no_signal_ui) {
        g_force_no_signal_ui = 1;
        StartNoSignalUi("cli-test-mode");
    } else {
        StartTvProcessing(pTvClientWrapper);
        UpdateNoSignalUiFromCurrentSource(pTvClientWrapper, "startup-check");
    }

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

    StopNoSignalUi();

    /* Cleanup config */
    config_cleanup();

    TRACE(1, "main() EXIT\n");
    return 0;
}
