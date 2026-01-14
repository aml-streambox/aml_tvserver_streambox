# TvClient API Reference

The client library provides a C-compatible API for controlling the TV server.

## C API (TvClientWrapper.h)

### Initialization

```c
struct TvClientWrapper_t *GetInstance(void);
```
Get singleton instance of TV client.

### Event Callback

```c
typedef enum {
    TV_EVENT_TYPE_SIGLE_DETECT,
    TV_EVENT_TYPE_SOURCE_CONNECT
} event_type_t;

typedef void (*TvEventCallback_t)(event_type_t eventType, void *eventData);

void setTvEventCallback(TvEventCallback_t callback);
```

### Source Control

```c
int StartTv(struct TvClientWrapper_t *wrapper, tv_source_input_t source);
int StopTv(struct TvClientWrapper_t *wrapper, tv_source_input_t source);
```

**Source Input Values:**
| Value | Name |
|-------|------|
| 0 | SOURCE_HDMI1 |
| 1 | SOURCE_HDMI2 |
| 2 | SOURCE_HDMI3 |
| 3 | SOURCE_HDMI4 |

### Video Info

```c
int GetCurrentSourceFrameWidth(struct TvClientWrapper_t *wrapper);
int GetCurrentSourceFrameHeight(struct TvClientWrapper_t *wrapper);
int GetCurrentSourceFrameFps(struct TvClientWrapper_t *wrapper);
```

### Game Mode / VRR / ALLM

```c
int SetGameMode(struct TvClientWrapper_t *wrapper, int enable, int game_mode_value);
int SetHdmiAllmEnabled(struct TvClientWrapper_t *wrapper, int enable);
int SetHdmiVrrEnabled(struct TvClientWrapper_t *wrapper, int enable);
int GetHdmiVrrEnabled(struct TvClientWrapper_t *wrapper);
```

**Game Mode Values:**
| Value | Meaning |
|-------|---------|
| 0 | Disabled |
| 1 | Game mode 1 |
| 3 | Game mode 2 (with VRR) |

### HDCP Control

```c
int SetHdmiHdcpEnabled(struct TvClientWrapper_t *wrapper, int enable);
int SetHdmiHdcpVersion(struct TvClientWrapper_t *wrapper, int version);
```

**HDCP Version:**
| Value | Version |
|-------|---------|
| 0 | HDCP 1.4 |
| 1 | HDCP 2.2 |

### EDID Control

```c
int SetEdidVersion(struct TvClientWrapper_t *wrapper, tv_source_input_t source, int version);
int GetEdidVersion(struct TvClientWrapper_t *wrapper, tv_source_input_t source);
```

---

## Event Structures

### Signal Detect Event

```c
typedef struct SignalDetectCallback_s {
    tv_source_input_t SourceInput;
    tvin_sig_fmt_t SignalFmt;
    tvin_trans_fmt TransFmt;
    tvin_sig_status_t SignalStatus;
    int isDviSignal;
} SignalDetectCallback_t;
```

**Signal Status Values:**
| Value | Name | Description |
|-------|------|-------------|
| 0 | TVIN_SIG_STATUS_NULL | Unknown |
| 1 | TVIN_SIG_STATUS_NOSIG | No signal |
| 2 | TVIN_SIG_STATUS_STABLE | Signal stable |
| 3 | TVIN_SIG_STATUS_UNSTABLE | Signal unstable |

### Source Connect Event

```c
typedef struct SourceConnectCallback_s {
    tv_source_input_t SourceInput;
    int ConnectionState;
} SourceConnectCallback_t;
```
