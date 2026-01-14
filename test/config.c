/*
 * StreamBox TV Configuration Implementation
 *
 * JSON config file parser with inotify file watching and SIGHUP reload support.
 */

#define LOG_MOUDLE_TAG "TV"
#define LOG_CLASS_TAG "Config"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <errno.h>
#include <cjson/cJSON.h>

#include "config.h"

/* Maximum number of registered callbacks */
#define MAX_CALLBACKS 8

/* Static configuration storage */
static streambox_config_t g_config;
static streambox_config_t g_config_prev;
static pthread_mutex_t g_config_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_config_path[256] = CONFIG_FILE_PATH;
static int g_initialized = 0;

/* Callback storage */
static config_change_callback_t g_callbacks[MAX_CALLBACKS];
static int g_callback_count = 0;

/* File watcher state */
static int g_inotify_fd = -1;
static int g_watch_fd = -1;
static pthread_t g_watch_thread;
static volatile int g_watch_running = 0;

/* Forward declarations */
static void config_set_defaults(streambox_config_t *config);
static int config_parse_json(const char *json_str, streambox_config_t *config);
static config_change_t config_compare(const streambox_config_t *old, const streambox_config_t *new);
static void config_notify_callbacks(config_change_t changes);
static void *config_watch_thread(void *arg);
static void config_sighup_handler(int sig);

/* --- Default Values --- */

static void config_set_defaults(streambox_config_t *config)
{
    /* Video defaults */
    config->video.game_mode = 2;
    config->video.vrr_mode = 2;
    strncpy(config->video.hdmi_source, "HDMI2", sizeof(config->video.hdmi_source) - 1);

    /* Audio defaults */
    config->audio.enabled = true;
    strncpy(config->audio.capture_device, "hw:0,2", sizeof(config->audio.capture_device) - 1);
    strncpy(config->audio.playback_device, "hw:0,0", sizeof(config->audio.playback_device) - 1);
    config->audio.latency_us = 10000;
    strncpy(config->audio.sample_format, "S16_LE", sizeof(config->audio.sample_format) - 1);
    config->audio.channels = 2;
    config->audio.sample_rate = 48000;

    /* HDCP defaults */
    config->hdcp.enabled = false;
    strncpy(config->hdcp.version, "auto", sizeof(config->hdcp.version) - 1);

    /* Debug defaults */
    config->debug.trace_level = 0;
}

/* --- JSON Parsing --- */

static const char *json_get_string(cJSON *obj, const char *key, const char *default_val)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return default_val;
}

static int json_get_int(cJSON *obj, const char *key, int default_val)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return default_val;
}

static bool json_get_bool(cJSON *obj, const char *key, bool default_val)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return default_val;
}

static int config_parse_json(const char *json_str, streambox_config_t *config)
{
    cJSON *root = NULL;
    cJSON *section = NULL;
    const char *str_val;

    /* Set defaults first */
    config_set_defaults(config);

    if (json_str == NULL || strlen(json_str) == 0) {
        return 0; /* Empty file, use defaults */
    }

    root = cJSON_Parse(json_str);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        fprintf(stderr, "[Config] JSON parse error: %s\n", error_ptr ? error_ptr : "unknown");
        return -1;
    }

    /* Parse video section */
    section = cJSON_GetObjectItemCaseSensitive(root, "video");
    if (cJSON_IsObject(section)) {
        config->video.game_mode = json_get_int(section, "game_mode", 2);
        config->video.vrr_mode = json_get_int(section, "vrr_mode", 2);
        str_val = json_get_string(section, "hdmi_source", "HDMI2");
        strncpy(config->video.hdmi_source, str_val, sizeof(config->video.hdmi_source) - 1);

        /* Validate ranges */
        if (config->video.game_mode < 0 || config->video.game_mode > 2) {
            config->video.game_mode = 2;
        }
        if (config->video.vrr_mode < 0 || config->video.vrr_mode > 2) {
            config->video.vrr_mode = 2;
        }
    }

    /* Parse audio section */
    section = cJSON_GetObjectItemCaseSensitive(root, "audio");
    if (cJSON_IsObject(section)) {
        config->audio.enabled = json_get_bool(section, "enabled", true);
        
        str_val = json_get_string(section, "capture_device", "hw:0,2");
        strncpy(config->audio.capture_device, str_val, sizeof(config->audio.capture_device) - 1);
        
        str_val = json_get_string(section, "playback_device", "hw:0,0");
        strncpy(config->audio.playback_device, str_val, sizeof(config->audio.playback_device) - 1);
        
        config->audio.latency_us = json_get_int(section, "latency_us", 10000);
        
        str_val = json_get_string(section, "sample_format", "S16_LE");
        strncpy(config->audio.sample_format, str_val, sizeof(config->audio.sample_format) - 1);
        
        config->audio.channels = json_get_int(section, "channels", 2);
        config->audio.sample_rate = json_get_int(section, "sample_rate", 48000);

        /* Validate ranges */
        if (config->audio.latency_us < 1000) config->audio.latency_us = 1000;
        if (config->audio.latency_us > 100000) config->audio.latency_us = 100000;
        if (config->audio.channels < 1) config->audio.channels = 2;
        if (config->audio.channels > 8) config->audio.channels = 8;
    }

    /* Parse hdcp section */
    section = cJSON_GetObjectItemCaseSensitive(root, "hdcp");
    if (cJSON_IsObject(section)) {
        config->hdcp.enabled = json_get_bool(section, "enabled", false);
        str_val = json_get_string(section, "version", "auto");
        strncpy(config->hdcp.version, str_val, sizeof(config->hdcp.version) - 1);
    }

    /* Parse debug section */
    section = cJSON_GetObjectItemCaseSensitive(root, "debug");
    if (cJSON_IsObject(section)) {
        config->debug.trace_level = json_get_int(section, "trace_level", 0);
        if (config->debug.trace_level < 0) config->debug.trace_level = 0;
        if (config->debug.trace_level > 3) config->debug.trace_level = 3;
    }

    cJSON_Delete(root);
    return 0;
}

