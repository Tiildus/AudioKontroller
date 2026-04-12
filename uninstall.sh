#!/bin/bash
set -e

APP_NAME="AudioKontroller"
BIN_NAME="audiokontroller"

# XDG Base Directory paths (must match install.sh)
BIN_DIR="$HOME/.local/bin"
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/audiokontroller"
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/audiokontroller"
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE_FILE="$SERVICE_DIR/audiokontroller.service"
YDOTOOLD_FILE="$SERVICE_DIR/ydotoold.service"

echo "=== $APP_NAME uninstall ==="

# --- Stop and remove audiokontroller service ---
if systemctl --user is-active audiokontroller.service &>/dev/null; then
    echo "Stopping audiokontroller service..."
    systemctl --user stop audiokontroller.service
fi
if [ -f "$SERVICE_FILE" ]; then
    echo "Removing audiokontroller service..."
    systemctl --user disable audiokontroller.service 2>/dev/null || true
    rm -f "$SERVICE_FILE"
fi

# --- Stop and remove ydotoold service (only if we created it) ---
# The install script creates a user-level ydotoold.service only when the
# system doesn't provide one. If it exists at the user service path, it's ours.
if [ -f "$YDOTOOLD_FILE" ]; then
    echo "Removing user ydotoold service (created by install.sh)..."
    systemctl --user stop ydotoold.service 2>/dev/null || true
    systemctl --user disable ydotoold.service 2>/dev/null || true
    rm -f "$YDOTOOLD_FILE"
fi

systemctl --user daemon-reload

# --- Remove binary ---
if [ -f "$BIN_DIR/$BIN_NAME" ]; then
    echo "Removing binary..."
    rm -f "$BIN_DIR/$BIN_NAME"
fi

# --- Remove config (ask first since the user may have customized it) ---
if [ -d "$CONFIG_DIR" ]; then
    read -rp "Remove config directory ($CONFIG_DIR)? [y/N] " answer
    if [[ "$answer" =~ ^[Yy] ]]; then
        rm -rf "$CONFIG_DIR"
        echo "Config removed."
    else
        echo "Config preserved."
    fi
fi

# --- Remove state (logs) ---
if [ -d "$STATE_DIR" ]; then
    echo "Removing log directory..."
    rm -rf "$STATE_DIR"
fi

# --- Remove udev rules ---
echo "Removing udev rules..."
sudo rm -f /etc/udev/rules.d/99-uinput.rules
sudo rm -f /etc/udev/rules.d/99-pcpanel.rules
sudo udevadm control --reload
sudo udevadm trigger

# --- Remove KWin script runtime files ---
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/audiokontroller-$(id -u)}/audiokontroller"
if [ -d "$RUNTIME_DIR" ]; then
    echo "Removing runtime files..."
    rm -rf "$RUNTIME_DIR"
fi

# --- Clean up old install locations (pre-XDG layout) ---
rm -f  "$BIN_DIR/AudioKontroller"       2>/dev/null || true  # old symlink
rm -rf "$BIN_DIR/audiokontroller/"       2>/dev/null || true  # old install dir in bin
rm -rf "$HOME/.local/lib/audiokontroller" 2>/dev/null || true  # intermediate layout

# --- Done ---
echo
echo "Done! $APP_NAME has been uninstalled."
echo
echo "The following were NOT removed (may be used by other programs):"
echo "  - System packages (cmake, qt6, hidapi, playerctl, ydotool, etc.)"
echo "  - uinput group membership"
