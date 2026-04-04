#include "ButtonHandler.h"
#include "Logger.h"
#include <unistd.h>
#include <signal.h>
#include <thread>
#include <chrono>

void ButtonHandler::forkExec(const std::vector<std::string>& argv) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child: build null-terminated argv array and exec
        std::vector<const char*> args;
        for (const auto& a : argv) args.push_back(a.c_str());
        args.push_back(nullptr);
        execvp(args[0], const_cast<char* const*>(args.data()));
        _exit(127); // exec failed
    }
    // Parent returns immediately. SA_NOCLDWAIT in main prevents zombies.
}

void ButtonHandler::toggleMediaPlayPause() {
    forkExec({"playerctl", "play-pause"});
    Logger::instance().info("ButtonHandler", "Toggled Play/Pause");
}

void ButtonHandler::sendKeySequence(const std::vector<std::string>& args) {
    std::vector<std::string> argv = {"ydotool"};
    argv.insert(argv.end(), args.begin(), args.end());
    forkExec(argv);

    std::string desc = "ydotool";
    for (const auto& a : args) desc += " " + a;
    Logger::instance().info("ButtonHandler", "Dispatched: " + desc);
}

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

    // Run kill sequence in a detached thread so HID loop is not blocked
    std::thread([pid]() {
        kill(pid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        // Only SIGKILL if process still exists
        if (kill(pid, 0) == 0) {
            kill(pid, SIGKILL);
        }
        Logger::instance().info("ButtonHandler", "Force closed PID: " + std::to_string(pid));
    }).detach();
}