/* --- Configuration Comparison --- */

static config_change_t config_compare(const streambox_config_t *old, const streambox_config_t *new)
{
    config_change_t changes = CONFIG_CHANGE_NONE;

    /* Compare video */
    if (old->video.game_mode != new->video.game_mode ||
        old->video.vrr_mode != new->video.vrr_mode ||
        strcmp(old->video.hdmi_source, new->video.hdmi_source) != 0) {
        changes |= CONFIG_CHANGE_VIDEO;
    }

    /* Compare audio */
    if (old->audio.enabled != new->audio.enabled ||
        strcmp(old->audio.capture_device, new->audio.capture_device) != 0 ||
        strcmp(old->audio.playback_device, new->audio.playback_device) != 0 ||
        old->audio.latency_us != new->audio.latency_us ||
        strcmp(old->audio.sample_format, new->audio.sample_format) != 0 ||
        old->audio.channels != new->audio.channels ||
        old->audio.sample_rate != new->audio.sample_rate) {
        changes |= CONFIG_CHANGE_AUDIO;
    }

    /* Compare hdcp */
    if (old->hdcp.enabled != new->hdcp.enabled ||
        strcmp(old->hdcp.version, new->hdcp.version) != 0) {
        changes |= CONFIG_CHANGE_HDCP;
    }

    /* Compare debug */
    if (old->debug.trace_level != new->debug.trace_level) {
        changes |= CONFIG_CHANGE_DEBUG;
    }

    return changes;
}

static void config_notify_callbacks(config_change_t changes)
{
    int i;
    if (changes == CONFIG_CHANGE_NONE) {
        return;
    }

    for (i = 0; i < g_callback_count; i++) {
        if (g_callbacks[i] != NULL) {
            g_callbacks[i](changes, &g_config_prev, &g_config);
        }
    }
}

/* --- File Reading --- */

static char *read_file_contents(const char *path)
{
    FILE *fp = NULL;
    char *buffer = NULL;
    long file_size = 0;
    size_t bytes_read = 0;

    fp = fopen(path, "r");
    if (fp == NULL) {
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 1024 * 1024) { /* Max 1MB */
        fclose(fp);
        return NULL;
    }

    buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL) {
        fclose(fp);
        return NULL;
    }

    bytes_read = fread(buffer, 1, file_size, fp);
    buffer[bytes_read] = '\0';
    fclose(fp);

    return buffer;
}

/* --- Public API --- */

int config_init(void)
{
    pthread_mutex_lock(&g_config_mutex);
    
    config_set_defaults(&g_config);
    memcpy(&g_config_prev, &g_config, sizeof(streambox_config_t));
    g_callback_count = 0;
    g_initialized = 1;
    
    pthread_mutex_unlock(&g_config_mutex);
    
    printf("[Config] Initialized with defaults\n");
    return 0;
}

int config_load(const char *path)
{
    char *json_str = NULL;
    streambox_config_t new_config;
    int ret = 0;

    if (!g_initialized) {
        config_init();
    }

    if (path != NULL) {
        strncpy(g_config_path, path, sizeof(g_config_path) - 1);
    }

    json_str = read_file_contents(g_config_path);
    if (json_str == NULL) {
        printf("[Config] Config file not found: %s (using defaults)\n", g_config_path);
        return 0; /* Not an error - use defaults */
    }

    ret = config_parse_json(json_str, &new_config);
    free(json_str);

    if (ret == 0) {
        pthread_mutex_lock(&g_config_mutex);
        memcpy(&g_config, &new_config, sizeof(streambox_config_t));
        pthread_mutex_unlock(&g_config_mutex);
        printf("[Config] Loaded from: %s\n", g_config_path);
    } else {
        printf("[Config] Parse error, using defaults\n");
    }

    return ret;
}

