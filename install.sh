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

echo "Installing $APP_NAME..."

# --- Install Dependencies ---
echo "Installing required packages..."
sudo dnf install -y \
    cmake gcc-c++ \
    qt6-qtbase-devel qt6-qttools-devel \
    pulseaudio-libs-devel \
    hidapi-devel \
    pkgconf-pkg-config \
    playerctl ydotool

# --- uinput permissions ---
echo "Setting up udev rules..."
sudo groupadd -f uinput
sudo usermod -aG uinput "$USER"

echo 'KERNEL=="uinput", GROUP="uinput", MODE="0660", OPTIONS+="static_node=uinput"' | \
  sudo tee /etc/udev/rules.d/99-uinput.rules > /dev/null

# --- PCPanel USB device permissions (group-restricted, not world-writable) ---
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

# --- ydotool daemon ---
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
echo "Building $APP_NAME..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make -j"$(nproc)"

# --- Install ---
echo "Installing to $INSTALL_DIR/"
mkdir -p "$INSTALL_DIR"
cp "$BUILD_DIR/$DAEMON_NAME" "$INSTALL_BIN"
chmod +x "$INSTALL_BIN"

if [ ! -f "$INSTALL_CONFIG" ]; then
    cp "$REPO_DIR/config.json" "$INSTALL_CONFIG"
    echo "Config copied to $INSTALL_CONFIG"
else
    echo "Config already exists at $INSTALL_CONFIG -- skipping"
fi

# --- CLI wrapper ---
echo "Installing CLI wrapper to $HOME/.local/bin/"
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

# --- systemd service (with security hardening) ---
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
echo "Installation complete!"
echo "  Install directory : $INSTALL_DIR/"
echo "  Service file      : $SERVICE_FILE"
echo "  CLI wrapper       : $HOME/.local/bin/AudioKontroller"
echo
echo "Usage:"
echo "  AudioKontroller start    - start the service"
echo "  AudioKontroller stop     - stop the service"
echo "  AudioKontroller restart  - restart the service"
echo "  AudioKontroller status   - check service status"
echo "  AudioKontroller config   - edit config file"
echo "  AudioKontroller log      - view log file"
echo
echo "IMPORTANT: Log out and back in to apply uinput group permissions."
