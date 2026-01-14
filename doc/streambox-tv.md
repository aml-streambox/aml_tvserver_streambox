# StreamBox TV Application

`streambox-tv` is the main HDMI passthrough application for StreamBox devices.

## Usage

```bash
streambox-tv [OPTIONS]

Options:
  -g, --game-mode <0|1|2>  Game mode selection:
                             0 = disable game mode
                             1 = game mode 1 (low latency)
                             2 = game mode 2 with VRR (default)
  -t, --trace <0-3>        Trace level (0=off, 3=verbose)
  -h, --help               Display help
```

## Game Modes

| Mode | Description |
|------|-------------|
| 0 | Game mode disabled |
| 1 | Game mode 1 - low latency mode without VRR |
| 2 | Game mode 2 - low latency with VRR frame lock (default) |

## Current Hardcoded Settings

These settings will be moved to config file:

| Setting | Current Value | Location |
|---------|---------------|----------|
| HDMI Source | `SOURCE_HDMI2` | line 38 |
| Audio Capture Device | `hw:0,2` | line 61 |
| Audio Playback Device | `hw:0,0` | line 62 |
| Audio Latency | 10000µs | line 63 |
| Audio Format | S16_LE | line 64 |
| Audio Channels | 2 | line 65 |
| Audio Sample Rate | 48000 | line 66 |

## Signal Flow

```
HDMI RX Signal Stable
        ↓
SynchronizeHdmitxToHdmirx()
        ↓
  ┌─────────────────────────────────────┐
  │ 1. Read RX resolution/framerate     │
  │ 2. Determine frac_rate_policy       │
  │ 3. Set TX frac_rate_policy          │
  │ 4. Disable TX mode                  │
  │ 5. Set TX mode to match RX          │
  │ 6. Enable VRR frame lock (if mode 2)│
  └─────────────────────────────────────┘
        ↓
StartAudioPassthrough()
        ↓
  ┌─────────────────────────────────────┐
  │ Fork and exec alsaloop:             │
  │ alsaloop -C hw:0,2 -P hw:0,0        │
  │          -t 10000 -f S16_LE         │
  │          -c 2 -r 48000              │
  └─────────────────────────────────────┘
```

## Key Functions

| Function | Purpose |
|----------|---------|
| `StartTvProcessing()` | Initialize TV with game mode, VRR, ALLM |
| `StopTvProcessing()` | Stop TV, disable VRR, stop audio |
| `SynchronizeHdmitxToHdmirx()` | Match TX output to RX input timing |
| `StartAudioPassthrough()` | Fork alsaloop for audio passthrough |
| `StopAudioPassthrough()` | Kill alsaloop process |
| `TvEventCallback()` | Handle signal stable/unstable events |
| `UeventMonitorThread()` | Monitor HDMI TX hotplug events |

## Systemd Service

Service file: `/lib/systemd/system/streambox-tv.service`

```ini
[Unit]
Description=StreamBox TV HDMI Passthrough
After=tvserver.service
Requires=tvserver.service

[Service]
User=system
ExecStartPre=/bin/sleep 3
ExecStart=/usr/bin/streambox-tv
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

Control commands:
```bash
systemctl start streambox-tv
systemctl stop streambox-tv
systemctl status streambox-tv
journalctl -u streambox-tv -f
```
