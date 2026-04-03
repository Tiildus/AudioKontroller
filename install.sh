#!/bin/bash
set -e

APP_NAME="MidiController"
REPO_DIR="$(pwd)"               # assumes you're running this from the project root
BUILD_DIR="$REPO_DIR/build"
INSTALL_BIN="$HOME/.local/bin/$APP_NAME"
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE_FILE="$SERVICE_DIR/midicontroller.service"

echo "🔧 Installing $APP_NAME on Fedora..."

# --- 1. Install Dependencies ---
echo "📦 Installing required packages..."
sudo dnf install -y \
    cmake gcc-c++ \
    qt6-qtbase-devel qt6-qttools-devel \
    pulseaudio-libs-devel \
    hidapi-devel \
    pkgconf-pkg-config \
    playerctl ydotool kdotool

# --- 2. Setup safe /dev/uinput permissions ---
echo "🧩 Setting up udev rule for /dev/uinput (safe configuration)..."
sudo groupadd -f uinput
sudo usermod -aG uinput "$USER"

# Create secure udev rule: group uinput, mode 0660
echo 'KERNEL=="uinput", GROUP="uinput", MODE="0660", OPTIONS+="static_node=uinput"' | \
  sudo tee /etc/udev/rules.d/99-uinput.rules > /dev/null

# --- PCPanel USB device permissions ---
echo "🎛️ Setting up udev rules for PCPanel USB devices..."
cat << 'PCPANEL_EOF' | sudo tee /etc/udev/rules.d/99-pcpanel.rules > /dev/null
# PCPanel Mini (STM32)
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="a3c4", MODE="0666"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="a3c4", MODE="0666"

# PCPanel Pro (STM32)
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="a3c5", MODE="0666"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="a3c5", MODE="0666"

# PCPanel RGB (Microchip)
SUBSYSTEM=="usb", ATTR{idVendor}=="04d8", ATTR{idProduct}=="eb42", MODE="0666"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="eb42", MODE="0666"
PCPANEL_EOF

sudo udevadm control --reload
sudo udevadm trigger

echo "✅ Udev rules installed. You must log out and back in after installation to apply group changes."

# --- 3. Setup ydotool user service ---
echo "⚙️ Setting up ydotool user service..."
mkdir -p "$SERVICE_DIR"

cat > "$SERVICE_DIR/ydotoold.service" << 'EOF'
[Unit]
Description=Ydotool user-level daemon
After=graphical-session.target

[Service]
ExecStart=/usr/bin/ydotoold
Restart=on-failure

[Install]
WantedBy=default.target
EOF

systemctl --user daemon-reload
systemctl --user enable --now ydotoold.service || true

# --- 4. Build the application ---
echo "🏗️ Building $APP_NAME..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make -j"$(nproc)"

# --- 5. Install binary into ~/.local/bin ---
echo "📂 Installing binary to ~/.local/bin/"
mkdir -p "$HOME/.local/bin"
cp "$BUILD_DIR/$APP_NAME" "$INSTALL_BIN"
chmod +x "$INSTALL_BIN"

# --- 6. Create user systemd service ---
echo "🧩 Creating user service at $SERVICE_FILE"
cat > "$SERVICE_FILE" << EOF
[Unit]
Description=MIDI Controller background service
After=graphical-session.target ydotoold.service

[Service]
Type=simple
ExecStart=$INSTALL_BIN
Restart=on-failure
RestartSec=3
StandardOutput=append:%h/.local/share/midicontroller.log
StandardError=append:%h/.local/share/midicontroller.err

[Install]
WantedBy=default.target
EOF

# --- 7. Enable & start service ---
echo "🚀 Enabling and starting $APP_NAME service..."
systemctl --user daemon-reload
systemctl --user enable --now midicontroller.service || true

# --- 8. Verify setup ---
echo
echo "✅ Installation complete!"
echo "📍 Binary: $INSTALL_BIN"
echo "🧠 Service file: $SERVICE_FILE"
echo
echo "You can control the service manually with:"
echo "   systemctl --user start midicontroller.service"
echo "   systemctl --user stop midicontroller.service"
echo "   systemctl --user restart midicontroller.service"
echo "   systemctl --user status midicontroller.service"
echo
echo "⚠️ IMPORTANT: Log out and back in now to apply the new 'uinput' group permissions."
echo "🎉 $APP_NAME will then start automatically on login and run in the background."
