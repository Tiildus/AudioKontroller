#include "FocusMonitor.h"
#include "Logger.h"
#include <QDBusMessage>
#include <QDBusReply>
#include <QFile>
#include <QTextStream>

static const char* KWIN_SERVICE = "org.kde.KWin";
static const char* KWIN_SCRIPTING_PATH = "/Scripting";
static const char* KWIN_SCRIPTING_IFACE = "org.kde.kwin.Scripting";
static const char* SCRIPT_NAME = "audiokontroller-focus";

FocusMonitor::FocusMonitor(const std::string& scriptDir, QObject* parent)
    : QObject(parent)
{
    scriptPath = scriptDir + "/focus-monitor.js";

    // Register our D-Bus service so KWin script can call us
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService("com.audiokontroller.FocusMonitor")) {
        Logger::instance().error("FocusMonitor", "Failed to register D-Bus service");
        return;
    }
    if (!bus.registerObject("/FocusMonitor", this, QDBusConnection::ExportAllSlots)) {
        Logger::instance().error("FocusMonitor", "Failed to register D-Bus object");
        return;
    }

    // Write the KWin script file
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

    // Try loading immediately; if KWin isn't on D-Bus yet, retry with a timer
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
    unloadKWinScript();
    QDBusConnection::sessionBus().unregisterObject("/FocusMonitor");
    QDBusConnection::sessionBus().unregisterService("com.audiokontroller.FocusMonitor");
}

void FocusMonitor::SetPID(int pid) {
    activePID.store(pid, std::memory_order_relaxed);
}

void FocusMonitor::retryLoadKWinScript() {
    retryCount++;
    if (loadKWinScript()) {
        retryTimer.stop();
        Logger::instance().warn("FocusMonitor",
            "KWin focus script loaded (after " + std::to_string(retryCount) +
            (retryCount == 1 ? " retry)" : " retries)"));
        return;
    }
    if (retryCount >= MAX_RETRIES) {
        retryTimer.stop();
        Logger::instance().error("FocusMonitor",
            "KWin not available after " + std::to_string(MAX_RETRIES) +
            " retries, focused window tracking disabled");
    }
}

bool FocusMonitor::loadKWinScript() {
    QDBusMessage msg = QDBusMessage::createMethodCall(
        KWIN_SERVICE, KWIN_SCRIPTING_PATH, KWIN_SCRIPTING_IFACE, "loadScript");
    msg << QString::fromStdString(scriptPath) << QString(SCRIPT_NAME);

    QDBusReply<int> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        return false;
    }
    scriptId = reply.value();

    // Run the loaded script
    QString scriptObjPath = QString("/Scripting/Script%1").arg(scriptId);
    QDBusMessage runMsg = QDBusMessage::createMethodCall(
        KWIN_SERVICE, scriptObjPath, "org.kde.kwin.Script", "run");
    QDBusConnection::sessionBus().call(runMsg);

    return true;
}

void FocusMonitor::unloadKWinScript() {
    if (scriptId < 0) return;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        KWIN_SERVICE, KWIN_SCRIPTING_PATH, KWIN_SCRIPTING_IFACE, "unloadScript");
    msg << QString(SCRIPT_NAME);
    QDBusConnection::sessionBus().call(msg);

    scriptId = -1;
}
