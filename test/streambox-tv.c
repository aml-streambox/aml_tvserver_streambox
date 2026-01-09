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
#include <TvClientWrapper.h>
#include <CTvClientLog.h>

static int run = 1;
static struct TvClientWrapper_t *g_pTvClientWrapper = NULL;

/* Global configuration options */
static int g_game_mode = 2;      /* 0=disable, 1=game mode 1, 2=game mode 2 with VRR (default) */
static int g_trace_level = 0;    /* 0=off, 1=low, 2=medium, 3=high */

/* VRR force frame lock for HDMI passthrough low latency mode */
#define VRR_DEBUG_PATH        "/sys/class/aml_vrr/vrr2/debug"

/* HDMI TX HPD state sysfs path */
#define HDMI_TX_HPD_STATE_PATH "/sys/class/amhdmitx/amhdmitx0/hpd_state"

/* HDMI TX HPD monitoring state */
static int g_hdmitx_connected = 0;   /* 0=disconnected, 1=connected */
static int g_tv_started = 0;         /* 0=stopped, 1=started */
static int g_uevent_fd = -1;         /* Netlink socket for uevent */
static pthread_t g_uevent_thread;    /* Uevent monitor thread */
static tv_source_input_t g_current_source = SOURCE_HDMI2;

/* Trace macro - runtime controlled */
#define TRACE(level, fmt, ...) do { \
    if (g_trace_level >= (level)) { \
        printf("[TRACE] streambox-tv: " fmt, ##__VA_ARGS__); \
        fflush(stdout); \
    } \
} while(0)

static void print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("\n");
    printf("HDMI input passthrough application for StreamBox with game mode and VRR support.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -g, --game-mode <0|1|2>  Game mode selection:\n");
    printf("                             0 = disable game mode\n");
    printf("                             1 = game mode 1\n");
    printf("                             2 = game mode 2 with force VRR enabled (default)\n");
    printf("  -t, --trace <0-3>        Trace level:\n");
    printf("                             0 = off (default)\n");
    printf("                             1 = low (major events)\n");
    printf("                             2 = medium (function calls)\n");
    printf("                             3 = high (verbose)\n");
    printf("  -h, --help               Display this help and exit\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                       # Default: game mode 2 with VRR\n", prog_name);
    printf("  %s -g 0                  # Disable game mode\n", prog_name);
    printf("  %s -g 1 -t 2             # Game mode 1 with medium trace\n", prog_name);
    printf("  %s --game-mode=2 --trace=3  # Game mode 2 with high trace\n", prog_name);
}

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

/* Read sysfs file and return value as string */
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
    /* Remove trailing newline if present */
    if (bytes_read > 0 && buf[bytes_read - 1] == '\n') {
        buf[bytes_read - 1] = '\0';
    }

    return 0;
}

/* Read HDMI TX HPD state: returns 1 if connected, 0 if disconnected, -1 on error */
static int GetHdmiTxHpdState(void)
{
    char buf[16] = {0};
    int ret = ReadSysfs(HDMI_TX_HPD_STATE_PATH, buf, sizeof(buf));
    if (ret != 0) {
        return -1;
    }
    return (atoi(buf) == 1) ? 1 : 0;
}

/* Create netlink socket for uevent monitoring */
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
    addr.nl_groups = 1; /* Receive broadcast messages */

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGD("Failed to bind uevent socket\n");
        close(fd);
        return -1;
    }

    return fd;
}

/* Parse uevent for hdmitx_hpd event, returns: 0=disconnect, 1=connect, -1=not HPD event */
static int ParseHdmiTxHpdEvent(const char *buf, int len)
{
    const char *ptr = buf;
    const char *end = buf + len;

    /* Look for hdmitx_hpd= in the uevent data */
    while (ptr < end) {
        if (strncmp(ptr, "hdmitx_hpd=", 11) == 0) {
            int state = atoi(ptr + 11);
            LOGD("Received HDMI TX HPD event: hdmitx_hpd=%d\n", state);
            return state;
        }
        ptr += strlen(ptr) + 1;
    }
    return -1; /* Not an HPD event */
}

