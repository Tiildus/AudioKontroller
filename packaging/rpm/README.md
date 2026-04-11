# AudioKontroller RPM Packaging

Build the Fedora RPM from the current working tree with:

```bash
./packaging/rpm/build-rpm.sh
```

The script creates a local `rpmbuild` tree under `packaging/rpm/` and packages
the current checkout, including uncommitted changes.

Install the resulting RPM with:

```bash
sudo dnf install packaging/rpm/rpmbuild/RPMS/*/audiokontroller-*.rpm
```

After install:

```bash
# Add yourself to the uinput group (first install only)
sudo usermod -aG uinput $USER
# Log out and back in, then:
systemctl --user enable --now audiokontroller.service
```

## File layout

| File | Path |
|------|------|
| Daemon binary | `/usr/libexec/audiokontroller/audiokontroller` |
| CLI command | `/usr/bin/AudioKontroller` (symlink to daemon) |
| Config | `/etc/audiokontroller/config.json` |
| Log | `~/.local/state/audiokontroller/audiokontroller.log` |
| User service | `/usr/lib/systemd/user/audiokontroller.service` |
| ydotool service | `/usr/lib/systemd/user/audiokontroller-ydotoold.service` |
| udev rules | `/usr/lib/udev/rules.d/99-audiokontroller-*.rules` |

The config file is marked `%config(noreplace)` — upgrades will never overwrite
your modifications.
