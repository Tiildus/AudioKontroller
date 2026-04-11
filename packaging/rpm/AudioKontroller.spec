Name:           audiokontroller
Version:        %{?version}%{!?version:0.0.0}
Release:        %{?release}%{!?release:1}%{?dist}
Summary:        PCPanel-based per-application audio controller for Fedora KDE
License:        LicenseRef-Unknown
Source0:        AudioKontroller-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  hidapi-devel
BuildRequires:  make
BuildRequires:  pkgconf-pkg-config
BuildRequires:  pulseaudio-libs-devel
BuildRequires:  qt6-qtbase-devel
BuildRequires:  systemd-rpm-macros

Requires:       less
Requires:       playerctl
Requires:       systemd
Requires:       ydotool
Requires(pre):  shadow-utils
Requires(post): systemd-udev
Requires(postun): systemd-udev

%description
AudioKontroller turns a supported PCPanel USB controller into a per-application
audio mixer for Fedora KDE sessions. This package installs the daemon, systemd
user units, and the required udev rules.

%prep
%autosetup -n AudioKontroller-%{version}

%build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSYSTEM_INSTALL=ON
cmake --build build %{?_smp_mflags}

%install
DESTDIR=%{buildroot} cmake --install build

# CLI symlink — the binary handles subcommands directly
install -d %{buildroot}%{_bindir}
ln -sf %{_libexecdir}/audiokontroller/audiokontroller %{buildroot}%{_bindir}/AudioKontroller

%pre
getent group uinput >/dev/null || groupadd -r uinput || :

%post
# Add the installing user to the uinput group (matches install.sh behavior).
# SUDO_USER is set when the install is run via "sudo dnf install".
if [ -n "$SUDO_USER" ]; then
    usermod -aG uinput "$SUDO_USER" 2>/dev/null || :
fi

# Reload udev rules
if [ -x /usr/bin/udevadm ]; then
    /usr/bin/udevadm control --reload-rules >/dev/null 2>&1 || :
    /usr/bin/udevadm trigger --subsystem-match=hidraw --subsystem-match=misc >/dev/null 2>&1 || :
fi

# Enable ydotool daemon — use ydotool's own user service if available,
# otherwise fall back to the ydotoold.service we ship (matches install.sh).
# systemctl --global enables a user service for all users at the system level.
if systemctl --global cat ydotool.service >/dev/null 2>&1; then
    systemctl --global enable ydotool.service >/dev/null 2>&1 || :
else
    systemctl --global enable ydotoold.service >/dev/null 2>&1 || :
fi

# Enable and start the main service for all users
systemctl --global enable audiokontroller.service >/dev/null 2>&1 || :

cat <<'EOF'

AudioKontroller installed and enabled.
Log out and back in for uinput group permissions to take effect.

Commands:
  AudioKontroller start|stop|restart|status|config|log

EOF

%preun
if [ $1 -eq 0 ]; then
    # Full uninstall — disable globally
    systemctl --global disable audiokontroller.service >/dev/null 2>&1 || :
    systemctl --global disable ydotoold.service >/dev/null 2>&1 || :
fi

%postun
if [ -x /usr/bin/udevadm ]; then
    /usr/bin/udevadm control --reload-rules >/dev/null 2>&1 || :
    /usr/bin/udevadm trigger --subsystem-match=hidraw --subsystem-match=misc >/dev/null 2>&1 || :
fi

%files
%doc packaging/rpm/README.md
%{_bindir}/AudioKontroller
%config(noreplace) %{_sysconfdir}/audiokontroller/config.json
%{_libexecdir}/audiokontroller/audiokontroller
%{_udevrulesdir}/99-audiokontroller-pcpanel.rules
%{_udevrulesdir}/99-audiokontroller-uinput.rules
%{_userunitdir}/ydotoold.service
%{_userunitdir}/audiokontroller.service

%changelog
* Thu Apr 10 2026 Zach Williams - %{version}-%{release}
- RPM packaging with embedded CLI, system config, XDG state log
