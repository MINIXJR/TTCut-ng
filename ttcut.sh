#!/usr/bin/env bash
# TTCut-ng launcher script
# Sets QT_QPA_PLATFORM=xcb for X11 compatibility on Wayland

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export QT_QPA_PLATFORM=xcb
exec "$SCRIPT_DIR/ttcut-ng" "$@"
