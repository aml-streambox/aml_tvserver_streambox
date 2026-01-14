# Config-Driven StreamBox TV Implementation Plan

Make `streambox-tv` config-file driven with dynamic reload support and create a Cockpit plugin for configuration management.

## User Review Required

> [!IMPORTANT]
> **Config File Format Choice**: This plan uses JSON for the config file. Alternative is INI format (simpler but less structured).

> [!NOTE]
> **Reload Mechanism**: Config reload is triggered via `SIGHUP` signal (like nginx). The process watches the config file with inotify and also responds to `kill -SIGHUP <pid>`.

---

## Proposed Changes

### Component 1: Config File Schema

#### [NEW] /etc/streambox-tv/config.json

Example configuration file:

```json
{
  "video": {
    "game_mode": 2,
    "vrr_mode": 2,
    "hdmi_source": "HDMI2"
  },
  "audio": {
    "enabled": true,
    "capture_device": "hw:0,2",
    "playback_device": "hw:0,0",
    "latency_us": 10000,
    "sample_format": "S16_LE",
    "channels": 2,
    "sample_rate": 48000
  },
  "hdcp": {
    "enabled": false,
    "version": "auto"
  },
  "debug": {
    "trace_level": 0
  }
}
```

**VRR Mode Details** (only applies when `game_mode == 2`):

| vrr_mode | Name | Behavior |
|----------|------|----------|
| 0 | Force VRR Only | Always use force VRR frame lock (vrr mode 1 in driver) |
| 1 | VRR On | Modify EDID to advertise VRR support. If HDMI TX supports VRR and RX device enables it, use native VRR (vrr mode 0). Otherwise no VRR. |
| 2 | Auto (default) | If TX supports VRR and RX enables it → use native VRR. Otherwise → use force VRR as fallback. |

> [!NOTE]
> ALLM (Auto Low Latency Mode) is not exposed as a config option - the chip always operates in low latency mode when VRR is active.

> [!WARNING]
> The VRR mode logic described above may not be fully implemented in the current codebase. Additional code fixes may be required to properly support all 3 VRR modes (especially mode 1 "VRR On" with EDID modification).

---

### Component 2: Config Infrastructure (C Code)

#### [NEW] test/config.h
Config structure definitions and function declarations:

- `struct streambox_config_t` - Main config structure
- `struct video_config_t` - Video settings (game_mode, vrr_mode, source)
- `struct audio_config_t` - Audio passthrough settings
- `struct hdcp_config_t` - HDCP settings
- `struct debug_config_t` - Debug settings
- Functions: `config_load()`, `config_reload()`, `config_get()`, `config_watch_start()`, `config_watch_stop()`

#### [NEW] test/config.c
Implementation using cJSON library:

- JSON parsing with validation
- Default value handling
- inotify-based file watching for dynamic reload
- SIGHUP signal handler for manual reload trigger (like nginx)
- Thread-safe config access with mutex
- Callback system for section-specific reload handlers

---

#### [MODIFY] test/streambox-tv.c

**Key Changes:**

1. **Remove global hardcoded values** - Replace with config lookups
2. **Add config reload handlers** for audio-only, video, and HDCP changes
3. **Modify main()** to load config at startup and start file watcher
4. **Modify StartAudioPassthrough()** to use config values
5. **Modify StartTvProcessing()** to use config values

---

### Component 3: Cockpit Plugin

Located in [`doc/cockpit/`](cockpit/) within this repository.

| File | Purpose |
|------|---------|
| `backend/api.py` | REST API for config management |
| `backend/main.py` | Application entry point |
| `frontend/index.html` | Web UI |
| `frontend/streambox-config.js` | Frontend logic |
| `frontend/streambox-config.css` | Styles |
| `frontend/manifest.json` | Cockpit manifest |

---

## Verification Plan

### Unit Tests
- Test config loading with valid/invalid JSON
- Test default value handling
- Test section parsing

### Integration Tests
```bash
# Audio-only reload test
streambox-tv &
sed -i 's/"latency_us": 10000/"latency_us": 5000/' /etc/streambox-tv/config.json
kill -SIGHUP $(pidof streambox-tv)
# Verify alsaloop restarted

# VRR toggle test
sed -i 's/"vrr_mode": 2/"vrr_mode": 0/' /etc/streambox-tv/config.json
kill -SIGHUP $(pidof streambox-tv)
```

### Manual Verification
1. Deploy to device, create config file, start service
2. Open Cockpit UI, change settings via UI
3. Verify settings take effect without full restart

---

## Implementation Order

1. Create `config.h` and `config.c` with parser and file watcher
2. Modify `streambox-tv.c` to use config system
3. Create Cockpit backend API
4. Create Cockpit frontend UI
5. Create Yocto recipes and test on device

**Estimated effort:** 3-4 days