/* Start TV processing - configure and start HDMI input */
static void StartTvProcessing(struct TvClientWrapper_t *pTvClientWrapper)
{
    if (g_tv_started) {
        LOGD("%s: TV already started\n", __FUNCTION__);
        return;
    }

    LOGD("%s: Starting TV processing\n", __FUNCTION__);

#ifdef STREAM_BOX
    if (g_game_mode > 0) {
        int ret;

        /* Enable ALLM (Auto Low Latency Mode) */
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
        int game_mode_value = (g_game_mode == 2) ? 3 : 1;
        ret = SetGameMode(pTvClientWrapper, 1, game_mode_value);
        if (ret == 0) {
            LOGD("Game Mode enabled successfully (game_mode_value=%d)\n", game_mode_value);
        } else {
            LOGD("Failed to enable Game Mode, ret=%d\n", ret);
        }
    }
#endif

    StartTv(pTvClientWrapper, g_current_source);
    g_tv_started = 1;
    LOGD("%s: TV processing started\n", __FUNCTION__);
}

/* Stop TV processing */
static void StopTvProcessing(struct TvClientWrapper_t *pTvClientWrapper)
{
    if (!g_tv_started) {
        LOGD("%s: TV already stopped\n", __FUNCTION__);
        return;
    }

    LOGD("%s: Stopping TV processing\n", __FUNCTION__);

    /* Disable VRR force mode */
    if (g_game_mode == 2) {
        WriteSysfs(VRR_DEBUG_PATH, "en 0");
    }

    StopTv(pTvClientWrapper, g_current_source);
    g_tv_started = 0;
    LOGD("%s: TV processing stopped\n", __FUNCTION__);
}

