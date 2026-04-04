<<<<<<< HEAD
// =============================================================================
// FocusMonitor.h — Event-driven focused window PID tracking via KWin D-Bus
//
// Problem: Wayland doesn't expose which window has focus to arbitrary processes.
// KDE's window manager (KWin) does have access to this, but we need a way to
// get that information without polling.
//
// Solution: We load a small JavaScript snippet into KWin at startup. That
// script hooks KWin's "windowActivated" event and calls back to us over D-Bus
// whenever the active window changes. We store the PID atomically, so the HID
// thread can read it instantly with no locking.
//
// Architecture:
//   KWin (JS script)  --D-Bus-->  FocusMonitor::SetPID  -->  activePID (atomic)
//                                                              ^
//                                 HID thread reads getPID() --+
//
// Startup race: KWin may not be on D-Bus yet when the daemon starts (e.g. if
// the daemon is auto-started very early). A QTimer retries the script load
// every 2 seconds for up to 30 seconds.
// =============================================================================

=======
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
#pragma once
#include <QObject>
#include <QDBusConnection>
#include <QTimer>
#include <atomic>
#include <string>

class FocusMonitor : public QObject {
    Q_OBJECT
<<<<<<< HEAD
    // This annotation tells Qt's MOC (Meta-Object Compiler) what D-Bus interface
    // name to expose for this class. KWin's script uses this name in callDBus().
    Q_CLASSINFO("D-Bus Interface", "com.audiokontroller.FocusMonitor")

public:
    // scriptDir: directory where focus-monitor.js will be written.
    //            Should be the install directory so it persists across runs.
    explicit FocusMonitor(const std::string& scriptDir, QObject* parent = nullptr);
    ~FocusMonitor();

    // Returns the PID of the currently focused window, or -1 if unknown.
    // Uses a relaxed atomic load — safe and lock-free from any thread.
    int getPID() const { return activePID.load(std::memory_order_relaxed); }

    // Returns true if the KWin script was successfully loaded and is running.
    bool isScriptLoaded() const { return scriptId >= 0; }

public Q_SLOTS:
    // D-Bus slot called by the KWin JavaScript script when the active window changes.
    // "Q_SLOTS" exposes this to Qt's signal/slot system; the D-Bus layer routes
    // incoming D-Bus method calls to it automatically.
    void SetPID(int pid);

private Q_SLOTS:
    // Fired by retryTimer — attempts to load the KWin script again.
    void retryLoadKWinScript();

private:
    // Atomic so the HID read thread can call getPID() without a mutex.
    std::atomic<int> activePID{-1};

    // ID assigned by KWin when the script is loaded — needed to run and unload it.
    int scriptId = -1;

    // Absolute path to the focus-monitor.js file on disk.
    std::string scriptPath;

    // Used to retry the KWin D-Bus call if KWin isn't available at startup.
    QTimer retryTimer;
    int retryCount = 0;
    static constexpr int MAX_RETRIES        = 15; // give up after ~30 seconds
    static constexpr int RETRY_INTERVAL_MS  = 2000;

    // Attempts to register the JS script with KWin and run it.
    // Returns true on success, false if KWin is unavailable or the call fails.
    bool loadKWinScript();

    // Unregisters the script from KWin. Called on destruction.
=======
    Q_CLASSINFO("D-Bus Interface", "com.audiokontroller.FocusMonitor")

public:
    explicit FocusMonitor(const std::string& scriptDir, QObject* parent = nullptr);
    ~FocusMonitor();

    int getPID() const { return activePID.load(std::memory_order_relaxed); }

public Q_SLOTS:
    // Called by KWin script via D-Bus when the active window changes
    void SetPID(int pid);

private Q_SLOTS:
    void retryLoadKWinScript();

private:
    std::atomic<int> activePID{-1};
    int scriptId = -1;
    std::string scriptPath;
    QTimer retryTimer;
    int retryCount = 0;
    static constexpr int MAX_RETRIES = 15;
    static constexpr int RETRY_INTERVAL_MS = 2000;

    bool loadKWinScript();
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    void unloadKWinScript();
};
