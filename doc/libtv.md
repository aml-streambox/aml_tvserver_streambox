# Core TV Library (libtv)

The `libtv` directory contains the core C++ library for TV control.

## Components

### CTv.cpp - Main TV Controller

Primary class that orchestrates TV operations.

**Key Methods:**

| Method | Description |
|--------|-------------|
| `StartTv(source)` | Start TV with specified source input |
| `StopTv(source)` | Stop TV from specified source |
| `SwitchSource(dest)` | Switch between sources |
| `SetGameMode(enable, value)` | Enable/disable game mode |
| `SetEdidVersion(source, ver)` | Set EDID version (1.4/2.0) |
| `SetColorRangeMode(mode)` | Set color range (auto/full/limited) |

**Signal Callbacks:**

| Callback | Trigger |
|----------|---------|
| `onSigToStable()` | Signal becomes stable |
| `onSigToUnstable()` | Signal becomes unstable |
| `onSigToNoSig()` | No signal detected |
| `onSigVrrChange()` | VRR state changed |
| `onSigDvAllmChange()` | ALLM state changed |

---

### CHDMIRxManager.cpp - HDMI RX Control

Manages HDMI receiver hardware.

**Key Methods:**

| Method | Description |
|--------|-------------|
| `HDMIRxOpenMoudle()` | Open HDMI RX device |
| `HdmiRxEdidDataSwitch()` | Load EDID data |
| `HdmiRxEdidVerSwitch()` | Set EDID version |
| `HdmiRxHdcpOnOff()` | Enable/disable HDCP |
| `HdmiRxHdcpVerSwitch()` | Force HDCP version |
| `SetAllmEnabled()` | Enable/disable ALLM |
| `SetVrrEnabled()` | Enable/disable VRR |
| `PassthroughEdidFromTxToRx()` | Copy TX EDID to RX |
| `PatchEdidFor120Hz()` | Patch EDID for 120Hz support |

**Device Path:** `/dev/hdmirx0`

**IOCTL Commands:**
| Command | Value | Purpose |
|---------|-------|---------|
| `HDMI_IOC_HDCP_ON` | 0x01 | Enable HDCP |
| `HDMI_IOC_HDCP_OFF` | 0x02 | Disable HDCP |
| `HDMI_IOC_HDCP22_AUTO` | 0x06 | Auto HDCP 2.2/1.4 |
| `HDMI_IOC_HDCP22_FORCE14` | 0x07 | Force HDCP 1.4 |

---

### CTvin.cpp - Video Input

Controls Video Digitizer IN (VDIN).

**Key Methods:**

| Method | Description |
|--------|-------------|
| `Tvin_OpenPort()` | Open VDIN port |
| `Tvin_ClosePort()` | Close VDIN port |
| `Tvin_StartDec()` | Start video decoding |
| `Tvin_StopDec()` | Stop video decoding |
| `Tvin_SetGameMode()` | Set game mode value |
| `Tvin_GetSignalInfo()` | Get signal info (fmt, fps, etc.) |
| `VDIN_GetFrontendInfo()` | Get frontend timing info |

**Device Path:** `/dev/vdin0`

---

### CTvAudio.cpp - Audio Patch

Manages audio routing through audio HAL.

**Key Methods:**

| Method | Description |
|--------|-------------|
| `create_audio_patch()` | Create audio routing patch |
| `release_audio_patch()` | Release audio patch |
| `set_audio_av_mute()` | Mute/unmute audio |

**Note:** Currently not used in StreamBox - audio passthrough is handled by `alsaloop` instead.

---

## Build Defines

| Define | Purpose |
|--------|---------|
| `STREAM_BOX` | Enable StreamBox-specific code paths |
| `STREAM_BOX_TRACE` | Enable verbose tracing |
| `STREAM_BOX_LEGACY` | Skip init_check() for legacy HAL |