/* Uevent monitor thread */
static void *UeventMonitorThread(void *arg)
{
    struct TvClientWrapper_t *pTvClientWrapper = (struct TvClientWrapper_t *)arg;
    char buf[4096];
    struct pollfd pfd;

    pfd.fd = g_uevent_fd;
    pfd.events = POLLIN;

    LOGD("Uevent monitor thread started\n");

    while (run) {
        int ret = poll(&pfd, 1, 1000); /* 1 second timeout */
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

/* Read input_rate from vdin and parse to get actual framerate in Hz (as float) */
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

    /* Parse format: "XXX.XXX" (e.g., "119.880") */
    ret = sscanf(buf, "%d.%d", &int_part, &frac_part);
    if (ret != 2) {
        LOGD("%s: Failed to parse input_rate: %s\n", __FUNCTION__, buf);
        return -1;
    }

    /* Convert to Hz: int_part + frac_part/1000.0 */
    rate = (float)int_part + (float)frac_part / 1000.0f;
    *input_rate_hz = rate;

    LOGD("%s: Read input_rate: %s -> %.3f Hz\n", __FUNCTION__, buf, rate);
    return 0;
}

/* Determine frac_rate_policy based on actual input_rate vs rounded fps */
static int DetermineFracRatePolicy(int rounded_fps, float actual_input_rate_hz)
{
    /* Fractional framerate thresholds:
     * - 120Hz: if input_rate <= 119.88Hz -> use fractional (1)
     * - 60Hz:  if input_rate <= 59.94Hz  -> use fractional (1)
     * - 30Hz:  if input_rate <= 29.97Hz  -> use fractional (1)
     * - 24Hz:  if input_rate <= 23.976Hz -> use fractional (1)
     * Otherwise use perfect framerate (0)
     */
    int frac_rate_policy = 0;

    switch (rounded_fps) {
    case 120:
        if (actual_input_rate_hz <= 119.88f) {
            frac_rate_policy = 1;
            LOGD("%s: Detected fractional framerate: %.3f Hz <= 119.88 Hz (for 120Hz)\n",
                 __FUNCTION__, actual_input_rate_hz);
        }
        break;
    case 60:
        if (actual_input_rate_hz <= 59.94f) {
            frac_rate_policy = 1;
            LOGD("%s: Detected fractional framerate: %.3f Hz <= 59.94 Hz (for 60Hz)\n",
                 __FUNCTION__, actual_input_rate_hz);
        }
        break;
    case 30:
        if (actual_input_rate_hz <= 29.97f) {
            frac_rate_policy = 1;
            LOGD("%s: Detected fractional framerate: %.3f Hz <= 29.97 Hz (for 30Hz)\n",
                 __FUNCTION__, actual_input_rate_hz);
        }
        break;
    case 24:
        if (actual_input_rate_hz <= 23.976f) {
            frac_rate_policy = 1;
            LOGD("%s: Detected fractional framerate: %.3f Hz <= 23.976 Hz (for 24Hz)\n",
                 __FUNCTION__, actual_input_rate_hz);
        }
        break;
    default:
        /* For other framerates, use perfect framerate (0) */
        break;
    }

    if (frac_rate_policy == 0) {
        LOGD("%s: Using perfect framerate (frac_rate_policy=0) for %dHz (actual: %.3f Hz)\n",
             __FUNCTION__, rounded_fps, actual_input_rate_hz);
    }

    return frac_rate_policy;
}

/* Format hdmitx mode string from resolution and framerate */
static int FormatHdmitxMode(int width, int height, int fps, char *mode_str, size_t mode_str_size, int fine_tune)
{
    if (mode_str == NULL || mode_str_size < 32) {
        return -1;
    }

    int rounded_fps = fps;

    if (fine_tune) {
        /* Fine-tune framerate to perfectly match rx framerate when VRR is not supported */
        /* Handle common fractional framerates by rounding to nearest standard framerate */
        if (fps >= 59 && fps <= 60) {
            /* 59.94Hz or similar -> 60Hz */
            rounded_fps = 60;
        } else if (fps >= 29 && fps <= 30) {
            /* 29.97Hz or similar -> 30Hz */
            rounded_fps = 30;
        } else if (fps >= 23 && fps <= 24) {
            /* 23.976Hz or similar -> 24Hz */
            rounded_fps = 24;
        } else if (fps >= 119 && fps <= 120) {
            /* 119.88Hz or similar -> 120Hz */
            rounded_fps = 120;
        } else {
            /* For other framerates, round to nearest integer */
            rounded_fps = (fps + 1) / 2 * 2; /* Round to nearest even number for common cases */
            if (rounded_fps < fps) rounded_fps = fps; /* Prefer rounding up */
        }
        LOGD("%s: Fine-tuning framerate: %dHz -> %dHz\n", __FUNCTION__, fps, rounded_fps);
    } else {
        /* With VRR support, use general framerate matching (round to standard values) */
        if (fps >= 50 && fps < 60) {
            rounded_fps = 60; /* 50Hz/59.94Hz -> 60Hz */
        } else if (fps >= 100 && fps < 120) {
            rounded_fps = 120; /* 100Hz/119.88Hz -> 120Hz */
        } else if (fps >= 24 && fps < 30) {
            rounded_fps = 30; /* 24Hz/29.97Hz -> 30Hz */
        }
        /* Otherwise use the framerate as-is */
    }

    /* Format: "widthxheightp<fps>hz" (e.g., "1920x1080p60hz") */
    snprintf(mode_str, mode_str_size, "%dx%dp%dhz", width, height, rounded_fps);
    return 0;
}

/* Synchronize hdmitx output to match hdmirx input */
static void SynchronizeHdmitxToHdmirx(struct TvClientWrapper_t *pTvClientWrapper)
{
    if (pTvClientWrapper == NULL) {
        LOGD("%s: pTvClientWrapper is NULL\n", __FUNCTION__);
        return;
    }

    TRACE(2, "SynchronizeHdmitxToHdmirx() ENTRY\n");

    /* Wait a bit for signal info to be available after event */
    usleep(100000); /* 100ms delay */

    /* Get current hdmirx resolution and framerate */
    int rx_width = GetCurrentSourceFrameWidth(pTvClientWrapper);
    int rx_height = GetCurrentSourceFrameHeight(pTvClientWrapper);
    int rx_fps = GetCurrentSourceFrameFps(pTvClientWrapper);

    if (rx_width <= 0 || rx_height <= 0 || rx_fps <= 0) {
        LOGD("%s: Invalid hdmirx parameters: %dx%d@%dHz, skipping sync\n", 
             __FUNCTION__, rx_width, rx_height, rx_fps);
        return;
    }

    LOGD("%s: HDMIRX input: %dx%d@%dHz\n", __FUNCTION__, rx_width, rx_height, rx_fps);

    int fine_tune_framerate = 0;
#ifdef STREAM_BOX
    /* Check VRR support on hdmirx side */
    int vrr_enabled = GetHdmiVrrEnabled(pTvClientWrapper);
    int vrr_supported = (vrr_enabled != 0);

    LOGD("%s: VRR enabled: %d\n", __FUNCTION__, vrr_enabled);

    if (!vrr_supported) {
        /* Fine-tune framerate to perfectly match rx framerate when VRR is not supported */
        fine_tune_framerate = 1;
        LOGD("%s: VRR not supported, fine-tuning framerate to match exactly\n", __FUNCTION__);
    } else {
        /* VRR is supported, use general framerate matching */
        LOGD("%s: VRR supported, using general framerate matching\n", __FUNCTION__);
    }
#endif

    /* Calculate rounded fps (same logic as FormatHdmitxMode) for frac_rate_policy determination */
    int rounded_fps = rx_fps;
    if (fine_tune_framerate) {
        /* Fine-tune framerate to perfectly match rx framerate when VRR is not supported */
        if (rx_fps >= 59 && rx_fps <= 60) {
            rounded_fps = 60;
        } else if (rx_fps >= 29 && rx_fps <= 30) {
            rounded_fps = 30;
        } else if (rx_fps >= 23 && rx_fps <= 24) {
            rounded_fps = 24;
        } else if (rx_fps >= 119 && rx_fps <= 120) {
            rounded_fps = 120;
        } else {
            rounded_fps = (rx_fps + 1) / 2 * 2;
            if (rounded_fps < rx_fps) rounded_fps = rx_fps;
        }
    } else {
        /* With VRR support, use general framerate matching */
        if (rx_fps >= 50 && rx_fps < 60) {
            rounded_fps = 60;
        } else if (rx_fps >= 100 && rx_fps < 120) {
            rounded_fps = 120;
        } else if (rx_fps >= 24 && rx_fps < 30) {
            rounded_fps = 30;
        }
    }

    /* Format hdmitx mode string */
    char mode_str[64] = {0};
    int ret = FormatHdmitxMode(rx_width, rx_height, rx_fps, mode_str, sizeof(mode_str), fine_tune_framerate);
    if (ret != 0) {
        LOGD("%s: Failed to format hdmitx mode string\n", __FUNCTION__);
        return;
    }

    /* Read actual input_rate from vdin to determine if fractional framerate is needed */
    float actual_input_rate_hz = 0.0f;
    int frac_rate_policy = 0;
    ret = GetVdinInputRate(&actual_input_rate_hz);
    if (ret == 0) {
        /* Successfully read input_rate, determine frac_rate_policy */
        frac_rate_policy = DetermineFracRatePolicy(rounded_fps, actual_input_rate_hz);
    } else {
        /* Failed to read input_rate, default to perfect framerate (0) */
        LOGD("%s: Failed to read input_rate, defaulting to perfect framerate (frac_rate_policy=0)\n", __FUNCTION__);
        frac_rate_policy = 0;
    }

    const char *hdmitx_disp_mode_path = "/sys/class/amhdmitx/amhdmitx0/disp_mode";
    const char *hdmitx_frac_rate_policy_path = "/sys/class/amhdmitx/amhdmitx0/frac_rate_policy";

    /* Set frac_rate_policy BEFORE setting disp_mode (required by driver) */
    char frac_policy_str[2] = {0};
    snprintf(frac_policy_str, sizeof(frac_policy_str), "%d", frac_rate_policy);
    LOGD("%s: Setting frac_rate_policy to %d before setting disp_mode\n", __FUNCTION__, frac_rate_policy);
    ret = WriteSysfs(hdmitx_frac_rate_policy_path, frac_policy_str);
    if (ret != 0) {
        LOGD("%s: Warning: Failed to set frac_rate_policy to %d (ret=%d), continuing anyway\n",
             __FUNCTION__, frac_rate_policy, ret);
    } else {
        LOGD("%s: Successfully set frac_rate_policy to %d\n", __FUNCTION__, frac_rate_policy);
        /* Small delay after setting frac_rate_policy */
        usleep(10000); /* 10ms delay */
    }

    /* Disable current mode before setting new mode (required by driver) */
    LOGD("%s: Disabling current hdmitx mode before setting new mode\n", __FUNCTION__);
    ret = WriteSysfs(hdmitx_disp_mode_path, "off");
    if (ret != 0) {
        LOGD("%s: Warning: Failed to disable current hdmitx mode (ret=%d), continuing anyway\n", __FUNCTION__, ret);
    } else {
        /* Wait a bit for mode to be disabled */
        usleep(50000); /* 50ms delay */
    }

    /* Set hdmitx output mode to match hdmirx input */
    ret = WriteSysfs(hdmitx_disp_mode_path, mode_str);
    if (ret == 0) {
        LOGD("%s: Successfully set hdmitx mode to: %s (with frac_rate_policy=%d)\n",
             __FUNCTION__, mode_str, frac_rate_policy);

        /* Wait for HDMI TX to stabilize after mode change */
        usleep(200000); /* 200ms delay for TX to stabilize */

        /* Enable force VRR frame lock for low latency HDMI passthrough */
        /* Only enable VRR if game_mode == 2 */
        if (g_game_mode == 2) {
            LOGD("%s: Enabling force VRR frame lock mode (game_mode=%d)\n", __FUNCTION__, g_game_mode);
            ret = WriteSysfs(VRR_DEBUG_PATH, "mode 1");
            if (ret == 0) {
                ret = WriteSysfs(VRR_DEBUG_PATH, "en 1");
                if (ret == 0) {
                    LOGD("%s: Force VRR frame lock enabled successfully\n", __FUNCTION__);
                } else {
                    LOGD("%s: Failed to enable VRR (ret=%d)\n", __FUNCTION__, ret);
                }
            } else {
                LOGD("%s: Failed to set VRR mode (ret=%d)\n", __FUNCTION__, ret);
            }
        } else {
            LOGD("%s: Skipping VRR force enable (game_mode=%d)\n", __FUNCTION__, g_game_mode);
        }
    } else {
        LOGD("%s: Failed to set hdmitx mode to: %s (ret=%d)\n", __FUNCTION__, mode_str, ret);
    }

    TRACE(2, "SynchronizeHdmitxToHdmirx() EXIT\n");
}

static int DisplayInit()
{
//    WriteSysfs("/sys/class/graphics/fb0/osd_display_debug", "1");
    WriteSysfs("/sys/class/graphics/fb0/blank", "1");
    return 0;

}

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

        /* When hdmirx input change event received and signal is stable, synchronize hdmitx */
        if (signalDetectEvent->SignalStatus == TVIN_SIG_STATUS_STABLE && g_pTvClientWrapper != NULL) {
            LOGD("%s: HDMIRX input change detected (stable signal), synchronizing HDMITX\n", __FUNCTION__);
            SynchronizeHdmitxToHdmirx(g_pTvClientWrapper);
        }
    } else if (eventType == TV_EVENT_TYPE_SOURCE_CONNECT) {
        SourceConnectCallback_t *sourceConnectEvent = (SourceConnectCallback_t *)(eventData);
        LOGD("%s: source: %d, connectStatus: %d\n", __FUNCTION__,
                  sourceConnectEvent->SourceInput, sourceConnectEvent->ConnectionState);
    } else {
        LOGD("%s: invalid event.\n", __FUNCTION__);
    }
}

