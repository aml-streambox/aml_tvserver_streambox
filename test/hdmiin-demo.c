#define LOG_MOUDLE_TAG "TV"
#define LOG_CLASS_TAG "TvTest-c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <TvClientWrapper.h>
#include <CTvClientLog.h>

static int run = 1;

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
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: main() ENTRY\n");
    fflush(stdout);
#endif
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: About to call GetInstance()\n");
    fflush(stdout);
#endif
    struct TvClientWrapper_t * pTvClientWrapper = GetInstance();
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: Returned from GetInstance(), pTvClientWrapper = %p\n", pTvClientWrapper);
    fflush(stdout);
#endif
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: About to call setTvEventCallback()\n");
    fflush(stdout);
#endif
    setTvEventCallback(TvEventCallback);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: About to call StopTv(SOURCE_HDMI2)\n");
    fflush(stdout);
#endif
    tv_source_input_t CurrentSource = SOURCE_HDMI2;
	StopTv(pTvClientWrapper, CurrentSource);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: Returned from StopTv()\n");
    fflush(stdout);
#endif

#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: About to sleep(1)\n");
    fflush(stdout);
#endif
	sleep(1);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: Woke up from sleep, about to call DisplayInit()\n");
    fflush(stdout);
#endif

    DisplayInit();
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: Returned from DisplayInit()\n");
    fflush(stdout);
#endif

#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: STREAM_BOX is defined, enabling ALLM/VRR before StartTv\n");
    fflush(stdout);
#endif

#ifdef STREAM_BOX
    // Enable ALLM (Auto Low Latency Mode) before StartTv
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: About to enable ALLM\n");
    fflush(stdout);
#endif
    int ret = SetHdmiAllmEnabled(pTvClientWrapper, 1);
    if (ret == 0) {
        LOGD("ALLM enabled successfully\n");
    } else {
        LOGD("Failed to enable ALLM, ret=%d\n", ret);
    }

    // Enable VRR (Variable Refresh Rate) before StartTv
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: About to enable VRR\n");
    fflush(stdout);
#endif
    ret = SetHdmiVrrEnabled(pTvClientWrapper, 1);
    if (ret == 0) {
        LOGD("VRR enabled successfully\n");
    } else {
        LOGD("Failed to enable VRR, ret=%d\n", ret);
    }

    // Force Game Mode for low latency (works even if source doesn't send ALLM)
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: About to enable Game Mode for low latency\n");
    fflush(stdout);
#endif
    ret = SetGameMode(pTvClientWrapper, 1);
    if (ret == 0) {
        LOGD("Game Mode enabled successfully\n");
    } else {
        LOGD("Failed to enable Game Mode, ret=%d\n", ret);
    }

    // Force PC Mode for low latency passthrough
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: About to enable PC Mode for low latency\n");
    fflush(stdout);
#endif
    ret = SetPcMode(pTvClientWrapper, 1);
    if (ret == 0) {
        LOGD("PC Mode enabled successfully\n");
    } else {
        LOGD("Failed to enable PC Mode, ret=%d\n", ret);
    }
#else
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: STREAM_BOX is NOT defined, skipping ALLM/VRR\n");
    fflush(stdout);
#endif
#endif

#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: About to call StartTv()\n");
    fflush(stdout);
#endif
    StartTv(pTvClientWrapper, CurrentSource);
#ifdef STREAM_BOX_TRACE
    printf("[TRACE] hdmiin-demo: Returned from StartTv()\n");
    fflush(stdout);
#endif

	signal(SIGINT, signal_handler);

	while (run)
	{
		sleep(1);
	}

    return 0;
}