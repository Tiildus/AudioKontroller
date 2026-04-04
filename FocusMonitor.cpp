<<<<<<< HEAD
// =============================================================================
// FocusMonitor.cpp — KWin D-Bus script lifecycle and focused PID tracking
// =============================================================================

=======
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
#include "FocusMonitor.h"
#include "Logger.h"
#include <QDBusMessage>
#include <QDBusReply>
#include <QFile>
#include <QTextStream>

<<<<<<< HEAD
// KWin's D-Bus service and scripting interface constants.
static const char* KWIN_SERVICE        = "org.kde.KWin";
static const char* KWIN_SCRIPTING_PATH = "/Scripting";
static const char* KWIN_SCRIPTING_IFACE = "org.kde.kwin.Scripting";

// Name used to identify our script in KWin. Must be unique among loaded scripts.
=======
static const char* KWIN_SERVICE = "org.kde.KWin";
static const char* KWIN_SCRIPTING_PATH = "/Scripting";
static const char* KWIN_SCRIPTING_IFACE = "org.kde.kwin.Scripting";
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
static const char* SCRIPT_NAME = "audiokontroller-focus";

FocusMonitor::FocusMonitor(const std::string& scriptDir, QObject* parent)
    : QObject(parent)
{
    scriptPath = scriptDir + "/focus-monitor.js";

<<<<<<< HEAD
    // --- Register our D-Bus presence ---
    // D-Bus has two steps: claiming a service name (the "address" on the bus)
    // and registering an object (the actual endpoint that handles method calls).
    QDBusConnection bus = QDBusConnection::sessionBus();

    // registerService claims "com.audiokontroller.FocusMonitor" as our unique
    // name on the session bus. The KWin script addresses its callDBus() to this.
=======
    // Register our D-Bus service so KWin script can call us
    QDBusConnection bus = QDBusConnection::sessionBus();
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    if (!bus.registerService("com.audiokontroller.FocusMonitor")) {
        Logger::instance().error("FocusMonitor", "Failed to register D-Bus service");
        return;
    }
<<<<<<< HEAD

    // registerObject maps incoming D-Bus calls at path "/FocusMonitor" to the
    // slots on this QObject. ExportAllSlots exposes every public Q_SLOT.
=======
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    if (!bus.registerObject("/FocusMonitor", this, QDBusConnection::ExportAllSlots)) {
        Logger::instance().error("FocusMonitor", "Failed to register D-Bus object");
        return;
    }

<<<<<<< HEAD
    // --- Write the KWin JavaScript script ---
    // This script is loaded into KWin and runs inside its scripting engine.
    // It connects to KWin's windowActivated signal and fires a D-Bus call
    // back to us with the newly focused window's PID every time focus changes.
=======
    // Write the KWin script file
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    QFile file(QString::fromStdString(scriptPath));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream out(&file);
        out << "workspace.windowActivated.connect(function(window) {\n"
            << "    if (window) {\n"
            << "        callDBus(\"com.audiokontroller.FocusMonitor\", \"/FocusMonitor\",\n"
            << "                 \"com.audiokontroller.FocusMonitor\", \"SetPID\", window.pid);\n"
            << "    }\n"
            << "});\n";
        file.close();
    } else {
        Logger::instance().error("FocusMonitor", "Failed to write KWin script to " + scriptPath);
        return;
    }

<<<<<<< HEAD
    // --- Load the script into KWin ---
    // Try immediately. If KWin isn't on D-Bus yet (startup race), set a timer
    // to retry periodically rather than blocking or failing permanently.
=======
    // Try loading immediately; if KWin isn't on D-Bus yet, retry with a timer
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    if (loadKWinScript()) {
        Logger::instance().warn("FocusMonitor", "KWin focus script loaded");
    } else {
        Logger::instance().warn("FocusMonitor",
            "KWin not available yet, will retry every 2s (up to 30s)");
        connect(&retryTimer, &QTimer::timeout, this, &FocusMonitor::retryLoadKWinScript);
        retryTimer.start(RETRY_INTERVAL_MS);
    }
}