static void signal_handler(int s)
{
    void *retval;
    run = 0;

	WriteSysfs("/sys/class/graphics/fb0/blank", "0");
    signal(s, SIG_DFL);
    raise(s);
}

int main(int argc, char **argv) {
    int opt;
    int option_index = 0;

    static struct option long_options[] = {
        {"game-mode", required_argument, 0, 'g'},
        {"trace",     required_argument, 0, 't'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    /* Parse command line arguments */
    while ((opt = getopt_long(argc, argv, "g:t:h", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'g':
            g_game_mode = atoi(optarg);
            if (g_game_mode < 0 || g_game_mode > 2) {
                fprintf(stderr, "Error: Invalid game mode '%s'. Must be 0, 1, or 2.\n", optarg);
                print_usage(argv[0]);
                return 1;
            }
            break;
        case 't':
            g_trace_level = atoi(optarg);
            if (g_trace_level < 0 || g_trace_level > 3) {
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

    /* Print configuration */
    printf("streambox-tv starting with: game_mode=%d, trace_level=%d\n", g_game_mode, g_trace_level);

    TRACE(1, "main() ENTRY\n");

    /* Create uevent socket for HPD monitoring */
    g_uevent_fd = CreateUeventSocket();
    if (g_uevent_fd < 0) {
        LOGD("Warning: Failed to create uevent socket, will use polling fallback\n");
    } else {
        LOGD("Uevent socket created successfully\n");
    }

    /* Check initial HDMI TX state */
    g_hdmitx_connected = (GetHdmiTxHpdState() == 1);
    LOGD("Initial HDMI TX state: %s\n", g_hdmitx_connected ? "connected" : "disconnected");

    /* Wait for HDMI TX connection if not connected */
    if (!g_hdmitx_connected) {
        LOGD("Waiting for HDMI TX connection...\n");
        while (run && !g_hdmitx_connected) {
            if (g_uevent_fd >= 0) {
                /* Wait for uevent */
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
                /* Fallback to polling */
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
    TRACE(2, "Returned from GetInstance(), pTvClientWrapper = %p\n", pTvClientWrapper);

    TRACE(2, "About to call setTvEventCallback()\n");
    setTvEventCallback(TvEventCallback);

    TRACE(2, "About to call StopTv()\n");
    StopTv(pTvClientWrapper, g_current_source);
    TRACE(2, "Returned from StopTv()\n");

    TRACE(3, "About to sleep(1)\n");
    sleep(1);

    DisplayInit();
    TRACE(2, "Returned from DisplayInit()\n");

    /* Start TV processing */
    StartTvProcessing(pTvClientWrapper);

    /* Start uevent monitor thread for HPD changes */
    if (g_uevent_fd >= 0) {
        if (pthread_create(&g_uevent_thread, NULL, UeventMonitorThread, pTvClientWrapper) != 0) {
            LOGD("Failed to create uevent monitor thread\n");
        } else {
            LOGD("Uevent monitor thread started\n");
        }
    }

    signal(SIGINT, signal_handler);

    TRACE(1, "Entering main loop\n");
    while (run) {
        sleep(1);
    }

cleanup:
    TRACE(1, "main() cleanup\n");

    /* Wait for uevent monitor thread to exit */
    if (g_uevent_fd >= 0) {
        pthread_join(g_uevent_thread, NULL);
        close(g_uevent_fd);
        g_uevent_fd = -1;
    }

    /* Stop TV if still running */
    if (g_tv_started && g_pTvClientWrapper) {
        StopTvProcessing(g_pTvClientWrapper);
    }

    TRACE(1, "main() EXIT\n");
    return 0;
}
