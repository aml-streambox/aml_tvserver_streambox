/**
 * StreamBox Config - Frontend JavaScript
 */

const API_BASE = '/streambox-config/api';

let currentConfig = null;

// --- API Functions ---

async function fetchConfig() {
    const response = await fetch(`${API_BASE}/config`);
    if (!response.ok) throw new Error('Failed to fetch config');
    return response.json();
}

async function saveConfig(config) {
    const response = await fetch(`${API_BASE}/config`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
    });
    if (!response.ok) throw new Error('Failed to save config');
    return response.json();
}

async function fetchStatus() {
    const response = await fetch(`${API_BASE}/status`);
    if (!response.ok) throw new Error('Failed to fetch status');
    return response.json();
}

async function reloadConfig() {
    const response = await fetch(`${API_BASE}/reload`, { method: 'POST' });
    if (!response.ok) throw new Error('Failed to reload config');
    return response.json();
}

async function fetchDefaults() {
    const response = await fetch(`${API_BASE}/defaults`);
    if (!response.ok) throw new Error('Failed to fetch defaults');
    return response.json();
}

// --- UI Functions ---

function showToast(message, type = 'info') {
    const toast = document.getElementById('toast');
    toast.textContent = message;
    toast.className = `toast toast-${type} show`;
    setTimeout(() => toast.classList.remove('show'), 3000);
}

function updateStatusBadge(status) {
    const badge = document.getElementById('service-status');
    if (status.active) {
        badge.textContent = `Running (PID: ${status.pid})`;
        badge.className = 'status-badge status-active';
    } else {
        badge.textContent = status.status || 'Stopped';
        badge.className = 'status-badge status-inactive';
    }
}

function populateForm(config) {
    // Video
    document.getElementById('game-mode').value = config.video.game_mode;
    document.getElementById('vrr-mode').value = config.video.vrr_mode;
    document.getElementById('hdmi-source').value = config.video.hdmi_source;

    // Update VRR mode visibility
    updateVrrModeVisibility();

    // Audio
    document.getElementById('audio-enabled').checked = config.audio.enabled;
    document.getElementById('capture-device').value = config.audio.capture_device;
    document.getElementById('playback-device').value = config.audio.playback_device;
    document.getElementById('latency').value = config.audio.latency_us;
    document.getElementById('sample-rate').value = config.audio.sample_rate;
    document.getElementById('sample-format').value = config.audio.sample_format;
    document.getElementById('channels').value = config.audio.channels;

    // HDCP
    document.getElementById('hdcp-enabled').checked = config.hdcp.enabled;
    document.getElementById('hdcp-version').value = config.hdcp.version;

    // Debug
    document.getElementById('trace-level').value = config.debug.trace_level;
}

function collectFormData() {
    return {
        video: {
            game_mode: parseInt(document.getElementById('game-mode').value),
            vrr_mode: parseInt(document.getElementById('vrr-mode').value),
            hdmi_source: document.getElementById('hdmi-source').value
        },
        audio: {
            enabled: document.getElementById('audio-enabled').checked,
            capture_device: document.getElementById('capture-device').value,
            playback_device: document.getElementById('playback-device').value,
            latency_us: parseInt(document.getElementById('latency').value),
            sample_rate: parseInt(document.getElementById('sample-rate').value),
            sample_format: document.getElementById('sample-format').value,
            channels: parseInt(document.getElementById('channels').value)
        },
        hdcp: {
            enabled: document.getElementById('hdcp-enabled').checked,
            version: document.getElementById('hdcp-version').value
        },
        debug: {
            trace_level: parseInt(document.getElementById('trace-level').value)
        }
    };
}

function updateVrrModeVisibility() {
    const gameMode = parseInt(document.getElementById('game-mode').value);
    const vrrGroup = document.getElementById('vrr-mode-group');
    vrrGroup.style.display = gameMode === 2 ? 'block' : 'none';
}

// --- Event Handlers ---

async function handleApply() {
    try {
        const config = collectFormData();
        const result = await saveConfig(config);

        if (result.success) {
            currentConfig = config;
            showToast('Configuration saved and reloaded', 'success');
        } else {
            showToast('Failed to save configuration', 'error');
        }
    } catch (error) {
        showToast(`Error: ${error.message}`, 'error');
    }
}

async function handleReload() {
    try {
        const result = await reloadConfig();
        if (result.success) {
            showToast('Configuration reloaded', 'success');
        } else {
            showToast(result.message || 'Reload failed', 'error');
        }
    } catch (error) {
        showToast(`Error: ${error.message}`, 'error');
    }
}

async function handleReset() {
    try {
        const defaults = await fetchDefaults();
        populateForm(defaults);
        showToast('Form reset to defaults (not saved yet)', 'info');
    } catch (error) {
        showToast(`Error: ${error.message}`, 'error');
    }
}

// --- Initialization ---

async function init() {
    try {
        // Load current config
        currentConfig = await fetchConfig();
        populateForm(currentConfig);

        // Load service status
        const status = await fetchStatus();
        updateStatusBadge(status);

        // Refresh status periodically
        setInterval(async () => {
            try {
                const status = await fetchStatus();
                updateStatusBadge(status);
            } catch (e) {
                console.error('Failed to refresh status:', e);
            }
        }, 5000);

    } catch (error) {
        showToast(`Failed to load configuration: ${error.message}`, 'error');
    }
}

// --- Event Listeners ---

document.getElementById('apply-btn').addEventListener('click', handleApply);
document.getElementById('reload-btn').addEventListener('click', handleReload);
document.getElementById('reset-btn').addEventListener('click', handleReset);
document.getElementById('game-mode').addEventListener('change', updateVrrModeVisibility);

// Initialize on load
document.addEventListener('DOMContentLoaded', init);