int config_reload(void)
{
    char *json_str = NULL;
    streambox_config_t new_config;
    config_change_t changes;
    int ret = 0;

    if (!g_initialized) {
        return -1;
    }

    printf("[Config] Reloading configuration...\n");

    json_str = read_file_contents(g_config_path);
    if (json_str == NULL) {
        printf("[Config] Config file not found: %s\n", g_config_path);
        return -1;
    }

    ret = config_parse_json(json_str, &new_config);
    free(json_str);

    if (ret != 0) {
        printf("[Config] Parse error during reload\n");
        return ret;
    }

    pthread_mutex_lock(&g_config_mutex);
    
    /* Save old config and update current */
    memcpy(&g_config_prev, &g_config, sizeof(streambox_config_t));
    changes = config_compare(&g_config_prev, &new_config);
    memcpy(&g_config, &new_config, sizeof(streambox_config_t));
    
    pthread_mutex_unlock(&g_config_mutex);

    printf("[Config] Reload complete, changes: 0x%x\n", changes);
    
    /* Notify callbacks outside of lock */
    config_notify_callbacks(changes);

    return 0;
}

const streambox_config_t *config_get(void)
{
    return &g_config;
}

int config_register_callback(config_change_callback_t callback)
{
    if (callback == NULL || g_callback_count >= MAX_CALLBACKS) {
        return -1;
    }

    g_callbacks[g_callback_count++] = callback;
    return 0;
}

/* --- File Watcher --- */

static void config_sighup_handler(int sig)
{
    (void)sig;
    printf("[Config] Received SIGHUP, reloading...\n");
    config_reload();
}

static void *config_watch_thread(void *arg)
{
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;
    ssize_t len;
    char *ptr;

    (void)arg;

    printf("[Config] File watcher thread started\n");

    while (g_watch_running) {
        len = read(g_inotify_fd, buf, sizeof(buf));
        if (len <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(100000); /* 100ms */
                continue;
            }
            break;
        }

        for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *)ptr;
            
            if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE)) {
                printf("[Config] File change detected, reloading...\n");
                usleep(50000); /* Small delay for writer to finish */
                config_reload();
            }
        }
    }

    printf("[Config] File watcher thread exiting\n");
    return NULL;
}

int config_watch_start(void)
{
    struct sigaction sa;
    char dir_path[256];
    char *last_slash;

    if (g_watch_running) {
        return 0; /* Already running */
    }

    /* Set up SIGHUP handler */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = config_sighup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGHUP, &sa, NULL) < 0) {
        perror("[Config] Failed to set SIGHUP handler");
    }

    /* Create inotify instance */
    g_inotify_fd = inotify_init1(IN_NONBLOCK);
    if (g_inotify_fd < 0) {
        perror("[Config] inotify_init failed");
        return -1;
    }

    /* Watch the config file's directory (for create events) and file itself */
    strncpy(dir_path, g_config_path, sizeof(dir_path) - 1);
    last_slash = strrchr(dir_path, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
        inotify_add_watch(g_inotify_fd, dir_path, IN_CREATE | IN_MOVED_TO);
    }

    /* Watch the config file itself */
    g_watch_fd = inotify_add_watch(g_inotify_fd, g_config_path, IN_MODIFY | IN_CLOSE_WRITE);
    if (g_watch_fd < 0 && errno != ENOENT) {
        perror("[Config] inotify_add_watch failed");
        /* Continue anyway - SIGHUP still works */
    }

    /* Start watcher thread */
    g_watch_running = 1;
    if (pthread_create(&g_watch_thread, NULL, config_watch_thread, NULL) != 0) {
        perror("[Config] pthread_create failed");
        g_watch_running = 0;
        close(g_inotify_fd);
        g_inotify_fd = -1;
        return -1;
    }

    printf("[Config] File watcher started for: %s\n", g_config_path);
    return 0;
}

void config_watch_stop(void)
{
    if (!g_watch_running) {
        return;
    }

    g_watch_running = 0;

    if (g_inotify_fd >= 0) {
        close(g_inotify_fd);
        g_inotify_fd = -1;
    }

    pthread_join(g_watch_thread, NULL);
    printf("[Config] File watcher stopped\n");
}

void config_cleanup(void)
{
    config_watch_stop();
    
    pthread_mutex_lock(&g_config_mutex);
    g_initialized = 0;
    g_callback_count = 0;
    pthread_mutex_unlock(&g_config_mutex);
    
    printf("[Config] Cleanup complete\n");
}

int config_get_source_input(const char *source_str)
{
    /* SOURCE_HDMI1 = 5, SOURCE_HDMI2 = 6, SOURCE_HDMI3 = 7, SOURCE_HDMI4 = 8
     * See TvCommon.h tv_source_input_t enum
     */
    if (source_str == NULL) {
        return 6; /* SOURCE_HDMI2 */
    }

    if (strcmp(source_str, "HDMI1") == 0) return 5;  /* SOURCE_HDMI1 */
    if (strcmp(source_str, "HDMI2") == 0) return 6;  /* SOURCE_HDMI2 */
    if (strcmp(source_str, "HDMI3") == 0) return 7;  /* SOURCE_HDMI3 */
    if (strcmp(source_str, "HDMI4") == 0) return 8;  /* SOURCE_HDMI4 */

    return 6; /* Default: SOURCE_HDMI2 */
}
