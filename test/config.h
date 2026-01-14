/*
 * StreamBox TV Configuration Header
 *
 * This file defines the configuration structures and functions
 * for the config-file driven design.
 */

#ifndef STREAMBOX_CONFIG_H
#define STREAMBOX_CONFIG_H

#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default config file path */
#define CONFIG_FILE_PATH "/etc/streambox-tv/config.json"

/* --- Configuration Structures --- */

/* Video configuration */
typedef struct {
    int game_mode;      /* 0=disabled, 1=mode1, 2=mode2+VRR (default: 2) */
    int vrr_mode;       /* 0=force VRR, 1=VRR on (EDID), 2=auto (default: 2) */
    char hdmi_source[16]; /* "HDMI1"-"HDMI4" (default: "HDMI2") */
} video_config_t;

/* Audio configuration */
typedef struct {
    bool enabled;               /* Enable audio passthrough (default: true) */
    char capture_device[32];    /* ALSA capture device (default: "hw:0,2") */
    char playback_device[32];   /* ALSA playback device (default: "hw:0,0") */
    int latency_us;             /* Latency in microseconds (default: 10000) */
    char sample_format[16];     /* Sample format (default: "S16_LE") */
    int channels;               /* Channel count (default: 2) */
    int sample_rate;            /* Sample rate (default: 48000) */
} audio_config_t;

/* HDCP configuration */
typedef struct {
    bool enabled;           /* Enable HDCP (default: false) */
    char version[8];        /* "auto", "1.4", "2.2" (default: "auto") */
} hdcp_config_t;

/* Debug configuration */
typedef struct {
    int trace_level;        /* 0-3 (default: 0) */
} debug_config_t;

/* Main configuration structure */
typedef struct {
    video_config_t video;
    audio_config_t audio;
    hdcp_config_t hdcp;
    debug_config_t debug;
} streambox_config_t;

/* --- Callback Types --- */

/* Config section changed flags */
typedef enum {
    CONFIG_CHANGE_NONE   = 0,
    CONFIG_CHANGE_VIDEO  = (1 << 0),
    CONFIG_CHANGE_AUDIO  = (1 << 1),
    CONFIG_CHANGE_HDCP   = (1 << 2),
    CONFIG_CHANGE_DEBUG  = (1 << 3),
    CONFIG_CHANGE_ALL    = 0xFFFF
} config_change_t;

/* Callback function type for config changes */
typedef void (*config_change_callback_t)(config_change_t changes, const streambox_config_t *old_config, const streambox_config_t *new_config);

/* --- Public API --- */

/**
 * Initialize configuration with defaults
 * Must be called before any other config functions
 * @return 0 on success, -1 on error
 */
int config_init(void);

/**
 * Load configuration from file
 * Falls back to defaults if file doesn't exist or is invalid
 * @param path Config file path (NULL for default)
 * @return 0 on success, -1 on error (defaults still loaded)
 */
int config_load(const char *path);

/**
 * Reload configuration from file
 * Triggers callbacks for changed sections
 * @return 0 on success, -1 on error
 */
int config_reload(void);

/**
 * Get current configuration (read-only)
 * @return Pointer to current config (never NULL after init)
 */
const streambox_config_t *config_get(void);

/**
 * Register callback for config changes
 * @param callback Function to call on config change
 * @return 0 on success, -1 on error
 */
int config_register_callback(config_change_callback_t callback);

/**
 * Start config file watcher (inotify)
 * Also sets up SIGHUP handler
 * @return 0 on success, -1 on error
 */
int config_watch_start(void);

/**
 * Stop config file watcher
 */
void config_watch_stop(void);

/**
 * Clean up configuration resources
 */
void config_cleanup(void);

/**
 * Convert HDMI source string to tv_source_input_t enum value
 * @param source_str Source string ("HDMI1"-"HDMI4")
 * @return Source enum value (default: SOURCE_HDMI2)
 */
int config_get_source_input(const char *source_str);

#ifdef __cplusplus
}
#endif

#endif /* STREAMBOX_CONFIG_H */
