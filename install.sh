#!/bin/bash
set -e

APP_NAME="AudioKontroller"
BIN_NAME="audiokontroller"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_DIR/build"

# XDG Base Directory paths
BIN_DIR="$HOME/.local/bin"
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/audiokontroller"
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/audiokontroller"
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE_FILE="$SERVICE_DIR/audiokontroller.service"

echo "=== $APP_NAME install/update ==="

# --- System dependencies ---
# dnf skips packages that are already installed, so this is safe to re-run.
echo "Installing required packages..."
sudo dnf install -y \
    cmake gcc-c++ \
    qt6-qtbase-devel qt6-qttools-devel \
    pulseaudio-libs-devel \
    hidapi-devel \
    pkgconf-pkg-config \
    playerctl ydotool

# --- uinput group and udev rules ---
# groupadd -f and usermod -aG are both idempotent.
sudo groupadd -f uinput
sudo usermod -aG uinput "$USER"

# Overwriting udev rule files with the same content is harmless.
echo "Setting up udev rules..."
echo 'KERNEL=="uinput", GROUP="uinput", MODE="0660", OPTIONS+="static_node=uinput"' | \
    sudo tee /etc/udev/rules.d/99-uinput.rules > /dev/null

cat << 'PCPANEL_EOF' | sudo tee /etc/udev/rules.d/99-pcpanel.rules > /dev/null
# PCPanel Mini (STM32)
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="a3c4", GROUP="uinput", MODE="0660"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="a3c4", GROUP="uinput", MODE="0660"

# PCPanel Pro (STM32)
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="a3c5", GROUP="uinput", MODE="0660"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="a3c5", GROUP="uinput", MODE="0660"

# PCPanel RGB (Microchip)
SUBSYSTEM=="usb", ATTR{idVendor}=="04d8", ATTR{idProduct}=="eb42", GROUP="uinput", MODE="0660"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="eb42", GROUP="uinput", MODE="0660"
PCPANEL_EOF

sudo udevadm control --reload
sudo udevadm trigger

# --- ydotool daemon service ---
# Use ydotool's own user-level service if available (Arch);
# otherwise create one. A user-level service is preferred over the
# system-level service Fedora ships — root bypasses the uinput group
# permission check on /dev/uinput, undermining the udev rule.
echo "Setting up ydotool service..."
mkdir -p "$SERVICE_DIR"
if systemctl --user cat ydotool.service &>/dev/null; then
    systemctl --user enable --now ydotool.service || true
else
    cat > "$SERVICE_DIR/ydotoold.service" << 'EOF'
[Unit]
Description=Ydotool user-level daemon
After=graphical-session.target

[Service]
ExecStart=/usr/bin/ydotoold
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
EOF
    systemctl --user daemon-reload
    systemctl --user enable --now ydotoold.service || true
fi

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
mkdir -p "$BIN_DIR" "$CONFIG_DIR" "$STATE_DIR"
systemctl --user stop audiokontroller.service 2>/dev/null || true

cp "$BUILD_DIR/$BIN_NAME" "$BIN_DIR/$BIN_NAME"
chmod +x "$BIN_DIR/$BIN_NAME"

# Config is the only file we guard — the user edits this, so never overwrite it.
if [ ! -f "$CONFIG_DIR/config.json" ]; then
    cp "$REPO_DIR/config.json" "$CONFIG_DIR/config.json"
    echo "Config created at $CONFIG_DIR/config.json"
fi

# --- systemd service ---
# Overwriting and reloading is idempotent.
echo "Creating user service..."
cat > "$SERVICE_FILE" << EOF
[Unit]
Description=AudioKontroller background service
After=graphical-session.target ydotool.service ydotoold.service

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
# Only show the logout warning if the user isn't in the uinput group yet
if ! id -nG "$USER" | grep -qw uinput; then
    echo "IMPORTANT: Log out and back in to apply uinput group permissions."
fi
