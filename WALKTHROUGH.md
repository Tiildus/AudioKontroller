# AudioKontroller Codebase Walkthrough

This document is a guided tour of the AudioKontroller codebase. It is written for someone who has never seen the code before and wants to understand how it works well enough to modify, extend, or debug it. Concepts are introduced in the order you need them, and each section builds on the ones before it.

---

## Table of Contents

1. [What Is AudioKontroller?](#1-what-is-audiokontroller)
2. [The Hardware](#2-the-hardware)
3. [Architecture Overview](#3-architecture-overview)
4. [Build System and Dependencies](#4-build-system-and-dependencies)
5. [Configuration — ConfigManager](#5-configuration--configmanager)
6. [Logging — Logger](#6-logging--logger)
7. [USB Input — PCPanelHandler](#7-usb-input--pcpanelhandler)
8. [Volume Control — AudioHandler](#8-volume-control--audiohandler)
9. [Window Focus Tracking — FocusMonitor](#9-window-focus-tracking--focusmonitor)
10. [Button Actions — ButtonHandler](#10-button-actions--buttonhandler)
11. [Visual Feedback — Overlay](#11-visual-feedback--overlay)
12. [The Entry Point — main.cpp](#12-the-entry-point--maincpp)
13. [Threading Model](#13-threading-model)
14. [Data Flow: From Knob Turn to Volume Change](#14-data-flow-from-knob-turn-to-volume-change)
15. [Installation and Deployment](#15-installation-and-deployment)
16. [Guide for Contributors](#16-guide-for-contributors)

---

## 1. What Is AudioKontroller?

AudioKontroller is a Linux daemon that turns a PCPanel USB hardware controller into a per-application audio mixer. Instead of alt-tabbing to a volume mixer to adjust Discord's volume separately from Spotify, you turn a physical knob. Each knob and button on the hardware device is mapped to an action through a JSON config file.

**Key facts:**
- Written in C++17
- Runs as a systemd user service (not as root)
- Targets Linux with KDE Plasma on Wayland
- Uses PulseAudio for audio control, D-Bus for desktop integration
- The compiled binary is named `audiokontroller`

---

## 2. The Hardware

The PCPanel is a small USB device with physical knobs (rotary encoders/potentiometers) and buttons. It communicates using HID (Human Interface Device) — the same USB protocol used by keyboards and mice. This means no special driver is needed; the OS already knows how to talk to HID devices, and a library called `hidapi` provides a clean C API to read the data.

Three device variants are supported, differing only in their USB identifiers and number of controls:

| Model | Knobs | Buttons | USB Vendor ID | USB Product ID |
|-------|-------|---------|---------------|----------------|
| Mini  | 4     | 4       | 0x0483        | 0xA3C4         |
| Pro   | 5 knobs + 4 sliders | 5 | 0x0483   | 0xA3C5         |
| RGB   | 4     | 4       | 0x04D8        | 0xEB42         |

When you turn a knob, the device sends a small packet (called an "HID report") over USB. The report is 64 bytes, but only the first 3 bytes matter:

```
Byte 0: Event type (0x01 = knob, 0x02 = button)
Byte 1: Index (which knob/button, starting from 0)
Byte 2: Value (0–255 for knobs, 0 or 1 for buttons)
```

The rest of the codebase is about reading these reports and turning them into useful actions.

---

## 3. Architecture Overview

The daemon is structured as a set of independent components that are wired together in `main.cpp` using callbacks and injected functions. No component directly depends on another (except through the configuration structs). Here's how they relate:

```
                    +-----------------+
                    |   config.json   |
                    +--------+--------+
                             |
                    +--------v--------+
                    |  ConfigManager   |  Parses JSON into structs
                    +--------+--------+
                             |
                    +--------v--------+
                    |     main.cpp     |  Constructs everything, wires callbacks
                    +--------+--------+
                             |
              +--------------+--------------+
              |                             |
   +----------v----------+      +-----------v-----------+
   |   PCPanelHandler     |      |     FocusMonitor       |
   |   (USB HID thread)   |      |  (KWin D-Bus script)   |
   +----+------------+----+      +-----------+------------+
        |            |                       |
   knob callback  button callback     getPID() function
        |            |                  injected into:
        v            v                       |
 +------+------+ +---+------------+    +-----+-------+
 | AudioHandler | | ButtonHandler  |<---+             |
 | (PulseAudio) | | (fork/exec)    |    +-------------+
 +------+-------+ +----------------+
        |
 +------v------+
 |   Overlay    |  Shows volume % on screen via KDE OSD
 +-------------+
```

**Why callbacks instead of direct references?** If `PCPanelHandler` held a pointer to `AudioHandler`, changing one would require changing the other. With callbacks, `PCPanelHandler` just says "a knob was turned" and doesn't know or care what happens next. This makes each component independently testable and replaceable.

**Why injected functions for PID?** Both `AudioHandler` and `ButtonHandler` need the focused window's PID, but neither should depend on `FocusMonitor` directly. Instead, `main.cpp` injects a lambda `[&focusMonitor]() { return focusMonitor.getPID(); }` into each. This is a lightweight form of dependency injection.

---

## 4. Build System and Dependencies

The project uses **CMake** (minimum version 3.16) as its build system. The build configuration lives in `CMakeLists.txt`.

### Compiler and Language

- **C++17** is required (`CMAKE_CXX_STANDARD 17`)
- Defaults to a **Release** build with `-O2` optimization (needed because `_FORTIFY_SOURCE` requires optimization to be active)

### Dependencies

The project depends on four external libraries, found via CMake's `find_package` and `pkg_check_modules`:

| Library | Purpose | How it's found |
|---------|---------|----------------|
| **Qt6 Core** | Event loop, JSON parsing, timers | `find_package(Qt6)` |
| **Qt6 DBus** | D-Bus communication (FocusMonitor, Overlay) | `find_package(Qt6)` |
| **libpulse** | PulseAudio client library for volume control | `pkg_check_modules(PULSE)` |
| **hidapi-libusb** | Cross-platform USB HID device access | `pkg_check_modules(HIDAPI)` |
| **pthread** | Threading (HID read thread) | Linked directly |

### Qt's Meta-Object Compiler (MOC)

The line `set(CMAKE_AUTOMOC ON)` is important. Qt uses a code generation step called **MOC** (Meta-Object Compiler) that scans header files for the `Q_OBJECT` macro and generates additional C++ code for Qt's signal/slot system and D-Bus integration. `AUTOMOC` tells CMake to do this automatically. Without it, `FocusMonitor`'s D-Bus slot (`SetPID`) would not work — the D-Bus layer wouldn't know how to route incoming calls to that method.

This is why both `.h` and `.cpp` files are listed in `add_executable()` — MOC needs to see the headers.

### Security Hardening

The build includes several security-focused compiler and linker flags:

- `-fstack-protector-strong` — inserts canary values on the stack to detect buffer overflows
- `-D_FORTIFY_SOURCE=2` — replaces some unsafe C library calls (like `memcpy`, `sprintf`) with bounds-checked versions at compile time
- `-Wl,-z,relro -Wl,-z,now` — marks memory regions as read-only after initialization and resolves all dynamic symbols at load time, making certain exploit techniques harder

These flags are good practice for any daemon that processes external input (in this case, USB HID data).

---

## 5. Configuration — ConfigManager

**Files:** `ConfigManager.h`, `ConfigManager.cpp`

Configuration is the first thing the daemon loads. Everything else — which device model to use, what each knob and button does, where to write logs — comes from a JSON file. Understanding the config is essential because it defines the behavior of every other component.

### The Config Structs

Three structs represent the parsed configuration:

**`KnobConfig`** describes what a single physical knob controls:
- `type` — one of three strings:
  - `"app"` — controls volume for a specific application (e.g., Firefox)
  - `"focused"` — controls volume for whichever window currently has keyboard focus
  - `"system"` — controls the master system volume
- `targets` — a list of application binary names to match (only used when `type` is `"app"`). The config supports both a single string and an array:
  ```json
  { "type": "app", "target": "firefox" }
  { "type": "app", "target": ["chrome", "firefox"] }
  ```

**`ButtonConfig`** describes what a single physical button does when pressed:
- `action` — one of: `"mediaPlayPause"`, `"sendKeys"`, `"forceClose"`, `"none"`
- `keys` — a human-readable key combo string like `"ctrl+grave"` (for `sendKeys`)
- `args` — raw ydotool command arguments as an alternative to `keys` (for advanced users)

**`Config`** is the top-level container holding the device model name, knob dead-zone threshold, log file path, and the lists of knob and button configs.

### Loading and Parsing

`ConfigManager::load()` does the following:

1. Tries to open the JSON file. If it doesn't exist (first run), it calls `createDefault()` to generate a sensible starter config, then opens the newly created file.
2. Parses the JSON using Qt's `QJsonDocument`. If parsing fails, it prints an error to stderr and returns `false`.
3. Reads top-level fields (`device`, `knobThreshold`, and optionally `logFile`) with fallback defaults if any are missing.
4. Iterates through the `"knobs"` and `"buttons"` JSON arrays, passing each object to `parseKnob()` or `parseButton()`.

**Why stderr instead of the Logger?** The Logger hasn't been initialized at config-load time (it depends on a successful config load), so there's nowhere else to write the error.

### Default Config Generation

`createDefault()` produces a config that works out of the box for a PCPanel Mini with a typical setup: Firefox and Discord on dedicated knobs, a focused-window knob, a system volume knob, and buttons for media play/pause, a key combo, no-op, and force-close. This means a new user can install and immediately have a working (if generic) configuration.

---

## 6. Logging — Logger

**Files:** `Logger.h`, `Logger.cpp`

The Logger is a utility used by every other component. It writes timestamped messages to a file, making it possible to debug the daemon after the fact (since a daemon has no terminal to print to).

### Singleton Pattern

Logger uses the **Meyers singleton**: a `static` local variable inside a function. In C++11 and later, the language guarantees that this variable is initialized exactly once, even if multiple threads call `instance()` simultaneously. This is the simplest thread-safe singleton in C++.

```cpp
Logger& Logger::instance() {
    static Logger logger;  // created once, lives until program exit
    return logger;
}
```

Every component calls `Logger::instance().info(...)` rather than constructing its own Logger.

### Log Levels and Flushing

There are three levels: `INFO`, `WARN`, and `ERROR`. The key design decision is the **flush policy**:

- **INFO messages are buffered.** The OS collects them in memory and writes to disk in batches. This matters because knobs generate many events per second — flushing every message would cause unnecessary disk I/O.
- **WARN and ERROR messages flush immediately.** If the daemon crashes, you want the last error message to actually be on disk, not stuck in a buffer that gets discarded.

### Truncation on Startup

The log file is opened with `std::ios::trunc` (truncate), meaning each daemon session starts with a fresh file. This prevents the log from growing without bound across restarts. For a long-running daemon that might be restarted multiple times a day, this is a deliberate trade-off: you lose history from previous sessions, but you never run out of disk space.

### Timestamp Format

Timestamps include millisecond precision (e.g., `2025-04-04 14:23:01.042`). This is important for debugging timing-sensitive issues — for example, determining whether a knob event was processed before or after a focus change.

---

## 7. USB Input — PCPanelHandler

**Files:** `PCPanelHandler.h`, `PCPanelHandler.cpp`

This is where physical hardware input enters the software. `PCPanelHandler` opens the USB device, reads raw HID reports in a background thread, and translates them into typed callbacks that the rest of the application can understand.

### Opening the Device

The constructor calls `hid_init()` (a one-time library setup) and then `hid_open(vid, pid, nullptr)` to open the device by its USB vendor and product IDs. The `nullptr` third argument means "any serial number" — if you have multiple identical devices, it opens whichever one it finds first.

If `hid_open` fails, the device pointer remains `nullptr` and `isConnected()` returns `false`. The rest of the daemon continues to run (so D-Bus services stay active), but no HID events will be processed.

The device handle is wrapped in a `std::unique_ptr` with a custom deleter (`HidDeviceDeleter`) that calls `hid_close()`. This means the device is automatically closed when the `PCPanelHandler` is destroyed, even if an exception is thrown.

### The Read Loop

`startListeningAsync()` launches the `readLoop()` method in a background thread. The loop repeatedly calls `hid_read_timeout()`, which blocks for up to 100ms waiting for data:

- **Return > 0:** Data was received. Parse the report.
- **Return == 0:** Timeout — no data available. Normal; just loop again.
- **Return < 0:** Error — the device may have been unplugged. Count consecutive errors and give up after 50.

### Startup Burst Skipping

When the device is first opened, it sends a burst of reports reflecting the current physical position of every knob. If these were processed normally, every knob's volume would be instantly set to its current physical position — potentially slamming your music to 100% or muting Discord. The `INIT_SKIP_READS` constant (set to 20) causes the first 20 knob reports to be silently discarded.

Button events are *not* skipped, because a button pressed during startup is always an intentional action.

### Dead-Zone Filtering

Potentiometers (the components inside the knobs) can produce slight fluctuations in their reported value even when physically at rest — electrical noise. Without filtering, this would cause constant tiny volume changes.

The `knobThreshold` (default: 4 on a 0–255 scale) defines the minimum change required to fire a callback. The `lastValue` array tracks the most recent reported value for each knob. A new value only triggers the callback if it differs from the last by at least `knobThreshold`:

```cpp
if (prev == NO_VALUE || std::abs(value - prev) >= knobThreshold) {
    lastValue[index] = value;
    callback(index, value / 255.0f);  // normalize to 0.0–1.0
}
```

The `NO_VALUE` sentinel (-1) ensures the very first movement of each knob always fires, since there's no previous value to compare against.

### Callbacks

Two callbacks can be registered:
- `setCallback(CCCallback)` — called with `(knob_index, normalized_volume)` on every qualifying knob movement
- `setButtonCallback(ButtonCallback)` — called with `(button_index)` on every button press

Button releases (value == 0) are ignored. Only presses (value == 1) trigger the callback.

These callbacks are invoked from the HID read thread, which has implications for thread safety discussed in [Section 13](#13-threading-model).

### Shutdown

`requestStop()` sets the atomic `running` flag to `false`. The read loop checks this flag at the top of every iteration and exits. `stopListening()` calls `requestStop()` and then joins the thread (waits for it to finish). The distinction exists because `requestStop()` is safe to call from a Unix signal handler (it only writes an atomic bool), while `stopListening()` does a thread join which is not async-signal-safe.

---

## 8. Volume Control — AudioHandler

**Files:** `AudioHandler.h`, `AudioHandler.cpp`

AudioHandler is the bridge between knob positions and actual audio volume changes. It connects to PulseAudio and can adjust volume for individual applications (by name) or for the entire system.

### PulseAudio Threading Model

PulseAudio provides three ways to use it. AudioKontroller uses `pa_threaded_mainloop`, where PulseAudio runs its own internal event loop in a dedicated thread. This is the recommended approach for applications that have their own threads (as we do — the HID read thread).

The key concept is the **mainloop lock**. PulseAudio's internal thread and our HID thread both need to access the same state. The lock ensures they don't step on each other:

```
HID Thread                          PulseAudio Thread
    |                                    |
    |  pa_threaded_mainloop_lock()       |
    |  [writes targetApps, targetVolume] |
    |  [calls requestSinkInputs()]       |
    |  pa_threaded_mainloop_unlock()     |
    |                                    |
    |                           [fires sinkInputInfoCallback()]
    |                           [reads targetApps, targetVolume]
    |                           [sets volume on matching streams]
```

Because both sides use `lock()`/`unlock()`, the shared fields (`targetApps`, `targetVolume`, `isSystem`) are never read and written simultaneously.

### Connecting to PulseAudio

`init()` creates a mainloop and a context (which represents the connection to the PulseAudio server), then starts the mainloop thread. It then enters a wait loop:

```cpp
{
    PaLockGuard lock(mainloop.get());
    while (true) {
        pa_context_state_t state = pa_context_get_state(context);
        if (state == PA_CONTEXT_READY) { connected = true; break; }
        if (!PA_CONTEXT_IS_GOOD(state)) { break; }
        pa_threaded_mainloop_wait(mainloop.get());
    }
}
```

`PaLockGuard` is a small RAII wrapper defined in `AudioHandler.h` that calls `pa_threaded_mainloop_lock()` in its constructor and `pa_threaded_mainloop_unlock()` in its destructor. This guarantees the lock is released even on early returns or exceptions.

`pa_threaded_mainloop_wait()` atomically releases the lock and puts the thread to sleep. When the connection state changes, PulseAudio calls `contextStateCallback()`, which calls `pa_threaded_mainloop_signal()` to wake us up. We re-check the state, and loop until it's either `READY` or a failure state. The `while` loop guards against spurious wakeups — a standard pattern when working with condition variables.

### Three Types of Volume Control

`handleKnob()` dispatches based on the knob's configured type:

**App volume (`type: "app"`):** Calls `setVolumeForApps()` with the list of target names from the config. This locks the mainloop, stores the target names and volume, then kicks off `requestSinkInputs()` — a PulseAudio operation that enumerates all active audio streams. For each stream, PulseAudio calls `sinkInputInfoCallback()`, where the code checks if any of the stream's name fields match any of the target names.

The matching is intentionally fuzzy: it does a case-insensitive substring search across three PulseAudio property fields (`application.process.binary`, `application.name`, `media.name`). This means `"discord"` in your config matches a binary called `"Discord"`, an app named `"discord-canary"`, etc.

**Focused window volume (`type: "focused"`):** Uses the injected `getPID()` function to get the currently focused window's PID, then reads `/proc/<pid>/comm` to get the process name. It then calls `setVolumeForApps()` with that name.

Why resolve to a name instead of matching by PID directly? Because of **Electron/Chromium apps**. In apps like Discord, VS Code, and Slack, the window you see is owned by one process, but the audio is played by a different child process. Both processes share the same binary name (e.g., `"discord"`), so matching by name catches both. Matching by PID would only find the window process, which might not be the one playing audio.

**System volume (`type: "system"`):** Calls `setSystemVolume()`, which queries the default output sink (the `@DEFAULT_SINK@` special name) and sets its volume directly. This affects all audio on the system regardless of which application is producing it.

### Volume Representation

PulseAudio uses an internal volume scale where `PA_VOLUME_NORM` (65536) represents 100%. AudioKontroller works with normalized floats (0.0 to 1.0) and converts at the boundary:

```cpp
pa_cvolume_set(&cv, info->volume.channels,
    static_cast<uint32_t>(targetVolume * PA_VOLUME_NORM));
```

The channel count comes from the stream itself (`info->volume.channels`), so stereo, mono, and surround streams all work correctly without hardcoding.

---

## 9. Window Focus Tracking — FocusMonitor

**Files:** `FocusMonitor.h`, `FocusMonitor.cpp`

FocusMonitor solves a specific problem: **on Wayland, there is no standard way for an application to ask "which window currently has focus?"** This information is available to the window manager (KWin), but not to regular applications. FocusMonitor uses a creative workaround involving D-Bus and KWin's scripting engine.

### The Strategy

1. Write a small JavaScript file that KWin can execute
2. Load that script into KWin via D-Bus
3. The script hooks KWin's `windowActivated` event
4. Every time focus changes, the script calls back to us via D-Bus with the new window's PID
5. We store the PID in an atomic integer for instant, lock-free reads from any thread

```
KWin (running our JS)                     AudioKontroller
         |                                      |
   user clicks a window                         |
         |                                      |
   windowActivated fires                        |
         |                                      |
   callDBus("com.audiokontroller.FocusMonitor", |
            "/FocusMonitor",                    |
            "SetPID", window.pid)               |
         |                                      |
         +------- D-Bus session bus ----------->|
                                                |
                                          SetPID(pid)
                                                |
                                          activePID.store(pid)
```

### D-Bus Registration

The constructor does two things on D-Bus:

1. **Claims a service name** (`com.audiokontroller.FocusMonitor`). This is like registering a domain name on the bus — it's the address that KWin's script will use to find us.
2. **Registers an object** at path `/FocusMonitor`. This maps incoming D-Bus method calls to the `Q_SLOTS` on the `FocusMonitor` class. `ExportAllSlots` tells Qt to expose every public slot.

The `Q_CLASSINFO("D-Bus Interface", "com.audiokontroller.FocusMonitor")` annotation tells Qt's MOC what D-Bus interface name to advertise for this object. The KWin script's `callDBus()` uses this exact name as its third argument.

### The KWin Script

The JavaScript file written to disk is minimal:

```javascript
workspace.windowActivated.connect(function(window) {
    if (window) {
        callDBus("com.audiokontroller.FocusMonitor", "/FocusMonitor",
                 "com.audiokontroller.FocusMonitor", "SetPID", window.pid);
    }
});
```

`workspace` is a global object provided by KWin's scripting environment. `windowActivated` is a signal emitted whenever a window gains focus. `callDBus()` is KWin's built-in function for making D-Bus calls from scripts.

### Loading the Script

Loading happens in two D-Bus calls to KWin:

1. **`loadScript(path, name)`** — tells KWin to register the JS file. Returns an integer script ID.
2. **`run()`** — activates the script. Called on the script object at `/Scripting/Script{id}`.

Scripts are dormant after `loadScript` until `run()` is called — this is KWin's design.

### The Startup Race

A critical edge case: the daemon may start before KWin is available on D-Bus. This happens when the daemon is auto-started early in the session via systemd. If `loadKWinScript()` fails (because KWin's D-Bus service doesn't exist yet), a `QTimer` retries every 2 seconds for up to 30 seconds (15 retries). The timer runs on the Qt event loop, so it doesn't block anything.

This is why `main.cpp` needs `QCoreApplication` — the `QTimer` only works when a Qt event loop is running.

### Cleanup

The destructor unloads the script from KWin (so it stops calling our D-Bus service) and unregisters both the D-Bus object and service name. This prevents stale scripts from accumulating in KWin across daemon restarts.

---

## 10. Button Actions — ButtonHandler

**Files:** `ButtonHandler.h`, `ButtonHandler.cpp`

ButtonHandler translates button press events into actions. Four action types are supported.

### Action Dispatch

`handleButton()` is a simple dispatcher:

```cpp
if (bc.action == "mediaPlayPause")     toggleMediaPlayPause();
else if (bc.action == "sendKeys")      sendKeyCombo(bc.keys) or sendKeySequence(bc.args);
else if (bc.action == "forceClose")    forceCloseFocusedWindow();
// "none" does nothing
```

### fork/exec: Why Not `system()`?

All external commands are run via `fork()` + `execvp()` instead of `std::system()`. This is a deliberate security and performance choice:

**Security:** `std::system()` passes the command through `/bin/sh`, which interprets shell metacharacters. If a config value contained `"; rm -rf /"`, the shell would execute it. With `execvp()`, arguments are passed directly to the program with no shell interpretation — they're just strings.

**Non-blocking:** `std::system()` waits for the child process to finish before returning. This would stall the HID read loop, making knobs and other buttons unresponsive while a command runs. `fork()` returns immediately in the parent process.

**Zombie prevention:** Normally, when a child process exits, its exit status stays in the kernel until the parent calls `wait()`. Since we never call `wait()` (fire-and-forget), these would accumulate as zombie processes. The `SA_NOCLDWAIT` flag set in `main.cpp` tells the kernel to discard child exit statuses automatically.

The child uses `_exit()` instead of `exit()` if `execvp()` fails. This is important: `exit()` flushes the parent's stdio buffers (which were duplicated during `fork()`), potentially causing duplicate output. `_exit()` exits immediately without running any cleanup.

### Media Play/Pause

Simply runs `playerctl play-pause`. Playerctl is a command-line tool that communicates with MPRIS-compatible media players (Spotify, Firefox, VLC, etc.) via D-Bus.

### Key Combos

The `sendKeyCombo()` function translates a human-readable string like `"ctrl+grave"` into the format ydotool expects. The process:

1. Split the string on `+` (e.g., `"ctrl+grave"` becomes `["ctrl", "grave"]`)
2. Trim whitespace and lowercase each token
3. Look up each token in `keyMap` to get its Linux keycode (e.g., `ctrl` = 29, `grave` = 41)
4. Build the ydotool command with press events in order, then release events in reverse:
   ```
   ydotool key 29:1 41:1 41:0 29:0
   ```
   The `:1` suffix means "press" and `:0` means "release". Reverse-order release is how a real keyboard works — you press modifiers first, press the key, release the key, then release modifiers.

The `keyMap` is a large `unordered_map` containing ~100 entries mapping human-readable names (like `"ctrl"`, `"grave"`, `"f1"`, `"space"`) to their Linux input event keycodes from `linux/input-event-codes.h`.

For advanced users, the `"args"` field in config allows passing raw ydotool arguments directly, bypassing the key name translation entirely.

### Force Close

`forceCloseFocusedWindow()` sends `SIGTERM` to the focused window's PID — a polite request for the process to exit (it can save state, flush buffers, etc.). There is no escalation to `SIGKILL`; if the process ignores `SIGTERM`, the user can press the button again or close it manually.

Before sending the signal, two safety checks run:

1. **Blocklist check:** The process name (from `/proc/<pid>/comm`) is compared against a hardcoded set of protected system processes (KDE, Wayland, PulseAudio, systemd, etc.). If matched, the kill is aborted and a warning is logged.
2. **TOCTOU re-check:** The process name is read a second time just before the `kill()` call. If the PID was recycled between the blocklist check and the kill, the names won't match and the kill is aborted. This guards against a race where PID reuse could cause `SIGTERM` to hit an unrelated process.

---

## 11. Visual Feedback — Overlay

**Files:** `Overlay.h`, `Overlay.cpp`

Overlay provides visual feedback when a knob is turned, showing the current volume percentage on screen.

### KDE OSD Integration

Instead of drawing its own window, Overlay uses KDE Plasma's built-in on-screen display system — the same system that shows a volume popup when you press your keyboard's volume keys. This is done via a D-Bus call:

```cpp
QDBusMessage msg = QDBusMessage::createMethodCall(
    "org.kde.plasmashell",        // service
    "/org/kde/osdService",        // object path
    "org.kde.osdService",         // interface
    "mediaPlayerVolumeChanged"    // method
);
msg << percent << text << icon;
QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
```

`mediaPlayerVolumeChanged` is the only method on this interface that shows a progress bar with a custom icon. It takes three arguments: the current percent (0–100), a label string, and a freedesktop icon name. `QDBus::NoBlock` sends the call without waiting for a response — we don't need confirmation that the OSD was shown.

### App Icon Resolution

When `showVolume()` is called with a process name (e.g. `"discord"`), `resolveAppIcon()` searches XDG application directories (including Flatpak export paths) for a `.desktop` file matching that process name, then extracts its `Icon=` value. This means the OSD popup shows the app's actual icon instead of a generic volume icon. Results are cached so repeated knob turns don't hit the filesystem. If no desktop file is found, the process name itself is used as the icon name — which works for native apps whose binary name matches their icon name.

### When the OSD Is Suppressed

In `main.cpp`, the overlay is deliberately *not* shown for system volume changes:

```cpp
if (cfg.knobs[knob].type != "system") {
    // Resolve a process name for the icon (from config targets or focused window)
    std::string target;
    if (!kc.targets.empty())
        target = kc.targets.front();
    else if (kc.type == "focused") {
        int pid = focusMonitor.getPID();
        if (pid > 0) target = getProcessName(pid);
    }
    overlay.showVolume(vol, target);
}
```

This is because changing the system volume through PulseAudio already triggers KDE's own volume OSD. Showing our overlay too would cause two popups to appear simultaneously. The `target` string is passed to `showVolume()` so the OSD can display the correct app icon.

---

## 12. The Entry Point — main.cpp

`main.cpp` is the orchestrator. It constructs all components, wires them together, and manages the application lifecycle. Here's the sequence, step by step:

### 1. Create the Qt Application

```cpp
QCoreApplication app(argc, argv);
```

This is required for Qt's D-Bus support. It sets up the event loop that FocusMonitor's `QTimer` and D-Bus callbacks depend on. `QCoreApplication` (as opposed to `QApplication`) is the headless variant — no GUI widgets, just the event loop.

### 2. Prevent Zombie Processes

```cpp
struct sigaction sa_chld{};
sa_chld.sa_handler = SIG_IGN;
sa_chld.sa_flags = SA_NOCLDWAIT;
sigaction(SIGCHLD, &sa_chld, nullptr);
```

When ButtonHandler spawns child processes (playerctl, ydotool), it never calls `wait()`. Without `SA_NOCLDWAIT`, finished children would linger as zombies. This flag tells the kernel to reap them automatically.

### 3. Load Configuration

```cpp
ConfigManager configMgr;
if (!configMgr.load(configPath)) { return 1; }
```

If config loading fails, the daemon exits immediately. There's nothing useful it can do without knowing its configuration.

### 4. Initialize Logger

```cpp
std::string logPath = cfg.logFile.empty() ? resolveLogPath() : cfg.logFile;
Logger::instance().init(logPath);
```

If the config specifies a custom `logFile` path, that's used directly. Otherwise `resolveLogPath()` returns the XDG state directory default (`~/.local/state/audiokontroller/audiokontroller.log`).

### 5. Construct Components

```cpp
Overlay overlay;
AudioHandler audio;
audio.init();
ButtonHandler button;
FocusMonitor focusMonitor;
```

Order matters for two reasons:
- `FocusMonitor` is constructed after `AudioHandler` and `ButtonHandler` because it starts D-Bus services in its constructor.
- Destruction happens in reverse order (LIFO). The `PCPanelHandler` (constructed next) must be stopped before `AudioHandler` is destroyed, because the HID thread's callback calls into AudioHandler.

### 6. Inject Dependencies

```cpp
button.setGetPIDFunc([&focusMonitor]() { return focusMonitor.getPID(); });
audio.setGetPIDFunc([&focusMonitor]() { return focusMonitor.getPID(); });
```

These lambdas capture a reference to `focusMonitor`. When ButtonHandler or AudioHandler calls `getPID()`, the lambda calls through to `focusMonitor.getPID()`. This is how the PID dependency is injected without creating a compile-time dependency between the classes.

### 7. Register Signal Handlers

```cpp
globalPanel = &panel;
sigaction(SIGTERM, &sa, nullptr);
sigaction(SIGINT,  &sa, nullptr);
```

- `SIGTERM` is sent by systemd when you run `systemctl stop`
- `SIGINT` is sent by Ctrl+C in a terminal

The signal handler calls `panel.requestStop()` (writes an atomic bool) and `QCoreApplication::quit()` (tells the event loop to exit). Both operations are safe to call from a signal handler context.

`globalPanel` is a raw global pointer because signal handlers can't capture lambdas or access member variables — they can only use global state.

### 8. Register Callbacks

```cpp
panel.setCallback([&](int knob, float vol) {
    if (knob < 0 || knob >= static_cast<int>(cfg.knobs.size())) return;
    audio.handleKnob(cfg.knobs[knob], vol);
    if (cfg.knobs[knob].type != "system") {
        // Resolve a target process name for the OSD icon
        std::string target;
        if (!kc.targets.empty()) target = kc.targets.front();
        else if (kc.type == "focused") {
            int pid = focusMonitor.getPID();
            if (pid > 0) target = getProcessName(pid);
        }
        overlay.showVolume(vol, target);
    }
});

panel.setButtonCallback([&](int btn) {
    if (btn >= 0 && btn < static_cast<int>(cfg.buttons.size()))
        button.handleButton(cfg.buttons[btn]);
});
```

These are the core connections. When the HID thread detects a knob movement, it calls the knob callback, which dispatches to AudioHandler and Overlay (with the focused app's icon). When it detects a button press, it calls the button callback, which dispatches to ButtonHandler.

Both callbacks include bounds checking to handle cases where the device has more controls than the config defines.

### 9. Start and Run

```cpp
panel.startListeningAsync();  // launches HID read thread
int ret = app.exec();         // blocks: runs Qt event loop on main thread
panel.stopListening();        // joins HID thread after event loop exits
```

The main thread is now dedicated to the Qt event loop, handling D-Bus callbacks and QTimer events. The HID read loop runs in its own thread. When a signal is received, the signal handler makes both stop: the HID thread via `requestStop()`, and the event loop via `QCoreApplication::quit()`.

---

## 13. Threading Model

The daemon uses three threads. Understanding which code runs on which thread is essential for debugging and for avoiding race conditions when making changes.

### Thread 1: Main Thread (Qt Event Loop)

- Runs `QCoreApplication::exec()`
- Handles all D-Bus traffic: FocusMonitor's `SetPID` slot, Overlay's OSD calls
- Runs FocusMonitor's retry `QTimer`
- **Rule:** Never do anything that blocks for a long time on this thread, or D-Bus callbacks will stall.

### Thread 2: HID Read Thread

- Created by `PCPanelHandler::startListeningAsync()`
- Runs the `readLoop()` method continuously
- Fires knob and button callbacks, which call into AudioHandler, ButtonHandler, and Overlay
- **Rule:** All code called from a callback (directly or indirectly) runs on this thread. Keep callbacks fast.

### Thread 3: PulseAudio Internal Thread

- Created by `pa_threaded_mainloop_start()` inside `AudioHandler::init()`
- Runs PulseAudio's event loop; fires PA callbacks (`sinkInputInfoCallback`, `sinkInfoCallback`)
- **Rule:** Never call PA functions without holding the mainloop lock (except from inside PA callbacks, which already hold it).

### Synchronization Mechanisms

| Shared State | Accessed By | Synchronization |
|-------------|-------------|-----------------|
| `activePID` (FocusMonitor) | Main thread (D-Bus write), HID thread (read) | `std::atomic<int>` with relaxed ordering |
| `running` (PCPanelHandler) | Signal handler (write), HID thread (read) | `std::atomic<bool>` |
| `knobThreshold` (PCPanelHandler) | Main thread (write at startup), HID thread (read) | `std::atomic<int>` |
| `targetApps`, `targetVolume` (AudioHandler) | HID thread (write), PA thread (read) | PA mainloop lock |
| Log file (Logger) | Any thread | `std::mutex` |

**Why relaxed ordering for `activePID`?** The PID is a single integer that doesn't guard any other data. We don't need happens-before guarantees — we just need the read to eventually see the write. Relaxed ordering is the cheapest atomic operation and is sufficient here.

---

## 14. Data Flow: From Knob Turn to Volume Change

To tie everything together, here's the complete journey of a knob event through the system, using the "focused window" knob type as the most complex example:

```
1. USER turns a physical knob on the PCPanel Mini

2. HARDWARE sends a 64-byte HID report over USB:
   [0x01, 0x02, 0xB3, ...]
   (type=knob, index=2, value=179)

3. PCPanelHandler::readLoop() [HID thread]
   - hid_read_timeout() returns 3+ bytes
   - Skips if within first 20 reads (startup burst)
   - Calls processKnobEvent(2, 179)

4. PCPanelHandler::processKnobEvent() [HID thread]
   - Checks dead zone: |179 - lastValue[2]| >= 4? Yes.
   - Updates lastValue[2] = 179
   - Normalizes: 179 / 255.0 = 0.702
   - Calls callback(2, 0.702)

5. Knob callback lambda (registered in main.cpp) [HID thread]
   - Looks up cfg.knobs[2] -> KnobConfig { type: "focused" }
   - Calls audio.handleKnob(knobConfig, 0.702)
   - Calls overlay.showVolume(0.702)

6. AudioHandler::handleKnob() [HID thread]
   - type == "focused", so call getPID()
   - getPID() lambda calls focusMonitor.getPID()
   - Returns activePID (e.g., 12345) via atomic load

7. getProcessName(12345) [HID thread]
   - Reads /proc/12345/comm -> "discord"

8. AudioHandler::setVolumeForApps({"discord"}, 0.702) [HID thread]
   - Locks PA mainloop
   - Sets targetApps = {"discord"}, targetVolume = 0.702
   - Calls requestSinkInputs()
   - Unlocks PA mainloop

9. PulseAudio enumerates streams [PA thread]
   - For each active audio stream, calls sinkInputInfoCallback()
   - Checks stream properties: application.process.binary = "discord"
   - "discord" contains "discord" (case-insensitive) -> match!
   - Sets stream volume to 0.702 * PA_VOLUME_NORM = 46006

10. Overlay::showVolume(0.702) [HID thread]
    - Sends D-Bus call to KDE Plasma OSD
    - Shows "Volume: 70%" popup on screen

11. USER hears Discord's audio at 70% volume
```

---

## 15. Installation and Deployment

The `install.sh` script handles the complete setup on Fedora Linux. Understanding it helps if you need to install on a different distro or debug permission issues.

### System Dependencies

```bash
sudo dnf install -y cmake gcc-c++ qt6-qtbase-devel qt6-qttools-devel \
    pulseaudio-libs-devel hidapi-devel pkgconf-pkg-config playerctl ydotool
```

These are the build-time and runtime dependencies. On non-Fedora distros, package names will differ.

### USB Permissions (udev Rules)

By default, only root can access USB HID devices. The install script creates udev rules that grant access to the `uinput` group:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="a3c4", GROUP="uinput", MODE="0660"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="a3c4", GROUP="uinput", MODE="0660"
```

The user is added to the `uinput` group. A logout/login is required for group membership to take effect.

### ydotool Daemon

`ydotool` requires a background daemon (`ydotoold`) running under the user's session. The install script creates a systemd user service for it. ydotool is used by ButtonHandler's `sendKeys` action to simulate keyboard input.

### Build and Install

The binary is built with CMake and installed to `~/.local/bin/AudioKontroller.d/audiokontrollerd`. The config file is copied to the same directory on first install only (to avoid overwriting user edits).

### systemd Service

A user-level systemd service is created at `~/.config/systemd/user/audiokontroller.service`. Key settings:

- `After=graphical-session.target ydotoold.service` — ensures the graphical session and ydotool daemon are up first
- `Restart=on-failure` — automatically restarts if the daemon crashes
- `NoNewPrivileges=yes` — prevents privilege escalation
- `ProtectSystem=strict` — makes the entire filesystem read-only except explicitly allowed paths
- `ReadWritePaths=` — allows writing only to the install directory (for logs and config)

### CLI Wrapper

A convenience script at `~/.local/bin/AudioKontroller` provides commands:
- `AudioKontroller start|stop|restart|status` — control the service
- `AudioKontroller config` — open config.json in your editor
- `AudioKontroller log` — view the log file

---

## 16. Guide for Contributors

### Adding a New Knob Type

1. Add the new type string to `KnobConfig::type` documentation in `ConfigManager.h`
2. Add a new `else if` branch in `AudioHandler::handleKnob()` (in `AudioHandler.cpp`)
3. If your type needs new PulseAudio operations, follow the existing pattern: lock the mainloop, set your state, kick off a PA operation, unlock
4. Update the default config in `ConfigManager::createDefault()` if it makes sense as a default

### Adding a New Button Action

1. Add the new action string to `ButtonConfig::action` documentation in `ConfigManager.h`
2. Add a new `else if` branch in `ButtonHandler::handleButton()`
3. Implement the action method. If it runs an external program, use `forkExec()`. If it might block, run it in a detached thread
4. If it needs the focused window PID, use the injected `getPID()` function

### Adding a New Device Variant

1. Add a new entry to the `PCPanelDevice` enum in `PCPanelHandler.h`
2. Add the VID/PID in `PCPanelHandler::getVidPid()`
3. Add the string mapping in `main.cpp`'s `deviceFromString()`
4. Add udev rules in `install.sh`

### Key Safety Rules

- **Never block the HID thread** for more than a few milliseconds. Use detached threads for long operations.
- **Always lock the PA mainloop** before reading or writing `targetApps`, `targetVolume`, or `isSystem`.
- **Use `forkExec()` for external commands**, never `std::system()`.
- **Use `_exit()` in fork children**, never `exit()`.
- **Test with the physical device** — some bugs only manifest with real hardware timing.

### File Quick Reference

| File | Lines | Role |
|------|-------|------|
| `main.cpp` | 160 | Entry point, wiring, lifecycle |
| `ConfigManager.h/cpp` | 75 + 155 | JSON config parsing |
| `Logger.h/cpp` | 50 + 75 | Thread-safe file logging |
| `PCPanelHandler.h/cpp` | 99 + 151 | USB HID device input |
| `AudioHandler.h/cpp` | 92 + 253 | PulseAudio volume control |
| `FocusMonitor.h/cpp` | 79 + 151 | KWin focus tracking via D-Bus |
| `ButtonHandler.h/cpp` | 59 + 199 | Button action execution |
| `Overlay.h/cpp` | 22 + 35 | KDE OSD volume display |
| `config.json` | 17 | Runtime configuration |
| `CMakeLists.txt` | 48 | Build configuration |
| `install.sh` | 162 | Installation and deployment |
