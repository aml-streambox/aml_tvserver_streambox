# StreamBox Config - Cockpit Plugin

Cockpit web UI plugin for managing StreamBox TV configuration.

## Directory Structure

```
cockpit/
├── README.md           # This file
├── backend/
│   ├── api.py          # REST API implementation
│   └── main.py         # Application entry point
├── frontend/
│   ├── index.html      # Main UI
│   ├── streambox-config.js
│   ├── streambox-config.css
│   └── manifest.json   # Cockpit manifest
└── yocto/
    └── cockpit-streambox-config.bb  # Yocto recipe
```

## Installation

### Manual Install

```bash
# Install backend
cp -r backend /usr/share/cockpit-streambox-config/
chmod +x /usr/share/cockpit-streambox-config/main.py

# Install frontend
cp -r frontend /usr/share/cockpit/streambox-config/

# Enable service
systemctl enable --now cockpit-streambox-config.socket
```

### Yocto Build

Add to your image:
```
IMAGE_INSTALL += "cockpit-streambox-config"
```

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/config` | GET | Get current config |
| `/api/config` | PUT | Update entire config |
| `/api/config/<section>` | PATCH | Update specific section |
| `/api/status` | GET | Get streambox-tv service status |
| `/api/reload` | POST | Trigger config reload (SIGHUP) |

## Usage

1. Open Cockpit: `https://<device-ip>:9090`
2. Navigate to "StreamBox Config" in sidebar
3. Modify settings as needed
4. Click "Apply" to save and reload
