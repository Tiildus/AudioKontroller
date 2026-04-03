#include "WindowUtils.h"
#include <cstdlib>

int getFocusedWindowPID() {
    FILE* pipe = popen("kdotool getactivewindow getwindowpid", "r");
    if (!pipe) return -1;
    char buffer[128];
    std::string result = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    try {
        return std::stoi(result);
    } catch (...) {
        return -1;
    }
}
