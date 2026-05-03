#!/bin/bash
set -e

APP_NAME="AudioKontroller"
BIN_NAME="audiokontroller"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_DIR/build"

# XDG Base Directory paths
# CONFIG_DIR is used in the output message only — the daemon creates config.json
# automatically on first run via ConfigManager::createDefault().
BIN_DIR="$HOME/.local/bin"
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/audiokontroller"
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/audiokontroller"
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE_FILE="$SERVICE_DIR/audiokontroller.service"

echo "=== $APP_NAME install/update (openSUSE Tumbleweed) ==="

# --- System dependencies ---
# zypper in is idempotent: already-installed packages are skipped.
echo "Installing required packages..."
sudo zypper --non-interactive install \
    cmake gcc-c++ \
    qt6-base-devel qt6-tools-devel \
    libpulse-devel \
    libhidapi-devel \
    pkgconf-pkg-config \
    playerctl

# --- pcpanel group and udev rules ---
# The "pcpanel" group gates hidraw access for the PCPanel device.
# groupadd -f and usermod -aG are both idempotent.
sudo groupadd -f pcpanel
sudo usermod -aG pcpanel "$USER"

echo "Setting up udev rules..."
cat << 'PCPANEL_EOF' | sudo tee /etc/udev/rules.d/99-pcpanel.rules > /dev/null
# PCPanel Mini (STM32)
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="a3c4", GROUP="pcpanel", MODE="0660"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="a3c4", GROUP="pcpanel", MODE="0660"

# PCPanel Pro (STM32)
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="a3c5", GROUP="pcpanel", MODE="0660"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="a3c5", GROUP="pcpanel", MODE="0660"

# PCPanel RGB (Microchip)
SUBSYSTEM=="usb", ATTR{idVendor}=="04d8", ATTR{idProduct}=="eb42", GROUP="pcpanel", MODE="0660"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="eb42", GROUP="pcpanel", MODE="0660"
PCPANEL_EOF

sudo udevadm control --reload
sudo udevadm trigger

mkdir -p "$SERVICE_DIR"

# --- Build ---
# 'make' is incremental — only changed files are recompiled.
# We build BEFORE stopping the service so a failed build never leaves
# the daemon offline (set -e exits immediately on any build error).
echo "Building $APP_NAME..."
mkdir -p "$BUILD_DIR"
cmake -S "$REPO_DIR" -B "$BUILD_DIR" --log-level=WARNING > /dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)"

# --- Install binary ---
# Stop first so the binary file isn't open when we overwrite it.
echo "Installing..."
mkdir -p "$BIN_DIR" "$STATE_DIR"
systemctl --user stop audiokontroller.service 2>/dev/null || true

cp "$BUILD_DIR/$BIN_NAME" "$BIN_DIR/$BIN_NAME"
chmod +x "$BIN_DIR/$BIN_NAME"

# --- systemd service ---
# Overwriting and reloading is idempotent.
echo "Creating user service..."
cat > "$SERVICE_FILE" << EOF
[Unit]
Description=AudioKontroller background service
After=graphical-session.target

[Service]
Type=simple
ExecStart=$BIN_DIR/$BIN_NAME
Restart=on-failure
RestartSec=3
NoNewPrivileges=yes
ProtectSystem=strict
ReadWritePaths=$STATE_DIR

[Install]
WantedBy=default.target
EOF
systemctl --user daemon-reload
systemctl --user enable --now audiokontroller.service || true

# --- Done ---
echo
echo "Done! $APP_NAME is running."
echo
echo "  Binary  : $BIN_DIR/$BIN_NAME"
echo "  Config  : $CONFIG_DIR/config.json"
echo "  Log     : $STATE_DIR/audiokontroller.log"
echo
echo "Commands:"
echo "  audiokontroller start|stop|restart|status|config|log"
echo
# Only show the logout warning if the user isn't in the pcpanel group yet
if ! id -nG "$USER" | grep -qw pcpanel; then
    echo "IMPORTANT: Log out and back in to apply pcpanel group permissions."
fi