FocusMonitor::~FocusMonitor() {
<<<<<<< HEAD
    // Remove the script from KWin so it doesn't keep calling our D-Bus service
    // after we've shut down.
=======
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    unloadKWinScript();
    QDBusConnection::sessionBus().unregisterObject("/FocusMonitor");
    QDBusConnection::sessionBus().unregisterService("com.audiokontroller.FocusMonitor");
}

<<<<<<< HEAD
// Called by the KWin JavaScript script over D-Bus whenever window focus changes.
// Stores the PID atomically so the HID thread can read it without locking.
=======
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
void FocusMonitor::SetPID(int pid) {
    activePID.store(pid, std::memory_order_relaxed);
}

<<<<<<< HEAD
// Retry slot — fired by retryTimer every RETRY_INTERVAL_MS milliseconds.
// Runs on the main thread (Qt event loop), so it's safe to make D-Bus calls.
=======
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
void FocusMonitor::retryLoadKWinScript() {
    retryCount++;
    if (loadKWinScript()) {
        retryTimer.stop();
        Logger::instance().warn("FocusMonitor",
            "KWin focus script loaded (after " + std::to_string(retryCount) +
            (retryCount == 1 ? " retry)" : " retries)"));
        return;
    }
<<<<<<< HEAD
    // If we've exhausted retries, stop trying and log a permanent failure.
=======
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    if (retryCount >= MAX_RETRIES) {
        retryTimer.stop();
        Logger::instance().error("FocusMonitor",
            "KWin not available after " + std::to_string(MAX_RETRIES) +
            " retries, focused window tracking disabled");
    }
}

<<<<<<< HEAD
// Sends two D-Bus calls to KWin:
//   1. loadScript(path, name) — registers the JS file and returns a script ID
//   2. run()                  — activates the script so its event hooks fire
// Returns false without logging if KWin is unavailable (caller handles logging).
bool FocusMonitor::loadKWinScript() {
    // Call KWin's loadScript method with the absolute path to our JS file.
    // KWin returns an integer script ID that we need for the run() call.
=======
bool FocusMonitor::loadKWinScript() {
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    QDBusMessage msg = QDBusMessage::createMethodCall(
        KWIN_SERVICE, KWIN_SCRIPTING_PATH, KWIN_SCRIPTING_IFACE, "loadScript");
    msg << QString::fromStdString(scriptPath) << QString(SCRIPT_NAME);

    QDBusReply<int> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
<<<<<<< HEAD
        return false; // KWin not available yet; caller decides whether to retry or give up
    }
    scriptId = reply.value();

    // Scripts loaded into KWin are dormant until run() is called.
    // The script object lives at "/Scripting/Script{id}" on KWin's bus.
=======
        return false;
    }
    scriptId = reply.value();

    // Run the loaded script
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5
    QString scriptObjPath = QString("/Scripting/Script%1").arg(scriptId);
    QDBusMessage runMsg = QDBusMessage::createMethodCall(
        KWIN_SERVICE, scriptObjPath, "org.kde.kwin.Script", "run");
    QDBusConnection::sessionBus().call(runMsg);

    return true;
}

<<<<<<< HEAD
// Unloads the script by name. After this, the windowActivated hook is removed
// and KWin will stop calling our SetPID slot.
void FocusMonitor::unloadKWinScript() {
    if (scriptId < 0) return; // script was never successfully loaded
=======
void FocusMonitor::unloadKWinScript() {
    if (scriptId < 0) return;
>>>>>>> 19608d98419239a49247ade622cad99dd04757f5

    QDBusMessage msg = QDBusMessage::createMethodCall(
        KWIN_SERVICE, KWIN_SCRIPTING_PATH, KWIN_SCRIPTING_IFACE, "unloadScript");
    msg << QString(SCRIPT_NAME);
    QDBusConnection::sessionBus().call(msg);

    scriptId = -1;
}
