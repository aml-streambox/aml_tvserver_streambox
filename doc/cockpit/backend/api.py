#!/usr/bin/env python3
"""
StreamBox Config API - Backend for Cockpit plugin

REST API for managing StreamBox TV configuration.
"""

import os
import json
import signal
import subprocess
from flask import Flask, jsonify, request

app = Flask(__name__)

CONFIG_PATH = '/etc/streambox-tv/config.json'
SERVICE_NAME = 'streambox-tv'

# Default configuration
DEFAULT_CONFIG = {
    "video": {
        "game_mode": 2,
        "vrr_mode": 2,
        "hdmi_source": "HDMI2"
    },
    "audio": {
        "enabled": True,
        "capture_device": "hw:0,2",
        "playback_device": "hw:0,0",
        "latency_us": 10000,
        "sample_format": "S16_LE",
        "channels": 2,
        "sample_rate": 48000
    },
    "hdcp": {
        "enabled": False,
        "version": "auto"
    },
    "debug": {
        "trace_level": 0
    }
}


def load_config():
    """Load config from file, return defaults if missing."""
    try:
        with open(CONFIG_PATH, 'r') as f:
            return json.load(f)
    except FileNotFoundError:
        return DEFAULT_CONFIG.copy()
    except json.JSONDecodeError as e:
        app.logger.error(f"Invalid JSON in config: {e}")
        return DEFAULT_CONFIG.copy()


def save_config(config):
    """Save config to file."""
    os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
    with open(CONFIG_PATH, 'w') as f:
        json.dump(config, f, indent=2)


def reload_service():
    """Send SIGHUP to streambox-tv to reload config."""
    try:
        result = subprocess.run(
            ['pidof', SERVICE_NAME],
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            pid = int(result.stdout.strip().split()[0])
            os.kill(pid, signal.SIGHUP)
            return True, f"Sent SIGHUP to PID {pid}"
        else:
            return False, "Service not running"
    except Exception as e:
        return False, str(e)


def get_service_status():
    """Get systemd service status."""
    try:
        result = subprocess.run(
            ['systemctl', 'is-active', SERVICE_NAME],
            capture_output=True,
            text=True
        )
        active = result.stdout.strip()
        
        result = subprocess.run(
            ['systemctl', 'show', SERVICE_NAME, '--property=MainPID'],
            capture_output=True,
            text=True
        )
        pid = result.stdout.strip().split('=')[1] if '=' in result.stdout else None
        
        return {
            "service": SERVICE_NAME,
            "active": active == "active",
            "status": active,
            "pid": int(pid) if pid and pid != '0' else None
        }
    except Exception as e:
        return {"error": str(e)}


# --- API Endpoints ---

@app.route('/api/config', methods=['GET'])
def get_config():
    """Get current configuration."""
    return jsonify(load_config())


@app.route('/api/config', methods=['PUT'])
def update_config():
    """Update entire configuration."""
    try:
        new_config = request.get_json()
        if not new_config:
            return jsonify({"error": "No config provided"}), 400
        
        save_config(new_config)
        success, message = reload_service()
        
        return jsonify({
            "success": True,
            "reload": {"success": success, "message": message}
        })
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route('/api/config/<section>', methods=['GET'])
def get_config_section(section):
    """Get specific config section."""
    config = load_config()
    if section in config:
        return jsonify(config[section])
    else:
        return jsonify({"error": f"Unknown section: {section}"}), 404


@app.route('/api/config/<section>', methods=['PATCH'])
def update_config_section(section):
    """Update specific config section."""
    try:
        config = load_config()
        if section not in config:
            return jsonify({"error": f"Unknown section: {section}"}), 404
        
        updates = request.get_json()
        if not updates:
            return jsonify({"error": "No updates provided"}), 400
        
        config[section].update(updates)
        save_config(config)
        success, message = reload_service()
        
        return jsonify({
            "success": True,
            "section": section,
            "reload": {"success": success, "message": message}
        })
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route('/api/status', methods=['GET'])
def get_status():
    """Get service status."""
    return jsonify(get_service_status())


@app.route('/api/reload', methods=['POST'])
def trigger_reload():
    """Trigger config reload."""
    success, message = reload_service()
    return jsonify({"success": success, "message": message})


@app.route('/api/defaults', methods=['GET'])
def get_defaults():
    """Get default configuration values."""
    return jsonify(DEFAULT_CONFIG)


if __name__ == '__main__':
    # For development only
    app.run(host='127.0.0.1', port=9091, debug=True)
