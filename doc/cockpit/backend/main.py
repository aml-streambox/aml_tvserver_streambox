#!/usr/bin/env python3
"""
StreamBox Config - Cockpit Plugin Main Entry Point

This module handles Cockpit socket activation.
"""

import sys
import os

# Add backend directory to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from api import app

def main():
    """Main entry point for socket activation."""
    # Check if running under systemd socket activation
    listen_pid = os.environ.get('LISTEN_PID')
    listen_fds = os.environ.get('LISTEN_FDS')
    
    if listen_pid and listen_fds:
        # Socket activation mode
        import socket
        from werkzeug.serving import run_simple
        
        # FD 3 is the first socket passed by systemd
        sock = socket.fromfd(3, socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        run_simple(
            '127.0.0.1', 0,
            app,
            use_reloader=False,
            use_debugger=False,
            passthrough_errors=True,
            fd=3
        )
    else:
        # Development mode
        app.run(host='127.0.0.1', port=9091, debug=True)


if __name__ == '__main__':
    main()
