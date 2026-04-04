#!/bin/bash
set -e

APP_NAME="AudioKontroller"
DAEMON_NAME="audiokontrollerd"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_DIR/build"
INSTALL_DIR="$HOME/.local/bin/$APP_NAME.d"
INSTALL_BIN="$INSTALL_DIR/$DAEMON_NAME"
INSTALL_CONFIG="$INSTALL_DIR/config.json"
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

<<<<<<< HEAD
=======
# --- PCPanel USB device permissions (group-restricted, not world-writable) ---
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
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
# Overwriting the service file and re-enabling is idempotent.
echo "Setting up ydotool service..."
mkdir -p "$SERVICE_DIR"
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
echo "Installing to $INSTALL_DIR/"
mkdir -p "$INSTALL_DIR"
systemctl --user stop audiokontroller.service 2>/dev/null || true

cp "$BUILD_DIR/$DAEMON_NAME" "$INSTALL_BIN"
chmod +x "$INSTALL_BIN"

# Config is the only file we guard — the user edits this, so never overwrite it.
if [ ! -f "$INSTALL_CONFIG" ]; then
    cp "$REPO_DIR/config.json" "$INSTALL_CONFIG"
    echo "Config copied to $INSTALL_CONFIG"
fi

# --- CLI wrapper ---
# Overwriting with the same content every run is harmless.
echo "Installing CLI wrapper..."
cat > "$HOME/.local/bin/AudioKontroller" << 'WRAPPER_EOF'
#!/bin/bash
INSTALL_DIR="$HOME/.local/bin/AudioKontroller.d"
SERVICE="audiokontroller.service"
CONFIG="$INSTALL_DIR/config.json"
LOG="$INSTALL_DIR/audiokontroller.log"

case "$1" in
    start)   systemctl --user start "$SERVICE" ;;
    stop)    systemctl --user stop "$SERVICE" ;;
    restart) systemctl --user restart "$SERVICE" ;;
    status)  systemctl --user status "$SERVICE" ;;
    config)  ${EDITOR:-nano} "$CONFIG" ;;
    log)     less +G "$LOG" ;;
    *)
        echo "Usage: AudioKontroller {start|stop|restart|status|config|log}"
        exit 1
        ;;
esac
WRAPPER_EOF
chmod +x "$HOME/.local/bin/AudioKontroller"

<<<<<<< HEAD
# --- systemd service ---
# Overwriting and reloading is idempotent.
=======
# --- systemd service (with security hardening) ---
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
echo "Creating user service..."
cat > "$SERVICE_FILE" << EOF
[Unit]
Description=AudioKontroller background service
After=graphical-session.target ydotoold.service

[Service]
Type=simple
WorkingDirectory=$INSTALL_DIR
ExecStart=$INSTALL_BIN
Restart=on-failure
RestartSec=3
NoNewPrivileges=yes
ProtectSystem=strict
ReadWritePaths=$INSTALL_DIR

[Install]
WantedBy=default.target
EOF
systemctl --user daemon-reload
systemctl --user enable --now audiokontroller.service || true

# --- Done ---
echo
echo "Done! $APP_NAME is running."
echo
echo "  Install directory : $INSTALL_DIR/"
echo "  Config            : $INSTALL_CONFIG"
echo "  Log               : $INSTALL_DIR/audiokontroller.log"
echo
echo "Commands:"
echo "  AudioKontroller start|stop|restart|status|config|log"
echo
# Only show the logout warning if the user isn't in the uinput group yet
if ! id -nG "$USER" | grep -qw uinput; then
    echo "IMPORTANT: Log out and back in to apply uinput group permissions."
fi
