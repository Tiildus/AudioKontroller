// =============================================================================
// ButtonHandler.cpp — Button action implementations
// =============================================================================

#include "ButtonHandler.h"
#include "Logger.h"
#include <unistd.h>   // fork, execvp, _exit
#include <signal.h>   // kill, SIGTERM, SIGKILL
#include <thread>
#include <chrono>

// Spawns an external program without going through a shell.
// Steps:
//   1. fork() duplicates this process. Both parent and child continue here.
//   2. In the child (pid == 0): build a null-terminated C argv array and call
//      execvp(), which replaces the child process image with the new program.
//   3. In the parent: fork() returns the child's PID. We ignore it and return.
//
// SA_NOCLDWAIT (set in main.cpp) prevents the child from becoming a zombie
// after it exits, since we never call wait() on it.
//
// We use _exit() instead of exit() in the child's error path because exit()
// flushes C++ destructors and stdio buffers from the parent process — running
// those in a forked child would corrupt shared file handles.
void ButtonHandler::forkExec(const std::vector<std::string>& argv) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process: build a null-terminated C-style argv array.
        // execvp() requires this format.
        std::vector<const char*> args;
        for (const auto& a : argv) args.push_back(a.c_str());
        args.push_back(nullptr);
        execvp(args[0], const_cast<char* const*>(args.data()));
        // If execvp returns, the exec failed (e.g. program not found).
        _exit(127); // 127 is the conventional "command not found" exit code
    }
    // Parent returns immediately. No wait() needed thanks to SA_NOCLDWAIT.
}

// Dispatches a button press to the appropriate action based on config.
void ButtonHandler::handleButton(const ButtonConfig& bc) {
    if (bc.action == "mediaPlayPause") {
        toggleMediaPlayPause();
    } else if (bc.action == "sendKeys") {
        sendKeySequence(bc.args);
    } else if (bc.action == "forceClose") {
        forceCloseFocusedWindow();
    }
    // "none" action intentionally does nothing
}

void ButtonHandler::toggleMediaPlayPause() {
    forkExec({"playerctl", "play-pause"});
    Logger::instance().info("ButtonHandler", "Toggled Play/Pause");
}

// Builds the full ydotool command from the args in config.json and runs it.
// Because we use execvp (not a shell), the args are passed literally — no
// risk of shell injection from config values.
void ButtonHandler::sendKeySequence(const std::vector<std::string>& args) {
    std::vector<std::string> argv = {"ydotool"};
    argv.insert(argv.end(), args.begin(), args.end());
    forkExec(argv);

    std::string desc = "ydotool";
    for (const auto& a : args) desc += " " + a;
    Logger::instance().info("ButtonHandler", "Dispatched: " + desc);
}

// Gracefully terminates the focused window's process:
//   1. SIGTERM — politely asks the process to exit (it can save state, etc.)
//   2. Wait 1 second.
//   3. If still running, SIGKILL — forcibly terminates it.
//
// This runs in a detached thread so the 1-second sleep doesn't stall the
// HID read loop (which would make knobs and other buttons feel unresponsive).
void ButtonHandler::forceCloseFocusedWindow() {
    if (!getPID) {
        Logger::instance().warn("ButtonHandler", "No PID source configured");
        return;
    }
    int pid = getPID();
    if (pid <= 0) {
        Logger::instance().warn("ButtonHandler", "No focused PID found");
        return;
    }

    std::thread([pid]() {
        kill(pid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        // kill(pid, 0) doesn't send a signal; it just checks if the process
        // still exists. Returns 0 if it does, -1 if it's gone.
        if (kill(pid, 0) == 0) {
            kill(pid, SIGKILL);
        }
        Logger::instance().info("ButtonHandler", "Force closed PID: " + std::to_string(pid));
    }).detach(); // detach so this thread cleans itself up when done
}
