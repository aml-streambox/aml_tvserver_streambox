# Config-File Design Specification

## Overview

This document specifies the config-file driven design for `streambox-tv`.

## Config File Location

- **Path:** `/etc/streambox-tv/config.json`
- **Format:** JSON
- **Reload:** SIGHUP signal or inotify file watch

## Default Values

If config file is missing or a field is omitted, these defaults are used:

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

## Field Reference

### video

| Field | Type | Values | Default | Description |
|-------|------|--------|---------|-------------|
| game_mode | int | 0, 1, 2 | 2 | Game mode level |
| vrr_mode | int | 0, 1, 2 | 2 | VRR behavior (only when game_mode=2) |
| hdmi_source | string | "HDMI1"-"HDMI4" | "HDMI2" | Input source |

**VRR Modes:**
| Mode | Name | Driver Mode | Description |
|------|------|-------------|-------------|
| 0 | Force VRR | vrr mode 1 | Always use force VRR frame lock |
| 1 | VRR On | vrr mode 0 | Use native VRR if TX/RX support |
| 2 | Auto | auto | Native VRR if supported, else force VRR |

### audio

| Field | Type | Values | Default | Description |
|-------|------|--------|---------|-------------|
| enabled | bool | true/false | true | Enable audio passthrough |
| capture_device | string | ALSA device | "hw:0,2" | HDMI RX audio device |
| playback_device | string | ALSA device | "hw:0,0" | HDMI TX audio device |
| latency_us | int | 1000-100000 | 10000 | Loop latency (µs) |
| sample_format | string | ALSA format | "S16_LE" | Sample format |
| channels | int | 2, 6, 8 | 2 | Channel count |
| sample_rate | int | 44100, 48000, etc | 48000 | Sample rate (Hz) |

### hdcp

| Field | Type | Values | Default | Description |
|-------|------|--------|---------|-------------|
| enabled | bool | true/false | false | Enable HDCP |
| version | string | "1.4", "2.2", "auto" | "auto" | HDCP version |

### debug

| Field | Type | Values | Default | Description |
|-------|------|--------|---------|-------------|
| trace_level | int | 0-3 | 0 | Trace verbosity |

## Dynamic Reload Behavior

When config is reloaded via SIGHUP:

| Section Changed | Effect |
|-----------------|--------|
| audio | Restart alsaloop with new params (no video impact) |
| video.vrr_mode | Apply new VRR settings |
| video.game_mode | Apply new game mode (may restart video) |
| video.hdmi_source | Switch source (full restart) |
| hdcp | Toggle HDCP |
| debug | Apply new trace level |

## Reload Command

```bash
# Send SIGHUP to reload config
kill -SIGHUP $(pidof streambox-tv)

# Or via systemd
systemctl reload streambox-tv
```
