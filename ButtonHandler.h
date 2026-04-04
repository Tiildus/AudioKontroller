#pragma once
#include <vector>
#include <string>
#include <functional>

class ButtonHandler {
public:
    ButtonHandler() = default;

    // Set a function that returns the focused window PID (injected from main)
    void setGetPIDFunc(std::function<int()> func) { getPID = func; }

    void toggleMediaPlayPause();
    void sendKeySequence(const std::vector<std::string>& keys);
    void forceCloseFocusedWindow();

private:
    std::function<int()> getPID;

    // Fork + execvp, parent returns immediately (fire-and-forget)
    static void forkExec(const std::vector<std::string>& argv);
};
