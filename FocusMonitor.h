#pragma once
#include <QObject>
#include <QDBusConnection>
#include <QTimer>
#include <atomic>
#include <string>

class FocusMonitor : public QObject {
    Q_OBJECT
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
    void unloadKWinScript();
};
